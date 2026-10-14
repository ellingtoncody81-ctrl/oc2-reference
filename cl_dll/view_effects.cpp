#include "view_effects.h"
#include <cmath>

namespace view_effects
{
	namespace
	{
		const float kPi = 3.14159265358979323846f;
		const float kMaxSampleGap = 1.0f;
		// GoldSrc adapters: keep ground chatter and short stair falls from
		// triggering a dip. The 0.25s pulse duration follows the CS:GO source;
		// amplitude and gates are explicitly adapter values, not Valve values.
		const float kMinimumAirTime = 0.05f;
		const float kMinimumFallSpeed = 100.0f;
		const float kLandingDuration = 0.25f;
		const float kLandingVelocityScale = 0.005f;
		const float kMaxLandingAmount = 2.0f;
		const float kRollFullSpeed = 180.0f; // degrees/second
		const float kRollResponse = 12.0f; // 1/second

		float Clamp( float v, float lo, float hi ) { return v < lo ? lo : ( v > hi ? hi : v ); }
		bool Finite( float v ) { return std::isfinite( v ); }
		bool Finite( const Vec3 &v ) { return Finite( v.x ) && Finite( v.y ) && Finite( v.z ); }
		float AngleDelta( float a, float b )
		{
			float d = std::fmod( a - b, 360.0f );
			if( d > 180.0f ) d -= 360.0f;
			if( d < -180.0f ) d += 360.0f;
			return d;
		}

		// Valve/GoldSrc AngleVectors convention, including the complete
		// forward vector. Null right/up outputs do NOT zero forward.y/z.
		void Basis( const Vec3 &angles, Vec3 &forward, Vec3 &right, Vec3 &up )
		{
			const float pitch = angles.x * kPi / 180.0f;
			const float yaw = angles.y * kPi / 180.0f;
			const float roll = angles.z * kPi / 180.0f;
			const float sp = std::sin( pitch ), cp = std::cos( pitch );
			const float sy = std::sin( yaw ), cy = std::cos( yaw );
			const float sr = std::sin( roll ), cr = std::cos( roll );
			forward = Vec3( cp * cy, cp * sy, -sp );
			right = Vec3( -sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp );
			up = Vec3( cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp );
		}

		void Push( SwayState &state, float time, const Vec3 &angles )
		{
			state.head = ( state.head + 1 ) % kSwayHistory;
			state.history[state.head].time = time;
			state.history[state.head].angles = angles;
			if( state.count < kSwayHistory ) ++state.count;
		}

		bool SampleAt( const SwayState &state, float target, Vec3 &angles )
		{
			for( int i = 0; i + 1 < state.count; ++i )
			{
				const AngleSample &newer = state.history[( state.head - i + kSwayHistory ) % kSwayHistory];
				const AngleSample &older = state.history[( state.head - i - 1 + kSwayHistory ) % kSwayHistory];
				if( older.time <= target && target <= newer.time )
				{
					const float span = newer.time - older.time;
					const float f = span > 0.0f ? Clamp( ( target - older.time ) / span, 0.0f, 1.0f ) : 1.0f;
					for( int k = 0; k < 3; ++k )
						angles[k] = older.angles[k] + AngleDelta( newer.angles[k], older.angles[k] ) * f;
					return true;
				}
			}
			return false;
		}

		bool SameContext( const EffectsState &state, const EffectsInput &input )
		{
			return state.view_entity == input.view_entity && state.weapon_id == input.weapon_id &&
				state.model_index == input.model_index && state.last_left_hand == input.left_hand;
		}

		void Prime( EffectsState &state, const EffectsInput &input )
		{
			state.primed = true;
			state.last_time = input.time;
			state.last_mouse_angles = input.mouse_angles;
			state.last_grounded = input.grounded;
			state.last_left_hand = input.left_hand;
			state.view_entity = input.view_entity;
			state.weapon_id = input.weapon_id;
			state.model_index = input.model_index;
			state.fall_speed = input.grounded ? 0.0f : Clamp( -input.vertical_speed, 0.0f, 10000.0f );
		}
	}

	void ResetSway( SwayState &state )
	{
		// Old slots need not be cleared: count prevents all stale reads.
		state.head = state.count = 0;
		state.last_time = 0.0f;
		state.has_time = state.was_paused = false;
		state.last_offset = Vec3();
	}

	void ResetEffects( EffectsState &state )
	{
		ResetSway( state.sway );
		state.primed = state.was_paused = false;
		state.last_grounded = true;
		state.air_time = state.fall_speed = state.landing_amount = state.roll = 0.0f;
		state.landing_time = -1.0f;
		state.last_output = EffectsOutput();
	}

