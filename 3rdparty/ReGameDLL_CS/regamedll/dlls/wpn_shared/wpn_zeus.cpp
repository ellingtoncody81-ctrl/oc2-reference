#include "precompiled.h"
#include "wpn_zeus.h"

#include <cmath>

// The API wrapper stays local to this new entity; no public ReAPI layout changes.
class CCSZeus : public CCSPlayerWeapon {};
LINK_ENTITY_TO_CLASS(weapon_zeus, CZeus, CCSZeus)

namespace
{
enum ZeusAnimation { ZEUS_IDLE, ZEUS_DRAW, ZEUS_FIRE };
const char *const kAmmo = "TaserCharge";
const char *const kShootSound = "weapons/csgo_ports/zeus/zeus_shoot.wav";
const char *const kHitSound = "weapons/csgo_ports/zeus/zeus_hit.wav";
const char *const kDrawSound = "weapons/csgo_ports/zeus/zeus_draw.wav";
// Local CS:GO items_game.txt:20823-20898, inherited defaults:15195/15241.
const float kDamage = 500.0f;
const float kRange = 190.0f;
const float kRangeModifier = 0.0049f;
const float kCycle = 0.15f;
const float kSpread = 0.002f;
const float kStand = 0.001f;
const float kMove = 0.001f;
const float kAir = 0.092959999f;
const float kAirInitial = 0.096620003f;
const float kLadder = 0.1195f;
const float kLand = 0.000175f;
const float kRecoveryStand = 0.345388f;
const float kRecoveryCrouch = 0.287823f;
const float kDropDelay = 0.5f; // weapon_taser.cpp:151-164.

float Clamp(float value, float low, float high)
{
	return value < low ? low : value > high ? high : value;
}
}

TYPEDESCRIPTION CZeus::m_SaveData[] =
{
	DEFINE_FIELD(CZeus, m_flLastFire, FIELD_TIME),
	DEFINE_FIELD(CZeus, m_iShotsFired, FIELD_INTEGER),
	DEFINE_FIELD(CZeus, m_flAccuracy, FIELD_FLOAT),
	DEFINE_FIELD(CZeus, m_flAccuracyUpdateTime, FIELD_TIME),
	DEFINE_FIELD(CZeus, m_flPreviousFallSpeed, FIELD_FLOAT),
	DEFINE_FIELD(CZeus, m_bWasAirborne, FIELD_BOOLEAN),
};

int CZeus::Save(CSave &save)
{
	if (!CBasePlayerWeapon::Save(save)) return 0;
	return save.WriteFields("ZEUS", this, m_SaveData, ARRAYSIZE(m_SaveData));
}

int CZeus::Restore(CRestore &restore)
{
	if (!CBasePlayerWeapon::Restore(restore)) return 0;
	const int result = restore.ReadFields("ZEUS", this, m_SaveData, ARRAYSIZE(m_SaveData));
	// The beam model index belongs to this map's precache table, not a save.
	Precache();
	return result;
}

void CZeus::Spawn()
{
	Precache();
	m_iId = WEAPON_TASER;
	SET_MODEL(edict(), "models/w_zeus.mdl");
	m_iClip = 0; // AddPrimaryAmmo transfers the sole default round into this clip.
	m_iDefaultAmmo = 1;
	m_iWeaponState = 0;
	m_iShotsFired = 0;
	m_flLastFire = 0;
	m_flAccuracy = kStand;
	m_flAccuracyUpdateTime = gpGlobals->time;
	m_flPreviousFallSpeed = 0;
	m_bWasAirborne = FALSE;
	m_fMaxSpeed = 220.0f;
#ifdef REGAMEDLL_API
	CSPlayerWeapon()->m_flBaseDamage = kDamage;
#endif
	FallInit();
	CBasePlayerWeapon::Spawn();
}

void CZeus::Precache()
{
	PRECACHE_MODEL("models/v_zeus.mdl");
	PRECACHE_MODEL("models/p_zeus.mdl");
	PRECACHE_MODEL("models/w_zeus.mdl");
	PRECACHE_SOUND(kShootSound);
	PRECACHE_SOUND(kHitSound);
	PRECACHE_SOUND(kDrawSound);
	m_iBeamSprite = PRECACHE_MODEL("sprites/lgtning.spr");
}

