#pragma once
#include "../../../../common/oc2_economy.h"

int OC2_KillAward(entvars_t *attacker, entvars_t *inflictor);
void OC2_EconomyRound(CHalfLifeMultiplay *rules, bool reset);
void OC2_SuicideAward(CBasePlayer *victim);
