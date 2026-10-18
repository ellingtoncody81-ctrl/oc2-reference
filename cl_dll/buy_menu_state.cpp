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
enum Kind { Gun, Grenade, Vest, VestHelmet, Defuser, Zeus };
struct Entry
{
	int id, column;
	const char *name, *alias, *image;
	int price;
	Kind kind;
};

// Gun constants in dlls/weapontype.h match ReGameDLL's g_weaponInfo_default
// (weapontype.cpp:222). Equipment prices follow its BuyItem path. Artwork
// has baked CS2 prices/names, which the menu replaces using these values.
// Stable indices are intentional: a team change must not remap a click.
const Entry catalog[] = {
	{1001,0,"Kevlar","vest","gfx/weaponbuy/kevlar.tga",650,Vest},
	{1002,0,"Kevlar + Helmet","vesthelm","gfx/weaponbuy/kevlarhelmet.tga",1000,VestHelmet},
	{1003,0,"Defuse Kit","defuser","gfx/weaponbuy/defusekit.tga",200,Defuser},
	{WEAPON_TASER,0,"Zeus x27","zeus","gfx/weaponbuy/taser.tga",TASER_PRICE,Zeus},
	{WEAPON_GLOCK18,1,"Glock-18","glock","gfx/weaponbuy/weapon_glock.tga",GLOCK18_PRICE,Gun},
	{WEAPON_USP,1,"USP-S","usp","gfx/weaponbuy/weapon_usp.tga",USP_PRICE,Gun},
	{WEAPON_P228,1,"P250","p250","gfx/weaponbuy/weapon_p228.tga",P228_PRICE,Gun},
	{WEAPON_DEAGLE,1,"Desert Eagle","deagle","gfx/weaponbuy/weapon_deagle.tga",DEAGLE_PRICE,Gun},
	{WEAPON_ELITE,1,"Dual Berettas","elites","gfx/weaponbuy/weapon_elites.tga",ELITE_PRICE,Gun},
	{WEAPON_FIVESEVEN,1,"Five-SeveN","fn57","gfx/weaponbuy/weapon_fiveseven.tga",FIVESEVEN_PRICE,Gun},
	{WEAPON_MAC10,2,"MAC-10","mac10","gfx/weaponbuy/weapon_mac10.tga",MAC10_PRICE,Gun},
	{WEAPON_TMP,2,"MP9","mp9","gfx/weaponbuy/weapon_tmp.tga",TMP_PRICE,Gun},
	{WEAPON_MP5N,2,"MP7","mp7","gfx/weaponbuy/weapon_mp7.tga",MP5NAVY_PRICE,Gun},
	{WEAPON_UMP45,2,"UMP-45","ump45","gfx/weaponbuy/weapon_ump45.tga",UMP45_PRICE,Gun},
	{WEAPON_P90,2,"P90","p90","gfx/weaponbuy/weapon_p90.tga",P90_PRICE,Gun},
	{WEAPON_M3,2,"Nova","nova","gfx/weaponbuy/weapon_m3.tga",M3_PRICE,Gun},
	{WEAPON_XM1014,2,"XM1014","xm1014","gfx/weaponbuy/weapon_xm1014.tga",XM1014_PRICE,Gun},
	{WEAPON_M249,2,"M249","m249","gfx/weaponbuy/weapon_m249.tga",M249_PRICE,Gun},
	{WEAPON_GALIL,3,"Galil AR","galil","gfx/weaponbuy/weapon_galil.tga",GALIL_PRICE,Gun},
	{WEAPON_FAMAS,3,"FAMAS","famas","gfx/weaponbuy/weapon_famas.tga",FAMAS_PRICE,Gun},
	{WEAPON_AK47,3,"AK-47","ak47","gfx/weaponbuy/weapon_ak47.tga",AK47_PRICE,Gun},
	{WEAPON_M4A1,3,"M4A1-S","m4a1","gfx/weaponbuy/weapon_m4a1.tga",M4A1_PRICE,Gun},
	{WEAPON_SG552,3,"SG553","sg553","gfx/weaponbuy/weapon_sg552.tga",SG552_PRICE,Gun},
	{WEAPON_AUG,3,"AUG","aug","gfx/weaponbuy/weapon_aug.tga",AUG_PRICE,Gun},
	{WEAPON_SCOUT,3,"SSG 08","scout","gfx/weaponbuy/weapon_scout.tga",SCOUT_PRICE,Gun},
	{WEAPON_AWP,3,"AWP","awp","gfx/weaponbuy/weapon_awp.tga",AWP_PRICE,Gun},
	{WEAPON_G3SG1,3,"G3SG1","g3sg1","gfx/weaponbuy/weapon_g3sg1.tga",G3SG1_PRICE,Gun},
	{WEAPON_SG550,3,"SCAR-20","sg550","gfx/weaponbuy/weapon_sg550.tga",SG550_PRICE,Gun},
	{WEAPON_FLASHBANG,4,"Flashbang","flash","gfx/weaponbuy/flashbang.tga",200,Grenade},
	{WEAPON_SMOKEGRENADE,4,"Smoke Grenade","sgren","gfx/weaponbuy/smoke.tga",300,Grenade},
	{WEAPON_HEGRENADE,4,"HE Grenade","hegren","gfx/weaponbuy/hegren.tga",300,Grenade},
	{WEAPON_MOLOTOV,4,"Molotov","molotov","gfx/weaponbuy/molly.tga",MOLOTOV_PRICE,Grenade},
};
const int kItems = sizeof(catalog)/sizeof(catalog[0]);
bool opened = false, hasServerState = false, serverCanBuy = false, hasItemStatus = false;
bool hasEquipmentState = false;
int equipmentVersion = 0, zeusBought = 0, equipmentMessages = 0;
int fireVariant = 0;
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
	return id > 0 && id < MAX_WEAPONS && (uint32_t(gHUD.m_iWeaponBits) & (1U << id));
}

