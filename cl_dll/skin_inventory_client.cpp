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
#include "oc2_cold_perf.h"
#include "oc2_texture_stream.h"
#include "accessory_client.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <chrono>

extern engine_studio_api_t IEngineStudio;
extern render_api_t gRenderAPI;
namespace
{
using namespace oc2_inventory;
Config loadout=DefaultConfig();Catalog catalog;
std::string parsedLoadout,parsedGloves;
unsigned configReadCalls=0;double configReadMs=0,configReadMax=0;
cvar_t *configPoll=nullptr,*worldGlovesEnabled=nullptr;
cvar_t *textureStreaming=nullptr;
int textureUploadFrame=-1,textureUploadsThisFrame=0;
double textureUploadMs=0;
unsigned texturePrefetchEpoch=0;
struct ConfigReadTimer
{
 std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
 ~ConfigReadTimer(){const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();++configReadCalls;configReadMs+=ms;configReadMax=std::max(configReadMax,ms);}
};
void ConfigPerfDump(){gEngfuncs.Con_Printf("oc2_config_perf: calls=%u total_ms=%.5f max_ms=%.5f\n",configReadCalls,configReadMs,configReadMax);configReadCalls=0;configReadMs=configReadMax=0;}
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
// A body's hand bindings are immutable for its model lifetime. Never repeat
// string allocation and bone-name classification for every vertex each frame.
std::map<std::pair<const studiohdr_t *,const mstudiomodel_t *>,bool> worldHandLayouts;
struct TextureSlot {int handle=0,frame=-1,visibleFrame=-1,borrowedFrame=-1;std::uint64_t serial=0,bytes=0;std::string name;};
std::map<std::string,TextureSlot> textures;
std::map<std::string,int> textureBlockedFrame;
std::map<std::string,std::uint64_t> textureKnownCost;
std::map<std::string,std::string> textureVariantAliases;
std::map<int,unsigned> texturePins;
struct PendingTexture {int handle=0;bool priority=false;std::string key,name;};
PendingTexture pendingTexture;
int textureFrame=-1,textureVisibleFrame=-1;std::uint64_t textureSerial=0,textureBytes=0;
cvar_t *textureBudget=nullptr;
struct TextureStats
{
 std::uint64_t hits=0,misses=0,reads=0,uploads=0,evictions=0,invalid=0,blocked=0,failures=0,peakBytes=0;
 std::uint64_t layoutHits=0,layoutBuilds=0,sameFrameEvictions=0,resized=0,variantFallbacks=0;
} textureStats;
struct OverlayLayout
{
 const studiohdr_t *header=nullptr;std::string model;
 int length=0,textureindex=0,skinindex=0,bodypartindex=0,numtextures=0,numskinref=0,numbodyparts=0;
 std::vector<std::pair<int,int>> commands;
};
std::map<const model_t *,OverlayLayout> overlayLayouts;
struct MaterialLayout
{
 int length=0,textureindex=0,numtextures=0,skinindex=0,numskinref=0,numskinfamilies=0;
 std::vector<int> decals;
 std::map<std::string,std::vector<unsigned char>> families;
};
std::map<const studiohdr_t *,MaterialLayout> materialLayouts;
std::map<int,std::size_t> resourceIndices,knifeIndices;
std::map<std::string,int> knifeModels;
double nextRead=-1,lastRead=-1;
bool catalogAttempted=false,configExists=false;
std::string readError;
cvar_t *enabled=nullptr,*knifeInfo=nullptr;
std::map<int,oc2_item::Appearance> items;
struct ItemQueryIndex
{
 bool dirty=true;
 std::array<std::vector<int>,33> owned;
 std::map<std::pair<int,int>,int> player,active,world;
} itemQueryIndex;
std::map<int,std::string> accessorySent;
bool networkReady=false,resyncSent=false,itemSnapshotStarted=false;
std::string sessionLevel,lastSent,queuedConfig;
std::vector<std::string> upload;
std::size_t uploadIndex=0;
struct DrawRecord {int serial=0,entity=0,context=0,swaps=0;double time=0;std::string model;};
std::map<std::pair<int,int>,DrawRecord> draws;
struct Diagnostics
{
 int weapon=0,knife=0,skin=0,swaps=0,bodyTexture=0;bool pendingBody=false;
 std::array<int,4> stickers{},slotTextures{};
 std::uint64_t instance=0;
 std::string model;
} diagnostics;

int ViewPlayer()
{
 if(g_iUser1==OBS_IN_EYE)return g_iUser2;
 const cl_entity_t *local=gEngfuncs.GetLocalPlayer();return local?local->index:0;
}
void ClearItemQueryIndex()
{
 for(auto &serials:itemQueryIndex.owned)serials.clear();
 itemQueryIndex.player.clear();itemQueryIndex.active.clear();itemQueryIndex.world.clear();
 itemQueryIndex.dirty=true;
}
void EnsureItemQueryIndex()
{
 if(!itemQueryIndex.dirty)return;
 ClearItemQueryIndex();
 const auto first=[](std::map<std::pair<int,int>,int> &index,int identity,int weapon,int serial)
 {
  // emplace retains the first serial from the ordered authoritative map.
  index.emplace(std::make_pair(identity,0),serial);
  index.emplace(std::make_pair(identity,weapon),serial);
 };
 for(const auto &row:items)
 {
  const auto &item=row.second;const int serial=row.first;
  if(item.owner>0&&item.owner<=32)
  {
   itemQueryIndex.owned[item.owner].push_back(serial);
   first(itemQueryIndex.player,item.owner,item.weapon,serial);
   if(item.flags&oc2_item::Active)first(itemQueryIndex.active,item.owner,item.weapon,serial);
  }
  else if(!item.owner)
  {
   if(item.entity>0)first(itemQueryIndex.world,item.entity,item.weapon,serial);
   if(item.box>0&&item.box!=item.entity)first(itemQueryIndex.world,item.box,item.weapon,serial);
  }
 }
 itemQueryIndex.dirty=false;
}
const oc2_item::Appearance *PlayerItem(int player,int weapon=0,bool active=true)
{
 if(player<1||player>32)return nullptr;
 EnsureItemQueryIndex();
 const auto &index=active?itemQueryIndex.active:itemQueryIndex.player;
 const auto hit=index.find(std::make_pair(player,weapon));
 if(hit==index.end())return nullptr;
 const auto found=items.find(hit->second);
 return found==items.end()?nullptr:&found->second;
}
// Predicted switching can precede the Active flag update. Resolve by the
// actual viewmodel weapon within this player's authoritative owned instances;
// never borrow the previously active gun's skin or attachment configuration.
const oc2_item::Appearance *ViewItem(int weapon)
{
 const int player=ViewPlayer();const auto *active=PlayerItem(player,weapon);
 return active?active:PlayerItem(player,weapon,false);
}
const oc2_item::Appearance *WorldItem(int entity,int weapon=0)
{
 if(entity<=0)return nullptr;
 EnsureItemQueryIndex();
 const auto hit=itemQueryIndex.world.find(std::make_pair(entity,weapon));
 if(hit==itemQueryIndex.world.end())return nullptr;
 const auto found=items.find(hit->second);
 return found==items.end()?nullptr:&found->second;
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
   CheckSession();items.clear();ClearItemQueryIndex();draws.clear();networkReady=true;itemSnapshotStarted=true;++texturePrefetchEpoch;return 1;
 }
 if(operation==oc2_item::Remove&&size==oc2_item::kRemoveBytes)
 {
   const int serial=reader.ReadLong();items.erase(serial);itemQueryIndex.dirty=true;++texturePrefetchEpoch;
  for(auto i=draws.begin();i!=draws.end();)if(i->first.first==serial)i=draws.erase(i);else ++i;
  return 1;
 }
 if(operation!=oc2_item::Upsert||size!=oc2_item::kUpsertBytes)return 1;
 oc2_item::Appearance item;
 item.serial=reader.ReadLong();item.entity=reader.ReadShort();item.owner=reader.ReadByte();item.box=reader.ReadShort();item.weapon=reader.ReadByte();
 item.definition=reader.ReadShort();item.skin=reader.ReadLong();for(int &s:item.stickers)s=reader.ReadLong();
 item.originUserid=reader.ReadLong();item.flags=reader.ReadByte();
 const int accessories=reader.ReadByte();item.accessoryReady=(accessories&4)!=0;
 auto &w=item.accessory;w.weapon=item.weapon;w.stattrak=(accessories&1)!=0;w.charm=(accessories&2)!=0;
 float *values[]={&w.statU,&w.statV,&w.charmU,&w.charmV,&w.amplitude,&w.length,&w.stiffness,&w.damping};
 for(auto *v:values)*v=oc2_item::BitsFloat(reader.ReadLong());
 w.sticker=reader.ReadLong();item.count=unsigned(reader.ReadLong());item.charm=reader.ReadLong();
 if((accessories&~7)||item.count>oc2_accessories::kMaximumCount||item.charm<-1||
    (item.accessoryReady&&(!oc2_accessories::Valid(w)&&item.weapon!=29)))return 1;
 if(item.serial<=0||item.entity<=0||item.owner>32||item.box<0||item.skin<0||item.definition<0||
    (oc2_accessories::Index(item.weapon)<0&&item.weapon!=29)||
    (item.flags&~(oc2_item::Active|oc2_item::BotOrigin|oc2_item::OriginOwner)))return 1;
 for(int s:item.stickers)if(s<-1)return 1;
 CheckSession();networkReady=true;itemQueryIndex.dirty=true;
 const auto previousAppearance=items.find(item.serial);
 if(previousAppearance==items.end()||previousAppearance->second.owner!=item.owner||previousAppearance->second.skin!=item.skin||previousAppearance->second.charm!=item.charm||
    !std::equal(item.stickers,item.stickers+4,previousAppearance->second.stickers))++texturePrefetchEpoch;
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
  CheckSession();if(playerGloves[player]!=resource)++texturePrefetchEpoch;playerGloves[player]=resource;gloveNetworkReady=true;return 1;
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
   gloveCatalog.swap(candidate);gloveCatalogAttempted=true;++texturePrefetchEpoch;
  }
  else if(exists){error="Cannot read glove catalog";goto failed;}
 }
 if(LoadText(oc2_gloves::kConfigPath,text,int(oc2_gloves::kMaximumConfigBytes),exists))
 {
  if(!force&&gloveConfigExists&&text==parsedGloves){gloveReadError.clear();return;}
  oc2_gloves::Config candidate;
  if(!oc2_gloves::ParseConfig(text,candidate,error)||!oc2_gloves::ValidateConfig(candidate,gloveCatalog,error))goto failed;
  gloveLoadout=candidate;gloveConfigExists=true;parsedGloves=text;
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
 if(!force&&configPoll&&configPoll->value==0)return;
 const double time=gEngfuncs.GetClientTime();
 if(!force&&time>=lastRead&&time<nextRead)return;
 ConfigReadTimer timer;
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
   catalog.swap(candidate);resourceIndices.clear();knifeIndices.clear();knifeModels.clear();++texturePrefetchEpoch;
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
  if(!force&&configExists&&text==parsedLoadout){ReportError("");SyncKnifeInfo();return;}
  Config candidate;
  if(!ParseConfig(text,candidate,error)||!ValidateConfig(candidate,catalog,error))
   {ReportError(error);SyncKnifeInfo();return;}
  loadout=candidate;configExists=true;parsedLoadout=text;
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
void CancelTextureUpload()
{
 if(pendingTexture.handle){TextureSlot slot;slot.handle=pendingTexture.handle;slot.name=pendingTexture.name;ReleaseTexture(slot);pendingTexture=PendingTexture();}
}
void ReleaseTextures()
{ CancelTextureUpload();for(const auto &v:textures)ReleaseTexture(v.second);textures.clear();textureBlockedFrame.clear();textureKnownCost.clear();textureVariantAliases.clear();overlayLayouts.clear();materialLayouts.clear();textureBytes=0; }
std::uint64_t TextureBudgetBytes()
{
 const float fallback=sizeof(void*)>=8?1536.f:768.f;
 const float maximum=sizeof(void*)>=8?4096.f:1024.f;
 const float mb=textureBudget&&std::isfinite(textureBudget->value)?textureBudget->value:fallback;
 return std::uint64_t(std::max(32.f,std::min(maximum,mb)))*1024u*1024u;
}
std::size_t TextureMaximumEntries(){return sizeof(void*)>=8?512:256;}
bool EvictTexture();
void BeginTextureDraw()
{
 textureFrame=textureFrame==0x7fffffff?0:textureFrame+1;
 int visibleFrame=textureFrame;
 if(IEngineStudio.GetTimes)IEngineStudio.GetTimes(&visibleFrame,nullptr,nullptr);
 if(textureVisibleFrame!=visibleFrame){textureVisibleFrame=visibleFrame;textureBlockedFrame.clear();}
 while(textureBytes>TextureBudgetBytes()&&EvictTexture()){}
}
bool EvictTexture()
{
 auto oldest=textures.end();
 for(auto i=textures.begin();i!=textures.end();++i)
  if(i->second.frame!=textureFrame&&i->second.visibleFrame!=textureVisibleFrame&&i->second.borrowedFrame!=textureVisibleFrame&&!texturePins.count(i->second.handle)&&
     (oldest==textures.end()||i->second.serial<oldest->second.serial))oldest=i;
 if(oldest==textures.end())
  for(auto i=textures.begin();i!=textures.end();++i)
   if(i->second.frame!=textureFrame&&i->second.borrowedFrame!=textureVisibleFrame&&!texturePins.count(i->second.handle)&&
      (oldest==textures.end()||i->second.serial<oldest->second.serial))oldest=i;
 if(oldest==textures.end())return false;
 if(oldest->second.visibleFrame==textureVisibleFrame)++textureStats.sameFrameEvictions;
 textureBytes-=oldest->second.bytes;ReleaseTexture(oldest->second);textures.erase(oldest);++textureStats.evictions;return true;
}
std::uint64_t TextureCost(int handle)
{
 if(gRenderAPI.RenderGetParm)
 {
  const int w=gRenderAPI.RenderGetParm(PARM_TEX_WIDTH,handle),h=gRenderAPI.RenderGetParm(PARM_TEX_HEIGHT,handle);
  if(w>0&&h>0&&w<=32768&&h<=32768)return (std::uint64_t(w)*std::uint64_t(h)*16u+2u)/3u;
 }
 // Conservative fallback when the renderer cannot report upload dimensions.
 return 8u*1024u*1024u;
}
bool DecodeTgaRle(const byte *data,std::size_t size,std::size_t start,int channels,
                  std::size_t rawBytes,std::vector<byte> &decoded)
{
 if(!data||start>size||(channels!=3&&channels!=4)||rawBytes%channels||rawBytes>16u*1024u*1024u)return false;
 decoded.resize(rawBytes);std::size_t input=start,output=0;
 while(output<rawBytes)
 {
  if(input>=size)return false;
  const unsigned packet=data[input++];const std::size_t count=(packet&127u)+1u;
  if(count>(rawBytes-output)/channels)return false;
  if(packet&128u)
  {
   if(size-input<std::size_t(channels))return false;
   for(std::size_t i=0;i<count;++i)std::memcpy(decoded.data()+output+i*channels,data+input,channels);
   input+=channels;output+=count*channels;
  }
  else
  {
   const std::size_t bytes=count*channels;
   if(size-input<bytes)return false;
   std::memcpy(decoded.data()+output,data+input,bytes);input+=bytes;output+=bytes;
  }
 }
 return true;
}
// Resize validated true-colour TGA (raw or RLE). Premultiplied-alpha area
// averaging preserves transparent edges without invisible RGB contamination.
// Unrecognised or invalid data remains on the original load path.
bool ResizeTga(const byte *data,int size,int limit,std::vector<byte> &out)
{
 if(!data||size<18||limit<=0||data[1]!=0||(data[2]!=2&&data[2]!=10)||(data[17]&0xc0))return false;
 const int channels=data[16]/8,alphaBits=data[17]&15;
 if((channels!=3&&channels!=4)||data[16]!=channels*8||
    (channels==3&&alphaBits!=0)||(channels==4&&alphaBits!=8))return false;
 const int w=int(data[12])|(int(data[13])<<8),h=int(data[14])|(int(data[15])<<8);
 const std::size_t start=18u+data[0];
 if(w<=0||h<=0||start>std::size_t(size))return false;
 const std::uint64_t pixelsCount=std::uint64_t(w)*std::uint64_t(h);
 const std::uint64_t rawBytes=pixelsCount*channels;
 // A tiny malicious RLE file must not expand into an unbounded allocation.
 if(rawBytes>16u*1024u*1024u)return false;
 const int longest=std::max(w,h);if(longest<=limit)return false;
 const byte *pixels=nullptr;std::vector<byte> decoded;
 if(data[2]==2)
 {
  if(rawBytes>std::size_t(size)-start)return false;
  pixels=data+start;
 }
 else
 {
  if(!DecodeTgaRle(data,std::size_t(size),start,channels,std::size_t(rawBytes),decoded))return false;
  pixels=decoded.data();
 }
 const int dw=std::max(1,(w*limit+longest/2)/longest),dh=std::max(1,(h*limit+longest/2)/longest);
 out.resize(start+std::size_t(dw)*dh*channels);std::memcpy(out.data(),data,start);
 out[2]=2;
 out[12]=byte(dw&255);out[13]=byte(dw>>8);out[14]=byte(dh&255);out[15]=byte(dh>>8);
 // Integer ratios give every source sample the same weight dw*dh.
 // Cancelling it preserves round-to-nearest, including odd denominators.
 // 65153*N bounds colour + alpha/2 (65025*N + floor(255*N/2)).
 if(w%dw==0&&h%dh==0)
 {
  const int stepX=w/dw,stepY=h/dh;
  const std::uint32_t samples=std::uint32_t(stepX)*std::uint32_t(stepY);
  if(samples<=0xffffffffu/65153u)
  {
   bool opaque=true;
   if(channels==4)for(std::size_t i=3;i<std::size_t(rawBytes);i+=4)
    if(pixels[i]!=255){opaque=false;break;}
   const std::size_t stride=std::size_t(w)*channels;
   for(int y=0;y<dh;++y)
   {
    const byte *row=pixels+std::size_t(y)*stepY*stride;
    byte *target=out.data()+start+std::size_t(y)*dw*channels;
    for(int x=0;x<dw;++x,target+=channels)
    {
     const byte *block=row+std::size_t(x)*stepX*channels;
     std::uint32_t red=0,green=0,blue=0;
     if(opaque)
     {
      for(int sy=0;sy<stepY;++sy)
      {
       const byte *p=block+std::size_t(sy)*stride;
       for(int sx=0;sx<stepX;++sx,p+=channels)
        {red+=p[0];green+=p[1];blue+=p[2];}
      }
      target[0]=byte((red+samples/2)/samples);
      target[1]=byte((green+samples/2)/samples);
      target[2]=byte((blue+samples/2)/samples);
      if(channels==4)target[3]=255;
     }
     else
     {
      std::uint32_t alpha=0;
      for(int sy=0;sy<stepY;++sy)
      {
       const byte *p=block+std::size_t(sy)*stride;
       for(int sx=0;sx<stepX;++sx,p+=4)
       {
        const std::uint32_t a=p[3];alpha+=a;
        red+=a*p[0];green+=a*p[1];blue+=a*p[2];
       }
      }
      target[0]=alpha?byte((red+alpha/2)/alpha):0;
      target[1]=alpha?byte((green+alpha/2)/alpha):0;
      target[2]=alpha?byte((blue+alpha/2)/alpha):0;
      target[3]=byte((alpha+samples/2)/samples);
     }
    }
   }
   return true;
  }
 }
 for(int y=0;y<dh;++y)for(int x=0;x<dw;++x)
 {
  const int xa=x*w,xb=(x+1)*w,ya=y*h,yb=(y+1)*h;
  std::uint64_t total=0,weightedAlpha=0,colour[3]={};
  for(int sy=ya/dh;sy<=(yb-1)/dh;++sy)for(int sx=xa/dw;sx<=(xb-1)/dw;++sx)
  {
   const int wx=std::min(xb,(sx+1)*dw)-std::max(xa,sx*dw);
   const int wy=std::min(yb,(sy+1)*dh)-std::max(ya,sy*dh);
   const std::uint64_t weight=std::uint64_t(wx)*wy;
   const byte *pixel=pixels+(std::size_t(sy)*w+sx)*channels;
   const unsigned alpha=channels==4?pixel[3]:255;
   total+=weight;weightedAlpha+=weight*alpha;
   for(int c=0;c<3;++c)colour[c]+=weight*alpha*pixel[c];
  }
  byte *pixel=out.data()+start+(std::size_t(y)*dw+x)*channels;
  for(int c=0;c<3;++c)pixel[c]=weightedAlpha?byte((colour[c]+weightedAlpha/2)/weightedAlpha):0;
  if(channels==4)pixel[3]=byte((weightedAlpha+total/2)/total);
 }
 return true;
}
// Prepare an uncompressed DDS on the worker. This is lossless and bypasses
// a second full TGA decode on the rendering thread. Match engine TGA Y order.
void PrepareStreamImage(std::vector<byte> &data,int limit,bool &resized,bool &dds)
{
 std::vector<byte> smaller;
 if(limit>0&&ResizeTga(data.data(),int(data.size()),limit,smaller)){data.swap(smaller);resized=true;}
 if(data.size()<18||data[1]||(data[2]!=2&&data[2]!=10)||(data[17]&0xc0))return;
 const int channels=data[16]/8,w=data[12]+(int(data[13])<<8),h=data[14]+(int(data[15])<<8);
 if((channels!=3&&channels!=4)||!w||!h||data[16]!=channels*8)return;
 const std::size_t start=18u+data[0],bytes=std::size_t(w)*h*channels;
 if(start>data.size()||bytes>16u*1024u*1024u)return;
 std::vector<byte> decoded;const byte *pixels=nullptr;
 if(data[2]==10){if(!DecodeTgaRle(data.data(),data.size(),start,channels,bytes,decoded))return;pixels=decoded.data();}
 else {if(bytes>data.size()-start)return;pixels=data.data()+start;}
 // Supply the full mip chain from the decoding worker. A single-level DDS
 // makes GL_LoadTexture rebuild every mip on the game thread at pickup or
 // observer entry. Keep the renderer's exact integer box filter and full size.
 std::size_t payload=0;unsigned mipCount=0;
 for(int mw=w,mh=h;;mw=std::max(1,mw/2),mh=std::max(1,mh/2))
 {payload+=std::size_t(mw)*mh*4;++mipCount;if(mw==1&&mh==1)break;}
 std::vector<byte> prepared(128u+payload,0);
 auto put=[&](int offset,unsigned value){for(int b=0;b<4;++b)prepared[offset+b]=byte(value>>(b*8));};
 put(0,0x20534444);put(4,124);put(8,0x2100f);put(12,h);put(16,w);put(20,unsigned(w)*4);put(28,mipCount);
 put(76,32);put(80,0x41);put(88,32);put(92,0x00ff0000);put(96,0x0000ff00);put(100,0x000000ff);put(104,0xff000000);put(108,0x401008);
 for(int y=0;y<h;++y)
 {
  const byte *row=pixels+std::size_t((data[17]&0x20)?y:h-1-y)*w*channels;
  byte *target=prepared.data()+128+std::size_t(y)*w*4;
  if(channels==4)memcpy(target,row,std::size_t(w)*4);
  else for(int x=0;x<w;++x){memcpy(target+x*4,row+x*3,3);target[x*4+3]=255;}
 }
 std::size_t offset=128;int mw=w,mh=h;
 while(mw>1||mh>1)
 {
  const int nw=std::max(1,mw/2),nh=std::max(1,mh/2);
  const byte *src=prepared.data()+offset;offset+=std::size_t(mw)*mh*4;
  byte *dst=prepared.data()+offset;
  for(int y=0;y<nh;++y)for(int x=0;x<nw;++x)for(int c=0;c<4;++c)
  {
   const int x0=x*2,x1=std::min(x0+1,mw-1),y0=y*2,y1=std::min(y0+1,mh-1);
   dst[(y*nw+x)*4+c]=byte((unsigned(src[(y0*mw+x0)*4+c])+src[(y0*mw+x1)*4+c]+src[(y1*mw+x0)*4+c]+src[(y1*mw+x1)*4+c])>>2);
  }
  mw=nw;mh=nh;
 }
 data.swap(prepared);dds=true;
}
bool PrepareTexture(const std::string &key)
{
 while(textures.size()>=TextureMaximumEntries()||textureBytes>=TextureBudgetBytes())
  if(!EvictTexture()){textureBlockedFrame[key]=textureVisibleFrame;++textureStats.blocked;return false;}
 const auto known=textureKnownCost.find(key);
 if(known!=textureKnownCost.end())
 {
  const std::uint64_t budget=TextureBudgetBytes();
  if(known->second>budget){textureBlockedFrame[key]=textureVisibleFrame;++textureStats.blocked;return false;}
  while(textureBytes>budget-known->second)
   if(!EvictTexture()){textureBlockedFrame[key]=textureVisibleFrame;++textureStats.blocked;return false;}
 }
 return true;
}
int CachedTexture(const std::string &key)
{
 auto found=textures.find(key);
 if(found!=textures.end())
 {
  if(OwnTexture(found->second))
   {found->second.frame=textureFrame;found->second.visibleFrame=textureVisibleFrame;found->second.serial=++textureSerial;++textureStats.hits;return found->second.handle;}
  textureBytes-=found->second.bytes;textures.erase(found);++textureStats.invalid;
 }
 return 0;
}
int PublishTexture(const std::string &key,const std::string &name,int handle)
{
 TextureSlot slot;slot.handle=handle;slot.frame=textureFrame;slot.visibleFrame=textureVisibleFrame;
 slot.serial=++textureSerial;slot.name=name;slot.bytes=TextureCost(handle);
 if(textureKnownCost.size()>=512&&!textureKnownCost.count(key))textureKnownCost.erase(textureKnownCost.begin());
 textureKnownCost[key]=slot.bytes;
 const std::uint64_t budget=TextureBudgetBytes();
 if(slot.bytes>budget)
 {ReleaseTexture(slot);textureBlockedFrame[key]=textureVisibleFrame;++textureStats.blocked;return 0;}
 while(textureBytes>budget-slot.bytes)
  if(!EvictTexture())
  {ReleaseTexture(slot);textureBlockedFrame[key]=textureVisibleFrame;++textureStats.blocked;return 0;}
 textureBytes+=slot.bytes;textureStats.peakBytes=std::max(textureStats.peakBytes,textureBytes);textures[key]=slot;
 return handle;
}
void PumpTextureUpload()
{
 if(!pendingTexture.handle)return;
 BeginTextureDraw();
 if(textureUploadFrame==textureVisibleFrame&&textureUploadsThisFrame>=1)return;
 textureUploadFrame=textureVisibleFrame;textureUploadsThisFrame=1;
 const char *name=gRenderAPI.GL_TextureName?gRenderAPI.GL_TextureName(pendingTexture.handle):nullptr;
 if(!name||pendingTexture.name!=name){pendingTexture=PendingTexture();return;}
 const int handle=gRenderAPI.GL_LoadTexture(pendingTexture.name.c_str(),nullptr,0,0);
 if(handle<0)return;
 if(handle)PublishTexture(pendingTexture.key,pendingTexture.name,handle);
 else CancelTextureUpload();
 pendingTexture=PendingTexture();
}
int Texture(const std::string &path,bool sticker,int maxDimension=0,bool priority=true)
{
 if(path.empty()||!gRenderAPI.GL_LoadTexture)return 0;
 const std::string requested=maxDimension>0?path+"|oc2max="+std::to_string(maxDimension):path;
 std::string key=requested;
 const auto alias=textureVariantAliases.find(requested);
 if(alias!=textureVariantAliases.end())key=alias->second;
 if(const int cached=CachedTexture(key))return cached;
 if(pendingTexture.handle)
 {
  if(priority&&pendingTexture.key==key)pendingTexture.priority=true;
  if(priority&&!pendingTexture.priority&&pendingTexture.key!=key)CancelTextureUpload();
  else return 0;
 }
 OC2ColdTimer cold("texture",path.c_str());
 ++textureStats.misses;
 if(textureBlockedFrame.count(key)){++textureStats.blocked;return 0;}
 // Prefer textures not used this frame; under pressure only evict completed,
 // unpinned draws. This keeps an oversized working set visually correct.
 bool prepared=maxDimension<=0||key!=requested||textureKnownCost.count(key);
 if(prepared&&!PrepareTexture(key))return 0;
 if(textureUploadFrame!=textureVisibleFrame){textureUploadFrame=textureVisibleFrame;textureUploadsThisFrame=0;textureUploadMs=0;}
 std::vector<byte> streamed;bool streamReady=false,streamResized=false,streamDds=false,engineData=false;
 if(textureStreaming&&textureStreaming->value!=0)
 {
  const int limit=key==requested?maxDimension:0;
   oc2_texture_stream::Request(path,limit,priority?3:0);
  if(textureUploadsThisFrame>=1||textureUploadMs>=2)return 0;
  const auto status=oc2_texture_stream::Take(path,limit,streamed,streamResized,streamDds);
  if(status==oc2_texture_stream::Status::Pending)return 0;
  streamReady=status==oc2_texture_stream::Status::Ready;
 }
 ++textureStats.reads;
 int size=streamReady?int(streamed.size()):0;byte *data=streamReady?streamed.data():nullptr;
 if(!streamReady){OC2ColdTimer read("texture_read",path.c_str());data=gEngfuncs.COM_LoadFile(const_cast<char *>(path.c_str()),5,&size);engineData=true;}
 const auto freeData=[&](){if(engineData&&data)gEngfuncs.COM_FreeFile(data);};
 if(!data){textureBlockedFrame[key]=textureVisibleFrame;++textureStats.failures;return 0;}
 if(size<=0||size>(streamDds?23:17)*1024*1024){freeData();textureBlockedFrame[key]=textureVisibleFrame;++textureStats.failures;return 0;}
 std::vector<byte> resized;
 if(maxDimension>0&&key==requested)
 {
  OC2ColdTimer resize("texture_resize",path.c_str());
  const bool tga=path.size()>=4&&!stricmp(path.c_str()+path.size()-4,".tga");
  if(streamReady?streamResized:tga&&ResizeTga(data,size,maxDimension,resized))++textureStats.resized;
  else
  {
   ++textureStats.variantFallbacks;
   if(textureVariantAliases.size()>=512&&!textureVariantAliases.count(requested))textureVariantAliases.erase(textureVariantAliases.begin());
   textureVariantAliases[requested]=path;key=path;prepared=false;
   if(const int cached=CachedTexture(key)){freeData();return cached;}
   if(textureBlockedFrame.count(key)){freeData();++textureStats.blocked;return 0;}
  }
 }
 if(!prepared&&!PrepareTexture(key)){freeData();return 0;}
 // A private renderer name avoids sharing an uncounted handle with menu pics.
 // The # prefix selects the engine's supported load-from-buffer image path.
 std::uint64_t hash=14695981039346656037ull;
 for(unsigned char c:key)hash=(hash^c)*1099511628211ull;
 const std::size_t dot=path.rfind('.');
 const std::string name=std::string(streamDds?"#oc2_stream_":"#oc2_inventory_")+std::to_string(hash)+(streamDds?".dds":dot==std::string::npos?".tga":path.substr(dot));
 ++textureStats.uploads;
 ++textureUploadsThisFrame;const auto uploadStart=std::chrono::steady_clock::now();
 int handle=0;
 {OC2ColdTimer upload("texture_upload",path.c_str());handle=gRenderAPI.GL_LoadTexture(name.c_str(),resized.empty()?data:resized.data(),resized.empty()?size:int(resized.size()),sticker?TF_CLAMP:0);}
 textureUploadMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();freeData();
 if(handle<0){pendingTexture.handle=-handle;pendingTexture.priority=priority;pendingTexture.key=key;pendingTexture.name=name;return 0;}
 if(!handle){textureBlockedFrame[key]=textureVisibleFrame;++textureStats.failures;return 0;}
 return PublishTexture(key,name,handle);
}
void PinTexture(int handle,std::vector<int> &pinned)
{
 if(!handle)return;
 ++texturePins[handle];pinned.push_back(handle);
}
void UnpinTextures(const std::vector<int> &pinned)
{
 for(int handle:pinned)
 {
  const auto found=texturePins.find(handle);
  if(found!=texturePins.end()&&!--found->second)texturePins.erase(found);
 }
}
int PendingCharmTexture()
{
 // A requested image can span several bounded uploads. Never substitute a
 // different picture during that interval, even when a model/skin changes.
 static int handle=0;static const char *name="*oc2_charm_pending";
 const char *known=handle&&gRenderAPI.GL_TextureName?gRenderAPI.GL_TextureName(handle):nullptr;
 if(known&&!strcmp(known,name))return handle;
 const byte tga[22]={0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,40,0,0,0,0};
 handle=gRenderAPI.GL_LoadTexture?gRenderAPI.GL_LoadTexture(name,tga,sizeof(tga),TF_CLAMP|TF_NOMIPMAP):0;
 return handle;
}
int StickerTexture(int id,int maxDimension=0)
{
 // Zero is the transparent default, never the MDL's embedded donor artwork.
 if(!id)return PendingCharmTexture();
 if(id<0)return Texture(kTransparentPath,true,maxDimension);
 const Entry *entry=Resource(id);
 return entry&&entry->kind==Kind::Sticker?Texture(entry->file,true,maxDimension):0;
}
int CharmTexture(int id,int maxDimension=0)
{
 const int texture=StickerTexture(id,maxDimension);
 return texture?texture:PendingCharmTexture();
}
bool MaterialMatches(const std::string &list,const char *name)
{
 const std::size_t length=std::strlen(name);std::size_t begin=0;
 for(;;)
 {
  const std::size_t end=list.find(',',begin);
  const std::size_t count=(end==std::string::npos?list.size():end)-begin;
  if(count==length&&!strnicmp(list.c_str()+begin,name,count))return true;
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
MaterialLayout &GetMaterialLayout(const studiohdr_t *h,const mstudiotexture_t *materials)
{
 auto &layout=materialLayouts[h];
 if(layout.length!=h->length||layout.textureindex!=h->textureindex||layout.numtextures!=h->numtextures||
    layout.skinindex!=h->skinindex||layout.numskinref!=h->numskinref||layout.numskinfamilies!=h->numskinfamilies)
 {
  layout=MaterialLayout();layout.length=h->length;layout.textureindex=h->textureindex;layout.numtextures=h->numtextures;
  layout.skinindex=h->skinindex;layout.numskinref=h->numskinref;layout.numskinfamilies=h->numskinfamilies;
  layout.decals.assign(h->numtextures,-1);
  for(int i=0;i<h->numtextures;++i)if(memchr(materials[i].name,0,sizeof(materials[i].name)))
   for(int slot=0;slot<4;++slot){char name[]="decal_A.bmp";name[6]=char('A'+slot);if(!stricmp(materials[i].name,name)){layout.decals[i]=slot;break;}}
 }
 return layout;
}
const std::vector<unsigned char> &MaterialFamily(const studiohdr_t *h,const mstudiotexture_t *materials,const std::string &names)
{
 auto &layout=GetMaterialLayout(h,materials);auto found=layout.families.find(names);
 if(found!=layout.families.end())return found->second;
 std::vector<unsigned char> matches(h->numtextures,0);
 for(int i=0;i<h->numtextures;++i)if(memchr(materials[i].name,0,sizeof(materials[i].name)))matches[i]=FamilyMaterialMatches(h,materials,i,names);
 return layout.families.emplace(names,std::move(matches)).first->second;
}
const OverlayLayout *GetOverlayLayout(model_t *model,studiohdr_t *header,mstudiotexture_t *materials)
{
 const auto old=overlayLayouts.find(model);
 if(old!=overlayLayouts.end())
 {
  const auto &v=old->second;
  if(v.header==header&&v.model==model->name&&v.length==header->length&&v.textureindex==header->textureindex&&
     v.skinindex==header->skinindex&&v.bodypartindex==header->bodypartindex&&v.numtextures==header->numtextures&&
     v.numskinref==header->numskinref&&v.numbodyparts==header->numbodyparts)
  {++textureStats.layoutHits;return &v;}
  overlayLayouts.erase(old);
 }
 if(header->numskinref<=0||header->numskinfamilies<=0||
    !HeaderRange(header,header->skinindex,header->numskinref,sizeof(short))||
    !HeaderRange(header,header->bodypartindex,header->numbodyparts,sizeof(mstudiobodyparts_t)))return nullptr;
 auto *bytes=reinterpret_cast<byte *>(header);
 const auto *refs=reinterpret_cast<const short *>(bytes+header->skinindex);
 const auto *parts=reinterpret_cast<const mstudiobodyparts_t *>(bytes+header->bodypartindex);
 std::map<int,int> commands;
 for(int p=0;p<header->numbodyparts;++p)
 {
  if(!HeaderRange(header,parts[p].modelindex,parts[p].nummodels,sizeof(mstudiomodel_t)))return nullptr;
  const auto *models=reinterpret_cast<const mstudiomodel_t *>(bytes+parts[p].modelindex);
  for(int m=0;m<parts[p].nummodels;++m)
  {
   if(!HeaderRange(header,models[m].meshindex,models[m].nummesh,sizeof(mstudiomesh_t)))return nullptr;
   const auto *meshes=reinterpret_cast<const mstudiomesh_t *>(bytes+models[m].meshindex);
   for(int j=0;j<models[m].nummesh;++j)
   {
    const auto &mesh=meshes[j];
    if(mesh.skinref<0||mesh.skinref>=header->numskinref||refs[mesh.skinref]<0||
       refs[mesh.skinref]>=header->numtextures||mesh.triindex%int(sizeof(short))||
       !HeaderRange(header,mesh.triindex,1,sizeof(short)))return nullptr;
    const char *name=materials[refs[mesh.skinref]].name;
    if(!memchr(name,0,sizeof(materials[0].name)))return nullptr;
    int slot=-1;
    for(int s=0;s<4;++s){char expected[]="decal_A.bmp";expected[6]=char('A'+s);if(!stricmp(name,expected)){slot=s;break;}}
    if(slot<0)return nullptr;
    const auto previous=commands.find(mesh.triindex);
    if(previous!=commands.end()&&previous->second!=slot)return nullptr;
    commands[mesh.triindex]=slot;
   }
  }
 }
 OverlayLayout v;v.header=header;v.model=model->name;v.length=header->length;v.textureindex=header->textureindex;
 v.skinindex=header->skinindex;v.bodypartindex=header->bodypartindex;v.numtextures=header->numtextures;
 v.numskinref=header->numskinref;v.numbodyparts=header->numbodyparts;
 v.commands.assign(commands.begin(),commands.end());
 if(overlayLayouts.size()>=64)overlayLayouts.clear();
 ++textureStats.layoutBuilds;return &overlayLayouts.emplace(model,std::move(v)).first->second;
}
void Reload() { oc2_texture_stream::Reset();++texturePrefetchEpoch;ReleaseTextures();OC2_ResetWorldStickers();catalogAttempted=false;ReadState(true); }
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
void CacheDump()
{
 gEngfuncs.Con_Printf("oc2inventory_cache: entries=%u bytes=%llu budget=%llu peak=%llu hits=%llu misses=%llu reads=%llu uploads=%llu evictions=%llu same_frame_evictions=%llu invalid=%llu blocked=%llu failures=%llu layout_hits=%llu layout_builds=%llu resized=%llu variant_fallbacks=%llu\n",
  unsigned(textures.size()),static_cast<unsigned long long>(textureBytes),static_cast<unsigned long long>(TextureBudgetBytes()),
  static_cast<unsigned long long>(textureStats.peakBytes),static_cast<unsigned long long>(textureStats.hits),static_cast<unsigned long long>(textureStats.misses),
  static_cast<unsigned long long>(textureStats.reads),static_cast<unsigned long long>(textureStats.uploads),static_cast<unsigned long long>(textureStats.evictions),static_cast<unsigned long long>(textureStats.sameFrameEvictions),
  static_cast<unsigned long long>(textureStats.invalid),static_cast<unsigned long long>(textureStats.blocked),static_cast<unsigned long long>(textureStats.failures),
  static_cast<unsigned long long>(textureStats.layoutHits),static_cast<unsigned long long>(textureStats.layoutBuilds),
  static_cast<unsigned long long>(textureStats.resized),static_cast<unsigned long long>(textureStats.variantFallbacks));
}
void CacheResetStats() { textureStats=TextureStats();textureStats.peakBytes=textureBytes; }
void Dump()
{
 ReadState(true);
 const auto &d=diagnostics;
 gEngfuncs.Con_Printf("oc2inventory: weapon=%d instance=%llu knife=%d equipped_knife=%d skin=%d swaps=%d revision=%u catalog=%u file=%d enabled=%d error=%d texture_cache=%u body_texture=%d pending_body=%d model=%s\n",
  d.weapon,static_cast<unsigned long long>(d.instance),d.knife,loadout.knife,d.skin,d.swaps,loadout.revision,unsigned(catalog.size()),
  int(configExists),int(Active()),int(!readError.empty()),unsigned(textures.size()),d.bodyTexture,int(d.pendingBody),d.model.empty()?"none":d.model.c_str());
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

void OC2_InventoryShutdown(){CancelTextureUpload();oc2_texture_stream::Shutdown();}
void OC2_InventoryInit()
{
 textureStreaming=gEngfuncs.pfnRegisterVariable("cl_oc2_texture_stream","1",0);
 oc2_texture_stream::Start(gEngfuncs.pfnGetGameDirectory(),PrepareStreamImage);
 configPoll=gEngfuncs.pfnRegisterVariable("cl_oc2_config_poll","1",0);
 gEngfuncs.pfnAddCommand("oc2_config_perf_dump",ConfigPerfDump);
 worldGlovesEnabled=gEngfuncs.pfnRegisterVariable("cl_oc2_world_gloves","1",0);
 OC2_WorldAccessoriesInit();
 OC2_InitWorldStickers();
 enabled=gEngfuncs.pfnRegisterVariable("cl_oc2_inventory","1",FCVAR_ARCHIVE);
 textureBudget=gEngfuncs.pfnRegisterVariable("cl_oc2_texture_cache_mb",sizeof(void*)>=8?"1536":"768",0);
 knifeInfo=gEngfuncs.pfnRegisterVariable("oc2_knife","0",FCVAR_USERINFO);
 gEngfuncs.pfnAddCommand("oc2_inventory_dump",Dump);
 gEngfuncs.pfnAddCommand("oc2_inventory_reload",Reload);
 gEngfuncs.pfnAddCommand("oc2_inventory_net_dump",NetDump);
 gEngfuncs.pfnAddCommand("oc2_inventory_cache_dump",CacheDump);
 gEngfuncs.pfnAddCommand("oc2_inventory_cache_reset_stats",CacheResetStats);
 gEngfuncs.pfnAddCommand("oc2_glove_dump",GloveDump);
 gEngfuncs.pfnHookUserMsg(oc2_item::kMessage,ItemMessage);
 gEngfuncs.pfnHookUserMsg(oc2_gloves::kMessage,GloveMessage);
 OC2_InventoryResetSession();
 ReadState(true);
}
void OC2_InventoryVidInit()
{
 CheckSession();
 oc2_texture_stream::Reset();++texturePrefetchEpoch;
 ClearItemQueryIndex();
 worldHandLayouts.clear();
 OC2_WorldAccessoriesReset();
 OC2_ResetWorldStickers();
 ReleaseTextures();textureFrame=-1;catalog.clear();resourceIndices.clear();knifeIndices.clear();knifeModels.clear();catalogAttempted=false;nextRead=lastRead=-1;
 gloveCatalog.clear();gloveCatalogAttempted=false;gloveDraw=GloveDraw();
 diagnostics=Diagnostics();ReadState(true);
}
namespace { void PrefetchAppearanceTextures(); }
void OC2_PrecacheMatchModels()
{
 CheckSession();ReadState();PrefetchAppearanceTextures();OC2_AccessoriesVidInit();
 // Prepare third-person geometry during the loading screen. Otherwise a
 // weapon first seen after a death/drop synchronously loads its body, overlay
 // and attachment models in the middle of the combat frame.
 if(IEngineStudio.Mod_ForName&&IEngineStudio.IsHardware&&IEngineStudio.IsHardware())
 {
  std::set<std::string> paths;
  static const char *guns[]={"p228","scout","xm1014","mac10","aug","elite","fiveseven","ump45","sg550","galil","famas","usp","glock18","awp","mp7","m249","m3","m4a1","tmp","g3sg1","deagle","sg552","ak47","p90"};
  for(const char *gun:guns)for(char prefix:{'v','p','w'})
  {const std::string name=std::string(1,prefix)+"_"+gun+".mdl";paths.insert("models/"+name);if(prefix!='v')paths.insert("models/oc2/stickers/"+name);}
  for(const auto &entry:gloveCatalog)
  {paths.insert(oc2_gloves::WorldModelPath(entry));paths.insert(entry.model);}
  // Observer targets can use another glove family. World-only gloves do not
  // populate the first-person model cache used when switching to their eyes.
  paths.insert("models/arms/glove_hardknuckle/v_glove_hardknuckle_black.mdl");
  paths.insert("models/arms/glove_fullfinger/v_glove_fullfinger.mdl");
  paths.insert("models/arms/sas/v_sleeve_sas.mdl");
  paths.insert("models/hostage/v_hostage_arm.mdl");
  for(const auto &entry:catalog)if(entry.kind==Kind::Knife&&entry.model.size()>8)
   for(char prefix:{'v','p','w'}){std::string path=entry.model;path[7]=prefix;paths.insert(path);}
  paths.insert("models/attachments/oc2_stattrak.mdl");paths.insert("models/attachments/oc2_sticker_charm.mdl");paths.insert("models/attachments/oc2_sticker_charm_m249.mdl");
  for(const auto &path:paths)if(!path.empty())IEngineStudio.Mod_ForName(path.c_str(),false);
  for(const char *gun:guns)for(char prefix:{'v','p','w'})
  {
   const std::string path=std::string("models/")+prefix+"_"+gun+".mdl";
   auto *model=IEngineStudio.Mod_ForName(path.c_str(),false);
   if(prefix=='v')OC2_PrecacheAccessoryMounts(model);else OC2_PrecacheWorldAccessoryMounts(model);
  }
 }
}
int OC2_InventoryPreviewTexture(const char *path)
{
 if(!path||!*path)return 0;
 BeginTextureDraw();const int handle=Texture(path,false);
 const auto found=textures.find(path);
 if(handle&&found!=textures.end())found->second.borrowedFrame=textureVisibleFrame;
 return handle;
}
void OC2_InventoryResetSession()
{
 CancelTextureUpload();
 oc2_texture_stream::Reset();++texturePrefetchEpoch;
 worldHandLayouts.clear();
 OC2_WorldAccessoriesReset();
 OC2_ResetWorldStickers();
 overlayLayouts.clear();
 materialLayouts.clear();
 accessorySent.clear();
 items.clear();ClearItemQueryIndex();draws.clear();networkReady=false;resyncSent=false;itemSnapshotStarted=false;lastSent.clear();queuedConfig.clear();upload.clear();uploadIndex=0;
 playerGloves.fill(0);gloveNetworkReady=false;lastGloveSent.clear();gloveDraw=GloveDraw();gloveShaders.clear();
 corpseGloves.clear();worldGloveDraws.clear();
}
namespace
{
void PrefetchAppearanceTextures()
{
 if(!textureStreaming||textureStreaming->value==0||!Active())return;
 struct Wanted {std::string path,key;int limit,rank;bool sticker;};
 static std::vector<Wanted> wanted;static double last=-1;static unsigned epoch=0;
 static int lastView=-1,lastActive=-1;static bool complete=false;static std::uint64_t evictions=0,invalid=0,budget=0;
 const double now=gEngfuncs.GetClientTime();const int view=ViewPlayer();
 const auto *active=PlayerItem(view);const int activeSerial=active?active->serial:0;
 if(epoch!=texturePrefetchEpoch||now<last||lastView!=view||lastActive!=activeSerial)
 {
  epoch=texturePrefetchEpoch;lastView=view;lastActive=activeSerial;complete=false;wanted.clear();
  std::map<std::pair<std::string,int>,std::pair<bool,int>> unique;
  const auto append=[&](int id,int limit,bool sticker,int rank){const auto *entry=Resource(id);if(entry&&!entry->file.empty()){
   auto result=unique.emplace(std::make_pair(entry->file,limit),std::make_pair(sticker,rank));
   result.first->second.second=std::min(result.first->second.second,rank);}};
  // Saved choices are known before the server's item snapshot and before a
  // purchase. Decode them ahead of time instead of first requesting on draw.
  const auto *local=gEngfuncs.GetLocalPlayer();const int localIndex=local?local->index:0;
  for(const auto &weapon:loadout.weapons)
  {append(weapon.skin,0,false,2);for(int id:weapon.stickers)if(id>0)append(id,0,true,2);if(weapon.charm>0)append(weapon.charm,0,true,2);}
  if(const auto *knife=FindKnife(loadout,loadout.knife))append(knife->skin,0,false,2);
  for(const auto &row:items)
  {
   const auto &item=row.second;if(!item.owner)continue;
   const int rank=item.serial==activeSerial?0:item.owner==view?1:item.owner==localIndex?2:4;
   append(item.skin,0,false,rank);for(int id:item.stickers)if(id>0)append(id,0,true,rank);if(item.charm>0)append(item.charm,0,true,rank);
   append(item.skin,512,false,3);for(int id:item.stickers)if(id>0)append(id,256,true,3);if(item.charm>0)append(item.charm,256,true,3);
  }
  for(int player=1;player<int(playerGloves.size());++player)if(const auto *glove=oc2_gloves::FindEntry(gloveCatalog,playerGloves[player]))
  {const int rank=player==view?0:player==localIndex?2:3;
   for(const auto &path:{glove->leftTexture,glove->rightTexture})if(!path.empty())
   {auto entry=unique.emplace(std::make_pair(path,0),std::make_pair(false,rank));entry.first->second.second=std::min(entry.first->second.second,rank);}}
  for(const auto &entry:unique)wanted.push_back({entry.first.first,entry.first.second?entry.first.first+"|oc2max="+std::to_string(entry.first.second):entry.first.first,entry.first.second,entry.second.second,entry.second.first});
  std::stable_sort(wanted.begin(),wanted.end(),[](const Wanted&a,const Wanted&b){return a.rank<b.rank;});
  if(wanted.size()>400)wanted.resize(400);
 }
 last=now;
 if(evictions!=textureStats.evictions||invalid!=textureStats.invalid||budget!=TextureBudgetBytes())complete=false;
 evictions=textureStats.evictions;invalid=textureStats.invalid;budget=TextureBudgetBytes();
 if(complete)return;
 BeginTextureDraw();
 const auto spareBudget=[](){const auto budget=TextureBudgetBytes();return textures.size()<TextureMaximumEntries()-32&&budget>64u*1024u*1024u&&textureBytes<budget-64u*1024u*1024u;};
 for(const auto &entry:wanted)
 {
  if(entry.rank>=4&&!spareBudget())continue;
  const auto alias=textureVariantAliases.find(entry.key);const std::string &key=alias==textureVariantAliases.end()?entry.key:alias->second;
  if(textures.count(key)||(pendingTexture.handle&&pendingTexture.key==key))continue;
  oc2_texture_stream::Request(entry.path,key==entry.key?entry.limit:0,entry.rank<=2?3-entry.rank:0);
 }
 bool outstanding=false;
 for(const auto &entry:wanted)
 {
  if(entry.rank>=4&&!spareBudget())continue;
  const auto alias=textureVariantAliases.find(entry.key);
  if(textures.count(alias==textureVariantAliases.end()?entry.key:alias->second))continue;
  outstanding=true;
  if(textureUploadFrame==textureVisibleFrame&&textureUploadsThisFrame>=1)break;
  Texture(entry.path,entry.sticker,entry.limit,entry.rank<=2);
 }
 complete=!outstanding;
}
}
void OC2_InventoryFrame()
{
 CheckSession();ReadState();
 PumpTextureUpload();
 PrefetchAppearanceTextures();
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
 EnsureItemQueryIndex();
 int count=0;
 for(int serial:itemQueryIndex.owned[player])
 {
  const auto found=items.find(serial);if(found==items.end())continue;
  out[count++]=found->second;
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
 ReadState();
 const auto *instance=networkReady?ViewItem(weapon):nullptr;
 if(instance&&instance->accessoryReady&&!OC2_InventoryOwnAccessories(weapon))return instance->charm;
 const WeaponState *state=Find(loadout,weapon);
 if(!OC2_InventoryOwnAccessories(weapon))return -1;
 if(!Active()||!state)return 0;
 const Entry *entry=Resource(state->charm);
 return entry&&IsTransparent(*entry)?-1:state->charm;
}
bool OC2_InventoryInstanceAccessories(int weapon,oc2_accessories::WeaponConfig &config,unsigned &count)
{
 if(!networkReady)return false;
 const auto *item=ViewItem(weapon);
 const auto *local=gEngfuncs.GetLocalPlayer();
 if(!item||!local||((item->flags&oc2_item::OriginOwner)&&!(item->flags&oc2_item::BotOrigin)&&ViewPlayer()==local->index)||!item->accessoryReady)return false;
 config=item->accessory;count=item->count;return true;
}
bool OC2_InventoryWorldAccessories(model_s *model,int player,int serial,int entity,oc2_accessories::WeaponConfig &config,unsigned &count,int &pattern,int &instance)
{
 if(!networkReady||!Active()||!model)return false;
 const int weapon=WeaponId(model->name);if(!weapon)return false;
 const auto found=items.find(serial);
 const auto *item=serial?(found!=items.end()&&found->second.owner==player?&found->second:nullptr):player?PlayerItem(player,weapon):WorldItem(entity,weapon);
 if(!item||!item->accessoryReady||item->weapon!=weapon)return false;
 config=item->accessory;count=item->count;instance=item->serial;
 if(item->charm)config.charm=item->charm>0;
 // Dropped models have player zero; holstered models pass an explicit serial.
 // Keep world charms only on the active held model, without altering its item.
 config.charm=config.charm&&player>0&&serial==0;
 pattern=0;
 if(config.charm)
 {
  pattern=item->charm>=0?CharmTexture(item->charm,256):0;
  if(!item->charm&&config.sticker>0)pattern=OC2_LegacyCharmTexture(config.sticker);
 }
 return config.stattrak||config.charm;
}
void OC2_InventoryPublishAccessories(int weapon,const oc2_accessories::WeaponConfig &config,unsigned count)
{
 if(!OC2_InventoryOwnAccessories(weapon))return;
 const auto *local=gEngfuncs.GetLocalPlayer();const auto *item=PlayerItem(local->index,weapon,false);
 if(!item)return;
 const auto *choice=Find(loadout,weapon);const int charm=choice?choice->charm:0;
 std::ostringstream s;s<<"oc2_item_acc "<<item->serial<<' '<<weapon<<' '<<int(config.stattrak)<<' '<<config.statU<<' '<<config.statV<<' '<<int(config.charm)<<' '<<config.charmU<<' '<<config.charmV<<' '<<config.sticker<<' '<<config.amplitude<<' '<<config.length<<' '<<config.stiffness<<' '<<config.damping<<' '<<count<<' '<<charm;
 auto &sent=accessorySent;
 const std::string command=s.str();if(sent[item->serial]==command)return;
 if(sent.size()>8192)sent.clear();sent[item->serial]=command;gEngfuncs.pfnServerCmd((command+"\n").c_str());
}
bool OC2_InventoryOwnAccessories(int weapon)
{
 const auto *local=gEngfuncs.GetLocalPlayer();
 if(!local||ViewPlayer()!=local->index)return false;
 if(!networkReady)return false;
 const auto *item=PlayerItem(local->index,weapon,false);
 return item&&(item->flags&oc2_item::OriginOwner)&&!(item->flags&oc2_item::BotOrigin);
}
bool OC2_InventoryCharmOverride(int weapon,int &texture,bool &hide)
{
 texture=0;hide=false;const int selection=OC2_InventoryCharmSelection(weapon);
 if(!selection)return false;
 const Entry *entry=Resource(selection);
 hide=selection<0||(entry&&IsTransparent(*entry));
 if(!hide)texture=CharmTexture(selection);
 return true; // The authoritative selection exists even while it is loading.
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
 BeginTextureDraw();
 ReadState();Diagnostics current;current.model=model->name;
 if(!Active()||!IEngineStudio.IsHardware())return;
 auto *header=renderer.m_pStudioHeader;
 if(!HeaderRange(header,header?header->textureindex:0,header?header->numtextures:0,sizeof(mstudiotexture_t)))return;
 auto *materials=reinterpret_cast<mstudiotexture_t *>(reinterpret_cast<byte *>(header)+header->textureindex);
 const int weapon=WeaponId(model->name);
 const bool knifeModel=KnifeModel(model->name);
 if(!weapon&&!knifeModel)return;
 const auto found=items.find(instance);
 const auto *item=instance?(player>=1&&player<=32&&found!=items.end()&&found->second.owner==player?&found->second:nullptr):context=='v'?ViewItem(knifeModel?29:weapon):context=='p'?PlayerItem(player):WorldItem(renderer.m_pCurrentEntity->index,knifeModel?29:weapon);
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
 const bool worldTexture=context=='p'||context=='w'||context=='c';
 int selectedTextures[4]={};
 if(stickersOnly)
 {
  if(!state)return;
  for(int slot=0;slot<4;++slot)
   if(state->stickers[slot]>0&&!(selectedTextures[slot]=StickerTexture(state->stickers[slot],worldTexture?256:0)))return;
  const auto *layout=GetOverlayLayout(model,header,materials);if(!layout)return;
  auto *bytes=reinterpret_cast<byte *>(header);
  for(const auto &command:layout->commands)if(state->stickers[command.second]<=0)
  {
   auto *first=reinterpret_cast<short *>(bytes+command.first);
   hidden.push_back(SavedShort(first,*first));*first=0;
  }
 }
 if(skin&&(weapon?(skin->kind!=Kind::GunSkin||skin->weapon!=weapon):(skin->kind!=Kind::KnifeSkin||skin->definition!=current.knife)))skin=nullptr;
 const int bodyTexture=skin&&!stickersOnly?Texture(skin->file,false,worldTexture?512:0):0;
 current.bodyTexture=bodyTexture;current.pendingBody=skin&&!stickersOnly&&!bodyTexture;
 const auto *bodyBindings=bodyTexture?&MaterialFamily(header,materials,skin->material):nullptr;
 const auto &decalSlots=GetMaterialLayout(header,materials).decals;
 for(int i=0;i<header->numtextures;++i)
 {
  auto &material=materials[i];int replacement=0;
   if(bodyBindings&&(*bodyBindings)[i])replacement=bodyTexture;
   const int slot=decalSlots[i];
   if(state&&slot>=0)
  {
    if(!stickersOnly||state->stickers[slot]>0)
     {replacement=stickersOnly?selectedTextures[slot]:StickerTexture(state->stickers[slot],worldTexture?256:0);current.slotTextures[slot]=replacement;}
  }
  if(!replacement)continue;
  saved.push_back(Saved(&material.index,material.index));PinTexture(replacement,pinned);material.index=replacement;++current.swaps;
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
 UnpinTextures(pinned);
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
 const auto &leftBindings=MaterialFamily(header,materials,entry->leftMaterial);
 const auto &rightBindings=MaterialFamily(header,materials,entry->rightMaterial);
 const bool left=std::find(leftBindings.begin(),leftBindings.end(),1)!=leftBindings.end();
 const bool right=std::find(rightBindings.begin(),rightBindings.end(),1)!=rightBindings.end();
 if(!left||!right)return;
 BeginTextureDraw();
 const int leftTexture=Texture(entry->leftTexture,false),rightTexture=Texture(entry->rightTexture,false);
 if(!leftTexture||!rightTexture)return;
 for(int index=0;index<header->numtextures;++index)
 {
  auto &material=materials[index];
  int replacement=0;
  if(leftBindings[index])replacement=leftTexture;
  else if(rightBindings[index])replacement=rightTexture;
  if(!replacement)continue;
  // Keep MDL dimensions, normalized UVs, flags and bare skin families intact.
  saved.push_back(Saved(&material.index,material.index));
  PinTexture(replacement,pinned);
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
 UnpinTextures(pinned);
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
 BeginTextureDraw();
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
 if(worldGlovesEnabled&&worldGlovesEnabled->value==0)return;
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
  const auto layoutKey=std::make_pair(static_cast<const studiohdr_t *>(bodyHeader),static_cast<const mstudiomodel_t *>(&sub));
  auto layout=worldHandLayouts.find(layoutKey);
  if(layout==worldHandLayouts.end())
  {
   const auto *bindings=reinterpret_cast<const byte *>(bodyHeader)+sub.vertinfoindex;
   bool handsOnly=true;std::array<int,MAXSTUDIOBONES> classification{};
   for(int v=0;v<sub.numverts&&handsOnly;++v)
   {
    const unsigned boneIndex=bindings[v];
    if(boneIndex>=unsigned(bodyHeader->numbones)||boneIndex>=classification.size()){handsOnly=false;break;}
    int &hand=classification[boneIndex];
    if(!hand)
    {
     const auto &bone=bones[boneIndex];
     if(!memchr(bone.name,0,sizeof(bone.name))){handsOnly=false;break;}
     const std::string boneName=oc2_inventory::Lower(bone.name);
     hand=(boneName.find("hand")!=std::string::npos||boneName.find("wrist")!=std::string::npos||boneName.find("forearm")!=std::string::npos)?1:-1;
    }
    handsOnly=hand>0;
   }
   layout=worldHandLayouts.emplace(layoutKey,handsOnly).first;
  }
  if(layout->second)hidden.push_back(Saved(&sub.nummesh,sub.nummesh));
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
