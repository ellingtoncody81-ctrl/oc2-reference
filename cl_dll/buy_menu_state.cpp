// CS:GO csgo_buymenu.cpp:219,438,849,936: display real state, send a
// purchase request, and close when CanPlayerBuy becomes false. GoldSrc's
// server remains responsible for inventory changes, prices and deductions.
#include "hud.h"
#include "cl_util.h"
#include "ammohistory.h"
#include "com_weapons.h"
#include "parsemsg.h"
#include "weapontype.h"
#include "cl_dll/IOc2BuyMenu.h"
#include "buy_menu_state.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
enum Kind { Gun, Grenade, Vest, VestHelmet, Defuser, NightVision, Shield, PrimaryAmmo, SecondaryAmmo };
enum { kT = 1, kCT = 2, kBoth = 3 };
struct Entry
{
	int id, column;
	const char *name, *alias, *image;
	int price, teams;
	Kind kind;
};

// Gun constants in dlls/weapontype.h match ReGameDLL's g_weaponInfo_default
// (weapontype.cpp:222). Equipment prices follow its BuyItem path. Artwork
// has baked CS2 prices/names, which the menu replaces using these values.
// Stable indices are intentional: a team change must not remap a click.
const Entry catalog[] = {
	{1001,0,"Kevlar","vest","gfx/weaponbuy/kevlar.tga",650,kBoth,Vest},
	{1002,0,"Kevlar + Helmet","vesthelm","gfx/weaponbuy/kevlarhelmet.tga",1000,kBoth,VestHelmet},
	{1003,0,"Defuse Kit","defuser","gfx/weaponbuy/defusekit.tga",200,kCT,Defuser},
	{1004,0,"Night Vision","nvgs","",1250,kBoth,NightVision},
	{WEAPON_SHIELDGUN,0,"Tactical Shield","shield","",SHIELDGUN_PRICE,kCT,Shield},
	{1005,0,"Primary Ammo","buyammo1","",0,kBoth,PrimaryAmmo},
	{1006,0,"Pistol Ammo","buyammo2","",0,kBoth,SecondaryAmmo},
	{WEAPON_GLOCK18,1,"Glock-18","glock","gfx/weaponbuy/weapon_glock.tga",GLOCK18_PRICE,kBoth,Gun},
	{WEAPON_USP,1,"USP","usp","gfx/weaponbuy/weapon_usp.tga",USP_PRICE,kBoth,Gun},
	{WEAPON_P228,1,"P228","p228","gfx/weaponbuy/weapon_p228.tga",P228_PRICE,kBoth,Gun},
	{WEAPON_DEAGLE,1,"Desert Eagle","deagle","gfx/weaponbuy/weapon_deagle.tga",DEAGLE_PRICE,kBoth,Gun},
	{WEAPON_ELITE,1,"Dual Elites","elites","gfx/weaponbuy/weapon_elites.tga",ELITE_PRICE,kT,Gun},
	{WEAPON_FIVESEVEN,1,"Five-SeveN","fn57","gfx/weaponbuy/weapon_fiveseven.tga",FIVESEVEN_PRICE,kCT,Gun},
	{WEAPON_MAC10,2,"MAC-10","mac10","gfx/weaponbuy/weapon_mac10.tga",MAC10_PRICE,kT,Gun},
	{WEAPON_TMP,2,"TMP","tmp","gfx/weaponbuy/weapon_tmp.tga",TMP_PRICE,kCT,Gun},
	{WEAPON_MP5N,2,"MP5","mp5","gfx/weaponbuy/weapon_mp5.tga",MP5NAVY_PRICE,kBoth,Gun},
	{WEAPON_UMP45,2,"UMP-45","ump45","gfx/weaponbuy/weapon_ump45.tga",UMP45_PRICE,kBoth,Gun},
	{WEAPON_P90,2,"P90","p90","gfx/weaponbuy/weapon_p90.tga",P90_PRICE,kBoth,Gun},
	{WEAPON_M3,2,"M3","m3","gfx/weaponbuy/weapon_m3.tga",M3_PRICE,kBoth,Gun},
	{WEAPON_XM1014,2,"XM1014","xm1014","gfx/weaponbuy/weapon_xm1014.tga",XM1014_PRICE,kBoth,Gun},
	{WEAPON_M249,2,"M249","m249","gfx/weaponbuy/weapon_m249.tga",M249_PRICE,kBoth,Gun},
	{WEAPON_GALIL,3,"Galil","galil","gfx/weaponbuy/weapon_galil.tga",GALIL_PRICE,kT,Gun},
	{WEAPON_FAMAS,3,"FAMAS","famas","gfx/weaponbuy/weapon_famas.tga",FAMAS_PRICE,kCT,Gun},
	{WEAPON_AK47,3,"AK-47","ak47","gfx/weaponbuy/weapon_ak47.tga",AK47_PRICE,kT,Gun},
	{WEAPON_M4A1,3,"M4A1","m4a1","gfx/weaponbuy/weapon_m4a1.tga",M4A1_PRICE,kCT,Gun},
	{WEAPON_SG552,3,"SG552","sg552","gfx/weaponbuy/weapon_sg552.tga",SG552_PRICE,kT,Gun},
	{WEAPON_AUG,3,"AUG","aug","gfx/weaponbuy/weapon_aug.tga",AUG_PRICE,kCT,Gun},
	{WEAPON_SCOUT,3,"Scout","scout","gfx/weaponbuy/weapon_scout.tga",SCOUT_PRICE,kBoth,Gun},
	{WEAPON_AWP,3,"AWP","awp","gfx/weaponbuy/weapon_awp.tga",AWP_PRICE,kBoth,Gun},
	{WEAPON_G3SG1,3,"G3SG1","g3sg1","gfx/weaponbuy/weapon_g3sg1.tga",G3SG1_PRICE,kT,Gun},
	{WEAPON_SG550,3,"SG550","sg550","gfx/weaponbuy/weapon_sg550.tga",SG550_PRICE,kCT,Gun},
	{WEAPON_FLASHBANG,4,"Flashbang","flash","gfx/weaponbuy/flashbang.tga",200,kBoth,Grenade},
	{WEAPON_SMOKEGRENADE,4,"Smoke Grenade","sgren","gfx/weaponbuy/smoke.tga",300,kBoth,Grenade},
	{WEAPON_HEGRENADE,4,"HE Grenade","hegren","gfx/weaponbuy/hegren.tga",300,kBoth,Grenade},
};
const int kItems = sizeof(catalog)/sizeof(catalog[0]);
bool opened = false, hasServerState = false, serverCanBuy = false, hasItemStatus = false;
int serverSeconds = -2, itemStatus = 0, stateMessages = 0;
float stateTime = -1.0f;

