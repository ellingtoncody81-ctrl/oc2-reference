/*
spectator_gui.cpp - HUD Overlays
Copyright (C) 2015 a1batross

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

In addition, as a special exception, the author gives permission to
link the code of this program with the Half-Life Game Engine ("HL
Engine") and Modified Game Libraries ("MODs") developed by Valve,
L.L.C ("Valve").  You must obey the GNU General Public License in all
respects for all of the code used other than the HL Engine and MODs
from Valve.  If you modify this file, you may extend this exception
to your version of the file, but you are not obligated to do so.  If
you do not wish to do so, delete this exception statement from your
version.
*/

#include <string.h>
#include <string>

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"

#include "vgui_parser.h"
#include "triangleapi.h"
#include "draw_util.h"
#include "pm_shared.h"
#include "match_ui.h"
#include "unicode_labels.h"
#include "presentation_contract.h"
#include "oc2_spectator_data.h"
#include "ammohistory.h"

// SpecHealth is scoped to the current target; never reuse it for another one.
static int s_specHealthTarget = 0;
static int s_specHealth = -1;
static int s_pendingHealthTarget = 0, s_pendingHealth = -1;
static float s_pendingHealthTime = -1;
static int s_rosterCount[2] = {}, s_rosterShown[2] = {};
static int s_rosterX[2] = {}, s_rosterTop[2] = {}, s_rosterWidth = 0, s_rosterRow = 0;
struct SpectatorRosterRow
{
	int slot, target, score, deaths, scoreKnown, health, money, dead, x, y;
	char name[128];
};
static SpectatorRosterRow s_rosterRows[2][5] = {};

static void SpectatorUiDump()
{
	gEngfuncs.Con_Printf("spectator_ui: mode=%d target=%d health_target=%d health=%d scoreboard=%d\n",
		g_iUser1, g_iUser2, s_specHealthTarget, s_specHealth, int(gHUD.m_Scoreboard.ShouldDrawScoreboard()));
	const oc2_spec::Round *round = oc2_spec::GetRound();
	const oc2_spec::Target *target = oc2_spec::GetTarget(g_iUser2);
	if (round) gEngfuncs.Con_Printf("spectator_round: completed=%d limit=%d alive_ct=%d alive_t=%d\n", round->completedRounds, round->maxRounds, round->aliveCT, round->aliveT);
	if (target) gEngfuncs.Con_Printf("spectator_target: slot=%d health=%d armor=%d weapon=%d clip=%d reserve=%d\n", target->playerIndex, target->health, target->armor, target->weaponId, target->clip, target->reserve);
	gEngfuncs.Con_Printf("spectator_rosters: CT=%d/%d T=%d/%d left=%d,%d right=%d,%d width=%d row=%d\n",
		s_rosterShown[0], s_rosterCount[0], s_rosterShown[1], s_rosterCount[1],
		s_rosterX[0], s_rosterTop[0], s_rosterX[1], s_rosterTop[1], s_rosterWidth, s_rosterRow);
	for (int side = 0; side < 2; ++side)
		for (int i = 0; i < s_rosterShown[side]; ++i)
		{
			const SpectatorRosterRow &row = s_rosterRows[side][i];
			gEngfuncs.Con_Printf("spectator_row: team=%s slot=%d target=%d name=%s score=%d deaths=%d score_known=%d health=%d money=%d dead=%d rect=%d,%d,%d,%d\n",
				side == 0 ? "CT" : "T", row.slot, row.target, row.name, row.score, row.deaths,
				row.scoreKnown, row.health, row.money, row.dead, row.x, row.y, s_rosterWidth, s_rosterRow);
			const oc2_spec::PlayerRow *data = oc2_spec::GetPlayerRow(row.slot);
			if (data) gEngfuncs.Con_Printf("spectator_stats: slot=%d userid=%d alive=%d hp=%d armor=%d weapon=%d kills=%d assists=%d deaths=%d rounds=%d adr=%.1f equipment=%d\n",
				row.slot, data->userId, data->alive, data->health, data->armor, data->weaponId, data->kills, data->assists, data->deaths, data->rounds, data->adr, data->equipment);
		}
}

static const char *SpectatorModeName(int mode, bool chinese)
{
	switch (mode)
	{
	case OBS_CHASE_LOCKED: return chinese ? "锁定追踪视角" : "LOCKED CHASE";
	case OBS_CHASE_FREE: return chinese ? "自由追踪视角" : "FREE CHASE";
	case OBS_ROAMING: return chinese ? "自由视角" : "FREE LOOK";
	case OBS_IN_EYE: return chinese ? "第一人称" : "FIRST PERSON";
	case OBS_MAP_FREE: return chinese ? "自由俯视视角" : "FREE OVERVIEW";
	case OBS_MAP_CHASE: return chinese ? "追踪俯视视角" : "CHASE OVERVIEW";
	default: return chinese ? "视角选项" : "CAMERA OPTIONS";
	}
}

static void SpectatorFixedText(const char *chinese, const char *fallback, float x, float y,
	float height, match_ui::Color color, bool right = false, float width = 0)
{
	if (g_pMenu && hud_unicode::LabelFont(height) > 0)
		hud_unicode::FixedLabel(chinese, x, y, height, color, right, width);
	else
		match_ui::Text(fallback, x, y, height, color, right, width);
}

// Names use the same font and measurement as the fixed labels, including on desktop.
static void SpectatorPlayerText(const char *name, float x, float y, float height,
	match_ui::Color color, bool right, float width)
{
	if (!name) return;
	std::string clean;
	for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p && clean.size() < 512; ++p)
	{
		if (*p == '\n' || *p == '\r') break;
		if (*p < 32) continue;
		if (*p == '^' && p[1] >= '0' && p[1] <= '9') { ++p; continue; }
		clean += char(*p);
	}
	SpectatorFixedText(clean.c_str(), clean.c_str(), x, y, height, color, right, width);
}

static void SpectatorCenteredText(const char *text, float center, float y, float height,
	match_ui::Color color, float width)
{
	const float measured = g_pMenu && hud_unicode::LabelFont(height) > 0
		? hud_unicode::FixedLabelWidth(text, height) : match_ui::TextWidth(text, height);
	SpectatorFixedText(text, text, center - min(measured, width) * 0.5f, y, height, color, false, width);
}

static void SpectatorCenteredLabel(const char *chinese, const char *fallback, float center, float y,
	float height, match_ui::Color color, float width)
{
	const bool unicode = g_pMenu && hud_unicode::LabelFont(height) > 0;
	const float measured = unicode ? hud_unicode::FixedLabelWidth(chinese, height)
		: match_ui::TextWidth(fallback, height);
	SpectatorFixedText(chinese, fallback, center - min(measured, width) * 0.5f, y, height, color, false, width);
}

// CSS skewed cards translated into pixel rows for the engine's 2-D renderer.
static void SpectatorSlantedPanel(int x, int y, int w, int h, int r, int g, int b, int alpha, int skew)
{
	if (w <= 0 || h <= 0) return;
	for (int row = 0; row < h; ++row)
	{
		const int shift = skew * (h - row - 1) / h;
		FillRGBABlend(x + shift, y + row, w, 1, r, g, b, alpha);
	}
}

