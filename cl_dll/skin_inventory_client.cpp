#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "cl_entity.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "render_api.h"
#include "oc2_skin_inventory.h"
#include "oc2_item_appearance.h"
#include "oc2_glove_inventory.h"
#include "skin_inventory_client.h"
#include "oc2_carried.h"
#include "oc2_world_stickers.h"
#include "parsemsg.h"
#include "demo_api.h"
#include "pm_shared.h"
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
oc2_gloves::Config gloveLoadout=oc2_gloves::DefaultConfig();
oc2_gloves::Catalog gloveCatalog;
std::array<int,33> playerGloves{};
bool gloveCatalogAttempted=false,gloveConfigExists=false,gloveNetworkReady=false;
std::string gloveReadError,lastGloveSent;
struct GloveDraw
{
 int player=0,resource=0,parts=0,body=0,skin=0,swaps=0,leftTexture=0,rightTexture=0;
 bool restored=true;
 double time=-1;std::string model;
} gloveDraw;
struct GloveShader
{
 std::string material;int width=0,height=0,original=0,replacement=0,during=0,after=0;
};
std::vector<GloveShader> gloveShaders;
struct CorpseGlove
{
 int tag=0,player=0,resource=0;model_t *model=nullptr;
};
std::map<const cl_entity_t *,CorpseGlove> corpseGloves;
struct WorldGloveDraw
{
 int player=0,resource=0,entity=0,tag=0,hidden=0,swaps=0;
 bool corpse=false,restored=false,legacy=false;double time=0;std::string model;
};
std::map<std::pair<int,int>,WorldGloveDraw> worldGloveDraws;
struct TextureSlot {int handle=0,frame=-1;std::uint64_t serial=0;std::string name;};
std::map<std::string,TextureSlot> textures;
int textureFrame=-1;std::uint64_t textureSerial=0;
std::map<int,std::size_t> resourceIndices,knifeIndices;
std::map<std::string,int> knifeModels;
double nextRead=-1,lastRead=-1;
bool catalogAttempted=false,configExists=false;
std::string readError;
cvar_t *enabled=nullptr,*knifeInfo=nullptr;
std::map<int,oc2_item::Appearance> items;
bool networkReady=false,resyncSent=false,itemSnapshotStarted=false;
std::string sessionLevel,lastSent,queuedConfig;
std::vector<std::string> upload;
std::size_t uploadIndex=0;
struct DrawRecord {int serial=0,entity=0,context=0,swaps=0;double time=0;std::string model;};
std::map<std::pair<int,int>,DrawRecord> draws;
struct Diagnostics
{
 int weapon=0,knife=0,skin=0,swaps=0;
 std::array<int,4> stickers{},slotTextures{};
 std::uint64_t instance=0;
 std::string model;
} diagnostics;

int ViewPlayer()
{
 if(g_iUser1==OBS_IN_EYE)return g_iUser2;
 const cl_entity_t *local=gEngfuncs.GetLocalPlayer();return local?local->index:0;
}
const oc2_item::Appearance *PlayerItem(int player,int weapon=0,bool active=true)
{
 if(player<1||player>32)return nullptr;
 for(const auto &row:items)
 {
  const auto &item=row.second;
  if(item.owner==player&&(!weapon||item.weapon==weapon)&&(!active||(item.flags&oc2_item::Active)))return &item;
 }
 return nullptr;
}
const oc2_item::Appearance *WorldItem(int entity,int weapon=0)
{
 if(entity<=0)return nullptr;
 for(const auto &row:items)
  if(!row.second.owner&&(!weapon||row.second.weapon==weapon)&&(row.second.box==entity||row.second.entity==entity))return &row.second;
 return nullptr;
}
void CheckSession()
{
 const char *level=gEngfuncs.pfnGetLevelName();
 if(sessionLevel!=(level?level:"")){OC2_InventoryResetSession();sessionLevel=level?level:"";}
}
int ItemMessage(const char *,int size,void *buffer)
{
 if(!buffer||size<oc2_item::kResetBytes)return 1;
 BufferReader reader(oc2_item::kMessage,buffer,size);
 if(reader.ReadByte()!=oc2_item::kVersion)return 1;
 const int operation=reader.ReadByte();
 if(operation==oc2_item::Reset&&size==oc2_item::kResetBytes)
 {
  CheckSession();items.clear();draws.clear();networkReady=true;itemSnapshotStarted=true;return 1;
 }
 if(operation==oc2_item::Remove&&size==oc2_item::kRemoveBytes)
 {
  const int serial=reader.ReadLong();items.erase(serial);
  for(auto i=draws.begin();i!=draws.end();)if(i->first.first==serial)i=draws.erase(i);else ++i;
  return 1;
 }
 if(operation!=oc2_item::Upsert||size!=oc2_item::kUpsertBytes)return 1;
 oc2_item::Appearance item;
 item.serial=reader.ReadLong();item.entity=reader.ReadShort();item.owner=reader.ReadByte();item.box=reader.ReadShort();item.weapon=reader.ReadByte();
 item.definition=reader.ReadShort();item.skin=reader.ReadLong();for(int &s:item.stickers)s=reader.ReadLong();
 item.originUserid=reader.ReadLong();item.flags=reader.ReadByte();
 if(item.serial<=0||item.entity<=0||item.owner>32||item.box<0||item.skin<0||item.definition<0||
    (oc2_accessories::Index(item.weapon)<0&&item.weapon!=29)||
    (item.flags&~(oc2_item::Active|oc2_item::BotOrigin|oc2_item::OriginOwner)))return 1;
 for(int s:item.stickers)if(s<-1)return 1;
 CheckSession();networkReady=true;
 // Entity slots can be reused. A new server identity retires the old entry;
 // one player can only have one active weapon even while a diff is arriving.
 for(auto i=items.begin();i!=items.end();)
 {
  if(i->first!=item.serial&&i->second.entity==item.entity)i=items.erase(i);
  else {if(item.owner&&(item.flags&oc2_item::Active)&&i->second.owner==item.owner)i->second.flags&=~oc2_item::Active;++i;}
 }
 if(items.size()<8192||items.count(item.serial))items[item.serial]=item;
 return 1;
}

