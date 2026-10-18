#include "precompiled.h"
#include "oc2_timed_grenade.h"

LINK_ENTITY_TO_CLASS(weapon_smokegrenade, CSmokeGrenade, CCSSmokeGrenade)

void CSmokeGrenade::Spawn()
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	Precache();

	m_iId = WEAPON_SMOKEGRENADE;
	SET_MODEL(edict(), "models/w_smokegrenade.mdl");

	pev->dmg = 4;

	m_iDefaultAmmo = SMOKEGRENADE_DEFAULT_GIVE;
	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

	// Get ready to fall down
	FallInit();

	// extend
	CBasePlayerWeapon::Spawn();
}

void CSmokeGrenade::Precache()
{
	PRECACHE_MODEL("models/v_smokegrenade.mdl");
	PRECACHE_MODEL("models/shield/v_shield_smokegrenade.mdl");

	PRECACHE_SOUND("weapons/pinpull.wav");
	PRECACHE_SOUND("weapons/sg_explode.wav");

	m_usCreateSmoke = PRECACHE_EVENT(1, "events/createsmoke.sc");
}

int CSmokeGrenade::GetItemInfo(ItemInfo *p)
{
	auto info = GetWeaponInfo(WEAPON_SMOKEGRENADE);

	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "SmokeGrenade";

	p->iMaxAmmo1 = info ? info->maxRounds : MAX_AMMO_SMOKEGRENADE;
	p->iMaxClip = info ? info->gunClipSize : WEAPON_NOCLIP;

	p->pszAmmo2 = nullptr;
	p->iMaxAmmo2 = -1;
	p->iSlot = 3;
	p->iPosition = 3;
	p->iId = m_iId = WEAPON_SMOKEGRENADE;
	p->iWeight = SMOKEGRENADE_WEIGHT;
	p->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;

	return 1;
}

BOOL CSmokeGrenade::Deploy()
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;

	m_flReleaseThrow = -1;
	m_fMaxSpeed = SMOKEGRENADE_MAX_SPEED;

	m_pPlayer->m_bShieldDrawn = false;

	if (m_pPlayer->HasShield())
		return DefaultDeploy("models/shield/v_shield_smokegrenade.mdl", "models/shield/p_shield_smokegrenade.mdl", SMOKEGRENADE_DRAW, "shieldgren", UseDecrement() != FALSE);
	else
		return DefaultDeploy("models/v_smokegrenade.mdl", "models/p_smokegrenade.mdl", SMOKEGRENADE_DRAW, "grenade", UseDecrement() != FALSE);
}

void CSmokeGrenade::Holster(int skiplocal)
{
	oc2_grenade::Reset(m_flStartThrow,m_flReleaseThrow,m_flLastFire);
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;

	if (!m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
	{
		// no more smokegrenades!
		// clear the smokegrenade of bits for HUD
#ifndef REGAMEDLL_FIXES
		// Moved to DestroyItem()
		m_pPlayer->pev->weapons &= ~(1 << WEAPON_SMOKEGRENADE);
#endif

		DestroyItem();
	}

	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
}

void CSmokeGrenade::PrimaryAttack()
{
	oc2_grenade::Begin(*this,false);
}

bool CSmokeGrenade::ShieldSecondaryFire(int iUpAnim, int iDownAnim)
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

		m_fMaxSpeed = SMOKEGRENADE_MAX_SPEED;
		m_pPlayer->m_bShieldDrawn = false;
	}
	else
	{
		m_iWeaponState |= WPNSTATE_SHIELD_DRAWN;
		SendWeaponAnim(iUpAnim, UseDecrement() != FALSE);

		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shielded");

		m_fMaxSpeed = SMOKEGRENADE_MAX_SPEED_SHIELD;
		m_pPlayer->m_bShieldDrawn = true;
	}

	m_pPlayer->UpdateShieldCrosshair((m_iWeaponState & WPNSTATE_SHIELD_DRAWN) != WPNSTATE_SHIELD_DRAWN);
	m_pPlayer->ResetMaxSpeed();

	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.4f;
	m_flNextPrimaryAttack = GetNextAttackDelay(0.4);
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.6f;

	return true;
}

void CSmokeGrenade::SecondaryAttack()
{
	if (!ShieldSecondaryFire(SHIELDGREN_UP, SHIELDGREN_DOWN)) oc2_grenade::Begin(*this,true);
}

void CSmokeGrenade::SetPlayerShieldAnim()
{
	if (!m_pPlayer->HasShield())
		return;

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shield");
	else
		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shieldgren");
}

void CSmokeGrenade::ResetPlayerShieldAnim()
{
	if (!m_pPlayer->HasShield())
		return;

	if (m_iWeaponState & WPNSTATE_SHIELD_DRAWN)
	{
		Q_strlcpy(m_pPlayer->m_szAnimExtention, "shieldgren");
	}
}

void CSmokeGrenade::ItemPostFrame()
{
	oc2_grenade::PostFrame(*this,m_usCreateSmoke);
}

void CSmokeGrenade::WeaponIdle()
{
	oc2_grenade::IdleFrame(*this);
}

LINK_HOOK_CLASS_CHAIN3(BOOL, CBasePlayerWeapon, CSmokeGrenade, CanDeploy)

BOOL EXT_FUNC CSmokeGrenade::__API_HOOK(CanDeploy)()
{
	return m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] != 0;
}
