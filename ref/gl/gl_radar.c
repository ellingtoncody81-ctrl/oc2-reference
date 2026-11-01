/*
gl_radar.c - CS:GO radar background blur

Reconstructed from the Android build of Offensive Classic 2
(apk/lib/armeabi-v7a/libxash.so: GL_InitRadarCapture,
GL_CaptureRadarBackground, GL_GetRadarBackgroundTexture,
GL_BindRadarTexture, GL_FreeRadarCapture, GL_ApplyBoxBlur).

The radar background is not an FBO render. The engine reads the pixels
already on screen around the radar back into a CPU buffer, box-blurs them
and uploads the result as a texture; the client then samples that texture
slightly oversized to get the CS:GO soft edge.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gl_local.h"
#include "gl_radar_blur.h"

// not every GL profile this renderer builds against exposes the read-buffer
// selector, but the constant itself is fixed by the spec
#ifndef GL_READ_BUFFER
#define GL_READ_BUFFER 0x0C02
#endif

#define RADAR_PIXEL_BUFFER_SIZE 0x100000	// 1 MB, covers RADAR_MAX_RES squared in RGBA
#define RADAR_MAX_RES 512				// 512*512*4 = 1 MB, matches the buffer above

static byte *p_radar_pixels;
static byte *p_radar_scratch;	// ping-pong buffer for the separable blur
static size_t i_radar_scratch_size;
static unsigned int i_radar_texture;
static poolhandle_t r_radar_pool;

// screen rect the current background texture covers, HUD coords (top-left
// origin). Zeroed until the first successful capture.
static int i_radar_rect[4];
static qboolean b_radar_warned_clip;

// what the client last asked for, replayed by GL_CaptureRadarBackground when
// the engine reaches the post-view hook
static int i_radar_pending[4];
static qboolean b_radar_has_params;

// resolution of the blur sample buffer, owned by the command layer
// (see engine/client/dll_int/cl_radar.c); defaults to 64 if unset
convar_t *cl_radar_blur_resolution;
#include "gl_radar_gpu.h"

void GL_SetRadarBlurCvar( convar_t *cv )
{
	cl_radar_blur_resolution = cv;
#if !XASH_GLES && !XASH_GL_STATIC
	if(CVAR_CHECK_SENTINEL(&r_radar_gpu))gEngfuncs.Cvar_RegisterVariable(&r_radar_gpu);
	gEngfuncs.Cmd_AddCommand("r_radar_gpu_test",GL_RadarGPUTest_f,"compare actual GPU radar pixels with CPU blur");
#endif
}/*
====================
GL_ApplyBoxBlur

Separable two-pass box blur over an RGBA8888 buffer.
Pass 1 walks rows (horizontal), pass 2 walks columns (vertical).
Borders are clamped rather than mirrored and every channel is averaged
with integer division -- that integer division is what the Android build
does, and it shows up as slight darkening on bright edges.
====================
*/
int GL_ApplyBoxBlur( const byte *src, byte *dst, int width, int height, int radius )
{
	byte *tmp;

	if( radius < 1 )
		return radius;

	if( !src || !dst || width <= 0 || height <= 0 )
		return radius;

	if( !r_radar_pool )
		r_radar_pool = Mem_AllocPool( "Radar Blur" );

	// grown on demand and reused; at the larger capture sizes a per-call
	// allocation of the same size is a megabyte of churn every capture
	if( i_radar_scratch_size < (size_t)width * height * 4 )
	{
		if( p_radar_scratch )
			Mem_Free( p_radar_scratch );
		p_radar_scratch = (byte *)Mem_Malloc( r_radar_pool, (size_t)width * height * 4 );
		i_radar_scratch_size = p_radar_scratch ? (size_t)width * height * 4 : 0;
	}

	tmp = p_radar_scratch;
	if( !tmp )
		return radius;

	GL_RadarBoxBlurRGBA( src, dst, tmp, width, height, radius );

	// tmp is the pool-owned scratch, kept for the next capture
	return radius;
}
/*
====================
GL_InitRadarCapture
====================
*/
void GL_InitRadarCapture( void )
{
	if( p_radar_pixels )
		return;

	if( !r_radar_pool )
		r_radar_pool = Mem_AllocPool( "Radar Blur" );

	p_radar_pixels = (byte *)Mem_Malloc( r_radar_pool, RADAR_PIXEL_BUFFER_SIZE );
	if( p_radar_pixels )
		memset( p_radar_pixels, 0, RADAR_PIXEL_BUFFER_SIZE );

	p_radar_scratch = NULL;
	i_radar_scratch_size = 0;
}

