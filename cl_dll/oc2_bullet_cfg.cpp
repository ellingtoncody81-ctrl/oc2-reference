/*
 * KeyValues reader for the Bullet3 ragdoll configuration.
 *
 * This file is deliberately free of engine and Bullet includes so it can be
 * compiled and exercised by the offline parser test without the game.
 */

#include "oc2_bullet_cfg.h"

#include <cstdlib>
#include <cstring>

namespace
{

struct Token
{
	enum Type { Str, Open, Close };
	Type type;
	std::string text;
};

bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool Tokenize(const char *text, size_t length, std::vector<Token> &out)
{
	size_t i = 0;
	while (i < length)
	{
		const char c = text[i];
		if (IsSpace(c))
		{
			++i;
			continue;
		}
		if (c == '/' && i + 1 < length && text[i + 1] == '/')
		{
			while (i < length && text[i] != '\n')
				++i;
			continue;
		}
		if (c == '{')
		{
			Token tok = { Token::Open, std::string() };
			out.push_back(tok);
			++i;
			continue;
		}
		if (c == '}')
		{
			Token tok = { Token::Close, std::string() };
			out.push_back(tok);
			++i;
			continue;
		}
		if (c == '"')
		{
			++i;
			std::string value;
			while (i < length)
			{
				if (text[i] == '\\' && i + 1 < length)
				{
					value.push_back(text[i + 1]);
					i += 2;
					continue;
				}
				if (text[i] == '"')
				{
					++i;
					break;
				}
				value.push_back(text[i]);
				++i;
			}
			Token tok = { Token::Str, value };
			out.push_back(tok);
			continue;
		}
		size_t start = i;
		while (i < length && !IsSpace(text[i]) && text[i] != '{' && text[i] != '}' && text[i] != '"')
			++i;
		Token tok = { Token::Str, std::string(text + start, i - start) };
		out.push_back(tok);
	}
	return true;
}

void ParseBlock(const std::vector<Token> &tokens, size_t &pos, BulletCfgNode &node)
{
	node.isBlock = true;
	while (pos < tokens.size())
	{
		const Token &tok = tokens[pos];
		if (tok.type == Token::Close)
		{
			++pos;
			return;
		}
		if (tok.type == Token::Open)
		{
			++pos;
			continue;
		}
		BulletCfgNode child;
		child.key = tok.text;
		++pos;
		if (pos < tokens.size() && tokens[pos].type == Token::Open)
		{
			++pos;
			ParseBlock(tokens, pos, child);
		}
		else if (pos < tokens.size() && tokens[pos].type == Token::Str)
		{
			child.value = tokens[pos].text;
			++pos;
		}
		node.children.push_back(child);
	}
}

/* Case insensitive compare without depending on POSIX strcasecmp. */
bool SameKey(const std::string &a, const char *b)
{
	if (!b)
		return false;
	size_t i = 0;
	for (; i < a.size() && b[i]; ++i)
	{
		const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? (char)(a[i] - 'A' + 'a') : a[i];
		const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? (char)(b[i] - 'A' + 'a') : b[i];
		if (ca != cb)
			return false;
	}
	return i == a.size() && b[i] == '\0';
}

bool SameKey(const char *a, const char *b)
{
	return a && SameKey(std::string(a), b);
}

const BulletCfgNode *FindChild(const BulletCfgNode &node, const char *key)
{
	for (size_t i = 0; i < node.children.size(); ++i)
		if (SameKey(node.children[i].key, key))
			return &node.children[i];
	return NULL;
}

bool GetString(const BulletCfgNode &node, const char *key, std::string &out)
{
	const BulletCfgNode *child = FindChild(node, key);
	if (!child || child->isBlock)
		return false;
	out = child->value;
	return true;
}

bool GetFloat(const BulletCfgNode &node, const char *key, float &out)
{
	const BulletCfgNode *child = FindChild(node, key);
	if (!child || child->isBlock || child->value.empty())
		return false;
	char *end = NULL;
	const float value = (float)::strtod(child->value.c_str(), &end);
	if (end == child->value.c_str())
		return false;
	out = value;
	return true;
}

bool GetInt(const BulletCfgNode &node, const char *key, int &out)
{
	float value = 0.0f;
	if (!GetFloat(node, key, value))
		return false;
	out = (int)value;
	return true;
}

bool GetBool(const BulletCfgNode &node, const char *key, bool &out)
{
	const BulletCfgNode *child = FindChild(node, key);
	if (!child || child->isBlock)
		return false;
	if (child->value.empty())
		return false;
	const char *v = child->value.c_str();
	if (SameKey(v, "1") || SameKey(v, "true") || SameKey(v, "yes"))
	{
		out = true;
		return true;
	}
	if (SameKey(v, "0") || SameKey(v, "false") || SameKey(v, "no"))
	{
		out = false;
		return true;
	}
	return false;
}

/* "x y z" or "x y z w" - extra components are ignored. */
bool GetVec3(const BulletCfgNode &node, const char *key, float *out)
{
	const BulletCfgNode *child = FindChild(node, key);
	if (!child || child->isBlock || child->value.empty())
		return false;
	const char *text = child->value.c_str();
	char *end = NULL;
	for (int i = 0; i < 3; ++i)
	{
		const float value = (float)::strtod(text, &end);
		if (end == text)
			return false;
		out[i] = value;
		text = end;
	}
	return true;
}

int ShapeTypeFromName(const std::string &name)
{
	if (SameKey(name, "Box")) return BulletShape_Box;
	if (SameKey(name, "Sphere")) return BulletShape_Sphere;
	if (SameKey(name, "Capsule")) return BulletShape_Capsule;
	if (SameKey(name, "Cylinder")) return BulletShape_Cylinder;
	if (SameKey(name, "MultiSphere")) return BulletShape_MultiSphere;
	if (SameKey(name, "TriangleMesh")) return BulletShape_TriangleMesh;
	if (SameKey(name, "Compound")) return BulletShape_Compound;
	return BulletShape_None;
}

int ConstraintTypeFromName(const std::string &name)
{
	if (SameKey(name, "ConeTwist")) return BulletConstraint_ConeTwist;
	if (SameKey(name, "Hinge")) return BulletConstraint_Hinge;
	if (SameKey(name, "Point")) return BulletConstraint_Point;
	if (SameKey(name, "Slider")) return BulletConstraint_Slider;
	if (SameKey(name, "Dof6")) return BulletConstraint_Dof6;
	if (SameKey(name, "Dof6Spring")) return BulletConstraint_Dof6Spring;
	if (SameKey(name, "Fixed")) return BulletConstraint_Fixed;
	return BulletConstraint_None;
}

} // namespace

