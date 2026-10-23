#pragma once
class CStudioModelRenderer;
struct alight_s;
void OC2_AccessoriesInit();
void OC2_AccessoriesVidInit();
void OC2_BeginAccessories(CStudioModelRenderer &renderer,int weapon);
void OC2_DrawAccessories(CStudioModelRenderer &renderer,alight_s &lighting,float *lightdir);
void OC2_AccessoryKill(const char *weapon,int killer,int victim,int killerTeam,int victimTeam);
