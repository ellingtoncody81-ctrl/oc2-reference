#include "hud.h"
#include "cl_util.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "studio_util.h"
#include "skin_inventory_client.h"
#include "accessory_geometry.h"
#include "world_accessory_mount.h"
#include "../common/oc2_world_mount_fits.h"
#include "../common/oc2_elite_world_regions.h"
#include "oc2_cold_perf.h"
#include "../common/oc2_accessories.h"
#include <array>
#include <map>
#include <string>
#include <cstring>
#include <cmath>
extern engine_studio_api_t IEngineStudio;

namespace
{
namespace geo=oc2_accessory;
cvar_t *worldAccessoriesEnabled=nullptr;
struct Matrix {float m[3][4]{};};
struct Model {const studiohdr_t *header=nullptr;geo::MountCache mounts;std::vector<geo::Triangle> triangles;bool attempted=false;int weapon=0;char kind=0;};
struct SourceModel {model_t *model=nullptr;const studiohdr_t *header=nullptr;geo::MountCache mounts;};
struct Placement {float u=-1,v=-1;geo::Mount mount;const studiohdr_t *source=nullptr;bool mapped=false;};
std::map<model_t *,Model> models;
std::map<int,SourceModel> sources;
std::map<std::pair<int,model_t *>,std::array<Placement,2>> placements;
struct Record {int player=0,count=0,drawn=0,pattern=0;float time=0;std::string model;int mapped=0;float mapDistance=0;};
std::map<int,Record> records;
std::map<const studiohdr_t *,std::vector<Matrix>> binds;
Matrix Product(const Matrix &a,const Matrix &b)
{
 Matrix c;for(int i=0;i<3;++i)for(int j=0;j<4;++j){for(int k=0;k<3;++k)c.m[i][j]+=a.m[i][k]*b.m[k][j];if(j==3)c.m[i][j]+=a.m[i][3];}return c;
}
const std::vector<Matrix> &Bind(const studiohdr_t *h)
{
 auto &result=binds[h];if(!result.empty())return result;
 const auto *bones=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(h)+h->boneindex);
 result.resize(h->numbones);
 for(int i=0;i<h->numbones;++i)
 {
  vec4_t q;AngleQuaternion(const_cast<float *>(&bones[i].value[3]),q);QuaternionMatrix(q,result[i].m);
  for(int a=0;a<3;++a)result[i].m[a][3]=bones[i].value[a];
  if(bones[i].parent>=0&&bones[i].parent<i)result[i]=Product(result[bones[i].parent],result[i]);
 }
 return result;
}
void Dump()
{
 for(const auto &row:records){const auto &r=row.second;gEngfuncs.Con_Printf("oc2_world_accessories: serial=%d player=%d count=%d drawn=%d pattern=%d time=%.4f model=%s mapped=%d map_distance=%.4f\n",row.first,r.player,r.count,r.drawn,r.pattern,r.time,r.model.c_str(),r.mapped,r.mapDistance);}
}
}
void OC2_WorldAccessoriesReset(){models.clear();sources.clear();placements.clear();records.clear();binds.clear();}
void OC2_WorldAccessoriesInit(){gEngfuncs.pfnAddCommand("oc2_world_accessories_dump",Dump);worldAccessoriesEnabled=gEngfuncs.pfnRegisterVariable("cl_oc2_world_accessories","1",0);}
void OC2_PrecacheWorldAccessoryMounts(model_s *model)
{
 if(!model||model->type!=mod_studio)return;
 const auto *header=static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model));if(!header)return;
 auto &entry=models[model];
 if(entry.header!=header)
 {
  entry=Model();entry.header=header;
  for(auto i=placements.begin();i!=placements.end();)if(i->first.second==model)i=placements.erase(i);else ++i;
 }
 if(!entry.attempted)
 {
  entry.attempted=true;
  const char *name=strrchr(model->name,'/');if(!name)return;
  const std::string path=std::string("models/oc2/stickers/")+(name+1);
  auto *sideModel=IEngineStudio.Mod_ForName(path.c_str(),false);
  auto *side=sideModel?static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(sideModel)):nullptr;
  if(!side||side->numbones!=header->numbones)return;
  const auto *a=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(header)+header->boneindex);
  const auto *b=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(side)+side->boneindex);
  for(int i=0;i<header->numbones;++i)if(a[i].parent!=b[i].parent||strncmp(a[i].name,b[i].name,sizeof(a[i].name)))return;
  {OC2ColdTimer build("world_mount_build",model->name);geo::BuildMountCache(header,entry.mounts,side);}
  if(entry.mounts.valid)geo::ReadStudioTriangles(entry.mounts.studio,0,0,
   reinterpret_cast<const float (*)[3][4]>(entry.mounts.bind.data()),entry.triangles);
  const char *file=name+1;
  for(const auto& fit:oc2_world_mount::kFits)
  {
   const char *viewFile=strrchr(fit.view,'/');
   if(!viewFile||file[0]!=fit.kind||strcmp(file+2,viewFile+3))continue;
   entry.weapon=fit.weapon;entry.kind=fit.kind;
   auto *viewModel=IEngineStudio.Mod_ForName(fit.view,false);
   const auto *viewHeader=viewModel?static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(viewModel)):nullptr;
   auto& source=sources[fit.weapon];
   if(viewHeader&&source.header!=viewHeader){source=SourceModel();source.model=viewModel;source.header=viewHeader;geo::BuildMountCache(viewHeader,source.mounts);}
   break;
  }
 }
 if(entry.weapon)
 {
  auto& source=sources[entry.weapon];
  const auto *current=source.model?static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(source.model)):nullptr;
  if(current&&current!=source.header){source.header=current;geo::BuildMountCache(current,source.mounts);}
 }
}
void OC2_DrawWorldAccessories(CStudioModelRenderer &r,model_s *model,int player,int serial,alight_s &lighting,float *lightdir)
{
 OC2ColdTimer cold("world_addons",model?model->name:nullptr);
 if(worldAccessoriesEnabled&&worldAccessoriesEnabled->value==0)return;
 const auto *enabled=gEngfuncs.pfnGetCvarPointer("oc2_accessories_enabled");if(enabled&&enabled->value==0)return;
 if(!model||!r.m_pCurrentEntity||!r.m_pStudioHeader||!IEngineStudio.IsHardware())return;
 float distance2=0;for(int axis=0;axis<3;++axis){const float d=r.m_pCurrentEntity->origin[axis]-r.m_vRenderOrigin[axis];distance2+=d*d;}if(distance2>1600*1600)return;
 oc2_accessories::WeaponConfig cfg;unsigned count=0;int pattern=0,instance=0;
 if(!OC2_InventoryWorldAccessories(model,player,serial,r.m_pCurrentEntity->index,cfg,count,pattern,instance))return;
 OC2_PrecacheWorldAccessoryMounts(model);
 auto &entry=models[model];
 if(!entry.mounts.valid)return;
 if(placements.size()>2048)placements.clear();
 auto &mounts=placements[std::make_pair(instance,model)];
 const auto savedEntity=*r.m_pCurrentEntity;
 auto *savedHeader=r.m_pStudioHeader;auto *savedModel=r.m_pRenderModel;auto *savedPart=r.m_pBodyPart;auto *savedSub=r.m_pSubModel;
 const int faceFlags=IEngineStudio.GetForceFaceFlags();
 std::array<Matrix,MAXSTUDIOBONES> bones,lights;
 for(int i=0;i<MAXSTUDIOBONES;++i){memcpy(bones[i].m,(*r.m_pbonetransform)[i],sizeof(bones[i].m));memcpy(lights[i].m,(*r.m_plighttransform)[i],sizeof(lights[i].m));}
 int drawn=0,mapped=0;float mapDistance=0;
 for(int kind=0;kind<2;++kind)
 {
  if(!(kind?cfg.charm:cfg.stattrak))continue;
  const float u=kind?cfg.charmU:cfg.statU,v=kind?cfg.charmV:cfg.statV;auto &p=mounts[kind];
  const auto source=sources.find(cfg.weapon);
  const auto *sourceHeader=source!=sources.end()?source->second.header:nullptr;
  if(p.u!=u||p.v!=v||p.source!=sourceHeader)
  {
   OC2ColdTimer find("world_mount_find",model->name);p.u=u;p.v=v;p.source=sourceHeader;p.mapped=false;
   const auto *fit=oc2_world_mount::FindFit(cfg.weapon,entry.kind,cfg.weapon==10&&u>=.5f?1:0);
   geo::Mount viewMount;float distance=0;
   const int region=cfg.weapon==10&&u>=.5f?1:0;
   if(fit&&source!=sources.end()&&geo::FindMount(source->second.mounts,u,v,viewMount))
    p.mapped=oc2_world_mount::Map(source->second.mounts,viewMount,entry.mounts,entry.triangles,fit->matrix,p.mount,distance,
     cfg.weapon==10&&entry.kind=='p'?(region?18:17):-1,
     cfg.weapon==10&&entry.kind=='w'?oc2_world_mount::kEliteWorldRegions:nullptr,
     sizeof(oc2_world_mount::kEliteWorldRegions),region);
   // An unregistered replacement model retains the old best-effort path.
   // A known model never silently switches coordinate systems on fit failure.
   if(!p.mapped){p.mount.bone=-1;if(!fit)geo::FindMount(entry.mounts,u,v,p.mount);}
  }
  if(p.mount.bone<0||p.mount.bone>=savedHeader->numbones)continue;
  if(p.mapped){++mapped;mapDistance=std::max(mapDistance,p.mount.snapDistance);}
  const char *path=kind?(cfg.weapon==20?"models/attachments/oc2_sticker_charm_m249.mdl":"models/attachments/oc2_sticker_charm.mdl"):"models/attachments/oc2_stattrak.mdl";
  auto *addon=IEngineStudio.Mod_ForName(path,false);auto *h=addon?static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(addon)):nullptr;
  if(!h||h->numbones<1||h->numbones>MAXSTUDIOBONES)continue;
  Matrix local;const float scale=kind?cfg.length:1;
  for(int i=0;i<3;++i){local.m[i][0]=p.mount.axisX[i]*scale;local.m[i][1]=p.mount.axisY[i]*scale;local.m[i][2]=p.mount.normal[i]*scale;local.m[i][3]=p.mount.origin[i]+p.mount.normal[i]*.01f;}
  const auto frame=Product(bones[p.mount.bone],local);const auto light=Product(lights[p.mount.bone],local);const auto &bind=Bind(h);
  for(int i=0;i<h->numbones;++i){const auto b=Product(frame,bind[i]),l=Product(light,bind[i]);memcpy((*r.m_pbonetransform)[i],b.m,sizeof(b.m));memcpy((*r.m_plighttransform)[i],l.m,sizeof(l.m));}
  r.m_pCurrentEntity->model=addon;r.m_pCurrentEntity->curstate.body=kind?0:int(count);r.m_pCurrentEntity->curstate.skin=0;
  r.m_pStudioHeader=h;r.m_pRenderModel=addon;IEngineStudio.StudioSetHeader(h);IEngineStudio.SetRenderModel(addon);
  auto *materials=reinterpret_cast<mstudiotexture_t *>(reinterpret_cast<byte *>(h)+h->textureindex);
  int *slot=nullptr,old=0;
  if(kind&&pattern)for(int i=0;i<h->numtextures;++i)if(!strcmp(materials[i].name,"charm_sticker.bmp")){slot=&materials[i].index;old=*slot;*slot=pattern;break;}
  IEngineStudio.StudioSetupLighting(&lighting);r.StudioRenderModel(lightdir);++drawn;
  if(slot)*slot=old;
 }
 *r.m_pCurrentEntity=savedEntity;r.m_pStudioHeader=savedHeader;r.m_pRenderModel=savedModel;r.m_pBodyPart=savedPart;r.m_pSubModel=savedSub;
 for(int i=0;i<MAXSTUDIOBONES;++i){memcpy((*r.m_pbonetransform)[i],bones[i].m,sizeof(bones[i].m));memcpy((*r.m_plighttransform)[i],lights[i].m,sizeof(lights[i].m));}
 IEngineStudio.StudioSetHeader(savedHeader);IEngineStudio.SetRenderModel(savedModel);IEngineStudio.SetForceFaceFlags(faceFlags);IEngineStudio.StudioSetupLighting(&lighting);
 auto &record=records[instance];record={player,int(count),drawn,pattern,float(gEngfuncs.GetClientTime()),model->name};
 record.mapped=mapped;record.mapDistance=mapDistance;
 if(records.size()>512)records.erase(records.begin());
}
