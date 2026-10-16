#pragma once

#include <algorithm>
#include <cmath>

namespace oc2_buy_scroll
{
// Positions are in card rows, independent of screen resolution. Each wheel
// impulse adds distance; it never turns into a page or wraps at an endpoint.
constexpr float visibleRows = 5.0f;
constexpr float rowsPerNotch = 0.72f;

struct State
{
	float position = 0.0f;
	float target = 0.0f;

	static float Maximum(int count) { return std::max(0.0f, count - visibleRows); }
	static float Bounded(float value, float maximum)
	{
		return std::isfinite(value) ? std::max(0.0f, std::min(value, maximum)) : 0.0f;
	}
	void Clamp(int count)
	{
		position = Bounded(position, Maximum(count));
		target = Bounded(target, Maximum(count));
	}
	void Scroll(float notches, int count)
	{
		Clamp(count);
		if (std::isfinite(notches))
			target = Bounded(target + std::max(-64.0f, std::min(64.0f, notches)) * rowsPerNotch, Maximum(count));
	}
	void Reveal(int row, int count)
	{
		Clamp(count);
		if (row < target) target = float(row);
		else if (row + 1 > target + visibleRows) target = row + 1 - visibleRows;
		Clamp(count);
	}
	void Advance(float seconds, int count)
	{
		Clamp(count);
		if (!std::isfinite(seconds) || seconds <= 0.0f) return;
		// Exponential easing is stable across frame rates and cannot overshoot.
		// A long pause contributes at most 100 ms, avoiding resume-time jumps.
		position += (target - position) * (1.0f - std::exp(-18.0f * std::min(seconds, 0.1f)));
		if (std::fabs(target - position) < 0.0001f) position = target;
		Clamp(count);
	}
};
}
