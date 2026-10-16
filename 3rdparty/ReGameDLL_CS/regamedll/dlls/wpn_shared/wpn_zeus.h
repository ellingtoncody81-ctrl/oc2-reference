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
	int iItemSlot() override { return KNIFE_SLOT; }
	BOOL UseDecrement() override
	{
#ifdef CLIENT_WEAPONS
		return TRUE;
#else
		return FALSE;
#endif
	}
	int Save(CSave &save) override;
	int Restore(CRestore &restore) override;
	static TYPEDESCRIPTION m_SaveData[];

private:
	void UpdateAccuracy();
	float ShotInaccuracy() const;
	void RemoveSpent();
	void ElectricEffect(const Vector &source, const Vector &end, bool hit);
	int m_iBeamSprite;
	float m_flAccuracyUpdateTime;
	float m_flPreviousFallSpeed;
	BOOL m_bWasAirborne;
};
