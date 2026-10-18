#ifndef OC2_GRENADE_THROW_H
#define OC2_GRENADE_THROW_H
#include <math.h>
// CS:GO weapon_basecsgrenade.cpp:348-432,558-634. The 2019 HE/flash/smoke
// schema gives throw velocity 750. Projectile collision remains GoldSrc.
namespace oc2_grenade
{
enum { Primary=1, Secondary=2048, Idle=0, BeginRelease=1, Emit=2, Invalid=3 };
inline bool Finite(float x) { return x-x == 0; }
inline float Clamp(float v,float low,float high) { return v<low?low:v>high?high:v; }
inline float Strength(float encoded) { return Finite(encoded)?Clamp(encoded/1024.0f,0,1):1; }
// fuser2/fuser3 use 1/128s network precision. Quantize on both sides, keep
// timestamps bounded, and reserve zero/negative values for the state machine.
inline float Clock(float now) { return floorf(fmodf(now,4096.0f)*128.0f)/128.0f+1.0f; }
inline float Deadline(float now)
{
	float ticks=ceilf((fmodf(now,4096.0f)+.1f)*128.0f);
	return fmodf(ticks,4096.0f*128.0f)/128.0f+1.0f;
}
inline float Elapsed(float newer,float older)
{
	float delta=newer-older;
	if (delta < -2048) delta+=4096;
	else if (delta > 2048) delta-=4096;
	return delta;
}
inline float Ideal(int buttons) { return buttons==(Primary|Secondary)?.5f:(buttons&Primary)?1.0f:0.0f; }
inline void Reset(float &held,float &release,float &strength) { held=0;release=-1;strength=1024; }
inline int Advance(float &held,float &release,float &strength,int buttons,float now)
{
	if (!Finite(now) || now<0 || !Finite(held) || !Finite(release) || !Finite(strength))
		{ Reset(held,release,strength);return Invalid; }
	if (held<=0) return Idle;
	buttons &= Primary|Secondary;
	if (release==0)
	{
		if (!buttons) { release=Deadline(now);return BeginRelease; }
		const float current=Strength(strength),target=Ideal(buttons);
		const float amount=Clamp(Elapsed(Clock(now),held),0,.25f)*1.3f;
		strength=(current<target?Clamp(current+amount,current,target):Clamp(current-amount,target,current))*1024.0f;
		held=Clock(now);return Idle;
	}
	return release>0 && Elapsed(Clock(now),release)>=0 ? Emit : Idle;
}
inline float Speed(float strength) { return 675.0f*(.3f+.7f*Clamp(strength,0,1)); }
inline float Lower(float strength) { return -12.0f*(1.0f-Clamp(strength,0,1)); }
inline float Pitch(float pitch)
{
	if (pitch>90) pitch-=360;else if (pitch < -90) pitch+=360;
	pitch=Clamp(pitch,-90,90);
	return pitch-10*(90-fabsf(pitch))/90.0f;
}
inline float ThrowDuration(int weapon,bool underhand) { return weapon==4 || underhand ? .4f : 22.0f/30.0f; }
}
#endif
