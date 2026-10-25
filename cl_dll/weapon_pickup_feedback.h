#pragma once

#include <cmath>
#include <cstdint>

namespace weapon_pickup
{
constexpr int kSlots = 32;
constexpr float kDuration = 4.0f;
struct Item
{
	bool pending = false;
	float start = -1.0f, received = -1.0f;
};
struct State
{
	Item items[kSlots];
	float lastTime = -1.0f;
	unsigned int events = 0;
};
inline void Clear(State &state) { state = State(); }
inline void Receive(State &state, int id, float time)
{
	if (id <= 0 || id >= kSlots || !std::isfinite(time)) return;
	Item &item = state.items[id];
	// Initial ammo and WeapPickup can describe the same acquisition packet.
	if (item.received >= 0 && time >= item.received && time-item.received < 0.05f) return;
	item.pending = true; item.start = -1; item.received = time;
	++state.events;
}
inline void Tick(State &state, float time, bool menuOpen, std::uint32_t owned, bool alive)
{
	if (!std::isfinite(time)) return;
	if (!alive || (state.lastTime >= 0 && time < state.lastTime)) { Clear(state); return; }
	state.lastTime = time;
	for (int id = 1; id < kSlots; ++id)
	{
		Item &item = state.items[id];
		if (!(owned & (std::uint32_t(1) << id)))
		{
			// Messages may precede the inventory snapshot by a frame.
			if (item.start >= 0 || (item.received >= 0 && time-item.received > 2.0f)) item = Item();
			continue;
		}
		if (item.pending && !menuOpen) { item.pending = false; item.start = time; }
		if (item.start >= 0 && time-item.start >= kDuration) item.start = -1;
	}
}
inline float Strength(const Item &item, float time)
{
	const float elapsed = time-item.start;
	if (item.start < 0 || !std::isfinite(elapsed) || elapsed < 0 || elapsed >= kDuration) return 0;
	const float fade = elapsed<2.8f?1.0f:(kDuration-elapsed)/1.2f;
	const float entrance = elapsed < 0.10f ? elapsed/0.10f : 1.0f;
	// Hold a bright icon through several soft pulses, then fade smoothly.
	const float wave = 0.75f+0.25f*std::cos(elapsed*9.42477796f);
	return entrance*fade*wave;
}
}
