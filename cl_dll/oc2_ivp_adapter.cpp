/* Client side adapter for oc2_ivp.dll (the shipped x64 ABI).
 *
 * Coordinates: the client world space, inches, Z UP. The DLL uses the same
 * space, so nothing is converted on this boundary.
 *
 * The DLL owns the 12 body skeleton (it is built from the measured production
 * bind pose). The adapter recovers the 16 joint positions from the 12 body
 * transforms and exposes the per corpse world to the client.
 */
#include "oc2_ivp_adapter.h"
#include "oc2_ivp.h"

// The client ships its own lean windows wrapper (cl_dll/windows_lean.h): it
// defines HSPRITE for the real header, so this file can also be included
// into the offline harness translation unit without a type clash.
#include "windows_lean.h"
// The lean windows header defines the legacy near/far macros as empty. The
// engine headers undefine them, so a translation unit that pulls this file in
// after them has to restore that, otherwise any far/near identifier breaks.
#undef near
#undef far
#include <stdio.h>
#include <string.h>
#include <math.h>

namespace {

struct IvpApi
{
    HMODULE module;
    const char *(*version)(void);
    void *(*create)(int, float);
    void (*destroy)(void *);
    int (*step)(void *, float);
    int (*body_count)(void *);
    int (*get_body_transform)(void *, int, float *);
    float (*get_body_radius)(void *, int);
    int (*health)(void *);
    int (*set_ground_y)(void *, float);
    int (*set_limits)(void *, int);
    int (*set_body_transform)(void *, int, const float *);
    int (*set_body_pose)(void *, const float *, const float *);
};

IvpApi g_api;
bool g_loaded = false;
bool g_ready = false;
char g_status[256] = "not loaded";

// The measured production bind pose, the same table the DLL builds from.
// The joint order matches the product joint order (pelvis .. right foot).
const float kJoint[16][3] = {
    {-0.03f, 0.64f, 37.10f}, {-0.05f, 0.00f, 43.02f}, {-0.03f, 0.58f, 51.23f}, {-0.01f, 1.00f, 59.73f},
    { 7.16f, 0.89f, 54.92f}, {14.01f, 1.28f, 47.31f}, {20.34f, -3.19f, 39.59f},
    {-7.18f, 1.44f, 54.92f}, {-13.97f, 2.37f, 47.31f}, {-20.63f, -1.60f, 39.59f},
    { 3.59f, 0.50f, 37.10f}, { 3.70f, -0.35f, 20.55f}, { 3.90f, 1.33f, 5.52f},
    {-3.64f, 0.78f, 37.10f}, {-3.82f, -0.06f, 20.55f}, {-3.89f, 1.63f, 5.52f}
};
const int kBodyPivot[12] = {0, 1, 2, 3, 4, 5, 7, 8, 10, 11, 13, 14};
const int kBodyTip[12] = {1, 2, 3, -1, 5, 6, 8, 9, 11, 12, 14, 15};

template <typename T>
bool Bind(T &slot, const char *name, bool required)
{
    slot = (T)GetProcAddress(g_api.module, name);
    if (!slot && required)
    {
        snprintf(g_status, sizeof(g_status), "missing symbol %s", name);
        return false;
    }
    return true;
}

void TryLoad(const char *path)
{
    if (g_api.module) return;
    g_api.module = LoadLibraryA(path);
}

void LoadFromClientDirectory()
{
    HMODULE self = NULL;
    char path[MAX_PATH];
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)(const void*)&IvpAdapterInit, &self)) return;
    if (!GetModuleFileNameA(self, path, sizeof(path))) return;
    char *slash = strrchr(path, '\\');
    if (!slash) return;
    slash[1] = 0;
    char candidate[MAX_PATH];
    snprintf(candidate, sizeof(candidate), "%soc2_ivp.dll", path);
    TryLoad(candidate);
}

void LoadFromGameDirectory()
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, sizeof(path))) return;
    char *slash = strrchr(path, '\\');
    if (!slash) return;
    slash[1] = 0;
    char candidate[MAX_PATH];
    snprintf(candidate, sizeof(candidate), "%scstrike\\cl_dlls\\oc2_ivp.dll", path);
    TryLoad(candidate);
    snprintf(candidate, sizeof(candidate), "%soc2_ivp.dll", path);
    TryLoad(candidate);
}

void QuatFromTo(const float from[3], const float to[3], float out[4])
{
    float c = from[0] * to[0] + from[1] * to[1] + from[2] * to[2];
    if (c > 0.9999f) { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; return; }
    if (c < -0.9999f)
    {
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        if (fabsf(from[0]) < 0.9f) { ax = 1.0f; }
        else { ay = 1.0f; }
        out[0] = ax; out[1] = ay; out[2] = az; out[3] = 0.0f;
        return;
    }
    out[0] = from[1] * to[2] - from[2] * to[1];
    out[1] = from[2] * to[0] - from[0] * to[2];
    out[2] = from[0] * to[1] - from[1] * to[0];
    out[3] = 1.0f + c;
    const float n = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    if (n > 1e-6f)
        for (int k = 0; k < 4; ++k) out[k] /= n;
}

