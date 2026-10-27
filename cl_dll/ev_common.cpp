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
// shared event functions
#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "entity_state.h"
#include "cl_entity.h"

#include "r_efx.h"

#include "eventscripts.h"
#include "event_api.h"
#include "pm_shared.h"
#include "events.h"
#include "com_model.h"
#include "studio.h"
#include "r_studioint.h"
#include "view_ads.h"

#include <cmath>

extern engine_studio_api_t IEngineStudio;
extern vec3_t v_origin, v_angles;

int g_iRShell, g_iPShell, g_iShotgunShell;

static cvar_t *s_shellDebug;
struct ShellDebugRecord
{
	TEMPENTITY *temp;
	float die;
	int id, samples;
};
static ShellDebugRecord s_shellRecords[64] = {};
static int s_shellSerial;

bool EV_ShellDebugEnabled()
{
	return s_shellDebug && s_shellDebug->value != 0.0f;
}

void EV_ShellDebugAllocation(TEMPENTITY *temp, int model, int soundtype,
	const float *origin, const float *velocity, float life)
{
	const int id = ++s_shellSerial;
	if (temp)
	{
		ShellDebugRecord &record = s_shellRecords[id % 64];
		record.temp = temp;
		record.die = temp->die;
		record.id = id;
		record.samples = 0;
	}
	const model_t *actual = temp ? temp->entity.model : NULL;
	gEngfuncs.Con_Printf("oc2shell alloc id=%d t=%.6f model=%d sound=%d life=%.3f ptr=%p actual=%s origin=%.4f,%.4f,%.4f vel=%.4f,%.4f,%.4f actualvel=%.4f,%.4f,%.4f\n",
		id, gEngfuncs.GetClientTime(), model, soundtype, life, (void *)temp,
		actual ? actual->name : "NULL", origin[0], origin[1], origin[2], velocity[0], velocity[1], velocity[2],
		temp ? temp->entity.baseline.origin[0] : 0.0f, temp ? temp->entity.baseline.origin[1] : 0.0f,
		temp ? temp->entity.baseline.origin[2] : 0.0f);
}

void EV_ShellDebugFrame(TEMPENTITY *temp, int visible, double time)
{
	if (!EV_ShellDebugEnabled()) return;
	for (int i = 0; i < 64; ++i)
	{
		ShellDebugRecord &record = s_shellRecords[i];
		if (record.temp != temp || record.die != temp->die || record.samples >= 6) continue;
		++record.samples;
		gEngfuncs.Con_Printf("oc2shell frame id=%d t=%.6f ptr=%p visible=%d model=%s pos=%.4f,%.4f,%.4f vel=%.4f,%.4f,%.4f\n",
			record.id, time, (void *)temp, visible, temp->entity.model ? temp->entity.model->name : "NULL",
			temp->entity.origin[0], temp->entity.origin[1], temp->entity.origin[2],
			temp->entity.baseline.origin[0], temp->entity.baseline.origin[1], temp->entity.baseline.origin[2]);
		return;
	}
}

static bool EV_GetScoutVirtualPort(cl_entity_t *view, studiohdr_t *header,
	int attachment, Vector &port)
{
	if (attachment > 1 || header->numattachments != 0 ||
		HUD_GetWeapon() != WEAPON_SCOUT ||
		stricmp(view->model->name, "models/v_scout.mdl") ||
		IEngineStudio.GetCurrentEntity() != view || !IEngineStudio.StudioGetBoneTransform() ||
		header->numbones <= 0 || header->numbones > MAXSTUDIOBONES ||
		header->boneindex < 0 ||
		header->boneindex > header->length - header->numbones * int(sizeof(mstudiobone_t)))
		return false;

	const mstudiobone_t *bones = (const mstudiobone_t *)((const byte *)header + header->boneindex);
	int parent = -1;
	for (int i = 0; i < header->numbones; ++i)
		if (!stricmp(bones[i].name, "v_weapon.ssg08_parent")) { parent = i; break; }
	if (parent < 0) return false;

	// The current Scout lost its two named ports. These positions come from
	// the source rig's flash/shell bones, mirrored with the current right hand.
	const Vector local = attachment == 0 ?
		Vector(0.221143f, 2.159587f, 35.395420f) :
		Vector(-0.254403f, 2.297265f, 8.667261f);
	float (*bonesWorld)[MAXSTUDIOBONES][3][4] =
		(float (*)[MAXSTUDIOBONES][3][4])IEngineStudio.StudioGetBoneTransform();
	for (int axis = 0; axis < 3; ++axis)
		port[axis] = (*bonesWorld)[parent][axis][3] +
			(*bonesWorld)[parent][axis][0] * local.x +
			(*bonesWorld)[parent][axis][1] * local.y +
			(*bonesWorld)[parent][axis][2] * local.z;
	return true;
}

