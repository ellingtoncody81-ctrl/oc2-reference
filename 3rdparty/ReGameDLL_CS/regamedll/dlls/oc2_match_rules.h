#pragma once
void OC2_MatchRegister();
bool OC2_MatchEnabled();
bool OC2_MatchBeforeRound(CHalfLifeMultiplay *rules);
bool OC2_MatchThink(CHalfLifeMultiplay *rules);
void OC2_MatchRoundReady(CHalfLifeMultiplay *rules);
void OC2_MatchApplyRequests(CHalfLifeMultiplay *rules);
bool OC2_MatchTeamPending(CBasePlayer *player);
int OC2_MatchChooseTeam(CBasePlayer *player, int team);
bool OC2_MatchCommand(CBasePlayer *player, const char *command);
void OC2_MatchDump();
