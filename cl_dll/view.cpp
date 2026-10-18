//========= Copyright ? 1996-2002, Valve LLC, All rights reserved. ============
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

// view/refresh setup functions

#include <string.h>

#include "hud.h"
#include "pm_math.h"
#include "cl_util.h"
#include "cvardef.h"
#include "usercmd.h"
#include "const.h"
#include "view_bob.h"
#include "view_effects.h"
#include "view_stairs.h"
#include "view_ads.h"
#include "weapon_spread.h"

#include "entity_state.h"
#include "cl_entity.h"
#include "ref_params.h"
#include "in_defs.h" // PITCH YAW ROLL
#include "pm_movevars.h"
#include "pm_shared.h"
#include "pm_defs.h"
#include "pm_debug.h"
#include "event_api.h"
#include "pmtrace.h"
#include "screenfade.h"
#include "shake.h"
#include "hltv.h"
#include "r_studioint.h"
#include "com_model.h"
#include "kbutton.h"
#include "input.h"
#include "com_weapons.h"
#include "triangleapi.h"

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

extern float	vJumpOrigin[3];
extern float	vJumpAngles[3];


extern engine_studio_api_t IEngineStudio;

/*
The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.
*/

extern cvar_t	*cl_forwardspeed;
extern cvar_t	*chase_active;
extern cvar_t	*scr_ofsx, *scr_ofsy, *scr_ofsz;
extern cvar_t	*cl_vsmoothing;

#define	CAM_MODE_RELAX		1
#define CAM_MODE_FOCUS		2

vec3_t v_origin, v_angles, v_cl_angles, v_sim_org, v_lastAngles, ev_punchangle;
Vector dead_viewangles( 0, 0, 0 ); // fake viewangles, for fixing
float  v_frametime, v_lastDistance;
float  v_cameraRelaxAngle	= 5.0f;
float  v_cameraFocusAngle	= 35.0f;
int	   v_cameraMode = CAM_MODE_FOCUS;
bool   v_resetCamera = 1;

cvar_t	*scr_ofsx;
cvar_t	*scr_ofsy;
cvar_t	*scr_ofsz;

cvar_t	*v_centermove;
cvar_t	*v_centerspeed;

cvar_t	*cl_bobcycle;
cvar_t	*cl_bob;
cvar_t	*cl_bobup;
cvar_t	*cl_waterdist;
cvar_t	*cl_chasedist;
cvar_t	*cl_weaponlag;
cvar_t	*cl_wpn_sway_interp;
cvar_t	*cl_wpn_sway_scale;
cvar_t	*cl_quakeguns;

// These cvars are not registered (so users can't cheat), so set the ->value field directly
// Register these cvars in V_Init() if needed for easy tweaking
cvar_t	v_iyaw_cycle		= {"v_iyaw_cycle", "2", 0, 2, NULL};
cvar_t	v_iroll_cycle		= {"v_iroll_cycle", "0.5", 0, 0.5, NULL};
cvar_t	v_ipitch_cycle		= {"v_ipitch_cycle", "1", 0, 1, NULL};
cvar_t	v_iyaw_level		= {"v_iyaw_level", "0.3", 0, 0.3, NULL};
cvar_t	v_iroll_level		= {"v_iroll_level", "0.1", 0, 0.1, NULL};
cvar_t	v_ipitch_level		= {"v_ipitch_level", "0.3", 0, 0.3, NULL};

float	v_idlescale;  // used by TFC for concussion grenade effect

//=============================================================================
/*
void V_NormalizeAngles( float *angles )
{
	int i;
	// Normalize angles
	for ( i = 0; i < 3; i++ )
	{
		if ( angles[i] > 180.0 )
		{
			angles[i] -= 360.0;
		}
		else if ( angles[i] < -180.0 )
		{
			angles[i] += 360.0;
		}
	}
}
*/
/*
===================
V_InterpolateAngles

Interpolate Euler angles.
FIXME:  Use Quaternions to avoid discontinuities
Frac is 0.0 to 1.0 ( i.e., should probably be clamped, but doesn't have to be )
===================
*/
/*
void V_InterpolateAngles( float *start, float *end, float *output, float frac )
{
	int i;
	float ang1, ang2;
	float d;

	V_NormalizeAngles( start );
	V_NormalizeAngles( end );

	for ( i = 0 ; i < 3 ; i++ )
	{
		ang1 = start[i];
		ang2 = end[i];

		d = ang2 - ang1;
		if ( d > 180 )
		{
			d -= 360;
		}
		else if ( d < -180 )
		{
			d += 360;
		}

		output[i] = ang1 + d * frac;
	}

	V_NormalizeAngles( output );
} */

// Quakeworld bob code, this fixes jitters in the mutliplayer since the clock (pparams->time) isn't quite linear
//
// s_BobState is the CS:GO bob's persistent state, kept here so it sits next to
// the classic bob it replaced rather than inside view_bob.cpp, which stays pure
// math with no engine types.
static view_bob::BobState s_BobState;
static view_effects::EffectsState s_ViewEffects;
static view_stairs::State s_ViewStairs;
static cvar_t *cl_smoothstairs;
static float s_flBobContextTime = -1.0f;
static int s_iBobContextEntity = -1;
static float s_flHudNextLog = 0.0f, s_flHudLandPeak = 0.0f;
static int s_iHudLogWeapon = -1, s_iHudLogShots = -1, s_iHudLogReload = -1, s_iHudLogClip = -1;

// viewmodel_bob_debug accumulator, see the bobdbg print in V_CalcNormalRefdef
static bool s_bDbgPrimed = false;
static bool s_bDbgGround = true;
static float s_flDbgLast = 0.0f, s_flDbgWindow = 0.0f;
static float s_flDbgMin = 0.0f, s_flDbgMax = 0.0f, s_flDbgJump = 0.0f, s_flDbgSum = 0.0f;
static float s_flDbgLastUsed = 0.0f, s_flDbgUsedJump = 0.0f;
static float s_flDbgLastVert = 0.0f, s_flDbgVertJump = 0.0f;
static float s_flDbgLastAir = 1.0f, s_flDbgAirJump = 0.0f;
static float s_flDbgLastLat = 0.0f, s_flDbgLatJump = 0.0f;
static int s_nDbgFrames = 0, s_nDbgStall = 0, s_nDbgFlips = 0;

// from cs_wpn/cs_weapons.cpp -- the held weapon, straight out of the weapon
// code this client already predicts.
extern "C" int HUD_GetBobWeaponState( int *id, int *shots_fired, int *in_reload, float *max_speed, float *accuracy, int *weapon_state, int *clip );
extern int g_iPlayerFlags;

static view_ads::State s_ADS;
static float s_ADSWorldFov=90.0f,s_ADSObservedTime=-1.0f;
static float s_ADSScreenX=0,s_ADSScreenY=0,s_ADSScreenRadius=0;
static float s_ADSFrontX=0,s_ADSFrontY=0;
static bool s_ADSFrontValid=false;
static bool s_ADSObserved=false,s_ADSDotVisible=false,s_ADSPaused=false;
static float s_ADSRelative[3]={0,0,0};

float V_ADSWorldFOV(float rawFov,float time)
{
	int id=0,shots=0,reload=0,state=0,clip=0;
	float speed=0,accuracy=0;
	HUD_GetBobWeaponState(&id,&shots,&reload,&speed,&accuracy,&state,&clip);
	const bool active=gHUD.m_Health.m_iHealth>0 && !g_iUser1 && !gEngfuncs.IsSpectateOnly() && !CL_IsThirdPerson();
	// UpdateClientData receives cl.time (cl_main.c:739), which stops advancing
	// during a pause. Retain the explicit refdef pause state as well: the engine
	// clock correction path can adjust cl.time even while rendering is paused.
	s_ADS.Update(id,active,reload!=0,rawFov,time,s_ADSPaused);
	if (!view_ads::Supported(id) || !active)
	{
		s_ADSObserved=false;
		return s_ADSWorldFov=rawFov;
	}
	static cvar_t *defaultFov=nullptr;
	if (!defaultFov) defaultFov=gEngfuncs.pfnGetCvarPointer("default_fov");
	const float hip=defaultFov?defaultFov->value:90.0f;
	return s_ADSWorldFov=s_ADS.Fov(view_ads::Clamp(hip,90,120));
}

void V_ApplyADSBody(cl_entity_s *entity)
{
	const view_ads::Profile *profile=view_ads::Get(s_ADS.weapon);
	if (!profile || profile->radius<=0 || !entity || entity!=gEngfuncs.GetViewModel() || !entity->model ||
		stricmp(entity->model->name,profile->model)) return;
	// Both converted assets have scope bodypart base=1, models={lens,blank}.
	// The opaque baked lens must open before showing the real world through it.
	// Preserve every other bodygroup; firing events commonly reset body to 2.
	entity->curstate.body=(entity->curstate.body&~1)|(s_ADS.amount>.5f?1:0);
}

void V_ObserveADSSight(cl_entity_s *entity,const char *bone,const float matrix[3][4])
{
	const view_ads::Profile *profile=view_ads::Get(s_ADS.weapon);
	if (!profile || !entity || entity!=gEngfuncs.GetViewModel() || !entity->model ||
		stricmp(entity->model->name,profile->model) || stricmp(bone,profile->bone)) return;
	float center[3],edge[3],screen[3],screenEdge[3];
	for(int axis=0;axis<3;++axis)
	{
		center[axis]=matrix[axis][3];
		for(int c=0;c<3;++c) center[axis]+=matrix[axis][c]*profile->localCenter[c];
		edge[axis]=center[axis]+matrix[axis][0]*profile->radius;
	}
	s_ADSObserved=!gEngfuncs.pTriAPI->WorldToScreen(center,screen) && !gEngfuncs.pTriAPI->WorldToScreen(edge,screenEdge);
	if (!s_ADSObserved) return;
	s_ADSScreenX=screen[0]*ScreenWidth*.5f;s_ADSScreenY=screen[1]*ScreenHeight*.5f;
	const float dx=(screenEdge[0]-screen[0])*ScreenWidth*.5f,dy=(screenEdge[1]-screen[1])*ScreenHeight*.5f;
	s_ADSScreenRadius=sqrtf(dx*dx+dy*dy);
	float front[3];
	for (int axis=0;axis<3;++axis)
	{
		front[axis]=matrix[axis][3];
		for (int c=0;c<3;++c) front[axis]+=matrix[axis][c]*profile->localFront[c];
	}
	s_ADSFrontValid=!gEngfuncs.pTriAPI->WorldToScreen(front,screen);
	s_ADSFrontX=screen[0]*ScreenWidth*.5f;s_ADSFrontY=screen[1]*ScreenHeight*.5f;
	s_ADSObservedTime=gEngfuncs.GetClientTime();
}

bool V_DrawADSCrosshair()
{
	s_ADSDotVisible=false;
	if (!view_ads::Supported(s_ADS.weapon) || s_ADS.amount<=.5f || g_iUser1 ||
		gHUD.m_Health.m_iHealth<=0 || CL_IsThirdPerson()) return false;
	if (gHUD.m_iHideHUDDisplay&(HIDEHUD_CROSSHAIR|HIDEHUD_ALL)) return true;
	const auto *profile=view_ads::Get(s_ADS.weapon);
	if (profile && profile->radius<=0) return true; // actual iron sights, no fabricated dot
	// A Source-style collimated dot stays on the aiming ray, but is visible only
	// inside the actually rendered aperture (including recoil and left hand).
	const float age=gEngfuncs.GetClientTime()-s_ADSObservedTime;
	const float radius=s_ADSScreenRadius*.85f;
	if (!s_ADSObserved || age<0 || age>.1f || radius<=0 ||
		s_ADSScreenX*s_ADSScreenX+s_ADSScreenY*s_ADSScreenY>radius*radius) return true;
	const int alpha=int(view_ads::Clamp((s_ADS.amount-.5f)*2,0,1)*230);
	const int dot=max(1,int(ScreenHeight/540.0f));
	const int x=ScreenWidth/2,y=ScreenHeight/2;
	const int red=s_ADS.weapon==8?60:255,green=s_ADS.weapon==8?255:55;
	FillRGBA(x-dot,y-dot,dot*2+1,dot*2+1,red,green,50,alpha/4);
	FillRGBA(x-dot/2,y-dot/2,max(1,dot),max(1,dot),red,green,50,alpha);
	s_ADSDotVisible=true;
	return true;
}

