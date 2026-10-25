#include "hud.h"
#include "cl_util.h"
#include "com_model.h"
#include "entity_types.h"
#include "ref_params.h"
#include "studio.h"
#include "r_studioint.h"
#include "camera.h"
#include "oc2_legs.h"
#include "StudioModelRenderer.h"
#include "viewmodel_transition.h"

#include <math.h>
#include <string.h>

extern engine_studio_api_t IEngineStudio;

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
    bool grounded;
    bool paused;
    bool valid;
};

LegView s_view = {};
cl_entity_t s_entity = {};
cvar_t *s_showLegs = NULL;
double s_lastTime = -1.0;
double s_animationTime = 0.0;
float s_phase = 0.0f;
float s_gaitPhase = 0.0f;
float s_playbackRate = 0.0f;
double s_sequenceStart = 0.0;
int s_previousSequence = -1;
bool s_hidden = true;
oc2_viewmodel::Transitioner s_transitions;
bool s_dumpRegistered = false;
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

bool Locomotion(int sequence)
{
    return sequence >= 3 && sequence <= 5;
}

int Sequence(const LegView &view, int previous)
{
    const float speed = sqrtf(view.velocity.x * view.velocity.x + view.velocity.y * view.velocity.y);
    if (!view.grounded) return 6; // jump
    // Separate entry/exit thresholds prevent prediction noise from repeatedly
    // restarting a walk/run or idle transition around one exact speed.
    const bool moving = speed > (Locomotion(previous) ? 6.0f : 12.0f);
    if (view.ducked) return moving ? 5 : 2;
    if (moving && speed > (previous == 4 ? 140.0f : 165.0f)) return 4;
    if (moving) return 3;
    return 1; // stand
}

float CycleRate(const mstudioseqdesc_t &desc, int sequence, float speed)
{
    if (desc.numframes <= 1) return 0.0f;
    const float distance = sqrtf(desc.linearmovement[0] * desc.linearmovement[0] +
        desc.linearmovement[1] * desc.linearmovement[1] +
        desc.linearmovement[2] * desc.linearmovement[2]);
    if (Locomotion(sequence) && distance > 1.0f) return speed / distance;
    return desc.fps / (desc.numframes - 1);
}

