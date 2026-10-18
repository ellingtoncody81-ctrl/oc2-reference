#pragma once

namespace hud_readability
{
constexpr float enlargement=1.2f;
inline float Scale(float base) { return base*enlargement; }
inline float AnchorScale(float width,float base)
{
	const float desired=Scale(base),narrow=width/1600.0f;
	return desired<narrow?desired:narrow;
}
}
