#include "hud.h"
#include "cl_util.h"
#include "const.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "event_api.h"
#include "r_efx.h"
#include "studio.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "eventscripts.h"
#include "oc2_ragdoll.h"
#include "player_model_policy.h"

#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

extern engine_studio_api_t IEngineStudio;

namespace
{
enum Joint
{
    PELVIS, WAIST, CHEST, HEAD,
    LEFT_SHOULDER, LEFT_ELBOW, LEFT_HAND,
    RIGHT_SHOULDER, RIGHT_ELBOW, RIGHT_HAND,
    LEFT_HIP, LEFT_KNEE, LEFT_FOOT,
    RIGHT_HIP, RIGHT_KNEE, RIGHT_FOOT,
    JOINT_COUNT
};

const char *const kBoneNames[JOINT_COUNT] = {
    "Bip01 Pelvis", "Bip01 Spine1", "Bip01 Spine3", "Bip01 Head",
    "Bip01 L UpperArm", "Bip01 L Forearm", "Bip01 L Hand",
    "Bip01 R UpperArm", "Bip01 R Forearm", "Bip01 R Hand",
    "Bip01 L Thigh", "Bip01 L Calf", "Bip01 L Foot",
    "Bip01 R Thigh", "Bip01 R Calf", "Bip01 R Foot"
};

const int kParent[JOINT_COUNT] = {
    -1, PELVIS, WAIST, CHEST,
    CHEST, LEFT_SHOULDER, LEFT_ELBOW,
    CHEST, RIGHT_SHOULDER, RIGHT_ELBOW,
    PELVIS, LEFT_HIP, LEFT_KNEE,
    PELVIS, RIGHT_HIP, RIGHT_KNEE
};

// Chest and pelvis are separate braced bodies sharing the waist socket.
// Keeping every chest-to-hip diagonal rigid would turn the spine into a plank.
const float kMass[JOINT_COUNT] = {
    12.0f, 8.0f, 12.0f, 6.0f, 3.0f, 2.0f, 1.0f, 3.0f, 2.0f, 1.0f,
    6.0f, 4.0f, 1.5f, 6.0f, 4.0f, 1.5f
};
const int kClusters[2][4] = {
    {PELVIS, WAIST, LEFT_HIP, RIGHT_HIP},
    {WAIST, CHEST, LEFT_SHOULDER, RIGHT_SHOULDER}
};
const int kChains[4][3] = {
    {LEFT_SHOULDER, LEFT_ELBOW, LEFT_HAND},
    {RIGHT_SHOULDER, RIGHT_ELBOW, RIGHT_HAND},
    {LEFT_HIP, LEFT_KNEE, LEFT_FOOT},
    {RIGHT_HIP, RIGHT_KNEE, RIGHT_FOOT}
};
const int kSegments[11][2] = {
    {PELVIS, WAIST}, {WAIST, CHEST}, {CHEST, HEAD},
    {LEFT_SHOULDER, LEFT_ELBOW}, {LEFT_ELBOW, LEFT_HAND},
    {RIGHT_SHOULDER, RIGHT_ELBOW}, {RIGHT_ELBOW, RIGHT_HAND},
    {LEFT_HIP, LEFT_KNEE}, {LEFT_KNEE, LEFT_FOOT},
    {RIGHT_HIP, RIGHT_KNEE}, {RIGHT_KNEE, RIGHT_FOOT}
};
const int MAX_SEGMENT_SAMPLES = 22;

struct Frame
{
    Vector side, forward, up;
};

struct JointState
{
    int bone;
    Vector base;
    Vector position;
    Vector previous;
    Vector lastFree;
    float radius;
};

struct SegmentSample
{
    int first, second;
    float fraction;
    bool dynamic;
    bool enabled;
    JointState volume;
};

struct Body
{
    int serial;
    int playerIndex;
    int userid;
    unsigned deathGeneration;
    bool ready;
    bool early;
    bool legacySeen;
    bool hasLivePose;
    bool hasLiveVelocity;
    bool headshot;
    bool sleeping;
    int quietFrames;
    int stationaryFrames;
    Vector stationaryAnchor;
    unsigned int steps;
    unsigned int contacts;
    int catchupSteps;
    float accumulator;
    float initialHeadZ;
    double birth;
    double lastTime;
    double expires;
    Vector launch;
    Vector impact;
    Vector deathOrigin;
    tempent_s *corpse;
    model_t *model;
    studiohdr_t *header;
    int boneCount;
    JointState joints[JOINT_COUNT];
    int sampleCount;
    SegmentSample samples[MAX_SEGMENT_SAMPLES];
    float rest[JOINT_COUNT];
    float clusterRest[2][4][4];
    Frame baseFrame;
    Frame basePelvisFrame;
    Vector hingeNormal[4];
    unsigned char hingeStop[4];
    int boneOwner[MAXSTUDIOBONES];
    Vector boneParentOffset[MAXSTUDIOBONES];
    Vector boneVelocity[MAXSTUDIOBONES];
    float baseMatrix[MAXSTUDIOBONES][3][4];
};

struct LivePose
{
    bool valid;
    bool velocityValid;
    model_t *model;
    double time;
    int boneCount;
    Vector origin;
    Vector angles;
    float matrix[MAXSTUDIOBONES][3][4];
    Vector velocity[MAXSTUDIOBONES];
};

// GoldSrc sends DeathMsg at the live/dead edge, then ClCorpse only after the
// server death animation. Both describe one death, not two physics objects.
// Keep ownership after the tempent expires, until an authoritative alive edge;
// position, sequence and the age of the first confirmation are not identities.
struct PlayerDeath
{
    int userid;
    unsigned generation;
    bool dead;
    bool known;
    bool claimed;
    tempent_s *corpse;
    int corpseTag;
    unsigned confirmations;
};

Body s_bodies[64] = {};
LivePose s_livePose[33] = {};
PlayerDeath s_deaths[33] = {};
int s_nextSerial = 0;
int s_pendingEarlyVictim = 0;
int s_pendingKiller = 0;
bool s_pendingHeadshot = false;
cvar_t *s_ragdoll = NULL;
bool s_dumpRegistered = false;

PlayerDeath &DeathState(int playerIndex)
{
    PlayerDeath &state = s_deaths[playerIndex];
    const player_info_t *info = IEngineStudio.PlayerInfo
        ? IEngineStudio.PlayerInfo(playerIndex - 1) : NULL;
    const int userid = info ? info->userid : 0;
    if (state.userid != userid)
    {
        state = PlayerDeath();
        state.userid = userid;
        s_livePose[playerIndex] = LivePose();
    }
    return state;
}

void BeginDeath(PlayerDeath &state)
{
    if (state.dead) return;
    ++state.generation;
    if (!state.generation) ++state.generation;
    state.dead = state.known = true;
    state.claimed = false;
    state.corpse = NULL;
    state.corpseTag = 0;
    state.confirmations = 0;
}

float Clamp(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

float Dot(const Vector &a, const Vector &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vector Cross(const Vector &a, const Vector &b)
{
    return Vector(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x);
}

float Length(const Vector &vector)
{
    return sqrtf(Dot(vector, vector));
}

Vector Origin(const float matrix[3][4])
{
    return Vector(matrix[0][3], matrix[1][3], matrix[2][3]);
}

int FindBone(const studiohdr_t *header, const char *name)
{
    const mstudiobone_t *bones = reinterpret_cast<const mstudiobone_t *>(
        reinterpret_cast<const byte *>(header) + header->boneindex);
    for (int i = 0; i < header->numbones; ++i)
        if (!strcmp(bones[i].name, name)) return i;
    return -1;
}

bool SupportedSkeleton(const studiohdr_t *header)
{
    if (!header || header->numbones <= 0 || header->numbones > MAXSTUDIOBONES)
        return false;
    for (int joint = 0; joint < JOINT_COUNT; ++joint)
    {
        if (FindBone(header, kBoneNames[joint]) >= 0) continue;
        if (joint == HEAD && FindBone(header, "Bip01 Neck") >= 0) continue;
        if (joint == CHEST && FindBone(header, "Bip01 Spine2") >= 0) continue;
        return false;
    }
    return true;
}

int FindLiveIdleSequence(const studiohdr_t *header)
{
    if (!header || header->numseq <= 0) return 0;
    const mstudioseqdesc_t *sequences = reinterpret_cast<const mstudioseqdesc_t *>(
        reinterpret_cast<const byte *>(header) + header->seqindex);
    for (int i = 0; i < header->numseq; ++i)
        if (!strncmp(sequences[i].label, "idle1", sizeof(sequences[i].label)))
            return i;
    for (int i = 0; i < header->numseq; ++i)
        if (!strncmp(sequences[i].label, "idle", 4) &&
            strncmp(sequences[i].label, "crouch", 6))
            return i;
    return 0;
}

bool IsDeathAnimation(model_t *model, int sequence)
{
    if (!model || !IEngineStudio.Mod_Extradata)
        return false;
    const studiohdr_t *header = static_cast<const studiohdr_t *>(
        IEngineStudio.Mod_Extradata(model));
    if (!header || sequence < 0 || sequence >= header->numseq)
        return false;
    const mstudioseqdesc_t *sequences = reinterpret_cast<const mstudioseqdesc_t *>(
        reinterpret_cast<const byte *>(header) + header->seqindex);
    const char *label = sequences[sequence].label;
    return !strncmp(label, "death", 5) ||
        !strncmp(label, "crouch_die", 10) ||
        !strncmp(label, "head", 5) ||
        !strncmp(label, "gutshot", 8) ||
        !strncmp(label, "left", 5) || !strncmp(label, "back", 5) ||
        !strncmp(label, "right", 6) || !strncmp(label, "forward", 8);
}

model_t *RenderedPlayerModel(int playerIndex)
{
    if (playerIndex < 1 || playerIndex > 32) return NULL;
    const char *teamModel = oc2_player_model::TeamModelPath(g_PlayerExtraInfo[playerIndex].teamnumber);
    if (teamModel) return gEngfuncs.CL_LoadModel(teamModel, NULL);
    // SetupPlayerModel reads RI.currententity as its fallback. A DeathMsg
    // arrives outside the render callback, so resolve the same userinfo path
    // directly instead of touching that renderer-global pointer.
    const player_info_t *info = IEngineStudio.PlayerInfo
        ? IEngineStudio.PlayerInfo(playerIndex - 1) : NULL;
    if (!info || !info->model[0] || !IEngineStudio.Mod_ForName)
        return NULL;
    char path[2 * MAX_QPATH + 32];
    snprintf(path, sizeof(path), "models/player/%s/%s.mdl",
        info->model, info->model);
    return IEngineStudio.Mod_ForName(path, 0);
}

Vector RotateArc(const Vector &vector, const Vector &from, const Vector &to)
{
    const float fromLength = Length(from);
    const float toLength = Length(to);
    if (fromLength < 0.001f || toLength < 0.001f) return vector;
    const Vector a = from * (1.0f / fromLength);
    const Vector b = to * (1.0f / toLength);
    const float cosine = Clamp(Dot(a, b), -1.0f, 1.0f);
    Vector axis = Cross(a, b);
    const float sine = Length(axis);
    if (sine < 0.0001f)
    {
        if (cosine > 0.0f) return vector;
        axis = Cross(a, Vector(0.0f, 0.0f, 1.0f));
        if (Length(axis) < 0.001f) axis = Cross(a, Vector(1.0f, 0.0f, 0.0f));
        axis = axis * (1.0f / Length(axis));
        return axis * (2.0f * Dot(axis, vector)) - vector;
    }
    axis = axis * (1.0f / sine);
    return vector * cosine + Cross(axis, vector) * sine +
        axis * (Dot(axis, vector) * (1.0f - cosine));
}

Vector Unit(const Vector &value, const Vector &fallback)
{
    const float length = Length(value);
    return length > 0.0001f ? value * (1.0f / length) : fallback;
}

Vector Limited(const Vector &value, float maximum)
{
    const float length = Length(value);
    if (!(length < 1000000.0f)) return Vector(0.0f, 0.0f, 0.0f);
    return length > maximum ? value * (maximum / length) : value;
}

Frame TorsoFrame(const Body &body, bool base)
{
    const Vector pelvis = base ? body.joints[WAIST].base : body.joints[WAIST].position;
    const Vector chest = base ? body.joints[CHEST].base : body.joints[CHEST].position;
    const Vector left = base ? body.joints[LEFT_SHOULDER].base : body.joints[LEFT_SHOULDER].position;
    const Vector right = base ? body.joints[RIGHT_SHOULDER].base : body.joints[RIGHT_SHOULDER].position;
    Frame frame;
    frame.up = Unit(chest - pelvis, Vector(0.0f, 0.0f, 1.0f));
    frame.side = Unit(right - left - frame.up * Dot(right - left, frame.up),
        Vector(0.0f, -1.0f, 0.0f));
    frame.forward = Unit(Cross(frame.up, frame.side), Vector(1.0f, 0.0f, 0.0f));
    frame.side = Cross(frame.forward, frame.up);
    return frame;
}

Frame PelvisFrame(const Body &body, bool base)
{
    const Vector pelvis = base ? body.joints[PELVIS].base : body.joints[PELVIS].position;
    const Vector waist = base ? body.joints[WAIST].base : body.joints[WAIST].position;
    const Vector left = base ? body.joints[LEFT_HIP].base : body.joints[LEFT_HIP].position;
    const Vector right = base ? body.joints[RIGHT_HIP].base : body.joints[RIGHT_HIP].position;
    Frame frame;
    frame.up = Unit(waist - pelvis, Vector(0.0f, 0.0f, 1.0f));
    frame.side = Unit(right - left - frame.up * Dot(right - left, frame.up),
        Vector(0.0f, -1.0f, 0.0f));
    frame.forward = Unit(Cross(frame.up, frame.side), Vector(1.0f, 0.0f, 0.0f));
    frame.side = Cross(frame.forward, frame.up);
    return frame;
}

Vector RotateFrame(const Vector &value, const Frame &from, const Frame &to)
{
    return to.side * Dot(value, from.side) + to.forward * Dot(value, from.forward) +
        to.up * Dot(value, from.up);
}

Vector HingeNormal(const Body &body, int chain, const Frame &frame)
{
    const int first = kChains[chain][0], middle = kChains[chain][1];
    const Frame &base = chain < 2 ? body.baseFrame : body.basePelvisFrame;
    const Vector oldAxis = RotateFrame(body.joints[middle].base - body.joints[first].base,
        base, frame);
    const Vector axis = body.joints[middle].position - body.joints[first].position;
    return Unit(RotateArc(RotateFrame(body.hingeNormal[chain], base, frame),
        oldAxis, axis), frame.side);
}

void SolveDistance(Body &body, int first, int second, float minimum, float maximum)
{
    Vector delta = body.joints[second].position - body.joints[first].position;
    const float distance = Length(delta);
    if (distance < 0.0001f) return;
    const float target = Clamp(distance, minimum, maximum);
    const float correction = (distance - target) / distance;
    const float firstMass = 1.0f / kMass[first];
    const float secondMass = 1.0f / kMass[second];
    const float sum = firstMass + secondMass;
    body.joints[first].position = body.joints[first].position + delta * (correction * firstMass / sum);
    body.joints[second].position = body.joints[second].position - delta * (correction * secondMass / sum);
}

void SolveHinge(Body &body, int chain, const Frame &frame)
{
    const int first = kChains[chain][0], middle = kChains[chain][1], last = kChains[chain][2];
    const Vector upper = Unit(body.joints[middle].position - body.joints[first].position,
        frame.up * -1.0f);
    const Vector lower = body.joints[last].position - body.joints[middle].position;
    const Vector normal = HingeNormal(body, chain, frame);
    const Vector bend = Cross(normal, upper);
    // A hinge has one bend axis. In particular a knee cannot reverse through
    // its straight position or fold sideways when its foot catches the floor.
    const float limit = chain < 2 ? 2.60f : 2.65f;
    const float signedAngle = atan2f(Dot(lower, bend), Dot(lower, upper));
    // Outside the permitted arc, remember which stop was selected until the
    // joint re-enters the interior. Otherwise a contact oscillating across the
    // negative-angle branch boundary switches the target between straight and
    // fully folded every solver iteration, injecting large limb velocities.
    float angle;
    if (signedAngle < 0.0f)
    {
        if (!body.hingeStop[chain])
            body.hingeStop[chain] = signedAngle < -1.5708f ? 2 : 1;
        angle = body.hingeStop[chain] == 2 ? limit : 0.015f;
    }
    else
    {
        angle = Clamp(signedAngle, 0.015f, limit);
        if (signedAngle > 0.075f && signedAngle < limit - 0.075f)
            body.hingeStop[chain] = 0;
    }
    const Vector desired = (upper * cosf(angle) + bend * sinf(angle)) * body.rest[last];
    const Vector correction = (desired - lower) * 0.85f;
    const float middleWeight = 1.0f / kMass[middle], lastWeight = 1.0f / kMass[last];
    const float sum = middleWeight + lastWeight;
    body.joints[middle].position = body.joints[middle].position - correction * (middleWeight / sum);
    body.joints[last].position = body.joints[last].position + correction * (lastWeight / sum);
}

void SolveCone(Body &body, int first, int second, const Vector &axis, float cosine)
{
    const Vector delta = body.joints[second].position - body.joints[first].position;
    const Vector direction = Unit(delta, axis);
    if (Dot(direction, axis) >= cosine) return;
    const Vector tangent = Unit(direction - axis * Dot(direction, axis),
        Unit(Cross(axis, Vector(1.0f, 0.0f, 0.0f)), Vector(0.0f, 1.0f, 0.0f)));
    const Vector desired = (axis * cosine + tangent * sqrtf(1.0f - cosine * cosine)) * body.rest[second];
    const Vector correction = (desired - delta) * 0.65f;
    const float firstWeight = 1.0f / kMass[first], secondWeight = 1.0f / kMass[second];
    const float sum = firstWeight + secondWeight;
    body.joints[first].position = body.joints[first].position - correction * (firstWeight / sum);
    body.joints[second].position = body.joints[second].position + correction * (secondWeight / sum);
}

void SolveWaistTwist(Body &body, const Frame &chest, const Frame &pelvis)
{
    const Vector referenceUp = RotateFrame(body.baseFrame.up, body.basePelvisFrame, pelvis);
    const Vector referenceSide = Unit(RotateArc(RotateFrame(body.baseFrame.side,
        body.basePelvisFrame, pelvis), referenceUp, chest.up), chest.side);
    const float angle = atan2f(Dot(Cross(referenceSide, chest.side), chest.up),
        Dot(referenceSide, chest.side));
    const float correction = (Clamp(angle, -0.75f, 0.75f) - angle) * 0.35f;
    if (fabsf(correction) < 0.0001f) return;
    const Vector socket = body.joints[WAIST].position;
    Vector centerChange(0, 0, 0);
    float mass = 0.0f;
    for (int joint = 0; joint < JOINT_COUNT; ++joint)
    {
        const float turn = correction * (joint > WAIST && joint <= RIGHT_HAND ? 0.5f : -0.5f);
        const Vector offset = body.joints[joint].position - socket;
        const Vector rotated = offset * cosf(turn) + Cross(chest.up, offset) * sinf(turn) +
            chest.up * (Dot(chest.up, offset) * (1.0f - cosf(turn)));
        centerChange = centerChange + (rotated - offset) * kMass[joint];
        mass += kMass[joint];
        body.joints[joint].position = socket + rotated;
    }
    // This is an internal angular constraint, so retain its center of mass.
    for (int joint = 0; joint < JOINT_COUNT; ++joint)
        body.joints[joint].position = body.joints[joint].position - centerChange * (1.0f / mass);
}

void SolveSkeleton(Body &body)
{
    for (int cluster = 0; cluster < 2; ++cluster)
        for (int first = 0; first < 4; ++first)
            for (int second = first + 1; second < 4; ++second)
                SolveDistance(body, kClusters[cluster][first], kClusters[cluster][second],
                    body.clusterRest[cluster][first][second], body.clusterRest[cluster][first][second]);
    const Frame frame = TorsoFrame(body, false);
    const Frame pelvis = PelvisFrame(body, false);
    for (int i = 1; i < JOINT_COUNT; ++i)
        SolveDistance(body, kParent[i], i, body.rest[i], body.rest[i]);
    for (int chain = 0; chain < 4; ++chain)
    {
        const Frame &socket = chain < 2 ? frame : pelvis;
        const Frame &base = chain < 2 ? body.baseFrame : body.basePelvisFrame;
        const Vector socketAxis = Unit(RotateFrame(body.joints[kChains[chain][1]].base -
            body.joints[kChains[chain][0]].base, base, socket), socket.up * -1.0f);
        SolveCone(body, kChains[chain][0], kChains[chain][1], socketAxis, -0.85f);
        if (chain >= 2)
        {
            SolveCone(body, kChains[chain][0], kChains[chain][1], pelvis.up * -1.0f, -0.77f);
            // Limit lateral hip abduction to 35 degrees, independently of
            // forward/backward flexion. Stay in the mass-weighted contact solver.
            const Vector inward = pelvis.side * (chain == 2 ? 1.0f : -1.0f);
            SolveCone(body, kChains[chain][0], kChains[chain][1], inward, -0.57357644f);
        }
        SolveHinge(body, chain, socket);
    }
    const Vector spine = Unit(RotateFrame(body.joints[CHEST].base - body.joints[WAIST].base,
        body.basePelvisFrame, pelvis), pelvis.up);
    SolveCone(body, WAIST, CHEST, spine, 0.55f);
    SolveWaistTwist(body, TorsoFrame(body, false), PelvisFrame(body, false));
    const Vector neck = Unit(RotateFrame(body.joints[HEAD].base - body.joints[CHEST].base,
        body.baseFrame, frame), frame.up);
    SolveCone(body, CHEST, HEAD, neck, 0.64f);
}

void TraceWorld(const Vector &start, const Vector &end, pmtrace_t &trace)
{
    Vector traceStart = start;
    Vector traceEnd = end;
    gEngfuncs.pEventAPI->EV_SetTraceHull(2);
    gEngfuncs.pEventAPI->EV_PlayerTrace(traceStart, traceEnd,
        PM_STUDIO_BOX | PM_WORLD_ONLY, -1, &trace);
}

bool WorldFree(const Vector &point)
{
    Vector tracePoint = point;
    return gEngfuncs.PM_PointContents(tracePoint, NULL) != CONTENTS_SOLID;
}

bool ContactPlaneApplies(const Vector &position, const Vector &normal, float offset, float radius)
{
    // A BSP trace returns an infinite plane, but its face may end at a stair
    // or a narrow ledge. Reuse it only while the projected surface point still
    // straddles solid world. Otherwise a departed face stretches the skeleton.
    const Vector surface = position + normal * (offset - radius - 0.1f - Dot(position, normal));
    return !WorldFree(surface - normal * 0.125f) && WorldFree(surface + normal * 0.125f);
}

Vector RecoverFree(const JointState &joint, const Vector &start)
{
    if (WorldFree(joint.lastFree)) return joint.lastFree;
    if (WorldFree(start)) return start;
    // A bone can spawn partly in a brush or be pulled through one by a
    // constraint. Search a bounded neighborhood instead of adding height
    // on every physics step, which used to accelerate corpses up walls.
    const Vector directions[] = {
        Vector(0.0f, 0.0f, 1.0f), Vector(1.0f, 0.0f, 0.0f),
        Vector(-1.0f, 0.0f, 0.0f), Vector(0.0f, 1.0f, 0.0f),
        Vector(0.0f, -1.0f, 0.0f)
    };
    for (int distance = 2; distance <= 16; distance *= 2)
        for (int direction = 0; direction < 5; ++direction)
        {
            const Vector candidate = start + directions[direction] * static_cast<float>(distance);
            if (WorldFree(candidate)) return candidate;
        }
    return joint.lastFree;
}

bool Collide(JointState &joint, const Vector &start, Vector &normal, bool &stuck, float &planeOffset)
{
    normal = Vector(0.0f, 0.0f, 0.0f);
    planeOffset = 0.0f;
    stuck = false;
    pmtrace_t trace;
    const Vector travel = joint.position - start;
    const Vector sweepEnd = joint.position + Unit(travel, Vector(0.0f, 0.0f, 0.0f)) * (joint.radius + 0.1f);
    TraceWorld(start, sweepEnd, trace);
    if (trace.startsolid || trace.allsolid)
    {
        joint.position = RecoverFree(joint, start);
        stuck = true;
        return true;
    }
    if (trace.fraction < 1.0f)
    {
        normal = Vector(trace.plane.normal);
        planeOffset = Dot(Vector(trace.endpos), normal) + joint.radius + 0.1f;
        const float depth = planeOffset - Dot(joint.position, normal);
        if (depth > 0.0f) joint.position = joint.position + normal * depth;
        return depth >= -0.03f;
    }
    // Point traces otherwise let feet sink until their center crosses the
    // floor. This short world-only probe keeps their collision radius above it.
    const Vector probeEnd = joint.position - Vector(0.0f, 0.0f, joint.radius + 10.0f);
    TraceWorld(joint.position, probeEnd, trace);
    if (trace.startsolid || trace.allsolid)
    {
        joint.position = RecoverFree(joint, start);
        stuck = true;
        return true;
    }
    if (trace.fraction < 1.0f && trace.plane.normal[2] > 0.5f)
    {
        normal = Vector(trace.plane.normal);
        // The vertical probe locates a plane, not a replacement center. On a
        // slope, moving to endpos + normal*radius adds a sideways step forever.
        planeOffset = Dot(Vector(trace.endpos), normal) + joint.radius + 0.1f;
        const float depth = planeOffset - Dot(joint.position, normal);
        if (depth > 0.0f) joint.position = joint.position + normal * depth;
        // Nearby planes can participate in later constraint iterations without
        // applying ground friction before the body actually reaches them.
        return depth >= -0.03f;
    }
    if (WorldFree(joint.position)) joint.lastFree = joint.position;
    return false;
}

Vector SamplePosition(const Body &body, const SegmentSample &sample)
{
    return body.joints[sample.first].position * (1.0f - sample.fraction) +
        body.joints[sample.second].position * sample.fraction;
}

void ProjectSample(Body &body, const SegmentSample &sample, const Vector &correction)
{
    const float a = 1.0f - sample.fraction, b = sample.fraction;
    const float wa = 1.0f / kMass[sample.first], wb = 1.0f / kMass[sample.second];
    const float sum = wa * a * a + wb * b * b;
    body.joints[sample.first].position = body.joints[sample.first].position + correction * (wa * a / sum);
    body.joints[sample.second].position = body.joints[sample.second].position + correction * (wb * b / sum);
}

struct SampleContact
{
    Vector start;
    Vector normal[2];
    float offset[2];
    bool active[2];
    int count;
};

void CollideSamples(Body &body, SampleContact *contacts)
{
    for (int i = 0; i < body.sampleCount; ++i)
    {
        SegmentSample &sample = body.samples[i];
        if (!sample.enabled) continue;
        SampleContact &contact = contacts[i];
        const Vector position = SamplePosition(body, sample);
        sample.volume.position = position;
        Vector normal;
        float offset;
        bool stuck;
        const bool active = Collide(sample.volume, contact.start, normal, stuck, offset);
        ProjectSample(body, sample, sample.volume.position - position);
        if (Length(normal) > 0.5f)
        {
            int slot = 0;
            if (contact.count && Dot(contact.normal[0], normal) < 0.95f) slot = 1;
            contact.normal[slot] = normal;
            contact.offset[slot] = offset;
            contact.active[slot] = active;
            if (contact.count <= slot) contact.count = slot + 1;
        }
        if (active) ++body.contacts;
    }
}

bool StationaryForSleep(Body &body, bool supported, float motion, float worstError)
{
    // Source also settles a ragdoll whose root stays in a small region even
    // if contact constraints keep an extremity oscillating. Require support,
    // bounded whole-body motion and joint error so an active fall or badly
    // distorted skeleton cannot be frozen by a stationary pelvis.
    if (!supported || motion >= 0.5f || worstError >= 2.0f)
    {
        body.stationaryFrames = 0;
        return false;
    }
    const Vector root = body.joints[PELVIS].position;
    const Vector delta = root - body.stationaryAnchor;
    if (!body.stationaryFrames || fabsf(delta.x) > 1.0f ||
        fabsf(delta.y) > 1.0f || fabsf(delta.z) > 1.0f)
    {
        body.stationaryAnchor = root;
        body.stationaryFrames = 1;
    }
    else
        ++body.stationaryFrames;
    return body.stationaryFrames >= 360;
}

void Simulate(Body &body, float dt)
{
    Vector start[JOINT_COUNT];
    Vector predictedTravel[JOINT_COUNT];
    bool stuck[JOINT_COUNT] = {};
    Vector contactNormal[JOINT_COUNT][2];
    float contactOffset[JOINT_COUNT][2] = {};
    bool contactActive[JOINT_COUNT][2] = {};
    int contactCount[JOINT_COUNT] = {};
    SampleContact samples[MAX_SEGMENT_SAMPLES] = {};
    for (int i = 0; i < body.sampleCount; ++i)
        samples[i].start = SamplePosition(body, body.samples[i]);
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        JointState &joint = body.joints[i];
        start[i] = joint.position;
        // Keep inertia in flight. Ground friction and joint projection supply
        // the damping at impact, instead of removing almost all momentum each second.
        Vector velocity = Limited(joint.position - joint.previous, 600.0f * dt) * 0.998f;
        joint.position = joint.position + velocity + Vector(0.0f, 0.0f, -800.0f * dt * dt);
        predictedTravel[i] = joint.position - start[i];
        Vector normal;
        bool trapped = false;
        float planeOffset;
        const bool collision = Collide(joint, start[i], normal, trapped, planeOffset);
        if (!trapped && Length(normal) > 0.5f)
        {
            contactNormal[i][0] = normal;
            contactOffset[i][0] = planeOffset;
            contactActive[i][0] = collision;
            contactCount[i] = 1;
        }
        if (collision)
        {
            stuck[i] = trapped;
            ++body.contacts;
        }
    }

    // Fixed samples alone miss a thin brush between two bones. Trace the
    // actual segment, then sweep its hit fraction from the previous pose;
    // that sweep finds the top of a ledge instead of separating the bones
    // onto opposite sides of it. Retain the support sample while settling.
    for (int i = 0; i < body.sampleCount; ++i)
    {
        SegmentSample &sample = body.samples[i];
        if (!sample.dynamic) continue;
        pmtrace_t trace;
        TraceWorld(body.joints[sample.first].position, body.joints[sample.second].position, trace);
        if (!trace.startsolid && !trace.allsolid && trace.fraction < 1.0f)
        {
            const float length = Length(body.joints[sample.second].position - body.joints[sample.first].position);
            sample.fraction = Clamp(trace.fraction + 0.02f / (length > 0.1f ? length : 0.1f), 0.01f, 0.99f);
            sample.enabled = true;
            samples[i].start = start[sample.first] * (1.0f - sample.fraction) + start[sample.second] * sample.fraction;
            if (WorldFree(samples[i].start)) sample.volume.lastFree = samples[i].start;
        }
    }
    CollideSamples(body, samples);
    for (int iteration = 0; iteration < 64; ++iteration)
    {
        Vector iterationStart[JOINT_COUNT];
        const bool checkConvergence = iteration >= 27 && (iteration & 3) == 3;
        if (checkConvergence)
            for (int i = 0; i < JOINT_COUNT; ++i) iterationStart[i] = body.joints[i].position;
        SolveSkeleton(body);
        // Reuse planes found this step between expensive world sweeps. This
        // lets joints and contacts converge together without stretching the
        // last chain whenever the final collision projection touches a floor.
        for (int i = 0; i < JOINT_COUNT; ++i)
            for (int contact = 0; contact < contactCount[i]; ++contact)
            {
                const Vector &normal = contactNormal[i][contact];
                const float depth = contactOffset[i][contact] - Dot(body.joints[i].position, normal);
                if (depth > 0.0f)
                {
                    if (!ContactPlaneApplies(body.joints[i].position, normal,
                        contactOffset[i][contact], body.joints[i].radius))
                    {
                        contactActive[i][contact] = false;
                        continue;
                    }
                    body.joints[i].position = body.joints[i].position + normal * depth;
                    contactActive[i][contact] = true;
                }
            }
        for (int i = 0; i < body.sampleCount; ++i)
            for (int contact = 0; contact < samples[i].count; ++contact)
            {
                const Vector &normal = samples[i].normal[contact];
                const float depth = samples[i].offset[contact] - Dot(SamplePosition(body, body.samples[i]), normal);
                if (depth > 0.0f)
                {
                    if (!ContactPlaneApplies(SamplePosition(body, body.samples[i]), normal,
                        samples[i].offset[contact], body.samples[i].volume.radius))
                    {
                        samples[i].active[contact] = false;
                        continue;
                    }
                    ProjectSample(body, body.samples[i], normal * depth);
                    samples[i].active[contact] = true;
                }
            }
        if (iteration == 11 || iteration == 23)
            for (int i = 0; i < JOINT_COUNT; ++i)
            {
                Vector normal;
                bool trapped = false;
                float planeOffset;
                if (Collide(body.joints[i], start[i], normal, trapped, planeOffset))
                {
                    stuck[i] = trapped;
                    if (!trapped && contactCount[i] < 2 &&
                        (!contactCount[i] || Dot(contactNormal[i][0], normal) < 0.95f))
                    {
                        contactNormal[i][contactCount[i]] = normal;
                        contactOffset[i][contactCount[i]] = planeOffset;
                        contactActive[i][contactCount[i]++] = true;
                    }
                    else if (!trapped && contactCount[i] && Dot(contactNormal[i][0], normal) >= 0.95f)
                        contactActive[i][0] = true;
                    ++body.contacts;
                }
            }
        if (iteration == 23) CollideSamples(body, samples);
        // Most steps converge quickly. Difficult brush corners get a bounded
        // number of extra local iterations without any additional world sweeps.
        if (checkConvergence)
        {
            float error = 0.0f;
            for (int i = 1; i < JOINT_COUNT; ++i)
            {
                const float distanceError = fabsf(Length(body.joints[i].position -
                    body.joints[kParent[i]].position) - body.rest[i]);
                if (distanceError > error) error = distanceError;
            }
            for (int cluster = 0; cluster < 2; ++cluster)
                for (int a = 0; a < 4; ++a)
                    for (int b = a + 1; b < 4; ++b)
                    {
                        const float distanceError = fabsf(Length(body.joints[kClusters[cluster][a]].position -
                            body.joints[kClusters[cluster][b]].position) - body.clusterRest[cluster][a][b]);
                        if (distanceError > error) error = distanceError;
                    }
            float motion = 0.0f;
            for (int i = 0; i < JOINT_COUNT; ++i)
            {
                const float correction = Length(body.joints[i].position - iterationStart[i]);
                if (correction > motion) motion = correction;
            }
            if (error < 0.08f && motion < 0.005f) break;
        }
    }

    Vector centerTravel(0.0f, 0.0f, 0.0f);
    Vector predictedCenterTravel(0.0f, 0.0f, 0.0f);
    float totalMass = 0.0f;
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        centerTravel = centerTravel + (body.joints[i].position - start[i]) * kMass[i];
        predictedCenterTravel = predictedCenterTravel + predictedTravel[i] * kMass[i];
        totalMass += kMass[i];
    }
    centerTravel = centerTravel * (1.0f / totalMass);
    predictedCenterTravel = predictedCenterTravel * (1.0f / totalMass);
    float motion = 0.0f;
    Vector solvedCarry[JOINT_COUNT];
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        JointState &joint = body.joints[i];
        // PBD velocity comes from the solved pose: this is how a local hit,
        // foot contact and limb inertia transmit torque through the skeleton.
        // Only contact depenetration is excluded from the outward velocity.
        Vector carry = stuck[i] ? Vector(0.0f, 0.0f, 0.0f)
            : joint.position - start[i];
        // Joint friction damps relative limb motion while preserving the
        // mass-weighted linear velocity (including gravity and running speed).
        if (!stuck[i]) carry = centerTravel + (carry - centerTravel) * 0.985f;
        for (int contact = 0; contact < contactCount[i]; ++contact)
        {
            if (!contactActive[i][contact]) continue;
            const Vector &normal = contactNormal[i][contact];
            const float normalSpeed = Dot(carry, normal);
            const float predictedNormal = Dot(predictedTravel[i], normal);
            const float outward = Clamp(normalSpeed, 0.0f,
                predictedNormal > 0.0f ? predictedNormal : 0.0f);
            Vector tangent = carry - normal * normalSpeed;
            const float tangentSpeed = Length(tangent);
            const float normalImpulse = (predictedNormal < 0.0f ? -predictedNormal : 0.0f) +
                800.0f * dt * dt * Clamp(normal.z, 0.0f, 1.0f);
            if (tangentSpeed > 0.0001f)
                tangent = tangent * Clamp(1.0f - 0.65f * normalImpulse / tangentSpeed, 0.0f, 1.0f);
            carry = tangent + normal * outward;
        }
        solvedCarry[i] = carry;
        const float displacement = Length(joint.position - start[i]);
        if (displacement > motion) motion = displacement;
        if (WorldFree(joint.position)) joint.lastFree = joint.position;
    }
    // A limb's middle can be supported while both bone endpoints lie outside
    // a narrow ledge. Apply that contact to the two endpoint velocities using
    // the same mass-weighted Jacobian as its position constraint.
    for (int i = 0; i < body.sampleCount; ++i)
    {
        const SegmentSample &sample = body.samples[i];
        const float a = 1.0f - sample.fraction, b = sample.fraction;
        const float wa = 1.0f / kMass[sample.first], wb = 1.0f / kMass[sample.second];
        const float sum = wa * a * a + wb * b * b;
        for (int contact = 0; contact < samples[i].count; ++contact)
        {
            if (!samples[i].active[contact]) continue;
            const Vector &normal = samples[i].normal[contact];
            const Vector carry = solvedCarry[sample.first] * a + solvedCarry[sample.second] * b;
            const float speed = Dot(carry, normal);
            const float predicted = Dot(predictedTravel[sample.first] * a + predictedTravel[sample.second] * b, normal);
            Vector tangent = carry - normal * speed;
            const float length = Length(tangent);
            const float impulse = (predicted < 0.0f ? -predicted : 0.0f) + 800.0f * dt * dt * Clamp(normal.z, 0.0f, 1.0f);
            if (length > 0.0001f) tangent = tangent * Clamp(1.0f - 0.65f * impulse / length, 0.0f, 1.0f);
            const Vector correction = tangent + normal * Clamp(speed, 0.0f, predicted > 0.0f ? predicted : 0.0f) - carry;
            solvedCarry[sample.first] = solvedCarry[sample.first] + correction * (wa * a / sum);
            solvedCarry[sample.second] = solvedCarry[sample.second] + correction * (wb * b / sum);
        }
    }
    // Constraints can transmit a contact's depenetration to non-contacting
    // limbs. Removing outward velocity only at the touching particle then
    // lets a trapped foot launch the entire corpse out of a corner. These
    // contacts have zero restitution: preserve any incoming outward momentum,
    // but do not turn the contact projection into new center-of-mass rebound.
    Vector solvedCenterTravel(0, 0, 0);
    for (int i = 0; i < JOINT_COUNT; ++i)
        solvedCenterTravel = solvedCenterTravel + solvedCarry[i] * (kMass[i] / totalMass);
    const Vector unclippedCenterTravel = solvedCenterTravel;
    const auto removeRebound = [&](const Vector &normal)
    {
        const float incoming = Dot(predictedCenterTravel, normal);
        const float allowed = incoming > 0.0f ? incoming : 0.0f;
        const float excess = Dot(solvedCenterTravel, normal) - allowed;
        if (excess > 0.0f) solvedCenterTravel = solvedCenterTravel - normal * excess;
    };
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < JOINT_COUNT; ++i)
            for (int contact = 0; contact < contactCount[i]; ++contact)
                if (contactActive[i][contact]) removeRebound(contactNormal[i][contact]);
        for (int i = 0; i < body.sampleCount; ++i)
            for (int contact = 0; contact < samples[i].count; ++contact)
                if (samples[i].active[contact]) removeRebound(samples[i].normal[contact]);
    }
    const Vector contactCorrection = solvedCenterTravel - unclippedCenterTravel;
    for (int i = 0; i < JOINT_COUNT; ++i)
        body.joints[i].previous = body.joints[i].position - Limited(solvedCarry[i] + contactCorrection, 600.0f * dt);
    // Contact sweeps return early, so their saved free point must follow the
    // final constrained segment too. Otherwise a resting limb that later
    // enters a brush can recover to its original airborne spawn position.
    for (int i = 0; i < body.sampleCount; ++i)
    {
        SegmentSample &sample = body.samples[i];
        if (!sample.enabled) continue;
        const Vector position = SamplePosition(body, sample);
        if (WorldFree(position)) sample.volume.lastFree = position;
    }
    float worstError = 0.0f;
    for (int i = 1; i < JOINT_COUNT; ++i)
    {
        const float error = fabsf(Length(body.joints[i].position -
            body.joints[kParent[i]].position) - body.rest[i]);
        if (error > worstError) worstError = error;
    }
    bool supported = false;
    for (int i = 0; i < JOINT_COUNT; ++i)
        for (int contact = 0; contact < contactCount[i]; ++contact)
            if (contactActive[i][contact] && contactNormal[i][contact].z > 0.5f) supported = true;
    for (int i = 0; i < body.sampleCount; ++i)
        for (int contact = 0; contact < samples[i].count; ++contact)
            if (samples[i].active[contact] && samples[i].normal[contact].z > 0.5f) supported = true;
    body.quietFrames = supported && motion < 0.06f && worstError < 0.8f
        ? body.quietFrames + 1 : 0;
    const bool stationary = StationaryForSleep(body, supported, motion, worstError);
    if (body.quietFrames > 60 ||
        stationary ||
        (supported && body.lastTime - body.birth > 12.0 && motion < 0.12f && worstError < 1.2f))
    {
        body.sleeping = true;
        for (int i = 0; i < JOINT_COUNT; ++i)
            body.joints[i].previous = body.joints[i].position;
    }
    ++body.steps;
}

