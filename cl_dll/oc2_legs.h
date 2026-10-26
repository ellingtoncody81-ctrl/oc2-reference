#pragma once

struct ref_params_s;
struct local_state_s;
struct cl_entity_s;
class CStudioModelRenderer;

void OC2_LegsInit();
// The scene is cleared before the current frame's camera is calculated.
void OC2_LegsBeginFrame();
// PM_Duck calls this after position categorization, before any jump/air move.
void OC2_LegsDuckGround(bool grounded);
// Capture both endpoints of the final prediction command before render interpolation.
void OC2_LegsPrediction(const local_state_s *from, const local_state_s *to,
    double commandTime, int commandMsec, unsigned int commandSeed);
// Called after the normal camera has resolved prediction and stair smoothing.
void OC2_LegsRefdef(const ref_params_s *view);
void OC2_LegsCreateEntity();
// Only the client-owned first-person body, never players or their weapons.
bool OC2_LegsIsEntity(const cl_entity_s *entity);
// Sample the client's ground layers and independent jump on the same foot anchor.
bool OC2_LegsBlend(CStudioModelRenderer &renderer, float pos[][3], float q[][4]);
