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
	float step_size = 18.0f;
	bool enabled = true, paused = false, grounded = true;
	bool static_support = true, jumping = false;
};
struct Output
{
	float offset = 0.0f, target_z = 0.0f;
	bool supported = false;
};
struct State
{
	bool primed = false, tracking = false, was_paused = false;
	float time = 0.0f, x = 0.0f, y = 0.0f, z = 0.0f;
	float view_height = 0.0f, smooth_z = 0.0f;
	Output output;
};
Output Step( State &state, const Input &input );
}
