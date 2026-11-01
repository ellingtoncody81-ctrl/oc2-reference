#pragma once

#ifndef CLIENT_DLL
#include "cbase.h"
#include <vector>

class CWeaponBox;

// One context per physical bullet/pellet. Reuse it for every penetration segment.
// The caller passes the ORIGINAL world trace endpoint; this never blocks damage.
class OC2_WeaponPhysicsBullet
{
public:
	OC2_WeaponPhysicsBullet();
	void Trace(const Vector &source, const Vector &end, float damage, int bulletType, entvars_t *attacker);

private:
	unsigned long long m_id;
	std::vector<EHANDLE> m_hit;
};

void OC2_WeaponPhysicsFrame();
void OC2_WeaponPhysicsDump(CWeaponBox *box = nullptr);
bool OC2_WeaponPhysicsAimPoint(CWeaponBox *box, Vector &point, float offset = 0.0f);
bool OC2_WeaponPhysicsIsAwake(CWeaponBox *box);
void OC2_WeaponPhysicsStop(CWeaponBox *box);
#endif
