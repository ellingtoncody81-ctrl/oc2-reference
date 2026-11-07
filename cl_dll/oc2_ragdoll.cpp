#include "hud.h"
#include "oc2_cold_perf.h"
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
#include "oc2_ivp_adapter.h"
#include "oc2_bullet_ragdoll.h"

#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

extern engine_studio_api_t IEngineStudio;
#include "oc2_player_snapshot.h"

// The frame rate of the death animation the corpse plays. The temp entity
// needs it because its own default of one frame per second would freeze the
// corpse in the pose of the first frame.
float g_oc2CorpseSequenceFramerate = 30.0f;

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
// Per-second damping keeps settling independent of fixed substeps.
const float kLinearDamping = 0.18f;
const float kJointDamping = 0.60f;
const float kAngularDamping = 0.75f;
// Render-side swing allowances. The physics keeps its authored cones and
// hinges; these only bound how far a drawn limb may leave the arc it had in
// the death pose, so a flailing arm cannot fly out to the side.
const float kShoulderSwing = 1.30f;
const float kHipFlexion = 1.40f;
const float kHipAbduction = 0.35f;
const float kKneeFlexion = 2.40f;
const int MAX_SEGMENT_SAMPLES = 22;

struct Frame
{
    Vector side, forward, up;
};

struct JointState
{
    int index;
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

struct Quat
{
    float x, y, z, w;
};

const int kPartCount = 12;
const int kPartPivot[kPartCount] = {
    PELVIS, WAIST, CHEST, HEAD, LEFT_SHOULDER, LEFT_ELBOW,
    RIGHT_SHOULDER, RIGHT_ELBOW, LEFT_HIP, LEFT_KNEE,
    RIGHT_HIP, RIGHT_KNEE
};
const int kPartTip[kPartCount] = {
    LEFT_HIP, CHEST, LEFT_SHOULDER, -1, LEFT_ELBOW, LEFT_HAND,
    RIGHT_ELBOW, RIGHT_HAND, LEFT_KNEE, LEFT_FOOT,
    RIGHT_KNEE, RIGHT_FOOT
};
const int kPartParent[kPartCount] = {
    -1, 0, 1, 2, 2, 4, 2, 6, 0, 8, 0, 10
};
const int kPartHinge[kPartCount] = {
    -1, -1, -1, -1, -1, 0, -1, 1, -1, 2, -1, 3
};
const float kPartConeLimit[kPartCount] = {
    0.0f, 0.85f, 0.95f, 0.85f, 2.05f, 0.0f,
    2.05f, 0.0f, 1.65f, 0.0f, 1.65f, 0.0f
};
const int kJointPart[JOINT_COUNT] = {
    0, 1, 2, 3, 4, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 11
};

struct AngularPart
{
    int pivot, tip, parent;
    Quat orientation, baseOrientation, restRelative;
    Vector angularVelocity, inertia, tipLocal;
    float length, radius, coneLimit, hingeLimit;
    Vector hingeLocal;
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
    // Where on the body the shot landed, from the feet (0) to the head (1).
    // It is what decides which part of the body the shot pushes and with it
    // how the body falls.
    float hitHeight = 0.72f;
    tempent_s *corpse;
    model_t *model;
    studiohdr_t *header;
    int boneCount;
    JointState joints[JOINT_COUNT];
    AngularPart parts[kPartCount];
    int sampleCount;
    SegmentSample samples[MAX_SEGMENT_SAMPLES];
    float rest[JOINT_COUNT];
    float clusterRest[2][4][4];
    Frame baseFrame;
    Frame basePelvisFrame;
    // Clamped render directions per chain: [chain][0] pivot->middle and
    // [chain][1] middle->tip. Rebuilt on every render, never persisted.
    Vector renderDir[4][2];
    Vector hingeNormal[4];
    IvpCorpse *ivp;
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
    // Who killed this player and where the shot landed. The corpse is
    // registered a second or more after the death message, because the death
    // animation has to play first, so this cannot live in a temporary that is
    // cleared right after the corpse was created.
    int killer;
    bool headshot;
    // Where the shooter stood when the shot was fired. The shooter may have
    // moved on by the time the corpse is handed to the physics.
    float killerOrigin[3];
    bool killerOriginValid;
    // How much higher the shooter stood than the victim. It decides where on
    // the body the shot landed.
    float shooterHeight;
};

Body s_bodies[64] = {};
LivePose s_livePose[33] = {};
PlayerDeath s_deaths[33] = {};
struct PendingDeath { int killer=0,userid=0,message=0;unsigned generation=0;double queued=0;bool active=false,headshot=false; };
PendingDeath s_pendingDeaths[33];
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
    state.killer = 0;
    state.headshot = false;
    state.killerOriginValid = false;
    state.shooterHeight = 0.0f;
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
    // The flinches are two frame hits, not falls. "head_flinch" would
    // otherwise pass for the "head" fall.
    if (strstr(label, "_flinch"))
        return false;
    return !strncmp(label, "death", 5) ||
        !strncmp(label, "crouch_die", 10) ||
        !strncmp(label, "head", 5) ||
        !strncmp(label, "gutshot", 8) ||
        !strncmp(label, "left", 5) || !strncmp(label, "back", 5) ||
        !strncmp(label, "right", 6) || !strncmp(label, "forward", 8);
}

// The position of a player entity is only usable when it means something.
// Xash keeps the slot of a player after it left the PVS, so the entity can
// still hold an old position. A position that is out of any plausible range
// would push the body the wrong way, so it is refused. A position that is in
// range but a little old is still much better than no direction at all.
bool UsableShooterOrigin(const cl_entity_t *shooter, const Vector &victim, Vector &out)
{
	if (!shooter) return false;
	const Vector position(shooter->origin);
	const float distance = Length(position - victim);
	if (distance < 1.0f || distance > 16384.0f)
		return false;
	out = position;
	return true;
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

Quat QuatIdentity()
{
    Quat q = { 0.0f, 0.0f, 0.0f, 1.0f };
    return q;
}

Quat QuatNormalize(const Quat &q)
{
    const float length = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (!(length > 0.0001f)) return QuatIdentity();
    const float s = 1.0f / length;
    Quat r = { q.x*s, q.y*s, q.z*s, q.w*s };
    return r;
}

Quat QuatMultiply(const Quat &a, const Quat &b)
{
    Quat r = {
        a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z };
    return r;
}

Quat QuatConjugate(const Quat &q)
{
    Quat r = { -q.x, -q.y, -q.z, q.w };
    return r;
}

Vector QuatRotate(const Quat &q, const Vector &v)
{
    const Vector u(q.x, q.y, q.z);
    const float s = q.w;
    return u*(2.0f*Dot(u, v)) + v*(s*s - Dot(u, u)) +
        Cross(u, v)*(2.0f*s);
}

Quat QuatFromAxisAngle(const Vector &axis, float angle)
{
    const Vector n = Unit(axis, Vector(0.0f, 0.0f, 1.0f));
    const float h = angle * 0.5f;
    const float s = sinf(h);
    Quat r = { n.x*s, n.y*s, n.z*s, cosf(h) };
    return r;
}

Quat QuatFromBasis(const Vector &x, const Vector &y, const Vector &z)
{
    const float trace = x.x + y.y + z.z;
    Quat q;
    if (trace > 0.0f)
    {
        const float s = sqrtf(trace + 1.0f) * 2.0f;
        q.w = 0.25f*s; q.x = (y.z - z.y)/s;
        q.y = (z.x - x.z)/s; q.z = (x.y - y.x)/s;
    }
    else if (x.x > y.y && x.x > z.z)
    {
        const float s = sqrtf(1.0f + x.x - y.y - z.z) * 2.0f;
        q.w = (y.z - z.y)/s; q.x = 0.25f*s;
        q.y = (x.y + y.x)/s; q.z = (z.x + x.z)/s;
    }
    else if (y.y > z.z)
    {
        const float s = sqrtf(1.0f + y.y - x.x - z.z) * 2.0f;
        q.w = (z.x - x.z)/s; q.x = (x.y + y.x)/s;
        q.y = 0.25f*s; q.z = (y.z + z.y)/s;
    }
    else
    {
        const float s = sqrtf(1.0f + z.z - x.x - y.y) * 2.0f;
        q.w = (x.y - y.x)/s; q.x = (z.x + x.z)/s;
        q.y = (y.z + z.y)/s; q.z = 0.25f*s;
    }
    return QuatNormalize(q);
}

Vector QuatLogVector(const Quat &input)
{
    Quat q = QuatNormalize(input);
    if (q.w < 0.0f) { q.x=-q.x; q.y=-q.y; q.z=-q.z; q.w=-q.w; }
    const float s = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z);
    if (s < 0.00001f) return Vector(2.0f*q.x, 2.0f*q.y, 2.0f*q.z);
    const float angle = 2.0f*atan2f(s, q.w);
    return Vector(q.x, q.y, q.z) * (angle / s);
}

