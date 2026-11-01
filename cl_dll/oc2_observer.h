#pragma once
struct cl_entity_s;
const char *OC2_ControlPrompt();
bool OC2_ObserverFullMap();
bool OC2_ObserverStatsOpen();
void OC2_ObserverResetInputs();
void OC2_ObserverSelect(int key);
void OC2_ControlApplyAnimation(cl_entity_s *view,int weapon,double time);
