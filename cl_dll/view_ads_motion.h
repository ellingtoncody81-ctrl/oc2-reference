// ADS keeps movement bob and turn looseness after its sight-line placement.
// CS:GO weapon_ironsightcontroller.cpp:233-255,343-365 retains both at full
// aim. These smaller, bounded amplitudes adapt the existing GoldSrc effects;
// no eye/camera movement or new time integration is introduced here.
#pragma once
#include <cmath>

namespace view_ads_motion
{
struct Motion
{
    float position[3] = {}, angles[3] = {};
    float speedRatio = 0, bobGain = 0, inertiaGain = 0;
};

inline float Safe(float value) { return std::isfinite(value) ? value : 0.0f; }

inline void Limit(float value[3], float maximum)
{
    float length2 = 0;
    for (int axis = 0; axis < 3; ++axis)
    {
        value[axis] = Safe(value[axis]);
        // Bound before squaring, including pathological cvar magnitudes.
        if (value[axis] > maximum) value[axis] = maximum;
        if (value[axis] < -maximum) value[axis] = -maximum;
        length2 += value[axis] * value[axis];
    }
    if (length2 > maximum * maximum)
    {
        const float scale = maximum / std::sqrt(length2);
        for (int axis = 0; axis < 3; ++axis) value[axis] *= scale;
    }
}

inline Motion Dampen(const Motion &bob, const float sway[3], float landDip,
					 float mouseRoll, float speedRatio, bool optical = false)
{
    Motion result;
    result.speedRatio = Safe(speedRatio);
    if (result.speedRatio < 0) result.speedRatio = 0;
    if (result.speedRatio > 1) result.speedRatio = 1;
    result.bobGain = optical ? 1.50f : 0.20f;
    // The local Source controller uses 0.1 stationary / 0.3 running looseness.
    result.inertiaGain = 0.10f + 0.20f * result.speedRatio;
    for (int axis = 0; axis < 3; ++axis)
    {
        result.position[axis] = Safe(bob.position[axis]) * result.bobGain +
            Safe(sway[axis]) * result.inertiaGain;
        result.angles[axis] = Safe(bob.angles[axis]) * result.bobGain;
    }
    result.position[2] -= Safe(landDip) * 0.10f;
    result.angles[2] += Safe(mouseRoll) * result.inertiaGain;
    Limit(result.position, 0.12f);
    Limit(result.angles, optical ? 0.90f : 0.20f);
    return result;
}
}
