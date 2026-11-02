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
#include "oc2_observer.h"
#include "ammohistory.h"
#include "spectator_layout.h"
#include "skin_inventory_client.h"
#include "hud_readability.h"
#include "oc2_knife_icons.h"
#include <cmath>
#include <vector>

// SpecHealth is scoped to the current target; never reuse it for another one.
static int s_specHealthTarget = 0;
static int s_specHealth = -1;
static float s_specHealthTime = -1;
static int s_pendingHealthTarget = 0, s_pendingHealth = -1;
static float s_pendingHealthTime = -1;
static int s_rosterCount[2] = {}, s_rosterShown[2] = {};
static int s_rosterX[2] = {}, s_rosterTop[2] = {}, s_rosterWidth = 0, s_rosterRow = 0;
struct SpectatorRosterRow
{
	int slot, target, score, deaths, scoreKnown, health, money, dead, x, y;
	int armor,primary,secondary,knife,taser,he,flash,smoke,fire,mirror,kit,kitMode;
	char name[128];
};
static SpectatorRosterRow s_rosterRows[2][5] = {};
static spectator_layout::Layout s_layout = {};
static bool s_layoutDrawn = false, s_focusDrawn = false;
static spectator_layout::HealthTrail s_healthTrails[MAX_PLAYERS+1] = {};
struct EquipmentMask { const char *path; int texture, width, height; bool tried; };
static EquipmentMask s_equipmentMasks[] = {
	{"gfx/vgui/kevlar.tga",0,0,0,false},
	{"gfx/vgui/kevlar_helmet.tga",0,0,0,false},
	{"gfx/vgui/defuser.tga",0,0,0,false}
};
static const char *SpectatorWeaponToken(int id,int team,int equipment,int player,int fireCount);

static void ClearEquipmentMasks()
{
	for (auto &mask:s_equipmentMasks)
	{
		if(mask.texture && gRenderAPI.GL_FreeTexture)gRenderAPI.GL_FreeTexture(mask.texture);
		mask.texture=0;mask.tried=false;
	}
}

// CSS mask-image semantics: retain the original icon alpha, render its dark
// artwork as a readable white silhouette. No source or runtime asset is edited.
static EquipmentMask *PrepareEquipmentMask(const char *path)
{
	EquipmentMask *mask=nullptr;
	for(auto &candidate:s_equipmentMasks)if(!strcmp(candidate.path,path)){mask=&candidate;break;}
	if(!mask || !gRenderAPI.GL_LoadTexture || !gEngfuncs.COM_LoadFile || !gEngfuncs.COM_FreeFile)
		return nullptr;
	if(!mask->tried)
	{
		mask->tried=true;
		const auto bounds=match_ui::SilhouetteBounds(path);
		int bytes=0;
		auto *data=gEngfuncs.COM_LoadFile(const_cast<char *>(path),5,&bytes);
		if(data && bytes>=18 && bounds.valid && data[1]==0 && data[2]==2 && data[16]==32)
		{
			const int sw=data[12]+(data[13]<<8),sh=data[14]+(data[15]<<8),offset=18+data[0];
			mask->width=bounds.right-bounds.left;mask->height=bounds.bottom-bounds.top;
			if(sw>0 && sh>0 && sw<=4096 && sh<=4096 && bytes>=offset+sw*sh*4 && mask->width>0 && mask->height>0)
			{
				std::vector<unsigned char> image(18+mask->width*mask->height*4,255);
				for(int i=0;i<18;++i)image[i]=0;
				image[2]=2;image[12]=mask->width&255;image[13]=mask->width>>8;
				image[14]=mask->height&255;image[15]=mask->height>>8;image[16]=32;image[17]=0x28;
				for(int py=0;py<mask->height;++py)for(int px=0;px<mask->width;++px)
				{
					const int sx=(data[17]&16) ? sw-1-(bounds.left+px) : bounds.left+px;
					const int sy=(data[17]&32) ? bounds.top+py : sh-1-(bounds.top+py);
					image[18+4*(py*mask->width+px)+3]=data[offset+4*(sy*sw+sx)+3];
				}
				char name[80];snprintf(name,sizeof(name),"#oc2_spectator_%s",strrchr(path,'/')+1);
				mask->texture=gRenderAPI.GL_LoadTexture(name,image.data(),int(image.size()),TF_NOMIPMAP|TF_CLAMP);
			}
		}
		if(data)gEngfuncs.COM_FreeFile(data);
	}
	return mask;
}
static bool SpectatorEquipmentIcon(const char *path,float x,float y,float w,float h,match_ui::Color color=match_ui::Color())
{
	auto *mask=PrepareEquipmentMask(path);
	if(!mask||!mask->texture)return match_ui::Silhouette(path,x,y,w,h,color);
	const float fit=min(w/mask->width,h/mask->height);
	const float width=mask->width*fit,height=mask->height*fit;
	match_ui::Picture(mask->texture,x+(w-width)*.5f,y+(h-height)*.5f,width,height,color);
	return true;
}

