/*
*
*   This program is free software; you can redistribute it and/or modify it
*   under the terms of the GNU General Public License as published by the
*   Free Software Foundation; either version 2 of the License, or (at
*   your option) any later version.
*
*   This program is distributed in the hope that it will be useful, but
*   WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*   General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program; if not, write to the Free Software Foundation,
*   Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*   In addition, as a special exception, the author gives permission to
*   link the code of this program with the Half-Life Game Engine ("HL
*   Engine") and Modified Game Libraries ("MODs") developed by Valve,
*   L.L.C ("Valve").  You must obey the GNU General Public License in all
*   respects for all of the code used other than the HL Engine and MODs
*   from Valve.  If you modify this file, you may extend this exception
*   to your version of the file, but you are not obligated to do so.  If
*   you do not wish to do so, delete this exception statement from your
*   version.
*
*/

#include "precompiled.h"
#include "oc2_economy.h"
#include "oc2_bot_control.h"

cvar_t cv_hostage_ai_enable = { "hostage_ai_enable", "0", 0, 0.0f, nullptr };
cvar_t cv_hostage_debug = { "hostage_debug", "0", FCVAR_SERVER, 0.0f, nullptr };
cvar_t cv_hostage_stop  = { "hostage_stop", "0", FCVAR_SERVER, 0.0f, nullptr };

CHostageManager *g_pHostages = nullptr;
int g_iHostageNumber = 0;
int gmsgOC2Hostage = 0;
unsigned short g_OC2HostageEpoch = 0;

// After a hostage has been picked up, the defending team only receives its
// position while a living teammate can actually see it or its carrier.
static bool OC2_HostageSpottedByT(CHostage *hostage, CBasePlayer *carrier)
{
	CBaseEntity *target = carrier ? (CBaseEntity *)carrier : (CBaseEntity *)hostage;
	const Vector point = target->pev->origin + Vector(0, 0, 36);
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBasePlayer *observer = UTIL_PlayerByIndex(i);
		if (!observer || observer->IsDormant() || !observer->IsAlive() || observer->m_iTeam != TERRORIST) continue;
		const Vector delta = point - observer->EyePosition();
		if (delta.Length() < 1.0f) return true;
		UTIL_MakeVectors(observer->pev->v_angle);
		if (DotProduct(delta.Normalize(), gpGlobals->v_forward) < 0.5f) continue;
		TraceResult tr;
		UTIL_TraceLine(observer->EyePosition(), point, dont_ignore_monsters, observer->edict(), &tr);
		if (tr.flFraction >= 1.0f || tr.pHit == target->edict()) return true;
	}
	return false;
}

static void OC2_HostageMessage(CBasePlayer *player, int kind, CHostage *hostage, int spotted = -1)
{
	if (!player || (player->m_iTeam != CT && player->m_iTeam != TERRORIST) || !gmsgOC2Hostage) return;
	int state = hostage ? hostage->OC2_State() : 0;
	CBasePlayer *carrier = hostage ? (CBasePlayer *)(CBaseEntity *)hostage->m_hOC2Carrier : nullptr;
	int slot = carrier ? carrier->entindex() : 0;
	int uid = carrier ? GETPLAYERUSERID(carrier->edict()) : 0;
	Vector pos = hostage ? hostage->OC2_Position() : Vector(0, 0, 0);
	if (hostage && player->m_iTeam == TERRORIST)
	{
		const bool terminal = state == 3 || state == 4;
		const bool moved = carrier || hostage->m_bOC2Untied || state == 1;
		const bool visible = !terminal && (!moved || (spotted >= 0 ? spotted != 0 : OC2_HostageSpottedByT(hostage, carrier)));
		state = visible ? 0 : 6; // 6: no position, including rescued hostages
		slot = uid = 0;
		if (!visible) pos = Vector(0, 0, 0);
	}
	MESSAGE_BEGIN(MSG_ONE, gmsgOC2Hostage, nullptr, player->pev);
		WRITE_BYTE(1); WRITE_BYTE(kind); WRITE_SHORT(g_OC2HostageEpoch);
		WRITE_BYTE(hostage ? hostage->m_iHostageIndex : 0);
		WRITE_BYTE(state); WRITE_BYTE(slot); WRITE_LONG(uid);
		WRITE_COORD(pos.x); WRITE_COORD(pos.y); WRITE_COORD(pos.z);
	MESSAGE_END();
}

int CHostage::OC2_State()
{
	if (m_iOC2Terminal) return m_iOC2Terminal;
	if (m_hOC2Carrier) return 2;
	return IsFollowingSomeone() ? 1 : 0;
}

Vector CHostage::OC2_Position()
{
	CBaseEntity *carrier = m_hOC2Carrier;
	return carrier ? carrier->pev->origin : pev->origin;
}

void CHostage::OC2_SendState(CBasePlayer *player)
{
	if (m_iHostageIndex < 1 || m_iHostageIndex > 24) return;
	if (player) { OC2_HostageMessage(player, 1, this); return; }
	CBasePlayer *carrier = (CBasePlayer *)(CBaseEntity *)m_hOC2Carrier;
	const int state = OC2_State();
	const bool needsSpot = state != 3 && state != 4 && (carrier || m_bOC2Untied || state == 1);
	const int spotted = needsSpot ? int(OC2_HostageSpottedByT(this, carrier)) : 0;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CBasePlayer *recipient = UTIL_PlayerByIndex(i);
		if (recipient && !recipient->IsDormant()) OC2_HostageMessage(recipient, 1, this, spotted);
	}
}

void OC2_HostageSnapshot(CBasePlayer *player)
{
	if (!player || (player->m_iTeam != CT && player->m_iTeam != TERRORIST)) return;
	OC2_HostageMessage(player, 0, nullptr);
	CHostage *hostage = nullptr;
	while ((hostage = UTIL_FindEntityByClassname(hostage, "hostage_entity")))
		hostage->OC2_SendState(player);
	OC2_HostageMessage(player, 2, nullptr);
}

void OC2_HostageRoundStart()
{
	++g_OC2HostageEpoch;
	for (int i = 1; i <= gpGlobals->maxClients; ++i)
		OC2_HostageSnapshot(UTIL_PlayerByIndex(i));
}

CHostage *OC2_HostageCarriedBy(CBasePlayer *player)
{
	CHostage *hostage = nullptr;
	while ((hostage = UTIL_FindEntityByClassname(hostage, "hostage_entity")))
		if ((CBaseEntity *)hostage->m_hOC2Carrier == player) return hostage;
	return nullptr;
}

bool OC2_HostageMap()
{
	return !CSGameRules()->m_bMapHasBombTarget &&
		UTIL_FindEntityByClassname(nullptr, "hostage_entity") != nullptr;
}

void OC2_HostageDropPlayer(CBasePlayer *player)
{
	CHostage *grabbing = OC2_HostageGrabbingBy(player);
	if (grabbing) grabbing->OC2_CancelGrab();
	CHostage *hostage = OC2_HostageCarriedBy(player);
	if (hostage) hostage->OC2_Drop(true);
}

CHostage *OC2_HostageGrabbingBy(CBasePlayer *player)
{
	CHostage *hostage = nullptr;
	while ((hostage = UTIL_FindEntityByClassname(hostage, "hostage_entity")))
		if ((CBaseEntity *)hostage->m_hOC2Grabber == player) return hostage;
	return nullptr;
}

bool CHostage::OC2_CanGrab(CBasePlayer *player)
{
	if (!player || !player->IsAlive() || player->IsDormant() || player->m_iTeam != CT ||
		!IsAlive() || m_iOC2Terminal || m_hOC2Carrier || m_bOC2PendingPlacement ||
		OC2_HostageCarriedBy(player) || player->m_bIsDefusing ||
		!(player->pev->flags & FL_ONGROUND) || !(player->pev->button & IN_USE) ||
		(player->pev->origin - pev->origin).Length() > 96.0f) return false;
	const Vector to = pev->origin + Vector(0, 0, 36) - player->EyePosition();
	UTIL_MakeVectors(player->pev->v_angle);
	if (DotProduct(to.Normalize(), gpGlobals->v_forward) < 0.7f) return false;
	TraceResult tr;
	UTIL_TraceLine(player->EyePosition(), pev->origin + Vector(0, 0, 36), dont_ignore_monsters, player->edict(), &tr);
	return tr.flFraction >= 1.0f || tr.pHit == edict();
}

bool OC2_HostageTryUse(CBasePlayer *player)
{
	if (!player || !(player->m_afButtonPressed & IN_USE) || OC2_HostageGrabbingBy(player)) return false;
	CHostage *candidate = nullptr, *best = nullptr;
	float bestDot = -1.0f; // CanGrab already applies the inclusive 0.7 cone.
	// Keep target selection on the same 96-unit, ground and line-of-sight
	// contract as the held interaction. The old generic 64-unit use sphere
	// otherwise makes some maps fail before Hostage::Use is even reached.
	while ((candidate = UTIL_FindEntityByClassname(candidate, "hostage_entity")))
	{
		if (candidate->m_hOC2Grabber || !candidate->OC2_CanGrab(player)) continue;
		const Vector to = candidate->pev->origin + Vector(0, 0, 36) - player->EyePosition();
		const float dot = DotProduct(to.Normalize(), gpGlobals->v_forward);
		if (dot > bestDot) { best = candidate; bestDot = dot; }
	}
	if (!best) return false;
	best->OC2_StartGrab(player);
	return (CBaseEntity *)best->m_hOC2Grabber == player;
}