	Vec3 StepSway( SwayState &state, float time, const Vec3 &angles,
		float interp, float scale, bool left_hand, bool paused )
	{
		if( !Finite( time ) || !Finite( angles ) || !Finite( interp ) || !Finite( scale ) || interp <= 0.0f || scale == 0.0f )
		{
			ResetSway( state );
			return Vec3();
		}
		if( state.has_time && ( time < state.last_time || time - state.last_time > kMaxSampleGap ) )
			ResetSway( state );
		if( paused )
		{
			state.was_paused = true;
			return state.last_offset;
		}
		if( state.was_paused ) ResetSway( state );
		if( state.has_time && time == state.last_time ) return state.last_offset;
		state.last_time = time;
		state.has_time = true;
		Push( state, time, angles );
		Vec3 lagged;
		state.last_offset = Vec3();
		if( !SampleAt( state, time - interp, lagged ) ) return state.last_offset;

		Vec3 forward, right, up, lagged_forward, unused_right, unused_up;
		Basis( angles, forward, right, up );
		// CS:GO: angleDiff = lagged - current; AngleVectors(-angleDiff,...).
		const Vec3 difference( AngleDelta( angles.x, lagged.x ), AngleDelta( angles.y, lagged.y ), AngleDelta( angles.z, lagged.z ) );
		Basis( difference, lagged_forward, unused_right, unused_up );
		const Vec3 diff( ( 1.0f - lagged_forward.x ) * scale, -lagged_forward.y * scale, -lagged_forward.z * scale );
		const float hand = left_hand ? -1.0f : 1.0f;
		const float vertical_fade = Clamp( std::fabs( up.z ) - 0.02f, 0.0f, 1.0f );
		for( int k = 0; k < 3; ++k )
			state.last_offset[k] = ( forward[k] * diff.x + right[k] * hand * -diff.y + up[k] * diff.z ) * vertical_fade;
		if( !Finite( state.last_offset ) ) state.last_offset = Vec3();
		return state.last_offset;
	}

	EffectsOutput StepEffects( EffectsState &state, const EffectsParams &params, const EffectsInput &input )
	{
		if( !input.active || !Finite( input.time ) || !Finite( input.frame_time ) || !Finite( input.view_angles ) ||
			!Finite( input.mouse_angles ) || !Finite( input.vertical_speed ) )
		{
			ResetEffects( state );
			return EffectsOutput();
		}
		if( state.primed && ( !SameContext( state, input ) || input.time < state.last_time || input.time - state.last_time > kMaxSampleGap ) )
			ResetEffects( state );
		if( input.paused )
		{
			state.was_paused = true;
			return state.last_output;
		}
		if( state.was_paused ) ResetEffects( state );
		if( !state.primed )
		{
			Prime( state, input );
			state.last_output.sway_offset = StepSway( state.sway, input.time, input.view_angles,
				params.sway_interp, params.sway_scale, input.left_hand );
			return state.last_output;
		}
		if( input.time == state.last_time ) return state.last_output;
		// Simulation time, rather than an FPS-dependent per-frame factor,
		// controls recovery. Repeated renderer calls never integrate twice.
		const float dt = input.time - state.last_time;
		EffectsOutput output;
		output.sway_offset = StepSway( state.sway, input.time, input.view_angles,
			params.sway_interp, params.sway_scale, input.left_hand );

		if( !input.grounded )
		{
			state.air_time += dt;
			state.fall_speed = Clamp( -input.vertical_speed, state.fall_speed, 10000.0f );
		}
		else
		{
			if( !state.last_grounded && state.air_time >= kMinimumAirTime && state.fall_speed >= kMinimumFallSpeed )
			{
				state.landing_time = input.time;
				state.landing_amount = Clamp( state.fall_speed * kLandingVelocityScale, 0.0f, kMaxLandingAmount );
			}
			state.air_time = state.fall_speed = 0.0f;
		}
		const float phase = ( input.time - state.landing_time ) / kLandingDuration;
		if( state.landing_time >= 0.0f && phase >= 0.0f && phase < 1.0f && Finite( params.land_scale ) && params.land_scale > 0.0f )
			output.land_dip = state.landing_amount * ( 0.5f - 0.5f * std::cos( phase * 2.0f * kPi ) ) * params.land_scale;
		else if( phase >= 1.0f || !Finite( params.land_scale ) || params.land_scale <= 0.0f )
			state.landing_amount = 0.0f;

		if( Finite( params.roll_angle ) && params.roll_angle > 0.0f )
		{
			const float yaw_speed = AngleDelta( input.mouse_angles.y, state.last_mouse_angles.y ) / dt;
			const float target = Clamp( yaw_speed / kRollFullSpeed, -1.0f, 1.0f ) * params.roll_angle * ( input.left_hand ? -1.0f : 1.0f );
			state.roll = Clamp( state.roll + ( target - state.roll ) * ( 1.0f - std::exp( -kRollResponse * dt ) ), -params.roll_angle, params.roll_angle );
			output.mouse_roll = state.roll;
		}
		else state.roll = 0.0f;

		if( !Finite( output.land_dip ) ) output.land_dip = 0.0f;
		if( !Finite( output.mouse_roll ) ) output.mouse_roll = state.roll = 0.0f;
		state.last_time = input.time;
		state.last_mouse_angles = input.mouse_angles;
		state.last_grounded = input.grounded;
		state.last_output = output;
		return output;
	}

	Vec3 FixedOffset( const Vec3 &angles, float x, float y, float z, bool left_hand )
	{
		if( !Finite( angles ) || !Finite( x ) || !Finite( y ) || !Finite( z ) ) return Vec3();
		Vec3 forward, right, up, offset;
		Basis( angles, forward, right, up );
		if( left_hand ) x = -x;
		for( int k = 0; k < 3; ++k ) offset[k] = right[k] * x + forward[k] * y + up[k] * z;
		return Finite( offset ) ? offset : Vec3();
	}
}
