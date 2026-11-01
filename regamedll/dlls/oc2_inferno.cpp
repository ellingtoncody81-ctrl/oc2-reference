#include "precompiled.h"
#include "effects.h"
#include "oc2_inferno.h"
#include "oc2_smoke.h"
#include "oc2_bot_tactics.h"
#include <cmath>

// Behavior reference: CS:GO molotov_projectile.cpp and Effects/inferno.cpp.
// Native sprite entities are the GoldSrc presentation adapter, not damage areas.
namespace
{
const int kCells = 16, kPools = 16, kProjectiles = 32, kClouds = 64;
const float kLifetime = 7.0f, kRadius = 60.0f, kSmokeLifetime = 17.3f;
const char *const kFlame = "sprites/flame1.spr";
const char *const kBurnSound = "weapons/molotov-1.wav"; // verified eight-second sample
const char *const kImpactSound = "weapons/molotov_hit1.wav";
EHANDLE s_pools[kPools], s_projectiles[kProjectiles];
struct SmokeCloud { Vector origin; float start = 0, end = 0; bool active = false, modern = false; };
SmokeCloud s_clouds[kClouds];
int s_flame = 0, s_puff = 0, s_damageEvents = 0;
float s_damageAmount = 0;

float Clamp(float value, float lo, float hi) { return value < lo ? lo : value > hi ? hi : value; }
bool Finite(const Vector &v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
		std::fabs(v.x) <= 32768 && std::fabs(v.y) <= 32768 && std::fabs(v.z) <= 32768;
}
Vector Unit(const Vector &v)
{
	const float length2 = v.LengthSquared();
	return length2 > 0.000001f && std::isfinite(length2) ? v*(1.0f/std::sqrt(length2)) : g_vecZero;
}
int FreeSlot(EHANDLE *handles, int count)
{
	for (int i=0; i<count; ++i) if (!handles[i]) return i;
	return -1;
}
void ForgetHandle(EHANDLE *handles, int count, CBaseEntity *entity)
{
	for (int i=0; i<count; ++i) if (handles[i] == entity) handles[i] = static_cast<CBaseEntity *>(nullptr);
}
template<class Wrapper, class Entity> Entity *NewEntity()
{
	edict_t *edict = CREATE_ENTITY();
	return FNullEnt(edict) ? nullptr : GetClassPtr<Wrapper>((Entity *)VARS(edict));
}
bool Water(const Vector &point)
{
	const int contents = UTIL_PointContents(point);
	return contents == CONTENTS_WATER || contents == CONTENTS_SLIME;
}
bool SurfacePoint(Vector &point)
{
	if (!Finite(point) || UTIL_PointContents(point) == CONTENTS_SOLID || UTIL_PointContents(point) == CONTENTS_SKY) return false;
	if (!Water(point)) return true;
	Vector low = point, high = point + Vector(0, 0, 30);
	if (Water(high) || UTIL_PointContents(high) == CONTENTS_SOLID) return false;
	// GoldSrc lacks a water-inclusive trace mask. Bounded contents search finds
	// the exposed shallow-water surface; deep water cannot support a flame.
	for (int i=0; i<10; ++i)
	{
		Vector middle = (low + high)*0.5f;
		if (Water(middle)) low = middle; else high = middle;
	}
	point = high + Vector(0, 0, 0.5f);
	return true;
}
bool Sky(const TraceResult &trace, const Vector &start, const Vector &end)
{
	if (!Finite(trace.vecEndPos) || !Finite(start) || !Finite(end)) return true;
	if (UTIL_PointContents(trace.vecEndPos) == CONTENTS_SKY) return true;
	const char *texture = TRACE_TEXTURE(trace.pHit ? trace.pHit : INDEXENT(0), start, end);
	return texture && !Q_strnicmp(texture, "sky", 3);
}
bool SmokeContains(const Vector &point, const Vector &origin)
{
	const Vector delta = point-origin;
	const float z = delta.z;
	if (z < -64 || z > 120) return false;
	float radius2 = 150.0f*150.0f;
	if (z > 72)
	{
		const float t = Clamp((z-72)/48.0f, 0, 1);
		radius2 += t*t*(100.0f*100.0f-radius2);
	}
	else if (z < 18)
	{
		const float t = Clamp((12-z)/76.0f, 0, 1);
		radius2 += t*t*(128.0f*128.0f-radius2);
	}
	return delta.x*delta.x + delta.y*delta.y <= radius2;
}
bool InSmoke(const Vector &point)
{
	if (OC2_SmokeInside(point)) return true;
	const float now = gpGlobals->time;
	for (SmokeCloud &cloud : s_clouds)
	{
		if (!cloud.active) continue;
		if (now < cloud.start || now > cloud.end) { cloud.active = false; continue; }
		if (!cloud.modern && SmokeContains(point, cloud.origin)) return true;
	}
	return false;
}
void Burst(const Vector &point)
{
	if (s_puff <= 0 || !Finite(point)) return;
	MESSAGE_BEGIN(MSG_PVS, SVC_TEMPENTITY, point);
		WRITE_BYTE(TE_SPRITE);
		WRITE_COORD(point.x); WRITE_COORD(point.y); WRITE_COORD(point.z);
		WRITE_SHORT(s_puff); WRITE_BYTE(5); WRITE_BYTE(180);
	MESSAGE_END();
}
void Steam(const Vector &point)
{
	if (g_sModelIndexSmoke <= 0 || !Finite(point)) return;
	MESSAGE_BEGIN(MSG_PVS,SVC_TEMPENTITY,point);
		WRITE_BYTE(TE_SMOKE);
		WRITE_COORD(point.x); WRITE_COORD(point.y); WRITE_COORD(point.z);
		WRITE_SHORT(g_sModelIndexSmoke); WRITE_BYTE(8); WRITE_BYTE(15);
	MESSAGE_END();
}
entvars_t *OwnerVars(EHANDLE &handle, int userid)
{
	CBaseEntity *owner = handle;
	if (owner && !owner->IsDormant() &&
		(!(owner->pev->flags & FL_CLIENT) || GETPLAYERUSERID(owner->edict()) == userid)) return owner->pev;
	return VARS(INDEXENT(0));
}
}