int GloveMessage(const char *,int size,void *buffer)
{
 if(!buffer||size<oc2_gloves::kResetBytes)return 1;
 BufferReader reader(oc2_gloves::kMessage,buffer,size);
 if(reader.ReadByte()!=oc2_gloves::kVersion)return 1;
 const int operation=reader.ReadByte();
 if(operation==oc2_gloves::Reset&&size==oc2_gloves::kResetBytes)
 {
  CheckSession();playerGloves.fill(0);gloveNetworkReady=true;lastGloveSent.clear();return 1;
 }
 if(operation!=oc2_gloves::Upsert||size!=oc2_gloves::kUpsertBytes)return 1;
 const int player=reader.ReadByte(),resource=reader.ReadLong();
 if(player<1||player>32||resource<0)return 1;
 CheckSession();playerGloves[player]=resource;gloveNetworkReady=true;return 1;
}

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
void ReadGloveState(bool force)
{
 std::string text,error;bool exists=false;
 if(force||!gloveCatalogAttempted)
 {
  if(LoadText(oc2_gloves::kCatalogPath,text,int(oc2_gloves::kMaximumCatalogBytes),exists))
  {
   oc2_gloves::Catalog candidate;
   if(!oc2_gloves::ParseCatalog(text,candidate,error))goto failed;
   gloveCatalog.swap(candidate);gloveCatalogAttempted=true;
  }
  else if(exists){error="Cannot read glove catalog";goto failed;}
 }
 if(LoadText(oc2_gloves::kConfigPath,text,int(oc2_gloves::kMaximumConfigBytes),exists))
 {
  oc2_gloves::Config candidate;
  if(!oc2_gloves::ParseConfig(text,candidate,error)||!oc2_gloves::ValidateConfig(candidate,gloveCatalog,error))goto failed;
  gloveLoadout=candidate;gloveConfigExists=true;
 }
 else if(exists){error="Cannot read glove loadout";goto failed;}
 else {gloveLoadout=oc2_gloves::DefaultConfig();gloveConfigExists=false;}
 gloveReadError.clear();return;
failed:
 if(error!=gloveReadError&&!error.empty())gEngfuncs.Con_Printf("glove_inventory: %s\n",error.c_str());
 gloveReadError=error;
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
 ReadGloveState(force);
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
 const char *base=strrchr(name,'/');base=base?base+1:name;
 if(strlen(base)<3||base[1]!='_'||!strchr("vpw",base[0]))return 0;
 for(int i=0;i<24;++i)if(!stricmp(base+2,names[i]+2))return oc2_accessories::kWeaponIds[i];
 return 0;
}
bool KnifeModel(const char *name)
{
 if(!name||strnicmp(name,"models/",7)||strlen(name)<10||name[8]!='_'||!strchr("vpw",name[7]))return false;
 std::string view=Lower(name);view[7]='v';
 return view=="models/v_knife.mdl"||knifeModels.count(view)!=0;
}
const Entry *Resource(int id)
{
 const auto i=resourceIndices.find(id);return i==resourceIndices.end()?nullptr:&catalog[i->second];
}
model_t *SelectedKnife(int definition,char context='v')
{
 if(!definition)return nullptr;
 const auto i=knifeIndices.find(definition);
 const Entry *entry=i==knifeIndices.end()?nullptr:&catalog[i->second];
 if(!entry)return nullptr;
 std::string path=entry->model;path[7]=context;
 model_t *model=IEngineStudio.Mod_ForName(path.c_str(),false);
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
 // Protect this draw only. Other players and dropped weapons may use more
 // than the budget over one frame; their completed draws can be evicted.
 while(textures.size()>=32)
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
bool FamilyMaterialMatches(const studiohdr_t *header,const mstudiotexture_t *materials,int texture,const std::string &list)
{
 if(MaterialMatches(list,materials[texture].name))return true;
 if(header->numskinref<=0||header->numskinfamilies<=1||header->numskinfamilies>256||
    header->numskinref>header->length/int(sizeof(short))/header->numskinfamilies||
    !HeaderRange(header,header->skinindex,header->numskinref*header->numskinfamilies,sizeof(short)))return false;
 const auto *skinrefs=reinterpret_cast<const short *>(reinterpret_cast<const byte *>(header)+header->skinindex);
 for(int column=0;column<header->numskinref;++column)
 {
  const int base=skinrefs[column];
  if(base<0||base>=header->numtextures||!memchr(materials[base].name,0,sizeof(materials[base].name))||
     !MaterialMatches(list,materials[base].name))continue;
  for(int family=1;family<header->numskinfamilies;++family)
   if(skinrefs[family*header->numskinref+column]==texture)return true;
 }
 return false;
}
void Reload() { ReleaseTextures();OC2_ResetWorldStickers();catalogAttempted=false;ReadState(true); }
void GloveDump()
{
 ReadState(true);
 const int player=ViewPlayer();const auto *entry=OC2_InventoryPlayerGlove(player);
 gEngfuncs.Con_Printf("oc2glove: ready=%d player=%d selected=%d instance=%llu equipped=%d revision=%u catalog=%u file=%d enabled=%d error=%d model=%s\n",
  int(gloveNetworkReady),player,gloveLoadout.resource,static_cast<unsigned long long>(gloveLoadout.instance),entry?entry->id:0,
  gloveLoadout.revision,unsigned(gloveCatalog.size()),int(gloveConfigExists),int(Active()),int(!gloveReadError.empty()),entry?entry->model.c_str():"default");
 for(int index=1;index<=32;++index)if(playerGloves[index])
  gEngfuncs.Con_Printf("oc2glove_player: player=%d resource=%d known=%d\n",index,playerGloves[index],int(oc2_gloves::FindEntry(gloveCatalog,playerGloves[index])!=nullptr));
 gEngfuncs.Con_Printf("oc2glove_draw: player=%d resource=%d parts=%d body=%d skin=%d swaps=%d left_texture=%d right_texture=%d restored=%d time=%.3f model=%s\n",
  gloveDraw.player,gloveDraw.resource,gloveDraw.parts,gloveDraw.body,gloveDraw.skin,gloveDraw.swaps,gloveDraw.leftTexture,gloveDraw.rightTexture,int(gloveDraw.restored),gloveDraw.time,gloveDraw.model.empty()?"none":gloveDraw.model.c_str());
 for(const auto &slot:gloveShaders)
  gEngfuncs.Con_Printf("oc2glove_shader: material=%s width=%d height=%d original=%d replacement=%d during=%d after=%d\n",
   slot.material.c_str(),slot.width,slot.height,slot.original,slot.replacement,slot.during,slot.after);
 for(const auto &row:worldGloveDraws)
 {
  const auto &d=row.second;
  gEngfuncs.Con_Printf("oc2glove_world: player=%d resource=%d entity=%d corpse=%d tag=%d hidden=%d legacy=%d swaps=%d restored=%d time=%.3f model=%s\n",
   d.player,d.resource,d.entity,int(d.corpse),d.tag,d.hidden,int(d.legacy),d.swaps,int(d.restored),d.time,d.model.c_str());
 }
 if(gRenderAPI.RenderGetParm&&gloveDraw.leftTexture&&gloveDraw.rightTexture)
  gEngfuncs.Con_Printf("oc2glove_gpu: left_size=%dx%d right_size=%dx%d\n",
   int(gRenderAPI.RenderGetParm(PARM_TEX_WIDTH,gloveDraw.leftTexture)),int(gRenderAPI.RenderGetParm(PARM_TEX_HEIGHT,gloveDraw.leftTexture)),
   int(gRenderAPI.RenderGetParm(PARM_TEX_WIDTH,gloveDraw.rightTexture)),int(gRenderAPI.RenderGetParm(PARM_TEX_HEIGHT,gloveDraw.rightTexture)));
}
void NetDump()
{
 gEngfuncs.Con_Printf("oc2appearance: ready=%d items=%u upload_remaining=%u player=%d knife=%d time=%.3f\n",int(networkReady),unsigned(items.size()),unsigned(upload.size()-uploadIndex),ViewPlayer(),OC2_InventoryKnifeDefinition(),gEngfuncs.GetClientTime());
 for(const auto &row:items)
 {
  const auto &a=row.second;
  gEngfuncs.Con_Printf("oc2appearance_item: serial=%d entity=%d owner=%d box=%d weapon=%d definition=%d skin=%d stickers=%d/%d/%d/%d origin=%d flags=%d\n",a.serial,a.entity,a.owner,a.box,a.weapon,a.definition,a.skin,a.stickers[0],a.stickers[1],a.stickers[2],a.stickers[3],a.originUserid,a.flags);
 }
 for(const auto &row:draws)
 {
  const auto &d=row.second;
  gEngfuncs.Con_Printf("oc2appearance_draw: serial=%d entity=%d context=%c swaps=%d time=%.3f model=%s\n",d.serial,d.entity,d.context,d.swaps,d.time,d.model.c_str());
 }
}
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
 gEngfuncs.pfnAddCommand("oc2_inventory_net_dump",NetDump);
 gEngfuncs.pfnAddCommand("oc2_glove_dump",GloveDump);
 gEngfuncs.pfnHookUserMsg(oc2_item::kMessage,ItemMessage);
 gEngfuncs.pfnHookUserMsg(oc2_gloves::kMessage,GloveMessage);
 OC2_InventoryResetSession();
 ReadState(true);
}
void OC2_InventoryVidInit()
{
 OC2_ResetWorldStickers();
 ReleaseTextures();textureFrame=-1;catalog.clear();resourceIndices.clear();knifeIndices.clear();knifeModels.clear();catalogAttempted=false;nextRead=lastRead=-1;
 gloveCatalog.clear();gloveCatalogAttempted=false;gloveDraw=GloveDraw();
 diagnostics=Diagnostics();ReadState(true);
}
void OC2_InventoryResetSession()
{
 OC2_ResetWorldStickers();
 items.clear();draws.clear();networkReady=false;resyncSent=false;itemSnapshotStarted=false;lastSent.clear();queuedConfig.clear();upload.clear();uploadIndex=0;
 playerGloves.fill(0);gloveNetworkReady=false;lastGloveSent.clear();gloveDraw=GloveDraw();gloveShaders.clear();
 corpseGloves.clear();worldGloveDraws.clear();
}
void OC2_InventoryFrame()
{
 CheckSession();ReadState();
 const auto *local=gEngfuncs.GetLocalPlayer();
 if(sessionLevel.empty()||!local||local->index<1||gEngfuncs.IsSpectateOnly()||
    (gEngfuncs.pDemoAPI&&gEngfuncs.pDemoAPI->IsPlayingback()))return;
 if(!resyncSent){gEngfuncs.pfnServerCmd("oc2_loadout resync\n");resyncSent=true;}
 if(gloveNetworkReady)
 {
  const auto config=Active()?gloveLoadout:oc2_gloves::DefaultConfig();
  const std::string command="oc2_glove "+std::to_string(config.revision)+" "+std::to_string(config.resource)+"\n";
  if(command!=lastGloveSent){gEngfuncs.pfnServerCmd(command.c_str());lastGloveSent=command;}
 }
 if(!networkReady)return;
 const Config config=Active()?loadout:DefaultConfig();
 const std::string serialized=SerializeConfig(config);
 if(upload.empty()&&serialized!=lastSent)
 {
  upload.push_back("oc2_loadout begin "+std::to_string(config.revision)+"\n");
  std::istringstream input(serialized);std::string line;std::getline(input,line);
  while(std::getline(input,line))if(!line.empty())upload.push_back("oc2_loadout "+line+"\n");
  upload.push_back("oc2_loadout end\n");queuedConfig=serialized;uploadIndex=0;
 }
 // Keep the reliable command buffer bounded even with every knife equipped.
 for(int count=0,bytes=0;uploadIndex<upload.size()&&count<4;++count)
 {
  const std::string &command=upload[uploadIndex];
  if(bytes+int(command.size())>512)break;
  gEngfuncs.pfnServerCmd(command.c_str());bytes+=int(command.size());++uploadIndex;
 }
 if(!upload.empty()&&uploadIndex==upload.size()){lastSent=queuedConfig;upload.clear();uploadIndex=0;}
}
int OC2_InventoryPlayerKnifeDefinition(int player)
{
 ReadState();if(!Active())return 0;
 if(networkReady){const auto *item=PlayerItem(player,29,false);return item?item->definition:0;}
 return 0;
}
int OC2_InventoryKnifeDefinition()
{ return OC2_InventoryPlayerKnifeDefinition(ViewPlayer()); }
int OC2_InventoryViewPlayer() { return ViewPlayer(); }
model_s *OC2_InventoryWorldKnifeModel(int definition,int &displayDefinition)
{
 ReadState();
 displayDefinition=Active()?definition:0;
 if(displayDefinition)return SelectedKnife(displayDefinition,'w');
 model_t *model=IEngineStudio.Mod_ForName("models/w_knife.mdl",false);
 return model&&model->type==mod_studio?model:nullptr;
}
int OC2_InventoryOwnedItems(int player,oc2_item::Appearance *out,int capacity)
{
 if(!networkReady||!itemSnapshotStarted||player<1||player>32||!out||capacity<=0)return 0;
 int count=0;
 for(const auto &row:items)
 {
  if(row.second.owner!=player)continue;
  out[count++]=row.second;
  if(count==capacity)break;
 }
 return count;
}
const oc2_gloves::Entry *OC2_InventoryPlayerGlove(int player)
{
 ReadState();
 if(!Active()||!gloveNetworkReady||player<1||player>32)return nullptr;
 return oc2_gloves::FindEntry(gloveCatalog,playerGloves[player]);
}
void OC2_InventoryGloveDrawn(int player,int resource,int parts,const char *model,int body,int skin)
{
 gloveDraw.player=player;gloveDraw.resource=resource;gloveDraw.parts=parts;gloveDraw.model=model?model:"";
 gloveDraw.body=body;gloveDraw.skin=skin;gloveDraw.time=gEngfuncs.GetClientTime();
 if(!resource){gloveDraw.swaps=gloveDraw.leftTexture=gloveDraw.rightTexture=0;gloveDraw.restored=true;gloveShaders.clear();}
}
float OC2_InventoryKnifeDuration(int animation,float fallback)
{
 const int definition=OC2_InventoryKnifeDefinition();if(!definition)return fallback;
 model_t *model=SelectedKnife(definition);
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
 if(!OC2_InventoryOwnAccessories(weapon))return -1;
 if(!Active()||!state)return 0;
 const Entry *entry=Resource(state->charm);
 return entry&&IsTransparent(*entry)?-1:state->charm;
}
bool OC2_InventoryOwnAccessories(int weapon)
{
 const auto *local=gEngfuncs.GetLocalPlayer();
 if(!local||ViewPlayer()!=local->index)return false;
 if(!networkReady)return false;
 const auto *item=PlayerItem(local->index,weapon);
 return item&&(item->flags&oc2_item::OriginOwner)&&!(item->flags&oc2_item::BotOrigin);
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
 if(!view||!view->model)return;
 ReadState();if(!Active()||!KnifeModel(view->model->name))return;
 const bool firstPerson=view==gEngfuncs.GetViewModel();
 const auto *item=firstPerson?PlayerItem(ViewPlayer(),29):WorldItem(view->index,29);
 const int definition=networkReady&&item&&item->weapon==29?item->definition:0;
 model_t *selected=SelectedKnife(definition,firstPerson?'v':'w');
 const studiohdr_t *header=selected?reinterpret_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(selected)):nullptr;
 if(!header||header->numseq<(firstPerson?9:1))return;
 entity=view;original=view->model;view->model=selected;
}
OC2_InventoryModelScope::~OC2_InventoryModelScope()
{ if(entity)entity->model=original; }
model_s *OC2_InventoryPlayerModel(model_s *model,int player)
{
 ReadState();if(!model||!Active()||!KnifeModel(model->name))return model;
 const auto *item=PlayerItem(player,29);
 if(!networkReady||!item)return model;
 model_t *selected=SelectedKnife(item->definition,'p');return selected?selected:model;
}
OC2_InventoryMaterialScope::OC2_InventoryMaterialScope(CStudioModelRenderer &renderer,model_s *weaponModel,int player,int instance,char requestedContext,bool stickersOnly)
{
 model_t *model=weaponModel?weaponModel:renderer.m_pRenderModel;
 if(!renderer.m_pCurrentEntity||!model)return;
 const bool firstPerson=!requestedContext&&!weaponModel&&renderer.m_pCurrentEntity==gEngfuncs.GetViewModel();
 const char context=requestedContext?requestedContext:firstPerson?'v':instance?'c':weaponModel?'p':'w';
 if(context!='v'&&context!='p'&&context!='w'&&context!='c')return;
 if((context=='c')!=(instance>0))return;
 if(firstPerson)diagnostics=Diagnostics();
 const char modelContext=context=='c'?'w':context;
 const char *base=strrchr(model->name,'/');base=base?base+1:model->name;
 if(!networkReady||(!firstPerson&&base[0]!=modelContext))return;
 textureFrame=textureFrame==0x7fffffff?0:textureFrame+1;
 ReadState();Diagnostics current;current.model=model->name;
 if(!Active()||!IEngineStudio.IsHardware())return;
 auto *header=renderer.m_pStudioHeader;
 if(!HeaderRange(header,header?header->textureindex:0,header?header->numtextures:0,sizeof(mstudiotexture_t)))return;
 auto *materials=reinterpret_cast<mstudiotexture_t *>(reinterpret_cast<byte *>(header)+header->textureindex);
 const int weapon=WeaponId(model->name);
 const bool knifeModel=KnifeModel(model->name);
 if(!weapon&&!knifeModel)return;
 const auto found=items.find(instance);
 const auto *item=instance?(player>=1&&player<=32&&found!=items.end()&&found->second.owner==player?&found->second:nullptr):context=='v'?PlayerItem(ViewPlayer()):context=='p'?PlayerItem(player):WorldItem(renderer.m_pCurrentEntity->index,knifeModel?29:weapon);
 WeaponState effective;
 const WeaponState *state=nullptr;
 const Entry *skin=nullptr;
 if(networkReady)
 {
  if(!item||item->weapon!=(knifeModel?29:weapon))return;
  current.weapon=item->weapon;current.instance=item->serial;current.skin=item->skin;current.knife=knifeModel?item->definition:0;
  skin=Resource(item->skin);
  if(weapon)
  {
   effective.weapon=weapon;effective.skin=item->skin;effective.instance=item->serial;
   std::copy(item->stickers,item->stickers+4,effective.stickers.begin());state=&effective;
  }
 }
 if(state)current.stickers=state->stickers;
 if(stickersOnly)
 {
  if(!state)return;
  for(int slot=0;slot<4;++slot)
   if(state->stickers[slot]>0&&!StickerTexture(state->stickers[slot]))return;
  if(header->numskinref<=0||header->numskinfamilies<=0||
     !HeaderRange(header,header->skinindex,header->numskinref,sizeof(short))||
     !HeaderRange(header,header->bodypartindex,header->numbodyparts,sizeof(mstudiobodyparts_t)))return;
  auto *bytes=reinterpret_cast<byte *>(header);
  const auto *refs=reinterpret_cast<const short *>(bytes+header->skinindex);
  const auto *parts=reinterpret_cast<const mstudiobodyparts_t *>(bytes+header->bodypartindex);
  std::map<int,int> commands;
  for(int p=0;p<header->numbodyparts;++p)
  {
   if(!HeaderRange(header,parts[p].modelindex,parts[p].nummodels,sizeof(mstudiomodel_t)))return;
   const auto *models=reinterpret_cast<const mstudiomodel_t *>(bytes+parts[p].modelindex);
   for(int m=0;m<parts[p].nummodels;++m)
   {
    if(!HeaderRange(header,models[m].meshindex,models[m].nummesh,sizeof(mstudiomesh_t)))return;
    const auto *meshes=reinterpret_cast<const mstudiomesh_t *>(bytes+models[m].meshindex);
    for(int j=0;j<models[m].nummesh;++j)
    {
     const auto &mesh=meshes[j];
     if(mesh.skinref<0||mesh.skinref>=header->numskinref||refs[mesh.skinref]<0||
        refs[mesh.skinref]>=header->numtextures||mesh.triindex%int(sizeof(short))||
        !HeaderRange(header,mesh.triindex,1,sizeof(short)))return;
     const char *name=materials[refs[mesh.skinref]].name;
     if(!memchr(name,0,sizeof(materials[0].name)))return;
     int slot=-1;
     for(int s=0;s<4;++s){char expected[]="decal_A.bmp";expected[6]=char('A'+s);if(!stricmp(name,expected)){slot=s;break;}}
     if(slot<0)return;
     const auto previous=commands.find(mesh.triindex);
     if(previous!=commands.end()&&previous->second!=slot)return;
     commands[mesh.triindex]=slot;
    }
   }
  }
  for(const auto &command:commands)if(state->stickers[command.second]<0)
  {
   auto *first=reinterpret_cast<short *>(bytes+command.first);
   hidden.push_back(SavedShort(first,*first));*first=0;
  }
 }
 if(skin&&(weapon?(skin->kind!=Kind::GunSkin||skin->weapon!=weapon):(skin->kind!=Kind::KnifeSkin||skin->definition!=current.knife)))skin=nullptr;
 const int bodyTexture=skin&&!stickersOnly?Texture(skin->file,false):0;
 for(int i=0;i<header->numtextures;++i)
 {
  auto &material=materials[i];int replacement=0;
  if(!memchr(material.name,0,sizeof(material.name)))continue;
  if(bodyTexture&&FamilyMaterialMatches(header,materials,i,skin->material))replacement=bodyTexture;
  if(state)for(int slot=0;slot<4;++slot)
  {
   char name[]="decal_A.bmp";name[6]=char('A'+slot);
   if(state->stickers[slot]&&(!stickersOnly||state->stickers[slot]>0)&&!stricmp(material.name,name))
    {replacement=StickerTexture(state->stickers[slot]);current.slotTextures[slot]=replacement;break;}
  }
  if(!replacement)continue;
  saved.push_back(Saved(&material.index,material.index));material.index=replacement;++current.swaps;
 }
 if(firstPerson)diagnostics=current;
 ready=true;
 if(networkReady&&item&&!stickersOnly)
 {
  DrawRecord record;record.serial=item->serial;record.entity=renderer.m_pCurrentEntity->index;record.context=context;record.swaps=current.swaps;record.time=gEngfuncs.GetClientTime();record.model=current.model;
  draws[std::make_pair(item->serial,int(context))]=record;
 }
}
OC2_InventoryMaterialScope::~OC2_InventoryMaterialScope()
{
 for(auto it=saved.rbegin();it!=saved.rend();++it)*it->address=it->value;
 for(auto it=hidden.rbegin();it!=hidden.rend();++it)*it->address=it->value;
}