void CacheBoneAttachments(Body &body)
{
    const mstudiobone_t *bones = reinterpret_cast<const mstudiobone_t *>(
        reinterpret_cast<const byte *>(body.header) + body.header->boneindex);
    for (int bone = 0; bone < body.boneCount; ++bone)
    {
        int owner = -1;
        for (int joint = 0; joint < JOINT_COUNT; ++joint)
            if (body.joints[joint].bone == bone) { owner = joint; break; }
        if (owner < 0)
        {
            const char *name = bones[bone].name;
            if (strstr(name, "Spine2") || strstr(name, "backpack")) owner = CHEST;
            else if (strstr(name, "Neck")) owner = HEAD;
            else if (strstr(name, "Clavicle")) owner = CHEST;
            else if (strstr(name, "L Knee")) owner = LEFT_KNEE;
            else if (strstr(name, "R Knee")) owner = RIGHT_KNEE;
            else if (strstr(name, "L Elbow")) owner = LEFT_ELBOW;
            else if (strstr(name, "R Elbow")) owner = RIGHT_ELBOW;
        }
        if (owner < 0)
        {
            int ancestor = bones[bone].parent;
            while (ancestor >= 0 && owner < 0)
            {
                for (int joint = 0; joint < JOINT_COUNT; ++joint)
                    if (body.joints[joint].bone == ancestor) { owner = joint; break; }
                ancestor = owner < 0 ? bones[ancestor].parent : -1;
            }
        }
        body.boneOwner[bone] = owner < 0 ? PELVIS : owner;
        const int parent = bones[bone].parent;
        if (parent >= 0 && parent < bone)
        {
            const float (*oldParent)[4] = body.baseMatrix[parent];
            const Vector offset = Origin(body.baseMatrix[bone]) - Origin(oldParent);
            for (int axis = 0; axis < 3; ++axis)
                body.boneParentOffset[bone][axis] = offset.x * oldParent[0][axis] +
                    offset.y * oldParent[1][axis] + offset.z * oldParent[2][axis];
        }
    }
}

