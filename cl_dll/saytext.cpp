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
// saytext.cpp
//
// implementation of CHudSayText class
//

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "vgui_parser.h"
#include "draw_util.h"
#include "com_weapons.h"
#include "utlstring.h"
#include "strl.h"
#include "hud/unicode_labels.h"
#include "hud/match_notice_layout.h"
//#include "vgui_TeamFortressViewport.h"

extern float *GetClientColor( int clientIndex );

#define MAX_LINES	5
#define MAX_CHARS_PER_LINE	1024  /* it can be less than this, depending on char size */

// allow 20 pixels on either side of the text
#define MAX_LINE_WIDTH  ( ScreenWidth - 40 )
#define LINE_START  10

static void StripSayTextControlCodes( const char *src, char *dst, int dstSize )
{
	if ( !src || dstSize <= 0 )
		return;

	int j = 0;
	for ( const char *p = src; *p && j < dstSize - 1; ++p )
	{
		if ( *p == '\x01' || *p == '\x02' || *p == '\x03' || *p == '\x04' )
			continue;

		dst[j++] = *p;
	}

	dst[j] = '\0';
}

static char g_szLineBuffer[ MAX_LINES + 1 ][ MAX_CHARS_PER_LINE ];
static float *g_pflNameColors[ MAX_LINES + 1 ];
static int g_iNameLengths[ MAX_LINES + 1 ];
static float g_lineBirth[MAX_LINES+1];
static float flScrollTime = 0;  // the time at which the lines next scroll up

static int Y_START = 0;
static int line_height = 0;

static void NoticeDump()
{
	const auto area=match_notice::Feed(ScreenWidth,ScreenHeight,g_iUser1!=0);
	gEngfuncs.Con_Printf("notice_feed: time=%.3f observer=%d x=%.1f bottom=%.1f width=%.1f font=%.1f line=%d\n",
		gHUD.m_flTime,int(g_iUser1!=0),area.x,area.y,area.width,area.height,line_height);
	for(int i=0;i<MAX_LINES;++i)if(g_szLineBuffer[i][0])
		gEngfuncs.Con_Printf("notice_line: index=%d born=%.3f text=%s\n",i,g_lineBirth[i],g_szLineBuffer[i]);
}

int CHudSayText :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_SayText, SayText );
	gEngfuncs.pfnAddCommand("hud_notice_dump",NoticeDump);

	InitHUDData();

	m_HUD_saytext =			gEngfuncs.pfnRegisterVariable( "hud_saytext_internal", "1", 0 );
	m_HUD_saytext_time =	gEngfuncs.pfnRegisterVariable( "hud_saytext_time", "15.5", 0 );
	m_HUD_saytext_console =	gEngfuncs.pfnRegisterVariable( "hud_saytext_console", "1", 0 );

	m_iFlags |= HUD_INTERMISSION; // is always drawn during an intermission

	return 1;
}


void CHudSayText :: InitHUDData( void )
{
	memset( g_szLineBuffer, 0, sizeof g_szLineBuffer );
	memset( g_pflNameColors, 0, sizeof g_pflNameColors );
	memset( g_iNameLengths, 0, sizeof g_iNameLengths );
	memset(g_lineBirth,0,sizeof(g_lineBirth));
}

int CHudSayText :: VidInit( void )
{
	return 1;
}


int ScrollTextUp( void )
{
	g_szLineBuffer[MAX_LINES][0] = 0;
	memmove( g_szLineBuffer[0], g_szLineBuffer[1], sizeof(g_szLineBuffer) - sizeof(g_szLineBuffer[0]) ); // overwrite the first line // -V512
	memmove( &g_pflNameColors[0], &g_pflNameColors[1], sizeof(g_pflNameColors) - sizeof(g_pflNameColors[0]) );
	memmove( &g_iNameLengths[0], &g_iNameLengths[1], sizeof(g_iNameLengths) - sizeof(g_iNameLengths[0]) );
	memmove(g_lineBirth,g_lineBirth+1,sizeof(g_lineBirth)-sizeof(g_lineBirth[0]));
	g_szLineBuffer[MAX_LINES-1][0] = 0;

	if ( g_szLineBuffer[0][0] == ' ' ) // also scroll up following lines
	{
		g_szLineBuffer[0][0] = 2;
		return 1 + ScrollTextUp();
	}

	return 1;
}

