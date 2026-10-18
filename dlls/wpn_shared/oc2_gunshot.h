#pragma once
#include "oc2_weapon_params.h"

// Scope the migrated gun's ballistics around its existing animation/sound
// routine. The five earlier conversions already sample Source directly.
// This adds no player/weapon fields: prediction restores the existing fusers.
class OC2GunShot
{
public:
	static OC2GunShot *&Active() { static OC2GunShot *active=nullptr;return active; }
	explicit OC2GunShot(CBasePlayerWeapon *item,bool burstContinuation=false)
		: weapon(item),params(oc2_weapon::FromId(item->m_iId)),previous(Active()),
		  continuation(burstContinuation),fired(false),pellet(0)
	{
		if (!params || oc2_weapon::DirectShotAdapter(item->m_iId) || !item->m_pPlayer) { params=nullptr;return; }
		CBasePlayer *p=item->m_pPlayer;
		oldPunch=p->pev->punchangle;oldClock=item->m_flLastFire;
		mode=oc2_weapon::Mode(item->m_iId,item->m_iWeaponState,p->pev->fov);
		const oc2_weapon::Motion motion={p->pev->velocity.Length2D(),p->pev->velocity.z,
			(p->pev->flags&FL_ONGROUND)!=0,(p->pev->flags&FL_DUCKING)!=0,p->pev->movetype==MOVETYPE_FLY,false};
		sample=oc2_weapon::SampleShot(*params,motion,mode,gpGlobals->time,
			item->m_flStartThrow/1024,item->m_flReleaseThrow/64,oldClock);
		oc2_weapon::SpreadOffsets(*params,mode,p->random_seed,sample.inaccuracy,sample.recoilIndex,offsets);
		Active()=this;
	}
	~OC2GunShot()
	{
		if (!params) return;
		Active()=previous;
		if (!fired) { weapon->m_flLastFire=oldClock;return; }
		oc2_weapon::RecordShot(*params,sample,mode,gpGlobals->time,weapon->m_flStartThrow,
			weapon->m_flReleaseThrow,weapon->m_flLastFire,weapon->m_flAccuracy);
		float pitch,yaw;
		oc2_weapon::Recoil(*params,mode,int(sample.recoilIndex),weapon->m_pPlayer->random_seed,pitch,yaw,oc2_weapon::BurstMode(weapon->m_iId,weapon->m_iWeaponState));
		weapon->m_pPlayer->pev->punchangle=oldPunch;
		weapon->m_pPlayer->pev->punchangle.x+=pitch;
		weapon->m_pPlayer->pev->punchangle.y+=yaw;
		if (!continuation)
		{
			if (oc2_weapon::RequiresTriggerRelease(weapon->m_iId,weapon->m_iWeaponState)) weapon->m_iShotsFired=1;
			const float cycle=oc2_weapon::Cycle(weapon->m_iId,weapon->m_iWeaponState,weapon->m_pPlayer->pev->fov);
			weapon->m_flNextPrimaryAttack=UTIL_WeaponTimeBase()+cycle;
			if (weapon->m_iId==17 && weapon->m_flGlock18Shoot>0) weapon->m_flGlock18Shoot=gpGlobals->time+params->burstSpacing;
			if (weapon->m_iId==15 && weapon->m_flFamasShoot>0) weapon->m_flFamasShoot=gpGlobals->time+params->burstSpacing;
		}
#ifndef CLIENT_DLL
		if (CVAR_GET_FLOAT("developer")>=2)
			ALERT(at_console,"oc2shot: t=%.6f id=%d mode=%d clip=%d damage=%d range=%.1f modifier=%.6f penetration=%.3f cycle=%.6f inaccuracy=%.8f pellets=%d continuation=%d\n",
				gpGlobals->time,weapon->m_iId,mode,weapon->m_iClip,params->damage,params->range,params->rangeModifier,
				params->penetration,oc2_weapon::Cycle(weapon->m_iId,weapon->m_iWeaponState,weapon->m_pPlayer->pev->fov),sample.inaccuracy,pellet,int(continuation));
#endif
	}
	bool Handles(const CBaseEntity *shooter) const { return params && shooter==weapon->m_pPlayer; }
	float Inaccuracy() const { return sample.inaccuracy; }
	float RecoilIndex() const { return sample.recoilIndex; }
	oc2_weapon::Offset Aim(Vector &direction,float &spread,float &range,int &penetration,int &damage,float &modifier)
	{
		const int index=pellet<params->pellets?pellet:params->pellets-1;
		const oc2_weapon::Offset result=offsets[index];++pellet;fired=true;
		UTIL_MakeVectors(weapon->m_pPlayer->pev->v_angle+oldPunch);
		direction=(gpGlobals->v_forward+result.x*gpGlobals->v_right+result.y*gpGlobals->v_up).Normalize();
		spread=0;range=params->range;penetration=max(1,int(ceilf(params->penetration)));damage=params->damage;modifier=params->rangeModifier;
		return result;
	}
private:
	CBasePlayerWeapon *weapon;
	const oc2_weapon::Params *params;
	OC2GunShot *previous;
	bool continuation,fired;
	int mode=0,pellet;
	float oldClock=0;
	Vector oldPunch;
	oc2_weapon::Sample sample{};
	oc2_weapon::Offset offsets[64]{};
};
