#include "precompiled.h"
#include "wpn_molotov.h"
#include "oc2_inferno.h"
#include <cmath>

LINK_ENTITY_TO_CLASS(weapon_molotov, CMolotovGrenade, CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_incendiary, CMolotovGrenade, CCSPlayerWeapon)
LINK_ENTITY_TO_CLASS(weapon_incgrenade, CMolotovGrenade, CCSPlayerWeapon)

TYPEDESCRIPTION CMolotovGrenade::m_SaveData[] =
{
	DEFINE_FIELD(CMolotovGrenade, m_iWeaponState, FIELD_INTEGER),
};
IMPLEMENT_SAVERESTORE(CMolotovGrenade, CBasePlayerWeapon)

namespace
{
enum { IDLE, PULLPIN, THROW, DRAW, UNDERHAND };
float Strength(float value) { return std::isfinite(value) ? Q_max(0.0f, Q_min(value, 1.0f)) : 1.0f; }
bool FiniteVector(const Vector &v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
		std::fabs(v.x) <= 32768 && std::fabs(v.y) <= 32768 && std::fabs(v.z) <= 32768;
}
}

void CMolotovGrenade::Spawn()
{
	// Both variants use one canonical inventory classname and ammo pool. The
	// physical item's state survives a weaponbox transfer, irrespective of team.
	m_iWeaponState = FClassnameIs(pev, "weapon_incendiary") || FClassnameIs(pev, "weapon_incgrenade") ? INCENDIARY_STATE : 0;
	// GiveCopyItem clones entvars and points chain at its source before Spawn.
	CBaseEntity *source = pev->chain ? CBaseEntity::Instance(pev->chain) : nullptr;
	if (source && FClassnameIs(source->pev, "weapon_molotov"))
	{
		CBasePlayerWeapon *weapon = static_cast<CBasePlayerWeapon *>(source);
		if (weapon->m_iId == WEAPON_MOLOTOV) m_iWeaponState = weapon->m_iWeaponState & INCENDIARY_STATE;
	}
	MAKE_STRING_CLASS("weapon_molotov", pev);
	Precache();
	m_iId = WEAPON_MOLOTOV;
	SET_MODEL(edict(), IsIncendiary() ? "models/w_incendiary.mdl" : "models/w_molotov.mdl");
	m_iDefaultAmmo = 1;
	m_flStartThrow = 0;
	m_flReleaseThrow = -1;
	m_flLastFire = 1;
	m_fMaxSpeed = 250;
	FallInit();
	CBasePlayerWeapon::Spawn();
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
	OC2_InfernoPrecache();
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

void OC2_AdoptFireVariant(CBasePlayerWeapon *original, const CBasePlayerWeapon *incoming)
{
	if (!original || !incoming || original == incoming || original->m_iId != WEAPON_MOLOTOV || incoming->m_iId != WEAPON_MOLOTOV) return;
	original->m_iWeaponState = (original->m_iWeaponState & ~CMolotovGrenade::INCENDIARY_STATE)
		| (incoming->m_iWeaponState & CMolotovGrenade::INCENDIARY_STATE);
	original->m_flStartThrow = 0;
	original->m_flReleaseThrow = -1;
	if (original->m_pPlayer && original->m_pPlayer->m_pActiveItem == original) original->Deploy();
}

int CMolotovGrenade::AddDuplicate(CBasePlayerItem *original)
{
	CBasePlayerWeapon *weapon = original && original->m_iId == WEAPON_MOLOTOV ? static_cast<CBasePlayerWeapon *>(original) : nullptr;
	const int ammo = weapon ? weapon->PrimaryAmmoIndex() : -1;
	const bool wasEmpty = weapon && weapon->m_pPlayer && ammo >= 0 && ammo < MAX_AMMO_SLOTS && weapon->m_pPlayer->m_rgAmmo[ammo] == 0;
	const int result = CBasePlayerWeapon::AddDuplicate(original);
	if (result && wasEmpty && weapon->m_pPlayer->m_rgAmmo[ammo] > 0) OC2_AdoptFireVariant(weapon, this);
	return result;
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
		DestroyItem();
}

void CMolotovGrenade::BeginHold(bool underhand)
{
	if (!CanDeploy() || m_flStartThrow != 0 || m_flReleaseThrow == -2 ||
		!m_pPlayer->IsAlive() || !m_pPlayer->m_bCanShoot || g_pGameRules->IsFreezePeriod()) return;
	m_flStartThrow = Q_max(0.0001f, gpGlobals->time);
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
				const float delta = Q_max(0.0f, Q_min(0.25f, now - m_flStartThrow)) * 1.3f;
				const float strength = Strength(m_flLastFire);
				m_flLastFire = strength < desired ? Q_min(desired, strength + delta) : Q_max(desired, strength - delta);
				m_flStartThrow = Q_max(0.0001f, now);
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
	const float strength = Strength(m_flLastFire);
	Vector angles = m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle;
	if (!FiniteVector(angles) || !FiniteVector(m_pPlayer->pev->origin) ||
		!FiniteVector(m_pPlayer->pev->view_ofs) || !FiniteVector(m_pPlayer->pev->velocity))
	{
		m_flStartThrow = 0; m_flReleaseThrow = -1; return;
	}
	angles.x = Q_max(-90.0f, Q_min(90.0f, angles.x));
	angles.x -= 10.0f * (90.0f - std::fabs(angles.x)) / 90.0f;
	UTIL_MakeVectors(angles);
	const Vector forward = gpGlobals->v_forward;
	Vector source = m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs + Vector(0, 0, -12.0f * (1.0f-strength));
	if (!FiniteVector(source) || !FiniteVector(forward)) { m_flStartThrow = 0; m_flReleaseThrow = -1; return; }
	TraceResult trace;
	// GoldSrc has fixed hulls; a point trace plus the Source six-unit retreat
	// prevents close-wall embedding without pretending to expose a +/-2 hull API.
	UTIL_TraceLine(source, source + forward*22, dont_ignore_monsters, m_pPlayer->edict(), &trace);
	if (!FiniteVector(trace.vecEndPos)) { m_flStartThrow = 0; m_flReleaseThrow = -1; return; }
	source = trace.vecEndPos - forward*6;
	// Explicit GoldSrc adapter: base throw velocity750; no local econ schema.
	const Vector velocity = forward*(675.0f*(0.3f+0.7f*strength)) + m_pPlayer->pev->velocity*1.25f;
	CGrenade *projectile = m_pPlayer->ThrowGrenade(this, source, velocity, 2.0f);
	if (projectile)
	{
		if (m_iPrimaryAmmoType >= 0 && m_iPrimaryAmmoType < MAX_AMMO_SLOTS && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0)
			--m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
		m_pPlayer->SetAnimation(PLAYER_ATTACK1);
		m_pPlayer->Radio("%!MRAD_FIREINHOLE", "#Fire_in_the_hole");
	}
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