bool InitialisePose(Body &body, studiohdr_t *header)
{
    if (!header || header->numbones <= 0 || header->numbones > MAXSTUDIOBONES) return false;
    body.header = header;
    body.boneCount = header->numbones;
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        int bone = FindBone(header, kBoneNames[i]);
        if (bone < 0 && i == HEAD) bone = FindBone(header, "Bip01 Neck");
        if (bone < 0 && i == CHEST) bone = FindBone(header, "Bip01 Spine2");
        if (bone < 0) return false;
        JointState &joint = body.joints[i];
        joint.bone = bone;
        joint.base = Origin(body.baseMatrix[bone]);
        joint.position = joint.base;
        joint.lastFree = joint.position;
        joint.radius = i == PELVIS || i == CHEST ? 7.0f : i == HEAD || i == WAIST ? 6.0f : 3.5f;
        body.rest[i] = kParent[i] >= 0
            ? Length(joint.base - body.joints[kParent[i]].base) : 0.0f;
        if (kParent[i] >= 0 && body.rest[i] < 0.5f) body.rest[i] = 0.5f;
    }
    body.baseFrame = TorsoFrame(body, true);
    body.basePelvisFrame = PelvisFrame(body, true);
    body.sampleCount = 0;
    for (int segment = 0; segment < 11; ++segment)
    {
        const int first = kSegments[segment][0], second = kSegments[segment][1];
        const float radius = body.joints[first].radius < body.joints[second].radius
            ? body.joints[first].radius : body.joints[second].radius;
        for (int part = 0; part < 2; ++part)
        {
            SegmentSample &sample = body.samples[body.sampleCount++];
            sample.first = first;
            sample.second = second;
            sample.fraction = 0.5f;
            sample.dynamic = part != 0;
            sample.enabled = part == 0;
            sample.volume.radius = radius;
            sample.volume.position = sample.volume.lastFree = SamplePosition(body, sample);
        }
    }
    for (int cluster = 0; cluster < 2; ++cluster)
        for (int first = 0; first < 4; ++first)
            for (int second = first + 1; second < 4; ++second)
                body.clusterRest[cluster][first][second] = Length(
                    body.joints[kClusters[cluster][first]].base - body.joints[kClusters[cluster][second]].base);
    for (int chain = 0; chain < 4; ++chain)
    {
        const Vector upper = Unit(body.joints[kChains[chain][1]].base -
            body.joints[kChains[chain][0]].base, body.baseFrame.up * -1.0f);
        const Vector lower = Unit(body.joints[kChains[chain][2]].base -
            body.joints[kChains[chain][1]].base, upper);
        const Frame &base = chain < 2 ? body.baseFrame : body.basePelvisFrame;
        const Vector fallback = Unit(Cross(upper, base.forward * -1.0f), base.side);
        body.hingeNormal[chain] = Length(Cross(upper, lower)) > 0.025f
            ? Unit(Cross(upper, lower), fallback) : fallback;
    }
    const Vector impact = Length(body.impact) > 0.01f ? body.impact : body.baseFrame.forward * 12.0f;
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        JointState &joint = body.joints[i];
        Vector velocity = body.hasLiveVelocity ? body.boneVelocity[joint.bone] : body.launch;
        float influence = i == PELVIS ? 0.15f : i == WAIST ? 0.3f : 0.0f;
        if (i == CHEST) influence = body.headshot ? 0.55f : 1.0f;
        if (i == HEAD) influence = body.headshot ? 1.0f : 0.45f;
        if (i == LEFT_SHOULDER || i == RIGHT_SHOULDER) influence = body.headshot ? 0.4f : 0.7f;
        velocity = Limited(velocity + impact * influence, 400.0f);
        joint.previous = joint.position - velocity * (1.0f / 90.0f);
    }
    body.initialHeadZ = body.joints[HEAD].position.z;
    // Ownership and authored parent-local offsets belong to this exact death
    // pose. Resolve them once, including helper bones; never reuse a live or
    // previous corpse's attachments after the slot/model/serial changes.
    CacheBoneAttachments(body);
    body.ready = true;
    return true;
}

