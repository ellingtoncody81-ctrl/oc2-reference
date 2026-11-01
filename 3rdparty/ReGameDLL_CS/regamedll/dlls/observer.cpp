#include "precompiled.h"
#include "oc2_spec_stats.h"
#include "wpn_shared/wpn_molotov.h"
#include "hostage/hostage.h"

static oc2_stats::Accumulator g_oc2Stats;
void OC2_SpecStatsReset() { g_oc2Stats.Reset(); }
void OC2_SpecStatsClear(int slot) { g_oc2Stats.ClearPlayer(slot); }
void OC2_SpecStatsRound(bool full) { g_oc2Stats.NextRound(full); }
void OC2_SpecStatsParticipate(CBasePlayer *p)
{
	if (p && !p->has_disconnected && p->m_iJoiningState == JOINED && (p->m_iTeam == TERRORIST || p->m_iTeam == CT))
		g_oc2Stats.Participate(p->entindex());
}
void OC2_SpecStatsDamage(CBaseEntity *entity, CBasePlayer *victim, float damage)
{
	if (!entity) return;
	CBasePlayer *attacker = nullptr;
	if (entity->Classify() == CLASS_PLAYER) attacker = static_cast<CBasePlayer *>(entity);
	else if (entity->Classify() == CLASS_VEHICLE)
		attacker = static_cast<CBasePlayer *>(static_cast<CFuncVehicle *>(entity)->m_pDriver);
	if (!attacker || !victim || attacker == victim || attacker->has_disconnected || victim->has_disconnected ||
		(attacker->m_iTeam != TERRORIST && attacker->m_iTeam != CT) ||
		(victim->m_iTeam != TERRORIST && victim->m_iTeam != CT) || attacker->m_iTeam == victim->m_iTeam ||
		victim->pev->takedamage == DAMAGE_NO || !victim->IsAlive()) return;
	const float applied = damage < victim->pev->health ? damage : victim->pev->health;
	g_oc2Stats.Damage(attacker->entindex(), victim->entindex(), applied);
}
void OC2_SpecStatsDeath(CBasePlayer *victim, CBasePlayer *killer)
{
	if (!victim) return;
	int killerIndex = 0; unsigned int eligible = 0;
	if (killer && !killer->has_disconnected && killer != victim &&
		(killer->m_iTeam == TERRORIST || killer->m_iTeam == CT) &&
		(victim->m_iTeam == TERRORIST || victim->m_iTeam == CT) && killer->m_iTeam != victim->m_iTeam)
	{
		killerIndex = killer->entindex();
		for (int i = 1; i <= gpGlobals->maxClients && i <= 32; ++i) {
			CBasePlayer *p = UTIL_PlayerByIndex(i);
			if (p && !p->has_disconnected && p->m_iTeam == killer->m_iTeam) eligible |= 1u << (i - 1);
		}
	}
	g_oc2Stats.Death(killerIndex, victim->entindex(), eligible);
}

static int OC2_SpecGrenadeCount(CBasePlayer *p, WeaponIdType id)
{
	CBasePlayerItem *item = p->GetItemById(id);
	if (!item) return 0;
	const int ammo = item->PrimaryAmmoIndex();
	return ammo >= 0 && ammo < MAX_AMMO_SLOTS ? clamp(p->m_rgAmmo[ammo], 0, 255) : 0;
}

LINK_HOOK_CHAIN(int, GetForceCamera, (CBasePlayer *pObserver), pObserver)

int __API_HOOK(GetForceCamera)(CBasePlayer *pObserver)
{
	int retVal;

	if (fadetoblack.value != FADETOBLACK_STAY)
	{
		retVal = int(CVAR_GET_FLOAT("mp_forcechasecam"));

		if (retVal == CAMERA_MODE_SPEC_ANYONE)
			retVal = int(CVAR_GET_FLOAT("mp_forcecamera"));
	}
	else
		retVal = CAMERA_MODE_SPEC_ONLY_FIRST_PERSON;

	return retVal;
}

LINK_HOOK_CLASS_CHAIN(CBasePlayer *, CBasePlayer, Observer_IsValidTarget, (int iPlayerIndex, bool bSameTeam), iPlayerIndex, bSameTeam)