void OC2_PrecacheSpectatorGui()
{
	// Called after every HUD VidInit, including scoreboard's font-cache reset.
	const float s=spectator_layout::Make(ScreenWidth,ScreenHeight).scale;
	for(float size:{9.f,12.f,12.5f,13.f,14.f,15.f,16.f,17.f,18.f,19.f,20.f,22.5f,24.f,25.f,26.f,30.f})
		hud_unicode::LabelFont(size*s,700);
	hud_unicode::LabelFont(18*s,700);hud_unicode::LabelFont(19*s,700);
	hud_unicode::LabelFont(28*hud_readability::Scale(match_ui::Scale()),700);
	hud_unicode::LabelFont(max(16.f,26*s),700);
	const float digitWidth=hud_unicode::FixedLabelWidth("00",30*s,700);
	const auto layout=spectator_layout::Make(ScreenWidth,ScreenHeight,digitWidth+15*s);
	hud_unicode::LabelFont(30*s*min(1.0f,layout.health.w/(214.254f*s)),700);
	for(auto &mask:s_equipmentMasks)PrepareEquipmentMask(mask.path);
}

static void SpectatorUiDump()
{
	gEngfuncs.Con_Printf("spectator_ui: mode=%d target=%d health_target=%d health=%d scoreboard=%d\n",
		g_iUser1, g_iUser2, s_specHealthTarget, s_specHealth, int(gHUD.m_Scoreboard.ShouldDrawScoreboard()));
	gEngfuncs.Con_Printf("spectator_layout: drawn=%d focus=%d viewport=%d,%d scale=%.4f top=%.1f,%.1f,%.1f,%.1f card=%.1f,%.1f,%.1f,%.1f hp=%.1f,%.1f,%.1f,%.1f ammo=%.1f,%.1f,%.1f,%.1f gap=%.1f\n",
		int(s_layoutDrawn),int(s_focusDrawn),ScreenWidth,ScreenHeight,s_layout.scale,
		s_layout.top.x,s_layout.top.y,s_layout.top.w,s_layout.top.h,
		s_layout.focus.x,s_layout.focus.y,s_layout.focus.w,s_layout.focus.h,
		s_layout.health.x,s_layout.health.y,s_layout.health.w,s_layout.health.h,
		s_layout.ammo.x,s_layout.ammo.y,s_layout.ammo.w,s_layout.ammo.h,s_layout.rowGap);
	gEngfuncs.Con_Printf("spectator_controls: rect=%.1f,%.1f,%.1f,%.1f text=%.1f weight=700\n",
		s_layout.controls.x,s_layout.controls.y,s_layout.controls.w,s_layout.controls.h,17*s_layout.scale);
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
			gEngfuncs.Con_Printf("spectator_equipment: slot=%d armor=%d primary=%d secondary=%d knife=%d taser=%d he=%d flash=%d smoke=%d fire=%d mirror=%d kit=%d kit_mode=%d\n",
				row.slot,row.armor,row.primary,row.secondary,row.knife,row.taser,row.he,row.flash,row.smoke,row.fire,row.mirror,row.kit,row.kitMode);
			const oc2_spec::PlayerRow *data = oc2_spec::GetPlayerRow(row.slot);
			if(data)gEngfuncs.Con_Printf("spectator_icons: slot=%d knife_definition=%d knife_icon=%s fire_icon=%s armor_type=%d\n",
				row.slot,OC2_InventoryPlayerKnifeDefinition(row.slot),
				SpectatorWeaponToken(29,data->team,data->equipment,row.slot,data->fire),
				SpectatorWeaponToken(31,data->team,data->equipment,row.slot,data->fire),data->armorType);
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
	if (g_pMenu && hud_unicode::LabelFont(height,700) > 0)
		hud_unicode::FixedLabel(chinese, x, y, height, color, right, width,700);
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
	match_ui::Color color, float width, int weight = 700)
{
	const bool unicode = g_pMenu && hud_unicode::LabelFont(height,weight) > 0;
	const float measured = unicode ? hud_unicode::FixedLabelWidth(text,height,weight) : match_ui::TextWidth(text,height);
	const float x = center - min(measured,width)*.5f;
	if(unicode)hud_unicode::FixedLabel(text,x,y,height,color,false,width,weight);
	else match_ui::Text(text,x,y,height,color,false,width);
}

static void SpectatorCenteredLabel(const char *chinese, const char *fallback, float center, float y,
	float height, match_ui::Color color, float width)
{
	const bool unicode = g_pMenu && hud_unicode::LabelFont(height,700) > 0;
	const float measured = unicode ? hud_unicode::FixedLabelWidth(chinese, height,700)
		: match_ui::TextWidth(fallback, height);
	SpectatorFixedText(chinese, fallback, center - min(measured, width) * 0.5f, y, height, color, false, width);
}

static const char *SpectatorWeaponToken(int id,int team,int equipment,int player,int fireCount)
{
	// The server snapshot contains an ID even before this client receives the
	// corresponding WeaponList. Resolve icons independently of local inventory.
	static const char *const icons[] = { "", "p228", "zeus", "scout", "hegrenade", "xm1014", "c4",
		"mac10", "aug", "smokegrenade", "elite", "fiveseven", "ump45", "sg550", "galil", "famas",
		"usp", "glock18", "awp", "mp7", "m249", "m3", "m4a1", "tmp", "g3sg1", "flashbang",
		"deagle", "sg552", "ak47", "knife", "p90", "molotov" };
	const char *name = icons[id];
	if(id==29)
	{
		const auto *knife=player>0 ? oc2_knife_icons::FindDefinition(OC2_InventoryPlayerKnifeDefinition(player)) : nullptr;
		name=knife ? knife->deathToken : team==TEAM_CT ? "knife" : "knife_t";
	}
	// Empty slots show the team's normal purchase; carried variants can cross teams.
	if(id==31 && (fireCount>0 ? (equipment & oc2_spec::Incendiary)!=0 : team==TEAM_CT))name="incendiary";
	return name;
}

