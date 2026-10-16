#include "BaseMenu.h"
#include "BuyModelPreview.h"
#include "const.h"
#include "ref_params.h"
#include "cl_entity.h"
#include "com_model.h"
#include "entity_types.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
const char *const kTActor = "models/lobby/phoenix_vara.mdl";
const char *const kCTActor = "models/lobby/sas_vara.mdl";
const char *const kWeapons = "models/lobby/weapons.mdl";
const int kMaxSequences = 512;
const int kSequenceBytes = 176; // Stock IDST v10 mstudioseqdesc_t.

struct PoseBounds
{
	float mins[3], maxs[3];
	bool valid;
};

struct ModelMetadata
{
	bool attempted, valid;
	int sequenceCount, firstBodyModels;
	PoseBounds poses[kMaxSequences];
};

struct Presentation
{
	int weaponId, weaponBody, ctSequence, tSequence;
};

// Resource body names and sequence indices were read directly from these
// IDST v10 assets (buy_model_presentation_indices.json). This table maps
// paired client weapon IDs to their verified presentation body and pose.
const Presentation kPresentations[] =
{
	{ 1, 24, 328, 366 }, // P250 in the paired legacy P228 slot.
	{ 2, 43, 371, 371 }, // Independent Zeus in the former unused ID 2.
	{ 3, 5, 306, 340 },  // Scout -> SSG08 artwork.
	{ 4, 11, 375, 375 }, // HE grenade.
	{ 5, 37, 313, 349 }, // XM1014.
	{ 6, -1, 379, 379 }, // No C4 body exists in this weapon bundle.
	{ 7, 39, 319, 352 }, // MAC10.
	{ 8, 28, 309, 342 }, // AUG.
	{ 9, 13, 374, 374 }, // Smoke grenade.
	{ 10, 20, 334, 368 }, // Dual Elites.
	{ 11, 21, 329, 363 }, // FiveSeven.
	{ 12, 1, 322, 354 }, // UMP45.
	{ 13, 4, 307, 345 }, // SG550 -> SCAR20 artwork.
	{ 14, 30, 305, 338 }, // Galil.
	{ 15, 29, 304, 339 }, // FAMAS.
	{ 16, 17, 326, 361 }, // USP.
	{ 17, 22, 327, 359 }, // Glock18.
	{ 18, 2, 308, 343 }, // AWP.
	{ 19, 41, 320, 356 }, // MP7: w_smg_mp7 body and verified CT/T poses.
	{ 20, 15, 317, 351 }, // M249.
	{ 21, 35, 312, 348 }, // Nova in the paired legacy M3 slot.
	{ 22, 32, 302, 337 }, // M4A1-S presentation of the CS1.6 M4A1.
	{ 23, 42, 318, 353 }, // MP9 in the paired legacy TMP slot.
	{ 24, 3, 311, 344 }, // G3SG1.
	{ 25, 10, 372, 372 }, // Flashbang.
	{ 26, 19, 332, 365 }, // Deagle.
	{ 27, 33, 310, 341 }, // SG553 in the paired legacy SG552 slot.
	{ 28, 27, 303, 336 }, // AK47.
	{ 29, 14, 369, 369 }, // Knife.
	{ 30, 0, 323, 357 }, // P90 (body zero is a real model).
	{ 31, 12, 373, 373 }, // Molotov: w_eq_molotov body.
	{ 1001, 6, 377, 377 }, // Armor.
	{ 1002, 7, 377, 377 }, // Armor and helmet.
	{ 1003, 9, 376, 376 } // Defuser.
};

ModelMetadata s_tMetadata = {}, s_ctMetadata = {}, s_weaponMetadata = {};
cl_entity_t s_actor = {}, s_weapon = {};
bool s_rendering = false;

template<typename T> T ReadValue(const byte *bytes, int offset)
{
	T result;
	std::memcpy(&result, bytes + offset, sizeof(result));
	return result;
}