int CHudSayText :: Draw( float flTime )
{
	int y = Y_START;
	const bool modern=match_ui::Enabled()&&g_pMenu;
	const float duration=max(1.f,min(30.f,m_HUD_saytext_time->value));

	//if ( ( gViewPort && gViewPort->AllowedToPrintText() == FALSE) || !m_HUD_saytext->value )
	if ( !m_HUD_saytext->value )
		return 1;

	// make sure the scrolltime is within reasonable bounds,  to guard against the clock being reset
	flScrollTime = min( flScrollTime, flTime + m_HUD_saytext_time->value );

	// make sure the scrolltime is within reasonable bounds,  to guard against the clock being reset
	flScrollTime = min( flScrollTime, flTime + m_HUD_saytext_time->value );

	if(modern)
	{
		while(g_szLineBuffer[0][0] && (flTime<g_lineBirth[0] || flTime-g_lineBirth[0]>=duration))ScrollTextUp();
	}
	else if ( flScrollTime <= flTime )
	{
		if ( *g_szLineBuffer[0] )
		{
			flScrollTime = flTime + m_HUD_saytext_time->value;
			// push the console up
			ScrollTextUp();
		}
		else
		{ // buffer is empty,  just disable drawing of this section
			m_iFlags &= ~HUD_DRAW;
		}
	}
	
	if (g_pMenu)
	{
		const auto area=match_notice::Feed(ScreenWidth,ScreenHeight,g_iUser1!=0);
		const float height=modern?area.height:max(12,gHUD.GetCharHeight());
		line_height=int(std::ceil(modern?max(area.line,hud_unicode::LineHeight(height)):hud_unicode::LineHeight(height)));
		int count=0;for(int i=0;i<MAX_LINES;++i)if(g_szLineBuffer[i][0])++count;
		if(modern)y=int(area.y-count*line_height);
		static const float normal[3]={1,1,1},gain[3]={162/255.f,1,71/255.f},penalty[3]={1,64/255.f,64/255.f};
		const float *defaultColor=modern?normal:g_ColorYellow;
		for (int i=0;i<MAX_LINES;++i)
		{
			if (!g_szLineBuffer[i][0]) continue;
			const int alpha=modern?int(255*match_notice::Fade(flTime-g_lineBirth[i],duration)):255;
			float x=modern?area.x:LINE_START;
			const float *color=defaultColor;
			if(modern)
			{
				const auto visible=hud_text_layout::VisibleText(g_szLineBuffer[i]);
				const float width=min(area.width,hud_unicode::FixedLabelWidth(visible.c_str(),height));
				FillRGBABlend(int(x-5),y-2,int(width+10),line_height,10,14,18,alpha*70/255);
			}
			std::string run;
			const auto flush=[&]()
			{
				if (run.empty()) return;
				const float available=modern?max(0.f,area.x+area.width-x):0;
				if(modern&&available<=0){run.clear();return;}
				if(modern)hud_unicode::FixedLabel(run.c_str(),x+1,float(y)+1,height,{0,0,0,alpha},false,available);
				hud_unicode::FixedLabel(run.c_str(),x,float(y),height,
					{int(color[0]*255),int(color[1]*255),int(color[2]*255),alpha},false,available);
				x+=hud_unicode::FixedLabelWidth(run.c_str(),height);run.clear();
			};
			const char *text=g_szLineBuffer[i];
			bool legacyName=false;
			for (const char *p=text;*p;)
			{
				if (legacyName && p-text>=g_iNameLengths[i])
				{ flush();color=defaultColor;legacyName=false; }
				const unsigned char code=static_cast<unsigned char>(*p);
				if (code=='^' && p[1]>='0' && p[1]<='9')
				{
					static const float colors[10][3]={{0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{0,1,1},{1,0,1},{1,1,1},{1,1,1},{1,1,1}};
					flush();color=modern&&p[1]=='2'?gain:modern&&p[1]=='1'?penalty:colors[p[1]-'0'];p+=2;continue;
				}
				if (code>=1 && code<=4)
				{
					flush();++p;
					if (code==4) color=g_ColorGreen;
					else if ((code==2 || code==3) && g_pflNameColors[i]) color=g_pflNameColors[i];
					else color=defaultColor;
					legacyName=code==2 && g_iNameLengths[i]>0;
					continue;
				}
				const size_t bytes=hud_text_layout::GlyphBytes(p);
				run.append(p,bytes);p+=bytes;
			}
			flush();y+=line_height;
		}
		return 1;
	}

	for (int i = 0; i < MAX_LINES; i++)
	{
		if (!g_szLineBuffer[i][0]) // skip empty string
			continue;

		int current_x = LINE_START;
		const char* text = g_szLineBuffer[i];
		size_t length = strlen(text);

		// default color if not set
		DrawUtils::SetConsoleTextColor(g_ColorYellow[0], g_ColorYellow[1], g_ColorYellow[2]);

		// buffer for accumulating characters of the same color
		char color_buffer[256] = {0};
		size_t buffer_pos = 0;

		for (size_t c = 0; c < length; c++)
		{
			// color code parse
			// '\x01' - normal (yellow); '\0x03' - teamcolor (R GREY B); '\x04' - green
			if (text[c] == '\x01' || text[c] == '\x03' || text[c] == '\x04')
			{
				// if there are characters in the buffer, we draw them with the current color
				if (buffer_pos > 0)
				{
					color_buffer[buffer_pos] = '\x00';
					current_x = DrawUtils::DrawConsoleString(current_x, y, color_buffer);
					buffer_pos = 0;
				}

				// switch to color code
				char color_code = text[c];
				switch (color_code)
				{
					case '\x01': // yellow normal
						DrawUtils::SetConsoleTextColor(g_ColorYellow[0], g_ColorYellow[1], g_ColorYellow[2]);
						break;
					case '\x03': // team color
						if (g_pflNameColors[i])
						{
							DrawUtils::SetConsoleTextColor(g_pflNameColors[i][0], g_pflNameColors[i][1], g_pflNameColors[i][2]);
						}
						break;
					case '\x04': // green
						DrawUtils::SetConsoleTextColor(g_ColorGreen[0], g_ColorGreen[1], g_ColorGreen[2]);
						break;
				}
				continue;
			}

			// add char to buf
			if (buffer_pos < sizeof(color_buffer) - 1)
			{
				color_buffer[buffer_pos++] = text[c];
			}
		}

		// draw the remaining characters
		if (buffer_pos > 0)
		{
			color_buffer[buffer_pos] = '\x00';
			DrawUtils::DrawConsoleString(current_x, y, color_buffer);
		}

		y += line_height;
	}
	
	return 1;
}

struct
{
	const char key[32];
	const char value[64];
	int numArgs;
	bool allowDead;
} sayTextFmt[] =
{
	{
		"#Cstrike_Chat_CT",
		"\x03（反恐精英）%s1：\x01%s2",
		2, true
	},
	{
		"#Cstrike_Chat_T",
		"\x03（恐怖分子）%s1：\x01%s2",
		2, true
	},
	{
		"#Cstrike_Chat_CT_Dead",
		"\x03（死亡·反恐精英）%s1：\x01%s2",
		2, false
	},
	{
		"#Cstrike_Chat_T_Dead",
		"\x03（死亡·恐怖分子）%s1：\x01%s2",
		2, false
	},
	{
		"#Cstrike_Chat_Spec",
		"\x03（观众）%s1：\x03%s2",
		2, false
	},
	{
		"#Cstrike_Chat_All",
		"\x03%s1：\x01%s2",
		2, true
	},
	{
		"#Cstrike_Chat_AllDead",
		"\x03（死亡）%s1：\x01%s2",
		2, false
	},
	{
		"#Cstrike_Chat_AllSpec",
		"\x03（观众）%s1：\x03%s2",
		2, false
	},
	{
		"#Cstrike_Name_Change",
		"\x03* %s1 已改名为 %s2",
		2, true
	},
	{
		"#Cstrike_Chat_T_Loc",
		"\x03（恐怖分子）%s1 @ %s3：\x01%s2",
		3, true
	},
	{
		"#Cstrike_Chat_CT_Loc",
		"\x03（反恐精英）%s1 @ %s3：\x01%s2",
		3, true
	},
	{
		"#Spec_PlayerItem",
		"%s1",
		1, true
	},
};

int CHudSayText :: MsgFunc_SayText( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );
	int client_index, argc, numArgs;		// the client who spoke the message
	CUtlString fmt;
	CUtlString argv[3];
	const char *fmt_tran = nullptr;
	bool allowDead, replaceFirstArgToName;

	client_index = reader.ReadByte();

	// find all arguments
	fmt = reader.ReadString();

	for( argc = 0; argc < 3; argc++ )
	{
		const char *arg = reader.ReadString();
		if( !arg[0] && !reader.Valid() )
			break;

		argv[argc] = arg;
	}

	// see if argv[0] is translatable
	if( fmt[0] == '#' )
	{
		for( int i = 0; i < sizeof( sayTextFmt ) / sizeof( sayTextFmt[0] ); i++ )
		{
			if( !strcmp( fmt, sayTextFmt[i].key ))
			{
				fmt_tran = (char*)sayTextFmt[i].value;
				allowDead = sayTextFmt[i].allowDead;
				numArgs = sayTextFmt[i].numArgs;
				break;
			}
		}
	}

	// no translations
	if( !fmt_tran )
	{
		fmt_tran = fmt.Get();
		numArgs = argc;
		allowDead = true;
	}

	// If text is sent from dead player or spectator
	// don't draw it, until local player isn't specator or dead.
	if( !allowDead && !CL_IsDead() && !g_iUser1 )
	{
		return 1;
	}

	// Resolve missing nickname locally via client_index mapping.
	if( client_index > 0 && client_index <= MAX_PLAYERS && numArgs > 0 && argv[0].Length() == 0 )
	{
		GetPlayerInfo( client_index, &g_PlayerInfoList[client_index] );
		argv[0] = g_PlayerInfoList[client_index].name;
	}

	const char *args[3] = { argv[0].Get(), argv[1].Get(), argv[2].Get() };
	char tmp[1024];
	Localize_Format( tmp, sizeof( tmp ), fmt_tran, args, numArgs );

	SayTextPrint( tmp, strlen( tmp ), client_index );

	return 1;
}