static void SpectatorWeaponIcon(int id, int team, int equipment, float x, float y, float width, float height, bool mirror)
{
	if (id <= 0 || id >= MAX_WEAPONS || !gEngfuncs.pTriAPI) return;
	// The server snapshot contains an ID even before this client receives the
	// corresponding WeaponList. Resolve icons independently of local inventory.
	static const char *const icons[] = { "", "p228", "zeus", "scout", "hegrenade", "xm1014", "c4",
		"mac10", "aug", "smokegrenade", "elite", "fiveseven", "ump45", "sg550", "galil", "famas",
		"usp", "glock18", "awp", "mp7", "m249", "m3", "m4a1", "tmp", "g3sg1", "flashbang",
		"deagle", "sg552", "ak47", "knife", "p90", "molotov" };
	const char *name = icons[id];
	if (id == 29 && team == TEAM_TERRORIST) name = "knife_t";
	if (id == 31 && (equipment & oc2_spec::Incendiary)) name = "incendiary";
	char path[96]; snprintf(path, sizeof(path), "gfx/vgui/death/d_%s.tga", name);
	if (match_ui::Silhouette(path, x, y, width, height, {255, 255, 255}, mirror)) return;
	const WEAPON *weapon = gWR.GetWeapon(id);
	if (!weapon || weapon->iId != id) return;
	const HSPRITE sprite = weapon->hActive ? weapon->hActive : weapon->hInactive;
	const wrect_t &rect = weapon->hActive ? weapon->rcActive : weapon->rcInactive;
	const int rw = rect.right - rect.left, rh = rect.bottom - rect.top;
	if (!sprite || rw <= 0 || rh <= 0) return;
	auto *tri = gEngfuncs.pTriAPI;
	const auto *model = gEngfuncs.GetSpritePointer(sprite);
	const int sw = SPR_Width(sprite, 0), sh = SPR_Height(sprite, 0);
	if (!model || sw <= 0 || sh <= 0) return;
	if (gRenderAPI.GL_SelectTexture) gRenderAPI.GL_SelectTexture(0);
	if (!tri->SpriteTexture(const_cast<model_s *>(model), 0)) return;
	const float fit = min(width / rw, height / rh);
	x += (width - rw * fit) * 0.5f; y += (height - rh * fit) * 0.5f;
	width = rw * fit; height = rh * fit;
	const float scale = gHUD.m_flScale > 0 ? gHUD.m_flScale : 1.0f;
	tri->RenderMode(kRenderTransAdd); tri->CullFace(TRI_NONE); tri->Color4ub(255,255,255,255);
	tri->Begin(TRI_QUADS);
	const float uLeft = float(mirror ? rect.right : rect.left)/sw;
	const float uRight = float(mirror ? rect.left : rect.right)/sw;
	tri->TexCoord2f(uLeft, float(rect.top)/sh); tri->Vertex3f(x*scale,y*scale,0);
	tri->TexCoord2f(uLeft, float(rect.bottom)/sh); tri->Vertex3f(x*scale,(y+height)*scale,0);
	tri->TexCoord2f(uRight, float(rect.bottom)/sh); tri->Vertex3f((x+width)*scale,(y+height)*scale,0);
	tri->TexCoord2f(uRight, float(rect.top)/sh); tri->Vertex3f((x+width)*scale,y*scale,0);
	tri->End(); tri->RenderMode(kRenderNormal); tri->CullFace(TRI_FRONT);
}

static const char *SpectatorMenuChinese(const char *text)
{
	struct Translation { const char *english; const char *chinese; };
	static const Translation labels[] = {
		{"Options", "选项"}, {"Spectate Options", "观战选项"},
		{"Close", "关闭"}, {"Help", "帮助"}, {"Settings", "设置"},
		{"Picture-in-Picture", "画中画"}, {"Autodirector", "自动导演"},
		{"Show scores", "显示比分"}, {"Chat messages", "聊天消息"},
		{"Show status", "显示状态"}, {"View cone", "视野锥"},
		{"Player names", "玩家名称"}, {"Chase Map Overview", "追踪俯视视角"},
		{"Free Map Overview", "自由俯视视角"}, {"First Person", "第一人称"},
		{"Free look", "自由视角"}, {"Free Chase Camera", "自由追踪视角"},
		{"Locked Chase Camera", "锁定追踪视角"}
	};
	for (unsigned int i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i)
		if (!strcmp(text, labels[i].english)) return labels[i].chinese;
	return "选项";
}

/*
 * We will draw all elements inside a box. It's size 16x10.
 */

#define XPOS( x ) ( (x) / 16.0f )
#define YPOS( y ) ( (y) / 10.0f  )

#define INT_XPOS(x) int(XPOS(x) * ScreenWidth)
#define INT_YPOS(y) int(YPOS(y) * ScreenHeight)

int CHudSpectatorGui::Init()
{
	if( !g_iXash )
		return 1;

	HOOK_MESSAGE( gHUD.m_SpectatorGui, SpecHealth );
	HOOK_MESSAGE( gHUD.m_SpectatorGui, SpecHealth2 );
	gEngfuncs.pfnAddCommand("spectator_ui_dump", SpectatorUiDump);

	HOOK_COMMAND( gHUD.m_SpectatorGui, "_spec_toggle_menu", ToggleSpectatorMenu );
	HOOK_COMMAND( gHUD.m_SpectatorGui, "_spec_toggle_menu_options", ToggleSpectatorMenuOptions );
	// close
	// help
	// settings
	// pip
	// autodirector
	// showscores

	HOOK_COMMAND( gHUD.m_SpectatorGui, "_spec_toggle_menu_options_settings", ToggleSpectatorMenuOptionsSettings );
	// settings
	// // chat msgs
	// // show status
	// // view cone
	// // player names

	HOOK_COMMAND( gHUD.m_SpectatorGui, "_spec_toggle_menu_spectate_options", ToggleSpectatorMenuSpectateOptions );
	// chase map overview
	// free map overview
	// first person
	// free look
	// free chase camera
	// locked chase camera

	HOOK_COMMAND_FUNC( "_spec_find_next_player_reverse", gHUD.m_Spectator.FindNextPlayer, true );
	HOOK_COMMAND_FUNC( "_spec_find_next_player", gHUD.m_Spectator.FindNextPlayer, false );

	gHUD.AddHudElem(this);
	m_iFlags = HUD_DRAW;
	m_menuFlags = 0;
	m_hTimerTexture = 0;
	return 1;
}

