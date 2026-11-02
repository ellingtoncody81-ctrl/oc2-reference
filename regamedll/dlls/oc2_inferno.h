#pragma once

class CGrenade;
void OC2_InfernoPrecache();
CGrenade *OC2_ThrowMolotov(entvars_t *owner, const Vector &source, const Vector &velocity, bool incendiary = false);
void OC2_InfernoSmokeDetonate(const Vector &origin, bool modern = false, int team = 0);
bool OC2_InfernoNearest(const Vector &point, float range, Vector *fire);
bool OC2_InfernoDanger(const Vector &point);
bool OC2_InfernoNearTeam(const Vector &point, float range, int team, float remaining = 0);
bool OC2_LegacySmokeNearTeam(const Vector &point, float range, int team);
// Called only from grenade BounceTouch. True means caller should SG_Detonate()
// and return; this predicate does not itself detonate the smoke grenade.
bool OC2_InfernoSmokeTouch(CGrenade *smoke);
// Call at RestartRound and ServerDeactivate, while current edicts remain valid.
void OC2_InfernoRoundReset();
void OC2_InfernoDump();
