#pragma once
class CBasePlayer;
bool OC2_BotTakeover(CBasePlayer *player);
bool OC2_BotParked(CBasePlayer *player);
void OC2_BotControlRegister();
void OC2_BotControlReset();
void OC2_BotControlRound();
void OC2_BotControlDisconnect(CBasePlayer *player);
void OC2_BotControlSpawn(CBasePlayer *player);
void OC2_BotControlDeath(CBasePlayer *player);
void OC2_BotControlTeam(CBasePlayer *player);
void OC2_BotControlFrame();
void OC2_BotControlStatus(CBasePlayer *player);
bool OC2_BotControlCommand(CBasePlayer *player,const char *command);
int OC2_GrenadeCount(CBasePlayer *player);
bool OC2_GrenadeWeapon(int weapon);
int OC2_GrenadeRoom(CBasePlayer *player,const char *ammo);
