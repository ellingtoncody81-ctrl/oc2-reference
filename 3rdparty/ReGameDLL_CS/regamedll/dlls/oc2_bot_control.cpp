#include "precompiled.h"
#include "oc2_bot_control.h"
#include "oc2_item_server.h"
#include "oc2_weapon_pickup.h"
#include "hostage/hostage.h"
void UpdateClientEffects(CBasePlayer *observer,int oldMode);

// Move a combat body between client entities; identity/account history and
// input ownership stay on each connection. No Deploy/Touch/Spawn side effects.
void OC2_ItemsRestoreBody(CBasePlayer *from,CBasePlayer *to) {
 to->RemoveAllItems(TRUE);
#define BODY(field) to->pev->field=from->pev->field
 BODY(origin);BODY(velocity);BODY(basevelocity);BODY(angles);BODY(v_angle);
 BODY(view_ofs);BODY(punchangle);BODY(health);BODY(max_health);BODY(armorvalue);
 BODY(takedamage);BODY(solid);BODY(movetype);BODY(gravity);BODY(friction);
 BODY(maxspeed);BODY(groundentity);BODY(waterlevel);BODY(watertype);
 BODY(air_finished);BODY(dmg);BODY(dmgtime);BODY(radsuit_finished);
 BODY(teleport_time);BODY(bInDuck);BODY(flDuckTime);BODY(flSwimTime);BODY(flTimeStepSound);
 BODY(fuser1);BODY(fuser2);BODY(fuser3);BODY(fuser4);BODY(weapons);
 BODY(viewmodel);BODY(weaponmodel);BODY(weaponanim);BODY(fov);BODY(body);BODY(skin);
  BODY(sequence);BODY(gaitsequence);BODY(frame);BODY(animtime);BODY(framerate);
#undef BODY
 const int movement=FL_DUCKING|FL_ONGROUND|FL_INWATER|FL_WATERJUMP|FL_ONTRAIN;
 to->pev->flags=(to->pev->flags&~(movement|FL_SPECTATOR|FL_DORMANT))|(from->pev->flags&movement);
 to->pev->effects=from->pev->effects&~EF_NODRAW;
 to->pev->deadflag=DEAD_NO;to->pev->fixangle=1;to->pev->button=to->pev->oldbuttons=0;
 to->pev->iuser1=to->pev->iuser2=to->pev->iuser3=0;
 to->m_hObserverTarget=nullptr;to->m_bWasFollowing=false;
 to->SetThink(nullptr);to->pev->nextthink=-1;
 SET_MODEL(to->edict(),STRING(from->pev->model));
 UTIL_SetSize(to->pev,from->pev->mins,from->pev->maxs);
 UTIL_SetOrigin(to->pev,from->pev->origin);
#define STATE(field) to->field=from->field
 STATE(m_iKevlar);STATE(m_iAccount);STATE(m_iModelName);STATE(m_modelIndexPlayer);
 STATE(m_bHasPrimary);STATE(m_bHasC4);STATE(m_bHasDefuser);STATE(m_bHasNightVision);STATE(m_bNightVisionOn);
 STATE(m_bOwnsShield);STATE(m_bShieldDrawn);STATE(m_bIsVIP);STATE(m_bCanShoot);
 STATE(m_flNextAttack);STATE(m_flVelocityModifier);STATE(m_flFallVelocity);
 STATE(m_iFOV);STATE(m_iLastZoom);STATE(m_bResumeZoom);STATE(m_flLastFired);
 STATE(m_blindUntilTime);STATE(m_blindStartTime);STATE(m_blindHoldTime);STATE(m_blindFadeTime);STATE(m_blindAlpha);
 STATE(m_afPhysicsFlags);STATE(m_flOc2WeaponAnimStart);STATE(m_Activity);STATE(m_IdealActivity);
 STATE(m_flFlinchTime);STATE(m_lastDamageAmount);
 STATE(m_bReceivesNoMoneyNextRound);STATE(m_bitsDamageType);STATE(m_tbdPrev);
 STATE(m_idrowndmg);STATE(m_idrownrestored);
 STATE(m_flEjectBrass);STATE(m_iWeaponVolume);STATE(m_iWeaponFlash);
#undef STATE
 Q_memcpy(to->m_szAnimExtention,from->m_szAnimExtention,sizeof(to->m_szAnimExtention));
 Q_memcpy(to->m_rgAmmo,from->m_rgAmmo,sizeof(to->m_rgAmmo));
 Q_memcpy(to->m_rgItems,from->m_rgItems,sizeof(to->m_rgItems));
 Q_memcpy(to->m_rgbTimeBasedDamage,from->m_rgbTimeBasedDamage,sizeof(to->m_rgbTimeBasedDamage));
 to->m_afPhysicsFlags&=~PFLAG_OBSERVER;to->m_iHideHUD=0;to->m_fDeadTime=0;
 to->m_bNotKilled=true;to->m_bKilledByBomb=to->m_bKilledByGrenade=false;
 to->m_afButtonLast=to->m_afButtonPressed=to->m_afButtonReleased=0;
 to->m_pActiveItem=from->m_pActiveItem;to->m_pLastItem=from->m_pLastItem;
 for(int slot=0;slot<MAX_ITEM_TYPES;++slot) {
  to->m_rgpPlayerItems[slot]=from->m_rgpPlayerItems[slot];from->m_rgpPlayerItems[slot]=nullptr;
  for(auto *item=to->m_rgpPlayerItems[slot];item;item=item->m_pNext) {
   item->m_pPlayer=to;item->pev->owner=item->pev->aiment=to->edict();
   // The entity, its complete subclass state and serial survive.
   item->m_bOc2Transferred=true;
   if(item->IsWeapon()){auto *w=static_cast<CBasePlayerWeapon *>(item);w->m_iClientClip=-1;w->m_iClientWeaponState=-1;}
  }
 }
 from->m_pActiveItem=from->m_pLastItem=from->m_pClientActiveItem=nullptr;
 Q_memset(from->m_rgAmmo,0,sizeof(from->m_rgAmmo));
 from->m_bHasPrimary=from->m_bHasC4=from->m_bHasDefuser=from->m_bHasNightVision=from->m_bNightVisionOn=false;
 from->m_bOwnsShield=from->m_bShieldDrawn=false;
 from->pev->weapons=from->pev->viewmodel=from->pev->weaponmodel=0;
 from->pev->health=0;from->pev->deadflag=DEAD_DEAD;from->pev->effects|=EF_NODRAW;
 from->pev->solid=SOLID_NOT;from->pev->takedamage=DAMAGE_NO;from->pev->movetype=MOVETYPE_NONE;
 from->pev->velocity=g_vecZero;from->m_bNotKilled=false;
 from->m_bitsDamageType=0;Q_memset(from->m_rgbTimeBasedDamage,0,sizeof(from->m_rgbTimeBasedDamage));
 from->m_idrowndmg=from->m_idrownrestored=0;
 from->SetThink(nullptr);from->pev->nextthink=-1;
}

