#pragma once
class CCSBot;
class CBasePlayer;
class CBaseEntity;
class CGrenade;
void OC2_BotUtilityThrown(CGrenade *grenade,CBasePlayer *owner,int id);
bool OC2_BotValidateUtilityThrow(CBasePlayer *owner,int id,const Vector &source,const Vector &velocity);
// Returns false when this friendly bot flash must have no effect. The shared
// batch state permits one explosion (not one recipient) per team / 15 seconds.
bool OC2_BotFriendlyFlash(CBaseEntity *grenade,CBasePlayer *victim,float &fade,float &hold,int &alpha,int &batch);
void OC2_BotBuyUtility(CCSBot *bot);
bool OC2_BotUtilityUpdate(CCSBot *bot,CBasePlayer *threat);
bool OC2_BotEquipUtility(CCSBot *bot,bool noSmoke);
bool OC2_BotCanUseZeus(CCSBot *bot);
bool OC2_BotUtilityAiming(CCSBot *bot);
void OC2_BotTacticsReset();
void OC2_BotTacticsFrame();
void OC2_BotTacticsDump();
void OC2_BotTacticsProbe();
void OC2_BotTacticsFixture();
void OC2_BotUtilityEffect(CBaseEntity *grenade,const Vector &point);
void OC2_BotUtilityBounce(CBaseEntity *grenade,const Vector &normal);