bool Initialise(Body &body, CStudioModelRenderer &renderer)
{
    if (!renderer.m_pStudioHeader || renderer.m_pStudioHeader->numbones <= 0 ||
        renderer.m_pStudioHeader->numbones > MAXSTUDIOBONES || !IEngineStudio.IsHardware()) return false;
    if (!body.hasLivePose)
        memcpy(body.baseMatrix, *renderer.m_pbonetransform,
            sizeof(body.baseMatrix[0]) * renderer.m_pStudioHeader->numbones);
    return InitialisePose(body, renderer.m_pStudioHeader);
}

Vector PoseVector(const Body &body, int owner, const Vector &value, const Frame &torso)
{
    if (owner == PELVIS)
        return RotateFrame(value, body.basePelvisFrame, PelvisFrame(body, false));
    if (owner == WAIST || owner == CHEST)
        return RotateFrame(value, body.baseFrame, torso);
    for (int chain = 0; chain < 4; ++chain)
    {
        const int first = kChains[chain][0], middle = kChains[chain][1], last = kChains[chain][2];
        if (owner != first && owner != middle && owner != last) continue;
        const int a = owner == first ? first : middle;
        const int b = owner == first ? middle : last;
        const Frame &base = chain < 2 ? body.baseFrame : body.basePelvisFrame;
        const Frame socket = chain < 2 ? torso : PelvisFrame(body, false);
        Frame from, to;
        from.up = Unit(body.joints[b].base - body.joints[a].base, base.up);
        from.side = body.hingeNormal[chain];
        from.forward = Unit(Cross(from.up, from.side), base.forward);
        from.side = Cross(from.forward, from.up);
        to.up = Unit(body.joints[b].position - body.joints[a].position, socket.up);
        // Transport the hinge frame through a straight elbow/knee too. A cross
        // product of almost-collinear segments would introduce arbitrary rolls.
        to.side = HingeNormal(body, chain, socket);
        to.forward = Unit(Cross(to.up, to.side), socket.forward);
        to.side = Cross(to.forward, to.up);
        return RotateFrame(value, from, to);
    }
    const Vector oldDirection = RotateFrame(body.joints[HEAD].base - body.joints[CHEST].base,
        body.baseFrame, torso);
    const Vector newDirection = body.joints[HEAD].position - body.joints[CHEST].position;
    return RotateArc(RotateFrame(value, body.baseFrame, torso), oldDirection, newDirection);
}