OC2_InventoryGloveMaterialScope::OC2_InventoryGloveMaterialScope(CStudioModelRenderer &renderer,int resource,bool diagnostics):record(diagnostics)
{
 if(!resource){ready=true;return;}
 if(record)
 {
  gloveDraw.swaps=gloveDraw.leftTexture=gloveDraw.rightTexture=0;
  gloveDraw.restored=true;gloveShaders.clear();
 }
 const auto *entry=oc2_gloves::FindEntry(gloveCatalog,resource);
 if(!entry||!Active())return;
 if(entry->leftTexture.empty()&&entry->rightTexture.empty()){ready=true;return;}
 if(!IEngineStudio.IsHardware())return;
 auto *header=renderer.m_pStudioHeader;
 if(!HeaderRange(header,header?header->textureindex:0,header?header->numtextures:0,sizeof(mstudiotexture_t)))return;
 auto *materials=reinterpret_cast<mstudiotexture_t *>(reinterpret_cast<byte *>(header)+header->textureindex);
 bool left=false,right=false;
 for(int index=0;index<header->numtextures;++index)
 {
  if(!memchr(materials[index].name,0,sizeof(materials[index].name)))continue;
  left=left||FamilyMaterialMatches(header,materials,index,entry->leftMaterial);
  right=right||FamilyMaterialMatches(header,materials,index,entry->rightMaterial);
 }
 if(!left||!right)return;
 textureFrame=textureFrame==0x7fffffff?0:textureFrame+1;
 const int leftTexture=Texture(entry->leftTexture,false),rightTexture=Texture(entry->rightTexture,false);
 if(!leftTexture||!rightTexture)return;
 for(int index=0;index<header->numtextures;++index)
 {
  auto &material=materials[index];if(!memchr(material.name,0,sizeof(material.name)))continue;
  int replacement=0;
  if(FamilyMaterialMatches(header,materials,index,entry->leftMaterial))replacement=leftTexture;
  else if(FamilyMaterialMatches(header,materials,index,entry->rightMaterial))replacement=rightTexture;
  if(!replacement)continue;
  // Keep MDL dimensions, normalized UVs, flags and bare skin families intact.
  saved.push_back(Saved(&material.index,material.index));
  if(record)
  {
   GloveShader slot;slot.material=material.name;slot.width=material.width;slot.height=material.height;
   slot.original=material.index;slot.replacement=replacement;gloveShaders.push_back(slot);
  }
  material.index=replacement;
 }
 ready=true;
 if(record){gloveDraw.swaps=int(saved.size());gloveDraw.leftTexture=leftTexture;gloveDraw.rightTexture=rightTexture;}
}
OC2_InventoryGloveMaterialScope::~OC2_InventoryGloveMaterialScope()
{
 if(record)for(std::size_t index=0;index<saved.size();++index)gloveShaders[index].during=*saved[index].address;
 for(auto it=saved.rbegin();it!=saved.rend();++it)*it->address=it->value;
 if(record)for(std::size_t index=0;index<saved.size();++index)
 {
  gloveShaders[index].after=*saved[index].address;
  gloveDraw.restored=gloveDraw.restored&&gloveShaders[index].after==gloveShaders[index].original;
 }
}