bool EV_GetViewModelAttachmentPoint( int player, int attachment, float *worldPoint )
{
	if ((player != 0 && !EV_IsLocal(player)) || !worldPoint || attachment < 0 || attachment >= 4)
		return false;

	cl_entity_t *view = gEngfuncs.GetViewModel();
	if (!view || !view->model || view->model->type != mod_studio)
		return false;
	studiohdr_t *header = static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(view->model));
	if (!header || header->numattachments < 0 || header->numattachments > 4)
		return false;

	Vector port;
	if (attachment < header->numattachments)
		port = Vector(view->attachment[attachment]);
	else if (!EV_GetScoutVirtualPort(view, header, attachment, port))
		return false;
	const Vector eye(v_origin);
	const Vector relative = port - eye;
	if (!std::isfinite(relative.x) || !std::isfinite(relative.y) ||
		!std::isfinite(relative.z) || relative.Length() > 128.0f ||
		(port - Vector(view->origin)).Length() > 128.0f)
		return false;

	Vector forward, right, up;
	AngleVectors(v_angles, forward, right, up);
	const float depth = DotProduct(relative, forward);
	if (depth < 1.0f)
		return false;

	// The viewmodel is rendered with its own projection. Temporary entities
	// use the world projection, so preserve the port's screen position there.
	const float worldFov = V_CurrentADSWorldFOV();
	const float modelFov = gEngfuncs.pfnGetCvarFloat("_oc2_viewmodel_fov");
	float scale = 1.0f;
	if (worldFov >= 10.0f && worldFov <= 150.0f && modelFov >= 30.0f && modelFov <= 120.0f)
	{
		const float radians = 3.14159265358979323846f / 360.0f;
		scale = tanf(worldFov * radians) / tanf(modelFov * radians);
	}
	if (!std::isfinite(scale) || scale < 0.2f || scale > 3.5f)
		return false;
	const Vector adjusted = eye + forward * depth +
		right * (DotProduct(relative, right) * scale) +
		up * (DotProduct(relative, up) * scale);
	if (!std::isfinite(adjusted.x) || !std::isfinite(adjusted.y) || !std::isfinite(adjusted.z))
		return false;
	for (int i = 0; i < 3; ++i) worldPoint[i] = adjusted[i];
	return true;
}

namespace
{
struct ViewModelTracer
{
	model_t *model;
	int weapon, attachment;
	double time;
	Vector end;
};

ViewModelTracer s_viewModelTracers[64];
int s_viewModelTracerCount = 0;

void EV_ExactMuzzleTracer(const Vector &start, const Vector &end)
{
	const Vector delta = end - start;
	const float distance = delta.Length();
	if (!std::isfinite(distance) || distance <= 0.0f)
		return;
	float speed = gEngfuncs.pfnGetCvarFloat("tracerspeed");
	if (!std::isfinite(speed)) speed = 6000.0f;
	if (speed < 3.0f) speed = 3.0f;
	Vector velocity = delta * (speed / distance);
	Vector origin = start;
	// R_TracerEffect adds a random [-10, 9] + traceroffset (default 30).
	// The allocator preserves its speed, length and color without moving the muzzle.
	gEngfuncs.pEfxAPI->R_TracerParticles(origin, velocity, distance / speed);
}
}

void EV_ClearViewModelTracers()
{
	s_viewModelTracerCount = 0;
}