namespace {
struct Control {
 EHANDLE human,bot;
 int userid=0,botUserid=0,money=0;
 TeamName team=UNASSIGNED;
 ModelName model=MODEL_UNASSIGNED;
 string_t modelPath=0;
 bool active=false;
 bool noMoney=false;
};
Control controls[33];
int message=0;
cvar_t takeover={"bot_controllable","1",FCVAR_SERVER,1};
cvar_t debug={"oc2_control_debug","0",0,0};
const char *const grenadeAmmo[]={"HEGrenade","Flashbang","SmokeGrenade","Molotov"};
int Reason(CBasePlayer *p,CBasePlayer *b) {
 if(!p||p->IsBot()||p->has_disconnected||p->m_iJoiningState!=JOINED||p->IsAlive()||
    p->GetObserverMode()==OBS_NONE||(p->m_iTeam!=CT&&p->m_iTeam!=TERRORIST))return 1;
 if(takeover.value==0||g_pGameRules->IsGameOver()||CSGameRules()->m_bRoundTerminating||g_pGameRules->IsFreezePeriod())return 2;
 if(!b||!b->IsBot()||b->has_disconnected||!b->IsAlive()||b->GetObserverMode()!=OBS_NONE||
    b->m_iTeam!=p->m_iTeam||OC2_BotParked(b))return 3;
  if(b->m_bIsVIP||b->m_bIsDefusing||OC2_HostageGrabbingBy(b)||OC2_HostageCarriedBy(b)||b->m_pTank)return 4;
  // Releasing another connection's attack input must not throw its primed grenade.
  if(b->m_pActiveItem&&OC2_GrenadeWeapon(b->m_pActiveItem->m_iId)&&
     static_cast<CBasePlayerWeapon *>(b->m_pActiveItem)->m_flStartThrow>0)return 4;
 if(b->m_pActiveItem&&b->m_pActiveItem->m_iId==WEAPON_C4&&static_cast<CC4 *>(b->m_pActiveItem)->m_bStartedArming)return 4;
 return 0;
}
void Send(CBasePlayer *p,int op,int bot,int reason=0,int weapon=0,int animation=0,int elapsed=0) {
 if(!message||!p||p->IsBot())return;
 MESSAGE_BEGIN(MSG_ONE,message,nullptr,p->pev);
 WRITE_BYTE(1);WRITE_BYTE(op);WRITE_BYTE(bot);WRITE_BYTE(reason);
 WRITE_BYTE(weapon);WRITE_BYTE(animation);WRITE_SHORT(elapsed);
 MESSAGE_END();
}
void SpectatorFlag(CBasePlayer *p,int value) {
 MESSAGE_BEGIN(MSG_ALL,gmsgSpectator);WRITE_BYTE(p->entindex());WRITE_BYTE(value);MESSAGE_END();
 for(int i=1;i<=gpGlobals->maxClients;++i){auto *peer=UTIL_PlayerByIndex(i);if(peer&&!peer->IsBot()&&!peer->has_disconnected)p->SetScoreAttrib(peer);}
}
void End(Control &c,bool returnBody) {
 if(!c.active)return;
 CBasePlayer *p=static_cast<CBasePlayer *>((CBaseEntity *)c.human);
 CBasePlayer *b=static_cast<CBasePlayer *>((CBaseEntity *)c.bot);
 if(p&&(GETPLAYERUSERID(p->edict())!=c.userid||p->has_disconnected))p=nullptr;
 if(b&&(GETPLAYERUSERID(b->edict())!=c.botUserid||b->has_disconnected))b=nullptr;
 c.active=false;
 if(b) {
  if(p){b->m_iAccount=p->m_iAccount;b->m_bReceivesNoMoneyNextRound=p->m_bReceivesNoMoneyNextRound;}
  if(p&&returnBody&&p->IsAlive()&&p->m_iTeam==c.team&&b->m_iTeam==c.team) {
   // Rehome the same surviving body before any normal round respawn runs.
   // Implemented below by swapping the combat state, with no Kill/Spawn.
   OC2_ItemsRestoreBody(p,b);
  }
  b->m_bNotKilled=returnBody&&b->IsAlive();
  SpectatorFlag(b,b->IsAlive()?0:1);
 }
 if(p&&GETPLAYERUSERID(p->edict())==c.userid) {
  p->m_iAccount=c.money;p->m_iModelName=c.model;
  p->m_bReceivesNoMoneyNextRound=c.noMoney;
  if(c.modelPath)SET_MODEL(p->edict(),STRING(c.modelPath));
  p->m_bNotKilled=false;
  OC2_ItemsControlGlove(p,nullptr);
  p->m_iClientHealth=p->m_iClientBattery=-1;
  p->AddAccount(0,RT_NONE,false);
  SpectatorFlag(p,p->IsAlive()?0:1);
  Send(p,2,0);
 }
 c=Control();
}
}