int CHudSpectatorGui::VidInit()
{
	if( !g_iXash )
	{
		ConsolePrint("Warning: CHudSpectatorGui is disabled! Dude, are you running me on old GoldSrc?\n");
		m_iFlags = 0;
		return 0;
	}

	m_hTimerTexture = gRenderAPI.GL_LoadTexture("gfx/vgui/timer.tga", NULL, 0, TF_NEAREST |TF_NOMIPMAP|TF_CLAMP );
	m_hChecked     = gRenderAPI.GL_LoadTexture("gfx/vgui/640_checked.tga", NULL, 0, TF_NEAREST | TF_NOMIPMAP | TF_CLAMP );
	m_hArrowDown   = gRenderAPI.GL_LoadTexture("gfx/vgui/1920_arrowdown.tga", NULL, 0, TF_NEAREST | TF_NOMIPMAP | TF_CLAMP );
	m_hArrowUp     = gRenderAPI.GL_LoadTexture("gfx/vgui/1920_arrowup.tga", NULL, 0, TF_NEAREST | TF_NOMIPMAP | TF_CLAMP );
	m_hArrowLeft   = gRenderAPI.GL_LoadTexture("gfx/vgui/1920_arrowleft.tga", NULL, 0, TF_NEAREST | TF_NOMIPMAP | TF_CLAMP );
	m_hArrowRight  = gRenderAPI.GL_LoadTexture("gfx/vgui/1920_arrowright.tga", NULL, 0, TF_NEAREST | TF_NOMIPMAP | TF_CLAMP );
	return 1;
}

void CHudSpectatorGui::Shutdown()
{
	gRenderAPI.GL_FreeTexture( m_hTimerTexture );
	gRenderAPI.GL_FreeTexture( m_hChecked );
	gRenderAPI.GL_FreeTexture( m_hArrowDown );
	gRenderAPI.GL_FreeTexture( m_hArrowUp );
	gRenderAPI.GL_FreeTexture( m_hArrowLeft );
	gRenderAPI.GL_FreeTexture( m_hArrowRight );
}

inline void DrawButtonWithText( int x1, int y1, int wide, int tall, const char *sz, int r, int g, int b, bool highlight = false )
{
	DrawUtils::DrawRectangle(x1, y1, wide, tall);

	if ( highlight )
	{
		FillRGBABlend(x1, y1, wide, tall, r, g, b, 48);
	}

	const float x = float(x1 + INT_XPOS(0.5));
	const float height = min(float(gHUD.GetCharHeight()), tall * 0.45f);
	const float y = y1 + (tall - height) * 0.5f;
	SpectatorFixedText(SpectatorMenuChinese(sz), sz, x, y, height, {r, g, b}, false,
		max(1.0f, float(x1 + wide) - x - tall * 0.15f));
}

// Unified icon drawing helper. align: -1 = left, 0 = center, 1 = right
static void DrawIconOnButton( int x1, int y1, int wide, int tall, int hTex, int align = -1, int r = 255, int g = 255, int b = 255, float alpha = 1.0f, int pad = 15 )
{
	if( !hTex )
		return;

	gRenderAPI.GL_SelectTexture( 0 );
	gRenderAPI.GL_Bind( 0, hTex );
	gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
	gEngfuncs.pTriAPI->Color4f( r / 255.0f, g / 255.0f, b / 255.0f, alpha );

	int uploadW = (int)gRenderAPI.RenderGetParm( PARM_TEX_WIDTH, hTex );
	int uploadH = (int)gRenderAPI.RenderGetParm( PARM_TEX_HEIGHT, hTex );


	// compute quad position in pixels
	float quadX = x1;
	float quadY = y1 + ( tall - uploadH ) / 2.0f;

	if( align == -1 ) // left
		quadX = x1 + pad; // small padding from left
	else if( align == 0 ) // center
		quadX = x1 + ( wide - uploadW ) * 0.5f;
	else if( align == 1 ) // right
		quadX = x1 + wide - uploadW - pad; // small padding from right

	DrawUtils::Draw2DQuad( quadX * gHUD.m_flScale,
						   quadY * gHUD.m_flScale,
						   (quadX + (float)uploadW) * gHUD.m_flScale,
						   (quadY + (float)uploadH) * gHUD.m_flScale );
}

