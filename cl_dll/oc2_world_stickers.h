#pragma once
class CStudioModelRenderer;
struct model_s;

// Call immediately after the corresponding P/W weapon draw, while its bone
// matrices and lighting are still current. Context is p, w, or c (carried W).
void OC2_DrawWorldStickers(CStudioModelRenderer &renderer,model_s *weapon,
                          char context,int player,int serial,float *lightdir);
void OC2_ResetWorldStickers();
void OC2_InitWorldStickers();