void CHostage::OC2_CancelGrab()
{
	CBasePlayer *player = (CBasePlayer *)(CBaseEntity *)m_hOC2Grabber;
	m_hOC2Grabber = nullptr;
	m_flOC2GrabEnd = 0;
	if (player)
	{
		// This short attack lock belongs to the grab. Clear it on cancellation
		// so a fresh E press on the next usercmd reaches PlayerUse immediately.
#ifdef CLIENT_WEAPONS
		player->m_flNextAttack = 0;
#else
		player->m_flNextAttack = gpGlobals->time;
#endif
		player->SetProgressBarTime(0);
		player->ResetMaxSpeed();
	}
}

void CHostage::OC2_StartGrab(CBasePlayer *player)
{
	if (m_hOC2Grabber || OC2_HostageGrabbingBy(player) || !OC2_CanGrab(player)) return;
	// CS:GO: cutting the restraints takes four seconds, or one with a rescue
	// kit / previously untied hostage. Use the existing authoritative kit bit.
	const int duration = player->m_bHasDefuser || m_bOC2Untied ? 1 : 4;
	m_hOC2Grabber = player;
	m_flOC2GrabEnd = gpGlobals->time + duration;
	player->SetProgressBarTime(duration);
	player->ResetMaxSpeed();
}

bool OC2_HostageUpdatePlayer(CBasePlayer *player)
{
	CHostage *hostage = OC2_HostageGrabbingBy(player);
	if (!hostage) return false;
	if (!hostage->OC2_CanGrab(player))
	{
		hostage->OC2_CancelGrab();
		return false;
	}
	if (gpGlobals->time >= hostage->m_flOC2GrabEnd)
	{
		hostage->OC2_CancelGrab();
		if (hostage->OC2_PickUp(player)) hostage->m_bOC2Untied = true;
		return false;
	}
#ifdef CLIENT_WEAPONS
	player->m_flNextAttack = 0.1f;
#else
	player->m_flNextAttack = gpGlobals->time + 0.1f;
#endif
	return true;
}

bool CHostage::OC2_PickUp(CBasePlayer *player)
{
	if (!player || player->entindex() < 1 || player->entindex() > 32 ||
		!player->IsAlive() || player->m_iTeam != CT || !IsAlive() ||
		m_hOC2Carrier || m_bOC2PendingPlacement || OC2_HostageCarriedBy(player) ||
		(player->pev->origin - pev->origin).Length() > 96.0f) return false;
	TraceResult tr;
	UTIL_TraceLine(player->EyePosition(), pev->origin + Vector(0, 0, 36), dont_ignore_monsters, player->edict(), &tr);
	if (tr.flFraction < 1.0f && tr.pHit != edict()) return false;
	m_vecOC2LastSafe = pev->origin;
	m_iOC2OriginalModel = pev->model;
	m_iOC2OriginalMoveType = pev->movetype;
	m_iOC2OriginalSolid = pev->solid;
	m_vecOC2OriginalVAngle = pev->v_angle;
	if (m_improv) m_improv->Idle();
	m_State = STAND; m_hTargetEnt = nullptr; m_hStoppedTargetEnt = nullptr;
	m_hOC2Carrier = player;
	SET_MODEL(edict(), "models/hostage/p_hostage_carry.mdl");
	pev->sequence = 0; pev->frame = 0; pev->velocity = Vector(0, 0, 0);
	pev->solid = SOLID_NOT; pev->movetype = MOVETYPE_FOLLOW; pev->aiment = player->edict();
	pev->v_angle = Vector(0, 0, 0);
	player->ResetMaxSpeed();
	GiveCTTouchBonus(player);
	player->HostageUsed();
	if (TheBots) TheBots->OnEvent(EVENT_HOSTAGE_USED, player);
	OC2_SendState();
	return true;
}

bool CHostage::OC2_FindDrop(const Vector &center, edict_t *ignore, Vector &target)
{
	// The old pickup point is only a candidate when nearby; it must pass the
	// same path, ground and full-hull tests as every other candidate.
	for (int i = 0; i < 33; ++i)
	{
		Vector candidate;
		if (i == 32)
		{
			candidate = m_vecOC2LastSafe + Vector(0, 0, 36);
			if ((candidate - center).Length() > 192.0f) continue;
		}
		else
		{
			const float angle = (i % 8) * 0.7853981634f;
			const float distance = 32.0f * (1 + i / 8);
			candidate = center + Vector(cos(angle) * distance, sin(angle) * distance, 0);
		}
		TraceResult path, floor, fit;
		UTIL_TraceHull(center, candidate, dont_ignore_monsters, human_hull, ignore, &path);
		if (path.fStartSolid || path.fAllSolid || path.flFraction < 1.0f) continue;
		UTIL_TraceHull(candidate, candidate - Vector(0, 0, 128), dont_ignore_monsters, human_hull, ignore, &floor);
		if (floor.fStartSolid || floor.fAllSolid || floor.flFraction >= 1.0f || floor.vecPlaneNormal.z < 0.7f) continue;
		// Ignore the hostage, not the player, when checking the final volume.
		const Vector hullCenter = floor.vecEndPos + Vector(0, 0, 1);
		UTIL_TraceHull(hullCenter, hullCenter, dont_ignore_monsters, human_hull, edict(), &fit);
		if (fit.fStartSolid || fit.fAllSolid) continue;
		target = hullCenter - Vector(0, 0, 36);
		return true;
	}
	return false;
}

bool CHostage::OC2_Drop(bool forced)
{
	CBasePlayer *player = (CBasePlayer *)(CBaseEntity *)m_hOC2Carrier;
	if (!player && (!forced || pev->movetype != MOVETYPE_FOLLOW)) return false;
	const Vector center = player ? player->pev->origin : pev->origin;
	Vector target;
	const bool found = OC2_FindDrop(center, player ? player->edict() : edict(), target);
	if (!found && !forced) return false;
	m_hOC2Carrier = nullptr;
	pev->aiment = nullptr;
	pev->movetype = found ? m_iOC2OriginalMoveType : MOVETYPE_NONE;
	pev->solid = found ? m_iOC2OriginalSolid : SOLID_NOT;
	pev->v_angle = m_vecOC2OriginalVAngle;
	SET_MODEL(edict(), STRING(m_iOC2OriginalModel));
	UTIL_SetSize(pev, VEC_HOSTAGE_HULL_MIN, VEC_HOSTAGE_HULL_MAX);
	UTIL_SetOrigin(pev, found ? target : center); pev->velocity = Vector(0, 0, 0);
	m_bOC2PendingPlacement = !found;
	m_flOC2NextPlacement = gpGlobals->time + 0.25f;
	SetActivity(ACT_IDLE);
	if (player) player->ResetMaxSpeed();
	OC2_SendState();
	return true;
}

void OC2_HostageDump()
{
	CBasePlayer *probePlayer = UTIL_PlayerByIndex(1);
	int total = 0, alive = 0, dead = 0, rescued = 0;
	CHostage *h = nullptr;
	while ((h = UTIL_FindEntityByClassname(h, "hostage_entity")))
	{
		++total; int state = h->OC2_State();
		if (state == 3) ++dead; else if (state == 4) ++rescued; else ++alive;
	}
	const float ratio = Q_min(Q_max(hostages_rescued_ratio.value, 0.0f), 1.0f);
	const int goal = (int)ceil((rescued + alive) * ratio);
	ALERT(at_console, "oc2hostage: time=%.3f epoch=%u total=%d alive=%d rescued=%d dead=%d round_win=%d ratio=%.3f goal=%d\n", gpGlobals->time, g_OC2HostageEpoch, total, alive, rescued, dead, CSGameRules()->m_iRoundWinStatus, ratio, goal);
	h = nullptr;
	while ((h = UTIL_FindEntityByClassname(h, "hostage_entity")))
	{
		int state = h->OC2_State();
		CBasePlayer *p = (CBasePlayer *)(CBaseEntity *)h->m_hOC2Carrier;
		Vector v = h->OC2_Position();
		ALERT(at_console, "oc2hostage_state: id=%d entity=%d state=%d carrier=%d userid=%d model=%s origin=%.1f,%.1f,%.1f speed=%.1f pending=%d solid=%d\n", h->m_iHostageIndex, h->entindex(), state, p ? p->entindex() : 0, p ? GETPLAYERUSERID(p->edict()) : 0, STRING(h->pev->model), v.x, v.y, v.z, p ? p->pev->maxspeed : 0.0f, int(h->m_bOC2PendingPlacement), int(h->pev->solid));
		CBasePlayer *grabber = (CBasePlayer *)(CBaseEntity *)h->m_hOC2Grabber;
		ALERT(at_console, "oc2hostage_detail: id=%d health=%.1f grabber=%d grab_end=%.3f untied=%d modelindex=%d effects=%d\n", h->m_iHostageIndex, h->pev->health, grabber ? grabber->entindex() : 0, h->m_flOC2GrabEnd, int(h->m_bOC2Untied), h->pev->modelindex, h->pev->effects);
		if (probePlayer)
		{
			const Vector eye = probePlayer->EyePosition(), center = h->pev->origin + Vector(0, 0, 36);
			UTIL_MakeVectors(probePlayer->pev->v_angle);
			const float dot = DotProduct((center - eye).Normalize(), gpGlobals->v_forward);
			TraceResult tr;
			UTIL_TraceLine(eye, center, dont_ignore_monsters, probePlayer->edict(), &tr);
			ALERT(at_console, "oc2hostage_probe: id=%d player=%.2f,%.2f,%.2f eye=%.2f,%.2f,%.2f ground=%d held=%d pressed=%d dist=%.3f dot=%.4f fraction=%.4f hit=%d target=%d ready=%d\n", h->m_iHostageIndex,
				probePlayer->pev->origin.x, probePlayer->pev->origin.y, probePlayer->pev->origin.z, eye.x, eye.y, eye.z,
				int((probePlayer->pev->flags & FL_ONGROUND) != 0), int((probePlayer->pev->button & IN_USE) != 0), int((probePlayer->m_afButtonPressed & IN_USE) != 0),
				(probePlayer->pev->origin - h->pev->origin).Length(), dot, tr.flFraction, tr.pHit ? ENTINDEX(tr.pHit) : 0, h->entindex(), int(h->OC2_CanGrab(probePlayer)));
		}
	}
	CBasePlayer *local = UTIL_PlayerByIndex(1);
	if (local) ALERT(at_console, "oc2hostage_player: kit_mode=%d kit=%d money=%d speed=%.1f progress_start=%.3f progress_end=%.3f\n", int(OC2_HostageMap()), int(local->m_bHasDefuser), local->m_iAccount, local->pev->maxspeed, local->m_progressStart, local->m_progressEnd);
}