class COC2FlameSprite: public CSprite
{
public:
	int ObjectCaps() override { return FCAP_DONT_SAVE; }
	void FrameThink()
	{
		const float now = gpGlobals->time;
		if (!m_pool || !std::isfinite(now) || now < m_last || now >= m_end) { UTIL_Remove(this); return; }
		Animate(Clamp(now-m_last, 0, 0.25f)*10);
		m_last = now;
		pev->nextthink = now + 0.1f;
	}
	EHANDLE m_pool;
	float m_end = 0, m_last = 0;
};

class COC2Inferno: public CPointEntity
{
public:
	struct Cell
	{
		Vector point, normal;
		EHANDLE sprite;
		float nextSpawn = 0, spawnEnd = 0, interval = 0;
		int parent = -1, depth = 0;
		bool burning = false;
	};
	int ObjectCaps() override { return FCAP_DONT_SAVE; }
	void Spawn() override
	{
		MAKE_STRING_CLASS("oc2_inferno", pev);
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;
		pev->effects |= EF_NODRAW;
		m_count = 0; m_stopped = false;
		m_start = gpGlobals->time; m_end = m_start+kLifetime;
		m_lastThink = m_start;
		m_nextDamage = m_start+0.2f;
		SetThink(&COC2Inferno::FireThink);
		pev->nextthink = m_start+0.1f;
	}
	bool Start(entvars_t *owner, const Vector &point, const Vector &normal, const Vector &velocity, bool incendiary)
	{
		pev->iuser1 = incendiary ? 1 : 0; // Keep the thrown item's identity for later burn kills.
		m_owner = owner ? CBaseEntity::Instance(owner) : nullptr;
		m_userid = (owner && (owner->flags & FL_CLIENT)) ? GETPLAYERUSERID(ENT(owner)) : 0;
		m_origin = point;
		const Vector plane = Unit(normal);
		m_splash = velocity-plane*DotProduct(velocity, plane);
		UTIL_SetOrigin(pev, point);
		if (!AddCell(point, plane, -1, 0)) return false;
		EMIT_SOUND(edict(), CHAN_STATIC, kBurnSound, 0.75f, ATTN_NORM);
		return true;
	}
	void Stop()
	{
		if (m_stopped) return;
		m_stopped = true;
		SetThink(nullptr);
		for (int i=0; i<m_count; ++i) { m_cells[i].burning = false; m_cells[i].sprite.Remove(); }
		STOP_SOUND(edict(), CHAN_STATIC, kBurnSound);
		ForgetHandle(s_pools, kPools, this);
		UTIL_Remove(this);
	}
	void Extinguish(const Vector &smoke)
	{
		if (m_stopped) return;
		const bool all = SmokeContains(m_origin, smoke);
		for (int i=0; i<m_count; ++i)
			if (m_cells[i].burning && (all || SmokeContains(m_cells[i].point, smoke))) KillCell(i);
		if (!BurningCount()) Stop();
	}
	bool SmokeTouching(const Vector &point, CBaseEntity *ignore)
	{
		if (m_stopped || !Finite(point) || gpGlobals->time >= m_end) return false;
		for (int i=0; i<m_count; ++i)
		{
			const Cell &cell = m_cells[i];
			if (!cell.burning) continue;
			Vector nearest = cell.point;
			nearest.z = Clamp(point.z, cell.point.z, cell.point.z+40);
			if ((point-nearest).LengthSquared() > 70.0f*70.0f) continue;
			TraceResult trace;
			UTIL_TraceLine(cell.point+Vector(0,0,30), point+Vector(0,0,2), ignore_monsters, ignore->edict(), &trace);
			if (trace.flFraction == 1 && !trace.fStartSolid && !trace.fAllSolid) return true;
		}
		return false;
	}
	int BurningCount() const
	{
		int count = 0;
		for (int i=0; i<m_count; ++i) if (m_cells[i].burning) ++count;
		return count;
	}
	bool Nearest(const Vector &point, float &distance, Vector &fire) const
	{
		bool found=false;
		if(m_stopped || gpGlobals->time>=m_end)return false;
		for(int i=0;i<m_count;++i)if(m_cells[i].burning)
		{
			const float d=(point-m_cells[i].point).LengthSquared();
			if(d<distance){distance=d;fire=m_cells[i].point;found=true;}
		}
		return found;
	}
	void FireThink()
	{
		const float now = gpGlobals->time;
		if (m_stopped || !std::isfinite(now) || now < m_start || now < m_lastThink || now >= m_end) { Stop(); return; }
		m_lastThink = now;
		for (int i=0; i<m_count; ++i)
		{
			Cell &cell = m_cells[i];
			if (!cell.burning) continue;
			Vector point = cell.point;
			if (!SurfacePoint(point) || InSmoke(point)) { KillCell(i); continue; }
			if (point.z != cell.point.z)
			{
				cell.point = point;
				if (cell.sprite) UTIL_SetOrigin(cell.sprite->pev, point+Vector(0,0,30));
			}
		}
		if (!BurningCount()) { Stop(); return; }
		Spread();
		// Bounded catch-up prevents a stalled frame from creating an unbounded
		// damage loop. Normal 24/60Hz processing preserves the .2s cadence.
		for (int ticks=0; ticks<8 && m_nextDamage <= now; ++ticks)
		{
			const float ramp = Clamp((m_nextDamage-m_start)/2.0f, 0, 1);
			DealDamage(8.0f*ramp);
			m_nextDamage += 0.2f;
			if (m_stopped) return;
		}
		if (m_nextDamage <= now) m_nextDamage = now+0.2f;
		pev->nextthink = Q_min(now+0.1f, m_nextDamage);
	}
private:
	void KillCell(int index)
	{
		if (m_cells[index].burning) Steam(m_cells[index].point);
		m_cells[index].burning = false;
		m_cells[index].sprite.Remove();
	}
	bool AddCell(Vector point, Vector normal, int parent, int depth)
	{
		if (m_count >= kCells || s_flame <= 0 || !Finite(normal) || normal.LengthSquared() < 0.25f || !SurfacePoint(point) || InSmoke(point)) return false;
		for (int i=0; i<m_count; ++i)
			if (m_cells[i].burning && (point-(m_cells[i].point+Vector(0,0,40))).LengthSquared() < kRadius*kRadius) return false;
		COC2FlameSprite *sprite = NewEntity<CCSSprite, COC2FlameSprite>();
		if (!sprite) return false;
		sprite->SpriteInit(kFlame, point+Vector(0,0,30));
		MAKE_STRING_CLASS("oc2_flame", sprite->pev);
		sprite->pev->spawnflags |= SF_SPRITE_TEMPORARY;
		sprite->SetTransparency(kRenderTransAdd, 255, 255, 255, 230, kRenderFxNone);
		sprite->SetScale(0.55f);
		sprite->pev->solid = SOLID_NOT;
		sprite->pev->movetype = MOVETYPE_NONE;
		sprite->pev->frame = float((m_count*7)%17);
		sprite->m_pool = this;
		sprite->m_end = m_end;
		sprite->m_last = gpGlobals->time;
		sprite->SetThink(&COC2FlameSprite::FrameThink);
		sprite->pev->nextthink = gpGlobals->time+0.1f;
		Cell &cell = m_cells[m_count++];
		cell.point = point;
		cell.normal = Unit(normal);
		cell.sprite = sprite;
		cell.parent = parent;
		cell.depth = depth;
		cell.interval = parent < 0 ? 0.02f : Q_min(0.5f, m_cells[parent].interval*0.1f);
		cell.nextSpawn = gpGlobals->time+cell.interval;
		cell.spawnEnd = gpGlobals->time+3.0f;
		cell.burning = true;
		return true;
	}
	void Spread()
	{
		int budget = 16;
		const float now = gpGlobals->time;
		for (int i=0; i<m_count && m_count<kCells && budget>=2; ++i)
		{
			Cell &parent = m_cells[i];
			if (!parent.burning || parent.depth >= 3 || now > parent.spawnEnd || now < parent.nextSpawn) continue;
			parent.nextSpawn = now+parent.interval;
			for (int attempt=0; attempt<4 && budget>=2; ++attempt)
			{
				Vector out;
				float angle;
				if (parent.parent < 0)
				{
					angle = RANDOM_FLOAT(-3.14159265f, 3.14159265f);
					out = Vector(std::cos(angle), std::sin(angle), 0);
				}
				else
				{
					const Vector direction = Unit(parent.point-m_cells[parent.parent].point);
					angle = std::atan2(direction.y,direction.x)+RANDOM_FLOAT(-0.78539816f,0.78539816f);
					const float horizontal = std::sqrt(direction.x*direction.x+direction.y*direction.y);
					out = Vector(horizontal*std::cos(angle),horizontal*std::sin(angle),direction.z);
				}
				out = out+m_splash*(0.003f*Clamp((m_end-now)/kLifetime,0,1)*std::pow(0.2f,float(parent.depth)));
				out = out-parent.normal*DotProduct(out,parent.normal);
				Vector point = parent.point+out*RANDOM_FLOAT(50,75);
				Vector delta = point-m_origin;
				if (delta.LengthSquared() > 150.0f*150.0f) point = m_origin+Unit(delta)*150;
				if (!Finite(point)) continue;
				const Vector start = point+Vector(0,0,50), end(point.x,point.y,parent.point.z-200);
				TraceResult trace;
				UTIL_TraceLine(start,end,ignore_monsters,edict(),&trace); --budget;
				if (trace.flFraction == 1 || trace.fStartSolid || trace.fAllSolid || !Finite(trace.vecPlaneNormal) || Sky(trace,start,end)) continue;
				point = trace.vecEndPos+trace.vecPlaneNormal*0.5f;
				const Vector normal = trace.vecPlaneNormal;
				UTIL_TraceLine(parent.point+Vector(0,0,30),point+Vector(0,0,30),ignore_monsters,edict(),&trace); --budget;
				if (trace.flFraction < 1 || trace.fStartSolid || trace.fAllSolid) continue;
				if (AddCell(point,normal,i,parent.depth+1)) break;
			}
		}
	}
	bool Touching(CBaseEntity *entity) const
	{
		if (!Finite(entity->pev->absmin) || !Finite(entity->pev->absmax)) return false;
		for (int i=0; i<m_count; ++i)
		{
			if (!m_cells[i].burning) continue;
			for (int pass=0; pass<2; ++pass)
			{
				const Vector center = m_cells[i].point+Vector(0,0,pass ? 40 : 70);
				Vector nearest;
				for (int axis=0; axis<3; ++axis) nearest[axis] = Clamp(center[axis],entity->pev->absmin[axis],entity->pev->absmax[axis]);
				if ((nearest-center).LengthSquared() >= kRadius*kRadius) continue;
				TraceResult trace;
				UTIL_TraceLine(center,nearest,ignore_monsters,entity->edict(),&trace);
				if (trace.flFraction == 1 && !trace.fStartSolid && !trace.fAllSolid) return true;
			}
		}
		return false;
	}
	void DealDamage(float damage)
	{
		if (damage <= 0 || !std::isfinite(damage)) return;
		Vector mins = m_origin, maxs = m_origin;
		for (int i=0; i<m_count; ++i) if (m_cells[i].burning)
			for (int axis=0; axis<3; ++axis)
			{
				mins[axis] = Q_min(mins[axis],m_cells[i].point[axis]-(axis == 2 ? 0 : 60));
				maxs[axis] = Q_max(maxs[axis],m_cells[i].point[axis]+(axis == 2 ? 100 : 60));
			}
		CBaseEntity *entities[256];
		const int count = UTIL_EntitiesInBox(entities,256,mins,maxs,0);
		entvars_t *owner = OwnerVars(m_owner,m_userid);
		for (int i=0; i<count && !m_stopped; ++i)
		{
			CBaseEntity *victim = entities[i];
			if (!victim || victim == this || victim->pev->takedamage == DAMAGE_NO || !victim->IsAlive() || !Touching(victim)) continue;
			const float health = victim->pev->health;
			EHANDLE victimHandle(victim);
			victim->TakeDamage(pev,owner,damage,DMG_BURN);
			CBaseEntity *after = victimHandle;
			const float applied = health-(after ? after->pev->health : 0);
			// TakeDamage returns false on a killing hit, too. Count actual health
			// loss rather than treating that return value as a damage receipt.
			if (std::isfinite(applied) && applied > 0)
			{
				if (s_damageEvents < 0x7fffffff) ++s_damageEvents;
				s_damageAmount = Q_min(1.0e9f,s_damageAmount+applied);
			}
		}
	}
	Cell m_cells[kCells];
	EHANDLE m_owner;
	Vector m_origin, m_splash;
	float m_start = 0, m_end = 0, m_nextDamage = 0, m_lastThink = 0;
	int m_count = 0, m_userid = 0;
	bool m_stopped = false;
};

