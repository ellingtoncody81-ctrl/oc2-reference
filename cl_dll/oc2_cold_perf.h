#pragma once
#include <chrono>
// Opt-in logging of expensive cold work; never logs steady-state draws.
class OC2ColdTimer
{
 const char *section,*resource;bool active;
 std::chrono::steady_clock::time_point start;
public:
 OC2ColdTimer(const char *s,const char *r):section(s),resource(r)
 {
  static const cvar_t *setting=nullptr;
  if(!setting)setting=gEngfuncs.pfnGetCvarPointer("r_studio_perf_cold");
  active=setting&&setting->value!=0;if(active)start=std::chrono::steady_clock::now();
 }
 ~OC2ColdTimer()
 {
  if(!active)return;
  const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(ms>=4)gEngfuncs.Con_Printf("oc2_cold: section=%s ms=%.4f time=%.4f resource=%s\n",section,ms,gEngfuncs.GetClientTime(),resource?resource:"none");
 }
};
