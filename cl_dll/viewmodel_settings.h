// CS:GO client/view.cpp presets; cs_gamerules.cpp competitive limits;
// baseviewmodel_shared.cpp offset axes; clientmode_shared.cpp ADS model FOV.
#pragma once
#include <cmath>

namespace viewmodel_settings
{
struct Settings { float fov=54, x=0, y=0, z=0; };
inline float Clamp(float value,float lo,float hi,float fallback=0)
{
    if (!std::isfinite(value)) return fallback;
    return value<lo?lo:(value>hi?hi:value);
}
inline Settings Effective(float fov,float x,float y,float z)
{
    Settings result;
    result.fov=Clamp(fov,54,68,54);result.x=Clamp(x,-2,2.5f);
    result.y=Clamp(y,-2,2);result.z=Clamp(z,-2,2);
    return result;
}
inline float ModelFov(float hip,float ads)
{
    hip=Clamp(hip,54,68,54);
    return hip+(54-hip)*Clamp(ads,0,1);
}
inline bool Preset(int number,Settings &result)
{
    if (number==1) { result={60,1,1,-1};return true; }
    if (number==2) { result={54,0,0,0};return true; }
    if (number==3) { result={68,2.5f,0,-1.5f};return true; }
    return false;
}
inline void Offset(const Settings &settings,const float forward[3],const float right[3],
                   const float up[3],bool left,float result[3])
{
    const float x=left?-settings.x:settings.x;
    for (int i=0;i<3;++i) result[i]=forward[i]*settings.y+right[i]*x+up[i]*settings.z;
}
}
