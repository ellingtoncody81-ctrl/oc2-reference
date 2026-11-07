// oc2_bullet_smoke.cpp
//
// OC2 ragdoll (BulletPhysics) - batch 1 smoke test.
//
// Purpose: only prove that the x64 Bullet3 static libraries can be linked into
// client_amd64.dll (CRT consistency, symbol conflicts and static-lib linking
// all work). There is no physics logic here; batch 3 replaces this file with
// the dphys_* adapter layer.
//
// The objects below are never used at runtime - they only make the linker pull
// Bullet object files out of the static libraries.

#include "btBulletDynamicsCommon.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"
#include "LinearMath/btDefaultMotionState.h"
#include "BulletDynamics/Dynamics/btRigidBody.h"

namespace
{

// Forces the linker to pull Bullet objects out of the static libraries.
btSphereShape s_smokeSphere(1.0f);
btTransform s_smokeTransform;
btDefaultMotionState s_smokeMotionState;
btRigidBody s_smokeBody(1.0f, &s_smokeMotionState, &s_smokeSphere);

} // namespace

extern "C" int OC2_BulletSmokeCheck()
{
    s_smokeTransform.setIdentity();
    s_smokeTransform.setOrigin(btVector3(0.0f, 0.0f, 0.0f));
    s_smokeBody.setWorldTransform(s_smokeTransform);
    return (int)s_smokeSphere.getRadius();
}
