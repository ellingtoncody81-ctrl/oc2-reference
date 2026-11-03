#include "precompiled.h"
#include "oc2_weapon_pickup.h"
#include "oc2_weapon_physics.h"
#include "hostage/hostage.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
constexpr float kDistance=500, kDeliver=140, kSpeed=360, kTimeout=4, kCooldown=.35f;
const char *sounds[]={"misc/silahat1.wav","misc/silahat2.wav","misc/silahat3.wav"};
cvar_t debug={"oc2_pickup_debug","0",FCVAR_SERVER,0,nullptr};
cvar_t rejectOnce={"oc2_pickup_test_reject","0",0,0,nullptr};
bool registered=false;
std::string map;
struct BoxState
{
 EHANDLE box, claimant, dropper;
 int userid=0, dropUserid=0, team=0;
 float dropped=-100, started=0;
 bool landingSound=false;
};
std::vector<BoxState> boxes;
struct PlayerState {int userid=0;float next=0;};
PlayerState players[33];
CWeaponBox *deliveryBox=nullptr;
CBasePlayer *deliveryPlayer=nullptr;
bool Finite(const Vector &v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
bool Valid(CWeaponBox *b){return b && !FNullEnt(b->edict()) && !(b->pev->flags&FL_KILLME) && FClassnameIs(b->pev,"weaponbox");}
void Init()
{
 if(!registered){CVAR_REGISTER(&debug);CVAR_REGISTER(&rejectOnce);registered=true;}
 // Player command prediction may temporarily rewind gpGlobals->time relative
 // to StartFrame. Reset by map lifecycle, never by an individual command clock.
 if(map!=STRING(gpGlobals->mapname))
 {boxes.clear();for(auto &p:players)p=PlayerState();map=STRING(gpGlobals->mapname);}
}
BoxState &State(CWeaponBox *b)
{
 for(auto &s:boxes)if(s.box==b)return s;
 boxes.emplace_back();boxes.back().box=b;return boxes.back();
}
void PickupLog(const char *event,CBasePlayer *p,CWeaponBox *b,int weapon=0,int serial=0)
{
 if(debug.value)ALERT(at_console,"oc2pickup: event=%s player=%d box=%d weapon=%d serial=%d t=%.3f\n",event,p?p->entindex():0,b?b->entindex():0,weapon,serial,gpGlobals->time);
}
void Message(CBasePlayer *p,const char *text){if(p&&!p->IsBot())ClientPrint(p->pev,HUD_PRINTCENTER,text);}
void PickupSound(CBaseEntity *source)
{
 const char *sample=sounds[RANDOM_LONG(0,2)];
 EMIT_SOUND(source->edict(),CHAN_WEAPON,sample,VOL_NORM,ATTN_NORM);
 if(debug.value)ALERT(at_console,"oc2pickup_sound: entity=%d landing=%d onground=%d sample=%s t=%.3f\n",source->entindex(),int(FClassnameIs(source->pev,"weaponbox")),int(bool(source->pev->flags&FL_ONGROUND)),sample,gpGlobals->time);
}
CBasePlayerItem *Item(CWeaponBox *b)
{
 if(!Valid(b))return nullptr;
 CBasePlayerItem *result=nullptr;
 for(int slot=1;slot<MAX_ITEM_TYPES;++slot)for(auto *w=b->m_rgpPlayerItems[slot];w;w=w->m_pNext)
 {if(result)return nullptr;result=w;}
 return result;
}
bool ClearLine(const Vector &a,const Vector &b,CBaseEntity *ignore)
{
 TraceResult tr{};UTIL_TraceLine(a,b,dont_ignore_monsters,ignore?ignore->edict():nullptr,&tr);
 return !tr.fStartSolid&&!tr.fAllSolid&&tr.flFraction>=.999f;
}
bool Eligible(CBasePlayer *p,CWeaponBox *b)
{
 auto *w=Item(b);
 if(!p||!p->IsAlive()||p->GetObserverMode()!=OBS_NONE||!w||w->m_pPlayer||p->m_bIsVIP||p->m_bShieldDrawn)return false;
 if(w->m_iId==WEAPON_C4 && (p->m_iTeam!=TERRORIST||p->GetItemById(WEAPON_C4)))return false;
 if(p->HasShield()&&(w->iItemSlot()==PRIMARY_WEAPON_SLOT||w->m_iId==WEAPON_ELITE))return false;
#ifdef REGAMEDLL_ADD
 if(p->HasRestrictItem((ItemID)w->m_iId,ITEM_TYPE_TOUCHED))return false;
#endif
 return true;
}
void Release(BoxState &s,const char *reason,bool notify)
{
 auto *b=s.box.Get<CWeaponBox>();auto *p=s.claimant.Get<CBasePlayer>();
 if(Valid(b))b->pev->velocity=g_vecZero;
 PickupLog(reason,p,b);if(notify)Message(p,u8"\u62fe\u53d6\u5df2\u53d6\u6d88");s.claimant=nullptr;s.userid=0;
}
// The old entity stays intact until delivery succeeds. No GiveNamedItem rollback.
bool Deliver(CBasePlayer *p,CWeaponBox *b)
{
 auto *incoming=Item(b);if(!incoming||!Eligible(p,b))return false;
 const int slot=incoming->iItemSlot();
 auto *old=(slot==PRIMARY_WEAPON_SLOT||slot==PISTOL_SLOT)?p->m_rgpPlayerItems[slot]:nullptr;
 if(old&&(!old->CanDrop()||!old->CanHolster())){Message(p,u8"\u5f53\u524d\u6b66\u5668\u65e0\u6cd5\u4ea4\u6362");return false;}
 CWeaponBox *discard=nullptr;
 if(old)
 {
  discard=static_cast<CWeaponBox *>(CBaseEntity::Create("weaponbox",p->pev->origin,p->pev->angles));
  if(!discard)return false;
  discard->pev->solid=SOLID_NOT;discard->pev->effects|=EF_NODRAW;
 }
 CBasePlayerItem *oldNext=old?old->m_pNext:nullptr;
 CBasePlayerItem *slotHead=p->m_rgpPlayerItems[slot];
 entvars_t oldVars={};if(old)oldVars=*old->pev;
 const bool oldTransferred=old&&old->m_bOc2Transferred;
 const entvars_t incomingVars=*incoming->pev;
 auto *incomingOwner=incoming->m_pPlayer;auto *incomingNext=incoming->m_pNext;
 const auto incomingAppearance=incoming->m_oc2Appearance;
 const bool incomingTransferred=incoming->m_bOc2Transferred;
 auto *incomingWeapon=static_cast<CBasePlayerWeapon *>(incoming);
 const int incomingDefault=incomingWeapon->m_iDefaultAmmo,incomingClip=incomingWeapon->m_iClip;
 int ammo[MAX_AMMO_SLOTS];Q_memcpy(ammo,p->m_rgAmmo,sizeof(ammo));
 const int bits=p->pev->weapons,hide=p->m_iHideHUD;
 auto *active=p->m_pActiveItem;auto *last=p->m_pLastItem;
 const int incomingId=incoming->m_iId, serial=incoming->m_oc2Appearance.serial;
 EHANDLE inHandle=incoming;
 const int ammoIndex=incoming->PrimaryAmmoIndex();
 const int quantity=ammoIndex>=0&&ammoIndex<MAX_AMMO_SLOTS?p->m_rgAmmo[ammoIndex]:-1;
 if(old)
 {
  // Stage the ORIGINAL old gun before touching the target. Suppress Holster's
  // AdvanceReload until commit, so rejection cannot duplicate reserve into clip.
  if(active==old)p->m_pActiveItem=nullptr;
  if(!discard->PackWeapon(old))
  {
   discard->m_rgpPlayerItems[slot]=nullptr;*old->pev=oldVars;
   old->m_pPlayer=p;old->m_pNext=oldNext;old->m_bOc2Transferred=oldTransferred;
   p->m_rgpPlayerItems[slot]=slotHead;
   p->m_pActiveItem=active;p->m_pLastItem=last;
   UTIL_Remove(discard);PickupLog("stage_rejected",p,b,incomingId,serial);return false;
  }
 }
 deliveryBox=b;deliveryPlayer=p;
 const bool reject=CVAR_GET_FLOAT("sv_cheats")!=0&&rejectOnce.value!=0;
 if(debug.value)ALERT(at_console,"oc2pickup_detail: phase=before player=%d weapon=%d slot=%d head=%d reject=%d team=%d vip=%d shield=%d\n",p->entindex(),incomingId,slot,p->m_rgpPlayerItems[slot]?p->m_rgpPlayerItems[slot]->m_iId:0,int(reject),p->m_iTeam,int(p->m_bIsVIP),int(p->m_bShieldDrawn));
 if(reject)CVAR_SET_FLOAT("oc2_pickup_test_reject",0);
 else b->Touch(p);
 deliveryBox=nullptr;deliveryPlayer=nullptr;
 auto *received=inHandle.Get<CBasePlayerItem>();
 bool stillBoxed=false;
 for(auto *w=b->m_rgpPlayerItems[slot];w;w=w->m_pNext)if(w==received){stillBoxed=true;break;}
 const bool acquired=received&&!stillBoxed&&received->m_pPlayer==p&&received->pev->owner==p->edict()&&received->pev->movetype==MOVETYPE_FOLLOW&&p->GetItemById((WeaponIdType)incomingId)==received;
 if(debug.value)ALERT(at_console,"oc2pickup_detail: phase=after player=%d weapon=%d boxed=%d owner=%d entity_owner=%d move=%d in_chain=%d acquired=%d\n",p->entindex(),incomingId,int(stillBoxed),received&&received->m_pPlayer?received->m_pPlayer->entindex():0,received&&received->pev->owner?ENTINDEX(received->pev->owner):0,received?int(received->pev->movetype):-1,int(received&&p->GetItemById((WeaponIdType)incomingId)==received),int(acquired));
 const bool stacked=slot==GRENADE_SLOT&&ammoIndex>=0&&ammoIndex<MAX_AMMO_SLOTS&&p->m_rgAmmo[ammoIndex]>quantity;
 if(!acquired&&!stacked)
 {
  p->m_rgpPlayerItems[slot]=slotHead;
  if(old)
  {
   discard->m_rgpPlayerItems[slot]=nullptr;
   *old->pev=oldVars;old->m_pPlayer=p;old->m_pNext=oldNext;
   old->m_bOc2Transferred=oldTransferred;p->m_rgpPlayerItems[slot]=old;
  }
  if(received)
  {
   *received->pev=incomingVars;received->m_pPlayer=incomingOwner;received->m_pNext=incomingNext;
   received->m_oc2Appearance=incomingAppearance;received->m_bOc2Transferred=incomingTransferred;
   incomingWeapon->m_iDefaultAmmo=incomingDefault;incomingWeapon->m_iClip=incomingClip;
  }
  Q_memcpy(p->m_rgAmmo,ammo,sizeof(ammo));p->pev->weapons=bits;p->m_iHideHUD=hide;
  p->m_pActiveItem=active;p->m_pLastItem=last;p->TabulateAmmo();
  if(discard)UTIL_Remove(discard);
  PickupLog("rollback",p,b,incomingId,serial);Message(p,u8"\u62fe\u53d6\u5931\u8d25\uff0c\u539f\u6b66\u5668\u5df2\u4fdd\u7559");return false;
 }
 if(old)
 {
  auto *oldWeapon=static_cast<CBasePlayerWeapon *>(old);
  oldWeapon->m_fInReload=FALSE;oldWeapon->m_fInSpecialReload=0;
  const char *model=GetCSModelName(old->m_iId);
  discard->SetModel(model);discard->pev->effects&=~EF_NODRAW;discard->pev->solid=SOLID_TRIGGER;
  discard->pev->movetype=MOVETYPE_TOSS;
  UTIL_MakeVectors(p->pev->v_angle);discard->pev->velocity=p->pev->velocity+gpGlobals->v_forward*180;
  UTIL_SetOrigin(discard->pev,p->pev->origin+gpGlobals->v_forward*10);
  discard->SetThink(&CWeaponBox::Kill);discard->pev->nextthink=gpGlobals->time+CGameRules::GetItemKillDelay();
  OC2_PickupDropped(discard,p);
  if(old->m_iId!=incomingId&&!p->GetItemById((WeaponIdType)old->m_iId))p->pev->weapons&=~(1u<<old->m_iId);
  if(active==old)
  {
   p->ResetAutoaim();p->pev->fov=p->m_iFOV=p->m_iLastZoom=DEFAULT_FOV;p->m_bResumeZoom=false;
   p->pev->viewmodel=0;p->pev->weaponmodel=0;
  }
 }
 if(acquired&&p->m_pActiveItem!=received&&(active==old||!p->m_pActiveItem||CSGameRules()->FShouldSwitchWeapon(p,received)))p->SwitchWeapon(received);
 PickupSound(p);
 PickupLog("success",p,b,incomingId,serial);Message(p,u8"\u5df2\u62fe\u53d6\u6b66\u5668");return true;
}
}

