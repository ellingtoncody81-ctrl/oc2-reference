/*
 * Bullet3 backend for the death ragdoll.
 *
 * The rigid body, constraint and motion state layout follows the upstream
 * MetaHookSv/BulletPhysics implementation, copied where the behaviour matters:
 *
 *   - CBoneMotionState keeps a rigid body attached to its studio bone through
 *     the pair (bone matrix, body offset);
 *   - the configuration supplies the body origin/angles as an offset in bone
 *     local space and the joint origin/angles as an offset in body space;
 *   - angles in the configuration are degrees, the joint limit factors are
 *     radians.
 *
 * The upstream GoldSrc to Bullet conversion is the identity here (the
 * upstream build has its metre to inch patch applied), so the world runs in
 * the client space: inches, Z up, gravity on -Z.
 */

#include <btBulletDynamicsCommon.h>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>

#include "hud.h"
#include "studio.h"
#include "r_studioint.h"
#include "StudioModelRenderer.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "event_api.h"
#include "include/studio/studio_util.h"

#include "oc2_bullet_ragdoll.h"

#include <math.h>
#include <string.h>

namespace
{

const float kFixedTimeStep = 1.0f / 60.0f;
const int kMaxSubSteps = 4;
const int kMaxRagdolls = 64;
const float kGravity = 386.0f;	/* inches per second squared, Z up */
const float kPi = 3.14159265358979323846f;

/* ------------------------------------------------------------------ */
/* Matrix helpers                                                      */
/* ------------------------------------------------------------------ */

void Matrix4x4Transpose(float out[4][4], const float in[4][4])
{
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			out[i][j] = in[j][i];
}

/* Upstream BulletPhysicManager.cpp:134 - row vector 3x4 to column vector transform. */
void Matrix3x4ToTransform(const float matrix3x4[3][4], btTransform &trans)
{
	float matrix4x4[4][4] = {
		{ 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f } };
	memcpy(matrix4x4, matrix3x4, sizeof(float[3][4]));

	float transposed[4][4];
	Matrix4x4Transpose(transposed, matrix4x4);

	trans.setFromOpenGLMatrix((float *)transposed);
}

/* Upstream BulletPhysicManager.cpp:149 */
void TransformToMatrix3x4(const btTransform &trans, float matrix3x4[3][4])
{
	float transposed[4][4];
	trans.getOpenGLMatrix((float *)transposed);

	float matrix4x4[4][4];
	Matrix4x4Transpose(matrix4x4, transposed);

	memcpy(matrix3x4, matrix4x4, sizeof(float[3][4]));
}

/* Upstream BulletPhysicManager.cpp:91 - the configuration stores degrees. */
void EulerMatrix(const btVector3 &in_euler, btMatrix3x3 &out_matrix)
{
	const btVector3 angles = in_euler * (btScalar)(kPi / 180.0f);

	const btScalar c1(btCos(angles[0]));
	const btScalar c2(btCos(angles[1]));
	const btScalar c3(btCos(angles[2]));
	const btScalar s1(btSin(angles[0]));
	const btScalar s2(btSin(angles[1]));
	const btScalar s3(btSin(angles[2]));

	out_matrix.setValue(c1 * c2, -c3 * s2 - s1 * s3 * c2, s3 * s2 - s1 * c3 * c2,
		c1 * s2, c3 * c2 - s1 * s3 * s2, -s3 * c2 - s1 * c3 * s2,
		s1, c1 * s3, c1 * c3);
}

/* ------------------------------------------------------------------ */
/* Motion state - upstream BulletPhysicManager.h:86                     */
/* ------------------------------------------------------------------ */

class CBoneMotionState : public btMotionState
{
public:
	CBoneMotionState(const btTransform &bone, const btTransform &offset)
		: m_bonematrix(bone), m_offsetmatrix(offset)
	{
	}

	void getWorldTransform(btTransform &worldTrans) const override
	{
		worldTrans.mult(m_bonematrix, m_offsetmatrix);
	}

	void setWorldTransform(const btTransform &worldTrans) override
	{
		m_bonematrix.mult(worldTrans, m_offsetmatrix.inverse());
	}

	btTransform m_bonematrix;
	btTransform m_offsetmatrix;
};

/* ------------------------------------------------------------------ */
/* Ragdoll state                                                       */
/* ------------------------------------------------------------------ */

struct Ragdoll
{
	unsigned key = 0;
	bool valid = false;
	int boneCount = 0;

	/* The collision filter bit this corpse and its floor share. */
	int groupBit = -1;

	std::vector<btRigidBody *> bodies;
	std::vector<CBoneMotionState *> motionStates;
	std::vector<btCollisionShape *> shapes;
	std::vector<btTypedConstraint *> constraints;
	std::vector<bool> constraintDisableCollision;
	std::vector<int> bodyBone;
	std::vector<float> bodyRadius;

