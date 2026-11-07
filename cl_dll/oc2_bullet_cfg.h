#pragma once
/*
 * Physics configuration for the Bullet3 ragdoll backend.
 *
 * The document layout follows the upstream MetaHookSv/BulletPhysics
 * "*_physics.txt" files: a KeyValues document with a "rigidBodies" map and a
 * "constraints" map. Only the keys that matter for a death flop are read.
 * Everything else (editor UI state, CRC placeholders, the physicBehaviors
 * and animControls sections) is ignored on purpose - the upstream code reads
 * those for barnacle/gargantua/camera behaviour and animation control, which
 * a death flop never uses.
 *
 * Units are inches, Z up - the same space the client world uses - so the
 * numbers are copied verbatim from the file without any scale conversion.
 */

#include <cstddef>
#include <string>
#include <vector>

enum BulletShapeType
{
	BulletShape_None = 0,
	BulletShape_Box,
	BulletShape_Sphere,
	BulletShape_Capsule,
	BulletShape_Cylinder,
	BulletShape_MultiSphere,
	BulletShape_TriangleMesh,
	BulletShape_Compound,
};

enum BulletShapeDirection
{
	BulletShapeAxis_X = 0,
	BulletShapeAxis_Y = 1,
	BulletShapeAxis_Z = 2,
};

enum BulletConstraintType
{
	BulletConstraint_None = 0,
	BulletConstraint_ConeTwist,
	BulletConstraint_Hinge,
	BulletConstraint_Point,
	BulletConstraint_Slider,
	BulletConstraint_Dof6,
	BulletConstraint_Dof6Spring,
	BulletConstraint_Fixed,
};

/* One rigid body: a capsule/box/sphere attached to a single studio bone. */
struct BulletRigidBodyConfig
{
	std::string name;
	int boneIndex = -1;

	/* Offset of the body origin relative to the bone, in inches. */
	float origin[3] = {0.0f, 0.0f, 0.0f};
	/* Rotation of the body relative to the bone, in degrees. */
	float angles[3] = {0.0f, 0.0f, 0.0f};
	/* Local forward axis of the body. */
	float forward[3] = {0.0f, 1.0f, 0.0f};

	float mass = 1.0f;
	float linearFriction = 1.0f;
	float rollingFriction = 0.2f;
	float restitution = 0.0f;
	float ccdRadius = 0.0f;
	float ccdThreshold = 0.001f;
	float linearSleepingThreshold = 5.0f;
	float angularSleepingThreshold = 3.0f;
	float additionalDampingFactor = 0.5f;
	float additionalLinearDampingThresholdSqr = 1.0f;
	float additionalAngularDampingThresholdSqr = 0.09f;

	int shapeType = BulletShape_None;
	int shapeDirection = BulletShapeAxis_Y;
	float shapeOrigin[3] = {0.0f, 0.0f, 0.0f};
	float shapeAngles[3] = {0.0f, 0.0f, 0.0f};
	/* Capsule: radius, height, unused. Cylinder: radius, height, unused. */
	float size[3] = {0.0f, 0.0f, 0.0f};
};

/* One joint between two rigid bodies. */
struct BulletConstraintConfig
{
	std::string name;
	int type = BulletConstraint_None;
	int rigidBodyA = -1;
	int rigidBodyB = -1;

	float originA[3] = {0.0f, 0.0f, 0.0f};
	float anglesA[3] = {0.0f, 0.0f, 0.0f};
	float originB[3] = {0.0f, 0.0f, 0.0f};
	float anglesB[3] = {0.0f, 0.0f, 0.0f};
	float forward[3] = {0.0f, 1.0f, 0.0f};

	float maxTolerantLinearError = 30.0f;
	bool disableCollision = true;
	bool useGlobalJointFromA = true;
	bool useLinearReferenceFrameA = true;

	/* ConeTwist */
	float coneSwingLimit1 = 0.0f;
	float coneSwingLimit2 = 0.0f;
	float coneTwistLimit = 0.0f;
	float coneSoftness = 1.0f;
	float coneBiasFactor = 0.3f;
	float coneRelaxationFactor = 1.0f;
	/* Hinge */
	float hingeLowLimit = 0.0f;
	float hingeHighLimit = 0.0f;
	float hingeSoftness = 1.0f;
	float hingeBiasFactor = 0.3f;
	float hingeRelaxationFactor = 1.0f;
	/* Slider */
	float sliderLowerLinearLimit = 0.0f;
	float sliderUpperLinearLimit = 0.0f;
	float sliderLowerAngularLimit = 0.0f;
	float sliderUpperAngularLimit = 0.0f;
	/* Solver */
	float linearERP = 0.0f;
	float linearCFM = 0.0f;
	float linearStopERP = 0.0f;
	float linearStopCFM = 0.0f;
	float angularERP = 0.0f;
	float angularCFM = 0.0f;
	float angularStopERP = 0.0f;
	float angularStopCFM = 0.0f;
	/* Dof6. The angular limits are stored as a fraction of pi, exactly like
	 * the other joints. */
	float dof6LowerLinearLimit[3] = {0.0f, 0.0f, 0.0f};
	float dof6UpperLinearLimit[3] = {0.0f, 0.0f, 0.0f};
	float dof6LowerAngularLimit[3] = {-1.0f, -1.0f, -1.0f};
	float dof6UpperAngularLimit[3] = {1.0f, 1.0f, 1.0f};
};

struct BulletRagdollConfig
{
	bool valid = false;
	std::string objectType;
	std::vector<BulletRigidBodyConfig> rigidBodies;
	std::vector<BulletConstraintConfig> constraints;
	/* Entries the parser had to drop, so the caller can report them instead of
	 * silently building a rig that is missing a limb or a joint. */
	int skippedBodies = 0;
	int skippedConstraints = 0;

	int FindRigidBody(const char *name) const;
};

/* A generic KeyValues node, used by the parser and by the offline test. */
struct BulletCfgNode
{
	std::string key;
	std::string value;
	std::vector<BulletCfgNode> children;
	bool isBlock = false;
};

/*
 * Parse a KeyValues document. Tolerant by design: an unterminated block or a
 * stray brace does not abort the parse, because a partially readable config
 * is still more useful than none.
 */
bool BulletCfgParseKeyValues(const char *text, size_t length, BulletCfgNode &root);

/*
 * Turn a parsed document into a ragdoll description. Returns false when the
 * document has no "rigidBodies" map or when a body has no usable bone index.
 */
bool BulletCfgParseRagdoll(const char *text, size_t length, BulletRagdollConfig &out);

/* "<model path without extension>_physics.txt" - the upstream lookup rule. */
std::string BulletCfgPathForModel(const char *modelPath);
