#pragma once

struct ref_params_s;
struct local_state_s;
struct usercmd_s;

// Explicit, bounded offline-support capture. Disabled until a console command.
void HUD_WeaponMotionInit();
void HUD_WeaponMotionView(const ref_params_s *view, int weapon, int state, int clip);
void HUD_WeaponMotionPrediction(const local_state_s *from, const local_state_s *to,
    const usercmd_s *command, int runfuncs, double time, unsigned int seed);
void HUD_WeaponMotionPose(const char *model, const void *header,
    const float pos[][3], const float q[][4], double time, double sampleTime,
    double start, int sequence, float frame, int change, int layers, bool snap);
