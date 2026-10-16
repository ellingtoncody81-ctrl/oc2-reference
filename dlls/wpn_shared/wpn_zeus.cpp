#include "stdafx.h"
#include "cbase.h"
#include "player.h"
#include "weapons.h"
#include "wpn_zeus.h"

LINK_ENTITY_TO_CLASS(weapon_zeus, CZeus)

namespace
{
enum ZeusAnimation { ZEUS_IDLE, ZEUS_DRAW, ZEUS_FIRE };
const float kCycle = 0.15f; // CS:GO items_game.txt inherited cycletime.
}

void CZeus::Spawn()
{
	pev->classname = MAKE_STRING("weapon_zeus");
	Precache();
	m_iId = WEAPON_TASER;
	m_iClip = 1;
	m_iDefaultAmmo = 1;
	m_iWeaponState = 0;
	m_iShotsFired = 0;
	m_flLastFire = 0;
	m_flAccuracy = 0.001f;
	m_fMaxSpeed = 220;
	FallInit();
}

void CZeus::Precache()
{
	PRECACHE_MODEL("models/v_zeus.mdl");
	PRECACHE_MODEL("models/p_zeus.mdl");
	PRECACHE_MODEL("models/w_zeus.mdl");
}

int CZeus::GetItemInfo(ItemInfo *info)
{
	info->pszName = "weapon_zeus";
	info->pszAmmo1 = "TaserCharge";
	info->iMaxAmmo1 = 0;
	info->pszAmmo2 = nullptr;
	info->iMaxAmmo2 = -1;
	info->iMaxClip = 1;
	info->iSlot = 2;
	info->iPosition = 2;
	info->iFlags = ITEM_FLAG_EXHAUSTIBLE | ITEM_FLAG_NOAUTORELOAD | ITEM_FLAG_NOAUTOSWITCHEMPTY;
	info->iId = m_iId = WEAPON_TASER;
	info->iWeight = 5;
	return 1;
}

BOOL CZeus::Deploy()
{
	if (!m_pPlayer || m_iClip <= 0) return FALSE;
	m_iWeaponState &= ~WPNSTATE_SHIELD_DRAWN;
	m_pPlayer->m_bShieldDrawn = false;
	m_fMaxSpeed = 220;
	if (!DefaultDeploy("models/v_zeus.mdl", "models/p_zeus.mdl", ZEUS_DRAW, "onehanded", UseDecrement())) return FALSE;
	// Matched to the server's GoldSrc timing adapter and verified model sequence.
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 1.0f;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 1.0f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.0f;
	return TRUE;
}

void CZeus::Holster(int skiplocal)
{
	if (m_pPlayer) CBasePlayerWeapon::Holster(skiplocal);
}

void CZeus::ItemPostFrame()
{
	// Server owns inventory removal and the next weapon. Predicting a local
	// deletion would disagree with authoritative m_iId during network latency.
	if (!m_pPlayer || m_iClip <= 0) return;
	CBasePlayerWeapon::ItemPostFrame();
}

void CZeus::PrimaryAttack()
{
	if (!m_pPlayer || m_iClip <= 0) return;
	--m_iClip;
	m_iShotsFired = 1;
	m_flLastFire = gpGlobals->time;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + kCycle;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5f;
	SendWeaponAnim(ZEUS_FIRE, UseDecrement());
	// Shot/hit sounds and the electric endpoint are emitted once by the server.
}

void CZeus::SecondaryAttack()
{
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + kCycle;
}

void CZeus::WeaponIdle()
{
	if (m_iClip <= 0 || m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
	SendWeaponAnim(ZEUS_IDLE, UseDecrement());
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 20.0f;
}
