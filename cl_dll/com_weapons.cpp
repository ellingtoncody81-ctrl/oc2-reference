/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/

// Com_Weapons.cpp
// Shared weapons common/shared functions
#include <stdarg.h>
#include "hud.h"
#include "oc2_observer.h"
#include "cl_util.h"
#include "com_weapons.h"

#include "const.h"
#include "entity_state.h"
#include "r_efx.h"
#include "wpn_shared/oc2_reload.h"
#include "wpn_shared/oc2_presentation.h"
#include "weapon_fire_feedback.h"
#include "viewmodel_motion.h"
#include "ref_params.h"
#include "com_model.h"
#include "usercmd.h"
#include "studio.h"
#include "studio_util.h"
#include <stdint.h>
#include <string.h>

extern int g_iUser1;

// g_runfuncs is true if this is the first time we've "predicated" a particular movement/firing
//  command.  If it is 1, then we should play events/sounds etc., otherwise, we just will be
//  updating state info, but not firing events
int	g_runfuncs = 0;

// During our weapon prediction processing, we'll need to reference some data that is part of
//  the final state passed into the postthink functionality.  We'll set this pointer and then
//  reset it to NULL as appropriate
struct local_state_s *g_finalstate = NULL;

/*
====================
COM_Log

Log debug messages to file ( appends )
====================
*/
void COM_Log( char *pszFile, char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	FILE *fp;
	const char *pfilename;
	
	if ( !pszFile )
	{
		pfilename = "c:\\hllog.txt";
	}
	else
	{
		pfilename = pszFile;
	}

	va_start (argptr,fmt);
	vsnprintf( string, sizeof( string ), fmt, argptr );
	va_end (argptr);

	fp = fopen( pfilename, "a+t");
	if (fp)
	{
		fprintf(fp, "%s", string);
		fclose(fp);
	}
}

// remember the current animation for the view model, in case we get out of sync with
//  server.
static int g_currentanim;
static int g_currentweapon;

