#ifndef OC2_PRESENTATION_H
#define OC2_PRESENTATION_H
#include <math.h>
// Mechanism: CS:GO weapon_csbase.cpp (sequence-length deploy),
// cs_player.cpp:11128-11190 (ACT_VM_IDLE_LOWERED and sequence-length inspect),
// animation.cpp:344-431 (weighted activity selection and prediction random).
// Timing and sequence IDs below are resource metadata, not APK gameplay code.
// Evidence generator: tools/weapon_presentation/generate_timing.py.
namespace oc2_presentation
{
// Deployed IDST v10 metadata. Inspect slots are complete independent sequences
// 8, 9 and 10; zero means that sequence is absent. All inspect FPS are 30.
// Evidence: tools/module5/gloves_inspect/inspect_metadata.py.
struct KnifeSequences { int definition, drawFrames; float drawFPS; int inspectFrames[3]; };
inline const KnifeSequences *FindKnife(int definition)
{
    static const KnifeSequences knives[] = {
        {0,31,30.0f,{187,0,0}}, {42,31,30.0f,{187,0,0}},
        {500,36,30.0f,{187,0,0}}, {503,31,30.0f,{187,118,0}},
        {505,31,30.0f,{187,0,0}}, {506,31,30.0f,{187,0,0}},
        {507,31,30.0f,{146,0,0}}, {508,31,30.0f,{187,0,0}},
        {509,31,30.0f,{187,0,0}}, {512,30,30.0f,{177,177,0}},
        {514,33,30.0f,{186,0,0}}, {515,24,23.0f,{124,136,146}},
        {516,31,30.0f,{177,0,0}}, {517,31,30.0f,{187,134,0}},
        {518,31,30.0f,{250,187,0}}, {519,31,30.0f,{187,151,0}},
        {520,31,30.0f,{187,0,0}}, {521,31,30.0f,{149,187,0}},
        {522,31,30.0f,{119,151,0}}, {523,31,30.0f,{146,81,0}},
        {525,31,30.0f,{185,67,0}}
    };
    for (unsigned i=0; i<sizeof(knives)/sizeof(knives[0]); ++i)
        if (knives[i].definition == definition) return &knives[i];
    return 0;
}
inline float KnifeDuration(int definition, int animation, float fallback)
{
    const KnifeSequences *s = FindKnife(definition);
    if (!s) return fallback;
    const int frames = animation == 3 ? s->drawFrames :
        animation >= 8 && animation <= 10 ? s->inspectFrames[animation-8] : 0;
    if (!frames) return fallback;
    const float fps = animation == 3 ? s->drawFPS : 30.0f;
    return static_cast<float>(ceil((frames-1)*1000.0/fps)/1000.0);
}

enum { InspectImpulse = 180, Draw = 1, Inspect = 2, Silencer = 3 };
struct Sequence { int weapon, animation, frames; float fps; int kind; bool incendiary; };
inline const Sequence *Find(int weapon, int animation, bool incendiary = false)
{
	static const Sequence sequences[] = {
		{1,6,31,30.000000f,1,false}, // p228: draw
		{1,7,194,30.000000f,2,false}, // p228: lookat01
		{2,1,31,30.000000f,1,false}, // zeus: draw
		{3,4,31,30.000000f,1,false}, // scout: draw
		{3,5,160,30.000000f,2,false}, // scout: inspect
		{4,3,31,30.000000f,1,false}, // hegrenade: deploy
		{5,6,31,30.000000f,1,false}, // xm1014: draw
		{5,7,138,30.000000f,2,false}, // xm1014: lookat01
		{6,1,24,30.000000f,1,false}, // c4: draw
		{7,2,31,30.000000f,1,false}, // mac10: mac10_draw
		{7,6,159,30.000000f,2,false}, // mac10: lookat01
		{8,2,36,30.000000f,1,false}, // aug: draw
		{8,6,131,30.000000f,2,false}, // aug: lookat01
		{9,3,31,30.000000f,1,false}, // smokegrenade: deploy
		{10,15,31,30.000000f,1,false}, // elite: draw
		{10,16,139,30.000000f,2,false}, // elite: lookat01
		{11,5,31,30.000000f,1,false}, // fiveseven: draw
		{11,6,194,30.000000f,2,false}, // fiveseven: lookat01
		{12,2,31,30.000000f,1,false}, // ump45: draw
		{12,6,206,30.000000f,2,false}, // ump45: lookat01
		{13,4,31,30.000000f,1,false}, // sg550: draw
		{13,5,160,30.000000f,2,false}, // sg550: lookat01
		{14,2,34,30.000000f,1,false}, // galil: draw
		{14,6,138,30.000000f,2,false}, // galil: lookat01
		{15,2,31,30.000000f,1,false}, // famas: draw
		{15,6,131,30.000000f,2,false}, // famas: lookat01
		{16,6,31,30.000000f,1,false}, // usp: draw
		{16,7,146,45.000000f,3,false}, // usp: add_silencer
		{16,14,31,30.000000f,1,false}, // usp: draw_unsil
		{16,15,146,45.000000f,3,false}, // usp: detach_silencer
		{16,16,194,30.000000f,2,false}, // usp: lookat01_masked_flash
		{16,17,194,30.000000f,2,false}, // usp: lookat01
		{17,8,34,30.000000f,1,false}, // glock18: glock_draw1
		{17,10,13,30.000000f,3,false}, // glock18: add_silencer
		{17,11,34,30.000000f,1,false}, // glock18: glock_draw2
		{17,13,194,30.000000f,2,false}, // glock18: lookat01
		{18,5,31,24.000000f,1,false}, // awp: awp_draw
		{18,6,151,30.000000f,2,false}, // awp: lookat01
		{19,2,31,30.000000f,1,false}, // mp7: draw
		{19,6,201,30.000000f,2,false}, // mp7: lookat01
		{20,4,34,30.000000f,1,false}, // m249: draw
		{20,5,207,30.000000f,2,false}, // m249: lookat01
		{21,6,31,30.000000f,1,false}, // m3: draw
		{21,7,169,30.000000f,2,false}, // m3: lookat01
		{22,5,35,30.000000f,1,false}, // m4a1: draw
		{22,6,146,30.000000f,3,false}, // m4a1: add_silencer
		{22,12,36,30.000000f,1,false}, // m4a1: draw_unsil
		{22,13,146,30.000000f,3,false}, // m4a1: detach_silencer
		{22,14,160,30.000000f,2,false}, // m4a1: silencer_masked_inspect
		{22,15,161,30.000000f,2,false}, // m4a1: inspect
		{23,2,37,30.000000f,1,false}, // tmp: draw
		{23,6,221,30.000000f,2,false}, // tmp: lookat01
		{24,4,31,25.000000f,1,false}, // g3sg1: draw
		{24,5,133,30.000000f,2,false}, // g3sg1: lookat01_masked_bolt
		{25,3,31,30.000000f,1,false}, // flashbang: deploy
		{26,5,31,30.000000f,1,false}, // deagle: draw
		{26,6,171,30.000000f,2,false}, // deagle: lookat01
		{26,7,256,30.000000f,2,false}, // deagle: lookat02
		{27,2,31,30.000000f,1,false}, // sg552: draw
		{27,6,131,30.000000f,2,false}, // sg552: lookat01
		{27,7,131,30.000000f,2,false}, // sg552: lookat01
		{28,2,31,30.000000f,1,false}, // ak47: ak47_draw
		{28,6,138,30.000000f,2,false}, // ak47: lookat01
		{29,3,31,30.000000f,1,false}, // knife: draw
		{29,8,187,30.000000f,2,false}, // knife: inspect_1
		{30,2,31,30.000000f,1,false}, // p90: draw
		{30,6,161,30.000000f,2,false}, // p90: lookat01
		{31,3,26,30.000000f,1,false}, // molotov: draw
		{31,3,31,30.000000f,1,true}, // incendiary: deploy
	};
	for (unsigned i=0; i<sizeof(sequences)/sizeof(sequences[0]); ++i)
		if (sequences[i].weapon == weapon && sequences[i].animation == animation &&
			sequences[i].incendiary == (weapon == 31 && incendiary)) return &sequences[i];
	return 0;
}
inline int FirstFrame(int weapon, int animation, bool incendiary = false)
{
	// Converted-resource pre-roll, proved by all-bone pose comparison.
	// M4 unsilenced frame 0 is an isolated foreign pose before frames 1..5.
	// Evidence: tests/weapon_flow/draw_prefix_evidence.json. MDLs stay intact.
	if (weapon == 17 && (animation == 8 || animation == 11)) return 3;
	if (weapon == 20 && animation == 4) return 3;
	if (weapon == 22 && animation == 5) return 4;
	if (weapon == 22 && animation == 12) return 5;
	if (weapon == 31 && animation == 3 && !incendiary) return 1;
	return 0;
}
inline float Duration(int weapon, int animation, bool incendiary, bool shield, float fallback)
{
	const Sequence *s = shield ? 0 : Find(weapon,animation,incendiary);
	if (!s) return fallback > 0 && fallback < 30 ? fallback : .75f;
	return static_cast<float>(ceil((s->frames-1-FirstFrame(weapon,animation,incendiary))*1000.0/s->fps)/1000.0);
}
inline int InspectSequence(int weapon, int state, int knifeDefinition = 0, int choice = 0)
{
    // CSGO selects an activity, not the first matching sequence label. The
    // converted MDLs have no activity weights; each distinct complete inspect
    // therefore gets one ticket. Callers use the command's shared random seed.
    if (weapon == 29)
    {
        const KnifeSequences *s = FindKnife(knifeDefinition);
        return s && choice >= 0 && choice < 3 && s->inspectFrames[choice] ? 8+choice : -1;
    }
    if (weapon == 26) return choice >= 0 && choice < 2 ? 6+choice : -1;
    if (choice != 0) return -1;
	// Verified silencer bone visibility; masked labels alone are ambiguous.
	if (weapon == 22) return state & 4 ? 14 : 15;
	if (weapon == 16) return state & 1 ? 16 : 17;
	// SG553 6/7 have identical complete bone tracks. The short AUG fidget
	// and SG553 shoot_fidget do not add another full inspect activity.
	for (int animation=0; animation<32; ++animation)
	{
		const Sequence *s = Find(weapon,animation);
		if (s && s->kind == Inspect) return animation;
	}
	return -1; // Do not fabricate an inspect for a model without one.
}
inline int InspectCount(int weapon, int state, int knifeDefinition = 0)
{
    if (weapon == 29)
    {
        const KnifeSequences *s = FindKnife(knifeDefinition);
        if (!s) return 0;
        return s->inspectFrames[2] ? 3 : s->inspectFrames[1] ? 2 : 1;
    }
    if (weapon == 26) return 2;
    return InspectSequence(weapon,state,knifeDefinition) >= 0 ? 1 : 0;
}
inline bool IsInspect(int weapon, int animation, int knifeDefinition = 0)
{
    if (weapon == 29)
    {
        const KnifeSequences *s = FindKnife(knifeDefinition);
        return s && animation >= 8 && animation <= 10 && s->inspectFrames[animation-8] != 0;
    }
	const Sequence *s = Find(weapon,animation);
	return s && s->kind == Inspect;
}
inline int IdleSequence(int weapon, int state, int clip)
{
	if (weapon == 22 && !(state & 4)) return 7;
	if (weapon == 16 && !(state & 1)) return 8;
	if (weapon == 10 && clip == 1) return 1;
	return 0;
}
}
#endif