struct
{
	HostageChatterType type;
	char *fileName;
} hostageSoundStruct[] = {
	{ HOSTAGE_CHATTER_START_FOLLOW, "hostage/huse/getouttahere.wav" },
	{ HOSTAGE_CHATTER_START_FOLLOW, "hostage/huse/illfollow.wav"    },
	{ HOSTAGE_CHATTER_START_FOLLOW, "hostage/huse/letsdoit.wav"     },
	{ HOSTAGE_CHATTER_START_FOLLOW, "hostage/huse/letsgo.wav"       },
	{ HOSTAGE_CHATTER_START_FOLLOW, "hostage/huse/letshurry.wav"    },
	{ HOSTAGE_CHATTER_START_FOLLOW, "hostage/huse/letsmove.wav"     },
	{ HOSTAGE_CHATTER_START_FOLLOW, "hostage/huse/okletsgo.wav"     },
	{ HOSTAGE_CHATTER_START_FOLLOW, "hostage/huse/youlead.wav"      },

	{ HOSTAGE_CHATTER_STOP_FOLLOW, "hostage/hunuse/comeback.wav"    },
	{ HOSTAGE_CHATTER_STOP_FOLLOW, "hostage/hunuse/dontleaveme.wav" },
	{ HOSTAGE_CHATTER_STOP_FOLLOW, "hostage/hunuse/illstayhere.wav" },
	{ HOSTAGE_CHATTER_STOP_FOLLOW, "hostage/hunuse/notleaveme.wav"  },
	{ HOSTAGE_CHATTER_STOP_FOLLOW, "hostage/hunuse/yeahillstay.wav" },

	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/donthurtme1.wav"     },
	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/donthurtme2.wav"     },
	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/dontkill.wav"        },
	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/endpeace.wav"        },
	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/nevernegotiate.wav"  },
	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/nottellanything.wav" },
	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/surrender.wav"       },
	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/whatdoyou.wav"       },
	{ HOSTAGE_CHATTER_INTIMIDATED, "hostage/hseenbyt/whyareyou.wav"       },

	{ HOSTAGE_CHATTER_RETREAT, "hostage/hseenbyt/donthurtme1.wav" },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hseenbyt/donthurtme2.wav" },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hseenbyt/dontkill.wav"    },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hseenbyt/dontkill.wav"    },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hretreat/illgoback1.wav"  },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hretreat/illgoback2.wav"  },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hretreat/okokgoing.wav"   },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hretreat/okokgoing2.wav"  },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hretreat/sorry1.wav"      },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hretreat/sorry2.wav"      },
	{ HOSTAGE_CHATTER_RETREAT, "hostage/hretreat/sorry3.wav"      },

	{ HOSTAGE_CHATTER_PAIN, "hostage/hpain/hpain1.wav" },
	{ HOSTAGE_CHATTER_PAIN, "hostage/hpain/hpain2.wav" },
	{ HOSTAGE_CHATTER_PAIN, "hostage/hpain/hpain3.wav" },
	{ HOSTAGE_CHATTER_PAIN, "hostage/hpain/hpain4.wav" },
	{ HOSTAGE_CHATTER_PAIN, "hostage/hpain/hpain5.wav" },
	{ HOSTAGE_CHATTER_PAIN, "hostage/hpain/hpain6.wav" },

	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/awwcrap1.wav"      },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/awwcrap2.wav"      },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/getdown1.wav"      },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/getdown2.wav"      },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/getdown3.wav"      },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/lookout1.wav"      },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/lookout2.wav"      },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/lookshooting1.wav" },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/lookshooting2.wav" },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/lookshooting3.wav" },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/ohgod1.wav"        },
	{ HOSTAGE_CHATTER_SCARED_OF_GUNFIRE, "hostage/hreactions/ohgod2.wav"        },

	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/awwcrap1.wav" },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/awwcrap2.wav" },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/deargod1.wav" },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/deargod2.wav" },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/deargod3.wav" },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/ohgod1.wav"   },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/ohgod2.wav"   },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/ohno1.wav"    },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/ohno2.wav"    },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/awww2.wav"    },
	{ HOSTAGE_CHATTER_SCARED_OF_MURDER, "hostage/hreactions/awww4.wav"    },

	{ HOSTAGE_CHATTER_LOOK_OUT, "hostage/hreactions/awwcrap1.wav" },
	{ HOSTAGE_CHATTER_LOOK_OUT, "hostage/hreactions/awwcrap2.wav" },
	{ HOSTAGE_CHATTER_LOOK_OUT, "hostage/hreactions/lookout1.wav" },
	{ HOSTAGE_CHATTER_LOOK_OUT, "hostage/hreactions/lookout2.wav" },

	{ HOSTAGE_CHATTER_PLEASE_RESCUE_ME, "hostage/hseenbyct/areyousave.wav"  },
	{ HOSTAGE_CHATTER_PLEASE_RESCUE_ME, "hostage/hseenbyct/getmeoutta1.wav" },
	{ HOSTAGE_CHATTER_PLEASE_RESCUE_ME, "hostage/hseenbyct/getmeoutta2.wav" },
	{ HOSTAGE_CHATTER_PLEASE_RESCUE_ME, "hostage/hseenbyct/imahostage.wav"  },
	{ HOSTAGE_CHATTER_PLEASE_RESCUE_ME, "hostage/hseenbyct/rescueme1.wav"   },
	{ HOSTAGE_CHATTER_PLEASE_RESCUE_ME, "hostage/hseenbyct/rescueme2.wav"   },
	{ HOSTAGE_CHATTER_PLEASE_RESCUE_ME, "hostage/hseenbyct/theyregonna.wav" },

	{ HOSTAGE_CHATTER_IMPATIENT_FOR_RESCUE, "hostage/hseenbyct/areyousave.wav"  },
	{ HOSTAGE_CHATTER_IMPATIENT_FOR_RESCUE, "hostage/hseenbyct/getmeoutta1.wav" },
	{ HOSTAGE_CHATTER_IMPATIENT_FOR_RESCUE, "hostage/hseenbyct/getmeoutta2.wav" },
	{ HOSTAGE_CHATTER_IMPATIENT_FOR_RESCUE, "hostage/hseenbyct/rescueme1.wav"   },
	{ HOSTAGE_CHATTER_IMPATIENT_FOR_RESCUE, "hostage/hseenbyct/rescueme2.wav"   },

	{ HOSTAGE_CHATTER_SEE_RESCUE_ZONE, "hostage/hseezone/almostouttahere.wav" },
	{ HOSTAGE_CHATTER_SEE_RESCUE_ZONE, "hostage/hseezone/almostthere.wav"     },
	{ HOSTAGE_CHATTER_SEE_RESCUE_ZONE, "hostage/hseezone/keepgoing.wav"       },
	{ HOSTAGE_CHATTER_SEE_RESCUE_ZONE, "hostage/hseezone/notfar.wav"          },

	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/alldead.wav"   },
	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/goodnews.wav"  },
	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/outtahere.wav" },
	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/over1.wav"     },
	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/over2.wav"     },
	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/over3.wav"     },
	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/over4.wav"     },
	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/over5.wav"     },
	{ HOSTAGE_CHATTER_CTS_WIN, "hostage/hctwin/relief.wav"    },

	{ HOSTAGE_CHATTER_TERRORISTS_WIN, "hostage/htwin/doomed.wav"  },
	{ HOSTAGE_CHATTER_TERRORISTS_WIN, "hostage/htwin/godno.wav"   },
	{ HOSTAGE_CHATTER_TERRORISTS_WIN, "hostage/htwin/nowwhat.wav" },
	{ HOSTAGE_CHATTER_TERRORISTS_WIN, "hostage/htwin/ohman.wav"   },
	{ HOSTAGE_CHATTER_TERRORISTS_WIN, "hostage/htwin/ohno.wav"    },

	{ HOSTAGE_CHATTER_RESCUED, "hostage/hrescued/finally.wav"     },
	{ HOSTAGE_CHATTER_RESCUED, "hostage/hrescued/finallysafe.wav" },
	{ HOSTAGE_CHATTER_RESCUED, "hostage/hrescued/thankyou.wav"    },
	{ HOSTAGE_CHATTER_RESCUED, "hostage/hrescued/wemadeit1.wav"   },
	{ HOSTAGE_CHATTER_RESCUED, "hostage/hrescued/wemadeit2.wav"   },
	{ HOSTAGE_CHATTER_RESCUED, "hostage/hrescued/wemadeit3.wav"   },

	{ HOSTAGE_CHATTER_WARN_NEARBY, "hostage/hwarn/becareful1.wav"   },
	{ HOSTAGE_CHATTER_WARN_NEARBY, "hostage/hwarn/becareful2.wav"   },
	{ HOSTAGE_CHATTER_WARN_NEARBY, "hostage/hwarn/stillaround1.wav" },
	{ HOSTAGE_CHATTER_WARN_NEARBY, "hostage/hwarn/stillaround2.wav" },

	{ HOSTAGE_CHATTER_WARN_SPOTTED, "hostage/hwarn/lookout1.wav"   },
	{ HOSTAGE_CHATTER_WARN_SPOTTED, "hostage/hwarn/lookout2.wav"   },
	{ HOSTAGE_CHATTER_WARN_SPOTTED, "hostage/hwarn/overthere1.wav" },
	{ HOSTAGE_CHATTER_WARN_SPOTTED, "hostage/hwarn/overthere2.wav" },

	{ HOSTAGE_CHATTER_CALL_TO_RESCUER, "hostage/hwavect/helpme1.wav"   },
	{ HOSTAGE_CHATTER_CALL_TO_RESCUER, "hostage/hwavect/helpme2.wav"   },
	{ HOSTAGE_CHATTER_CALL_TO_RESCUER, "hostage/hwavect/hey1.wav"      },
	{ HOSTAGE_CHATTER_CALL_TO_RESCUER, "hostage/hwavect/hey2.wav"      },
	{ HOSTAGE_CHATTER_CALL_TO_RESCUER, "hostage/hwavect/overhere1.wav" },

	{ HOSTAGE_CHATTER_COUGH, "hostage/hsmoke/cough1.wav" },
	{ HOSTAGE_CHATTER_COUGH, "hostage/hsmoke/cough2.wav" },
	{ HOSTAGE_CHATTER_COUGH, "hostage/hsmoke/cough3.wav" },
	{ HOSTAGE_CHATTER_COUGH, "hostage/hsmoke/cough4.wav" },
	{ HOSTAGE_CHATTER_COUGH, "hostage/hsmoke/cough5.wav" },
	{ HOSTAGE_CHATTER_COUGH, "hostage/hsmoke/cough6.wav" },

	{ HOSTAGE_CHATTER_BLINDED, "hostage/hflash/cantsee.wav" },
	{ HOSTAGE_CHATTER_BLINDED, "hostage/hflash/myeyes.wav"  },

	{ HOSTAGE_CHATTER_SAW_HE_GRENADE, "hostage/hgrenade/grenade1.wav" },
	{ HOSTAGE_CHATTER_SAW_HE_GRENADE, "hostage/hgrenade/grenade2.wav" },
	{ HOSTAGE_CHATTER_SAW_HE_GRENADE, "hostage/hgrenade/run.wav"      },
	{ HOSTAGE_CHATTER_SAW_HE_GRENADE, "hostage/hgrenade/watchout.wav" },

	{ HOSTAGE_CHATTER_DEATH_CRY, "hostage/hdie/hdeath1.wav" },
	{ HOSTAGE_CHATTER_DEATH_CRY, "hostage/hdie/hdeath2.wav" },
	{ HOSTAGE_CHATTER_DEATH_CRY, "hostage/hdie/hdeath3.wav" },
};

