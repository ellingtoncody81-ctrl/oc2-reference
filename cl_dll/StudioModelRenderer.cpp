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

// Big thanks to Chicken Fortress developers
// for this code.
#include <assert.h>
#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "com_model.h"
#include "studio.h"
#include "entity_state.h"
#include "cl_entity.h"
#include "dlight.h"
#include "triangleapi.h"

#include <stdio.h>
#include <string.h>
#include <memory.h>
#include <math.h>

#include "studio_util.h"
#include "r_studioint.h"

#include "StudioModelRenderer.h"
#include "GameStudioModelRenderer.h"

#include "event_api.h"
#include "pm_defs.h"

#include "com_weapons.h"
#include "viewmodel_transition.h"
#include "wpn_shared/oc2_presentation.h"
#include "view_ads.h"

extern CGameStudioModelRenderer g_StudioRenderer;
extern engine_studio_api_t IEngineStudio;
typedef struct pmtrace_s pmtrace_t;

static oc2_viewmodel::Transitioner g_viewTransitions;
static bool g_viewFirstEvents;
static model_t *g_viewEventModel;
static int g_viewEventSequence = -1;
static double g_viewEventStart = -1;
struct ViewFlowDiagnostics
{
	model_t *model = NULL;
	double time = 0, start = 0;
	int sequence = 0, first = 0, changed = 0, layers = 0, snap = 0, firstEvents = 0;
	float frame = 0, rawHand[3] = {}, blendedHand[3] = {};
	int oldSequence[2] = {-1,-1};
	float oldFrame[2] = {}, weight[2] = {};
} g_viewFlow;

static int ViewModelFirstFrame(model_t *model, studiohdr_t *header, int sequence)
{
	if (!model || !header || sequence < 0 || sequence >= header->numseq) return 0;
	int weapon = !stricmp(model->name,"models/v_m4a1.mdl") ? 22 :
		!stricmp(model->name,"models/v_glock18.mdl") ? 17 :
		!stricmp(model->name,"models/v_m249.mdl") ? 20 :
		!stricmp(model->name,"models/v_molotov.mdl") ? 31 : 0;
	const oc2_presentation::Sequence *entry = oc2_presentation::Find(weapon,sequence);
	const mstudioseqdesc_t &seq = ((mstudioseqdesc_t *)((byte *)header+header->seqindex))[sequence];
	if (!entry || entry->frames != seq.numframes || fabs(entry->fps-seq.fps) > .001f) return 0;
	return oc2_presentation::FirstFrame(weapon,sequence);
}

static void ViewModelHandPosition(studiohdr_t *header, float pos[][3], vec4_t q[], float result[3])
{
	const mstudiobone_t *bones = (const mstudiobone_t *)((byte *)header+header->boneindex);
	int hand = -1;
	for (int i=0; i<header->numbones; ++i)
		if (!stricmp(bones[i].name,"v_weapon.Bip01_R_Hand")) { hand=i; break; }
	if (hand < 0) { result[0]=result[1]=result[2]=0; return; }
	int chain[MAXSTUDIOBONES],count=0;
	for (int i=hand; i>=0 && i<header->numbones && count<MAXSTUDIOBONES; i=bones[i].parent) chain[count++]=i;
	float accumulated[3][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0}};
	for (int index=count-1; index>=0; --index)
	{
		const int bone=chain[index]; float local[3][4], next[3][4];
		QuaternionMatrix(q[bone],local);
		for (int axis=0; axis<3; ++axis) local[axis][3]=pos[bone][axis];
		ConcatTransforms(accumulated,local,next); memcpy(accumulated,next,sizeof(next));
	}
	for (int axis=0; axis<3; ++axis) result[axis]=accumulated[axis][3];
}

static void DumpViewModelFlow()
{
	gEngfuncs.Con_Printf("weaponflow: model=%s t=%.6f start=%.6f seq=%d first=%d frame=%.6f change=%d layers=%d snap=%d first_events=%d old0=%d oldframe0=%.6f weight0=%.6f old1=%d oldframe1=%.6f weight1=%.6f raw_hand=%.6f,%.6f,%.6f blended_hand=%.6f,%.6f,%.6f\n",
		g_viewFlow.model ? g_viewFlow.model->name : "none",g_viewFlow.time,g_viewFlow.start,g_viewFlow.sequence,g_viewFlow.first,g_viewFlow.frame,
		g_viewFlow.changed,g_viewFlow.layers,g_viewFlow.snap,g_viewFlow.firstEvents,g_viewFlow.oldSequence[0],g_viewFlow.oldFrame[0],g_viewFlow.weight[0],
		g_viewFlow.oldSequence[1],g_viewFlow.oldFrame[1],g_viewFlow.weight[1],
		g_viewFlow.rawHand[0],g_viewFlow.rawHand[1],g_viewFlow.rawHand[2],g_viewFlow.blendedHand[0],g_viewFlow.blendedHand[1],g_viewFlow.blendedHand[2]);
}

static model_t *g_lastArmsWeapon;
static int g_lastArmsDrawn;

static bool StudioRange(const studiohdr_t *header, int offset, int count, int size)
{
	return header && offset >= 0 && count >= 0 && size > 0 && offset <= header->length &&
		count <= (header->length - offset) / size;
}

static bool HasSourceHandRig(const studiohdr_t *header)
{
	if (!header || header->numbones <= 0 || header->numbones > MAXSTUDIOBONES ||
		!StudioRange(header, header->boneindex, header->numbones, sizeof(mstudiobone_t))) return false;
	const mstudiobone_t *bones = (const mstudiobone_t *)((const byte *)header + header->boneindex);
	bool right = false, left = false;
	for (int i = 0; i < header->numbones; ++i)
	{
		if (!stricmp(bones[i].name, "v_weapon.Bip01_R_Hand")) right = true;
		if (!stricmp(bones[i].name, "v_weapon.Bip01_L_Hand")) left = true;
	}
	return right && left;
}

static bool MirrorViewModel(const studiohdr_t *header)
{
	const bool rightHand = !gHUD.cl_righthand || gHUD.cl_righthand->value > 0;
	// Source-converted APK rigs are authored right-handed (idle R_Hand Y<0).
	// Original GoldSrc rigs use the opposite authoring convention. The GL
	// renderer uses the same rig predicate for winding and lighting.
	return HasSourceHandRig(header) ? !rightHand : rightHand;
}

struct ViewModelArmsInfo
{
	bool sourceRig, placeholder, nativeArms;
	bool NeedsArms() const { return sourceRig && placeholder && !nativeArms; }
};

static ViewModelArmsInfo InspectViewModelArms(const studiohdr_t *header, int body, int skin)
{
	ViewModelArmsInfo info = { HasSourceHandRig(header), false, false };
	if (!info.sourceRig || !StudioRange(header, header->bodypartindex, header->numbodyparts, sizeof(mstudiobodyparts_t))) return info;
	const byte *data = (const byte *)header;
	const mstudiobone_t *bones = (const mstudiobone_t *)(data + header->boneindex);
	bool armBone[MAXSTUDIOBONES] = {};
	for (int i = 0; i < header->numbones; ++i)
		armBone[i] = strstr(bones[i].name, "Bip01_") && (strstr(bones[i].name, "Hand") ||
			strstr(bones[i].name, "Finger") || strstr(bones[i].name, "Forearm") || strstr(bones[i].name, "UpperArm"));
	const mstudiobodyparts_t *parts = (const mstudiobodyparts_t *)(data + header->bodypartindex);
	const bool texturesValid = StudioRange(header, header->textureindex, header->numtextures, sizeof(mstudiotexture_t)) &&
		header->numtextures > 0 && header->numskinfamilies > 0 && header->numskinref > 0 &&
		StudioRange(header, header->skinindex, header->numskinref, sizeof(short));
	const mstudiotexture_t *textures = texturesValid ? (const mstudiotexture_t *)(data + header->textureindex) : NULL;
	if (skin < 0 || skin >= header->numskinfamilies) skin = 0;
	const bool skinValid = texturesValid && skin <= (header->length - header->skinindex) / (header->numskinref * int(sizeof(short))) - 1;
	const short *skinref = skinValid ? (const short *)(data + header->skinindex) + skin * header->numskinref : NULL;
	for (int part = 0; part < header->numbodyparts; ++part)
	{
		const mstudiobodyparts_t &group = parts[part];
		if (group.base <= 0 || group.nummodels <= 0 || !StudioRange(header, group.modelindex, group.nummodels, sizeof(mstudiomodel_t)))
			{ info.nativeArms = true; return info; }
		const int selected = (body >= 0 ? body / group.base : 0) % group.nummodels;
		const mstudiomodel_t &model = ((const mstudiomodel_t *)(data + group.modelindex))[selected];
		if (!StudioRange(header, model.vertinfoindex, model.numverts, 1) ||
			!StudioRange(header, model.meshindex, model.nummesh, sizeof(mstudiomesh_t)))
			{ info.nativeArms = true; return info; }
		const byte *vertexBones = data + model.vertinfoindex;
		bool hasArmVertices = false;
		for (int vertex = 0; vertex < model.numverts; ++vertex)
		{
			if (vertexBones[vertex] >= header->numbones) { info.nativeArms = true; return info; }
			if (armBone[vertexBones[vertex]]) hasArmVertices = true;
		}
		if (!hasArmVertices || model.nummesh == 0) continue;
		const bool placeholderName = !stricmp(model.name, "null") || !stricmp(model.name, "hand\\null") ||
			!stricmp(model.name, "cs_arms_reference");
		bool invisiblePlaceholder = placeholderName && skinref;
		const mstudiomesh_t *meshes = (const mstudiomesh_t *)(data + model.meshindex);
		for (int mesh = 0; invisiblePlaceholder && mesh < model.nummesh; ++mesh)
		{
			const int ref = meshes[mesh].skinref;
			if (ref < 0 || ref >= header->numskinref || skinref[ref] < 0 || skinref[ref] >= header->numtextures)
				{ invisiblePlaceholder = false; break; }
			const mstudiotexture_t &texture = textures[skinref[ref]];
			// Resource inspection confirmed these named additive/chrome
			// placeholder textures have black palettes. An opaque replacement
			// or a real hand mesh must retain its own arms, without an overlay.
			const bool name = !stricmp(texture.name, "null.bmp") ||
				((!stricmp(texture.name, "view_glove.bmp") || !stricmp(texture.name, "view_finger.bmp") ||
				!stricmp(texture.name, "view_skin.bmp")) && texture.width == 8 && texture.height == 8);
			// This older client SDK omits Xash's additive texture flag (0x20).
			if (!name || !(texture.flags & 0x0020) || !(texture.flags & STUDIO_NF_CHROME)) invisiblePlaceholder = false;
		}
		if (invisiblePlaceholder) info.placeholder = true;
		else info.nativeArms = true;
	}
	return info;
}

