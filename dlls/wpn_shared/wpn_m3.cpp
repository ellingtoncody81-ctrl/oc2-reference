/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

#include "stdafx.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "oc2_weapon_params.h"
#include "oc2_gunshot.h"

namespace
{
oc2_weapon::Motion NovaMotion(const CBasePlayer *player)
{
	oc2_weapon::Motion result = {player->pev->velocity.Length2D(), player->pev->velocity.z,
		(player->pev->flags & FL_ONGROUND) != 0, (player->pev->flags & FL_DUCKING) != 0,
		player->pev->movetype == MOVETYPE_FLY, false};
	return result;
}
}

LINK_ENTITY_TO_CLASS(weapon_m3, CM3)

void CM3::Spawn()
{
	pev->classname = MAKE_STRING("weapon_m3");
	Precache();
	m_iId = WEAPON_M3;
	SET_MODEL(edict(), "models/w_m3.mdl");
	m_iDefaultAmmo = M3_DEFAULT_GIVE;
	oc2_weapon::ResetState(oc2_weapon::Get(oc2_weapon::Nova), m_flStartThrow, m_flReleaseThrow, m_flLastFire, m_flAccuracy, m_iShotsFired);
	m_bDelayFire = false;
	FallInit();
}

void CM3::Precache()
{
	PRECACHE_MODEL("models/v_m3.mdl");
	PRECACHE_MODEL("models/p_m3.mdl");
	PRECACHE_MODEL("models/w_m3.mdl");
	m_iShellId = m_iShell = PRECACHE_MODEL("models/shotgunshell.mdl");
	PRECACHE_SOUND("weapons/Zshiryu777/nova/nova-1.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/nova/nova_insertshell_01.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/nova/nova_pump.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement1.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/nova/nova_draw.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement2.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement3.wav");
	m_usFireM3 = PRECACHE_EVENT(1, "events/m3.sc");
}

int CM3::GetItemInfo(ItemInfo *p)
{
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::Nova);
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "buckshot";
	p->iMaxAmmo1 = params.reserve;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = params.clip;
	p->iSlot = 0;
	p->iPosition = 5;
	p->iId = m_iId = WEAPON_M3;
	p->iFlags = 0;
	p->iWeight = M3_WEIGHT;
	return 1;
}

BOOL CM3::Deploy()
{
	oc2_weapon::ResetState(oc2_weapon::Get(oc2_weapon::Nova), m_flStartThrow, m_flReleaseThrow, m_flLastFire, m_flAccuracy, m_iShotsFired);
	m_bDelayFire = false;
	m_fInSpecialReload = 0;
	return DefaultDeploy("models/v_m3.mdl", "models/p_m3.mdl", M3_DRAW, "shotgun", UseDecrement() != FALSE);
}

void CM3::PrimaryAttack()
{
	if (m_iShotsFired > 0) return; // Nova is semi-auto, including a held trigger after pumping.
	if (m_pPlayer->pev->waterlevel == 3)
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = GetNextAttackDelay(.15f);
		return;
	}
	if (m_iClip <= 0)
	{
		Reload();
		if (!m_iClip) PlayEmptySound();
#ifndef CLIENT_DLL
		if (TheBots) TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
#endif
		m_flNextPrimaryAttack = GetNextAttackDelay(.2f);
		return;
	}
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::Nova);
	const oc2_weapon::Sample shot = oc2_weapon::SampleShot(params, NovaMotion(m_pPlayer), 0,
		gpGlobals->time, m_flStartThrow / 1024, m_flReleaseThrow / 64, m_flLastFire);
	oc2_weapon::Offset offsets[9];
	oc2_weapon::SpreadOffsets(params, 0, m_pPlayer->random_seed, shot.inaccuracy, shot.recoilIndex, offsets);
	m_iShotsFired = 1;
	--m_iClip;
	m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
	m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
#ifndef CLIENT_DLL
	m_pPlayer->SetAnimation(PLAYER_ATTACK1);
#endif
	const Vector aimAngles = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;
	UTIL_MakeVectors(aimAngles);
	const Vector vecSrc = m_pPlayer->GetGunPosition();
	const Vector vecAiming = gpGlobals->v_forward;
#ifdef CLIENT_WEAPONS
	const int flag = FEV_NOTHOST;
#else
	const int flag = 0;
#endif
	// Explicit angles leave both integer event fields available for the RNG
	// seed and pattern index. bparam fields have only one bit on the wire.
	PLAYBACK_EVENT_FULL(flag, m_pPlayer->edict(), m_usFireM3, 0, (float *)&g_vecZero, (float *)&aimAngles,
		shot.inaccuracy * 1024, 0, m_pPlayer->random_seed & 255, int(shot.recoilIndex), FALSE, FALSE);
#ifndef CLIENT_DLL
	if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
		m_pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
#endif
	if (m_iClip) m_flPumpTime = UTIL_WeaponTimeBase() + .5f;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = GetNextAttackDelay(params.cycle);
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + (m_iClip ? 2.5f : .875f);
	m_fInSpecialReload = 0;
	oc2_weapon::RecordShot(params, shot, 0, gpGlobals->time, m_flStartThrow,
		m_flReleaseThrow, m_flLastFire, m_flAccuracy);
	float pitch, yaw;
	oc2_weapon::Recoil(params, 0, int(shot.recoilIndex), m_pPlayer->random_seed, pitch, yaw);
	m_pPlayer->pev->punchangle.x += pitch;
	m_pPlayer->pev->punchangle.y += yaw;
	m_pPlayer->m_flEjectBrass = gpGlobals->time + .45f;
}

void CM3::Reload()
{
	if (!(m_pPlayer->pev->button & IN_ATTACK)) m_iShotsFired = 0;
	// Source Nova keeps one-round phases; only the resource timing changes.
	DefaultShotgunReload(M3_RELOAD,M3_START_RELOAD,.5f,.5f);
}

void CM3::WeaponIdle()
{
	if (!(m_pPlayer->pev->button & IN_ATTACK)) m_iShotsFired = 0;
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::Nova);
	const oc2_weapon::Sample shot = oc2_weapon::SampleShot(params, NovaMotion(m_pPlayer), 0,
		gpGlobals->time, m_flStartThrow / 1024, m_flReleaseThrow / 64, m_flLastFire);
	m_flAccuracy = oc2_weapon::Clamp(shot.inaccuracy + params.spread[0], 0, 1);
	ResetEmptySound();
	m_pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);
	if (m_flPumpTime && m_flPumpTime < UTIL_WeaponTimeBase()) m_flPumpTime = 0;
	if (m_flTimeWeaponIdle >= UTIL_WeaponTimeBase()) return;
	if (m_iClip == 0 && m_fInSpecialReload == 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]) Reload();
	else if (m_fInSpecialReload != 0)
	{
		if (m_iClip != iMaxClip() && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType]) Reload();
		else
		{
			FinishShotgunReload(M3_PUMP,1.5f);
		}
	}
	else
	{
		SendWeaponAnim(M3_IDLE, UseDecrement() != FALSE);
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 20;
	}
}

float CM3::GetMaxSpeed()
{
	return oc2_weapon::MaxSpeed(21,m_iWeaponState,m_pPlayer?m_pPlayer->pev->fov:90);
}
