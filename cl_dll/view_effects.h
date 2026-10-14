/*
view_effects.h - engine independent viewmodel effects.

Sway follows predicted_viewmodel.cpp:83-125 in the local CS:GO source.
The offset convention, mouse roll and landing pulse are GoldSrc adapters;
the local CS:GO snapshot comments out its viewmodel dip application.
All persistent state is explicit so map, camera and weapon changes can reset it.
*/
#pragma once
#ifndef VIEW_EFFECTS_H
#define VIEW_EFFECTS_H

namespace view_effects
{
	struct Vec3
	{
		float x, y, z;
		Vec3( float a = 0.0f, float b = 0.0f, float c = 0.0f ) : x( a ), y( b ), z( c ) {}
		float &operator[]( int i ) { return i == 0 ? x : ( i == 1 ? y : z ); }
		const float &operator[]( int i ) const { return i == 0 ? x : ( i == 1 ? y : z ); }
	};

	struct AngleSample
	{
		float time;
		Vec3 angles; // pitch, yaw, roll in degrees
	};

	// 512 entries cover 100ms even at 1000fps. If a requested time is
	// outside retained history, do not extrapolate an unrelated angle.
	const int kSwayHistory = 512;
	struct SwayState
	{
		AngleSample history[kSwayHistory];
		int head, count;
		float last_time;
		bool has_time, was_paused;
		Vec3 last_offset;
		SwayState() : head( 0 ), count( 0 ), last_time( 0.0f ), has_time( false ), was_paused( false ) {}
	};

	struct EffectsParams
	{
		float sway_interp, sway_scale;
		float land_scale, roll_angle;
		EffectsParams() : sway_interp( 0.1f ), sway_scale( 1.6f ), land_scale( 1.0f ), roll_angle( 0.5f ) {}
	};

	struct EffectsInput
	{
		float time, frame_time;
		Vec3 view_angles;  // final viewmodel angles, for sway
		Vec3 mouse_angles; // raw client view angles, excludes bob/recoil
		float vertical_speed;
		bool grounded, active, paused, left_hand;
		int view_entity, weapon_id, model_index;
		EffectsInput() : time( 0.0f ), frame_time( 0.0f ), vertical_speed( 0.0f ),
			grounded( true ), active( true ), paused( false ), left_hand( false ),
			view_entity( 0 ), weapon_id( 0 ), model_index( 0 ) {}
	};

	struct EffectsOutput
	{
		Vec3 sway_offset;
		float land_dip;  // subtract from viewmodel world Z only
		float mouse_roll; // add to viewmodel roll only
		EffectsOutput() : land_dip( 0.0f ), mouse_roll( 0.0f ) {}
	};

	struct EffectsState
	{
		SwayState sway;
		bool primed, was_paused, last_grounded, last_left_hand;
		int view_entity, weapon_id, model_index;
		float last_time, air_time, fall_speed, landing_time, landing_amount, roll;
		Vec3 last_mouse_angles;
		EffectsOutput last_output;
		EffectsState() : primed( false ), was_paused( false ), last_grounded( true ), last_left_hand( false ),
			view_entity( 0 ), weapon_id( 0 ), model_index( 0 ), last_time( 0.0f ), air_time( 0.0f ),
			fall_speed( 0.0f ), landing_time( -1.0f ), landing_amount( 0.0f ), roll( 0.0f ) {}
	};

	void ResetSway( SwayState &state );
	void ResetEffects( EffectsState &state );
	Vec3 StepSway( SwayState &state, float time, const Vec3 &angles,
		float interp, float scale, bool left_hand, bool paused = false );
	EffectsOutput StepEffects( EffectsState &state, const EffectsParams &params, const EffectsInput &input );

	// x = right, y = forward, z = up. Positive x moves toward the hand
	// holding the weapon; left-handed models reverse only the right axis.
	Vec3 FixedOffset( const Vec3 &angles, float x, float y, float z, bool left_hand );
}
#endif
