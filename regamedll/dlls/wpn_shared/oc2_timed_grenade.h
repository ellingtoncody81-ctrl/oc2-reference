#ifndef OC2_TIMED_GRENADE_H
#define OC2_TIMED_GRENADE_H
#include "oc2_grenade_throw.h"
#ifdef CLIENT_DLL
extern int g_iFreezeTimeOver;
#endif

namespace oc2_grenade
{
inline bool HasAmmo(CBasePlayerWeapon &w)
{
	return w.m_pPlayer && w.m_iPrimaryAmmoType>=0 && w.m_iPrimaryAmmoType<MAX_AMMO_SLOTS &&
		w.m_pPlayer->m_rgAmmo[w.m_iPrimaryAmmoType]>0;
}
inline void Begin(CBasePlayerWeapon &w,bool underhand)
{
	if (!HasAmmo(w) || w.m_flStartThrow!=0 || w.m_flReleaseThrow==-2 ||
		(w.m_iWeaponState&WPNSTATE_SHIELD_DRAWN) || !w.m_pPlayer->IsAlive() || !w.m_pPlayer->m_bCanShoot) return;
#ifdef CLIENT_DLL
	if (!::g_iFreezeTimeOver) return;
#else
	if (g_pGameRules->IsFreezePeriod()) return;
#endif
	if (!Finite(gpGlobals->time) || gpGlobals->time<0) return;
	w.m_flStartThrow=Clock(gpGlobals->time); w.m_flReleaseThrow=0; w.m_flLastFire=underhand?0:1024;
	w.SendWeaponAnim(1,w.UseDecrement()); // Verified pullpin sequence on all three resources.
	w.m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+29.0f/30.0f;
}
inline void IdleFrame(CBasePlayerWeapon &w)
{
	if (w.m_flStartThrow>0 || w.m_flTimeWeaponIdle>UTIL_WeaponTimeBase()) return;
	if (!HasAmmo(w) || (w.m_flReleaseThrow==-2 && w.m_iId==WEAPON_FLASHBANG)) { w.RetireWeapon();return; }
	if (w.m_flReleaseThrow==-2)
	{
		w.m_flReleaseThrow=-1;w.SendWeaponAnim(3,w.UseDecrement());
		w.m_pPlayer->m_flNextAttack=w.m_flNextPrimaryAttack=w.m_flNextSecondaryAttack=w.m_flTimeWeaponIdle=
			UTIL_WeaponTimeBase()+w.AnimationDuration(3,1);
	}
	else { w.SendWeaponAnim(0,w.UseDecrement());w.m_flTimeWeaponIdle=UTIL_WeaponTimeBase()+20; }
}
#ifndef CLIENT_DLL
inline bool FiniteVector(const Vector &v)
{
	return Finite(v.x)&&Finite(v.y)&&Finite(v.z)&&fabsf(v.x)<32768&&fabsf(v.y)<32768&&fabsf(v.z)<32768;
}
#endif
inline void EmitProjectile(CBasePlayerWeapon &w,unsigned short event)
{
	if (!HasAmmo(w)) { Reset(w.m_flStartThrow,w.m_flReleaseThrow,w.m_flLastFire);return; }
#ifndef CLIENT_DLL
	CBasePlayer *player=w.m_pPlayer;
	Vector angles=player->pev->v_angle+player->pev->punchangle;
	if (!FiniteVector(angles)||!FiniteVector(player->pev->origin)||!FiniteVector(player->pev->view_ofs)||!FiniteVector(player->pev->velocity))
		{ Reset(w.m_flStartThrow,w.m_flReleaseThrow,w.m_flLastFire);return; }
	const float strength=Strength(w.m_flLastFire);
	angles.x=Pitch(angles.x);UTIL_MakeVectors(angles);
	const Vector forward=gpGlobals->v_forward;
	const Vector eye=player->pev->origin+player->pev->view_ofs;
	Vector source=eye+Vector(0,0,Lower(strength));
	TraceResult trace;
	// GoldSrc exposes fixed hulls, so use its point trace with Source's 6-unit
	// retreat. Do not claim the unavailable Source +/-2 grenade hull.
	UTIL_TraceLine(source,source+forward*22,dont_ignore_monsters,player->edict(),&trace);
	if (!FiniteVector(trace.vecEndPos)) { Reset(w.m_flStartThrow,w.m_flReleaseThrow,w.m_flLastFire);return; }
	source=trace.vecEndPos-forward*6;
	const Vector velocity=forward*Speed(strength)+player->pev->velocity*1.25f;
	CGrenade *projectile=player->ThrowGrenade(&w,source,velocity,1.5f,event);
	const int before=player->m_rgAmmo[w.m_iPrimaryAmmoType];
	if (projectile)
	{
		--player->m_rgAmmo[w.m_iPrimaryAmmoType];player->SetAnimation(PLAYER_ATTACK1);
		player->Radio("%!MRAD_FIREINHOLE","#Fire_in_the_hole");
	}
	if (CVAR_GET_FLOAT("developer")>0)
		ALERT(at_console,"oc2throw: t=%.6f player=%d id=%d strength=%.6f speed=%.6f lower=%.6f eye=%.6f,%.6f,%.6f trace=%.6f origin=%.6f,%.6f,%.6f velocity=%.6f,%.6f,%.6f inherited=%.6f,%.6f,%.6f pitch=%.6f created=%d before=%d after=%d\n",
			gpGlobals->time,player->entindex(),w.m_iId,strength,Speed(strength),Lower(strength),eye.x,eye.y,eye.z,trace.flFraction,source.x,source.y,source.z,
			velocity.x,velocity.y,velocity.z,player->pev->velocity.x,player->pev->velocity.y,player->pev->velocity.z,
			angles.x,projectile?projectile->entindex():0,before,player->m_rgAmmo[w.m_iPrimaryAmmoType]);
#else
	--w.m_pPlayer->m_rgAmmo[w.m_iPrimaryAmmoType]; // Replayed from the authoritative snapshot.
#endif
	w.m_flStartThrow=0;w.m_flReleaseThrow=-2; // One-shot latch; keep the throw animation's remaining idle time.
}
inline void PostFrame(CBasePlayerWeapon &w,unsigned short event)
{
	if (!w.m_pPlayer || !w.m_pPlayer->IsAlive()) return;
	const int buttons=w.m_pPlayer->pev->button&(IN_ATTACK|IN_ATTACK2);
	if (w.m_flStartThrow>0)
	{
		const int step=Advance(w.m_flStartThrow,w.m_flReleaseThrow,w.m_flLastFire,buttons,gpGlobals->time);
		if (step==BeginRelease)
		{
			const bool low=Strength(w.m_flLastFire)<=.33f;
			w.SendWeaponAnim(low?4:2,w.UseDecrement());
			w.m_flNextPrimaryAttack=w.m_flNextSecondaryAttack=w.m_flTimeWeaponIdle=
				UTIL_WeaponTimeBase()+ThrowDuration(w.m_iId,low);
		}
		else if (step==Emit) EmitProjectile(w,event);
		return;
	}
	if (w.m_flReleaseThrow==-2) { IdleFrame(w);return; }
	if ((buttons&IN_ATTACK) && w.m_flNextPrimaryAttack<=UTIL_WeaponTimeBase()) w.PrimaryAttack();
	else if ((buttons&IN_ATTACK2) && w.m_flNextSecondaryAttack<=UTIL_WeaponTimeBase()) w.SecondaryAttack();
	else if (!buttons) IdleFrame(w);
}
}
#endif
