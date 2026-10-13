/*
view_bob.cpp - CS:GO style weapon bob. See view_bob.h.

Every operation here is in the order the reference uses, and on float rather
than double throughout. Reordering an expression or letting one promote to
double changes the low bits, and this is matched against the mobile build
bit for bit.
*/

#include "view_bob.h"

#include <cmath>

namespace
{
	const float kPi = static_cast<float>( 3.14159265358979323846 );

	// std::clamp is C++17 and this client still builds as C++14
	inline float Clamp( float value, float lo, float hi )
	{
		return value < lo ? lo : ( value > hi ? hi : value );
	}

	// Engine Remap, kept as its own expression so the arithmetic order stays
	// identical to the reference.
	inline float Remap( float value, float low1, float high1, float low2, float high2 )
	{
		return low2 + ( value - low1 ) * ( high2 - low2 ) / ( high1 - low1 );
	}

	// Position within the current cycle. The int cast truncates towards zero on
	// both sides, which is what the original does.
	inline float BobPhase( double time, float cycle_len )
	{
		float cycle = static_cast<float>( time - static_cast<int>( time / cycle_len ) * cycle_len ) / cycle_len;

		// Guards the degenerate cases a caller can produce: cycle_len 0 or the
		// result landing exactly on 1.0. A NaN here would never compare false
		// and would sail through the clamps.
		if ( !( cycle >= 0.0f && cycle < 1.0f ) )
			cycle = 0.0f;

		return cycle;
	}

	// NaN has to be caught here rather than clamped: a comparison against NaN is
	// always false, so it would sail through and a NaN view origin makes
	// SV_RecursiveHullCheck spin.
	inline float ClampBobFinite( float bob, float lo, float hi )
	{
		if ( !( bob >= -3.0e38f && bob <= 3.0e38f ) )
			return 0.0f;

		return Clamp( bob, lo, hi );
	}

	// Raises the leading half of the cycle to a full sine sweep, so the bob
	// spends 'bob_up' of its cycle going up and the rest coming down.
	inline float RaisedCycle( float cycle, float bob_up )
	{
		if ( cycle < bob_up )
			return kPi * cycle / bob_up;

		return kPi + kPi * ( cycle - bob_up ) / ( 1.0f - bob_up );
	}
}

namespace view_bob
{
	BobOffsets StepBob( BobState &state, const BobParams &params,
	                    float time, float speed, bool onground )
	{
		BobOffsets out{ 0.0f, 0.0f };

		// Follow the real speed instead of snapping to it. Without this the first
		// frame of a sprint lands the view model at full amplitude and it snaps
		// back over the next few frames.
		float max_speed_delta = ( time - state.last_bob_time ) * kSpeedAccel;
		if ( max_speed_delta < 0.0f )
			max_speed_delta = 0.0f;

		speed = Clamp( speed, state.last_speed - max_speed_delta, state.last_speed + max_speed_delta );
		speed = Clamp( speed, -320.0f, 320.0f );

		state.last_speed = speed;

		float lower_amt = params.lower_amt * ( speed * 0.001f );

		// Faster movement advances the phase faster, so the stride quickens with
		// speed instead of just getting taller.
		float bob_offset = Remap( speed, 0.0f, 320.0f, 0.0f, 1.0f );

		state.bob_time += ( time - state.last_bob_time ) * bob_offset;
		state.last_bob_time = time;

		float bob_cycle = ( ( ( 1000.0f - 150.0f ) / 3.5f ) * 0.001f ) * params.bob_cycle * 1.25f;

		if ( bob_cycle <= 0.0f )
			return out;

		float cycle = RaisedCycle( BobPhase( state.bob_time, bob_cycle ), params.bob_up );

		// Five times smaller in the air, so a jump keeps the weapon readable
		// instead of flinging it.
		float bob_scale = onground ? 0.00625f : 0.00125f;

		float vert = speed * ( bob_scale * params.amt_vert );
		vert = vert * 0.3f + vert * 0.7f * sinf( cycle );
		vert = ClampBobFinite( vert - lower_amt, -8.0f, 4.0f );

		// Lateral runs at half the vertical rate. The original writes this as
		// (int)(t / c * 2) * c * 2, which truncates half-cycles -- it is NOT the
		// same as BobPhase(t, c * 2) and writing it the obvious way changes the
		// result.
		cycle = state.bob_time - (int)( state.bob_time / bob_cycle * 2 ) * bob_cycle * 2;
		cycle /= bob_cycle * 2;
		cycle = RaisedCycle( cycle, params.bob_up );

		float hor = speed * ( bob_scale * params.amt_lat );
		hor = hor * 0.3f + hor * 0.7f * sinf( cycle );
		hor = ClampBobFinite( hor, -7.0f, 4.0f );

		out.vert = vert;
		out.hor = hor;
		return out;
	}

	Placement PlaceBob( const BobOffsets &offsets )
	{
		Placement placed;

		placed.forward = offsets.vert * 0.4f;
		placed.up = offsets.vert * 0.1f;
		placed.side = offsets.hor * 0.2f;

		return placed;
	}
}
