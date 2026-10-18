#include "weapon_spread.h"
#include "../dlls/wpn_shared/oc2_weapon_params.h"

#include <cmath>

namespace oc2_weapon_spread
{
namespace
{
// Controlled copies of the client IDs and state bits in dlls/weapontype.h.
enum WeaponId
{
	P228 = 1, SCOUT = 3, XM1014 = 5, MAC10 = 7, AUG = 8, ELITE = 10,
	FIVESEVEN = 11, UMP45 = 12, SG550 = 13, GALIL = 14, FAMAS = 15,
	USP = 16, GLOCK18 = 17, AWP = 18, MP5N = 19, M249 = 20, M3 = 21,
	M4A1 = 22, TMP = 23, G3SG1 = 24, DEAGLE = 26, SG552 = 27,
	AK47 = 28, P90 = 30
};
enum WeaponState
{
	USP_SILENCED = 1 << 0, GLOCK18_BURST = 1 << 1,
	M4A1_SILENCED = 1 << 2, FAMAS_BURST = 1 << 4
};

const oc2_weapon::Params *SchemaWeapon(int id)
{
	return oc2_weapon::FromId(id);
}

float DefaultAccuracy(int id)
{
	// Values assigned by each dlls/wpn_shared/wpn_*.cpp Deploy method.
	switch (id)
	{
	case DEAGLE: case GLOCK18: return 0.9f;
	case ELITE: return 0.88f;
	case FIVESEVEN: case USP: return 0.92f;
	case MAC10: return 0.15f;
	case AK47: case AUG: case GALIL: case FAMAS: case M249:
	case M4A1: case G3SG1: case P90: return 0.2f;
	// UMP45 explicitly resets to zero. SG550 never initializes accuracy
	// in Spawn/Deploy: its static predicted entity begins at zero and retains
	// accuracy on redeploy. Zero here describes that initial baseline only.
	default: return 0.0f;
	}
}

float PistolSpread(const SpreadInput &in, float a,
	float air, float moving, float crouch, float stand)
{
	const float scale = !in.grounded ? air : in.speed > 0.0f ? moving
		: in.ducking ? crouch : stand;
	return scale * (1.0f - a);
}

float RifleSpread(const SpreadInput &in, float a,
	float air_base, float air_scale, float moving_base, float moving_scale,
	float stationary_scale, float threshold = 140.0f)
{
	if (!in.grounded) return air_base + air_scale * a;
	if (in.speed > threshold) return moving_base + moving_scale * a;
	return stationary_scale * a;
}

float SpreadAt(const SpreadInput &in, float a)
{
	const bool unscoped = in.fov == 90.0f;
	switch (in.weapon_id)
	{
	// PrimaryAttack branch ordering is air, movement, duck, stand; rifle
	// ducking does not change the PrimaryAttack spread in this client.
	case FIVESEVEN:
		return PistolSpread(in, a, 1.5f, 0.255f, 0.075f, 0.15f);
	case DEAGLE:
		return PistolSpread(in, a, 1.5f, 0.25f, 0.115f, 0.13f);
	case ELITE:
		return PistolSpread(in, a, 1.3f, 0.175f, 0.08f, 0.1f);
	case GLOCK18:
		return (in.weapon_state & GLOCK18_BURST)
			? PistolSpread(in, a, 1.2f, 0.185f, 0.095f, 0.3f)
			: PistolSpread(in, a, 1.0f, 0.165f, 0.075f, 0.1f);
	case USP:
		return (in.weapon_state & USP_SILENCED)
			? PistolSpread(in, a, 1.3f, 0.25f, 0.125f, 0.15f)
			: PistolSpread(in, a, 1.2f, 0.225f, 0.08f, 0.1f);
	case AK47:
		return RifleSpread(in, a, 0.04f, 0.4f, 0.04f, 0.07f, 0.0275f);
	case AUG:
		return RifleSpread(in, a, 0.035f, 0.4f, 0.035f, 0.07f, 0.02f);
	case GALIL:
		return RifleSpread(in, a, 0.04f, 0.3f, 0.04f, 0.07f, 0.0375f);
	case FAMAS:
		// FamasFire adds 0.01 in full-auto mode after PrimaryAttack.
		return RifleSpread(in, a, 0.03f, 0.3f, 0.03f, 0.07f, 0.02f)
			+ ((in.weapon_state & FAMAS_BURST) ? 0.0f : 0.01f);
	case M4A1:
		return RifleSpread(in, a, 0.035f, 0.4f, 0.035f, 0.07f,
			(in.weapon_state & M4A1_SILENCED) ? 0.025f : 0.02f);
	case M249:
		return RifleSpread(in, a, 0.045f, 0.5f, 0.045f, 0.095f, 0.03f);
	case P90:
		return RifleSpread(in, a, 0.0f, 0.3f, 0.0f, 0.115f, 0.045f, 170.0f);
	case MAC10: return (in.grounded ? 0.03f : 0.375f) * a;
	case UMP45: return (in.grounded ? 0.04f : 0.24f) * a;
	case AWP:
		return (!in.grounded ? 0.85f : in.speed > 140.0f ? 0.25f
			: in.speed > 10.0f ? 0.1f : in.ducking ? 0.0f : 0.001f)
			+ (unscoped ? 0.08f : 0.0f);
	case SCOUT:
		return (!in.grounded ? 0.2f : in.speed > 170.0f ? 0.075f
			: in.ducking ? 0.0f : 0.007f) + (unscoped ? 0.025f : 0.0f);
	case G3SG1:
		// G3SG1Fire scales the whole value, including the no-scope penalty.
		return (1.0f - a) * ((!in.grounded ? 0.45f : in.speed > 0.0f
			? 0.15f : in.ducking ? 0.035f : 0.055f)
			+ (unscoped ? 0.025f : 0.0f));
	case SG550:
		// SG550's moving spread alone is independent of accuracy; its
		// no-scope penalty is added after the accuracy-scaled branches.
		return (!in.grounded ? 0.45f * (1.0f - a) : in.speed > 0.0f
			? 0.15f : (in.ducking ? 0.04f : 0.05f) * (1.0f - a))
			+ (unscoped ? 0.025f : 0.0f);
	case XM1014: return 0.0725f; // XM1014_CONE_VECTOR.x.
	default: return 0.0f;
	}
}

float SafeSpread(float value)
{
	return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}
}

SpreadOutput Calculate(const SpreadInput &input)
{
	SpreadInput in = input;
	if (!std::isfinite(in.speed) || in.speed < 0.0f) in.speed = 0.0f;
	if (!std::isfinite(in.fov) || in.fov <= 0.0f) in.fov = 90.0f;
	if (const oc2_weapon::Params *schema = SchemaWeapon(in.weapon_id))
	{
		const int mode = oc2_weapon::Mode(in.weapon_id,in.weapon_state,in.fov);
		SpreadOutput output;
		output.stand = schema->stand[mode] + schema->spread[mode];
		output.crouch = schema->crouch[mode] + schema->spread[mode];
		output.current = std::isfinite(in.accuracy)
			? oc2_weapon::Clamp(in.accuracy, 0.0f, 1.0f) : output.stand;
		return output;
	}
	if (!std::isfinite(in.accuracy)) in.accuracy = DefaultAccuracy(in.weapon_id);
	// The real weapon values are at most 1.65 (MAC10). A broad finite bound
	// preserves those values while preventing corrupted state from reaching bob.
	if (in.accuracy < 0.0f) in.accuracy = 0.0f;
	if (in.accuracy > 2.0f) in.accuracy = 2.0f;
	SpreadOutput output;
	output.current = SafeSpread(SpreadAt(in, in.accuracy));
	in.grounded = true;
	in.speed = 0.0f;
	in.ducking = false;
	const float baseline = DefaultAccuracy(in.weapon_id);
	output.stand = SafeSpread(SpreadAt(in, baseline));
	in.ducking = true;
	output.crouch = SafeSpread(SpreadAt(in, baseline));
	return output;
}
}