int CHudSpectatorGui::Draw( float flTime )
{
	s_rosterCount[0] = s_rosterCount[1] = s_rosterShown[0] = s_rosterShown[1] = 0;
	s_rosterX[0] = s_rosterX[1] = s_rosterTop[0] = s_rosterTop[1] = 0;
	s_rosterWidth = s_rosterRow = 0;
	if( !g_iUser1 )
	{
		s_specHealthTarget = 0;
		s_specHealth = -1;
		if( m_menuFlags & ROOT_MENU )
		{
			UserCmd_ToggleSpectatorMenu(); // this will remove any submenus;
			m_menuFlags = 0;
		}
		return 1;
	}

	// Keep the existing touch menu and controls; only replace the status overlay.
	if (s_specHealthTarget != g_iUser2 || g_iUser1 == OBS_ROAMING || g_iUser1 == OBS_MAP_FREE)
	{
		s_specHealthTarget = 0;
		s_specHealth = -1;
	}
	// SpecHealth2 can arrive before the engine publishes the new iuser2.
	if (g_iUser2 > 0 && g_iUser2 == s_pendingHealthTarget &&
		gEngfuncs.GetClientTime() - s_pendingHealthTime >= 0 &&
		gEngfuncs.GetClientTime() - s_pendingHealthTime < 3.0f)
	{
		s_specHealthTarget = s_pendingHealthTarget;
		s_specHealth = s_pendingHealth;
		s_pendingHealthTarget = 0;
	}

	int r = 255, g = 140, b = 0;
	if (ScreenWidth >= 120 && ScreenHeight >= 100 && !gHUD.m_Scoreboard.ShouldDrawScoreboard())
	{
		const bool scaledFont = (g_pMenu && hud_unicode::LabelFont(14.0f) > 0) ||
			(g_iMobileAPIVersion && gMobileAPI.pfnDrawScaledCharacter);
		// The available top area starts after the picture-in-picture window.
		int left = 0;
		if (gHUD.m_Spectator.m_pip && gHUD.m_Spectator.m_pip->value != INSET_OFF && TrueWidth > 0)
			left = max(0, min(ScreenWidth, int((XRES(gHUD.m_Spectator.m_OverviewData.insetWindowWidth) +
				XRES(gHUD.m_Spectator.m_OverviewData.insetWindowX)) * float(ScreenWidth) / TrueWidth)));
		int topY = max(0, int(ScreenHeight * 0.01f));
		if (ScreenWidth - left < 60)
		{
			// If PiP leaves no useful horizontal space, put the bar below it.
			if (TrueHeight > 0)
				topY = min(int(ScreenHeight * 0.65f), max(topY, int((YRES(gHUD.m_Spectator.m_OverviewData.insetWindowHeight) +
					YRES(gHUD.m_Spectator.m_OverviewData.insetWindowY)) * float(ScreenHeight) / TrueHeight)));
			left = 0;
		}
		const int available = ScreenWidth - left;
		const int topW = min(int(ScreenWidth * 0.52f), available);
		const int topH = max(1, min(int(ScreenHeight * 0.15f),
			max(min(int(ScreenHeight * 0.060f), 65), scaledFont ? 1 : gHUD.GetCharHeight() + 4)));
		const int topX = left + (available - topW) / 2;
		const float topFont = min(topH * 0.52f, topW * 0.045f);
		if (topW >= 60 && topY + topH <= int(ScreenHeight * 0.70f))
		{
			// Team names on translucent grey, scores on team-colored blocks.
			FillRGBABlend(left, topY, available, topH, 14, 16, 18, 185);
			FillRGBABlend(topX, topY, topW, topH, 72, 78, 84, 180);
			FillRGBABlend(topX + topW * 35 / 100, topY, topW * 8 / 100, topH, 60, 124, 168, 235);
			FillRGBABlend(topX + topW * 43 / 100, topY, topW * 14 / 100, topH, 29, 35, 42, 220);
			FillRGBABlend(topX + topW * 57 / 100, topY, topW * 8 / 100, topH, 200, 145, 60, 235);
			FillRGBABlend(topX, topY, max(1, topW / 150), topH, 60, 124, 168, 255);
			FillRGBABlend(topX + topW - max(1, topW / 150), topY, max(1, topW / 150), topH, 200, 145, 60, 255);
			char t[32], ct[32], clock[32];
			if (gHUD.m_Scoreboard.RoundScoreKnown(TEAM_TERRORIST))
				snprintf(t, sizeof(t), "%d", gHUD.m_Scoreboard.RoundScore(TEAM_TERRORIST));
			else snprintf(t, sizeof(t), "--");
			if (gHUD.m_Scoreboard.RoundScoreKnown(TEAM_CT))
				snprintf(ct, sizeof(ct), "%d", gHUD.m_Scoreboard.RoundScore(TEAM_CT));
			else snprintf(ct, sizeof(ct), "--");
			snprintf(clock, sizeof(clock), "--:--");
			if (gHUD.m_Timer.BombState() == hud_presentation::RoundTimer &&
				(gHUD.m_Timer.m_iFlags & HUD_DRAW) && gHUD.m_Timer.m_iTime > 0)
			{
				const int seconds = max(0, int(gHUD.m_Timer.m_iTime + gHUD.m_Timer.m_fStartTime - gHUD.m_flTime));
				snprintf(clock, sizeof(clock), "%d:%02d", seconds / 60, seconds % 60);
			}
			const float y = topY + (topH - topFont) * 0.5f;
			SpectatorCenteredLabel("反恐精英", "CT", topX + topW * 0.175f, y, topFont,
				{245, 245, 245}, topW * 0.32f);
			SpectatorCenteredText(ct, topX + topW * 0.39f, y, topFont, {255, 255, 255}, topW * 0.075f);
			SpectatorCenteredText(clock, topX + topW * 0.50f, topY + topH * 0.06f,
				topH * 0.46f, {255, 255, 255}, topW * 0.13f);
			SpectatorCenteredText(t, topX + topW * 0.61f, y, topFont, {255, 255, 255}, topW * 0.075f);
			SpectatorCenteredLabel("恐怖分子", "T", topX + topW * 0.825f, y, topFont,
				{245, 245, 245}, topW * 0.32f);
			const oc2_spec::Round *round = oc2_spec::GetRound();
			if (round)
			{
				const int aliveX = int(ScreenWidth * 0.03f), aliveW = int(ScreenWidth * 0.18f);
				FillRGBABlend(aliveX, topY, aliveW, topH, 14, 16, 18, 210);
				SpectatorCenteredLabel("存活人数", "PLAYERS ALIVE", aliveX + aliveW * 0.5f,
					topY + topH * 0.03f, topH * 0.25f, {154, 160, 166}, aliveW * 0.95f);
				char alive[32]; snprintf(alive, sizeof(alive), "%d    VS    %d", round->aliveCT, round->aliveT);
				SpectatorCenteredText(alive, aliveX + aliveW * 0.5f, topY + topH * 0.35f,
					topH * 0.40f, {255, 255, 255}, aliveW * 0.9f);
				char roundText[48];
				if (round->maxRounds > 0) snprintf(roundText, sizeof(roundText), "第 %d / %d 回合", round->completedRounds + 1, round->maxRounds);
				else snprintf(roundText, sizeof(roundText), "第 %d 回合", round->completedRounds + 1);
				SpectatorCenteredText(roundText, topX + topW * 0.5f, topY + topH * 0.61f,
					topH * 0.31f, {210, 215, 220}, topW * 0.135f);
			}
		}

		if (gHUD.m_Spectator.m_drawstatus && gHUD.m_Spectator.m_drawstatus->value)
		{
		const int panelW = min(int(ScreenWidth * 0.28f), int(ScreenHeight * 0.85f));
		const int panelH = max(1, min(int(ScreenHeight * 0.18f),
			max(min(int(ScreenHeight * 0.085f), 82), scaledFont ? 1 : 2 * gHUD.GetCharHeight() + 4)));
		const int panelX = (ScreenWidth - panelW) / 2;
		int panelY = max(0, min(ScreenHeight - panelH,
			int(ScreenHeight * ((m_menuFlags & ROOT_MENU) ? 0.74f : 0.895f))));
		const float font = min(panelH * 0.33f, panelW * 0.055f);

		const bool targetMode = g_iUser1 == OBS_IN_EYE || g_iUser1 == OBS_CHASE_FREE ||
			g_iUser1 == OBS_CHASE_LOCKED || g_iUser1 == OBS_MAP_CHASE;
		hud_player_info_t info = {};
		if (targetMode && g_iUser2 >= 1 && g_iUser2 <= MAX_PLAYERS)
			GetPlayerInfo(g_iUser2, &info);
		const bool hasTarget = info.name && info.name[0];
		const oc2_spec::Target *targetData = hasTarget ? oc2_spec::GetTarget(g_iUser2) : nullptr;
		const oc2_spec::PlayerRow *targetRow = hasTarget ? oc2_spec::GetPlayerRow(g_iUser2) : nullptr;
		if (!hasTarget)
		{
			s_specHealthTarget = 0;
			s_specHealth = -1;
		}
		int targetTeam = hasTarget ? g_PlayerExtraInfo[g_iUser2].teamnumber : TEAM_UNASSIGNED;
		if (hasTarget && targetTeam != TEAM_CT && targetTeam != TEAM_TERRORIST) targetTeam = info.team;
		const int tr = targetTeam == TEAM_CT ? 60 : targetTeam == TEAM_TERRORIST ? 200 : 92;
		const int tg = targetTeam == TEAM_CT ? 124 : targetTeam == TEAM_TERRORIST ? 145 : 98;
		const int tb = targetTeam == TEAM_CT ? 168 : targetTeam == TEAM_TERRORIST ? 60 : 105;
		const int headerH = max(1, int(panelH * 0.54f));
		const int skew = -max(1, int(panelH * 0.21f));
		SpectatorSlantedPanel(panelX - 2, panelY - 1, panelW + 4, panelH + 2, tr, tg, tb, 255, skew);
		SpectatorSlantedPanel(panelX, panelY, panelW, headerH, tr, tg, tb, 195, skew / 2);
		SpectatorSlantedPanel(panelX, panelY + headerH, panelW, max(1, panelH - headerH), 14, 16, 18, 205, skew / 2);
		const float pad = panelW * 0.04f;
		const float nameWidth = panelW * 0.60f;
		if (hasTarget)
			SpectatorPlayerText(info.name, panelX + pad, panelY + panelH * 0.06f, font,
				{255, 255, 255}, false, nameWidth);
		else
			SpectatorFixedText("无观察目标", "NO TARGET", panelX + pad, panelY + panelH * 0.06f, font,
				{245, 245, 245}, false, nameWidth);
		const float detailFont = min(panelH * 0.23f, panelW * 0.038f);
		char hp[32], hpFallback[32];
		int targetHealth = targetData ? targetData->health : hasTarget && s_specHealthTarget == g_iUser2 ? s_specHealth : -1;
		if (hasTarget && targetHealth < 0 && gHUD.m_pShowHealth && gHUD.m_pShowHealth->value &&
			!g_PlayerExtraInfo[g_iUser2].dead && g_PlayerExtraInfo[g_iUser2].sb_health >= 0)
			targetHealth = g_PlayerExtraInfo[g_iUser2].sb_health;
		if (targetHealth >= 0)
		{
			snprintf(hp, sizeof(hp), "生命 %d", targetHealth);
			snprintf(hpFallback, sizeof(hpFallback), "HP %d", targetHealth);
		}
		else { snprintf(hp, sizeof(hp), "生命 --"); snprintf(hpFallback, sizeof(hpFallback), "HP --"); }
		if (hasTarget && !(m_menuFlags & ROOT_MENU))
		{
			const int statY = int(ScreenHeight * 0.945f), statH = max(1, int(ScreenHeight * 0.042f));
			const int hpX = int(ScreenWidth * 0.255f), hpW = int(ScreenWidth * 0.095f);
			const int ammoX = int(ScreenWidth * 0.655f), ammoW = int(ScreenWidth * 0.065f);
			SpectatorSlantedPanel(hpX, statY, hpW, statH, tr, tg, tb, 230, -statH / 5);
			char healthArmor[48];
			if (targetData) snprintf(healthArmor, sizeof(healthArmor), "%d / %d", targetData->health, targetData->armor);
			else if (targetHealth >= 0) snprintf(healthArmor, sizeof(healthArmor), "%d / --", targetHealth);
			else snprintf(healthArmor, sizeof(healthArmor), "-- / --");
			SpectatorCenteredText(healthArmor, hpX + hpW * 0.5f, statY + statH * 0.15f,
				statH * 0.56f, {255,255,255}, hpW * 0.9f);
			SpectatorSlantedPanel(ammoX, statY, ammoW, statH, tr, tg, tb, 230, -statH / 5);
			char ammunition[32];
			if (targetData && targetData->clip >= 0 && targetData->reserve >= 0)
				snprintf(ammunition, sizeof(ammunition), "%d / %d", targetData->clip, targetData->reserve);
			else snprintf(ammunition, sizeof(ammunition), "-- / --");
			SpectatorCenteredText(ammunition, ammoX + ammoW * 0.5f, statY + statH * 0.15f,
				statH * 0.56f, {255,255,255}, ammoW * 0.9f);
		}
		if (hasTarget)
		{
			char stats[64], fallback[64];
			if (targetRow)
			{
				snprintf(stats, sizeof(stats), "击杀 %d  助攻 %d  阵亡 %d  场均 %.0f", targetRow->kills, targetRow->assists, targetRow->deaths, targetRow->adr);
				snprintf(fallback, sizeof(fallback), "K %d  A %d  D %d  ADR %.0f", targetRow->kills, targetRow->assists, targetRow->deaths, targetRow->adr);
			}
			else { snprintf(stats, sizeof(stats), "击杀 --  助攻 --  阵亡 --  场均 --"); snprintf(fallback, sizeof(fallback), "K --  A --  D --  ADR --"); }
			SpectatorFixedText(stats, fallback, panelX + pad, panelY + panelH * 0.61f,
				detailFont, {240, 242, 245}, false, panelW * 0.92f);
		}

		// Leave the sides clear while the touch menu is open. The rows use the
		// same server-fed fields and visibility settings as the scoreboard.
		if (!(m_menuFlags & ROOT_MENU))
		{
			int slots[2][MAX_PLAYERS] = {};
			int counts[2] = {};
			hud_player_info_t players[MAX_PLAYERS + 1] = {};
			const int limit = min(MAX_PLAYERS, max(0, gEngfuncs.GetMaxClients()));
			for (int slot = 1; slot <= limit; ++slot)
			{
				GetPlayerInfo(slot, &players[slot]);
				if (!players[slot].name || !players[slot].name[0]) continue;
				int team = g_PlayerExtraInfo[slot].teamnumber;
				if (team != TEAM_CT && team != TEAM_TERRORIST) team = players[slot].team;
				if (team != TEAM_CT && team != TEAM_TERRORIST) continue;
				const int side = team == TEAM_CT ? 0 : 1;
				slots[side][counts[side]++] = slot;
			}
			const int rowW = max(1, int(ScreenWidth * 0.24f));
			const int rowH = max(2, min(int(ScreenHeight * 0.25f),
				max(min(int(ScreenHeight * 0.068f), 75), scaledFont ? 2 : 2 * gHUD.GetCharHeight() + 4)));
			const int rowLimit = max(1, min(5, (int(ScreenHeight * 0.38f) - rowH / 2) / rowH));
			const int rowBottom = min(ScreenHeight, int(ScreenHeight * 0.985f));
			const int margin = max(0, int(ScreenWidth * 0.01f));
			const bool showHealth = gHUD.m_pShowHealth && gHUD.m_pShowHealth->value != 0;
			const bool showMoney = gHUD.m_pShowMoney && gHUD.m_pShowMoney->value != 0;
			s_rosterWidth = rowW; s_rosterRow = rowH;
			for (int side = 0; side < 2; ++side)
			{
				const int shown = min(counts[side], rowLimit);
				const int omitted = counts[side] - shown;
				// Keep the observed player visible, including in a large server.
				if (hasTarget && omitted > 0)
					for (int i = shown; i < counts[side]; ++i)
						if (slots[side][i] == g_iUser2) { slots[side][shown - 1] = g_iUser2; break; }
				const int overflowH = omitted ? max(1, rowH / 2) : 0;
				const int x = side == 0 ? margin : ScreenWidth - margin - rowW;
				const int y = max(0, rowBottom - shown * rowH - overflowH);
				s_rosterCount[side] = counts[side]; s_rosterShown[side] = shown;
				s_rosterX[side] = x; s_rosterTop[side] = y;
				for (int i = 0; i < shown; ++i)
				{
					const int slot = slots[side][i], rowY = y + i * rowH;
					const extra_player_info_t &extra = g_PlayerExtraInfo[slot];
					const oc2_spec::PlayerRow *rowData = oc2_spec::GetPlayerRow(slot);
					const int cr = side == 0 ? 60 : 200, cg = side == 0 ? 124 : 145, cb = side == 0 ? 168 : 60;
					const bool selected = hasTarget && slot == g_iUser2;
					const bool dead = rowData ? !rowData->alive : extra.dead != 0;
					const int alpha = dead ? 100 : selected ? 245 : 205;
					FillRGBABlend(x, rowY, rowW, max(1, rowH / 2), cr, cg, cb, alpha);
					FillRGBABlend(x, rowY + rowH / 2, rowW, max(1, rowH - rowH / 2 - 1), 43, 49, 55, selected ? 225 : 190);
					if (selected)
						FillRGBABlend(side == 0 ? x + rowW - max(1, rowW / 65) : x, rowY,
							max(1, rowW / 65), rowH - 1, 255, 255, 255, 255);
					const float f = min(rowH * 0.34f, rowW * 0.067f);
					const float inset = rowW * 0.035f;
					SpectatorPlayerText(players[slot].name, side == 0 ? x + rowW * 0.28f : x + rowW * 0.24f,
						rowY + rowH * 0.045f, f, {255, 255, 255}, false, rowW * 0.48f);
					if (rowData && !dead)
						SpectatorWeaponIcon(rowData->weaponId, rowData->team, rowData->equipment, side == 0 ? x + rowW * 0.77f : x + rowW * 0.025f,
							rowY + rowH * 0.04f, rowW * 0.20f, rowH * 0.38f, side == 0);
					char health[32], healthFallback[32], stats[64], statsFallback[64], money[32];
					int value = -1;
					if (!dead && rowData) value = rowData->health;
					else if (showHealth && !dead)
						value = slot == s_specHealthTarget && s_specHealth >= 0 ? s_specHealth : extra.sb_health;
					if (value >= 0)
					{
						if (rowData) snprintf(health, sizeof(health), "%d/%d", value, rowData->armor);
						else snprintf(health, sizeof(health), "%d/--", value);
						snprintf(healthFallback, sizeof(healthFallback), "%s", health);
					}
					else if (dead) { snprintf(health, sizeof(health), "阵亡"); snprintf(healthFallback, sizeof(healthFallback), "DEAD"); }
					else { snprintf(health, sizeof(health), "--/--"); snprintf(healthFallback, sizeof(healthFallback), "--/--"); }
					if (rowData)
					{
						snprintf(stats, sizeof(stats), "杀 %d  死 %d  均 %.0f%s%s", rowData->kills, rowData->deaths, rowData->adr,
							(rowData->equipment & oc2_spec::Defuser) ? " 钳" : "", rowData->armorType == 2 ? " 盔" : "");
						snprintf(statsFallback, sizeof(statsFallback), "K %d  D %d  ADR %.0f", rowData->kills, rowData->deaths, rowData->adr);
					}
					else { snprintf(stats, sizeof(stats), "杀 --  死 --  均 --"); snprintf(statsFallback, sizeof(statsFallback), "K --  D --  ADR --"); }
					if (rowData) snprintf(money, sizeof(money), "$%d", rowData->money);
					else if (showMoney && extra.sb_account >= 0) snprintf(money, sizeof(money), "$%d", extra.sb_account);
					else snprintf(money, sizeof(money), "$--");
					SpectatorFixedText(health, healthFallback, side == 0 ? x + inset : x + rowW - inset,
						rowY + rowH * 0.045f, f, {255, 255, 255}, side != 0, rowW * 0.27f);
					SpectatorFixedText(stats, statsFallback, x + inset, rowY + rowH * 0.55f, f * 0.75f,
						{200, 210, 220}, false, rowW * 0.68f);
					SpectatorFixedText(money, money, x + rowW - inset, rowY + rowH * 0.55f, f * 0.82f,
						{224, 169, 58}, true, rowW * 0.30f);
					SpectatorRosterRow &drawn = s_rosterRows[side][i];
					drawn.slot = slot; drawn.target = hasTarget && slot == g_iUser2;
					drawn.score = extra.frags; drawn.deaths = extra.deaths;
					drawn.scoreKnown = gHUD.m_Scoreboard.ScoreInfoKnown(slot);
					drawn.health = value; drawn.money = showMoney && extra.sb_account >= 0 ? extra.sb_account : -1;
					drawn.dead = dead; drawn.x = x; drawn.y = rowY;
					snprintf(drawn.name, sizeof(drawn.name), "%s", players[slot].name);
				}
				if (omitted)
				{
					const int overflowY = y + shown * rowH;
					FillRGBABlend(x, overflowY, rowW, overflowH, 17, 23, 29, 205);
					char more[32], moreFallback[32];
					snprintf(more, sizeof(more), "另有 %d 人", omitted);
					snprintf(moreFallback, sizeof(moreFallback), "+%d", omitted);
					SpectatorFixedText(more, moreFallback, x + rowW * 0.04f, overflowY, min(overflowH * 0.65f, rowW * 0.07f),
						{220, 225, 230}, false, rowW * 0.92f);
				}
			}
		}
		}
	}

	if( m_menuFlags & ROOT_MENU )
	{
		// draw the root menu

		// options
		{
			// highlight when opened
			if( m_menuFlags & MENU_OPTIONS )
				DrawButtonWithText(INT_XPOS(0.5),  INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1), "Options", r, g, b, true);
			else
				DrawButtonWithText(INT_XPOS(0.5),  INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1), "Options", r, g, b );
			// arrow on right part of button: down when closed, up when open
			if( m_menuFlags & MENU_OPTIONS )
				DrawIconOnButton( INT_XPOS(0.5), INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1), m_hArrowUp, 1, r, g, b );
			else
				DrawIconOnButton( INT_XPOS(0.5), INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1), m_hArrowDown, 1, r, g, b );
		}

		DrawUtils::DrawRectangle(INT_XPOS(5), INT_YPOS(8.5), INT_XPOS(1), INT_YPOS(1));
		DrawIconOnButton( INT_XPOS(5), INT_YPOS(8.5), INT_XPOS(1), INT_YPOS(1), m_hArrowLeft, 0, r, g, b );

		DrawUtils::DrawRectangle(INT_XPOS(6), INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1));
		// name will be drawn later

		DrawUtils::DrawRectangle(INT_XPOS(10), INT_YPOS(8.5), INT_XPOS(1), INT_YPOS(1));
		DrawIconOnButton( INT_XPOS(10), INT_YPOS(8.5), INT_XPOS(1), INT_YPOS(1), m_hArrowRight, 0, r, g, b );

		// spectate options
		{
			// highlight when opened
			if( m_menuFlags & MENU_SPEC_OPTIONS )
				DrawButtonWithText(INT_XPOS(11.5), INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1), "Spectate Options", r, g, b, true);
			else
				DrawButtonWithText(INT_XPOS(11.5), INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1), "Spectate Options", r, g, b );
			// arrow on right part of button: down when closed, up when open
			if( m_menuFlags & MENU_SPEC_OPTIONS )
				DrawIconOnButton( INT_XPOS(11.5), INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1), m_hArrowUp, 1, r, g, b );
			else
				DrawIconOnButton( INT_XPOS(11.5), INT_YPOS(8.5), INT_XPOS(4), INT_YPOS(1), m_hArrowDown, 1, r, g, b );
		}

		if( m_menuFlags & MENU_OPTIONS )
		{
			DrawButtonWithText(INT_XPOS(0.5), INT_YPOS(2.5), INT_XPOS(4), INT_YPOS(1), "Close", r, g, b );
			DrawButtonWithText(INT_XPOS(0.5), INT_YPOS(3.5), INT_XPOS(4), INT_YPOS(1), "Help", r, g, b );
			
			// settings
			{
				// highlight when opened
				if( m_menuFlags & MENU_OPTIONS_SETTINGS )
					DrawButtonWithText(INT_XPOS(0.5), INT_YPOS(4.5), INT_XPOS(4), INT_YPOS(1), "Settings", r, g, b, true );
				else
					DrawButtonWithText(INT_XPOS(0.5), INT_YPOS(4.5), INT_XPOS(4), INT_YPOS(1), "Settings", r, g, b );

				DrawIconOnButton( INT_XPOS(0.5), INT_YPOS(4.5), INT_XPOS(4), INT_YPOS(1), m_hArrowRight, 1, r, g, b );
			}

			DrawButtonWithText(INT_XPOS(0.5), INT_YPOS(5.5), INT_XPOS(4), INT_YPOS(1), "Picture-in-Picture", r, g, b );
			if( gHUD.m_Spectator.m_pip && gHUD.m_Spectator.m_pip->value != INSET_OFF )
				DrawIconOnButton( INT_XPOS(0.5), INT_YPOS(5.5), INT_XPOS(4), INT_YPOS(1), m_hChecked, -1, r, g, b );

			DrawButtonWithText(INT_XPOS(0.5), INT_YPOS(6.5), INT_XPOS(4), INT_YPOS(1), "Autodirector", r, g, b );
			if( gHUD.m_Spectator.m_autoDirector && gHUD.m_Spectator.m_autoDirector->value )
				DrawIconOnButton( INT_XPOS(0.5), INT_YPOS(6.5), INT_XPOS(4), INT_YPOS(1), m_hChecked, -1, r, g, b );

			DrawButtonWithText(INT_XPOS(0.5), INT_YPOS(7.5), INT_XPOS(4), INT_YPOS(1), "Show scores", r, g, b );
			if( gHUD.m_Scoreboard.m_bForceDraw || gHUD.m_Scoreboard.m_bShowscoresHeld )
				DrawIconOnButton( INT_XPOS(0.5), INT_YPOS(7.5), INT_XPOS(4), INT_YPOS(1), m_hChecked, -1, r, g, b );

			if( m_menuFlags & MENU_OPTIONS_SETTINGS )
			{
				DrawButtonWithText(INT_XPOS(4.5), INT_YPOS(4.5), INT_XPOS(4), INT_YPOS(1), "Chat messages", r, g, b );
				if( gHUD.m_Spectator.m_HUD_saytext && gHUD.m_Spectator.m_HUD_saytext->value )
					DrawIconOnButton( INT_XPOS(4.5), INT_YPOS(4.5), INT_XPOS(4), INT_YPOS(1), m_hChecked, -1, r, g, b );

				DrawButtonWithText(INT_XPOS(4.5), INT_YPOS(5.5), INT_XPOS(4), INT_YPOS(1), "Show status", r, g, b );
				if( gHUD.m_Spectator.m_drawstatus && gHUD.m_Spectator.m_drawstatus->value )
					DrawIconOnButton( INT_XPOS(4.5), INT_YPOS(5.5), INT_XPOS(4), INT_YPOS(1), m_hChecked, -1, r, g, b );

				DrawButtonWithText(INT_XPOS(4.5), INT_YPOS(6.5), INT_XPOS(4), INT_YPOS(1), "View cone", r, g, b );
				if( gHUD.m_Spectator.m_drawcone && gHUD.m_Spectator.m_drawcone->value )
					DrawIconOnButton( INT_XPOS(4.5), INT_YPOS(6.5), INT_XPOS(4), INT_YPOS(1), m_hChecked, -1, r, g, b );

				DrawButtonWithText(INT_XPOS(4.5), INT_YPOS(7.5), INT_XPOS(4), INT_YPOS(1), "Player names", r, g, b );
				if( gHUD.m_Spectator.m_drawnames && gHUD.m_Spectator.m_drawnames->value )
					DrawIconOnButton( INT_XPOS(4.5), INT_YPOS(7.5), INT_XPOS(4), INT_YPOS(1), m_hChecked, -1, r, g, b );
			}
		}

		if( m_menuFlags & MENU_SPEC_OPTIONS )
		{
			DrawButtonWithText(INT_XPOS(11.5), INT_YPOS(2.5), INT_XPOS(4), INT_YPOS(1), "Chase Map Overview", r, g, b );
			DrawButtonWithText(INT_XPOS(11.5), INT_YPOS(3.5), INT_XPOS(4), INT_YPOS(1), "Free Map Overview", r, g, b );
			DrawButtonWithText(INT_XPOS(11.5), INT_YPOS(4.5), INT_XPOS(4), INT_YPOS(1), "First Person", r, g, b );
			DrawButtonWithText(INT_XPOS(11.5), INT_YPOS(5.5), INT_XPOS(4), INT_YPOS(1), "Free look", r, g, b );
			DrawButtonWithText(INT_XPOS(11.5), INT_YPOS(6.5), INT_XPOS(4), INT_YPOS(1), "Free Chase Camera", r, g, b );
			DrawButtonWithText(INT_XPOS(11.5), INT_YPOS(7.5), INT_XPOS(4), INT_YPOS(1), "Locked Chase Camera", r, g, b );
		}
	}


	return 1;
}

