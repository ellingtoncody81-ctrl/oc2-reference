#ifndef OC2_VIEWMODEL_HANDEDNESS_H
#define OC2_VIEWMODEL_HANDEDNESS_H

namespace oc2_viewmodel
{
inline bool IsC4Model(const char *name)
{
	// Match the deployed viewmodel, not an inventory/HUD weapon id that can
	// change before the old model finishes drawing during weapon switches.
	const char *expected = "models/v_c4.mdl";
	if (!name) return false;
	for (; *name && *expected; ++name, ++expected)
	{
		char c = *name;
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if (c == '\\') c = '/';
		if (c != *expected) return false;
	}
	return !*name && !*expected;
}

inline bool UsesRightHand(const char *modelName, float preference)
{
	return IsC4Model(modelName) || preference > 0.0f;
}
}

#endif