namespace
{
struct WeaponMotionSample
{
	double time=0, sampleTime=0, start=0, commandTime=0;
	int weapon=0, state=0, clip=0, fromClip=0, toClip=0, sequence=0, change=0, layers=0;
	int events=0, runfuncs=0, msec=0, sends=0, forced=0, blocked=0, ground=0, loop=0, snap=0;
	unsigned seed=0;
	uint32_t pose=0;
	float frame=0, vz=0, pitch=0, eye[3]={}, gun[3]={}, punch[3]={}, hand[3]={};
	float poseStep=0, angleStep=0, positionRms=0, positionPeak=0, rotationRms=0, rotationPeak=0;
	char model[96]={}, label[32]={};
};
struct WeaponMotionCapture
{
	bool active=false, viewValid=false, previous=false;
	int count=0, dropped=0, sends=0, forced=0, blocked=0, previousBones=0;
	char tag[48]="capture";
	WeaponMotionSample view, prediction, rows[2048];
	float previousMatrices[MAXSTUDIOBONES][3][4]={};
	float previousPosition[MAXSTUDIOBONES][3]={}, previousRotation[MAXSTUDIOBONES][4]={};
} g_motion;

void WeaponMotionBegin()
{
	const char *tag=gEngfuncs.Cmd_Argc()>1 ? gEngfuncs.Cmd_Argv(1) : "capture";
	const size_t length=strlen(tag);
	if (!length || length>=sizeof(g_motion.tag)) { gEngfuncs.Con_Printf("weaponmotion: invalid tag\n");return; }
	for (size_t i=0;i<length;++i)
		if (!((tag[i]>='a'&&tag[i]<='z') || (tag[i]>='A'&&tag[i]<='Z') ||
			(tag[i]>='0'&&tag[i]<='9') || tag[i]=='_'))
			{ gEngfuncs.Con_Printf("weaponmotion: invalid tag\n");return; }
	g_motion.active=false;g_motion.count=g_motion.dropped=0;g_motion.previous=false;
	g_motion.sends=g_motion.forced=g_motion.blocked=0;
	strcpy(g_motion.tag,tag);g_motion.active=true;
	gEngfuncs.Con_Printf("weaponmotion: begin tag=%s capacity=2048\n",g_motion.tag);
}

void WeaponMotionEnd()
{
	g_motion.active=false;
	gEngfuncs.Con_Printf("weaponmotion: end tag=%s frames=%d dropped=%d\n",g_motion.tag,g_motion.count,g_motion.dropped);
}

void WeaponMotionDump()
{
	char path[1024];
	snprintf(path,sizeof(path),"%s/weapon_motion_%s.log",gEngfuncs.pfnGetGameDirectory(),g_motion.tag);
	FILE *file=fopen(path,"ab");
	if (!file) { gEngfuncs.Con_Printf("weaponmotion: save failed path=%s\n",path);return; }
	fprintf(file,"capture tag=%s frames=%d dropped=%d active=%d\n",g_motion.tag,g_motion.count,g_motion.dropped,int(g_motion.active));
	fputs("time,sample,start,weapon,state,clip,model,label,seq,frame,change,layers,snap,loop,events,sends,forced,replay_blocked,ground,vz,pitch,eye_x,eye_y,eye_z,gun_x,gun_y,gun_z,punch_x,punch_y,punch_z,pose_hash,pose_step,angle_step,hand_x,hand_y,hand_z,cmd_time,cmd_msec,cmd_seed,cmd_runfuncs,from_clip,to_clip,pos_rms,pos_peak,rot_rms,rot_peak\n",file);
	for (int i=0;i<g_motion.count;++i)
	{
		const WeaponMotionSample &r=g_motion.rows[i];
		fprintf(file,"%.9f,%.9f,%.9f,%d,%d,%d,%s,%s,%d,%.6f,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%08x,%.6f,%.6f,%.6f,%.6f,%.6f,%.9f,%d,%u,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n",
			r.time,r.sampleTime,r.start,r.weapon,r.state,r.clip,r.model,r.label,r.sequence,r.frame,r.change,r.layers,r.snap,r.loop,
			r.events,r.sends,r.forced,r.blocked,r.ground,r.vz,r.pitch,r.eye[0],r.eye[1],r.eye[2],r.gun[0],r.gun[1],r.gun[2],
			r.punch[0],r.punch[1],r.punch[2],r.pose,r.poseStep,r.angleStep,r.hand[0],r.hand[1],r.hand[2],
			r.commandTime,r.msec,r.seed,r.runfuncs,r.fromClip,r.toClip,r.positionRms,r.positionPeak,r.rotationRms,r.rotationPeak);
	}
	fclose(file);
	gEngfuncs.Con_Printf("weaponmotion: saved tag=%s frames=%d dropped=%d path=%s\n",g_motion.tag,g_motion.count,g_motion.dropped,path);
}
}

void HUD_WeaponMotionInit()
{
	static bool initialized=false;
	if (initialized) return;
	initialized=true;
	gEngfuncs.pfnAddCommand("weapon_motion_begin",WeaponMotionBegin);
	gEngfuncs.pfnAddCommand("weapon_motion_end",WeaponMotionEnd);
	gEngfuncs.pfnAddCommand("weapon_motion_dump",WeaponMotionDump);
}

void HUD_WeaponMotionPrediction(const local_state_s *from, const local_state_s *to,
	const usercmd_s *command, int runfuncs, double time, unsigned int seed)
{
	if (!g_motion.active || !from || !to || !command) return;
	const int id=to->client.m_iId;
	g_motion.prediction.commandTime=time;g_motion.prediction.msec=command->msec;
	g_motion.prediction.seed=seed;g_motion.prediction.runfuncs=runfuncs;
	g_motion.prediction.fromClip=id>0&&id<32?from->weapondata[id].m_iClip:-1;
	g_motion.prediction.toClip=id>0&&id<32?to->weapondata[id].m_iClip:-1;
}

void HUD_WeaponMotionView(const ref_params_s *view, int weapon, int state, int clip)
{
	g_motion.viewValid=view&&view->health>0&&!view->spectator&&!g_iUser1;
	if (!g_motion.active || !g_motion.viewValid) return;
	g_motion.view=g_motion.prediction;
	g_motion.view.weapon=weapon;g_motion.view.state=state;g_motion.view.clip=clip;
	g_motion.view.ground=view->onground!=0;g_motion.view.vz=view->simvel[2];
	g_motion.view.pitch=view->viewangles[0];
	const cl_entity_t *gun=gEngfuncs.GetViewModel();
	for (int axis=0;axis<3;++axis)
	{
		g_motion.view.eye[axis]=view->vieworg[axis];
		g_motion.view.gun[axis]=gun?gun->origin[axis]:0;
		g_motion.view.punch[axis]=view->punchangle[axis];
	}
}

