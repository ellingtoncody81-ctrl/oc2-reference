#ifndef OC2_VIEWMODEL_HANDEDNESS_H
#define OC2_VIEWMODEL_HANDEDNESS_H

namespace oc2_viewmodel
{
inline bool UsesRightHand(const char *, float preference)
{
	return preference > 0.0f;
}
}

#endif
