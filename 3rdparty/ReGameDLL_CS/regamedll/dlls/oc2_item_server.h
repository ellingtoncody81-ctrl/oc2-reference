#pragma once
class CBasePlayer;
class CBasePlayerItem;
void OC2_ItemsRegisterMessage();
void OC2_ItemsPrecache();
void OC2_ItemsClientConnected(CBasePlayer *player);
void OC2_ItemsClientDisconnected(CBasePlayer *player);
void OC2_ItemsAcquire(CBasePlayerItem *item, CBasePlayer *player);
void OC2_ItemsFrame();
bool OC2_ItemsCommand(CBasePlayer *player, const char *command);