bool OC2_GrenadeWeapon(int id) {return id==WEAPON_HEGRENADE||id==WEAPON_FLASHBANG||id==WEAPON_SMOKEGRENADE||id==WEAPON_MOLOTOV;}
int OC2_GrenadeCount(CBasePlayer *p) {
 int total=0;if(!p)return 0;
 for(const char *name:grenadeAmmo){const int i=p->GetAmmoIndex(name);if(i>=0&&i<MAX_AMMO_SLOTS)total+=Q_max(0,p->m_rgAmmo[i]);}
 return total;
}
int OC2_GrenadeRoom(CBasePlayer *p,const char *ammo) {
 for(const char *name:grenadeAmmo)if(!Q_stricmp(name,ammo))return Q_max(0,3-OC2_GrenadeCount(p));
 return MAX_AMMO_SLOTS*1000;
}
bool OC2_BotParked(CBasePlayer *p) {
 if(!p)return false;
 for(const auto &c:controls)if(c.active&&c.bot==p&&GETPLAYERUSERID(p->edict())==c.botUserid)return true;
 return false;
}
bool OC2_ControlledBot(CBasePlayer *p) {
 if(!p)return false;
 for(const auto &c:controls)if(c.active&&c.human==p&&GETPLAYERUSERID(p->edict())==c.userid)return true;
 return false;
}
void OC2_BotControlTeamAward(int team,int amount) {
 for(int i=1;i<=gpGlobals->maxClients;++i){
  auto *p=UTIL_PlayerByIndex(i);
  if(!UTIL_IsValidPlayer(p)||p->has_disconnected||p->m_iTeam!=team||OC2_BotParked(p))continue;
  p->AddAccount(amount,RT_HOSTAGE_TOOK);
  for(auto &c:controls)if(c.active&&c.human==p&&GETPLAYERUSERID(p->edict())==c.userid)
   c.money=Q_max(0,Q_min(int(maxmoney.value),c.money+amount));
 }
}
void OC2_BotControlRegister(){message=REG_USER_MSG("OC2Control",8);CVAR_REGISTER(&takeover);CVAR_REGISTER(&debug);}
void OC2_BotControlReset(){for(auto &c:controls)c=Control();}
void OC2_BotControlRound(){for(auto &c:controls)End(c,true);}
void OC2_BotControlDisconnect(CBasePlayer *p){for(auto &c:controls)if(c.active&&(c.human==p||c.bot==p))End(c,c.human==p);}
void OC2_BotControlSpawn(CBasePlayer *p){for(auto &c:controls)if(c.active&&(c.human==p||c.bot==p))End(c,true);}
void OC2_BotControlDeath(CBasePlayer *p){for(auto &c:controls)if(c.active&&c.human==p&&!p->IsAlive())End(c,false);}
void OC2_BotControlTeam(CBasePlayer *p){for(auto &c:controls)if(c.active&&(c.human==p||c.bot==p))End(c,true);}
void OC2_BotControlFrame() {
 for(auto &c:controls)if(c.active) {
  auto *p=static_cast<CBasePlayer *>((CBaseEntity *)c.human);
  auto *b=static_cast<CBasePlayer *>((CBaseEntity *)c.bot);
  if(!p||!b||p->has_disconnected||b->has_disconnected||p->m_iTeam!=c.team||b->m_iTeam!=c.team||
     GETPLAYERUSERID(p->edict())!=c.userid||GETPLAYERUSERID(b->edict())!=c.botUserid)End(c,true);
 }
}
void OC2_BotControlStatus(CBasePlayer *p) {
 static int userid[33]={},target[33]={},reasons[33]={};static float sent[33]={};
 CBasePlayer *b=p->m_hObserverTarget;
 const int i=p->entindex(),id=GETPLAYERUSERID(p->edict()),bot=b?b->entindex():0,reason=Reason(p,b);
 if(userid[i]==id&&target[i]==bot&&reasons[i]==reason&&sent[i]>gpGlobals->time&&sent[i]<gpGlobals->time+1.1f)return;
 userid[i]=id;target[i]=bot;reasons[i]=reason;sent[i]=gpGlobals->time+1;
 Send(p,0,bot,reason);
}
bool OC2_BotTakeover(CBasePlayer *p) {
 CBasePlayer *b=p?static_cast<CBasePlayer *>(p->m_hObserverTarget):nullptr;
 const int reason=Reason(p,b);
 if(reason){Send(p,0,b?b->entindex():0,reason);return false;}
 auto &c=controls[p->entindex()];End(c,false);
 c.human=p;c.bot=b;c.userid=GETPLAYERUSERID(p->edict());c.botUserid=GETPLAYERUSERID(b->edict());
 c.money=p->m_iAccount;c.model=p->m_iModelName;c.modelPath=p->pev->model;c.team=p->m_iTeam;c.active=true;c.noMoney=p->m_bReceivesNoMoneyNextRound;
 OC2_ItemsControlGlove(p,b);
 OC2_ItemsRestoreBody(b,p);
 // A parked bot is dead/invisible for gameplay but retains its connection,
 // team, score and AI object. It never emits DeathMsg or drops a second kit.
 SpectatorFlag(p,0);SpectatorFlag(b,1);
 p->m_iClientHealth=p->m_iClientBattery=-1;p->m_iClientHideHUD=-1;
 p->m_pClientActiveItem=nullptr;p->m_fWeapon=FALSE;
 for(auto &v:p->m_rgAmmoLast)v=-1;
 p->AddAccount(0,RT_NONE,false);p->SendFOV(int(p->pev->fov));
 MESSAGE_BEGIN(MSG_ONE,gmsgNVGToggle,nullptr,p->pev);WRITE_BYTE(p->m_bNightVisionOn?1:0);MESSAGE_END();
 if(p->m_bHasC4)p->SetBombIcon();
 const float end=p->m_blindStartTime+p->m_blindHoldTime+p->m_blindFadeTime;
 if(end>gpGlobals->time&&p->m_blindFadeTime>0) {
  const float hold=Q_max(0.0f,p->m_blindStartTime+p->m_blindHoldTime-gpGlobals->time);
  const float fade=Q_min(p->m_blindFadeTime,end-gpGlobals->time);
  UTIL_ScreenFade(p,Vector(255,255,255),fade,hold,int(p->m_blindAlpha*fade/p->m_blindFadeTime));
 }else UTIL_ScreenFade(p,Vector(0,0,0),0.001f);
 const int weapon=p->m_pActiveItem?p->m_pActiveItem->m_iId:0;
 const int elapsed=clamp(int((gpGlobals->time-p->m_flOc2WeaponAnimStart)*1000),0,30000);
 Send(p,1,b->entindex(),0,weapon,p->pev->weaponanim,elapsed);
 ClientPrint(p->pev,HUD_PRINTCENTER,u8"\u5df2\u63a5\u7ba1\u673a\u5668\u4eba");
 p->UpdateClientData();
 if(debug.value)ALERT(at_console,"oc2control: event=takeover player=%d bot=%d hp=%.0f armor=%.0f money=%d weapon=%d grenades=%d elapsed=%d\n",p->entindex(),b->entindex(),p->pev->health,p->pev->armorvalue,p->m_iAccount,weapon,OC2_GrenadeCount(p),elapsed);
 return true;
}
bool OC2_BotControlCommand(CBasePlayer *p,const char *command) {
 if(FStrEq(command,"oc2_control_test")) {
  if(!p||p->entindex()!=1||IS_DEDICATED_SERVER()||CVAR_GET_FLOAT("sv_cheats")==0||CMD_ARGC()!=3)return true;
  const int i=atoi(CMD_ARGV(2));if(i<1||i>gpGlobals->maxClients)return true;
  auto *q=UTIL_PlayerByIndex(i);if(!q||!q->IsAlive())return true;
   if(FStrEq(CMD_ARGV(1),"kill")){q->Kill();return true;}
   if(FStrEq(CMD_ARGV(1),"prime")&&q->m_pActiveItem&&OC2_GrenadeWeapon(q->m_pActiveItem->m_iId)) {
    auto *w=static_cast<CBasePlayerWeapon *>(q->m_pActiveItem);
    w->m_flNextPrimaryAttack=0;w->PrimaryAttack();
    // Freeze this legal action for the local negative-eligibility fixture.
    if(w->m_flStartThrow>0)q->m_flNextAttack=60;
    return true;
   }
  if(FStrEq(CMD_ARGV(1),"setup")) {
   q->pev->health=37;q->pev->armorvalue=63;q->m_iKevlar=ARMOR_VESTHELM;
   q->m_bHasNightVision=true;q->m_bNightVisionOn=false;q->m_iAccount=2345;
   if(q->m_iTeam==CT)q->GiveDefuser();q->AddAccount(0,RT_NONE,false);
  } else if(FStrEq(CMD_ARGV(1),"reload")&&q->m_pActiveItem&&q->m_pActiveItem->IsWeapon()) {
   auto *w=static_cast<CBasePlayerWeapon *>(q->m_pActiveItem);const int ammo=w->PrimaryAmmoIndex();
   if(ammo>=0&&ammo<MAX_AMMO_SLOTS){w->m_iClip=7;q->m_rgAmmo[ammo]=47;q->m_flNextAttack=0;w->m_flNextPrimaryAttack=0;w->Reload();}
  }
  return true;
 }
 if(FStrEq(command,"spec_player")) {
  if(p->GetObserverMode()==OBS_NONE||!p->CanSwitchObserverModes()||CMD_ARGC()!=2)return true;
  const int slot=atoi(CMD_ARGV(1));auto *target=p->Observer_IsValidTarget(slot,GetForceCamera(p)!=CAMERA_MODE_SPEC_ANYONE&&p->m_iTeam!=SPECTATOR);
  if(!target||!target->IsAlive())return true;
  if(p->m_flNextFollowTime>gpGlobals->time)return true;
  p->m_flNextFollowTime=gpGlobals->time+.25f;
  p->m_hObserverTarget=target;
  if(p->GetObserverMode()==OBS_ROAMING)p->Observer_SetMode(OBS_IN_EYE);
  p->pev->iuser2=target->entindex();p->pev->iuser3=0;
  UTIL_SetOrigin(p->pev,target->pev->origin);
  MESSAGE_BEGIN(MSG_ONE,gmsgSpecHealth2,nullptr,p->pev);WRITE_BYTE(int(target->pev->health));WRITE_BYTE(target->entindex());MESSAGE_END();
  UpdateClientEffects(p,p->GetObserverMode());
  return true;
 }
 if(FStrEq(command,"bot_takeover")){OC2_BotTakeover(p);return true;}
 if(FStrEq(command,"spec_next")||FStrEq(command,"spec_prev")) {
  if(p->GetObserverMode()!=OBS_NONE&&p->CanSwitchObserverModes())p->Observer_FindNextPlayer(FStrEq(command,"spec_prev"));return true;
 }
 if(FStrEq(command,"spec_cycle")) {
  if(p->GetObserverMode()!=OBS_NONE&&p->CanSwitchObserverModes())p->Observer_SetMode(p->GetObserverMode()==OBS_IN_EYE?OBS_CHASE_FREE:p->GetObserverMode()==OBS_CHASE_FREE?OBS_ROAMING:OBS_IN_EYE);return true;
 }
 if(!FStrEq(command,"oc2_control_dump"))return false;
 for(int i=1;i<=gpGlobals->maxClients&&i<=32;++i) {
  auto *q=UTIL_PlayerByIndex(i);if(!q)continue;
  auto *w=q->m_pActiveItem&&q->m_pActiveItem->IsWeapon()?static_cast<CBasePlayerWeapon *>(q->m_pActiveItem):nullptr;
  ALERT(at_console,"oc2control_state: player=%d userid=%d alive=%d parked=%d team=%d mode=%d target=%d hp=%.0f armor=%.0f kevlar=%d kit=%d nvg=%d c4=%d money=%d grenades=%d weapon=%d entity=%d serial=%d clip=%d reload=%d next=%.3f primary=%.3f idle=%.3f anim=%d origin=%.2f,%.2f,%.2f\n",i,GETPLAYERUSERID(q->edict()),q->IsAlive(),OC2_BotParked(q),q->m_iTeam,q->GetObserverMode(),q->pev->iuser2,q->pev->health,q->pev->armorvalue,q->m_iKevlar,q->m_bHasDefuser,q->m_bHasNightVision,q->m_bHasC4,q->m_iAccount,OC2_GrenadeCount(q),w?w->m_iId:0,w?w->entindex():0,w?w->m_oc2Appearance.serial:0,w?w->m_iClip:0,w?w->m_fInReload:0,q->m_flNextAttack,w?w->m_flNextPrimaryAttack:0,w?w->m_flTimeWeaponIdle:0,q->pev->weaponanim,q->pev->origin.x,q->pev->origin.y,q->pev->origin.z);
 }
 return true;
}