void CHudSpectatorGui::CalcAllNeededData( )
{
}
void CHudSpectatorGui::InitHUDData()
{
	s_specHealthTarget = 0;
	s_specHealth = -1;
	s_pendingHealthTarget = 0; s_pendingHealth = -1; s_pendingHealthTime = -1;
	m_bBombPlanted = false;
	label.m_szMap[0] = '\0';
}

void CHudSpectatorGui::Reset()
{
	s_specHealthTarget = 0;
	s_specHealth = -1;
	s_pendingHealthTarget = 0; s_pendingHealth = -1; s_pendingHealthTime = -1;
	m_bBombPlanted = false;
	if( m_menuFlags & ROOT_MENU )
	{
		UserCmd_ToggleSpectatorMenu(); // this will remove any submenus;
		m_menuFlags = 0;
	}
}

int CHudSpectatorGui::MsgFunc_SpecHealth(const char *pszName, int iSize, void *buf)
{
	BufferReader reader( pszName, buf, iSize );

	int health = reader.ReadByte();

	if (g_iUser2 >= 1 && g_iUser2 <= MAX_PLAYERS)
	{
		s_specHealthTarget = g_iUser2;
		s_specHealth = health;
		if (s_pendingHealthTarget == g_iUser2) s_pendingHealthTarget = 0;
		g_PlayerExtraInfo[g_iUser2].health = health;
		gHUD.m_Health.m_iPlayerLastPointedAt = g_iUser2;
	}

	return 1;
}

