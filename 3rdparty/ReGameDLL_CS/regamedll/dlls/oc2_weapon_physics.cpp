#include "precompiled.h"
#include "oc2_weapon_physics.h"
#include <cmath>
#include <string>
#include <vector>

namespace
{
constexpr float kMaxSpeed = 420.0f;
constexpr float kMaxSpin = 480.0f;
constexpr float kSkin = 0.25f;
cvar_t physicsDebug = { "oc2_weapon_physics_debug", "0", FCVAR_SERVER, 0, nullptr };
bool registered = false;
unsigned long long nextShot = 0;
float lastFrame = -1.0f;
std::string lastMap;

struct Awake
{
	EHANDLE handle;
	float lastHit = 0, quiet = 0;
	Vector safeAngles, safeMins, safeMaxs;
	Vector clearOrigin, clearAngles, clearMins, clearMaxs;
	float lastClearCheck = 0;
	float lastResolve = -1;
	bool hasClearPose = false;
	Vector restNormal;
	bool hasRestNormal = false;
	Vector pendingSpin;
	int model = 0;
};
std::vector<Awake> awake;

bool Finite(float value) { return std::isfinite(value); }
bool Finite(const Vector &v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }
float Clamp(float value, float lo, float hi) { return value < lo ? lo : value > hi ? hi : value; }
Vector Limited(const Vector &v, float maxLength)
{
	if (!Finite(v)) return g_vecZero;
	const float n = v.Length();
	if (!Finite(n)) return g_vecZero;
	return n > maxLength ? v * (maxLength / n) : v;
}

void Init()
{
	if (!registered)
	{
		CVAR_REGISTER(&physicsDebug);
		registered = true;
	}
}

// Extra traces must leave the normal bullet trace state available to game hooks.
struct TraceState
{
	int flags;
	TraceResult trace;
	TraceState() : flags(gpGlobals->trace_flags), trace(UTIL_GetGlobalTrace()) {}
	~TraceState()
	{
		gpGlobals->trace_flags = flags;
		gpGlobals->trace_fraction = trace.flFraction;
		gpGlobals->trace_inwater = float(trace.fInWater);
		gpGlobals->trace_allsolid = float(trace.fAllSolid);
		gpGlobals->trace_startsolid = float(trace.fStartSolid);
		gpGlobals->trace_inopen = float(trace.fInOpen);
		gpGlobals->trace_endpos = trace.vecEndPos;
		gpGlobals->trace_plane_dist = trace.flPlaneDist;
		gpGlobals->trace_plane_normal = trace.vecPlaneNormal;
		gpGlobals->trace_hitgroup = trace.iHitgroup;
		gpGlobals->trace_ent = trace.pHit;
	}
};

// Trigger pickup bounds are deliberately NOT used as a target or physical body.
bool Contents(CWeaponBox *box, float &mass, int &weapon)
{
	mass = 0;
	weapon = 0;
	if (!box || FNullEnt(box->edict()) || (box->pev->flags & FL_KILLME) || box->m_bIsBomb ||
		box->pev->solid != SOLID_TRIGGER || box->pev->effects & EF_NODRAW || !box->pev->modelindex ||
		!FClassnameIs(box->pev, "weaponbox")) return false;
	for (int slot = 0; slot < MAX_ITEM_TYPES; ++slot)
	{
		int guard = 0;
		for (CBasePlayerItem *item = box->m_rgpPlayerItems[slot]; item && guard++ < 64; item = item->m_pNext)
		{
			if (item->m_iId == WEAPON_C4) return false;
			if (item->m_iId <= WEAPON_NONE) continue;
			if (!weapon) weapon = item->m_iId;
			switch (item->m_iId)
			{
			case WEAPON_KNIFE: mass += 0.8f; break;
			case WEAPON_HEGRENADE: case WEAPON_FLASHBANG: case WEAPON_SMOKEGRENADE: case WEAPON_MOLOTOV: mass += 0.7f; break;
			case WEAPON_P228: case WEAPON_USP: case WEAPON_GLOCK18: case WEAPON_FIVESEVEN: case WEAPON_TASER: mass += 1.1f; break;
			case WEAPON_DEAGLE: case WEAPON_ELITE: mass += 1.8f; break;
			case WEAPON_M249: mass += 6.0f; break;
			case WEAPON_AWP: case WEAPON_G3SG1: case WEAPON_SG550: mass += 4.8f; break;
			case WEAPON_MAC10: case WEAPON_TMP: case WEAPON_MP5N: case WEAPON_UMP45: case WEAPON_P90: mass += 2.6f; break;
			default: mass += 3.6f; break;
			}
		}
	}
	mass = Clamp(mass, 0.7f, 12.0f);
	return weapon != 0;
}

struct Body
{
	Vector mins, maxs, center;
	float radius = 0;
};

bool StudioBody(CWeaponBox *box, Body &body, float aimOffset = 0, Vector *aim = nullptr, Vector *corners = nullptr)
{
	auto *hdr = static_cast<studiohdr_t *>(GET_MODEL_PTR(box->edict()));
	if (!hdr || hdr->numbones < 1 || hdr->numbones > MAXSTUDIOBONES || hdr->numhitboxes < 1 ||
		hdr->numhitboxes > 128 || hdr->hitboxindex < int(sizeof(studiohdr_t)) ||
		hdr->hitboxindex > hdr->length - int(sizeof(mstudiobbox_t)) * hdr->numhitboxes) return false;
	auto *hitboxes = reinterpret_cast<mstudiobbox_t *>(reinterpret_cast<byte *>(hdr) + hdr->hitboxindex);
	body.mins = Vector(99999, 99999, 99999);
	body.maxs = Vector(-99999, -99999, -99999);
	for (int h = 0; h < hdr->numhitboxes; ++h)
	{
		const auto &hitbox = hitboxes[h];
		if (hitbox.bone < 0 || hitbox.bone >= hdr->numbones) return false;
		Vector origin, angles;
		// Xash's bone-position callback uses entity pitch directly, while its
		// studio hull and renderer reverse it for this game's engine features.
		const float pitch = box->pev->angles.x;
		box->pev->angles.x = -pitch;
		GET_BONE_POSITION(box->edict(), hitbox.bone, origin, angles);
		box->pev->angles.x = pitch;
		if (!Finite(origin) || !Finite(angles)) return false;
		float matrix[3][4];
		AngleMatrix(angles, matrix);
		for (int c = 0; c < 8; ++c)
		{
			Vector local((c & 1) ? hitbox.bbmax[0] : hitbox.bbmin[0],
				(c & 2) ? hitbox.bbmax[1] : hitbox.bbmin[1], (c & 4) ? hitbox.bbmax[2] : hitbox.bbmin[2]);
			Vector point;
			for (int a = 0; a < 3; ++a)
				point[a] = origin[a] - box->pev->origin[a] + local.x * matrix[a][0] + local.y * matrix[a][1] + local.z * matrix[a][2];
			if (!Finite(point) || point.Length() > 128.0f) return false;
			if (corners) corners[h * 8 + c] = box->pev->origin + point;
			for (int a = 0; a < 3; ++a)
			{
				body.mins[a] = Q_min(body.mins[a], point[a]);
				body.maxs[a] = Q_max(body.maxs[a], point[a]);
			}
			body.radius = Q_max(body.radius, point.Length());
		}
		if (aim && h == 0)
		{
			Vector local;
			int longest = 0;
			for (int a = 0; a < 3; ++a)
			{
				local[a] = (hitbox.bbmin[a] + hitbox.bbmax[a]) * 0.5f;
				if (hitbox.bbmax[a] - hitbox.bbmin[a] > hitbox.bbmax[longest] - hitbox.bbmin[longest]) longest = a;
			}
			local[longest] += Clamp(aimOffset, -0.85f, 0.85f) * (hitbox.bbmax[longest] - hitbox.bbmin[longest]) * 0.5f;
			for (int a = 0; a < 3; ++a)
				(*aim)[a] = origin[a] + local.x * matrix[a][0] + local.y * matrix[a][1] + local.z * matrix[a][2];
		}
	}
	body.center = box->pev->origin + (body.mins + body.maxs) * 0.5f;
	return body.radius > 0.1f;
}

bool Fits(CWeaponBox *box, const Vector &origin)
{
	TraceResult trace{};
	gpGlobals->trace_flags = 0;
	TRACE_MONSTER_HULL(box->edict(), origin, origin, 1, box->edict(), &trace);
	return !trace.fStartSolid && !trace.fAllSolid && trace.flFraction == 1.0f;
}

bool Size(CWeaponBox *box, const Vector &mins, const Vector &maxs)
{
	// GoldSrc BSPs only have fixed 32x32x36, 32x32x72 and 64x64x64
	// clipping hulls. Select one which encloses ALL axes; raw model dimensions
	// would select solely by X and let a sideways rifle cross a wall in Y.
	const Vector extent = maxs - mins;
	const bool wide = extent.x > 32.0f || extent.y > 32.0f;
	const float width = wide ? 64.0f : 32.0f;
	const float height = wide ? 64.0f : extent.z > 36.0f ? 72.0f : 36.0f;
	if (!Finite(mins) || !Finite(maxs) || extent.x < 0 || extent.y < 0 || extent.z < 0 ||
		extent.x > width || extent.y > width || extent.z > height) return false;
	const Vector center = (mins + maxs) * 0.5f;
	const Vector hullMins(center.x - width * 0.5f, center.y - width * 0.5f, mins.z);
	UTIL_SetSize(box->pev, hullMins, hullMins + Vector(width, width, height));
	return true;
}

// The precompiled BSP hull is not a box expansion on sloping planes.  Test
// the actual hitbox corners and all connecting chords against world brushes.
// This is a bounded geometric check, not a replacement for a mesh sweep.
bool BodyClear(CWeaponBox *box, const std::vector<Vector> &points, const Vector &offset)
{
	for (size_t h = 0; h < points.size(); h += 8)
		for (int a = 0; a < 8; ++a)
			for (int b = a; b < 8; ++b)
			{
				TraceResult trace{};
				gpGlobals->trace_flags = 0;
				UTIL_TraceLine(points[h + a] + offset, points[h + b] + offset, ignore_monsters, box->edict(), &trace);
				if (trace.fStartSolid || trace.fAllSolid || trace.flFraction < 1.0f) return false;
			}
	return true;
}

bool BodyPoints(CWeaponBox *box, Body &body, std::vector<Vector> &points)
{
	auto *hdr = static_cast<studiohdr_t *>(GET_MODEL_PTR(box->edict()));
	if (!hdr || hdr->numhitboxes < 1 || hdr->numhitboxes > 128) return false;
	points.resize(hdr->numhitboxes * 8);
	return StudioBody(box, body, 0, nullptr, points.data());
}

bool NativePath(CWeaponBox *box, const Vector &from, const Vector &to)
{
	TraceResult trace{};
	gpGlobals->trace_flags = 0;
	TRACE_MONSTER_HULL(box->edict(), from, to, 1, box->edict(), &trace);
	return !trace.fStartSolid && !trace.fAllSolid && trace.flFraction == 1.0f;
}

// For points already below a floor, trace backwards from the proposed clear
// location and only allow crossing an upward-facing support. Other point paths
// must be entirely clear, including ceilings and walls.
bool BodyPath(CWeaponBox *box, const std::vector<Vector> &points, const Vector &offset)
{
	for (const Vector &point : points)
	{
		TraceResult at{}, path{};
		gpGlobals->trace_flags = 0;
		UTIL_TraceLine(point, point, ignore_monsters, box->edict(), &at);
		const bool inside = at.fStartSolid || at.fAllSolid;
		gpGlobals->trace_flags = 0;
		UTIL_TraceLine(inside ? point + offset : point, inside ? point : point + offset,
			ignore_monsters, box->edict(), &path);
		if (path.fStartSolid || path.fAllSolid) return false;
		if (inside)
		{
			if (path.flFraction >= 1.0f || path.vecPlaneNormal.z <= 0.7f) return false;
		}
		else if (path.flFraction < 1.0f) return false;
	}
	return true;
}

void RememberClear(CWeaponBox *box, Awake &entry)
{
	Body body;
	std::vector<Vector> points;
	if (!BodyPoints(box, body, points) || !Fits(box, box->pev->origin) || !BodyClear(box, points, g_vecZero)) return;
	entry.clearOrigin = box->pev->origin;
	entry.clearAngles = box->pev->angles;
	entry.clearMins = box->pev->mins;
	entry.clearMaxs = box->pev->maxs;
	entry.hasClearPose = true;
}

// Correct a grounded pose locally while keeping the engine's support hull in
// exactly the same world position. If that cannot be done safely, recover a
// nearby validated pose only when its orientation and both hull sweeps agree.
bool ResolveGroundBody(CWeaponBox *box, Awake &entry)
{
	Body body;
	std::vector<Vector> points;
	if (!BodyPoints(box, body, points)) return false;
	if (BodyClear(box, points, g_vecZero)) return Fits(box, box->pev->origin);
	const Vector origin = box->pev->origin;
	const Vector oldMins = box->pev->mins, oldMaxs = box->pev->maxs;
	float low = 0, high = 0.25f;
	while (high <= 16.0f && !BodyClear(box, points, Vector(0, 0, high))) { low = high; high *= 2.0f; }
	if (high <= 16.0f)
	{
		for (int i = 0; i < 7; ++i)
		{
			const float mid = (low + high) * 0.5f;
			if (BodyClear(box, points, Vector(0, 0, mid))) high = mid;
			else low = mid;
		}
		high += kSkin;
		const Vector lift(0, 0, high);
		// The lowered relative hull and raised origin leave native support at
		// the same world coordinates; never shrink it around the body.
		if (body.maxs.z + kSkin <= oldMaxs.z - high && BodyClear(box, points, lift) &&
			NativePath(box, origin, origin + lift) && BodyPath(box, points, lift))
		{
			UTIL_SetSize(box->pev, oldMins - lift, oldMaxs - lift);
			if (Fits(box, origin + lift))
			{
				UTIL_SetOrigin(box->pev, origin + lift);
				return true;
			}
			UTIL_SetSize(box->pev, oldMins, oldMaxs);
		}
	}
	const Vector delta = entry.clearOrigin - origin;
	if (!entry.hasClearPose || delta.Length() > 16.0f || (entry.clearAngles - box->pev->angles).Length() > 0.01f ||
		!NativePath(box, origin, entry.clearOrigin) || !BodyPath(box, points, delta) ||
		!BodyClear(box, points, delta)) return false;
	UTIL_SetSize(box->pev, entry.clearMins, entry.clearMaxs);
	if (!NativePath(box, origin, entry.clearOrigin) || !Fits(box, entry.clearOrigin))
	{
		UTIL_SetSize(box->pev, oldMins, oldMaxs);
		return false;
	}
	UTIL_SetOrigin(box->pev, entry.clearOrigin);
	box->pev->flags &= ~FL_ONGROUND;
	box->pev->groundentity = nullptr;
	return false; // Let native physics establish support at the recovered pose.
}

float ShortAngle(float angle)
{
	angle = fmodf(angle + 180.0f, 360.0f);
	if (angle < 0) angle += 360.0f;
	return angle - 180.0f;
}

bool NeedsRotationHull(bool ground, float quiet, float sinceHit)
{
	return !ground || (quiet <= 0.18f && sinceHit <= 5.0f);
}

// Preserve yaw and align the W model's local up axis with the support normal.
// Studio uses the opposite entity pitch sign to AngleMatrix.
Vector GroundAngles(const Vector &angles, const Vector &normal)
{
	const float radians = angles.y * 0.01745329252f;
	const float x = normal.x * cosf(radians) + normal.y * sinf(radians);
	const float y = -normal.x * sinf(radians) + normal.y * cosf(radians);
	return Vector(-atan2f(x, normal.z) * 57.29577951f, angles.y,
		-asinf(Clamp(y, -1.0f, 1.0f)) * 57.29577951f);
}

float SupportProjection(const std::vector<Vector> &points, const Vector &normal)
{
	float value = 999999.0f;
	for (const Vector &p : points)
		value = Q_min(value, p.x * normal.x + p.y * normal.y + p.z * normal.z);
	return value;
}

// Return true only after taking a safe, bounded angular step. A blocked step
// permits the item to rest against the obstruction instead of forcing it.
bool GroundRelax(CWeaponBox *box, Awake &entry, float dt)
{
	Body current;
	std::vector<Vector> initial;
	if (!BodyPoints(box, current, initial) || !BodyClear(box, initial, g_vecZero)) return false;
	// Native hull bevels can report a horizontal plane on a sloping surface.
	// Find the closest actual support under the real hitbox corners instead.
	TraceResult support{};
	float nearest = 1.0f;
	for (const Vector &point : initial)
	{
		TraceResult trace{};
		gpGlobals->trace_flags = 0;
		UTIL_TraceLine(point, point - Vector(0, 0, 16), ignore_monsters, box->edict(), &trace);
		if (!trace.fStartSolid && !trace.fAllSolid && trace.flFraction < nearest &&
			Finite(trace.vecPlaneNormal) && trace.vecPlaneNormal.z > 0.7f)
		{
			support = trace;
			nearest = trace.flFraction;
		}
	}
	if (nearest >= 1.0f) return false;
	// A body can straddle a floor/slope seam. The nearest corner can switch
	// surfaces as it turns, so retain the first support for this quiet phase.
	if (!entry.hasRestNormal)
	{
		entry.restNormal = support.vecPlaneNormal;
		entry.hasRestNormal = true;
	}
	support.vecPlaneNormal = entry.restNormal;
	const Vector angles = box->pev->angles, origin = box->pev->origin;
	const Vector target = GroundAngles(angles, support.vecPlaneNormal);
	Vector delta(ShortAngle(target.x - angles.x), 0, ShortAngle(target.z - angles.z));
	const float distance = delta.Length();
	if (!Finite(distance) || distance < 0.5f) return false;
	const float step = Q_min(distance, 120.0f * Clamp(dt, 0.0f, 0.1f));
	if (step <= 0) return false;
	delta = delta * (step / distance);
	const float projection = SupportProjection(initial, support.vecPlaneNormal);
	const Vector oldMins = box->pev->mins, oldMaxs = box->pev->maxs;
	std::vector<Vector> previous = initial, points;
	Body candidate;
	box->pev->angles = angles + delta;
	if (!BodyPoints(box, candidate, points)) { box->pev->angles = angles; return false; }
	const float baseLift = (projection - SupportProjection(points, support.vecPlaneNormal)) / support.vecPlaneNormal.z;
	const Vector skin(kSkin, kSkin, kSkin);
	if (!Finite(baseLift) || fabsf(baseLift) > 8.0f ||
		!Size(box, candidate.mins - skin, candidate.maxs + skin))
	{
		box->pev->angles = angles;
		UTIL_SetSize(box->pev, oldMins, oldMaxs);
		return false;
	}
	const Vector newMins = box->pev->mins, newMaxs = box->pev->maxs;
	UTIL_SetSize(box->pev, oldMins, oldMaxs);
	// Where the fixed BSP hull changes size, its contact plane can move even
	// though the model is clear. Find the smallest local height adjustment
	// accepted by both old-hull movement and the new hull at the destination.
	auto fitsStep = [&](float extra)
	{
		const Vector lift(0, 0, baseLift + extra);
		if (!BodyClear(box, points, lift) || !NativePath(box, origin, origin + lift)) return false;
		UTIL_SetSize(box->pev, newMins, newMaxs);
		const bool fit = Fits(box, origin + lift);
		UTIL_SetSize(box->pev, oldMins, oldMaxs);
		return fit;
	};
	float extra = -Q_min(0.5f, Q_max(0.0f, nearest * 16.0f - kSkin));
	if (!fitsStep(extra))
	{
		float lo = extra, hi = Q_max(0.125f, extra + 0.125f);
		while (hi <= 4.0f && !fitsStep(hi)) { lo = hi; hi *= 2.0f; }
		if (hi > 4.0f)
		{
			box->pev->angles = angles;
			return false;
		}
		for (int i = 0; i < 7; ++i)
		{
			const float mid = (lo + hi) * 0.5f;
			if (fitsStep(mid)) hi = mid; else lo = mid;
		}
		extra = hi;
	}
	Vector lift;
	bool clear = true;
	// Intermediate chords and per-corner sweeps check the actual rotating body;
	// movement uses at most 3 degrees per substep even on a long frame.
	const int samples = Q_max(1, int(ceilf(step / 3.0f)));
	for (int s = 1; s <= samples && clear; ++s)
	{
		box->pev->angles = angles + delta * (float(s) / float(samples));
		if (!BodyPoints(box, candidate, points)) { clear = false; break; }
		const float dz = (projection - SupportProjection(points, support.vecPlaneNormal)) / support.vecPlaneNormal.z +
			extra * float(s) / float(samples);
		if (!Finite(dz) || fabsf(dz) > 8.0f) { clear = false; break; }
		lift = Vector(0, 0, dz);
		// Keep a small vertical clearance at floor seams; accepting a pose at
		// numerical contact makes adjacent plane choice and log rounding noisy.
		if (!BodyClear(box, points, lift - Vector(0, 0, 0.1f))) { clear = false; break; }
		for (size_t i = 0; i < points.size(); ++i)
		{
			points[i] = points[i] + lift;
			TraceResult path{};
			gpGlobals->trace_flags = 0;
			UTIL_TraceLine(previous[i], points[i], ignore_monsters, box->edict(), &path);
			if (path.fStartSolid || path.fAllSolid || path.flFraction < 1.0f) { clear = false; break; }
		}
		previous = points;
	}
	if (!clear)
	{
		box->pev->angles = angles;
		UTIL_SetSize(box->pev, oldMins, oldMaxs);
		return false;
	}
	UTIL_SetSize(box->pev, newMins, newMaxs);
	UTIL_SetOrigin(box->pev, origin + lift);
	entry.safeAngles = box->pev->angles;
	entry.safeMins = box->pev->mins;
	entry.safeMaxs = box->pev->maxs;
	return true;
}

// A larger precompiled hull can stop above the real support. Move only the
// model down in small verified steps while keeping that native hull in place;
// a later bullet wake rebuilds its normal enclosing moving hull.
bool GroundLower(CWeaponBox *box, Awake &entry, float dt)
{
	Body body;
	std::vector<Vector> points;
	if (!BodyPoints(box, body, points) || !BodyClear(box, points, g_vecZero)) return false;
	float nearest = 1.0f;
	for (const Vector &point : points)
	{
		TraceResult trace{};
		gpGlobals->trace_flags = 0;
		UTIL_TraceLine(point, point - Vector(0, 0, 16), ignore_monsters, box->edict(), &trace);
		if (!trace.fStartSolid && !trace.fAllSolid && trace.flFraction < nearest &&
			Finite(trace.vecPlaneNormal) && trace.vecPlaneNormal.z > 0.7f) nearest = trace.flFraction;
	}
	if (nearest >= 1.0f) return false;
	const float gap = nearest * 16.0f - 0.4f;
	if (gap <= 0.01f) return false;
	const float distance = Q_min(20.0f * Clamp(dt, 0.0f, 0.1f), gap);
	if (distance <= 0.0f) return false;
	const Vector drop(0, 0, -distance);
	const Vector origin = box->pev->origin, oldMins = box->pev->mins, oldMaxs = box->pev->maxs;
	if (!BodyClear(box, points, drop) || !BodyPath(box, points, drop)) return false;
	UTIL_SetSize(box->pev, oldMins - drop, oldMaxs - drop);
	if (!Fits(box, origin + drop))
	{
		UTIL_SetSize(box->pev, oldMins, oldMaxs);
		return false;
	}
	UTIL_SetOrigin(box->pev, origin + drop);
	entry.safeAngles = box->pev->angles;
	entry.safeMins = box->pev->mins;
	entry.safeMaxs = box->pev->maxs;
	return true;
}

Awake *Find(CWeaponBox *box)
{
	for (auto &entry : awake) if (entry.handle == box) return &entry;
	return nullptr;
}

// A newly hit stock box may have settled using its former point hull. Fit the
// real body above that surface, without crossing any wall or ceiling. Failure
// keeps the old box intact instead of forcing it through the world.
bool Wake(CWeaponBox *box, const Body &body)
{
	if (auto *entry = Find(box))
	{
		entry->lastHit = gpGlobals->time;
		entry->quiet = 0;
		entry->lastResolve = -1;
		entry->hasRestNormal = false;
		return true;
	}
	const Vector oldMins = box->pev->mins, oldMaxs = box->pev->maxs;
	const Vector padding(kSkin, kSkin, kSkin);
	if (!Size(box, body.mins - padding, body.maxs + padding)) return false;
	Vector position = box->pev->origin;
	bool fit = Fits(box, position);
	if (!fit)
	{
		const float up = Clamp(-body.mins.z + 0.5f, 0.5f, 32.0f);
		for (int n = 0; n < 8 && !fit; ++n)
		{
			const Vector candidate = box->pev->origin + Vector(0, 0, up + float(n) * 2.0f);
			TraceResult path{};
			UTIL_TraceLine(box->pev->origin, candidate, ignore_monsters, box->edict(), &path);
			if (!path.fStartSolid && !path.fAllSolid && path.flFraction == 1.0f && Fits(box, candidate))
			{
				position = candidate;
				fit = true;
			}
		}
	}
	if (!fit)
	{
		UTIL_SetSize(box->pev, oldMins, oldMaxs);
		return false;
	}
	UTIL_SetOrigin(box->pev, position);
	Awake entry;
	entry.handle = box;
	entry.lastHit = gpGlobals->time;
	entry.safeAngles = box->pev->angles;
	entry.safeMins = box->pev->mins;
	entry.safeMaxs = box->pev->maxs;
	entry.model = box->pev->modelindex;
	RememberClear(box, entry);
	awake.push_back(entry);
	box->pev->movetype = MOVETYPE_BOUNCE;
	box->pev->friction = 0.62f; // engine bounce restitution = 1 - friction
	return true;
}

