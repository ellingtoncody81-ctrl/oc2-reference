#pragma once

// CS:GO SmoothViewOnStairs, with a presentation-only GoldSrc support adapter.
// This module never changes movement, ground flags or weapon effects.
namespace view_stairs
{
struct Input
{
	float time = 0.0f, frame_time = 0.0f;
	float x = 0.0f, y = 0.0f, z = 0.0f, view_height = 0.0f;
	float vertical_speed = 0.0f, support_z = 0.0f;
	float normal_x = 0.0f, normal_y = 0.0f, normal_z = 1.0f;
	int support_entity = 0;
	float step_size = 18.0f;
	bool enabled = true, paused = false, grounded = true;
	bool static_support = true, jumping = false;
	// Set only after tracing the actual support between nonparallel faces.
	bool seam_verified = false;
};
struct Output
{
	float offset = 0.0f, target_z = 0.0f;
	bool supported = false;
	bool continuous_slope = false;
	float plane_delta = 0.0f;
};
struct State
{
	bool primed = false, tracking = false, was_paused = false;
	float time = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f;
	float view_height = 0.0f, smooth_z = 0.0f;
	float support_z = 0.0f, normal_x = 0.0f, normal_y = 0.0f, normal_z = 1.0f;
	int support_entity = 0;
	Output output;
};
struct SeamProbe
{
	bool required = false;
	float crossing = 0.0f, previous_delta = 0.0f, current_delta = 0.0f;
};
SeamProbe DescribeSeam( const State &state, const Input &input );
float SeamHeight( const State &state, const Input &input, const SeamProbe &seam, float fraction );
bool MatchSeamSample( const State &state, const Input &input, const SeamProbe &seam,
	float fraction, float support_z, float normal_x, float normal_y, float normal_z, int entity );
Output Step( State &state, const Input &input );
}