CBasePlayer *CBasePlayer::__API_HOOK(Observer_IsValidTarget)(int iPlayerIndex, bool bSameTeam)
{
	if (iPlayerIndex > gpGlobals->maxClients || iPlayerIndex < 1)
		return nullptr;

	CBasePlayer *pPlayer = UTIL_PlayerByIndex(iPlayerIndex);

	// Don't spec observers or players who haven't picked a class yet
	if (!pPlayer || pPlayer == this)
		return nullptr;

	if (pPlayer->has_disconnected)
		return nullptr;

	if (pPlayer->GetObserverMode() != OBS_NONE)
		return nullptr;

	if (pPlayer->pev->effects & EF_NODRAW)
		return nullptr;

	if (pPlayer->m_iTeam == UNASSIGNED || (bSameTeam && pPlayer->m_iTeam != m_iTeam))
		return nullptr;

	return pPlayer;
}

void UpdateClientEffects(CBasePlayer *pObserver, int oldMode)
{
	bool clearProgress = false;
	bool clearBlindness = false;
	bool blindnessOk = (fadetoblack.value != FADETOBLACK_STAY);
	bool clearNightvision = false;

	if (pObserver->GetObserverMode() == OBS_IN_EYE)
	{
		clearProgress = true;
		clearBlindness = true;
		clearNightvision = true;

		if (pObserver->m_hObserverTarget->IsPlayer())
		{
			CBasePlayer *pPlayer = UTIL_PlayerByIndex(pObserver->m_hObserverTarget->entindex());

			if (pPlayer)
			{
				if (pPlayer->m_progressStart && pPlayer->m_progressEnd > pPlayer->m_progressStart)
				{
					if (pPlayer->m_progressEnd > gpGlobals->time)
					{
						float percentRemaining = gpGlobals->time - pPlayer->m_progressStart;
						pObserver->SetProgressBarTime2(int(pPlayer->m_progressEnd - pPlayer->m_progressStart), percentRemaining);
						clearProgress = false;
					}
				}

				if (blindnessOk && pPlayer->m_blindStartTime && pPlayer->m_blindFadeTime)
				{
					float fadeTime, holdTime, alpha, ratio;
					float endTime = pPlayer->m_blindFadeTime + pPlayer->m_blindHoldTime + pPlayer->m_blindStartTime;

					if (endTime > gpGlobals->time)
					{
						clearBlindness = false;

						fadeTime = pPlayer->m_blindFadeTime;
						alpha = float(pPlayer->m_blindAlpha);
						holdTime = pPlayer->m_blindHoldTime + pPlayer->m_blindStartTime - gpGlobals->time;

						if (holdTime <= 0)
						{
							holdTime = 0;
							ratio = (endTime - gpGlobals->time) / fadeTime;
							alpha = pPlayer->m_blindAlpha * ratio;
							fadeTime = ratio * fadeTime;
						}

						UTIL_ScreenFade(pObserver, Vector(255, 255, 255), fadeTime, holdTime, alpha);
					}
				}

				clearNightvision = false;

				if (pPlayer->m_bNightVisionOn != pObserver->m_bNightVisionOn)
				{
					MESSAGE_BEGIN(MSG_ONE, gmsgNVGToggle, nullptr, pObserver->pev);
						WRITE_BYTE(pPlayer->m_bNightVisionOn ? STATUS_NIGHTVISION_ON : STATUS_NIGHTVISION_OFF);
					MESSAGE_END();

					pObserver->m_bNightVisionOn = pPlayer->m_bNightVisionOn;
				}
			}
		}
	}
	else if (oldMode == OBS_IN_EYE)
	{
		clearProgress = true;
		clearBlindness = true;
		clearNightvision = true;
	}

	if (clearProgress)
		pObserver->SetProgressBarTime(0);

	if (blindnessOk && clearBlindness)
		UTIL_ScreenFade(pObserver, Vector(0, 0, 0), 0.001);

	if (clearNightvision)
	{
		MESSAGE_BEGIN(MSG_ONE, gmsgNVGToggle, nullptr, pObserver->pev);
			WRITE_BYTE(STATUS_NIGHTVISION_OFF);
		MESSAGE_END();

		pObserver->m_bNightVisionOn = false;
	}
}

LINK_HOOK_CLASS_VOID_CHAIN(CBasePlayer, Observer_FindNextPlayer, (bool bReverse, const char *name), bReverse, name)