void EV_FireWeaponTracer(int player, int attachment, const float *fallbackStart, const float *end)
{
	if (!EV_IsLocal(player))
	{
		Vector origin(fallbackStart[0], fallbackStart[1], fallbackStart[2]);
		Vector target(end[0], end[1], end[2]);
		gEngfuncs.pEfxAPI->R_TracerEffect(origin, target);
		return;
	}

	cl_entity_t *view = gEngfuncs.GetViewModel();
	const double time = gEngfuncs.GetClientTime();
	if (!view || !view->model || attachment < 0 || attachment >= 4 || !std::isfinite(time))
		return;
	if (s_viewModelTracerCount >= int(sizeof(s_viewModelTracers) / sizeof(s_viewModelTracers[0])))
		return;
	ViewModelTracer &tracer = s_viewModelTracers[s_viewModelTracerCount++];
	tracer.model = view->model;
	tracer.weapon = HUD_GetWeapon();
	tracer.attachment = attachment;
	tracer.time = time;
	tracer.end = Vector(end[0], end[1], end[2]);
}

void EV_FlushViewModelTracers(cl_entity_t *view)
{
	if (view != gEngfuncs.GetViewModel() || IEngineStudio.GetCurrentEntity() != view)
		return;
	const int count = s_viewModelTracerCount;
	s_viewModelTracerCount = 0;
	const double time = gEngfuncs.GetClientTime();
	for (int i = 0; i < count; ++i)
	{
		const ViewModelTracer &tracer = s_viewModelTracers[i];
		if (tracer.model != view->model || tracer.weapon != HUD_GetWeapon() ||
			!std::isfinite(time) || time < tracer.time || time - tracer.time > 0.25)
			continue;
		Vector muzzle;
		// The STUDIO_EVENTS pass has just calculated this frame's bones and
		// attachments. Share the same FOV correction as HUD_ModelMuzzleFlash.
		if (EV_GetViewModelAttachmentPoint(0, tracer.attachment, muzzle))
			EV_ExactMuzzleTracer(muzzle, tracer.end);
	}
}

/*
======================
Game_HookEvents

Associate script file name with callback functions.  Callback's must be extern "C" so
 the engine doesn't get confused about name mangling stuff.  Note that the format is
 always the same.  Of course, a clever mod team could actually embed parameters, behavior
 into the actual .sc files and create a .sc file parser and hook their functionality through
 that.. i.e., a scripting system.

That was what we were going to do, but we ran out of time...oh well.
======================
*/
void Game_HookEvents( void )
{
	s_shellDebug = CVAR_CREATE("oc2_shell_debug", "0", 0);
	HOOK_EVENT( ak47, FireAK47 );
	HOOK_EVENT( aug, FireAUG );
	HOOK_EVENT( awp, FireAWP );
	HOOK_EVENT( createexplo, CreateExplo );
	HOOK_EVENT( createsmoke, CreateSmoke );
	HOOK_EVENT( deagle, FireDEAGLE );
	HOOK_EVENT( decal_reset, DecalReset );
	HOOK_EVENT( elite_left, FireEliteLeft );
	HOOK_EVENT( elite_right, FireEliteRight );
	HOOK_EVENT( famas, FireFAMAS );
	HOOK_EVENT( fiveseven, Fire57 );
	HOOK_EVENT( g3sg1, FireG3SG1 );
	HOOK_EVENT( galil, FireGALIL );
	HOOK_EVENT( glock18, Fireglock18 );
	HOOK_EVENT( knife, Knife );
	HOOK_EVENT( m249, FireM249 );
	HOOK_EVENT( m3, FireM3 );
	HOOK_EVENT( m4a1, FireM4A1 );
	HOOK_EVENT( mac10, FireMAC10 );
	HOOK_EVENT( mp5n, FireMP5 );
	HOOK_EVENT( p228, FireP228 );
	HOOK_EVENT( p90, FireP90 );
	HOOK_EVENT( scout, FireScout );
	HOOK_EVENT( sg550, FireSG550 );
	HOOK_EVENT( sg552, FireSG552 );
	HOOK_EVENT( tmp, FireTMP );
	HOOK_EVENT( ump45, FireUMP45 );
	HOOK_EVENT( usp, FireUSP );
	HOOK_EVENT( vehicle, Vehicle );
	HOOK_EVENT( xm1014, FireXM1014 );

	if( !stricmp( gEngfuncs.pfnGetGameDirectory(), "czeror" ) )
	{
		HOOK_EVENT( m60, FireM60 );
		HOOK_EVENT( camera, FireCamera );
		HOOK_EVENT( fiberopticcamera, FireFiberOpticCamera );
		HOOK_EVENT( shieldgun, FireShieldGun );
		HOOK_EVENT( blowtorchholster, HolsterBlowtorch );
		HOOK_EVENT( blowtorchidle, IdleBlowtorch );
		HOOK_EVENT( blowtorch, FireBlowtorch );
		HOOK_EVENT( laws, FireLaws );
		HOOK_EVENT( briefcase, FireBriefcase );
		HOOK_EVENT( medkit, FireMedkit );
		HOOK_EVENT( syringe, FireSyringe );
		HOOK_EVENT( radio, FireRadio );
		HOOK_EVENT( zipline, FireZipline );
		HOOK_EVENT( create_glass, CreateGlass );
		HOOK_EVENT( explosion, GrenadeExplosion );
	}
}