int LocalSlot(hud_player_info_t &info)
{
	const auto *entity = gEngfuncs.GetLocalPlayer();
	const char *level = gEngfuncs.pfnGetLevelName();
	if (!entity || entity->index < 1 || entity->index > MAX_PLAYERS || !level || !*level) return 0;
	GetPlayerInfo(entity->index,&info);
	return info.name && *info.name ? entity->index : 0;
}

bool Owned(int id)
{
	return id > 0 && id < MAX_WEAPONS && (uint32_t(gHUD.m_iWeaponBits) & (uint32_t(1)<<id));
}

const WEAPON *Weapon(int id)
{
	const WEAPON *weapon = gWR.GetWeapon(id);
	return weapon && weapon->iId == id ? weapon : nullptr;
}

int Ammo(const WEAPON *weapon)
{
	return weapon && weapon->iAmmoType >= 0 && weapon->iAmmoType < MAX_AMMO_TYPES
		? max(0,gWR.CountAmmo(weapon->iAmmoType)) : 0;
}

const WEAPON *SlotWeapon(int slot)
{
	for (int pos = 0; pos < MAX_WEAPON_POSITIONS; ++pos)
	{
		const WEAPON *weapon = gWR.GetWeaponSlot(slot,pos);
		if (weapon && Owned(weapon->iId)) return weapon;
	}
	return nullptr;
}

int AmmoPrice(int id)
{
	switch (id)
	{
	case WEAPON_AWP: return AMMO_338MAG_PRICE;
	case WEAPON_AK47: case WEAPON_SCOUT: case WEAPON_G3SG1: return AMMO_762MM_PRICE;
	case WEAPON_M3: case WEAPON_XM1014: return AMMO_BUCKSHOT_PRICE;
	case WEAPON_M249: case WEAPON_AUG: case WEAPON_SG550: case WEAPON_GALIL:
	case WEAPON_FAMAS: case WEAPON_M4A1: case WEAPON_SG552: return AMMO_556MM_PRICE;
	case WEAPON_P228: return AMMO_357SIG_PRICE;
	case WEAPON_USP: case WEAPON_MAC10: case WEAPON_UMP45: return AMMO_45ACP_PRICE;
	case WEAPON_DEAGLE: return AMMO_50AE_PRICE;
	case WEAPON_FIVESEVEN: case WEAPON_P90: return AMMO_57MM_PRICE;
	case WEAPON_GLOCK18: case WEAPON_ELITE: case WEAPON_MP5N: case WEAPON_TMP: return AMMO_9MM_PRICE;
	default: return 0;
	}
}