LINK_ENTITY_TO_CLASS(hostage_entity, CHostage, CCSHostage)
LINK_ENTITY_TO_CLASS(monster_scientist, CHostage, CCSHostage)

void CHostage::Spawn()
{
	m_hOC2Carrier = nullptr;
	m_hOC2Grabber = nullptr;
	m_flOC2GrabEnd = 0;
	m_bOC2Untied = false;
	m_iOC2Terminal = 0;
	m_bOC2PendingPlacement = false;
	m_flOC2NextPlacement = 0.0f;
	if (!g_pHostages) {
		g_pHostages = new CHostageManager;
	}

	Precache();

	MAKE_STRING_CLASS("hostage_entity", pev);

	pev->movetype = MOVETYPE_STEP;
	pev->solid = SOLID_SLIDEBOX;
	pev->takedamage = DAMAGE_YES;
	pev->flags |= FL_MONSTER;
	pev->deadflag = DEAD_NO;
	pev->max_health = 100;
	pev->health = pev->max_health;
	pev->gravity = 1;
	pev->view_ofs = VEC_HOSTAGE_VIEW;
	pev->velocity = Vector(0, 0, 0);

	if (pev->spawnflags & SF_MONSTER_HITMONSTERCLIP)
		pev->flags |= FL_MONSTERCLIP;

	if (pev->skin < 0)
		pev->skin = 0;

	SET_MODEL(edict(), STRING(pev->model));
	SetActivity(ACT_IDLE);

	m_flNextChange = 0;
	m_State = STAND;
	m_hTargetEnt = nullptr;
	m_hStoppedTargetEnt = nullptr;
	m_vPathToFollow[0] = Vector(0, 0, 0);
	m_flFlinchTime = 0;
	m_bRescueMe = FALSE;

	UTIL_SetSize(pev, VEC_HOSTAGE_HULL_MIN, VEC_HOSTAGE_HULL_MAX);
	UTIL_MakeVectors(pev->v_angle);

	SetBoneController(0, UTIL_VecToYaw(gpGlobals->v_forward));
	SetBoneController(1, 0);
	SetBoneController(2, 0);
	SetBoneController(3, 0);
	SetBoneController(4, 0);

	DROP_TO_FLOOR(edict());

	SetThink(&CHostage::IdleThink);
	pev->nextthink = gpGlobals->time + RANDOM_FLOAT(0.1, 0.2);

	m_flNextFullThink = gpGlobals->time + RANDOM_FLOAT(0.1, 0.2);
	m_vStart = pev->origin;
	m_vStartAngles = pev->angles;
	m_vOldPos = Vector(9999, 9999, 9999);
	m_iHostageIndex = ++g_iHostageNumber;

	m_nTargetNode = NODE_INVALID_EMPTY;
	m_fHasPath = FALSE;

	m_flLastPathCheck = -1;
	m_flPathAcquired = -1;
	m_flPathCheckInterval = 0.1f;
	m_flNextRadarTime = gpGlobals->time + RANDOM_FLOAT(0, 1);

	m_LocalNav = new CLocalNav(this);
	m_bStuck = FALSE;
	m_flStuckTime = 0;
	m_improv = nullptr;
}

void CHostage::Precache()
{
	PRECACHE_MODEL("models/hostage/p_hostage_carry.mdl");
	PRECACHE_MODEL("models/hostage/v_hostage_arm.mdl");
	if (cv_hostage_ai_enable.value)
	{
		string_t model = iStringNull;

		static int which = 0;
		switch (which)
		{
		default:
		case REGULAR_GUY:
			model = MAKE_STRING("models/hostageA.mdl");
			break;
		case OLD_GUY:
			model = MAKE_STRING("models/hostageB.mdl");
			break;
		case BLACK_GUY:
			model = MAKE_STRING("models/hostageC.mdl");
			break;
		case GOOFY_GUY:
			model = MAKE_STRING("models/hostageD.mdl");
			break;
		}

		m_whichModel = static_cast<ModelType>(which);

		if (++which > GOOFY_GUY)
			which = REGULAR_GUY;

		if (g_pFileSystem->FileExists(model))
		{
			pev->model = model;
		}
		else
		{
			// It seems that the model is missing, so use classic hostages
			CVAR_SET_FLOAT("hostage_ai_enable", 0);
		}
	}

	// Several shipped hostage maps request scientist.mdl, which is absent in
	// this asset set. Resolve the installed hostage before PRECACHE_MODEL;
	// preserve valid authored models and normalize old /models/... paths.
	if (!pev->model.IsNull())
	{
		const char *path = STRING(pev->model);
		while (*path == '/' || *path == '\\') ++path;
		pev->model = ALLOC_STRING(path);
	}
	if (pev->model.IsNull() || !g_pFileSystem->FileExists(pev->model))
	{
		m_whichModel = REGULAR_GUY;
		pev->model = MAKE_STRING("models/hostage.mdl");
	}

	PRECACHE_MODEL(pev->model);
	PRECACHE_SOUND("hostage/hos1.wav");
	PRECACHE_SOUND("hostage/hos2.wav");
	PRECACHE_SOUND("hostage/hos3.wav");
	PRECACHE_SOUND("hostage/hos4.wav");
	PRECACHE_SOUND("hostage/hos5.wav");
	PRECACHE_MODEL("sprites/smoke.spr");
}

void CHostage::SetActivity(Activity act)
{
	if (m_Activity != act)
	{
		int sequence = LookupActivity(act);
		if (sequence != ACT_INVALID)
		{
			if (pev->sequence != sequence)
			{
				if ((m_Activity != ACT_WALK && m_Activity != ACT_RUN) || (act != ACT_WALK && act != ACT_RUN))
				{
					pev->frame = 0;
				}

				pev->sequence = sequence;
			}

			m_Activity = act;
			ResetSequenceInfo();
		}
	}
}

