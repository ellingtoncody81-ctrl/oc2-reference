/***
*
*	Copyright (c) 1999, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
//
// Scoreboard.cpp
//
// implementation of CHudScoreboard class
//
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "hud.h"
#include "oc2_observer.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "triangleapi.h"
#include "com_weapons.h"
#include "cdll_dll.h"
#include "draw_util.h"
#include "vgui_parser.h"
#include "eventscripts.h"
#include "match_ui.h"
#include "scoreboard_layout.h"
#include "radar_fullmap_layout.h"
#include "unicode_labels.h"
#include "oc2_spectator_data.h"
#include <algorithm>
#include <vector>
#include <string>

hud_player_info_t   g_PlayerInfoList[MAX_PLAYERS+1]; // player info from the engine
extra_player_info_t	g_PlayerExtraInfo[MAX_PLAYERS+1]; // additional player info sent directly to the client dll
team_info_t         g_TeamInfo[MAX_TEAMS+1];
hostage_info_t      g_HostageInfo[MAX_HOSTAGES+1];
int g_iUser1;
int g_iUser2;
int g_iUser3;
int g_iTeamNumber;

namespace
{
const char *ObserverKitLabel(int slot)
{
	const auto *row=oc2_spec::GetPlayerRow(slot);
	return row ? (row->equipment & oc2_spec::RescueKitMode ? u8"\u6551\u63f4\u5de5\u5177" : u8"\u62c6\u5f39\u5668") : u8"\u62c6\u5f39\u94b3";
}
int s_scoreRowGap = 15;
int s_scoreAvatarSize = 0;
float s_scoreFontHeight = 16;
bool s_scoreStyled = false;

struct ModernScoreRow
{
	int slot=0,team=0,frags=0,deaths=0,ping=0,health=-1,money=-1;
	bool dead=false,local=false,bot=false;
	float x=0,y=0,width=0,height=0;
};
scoreboard_layout::Layout s_modernScoreLayout;
std::vector<ModernScoreRow> s_modernScoreRows;
int s_modernSpectators=0;
using hud_unicode::UnicodeLabel;
using hud_unicode::FixedLabelWidth;
using hud_unicode::FixedLabel;

void ModernScoreDump()
{
	const auto &v=s_modernScoreLayout;
	gEngfuncs.Con_Printf("scoreboardlayout: visible=%d modern=%d screen=%d,%d scale=%.3f rect=%.3f,%.3f,%.3f,%.3f row=%.3f avatar=%.3f font=%.3f side_by_side=%d rows=%d spectators=%d scores=%d,%d known=%d,%d\n",
		int(gHUD.m_Scoreboard.ShouldDrawScoreboard()),int(match_ui::Enabled()&&!gHUD.m_Scoreboard.m_bForceDraw&&gHUD.m_Teamplay),ScreenWidth,ScreenHeight,gHUD.m_flScale,v.x,v.y,v.width,v.height,v.row,v.avatar,v.font,int(v.sideBySide),
		int(s_modernScoreRows.size()),s_modernSpectators,gHUD.m_Scoreboard.RoundScore(TEAM_CT),gHUD.m_Scoreboard.RoundScore(TEAM_TERRORIST),
		int(gHUD.m_Scoreboard.RoundScoreKnown(TEAM_CT)),int(gHUD.m_Scoreboard.RoundScoreKnown(TEAM_TERRORIST)));
	for (const auto &p:s_modernScoreRows)
		gEngfuncs.Con_Printf("scoreboardrow: slot=%d team=%d frags=%d deaths=%d ping=%d dead=%d local=%d bot=%d hp=%d money=%d rect=%.3f,%.3f,%.3f,%.3f\n",
			p.slot,p.team,p.frags,p.deaths,p.ping,int(p.dead),int(p.local),int(p.bot),p.health,p.money,p.x,p.y,p.width,p.height);
}

void ModernScoreFill(float x,float y,float width,float height,match_ui::Color color)
{
	FillRGBABlend(int(x),int(y),max(1,int(width)),max(1,int(height)),color.r,color.g,color.b,color.a);
}

void ModernScoreValue(const char *value,float right,float y,float width,float height,match_ui::Color color)
{
	if(UnicodeLabel(value))FixedLabel(value,right,y,height,color,true,width,700);
	else match_ui::Text(value,right,y,height,color,true,width);
}

int DrawModernScoreboard()
{
	// CS:GO csgo_scoreboard.cpp:148-198 refreshes on real round/player events;
	// uicomponent_gamestate.cpp:680/746/757/767/787 supplies team/alive/ping/K/D.
	// These fields map to existing TeamInfo/TeamScore/ScoreInfo/ScoreAttrib.
	// ScoreInfo.frags includes suicide/team-kill penalties, unlike Source's
	// independent kills field, so label that GoldSrc column SCORE rather than K.
	// GoldSrc has no comparable assists/ADR/MVP payload: do not synthesize it.
	std::vector<int> teams[2],spectators;
	for (int slot=1;slot<=MAX_PLAYERS;++slot)
	{
		const auto &info=g_PlayerInfoList[slot];
		if (!info.name || !info.name[0]) continue;
		const int team=g_PlayerExtraInfo[slot].teamnumber;
		if (team==TEAM_CT) teams[0].push_back(slot);
		else if (team==TEAM_TERRORIST) teams[1].push_back(slot);
		else spectators.push_back(slot);
	}
	for (auto &team:teams) std::sort(team.begin(),team.end(),[](int a,int b)
	{
		const auto &pa=g_PlayerExtraInfo[a],&pb=g_PlayerExtraInfo[b];
		return scoreboard_layout::Before(pa.frags,pa.deaths,a,pb.frags,pb.deaths,b);
	});
	auto layout=scoreboard_layout::Make(ScreenWidth,ScreenHeight,match_ui::Scale(),int(teams[0].size()),int(teams[1].size()));
	if(gHUD.m_RadarModern.IsEnabled())
	{
		const auto radar=radar_fullmap_layout::Make(ScreenWidth,ScreenHeight);
		scoreboard_layout::ReserveRadar(layout,float(radar.x+radar.size+8*layout.scale));
	}
	s_modernScoreLayout=layout;s_modernScoreRows.clear();s_modernSpectators=int(spectators.size());
	const float s=layout.scale;
	ModernScoreFill(layout.x-2*s,layout.y-2*s,layout.width+4*s,layout.height+4*s,{8,10,13,225});
	const char *server=gHUD.m_szServerName[0]?gHUD.m_szServerName:"反恐精英";
	if(gHUD.m_szServerName[0])match_ui::Text(server,layout.x+20*s,layout.y+12*s,28*s,{240,243,246},false,layout.width*.68f);
	else FixedLabel(server,layout.x+20*s,layout.y+12*s,28*s,{240,243,246},false,layout.width*.68f);
	std::string map=gEngfuncs.pfnGetLevelName()?gEngfuncs.pfnGetLevelName():"";
	const auto separator=map.find_last_of("/\\");if(separator!=std::string::npos)map.erase(0,separator+1);
	if(map.size()>4 && map.compare(map.size()-4,4,".bsp")==0)map.resize(map.size()-4);
	match_ui::Text(map.c_str(),layout.x+layout.width-20*s,layout.y+18*s,20*s,{170,180,192},true,layout.width*.26f);
	const bool showHealth=gHUD.m_pShowHealth && gHUD.m_pShowHealth->value!=0;
	const bool showMoney=gHUD.m_pShowMoney && gHUD.m_pShowMoney->value!=0;
	for(int index=0;index<2;++index)
	{
		const int team=index==0?TEAM_CT:TEAM_TERRORIST;
		const match_ui::Color theme=index==0?match_ui::Color(115,175,225):match_ui::Color(237,199,97);
		const float x=layout.TeamX(index),y=layout.TeamY(index),width=layout.teamWidth;
		ModernScoreFill(x,y,width,layout.TeamHeight(index),{13,17,22,205});
		ModernScoreFill(x,y,width,layout.teamHeading,{theme.r/3,theme.g/3,theme.b/3,210});
		ModernScoreFill(x,y,4*s,layout.TeamHeight(index),theme);
		match_ui::Silhouette(index==0?"gfx/vgui/icon_ct.tga":"gfx/vgui/icon_t.tga",x+16*s,y+10*s,36*s,36*s,theme);
		FixedLabel(index==0?"反恐精英":"恐怖分子",x+64*s,y+7*s,(layout.sideBySide?22:26)*s,theme,false,width-205*s,700);
		int alive=0;for(int slot:teams[index])if(!g_PlayerExtraInfo[slot].dead)++alive;
		char text[64];snprintf(text,sizeof(text),"存活 %d / %d 名玩家",alive,int(teams[index].size()));
		FixedLabel(text,x+64*s,y+34*s,16*s,{182,190,198},false,width-205*s,700);
		if(gHUD.m_Scoreboard.RoundScoreKnown(team))snprintf(text,sizeof(text),"%d",gHUD.m_Scoreboard.RoundScore(team));else snprintf(text,sizeof(text),"--");
		ModernScoreValue(text,x+width-20*s,y+8*s,110*s,38*s,theme);
		const float columnY=y+layout.teamHeading;
		ModernScoreFill(x,columnY,width,layout.columns,{8,11,15,205});
		const float innerRight=x+width-20*s;
		const float pingRight=innerRight,pingWidth=72*s;
		const float deathsRight=pingRight-pingWidth-12*s,deathsWidth=50*s;
		const float killsRight=deathsRight-deathsWidth-12*s,killsWidth=72*s;
		const float hpRight=killsRight-killsWidth-12*s,hpWidth=showHealth?62*s:0;
		const float moneyRight=hpRight-(showHealth?hpWidth+12*s:0),moneyWidth=showMoney?110*s:0;
		const float statusRight=moneyRight-(showMoney?moneyWidth+12*s:0);
		const float statusWidth=max(82*s,hud_unicode::FixedLabelWidth(u8"\u6551\u63f4\u5de5\u5177",layout.font)+4*s);
		const float nameRight=statusRight-statusWidth-12*s;
		const float nameX=x+20*s+layout.avatar+12*s;
		const float labelHeight=16*s,labelY=columnY+5*s;
		FixedLabel("玩家",nameX,labelY,labelHeight,{175,184,194},false,max(1.0f,nameRight-nameX),700);
		ModernScoreValue("延迟",pingRight,labelY,pingWidth,labelHeight,{175,184,194});
		ModernScoreValue("阵亡",deathsRight,labelY,deathsWidth,labelHeight,{175,184,194});
		ModernScoreValue("得分",killsRight,labelY,killsWidth,labelHeight,{175,184,194});
		if(showHealth)ModernScoreValue("生命",hpRight,labelY,hpWidth,labelHeight,{175,184,194});
		if(showMoney)ModernScoreValue("金钱",moneyRight,labelY,moneyWidth,labelHeight,{175,184,194});
		for(int ordinal=0;ordinal<int(teams[index].size());++ordinal)
		{
			const int slot=teams[index][ordinal];const auto &info=g_PlayerInfoList[slot];const auto &extra=g_PlayerExtraInfo[slot];
			const float rowY=layout.RowY(index,ordinal),textY=rowY+(layout.row-layout.font)*.5f;
			const match_ui::Color rowColor=info.thisplayer?match_ui::Color(theme.r/3,theme.g/3,theme.b/3,175):match_ui::Color(30,36,43,ordinal%2?110:65);
			ModernScoreFill(x+4*s,rowY,width-4*s,layout.row-1*s,rowColor);
			if(info.thisplayer)ModernScoreFill(x+4*s,rowY,3*s,layout.row,theme);
			if(gHUD.m_Avatars.Enabled())gHUD.m_Avatars.DrawPlayerAvatar(slot,int(x+20*s),int(rowY+(layout.row-layout.avatar)*.5f),int(layout.avatar));
			const match_ui::Color foreground=extra.dead?match_ui::Color(145,151,158):match_ui::Color(235,238,241);
			match_ui::Text(info.name,nameX,textY,layout.font,foreground,false,max(1.0f,nameRight-nameX));
			const char *status=extra.dead?"阵亡":extra.has_c4?"炸弹":extra.vip?"要员":extra.has_defuse_kit?ObserverKitLabel(slot):"";
			ModernScoreValue(status,statusRight,textY,statusWidth,layout.font,extra.dead?foreground:theme);
			if(showMoney){if(extra.sb_account>=0)snprintf(text,sizeof(text),"$%d",extra.sb_account);else snprintf(text,sizeof(text),"--");ModernScoreValue(text,moneyRight,textY,moneyWidth,layout.font,foreground);}
			if(showHealth){if(extra.sb_health>=0&&!extra.dead)snprintf(text,sizeof(text),"%d",extra.sb_health);else snprintf(text,sizeof(text),"--");ModernScoreValue(text,hpRight,textY,hpWidth,layout.font,foreground);}
			snprintf(text,sizeof(text),"%d",extra.frags);ModernScoreValue(text,killsRight,textY,killsWidth,layout.font,foreground);
			snprintf(text,sizeof(text),"%d",extra.deaths);ModernScoreValue(text,deathsRight,textY,deathsWidth,layout.font,foreground);
			const char *bot=gEngfuncs.PlayerInfo_ValueForKey(slot,"*bot");
			if(bot && atoi(bot)>0)snprintf(text,sizeof(text),"机器人");else if(info.ping>=0)snprintf(text,sizeof(text),"%d",info.ping);else snprintf(text,sizeof(text),"--");
			ModernScoreValue(text,pingRight,textY,pingWidth,layout.font,foreground);
			ModernScoreRow row;row.slot=slot;row.team=team;row.frags=extra.frags;row.deaths=extra.deaths;row.ping=info.ping;row.dead=extra.dead;row.local=info.thisplayer;
			row.bot=bot&&atoi(bot)>0;row.health=showHealth&&!extra.dead?extra.sb_health:-1;row.money=showMoney?extra.sb_account:-1;row.x=x;row.y=rowY;row.width=width;row.height=layout.row;s_modernScoreRows.push_back(row);
		}
	}
	const float footerY=layout.y+layout.height-layout.footer;
	ModernScoreFill(layout.x,footerY,layout.width,layout.footer,{8,11,15,200});
	char label[64];snprintf(label,sizeof(label),"观察者 / 正在加入  %d",int(spectators.size()));
	FixedLabel(label,layout.x+20*s,footerY+12*s,18*s,{160,170,182},false,270*s,700);
	std::string names;for(int slot:spectators){if(!names.empty())names+="   ";names+=g_PlayerInfoList[slot].name;}
	match_ui::Text(names.c_str(),layout.x+300*s,footerY+12*s,18*s,{195,201,207},false,max(1.0f,layout.width-320*s));
	return 1;
}

int ScoreTextWidth(const char *text)
{
	if(UnicodeLabel(text)&&g_pMenu)return int(FixedLabelWidth(text,s_scoreFontHeight)+0.5f);
	return s_scoreStyled ? int(match_ui::TextWidth(text, s_scoreFontHeight) + 0.5f)
		: DrawUtils::HudStringLen(text ? text : "");
}

void ScoreText(int x, int y, int boundary, const char *text, int r, int g, int b)
{
	if (!s_scoreStyled)
	{
		DrawUtils::DrawHudString(x, y, boundary, text, r, g, b);
		return;
	}
	if (boundary <= x) return;
	match_ui::Text(text, x, y + max(0.0f, (s_scoreRowGap-s_scoreFontHeight)*0.5f),
		s_scoreFontHeight, {r, g, b}, false, float(boundary-x));
}

void ScoreTextReverse(int x, int y, int boundary, const char *text, int r, int g, int b)
{
	if(UnicodeLabel(text)&&g_pMenu)
	{
		if(boundary<x)FixedLabel(text,float(x),y+max(0.0f,(s_scoreRowGap-s_scoreFontHeight)*0.5f),s_scoreFontHeight,{r,g,b},true,float(x-boundary));
		return;
	}
	if (!s_scoreStyled)
	{
		DrawUtils::DrawHudStringReverse(x, y, boundary, text, r, g, b);
		return;
	}
	if (boundary >= x) return;
	match_ui::Text(text, x, y + max(0.0f, (s_scoreRowGap-s_scoreFontHeight)*0.5f),
		s_scoreFontHeight, {r, g, b}, true, float(x-boundary));
}

void ScoreNumber(int x, int y, int boundary, int value, int r, int g, int b)
{
	if (!s_scoreStyled)
	{
		DrawUtils::DrawHudNumberString(x, y, boundary, value, r, g, b);
		return;
	}
	char text[24];
	snprintf(text, sizeof(text), "%d", value);
	ScoreTextReverse(x, y, boundary, text, r, g, b);
}
}


// X positions

int xstart, xend;
int ystart, yend;

enum
{
	COL_NAME = 0,
	COL_ATTRIB,
	COL_HP,
	COL_MONEY,
	COL_KILLS,
	COL_DEATHS,
	COL_PING,
	TOTAL_COLUMNS
};

static struct Column
{
	int start, end;
	const char *name;

	Column() :
	    start( 0 ), end( 0 ), name( nullptr ) { }

	Column( int s, const char *n = nullptr, bool reverse = true )
	{
		name = n;
		end = 0;
		start = 0;

		if ( reverse )
		{
			start = s;
			if ( n )
				end = start - ScoreTextWidth( n );
		}
		else
		{
			start = s;
			if ( n )
				end = start + ScoreTextWidth( n );
		}
	}
} g_Columns[TOTAL_COLUMNS];

//#include "vgui_TeamFortressViewport.h"

int CHudScoreboard :: Init( void )
{
	gHUD.AddHudElem( this );

	// Hook messages & commands here
	HOOK_COMMAND( gHUD.m_Scoreboard, "+showscores", ShowScores );
	HOOK_COMMAND( gHUD.m_Scoreboard, "-showscores", HideScores );
	HOOK_COMMAND( gHUD.m_Scoreboard, "showscoreboard2", ShowScoreboard2 );
	HOOK_COMMAND( gHUD.m_Scoreboard, "hidescoreboard2", HideScoreboard2 );

	HOOK_MESSAGE( gHUD.m_Scoreboard, ScoreInfo );
	HOOK_MESSAGE( gHUD.m_Scoreboard, TeamScore );
	HOOK_MESSAGE( gHUD.m_Scoreboard, TeamInfo );
	gEngfuncs.pfnAddCommand("scoreboard_dump",ModernScoreDump);

	InitHUDData();

	cl_showpacketloss = CVAR_CREATE( "cl_showpacketloss", "0", FCVAR_ARCHIVE );
	cl_showplayerversion = CVAR_CREATE( "cl_showplayerversion", "0", 0 );
	cl_show_scoreboard_on_death = CVAR_CREATE( "cl_show_scoreboard_on_death", "0", FCVAR_ARCHIVE );

	return 1;
}


int CHudScoreboard :: VidInit( void )
{
	hud_unicode::ClearFonts();
	xstart = ScreenWidth * 0.125f;
	xend = ScreenWidth - xstart;
	ystart = 100;
	yend = ScreenHeight - ystart;
	m_bForceDraw = false;

	// Load sprites here
	return 1;
}

void CHudScoreboard :: InitHUDData( void )
{
	s_modernScoreRows.clear();s_modernScoreLayout=scoreboard_layout::Layout();s_modernSpectators=0;
	memset( g_PlayerExtraInfo, 0, sizeof g_PlayerExtraInfo );
	m_iLastKilledBy = 0;
	m_fLastKillTime = 0;
	m_iPlayerNum = 0;
	m_iNumTeams = 0;
	memset( g_TeamInfo, 0, sizeof g_TeamInfo );
	memset(m_teamScores, 0, sizeof(m_teamScores));
	memset(m_teamScoreKnown, 0, sizeof(m_teamScoreKnown));
	memset(m_scoreInfoKnown, 0, sizeof(m_scoreInfoKnown));

	for ( int i = 1; i <= MAX_PLAYERS; i++ )
	{
		// a1ba: get the cl.playernum from the engine
		// it shouldn't ever change during normal gameplay
		if( !m_iPlayerNum && EV_IsLocal( i ))
			m_iPlayerNum = i;

		g_PlayerExtraInfo[i].sb_health = -1;
		g_PlayerExtraInfo[i].sb_account = -1;
	}

	m_iFlags &= ~HUD_DRAW;  // starts out inactive

	m_iFlags |= HUD_INTERMISSION; // is always drawn during an intermission
}

bool CHudScoreboard :: ShouldDrawScoreboard() const
{
	if( m_bForceDraw )
		return true;

	if( m_bShowscoresHeld || OC2_ObserverStatsOpen() || gHUD.m_iIntermission )
		return true;

	if( cl_show_scoreboard_on_death && cl_show_scoreboard_on_death->value && gHUD.m_Health.m_iHealth <= 0 )
		return true;

	return false;
}

// Y positions
#define ROW_GAP s_scoreRowGap

int CHudScoreboard :: Draw( float flTime )
{
	if( !ShouldDrawScoreboard( ))
		return 1;

	if( !m_bForceDraw )
	{
		xstart     = 0.125f * ScreenWidth;
		xend       = ScreenWidth - xstart;
		ystart     = 90;
		yend       = ScreenHeight - ystart;
		m_colors.r = 0;
		m_colors.g = 0;
		m_colors.b = 0;
		m_colors.a = 153;
		m_bDrawStroke = true;
	}

	return DrawScoreboard(flTime);
}

int CHudScoreboard :: DrawScoreboard( float fTime )
{
	GetAllPlayersInfo();
	if(match_ui::Enabled() && !m_bForceDraw && gHUD.m_Teamplay) return DrawModernScoreboard();
	char ServerName[90];
	int connected = 0;
	for (int i = 1; i <= MAX_PLAYERS; ++i)
		if (g_PlayerInfoList[i].name && g_PlayerInfoList[i].name[0]) ++connected;
	s_scoreStyled = match_ui::Enabled();
	if (s_scoreStyled)
	{
		const float scale = match_ui::Scale();
		const int rows = max(1, connected + m_iNumTeams * 4 + 9);
		const int preferred = max(6, int((gHUD.m_Avatars.Enabled() ? 36.0f : 28.0f)*scale));
		s_scoreRowGap = max(6, min(preferred, (yend-ystart)/rows));
		s_scoreFontHeight = max(4.0f, min(18.0f*scale, float(s_scoreRowGap-3)));
		s_scoreAvatarSize = gHUD.m_Avatars.Enabled()
			? max(2, min(int(32.0f*scale), s_scoreRowGap-4)) : 0;
	}
	else
	{
		s_scoreRowGap = gHUD.m_Avatars.Enabled()
			? max(15, min(26, (yend-ystart)/max(1, connected+m_iNumTeams*4+5))) : 15;
		s_scoreFontHeight = gHUD.GetCharHeight();
		s_scoreAvatarSize = gHUD.m_Avatars.Enabled() ? s_scoreRowGap-2 : 0;
	}

//	Packetloss removed on Kelly 'shipping nazi' Bailey's orders
//	if ( cl_showpacketloss && cl_showpacketloss->value && ( ScreenWidth >= 400 ) )
//	{
//		can_show_packetloss = 1;
//	}

	// just sort the list on the fly
	// list is sorted first by frags, then by deaths
	float list_slot = 0;

	// calculate columns sizes
	g_Columns[COL_PING] = Column( xend - 15, "延迟" );
	g_Columns[COL_PING].end = min( g_Columns[COL_PING].end, g_Columns[COL_PING].start - ScoreTextWidth( "9999" ) );

	g_Columns[COL_DEATHS] = Column( g_Columns[COL_PING].end - 10, "阵亡" );
	g_Columns[COL_DEATHS].end = min( g_Columns[COL_DEATHS].end, g_Columns[COL_DEATHS].start - ScoreTextWidth( "9999" ) );

	g_Columns[COL_KILLS] = Column( g_Columns[COL_DEATHS].end - 10, "得分" );
	g_Columns[COL_KILLS].end = min( g_Columns[COL_KILLS].end, g_Columns[COL_KILLS].start - ScoreTextWidth( "9999" ) );

	g_Columns[COL_MONEY] = Column( g_Columns[COL_KILLS].end - 10, "金钱" );
	g_Columns[COL_MONEY].end = min( g_Columns[COL_MONEY].end, g_Columns[COL_MONEY].start - ScoreTextWidth( "$999999" ) );

	g_Columns[COL_HP] = Column( g_Columns[COL_MONEY].end - 10, "生命" );
	g_Columns[COL_HP].end = min( g_Columns[COL_HP].end, g_Columns[COL_HP].start - ScoreTextWidth( "999999" ) );

	g_Columns[COL_ATTRIB] = Column( g_Columns[COL_HP].end - 10 );
	g_Columns[COL_ATTRIB].end = g_Columns[COL_ATTRIB].start - ScoreTextWidth( u8"\u6551\u63f4\u5de5\u5177" );

	g_Columns[COL_NAME] = Column( xstart + 15, nullptr, false );
	g_Columns[COL_NAME].end = g_Columns[COL_ATTRIB].end - 10;

	// print the heading line

	DrawUtils::DrawRectangle(xstart, ystart, xend - xstart, yend - ystart,
		m_colors.r, m_colors.g, m_colors.b, m_colors.a, m_bDrawStroke);

	int ypos = ystart + (list_slot * ROW_GAP) + 5;

	if( gHUD.m_szServerName[0] )
		// snprintf( ServerName, 80, "%s", (char*)(gHUD.m_Teamplay ? "TEAMS" : "PLAYERS"), gHUD.m_szServerName );
		strncpy( ServerName, gHUD.m_szServerName, 80 );
	else
		strncpy( ServerName, gHUD.m_Teamplay ? "队伍" : "玩家", 80 );

	if(gHUD.m_szServerName[0])ScoreText( g_Columns[COL_NAME].start, ypos, g_Columns[COL_NAME].end, ServerName, 255, 140, 0 );
	else FixedLabel(ServerName,float(g_Columns[COL_NAME].start),float(ypos),s_scoreFontHeight,{255,140,0},false,float(g_Columns[COL_NAME].end-g_Columns[COL_NAME].start));
	ScoreTextReverse( g_Columns[COL_HP].start, ypos, g_Columns[COL_HP].end, g_Columns[COL_HP].name, 255, 140, 0 );
	ScoreTextReverse( g_Columns[COL_MONEY].start, ypos, g_Columns[COL_MONEY].end, g_Columns[COL_MONEY].name, 255, 140, 0 );
	ScoreTextReverse( g_Columns[COL_KILLS].start, ypos, g_Columns[COL_KILLS].end, g_Columns[COL_KILLS].name, 255, 140, 0 );
	ScoreTextReverse( g_Columns[COL_DEATHS].start, ypos, g_Columns[COL_DEATHS].end, g_Columns[COL_DEATHS].name, 255, 140, 0 );
	ScoreTextReverse( g_Columns[COL_PING].start, ypos, g_Columns[COL_PING].end, g_Columns[COL_PING].name, 255, 140, 0 );

	list_slot += 2;
	ypos = ystart + (list_slot * ROW_GAP);
	FillRGBA( xstart, ypos, xend - xstart, 1, 255, 140, 0, 255);  // draw the separator line

	list_slot += 0.8;

	if ( gHUD.m_Teamplay )
	{
		DrawTeams( list_slot );
	}
	else
	{
		// it's not teamplay,  so just draw a simple player list
		DrawPlayers( list_slot );
	}
	return 1;
}

int CHudScoreboard :: DrawTeams( float list_slot )
{
	int j;
	int ypos = ystart + (list_slot * ROW_GAP) + 5;

	// clear out team scores
	for ( int i = 1; i <= m_iNumTeams; i++ )
	{
		if ( !g_TeamInfo[i].scores_overriden )
			g_TeamInfo[i].frags = g_TeamInfo[i].deaths = 0;
		g_TeamInfo[i].sumping = 0;
		g_TeamInfo[i].players = 0;
		g_TeamInfo[i].already_drawn = FALSE;
	}

	// recalc the team scores, then draw them
	for ( int i = 1; i <= MAX_PLAYERS; i++ )
	{
		if ( !g_PlayerInfoList[i].name || !g_PlayerInfoList[i].name[0] )
			continue; // empty player slot, skip

		if ( g_PlayerExtraInfo[i].teamname[0] == 0 )
			continue; // skip over players who are not in a team

		// find what team this player is in
		for ( j = 1; j <= m_iNumTeams; j++ )
		{
			if ( !stricmp( g_PlayerExtraInfo[i].teamname, g_TeamInfo[j].name ) )
				break;
		}

		if ( j > m_iNumTeams )  // player is not in a team, skip to the next guy
			continue;

		if ( !g_TeamInfo[j].scores_overriden )
		{
			g_TeamInfo[j].frags += g_PlayerExtraInfo[i].frags;
			g_TeamInfo[j].deaths += g_PlayerExtraInfo[i].deaths;
		}

		g_TeamInfo[j].sumping += g_PlayerInfoList[i].ping;

		if ( g_PlayerInfoList[i].thisplayer )
			g_TeamInfo[j].ownteam = TRUE;
		else
			g_TeamInfo[j].ownteam = FALSE;

		g_TeamInfo[j].players++;
	}

	// Draw the teams
	int iSpectatorPos = -1;

	while( true )
	{
		int highest_frags = -99999; int lowest_deaths = 99999;
		int best_team = 0;

		for ( int i = 1; i <= m_iNumTeams; i++ )
		{
			// don't draw team without players
			if ( g_TeamInfo[i].players <= 0 )
				continue;

			if (!strnicmp(g_TeamInfo[i].name, "SPECTATOR", MAX_TEAM_NAME))
			{
				iSpectatorPos = i;
				continue;
			}

			if ( !g_TeamInfo[i].already_drawn && g_TeamInfo[i].frags >= highest_frags )
			{
				if ( g_TeamInfo[i].frags > highest_frags || g_TeamInfo[i].deaths < lowest_deaths )
				{
					best_team = i;
					lowest_deaths = g_TeamInfo[i].deaths;
					highest_frags = g_TeamInfo[i].frags;
				}
			}
		}

		// draw the best team on the scoreboard
		if ( !best_team )
		{
			// if spectators is found and still not drawn
			if( iSpectatorPos != -1 && g_TeamInfo[iSpectatorPos].already_drawn == FALSE )
				best_team = iSpectatorPos;
			else break;
		}
		// draw out the best team
		team_info_t *team_info = &g_TeamInfo[best_team];

		// don't draw team without players
		if ( team_info->players <= 0 )
			continue;

		ypos = ystart + (list_slot * ROW_GAP);

		// check we haven't drawn too far down
		if ( ypos + ROW_GAP > yend )  // keep the full row inside the panel
			break;

		int r, g, b;
		char teamName[64]="";

		GetTeamColor( r, g, b, team_info->teamnumber );
		switch ( team_info->teamnumber )
		{
		case TEAM_TERRORIST:
		{
			snprintf(teamName,sizeof(teamName),"恐怖分子 · %d 人",team_info->players);
			ScoreNumber( g_Columns[COL_KILLS].start, ypos, g_Columns[COL_KILLS].end, team_info->frags, r, g, b );
			break;
		}
		case TEAM_CT:
		{
			snprintf(teamName,sizeof(teamName),"反恐精英 · %d 人",team_info->players);
			ScoreNumber( g_Columns[COL_KILLS].start, ypos, g_Columns[COL_KILLS].end, team_info->frags, r, g, b );
			break;
		}
		case TEAM_SPECTATOR:
		case TEAM_UNASSIGNED:
			snprintf(teamName,sizeof(teamName),"观察者 · %d 人",team_info->players);
			break;
		}

		FixedLabel(teamName,float(g_Columns[COL_NAME].start),float(ypos),s_scoreFontHeight,{r,g,b},false,float(g_Columns[COL_NAME].end-g_Columns[COL_NAME].start));
		ScoreNumber( g_Columns[COL_PING].start, ypos, g_Columns[COL_PING].end, team_info->sumping / team_info->players, r, g, b );

		team_info->already_drawn = TRUE;  // set the already_drawn to be TRUE, so this team won't get drawn again

		// draw underline
		list_slot += 1.2f;
		FillRGBA( xstart, ystart + (list_slot * ROW_GAP), xend - xstart, 1, r, g, b, 255);

		list_slot += 0.4f;
		// draw all the players that belong to this team, indented slightly
		list_slot = DrawPlayers( list_slot, 10, team_info->name );
	}

	// draw all the players who are not in a team
	list_slot += 4.0f;
	DrawPlayers( list_slot, 0, "" );

	return 1;
}

// returns the ypos where it finishes drawing
int CHudScoreboard :: DrawPlayers( float list_slot, int nameoffset, const char *team )
{
	// draw the players, in order,  and restricted to team if set
	while ( 1 )
	{
		// Find the top ranking player
		int highest_frags = -99999;	int lowest_deaths = 99999;
		int best_player = 0;

		for ( int i = 1; i <= MAX_PLAYERS; i++ )
		{
			if ( g_PlayerInfoList[i].name && g_PlayerExtraInfo[i].frags >= highest_frags )
			{
				if ( !(team && stricmp(g_PlayerExtraInfo[i].teamname, team)) )  // make sure it is the specified team
				{
					extra_player_info_t *pl_info = &g_PlayerExtraInfo[i];
					if ( pl_info->frags > highest_frags || pl_info->deaths < lowest_deaths )
					{
						best_player = i;
						lowest_deaths = pl_info->deaths;
						highest_frags = pl_info->frags;
					}
				}
			}
		}

		if ( !best_player )
			break;

		// draw out the best player
		hud_player_info_t *pl_info = &g_PlayerInfoList[best_player];

		int ypos = ystart + (list_slot * ROW_GAP);

		// check we haven't drawn too far down
		if ( ypos + ROW_GAP > yend )  // keep the full avatar and text row inside the panel
			break;

		int r = 255, g = 255, b = 255;
		float *colors = GetClientColor( best_player );
		r *= colors[0];
		g *= colors[1];
		b *= colors[2];

		if(pl_info->thisplayer) // hey, it's me!
		{
			FillRGBABlend( xstart, ypos, xend - xstart, ROW_GAP, 255, 255, 255, 15 );
		}

		int nameX = g_Columns[COL_NAME].start + nameoffset;
		if (s_scoreAvatarSize && nameX+s_scoreAvatarSize+4 < g_Columns[COL_NAME].end)
		{
			gHUD.m_Avatars.DrawPlayerAvatar(best_player, nameX,
				ypos+(ROW_GAP-s_scoreAvatarSize)/2, s_scoreAvatarSize);
			nameX += s_scoreAvatarSize+max(3, int(6*match_ui::Scale()));
		}
		ScoreText(nameX, ypos, g_Columns[COL_NAME].end, pl_info->name, r, g, b);

		if( cl_showplayerversion->value == 0.0f )
		{
			if( team && stricmp( team, "SPECTATOR" ))
			{
				// draw bomb( if player have the bomb )
				if( g_PlayerExtraInfo[best_player].dead )
					ScoreTextReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, "阵亡", r, g, b );
				else if( g_PlayerExtraInfo[best_player].has_c4 )
					ScoreTextReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, "炸弹", r, g, b );
				else if( g_PlayerExtraInfo[best_player].vip )
					ScoreTextReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, "要员",  r, g, b );
				else if (g_PlayerExtraInfo[best_player].has_defuse_kit )
					ScoreTextReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, ObserverKitLabel(best_player),  r, g, b );
			}
		}
		else
		{
			ScoreTextReverse( g_Columns[COL_ATTRIB].start, ypos, g_Columns[COL_ATTRIB].end, gEngfuncs.PlayerInfo_ValueForKey( best_player, "cscl_ver" ),  r, g, b );
		}

		if ( g_PlayerExtraInfo[best_player].sb_health >= 0 && !g_PlayerExtraInfo[best_player].dead )
		{
			if ( gHUD.m_pShowHealth->value )
			{
				static char buf[64];
				sprintf( buf, "%d", g_PlayerExtraInfo[best_player].sb_health );
				ScoreTextReverse( g_Columns[COL_HP].start, ypos, g_Columns[COL_HP].end, buf, r, g, b );
			}
		}

		if ( g_PlayerExtraInfo[best_player].sb_account >= 0 )
		{
			if ( gHUD.m_pShowMoney->value )
			{
				static char buf[64];
				sprintf( buf, "$%d", g_PlayerExtraInfo[best_player].sb_account );
				ScoreTextReverse( g_Columns[COL_MONEY].start, ypos, g_Columns[COL_MONEY].end, buf, r, g, b );
			}
		}

		// draw kills (right to left)
		if( team && stricmp( team, "SPECTATOR" ) )
		{
			ScoreNumber( g_Columns[COL_KILLS].start, ypos, g_Columns[COL_KILLS].end, g_PlayerExtraInfo[best_player].frags, r, g, b );

			// draw deaths
			ScoreNumber( g_Columns[COL_DEATHS].start, ypos, g_Columns[COL_DEATHS].end, g_PlayerExtraInfo[best_player].deaths, r, g, b );
		}

		// draw ping & packetloss
		const char *value;
		if( pl_info->ping <= 5  // must be 0, until Xash's bug not fixed
			&& ( value = gEngfuncs.PlayerInfo_ValueForKey( best_player, "*bot" ) )
			&& atoi( value ) > 0 )
		{
			ScoreTextReverse( g_Columns[COL_PING].start, ypos, g_Columns[COL_PING].end, "机器人", r, g, b );
		}
		else
		{
			static char buf[64];
			sprintf( buf, "%d", pl_info->ping );
			ScoreTextReverse( g_Columns[COL_PING].start, ypos, g_Columns[COL_PING].end, buf, r, g, b );
		}

		pl_info->name = NULL;  // set the name to be NULL, so this client won't get drawn again
		list_slot++;
	}

	list_slot += 2.0f;

	return list_slot;
}


void CHudScoreboard :: GetAllPlayersInfo( void )
{
	memset( &g_PlayerInfoList[0], 0, sizeof( g_PlayerInfoList[0] ));

	for( int i = 1; i <= MAX_PLAYERS; i++ )
		GetPlayerInfo( i, &g_PlayerInfoList[i] );
}

int CHudScoreboard :: MsgFunc_ScoreInfo( const char *pszName, int iSize, void *pbuf )
{
	m_iFlags |= HUD_DRAW;

	BufferReader reader( pszName, pbuf, iSize );
	short cl = reader.ReadByte();
	short frags = reader.ReadShort();
	short deaths = reader.ReadShort();
	short playerclass = reader.ReadShort();
	reader.ReadShort();

	if ( cl > 0 && cl <= MAX_PLAYERS )
	{
		m_scoreInfoKnown[cl] = true;
		g_PlayerExtraInfo[cl].frags = frags;
		g_PlayerExtraInfo[cl].deaths = deaths;
		g_PlayerExtraInfo[cl].playerclass = playerclass;
		gHUD.m_Avatars.NoteMessage(1, cl);

		//gViewPort->UpdateOnPlayerInfo();
	}

	return 1;
}

// Message handler for TeamInfo message
// accepts two values:
//		byte: client number
//		string: client team name
int CHudScoreboard :: MsgFunc_TeamInfo( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );
	short cl = reader.ReadByte();
	int teamNumber = 0;

	if ( cl > 0 && cl <= MAX_PLAYERS )
	{
		// set the players team
		char teamName[MAX_TEAM_NAME];
		strncpy( teamName, reader.ReadString(), MAX_TEAM_NAME );
		teamName[MAX_TEAM_NAME-1] = 0;

		if( !strcmp( teamName, "TERRORIST") )
			teamNumber = TEAM_TERRORIST;
		else if( !strcmp( teamName, "CT") )
			teamNumber = TEAM_CT;
		else if( !strcmp( teamName, "SPECTATOR" ) )
		{
			teamNumber = TEAM_SPECTATOR;
		}
		else if( !strcmp( teamName, "UNASSIGNED" ) )
		{
			teamNumber = TEAM_UNASSIGNED;
			strncpy( teamName, "SPECTATOR", MAX_TEAM_NAME );
		}
		// just in case
		else teamNumber = TEAM_UNASSIGNED;

		strncpy( g_PlayerExtraInfo[cl].teamname, teamName, MAX_TEAM_NAME );
		g_PlayerExtraInfo[cl].teamnumber = teamNumber;
		gHUD.m_Avatars.NoteMessage(0, cl);
	}

	// rebuild the list of teams

	// clear out player counts from teams
	for ( int i = 1; i <= m_iNumTeams; i++ )
	{
		g_TeamInfo[i].players = 0;
	}

	// rebuild the team list
	GetAllPlayersInfo();
	m_iNumTeams = 0;

	for ( int i = 1; i <= MAX_PLAYERS; i++ )
	{
		int j;
		//if ( g_PlayerInfoList[i].name == NULL )
		//	continue;

		if ( g_PlayerExtraInfo[i].teamname[0] == 0 )
			continue; // skip over players who are not in a team

		// is this player in an existing team?
		for ( j = 1; j <= m_iNumTeams; j++ )
		{
			if ( g_TeamInfo[j].name[0] == '\0' )
				break;

			if ( !stricmp( g_PlayerExtraInfo[i].teamname, g_TeamInfo[j].name ) )
				break;
		}

		if ( j > m_iNumTeams )
		{
			// they aren't in a listed team, so make a new one
			for ( j = 1; j <= m_iNumTeams; j++ )
			{
				if ( g_TeamInfo[j].name[0] == '\0' )
					break;
			}


			m_iNumTeams = max( j, m_iNumTeams );

			strncpy( g_TeamInfo[j].name, g_PlayerExtraInfo[i].teamname, MAX_TEAM_NAME );
			g_TeamInfo[j].teamnumber = g_PlayerExtraInfo[i].teamnumber;
			g_TeamInfo[j].players = 0;
		}

		g_TeamInfo[j].players++;
	}

	// clear out any empty teams
	for ( int i = 1; i <= m_iNumTeams; i++ )
	{
		const int number = g_TeamInfo[i].teamnumber;
		if (number >= 1 && number <= 2 && m_teamScoreKnown[number])
		{
			g_TeamInfo[i].scores_overriden = TRUE;
			g_TeamInfo[i].frags = m_teamScores[number];
		}
		if ( g_TeamInfo[i].players < 1 )
			memset( &g_TeamInfo[i], 0, sizeof(team_info_t) );
	}

	return 1;
}

// Message handler for TeamScore message
// accepts three values:
//		string: team name
//		short: teams kills
//		short: teams deaths
// if this message is never received, then scores will simply be the combined totals of the players.
int CHudScoreboard :: MsgFunc_TeamScore( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );
	char *TeamName = reader.ReadString();
	int i;
	const int score = reader.ReadShort();
	const int number = !stricmp(TeamName, "CT") ? TEAM_CT
		: !stricmp(TeamName, "TERRORIST") ? TEAM_TERRORIST : TEAM_UNASSIGNED;
	// ReGameDLL sends TeamScore before TeamInfo on join. Preserve it independently.
	if (number >= 1 && number <= 2)
	{
		m_teamScores[number] = score;
		m_teamScoreKnown[number] = true;
	}

	// find the team matching the name
	for ( i = 1; i <= m_iNumTeams; i++ )
	{
		if ( !stricmp( TeamName, g_TeamInfo[i].name ) )
			break;
	}
	if ( i > m_iNumTeams )
	{
		reader.Flush();
		return 1;
	}

	// use this new score data instead of combined player scores
	g_TeamInfo[i].scores_overriden = TRUE;
	g_TeamInfo[i].frags = score;
	// g_TeamInfo[i].deaths = reader.ReadShort();

	return 1;
}

void CHudScoreboard :: DeathMsg( int killer, int victim )
{
	// if we were the one killed,  or the world killed us, set the scoreboard to indicate suicide
	if ( victim == m_iPlayerNum || killer == 0 )
	{
		m_iLastKilledBy = killer ? killer : m_iPlayerNum;
		m_fLastKillTime = gHUD.m_flTime + 10;	// display who we were killed by for 10 seconds

		if ( killer == m_iPlayerNum )
			m_iLastKilledBy = m_iPlayerNum;
	}
}



void CHudScoreboard :: UserCmd_ShowScores( void )
{
	m_bForceDraw = false;
	m_bShowscoresHeld = true;
}

void CHudScoreboard :: UserCmd_HideScores( void )
{
	m_bForceDraw = m_bShowscoresHeld = false;
}


void CHudScoreboard	:: UserCmd_ShowScoreboard2()
{
	if( gEngfuncs.Cmd_Argc() != 9 )
	{
		ConsolePrint("showscoreboard2 <xstart> <xend> <ystart> <yend> <r> <g> <b> <a>");
	}

	xstart     = atof(gEngfuncs.Cmd_Argv(1)) * ScreenWidth;
	xend       = atof(gEngfuncs.Cmd_Argv(2)) * ScreenWidth;
	ystart     = atof(gEngfuncs.Cmd_Argv(3)) * ScreenHeight;
	yend       = atof(gEngfuncs.Cmd_Argv(4)) * ScreenHeight;
	m_colors.r = atoi(gEngfuncs.Cmd_Argv(5));
	m_colors.b = atoi(gEngfuncs.Cmd_Argv(6));
	m_colors.b = atoi(gEngfuncs.Cmd_Argv(7));
	m_colors.a = atoi(gEngfuncs.Cmd_Argv(8));
	m_bDrawStroke = false;
	m_bForceDraw = true;
}

void CHudScoreboard :: UserCmd_HideScoreboard2()
{
	m_bForceDraw = m_bShowscoresHeld = false; // and disable it
}
