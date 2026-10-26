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
#include "studio_util.h"
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
    Vector eye;
    float pitch;
    float yaw;
    double time;
    float dt;
    bool ducked;
    float stance;
    bool grounded;
    bool paused;
    bool valid;
};

struct LegPrediction
{
    Vector fromOrigin;
    Vector toOrigin;
    float fromHull;
    float toHull;
    int player;
    double endTime;
    unsigned int commandSeed;
    bool groundHullShift;
    bool valid;
};

LegView s_view = {};
LegPrediction s_prediction = {};
bool s_duckGround = false;
bool s_duckSampled = false;
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

float RenderHullHeight(const ref_params_s &view)
{
    if (!s_prediction.groundHullShift || s_prediction.fromHull == s_prediction.toHull)
        return s_prediction.toHull;
    // CL_PredictMovement lerps origin but selects the destination eye height
    // and hull. Reconstruct that lerp's hull component on the same endpoints.
    // A snapped/teleported destination gives 1; coincident origins also use the
    // destination, because no origin interpolation can be observed there.
    const Vector span = s_prediction.toOrigin - s_prediction.fromOrigin;
    const Vector offset = Vector(view.simorg) - s_prediction.fromOrigin;
    const float lengthSquared = DotProduct(span, span);
    const float fraction = lengthSquared > 0.000001f
        ? Clamp(DotProduct(offset, span) / lengthSquared, 0.0f, 1.0f) : 1.0f;
    return Mix(s_prediction.fromHull, s_prediction.toHull, fraction);
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

void MultiplyRotation(const float a[4], const float b[4], float out[4])
{
    const float value[4] = {
        a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1],
        a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0],
        a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3],
        a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2] };
    memcpy(out, value, sizeof(value));
}

void ModelPose(const mstudiobone_t *bones, int bone, float pos[][3], float q[][4],
    float origin[3], float rotation[4])
{
    int chain[MAXSTUDIOBONES], count = 0;
    for (int i = bone; i >= 0 && count < MAXSTUDIOBONES; i = bones[i].parent)
        chain[count++] = i;
    origin[0] = origin[1] = origin[2] = 0.0f;
    rotation[0] = rotation[1] = rotation[2] = 0.0f; rotation[3] = 1.0f;
    for (int i = count-1; i >= 0; --i)
    {
        float matrix[3][4];
        QuaternionMatrix(rotation, matrix);
        for (int axis = 0; axis < 3; ++axis)
            origin[axis] += matrix[axis][0]*pos[chain[i]][0] +
                matrix[axis][1]*pos[chain[i]][1] + matrix[axis][2]*pos[chain[i]][2];
        MultiplyRotation(rotation, q[chain[i]], rotation);
    }
}

void DumpLegs()
{
    gEngfuncs.Con_Printf("legs: visible=%d model=%s sequence=%d phase=%.3f grounded=%d ducked=%d origin=%.1f/%.1f/%.1f rate=%.3f stance=%.3f move=%.3f/%.3f run=%.3f jump=%.3f/%.3f eye=%.3f/%.3f/%.3f eye_body_z=%.6f pitch=%.6f vz=%.6f\n",
        int(s_lastSubmitted),
        s_modelName, s_entity.curstate.sequence,
        s_phase, int(s_view.grounded), int(s_view.ducked),
        s_view.origin.x, s_view.origin.y, s_view.origin.z, s_playbackRate, s_view.stance,
        s_standMove, s_duckMove, s_runWeight, s_jumpPhase, s_jumpWeight,
        s_view.eye.x,s_view.eye.y,s_view.eye.z,s_view.eye.z-s_view.origin.z,s_view.pitch,s_view.velocity.z);
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

void OC2_LegsPrediction(const local_state_s *from, const local_state_s *to,
    double commandTime, int commandMsec, unsigned int commandSeed)
{
    const bool duckGround = s_duckSampled && s_duckGround;
    s_duckSampled = false;
    bool append = s_prediction.valid && s_prediction.commandSeed == commandSeed &&
        commandMsec > 0 && fabs(commandTime - s_prediction.endTime) < 0.000001;
    s_prediction.valid = false;
    if (!from || !to || from->playerstate.usehull < 0 || from->playerstate.usehull > 1 ||
        to->playerstate.usehull < 0 || to->playerstate.usehull > 1 ||
        !oc2_viewmodel::Finite(commandTime) || commandMsec < 0 || commandMsec > 255) return;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (!oc2_viewmodel::Finite(from->playerstate.origin[axis]) ||
            !oc2_viewmodel::Finite(to->playerstate.origin[axis])) return;
        append = append && from->playerstate.origin[axis] == s_prediction.toOrigin[axis];
    }
    const float fromHull = from->playerstate.usehull == 1 ? 18.0f : 36.0f;
    const float toHull = to->playerstate.usehull == 1 ? 18.0f : 36.0f;
    append = append && s_prediction.toHull == fromHull &&
        s_prediction.player == to->playerstate.number - 1;
    const bool groundShift = duckGround && fromHull != toHull;
    if (!append)
    {
        s_prediction.fromOrigin = Vector(from->playerstate.origin);
        s_prediction.fromHull = fromHull;
        s_prediction.groundHullShift = groundShift;
    }
    else
        s_prediction.groundHullShift = s_prediction.groundHullShift || groundShift;
    // Xash recursively splits commands longer than 50 ms. The render lerp
    // spans the outer command, so preserve its first endpoint across halves.
    // A new seed, replayed start time or discontinuous state starts a new span.
    s_prediction.toOrigin = Vector(to->playerstate.origin);
    s_prediction.toHull = toHull;
    s_prediction.player = to->playerstate.number - 1;
    s_prediction.endTime = commandTime + commandMsec / 1000.0;
    s_prediction.commandSeed = commandSeed;
    s_prediction.valid = true;
}

