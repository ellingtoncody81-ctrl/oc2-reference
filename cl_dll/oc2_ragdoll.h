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
void OC2_RagdollPlayerState(int playerIndex, bool dead);
bool OC2_RagdollCanCreateCorpse(int playerIndex);
void OC2_RagdollCorpseCreated(tempent_s *corpse, int playerIndex);
void OC2_RagdollCorpseForgotten(tempent_s *corpse);
void OC2_RagdollApply(CStudioModelRenderer &renderer);
void OC2_RagdollTick(double clientTime);
void OC2_RagdollResolveDeaths(double clientTime);
void OC2_RagdollClear();
// Hand the corpse over to the bone physics: the temp entity must stop
// animating and colliding on its own.
void OC2_CorpseTakeOverPhysics(tempent_s *corpse);

// The frame rate of the death animation the corpse should play. The temp
// entity needs it because its own default of one frame per second would
// freeze the corpse in the pose of the first frame.
extern float g_oc2CorpseSequenceFramerate;
void OC2_CorpseForget(tempent_s *corpse);
