#pragma once
#include "weapons.h"

// The paired client uses the same three existing prediction fields:
// startThrow=last held update, releaseThrow=emission deadline, lastFire=strength.
class CMolotovGrenade: public CBasePlayerWeapon
{
public:
	enum { INCENDIARY_STATE = 1 << 6 }; // Paired weapondata::m_iWeaponState bit.
	bool IsIncendiary() const { return (m_iWeaponState & INCENDIARY_STATE) != 0; }
	int Save(CSave &save) override;
	int Restore(CRestore &restore) override;
	static TYPEDESCRIPTION m_SaveData[];
	void Spawn() override;
	void Precache() override;
	int GetItemInfo(ItemInfo *info) override;
	BOOL Deploy() override;
	BOOL CanDeploy() override;
	BOOL CanDrop() override { return m_flStartThrow <= 0 && m_flReleaseThrow < 0 && CanDeploy(); }
	int AddDuplicate(CBasePlayerItem *original) override;
	BOOL CanHolster() override { return TRUE; }
	void Holster(int skiplocal = 0) override;
	void PrimaryAttack() override;
	void SecondaryAttack() override;
	void ItemPostFrame() override;
	void WeaponIdle() override;
	float GetMaxSpeed() override { return 250.0f; }
	int iItemSlot() override { return GRENADE_SLOT; }
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

// A spent ID31 entity may remain until its throw animation retires. When a
// pickup replenishes that empty slot, transfer the incoming physical variant.
void OC2_AdoptFireVariant(CBasePlayerWeapon *original, const CBasePlayerWeapon *incoming);