int CZeus::GetItemInfo(ItemInfo *info)
{
	info->pszName = "weapon_zeus";
	info->pszAmmo1 = kAmmo;
	info->iMaxAmmo1 = 0;
	info->pszAmmo2 = nullptr;
	info->iMaxAmmo2 = -1;
	info->iMaxClip = 1;
	info->iSlot = 2; // CS:GO melee slot, alongside the knife rather than replacing it.
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
	m_fMaxSpeed = 220.0f;
	m_flAccuracy = kStand;
	m_flAccuracyUpdateTime = gpGlobals->time;
	m_bWasAirborne = !(m_pPlayer->pev->flags & FL_ONGROUND);
	m_flPreviousFallSpeed = m_pPlayer->pev->velocity.z;
	if (!DefaultDeploy("models/v_zeus.mdl", "models/p_zeus.mdl", ZEUS_DRAW, "onehanded", UseDecrement())) return FALSE;
	// GoldSrc presentation timing: the verified draw animation is 31 frames/30fps.
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 1.0f;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 1.0f;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.0f;
	// The draw cue must not share CHAN_ITEM with the purchase cue: the same
	// entity and channel would replace items/gunpickup2.wav as soon as the
	// Zeus is bought, and the purchase cue would never be heard.
	EMIT_SOUND(m_pPlayer->edict(), CHAN_WEAPON, kDrawSound, 0.3f, ATTN_NORM);
	UpdateAccuracy();
	return TRUE;
}

void CZeus::Holster(int skiplocal)
{
	if (!m_pPlayer) return;
	CBasePlayerWeapon::Holster(skiplocal);
	// CS:GO removes an exhausted taser immediately when switching away.
	// GoldSrc discards the spent item instead of leaving an unusable world pickup.
	if (m_iClip <= 0) DestroyItem();
}

void CZeus::RemoveSpent()
{
	CBasePlayer *owner = m_pPlayer;
	if (!owner || m_iClip > 0) return;
	if (owner->m_pActiveItem == this) RetireWeapon();
	// RetireWeapon may already have called our Holster/DestroyItem.
	if (owner->HasPlayerItem(this)) DestroyItem();
}

void CZeus::ItemPostFrame()
{
	if (!m_pPlayer) return;
	if (m_iClip <= 0)
	{
		if (gpGlobals->time >= m_flLastFire + kDropDelay) RemoveSpent();
		return;
	}
	UpdateAccuracy();
	CBasePlayerWeapon::ItemPostFrame();
}

void CZeus::UpdateAccuracy()
{
	// weapon_csbase.cpp:3812-3867/3886-3945. Use GoldSrc's held-player
	// velocity/ground state and elapsed server time in place of Source ticks.
	const bool ladder = m_pPlayer->pev->movetype == MOVETYPE_FLY;
	const bool air = !(m_pPlayer->pev->flags & FL_ONGROUND) && !ladder;
	if (!air && !ladder && m_bWasAirborne)
		m_flAccuracy += kLand * Q_max(0.0f, -m_flPreviousFallSpeed);
	const float floor = ladder ? kLadder*2.0f : air ? kStand+kAir : kStand;
	const float recovery = ladder ? kRecoveryStand : air ? kRecoveryCrouch*4.0f
		: (m_pPlayer->pev->flags & FL_DUCKING) ? kRecoveryCrouch : kRecoveryStand;
	const float dt = Q_max(0.0f, gpGlobals->time - m_flAccuracyUpdateTime);
	if (floor > m_flAccuracy) m_flAccuracy = floor;
	else m_flAccuracy = floor + (m_flAccuracy-floor)*std::exp(-dt*std::log(10.0f)/recovery);
	m_flAccuracyUpdateTime = gpGlobals->time;
	m_bWasAirborne = air ? TRUE : FALSE;
	m_flPreviousFallSpeed = m_pPlayer->pev->velocity.z;
}

float CZeus::ShotInaccuracy() const
{
	// weapon_csbase.cpp:1260-1339: two instantaneous movement penalties.
	float accuracy = m_flAccuracy;
	float movement = Clamp((m_pPlayer->pev->velocity.Length2D()-220.0f*0.34f)/(220.0f*(0.95f-0.34f)), 0, 1);
	// IN_RUN is GoldSrc's +speed/walk input, adapting Source m_bIsWalking.
	if (!(m_pPlayer->pev->button & IN_RUN)) movement = std::pow(movement, 0.25f);
	accuracy += movement*kMove;
	if (!(m_pPlayer->pev->flags & FL_ONGROUND))
	{
		// CS:GO cs_gamemovement.cpp:51 default sv_jump_impulse.
		const float referenceJump = std::sqrt(301.993377f);
		const float vertical = std::sqrt(std::fabs(m_pPlayer->pev->velocity.z));
		accuracy += Clamp((vertical-referenceJump*0.25f)/(referenceJump*0.75f)*kAirInitial, 0, 2*kAirInitial);
	}
	return Clamp(accuracy, 0, 1);
}

