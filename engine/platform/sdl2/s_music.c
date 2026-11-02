/* Independent MP3 music. Only the main thread touches engine FS or pools;
 * the worker owns its decoders, and the audio callback only consumes PCM.
 * A separate SDL output device keeps the game's long DMA lock off this path. */
#include "common.h"
#include "sound.h"
#include "client.h"
#include "sound/music_async.h"
#include <SDL.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#define MPG123_PORTABLE_API 1
#include <mpg123.h>

#define BGM_BLOCKS 32u
#define BGM_FRAMES 2048u
#define BGM_MAX_FILE (64u * 1024u * 1024u)
typedef struct
{
	byte *data; size_t size, cursor;
	mpg123_handle *decoder;
	SDL_AudioStream *converter; long rate; int channels;
	string name;
} bgm_source_t;
typedef struct
{
	short pcm[BGM_FRAMES * 2];
	unsigned frames; int track; int64_t position;
} bgm_block_t;
typedef struct
{
	bgm_source_t source[2]; qboolean shared, loop;
	SDL_AudioDeviceID device; SDL_Thread *worker;
	SDL_atomic_t stop, read, written, paused, gain, active, eof, finished;
	SDL_atomic_t callbacks, played, underruns, nonzero, track, position, loops;
	unsigned offset, fadeStart, fadeDuration;
	bgm_block_t blocks[BGM_BLOCKS];
} bgm_state_t;
static bgm_state_t *bgm;

static int BGM_Read( void *opaque, void *buffer, size_t bytes, size_t *done )
{
	bgm_source_t *s=opaque;*done=Q_min(bytes,s->size-s->cursor);
	memcpy(buffer,s->data+s->cursor,*done);s->cursor+=*done;return 0;
}
static int64_t BGM_Seek( void *opaque, int64_t offset, int whence )
{
	bgm_source_t *s=opaque;int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?(int64_t)s->cursor:whence==SEEK_END?(int64_t)s->size:-1;
	if(base<0||offset < -base||offset>(int64_t)s->size-base)return -1;
	s->cursor=(size_t)(base+offset);return s->cursor;
}
static void BGM_FreeSource( bgm_source_t *s )
{
	if(s->decoder)mpg123_delete(s->decoder);
	if(s->converter)SDL_FreeAudioStream(s->converter);
	free(s->data);memset(s,0,sizeof(*s));
}
static qboolean BGM_Load( bgm_source_t *s, const char *name, size_t maximum )
{
	const char *ext=COM_FileExtension(name);string path;
	if(*ext&&Q_stricmp(ext,"mp3"))return false;
	Q_snprintf(path,sizeof(path),*ext?"%s":"%s.mp3",name);
	file_t *file=FS_Open(path,"rb",false);
	if(!file&&Q_strncmp(path,"media/",6))
	{
		string relative;Q_snprintf(relative,sizeof(relative),"media/%s",path);
		file=FS_Open(relative,"rb",false);if(file)Q_strncpy(path,relative,sizeof(path));
	}
	if(!file)return false;
	const fs_offset_t length=FS_FileLength(file);
	if(length<=0||length>(fs_offset_t)maximum){FS_Close(file);return false;}
	s->size=(size_t)length;s->data=malloc(s->size);
	const qboolean loaded=s->data&&FS_Read(file,s->data,s->size)==length;
	FS_Close(file);if(!loaded)return false;
	Q_strncpy(s->name,path,sizeof(s->name));
	int error=0;s->decoder=mpg123_new(NULL,&error);if(!s->decoder)return false;
	mpg123_param2(s->decoder,MPG123_ADD_FLAGS,MPG123_QUIET,0);
	if(mpg123_reader64(s->decoder,BGM_Read,BGM_Seek,NULL)!=MPG123_OK ||
		mpg123_open_handle64(s->decoder,s)!=MPG123_OK)return false;
	long rate=0;int channels=0,encoding=0;
	if(mpg123_getformat(s->decoder,&rate,&channels,&encoding)!=MPG123_OK||rate<8000||rate>192000||channels<1||channels>2||encoding!=MPG123_ENC_SIGNED_16)return false;
	s->rate=rate;s->channels=channels;
	// The bundled mpg123 intentionally supports native rates only. Conversion
	// belongs to this private stream, never the engine's global sound scratch.
	s->converter=SDL_NewAudioStream(AUDIO_S16SYS,channels,rate,AUDIO_S16SYS,2,SOUND_DMA_SPEED);
	return s->converter!=NULL;
}

