#pragma once

class CGrenade;
void OC2_SmokeRegister();
void OC2_SmokePrecache();
void OC2_SmokeReset();
bool OC2_SmokeEnabled();
bool OC2_SmokeCreate(CGrenade *grenade);
bool OC2_SmokeThink(CGrenade *grenade);
void OC2_SmokeBlast(const Vector &origin, float damage);
bool OC2_SmokeInside(const Vector &point);
bool OC2_SmokeBlocks(const Vector &from, const Vector &to);
bool OC2_SmokeNearTeam(const Vector &point, float range, int team);
void OC2_SmokeDump();