void HUD_WeaponMotionPose(const char *model, const void *data,
	const float pos[][3], const float q[][4], double time, double sampleTime,
	double start, int sequence, float frame, int change, int layers, bool snap)
{
	if (!g_motion.active || !g_motion.viewValid) return;
	if (g_motion.count && g_motion.rows[g_motion.count-1].time==time) return;
	if (g_motion.count==2048) { ++g_motion.dropped;return; }
	const studiohdr_t *header=static_cast<const studiohdr_t *>(data);
	if (!header || header->numbones<=0 || header->numbones>MAXSTUDIOBONES || sequence<0 || sequence>=header->numseq) return;
	WeaponMotionSample row=g_motion.view;
	row.time=time;row.sampleTime=sampleTime;row.start=start;row.sequence=sequence;
	row.frame=frame;row.change=change;row.layers=layers;row.snap=snap;
	row.events=g_iShotsFired;row.sends=g_motion.sends;row.forced=g_motion.forced;row.blocked=g_motion.blocked;
	strncpy(row.model,model?model:"none",sizeof(row.model)-1);
	const mstudioseqdesc_t &desc=reinterpret_cast<const mstudioseqdesc_t *>(reinterpret_cast<const byte *>(header)+header->seqindex)[sequence];
	strncpy(row.label,desc.label,sizeof(row.label)-1);row.loop=(desc.flags&STUDIO_LOOPING)!=0;
	const mstudiobone_t *bones=reinterpret_cast<const mstudiobone_t *>(reinterpret_cast<const byte *>(header)+header->boneindex);
	float matrices[MAXSTUDIOBONES][3][4];
	const bool compare=g_motion.previous&&g_motion.previousBones==header->numbones&&
		g_motion.count&&strcmp(g_motion.rows[g_motion.count-1].model,row.model)==0;
	for (int bone=0;bone<header->numbones;++bone)
	{
		float rotation[4],local[3][4];memcpy(rotation,q[bone],sizeof(rotation));QuaternionMatrix(rotation,local);
		for (int axis=0;axis<3;++axis) local[axis][3]=pos[bone][axis];
		if (bones[bone].parent>=0&&bones[bone].parent<bone) ConcatTransforms(matrices[bones[bone].parent],local,matrices[bone]);
		else memcpy(matrices[bone],local,sizeof(local));
		if (!stricmp(bones[bone].name,"v_weapon.Bip01_R_Hand"))
			for (int axis=0;axis<3;++axis) row.hand[axis]=matrices[bone][axis][3];
		if (compare)
		{
			float localDistance=0,quaternionDot=0;
			for (int axis=0;axis<3;++axis)
			{
				const float delta=pos[bone][axis]-g_motion.previousPosition[bone][axis];localDistance+=delta*delta;
			}
			for (int axis=0;axis<4;++axis) quaternionDot+=q[bone][axis]*g_motion.previousRotation[bone][axis];
			localDistance=sqrtf(localDistance);row.positionRms+=localDistance*localDistance;
			if (localDistance>row.positionPeak) row.positionPeak=localDistance;
			quaternionDot=fabsf(quaternionDot);if (quaternionDot>1) quaternionDot=1;
			const float localAngle=2*acosf(quaternionDot)*57.2957795f;
			row.rotationRms+=localAngle*localAngle;if (localAngle>row.rotationPeak) row.rotationPeak=localAngle;
			float distance=0,trace=0;
			for (int axis=0;axis<3;++axis)
			{
				const float delta=matrices[bone][axis][3]-g_motion.previousMatrices[bone][axis][3];distance+=delta*delta;
				for (int column=0;column<3;++column) trace+=matrices[bone][axis][column]*g_motion.previousMatrices[bone][axis][column];
			}
			distance=sqrtf(distance);if (distance>row.poseStep) row.poseStep=distance;
			float cosine=(trace-1)*.5f;cosine=cosine<-1?-1:cosine>1?1:cosine;
			const float angle=acosf(cosine)*57.2957795f;if (angle>row.angleStep) row.angleStep=angle;
		}
	}
	row.positionRms=sqrtf(row.positionRms/header->numbones);row.rotationRms=sqrtf(row.rotationRms/header->numbones);
	row.pose=2166136261u;
	const byte *bytes=reinterpret_cast<const byte *>(matrices);
	for (size_t i=0;i<header->numbones*sizeof(matrices[0]);++i) row.pose=(row.pose^bytes[i])*16777619u;
	memcpy(g_motion.previousMatrices,matrices,header->numbones*sizeof(matrices[0]));
	memcpy(g_motion.previousPosition,pos,header->numbones*sizeof(pos[0]));
	memcpy(g_motion.previousRotation,q,header->numbones*sizeof(q[0]));
	g_motion.previous=true;g_motion.previousBones=header->numbones;
	g_motion.rows[g_motion.count++]=row;
}