class COC2MolotovProjectile: public CGrenade
{
public:
	int ObjectCaps() override { return FCAP_DONT_SAVE; }
	void Spawn() override
	{
		m_finished = false;
		m_initialDirection = g_vecZero;
		m_nextSound = m_nextImpactDamage = 0;
		MAKE_STRING_CLASS("oc2_molotov_projectile",pev);
		pev->movetype = MOVETYPE_BOUNCE;
		pev->solid = SOLID_BBOX;
		pev->gravity = 0.4f;
		pev->friction = 0.55f; // GoldSrc restitution1-friction maps Source elasticity.45.
		SET_MODEL(edict(),"models/w_molotov.mdl");
		UTIL_SetSize(pev,Vector(-2,-2,-2),Vector(2,2,2));
		pev->sequence = 4;
		pev->framerate = 1;
		pev->avelocity = Vector(600,RANDOM_FLOAT(-1200,1200),0);
		m_bIsC4 = false; m_bDetonated = false; m_iBounceCount = 0;
		m_deadline = gpGlobals->time+2;
		m_dampTime = gpGlobals->time;
		m_lastThink = gpGlobals->time;
		SetTouch(&COC2MolotovProjectile::Impact);
		SetThink(&COC2MolotovProjectile::FlightThink);
		pev->nextthink = gpGlobals->time+0.05f;
	}
	void SetOwner(entvars_t *owner)
	{
		m_owner = owner ? CBaseEntity::Instance(owner) : nullptr;
		m_userid = owner && (owner->flags & FL_CLIENT) ? GETPLAYERUSERID(ENT(owner)) : 0;
		pev->owner = owner ? ENT(owner) : nullptr;
	}
	void SetVariant(bool incendiary)
	{
		pev->iuser1 = incendiary ? 1 : 0;
		SET_MODEL(edict(), incendiary ? "models/w_incendiary.mdl" : "models/w_molotov.mdl");
	}
	void RemoveProjectile()
	{
		if (m_finished) return;
		m_finished = true;
		SetTouch(nullptr); SetThink(nullptr);
		pev->velocity = g_vecZero; pev->solid = SOLID_NOT; pev->effects |= EF_NODRAW;
		ForgetHandle(s_projectiles,kProjectiles,this);
		UTIL_Remove(this);
	}
	void Impact(CBaseEntity *other)
	{
		if (m_finished || !other || other->edict() == pev->owner || other->pev->solid == SOLID_TRIGGER || other->pev->solid == SOLID_NOT) return;
		if (!std::isfinite(gpGlobals->time) || !Finite(pev->origin) || !Finite(pev->velocity)) { RemoveProjectile(); return; }
		TraceResult trace = UTIL_GetGlobalTrace();
		const Vector start = pev->origin-Unit(pev->velocity)*4, end = pev->origin+Unit(pev->velocity)*4;
		if (Sky(trace,start,end)) { RemoveProjectile(); return; }
		if (gpGlobals->time >= m_nextSound)
		{
			EMIT_SOUND(edict(),CHAN_BODY,kImpactSound,0.65f,ATTN_NORM);
			m_nextSound = gpGlobals->time+0.2f;
		}
		if (other->pev->takedamage != DAMAGE_NO && other->IsAlive() &&
			pev->velocity.LengthSquared() > 100.0f*100.0f && gpGlobals->time >= m_nextImpactDamage)
		{
			m_nextImpactDamage = gpGlobals->time+0.25f;
			if (other->IsPlayer()) static_cast<CBasePlayer *>(other)->m_LastHitGroup = HITGROUP_GENERIC;
			EHANDLE contact(other);
			other->TakeDamage(pev,OwnerVars(m_owner,m_userid),1.0f,DMG_GENERIC);
			other = contact;
			if (!other) return;
		}
		if (other->IsPlayer() || (other->pev->flags & FL_MONSTER)) return;
		if (FClassnameIs(other->pev,"func_breakable") || FClassnameIs(other->pev,"func_breakable_surf") || FClassnameIs(other->pev,"func_ladder")) return;
		if (trace.vecPlaneNormal.z >= 0.8660254f)
		{
			if (other->IsBSPModel()) Ignite(trace.vecEndPos+trace.vecPlaneNormal*0.5f,trace.vecPlaneNormal);
			else GroundDetonate();
		}
	}
	void FlightThink()
	{
		const float now = gpGlobals->time;
		if (m_finished) return;
		if (!std::isfinite(now) || now < m_lastThink || !Finite(pev->origin) || !Finite(pev->velocity) || !IsInWorld()) { RemoveProjectile(); return; }
		m_lastThink = now;
		if (!m_owner) pev->owner = nullptr;
		if (now >= m_deadline)
		{
			GroundDetonate();
			return;
		}
		if (pev->waterlevel && now >= m_dampTime) { pev->velocity = pev->velocity*0.5f; m_dampTime = now+0.2f; }
		StudioFrameAdvance();
		pev->nextthink = now+0.05f;
	}
private:
	void GroundDetonate()
	{
		TraceResult trace;
		const Vector start = pev->origin+Vector(0,0,10), end = pev->origin-Vector(0,0,128);
		UTIL_TraceLine(start,end,ignore_monsters,edict(),&trace);
		if (trace.flFraction == 1 || trace.fStartSolid || trace.fAllSolid || Sky(trace,start,end))
		{
			Burst(pev->origin);
			EMIT_SOUND(edict(),CHAN_BODY,kImpactSound,0.65f,ATTN_NORM);
			RemoveProjectile(); return;
		}
		Ignite(trace.vecEndPos+trace.vecPlaneNormal*0.5f,trace.vecPlaneNormal);
	}
	void Ignite(Vector point, const Vector &normal)
	{
		if (m_finished) return;
		bool burning = false;
		const int slot = FreeSlot(s_pools,kPools);
		if (slot >= 0 && SurfacePoint(point) && !InSmoke(point))
		{
			COC2Inferno *pool = NewEntity<CCSEntity,COC2Inferno>();
			if (pool)
			{
				pool->Spawn();
				const Vector splash = m_initialDirection*pev->velocity.Length();
				s_pools[slot] = pool;
				burning = pool->Start(OwnerVars(m_owner,m_userid),point,normal,splash,pev->iuser1 != 0);
				if (!burning) pool->Stop();
			}
		}
		if (burning) Burst(point); else Steam(point);
		RemoveProjectile();
	}
public:
	Vector m_initialDirection;
private:
	EHANDLE m_owner;
	float m_deadline = 0, m_dampTime = 0, m_nextSound = 0, m_lastThink = 0, m_nextImpactDamage = 0;
	int m_userid = 0;
	bool m_finished = false;
};