const WEAPON *Weapon(int id)
{
	const WEAPON *weapon = gWR.GetWeapon(id);
	return weapon && weapon->iId == id && weapon->szName[0] ? weapon : nullptr;
}

int Ammo(const WEAPON *weapon)
{
	return weapon && weapon->iAmmoType >= 0 && weapon->iAmmoType < MAX_AMMO_TYPES
		? max(0,gWR.CountAmmo(weapon->iAmmoType)) : 0;
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
	if (!hasEquipmentState) state.reason = "Server equipment protocol required";
	else if (!hasServerState) state.reason = "Waiting for server buy state";
	else if (state.team != TEAM_TERRORIST && state.team != TEAM_CT) state.reason = "Choose a team";
	else if (!state.alive) state.reason = "Must be alive";
	else
	{
		// BuyState's eligibility is authoritative, including the exact deadline
		// instant. A displayed zero is not a substitute for CanPlayerBuy(false).
		state.canBuy = serverCanBuy;
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
	// One stable slot offers the team's own fire grenade. Inventory remains a
	// physical item and may contain the other variant after a world pickup.
	if (entry.id == WEAPON_MOLOTOV && state.team == TEAM_CT)
	{
		item.name = "Incendiary Grenade";
		item.image = "gfx/weaponbuy/incendiary.tga";
		item.price = 600;
	}
	item.visible = (state.team == TEAM_TERRORIST || state.team == TEAM_CT)
		&& (entry.kind != Defuser || state.team == TEAM_CT);
	const int armor = max(0,gHUD.m_Battery.GetArmor());
	const bool helmet = gHUD.m_Battery.HasHelmet();
	hud_player_info_t info{}; const int local = LocalSlot(info);
	switch (entry.kind)
	{
	case Gun: case Zeus: item.owned = Owned(entry.id); item.count = item.owned ? 1 : 0; break;
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
	}

	if (!item.visible) item.reason = entry.kind == Defuser ? "CT equipment only" : "Choose a team";
	else if (!state.canBuy) item.reason = state.reason;
	else if (entry.id == WEAPON_ELITE && g_bHoldingShield)
		item.reason = "Conflicts with carried equipment";
	else if (entry.kind == Zeus && zeusBought >= 1) item.reason = "Round purchase limit reached";
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
	oc2_buy::State state; GetState(state);
	const char *alias = item.id == WEAPON_MOLOTOV && state.team == TEAM_CT ? "incendiary" : catalog[index].alias;
	gEngfuncs.Con_Printf("buymenu: purchase index=%d id=%d alias=%s price=%d money=%d allowed=%d reason=%s\n",
		index,item.id,alias,item.price,gHUD.m_Money.GetMoney(),int(item.enabled),item.reason);
	if (!item.enabled) return false;
	// Catalog aliases are compile-time constants. Never concatenate user text,
	// optimistically deduct money, or use CS:GO's incompatible buy-slot command.
	gEngfuncs.pfnServerCmd(alias);
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
	gEngfuncs.Con_Printf("buyequipment: authoritative=%d version=%d zeus_bought=%d messages=%d fire_variant=%d\n",
		int(hasEquipmentState),equipmentVersion,zeusBought,equipmentMessages,fireVariant);
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
		gEngfuncs.pfnHookUserMsg("Equipment",EquipmentMessage);
		gEngfuncs.pfnHookUserMsg("FireVariant",FireVariantMessage);
		gEngfuncs.pfnAddCommand("buy_menu_dump",Dump);
		initialized = true;
	}
	ClearMap();
}