	// Native TOSS/BOUNCE performs swept BSP-hull movement. Include the complete
// next angular step so rotation cannot put the visible body through a wall.
// Contact can arrest rotation; this is a bounded approximation, not VPhysics.
void RotationHull(CWeaponBox *box, Awake &entry, float dt)
{
	// A pause or unusually long engine step must not rotate farther than the
	// bounded envelope below. Linear movement still uses the native swept hull.
	if (!Finite(gpGlobals->frametime) || gpGlobals->frametime <= 0 || gpGlobals->frametime > 0.1f)
		box->pev->avelocity = g_vecZero;
	Body body;
	if (!StudioBody(box, body)) { box->pev->avelocity = g_vecZero; return; }
	const Vector originalAngles = box->pev->angles;
	Vector mins = body.mins, maxs = body.maxs;
	for (int sample = 1; sample <= 4; ++sample)
	{
		box->pev->angles = originalAngles + box->pev->avelocity * (dt * float(sample) * 0.25f);
		Body next;
		if (!StudioBody(box, next)) break;
		for (int a = 0; a < 3; ++a)
		{
			mins[a] = Q_min(mins[a], next.mins[a]);
			maxs[a] = Q_max(maxs[a], next.maxs[a]);
		}
	}
	box->pev->angles = originalAngles;
	// The second derivative bound covers the arc between samples (Euler rates
	// included), in addition to the contact skin. Never rely on sparse corners.
	const Vector omega = box->pev->avelocity;
	const float radians = (fabsf(omega.x) + fabsf(omega.y) + fabsf(omega.z)) * 0.01745329252f;
	const float gap = body.radius * radians * radians * dt * dt / 128.0f;
	const Vector padding(kSkin + gap, kSkin + gap, kSkin + gap);
	if (!Size(box, mins - padding, maxs + padding) || !Fits(box, box->pev->origin))
	{
		// Use the current pose if it fits; otherwise restore the last safe one.
		const Vector skin(kSkin, kSkin, kSkin);
		if (!Size(box, body.mins - skin, body.maxs + skin) || !Fits(box, box->pev->origin))
		{
			box->pev->angles = entry.safeAngles;
			UTIL_SetSize(box->pev, entry.safeMins, entry.safeMaxs);
		}
		box->pev->avelocity = g_vecZero;
	}
	else entry.pendingSpin = g_vecZero;
	entry.safeAngles = box->pev->angles;
	entry.safeMins = box->pev->mins;
	entry.safeMaxs = box->pev->maxs;
}

void Apply(CWeaponBox *box, const Body &body, const Vector &direction, const Vector &hit, float damage, float mass)
{
	const bool grounded = (box->pev->flags & FL_ONGROUND) != 0;
	const float kick = Clamp(sqrtf(Clamp(damage, 0.0f, 200.0f)) * 27.0f / sqrtf(mass), 0.0f, 200.0f);
	Vector impulse = direction * kick;
	// Redirect part of a floor impact upwards so a rifle can clear its body
	// support and visibly tumble instead of dragging its point origin in place.
	if (grounded && kick > 0) impulse.z = Q_max(impulse.z, Q_min(160.0f, 45.0f + kick * 0.8f));
	const Vector arm = Limited(hit - body.center, 64.0f);
	const Vector torque = CrossProduct(arm, impulse) * (42.0f / Q_max(body.radius * body.radius * 0.4f, 16.0f));
	// Map world torque to GoldSrc entity pitch/yaw/roll rates. Studio flips pitch.
	const Vector angular(-torque.y, torque.z, torque.x);
	box->pev->velocity = Limited(box->pev->velocity + impulse, kMaxSpeed);
	box->pev->avelocity = Limited(box->pev->avelocity + angular, kMaxSpin);
	box->pev->flags &= ~FL_ONGROUND;
	box->pev->groundentity = nullptr;
}
}