void CHostage::IdleThink()
{
	float flInterval;
	const float upkeepRate = 0.03f;
	const float giveUpTime = (1 / 30.0f);
	float const updateRate = 0.1f;
	if (m_hOC2Grabber && !OC2_CanGrab((CBasePlayer *)(CBaseEntity *)m_hOC2Grabber)) OC2_CancelGrab();
	CBasePlayer *oc2Carrier = (CBasePlayer *)(CBaseEntity *)m_hOC2Carrier;
	if (!oc2Carrier && pev->movetype == MOVETYPE_FOLLOW) OC2_Drop(true);
	if (oc2Carrier && (!oc2Carrier->IsAlive() || oc2Carrier->m_iTeam != CT || oc2Carrier->IsDormant()))
	{
		OC2_Drop(true);
		oc2Carrier = nullptr;
	}
	if (m_bOC2PendingPlacement)
	{
		if (gpGlobals->time >= m_flOC2NextPlacement)
		{
			Vector target;
			m_flOC2NextPlacement = gpGlobals->time + 0.25f;
			if (OC2_FindDrop(pev->origin, edict(), target))
			{
				m_bOC2PendingPlacement = false;
				pev->movetype = m_iOC2OriginalMoveType;
				pev->solid = m_iOC2OriginalSolid;
				UTIL_SetOrigin(pev, target);
				OC2_SendState();
			}
		}
		if (m_bOC2PendingPlacement)
		{
			if (gpGlobals->time >= m_flNextRadarTime)
			{
				OC2_SendState();
				m_flNextRadarTime = gpGlobals->time + 1.0f;
			}
			pev->nextthink = gpGlobals->time + giveUpTime;
			return;
		}
	}

	if (cv_hostage_ai_enable.value && !TheNavAreaList.empty())
	{
		if (!m_improv)
		{
			m_improv = new CHostageImprov(this);
		}
	}
	else
	{
		if (m_improv)
		{
			delete m_improv;
			m_improv = nullptr;
		}
	}

	pev->nextthink = gpGlobals->time + giveUpTime;

	flInterval = StudioFrameAdvance();
	DispatchAnimEvents(flInterval);

	if (m_improv && !oc2Carrier)
	{
		m_improv->OnUpkeep(upkeepRate);
	}

	if (m_flNextFullThink > gpGlobals->time)
	{
		return;
	}

	m_flNextFullThink = gpGlobals->time + 0.1f;

	if (pev->deadflag == DEAD_DEAD)
	{
		UTIL_SetSize(pev, Vector(0, 0, 0), Vector(0, 0, 0));
		return;
	}

	if (m_hTargetEnt && ((m_bStuck && gpGlobals->time - m_flStuckTime > 5.0f) || m_hTargetEnt->pev->deadflag != DEAD_NO))
	{
		m_State = STAND;
		m_hTargetEnt = nullptr;
		m_bStuck = FALSE;
	}

	if (m_hTargetEnt || m_improv || oc2Carrier)
	{
		CBasePlayer *pPlayer = nullptr;

		if (oc2Carrier)
			pPlayer = oc2Carrier;
		else if (m_improv)
		{
			if (m_improv->IsFollowing())
				pPlayer = m_improv->GetFollowLeader();
		}
		else
		{
			pPlayer = GetClassPtr<CCSPlayer>((CBasePlayer *)m_hTargetEnt->pev);
		}

		if (!pPlayer || pPlayer->m_iTeam == CT)
		{
			if (!CSGameRules()->m_bMapHasRescueZone)
			{
				bool bResHostagePt = false;

				if (UTIL_FindEntityByClassname(nullptr, "info_hostage_rescue"))
					bResHostagePt = true;

				CBaseEntity *pSpot = nullptr;
				while ((pSpot = UTIL_FindEntityByClassname(pSpot, "info_hostage_rescue")))
				{
					if ((pSpot->pev->origin - OC2_Position()).Length() < MAX_HOSTAGES_RESCUE_RADIUS)
					{
						m_bRescueMe = TRUE;
						break;
					}
				}

				if (!bResHostagePt)
				{
					pSpot = nullptr;

					while ((pSpot = UTIL_FindEntityByClassname(pSpot, "info_player_start")))
					{
						if ((pSpot->pev->origin - OC2_Position()).Length() < MAX_HOSTAGES_RESCUE_RADIUS)
						{
							m_bRescueMe = TRUE;
							break;
						}
					}
				}
			}

			if (m_bRescueMe)
			{
				if (oc2Carrier)
				{
					Vector rescuePos = OC2_Position();
					OC2_Drop(true);
					pev->solid = SOLID_NOT;
					pev->movetype = MOVETYPE_NONE;
					UTIL_SetOrigin(pev, rescuePos);
				}
				m_bOC2PendingPlacement = false;
				m_iOC2Terminal = 4;
				OC2_SendState();
				if (TheBots)
				{
					TheBots->OnEvent(EVENT_HOSTAGE_RESCUED, pPlayer, this);
				}

				if (TheCareerTasks && CSGameRules()->IsCareer() && pPlayer && !pPlayer->IsBot())
				{
					TheCareerTasks->HandleEvent(EVENT_HOSTAGE_RESCUED, pPlayer);
				}

				pev->deadflag = DEAD_RESPAWNABLE;

				if (pPlayer)
				{
					pPlayer->AddAccount(REWARD_TAKEN_HOSTAGE, RT_HOSTAGE_RESCUED);
					UTIL_LogPrintf("\"%s<%i><%s><CT>\" triggered \"Rescued_A_Hostage\"\n", STRING(pPlayer->pev->netname),
						GETPLAYERUSERID(pPlayer->edict()), GETPLAYERAUTHID(pPlayer->edict()));
				}

#ifndef REGAMEDLL_FIXES
				SendHostageEventMsg();
#endif

				MESSAGE_BEGIN(MSG_SPEC, SVC_DIRECTOR);
					WRITE_BYTE(9);
					WRITE_BYTE(DRC_CMD_EVENT);
					WRITE_SHORT(pPlayer ? pPlayer->entindex() : 0);
					WRITE_SHORT(entindex());
					WRITE_LONG(15);
				MESSAGE_END();

				pev->effects |= EF_NODRAW;
				Remove();

#ifdef REGAMEDLL_FIXES
				SendHostageEventMsg();
#endif

				CSGameRules()->m_iHostagesRescued++;
				CSGameRules()->CheckWinConditions();

				Broadcast(pPlayer ? "rescued" : "escaped");
			}
		}
	}

	if (m_improv && !oc2Carrier)
	{
		m_improv->OnUpdate(updateRate);
	}
	else if (!oc2Carrier)
	{
		DoFollow();

		if (gpGlobals->time >= m_flFlinchTime)
		{
			if (pev->velocity.Length() > 160.0f)
			{
				SetActivity(ACT_RUN);
			}
			else if (pev->velocity.Length() > 15.0f)
			{
				SetActivity(ACT_WALK);
			}
			else
			{
				SetActivity(ACT_IDLE);
			}
		}
	}

	if (pev->deadflag != DEAD_DEAD && !(pev->effects & EF_NODRAW))
	{
		if (m_flNextRadarTime <= gpGlobals->time)
		{
			Vector vDistance = m_vOldPos - OC2_Position();

			if (vDistance.Length() > 1.0f)
			{
				m_vOldPos = OC2_Position();

				if (!CSGameRules()->m_flRestartRoundTime)
				{
					SendHostagePositionMsg();
				}
			}
			// Spotting can change even while the hostage is stationary.
			if (!CSGameRules()->m_flRestartRoundTime) OC2_SendState();

			m_flNextRadarTime = gpGlobals->time + 1.0f;
		}
	}
}

void CHostage::Remove()
{
	pev->movetype = MOVETYPE_NONE;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;

#ifdef REGAMEDLL_FIXES
	pev->deadflag = DEAD_DEAD;
#endif

	UTIL_SetSize(pev, Vector(0, 0, 0), Vector(0, 0, 0));
	pev->nextthink = -1;
	m_flNextFullThink = -1;
}

void CHostage::RePosition()
{
	OC2_CancelGrab();
	m_bOC2Untied = false;
	CBasePlayer *carrier = (CBasePlayer *)(CBaseEntity *)m_hOC2Carrier;
	if (carrier || pev->movetype == MOVETYPE_FOLLOW || m_bOC2PendingPlacement)
	{
		m_hOC2Carrier = nullptr;
		pev->aiment = nullptr;
		pev->v_angle = m_vecOC2OriginalVAngle;
		SET_MODEL(edict(), STRING(m_iOC2OriginalModel));
		if (carrier) carrier->ResetMaxSpeed();
	}
	m_bOC2PendingPlacement = false;
	m_flOC2NextPlacement = 0.0f;
	m_iOC2Terminal = 0;
	pev->health = pev->max_health;
	pev->movetype = MOVETYPE_STEP;
	pev->solid = SOLID_SLIDEBOX;
	pev->takedamage = DAMAGE_YES;
	pev->deadflag = DEAD_NO;
	pev->velocity = Vector(0, 0, 0);
	pev->angles = m_vStartAngles;
	pev->effects &= ~EF_NODRAW;

	m_hTargetEnt = nullptr;
	m_hStoppedTargetEnt = nullptr;

	m_bTouched = FALSE;
	m_bRescueMe = FALSE;

	m_flNextRadarTime = 0;
	m_vOldPos = Vector(9999, 9999, 9999);

	UTIL_SetOrigin(pev, m_vStart);
	UTIL_SetSize(pev, VEC_HOSTAGE_HULL_MIN, VEC_HOSTAGE_HULL_MAX);

	DROP_TO_FLOOR(edict());
	SetActivity(ACT_IDLE);

	SetThink(&CHostage::IdleThink);
	pev->nextthink = gpGlobals->time + RANDOM_FLOAT(0.1, 0.2);

	m_fHasPath = FALSE;
	m_nTargetNode = NODE_INVALID_EMPTY;

	m_flLastPathCheck = -1;
	m_flPathAcquired = -1;
	m_flPathCheckInterval = 0.1f;
	m_flNextFullThink = gpGlobals->time + RANDOM_FLOAT(0.1, 0.2);
}

bool CHostage::CanTakeDamage(entvars_t *pevAttacker)
{
	bool bCanTakeDmg = true; // default behaviour

#ifdef REGAMEDLL_ADD
	CBasePlayer *pAttacker = CBasePlayer::Instance(pevAttacker);
	switch ((int)hostagehurtable.value)
	{
	case 0:
		bCanTakeDmg = false;
		break;
	case 2:
		bCanTakeDmg = (pAttacker && pAttacker->IsPlayer() && pAttacker->m_iTeam == CT);
		break;
	case 3:
		bCanTakeDmg = (pAttacker && pAttacker->IsPlayer() && pAttacker->m_iTeam == TERRORIST);
		break;
	default:
		break;
	}
#endif

	return bCanTakeDmg;
}

