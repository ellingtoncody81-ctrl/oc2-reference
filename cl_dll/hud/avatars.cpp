// CS:GO behavior reference: panorama/hud/csgo_hudteamcounter.cpp
// UpdateMiniScoreboard (2279), UpdateAvatarSlot (1919); csgo_avatarimage.cpp (104).
// GoldSrc rendering and resource-configured portraits are platform adaptations.
#include "hud.h"
#include "cl_util.h"
#include "draw_util.h"
#include "triangleapi.h"
#include "render_api.h"
#include "match_ui.h"
#include "hud_readability.h"
#include <stdio.h>
#include <stdlib.h>

namespace
{
float s_avatarDrawScale=0,s_avatarDrawY=0;
int s_avatarDrawTSize=0,s_avatarDrawCTSize=0;
int TeamOf(const hud_player_info_t& info, int slot)
{
	if (info.spectator) return TEAM_SPECTATOR;
	if (g_PlayerExtraInfo[slot].teamname[0]) return g_PlayerExtraInfo[slot].teamnumber;
	return info.team >= 0 ? info.team : g_PlayerExtraInfo[slot].teamnumber;
}

int PortraitOf(int slot)
{
	// OC2 supplies the loadout_avatar setting and portraits 1..10. GoldSrc
	// publishes the selection through userinfo; no OC2 game protocol is copied.
	const char *value = gEngfuncs.PlayerInfo_ValueForKey(slot, "loadout_avatar");
	if (!value || !*value) return 0;
	char *end = nullptr;
	const long index = strtol(value, &end, 10);
	return end && !*end && index >= 1 && index <= 10 ? (int)index : 0;
}

void CenterText(int x, int y, const char *text, int r, int g, int b)
{
	const float height = gHUD.GetCharHeight();
	match_ui::Text(text, x - match_ui::TextWidth(text, height)*0.5f, y, height, {r, g, b});
}
}

int CHudAvatars::Init()
{
	match_ui::Init();
	gHUD.AddHudElem(this);
	m_enabled = CVAR_CREATE("hud_avatars", "1", FCVAR_ARCHIVE);
	CVAR_CREATE("loadout_avatar", "0", FCVAR_ARCHIVE | FCVAR_USERINFO);
	// CS:GO c_cs_player.cpp:227-228: 0=top/avatars, 1=bottom/alive counts.
	m_position = CVAR_CREATE("cl_hud_playercount_pos", "0", FCVAR_ARCHIVE);
	m_showCount = CVAR_CREATE("cl_hud_playercount_showcount", "0", FCVAR_ARCHIVE);
	HOOK_COMMAND(gHUD.m_Avatars, "avatar_dump", AvatarDump);
	InitHUDData();
	return 1;
}

void CHudAvatars::InitHUDData()
{
	avatar_state::Clear(m_state);
	memset(m_portraits, 0, sizeof(m_portraits));
	memset(m_messages, 0, sizeof(m_messages));
	m_refreshTime = -1.0f;
	m_iFlags = HUD_DRAW | HUD_INTERMISSION;
}

bool CHudAvatars::Enabled() const
{
	return m_enabled && m_enabled->value != 0;
}

bool CHudAvatars::HasPlayers()
{
	Refresh(gHUD.m_flTime);
	return m_state.counter_terrorists.count || m_state.terrorists.count;
}

int CHudAvatars::VidInit()
{
	if (!gRenderAPI.GL_LoadTexture) return 1;
	const char *names[] = {"gfx/vgui/avatar_t.tga", "gfx/vgui/avatar_ct.tga", "gfx/vgui/avatar_dead.tga"};
	for (int i = 0; i < 13; ++i)
	{
		char path[80];
		if (i < 3) snprintf(path, sizeof(path), "%s", names[i]);
		else snprintf(path, sizeof(path), "gfx/vgui/avatar/avatar_%d.tga", i - 2);
		m_textures[i] = match_ui::Texture(path);
	}
	m_refreshTime = -1.0f;
	return 1;
}

void CHudAvatars::Shutdown()
{
	for (int& texture : m_textures)
	{
		texture = 0;
	}
	InitHUDData();
}

void CHudAvatars::NoteMessage(int kind, int player)
{
	if (kind >= 0 && kind < 3 && player > 0 && player <= MAX_PLAYERS) ++m_messages[kind];
	m_refreshTime = -1.0f;
}