OC2_WeaponPhysicsBullet::OC2_WeaponPhysicsBullet() : m_id(++nextShot) {}

void OC2_WeaponPhysicsBullet::Trace(const Vector &source, const Vector &end, float damage, int bulletType, entvars_t *attacker)
{
	Init();
	if (bulletType == BULLET_NONE || !Finite(source) || !Finite(end) || !Finite(damage) || damage <= 0) return;
	const Vector delta = end - source;
	const float distance = delta.Length();
	if (!Finite(distance) || distance < 0.001f) return;
	const Vector direction = delta / distance;
	TraceState saved;
	CBaseEntity *entity = nullptr;
	while ((entity = UTIL_FindEntityByClassname(entity, "weaponbox")) != nullptr)
	{
		auto *box = static_cast<CWeaponBox *>(entity);
		float mass;
		int weapon;
		if (!Contents(box, mass, weapon)) continue;
		bool seen = false;
		for (const auto &handle : m_hit) if (handle == box) { seen = true; break; }
		if (seen) continue;
		TraceResult trace{};
		gpGlobals->trace_flags = 0;
		UTIL_TraceModel(source, end, 0, box->edict(), &trace);
		if (trace.flFraction >= 1.0f || trace.flFraction < 0 || !Finite(trace.flFraction) || !Finite(trace.vecEndPos)) continue;
		Body body;
		if (!StudioBody(box, body)) continue;
		m_hit.emplace_back(box);
		const Vector before = box->pev->velocity, spinBefore = box->pev->avelocity;
		const bool applied = Wake(box, body);
		if (applied)
		{
			Apply(box, body, direction, trace.vecEndPos, damage, mass);
			if (auto *entry = Find(box))
			{
				entry->pendingSpin = box->pev->avelocity;
				RotationHull(box, *entry, Clamp(gpGlobals->frametime, 0.001f, 0.1f));
			}
		}
		if (physicsDebug.value != 0)
		{
			const Vector impulse = box->pev->velocity - before;
			ALERT(at_console, "oc2physics hit shot=%llu t=%.6f attacker=%d box=%d weapon=%d bullet=%d applied=%d damage=%.3f mass=%.3f source=%.3f,%.3f,%.3f end=%.3f,%.3f,%.3f hit=%.3f,%.3f,%.3f impulse=%.3f,%.3f,%.3f before=%.3f,%.3f,%.3f after=%.3f,%.3f,%.3f spinbefore=%.3f,%.3f,%.3f spinafter=%.3f,%.3f,%.3f\n",
				m_id, gpGlobals->time, attacker ? ENTINDEX(ENT(attacker)) : 0, box->entindex(), weapon, bulletType, applied ? 1 : 0, damage, mass,
				source.x, source.y, source.z, end.x, end.y, end.z, trace.vecEndPos.x, trace.vecEndPos.y, trace.vecEndPos.z,
				impulse.x, impulse.y, impulse.z, before.x, before.y, before.z,
				box->pev->velocity.x, box->pev->velocity.y, box->pev->velocity.z,
				spinBefore.x, spinBefore.y, spinBefore.z, box->pev->avelocity.x, box->pev->avelocity.y, box->pev->avelocity.z);
		}
	}
}