static bool ArmMeshBonesMatch(const studiohdr_t *arms, const CStudioModelRenderer &renderer)
{
	if (!arms || arms->numbones <= 0 || arms->numbones > MAXSTUDIOBONES || arms->numseq <= 0 ||
		!StudioRange(arms, arms->boneindex, arms->numbones, sizeof(mstudiobone_t)) ||
		!StudioRange(arms, arms->bodypartindex, arms->numbodyparts, sizeof(mstudiobodyparts_t))) return false;
	const byte *data = (const byte *)arms;
	const mstudiobone_t *bones = (const mstudiobone_t *)(data + arms->boneindex);
	bool matched[MAXSTUDIOBONES] = {};
	for (int i = 0; i < arms->numbones; ++i)
		for (int j = 0; j < renderer.m_nCachedBones; ++j)
			if (!stricmp(bones[i].name, renderer.m_nCachedBoneNames[j])) { matched[i] = true; break; }
	const mstudiobodyparts_t *parts = (const mstudiobodyparts_t *)(data + arms->bodypartindex);
	for (int part = 0; part < arms->numbodyparts; ++part)
	{
		if (!StudioRange(arms, parts[part].modelindex, parts[part].nummodels, sizeof(mstudiomodel_t))) return false;
		const mstudiomodel_t *models = (const mstudiomodel_t *)(data + parts[part].modelindex);
		for (int model = 0; model < parts[part].nummodels; ++model)
		{
			if (!StudioRange(arms, models[model].vertinfoindex, models[model].numverts, 1)) return false;
			const byte *vertexBones = data + models[model].vertinfoindex;
			for (int vertex = 0; vertex < models[model].numverts; ++vertex)
				if (vertexBones[vertex] >= arms->numbones || !matched[vertexBones[vertex]]) return false;
		}
	}
	return true;
}

static void DumpOC2ViewModel()
{
	cl_entity_t *view = gEngfuncs.GetViewModel();
	studiohdr_t *header = view && view->model && view->model->type == mod_studio ?
		(studiohdr_t *)IEngineStudio.Mod_Extradata(view->model) : NULL;
	if (!header) { gEngfuncs.Con_Printf("oc2viewmodel: model=none\n"); return; }
	const ViewModelArmsInfo arms = InspectViewModelArms(header, view->curstate.body, view->curstate.skin);
	float animFrame = -1, animFps = 0;
	int animFrames = 0, frameCount;
	double renderTime, oldTime, animElapsed = 0;
	IEngineStudio.GetTimes(&frameCount, &renderTime, &oldTime);
	if (header->numseq > 0 && StudioRange(header, header->seqindex, header->numseq, sizeof(mstudioseqdesc_t)))
	{
		const int sequence = view->curstate.sequence >= 0 && view->curstate.sequence < header->numseq ? view->curstate.sequence : 0;
		mstudioseqdesc_t *seq = (mstudioseqdesc_t *)((byte *)header + header->seqindex) + sequence;
		animFrames = seq->numframes;
		animFps = seq->fps * view->curstate.framerate;
		animElapsed = renderTime >= view->curstate.animtime ? renderTime - view->curstate.animtime : 0;
		// Estimate the real sequence sample at the studio renderer's time,
		// using the weapon header, never the last merged glove/sleeve header.
		// StudioEstimateFrame only reads these fields; restore all three.
		cl_entity_t *const savedEntity = g_StudioRenderer.m_pCurrentEntity;
		studiohdr_t *const savedHeader = g_StudioRenderer.m_pStudioHeader;
		const double savedTime = g_StudioRenderer.m_clTime;
		g_StudioRenderer.m_pCurrentEntity = view;
		g_StudioRenderer.m_pStudioHeader = header;
		g_StudioRenderer.m_clTime = renderTime;
		animFrame = g_StudioRenderer.StudioEstimateFrame(seq);
		g_StudioRenderer.m_pCurrentEntity = savedEntity;
		g_StudioRenderer.m_pStudioHeader = savedHeader;
		g_StudioRenderer.m_clTime = savedTime;
	}
	float screenX = 0;
	bool handFound = false;
	if (g_StudioRenderer.m_pCurrentEntity == view && g_StudioRenderer.m_pStudioHeader == header)
		for (int i = 0; i < g_StudioRenderer.m_nCachedBones; ++i)
			if (!stricmp(g_StudioRenderer.m_nCachedBoneNames[i], "v_weapon.Bip01_R_Hand"))
			{
				for (int axis = 0; axis < 3; ++axis)
					screenX += (g_StudioRenderer.m_rgCachedBoneTransform[i][axis][3] - g_StudioRenderer.m_vRenderOrigin[axis]) * g_StudioRenderer.m_vRight[axis];
				handFound = true; break;
			}
	gEngfuncs.Con_Printf("oc2viewmodel: model=%s body=%d skin=%d seq=%d frame=%.3f anim_frame=%.3f anim_fps=%.3f anim_frames=%d anim_elapsed=%.6f right=%.3f mirror=%d source_rig=%d placeholder=%d native_arms=%d extra_arms=%d drawn_parts=%d hand_valid=%d hand_screen_x=%.3f\n",
		view->model->name, view->curstate.body, view->curstate.skin, view->curstate.sequence, view->curstate.frame,
		animFrame, animFps, animFrames, animElapsed,
		gHUD.cl_righthand ? gHUD.cl_righthand->value : 1.0f, int(MirrorViewModel(header)), int(arms.sourceRig),
		int(arms.placeholder), int(arms.nativeArms), int(arms.NeedsArms()),
		g_lastArmsWeapon == view->model ? g_lastArmsDrawn : 0, int(handFound), screenX);
}

void CStudioModelRenderer::Init(void)
{
	gEngfuncs.pfnAddCommand("oc2_viewmodel_dump", DumpOC2ViewModel);
	gEngfuncs.pfnAddCommand("weapon_flow_dump", DumpViewModelFlow);
	g_viewTransitions.Clear();
	g_viewFirstEvents = false;
	g_viewEventModel = NULL; g_viewEventSequence = -1; g_viewEventStart = -1;
	m_pCvarHiModels = IEngineStudio.GetCvar("cl_himodels");
	m_pCvarDeveloper = IEngineStudio.GetCvar("developer");
	m_pCvarDrawEntities = IEngineStudio.GetCvar("r_drawentities");
	m_pCvarShadows = CVAR_CREATE("cl_shadows", "1", FCVAR_ARCHIVE );
#ifndef NDEBUG
	m_pCvarDebug = CVAR_CREATE( "r_smr_debug", "0", 0 );
#endif
	m_pChromeSprite = IEngineStudio.GetChromeSprite();

	IEngineStudio.GetModelCounters(&m_pStudioModelCount, &m_pModelsDrawn);

	m_pbonetransform = (float (*)[MAXSTUDIOBONES][3][4])IEngineStudio.StudioGetBoneTransform();
	m_plighttransform = (float (*)[MAXSTUDIOBONES][3][4])IEngineStudio.StudioGetLightTransform();
	m_paliastransform = (float (*)[3][4])IEngineStudio.StudioGetAliasTransform();
	m_protationmatrix = (float (*)[3][4])IEngineStudio.StudioGetRotationMatrix();
}

CStudioModelRenderer::CStudioModelRenderer(void)
{
	m_fDoInterp = 1;
	m_fGaitEstimation = 1;
	m_pCurrentEntity = NULL;
	m_pCvarHiModels = NULL;
	m_pCvarDeveloper = NULL;
	m_pCvarDrawEntities = NULL;
	m_pChromeSprite = NULL;
	m_pStudioModelCount = NULL;
	m_pModelsDrawn = NULL;
	m_protationmatrix = NULL;
	m_paliastransform = NULL;
	m_pbonetransform = NULL;
	m_plighttransform = NULL;
	m_pStudioHeader = NULL;
	m_pBodyPart = NULL;
	m_pSubModel = NULL;
	m_pPlayerInfo = NULL;
	m_pRenderModel = NULL;
	m_iShadowSprite = 0;
}

CStudioModelRenderer::~CStudioModelRenderer(void)
{
}