void CHudAvatars::Refresh(float time)
{
	if (time == m_refreshTime) return;
	m_refreshTime = time;
	avatar_state::Snapshot snapshot{};
	const int limit = min(MAX_PLAYERS, gEngfuncs.GetMaxClients());
	for (int slot = 1; slot <= MAX_PLAYERS; ++slot)
	{
		m_portraits[slot] = 0;
		if (slot > limit) continue;
		hud_player_info_t info{};
		GetPlayerInfo(slot, &info);
		if (!info.name || !info.name[0]) continue;
		auto& player = snapshot.players[slot];
		player.connected = true;
		player.team = TeamOf(info, slot);
		player.dead = g_PlayerExtraInfo[slot].dead;
		player.local = info.thisplayer != 0;
		const char *userid = gEngfuncs.PlayerInfo_ValueForKey(slot, "*oc2_userid");
		// Connection userid, never an invented Steam identity. Older engines fall back to name+slot.
		player.identity = avatar_state::MakeIdentity(userid && *userid ? atoi(userid) : slot, info.name);
		m_portraits[slot] = PortraitOf(slot);
	}
	avatar_state::Update(m_state, snapshot);
}

void CHudAvatars::DrawTexture(int texture, int x, int y, int size, int brightness)
{
	match_ui::Picture(texture, x, y, size, size, {brightness, brightness, brightness}, true);
}

int CHudAvatars::AvatarTexture(int slot)
{
	if (slot < 1 || slot > MAX_PLAYERS) return 0;
	Refresh(gHUD.m_flTime);
	if (!m_state.players[slot].connected) return 0;
	const int portrait = m_portraits[slot];
	if (portrait && m_textures[portrait+2]) return m_textures[portrait+2];
	return m_state.players[slot].team == TEAM_CT ? m_textures[1]
		: m_state.players[slot].team == TEAM_TERRORIST ? m_textures[0] : 0;
}

void CHudAvatars::DrawPlayerAvatar(int slot, int x, int y, int size)
{
	if (!Enabled() || slot < 1 || slot > MAX_PLAYERS || size < 1) return;
	Refresh(gHUD.m_flTime);
	const auto& player = m_state.players[slot];
	if (!player.connected) return;
	const bool ct = player.team == TEAM_CT;
	const int r = ct ? 115 : 220, g = ct ? 175 : 180, b = ct ? 225 : 95;
	FillRGBABlend(x-1, y-1, size+2, size+2, r, g, b, player.local ? 255 : 125);
	FillRGBABlend(x, y, size, size, 14, 18, 24, 255);
	const int portrait = m_portraits[slot];
	const bool isPortrait = portrait && m_textures[portrait+2];
	const int texture = isPortrait ? m_textures[portrait+2]
		: (player.team == TEAM_CT ? m_textures[1] : player.team == TEAM_TERRORIST ? m_textures[0] : 0);
	// CSGO hides the default team background on death and shows a skull;
	// an identity portrait stays beneath it. Dimming here is a GoldSrc presentation choice.
	if (!player.dead || isPortrait)
	{
		if (texture) DrawTexture(texture, x, y, size, player.dead ? 90 : 255);
		else if (size >= gHUD.GetCharHeight()) CenterText(x + size/2, y, ct ? "CT" : player.team == TEAM_TERRORIST ? "T" : "?", r, g, b);
	}
	if (player.dead)
	{
		if (m_textures[2]) DrawTexture(m_textures[2], x, y, size, 255);
		else CenterText(x + size/2, y, "X", 220, 220, 220);
	}
}

