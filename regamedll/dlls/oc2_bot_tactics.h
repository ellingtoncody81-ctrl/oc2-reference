#pragma once
class CCSBot;
class CBasePlayer;
void OC2_BotBuyUtility(CCSBot *bot);
bool OC2_BotUtilityUpdate(CCSBot *bot,CBasePlayer *threat);
bool OC2_BotEquipUtility(CCSBot *bot,bool noSmoke);
bool OC2_BotCanUseZeus(CCSBot *bot);
void OC2_BotTacticsReset();
void OC2_BotTacticsDump();