struct ReloadAnimationSnapshot
{
	bool active = false, shield = false;
	int weapon = 0, animation = 0, model = 0, fireEvents = 0;
	unsigned action = 0;
	float remaining = 0;
	double deadline = 0, sampled = 0;
} g_reloadAnimation;

struct ReloadAnimationDisplay
{
	bool active = false, repaired = false, bound = false, superseded = false, shield = false;
	int weapon = 0, animation = 0, model = 0, repairs = 0, reason = 0;
	int engineSequence = 0, fireEvents = 0;
	unsigned action = 0;
	double deadline = 0, start = 0, engineStart = 0, time = 0;
	float elapsed = 0, observed = 0;
} g_reloadDisplay;

static unsigned g_weaponAnimationAction = 0;

void HUD_UpdateReloadAnimation(const local_state_s *state, double predictedTime)
{
	// A corrected prediction can first enter Reload on a replay (runfuncs == 0), after the
	// original command was rejected by a corrected attack gate. Record the
	// final state, without dispatching replayed sounds or restarting a clock.
	g_reloadAnimation.active = false;
	if (!state || predictedTime-predictedTime != 0) return;
	const int id = state->client.m_iId;
	if (id <= 0 || id >= 32 || state->client.health <= 0 || !state->client.viewmodel) return;
	const weapon_data_t &weapon = state->weapondata[id];
	const bool shield = (state->client.iuser3 & PLAYER_HOLDING_SHIELD) != 0;
	if (!(weapon.m_fInReload || weapon.m_fInSpecialReload) ||
		!oc2_reload::Find(id,state->client.weaponanim,shield)) return;
	const float remaining = oc2_reload::Shotgun(id) ? weapon.m_flTimeWeaponIdle : state->client.m_flNextAttack;
	if (!oc2_reload::Finite(remaining) || remaining <= 0) return;
	g_reloadAnimation.active = true; g_reloadAnimation.shield = shield;
	g_reloadAnimation.weapon = id; g_reloadAnimation.animation = state->client.weaponanim;
	g_reloadAnimation.model = state->client.viewmodel; g_reloadAnimation.remaining = remaining;
	g_reloadAnimation.deadline = predictedTime + remaining;
	g_reloadAnimation.sampled = gEngfuncs.GetClientTime();
	g_reloadAnimation.action = g_weaponAnimationAction;
	g_reloadAnimation.fireEvents = g_iShotsFired;
}