void CHostage::TraceAttack(entvars_t *pevAttacker, float flDamage, Vector vecDir, TraceResult *ptr, int bitsDamageType)
{
	if (pev->takedamage == DAMAGE_NO || !ptr) return;
	if (CVAR_GET_FLOAT("sv_cheats") != 0.0f)
		ALERT(at_console, "oc2hostage_hit: id=%d hitgroup=%d damage=%.1f health=%.1f type=%d\n", m_iHostageIndex, ptr->iHitgroup, flDamage, pev->health, bitsDamageType);
	// Keep normal hit feedback and hitgroup handling. TakeDamage below absorbs
	// the queued damage, so the hostage remains alive and usable.
	CBaseMonster::TraceAttack(pevAttacker, flDamage, vecDir, ptr, bitsDamageType);
	TraceBleed(flDamage, vecDir, ptr, bitsDamageType);
}

BOOL CHostage::TakeDamage(entvars_t *pevInflictor, entvars_t *pevAttacker, float flDamage, int bitsDamageType)
{
	if (pev->takedamage == DAMAGE_NO || m_iOC2Terminal) return FALSE;
	CBaseEntity *attacker = CBaseEntity::Instance(pevAttacker);
	if (attacker && attacker->IsPlayer() && flDamage > 0 && std::isfinite(flDamage))
		static_cast<CBasePlayer *>(attacker)->AddAccount(-oc2_economy::HostageDamage
			* int(Q_min(flDamage, Q_max(0.0f, pev->health))), RT_HOSTAGE_DAMAGED);
	PlayPainSound();
	return TRUE;
}

float CHostage::GetModifiedDamage(float flDamage, int nHitGroup)
{
	switch (nHitGroup)
	{
	case HITGROUP_GENERIC: flDamage *= 1.75; break;
	case HITGROUP_HEAD: flDamage *= 2.5; break;
	case HITGROUP_CHEST: flDamage *= 1.5; break;
	case HITGROUP_STOMACH: flDamage *= 1.75; break;
	case HITGROUP_LEFTARM:
	case HITGROUP_RIGHTARM: flDamage *= 0.75; break;
	case HITGROUP_LEFTLEG:
	case HITGROUP_RIGHTLEG: flDamage *= 0.6; break;
	default: flDamage *= 1.5; break;
	}

	return flDamage;
}

void CHostage::PlayPainSound()
{
	if (m_LastHitGroup != HITGROUP_HEAD)
		return;

	switch (RANDOM_LONG(0, 1))
	{
	case 0: EMIT_SOUND(edict(), CHAN_VOICE, "player/headshot1.wav", VOL_NORM, ATTN_NORM); break;
	case 1: EMIT_SOUND(edict(), CHAN_VOICE, "player/headshot2.wav", VOL_NORM, ATTN_NORM); break;
	}
}

void CHostage::SetFlinchActivity()
{
	Activity activity = ACT_SMALL_FLINCH;

	if (m_improv)
	{
		m_improv->Flinch(activity);
		return;
	}

	SetActivity(activity);
}

void CHostage::SetDeathActivity()
{
	if (m_improv && m_improv->IsCrouching())
	{
		m_improv->CrouchDie();
		return;
	}

	if (cv_hostage_ai_enable.value)
	{
		switch (m_LastHitGroup)
		{
		case HITGROUP_GENERIC:
		case HITGROUP_LEFTARM:
		case HITGROUP_RIGHTARM:
		case HITGROUP_LEFTLEG:
		case HITGROUP_RIGHTLEG:
			SetActivity(ACT_DIESIMPLE);
			break;
		case HITGROUP_HEAD:
			SetActivity(ACT_DIE_HEADSHOT);
			break;
		case HITGROUP_CHEST:
			SetActivity(ACT_DIE_CHESTSHOT);
			break;
		case HITGROUP_STOMACH:
			SetActivity(ACT_DIE_GUTSHOT);
			break;
		}

		return;
	}

	// for CS 1.6
	switch (m_LastHitGroup)
	{
	case HITGROUP_GENERIC:
	case HITGROUP_HEAD:
		SetActivity(ACT_DIE_HEADSHOT);
		break;
	case HITGROUP_CHEST:
	case HITGROUP_RIGHTARM:
		SetActivity(ACT_DIESIMPLE);
		break;
	case HITGROUP_STOMACH:
	case HITGROUP_RIGHTLEG:
		SetActivity(ACT_DIEFORWARD);
		break;
	case HITGROUP_LEFTARM:
	case HITGROUP_LEFTLEG:
		SetActivity(ACT_DIEBACKWARD);
		break;
	}
}

void CHostage::AnnounceDeath(CBasePlayer *pAttacker)
{
	ClientPrint(pAttacker->pev, HUD_PRINTCENTER, "#Killed_Hostage");

	if (!(pAttacker->m_flDisplayHistory & DHF_HOSTAGE_KILLED))
	{
		pAttacker->HintMessage("#Hint_lost_money");
		pAttacker->m_flDisplayHistory |= DHF_HOSTAGE_KILLED;
	}

	if (!g_pGameRules->IsMultiplayer())
	{
		CHalfLifeTraining::HostageDied();
	}

	UTIL_LogPrintf("\"%s<%i><%s><%s>\" triggered \"Killed_A_Hostage\"\n", STRING(pAttacker->pev->netname), GETPLAYERUSERID(pAttacker->edict()),
		GETPLAYERAUTHID(pAttacker->edict()), GetTeam(pAttacker->m_iTeam));

	MESSAGE_BEGIN(MSG_SPEC, SVC_DIRECTOR);
		WRITE_BYTE(9);
		WRITE_BYTE(DRC_CMD_EVENT);
		WRITE_SHORT(ENTINDEX(pAttacker->edict()));
		WRITE_SHORT(ENTINDEX(edict()));
		WRITE_LONG(15);
	MESSAGE_END();
}

void CHostage::ApplyHostagePenalty(CBasePlayer *pAttacker)
{
	if (pAttacker->m_iTeam != TERRORIST)
		return;

	int iHostagePenalty = int(CVAR_GET_FLOAT("mp_hostagepenalty"));
	if (iHostagePenalty)
	{
		if (pAttacker->m_iHostagesKilled++ == iHostagePenalty)
		{
			pAttacker->HintMessage("#Hint_removed_for_next_hostage_killed", TRUE);
		}
		else if (pAttacker->m_iHostagesKilled >= iHostagePenalty)
		{
#ifdef REGAMEDLL_FIXES
			SERVER_COMMAND(UTIL_VarArgs("kick #%d \"For killing too many hostages\"\n", GETPLAYERUSERID(pAttacker->edict())));
#else
			CLIENT_COMMAND(pAttacker->edict(), "disconnect\n");
#endif
		}
	}
}

void CHostage::Use(CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value)
{
	if (!pActivator || !pActivator->IsPlayer() || (useType == USE_SET && value == 0.0f)) return;
	CBasePlayer *player = (CBasePlayer *)pActivator;
	// Starting requires a fresh press; holding E after a drop cannot pick it
	// straight back up. Progress is owned by this hostage, so two CTs cannot
	// untie it simultaneously.
	if ((player->m_afButtonPressed & IN_USE) && gpGlobals->time >= m_flNextChange)
		OC2_StartGrab(player);
}

void CHostage::PlayFollowRescueSound()
{
	switch (RANDOM_LONG(0, 4))
	{
	case 0: EMIT_SOUND(ENT(pev), CHAN_VOICE, "hostage/hos1.wav", VOL_NORM, ATTN_NORM); break;
	case 1: EMIT_SOUND(ENT(pev), CHAN_VOICE, "hostage/hos2.wav", VOL_NORM, ATTN_NORM); break;
	case 2: EMIT_SOUND(ENT(pev), CHAN_VOICE, "hostage/hos3.wav", VOL_NORM, ATTN_NORM); break;
	case 3: EMIT_SOUND(ENT(pev), CHAN_VOICE, "hostage/hos4.wav", VOL_NORM, ATTN_NORM); break;
	case 4: EMIT_SOUND(ENT(pev), CHAN_VOICE, "hostage/hos5.wav", VOL_NORM, ATTN_NORM); break;
	}
}

void CHostage::GiveCTTouchBonus(CBasePlayer *pPlayer)
{
	if (m_bTouched)
		return;

	m_bTouched = TRUE;
	OC2_BotControlTeamAward(CT,oc2_economy::HostageTouchTeam);

	pPlayer->AddAccount(oc2_economy::HostageTouch, RT_HOSTAGE_TOOK);
	UTIL_LogPrintf("\"%s<%i><%s><CT>\" triggered \"Touched_A_Hostage\"\n", STRING(pPlayer->pev->netname), GETPLAYERUSERID(pPlayer->edict()), GETPLAYERAUTHID(pPlayer->edict()));
}

int CHostage::ObjectCaps()
{
	return (CBaseMonster::ObjectCaps() | FCAP_MUST_SPAWN | FCAP_ONOFF_USE);
}

