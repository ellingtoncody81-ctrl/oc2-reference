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
#include "cl_util.h"
#include "com_weapons.h"

#include "const.h"
#include "entity_state.h"
#include "r_efx.h"
#include "wpn_shared/oc2_reload.h"
#include "wpn_shared/oc2_presentation.h"
#include "weapon_fire_feedback.h"

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

struct ReloadAnimationSnapshot
{
	bool active = false, shield = false;
	int weapon = 0, animation = 0, model = 0;
	float remaining = 0;
	double deadline = 0, sampled = 0;
} g_reloadAnimation;

struct ReloadAnimationDisplay
{
	bool active = false, repaired = false;
	int weapon = 0, animation = 0, model = 0, repairs = 0, reason = 0;
	double deadline = 0, start = 0, engineStart = 0, time = 0;
	float elapsed = 0, observed = 0;
} g_reloadDisplay;

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
}

void HUD_ApplyReloadAnimation(cl_entity_s *view, int weapon, bool shield, double time)
{
	if (!view || time-time != 0) return;
	if (time < g_reloadDisplay.time)
		g_reloadDisplay.active = g_reloadDisplay.repaired = false;
	g_reloadDisplay.time = time;
	if (!g_reloadAnimation.active || weapon != g_reloadAnimation.weapon ||
		shield != g_reloadAnimation.shield || view->curstate.modelindex != g_reloadAnimation.model)
	{
		// A repaired clock may finish on its terminal pose while R is held.
		// Keep that correction until a real new animation changes the engine
		// start/sequence; do not resurrect a cancelled reload or override fire.
		if (g_reloadDisplay.repaired && weapon == g_reloadDisplay.weapon &&
			view->curstate.modelindex == g_reloadDisplay.model && view->curstate.sequence == g_reloadDisplay.animation &&
			view->curstate.animtime == g_reloadDisplay.engineStart)
			view->curstate.animtime = g_reloadDisplay.start;
		else g_reloadDisplay.repaired = false;
		g_reloadDisplay.active = false;
		return;
	}
	const bool changed = !g_reloadDisplay.active || weapon != g_reloadDisplay.weapon ||
		g_reloadAnimation.animation != g_reloadDisplay.animation ||
		fabs(g_reloadAnimation.deadline-g_reloadDisplay.deadline) > .2;
	if (changed)
	{
		g_reloadDisplay.repaired = false;
		g_reloadDisplay.weapon = weapon; g_reloadDisplay.animation = g_reloadAnimation.animation;
		g_reloadDisplay.model = g_reloadAnimation.model;
	}
	g_reloadDisplay.active = true; g_reloadDisplay.deadline = g_reloadAnimation.deadline;
	const float duration = oc2_reload::Duration(weapon,g_reloadAnimation.animation,shield,0);
	const float elapsed = duration-g_reloadAnimation.remaining+static_cast<float>(time-g_reloadAnimation.sampled);
	if (!oc2_reload::Finite(elapsed) || elapsed < -.1f || elapsed >= duration) return;
	g_reloadDisplay.elapsed = elapsed > 0 ? elapsed : 0;
	g_reloadDisplay.observed = static_cast<float>(time-view->curstate.animtime);
	const bool sequenceMismatch = view->curstate.sequence != g_reloadAnimation.animation;
	const bool staleClock = !g_reloadDisplay.repaired &&
		fabs(g_reloadDisplay.observed-g_reloadDisplay.elapsed) > .15f;
	if (sequenceMismatch || staleClock)
	{
		// Source has animation parity for a new instance of the same sequence.
		// GoldSrc only has its sequence number, so recover a missing instance
		// from the existing predicted reload lock, never from an R key press.
		HUD_ClearWeaponFlash();
		g_currentanim = g_reloadAnimation.animation; g_currentweapon = weapon;
		gEngfuncs.pfnWeaponAnim(g_currentanim,view->curstate.body);
		view->curstate.sequence = g_currentanim; view->curstate.frame = 0;
		g_reloadDisplay.start = static_cast<float>(time-g_reloadDisplay.elapsed);
		g_reloadDisplay.repaired = true; ++g_reloadDisplay.repairs;
		g_reloadDisplay.reason = sequenceMismatch ? 1 : 2;
	}
	if (g_reloadDisplay.repaired)
	{
		// pfnWeaponAnim queues the engine clock for the next view setup. Only
		// presentation is corrected here; magazine events and firing locks
		// continue using the unchanged predicted/server reload state.
		if (view->curstate.animtime != g_reloadDisplay.start)
			g_reloadDisplay.engineStart = view->curstate.animtime;
		view->curstate.animtime = g_reloadDisplay.start;
	}
}

void HUD_DumpReloadAnimation()
{
	gEngfuncs.Con_Printf("oc2reloadsync: active=%d weapon=%d seq=%d remaining=%.6f expected_elapsed=%.6f observed_elapsed=%.6f repaired=%d repairs=%d reason=%d deadline=%.6f\n",
		g_reloadAnimation.active,g_reloadAnimation.weapon,g_reloadAnimation.animation,g_reloadAnimation.remaining,
		g_reloadDisplay.elapsed,g_reloadDisplay.observed,g_reloadDisplay.repaired,g_reloadDisplay.repairs,
		g_reloadDisplay.reason,g_reloadAnimation.deadline);
}

/*
=====================
HUD_SendWeaponAnim

Change weapon model animation
=====================
*/
void HUD_SendWeaponAnim( int iAnim, int iWeaponId, int iBody, int iForce )
{
	if( g_runfuncs || iForce )
	{
		if (g_currentweapon != iWeaponId || oc2_presentation::Find(iWeaponId,iAnim) ||
			oc2_reload::Find(iWeaponId,iAnim,g_bHoldingShield))
			HUD_ClearWeaponFlash();
		g_reloadDisplay.active = g_reloadDisplay.repaired = false;
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
