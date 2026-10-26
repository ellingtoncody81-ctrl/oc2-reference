/*
view_bob.cpp - CS:GO view bob. See view_bob.h.

The waveform and constants follow CalcViewModelBobHelper() in
weapon_csbase.cpp. GoldSrc additionally smooths tick-sized velocity steps;
the Source follow limit controls phase while that smoothed speed controls
amplitude. Where CS:GO has a no-op (the clamp whose result is thrown away) it
is left as a no-op and marked, rather than "fixed".
*/

#include "view_bob.h"

#include <cmath>

namespace
{
	const double kPi = 3.14159265358979323846;

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
			return static_cast<float>( kPi * cycle / bob_up );

		return static_cast<float>( kPi + kPi * ( cycle - bob_up ) / ( 1.0 - bob_up ) );
	}

	// amplitude * 0.3 + amplitude * 0.7 * sin( cycle )
	inline float Blend( float amplitude, float sine )
	{
		return static_cast<float>( amplitude * 0.3 + amplitude * 0.7 * sine );
	}
}

namespace view_bob
{
	RenderStep StepRenderClock( RenderClock &state, double stamp, float frame_time,
		bool active, bool paused, int view_entity )
	{
		RenderStep out{ static_cast<float>( state.elapsed ), 0.0f, false };
		if( !active || !std::isfinite( stamp ) || !std::isfinite( frame_time ) || frame_time < 0.0f )
		{
			state = RenderClock();
			return RenderStep{ 0.0f, 0.0f, true };
		}
		// A map/demo seek, camera change or a real hitch still invalidates
		// history. A <=250ms clock correction must not restart a walking stride.
		if( state.primed && ( view_entity != state.view_entity ||
			( !paused && ( stamp < state.last_stamp - 0.25 || stamp > state.last_stamp + 1.0 || frame_time > 0.25f ) ) ) )
		{
			state = RenderClock();
			out.reset = true;
			out.time = 0.0f;
		}
		if( state.primed && stamp == state.last_stamp ) return out;
		state.primed = true;
		state.view_entity = view_entity;
		state.last_stamp = stamp;
		if( paused || frame_time > 0.25f ) return out;
		const float previous = static_cast<float>( state.elapsed );
		state.elapsed += frame_time;
		out.time = static_cast<float>( state.elapsed );
		out.delta = out.time - previous;
		return out;
	}