// Find the next client in the game for this player to spectate
void CBasePlayer::__API_HOOK(Observer_FindNextPlayer)(bool bReverse, const char *name)
{
	int iStart;
	int iCurrent;
	int iDir;
	bool bForceSameTeam;

	if (m_flNextFollowTime && m_flNextFollowTime > gpGlobals->time)
		return;

	m_flNextFollowTime = gpGlobals->time + 0.25f;

	iStart = m_hObserverTarget ? ENTINDEX(m_hObserverTarget->edict()) : ENTINDEX(edict());
	iCurrent = iStart;

	m_hObserverTarget = nullptr;

	iDir = bReverse ? -1 : 1;

	bForceSameTeam = (GetForceCamera(this) != CAMERA_MODE_SPEC_ANYONE && m_iTeam != SPECTATOR);

	do
	{
		iCurrent += iDir;

		// Loop through the clients
		if (iCurrent > gpGlobals->maxClients)
			iCurrent = 1;

		else if (iCurrent < 1)
			iCurrent = gpGlobals->maxClients;

		m_hObserverTarget = Observer_IsValidTarget(iCurrent, bForceSameTeam);

		if (m_hObserverTarget)
		{
			if (!name)
				break;

			CBasePlayer *pPlayer = UTIL_PlayerByIndex(m_hObserverTarget->entindex());
			if (!Q_strcmp(name, STRING(pPlayer->pev->netname)))
				break;
		}
	}
	while (iCurrent != iStart);

	// Did we find a target?
	if (m_hObserverTarget)
	{
		// Move to the target
		UTIL_SetOrigin(pev, m_hObserverTarget->pev->origin);

		if (m_hObserverTarget->pev->health < 0.0f)
			m_hObserverTarget->pev->health = 0.0f;

		MESSAGE_BEGIN(MSG_ONE, gmsgSpecHealth2, nullptr, pev);
			WRITE_BYTE(int(m_hObserverTarget->pev->health));
			WRITE_BYTE(ENTINDEX(m_hObserverTarget->edict()));
		MESSAGE_END();

		// Store the target in pev so the physics DLL can get to it
		if (pev->iuser1 != OBS_ROAMING)
			pev->iuser2 = ENTINDEX(m_hObserverTarget->edict());

		UpdateClientEffects(this, pev->iuser1);
	}
}

// Handle buttons in observer mode
void CBasePlayer::Observer_HandleButtons()
{
	// Slow down mouse clicks
	if (m_flNextObserverInput > gpGlobals->time)
		return;

	// Jump changes from modes: Chase to Roaming
	if (m_afButtonPressed & IN_JUMP)
	{
		switch (pev->iuser1)
		{
		case OBS_CHASE_LOCKED:
			Observer_SetMode(OBS_IN_EYE);
			break;
		case OBS_CHASE_FREE:
			Observer_SetMode(OBS_IN_EYE);
			break;
		case OBS_IN_EYE:
			Observer_SetMode(OBS_ROAMING);
			break;
		case OBS_ROAMING:
			Observer_SetMode(OBS_MAP_FREE);
			break;
		case OBS_MAP_FREE:
			Observer_SetMode(OBS_MAP_CHASE);
			break;
		default:
			Observer_SetMode(m_bObserverAutoDirector ? OBS_CHASE_LOCKED : OBS_CHASE_FREE);
			break;
		}

		m_flNextObserverInput = gpGlobals->time + 0.2f;
	}

	// Attack moves to the next player
	if (m_afButtonPressed & IN_ATTACK)
	{
		Observer_FindNextPlayer(false);
		m_flNextObserverInput = gpGlobals->time + 0.2f;
	}

	// Attack2 moves to the prev player
	if (m_afButtonPressed & IN_ATTACK2)
	{
		Observer_FindNextPlayer(true);
		m_flNextObserverInput = gpGlobals->time + 0.2f;
	}
}

