#pragma once

struct ref_params_s;
class CStudioModelRenderer;

void OC2_LegsInit();
// The scene is cleared before the current frame's camera is calculated.
void OC2_LegsBeginFrame();
// Called after the normal camera has resolved prediction and stair smoothing.
void OC2_LegsRefdef(const ref_params_s *view);
void OC2_LegsCreateEntity();
// Sample the client's ground layers and independent jump on the same foot anchor.
bool OC2_LegsBlend(CStudioModelRenderer &renderer, float pos[][3], float q[][4]);