LINK_ENTITY_TO_CLASS(oc2_inferno,COC2Inferno,CCSEntity)
LINK_ENTITY_TO_CLASS(oc2_molotov_projectile,COC2MolotovProjectile,CCSGrenade)

void OC2_InfernoPrecache()
{
	PRECACHE_MODEL("models/w_molotov.mdl");
	PRECACHE_MODEL("models/w_incendiary.mdl");
	s_flame = PRECACHE_MODEL(kFlame);
	s_puff = PRECACHE_MODEL("sprites/flame_puff01.spr");
	PRECACHE_SOUND(kBurnSound);
	PRECACHE_SOUND(kImpactSound);
}

CGrenade *OC2_ThrowMolotov(entvars_t *owner,const Vector &source,const Vector &velocity,bool incendiary)
{
	const int slot = FreeSlot(s_projectiles,kProjectiles);
	if (slot < 0 || !std::isfinite(gpGlobals->time) || !Finite(source) || !Finite(velocity) || UTIL_PointContents(source) == CONTENTS_SOLID) return nullptr;
	COC2MolotovProjectile *grenade = NewEntity<CCSGrenade,COC2MolotovProjectile>();
	if (!grenade) return nullptr;
	grenade->Spawn(); grenade->SetVariant(incendiary); grenade->SetOwner(owner);
	UTIL_SetOrigin(grenade->pev,source);
	grenade->pev->velocity = velocity.LengthSquared() > 3000.0f*3000.0f ? Unit(velocity)*3000 : velocity;
	grenade->m_initialDirection = Unit(velocity);
	s_projectiles[slot] = grenade;
	return grenade;
}

