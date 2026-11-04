/* oc2_ivp.dll - the exported C ABI (x64, C linkage, no C++ types).
 *
 * COORDINATES: the DLL works directly in the GoldSrc model space: inches,
 * Z UP, right handed. Our client world is the same space, so NO axis or unit
 * conversion happens anywhere on this boundary.
 *
 * The 12 bodies are the CSSO style segments, in this order:
 *   0 pelvis->waist   1 waist->chest    2 chest->head     3 head
 *   4 L_shoulder->L_elbow  5 L_elbow->L_hand
 *   6 R_shoulder->R_elbow  7 R_elbow->R_hand
 *   8 L_hip->L_knee   9 L_knee->L_foot
 *  10 R_hip->R_knee  11 R_knee->R_foot
 * The 16 joint positions are recovered from those 12 transforms.
 */
#ifndef OC2_IVP_H
#define OC2_IVP_H
#ifdef __cplusplus
extern "C" {
#endif

#define OC2_IVP_BODIES 12
#define OC2_IVP_JOINTS 16

typedef struct {
    float mass, rotInertiaLimit, damping, rotDamping, radius, halfLength;
    float bindPos[3];
    float bindQuat[4];
    float axisLocal[3];
} Oc2IvpBodyDesc;

typedef struct {
    int parentBody, childBody;
    float minDeg[3], maxDeg[3], frictionTorque[3], targetDeg[3];
    float toAttached[16];
} Oc2IvpJointDesc;

/* --- the shipped ABI --- */
const char *oc2_ivp_version(void);
void       *oc2_ivp_create(int enable_limits, float gravity_z);
void        oc2_ivp_destroy(void *world);
int         oc2_ivp_step(void *world, float dt);
int         oc2_ivp_body_count(void *world);
int         oc2_ivp_get_body_transform(void *world, int body, float out7[7]);
float       oc2_ivp_get_body_radius(void *world, int body);
int         oc2_ivp_health(void *world);
int         oc2_ivp_set_ground_y(void *world, float y);
int         oc2_ivp_set_limits(void *world, int on);
int         oc2_ivp_set_body_transform(void *world, int body, const float pos3[3]);

/* --- optional extension, used when the DLL exports it ---
 * Sets all 12 body poses at once and rebuilds once, instead of the per body
 * teleport that rebuilds the environment on every call. */
int         oc2_ivp_set_body_pose(void *world, const float pos12[36], const float quat12[48]);

#ifdef __cplusplus
}
#endif
#endif
