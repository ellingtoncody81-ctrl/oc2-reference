#include "hud.h"
#include "cl_util.h"
#include "cl_entity.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "skin_inventory_client.h"
#include "oc2_world_stickers.h"
#include <chrono>
#include <cstring>
#include <map>
#include <string>
#include <utility>

extern engine_studio_api_t IEngineStudio;
namespace
{
std::map<std::string,model_t *> overlays;
struct OverlayAlias { std::string weaponName;model_t *model=nullptr; };
std::map<std::pair<model_t *,char>,OverlayAlias> overlayAliases;
struct CompatibleEntry
{
 const studiohdr_t *base=nullptr,*overlay=nullptr;
 int baseLength=0,baseBones=0,baseBoneIndex=0;
 int overlayLength=0,overlayBones=0,overlayBoneIndex=0,overlayTextures=0,overlayTextureIndex=0,overlayParts=0;
};
std::map<std::pair<model_t *,model_t *>,CompatibleEntry> compatibleModels;
struct DrawRecord { int entity=0,player=0,serial=0; char context=0; double time=0; std::string model; };
std::map<std::pair<model_t *,int>,DrawRecord> draws;
using PerfClock=std::chrono::steady_clock;
struct PerfStats
{
 bool active=false;
 unsigned calls=0,submitted=0,compatibleHits=0,compatibleMisses=0,incompatible=0,cacheResets=0;
 double totalMs=0,lookupMs=0,compatibleMs=0,materialAndRenderMs=0,renderMs=0,maxCallMs=0;
} perf;
struct PerfScope
{
 bool active;double *total;unsigned *count;double *maximum;PerfClock::time_point start;
 PerfScope(double &value,unsigned *calls=nullptr,double *peak=nullptr)
  :active(perf.active),total(&value),count(calls),maximum(peak)
 {if(active)start=PerfClock::now();}
 ~PerfScope()
 {
  if(!active)return;
  const double elapsed=std::chrono::duration<double,std::milli>(PerfClock::now()-start).count();
  *total+=elapsed;if(count)++*count;if(maximum&&elapsed>*maximum)*maximum=elapsed;
 }
};
void PerfReset()
{
 perf=PerfStats();perf.active=true;
 gEngfuncs.Con_Printf("oc2_world_stickers_perf: collecting CPU timings\n");
}
void PerfDump()
{
 perf.active=false;
 gEngfuncs.Con_Printf("oc2_world_stickers_perf: calls=%u submitted=%u compatible_hits=%u compatible_misses=%u incompatible=%u cache_resets=%u cache_pairs=%u total_ms=%.6f mean_call_ms=%.6f max_call_ms=%.6f lookup_ms=%.6f compatibility_ms=%.6f material_scope_including_render_ms=%.6f render_ms=%.6f scope_nonrender_ms=%.6f\n",
  perf.calls,perf.submitted,perf.compatibleHits,perf.compatibleMisses,perf.incompatible,perf.cacheResets,
  unsigned(compatibleModels.size()),perf.totalMs,perf.calls?perf.totalMs/perf.calls:0.,perf.maxCallMs,
  perf.lookupMs,perf.compatibleMs,perf.materialAndRenderMs,perf.renderMs,perf.materialAndRenderMs-perf.renderMs);
}
void Dump()
{
 gEngfuncs.Con_Printf("oc2_world_stickers: time=%.3f models=%u draws=%u\n",gEngfuncs.GetClientTime(),unsigned(overlays.size()),unsigned(draws.size()));
 for(const auto &row:draws)
 {
  const auto &d=row.second;
  gEngfuncs.Con_Printf("oc2_world_sticker_draw: context=%c entity=%d player=%d serial=%d time=%.3f model=%s\n",d.context,d.entity,d.player,d.serial,d.time,d.model.c_str());
 }
}
bool Range(const studiohdr_t *header,int offset,int count,std::size_t stride)
{
 return header&&header->length>=int(sizeof(studiohdr_t))&&offset>=0&&count>=0&&
  offset<=header->length&&std::size_t(count)<=std::size_t(header->length-offset)/stride;
}
bool Compatible(const studiohdr_t *base,const studiohdr_t *overlay)
{
 if(!base||!overlay||base->numbones<1||base->numbones>MAXSTUDIOBONES||
    overlay->numbones!=base->numbones||overlay->numtextures!=4||overlay->numbodyparts<1||
    !Range(base,base->boneindex,base->numbones,sizeof(mstudiobone_t))||
    !Range(overlay,overlay->boneindex,overlay->numbones,sizeof(mstudiobone_t))||
    !Range(overlay,overlay->textureindex,overlay->numtextures,sizeof(mstudiotexture_t)))return false;
 const auto *a=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(base)+base->boneindex);
 const auto *b=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(overlay)+overlay->boneindex);
 for(int i=0;i<base->numbones;++i)
 {
  if(!memchr(a[i].name,0,sizeof(a[i].name))||!memchr(b[i].name,0,sizeof(b[i].name))||
     stricmp(a[i].name,b[i].name)||a[i].parent!=b[i].parent||
     memcmp(a[i].value,b[i].value,sizeof(a[i].value))||memcmp(a[i].scale,b[i].scale,sizeof(a[i].scale)))return false;
 }
 const auto *materials=reinterpret_cast<const mstudiotexture_t *>(reinterpret_cast<const byte *>(overlay)+overlay->textureindex);
 // GoldSrc studio texture flags: additive 0x20, masked 0x40.
 const int additive=0x20,masked=0x40;
 unsigned slots=0;
 for(int i=0;i<4;++i)
 {
  if(!memchr(materials[i].name,0,sizeof(materials[i].name))||!(materials[i].flags&masked)||
     (materials[i].flags&additive))return false;
  for(int slot=0;slot<4;++slot)
  {
   char name[]="decal_A.bmp";name[6]=char('A'+slot);
   if(!stricmp(materials[i].name,name))slots|=1u<<slot;
  }
 }
 return slots==15;
}
bool CachedCompatible(model_t *weapon,model_t *model,const studiohdr_t *base,const studiohdr_t *overlay)
{
 if(!base||!overlay)return false;
 const auto key=std::make_pair(weapon,model);
 auto found=compatibleModels.find(key);
 if(found!=compatibleModels.end())
 {
  const auto &v=found->second;
  if(v.base==base&&v.overlay==overlay&&v.baseLength==base->length&&v.baseBones==base->numbones&&
     v.baseBoneIndex==base->boneindex&&v.overlayLength==overlay->length&&v.overlayBones==overlay->numbones&&
     v.overlayBoneIndex==overlay->boneindex&&v.overlayTextures==overlay->numtextures&&
     v.overlayTextureIndex==overlay->textureindex&&v.overlayParts==overlay->numbodyparts)
  {if(perf.active)++perf.compatibleHits;return true;}
  compatibleModels.erase(found);
 }
 if(perf.active)++perf.compatibleMisses;
 if(!Compatible(base,overlay)){if(perf.active)++perf.incompatible;return false;}
 CompatibleEntry entry;
 entry.base=base;entry.overlay=overlay;entry.baseLength=base->length;entry.baseBones=base->numbones;
 entry.baseBoneIndex=base->boneindex;entry.overlayLength=overlay->length;entry.overlayBones=overlay->numbones;
 entry.overlayBoneIndex=overlay->boneindex;entry.overlayTextures=overlay->numtextures;
 entry.overlayTextureIndex=overlay->textureindex;entry.overlayParts=overlay->numbodyparts;
 if(compatibleModels.size()>=256)compatibleModels.clear();
 compatibleModels.emplace(key,entry);
 return true;
}
model_t *Overlay(model_t *weapon,char context)
{
 if(!weapon||strnicmp(weapon->name,"models/",7))return nullptr;
 const char prefix=context=='p'?'p':'w';
 const auto key=std::make_pair(weapon,prefix);
 const auto alias=overlayAliases.find(key);
 if(alias!=overlayAliases.end())
 {
  if(alias->second.weaponName==weapon->name)return alias->second.model;
  overlayAliases.erase(alias);
 }
 const char *base=strrchr(weapon->name,'/');base=base?base+1:weapon->name;
 if(strlen(base)<4||base[0]!=prefix||base[1]!='_')return nullptr;
 const std::string path=std::string("models/oc2/stickers/")+base;
 const auto cached=overlays.find(path);
 if(cached!=overlays.end())
 {
  OverlayAlias entry;entry.weaponName=weapon->name;entry.model=cached->second;
  overlayAliases.emplace(key,std::move(entry));return cached->second;
 }
 int size=0;byte *data=gEngfuncs.COM_LoadFile(const_cast<char *>(path.c_str()),5,&size);
 if(!data)
 {
  overlays[path]=nullptr;
  OverlayAlias entry;entry.weaponName=weapon->name;overlayAliases.emplace(key,std::move(entry));return nullptr;
 }
 gEngfuncs.COM_FreeFile(data);
 model_t *model=size>0?IEngineStudio.Mod_ForName(path.c_str(),false):nullptr;
 if(!model||model->type!=mod_studio)model=nullptr;
 overlays[path]=model;
 OverlayAlias entry;entry.weaponName=weapon->name;entry.model=model;overlayAliases.emplace(key,std::move(entry));return model;
}
}
void OC2_ResetWorldStickers() { overlays.clear();overlayAliases.clear();compatibleModels.clear();draws.clear();if(perf.active)++perf.cacheResets; }
void OC2_InitWorldStickers()
{
 gEngfuncs.pfnAddCommand("oc2_world_stickers_dump",Dump);
 gEngfuncs.pfnAddCommand("oc2_world_stickers_perf_reset",PerfReset);
 gEngfuncs.pfnAddCommand("oc2_world_stickers_perf_dump",PerfDump);
}
void OC2_DrawWorldStickers(CStudioModelRenderer &renderer,model_s *weapon,char context,int player,int serial,float *lightdir)
{
 if((context!='p'&&context!='w'&&context!='c')||!weapon||!renderer.m_pCurrentEntity||
    !lightdir||!IEngineStudio.IsHardware())return;
 PerfScope whole(perf.totalMs,&perf.calls,&perf.maxCallMs);
 model_t *model=nullptr;
 {PerfScope timer(perf.lookupMs);model=Overlay(weapon,context);}
 if(!model)return;
 auto *header=static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model));
 auto *base=static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(weapon));
 bool compatible=false;
 {PerfScope timer(perf.compatibleMs);compatible=CachedCompatible(weapon,model,base,header);}
 if(!compatible)return;
 const cl_entity_t savedEntity=*renderer.m_pCurrentEntity;
 auto *savedHeader=renderer.m_pStudioHeader;auto *savedModel=renderer.m_pRenderModel;
 auto *savedPart=renderer.m_pBodyPart;auto *savedSub=renderer.m_pSubModel;
 const int faceFlags=IEngineStudio.GetForceFaceFlags();
 renderer.m_pCurrentEntity->model=model;
 renderer.m_pCurrentEntity->curstate.body=0;renderer.m_pCurrentEntity->curstate.skin=0;
 renderer.m_pStudioHeader=header;renderer.m_pRenderModel=model;
 IEngineStudio.StudioSetHeader(header);IEngineStudio.SetRenderModel(model);
 {
  PerfScope timer(perf.materialAndRenderMs);
  OC2_InventoryMaterialScope materials(renderer,model,player,serial,context,true);
  if(materials.Ready())
  {
   {PerfScope renderTimer(perf.renderMs);renderer.StudioRenderModel(lightdir);}
   if(perf.active)++perf.submitted;
   if(draws.size()>512)draws.clear();
   const auto key=std::make_pair(model,savedEntity.index);
   auto found=draws.find(key);
   if(found==draws.end())
   {
    DrawRecord record;record.model=model->name;
    found=draws.emplace(key,std::move(record)).first;
   }
   auto &record=found->second;
   record.entity=savedEntity.index;record.player=player;record.serial=serial;
   record.context=context;record.time=gEngfuncs.GetClientTime();
  }
 }
 *renderer.m_pCurrentEntity=savedEntity;
 renderer.m_pStudioHeader=savedHeader;renderer.m_pRenderModel=savedModel;
 renderer.m_pBodyPart=savedPart;renderer.m_pSubModel=savedSub;
 IEngineStudio.StudioSetHeader(savedHeader);IEngineStudio.SetRenderModel(savedModel);
 IEngineStudio.SetForceFaceFlags(faceFlags);
}