/*
====================
GL_SetRadarCaptureParams

The client calls this from its HUD pass; it only records what was asked for.
The capture itself happens later, when the engine calls
GL_CaptureRadarBackground from the post-view hook.

Those two have to be separate. By the time the HUD is drawing, the colour
buffer already has the radar on it, and the previous frame's image has one
too, so there is no moment during the HUD pass at which the buffer holds
just the world. Right after the view render does.
====================
*/
void GL_SetRadarCaptureParams( int blurRadius, int cx, int cy, int size )
{
	i_radar_pending[0] = blurRadius;
	i_radar_pending[1] = cx;
	i_radar_pending[2] = cy;
	i_radar_pending[3] = size;
	b_radar_has_params = true;
}

/*
====================
GL_CaptureRadarBackground

Read the on-screen block centred on the radar, blur it, upload it as a
texture, using whatever GL_SetRadarCaptureParams was last given.

Called from the engine once per frame, after the 3D view is drawn and before
the HUD is composited.

Two things this has to get right, and neither is obvious:

  * WHICH BUFFER. The scene target is still bound here, and it holds nothing
    but the world -- no radar, no HUD, no weapon sprite. That is the whole
    point of capturing here. The previous version read GL_FRONT from inside
    the HUD pass, which handed back the previous frame's radar and blurred
    it into its own backdrop.

  * WHICH ORIGIN. HUD coordinates have their origin at the top left,
    glReadPixels at the bottom left, so Y has to be flipped through the
    framebuffer height.

The block is clamped to stay inside the framebuffer, so when the radar sits
too close to an edge the texture no longer covers the window that was asked
for. That rectangle is recorded and handed back through
GL_GetRadarCaptureRect rather than assumed.
====================
*/
void GL_CaptureRadarBackground( void )
{
	int radius, cx, cy;
	int res, x, y, scr_w, scr_h;
	size_t bufsize;
	byte *buf;
	GLint prevTex, prevReadBuf, glerr;

	if( !b_radar_has_params )
		return;

	if( !p_radar_pixels )
		GL_InitRadarCapture();
	if( !p_radar_pixels )
		return;

	radius = i_radar_pending[0];
	cx = i_radar_pending[1];
	cy = i_radar_pending[2];

	// The capture has to cover the whole radar, not a fixed square near its
	// centre: the client draws the backdrop across the full circle, so
	// anything smaller leaves the outer ring sampling clamped edge pixels
	// and the radar ends up sitting on a smear of itself. Deriving the
	// resolution from the radius the client actually drew keeps the two in
	// step at every screen size and every cl_newradar_size.
	{
		int iSize = i_radar_pending[3];
		res = iSize * 2;
		if( res > RADAR_MAX_RES ) res = RADAR_MAX_RES;
		if( res < 32 )           res = 32;
	}

	// cl_radar_blur_resolution stays as a manual override: 0 means "follow
	// the radar size", anything else pins the capture square
	if( cl_radar_blur_resolution && cl_radar_blur_resolution->value >= 32.0f )
	{
		res = (int)cl_radar_blur_resolution->value;
		if( res > RADAR_MAX_RES ) res = RADAR_MAX_RES;
	}

	// The capture resolution follows the radar, so a fixed pixel radius
	// means something completely different at every screen size -- 6 samples
	// is a faint haze on a 378px block and a total smear on a 64px one.
	// <= 0 picks a fraction of the block, which keeps the blur looking the
	// same however big the radar is drawn.
	if( radius <= 0 )
		radius = res / 16;
	if( radius > res / 8 )
		radius = res / 8;


	// the world is what we want, so make sure it is what is bound
	GL_BindSceneTarget();
	GL_GetSceneTargetSize( &scr_w, &scr_h );
	if( scr_w < 1 || scr_h < 1 )
		return;

	// centre on the radar, then clamp into the framebuffer
	x = cx - res / 2;
	y = scr_h - cy - res / 2;

	if( x < 0 )           x = 0;
	if( y < 0 )           y = 0;
	if( scr_w < x + res ) x = scr_w - res;
	if( scr_h < y + res ) y = scr_h - res;
	if( x < 0 )           x = 0;
	if( y < 0 )           y = 0;

	if( x != cx - res / 2 || y != scr_h - cy - res / 2 )
	{
		if( !b_radar_warned_clip )
		{
			b_radar_warned_clip = true;
			gEngfuncs.Con_Printf( S_WARN "%s: radar at (%i,%i) is too close to the edge for a "
				"%ix%i capture, clamped to (%i,%i) - lower cl_radar_blur_resolution\n",
				__func__, cx, cy, res, res, x, y );
		}
	}

	if(GL_RadarGPUProcess(x,y,res,radius,NULL,NULL))
	{
#if !XASH_GLES && !XASH_GL_STATIC
		++radarGPUCaptures;
#endif
		i_radar_rect[0]=x;i_radar_rect[1]=scr_h-y-res;i_radar_rect[2]=i_radar_rect[3]=res;
		return;
	}
#if !XASH_GLES && !XASH_GL_STATIC
	++radarCPUFallbacks;radarGPUSize=0;
#endif
	bufsize = (size_t)res * res * 4;
	buf = (byte *)Mem_Malloc( r_radar_pool, bufsize );
	if( !buf )return;
	// with render targets off the scene is the window, so read the back
	// buffer; with them on, the FBO's colour attachment 0 is already the
	// read buffer and asking for GL_BACK is an error
	pglGetIntegerv( GL_READ_BUFFER, &prevReadBuf );
	if( !GL_RenderTargetsActive() )
		pglReadBuffer( GL_BACK );
	pglPixelStorei( GL_PACK_ALIGNMENT, 1 );

	pglReadPixels( x, y, res, res, GL_RGBA, GL_UNSIGNED_BYTE, buf );

	pglReadBuffer( prevReadBuf );

	glerr = pglGetError();
	if( glerr != GL_NO_ERROR )
	{
		gEngfuncs.Con_Printf( S_ERROR "%s: glReadPixels failed (0x%04x)\n", __func__, (unsigned)glerr );
		Mem_Free( buf );
		return;
	}

	if( radius < 1 )
		memcpy( p_radar_pixels, buf, bufsize );
	else
		GL_ApplyBoxBlur( buf, p_radar_pixels, res, res, radius );

	Mem_Free( buf );

	// (re)upload the blurred block
	pglGetIntegerv( GL_TEXTURE_BINDING_2D, &prevTex );

	if( !i_radar_texture )
		pglGenTextures( 1, &i_radar_texture );

	pglBindTexture( GL_TEXTURE_2D, i_radar_texture );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
	pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, res, res, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, p_radar_pixels );

	pglBindTexture( GL_TEXTURE_2D, prevTex );

	// HUD coords: the texture's row 0 is what glReadPixels put at y, which is
	// scr_h-y-res from the bottom, i.e. this top edge going down.
	i_radar_rect[0] = x;
	i_radar_rect[1] = scr_h - y - res;
	i_radar_rect[2] = res;
	i_radar_rect[3] = res;
}