void OC2_InventoryCaptureCorpseGlove(cl_entity_s *entity,int player)
{
 if(!entity||player<1||player>32)return;
 const auto *entry=OC2_InventoryPlayerGlove(player);
 CorpseGlove state;state.tag=entity->curstate.iuser3;state.player=player;
 state.resource=entry?entry->id:0;state.model=entity->model;
 // Temp entities are pooled. The generation tag rejects a reused slot;
 // the copied resource keeps an old corpse independent of a respawn/equip.
 if(corpseGloves.size()>=128&&!corpseGloves.count(entity))corpseGloves.erase(corpseGloves.begin());
 corpseGloves[entity]=state;
}

namespace
{
bool LegacyGloveBody(const model_t *model)
{
 return model&&(!stricmp(model->name,"models/oc2/gloves/players/guerilla.mdl")||
                !stricmp(model->name,"models/oc2/gloves/players/vip.mdl"));
}
model_t *GloveBodyModel(model_t *original,int resource)
{
 const auto *choice=oc2_gloves::FindEntry(gloveCatalog,resource);
 if(!original||!choice||!Active()||!IEngineStudio.IsHardware())return original;
 const char *path=nullptr;
 if(!stricmp(original->name,"models/player/guerilla/guerilla.mdl"))path="models/oc2/gloves/players/guerilla.mdl";
 else if(!stricmp(original->name,"models/player/vip/vip.mdl"))path="models/oc2/gloves/players/vip.mdl";
 if(!path)return original;
 const std::string world=oc2_gloves::WorldModelPath(*choice);
 auto *wearable=IEngineStudio.Mod_ForName(world.c_str(),false);
 if(!wearable||wearable->type!=mod_studio)return original;
 textureFrame=textureFrame==0x7fffffff?0:textureFrame+1;
 if((!choice->leftTexture.empty()&&!Texture(choice->leftTexture,false))||
    (!choice->rightTexture.empty()&&!Texture(choice->rightTexture,false)))return original;
 auto *selected=IEngineStudio.Mod_ForName(path,false);
 return selected&&selected->type==mod_studio?selected:original;
}
}
OC2_InventoryGloveBodyModelScope::OC2_InventoryGloveBodyModelScope(CStudioModelRenderer &studio,int player):renderer(studio)
{
 auto *entity=renderer.m_pCurrentEntity;
 if(!entity||!renderer.m_pRenderModel||!renderer.m_pStudioHeader)return;
 int resource=0;
 if(player)
 {
  const auto *entry=OC2_InventoryPlayerGlove(player);resource=entry?entry->id:0;
 }
 else
 {
  const auto found=corpseGloves.find(entity);
  if(found==corpseGloves.end()||found->second.tag!=entity->curstate.iuser3||found->second.model!=entity->model)return;
  resource=found->second.resource;
 }
 auto *selected=GloveBodyModel(renderer.m_pRenderModel,resource);
 if(selected==renderer.m_pRenderModel)return;
 auto *selectedHeader=static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(selected));
 auto *ownerHeader=renderer.m_pStudioHeader;
 // This is an explicit canonical/derived resource pair. Require the exact
 // same named skeleton before using the owner's already solved matrices.
 if(!HeaderRange(selectedHeader,selectedHeader?selectedHeader->boneindex:0,selectedHeader?selectedHeader->numbones:0,sizeof(mstudiobone_t))||
    !HeaderRange(ownerHeader,ownerHeader->boneindex,ownerHeader->numbones,sizeof(mstudiobone_t))||
    selectedHeader->numbones!=ownerHeader->numbones||
    memcmp(reinterpret_cast<const byte *>(selectedHeader)+selectedHeader->boneindex,
           reinterpret_cast<const byte *>(ownerHeader)+ownerHeader->boneindex,ownerHeader->numbones*sizeof(mstudiobone_t)))return;
 original=renderer.m_pRenderModel;header=ownerHeader;bodyPart=renderer.m_pBodyPart;subModel=renderer.m_pSubModel;
 renderer.m_pRenderModel=selected;renderer.m_pStudioHeader=selectedHeader;
 IEngineStudio.StudioSetHeader(selectedHeader);IEngineStudio.SetRenderModel(selected);
}
OC2_InventoryGloveBodyModelScope::~OC2_InventoryGloveBodyModelScope()
{
 if(!original)return;
 renderer.m_pRenderModel=original;renderer.m_pStudioHeader=static_cast<studiohdr_t *>(header);
 renderer.m_pBodyPart=static_cast<mstudiobodyparts_t *>(bodyPart);renderer.m_pSubModel=static_cast<mstudiomodel_t *>(subModel);
 IEngineStudio.StudioSetHeader(renderer.m_pStudioHeader);IEngineStudio.SetRenderModel(original);
}