Quat QuatAlignDirection(const Quat &q, const Vector &from, const Vector &to)
{
    const Vector a = Unit(from, Vector(0.0f, 0.0f, 1.0f));
    const Vector b = Unit(to, a);
    const Vector cross = Cross(a, b);
    const float sine = Length(cross);
    const float cosine = Dot(a, b);
    if (sine < 0.0001f)
    {
        if (cosine > 0.0f) return q;
        const Vector axis = Unit(Cross(a, Vector(0.0f, 0.0f, 1.0f)),
            Vector(0.0f, 1.0f, 0.0f));
        return QuatMultiply(QuatFromAxisAngle(axis, 3.14159265f), q);
    }
    const Vector axis = cross * (1.0f / sine);
    return QuatMultiply(QuatFromAxisAngle(axis, atan2f(sine, cosine)), q);
}

Vector AngularInvInertia(const AngularPart &part, const Vector &torque)
{
    const Vector local = QuatRotate(QuatConjugate(part.orientation), torque);
    const Vector result(local.x/fmaxf(part.inertia.x, 0.02f),
        local.y/fmaxf(part.inertia.y, 0.02f),
        local.z/fmaxf(part.inertia.z, 0.02f));
    return QuatRotate(part.orientation, result);
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
    const float angle = Clamp(signedAngle, 0.015f, limit);
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

void ApplyContactTorque(Body &b, int j, const Vector &d);
void ApplySampleTorque(Body &b, int a, int c, float f, const Vector &d);

bool Collide(Body &body, JointState &joint, const Vector &start, Vector &normal, bool &stuck, float &planeOffset)
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
        if (depth > 0.0f)
        {
            const Vector correction = normal * depth;
            joint.position = joint.position + correction;
            ApplyContactTorque(body, joint.index, correction);
        }
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
        if (depth > 0.0f)
        {
            const Vector correction = normal * depth;
            joint.position = joint.position + correction;
            ApplyContactTorque(body, joint.index, correction);
        }
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
        const bool active = Collide(body, sample.volume, contact.start, normal, stuck, offset);
        const Vector correction = sample.volume.position - position;
        ProjectSample(body, sample, correction);
        ApplySampleTorque(body, sample.first, sample.second, sample.fraction, correction);
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

Quat BoneQuat(const Body &body, int bone)
{
    const float (*m)[4] = body.baseMatrix[bone];
    Vector x = Unit(Vector(m[0][0], m[1][0], m[2][0]), Vector(1,0,0));
    Vector y = Unit(Vector(m[0][1], m[1][1], m[2][1]), Vector(0,1,0));
    Vector z = Unit(Cross(x, y), Vector(0,0,1));
    y = Unit(Cross(z, x), y);
    x = Unit(Cross(y, z), x);
    return QuatFromBasis(x, y, z);
}

float PartMass(const Body &body, int part)
{
    const int pivot = kPartPivot[part];
    const int tip = kPartTip[part];
    return kMass[pivot] + (tip >= 0 ? 0.5f*kMass[tip] : 0.5f*kMass[pivot]);
}

Vector PartCenter(const Body &body, int part)
{
    const int pivot = kPartPivot[part];
    const int tip = kPartTip[part];
    if (tip >= 0)
        return (body.joints[pivot].position + body.joints[tip].position) * 0.5f;
    return body.joints[pivot].position +
        QuatRotate(body.parts[part].orientation, body.parts[part].tipLocal) * 0.5f;
}

void SyncAngularPart(Body &body, int partIndex)
{
    AngularPart &part = body.parts[partIndex];
    if (part.tip < 0) return;
    const Vector expected = QuatRotate(part.orientation, part.tipLocal);
    const Vector current = body.joints[part.tip].position - body.joints[part.pivot].position;
    part.orientation = QuatNormalize(QuatAlignDirection(part.orientation, expected, current));
}

void SyncAngularParts(Body &body)
{
    for (int part = 0; part < kPartCount; ++part)
        SyncAngularPart(body, part);
}

void IntegrateAngular(Body &body, float dt)
{
    const float damping = expf(-kAngularDamping * dt);
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        Vector w = Limited(p.angularVelocity, 30.0f);
        const float speed = Length(w);
        if (speed > 0.0001f)
        {
            const Quat dq = QuatFromAxisAngle(w * (1.0f/speed), speed*dt);
            p.orientation = QuatNormalize(QuatMultiply(dq, p.orientation));
        }
        p.angularVelocity = w * damping;
    }
}

void SolveOrientedSegments(Body &body)
{
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        if (p.tip < 0 || p.length < 0.01f) continue;
        const Vector dir = Unit(QuatRotate(p.orientation, p.tipLocal), Vector(0,0,1));
        const Vector desired = body.joints[p.pivot].position + dir * p.length;
        const Vector correction =
            (desired - body.joints[p.tip].position) * 0.28f;
        const float wp = 1.0f/kMass[p.pivot];
        const float wt = 1.0f/kMass[p.tip];
        const float sum = wp + wt;
        body.joints[p.tip].position = body.joints[p.tip].position + correction*(wt/sum);
        body.joints[p.pivot].position = body.joints[p.pivot].position - correction*(wp/sum);
    }
}

void EnforceAngularLimits(Body &body, float relax)
{
    for (int part = 1; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        const int parent = kPartParent[part];
        if (parent < 0 || parent >= kPartCount) continue;
        AngularPart &pa = body.parts[parent];
        if (kPartHinge[part] >= 0)
        {
            const Vector axis = Unit(QuatRotate(pa.orientation, p.hingeLocal),
                Vector(0,0,1));
            const Vector upper = Unit(QuatRotate(pa.orientation, pa.tipLocal), axis);
            const Vector lower = Unit(QuatRotate(p.orientation, p.tipLocal), upper);
            const Vector bend = Cross(axis, upper);
            const float signedAngle = atan2f(Dot(lower,bend),Dot(lower,upper));
            const float limit = p.hingeLimit*(0.82f + 0.18f*relax);
            const float target = Clamp(signedAngle, 0.015f, limit);
            if (fabsf(target - signedAngle) > 0.0005f)
            {
                const Quat turn = QuatFromAxisAngle(axis, (target-signedAngle)*0.85f);
                p.orientation = QuatNormalize(QuatMultiply(turn, p.orientation));
            }
            const Vector relW = p.angularVelocity - pa.angularVelocity;
            p.angularVelocity = p.angularVelocity -
                (relW - axis*Dot(relW, axis))*0.35f;
        }
        else if (p.coneLimit > 0.001f)
        {
            const Quat qrel = QuatMultiply(QuatConjugate(pa.orientation), p.orientation);
            const Quat delta = QuatMultiply(QuatConjugate(p.restRelative), qrel);
            const Vector v = QuatLogVector(delta);
            const float angle = Length(v);
            const float limit = p.coneLimit*(0.72f + 0.28f*relax);
            if (angle > limit && angle > 0.0001f)
            {
                const Vector axis = v*(1.0f/angle);
                const Quat limited = QuatMultiply(
                    QuatFromAxisAngle(axis, limit), p.restRelative);
                p.orientation = QuatNormalize(QuatMultiply(pa.orientation, limited));
                const Vector relW = p.angularVelocity - pa.angularVelocity;
                const float outward = Dot(relW, axis);
                if (outward > 0.0f)
                    p.angularVelocity = p.angularVelocity - axis*outward*0.35f;
            }
        }
    }
}