void CBasePlayer::Observer_CheckTarget()
{
	if (pev->iuser1 == OBS_ROAMING && !m_bWasFollowing)
		return;

	if (m_bWasFollowing)
	{
		Observer_FindNextPlayer(false);

		if (m_hObserverTarget)
			Observer_SetMode(m_iObserverLastMode);
	}
	else
	{
		int lastMode;

		// try to find a traget if we have no current one
		if (!m_hObserverTarget)
			Observer_FindNextPlayer(false);

		if (m_hObserverTarget)
		{
			CBasePlayer *target = UTIL_PlayerByIndex(m_hObserverTarget->entindex());

			// check taget
			if (!target || target->pev->deadflag == DEAD_RESPAWNABLE)
			{
				Observer_FindNextPlayer(false);
			}
			else if (target->pev->effects & EF_NODRAW)
			{
#ifdef REGAMEDLL_FIXES
				bool bStillDying = (target->pev->deadflag == DEAD_DYING || (target->pev->deadflag == DEAD_DEAD && !target->HasTimePassedSinceDeath(2.0f)));
				if (!bStillDying || (target->m_afPhysicsFlags & PFLAG_OBSERVER)) // keep observing to victim until dying, even if it is invisible
#endif
					Observer_FindNextPlayer(false);
			}
			else if (target->pev->deadflag == DEAD_DEAD && target->HasTimePassedSinceDeath(2.0f))
			{
				// 2 secs after death change target
				Observer_FindNextPlayer(false);

				if (!m_hObserverTarget)
				{
					lastMode = pev->iuser1;
					Observer_SetMode(OBS_ROAMING);
					m_iObserverLastMode = lastMode;

					m_bWasFollowing = true;
				}
			}
		}
		// no target found at all
		else
		{
			lastMode = pev->iuser1;
			Observer_SetMode(OBS_ROAMING);	// don't overwrite users lastmode
			m_iObserverLastMode = lastMode;
		}
	}
}

void CBasePlayer::Observer_CheckProperties()
{
	// try to find a traget if we have no current one
	if (pev->iuser1 == OBS_IN_EYE && m_hObserverTarget)
	{
		CBasePlayer *target = UTIL_PlayerByIndex(m_hObserverTarget->entindex());

		if (!target)
			return;

		int weapon = target->m_pActiveItem ? target->m_pActiveItem->m_iId : 0;
		int targetBombState = STATUSICON_HIDE;

		// use fov of tracked client
		if (m_iFOV != target->m_iFOV || m_iObserverWeapon != weapon)
		{
			m_iClientFOV = m_iFOV = target->m_iFOV;

			// write fov before wepon data, so zoomed crosshair is set correctly
			MESSAGE_BEGIN(MSG_ONE, gmsgSetFOV, nullptr, pev);
				WRITE_BYTE(m_iFOV);
			MESSAGE_END();

			m_iObserverWeapon = weapon;

			//send weapon update
			MESSAGE_BEGIN(MSG_ONE, gmsgCurWeapon, nullptr, pev);
				WRITE_BYTE(1);	// 1 = current weapon, not on target
				WRITE_BYTE(m_iObserverWeapon);
				WRITE_BYTE(0);	// clip
			MESSAGE_END();
		}

		if (target->m_bHasC4)
		{
			if (target->m_signals.GetState() & SIGNAL_BOMB)
				targetBombState = STATUSICON_FLASH;
			else
				targetBombState = STATUSICON_SHOW;
		}

		if (m_iObserverC4State != targetBombState)
		{
			m_iObserverC4State = targetBombState;

			if (targetBombState)
			{
				MESSAGE_BEGIN(MSG_ONE, gmsgStatusIcon, nullptr, pev);
					WRITE_BYTE(m_iObserverC4State);
					WRITE_STRING("c4");
					WRITE_BYTE(0);
					WRITE_BYTE(160);
					WRITE_BYTE(0);
				MESSAGE_END();
			}
			else
			{
				MESSAGE_BEGIN(MSG_ONE, gmsgStatusIcon, nullptr, pev);
					WRITE_BYTE(STATUSICON_HIDE);
					WRITE_STRING("c4");
				MESSAGE_END();
			}
		}

		if (m_bObserverHasDefuser != target->m_bHasDefuser)
		{
			m_bObserverHasDefuser = target->m_bHasDefuser;

			if (target->m_bHasDefuser)
			{
				MESSAGE_BEGIN(MSG_ONE, gmsgStatusIcon, nullptr, pev);
					WRITE_BYTE(STATUSICON_SHOW);
					WRITE_STRING("defuser");
					WRITE_BYTE(0);
					WRITE_BYTE(160);
					WRITE_BYTE(0);
				MESSAGE_END();
			}
			else
			{
				MESSAGE_BEGIN(MSG_ONE, gmsgStatusIcon, nullptr, pev);
					WRITE_BYTE(STATUSICON_HIDE);
					WRITE_STRING("defuser");
				MESSAGE_END();
			}
		}
	}
	else
	{
		m_iFOV = DEFAULT_FOV;

		if (m_iObserverWeapon)
		{
			m_iObserverWeapon = 0;

			MESSAGE_BEGIN(MSG_ONE, gmsgCurWeapon, nullptr, pev);
				WRITE_BYTE(1);	// 1 = current weapon
				WRITE_BYTE(m_iObserverWeapon);
				WRITE_BYTE(0);	// clip
			MESSAGE_END();
		}

		if (m_iObserverC4State)
		{
			m_iObserverC4State = 0;

			MESSAGE_BEGIN(MSG_ONE, gmsgStatusIcon, nullptr, pev);
				WRITE_BYTE(0);
				WRITE_STRING("c4");
			MESSAGE_END();
		}

		if (m_bObserverHasDefuser)
		{
			m_bObserverHasDefuser = false;

			MESSAGE_BEGIN(MSG_ONE, gmsgStatusIcon, nullptr, pev);
				WRITE_BYTE(0);
				WRITE_STRING("defuser");
			MESSAGE_END();
		}
	}
}