static void V_DumpADS()
{
	cl_entity_t *view=gEngfuncs.GetViewModel();
	gEngfuncs.Con_Printf("ads: time=%.5f id=%d amount=%.5f gain=%.5f sighted=%d raw=%.3f world=%.3f lens_valid=%d lens_px=%.3f/%.3f radius=%.3f age=%.4f body=%d relative=%.4f/%.4f/%.4f dot=%d paused=%d\n",
		s_ADS.lastTime,s_ADS.weapon,s_ADS.amount,view_ads::Gain(s_ADS.amount),int(s_ADS.sighted),g_lastFOV,s_ADSWorldFov,
		int(s_ADSObserved),s_ADSScreenX,s_ADSScreenY,s_ADSScreenRadius,gEngfuncs.GetClientTime()-s_ADSObservedTime,
		view?view->curstate.body:0,s_ADSRelative[0],s_ADSRelative[1],s_ADSRelative[2],int(s_ADSDotVisible),int(s_ADSPaused));
	gEngfuncs.Con_Printf("aimline: front_valid=%d front_px=%.3f/%.3f\n",int(s_ADSFrontValid),s_ADSFrontX,s_ADSFrontY);
}

static void V_PositionADS(ref_params_s *params,cl_entity_s *view)
{
	const view_ads::Profile *profile=view_ads::Get(s_ADS.weapon);
	if (!profile || s_ADS.amount<=0 || !view->model || stricmp(view->model->name,profile->model)) return;
	const float weight=view_ads::Gain(s_ADS.amount);
	Vector forward,right,up;
	AngleVectors(params->viewangles,forward,right,up);
	const float hand=gHUD.cl_righthand && gHUD.cl_righthand->value==0?-1.0f:1.0f;
	// Re-anchor to the finished eye, so viewsize, hip offsets and stair smoothing
	// cannot move an aimed scope away from the shooting ray. Model-space Y is
	// left; the GoldSrc right vector has the opposite sign. X remains forward.
	const float front=gHUD.viewmodel_offset_y?view_ads::Clamp(gHUD.viewmodel_offset_y->value,-8,8):0;
    // Compose the measured sight-line rotation with the camera, rather than
    // adding Euler angles (which drifts when looking up/down). Studio flips
    // entity pitch; mirror-conjugating yaw/roll keeps left-hand alignment.
    float camera[3][4],correction[3][4],aimed[3][4];
    vec3_t offset={profile->angles[0],profile->angles[1]*hand,profile->angles[2]*hand};
    AngleMatrix(params->viewangles,camera);AngleMatrix(offset,correction);
    for (int row=0;row<3;++row)
        for (int column=0;column<3;++column)
        {
            aimed[row][column]=0;
            for (int axis=0;axis<3;++axis) aimed[row][column]+=camera[row][axis]*correction[axis][column];
        }
    const float horizontal=sqrtf(aimed[0][0]*aimed[0][0]+aimed[1][0]*aimed[1][0]);
    vec3_t wantedAngles={atan2f(aimed[2][0],horizontal)*180.0f/M_PI,
        atan2f(aimed[1][0],aimed[0][0])*180.0f/M_PI,
        atan2f(aimed[2][1],aimed[2][2])*180.0f/M_PI};

	for(int axis=0;axis<3;++axis)
	{
		const float target=params->vieworg[axis]+forward[axis]*front+
			right[axis]*profile->center[1]*hand-up[axis]*profile->center[2];
		view->origin[axis]+=(target-view->origin[axis])*weight;
		float wanted=wantedAngles[axis];
		float delta=wanted-view->angles[axis];
		while(delta>180)delta-=360;
		while(delta<-180)delta+=360;
		view->angles[axis]+=delta*weight;
	}
	VectorCopy(view->angles,view->curstate.angles);
	Vector relative=view->origin-params->vieworg;
	s_ADSRelative[0]=DotProduct(relative,forward);s_ADSRelative[1]=DotProduct(relative,right);s_ADSRelative[2]=DotProduct(relative,up);
	V_ApplyADSBody(view);
}

/*
========================
V_IsSniperWeapon

CS:GO reads GetWeaponType() == WEAPONTYPE_SNIPER_RIFLE to pick between the 18x
and the 0.15x gain on the leftward accuracy shift. The GoldSrc weapon id list
has no weapon type, so this is the sniper set by hand.
========================
*/
static bool V_IsSniperWeapon( int weapon_id )
{
	switch( weapon_id )
	{
	case WEAPON_AWP:
	case WEAPON_SCOUT:
	case WEAPON_G3SG1:
	case WEAPON_SG550:
		return true;
	}

	return false;
}

float V_CalcBob ( struct ref_params_s *pparams )
{
	static	double	bobtime;
	static float	bob;
	float	cycle;
	static float	lasttime;
	vec3_t	vel;


	if ( pparams->onground == -1 ||
		 pparams->time == lasttime )
	{
		// just use old value
		return bob;
	}

	lasttime = pparams->time;

	bobtime += pparams->frametime;
	cycle = bobtime - (int)( bobtime / cl_bobcycle->value ) * cl_bobcycle->value;
	cycle /= cl_bobcycle->value;

	if ( cycle < cl_bobup->value )
	{
		cycle = M_PI * cycle / cl_bobup->value;
	}
	else
	{
		cycle = M_PI + M_PI * ( cycle - cl_bobup->value )/( 1.0 - cl_bobup->value );
	}

	// bob is proportional to simulated velocity in the xy plane
	// (don't count Z, or jumping messes it up)
	VectorCopy( pparams->simvel, vel );
	vel[2] = 0;

	bob = sqrt( vel[0] * vel[0] + vel[1] * vel[1] ) * cl_bob->value;
	bob = bob * 0.3 + bob * 0.7 * sin(cycle);
	bob = min( bob, 4.0f );
	bob = max( bob, -7.0f );
	return bob;

}

/*
===============
V_CalcRoll
Used by view and sv_user
===============
*/
float V_CalcRoll (vec3_t angles, vec3_t velocity, float rollangle, float rollspeed )
{
	float   sign;
	float   side;
	float   value;
	vec3_t  forward, right, up;

	AngleVectors ( angles, forward, right, up );

	side = DotProduct (velocity, right);
	sign = side < 0 ? -1 : 1;
	side = fabs( side );

	value = rollangle;
	if (side < rollspeed)
	{
		side = side * value / rollspeed;
	}
	else
	{
		side = value;
	}
	return side * sign;
}
#if 0
typedef struct pitchdrift_s
{
	float		pitchvel;
	int			nodrift;
	float		driftmove;
	double		laststop;
} pitchdrift_t;

static pitchdrift_t pd;

void V_StartPitchDrift( void )
{
	if ( pd.laststop == gEngfuncs.GetClientTime() )
	{
		return;		// something else is keeping it from drifting
	}

	if ( pd.nodrift || !pd.pitchvel )
	{
		pd.pitchvel = v_centerspeed->value;
		pd.nodrift = 0;
		pd.driftmove = 0;
	}
}

void V_StopPitchDrift ( void )
{
	pd.laststop = gEngfuncs.GetClientTime();
	pd.nodrift = 1;
	pd.pitchvel = 0;
}

/*
===============
V_DriftPitch

Moves the client pitch angle towards idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.
===============
*/
void V_DriftPitch ( struct ref_params_s *pparams )
{
	float		delta, move;

	if ( gEngfuncs.IsNoClipping() || !pparams->onground || pparams->demoplayback || pparams->spectator )
	{
		pd.driftmove = 0;
		pd.pitchvel = 0;
		return;
	}

	// don't count small mouse motion
	if (pd.nodrift)
	{
		if ( fabs( pparams->cmd->forwardmove ) < cl_forwardspeed->value )
			pd.driftmove = 0;
		else
			pd.driftmove += pparams->frametime;

		if ( pd.driftmove > v_centermove->value)
		{
			V_StartPitchDrift ();
		}
		return;
	}

	delta = pparams->idealpitch - pparams->cl_viewangles[PITCH];

	if (!delta)
	{
		pd.pitchvel = 0;
		return;
	}

	move = pparams->frametime * pd.pitchvel;
	pd.pitchvel += pparams->frametime * v_centerspeed->value;

	if (delta > 0)
	{
		if (move > delta)
		{
			pd.pitchvel = 0;
			move = delta;
		}
		pparams->cl_viewangles[PITCH] += move;
	}
	else if (delta < 0)
	{
		if (move > -delta)
		{
			pd.pitchvel = 0;
			move = -delta;
		}
		pparams->cl_viewangles[PITCH] -= move;
	}
}
#endif
/*
==============================================================================
						VIEW RENDERING
==============================================================================
*/

/*
=============
V_DropPunchAngle

=============
*/
void V_DropPunchAngle ( float frametime, float *ev_punchangle )
{
	float	len;

	len = VectorNormalize ( ev_punchangle );
	len -= (10.0 + len * 0.5) * frametime;
	len = max( len, 0.0 );
	VectorScale ( ev_punchangle, len, ev_punchangle );
}