void OC2_InfernoSmokeDetonate(const Vector &origin, bool modern)
{
	if (!Finite(origin) || !std::isfinite(gpGlobals->time)) return;
	int slot = 0;
	for (int i=0; i<kClouds; ++i)
	{
		if (!s_clouds[i].active || s_clouds[i].end < gpGlobals->time) { slot = i; break; }
		if (s_clouds[i].end < s_clouds[slot].end) slot = i;
	}
	s_clouds[slot].origin = origin;
	s_clouds[slot].start = gpGlobals->time;
	s_clouds[slot].end = gpGlobals->time+kSmokeLifetime;
	s_clouds[slot].active = true;
	s_clouds[slot].modern = modern;
	for (int i=0; i<kPools; ++i)
	{
		CBaseEntity *entity = s_pools[i];
		if (entity) static_cast<COC2Inferno *>(entity)->Extinguish(origin);
	}
}

bool OC2_InfernoSmokeTouch(CGrenade *smoke)
{
	if (!smoke || smoke->m_bDetonated || !std::isfinite(gpGlobals->time) || !Finite(smoke->pev->origin) || !smoke->pev->model || Q_strcmp(STRING(smoke->pev->model),"models/w_smokegrenade.mdl")) return false;
	TraceResult trace;
	UTIL_TraceLine(smoke->pev->origin,smoke->pev->origin-Vector(0,0,100),ignore_monsters,smoke->edict(),&trace);
	if (!trace.fStartSolid && trace.flFraction == 1) return false;
	const Vector point = trace.fStartSolid ? smoke->pev->origin : trace.vecEndPos;
	if (!Finite(point)) return false;
	for (int i=0; i<kPools; ++i)
	{
		CBaseEntity *entity = s_pools[i];
		if (entity && static_cast<COC2Inferno *>(entity)->SmokeTouching(point,smoke)) return true;
	}
	return false;
}

