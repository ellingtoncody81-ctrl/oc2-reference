#pragma once

// CS1.6 predicted weapon spread, adapted as the precision input to CS:GO bob.
// This is not the CS:GO item-schema inaccuracy model. No engine state is read.
namespace oc2_weapon_spread
{
struct SpreadInput
{
	int weapon_id = 0;
	float accuracy = 0.0f; // Current predicted CBasePlayerWeapon::m_flAccuracy.
	int weapon_state = 0; // WPNSTATE_* bits from dlls/weapontype.h.
	float speed = 0.0f; // Horizontal velocity length, in GoldSrc units/second.
	bool grounded = true;
	bool ducking = false;
	float fov = 90.0f; // Current FOV; zero means the normal 90 degree FOV.
};

struct SpreadOutput
{
	float stand = 0.0f; // Stationary, grounded spread at the Deploy accuracy.
	float crouch = 0.0f; // Same baseline, with FL_DUCKING.
	float current = 0.0f; // Potential spread using the current predicted accuracy.
};

// Silencer/burst/FOV state applies to both baseline and current spread.
// A prediction snapshot cannot reconstruct the already emitted bullet: most
// Fire methods update accuracy after choosing flSpread; G3SG1 multiplies with
// its updated accuracy instead. Use current for the continuous bob input.
// Burst mode describes PrimaryAttack's first round. FireRemaining emits the
// later Glock rounds with a fixed 0.05 cone and FAMAS rounds with saved spread;
// their pending-shot timing is deliberately outside this continuous API.
// M3/XM1014 return their fixed horizontal pellet cone, so they add no dynamic
// accuracy drift. Knife, grenades, C4 and unrecognized IDs return zero.
SpreadOutput Calculate(const SpreadInput &input);
}
