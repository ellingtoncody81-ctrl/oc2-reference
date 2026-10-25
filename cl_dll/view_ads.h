#pragma once
#include <cmath>
#include "../dlls/wpn_shared/oc2_weapon_params.h"

// CS:GO weapon_ironsightcontroller.cpp:108,219,597 supplies approach speeds,
// Gain(0.8), FOV and crosshair threshold. Offsets describe the converted OC2
// model's real scope mesh, independently measured in tools/weapon_flow.
namespace view_ads
{
inline float Clamp(float x,float lo,float hi) { return x<lo?lo:(x>hi?hi:x); }
inline bool Supported(int id) { return oc2_weapon::SupportsAim(id); }
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
	float angles[3];
	float localFront[3];
};
inline const Profile *Get(int id)
{
	// AK/FAMAS/M249 also constrain roll with the measured upper receiver plane;
	// the rear/front sight line alone leaves that degree of freedom unresolved.
	static const Profile p21={"models/v_m3.mdl","v_weapon.NOVA_PARENT",{14.0937285f,-5.3659978f,-1.8899957f},{-0.0099384f,2.6696895f,3.7146899f},0.000000f,{0.1219057f,-0.0243812f,0.0000000f},{-0.0082299f,2.7152673f,27.2147010f}};
	static const Profile p19={"models/v_mp7.mdl","v_weapon.mp7_parent",{13.0034471f,-5.4116170f,-0.7563493f},{-0.0060825f,5.1878991f,-3.1330721f},0.000000f,{0.3365532f,0.0810230f,0.0000000f},{0.0003803f,5.2500768f,6.0598832f}};
	static const Profile p1={"models/v_p228.mdl","v_weapon.p250_parent",{13.7614387f,-5.1431513f,-1.2740882f},{-0.0271565f,3.1623820f,-0.7513950f},0.000000f,{-1.5697351f,0.2368734f,0.0000000f},{-0.0198587f,3.0523578f,6.0230081f}};
	static const Profile p27={"models/v_sg552.mdl","v_weapon.sg556_Parent",{14.9547080f,-5.1509460f,-1.2170120f},{-0.0804200f,6.0949190f,1.0642670f},0.630000f,{0.0000000f,0.0000000f,0.0000000f},{-0.0804200f,6.0949190f,1.0642670f}};
	static const Profile p23={"models/v_tmp.mdl","v_weapon.mp9_parent",{11.8358803f,-5.2679850f,-0.8733676f},{0.0011023f,4.5407169f,-2.0744237f},0.000000f,{-1.0025675f,0.2035499f,0.0000000f},{-0.0136782f,4.4200310f,5.5258154f}};
	static const Profile p28={"models/v_ak47.mdl","v_weapon.ak47_parent",{19.2634034f,-6.9373887f,-0.6909891f},{-0.0042481f,5.4006330f,8.7996990f},0.000000f,{-0.9026520f,0.0258157f,-2.0984740f},{-0.0103815f,5.3402944f,24.5995449f}};
	static const Profile p8={"models/v_aug.mdl","v_weapon.aug_Parent",{11.3462560f,-3.5921550f,-0.0807030f},{-0.0100040f,6.3953180f,-1.2628380f},0.400000f,{0.0000000f,0.0000000f,0.0000000f},{-0.0100040f,6.3953180f,-1.2628380f}};
	static const Profile p26={"models/v_deagle.mdl","v_weapon.deagle_parent",{13.8777727f,-5.2328629f,-1.0435109f},{-0.0542729f,3.8604175f,-0.6200184f},0.000000f,{-0.6966655f,0.1534020f,0.0000000f},{-0.0297329f,3.8497844f,8.3446365f}};
	static const Profile p15={"models/v_famas.mdl","v_weapon.famas_Parent",{13.5935023f,-6.9981212f,-0.3633107f},{0.0356798f,6.6695820f,-2.2315144f},0.000000f,{0.2628177f,-0.0407537f,-2.3727711f},{0.0049894f,6.7299454f,11.2099599f}};
	static const Profile p11={"models/v_fiveseven.mdl","v_weapon.fiveSeven_parent",{13.8222750f,-5.1870384f,-1.2840957f},{-0.0174880f,2.9650147f,-1.1916154f},0.000000f,{-0.8003411f,0.0330059f,0.0000000f},{0.0150306f,2.9457522f,5.7526604f}};
	static const Profile p14={"models/v_galil.mdl","v_weapon.galilar_parent",{12.6157636f,-5.2444000f,-0.4062126f},{0.0017743f,5.3753430f,0.9575570f},0.000000f,{-1.5622249f,-0.0000000f,0.0000000f},{0.0000359f,4.9456849f,17.4580992f}};
	static const Profile p17={"models/v_glock18.mdl","v_weapon.glock_parent",{13.1747919f,-4.9135767f,-2.1129129f},{0.0150095f,2.7766112f,-1.4312439f},0.000000f,{0.0675975f,0.3157220f,0.0000000f},{-0.0250243f,2.8659607f,5.7727052f}};
	static const Profile p20={"models/v_m249.mdl","v_weapon.m249_Parent",{15.0014866f,-5.5531517f,-0.9763230f},{-0.0429538f,7.0189214f,-0.2597900f},0.000000f,{-0.0767164f,0.0017112f,-1.2777426f},{-0.2051246f,6.8080492f,22.1386504f}};
	static const Profile p22={"models/v_m4a1.mdl","v_weapon.M4A1_s_Parent",{13.6727191f,-5.6644471f,0.3754883f},{-0.2294125f,8.2692014f,-0.4735447f},0.000000f,{-1.1941297f,-0.2279741f,0.0000000f},{-0.2550011f,7.9600171f,16.6049826f}};
	static const Profile p7={"models/v_mac10.mdl","v_weapon.mac10_parent",{16.9874093f,-7.0399817f,-2.1515233f},{-0.0186105f,4.5722242f,-3.5215783f},0.000000f,{-0.6684101f,-0.3183066f,0.0000000f},{0.0478399f,4.4730149f,5.4783810f}};
	static const Profile p30={"models/v_p90.mdl","v_weapon.p90_Parent",{12.8987481f,-5.3354871f,0.4617458f},{-0.0044512f,6.7150347f,1.3646883f},0.000000f,{-0.7185231f,0.0378190f,0.0000000f},{0.0048194f,6.6696272f,5.9098105f}};
	static const Profile p12={"models/v_ump45.mdl","v_weapon.ump45_Parent",{13.7059773f,-5.4355642f,-0.9935972f},{-0.0095423f,6.9496688f,0.0505150f},0.000000f,{0.8708954f,-0.1110051f,0.0000000f},{0.0205100f,7.1297131f,13.4708493f}};
	static const Profile p16={"models/v_usp.mdl","v_weapon.usp_parent",{14.4788813f,-4.4050000f,-1.6212325f},{0.0003547f,3.7379238f,-0.9674217f},0.000000f,{0.7957236f,-0.0000000f,0.0000000f},{0.0002732f,3.9184128f,6.2310103f}};
	static const Profile p5={"models/v_xm1014.mdl","v_weapon.xm1014_Parent",{13.6075518f,-5.4000000f,-1.1434314f},{-0.0376073f,4.8087597f,2.8430338f},0.000000f,{-0.3646046f,-0.0000000f,0.0000000f},{-0.0321281f,4.6684735f,24.8430313f}};
	switch(id) {
	case 21: return &p21;
	case 19: return &p19;
	case 1: return &p1;
	case 27: return &p27;
	case 23: return &p23;
	case 28: return &p28;
	case 8: return &p8;
	case 26: return &p26;
	case 15: return &p15;
	case 11: return &p11;
	case 14: return &p14;
	case 17: return &p17;
	case 20: return &p20;
	case 22: return &p22;
	case 7: return &p7;
	case 30: return &p30;
	case 12: return &p12;
	case 16: return &p16;
	case 5: return &p5;
	default: return nullptr;
	}
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
		if (weapon!=id || lastTime<0)
		{
			Reset();weapon=id;lastTime=time;
		}
		// Pause/clock correction can arrive before refdef's paused flag. Rebase
		// time without dropping an already raised weapon back to the hip.
		if (time<lastTime || time-lastTime>1) { lastTime=time; return; }
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
float V_CurrentADSWorldFOV();
bool V_DrawADSCrosshair();
void V_ApplyADSBody(cl_entity_s *entity);
void V_ObserveADSSight(cl_entity_s *entity,const char *bone,const float matrix[3][4]);