void CHostage::Touch(CBaseEntity *pOther)
{
	Vector2D vPush;
	const float pushForce = 50.0f;

	if (m_improv)
	{
		m_improv->OnTouch(pOther);
		return;
	}

	CBasePlayer *pPlayer = (CBasePlayer *)pOther;
	if (pPlayer->IsPlayer())
	{
		if (pPlayer->m_iTeam != CT)
			return;
	}
	else
	{
		if (!FClassnameIs(pPlayer->pev, "hostage_entity"))
			return;
	}

	vPush = (pev->origin - pOther->pev->origin).Make2D();

#ifndef PLAY_GAMEDLL
	vPush = vPush.Normalize() * pushForce;

	pev->velocity.x += vPush.x;
	pev->velocity.y += vPush.y;
#else
	// TODO: fix test demo
	pev->velocity = pev->velocity + NormalizeMulScalar<real_t, real_t, float>(vPush, pushForce);
#endif
}

void CHostage::DoFollow()
{
	CBaseEntity *pFollowing;
	Vector vecDest;
	float flRadius = 0;
	float flDistToDest;

	if (!m_hTargetEnt)
		return;

	if (cv_hostage_stop.value > 0.0f)
	{
		m_State = STAND;
		m_hTargetEnt = nullptr;
		m_hStoppedTargetEnt = nullptr;
		return;
	}

	pFollowing = GetClassPtr<CCSEntity>((CBaseEntity *)m_hTargetEnt->pev);
	m_LocalNav->SetTargetEnt(pFollowing);

	vecDest = pFollowing->pev->origin;
	vecDest.z += pFollowing->pev->mins.z;
	flDistToDest = (vecDest - pev->origin).Length();

	if (flDistToDest < 80.0f && (m_fHasPath || m_LocalNav->PathTraversable(pev->origin, vecDest, TRUE)))
		return;

	if (pev->flags & FL_ONGROUND)
	{
		if (m_flPathCheckInterval + m_flLastPathCheck < gpGlobals->time)
		{
			if (!m_fHasPath || pFollowing->pev->velocity.Length2D() > 1.0f)
			{
				m_flLastPathCheck = gpGlobals->time;
				m_LocalNav->RequestNav(this);
			}
		}
	}

	if (m_fHasPath)
	{
		m_nTargetNode = m_LocalNav->GetFurthestTraversableNode(pev->origin, vecNodes, m_nPathNodes, TRUE);

		if (!m_nTargetNode)
		{
			if ((vecNodes[m_nTargetNode] - pev->origin).Length2D() < HOSTAGE_STEPSIZE)
				m_nTargetNode = NODE_INVALID_EMPTY;
		}

		if (m_nTargetNode == NODE_INVALID_EMPTY)
		{
			m_fHasPath = FALSE;
			m_flPathCheckInterval = 0.1f;
		}
	}

	if (gpGlobals->time < m_flFlinchTime)
		return;

	if (m_nTargetNode != NODE_INVALID_EMPTY)
	{
		if (pev->flags & FL_ONGROUND)
			PointAt(vecNodes[m_nTargetNode]);

		if (pev->movetype == MOVETYPE_FLY)
			pev->v_angle.x = -60;

		MoveToward(vecNodes[m_nTargetNode]);
		m_bStuck = FALSE;
	}
	else if (IsAlive())
	{
		if (IsFollowingSomeone())
		{
			if (!m_bStuck && flDistToDest > 200.0f)
			{
				m_bStuck = TRUE;
				m_flStuckTime = gpGlobals->time;
			}
		}
	}

	if (pev->flags & FL_ONGROUND)
	{
		if (m_flPathAcquired != -1 && m_flPathAcquired + 2 > gpGlobals->time)
		{
			if (pev->velocity.Length2D() < 1 || m_nTargetNode == NODE_INVALID_EMPTY)
			{
				Wiggle();
			}
		}
	}
}

void CHostage::PointAt(const Vector &vecLoc)
{
	pev->angles.x = 0;
	pev->angles.y = UTIL_VecToAngles(vecLoc - pev->origin).y;
	pev->angles.z = 0;
}

void CHostage::MoveToward(const Vector &vecLoc)
{
	Vector vecFwd;
	Vector vecbigDest;
	Vector vecMove;
	CBaseEntity *pFollowing;
	real_t flDist;

	pFollowing = GetClassPtr<CCSEntity>((CBaseEntity *)m_hTargetEnt->pev);
	vecMove = vecLoc - pev->origin;

	Vector vecAng(0, UTIL_VecToAngles(vecMove).y, 0);
	UTIL_MakeVectorsPrivate(vecAng, vecFwd, nullptr, nullptr);

	if ((vecFwd * m_LocalNav->m_flStepSize).Length2D() <= (vecLoc - pev->origin).Length2D())
		flDist = (vecFwd * m_LocalNav->m_flStepSize).Length2D();
	else
		flDist = (vecLoc - pev->origin).Length2D();

	vecbigDest = pev->origin + (vecFwd * flDist);

	auto nFwdMove = m_LocalNav->PathTraversable(pev->origin, vecbigDest, FALSE);
	if (nFwdMove != PTRAVELS_EMPTY)
	{
		real_t flSpeed = 250;

		vecbigDest = pFollowing->pev->origin;
		vecbigDest.z += pFollowing->pev->mins.z;

		if (pev->flags & FL_ONGROUND)
		{
			flSpeed = (vecbigDest - pev->origin).Length();

			if (flSpeed >= 110)
			{
				if (flSpeed >= 250)
					flSpeed = 400;
				else
					flSpeed = 300;
			}
		}

		pev->velocity.x = vecFwd.x * flSpeed;
		pev->velocity.y = vecFwd.y * flSpeed;

		if (nFwdMove == PTRAVELS_STEPJUMPABLE)
		{
			if (pev->flags & FL_ONGROUND)
			{
				pev->velocity.z = 270;
			}
		}
	}
}

BOOL CHostage::IsOnLadder()
{
	return pev->movetype == MOVETYPE_FLY;
}

void CHostage::NavReady()
{
	CBaseEntity *pFollowing;
	Vector vecDest;
	float flRadius = 40.0f;

	if (!m_hTargetEnt)
	{
		return;
	}

	pFollowing = GetClassPtr<CCSEntity>((CBaseEntity *)m_hTargetEnt->pev);
	vecDest = pFollowing->pev->origin;

	if (!(pFollowing->pev->flags & FL_ONGROUND))
	{
		TraceResult tr;
		Vector vecDropDest = (pFollowing->pev->origin - Vector(0, 0, 300));
		UTIL_TraceHull(vecDest, vecDropDest, ignore_monsters, human_hull, pFollowing->edict(), &tr);

		if (tr.fStartSolid || tr.flFraction == 1.0f)
		{
			return;
		}

		vecDest = tr.vecEndPos;
	}

	vecDest.z += pFollowing->pev->mins.z;
	m_LocalNav->SetTargetEnt(pFollowing);

	node_index_t nindexPath = m_LocalNav->FindPath(pev->origin, vecDest, flRadius, TRUE);
	if (nindexPath == NODE_INVALID_EMPTY)
	{
		if (!m_fHasPath)
		{
			m_flPathCheckInterval += 0.1f;

			if (m_flPathCheckInterval >= 0.5f)
			{
				m_flPathCheckInterval = 0.5f;
			}
		}
	}
	else
	{
		m_fHasPath = TRUE;
		m_nTargetNode = NODE_INVALID_EMPTY;
		m_flPathAcquired = gpGlobals->time;
		m_flPathCheckInterval = 0.5f;

		m_nPathNodes = m_LocalNav->SetupPathNodes(nindexPath, vecNodes, 1);
	}
}

void CHostage::SendHostagePositionMsg()
{
	CBaseEntity *pEntity = nullptr;
	while ((pEntity = UTIL_FindEntityByClassname(pEntity, "player")))
	{
		if (FNullEnt(pEntity->edict()))
			break;

		if (!pEntity->IsPlayer())
			continue;

		if (pEntity->IsDormant())
			continue;

		CBasePlayer *pTempPlayer = GetClassPtr<CCSPlayer>((CBasePlayer *)pEntity->pev);

		if (pTempPlayer->pev->deadflag == DEAD_NO && pTempPlayer->m_iTeam == CT)
		{
			MESSAGE_BEGIN(MSG_ONE, gmsgHostagePos, nullptr, pTempPlayer->pev);
				WRITE_BYTE(0);
				WRITE_BYTE(m_iHostageIndex);
				WRITE_COORD(pev->origin.x);
				WRITE_COORD(pev->origin.y);
				WRITE_COORD(pev->origin.z);
			MESSAGE_END();
		}
	}
}

void CHostage::SendHostageEventMsg()
{
	CBaseEntity *pEntity = nullptr;
	while ((pEntity = UTIL_FindEntityByClassname(pEntity, "player")))
	{
		if (FNullEnt(pEntity->edict()))
			break;

		if (!pEntity->IsPlayer())
			continue;

		if (pEntity->IsDormant())
			continue;

		CBasePlayer *pTempPlayer = GetClassPtr<CCSPlayer>((CBasePlayer *)pEntity->pev);

		if (pTempPlayer->pev->deadflag == DEAD_NO && pTempPlayer->m_iTeam == CT)
		{
			MESSAGE_BEGIN(MSG_ONE, gmsgHostageK, nullptr, pTempPlayer->pev);
				WRITE_BYTE(m_iHostageIndex);
			MESSAGE_END();
		}

		if (pTempPlayer->pev->deadflag == DEAD_NO)
			pTempPlayer->SendHostageIcons();
	}
}