void ApplyContactTorque(Body &body, int joint, const Vector &correction)
{
    if (!body.ready || Length(correction) < 0.0001f) return;
    if (joint < 0 || joint >= JOINT_COUNT) return;
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        const Vector center = PartCenter(body, part);
        const Vector lever = body.joints[joint].position - center;
        if (joint != p.pivot && joint != p.tip) continue;
        const Vector dw = AngularInvInertia(p, Cross(lever, correction)) * 14.0f;
        p.angularVelocity = p.angularVelocity + Limited(dw, 0.75f);
    }
}

void ApplySampleTorque(Body &body, int first, int second, float fraction,
    const Vector &correction)
{
    if (!body.ready || Length(correction) < 0.0001f) return;
    const Vector contact = body.joints[first].position*(1.0f-fraction) +
        body.joints[second].position*fraction;
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        if (p.pivot != first || p.tip != second) continue;
        const Vector lever = contact - PartCenter(body, part);
        const Vector dw = AngularInvInertia(p, Cross(lever, correction)) * 14.0f;
        p.angularVelocity = p.angularVelocity + Limited(dw, 0.75f);
    }
}

void ReconstructAngularVelocity(Body &body, const Vector *carry, float dt)
{
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        if (p.tip >= 0)
        {
            const Vector r = body.joints[p.tip].position - body.joints[p.pivot].position;
            const Vector vrel = (carry[p.tip] - carry[p.pivot]) * (1.0f/dt);
            Vector w = Cross(r, vrel) * (1.0f/fmaxf(Dot(r, r), 0.01f));
            w = Limited(w, 25.0f);
            p.angularVelocity = p.angularVelocity*0.25f + w*0.75f;
        }
        p.angularVelocity = Limited(p.angularVelocity, 25.0f) *
            expf(-kAngularDamping*dt*0.5f);
    }
}

void InitialiseAngularParts(Body &body)
{
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        p.pivot = kPartPivot[part];
        p.tip = kPartTip[part];
        p.parent = kPartParent[part];
        p.baseOrientation = BoneQuat(body, body.joints[p.pivot].bone);
        p.orientation = p.baseOrientation;
        p.angularVelocity = Vector(0,0,0);
        p.coneLimit = kPartConeLimit[part];
        p.hingeLimit = kPartHinge[part] < 0 ? 0.0f :
            (kPartHinge[part] < 2 ? 2.60f : 2.65f);
        const Vector basePivot = body.joints[p.pivot].base;
        if (p.tip >= 0)
            p.tipLocal = QuatRotate(QuatConjugate(p.baseOrientation),
                body.joints[p.tip].base - basePivot);
        else
            p.tipLocal = QuatRotate(QuatConjugate(p.baseOrientation),
                body.baseFrame.up * body.joints[p.pivot].radius * 1.35f);
        p.length = Length(p.tipLocal);
        if (p.length < 0.1f) p.length = 1.0f;
        const float endRadius = p.tip >= 0 ?
            fminf(body.joints[p.pivot].radius, body.joints[p.tip].radius) :
            body.joints[p.pivot].radius;
        p.radius = Clamp(endRadius*0.55f, 1.2f, 4.5f);
        const float mass = PartMass(body, part);
        const float r = fmaxf(p.radius, 1.0f);
        const float transverse = mass*(p.length*p.length + 3.0f*r*r)/12.0f;
        const float axial = 0.5f*mass*r*r;
        const Vector localUnit = Unit(p.tipLocal, Vector(0,0,1));
        p.inertia = Vector(
            axial + (transverse-axial)*localUnit.x*localUnit.x,
            axial + (transverse-axial)*localUnit.y*localUnit.y,
            axial + (transverse-axial)*localUnit.z*localUnit.z);
    }
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        const int parent = kPartParent[part];
        if (parent >= 0)
            p.restRelative = QuatMultiply(QuatConjugate(body.parts[parent].baseOrientation),
                p.baseOrientation);
        const int hinge = kPartHinge[part];
        if (hinge >= 0 && parent >= 0)
            p.hingeLocal = QuatRotate(
                QuatConjugate(body.parts[parent].baseOrientation), body.hingeNormal[hinge]);
    }
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        const int parent = kPartParent[part];
        p.angularVelocity = parent >= 0 ? body.parts[parent].angularVelocity :
            Vector(0,0,0);
        if (body.hasLiveVelocity && p.tip >= 0)
        {
            const Vector r = QuatRotate(p.baseOrientation, p.tipLocal);
            const Vector vrel = body.boneVelocity[body.joints[p.tip].bone] -
                body.boneVelocity[body.joints[p.pivot].bone];
            const Vector w = Cross(r, vrel) * (1.0f/fmaxf(Dot(r,r),0.01f));
            p.angularVelocity = p.angularVelocity*0.25f + Limited(w,25.0f)*0.75f;
        }
    }
}

void ApplyDeathImpulse(Body &body)
{
    if (Length(body.impact) < 0.01f) return;
    float totalMass = 0.0f;
    for (int i = 0; i < JOINT_COUNT; ++i) totalMass += kMass[i];
    const int owner = body.headshot ? HEAD : CHEST;
    const Vector impulse = Limited(body.impact * 8.0f, 1400.0f);
    const float sideSign = Dot(body.impact, body.baseFrame.side) < 0.0f ? -1.0f : 1.0f;
    const Vector contact = body.joints[owner].position +
        body.baseFrame.forward * body.joints[owner].radius * 0.24f +
        body.baseFrame.side * sideSign * body.joints[owner].radius * 0.30f;
    for (int part = 0; part < kPartCount; ++part)
    {
        AngularPart &p = body.parts[part];
        const Vector share = impulse * (PartMass(body, part)/totalMass);
        const Vector lever = contact - PartCenter(body, part);
        p.angularVelocity = p.angularVelocity +
            Limited(AngularInvInertia(p, Cross(lever, share)), 8.0f);
    }
    const float twist = (body.headshot ? 2.4f : 1.1f) * sideSign;
    body.parts[1].angularVelocity = body.parts[1].angularVelocity + body.baseFrame.up*twist*0.55f;
    body.parts[2].angularVelocity = body.parts[2].angularVelocity + body.baseFrame.up*twist;
    if (body.headshot)
        body.parts[3].angularVelocity = body.parts[3].angularVelocity +
            body.baseFrame.side*1.2f + body.baseFrame.up*(sideSign*1.7f);
}

