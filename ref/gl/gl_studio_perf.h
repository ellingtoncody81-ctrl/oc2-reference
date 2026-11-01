// Opt-in, bounded model-level timings for real frame profiling.
typedef struct
{
 char name[128]; unsigned calls,vertices,triangles;
 double transform,lighting,submit;
} oc2_studio_perf_t;
static oc2_studio_perf_t oc2_studio_perf[256];
static unsigned oc2_studio_perf_count;
static qboolean oc2_studio_perf_active;
static void R_StudioPerfReset_f(void)
{
 memset(oc2_studio_perf,0,sizeof(oc2_studio_perf));oc2_studio_perf_count=0;oc2_studio_perf_active=true;
 oc2_mesh_cache_hits=oc2_mesh_cache_misses=oc2_mesh_cache_evictions=0;
}
static void R_StudioPerfDump_f(void)
{
 oc2_studio_perf_active=false;
 gEngfuncs.Con_Printf("studioperf_cache: hits=%u misses=%u evictions=%u bytes=%u\n",oc2_mesh_cache_hits,oc2_mesh_cache_misses,oc2_mesh_cache_evictions,oc2_mesh_cache_bytes);
 for(unsigned i=0;i<oc2_studio_perf_count;++i)
 {
  const oc2_studio_perf_t *p=&oc2_studio_perf[i];
  gEngfuncs.Con_Printf("studioperf_model: model=%s calls=%u vertices=%u triangles=%u transform_ms=%.5f lighting_ms=%.5f submit_ms=%.5f\n",
   p->name,p->calls,p->vertices,p->triangles,p->transform,p->lighting,p->submit);
 }
}
static oc2_studio_perf_t *R_StudioPerfModel(void)
{
 if(!oc2_studio_perf_active||!RI.currentmodel)return NULL;
 for(unsigned i=0;i<oc2_studio_perf_count;++i)
  if(!Q_strcmp(oc2_studio_perf[i].name,RI.currentmodel->name))return &oc2_studio_perf[i];
 if(oc2_studio_perf_count>=256)return NULL;
 oc2_studio_perf_t *p=&oc2_studio_perf[oc2_studio_perf_count++];
 Q_strncpy(p->name,RI.currentmodel->name,sizeof(p->name));return p;
}