bool OC2_WeaponPhysicsAimPoint(CWeaponBox *box, Vector &point, float offset)
{
	float mass;
	int weapon;
	Body body;
	return Finite(offset) && Contents(box, mass, weapon) && StudioBody(box, body, offset, &point);
}

bool OC2_WeaponPhysicsIsAwake(CWeaponBox *box) { return Find(box) != nullptr; }

void OC2_WeaponPhysicsFrame()
{
	Init();
	const float now = gpGlobals->time;
	if (!Finite(now) || lastMap != STRING(gpGlobals->mapname) || now < lastFrame)
	{
		awake.clear();
		lastMap = STRING(gpGlobals->mapname);
		lastFrame = now;
		return;
	}
	const float dt = lastFrame >= 0 ? Clamp(now - lastFrame, 0.0f, 0.1f) : 0;
	lastFrame = now;
	if (dt <= 0) return;
	TraceState saved;
	for (size_t i = 0; i < awake.size();)
	{
		auto &entry = awake[i];
		auto *box = entry.handle.Get<CWeaponBox>();
		float mass;
		int weapon;
		if (!Contents(box, mass, weapon) || box->pev->modelindex != entry.model)
		{
			awake.erase(awake.begin() + i);
			continue;
		}
		box->pev->velocity = Limited(box->pev->velocity, kMaxSpeed);
		box->pev->avelocity = Limited(box->pev->avelocity, kMaxSpin);
		const bool ground = (box->pev->flags & FL_ONGROUND) != 0;
		if (!ground) entry.hasRestNormal = false;
		box->pev->velocity = box->pev->velocity * expf(-(ground ? 5.0f : 0.22f) * dt);
		box->pev->avelocity = box->pev->avelocity * expf(-(ground ? 8.0f : 1.1f) * dt);
		// A floor contact can block the first angular step before the linear
		// impulse lifts the item. Retry that same finite torque briefly in air.
		if (!ground && now - entry.lastHit < 0.2f && box->pev->avelocity.Length() < 0.01f)
			box->pev->avelocity = entry.pendingSpin;
		// During the grounded rest phase GroundRelax/GroundLower maintain an
		// intentional proxy offset; rebuilding it here undoes that correction.
		if (NeedsRotationHull(ground, entry.quiet, now - entry.lastHit))
			RotationHull(box, entry, Clamp(gpGlobals->frametime, 0.001f, 0.1f));
		if (now - entry.lastClearCheck >= 0.1f)
		{
			RememberClear(box, entry);
			entry.lastClearCheck = now;
		}
		if (ground && box->pev->velocity.Length() < 22.0f) entry.quiet += dt;
		else entry.quiet = 0;
		if (ground && (entry.quiet > 0.18f || now - entry.lastHit > 5.0f))
		{
			if (entry.lastResolve >= 0 && now - entry.lastResolve < 0.2f)
			{
				++i;
				continue;
			}
			entry.lastResolve = now;
			if (!ResolveGroundBody(box, entry))
			{
				++i;
				continue;
			}
			// The existing sleep gate has already decided that linear motion is
			// quiet; do not let native angular integration fight the rest pose.
			box->pev->velocity = box->pev->avelocity = g_vecZero;
			if (weapon != WEAPON_KNIFE && weapon != WEAPON_HEGRENADE && weapon != WEAPON_FLASHBANG &&
				weapon != WEAPON_SMOKEGRENADE && weapon != WEAPON_MOLOTOV && GroundRelax(box, entry, dt))
			{
				entry.lastResolve = -1;
				++i;
				continue;
			}
			if (weapon != WEAPON_KNIFE && weapon != WEAPON_HEGRENADE && weapon != WEAPON_FLASHBANG &&
				weapon != WEAPON_SMOKEGRENADE && weapon != WEAPON_MOLOTOV && GroundLower(box, entry, dt))
			{
				entry.lastResolve = -1;
				++i;
				continue;
			}
			box->pev->velocity = box->pev->avelocity = g_vecZero;
			box->pev->movetype = MOVETYPE_TOSS;
			if (physicsDebug.value != 0) ALERT(at_console, "oc2physics sleep t=%.6f box=%d origin=%.3f,%.3f,%.3f\n",
				now, box->entindex(), box->pev->origin.x, box->pev->origin.y, box->pev->origin.z);
			awake.erase(awake.begin() + i);
			continue;
		}
		++i;
	}
}