void SolveSelfCollisions(Body &body)
{
    struct Proxy { int first, second; float fraction, radius; };
    const Proxy proxies[13] = {
        {HEAD, HEAD, 0.0f, 2.8f},
        {LEFT_SHOULDER, RIGHT_SHOULDER, 0.5f, 3.2f},
        {LEFT_HIP, RIGHT_HIP, 0.5f, 3.8f},
        {LEFT_ELBOW, LEFT_HAND, 0.5f, 2.2f},
        {RIGHT_ELBOW, RIGHT_HAND, 0.5f, 2.2f},
        {LEFT_HAND, LEFT_HAND, 0.0f, 1.6f},
        {RIGHT_HAND, RIGHT_HAND, 0.0f, 1.6f},
        {LEFT_HIP, LEFT_KNEE, 0.5f, 2.6f},
        {RIGHT_HIP, RIGHT_KNEE, 0.5f, 2.6f},
        {LEFT_KNEE, LEFT_FOOT, 0.5f, 2.2f},
        {RIGHT_KNEE, RIGHT_FOOT, 0.5f, 2.2f},
        {LEFT_FOOT, LEFT_FOOT, 0.0f, 1.5f},
        {RIGHT_FOOT, RIGHT_FOOT, 0.0f, 1.5f}
    };
    const int pairs[14][2] = {
        {0,1}, {3,1}, {4,1}, {5,1}, {6,1}, {5,2}, {6,2},
        {5,6}, {3,4}, {3,6}, {4,5}, {7,8}, {9,10}, {11,12}
    };
    for (int pair = 0; pair < 14; ++pair)
    {
        const Proxy &pa = proxies[pairs[pair][0]];
        const Proxy &pb = proxies[pairs[pair][1]];
        const Vector a = body.joints[pa.first].position*(1.0f-pa.fraction) +
            body.joints[pa.second].position*pa.fraction;
        const Vector b = body.joints[pb.first].position*(1.0f-pb.fraction) +
            body.joints[pb.second].position*pb.fraction;
        const Vector delta = b - a;
        const float distance = Length(delta);
        const float minimum = pa.radius + pb.radius;
        if (distance < minimum - 0.20f && distance > 0.001f)
        {
            const Vector correction = delta *
                ((minimum - 0.20f - distance)/(distance*2.0f));
            const auto apply = [&](const Proxy &px, const Vector &d)
            {
                const float wa = 1.0f/kMass[px.first];
                const float wb = 1.0f/kMass[px.second];
                const float sum = wa*(1.0f-px.fraction)*(1.0f-px.fraction) +
                    wb*px.fraction*px.fraction;
                body.joints[px.first].position = body.joints[px.first].position +
                    d*(wa*(1.0f-px.fraction)/sum);
                body.joints[px.second].position = body.joints[px.second].position +
                    d*(wb*px.fraction/sum);
            };
            apply(pa, correction * -1.0f);
            apply(pb, correction);
            ApplySampleTorque(body, pa.first, pa.second, pa.fraction, correction * -1.0f);
            ApplySampleTorque(body, pb.first, pb.second, pb.fraction, correction);
        }
    }
}

// ---------------------------------------------------------------- IVP backend
// The optional oc2_ivp.dll carries the CSSO/Source 1 style articulated
// ragdoll: IVP/Havok balls joined by three axis limited ball sockets. When
// the DLL is absent every entry point falls back to the PBD solver.
//
// The DLL works in the model space of the corpse (the same inches, Z up
// space as the studio bind pose), so the client only adds the death origin.

// The DLL body order, with the joint index of its pivot and its tip:
//   0 pelvis->waist   1 waist->chest   2 chest->head    3 head
//   4 L_shoulder->L_elbow  5 L_elbow->L_hand
//   6 R_shoulder->R_elbow  7 R_elbow->R_hand
//   8 L_hip->L_knee   9 L_knee->L_foot  10 R_hip->R_knee  11 R_knee->R_foot
static const int kIvpBodyPivot[12] = {0, 1, 2, 3, 4, 5, 7, 8, 10, 11, 13, 14};
static const int kIvpBodyTip[12] = {1, 2, 3, -1, 5, 6, 8, 9, 11, 12, 14, 15};

// The floor under the corpse, in the model space of the corpse.
static float IvpGroundHeight(const Vector &origin)
{
    const Vector start = origin + Vector(0.0f, 0.0f, 16.0f);
    const Vector end = origin - Vector(0.0f, 0.0f, 4096.0f);
    pmtrace_t trace;
    TraceWorld(start, end, trace);
    if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f) return -4096.0f;
    return start.z + (end.z - start.z) * trace.fraction;
}

/* cl_ragdoll is a three state switch: 0 off, 1 the PBD solver, 2 Bullet. */
static bool BulletMode()
{
    return s_ragdoll && s_ragdoll->value >= 2.0f;
}

// The height of the hit along the body: zero at the feet, one at the head,
// and half at the pelvis. A shot from above lands on the shoulder and tips
// the body backwards, a level shot lands on the chest and a shot from below
// lands on the legs and throws the body forward. On level ground the shooter
// stands at the same height as the victim, so this mostly keeps the head
// shot apart from the chest shot; the side the shot came from is what tells
// two corpses apart.
float HitHeightFor(float shooterHeight, bool headshot)
{
    if (headshot) return 0.97f;
    return Clamp(0.72f + shooterHeight * 0.008f, 0.16f, 0.93f);
}

// The point the fatal hit landed on. It gives the fall a lever arm, so a head
// shot and a leg shot topple the body differently.
static void DeathContactPoint(const Body &body, float out[3])
{
    // The hit lands somewhere on the body axis, from the legs up to the
    // shoulder, so the push is applied where the body was actually hit.
    const float height = Clamp(body.hitHeight, 0.0f, 1.0f);
    const Vector foot = body.joints[LEFT_FOOT].position;
    const Vector pelvis = body.joints[PELVIS].position;
    const Vector head = body.joints[HEAD].position;
    Vector contact;
    if (height < 0.5f)
        contact = foot + (pelvis - foot) * (height / 0.5f);
    else
        contact = pelvis + (head - pelvis) * ((height - 0.5f) / 0.5f);
    // The shot came from the side the shooter stands on, so the contact point
    // is on that side of the body and not inside it. The offset follows the
    // shooter around the body, which is what tells a shot from the front
    // apart from a shot from the side.
    Vector approach = body.impact;
    approach.z = 0.0f;
    const float length = Length(approach);
    if (length > 0.01f)
    {
        // The radius of the body where the hit landed. The legs are much
        // thinner than the torso, so the chest radius would push the contact
        // point off the surface of the leg. The two are blended, otherwise
        // the offset steps by an inch at the hip.
        const float legRadius = body.joints[LEFT_KNEE].radius;
        const float torsoRadius = body.joints[CHEST].radius;
        const float blend = Clamp((0.5f - height) * 2.0f, 0.0f, 1.0f);
        const float radius = torsoRadius + (legRadius - torsoRadius) * blend;
        contact = contact - approach * (radius * 0.35f / length);
    }
    out[0] = contact.x;
    out[1] = contact.y;
    out[2] = contact.z;
}