int CHudAvatars::Draw(float time)
{
	if (!Enabled() || (gHUD.m_iHideHUDDisplay & HIDEHUD_ALL)) return 1;
	Refresh(time);
	const auto& ct = m_state.counter_terrorists;
	const auto& t = m_state.terrorists;
	if (!ct.count && !t.count) return 1;
	const float scale = hud_readability::Scale(match_ui::Scale());
	const float center = ScreenWidth * 0.5f;
	const float anchor = 65.0f*scale;
	const float sideWidth = max(1.0f, center - anchor - 12.0f*scale);
	const int tColumns = t.count >= 6 ? (t.count+1)/2 : t.count;
	const int ctColumns = ct.count >= 6 ? (ct.count+1)/2 : ct.count;
	const float tSpacing = (t.count >= 6 ? 33.0f : 66.0f)*scale;
	const float ctSpacing = (ct.count >= 6 ? 33.0f : 66.0f)*scale;
	const float widest = max(tColumns*tSpacing, ctColumns*ctSpacing);
	const float fit = widest > sideWidth ? sideWidth/widest : 1.0f;
	const int tSize = max(1, int((t.count >= 6 ? 32.0f : 64.0f)*scale*fit));
	const int ctSize = max(1, int((ct.count >= 6 ? 32.0f : 64.0f)*scale*fit));
	const bool showCounts = m_showCount->value != 0
		|| (t.count && tSize*gHUD.m_flScale < 16) || (ct.count && ctSize*gHUD.m_flScale < 16);
	const float height = 120.0f*scale;
	const float y = m_position->value != 0 ? ScreenHeight-height-220.0f*match_ui::Scale() : 5.0f*scale;
	s_avatarDrawScale=scale;s_avatarDrawY=y;s_avatarDrawTSize=tSize;s_avatarDrawCTSize=ctSize;
	const int background = match_ui::Texture("resource/hud/csgo/black.tga");
	if (background) match_ui::Picture(background, center-60.0f*scale, y,
		120.0f*scale, 220.0f*scale, {255, 255, 255, 200});
	else FillRGBABlend(int(center-60.0f*scale), int(y), int(120.0f*scale), int(height), 12, 16, 22, 185);
	const match_ui::Color terrorist(220, 180, 95), counterTerrorist(115, 175, 225);
	if (gHUD.m_Timer.ShouldDrawTimer() && !gHUD.m_Timer.DrawBomb(time,center,y,scale))
	{
		const int seconds = max(0, gHUD.m_Timer.SecondsRemaining(time));
		const int stopwatch = gHUD.GetSpriteIndex("stopwatch");
		const int stopwatchWidth = stopwatch >= 0 ? gHUD.GetSpriteRect(stopwatch).Width() : 23;
		match_ui::Number(seconds/60, center-15.0f*scale, y+10.0f*scale, 20.0f*scale, {}, true, 1, 15.0f*scale);
		FillRGBABlend(int(center-7.0f*scale), int(y+15.0f*scale), max(1, int(4*scale)), max(1, int(4*scale)), 255, 255, 255, 255);
		FillRGBABlend(int(center-7.0f*scale), int(y+25.0f*scale), max(1, int(4*scale)), max(1, int(4*scale)), 255, 255, 255, 255);
		match_ui::Number(seconds%60, center+(float(stopwatchWidth+30)*0.5f+10.0f)*scale,
			y+10.0f*scale, 20.0f*scale, {}, true, 2, 15.0f*scale);
	}
	match_ui::Number(gHUD.m_Scoreboard.RoundScore(TEAM_TERRORIST), center-20.0f*scale,
		y+60.0f*scale, 20.0f*scale, terrorist, true, 1, 15.0f*scale);
	match_ui::Number(gHUD.m_Scoreboard.RoundScore(TEAM_CT), center+40.0f*scale,
		y+60.0f*scale, 20.0f*scale, counterTerrorist, true, 1, 15.0f*scale);
	if (showCounts)
	{
		match_ui::Number(t.alive, center-15.0f*scale, y+95.0f*scale, 15.0f*scale, terrorist, true, 1, 10.0f*scale);
		match_ui::Number(ct.alive, center+45.0f*scale, y+95.0f*scale, 15.0f*scale, counterTerrorist, true, 1, 10.0f*scale);
		const int alive = match_ui::Texture("gfx/vgui/aliveava.tga");
		match_ui::Picture(alive, center-15.0f*scale, y+94.0f*scale, 15.0f*scale, 15.0f*scale, terrorist);
		match_ui::Picture(alive, center+45.0f*scale, y+94.0f*scale, 15.0f*scale, 15.0f*scale, counterTerrorist);
		return 1;
	}
	// APK resource geometry uses T left / CT right; CS:GO state rules remain
	// independent: index order, retained death slots and connection identities.
	for (int i = 0; i < t.count; ++i)
		DrawPlayerAvatar(t.slots[i], int(center-anchor-tSize-(i%tColumns)*tSpacing*fit),
			int(y+(i/tColumns)*tSpacing*fit), tSize);
	for (int i = 0; i < ct.count; ++i)
		DrawPlayerAvatar(ct.slots[i], int(center+anchor+(i%ctColumns)*ctSpacing*fit),
			int(y+(i/ctColumns)*ctSpacing*fit), ctSize);
	return 1;
}

void CHudAvatars::UserCmd_AvatarDump()
{
	gEngfuncs.Con_Printf("avatarlayout: scale=%.4f y=%.3f t_size=%d ct_size=%d timer_height=%.3f\n",s_avatarDrawScale,s_avatarDrawY,s_avatarDrawTSize,s_avatarDrawCTSize,20*s_avatarDrawScale);
	m_refreshTime = -1.0f;
	Refresh(gHUD.m_flTime);
	gEngfuncs.Con_Printf("avatars: time=%.3f ct=%d/%d t=%d/%d messages=%u,%u,%u textures=%d,%d,%d\n",
		gHUD.m_flTime, m_state.counter_terrorists.alive, m_state.counter_terrorists.count,
		m_state.terrorists.alive, m_state.terrorists.count, m_messages[0], m_messages[1], m_messages[2],
		m_textures[0], m_textures[1], m_textures[2]);
	for (int slot = 1; slot <= MAX_PLAYERS; ++slot)
	{
		const auto& player = m_state.players[slot];
		if (!player.connected) continue;
		const char *userid = gEngfuncs.PlayerInfo_ValueForKey(slot, "*oc2_userid");
		gEngfuncs.Con_Printf("avatar: slot=%d team=%d dead=%d local=%d portrait=%d identity=%llu revision=%u uid=%d\n",
			slot, player.team, player.dead, player.local, m_portraits[slot], player.identity, player.identity_revision,
			userid && *userid ? atoi(userid) : 0);
	}
}
