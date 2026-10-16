#include "stdafx.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "wpn_molotov.h"
#include <cmath>

extern int g_iFreezeTimeOver;

LINK_ENTITY_TO_CLASS(weapon_molotov, CMolotovGrenade)

namespace
{
enum { IDLE, PULLPIN, THROW, DRAW, UNDERHAND };
float Strength(float value) { return std::isfinite(value) ? max(0.0f, min(value, 1.0f)) : 1.0f; }
}

void CMolotovGrenade::Spawn()
{
	Precache();
	m_iId = WEAPON_MOLOTOV;
	SET_MODEL(edict(), "models/w_molotov.mdl");
	m_iDefaultAmmo = 1;
	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
	m_flLastFire = 1;
	m_fMaxSpeed = 250;
	FallInit();
}

void CMolotovGrenade::Precache()
{
	PRECACHE_MODEL("models/v_molotov.mdl");
	PRECACHE_MODEL("models/p_molotov.mdl");
	PRECACHE_MODEL("models/w_molotov.mdl");
	PRECACHE_MODEL("models/v_incendiary.mdl");
	PRECACHE_MODEL("models/p_incendiary.mdl");
	PRECACHE_MODEL("models/w_incendiary.mdl");
	PRECACHE_SOUND("weapons/Zshiryu777/molotov/draw.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/molotov/pinpull.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/molotov/throw.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/smokegrenade/pinpull_start.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/smokegrenade/pinpull.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/smokegrenade/grenade_throw.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/smokegrenade/smokegrenade_draw.wav");
}

int CMolotovGrenade::GetItemInfo(ItemInfo *info)
{
	info->pszName = "weapon_molotov";
	info->pszAmmo1 = "Molotov";
	info->iMaxAmmo1 = 1;
	info->pszAmmo2 = nullptr;
	info->iMaxAmmo2 = -1;
	info->iMaxClip = WEAPON_NOCLIP;
	info->iSlot = 3;
	info->iPosition = 4;
	info->iId = m_iId = WEAPON_MOLOTOV;
	info->iWeight = 2;
	info->iFlags = ITEM_FLAG_LIMITINWORLD | ITEM_FLAG_EXHAUSTIBLE;
	return 1;
}

BOOL CMolotovGrenade::CanDeploy()
{
	return m_pPlayer && m_iPrimaryAmmoType >= 0 && m_iPrimaryAmmoType < MAX_AMMO_SLOTS && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0;
}

BOOL CMolotovGrenade::Deploy()
{
	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
	m_flLastFire = 1;
	m_fMaxSpeed = 250;
	const BOOL result = DefaultDeploy(IsIncendiary() ? "models/v_incendiary.mdl" : "models/v_molotov.mdl",
		IsIncendiary() ? "models/p_incendiary.mdl" : "models/p_molotov.mdl", DRAW, "grenade", UseDecrement() != FALSE);
	if (result)
	{
		// The CT and T resources have different draw durations.
	}
	return result;
}

void CMolotovGrenade::Holster(int skiplocal)
{
	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
	m_flLastFire = 1;
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5f;
	if (m_iPrimaryAmmoType >= 0 && m_iPrimaryAmmoType < MAX_AMMO_SLOTS && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
	{
		m_pPlayer->pev->weapons &= ~(1u << WEAPON_MOLOTOV);
		DestroyItem();
	}
}

void CMolotovGrenade::BeginHold(bool underhand)
{
	if (!CanDeploy() || m_flStartThrow != 0 || m_flReleaseThrow == -2 ||
		!m_pPlayer->IsAlive() || !m_pPlayer->m_bCanShoot || !g_iFreezeTimeOver) return;
	m_flStartThrow = max(0.0001f, gpGlobals->time);
	m_flReleaseThrow = 0;
	m_flLastFire = underhand ? 0.0f : 1.0f;
	SendWeaponAnim(PULLPIN, UseDecrement() != FALSE);
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.1f;
}

void CMolotovGrenade::PrimaryAttack() { BeginHold(false); }
void CMolotovGrenade::SecondaryAttack() { BeginHold(true); }

void CMolotovGrenade::ItemPostFrame()
{
	if (!m_pPlayer || !m_pPlayer->IsAlive()) return;
	const float now = gpGlobals->time;
	if (!std::isfinite(now) || !std::isfinite(m_flStartThrow) || !std::isfinite(m_flReleaseThrow))
	{
		m_flStartThrow = 0; m_flReleaseThrow = -1; m_flLastFire = 1; return;
	}
	const int buttons = m_pPlayer->pev->button & (IN_ATTACK | IN_ATTACK2);
	if (m_flStartThrow > 0)
	{
		if (m_flReleaseThrow == 0)
		{
			if (buttons)
			{
				const float desired = buttons == (IN_ATTACK | IN_ATTACK2) ? 0.5f : (buttons & IN_ATTACK) ? 1.0f : 0.0f;
				const float delta = max(0.0f, min(0.25f, now - m_flStartThrow)) * 1.3f;
				const float strength = Strength(m_flLastFire);
				m_flLastFire = strength < desired ? min(desired, strength + delta) : max(desired, strength - delta);
				m_flStartThrow = max(0.0001f, now);
			}
			else
			{
				m_flReleaseThrow = now + 0.1f;
				SendWeaponAnim(Strength(m_flLastFire) <= 0.33f ? UNDERHAND : THROW, UseDecrement() != FALSE);
				m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.75f;
			}
		}
		else if (m_flReleaseThrow > 0 && now >= m_flReleaseThrow)
			EmitThrow();
		return;
	}
	if (m_flReleaseThrow == -2) { WeaponIdle(); return; }
	if ((buttons & IN_ATTACK) && m_flNextPrimaryAttack <= UTIL_WeaponTimeBase()) PrimaryAttack();
	else if ((buttons & IN_ATTACK2) && m_flNextSecondaryAttack <= UTIL_WeaponTimeBase()) SecondaryAttack();
	else if (!buttons) WeaponIdle();
}

void CMolotovGrenade::EmitThrow()
{
	// Prediction consumes exactly once; projectile creation/physics/damage are
	// authoritative on the paired server. State uses existing weapondata fields.
	if (m_iPrimaryAmmoType >= 0 && m_iPrimaryAmmoType < MAX_AMMO_SLOTS && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0)
		--m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
	m_flStartThrow = 0;
	m_flReleaseThrow = -2;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.65f;
}

void CMolotovGrenade::WeaponIdle()
{
	if (m_flStartThrow > 0 || m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
	if (!CanDeploy()) { RetireWeapon(); return; }
	if (m_flReleaseThrow == -2)
	{
		m_flReleaseThrow = -1;
		SendWeaponAnim(DRAW, UseDecrement() != FALSE);
		m_pPlayer->m_flNextAttack = m_flNextPrimaryAttack = m_flNextSecondaryAttack =
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + AnimationDuration(DRAW,0.85f);
	}
	else
	{
		SendWeaponAnim(IDLE, UseDecrement() != FALSE);
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.0f;
	}
}