void CHudSayText :: SayTextPrint( const char *pszBuf, int iBufSize, int clientIndex )
{
	if ( m_HUD_saytext_console->value && pszBuf && *pszBuf )
	{
		// Print it straight to the console
		char szConsoleText[ MAX_CHARS_PER_LINE ];
		StripSayTextControlCodes( pszBuf, szConsoleText, sizeof( szConsoleText ) );
		ConsolePrint( szConsoleText );
	}

	// find an empty string slot
	int i;
	for ( i = 0; i < MAX_LINES; i++ )
	{
		if ( ! *g_szLineBuffer[i] )
			break;
	}
	if ( i == MAX_LINES )
	{
		// force scroll buffer up
		ScrollTextUp();
		for(i=0;i<MAX_LINES && g_szLineBuffer[i][0];++i) {}
	}

	g_iNameLengths[i] = 0;
	g_lineBirth[i]=gHUD.m_flTime;
	g_pflNameColors[i] = NULL;

#if 1
	// if it's a say message, search for the players name in the string
	if ( clientIndex > 0 && clientIndex <= MAX_PLAYERS )
	{
		GetPlayerInfo( clientIndex, &g_PlayerInfoList[clientIndex] );
		const char *pName = g_PlayerInfoList[clientIndex].name;
		g_pflNameColors[i] = GetClientColor( clientIndex );
		if(match_ui::Enabled())
		{
			static const float ct[3]={162/255.f,198/255.f,1},t[3]={1,223/255.f,147/255.f};
			const int team=g_PlayerExtraInfo[clientIndex].teamnumber;
			if(team==TEAM_CT)g_pflNameColors[i]=const_cast<float *>(ct);
			else if(team==TEAM_TERRORIST)g_pflNameColors[i]=const_cast<float *>(t);
		}

		if ( pName )
		{
			const char *nameInString = strstr( pszBuf, pName );

			if ( nameInString )
			{
				g_iNameLengths[i] = strlen( pName ) + (nameInString - pszBuf);
			}
		}
	}
#endif


	hud_text_layout::Copy(g_szLineBuffer[i],sizeof(g_szLineBuffer[i]),pszBuf);

	// make sure the text fits in one line
	EnsureTextFitsInOneLineAndWrapIfHaveTo( i );

	// Set scroll time
	if ( i == 0 )
	{
		flScrollTime = gHUD.m_flTime + m_HUD_saytext_time->value;
	}

	m_iFlags |= HUD_DRAW;
	PlaySound( "misc/talk.wav", 1 );

	if( !g_iUser1 )
	{
		Y_START = ScreenHeight - 60;
	}
	else
	{
		Y_START = ScreenHeight * 4 / 5;
	}
	Y_START -= (line_height * (MAX_LINES+1));

}