void HUD_ApplyReloadAnimation(cl_entity_s *view, int weapon, bool shield, double time)
{
	if (!view || time-time != 0) return;
	// Small server-clock corrections are not new animation instances. Keep the
	// display start so the renderer can hold its already reached sample.
	if (time < g_reloadDisplay.time-.5)
		g_reloadDisplay = ReloadAnimationDisplay();
	g_reloadDisplay.time = time;
	const bool sameContext = g_reloadDisplay.bound && weapon == g_reloadDisplay.weapon &&
		shield == g_reloadDisplay.shield && view->curstate.modelindex == g_reloadDisplay.model;
	const bool ownSample = sameContext && g_reloadDisplay.repaired &&
		view->curstate.sequence == g_reloadDisplay.animation && view->curstate.animtime == g_reloadDisplay.start;
	const bool engineSample = sameContext && view->curstate.sequence == g_reloadDisplay.engineSequence &&
		view->curstate.animtime == g_reloadDisplay.engineStart;
	const bool newAction = sameContext && (g_weaponAnimationAction != g_reloadDisplay.action ||
		g_iShotsFired != g_reloadDisplay.fireEvents || (!ownSample && !engineSample));
	if (newAction)
	{
		// CL_FireEvents runs after prediction and before the view is drawn. An
		// accepted fire/idle instance must win over a preceding reload snapshot,
		// including replays of that same obsolete predicted reload on later frames.
		g_reloadDisplay.superseded = true;
		g_reloadDisplay.repaired = false;
		g_reloadDisplay.reason = 3;
	}
	if (!g_reloadAnimation.active || weapon != g_reloadAnimation.weapon ||
		shield != g_reloadAnimation.shield || view->curstate.modelindex != g_reloadAnimation.model)
	{
		// A repaired clock may finish on its terminal pose while R is held.
		// Keep that correction until a real new animation changes the engine
		// start/sequence; do not resurrect a cancelled reload or override fire.
		if (g_reloadDisplay.repaired && !g_reloadDisplay.superseded && sameContext &&
			view->curstate.sequence == g_reloadDisplay.animation && (ownSample || engineSample))
			view->curstate.animtime = g_reloadDisplay.start;
		else g_reloadDisplay.repaired = false;
		g_reloadDisplay.active = false;
		return;
	}
	const bool changed = !sameContext ||
		g_reloadAnimation.animation != g_reloadDisplay.animation ||
		g_reloadAnimation.action != g_reloadDisplay.action ||
		fabs(g_reloadAnimation.deadline-g_reloadDisplay.deadline) > .2;
	if (changed)
	{
		g_reloadDisplay.repaired = false;
		g_reloadDisplay.bound = true; g_reloadDisplay.superseded = false;
		g_reloadDisplay.weapon = weapon; g_reloadDisplay.animation = g_reloadAnimation.animation;
		g_reloadDisplay.model = g_reloadAnimation.model; g_reloadDisplay.shield = shield;
		g_reloadDisplay.action = g_reloadAnimation.action; g_reloadDisplay.fireEvents = g_reloadAnimation.fireEvents;
		g_reloadDisplay.engineSequence = view->curstate.sequence; g_reloadDisplay.engineStart = view->curstate.animtime;
	}
	g_reloadDisplay.active = true; g_reloadDisplay.deadline = g_reloadAnimation.deadline;
	if (g_reloadDisplay.superseded || g_reloadAnimation.action != g_weaponAnimationAction ||
		g_reloadDisplay.fireEvents != g_iShotsFired)
	{
		g_reloadDisplay.superseded = true; g_reloadDisplay.active = g_reloadDisplay.repaired = false;
		g_reloadDisplay.reason = 3;
		return;
	}
	const float duration = oc2_reload::Duration(weapon,g_reloadAnimation.animation,shield,0);
	const float elapsed = duration-g_reloadAnimation.remaining+static_cast<float>(time-g_reloadAnimation.sampled);
	if (!oc2_reload::Finite(elapsed) || duration <= 0) return;
	g_reloadDisplay.elapsed = elapsed > 0 ? elapsed : 0;
	g_reloadDisplay.observed = static_cast<float>(time-view->curstate.animtime);
	const bool sequenceMismatch = view->curstate.sequence != g_reloadAnimation.animation;
	const bool staleClock = !g_reloadDisplay.repaired &&
		fabs(g_reloadDisplay.observed-g_reloadDisplay.elapsed) > .15f;
	if (!g_reloadDisplay.repaired && elapsed >= -.1f && elapsed < duration && (sequenceMismatch || staleClock))
	{
		// Source has animation parity for a new instance of the same sequence.
		// GoldSrc only has its sequence number, so recover a missing instance
		// from the existing predicted reload lock, never from an R key press.
		g_reloadDisplay.start = static_cast<float>(time-g_reloadDisplay.elapsed);
		g_reloadDisplay.repaired = true; ++g_reloadDisplay.repairs;
		g_reloadDisplay.reason = sequenceMismatch ? 1 : 2;
	}
	if (g_reloadDisplay.repaired)
	{
		// This is only the current render sample, never an engine animation
		// command. In particular, do not clear weaponstarttime from rendering.
		// Keep the existing correction across the terminal frame: prediction can
		// still report 5ms remaining when this render is already 16ms later.
		view->curstate.sequence = g_reloadDisplay.animation; view->curstate.frame = 0;
		view->curstate.animtime = g_reloadDisplay.start;
	}
}