void CZeus::PrimaryAttack()
{
	if (!m_pPlayer || m_iClip <= 0) return;
	UpdateAccuracy();
	--m_iClip;
	m_iShotsFired = 1;
	m_flLastFire = gpGlobals->time;
	m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + kCycle;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + kDropDelay;
	SendWeaponAnim(ZEUS_FIRE, UseDecrement());
	m_pPlayer->SetAnimation(PLAYER_ATTACK1);
	m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = 0;
	EMIT_SOUND(m_pPlayer->edict(), CHAN_WEAPON, kShootSound, 0.35f, ATTN_NORM);

	UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
	const Vector forward = gpGlobals->v_forward, right = gpGlobals->v_right, up = gpGlobals->v_up;
	const Vector source = m_pPlayer->GetGunPosition();
	// fx_cs_shared.cpp:414-490 samples two polar disks. The GoldSrc shared
	// RNG replaces Source's stream, retaining the same density and magnitudes.
	const unsigned int seed = m_pPlayer->random_seed;
	const float radius0 = UTIL_SharedRandomFloat(seed, 0, 1)*ShotInaccuracy();
	const float angle0 = UTIL_SharedRandomFloat(seed+1, 0, 6.28318530718f);
	const float radius1 = UTIL_SharedRandomFloat(seed+2, 0, 1)*kSpread;
	const float angle1 = UTIL_SharedRandomFloat(seed+3, 0, 6.28318530718f);
	const float dx = radius0*std::cos(angle0) + radius1*std::cos(angle1);
	const float dy = radius0*std::sin(angle0) + radius1*std::sin(angle1);
	const Vector direction = (forward + right*dx + up*dy).Normalize();
	TraceResult trace;
	UTIL_TraceLine(source, source + direction*kRange, dont_ignore_monsters, m_pPlayer->edict(), &trace);
	const bool hit = trace.flFraction < 1.0f && !trace.fAllSolid;
	if(m_pPlayer->IsBot()&&CVAR_GET_FLOAT("developer")>0)
		ALERT(at_console,"oc2zeus_shot: t=%.3f player=%d clip=%d hit=%d target=%d distance=%.2f\n",
			gpGlobals->time,m_pPlayer->entindex(),m_iClip,hit,hit&&!FNullEnt(trace.pHit)?ENTINDEX(trace.pHit):0,trace.flFraction*kRange);
	// The short muzzle offset is a GoldSrc visual adapter only; the damage
	// trace and its 190-unit range originate at the unshifted eye/gun position.
	ElectricEffect(source + forward*12 + right*3 - up*3, trace.vecEndPos, hit);
	if (hit && !FNullEnt(trace.pHit))
	{
		CBaseEntity *target = CBaseEntity::Instance(trace.pHit);
		if (target && target->pev->takedamage != DAMAGE_NO)
		{
			// Source TraceAttack checks an actual shield before its shock branch.
			// Keep that obstruction even though ordinary armor is bypassed.
			bool blocked = false;
			if (target->IsPlayer())
			{
				Vector incoming = direction;
				blocked = static_cast<CBasePlayer *>(target)->IsHittingShield(incoming, &trace);
			}
			const float distance = Clamp(trace.flFraction, 0, 1)*kRange;
			const float damage = kDamage*std::pow(kRangeModifier, distance/500.0f);
			// cs_player_shared.cpp:2224-2228 / cs_player.cpp:6915: shock has no
			// hit-location multiplier or blood. The paired player adapter handles
			// the generic hitgroup/armor exception and preserves team damage rules.
			if (!blocked)
			{
				target->TakeDamage(pev, m_pPlayer->pev, damage, DMG_SHOCK | DMG_NEVERGIB);
				EMIT_SOUND(target->edict(), CHAN_STATIC, kHitSound, 0.6f, ATTN_NORM);
			}
		}
	}
	m_flAccuracy += 0.022120001f;
}

void CZeus::ElectricEffect(const Vector &source, const Vector &end, bool hit)
{
	// GoldSrc visual adaptation of CS:GO's weapon_tracers_taser particle.
	// Lifetime/width/noise/color are presentation choices, not weapon damage data.
	MESSAGE_BEGIN(MSG_BROADCAST, SVC_TEMPENTITY);
		WRITE_BYTE(TE_BEAMPOINTS);
		WRITE_COORD(source.x); WRITE_COORD(source.y); WRITE_COORD(source.z);
		WRITE_COORD(end.x); WRITE_COORD(end.y); WRITE_COORD(end.z);
		WRITE_SHORT(m_iBeamSprite);
		WRITE_BYTE(0); WRITE_BYTE(20); WRITE_BYTE(2); WRITE_BYTE(6); WRITE_BYTE(16);
		WRITE_BYTE(140); WRITE_BYTE(190); WRITE_BYTE(255); WRITE_BYTE(230); WRITE_BYTE(20);
	MESSAGE_END();
	if (hit) UTIL_Sparks(end);
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
