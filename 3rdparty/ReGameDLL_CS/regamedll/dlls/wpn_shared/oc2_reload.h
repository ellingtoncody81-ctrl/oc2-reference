#ifndef OC2_RELOAD_H
#define OC2_RELOAD_H

#include <math.h>

// Mechanism: local CS:GO weapon_csbase.cpp:939-947,2852-2868 and
// basecombatweapon_shared.cpp:2512-2567. Ammo completion is separate from the
// sequence-end attack lock. GoldSrc poses are retimed to local CS:GO 2019
// VPK sequence duration and AE_WPN_COMPLETE_RELOAD, not the APK animation fps.
// Runtime v_*.mdl sequence descriptors/events are recorded and independently
// checked by tools/equipment_followup/inspect_reload.py. Event 5003 is a visual
// resource marker, not an imported APK event handler. No APK gameplay code.
// Contact poses: tools/module3_finish/inspect_reload_contacts.py; markers alone
// cannot identify insertion (e.g. FAMAS 44, AUG 46, unsilenced M4 42).
namespace oc2_reload
{
struct Sequence
{
	int weapon, animation, frames, insertFrame;
	float fps;
	bool shield;
	float sourceDuration, completeTime;
};

inline const Sequence *Find(int weapon, int animation, bool shield = false)
{
	static const Sequence sequences[] = {
		{28,1,74,35,30,false,73/30.f,35/30.f}, // AK47 clip and release latch seated.
		{8,1,114,46,30,false,113/30.f,46/30.f}, // AUG: actual seated frame, not later slap 63.
		{18,4,111,60,40,false,110/30.f,60/30.f}, // AWP: Source completion after magazine slap.
		{26,4,67,26,30,false,66/30.f,26/30.f}, // Deagle
		{10,14,114,87,30,false,113/30.f,87/30.f}, // Dual Elites
		{15,1,90,44,27,false,89/27.f,44/27.f}, // FAMAS: seated before later slap.
		{11,4,69,29,30,false,68/30.f,28/30.f}, // Five-SeveN
		{24,3,141,78,35,false,140/30.f,78/30.f}, // G3SG1
		{14,1,92,35,30,false,91/30.f,35/30.f}, // Galil
		{17,7,69,28,30,false,68/30.f,28/30.f}, {17,12,69,28,30,false,68/30.f,28/30.f},
		{20,3,172,112,35,false,171/30.f,112/30.f}, // M249: cover closure.
		{22,4,93,41,30,false,92/30.f,41/30.f}, {22,11,94,42,30,false,92/30.f,41/30.f},
		{7,1,78,37,30,false,77/30.f,38/30.f}, // MAC10 seated before its Source event.
		{19,1,95,43,30,false,94/30.f,43/30.f}, // MP7
		{1,5,69,28,30,false,68/30.f,28/30.f}, // P250
		{30,1,102,59,30,false,101/30.f,59/30.f}, // P90
		{3,3,112,59,40,false,111/30.f,59/30.f}, // SSG08: Source completion after slap.
		{13,3,93,42,30,false,92/30.f,42/30.f}, // SCAR20
		{27,1,84,31,30,false,83/30.f,31/30.f}, // SG553
		{23,1,65,26,30,false,64/30.f,26/30.f}, // MP9
		{12,1,104,45,30,false,103/30.f,45/30.f}, // UMP45
		{16,5,66,26,30,false,65/30.f,29/30.f}, {16,13,66,26,30,false,65/30.f,29/30.f},
		{26,4,97,41,30,true,3.2f,41/30.f}, {11,4,97,41,30,true,3.2f,41/30.f}, // Legacy-only shield.
		{17,4,97,41,30,true,3.2f,41/30.f}, {16,4,97,41,30,true,3.2f,41/30.f},
		// CS:GO shotgun code: .5s start / .5s per shell; finish is interruptible.
		{21,5,12,-1,30,false,.5f,.5f}, {21,3,14,10,30,false,.5f,.5f}, {21,4,25,-1,30,false,.8f,.8f},
		{5,5,21,-1,30,false,.5f,.5f}, {5,3,13,8,20,false,.5f,.5f}, {5,4,13,-1,30,false,.4f,.4f}
	};
	for (unsigned i=0; i<sizeof(sequences)/sizeof(sequences[0]); ++i)
		if (sequences[i].weapon == weapon && sequences[i].animation == animation && sequences[i].shield == shield)
			return &sequences[i];
	return 0;
}

inline bool Shotgun(int id) { return id == 21 || id == 5; }
inline bool Finite(float value) { return value - value == 0; }
// Holding reload must not suppress the existing idle/shotgun finish once a
// completed reload has no further ammunition to load. This only selects the
// existing animation state machine; it does not change the reload timers.
inline bool IdleAfterHeldReload(int weapon, int animation, bool shield, int clip, int maximum,
	int reserve, float idleRemaining, bool inReload, bool attacking)
{
	return Find(weapon,animation,shield) && maximum > 0 && !inReload && !attacking &&
		Finite(idleRemaining) && idleRemaining <= 0 && (clip >= maximum || reserve <= 0);
}
inline int Milliseconds(float seconds)
{
	return Finite(seconds) && seconds >= 0 && seconds <= 60 ?
		static_cast<int>(floor(static_cast<double>(seconds)*1000.0+.5)) : 0;
}
inline float Duration(int weapon, int animation, bool shield, float fallback)
{
	const Sequence *sequence = Find(weapon,animation,shield);
	if (!sequence) return Finite(fallback) && fallback > 0 ? fallback : 0;
	return static_cast<float>(ceil(sequence->sourceDuration*1000.0-.001)/1000.0);
}
inline float PendingTail(int weapon, int animation, bool shield, float duration)
{
	const Sequence *sequence = Find(weapon,animation,shield);
	const int end = Milliseconds(duration);
	int insert = sequence ? static_cast<int>(ceil(sequence->completeTime*1000.0-.001)) : end;
	if (insert > end) insert = end;
	// m_flNextReload travels through DT_FLOAT 22 signed / multiplier 1000.
	// .25ms bias avoids Xash's double-product truncation dropping a millisecond;
	// decode to nearest millisecond on both sides before threshold comparison.
	return static_cast<float>((end-insert+.25)/1000.0);
}

// The model keeps its resource pose and sound frames. Two linear spans put
// physical magazine contact on the Source completion event, and the last pose
// on the Source attack-unlock time. Shotguns stock a shell at cycle end.
inline float ModelFrame(int weapon, int animation, bool shield, float elapsed, int frames, float fps)
{
	const Sequence *sequence=Find(weapon,animation,shield);
	if (!sequence || sequence->frames!=frames || fabs(sequence->fps-fps)>.001f || !Finite(elapsed)) return -1;
	const float duration=Duration(weapon,animation,shield,0);
	if (elapsed<=0) return 0;
	if (elapsed>=duration) return static_cast<float>(frames-1);
	const float complete=duration-Milliseconds(PendingTail(weapon,animation,shield,duration))/1000.f;
	if (sequence->insertFrame<0 || Shotgun(weapon) || complete<=0 || complete>=duration)
		return elapsed/duration*(frames-1);
	if (elapsed<complete) return elapsed/complete*sequence->insertFrame;
	return sequence->insertFrame+(elapsed-complete)/(duration-complete)*(frames-1-sequence->insertFrame);
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
