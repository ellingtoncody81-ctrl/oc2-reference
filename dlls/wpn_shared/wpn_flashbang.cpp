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
#include "oc2_timed_grenade.h"

LINK_ENTITY_TO_CLASS(weapon_flashbang, CFlashbang)

void CFlashbang::Spawn(void)
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	pev->classname = MAKE_STRING("weapon_flashbang");

	Precache();

	m_iId = WEAPON_FLASHBANG;
	SET_MODEL(edict(), "models/w_flashbang.mdl");

	pev->dmg = 4;

	m_iDefaultAmmo = FLASHBANG_DEFAULT_GIVE;
	m_flStartThrow = 0;
	m_flReleaseThrow = -1.0f;
	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

	// get ready to fall down.
	FallInit();
}

void CFlashbang::Precache(void)
{
	PRECACHE_MODEL("models/v_flashbang.mdl");
	PRECACHE_MODEL("models/shield/v_shield_flashbang.mdl");

	PRECACHE_SOUND("weapons/flashbang-1.wav");
	PRECACHE_SOUND("weapons/flashbang-2.wav");
	PRECACHE_SOUND("weapons/pinpull.wav");
}

int CFlashbang::GetItemInfo(ItemInfo *p)
{
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "Flashbang";

	p->iMaxAmmo1 = MAX_AMMO_FLASHBANG;
	p->iMaxClip = WEAPON_NOCLIP;

	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iSlot = 3;
	p->iPosition = 2;
	p->iId = m_iId = WEAPON_FLASHBANG;
	p->iWeight = FLASHBANG_WEIGHT;
	p->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;

	return 1;
}

BOOL CFlashbang::Deploy(void)
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	m_flReleaseThrow = -1.0f;
	m_fMaxSpeed = FLASHBANG_MAX_SPEED;

	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;
	m_pPlayer->m_bShieldDrawn = false;

	if (m_pPlayer->HasShield())
		return DefaultDeploy("models/shield/v_shield_flashbang.mdl", "models/shield/p_shield_flashbang.mdl", FLASHBANG_DRAW, "shieldgren", UseDecrement() != FALSE);
	else
		return DefaultDeploy("models/v_flashbang.mdl", "models/p_flashbang.mdl", FLASHBANG_DRAW, "grenade", UseDecrement() != FALSE);
}

void CFlashbang::Holster(int skiplocal)
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if (!m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
	{
		m_pPlayer->pev->weapons &= ~(1 << WEAPON_FLASHBANG);
		DestroyItem();
	}

	m_flStartThrow = 0;
	m_flReleaseThrow = -1.0f;
}

void CFlashbang::PrimaryAttack(void)
{
	oc2_grenade::Begin(*this,false);
}

void CFlashbang::SetPlayerShieldAnim(void)
{
	if (!m_pPlayer->HasShield())
		return;

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
		strcpy(m_pPlayer->m_szAnimExtention, "shield");
	else
		strcpy(m_pPlayer->m_szAnimExtention, "shieldgren");
}

void CFlashbang::ResetPlayerShieldAnim(void)
{
	if (!m_pPlayer->HasShield())
		return;

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
	{
		strcpy(m_pPlayer->m_szAnimExtention, "shieldgren");
	}
}

bool CFlashbang::ShieldSecondaryFire(int iUpAnim, int iDownAnim)
{
	if (!m_pPlayer->HasShield() || m_flStartThrow > 0)
	{
		return false;
	}

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
	{
		m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;
		SendWeaponAnim(iDownAnim, UseDecrement() != FALSE);

		strcpy(m_pPlayer->m_szAnimExtention, "shieldgren");

		m_fMaxSpeed = FLASHBANG_MAX_SPEED;
		m_pPlayer->m_bShieldDrawn = false;
	}
	else
	{
		m_iWeaponState |= WPNSTATE_SHIELD_DRAWN;
		SendWeaponAnim(iUpAnim, UseDecrement() != FALSE);

		strcpy(m_pPlayer->m_szAnimExtention, "shielded");

		m_fMaxSpeed = FLASHBANG_MAX_SPEED_SHIELD;
		m_pPlayer->m_bShieldDrawn = true;
	}

#ifndef CLIENT_DLL
	m_pPlayer->UpdateShieldCrosshair((m_iWeaponState & WPNSTATE_SHIELD_DRAWN) != WPNSTATE_SHIELD_DRAWN);
#endif
	m_pPlayer->ResetMaxSpeed();

	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.4f;
	m_flNextPrimaryAttack = GetNextAttackDelay(0.4);
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.6f;

	return true;
}

void CFlashbang::SecondaryAttack(void)
{
	if (!ShieldSecondaryFire(SHIELDGUN_DRAW, SHIELDGUN_DRAWN_IDLE)) oc2_grenade::Begin(*this,true);
}

void CFlashbang::ItemPostFrame()
{
	oc2_grenade::PostFrame(*this,0);
}

void CFlashbang::WeaponIdle(void)
{
	oc2_grenade::IdleFrame(*this);
}

BOOL CFlashbang::CanDeploy(void)
{
	return m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0;
}