	/* World space relation of every bone to its parent, used to carry the
	 * bones that no rigid body drives along with their parent. */
	float boneRelative[MAXSTUDIOBONES][3][4];
	bool boneDriven[MAXSTUDIOBONES];

	/* Cached floor height, refreshed by an engine trace a few times a second. */
	float groundZ = 0.0f;
	bool groundValid = false;
	double groundTime = 0.0;

	/* The floor the corpse rests on. Bullet resolves the contact itself, so
	 * the joint solver is not interrupted by an outside correction. */
	btRigidBody *ground = NULL;
	btCollisionShape *groundShape = NULL;
};

Ragdoll s_ragdolls[kMaxRagdolls];

btDefaultCollisionConfiguration *s_config = NULL;
btCollisionDispatcher *s_dispatcher = NULL;
btDbvtBroadphase *s_broadphase = NULL;
btSequentialImpulseConstraintSolver *s_solver = NULL;
btDiscreteDynamicsWorld *s_world = NULL;
char s_status[256] = "not initialised";

Ragdoll *FindRagdoll(unsigned key)
{
	for (int i = 0; i < kMaxRagdolls; ++i)
		if (s_ragdolls[i].valid && s_ragdolls[i].key == key)
			return &s_ragdolls[i];
	return NULL;
}

/* Bullet's collision filter has 32 bits. Every corpse takes one of them, so
 * that the filter can tell "a limb of this corpse" from "a limb of that
 * corpse". Bit 31 is left out, because the filter value is a signed int and
 * a negative mask confuses the broad phase. */
const int kGroupBitCount = 31;

int AllocGroupBit()
{
	for (int bit = 0; bit < kGroupBitCount; ++bit)
	{
		bool used = false;
		for (int i = 0; i < kMaxRagdolls; ++i)
			if (s_ragdolls[i].valid && s_ragdolls[i].groupBit == bit)
			{
				used = true;
				break;
			}
		if (!used)
			return bit;
	}
	return -1;
}

btCollisionShape *CreateShape(const BulletRigidBodyConfig &config)
{
	switch (config.shapeType)
	{
	case BulletShape_Box:
	{
		const btVector3 halfExtents(config.size[0], config.size[1], config.size[2]);
		return new btBoxShape(halfExtents);
	}
	case BulletShape_Sphere:
		return new btSphereShape(config.size[0]);
	case BulletShape_Capsule:
	{
		const btScalar radius = config.size[0];
		const btScalar height = config.size[1];
		if (config.shapeDirection == BulletShapeAxis_X)
			return new btCapsuleShapeX(radius, height);
		if (config.shapeDirection == BulletShapeAxis_Z)
			return new btCapsuleShapeZ(radius, height);
		return new btCapsuleShape(radius, height);
	}
	case BulletShape_Cylinder:
	{
		const btVector3 halfExtents(config.size[0], config.size[1], config.size[2]);
		if (config.shapeDirection == BulletShapeAxis_X)
			return new btCylinderShapeX(halfExtents);
		if (config.shapeDirection == BulletShapeAxis_Z)
			return new btCylinderShapeZ(halfExtents);
		return new btCylinderShape(halfExtents);
	}
	default:
		return NULL;
	}
}

float ShapeRadius(const BulletRigidBodyConfig &config)
{
	switch (config.shapeType)
	{
	case BulletShape_Sphere:
		return config.size[0];
	case BulletShape_Capsule:
		return config.size[0];
	case BulletShape_Box:
		return config.size[0] < config.size[1]
			? (config.size[0] < config.size[2] ? config.size[0] : config.size[2])
			: (config.size[1] < config.size[2] ? config.size[1] : config.size[2]);
	case BulletShape_Cylinder:
		return config.size[0] < config.size[1] ? config.size[0] : config.size[1];
	default:
		return 0.0f;
	}
}

/* Upstream BulletPhysicManager.cpp:360-389 hands the error reduction and the
 * constraint force mixing factors of the constraint to the solver. Every joint
 * type uses a different set of solver axes, so the mapping is done per type:
 *
 *   ConeTwist  linear ERP/CFM on the axes 0,1,2 - angular ERP/CFM on 3,4
 *   Hinge      angular ERP/CFM and the stop factors on the axis 5
 *   Slider     linear CFM and the stop factors on the axis 0 - angular CFM
 *              and the stop factors on the axis 3
 *   Point      angular ERP/CFM on the axis -1, which covers the whole joint
 */
void ApplyConeTwistSolverParams(btConeTwistConstraint *constraint,
	const BulletConstraintConfig &config)
{
	if (config.linearERP != 0.0f)
	{
		constraint->setParam(BT_CONSTRAINT_ERP, config.linearERP, 0);
		constraint->setParam(BT_CONSTRAINT_ERP, config.linearERP, 1);
		constraint->setParam(BT_CONSTRAINT_ERP, config.linearERP, 2);
	}
	if (config.linearCFM != 0.0f)
	{
		constraint->setParam(BT_CONSTRAINT_CFM, config.linearCFM, 0);
		constraint->setParam(BT_CONSTRAINT_CFM, config.linearCFM, 1);
		constraint->setParam(BT_CONSTRAINT_CFM, config.linearCFM, 2);
	}
	if (config.angularERP != 0.0f)
	{
		constraint->setParam(BT_CONSTRAINT_ERP, config.angularERP, 3);
		constraint->setParam(BT_CONSTRAINT_ERP, config.angularERP, 4);
	}
	if (config.angularCFM != 0.0f)
	{
		constraint->setParam(BT_CONSTRAINT_CFM, config.angularCFM, 3);
		constraint->setParam(BT_CONSTRAINT_CFM, config.angularCFM, 4);
	}
}

void ApplyHingeSolverParams(btHingeConstraint *constraint,
	const BulletConstraintConfig &config)
{
	if (config.angularERP != 0.0f)
		constraint->setParam(BT_CONSTRAINT_ERP, config.angularERP, 5);
	if (config.angularCFM != 0.0f)
		constraint->setParam(BT_CONSTRAINT_CFM, config.angularCFM, 5);
	if (config.angularStopERP != 0.0f)
		constraint->setParam(BT_CONSTRAINT_STOP_ERP, config.angularStopERP, 5);
	if (config.angularStopCFM != 0.0f)
		constraint->setParam(BT_CONSTRAINT_STOP_CFM, config.angularStopCFM, 5);
}

void ApplySliderSolverParams(btSliderConstraint *constraint,
	const BulletConstraintConfig &config)
{
	if (config.linearCFM != 0.0f)
		constraint->setParam(BT_CONSTRAINT_CFM, config.linearCFM, 0);
	if (config.linearStopERP != 0.0f)
		constraint->setParam(BT_CONSTRAINT_STOP_ERP, config.linearStopERP, 0);
	if (config.linearStopCFM != 0.0f)
		constraint->setParam(BT_CONSTRAINT_STOP_CFM, config.linearStopCFM, 0);
	if (config.angularCFM != 0.0f)
		constraint->setParam(BT_CONSTRAINT_CFM, config.angularCFM, 3);
	if (config.angularStopERP != 0.0f)
		constraint->setParam(BT_CONSTRAINT_STOP_ERP, config.angularStopERP, 3);
	if (config.angularStopCFM != 0.0f)
		constraint->setParam(BT_CONSTRAINT_STOP_CFM, config.angularStopCFM, 3);
}

void ApplyPointSolverParams(btPoint2PointConstraint *constraint,
	const BulletConstraintConfig &config)
{
	if (config.angularERP != 0.0f)
		constraint->setParam(BT_CONSTRAINT_ERP, config.angularERP, -1);
	if (config.angularCFM != 0.0f)
		constraint->setParam(BT_CONSTRAINT_CFM, config.angularCFM, -1);
}

/* Upstream BulletRagdollObject.cpp:242, new configuration path only. */
btTypedConstraint *CreateConstraint(const BulletConstraintConfig &config,
	btRigidBody *bodyA, btRigidBody *bodyB)
{
	const btTransform worldTransA = bodyA->getWorldTransform();
	const btTransform worldTransB = bodyB->getWorldTransform();
	const btTransform invWorldTransA = worldTransA.inverse();
	const btTransform invWorldTransB = worldTransB.inverse();

	btTransform localTransA;
	localTransA.setIdentity();
	localTransA.setOrigin(btVector3(config.originA[0], config.originA[1], config.originA[2]));
	btVector3 anglesA(config.anglesA[0], config.anglesA[1], config.anglesA[2]);
	EulerMatrix(anglesA, localTransA.getBasis());

	btTransform localTransB;
	localTransB.setIdentity();
	localTransB.setOrigin(btVector3(config.originB[0], config.originB[1], config.originB[2]));
	btVector3 anglesB(config.anglesB[0], config.anglesB[1], config.anglesB[2]);
	EulerMatrix(anglesB, localTransB.getBasis());

	const btTransform globalJointA = worldTransA * localTransA;
	const btTransform globalJointB = worldTransB * localTransB;

	/* The global joint is shared by both bodies; the local frames follow from
	 * it, exactly like upstream BulletCreateConstraintFromGlobalJointTransform. */
	const btTransform &globalJoint = config.useGlobalJointFromA ? globalJointA : globalJointB;
	const btTransform finalLocalTransA = invWorldTransA * globalJoint;
	const btTransform finalLocalTransB = invWorldTransB * globalJoint;

	switch (config.type)
	{
	case BulletConstraint_ConeTwist:
	{
		btConeTwistConstraint *constraint = new btConeTwistConstraint(
			*bodyA, *bodyB, finalLocalTransA, finalLocalTransB);
		/* The configuration stores the limits as a fraction of pi, exactly like
		 * upstream BulletPhysicManager.cpp:358. The solver values are the
		 * upstream defaults from ClientPhysicCommon.h:8-10. */
		constraint->setLimit(config.coneSwingLimit1 * kPi, config.coneSwingLimit2 * kPi,
			config.coneTwistLimit * kPi, config.coneSoftness, config.coneBiasFactor,
			config.coneRelaxationFactor);
		ApplyConeTwistSolverParams(constraint, config);
		return constraint;
	}
	case BulletConstraint_Hinge:
	{
		btHingeConstraint *constraint = new btHingeConstraint(
			*bodyA, *bodyB, finalLocalTransA, finalLocalTransB);
		/* Upstream BulletPhysicManager.cpp:404 scales the limits by pi too. */
		constraint->setLimit(config.hingeLowLimit * kPi, config.hingeHighLimit * kPi,
			config.hingeSoftness, config.hingeBiasFactor, config.hingeRelaxationFactor);
		ApplyHingeSolverParams(constraint, config);
		return constraint;
	}
	case BulletConstraint_Slider:
	{
		btSliderConstraint *constraint = new btSliderConstraint(
			*bodyA, *bodyB, finalLocalTransA, finalLocalTransB, config.useLinearReferenceFrameA);
		constraint->setLowerLinLimit(config.sliderLowerLinearLimit);
		constraint->setUpperLinLimit(config.sliderUpperLinearLimit);
		constraint->setLowerAngLimit(config.sliderLowerAngularLimit * kPi);
		constraint->setUpperAngLimit(config.sliderUpperAngularLimit * kPi);
		ApplySliderSolverParams(constraint, config);
		return constraint;
	}
	case BulletConstraint_Point:
	{
		btPoint2PointConstraint *constraint = new btPoint2PointConstraint(
			*bodyA, *bodyB, finalLocalTransA.getOrigin(), finalLocalTransB.getOrigin());
		ApplyPointSolverParams(constraint, config);
		return constraint;
	}
	case BulletConstraint_Dof6:
	case BulletConstraint_Dof6Spring:
	{
		btGeneric6DofConstraint *constraint = new btGeneric6DofConstraint(
			*bodyA, *bodyB, finalLocalTransA, finalLocalTransB, config.useLinearReferenceFrameA);
		/* Upstream BulletPhysicManager.cpp:500-535 reads the limits from the
		 * configuration. The angular limits are stored as a fraction of pi. */
		constraint->setLinearLowerLimit(btVector3(config.dof6LowerLinearLimit[0],
			config.dof6LowerLinearLimit[1], config.dof6LowerLinearLimit[2]));
		constraint->setLinearUpperLimit(btVector3(config.dof6UpperLinearLimit[0],
			config.dof6UpperLinearLimit[1], config.dof6UpperLinearLimit[2]));
		constraint->setAngularLowerLimit(btVector3(
			config.dof6LowerAngularLimit[0] * kPi,
			config.dof6LowerAngularLimit[1] * kPi,
			config.dof6LowerAngularLimit[2] * kPi));
		constraint->setAngularUpperLimit(btVector3(
			config.dof6UpperAngularLimit[0] * kPi,
			config.dof6UpperAngularLimit[1] * kPi,
			config.dof6UpperAngularLimit[2] * kPi));
		return constraint;
	}
	case BulletConstraint_Fixed:
	{
		return new btFixedConstraint(*bodyA, *bodyB, finalLocalTransA, finalLocalTransB);
	}
	default:
		return NULL;
	}
}

void TraceWorld(const float start[3], const float end[3], pmtrace_t &trace)
{
	float traceStart[3] = { start[0], start[1], start[2] };
	float traceEnd[3] = { end[0], end[1], end[2] };
	gEngfuncs.pEventAPI->EV_SetTraceHull(2);
	gEngfuncs.pEventAPI->EV_PlayerTrace(traceStart, traceEnd,
		PM_STUDIO_BOX | PM_WORLD_ONLY, -1, &trace);
}

} // namespace

