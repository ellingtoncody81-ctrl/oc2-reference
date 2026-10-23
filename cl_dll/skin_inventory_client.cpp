#include "hud.h"
#include "cl_util.h"
#include "cl_entity.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "render_api.h"
#include "oc2_skin_inventory.h"
#include "skin_inventory_client.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

extern engine_studio_api_t IEngineStudio;
extern render_api_t gRenderAPI;
namespace
{
using namespace oc2_inventory;
Config loadout=DefaultConfig();Catalog catalog;
struct TextureSlot {int handle=0,frame=-1;std::uint64_t serial=0;std::string name;};
std::map<std::string,TextureSlot> textures;
int textureFrame=-1;std::uint64_t textureSerial=0;
std::map<int,std::size_t> resourceIndices,knifeIndices;
std::map<std::string,int> knifeModels;
double nextRead=-1,lastRead=-1;
bool catalogAttempted=false,configExists=false;
std::string readError;
cvar_t *enabled=nullptr,*knifeInfo=nullptr;
struct Diagnostics
{
 int weapon=0,knife=0,skin=0,swaps=0;
 std::array<int,4> stickers{},slotTextures{};
 std::uint64_t instance=0;
 std::string model;
} diagnostics;

bool LoadText(const char *path,std::string &text,int maximum,bool &exists)
{
 int size=0;byte *data=gEngfuncs.COM_LoadFile(const_cast<char *>(path),5,&size);
 exists=data!=nullptr;
 if(!data)
 {
  int count=0;if(gRenderAPI.pfnGetFilesList)gRenderAPI.pfnGetFilesList(path,&count,1);
  exists=count>0;return false;
 }
 const bool valid=size>=0&&size<=maximum&&!memchr(data,0,size);
 if(valid)text.assign(reinterpret_cast<const char *>(data),size);
 gEngfuncs.COM_FreeFile(data);return valid;
}
void ReportError(const std::string &error)
{
 if(error!=readError&&!error.empty())gEngfuncs.Con_Printf("inventory: %s\n",error.c_str());
 readError=error;
}
bool Active() { return !enabled||enabled->value!=0; }
void SyncKnifeInfo()
{
 const int definition=Active()?loadout.knife:0;
 if(knifeInfo&&knifeInfo->value!=float(definition))gEngfuncs.Cvar_SetValue("oc2_knife",float(definition));
}
void ReadState(bool force=false)
{
 const double time=gEngfuncs.GetClientTime();
 if(!force&&time>=lastRead&&time<nextRead)return;
 lastRead=time;nextRead=time+.25;
 std::string text,error;bool exists=false;
 if(force||!catalogAttempted)
 {
  catalogAttempted=true;
  if(LoadText(kCatalogPath,text,int(kMaximumCatalogBytes),exists))
  {
   Catalog candidate;
   if(!ParseCatalog(text,candidate,error)){catalogAttempted=false;ReportError(error);SyncKnifeInfo();return;}
   catalog.swap(candidate);resourceIndices.clear();knifeIndices.clear();knifeModels.clear();
   for(std::size_t i=0;i<catalog.size();++i)
   {
    const Entry &e=catalog[i];resourceIndices[e.id]=i;
    if(e.kind==Kind::Knife){knifeIndices[e.definition]=i;knifeModels[Lower(e.model)]=e.definition;}
   }
  }
  else if(exists){catalogAttempted=false;ReportError("Cannot read skin catalog");SyncKnifeInfo();return;}
  else catalogAttempted=false;
 }
 if(LoadText(kConfigPath,text,int(kMaximumConfigBytes),exists))
 {
  Config candidate;
  if(!ParseConfig(text,candidate,error)||!ValidateConfig(candidate,catalog,error))
   {ReportError(error);SyncKnifeInfo();return;}
  loadout=candidate;configExists=true;
 }
 else if(exists){ReportError("Cannot read loadout file");SyncKnifeInfo();return;}
 else {loadout=DefaultConfig();configExists=false;}
 ReportError("");SyncKnifeInfo();
}
int WeaponId(const char *name)
{
 static const char *const names[24]={"v_p228.mdl","v_scout.mdl","v_xm1014.mdl","v_mac10.mdl","v_aug.mdl","v_elite.mdl",
  "v_fiveseven.mdl","v_ump45.mdl","v_sg550.mdl","v_galil.mdl","v_famas.mdl","v_usp.mdl","v_glock18.mdl","v_awp.mdl",
  "v_mp7.mdl","v_m249.mdl","v_m3.mdl","v_m4a1.mdl","v_tmp.mdl","v_g3sg1.mdl","v_deagle.mdl","v_sg552.mdl","v_ak47.mdl","v_p90.mdl"};
 if(!name||strnicmp(name,"models/",7))return 0;
 for(int i=0;i<24;++i)if(!stricmp(name+7,names[i]))return oc2_accessories::kWeaponIds[i];
 return 0;
}
bool KnifeModel(const char *name)
{
 if(!name)return false;
 if(!stricmp(name,"models/v_knife.mdl"))return true;
 return knifeModels.count(Lower(name))!=0;
}
const Entry *Resource(int id)
{
 const auto i=resourceIndices.find(id);return i==resourceIndices.end()?nullptr:&catalog[i->second];
}
model_t *SelectedKnife()
{
 const auto i=knifeIndices.find(loadout.knife);
 const Entry *entry=i==knifeIndices.end()?nullptr:&catalog[i->second];
 model_t *model=entry?IEngineStudio.Mod_ForName(entry->model.c_str(),false):nullptr;
 return model&&model->type==mod_studio?model:nullptr;
}
bool HeaderRange(const studiohdr_t *header,int offset,int count,std::size_t stride)
{
 return header&&header->length>=int(sizeof(studiohdr_t))&&offset>=0&&count>=0&&offset<=header->length&&
  std::size_t(count)<=std::size_t(header->length-offset)/stride;
}
bool OwnTexture(const TextureSlot &slot)
{
 const char *name=gRenderAPI.GL_TextureName?gRenderAPI.GL_TextureName(slot.handle):nullptr;
 return name&&!stricmp(name,slot.name.c_str());
}
void ReleaseTexture(const TextureSlot &slot)
{ if(gRenderAPI.GL_FreeTexture&&OwnTexture(slot))gRenderAPI.GL_FreeTexture(slot.handle); }
void ReleaseTextures()
{ for(const auto &v:textures)ReleaseTexture(v.second);textures.clear(); }
int Texture(const std::string &path,bool sticker)
{
 if(path.empty()||!gRenderAPI.GL_LoadTexture)return 0;
 auto found=textures.find(path);
 if(found!=textures.end())
 {
  if(OwnTexture(found->second))
   {found->second.frame=textureFrame;found->second.serial=++textureSerial;return found->second.handle;}
  textures.erase(found);
 }
 // One viewmodel needs at most a body image, four slots and a charm. Bound
 // previous choices too: browsing skins must not retain gigabytes of images.
 while(textures.size()>=12)
 {
  auto oldest=textures.end();
  for(auto i=textures.begin();i!=textures.end();++i)
   if(i->second.frame!=textureFrame&&(oldest==textures.end()||i->second.serial<oldest->second.serial))oldest=i;
  if(oldest==textures.end())return 0; // never free an image in this draw
  ReleaseTexture(oldest->second);textures.erase(oldest);
 }
 int size=0;byte *data=gEngfuncs.COM_LoadFile(const_cast<char *>(path.c_str()),5,&size);
 if(!data)return 0;
 if(size<=0||size>16*1024*1024){gEngfuncs.COM_FreeFile(data);return 0;}
 // A private renderer name avoids sharing an uncounted handle with menu pics.
 // The # prefix selects the engine's supported load-from-buffer image path.
 std::uint64_t hash=14695981039346656037ull;
 for(unsigned char c:path)hash=(hash^c)*1099511628211ull;
 const std::size_t dot=path.rfind('.');
 const std::string name="#oc2_inventory_"+std::to_string(hash)+(dot==std::string::npos?".tga":path.substr(dot));
 const int handle=gRenderAPI.GL_LoadTexture(name.c_str(),data,size,sticker?TF_CLAMP:0);
 gEngfuncs.COM_FreeFile(data);
 if(handle)
 {
  TextureSlot slot;slot.handle=handle;slot.frame=textureFrame;slot.serial=++textureSerial;slot.name=name;textures[path]=slot;
 }
 return handle;
}
int StickerTexture(int id)
{
 if(id<0)return Texture(kTransparentPath,true);
 const Entry *entry=Resource(id);
 return entry&&entry->kind==Kind::Sticker?Texture(entry->file,true):0;
}
bool MaterialMatches(const std::string &list,const char *name)
{
 std::size_t begin=0;
 for(;;)
 {
  const std::size_t end=list.find(',',begin);
  if(!stricmp(list.substr(begin,end==std::string::npos?end:end-begin).c_str(),name))return true;
  if(end==std::string::npos)return false;begin=end+1;
 }
}
void Reload() { ReleaseTextures();catalogAttempted=false;ReadState(true); }
void Dump()
{
 ReadState(true);
 const auto &d=diagnostics;
 gEngfuncs.Con_Printf("oc2inventory: weapon=%d instance=%llu knife=%d equipped_knife=%d skin=%d swaps=%d revision=%u catalog=%u file=%d enabled=%d error=%d texture_cache=%u model=%s\n",
  d.weapon,static_cast<unsigned long long>(d.instance),d.knife,loadout.knife,d.skin,d.swaps,loadout.revision,unsigned(catalog.size()),
  int(configExists),int(Active()),int(!readError.empty()),unsigned(textures.size()),d.model.empty()?"none":d.model.c_str());
 gEngfuncs.Con_Printf("oc2inventory_stickers: weapon=%d selected=%d/%d/%d/%d textures=%d/%d/%d/%d charm=%d\n",
  d.weapon,d.stickers[0],d.stickers[1],d.stickers[2],d.stickers[3],d.slotTextures[0],d.slotTextures[1],d.slotTextures[2],d.slotTextures[3],
  OC2_InventoryCharmSelection(d.weapon));
 for(const auto &w:loadout.weapons)
  gEngfuncs.Con_Printf("oc2inventory_item: weapon=%d definition=%d instance=%llu skin=%d stickers=%d/%d/%d/%d charm=%d\n",
   w.weapon,Definition(w.weapon),static_cast<unsigned long long>(w.instance),w.skin,w.stickers[0],w.stickers[1],w.stickers[2],w.stickers[3],w.charm);
 for(const auto &v:loadout.knives)
  gEngfuncs.Con_Printf("oc2inventory_knife: definition=%d instance=%llu skin=%d equipped=%d\n",
   v.definition,static_cast<unsigned long long>(v.instance),v.skin,int(v.definition==loadout.knife));
 for(const auto &v:textures)
  gEngfuncs.Con_Printf("oc2inventory_texture: handle=%d owned=%d source=%s renderer_name=%s\n",
   v.second.handle,int(OwnTexture(v.second)),v.first.c_str(),v.second.name.c_str());
}
}