OC2_InventoryPlayerGloveScope::OC2_InventoryPlayerGloveScope(CStudioModelRenderer &studio,int index):renderer(studio),player(index)
{
 auto *entity=renderer.m_pCurrentEntity;
 if(!entity||!renderer.m_pStudioHeader||!IEngineStudio.IsHardware()||!Active())return;
 if(!player)
 {
  const auto found=corpseGloves.find(entity);
  if(found==corpseGloves.end()||found->second.tag!=entity->curstate.iuser3||
     (found->second.model!=entity->model&&!LegacyGloveBody(entity->model)))return;
  player=found->second.player;resource=found->second.resource;corpse=true;corpseTag=found->second.tag;
 }
 else
 {
  const auto *choice=OC2_InventoryPlayerGlove(player);resource=choice?choice->id:0;
 }
 const auto *choice=oc2_gloves::FindEntry(gloveCatalog,resource);
 if(!choice)return;
 // Every supported player must expose both actual hand bones. Never merge a
 // viewmodel skeleton by approximate names or recolor the character's UVs.
 bool left=false,right=false;
 for(int i=0;i<renderer.m_nCachedBones;++i)
 {
  left=left||!stricmp(renderer.m_nCachedBoneNames[i],"Bip01 L Hand");
  right=right||!stricmp(renderer.m_nCachedBoneNames[i],"Bip01 R Hand");
 }
 if(!left||!right)return;
 const std::string path=oc2_gloves::WorldModelPath(*choice);
 model=IEngineStudio.Mod_ForName(path.c_str(),false);
 auto *gloveHeader=model&&model->type==mod_studio?static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model)):nullptr;
 if(!HeaderRange(gloveHeader,gloveHeader?gloveHeader->boneindex:0,gloveHeader?gloveHeader->numbones:0,sizeof(mstudiobone_t))||
    gloveHeader->numbones>MAXSTUDIOBONES){model=nullptr;return;}
 auto *bodyHeader=renderer.m_pStudioHeader;
 renderer.m_pStudioHeader=gloveHeader;
 bool ready=false;
 {OC2_InventoryGloveMaterialScope materials(renderer,resource,false);ready=materials.Ready();}
 renderer.m_pStudioHeader=bodyHeader;
 if(!ready){model=nullptr;return;}
 if(!HeaderRange(bodyHeader,bodyHeader->bodypartindex,bodyHeader->numbodyparts,sizeof(mstudiobodyparts_t)))return;
 auto *parts=reinterpret_cast<mstudiobodyparts_t *>(reinterpret_cast<byte *>(bodyHeader)+bodyHeader->bodypartindex);
 if(!HeaderRange(bodyHeader,bodyHeader->boneindex,bodyHeader->numbones,sizeof(mstudiobone_t)))return;
 const auto *bones=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<byte *>(bodyHeader)+bodyHeader->boneindex);
 for(int p=0;p<bodyHeader->numbodyparts;++p)
 {
  const auto &part=parts[p];
  if(part.base<=0||part.nummodels<=0||!HeaderRange(bodyHeader,part.modelindex,part.nummodels,sizeof(mstudiomodel_t)))continue;
  auto &sub=reinterpret_cast<mstudiomodel_t *>(reinterpret_cast<byte *>(bodyHeader)+part.modelindex)[(entity->curstate.body/part.base)%part.nummodels];
  if(!memchr(sub.name,0,sizeof(sub.name))||sub.numverts<=0||sub.nummesh<=0||
     !HeaderRange(bodyHeader,sub.vertinfoindex,sub.numverts,1))continue;
  const std::string name=oc2_inventory::Lower(sub.name);
  if(name.find("glove")==std::string::npos&&name.find("hand")==std::string::npos)continue;
  const auto *bindings=reinterpret_cast<const byte *>(bodyHeader)+sub.vertinfoindex;
  bool handsOnly=true;
  for(int v=0;v<sub.numverts&&handsOnly;++v)
  {
   if(bindings[v]>=bodyHeader->numbones){handsOnly=false;break;}
   const auto &bone=bones[bindings[v]];
   if(!memchr(bone.name,0,sizeof(bone.name))){handsOnly=false;break;}
   const std::string boneName=oc2_inventory::Lower(bone.name);
   handsOnly=boneName.find("hand")!=std::string::npos||boneName.find("wrist")!=std::string::npos||boneName.find("forearm")!=std::string::npos;
  }
  if(handsOnly)hidden.push_back(Saved(&sub.nummesh,sub.nummesh));
 }
 replacementBody=LegacyGloveBody(renderer.m_pRenderModel);
 if(hidden.empty()&&!replacementBody){model=nullptr;return;}
 header=gloveHeader;
 for(auto &part:hidden)*part.address=0;
}