/*
====================
GL_GetRadarCaptureRect

Screen rectangle (HUD coords) the background texture covers. Returns 0 when
no capture has succeeded yet, so a client can tell "not captured" apart from
"captured a black frame".
====================
*/
int GL_GetRadarCaptureRect( int *x, int *y, int *w, int *h )
{
	if( !i_radar_texture || i_radar_rect[2] <= 0 || i_radar_rect[3] <= 0 )
		return 0;

	if( x ) *x = i_radar_rect[0];
	if( y ) *y = i_radar_rect[1];
	if( w ) *w = i_radar_rect[2];
	if( h ) *h = i_radar_rect[3];
	return 1;
}

/*
====================
GL_GetRadarBackgroundTexture
====================
*/
unsigned int GL_GetRadarBackgroundTexture( void )
{
	return i_radar_texture;
}

/*
====================
GL_BindRadarTexture
====================
*/
void GL_BindRadarTexture( void )
{
	if( !i_radar_texture )
		return;

	pglEnable( GL_TEXTURE_2D );
	pglBindTexture( GL_TEXTURE_2D, i_radar_texture );
}

/*
====================
GL_FreeRadarCapture
====================
*/
void GL_FreeRadarCapture( void )
{
	GL_RadarGPUFree();
	if( p_radar_pixels )
	{
		Mem_Free( p_radar_pixels );
		p_radar_pixels = NULL;
	}

	if( i_radar_texture )
	{
		pglDeleteTextures( 1, &i_radar_texture );
		i_radar_texture = 0;
	}

	if( p_radar_scratch )
	{
		Mem_Free( p_radar_scratch );
		p_radar_scratch = NULL;
		i_radar_scratch_size = 0;
	}

	memset( i_radar_rect, 0, sizeof( i_radar_rect ));

	b_radar_has_params = false;
	memset( i_radar_pending, 0, sizeof( i_radar_pending ));
}
