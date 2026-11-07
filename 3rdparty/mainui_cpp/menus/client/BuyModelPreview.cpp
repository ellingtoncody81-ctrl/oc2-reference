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
const char *const kM249Full = "models/lobby/m249_full.mdl";
const char *const kGloves = "models/lobby/glove.mdl";
const int kMaxSequences = 512;
const int kSequenceBytes = 176; // Stock IDST v10 mstudioseqdesc_t.
// APK Character3DLobby advances the network frame (0..256), not source frames:
// libxash.so 0x81ec6..0x81ee8, then walkup -> idle at frame 250.
const float kLobbyFrameRate = 30.0f;
const float kLobbyEntryEnd = 250.0f;
const float kLobbyIdleEnd = 255.0f;
const float kLobbyBlendTime = 0.2f; // GoldSrc's native sequence blend window.

struct PoseBounds
{
	float mins[3], maxs[3];
	float fps;
	int frames;
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

ModelMetadata s_tMetadata = {}, s_ctMetadata = {}, s_weaponMetadata = {}, s_gloveMetadata = {};
ModelMetadata s_m249Metadata = {};
cl_entity_t s_actor = {}, s_weapon = {}, s_glove = {};
bool s_rendering = false;
int s_lastWeapon=0,s_lastGlove=-1,s_lastSwaps=0;
bool s_lastAnimated=false;
struct LobbyPlayback
{
	bool active=false;
	int team=0,weapon=0,entry=-1,idle=-1;
	std::uint64_t appearance=0;
	float started=0,lastTime=0,elapsed=0;
} s_lobby,s_teamIdle[2];
enum class PresentationMode { Shop, Lobby, Team };
struct TeamPreviewSample
{
	bool drawn=false;
	int sequence=-1,viewport[4]={};
	float frame=0,elapsed=0,origin[3]={};
} s_teamSamples[2];
bool s_lastEntering=false;
float s_lastSourceFrame=0,s_lastElapsed=0;
int s_lastViewport[4]={};
float s_lastFov[2]={};

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
				pose.frames = ReadValue<std::int32_t>(bytes, offset + 56);
				pose.fps = ReadValue<float>(bytes, offset + 32);
				pose.valid = pose.frames > 0 && std::isfinite(pose.fps) && pose.fps > 0;
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

int LobbySequence(int weapon,bool ct)
{
	// APK loadout idle sequences, shared by actor/weapons/glove resources.
	// These are complete animated poses; the 302+ shop poses stay still.
	// Several converted CT/T aliases lower the support hand away from the gun.
	// Both actors contain every source pose; select the actual held-weapon idle.
	switch(weapon)
	{
	case 1:case 11:case 16:case 17:case 26:return ct?25:162;
	case 10:return ct?32:170;
	case 3:return ct?44:217;
	case 5:return 114;
	case 7:return 178;
	case 8:case 28:return ct?54:210;
	case 12:return 194;
	case 13:return ct?49:217;
	case 14:return ct?54:231;
	case 15:return ct?54:210;
	case 18:return ct?39:224;
	case 19:return 178;
	case 20:return 253;
	case 21:return 107;
	case 22:case 27:return ct?54:210;
	case 23:return 178;
	case 24:return ct?49:238;
	case 29:return ct?5:140;
	case 30:return 186;
	default:return -1;
	}
}

int LobbyEntrySequence(int idle)
{
	// Every held-weapon idle immediately follows its matching walkup, except
	// knife idle1 which follows two alternative entrances. Use the first one.
	return idle==5?3:idle==140?138:idle-1;
}

void AdvanceLobby(LobbyPlayback &playback,int team,int weapon,int idle,float time,std::uint64_t appearance=0)
{
	if(!playback.active||playback.team!=team||playback.weapon!=weapon||playback.idle!=idle
		||playback.appearance!=appearance
		||!std::isfinite(time)||time<playback.lastTime)
	{
		playback=LobbyPlayback();playback.active=true;
		playback.team=team;playback.weapon=weapon;playback.idle=idle;
		playback.appearance=appearance;
		playback.entry=LobbyEntrySequence(idle);playback.started=time;
	}
	playback.lastTime=time;
	playback.elapsed=std::isfinite(time)?time-playback.started:0;
}

float EntryFrame(const PoseBounds &pose)
{
	// Converted walkups are flagged LOOPING and duplicate their first pose at
	// the last source frame. Never interpolate into that artificial return.
	const float lastReal=pose.frames>2?256.0f*float(pose.frames-2)/float(pose.frames-1):0;
	return lastReal<kLobbyEntryEnd?lastReal:kLobbyEntryEnd;
}

void ApplyLobbyFrame(cl_entity_t &entity,const ModelMetadata &metadata,
	const LobbyPlayback &playback,bool playEntry,float time)
{
	const float entryDuration=playEntry?kLobbyEntryEnd/kLobbyFrameRate:0;
	const bool entering=playback.elapsed<entryDuration;
	const float idleTime=entering?0:playback.elapsed-entryDuration;
	const PoseBounds &pose=metadata.poses[entity.curstate.sequence];
	entity.curstate.frame=entering?playback.elapsed*kLobbyFrameRate:
		std::fmod(idleTime*kLobbyFrameRate,kLobbyIdleEnd);
	if(entering&&entity.curstate.frame>EntryFrame(pose))entity.curstate.frame=EntryFrame(pose);
	if(playEntry&&!entering&&idleTime<kLobbyBlendTime)
	{
		const PoseBounds &entry=metadata.poses[playback.entry];
		entity.latched.prevsequence=playback.entry;
		entity.latched.prevframe=EntryFrame(entry)*float(entry.frames-1)/256.0f;
		entity.latched.sequencetime=time-idleTime;
	}
}

struct PreviewAPI
{
	unsigned version;
	void *(*StudioHeader)(cl_entity_t *);
	int (*LoadTexture)(const char *,int);
	void (*FreeTexture)(int);
	int (*OwnTexture)(int,const char *);
	void (*SetTransform)(cl_entity_t *,const float matrix[3][4]);
	void (*SetScene)(int);
};
class PreviewScene
{
	PreviewAPI *m_api;
public:
	PreviewScene():m_api(EngFuncs::textfuncs.pfnGetNativeObject?static_cast<PreviewAPI *>(EngFuncs::textfuncs.pfnGetNativeObject("OC2ModelPreview002")):nullptr)
	{if(m_api&&m_api->version==2)m_api->SetScene(1);else m_api=nullptr;}
	~PreviewScene(){if(m_api)m_api->SetScene(0);}
};

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
	// Static shop poses face -X. Animated loadout poses face +X and receive
	// their presentation yaw below, identically on actor, gun and gloves.
	entity.angles[1] = entity.curstate.angles[1] = 0.0f;
	for (int axis = 0; axis < 3; ++axis)
		entity.origin[axis] = entity.curstate.origin[axis] = origin[axis];
	for (int controller = 0; controller < 4; ++controller)
		entity.curstate.controller[controller] = entity.latched.prevcontroller[controller] = 127;
	entity.latched.prevsequence = sequence;
	entity.latched.prevanimtime = time;
}
}

