#pragma once
#include "weapons.h"

// The paired client uses the same three existing prediction fields:
// startThrow=last held update, releaseThrow=emission deadline, lastFire=strength.
class CMolotovGrenade: public CBasePlayerWeapon
{
public:
	enum { INCENDIARY_STATE = 1 << 6 }; // Paired weapondata::m_iWeaponState bit.
	bool IsIncendiary() const { return (m_iWeaponState & INCENDIARY_STATE) != 0; }
	void Spawn() override;
	void Precache() override;
	int GetItemInfo(ItemInfo *info) override;
	BOOL Deploy() override;
	BOOL CanDeploy() override;
	BOOL CanDrop() override { return m_flStartThrow <= 0 && m_flReleaseThrow < 0 && CanDeploy(); }
	BOOL CanHolster() override { return TRUE; }
	void Holster(int skiplocal = 0) override;
	void PrimaryAttack() override;
	void SecondaryAttack() override;
	void ItemPostFrame() override;
	void WeaponIdle() override;
	float GetMaxSpeed() override { return 250.0f; }
	int iItemSlot() override { return WPNSLOT_GRENADE; }
	BOOL UseDecrement() override
	{
#ifdef CLIENT_WEAPONS
		return TRUE;
#else
		return FALSE;
#endif
	}
private:
	void BeginHold(bool underhand);
	void EmitThrow();
};
