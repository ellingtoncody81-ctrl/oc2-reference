/*
view_bob.h - CS:GO view bob.

Transcribed from CS:GO's own source, not from a re-implementation:

  D:\RLRLJYRSBJQRS\cstrike19_src\src\game\shared\cstrike15\weapon_csbase.cpp
    3295-3306  the cvars and their stock values
    3311-3500  CalcViewModelBobHelper()
    3505-3529  AddViewModelBobHelper()

The bob this reproduces is the original; GoldClient and NextClient are both
later forks of it and each drifts from CS:GO in ways that are visible:
GoldClient adds an exponential speed smoother, a health>=45 gate and a 1.35
cycle multiplier, and folds the final placement scale into the output. Those
are NOT reproduced here.

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

		// GetInaccuracyCrouch() / GetInaccuracyStand() / GetInaccuracy().
		// CS:GO reads these out of the item schema; see the note on
		// kInaccuracyStand below for how we fill them here.
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

	// ---- persistent state ----
	struct BobState
	{
		float bob_time;        // m_flBobTime
		float last_bob_time;   // m_flLastBobTime
		float last_speed;      // m_flLastSpeed
		float gun_acc_pos;     // m_flGunAccuracyPosition (lives on the weapon)

		BobState()
			: bob_time( 0.0f ), last_bob_time( 0.0f ),
			  last_speed( 0.0f ), gun_acc_pos( 0.0f )
		{
		}
	};

	// stock CS:GO stand/crouch inaccuracy. CS:GO reads these per weapon out of
	// items_game.txt, which is not in the local dump -- these are AK-47's
	// values and are the only numbers here that are not lifted from source.
	// They only matter for the crouching half of the accuracy shift.
	extern const float kInaccuracyStand;
	extern const float kInaccuracyCrouch;

	BobOutput StepBob( BobState &state, const BobParams &params,
	                   const BobInput &input,
	                   float time, float frametime );
}

#endif // VIEW_BOB_H