void ApplyPose(Body &body, CStudioModelRenderer &renderer)
{
    const Frame torso = TorsoFrame(body, false);
    const mstudiobone_t *bones = reinterpret_cast<const mstudiobone_t *>(
        reinterpret_cast<const byte *>(body.header) + body.header->boneindex);
    for (int bone = 0; bone < body.boneCount; ++bone)
    {
        const int owner = body.boneOwner[bone];
        const JointState &joint = body.joints[owner];
        const float (*original)[4] = body.baseMatrix[bone];
        float (*render)[4] = (*renderer.m_pbonetransform)[bone];
        float (*light)[4] = (*renderer.m_plighttransform)[bone];
        for (int column = 0; column < 3; ++column)
        {
            const Vector axis(original[0][column], original[1][column], original[2][column]);
            const Vector rotated = PoseVector(body, owner, axis, torso);
            render[0][column] = light[0][column] = rotated.x;
            render[1][column] = light[1][column] = rotated.y;
            render[2][column] = light[2][column] = rotated.z;
        }
        const Vector localOffset = Origin(original) - joint.base;
        Vector position = joint.position + PoseVector(body, owner, localOffset, torso);
        // As in Source's RagdollGetBoneMatrix, keep the authored parent-local
        // attachment offset. Independent physics transforms must not pull the
        // single-weight GoldSrc mesh apart across spine/clavicle helper bones.
        const int parent = bones[bone].parent;
        if (parent >= 0 && parent < bone)
        {
            const float (*newParent)[4] = (*renderer.m_pbonetransform)[parent];
            const Vector &parentLocal = body.boneParentOffset[bone];
            for (int axis = 0; axis < 3; ++axis)
                position[axis] = newParent[axis][3] + newParent[axis][0] * parentLocal.x +
                    newParent[axis][1] * parentLocal.y + newParent[axis][2] * parentLocal.z;
        }
        render[0][3] = light[0][3] = position.x;
        render[1][3] = light[1][3] = position.y;
        render[2][3] = light[2][3] = position.z;
    }
    // Let the normal studio frustum test follow the physical torso after it
    // travels away from the death-message origin.
    body.joints[PELVIS].position.CopyToArray(renderer.m_pCurrentEntity->origin);
    VectorCopy(renderer.m_pCurrentEntity->origin, renderer.m_pCurrentEntity->curstate.origin);
}