bool LoadMetadata(const char *path, ModelMetadata &result)
{
	if (result.attempted) return result.valid;
	result.attempted = true;
	int length = 0;
	byte *bytes = EngFuncs::COM_LoadFile(path, &length);
	if (!bytes) return false;
	// Read the on-disk header rather than model_t::cache, which belongs to
	// the engine. These three presentation models contain no external seqgroup.
	if (length >= 244 && std::memcmp(bytes, "IDST", 4) == 0
		&& ReadValue<std::int32_t>(bytes, 4) == 10)
	{
		const int count = ReadValue<std::int32_t>(bytes, 164);
		const int sequenceOffset = ReadValue<std::int32_t>(bytes, 168);
		const int bodyCount = ReadValue<std::int32_t>(bytes, 204);
		const int bodyOffset = ReadValue<std::int32_t>(bytes, 208);
		if (count > 0 && count <= kMaxSequences && sequenceOffset >= 244
			&& sequenceOffset <= length - count*kSequenceBytes
			&& bodyCount > 0 && bodyCount <= 128 && bodyOffset >= 244
			&& bodyOffset <= length - bodyCount*76)
		{
			result.sequenceCount = count;
			result.firstBodyModels = ReadValue<std::int32_t>(bytes, bodyOffset + 64);
			for (int i = 0; i < count; ++i)
			{
				const int offset = sequenceOffset + i*kSequenceBytes;
				PoseBounds &pose = result.poses[i];
				pose.valid = ReadValue<std::int32_t>(bytes, offset + 56) == 1;
				for (int axis = 0; axis < 3; ++axis)
				{
					pose.mins[axis] = ReadValue<float>(bytes, offset + 96 + axis*4);
					pose.maxs[axis] = ReadValue<float>(bytes, offset + 108 + axis*4);
					if (!std::isfinite(pose.mins[axis]) || !std::isfinite(pose.maxs[axis])
						|| pose.maxs[axis] < pose.mins[axis] || std::fabs(pose.mins[axis]) > 4096
						|| std::fabs(pose.maxs[axis]) > 4096) pose.valid = false;
				}
			}
			result.valid = result.firstBodyModels > 0;
		}
	}
	EngFuncs::COM_FreeFile(bytes);
	return result.valid;
}

Presentation SelectPresentation(int weaponId)
{
	for (const Presentation &item : kPresentations)
		if (item.weaponId == weaponId) return item;
	// Empty/unknown selection still previews the team actor, without a gun.
	return Presentation{weaponId, -1, 379, 379};
}

bool ValidPose(const ModelMetadata &model, int sequence)
{
	return sequence >= 0 && sequence < model.sequenceCount && model.poses[sequence].valid;
}

void InitializeEntity(cl_entity_t &entity, int number, int sequence, int body,
	const float origin[3], float time)
{
	std::memset(&entity, 0, sizeof(entity));
	// Generic studio entities retain the selected body and cannot substitute
	// the real local player's model/gait. index zero also avoids world attachments.
	entity.index = 0;
	entity.player = false;
	entity.curstate.number = number;
	entity.curstate.body = body;
	entity.curstate.sequence = sequence;
	entity.curstate.scale = 1.0f;
	entity.curstate.frame = 0.0f;
	entity.curstate.framerate = 0.0f; // All verified buy poses are single-frame.
	entity.curstate.animtime = time;
	entity.curstate.effects = EF_FULLBRIGHT | EF_NOINTERP;
	entity.curstate.rendermode = kRenderNormal;
	entity.curstate.renderamt = 255;
	entity.curstate.rendercolor.r = 255;
	entity.curstate.rendercolor.g = 255;
	entity.curstate.rendercolor.b = 255;
	// Lobby buy poses face -X at yaw zero, toward this +X-looking camera.
	entity.angles[1] = entity.curstate.angles[1] = 0.0f;
	for (int axis = 0; axis < 3; ++axis)
		entity.origin[axis] = entity.curstate.origin[axis] = origin[axis];
	for (int controller = 0; controller < 4; ++controller)
		entity.curstate.controller[controller] = entity.latched.prevcontroller[controller] = 127;
	entity.latched.prevsequence = sequence;
	entity.latched.prevanimtime = time;
}
}