int BulletRagdollConfig::FindRigidBody(const char *name) const
{
	for (size_t i = 0; i < rigidBodies.size(); ++i)
		if (SameKey(rigidBodies[i].name, name))
			return (int)i;
	return -1;
}

bool BulletCfgParseKeyValues(const char *text, size_t length, BulletCfgNode &root)
{
	if (!text || !length)
		return false;
	std::vector<Token> tokens;
	if (!Tokenize(text, length, tokens) || tokens.empty())
		return false;
	size_t pos = 0;
	ParseBlock(tokens, pos, root);
	return true;
}

bool BulletCfgParseRagdoll(const char *text, size_t length, BulletRagdollConfig &out)
{
	out = BulletRagdollConfig();

	BulletCfgNode root;
	if (!BulletCfgParseKeyValues(text, length, root))
		return false;

	/* The document may wrap everything in a "PhysicObjectConfig" block. */
	const BulletCfgNode *doc = &root;
	const BulletCfgNode *wrapper = FindChild(root, "PhysicObjectConfig");
	if (wrapper && wrapper->isBlock)
		doc = wrapper;

	GetString(*doc, "type", out.objectType);
	if (!SameKey(out.objectType, "RagdollObject"))
		return false;

	const BulletCfgNode *bodies = FindChild(*doc, "rigidBodies");
	if (!bodies || !bodies->isBlock)
		return false;

	for (size_t i = 0; i < bodies->children.size(); ++i)
	{
		const BulletCfgNode &node = bodies->children[i];
		if (!node.isBlock)
			continue;

		BulletRigidBodyConfig body;
		body.name = node.key;
		if (!GetInt(node, "boneindex", body.boneIndex))
		{
			++out.skippedBodies;
			continue;
		}
		if (body.boneIndex < 0)
		{
			++out.skippedBodies;
			continue;
		}

		GetVec3(node, "origin", body.origin);
		GetVec3(node, "angles", body.angles);
		GetVec3(node, "forward", body.forward);
		GetFloat(node, "mass", body.mass);
		GetFloat(node, "LinearFriction", body.linearFriction);
		GetFloat(node, "RollingFriction", body.rollingFriction);
		GetFloat(node, "Restitution", body.restitution);
		GetFloat(node, "CCDRadius", body.ccdRadius);
		GetFloat(node, "CCDThreshold", body.ccdThreshold);
		GetFloat(node, "LinearSleepingThreshold", body.linearSleepingThreshold);
		GetFloat(node, "AngularSleepingThreshold", body.angularSleepingThreshold);
		GetFloat(node, "AdditionalDampingFactor", body.additionalDampingFactor);
		GetFloat(node, "AdditionalLinearDampingThresholdSqr", body.additionalLinearDampingThresholdSqr);
		GetFloat(node, "AdditionalAngularDampingThresholdSqr", body.additionalAngularDampingThresholdSqr);

		const BulletCfgNode *shape = FindChild(node, "collisionShape");
		if (shape && shape->isBlock)
		{
			std::string typeName;
			if (GetString(*shape, "type", typeName))
				body.shapeType = ShapeTypeFromName(typeName);
			GetInt(*shape, "direction", body.shapeDirection);
			GetVec3(*shape, "origin", body.shapeOrigin);
			GetVec3(*shape, "angles", body.shapeAngles);
			GetVec3(*shape, "size", body.size);
		}

		out.rigidBodies.push_back(body);
	}

	if (out.rigidBodies.empty())
		return false;

	const BulletCfgNode *constraints = FindChild(*doc, "constraints");
	if (constraints && constraints->isBlock)
	{
		for (size_t i = 0; i < constraints->children.size(); ++i)
		{
			const BulletCfgNode &node = constraints->children[i];
			if (!node.isBlock)
				continue;

			BulletConstraintConfig constraint;
			constraint.name = node.key;
			std::string typeName;
			if (GetString(node, "type", typeName))
				constraint.type = ConstraintTypeFromName(typeName);
			if (constraint.type == BulletConstraint_None)
			{
				++out.skippedConstraints;
				continue;
			}

			std::string bodyA, bodyB;
			if (GetString(node, "rigidbodyA", bodyA))
				constraint.rigidBodyA = out.FindRigidBody(bodyA.c_str());
			if (GetString(node, "rigidbodyB", bodyB))
				constraint.rigidBodyB = out.FindRigidBody(bodyB.c_str());
			if (constraint.rigidBodyA < 0 || constraint.rigidBodyB < 0)
			{
				++out.skippedConstraints;
				continue;
			}

			GetVec3(node, "originA", constraint.originA);
			GetVec3(node, "anglesA", constraint.anglesA);
			GetVec3(node, "originB", constraint.originB);
			GetVec3(node, "anglesB", constraint.anglesB);
			GetVec3(node, "forward", constraint.forward);
			GetFloat(node, "maxTolerantLinearError", constraint.maxTolerantLinearError);
			GetBool(node, "disableCollision", constraint.disableCollision);
			GetBool(node, "useGlobalJointFromA", constraint.useGlobalJointFromA);
			GetBool(node, "useLinearReferenceFrameA", constraint.useLinearReferenceFrameA);

			const BulletCfgNode *factors = FindChild(node, "factors");
			if (factors && factors->isBlock)
			{
				GetFloat(*factors, "ConeTwistSwingSpanLimit1", constraint.coneSwingLimit1);
				GetFloat(*factors, "ConeTwistSwingSpanLimit2", constraint.coneSwingLimit2);
				GetFloat(*factors, "ConeTwistTwistSpanLimit", constraint.coneTwistLimit);
				GetFloat(*factors, "ConeTwistSoftness", constraint.coneSoftness);
				GetFloat(*factors, "ConeTwistBiasFactor", constraint.coneBiasFactor);
				GetFloat(*factors, "ConeTwistRelaxationFactor", constraint.coneRelaxationFactor);
				GetFloat(*factors, "HingeLowLimit", constraint.hingeLowLimit);
				GetFloat(*factors, "HingeHighLimit", constraint.hingeHighLimit);
				GetFloat(*factors, "HingeSoftness", constraint.hingeSoftness);
				GetFloat(*factors, "HingeBiasFactor", constraint.hingeBiasFactor);
				GetFloat(*factors, "HingeRelaxationFactor", constraint.hingeRelaxationFactor);
				GetFloat(*factors, "SliderLowerLinearLimit", constraint.sliderLowerLinearLimit);
				GetFloat(*factors, "SliderUpperLinearLimit", constraint.sliderUpperLinearLimit);
				GetFloat(*factors, "SliderLowerAngularLimit", constraint.sliderLowerAngularLimit);
				GetFloat(*factors, "SliderUpperAngularLimit", constraint.sliderUpperAngularLimit);
				GetFloat(*factors, "LinearERP", constraint.linearERP);
				GetFloat(*factors, "LinearCFM", constraint.linearCFM);
				GetFloat(*factors, "LinearStopERP", constraint.linearStopERP);
				GetFloat(*factors, "LinearStopCFM", constraint.linearStopCFM);
				GetFloat(*factors, "AngularERP", constraint.angularERP);
				GetFloat(*factors, "AngularCFM", constraint.angularCFM);
				GetFloat(*factors, "AngularStopERP", constraint.angularStopERP);
				GetFloat(*factors, "AngularStopCFM", constraint.angularStopCFM);
				/* Upstream BulletPhysicManager.cpp:500-535 reads the Dof6
				 * limits from the same factor block. */
				static const char *const kDof6Keys[12] = {
					"Dof6LowerLinearLimitX", "Dof6LowerLinearLimitY", "Dof6LowerLinearLimitZ",
					"Dof6UpperLinearLimitX", "Dof6UpperLinearLimitY", "Dof6UpperLinearLimitZ",
					"Dof6LowerAngularLimitX", "Dof6LowerAngularLimitY", "Dof6LowerAngularLimitZ",
					"Dof6UpperAngularLimitX", "Dof6UpperAngularLimitY", "Dof6UpperAngularLimitZ" };
				float *const kDof6Targets[12] = {
					&constraint.dof6LowerLinearLimit[0], &constraint.dof6LowerLinearLimit[1],
					&constraint.dof6LowerLinearLimit[2], &constraint.dof6UpperLinearLimit[0],
					&constraint.dof6UpperLinearLimit[1], &constraint.dof6UpperLinearLimit[2],
					&constraint.dof6LowerAngularLimit[0], &constraint.dof6LowerAngularLimit[1],
					&constraint.dof6LowerAngularLimit[2], &constraint.dof6UpperAngularLimit[0],
					&constraint.dof6UpperAngularLimit[1], &constraint.dof6UpperAngularLimit[2] };
				for (int i = 0; i < 12; ++i)
					GetFloat(*factors, kDof6Keys[i], *kDof6Targets[i]);
			}

			out.constraints.push_back(constraint);
		}
	}

	out.valid = true;
	return true;
}

std::string BulletCfgPathForModel(const char *modelPath)
{
	std::string path = modelPath ? modelPath : "";
	const size_t slash = path.find_last_of("/\\");
	const size_t dot = path.find_last_of('.');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
		path.erase(dot);
	path += "_physics.txt";
	return path;
}
