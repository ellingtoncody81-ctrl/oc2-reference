#ifndef OC2_WEAPON_PARAMS_H
#define OC2_WEAPON_PARAMS_H

#include <math.h>
#include <float.h>

// Local CS:GO 2019 items_game.txt prefabs (16361/17053/17327/18440/19333),
// including statted_item_base inheritance. Schema accuracy units are * 0.001.
// Kept byte-identical in the independently built client and ReGameDLL trees.
namespace oc2_weapon
{
enum Kind { Nova, MP7, P250, SG553, MP9 };

struct Params
{
	int damage, clip, reserve, pellets, price, penetration;
	float cycle, range, rangeModifier, speed[2], spread[2];
	float crouch[2], stand[2], fire, move[2], jump, jumpInitial, ladder[2];
	float recoveryCrouch, recoveryStand, land;
	int recoilSeed;
	bool fullAuto;
	float recoilAngleVariance, recoilMagnitude[2], recoilMagnitudeVariance;
	int spreadSeed;
};

inline const Params &Get(Kind kind)
{
	static const Params values[] = {
		{26, 8, 32, 9, 1200, 0, .88f, 3000, .70f, {220,220}, {.040f,.040f},
		 {.00525f,.00525f}, {.007f,.007f}, .00972f, {.03675f,.03675f}, .126309998f, .109699997f, {.07875f,.07875f},
		 .328941f,.460517f,.000236f,7763,false,20,{143,143},22,17514},
		{29, 30, 120, 1, 1500, 1, .08f, 3600, .85f, {220,220}, {.0006f,.0006f},
		 {.00592f,.00592f}, {.010f,.010f}, .00218f, {.019860001f,.019860001f}, .059599998f, .05541f, {.057560001f,.057560001f},
		 .312494f,.437491f,.000115f,61649,true,70,{16,16},1,0},
		{38, 13, 26, 1, 300, 1, .15f, 4096, .90f, {240,240}, {.002f,.002f},
		 {.00683f,.00683f}, {.0091f,.0091f}, .052450001f, {.020f,.01341f}, .092959999f, .096620003f, {.138f,.138f},
		 .287823f,.345388f,.000190f,9788,false,10,{26,26},3,0},
		{30, 30, 90, 1, 2750, 2, .09f, 8192, .98f, {210,150}, {.0005f,.0003f},
		 {.00284f,.00104f}, {.00378f,.00218f}, .00668f, {.136009995f,.136009995f}, .109f, .078790001f, {.083660004f,.138757996f},
		 .379204f,.452886f,.000188f,43500,true,60,{28,19},2,0},
		{26, 30, 120, 1, 1250, 1, .07f, 3600, .87f, {240,240}, {.0006f,.0006f},
		 {.0055f,.0055f}, {.009f,.009f}, .0037f, {.029040001f,.029040001f}, .01843f, .037279999f, {.148912506f,.148912506f},
		 .184207f,.257890f,.000056f,50729,true,70,{19,19},1,0}
	};
	return values[static_cast<int>(kind)];
}

inline float Clamp(float value, float lo, float hi)
{
	if (!(value >= lo)) return lo;
	return value > hi ? hi : value;
}

// Source vstdlib/random.cpp CUniformRandomStream; private state never changes
// either engine's global RNG and therefore remains replay/pellet deterministic.
class RandomStream
{
public:
	explicit RandomStream(int seed) : state(seed > 0 ? -seed : seed), output(0) {}
	float Float(float low = 0, float high = 1)
	{
		int k, j;
		if (state <= 0 || !output)
		{
			state = state < -1 ? -state : 1;
			for (j = 39; j >= 0; --j)
			{
				k = state / 127773;
				state = 16807 * (state - k * 127773) - 2836 * k;
				if (state < 0) state += 2147483647;
				if (j < 32) shuffle[j] = state;
			}
			output = shuffle[0];
		}
		k = state / 127773;
		state = 16807 * (state - k * 127773) - 2836 * k;
		if (state < 0) state += 2147483647;
		j = output / 67108864;
		output = shuffle[j & 31];
		shuffle[j & 31] = state;
		float unit = static_cast<float>((1.0 / 2147483647.0) * output);
		if (unit > 1.0f - 1.2e-7f) unit = 1.0f - 1.2e-7f;
		return low + unit * (high - low);
	}
private:
	int state, output, shuffle[32];
};

struct Motion
{
	float speed, verticalSpeed;
	bool grounded, crouched, ladder, walking;
};

inline float BasePenalty(const Params &p, const Motion &m, int mode)
{
	if (m.ladder) return p.ladder[mode] + p.ladder[0];
	if (!m.grounded) return p.stand[mode] + p.jump;
	return m.crouched ? p.crouch[mode] : p.stand[mode];
}

// Shot-time GoldSrc adapter to weapon_csbase.cpp GetInaccuracy/RecoveryTime.
// A constant posture between samples has the same exponential recovery. Full
// per-tick posture history, turning/landing impulses require a movement bridge;
// this helper deliberately does not invent those untransmitted state changes.
inline float Inaccuracy(const Params &p, const Motion &m, int mode,
	float previousPenalty, float elapsed, bool hasPreviousShot, float &penalty)
{
	const float base = BasePenalty(p, m, mode);
	const float recovery = !m.grounded && !m.ladder ? p.recoveryCrouch * 4 :
		(m.crouched && !m.ladder ? p.recoveryCrouch : p.recoveryStand);
	penalty = base;
	if (hasPreviousShot && previousPenalty > base)
		penalty += (Clamp(previousPenalty, 0, 1) - base) * expf(-logf(10.0f) * Clamp(elapsed, 0, 60) / recovery);
	float result = penalty;
	float movement = Clamp((m.speed / p.speed[mode] - .34f) / (.95f - .34f), 0, 1);
	if (!m.walking) movement = powf(movement, .25f);
	result += movement * p.move[mode];
	if (!m.grounded)
	{
		// Source default sv_jump_impulse; the GoldSrc movement itself is unchanged.
		const float jumpRoot = sqrtf(301.993377f);
		const float verticalRoot = sqrtf(fabsf(m.verticalSpeed));
		result += Clamp((verticalRoot - jumpRoot * .25f) / (jumpRoot * .75f), 0, 2) * p.jumpInitial;
	}
	return Clamp(result, 0, 1);
}

inline float RecoveredIndex(float index, float elapsed, float cycle)
{
	// Continuous equivalent of Source's 2.0*log(10) index decay after firing.
	return Clamp(index, 0, 1024) * expf(-2.0f * logf(10.0f) * Clamp(elapsed - cycle, 0, 60));
}

struct Sample { float inaccuracy, penalty, recoilIndex; };

// m_fAimedDamage is an unsigned 22-bit millisecond field. A 4096-second
// rolling clock fits without changing either engine's prediction protocol.
// Quantize before subtracting so a cycle such as 80 ms remains exactly one
// cycle on both sides of a network restore. This is a GoldSrc time adapter.
inline double ShotClockMillis(float time)
{
	if (!(time >= 0) || time > FLT_MAX) return 0;
	return fmod(floor(static_cast<double>(time) * 1000.0 + .5), 4096000.0);
}

inline Sample SampleShot(const Params &p, const Motion &motion, int mode,
	float now, float previousPenalty, float previousIndex, float lastShot)
{
	const double previousMillis = floor(static_cast<double>(lastShot) * 1000.0 + .5);
	double elapsedMillis = ShotClockMillis(now) - previousMillis;
	if (elapsedMillis < 0) elapsedMillis += 4096000.0;
	// A backwards clock outside the wrap boundary or >60 seconds idle is
	// fully recovered. An exact whole-period idle is ambiguous on the wire;
	// normal Spawn/Deploy resets remove that ambiguity at ownership changes.
	// A shot at the period boundary decodes to timestamp zero. The stored
	// recoil index (RecordShot always adds one) distinguishes it from Reset.
	const bool validTime = previousIndex > 0 && lastShot >= 0 && previousMillis >= 0 && previousMillis < 4096000.0 &&
		now >= 0 && now <= FLT_MAX && elapsedMillis >= 0 && elapsedMillis <= 60000.0;
	const float elapsed = validTime ? static_cast<float>(elapsedMillis * .001) : 60;
	Sample result;
	result.inaccuracy = Inaccuracy(p, motion, mode, previousPenalty, elapsed, validTime, result.penalty);
	result.recoilIndex = validTime ? RecoveredIndex(previousIndex, elapsed, p.cycle) : 0;
	return result;
}

// fuser2/fuser3 already travel through both prediction paths, but delta.lst
// quantizes them at 1/128. Encode small accuracy values before that transport.
inline void ResetState(const Params &p, float &penaltyCode, float &indexCode,
	float &lastShot, float &displaySpread, int &shots)
{
	penaltyCode = p.stand[0] * 1024;
	indexCode = lastShot = 0;
	displaySpread = p.stand[0] + p.spread[0];
	shots = 0;
}

inline void RecordShot(const Params &p, const Sample &sample, int mode, float now,
	float &penaltyCode, float &indexCode, float &lastShot, float &displaySpread)
{
	penaltyCode = floorf(Clamp(sample.penalty + p.fire, 0, 1) * 131072) / 128;
	indexCode = floorf(Clamp(sample.recoilIndex + 1, 0, 128) * 8192) / 128;
	// Xash writes int((double)value * 1000), not a rounded integer. A .25 ms
	// positive bias survives float storage throughout the 4096-second range,
	// yet still truncates to the intended millisecond on the wire. SampleShot
	// normalizes both the biased local and decoded network values identically.
	lastShot = static_cast<float>((ShotClockMillis(now) + .25) / 1000.0);
	displaySpread = Clamp(sample.inaccuracy + p.fire + p.spread[mode], 0, 1);
}

struct Offset { float x, y; };

inline void SpreadOffsets(const Params &p, int mode, unsigned int seed,
	float inaccuracy, float recoilIndex, Offset *offsets)
{
	const float tau = 6.2831853071795864769f;
	RandomStream shot((seed & 255u) + 1);
	RandomStream pattern(p.spreadSeed);
	float angles[64], radii[64];
	if (p.pellets > 1)
	{
		for (int i = 0; i < 64; ++i)
		{
			angles[i] = pattern.Float(0, tau);
			radii[i] = pattern.Float(float(i % p.pellets) / p.pellets, float(i % p.pellets + 1) / p.pellets);
		}
	}
	for (int i = 0; i < p.pellets; ++i)
	{
		const float r0 = shot.Float() * inaccuracy;
		const float a0 = shot.Float(0, tau);
		float a1, r1;
		const int index = i + int(Clamp(recoilIndex, 0, 1024)) * p.pellets;
		if (p.pellets > 1 && index < 64)
		{
			a1 = angles[index];
			r1 = radii[index];
		}
		else if (p.pellets > 1)
		{
			a1 = shot.Float(0, tau);
			r1 = shot.Float();
		}
		else
		{
			a1 = shot.Float(0, tau);
			r1 = shot.Float();
		}
		r1 *= p.spread[mode];
		offsets[i].x = r0 * cosf(a0) + r1 * cosf(a1);
		offsets[i].y = r0 * sinf(a0) + r1 * sinf(a1);
	}
}

inline void Recoil(const Params &p, int mode, int shotIndex, unsigned int seed, float &pitch, float &yaw)
{
	const int target = p.fullAuto ? (shotIndex & 63) : int((seed & 0x7fffffffu) % 64u);
	RandomStream random(p.recoilSeed);
	float angle = 0, magnitude = 0;
	for (int i = 0; i <= target; ++i)
	{
		const float a = random.Float(-p.recoilAngleVariance, p.recoilAngleVariance);
		const float r = p.recoilMagnitude[mode] + random.Float(-p.recoilMagnitudeVariance, p.recoilMagnitudeVariance);
		angle = p.fullAuto && i ? angle + .55f * (a - angle) : a;
		magnitude = p.fullAuto && i ? magnitude + .55f * (r - magnitude) : r;
		if (p.fullAuto && i < 4) magnitude *= .75f + .25f * float(i) / 4;
	}
	// cs_player_shared.cpp KickBack's explicit view-punch component (.055).
	// GoldSrc has one punchangle, not Source's independent aim-punch velocity /
	// HybridDecay integrator; this is an angle adapter, not that missing spring.
	const float radians = angle * (3.14159265358979323846f / 180.0f);
	pitch = -cosf(radians) * magnitude * .055f;
	yaw = -sinf(radians) * magnitude * .055f;
}
}
#endif
