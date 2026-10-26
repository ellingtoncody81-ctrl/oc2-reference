#pragma once

struct tempent_s;
class CStudioModelRenderer;
class Vector;

void OC2_RagdollInit();
bool OC2_RagdollRegister(tempent_s *corpse, int playerIndex);
void OC2_RagdollCaptureLive(CStudioModelRenderer &renderer, int playerIndex);
bool OC2_RagdollHideDeadPlayer(int playerIndex, int drawnSequence);
void OC2_RagdollSpawnDeath(int victimIndex, int killerIndex, bool headshot = false);
bool OC2_RagdollSuppressDelayedCorpse(int playerIndex, const Vector &origin);
void OC2_RagdollApply(CStudioModelRenderer &renderer);
void OC2_RagdollTick(double clientTime);
void OC2_RagdollClear();
void OC2_CorpseForget(tempent_s *corpse);