bool GetState(oc2_buy::State &state)
{
	state = oc2_buy::State();
	hud_player_info_t info{};
	const int slot = LocalSlot(info);
	if (!slot) { state.reason = "Not connected"; return false; }
	state.team = g_PlayerExtraInfo[slot].teamname[0] ? g_PlayerExtraInfo[slot].teamnumber : info.team;
	state.money = max(0,gHUD.m_Money.GetMoney());
	state.alive = g_iUser1 == 0 && gHUD.m_Health.m_iHealth > 0 && !g_PlayerExtraInfo[slot].dead;
	state.inBuyZone = gHUD.m_StatusIcons.IsIconActive("buyzone");
	state.seconds = serverSeconds;
	if (hasServerState && serverSeconds >= 0)
	{
		const float now = gEngfuncs.GetClientTime();
		const float elapsed = std::isfinite(now) && now >= stateTime ? now-stateTime : 0.0f;
		state.seconds = max(0,serverSeconds-int(min(elapsed,32767.0f)));
	}
	if (state.team != TEAM_TERRORIST && state.team != TEAM_CT) state.reason = "Choose a team";
	else if (!state.alive) state.reason = "Must be alive";
	else
	{
		// BuyState's eligibility is authoritative, including the exact deadline
		// instant. A displayed zero is not a substitute for CanPlayerBuy(false).
		state.canBuy = hasServerState ? serverCanBuy : state.inBuyZone;
		if (!state.canBuy)
			state.reason = !state.inBuyZone ? "Not in buy zone"
				: hasServerState && state.seconds == 0 ? "Buy time expired" : "Purchases disabled by server";
	}
	return true;
}

bool GetItem(int index, oc2_buy::Item &item)
{
	item = oc2_buy::Item();
	if (index < 0 || index >= kItems) return false;
	const Entry &entry = catalog[index];
	oc2_buy::State state; GetState(state);
	item.id = entry.id; item.column = entry.column; item.name = entry.name;
	item.image = entry.image; item.price = entry.price; item.limit = 1;
	item.visible = (state.team == TEAM_TERRORIST || state.team == TEAM_CT) && (entry.teams & state.team);
	const int armor = max(0,gHUD.m_Battery.GetArmor());
	const bool helmet = gHUD.m_Battery.HasHelmet();
	hud_player_info_t info{}; const int local = LocalSlot(info);
	switch (entry.kind)
	{
	case Gun: item.owned = Owned(entry.id); item.count = item.owned ? 1 : 0; break;
	case Grenade:
		item.count = Ammo(Weapon(entry.id));
		item.limit = entry.id == WEAPON_FLASHBANG ? MAX_AMMO_FLASHBANG : 1;
		item.owned = item.count > 0; break;
	case Vest:
		item.count = armor; item.limit = 100; item.owned = armor >= 100; break;
	case VestHelmet:
		item.count = armor; item.limit = 100; item.owned = armor >= 100 && helmet;
		// ReGameDLL BuyItem: full vest needs only a helmet; a damaged vest
		// with a helmet pays for kevlar, not a second helmet.
		item.price = armor >= 100 && !helmet ? 350 : helmet ? 650 : 1000;
		break;
	case Defuser:
		item.owned = hasItemStatus ? (itemStatus & 2) != 0 : local && g_PlayerExtraInfo[local].has_defuse_kit;
		item.count = item.owned ? 1 : 0; break;
	case NightVision: item.owned = (itemStatus & 1) != 0; item.count = item.owned ? 1 : 0; break;
	case Shield: item.owned = g_bHoldingShield; item.count = item.owned ? 1 : 0; break;
	case PrimaryAmmo: case SecondaryAmmo:
	{
		const WEAPON *weapon = SlotWeapon(entry.kind == PrimaryAmmo ? 0 : 1);
		item.price = weapon ? AmmoPrice(weapon->iId) : 0;
		item.count = Ammo(weapon); item.limit = weapon ? max(0,weapon->iMax1) : 0;
		item.owned = item.limit > 0 && item.count >= item.limit;
		if (!weapon || item.price <= 0) item.reason = "No matching weapon";
		break;
	}
	}
	if (!state.canBuy) item.reason = state.reason;
	else if (!(entry.teams & state.team)) item.reason = "Unavailable for this team";
	else if ((entry.id == WEAPON_ELITE && g_bHoldingShield) || (entry.kind == Shield && Owned(WEAPON_ELITE)))
		item.reason = "Conflicts with carried equipment";
	else if (entry.kind == Grenade && item.count >= item.limit) item.reason = "Carry limit reached";
	else if (entry.kind != Grenade && item.owned) item.reason = "Already owned";
	else if (state.money < item.price) item.reason = "Not enough money";
	item.enabled = !item.reason[0];
	return true;
}