static void SpectatorWeaponIcon(int id, int team, int equipment, float x, float y, float width, float height, bool mirror, int alpha = 255, int player = 0, int fireCount = 0)
{
	if (id <= 0 || id >= MAX_WEAPONS || !gEngfuncs.pTriAPI) return;
	const char *name=SpectatorWeaponToken(id,team,equipment,player,fireCount);
	char path[96]; snprintf(path, sizeof(path), "gfx/vgui/death/d_%s.tga", name);
	if (match_ui::Silhouette(path, x, y, width, height, {255, 255, 255, alpha}, mirror)) return;
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
	tri->RenderMode(kRenderTransAdd); tri->CullFace(TRI_NONE); tri->Color4ub(alpha,alpha,alpha,255);
	tri->Begin(TRI_QUADS);
	const float uLeft = float(mirror ? rect.right : rect.left)/sw;
	const float uRight = float(mirror ? rect.left : rect.right)/sw;
	tri->TexCoord2f(uLeft, float(rect.top)/sh); tri->Vertex3f(x*scale,y*scale,0);
	tri->TexCoord2f(uLeft, float(rect.bottom)/sh); tri->Vertex3f(x*scale,(y+height)*scale,0);
	tri->TexCoord2f(uRight, float(rect.bottom)/sh); tri->Vertex3f((x+width)*scale,(y+height)*scale,0);
	tri->TexCoord2f(uRight, float(rect.top)/sh); tri->Vertex3f((x+width)*scale,y*scale,0);
	tri->End(); tri->RenderMode(kRenderNormal); tri->CullFace(TRI_FRONT);
}

static void SpectatorPanel(spectator_layout::Rect rect, match_ui::Color color,
	float leftTop = 0, float leftBottom = 0, float rightTop = 0, float rightBottom = 0)
{
	// Coalesce identical scanlines: CSS skew/clip-path without one draw per pixel.
	const int height = max(1, int(std::floor(rect.h + .5f)));
	int run = 0, previousLeft = 0, previousRight = 0;
	for (int row = 0; row <= height; ++row)
	{
		const float t = height > 1 ? float(row)/float(height-1) : 0;
		const int left = int(std::floor(rect.x + leftTop + (leftBottom-leftTop)*t + .5f));
		const int right = int(std::floor(rect.x+rect.w + rightTop + (rightBottom-rightTop)*t + .5f));
		if (row && (row == height || left != previousLeft || right != previousRight))
		{
			if (previousRight > previousLeft)
				FillRGBABlend(previousLeft, int(std::floor(rect.y+.5f))+run,
					previousRight-previousLeft, row-run, color.r,color.g,color.b,color.a);
			run = row;
		}
		previousLeft = left; previousRight = right;
	}
}

static int SpectatorOwnedWeapon(const oc2_spec::PlayerRow *row, bool primary)
{
	if (!row) return 0;
	static const int mainIds[] = {3,5,7,8,12,13,14,15,18,19,20,21,22,23,24,27,28,30};
	static const int pistols[] = {1,10,11,16,17,26};
	const int *ids = primary ? mainIds : pistols;
	const int count = primary ? int(sizeof(mainIds)/sizeof(*mainIds)) : int(sizeof(pistols)/sizeof(*pistols));
	for (int i=0;i<count;++i) if (row->weaponId == ids[i]) return ids[i];
	for (int i=0;i<count;++i) if (row->weapons & (1u << ids[i])) return ids[i];
	return 0;
}

