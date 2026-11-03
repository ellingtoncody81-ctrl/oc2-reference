#pragma once

class CWeaponBox;
class CBasePlayer;
class CBaseEntity;
void OC2_PickupPrecache();
void OC2_PickupDropped(CWeaponBox *box, CBasePlayer *owner);
void OC2_PickupSoundOnLanding(CBaseEntity *box);
bool OC2_PickupTryUse(CBasePlayer *player);
void OC2_PickupFrame();
bool OC2_PickupCanTouch(CWeaponBox *box, CBasePlayer *player);
bool OC2_PickupUseDelivery(CWeaponBox *box, CBasePlayer *player);
bool OC2_PickupDeferSwitch(CBasePlayer *player);
