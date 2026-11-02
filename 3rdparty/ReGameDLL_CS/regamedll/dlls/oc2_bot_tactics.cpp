#include "precompiled.h"
#include "oc2_bot_tactics.h"
#include "oc2_bot_control.h"
#include "oc2_inferno.h"
#include "oc2_smoke.h"
#include "oc2_bot_trajectory.h"
#include "wpn_shared/oc2_grenade_throw.h"
#include "hostage/hostage.h"
#include <cmath>
#include <utility>
#include <algorithm>

namespace
{
struct Shot { float yaw=0,pitch=0,strength=1; };
struct Plan
{
    int userid=0,team=0,id=0,phase=0,threatUser=0,cursor=0,count=0,walls=0;
    Vector target,aim,effect,origin,routeFire;
    float next=0,cooldown=0,start=0,hold=0,strength=1,knownUntil=0,routeUntil=0;
    bool emergency=false,released=false;
    Shot shots[24],shot;
};
Plan plans[33];
struct Reservation { int userid=0,team=0,id=0;Vector effect;float until=0;bool released=false; };
Reservation reservations[33];
struct Flight { EHANDLE entity;int team=0,id=0,player=0,walls=0;bool bot=false,planned=false;Vector predicted;float until=0; };
Flight flights[64];
float plannerFrame=-1,friendlyFlashNext[3]{},lastResetTime=-1;
int plannerLast=0,plannerUsed=0;
EHANDLE fireHullProbe;
float physicsStep=.01f,physicsTime=-1,physicsSamples[16]{};
int physicsCount=0,physicsCursor=0;
int protectedUtility=0,reservedUtility=0,bankThrows=0,arcTraces=0,flashLimited=0,flashSuppressed=0;
int purchases=0,throws=0,cancels=0,zeusUses=0;
struct BuyChoice {int userid=0,team=0;bool rolled=false,grenade=false,zeus=false,completed=false;};
BuyChoice buyChoices[33];
BuyChoice& UtilityBuyChoice(CCSBot *bot)
{
 auto& choice=buyChoices[bot->entindex()];const int userid=GETPLAYERUSERID(bot->edict());
 if(choice.userid!=userid||choice.team!=bot->m_iTeam){choice={};choice.userid=userid;choice.team=bot->m_iTeam;}
 if(!choice.rolled)
 {
  choice.rolled=true;choice.grenade=RANDOM_LONG(0,99)<20;choice.zeus=RANDOM_LONG(0,99)<20;
  if(CVAR_GET_FLOAT("developer")>0)ALERT(at_console,"oc2buy_roll: player=%d userid=%d grenade=%d zeus=%d chance=20\n",bot->entindex(),userid,int(choice.grenade),int(choice.zeus));
 }
 return choice;
}
int decisions=0,noOptions=0,unsafe=0,blockedArc=0,chanceSkipped=0,started=0;
CBasePlayerWeapon *Find(CCSBot *bot,int id,int slot)
{
    for(auto *item=bot->m_rgpPlayerItems[slot];item;item=item->m_pNext)
    {
        auto *w=static_cast<CBasePlayerWeapon *>(item);
        if(w->m_iId==id && (w->m_iPrimaryAmmoType<0 || bot->m_rgAmmo[w->m_iPrimaryAmmoType]>0 || w->m_iClip>0))return w;
    }
    return nullptr;
}
bool ClearLine(const Vector &a,const Vector &b,CBaseEntity *ignore=nullptr)
{
    TraceResult tr;UTIL_TraceLine(a,b,ignore_monsters,ignore?ignore->edict():nullptr,&tr);
    return !tr.fStartSolid&&!tr.fAllSolid&&tr.flFraction==1.0f;
}
bool FriendsSafe(CCSBot *bot,int id,const Vector &target)
{
    const float radius=id==WEAPON_HEGRENADE?400:id==WEAPON_MOLOTOV?210:3000;
    for(int i=1;i<=gpGlobals->maxClients;++i)
    {
        auto *p=UTIL_PlayerByIndex(i);
        if(!UTIL_IsValidPlayer(p)||p->has_disconnected||!p->IsAlive()||p->m_iTeam!=bot->m_iTeam)continue;
        const Vector eye=p->EyePosition();
        // Reserve a short movement envelope as well as the current position.
        const Vector predicted=eye+p->pev->velocity*.5f;
        const Vector delta=target-eye;
        if(delta.LengthSquared()>radius*radius && (target-predicted).LengthSquared()>radius*radius)continue;
        if(!ClearLine(target+Vector(0,0,2),eye,bot)&&!ClearLine(target+Vector(0,0,2),predicted,bot))continue;
        if(id!=WEAPON_FLASHBANG)return false;
        if(delta.LengthSquared()<450*450)return false;
        if(p==bot)continue; // turns away; actual friendly exposure is capped too
        UTIL_MakeVectors(p->pev->v_angle);
        if(DotProduct(delta.Normalize(),gpGlobals->v_forward)>-0.2f)return false;
        if(bot->m_iTeam<1||bot->m_iTeam>2||gpGlobals->time<friendlyFlashNext[bot->m_iTeam])return false;
    }
    return true;
}
bool BallisticAim(CCSBot *bot,const Vector &target,Vector &aim,bool smoke=false,float strength=1)
{
    const Vector eye=bot->EyePosition();
    const Vector base=eye+Vector(0,0,-12*(1-strength));
    Vector delta=target+Vector(0,0,2)-base;
    const float range=delta.Length2D(),speed=675.0f*(.3f+.7f*strength),gravity=CVAR_GET_FLOAT("sv_gravity")*0.4f;
    if(range<(smoke?32:180) || range>1000 || gravity<=0)return false;
    const float v2=speed*speed;
    float theta=0;
    // Match the real 16-unit launch offset, including its vertical component.
    for(int i=0;i<3;++i)
    {
        const float distance=range-16*std::cos(theta),height=delta.z-16*std::sin(theta);
        const float discriminant=v2*v2-gravity*(gravity*distance*distance+2*height*v2);
        if(discriminant<0 || distance<=0)return false;
        theta=std::atan((v2-std::sqrt(discriminant))/(gravity*distance));
    }
    const float desired=-theta*180/float(M_PI);
    const float viewPitch=desired<=-10?(desired+10)/0.8888889f:(desired+10)/1.1111111f;
    aim=target;aim.z=eye.z-std::tan(viewPitch*float(M_PI)/180)*range;
    Vector velocity(delta.x/range*speed*std::cos(theta),delta.y/range*speed*std::cos(theta),speed*std::sin(theta));
    const float flight=(range-16*std::cos(theta))/(speed*std::cos(theta));
    const Vector source=base+velocity*(16/speed);
    if(!ClearLine(base,source,bot))return false;
    Vector previous=source;
    for(int i=1;i<=12;++i)
    {
        const float t=flight*i/12;
        const Vector point=source+velocity*t-Vector(0,0,0.5f*gravity*t*t);
        TraceResult tr;UTIL_TraceLine(previous,point,ignore_monsters,bot->edict(),&tr);
        if(tr.fStartSolid || tr.fAllSolid || (tr.flFraction<0.98f && i<12))return false;
        previous=point;
    }
    return true;
}
bool GroundTarget(CCSBot *bot,const Vector &point,Vector &ground)
{
    TraceResult tr;
    UTIL_TraceLine(point+Vector(0,0,16),point-Vector(0,0,384),ignore_monsters,bot->edict(),&tr);
    if(tr.fStartSolid||tr.fAllSolid||tr.flFraction==1||tr.vecPlaneNormal.z<0.7f)return false;
    ground=tr.vecEndPos+Vector(0,0,2);
    return true;
}
bool Safe(CCSBot *bot,int id,const Vector &target)
{
    const float range=(target-bot->pev->origin).Length();
    if(range<(id==WEAPON_SMOKEGRENADE?32:180) || range>1000)return false;
    const auto &plan=plans[bot->entindex()];
    if(id==WEAPON_SMOKEGRENADE)
    {
        if(OC2_SmokeNearTeam(target+Vector(0,0,24),100,0))return false;
        if(!plan.emergency&&OC2_InfernoNearTeam(target,170,bot->m_iTeam))
        {++protectedUtility;return false;}
    }
    if(id==WEAPON_HEGRENADE&&OC2_SmokeNearTeam(target+Vector(0,0,2),370,bot->m_iTeam))
    {++protectedUtility;return false;}
    if(id==WEAPON_MOLOTOV&&(OC2_SmokeNearTeam(target+Vector(0,0,24),160,0)||OC2_InfernoNearTeam(target,100,0)))
    {++protectedUtility;return false;}
    for(int i=1;i<=gpGlobals->maxClients;++i)
    {
        if(i==bot->entindex())continue;
        auto &r=reservations[i];
        if(r.until<=gpGlobals->time||r.team!=bot->m_iTeam)continue;
        auto *owner=UTIL_PlayerByIndex(i);
        if(!r.released&&(!UTIL_IsValidPlayer(owner)||owner->has_disconnected||!owner->IsAlive()
            ||GETPLAYERUSERID(owner->edict())!=r.userid||owner->m_iTeam!=r.team||OC2_BotParked(owner)))
        {r={};continue;}
        const bool conflict=r.id==id||(id==WEAPON_SMOKEGRENADE&&r.id==WEAPON_MOLOTOV)
            ||(id==WEAPON_MOLOTOV&&r.id==WEAPON_SMOKEGRENADE)
            ||(id==WEAPON_HEGRENADE&&r.id==WEAPON_SMOKEGRENADE)
            ||(id==WEAPON_SMOKEGRENADE&&r.id==WEAPON_HEGRENADE);
        const float separation=(id==WEAPON_HEGRENADE||r.id==WEAPON_HEGRENADE)?500:300;
        if(conflict&&(target-r.effect).LengthSquared()<separation*separation)
        {++reservedUtility;return false;}
    }
    if(id==WEAPON_SMOKEGRENADE)return true;
    return FriendsSafe(bot,id,target);
}
Vector Forward(float pitch,float yaw)
{
    const float p=pitch*float(M_PI)/180,y=yaw*float(M_PI)/180;
    return Vector(std::cos(p)*std::cos(y),std::cos(p)*std::sin(y),-std::sin(p));
}
oc2_bot_arc::Result Predict(CCSBot *bot,int id,const Shot &shot,bool actualVelocity=false,
                           const Vector *sourceOverride=nullptr,const Vector *velocityOverride=nullptr)
{
    using namespace oc2_bot_arc;
    const auto vec=[](const Vector &v){return Vec(v.x,v.y,v.z);};
    const auto trace=[&](Vec a,Vec b)
    {
        TraceResult tr;const Vector from(a.x,a.y,a.z),to(b.x,b.y,b.z);
        if(id==WEAPON_MOLOTOV)
        {
            CBaseEntity *probe=fireHullProbe;
            if(!probe)
            {
                probe=GetClassPtr<CCSEntity>((CBaseEntity *)nullptr);
                if(!probe){Hit h;h.invalid=true;return h;}
                MAKE_STRING_CLASS("oc2_bot_trace_probe",probe->pev);
                probe->pev->solid=SOLID_NOT;probe->pev->movetype=MOVETYPE_NONE;probe->pev->effects=EF_NODRAW;
                UTIL_SetSize(probe->pev,Vector(-2,-2,-2),Vector(2,2,2));fireHullProbe=probe;
            }
            g_engfuncs.pfnTraceMonsterHull(probe->edict(),from,to,dont_ignore_monsters,bot->edict(),&tr);
        }
        else UTIL_TraceLine(from,to,dont_ignore_monsters,bot->edict(),&tr);
        Hit h;h.fraction=tr.flFraction;h.end=vec(tr.vecEndPos);h.normal=vec(tr.vecPlaneNormal);
        h.invalid=tr.fStartSolid||tr.fAllSolid||UTIL_PointContents(tr.vecEndPos)==CONTENTS_SKY
            ||UTIL_PointContents(tr.vecEndPos)==CONTENTS_WATER;
        // Door, glass, moving props and player contact invalidate a line-up.
        if(tr.flFraction<1&&tr.pHit&&ENTINDEX(tr.pHit)!=0)h.invalid=true;
        return h;
    };
    const Vector forward=Forward(oc2_grenade::Pitch(shot.pitch+bot->pev->punchangle.x),shot.yaw+bot->pev->punchangle.y);
    const Vector base=bot->EyePosition()+Vector(0,0,oc2_grenade::Lower(shot.strength));
    TraceResult launch;UTIL_TraceLine(base,base+forward*22,dont_ignore_monsters,bot->edict(),&launch);
    if(!sourceOverride&&(launch.fStartSolid||launch.fAllSolid))return {};
    const Vec source=sourceOverride?vec(*sourceOverride):vec(launch.vecEndPos-forward*6);
    const Vector velocity=velocityOverride?*velocityOverride:forward*oc2_grenade::Speed(shot.strength)+(actualVelocity?bot->pev->velocity*1.25f:g_vecZero);
    const auto kind=id==WEAPON_HEGRENADE?HE:id==WEAPON_FLASHBANG?Flash:id==WEAPON_SMOKEGRENADE?Smoke:Fire;
    auto out=oc2_bot_arc::Predict(source,vec(velocity),CVAR_GET_FLOAT("sv_gravity")*.4f,kind,trace,
        [&](Vec p){Vector ground;return GroundTarget(bot,Vector(p.x,p.y,p.z),ground)&&OC2_InfernoNearTeam(ground,70,0);},physicsStep);
    arcTraces+=out.traces;
    return out;
}
void AddShot(CCSBot *bot,Plan &p,const Vector &point,float strength,bool high)
{
    if(p.count>=24)return;
    const Vector delta=point-bot->EyePosition()-Vector(0,0,oc2_grenade::Lower(strength));
    const float range=delta.Length2D(),speed=oc2_grenade::Speed(strength),g=CVAR_GET_FLOAT("sv_gravity")*.4f;
    if(range<24||g<=0)return;
    const float v2=speed*speed,d=Q_max(8.f,range-16);
    const float discriminant=v2*v2-g*(g*d*d+2*delta.z*v2);
    if(discriminant<0)return;
    const float angle=-std::atan((v2+(high?1:-1)*std::sqrt(discriminant))/(g*d))*180/float(M_PI);
    Shot shot;shot.pitch=angle<=-10?(angle+10)/.8888889f:(angle+10)/1.1111111f;
    shot.yaw=std::atan2(delta.y,delta.x)*180/float(M_PI);shot.strength=strength;
    p.shots[p.count++]=shot;
}
void PrepareShots(CCSBot *bot,Plan &p)
{
    p.count=p.cursor=0;p.origin=bot->pev->origin;
    // Shorter first-contact distances compensate for timed grenades rolling
    // before their fuse expires; every candidate is simulated to its effect.
    for(float factor:{.75f,1.f,.55f})
        for(float strength:{.5f,1.f})AddShot(bot,p,bot->pev->origin+(p.target-bot->pev->origin)*factor,strength,false);
    AddShot(bot,p,p.target,1,true);
    const Vector direction=(p.target-bot->EyePosition()).Normalize();
    const Vector side(-direction.y,direction.x,0);
    // Reflect a desired target across nearby static wall planes, then verify
    // using Xash's inelastic bounce. Reflection alone is never accepted.
    for(float sign:{-1.f,1.f})
    {
        TraceResult tr;const Vector eye=bot->EyePosition();
        UTIL_TraceLine(eye,eye+side*(384*sign),ignore_monsters,bot->edict(),&tr);
        if(tr.fStartSolid||tr.flFraction==1||std::fabs(tr.vecPlaneNormal.z)>.2f||!tr.pHit||ENTINDEX(tr.pHit)!=0)continue;
        const Vector reflected=p.target-tr.vecPlaneNormal*(2*DotProduct(p.target-tr.vecEndPos,tr.vecPlaneNormal));
        for(float strength:{1.f,.5f})
            for(float factor:{1.f,.8f,1.2f})AddShot(bot,p,eye+(reflected-eye)*factor,strength,false);
    }
    const int original=p.count;
    for(int i=0;i<Q_min(4,original)&&p.count<24;++i)
    {
        Shot shot=p.shots[i];shot.pitch-=12;p.shots[p.count++]=shot;
    }
}
bool EffectUseful(CCSBot *bot,const Plan &p,const Vector &effect)
{
    const float tolerance=p.id==WEAPON_FLASHBANG?220:p.id==WEAPON_SMOKEGRENADE?115:140;
    if((effect-p.target).LengthSquared()>tolerance*tolerance)return false;
    if(p.knownUntil>0&&gpGlobals->time>p.knownUntil)return false;
    if(p.id==WEAPON_FLASHBANG&&!ClearLine(effect+Vector(0,0,1),p.target+Vector(0,0,48),bot))return false;
    if(p.id==WEAPON_HEGRENADE&&!ClearLine(effect+Vector(0,0,1),p.target+Vector(0,0,32),bot))return false;
    return true;
}
void Cancel(CCSBot *bot,Plan &p,const char *reason="state")
{
    if(CVAR_GET_FLOAT("developer")>0)ALERT(at_console,"oc2tactic_cancel: t=%.3f player=%d id=%d phase=%d active=%d reason=%s held=%.3f dt=%.5f\n",
        gpGlobals->time,bot->entindex(),p.id,p.phase,bot->GetActiveWeapon()?bot->GetActiveWeapon()->m_iId:0,reason,gpGlobals->time-p.start,gpGlobals->frametime);
    bot->ClearPrimaryAttack();bot->ClearLookAt();
    bot->StopRapidFire();
    bot->EquipBestWeapon(MUST_EQUIP);p.phase=0;p.cooldown=gpGlobals->time+1.5f;
    if(!reservations[bot->entindex()].released)reservations[bot->entindex()]={};
    ++cancels;
}
}
void OC2_BotTacticsReset()
{
    if(gpGlobals->time<lastResetTime)for(auto &time:friendlyFlashNext)time=0;
    lastResetTime=gpGlobals->time;
    for(auto &p:plans)p={};for(auto& choice:buyChoices)choice={};purchases=throws=cancels=zeusUses=0;
    for(auto &r:reservations)r={};for(auto &f:flights)f={};plannerFrame=-1;
    plannerLast=0;
    if(CBaseEntity *probe=fireHullProbe)UTIL_Remove(probe);
    fireHullProbe=static_cast<CBaseEntity *>(nullptr);
    protectedUtility=reservedUtility=bankThrows=arcTraces=0;
    decisions=noOptions=unsafe=blockedArc=chanceSkipped=started=0;
}
void OC2_BotTacticsFrame()
{
    // RunPlayerMove replaces globals->frametime with each bot's command msec.
    // Capture the real server tick clock before bot commands instead. A short
    // median rejects one slow render frame without assuming a fixed tickrate.
    const float now=gpGlobals->time;
    if(now<physicsTime){physicsCount=physicsCursor=0;physicsStep=.01f;}
    const float delta=now-physicsTime;physicsTime=now;
    if(delta>.001f&&delta<.1f)
    {
        physicsSamples[physicsCursor++%16]=delta;physicsCount=Q_min(16,physicsCount+1);
        float sorted[16];for(int i=0;i<physicsCount;++i)sorted[i]=physicsSamples[i];
        std::sort(sorted,sorted+physicsCount);physicsStep=sorted[physicsCount/2];
    }
    const float fixed=CVAR_GET_FLOAT("sv_fps");if(fixed>=10)physicsStep=1.f/(fixed-.01f);
}
bool OC2_BotUtilityAiming(CCSBot *bot)
{
    if(!bot||!bot->IsAlive()||bot->entindex()<1||bot->entindex()>32||OC2_BotParked(bot))return false;
    const auto &p=plans[bot->entindex()];
    return p.userid==GETPLAYERUSERID(bot->edict())&&p.team==bot->m_iTeam&&(p.phase==1||p.phase==3||p.phase==4);
}
void OC2_BotUtilityThrown(CGrenade *grenade,CBasePlayer *owner,int id)
{
    if(!grenade||!owner)return;
    Flight *slot=&flights[0];
    for(auto &f:flights)
    {if(!f.entity||f.until<=gpGlobals->time){slot=&f;break;}if(f.until<slot->until)slot=&f;}
    *slot={};slot->entity=grenade;slot->team=owner->m_iTeam;slot->bot=owner->IsBot();slot->id=id;slot->until=gpGlobals->time+22;slot->player=owner->entindex();
    if(!owner->IsBot()||owner->entindex()<1||owner->entindex()>32)return;
    auto &p=plans[owner->entindex()];
    if(!owner->IsAlive())
    {
        p.phase=0;auto &r=reservations[owner->entindex()];
        r.userid=GETPLAYERUSERID(owner->edict());r.team=owner->m_iTeam;r.id=id;r.effect=grenade->pev->origin;r.until=gpGlobals->time+3;r.released=true;
        return;
    }
    if(p.userid!=GETPLAYERUSERID(owner->edict())||p.id!=id||p.phase==0||p.phase==5)return;
    p.released=true;++throws;if(p.walls>0)++bankThrows;
    slot->planned=true;slot->predicted=p.effect;
    auto &r=reservations[owner->entindex()];r.released=true;r.until=gpGlobals->time+(id==WEAPON_SMOKEGRENADE?5:3);
    if(CVAR_GET_FLOAT("developer")>0)ALERT(at_console,"oc2tactic_release: t=%.3f player=%d id=%d entity=%d walls=%d predicted=%.2f,%.2f,%.2f\n",
        gpGlobals->time,owner->entindex(),id,grenade->entindex(),p.walls,p.effect.x,p.effect.y,p.effect.z);
}
bool OC2_BotValidateUtilityThrow(CBasePlayer *owner,int id,const Vector &source,const Vector &velocity)
{
    if(!owner||!owner->IsBot()||!owner->IsAlive()||owner->entindex()<1||owner->entindex()>32)return true;
    auto &p=plans[owner->entindex()];
    if(p.phase!=3||p.id!=id||p.userid!=GETPLAYERUSERID(owner->edict()))return true;
    auto *bot=static_cast<CCSBot *>(owner);
    const auto check=Predict(bot,id,p.shot,true,&source,&velocity);
    const Vector effect(check.effect.x,check.effect.y,check.effect.z);
    if(check.valid&&EffectUseful(bot,p,effect)&&Safe(bot,id,effect))
    {p.effect=effect;reservations[owner->entindex()].effect=effect;return true;}
    if(CVAR_GET_FLOAT("developer")>0)ALERT(at_console,"oc2tactic_emit_veto: valid=%d useful=%d safe=%d distance=%.2f source=%.2f,%.2f,%.2f velocity=%.2f,%.2f,%.2f effect=%.2f,%.2f,%.2f step=%.5f\n",
        check.valid,EffectUseful(bot,p,effect),Safe(bot,id,effect),(effect-p.target).Length(),source.x,source.y,source.z,velocity.x,velocity.y,velocity.z,effect.x,effect.y,effect.z,physicsStep);
    p.phase=0;p.cooldown=gpGlobals->time+1.5f;reservations[owner->entindex()]={};++cancels;
    bot->ClearPrimaryAttack();bot->ClearLookAt();return false;
}
void OC2_BotUtilityEffect(CBaseEntity *grenade,const Vector &point)
{
    if(CVAR_GET_FLOAT("developer")<=0)return;
    for(const auto &f:flights)if(f.entity==grenade&&f.until>gpGlobals->time)
    {
        ALERT(at_console,"oc2tactic_effect: t=%.3f entity=%d player=%d team=%d id=%d planned=%d point=%.2f,%.2f,%.2f error=%.2f walls=%d\n",
            gpGlobals->time,grenade->entindex(),f.player,f.team,f.id,f.planned,point.x,point.y,point.z,f.planned?(point-f.predicted).Length():0,f.walls);
        break;
    }
}
void OC2_BotUtilityBounce(CBaseEntity *grenade,const Vector &normal)
{
    if(std::fabs(normal.z)>=.7f)return;
    for(auto &f:flights)if(f.entity==grenade&&f.until>gpGlobals->time){++f.walls;break;}
}
void OC2_BotTacticsProbe()
{
    if(IS_DEDICATED_SERVER()||CVAR_GET_FLOAT("sv_cheats")==0||(CMD_ARGC()!=6&&CMD_ARGC()!=7))return;
    const int index=atoi(CMD_ARGV(1)),id=atoi(CMD_ARGV(2));
    auto *player=index>0&&index<=gpGlobals->maxClients?UTIL_PlayerByIndex(index):nullptr;
    if(!player||!player->IsBot()||!player->IsAlive()||!OC2_GrenadeWeapon(id))return;
    auto *bot=static_cast<CCSBot *>(player);Plan p;
    p.id=id;p.target=Vector(atof(CMD_ARGV(3)),atof(CMD_ARGV(4)),atof(CMD_ARGV(5)));
    if(!std::isfinite(p.target.LengthSquared())||p.target.LengthSquared()>32768.f*32768)return;
    Vector ground;const bool grounded=GroundTarget(bot,p.target,ground);
    if(grounded)p.target=ground+Vector(0,0,id==WEAPON_FLASHBANG?55:0);
    const auto saved=plans[index];plans[index]=p;
    const bool safe=grounded&&Safe(bot,id,p.target);if(grounded)PrepareShots(bot,p);
    int valid=0,banks=0,firstWall=-1;float best=99999;
    for(int i=0;i<p.count;++i)
    {
        const auto r=Predict(bot,id,p.shots[i]);const Vector effect(r.effect.x,r.effect.y,r.effect.z);
        if(!r.valid)continue;
        best=Q_min(best,(effect-p.target).Length());
        if(!EffectUseful(bot,p,effect)||!Safe(bot,id,effect))continue;
        if(!valid)firstWall=r.walls;
        ++valid;banks+=r.walls>0;
    }
    plans[index]=saved;
    if(CMD_ARGC()==7&&!Q_stricmp(CMD_ARGV(6),"launch")&&valid&&safe&&Find(bot,id,GRENADE_SLOT))
    {
        // Test-only knowledge seed. Planning, aiming, pin/release and native
        // physics remain the production path; no projectile is spawned here.
        p.userid=GETPLAYERUSERID(bot->edict());p.team=bot->m_iTeam;p.phase=5;p.start=gpGlobals->time;plans[index]=p;
    }
    ALERT(at_console,"oc2tactic_probe: player=%d id=%d safe=%d candidates=%d valid=%d banks=%d first_wall=%d best_error=%.2f protected=%d reserved=%d target=%.2f,%.2f,%.2f\n",
        index,id,safe,p.count,valid,banks,firstWall,best,protectedUtility,reservedUtility,p.target.x,p.target.y,p.target.z);
}
void OC2_BotTacticsFixture()
{
    // Explicit local-only effect fixture. It uses the real weapon factory,
    // projectile think/touch and flash/damage paths; never enabled in play.
    if(IS_DEDICATED_SERVER()||CVAR_GET_FLOAT("sv_cheats")==0||CMD_ARGC()!=6)return;
    const int index=atoi(CMD_ARGV(1)),id=atoi(CMD_ARGV(2));
    auto *owner=index>0&&index<=gpGlobals->maxClients?UTIL_PlayerByIndex(index):nullptr;
    if(!owner||!owner->IsAlive()||!OC2_GrenadeWeapon(id))return;
    Vector point(atof(CMD_ARGV(3)),atof(CMD_ARGV(4)),atof(CMD_ARGV(5)));
    if(!std::isfinite(point.LengthSquared())||point.LengthSquared()>32768.f*32768)return;
    auto *weapon=static_cast<CBasePlayerWeapon *>(owner->GetItemById(static_cast<WeaponIdType>(id)));
    if(!weapon)return;
    auto *grenade=owner->ThrowGrenade(weapon,point,Vector(0,0,-20),.1f);
    ALERT(at_console,"oc2tactic_fixture: owner=%d id=%d created=%d\n",index,id,grenade?grenade->entindex():0);
}
bool OC2_BotFriendlyFlash(CBaseEntity *grenade,CBasePlayer *victim,float &fade,float &hold,int &alpha,int &batch)
{
    if(!grenade||!victim)return true;
    const Flight *flight=nullptr;
    for(const auto &f:flights)if(f.entity==grenade&&f.until>gpGlobals->time){flight=&f;break;}
    if(!flight||!flight->bot||flight->id!=WEAPON_FLASHBANG||flight->team!=victim->m_iTeam||flight->team<1||flight->team>2)return true;
    // Never shorten or extend a pre-existing enemy/human flash to implement
    // this courtesy limit; leave that original exposure untouched.
    if(victim->m_blindStartTime+victim->m_blindHoldTime+victim->m_blindFadeTime>gpGlobals->time)
    {++flashSuppressed;return false;}
    if(batch==0)
    {
        batch=gpGlobals->time>=friendlyFlashNext[flight->team]?1:-1;
        if(batch==1)friendlyFlashNext[flight->team]=gpGlobals->time+15;
    }
    if(batch<0){++flashSuppressed;return false;}
    hold=Q_min(hold,.1f);fade=Q_min(fade,.5f-hold);alpha=Q_min(alpha,160);++flashLimited;
    if(CVAR_GET_FLOAT("developer")>0)ALERT(at_console,"oc2friendlyflash: t=%.3f victim=%d team=%d fade=%.3f hold=%.3f alpha=%d next=%.3f\n",
        gpGlobals->time,victim->entindex(),flight->team,fade,hold,alpha,friendlyFlashNext[flight->team]);
    return true;
}
bool OC2_BotWantsGrenadePurchase(CCSBot *bot)
{return bot&&bot->entindex()>0&&bot->entindex()<=32&&UtilityBuyChoice(bot).grenade;}
void OC2_BotBuyUtility(CCSBot *bot,bool buyGrenade)
{
    if(!bot || !bot->IsAlive()||bot->entindex()<1||bot->entindex()>32)return;
    auto& choice=UtilityBuyChoice(bot);if(choice.completed)return;choice.completed=true;
    if(buyGrenade&&choice.grenade&&TheCSBots()->AllowGrenades())
    {
        const char *aliases[]={"sgren","hegren","flash",bot->m_iTeam==CT?"incendiary":"molotov"};
        const float aggression=bot->GetProfile()->GetAggression(),teamwork=bot->GetProfile()->GetTeamwork();
        float weights[]={45+teamwork*45,50+aggression*20,40+teamwork*25+aggression*15,40+aggression*45};
        int order[]={0,1,2,3};
        for(int i=0;i<4;++i)weights[i]+=RANDOM_FLOAT(0,25);
        for(int i=0;i<4;++i)for(int j=i+1;j<4;++j)if(weights[order[j]]>weights[order[i]])std::swap(order[i],order[j]);
        // Buy through the real server command path. Two bounded passes permit
        // a second flash when another type is unaffordable or already held.
        for(int i=0;i<8 && OC2_GrenadeCount(bot)<3;++i)
        {
            if(bot->m_iAccount<200)break;
            const int before=OC2_GrenadeCount(bot);
            bot->ClientCommand(aliases[order[i%4]]);
            if(OC2_GrenadeCount(bot)>before)++purchases;
        }
    }
    if(choice.zeus&&bot->m_bHasPrimary && bot->m_iAccount>=600)
    {
        const int money=bot->m_iAccount;bot->ClientCommand("zeus");
        if(bot->m_iAccount<money)++purchases;
    }
    if(CVAR_GET_FLOAT("developer")>0)ALERT(at_console,"oc2tactic_buy: player=%d name=%s grenades=%d money=%d aggression=%.2f teamwork=%.2f favorite=%d\n",
        bot->entindex(),bot->GetProfile()->GetName(),OC2_GrenadeCount(bot),bot->m_iAccount,bot->GetProfile()->GetAggression(),bot->GetProfile()->GetTeamwork(),
        bot->GetProfile()->GetWeaponPreferenceCount()?bot->GetProfile()->GetWeaponPreference(0):0);
}
bool OC2_BotEquipUtility(CCSBot *bot,bool noSmoke)
{
    auto &p=plans[bot->entindex()];
    if(!p.phase || (noSmoke&&p.id==WEAPON_SMOKEGRENADE))return false;
    auto *w=Find(bot,p.id,GRENADE_SLOT);if(!w)return false;
    bot->SelectItem(STRING(w->pev->classname));return true;
}
bool OC2_BotUtilityUpdate(CCSBot *bot,CBasePlayer *threat)
{
    if(!bot||bot->entindex()<1||bot->entindex()>32)return false;
    auto &p=plans[bot->entindex()];
    if(!bot->IsAlive()||OC2_BotParked(bot))
    {p.phase=0;if(!reservations[bot->entindex()].released)reservations[bot->entindex()]={};return false;}
    const int userid=GETPLAYERUSERID(bot->edict());
    if(p.userid!=userid||p.team!=bot->m_iTeam){p={};p.userid=userid;p.team=bot->m_iTeam;}
    if(CSGameRules()->IsFreezePeriod() || CSGameRules()->m_bRoundTerminating || bot->IsBlind()
        || bot->IsUsingLadder() || bot->IsDefusingBomb() || bot->GetTask()==CCSBot::PLANT_BOMB || OC2_HostageCarriedBy(bot) || OC2_HostageGrabbingBy(bot))
    {if(p.phase)Cancel(bot,p);bot->m_isWaitingToTossGrenade=false;return false;}
    auto *active=bot->GetActiveWeapon();
    if(!p.phase)bot->m_isWaitingToTossGrenade=false;
    if(!p.phase && active && OC2_GrenadeWeapon(active->m_iId))
    {bot->EquipBestWeapon(MUST_EQUIP);active=bot->GetActiveWeapon();}
    if(p.phase==4)
    {
        bot->StopRapidFire();bot->ClearPrimaryAttack();bot->ClearMovement();bot->StopAiming();
        bot->SetLookAt("AvoidOwnFlash",&p.aim,PRIORITY_UNINTERRUPTABLE,2,false,3);
        if(gpGlobals->time-p.start>=1.7f){p.phase=0;p.cooldown=gpGlobals->time+8;bot->ClearLookAt();}
        return true;
    }
    if(p.phase==5)
    {
        // A line-up needs a stable launch point. Do not let the navigation
        // state's stuck/jump logic move us while candidates are being checked.
        bot->ResetStuckMonitor();bot->ClearMovement();bot->ClearPrimaryAttack();
        if(gpGlobals->time-p.start>2.5f||(bot->pev->origin-p.origin).LengthSquared()>96*96)
        {p.phase=0;p.next=gpGlobals->time+.5f;return false;}
        if(!(bot->pev->flags&FL_ONGROUND)||std::fabs(bot->pev->velocity.z)>5)return true;
        if(plannerFrame!=gpGlobals->time){plannerFrame=gpGlobals->time;plannerUsed=0;}
        if(plannerUsed>=3)return true;
        int turn=0;
        for(int n=1;n<=32;++n)
        {
            const int index=(plannerLast+n-1)%32+1;
            if(plans[index].phase!=5)continue;
            auto *peer=UTIL_PlayerByIndex(index);
            if(!UTIL_IsValidPlayer(peer)||peer->has_disconnected||!peer->IsAlive()||OC2_BotParked(peer)
                ||GETPLAYERUSERID(peer->edict())!=plans[index].userid||peer->m_iTeam!=plans[index].team)
            {plans[index].phase=0;continue;}
            turn=index;break;
        }
        if(turn!=bot->entindex())return true;
        ++plannerUsed;
        plannerLast=bot->entindex();
        for(int attempt=0;attempt<2&&p.cursor<p.count;++attempt)
        {
            const Shot shot=p.shots[p.cursor++];const auto result=Predict(bot,p.id,shot);
            const Vector effect(result.effect.x,result.effect.y,result.effect.z);
            if(!result.valid||!EffectUseful(bot,p,effect)||!Safe(bot,p.id,effect))continue;
            auto *w=Find(bot,p.id,GRENADE_SLOT);if(!w)break;
            p.shot=shot;p.effect=effect;p.walls=result.walls;p.strength=shot.strength;
            p.aim=bot->EyePosition()+Forward(shot.pitch,shot.yaw)*1000;
            p.start=gpGlobals->time;p.hold=0;p.phase=1;p.released=false;
            auto &r=reservations[bot->entindex()];r.userid=userid;r.team=bot->m_iTeam;r.id=p.id;r.effect=effect;r.until=gpGlobals->time+5;r.released=false;
            ++started;
            if(CVAR_GET_FLOAT("developer")>0)ALERT(at_console,"oc2tactic_plan: t=%.3f player=%d userid=%d id=%d strength=%.3f target=%.1f,%.1f,%.1f effect=%.1f,%.1f,%.1f walls=%d\n",
                gpGlobals->time,bot->entindex(),userid,p.id,p.strength,p.target.x,p.target.y,p.target.z,effect.x,effect.y,effect.z,p.walls);
            bot->StopRapidFire();bot->StopAiming();bot->ClearPrimaryAttack();bot->SelectItem(STRING(w->pev->classname));
            bot->ClearMovement();bot->m_isWaitingToTossGrenade=true;return true;
        }
        if(p.cursor>=p.count){++blockedArc;p.phase=0;p.next=gpGlobals->time+.6f;}
        return p.phase==5;
    }
    if(p.phase)
    {
        if(p.released)
        {
            bot->ClearPrimaryAttack();bot->m_isWaitingToTossGrenade=false;p.phase=0;p.cooldown=gpGlobals->time+8;
            if(p.id==WEAPON_FLASHBANG){p.phase=4;p.start=gpGlobals->time;p.aim=bot->EyePosition()+(bot->EyePosition()-p.target).Normalize()*500;bot->ClearLookAt();}
            return true;
        }
        const float preparationLimit=p.walls>0?6.f:4.f;
        if(!active || active->m_iId!=p.id || gpGlobals->time-p.start>preparationLimit || !Safe(bot,p.id,p.effect)
            ||(p.knownUntil>0&&gpGlobals->time>p.knownUntil))
        {Cancel(bot,p);bot->m_isWaitingToTossGrenade=false;return false;}
        bot->StopRapidFire();bot->ResetStuckMonitor();bot->ClearMovement();bot->StopAiming();
        p.aim=bot->EyePosition()+Forward(p.shot.pitch,p.shot.yaw)*1000;
        bot->SetLookAt("TacticalUtility",&p.aim,PRIORITY_UNINTERRUPTABLE,preparationLimit,false,p.walls>0?.2f:2.f);
        if(p.phase==3){bot->ClearPrimaryAttack();return true;}
        if(active->m_flStartThrow>0)
        {
            if(p.hold==0)p.hold=gpGlobals->time;
            // Timed grenades encode strength in 1/1024 units; fire grenades store 0..1.
            const float strength=active->m_iId==WEAPON_MOLOTOV?active->m_flLastFire:active->m_flLastFire/1024;
            if(gpGlobals->time-p.hold>=0.5f && fabsf(strength-p.strength)<.025f
                && bot->m_lookAtSpotState==CCSBot::LOOK_AT_SPOT)
            {
                Shot actual=p.shot;actual.pitch=bot->pev->v_angle.x;actual.yaw=bot->pev->v_angle.y;actual.strength=strength;
                const auto check=Predict(bot,p.id,actual,true);const Vector effect(check.effect.x,check.effect.y,check.effect.z);
                if(!check.valid||!EffectUseful(bot,p,effect)||!Safe(bot,p.id,effect))
                {
                    if(CVAR_GET_FLOAT("developer")>0)ALERT(at_console,"oc2tactic_recheck: valid=%d distance=%.2f ideal=%.3f,%.3f actual=%.3f,%.3f eye=%.2f,%.2f,%.2f\n",
                        check.valid,(effect-p.target).Length(),p.shot.pitch,p.shot.yaw,actual.pitch,actual.yaw,bot->EyePosition().x,bot->EyePosition().y,bot->EyePosition().z);
                    // Keep the pin held while aim converges. The real launch
                    // must pass this check; the outer preparation timeout
                    // still holsters safely if no accurate solution settles.
                    bot->PrimaryAttack();if(p.strength==.5f)bot->SecondaryAttack();return true;
                }
                p.effect=effect;reservations[bot->entindex()].effect=effect;
                bot->ClearPrimaryAttack();p.phase=3;
            }
            else {bot->PrimaryAttack();if(p.strength==.5f)bot->SecondaryAttack();}
        }
        else if(bot->m_lookAtSpotState==CCSBot::LOOK_AT_SPOT && gpGlobals->time-p.start>0.2f)
        {bot->PrimaryAttack();if(p.strength==.5f)bot->SecondaryAttack();}
        else bot->ClearPrimaryAttack();
        return true;
    }
    if(bot->HasPath() && bot->m_pathIndex>=0 && bot->m_pathIndex<bot->m_pathLength)
    {
        const Vector delta=bot->m_path[bot->m_pathIndex].pos-bot->pev->origin;
        const float length=Q_min(240.0f,delta.Length());
        for(float t=40;t<=length;t+=40)
            if(OC2_InfernoDanger(bot->pev->origin+delta.Normalize()*t))
            {
                p.routeFire=bot->pev->origin+delta.Normalize()*t;p.routeUntil=gpGlobals->time+2;
                bot->DestroyPath();bot->ClearMovement();break;
            }
    }
    Vector nearest;
    if(OC2_InfernoNearest(bot->pev->origin,95,&nearest))
    {
        Vector away=bot->pev->origin-nearest;away.z=0;
        if(away.LengthSquared()<1)away=Vector(1,0,0);
        bot->DestroyPath();bot->ClearMovement();
        const Vector directions[]={away.Normalize(),Vector(-away.y,away.x,0).Normalize(),Vector(away.y,-away.x,0).Normalize()};
        for(const auto &direction:directions)
        {
            Vector safe=bot->pev->origin+direction*150;TraceResult floor;
            UTIL_TraceLine(safe+Vector(0,0,48),safe-Vector(0,0,128),ignore_monsters,bot->edict(),&floor);
            if(floor.fStartSolid||floor.fAllSolid||floor.flFraction==1||floor.vecPlaneNormal.z<0.7f)continue;
            safe=floor.vecEndPos+Vector(0,0,1-bot->pev->mins.z);
            if(!OC2_InfernoDanger(safe)&&ClearLine(bot->EyePosition(),safe+bot->pev->view_ofs,bot))
            {bot->MoveTowardsPosition(&safe);return true;}
        }
        return true;
    }
    // Recognition is delayed and stores an entity handle, not a visible-position
    // snapshot. Recheck before reading its current location for a new plan.
    if(threat && (!UTIL_IsValidPlayer(threat)||threat->has_disconnected||!threat->IsAlive()
        ||threat->m_iTeam==bot->m_iTeam))threat=nullptr;
    const int threatUser=threat?GETPLAYERUSERID(threat->edict()):0;
    const bool newThreat=threatUser && threatUser!=p.threatUser;
    // Zeus is an opportunistic close-range weapon; never a substitute for a knife.
    const float enemyRange=threat?(threat->pev->origin-bot->pev->origin).Length():99999;
    auto *zeus=Find(bot,WEAPON_TASER,KNIFE_SLOT);
    if(threat && zeus && enemyRange<150 && threat->pev->health<=100 && OC2_BotCanUseZeus(bot)
        && bot->IsVisible(threat,CHECK_FOV)
        && ClearLine(bot->EyePosition(),threat->BodyTarget(bot->EyePosition()),bot)
        && !OC2_SmokeBlocks(bot->EyePosition(),threat->EyePosition()))
    {
        bot->StopRapidFire();
        if(active!=zeus){bot->ClearPrimaryAttack();bot->SelectItem(STRING(zeus->pev->classname));}
        if(bot->GetActiveWeapon()==zeus)return false;
    }
    else if(active && active->m_iId==WEAPON_TASER){bot->StopRapidFire();bot->ClearPrimaryAttack();bot->EquipBestWeapon(MUST_EQUIP);}
    if(gpGlobals->time<p.cooldown || (!newThreat && gpGlobals->time<p.next)
        || !TheCSBots()->AllowGrenades() || bot->IsReloading())return false;
    p.next=gpGlobals->time+.4f+bot->entindex()*.013f;++decisions;
    if(threatUser)p.threatUser=threatUser;
    if(threat && !bot->IsVisible(threat,CHECK_FOV))threat=nullptr;
    int options[4],count=0;Vector targets[4];float scores[4];
    const bool recent=!bot->m_isLastEnemyDead&&bot->GetLastSawEnemyTimestamp()>0&&bot->GetTimeSinceLastSawEnemy()<2.5f;
    const bool known=threat||recent;
    const Vector enemyPoint=threat?threat->pev->origin:bot->GetLastKnownEnemyPosition();
    const float range=known?(enemyPoint-bot->pev->origin).Length():99999;
    Vector fire;bool neededFire=false,emergency=false;
    // A fire elsewhere in view is not a reason to smoke. Extinguish only a
    // real route obstruction, or to rescue an ally in immediate danger.
    for(int i=1;i<=gpGlobals->maxClients;++i)
    {
        auto *ally=UTIL_PlayerByIndex(i);
        if(!UTIL_IsValidPlayer(ally)||!ally->IsAlive()||ally->m_iTeam!=bot->m_iTeam)continue;
        if((ally->pev->origin-bot->pev->origin).LengthSquared()>650*650)continue;
        if(OC2_InfernoNearest(ally->pev->origin,110,&fire)&&ClearLine(bot->EyePosition(),fire+Vector(0,0,24),bot))
        {neededFire=emergency=true;break;}
    }
    if(!neededFire&&p.routeUntil>gpGlobals->time&&OC2_InfernoNearest(p.routeFire,85,&fire)
        &&!OC2_InfernoNearTeam(fire,90,bot->m_iTeam)&&OC2_InfernoNearTeam(fire,90,0,2))neededFire=true;
    if(!neededFire&&bot->HasPath()&&bot->m_pathIndex>=0&&bot->m_pathIndex<bot->m_pathLength)
    {
        const Vector step=bot->m_path[bot->m_pathIndex].pos-bot->pev->origin;
        for(float d=80;d<=Q_min(400.f,step.Length());d+=80)
            if(OC2_InfernoNearest(bot->pev->origin+step.Normalize()*d,85,&fire)
                &&!OC2_InfernoNearTeam(fire,90,bot->m_iTeam)&&OC2_InfernoNearTeam(fire,90,0,2))
            {neededFire=true;break;}
    }
    bool advancing=false;
    if(known && range>=450 && bot->HasPath() && bot->m_pathIndex>=0 && bot->m_pathIndex<bot->m_pathLength)
    {
        const Vector step=bot->m_path[bot->m_pathIndex].pos-bot->pev->origin;
        advancing=step.LengthSquared()>40*40 && DotProduct(step.Normalize(),(enemyPoint-bot->pev->origin).Normalize())>.5f;
    }
    const float aggression=bot->GetProfile()->GetAggression(),teamwork=bot->GetProfile()->GetTeamwork();
    const auto add=[&](int id,const Vector &point,float score){if(Find(bot,id,GRENADE_SLOT)&&count<4){options[count]=id;targets[count]=point;scores[count++]=score+RANDOM_FLOAT(0,12);}};
    if(Find(bot,WEAPON_SMOKEGRENADE,GRENADE_SLOT))
    {
        if(neededFire)add(WEAPON_SMOKEGRENADE,fire,emergency?130:95);
        else if(known && range>550 && (bot->pev->health<45 || advancing || (bot->m_iTeam==CT && bot->GetGameState()->IsBombPlanted())))
            add(WEAPON_SMOKEGRENADE,bot->pev->origin+(enemyPoint-bot->pev->origin)*.65f,65+teamwork*25);
    }
    if(known&&range>=350&&range<=950)
    {
        add(WEAPON_HEGRENADE,enemyPoint,60+aggression*20);
        // Fire flushes a held/recent corner and blocks a route. No fire into
        // smoke, and no flash merely because a flash happens to be carried.
        if(!threat||threat->pev->velocity.Length2D()<120||!advancing)
            add(WEAPON_MOLOTOV,enemyPoint,65+aggression*20);
        if(advancing||(!threat&&recent)||bot->GetNearbyFriendCount()>0)
            add(WEAPON_FLASHBANG,enemyPoint+Vector(0,0,55),50+teamwork*25);
    }
    if(!count){++noOptions;return false;}
    for(int i=0;i<count;++i)for(int j=i+1;j<count;++j)if(scores[j]>scores[i])
    {std::swap(scores[i],scores[j]);std::swap(options[i],options[j]);std::swap(targets[i],targets[j]);}
    for(int j=0;j<count;++j)
    {
        const int id=options[j];Vector landing;
        p.emergency=emergency&&id==WEAPON_SMOKEGRENADE;
        if(!GroundTarget(bot,targets[j],landing)){++unsafe;continue;}
        if(id==WEAPON_FLASHBANG)landing.z+=55;
        if(!Safe(bot,id,landing)){++unsafe;continue;}
        p.id=id;p.target=landing;p.start=gpGlobals->time;p.phase=5;
        p.knownUntil=(neededFire&&id==WEAPON_SMOKEGRENADE)?0:threat?gpGlobals->time+4:bot->GetLastSawEnemyTimestamp()+3.5f;
        PrepareShots(bot,p);return false;
    }
    return false;
}
void OC2_BotTacticsDump()
{
    ALERT(at_console,"oc2tactics: purchases=%d throws=%d cancels=%d zeus=%d decisions=%d no_options=%d unsafe=%d blocked_arc=%d chance_skipped=%d started=%d\n",
        purchases,throws,cancels,zeusUses,decisions,noOptions,unsafe,blockedArc,chanceSkipped,started);
    ALERT(at_console,"oc2tactic_guards: protected=%d reserved=%d bank_throws=%d arc_traces=%d flash_limited=%d flash_suppressed=%d flash_max=0.5 flash_interval=15 physics_step=%.5f\n",
        protectedUtility,reservedUtility,bankThrows,arcTraces,flashLimited,flashSuppressed,physicsStep);
    for(int i=1;i<33;++i)if(plans[i].userid)
        ALERT(at_console,"oc2tactic: player=%d userid=%d phase=%d weapon=%d next=%.3f target=%.1f,%.1f,%.1f\n",i,plans[i].userid,plans[i].phase,plans[i].id,plans[i].next,plans[i].target.x,plans[i].target.y,plans[i].target.z);
    for(int i=1;i<=gpGlobals->maxClients;++i)
    {
        auto *player=UTIL_PlayerByIndex(i);
        if(!UTIL_IsValidPlayer(player)||!player->IsBot()||!player->IsAlive())continue;
        auto *bot=static_cast<CCSBot *>(player);auto *enemy=bot->GetEnemy();Vector aim,landing;
        const auto *taser=Find(bot,WEAPON_TASER,KNIFE_SLOT);
        ALERT(at_console,"oc2tactic_zeus: player=%d owned=%d clip=%d usable=%d\n",i,taser!=nullptr,taser?taser->m_iClip:0,OC2_BotCanUseZeus(bot));
        ALERT(at_console,"oc2tactic_blind: player=%d alpha=%d remaining=%.3f fade=%.3f hold=%.3f\n",i,
            player->m_blindAlpha,Q_max(0.f,player->m_blindStartTime+player->m_blindHoldTime+player->m_blindFadeTime-gpGlobals->time),player->m_blindFadeTime,player->m_blindHoldTime);
        const bool grounded=enemy&&GroundTarget(bot,enemy->pev->origin,landing);
        ALERT(at_console,"oc2tactic_actor: player=%d enemy=%d visible=%d he_safe=%d ballistic=%d range=%.1f\n",i,
            enemy?enemy->entindex():0,bot->IsEnemyVisible(),grounded?FriendsSafe(bot,WEAPON_HEGRENADE,landing):false,
            grounded?BallisticAim(bot,landing,aim):false,enemy?(enemy->pev->origin-bot->pev->origin).Length():-1.0f);
    }
}