void CStudioModelRenderer::StudioCalcBoneAdj(float dadt, float *adj, const byte *pcontroller1, const byte *pcontroller2, byte mouthopen)
{
	int i, j;
	float value;
	mstudiobonecontroller_t *pbonecontroller;

	pbonecontroller = (mstudiobonecontroller_t *)((byte *)m_pStudioHeader + m_pStudioHeader->bonecontrollerindex);

	for (j = 0; j < m_pStudioHeader->numbonecontrollers; j++)
	{
		i = pbonecontroller[j].index;

		if (i <= 3)
		{
			if (pbonecontroller[j].type & STUDIO_RLOOP)
			{
				if (abs(pcontroller1[i] - pcontroller2[i]) > 128)
				{
					int a, b;
					a = (pcontroller1[j] + 128) % 256;
					b = (pcontroller2[j] + 128) % 256;
					value = ((a * dadt) + (b * (1 - dadt)) - 128) * (360.0 / 256.0) + pbonecontroller[j].start;
				}
				else
				{
					value = ((pcontroller1[i] * dadt + (pcontroller2[i]) * (1.0 - dadt))) * (360.0 / 256.0) + pbonecontroller[j].start;
				}
			}
			else
			{
				value = (pcontroller1[i] * dadt + pcontroller2[i] * (1.0 - dadt)) / 255.0;

				if (value < 0)
					value = 0;

				if (value > 1.0)
					value = 1.0;

				value = (1.0 - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
			}
		}
		else
		{
			value = mouthopen / 64.0;

			if (value > 1.0)
				value = 1.0;

			value = (1.0 - value) * pbonecontroller[j].start + value * pbonecontroller[j].end;
		}

		switch (pbonecontroller[j].type & STUDIO_TYPES)
		{
			case STUDIO_XR:
			case STUDIO_YR:
			case STUDIO_ZR:
			{
				adj[j] = value * (M_PI / 180.0);
				break;
			}
			case STUDIO_X:
			case STUDIO_Y:
			case STUDIO_Z:
			{
				adj[j] = value;
				break;
			}
		}
	}
}

void CStudioModelRenderer::StudioCalcBoneQuaterion(int frame, float s, mstudiobone_t *pbone, mstudioanim_t *panim, float *adj, float *q)
{
	int j, k;
	vec4_t q1, q2;
	vec3_t angle1, angle2;
	mstudioanimvalue_t *panimvalue;

	for (j = 0; j < 3; j++)
	{
		if (panim->offset[j + 3] == 0)
		{
			angle2[j] = angle1[j] = pbone->value[j + 3];
		}
		else
		{
			panimvalue = (mstudioanimvalue_t *)((byte *)panim + panim->offset[j + 3]);
			k = frame;

			if (panimvalue->num.total < panimvalue->num.valid)
				k = 0;

			while (panimvalue->num.total <= k)
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;

				if (panimvalue->num.total < panimvalue->num.valid)
					k = 0;
			}

			if (panimvalue->num.valid > k)
			{
				angle1[j] = panimvalue[k + 1].value;

				if (panimvalue->num.valid > k + 1)
				{
					angle2[j] = panimvalue[k + 2].value;
				}
				else
				{
					if (panimvalue->num.total > k + 1)
						angle2[j] = angle1[j];
					else
						angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
				}
			}
			else
			{
				angle1[j] = panimvalue[panimvalue->num.valid].value;

				if (panimvalue->num.total > k + 1)
					angle2[j] = angle1[j];
				else
					angle2[j] = panimvalue[panimvalue->num.valid + 2].value;
			}

			angle1[j] = pbone->value[j + 3] + angle1[j] * pbone->scale[j + 3];
			angle2[j] = pbone->value[j + 3] + angle2[j] * pbone->scale[j + 3];
		}

		if (pbone->bonecontroller[j + 3] != -1)
		{
			angle1[j] += adj[pbone->bonecontroller[j + 3]];
			angle2[j] += adj[pbone->bonecontroller[j + 3]];
		}
	}

	if (!VectorCompare(angle1, angle2))
	{
		AngleQuaternion(angle1, q1);
		AngleQuaternion(angle2, q2);
		QuaternionSlerp(q1, q2, s, q);
	}
	else
	{
		AngleQuaternion(angle1, q);
	}
}

void CStudioModelRenderer::StudioCalcBonePosition(int frame, float s, mstudiobone_t *pbone, mstudioanim_t *panim, float *adj, float *pos)
{
	int j, k;
	mstudioanimvalue_t *panimvalue;

	for (j = 0; j < 3; j++)
	{
		pos[j] = pbone->value[j];

		if (panim->offset[j] != 0)
		{
			panimvalue = (mstudioanimvalue_t *)((byte *)panim + panim->offset[j]);
			k = frame;

			if (panimvalue->num.total < panimvalue->num.valid)
				k = 0;

			while (panimvalue->num.total <= k)
			{
				k -= panimvalue->num.total;
				panimvalue += panimvalue->num.valid + 1;

				if (panimvalue->num.total < panimvalue->num.valid)
					k = 0;
			}

			if (panimvalue->num.valid > k)
			{
				if (panimvalue->num.valid > k + 1)
					pos[j] += (panimvalue[k + 1].value * (1.0 - s) + s * panimvalue[k + 2].value) * pbone->scale[j];
				else
					pos[j] += panimvalue[k + 1].value * pbone->scale[j];
			}
			else
			{
				if (panimvalue->num.total <= k + 1)
					pos[j] += (panimvalue[panimvalue->num.valid].value * (1.0 - s) + s * panimvalue[panimvalue->num.valid + 2].value) * pbone->scale[j];
				else
					pos[j] += panimvalue[panimvalue->num.valid].value * pbone->scale[j];
			}
		}

		if (pbone->bonecontroller[j] != -1 && adj)
			pos[j] += adj[pbone->bonecontroller[j]];
	}
}

void CStudioModelRenderer::StudioSlerpBones(vec4_t q1[], float pos1[][3], vec4_t q2[], float pos2[][3], float s)
{
	int i;
	vec4_t q3;
	float s1;

	if (s < 0)
		s = 0;
	else if (s > 1.0)
		s = 1.0;

	s1 = 1.0 - s;

	for (i = 0; i < m_pStudioHeader->numbones; i++)
	{
		QuaternionSlerp(q1[i], q2[i], s, q3);

		q1[i][0] = q3[0];
		q1[i][1] = q3[1];
		q1[i][2] = q3[2];
		q1[i][3] = q3[3];
		pos1[i][0] = pos1[i][0] * s1 + pos2[i][0] * s;
		pos1[i][1] = pos1[i][1] * s1 + pos2[i][1] * s;
		pos1[i][2] = pos1[i][2] * s1 + pos2[i][2] * s;
	}
}

mstudioanim_t *CStudioModelRenderer::StudioGetAnim(model_t *pSubModel, mstudioseqdesc_t *pseqdesc)
{
	mstudioseqgroup_t *pseqgroup;
	cache_user_t *paSequences;

	pseqgroup = (mstudioseqgroup_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqgroupindex) + pseqdesc->seqgroup;

	if (pseqdesc->seqgroup == 0)
		return (mstudioanim_t *)((byte *)m_pStudioHeader + pseqdesc->animindex);

	paSequences = (cache_user_t *)pSubModel->submodels;

	if (paSequences == NULL)
	{
		paSequences = (cache_user_t *)IEngineStudio.Mem_Calloc(16, sizeof(cache_user_t));
		pSubModel->submodels = (dmodel_t *)paSequences;
	}

	if (!IEngineStudio.Cache_Check((struct cache_user_s *)&(paSequences[pseqdesc->seqgroup])))
	{
		gEngfuncs.Con_DPrintf("loading %s\n", pseqgroup->name);
		IEngineStudio.LoadCacheFile(pseqgroup->name, (struct cache_user_s *)&paSequences[pseqdesc->seqgroup]);
	}

	return (mstudioanim_t *)((byte *)paSequences[pseqdesc->seqgroup].data + pseqdesc->animindex);
}

void CStudioModelRenderer::StudioPlayerBlend(mstudioseqdesc_t *pseqdesc, int *pBlend, float *pPitch)
{
	*pBlend = (*pPitch * 3);

	if (*pBlend < pseqdesc->blendstart[0])
	{
		*pPitch -= pseqdesc->blendstart[0] / 3.0;
		*pBlend = 0;
	}
	else if (*pBlend > pseqdesc->blendend[0])
	{
		*pPitch -= pseqdesc->blendend[0] / 3.0;
		*pBlend = 255;
	}
	else
	{
		if (pseqdesc->blendend[0] - pseqdesc->blendstart[0] < 0.1)
			*pBlend = 127;
		else
			*pBlend = 255 * (*pBlend - pseqdesc->blendstart[0]) / (pseqdesc->blendend[0] - pseqdesc->blendstart[0]);

		*pPitch = 0;
	}
}

void CStudioModelRenderer::StudioSetUpTransform(int trivial_accept)
{
	int i;
	vec3_t angles;
	vec3_t modelpos;

	VectorCopy(m_pCurrentEntity->origin, modelpos);

	angles[ROLL] = m_pCurrentEntity->curstate.angles[ROLL];
	angles[PITCH] = m_pCurrentEntity->curstate.angles[PITCH];
	angles[YAW] = m_pCurrentEntity->curstate.angles[YAW];

	if (m_pCurrentEntity->curstate.movetype != MOVETYPE_NONE)
	{
		VectorCopy(m_pCurrentEntity->angles, angles);
	}

	angles[PITCH] = -angles[PITCH];
	AngleMatrix(angles, (*m_protationmatrix));

	if (!IEngineStudio.IsHardware())
	{
		static float viewmatrix[3][4];

		VectorCopy(m_vRight, viewmatrix[0]);
		VectorCopy(m_vUp, viewmatrix[1]);
		VectorInverse(viewmatrix[1]);
		VectorCopy(m_vNormal, viewmatrix[2]);

		(*m_protationmatrix)[0][3] = modelpos[0] - m_vRenderOrigin[0];
		(*m_protationmatrix)[1][3] = modelpos[1] - m_vRenderOrigin[1];
		(*m_protationmatrix)[2][3] = modelpos[2] - m_vRenderOrigin[2];

		ConcatTransforms(viewmatrix, (*m_protationmatrix), (*m_paliastransform));

		if (trivial_accept)
		{
			for (i = 0; i < 4; i++)
			{
				(*m_paliastransform)[0][i] *= m_fSoftwareXScale * (1.0 / (ZISCALE * 0x10000));
				(*m_paliastransform)[1][i] *= m_fSoftwareYScale * (1.0 / (ZISCALE * 0x10000));
				(*m_paliastransform)[2][i] *= 1.0 / (ZISCALE * 0x10000);
			}
		}
	}

	(*m_protationmatrix)[0][3] = modelpos[0];
	(*m_protationmatrix)[1][3] = modelpos[1];
	(*m_protationmatrix)[2][3] = modelpos[2];
}

