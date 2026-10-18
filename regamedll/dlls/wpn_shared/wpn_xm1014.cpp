#include "precompiled.h"
#include "oc2_gunshot.h"

const Vector XM1014_CONE_VECTOR = Vector(0.0725, 0.0725, 0.0); // special shotgun spreads

LINK_ENTITY_TO_CLASS(weapon_xm1014, CXM1014, CCSXM1014)

void CXM1014::Spawn()
{
	Precache();

	m_iId = WEAPON_XM1014;
	SET_MODEL(edict(), "models/w_xm1014.mdl");

	m_iDefaultAmmo = XM1014_DEFAULT_GIVE;

#ifdef REGAMEDLL_API
	CSPlayerWeapon()->m_flBaseDamage = XM1014_DAMAGE;
#endif

	// Get ready to fall down
	FallInit();

	// extend
	CBasePlayerWeapon::Spawn();
}

void CXM1014::Precache()
{
	PRECACHE_MODEL("models/v_xm1014.mdl");
	PRECACHE_MODEL("models/w_xm1014.mdl");

	m_iShellId = m_iShell = PRECACHE_MODEL("models/shotgunshell.mdl");

	PRECACHE_SOUND("weapons/xm1014-1.wav");
	PRECACHE_SOUND("weapons/reload1.wav");
	PRECACHE_SOUND("weapons/reload3.wav");

	m_usFireXM1014 = PRECACHE_EVENT(1, "events/xm1014.sc");
}

int CXM1014::GetItemInfo(ItemInfo *p)
{
	const oc2_weapon::Params &params=oc2_weapon::Get(oc2_weapon::XM1014);
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "buckshot";
	p->iMaxAmmo1 = params.reserve;
	p->pszAmmo2 = nullptr;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = params.clip;
	p->iSlot = 0;
	p->iPosition = 12;
	p->iId = m_iId = WEAPON_XM1014;
	p->iFlags = ITEM_FLAG_NOFIREUNDERWATER;
	p->iWeight = XM1014_WEIGHT;

	return 1;
}

BOOL CXM1014::Deploy()
{
	return DefaultDeploy("models/v_xm1014.mdl", "models/p_xm1014.mdl", XM1014_DRAW, "m249", UseDecrement() != FALSE);
}

BOOL CXM1014::PlayEmptySound()
{
	BOOL result = CBasePlayerWeapon::PlayEmptySound();
	m_iPlayEmptySound = 0;
	return result;
}

void CXM1014::PrimaryAttack()
{
	OC2GunShot sourceShot(this);
	Vector vecAiming, vecSrc, vecDir;
	int flag;

	if (m_iClip <= 0)
	{
#ifdef BUILD_LATEST_FIXES
		if (!m_fInSpecialReload)
		{
			PlayEmptySound();

			if (TheBots)
			{
				TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
			}
		}

		Reload();
#else
		Reload();

		if (!m_iClip)
		{
			PlayEmptySound();
		}

		if (TheBots)
		{
			TheBots->OnEvent(EVENT_WEAPON_FIRED_ON_EMPTY, m_pPlayer);
		}

		m_flNextPrimaryAttack = GetNextAttackDelay(1);
#endif // #ifdef BUILD_LATEST_FIXES

		return;
	}

	m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
	m_iClip--;

	m_pPlayer->pev->effects |= EF_MUZZLEFLASH;
	// player "shoot" animation
	m_pPlayer->SetAnimation(PLAYER_ATTACK1);

	UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);

	vecSrc = m_pPlayer->GetGunPosition();
	vecAiming = gpGlobals->v_forward;

    Vector sourceAngles=m_pPlayer->pev->v_angle+m_pPlayer->pev->punchangle;
    const auto &schema=oc2_weapon::Get(oc2_weapon::XM1014);
    for (int pellet=0;pellet<schema.pellets;++pellet)
    {
        // ReGameDLL's VectorRef trace advances its source after a hit.
        // Every pellet must begin at the same muzzle, as Source FireBullets does.
        Vector pelletSource=vecSrc,pelletAim=vecAiming;
        m_pPlayer->FireBullets3(pelletSource,pelletAim,0,schema.range,1,BULLET_PLAYER_BUCKSHOT,
            schema.damage,schema.rangeModifier,m_pPlayer->pev,false,m_pPlayer->random_seed);
    }