void DumpRagdolls()
{
    const double now = gEngfuncs.GetClientTime();
    int live = 0, moving = 0, sleeping = 0;
    for (int i = 0; i < 64; ++i)
    {
        const Body &body = s_bodies[i];
        if (!body.serial || body.expires < now || !body.corpse ||
            body.corpse->entity.curstate.iuser4 != body.serial) continue;
        ++live;
        if (!body.ready) continue;
        if (body.sleeping) ++sleeping; else ++moving;
        if (live <= 4)
        {
            float worst = 0.0f;
            for (int joint = 1; joint < JOINT_COUNT; ++joint)
            {
                const float error = fabsf(Length(body.joints[joint].position -
                    body.joints[kParent[joint]].position) - body.rest[joint]);
                if (error > worst) worst = error;
            }
            const Vector &pelvis = body.joints[PELVIS].position;
            const float speed = Length(pelvis - body.joints[PELVIS].previous) * 90.0f;
            gEngfuncs.Con_Printf("ragdoll: serial=%d player=%d userid=%d death=%u age=%.2f early=%d live_pose=%d legacy_seen=%d sleep=%d steps=%u contacts=%u speed=%.2f joint_error=%.2f pelvis=%.1f/%.1f/%.1f head_z=%.1f head_drop=%.1f\n",
                body.serial, body.playerIndex, body.userid, body.deathGeneration, now - body.birth,
                int(body.early), int(body.hasLivePose), int(body.legacySeen),
                int(body.sleeping),
                body.steps, body.contacts, speed, worst,
                pelvis.x, pelvis.y, pelvis.z,
                body.joints[HEAD].position.z,
                body.initialHeadZ - body.joints[HEAD].position.z);
        }
    }
    for (int player = 1; player <= 32; ++player)
    {
        const PlayerDeath &state = s_deaths[player];
        if (state.dead && state.known)
            gEngfuncs.Con_Printf("ragdoll_owner: player=%d userid=%d death=%u claimed=%d confirms=%u\n",
                player, state.userid, state.generation, int(state.claimed), state.confirmations);
    }
    gEngfuncs.Con_Printf("ragdoll: live=%d moving=%d sleeping=%d\n", live, moving, sleeping);
}
}

