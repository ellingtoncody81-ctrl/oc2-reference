/***
*
*	Copyright (c) 1996-2002, Valve LLC. All rights reserved.
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
// menu.cpp
//
// generic menu handler
//
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "com_weapons.h"
#include "com_model.h"
#include "buy_menu_state.h"

#include <string.h>
#include <stdio.h>
#include "draw_util.h"
#include "strl.h"
#include "hud/unicode_labels.h"
#include <string>
#include <vector>

//#include "vgui_TeamFortressViewport.h"

#define MAX_MENU_STRING	512

char g_szMenuString[MAX_MENU_STRING];
char g_szPrelocalisedMenuString[MAX_MENU_STRING];

namespace
{
bool s_radioMenu = false;

// Translate only the known English titles.txt strings. Custom translations and
// server/plugin text pass through unchanged, and slot numbers remain untouched.
const char *RadioChinese(const char *text)
{
	struct Translation { const char *source; const char *translated; };
	static const Translation translations[] = {
		{"Radio Commands", u8"\u65e0\u7ebf\u7535\u6307\u4ee4"},
		{"Group Radio Commands", u8"\u56e2\u961f\u65e0\u7ebf\u7535\u6307\u4ee4"},
		{"Radio Responses/Reports", u8"\u65e0\u7ebf\u7535\u56de\u5e94\u4e0e\u62a5\u544a"},
		{"\"Cover Me\"", u8"\u63a9\u62a4\u6211"},
		{"\"You Take the Point\"", u8"\u4f60\u62c5\u4efb\u5c16\u5175"},
		{"\"Hold This Position\"", u8"\u575a\u5b88\u6b64\u5904"},
		{"\"Regroup Team\"", u8"\u961f\u5458\u96c6\u5408"},
		{"\"Follow Me\"", u8"\u8ddf\u7740\u6211"},
		{"\"Taking Fire, Need Assistance\"", u8"\u906d\u9047\u653b\u51fb\uff0c\u9700\u8981\u652f\u63f4"},
		{"\"Go\"", u8"\u524d\u8fdb"},
		{"\"Fall Back\"", u8"\u64a4\u9000"},
		{"\"Stick Together Team\"", u8"\u961f\u5458\u4fdd\u6301\u5728\u4e00\u8d77"},
		{"\"Get in Position\"", u8"\u5c31\u4f4d"},
		{"\"Storm the Front\"", u8"\u653b\u51fb\u524d\u7ebf"},
		{"\"Report In\"", u8"\u62a5\u544a\u60c5\u51b5"},
		{"\"Affirmative/Roger\"", u8"\u6536\u5230"},
		{"\"Enemy Spotted\"", u8"\u53d1\u73b0\u654c\u4eba"},
		{"\"Need Backup\"", u8"\u9700\u8981\u652f\u63f4"},
		{"\"Sector Clear\"", u8"\u533a\u57df\u5b89\u5168"},
		{"\"I'm in Position\"", u8"\u6211\u5df2\u5c31\u4f4d"},
		{"\"Reporting In\"", u8"\u6b63\u5728\u62a5\u544a"},
		{"\"She's gonna Blow!\"", u8"\u5feb\u7206\u70b8\u4e86\uff01"},
		{"\"Negative\"", u8"\u4e0d\u884c"},
		{"\"Enemy Down\"", u8"\u654c\u4eba\u5df2\u88ab\u51fb\u5012"},
		{"Exit", u8"\u9000\u51fa"}
	};
	for (const auto &entry : translations)
		if (!strcmp(text,entry.source)) return entry.translated;
	return text;
}

// Keep the server slot numbers and remove legacy color escapes for the panel.
bool DrawRadioMenu(const char *text, int validSlots)
{
	if (!g_pMenu || !text || !*text || !hud_unicode::LabelFont(16*match_ui::Scale())) return false;
	std::vector<std::string> lines;
	std::string line;
	for (const char *p=text;; ++p)
	{
		if (*p=='\\' && p[1] && p[1]!='\n' && strchr("ywrRd",p[1])) { ++p; continue; }
		if (*p=='\n' || !*p)
		{
			const size_t first=line.find_first_not_of(" \t\r");
			if (first!=std::string::npos)
			{
				const size_t last=line.find_last_not_of(" \t\r");
				lines.push_back(line.substr(first,last-first+1));
			}
			line.clear();
			if (!*p) break;
		}
		else line+=*p;
	}
	if (lines.size()<2) return false;
	const float scale=match_ui::Scale();
	const float row=26.0f*scale, header=34.0f*scale;
	const float width=(std::min)(390.0f*scale,ScreenWidth*0.44f);
	const float height=header+row*float(lines.size()-1)+6.0f*scale;
	const float x=18.0f*scale;
	const float y=(std::max)(8.0f*scale,(ScreenHeight-height)*0.5f);
	FillRGBABlend(int(x),int(y),int(width),int(height),12,16,22,215);
	FillRGBABlend(int(x),int(y),int(3*scale),int(height),112,158,184,235);
	hud_unicode::FixedLabel(RadioChinese(lines[0].c_str()),x+14*scale,y+5*scale,17*scale,{225,232,238},false,width-24*scale);
	for (size_t i=1; i<lines.size(); ++i)
	{
		const std::string &entry=lines[i];
		const float top=y+header+float(i-1)*row;
		if ((i&1)==0) FillRGBABlend(int(x+3*scale),int(top),int(width-3*scale),int(row),255,255,255,12);
		if (entry.size()>=2 && entry[0]>='0' && entry[0]<='9' && (entry[1]=='.' || entry[1]==')'))
		{
			const int slot=entry[0]=='0'?10:entry[0]-'0';
			const bool enabled=(validSlots & (1<<(slot-1)))!=0;
			const char number[2]={entry[0],0};
			const size_t first=entry.find_first_not_of(" \t",2);
			hud_unicode::FixedLabel(number,x+14*scale,top+3*scale,16*scale,enabled?match_ui::Color(133,187,214):match_ui::Color(115,120,126));
			if (first!=std::string::npos)
				hud_unicode::FixedLabel(RadioChinese(entry.c_str()+first),x+38*scale,top+3*scale,16*scale,enabled?match_ui::Color(231,235,239):match_ui::Color(115,120,126),false,width-48*scale);
		}
		else hud_unicode::FixedLabel(entry.c_str(),x+14*scale,top+3*scale,16*scale,{231,235,239},false,width-24*scale);
	}
	return true;
}
}

int KB_ConvertString( char *in, char **ppout );

static void DumpTeamSelectionPlayer()
{
	const cl_entity_t *entity=gEngfuncs.GetLocalPlayer();
	const int index=entity?entity->index:0;
	const bool valid=index>0&&index<=MAX_PLAYERS;
	hud_player_info_t info{};
	if(valid)GetPlayerInfo(index,&info);
	const char *model=info.model;
	const int team=valid?g_PlayerExtraInfo[index].teamnumber:0;
	const bool alive=valid&&g_iUser1==0&&gHUD.m_Health.m_iHealth>0&&!g_PlayerExtraInfo[index].dead;
	gEngfuncs.Con_Printf("teamselect_player: index=%d team=%d model=%s entity_model=%s alive=%d health=%d\n",
		index,team,model&&*model?model:"none",entity&&entity->model?entity->model->name:"none",int(alive),gHUD.m_Health.m_iHealth);
}

void Touch_CloseMenu()
{
	gMobileAPI.pfnTouchRemoveButton( "_menu_*" );
	gMobileAPI.pfnTouchSetClientOnly( 0 );
}

int CHudMenu :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_Menu, ShowMenu );
	HOOK_MESSAGE( gHUD.m_Menu, VGUIMenu );
	HOOK_MESSAGE( gHUD.m_Menu, BuyClose );
	HOOK_MESSAGE( gHUD.m_Menu, AllowSpec );
	HOOK_COMMAND( gHUD.m_Menu, "client_buy_open", OldStyleMenuOpen );
	HOOK_COMMAND( gHUD.m_Menu, "client_buy_close", OldStyleMenuClose );
	HOOK_COMMAND( gHUD.m_Menu, "showvguimenu", ShowVGUIMenu );
	gEngfuncs.pfnAddCommand("teamselect_player_dump",DumpTeamSelectionPlayer);

	_extended_menus = CVAR_CREATE("_extended_menus", "1", FCVAR_ARCHIVE);

	InitHUDData();

	m_bAllowSpec = true; // by default, spectating is allowed

	return 1;
}

void CHudMenu :: InitHUDData( void )
{
	buy_menu_state::ClearMap();
	m_fMenuDisplayed = 0;
	m_bitsValidSlots = 0;
	Reset();
}

void CHudMenu :: Reset( void )
{
	s_radioMenu = false;
	g_szPrelocalisedMenuString[0] = 0;
	m_fWaitingForMore = FALSE;
}

int CHudMenu :: VidInit( void )
{
	return 1;
}

int CHudMenu :: Draw( float flTime )
{
	// check for if menu is set to disappear
	if ( m_flShutoffTime > 0 )
	{
		if ( m_flShutoffTime <= gHUD.m_flTime )
		{  // times up, shutoff
			UserCmd_OldStyleMenuClose();
			return 1;
		}
	}
	if (s_radioMenu && DrawRadioMenu(g_szMenuString,m_bitsValidSlots)) return 1;

	// don't draw the menu if the scoreboard is being shown
	//if ( gViewPort && gViewPort->IsScoreBoardVisible() )
		//return 1;

	// draw the menu, along the left-hand side of the screen

	// count the number of newlines
	int nlc = 0;
	int i;
	for ( i = 0; i < MAX_MENU_STRING && g_szMenuString[i] != '\0'; i++ )
	{
		if ( g_szMenuString[i] == '\n' )
			nlc++;
	}

	// center it
	int y = (ScreenHeight/2) - ((nlc/2)*12) - 40; // make sure it is above the say text
	int x = 20;

	i = 0;
	while ( i < MAX_MENU_STRING && g_szMenuString[i] != '\0' )
	{
		DrawUtils::DrawHudString( x, y, 320, g_szMenuString + i, 255, 255, 255 );
		y += 24;

		while ( i < MAX_MENU_STRING && g_szMenuString[i] != '\0' && g_szMenuString[i] != '\n' )
			i++;
		if ( g_szMenuString[i] == '\n' )
			i++;
	}

	return 1;
}

// selects an item from the menu
void CHudMenu :: SelectMenuItem( int menu_item )
{
	// if menu_item is in a valid slot,  send a menuselect command to the server
	if ( (menu_item > 0) && (m_bitsValidSlots & (1 << (menu_item-1))) )
	{
		char szbuf[32];
		sprintf( szbuf, "menuselect %d\n", menu_item );
		ClientCmd( szbuf );

		UserCmd_OldStyleMenuClose();
	}
}


// Message handler for ShowMenu message
// takes four values:
//		short: a bitfield of keys that are valid input
//		char : the duration, in seconds, the menu should stay up. -1 means is stays until something is chosen.
//		byte : a boolean, TRUE if there is more string yet to be received before displaying the menu, FALSE if it's the last string
//		string: menu string to display
// if this message is never received, then scores will simply be the combined totals of the players.
int CHudMenu :: MsgFunc_ShowMenu( const char *pszName, int iSize, void *pbuf )
{
	char *temp = NULL, *menustring;

	BufferReader reader( pszName, pbuf, iSize );

	m_bitsValidSlots = reader.ReadShort();
	int DisplayTime = reader.ReadChar();
	int NeedMore = reader.ReadByte();

	if ( DisplayTime > 0 )
		m_flShutoffTime = DisplayTime + gHUD.m_flTime;
	else
		m_flShutoffTime = -1;

	if ( !m_bitsValidSlots )
	{
		UserCmd_OldStyleMenuClose(); // no valid slots means that the menu should be turned off
		return 1;
	}

	menustring = reader.ReadString();
	if (!m_fWaitingForMore) s_radioMenu = false;
	// Route known server roots on either protocol; arbitrary plugin menus retain
	// their original text/slot handling. Class roots acknowledge jointeam.
	if(!m_fWaitingForMore&&!NeedMore&&g_pMenu&&!UseOldTouchMenusEnabled())
	{
		const bool teamMenu=!strcmp(menustring,"#Team_Select")||!strcmp(menustring,"#Team_Select_Spect")||
			!strcmp(menustring,"#IG_Team_Select")||!strcmp(menustring,"#IG_Team_Select_Spect")||
			!strcmp(menustring,"#IG_VIP_Team_Select")||!strcmp(menustring,"#IG_VIP_Team_Select_Spect");
		const int classMenu=!strcmp(menustring,"#Terrorist_Select")?MENU_CLASS_T:
			!strcmp(menustring,"#CT_Select")?MENU_CLASS_CT:0;
		if(teamMenu||classMenu)
		{
			m_fMenuDisplayed=0;m_iFlags&=~HUD_DRAW;
			Touch_CloseMenu();
			if(teamMenu)
			{
				const int flags=((m_bitsValidSlots&(1<<5))?1:0)|((m_bitsValidSlots&(1<<2))?2:0)|((m_bitsValidSlots&(1<<9))?4:0);
				const int team=g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber;
				g_pMenu->ShowVGUIMenu(MENU_TEAM,flags,team);
			}
			else ShowVGUIMenu(classMenu);
			return 1;
		}
	}
	if (!m_fWaitingForMore && !NeedMore && !strcmp(menustring,"#Buy") && g_pMenu && !UseOldTouchMenusEnabled())
	{
		m_fMenuDisplayed = 0;
		m_iFlags &= ~HUD_DRAW;
		ShowVGUIMenu(MENU_BUY);
		return 1;
	}

	// menu will be replaced by scripted touch config
	// so execute it and exit
	bool useScriptedTouchMenus = UseOldTouchMenusEnabled();
#ifdef __ANDROID__
	useScriptedTouchMenus = true;
#endif
	if( _extended_menus->value != 0.0f && useScriptedTouchMenus )
	{
		if( !strncmp(menustring, "#Radio", 6 ) )
		{
			if( menustring[6] == 'A' )
			{
				ShowVGUIMenu(MENU_RADIOA); return 1;
			}
			else if( menustring[6] == 'B' )
			{
				ShowVGUIMenu(MENU_RADIOB); return 1;
			}
			else if( menustring[6] == 'C' )
			{
				ShowVGUIMenu(MENU_RADIOC); return 1;
			}
			else ShowVGUIMenu( MENU_NUMERICAL_MENU ); // we just show touch screen numbers
		}
		else ShowVGUIMenu(MENU_NUMERICAL_MENU);
	}
	else ShowVGUIMenu(MENU_NUMERICAL_MENU);

	if ( !m_fWaitingForMore ) // this is the start of a new menu
	{
		strlcpy( g_szPrelocalisedMenuString, menustring, sizeof( g_szPrelocalisedMenuString ) );
	}
	else
	{  // append to the current menu string
		strlcat( g_szPrelocalisedMenuString, menustring, sizeof( g_szPrelocalisedMenuString ) );
	}

	if ( !NeedMore )
	{  // we have the whole string, so we can localise it now
		s_radioMenu = !strcmp(g_szPrelocalisedMenuString,"#RadioA") ||
			!strcmp(g_szPrelocalisedMenuString,"#RadioB") || !strcmp(g_szPrelocalisedMenuString,"#RadioC");
		strlcpy( g_szMenuString, gHUD.m_TextMessage.BufferedLocaliseTextString( g_szPrelocalisedMenuString ), sizeof( g_szMenuString ) );
		// Swap in characters
		if ( KB_ConvertString( g_szMenuString, &temp ) )
		{
			strlcpy( g_szMenuString, temp, sizeof( g_szMenuString ) );
			free( temp );
		}
	}

	m_fMenuDisplayed = 1;
	m_iFlags |= HUD_DRAW;

	m_fWaitingForMore = NeedMore;

	return 1;
}

int CHudMenu::MsgFunc_VGUIMenu( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );

	int menuType = reader.ReadByte();
	m_bitsValidSlots = reader.ReadShort(); // is ignored

	ShowVGUIMenu(menuType);
	return 1;
}

int CHudMenu::MsgFunc_BuyClose(const char *pszName, int iSize, void *pbuf)
{
	buy_menu_state::Close();
	Touch_CloseMenu();

	if (g_pMenu)
		g_pMenu->HideVGUIMenu();

	return 1;
}

int CHudMenu::MsgFunc_AllowSpec(const char *pszName, int iSize, void *pbuf)
{
	BufferReader reader( pszName, pbuf, iSize );

	m_bAllowSpec = (bool)reader.ReadByte();

	return 1;
}

void CHudMenu::UserCmd_OldStyleMenuOpen()
{
	m_flShutoffTime = -1; // stay open until user will not close it
	strlcpy( g_szMenuString, gHUD.m_TextMessage.BufferedLocaliseTextString("Buy"), sizeof( g_szMenuString ) );
}

void CHudMenu::UserCmd_OldStyleMenuClose()
{
	buy_menu_state::Close();
	s_radioMenu = false;
	m_fWaitingForMore = FALSE;
	m_fMenuDisplayed = 0; // no valid slots means that the menu should be turned off
	m_iFlags &= ~HUD_DRAW;

	Touch_CloseMenu();
}

// lol, no real VGUI here
// it's really good only for touchscreen

void CHudMenu::ShowVGUIMenu( int menuType )
{
	int team = g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber;
	int haveCancel = team != TEAM_UNASSIGNED ? 1 : 0;

	if( g_pMenu && !UseOldTouchMenusEnabled() )
	{
		switch( menuType )
		{
		case MENU_TEAM:
		{
			int param = 0;
			if( m_bAllowSpec )
			{
				if( g_iTeamNumber == TEAM_UNASSIGNED ||
					g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].dead ||
					!g_iFreezeTimeOver )
				{
					param |= 1 << 0;
				}
			}

			if( IsASMapType() && g_iTeamNumber == TEAM_CT )
				param |= 1 << 1;

			if( haveCancel )
				param |= 1 << 2;

			g_pMenu->ShowVGUIMenu( menuType, param, team );
			return;
		}
		case MENU_CLASS_T:
		case MENU_CLASS_CT:
			g_pMenu->ShowVGUIMenu( menuType, gHUD.GetGameType() == GAME_CZERO, haveCancel );
			return;
		case MENU_BUY:
		case MENU_BUY_PISTOL:
		case MENU_BUY_SHOTGUN:
		case MENU_BUY_RIFLE:
		case MENU_BUY_SUBMACHINEGUN:
		case MENU_BUY_MACHINEGUN:
		case MENU_BUY_ITEM:
			g_pMenu->ShowVGUIMenu( menuType, gHUD.GetGameType() == GAME_CZERO, team );
			return;
		}
	}

	const char *szCmd;

	switch(menuType)
	{
	case MENU_TEAM:
		szCmd = "exec touch/chooseteam.cfg";
		break;
	case MENU_CLASS_T:
		szCmd = "exec touch/chooseteam_tr.cfg";
		break;
	case MENU_CLASS_CT:
		szCmd = "exec touch/chooseteam_ct.cfg";
		break;
	case MENU_BUY:
		szCmd = "exec touch/buy.cfg";
		break;
	case MENU_BUY_PISTOL:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_pistol_t.cfg";
		else szCmd = "exec touch/buy_pistol_ct.cfg";
		break;
	case MENU_BUY_SHOTGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_shotgun_t.cfg";
		else szCmd = "exec touch/buy_shotgun_ct.cfg";
		break;
	case MENU_BUY_RIFLE:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_rifle_t.cfg";
		else szCmd ="exec touch/buy_rifle_ct.cfg";
		break;
	case MENU_BUY_SUBMACHINEGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_submachinegun_t.cfg";
		else szCmd = "exec touch/buy_submachinegun_ct.cfg";
		break;
	case MENU_BUY_MACHINEGUN:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_machinegun_t.cfg";
		else szCmd = "exec touch/buy_machinegun_ct.cfg";
		break;
	case MENU_BUY_ITEM:
		if( g_PlayerExtraInfo[gHUD.m_Scoreboard.m_iPlayerNum].teamnumber == TEAM_TERRORIST )
			szCmd = "exec touch/buy_item_t.cfg";
		else szCmd = "exec touch/buy_item_ct.cfg";
		break;
	case MENU_RADIOA:
		szCmd = "exec touch/radioa.cfg";
		break;
	case MENU_RADIOB:
		szCmd = "exec touch/radiob.cfg";
		break;
	case MENU_RADIOC:
		szCmd = "exec touch/radioc.cfg";
		break;
	case MENU_RADIOSELECTOR:
		szCmd = "exec touch/radioselector.cfg";
		break;
	case MENU_BUY_CSDM:
		szCmd = "exec touch/custom/dm_menu.cfg";
		break;
	case MENU_NUMERICAL_MENU:
#ifdef __ANDROID__
		szCmd = "exec touch/numerical_menu.cfg";
		break;
#else
		return;
#endif
	default:
		UserCmd_OldStyleMenuClose();
		return;
	}

	m_fMenuDisplayed = 1;
	ClientCmd(szCmd);
}

void CHudMenu::UserCmd_ShowVGUIMenu()
{
	if( gEngfuncs.Cmd_Argc() < 2 )
	{
		ConsolePrint("usage: showvguimenu <menuType>\n");
		return;
	}

	int menuType = atoi(gEngfuncs.Cmd_Argv(1));
	ShowVGUIMenu(menuType);
}