void RotateByQuat(const float q[4], const float v[3], float out[3])
{
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    const float tx = 2.0f * (y * v[2] - z * v[1]);
    const float ty = 2.0f * (z * v[0] - x * v[2]);
    const float tz = 2.0f * (x * v[1] - y * v[0]);
    out[0] = v[0] + w * tx + (y * tz - z * ty);
    out[1] = v[1] + w * ty + (z * tx - x * tz);
    out[2] = v[2] + w * tz + (x * ty - y * tx);
}

} // namespace

void IvpAdapterInit()
{
    if (g_loaded) return;
    g_loaded = true;
    LoadFromClientDirectory();
    if (!g_api.module) LoadFromGameDirectory();
    if (!g_api.module) TryLoad("oc2_ivp.dll");
    if (!g_api.module)
    {
        snprintf(g_status, sizeof(g_status), "oc2_ivp.dll not found, using the PBD solver");
        return;
    }
    bool ok = true;
    ok = ok && Bind(g_api.version, "oc2_ivp_version", true);
    ok = ok && Bind(g_api.create, "oc2_ivp_create", true);
    ok = ok && Bind(g_api.destroy, "oc2_ivp_destroy", true);
    ok = ok && Bind(g_api.step, "oc2_ivp_step", true);
    ok = ok && Bind(g_api.body_count, "oc2_ivp_body_count", true);
    ok = ok && Bind(g_api.get_body_transform, "oc2_ivp_get_body_transform", true);
    ok = ok && Bind(g_api.get_body_radius, "oc2_ivp_get_body_radius", true);
    ok = ok && Bind(g_api.health, "oc2_ivp_health", true);
    ok = ok && Bind(g_api.set_ground_y, "oc2_ivp_set_ground_y", true);
    ok = ok && Bind(g_api.set_limits, "oc2_ivp_limits", false);
    ok = ok && Bind(g_api.set_body_transform, "oc2_ivp_set_body_transform", true);
    Bind(g_api.set_body_pose, "oc2_ivp_set_body_pose", false);
    if (!ok)
    {
        FreeLibrary(g_api.module);
        g_api.module = NULL;
        snprintf(g_status, sizeof(g_status), "%s, using the PBD solver", g_status);
        return;
    }
    const char *version = g_api.version ? g_api.version() : "";
    if (!version || strncmp(version, "oc2_ivp", 7) != 0)
    {
        FreeLibrary(g_api.module);
        g_api.module = NULL;
        snprintf(g_status, sizeof(g_status), "unexpected oc2_ivp.dll version, using the PBD solver");
        return;
    }
    g_ready = true;
    snprintf(g_status, sizeof(g_status), "oc2_ivp.dll ready (%s)%s", version,
        g_api.set_body_pose ? " with pose extension" : " without pose extension");
}

bool IvpAdapterAvailable()
{
    return g_ready;
}

const char *IvpAdapterStatus()
{
    return g_status;
}

void IvpAdapterShutdown()
{
    if (g_api.module) FreeLibrary(g_api.module);
    memset(&g_api, 0, sizeof(g_api));
    g_ready = false;
    g_loaded = false;
    snprintf(g_status, sizeof(g_status), "not loaded");
}

struct IvpCorpse
{
    void *world;
    float axis[OC2_IVP_BODIES][3];
    float half[OC2_IVP_BODIES];
    float scratch[OC2_IVP_BODIES * 7];
};

IvpCorpse *IvpCorpseCreate(float groundZ)
{
    IvpAdapterInit();
    if (!g_ready) return NULL;
    IvpCorpse *corpse = new IvpCorpse;
    memset(corpse, 0, sizeof(*corpse));
    // The gravity is the client gravity in the shared Z up space.
    corpse->world = g_api.create(1, -800.0f);
    if (!corpse->world)
    {
        delete corpse;
        return NULL;
    }
    if (g_api.body_count(corpse->world) != OC2_IVP_BODIES)
    {
        g_api.destroy(corpse->world);
        delete corpse;
        return NULL;
    }
    for (int i = 0; i < OC2_IVP_BODIES; ++i)
    {
        const int pivot = kBodyPivot[i];
        const int tip = kBodyTip[i];
        float ax = 0.0f, ay = 0.0f, az = 1.0f;
        float half = 4.0f;
        if (tip >= 0)
        {
            ax = kJoint[tip][0] - kJoint[pivot][0];
            ay = kJoint[tip][1] - kJoint[pivot][1];
            az = kJoint[tip][2] - kJoint[pivot][2];
            const float length = sqrtf(ax * ax + ay * ay + az * az);
            half = length * 0.5f;
            if (length > 1e-6f) { ax /= length; ay /= length; az /= length; }
        }
        corpse->axis[i][0] = ax;
        corpse->axis[i][1] = ay;
        corpse->axis[i][2] = az;
        corpse->half[i] = half;
    }
    g_api.set_ground_y(corpse->world, groundZ);
    return corpse;
}