void OC2_RagdollInit()
{
    if (!s_ragdoll)
        s_ragdoll = CVAR_CREATE("cl_ragdoll", "1", FCVAR_ARCHIVE);
    if (!s_dumpRegistered)
    {
        gEngfuncs.pfnAddCommand("oc2_ragdoll_dump", DumpRagdolls);
        s_dumpRegistered = true;
    }
}

void OC2_RagdollCaptureLive(CStudioModelRenderer &renderer, int playerIndex)
{
    if (playerIndex < 1 || playerIndex > 32 || !renderer.m_pCurrentEntity ||
        renderer.m_pCurrentEntity->curstate.renderfx == kRenderFxDeadPlayer ||
        g_PlayerExtraInfo[playerIndex].dead ||
        !renderer.m_pRenderModel ||
        !renderer.m_pStudioHeader || renderer.m_pStudioHeader->numbones <= 0 ||
        renderer.m_pStudioHeader->numbones > MAXSTUDIOBONES)
        return;
    if (IsDeathAnimation(renderer.m_pRenderModel, renderer.m_pCurrentEntity->curstate.sequence)) return;
    if (DeathState(playerIndex).dead) return;
    LivePose &pose = s_livePose[playerIndex];
    const double elapsed = renderer.m_clTime - pose.time;
    const bool sameSample = pose.valid && pose.model == renderer.m_pRenderModel &&
        pose.boneCount == renderer.m_pStudioHeader->numbones;
    const float originDelta = Length(Vector(renderer.m_pCurrentEntity->origin) - pose.origin);
    if (sameSample && fabs(elapsed) < 0.0001 && originDelta < 0.001f) return;
    if (sameSample && elapsed >= 0.0001 && elapsed <= 0.12 &&
        originDelta < 64.0f)
    {
        for (int bone = 0; bone < pose.boneCount; ++bone)
            pose.velocity[bone] = Limited((Origin((*renderer.m_pbonetransform)[bone]) -
                Origin(pose.matrix[bone])) * static_cast<float>(1.0 / elapsed), 400.0f);
        pose.velocityValid = true;
    }
    else
        pose.velocityValid = false;
    pose.valid = true;
    pose.model = renderer.m_pRenderModel;
    pose.time = renderer.m_clTime;
    pose.boneCount = renderer.m_pStudioHeader->numbones;
    pose.origin = Vector(renderer.m_pCurrentEntity->origin);
    pose.angles = Vector(renderer.m_pCurrentEntity->angles);
    memcpy(pose.matrix, *renderer.m_pbonetransform,
        sizeof(pose.matrix[0]) * pose.boneCount);
}

bool OC2_RagdollHideDeadPlayer(int playerIndex, int drawnSequence)
{
    if (playerIndex < 1 || playerIndex > 32) return false;
    const PlayerDeath &state = DeathState(playerIndex);
    return s_ragdoll && s_ragdoll->value > 0 && state.dead && state.claimed;
}

void OC2_RagdollSpawnDeath(int victimIndex, int killerIndex, bool headshot)
{
    OC2_RagdollInit();
    if (victimIndex < 1 || victimIndex > gEngfuncs.GetMaxClients() ||
        victimIndex > 32 || !s_ragdoll || s_ragdoll->value <= 0 ||
        !IEngineStudio.IsHardware || !IEngineStudio.IsHardware())
        return;
    PlayerDeath &state = DeathState(victimIndex);
    BeginDeath(state);
    if (state.claimed) return;
    cl_entity_t *player = gEngfuncs.GetEntityByIndex(victimIndex);
    const LivePose &pose = s_livePose[victimIndex];
    const double now = gEngfuncs.GetClientTime();
    const bool freshPose = pose.valid && pose.model &&
        now >= pose.time - 0.05 && now - pose.time < 0.5;
    model_t *model = freshPose ? pose.model : RenderedPlayerModel(victimIndex);
    if (!model && player) model = player->model;
    if (!model || !model->name[0]) return;
    const Vector origin = player ? Vector(player->origin) : pose.origin;
    const Vector angles = player ? Vector(player->angles) : pose.angles;
    // The local player is normally hidden in first person and has no cached
    // world-model bones. Use the model's own standing sequence in that case;
    // the current network sequence may already be a prone death animation.
    const studiohdr_t *header = IEngineStudio.Mod_Extradata
        ? static_cast<const studiohdr_t *>(IEngineStudio.Mod_Extradata(model)) : NULL;
    if (!SupportedSkeleton(header)) return;
    const int sequence = FindLiveIdleSequence(header);
    s_pendingEarlyVictim = victimIndex;
    s_pendingKiller = killerIndex;
    s_pendingHeadshot = headshot;
    CreateCorpse(origin, angles, model->name, static_cast<float>(now), sequence,
        player ? player->curstate.body : 0, victimIndex);
    s_pendingEarlyVictim = s_pendingKiller = 0;
    s_pendingHeadshot = false;
}

bool OC2_RagdollSuppressDelayedCorpse(int playerIndex, const Vector &origin)
{
    OC2_RagdollInit();
    if (playerIndex < 1 || playerIndex > 32 || !s_ragdoll || s_ragdoll->value <= 0)
        return false;
    PlayerDeath &state = DeathState(playerIndex);
    ++state.confirmations;
    // A confirmation may arrive after respawn, or after the original tempent
    // expired/recycled. Neither case is a new death and must not allocate again.
    if (state.known && (!state.dead || state.claimed))
    {
        for (int i = 0; i < 64; ++i)
            if (s_bodies[i].playerIndex == playerIndex &&
                s_bodies[i].userid == state.userid &&
                s_bodies[i].deathGeneration == state.generation)
                s_bodies[i].legacySeen = true;
        return true;
    }
    // Late join/demo playback may have ClCorpse and ScoreAttrib without a
    // local DeathMsg. Let that path claim the same owner exactly once.
    BeginDeath(state);
    return false;
}

