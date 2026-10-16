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

namespace
{
oc2_weapon::Motion OC2Motion(const CBasePlayer *player)
{
	oc2_weapon::Motion result = {player->pev->velocity.Length2D(), player->pev->velocity.z,
		(player->pev->flags & FL_ONGROUND) != 0, (player->pev->flags & FL_DUCKING) != 0,
		player->pev->movetype == MOVETYPE_FLY, false};
	// GoldSrc +speed scales usercmd velocity; it does not transmit Source's
	// independent m_bIsWalking flag. Do not guess that flag from low speed.
	return result;
}
}

LINK_ENTITY_TO_CLASS(weapon_p228, CP228)

void CP228::Spawn()
{
	pev->classname = MAKE_STRING("weapon_p228");
	Precache();
	m_iId = WEAPON_P228;
	SET_MODEL(edict(), "models/w_p228.mdl");
	m_iDefaultAmmo = P228_DEFAULT_GIVE;
	oc2_weapon::ResetState(oc2_weapon::Get(oc2_weapon::P250), m_flStartThrow, m_flReleaseThrow, m_flLastFire, m_flAccuracy, m_iShotsFired);
	m_bDelayFire = false;
	FallInit();
}

void CP228::Precache()
{
	PRECACHE_MODEL("models/v_p228.mdl");
	PRECACHE_MODEL("models/p_p228.mdl");
	PRECACHE_MODEL("models/w_p228.mdl");
	PRECACHE_SOUND("weapons/p228-1.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/p250/p250_clipout.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/p250/p250_clipin.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/p250/p250_slideback.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/p250/p250_sliderelease.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/p250/p250_draw.wav");
	m_iShell = PRECACHE_MODEL("models/pshell.mdl");
	m_usFireP228 = PRECACHE_EVENT(1, "events/p228.sc");
}

int CP228::GetItemInfo(ItemInfo *p)
{
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::P250);
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "357SIG";
	p->iMaxAmmo1 = params.reserve;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = params.clip;
	p->iSlot = 1;
	p->iPosition = 3;
	p->iId = m_iId = WEAPON_P228;
	p->iFlags = 0;
	p->iWeight = P228_WEIGHT;
	return 1;
}

BOOL CP228::Deploy()
{
	oc2_weapon::ResetState(oc2_weapon::Get(oc2_weapon::P250), m_flStartThrow, m_flReleaseThrow, m_flLastFire, m_flAccuracy, m_iShotsFired);
	m_bDelayFire = false;
	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;
	m_pPlayer->m_bShieldDrawn = false;
	m_fMaxSpeed = P228_MAX_SPEED;
	return DefaultDeploy("models/v_p228.mdl", "models/p_p228.mdl", P228_DRAW, "onehanded", UseDecrement() != FALSE);
}

void CP228::SecondaryAttack()
{
	// P250 has no shield/secondary firing mode. Keep the legacy method ABI.
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + .3f;
}

void CP228::PrimaryAttack()
{
	// Source generic gun takes its primary cycle from the item schema.
	P228Fire(0, oc2_weapon::Get(oc2_weapon::P250).cycle, FALSE);
}

void CP228::P228Fire(float flSpread, float flCycleTime, BOOL fUseAutoAim)
{
	if (m_iShotsFired > 0) return; // Source semi-auto trigger latch, released in ItemPostFrame.
	if (m_iClip <= 0)
	{
		if (m_fFireOnEmpty)
		{
			PlayEmptySound();
			m_flNextPrimaryAttack = GetNextAttackDelay(.2f);
		}
#ifndef CLIENT_DLL
		if (TheBots) TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
#endif
		return;
	}
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::P250);
	const int mode = 0;
	const oc2_weapon::Sample shot = oc2_weapon::SampleShot(params, OC2Motion(m_pPlayer), mode,
		gpGlobals->time, m_flStartThrow / 1024, m_flReleaseThrow / 64, m_flLastFire);
	oc2_weapon::Offset offset;
	oc2_weapon::SpreadOffsets(params, mode, m_pPlayer->random_seed, shot.inaccuracy, shot.recoilIndex, &offset);
	m_bDelayFire = params.fullAuto;
	++m_iShotsFired;
	--m_iClip;
	m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
#ifndef CLIENT_DLL
	m_pPlayer->SetAnimation(PLAYER_ATTACK1);
#endif
	UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
	Vector vecSrc = m_pPlayer->GetGunPosition();
	Vector vecAiming = (gpGlobals->v_forward + offset.x * gpGlobals->v_right + offset.y * gpGlobals->v_up).Normalize();
#ifdef REGAMEDLL_API
	const float damage = CSPlayerWeapon()->m_flBaseDamage;
#else
	const float damage = params.damage;
#endif
	// The deterministic Source two-circle offset is already applied. The
	// GoldSrc trace/penetration adapter must not add its old Gaussian spread.
	m_pPlayer->FireBullets3(vecSrc, vecAiming, 0, params.range, params.penetration,
		BULLET_PLAYER_357SIG, damage, params.rangeModifier, m_pPlayer->pev, true, m_pPlayer->random_seed);
#ifdef CLIENT_WEAPONS
	const int flag = FEV_NOTHOST;
#else
	const int flag = 0;
#endif
	// event_t fparams are quantized at 1/100; scale small offsets for transport.
	PLAYBACK_EVENT_FULL(flag, m_pPlayer->edict(), m_usFireP228, 0, (float *)&g_vecZero, (float *)&g_vecZero,
		offset.x * 1024, offset.y * 1024, int(m_pPlayer->pev->punchangle.x * 100),
		int(m_pPlayer->pev->punchangle.y * 100), m_iClip == 0, FALSE);
	m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = GetNextAttackDelay(params.cycle);
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.0f;
#ifndef CLIENT_DLL
	if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
		m_pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
#endif
	oc2_weapon::RecordShot(params, shot, mode, gpGlobals->time, m_flStartThrow,
		m_flReleaseThrow, m_flLastFire, m_flAccuracy);
	float pitch, yaw;
	oc2_weapon::Recoil(params, mode, int(shot.recoilIndex), m_pPlayer->random_seed, pitch, yaw);
	m_pPlayer->pev->punchangle.x += pitch;
	m_pPlayer->pev->punchangle.y += yaw;
}

void CP228::Reload()
{
	if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0) return;
	// Source generic reload follows SequenceDuration. This duration is read
	// from the deployed converted model's reload sequence, not an APK rule.
	if (DefaultReload(iMaxClip(), P228_RELOAD, 2.266666667f))
	{
#ifndef CLIENT_DLL
		m_pPlayer->SetAnimation(PLAYER_RELOAD);
#endif
		m_iShotsFired = 0;
		m_bDelayFire = false;
	}
}

void CP228::WeaponIdle()
{
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::P250);
	const int mode = 0;
	const oc2_weapon::Sample shot = oc2_weapon::SampleShot(params, OC2Motion(m_pPlayer), mode,
		gpGlobals->time, m_flStartThrow / 1024, m_flReleaseThrow / 64, m_flLastFire);
	m_flAccuracy = oc2_weapon::Clamp(shot.inaccuracy + params.spread[mode], 0, 1);
	ResetEmptySound();
	m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
	if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 20.0f;
	SendWeaponAnim(P228_IDLE, UseDecrement() != FALSE);
}