void CCSBot::PrimaryAttack()
{
    auto *weapon=GetActiveWeapon();
    if(weapon && weapon->m_iId==WEAPON_TASER)
    {
        StopRapidFire();
        if(!OC2_BotCanUseZeus(this))return;
        if(weapon->m_iClip>0 && m_flNextAttack<=0 && weapon->m_flNextPrimaryAttack<=0)++zeusUses;
    }
    CBot::PrimaryAttack();
}
bool OC2_BotCanUseZeus(CCSBot *bot)
{
    auto *enemy=bot->GetEnemy();
    if(!(enemy&&enemy->IsAlive()&&enemy->pev->health<=100&&!bot->IsBlind()&&bot->IsEnemyVisible()
        &&(enemy->pev->origin-bot->pev->origin).LengthSquared()<=150*150
        &&!bot->IsFriendInLineOfFire()&&ClearLine(bot->EyePosition(),enemy->BodyTarget(bot->EyePosition()),bot)
        &&!OC2_SmokeBlocks(bot->EyePosition(),enemy->EyePosition())))return false;
    const Vector source=bot->EyePosition(),line=enemy->BodyTarget(source)-source;
    const float length2=line.LengthSquared();if(length2<1)return false;
    // A single aim ray does not cover spread or an ally moving across it.
    // Reserve the ally's body envelope, including a ducking player in between.
    for(int i=1;i<=gpGlobals->maxClients;++i)
    {
        auto *ally=UTIL_PlayerByIndex(i);
        if(!UTIL_IsValidPlayer(ally)||ally==bot||!ally->IsAlive()||ally->m_iTeam!=bot->m_iTeam)continue;
        const Vector center=ally->pev->origin+Vector(0,0,16);
        const float t=DotProduct(center-source,line)/length2;
        if(t<=0||t>=1)continue;
        const Vector delta=center-(source+line*t);
        if(delta.x*delta.x+delta.y*delta.y<32*32&&std::fabs(delta.z)<54)return false;
    }
    return true;
}