bool OC2_DrawBuyPreview(int team, int weaponId, int x, int y, int width, int height)
{
	const ui_enginefuncs_t &api = EngFuncs::engfuncs;
	if (s_rendering || !gpGlobals || (team != 1 && team != 2)
		|| width <= 0 || height <= 0 || x < 0 || y < 0
		|| x >= gpGlobals->scrWidth || y >= gpGlobals->scrHeight
		|| !api.pfnSetModel || !api.pfnClearScene || !api.pfnRenderScene
		|| !api.CL_CreateVisibleEntity || !api.COM_LoadFile || !api.COM_FreeFile) return false;
	if (width > gpGlobals->scrWidth - x) width = gpGlobals->scrWidth - x;
	if (height > gpGlobals->scrHeight - y) height = gpGlobals->scrHeight - y;
	const bool ct = team == 2;
	const char *actorPath = ct ? kCTActor : kTActor;
	ModelMetadata &actorMetadata = ct ? s_ctMetadata : s_tMetadata;
	Presentation selected = SelectPresentation(weaponId);
	// The shop's stable fire slot offers CT Incendiary, while the carried
	// item's variant is tracked independently by the game client/server.
	if (weaponId == 31 && ct) selected.weaponBody = 44; // w_eq_incendiarygrenade.
	const int sequence = ct ? selected.ctSequence : selected.tSequence;
	if (!LoadMetadata(actorPath, actorMetadata) || !ValidPose(actorMetadata, sequence)) return false;
	const bool drawWeapon = selected.weaponBody >= 0;
	if (drawWeapon && (!LoadMetadata(kWeapons, s_weaponMetadata)
		|| !ValidPose(s_weaponMetadata, sequence)
		|| selected.weaponBody >= s_weaponMetadata.firstBodyModels)) return false;

	ref_viewpass_t view = {};
	view.viewport[0] = x;
	view.viewport[1] = y;
	view.viewport[2] = width;
	view.viewport[3] = height;
	view.fov_y = 35.0f;
	const float tanY = std::tan(35.0f * 0.00872664626f);
	const float tanX = tanY * float(width)/float(height);
	view.fov_x = std::atan(tanX) * 114.591559f;
	// Header bounds are zero in these assets. The per-pose actor bounds put
	// feet at z~=0 and head at z~=78. Frame that full height, with side room
	// for the held weapon; weapons.mdl bounds span all 45 bodies and would
	// unnecessarily shrink the actor if used directly for camera fitting.
	const PoseBounds &bounds = actorMetadata.poses[sequence];
	const float halfHeight = (bounds.maxs[2] - bounds.mins[2])*0.5f;
	const float halfWidth = (bounds.maxs[1] - bounds.mins[1])*0.5f + (drawWeapon ? 12.0f : 4.0f);
	const float halfDepth = (bounds.maxs[0] - bounds.mins[0])*0.5f;
	const float fitHeight = halfHeight/tanY;
	const float fitWidth = halfWidth/tanX;
	const float distance = (fitHeight > fitWidth ? fitHeight : fitWidth)*1.12f + halfDepth + 4.0f;
	float origin[3] =
	{
		distance - (bounds.mins[0] + bounds.maxs[0])*0.5f,
		-(bounds.mins[1] + bounds.maxs[1])*0.5f,
		-(bounds.mins[2] + bounds.maxs[2])*0.5f
	};
	InitializeEntity(s_actor, 1, sequence, 1, origin, gpGlobals->time);
	EngFuncs::SetModel(&s_actor, actorPath);
	if (!s_actor.model || s_actor.model->type != mod_studio) return false;
	if (drawWeapon)
	{
		// Both resources bake the same presentation pose into their own bones.
		// Sharing transform/sequence/frame is deliberate; MOVETYPE_FOLLOW would
		// instead merge incompatible actor/weapon bone layouts.
		InitializeEntity(s_weapon, 2, sequence, selected.weaponBody, origin, gpGlobals->time);
		EngFuncs::SetModel(&s_weapon, kWeapons);
		if (!s_weapon.model || s_weapon.model->type != mod_studio) return false;
	}

	s_rendering = true;
	// Match PlayerModelView: ClearScene pushes the renderer's scene; RenderScene
	// restores 2D mode and pops it. Always pair them, even when an add fails.
	EngFuncs::ClearScene();
	const bool actorAdded = EngFuncs::CL_CreateVisibleEntity(ET_NORMAL, &s_actor) != 0;
	const bool weaponAdded = !drawWeapon || EngFuncs::CL_CreateVisibleEntity(ET_NORMAL, &s_weapon) != 0;
	EngFuncs::RenderScene(&view);
	s_rendering = false;
	return actorAdded && weaponAdded;
}