#ifdef CLIENT_WEAPONS
	flag = FEV_NOTHOST;
#else
	flag = 0;
#endif

#ifdef REGAMEDLL_ADD
	// HACKHACK: client-side weapon prediction fix
	if (!(iFlags() & ITEM_FLAG_NOFIREUNDERWATER) && m_pPlayer->pev->waterlevel == 3)
		flag = 0;
#endif

    PLAYBACK_EVENT_FULL(flag,m_pPlayer->edict(),m_usFireXM1014,0,(float *)&g_vecZero,
        (float *)&sourceAngles,sourceShot.Inaccuracy()*1024,0,int(m_pPlayer->random_seed&255),
        int(sourceShot.RecoilIndex()),m_iClip==0,FALSE);

	if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
	{
		// HEV suit - indicate out of ammo condition
		m_pPlayer->SetSuitUpdate("!HEV_AMO0", SUIT_SENTENCE, SUIT_REPEAT_OK);
	}

#ifndef REGAMEDLL_FIXES
	if (m_iClip != 0)
		m_flPumpTime = UTIL_WeaponTimeBase() + 0.125f;
#endif

	m_flNextPrimaryAttack = GetNextAttackDelay(0.25);
	m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 0.25f;

	if (m_iClip != 0)
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 2.25f;
	else
		m_flTimeWeaponIdle = 0.75f;

	m_fInSpecialReload = 0;

#ifdef REGAMEDLL_ADD
	if (m_pPlayer->pev->flags & FL_ONGROUND)
		KickBack(UTIL_SharedRandomLong(m_pPlayer->random_seed + 1, 3, 5), 0.0, 0.0, 0.0, 0.0, 0.0, 0);
	else
		KickBack(UTIL_SharedRandomLong(m_pPlayer->random_seed + 1, 7, 10), 0.0, 0.0, 0.0, 0.0, 0.0, 0);
#else
	if (m_pPlayer->pev->flags & FL_ONGROUND)
		m_pPlayer->pev->punchangle.x -= UTIL_SharedRandomLong(m_pPlayer->random_seed + 1, 3, 5);
	else
		m_pPlayer->pev->punchangle.x -= UTIL_SharedRandomLong(m_pPlayer->random_seed + 1, 7, 10);
#endif
}

void CXM1014::Reload()
{
	if (!DefaultShotgunReload(XM1014_RELOAD, XM1014_START_RELOAD, 0.3f, 0.55f, "weapons/reload1.wav", "weapons/reload3.wav"))
	{
		/* do nothing */
	}
}

void CXM1014::WeaponIdle()
{
	ResetEmptySound();
	m_pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);

#ifndef REGAMEDLL_FIXES
	if (m_flPumpTime && m_flPumpTime < UTIL_WeaponTimeBase())
	{
		m_flPumpTime = 0;
	}
#endif

	if (m_flTimeWeaponIdle < UTIL_WeaponTimeBase())
	{
		if (m_iClip == 0 && m_fInSpecialReload == 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
		{
			Reload();
		}
		else if (m_fInSpecialReload != 0)
		{
			if (m_iClip != iMaxClip() && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType])
			{
				Reload();
			}
			else
			{
				// reload debounce has timed out
				FinishShotgunReload(XM1014_PUMP,1.5f);
			}
		}
		else
		{
#ifdef REGAMEDLL_FIXES
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 20.0f;
#endif
			SendWeaponAnim(XM1014_IDLE, UseDecrement() != FALSE);
		}
	}
}

float CXM1014::GetMaxSpeed()
{
	return oc2_weapon::Get(oc2_weapon::XM1014).speed[oc2_weapon::Mode(5,m_iWeaponState,m_pPlayer?m_pPlayer->pev->fov:90)];
}