void CHostage::Wiggle()
{
	Vector vec(0, 0, 0);
	Vector wiggle_directions[] =
	{
		Vector(50, 0, 0),
		Vector(-50, 0, 0),
		Vector(0, 50, 0),
		Vector(0, -50, 0),
		Vector(50, 50, 0),
		Vector(50, -50, 0),
		Vector(-50, 50, 0),
		Vector(-50, -50, 0)
	};

	for (auto& dir : wiggle_directions) {
		Vector dest = pev->origin + dir;
		if (m_LocalNav->PathTraversable(pev->origin, dest, TRUE) == PTRAVELS_EMPTY)
			vec = vec - dir;
	}

#ifndef PLAY_GAMEDLL
	vec = vec + Vector(RANDOM_FLOAT(-3, 3), RANDOM_FLOAT(-3, 3), 0);
#else
	// TODO: fix test demo
	vec.y = vec.y + RANDOM_FLOAT(-3.0, 3.0);
	vec.x = vec.x + RANDOM_FLOAT(-3.0, 3.0);

#endif

	pev->velocity = pev->velocity + (vec.Normalize() * 100);
}

void CHostage::PreThink()
{
	Vector vecSrc;
	Vector vecDest;
	TraceResult tr;
	float flOrigDist;
	float flRaisedDist;
	float flInterval;

	if (m_improv)
	{
		return;
	}

	if (!(pev->flags & FL_ONGROUND))
	{
		return;
	}

	if (pev->velocity.Length2D() < 1)
	{
		return;
	}

	vecSrc = pev->origin;

	flInterval = m_LocalNav->m_flStepSize;
	vecDest = vecSrc + pev->velocity * gpGlobals->frametime;
	vecDest.z = vecSrc.z;

	TRACE_MONSTER_HULL(edict(), vecSrc, vecDest, dont_ignore_monsters, edict(), &tr);

	if (tr.fStartSolid || tr.flFraction == 1.0f || tr.vecPlaneNormal.z > MaxUnitZSlope)
	{
		return;
	}

	flOrigDist = (tr.vecEndPos - pev->origin).Length2D();
	vecSrc.z += flInterval;
	vecDest = vecSrc + (pev->velocity.Normalize() * 0.1f);
	vecDest.z = vecSrc.z;

	TRACE_MONSTER_HULL(edict(), vecSrc, vecDest, dont_ignore_monsters, edict(), &tr);

	if (tr.fStartSolid)
	{
		return;
	}

	vecSrc = tr.vecEndPos;
	vecDest = tr.vecEndPos;
	vecDest.z -= flInterval;

	TRACE_MONSTER_HULL(edict(), vecSrc, vecDest, dont_ignore_monsters, edict(), &tr);

	if (tr.vecPlaneNormal.z < MaxUnitZSlope)
	{
		return;
	}

	flRaisedDist = (tr.vecEndPos - pev->origin).Length2D();

	if (flRaisedDist > flOrigDist)
	{
		Vector vecNewOrigin(pev->origin);
		vecNewOrigin.z = tr.vecEndPos.z;

		UTIL_SetOrigin(pev, vecNewOrigin);
		pev->velocity.z += pev->gravity * g_psv_gravity->value * gpGlobals->frametime;
	}
}

void Hostage_RegisterCVars()
{
// These cvars are only used in czero
#ifdef REGAMEDLL_FIXES
	if (!cv_hostage_ai_enable.value)
		return;
#endif

	CVAR_REGISTER(&cv_hostage_debug);
	CVAR_REGISTER(&cv_hostage_stop);
}

void InstallHostageManager()
{
	if (g_pHostages)
	{
		delete g_pHostages;
		g_pHostages = nullptr;
	}

	g_pHostages = new CHostageManager;
}

CHostageManager::CHostageManager()
{
	Q_memset(&m_chatter, 0, sizeof(m_chatter));
	m_hostageCount = 0;
}

void CHostageManager::ServerActivate()
{
	m_hostageCount = 0;

	CHostage *pHostage = nullptr;
	while ((pHostage = UTIL_FindEntityByClassname(pHostage, "hostage_entity")))
	{
		AddHostage(pHostage);
	}

	if (cv_hostage_ai_enable.value)
	{
		for (auto& snd : hostageSoundStruct) {
			m_chatter.AddSound(snd.type, snd.fileName);
		}

#ifdef REGAMEDLL_ADD
		if (!AreRunningCZero()) {
			// Free stale navigation data of the previous map,
			// CCSBotManager::LoadNavigationMap does this only when bots are allowed
			if (!AreBotsAllowed())
				DestroyNavigationMap();

			LoadNavigationMap();
		}
#endif
	}
}

void CHostageManager::ServerDeactivate()
{
	;
}

void CHostageManager::RestartRound()
{
	for (int i = 0; i < m_hostageCount; i++)
	{
		if (m_hostage[i]->m_improv)
		{
			m_hostage[i]->m_improv->OnReset();
		}
	}
}

void CHostageManager::AddHostage(CHostage *hostage)
{
	if (m_hostageCount >= MAX_HOSTAGES)
		return;

	int i;
	for (i = 0; i < m_hostageCount; i++)
	{
		if (m_hostage[i] == hostage)
		{
			break;
		}
	}

	if (i == m_hostageCount)
	{
		m_hostage[m_hostageCount] = hostage;
		m_hostageCount++;
	}
}

bool CHostageManager::IsNearbyHostageTalking(CHostageImprov *improv)
{
	for (int i = 0; i < m_hostageCount; i++)
	{
		const float closeRange = 500.0f;
		const CHostageImprov *pHostage = m_hostage[i]->m_improv;

		if (!pHostage)
			continue;

		if (!pHostage->IsAlive() || pHostage == improv)
			continue;

		if (!(improv->GetCentroid() - pHostage->GetCentroid()).IsLengthGreaterThan(closeRange) && !pHostage->IsTalking())
		{
			return true;
		}
	}

	return false;
}

bool CHostageManager::IsNearbyHostageJumping(CHostageImprov *improv)
{
	for (int i = 0; i < m_hostageCount; i++)
	{
		const CHostageImprov *pHostage = m_hostage[i]->m_improv;

		if (!pHostage)
			continue;

		if (!pHostage->IsAlive() || pHostage == improv)
			continue;

		const float closeRange = 500.0f;
		if (!(improv->GetCentroid() - pHostage->GetCentroid()).IsLengthGreaterThan(closeRange) && pHostage->IsJumping())
		{
			return true;
		}
	}

	return false;
}

void CHostageManager::OnEvent(GameEventType event, CBaseEntity *pEntity, CBaseEntity *pOther)
{
	for (int i = 0; i < m_hostageCount; i++)
	{
		CHostageImprov *improv = m_hostage[i]->m_improv;
		if (improv)
		{
			improv->OnGameEvent(event, pEntity, pOther);
		}
	}
}

SimpleChatter::SimpleChatter()
{
	for (auto& chatter : m_chatter)
	{
		chatter.count = 0;
		chatter.index = 0;
		chatter.needsShuffle = false;
	}
}

SimpleChatter::~SimpleChatter()
{
	for (auto& chatter : m_chatter)
	{
		for (int f = 0; f < chatter.count; f++)
		{
			delete[] chatter.file[f].filename;
			chatter.file[f].filename = nullptr;
		}
	}
}

void SimpleChatter::AddSound(HostageChatterType type, char *filename)
{
	char actualFilename[128];
	auto chatter = &m_chatter[type];

	Q_snprintf(actualFilename, sizeof(actualFilename), "sound\\%s", filename);

	if (!g_pFileSystem->FileExists(actualFilename))
		return;

	chatter->file[chatter->count].filename = CloneString(filename);
	chatter->file[chatter->count].duration = (double)GET_APPROX_WAVE_PLAY_LEN(actualFilename) / 1000.0;

	chatter->needsShuffle = true;
	chatter->count++;

	PRECACHE_SOUND(filename);
}

void SimpleChatter::Shuffle(ChatterSet *chatter)
{
	if (!chatter->needsShuffle)
		return;

	for (int i = 1; i < chatter->count; i++)
	{
		for (int j = i; j < chatter->count; j++)
		{
			if (RANDOM_LONG(0, 100) < 50)
			{
				SoundFile tmp;
				tmp.filename = chatter->file[i - 1].filename;
				tmp.duration = chatter->file[i - 1].duration;

				chatter->file[i - 1] = chatter->file[j];
				chatter->file[j].filename = tmp.filename;
				chatter->file[j].duration = tmp.duration;
			}
		}
	}

	chatter->needsShuffle = false;
}

char *SimpleChatter::GetSound(HostageChatterType type, float *duration)
{
	ChatterSet *chatter = &m_chatter[type];
	if (chatter->count == 0)
		return nullptr;

	char *sound;

	Shuffle(chatter);
	sound = chatter->file[chatter->index].filename;
	*duration = chatter->file[chatter->index].duration;

	if (++chatter->index >= chatter->count)
	{
		chatter->index = 0;
	}

	return sound;
}

float SimpleChatter::PlaySound(CBaseEntity *pEntity, HostageChatterType type)
{
	float duration;
	char *pszSoundName = GetSound(type, &duration);
	CHostage *pHostage = static_cast<CHostage *>(pEntity);

	if (!pszSoundName)
	{
		return 0;
	}

	int pitch;
	int attenuation = 1;

	switch (pHostage->m_whichModel)
	{
	case CHostage::REGULAR_GUY:
		pitch = 92;
		break;
	case CHostage::BLACK_GUY:
		pitch = 86;
		break;
	case CHostage::GOOFY_GUY:
		pitch = 100;
		break;
	case CHostage::OLD_GUY:
	default:
		pitch = 88;
		break;
	}

	EMIT_SOUND_DYN(ENT(pHostage->pev), CHAN_VOICE, pszSoundName, VOL_NORM, attenuation, 0, pitch);

	if (type == HOSTAGE_CHATTER_CALL_TO_RESCUER)
	{
		if (TheBots)
		{
			TheBots->OnEvent(EVENT_HOSTAGE_CALLED_FOR_HELP, pHostage);
		}
	}

	return duration;
}