int CHudSpectatorGui::MsgFunc_SpecHealth2(const char *pszName, int iSize, void *buf)
{
	BufferReader reader( pszName, buf, iSize );

	int health = reader.ReadByte();
	int client = reader.ReadByte();

	if (client >= 1 && client <= MAX_PLAYERS)
	{
		g_PlayerExtraInfo[client].health = health;
		s_pendingHealthTarget = client;
		s_pendingHealth = health;
		s_pendingHealthTime = gEngfuncs.GetClientTime();
		if (client == g_iUser2)
		{
			s_specHealthTarget = client;
			s_specHealth = health;
			s_pendingHealthTarget = 0;
		}
		gHUD.m_Health.m_iPlayerLastPointedAt = g_iUser2;
	}

	return 1;
}

#define PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y(x, y) XPOS(x), YPOS(y), XPOS(x + 4.0f), YPOS(y + 1.0f)

void CHudSpectatorGui::UserCmd_ToggleSpectatorMenu()
{
	static byte color[4] = {0, 0, 0, 0};

	if( !g_iMobileAPIVersion )
		return;

	gMobileAPI.pfnTouchSetClientOnly( !(m_menuFlags & ROOT_MENU) );

	if( !(m_menuFlags & ROOT_MENU) )
	{
		m_menuFlags |= ROOT_MENU;

		gMobileAPI.pfnTouchAddClientButton( "_spec_menu_options", "*white", "_spec_toggle_menu_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 0.5f, 8.5f ), color, 0, 1.0f, 0 );

		gMobileAPI.pfnTouchAddClientButton( "_spec_menu_find_next_player_reverse", "*white", "_spec_find_next_player_reverse",
			XPOS(5.0f), YPOS(8.5f), XPOS(6.0f), YPOS(9.5f), color, 0, 1.0f, 0 );

		gMobileAPI.pfnTouchAddClientButton( "_spec_menu_find_next_player", "*white", "_spec_find_next_player",
			XPOS(10.0f),YPOS(8.5f), XPOS(11.0f),YPOS(9.5f), color, 0, 1.0f, 0 );

		gMobileAPI.pfnTouchAddClientButton( "_spec_menu_spectate_options", "*white", "_spec_toggle_menu_spectate_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 11.5f, 8.5f ),color, 0, 1.0f, 0 );
	}
	else
	{
		m_menuFlags &= ~ROOT_MENU;
		m_menuFlags &= ~MENU_OPTIONS;
		m_menuFlags &= ~MENU_OPTIONS_SETTINGS;
		m_menuFlags &= ~MENU_SPEC_OPTIONS;
		gMobileAPI.pfnTouchRemoveButton( "_spec_*" );
	}
}

void CHudSpectatorGui::UserCmd_ToggleSpectatorMenuOptions()
{
	static byte color[4] = {0, 0, 0, 0};

	if( !(m_menuFlags & ROOT_MENU) || !g_iMobileAPIVersion )
		return;

	if( !(m_menuFlags & MENU_OPTIONS) )
	{
		m_menuFlags |= MENU_OPTIONS;
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_close", "*white", "_spec_toggle_menu",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 0.5f, 2.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_help", "*white", "spec_help; _spec_toggle_menu_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 0.5f, 3.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_settings", "*white", "_spec_toggle_menu_options_settings",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 0.5f, 4.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_pip", "*white", "spec_pip t; _spec_toggle_menu_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 0.5f, 5.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_ad", "*white", "spec_autodirector t; _spec_toggle_menu_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 0.5f, 6.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_showscores", "*white", "_spec_toggle_menu_options; scoreboard",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 0.5f, 7.5f ), color, 0, 1.0f, 0 );
	}
	else
	{
		m_menuFlags &= ~MENU_OPTIONS;
		m_menuFlags &= ~MENU_OPTIONS_SETTINGS;
		gMobileAPI.pfnTouchRemoveButton( "_spec_opt_*" );
	}
}

void CHudSpectatorGui::UserCmd_ToggleSpectatorMenuOptionsSettings()
{
	static byte color[4] = {0, 0, 0, 0};

	if( !(m_menuFlags & ROOT_MENU) || !g_iMobileAPIVersion )
		return;

	if( !(m_menuFlags & MENU_OPTIONS_SETTINGS) )
	{
		m_menuFlags |= MENU_OPTIONS_SETTINGS;
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_chat_msgs", "*white", "hud_saytext t; _spec_toggle_menu_options_settings",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 4.5f, 4.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_set_status", "*white", "spec_drawstatus t; _spec_toggle_menu_options_settings",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 4.5f, 5.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_draw_cones", "*white", "spec_drawcone t; _spec_toggle_menu_options_settings",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 4.5f, 6.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_opt_draw_names", "*white", "spec_drawnames t; _spec_toggle_menu_options_settings",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 4.5f, 7.5f ), color, 0, 1.0f, 0 );
	}
	else
	{
		m_menuFlags &= ~MENU_OPTIONS_SETTINGS;
		gMobileAPI.pfnTouchRemoveButton( "_spec_opt_set_*" );
	}
}