void HUD_DumpReloadAnimation()
{
	gEngfuncs.Con_Printf("oc2reloadsync: active=%d weapon=%d seq=%d remaining=%.6f expected_elapsed=%.6f observed_elapsed=%.6f repaired=%d repairs=%d reason=%d deadline=%.6f bound=%d superseded=%d action=%u fire_events=%d engine_start=%.6f display_start=%.6f\n",
		g_reloadAnimation.active,g_reloadAnimation.weapon,g_reloadAnimation.animation,g_reloadAnimation.remaining,
		g_reloadDisplay.elapsed,g_reloadDisplay.observed,g_reloadDisplay.repaired,g_reloadDisplay.repairs,
		g_reloadDisplay.reason,g_reloadAnimation.deadline,g_reloadDisplay.bound,g_reloadDisplay.superseded,
		g_reloadDisplay.action,g_reloadDisplay.fireEvents,g_reloadDisplay.engineStart,g_reloadDisplay.start);
}

/*
=====================
HUD_SendWeaponAnim

Change weapon model animation
=====================
*/
void HUD_SendWeaponAnim( int iAnim, int iWeaponId, int iBody, int iForce )
{
	if (g_motion.active) { ++g_motion.sends;if (iForce) ++g_motion.forced;if (!g_runfuncs&&!iForce) ++g_motion.blocked; }
	if( g_runfuncs || iForce )
	{
		if (g_currentweapon != iWeaponId || oc2_presentation::Find(iWeaponId,iAnim) ||
			oc2_reload::Find(iWeaponId,iAnim,g_bHoldingShield))
			HUD_ClearWeaponFlash();
		g_reloadDisplay.active = g_reloadDisplay.repaired = false;
		g_reloadDisplay.bound = g_reloadDisplay.superseded = false;
		g_reloadAnimation.active = false;
		++g_weaponAnimationAction;
		g_currentanim = iAnim;
		g_currentweapon = iWeaponId;

		// Tell animation system new info
		gEngfuncs.pfnWeaponAnim( iAnim, iBody );
	}
}

/*
=====================
HUD_GetWeaponAnim

Retrieve current predicted weapon animation
=====================
*/
int HUD_GetWeaponAnim( void )
{
	return g_currentanim;
}

/*
=====================
HUD_GetWeapon

Retrieve current predicted weapon id
=====================
*/
unsigned HUD_WeaponAnimationAction(){return g_weaponAnimationAction;}
int HUD_GetWeapon( void )
{
	return g_currentweapon;
}

/*
=====================
HUD_PlaySound

Play a sound, if we are seeing this command for the first time
=====================
*/
void HUD_PlaySound( char *sound, float volume )
{
	if ( !g_runfuncs || !g_finalstate )
		return;

	gEngfuncs.pfnPlaySoundByNameAtLocation( sound, volume, (float *)&g_finalstate->playerstate.origin );
}

/*
=====================
HUD_PlaybackEvent

Directly queue up an event on the client
=====================
*/
void HUD_PlaybackEvent( int flags, const edict_t *pInvoker, unsigned short eventindex, float delay,
	float *origin, float *angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2 )
{
	if ( !g_runfuncs || !g_finalstate )
	     return;

	Vector org;
	Vector ang;

	// Weapon prediction events are assumed to occur at the player's origin
	org	= g_finalstate->playerstate.origin;
	ang	= v_angles;
	gEngfuncs.pfnPlaybackEvent( flags, pInvoker, eventindex, delay, org, ang,
								fparam1, fparam2,
								iparam1, iparam2,
								bparam1, bparam2 );
}