	BobOutput StepBob( BobState &state, const BobParams &params,
	                   const BobInput &input,
	                   float time, float frametime )
	{
		BobOutput out{ state.vertical_bob, state.lateral_bob };

		// "if ( !gpGlobals->frametime || !player ) return 0;" -- the comment
		// in the source says the return value is unused, the old values stay.
		if( !std::isfinite( frametime ) || frametime <= 0.0f || !std::isfinite( time ) )
			return out;

		// User cvars may be edited from the console. Reject a singular cycle
		// before division or conversion of its phase to an integer.
		if( !std::isfinite( params.bob_cycle ) || params.bob_cycle <= 0.0f
			|| !std::isfinite( params.bob_up ) || params.bob_up <= 0.0f || params.bob_up >= 1.0f
			|| !std::isfinite( input.vx ) || !std::isfinite( input.vy )
			|| !std::isfinite( input.weapon_max_speed ) || input.weapon_max_speed >= 1000.0f )
			return out;

		// Find the speed of the player.
		float speed = std::sqrt( input.vx * input.vx + input.vy * input.vy );
		const float raw_speed = speed;

		// The amplitude adapter smooths server-tick velocity steps. Keep its
		// history separate from Source's rate-limited stride speed below.
		if( params.speed_smooth > 0.0f )
		{
			float f = 1.0f - std::exp( -params.speed_smooth * frametime );

			if( f < 0.0f ) f = 0.0f;
			if( f > 1.0f ) f = 1.0f;

			state.speed_in = state.speed_in + ( speed - state.speed_in ) * f;
			speed = state.speed_in;
		}
		else
		{
			state.speed_in = speed;
		}
		// Smooth tick-sized amplitude steps separately. Cascading this low-pass
		// into Source's phase follow limit makes the first strides drag behind
		// an already moving player, especially during short strafes.
		const float amplitude_speed = Clamp( speed, 0.0f, 320.0f );
		speed = raw_speed;

		// don't allow too big speed changes
		float max_speed_delta = std::fmax( 0.0f, ( time - state.last_bob_time ) * params.speed_follow );
		speed = Clamp( speed, state.last_speed - max_speed_delta, state.last_speed + max_speed_delta );
		speed = Clamp( speed, -320.0f, 320.0f );

		state.last_speed = speed;

		// Preserve the Source follow-limited run lowering independently of bob amplitude.
		float run_add_amt = 0.0f;
		if( !input.zoomed )
		{
			float speed_factor = speed * 0.006f;
			(void)Clamp( speed_factor, 0.0f, 0.5f );
			float lower_amt = static_cast<float>( params.lower_amt * 0.2 );
			run_add_amt = lower_amt * speed_factor;
		}

		float bob_offset = BobOffset( speed );
		const float phase_step = ( time - state.last_bob_time ) * bob_offset;
		state.bob_time += phase_step;
		state.last_bob_time = time;

		float bob_cycle = 0.5f;
		float accuracy_diff = 0.0f;
		float gun_acc_pos = 0.0f;

		// Whether a weapon is held, and the cycle length.
		//
		// These must not be the same test. The client's predicted weapon only
		// knows its max speed for some classes -- a Glock reports 0 until you
		// switch weapons and the client rebuilds it -- and gating the lateral
		// bob on that value silently disables left/right sway for those guns.
		// CS:GO cannot hit this because GetMaxSpeed() comes out of the item
		// schema and is always populated.
		bool have_weapon = input.weapon_max_speed > 0.0f
			|| ( input.weapon_id != 0 && input.weapon_id != kNoWeaponId );

		// A weapon we have no speed for falls back to CS:GO's own no-weapon
		// cycle, which is the fixed 150 branch below.
		float weapon_max_speed = input.weapon_max_speed > 0.0f ? input.weapon_max_speed : 150.0f;

		if( have_weapon )
		{
			int weapon_id = input.weapon_id > 0 && input.weapon_id < 32 ? input.weapon_id : 0;
			if( weapon_id != state.active_weapon_id )
			{
				state.weapon_acc_pos[state.active_weapon_id] = state.gun_acc_pos;
				state.active_weapon_id = weapon_id;
				state.gun_acc_pos = state.weapon_acc_pos[weapon_id];
			}
			run_add_amt *= input.run_bob_scale;

			bob_cycle = ( ( ( 1000.0f - weapon_max_speed ) / 3.5f ) * 0.001f ) * params.bob_cycle;

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

		// A weapon changes cycle length, not the foot's position in a stride.
		// Source recomputes bob_time / cycle directly; that can jump at a switch
		// after a long run. Preserve phase while adopting the new cadence.
		if( state.last_cycle > 0.0f && bob_cycle != state.last_cycle )
		{
			state.bob_time = ( state.bob_time - phase_step ) * ( bob_cycle / state.last_cycle ) + phase_step;
		}
		state.last_cycle = bob_cycle;

		// ---- vertical ----
		float cycle = state.bob_time - static_cast<int>( state.bob_time / bob_cycle ) * bob_cycle;
		cycle /= bob_cycle;
		cycle = RaisedCycle( cycle, params.bob_up );

		// if we're in the air, slow our bob down a bit
		//
		// CS:GO flips between 0.00625 and 0.00125 in a single frame. On a ramp
		// crest the grounded flag flickers, so the amplitude steps by 5x every
		// other frame -- on its own small enough not to notice, but the swing
		// is 5x bigger on a GoldSrc view model than on CS:GO's, so it reads as
		// a stutter. air_speed rate limits the change; 0 gives the original.
		//
		// ground_delay comes first. The flag is not "flickering at a crest",
		// it chatters ~8 times a second for the whole ramp, so the rate limit
		// alone is not enough -- the amplitude gets dragged through its full
		// range several times a second. CS:GO tests GetGroundEntity() == NULL,
		// a sticky state that never behaves like that; this is the equivalent.
		float air_target = 1.0f;

		if( input.onground )
		{
			state.air_timer = 0.0f;
		}
		else
		{
			state.air_timer += frametime;

			if( params.ground_delay <= 0.0f || state.air_timer >= params.ground_delay )
				air_target = params.air_scale;
		}

		if( params.air_speed > 0.0f )
		{
			float step = params.air_speed * frametime;

			if( state.air_mix < air_target )
				state.air_mix = air_target < state.air_mix + step ? air_target : state.air_mix + step;
			else
				state.air_mix = air_target > state.air_mix - step ? air_target : state.air_mix - step;
		}
		else
		{
			state.air_mix = air_target;
		}

		float bob_multiplier = 0.00625f * state.air_mix;

		float vertical = amplitude_speed * ( bob_multiplier * params.amt_vert );
		vertical = Blend( vertical, std::sin( cycle ) );
		out.vertical = ClampFinite( static_cast<float>( vertical - ( run_add_amt - ( gun_acc_pos * 0.2 ) ) ), -7.0f, 4.0f );

		// ---- lateral ----
		// CS:GO quirk kept verbatim: (int)( t / c * 2 ) truncates half-cycles,
		// it does not compute t / (2c). Writing the obvious way gives a
		// different waveform near the cycle boundary.
		if( have_weapon )
		{
			cycle = state.bob_time - static_cast<int>( state.bob_time / bob_cycle * 2 ) * bob_cycle * 2;
			cycle /= bob_cycle * 2;
			cycle = RaisedCycle( cycle, params.bob_up );

			float lateral = amplitude_speed * ( bob_multiplier * params.amt_lat );
			lateral = Blend( lateral, std::sin( cycle ) );
			out.lateral = ClampFinite( static_cast<float>( lateral + gun_acc_pos * 0.25 ), -8.0f, 8.0f );
		}

		state.vertical_bob = out.vertical;
		state.lateral_bob = out.lateral;
		return out;
	}
}
