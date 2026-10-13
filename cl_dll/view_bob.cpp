/*
view_bob.cpp - CS:GO view bob. See view_bob.h.

Statement order and every constant follow CalcViewModelBobHelper() in
weapon_csbase.cpp. Where CS:GO has a no-op (the clamp whose result is thrown
away) it is left as a no-op and marked, rather than "fixed".
*/

#include "view_bob.h"

#include <cmath>

namespace
{
	const float kPi = 3.14159265358979323846f;

	// Source: UTIL_SharedRandomFloat's helper in mathlib, and the identical
	// pattern appears in every CS:GO clamp macro. std::clamp is C++17 and
	// this client builds as C++14.
	inline float Clamp( float v, float lo, float hi )
	{
		return v < lo ? lo : ( v > hi ? hi : v );
	}

	// CS:GO uses plain clamp() here, so NaN would sail through. Guard it
	// anyway: a NaN view origin makes SV_RecursiveHullCheck spin forever.
	inline float ClampFinite( float v, float lo, float hi )
	{
		if( !( v >= -3.0e38f && v <= 3.0e38f ) )
			return 0.0f;

		return Clamp( v, lo, hi );
	}

	// RemapVal( speed, 0, 320, 0, 1 )
	inline float BobOffset( float speed )
	{
		return Clamp( ( speed - 0.0f ) / ( 320.0f - 0.0f ), 0.0f, 1.0f );
	}

	// Approach( target, value, maxMove )
	inline float Approach( float target, float value, float max_move )
	{
		float delta = target - value;

		if( delta > max_move )
			return value + max_move;

		if( delta < -max_move )
			return value - max_move;

		return target;
	}

	// The rising half of the cycle is compressed into bob_up, the rest is the
	// way back down. Both axes run this.
	inline float RaisedCycle( float cycle, float bob_up )
	{
		if( cycle < bob_up )
			return kPi * cycle / bob_up;

		return kPi + kPi * ( cycle - bob_up ) / ( 1.0f - bob_up );
	}

	// amplitude * 0.3 + amplitude * 0.7 * sin( cycle )
	inline float Blend( float amplitude, float sine )
	{
		return amplitude * 0.3f + amplitude * 0.7f * sine;
	}
}

namespace view_bob
{
	const float kInaccuracyStand = 0.0008f;
	const float kInaccuracyCrouch = 0.0005f;

	BobOutput StepBob( BobState &state, const BobParams &params,
	                   const BobInput &input,
	                   float time, float frametime )
	{
		BobOutput out{ 0.0f, 0.0f };

		// "if ( !gpGlobals->frametime || !player ) return 0;" -- the comment
		// in the source says the return value is unused, the old values stay.
		if( frametime == 0.0f )
			return out;

		// Find the speed of the player.
		float speed = std::sqrt( input.vx * input.vx + input.vy * input.vy );

		// do the dip before speed gets clamped
		float run_add_amt = 0.0f;

		// when the player is moving forward, the gun lowers a bit
		if( !input.zoomed )
		{
			float speed_factor = speed * 0.006f;

			// CS:GO calls clamp( speed_factor, 0, 0.5 ) and throws the result
			// away. Reproduced as written.
			(void)Clamp( speed_factor, 0.0f, 0.5f );

			float lower_amt = params.lower_amt * 0.2f;
			run_add_amt = lower_amt * speed_factor;
		}

		// don't allow too big speed changes
		float max_speed_delta = std::fmax( 0.0f, ( time - state.last_bob_time ) * 640.0f );
		speed = Clamp( speed, state.last_speed - max_speed_delta, state.last_speed + max_speed_delta );
		speed = Clamp( speed, -320.0f, 320.0f );

		state.last_speed = speed;

		float bob_offset = BobOffset( speed );
		state.bob_time += ( time - state.last_bob_time ) * bob_offset;
		state.last_bob_time = time;

		float bob_cycle = 0.5f;
		float accuracy_diff = 0.0f;
		float gun_acc_pos = 0.0f;

		bool have_weapon = input.weapon_max_speed > 0.0f;

		if( have_weapon )
		{
			run_add_amt *= input.run_bob_scale;

			bob_cycle = ( ( ( 1000.0f - input.weapon_max_speed ) / 3.5f ) * 0.001f ) * params.bob_cycle;

			// move the gun left or right based on the player's current accuracy
			if( !input.in_reload && !input.is_elite )
			{
				float crouch_accuracy = input.inaccuracy_crouch;
				float base_accuracy = input.inaccuracy_stand;

				float accuracy = input.ducking ? crouch_accuracy : input.inaccuracy;

				float multiplier = 1.0f;

				if( accuracy < base_accuracy )
				{
					multiplier = input.sniper ? 0.15f : 18.0f;
					multiplier *= params.shift_left;
				}
				else
				{
					accuracy = std::fmin( accuracy, 0.082f );
					multiplier *= params.shift_right;
				}

				accuracy_diff = std::fmax( ( accuracy - base_accuracy ) * multiplier, -0.1f );
			}

			float target = accuracy_diff * 80.0f;
			state.gun_acc_pos = Approach( target, state.gun_acc_pos,
				std::fabs( ( target - state.gun_acc_pos ) * frametime ) * 4.0f );

			gun_acc_pos = state.gun_acc_pos;
		}
		else
		{
			bob_cycle = ( ( ( 1000.0f - 150.0f ) / 3.5f ) * 0.001f ) * params.bob_cycle;
		}

		// ---- vertical ----
		float cycle = state.bob_time - static_cast<int>( state.bob_time / bob_cycle ) * bob_cycle;
		cycle /= bob_cycle;
		cycle = RaisedCycle( cycle, params.bob_up );

		// if we're in the air, slow our bob down a bit
		float bob_multiplier = input.onground ? 0.00625f : 0.00125f;

		float vertical = speed * ( bob_multiplier * params.amt_vert );
		vertical = Blend( vertical, std::sin( cycle ) );
		out.vertical = ClampFinite( vertical - ( run_add_amt - ( gun_acc_pos * 0.2f ) ), -7.0f, 4.0f );

		// ---- lateral ----
		// CS:GO quirk kept verbatim: (int)( t / c * 2 ) truncates half-cycles,
		// it does not compute t / (2c). Writing the obvious way gives a
		// different waveform near the cycle boundary.
		if( have_weapon )
		{
			cycle = state.bob_time - static_cast<int>( state.bob_time / bob_cycle * 2 ) * bob_cycle * 2;
			cycle /= bob_cycle * 2;
			cycle = RaisedCycle( cycle, params.bob_up );

			float lateral = speed * ( bob_multiplier * params.amt_lat );
			lateral = Blend( lateral, std::sin( cycle ) );
			out.lateral = ClampFinite( lateral + gun_acc_pos * 0.25f, -8.0f, 8.0f );
		}

		return out;
	}
}