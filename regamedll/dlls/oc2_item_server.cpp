#include "precompiled.h"
#include "oc2_weapon_pickup.h"
#include "oc2_item_server.h"
#include "oc2_weapon_physics.h"
#include "../../../../common/oc2_skin_inventory.h"
#include "../../../../common/oc2_glove_inventory.h"
#include "../../../../common/oc2_carry_state.h"
#include "wpn_shared/wpn_molotov.h"
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
int carryMessageId=0;
struct CarryPublished
{
 bool valid=false;
 oc2_carry::State state;
};
CarryPublished carryPublished[33];
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
 bool botGloveAssigned=false;
 int controlGlove=-1;
};
PlayerLoadout players[33];

int RawCarryAmmo(CBasePlayer *player,CBasePlayerItem *item)
{
 if(!item)return 0;
 const int ammo=item->PrimaryAmmoIndex();
 return ammo>=0&&ammo<MAX_AMMO_SLOTS?player->m_rgAmmo[ammo]:0;
}
int CarryAmmo(CBasePlayer *player,CBasePlayerItem *item)
{ return clamp(RawCarryAmmo(player,item),0,255); }
oc2_carry::State CarryCurrent(CBasePlayer *player)
{
 oc2_carry::State state;
 state.player=player->entindex();
 state.userid=GETPLAYERUSERID(player->edict());
 if(!player->IsAlive())return state;
 if(player->m_pActiveItem&&player->m_pActiveItem->m_iId>0&&player->m_pActiveItem->m_iId<=31)
  state.active=player->m_pActiveItem->m_iId;
 if(player->m_pActiveItem&&player->m_pActiveItem->m_flReleaseThrow==-2 &&
    (state.active==WEAPON_HEGRENADE||state.active==WEAPON_FLASHBANG||
     state.active==WEAPON_SMOKEGRENADE||state.active==WEAPON_MOLOTOV))
  state.flags|=oc2_carry::ThrowReleased;
 if(player->m_bHasC4)state.flags|=oc2_carry::HasC4;
 state.he=CarryAmmo(player,player->GetItemById(WEAPON_HEGRENADE));
 state.flash=CarryAmmo(player,player->GetItemById(WEAPON_FLASHBANG));
 state.smoke=CarryAmmo(player,player->GetItemById(WEAPON_SMOKEGRENADE));
 auto *fire=player->GetItemById(WEAPON_MOLOTOV);
 if(fire)
 {
  state.fire=CarryAmmo(player,fire);
  if(state.fire && (static_cast<CBasePlayerWeapon *>(fire)->m_iWeaponState & CMolotovGrenade::INCENDIARY_STATE))
   state.flags|=oc2_carry::Incendiary;
 }
 return state;
}
bool CarryEqual(const oc2_carry::State &a,const oc2_carry::State &b)
{
 return a.player==b.player&&a.userid==b.userid&&a.active==b.active&&
  a.flash==b.flash&&a.he==b.he&&a.smoke==b.smoke&&a.fire==b.fire&&a.flags==b.flags;
}
void SendCarry(int operation,const oc2_carry::State *state=nullptr,CBasePlayer *target=nullptr)
{
 if(!carryMessageId)return;
 if(target)MESSAGE_BEGIN(MSG_ONE,carryMessageId,nullptr,target->pev);
 else MESSAGE_BEGIN(MSG_ALL,carryMessageId);
 WRITE_BYTE(oc2_carry::kVersion);WRITE_BYTE(operation);
 if(operation!=oc2_carry::Reset){WRITE_BYTE(state->player);WRITE_LONG(state->userid);}
 if(operation==oc2_carry::Upsert)
 {
  WRITE_BYTE(state->active);WRITE_BYTE(state->flash);WRITE_BYTE(state->he);
  WRITE_BYTE(state->smoke);WRITE_BYTE(state->fire);WRITE_BYTE(state->flags);
 }
 MESSAGE_END();
}
void PublishCarry()
{
 for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)
 {
  auto *player=UTIL_PlayerByIndex(i);
  auto &previous=carryPublished[i];
  if(!player||player->IsDormant())
  {
   if(previous.valid){SendCarry(oc2_carry::Remove,&previous.state);previous=CarryPublished();}
   continue;
  }
  const auto state=CarryCurrent(player);
  if(state.userid<=0)
  {
   if(previous.valid){SendCarry(oc2_carry::Remove,&previous.state);previous=CarryPublished();}
   continue;
  }
  if(!previous.valid||!CarryEqual(previous.state,state))
  {
   SendCarry(oc2_carry::Upsert,&state);
   previous.valid=true;previous.state=state;
  }
 }
}
void CarrySnapshot(CBasePlayer *target)
{
 PublishCarry();
 SendCarry(oc2_carry::Reset,nullptr,target);
 for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)
  if(carryPublished[i].valid)SendCarry(oc2_carry::Upsert,&carryPublished[i].state,target);
}

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
  const auto &w=a->accessory;
  WRITE_BYTE((a->accessoryReady?4:0)|(w.stattrak?1:0)|(w.charm?2:0));
  for(float v:{w.statU,w.statV,w.charmU,w.charmV,w.amplitude,w.length,w.stiffness,w.damping})WRITE_LONG(oc2_item::FloatBits(v));
  WRITE_LONG(w.sticker);WRITE_LONG(a->count);WRITE_LONG(a->charm);
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
void EquipBotGlove(CBasePlayer *player)
{
 if(!player||!player->IsBot())return;
 auto &state=PlayerState(player);
 if(state.userid<=0)return; // Fake-client construction may precede its userid.
 if(state.botGloveAssigned)return;
 // A wearable belongs to the player connection, never to an acquired gun.
 // Keep it across deaths/rounds; a new userid receives a new random choice.
 state.botGloveAssigned=true;
 state.glove=gloveCatalog.empty()?0:gloveCatalog[RANDOM_LONG(0,int(gloveCatalog.size())-1)].id;
 SendGlove(oc2_gloves::Upsert,player->entindex(),state.controlGlove>=0?state.controlGlove:state.glove);
 ALERT(at_console,"oc2gloveserver: bot player=%d userid=%d resource=%d catalog=%u\n",
  player->entindex(),state.userid,state.glove,unsigned(gloveCatalog.size()));
}
void GloveSnapshot(CBasePlayer *target)
{
 SendGlove(oc2_gloves::Reset,0,0,target);
 for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)
 {
  auto *peer=UTIL_PlayerByIndex(i);
  if(peer&&!peer->IsDormant())
  {
   EquipBotGlove(peer);
   SendGlove(oc2_gloves::Upsert,i,PlayerState(peer).controlGlove>=0?PlayerState(peer).controlGlove:PlayerState(peer).glove,target);
  }
 }
}
void Snapshot(CBasePlayer *player)
{
 Publish();Send(oc2_item::Reset,nullptr,player);
 for(const auto &entry:published)Send(oc2_item::Upsert,&entry.second,player);
 GloveSnapshot(player);
 CarrySnapshot(player);
 PlayerState(player).snapshot=false;
}
void DropStateDump()
{
 OC2_WeaponPhysicsDump();
 CBaseEntity *entity=nullptr;
 while((entity=UTIL_FindEntityByClassname(entity,"weaponbox")))
 {
  if(entity->pev->flags&FL_KILLME)continue;
  auto *box=static_cast<CWeaponBox *>(entity);
  OC2_WeaponPhysicsDump(box);
  for(int slot=0;slot<MAX_ITEM_TYPES;++slot)
   for(auto *item=box->m_rgpPlayerItems[slot];item;item=item->m_pNext)
   {
    auto *weapon=static_cast<CBasePlayerWeapon *>(item);
    const int ammoIndex=item->pszAmmo1()?box->GetAmmoIndex(item->pszAmmo1()):-1;
    const int ammo=ammoIndex>=0&&ammoIndex<MAX_AMMO_SLOTS?box->m_rgAmmo[ammoIndex]:0;
    ALERT(at_console,"oc2drop_box: t=%.3f entity=%d weapon=%d ammo=%d defaultammo=%d firestate=%d serial=%d model=%s origin=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f angular=%.3f,%.3f,%.3f angles=%.3f,%.3f,%.3f onground=%d solid=%d movetype=%d\n",
     gpGlobals->time,box->entindex(),item->m_iId,ammo,weapon->m_iDefaultAmmo,weapon->m_iWeaponState,item->m_oc2Appearance.serial,STRING(box->pev->model),
     box->pev->origin.x,box->pev->origin.y,box->pev->origin.z,box->pev->velocity.x,box->pev->velocity.y,box->pev->velocity.z,
     box->pev->avelocity.x,box->pev->avelocity.y,box->pev->avelocity.z,box->pev->angles.x,box->pev->angles.y,box->pev->angles.z,
     int(bool(box->pev->flags&FL_ONGROUND)),box->pev->solid,box->pev->movetype);
   }
 }
}
void Dump()
{
 Publish();PublishCarry();
 ALERT(at_console,"oc2items: count=%u catalog=%u\n",unsigned(published.size()),unsigned(catalog.size()));
 for(const auto &entry:published)
 {
  const auto &a=entry.second;
  ALERT(at_console,"oc2item: serial=%d entity=%d owner=%d box=%d weapon=%d definition=%d skin=%d stickers=%d/%d/%d/%d origin=%d flags=%d\n",
   a.serial,a.entity,a.owner,a.box,a.weapon,a.definition,a.skin,a.stickers[0],a.stickers[1],a.stickers[2],a.stickers[3],a.originUserid,a.flags);
   auto found=issued.find(a.serial);
   if(found!=issued.end())if(CBaseEntity *entity=found->second)
   {
    const auto &appearance=static_cast<CBasePlayerItem *>(entity)->m_oc2Appearance;
    ALERT(at_console,"oc2item_accessory: serial=%d owner=%d stattrak=%d charm=%d pattern=%d count=%u\n",
     a.serial,a.owner,appearance.accessory.stattrak,appearance.accessory.charm,appearance.charm,appearance.count);
   }
 }
 for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)
 {
  auto *peer=UTIL_PlayerByIndex(i);
  if(!peer||peer->IsDormant())continue;
  EquipBotGlove(peer);
  const auto &state=PlayerState(peer);
  ALERT(at_console,"oc2gloveserver_player: player=%d userid=%d bot=%d resource=%d revision=%u assigned=%d\n",
   i,state.userid,int(peer->IsBot()),state.glove,state.gloveRevision,int(state.botGloveAssigned));
  const auto carry=CarryCurrent(peer);
  auto *active=peer->m_pActiveItem;
  auto *activeWeapon=active?static_cast<CBasePlayerWeapon *>(active):nullptr;
  ALERT(at_console,"oc2drop_player: t=%.3f player=%d alive=%d active=%d clip=%d reserve=%d start=%.3f release=%.3f nextattack=%.3f nextprimary=%.3f origin=%.3f,%.3f,%.3f view=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f\n",
   gpGlobals->time,i,int(peer->IsAlive()),active?active->m_iId:0,activeWeapon?activeWeapon->m_iClip:-1,RawCarryAmmo(peer,active),
   active?active->m_flStartThrow:0.0f,active?active->m_flReleaseThrow:0.0f,float(peer->m_flNextAttack),
   activeWeapon?float(activeWeapon->m_flNextPrimaryAttack):0.0f,
   peer->pev->origin.x,peer->pev->origin.y,peer->pev->origin.z,peer->pev->v_angle.x,peer->pev->v_angle.y,peer->pev->v_angle.z,
   peer->pev->velocity.x,peer->pev->velocity.y,peer->pev->velocity.z);
  auto *fire=peer->GetItemById(WEAPON_MOLOTOV);
  ALERT(at_console,"oc2carry_server: player=%d userid=%d alive=%d active=%d release=%.3f c4=%d he=%d flash=%d smoke=%d fire=%d firestate=%d protocol_active=%d protocol_he=%d protocol_flash=%d protocol_smoke=%d protocol_fire=%d protocol_flags=%d\n",
   i,carry.userid,int(peer->IsAlive()),active?active->m_iId:0,active?active->m_flReleaseThrow:0.0f,
   int(peer->m_bHasC4),RawCarryAmmo(peer,peer->GetItemById(WEAPON_HEGRENADE)),
   RawCarryAmmo(peer,peer->GetItemById(WEAPON_FLASHBANG)),RawCarryAmmo(peer,peer->GetItemById(WEAPON_SMOKEGRENADE)),
   RawCarryAmmo(peer,fire),fire?static_cast<CBasePlayerWeapon *>(fire)->m_iWeaponState:0,
   carry.active,carry.he,carry.flash,carry.smoke,carry.fire,carry.flags);
 }
 DropStateDump();
}
bool TestUtilityWeapon(int value)
{
 return value==WEAPON_HEGRENADE||value==WEAPON_C4||value==WEAPON_SMOKEGRENADE||
  value==WEAPON_FLASHBANG||value==WEAPON_MOLOTOV;
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
 if(!caller||CVAR_GET_FLOAT("sv_cheats")==0||IS_DEDICATED_SERVER()||caller->entindex()!=1||CMD_ARGC()!=4)return;
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
  if(FStrEq(action,"money"))
  {
   if(value<=16000)target->AddAccount(value-target->m_iAccount,RT_NONE,false);
   return;
  }
  if(FStrEq(action,"aimhead")||FStrEq(action,"aimbody"))
  {
   auto *victim=value>0&&value<=gpGlobals->maxClients?UTIL_PlayerByIndex(value):nullptr;
   if(!victim||victim==target||!victim->IsAlive())return;
   Vector point=victim->BodyTarget(target->GetGunPosition());
   if(FStrEq(action,"aimhead"))
   {
    auto *hdr=static_cast<studiohdr_t *>(GET_MODEL_PTR(victim->edict()));
    if(!hdr||hdr->numhitboxes<1||hdr->numhitboxes>128||hdr->hitboxindex<int(sizeof(studiohdr_t))
        ||hdr->hitboxindex>hdr->length-int(sizeof(mstudiobbox_t))*hdr->numhitboxes)return;
    const auto *boxes=reinterpret_cast<const mstudiobbox_t *>(reinterpret_cast<const byte *>(hdr)+hdr->hitboxindex);
    for(int i=0;i<hdr->numhitboxes;++i)if(boxes[i].group==HITGROUP_HEAD&&boxes[i].bone>=0&&boxes[i].bone<hdr->numbones)
    {
     Vector origin,angles;GET_BONE_POSITION(victim->edict(),boxes[i].bone,origin,angles);
     float matrix[3][4];AngleMatrix(angles,matrix);
     const Vector local=(boxes[i].bbmin+boxes[i].bbmax)*0.5f;
     point=origin+Vector(DotProduct(local,matrix[0]),DotProduct(local,matrix[1]),DotProduct(local,matrix[2]));break;
    }
   }
   Vector angles=UTIL_VecToAngles(point-target->GetGunPosition());angles.x=-angles.x;angles.z=0;
   target->pev->v_angle=target->pev->angles=angles;target->pev->fixangle=1;
   return;
  }
 if(FStrEq(action,"utility"))
 {
  if(!TestUtilityWeapon(value))return;
  const auto *info=GetWeaponInfo(value);
  if(!info||!info->entityName)return;
  const char *name=value==WEAPON_MOLOTOV&&target->m_iTeam==CT?"weapon_incendiary":info->entityName;
  if(!target->GetItemById(static_cast<WeaponIdType>(value)))target->GiveNamedItem(name);
  auto *item=target->GetItemById(static_cast<WeaponIdType>(value));
  if(item&&value==WEAPON_C4)
  {
   target->m_bHasC4=true;target->pev->body=1;target->SetBombIcon();
  }
  else if(item)
  {
   const int index=item->PrimaryAmmoIndex();
   const int maximum=value==WEAPON_FLASHBANG?2:1;
   if(index>=0&&index<MAX_AMMO_SLOTS&&item->pszAmmo1()&&target->m_rgAmmo[index]<maximum)
    target->GiveAmmo(maximum-target->m_rgAmmo[index],item->pszAmmo1(),maximum);
  }
  Dump();return;
 }
 if(FStrEq(action,"aim"))
 {
  if(value>359)return;
  target->pev->v_angle=target->pev->angles=Vector(12,float(value),0);
  target->pev->fixangle=1;
  return;
 }
 if(FStrEq(action,"aimbox"))
 {
  if(value<=gpGlobals->maxClients||value>=gpGlobals->maxEntities)return;
  edict_t *edict=INDEXENT(value);
  if(FNullEnt(edict)||!FClassnameIs(&edict->v,"weaponbox"))return;
  auto *entity=CBaseEntity::Instance(edict);
  Vector point;
   if(!entity)return;
   if(!OC2_WeaponPhysicsAimPoint(static_cast<CWeaponBox *>(entity),point))
   {
    // C4 is deliberately excluded from bullet physics; aim its real Studio
    // hitbox here for local test camera placement only.
    auto *box=static_cast<CWeaponBox *>(entity);
    auto *hdr=static_cast<studiohdr_t *>(GET_MODEL_PTR(box->edict()));
    if(!box->m_bIsBomb||!hdr||hdr->numhitboxes<1||hdr->numhitboxes>128||hdr->hitboxindex<int(sizeof(studiohdr_t))||hdr->hitboxindex>hdr->length-int(sizeof(mstudiobbox_t))*hdr->numhitboxes)return;
    const auto *hitbox=reinterpret_cast<const mstudiobbox_t *>(reinterpret_cast<const byte *>(hdr)+hdr->hitboxindex);
    if(hitbox->bone<0||hitbox->bone>=hdr->numbones)return;
    Vector origin,angles;GET_BONE_POSITION(box->edict(),hitbox->bone,origin,angles);
    float matrix[3][4];AngleMatrix(angles,matrix);
    const Vector local=(hitbox->bbmin+hitbox->bbmax)*.5f;
    point=origin+Vector(DotProduct(local,matrix[0]),DotProduct(local,matrix[1]),DotProduct(local,matrix[2]));
   }
  Vector angles=UTIL_VecToAngles(point-target->GetGunPosition());
  angles.x=-angles.x;angles.z=0;
  target->pev->v_angle=target->pev->angles=angles;
  target->pev->fixangle=1;
  return;
 }
 if(FStrEq(action,"aimhostage"))
 {
  if(value<=gpGlobals->maxClients||value>=gpGlobals->maxEntities)return;
  edict_t *edict=INDEXENT(value);
  if(FNullEnt(edict)||!FClassnameIs(&edict->v,"hostage_entity"))return;
  auto *hostage=CBaseEntity::Instance(edict);
  if(!hostage||!hostage->IsAlive())return;
  // Local cheat camera placement only. The subsequent +attack/+use still
  // executes the normal weapon trace, damage and interaction paths.
  Vector angles=UTIL_VecToAngles(hostage->Center()-target->GetGunPosition());
  angles.x=-angles.x;angles.z=0;
  target->pev->v_angle=target->pev->angles=angles;
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
   if(value!=WEAPON_KNIFE&&value!=WEAPON_TASER&&oc2_accessories::Index(value)<0&&
     !TestUtilityWeapon(value))return;
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
    target->GiveNamedItemEx(info->entityName);
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

void OC2_ItemsControlGlove(CBasePlayer *player,CBasePlayer *bot)
{
 if(!player)return;
 auto &state=PlayerState(player);
 state.controlGlove=bot?PlayerState(bot).glove:-1;
 SendGlove(oc2_gloves::Upsert,player->entindex(),state.controlGlove>=0?state.controlGlove:state.glove);
}
void OC2_ItemsKill(CBasePlayer *player)
{
 auto *item=player?player->m_pActiveItem:nullptr;
 if(!item||!item->m_oc2Appearance.accessory.stattrak)return;
 auto &a=item->m_oc2Appearance;
 if((item->m_bOc2Transferred||(a.flags&oc2_item::BotOrigin))&&a.count<oc2_accessories::kMaximumCount)++a.count;
}
void OC2_ItemsRegisterMessage()
{ messageId=REG_USER_MSG(oc2_item::kMessage,-1);gloveMessageId=REG_USER_MSG(oc2_gloves::kMessage,-1);carryMessageId=REG_USER_MSG(oc2_carry::kMessage,-1); }

void OC2_ItemsPrecache()
{
 issued.clear();published.clear();nextSerial=1;catalog.clear();knives.clear();stickers.clear();knifeSkins.clear();
 for(auto &v:gunSkins)v.clear();
 for(auto &v:players)v=PlayerLoadout();
 for(auto &v:carryPublished)v=CarryPublished();
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
 if(player->IsBot())EquipBotGlove(player);
 else SendGlove(oc2_gloves::Upsert,player->entindex(),0);
}
void OC2_ItemsClientDisconnected(CBasePlayer *player)
{
 if(!player||player->entindex()<1||player->entindex()>32)return;
 auto &carry=carryPublished[player->entindex()];
 if(carry.valid){SendCarry(oc2_carry::Remove,&carry.state);carry=CarryPublished();}
 players[player->entindex()]=PlayerLoadout();
 SendGlove(oc2_gloves::Upsert,player->entindex(),0);
}
void OC2_ItemsAcquire(CBasePlayerItem *item,CBasePlayer *player)
{
 if(!item||!player||(item->m_iId!=WEAPON_KNIFE&&oc2_accessories::Index(item->m_iId)<0))return;
 EquipBotGlove(player);
 auto &a=item->m_oc2Appearance;
 if(!a.serial)
 {
  a.serial=nextSerial++;a.weapon=item->m_iId;
  a.originUserid=GETPLAYERUSERID(player->edict());
  a.flags=player->IsBot()?oc2_item::BotOrigin:0;
  ApplyChoice(item,player);
  const auto defaults=oc2_accessories::DefaultConfig();
  if(const auto *config=oc2_accessories::Find(defaults,item->m_iId))
  {
   a.accessory=*config;a.accessoryReady=true;
   if(player->IsBot())
   {
     // Decide only once for this item instance; transfers and possession keep
     // both independent choices. The catalogue already caches valid stickers.
     unsigned seed=BotSeed(player)^(unsigned(item->m_iId)*0x9e3779b9u)^(a.serial*0x85ebca6bu);
     seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;
     a.accessory.stattrak=(seed%100)<60;
     a.accessory.charm=((seed>>8)%100)<55;
     // -1 is the protocol's explicit no-charm selection. A positive pattern
     // otherwise enables the charm in both world and first-person rendering.
     a.charm=a.accessory.charm?Pick(stickers,seed):-1;
   }
  }
 }
 else if(a.originUserid!=GETPLAYERUSERID(player->edict()))item->m_bOc2Transferred=true;
 issued[a.serial]=static_cast<CBaseEntity *>(item);
}
void OC2_ItemsFrame()
{
 OC2_WeaponPhysicsFrame();
 OC2_PickupFrame();
 Publish();
 PublishCarry();
 for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i)
 {
  auto *player=UTIL_PlayerByIndex(i);
  if(!player||player->IsDormant())continue;
  if(player->IsBot()){EquipBotGlove(player);continue;}
  auto &state=PlayerState(player);
  if(state.snapshot&&!player->m_fInitHUD)Snapshot(player);
  if(!state.pending.empty()&&gpGlobals->time>state.deadline)state.pending.clear();
 }
}
bool OC2_ItemsCommand(CBasePlayer *player,const char *command)
{
 if(FStrEq(command,"oc2_item_acc"))
 {
  if(!player||player->IsBot()||CMD_ARGC()!=16)return true;
  std::ostringstream row;for(int i=1;i<CMD_ARGC();++i)row<<CMD_ARGV(i)<<' ';
  std::istringstream input(row.str());int serial=0,stat=0,charm=0;Appearance candidate;
  auto &w=candidate.accessory;
  if(!(input>>serial>>w.weapon>>stat>>w.statU>>w.statV>>charm>>w.charmU>>w.charmV>>w.sticker>>w.amplitude>>w.length>>w.stiffness>>w.damping>>candidate.count>>candidate.charm)||!oc2_accessories::End(input)||stat<0||stat>1||charm<0||charm>1||!oc2_accessories::Valid(w)||candidate.count>oc2_accessories::kMaximumCount)return true;
  w.stattrak=stat!=0;w.charm=charm!=0;
  if(candidate.charm>0){const auto *entry=FindEntry(catalog,candidate.charm);if(!entry||entry->kind!=Kind::Sticker)return true;}
  const auto found=issued.find(serial);if(found==issued.end())return true;
  auto *item=static_cast<CBasePlayerItem *>((CBaseEntity *)found->second);
  if(!item||item->m_pPlayer!=player||item->m_bOc2Transferred||item->m_iId!=w.weapon||item->m_oc2Appearance.originUserid!=GETPLAYERUSERID(player->edict()))return true;
  item->m_oc2Appearance.accessory=w;item->m_oc2Appearance.accessoryReady=true;
  item->m_oc2Appearance.count=candidate.count;item->m_oc2Appearance.charm=candidate.charm;Publish();return true;
 }
 if(FStrEq(command,"oc2_glove_test"))
 {
  // Explicit local verification only; changes server memory, never a DAT.
  if(!player||player->entindex()!=1||IS_DEDICATED_SERVER()||CVAR_GET_FLOAT("sv_cheats")==0||CMD_ARGC()!=3)return true;
  int index=0,resource=0;
  if(!ArgumentNumber(1,32,index)||index<1||!ArgumentNumber(2,std::numeric_limits<int>::max(),resource)||
     (resource&&!oc2_gloves::FindEntry(gloveCatalog,resource)))return true;
  auto *target=UTIL_PlayerByIndex(index);
  if(!target||target->IsDormant())return true;
  auto &state=PlayerState(target);state.glove=resource;state.botGloveAssigned=target->IsBot();
  SendGlove(oc2_gloves::Upsert,index,resource);
  ALERT(at_console,"oc2gloveserver: test player=%d resource=%d\n",index,resource);
  return true;
 }
 if(FStrEq(command,"oc2_glove"))
 {
  if(!player||player->IsBot()||player->entindex()<1||player->entindex()>32)return true;
  std::uint64_t revision=0,resource=0;
  if(CMD_ARGC()!=3||!Number(CMD_ARGV(1),std::numeric_limits<unsigned>::max(),revision)||!revision||
     !Number(CMD_ARGV(2),std::numeric_limits<int>::max(),resource)||
     (resource&&!oc2_gloves::FindEntry(gloveCatalog,int(resource))))
   {ALERT(at_console,"oc2gloveserver: rejected player=%d\n",player->entindex());return true;}
  auto &state=PlayerState(player);state.glove=int(resource);state.gloveRevision=unsigned(revision);
  SendGlove(oc2_gloves::Upsert,player->entindex(),state.controlGlove>=0?state.controlGlove:state.glove);
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