float CStudioModelRenderer::StudioEstimateInterpolant(void)
{
	float dadt = 1.0;

	if (m_fDoInterp && (m_pCurrentEntity->curstate.animtime >= m_pCurrentEntity->latched.prevanimtime + 0.01))
	{
		dadt = (m_clTime - m_pCurrentEntity->curstate.animtime) / 0.1;

		if (dadt > 2.0)
			dadt = 2.0;
	}

	return dadt;
}

void CStudioModelRenderer::StudioCalcRotations(float pos[][3], vec4_t *q, mstudioseqdesc_t *pseqdesc, mstudioanim_t *panim, float f)
{
	int i;
	int frame;
	mstudiobone_t *pbone;

	float s;
	float adj[MAXSTUDIOCONTROLLERS];
	float dadt;

	if (f > pseqdesc->numframes - 1)
		f = 0;
	else if (f < -0.01)
		f = -0.01;

	frame = (int)f;
	dadt = StudioEstimateInterpolant();
	s = (f - frame);

	pbone = (mstudiobone_t *)((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);

	StudioCalcBoneAdj(dadt, adj, m_pCurrentEntity->curstate.controller, m_pCurrentEntity->latched.prevcontroller, m_pCurrentEntity->mouth.mouthopen);

	for (i = 0; i < m_pStudioHeader->numbones; i++, pbone++, panim++)
	{
		StudioCalcBoneQuaterion(frame, s, pbone, panim, adj, q[i]);
		StudioCalcBonePosition(frame, s, pbone, panim, adj, pos[i]);
	}

	if (pseqdesc->motiontype & STUDIO_X)
		pos[pseqdesc->motionbone][0] = 0.0;

	if (pseqdesc->motiontype & STUDIO_Y)
		pos[pseqdesc->motionbone][1] = 0.0;

	if (pseqdesc->motiontype & STUDIO_Z)
		pos[pseqdesc->motionbone][2] = 0.0;

	s = 0 * ((1.0 - (f - (int)(f))) / (pseqdesc->numframes)) * m_pCurrentEntity->curstate.framerate;

	if (pseqdesc->motiontype & STUDIO_LX)
		pos[pseqdesc->motionbone][0] += s * pseqdesc->linearmovement[0];

	if (pseqdesc->motiontype & STUDIO_LY)
		pos[pseqdesc->motionbone][1] += s * pseqdesc->linearmovement[1];

	if (pseqdesc->motiontype & STUDIO_LZ)
		pos[pseqdesc->motionbone][2] += s * pseqdesc->linearmovement[2];
}

void CStudioModelRenderer::StudioFxTransform(cl_entity_t *ent, float transform[3][4])
{
	switch (ent->curstate.renderfx)
	{
		case kRenderFxDistort:
		case kRenderFxHologram:
		{
			if (Com_RandomLong(0, 49) == 0)
			{
				int axis = Com_RandomLong(0, 1);

				if (axis == 1)
					axis = 2;

				VectorScale( transform[axis], gEngfuncs.pfnRandomFloat(1,1.484), transform[axis] );
			}
			else if (Com_RandomLong(0, 49) == 0)
			{
				float offset;
				int axis = Com_RandomLong(0, 1);

				if (axis == 1)
					axis = 2;

				offset = gEngfuncs.pfnRandomFloat(-10, 10);
				transform[Com_RandomLong(0, 2)][3] += offset;
			}

			break;
		}

		case kRenderFxExplode:
		{
			float scale;

			scale = 1.0 + (m_clTime - ent->curstate.animtime) * 10.0;

			if (scale > 2)
				scale = 2;

			transform[0][1] *= scale;
			transform[1][1] *= scale;
			transform[2][1] *= scale;
			break;
		}
	}
}

float CStudioModelRenderer::StudioEstimateFrame(mstudioseqdesc_t *pseqdesc)
{
	double dfdt, f;

	if (m_fDoInterp)
	{
		if (m_clTime < m_pCurrentEntity->curstate.animtime)
			dfdt = 0;
		else
			dfdt = (m_clTime - m_pCurrentEntity->curstate.animtime) * m_pCurrentEntity->curstate.framerate * pseqdesc->fps;
	}
	else
		dfdt = 0;

	if (pseqdesc->numframes <= 1)
		f = 0;
	else
		f = (m_pCurrentEntity->curstate.frame * (pseqdesc->numframes - 1)) / 256.0;

	f += dfdt;

	if (pseqdesc->flags & STUDIO_LOOPING)
	{
		if (pseqdesc->numframes > 1)
			f -= (int)(f / (pseqdesc->numframes - 1)) * (pseqdesc->numframes - 1);

		if (f < 0)
			f += (pseqdesc->numframes - 1);
	}
	else
	{
		if (f >= pseqdesc->numframes - 1.001)
			f = pseqdesc->numframes - 1.001;

		if (f < 0.0)
			f = 0.0;
	}

	return f;
}

void CStudioModelRenderer::StudioSampleSequence(int sequence, float frame, int blend0, int blend1, float pos[][3], vec4_t q[])
{
	mstudioseqdesc_t *seq = (mstudioseqdesc_t *)((byte *)m_pStudioHeader+m_pStudioHeader->seqindex)+sequence;
	mstudioanim_t *anim = StudioGetAnim(m_pRenderModel,seq);
	StudioCalcRotations(pos,q,seq,anim,frame);
	if (seq->numblends <= 1) return;
	static float p2[MAXSTUDIOBONES][3],p3[MAXSTUDIOBONES][3],p4[MAXSTUDIOBONES][3];
	static vec4_t q2[MAXSTUDIOBONES],q3[MAXSTUDIOBONES],q4[MAXSTUDIOBONES];
	StudioCalcRotations(p2,q2,seq,anim+m_pStudioHeader->numbones,frame);
	StudioSlerpBones(q,pos,q2,p2,blend0/255.0f);
	if (seq->numblends == 4)
	{
		StudioCalcRotations(p3,q3,seq,anim+2*m_pStudioHeader->numbones,frame);
		StudioCalcRotations(p4,q4,seq,anim+3*m_pStudioHeader->numbones,frame);
		StudioSlerpBones(q3,p3,q4,p4,blend0/255.0f);
		StudioSlerpBones(q,pos,q3,p3,blend1/255.0f);
	}
}

void CStudioModelRenderer::StudioSetupBones(void)
{
	int i;
	double f;

	mstudiobone_t *pbones;
	mstudioseqdesc_t *pseqdesc;
	mstudioanim_t *panim;

	static float pos[MAXSTUDIOBONES][3];
	static vec4_t q[MAXSTUDIOBONES];
	float bonematrix[3][4];

	static float pos2[MAXSTUDIOBONES][3];
	static vec4_t q2[MAXSTUDIOBONES];
	static float pos3[MAXSTUDIOBONES][3];
	static vec4_t q3[MAXSTUDIOBONES];
	static float pos4[MAXSTUDIOBONES][3];
	static vec4_t q4[MAXSTUDIOBONES];

	if( m_pCurrentEntity->curstate.sequence >= m_pStudioHeader->numseq
		|| m_pCurrentEntity->curstate.sequence < 0 )
		m_pCurrentEntity->curstate.sequence = 0;

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + m_pCurrentEntity->curstate.sequence;

	f = StudioEstimateFrame(pseqdesc);
	panim = StudioGetAnim(m_pRenderModel, pseqdesc);

	StudioCalcRotations(pos, q, pseqdesc, panim, f);

	if (pseqdesc->numblends > 1)
	{
		float s;
		float dadt;

		panim += m_pStudioHeader->numbones;
		StudioCalcRotations(pos2, q2, pseqdesc, panim, f);

		dadt = StudioEstimateInterpolant();
		s = (m_pCurrentEntity->curstate.blending[0] * dadt + m_pCurrentEntity->latched.prevblending[0] * (1.0 - dadt)) / 255.0;

		StudioSlerpBones(q, pos, q2, pos2, s);

		if (pseqdesc->numblends == 4)
		{
			panim += m_pStudioHeader->numbones;
			StudioCalcRotations(pos3, q3, pseqdesc, panim, f);

			panim += m_pStudioHeader->numbones;
			StudioCalcRotations(pos4, q4, pseqdesc, panim, f);

			s = (m_pCurrentEntity->curstate.blending[0] * dadt + m_pCurrentEntity->latched.prevblending[0] * (1.0 - dadt)) / 255.0;
			StudioSlerpBones(q3, pos3, q4, pos4, s);

			s = (m_pCurrentEntity->curstate.blending[1] * dadt + m_pCurrentEntity->latched.prevblending[1] * (1.0 - dadt)) / 255.0;
			StudioSlerpBones(q, pos, q3, pos3, s);
		}
	}

	if (gEngfuncs.GetViewModel() == m_pCurrentEntity)
	{
		// Source tracks sequence restarts explicitly, advances departing cycles,
		// and clears history when the model changes. First-person viewents never
		// pass through GoldSrc network-entity CL_UpdateLatchedVars.
		oc2_viewmodel::Layer current;
		current.sequence=m_pCurrentEntity->curstate.sequence; current.frame=f;
		current.frames=pseqdesc->numframes; current.fps=pseqdesc->fps;
		current.rate=m_pCurrentEntity->curstate.framerate; current.loop=(pseqdesc->flags & STUDIO_LOOPING) != 0;
		current.sampleTime=m_clTime; current.startTime=m_pCurrentEntity->curstate.animtime;
		current.blend0=m_pCurrentEntity->curstate.blending[0]; current.blend1=m_pCurrentEntity->curstate.blending[1];
		// Source's STUDIO_SNAP clears transitions for immediate action clips.
		// The converted v10 resources omit this metadata: map their fire/knife
		// labels to that role so acquisition/reload smoothing never softens a shot.
		const bool snap = strstr(pseqdesc->label,"shoot") || strstr(pseqdesc->label,"fire") ||
			strstr(pseqdesc->label,"slash") || strstr(pseqdesc->label,"stab");
		const oc2_viewmodel::Transitioner::Change changed = g_viewTransitions.Observe(reinterpret_cast<uintptr_t>(m_pRenderModel),current,
			m_fDoInterp && !snap && !(m_pCurrentEntity->curstate.effects & EF_NOINTERP));
		// Hiding/pause clears blend history but must not replay old sounds.
		// First-frame events follow actual model/sequence/start identity only.
		if (g_viewEventModel != m_pRenderModel || g_viewEventSequence != current.sequence || g_viewEventStart != current.startTime)
		{
			g_viewFirstEvents=true; g_viewEventModel=m_pRenderModel;
			g_viewEventSequence=current.sequence; g_viewEventStart=current.startTime;
		}
		g_viewFlow.model=m_pRenderModel; g_viewFlow.time=m_clTime; g_viewFlow.start=current.startTime;
		g_viewFlow.sequence=current.sequence; g_viewFlow.frame=f; g_viewFlow.changed=changed; g_viewFlow.snap=snap;
		g_viewFlow.first=ViewModelFirstFrame(m_pRenderModel,m_pStudioHeader,current.sequence);
		g_viewFlow.layers=g_viewTransitions.Count();
		ViewModelHandPosition(m_pStudioHeader,pos,q,g_viewFlow.rawHand);
		static float oldPos[MAXSTUDIOBONES][3]; static vec4_t oldQ[MAXSTUDIOBONES];
		for (int layer=1; layer>=0; --layer)
		{
			g_viewFlow.oldSequence[layer]=-1; g_viewFlow.oldFrame[layer]=g_viewFlow.weight[layer]=0;
			if (layer >= g_viewTransitions.Count()) continue;
			const oc2_viewmodel::Layer &old=g_viewTransitions.Previous(layer);
			const float oldFrame=g_viewTransitions.PreviousFrame(layer,m_clTime);
			const float weight=g_viewTransitions.Weight(layer,m_clTime);
			g_viewFlow.oldSequence[layer]=old.sequence; g_viewFlow.oldFrame[layer]=oldFrame; g_viewFlow.weight[layer]=weight;
			if (old.sequence < 0 || old.sequence >= m_pStudioHeader->numseq) continue;
			StudioSampleSequence(old.sequence,oldFrame,old.blend0,old.blend1,oldPos,oldQ);
			StudioSlerpBones(q,pos,oldQ,oldPos,weight);
		}
		ViewModelHandPosition(m_pStudioHeader,pos,q,g_viewFlow.blendedHand);
	}
	else if (m_fDoInterp && m_pCurrentEntity->latched.sequencetime && (m_pCurrentEntity->latched.sequencetime + 0.2 > m_clTime) && (m_pCurrentEntity->latched.prevsequence >= 0) && (m_pCurrentEntity->latched.prevsequence < m_pStudioHeader->numseq))
	{
		static float pos1b[MAXSTUDIOBONES][3];
		static vec4_t q1b[MAXSTUDIOBONES];
		float s;

		pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + m_pCurrentEntity->latched.prevsequence;
		panim = StudioGetAnim(m_pRenderModel, pseqdesc);

		StudioCalcRotations(pos1b, q1b, pseqdesc, panim, m_pCurrentEntity->latched.prevframe);

		if (pseqdesc->numblends > 1)
		{
			panim += m_pStudioHeader->numbones;
			StudioCalcRotations(pos2, q2, pseqdesc, panim, m_pCurrentEntity->latched.prevframe);

			s = (m_pCurrentEntity->latched.prevseqblending[0]) / 255.0;
			StudioSlerpBones(q1b, pos1b, q2, pos2, s);

			if (pseqdesc->numblends == 4)
			{
				panim += m_pStudioHeader->numbones;
				StudioCalcRotations(pos3, q3, pseqdesc, panim, m_pCurrentEntity->latched.prevframe);

				panim += m_pStudioHeader->numbones;
				StudioCalcRotations(pos4, q4, pseqdesc, panim, m_pCurrentEntity->latched.prevframe);

				s = (m_pCurrentEntity->latched.prevseqblending[0]) / 255.0;
				StudioSlerpBones(q3, pos3, q4, pos4, s);

				s = (m_pCurrentEntity->latched.prevseqblending[1]) / 255.0;
				StudioSlerpBones(q1b, pos1b, q3, pos3, s);
			}
		}

		s = 1.0 - (m_clTime - m_pCurrentEntity->latched.sequencetime) / 0.2;
		StudioSlerpBones(q, pos, q1b, pos1b, s);
	}
	else
	{
		m_pCurrentEntity->latched.prevframe = f;
	}

	pbones = (mstudiobone_t *)((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);

	if (m_pPlayerInfo && m_pPlayerInfo->gaitsequence != 0)
	{
		pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + m_pPlayerInfo->gaitsequence;

		panim = StudioGetAnim(m_pRenderModel, pseqdesc);
		StudioCalcRotations(pos2, q2, pseqdesc, panim, m_pPlayerInfo->gaitframe);

		for (i = 0; i < m_pStudioHeader->numbones; i++)
		{
			if (strcmp(pbones[i].name, "Bip01 Spine") == 0)
				break;

			memcpy(pos[i], pos2[i], sizeof( pos[i]));
			memcpy(q[i], q2[i], sizeof( q[i]));
		}
	}

	bool bIsViewModel = gEngfuncs.GetViewModel() == m_pCurrentEntity;

	for (i = 0; i < m_pStudioHeader->numbones; i++)
	{
		QuaternionMatrix(q[i], bonematrix);

		bonematrix[0][3] = pos[i][0];
		bonematrix[1][3] = pos[i][1];
		bonematrix[2][3] = pos[i][2];

		if (pbones[i].parent == -1)
		{
			if (IEngineStudio.IsHardware())
			{
				// i know this looks HORRIBLE but I'm too lazy to simplify this right now
				if (bIsViewModel && MirrorViewModel(m_pStudioHeader))
				{
					bonematrix[1][0] = -bonematrix[1][0];
					bonematrix[1][1] = -bonematrix[1][1];
					bonematrix[1][2] = -bonematrix[1][2];
					bonematrix[1][3] = -bonematrix[1][3];
				}

				ConcatTransforms((*m_protationmatrix), bonematrix, (*m_pbonetransform)[i]);
				MatrixCopy((*m_pbonetransform)[i], (*m_plighttransform)[i]);
			}
			else
			{
				ConcatTransforms((*m_paliastransform), bonematrix, (*m_pbonetransform)[i]);
				ConcatTransforms((*m_protationmatrix), bonematrix, (*m_plighttransform)[i]);
			}

			StudioFxTransform(m_pCurrentEntity, (*m_pbonetransform)[i]);
		}
		else
		{
			ConcatTransforms((*m_pbonetransform)[pbones[i].parent], bonematrix, (*m_pbonetransform)[i]);
			ConcatTransforms((*m_plighttransform)[pbones[i].parent], bonematrix, (*m_plighttransform)[i]);
		}
	}
	if (gEngfuncs.GetViewModel() == m_pCurrentEntity)
		for (int bone=0; bone<m_pStudioHeader->numbones; ++bone)
			if (!stricmp(pbones[bone].name,"v_weapon.aug_Parent") || !stricmp(pbones[bone].name,"v_weapon.sg556_Parent"))
				V_ObserveADSSight(m_pCurrentEntity,pbones[bone].name,(*m_pbonetransform)[bone]);

}

void CStudioModelRenderer::StudioSaveBones(void)
{
	int i;

	mstudiobone_t *pbones;
	pbones = (mstudiobone_t *)((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);

	m_nCachedBones = m_pStudioHeader->numbones;

	for (i = 0; i < m_pStudioHeader->numbones; i++)
	{
		strncpy(m_nCachedBoneNames[i], pbones[i].name, 32);
		MatrixCopy((*m_pbonetransform)[i], m_rgCachedBoneTransform[i]);
		MatrixCopy((*m_plighttransform)[i], m_rgCachedLightTransform[i]);
	}

}

void CStudioModelRenderer::StudioMergeBones(model_t *pSubModel)
{
	int i, j;
	double f;

	mstudiobone_t *pbones;
	mstudioseqdesc_t *pseqdesc;
	mstudioanim_t *panim;

	static float pos[MAXSTUDIOBONES][3];
	float bonematrix[3][4];
	static vec4_t q[MAXSTUDIOBONES];

	if( !m_pStudioHeader || !m_pCurrentEntity )
		return;

	if (m_pCurrentEntity->curstate.sequence >= m_pStudioHeader->numseq)
		m_pCurrentEntity->curstate.sequence = 0;

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + m_pCurrentEntity->curstate.sequence;

	f = StudioEstimateFrame(pseqdesc);

	/*if (m_pCurrentEntity->latched.prevframe > f)
	{
	}*/

	panim = StudioGetAnim(pSubModel, pseqdesc);
	StudioCalcRotations(pos, q, pseqdesc, panim, f);

	pbones = (mstudiobone_t *)((byte *)m_pStudioHeader + m_pStudioHeader->boneindex);

	for (i = 0; i < m_pStudioHeader->numbones; i++)
	{
		for (j = 0; j < m_nCachedBones; j++)
		{
			if (stricmp(pbones[i].name, m_nCachedBoneNames[j]) == 0)
			{
				MatrixCopy(m_rgCachedBoneTransform[j], (*m_pbonetransform)[i]);
				MatrixCopy(m_rgCachedLightTransform[j], (*m_plighttransform)[i]);
				break;
			}
		}

		if (j >= m_nCachedBones)
		{
			QuaternionMatrix(q[i], bonematrix);

			bonematrix[0][3] = pos[i][0];
			bonematrix[1][3] = pos[i][1];
			bonematrix[2][3] = pos[i][2];

			if (pbones[i].parent == -1)
			{
				if (IEngineStudio.IsHardware())
				{
					ConcatTransforms((*m_protationmatrix), bonematrix, (*m_pbonetransform)[i]);
					MatrixCopy((*m_pbonetransform)[i], (*m_plighttransform)[i]);
				}
				else
				{
					ConcatTransforms((*m_paliastransform), bonematrix, (*m_pbonetransform)[i]);
					ConcatTransforms((*m_protationmatrix), bonematrix, (*m_plighttransform)[i]);
				}

				StudioFxTransform(m_pCurrentEntity, (*m_pbonetransform)[i]);
			}
			else
			{
				ConcatTransforms((*m_pbonetransform)[pbones[i].parent], bonematrix, (*m_pbonetransform)[i]);
				ConcatTransforms((*m_plighttransform)[pbones[i].parent], bonematrix, (*m_plighttransform)[i]);
			}
		}
	}
}

static void DrawOC2EquipmentArms(CStudioModelRenderer &renderer, alight_t &lighting, float *lightdir)
{
	g_lastArmsWeapon = renderer.m_pRenderModel;
	g_lastArmsDrawn = 0;
	const ViewModelArmsInfo info = InspectViewModelArms(renderer.m_pStudioHeader,
		renderer.m_pCurrentEntity->curstate.body, renderer.m_pCurrentEntity->curstate.skin);
	if (!info.NeedsArms()) return;

	int team = g_iTeamNumber;
	const int index = renderer.m_pCurrentEntity->index;
	if (index > 0 && index <= MAX_PLAYERS &&
		(g_PlayerExtraInfo[index].teamnumber == 1 || g_PlayerExtraInfo[index].teamnumber == 2))
		team = g_PlayerExtraInfo[index].teamnumber;
	const bool ct = team == 2;
	// APK scripts_character/phoenix_vara.txt and sas_vara.txt specify these
	// models/skin5. HideArms selects blank0 for the CT sleeve, bare forearms1
	// for the T glove. This is resource configuration, not APK game behavior.
	const char *const paths[] = {
		ct ? "models/arms/glove_hardknuckle/v_glove_hardknuckle_black.mdl" :
			"models/arms/glove_fullfinger/v_glove_fullfinger.mdl",
		"models/arms/sas/v_sleeve_sas.mdl"
	};
	studiohdr_t *const weaponHeader = renderer.m_pStudioHeader;
	model_t *const weaponModel = renderer.m_pRenderModel;
	const cl_entity_t weaponEntity = *renderer.m_pCurrentEntity;
	mstudiobodyparts_t *const savedBodyPart = renderer.m_pBodyPart;
	mstudiomodel_t *const savedSubModel = renderer.m_pSubModel;
	const int savedFaceFlags = IEngineStudio.GetForceFaceFlags();
	for (int part = 0; part < (ct ? 2 : 1); ++part)
	{
		// Mod_ForName uses the engine's model cache and does not retain a raw
		// client pointer across map changes. Missing optional assets fail closed.
		model_t *model = IEngineStudio.Mod_ForName(paths[part], false);
		studiohdr_t *header = model ? (studiohdr_t *)IEngineStudio.Mod_Extradata(model) : NULL;
		if (!ArmMeshBonesMatch(header, renderer))
			continue;
		renderer.m_pRenderModel = model;
		renderer.m_pStudioHeader = header;
		renderer.m_pCurrentEntity->model = model;
		renderer.m_pCurrentEntity->curstate.body = part == 0 && !ct ? 1 : 0;
		renderer.m_pCurrentEntity->curstate.skin = part == 0 && header->numskinfamilies > 5 ? 5 : 0;
		renderer.m_pCurrentEntity->curstate.sequence = 0;
		renderer.m_pCurrentEntity->curstate.frame = 0;
		renderer.m_pCurrentEntity->curstate.framerate = 0;
		renderer.m_pCurrentEntity->curstate.animtime = renderer.m_clTime;
		IEngineStudio.StudioSetHeader(header);
		IEngineStudio.SetRenderModel(model);
		// StudioSaveBones already cached the animated weapon. Never replace
		// that cache between glove and sleeve; both follow that same pose.
		renderer.StudioMergeBones(model);
		IEngineStudio.StudioSetupLighting(&lighting);
		IEngineStudio.StudioSetRemapColors(renderer.m_nTopColor, renderer.m_nBottomColor);
		renderer.StudioRenderModel(lightdir);
		++g_lastArmsDrawn;
	}
	*renderer.m_pCurrentEntity = weaponEntity;
	renderer.m_pRenderModel = weaponModel;
	renderer.m_pStudioHeader = weaponHeader;
	renderer.m_pBodyPart = savedBodyPart;
	renderer.m_pSubModel = savedSubModel;
	for (int bone = 0; bone < renderer.m_nCachedBones; ++bone)
	{
		MatrixCopy(renderer.m_rgCachedBoneTransform[bone], (*renderer.m_pbonetransform)[bone]);
		MatrixCopy(renderer.m_rgCachedLightTransform[bone], (*renderer.m_plighttransform)[bone]);
	}
	IEngineStudio.StudioSetHeader(weaponHeader);
	IEngineStudio.SetRenderModel(weaponModel);
	IEngineStudio.StudioSetupLighting(&lighting);
	IEngineStudio.StudioSetRemapColors(renderer.m_nTopColor, renderer.m_nBottomColor);
	IEngineStudio.SetForceFaceFlags(savedFaceFlags);
}

int CStudioModelRenderer::StudioDrawModel(int flags)
{
	m_pCurrentEntity = IEngineStudio.GetCurrentEntity();

	IEngineStudio.GetTimes(&m_nFrameCount, &m_clTime, &m_clOldTime);
	IEngineStudio.GetViewInfo(m_vRenderOrigin, m_vUp, m_vRight, m_vNormal);
	IEngineStudio.GetAliasScale(&m_fSoftwareXScale, &m_fSoftwareYScale);

	if (m_pCurrentEntity->curstate.renderfx == kRenderFxDeadPlayer)
	{
		entity_state_t deadplayer;

		int result;
		int save_interp;

		if (m_pCurrentEntity->curstate.renderamt <= 0 || m_pCurrentEntity->curstate.renderamt > gEngfuncs.GetMaxClients())
			return 0;

		deadplayer = *(IEngineStudio.GetPlayerState(m_pCurrentEntity->curstate.renderamt - 1));

		deadplayer.number = m_pCurrentEntity->curstate.renderamt;
		deadplayer.weaponmodel = 0;
		deadplayer.gaitsequence = 0;

		deadplayer.movetype = MOVETYPE_NONE;
		VectorCopy(m_pCurrentEntity->curstate.angles, deadplayer.angles);
		VectorCopy(m_pCurrentEntity->curstate.origin, deadplayer.origin);

		save_interp = m_fDoInterp;
		m_fDoInterp = 0;

		result = StudioDrawPlayer(flags, &deadplayer);

		m_fDoInterp = save_interp;
		return result;
	}

	m_pRenderModel = m_pCurrentEntity->model;
	m_pStudioHeader = (studiohdr_t *)IEngineStudio.Mod_Extradata(m_pRenderModel);

	IEngineStudio.StudioSetHeader(m_pStudioHeader);
	IEngineStudio.SetRenderModel(m_pRenderModel);

	StudioSetUpTransform(0);

	if (flags & STUDIO_RENDER)
	{
		if (!IEngineStudio.StudioCheckBBox())
			return 0;

		(*m_pModelsDrawn)++;
		(*m_pStudioModelCount)++;

		if (m_pStudioHeader->numbodyparts == 0)
			return 1;
	}

	bool bIsViewModel = gEngfuncs.GetViewModel() == m_pCurrentEntity;
	if (bIsViewModel)
	{
		V_ApplyADSBody(m_pCurrentEntity);
		const int first=ViewModelFirstFrame(m_pRenderModel,m_pStudioHeader,m_pCurrentEntity->curstate.sequence);
		const int seq=m_pCurrentEntity->curstate.sequence;
		if (first > 0 && seq >= 0 && seq < m_pStudioHeader->numseq)
		{
			const mstudioseqdesc_t &desc=((mstudioseqdesc_t *)((byte *)m_pStudioHeader+m_pStudioHeader->seqindex))[seq];
			// The common frame base is consumed by both SDK bone rendering and
			// engine studio events, so bolt/sleeve sounds stay on the same clock.
			m_pCurrentEntity->curstate.frame=first*256.0f/(desc.numframes-1);
		}
	}

	if (m_pCurrentEntity->curstate.movetype == MOVETYPE_FOLLOW)
		StudioMergeBones(m_pRenderModel);
	else
		StudioSetupBones();

	StudioSaveBones();

	if (flags & STUDIO_EVENTS)
	{
		StudioCalcAttachments();
		const float savedSequenceTime=m_pCurrentEntity->latched.sequencetime;
		if (bIsViewModel)
		{
			g_viewFlow.firstEvents=g_viewFirstEvents;
			m_pCurrentEntity->latched.sequencetime=g_viewFirstEvents ? m_pCurrentEntity->curstate.animtime : -1.0f;
		}
		IEngineStudio.StudioClientEvents();
		if (bIsViewModel) { m_pCurrentEntity->latched.sequencetime=savedSequenceTime; g_viewFirstEvents=false; }

		if (m_pCurrentEntity->index > 0)
		{
			cl_entity_t *ent = gEngfuncs.GetEntityByIndex(m_pCurrentEntity->index);
			memcpy(ent->attachment, m_pCurrentEntity->attachment, sizeof(vec3_t) * 4);
		}
	}

	if (flags & STUDIO_RENDER)
	{
		alight_t lighting;
		vec3_t dir;
		lighting.plightvec = dir;

		IEngineStudio.StudioDynamicLight(m_pCurrentEntity, &lighting);

		IEngineStudio.StudioEntityLight(&lighting);
		IEngineStudio.StudioSetupLighting(&lighting);

		m_nTopColor = m_pCurrentEntity->curstate.colormap & 0xFF;
		m_nBottomColor = (m_pCurrentEntity->curstate.colormap & 0xFF00) >> 8;

		IEngineStudio.StudioSetRemapColors(m_nTopColor, m_nBottomColor);

		StudioRenderModel(dir);
		if (bIsViewModel) DrawOC2EquipmentArms(*this, lighting, dir);
	}

	return 1;
}

void CStudioModelRenderer::StudioEstimateGait(entity_state_t *pplayer)
{
	float dt;
	vec3_t est_velocity;

	dt = (m_clTime - m_clOldTime);

	if (dt < 0)
		dt = 0;
	else if (dt > 1.0)
		dt = 1;

	if (dt == 0 || m_pPlayerInfo->renderframe == m_nFrameCount)
	{
		m_flGaitMovement = 0;
		return;
	}

	if (m_fGaitEstimation)
	{
		VectorSubtract(m_pCurrentEntity->origin, m_pPlayerInfo->prevgaitorigin, est_velocity);
		VectorCopy(m_pCurrentEntity->origin, m_pPlayerInfo->prevgaitorigin);
		m_flGaitMovement = est_velocity.Length();

		if (dt <= 0 || m_flGaitMovement / dt < 5)
		{
			m_flGaitMovement = 0;
			est_velocity[0] = 0;
			est_velocity[1] = 0;
		}
	}
	else
	{
		VectorCopy(pplayer->velocity, est_velocity);
		m_flGaitMovement = est_velocity.Length() * dt;
	}

	if (est_velocity[1] == 0 && est_velocity[0] == 0)
	{
		float flYawDiff = m_pCurrentEntity->angles[YAW] - m_pPlayerInfo->gaityaw;
		flYawDiff = flYawDiff - (int)(flYawDiff / 360) * 360;

		if (flYawDiff > 180)
			flYawDiff -= 360;
		if (flYawDiff < -180)
			flYawDiff += 360;

		if (dt < 0.25)
			flYawDiff *= dt * 4;
		else
			flYawDiff *= dt;

		m_pPlayerInfo->gaityaw += flYawDiff;
		m_pPlayerInfo->gaityaw = m_pPlayerInfo->gaityaw - (int)(m_pPlayerInfo->gaityaw / 360) * 360;
		m_flGaitMovement = 0;
	}
	else
	{
		m_pPlayerInfo->gaityaw = (atan2(est_velocity[1], est_velocity[0]) * 180 / M_PI);

		if (m_pPlayerInfo->gaityaw > 180)
			m_pPlayerInfo->gaityaw = 180;

		if (m_pPlayerInfo->gaityaw < -180)
			m_pPlayerInfo->gaityaw = -180;
	}
}

void CStudioModelRenderer::StudioProcessGait(entity_state_t *pplayer)
{
	mstudioseqdesc_t *pseqdesc;
	float dt;
	int iBlend;
	float flYaw;

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + m_pCurrentEntity->curstate.sequence;

	StudioPlayerBlend(pseqdesc, &iBlend, &m_pCurrentEntity->angles[PITCH]);

	m_pCurrentEntity->latched.prevangles[PITCH] = m_pCurrentEntity->angles[PITCH];
	m_pCurrentEntity->curstate.blending[0] = iBlend;
	m_pCurrentEntity->latched.prevblending[0] = m_pCurrentEntity->curstate.blending[0];
	m_pCurrentEntity->latched.prevseqblending[0] = m_pCurrentEntity->curstate.blending[0];

	dt = (m_clTime - m_clOldTime);

	if (dt < 0)
		dt = 0;
	else if (dt > 1.0)
		dt = 1;

	StudioEstimateGait(pplayer);

	flYaw = m_pCurrentEntity->angles[YAW] - m_pPlayerInfo->gaityaw;
	flYaw = flYaw - (int)(flYaw / 360) * 360;

	if (flYaw < -180)
		flYaw = flYaw + 360;

	if (flYaw > 180)
		flYaw = flYaw - 360;

	if (flYaw > 120)
	{
		m_pPlayerInfo->gaityaw = m_pPlayerInfo->gaityaw - 180;
		m_flGaitMovement = -m_flGaitMovement;
		flYaw = flYaw - 180;
	}
	else if (flYaw < -120)
	{
		m_pPlayerInfo->gaityaw = m_pPlayerInfo->gaityaw + 180;
		m_flGaitMovement = -m_flGaitMovement;
		flYaw = flYaw + 180;
	}

	m_pCurrentEntity->curstate.controller[0] = ((flYaw / 4.0) + 30) / (60.0 / 255.0);
	m_pCurrentEntity->curstate.controller[1] = ((flYaw / 4.0) + 30) / (60.0 / 255.0);
	m_pCurrentEntity->curstate.controller[2] = ((flYaw / 4.0) + 30) / (60.0 / 255.0);
	m_pCurrentEntity->curstate.controller[3] = ((flYaw / 4.0) + 30) / (60.0 / 255.0);
	m_pCurrentEntity->latched.prevcontroller[0] = m_pCurrentEntity->curstate.controller[0];
	m_pCurrentEntity->latched.prevcontroller[1] = m_pCurrentEntity->curstate.controller[1];
	m_pCurrentEntity->latched.prevcontroller[2] = m_pCurrentEntity->curstate.controller[2];
	m_pCurrentEntity->latched.prevcontroller[3] = m_pCurrentEntity->curstate.controller[3];

	m_pCurrentEntity->angles[YAW] = m_pPlayerInfo->gaityaw;

	if (m_pCurrentEntity->angles[YAW] < -0)
		m_pCurrentEntity->angles[YAW] += 360;

	m_pCurrentEntity->latched.prevangles[YAW] = m_pCurrentEntity->angles[YAW];

	pseqdesc = (mstudioseqdesc_t *)((byte *)m_pStudioHeader + m_pStudioHeader->seqindex) + pplayer->gaitsequence;

	if (pseqdesc->linearmovement[0] > 0)
		m_pPlayerInfo->gaitframe += (m_flGaitMovement / pseqdesc->linearmovement[0]) * pseqdesc->numframes;
	else
		m_pPlayerInfo->gaitframe += pseqdesc->fps * dt;

	m_pPlayerInfo->gaitframe = m_pPlayerInfo->gaitframe - (int)(m_pPlayerInfo->gaitframe / pseqdesc->numframes) * pseqdesc->numframes;

	if (m_pPlayerInfo->gaitframe < 0)
		m_pPlayerInfo->gaitframe += pseqdesc->numframes;
}

int CStudioModelRenderer::StudioDrawPlayer(int flags, entity_state_t *pplayer)
{
	m_pCurrentEntity = IEngineStudio.GetCurrentEntity();

	IEngineStudio.GetTimes(&m_nFrameCount, &m_clTime, &m_clOldTime);
	IEngineStudio.GetViewInfo(m_vRenderOrigin, m_vUp, m_vRight, m_vNormal);
	IEngineStudio.GetAliasScale(&m_fSoftwareXScale, &m_fSoftwareYScale);

	m_nPlayerIndex = pplayer->number - 1;

	if (m_nPlayerIndex < 0 || m_nPlayerIndex >= gEngfuncs.GetMaxClients())
		return 0;

	m_pRenderModel = IEngineStudio.SetupPlayerModel(m_nPlayerIndex);

	if (m_pRenderModel == NULL)
		return 0;

	m_pStudioHeader = (studiohdr_t *)IEngineStudio.Mod_Extradata(m_pRenderModel);

	IEngineStudio.StudioSetHeader(m_pStudioHeader);
	IEngineStudio.SetRenderModel(m_pRenderModel);

	if (pplayer->gaitsequence)
	{
		vec3_t orig_angles;
		m_pPlayerInfo = IEngineStudio.PlayerInfo(m_nPlayerIndex);

		VectorCopy(m_pCurrentEntity->angles, orig_angles);

		StudioProcessGait(pplayer);

		m_pPlayerInfo->gaitsequence = pplayer->gaitsequence;
		m_pPlayerInfo = NULL;

		StudioSetUpTransform(0);
		VectorCopy(orig_angles, m_pCurrentEntity->angles);
	}
	else
	{
		m_pCurrentEntity->curstate.controller[0] = 127;
		m_pCurrentEntity->curstate.controller[1] = 127;
		m_pCurrentEntity->curstate.controller[2] = 127;
		m_pCurrentEntity->curstate.controller[3] = 127;
		m_pCurrentEntity->latched.prevcontroller[0] = m_pCurrentEntity->curstate.controller[0];
		m_pCurrentEntity->latched.prevcontroller[1] = m_pCurrentEntity->curstate.controller[1];
		m_pCurrentEntity->latched.prevcontroller[2] = m_pCurrentEntity->curstate.controller[2];
		m_pCurrentEntity->latched.prevcontroller[3] = m_pCurrentEntity->curstate.controller[3];

		m_pPlayerInfo = IEngineStudio.PlayerInfo(m_nPlayerIndex);
		m_pPlayerInfo->gaitsequence = 0;

		StudioSetUpTransform(0);
	}

	if (flags & STUDIO_RENDER)
	{
		if (!IEngineStudio.StudioCheckBBox())
			return 0;

		(*m_pModelsDrawn)++;
		(*m_pStudioModelCount)++;

		if (m_pStudioHeader->numbodyparts == 0)
			return 1;
	}

	m_pPlayerInfo = IEngineStudio.PlayerInfo(m_nPlayerIndex);

	StudioSetupBones();
	StudioSaveBones();

	m_pPlayerInfo->renderframe = m_nFrameCount;
	m_pPlayerInfo = NULL;

	if (flags & STUDIO_EVENTS)
	{
		StudioCalcAttachments();
		IEngineStudio.StudioClientEvents();

		if (m_pCurrentEntity->index > 0)
		{
			cl_entity_t *ent = gEngfuncs.GetEntityByIndex(m_pCurrentEntity->index);
			memcpy(ent->attachment, m_pCurrentEntity->attachment, sizeof(vec3_t) * 4);
		}
	}

	if (flags & STUDIO_RENDER)
	{
		if (m_pCvarHiModels->value && m_pRenderModel != m_pCurrentEntity->model)
			m_pCurrentEntity->curstate.body = 255;

		if (!(m_pCvarDeveloper->value == 0 && gEngfuncs.GetMaxClients() == 1) && (m_pRenderModel == m_pCurrentEntity->model))
			m_pCurrentEntity->curstate.body = 1;

		alight_t lighting;
		vec3_t dir;
		lighting.plightvec = dir;

		IEngineStudio.StudioDynamicLight(m_pCurrentEntity, &lighting);
		IEngineStudio.StudioEntityLight(&lighting);
		IEngineStudio.StudioSetupLighting(&lighting);

		m_pPlayerInfo = IEngineStudio.PlayerInfo(m_nPlayerIndex);

		m_nTopColor = m_pPlayerInfo->topcolor;

		if (m_nTopColor < 0)
			m_nTopColor = 0;

		if (m_nTopColor > 360)
			m_nTopColor = 360;

		m_nBottomColor = m_pPlayerInfo->bottomcolor;

		if (m_nBottomColor < 0)
			m_nBottomColor = 0;

		if (m_nBottomColor > 360)
			m_nBottomColor = 360;

		IEngineStudio.StudioSetRemapColors(m_nTopColor, m_nBottomColor);

		StudioRenderModel(dir);
		m_pPlayerInfo = NULL;

		if (pplayer->weaponmodel)
		{
			cl_entity_t saveent = *m_pCurrentEntity;
			model_t *pweaponmodel = IEngineStudio.GetModelByIndex(pplayer->weaponmodel);

			m_pStudioHeader = (studiohdr_t *)IEngineStudio.Mod_Extradata(pweaponmodel);
			IEngineStudio.StudioSetHeader(m_pStudioHeader);

			StudioMergeBones(pweaponmodel);

			IEngineStudio.StudioSetupLighting(&lighting);

			StudioRenderModel(dir);

			StudioCalcAttachments();

			*m_pCurrentEntity = saveent;
		}
	}

	return 1;
}

void CStudioModelRenderer::StudioCalcAttachments(void)
{
	int i;
	mstudioattachment_t *pattachment;

	if (m_pStudioHeader->numattachments > 4)
		gEngfuncs.Con_DPrintf("Too many attachments on %s\n", m_pCurrentEntity->model->name);

	pattachment = (mstudioattachment_t *)((byte *)m_pStudioHeader + m_pStudioHeader->attachmentindex);

	for (i = 0; i < m_pStudioHeader->numattachments; i++)
		VectorTransform(pattachment[i].org, (*m_plighttransform)[pattachment[i].bone], m_pCurrentEntity->attachment[i]);
}

void CStudioModelRenderer::StudioRenderModel(float *lightdir)
{
	IEngineStudio.SetChromeOrigin();

	int iSaveRenderMode =  m_pCurrentEntity->curstate.rendermode;
	int iSaveRenderFx = m_pCurrentEntity->curstate.renderfx;
	int iSaveRenderAmt = m_pCurrentEntity->curstate.renderamt;


	IEngineStudio.SetForceFaceFlags(0);
	StudioRenderFinal();

	if (iSaveRenderFx == kRenderFxGlowShell)
	{
		m_pCurrentEntity->curstate.renderfx = kRenderFxGlowShell;

		gEngfuncs.pTriAPI->SpriteTexture(m_pChromeSprite, 0);

		IEngineStudio.SetForceFaceFlags(STUDIO_NF_CHROME);
		StudioRenderFinal();
	}


	m_pCurrentEntity->curstate.rendermode = iSaveRenderMode;
	m_pCurrentEntity->curstate.renderfx = iSaveRenderFx;
	m_pCurrentEntity->curstate.renderamt = iSaveRenderAmt;
}

void CStudioModelRenderer::StudioRenderFinal_Software(void)
{
	int i;

	IEngineStudio.SetupRenderer(0);

	/*if (m_pCvarDrawEntities->value == 2)
	{
		IEngineStudio.StudioDrawBones();
	}
	else if (m_pCvarDrawEntities->value == 3)
	{
		IEngineStudio.StudioDrawHulls();
	}
	else*/
	{
		for (i = 0; i < m_pStudioHeader->numbodyparts; i++)
		{
			IEngineStudio.StudioSetupModel(i, (void **)&m_pBodyPart, (void **)&m_pSubModel);
			IEngineStudio.StudioDrawPoints();
		}
	}

	/*if (m_pCvarDrawEntities->value == 4)
	{
		gEngfuncs.pTriAPI->RenderMode(kRenderTransAdd);
		IEngineStudio.StudioDrawHulls();
		gEngfuncs.pTriAPI->RenderMode(kRenderNormal);
	}

	if (m_pCvarDrawEntities->value == 5)
		IEngineStudio.StudioDrawAbsBBox();*/

	IEngineStudio.RestoreRenderer();
}

int twice;

void CStudioModelRenderer::StudioRenderFinal_Hardware(void)
{
	int i;
	int rendermode;

	rendermode = IEngineStudio.GetForceFaceFlags() ? kRenderTransAdd : m_pCurrentEntity->curstate.rendermode;
	IEngineStudio.SetupRenderer(rendermode);

	/*if (m_pCvarDrawEntities->value == 2)
	{
		IEngineStudio.StudioDrawBones();
	}
	else if (m_pCvarDrawEntities->value == 3)
	{
		IEngineStudio.StudioDrawHulls();
	}
	else*/
	{
		for (i = 0; i < m_pStudioHeader->numbodyparts; i++)
		{
			IEngineStudio.StudioSetupModel(i, (void **)&m_pBodyPart, (void **)&m_pSubModel);

			if (m_fDoInterp)
				m_pCurrentEntity->trivial_accept = 0;

			IEngineStudio.GL_SetRenderMode(rendermode);
			IEngineStudio.StudioSetRenderamt(m_pCurrentEntity->curstate.renderamt);
			IEngineStudio.StudioDrawPoints();
		}
	}

	/*if (m_pCvarDrawEntities->value == 4)
	{
		gEngfuncs.pTriAPI->RenderMode(kRenderTransAdd);
		IEngineStudio.StudioDrawHulls();
		gEngfuncs.pTriAPI->RenderMode(kRenderNormal);
	}*/

	//if (m_pCvarDrawEntities->value == 5)
	//	IEngineStudio.StudioDrawAbsBBox();

	IEngineStudio.RestoreRenderer();
}

void CStudioModelRenderer::StudioRenderFinal(void)
{
	if (IEngineStudio.IsHardware())
		StudioRenderFinal_Hardware();
	else
		StudioRenderFinal_Software();
}

void CStudioModelRenderer::StudioSetShadowSprite(int idx)
{
	m_iShadowSprite = idx;
}

void CStudioModelRenderer::StudioDrawShadow( Vector origin, float scale )
{
	Vector endPoint = origin;
	Vector p1, p2, p3, p4;
	pmtrace_t pmtrace;

	endPoint.z -= 150.0f;

	gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( false, true );
	gEngfuncs.pEventAPI->EV_PushPMStates( );
		gEngfuncs.pEventAPI->EV_SetSolidPlayers( -1 );
		gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
		gEngfuncs.pEventAPI->EV_PlayerTrace( origin, endPoint, PM_STUDIO_IGNORE | PM_GLASS_IGNORE, -1, &pmtrace );
	gEngfuncs.pEventAPI->EV_PopPMStates( );

	// don't allow shadow if player in solid area
	if( pmtrace.startsolid )
		return;

	// don't allow shadow if doesn't hit anything
	if( pmtrace.fraction >= 1.0f )
		return;

	pmtrace.plane.normal = pmtrace.plane.normal.Normalize( );

	// don't allow shadow on too lean planes
	if( pmtrace.plane.normal.z <= 0.7 )
		return;

	pmtrace.plane.normal = pmtrace.plane.normal * scale * ( 1.0 - pmtrace.fraction );


	// add 2.0f to Z, for avoid Z-fighting
	p1.x = pmtrace.endpos.x - pmtrace.plane.normal.z;
	p1.y = pmtrace.endpos.y + pmtrace.plane.normal.z;
	p1.z = pmtrace.endpos.z + 2.0f + pmtrace.plane.normal.x - pmtrace.plane.normal.y;

	p2.x = pmtrace.endpos.x + pmtrace.plane.normal.z;
	p2.y = pmtrace.endpos.y + pmtrace.plane.normal.z;
	p2.z = pmtrace.endpos.z + 2.0f - pmtrace.plane.normal.x - pmtrace.plane.normal.y;

	p3.x = pmtrace.endpos.x + pmtrace.plane.normal.z;
	p3.y = pmtrace.endpos.y - pmtrace.plane.normal.z;
	p3.z = pmtrace.endpos.z + 2.0f - pmtrace.plane.normal.x + pmtrace.plane.normal.y;

	p4.x = pmtrace.endpos.x - pmtrace.plane.normal.z;
	p4.y = pmtrace.endpos.y - pmtrace.plane.normal.z;
	p4.z = pmtrace.endpos.z + 2.0f + pmtrace.plane.normal.x + pmtrace.plane.normal.y;

	IEngineStudio.StudioRenderShadow( m_iShadowSprite, p1, p2, p3, p4 );
}
