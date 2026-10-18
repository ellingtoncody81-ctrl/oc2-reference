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

LINK_ENTITY_TO_CLASS(weapon_hegrenade, CHEGrenade)

void CHEGrenade::Spawn(void)
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	Precache();

	m_iId = WEAPON_HEGRENADE;
	SET_MODEL(edict(), "models/w_hegrenade.mdl");

	pev->dmg = 4;

	m_iDefaultAmmo = HEGRENADE_DEFAULT_GIVE;
	m_flStartThrow = 0;
	m_flReleaseThrow = -1.0f;
	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

	// get ready to fall down.
	FallInit();
}

void CHEGrenade::Precache(void)
{
	PRECACHE_MODEL("models/v_hegrenade.mdl");
	PRECACHE_MODEL("models/shield/v_shield_hegrenade.mdl");

	PRECACHE_SOUND("weapons/hegrenade-1.wav");
	PRECACHE_SOUND("weapons/hegrenade-2.wav");
	PRECACHE_SOUND("weapons/he_bounce-1.wav");
	PRECACHE_SOUND("weapons/pinpull.wav");

	m_usCreateExplosion = PRECACHE_EVENT(1, "events/createexplo.sc");
}

int CHEGrenade::GetItemInfo(ItemInfo *p)
{
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "HEGrenade";

	p->iMaxAmmo1 = MAX_AMMO_HEGRENADE;
	p->iMaxClip = WEAPON_NOCLIP;

	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iSlot = 3;
	p->iPosition = 1;
	p->iId = m_iId = WEAPON_HEGRENADE;
	p->iWeight = HEGRENADE_WEIGHT;
	p->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;

	return 1;}

BOOL CHEGrenade::Deploy(void)
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	m_flReleaseThrow = -1.0f;
	m_fMaxSpeed = HEGRENADE_MAX_SPEED;
	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

	m_pPlayer->m_bShieldDrawn = false;

	if (m_pPlayer->HasShield())
		return DefaultDeploy("models/shield/v_shield_hegrenade.mdl", "models/shield/p_shield_hegrenade.mdl", HEGRENADE_DRAW, "shieldgren", UseDecrement() != FALSE);
	else
		return DefaultDeploy("models/v_hegrenade.mdl", "models/p_hegrenade.mdl", HEGRENADE_DRAW, "grenade", UseDecrement() != FALSE);
}

BOOL CHEGrenade::CanHolster(void)
{
	return TRUE; // Source permits cancelling a held grenade by holstering.
}

void CHEGrenade::Holster(int skiplocal)
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if (!m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
	{
		m_pPlayer->pev->weapons &= ~(1 << WEAPON_HEGRENADE);
		DestroyItem();
	}

	m_flStartThrow = 0;
	m_flReleaseThrow = -1.0f;
}

void CHEGrenade::PrimaryAttack(void)
{
	oc2_grenade::Begin(*this,false);
}

void CHEGrenade::SetPlayerShieldAnim(void)
{
	if (!m_pPlayer->HasShield())
		return;

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
		strcpy(m_pPlayer->m_szAnimExtention, "shield");
	else
		strcpy(m_pPlayer->m_szAnimExtention, "shieldgren");
}

void CHEGrenade::ResetPlayerShieldAnim(void)
{
	if (!m_pPlayer->HasShield())
		return;

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
	{
		strcpy(m_pPlayer->m_szAnimExtention, "shieldgren");
	}
}

bool CHEGrenade::ShieldSecondaryFire(int iUpAnim, int iDownAnim)
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

		m_fMaxSpeed = HEGRENADE_MAX_SPEED;
		m_pPlayer->m_bShieldDrawn = false;
	}
	else
	{
		m_iWeaponState |= WPNSTATE_SHIELD_DRAWN;
		SendWeaponAnim(iUpAnim, UseDecrement() != FALSE);
		strcpy(m_pPlayer->m_szAnimExtention, "shielded");

		m_fMaxSpeed = HEGRENADE_MAX_SPEED_SHIELD;
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

void CHEGrenade::SecondaryAttack(void)
{
	if (!ShieldSecondaryFire(SHIELDGUN_DRAW, SHIELDGUN_DRAWN_IDLE)) oc2_grenade::Begin(*this,true);
}

void CHEGrenade::ItemPostFrame()
{
	oc2_grenade::PostFrame(*this,m_usCreateExplosion);
}

void CHEGrenade::WeaponIdle(void)
{
	oc2_grenade::IdleFrame(*this);
}

BOOL CHEGrenade::CanDeploy(void)
{
	return m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0;
}
