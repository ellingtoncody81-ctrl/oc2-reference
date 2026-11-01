#include "precompiled.h"
#include "oc2_bot_tactics.h"
#include "oc2_bot_control.h"
#include "oc2_inferno.h"
#include "oc2_smoke.h"
#include "hostage/hostage.h"
#include <cmath>
#include <utility>

namespace
{
struct Plan { int userid=0,id=0,phase=0; Vector target,aim; float next=0,start=0,hold=0,strength=1; };
Plan plans[33];
int purchases=0,throws=0,cancels=0,zeusUses=0;
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
        if(id==WEAPON_FLASHBANG && p==bot)continue; // thrower turns away after release
        const Vector delta=target-p->EyePosition();
        if(delta.LengthSquared()>radius*radius || !ClearLine(target+Vector(0,0,32),p->EyePosition(),bot))continue;
        if(id!=WEAPON_FLASHBANG)return false;
        if(delta.LengthSquared()<500*500)return false;
        UTIL_MakeVectors(p->pev->v_angle);
        if(DotProduct(delta.Normalize(),gpGlobals->v_forward)>-0.2f)return false;
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
    if(id==WEAPON_SMOKEGRENADE)return !OC2_SmokeInside(target+Vector(0,0,40));
    return FriendsSafe(bot,id,target);
}
void Cancel(CCSBot *bot,Plan &p)
{
    bot->ClearPrimaryAttack();bot->ClearLookAt();
    bot->StopRapidFire();
    bot->EquipBestWeapon(MUST_EQUIP);p.phase=0;p.next=gpGlobals->time+3;++cancels;
}
}
void OC2_BotTacticsReset()
{
    for(auto &p:plans)p={};purchases=throws=cancels=zeusUses=0;
}
void OC2_BotBuyUtility(CCSBot *bot)
{
    if(!bot || !bot->IsAlive())return;
    if(TheCSBots()->AllowGrenades())
    {
        const char *aliases[]={"sgren","hegren","flash",bot->m_iTeam==CT?"incendiary":"molotov"};
        int order[]={0,1,2,3};
        for(int i=3;i>0;--i)std::swap(order[i],order[RANDOM_LONG(0,i)]);
        const int desired=RANDOM_LONG(1,3);
        for(int i=0;i<4 && OC2_GrenadeCount(bot)<desired;++i)
        {
            if(bot->m_iAccount<200)break;
            const int before=OC2_GrenadeCount(bot);
            bot->ClientCommand(aliases[order[i]]);
            if(OC2_GrenadeCount(bot)>before)++purchases;
        }
    }
    if(bot->m_bHasPrimary && bot->m_iAccount>=600 && RANDOM_LONG(0,99)<25)
    {
        const int money=bot->m_iAccount;bot->ClientCommand("zeus");
        if(bot->m_iAccount<money)++purchases;
    }
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
    if(!bot || !bot->IsAlive() || OC2_BotParked(bot))return false;
    auto &p=plans[bot->entindex()];
    const int userid=GETPLAYERUSERID(bot->edict());
    if(p.userid!=userid){p={};p.userid=userid;}
    if(CSGameRules()->IsFreezePeriod() || CSGameRules()->m_bRoundTerminating || bot->IsBlind()
        || bot->IsDefusingBomb() || bot->GetTask()==CCSBot::PLANT_BOMB || OC2_HostageCarriedBy(bot) || OC2_HostageGrabbingBy(bot))
    {if(p.phase)Cancel(bot,p);bot->m_isWaitingToTossGrenade=false;return false;}
    auto *active=bot->GetActiveWeapon();
    if(!p.phase && active && OC2_GrenadeWeapon(active->m_iId))
    {bot->EquipBestWeapon(MUST_EQUIP);active=bot->GetActiveWeapon();}
    if(p.phase==4)
    {
        bot->StopRapidFire();bot->ClearPrimaryAttack();bot->ClearMovement();bot->StopAiming();
        bot->SetLookAt("AvoidOwnFlash",&p.aim,PRIORITY_UNINTERRUPTABLE,2,false,3);
        if(gpGlobals->time-p.start>=1.7f){p.phase=0;p.next=gpGlobals->time+8;bot->ClearLookAt();}
        return true;
    }
    if(p.phase)
    {
        if(active && active->m_iId==p.id && active->m_flReleaseThrow==-2)
        {
            bot->ClearPrimaryAttack();bot->m_isWaitingToTossGrenade=false;p.phase=0;p.next=gpGlobals->time+8;++throws;
            if(p.id==WEAPON_FLASHBANG){p.phase=4;p.start=gpGlobals->time;p.aim=bot->EyePosition()+(bot->EyePosition()-p.target).Normalize()*500;bot->ClearLookAt();}
            return true;
        }
        if(!active || active->m_iId!=p.id || gpGlobals->time-p.start>4 || !Safe(bot,p.id,p.target))
        {Cancel(bot,p);bot->m_isWaitingToTossGrenade=false;return false;}
        bot->StopRapidFire();bot->ResetStuckMonitor();bot->ClearMovement();bot->StopAiming();
        // Combat posture can change eye height while the pin is held.
        if(p.phase!=3 && !BallisticAim(bot,p.target,p.aim,p.id==WEAPON_SMOKEGRENADE,p.strength))
        {Cancel(bot,p);bot->m_isWaitingToTossGrenade=false;return false;}
        bot->SetLookAt("TacticalUtility",&p.aim,PRIORITY_UNINTERRUPTABLE,4,false,2);
        if(p.phase==3){bot->ClearPrimaryAttack();return true;}
        if(active->m_flStartThrow>0)
        {
            if(p.hold==0)p.hold=gpGlobals->time;
            if(gpGlobals->time-p.hold>=0.5f && fabsf(active->m_flLastFire/1024-p.strength)<.025f
                && bot->m_lookAtSpotState==CCSBot::LOOK_AT_SPOT)
            {bot->ClearPrimaryAttack();p.phase=3;}
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
            {bot->DestroyPath();bot->ClearMovement();break;}
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
    // Zeus is an opportunistic close-range weapon; never a substitute for a knife.
    const float enemyRange=threat?(threat->pev->origin-bot->pev->origin).Length():99999;
    auto *zeus=Find(bot,WEAPON_TASER,KNIFE_SLOT);
    if(threat && zeus && enemyRange<150 && threat->pev->health<=100
        && ClearLine(bot->EyePosition(),threat->BodyTarget(bot->EyePosition()),bot)
        && !OC2_SmokeBlocks(bot->EyePosition(),threat->EyePosition()))
    {
        bot->StopRapidFire();
        if(active!=zeus){bot->ClearPrimaryAttack();bot->SelectItem(STRING(zeus->pev->classname));}
        if(bot->GetActiveWeapon()==zeus)return false;
    }
    else if(active && active->m_iId==WEAPON_TASER){bot->StopRapidFire();bot->ClearPrimaryAttack();bot->EquipBestWeapon(MUST_EQUIP);}
    if(gpGlobals->time<p.next || !TheCSBots()->AllowGrenades() || bot->IsReloading())return false;
    p.next=gpGlobals->time+RANDOM_FLOAT(1,2);
    int options[4],count=0;Vector targets[4];
    Vector fire;
    const bool nearbyFire=OC2_InfernoNearest(bot->pev->origin,700,&fire)
        && ClearLine(bot->EyePosition(),fire+Vector(0,0,20),bot);
    if(threat && enemyRange<350 && !nearbyFire)return false;
    if(Find(bot,WEAPON_SMOKEGRENADE,GRENADE_SLOT))
    {
        if(nearbyFire){options[count]=WEAPON_SMOKEGRENADE;targets[count++]=fire;}
        else if(threat && (bot->pev->health<45 || (bot->m_iTeam==CT && bot->GetGameState()->IsBombPlanted())))
        {options[count]=WEAPON_SMOKEGRENADE;targets[count++]=bot->pev->origin+(threat->pev->origin-bot->pev->origin)*0.65f;}
    }
    if(threat && enemyRange<=950 && !OC2_SmokeBlocks(bot->EyePosition(),threat->EyePosition()))
        for(int id:{WEAPON_HEGRENADE,WEAPON_FLASHBANG,WEAPON_MOLOTOV})
            if(Find(bot,id,GRENADE_SLOT)){options[count]=id;targets[count++]=threat->pev->origin;}
    if(!count || RANDOM_LONG(0,99)>=65)return false;
    const int first=RANDOM_LONG(0,count-1);
    for(int j=0;j<count;++j)
    {
        const int index=(first+j)%count,id=options[index];Vector aim,landing;
        if(!GroundTarget(bot,targets[index],landing)||!Safe(bot,id,landing))continue;
        // Medium throws reduce the overshoot after a fast first ground bounce.
        float strength=(landing-bot->pev->origin).Length2D()<=650?.5f:1;
        if(!BallisticAim(bot,landing,aim,id==WEAPON_SMOKEGRENADE,strength))
        {strength=1;if(!BallisticAim(bot,landing,aim,id==WEAPON_SMOKEGRENADE,strength))continue;}
        auto *w=Find(bot,id,GRENADE_SLOT);if(!w)continue;
        p.id=id;p.target=landing;p.aim=aim;p.start=gpGlobals->time;p.hold=0;p.strength=strength;p.phase=1;
        bot->StopRapidFire();bot->StopAiming();bot->ClearPrimaryAttack();bot->SelectItem(STRING(w->pev->classname));
        bot->ClearMovement();bot->m_isWaitingToTossGrenade=true;
        return true;
    }
    return false;
}
void OC2_BotTacticsDump()
{
    ALERT(at_console,"oc2tactics: purchases=%d throws=%d cancels=%d zeus=%d\n",purchases,throws,cancels,zeusUses);
    for(int i=1;i<33;++i)if(plans[i].userid)
        ALERT(at_console,"oc2tactic: player=%d userid=%d phase=%d weapon=%d next=%.3f target=%.1f,%.1f,%.1f\n",i,plans[i].userid,plans[i].phase,plans[i].id,plans[i].next,plans[i].target.x,plans[i].target.y,plans[i].target.z);
    for(int i=1;i<=gpGlobals->maxClients;++i)
    {
        auto *player=UTIL_PlayerByIndex(i);
        if(!UTIL_IsValidPlayer(player)||!player->IsBot()||!player->IsAlive())continue;
        auto *bot=static_cast<CCSBot *>(player);auto *enemy=bot->GetEnemy();Vector aim,landing;
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
    return enemy&&enemy->IsAlive()&&enemy->pev->health<=100&&!bot->IsBlind()&&bot->IsEnemyVisible()
        &&(enemy->pev->origin-bot->pev->origin).LengthSquared()<=150*150
        &&!bot->IsFriendInLineOfFire()&&ClearLine(bot->EyePosition(),enemy->BodyTarget(bot->EyePosition()),bot)
        &&!OC2_SmokeBlocks(bot->EyePosition(),enemy->EyePosition());
}
