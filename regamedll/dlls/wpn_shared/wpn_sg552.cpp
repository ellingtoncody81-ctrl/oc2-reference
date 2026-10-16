#include "precompiled.h"
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

LINK_ENTITY_TO_CLASS(weapon_sg552, CSG552, CCSSG552)

void CSG552::Spawn()
{
	Precache();
	m_iId = WEAPON_SG552;
	SET_MODEL(edict(), "models/w_sg552.mdl");
	m_iDefaultAmmo = SG552_DEFAULT_GIVE;
	oc2_weapon::ResetState(oc2_weapon::Get(oc2_weapon::SG553), m_flStartThrow, m_flReleaseThrow, m_flLastFire, m_flAccuracy, m_iShotsFired);
	m_bDelayFire = false;

#ifdef REGAMEDLL_API
	CSPlayerWeapon()->m_flBaseDamage = oc2_weapon::Get(oc2_weapon::SG553).damage;
#endif
	FallInit();
	CBasePlayerWeapon::Spawn();
}

void CSG552::Precache()
{
	PRECACHE_MODEL("models/v_sg552.mdl");
	PRECACHE_MODEL("models/p_sg552.mdl");
	PRECACHE_MODEL("models/w_sg552.mdl");
	PRECACHE_SOUND("weapons/sg552-1.wav");
	PRECACHE_SOUND("weapons/sg552-2.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/sg556/sg556_clipout.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/sg556/sg556_clipin.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/sg556/sg556_boltback.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/sg556/sg556_boltforward.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/sg556/sg556_draw.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement1.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement2.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement3.wav");
	m_iShell = PRECACHE_MODEL("models/rshell.mdl");
	m_usFireSG552 = PRECACHE_EVENT(1, "events/sg552.sc");
}

int CSG552::GetItemInfo(ItemInfo *p)
{
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::SG553);
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "556Nato";
	p->iMaxAmmo1 = params.reserve;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = params.clip;
	p->iSlot = 0;
	p->iPosition = 10;
	p->iId = m_iId = WEAPON_SG552;
	p->iFlags = 0;
	p->iWeight = SG552_WEIGHT;
	return 1;
}

BOOL CSG552::Deploy()
{
	oc2_weapon::ResetState(oc2_weapon::Get(oc2_weapon::SG553), m_flStartThrow, m_flReleaseThrow, m_flLastFire, m_flAccuracy, m_iShotsFired);
	m_bDelayFire = false;
	iShellOn = 1;
	return DefaultDeploy("models/v_sg552.mdl", "models/p_sg552.mdl", SG552_DRAW, "mp5", UseDecrement() != FALSE);
}

void CSG552::SecondaryAttack()
{
	// weapon_sg556 (SG553) schema: one 45-degree sight, not old SG552's 55.
	m_pPlayer->pev->fov = m_pPlayer->m_iFOV = m_pPlayer->pev->fov == DEFAULT_FOV ? 45 : DEFAULT_FOV;
#ifndef CLIENT_DLL
	if (TheBots) TheBots->OnEvent(EVENT_WEAPON_ZOOMED, m_pPlayer);
	m_pPlayer->ResetMaxSpeed();
#endif
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + .3f;
}

void CSG552::PrimaryAttack()
{
	// Source generic gun uses primary cycle even while SG553 is sighted.
	SG552Fire(0, oc2_weapon::Get(oc2_weapon::SG553).cycle, FALSE);
}

void CSG552::SG552Fire(float flSpread, float flCycleTime, BOOL fUseAutoAim)
{
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
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::SG553);
	const int mode = m_pPlayer->pev->fov != DEFAULT_FOV ? 1 : 0;
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
		BULLET_PLAYER_556MM, damage, params.rangeModifier, m_pPlayer->pev, false, m_pPlayer->random_seed);
#ifdef CLIENT_WEAPONS
	const int flag = FEV_NOTHOST;
#else
	const int flag = 0;
#endif
	// event_t fparams are quantized at 1/100; scale small offsets for transport.
	PLAYBACK_EVENT_FULL(flag, m_pPlayer->edict(), m_usFireSG552, 0, (float *)&g_vecZero, (float *)&g_vecZero,
		offset.x * 1024, offset.y * 1024, int(m_pPlayer->pev->punchangle.x * 100),
		int(m_pPlayer->pev->punchangle.y * 100), FALSE, mode != 0);
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

void CSG552::Reload()
{
	if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0) return;
	// Source generic reload follows SequenceDuration. This duration is read
	// from the deployed converted model's reload sequence, not an APK rule.
	if (DefaultReload(iMaxClip(), SG552_RELOAD, 2.766666667f))
	{
		if (m_pPlayer->pev->fov != DEFAULT_FOV) SecondaryAttack();
#ifndef CLIENT_DLL
		m_pPlayer->SetAnimation(PLAYER_RELOAD);
#endif
		m_iShotsFired = 0;
		m_bDelayFire = false;
	}
}

void CSG552::WeaponIdle()
{
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::SG553);
	const int mode = m_pPlayer->pev->fov != DEFAULT_FOV ? 1 : 0;
	const oc2_weapon::Sample shot = oc2_weapon::SampleShot(params, OC2Motion(m_pPlayer), mode,
		gpGlobals->time, m_flStartThrow / 1024, m_flReleaseThrow / 64, m_flLastFire);
	m_flAccuracy = oc2_weapon::Clamp(shot.inaccuracy + params.spread[mode], 0, 1);
	ResetEmptySound();
	m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
	if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 20.0f;
	SendWeaponAnim(SG552_IDLE1, UseDecrement() != FALSE);
}

float CSG552::GetMaxSpeed()
{
	return m_pPlayer->pev->fov == DEFAULT_FOV ? SG552_MAX_SPEED : SG552_MAX_SPEED_ZOOM;
}
