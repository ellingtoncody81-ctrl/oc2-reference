#include "precompiled.h"
#include "oc2_item_server.h"
#include "../../../../common/oc2_skin_inventory.h"
#include "../../../../common/oc2_glove_inventory.h"
#include <map>

namespace
{
using oc2_item::Appearance;
using namespace oc2_inventory;
Catalog catalog;
std::array<std::vector<int>,24> gunSkins;
std::map<int,std::vector<int>> knifeSkins;
std::vector<int> knives, stickers;
std::map<int,EHANDLE> issued;
std::map<int,Appearance> published;
int messageId=0, nextSerial=1;
int gloveMessageId=0;
oc2_gloves::Catalog gloveCatalog;
struct PlayerLoadout
{
 int userid=0;
 Config config=DefaultConfig();
 std::string pending;
 float deadline=0;
 bool snapshot=false;
 int glove=0;
 unsigned gloveRevision=0;
};
PlayerLoadout players[33];

PlayerLoadout &PlayerState(CBasePlayer *player)
{
 auto &state=players[player->entindex()];
 const int userid=GETPLAYERUSERID(player->edict());
 if(state.userid!=userid){state=PlayerLoadout();state.userid=userid;state.snapshot=true;}
 return state;
}
unsigned BotSeed(CBasePlayer *player)
{
 unsigned seed=2166136261u;
 for(const unsigned char *p=reinterpret_cast<const unsigned char *>(STRING(player->pev->netname));*p;++p)
  seed=(seed^*p)*16777619u;
 return seed+unsigned(player->entindex())*7919u;
}
int Pick(const std::vector<int> &choices,unsigned seed)
{ return choices.empty()?0:choices[seed%choices.size()]; }

void ApplyChoice(CBasePlayerItem *item,CBasePlayer *player)
{
 auto &a=item->m_oc2Appearance;
 a.definition=0;a.skin=0;
 for(int &s:a.stickers)s=0;
 if(player->IsBot())
 {
  const unsigned seed=BotSeed(player);
  if(item->m_iId==WEAPON_KNIFE)
  {
   a.definition=Pick(knives,seed);
   const auto found=knifeSkins.find(a.definition);
   if(found!=knifeSkins.end())a.skin=Pick(found->second,seed/7u);
  }
  else
  {
   const int index=oc2_accessories::Index(item->m_iId);
   if(index<0)return;
   a.definition=Definition(item->m_iId);
   a.skin=Pick(gunSkins[index],seed+unsigned(item->m_iId)*37u);
   for(int slot=0;slot<4;++slot)a.stickers[slot]=Pick(stickers,seed+unsigned(item->m_iId)*71u+unsigned(slot)*137u);
  }
  return;
 }
 const auto &config=PlayerState(player).config;
 if(item->m_iId==WEAPON_KNIFE)
 {
  a.definition=config.knife;
  if(const auto *choice=FindKnife(config,config.knife))a.skin=choice->skin;
 }
 else if(const auto *choice=Find(config,item->m_iId))
 {
  a.definition=Definition(item->m_iId);a.skin=choice->skin;
  for(int slot=0;slot<4;++slot)a.stickers[slot]=choice->stickers[slot];
 }
}
Appearance Current(CBasePlayerItem *item)
{
 Appearance a=item->m_oc2Appearance;
 a.entity=item->entindex();a.owner=0;a.box=0;
 a.flags&=oc2_item::BotOrigin;
 if(item->m_pPlayer)
 {
  a.owner=item->m_pPlayer->entindex();
  if(item->m_pPlayer->m_pActiveItem==item&&item->m_pPlayer->IsAlive())a.flags|=oc2_item::Active;
  if(GETPLAYERUSERID(item->m_pPlayer->edict())==a.originUserid)a.flags|=oc2_item::OriginOwner;
 }
 else if(!FNullEnt(item->pev->owner)&&FClassnameIs(&item->pev->owner->v,"weaponbox"))
  a.box=ENTINDEX(item->pev->owner);
 return a;
}
void Send(int operation,const Appearance *a,CBasePlayer *target=nullptr)
{
 if(!messageId)return;
 if(target)MESSAGE_BEGIN(MSG_ONE,messageId,nullptr,target->pev);
 else MESSAGE_BEGIN(MSG_ALL,messageId);
 WRITE_BYTE(oc2_item::kVersion);WRITE_BYTE(operation);
 if(operation!=oc2_item::Reset)WRITE_LONG(a->serial);
 if(operation==oc2_item::Upsert)
 {
  WRITE_SHORT(a->entity);WRITE_BYTE(a->owner);WRITE_SHORT(a->box);WRITE_BYTE(a->weapon);
  WRITE_SHORT(a->definition);WRITE_LONG(a->skin);
  for(int s:a->stickers)WRITE_LONG(s);
  WRITE_LONG(a->originUserid);WRITE_BYTE(a->flags);
 }
 MESSAGE_END();
}
void Publish()
{
 for(auto it=issued.begin();it!=issued.end();)
 {
  CBaseEntity *entity=it->second;
  auto *item=entity?static_cast<CBasePlayerItem *>(entity):nullptr;
  if(!item||(item->pev->flags&FL_KILLME)||item->m_oc2Appearance.serial!=it->first)
  {
   const auto old=published.find(it->first);
   if(old!=published.end()){Send(oc2_item::Remove,&old->second);published.erase(old);}
   it=issued.erase(it);continue;
  }
  const auto a=Current(item);
  const auto old=published.find(a.serial);
  if(old==published.end()||!oc2_item::Equal(a,old->second))
  {Send(oc2_item::Upsert,&a);published[a.serial]=a;}
  ++it;
 }
}
void SendGlove(int operation,int index=0,int resource=0,CBasePlayer *target=nullptr)
{
 if(!gloveMessageId)return;
 if(target)MESSAGE_BEGIN(MSG_ONE,gloveMessageId,nullptr,target->pev);
 else MESSAGE_BEGIN(MSG_ALL,gloveMessageId);
 WRITE_BYTE(oc2_gloves::kVersion);WRITE_BYTE(operation);
 if(operation==oc2_gloves::Upsert){WRITE_BYTE(index);WRITE_LONG(resource);}
 MESSAGE_END();
}
void GloveSnapshot(CBasePlayer *target)
{
 SendGlove(oc2_gloves::Reset,0,0,target);
 for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)
 {
  auto *peer=UTIL_PlayerByIndex(i);
  if(peer&&!peer->IsDormant())SendGlove(oc2_gloves::Upsert,i,peer->IsBot()?0:PlayerState(peer).glove,target);
 }
}
void Snapshot(CBasePlayer *player)
{
 Publish();Send(oc2_item::Reset,nullptr,player);
 for(const auto &entry:published)Send(oc2_item::Upsert,&entry.second,player);
 GloveSnapshot(player);
 PlayerState(player).snapshot=false;
}
void Dump()
{
 Publish();
 ALERT(at_console,"oc2items: count=%u catalog=%u\n",unsigned(published.size()),unsigned(catalog.size()));
 for(const auto &entry:published)
 {
  const auto &a=entry.second;
  ALERT(at_console,"oc2item: serial=%d entity=%d owner=%d box=%d weapon=%d definition=%d skin=%d stickers=%d/%d/%d/%d origin=%d flags=%d\n",
   a.serial,a.entity,a.owner,a.box,a.weapon,a.definition,a.skin,a.stickers[0],a.stickers[1],a.stickers[2],a.stickers[3],a.originUserid,a.flags);
 }
}
bool ArgumentNumber(int index,int maximum,int &value)
{
 std::uint64_t number=0;
 if(index>=CMD_ARGC()||!Number(CMD_ARGV(index),maximum,number))return false;
 value=int(number);return true;
}
void TestCommand(CBasePlayer *caller)
{
 // Explicit local cheat harness. Acquisition, dropping and touching still use
 // the game's normal GiveNamedItem/PackWeapon/physics Touch paths.
 if(CVAR_GET_FLOAT("sv_cheats")==0||IS_DEDICATED_SERVER()||caller->entindex()!=1||CMD_ARGC()!=4)return;
 int playerIndex=0,value=0;
 if(!ArgumentNumber(2,32,playerIndex)||playerIndex<1||!ArgumentNumber(3,32767,value))return;
 auto *target=UTIL_PlayerByIndex(playerIndex);
 if(!target)return;
 const char *action=CMD_ARGV(1);
 if(FStrEq(action,"respawn"))
 {
  if(!target->IsAlive())target->RoundRespawn();
  return;
 }
 if(!target->IsAlive())return;
 if(FStrEq(action,"aim"))
 {
  if(value>359)return;
  target->pev->v_angle=target->pev->angles=Vector(12,float(value),0);
  target->pev->fixangle=1;
  return;
 }
 if(FStrEq(action,"knife"))
 {
  if(value==59||(value&&!FindKnifeEntry(catalog,value)))return;
  auto *knife=static_cast<CBasePlayerItem *>(target->GetItemById(WEAPON_KNIFE));
  if(!knife)return;
  knife->m_oc2Appearance.definition=value;
  const auto skins=knifeSkins.find(value);
  knife->m_oc2Appearance.skin=skins==knifeSkins.end()?0:Pick(skins->second,BotSeed(target));
  target->SwitchWeapon(knife);Publish();return;
 }
 if(FStrEq(action,"stab")||FStrEq(action,"blast"))
 {
  auto *victim=value>0&&value<=gpGlobals->maxClients?UTIL_PlayerByIndex(value):nullptr;
  if(!target->IsBot()||!victim||!victim->IsAlive()||victim==target||victim->m_iTeam==target->m_iTeam)return;
  auto *knife=static_cast<CBasePlayerWeapon *>(target->GetItemById(WEAPON_KNIFE));
  if(!knife)return;
  if(FStrEq(action,"blast"))
  {
   target->GiveNamedItem("weapon_hegrenade");
   auto *he=static_cast<CHEGrenade *>(target->GetItemById(WEAPON_HEGRENADE));
   if(!he)return;
   victim->pev->health=1;victim->pev->flags&=~FL_GODMODE;
   Vector start=victim->pev->origin+Vector(0,48,8),velocity=g_vecZero;
   CGrenade::ShootTimed2(target->pev,start,velocity,0.4f,target->m_iTeam,he->m_usCreateExplosion);
   target->SwitchWeapon(knife);Publish();return;
  }
  if(target->m_pActiveItem!=knife)return;
  UTIL_SetOrigin(target->pev,victim->pev->origin-Vector(40,0,0));
  target->pev->v_angle=target->pev->angles=g_vecZero;
  target->pev->velocity=victim->pev->velocity=g_vecZero;
  victim->pev->health=1;victim->pev->flags&=~FL_GODMODE;
  // Execute the real knife trace, TraceAttack/TakeDamage and DeathMsg path.
  knife->SecondaryAttack();Publish();return;
 }
 if(FStrEq(action,"pickup"))
 {
  if(value<=gpGlobals->maxClients||value>=gpGlobals->maxEntities)return;
  edict_t *box=INDEXENT(value);
  if(FNullEnt(box)||!FClassnameIs(&box->v,"weaponbox"))return;
  UTIL_SetOrigin(target->pev,box->v.origin+Vector(0,0,-target->pev->mins.z+1));
  target->pev->velocity=g_vecZero;
 }
 else
 {
  if(value!=WEAPON_KNIFE&&oc2_accessories::Index(value)<0)return;
  const auto *info=GetWeaponInfo(value);
  if(!info||!info->entityName)return;
  if(FStrEq(action,"give"))
  {
   const auto *slot=GetWeaponSlot(static_cast<WeaponIdType>(value));
   if(slot&&(slot->slot==PRIMARY_WEAPON_SLOT||slot->slot==PISTOL_SLOT))
   {
    auto *previous=target->m_rgpPlayerItems[slot->slot];
    if(previous)
     if(auto *box=target->DropPlayerItem(STRING(previous->pev->classname)))
      UTIL_SetOrigin(box->pev,Vector(0,0,-4096));
   }
   target->GiveNamedItem(info->entityName);
  }
  else if(FStrEq(action,"drop"))target->DropPlayerItem(info->entityName);
  else if(FStrEq(action,"park"))
  {
   if(auto *box=target->DropPlayerItem(info->entityName))UTIL_SetOrigin(box->pev,Vector(0,0,-4096));
  }
  else if(FStrEq(action,"select"))
  {
   auto *item=static_cast<CBasePlayerItem *>(target->GetItemById(static_cast<WeaponIdType>(value)));
   if(item)target->SwitchWeapon(item);
  }
 }
 Publish();Dump();
}
}

