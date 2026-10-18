// Presentation coordinates/textures: OC2 APK HUD draw routines (IDA evidence).
// Authoritative values remain CS1.6 messages; visibility/change rules follow
// CS:GO csgo_hudhealtharmor/money/weaponpanel, adapted to GoldSrc's HUD API.
#include "hud.h"
#include "cl_util.h"
#include "match_ui.h"
#include "match_status.h"
#include "kill_feedback.h"
#include "pm_shared.h"
#include "hud_readability.h"
#include <stdio.h>

namespace match_status
{
namespace
{
struct Snapshot
{
	int health = 0, armor = 0, money = 0, clip = -1, reserve = -1, secondary = -1, owner = 0;
	float healthTime = -1, armorTime = -1, moneyTime = -1, ammoTime = -1, damageUntil = -1;
} values;
cvar_t *bottomAvatar = nullptr;

int LocalTeam()
{
	hud_player_info_t info{};
	const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	const int slot = g_iUser1 == OBS_IN_EYE ? g_iUser2 : local ? local->index : 0;
	if (slot >= 1 && slot <= MAX_PLAYERS)
	{
		if (g_PlayerExtraInfo[slot].teamname[0]) return g_PlayerExtraInfo[slot].teamnumber;
		GetPlayerInfo(slot, &info);
	}
	return info.team;
}

match_ui::Color Theme()
{
	match_ui::Color color = LocalTeam() == TEAM_CT
		? match_ui::Color(115, 161, 209) : match_ui::Color(237, 199, 97);
	const float flash = HUD_LocalKillFlash(gHUD.m_flTime);
	color.r += int((255 - color.r) * flash);
	color.g += int((255 - color.g) * flash);
	color.b += int((255 - color.b) * flash);
	return color;
}
}

void Init()
{
	match_ui::Init();
	bottomAvatar = CVAR_CREATE("cl_bottom_avatar", "0", FCVAR_ARCHIVE);
	gEngfuncs.pfnAddCommand("match_ui_dump", Dump);
	Reset();
}

void Reset() { values = Snapshot(); }

void Health(int value, int player, float time)
{
	value = max(0, value);
	if (values.owner == player && values.healthTime >= 0 && value < values.health)
		values.damageUntil = time + 1.0f; // CS:GO healtharmor: same owner, one-second damage flash.
	if (values.owner != player || time < values.healthTime) values.damageUntil = -1;
	values.owner = player;
	values.health = value;
	values.healthTime = time;
	const float s = hud_readability::Scale(match_ui::Scale()), a = hud_readability::AnchorScale(ScreenWidth,match_ui::Scale());
	const float cx = ScreenWidth * 0.5f, h = float(ScreenHeight),healthX=cx-372*a;
	auto color = value <= 20 ? match_ui::Color(255, 70, 70) : Theme();
	if (time < values.damageUntil)
	{
		const float f = min(1.0f, values.damageUntil - time);
		color.r += int((255-color.r)*f); color.g += int((255-color.g)*f); color.b += int((255-color.b)*f);
	}
	// OC2 bottom watermark/team emblem: geometry, not a fabricated gameplay state.
	// Card is behind the badge, matching the reference's card-emerges-from-ring composition.
	HUD_DrawKillCard(time);
	const float emblemScale = match_ui::Scale();
	match_ui::Picture(match_ui::Texture("gfx/vgui/bgg.tga"), cx-345*emblemScale, h-405*emblemScale, 690*emblemScale, 690*emblemScale, Theme());
	const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	if (bottomAvatar && bottomAvatar->value != 0 && local && player == local->index)
		match_ui::Circle(gHUD.m_Avatars.AvatarTexture(player), cx-40*emblemScale, h-100*emblemScale, 80*emblemScale);
	else
		match_ui::Circle(match_ui::Texture(LocalTeam() == TEAM_CT ? "gfx/vgui/icon_ct.tga" : "gfx/vgui/icon_t.tga"),
			cx-40*emblemScale, h-100*emblemScale, 80*emblemScale, Theme());
	const int glowAlpha = min(255, max(0, int((100-value)*2.55f)));
	match_ui::Picture(match_ui::Texture("gfx/vgui/healthBg.tga"), healthX-60*s, h-119*s, 120*s, 120*s,
		match_ui::Color(255, 0, 0, glowAlpha));
	const float height = 42*s;
	const float advanceWidth = match_ui::NumberWidth(value, height,1,32*s);
	match_ui::Number(value, healthX-advanceWidth/2, h-80*s, height, color,false,1,32*s);
	FillRGBABlend(int(healthX-54*s), int(h-26*s), int(108*s), max(2, int(6*s)), 30, 30, 30, 190);
	FillRGBABlend(int(healthX-54*s), int(h-26*s), int(108*s*min(value,100)/100.0f), max(2, int(6*s)), color.r, color.g, color.b, 255);
}

void Armor(int value)
{
	values.armor = max(0, value); values.armorTime = gHUD.m_flTime;
	if (value <= 0) return;
	const float s = hud_readability::Scale(match_ui::Scale()), a = hud_readability::AnchorScale(ScreenWidth,match_ui::Scale());
	match_ui::Number(value, ScreenWidth*0.5f-440*a, ScreenHeight-65*s, 20*s, Theme(), true);
}

void Money(int value, int delta, float fade, bool blink)
{
	values.money = value; values.moneyTime = gHUD.m_flTime;
	const float s = hud_readability::Scale(match_ui::Scale()), x = 57*s, y = ScreenHeight-70*s;
	const auto color = blink ? match_ui::Color(255,70,70) : Theme();
	match_ui::Picture(match_ui::Texture("gfx/vgui/dollar.tga"), 37*s, y, 26*s, 26*s, color);
	// The original atlas has a transparent edge: its first number cell starts at 57.
	match_ui::Number(max(0,value), x, y, 26*s, color, false, 1, 20*s);
	if (gHUD.m_StatusIcons.IsIconActive("buyzone"))
		match_ui::Picture(match_ui::Texture("gfx/vgui/cart.tga"), x+match_ui::NumberWidth(max(0,value),26*s,1,20*s)+12*s,
			y, 26*s, 26*s, match_ui::Color());
	if (delta && fade > 0)
	{
		auto change = delta > 0 ? match_ui::Color(120,220,135) : match_ui::Color(255,90,90);
		change.a = int(min(1.0f, fade/5.0f)*255);
		match_ui::Text(delta > 0 ? "+" : "-", 37*s, y-32*s, 22*s, change);
		const int absolute = delta == (-2147483647-1) ? 2147483647 : (delta < 0 ? -delta : delta);
		match_ui::Number(absolute, x, y-32*s, 22*s, change, false, 1, 16*s);
	}
}

void Ammo(int clip, int reserve, int secondary)
{
	values.clip = clip; values.reserve = reserve; values.secondary = secondary; values.ammoTime = gHUD.m_flTime;
	const float s = hud_readability::Scale(match_ui::Scale()), a = hud_readability::AnchorScale(ScreenWidth,match_ui::Scale());
	const float cx = ScreenWidth*0.5f, h = float(ScreenHeight);
	const auto color = Theme();
	if (reserve >= 0)
	{
		if (clip >= 0)
		{
			match_ui::Number(clip, cx+390*a, h-80*s, 42*s, color, true,1,32*s);
			match_ui::Number(reserve, cx+462*a, h-70*s, 20*s, color, true, 1, 15*s);
		}
		else match_ui::Number(reserve, cx+390*a, h-80*s, 42*s, color, true,1,32*s);
	}
	if (secondary > 0) match_ui::Number(secondary, cx+462*a, h-102*s, 20*s, color, true, 1, 15*s);
}

void Dump()
{
	gEngfuncs.Con_Printf("matchui: t=%.3f enabled=%d screen=%dx%d scale=%.3f owner=%d hp=%d armor=%d money=%d clip=%d reserve=%d secondary=%d times=%.3f,%.3f,%.3f,%.3f\n",
		gHUD.m_flTime, match_ui::Enabled(), ScreenWidth, ScreenHeight, match_ui::Scale(), values.owner,
		values.health, values.armor, values.money, values.clip, values.reserve, values.secondary,
		values.healthTime, values.armorTime, values.moneyTime, values.ammoTime);
	const float s=hud_readability::Scale(match_ui::Scale()),a=hud_readability::AnchorScale(ScreenWidth,match_ui::Scale());
	gEngfuncs.Con_Printf("matchuilayout: size_scale=%.4f anchor_scale=%.4f hp_height=%.3f armor_height=%.3f money_height=%.3f ammo_height=%.3f badge=%.3f hp_center=%.3f armor_right=%.3f ammo_right=%.3f\n",
		s,a,42*s,20*s,26*s,42*s,80*match_ui::Scale(),ScreenWidth*.5f-372*a,ScreenWidth*.5f-440*a,ScreenWidth*.5f+390*a);
	HUD_KillFeedbackDump();
}
}
