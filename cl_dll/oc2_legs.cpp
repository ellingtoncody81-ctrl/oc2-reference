#include "hud.h"
#include "cl_util.h"
#include "com_model.h"
#include "entity_types.h"
#include "ref_params.h"
#include "studio.h"
#include "r_studioint.h"
#include "camera.h"
#include "const.h"
#include "oc2_legs.h"
#include "StudioModelRenderer.h"
#include "viewmodel_transition.h"

#include <math.h>
#include <string.h>

extern engine_studio_api_t IEngineStudio;
extern int g_iPlayerFlags;

namespace
{
struct LegView
{
    Vector origin;
    Vector velocity;
    float yaw;
    double time;
    float dt;
    bool ducked;
    float stance;
    bool grounded;
    bool paused;
    bool valid;
};

LegView s_view = {};
cl_entity_t s_entity = {};
cvar_t *s_showLegs = NULL;
double s_lastTime = -1.0;
float s_phase = 0.0f;
float s_jumpPhase = 0.0f;
float s_jumpElapsed = 0.0f;
float s_jumpWeight = 0.0f;
float s_standMove = 0.0f;
float s_duckMove = 0.0f;
float s_runWeight = 0.0f;
float s_stanceWeight = 0.0f;
float s_playbackRate = 0.0f;
bool s_jumping = false;
bool s_wasGrounded = true;
bool s_hidden = true;
bool s_dumpRegistered = false;
bool s_submitted = false;
bool s_lastSubmitted = false;
char s_modelName[96] = "none";

float Clamp(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

const char *ModelName(const cl_entity_t *local)
{
    const char *name = local && local->model ? local->model->name : "";
    if (strstr(name, "phoenix_varb")) return "models/oc2/legs/phoenix_varb.mdl";
    if (strstr(name, "phoenix_vara")) return "models/oc2/legs/phoenix_vara.mdl";
    if (strstr(name, "sas_varb")) return "models/oc2/legs/sas_varb.mdl";
    if (strstr(name, "sas_vara")) return "models/oc2/legs/sas_vara.mdl";
    const int index = local ? local->index : 0;
    const int team = index > 0 && index <= gEngfuncs.GetMaxClients()
        ? g_PlayerExtraInfo[index].teamnumber : 0;
    if (team == TEAM_TERRORIST) return "models/oc2/legs/phoenix_vara.mdl";
    if (team == TEAM_CT) return "models/oc2/legs/sas_vara.mdl";
    return NULL;
}

float Mix(float a, float b, float weight)
{
    return a + (b - a) * weight;
}

float Approach(float current, float target, float amount)
{
    return current + Clamp(target - current, -amount, amount);
}

float NativeCycleRate(const mstudioseqdesc_t &desc)
{
    return desc.numframes > 1 ? desc.fps / (desc.numframes - 1) : 0.0f;
}

float Stride(const mstudioseqdesc_t &desc)
{
    return sqrtf(desc.linearmovement[0] * desc.linearmovement[0] +
        desc.linearmovement[1] * desc.linearmovement[1] +
        desc.linearmovement[2] * desc.linearmovement[2]);
}

float NativeSpeed(const mstudioseqdesc_t &desc)
{
    return Stride(desc) * NativeCycleRate(desc);
}

float WrapCycle(float cycle)
{
    return cycle - floorf(cycle);
}

float GaitDirection()
{
    const float travelYaw = atan2f(s_view.velocity.y, s_view.velocity.x) * 57.2957795f;
    float delta = fmodf(travelYaw - s_view.yaw + 180.0f, 360.0f);
    if (delta < 0.0f) delta += 360.0f;
    delta -= 180.0f;
    // GoldSrc reverses its gait beyond 120 degrees instead of turning backwards.
    return fabsf(delta) > 120.0f ? -1.0f : 1.0f;
}

void Sample(CStudioModelRenderer &renderer, const mstudioseqdesc_t *descs,
    int sequence, float cycle, float pos[][3], float q[][4])
{
    const mstudioseqdesc_t &desc = descs[sequence];
    const float frame = Clamp(cycle * (desc.numframes - 1), 0.0f,
        static_cast<float>(desc.numframes - 1) - 0.001f);
    renderer.StudioSampleSequence(sequence, frame, 0, 0, pos, q);
    if (sequence == 2 || sequence == 5)
    {
        // Crouch clips were authored for the 18-unit hull origin. All layers
        // now share the standing hull's foot anchor before any pose blending.
        const mstudiobone_t *bones = reinterpret_cast<const mstudiobone_t *>(
            reinterpret_cast<const byte *>(renderer.m_pStudioHeader) + renderer.m_pStudioHeader->boneindex);
        for (int bone = 0; bone < renderer.m_pStudioHeader->numbones; ++bone)
            if (bones[bone].parent < 0) pos[bone][2] -= 18.0f;
    }
}

void DumpLegs()
{
    gEngfuncs.Con_Printf("legs: visible=%d model=%s sequence=%d phase=%.3f grounded=%d ducked=%d origin=%.1f/%.1f/%.1f rate=%.3f stance=%.3f move=%.3f/%.3f run=%.3f jump=%.3f/%.3f\n",
        int(s_lastSubmitted),
        s_modelName, s_entity.curstate.sequence,
        s_phase, int(s_view.grounded), int(s_view.ducked),
        s_view.origin.x, s_view.origin.y, s_view.origin.z, s_playbackRate, s_view.stance,
        s_standMove, s_duckMove, s_runWeight, s_jumpPhase, s_jumpWeight);
}
}

void OC2_LegsInit()
{
    if (!s_showLegs)
        s_showLegs = CVAR_CREATE("cl_showlegs", "1", FCVAR_ARCHIVE);
    if (!s_dumpRegistered)
    {
        gEngfuncs.pfnAddCommand("oc2_legs_dump", DumpLegs);
        s_dumpRegistered = true;
    }
}

void OC2_LegsBeginFrame()
{
    s_submitted = s_lastSubmitted = false;
    s_entity.curstate.effects |= EF_NODRAW;
}

void OC2_LegsRefdef(const ref_params_s *view)
{
    OC2_LegsInit();
    s_view.valid = false;
    if (!view || !s_showLegs || s_showLegs->value <= 0 || view->intermission ||
        view->spectator || g_iUser1 || CL_IsThirdPerson() || view->health <= 0 ||
        view->viewentity > view->maxclients || gEngfuncs.IsSpectateOnly())
    {
        s_hidden = true;
        s_entity.curstate.effects |= EF_NODRAW;
        s_lastSubmitted = false;
        return;
    }

    // A hull-origin change must not move the body's feet. Every sampled layer
    // uses one standing-hull anchor; the camera retains its own stair smoothing.
    s_view.origin = Vector(view->simorg[0], view->simorg[1], view->simorg[2]);
    s_view.velocity = Vector(view->simvel[0], view->simvel[1], view->simvel[2]);
    s_view.yaw = view->cl_viewangles[1];
    s_view.time = view->time;
    s_view.dt = view->frametime;
    s_view.ducked = (g_iPlayerFlags & FL_DUCKING) != 0;
    const float hullHeight = s_view.ducked ? 18.0f : 36.0f;
    s_view.origin.z += 36.0f - hullHeight;
    // Eye height above the feet is 53 standing and 30 fully crouched.
    s_view.stance = Clamp((53.0f - (hullHeight + view->viewheight[2])) / 23.0f, 0.0f, 1.0f);
    // Xash passes a boolean ref_params.onground (0 in air, 1 on ground).
    s_view.grounded = view->onground != 0;
    s_view.paused = view->paused != 0;
    s_view.valid = true;
    if (!oc2_viewmodel::Finite(s_view.velocity.x) || !oc2_viewmodel::Finite(s_view.velocity.y) ||
        !oc2_viewmodel::Finite(s_view.velocity.z)) s_view.valid = false;
    if (s_view.valid)
        OC2_LegsCreateEntity();
    else
    {
        s_entity.curstate.effects |= EF_NODRAW;
        s_lastSubmitted = false;
    }
}

void OC2_LegsCreateEntity()
{
    if (!s_view.valid || !s_showLegs || s_showLegs->value <= 0) return;
    cl_entity_t *local = gEngfuncs.GetLocalPlayer();
    const char *path = ModelName(local);
    if (!path) return;
    // These are client-only resources and are deliberately absent from the
    // server precache table. CL_LoadModel would reject that valid case.
    if (!IEngineStudio.Mod_ForName || !IEngineStudio.Mod_Extradata) return;
    model_t *model = IEngineStudio.Mod_ForName(path, 0);
    if (!model) return;
    strncpy(s_modelName, path, sizeof(s_modelName) - 1);
    s_modelName[sizeof(s_modelName) - 1] = '\0';
    studiohdr_t *header = static_cast<studiohdr_t *>(IEngineStudio.Mod_Extradata(model));
    if (!header || header->numseq <= 6) return;

    const mstudioseqdesc_t *descs = reinterpret_cast<const mstudioseqdesc_t *>(
        reinterpret_cast<const byte *>(header) + header->seqindex);
    const double now = s_view.time;
    if (!oc2_viewmodel::Finite(now)) return;
    const bool reset = s_hidden || s_lastTime < 0 || now < s_lastTime - 0.5 || model != s_entity.model;
    float dt = 0.0f;
    if (reset)
    {
        // Entity and latched state persist across frames and extra render views.
        // Reset only for a new body/life/map, never for an ordinary slow frame.
        s_entity = cl_entity_t();
        s_phase = 0.0f;
        s_jumpPhase = s_jumpElapsed = s_jumpWeight = 0.0f;
        s_standMove = s_duckMove = s_runWeight = 0.0f;
        s_stanceWeight = s_view.stance;
        s_jumping = false;
        s_wasGrounded = s_view.grounded;
        s_lastTime = now;
    }
    else if (now > s_lastTime)
    {
        // Client time, rather than callback count or repeated frametime, is the
        // authority. Small clock backsteps hold the sampled pose until caught up.
        if (!s_view.paused && s_view.dt > 0.0f)
            dt = Clamp(static_cast<float>(now - s_lastTime), 0.0f, 0.25f);
        s_lastTime = now;
    }
    s_hidden = false;
    const float speed = sqrtf(s_view.velocity.x * s_view.velocity.x + s_view.velocity.y * s_view.velocity.y);
    if (reset || dt > 0.0f)
    {
        // Follow the predicted duck curve directly; GoldSrc unducks its hull
        // instantly, so only that sharp return needs a brief visual recovery.
        s_stanceWeight = Approach(s_stanceWeight, s_view.stance, dt / 0.12f);
        const float walkSpeed = NativeSpeed(descs[3]);
        const float runSpeed = NativeSpeed(descs[4]);
        const float run = Clamp((speed - walkSpeed) / (runSpeed - walkSpeed), 0.0f, 1.0f);
        const float weightStep = dt / 0.1f;
        s_runWeight = Approach(s_runWeight, run, weightStep);
        const float standSpeed = Mix(walkSpeed, runSpeed, s_runWeight);
        s_standMove = Approach(s_standMove, Clamp(speed / standSpeed, 0.0f, 1.0f), weightStep);
        s_duckMove = Approach(s_duckMove, Clamp(speed / NativeSpeed(descs[5]), 0.0f, 1.0f), weightStep);
        const float stride = Mix(Mix(Stride(descs[3]), Stride(descs[4]), s_runWeight),
            Stride(descs[5]), s_stanceWeight);
        const float move = Mix(s_standMove, s_duckMove, s_stanceWeight);
        const float idleRate = Mix(NativeCycleRate(descs[1]), NativeCycleRate(descs[2]), s_stanceWeight);
        s_playbackRate = Mix(idleRate, GaitDirection() * speed / stride, move);
        // One continuously advancing cycle belongs to all ground activities.
        // Stopping, changing speed or crouching never retrieves an old phase.
        s_phase = WrapCycle(s_phase + s_playbackRate * dt);

        if (!s_jumping && (reset || s_wasGrounded) && !s_view.grounded && s_view.velocity.z > 1.0f)
        {
            s_jumping = true;
            s_jumpPhase = s_jumpElapsed = 0.0f;
        }
        if (s_jumping)
        {
            s_jumpElapsed += dt;
            s_jumpPhase = Clamp(s_jumpPhase + NativeCycleRate(descs[6]) * dt, 0.0f, 1.0f);
            // The first predicted airborne update can still carry the old
            // ground flag. Match Source's brief landing grace after takeoff.
            if (s_view.grounded && s_jumpElapsed >= 0.2f) s_jumping = false;
        }
        s_jumpWeight = Approach(s_jumpWeight, s_jumping ? 1.0f : 0.0f,
            dt / (s_jumping ? 0.08f : 0.12f));
        s_wasGrounded = s_view.grounded;
    }

    s_entity.model = model;
    s_entity.index = 0;
    s_entity.player = 0;
    s_view.origin.CopyToArray(s_entity.origin);
    // Keep the arm/neck openings behind the eye while retaining the feet.
    // Crouching brings the lower body closer, so use a smaller rear offset.
    const float bodyBack = Mix(8.0f, 4.0f, s_stanceWeight);
    const float bodyYaw = s_view.yaw * 0.017453292519943295f;
    s_entity.origin[0] -= cosf(bodyYaw) * bodyBack;
    s_entity.origin[1] -= sinf(bodyYaw) * bodyBack;
    s_entity.angles[0] = 0.0f;
    s_entity.angles[1] = s_view.yaw;
    s_entity.angles[2] = 0.0f;
    VectorCopy(s_entity.origin, s_entity.curstate.origin);
    VectorCopy(s_entity.angles, s_entity.curstate.angles);
    s_entity.curstate.modelindex = 0;
    const int groundSequence = s_stanceWeight > 0.5f ? (s_duckMove > 0.5f ? 5 : 2)
        : (s_standMove > 0.5f ? (s_runWeight > 0.5f ? 4 : 3) : 1);
    s_entity.curstate.sequence = s_jumpWeight > 0.5f ? 6 : groundSequence;
    // StudioEstimateFrame uses a 256-unit normalized frame. This is an explicit
    // prediction sample; renderer extrapolation must not advance it a second time.
    s_entity.curstate.frame = (s_entity.curstate.sequence == 6 ? s_jumpPhase : s_phase) * 256.0f;
    s_entity.curstate.animtime = static_cast<float>(s_lastTime);
    s_entity.curstate.framerate = 0.0f;
    s_entity.curstate.skin = 0;
    s_entity.curstate.movetype = MOVETYPE_NONE;
    s_entity.curstate.rendermode = kRenderNormal;
    s_entity.curstate.renderamt = 255;
    s_entity.curstate.effects &= ~EF_NODRAW;
    s_entity.curstate.colormap = local ? local->curstate.colormap : 0;
    // HUD_CreateEntities runs before this frame's V_CalcRefdef. Submit only
    // after sampling the current view, and reuse the pointer for extra views.
    if (!s_submitted)
        s_submitted = gEngfuncs.CL_CreateVisibleEntity(ET_NORMAL, &s_entity) != 0;
    s_lastSubmitted = s_submitted;
}

bool OC2_LegsIsEntity(const cl_entity_s *entity)
{
    return entity == &s_entity;
}

bool OC2_LegsBlend(CStudioModelRenderer &renderer, float pos[][3], float q[][4])
{
    if (!OC2_LegsIsEntity(renderer.m_pCurrentEntity)) return false;
    const mstudioseqdesc_t *descs = reinterpret_cast<const mstudioseqdesc_t *>(
        reinterpret_cast<const byte *>(renderer.m_pStudioHeader) + renderer.m_pStudioHeader->seqindex);
    static float movePos[MAXSTUDIOBONES][3], duckPos[MAXSTUDIOBONES][3], runPos[MAXSTUDIOBONES][3];
    static vec4_t moveQ[MAXSTUDIOBONES], duckQ[MAXSTUDIOBONES], runQ[MAXSTUDIOBONES];
    Sample(renderer, descs, 1, s_phase, pos, q);
    Sample(renderer, descs, 3, s_phase, movePos, moveQ);
    Sample(renderer, descs, 4, s_phase, runPos, runQ);
    renderer.StudioSlerpBones(moveQ, movePos, runQ, runPos, s_runWeight);
    renderer.StudioSlerpBones(q, pos, moveQ, movePos, s_standMove);
    Sample(renderer, descs, 2, s_phase, duckPos, duckQ);
    Sample(renderer, descs, 5, s_phase, movePos, moveQ);
    renderer.StudioSlerpBones(duckQ, duckPos, moveQ, movePos, s_duckMove);
    // Stance is the real eye/hull transition, with no second sequence fade.
    renderer.StudioSlerpBones(q, pos, duckQ, duckPos, s_stanceWeight);
    // This rig has an upright jump clip but no crouch-jump pose. Preserve the
    // duck layer under the lowered eye; the airborne clock keeps advancing.
    const float jumpWeight = s_jumpWeight * (1.0f - s_stanceWeight);
    if (jumpWeight > 0.0f)
    {
        Sample(renderer, descs, 6, s_jumpPhase, movePos, moveQ);
        renderer.StudioSlerpBones(q, pos, moveQ, movePos, jumpWeight);
    }
    return true;
}