static int BGM_Decode( void *opaque )
{
	bgm_state_t *s=opaque;int track=0,empty=0; qboolean ending=false;
	short native[8192];
	bgm_source_t *source=&s->source[0];int64_t position=mpg123_tell64(source->decoder)*SOUND_DMA_SPEED/source->rate;
	while(!SDL_AtomicGet(&s->stop))
	{
		unsigned written=(unsigned)SDL_AtomicGet(&s->written),read=(unsigned)SDL_AtomicGet(&s->read);
		if(written-read>=BGM_BLOCKS){SDL_Delay(2);continue;}
		bgm_block_t *block=&s->blocks[written&(BGM_BLOCKS-1)];
		int available=SDL_AudioStreamAvailable(source->converter);
		if(available<0){SDL_AtomicSet(&s->eof,1);break;}
		if(!ending&&available<(int)sizeof(block->pcm))
		{
			size_t bytes=0;const int status=mpg123_read(source->decoder,(unsigned char *)native,sizeof(native),&bytes);
			if(bytes)
			{
				if(bytes%(source->channels*2)||SDL_AudioStreamPut(source->converter,native,(int)bytes)<0){SDL_AtomicSet(&s->eof,1);break;}
				empty=0;
			}
			else if(++empty>4){SDL_AtomicSet(&s->eof,1);break;}
			if(status==MPG123_DONE){ending=true;SDL_AudioStreamFlush(source->converter);}
			else if(status==MPG123_NEW_FORMAT)
			{
				long rate;int channels,encoding;
				if(mpg123_getformat(source->decoder,&rate,&channels,&encoding)!=MPG123_OK||rate!=source->rate||channels!=source->channels||encoding!=MPG123_ENC_SIGNED_16){SDL_AtomicSet(&s->eof,1);break;}
			}
			else if(status!=MPG123_OK){SDL_AtomicSet(&s->eof,1);break;}
			continue;
		}
		if(available>0)
		{
			const int bytes=SDL_AudioStreamGet(source->converter,block->pcm,Q_min(available,(int)sizeof(block->pcm)));
			if(bytes<=0||bytes%4){SDL_AtomicSet(&s->eof,1);break;}
			block->frames=(unsigned)bytes/4;block->track=track;block->position=position;
			position+=block->frames;empty=0;
			SDL_AtomicSet(&s->written,(int)(written+1));
			continue;
		}
		if(ending)
		{
			if(!s->loop){SDL_AtomicSet(&s->eof,1);break;}
			track=1;source=&s->source[s->shared?0:1];
			if(mpg123_seek64(source->decoder,0,SEEK_SET)<0){SDL_AtomicSet(&s->eof,1);break;}
			SDL_AudioStreamClear(source->converter);position=0;ending=false;
			SDL_AtomicAdd(&s->loops,1);
		}
	}
	return 0;
}

static void BGM_Callback( void *opaque, Uint8 *output, int bytes )
{
	bgm_state_t *s=opaque;memset(output,0,bytes);SDL_AtomicAdd(&s->callbacks,1);
	if(SDL_AtomicGet(&s->finished))return;
	int gain=SDL_AtomicGet(&s->gain);
	if(s->fadeDuration)
	{
		unsigned elapsed=SDL_GetTicks()-s->fadeStart;
		if(elapsed>=s->fadeDuration){SDL_AtomicSet(&s->finished,1);return;}
		gain=(int)((int64_t)gain*(s->fadeDuration-elapsed)/s->fadeDuration);
	}
	if(SDL_AtomicGet(&s->paused)||!SDL_AtomicGet(&s->active))return;
	short *out=(short *)output;unsigned remaining=(unsigned)bytes/4,played=0,nonzero=0;
	while(remaining)
	{
		unsigned read=(unsigned)SDL_AtomicGet(&s->read),written=(unsigned)SDL_AtomicGet(&s->written);
		if(read==written)
		{
			if(SDL_AtomicGet(&s->eof))
			{
				// EOF is published after the last block. Re-read the producer
				// cursor after observing it so a concurrent final block survives.
				if(read!=(unsigned)SDL_AtomicGet(&s->written))continue;
				SDL_AtomicSet(&s->finished,1);
			}
			else SDL_AtomicAdd(&s->underruns,1);
			break;
		}
		const bgm_block_t *block=&s->blocks[read&(BGM_BLOCKS-1)];
		const unsigned count=Q_min(remaining,block->frames-s->offset);
		for(unsigned i=0;i<count*2;++i){out[i]=(short)((int)block->pcm[s->offset*2+i]*gain/32768);nonzero+=out[i]!=0;}
		s->offset+=count;out+=count*2;remaining-=count;played+=count;
		SDL_AtomicSet(&s->track,block->track);
		SDL_AtomicSet(&s->position,(int)Q_min(block->position+s->offset,INT_MAX));
		if(s->offset==block->frames){s->offset=0;SDL_AtomicSet(&s->read,(int)(read+1));}
	}
	SDL_AtomicAdd(&s->played,(int)played);SDL_AtomicAdd(&s->nonzero,(int)nonzero);
}

