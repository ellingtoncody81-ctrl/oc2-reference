#include "hud.h"
#include "cl_util.h"
#include "cl_entity.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "skin_inventory_client.h"
#include "oc2_world_stickers.h"
#include <cstring>
#include <map>
#include <string>

extern engine_studio_api_t IEngineStudio;
namespace
{
std::map<std::string,model_t *> overlays;
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
model_t *Overlay(model_t *weapon,char context)
{
 if(!weapon||strnicmp(weapon->name,"models/",7))return nullptr;
 const char *base=strrchr(weapon->name,'/');base=base?base+1:weapon->name;
 const char prefix=context=='p'?'p':'w';
 if(strlen(base)<4||base[0]!=prefix||base[1]!='_')return nullptr;
 const std::string path=std::string("models/oc2/stickers/")+base;
 const auto cached=overlays.find(path);if(cached!=overlays.end())return cached->second;
 int size=0;byte *data=gEngfuncs.COM_LoadFile(const_cast<char *>(path.c_str()),5,&size);
 if(!data){overlays[path]=nullptr;return nullptr;}
 gEngfuncs.COM_FreeFile(data);
 model_t *model=size>0?IEngineStudio.Mod_ForName(path.c_str(),false):nullptr;
 if(!model||model->type!=mod_studio)model=nullptr;
 overlays[path]=model;return model;
}
}
void OC2_ResetWorldStickers() { overlays.clear(); }
void OC2_DrawWorldStickers(CStudioModelRenderer &renderer,model_s *weapon,char context,int player,int serial,float *lightdir)
{
 if((context!='p'&&context!='w'&&context!='c')||!weapon||!renderer.m_pCurrentEntity||
    !lightdir||!IEngineStudio.IsHardware())return;
 model_t *model=Overlay(weapon,context);if(!model)return;
 auto *header=static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model));
 auto *base=static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(weapon));
 if(!Compatible(base,header))return;
 const cl_entity_t savedEntity=*renderer.m_pCurrentEntity;
 auto *savedHeader=renderer.m_pStudioHeader;auto *savedModel=renderer.m_pRenderModel;
 auto *savedPart=renderer.m_pBodyPart;auto *savedSub=renderer.m_pSubModel;
 const int faceFlags=IEngineStudio.GetForceFaceFlags();
 renderer.m_pCurrentEntity->model=model;
 renderer.m_pCurrentEntity->curstate.body=0;renderer.m_pCurrentEntity->curstate.skin=0;
 renderer.m_pStudioHeader=header;renderer.m_pRenderModel=model;
 IEngineStudio.StudioSetHeader(header);IEngineStudio.SetRenderModel(model);
 {
  OC2_InventoryMaterialScope materials(renderer,model,player,serial,context,true);
  if(materials.Ready())renderer.StudioRenderModel(lightdir);
 }
 *renderer.m_pCurrentEntity=savedEntity;
 renderer.m_pStudioHeader=savedHeader;renderer.m_pRenderModel=savedModel;
 renderer.m_pBodyPart=savedPart;renderer.m_pSubModel=savedSub;
 IEngineStudio.StudioSetHeader(savedHeader);IEngineStudio.SetRenderModel(savedModel);
 IEngineStudio.SetForceFaceFlags(faceFlags);
}
