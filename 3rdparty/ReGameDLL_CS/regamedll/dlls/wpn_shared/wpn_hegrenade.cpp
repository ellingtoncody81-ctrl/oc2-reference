#include "precompiled.h"
#include "oc2_timed_grenade.h"

LINK_ENTITY_TO_CLASS(weapon_hegrenade, CHEGrenade, CCSHEGrenade)

void CHEGrenade::Spawn()
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

	// Get ready to fall down
	FallInit();

	// extend
	CBasePlayerWeapon::Spawn();
}

void CHEGrenade::Precache()
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
	auto info = GetWeaponInfo(WEAPON_HEGRENADE);

	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "HEGrenade";

	p->iMaxAmmo1 = info ? info->maxRounds : MAX_AMMO_HEGRENADE;
	p->iMaxClip = info ? info->gunClipSize : WEAPON_NOCLIP;

	p->pszAmmo2 = nullptr;
	p->iMaxAmmo2 = -1;
	p->iSlot = 3;
	p->iPosition = 1;
	p->iId = m_iId = WEAPON_HEGRENADE;
	p->iWeight = HEGRENADE_WEIGHT;
	p->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;

	return 1;
}

BOOL CHEGrenade::Deploy()
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

void CHEGrenade::Holster(int skiplocal)
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if (!m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
	{
#ifndef REGAMEDLL_FIXES
		// Moved to DestroyItem()
		m_pPlayer->pev->weapons &= ~(1 << WEAPON_HEGRENADE);
#endif

		DestroyItem();
	}

	m_flStartThrow = 0;
	m_flReleaseThrow = -1.0f;
}

void CHEGrenade::PrimaryAttack()
{
	oc2_grenade::Begin(*this,false);
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
		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shieldgren");

		m_fMaxSpeed = HEGRENADE_MAX_SPEED;
		m_pPlayer->m_bShieldDrawn = false;
	}
	else
	{
		m_iWeaponState |= WPNSTATE_SHIELD_DRAWN;
		SendWeaponAnim(iUpAnim, UseDecrement() != FALSE);
		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shielded");

		m_fMaxSpeed = HEGRENADE_MAX_SPEED_SHIELD;
		m_pPlayer->m_bShieldDrawn = true;
	}

	m_pPlayer->UpdateShieldCrosshair((m_iWeaponState & WPNSTATE_SHIELD_DRAWN) != WPNSTATE_SHIELD_DRAWN);
	m_pPlayer->ResetMaxSpeed();

	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.4f;
	m_flNextPrimaryAttack = GetNextAttackDelay(0.4);
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.6f;

	return true;
}

void CHEGrenade::SecondaryAttack()
{
	if (!ShieldSecondaryFire(SHIELDGREN_UP, SHIELDGREN_DOWN)) oc2_grenade::Begin(*this,true);
}

void CHEGrenade::SetPlayerShieldAnim()
{
	if (!m_pPlayer->HasShield())
		return;

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shield");
	else
		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shieldgren");
}

void CHEGrenade::ResetPlayerShieldAnim()
{
	if (!m_pPlayer->HasShield())
		return;

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
	{
		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shieldgren");
	}
}

void CHEGrenade::ItemPostFrame()
{
	oc2_grenade::PostFrame(*this,m_usCreateExplosion);
}

void CHEGrenade::WeaponIdle()
{
	oc2_grenade::IdleFrame(*this);
}

LINK_HOOK_CLASS_CHAIN3(BOOL, CBasePlayerWeapon, CHEGrenade, CanDeploy)

BOOL EXT_FUNC CHEGrenade::__API_HOOK(CanDeploy)()
{
	return m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0;
}