bool Purchase(int index)
{
	oc2_buy::Item item;
	if (!GetItem(index,item)) return false;
	gEngfuncs.Con_Printf("buymenu: purchase index=%d id=%d alias=%s price=%d money=%d allowed=%d reason=%s\n",
		index,item.id,catalog[index].alias,item.price,gHUD.m_Money.GetMoney(),int(item.enabled),item.reason);
	if (!item.enabled) return false;
	// Catalog aliases are compile-time constants. Never concatenate user text,
	// optimistically deduct money, or use CS:GO's incompatible buy-slot command.
	gEngfuncs.pfnServerCmd(catalog[index].alias);
	return true;
}

void Dump()
{
	if (gEngfuncs.Cmd_Argc() >= 3 && !strcmp(gEngfuncs.Cmd_Argv(1),"purchase"))
	{
		char *end = nullptr; const long index = std::strtol(gEngfuncs.Cmd_Argv(2),&end,10);
		if (end && !*end && index >= 0 && index < kItems) Purchase(int(index));
		else gEngfuncs.Con_Printf("buymenu: invalid purchase index\n");
	}
	oc2_buy::State state; GetState(state);
	gEngfuncs.Con_Printf("buymenu: open=%d team=%d money=%d alive=%d zone=%d can=%d seconds=%d authoritative=%d messages=%d items=%d reason=%s\n",
		int(opened),state.team,state.money,int(state.alive),int(state.inBuyZone),int(state.canBuy),state.seconds,
		int(hasServerState),stateMessages,kItems,state.reason);
	if (gEngfuncs.Cmd_Argc() >= 2 && !strcmp(gEngfuncs.Cmd_Argv(1),"items"))
		for (int i = 0; i < kItems; ++i)
		{
			oc2_buy::Item item; GetItem(i,item);
			gEngfuncs.Con_Printf("buyitem: index=%d id=%d column=%d name=%s price=%d count=%d limit=%d visible=%d owned=%d enabled=%d reason=%s\n",
				i,item.id,item.column,item.name,item.price,item.count,item.limit,int(item.visible),int(item.owned),int(item.enabled),item.reason);
		}
}
}

namespace buy_menu_state
{
void Init()
{
	static bool initialized = false;
	if (!initialized)
	{
		gEngfuncs.pfnHookUserMsg("BuyState",Message);
		gEngfuncs.pfnAddCommand("buy_menu_dump",Dump);
		initialized = true;
	}
	ClearMap();
}

void Close()
{
	const bool wasOpen = opened;
	opened = false;
	if (wasOpen && g_pMenu) g_pMenu->HideVGUIMenu();
}

void ClearMap()
{
	Close();
	hasServerState = serverCanBuy = hasItemStatus = false;
	serverSeconds = -2; itemStatus = stateMessages = 0; stateTime = -1.0f;
}

void Think()
{
	if (!opened) return;
	oc2_buy::State state;
	if (!GetState(state) || !state.canBuy) Close();
}

bool IsOpen() { return opened; }

int Message(const char *name, int size, void *buffer)
{
	if (size != 3 || !buffer) return 0;
	BufferReader reader(name,buffer,size);
	const int allowed = reader.ReadByte(), seconds = reader.ReadShort();
	if (reader.Bad() || allowed > 1 || seconds < -1) return 0;
	hasServerState = true; serverCanBuy = allowed != 0;
	serverSeconds = seconds; stateTime = gEngfuncs.GetClientTime(); ++stateMessages;
	return 1;
}

int ItemStatusMessage(const char *name, int size, void *buffer)
{
	if (size != 1 || !buffer) return 0;
	BufferReader reader(name,buffer,size);
	itemStatus = reader.ReadByte(); hasItemStatus = !reader.Bad();
	return hasItemStatus ? 1 : 0;
}
}

class COc2BuyMenu : public IOc2BuyMenu
{
public:
	bool GetState(oc2_buy::State &state) override { return ::GetState(state); }
	int ItemCount() override { return kItems; }
	bool GetItem(int index, oc2_buy::Item &item) override { return ::GetItem(index,item); }
	bool Purchase(int index) override { return ::Purchase(index); }
	void SetOpen(bool open) override
	{
		oc2_buy::State state;
		opened = open && ::GetState(state) && state.canBuy;
	}
};
EXPOSE_SINGLE_INTERFACE(COc2BuyMenu,IOc2BuyMenu,OC2_BUYMENU_INTERFACE_VERSION)