void CHudSpectatorGui::UserCmd_ToggleSpectatorMenuSpectateOptions()
{
	static byte color[4] = {0, 0, 0, 0};

	if( !(m_menuFlags & ROOT_MENU) || !g_iMobileAPIVersion )
		return;

	if( !(m_menuFlags & MENU_SPEC_OPTIONS) )
	{
		m_menuFlags |= MENU_SPEC_OPTIONS;
		gMobileAPI.pfnTouchAddClientButton( "_spec_spec_6", "*white", "spec_mode 6; _spec_toggle_menu_spectate_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 11.5f, 2.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_spec_5", "*white", "spec_mode 5; _spec_toggle_menu_spectate_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 11.5f, 3.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_spec_4", "*white", "spec_mode 4; _spec_toggle_menu_spectate_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 11.5f, 4.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_spec_3", "*white", "spec_mode 3; _spec_toggle_menu_spectate_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 11.5f, 5.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_spec_2", "*white", "spec_mode 2; _spec_toggle_menu_spectate_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 11.5f, 6.5f ), color, 0, 1.0f, 0 );
		gMobileAPI.pfnTouchAddClientButton( "_spec_spec_1", "*white", "spec_mode 1; _spec_toggle_menu_spectate_options",
			PLACE_DEFAULT_SIZE_BUTTON_AT_X_Y( 11.5f, 7.5f ), color, 0, 1.0f, 0 );
	}
	else
	{
		m_menuFlags &= ~MENU_SPEC_OPTIONS;
		gMobileAPI.pfnTouchRemoveButton( "_spec_spec_*" );
	}
}
