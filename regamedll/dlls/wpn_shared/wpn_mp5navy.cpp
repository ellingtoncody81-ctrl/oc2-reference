#include "precompiled.h"
#include "oc2_weapon_params.h"
#include "oc2_gunshot.h"

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

LINK_ENTITY_TO_CLASS(weapon_mp5navy, CMP5N, CCSMP5N)

void CMP5N::Spawn()
{
	Precache();
	m_iId = WEAPON_MP5N;
	SET_MODEL(edict(), "models/w_mp7.mdl");
	m_iDefaultAmmo = MP5NAVY_DEFAULT_GIVE;
	oc2_weapon::ResetState(oc2_weapon::Get(oc2_weapon::MP7), m_flStartThrow, m_flReleaseThrow, m_flLastFire, m_flAccuracy, m_iShotsFired);
	m_bDelayFire = false;

#ifdef REGAMEDLL_API
	CSPlayerWeapon()->m_flBaseDamage = oc2_weapon::Get(oc2_weapon::MP7).damage;
#endif
	FallInit();
	CBasePlayerWeapon::Spawn();
}

void CMP5N::Precache()
{
	PRECACHE_MODEL("models/v_mp7.mdl");
	PRECACHE_MODEL("models/p_mp7.mdl");
	PRECACHE_MODEL("models/w_mp7.mdl");
	PRECACHE_SOUND("weapons/mp7-1.wav");
	PRECACHE_SOUND("weapons/mp7-2.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/mp7/mp7_clipout.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/mp7/mp7_clipin.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/mp7/mp7_slideback.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/mp7/mp7_slideforward.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/mp7/mp7_draw.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement1.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement2.wav");
	PRECACHE_SOUND("weapons/Zshiryu777/movement3.wav");
	m_iShell = PRECACHE_MODEL("models/pshell.mdl");
	m_usFireMP5N = PRECACHE_EVENT(1, "events/mp5n.sc");
}

int CMP5N::GetItemInfo(ItemInfo *p)
{
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::MP7);
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "9mm";
	p->iMaxAmmo1 = params.reserve;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = params.clip;
	p->iSlot = 0;
	p->iPosition = 7;
	p->iId = m_iId = WEAPON_MP5N;
	p->iFlags = 0;
	p->iWeight = MP5NAVY_WEIGHT;
	return 1;
}

BOOL CMP5N::Deploy()
{
	oc2_weapon::ResetState(oc2_weapon::Get(oc2_weapon::MP7), m_flStartThrow, m_flReleaseThrow, m_flLastFire, m_flAccuracy, m_iShotsFired);
	m_bDelayFire = false;
	iShellOn = 1;
	return DefaultDeploy("models/v_mp7.mdl", "models/p_mp7.mdl", MP5N_DRAW, "mp5", UseDecrement() != FALSE);
}

void CMP5N::PrimaryAttack()
{
	// Source generic gun takes its primary cycle from the item schema.
	MP5NFire(0, oc2_weapon::Get(oc2_weapon::MP7).cycle, FALSE);
}

void CMP5N::MP5NFire(float flSpread, float flCycleTime, BOOL fUseAutoAim)
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
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::MP7);
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
		BULLET_PLAYER_9MM, damage, params.rangeModifier, m_pPlayer->pev, false, m_pPlayer->random_seed);
#ifdef CLIENT_WEAPONS
	const int flag = FEV_NOTHOST;
#else
	const int flag = 0;
#endif
	// event_t fparams are quantized at 1/100; scale small offsets for transport.
	PLAYBACK_EVENT_FULL(flag, m_pPlayer->edict(), m_usFireMP5N, 0, (float *)&g_vecZero, (float *)&g_vecZero,
		offset.x * 1024, offset.y * 1024, int(m_pPlayer->pev->punchangle.x * 100),
		int(m_pPlayer->pev->punchangle.y * 100), FALSE, FALSE);
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

void CMP5N::Reload()
{
	if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0) return;
	// Source generic reload follows SequenceDuration. This duration is read
	// from the deployed converted model's reload sequence, not an APK rule.
	if (DefaultReload(iMaxClip(), MP5N_RELOAD, 3.133333333f))
	{
#ifndef CLIENT_DLL
		m_pPlayer->SetAnimation(PLAYER_RELOAD);
#endif
		m_iShotsFired = 0;
		m_bDelayFire = false;
	}
}

void CMP5N::WeaponIdle()
{
	const oc2_weapon::Params &params = oc2_weapon::Get(oc2_weapon::MP7);
	const int mode = 0;
	const oc2_weapon::Sample shot = oc2_weapon::SampleShot(params, OC2Motion(m_pPlayer), mode,
		gpGlobals->time, m_flStartThrow / 1024, m_flReleaseThrow / 64, m_flLastFire);
	m_flAccuracy = oc2_weapon::Clamp(shot.inaccuracy + params.spread[mode], 0, 1);
	ResetEmptySound();
	m_pPlayer->GetAutoaimVector(AUTOAIM_10DEGREES);
	if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase()) return;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 20.0f;
	SendWeaponAnim(MP5N_IDLE1, UseDecrement() != FALSE);
}

float CMP5N::GetMaxSpeed()
{
	return oc2_weapon::MaxSpeed(19,m_iWeaponState,m_pPlayer?m_pPlayer->pev->fov:90);
}