void OC2_LegsDuckGround(bool grounded)
{
    s_duckGround = grounded;
    s_duckSampled = true;
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
        s_prediction.valid = false;
        s_duckSampled = false;
        s_entity.curstate.effects |= EF_NODRAW;
        s_lastSubmitted = false;
        return;
    }

    // A hull-origin change must not move the body's feet. Every sampled layer
    // uses one standing-hull anchor; the camera retains its own stair smoothing.
    s_view.origin = Vector(view->simorg[0], view->simorg[1], view->simorg[2]);
    s_view.velocity = Vector(view->simvel[0], view->simvel[1], view->simvel[2]);
    s_view.eye = Vector(view->vieworg);
    s_view.pitch = view->viewangles[0];
    s_view.yaw = view->cl_viewangles[1];
    s_view.time = view->time;
    s_view.dt = view->frametime;
    if (s_lastTime >= 0 && view->time < s_lastTime - 0.5)
        s_prediction.valid = false;
    const bool prediction = s_prediction.valid &&
        (s_prediction.player < 0 || s_prediction.player == view->playernum);
    const float hullHeight = prediction ? s_prediction.toHull
        : ((g_iPlayerFlags & FL_DUCKING) != 0 ? 18.0f : 36.0f);
    s_view.ducked = hullHeight < 36.0f;
    s_view.origin.z += 36.0f - (prediction ? RenderHullHeight(*view) : hullHeight);
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
        s_prediction.valid = false;
        s_duckSampled = false;
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
        const mstudiobone_t *bones = reinterpret_cast<const mstudiobone_t *>(
            reinterpret_cast<const byte *>(renderer.m_pStudioHeader) + renderer.m_pStudioHeader->boneindex);
        bool upper[MAXSTUDIOBONES] = {};
        int chest = -1;
        for (int bone = 0; bone < renderer.m_pStudioHeader->numbones; ++bone)
            if (strcmp(bones[bone].name, "Bip01 Spine1") == 0) { chest = bone; break; }
        float groundOrigin[3], groundRotation[4], idleOrigin[3], idleRotation[4];
        if (chest >= 0) ModelPose(bones, chest, pos, q, groundOrigin, groundRotation);
        Sample(renderer, descs, 1, s_phase, runPos, runQ);
        Sample(renderer, descs, 2, s_phase, duckPos, duckQ);
        renderer.StudioSlerpBones(runQ, runPos, duckQ, duckPos, s_stanceWeight);
        if (chest >= 0) ModelPose(bones, chest, runPos, runQ, idleOrigin, idleRotation);
        Sample(renderer, descs, 6, s_jumpPhase, movePos, moveQ);
        for (int bone = 0; bone < renderer.m_pStudioHeader->numbones; ++bone)
        {
            upper[bone] = strcmp(bones[bone].name, "Bip01 Spine1") == 0 ||
                (bones[bone].parent >= 0 && upper[bones[bone].parent]);
            if (upper[bone])
            {
                memcpy(movePos[bone], runPos[bone], sizeof(movePos[bone]));
                memcpy(moveQ[bone], runQ[bone], sizeof(moveQ[bone]));
            }
        }
        // The third-person jump swings the open collar toward the eye. Use
        // the matching stance's idle upper-chest layer during its existing
        // fade, while the root, waist and legs keep the authored jump pose.
        renderer.StudioSlerpBones(q, pos, moveQ, movePos, jumpWeight);
        if (chest >= 0 && bones[chest].parent >= 0)
        {
            // The idle LOCAL chest still inherits the jumping pelvis/Spine.
            // Resolve its target in model space, then convert only Spine1
            // back through the actual parent. This also covers descent and
            // the landing fade without moving the body anchor or lower rig.
            float parentOrigin[3], parentRotation[4], targetRotation[4];
            ModelPose(bones, bones[chest].parent, pos, q, parentOrigin, parentRotation);
            QuaternionSlerp(groundRotation, idleRotation, jumpWeight, targetRotation);
            const float inverse[4] = {-parentRotation[0], -parentRotation[1],
                -parentRotation[2], parentRotation[3]};
            MultiplyRotation(inverse, targetRotation, q[chest]);
            float matrix[3][4], delta[3];
            QuaternionMatrix(parentRotation, matrix);
            for (int axis = 0; axis < 3; ++axis)
                delta[axis] = Mix(groundOrigin[axis], idleOrigin[axis], jumpWeight) - parentOrigin[axis];
            for (int axis = 0; axis < 3; ++axis)
                pos[chest][axis] = matrix[0][axis]*delta[0] + matrix[1][axis]*delta[1] + matrix[2][axis]*delta[2];
        }
    }
    return true;
}
