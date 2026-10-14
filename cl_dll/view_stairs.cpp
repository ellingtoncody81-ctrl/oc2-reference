#include "view_stairs.h"
#include <cmath>

namespace view_stairs
{
namespace
{
float Clamp( float v, float lo, float hi ) { return v < lo ? lo : ( v > hi ? hi : v ); }
bool Valid( const Input &in )
{
	return std::isfinite( in.time ) && std::isfinite( in.frame_time )
		&& std::isfinite( in.x ) && std::isfinite( in.y ) && std::isfinite( in.z )
		&& std::isfinite( in.view_height ) && std::isfinite( in.vertical_speed )
		&& std::isfinite( in.support_z ) && std::isfinite( in.step_size ) && in.step_size > 0.0f;
}
void Save( State &s, const Input &in )
{
	s.time = in.time; s.x = in.x; s.y = in.y; s.z = in.z; s.view_height = in.view_height;
}
}

Output Step( State &s, const Input &in )
{
	if( !Valid( in ) ) { s = State(); return Output(); }
	const float step = Clamp( in.step_size, 0.0f, 18.0f );
	const bool support = in.enabled && in.static_support && !in.jumping
		&& ( in.grounded || in.vertical_speed <= 0.0f ) && in.z - in.support_z <= step + 0.125f
		&& in.support_z - in.z <= 2.125f;
	const bool discontinuity = s.primed && ( in.time < s.time || in.time - s.time > 1.0f
		|| std::fabs( in.x - s.x ) > 64.0f || std::fabs( in.y - s.y ) > 64.0f
		|| std::fabs( in.z - s.z ) > 64.0f );
	if( discontinuity ) s = State();
	// GoldSrc changes the hull CENTER by 18 units when standing up. Source
	// uses a feet origin, so its view-height reset cannot be copied for that
	// one transition: retain GoldSrc's existing upward-step compensation.
	const bool standing_up = s.primed && in.enabled && !in.jumping && in.grounded && s.tracking
		&& in.view_height > s.view_height && in.z > s.z && in.z - s.z <= step + 0.125f;
	if( s.primed && in.enabled && in.paused ) { s.was_paused = true; return s.output; }
	if( !s.primed || !in.enabled || s.was_paused || ( in.view_height != s.view_height && !standing_up ) )
	{
		s = State(); s.primed = true; s.smooth_z = in.z;
		s.tracking = support && in.grounded;
		s.output.target_z = in.z; s.output.supported = s.tracking;
		Save( s, in );
		if( in.paused ) s.was_paused = true;
		return s.output;
	}
	if( in.time == s.time || in.frame_time <= 0.0f ) return s.output;

	// Source maintains physical ground contact with StayOnGround. GoldSrc
	// only traces 2 units down, so it can briefly fall along a continuous
	// ramp. Follow a verified walkable surface for presentation only. Once
	// support is lost or a jump starts, require real grounding to re-arm;
	// a falling player must not get pulled down early near the floor.
	s.tracking = standing_up || ( support && ( in.grounded || s.tracking ) );
	Output out;
	out.supported = s.tracking;
	out.target_z = standing_up ? in.z : ( s.tracking ? in.support_z : in.z );
	if( s.tracking )
	{
		// baseplayer_shared.cpp SmoothViewOnStairs: BOTH directions, 150 u/s,
		// at most 18 units behind the supported player position.
		const float dt = in.time - s.time;
		const float delta = out.target_z - s.smooth_z;
		s.smooth_z += Clamp( delta, -dt * 150.0f, dt * 150.0f );
		s.smooth_z = Clamp( s.smooth_z, out.target_z - step, out.target_z + step );
		out.offset = Clamp( s.smooth_z - in.z, -step, step );
	}
	else if( in.jumping || ( !in.grounded && in.vertical_speed > 0.0f )
		|| ( in.grounded && !in.static_support ) )
	{
		// Real takeoff and moving-ground contexts keep their native eye path.
		s.smooth_z = in.z;
	}
	else
	{
		// A fast stair descent can cross a second step before GoldSrc's
		// physical hull has landed. Do not snap an existing camera correction
		// back to zero when that next tread is beyond the probe's reach.
		// Release only the old correction; the airborne latch still forbids
		// acquiring new support before real grounding.
		const float dt = in.time - s.time;
		out.offset = s.output.offset + Clamp( -s.output.offset, -150.0f * dt, 150.0f * dt );
		if( s.output.offset < 0.0f && !in.grounded && in.z <= s.z )
		{
			// Never lift the eye while the physical player is descending.
			const float caught_up = Clamp( s.output.offset + s.z - in.z, -step, 0.0f );
			if( out.offset > caught_up ) out.offset = caught_up;
		}
		s.smooth_z = in.z + out.offset;
	}
	Save( s, in ); s.output = out;
	return out;
}
}