static void SpectatorUtility(const oc2_spec::PlayerRow *row, float x, float y, float s, bool mirror)
{
	if (!row || !row->alive) return;
	const int counts[] = {row->he,row->flash,row->smoke,row->fire};
	const int ids[] = {4,25,9,31};
	for (int i=0;i<4;++i)
	{
		if (counts[i] <= 0) continue;
		const float at = x+(mirror ? 3-i : i)*22.5f*s;
		SpectatorWeaponIcon(ids[i],row->team,row->equipment,at,y,18*s,15*s,mirror,
			row->weaponId == ids[i] ? 255 : 165,row->playerIndex,row->fire);
		if (counts[i] > 1)
		{
			char n[8]; snprintf(n,sizeof(n),"%d",counts[i]);
			SpectatorFixedText(n,n,at+14*s,y+5*s,9*s,{255,255,255},false,9*s);
		}
	}
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
	ClearEquipmentMasks();
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
	ClearEquipmentMasks();
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
	s_layoutDrawn = s_focusDrawn = false;
	s_rosterCount[0] = s_rosterCount[1] = s_rosterShown[0] = s_rosterShown[1] = 0;
	s_rosterX[0] = s_rosterX[1] = s_rosterTop[0] = s_rosterTop[1] = 0;
	s_rosterWidth = s_rosterRow = 0;
	if( !g_iUser1 )
	{
 OC2_ObserverResetInputs();
		s_specHealthTarget = 0;
		s_specHealth = -1;
		s_specHealthTime = -1;
		s_pendingHealthTarget = 0; s_pendingHealthTime = -1;
		for (auto &trail : s_healthTrails) trail = spectator_layout::HealthTrail();
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
		s_specHealthTime = -1;
	}
	// SpecHealth2 can arrive before the engine publishes the new iuser2.
	if (g_iUser2 > 0 && g_iUser2 == s_pendingHealthTarget &&
		gEngfuncs.GetClientTime() - s_pendingHealthTime >= 0 &&
		gEngfuncs.GetClientTime() - s_pendingHealthTime < 3.0f)
	{
		s_specHealthTarget = s_pendingHealthTarget;
		s_specHealth = s_pendingHealth;
		s_specHealthTime = s_pendingHealthTime;
		s_pendingHealthTarget = 0;
	}

	int r = 255, g = 140, b = 0;
	if (ScreenWidth >= 120 && ScreenHeight >= 100 && !gHUD.m_Scoreboard.ShouldDrawScoreboard())
	{
		const float baseScale=spectator_layout::Make(ScreenWidth,ScreenHeight).scale;
		const float digitWidth=g_pMenu && hud_unicode::LabelFont(30*baseScale,700)>0
			? hud_unicode::FixedLabelWidth("00",30*baseScale,700) : match_ui::TextWidth("00",30*baseScale);
		s_layout = spectator_layout::Make(ScreenWidth,ScreenHeight,digitWidth+15*baseScale);
		s_layoutDrawn = true;
		const auto &l = s_layout;
		const float s = l.scale;
		const match_ui::Color ctColor(29,158,222), tColor(193,136,3);
		const match_ui::Color muted(180,188,198), green(51,204,77);
		const auto teamColor = [&](int team, int alpha=255) {
			return team==TEAM_CT ? match_ui::Color(29,158,222,alpha) :
				team==TEAM_TERRORIST ? match_ui::Color(193,136,3,alpha) : match_ui::Color(100,110,120,alpha);
		};
		// One centered CSS grid. Do not paint the old full-width grey strip.
		for (int side=0;side<2;++side)
		{
			const auto &team = l.team[side];
			const float skew=(side ? 1 : -1)*16.5f*s;
			SpectatorPanel(team,side ? match_ui::Color(193,136,3,128) : match_ui::Color(29,158,222,128),
				skew,0,skew,0);
			SpectatorPanel({side ? team.x+team.w-20*s : team.x,team.y,20*s,team.h},
				side ? tColor : ctColor,skew,0,skew,0);
			SpectatorCenteredLabel(side ? u8"\u6050\u6016\u5206\u5b50" : u8"\u53cd\u6050\u7cbe\u82f1",
				side ? "TERRORISTS" : "COUNTER-TERRORISTS",team.x+team.w*.5f,team.y+8*s,
				25*s,{255,255,255},team.w-55*s);
			const auto &score=l.score[side];
			SpectatorPanel(score,side ? tColor : ctColor,side ? 0 : -17*s,0,side ? 17*s : 0,0);
			char number[24];
			const int teamId=side ? TEAM_TERRORIST : TEAM_CT;
			if (gHUD.m_Scoreboard.RoundScoreKnown(teamId))
				snprintf(number,sizeof(number),"%d",gHUD.m_Scoreboard.RoundScore(teamId));
			else snprintf(number,sizeof(number),"--");
			SpectatorCenteredText(number,score.x+score.w*.5f,score.y+6*s,30*s,{255,255,255},score.w-6*s);
		}
		SpectatorPanel(l.clock,{0,0,0,128});
		const int bomb=gHUD.m_Timer.BombState();
		char clock[32]="--:--";
		int seconds=-1;
		if (bomb==hud_presentation::RoundTimer && (gHUD.m_Timer.m_iFlags & HUD_DRAW) && gHUD.m_Timer.m_iTime>0)
		{
			seconds=max(0,int(std::ceil(gHUD.m_Timer.m_iTime+gHUD.m_Timer.m_fStartTime-gHUD.m_flTime)));
			snprintf(clock,sizeof(clock),"%d:%02d",seconds/60,seconds%60);
		}
		if (bomb==hud_presentation::Planted || bomb==hud_presentation::Defused)
		{
			const bool planted=bomb==hud_presentation::Planted;
			match_ui::Silhouette(planted ? "gfx/vgui/c4icon.tga" : "gfx/vgui/c4defused.tga",
				l.clock.x+8*s,l.clock.y+3*s,22*s,22*s,planted ? match_ui::Color(255,51,51) : green);
			SpectatorFixedText(planted ? u8"\u5df2\u5b89\u653e" : u8"\u5df2\u62c6\u9664",
				planted ? "PLANTED" : "DEFUSED",l.clock.x+33*s,l.clock.y+6*s,14*s,
				planted ? match_ui::Color(255,51,51) : green,false,l.clock.w-38*s);
		}
		else SpectatorCenteredText(clock,l.clock.x+l.clock.w*.5f,l.clock.y+2*s,22.5f*s,
			seconds>=0 && seconds<=10 ? match_ui::Color(255,51,51) : match_ui::Color(),l.clock.w-5*s);
		const oc2_spec::Round *round=oc2_spec::GetRound();
		if (round)
		{
			char text[64];
			if (round->maxRounds>0) snprintf(text,sizeof(text),u8"\u7b2c %d/%d \u56de\u5408",round->completedRounds+1,round->maxRounds);
			else snprintf(text,sizeof(text),u8"\u7b2c %d \u56de\u5408",round->completedRounds+1);
			SpectatorCenteredText(text,l.clock.x+l.clock.w*.5f,l.clock.y+26*s,18*s,{255,255,255},l.clock.w-6*s,700);
			SpectatorPanel(l.alive,{0,0,0,128});
			SpectatorCenteredLabel(u8"\u5b58\u6d3b\u4eba\u6570","PLAYERS ALIVE",l.alive.x+l.alive.w*.5f,
				l.alive.y+2*s,12.5f*s,{230,233,238},l.alive.w-4*s);
			SpectatorPanel({l.alive.x,l.alive.y+18*s,42*s,40*s},ctColor);
			SpectatorPanel({l.alive.x+l.alive.w-42*s,l.alive.y+18*s,42*s,40*s},tColor);
			char alive[16];snprintf(alive,sizeof(alive),"%d",round->aliveCT);
			SpectatorCenteredText(alive,l.alive.x+21*s,l.alive.y+19*s,30*s,{255,255,255},40*s);
			snprintf(alive,sizeof(alive),"%d",round->aliveT);
			SpectatorCenteredText(alive,l.alive.x+l.alive.w-21*s,l.alive.y+19*s,30*s,{255,255,255},40*s);
			SpectatorCenteredText("VS",l.alive.x+l.alive.w*.5f,l.alive.y+31*s,15*s,{255,255,255},32*s);
		}

		if (gHUD.m_Spectator.m_drawstatus && gHUD.m_Spectator.m_drawstatus->value && !(m_menuFlags & ROOT_MENU))
		{
			const bool targetMode=g_iUser1==OBS_IN_EYE || g_iUser1==OBS_CHASE_FREE ||
				g_iUser1==OBS_CHASE_LOCKED || g_iUser1==OBS_MAP_CHASE;
			hud_player_info_t info={};
			if (targetMode && g_iUser2>=1 && g_iUser2<=MAX_PLAYERS) GetPlayerInfo(g_iUser2,&info);
			const bool hasTarget=info.name && info.name[0];
			const oc2_spec::Target *targetData=hasTarget ? oc2_spec::GetTarget(g_iUser2) : nullptr;
			const oc2_spec::PlayerRow *targetRow=hasTarget ? oc2_spec::GetPlayerRow(g_iUser2) : nullptr;
			if (hasTarget)
			{
				s_focusDrawn=true;
				int targetTeam=targetRow ? targetRow->team : g_PlayerExtraInfo[g_iUser2].teamnumber;
				if (targetTeam!=TEAM_CT && targetTeam!=TEAM_TERRORIST) targetTeam=info.team;
				const auto &f=l.focus;
				const auto color=teamColor(targetTeam);
				SpectatorPanel({f.x,f.y,15*s,f.h},color,-17.5f*s,15*s,-17.5f*s,15*s);
				SpectatorPanel({f.x+f.w-15*s,f.y,15*s,f.h},color,17.5f*s,-15*s,17.5f*s,-15*s);
				SpectatorPanel({f.x+15*s,f.y,f.w-30*s,45*s},teamColor(targetTeam,153),-17.5f*s,-1.25f*s,17.5f*s,1.25f*s);
				SpectatorPanel({f.x+15*s,f.y+45*s,f.w-30*s,45*s},{0,0,0,190},-1.25f*s,15*s,1.25f*s,-15*s);
				SpectatorPlayerText(info.name,f.x+25*s,f.y+9*s,25*s,{255,255,255},false,f.w-55*s);
				const char *labels[]={"K","A","D","ADR"};
				const int stats[]={targetRow ? targetRow->kills : -1,targetRow ? targetRow->assists : -1,
					targetRow ? targetRow->deaths : -1,targetRow ? int(targetRow->adr+.5f) : -1};
				for (int i=0;i<4;++i)
				{
					const float x=f.x+40*s+i*50*s;
					SpectatorCenteredText(labels[i],x+23*s,f.y+46*s,15*s,{230,235,242},46*s);
					char value[16];if (stats[i]>=0) snprintf(value,sizeof(value),"%d",stats[i]);else snprintf(value,sizeof(value),"--");
					SpectatorCenteredText(value,x+23*s,f.y+63*s,20*s,{255,255,255},46*s);
				}
				if (targetRow && targetRow->alive)
				{
					if (targetRow->equipment & oc2_spec::Taser)
						SpectatorWeaponIcon(2,targetTeam,0,f.x+249*s,f.y+59*s,28*s,22*s,false,150);
					if (targetRow->equipment & oc2_spec::Defuser)
					{
						SpectatorEquipmentIcon("gfx/vgui/defuser.tga",f.x+282*s,f.y+60*s,20*s,22*s);
						const bool rescue=(targetRow->equipment & oc2_spec::RescueKitMode)!=0;
						SpectatorFixedText(rescue ? u8"\u6551" : u8"\u62c6",rescue ? "R" : "D",f.x+304*s,f.y+65*s,12*s,{255,255,255},false,15*s);
					}
					if (targetRow->equipment & oc2_spec::Bomb)
						SpectatorWeaponIcon(6,targetTeam,0,f.x+285*s,f.y+60*s,22*s,22*s,false);
					SpectatorUtility(targetRow,f.x+322*s,f.y+63*s,1.1f*s,false);
				}
				int hp=targetData ? targetData->health : -1;
				if (hp<0 && s_specHealthTarget==g_iUser2 && s_specHealthTime>=0 &&
					flTime>=s_specHealthTime && flTime-s_specHealthTime<3) hp=s_specHealth;
				if (hp<0 && targetRow) hp=targetRow->health;
				const int armor=targetData ? targetData->armor : targetRow ? targetRow->armor : -1;
				SpectatorPanel(l.health,color,-12*s,0,-12*s,0);
				const float cy=l.health.y, hs=s*min(1.0f,l.health.w/(214.254f*s));
				FillRGBABlend(int(l.health.x+13*hs),int(cy+20*s),int(21*hs),max(1,int(5*hs)),255,255,255,255);
				FillRGBABlend(int(l.health.x+21*hs),int(cy+12*s),max(1,int(5*hs)),int(21*hs),255,255,255,255);
				char text[24];if(hp>=0)snprintf(text,sizeof(text),"%d",hp);else snprintf(text,sizeof(text),"--");
				const float hpWidth=min(62*hs,g_pMenu ? hud_unicode::FixedLabelWidth(text,30*hs,700) : match_ui::TextWidth(text,30*hs));
				SpectatorFixedText(text,text,l.health.x+43*hs,cy+6*s,30*hs,{255,255,255},false,62*hs);
				const int armorType=targetData ? targetData->armorType : targetRow ? targetRow->armorType : 0;
				if(armor>=0)snprintf(text,sizeof(text),"%d",armor);else snprintf(text,sizeof(text),"--");
				const float armorWidth=min(62*hs,g_pMenu ? hud_unicode::FixedLabelWidth(text,30*hs,700) : match_ui::TextWidth(text,30*hs));
				const float armorRight=l.health.x+l.health.w-12*hs;
				const float iconLeft=l.health.x+43*hs+hpWidth+6*hs;
				const float iconWidth=max(0.0f,armorRight-armorWidth-6*hs-iconLeft);
				SpectatorEquipmentIcon(armorType==2 ? "gfx/vgui/kevlar_helmet.tga" : "gfx/vgui/kevlar.tga",
					iconLeft,cy+(45*s-36*hs)*.5f,iconWidth,36*hs,{255,255,255,armor>0 ? 255 : 120});
				SpectatorFixedText(text,text,armorRight,cy+6*s,30*hs,{255,255,255},true,62*hs);
				// The reference only shows ammunition for a held primary/pistol.
				if (targetData && (targetData->weaponId==SpectatorOwnedWeapon(targetRow,true) ||
					targetData->weaponId==SpectatorOwnedWeapon(targetRow,false)) && targetData->clip>=0)
				{
					SpectatorPanel(l.ammo,color,12*s,0,12*s,0);
					snprintf(text,sizeof(text),"%d",targetData->clip);
					SpectatorFixedText(text,text,l.ammo.x+21*s,l.ammo.y+5*s,30*s,{255,255,255},false,65*s);
					if(targetData->reserve>=0)snprintf(text,sizeof(text),"/ %d",targetData->reserve);else snprintf(text,sizeof(text),"/ --");
					SpectatorFixedText(text,text,l.ammo.x+86*s,l.ammo.y+14*s,20*s,{255,255,255},false,l.ammo.w-90*s);
				}
			}

			int slots[2][MAX_PLAYERS]={},counts[2]={};
			hud_player_info_t players[MAX_PLAYERS+1]={};
			const int limit=min(MAX_PLAYERS,max(0,gEngfuncs.GetMaxClients()));
			for (int slot=1;slot<=limit;++slot)
			{
				GetPlayerInfo(slot,&players[slot]);
				if(!players[slot].name || !players[slot].name[0]) continue;
				int team=g_PlayerExtraInfo[slot].teamnumber;
				const auto *data=oc2_spec::GetPlayerRow(slot);
				if(data) team=data->team;
				if(team!=TEAM_CT && team!=TEAM_TERRORIST) team=players[slot].team;
				if(team==TEAM_CT || team==TEAM_TERRORIST) slots[team==TEAM_CT ? 0 : 1][counts[team==TEAM_CT ? 0 : 1]++]=slot;
			}
			s_rosterWidth=int(l.rowWidth+.5f);s_rosterRow=int(l.rowHeight+.5f);
			for(int side=0;side<2;++side)
			{
				const int shown=min(counts[side],5),omitted=counts[side]-shown;
				if(hasTarget && omitted>0)
					for(int i=shown;i<counts[side];++i)
						if(slots[side][i]==g_iUser2){slots[side][shown-1]=g_iUser2;break;}
				const float x=l.RowX(side),y=l.RowTop(shown,omitted>0);
				s_rosterCount[side]=counts[side];s_rosterShown[side]=shown;
				s_rosterX[side]=int(x+.5f);s_rosterTop[side]=int(y+.5f);
				const int team=side ? TEAM_TERRORIST : TEAM_CT;
				for(int i=0;i<shown;++i)
				{
					const int slot=slots[side][i];
					const float rowY=y+i*(l.rowHeight+l.rowGap);
					const auto &extra=g_PlayerExtraInfo[slot];
					const auto *data=oc2_spec::GetPlayerRow(slot);
					const bool selected=hasTarget && slot==g_iUser2;
					const bool dead=extra.dead || (data && !data->alive);
					const bool mirror=side==0; // Explicit user correction: face opposite to the first draft.
					int hp=data ? data->health : -1;
					if(dead)hp=-1;
					else if(hp<0 && gHUD.m_pShowHealth && gHUD.m_pShowHealth->value)hp=extra.sb_health;
					const int armor=data ? data->armor : -1;
					const int money=data ? data->money : gHUD.m_pShowMoney && gHUD.m_pShowMoney->value ? extra.sb_account : -1;
					const int primary=SpectatorOwnedWeapon(data,true),secondary=SpectatorOwnedWeapon(data,false);
					SpectatorPanel({x,rowY,l.rowWidth,l.rowHeight},{0,0,0,175});
					if(!dead)
					{
						SpectatorPanel({x,rowY,l.rowWidth,38*s},{0,0,0,77});
						const float trail=s_healthTrails[slot].Update(data ? data->userId : slot,hp,flTime);
						if(hp>=0)
						{
							const float tw=l.rowWidth*trail,hw=l.rowWidth*spectator_layout::Ratio(hp);
							SpectatorPanel({side ? x+l.rowWidth-tw : x,rowY,tw,38*s},{255,51,51,255});
							SpectatorPanel({side ? x+l.rowWidth-hw : x,rowY,hw,38*s},teamColor(team));
						}
					}
					else s_healthTrails[slot]=spectator_layout::HealthTrail();
					if(selected)SpectatorPanel({side ? x-5*s : x+l.rowWidth,rowY,5*s,l.rowHeight},{255,255,255,215});
					char text[64];
					if(dead)
						SpectatorCenteredLabel(u8"\u9635\u4ea1","DEAD",x+(side ? 467.5f : 32.5f)*s,rowY+8*s,18*s,{235,238,242},58*s);
					else
					{
						if(hp>=0)snprintf(text,sizeof(text),"%d",hp);else snprintf(text,sizeof(text),"--");
						SpectatorCenteredText(text,x+(side ? 467.5f : 32.5f)*s,rowY+4*s,24*s,{255,255,255},60*s);
					}
					const int number=(i+1+(side ? 5 : 0))%10;
					snprintf(text,sizeof(text),"%d",number);
					SpectatorCenteredText(text,x+(side ? 427.5f : 72.5f)*s,rowY+10*s,17*s,{255,255,255},20*s);
					SpectatorPanel({x+(side ? 415 : 84)*s,rowY+10*s,s,20*s},{255,255,255,175});
					SpectatorPlayerText(players[slot].name,x+(side ? 410 : 90)*s,rowY+4*s,25*s,
						{255,255,255},side!=0,285*s);
					if(data && !dead)
					{
						SpectatorWeaponIcon(primary,team,data->equipment,x+(side ? 10 : 405)*s,rowY+5*s,85*s,28*s,mirror,data->weaponId==primary ? 255 : 150);
						SpectatorWeaponIcon(secondary,team,data->equipment,x+(side ? 10 : 405)*s,rowY+41*s,85*s,23*s,mirror,data->weaponId==secondary ? 255 : 150);
					}
					// Armor is a visible numeric field, including a known zero. A
					// helmet silhouette only means the actual server armorType==2.
					if(!dead)
					{
						SpectatorEquipmentIcon(data && data->armorType==2 ? "gfx/vgui/kevlar_helmet.tga" : "gfx/vgui/kevlar.tga",
							x+(side ? 466 : 6)*s,rowY+41*s,27*s,24*s,{255,255,255,armor>0 ? 255 : 110});
						if(armor>=0)snprintf(text,sizeof(text),"%d",armor);else snprintf(text,sizeof(text),"--");
						SpectatorFixedText(text,text,x+(side ? 460 : 38)*s,rowY+40*s,20*s,
							armor>0 ? match_ui::Color() : muted,side!=0,48*s);
					}
					if(money>=0)snprintf(text,sizeof(text),"$%d",money);else snprintf(text,sizeof(text),"$--");
					SpectatorFixedText(text,text,x+(side ? 400 : 100)*s,rowY+40*s,20*s,green,side!=0,95*s);
					const int kills=data ? data->kills : extra.frags,deaths=data ? data->deaths : extra.deaths;
					snprintf(text,sizeof(text),"K %d",kills);
					SpectatorFixedText(text,text,x+(side ? 210 : 200)*s,rowY+40*s,20*s,muted,false,50*s);
					snprintf(text,sizeof(text),"D %d",deaths);
					SpectatorFixedText(text,text,x+(side ? 260 : 250)*s,rowY+40*s,20*s,muted,false,50*s);
					if(data)snprintf(text,sizeof(text),"ADR %.0f",data->adr);else snprintf(text,sizeof(text),"ADR --");
					SpectatorFixedText(text,text,x+(side ? 110 : 305)*s,rowY+41*s,19*s,muted,false,90*s);
					// Complete carried inventory, independent of the active gun.
					// Empty utility slots are dim; grenade quantities include zero.
					SpectatorPanel({x+8*s,rowY+68*s,l.rowWidth-16*s,s},{255,255,255,35});
					SpectatorFixedText(u8"\u88c5\u5907","GEAR",x+(side ? 490 : 10)*s,rowY+73*s,17*s,muted,side!=0,42*s);
					if(!dead && data)
					{
						const int ownedKnife=((data->weapons & (1u<<29)) || (data->equipment & oc2_spec::Knife)) ? 1 : 0;
						const int ownedTaser=((data->weapons & (1u<<2)) || (data->equipment & oc2_spec::Taser)) ? 1 : 0;
						const int quantities[]={ownedKnife,ownedTaser,data->he,data->flash,data->smoke,data->fire,
							(data->equipment & oc2_spec::Defuser) ? 1 : 0,(data->equipment & oc2_spec::Bomb) ? 1 : 0};
						const int ids[]={29,2,4,25,9,31,0,6};
						for(int item=0;item<8;++item)
						{
							const float local=55+item*52;
							const float at=x+(side ? 500-local-46 : local)*s;
							const int alpha=quantities[item]>0 ? data->weaponId==ids[item] ? 255 : 185 : 45;
							if(item==6)
							{
								if(team!=TEAM_CT)continue;
								const bool rescue=(data->equipment & oc2_spec::RescueKitMode)!=0;
								SpectatorEquipmentIcon("gfx/vgui/defuser.tga",at+5*s,rowY+72*s,23*s,22*s,{255,255,255,alpha});
								SpectatorFixedText(rescue ? u8"\u6551\u63f4\u5de5\u5177" : u8"\u62c6\u5f39\u5668",rescue ? "RESCUE" : "KIT",
									at+31*s,rowY+75*s,15*s,{255,255,255,quantities[item]>0 ? 255 : 100},false,65*s);
							}
							else if(item==7 && team==TEAM_CT)continue;
							else SpectatorWeaponIcon(ids[item],team,data->equipment,at,rowY+71*s,40*s,23*s,mirror,alpha,slot,data->fire);
							if(item>=2 && item<=5)
							{
								snprintf(text,sizeof(text),"%d",quantities[item]);
								SpectatorFixedText(text,text,at+35*s,rowY+77*s,16*s,
									quantities[item]>0 ? match_ui::Color() : muted,false,14*s);
							}
						}
					}
					auto &drawn=s_rosterRows[side][i];
					drawn.slot=slot;drawn.target=selected;drawn.score=kills;drawn.deaths=deaths;
					drawn.scoreKnown=data || gHUD.m_Scoreboard.ScoreInfoKnown(slot);
					drawn.health=hp;drawn.money=money;drawn.dead=dead;drawn.x=int(x+.5f);drawn.y=int(rowY+.5f);
					drawn.armor=armor;drawn.primary=primary;drawn.secondary=secondary;drawn.mirror=mirror;
					drawn.knife=data && ((data->weapons&(1u<<29)) || (data->equipment&oc2_spec::Knife));
					drawn.taser=data && ((data->weapons&(1u<<2)) || (data->equipment&oc2_spec::Taser));
					drawn.he=data ? data->he : 0;drawn.flash=data ? data->flash : 0;
					drawn.smoke=data ? data->smoke : 0;drawn.fire=data ? data->fire : 0;
					drawn.kit=data && (data->equipment&oc2_spec::Defuser);
					drawn.kitMode=data && (data->equipment&oc2_spec::RescueKitMode) ? 1 : 0;
					snprintf(drawn.name,sizeof(drawn.name),"%s",players[slot].name);
				}
				if(omitted)
				{
					const float at=y+shown*(l.rowHeight+l.rowGap);
					SpectatorPanel({x,at,l.rowWidth,20*s},{0,0,0,153});
					char text[48],fallback[32];snprintf(text,sizeof(text),u8"\u53e6\u6709 %d \u4eba",omitted);snprintf(fallback,sizeof(fallback),"+%d PLAYERS",omitted);
					SpectatorFixedText(text,fallback,x+8*s,at+2*s,13*s,muted,false,l.rowWidth-16*s);
				}
			}
		}
	}

	if (s_layoutDrawn)
	{
		const float s=s_layout.scale;
		const char *prompt=OC2_ControlPrompt();
		if(prompt&&*prompt)
		{
			const float y=s_layout.focus.y-31*s;
			SpectatorCenteredText(prompt,ScreenWidth*.5f+s,y+s,19*s,{0,0,0,230},540*s,700);
			SpectatorCenteredText(prompt,ScreenWidth*.5f,y,19*s,{255,255,255},540*s,700);
		}
		const auto &controls=s_layout.controls;
		SpectatorPanel(controls,{0,0,0,175});
		SpectatorCenteredText(u8"\u5de6\u952e / \u53f3\u952e  \u5207\u6362\u73a9\u5bb6     \u7a7a\u683c / \u6eda\u8f6e  \u5207\u6362\u89c6\u89d2",
			ScreenWidth*.5f,controls.y,17*s,{245,247,250},controls.w-16*s);
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
	s_specHealthTime = -1;
	s_pendingHealthTarget = 0; s_pendingHealth = -1; s_pendingHealthTime = -1;
	m_bBombPlanted = false;
	label.m_szMap[0] = '\0';
}

void CHudSpectatorGui::Reset()
{
	s_specHealthTarget = 0;
	s_specHealth = -1;
	s_specHealthTime = -1;
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
		s_specHealthTime = gEngfuncs.GetClientTime();
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
			s_specHealthTime = gEngfuncs.GetClientTime();
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
