#pragma once

#include "weapons.h"

class CZeus : public CBasePlayerWeapon
{
public:
	void Spawn() override;
	void Precache() override;
	int GetItemInfo(ItemInfo *info) override;
	BOOL Deploy() override;
	void Holster(int skiplocal = 0) override;
	void ItemPostFrame() override;
	void PrimaryAttack() override;
	void SecondaryAttack() override;
	void Reload() override {}
	void WeaponIdle() override;
	float GetMaxSpeed() override { return 220.0f; }
	int iItemSlot() override { return WPNSLOT_KNIFE; }
	BOOL UseDecrement() override { return TRUE; }
};