/*
==================
V_CalcGunAngle
==================
*/
void V_CalcGunAngle ( struct ref_params_s *pparams )
{
	cl_entity_t *viewent;

	viewent = gEngfuncs.GetViewModel();
	if ( !viewent )
		return;

	viewent->angles[YAW]   =  pparams->viewangles[YAW]   + pparams->crosshairangle[YAW];
	viewent->angles[PITCH] = -pparams->viewangles[PITCH] + pparams->crosshairangle[PITCH] * 0.25;
	viewent->angles[ROLL]  -= v_idlescale * sin(pparams->time*v_iroll_cycle.value) * v_iroll_level.value;

	// don't apply all of the v_ipitch to prevent normally unseen parts of viewmodel from coming into view.
	viewent->angles[PITCH] -= v_idlescale * sin(pparams->time*v_ipitch_cycle.value) * (v_ipitch_level.value * 0.5);
	viewent->angles[YAW]   -= v_idlescale * sin(pparams->time*v_iyaw_cycle.value) * v_iyaw_level.value;

	if ( !( gHUD.cl_viewbob && gHUD.cl_viewbob->value ) )
	{
		VectorCopy( viewent->angles, viewent->curstate.angles );
		VectorCopy( viewent->angles, viewent->latched.prevangles );
	}
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle ( struct ref_params_s *pparams )
{
	pparams->viewangles[ROLL]  += v_idlescale * sin(pparams->time * v_iroll_cycle.value) * v_iroll_level.value;
	pparams->viewangles[PITCH] += v_idlescale * sin(pparams->time * v_ipitch_cycle.value) * v_ipitch_level.value;
	pparams->viewangles[YAW]   += v_idlescale * sin(pparams->time * v_iyaw_cycle.value) * v_iyaw_level.value;
}


/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll ( struct ref_params_s *pparams )
{
	float		side;
	cl_entity_t *viewentity;

	viewentity = gEngfuncs.GetEntityByIndex( pparams->viewentity );
	if ( !viewentity )
		return;

	side = V_CalcRoll ( viewentity->angles, pparams->simvel, pparams->movevars->rollangle, pparams->movevars->rollspeed );

	pparams->viewangles[ROLL] += side;

	if ( pparams->health <= 0 && ( pparams->viewheight[2] != 0 ) )
	{
		// only roll the view if the player is dead and the viewheight[2] is nonzero
		// this is so deadcam in multiplayer will work.
		pparams->viewangles[ROLL] = 80;	// dead view angle
		return;
	}
}

void V_SmoothInterpolateAngles( float * startAngle, float * endAngle, float * finalAngle, float degreesPerSec )
{
	float absd,frac,d,threshold;

	NormalizeAngles( startAngle );
	NormalizeAngles( endAngle );

	for ( int i = 0 ; i < 3 ; i++ )
	{
		d = endAngle[i] - startAngle[i];

		if ( d > 180.0f )
		{
			d -= 360.0f;
		}
		else if ( d < -180.0f )
		{
			d += 360.0f;
		}

		absd = fabs(d);

		if ( absd > 0.01f )
		{
			frac = degreesPerSec * v_frametime;

			threshold= degreesPerSec / 4;

			if ( absd < threshold )
			{
				float h = absd / threshold;
				h *= h;
				frac*= h;  // slow down last degrees
			}

			if ( frac >  absd )
			{
				finalAngle[i] = endAngle[i];
			}
			else
			{
				if ( d>0)
					finalAngle[i] = startAngle[i] + frac;
				else
					finalAngle[i] = startAngle[i] - frac;
			}
		}
		else
		{
			finalAngle[i] = endAngle[i];
		}

	}

	NormalizeAngles( finalAngle );
}


/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef ( struct ref_params_s *pparams )
{
	cl_entity_t	*view;
	float		old;

	// view is the weapon model (only visible from inside body)
	view = gEngfuncs.GetViewModel();

	VectorCopy ( pparams->simorg, pparams->vieworg );
	VectorCopy ( pparams->cl_viewangles, pparams->viewangles );

	view->model = NULL;

	// allways idle in intermission
	old = v_idlescale;
	v_idlescale = 1;

	V_AddIdle ( pparams );

	if ( gEngfuncs.IsSpectateOnly() )
	{
		// in HLTV we must go to 'intermission' position by ourself
		VectorCopy( gHUD.m_Spectator.m_cameraOrigin, pparams->vieworg );
		VectorCopy( gHUD.m_Spectator.m_cameraAngles, pparams->viewangles );
	}

	v_idlescale = old;

	v_cl_angles = pparams->cl_viewangles;
	v_origin = pparams->vieworg;
	v_angles = pparams->viewangles;
}

#define ORIGIN_BACKUP 64
#define ORIGIN_MASK ( ORIGIN_BACKUP - 1 )

struct viewinterp_t
{
	Vector Origins[ ORIGIN_BACKUP ];
	float OriginTime[ ORIGIN_BACKUP ];

	Vector Angles[ ORIGIN_BACKUP ];
	float AngleTime[ ORIGIN_BACKUP ];

	int CurrentOrigin;
	int CurrentAngle;
};

float GetGunOffset( cl_entity_t *e )
{
	int id = HUD_GetWeapon();

	switch( id )
	{
	case WEAPON_GLOCK18:
		return -4.55f;
	case WEAPON_USP:
		return -4.64f;
	case WEAPON_P228:
		return -4.65f;
	case WEAPON_DEAGLE:
		return -4.71f;
	case WEAPON_FIVESEVEN:
		return -4.84f;
	case WEAPON_M3:
		return -5.03f;
	case WEAPON_XM1014:
		return -5.82f;
	case WEAPON_MAC10:
		return -5.05f;
	case WEAPON_TMP:
		return -6.47f;
	case WEAPON_MP5N:
		return -5.53f;
	case WEAPON_UMP45:
		return -5.53f;
	case WEAPON_P90:
		return -4.32f;
	case WEAPON_SCOUT:
		return -5.14f;
	case WEAPON_AWP:
		return -6.02f;
	case WEAPON_FAMAS:
		return -4.84f;
	case WEAPON_AUG:
		return -6.02f;
	case WEAPON_M4A1:
		return -7.0f; //-6.47f;
	case WEAPON_SG550:
		return -7.11f;
	case WEAPON_AK47:
		return -6.79f;
	case WEAPON_G3SG1:
		return -6.19f;
	case WEAPON_SG552:
		return -5.27f;
	case WEAPON_GALIL:
		return -4.78f;
	case WEAPON_M249:
		return -5.13f;
	}
	return 0;
}

void V_CalcQuakeGuns()
{
#if 1
	cl_entity_s * vm = gEngfuncs.GetViewModel();

	if(!cl_quakeguns->value)
		return;

	if(!vm)
		return;

	float gunoffsetr = GetGunOffset(vm);
	if(gunoffsetr == 0)
		return;

	float* org = vm->origin;

	vec3_t forward, right, up;

	gEngfuncs.pfnAngleVectors(v_angles, forward, right, up);

	org[0] += forward[0] + up[0] + right[0]*gunoffsetr;
	org[1] += forward[1] + up[1] + right[1]*gunoffsetr;
	org[2] += forward[2] + up[2] + right[2]*gunoffsetr;
#endif
}


void V_ResetViewModelEffects()
{
	s_ADS.Reset();
	s_ADSObserved=false;
	s_BobState = view_bob::BobState();
	view_effects::ResetEffects( s_ViewEffects );
	s_ViewStairs = view_stairs::State();
	s_flBobContextTime = -1.0f;
	s_iBobContextEntity = -1;
	s_bDbgPrimed = false;
	s_flHudNextLog = s_flHudLandPeak = 0.0f;
	s_iHudLogWeapon = s_iHudLogShots = s_iHudLogReload = s_iHudLogClip = -1;
}

/*
==================
V_CalcRefdef

==================
*/
void V_CalcNormalRefdef ( struct ref_params_s *pparams )
{
	cl_entity_t		*ent, *view;
	int				i;
	vec3_t			angles;
	float			bob, waterOffset;
	static viewinterp_t		ViewInterp;

	static float oldz = 0;
	static float lasttime;

	vec3_t camAngles, camForward, camRight, camUp;
	cl_entity_t *pwater;

	if ( gEngfuncs.IsSpectateOnly() )
	{
		ent = gEngfuncs.GetEntityByIndex( g_iUser2 );
	}
	else
	{
		// ent is the player model ( visible when out of body )
		ent = gEngfuncs.GetLocalPlayer();
	}

	// view is the weapon model (only visible from inside body)
	view = gEngfuncs.GetViewModel();
	int iWeaponId = 0, iShotsFired = 0, iInReload = 0, iWeaponState = 0, iWeaponClip = 0;
	float flMaxSpeed = 0.0f, flWeaponAccuracy = 0.0f;
	HUD_GetBobWeaponState( &iWeaponId, &iShotsFired, &iInReload, &flMaxSpeed,
		&flWeaponAccuracy, &iWeaponState, &iWeaponClip );
	bool bVmActive = pparams->health > 0 && !g_iUser1 && !gEngfuncs.IsSpectateOnly()
		&& !CL_IsThirdPerson() && pparams->viewentity <= pparams->maxclients;
	bool bSameBobFrame = s_flBobContextTime == pparams->time;
	if( !bVmActive || s_flBobContextTime < 0.0f || pparams->time < s_flBobContextTime
		|| pparams->time - s_flBobContextTime > 1.0f || pparams->viewentity != s_iBobContextEntity )
	{
		const bool keepADS=bVmActive && s_flBobContextTime>=0 && pparams->viewentity==s_iBobContextEntity && s_ADS.weapon==iWeaponId;
		const view_ads::State savedADS=s_ADS;
		V_ResetViewModelEffects();
		// A local pause can correct the clock before its pause flag arrives.
		// Other effects keep their established reset; ADS retains its pose.
		if (keepADS) { s_ADS=savedADS;s_ADS.lastTime=pparams->time; }
		s_BobState.last_bob_time = pparams->time - pparams->frametime;
		bSameBobFrame = false;
	}
	s_flBobContextTime = pparams->time;
	s_iBobContextEntity = pparams->viewentity;
	s_ADS.Update(iWeaponId,bVmActive,iInReload!=0,g_lastFOV,pparams->time,pparams->paused!=0);
	oc2_weapon_spread::SpreadInput spreadInput = { iWeaponId, flWeaponAccuracy, iWeaponState,
		sqrtf( pparams->simvel[0] * pparams->simvel[0] + pparams->simvel[1] * pparams->simvel[1] ),
		pparams->onground != 0, ( g_iPlayerFlags & FL_DUCKING ) != 0, g_lastFOV > 0.0f ? g_lastFOV : 90.0f };
	oc2_weapon_spread::SpreadOutput spread = oc2_weapon_spread::Calculate( spreadInput );

	// CS:GO smooths stairs in BOTH directions. The GoldSrc movement code
	// only probes 2 units below the hull; inspect a full step for presentation
	// so a continuous downhill surface does not appear as repeated falls.
	view_stairs::Input stairInput;
	stairInput.time = pparams->time;
	stairInput.frame_time = pparams->frametime;
	stairInput.x = pparams->simorg[0]; stairInput.y = pparams->simorg[1];
	stairInput.z = pparams->simorg[2]; stairInput.view_height = pparams->viewheight[2];
	stairInput.vertical_speed = pparams->simvel[2];
	stairInput.grounded = pparams->onground != 0;
	stairInput.paused = pparams->paused != 0;
	stairInput.jumping = pparams->cmd && ( pparams->cmd->buttons & IN_JUMP ) != 0;
	stairInput.enabled = bVmActive && !pparams->smoothing && pparams->waterlevel < 2
		&& !gEngfuncs.IsNoClipping() && ent && ent->curstate.movetype == MOVETYPE_WALK;
	stairInput.step_size = pparams->movevars ? pparams->movevars->stepsize : 18.0f;
	if( stairInput.step_size > 18.0f ) stairInput.step_size = 18.0f;
	stairInput.static_support = false;
	stairInput.support_z = stairInput.z;
	Vector stairNormal(0,0,0);
	int stairTraceEntity=-1;
	int stairTraceFlags=0;
	const bool smoothStairs = cl_smoothstairs && cl_smoothstairs->value != 0.0f;
	if( smoothStairs && stairInput.enabled && stairInput.step_size > 0.0f )
	{
		const int hull = gEngfuncs.pEventAPI->EV_LocalPlayerDucking() ? 1 : 0;
		Vector start = pparams->simorg, end = pparams->simorg;
		end[2] += 2.0f;
		pmtrace_t upTrace = *gEngfuncs.PM_TraceLine( start, end, PM_TRACELINE_PHYSENTSONLY, hull, -1 );
		stairTraceFlags=(upTrace.startsolid?1:0)|(upTrace.allsolid?2:0);
		if( !upTrace.startsolid && !upTrace.allsolid )
		{
			// PM_TraceLine returns a shared result; copy the first trace before
			// calling it again. The API restores its temporary usehull value.
			start = upTrace.endpos;
			end = pparams->simorg; end[2] -= stairInput.step_size;
			pmtrace_t downTrace = *gEngfuncs.PM_TraceLine( start, end, PM_TRACELINE_PHYSENTSONLY, hull, -1 );
			stairNormal=downTrace.plane.normal;
			stairTraceEntity=downTrace.ent;
			stairTraceFlags|=(downTrace.startsolid?4:0)|(downTrace.allsolid?8:0);
			physent_t *ground = gEngfuncs.pEventAPI->EV_GetPhysent( downTrace.ent );
			if( downTrace.fraction > 0.0f && downTrace.fraction < 1.0f && !downTrace.startsolid
				&& !downTrace.allsolid && downTrace.plane.normal[2] >= 0.7f
				&& ground && !ground->player && ground->movetype == MOVETYPE_NONE )
			{
				stairInput.static_support = true;
				stairInput.support_z = downTrace.endpos[2];
				stairInput.normal_x = downTrace.plane.normal[0];
				stairInput.normal_y = downTrace.plane.normal[1];
				stairInput.normal_z = downTrace.plane.normal[2];
				stairInput.support_entity = downTrace.ent;
			}
		}
	}
	view_stairs::Output stairOutput;
	int stairSeamSamples=0;
	const view_stairs::SeamProbe seam=view_stairs::DescribeSeam(s_ViewStairs,stairInput);
	if( smoothStairs && seam.required )
	{
		const float dx=stairInput.x-s_ViewStairs.x,dy=stairInput.y-s_ViewStairs.y;
		const int intervals=bound(2,int(ceilf(sqrtf(dx*dx+dy*dy)*2.0f)),64);
		const int hull=gEngfuncs.pEventAPI->EV_LocalPlayerDucking()?1:0;
		stairInput.seam_verified=true;
		// A plane intersection can lie past the end of the real face. Verify
		// both sides of that join AND the intervening segment against collision
		// geometry. At most 65 probes, only when the support normal changes;
		// coplanar ramp frames retain the original two traces. PM_TraceLine
		// saves/restores usehull internally for every call.
		for( int sample=-2;sample<intervals-1 && stairInput.seam_verified;++sample )
		{
			const float fraction=sample==-2?seam.crossing*.5f:sample==-1?(seam.crossing+1)*.5f:
				float(sample+1)/intervals;
			const float support=view_stairs::SeamHeight(s_ViewStairs,stairInput,seam,fraction);
			Vector start(s_ViewStairs.x+dx*fraction,s_ViewStairs.y+dy*fraction,support+2.0f);
			Vector end=start;end[2]=support-2.0f;
			const pmtrace_t trace=*gEngfuncs.PM_TraceLine(start,end,PM_TRACELINE_PHYSENTSONLY,hull,-1);
			const physent_t *ground=gEngfuncs.pEventAPI->EV_GetPhysent(trace.ent);
			++stairSeamSamples;
			stairInput.seam_verified=trace.fraction>0 && trace.fraction<1 && !trace.startsolid && !trace.allsolid
				&& ground && !ground->player && ground->movetype==MOVETYPE_NONE
				&& view_stairs::MatchSeamSample(s_ViewStairs,stairInput,seam,fraction,trace.endpos[2],
					trace.plane.normal[0],trace.plane.normal[1],trace.plane.normal[2],trace.ent);
		}
	}
	if( smoothStairs ) stairOutput = view_stairs::Step( s_ViewStairs, stairInput );
	const bool visualGrounded = pparams->onground != 0 || stairOutput.continuous_slope;


	// transform the view offset by the model's matrix to get the offset from
	// model origin for the view
	//
	// The CS:GO bob replaces the classic one outright rather than layering on
	// top of it, and it moves the view model, not the eye -- cl_bob_camera
	// explicitly does nothing for cl_bobstyle 2. Leaving the classic bob live
	// here bounces the whole view up and down and buries the three-axis view
	// model bob underneath it. NextClient does the same by zeroing cl_bob
	// around the original CalcRefdef.
	if( gHUD.cl_bobstyle && gHUD.cl_bobstyle->value == view_bob::kStyleModern )
		bob = 0.0f;
	else
		bob = V_CalcBob ( pparams );

	// refresh position
	VectorCopy ( pparams->simorg, pparams->vieworg );
	pparams->vieworg[2] += ( bob );
	VectorAdd( pparams->vieworg, pparams->viewheight, pparams->vieworg );

	if( pparams->health <= 0 )
	{
		VectorCopy( dead_viewangles, pparams->viewangles );
	}
	else
	{
		VectorCopy ( pparams->cl_viewangles, pparams->viewangles );
	}

	gEngfuncs.V_CalcShake();
	gEngfuncs.V_ApplyShake( pparams->vieworg, pparams->viewangles, 1.0 );

	// never let view origin sit exactly on a node line, because a water plane can
	// disappear when viewed with the eye exactly on it.
	// FIXME, we send origin at 1/128 now, change this?
	// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis

	pparams->vieworg[0] += 1.0/32;
	pparams->vieworg[1] += 1.0/32;
	pparams->vieworg[2] += 1.0/32;

	// Check for problems around water, move the viewer artificially if necessary
	// -- this prevents drawing errors in GL due to waves

	waterOffset = 0;
	if ( pparams->waterlevel >= 2 )
	{
		int		i, contents, waterDist, waterEntity;
		vec3_t	point;
		waterDist = cl_waterdist->value;

		if ( pparams->hardware )
		{
			waterEntity = gEngfuncs.PM_WaterEntity( pparams->simorg );
			if ( waterEntity >= 0 && waterEntity < pparams->max_entities )
			{
				pwater = gEngfuncs.GetEntityByIndex( waterEntity );
				if ( pwater && ( pwater->model != NULL ) )
				{
					waterDist += ( pwater->curstate.scale * 16 );	// Add in wave height
				}
			}
		}
		else
		{
			waterEntity = 0;	// Don't need this in software
		}

		VectorCopy( pparams->vieworg, point );

		// Eyes are above water, make sure we're above the waves
		if ( pparams->waterlevel == 2 )
		{
			point[2] -= waterDist;
			for ( i = 0; i < waterDist; i++ )
			{
				contents = gEngfuncs.PM_PointContents( point, NULL );
				if ( contents > CONTENTS_WATER )
					break;
				point[2] += 1;
			}
			waterOffset = (point[2] + waterDist) - pparams->vieworg[2];
		}
		else
		{
			// eyes are under water.  Make sure we're far enough under
			point[2] += waterDist;

			for ( i = 0; i < waterDist; i++ )
			{
				contents = gEngfuncs.PM_PointContents( point, NULL );
				if ( contents <= CONTENTS_WATER )
					break;
				point[2] -= 1;
			}
			waterOffset = (point[2] - waterDist) - pparams->vieworg[2];
		}
	}

	pparams->vieworg[2] += waterOffset;

	V_CalcViewRoll ( pparams );

	V_AddIdle ( pparams );

	// offsets
	if( pparams->health <= 0 )
	{
		VectorCopy( dead_viewangles, angles );
	}
	else
	{
		VectorCopy( pparams->cl_viewangles, angles );
	}

	AngleVectors ( angles, pparams->forward, pparams->right, pparams->up );

	// don't allow cheats in multiplayer
	if ( pparams->maxclients <= 1 )
	{
		for ( i=0 ; i<3 ; i++ )
		{
			pparams->vieworg[i] += scr_ofsx->value*pparams->forward[i] + scr_ofsy->value*pparams->right[i] + scr_ofsz->value*pparams->up[i];
		}
	}

	// Treating cam_ofs[2] as the distance
	/*if( CL_IsThirdPerson() )
	{
		vec3_t ofs;

		ofs[0] = ofs[1] = ofs[2] = 0.0;

		CL_CameraOffset( (float *)&ofs );

		VectorCopy( ofs, camAngles );
		camAngles[ ROLL ]	= 0;

		AngleVectors( camAngles, camForward, camRight, camUp );

		for ( i = 0; i < 3; i++ )
		{
			pparams->vieworg[ i ] += -ofs[2] * camForward[ i ];
		}
	}*/

	// Give gun our viewangles
	if( pparams->health <= 0 )
	{
		VectorCopy( dead_viewangles, view->angles );
	}
	else
	{
		VectorCopy ( pparams->cl_viewangles, view->angles );
	}

	// set up gun position
	V_CalcGunAngle ( pparams );

	// Use predicted origin as view origin.
	VectorCopy ( pparams->simorg, view->origin );
	view->origin[2] += ( waterOffset );
	VectorAdd( view->origin, pparams->viewheight, view->origin );

	// Let the viewmodel shake at about 10% of the amplitude
	gEngfuncs.V_ApplyShake( view->origin, view->angles, 0.9 );

	// ---- view bob
	//
	// Two paths share this spot. The classic Quake bob above produced one scalar
	// that drove a forward push, a lift and a sway; that is what cl_bobstyle 0
	// and 1 still get, untouched. cl_bobstyle 2 is the CS:GO bob and works on a
	// three-axis offset instead -- it has no sway at all, and cl_bob_camera does
	// not apply to it because it moves the view model, not the eye.
	// The classic expressions are kept verbatim, including their untyped double
	// literals. "bob * 0.4" promotes to double before it comes back down to a
	// float on assignment, and writing 0.4f instead computes in single
	// precision and lands on a slightly different value. The bob is small
	// enough that nobody would notice frame to frame, but the current look was
	// tuned against the double path and this keeps it identical.
	float bobForward = bob * 0.4;
	float bobUp      = bob;
	float bobSide    = 0.0f;
	float bobYaw     = - bob * 0.5;
	float bobRoll    = - bob * 1;
	float bobPitch   = - bob * 0.3;

if( gHUD.cl_bobstyle && gHUD.cl_bobstyle->value == view_bob::kStyleModern )
	{
		view_bob::BobParams params;

		params.bob_cycle   = cl_bobcycle->value;
		params.bob_up      = cl_bobup->value;
		params.amt_vert    = gHUD.cl_bobamt_vert ? gHUD.cl_bobamt_vert->value : 0.35f;
		params.amt_lat     = gHUD.cl_bobamt_lat  ? gHUD.cl_bobamt_lat->value  : 0.86f;
		params.lower_amt   = gHUD.cl_bob_lower_amt ? gHUD.cl_bob_lower_amt->value : 8.0f;
		params.air_scale   = gHUD.viewmodel_bob_air_scale ? gHUD.viewmodel_bob_air_scale->value : 0.8f;
		params.air_speed   = gHUD.viewmodel_bob_air_speed ? gHUD.viewmodel_bob_air_speed->value : 25.0f;
		params.speed_smooth = gHUD.viewmodel_bob_speed_smooth ? gHUD.viewmodel_bob_speed_smooth->value : 15.0f;
		params.ground_delay = gHUD.viewmodel_bob_ground_delay ? gHUD.viewmodel_bob_ground_delay->value : 0.05f;
		params.speed_follow = gHUD.viewmodel_bob_speed_follow ? gHUD.viewmodel_bob_speed_follow->value : 200.0f;
		params.shift_left  = gHUD.viewmodel_shift_left_amt  ? gHUD.viewmodel_shift_left_amt->value  : 1.5f;
		params.shift_right = gHUD.viewmodel_shift_right_amt ? gHUD.viewmodel_shift_right_amt->value : 0.75f;

		// The held weapon, out of the weapon code this client already predicts.
		// Falls back to CS:GO's no-weapon branch when nothing is in hand.
		// Snapshot was obtained once above and shared with diagnostics/effects.

		// The line below prints on the first frame of the map, when nothing is
		// held yet. Fire once more when a weapon shows up, so the weapon path is
		// proven reachable rather than assumed -- "did not show up in 30 s" is
		// not evidence that it does not exist.
		static bool s_bLoggedWeapon = false;

		if( !s_bLoggedWeapon && flMaxSpeed > 0.0f )
		{
			s_bLoggedWeapon = true;
			gEngfuncs.Con_Printf( "viewbob: weapon path live (id=%d maxspeed=%g "
				"shots=%d reload=%d sniper=%d)\n",
				iWeaponId, flMaxSpeed, iShotsFired, iInReload,
				(int)V_IsSniperWeapon( iWeaponId ) );
		}

		view_bob::BobInput in;

		in.vx               = pparams->simvel[0];
		in.vy               = pparams->simvel[1];
		in.onground         = visualGrounded;
		in.ducking          = ( g_iPlayerFlags & FL_DUCKING ) != 0;
		in.in_reload        = iInReload != 0;
		in.is_elite         = ( iWeaponId == WEAPON_ELITE );
		in.sniper           = V_IsSniperWeapon( iWeaponId );
		in.zoomed           = g_lastFOV > 0.0f && g_lastFOV < 90.0f;
		in.run_bob_scale    = 1.0f;
		in.weapon_max_speed = flMaxSpeed;
		in.weapon_id = iWeaponId;

		// GoldSrc adapter: native predicted spread relative to this weapon's
		// default standing spread in the same scope/silencer/burst mode.
		// The Valve helper's 0.082 cap now limits the spread increase. This
		// deliberately does not claim CS:GO schema values.
		in.inaccuracy_stand  = 0.0f;
		in.inaccuracy_crouch = spread.crouch - spread.stand;
		in.inaccuracy        = spread.current - spread.stand;

		view_bob::BobOutput offsets = view_bob::StepBob( s_BobState, params, in,
			pparams->time, pparams->paused || bSameBobFrame ? 0.0f : pparams->frametime );

		// ---- diagnostics, off by default ----
		//
		// The slope stutter is not the airborne amplitude snap: at these
		// amplitudes that step is under 0.1 units. Rather than guess again,
		// viewmodel_bob_debug 1 prints one line a second with the numbers that
		// can tell a rate change from a real phase stall.
		if( gHUD.viewmodel_bob_debug && gHUD.viewmodel_bob_debug->value != 0.0f )
		{
			float flSpd = sqrtf( in.vx * in.vx + in.vy * in.vy );
			int iWeaponIdDbg = in.weapon_id;
			float flMaxSpeedDbg = in.weapon_max_speed;

			if( !s_bDbgPrimed )
			{
				s_bDbgPrimed = true;
				s_flDbgLast = flSpd;
				s_flDbgWindow = pparams->time;
				s_flDbgMin = flSpd;   // seeding with 0 pinned the minimum there forever
				s_flDbgMax = flSpd;
			}

			float flJump = fabsf( flSpd - s_flDbgLast );
			s_flDbgLast = flSpd;

			// what the bob actually consumes, after the low pass and the
			// CS:GO follow clamp -- dmax on the raw value alone says nothing
			// about whether the fix worked
			float flUsed = s_BobState.last_speed;
			float flUsedJump = fabsf( flUsed - s_flDbgLastUsed );
			s_flDbgLastUsed = flUsed;

			// what the eye actually sees, and whether the onground debounce
			// held -- flips counts the raw flag, which grows whether or not
			// the amplitude reacts to it
			float flVertJump = fabsf( offsets.vertical - s_flDbgLastVert );
			s_flDbgLastVert = offsets.vertical;

			// the lateral is what "no sway" actually looks like, and it is
			// gated on the weapon being present, so log the weapon too
			float flLatJump = fabsf( offsets.lateral - s_flDbgLastLat );
			s_flDbgLastLat = offsets.lateral;

			float flAirJump = fabsf( s_BobState.air_mix - s_flDbgLastAir );
			s_flDbgLastAir = s_BobState.air_mix;

			if( flSpd < s_flDbgMin ) s_flDbgMin = flSpd;
			if( flSpd > s_flDbgMax ) s_flDbgMax = flSpd;
			if( flJump > s_flDbgJump ) s_flDbgJump = flJump;
			if( flUsedJump > s_flDbgUsedJump ) s_flDbgUsedJump = flUsedJump;
			if( flVertJump > s_flDbgVertJump ) s_flDbgVertJump = flVertJump;
			if( flLatJump > s_flDbgLatJump ) s_flDbgLatJump = flLatJump;
			if( flAirJump > s_flDbgAirJump ) s_flDbgAirJump = flAirJump;
			s_flDbgSum += flSpd;
			s_nDbgFrames++;

			if( flSpd < 40.0f ) s_nDbgStall++;

			if( in.onground != s_bDbgGround )
			{
				s_nDbgFlips++;
				s_bDbgGround = in.onground;
			}

			if( pparams->time >= s_flDbgWindow + 1.0f )
			{
				gEngfuncs.Con_Printf( "bobdbg: spd %.0f..%.0f avg %.0f | raw_d %.0f used_d %.0f | "
					"SEEN vert_d %.2f lat_d %.2f | "
					"wp %d/%.0f air %.2f | flips %d | amp %.2f/%.2f\n",
					s_flDbgMin, s_flDbgMax,
					s_nDbgFrames ? s_flDbgSum / s_nDbgFrames : 0.0f,
					s_flDbgJump, s_flDbgUsedJump,
					s_flDbgVertJump, s_flDbgLatJump,
					iWeaponIdDbg, flMaxSpeedDbg, s_BobState.air_mix,
					s_nDbgFlips,
					params.amt_vert, params.amt_lat );

				s_flDbgWindow = pparams->time;
				s_flDbgMin = s_flDbgMax = s_flDbgJump = s_flDbgSum = 0.0f;
				s_flDbgUsedJump = s_flDbgVertJump = s_flDbgAirJump = 0.0f;
				s_flDbgLatJump = 0.0f;
				s_nDbgFrames = s_nDbgStall = 0;
			}
		}

		// one-shot so the log says which path actually ran
		static bool s_bLoggedStyle = false;
		if( !s_bLoggedStyle )
		{
			s_bLoggedStyle = true;
			gEngfuncs.Con_Printf( "viewbob: cl_bobstyle=%d, CS:GO bob active "
				"(cycle=%g up=%g vert=%g lat=%g lower=%g shift=%g/%g)\n",
				(int)gHUD.cl_bobstyle->value, params.bob_cycle, params.bob_up,
				params.amt_vert, params.amt_lat, params.lower_amt,
				params.shift_left, params.shift_right );
		}

		// AddViewModelBobHelper(): the two bob scalars reach the view model
		// through these. viewmodel_bob_scale trims all six together; 1.0 leaves
		// CS:GO's own 0.4 / 0.1 / 0.2 untouched.
		float flBobScale = gHUD.viewmodel_bob_scale ? gHUD.viewmodel_bob_scale->value : 1.0f;

		bobForward = offsets.vertical * 0.4f * flBobScale;
		bobUp      = offsets.vertical * 0.1f * flBobScale;
		bobSide    = offsets.lateral  * 0.2f * flBobScale;
		bobRoll    = offsets.vertical * 0.5f * flBobScale;
		bobPitch   = -offsets.vertical * 0.4f * flBobScale;
		bobYaw     = -offsets.lateral  * 0.3f * flBobScale;

		// Safety net. A view model pulled far enough forward crosses the near
		// plane and the weapon model fills the screen in flat grey, which reads
		// as a hang rather than as a bug. CS:GO cannot hit this because it
		// renders at 1/64 scale; a GoldSrc view model is a few units from the
		// eye, so cl_bob_lower_amt can do it in one step. Nothing here should
		// ever bind -- if it does, the amplitude cvars are wrong, not the clamp.
		const float kMaxBobOffset = 3.0f;

		bobForward = bound( bobForward, -kMaxBobOffset, kMaxBobOffset );
		bobUp      = bound( bobUp,      -kMaxBobOffset, kMaxBobOffset );
		bobSide    = bound( bobSide,    -kMaxBobOffset, kMaxBobOffset );
		bobRoll    = bound( bobRoll,    -30.0f, 30.0f );
		bobPitch   = bound( bobPitch,   -30.0f, 30.0f );
		bobYaw     = bound( bobYaw,     -30.0f, 30.0f );
	}
	for ( i = 0; i < 3; i++ )
	{
		view->origin[ i ] += bobForward * pparams->forward[ i ];
		view->origin[ i ] += bobSide * pparams->right[ i ];
	}
	view->origin[2] += bobUp;

	// throw in a little tilt.
	view->angles[YAW]   += bobYaw;
	view->angles[ROLL]  += bobRoll;
	view->angles[PITCH] += bobPitch;

	// pushing the view origin down off of the same X/Z plane as the ent's origin will give the
	// gun a very nice 'shifting' effect when the player looks up/down. If there is a problem
	// with view model distortion, this may be a cause. (SJB).
	view->origin[2] -= 1;

	// fudge position around to keep amount of weapon visible
	// roughly equal with different FOV
	if (pparams->viewsize == 110)
	{
		view->origin[2] += 1;
	}
	else if (pparams->viewsize == 100)
	{
		view->origin[2] += 2;
	}
	else if (pparams->viewsize == 90)
	{
		view->origin[2] += 1;
	}
	else if (pparams->viewsize == 80)
	{
		view->origin[2] += 0.5;
	}

	// Don't allow viewmodel, if we are in sniper scope
	if( gHUD.m_iFOV <= 40 )
		view->model = NULL;

	// Add in the punchangle, if any
	pparams->viewangles = pparams->viewangles + pparams->punchangle;

#if 0
	// Include client side punch, too
	VectorAdd ( pparams->viewangles, (float *)&ev_punchangle, pparams->viewangles);

	V_DropPunchAngle ( pparams->frametime, (float *)&ev_punchangle );
#endif
	if( smoothStairs )
	{
		// One common translation keeps the weapon's position relative to the
		// eye unchanged, just as Source passes its finished eye to the model.
		pparams->vieworg[2] += stairOutput.offset;
		view->origin[2] += stairOutput.offset;
		oldz = pparams->simorg[2];
	}
	else
	{
		s_ViewStairs = view_stairs::State();
		if ( !pparams->smoothing && pparams->onground && pparams->simorg[2] - oldz > 0)
		{
			float steptime;

			steptime = pparams->time - lasttime;
			if (steptime < 0)
				//FIXME		I_Error ("steptime < 0");
				steptime = 0;

			oldz += steptime * 150;
			if (oldz > pparams->simorg[2])
				oldz = pparams->simorg[2];
			if (pparams->simorg[2] - oldz > 18)
				oldz = pparams->simorg[2]- 18;
			pparams->vieworg[2] += oldz - pparams->simorg[2];
			view->origin[2] += oldz - pparams->simorg[2];
		}
		else
		{
			oldz = pparams->simorg[2];
		}
		stairOutput.offset = oldz - pparams->simorg[2];
		stairOutput.target_z = pparams->simorg[2];
	}

	static Vector lastorg;
	Vector delta;

	delta = pparams->simorg - lastorg;

	if( delta.x || delta.y || delta.z )
	{
		ViewInterp.Origins[ ViewInterp.CurrentOrigin & ORIGIN_MASK ] = pparams->simorg;
		ViewInterp.OriginTime[ ViewInterp.CurrentOrigin & ORIGIN_MASK ] = pparams->time;
		ViewInterp.CurrentOrigin++;

		lastorg = pparams->simorg;
	}

	V_CalcQuakeGuns();
	view_effects::EffectsParams effectParams;
	effectParams.sway_interp = cl_wpn_sway_interp->value;
	effectParams.sway_scale = cl_wpn_sway_scale->value;
	effectParams.land_scale = gHUD.viewmodel_lag_scale_land->value;
	effectParams.roll_angle = gHUD.viewmodel_rollangle->value;
	view_effects::EffectsInput effectInput;
	effectInput.time = pparams->time;
	effectInput.frame_time = pparams->frametime;
	effectInput.vertical_speed = pparams->simvel[2];
	effectInput.grounded = visualGrounded;
	effectInput.active = bVmActive && view->model != NULL;
	effectInput.paused = pparams->paused != 0;
	effectInput.left_hand = gHUD.cl_righthand && gHUD.cl_righthand->value == 0.0f;
	effectInput.view_entity = pparams->viewentity;
	effectInput.weapon_id = iWeaponId;
	effectInput.model_index = view->curstate.modelindex;
	for( i = 0; i < 3; ++i )
	{
		effectInput.view_angles[i] = view->angles[i];
		effectInput.mouse_angles[i] = pparams->cl_viewangles[i];
	}
	view_effects::EffectsOutput effects = view_effects::StepEffects( s_ViewEffects, effectParams, effectInput );
	view_effects::Vec3 fixed = view_effects::FixedOffset( effectInput.mouse_angles,
		gHUD.viewmodel_offset_x->value, gHUD.viewmodel_offset_y->value,
		gHUD.viewmodel_offset_z->value, effectInput.left_hand );
	if( effectInput.active )
	{
		for( i = 0; i < 3; ++i ) view->origin[i] += effects.sway_offset[i] + fixed[i];
		view->origin[2] -= effects.land_dip;
		view->angles[ROLL] += effects.mouse_roll;
	}
	if( gHUD.viewmodel_bob_debug && gHUD.viewmodel_bob_debug->value && bVmActive && !pparams->paused )
	{
		if( effects.land_dip > s_flHudLandPeak ) s_flHudLandPeak = effects.land_dip;
		if( pparams->time >= s_flHudNextLog || iWeaponId != s_iHudLogWeapon
			|| iShotsFired != s_iHudLogShots || iInReload != s_iHudLogReload || iWeaponClip != s_iHudLogClip )
		{
			gEngfuncs.Con_Printf( "hudphase2: t %.3f wp %d shots %d reload %d clip %d acc %.5f "
				"spread %.5f/%.5f/%.5f | land_peak %.3f roll %.3f sway %.3f %.3f %.3f\n",
				pparams->time, iWeaponId, iShotsFired, iInReload, iWeaponClip, flWeaponAccuracy,
				spread.stand, spread.crouch, spread.current, s_flHudLandPeak, effects.mouse_roll,
				effects.sway_offset.x, effects.sway_offset.y, effects.sway_offset.z );
			s_flHudNextLog = pparams->time + 1.0f;
			s_flHudLandPeak = 0.0f;
			s_iHudLogWeapon = iWeaponId; s_iHudLogShots = iShotsFired;
			s_iHudLogReload = iInReload; s_iHudLogClip = iWeaponClip;
		}
	}

	// Smooth out whole view in multiplayer when on trains, lifts
	if ( cl_vsmoothing && cl_vsmoothing->value && ( pparams->smoothing && ( pparams->maxclients > 1 ) ) )
	{
		int foundidx;
		int i;
		float t;

		if ( cl_vsmoothing->value < 0.0 )
			gEngfuncs.Cvar_SetValue( "cl_vsmoothing", 0.0 );

		t = pparams->time - cl_vsmoothing->value;

		for ( i = 1; i < ORIGIN_MASK; i++ )
		{
			foundidx = ViewInterp.CurrentOrigin - 1 - i;
			if ( ViewInterp.OriginTime[ foundidx & ORIGIN_MASK ] <= t )
				break;
		}

		if ( i < ORIGIN_MASK && ViewInterp.OriginTime[ foundidx & ORIGIN_MASK ] != 0.0 )
		{
			// Interpolate
			vec3_t delta;
			double frac;
			double dt;
			vec3_t neworg;

			dt = ViewInterp.OriginTime[ (foundidx + 1) & ORIGIN_MASK ] - ViewInterp.OriginTime[ foundidx & ORIGIN_MASK ];
			if ( dt > 0.0 )
			{
				frac = ( t - ViewInterp.OriginTime[ foundidx & ORIGIN_MASK] ) / dt;
				frac = min( 1.0, frac );
				VectorSubtract( ViewInterp.Origins[ ( foundidx + 1 ) & ORIGIN_MASK ], ViewInterp.Origins[ foundidx & ORIGIN_MASK ], delta );
				VectorMA( ViewInterp.Origins[ foundidx & ORIGIN_MASK ], frac, delta, neworg );

				// Don't interpolate large changes
				if ( Length( delta ) < 64 )
				{
					VectorSubtract( neworg, pparams->simorg, delta );

					VectorAdd( pparams->simorg, delta, pparams->simorg );
					VectorAdd( pparams->vieworg, delta, pparams->vieworg );
					VectorAdd( view->origin, delta, view->origin );

				}
			}
		}
	}


	// Store off v_angles before munging for third person
	v_angles = pparams->viewangles;
	v_lastAngles = pparams->viewangles;
	//	v_cl_angles = pparams->cl_viewangles;	// keep old user mouse angles !
	if ( CL_IsThirdPerson() )
	{
		VectorCopy( camAngles, pparams->viewangles);
		float pitch = camAngles[ 0 ];

		// Normalize angles
		if ( pitch > 180 )
			pitch -= 360.0;
		else if ( pitch < -180 )
			pitch += 360;

		// Player pitch is inverted
		pitch /= -3.0;

		// Slam local player's pitch value
		ent->angles[ 0 ] = pitch;
		ent->curstate.angles[ 0 ] = pitch;
		ent->prevstate.angles[ 0 ] = pitch;
		ent->latched.prevangles[ 0 ] = pitch;
	}

	// override all previous settings if the viewent isn't the client
	if ( pparams->viewentity > pparams->maxclients )
	{
		cl_entity_t *viewentity;
		viewentity = gEngfuncs.GetEntityByIndex( pparams->viewentity );
		if ( viewentity )
		{
			VectorCopy( viewentity->origin, pparams->vieworg );
			VectorCopy( viewentity->angles, pparams->viewangles );

			// Store off overridden viewangles
			v_angles = pparams->viewangles;
		}
	}

	V_PositionADS(pparams,view);
	if ( gHUD.cl_viewbob && gHUD.cl_viewbob->value )
	{
		VectorCopy( view->origin, view->curstate.origin );
		VectorCopy( view->origin, view->latched.prevorigin );
		VectorCopy( view->angles, view->curstate.angles );
		VectorCopy( view->angles, view->latched.prevangles );
	}

	if( gHUD.viewmodel_bob_debug && gHUD.viewmodel_bob_debug->value >= 2.0f
		&& bVmActive && !pparams->paused )
		gEngfuncs.Con_Printf( "stairsdbg: t %.5f mode %d pos %.3f %.3f %.3f vz %.3f "
			"ground %d support %d track %d target %.3f offset %.3f eye %.3f rel %.3f "
			"vh %.3f move %d jump %d normal %.6f %.6f %.6f probe %.6f ent %d "
			"punch %.4f %.4f %.4f angles %.4f %.4f %.4f traceflags %d slope %d visualground %d landdip %.6f planedelta %.6f seam_samples %d seam_ok %d\n", pparams->time, (int)smoothStairs,
			pparams->simorg[0], pparams->simorg[1], pparams->simorg[2], pparams->simvel[2],
			(int)stairInput.grounded, (int)stairInput.static_support, (int)stairOutput.supported,
			stairOutput.target_z, stairOutput.offset, pparams->vieworg[2],
			view->origin[2] - pparams->vieworg[2], pparams->viewheight[2],
			ent ? ent->curstate.movetype : -1, (int)stairInput.jumping,
			stairNormal[0],stairNormal[1],stairNormal[2],stairInput.support_z,stairTraceEntity,
			pparams->punchangle[0],pparams->punchangle[1],pparams->punchangle[2],
			pparams->viewangles[0],pparams->viewangles[1],pparams->viewangles[2],stairTraceFlags,
			int(stairOutput.continuous_slope),int(visualGrounded),effects.land_dip,stairOutput.plane_delta,
			stairSeamSamples,int(stairInput.seam_verified) );
	lasttime = pparams->time;

	v_origin = pparams->vieworg;
}

// Get the origin of the Observer based around the target's position and angles
void V_GetChaseOrigin( float * angles, float * origin, float distance, float * returnvec )
{
	Vector vecStart, vecEnd;
	pmtrace_t *trace;
	int maxLoops = 8;

	Vector forward, right, up;

	// trace back from the target using the player's view angles
	AngleVectors( angles, forward, right, up );
	forward = -forward;

	vecStart = origin;
	vecEnd = vecStart + forward * distance;

	int ignoreent = -1;	// first, ignore no entity
	cl_entity_t *ent = NULL;

	while( maxLoops > 0 )
	{
		trace = gEngfuncs.PM_TraceLine( vecStart, vecEnd, PM_TRACELINE_PHYSENTSONLY, 2, ignoreent );

		if( trace->ent <= 0 )
			break; // we hit the world or nothing, stop trace

		ent = gEngfuncs.GetEntityByIndex( PM_GetPhysEntInfo( trace->ent ));
		if( ent == NULL ) break;

		// hit non-player solid BSP, stop here
		if( ent->curstate.solid == SOLID_BSP && !ent->player )
			break;

		// if close enought to end pos, stop, otherwise continue trace
		if( trace->fraction < 1.0f )
		{
			break;
		}
		else
		{
			ignoreent = trace->ent;	// ignore last hit entity
			vecStart = trace->endpos;
		}
		maxLoops--;
	}

	VectorMA( trace->endpos, 8, trace->plane.normal, returnvec );
	v_lastDistance = (trace->endpos - Vector(origin)).Length();	// real distance without offset
}

void V_GetDeathCam(cl_entity_t * ent1, cl_entity_t * ent2, float * angle, float * origin)
{
	float newAngle[3];
	float newOrigin[3];
	static vec3_t nonDestructedOrigin;
	float distance = 168.0f;

	v_lastDistance += v_frametime * 96.0f;	// move unit per seconds back

	if ( v_resetCamera )
		v_lastDistance = 64.0f;

	if ( distance > v_lastDistance )
		distance = v_lastDistance;

	if (ent1->origin.x == 0 && ent1->origin.y == 0 && ent1->origin.z == 0)
		nonDestructedOrigin.CopyToArray(newOrigin);
	else
	{
		nonDestructedOrigin = ent1->origin;
		VectorCopy(ent1->origin, newOrigin);
	}

	if ( ent1->player )
		newOrigin[2]+= 17; // head level of living player

	// get new angle towards second target
	if ( ent2 )
	{
		VectorSubtract(ent2->origin, nonDestructedOrigin, newAngle);
		VectorAngles( newAngle, newAngle );
		newAngle[0] = -newAngle[0];
	}
	else
	{
		// if no second target is given, look down to dead player
		newAngle[0] = 90.0f;
		newAngle[1] = 0.0f;
		newAngle[2] = 0;
	}

	// and smooth view
	V_SmoothInterpolateAngles( v_lastAngles, newAngle, angle, 120.0f );

	V_GetChaseOrigin( angle, newOrigin, distance, origin );

	VectorCopy(angle, v_lastAngles);
}

void V_GetSingleTargetCam(cl_entity_t * ent1, float * angle, float * origin)
{
	float newAngle[3]; float newOrigin[3];

	int flags 	   = gHUD.m_Spectator.m_iObserverFlags;

	// see is target is a dead player
	bool deadPlayer = ent1->player && (ent1->curstate.solid == SOLID_NOT);

	float dfactor   = ( flags & DRC_FLAG_DRAMATIC )? -1.0f : 1.0f;

	float distance = 112.0f + ( 16.0f * dfactor ); // get close if dramatic;

	// go away in final scenes or if player just died
	if ( flags & DRC_FLAG_FINAL )
		distance*=2.0f;
	else if ( deadPlayer )
		distance*=1.5f;

	// let v_lastDistance float smoothly away
	v_lastDistance+= v_frametime * 32.0f;	// move unit per seconds back

	if ( distance > v_lastDistance )
		distance = v_lastDistance;

	VectorCopy(ent1->origin, newOrigin);

	if ( ent1->player )
	{
		if ( deadPlayer )
			newOrigin[2]+= 2;	//laying on ground
		else
			newOrigin[2]+= 17; // head level of living player

	}
	else
		newOrigin[2]+= 8;	// object, tricky, must be above bomb in CS

	// we have no second target, choose view direction based on
	// show front of primary target
	VectorCopy(ent1->angles, newAngle);

	// show dead players from front, normal players back
	if ( flags & DRC_FLAG_FACEPLAYER )
		newAngle[1]+= 180.0f;


	newAngle[0]+= 12.5f * dfactor; // lower angle if dramatic

	// if final scene (bomb), show from real high pos
	if ( flags & DRC_FLAG_FINAL )
		newAngle[0] = 22.5f;

	// choose side of object/player
	if ( flags & DRC_FLAG_SIDE )
		newAngle[1]+=22.5f;
	else
		newAngle[1]-=22.5f;

	V_SmoothInterpolateAngles( v_lastAngles, newAngle, angle, 120.0f );

	// HACK, if player is dead don't clip against his dead body, can't check this
	V_GetChaseOrigin( angle, newOrigin, distance, origin );
}

float MaxAngleBetweenAngles(  float * a1, float * a2 )
{
	float d, maxd = 0.0f;

	NormalizeAngles( a1 );
	NormalizeAngles( a2 );

	for ( int i = 0 ; i < 3 ; i++ )
	{
		d = a2[i] - a1[i];
		if ( d > 180 )
		{
			d -= 360;
		}
		else if ( d < -180 )
		{
			d += 360;
		}

		d = fabs(d);

		if ( d > maxd )
			maxd=d;
	}

	return maxd;
}

void V_GetDoubleTargetsCam(cl_entity_t	 * ent1, cl_entity_t * ent2,float * angle, float * origin)
{
	float newAngle[3]; float newOrigin[3]; float tempVec[3];

	int flags 	   = gHUD.m_Spectator.m_iObserverFlags;

	float dfactor   = ( flags & DRC_FLAG_DRAMATIC )? -1.0f : 1.0f;

	float distance = 112.0f + ( 16.0f * dfactor ); // get close if dramatic;

	// go away in final scenes or if player just died
	if ( flags & DRC_FLAG_FINAL )
		distance*=2.0f;

	// let v_lastDistance float smoothly away
	v_lastDistance+= v_frametime * 32.0f;	// move unit per seconds back

	if ( distance > v_lastDistance )
		distance = v_lastDistance;

	VectorCopy(ent1->origin, newOrigin);

	if ( ent1->player )
		newOrigin[2]+= 17; // head level of living player
	else
		newOrigin[2]+= 8;	// object, tricky, must be above bomb in CS

	// get new angle towards second target
	VectorSubtract( ent2->origin, ent1->origin, newAngle );

	VectorAngles( newAngle, newAngle );
	newAngle[0] = -newAngle[0];

	// set angle different in Dramtaic scenes
	newAngle[0]+= 12.5f * dfactor; // lower angle if dramatic

	if ( flags & DRC_FLAG_SIDE )
		newAngle[1]+=22.5f;
	else
		newAngle[1]-=22.5f;

	float d = MaxAngleBetweenAngles( v_lastAngles, newAngle );

	if ( ( d < v_cameraFocusAngle) && ( v_cameraMode == CAM_MODE_RELAX ) )
	{
		// difference is to small and we are in relax camera mode, keep viewangles
		VectorCopy(v_lastAngles, newAngle );
	}
	else if ( (d < v_cameraRelaxAngle) && (v_cameraMode == CAM_MODE_FOCUS) )
	{
		// we catched up with our target, relax again
		v_cameraMode = CAM_MODE_RELAX;
	}
	else
	{
		// target move too far away, focus camera again
		v_cameraMode = CAM_MODE_FOCUS;
	}

	// and smooth view, if not a scene cut
	if ( v_resetCamera || (v_cameraMode == CAM_MODE_RELAX) )
	{
		VectorCopy( newAngle, angle );
	}
	else
	{
		V_SmoothInterpolateAngles( v_lastAngles, newAngle, angle, 180.0f );
	}

	V_GetChaseOrigin( newAngle, newOrigin, distance, origin );

	// move position up, if very close at target
	if ( v_lastDistance < 64.0f )
		origin[2]+= 16.0f*( 1.0f - (v_lastDistance / 64.0f ) );

	// calculate angle to second target
	VectorSubtract( ent2->origin, origin, tempVec );
	VectorAngles( tempVec, tempVec );
	tempVec[0] = -tempVec[0];

	/* take middle between two viewangles
	InterpolateAngles( newAngle, tempVec, newAngle, 0.5f); */



}

void V_GetDirectedChasePosition(cl_entity_t	 * ent1, cl_entity_t * ent2,float * angle, float * origin)
{

	if ( v_resetCamera )
	{
		v_lastDistance = 4096.0f;
		// v_cameraMode = CAM_MODE_FOCUS;
	}

	if ( ( ent2 == (cl_entity_t*)0xFFFFFFFF ) || ( ent1->player && (ent1->curstate.solid == SOLID_NOT) ) )
	{
		// we have no second target or player just died
		V_GetSingleTargetCam(ent1, angle, origin);
	}
	else if ( ent2 )
	{
		// keep both target in view
		V_GetDoubleTargetsCam( ent1, ent2, angle, origin );
	}
	else
	{
		// second target disappeard somehow (dead)

		// keep last good viewangle
		float newOrigin[3];

		int flags 	   = gHUD.m_Spectator.m_iObserverFlags;

		float dfactor   = ( flags & DRC_FLAG_DRAMATIC )? -1.0f : 1.0f;

		float distance = 112.0f + ( 16.0f * dfactor ); // get close if dramatic;

		// go away in final scenes or if player just died
		if ( flags & DRC_FLAG_FINAL )
			distance*=2.0f;

		// let v_lastDistance float smoothly away
		v_lastDistance+= v_frametime * 32.0f;	// move unit per seconds back

		if ( distance > v_lastDistance )
			distance = v_lastDistance;

		VectorCopy(ent1->origin, newOrigin);

		if ( ent1->player )
			newOrigin[2]+= 17; // head level of living player
		else
			newOrigin[2]+= 8;	// object, tricky, must be above bomb in CS

		V_GetChaseOrigin( angle, newOrigin, distance, origin );
	}

	VectorCopy(angle, v_lastAngles);
}

void V_GetChasePos(int target, float * cl_angles, float * origin, float * angles)
{
	cl_entity_t	 *	ent = NULL;
	cl_entity_t *local = gEngfuncs.GetLocalPlayer();

	if ( target )
	{
		ent = gEngfuncs.GetEntityByIndex( target );
	}

	if (!ent)
	{
		// just copy a save in-map position
		VectorCopy ( vJumpAngles, angles );
		VectorCopy ( vJumpOrigin, origin );
		return;
	}


	if( ent->index == local->index )
	{
		if ( g_iUser3 && g_iUser3 != 1 )
			V_GetDeathCam( ent, gEngfuncs.GetEntityByIndex( g_iUser3 ),
						   angles, origin );
		else
			V_GetDeathCam( ent, NULL,
						   angles, origin );
	}
	else if ( gHUD.m_Spectator.m_autoDirector->value )
	{
		if ( g_iUser3 && g_iUser3 != 1 )
			V_GetDirectedChasePosition( ent, gEngfuncs.GetEntityByIndex( g_iUser3 ),
										angles, origin );
		else
			V_GetDirectedChasePosition( ent, (cl_entity_t*)0xFFFFFFFF,
										angles, origin );
	}
	else
	{
		if ( cl_angles == NULL )	// no mouse angles given, use entity angles ( locked mode )
		{
			VectorCopy ( ent->angles, angles);
			angles[0]*=-1;
		}
		else
			VectorCopy ( cl_angles, angles);


		VectorCopy ( ent->origin, origin);

		origin[2]+= 28; // DEFAULT_VIEWHEIGHT - some offset

		V_GetChaseOrigin( angles, origin, cl_chasedist->value, origin );
	}

	v_resetCamera = false;
}

void V_ResetChaseCam()
{
	v_resetCamera = true;
}


void V_GetInEyePos(int target, float * origin, float * angles )
{
	if ( !target)
	{
		// just copy a save in-map position
		VectorCopy ( vJumpAngles, angles );
		VectorCopy ( vJumpOrigin, origin );
		return;
	};


	cl_entity_t	 * ent = gEngfuncs.GetEntityByIndex( target );

	if ( !ent )
		return;

	VectorCopy ( ent->origin, origin );
	VectorCopy ( ent->angles, angles );

	angles[PITCH]*=-3.0f;	// see CL_ProcessEntityUpdate()

	if ( ent->curstate.solid == SOLID_NOT )
	{
		angles[ROLL] = 80;	// dead view angle
		origin[2]+= -8 ; // PM_DEAD_VIEWHEIGHT
	}
	else if (ent->curstate.usehull == 1 )
		origin[2]+= 12; // VEC_DUCK_VIEW;
	else
		// exacty eye position can't be calculated since it depends on
		// client values like cl_bobcycle, this offset matches the default values
		origin[2]+= 28; // DEFAULT_VIEWHEIGHT
}

void V_GetMapFreePosition( float * cl_angles, float * origin, float * angles )
{
	vec3_t forward;
	vec3_t zScaledTarget;

	VectorCopy(cl_angles, angles);

	// modify angles since we don't wanna see map's bottom
	angles[0] = 51.25f + 38.75f*(angles[0]/90.0f);

	zScaledTarget[0] = gHUD.m_Spectator.m_mapOrigin[0];
	zScaledTarget[1] = gHUD.m_Spectator.m_mapOrigin[1];
	zScaledTarget[2] = gHUD.m_Spectator.m_mapOrigin[2] * (( 90.0f - angles[0] ) / 90.0f );


	AngleVectors(angles, forward, NULL, NULL);

	VectorNormalize(forward);

	VectorMA(zScaledTarget, -( 4096.0f / gHUD.m_Spectator.m_mapZoom ), forward , origin);
}

void V_GetMapChasePosition(int target, float * cl_angles, float * origin, float * angles)
{
	vec3_t forward;

	if ( target )
	{
		cl_entity_t	 *	ent = gEngfuncs.GetEntityByIndex( target );

		if ( gHUD.m_Spectator.m_autoDirector->value )
		{
			// this is done to get the angles made by director mode
			V_GetChasePos(target, cl_angles, origin, angles);
			VectorCopy(ent->origin, origin);

			// keep fix chase angle horizontal
			angles[0] = 45.0f;
		}
		else
		{
			VectorCopy(cl_angles, angles);
			VectorCopy(ent->origin, origin);

			// modify angles since we don't wanna see map's bottom
			angles[0] = 51.25f + 38.75f*(angles[0]/90.0f);
		}
	}
	else
	{
		// keep out roaming position, but modify angles
		VectorCopy(cl_angles, angles);
		angles[0] = 51.25f + 38.75f*(angles[0]/90.0f);
	}

	origin[2] *= (( 90.0f - angles[0] ) / 90.0f );
	angles[2] = 0.0f;	// don't roll angle (if chased player is dead)

	AngleVectors(angles, forward, NULL, NULL);

	VectorNormalize(forward);

	VectorMA(origin, -1536, forward, origin);
}

int V_FindViewModelByWeaponModel( int iWeaponIndex )
{
	model_t *pWeaponModel = IEngineStudio.GetModelByIndex( iWeaponIndex );
	if ( pWeaponModel )
	{
		static char szViewModelName[MAX_MODEL_NAME];
		strncpy( szViewModelName, pWeaponModel->name, sizeof( szViewModelName ) );

		char *szSub = strstr( szViewModelName, "/p_" );
		if( szSub )
		{
			// szSub pointer is a part of szViewModelName
			szSub[1] = 'v';
			return gEngfuncs.pEventAPI->EV_FindModelIndex(szViewModelName);
		}
		return 0;
	}
	else
		return 0;

}


/*
==================
V_CalcSpectatorRefdef

==================
*/
void V_CalcSpectatorRefdef ( struct ref_params_s * pparams )
{
	static vec3_t			velocity ( 0.0f, 0.0f, 0.0f);

	static int lastWeaponModelIndex = 0;
	static int lastViewModelIndex = 0;

	cl_entity_t	 * ent = gEngfuncs.GetEntityByIndex( g_iUser2 );

	pparams->onlyClientDraw = false;

	// refresh position
	VectorCopy ( pparams->simorg, v_sim_org );

	// get old values
	VectorCopy ( pparams->cl_viewangles, v_cl_angles );
	VectorCopy ( pparams->viewangles, v_angles );
	VectorCopy ( pparams->vieworg, v_origin );

	if ( ( g_iUser1 == OBS_IN_EYE || gHUD.m_Spectator.m_pip->value == INSET_IN_EYE ) && ent )
	{
		// calculate player velocity
		float timeDiff = ent->curstate.msg_time - ent->prevstate.msg_time;

		if ( timeDiff > 0 )
		{
			vec3_t distance;
			VectorSubtract(ent->prevstate.origin, ent->curstate.origin, distance);
			VectorScale(distance, 1/timeDiff, distance );

			velocity[0] = velocity[0]*0.9f + distance[0]*0.1f;
			velocity[1] = velocity[1]*0.9f + distance[1]*0.1f;
			velocity[2] = velocity[2]*0.9f + distance[2]*0.1f;

			VectorCopy(velocity, pparams->simvel);
		}

		// predict missing client data and set weapon model ( in HLTV mode or inset in eye mode )
		if ( gEngfuncs.IsSpectateOnly() )
		{
			V_GetInEyePos( g_iUser2, pparams->simorg, pparams->cl_viewangles );

			pparams->health = 1;

			cl_entity_t	*gunModel = gEngfuncs.GetViewModel();

			if ( lastWeaponModelIndex != ent->curstate.weaponmodel )
			{
				// weapon model changed

				lastWeaponModelIndex = ent->curstate.weaponmodel;
				lastViewModelIndex = V_FindViewModelByWeaponModel( lastWeaponModelIndex );
				if ( lastViewModelIndex )
				{
					gEngfuncs.pfnWeaponAnim(0,0);	// reset weapon animation
				}
				else
				{
					// model not found
					gunModel->model = NULL;	// disable weapon model
					lastWeaponModelIndex = lastViewModelIndex = 0;
				}
			}

			if ( lastViewModelIndex )
			{
				gunModel->model = IEngineStudio.GetModelByIndex( lastViewModelIndex );
				gunModel->curstate.modelindex = lastViewModelIndex;
				gunModel->curstate.frame = 0;
				gunModel->curstate.colormap = 0;
				gunModel->index = g_iUser2;
			}
			else
			{
				gunModel->model = NULL;	// disable weaopn model
			}
		}
		else
		{
			// only get viewangles from entity
			VectorCopy ( ent->angles, pparams->cl_viewangles );
			pparams->cl_viewangles[PITCH]*=-3.0f;	// see CL_ProcessEntityUpdate()
		}
	}

	v_frametime = pparams->frametime;

	if ( pparams->nextView == 0 )
	{
		// first renderer cycle, full screen

		switch ( g_iUser1 )
		{
		case OBS_CHASE_LOCKED:
			V_GetChasePos( g_iUser2, NULL, v_origin, v_angles );
			break;

		case OBS_CHASE_FREE:
			V_GetChasePos( g_iUser2, v_cl_angles, v_origin, v_angles );
			break;

		case OBS_ROAMING:
			VectorCopy (v_cl_angles, v_angles);
			VectorCopy (v_sim_org, v_origin);
			break;

		case OBS_IN_EYE:
			V_CalcNormalRefdef ( pparams );
			break;

		case OBS_MAP_FREE:
			pparams->onlyClientDraw = true;
			V_GetMapFreePosition( v_cl_angles, v_origin, v_angles );
			break;

		case OBS_MAP_CHASE:
			pparams->onlyClientDraw = true;
			V_GetMapChasePosition( g_iUser2, v_cl_angles, v_origin, v_angles );
			break;
		}

		if ( gHUD.m_Spectator.m_pip->value )
			pparams->nextView = 1;	// force a second renderer view

		gHUD.m_Spectator.m_iDrawCycle = 0;

	}
	else
	{
		// second renderer cycle, inset window

		// set inset parameters
		pparams->viewport[0] = XRES(gHUD.m_Spectator.m_OverviewData.insetWindowX);	// change viewport to inset window
		pparams->viewport[1] = YRES(gHUD.m_Spectator.m_OverviewData.insetWindowY);
		pparams->viewport[2] = XRES(gHUD.m_Spectator.m_OverviewData.insetWindowWidth);
		pparams->viewport[3] = YRES(gHUD.m_Spectator.m_OverviewData.insetWindowHeight);
		pparams->nextView	 = 0;	// on further view

		// override some settings in certain modes
		switch ( (int)gHUD.m_Spectator.m_pip->value )
		{
		case INSET_CHASE_LOCKED:
			V_GetChasePos( g_iUser2, NULL, v_origin, v_angles );
			break;

		case INSET_CHASE_FREE:
			V_GetChasePos( g_iUser2, v_cl_angles, v_origin, v_angles );
			break;

		case INSET_IN_EYE:
			V_CalcNormalRefdef ( pparams );
			break;

		case INSET_MAP_FREE:
			pparams->onlyClientDraw = true;
			V_GetMapFreePosition( v_cl_angles, v_origin, v_angles );
			break;

		case INSET_MAP_CHASE:
			pparams->onlyClientDraw = true;

			if ( g_iUser1 == OBS_ROAMING )
				V_GetMapChasePosition( 0, v_cl_angles, v_origin, v_angles );
			else
				V_GetMapChasePosition( g_iUser2, v_cl_angles, v_origin, v_angles );

			break;
		}

		gHUD.m_Spectator.m_iDrawCycle = 1;
	}

	// write back new values into pparams
	VectorCopy ( v_cl_angles, pparams->cl_viewangles );
	VectorCopy ( v_angles, pparams->viewangles )
	VectorCopy ( v_origin, pparams->vieworg );
}

void V_CalcThirdPersonRefdef( ref_params_t *pparams )
{
	static float lasttime, oldz = 0;

	pparams->vieworg = pparams->simorg;
	pparams->vieworg = pparams->vieworg + pparams->viewheight;
	pparams->viewangles = pparams->cl_viewangles + pparams->punchangle + ev_punchangle;

	v_angles = pparams->viewangles;
	v_lastAngles = pparams->viewangles;

	// never let view origin sit exactly on a node line, because a water plane can
	// disappear when viewed with the eye exactly on it.
	// FIXME, we send origin at 1/128 now, change this?
	// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
	pparams->vieworg[0] += 1.0f / 32;
	pparams->vieworg[1] += 1.0f / 32;
	pparams->vieworg[2] += 1.0f / 32;

	// this is smooth stair climbing in thirdperson mode but not affected for client model :(
	if( !pparams->smoothing && pparams->onground && pparams->simorg[2] - oldz > 0.0f )
	{
		float steptime;

		steptime = pparams->time - lasttime;
		if( steptime < 0 ) steptime = 0;

		oldz += steptime * 150.0f;

		if( oldz > pparams->simorg[2] )
			oldz = pparams->simorg[2];
		if( pparams->simorg[2] - oldz > pparams->movevars->stepsize )
			oldz = pparams->simorg[2] - pparams->movevars->stepsize;

		pparams->vieworg[2] += oldz - pparams->simorg[2];
	}
	else
	{
		oldz = pparams->simorg[2];
	}

	lasttime = pparams->time;

	vec3_t ofs, camAngles, camForward, camRight, camUp;

	ofs[0] = ofs[1] = ofs[2] = 0.0;

	CL_CameraOffset( (float *)&ofs );

	VectorCopy( ofs, camAngles );
	camAngles[ ROLL ]	= 0;

	AngleVectors( camAngles, camForward, camRight, camUp );

	for ( int i = 0; i < 3; i++ )
	{
		pparams->vieworg[ i ] += -ofs[2] * camForward[ i ];
	}

	VectorCopy( camAngles, pparams->viewangles);

	V_GetChaseOrigin( pparams->viewangles, pparams->vieworg, cl_chasedist->value, pparams->vieworg );

	float pitch = pparams->viewangles[PITCH];

	// normalize angles
	if( pitch > 180.0f )
		pitch -= 360.0f;
	else if( pitch < -180.0f )
		pitch += 360.0f;

	// player pitch is inverted
	pitch /= -3.0;

	cl_entity_t *ent = gEngfuncs.GetLocalPlayer();

	// slam local player's pitch value
	ent->angles[PITCH] = pitch;
	ent->curstate.angles[PITCH] = pitch;
	ent->prevstate.angles[PITCH] = pitch;
	ent->latched.prevangles[PITCH] = pitch;
}

void DLLEXPORT V_CalcRefdef( struct ref_params_s *pparams )
{
	s_ADSPaused=pparams->paused!=0;
	// These branches bypass the local first-person effect update entirely.
	if( pparams->intermission || g_iUser1 || CL_IsThirdPerson() || pparams->health <= 0 )
		V_ResetViewModelEffects();
	// intermission / finale rendering
	if ( pparams->intermission )
	{
		V_CalcIntermissionRefdef ( pparams );
	}
	else if ( g_iUser1 )	// g_iUser true if in spectator mode
	{
		V_CalcSpectatorRefdef ( pparams );
	}
	else if ( CL_IsThirdPerson() )
	{
		V_CalcThirdPersonRefdef ( pparams );
	}
	else
	{
		V_CalcNormalRefdef ( pparams );
	}
}

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	gEngfuncs.pfnAddCommand("ads_dump",V_DumpADS);
	//gEngfuncs.pfnAddCommand ("centerview", V_StartPitchDrift );

	scr_ofsx			= gEngfuncs.pfnRegisterVariable( "scr_ofsx","0", 0 );
	scr_ofsy			= gEngfuncs.pfnRegisterVariable( "scr_ofsy","0", 0 );
	scr_ofsz			= gEngfuncs.pfnRegisterVariable( "scr_ofsz","0", 0 );

	v_centermove		= gEngfuncs.pfnRegisterVariable( "v_centermove", "0.15", 0 );
	v_centerspeed		= gEngfuncs.pfnRegisterVariable( "v_centerspeed","500", 0 );

	cl_bobcycle			= gEngfuncs.pfnRegisterVariable( "cl_bobcycle","0.98", FCVAR_ARCHIVE );
	cl_bob				= gEngfuncs.pfnRegisterVariable( "cl_bob","0.01", FCVAR_ARCHIVE );// best default for my experimental gun wag (sjb)
	cl_bobup			= gEngfuncs.pfnRegisterVariable( "cl_bobup","0.5", FCVAR_ARCHIVE );
	cl_smoothstairs = gEngfuncs.pfnRegisterVariable( "cl_smoothstairs", "1", FCVAR_ARCHIVE );
	cl_waterdist		= gEngfuncs.pfnRegisterVariable( "cl_waterdist","4", 0 );
	cl_chasedist		= gEngfuncs.pfnRegisterVariable( "cl_chasedist","112", 0 );

	cl_quakeguns		= gEngfuncs.pfnRegisterVariable( "cl_quakeguns", "0", FCVAR_ARCHIVE );
	cl_weaponlag			= gEngfuncs.pfnRegisterVariable( "cl_weaponlag", "0", FCVAR_ARCHIVE );

	// CS:GO weapon sway, from predicted_viewmodel.cpp CalcViewModelLag().
	// interp is how far back the view angles are sampled -- 0.1 is CS:GO's
	// stock 100 ms -- and scale is the gain on all three local sway axes.
	cl_wpn_sway_interp		= gEngfuncs.pfnRegisterVariable( "cl_wpn_sway_interp", "0.1", FCVAR_ARCHIVE );
	cl_wpn_sway_scale		= gEngfuncs.pfnRegisterVariable( "cl_wpn_sway_scale", "1.6", FCVAR_ARCHIVE );
}
