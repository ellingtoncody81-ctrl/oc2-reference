#pragma once
#include <cmath>
#include <cstring>

namespace hud_presentation
{
enum Bomb { RoundTimer, Planted, Defused, Exploded };
inline Bomb TextBombState(Bomb previous, const char *key)
{
    if (!key) return previous;
    if (*key == '#') ++key;
    if (!std::strncmp(key,"Cstrike_TitlesTXT_",18)) key += 18;
    if (!std::strcmp(key,"Bomb_Planted")) return Planted;
    if (!std::strcmp(key,"Bomb_Defused")) return Defused;
    if (!std::strcmp(key,"Target_Bombed")) return Exploded;
    return previous;
}
inline int BombAlpha(float time)
{
    return std::isfinite(time) && (int(std::fmod(std::fabs(time)*2.0f,2.0f)) & 1) ? 100 : 255;
}
struct Rect { float x,y,width,height; };
inline float CenterY(float height)
{
    // Match the original client CenterPrint anchor at one quarter screen height.
    return float(int(height * .25f));
}
inline float CenterExpansion(float elapsed, float remaining)
{
    if (!std::isfinite(elapsed) || !std::isfinite(remaining) || elapsed < 0 || remaining <= 0) return 0;
    float value = 1;
    if (elapsed < .3f)
    {
        const float t = 1 - elapsed * 3.3333f;
        value = 1 - t*t*t;
    }
    else if (remaining < .3f)
    {
        const float t = remaining * 3.3333f;
        value = t*t*t;
    }
    return value < 0 ? 0 : value > 1 ? 1 : value;
}
inline Rect BombRect(float center, float top, float scale, Bomb state)
{
    // Keep the original 120:70 drawing proportions, scaled into the time row.
    // The two 360x80 images have different transparent margins: center the
    // visible vertical bounds (3..54 planted, 7..43 defused) at row y=23.
    const float height = (140.0f/3.0f)*scale;
    const float visibleCenter = state == Defused ? 25.0f : 28.5f;
    return {center-40*scale,top+23*scale-height*visibleCenter/80.0f,80*scale,height};
}
}