void OC2_InventoryInit()
{
 enabled=gEngfuncs.pfnRegisterVariable("cl_oc2_inventory","1",FCVAR_ARCHIVE);
 knifeInfo=gEngfuncs.pfnRegisterVariable("oc2_knife","0",FCVAR_USERINFO);
 gEngfuncs.pfnAddCommand("oc2_inventory_dump",Dump);
 gEngfuncs.pfnAddCommand("oc2_inventory_reload",Reload);
 ReadState(true);
}
void OC2_InventoryVidInit()
{
 ReleaseTextures();textureFrame=-1;catalog.clear();resourceIndices.clear();knifeIndices.clear();knifeModels.clear();catalogAttempted=false;nextRead=lastRead=-1;
 diagnostics=Diagnostics();ReadState(true);
}
int OC2_InventoryKnifeDefinition()
{ ReadState();return Active()?loadout.knife:0; }
float OC2_InventoryKnifeDuration(int animation,float fallback)
{
 ReadState();if(!Active()||!loadout.knife)return fallback;
 model_t *model=SelectedKnife();
 studiohdr_t *header=model?reinterpret_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model)):nullptr;
 if(!HeaderRange(header,header?header->seqindex:0,header?header->numseq:0,sizeof(mstudioseqdesc_t))||animation<0||animation>=header->numseq)return fallback;
 const auto &s=reinterpret_cast<const mstudioseqdesc_t *>(reinterpret_cast<const byte *>(header)+header->seqindex)[animation];
 if(!std::isfinite(s.fps)||s.fps<=0||s.numframes<2)return fallback;
 const float duration=float(std::ceil((s.numframes-1)*1000.0/s.fps)/1000.0);
 return duration>0&&duration<30?duration:fallback;
}
int OC2_InventoryCharmSelection(int weapon)
{
 ReadState();const WeaponState *state=Find(loadout,weapon);
 if(!Active()||!state)return 0;
 const Entry *entry=Resource(state->charm);
 return entry&&IsTransparent(*entry)?-1:state->charm;
}
bool OC2_InventoryCharmOverride(int weapon,int &texture,bool &hide)
{
 texture=0;hide=false;const int selection=OC2_InventoryCharmSelection(weapon);
 if(!selection)return false;
 const Entry *entry=Resource(selection);
 hide=selection<0||(entry&&IsTransparent(*entry));
 if(!hide)texture=StickerTexture(selection);
 return hide||texture!=0;
}
OC2_InventoryModelScope::OC2_InventoryModelScope(cl_entity_s *view)
{
 if(!view||view!=gEngfuncs.GetViewModel()||!view->model)return;
 ReadState();if(!Active()||!loadout.knife||!KnifeModel(view->model->name))return;
 model_t *selected=SelectedKnife();
 const studiohdr_t *header=selected?reinterpret_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(selected)):nullptr;
 if(!header||header->numseq<9)return;
 entity=view;original=view->model;view->model=selected;
}
OC2_InventoryModelScope::~OC2_InventoryModelScope()
{ if(entity)entity->model=original; }
OC2_InventoryMaterialScope::OC2_InventoryMaterialScope(CStudioModelRenderer &renderer)
{
 if(renderer.m_pCurrentEntity!=gEngfuncs.GetViewModel()||!renderer.m_pRenderModel)return;
 textureFrame=renderer.m_nFrameCount;
 ReadState();diagnostics=Diagnostics();
 diagnostics.model=renderer.m_pRenderModel->name;
 if(!Active()||!IEngineStudio.IsHardware())return;
 auto *header=renderer.m_pStudioHeader;
 if(!HeaderRange(header,header?header->textureindex:0,header?header->numtextures:0,sizeof(mstudiotexture_t)))return;
 auto *materials=reinterpret_cast<mstudiotexture_t *>(reinterpret_cast<byte *>(header)+header->textureindex);
 const int weapon=WeaponId(renderer.m_pRenderModel->name);
 const WeaponState *state=Find(loadout,weapon);
 const Entry *skin=nullptr;
 if(state)
 {
  diagnostics.weapon=weapon;diagnostics.instance=state->instance;diagnostics.skin=state->skin;diagnostics.stickers=state->stickers;
  skin=Resource(state->skin);
  if(skin&&(skin->kind!=Kind::GunSkin||skin->weapon!=weapon))skin=nullptr;
 }
 else if(KnifeModel(renderer.m_pRenderModel->name))
 {
  diagnostics.weapon=29;diagnostics.knife=loadout.knife;
  const KnifeState *knife=FindKnife(loadout,loadout.knife);
  if(knife){diagnostics.instance=knife->instance;diagnostics.skin=knife->skin;skin=Resource(knife->skin);}
  if(skin&&(skin->kind!=Kind::KnifeSkin||skin->definition!=loadout.knife))skin=nullptr;
 }
 const int bodyTexture=skin?Texture(skin->file,false):0;
 for(int i=0;i<header->numtextures;++i)
 {
  auto &material=materials[i];int replacement=0;
  if(!memchr(material.name,0,sizeof(material.name)))continue;
  if(bodyTexture&&MaterialMatches(skin->material,material.name))replacement=bodyTexture;
  if(state)for(int slot=0;slot<4;++slot)
  {
   char name[]="decal_A.bmp";name[6]=char('A'+slot);
   if(state->stickers[slot]&&!stricmp(material.name,name))
    {replacement=StickerTexture(state->stickers[slot]);diagnostics.slotTextures[slot]=replacement;break;}
  }
  if(!replacement)continue;
  saved.push_back(Saved(&material.index,material.index));material.index=replacement;++diagnostics.swaps;
 }
}
OC2_InventoryMaterialScope::~OC2_InventoryMaterialScope()
{ for(auto it=saved.rbegin();it!=saved.rend();++it)*it->address=it->value; }