void Close()
{
	const bool wasOpen = opened;
	opened = false;
	if (wasOpen) gEngfuncs.pfnServerCmd("oc2_buymenu_close\n");
	if (wasOpen && g_pMenu) g_pMenu->HideVGUIMenu();
}

void ClearMap()
{
	// A map/reset is cancellation, never a purchase deploy on a new connection.
	const bool wasOpen = opened;
	opened = false;
	if (wasOpen && g_pMenu) g_pMenu->HideVGUIMenu();
	hasServerState = serverCanBuy = hasItemStatus = false;
	serverSeconds = -2; itemStatus = stateMessages = 0; stateTime = -1.0f;
	hasEquipmentState = false; equipmentVersion = zeusBought = equipmentMessages = 0;
	fireVariant = 0;
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

int EquipmentMessage(const char *name, int size, void *buffer)
{
	if (size != 2 || !buffer)
	{
		hasEquipmentState = false;
		Close();
		return 0;
	}
	BufferReader reader(name,buffer,size);
	const int version = reader.ReadByte(), purchases = reader.ReadByte();
	if (reader.Bad() || version != 2)
	{
		hasEquipmentState = false; equipmentVersion = version;
		Close();
		return 0;
	}
	hasEquipmentState = true; equipmentVersion = version; zeusBought = purchases;
	++equipmentMessages;
	return 1;
}

int FireVariantMessage(const char *name, int size, void *buffer)
{
	if (size != 1 || !buffer) return 0;
	BufferReader reader(name,buffer,size);
	const int variant = reader.ReadByte();
	if (reader.Bad() || variant > 1) return 0;
	fireVariant = variant;
	return 1;
}

bool FireIsIncendiary() { return fireVariant == 1; }

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
		const bool next = open && ::GetState(state) && state.canBuy;
		if (next == opened) return;
		opened = next;
		gEngfuncs.pfnServerCmd(next ? "oc2_buymenu_open\n" : "oc2_buymenu_close\n");
	}
};
EXPOSE_SINGLE_INTERFACE(COc2BuyMenu,IOc2BuyMenu,OC2_BUYMENU_INTERFACE_VERSION)