void OC2_InfernoRoundReset()
{
	OC2_SmokeReset();
	OC2_BotTacticsReset();
	for (int i=0; i<kProjectiles; ++i)
	{
		CBaseEntity *entity = s_projectiles[i];
		if (entity) static_cast<COC2MolotovProjectile *>(entity)->RemoveProjectile();
		s_projectiles[i] = static_cast<CBaseEntity *>(nullptr);
	}
	for (int i=0; i<kPools; ++i)
	{
		CBaseEntity *entity = s_pools[i];
		if (entity) static_cast<COC2Inferno *>(entity)->Stop();
		s_pools[i] = static_cast<CBaseEntity *>(nullptr);
	}
	for (SmokeCloud &cloud : s_clouds) cloud.active = false;
	s_damageEvents = 0; s_damageAmount = 0;
}

void OC2_InfernoDump()
{
	int pools = 0, cells = 0, projectiles = 0;
	for (int i=0; i<kPools; ++i)
	{
		CBaseEntity *entity = s_pools[i];
		if (entity) { ++pools; cells += static_cast<COC2Inferno *>(entity)->BurningCount(); }
	}
	for (int i=0; i<kProjectiles; ++i) if (s_projectiles[i]) ++projectiles;
	ALERT(at_console,"oc2inferno: t=%.3f projectiles=%d pools=%d flames=%d damage_events=%d health_damage=%.3f\n",gpGlobals->time,projectiles,pools,cells,s_damageEvents,s_damageAmount);
	for (int i=0; i<kPools; ++i)
	{
		CBaseEntity *entity=s_pools[i];
		if (entity) ALERT(at_console,"oc2fire: index=%d origin=%.3f/%.3f/%.3f\n",entity->entindex(),entity->pev->origin.x,entity->pev->origin.y,entity->pev->origin.z);
	}
	for (const SmokeCloud &cloud:s_clouds)
		if (cloud.active && cloud.end>gpGlobals->time)
			ALERT(at_console,"oc2smoke: origin=%.3f/%.3f/%.3f remaining=%.3f\n",cloud.origin.x,cloud.origin.y,cloud.origin.z,cloud.end-gpGlobals->time);
	CBaseEntity *entity=nullptr;
	while ((entity=UTIL_FindEntityByClassname(entity,"grenade")))
	{
		CGrenade *grenade=static_cast<CGrenade *>(entity);
		ALERT(at_console,"oc2grenade: index=%d model=%s origin=%.3f/%.3f/%.3f velocity=%.3f/%.3f/%.3f detonated=%d flags=%d\n",entity->entindex(),STRING(entity->pev->model),entity->pev->origin.x,entity->pev->origin.y,entity->pev->origin.z,entity->pev->velocity.x,entity->pev->velocity.y,entity->pev->velocity.z,int(grenade->m_bDetonated),entity->pev->flags);
	}
}

bool OC2_InfernoNearest(const Vector &point,float range,Vector *fire)
{
	if(!fire || !Finite(point) || range<=0)return false;
	float distance=range*range;bool found=false;
	for(auto &handle:s_pools)
	{
		CBaseEntity *entity=handle;
		if(entity && static_cast<COC2Inferno *>(entity)->Nearest(point,distance,*fire))found=true;
	}
	return found;
}
bool OC2_InfernoDanger(const Vector &point)
{
	Vector fire;
	return OC2_InfernoNearest(point,85,&fire)&&std::fabs(point.z-fire.z)<60;
}