void OC2_WeaponPhysicsDump(CWeaponBox *box)
{
	Init();
	if (!box)
	{
		ALERT(at_console, "oc2physics state t=%.6f awake=%u shots=%llu debug=%.0f\n", gpGlobals->time, unsigned(awake.size()), nextShot, physicsDebug.value);
		return;
	}
	float mass;
	int weapon;
	const bool eligible = Contents(box, mass, weapon);
	Vector aim = box->pev->origin;
	OC2_WeaponPhysicsAimPoint(box, aim);
	ALERT(at_console, "oc2physics box t=%.6f box=%d eligible=%d awake=%d mass=%.3f weapon=%d move=%d solid=%d ground=%d aim=%.3f,%.3f,%.3f velocity=%.3f,%.3f,%.3f spin=%.3f,%.3f,%.3f mins=%.3f,%.3f,%.3f maxs=%.3f,%.3f,%.3f\n",
		gpGlobals->time, box->entindex(), eligible ? 1 : 0, Find(box) ? 1 : 0, mass, weapon,
		box->pev->movetype, box->pev->solid, (box->pev->flags & FL_ONGROUND) ? 1 : 0, aim.x, aim.y, aim.z,
		box->pev->velocity.x, box->pev->velocity.y, box->pev->velocity.z, box->pev->avelocity.x, box->pev->avelocity.y, box->pev->avelocity.z,
		box->pev->mins.x, box->pev->mins.y, box->pev->mins.z, box->pev->maxs.x, box->pev->maxs.y, box->pev->maxs.z);
	if (eligible && physicsDebug.value != 0)
	{
		auto *hdr = static_cast<studiohdr_t *>(GET_MODEL_PTR(box->edict()));
		if (!hdr || hdr->numhitboxes < 1 || hdr->numhitboxes > 128) return;
		std::vector<Vector> corners(hdr->numhitboxes * 8);
		Body body;
		if (!StudioBody(box, body, 0, nullptr, corners.data())) return;
		ALERT(at_console, "oc2physics body box=%d mins=%.6f,%.6f,%.6f maxs=%.6f,%.6f,%.6f\n",
			box->entindex(), body.mins.x, body.mins.y, body.mins.z, body.maxs.x, body.maxs.y, body.maxs.z);
		for (int h = 0; h < hdr->numhitboxes; ++h)
			for (int c = 0; c < 8; ++c)
			{
				const Vector &point = corners[h * 8 + c];
				ALERT(at_console, "oc2physics corner box=%d hitbox=%d corner=%d point=%.6f,%.6f,%.6f\n",
					box->entindex(), h, c, point.x, point.y, point.z);
			}
	}
}