void IvpCorpseSetJoints(IvpCorpse *corpse, const float *joints16)
{
    if (!corpse || !g_ready || !joints16) return;
    float centers[OC2_IVP_BODIES * 3];
    float quats[OC2_IVP_BODIES * 4];
    for (int i = 0; i < OC2_IVP_BODIES; ++i)
    {
        const int pivot = kBodyPivot[i];
        const int tip = kBodyTip[i];
        const float *a = joints16 + pivot * 3;
        float b[3];
        if (tip >= 0)
        {
            for (int k = 0; k < 3; ++k) b[k] = joints16[tip * 3 + k];
        }
        else
        {
            b[0] = a[0]; b[1] = a[1]; b[2] = a[2] + 4.0f;
        }
        for (int k = 0; k < 3; ++k) centers[i * 3 + k] = (a[k] + b[k]) * 0.5f;
        float target[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        const float length = sqrtf(target[0] * target[0] + target[1] * target[1] + target[2] * target[2]);
        if (length > 1e-6f)
            for (int k = 0; k < 3; ++k) target[k] /= length;
        QuatFromTo(corpse->axis[i], target, quats + i * 4);
    }
    if (g_api.set_body_pose)
    {
        g_api.set_body_pose(corpse->world, centers, quats);
        return;
    }
    // Fallback for a DLL without the pose export: the per body teleport.
    for (int i = 0; i < OC2_IVP_BODIES; ++i)
        g_api.set_body_transform(corpse->world, i, centers + i * 3);
}

int IvpCorpseStep(IvpCorpse *corpse, float dt)
{
    if (!corpse || !g_ready || !corpse->world) return -1;
    const int result = g_api.step(corpse->world, dt);
    if (result != 0 && !g_api.health(corpse->world)) return -1;
    return result;
}

int IvpCorpseReadJoints(const IvpCorpse *corpse, float *joints16, float *quats12)
{
    if (!corpse || !g_ready || !corpse->world) return 0;
    const int count = g_api.body_count(corpse->world);
    if (count != OC2_IVP_BODIES) return 0;
    float scratch[OC2_IVP_BODIES * 7];
    for (int i = 0; i < OC2_IVP_BODIES; ++i)
    {
        if (g_api.get_body_transform(corpse->world, i, scratch + i * 7) != 0) return 0;
        if (quats12)
        {
            for (int k = 0; k < 4; ++k) quats12[i * 4 + k] = scratch[i * 7 + 3 + k];
        }
    }
    if (!joints16) return OC2_IVP_JOINTS;
    for (int i = 0; i < OC2_IVP_BODIES; ++i)
    {
        const float *entry = scratch + i * 7;
        float axis[3];
        RotateByQuat(entry + 3, corpse->axis[i], axis);
        const float half = corpse->half[i];
        const int pivot = kBodyPivot[i];
        const int tip = kBodyTip[i];
        for (int k = 0; k < 3; ++k)
            joints16[pivot * 3 + k] = entry[k] - axis[k] * half;
        if (tip >= 0)
        {
            for (int k = 0; k < 3; ++k)
                joints16[tip * 3 + k] = entry[k] + axis[k] * half;
        }
    }
    return OC2_IVP_JOINTS;
}

void IvpCorpseSetGround(IvpCorpse *corpse, float groundZ)
{
    if (!corpse || !g_ready || !corpse->world) return;
    g_api.set_ground_y(corpse->world, groundZ);
}

int IvpCorpseSetBody(IvpCorpse *corpse, int body, const float pos[3])
{
    if (!corpse || !g_ready || !corpse->world || !pos) return -1;
    return g_api.set_body_transform(corpse->world, body, pos);
}

int IvpCorpseReadBodies(const IvpCorpse *corpse, float *centres12)
{
    if (!corpse || !g_ready || !corpse->world || !centres12) return 0;
    if (g_api.body_count(corpse->world) != OC2_IVP_BODIES) return 0;
    float scratch[OC2_IVP_BODIES * 7];
    for (int i = 0; i < OC2_IVP_BODIES; ++i)
    {
        if (g_api.get_body_transform(corpse->world, i, scratch + i * 7) != 0) return 0;
        for (int k = 0; k < 3; ++k) centres12[i * 3 + k] = scratch[i * 7 + k];
    }
    return OC2_IVP_BODIES;
}

float IvpCorpseBodyRadius(const IvpCorpse *corpse, int body)
{
    if (!corpse || !g_ready || !corpse->world) return 0.0f;
    if (body < 0 || body >= OC2_IVP_BODIES) return 0.0f;
    return g_api.get_body_radius(corpse->world, body);
}

void IvpCorpseDestroy(IvpCorpse *corpse)
{
    if (!corpse) return;
    if (corpse->world && g_ready) g_api.destroy(corpse->world);
    delete corpse;
}
