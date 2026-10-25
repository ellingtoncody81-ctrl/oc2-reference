/*
view_bob.h - CS:GO view bob.

Transcribed from CS:GO's own source, not from a re-implementation:

  D:\RLRLJYRSBJQRS\cstrike19_src\src\game\shared\cstrike15\weapon_csbase.cpp
    3295-3306  the cvars and their stock values
    3311-3500  CalcViewModelBobHelper()
    3505-3529  AddViewModelBobHelper()

The waveform follows the original. GoldSrc adapters smooth velocity steps,
keep the stride phase rate limited by the user's follow setting, and let
amplitude react to the smoothed velocity. These are separate from the Source
waveform constants.

Pure math: no engine types, no globals, no cvars. The caller supplies the
weapon profile and the accuracy numbers, so this file can be checked offline
against a transcription of the source.

See md/15-hud-module.md.
*/

#pragma once
#ifndef VIEW_BOB_H
#define VIEW_BOB_H

namespace view_bob
{
	// GoldSrc has no separate "nothing" id: 0 is WEAPON_NONE.
	const int kNoWeaponId = 0;

	// cl_bobstyle. CS:GO only has the one bob, behind style 2.
	enum
	{
		kStyleClassic = 0,
		kStyleClassicSway = 1,
		kStyleModern = 2
	};

	// ---- cvars (weapon_csbase.cpp:3295-3306) ----

	struct BobParams
	{
		float bob_cycle;    // cl_bobcycle            default 0.98
		float bob_up;       // cl_bobup               default 0.5
		float amt_vert;     // cl_bobamt_vert         default 0.25
		float amt_lat;      // cl_bobamt_lat          default 0.4
		float lower_amt;    // cl_bob_lower_amt       default 21
		float shift_left;   // cl_viewmodel_shift_left_amt   default 1.5
		float shift_right;  // cl_viewmodel_shift_right_amt  default 0.75

		// Airborne handling. CS:GO scales the amplitude to a fifth the instant
		// the grounded flag drops. That is reproduced here, but the switch can
		// be rate limited: on a ramp crest the flag flickers, and a 5x
		// amplitude step every other frame reads as a stutter.
		float air_scale;   // airborne multiplier, CS:GO uses 0.2
		float air_speed;   // slew rate toward it, 0 = instant like CS:GO

		// Low-pass on the speed before amplitude and stride phase read it, in
		// units of 1/s.
		// CS:GO's GetLocalVelocity() is predicted and arrives once per rendered
		// frame; here it is the raw server velocity whenever the server has not
		// enabled prediction (engine/client/dll_int/cl_pmove.c:1008 copies
		// frame->clientdata.velocity straight in), so it steps at the tick
		// rate instead. The classic GoldSrc bob gets away with that because its
		// phase advances with frametime no matter what the speed does -- only
		// the amplitude breathes. This bob feeds the speed into bob_offset, so
		// a tick step kicks the phase rate directly and the stride hiccups on
		// every ramp. 0 disables.
		float speed_smooth;

		// How long the grounded flag must read false before the bob treats
		// the player as airborne. GoldSrc's onground chatters several times a
		// second while running up a grade -- measured at ~8 flips per second
		// on de_dust2 -- and every flip modulates the amplitude, so the stride
		// stutters on the ramp. CS:GO tests GetGroundEntity() == NULL, which
		// is a sticky state and never flickers like that. 0 disables.
		float ground_delay;

		// How fast the smoothed speed may advance the stride phase, in units
		// per second. CS:GO uses 640. Lower values still calm phase changes;
		// the visible amplitude follows the low-passed velocity above so a
		// preferred low phase limit does not make the weapon respond late.
		float speed_follow;
	};

	// ---- everything the helper needs that is not a cvar ----
	struct BobInput
	{
		float vx, vy;          // player local velocity; the length is the speed

		bool  onground;       // false == GetGroundEntity() == NULL
		bool  ducking;        // m_bDucking || m_bDucked
		bool  in_reload;      // m_bInReload, kills the accuracy shift
		bool  is_elite;       // WEAPON_ELITE, also kills it
		bool  sniper;         // GetWeaponType() == WEAPONTYPE_SNIPER_RIFLE
		bool  zoomed;         // skips the run lowering while scoped
		float run_bob_scale;  // GetRunBobScale(), 1.0 on the base class

		// GetMaxSpeed() on the active weapon. <= 0 means "no weapon", which
		// is the branch that falls back to a fixed 150.
		float weapon_max_speed;
		int   weapon_id;          // kNoWeaponId when nothing is held

		// GetInaccuracyCrouch() / GetInaccuracyStand() / GetInaccuracy().
		// CS:GO reads these out of the item schema. The GoldSrc caller
		// supplies native predicted spread differences relative to standing.
		float inaccuracy_crouch;
		float inaccuracy_stand;
		float inaccuracy;
	};

	// ---- the two numbers the helper produces ----
	struct BobOutput
	{
		float vertical;   // m_flVerticalBob, already clamped to [-7, 4]
		float lateral;    // m_flLateralBob,  already clamped to [-8, 8]
	};

	// cl.time is corrected after rendering by Xash's CL_AdjustClock. Keep
	// presentation integration on one frame clock instead of mixing that
	// corrected timestamp with host.frametime. The double engine timestamp
	// identifies duplicate refdef calls without float-time quantization.
	struct RenderClock
	{
		double elapsed, last_stamp;
		int view_entity;
		bool primed;
		RenderClock() : elapsed( 0.0 ), last_stamp( 0.0 ), view_entity( -1 ), primed( false ) {}
	};

	struct RenderStep
	{
		float time, delta;
		bool reset;
	};

	RenderStep StepRenderClock( RenderClock &state, double stamp, float frame_time,
		bool active, bool paused, int view_entity );

	// ---- persistent state ----
	struct BobState
	{
		float bob_time;        // m_flBobTime
		float last_bob_time;   // m_flLastBobTime
		float last_speed;      // phase speed after the Source follow limit
		float gun_acc_pos;     // current weapon's m_flGunAccuracyPosition
		float weapon_acc_pos[32]; // OC2 weapon IDs 0..31, retained across switches
		int active_weapon_id;
		float vertical_bob, lateral_bob; // Valve keeps these when paused/unarmed
		float air_mix;         // smoothed airborne multiplier, 1 = grounded
		float speed_in;       // low-passed speed, before the CS:GO follow clamp
		float air_timer;      // how long the raw grounded flag has read false

		BobState()
			: bob_time( 0.0f ), last_bob_time( 0.0f ),
			  last_speed( 0.0f ), gun_acc_pos( 0.0f ), weapon_acc_pos{},
			  active_weapon_id( 0 ), vertical_bob( 0.0f ), lateral_bob( 0.0f ), air_mix( 1.0f ),
			  speed_in( 0.0f ), air_timer( 0.0f )
		{
		}
	};

	// The caller supplies the native CS1.6 predicted spread for this weapon.
	// This is a GoldSrc data adapter, not a replacement CS:GO item schema.

	BobOutput StepBob( BobState &state, const BobParams &params,
	                   const BobInput &input,
	                   float time, float frametime );
}

#endif // VIEW_BOB_H