void OC2_ItemsRegisterMessage()
{ messageId=REG_USER_MSG(oc2_item::kMessage,-1);gloveMessageId=REG_USER_MSG(oc2_gloves::kMessage,-1); }

void OC2_ItemsPrecache()
{
 issued.clear();published.clear();nextSerial=1;catalog.clear();knives.clear();stickers.clear();knifeSkins.clear();
 for(auto &v:gunSkins)v.clear();
 for(auto &v:players)v=PlayerLoadout();
 gloveCatalog.clear();
 int gloveSize=0;char *gloveData=reinterpret_cast<char *>(LOAD_FILE_FOR_ME(oc2_gloves::kCatalogPath,&gloveSize));
 if(gloveData)
 {
  std::string gloveError;
  if(gloveSize>0&&gloveSize<=int(oc2_gloves::kMaximumCatalogBytes))
   oc2_gloves::ParseCatalog(std::string(gloveData,gloveSize),gloveCatalog,gloveError);
  FREE_FILE(gloveData);
 }
 int size=0;char *data=reinterpret_cast<char *>(LOAD_FILE_FOR_ME(kCatalogPath,&size));
 std::string error;
 if(data)
 {
  if(size>0&&size<=int(kMaximumCatalogBytes))ParseCatalog(std::string(data,size),catalog,error);
  FREE_FILE(data);
 }
 if(catalog.empty()){ALERT(at_console,"oc2items: catalog unavailable; default appearances\n");return;}
 knives.push_back(0); // The existing T knife is the sole default; never definition 59.
 for(const auto &entry:catalog)
 {
  if(entry.kind==Kind::GunSkin)gunSkins[oc2_accessories::Index(entry.weapon)].push_back(entry.id);
  else if(entry.kind==Kind::Knife&&entry.definition!=59)knives.push_back(entry.definition);
  else if(entry.kind==Kind::KnifeSkin&&entry.definition!=59)knifeSkins[entry.definition].push_back(entry.id);
  else if(entry.kind==Kind::Sticker&&!IsTransparent(entry))stickers.push_back(entry.id);
 }
 ALERT(at_console,"oc2items: catalog=%u knives=%u stickers=%u\n",unsigned(catalog.size()),unsigned(knives.size()),unsigned(stickers.size()));
}
void OC2_ItemsClientConnected(CBasePlayer *player)
{
 if(!player||player->entindex()<1||player->entindex()>32)return;
 players[player->entindex()]=PlayerLoadout();
 auto &state=players[player->entindex()];
 state.userid=GETPLAYERUSERID(player->edict());state.snapshot=!player->IsBot();
 SendGlove(oc2_gloves::Upsert,player->entindex(),0);
}
void OC2_ItemsClientDisconnected(CBasePlayer *player)
{
 if(!player||player->entindex()<1||player->entindex()>32)return;
 players[player->entindex()]=PlayerLoadout();
 SendGlove(oc2_gloves::Upsert,player->entindex(),0);
}
void OC2_ItemsAcquire(CBasePlayerItem *item,CBasePlayer *player)
{
 if(!item||!player||(item->m_iId!=WEAPON_KNIFE&&oc2_accessories::Index(item->m_iId)<0))return;
 auto &a=item->m_oc2Appearance;
 if(!a.serial)
 {
  a.serial=nextSerial++;a.weapon=item->m_iId;
  a.originUserid=GETPLAYERUSERID(player->edict());
  a.flags=player->IsBot()?oc2_item::BotOrigin:0;
  ApplyChoice(item,player);
 }
 else if(a.originUserid!=GETPLAYERUSERID(player->edict()))item->m_bOc2Transferred=true;
 issued[a.serial]=static_cast<CBaseEntity *>(item);
}
void OC2_ItemsFrame()
{
 Publish();
 for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)
 {
  auto *player=UTIL_PlayerByIndex(i);
  if(!player||player->IsBot()||player->IsDormant())continue;
  auto &state=PlayerState(player);
  if(state.snapshot&&!player->m_fInitHUD)Snapshot(player);
  if(!state.pending.empty()&&gpGlobals->time>state.deadline)state.pending.clear();
 }
}
bool OC2_ItemsCommand(CBasePlayer *player,const char *command)
{
 if(FStrEq(command,"oc2_glove"))
 {
  if(!player||player->IsBot()||player->entindex()<1||player->entindex()>32)return true;
  std::uint64_t revision=0,resource=0;
  if(CMD_ARGC()!=3||!Number(CMD_ARGV(1),std::numeric_limits<unsigned>::max(),revision)||!revision||
     !Number(CMD_ARGV(2),std::numeric_limits<int>::max(),resource)||
     (resource&&!oc2_gloves::FindEntry(gloveCatalog,int(resource))))
   {ALERT(at_console,"oc2gloveserver: rejected player=%d\n",player->entindex());return true;}
  auto &state=PlayerState(player);state.glove=int(resource);state.gloveRevision=unsigned(revision);
  SendGlove(oc2_gloves::Upsert,player->entindex(),state.glove);
  ALERT(at_console,"oc2gloveserver: accepted player=%d resource=%d revision=%u\n",player->entindex(),state.glove,state.gloveRevision);
  return true;
 }
 if(FStrEq(command,"oc2_items_dump")){Dump();return true;}
 if(FStrEq(command,"oc2_item_test")){TestCommand(player);return true;}
 if(!FStrEq(command,"oc2_loadout"))return false;
 if(!player||player->IsBot()||player->entindex()<1||player->entindex()>32)return true;
 auto &state=PlayerState(player);
 const char *action=CMD_ARGV(1);
 if(FStrEq(action,"resync")){if(CMD_ARGC()==2)Snapshot(player);return true;}
 if(FStrEq(action,"begin"))
 {
  std::uint64_t revision=0;state.pending.clear();
  if(CMD_ARGC()==3&&Number(CMD_ARGV(2),std::numeric_limits<unsigned>::max(),revision)&&revision)
  {state.pending="OC2_LOADOUT 1 ";state.pending+=CMD_ARGV(2);state.pending+='\n';state.deadline=gpGlobals->time+15;}
  return true;
 }
 if(state.pending.empty()||gpGlobals->time>state.deadline){state.pending.clear();return true;}
 if(FStrEq(action,"end"))
 {
  Config candidate;std::string error;
  const bool valid=CMD_ARGC()==2&&ParseConfig(state.pending,candidate,error)&&ValidateConfig(candidate,catalog,error)&&candidate.knife!=59;
  state.pending.clear();
  if(!valid){ALERT(at_console,"oc2loadout: rejected player=%d %s\n",player->entindex(),error.c_str());return true;}
  state.config=candidate;
  for(auto &entry:issued)
  {
   CBaseEntity *entity=entry.second;
   auto *item=entity?static_cast<CBasePlayerItem *>(entity):nullptr;
   if(item&&item->m_pPlayer==player&&!item->m_bOc2Transferred&&item->m_oc2Appearance.originUserid==state.userid)
    ApplyChoice(item,player);
  }
  Publish();
  ALERT(at_console,"oc2loadout: accepted player=%d revision=%u knife=%d\n",player->entindex(),candidate.revision,candidate.knife);
  return true;
 }
 if(!FStrEq(action,"gun")&&!FStrEq(action,"knife")&&!FStrEq(action,"equip_knife")){state.pending.clear();return true;}
 std::string row;
 for(int i=1;i<CMD_ARGC();++i){if(i>1)row+=' ';row+=CMD_ARGV(i);}
 if(row.size()>256||state.pending.size()+row.size()+1>kMaximumConfigBytes){state.pending.clear();return true;}
 state.pending+=row;state.pending+='\n';
 return true;
}