void OC2_PickupPrecache(){Init();boxes.clear();for(auto &p:players)p=PlayerState();for(const char *s:sounds)PRECACHE_SOUND(const_cast<char *>(s));}
void OC2_PickupDropped(CWeaponBox *b,CBasePlayer *p)
{
 Init();if(!Valid(b))return;auto &s=State(b);s.dropper=p;s.dropUserid=p?GETPLAYERUSERID(p->edict()):0;s.dropped=gpGlobals->time;
}
void OC2_PickupSoundOnLanding(CBaseEntity *entity)
{
 if(!entity || !FClassnameIs(entity->pev,"weaponbox"))return;
 Init();auto *box=static_cast<CWeaponBox *>(entity);if(Valid(box))State(box).landingSound=true;
}
bool OC2_PickupUseDelivery(CWeaponBox *b,CBasePlayer *p){return deliveryBox==b&&deliveryPlayer==p;}
bool OC2_PickupDeferSwitch(CBasePlayer *p){return deliveryPlayer==p;}
bool OC2_PickupCanTouch(CWeaponBox *b,CBasePlayer *p)
{
 Init();if(OC2_PickupUseDelivery(b,p))return true;
 for(auto &s:boxes)if(s.box==b)
 {
  if(s.claimant)return false;
  if(s.dropper==p&&s.dropUserid==GETPLAYERUSERID(p->edict())&&gpGlobals->time-s.dropped<1.5f)return false;
 }
 return true;
}
bool OC2_PickupTryUse(CBasePlayer *p)
{
 Init();if(!(p->m_afButtonPressed&IN_USE)||!p->IsAlive()||p->GetObserverMode()!=OBS_NONE||p->m_bIsDefusing||OC2_HostageGrabbingBy(p)||OC2_HostageCarriedBy(p))return false;
 UTIL_MakeVectors(p->pev->v_angle);
 const Vector start=p->GetGunPosition(),end=start+gpGlobals->v_forward*kDistance;
 TraceResult obstruction{};UTIL_TraceLine(start,end,dont_ignore_monsters,p->edict(),&obstruction);
 const float limit=obstruction.flFraction;
 if(debug.value)ALERT(at_console,"oc2pickup_trace: player=%d eye=%.2f,%.2f,%.2f angles=%.2f,%.2f limit=%.4f\n",p->entindex(),start.x,start.y,start.z,p->pev->v_angle.x,p->pev->v_angle.y,limit);
 CWeaponBox *best=nullptr;float fraction=limit;
 CBaseEntity *entity=nullptr;
 while((entity=UTIL_FindEntityInSphere(entity,p->pev->origin,kDistance))!=nullptr)
 {
  if(!FClassnameIs(entity->pev,"weaponbox"))continue;
  auto *b=static_cast<CWeaponBox *>(entity);if(!Item(b)||b->pev->solid!=SOLID_TRIGGER)continue;
  TraceResult hit{};UTIL_TraceModel(start,end,0,b->edict(),&hit);
  if(debug.value)ALERT(at_console,"oc2pickup_candidate: box=%d weapon=%d fraction=%.4f startsolid=%d\n",b->entindex(),Item(b)->m_iId,hit.flFraction,int(hit.fStartSolid));
  if(!hit.fStartSolid&&hit.flFraction>=0&&hit.flFraction<fraction){best=b;fraction=hit.flFraction;}
 }
 if(!best)return false;
 const int slot=p->entindex(),userid=GETPLAYERUSERID(p->edict());if(slot<1||slot>32)return false;
 auto &ps=players[slot];if(ps.userid!=userid){ps=PlayerState();ps.userid=userid;}
 if(gpGlobals->time<ps.next)return true;ps.next=gpGlobals->time+kCooldown;
 auto &state=State(best);
 if(state.claimant){Message(p,u8"\u8be5\u6b66\u5668\u6b63\u5728\u88ab\u62fe\u53d6");PickupLog("busy",p,best);return true;}
 if(state.dropper==p&&state.dropUserid==userid&&gpGlobals->time-state.dropped<1.5f){Message(p,u8"\u8bf7\u7a0d\u540e\u62fe\u53d6\u521a\u4e22\u4e0b\u7684\u6b66\u5668");PickupLog("self_grace",p,best);return true;}
 if(!(best->pev->flags&FL_ONGROUND)||!Eligible(p,best)){Message(p,u8"\u5f53\u524d\u65e0\u6cd5\u62fe\u53d6\u8be5\u6b66\u5668");PickupLog("ineligible",p,best);return true;}
 for(auto &s:boxes)if(s.claimant==p)Release(s,"replaced",false);
 state.claimant=p;state.userid=userid;state.team=p->m_iTeam;state.started=gpGlobals->time;
 OC2_WeaponPhysicsStop(best);PickupLog("start",p,best);return true;
}
void OC2_PickupFrame()
{
 Init();
 for(size_t i=0;i<boxes.size();)
 {
  auto *b=boxes[i].box.Get<CWeaponBox>();
  if(!Valid(b)){boxes.erase(boxes.begin()+i);continue;}
  if(boxes[i].landingSound && (b->pev->flags&FL_ONGROUND))
  {boxes[i].landingSound=false;PickupSound(b);}
  auto *p=boxes[i].claimant.Get<CBasePlayer>();
  if(boxes[i].userid)
  {
   if(!p||!p->IsAlive()||p->m_iTeam!=boxes[i].team||GETPLAYERUSERID(p->edict())!=boxes[i].userid||p->GetObserverMode()!=OBS_NONE||gpGlobals->time-boxes[i].started>kTimeout||!Eligible(p,b)||p->m_bIsDefusing||OC2_HostageGrabbingBy(p)||OC2_HostageCarriedBy(p))
   {Release(boxes[i],"cancel",p!=nullptr);++i;continue;}
   if(!ClearLine(p->GetGunPosition(),b->pev->origin,p)){Release(boxes[i],"blocked",true);++i;continue;}
   const Vector delta=p->pev->origin-b->pev->origin;const float distance=delta.Length();
   if(!std::isfinite(distance)||distance>kDistance+32){Release(boxes[i],"range",true);++i;continue;}
   if(distance<kDeliver)
   {
    if(!ClearLine(b->pev->origin,p->pev->origin,p)){Release(boxes[i],"blocked_delivery",true);++i;continue;}
    // Deliver may append the old gun's box and invalidate vector references.
    boxes[i].claimant=nullptr;boxes[i].userid=0;b->pev->velocity=g_vecZero;
    Deliver(p,b);++i;continue;
   }
   const Vector velocity=delta*(kSpeed/distance);
   const float dt=Q_min(.05f,Q_max(.001f,gpGlobals->frametime));
   if(!ClearLine(b->pev->origin,b->pev->origin+velocity*dt,b))Release(boxes[i],"blocked",true);
   else {b->pev->flags&=~FL_ONGROUND;b->pev->velocity=velocity;}
  }
  ++i;
 }
}