void CHudSayText :: EnsureTextFitsInOneLineAndWrapIfHaveTo( int line )
{
	if (g_pMenu)
	{
		const bool modern=match_ui::Enabled();
		const auto area=match_notice::Feed(ScreenWidth,ScreenHeight,g_iUser1!=0);
		const float height=modern?area.height:max(12,gHUD.GetCharHeight());
		line_height=int(std::ceil(hud_unicode::LineHeight(height)));
		const float available=(modern?area.width:float(MAX_LINE_WIDTH-LINE_START))-hud_unicode::FixedLabelWidth(" ",height);
		auto lines=hud_unicode::WrapLines(g_szLineBuffer[line],height,available,true);
		while(!lines.empty() && hud_text_layout::VisibleText(lines.back().c_str()).find_first_not_of(" \t\r\n")==std::string::npos)
			lines.pop_back();
		if(lines.size()>MAX_LINES){lines.resize(MAX_LINES);lines.back()+="...";}
		float *nameColor=g_pflNameColors[line];
		const int nameLength=g_iNameLengths[line];
		const float birth=g_lineBirth[line];
		// Reserve the entire wrapped message before inserting any continuation.
		g_szLineBuffer[line][0]=0;
		while(line+int(lines.size())>MAX_LINES)line=max(0,line-ScrollTextUp());
		std::string activeColor(1,char(1));
		for (size_t part=0;part<lines.size();++part)
		{
			const int target=line+int(part);
			std::string row=part?std::string(" ")+activeColor+lines[part]:lines[part];
			if (row.empty()) row=" ";
			hud_text_layout::Copy(g_szLineBuffer[target],sizeof(g_szLineBuffer[target]),row.c_str());
			g_pflNameColors[target]=nameColor;
			g_iNameLengths[target]=part?0:nameLength;
			g_lineBirth[target]=birth;
			for (const char *p=lines[part].c_str();*p;++p)
			{
				const unsigned char ch=static_cast<unsigned char>(*p);
				if (ch>=1 && ch<=4) activeColor.assign(1,char(ch));
				else if (*p=='^' && p[1]>='0' && p[1]<='9') { activeColor.assign(p,2);++p; }
			}
		}
		return;
	}
	int line_width = 0;
	DrawUtils::ConsoleStringSize(g_szLineBuffer[line], &line_width, &line_height );

	if ( (line_width + LINE_START) > MAX_LINE_WIDTH )
	{ // string is too long to fit on line
		// scan the string until we find what word is too long,  and wrap the end of the sentence after the word
		int length = LINE_START;
		int tmp_len = 0;
		char *last_break = NULL;
		for ( char *x = g_szLineBuffer[line]; *x != 0; x++ )
		{
			// check for a color change, if so skip past it
			if ( x[0] == '/' && x[1] == '(' )
			{
				x += 2;
				// skip forward until past mode specifier
				while ( *x != 0 && *x != ')' )
					x++;

				if ( *x != 0 )
					x++;

				if ( *x == 0 )
					break;
			}

			char buf[2];
			buf[1] = 0;

			if ( *x == ' ' && x != g_szLineBuffer[line] )  // store each line break,  except for the very first character
				last_break = x;

			buf[0] = *x;  // get the length of the current character
			DrawUtils::ConsoleStringSize( buf, &tmp_len, &line_height );
			length += tmp_len;

			if ( length > MAX_LINE_WIDTH )
			{  // needs to be broken up
				if ( !last_break )
					last_break = x-1;

				x = last_break;

				// find an empty string slot
				int j;
				do 
				{
					for ( j = 0; j < MAX_LINES; j++ )
					{
						if ( ! *g_szLineBuffer[j] )
							break;
					}
					if ( j == MAX_LINES )
					{
						// need to make more room to display text, scroll stuff up then fix the pointers
						int linesmoved = ScrollTextUp();
						line -= linesmoved;
						last_break = last_break - (sizeof(g_szLineBuffer[0]) * linesmoved);
					}
				}
				while ( j == MAX_LINES );

				// copy remaining string into next buffer,  making sure it starts with a space character
				if ( (char)*last_break == (char)' ' )
				{
					strlcat( g_szLineBuffer[j], last_break, sizeof( g_szLineBuffer[j] ) );
				}
				else
				{
					strlcat( g_szLineBuffer[j], " ", sizeof( g_szLineBuffer[j] ) );
					strlcat( g_szLineBuffer[j], last_break, sizeof( g_szLineBuffer[j] ) );
				}

				*last_break = 0; // cut off the last string

				EnsureTextFitsInOneLineAndWrapIfHaveTo( j );
				break;
			}
		}
	}
}