void OC2_RagdollPlayerState(int playerIndex, bool dead)
{
    if (playerIndex < 1 || playerIndex > 32) return;
    PlayerDeath &state = DeathState(playerIndex);
    if (dead)
        BeginDeath(state);
    else
    {
        if (state.dead) s_livePose[playerIndex] = LivePose();
        state.known = true;
        state.dead = false;
        state.corpse = NULL;
        state.corpseTag = 0;
    }
}

bool OC2_RagdollCanCreateCorpse(int playerIndex)
{
    OC2_RagdollInit();
    if (playerIndex < 1 || playerIndex > 32 || !s_ragdoll || s_ragdoll->value <= 0)
        return true;
    const PlayerDeath &state = DeathState(playerIndex);
    return !state.known || (state.dead && !state.claimed);
}

void OC2_RagdollCorpseCreated(tempent_s *corpse, int playerIndex)
{
    if (!corpse || playerIndex < 1 || playerIndex > 32 ||
        !s_ragdoll || s_ragdoll->value <= 0) return;
    PlayerDeath &state = DeathState(playerIndex);
    BeginDeath(state);
    state.claimed = true;
    state.corpse = corpse;
    state.corpseTag = corpse->entity.curstate.iuser3;
}

void OC2_RagdollCorpseForgotten(tempent_s *corpse)
{
    if (!corpse) return;
    for (int player = 1; player <= 32; ++player)
    {
        PlayerDeath &state = s_deaths[player];
        if (state.corpse == corpse && state.corpseTag == corpse->entity.curstate.iuser3)
            state.corpse = NULL;
    }
}

bool OC2_RagdollRegister(tempent_s *corpse, int playerIndex)
{
    OC2_RagdollInit();
    if (!corpse || !corpse->entity.model || !s_ragdoll || s_ragdoll->value <= 0 ||
        !IEngineStudio.Mod_Extradata || !IEngineStudio.IsHardware ||
        !IEngineStudio.IsHardware()) return false;
    const studiohdr_t *header = static_cast<const studiohdr_t *>(
        IEngineStudio.Mod_Extradata(corpse->entity.model));
    if (!SupportedSkeleton(header)) return false;
    const double now = gEngfuncs.GetClientTime();
    int slot = -1;
    for (int i = 0; i < 64; ++i)
        if (!s_bodies[i].serial || s_bodies[i].expires < now) { slot = i; break; }
    if (slot < 0)
    {
        slot = 0;
        for (int i = 1; i < 64; ++i)
            if (s_bodies[i].birth < s_bodies[slot].birth) slot = i;
    }
    if (s_bodies[slot].serial && s_bodies[slot].expires >= now &&
        s_bodies[slot].corpse &&
        s_bodies[slot].corpse->entity.curstate.iuser4 == s_bodies[slot].serial)
    {
        OC2_CorpseForget(s_bodies[slot].corpse);
        s_bodies[slot].corpse->flags &= ~FTENT_FADEOUT;
        s_bodies[slot].corpse->die = static_cast<float>(now) - 1.0f;
    }
    Body &body = s_bodies[slot];
    body = Body();
    if (s_nextSerial >= INT_MAX - 1) s_nextSerial = 0;
    body.serial = ++s_nextSerial;
    body.playerIndex = playerIndex;
    if (playerIndex >= 1 && playerIndex <= 32)
    {
        const PlayerDeath &state = DeathState(playerIndex);
        body.userid = state.userid;
        body.deathGeneration = state.generation;
    }
    body.early = s_pendingEarlyVictim == playerIndex;
    body.headshot = body.early && s_pendingHeadshot;
    body.birth = body.lastTime = now;
    body.expires = corpse->die;
    body.corpse = corpse;
    body.model = corpse->entity.model;
    body.deathOrigin = Vector(corpse->entity.origin);
    if (body.early && playerIndex > 0 && playerIndex <= 32)
    {
        const LivePose &pose = s_livePose[playerIndex];
        if (pose.valid && pose.model &&
            !strcmp(pose.model->name, body.model->name) &&
            pose.boneCount == header->numbones &&
            now >= pose.time - 0.05 && now - pose.time < 0.5)
        {
            memcpy(body.baseMatrix, pose.matrix,
                sizeof(body.baseMatrix[0]) * pose.boneCount);
            const Vector delta = body.deathOrigin - pose.origin;
            for (int bone = 0; bone < pose.boneCount; ++bone)
            {
                body.baseMatrix[bone][0][3] += delta.x;
                body.baseMatrix[bone][1][3] += delta.y;
                body.baseMatrix[bone][2][3] += delta.z;
            }
            body.hasLivePose = true;
            // Stale bones can still be a useful starting pose, but stale
            // animation velocity must not launch a player who has since stopped.
            if (pose.velocityValid && now - pose.time <= 0.12)
            {
                memcpy(body.boneVelocity, pose.velocity, sizeof(pose.velocity));
                body.hasLiveVelocity = true;
            }
        }
    }
    if (playerIndex > 0 && playerIndex <= gEngfuncs.GetMaxClients())
    {
        cl_entity_t *player = gEngfuncs.GetEntityByIndex(playerIndex);
        if (player) body.launch = Vector(player->curstate.velocity);
    }
    if (body.early && s_pendingKiller > 0 &&
        s_pendingKiller <= gEngfuncs.GetMaxClients() &&
        s_pendingKiller != playerIndex)
    {
        cl_entity_t *killer = gEngfuncs.GetEntityByIndex(s_pendingKiller);
        if (killer)
        {
            Vector away = body.deathOrigin - Vector(killer->origin);
            away.z = 0.0f;
            const float distance = Length(away);
            if (distance > 1.0f)
                body.impact = away * ((body.headshot ? 125.0f : 100.0f) / distance);
        }
    }
    body.launch = Limited(body.launch, 400.0f);
    corpse->entity.curstate.iuser4 = body.serial;
    // A captured live pose is sufficient to begin physics immediately, even
    // if the corpse is outside the frustum when its DeathMsg arrives.
    if (body.hasLivePose)
        InitialisePose(body, const_cast<studiohdr_t *>(header));
    return true;
}

static void Advance(Body &body, double now)
{
    // Temp-entity time is float while studio render time is double. Ignore
    // their sub-frame rounding difference; only a real seek resets physics.
    if (now < body.lastTime - 0.01)
    {
        body.sleeping = false;
        body.quietFrames = 0;
        body.stationaryFrames = 0;
        body.accumulator = 0;
        body.lastTime = now;
    }
    if (now <= body.lastTime) return;
    if (!body.sleeping && now > body.lastTime)
    {
        const double gap = now - body.lastTime;
        if (gap < 0.25)
            body.accumulator += Clamp(static_cast<float>(gap), 0.0f, 0.10f);
        const float stepTime = 1.0f / 90.0f;
        for (int step = 0; step < 9 && body.accumulator >= stepTime &&
            !body.sleeping; ++step)
        {
            Simulate(body, stepTime);
            body.accumulator -= stepTime;
        }
    }
    body.lastTime = now;
}

void OC2_RagdollTick(double clientTime)
{
    double now = clientTime;
    if (IEngineStudio.GetTimes) IEngineStudio.GetTimes(NULL, &now, NULL);
    for (int i = 0; i < 64; ++i)
    {
        Body &body = s_bodies[i];
        if (!body.serial || !body.ready || body.expires < now ||
            !body.corpse || body.corpse->entity.curstate.iuser4 != body.serial)
            continue;
        for (int step = 0; step < 10 && body.catchupSteps > 0 &&
            !body.sleeping; ++step, --body.catchupSteps)
            Simulate(body, 1.0f / 90.0f);
        Advance(body, now);
    }
}

void OC2_RagdollApply(CStudioModelRenderer &renderer)
{
    cl_entity_t *entity = renderer.m_pCurrentEntity;
    if (!entity || !entity->curstate.iuser4) return;
    Body *body = NULL;
    for (int i = 0; i < 64; ++i)
        if (s_bodies[i].serial == entity->curstate.iuser4) { body = &s_bodies[i]; break; }
    if (!body || body->model != entity->model) return;
    if (!body->ready)
    {
        if (!Initialise(*body, renderer)) return;
        const double age = renderer.m_clTime - body->birth;
        body->catchupSteps = age > 0.0 ? static_cast<int>(Clamp(
            static_cast<float>(age * 90.0), 0.0f, 90.0f)) : 0;
        body->lastTime = renderer.m_clTime;
    }
    if (body->header != renderer.m_pStudioHeader) return;
    ApplyPose(*body, renderer);
}

void OC2_RagdollClear()
{
    for (int i = 0; i < 64; ++i) s_bodies[i] = Body();
    for (int i = 0; i < 33; ++i) s_livePose[i] = LivePose();
    for (int i = 0; i < 33; ++i) s_deaths[i] = PlayerDeath();
    s_pendingEarlyVictim = s_pendingKiller = 0;
    s_pendingHeadshot = false;
}