static void IvpCreateCorpse(Body &body)
{
    if (BulletMode() && body.model && body.header && body.boneCount > 0)
    {
        // The Bullet backend owns its own world; the IVP path stays untouched.
        float contact[3] = { 0.0f, 0.0f, 0.0f };
        DeathContactPoint(body, contact);
        const float impact[3] = { body.impact.x, body.impact.y, body.impact.z };
        const float launch[3] = { body.launch.x, body.launch.y, body.launch.z };
        if (gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("ragdoll_bullet: corpse %u takeover pose head %.1f pelvis %.1f, hit on %s\n",
                body.serial, body.joints[HEAD].position.z, body.joints[PELVIS].position.z,
                body.headshot ? "head" : "chest");
        oc2_bullet::Init();
        oc2_bullet::CreateRagdoll(body.serial, body.model->name, body.header,
            body.baseMatrix, body.boneCount, impact, launch, contact, body.headshot);
    }
    // One time console line, so the tester can prove that the DLL loaded and
    // see whether the pose extension is present. The offline harness stubs the
    // console with null pointers, so the call is guarded.
    static bool reported = false;
    if (!reported)
    {
        reported = true;
        if (gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("ragdoll_ivp: %s\n", IvpAdapterStatus());
    }
    if (!IvpAdapterAvailable() || body.ivp) return;
    const float ground = IvpGroundHeight(body.deathOrigin) - body.deathOrigin.z;
    body.ivp = IvpCorpseCreate(ground);
    if (!body.ivp) return;
    // Pose the world in the death pose: the 16 bind pose joints of this
    // corpse are already in the model space.
    float joints[JOINT_COUNT * 3];
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        joints[i * 3 + 0] = body.joints[i].base.x;
        joints[i * 3 + 1] = body.joints[i].base.y;
        joints[i * 3 + 2] = body.joints[i].base.z;
    }
    IvpCorpseSetJoints(body.ivp, joints);
}

static void IvpDestroyCorpse(Body &body)
{
    if (body.serial)
        oc2_bullet::DestroyRagdoll(body.serial);
    if (!body.ivp) return;
    IvpCorpseDestroy(body.ivp);
    body.ivp = NULL;
}

// Resolve the world once per step for the whole rig: the floor height under
// the root is pushed into the DLL, which clamps the entire skeleton. Teleporting
// the bodies one by one fights the constraints and tears the ragdoll apart, so
// the per body teleport is not used here (it stays available in the adapter for
// a rate limited rescue only).
static int IvpResolveWorld(Body &body)
{
    const Vector rootWorld = body.deathOrigin + body.joints[PELVIS].position;
    const float floorWorld = IvpGroundHeight(rootWorld);
    const float floorModel = floorWorld - body.deathOrigin.z;
    IvpCorpseSetGround(body.ivp, floorModel);
    body.contacts++;
    return 1;
}

// One fixed step of the IVP world, then map the 16 joint positions back onto
// the product joints and the 12 angular parts. The existing render path then
// follows the joint skeleton with no further change.
static void IvpSimulate(Body &body, float dt)
{
    if (!body.ivp) return;
    if (IvpCorpseStep(body.ivp, dt) != 0) { IvpDestroyCorpse(body); return; }
    IvpResolveWorld(body);
    float joints[JOINT_COUNT * 3];
    float quats[12 * 4];
    if (IvpCorpseReadJoints(body.ivp, joints, quats) != JOINT_COUNT) return;
    Vector before[JOINT_COUNT];
    for (int i = 0; i < JOINT_COUNT; ++i) before[i] = body.joints[i].position;
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        body.joints[i].position = Vector(joints[i * 3], joints[i * 3 + 1], joints[i * 3 + 2]) +
            body.deathOrigin;
    }
    // The part rotations: the DLL body rotation is the segment rotation, the
    // part orientation is that rotation composed with the bind pose offset.
    static const int kIvpPartBody[12] = {1, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, -1};
    for (int part = 0; part < kPartCount; ++part)
    {
        const int source = kIvpPartBody[part];
        if (source < 0) continue;
        Quat bodyRotation;
        bodyRotation.x = quats[source * 4];
        bodyRotation.y = quats[source * 4 + 1];
        bodyRotation.z = quats[source * 4 + 2];
        bodyRotation.w = quats[source * 4 + 3];
        body.parts[part].orientation = QuatMultiply(bodyRotation, body.parts[part].baseOrientation);
    }
    float motion = 0.0f;
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        const float travel = Length(body.joints[i].position - before[i]);
        if (travel > motion) motion = travel;
        body.joints[i].previous = before[i];
    }
    float worstError = 0.0f;
    for (int i = 1; i < JOINT_COUNT; ++i)
    {
        const float error = fabsf(Length(body.joints[i].position -
            body.joints[kParent[i]].position) - body.rest[i]);
        if (error > worstError) worstError = error;
    }
    const bool supported = body.contacts > 0;
    body.quietFrames = supported && motion < 0.06f && worstError < 0.8f
        ? body.quietFrames + 1 : 0;
    const bool stationary = StationaryForSleep(body, supported, motion, worstError);
    if ((body.quietFrames > 60 && body.lastTime - body.birth >= 2.0) || stationary ||
        (supported && body.lastTime - body.birth > 12.0 && motion < 0.12f && worstError < 1.2f))
    {
        body.sleeping = true;
        for (int i = 0; i < JOINT_COUNT; ++i) body.joints[i].previous = body.joints[i].position;
    }
    ++body.steps;
}

