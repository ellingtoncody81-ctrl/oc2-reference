#ifndef OC2_WEAPON_PARAMS_H
#define OC2_WEAPON_PARAMS_H

#include <math.h>
#include <float.h>

// Local CS:GO 2019 items_game.txt: all 24 inherited weapon prefabs,
// including statted_item_base inheritance. Schema accuracy units are * 0.001.
// Kept byte-identical in the independently built client and ReGameDLL trees.
namespace oc2_weapon
{
enum Kind { Nova, MP7, P250, SG553, MP9, AK47, AUG, AWP, Deagle, DualBerettas, FAMAS, FiveSeven, G3SG1, GalilAR, Glock18, M249, M4A1S, MAC10, P90, SSG08, SCAR20, UMP45, USPS, XM1014, GunCount };

struct Params
{
	int damage, clip, reserve, pellets, price;
	float penetration;
	float cycle, range, rangeModifier, speed[2], spread[2];
	float crouch[2], stand[2], fire, move[2], jump, jumpInitial, ladder[2];
	float recoveryCrouch, recoveryStand, land;
	int recoilSeed;
	bool fullAuto;
	float recoilAngleVariance, recoilMagnitude[2], recoilMagnitudeVariance;
	int spreadSeed;
	float armorRatio, burstCycle, burstSpacing, zoomCycle;
	float fireAlt,landAlt,recoilAngleVarianceAlt,recoilMagnitudeVarianceAlt;
};

inline const Params &Get(Kind kind)
{
	static const Params values[] = {
		// Nova <- weapon_nova_prefab
		{26, 8, 32, 9, 1200, 0, 0.88f, 3000, 0.7f, {220,220}, {0.04f,0.04f}, {0.00525f,0.00525f}, {0.007f,0.007f}, 0.00972f, {0.03675f,0.03675f}, 0.126309998f, 0.109699997f, {0.07875f,0.07875f}, 0.328941f, 0.460517f, 0.000236f, 7763, false, 20, {143,143}, 22, 17514, 1, 0.88f, 0, 0.88f, 0.009720000f, 0.000236000f, 20.000000000f, 22.000000000f},
		// MP7 <- weapon_mp7_prefab
		{29, 30, 120, 1, 1500, 1, 0.08f, 3600, 0.85f, {220,220}, {0.0006f,0.0006f}, {0.00592f,0.00592f}, {0.01f,0.01f}, 0.00218f, {0.019860001f,0.019860001f}, 0.059599998f, 0.05541f, {0.057560001f,0.057560001f}, 0.312494f, 0.437491f, 0.000115f, 61649, true, 70, {16,16}, 1, 0, 1.25f, 0.08f, 0, 0.08f, 0.002180000f, 0.000115000f, 70.000000000f, 1.000000000f},
		// P250 <- weapon_p250_prefab
		{38, 13, 26, 1, 300, 1, 0.15f, 4096, 0.9f, {240,240}, {0.002f,0.002f}, {0.00683f,0.00683f}, {0.0091f,0.0091f}, 0.052450001f, {0.02f,0.01341f}, 0.092959999f, 0.096620003f, {0.138f,0.138f}, 0.287823f, 0.345388f, 0.00019f, 9788, false, 10, {26,26}, 3, 0, 1.28f, 0.15f, 0, 0.15f, 0.052450001f, 0.000190000f, 10.000000000f, 3.000000000f},
		// SG553 <- weapon_sg556_prefab
		{30, 30, 90, 1, 2750, 2, 0.09f, 8192, 0.98f, {210,150}, {0.0005f,0.0003f}, {0.00284f,0.00104f}, {0.00378f,0.00218f}, 0.00668f, {0.136009995f,0.136009995f}, 0.109f, 0.078790001f, {0.083660004f,0.138757996f}, 0.379204f, 0.452886f, 0.000188f, 43500, true, 60, {28,19}, 2, 0, 2, 0.09f, 0, 0.09f, 0.006680000f, 0.000188000f, 60.000000000f, 2.000000000f},
		// MP9 <- weapon_mp9_prefab
		{26, 30, 120, 1, 1250, 1, 0.07f, 3600, 0.87f, {240,240}, {0.0006f,0.0006f}, {0.0055f,0.0055f}, {0.009f,0.009f}, 0.0037f, {0.029040001f,0.029040001f}, 0.01843f, 0.037279999f, {0.148912506f,0.148912506f}, 0.184207f, 0.25789f, 5.6e-05f, 50729, true, 70, {19,19}, 1, 0, 1.2f, 0.07f, 0, 0.07f, 0.003700000f, 0.000056000f, 70.000000000f, 1.000000000f},
		// AK47 <- weapon_ak47_prefab
		{36, 30, 90, 1, 2700, 2, 0.1f, 8192, 0.98f, {215,215}, {0.0006f,0.0006f}, {0.00481f,0.00481f}, {0.00641f,0.00641f}, 0.0078f, {0.175059998f,0.175059998f}, 0.140759995f, 0.100940002f, {0.14f,0.14f}, 0.305257f, 0.368f, 0.000242f, 223, true, 70, {30,30}, 0, 0, 1.55f, 0.1f, 0, 0.1f, 0.007800000f, 0.000242000f, 70.000000000f, 0.000000000f},
		// AUG <- weapon_aug_prefab
		{28, 30, 90, 1, 3150, 2, 0.09f, 8192, 0.98f, {220,150}, {0.0005f,0.0003f}, {0.00288f,0.00101f}, {0.00385f,0.00212f}, 0.00616f, {0.135449997f,0.105449997f}, 0.105989998f, 0.101559998f, {0.110040001f,0.100040001f}, 0.30552f, 0.429727f, 0.000208f, 24204, true, 60, {24,16}, 0, 0, 1.8f, 0.09f, 0, 0.09f, 0.006160000f, 0.000208000f, 60.000000000f, 0.000000000f},
		// AWP <- weapon_awp_prefab
		{115, 10, 30, 1, 4750, 2.5f, 1.455f, 8192, 0.99f, {200,100}, {0.0002f,0.0002f}, {0.060599998f,0.0015f}, {0.080800003f,0.002f}, 0.053849998f, {0.176479996f,0.176479996f}, 0.133830002f, 0.172860001f, {0.1365f,0.1365f}, 0.24671f, 0.34539f, 0.000307f, 4100, false, 20, {78,78}, 15, 0, 1.95f, 1.455f, 0, 1.455f, 0.053849998f, 0.000100000f, 20.000000000f, 15.000000000f},
		// Deagle <- weapon_deagle_prefab
		{63, 7, 35, 1, 700, 2, 0.225f, 4096, 0.81f, {230,230}, {0.002f,0.002f}, {0.00218f,0.00218f}, {0.0042f,0.0042f}, 0.072230003f, {0.048099998f,0.048099998f}, 0.371549988f, 0.217270004f, {0.152f,0.152f}, 0.449927f, 0.8112f, 0.00073f, 1454, false, 60, {48.200001f,48.200001f}, 18, 0, 1.864f, 0.225f, 0, 0.225f, 0.072230003f, 0.000730000f, 60.000000000f, 18.000000000f},
		// DualBerettas <- weapon_elite_prefab
		{38, 30, 120, 1, 400, 1, 0.12f, 4096, 0.79f, {240,240}, {0.002f,0.002f}, {0.00525f,0.0075f}, {0.007f,0.01f}, 0.01116f, {0.01785f,0.01785f}, 0.158419998f, 0.095860001f, {0.102f,0.102f}, 0.437491f, 0.524989f, 0.000255f, 24563, false, 20, {27,27}, 4, 0, 1.15f, 0.12f, 0, 0.12f, 0.011960000f, 0.000255000f, 20.000000000f, 4.000000000f},
		// FAMAS <- weapon_famas_prefab
		{30, 25, 90, 1, 2250, 2, 0.09f, 8192, 0.96f, {220,220}, {0.0006f,0.0006f}, {0.00739f,0.00395f}, {0.00985f,0.00369f}, 0.0067f, {0.099339996f,0.099339996f}, 0.110389999f, 0.094769997f, {0.118716003f,0.118716003f}, 0.12f, 0.25f, 0.000205f, 39623, true, 60, {20,20}, 1, 0, 1.4f, 0.55f, 0.075f, 0.09f, 0.003350000f, 0.000205000f, 50.000000000f, 1.000000000f},
		// FiveSeven <- weapon_fiveseven_prefab
		{32, 20, 100, 1, 500, 1, 0.15f, 4096, 0.81f, {240,240}, {0.002f,0.002f}, {0.00683f,0.00683f}, {0.0091f,0.0091f}, 0.025f, {0.04f,0.01341f}, 0.089699997f, 0.099879997f, {0.138f,0.138f}, 0.2f, 0.2f, 0.00019f, 33244, false, 5, {25,25}, 4, 0, 1.823f, 0.15f, 0, 0.15f, 0.032450001f, 0.000190000f, 5.000000000f, 4.000000000f},
		// G3SG1 <- weapon_g3sg1_prefab
		{80, 20, 90, 1, 5000, 2.5f, 0.25f, 8192, 0.98f, {215,120}, {0.0003f,0.0003f}, {0.01935f,0.0015f}, {0.025799999f,0.002f}, 0.018610001f, {0.150479996f,0.150479996f}, 0.153770004f, 0.107690002f, {0.116389999f,0.116389999f}, 0.388808f, 0.544331f, 0.000262f, 29908, true, 30, {30,30}, 4, 0, 1.65f, 0.25f, 0, 0.25f, 0.018610001f, 0.000262000f, 30.000000000f, 4.000000000f},
		// GalilAR <- weapon_galilar_prefab
		{30, 35, 90, 1, 2000, 2, 0.09f, 8192, 0.98f, {215,215}, {0.0006f,0.0006f}, {0.00658f,0.00484f}, {0.00877f,0.00778f}, 0.00878f, {0.123559998f,0.106519997f}, 0.149779999f, 0.105389999f, {0.113580002f,0.113580002f}, 0.15f, 0.3f, 0.000256f, 51191, true, 70, {21,21}, 1, 0, 1.55f, 0.09f, 0, 0.09f, 0.005850000f, 0.000256000f, 70.000000000f, 1.000000000f},
		// Glock18 <- weapon_glock_prefab
		{30, 20, 120, 1, 200, 1, 0.15f, 4096, 0.85f, {240,240}, {0.002f,0.015f}, {0.0042f,0.003f}, {0.0056f,0.0056f}, 0.056f, {0.01f,0.01295f}, 0.087870003f, 0.096620003f, {0.137f,0.11925f}, 0.2f, 0.2f, 0.000185f, 4484, false, 20, {18,30}, 0, 0, 0.94f, 0.5f, 0.05f, 0.15f, 0.045000000f, 0.000185000f, 20.000000000f, 5.000000000f},
		// M249 <- weapon_m249_prefab
		{32, 100, 200, 1, 5200, 2, 0.08f, 8192, 0.97f, {195,195}, {0.002f,0.002f}, {0.00534f,0.00534f}, {0.0077f,0.0077f}, 0.00356f, {0.15625f,0.15625f}, 0.279470001f, 0.118269997f, {0.132809998f,0.132809998f}, 0.592093f, 0.828931f, 0.000398f, 50310, true, 50, {25,25}, 2, 0, 1.6f, 0.08f, 0, 0.08f, 0.003560000f, 0.000398000f, 50.000000000f, 2.000000000f},
		// M4A1S <- weapon_m4a1_silencer_prefab
		{33, 20, 60, 1, 3100, 2, 0.1f, 8192, 0.99f, {225,225}, {0.0006f,0.0005f}, {0.0041f,0.0041f}, {0.0049f,0.0049f}, 0.012f, {0.092879997f,0.122f}, 0.099699997f, 0.096769997f, {0.110994003f,0.113671997f}, 0.2421f, 0.338941f, 0.000197f, 38965, true, 65, {25,21}, 3, 0, 1.4f, 0.1f, 0.09f, 0.1f, 0.007000000f, 0.000197000f, 65.000000000f, 0.000000000f},
		// MAC10 <- weapon_mac10_prefab
		{29, 30, 100, 1, 1050, 1, 0.075f, 3600, 0.8f, {240,240}, {0.0006f,0.0006f}, {0.00998f,0.00998f}, {0.0133f,0.0133f}, 0.00476f, {0.01399f,0.01399f}, 0.033299999f, 0.034990002f, {0.034259998f,0.034259998f}, 0.285521f, 0.399729f, 6.9e-05f, 34079, true, 70, {18,18}, 1, 0, 1.15f, 0.075f, 0, 0.075f, 0.004760000f, 0.000069000f, 70.000000000f, 1.000000000f},
		// P90 <- weapon_p90_prefab
		{26, 50, 100, 1, 2350, 1, 0.07f, 3700, 0.86f, {230,230}, {0.001f,0.001f}, {0.01024f,0.01024f}, {0.01365f,0.01365f}, 0.00285f, {0.031f,0.031f}, 0.090080002f, 0.104599998f, {0.132169998f,0.132169998f}, 0.265784f, 0.372098f, 8.2e-05f, 6213, true, 70, {16,16}, 1, 0, 1.38f, 0.07f, 0, 0.07f, 0.002850000f, 0.000082000f, 70.000000000f, 1.000000000f},
		// SSG08 <- weapon_ssg08_prefab
		{88, 10, 90, 1, 1700, 2.5f, 1.25f, 8192, 0.98f, {230,230}, {0.00028f,0.00023f}, {0.023780001f,0.0028f}, {0.031700001f,0.003f}, 0.02292f, {0.123449997f,0.123449997f}, 0.00572f, 0.208720001f, {0.095489998f,0.095489998f}, 0.055783f, 0.142096f, 0.000215f, 1278, false, 20, {33,33}, 15, 0, 1.7f, 1.25f, 0, 1.25f, 0.022920000f, 0.000215000f, 20.000000000f, 15.000000000f},
		// SCAR20 <- weapon_scar20_prefab
		{80, 20, 90, 1, 5000, 2.5f, 0.25f, 8192, 0.98f, {215,120}, {0.0003f,0.0003f}, {0.01935f,0.0015f}, {0.025799999f,0.002f}, 0.018610001f, {0.150479996f,0.150479996f}, 0.153770004f, 0.107690002f, {0.116389999f,0.116389999f}, 0.388808f, 0.544331f, 0.000262f, 19364, true, 30, {31,31}, 4, 0, 1.65f, 0.25f, 0, 0.25f, 0.018610001f, 0.000262000f, 30.000000000f, 4.000000000f},
		// UMP45 <- weapon_ump45_prefab
		{35, 25, 100, 1, 1200, 1, 0.09f, 3700, 0.75f, {230,230}, {0.001f,0.001f}, {0.01007f,0.01007f}, {0.01343f,0.01343f}, 0.00342f, {0.02876f,0.02876f}, 0.03725f, 0.047209999f, {0.042349998f,0.042349998f}, 0.249995f, 0.349993f, 8.5e-05f, 59299, true, 40, {23,23}, 1, 0, 1.3f, 0.09f, 0, 0.09f, 0.003420000f, 0.000085000f, 40.000000000f, 1.000000000f},
		// USPS <- weapon_usp_silencer_prefab
		{35, 12, 24, 1, 200, 1, 0.17f, 4096, 0.91f, {240,240}, {0.0025f,0.0015f}, {0.00368f,0.00368f}, {0.0049f,0.0049f}, 0.071f, {0.01387f,0.01387f}, 0.094480003f, 0.096599998f, {0.138320007f,0.119900002f}, 0.291277f, 0.349532f, 0.000191f, 5426, false, 0, {29,23}, 0, 0, 1.01f, 0.17f, 0, 0.17f, 0.052000000f, 0.000198000f, 0.000000000f, 0.000000000f},
		// XM1014 <- weapon_xm1014_prefab
		{20, 7, 32, 6, 2000, 1, 0.35f, 3000, 0.7f, {215,215}, {0.038f,0.038f}, {0.00525f,0.00525f}, {0.007f,0.007f}, 0.00883f, {0.036029999f,0.036029999f}, 0.130830002f, 0.100379997f, {0.077209999f,0.077209999f}, 0.361835f, 0.506569f, 0.000232f, 24862, true, 20, {80,80}, 20, 817955, 1.6f, 0.35f, 0, 0.35f, 0.008830000f, 0.000232000f, 20.000000000f, 20.000000000f}
	};
	return values[static_cast<int>(kind)];
}

// Legacy IDs stay on the wire; both builds resolve the same schema here.
inline bool KindForId(int id, Kind &kind)
{
    switch(id) {
	case 21: kind=Nova; return true;
	case 19: kind=MP7; return true;
	case 1: kind=P250; return true;
	case 27: kind=SG553; return true;
	case 23: kind=MP9; return true;
	case 28: kind=AK47; return true;
	case 8: kind=AUG; return true;
	case 18: kind=AWP; return true;
	case 26: kind=Deagle; return true;
	case 10: kind=DualBerettas; return true;
	case 15: kind=FAMAS; return true;
	case 11: kind=FiveSeven; return true;
	case 24: kind=G3SG1; return true;
	case 14: kind=GalilAR; return true;
	case 17: kind=Glock18; return true;
	case 20: kind=M249; return true;
	case 22: kind=M4A1S; return true;
	case 7: kind=MAC10; return true;
	case 30: kind=P90; return true;
	case 3: kind=SSG08; return true;
	case 13: kind=SCAR20; return true;
	case 12: kind=UMP45; return true;
	case 16: kind=USPS; return true;
	case 5: kind=XM1014; return true;

    default: return false;
    }
}
inline const Params *FromId(int id) { Kind kind;return KindForId(id,kind)?&Get(kind):nullptr; }
inline int Mode(int id,int state,float fov)
{
    if (id==17) return (state&(1<<1))?1:0;
    if (id==15) return (state&(1<<4))?1:0;
    if (id==16) return (state&1)?0:1; // Source primary mode is suppressed.
    if (id==22) return (state&(1<<2))?0:1;
    return (id==8 || id==27 || id==3 || id==18 || id==13 || id==24) && fov>0 && fov<90 ? 1:0;
}
// Source IsFullAuto() excludes the selectable three-round burst modes.
inline bool BurstMode(int id,int state) { return (id==17 && (state&(1<<1))) || (id==15 && (state&(1<<4))); }
inline bool RequiresTriggerRelease(int id,int state) { const Params *p=FromId(id);return p && (!p->fullAuto || BurstMode(id,state)); }
inline bool DirectShotAdapter(int id) { return id==21 || id==19 || id==1 || id==27 || id==23; }
inline bool SupportsAim(int id) { return FromId(id) && id!=10 && id!=3 && id!=18 && id!=13 && id!=24; }
inline float Cycle(int id,int state,float fov)
{
    const Params *p=FromId(id);if (!p) return .1f;
    if ((id==17 && (state&(1<<1))) || (id==15 && (state&(1<<4)))) return p->burstCycle;
    if ((id==8 || id==27) && fov>0 && fov<90) return p->zoomCycle;
    return p->cycle;
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
	penaltyCode = floorf(Clamp(sample.penalty + (mode?p.fireAlt:p.fire), 0, 1) * 131072) / 128;
	indexCode = floorf(Clamp(sample.recoilIndex + 1, 0, 128) * 8192) / 128;
	// Xash writes int((double)value * 1000), not a rounded integer. A .25 ms
	// positive bias survives float storage throughout the 4096-second range,
	// yet still truncates to the intended millisecond on the wire. SampleShot
	// normalizes both the biased local and decoded network values identically.
	lastShot = static_cast<float>((ShotClockMillis(now) + .25) / 1000.0);
	displaySpread = Clamp(sample.inaccuracy + (mode?p.fireAlt:p.fire) + p.spread[mode], 0, 1);
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

inline void Recoil(const Params &p, int mode, int shotIndex, unsigned int seed, float &pitch, float &yaw, bool burst=false)
{
	const int target = p.fullAuto && !burst ? (shotIndex & 63) : int((seed & 0x7fffffffu) % 64u);
	RandomStream random(p.recoilSeed);
	float angle = 0, magnitude = 0;
	for (int i = 0; i <= target; ++i)
	{
		const float a = random.Float(-(mode?p.recoilAngleVarianceAlt:p.recoilAngleVariance), (mode?p.recoilAngleVarianceAlt:p.recoilAngleVariance));
		const float r = p.recoilMagnitude[mode] + random.Float(-(mode?p.recoilMagnitudeVarianceAlt:p.recoilMagnitudeVariance), (mode?p.recoilMagnitudeVarianceAlt:p.recoilMagnitudeVariance));
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
