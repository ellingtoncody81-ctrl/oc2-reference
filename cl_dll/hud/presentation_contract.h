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
    // APK enginefuncs[31] -> sub_44410: inlined CL_CenterPrint(text, .25).
    return float(int(double(height * 1.25f) * 0.578034682));
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
inline Rect BombRect(float center, float top, float scale)
{
    // The APK deliberately maps the complete 360x80 texture to 120x70.
    // Its source image is pre-stretched; alpha cropping/aspect containment
    // would undo this layout and make the C4 device look wide and flat.
    return {center-60*scale,top+10*scale,120*scale,70*scale};
}
}
