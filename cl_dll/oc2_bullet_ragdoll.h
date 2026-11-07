#pragma once
/*
 * Bullet3 backend for the death ragdoll.
 *
 * The solver, the rigid body layout and the constraint layout follow the
 * upstream MetaHookSv/BulletPhysics implementation:
 *
 *   - one rigid body per configured bone, each carrying a capsule/box/sphere
 *     and a motion state that keeps the body attached to its studio bone;
 *   - one joint per configured constraint (cone twist, hinge, point, dof6,
 *     fixed);
 *   - the world runs in the client space: inches, Z up, gravity on -Z.
 *
 * The upstream GoldSrc to Bullet conversion is the identity here, because the
 * upstream build has its metre to inch patch applied (INCHES_PER_METER is 1),
 * so no axis or scale conversion is needed.
 */

#include "oc2_bullet_cfg.h"

/*
 * The studio header is passed as an opaque pointer: the engine header cannot
 * be included from here without dragging in its own type dependencies, and a
 * forward declaration of the tag would not stay compatible with the typedef
 * the engine uses.
 */

class CStudioModelRenderer;

namespace oc2_bullet
{

/* Create the world on demand. Safe to call more than once. */
bool Init();
void Shutdown();
bool Available();
const char *Status();

/*
 * Build one corpse. baseMatrix holds the world space bone matrices of the
 * death pose; the rigid bodies inherit them so the corpse starts exactly in
 * the pose the animation left behind.
 *
 * impact is the direction and strength of the hit that killed the player, in
 * world space. launch is the velocity the player carried when they died, and
 * contact is the point the hit landed on. Together they decide which way the
 * body falls; without them every corpse would collapse straight down.
 */
bool CreateRagdoll(unsigned key, const char *modelPath, const void *header,
	const float baseMatrix[][3][4], int boneCount,
	const float impact[3], const float launch[3], const float contact[3],
	bool headshot);
void DestroyRagdoll(unsigned key);
bool HasRagdoll(unsigned key);

/* Advance every corpse. dt is the frame time in seconds, now the client time. */
void Step(float dt, double now);

/* Write the simulated bone matrices back into the renderer. */
void ApplyBones(CStudioModelRenderer &renderer, unsigned key);

int ActiveCount();

} // namespace oc2_bullet
