#pragma once
#include <cmath>

// CS:GO weapon_ironsightcontroller.cpp:108,219,597 supplies approach speeds,
// Gain(0.8), FOV and crosshair threshold. Offsets describe the converted OC2
// model's real scope mesh, independently measured in tools/weapon_flow.
namespace view_ads
{
inline float Clamp(float x,float lo,float hi) { return x<lo?lo:(x>hi?hi:x); }
inline bool Supported(int id) { return id==8 || id==27; }
inline float Gain(float x)
{
	x=Clamp(x,0,1);
	const float exponent=std::log(.2f)*-1.4427f;
	return x<.5f?.5f*std::pow(2*x,exponent):1-.5f*std::pow(2-2*x,exponent);
}
struct Profile
{
	const char *model;
	const char *bone;
	float center[3];
	float localCenter[3];
	float radius;
};
inline const Profile *Get(int id)
{
	static const Profile aug={"models/v_aug.mdl","v_weapon.aug_Parent",
		{11.346256f,-3.592155f,-.080703f},{-.010004f,6.395318f,-1.262838f},.4f};
	static const Profile sg={"models/v_sg552.mdl","v_weapon.sg556_Parent",
		{14.954708f,-5.150946f,-1.217012f},{-.080420f,6.094919f,1.064267f},.63f};
	return id==8?&aug:(id==27?&sg:nullptr);
}
struct State
{
	int weapon=0;
	float amount=0,scopeFov=45,lastTime=-1;
	bool sighted=false;
	void Reset() { *this=State(); }
	void Update(int id,bool active,bool reloading,float rawFov,float time,bool paused=false)
	{
		if (!active || !Supported(id) || !std::isfinite(time) || !std::isfinite(rawFov)) { Reset(); return; }
		if (paused && weapon==id) { lastTime=time; return; }
		if (weapon!=id || lastTime<0 || time<lastTime || time-lastTime>1)
		{
			Reset();weapon=id;lastTime=time;
		}
		sighted=!reloading && rawFov>0 && rawFov<90;
		if (sighted) scopeFov=Clamp(rawFov,41,89);
		float dt=Clamp(time-lastTime,0,.1f);lastTime=time;
		if (paused) return;
		const float step=dt*(sighted?10.0f:8.0f);
		amount=Clamp(amount+(sighted?step:-step),0,1);
	}
	float Fov(float hipFov) const { return hipFov+(scopeFov-hipFov)*amount; }
};
}

struct cl_entity_s;
float V_ADSWorldFOV(float rawFov,float time);
bool V_DrawADSCrosshair();
void V_ApplyADSBody(cl_entity_s *entity);
void V_ObserveADSSight(cl_entity_s *entity,const char *bone,const float matrix[3][4]);
