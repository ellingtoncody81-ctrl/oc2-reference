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
#include "oc2_gunshot.h"

LINK_ENTITY_TO_CLASS(weapon_xm1014, CXM1014)

void CXM1014::Spawn(void)
{
	Precache();

	m_iId = WEAPON_XM1014;
	SET_MODEL(edict(), "models/w_xm1014.mdl");

	m_iDefaultAmmo = XM1014_DEFAULT_GIVE;

	// get ready to fall
	FallInit();
}

void CXM1014::Precache()
{
	PRECACHE_MODEL("models/v_xm1014.mdl");
	PRECACHE_MODEL("models/w_xm1014.mdl");

	m_iShellId = m_iShell = PRECACHE_MODEL("models/shotgunshell.mdl");

	PRECACHE_SOUND("weapons/xm1014-1.wav");
	PRECACHE_SOUND("weapons/reload1.wav");
	PRECACHE_SOUND("weapons/reload3.wav");

	m_usFireXM1014 = PRECACHE_EVENT(1, "events/xm1014.sc");
}

int CXM1014::GetItemInfo(ItemInfo *p)
{
	const oc2_weapon::Params &params=oc2_weapon::Get(oc2_weapon::XM1014);
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "buckshot";
	p->iMaxAmmo1 = params.reserve;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = params.clip;
	p->iSlot = 0;
	p->iPosition = 12;
	p->iId = m_iId = WEAPON_XM1014;
	p->iFlags = 0;
	p->iWeight = XM1014_WEIGHT;

	return 1;
}

BOOL CXM1014::Deploy()
{
	return DefaultDeploy("models/v_xm1014.mdl", "models/p_xm1014.mdl", XM1014_DRAW, "m249", UseDecrement() != FALSE);
}

void CXM1014::PrimaryAttack()
{
	OC2GunShot sourceShot(this);
	Vector vecAiming, vecSrc, vecDir;
	int flag;

	// don't fire underwater
	if (m_pPlayer->pev->waterlevel == 3)
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = GetNextAttackDelay(0.15);
		return;
	}

	if (m_iClip <= 0)
	{
		Reload();

		if (!m_iClip)
		{
			PlayEmptySound();
		}

#ifndef CLIENT_DLL
		if (TheBots != NULL)
		{
			TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
		}
#endif

		m_flNextPrimaryAttack = GetNextAttackDelay(1);
		return;
	}

	m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
	m_iClip--;

	m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
	// player "shoot" animation
#ifndef CLIENT_DLL
	m_pPlayer->SetAnimation(PLAYER_ATTACK1);
#endif

	UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);

	vecSrc = m_pPlayer->GetGunPosition();
	vecAiming = gpGlobals->v_forward;

    Vector sourceAngles=m_pPlayer->pev->v_angle+m_pPlayer->pev->punchangle;
    const auto &schema=oc2_weapon::Get(oc2_weapon::XM1014);
    for (int pellet=0;pellet<schema.pellets;++pellet)
    {
        Vector pelletSource=vecSrc,pelletAim=vecAiming;
        m_pPlayer->FireBullets3(pelletSource,pelletAim,0,schema.range,1,BULLET_PLAYER_BUCKSHOT,
            schema.damage,schema.rangeModifier,m_pPlayer->pev,false,m_pPlayer->random_seed);
    }


#ifdef CLIENT_WEAPONS
	flag = FEV_NOTHOST;
#else
	flag = 0;
#endif

    PLAYBACK_EVENT_FULL(flag,m_pPlayer->edict(),m_usFireXM1014,0,(float *)&g_vecZero,
        (float *)&sourceAngles,sourceShot.Inaccuracy()*1024,0,int(m_pPlayer->random_seed&255),
        int(sourceShot.RecoilIndex()),m_iClip==0,FALSE);

#ifndef CLIENT_DLL
	if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
	{
		// HEV suit - indicate out of ammo condition
		m_pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
	}
#endif
	if (m_iClip != 0)
		m_flPumpTime = UTIL_WeaponTimeBase() + 0.125f;

	m_flNextPrimaryAttack = GetNextAttackDelay(0.25);
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.25f;

	if (m_iClip != 0)
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.25f;
	else
		m_flTimeWeaponIdle = 0.75f;

	m_fInSpecialReload = 0;

	if (m_pPlayer->pev->flags & FL_ONGROUND)
		m_pPlayer->pev->punchangle.x -= UTIL_SharedRandomLong(m_pPlayer->random_seed + 1, 3, 5);
	else
		m_pPlayer->pev->punchangle.x -= UTIL_SharedRandomLong(m_pPlayer->random_seed + 1, 7, 10);
}

void CXM1014::Reload()
{
	DefaultShotgunReload(XM1014_RELOAD,XM1014_START_RELOAD,.5f,.5f,
		"weapons/reload1.wav","weapons/reload3.wav");
}

void CXM1014::WeaponIdle()
{
	ResetEmptySound();
	m_pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);

	if (m_flPumpTime && m_flPumpTime < UTIL_WeaponTimeBase())
	{
		m_flPumpTime = 0;
	}

	if (m_flTimeWeaponIdle < UTIL_WeaponTimeBase())
	{
		if (m_iClip == 0 && m_fInSpecialReload == 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
		{
			Reload();
		}
		else if (m_fInSpecialReload != 0)
		{
			if (m_iClip != iMaxClip() && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
			{
				Reload();
			}
			else
			{
				// reload debounce has timed out
				FinishShotgunReload(XM1014_PUMP,1.5f);
			}
		}
		else
		{
			SendWeaponAnim(XM1014_IDLE, UseDecrement() != FALSE);
		}
	}
}

float CXM1014::GetMaxSpeed()
{
	return oc2_weapon::Get(oc2_weapon::XM1014).speed[oc2_weapon::Mode(5,m_iWeaponState,m_pPlayer?m_pPlayer->pev->fov:90)];
}