void BGM_Stop( void )
{
	bgm_state_t *s=bgm;if(!s)return;bgm=NULL;
	SDL_AtomicSet(&s->paused,1);SDL_AtomicSet(&s->stop,1);
	if(s->device)SDL_CloseAudioDevice(s->device);
	if(s->worker)SDL_WaitThread(s->worker,NULL);
	BGM_FreeSource(&s->source[0]);BGM_FreeSource(&s->source[1]);free(s);
}
qboolean BGM_Start( const char *intro, const char *loop, int position )
{
	BGM_Stop();bgm=calloc(1,sizeof(*bgm));if(!bgm)return false;
	if(!BGM_Load(&bgm->source[0],intro,BGM_MAX_FILE))goto fail;
	bgm->loop=loop&&*loop;bgm->shared=bgm->loop&&!Q_stricmp(intro,loop);
	if(bgm->loop&&!bgm->shared&&!BGM_Load(&bgm->source[1],loop,BGM_MAX_FILE-bgm->source[0].size))goto fail;
	if(bgm->source[0].size+bgm->source[1].size>BGM_MAX_FILE)goto fail;
	if(position>0&&mpg123_seek64(bgm->source[0].decoder,position,SEEK_SET)<0)goto fail;
	SDL_AtomicSet(&bgm->position,(int)Q_min(mpg123_tell64(bgm->source[0].decoder)*SOUND_DMA_SPEED/bgm->source[0].rate,INT_MAX));
	SDL_AtomicSet(&bgm->paused,1);SDL_AtomicSet(&bgm->active,1);
	SDL_AudioSpec desired={0},obtained;
	desired.freq=SOUND_DMA_SPEED;desired.format=AUDIO_S16SYS;desired.channels=2;desired.samples=1024;
	desired.callback=BGM_Callback;desired.userdata=bgm;
	bgm->device=SDL_OpenAudioDevice(NULL,0,&desired,&obtained,0);
	if(!bgm->device)goto fail;
	bgm->worker=SDL_CreateThread(BGM_Decode,"OC2 music",bgm);if(!bgm->worker)goto fail;
	// Prime only at track start; playback later has no main-thread dependency.
	for(int i=0;i<100&&!SDL_AtomicGet(&bgm->eof)&&(unsigned)SDL_AtomicGet(&bgm->written)<4;++i)SDL_Delay(1);
	SDL_PauseAudioDevice(bgm->device,0);
	Con_DPrintf("bgm_async: started compressed=%u ring_kib=%u rate=%d stereo=1\n",(unsigned)(bgm->source[0].size+bgm->source[1].size),(unsigned)(sizeof(bgm->blocks)/1024),SOUND_DMA_SPEED);
	return true;
fail:
	BGM_Stop();return false;
}
qboolean BGM_Active( void ){return bgm!=NULL;}
qboolean BGM_Finished( void ){return bgm&&SDL_AtomicGet(&bgm->finished);}
void BGM_Update( float volume, qboolean paused )
{
	if(!bgm)return;
	if(!isfinite(volume))volume=0;
	SDL_AtomicSet(&bgm->gain,(int)(bound(0.f,volume,1.f)*32768));SDL_AtomicSet(&bgm->paused,paused);
}
void BGM_Fade( float seconds )
{
	if(!bgm)return;
	if(!isfinite(seconds)||seconds<=0){BGM_Stop();return;}
	SDL_LockAudioDevice(bgm->device);bgm->fadeStart=SDL_GetTicks();bgm->fadeDuration=(unsigned)(Q_min(seconds,3600.f)*1000);
	if(!bgm->fadeDuration)bgm->fadeDuration=1;SDL_UnlockAudioDevice(bgm->device);
}
void BGM_Activate( qboolean active ){if(bgm)SDL_AtomicSet(&bgm->active,active);}
void BGM_Current( char *name, size_t size, int *position )
{
	if(!bgm)return;
	SDL_LockAudioDevice(bgm->device);
	if(name)Q_strncpy(name,bgm->source[SDL_AtomicGet(&bgm->track)&&!bgm->shared?1:0].name,size);
	if(position)*position=(int)Q_min((int64_t)SDL_AtomicGet(&bgm->position)*bgm->source[SDL_AtomicGet(&bgm->track)&&!bgm->shared?1:0].rate/SOUND_DMA_SPEED,INT_MAX);
	SDL_UnlockAudioDevice(bgm->device);
}
void BGM_Dump( void )
{
	if(!bgm){Con_Printf("bgm_async: active=0\n");return;}
	Con_Printf("bgm_async: active=1 callbacks=%u frames=%u underruns=%u nonzero=%u paused=%d finished=%d queued=%u compressed=%u loops=%u\n",
		(unsigned)SDL_AtomicGet(&bgm->callbacks),(unsigned)SDL_AtomicGet(&bgm->played),(unsigned)SDL_AtomicGet(&bgm->underruns),
		(unsigned)SDL_AtomicGet(&bgm->nonzero),SDL_AtomicGet(&bgm->paused),SDL_AtomicGet(&bgm->finished),
		(unsigned)SDL_AtomicGet(&bgm->written)-(unsigned)SDL_AtomicGet(&bgm->read),(unsigned)(bgm->source[0].size+bgm->source[1].size),(unsigned)SDL_AtomicGet(&bgm->loops));
}
void BGM_DebugStall( void )
{
	if(Cvar_VariableValue("developer")<1)return;
	const int ms=bound(1,Q_atoi(Cmd_Argv(1)),10000);
	Con_Printf("bgm_stall: begin ms=%d\n",ms);BGM_Dump();
	const double start=Sys_DoubleTime();SDL_Delay(ms);
	BGM_Dump();Con_Printf("bgm_stall: end seconds=%.3f\n",Sys_DoubleTime()-start);
}