void Simulate(Body &body, float dt)
{
    if (BulletMode()) return;
    if (body.ivp)
    {
        IvpSimulate(body, dt);
        return;
    }
    Vector start[JOINT_COUNT];
    Vector predictedTravel[JOINT_COUNT];
    bool stuck[JOINT_COUNT] = {};
    Vector contactNormal[JOINT_COUNT][2];
    float contactOffset[JOINT_COUNT][2] = {};
    bool contactActive[JOINT_COUNT][2] = {};
    int contactCount[JOINT_COUNT] = {};
    SampleContact samples[MAX_SEGMENT_SAMPLES] = {};
    IntegrateAngular(body, dt);
    for (int i = 0; i < body.sampleCount; ++i)
        samples[i].start = SamplePosition(body, body.samples[i]);
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        JointState &joint = body.joints[i];
        start[i] = joint.position;
        // Keep inertia in flight. Ground friction and joint projection supply
        // the damping at impact, instead of removing almost all momentum each second.
        Vector velocity = Limited(joint.position - joint.previous, 600.0f * dt) * expf(-kLinearDamping * dt);
        joint.position = joint.position + velocity + Vector(0.0f, 0.0f, -800.0f * dt * dt);
        predictedTravel[i] = joint.position - start[i];
        Vector normal;
        bool trapped = false;
        float planeOffset;
        const bool collision = Collide(body, joint, start[i], normal, trapped, planeOffset);
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
        if ((iteration & 3) == 0)
            EnforceAngularLimits(body, iteration/63.0f);
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
                if (Collide(body, body.joints[i], start[i], normal, trapped, planeOffset))
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

    SolveSkeleton(body);
    EnforceAngularLimits(body, 1.0f);
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
        if (!stuck[i]) carry = centerTravel + (carry - centerTravel) * expf(-kJointDamping * dt);
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
    ReconstructAngularVelocity(body, solvedCarry, dt);
    SyncAngularParts(body);
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
    // Give the initial fall and small limb settling at least two seconds.
    // Late landings retain the normal quiet interval instead of waiting for
    // a second full settling window after every contact change.
    if ((body.quietFrames > 60 && body.lastTime - body.birth >= 2.0) ||
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

void DeriveCollisionRadii(Body &body)
{
    float zMin = body.joints[0].base.z, zMax = zMin;
    for (int i = 1; i < JOINT_COUNT; ++i)
    {
        zMin = fminf(zMin, body.joints[i].base.z);
        zMax = fmaxf(zMax, body.joints[i].base.z);
    }
    float skeletonScale = zMax - zMin;
    if (skeletonScale < 40.0f || skeletonScale > 100.0f)
        skeletonScale = Clamp(body.rest[WAIST] + body.rest[CHEST] +
            body.rest[HEAD] + body.rest[LEFT_HIP] + body.rest[LEFT_KNEE],
            40.0f, 100.0f);
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        float boneLength = body.rest[i];
        for (int child = 1; child < JOINT_COUNT; ++child)
            if (kParent[child] == i && body.rest[child] > boneLength)
                boneLength = body.rest[child];
        const float limbRadius = Clamp(
            1.1f + boneLength*0.12f, 3.5f, 4.5f);
        if (i == PELVIS || i == CHEST)
            body.joints[i].radius = Clamp(
                fmaxf(boneLength*0.30f, skeletonScale*0.10f), 7.0f, 9.0f);
        else if (i == WAIST)
            body.joints[i].radius = Clamp(
                fmaxf(boneLength*0.35f, skeletonScale*0.085f), 6.0f, 7.5f);
        else if (i == HEAD)
            body.joints[i].radius = Clamp(
                fmaxf(boneLength*0.50f, skeletonScale*0.075f), 6.0f, 7.5f);
        else
            body.joints[i].radius = limbRadius;
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
        joint.index = i;
        joint.bone = bone;
        joint.base = Origin(body.baseMatrix[bone]);
        joint.position = joint.base;
        joint.lastFree = joint.position;
        joint.radius = 0.0f;
        body.rest[i] = kParent[i] >= 0
            ? Length(joint.base - body.joints[kParent[i]].base) : 0.0f;
        if (kParent[i] >= 0 && body.rest[i] < 0.5f) body.rest[i] = 0.5f;
    }
    DeriveCollisionRadii(body);
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
            sample.fraction = part == 0 ? 0.36f : 0.68f;
            sample.dynamic = part != 0;
            sample.enabled = part == 0;
            sample.volume.index = -1;
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
        const Vector fallback = Unit(Cross(upper, base.forward *
            (chain < 2 ? 1.0f : -1.0f)), base.side);
        body.hingeNormal[chain] = Length(Cross(upper, lower)) > 0.045f
            ? Unit(Cross(upper, lower), fallback) : fallback;
    }
    if (Length(body.impact) < 0.01f)
    {
        body.impact = body.baseFrame.forward * 12.0f;
        if (gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("ragdoll_bullet: corpse %u has no shot direction, falling straight\n",
                body.serial);
    }
    InitialiseAngularParts(body);
    ApplyDeathImpulse(body);
    IvpCreateCorpse(body);
    float totalMass = 0.0f;
    for (int i = 0; i < JOINT_COUNT; ++i) totalMass += kMass[i];
    const Vector impulseVelocity = Limited(body.impact*(8.0f/totalMass), 40.0f);
    for (int i = 0; i < JOINT_COUNT; ++i)
    {
        JointState &joint = body.joints[i];
        Vector velocity = body.hasLiveVelocity ? body.boneVelocity[joint.bone] : body.launch;
        velocity = velocity + impulseVelocity;
        joint.previous = joint.position - Limited(velocity, 400.0f)*(1.0f/90.0f);
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
    // The Bullet backend starts the corpse from the pose the fall animation
    // left behind, so the bones of the frame the physics takes over are the
    // ones that count and not the ones from the moment of death.
    if (!body.hasLivePose || BulletMode())
        memcpy(body.baseMatrix, *renderer.m_pbonetransform,
            sizeof(body.baseMatrix[0]) * renderer.m_pStudioHeader->numbones);
    return InitialisePose(body, renderer.m_pStudioHeader);
}

Vector RotateAboutAxis(const Vector &value, const Vector &axis, float angle)
{
    const Vector unit = Unit(axis, Vector(0.0f, 0.0f, 1.0f));
    const float cosine = cosf(angle);
    const float sine = sinf(angle);
    return value * cosine + Cross(unit, value) * sine +
        unit * (Dot(unit, value) * (1.0f - cosine));
}

// Keep a drawn limb inside the arc it had in the death pose.
Vector ClampToCone(const Vector &direction, const Vector &reference, float limit)
{
    const Vector d = direction;
    const Vector r = Unit(reference, d);
    const float angle = acosf(Clamp(Dot(d, r), -1.0f, 1.0f));
    if (angle <= limit || angle < 0.0001f) return d;
    Vector axis = Cross(r, d);
    const float length = Length(axis);
    if (length < 0.0001f) return d;
    axis = axis * (1.0f / length);
    return Unit(RotateAboutAxis(d, axis, angle - limit), d);
}

// Separate forward and sideways allowances around the death-pose direction.
Vector ClampToSwingAxes(const Vector &direction, const Vector &reference,
    const Vector &flexionAxis, const Vector &abductionAxis,
    float flexion, float abduction)
{
    Vector d = direction;
    const Vector r = Unit(reference, d);
    if (flexion > 0.0f)
    {
        Vector a = r - flexionAxis * Dot(r, flexionAxis);
        Vector b = d - flexionAxis * Dot(d, flexionAxis);
        const float la = Length(a), lb = Length(b);
        if (la > 0.001f && lb > 0.001f)
        {
            a = a * (1.0f / la);
            b = b * (1.0f / lb);
            const float angle = atan2f(Dot(Cross(a, b), flexionAxis), Dot(a, b));
            const float limit = Clamp(angle, -flexion, flexion);
            if (fabsf(limit - angle) > 0.0001f)
                d = Unit(RotateAboutAxis(d, flexionAxis, limit - angle), d);
        }
    }
    if (abduction > 0.0f)
    {
        Vector a = r - abductionAxis * Dot(r, abductionAxis);
        Vector b = d - abductionAxis * Dot(d, abductionAxis);
        const float la = Length(a), lb = Length(b);
        if (la > 0.001f && lb > 0.001f)
        {
            a = a * (1.0f / la);
            b = b * (1.0f / lb);
            const float angle = atan2f(Dot(Cross(a, b), abductionAxis), Dot(a, b));
            const float limit = Clamp(angle, -abduction, abduction);
            if (fabsf(limit - angle) > 0.0001f)
                d = Unit(RotateAboutAxis(d, abductionAxis, limit - angle), d);
        }
    }
    return d;
}

// The hinge axis has to follow the clamped bone direction, otherwise the
// rendered roll drifts away from the limb it belongs to.
Vector RenderHingeNormal(const Body &body, int chain, const Frame &frame,
    const Vector &axis)
{
    const int first = kChains[chain][0], middle = kChains[chain][1];
    const Frame &base = chain < 2 ? body.baseFrame : body.basePelvisFrame;
    const Vector oldAxis = RotateFrame(body.joints[middle].base - body.joints[first].base, base, frame);
    return Unit(RotateArc(RotateFrame(body.hingeNormal[chain], base, frame), oldAxis, axis), frame.side);
}

// Rebuild the drawn chain directions for this frame.
void CacheRenderDirections(Body &body, const Frame &torso)
{
    for (int chain = 0; chain < 4; ++chain)
    {
        const int first = kChains[chain][0], middle = kChains[chain][1];
        const int last = kChains[chain][2];
        const Frame &socket = chain < 2 ? torso : PelvisFrame(body, false);
        const Vector upper = Unit(body.joints[middle].position - body.joints[first].position, socket.up);
        const Vector upperBase = Unit(body.joints[middle].base - body.joints[first].base, socket.up);
        if (chain < 2)
        {
            body.renderDir[chain][0] = ClampToCone(upper, upperBase, kShoulderSwing);
        }
        else
        {
            body.renderDir[chain][0] = ClampToSwingAxes(upper, upperBase,
                socket.side, socket.forward, kHipFlexion, kHipAbduction);
        }
        const Vector lower = Unit(body.joints[last].position - body.joints[middle].position, socket.up);
        if (chain < 2)
        {
            body.renderDir[chain][1] = lower;
        }
        else
        {
            // A knee that was already bent in the death pose keeps its own arc.
            const Vector lowerBase = Unit(body.joints[last].base - body.joints[middle].base, socket.up);
            const float baseKnee = acosf(Clamp(Dot(upperBase, lowerBase), -1.0f, 1.0f));
            const float kneeLimit = baseKnee > kKneeFlexion ? baseKnee : kKneeFlexion;
            body.renderDir[chain][1] = ClampToCone(lower, body.renderDir[chain][0], kneeLimit);
        }
    }
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
        to.up = body.renderDir[chain][owner == first ? 0 : 1];
        // Transport the hinge frame through a straight elbow/knee too. A cross
        // product of almost-collinear segments would introduce arbitrary rolls.
        to.side = RenderHingeNormal(body, chain, socket, body.renderDir[chain][0]);
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
    const mstudiobone_t *bones = reinterpret_cast<const mstudiobone_t *>(
        reinterpret_cast<const byte *>(body.header) + body.header->boneindex);
    const Frame torso = TorsoFrame(body, false);
    CacheRenderDirections(body, torso);
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
        const int parent = bones[bone].parent;
        if (parent >= 0 && parent < bone)
        {
            const float (*newParent)[4] = (*renderer.m_pbonetransform)[parent];
            const Vector &parentLocal = body.boneParentOffset[bone];
            for (int axis = 0; axis < 3; ++axis)
                position[axis] = newParent[axis][3] +
                    newParent[axis][0]*parentLocal.x +
                    newParent[axis][1]*parentLocal.y +
                    newParent[axis][2]*parentLocal.z;
        }
        render[0][3] = light[0][3] = position.x;
        render[1][3] = light[1][3] = position.y;
        render[2][3] = light[2][3] = position.z;
    }
    body.joints[PELVIS].position.CopyToArray(renderer.m_pCurrentEntity->origin);
    VectorCopy(renderer.m_pCurrentEntity->origin,
        renderer.m_pCurrentEntity->curstate.origin);
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
        gEngfuncs.Con_Printf("ragdoll_spawn: serial=%d player=%d ready=%d early=%d age=%.3f origin=%.1f,%.1f,%.1f\n",
            body.serial,body.playerIndex,int(body.ready),int(body.early),now-body.birth,
            body.deathOrigin.x,body.deathOrigin.y,body.deathOrigin.z);
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

static void SpawnCurrentDeath(int victimIndex, int killerIndex, bool headshot)
{
    OC2_RagdollInit();
    if (victimIndex < 1 || victimIndex > gEngfuncs.GetMaxClients() ||
        victimIndex > 32 || !s_ragdoll || s_ragdoll->value <= 0 ||
        !IEngineStudio.IsHardware || !IEngineStudio.IsHardware())
        return;
    PlayerDeath &state = DeathState(victimIndex);
    BeginDeath(state);
    if (state.claimed) return;
    // BeginDeath leaves everything alone when the player was already dead, so
    // the shot of this death has to be cleared here. Otherwise a death without
    // a usable shooter position would fall back to the position of the
    // previous death.
    state.killer = killerIndex;
    state.headshot = headshot;
    state.killerOriginValid = false;
    state.shooterHeight = 0.0f;
    cl_entity_t *player = gEngfuncs.GetEntityByIndex(victimIndex);
    // DeathMsg is global, entity updates are PVS-limited. Wait for the
    // authoritative ClCorpse position when this slot is only an old snapshot.
    if (!OC2_CurrentPlayerSnapshot(player)) return;
    const LivePose &pose = s_livePose[victimIndex];
    const double now = gEngfuncs.GetClientTime();
    const bool freshPose = pose.valid && pose.model &&
        now >= pose.time - 0.05 && now - pose.time < 0.5 &&
        (Vector(player->origin) - pose.origin).Length() <= 128.0f;
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
    // The corpse starts from the standing pose and the physics does the whole
    // fall. That is what a ragdoll is: the shot only decides where the body
    // is pushed and how hard, the rest is solved.
    const int sequence = FindLiveIdleSequence(header);
    bool haveAttacker = false;
    if (BulletMode() && killerIndex > 0 &&
        killerIndex <= gEngfuncs.GetMaxClients() && killerIndex != victimIndex)
    {
        cl_entity_t *killer = gEngfuncs.GetEntityByIndex(killerIndex);
        Vector shooter;
        if (killer && UsableShooterOrigin(killer, origin, shooter))
        {
            // The shooter may have moved on by the time the corpse reaches the
            // physics, so the position of the shot is kept with the death.
            state.killerOrigin[0] = shooter.x;
            state.killerOrigin[1] = shooter.y;
            state.killerOrigin[2] = shooter.z;
            state.killerOriginValid = true;
            // How much higher the shooter stood. It decides where on the body
            // the shot landed.
            state.shooterHeight = shooter.z - origin.z;
            haveAttacker = true;
        }
    }
    if (gEngfuncs.Con_Printf && BulletMode())
    {
        gEngfuncs.Con_Printf("ragdoll_bullet: corpse starts from the standing pose, shooter=%d headshot=%d\n",
            haveAttacker ? 1 : 0, headshot ? 1 : 0);
    }
    g_oc2CorpseSequenceFramerate = 1.0f;
    // The corpse has to play the death animation at its own frame rate.
    //
    // The renderer drives a studio sequence from two sources at once
    // (StudioModelRenderer.cpp:932-947): the temp entity frame, which counts
    // in a 0..255 space, and the elapsed time since the animation started.
    // Together they advance the animation frame at
    //
    //     framerate * (fps + (numframes - 1) / 256)
    //
    // per second, so the rate has to be divided by that sum to make the
    // animation play at its own fps. With the plain fps the second term
    // runs the animation dozens of times too fast and the corpse reaches
    // its last frame within a few milliseconds, which is exactly the
    // "every corpse falls the same way" symptom.
    {
        const mstudioseqdesc_t *sequences = reinterpret_cast<const mstudioseqdesc_t *>(
            reinterpret_cast<const byte *>(header) + header->seqindex);
        const float fps = (sequence > 0 && sequence < header->numseq)
            ? sequences[sequence].fps : 0.0f;
        const int total = (sequence > 0 && sequence < header->numseq)
            ? sequences[sequence].numframes : 0;
        g_oc2CorpseSequenceFramerate = (fps > 0.0f && total > 1)
            ? (fps / (fps + static_cast<float>(total - 1) / 256.0f)) : 1.0f;
    }
    // The killer and the hit belong to the death, not to the moment the
    // corpse was created: the corpse is registered again a second or more
    // later, after its death animation has played, and the temporaries that
    // carried them are long gone by then.
    s_pendingEarlyVictim = victimIndex;
    s_pendingKiller = killerIndex;
    s_pendingHeadshot = headshot;
    CreateCorpse(origin, angles, model->name, static_cast<float>(now), sequence,
        player ? player->curstate.body : 0, victimIndex);
    s_pendingEarlyVictim = s_pendingKiller = 0;
    s_pendingHeadshot = false;
}

void OC2_RagdollSpawnDeath(int victimIndex, int killerIndex, bool headshot)
{
    OC2_RagdollInit();
    if(victimIndex<1||victimIndex>32||victimIndex>gEngfuncs.GetMaxClients()||!s_ragdoll||s_ragdoll->value<=0)return;
    PlayerDeath &state=DeathState(victimIndex);BeginDeath(state);
    if(state.claimed)return;
    PendingDeath &pending=s_pendingDeaths[victimIndex];pending.active=true;pending.killer=killerIndex;
    pending.userid=state.userid;pending.generation=state.generation;pending.headshot=headshot;
    const cl_entity_t *local=gEngfuncs.GetLocalPlayer();
    const entity_state_t *snapshot=local&&IEngineStudio.GetPlayerState?IEngineStudio.GetPlayerState(local->index-1):NULL;
    pending.message=snapshot?snapshot->messagenum:0;pending.queued=gEngfuncs.GetClientTime();
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
        // The player is alive again, the shot of the last death is gone.
        state.killer = 0;
        state.headshot = false;
        state.killerOriginValid = false;
        state.shooterHeight = 0.0f;
        // The corpse of the last death is released too, so that a death that
        // follows without a living edge in between is not thrown away.
        state.claimed = false;
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

    if (BulletMode() && gEngfuncs.Con_Printf)
    {
        const int sequence = corpse->entity.curstate.sequence;
        gEngfuncs.Con_Printf("ragdoll_bullet: register check seq=%d numseq=%d model=%s\n",
            sequence, header->numseq,
            corpse->entity.model ? corpse->entity.model->name : "(null)");
    }

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
    IvpDestroyCorpse(body);
    body = Body();
    if (s_nextSerial >= INT_MAX - 1) s_nextSerial = 0;
    body.serial = ++s_nextSerial;
    body.playerIndex = playerIndex;
    int killerIndex = 0;
    bool headshot = false;
    if (playerIndex >= 1 && playerIndex <= 32)
    {
        const PlayerDeath &state = DeathState(playerIndex);
        body.userid = state.userid;
        body.deathGeneration = state.generation;
        killerIndex = state.killer;
        headshot = state.headshot;
    }
    // The corpse is registered a second time after its death animation has
    // played, so the killer and the hit come from the death state and not
    // from a temporary that was cleared when the corpse was created.
    body.early = killerIndex > 0 || s_pendingEarlyVictim == playerIndex;
    body.headshot = headshot || (body.early && s_pendingHeadshot);
    // Where the shot lands on the body. Without a shooter only the head shot
    // tells anything, a level shot is assumed.
    body.hitHeight = HitHeightFor(0.0f, body.headshot);
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
            now >= pose.time - 0.05 && now - pose.time < 0.5 &&
            (body.deathOrigin - pose.origin).Length() <= 128.0f)
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
    if (killerIndex > 0 && killerIndex <= gEngfuncs.GetMaxClients() &&
        killerIndex != playerIndex)
    {
        float shooter[3] = {0.0f, 0.0f, 0.0f};
        bool haveShooter = false;
        float shooterHeight = 0.0f;
        // The position of the shot comes with the death. The shooter may have
        // moved on by the time the corpse reaches the physics.
        if (playerIndex >= 1 && playerIndex <= 32)
        {
            const PlayerDeath &state = DeathState(playerIndex);
            if (state.killerOriginValid)
            {
                memcpy(shooter, state.killerOrigin, sizeof(shooter));
                shooterHeight = state.shooterHeight;
                haveShooter = true;
            }
        }
        if (!haveShooter)
        {
            cl_entity_t *killer = gEngfuncs.GetEntityByIndex(killerIndex);
            Vector position;
            if (killer && UsableShooterOrigin(killer, body.deathOrigin, position))
            {
                shooter[0] = position.x;
                shooter[1] = position.y;
                shooter[2] = position.z;
                shooterHeight = position.z - body.deathOrigin.z;
                haveShooter = true;
            }
        }
        if (haveShooter)
        {
            Vector away = body.deathOrigin - Vector(shooter);
            away.z = 0.0f;
            const float distance = Length(away);
            if (distance > 1.0f)
            {
                // A shot from close range throws the body, a distant one only
                // makes it flop over.
                const float reach = Clamp(240.0f / (distance + 120.0f), 0.55f, 1.45f);
                const float strength = (body.headshot ? 125.0f : 100.0f) * reach;
                body.impact = away * (strength / distance);
            }
            // The height of the shooter decides where on the body the shot
            // landed.
            body.hitHeight = HitHeightFor(shooterHeight, body.headshot);
            if (gEngfuncs.Con_Printf)
                gEngfuncs.Con_Printf("ragdoll_bullet: corpse %u shot from %.0f inch away, impact %.0f, hit height %.2f\n",
                    body.serial, distance, Length(body.impact), body.hitHeight);
        }
        else if (gEngfuncs.Con_Printf)
            gEngfuncs.Con_Printf("ragdoll_bullet: corpse %u has no attacker position, no impact\n",
                body.serial);
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

void OC2_RagdollResolveDeaths(double now)
{
    // Reliable DeathMsg precedes entity deltas inside a packet. Resolve the
    // early body here, after packet parsing, against the updated PVS snapshot.
    for(int player=1;player<=32;++player)if(s_pendingDeaths[player].active)
    {
        const PendingDeath pending=s_pendingDeaths[player];
        const PlayerDeath &state=DeathState(player);
        const cl_entity_t *local=gEngfuncs.GetLocalPlayer();
        const entity_state_t *snapshot=local&&IEngineStudio.GetPlayerState?IEngineStudio.GetPlayerState(local->index-1):NULL;
        if(!snapshot||snapshot->messagenum==pending.message)
        {if(now-pending.queued>.25||now<pending.queued)s_pendingDeaths[player].active=false;continue;}
        s_pendingDeaths[player].active=false;
        if(state.dead&&!state.claimed&&state.userid==pending.userid&&state.generation==pending.generation&&now-pending.queued<=.25)
            SpawnCurrentDeath(player,pending.killer,pending.headshot);
    }
}

void OC2_RagdollTick(double clientTime)
{
    OC2ColdTimer cold("ragdoll_tick","bodies");
    double now = clientTime;
    if (IEngineStudio.GetTimes) IEngineStudio.GetTimes(NULL, &now, NULL);

    // The Bullet world holds every corpse, so the step is global.
    if (BulletMode())
    {
        // One world holds every corpse, so the step is global.
        static double lastTime = 0.0;
        const double dt = now - lastTime;
        lastTime = now;
        if (dt > 0.0 && dt < 0.25)
            oc2_bullet::Step((float)dt, now);
    }
    for (int i = 0; i < 64; ++i)
    {
        Body &body = s_bodies[i];
        if (!body.serial || !body.ready || body.expires < now ||
            !body.corpse || body.corpse->entity.curstate.iuser4 != body.serial)
        {
            IvpDestroyCorpse(body);
            continue;
        }
        for (int step = 0; step < 10 && body.catchupSteps > 0 &&
            !body.sleeping; ++step, --body.catchupSteps)
            Simulate(body, 1.0f / 90.0f);
        Advance(body, now);
    }
}

void OC2_RagdollApply(CStudioModelRenderer &renderer)
{
    OC2ColdTimer cold("ragdoll_apply",renderer.m_pRenderModel?renderer.m_pRenderModel->name:nullptr);
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
    if (BulletMode() && oc2_bullet::HasRagdoll(body->serial))
    {
        oc2_bullet::ApplyBones(renderer, body->serial);
        return;
    }
    ApplyPose(*body, renderer);
}

void OC2_RagdollClear()
{
    for (int i = 0; i < 64; ++i) IvpDestroyCorpse(s_bodies[i]);
    for (int i = 0; i < 64; ++i) s_bodies[i] = Body();
    for (int i = 0; i < 33; ++i) s_livePose[i] = LivePose();
    for (int i = 0; i < 33; ++i) s_deaths[i] = PlayerDeath();
    for (int i = 0; i < 33; ++i) s_pendingDeaths[i] = PendingDeath();
    s_pendingEarlyVictim = s_pendingKiller = 0;
    s_pendingHeadshot = false;
}
