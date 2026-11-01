#pragma once

class CWeaponBox;
class CBasePlayer;
void OC2_PickupPrecache();
void OC2_PickupDropped(CWeaponBox *box, CBasePlayer *owner);
bool OC2_PickupTryUse(CBasePlayer *player);
void OC2_PickupFrame();
bool OC2_PickupCanTouch(CWeaponBox *box, CBasePlayer *player);
bool OC2_PickupUseDelivery(CWeaponBox *box, CBasePlayer *player);
bool OC2_PickupDeferSwitch(CBasePlayer *player);
