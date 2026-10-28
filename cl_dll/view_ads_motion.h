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

// The Source controller lets its remembered view catch up at 60/s, then
// rotates the sight around its pivot. Keep a time-based equivalent so the
// response does not depend on the number of refdef calls per frame.
struct TurnState
{
    float last[2] = {}, input[2] = {}, output[2] = {}, time = 0;
    float speedRatio = 0, swayScale = 0, hand = 1;
    int weapon = 0;
    bool primed = false;
};

inline float Safe(float value) { return std::isfinite(value) ? value : 0.0f; }

inline float AngleDelta(float a, float b)
{
    float delta = std::fmod(a - b, 360.0f);
    if (delta > 180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;
    return delta;
}

inline void StepTurn(TurnState &state, const float angles[3], float time,
                     float speedRatio, float maxSpeed, float swayScale, bool active, bool paused,
                     int weapon, bool left, float output[3])
{
    output[0] = output[1] = output[2] = 0;
    if (!active || !std::isfinite(time) || !std::isfinite(angles[0]) ||
        !std::isfinite(angles[1]))
    {
        state = TurnState();
        return;
    }
    float ratio = Safe(speedRatio);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    const float scale = Safe(swayScale);
    const float hand = left ? -1.0f : 1.0f;
    if (!state.primed || state.weapon != weapon ||
        time < state.time || time - state.time > 1.0f)
    {
        state = TurnState();
        state.primed = true; state.weapon = weapon;
        state.time = time;
        state.speedRatio = ratio; state.swayScale = scale; state.hand = hand;
        for (int axis = 0; axis < 2; ++axis) state.last[axis] = state.input[axis] = angles[axis];
    }
    else if (!paused && time > state.time)
    {
        const float dt = time - state.time;
        const float decay = std::exp(-60.0f * dt);
        const float follow = (1.0f - decay) / (60.0f * dt);
        // Follow speed with the Source-style 320 units/second approach limit.
        // Applying it to the ratio also smooths a zoom-dependent speed cap.
        const float safeMaxSpeed = std::isfinite(maxSpeed) && maxSpeed > 1.0f ? maxSpeed : 320.0f;
        const float speedStep = 320.0f / safeMaxSpeed * dt;
        const float speedChange = ratio - state.speedRatio;
        state.speedRatio += speedChange > speedStep ? speedStep :
            (speedChange < -speedStep ? -speedStep : speedChange);
        // Settings and handedness can change while aimed. Fade their effect
        // over time without resetting the physical view-angle history.
        const float blend = 1.0f - std::exp(-10.0f * dt);
        state.swayScale += (scale - state.swayScale) * blend;
        state.hand += (hand - state.hand) * blend;
        const float gain = (0.03f + 0.27f * state.speedRatio) * state.swayScale / 1.6f;
        for (int axis = 0; axis < 2; ++axis)
        {
            // Integrate a linearly changing input over this frame; comparing
            // only against the end sample makes a 24 FPS turn much larger.
            const float previous = AngleDelta(state.last[axis], state.input[axis]);
            const float change = AngleDelta(angles[axis], state.input[axis]);
            const float delta = previous * decay - change * follow;
            float residual = delta;
            if (residual > 2.0f) residual = 2.0f;
            if (residual < -2.0f) residual = -2.0f;
            state.output[axis] = residual * gain;
            state.last[axis] = angles[axis] + delta;
            state.input[axis] = angles[axis];
        }
        state.time = time;
    }
    // Studio reverses entity pitch; left-hand rendering mirrors yaw.
    output[0] = -state.output[0];
    output[1] = state.output[1] * state.hand;
}

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
					 float mouseRoll, float speedRatio, bool optical = false,
                     const float *turn = nullptr)
{
    Motion result;
    result.speedRatio = Safe(speedRatio);
    if (result.speedRatio < 0) result.speedRatio = 0;
    if (result.speedRatio > 1) result.speedRatio = 1;
    result.bobGain = optical ? 1.50f : 0.50f;
    result.inertiaGain = 0.10f + 0.20f * result.speedRatio;
    for (int axis = 0; axis < 3; ++axis)
    {
        result.position[axis] = Safe(bob.position[axis]) * result.bobGain +
            Safe(sway[axis]) * result.inertiaGain;
        result.angles[axis] = Safe(bob.angles[axis]) * result.bobGain +
            (turn ? Safe(turn[axis]) : 0.0f);
    }
    result.position[2] -= Safe(landDip) * 0.10f;
    result.angles[2] += Safe(mouseRoll) * result.inertiaGain;
    Limit(result.position, 0.12f);
    Limit(result.angles, optical ? 0.90f : 0.50f);
    return result;
}
}
