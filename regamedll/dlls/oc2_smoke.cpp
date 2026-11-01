#include "precompiled.h"
#include "effects.h"
#include "oc2_smoke.h"
#include <cmath>

// GoldSrc adaptation of Trinity Cube / z0h1r-LK's tc_smokegrenade_ex 0.4p1.
// Original sprite and sound: https://gamebanana.com/mods/659194.
// Expanding puffs and HE dispersal are shared with bot vision. This is not a
// Source 2 voxel simulation; sprites never become bullet-blocking solids.
namespace
{
constexpr int Clouds = 16, Puffs = 8;
constexpr float Lifetime = 17.05f, FadeTime = 4.55f, PuffRadius = 82.0f;
const char *const Sprite = "sprites/trinity_cubes/sg_smoke.spr";
const char *const Sound = "trinity_cubes/sg_explode.wav";
cvar_t enabled = {"oc2_smoke", "1", FCVAR_SERVER};
struct Puff { EHANDLE sprite; Vector point; float blast = 0, clearUntil = 0; };
struct Cloud { EHANDLE grenade; Vector origin; float start = 0; Puff puffs[Puffs]; };
Cloud clouds[Clouds];
int spriteIndex = 0, blasts = 0;
float Clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
bool Finite(const Vector &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float Growth(const Cloud &c) { return Clamp((gpGlobals->time-c.start)/0.65f, 0, 1); }
float Density(const Cloud &c, const Puff &p)
{
    const float age = gpGlobals->time-c.start;
    if (!c.grenade || !p.sprite || age < 0 || age >= Lifetime) return 0;
    const float recover = Clamp((gpGlobals->time-p.clearUntil)/1.0f, 0, 1);
    return Growth(c)*Clamp((Lifetime-age)/FadeTime, 0, 1)*(1-p.blast*(1-recover));
}
Vector Position(const Cloud &c, const Puff &p)
{
    return c.origin+(p.point-c.origin)*Growth(c);
}
void Remove(Cloud &c, bool grenadeToo)
{
    for (Puff &p : c.puffs)
    {
        CBaseEntity *sprite = p.sprite;
        if (sprite) { sprite->pev->effects |= EF_NODRAW; UTIL_Remove(sprite); }
    }
    CBaseEntity *grenade = c.grenade;
    if (grenadeToo && grenade)
    {
        if (TheBots) TheBots->RemoveGrenade(static_cast<CGrenade *>(grenade));
        UTIL_Remove(grenade);
    }
    c = Cloud{};
}
}

void OC2_SmokeRegister()
{
    CVAR_REGISTER(&enabled);
    ADD_SERVER_COMMAND("oc2_smoke_dump", OC2_SmokeDump);
}
bool OC2_SmokeEnabled() { return enabled.value != 0; }
void OC2_SmokePrecache()
{
    OC2_SmokeReset();
    spriteIndex = PRECACHE_MODEL(Sprite);
    PRECACHE_SOUND(Sound);
}
void OC2_SmokeReset()
{
    for (Cloud &c : clouds) Remove(c, true);
    blasts = 0;
}
bool OC2_SmokeCreate(CGrenade *grenade)
{
    if (!OC2_SmokeEnabled() || !spriteIndex || !grenade || !Finite(grenade->pev->origin)) return false;
    Cloud *cloud = nullptr;
    for (Cloud &c : clouds) if (!c.grenade) { cloud = &c; break; }
    if (!cloud) return false; // retain the original fallback when entity capacity is exhausted
    Cloud &c = *cloud;
    Remove(c, false);
    c.grenade = grenade;
    c.start = gpGlobals->time;
    c.origin = grenade->pev->origin+Vector(0, 0, 24);
    const Vector offsets[Puffs] = {Vector(55,0,0), Vector(-55,0,0), Vector(0,55,0), Vector(0,-55,0),
        Vector(40,40,54), Vector(-40,40,54), Vector(40,-40,54), Vector(-40,-40,54)};
    int created = 0;
    for (int i=0; i<Puffs; ++i)
    {
        TraceResult tr;
        UTIL_TraceLine(c.origin, c.origin+offsets[i], ignore_monsters, grenade->edict(), &tr);
        if (tr.fAllSolid || tr.fStartSolid) continue;
        Puff &p = c.puffs[i];
        p.point = tr.vecEndPos-(tr.flFraction < 1 ? offsets[i].Normalize()*6 : g_vecZero);
        CSprite *sprite = CSprite::SpriteCreate(Sprite, c.origin, FALSE);
        if (!sprite) continue;
        MAKE_STRING_CLASS("oc2_smoke_puff", sprite->pev);
        sprite->pev->effects &= ~EF_NODRAW;
        sprite->pev->scale = 0.01f;
        sprite->pev->frame = 0;
        sprite->pev->angles.z = RANDOM_FLOAT(0, 360);
        sprite->SetTransparency(kRenderTransAlpha, 255, 255, 255, 0, kRenderFxNone);
        p.sprite = sprite;
        ++created;
    }
    if (!created) { Remove(c, false); return false; }
    grenade->pev->effects |= EF_NODRAW;
    grenade->pev->velocity = g_vecZero;
    grenade->pev->avelocity = g_vecZero;
    grenade->pev->solid = SOLID_NOT;
    if(TheBots)TheBots->MarkOC2Smoke(grenade);
    EMIT_SOUND(grenade->edict(), CHAN_WEAPON, Sound, VOL_NORM, ATTN_NORM);
    return true;
}
bool OC2_SmokeThink(CGrenade *grenade)
{
    for (Cloud &c : clouds)
    {
        if (c.grenade != grenade) continue;
        if (gpGlobals->time < c.start || gpGlobals->time-c.start >= Lifetime)
        { Remove(c, true); return true; }
        for (Puff &p : c.puffs)
        {
            CBaseEntity *sprite = p.sprite;
            if (!sprite) continue;
            UTIL_SetOrigin(sprite->pev, Position(c, p));
            sprite->pev->scale = 2.0f*Growth(c);
            sprite->pev->renderamt = 255*Density(c, p);
            sprite->pev->angles.z += 0.8f;
        }
        grenade->pev->nextthink = gpGlobals->time+0.1f;
        return true;
    }
    return false;
}
void OC2_SmokeBlast(const Vector &origin, float damage)
{
    if (!Finite(origin) || damage <= 0) return;
    bool affected = false;
    for (Cloud &c : clouds) if (c.grenade)
        for (Puff &p : c.puffs)
        {
            if (!p.sprite) continue;
            const Vector point = Position(c, p);
            const float distance = (point-origin).Length();
            if (distance >= 350) continue;
            TraceResult tr;
            UTIL_TraceLine(origin, point, ignore_monsters, nullptr, &tr);
            if (tr.fStartSolid || tr.fAllSolid || tr.flFraction != 1.0f) continue;
            const float strength = Clamp((350-distance)/150, 0, 1)*Clamp(damage/75, 0, 1);
            p.blast = Q_max(p.blast*(1-Clamp((gpGlobals->time-p.clearUntil),0,1)), strength);
            p.clearUntil = gpGlobals->time+1.5f;
            affected = true;
        }
    if (affected) ++blasts;
}
bool OC2_SmokeInside(const Vector &point)
{
    if (!Finite(point)) return false;
    float transmission=1;
    for (const Cloud &c : clouds) if (c.grenade)
        for (const Puff &p : c.puffs)
            if ((point-Position(c,p)).LengthSquared() < PuffRadius*PuffRadius*Growth(c)*Growth(c))
            {transmission*=1-Density(c,p);if(transmission<0.65f)return true;}
    return false;
}
bool OC2_SmokeBlocks(const Vector &from, const Vector &to)
{
    if (!Finite(from) || !Finite(to)) return false;
    Vector direction = to-from;
    const float length = direction.Length();
    if (length < 0.001f) return OC2_SmokeInside(from);
    direction = direction*(1/length);
    float total = 0;
    for (const Cloud &c : clouds) if (c.grenade)
    {
        float coverageSum = 0;
        for (const Puff &p : c.puffs)
        {
            const float density = Density(c,p);
            if (density <= 0) continue;
            const Vector delta = Position(c,p)-from;
            const float along = DotProduct(delta,direction);
            const float radius = PuffRadius*Growth(c);
            const float half2 = radius*radius-(delta.LengthSquared()-along*along);
            if (half2 <= 0) continue;
            const float half = std::sqrt(half2);
            const float coverage = Q_max(0.0f, Q_min(length,along+half)-Q_max(0.0f,along-half));
            coverageSum += coverage*density;
        }
        total += coverageSum; // alpha-blended puffs also accumulate opacity
        if (total > 70) return true;
    }
    return false;
}
void OC2_SmokeDump()
{
    int active = 0, puffs = 0;
    for (const Cloud &c : clouds) if (c.grenade)
    {
        ++active;
        float density = 0;
        for (const Puff &p : c.puffs) if (p.sprite) { ++puffs; density += Density(c,p); }
        ALERT(at_console,"oc2smoke_cloud: index=%d age=%.3f density=%.3f origin=%.2f,%.2f,%.2f blocked=%d\n",
            static_cast<CBaseEntity *>(c.grenade)->entindex(),gpGlobals->time-c.start,density/Puffs,
            c.origin.x,c.origin.y,c.origin.z,OC2_SmokeBlocks(c.origin-Vector(250,0,0),c.origin+Vector(250,0,0)));
    }
    ALERT(at_console,"oc2smoke_state: enabled=%d clouds=%d puffs=%d blasts=%d lifetime=%.1f\n",OC2_SmokeEnabled(),active,puffs,blasts,Lifetime);
}
