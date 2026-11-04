#pragma once
/* Client side adapter for oc2_ivp.dll.
 *
 * Coordinates are the client world space: inches, Z UP. The DLL uses the same
 * space, so nothing is converted here.
 */

struct IvpCorpse;

void IvpAdapterInit();
bool IvpAdapterAvailable();
const char *IvpAdapterStatus();
void IvpAdapterShutdown();

/* One corpse world. groundZ is the BSP floor height under the corpse. */
IvpCorpse *IvpCorpseCreate(float groundZ);

/* Optional: pose the world from the 16 joint positions (48 floats, model
 * space). The adapter derives the 12 body centres and body rotations and
 * applies them in one batch, so the DLL rebuilds the scene only once. */
void IvpCorpseSetJoints(IvpCorpse *corpse, const float *joints16);

/* One fixed step. */
int IvpCorpseStep(IvpCorpse *corpse, float dt);

/* The 16 joint positions (48 floats) in the product joint order, and
 * optionally the 12 body quaternions (48 floats). */
int IvpCorpseReadJoints(const IvpCorpse *corpse, float *joints16, float *quats12);

/* The BSP floor height for the DLL kinematic clamp (no rebuild). */
void IvpCorpseSetGround(IvpCorpse *corpse, float groundZ);

/* Correct one body position. The DLL teleports the body (a beam teleport, no
 * rebuild), so this is safe to call every step. */
int IvpCorpseSetBody(IvpCorpse *corpse, int body, const float pos[3]);

/* The 12 body centres (36 floats, model space) and the body radii, for the
 * BSP contact pass. */
int IvpCorpseReadBodies(const IvpCorpse *corpse, float *centres12);
float IvpCorpseBodyRadius(const IvpCorpse *corpse, int body);

void IvpCorpseDestroy(IvpCorpse *corpse);