OC2_InventoryPlayerGloveScope::~OC2_InventoryPlayerGloveScope()
{
 for(auto it=hidden.rbegin();it!=hidden.rend();++it)*it->address=it->value;
 auto found=worldGloveDraws.find(std::make_pair(player,corpse?corpseTag:0));
 if(found!=worldGloveDraws.end()&&found->second.resource==resource&&found->second.time==renderer.m_clTime)
  for(const auto &part:hidden)found->second.restored=found->second.restored&&*part.address==part.value;
}

void OC2_InventoryPlayerGloveScope::Draw(alight_s &lighting,float *lightdir)
{
 if(!model||!header||(hidden.empty()&&!replacementBody))return;
 auto *bodyHeader=renderer.m_pStudioHeader;
 auto *bodyModel=renderer.m_pRenderModel;
 const cl_entity_t savedEntity=*renderer.m_pCurrentEntity;
 auto *savedPart=renderer.m_pBodyPart;auto *savedSub=renderer.m_pSubModel;
 const int faceFlags=IEngineStudio.GetForceFaceFlags();
 renderer.m_pRenderModel=model;renderer.m_pStudioHeader=static_cast<studiohdr_t *>(header);
 auto &state=renderer.m_pCurrentEntity->curstate;
 renderer.m_pCurrentEntity->model=model;state.body=0;state.skin=0;state.sequence=0;state.frame=0;state.framerate=0;state.animtime=renderer.m_clTime;
 IEngineStudio.StudioSetHeader(renderer.m_pStudioHeader);IEngineStudio.SetRenderModel(model);
 WorldGloveDraw record;record.player=player;record.resource=resource;record.corpse=corpse;record.tag=corpseTag;
 record.legacy=replacementBody;
 record.entity=savedEntity.index;record.hidden=int(hidden.size());record.time=renderer.m_clTime;record.model=model->name;
 {
  OC2_InventoryGloveMaterialScope materials(renderer,resource,false);
  if(materials.Ready())
  {
   renderer.StudioMergeBones(model);
   IEngineStudio.StudioSetupLighting(&lighting);
   IEngineStudio.StudioSetRemapColors(renderer.m_nTopColor,renderer.m_nBottomColor);
   renderer.StudioRenderModel(lightdir);record.swaps=materials.Swaps();
  }
 }
 *renderer.m_pCurrentEntity=savedEntity;renderer.m_pRenderModel=bodyModel;renderer.m_pStudioHeader=bodyHeader;
 renderer.m_pBodyPart=savedPart;renderer.m_pSubModel=savedSub;
 // StudioSaveBones remains the owner's pose for following weapon/addon draws.
 for(int i=0;i<renderer.m_nCachedBones;++i)
 {
  memcpy((*renderer.m_pbonetransform)[i],renderer.m_rgCachedBoneTransform[i],sizeof(float)*12);
  memcpy((*renderer.m_plighttransform)[i],renderer.m_rgCachedLightTransform[i],sizeof(float)*12);
 }
 IEngineStudio.StudioSetHeader(bodyHeader);IEngineStudio.SetRenderModel(bodyModel);
 IEngineStudio.StudioSetupLighting(&lighting);IEngineStudio.StudioSetRemapColors(renderer.m_nTopColor,renderer.m_nBottomColor);
 IEngineStudio.SetForceFaceFlags(faceFlags);
 record.restored=renderer.m_pStudioHeader==bodyHeader&&renderer.m_pCurrentEntity->model==savedEntity.model;
 for(int i=0;i<renderer.m_nCachedBones;++i)
  record.restored=record.restored&&!memcmp((*renderer.m_pbonetransform)[i],renderer.m_rgCachedBoneTransform[i],sizeof(float)*12)&&
   !memcmp((*renderer.m_plighttransform)[i],renderer.m_rgCachedLightTransform[i],sizeof(float)*12);
 worldGloveDraws[std::make_pair(player,corpse?corpseTag:0)]=record;
 // Diagnostics are bounded even during long sessions with many corpses.
 if(worldGloveDraws.size()>128)worldGloveDraws.erase(worldGloveDraws.begin());
}
