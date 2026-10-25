#pragma once

struct ref_params_s;
class CStudioModelRenderer;

void OC2_LegsInit();
// Called after the normal camera has resolved prediction and stair smoothing.
void OC2_LegsRefdef(const ref_params_s *view);
void OC2_LegsCreateEntity();
// Blend only the client body; weapon/player animation histories stay separate.
bool OC2_LegsBlend(CStudioModelRenderer &renderer, float pos[][3], float q[][4]);