// Attempt to change the observer mode
LINK_HOOK_CLASS_VOID_CHAIN(CBasePlayer, Observer_SetMode, (int iMode), iMode)

void EXT_FUNC CBasePlayer::__API_HOOK(Observer_SetMode)(int iMode)
{
	int forcecamera;
	int oldMode;

	// Just abort if we're changing to the mode we're already in
	if (iMode == pev->iuser1)
		return;

	forcecamera = GetForceCamera(this);

	// is valid mode ?
	if (iMode < OBS_CHASE_LOCKED || iMode > OBS_MAP_CHASE)
		iMode = OBS_IN_EYE;

	oldMode = pev->iuser1;

	if (m_iTeam != SPECTATOR)
	{
		if (forcecamera == CAMERA_MODE_SPEC_ONLY_TEAM)
		{
			if (iMode == OBS_ROAMING)
				iMode = OBS_MAP_FREE;
		}
		else if (forcecamera == CAMERA_MODE_SPEC_ONLY_FIRST_PERSON)
			iMode = OBS_IN_EYE;
	}

	// verify observer target again
	if (m_hObserverTarget)
	{
#ifdef REGAMEDLL_FIXES
		m_hObserverTarget = Observer_IsValidTarget( ENTINDEX(m_hObserverTarget->edict()), forcecamera != CAMERA_MODE_SPEC_ANYONE );
#else
		CBasePlayer *pTarget = m_hObserverTarget;

		if (pTarget == this
			|| !pTarget
			|| pTarget->has_disconnected
			|| pTarget->GetObserverMode() != OBS_NONE
			|| (pTarget->pev->effects & EF_NODRAW)
			|| (forcecamera != CAMERA_MODE_SPEC_ANYONE && pTarget->m_iTeam != m_iTeam))
			m_hObserverTarget = nullptr;
#endif
	}

	// set spectator mode
	pev->iuser1 = iMode;

	// if we are not roaming, we need a valid target to track
	if (iMode != OBS_ROAMING && !m_hObserverTarget)
	{
		Observer_FindNextPlayer(false);

		// if we didn't find a valid target switch to roaming
		if (!m_hObserverTarget)
		{
			ClientPrint(pev, HUD_PRINTCENTER, "#Spec_NoTarget");
			pev->iuser1 = OBS_ROAMING;
		}
	}

	// set target if not roaming
	if (pev->iuser1 == OBS_ROAMING)
		pev->iuser2 = 0;
	else
		pev->iuser2 = ENTINDEX(m_hObserverTarget->edict());

	// clear second target from death cam
	pev->iuser3 = 0;

	if (m_hObserverTarget)
	{
		UTIL_SetOrigin(pev, m_hObserverTarget->pev->origin);
	}

#ifndef REGAMEDLL_FIXES
	MESSAGE_BEGIN(MSG_ONE, gmsgCrosshair, nullptr, pev);
		WRITE_BYTE((iMode == OBS_ROAMING) != 0);
	MESSAGE_END();
#endif

	UpdateClientEffects(this, oldMode);

	// print spepctaor mode on client screen

	char modemsg[16];
	Q_snprintf(modemsg, sizeof(modemsg), "#Spec_Mode%i", pev->iuser1);
	ClientPrint(pev, HUD_PRINTCENTER, modemsg);

	m_iObserverLastMode = iMode;
	m_bWasFollowing = false;
}