void DumpLegs()
{
    gEngfuncs.Con_Printf("legs: visible=%d model=%s sequence=%d phase=%.3f grounded=%d ducked=%d origin=%.1f/%.1f/%.1f rate=%.3f layers=%d\n",
        int(s_lastSubmitted),
        s_modelName, s_entity.curstate.sequence,
        s_phase, int(s_view.grounded), int(s_view.ducked),
        s_view.origin.x, s_view.origin.y, s_view.origin.z, s_playbackRate, s_transitions.Count());
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

void OC2_LegsRefdef(const ref_params_s *view)
{
    OC2_LegsInit();
    s_view.valid = false;
    if (!view || !s_showLegs || s_showLegs->value <= 0 || view->intermission ||
        view->spectator || g_iUser1 || CL_IsThirdPerson() || view->health <= 0 ||
        view->viewentity > view->maxclients || gEngfuncs.IsSpectateOnly())
    {
        s_hidden = true;
        return;
    }

    // The player collision origin sits on the predicted floor. Keep the body
    // there while the camera independently eases stairs, bobs and recoils.
    s_view.origin = Vector(view->simorg[0], view->simorg[1], view->simorg[2]);
    s_view.velocity = Vector(view->simvel[0], view->simvel[1], view->simvel[2]);
    s_view.yaw = view->cl_viewangles[1];
    s_view.time = view->time;
    s_view.dt = view->frametime;
    // This project uses PM_VEC_VIEW=17 and PM_VEC_DUCK_VIEW=12.
    s_view.ducked = view->viewheight[2] < 14.5f;
    // Xash passes a boolean ref_params.onground (0 in air, 1 on ground).
    s_view.grounded = view->onground != 0;
    s_view.paused = view->paused != 0;
    s_view.valid = true;
    if (!oc2_viewmodel::Finite(s_view.velocity.x) || !oc2_viewmodel::Finite(s_view.velocity.y) ||
        !oc2_viewmodel::Finite(s_view.velocity.z)) s_view.valid = false;
}

void OC2_LegsCreateEntity()
{
    s_lastSubmitted = false;
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
        s_gaitPhase = 0.0f;
        s_previousSequence = -1;
        s_lastTime = now;
        s_animationTime = now;
        s_transitions.Clear();
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
        s_animationTime += dt;
        const int sequence = Sequence(s_view, s_previousSequence);
        if (sequence != s_previousSequence)
        {
            s_entity.latched.prevsequence = s_previousSequence;
            if (s_previousSequence >= 0)
                s_entity.latched.prevframe = s_phase * (descs[s_previousSequence].numframes - 1);
            s_entity.latched.sequencetime = static_cast<float>(s_animationTime);
            // Preserve planted-foot phase when changing speed or stance. Idle
            // breathing and a new nonlooping jump have their own timeline.
            s_phase = Locomotion(sequence) ? s_gaitPhase : 0.0f;
            s_sequenceStart = s_animationTime;
            s_previousSequence = sequence;
        }
        const mstudioseqdesc_t &desc = descs[s_previousSequence];
        const float cycles = CycleRate(desc, s_previousSequence, speed);
        s_phase += cycles * dt;
        if (desc.flags & STUDIO_LOOPING)
            s_phase -= floorf(s_phase);
        else if (desc.numframes > 1)
            s_phase = Clamp(s_phase, 0.0f, 1.0f - 0.001f / (desc.numframes - 1));
        if (Locomotion(s_previousSequence)) s_gaitPhase = s_phase;
        s_playbackRate = desc.fps > 0.0f ? cycles * (desc.numframes - 1) / desc.fps : 0.0f;
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
    s_entity.curstate.sequence = s_previousSequence;
    // StudioEstimateFrame uses a 256-unit normalized frame. This is an explicit
    // prediction sample; renderer extrapolation must not advance it a second time.
    s_entity.curstate.frame = s_phase * 256.0f;
    s_entity.curstate.animtime = static_cast<float>(s_lastTime);
    s_entity.curstate.framerate = 0.0f;
    s_entity.curstate.skin = 0;
    s_entity.curstate.movetype = MOVETYPE_NONE;
    s_entity.curstate.rendermode = kRenderNormal;
    s_entity.curstate.renderamt = 255;
    s_entity.curstate.colormap = local ? local->curstate.colormap : 0;
    s_lastSubmitted = gEngfuncs.CL_CreateVisibleEntity(ET_NORMAL, &s_entity) != 0;
}

bool OC2_LegsBlend(CStudioModelRenderer &renderer, float pos[][3], float q[][4])
{
    if (renderer.m_pCurrentEntity != &s_entity) return false;
    const int sequence = s_entity.curstate.sequence;
    const mstudioseqdesc_t &desc = reinterpret_cast<const mstudioseqdesc_t *>(
        reinterpret_cast<const byte *>(renderer.m_pStudioHeader) + renderer.m_pStudioHeader->seqindex)[sequence];
    oc2_viewmodel::Layer current;
    current.sequence = sequence;
    current.frame = s_phase * (desc.numframes - 1);
    current.frames = desc.numframes;
    current.fps = desc.fps;
    current.rate = s_playbackRate;
    current.loop = (desc.flags & STUDIO_LOOPING) != 0;
    current.sampleTime = s_animationTime;
    current.startTime = s_sequenceStart;
    s_transitions.Observe(reinterpret_cast<uintptr_t>(s_entity.model), current, renderer.m_fDoInterp != 0);
    static float oldPos[MAXSTUDIOBONES][3];
    static vec4_t oldQ[MAXSTUDIOBONES];
    for (int layer = s_transitions.Count() - 1; layer >= 0; --layer)
    {
        const oc2_viewmodel::Layer &old = s_transitions.Previous(layer);
        if (old.sequence < 0 || old.sequence >= renderer.m_pStudioHeader->numseq) continue;
        // Outgoing gait keeps moving during the fade, including interrupted
        // crouch/start/stop transitions, instead of holding one frozen old frame.
        renderer.StudioSampleSequence(old.sequence, s_transitions.PreviousFrame(layer, s_animationTime),
            old.blend0, old.blend1, oldPos, oldQ);
        renderer.StudioSlerpBones(q, pos, oldQ, oldPos, s_transitions.Weight(layer, s_animationTime));
    }
    return true;
}