/*
=====================
UTIL_WeaponTimeBase

Always 0.0 on client, even if not predicting weapons ( won't get called
 in that case )
=====================
*/
/*
moved in util.h

float UTIL_WeaponTimeBase( void )
{
	return 0.0;
}
*/

static unsigned int glSeed = 0; 

unsigned int seed_table[ 256 ] =
{
	28985, 27138, 26457, 9451, 17764, 10909, 28790, 8716, 6361, 4853, 17798, 21977, 19643, 20662, 10834, 20103,
	27067, 28634, 18623, 25849, 8576, 26234, 23887, 18228, 32587, 4836, 3306, 1811, 3035, 24559, 18399, 315,
	26766, 907, 24102, 12370, 9674, 2972, 10472, 16492, 22683, 11529, 27968, 30406, 13213, 2319, 23620, 16823,
	10013, 23772, 21567, 1251, 19579, 20313, 18241, 30130, 8402, 20807, 27354, 7169, 21211, 17293, 5410, 19223,
	10255, 22480, 27388, 9946, 15628, 24389, 17308, 2370, 9530, 31683, 25927, 23567, 11694, 26397, 32602, 15031,
	18255, 17582, 1422, 28835, 23607, 12597, 20602, 10138, 5212, 1252, 10074, 23166, 19823, 31667, 5902, 24630,
	18948, 14330, 14950, 8939, 23540, 21311, 22428, 22391, 3583, 29004, 30498, 18714, 4278, 2437, 22430, 3439,
	28313, 23161, 25396, 13471, 19324, 15287, 2563, 18901, 13103, 16867, 9714, 14322, 15197, 26889, 19372, 26241,
	31925, 14640, 11497, 8941, 10056, 6451, 28656, 10737, 13874, 17356, 8281, 25937, 1661, 4850, 7448, 12744,
	21826, 5477, 10167, 16705, 26897, 8839, 30947, 27978, 27283, 24685, 32298, 3525, 12398, 28726, 9475, 10208,
	617, 13467, 22287, 2376, 6097, 26312, 2974, 9114, 21787, 28010, 4725, 15387, 3274, 10762, 31695, 17320,
	18324, 12441, 16801, 27376, 22464, 7500, 5666, 18144, 15314, 31914, 31627, 6495, 5226, 31203, 2331, 4668,
	12650, 18275, 351, 7268, 31319, 30119, 7600, 2905, 13826, 11343, 13053, 15583, 30055, 31093, 5067, 761,
	9685, 11070, 21369, 27155, 3663, 26542, 20169, 12161, 15411, 30401, 7580, 31784, 8985, 29367, 20989, 14203,
	29694, 21167, 10337, 1706, 28578, 887, 3373, 19477, 14382, 675, 7033, 15111, 26138, 12252, 30996, 21409,
	25678, 18555, 13256, 23316, 22407, 16727, 991, 9236, 5373, 29402, 6117, 15241, 27715, 19291, 19888, 19847
};

unsigned int U_Random( void ) 
{ 
	glSeed *= 69069; 
	glSeed += seed_table[ glSeed & 0xff ];
 
	return ( ++glSeed & 0x0fffffff ); 
} 

void U_Srand( unsigned int seed )
{
	glSeed = seed_table[ seed & 0xff ];
}

/*
=====================
UTIL_SharedRandomLong
=====================
*/
int UTIL_SharedRandomLong( unsigned int seed, int low, int high )
{
	unsigned int range;

	U_Srand( (int)seed + low + high );

	range = high - low + 1;
	if ( !(range - 1) )
	{
		return low;
	}
	else
	{
		int offset;
		int rnum;

		rnum = U_Random();

		offset = rnum % range;

		return (low + offset);
	}
}

/*
=====================
UTIL_SharedRandomFloat
=====================
*/
float UTIL_SharedRandomFloat( unsigned int seed, float low, float high )
{
	//
	unsigned int range;

	U_Srand( (int)seed + *(int *)&low + *(int *)&high );

	U_Random();
	U_Random();

	range = high - low;
	if ( !range )
	{
		return low;
	}
	else
	{
		int tensixrand;
		float offset;

		tensixrand = U_Random() & 65535;

		offset = (float)tensixrand / 65536.0;

		return (low + offset * range );
	}
}