namespace oc2_bullet
{

bool Init()
{
	if (s_world)
		return true;

	s_config = new btDefaultCollisionConfiguration();
	s_dispatcher = new btCollisionDispatcher(s_config);
	s_broadphase = new btDbvtBroadphase();
	s_solver = new btSequentialImpulseConstraintSolver();
	s_world = new btDiscreteDynamicsWorld(s_dispatcher, s_broadphase, s_solver, s_config);
	s_world->setGravity(btVector3(0.0f, 0.0f, -kGravity));

	snprintf(s_status, sizeof(s_status), "bullet ready");
	return true;
}

void Shutdown()
{
	for (int i = 0; i < kMaxRagdolls; ++i)
		DestroyRagdoll(s_ragdolls[i].key);

	if (s_world)
	{
		delete s_world;
		s_world = NULL;
	}
	if (s_solver)
	{
		delete s_solver;
		s_solver = NULL;
	}
	if (s_broadphase)
	{
		delete s_broadphase;
		s_broadphase = NULL;
	}
	if (s_dispatcher)
	{
		delete s_dispatcher;
		s_dispatcher = NULL;
	}
	if (s_config)
	{
		delete s_config;
		s_config = NULL;
	}
	snprintf(s_status, sizeof(s_status), "bullet shut down");
}

bool Available()
{
	return s_world != NULL;
}

const char *Status()
{
	return s_status;
}

bool CreateRagdoll(unsigned key, const char *modelPath, const void *header,
	const float baseMatrix[][3][4], int boneCount,
	const float impact[3], const float launch[3], const float contact[3],
	bool headshot)
{
	const studiohdr_t *studioHeader = static_cast<const studiohdr_t *>(header);
	if (!s_world || !studioHeader || !baseMatrix || boneCount <= 0 || boneCount > MAXSTUDIOBONES)
		return false;

	DestroyRagdoll(key);

	Ragdoll *slot = NULL;
	for (int i = 0; i < kMaxRagdolls; ++i)
		if (!s_ragdolls[i].valid)
		{
			slot = &s_ragdolls[i];
			break;
		}
	if (!slot)
		return false;

	/* Every corpse gets its own collision filter bit before anything is
	 * built, so a full filter is reported without leaking a half built
	 * rig. */
	const int groupBit = AllocGroupBit();
	if (groupBit < 0)
	{
		snprintf(s_status, sizeof(s_status), "no collision filter bit left for a new corpse");
		if (gEngfuncs.Con_Printf)
			gEngfuncs.Con_Printf("ragdoll_bullet: %s, the corpse gets no physics\n", s_status);
		return false;
	}
	const int group = 1 << groupBit;

	BulletRagdollConfig config;
	const std::string cfgPath = BulletCfgPathForModel(modelPath);
	int length = 0;
	byte *data = gEngfuncs.COM_LoadFile(const_cast<char *>(cfgPath.c_str()), 5, &length);
	if (!data)
	{
		snprintf(s_status, sizeof(s_status), "no physics config for %s", cfgPath.c_str());
		return false;
	}
	const bool parsed = BulletCfgParseRagdoll((const char *)data, (size_t)length, config);
	gEngfuncs.COM_FreeFile(data);
	if (!parsed || !config.valid)
	{
		snprintf(s_status, sizeof(s_status), "physics config %s is not usable", cfgPath.c_str());
		return false;
	}

	slot->key = key;
	slot->boneCount = boneCount;
	slot->groupBit = groupBit;
	memset(slot->boneDriven, 0, sizeof(slot->boneDriven));

	/* Rigid bodies. The configuration index and the runtime index can differ:
	 * a body whose bone index does not fit this model, or whose shape is not
	 * supported, is dropped here. The map keeps the constraint references
	 * pointing at the right body. */
	std::vector<int> configToRuntime(config.rigidBodies.size(), -1);
	for (size_t i = 0; i < config.rigidBodies.size(); ++i)
	{
		const BulletRigidBodyConfig &bodyConfig = config.rigidBodies[i];
		if (bodyConfig.boneIndex < 0 || bodyConfig.boneIndex >= boneCount)
		{
			if (gEngfuncs.Con_Printf)
				gEngfuncs.Con_Printf("ragdoll_bullet: body %s has bone index %d outside 0..%d, skipped\n",
					bodyConfig.name.c_str(), bodyConfig.boneIndex, boneCount - 1);
			continue;
		}

		btCollisionShape *shape = CreateShape(bodyConfig);
		if (!shape)
		{
			if (gEngfuncs.Con_Printf)
				gEngfuncs.Con_Printf("ragdoll_bullet: body %s has an unsupported shape, skipped\n",
					bodyConfig.name.c_str());
			continue;
		}

		configToRuntime[i] = (int)slot->bodies.size();

		btTransform localTrans;
		localTrans.setIdentity();
		localTrans.setOrigin(btVector3(
			bodyConfig.origin[0], bodyConfig.origin[1], bodyConfig.origin[2]));
		btVector3 angles(bodyConfig.angles[0], bodyConfig.angles[1], bodyConfig.angles[2]);
		EulerMatrix(angles, localTrans.getBasis());

		btTransform boneMatrix;
		Matrix3x4ToTransform(baseMatrix[bodyConfig.boneIndex], boneMatrix);

		CBoneMotionState *motionState = new CBoneMotionState(boneMatrix, localTrans);

		btVector3 inertia(0.0f, 0.0f, 0.0f);
		if (bodyConfig.mass > 0.0f)
			shape->calculateLocalInertia(bodyConfig.mass, inertia);

		btRigidBody::btRigidBodyConstructionInfo info(bodyConfig.mass, motionState, shape, inertia);
		info.m_friction = bodyConfig.linearFriction;
		info.m_rollingFriction = bodyConfig.rollingFriction;
		info.m_restitution = bodyConfig.restitution;
		/* Upstream BulletRagdollObject.cpp:203-210 uses the additional damping
		 * model instead of the plain linear and angular damping. */
		info.m_linearDamping = 0.0f;
		info.m_angularDamping = 0.0f;
		info.m_additionalDamping = true;
		info.m_additionalDampingFactor = bodyConfig.additionalDampingFactor;
		info.m_additionalLinearDampingThresholdSqr = bodyConfig.additionalLinearDampingThresholdSqr;
		info.m_additionalAngularDampingThresholdSqr = bodyConfig.additionalAngularDampingThresholdSqr;

		btRigidBody *body = new btRigidBody(info);
		body->setSleepingThresholds(bodyConfig.linearSleepingThreshold,
			bodyConfig.angularSleepingThreshold);
		/* Swept sphere collision detection, like upstream
		 * BulletPhysicRigidBody.cpp:31-38. Without it a fast limb tunnels
		 * through the ground. */
		body->setCcdSweptSphereRadius(bodyConfig.ccdRadius);
		body->setCcdMotionThreshold(bodyConfig.ccdThreshold);

		slot->bodies.push_back(body);
		slot->motionStates.push_back(motionState);
		slot->shapes.push_back(shape);
		slot->bodyBone.push_back(bodyConfig.boneIndex);
		slot->bodyRadius.push_back(ShapeRadius(bodyConfig));
		slot->boneDriven[bodyConfig.boneIndex] = true;
	}

	if (slot->bodies.empty())
	{
		snprintf(s_status, sizeof(s_status), "no usable rigid bodies in %s", cfgPath.c_str());
		slot->valid = false;
		return false;
	}

	/* Constraints. */
	for (size_t i = 0; i < config.constraints.size(); ++i)
	{
		const BulletConstraintConfig &constraintConfig = config.constraints[i];
		if (constraintConfig.rigidBodyA < 0 || constraintConfig.rigidBodyB < 0)
			continue;
		if (constraintConfig.rigidBodyA >= (int)configToRuntime.size() ||
			constraintConfig.rigidBodyB >= (int)configToRuntime.size())
			continue;
		const int runtimeA = configToRuntime[constraintConfig.rigidBodyA];
		const int runtimeB = configToRuntime[constraintConfig.rigidBodyB];
		if (runtimeA < 0 || runtimeB < 0)
		{
			if (gEngfuncs.Con_Printf)
				gEngfuncs.Con_Printf("ragdoll_bullet: constraint %s refers to a body that was not built, skipped\n",
					constraintConfig.name.c_str());
			continue;
		}
		if (runtimeA == runtimeB)
		{
			/* Upstream BulletRagdollObject.cpp:264-268 rejects this. */
			if (gEngfuncs.Con_Printf)
				gEngfuncs.Con_Printf("ragdoll_bullet: constraint %s connects a body to itself, skipped\n",
					constraintConfig.name.c_str());
			continue;
		}
		btRigidBody *bodyA = slot->bodies[runtimeA];
		btRigidBody *bodyB = slot->bodies[runtimeB];
		if (!bodyA || !bodyB)
			continue;

		btTypedConstraint *constraint = CreateConstraint(constraintConfig, bodyA, bodyB);
		if (!constraint)
			continue;
		slot->constraints.push_back(constraint);
		slot->constraintDisableCollision.push_back(constraintConfig.disableCollision);
	}

	/* Bone relations, so the bones without a rigid body follow their parent. */
	const mstudiobone_t *bones = reinterpret_cast<const mstudiobone_t *>(
		reinterpret_cast<const byte *>(studioHeader) + studioHeader->boneindex);
	for (int bone = 0; bone < boneCount; ++bone)
	{
		const int parent = bones[bone].parent;
		if (parent < 0 || parent >= bone)
		{
			memcpy(slot->boneRelative[bone], baseMatrix[bone], sizeof(float[3][4]));
			continue;
		}
		btTransform boneTransform;
		btTransform parentTransform;
		Matrix3x4ToTransform(baseMatrix[bone], boneTransform);
		Matrix3x4ToTransform(baseMatrix[parent], parentTransform);
		const btTransform relative = parentTransform.inverse() * boneTransform;
		TransformToMatrix3x4(relative, slot->boneRelative[bone]);
	}

	for (size_t i = 0; i < slot->bodies.size(); ++i)
	{
		/* Bullet filters with a 32 bit group and mask, and two objects only
		 * collide when both filters accept the other. Every corpse gets its
		 * own bit and shares it with its own floor, so two corpses never
		 * collide with each other and never land on each other's floor.
		 * The limbs of one corpse still collide with each other, apart from
		 * the pairs the joints already connect. */
		s_world->addRigidBody(slot->bodies[i], group, group);
	}

	/* The floor under this corpse. Its height comes from an engine trace at
	 * the spot where the body fell. */
	{
		const btVector3 origin = slot->bodies[0]->getWorldTransform().getOrigin();
		const float start[3] = { origin.x(), origin.y(), origin.z() + 16.0f };
		const float end[3] = { origin.x(), origin.y(), origin.z() - 128.0f };
		pmtrace_t trace;
		memset(&trace, 0, sizeof(trace));
		TraceWorld(start, end, trace);
		float floorZ = origin.z() - 64.0f;
		if (!trace.startsolid && !trace.allsolid && trace.fraction < 1.0f &&
			trace.plane.normal[2] >= 0.5f)
			floorZ = trace.endpos[2];

		slot->groundShape = new btBoxShape(btVector3(96.0f, 96.0f, 4.0f));
		btTransform groundTransform;
		groundTransform.setIdentity();
		groundTransform.setOrigin(btVector3(origin.x(), origin.y(), floorZ - 4.0f));
		btRigidBody::btRigidBodyConstructionInfo groundInfo(0.0f, NULL, slot->groundShape);
		groundInfo.m_friction = 1.0f;
		groundInfo.m_restitution = 0.0f;
		slot->ground = new btRigidBody(groundInfo);
		slot->ground->setWorldTransform(groundTransform);
		s_world->addRigidBody(slot->ground, group, group);
	}
	for (size_t i = 0; i < slot->constraints.size(); ++i)
	{
		/* Upstream BulletPhysicConstraint.cpp:182 adds the joint with the
		 * collision between the two bodies it joins switched off. */
		s_world->addConstraint(slot->constraints[i], slot->constraintDisableCollision[i]);
	}

	/*
	 * The corpse keeps the speed the victim had when it died. A body that was
	 * running falls differently from one that was standing still, and that is
	 * one of the few things that still tell two corpses apart when the fall
	 * animation is not used.
	 */
	{
		const btVector3 launchVelocity(launch[0], launch[1], launch[2]);
		for (size_t b = 0; b < slot->bodies.size(); ++b)
			slot->bodies[b]->setLinearVelocity(launchVelocity);
		if (gEngfuncs.Con_Printf)
			gEngfuncs.Con_Printf("ragdoll_bullet: corpse %u keeps %.0f %.0f %.0f inch/s\n",
				key, launch[0], launch[1], launch[2]);
	}

	/*
	 * The impulse of the shot that killed the player.
	 *
	 * The body starts from the pose the death animation left behind, so the
	 * fall direction is already in the pose. What the shot adds is the
	 * impulse at the point it landed: that part of the body is pushed away
	 * from the shooter, and the lever arm from the centre of mass to that
	 * point turns the rest of the body around it.
	 *
	 * The whole body is deliberately not pushed as one block. A body that
	 * keeps its shape all the way down always lands in the same pose, no
	 * matter where it was hit.
	 */
	{
		btVector3 shotDir(impact[0], impact[1], impact[2]);
		const float shotStrength = shotDir.length();
		if (shotStrength > 0.01f)
			shotDir /= shotStrength;

		/* The body that was hit: the one whose origin is closest to the
		 * point the bullet landed on. */
		int hitBody = -1;
		float bestDistance = 1.0e30f;
		for (size_t b = 0; b < slot->bodies.size(); ++b)
		{
			const btVector3 origin = slot->bodies[b]->getWorldTransform().getOrigin();
			const btVector3 delta = origin - btVector3(contact[0], contact[1], contact[2]);
			const float distance = delta.length();
			if (distance < bestDistance)
			{
				bestDistance = distance;
				hitBody = (int)b;
			}
		}

		if (hitBody >= 0 && shotStrength > 0.01f)
		{
			btRigidBody *body = slot->bodies[hitBody];
			const btVector3 bodyOrigin = body->getWorldTransform().getOrigin();
			/* The lever arm from the centre of the hit body to the point the
			 * bullet landed on. It keeps its vertical part: dropping it is
			 * what made every corpse fall the same way. */
			const btVector3 lever(contact[0] - bodyOrigin.x(), contact[1] - bodyOrigin.y(),
				contact[2] - bodyOrigin.z());
			const float mass = body->getInvMass() > 0.0f ? 1.0f / body->getInvMass() : 1.0f;
			/* The scale turns the strength of the shot into a push of about
			 * twenty inches per second on the hit body. */
			const btVector3 impulse = shotDir * (shotStrength * mass * 0.2f);
			body->applyImpulse(impulse, lever);
			if (gEngfuncs.Con_Printf)
				gEngfuncs.Con_Printf("ragdoll_bullet: corpse %u takes the hit on body %d, push %.1f in/s\n",
					key, hitBody, impulse.length() / mass);
		}
	}
	(void)headshot;

	slot->valid = true;
	snprintf(s_status, sizeof(s_status), "%u bodies, %u constraints",
		(unsigned)slot->bodies.size(), (unsigned)slot->constraints.size());
	if (gEngfuncs.Con_Printf)
		gEngfuncs.Con_Printf("ragdoll_bullet: %s for %s (config dropped %d bodies, %d constraints)\n",
			s_status, cfgPath.c_str(), config.skippedBodies, config.skippedConstraints);
	return true;
}

void DestroyRagdoll(unsigned key)
{
	for (int i = 0; i < kMaxRagdolls; ++i)
	{
		Ragdoll &ragdoll = s_ragdolls[i];
		if (!ragdoll.valid || ragdoll.key != key)
			continue;

		if (s_world)
		{
			for (size_t c = 0; c < ragdoll.constraints.size(); ++c)
				s_world->removeConstraint(ragdoll.constraints[c]);
			for (size_t b = 0; b < ragdoll.bodies.size(); ++b)
				s_world->removeRigidBody(ragdoll.bodies[b]);
			if (ragdoll.ground)
				s_world->removeRigidBody(ragdoll.ground);
		}
		for (size_t c = 0; c < ragdoll.constraints.size(); ++c)
			delete ragdoll.constraints[c];
		for (size_t m = 0; m < ragdoll.motionStates.size(); ++m)
			delete ragdoll.motionStates[m];
		for (size_t b = 0; b < ragdoll.bodies.size(); ++b)
			delete ragdoll.bodies[b];
		for (size_t s = 0; s < ragdoll.shapes.size(); ++s)
			delete ragdoll.shapes[s];
		if (ragdoll.ground)
			delete ragdoll.ground;
		if (ragdoll.groundShape)
			delete ragdoll.groundShape;

		ragdoll = Ragdoll();
		return;
	}
}

bool HasRagdoll(unsigned key)
{
	return FindRagdoll(key) != NULL;
}

void Step(float dt, double now)
{
	if (!s_world || dt <= 0.0f)
		return;

	s_world->stepSimulation(dt, kMaxSubSteps, kFixedTimeStep);

	(void)now;
}

void ApplyBones(CStudioModelRenderer &renderer, unsigned key)
{
	Ragdoll *ragdoll = FindRagdoll(key);
	if (!ragdoll || !ragdoll->valid)
		return;
	if (!renderer.m_pStudioHeader || !renderer.m_pbonetransform || !renderer.m_plighttransform)
		return;

	/* The bones a rigid body drives take the simulated matrix. */
	for (size_t i = 0; i < ragdoll->bodies.size(); ++i)
	{
		const int bone = ragdoll->bodyBone[i];
		if (bone < 0 || bone >= ragdoll->boneCount)
			continue;
		float matrix[3][4];
		TransformToMatrix3x4(ragdoll->motionStates[i]->m_bonematrix, matrix);
		memcpy((*renderer.m_pbonetransform)[bone], matrix, sizeof(matrix));
		memcpy((*renderer.m_plighttransform)[bone], matrix, sizeof(matrix));
	}

	/* Every other bone keeps its relation to its parent. */
	const mstudiobone_t *bones = reinterpret_cast<const mstudiobone_t *>(
		reinterpret_cast<const byte *>(renderer.m_pStudioHeader) +
		renderer.m_pStudioHeader->boneindex);
	for (int bone = 0; bone < ragdoll->boneCount; ++bone)
	{
		if (ragdoll->boneDriven[bone])
			continue;
		const int parent = bones[bone].parent;
		if (parent < 0 || parent >= bone)
			continue;
		ConcatTransforms((*renderer.m_pbonetransform)[parent],
			ragdoll->boneRelative[bone], (*renderer.m_pbonetransform)[bone]);
		memcpy((*renderer.m_plighttransform)[bone],
			(*renderer.m_pbonetransform)[bone], sizeof(float[3][4]));
	}
}

int ActiveCount()
{
	int count = 0;
	for (int i = 0; i < kMaxRagdolls; ++i)
		if (s_ragdolls[i].valid)
			++count;
	return count;
}

} // namespace oc2_bullet
