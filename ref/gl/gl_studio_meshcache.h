// Cached command topology, with byte comparison and model lifecycle invalidation.
// Positions, normals, chrome, lighting, material dimensions and visibility remain
// live. A zeroed sticker command is honored before any cache lookup.
#define OC2_MESH_CACHE_SLOTS 256
#define OC2_MESH_CACHE_BYTES (32u * 1024u * 1024u)
typedef struct { unsigned first,vertices,element,elements; } oc2_mesh_chunk_t;
typedef struct
{
 const studiohdr_t *header;
 const short *commands;
 short *raw,*corners;
 float *uv,uvS,uvT;
 int uvFlags;
 qboolean uvValid;
 unsigned short *indices;
 oc2_mesh_chunk_t *chunks;
 unsigned rawBytes,vertices,elements,numChunks,bytes,used;
} oc2_mesh_cache_t;
static oc2_mesh_cache_t oc2_mesh_cache[OC2_MESH_CACHE_SLOTS];
static unsigned oc2_mesh_cache_bytes,oc2_mesh_cache_use;
static unsigned oc2_mesh_cache_hits,oc2_mesh_cache_misses,oc2_mesh_cache_evictions;
// Merge only identical authored corner tuples. Position/normal/UV seams,
// triangle order and winding stay byte-for-byte equivalent after expansion.
#define OC2_CORNER_HASH_SIZE (MAXSTUDIOVERTS * 2)
static unsigned short oc2_corner_hash[OC2_CORNER_HASH_SIZE];
static unsigned short oc2_corner_remap[MAXSTUDIOVERTS];
static void R_StudioFreeMeshCache(oc2_mesh_cache_t *c)
{
 if(c->raw)Mem_Free(c->raw);
 oc2_mesh_cache_bytes-=c->bytes;memset(c,0,sizeof(*c));
}
void R_StudioClearMeshCache(void)
{
 for(int i=0;i<OC2_MESH_CACHE_SLOTS;++i)R_StudioFreeMeshCache(&oc2_mesh_cache[i]);
}
static void R_StudioInvalidateMeshCache(const studiohdr_t *header)
{
 for(int i=0;i<OC2_MESH_CACHE_SLOTS;++i)
  if(oc2_mesh_cache[i].header==header)R_StudioFreeMeshCache(&oc2_mesh_cache[i]);
}
static oc2_mesh_cache_t *R_StudioGetMeshCache(const short *commands)
{
 const short *end=(const short *)((const byte *)m_pStudioHeader+m_pStudioHeader->length);
 if(commands<(const short *)m_pStudioHeader||commands>=end||!*commands)return NULL;
 unsigned vertices=0,elements=0,chunks=0;const short *cursor=commands,*next=NULL;
 int empty=-1;
 for(int i=0;i<OC2_MESH_CACHE_SLOTS;++i)
 {
  oc2_mesh_cache_t *c=&oc2_mesh_cache[i];
  if(!c->raw){empty=i;continue;}
  if(c->header!=m_pStudioHeader||c->commands!=commands)continue;
  if(c->rawBytes<=(unsigned)((const byte *)end-(const byte *)commands)&&!memcmp(c->raw,commands,c->rawBytes))
  {c->used=++oc2_mesh_cache_use;++oc2_mesh_cache_hits;return c;}
  R_StudioFreeMeshCache(c);empty=i;break;
 }
 ++oc2_mesh_cache_misses;
 do
 {
  if(R_StudioPlanArrayChunk(cursor,end,&next)!=1||next<cursor)return NULL;
  ++chunks;
  while(cursor<next)
  {
   int n=*cursor++;if(n<0)n=-n;
   vertices+=n;elements+=(n-2)*3;
   for(int j=0;j<n;++j,cursor+=4)
    if(cursor[0]<0||cursor[0]>=m_pSubModel->numverts||cursor[1]<0||cursor[1]>=m_pSubModel->numnorms)return NULL;
  }
 }while(*cursor);
 unsigned rawBytes=(unsigned)((const byte *)(cursor+1)-(const byte *)commands);
 // Align the following chunk array independently of the short command tail.
 unsigned aligned=(rawBytes+3u)&~3u;
 unsigned bytes=aligned+chunks*sizeof(oc2_mesh_chunk_t)+vertices*16u+elements*2u;
 if(bytes>OC2_MESH_CACHE_BYTES)return NULL;
 while(empty<0||oc2_mesh_cache_bytes+bytes>OC2_MESH_CACHE_BYTES)
 {
  int oldest=-1;
  for(int i=0;i<OC2_MESH_CACHE_SLOTS;++i)if(oc2_mesh_cache[i].raw&&(oldest<0||oc2_mesh_cache[i].used<oc2_mesh_cache[oldest].used))oldest=i;
  if(oldest<0)return NULL;
  R_StudioFreeMeshCache(&oc2_mesh_cache[oldest]);empty=oldest;++oc2_mesh_cache_evictions;
 }
 oc2_mesh_cache_t *c=&oc2_mesh_cache[empty];
 c->raw=Mem_Malloc(r_temppool,bytes);memcpy(c->raw,commands,rawBytes);
 c->chunks=(oc2_mesh_chunk_t *)((byte *)c->raw+aligned);
 c->corners=(short *)(c->chunks+chunks);c->uv=(float *)(c->corners+vertices*4);c->indices=(unsigned short *)(c->uv+vertices*2);
 c->header=m_pStudioHeader;c->commands=commands;c->rawBytes=rawBytes;
 c->vertices=vertices;c->elements=elements;c->numChunks=chunks;c->bytes=bytes;c->used=++oc2_mesh_cache_use;
 oc2_mesh_cache_bytes+=bytes;cursor=commands;unsigned v=0,e=0;
 for(unsigned chunk=0;chunk<chunks;++chunk)
 {
  if(R_StudioPlanArrayChunk(cursor,end,&next)!=1){R_StudioFreeMeshCache(c);return NULL;}
  oc2_mesh_chunk_t *batch=&c->chunks[chunk];batch->first=v;batch->element=e;
  memset(oc2_corner_hash,0,sizeof(oc2_corner_hash));
  unsigned unique=0;
  g_studio.numverts=g_studio.numelems=0;
  while(cursor<next)
  {
   int n=*cursor++,state=0;qboolean strip=n>0;if(n<0)n=-n;
   for(int j=0;j<n;++j,cursor+=4)
   {
    unsigned hash=2166136261u;
    for(int k=0;k<4;++k)hash=(hash^(unsigned short)cursor[k])*16777619u;
    hash%=OC2_CORNER_HASH_SIZE;
    while(oc2_corner_hash[hash]&&memcmp(c->corners+(batch->first+oc2_corner_hash[hash]-1)*4,cursor,8))
     hash=(hash+1)%OC2_CORNER_HASH_SIZE;
    if(!oc2_corner_hash[hash])
    {memcpy(c->corners+(v++)*4,cursor,8);oc2_corner_hash[hash]=(unsigned short)(++unique);}
    oc2_corner_remap[g_studio.numverts]=oc2_corner_hash[hash]-1;
    state=R_StudioBuildIndices(strip,state);++g_studio.numverts;
   }
  }
  batch->vertices=unique;batch->elements=g_studio.numelems;
  for(unsigned i=0;i<batch->elements;++i)c->indices[e+i]=oc2_corner_remap[g_studio.arrayelems[i]];
  e+=batch->elements;
 }
 c->vertices=v;
 return c;
}
static qboolean R_StudioSubmitCached(short *commands,vec3_t *normals,float s,float t,float shellscale)
{
 const uintptr_t begin=(uintptr_t)m_pStudioHeader,p=(uintptr_t)commands;
 if(m_pStudioHeader->length<(int)sizeof(short)||p<begin||p-begin>(unsigned)m_pStudioHeader->length-sizeof(short))return true;
 if(!*commands)return true;
 oc2_mesh_cache_t *cache=R_StudioGetMeshCache(commands);if(!cache)return false;
 if(!FBitSet(g_nFaceFlags,STUDIO_NF_CHROME)&&(!cache->uvValid||cache->uvS!=s||cache->uvT!=t||cache->uvFlags!=(g_nFaceFlags&STUDIO_NF_UV_COORDS)))
 {
  for(unsigned v=0;v<cache->vertices;++v)
  {
   const short *p=cache->corners+v*4;
   cache->uv[v*2]=FBitSet(g_nFaceFlags,STUDIO_NF_UV_COORDS)?HalfToFloat(p[2]):p[2]*s;
   cache->uv[v*2+1]=FBitSet(g_nFaceFlags,STUDIO_NF_UV_COORDS)?HalfToFloat(p[3]):p[3]*t;
  }
  cache->uvS=s;cache->uvT=t;cache->uvFlags=g_nFaceFlags&STUDIO_NF_UV_COORDS;cache->uvValid=true;
 }
 for(unsigned n=0;n<cache->numChunks;++n)
 {
  const oc2_mesh_chunk_t *batch=&cache->chunks[n];
  g_studio.numverts=batch->vertices;g_studio.numelems=batch->elements;
  memcpy(g_studio.arrayelems,cache->indices+batch->element,batch->elements*sizeof(unsigned short));
  if(!FBitSet(g_nFaceFlags,STUDIO_NF_CHROME))memcpy(g_studio.arraycoord,cache->uv+batch->first*2,batch->vertices*8);
  for(unsigned v=0;v<batch->vertices;++v)
  {
   const short *p=cache->corners+(batch->first+v)*4;
   GLubyte *color=g_studio.arraycolor[v];
   VectorCopy(g_studio.verts[p[0]],g_studio.arrayverts[v]);
   if(FBitSet(g_nFaceFlags,STUDIO_NF_CHROME))
   {
    int normal=p[1];
    if(shellscale>0)
    {
     normal=g_studio.normaltable[p[0]];
     VectorMA(g_studio.verts[p[0]],shellscale,g_studio.norms[p[0]],g_studio.arrayverts[v]);
     color[0]=RI.currententity->curstate.rendercolor.r;color[1]=RI.currententity->curstate.rendercolor.g;color[2]=RI.currententity->curstate.rendercolor.b;color[3]=255;
    }
    else R_StudioSetColorArray((short *)p,normals,color);
    g_studio.arraycoord[v][0]=g_studio.chrome[normal][0]*s;g_studio.arraycoord[v][1]=g_studio.chrome[normal][1]*t;
   }
   else
   {
    R_StudioSetColorArray((short *)p,normals,color);
   }
  }
  R_StudioDrawArrays(0,0);
 }
 return true;
}
