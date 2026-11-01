// Optional bounded frame timing. No per-frame output or allocation.
#define HOST_PERF_CAPACITY 8192
static double host_perf_samples[HOST_PERF_CAPACITY][16], host_perf_current[16];
static double host_perf_previous;
static unsigned host_perf_count, host_perf_generation;
static qboolean host_perf_active;

double Host_PerfNow( void ) { return host_perf_active ? Platform_DoubleTime() : 0; }
void Host_PerfAdd( int stage, double start )
{
 if( host_perf_active && start > 0 && stage >= 0 && stage < 16 )
  host_perf_current[stage] += (Platform_DoubleTime() - start) * 1000.0;
}
static void Host_PerfReset_f( void )
{
 host_perf_count = 0; host_perf_previous = 0; host_perf_active = true; ++host_perf_generation;
}
static int Host_PerfCompare( const void *a, const void *b )
{
 const double x=*(const double *)a, y=*(const double *)b;
 return (x>y)-(x<y);
}
static void Host_PerfDump_f( void )
{
 static const char *names[]={"wall","input","server","client","screen","entities","prediction","work","radar","hud","swap","view","sound","packets","client_frame","resources"};
 static double sorted[HOST_PERF_CAPACITY];
 host_perf_active=false;
 for( unsigned stage=0; stage<16; ++stage )
 {
  double sum=0; unsigned over16=0,over33=0;
  for( unsigned i=0; i<host_perf_count; ++i )
  { double v=host_perf_samples[i][stage]; sorted[i]=v; sum+=v; over16+=v>16.667; over33+=v>33.333; }
  if(!host_perf_count)continue;
  qsort(sorted,host_perf_count,sizeof(double),Host_PerfCompare);
  Con_Printf("hostperf_metric: name=%s frames=%u mean_ms=%.5f p95_ms=%.5f p99_ms=%.5f max_ms=%.5f over16=%u over33=%u\n",
   names[stage],host_perf_count,sum/host_perf_count,sorted[(host_perf_count-1)*95/100],
   sorted[(host_perf_count-1)*99/100],sorted[host_perf_count-1],over16,over33);
 }
 for(unsigned i=0;i<host_perf_count;++i)if(host_perf_samples[i][7]>33.333)
 {
  const double *p=host_perf_samples[i];
  Con_Printf("hostperf_slow: sample=%u work_ms=%.4f server_ms=%.4f client_ms=%.4f view_ms=%.4f entities_ms=%.4f hud_ms=%.4f radar_ms=%.4f swap_ms=%.4f sound_ms=%.4f packets_ms=%.4f client_frame_ms=%.4f resources_ms=%.4f\n",i,p[7],p[2],p[3],p[11],p[5],p[9],p[8],p[10],p[12],p[13],p[14],p[15]);
 }
}