LINK_HOOK_CLASS_VOID_CHAIN2(CBasePlayer, Observer_Think)

void EXT_FUNC CBasePlayer::__API_HOOK(Observer_Think)()
{
	Observer_HandleButtons();
	Observer_CheckTarget();
	Observer_CheckProperties();

	// A private, target-tagged snapshot. Limit updates per observer to 10 Hz.
	static float nextUpdate[33] = {};
	static int lastTarget[33] = {};
	const int observer = entindex();
	if (observer < 1 || observer > 32)
		return;
	// Do not expose positions to dead team members subject to force-camera rules.
	// Unrestricted spectators, including normal offline play, get both teams.
	static float nextRadar[33] = {};
	if (nextRadar[observer] <= gpGlobals->time || nextRadar[observer] > gpGlobals->time + 0.5f)
	{
		nextRadar[observer] = gpGlobals->time + 0.25f;
		int aliveT = 0, aliveCT = 0;
		const bool allTeams = GetForceCamera(this) == CAMERA_MODE_SPEC_ANYONE;
		for (int i = 1; i <= gpGlobals->maxClients; ++i)
		{
			CBasePlayer *player = UTIL_PlayerByIndex(i);
			if (!player || player->has_disconnected || !player->IsAlive())
				continue;
			if (player->m_iTeam == TERRORIST) ++aliveT;
			else if (player->m_iTeam == CT) ++aliveCT;
			else continue;
			if (!allTeams && m_iTeam != SPECTATOR && player->m_iTeam != m_iTeam)
				continue;
			MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, gmsgOC2Radar, nullptr, pev);
				WRITE_BYTE(1);
				WRITE_BYTE(i);
				WRITE_BYTE(player->m_iTeam);
				WRITE_COORD(player->pev->origin.x);
				WRITE_COORD(player->pev->origin.y);
				WRITE_COORD(player->pev->origin.z);
			MESSAGE_END();
		}
		CHalfLifeMultiplay *rules = CSGameRules();
		if (rules)
		{
			MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, gmsgOC2Round, nullptr, pev);
				WRITE_BYTE(1);
				WRITE_SHORT(clamp(rules->m_iTotalRoundsPlayed, 0, 32767));
				WRITE_SHORT(clamp(rules->m_iMaxRounds, 0, 32767));
				WRITE_BYTE(aliveT);
				WRITE_BYTE(aliveCT);
			MESSAGE_END();
		}
	}
	// Stagger player rows to keep each observer's unreliable datagram bounded.
	static float nextRows[33] = {};
	static int nextSlot[33] = {};
	if (nextRows[observer] <= gpGlobals->time || nextRows[observer] > gpGlobals->time + 0.3f)
	{
		nextRows[observer] = gpGlobals->time + 0.1f;
		const bool allTeams = GetForceCamera(this) == CAMERA_MODE_SPEC_ANYONE || m_iTeam == SPECTATOR;
		for (int n = 0; n < 8 && n < gpGlobals->maxClients; ++n)
		{
			if (++nextSlot[observer] > gpGlobals->maxClients || nextSlot[observer] > 32) nextSlot[observer] = 1;
			const int i = nextSlot[observer];
			CBasePlayer *p = UTIL_PlayerByIndex(i);
			if (!p || p->has_disconnected || (p->m_iTeam != TERRORIST && p->m_iTeam != CT) ||
				(!allTeams && p->m_iTeam != m_iTeam)) continue;
			const bool alive = p->IsAlive();
			CBasePlayerItem *fire = alive ? p->GetItemById(WEAPON_MOLOTOV) : nullptr;
			int equip = alive ? ((p->m_bHasDefuser ? 1 : 0) | (p->m_bHasC4 ? 2 : 0) |
				(p->m_bHasNightVision ? 4 : 0) | (p->GetItemById(WEAPON_KNIFE) ? 8 : 0) |
				(p->GetItemById(WEAPON_TASER) ? 16 : 0) | (p->HasShield() ? 64 : 0)) : 0;
			// Keep bit 1 as ownership for both kits; unused bit 128 describes
			// the server's map mode, including before a CT purchases the kit.
			if (alive && p->m_iTeam == CT && OC2_HostageMap()) equip |= 128;
			if (fire && OC2_SpecGrenadeCount(p, WEAPON_MOLOTOV) > 0 &&
				(static_cast<CBasePlayerWeapon *>(fire)->m_iWeaponState & CMolotovGrenade::INCENDIARY_STATE)) equip |= 32;
			const oc2_stats::Player &stats = g_oc2Stats.Get(i);
			const int damage10 = int(clamp(stats.damage * 10.0f, 0.0f, 2147483000.0f));
			MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, gmsgOC2Row, nullptr, pev);
				WRITE_BYTE(1); WRITE_BYTE(i); WRITE_LONG(GETPLAYERUSERID(p->edict()));
				WRITE_BYTE(p->m_iTeam); WRITE_BYTE(alive ? 1 : 0);
				WRITE_SHORT(clamp(int(p->pev->health), 0, 32767)); WRITE_SHORT(clamp(int(p->pev->armorvalue), 0, 32767));
				WRITE_BYTE(alive && p->m_pActiveItem ? p->m_pActiveItem->m_iId : 0);
				WRITE_BYTE(alive ? p->m_iKevlar : ARMOR_NONE); WRITE_BYTE(equip);
				WRITE_SHORT(clamp(p->m_iAccount, 0, 32767)); WRITE_LONG(alive ? p->pev->weapons : 0);
				WRITE_SHORT(clamp(stats.kills, 0, 32767)); WRITE_SHORT(clamp(stats.assists, 0, 32767));
				WRITE_SHORT(clamp(stats.deaths, 0, 32767)); WRITE_LONG(damage10);
				WRITE_SHORT(clamp(stats.rounds, 0, 32767));
				WRITE_BYTE(alive ? OC2_SpecGrenadeCount(p, WEAPON_HEGRENADE) : 0);
				WRITE_BYTE(alive ? OC2_SpecGrenadeCount(p, WEAPON_FLASHBANG) : 0);
				WRITE_BYTE(alive ? OC2_SpecGrenadeCount(p, WEAPON_SMOKEGRENADE) : 0);
				WRITE_BYTE(alive ? OC2_SpecGrenadeCount(p, WEAPON_MOLOTOV) : 0);
			MESSAGE_END();
		}
	}
	CBasePlayer *target = m_hObserverTarget ? UTIL_PlayerByIndex(m_hObserverTarget->entindex()) : nullptr;
	if (!target || pev->iuser2 != target->entindex())
	{
		lastTarget[observer] = 0;
		return;
	}
	if (lastTarget[observer] == target->entindex() && nextUpdate[observer] > gpGlobals->time && nextUpdate[observer] <= gpGlobals->time + 0.2f)
		return;
	lastTarget[observer] = target->entindex();
	nextUpdate[observer] = gpGlobals->time + 0.1f;

	CBasePlayerWeapon *weapon = static_cast<CBasePlayerWeapon *>(target->m_pActiveItem);
	int clip = -1, reserve = -1;
	if (weapon)
	{
		clip = weapon->m_iClip;
		if (weapon->m_iPrimaryAmmoType >= 0 && weapon->m_iPrimaryAmmoType < MAX_AMMO_SLOTS)
			reserve = target->m_rgAmmo[weapon->m_iPrimaryAmmoType];
	}
	const int flags = (target->m_bHasDefuser ? 1 : 0) | (target->m_bHasC4 ? 2 : 0) |
		(target->m_bHasNightVision ? 4 : 0) | (target->m_iTeam == CT && OC2_HostageMap() ? 128 : 0);
	MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, gmsgOC2Spec, nullptr, pev);
		WRITE_BYTE(1); // protocol version
		WRITE_BYTE(target->entindex());
		WRITE_SHORT(clamp(int(target->pev->health), 0, 32767));
		WRITE_SHORT(clamp(int(target->pev->armorvalue), 0, 32767));
		WRITE_BYTE(weapon ? weapon->m_iId : 0);
		WRITE_SHORT(clamp(clip, -1, 32767));
		WRITE_SHORT(clamp(reserve, -1, 32767));
		WRITE_LONG(target->pev->weapons);
		WRITE_SHORT(clamp(target->m_iAccount, 0, 32767));
		WRITE_SHORT(clamp(int(target->pev->frags), -32768, 32767));
		WRITE_SHORT(clamp(target->m_iDeaths, 0, 32767));
		WRITE_BYTE(target->m_iKevlar);
		WRITE_BYTE(flags);
	MESSAGE_END();
}
