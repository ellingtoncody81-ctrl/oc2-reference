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
		&& std::isfinite( in.normal_x ) && std::isfinite( in.normal_y ) && std::isfinite( in.normal_z )
		&& std::isfinite( in.support_z ) && std::isfinite( in.step_size ) && in.step_size > 0.0f;
}
void Save( State &s, const Input &in )
{
	s.time = in.time; s.x = in.x; s.y = in.y; s.z = in.z; s.view_height = in.view_height;
	s.support_z = in.support_z; s.normal_x = in.normal_x; s.normal_y = in.normal_y; s.normal_z = in.normal_z;
	s.support_entity = in.support_entity;
}
}

SeamProbe DescribeSeam( const State &s, const Input &in )
{
	SeamProbe seam;
	if( !Valid(in) || !s.primed || !s.tracking || !in.enabled || !in.static_support || in.jumping || in.paused
		|| in.time<=s.time || in.time-s.time>1 || std::fabs(in.x-s.x)>64 || std::fabs(in.y-s.y)>64
		|| (!in.grounded && in.vertical_speed>0) || in.view_height!=s.view_height
		|| in.support_entity!=s.support_entity || in.normal_z<.7f || in.normal_z>1.0001f || s.normal_z<.7f
		|| in.z-in.support_z>Clamp(in.step_size,0,18)+.125f || in.support_z-in.z>2.125f ) return seam;
	if( in.normal_z>=.9999f && s.normal_z>=.9999f ) return seam;
	if( std::fabs(in.normal_x-s.normal_x)<.0005f && std::fabs(in.normal_y-s.normal_y)<.0005f
		&& std::fabs(in.normal_z-s.normal_z)<.0005f ) return seam;
	const float dx=in.x-s.x,dy=in.y-s.y;
	seam.previous_delta=-(s.normal_x*dx+s.normal_y*dy)/s.normal_z;
	seam.current_delta=-(in.normal_x*dx+in.normal_y*dy)/in.normal_z;
	const float denominator=seam.previous_delta-seam.current_delta;
	if( std::fabs(denominator)<=.0001f ) return seam;
	seam.crossing=(in.support_z-s.support_z-seam.current_delta)/denominator;
	seam.required=seam.crossing>=0 && seam.crossing<=1;
	return seam;
}

float SeamHeight( const State &s, const Input &in, const SeamProbe &seam, float fraction )
{
	return fraction<=seam.crossing ? s.support_z+fraction*seam.previous_delta :
		in.support_z-(1-fraction)*seam.current_delta;
}

bool MatchSeamSample( const State &s, const Input &in, const SeamProbe &seam,
	float fraction, float z, float nx, float ny, float nz, int entity )
{
	if( !seam.required || !std::isfinite(fraction) || !std::isfinite(z) || !std::isfinite(nx)
		|| !std::isfinite(ny) || !std::isfinite(nz) || fraction<0 || fraction>1 || entity!=in.support_entity
		|| std::fabs(z-SeamHeight(s,in,seam,fraction))>.125f ) return false;
	const bool previous=std::fabs(nx-s.normal_x)<.0005f && std::fabs(ny-s.normal_y)<.0005f && std::fabs(nz-s.normal_z)<.0005f;
	const bool current=std::fabs(nx-in.normal_x)<.0005f && std::fabs(ny-in.normal_y)<.0005f && std::fabs(nz-in.normal_z)<.0005f;
	const float dx=in.x-s.x,dy=in.y-s.y;
	// Hull tracing uses a normal-dependent separation epsilon at a real join.
	// Accept either adjoining normal only within a quarter unit of that join.
	if( std::fabs(fraction-seam.crossing)*std::sqrt(dx*dx+dy*dy)<=.25f ) return previous||current;
	return fraction<seam.crossing ? previous : current;
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
	const bool was_tracking = s.tracking;
	s.tracking = standing_up || ( support && ( in.grounded || s.tracking ) );
	Output out;
	out.supported = s.tracking;
	out.target_z = standing_up ? in.z : ( s.tracking ? in.support_z : in.z );
	// A ramp is not a sequence of landings. The same support plane can be
	// continued directly; different planes also require real in-between hull
	// probes. Intersecting infinite planes alone cannot prove connected faces.
	if( s.tracking && was_tracking && !standing_up && in.view_height == s.view_height
		&& in.normal_z >= 0.7f && in.normal_z <= 1.0001f && s.normal_z >= 0.7f
		&& in.support_entity == s.support_entity )
	{
		const float dx=in.x-s.x,dy=in.y-s.y;
		const float previous_delta=-(s.normal_x*dx+s.normal_y*dy)/s.normal_z;
		const float current_delta=-(in.normal_x*dx+in.normal_y*dy)/in.normal_z;
		const float height_delta=in.support_z-s.support_z;
		const bool inclined=in.normal_z<0.9999f || s.normal_z<0.9999f;
		const bool same_normal=std::fabs(in.normal_x-s.normal_x)<0.0005f
			&& std::fabs(in.normal_y-s.normal_y)<0.0005f && std::fabs(in.normal_z-s.normal_z)<0.0005f;
		if( same_normal && std::fabs(height_delta-previous_delta)<=0.125f )
		{
			// Retain visual contact at a ramp's flat run-out only until the
			// physical hull catches up; a true jump already disarmed tracking.
			out.continuous_slope=inclined || (s.output.continuous_slope && !in.grounded);
		}
		else if( inclined && std::fabs(previous_delta-current_delta)>0.0001f )
		{
			out.continuous_slope=in.seam_verified && DescribeSeam(s,in).required;
		}
		if( out.continuous_slope ) out.plane_delta=height_delta;
	}
	if( s.tracking )
	{
		// baseplayer_shared.cpp SmoothViewOnStairs: BOTH directions, 150 u/s,
		// at most 18 units behind the supported player position.
		const float dt = in.time - s.time;
		// Carry continuous plane travel directly; only a remaining discrete
		// height change needs the Source 150 u/s stair approach. This preserves
		// the existing flat-tread behavior even at a sloped-to-flat boundary.
		s.smooth_z += out.plane_delta;
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
