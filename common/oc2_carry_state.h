#pragma once

// OC2Carry user message, version 1. Counts are authoritative ammo amounts;
// before throw recovery they include the grenade currently held in hand.
// A Reset precedes a complete snapshot for a newly connected client.
namespace oc2_carry
{
static const char *const kMessage="OC2Carry";
enum { kVersion=2, Reset=0, Upsert=1, Remove=2 };
// ThrowReleased means the active grenade is in throw recovery (release latch
// -2). Projectile creation may fail, in which case ammo is not decremented.
enum { HasC4=1, Incendiary=2, ThrowReleased=4 };
enum { kResetBytes=2, kRemoveBytes=7, kUpsertBytes=14 };
// Wire layout, all integer fields little-endian:
// version:byte, operation:byte;
// Remove: player:byte, userid:long;
// Upsert: player:byte, userid:long, active:byte, flash:byte, he:byte,
//         smoke:byte, fire:byte, flags:byte, taser:byte.
struct State
{
 int player=0,userid=0,active=0;
 int flash=0,he=0,smoke=0,fire=0,flags=0;
 int taser=0;
};
}