/*
=================
EV_GetGunPosition

Figure out the height of the gun
=================
*/
void EV_GetGunPosition( event_args_t *args, Vector &pos, const Vector &origin )
{
	int idx;
	Vector view_ofs( 0, 0, DEFAULT_VIEWHEIGHT );

	idx = args->entindex;

	if ( EV_IsPlayer( idx ) )
	{
		// in spec mode use entity viewheigh, not own
		if ( EV_IsLocal( idx ) && !IS_FIRSTPERSON_SPEC )
		{
			// Grab predicted result for local player
			gEngfuncs.pEventAPI->EV_LocalPlayerViewheight( view_ofs );
		}
		else if ( args->ducking == 1 )
		{
			view_ofs[2] = VEC_DUCK_VIEW;
		}
	}

	pos = origin + view_ofs;
}

/*
=================
EV_GetDefaultShellInfo

Determine where to eject shells from
=================
*/
void EV_GetDefaultShellInfo( event_args_t *args, float *origin, float *velocity, float *ShellVelocity, float *ShellOrigin, float *forward, float *right, float *up, float forwardScale, float upScale, float rightScale, bool bReverseDirection )
{
	int idx = args->entindex;

	vec3_t view_ofs = { 0, 0, DEFAULT_VIEWHEIGHT };
	if ( EV_IsPlayer( idx ) )
	{
		if ( EV_IsLocal( idx ) )
		{
			gEngfuncs.pEventAPI->EV_LocalPlayerViewheight( view_ofs );
		}
		else if ( args->ducking == 1 )
		{
			view_ofs[2] = VEC_DUCK_VIEW;
		}
	}

	// Most of the impulse is away from the ejection port. The old forward
	// impulse could exceed the sideways impulse by four times.
	const float fR = gEngfuncs.pfnRandomFloat( 70, 100 );
	const float fU = gEngfuncs.pfnRandomFloat( 75, 125 );
	const float fF = gEngfuncs.pfnRandomFloat( -15, 40 );
	const bool local = EV_IsLocal(idx);
	const float fDirection = local && gHUD.cl_righthand && gHUD.cl_righthand->value == 0.0f ? -1.0f : 1.0f;
	const float fallbackRightScale = local && HUD_GetWeapon() == WEAPON_ELITE ?
		-rightScale * fDirection : rightScale;
	// The legacy CZ reverse hint describes its old offset convention. The
	// actual viewmodel hand now determines ejection side for every firearm.
	(void)bReverseDirection;

	for ( int i = 0; i < 3; i++ )
	{
		ShellVelocity[i] = velocity[i] + right[i] * fR * fDirection + up[i] * fU + forward[i] * fF;
		ShellOrigin[i]   = velocity[i] * 0.1f + origin[i] + view_ofs[i] +
				upScale * up[i] + forwardScale * forward[i] + fallbackRightScale * right[i];
	}
	if (local)
	{
		// Current OC2 viewmodels use attachment 1 for the ejection port.
		// Dual Berettas use attachments 2/3 for the left/right ports.
		const int port = HUD_GetWeapon() == WEAPON_ELITE ?
			(rightScale < 0.0f ? 3 : 2) : 1;
		EV_GetViewModelAttachmentPoint(idx, port, ShellOrigin);
	}
}
