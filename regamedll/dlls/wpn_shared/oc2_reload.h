#ifndef OC2_RELOAD_H
#define OC2_RELOAD_H

#include <math.h>

// Mechanism: local CS:GO weapon_csbase.cpp:939-947,2852-2868 and
// basecombatweapon_shared.cpp:2512-2567. Ammo completion is separate from the
// sequence-end attack lock. The table contains GoldSrc resource timing only.
// Runtime v_*.mdl sequence descriptors/events are recorded and independently
// checked by tools/equipment_followup/inspect_reload.py. Event 5003 is a visual
// resource marker, not an imported APK event handler. No APK gameplay code.
namespace oc2_reload
{
struct Sequence
{
	int weapon, animation, frames, insertFrame;
	float fps;
	bool shield;
};

inline const Sequence *Find(int weapon, int animation, bool shield = false)
{
	static const Sequence sequences[] = {
		{28,1,74,33,30,false}, // AK47: sound names clipin/clipout are reversed; use verified late marker.
		{8,1,114,63,30,false}, // AUG: after magazine seating/slap.
		{18,4,111,63,40,false}, // AWP
		{26,4,67,22,30,false}, // Deagle
		{10,14,114,87,30,false}, // Elites: both magazines, after the right magazine.
		{15,1,90,60,27,false}, // FAMAS
		{11,4,69,29,30,false}, // Five-SeveN
		{24,3,141,76,35,false}, // G3SG1
		{14,1,92,35,30,false}, // Galil
		{17,7,69,28,30,false}, {17,12,69,28,30,false}, // Glock variants
		{20,3,172,114,35,false}, // M249: belt/cover seated, before charging-handle tail.
		{22,4,93,38,30,false}, {22,11,94,38,30,false}, // M4 silenced/unsilenced
		{7,1,78,33,30,false}, // MAC10
		{19,1,95,44,30,false}, // MP7
		{1,5,69,27,30,false}, // P250
		{30,1,102,61,30,false}, // P90: after seating/slap
		{3,3,112,62,40,false}, // Scout/SSG08 resource
		{13,3,93,41,30,false}, // SG550/SCAR20 resource
		{27,1,84,32,30,false}, // SG553
		{23,1,65,27,30,false}, // MP9
		{12,1,104,44,30,false}, // UMP45
		{16,5,66,28,30,false}, {16,13,66,21,30,false}, // USP variants
		{26,4,97,41,30,true}, {11,4,97,41,30,true}, // Legacy shield resource compatibility.
		{17,4,97,41,30,true}, {16,4,97,41,30,true},
		{21,5,12,-1,30,false}, {21,3,14,10,30,false}, {21,4,25,-1,30,false}, // Nova start/insert/finish
		{5,5,21,-1,30,false}, {5,3,13,8,20,false}, {5,4,13,-1,30,false} // XM1014
	};
	for (unsigned i=0; i<sizeof(sequences)/sizeof(sequences[0]); ++i)
		if (sequences[i].weapon == weapon && sequences[i].animation == animation && sequences[i].shield == shield)
			return &sequences[i];
	return 0;
}

inline bool Shotgun(int id) { return id == 21 || id == 5; }
inline bool Finite(float value) { return value - value == 0; }
inline int Milliseconds(float seconds)
{
	return Finite(seconds) && seconds >= 0 && seconds <= 60 ?
		static_cast<int>(floor(static_cast<double>(seconds)*1000.0+.5)) : 0;
}
inline float Duration(int weapon, int animation, bool shield, float fallback)
{
	const Sequence *sequence = Find(weapon,animation,shield);
	if (!sequence) return Finite(fallback) && fallback > 0 ? fallback : 0;
	// Round up so the firing lock never ends before the last model frame.
	return static_cast<float>(ceil((sequence->frames-1)*1000.0/sequence->fps)/1000.0);
}
inline float PendingTail(int weapon, int animation, bool shield, float duration)
{
	const Sequence *sequence = Find(weapon,animation,shield);
	const int end = Milliseconds(duration);
	int insert = sequence && sequence->insertFrame >= 0 ?
		static_cast<int>(ceil(sequence->insertFrame*1000.0/sequence->fps)) : end;
	if (insert > end) insert = end;
	// m_flNextReload travels through DT_FLOAT 22 signed / multiplier 1000.
	// .25ms bias avoids Xash's double-product truncation dropping a millisecond;
	// decode to nearest millisecond on both sides before threshold comparison.
	return static_cast<float>((end-insert+.25)/1000.0);
}

struct Completion { bool event; int rounds; };
inline Completion Advance(float remaining, float &pendingTail, int &clip, int &reserve,
	int maximum, int perEvent, bool consumeReserve = true)
{
	Completion result = {false,0};
	if (!Finite(remaining) || !Finite(pendingTail) || pendingTail < 0 ||
		Milliseconds(remaining > 0 ? remaining : 0) > Milliseconds(pendingTail)) return result;
	result.event = true;
	pendingTail = -1; // Transported latch: replay/next frame cannot add again.
	if (clip < 0 || maximum <= clip || reserve <= 0) return result;
	int count = maximum-clip;
	if (count > reserve) count = reserve;
	if (perEvent > 0 && count > perEvent) count = perEvent;
	clip += count;
	if (consumeReserve) reserve -= count;
	result.rounds = count;
	return result;
}
}
#endif