static bool DrawPresentation(int team, int weaponId, int x, int y, int width, int height,PresentationMode mode)
{
	const ui_enginefuncs_t &api = EngFuncs::engfuncs;
	if (s_rendering || !gpGlobals || !std::isfinite(gpGlobals->time) || (team != 1 && team != 2)
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
	// The APK bundle's W LOD omits M249's feed belt. This separate resource
	// carries the real V gun + all 16 belt meshes on the identical lobby rig.
	const char *weaponPath=weaponId==20?kM249Full:kWeapons;
	ModelMetadata &weaponMetadata=weaponId==20?s_m249Metadata:s_weaponMetadata;
	if(weaponId==20)selected.weaponBody=0;
	// The shop's stable fire slot offers CT Incendiary, while the carried
	// item's variant is tracked independently by the game client/server.
	if (weaponId == 31 && ct) selected.weaponBody = 44; // w_eq_incendiarygrenade.
	const bool playEntry=mode==PresentationMode::Lobby;
	const int idle=mode!=PresentationMode::Shop?LobbySequence(weaponId,ct):-1;
	LobbyPlayback &playback=mode==PresentationMode::Team?s_teamIdle[team-1]:s_lobby;
	const float presentationYaw=idle>=0?180.0f:0.0f;
	const float facing=idle>=0?-1.0f:1.0f;
	if(idle>=0)AdvanceLobby(playback,team,weaponId,idle,gpGlobals->time,OC2_PreviewAppearanceSignature(weaponId));
	const bool entering=idle>=0&&playEntry&&playback.elapsed<kLobbyEntryEnd/kLobbyFrameRate;
	const int sequence = idle>=0?(entering?playback.entry:idle):(ct ? selected.ctSequence : selected.tSequence);
	if (!LoadMetadata(actorPath, actorMetadata) || !ValidPose(actorMetadata, sequence)
		|| (idle>=0&&(!ValidPose(actorMetadata,idle)||(playEntry&&!ValidPose(actorMetadata,playback.entry))))) return false;
	const bool drawWeapon = selected.weaponBody >= 0;
	// The preview is the only place where the weapon model is chosen, so the
	// mapping is logged when the selection changes. A wrong model in the buy
	// menu is then visible in the log instead of having to be guessed from
	// the screen.
	static int s_loggedWeaponId = -1;
	static int s_loggedTeam = -1;
	if (weaponId != s_loggedWeaponId || team != s_loggedTeam)
	{
		s_loggedWeaponId = weaponId;
		s_loggedTeam = team;
		Con_Printf("buymenu_preview: weaponId=%d body=%d path=%s sequence=%d team=%d\n",
			weaponId, selected.weaponBody, weaponPath, sequence, team);
	}
	if (drawWeapon && (!LoadMetadata(weaponPath, weaponMetadata)
		|| !ValidPose(weaponMetadata, sequence)
		|| (idle>=0&&(!ValidPose(weaponMetadata,idle)||(playEntry&&!ValidPose(weaponMetadata,playback.entry))))
		|| selected.weaponBody >= weaponMetadata.firstBodyModels)) return false;

	ref_viewpass_t view = {};
	// The logical portrait is an anchor/hit region, not a clipping rectangle.
	// Raised weapons can leave it during walkup; only the real UI screen clips
	// the lobby scene. Shop and team cards retain their own portrait viewport.
	const bool fullScene=idle>=0&&playEntry;
	view.viewport[0] = fullScene?0:x;
	view.viewport[1] = fullScene?0:y;
	view.viewport[2] = fullScene?gpGlobals->scrWidth:width;
	view.viewport[3] = fullScene?gpGlobals->scrHeight:height;
	const float tanY = std::tan(35.0f * 0.00872664626f);
	// Preserve the original portrait's pixel focal length when extending the
	// viewport, so widening the drawing area does not enlarge or shrink actors.
	const float sceneTanY=tanY*float(view.viewport[3])/float(height);
	const float tanX = tanY * float(view.viewport[2])/float(height);
	view.fov_y = std::atan(sceneTanY)*114.591559f;
	view.fov_x = std::atan(tanX) * 114.591559f;
	// Header bounds are zero in these assets. The per-pose actor bounds put
	// feet at z~=0 and head at z~=78. Frame that full height, with side room
	// for the held weapon; weapons.mdl bounds span all 45 bodies and would
	// unnecessarily shrink the actor if used directly for camera fitting.
	// Keep the final portrait framing throughout the walkup. Its source bounds
	// cover the entrance path and would otherwise shrink/recenter the actor.
	const PoseBounds &bounds = actorMetadata.poses[idle>=0?idle:sequence];
	const float halfHeight = (bounds.maxs[2] - bounds.mins[2])*0.5f;
	const float halfDepth = (bounds.maxs[0] - bounds.mins[0])*0.5f;
	const float fitHeight = halfHeight/tanY;
	// Keep the actor at a consistent portrait size across armor, pistols and
	// long guns. Fitting the complete weapon width made every person too small.
	const float distance = fitHeight*1.02f + halfDepth*.25f + 4.0f;
	float origin[3] =
	{
		distance - facing*(bounds.mins[0] + bounds.maxs[0])*0.5f,
		-facing*(bounds.mins[1] + bounds.maxs[1])*0.5f,
		-(bounds.mins[2] + bounds.maxs[2])*0.5f
	};
	if(fullScene)
	{
		// Keep the actor on the same right-hand portrait anchor in the larger
		// scene. These values depend only on layout and the fixed idle bounds;
		// the animation frame never changes the camera, scale or anchor.
		const float unitsPerPixel=distance*2.0f*tanY/float(height);
		origin[1]-=(float(x)+float(width)*.5f-float(gpGlobals->scrWidth)*.5f)*unitsPerPixel;
		origin[2]+=(float(gpGlobals->scrHeight)*.5f-float(y)-float(height)*.5f)*unitsPerPixel;
	}
	InitializeEntity(s_actor, 1, sequence, 1, origin, gpGlobals->time);
	EngFuncs::SetModel(&s_actor, actorPath);
	if (!s_actor.model || s_actor.model->type != mod_studio) return false;
	InitializeEntity(s_glove,3,sequence,0,origin,gpGlobals->time);
	EngFuncs::SetModel(&s_glove,kGloves);
	const int gloveBody=OC2_PreviewEquippedGloveBody(s_glove);
	const bool drawGlove=gloveBody>=0&&LoadMetadata(kGloves,s_gloveMetadata)&&ValidPose(s_gloveMetadata,sequence)
		&&(idle<0||(ValidPose(s_gloveMetadata,idle)&&(!playEntry||ValidPose(s_gloveMetadata,playback.entry))));
	if(drawGlove){s_glove.curstate.body=gloveBody;s_actor.curstate.body=0;}
	if (drawWeapon)
	{
		// Both resources bake the same presentation pose into their own bones.
		// Sharing transform/sequence/frame is deliberate; MOVETYPE_FOLLOW would
		// instead merge incompatible actor/weapon bone layouts.
		InitializeEntity(s_weapon, 2, sequence, selected.weaponBody, origin, gpGlobals->time);
		EngFuncs::SetModel(&s_weapon, weaponPath);
		if (!s_weapon.model || s_weapon.model->type != mod_studio) return false;
	}
	else s_weapon.model=nullptr;
	s_actor.angles[1]=s_actor.curstate.angles[1]=presentationYaw;
	s_weapon.angles[1]=s_weapon.curstate.angles[1]=presentationYaw;
	s_glove.angles[1]=s_glove.curstate.angles[1]=presentationYaw;
	if(idle>=0)
	{
		ApplyLobbyFrame(s_actor,actorMetadata,playback,playEntry,gpGlobals->time);
		if(drawWeapon)ApplyLobbyFrame(s_weapon,weaponMetadata,playback,playEntry,gpGlobals->time);
		if(drawGlove)ApplyLobbyFrame(s_glove,s_gloveMetadata,playback,playEntry,gpGlobals->time);
	}

	s_rendering = true;
	PreviewScene preview;
	OC2_PreviewAppearance appearance(s_weapon,drawGlove?&s_glove:nullptr,weaponId);
	s_lastWeapon=weaponId;s_lastGlove=drawGlove?gloveBody:-1;s_lastSwaps=appearance.Swaps();s_lastAnimated=idle>=0;
	s_lastEntering=entering;s_lastSourceFrame=s_actor.curstate.frame*float(actorMetadata.poses[sequence].frames-1)/256.0f;
	s_lastElapsed=idle>=0?playback.elapsed:0;
	std::memcpy(s_lastViewport,view.viewport,sizeof(s_lastViewport));s_lastFov[0]=view.fov_x;s_lastFov[1]=view.fov_y;
	// Match PlayerModelView: ClearScene pushes the renderer's scene; RenderScene
	// restores 2D mode and pops it. Always pair them, even when an add fails.
	EngFuncs::ClearScene();
	const bool actorAdded = EngFuncs::CL_CreateVisibleEntity(ET_NORMAL, &s_actor) != 0;
	const bool weaponAdded = !drawWeapon || EngFuncs::CL_CreateVisibleEntity(ET_NORMAL, &s_weapon) != 0;
	const bool gloveAdded = !drawGlove || EngFuncs::CL_CreateVisibleEntity(ET_NORMAL, &s_glove) != 0;
	EngFuncs::RenderScene(&view);
	s_rendering = false;
	if(mode==PresentationMode::Team)
	{
		TeamPreviewSample &sample=s_teamSamples[team-1];
		sample.drawn=actorAdded&&weaponAdded&&gloveAdded;sample.sequence=sequence;
		sample.frame=s_actor.curstate.frame;sample.elapsed=playback.elapsed;
		std::memcpy(sample.origin,s_actor.origin,sizeof(sample.origin));
		std::memcpy(sample.viewport,view.viewport,sizeof(sample.viewport));
	}
	return actorAdded && weaponAdded && gloveAdded;
}

bool OC2_DrawBuyPreview(int team,int weaponId,int x,int y,int width,int height)
{return DrawPresentation(team,weaponId,x,y,width,height,PresentationMode::Shop);}
void OC2_PrecacheBuyModels()
{
	if(!EngFuncs::engfuncs.pfnSetModel || !EngFuncs::engfuncs.COM_LoadFile)return;
	const char *paths[]={kTActor,kCTActor,kWeapons,kM249Full,kGloves};
	ModelMetadata *metadata[]={&s_tMetadata,&s_ctMetadata,&s_weaponMetadata,&s_m249Metadata,&s_gloveMetadata};
	for(int i=0;i<5;++i)
	{
		cl_entity_t entity={};EngFuncs::SetModel(&entity,paths[i]);
		LoadMetadata(paths[i],*metadata[i]);
	}
}
bool OC2_DrawLobbyPreview(int team,int weaponId,int x,int y,int width,int height)
{return DrawPresentation(team,weaponId,x,y,width,height,PresentationMode::Lobby);}
bool OC2_DrawTeamPreview(int team,int weaponId,int x,int y,int width,int height)
{return DrawPresentation(team,weaponId,x,y,width,height,PresentationMode::Team);}
void OC2_RestartLobbyPreview(){s_lobby=LobbyPlayback();}
void OC2_RestartTeamPreviews()
{
	for(int i=0;i<2;++i){s_teamIdle[i]=LobbyPlayback();s_teamSamples[i]=TeamPreviewSample();}
}
void OC2_DumpTeamPreviews()
{
	for(int i=0;i<2;++i)
	{
		const TeamPreviewSample &sample=s_teamSamples[i];
		Con_Printf("teamselect_preview: team=%d drawn=%d sequence=%d frame=%.4f elapsed=%.4f frame_rate=%.3f origin=%.4f/%.4f/%.4f viewport=%d/%d/%d/%d\n",
			i+1,int(sample.drawn),sample.sequence,sample.frame,sample.elapsed,kLobbyFrameRate,
			sample.origin[0],sample.origin[1],sample.origin[2],sample.viewport[0],sample.viewport[1],sample.viewport[2],sample.viewport[3]);
	}
}
int OC2_LastPresentationSwaps(){return s_lastSwaps;}
void OC2_DumpPresentation(const char *tag)
{
	Con_Printf("oc2_presentation: tag=%s animated=%d sequence=%d frame=%.4f weapon=%d weapon_body=%d glove_body=%d material_swaps=%d actor=%s actor_yaw=%.3f stage=%s elapsed=%.4f source_frame=%.4f frame_rate=%.3f weapon_model=%s scene_viewport=%d/%d/%d/%d scene_fov=%.4f/%.4f\n",
		tag?tag:"none",int(s_lastAnimated),s_actor.curstate.sequence,s_actor.curstate.frame,s_lastWeapon,
		s_weapon.curstate.body,s_lastGlove,s_lastSwaps,s_actor.model?s_actor.model->name:"none",s_actor.angles[1],
		s_lastAnimated?(s_lastEntering?"entry":"idle"):"shop",s_lastElapsed,s_lastSourceFrame,s_lastAnimated?kLobbyFrameRate:0,
		s_weapon.model?s_weapon.model->name:"none",s_lastViewport[0],s_lastViewport[1],s_lastViewport[2],s_lastViewport[3],s_lastFov[0],s_lastFov[1]);
}
