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
// text_message.cpp
//
// implementation of CHudTextMessage class
//
// this class routes messages through titles.txt for localisation
//

#include "hud.h"
#include "cl_util.h"
#include <string.h>
#include <stdio.h>
#include "parsemsg.h"
#include "vgui_parser.h"
#include "ctype.h"
#include "draw_util.h"
#include "hud/message_zh.h"
#include "hud/unicode_labels.h"
#include "hud/match_notice_layout.h"
#include "hud/presentation_contract.h"
#include "hostage_hint.h"
#include <string>
#include <vector>

static bool s_centerHostageHint = false;
namespace
{
match_notice::BannerQueue phaseQueue;
match_notice::ActionNotice purchaseNotice;
float phaseAlpha=0;
const char *centerDrawSource="none";
float centerTextAlpha=0;
bool PhaseBusy(float time)
{
	return phaseQueue.count>0 || phaseQueue.current.priority>=4
		|| (!phaseQueue.current.text.empty() && time<phaseQueue.current.end);
}
int MatchPhase(const char *text)
{
	static const char *notices[]={u8"\u534a\u573a\u4f11\u606f\uff0c\u5373\u5c06\u4ea4\u6362\u9635\u8425",
		u8"\u4e0a\u534a\u573a\u7ed3\u675f\uff0c\u4ea4\u6362\u9635\u8425",
		u8"\u8d5b\u70b9\uff1a\u672c\u56de\u5408\u53ef\u51b3\u5b9a\u80dc\u8d1f",
		u8"\u6bd4\u8d5b\u7ed3\u675f",u8"\u6bd4\u8d5b\u7ed3\u675f\uff1a\u5e73\u5c40"};
	for(int i=0;i<5;++i)if(!strcmp(text,notices[i]))return i>=3?4:i==2?2:3;
	return 0;
}
bool FeedNotification(const char *token)
{
	return !strcmp(token,"#Game_join_ct")||!strcmp(token,"#Game_join_terrorist")
		||!strcmp(token,"#Game_join_ct_auto")||!strcmp(token,"#Game_join_terrorist_auto")
		||!strcmp(token,"#Game_connected")||!strcmp(token,"#Game_disconnected");
}
}

void OC2_MatchNoticeRound(){phaseQueue.NewRound(gHUD.m_flTime);purchaseNotice.Clear();}
void CHudTextMessage::ClearPurchaseNotice(){purchaseNotice.Clear();}
void CHudTextMessage::ShowPurchaseExpired()
{
	if(g_iUser1 || gHUD.m_Health.m_iHealth<=0)return;
	const char *text=u8"购买时间已结束，请等待下一回合";
	if(!match_ui::Enabled()){ShowCenter(text);return;}
	phaseQueue.Update(gHUD.m_flTime);
	purchaseNotice.Push(text,gHUD.m_flTime,PhaseBusy(gHUD.m_flTime));
}

int CHudTextMessage::Init(void)
{
	HOOK_MESSAGE( gHUD.m_TextMessage, TextMsg );
	HOOK_COMMAND(gHUD.m_TextMessage,"hud_presentation_dump",PresentationDump);

	gHUD.AddHudElem( this );
	InitHUDData();

	return 1;
}

void CHudTextMessage::InitHUDData()
{
	m_centerText[0]=m_lastToken[0]=0;
	m_centerStart=m_centerEnd=0;
	m_received=m_suppressed=m_drawn=0;
	m_lastDestination=0;
	m_centerDrawTime=-1;m_centerDrawY=m_centerDrawExpansion=0;
	s_centerHostageHint=false;
	phaseQueue.Clear();purchaseNotice.Clear();phaseAlpha=0;
	centerDrawSource="none";centerTextAlpha=0;
	m_iFlags=HUD_DRAW | HUD_INTERMISSION;
}
void CHudTextMessage::Reset()
{
	// ResetHUD can be a fullupdate; the center text still has its own clock.
}
void CHudTextMessage::ShowCenter(const char *text)
{
	if(text&&match_ui::Enabled()&&MatchPhase(text))
	{
		phaseQueue.Push(text,MatchPhase(text),gHUD.m_flTime);
		return;
	}
	s_centerHostageHint=false;
	purchaseNotice.Clear();
	hud_text_layout::Copy(m_centerText,sizeof(m_centerText),text);
	m_centerStart=gHUD.m_flTime;
	const float hold=gEngfuncs.pfnGetCvarFloat("scr_centertime");
	m_centerEnd=m_centerStart+max(0.0f,hold);
}
int CHudTextMessage::Draw(float time)
{
	phaseQueue.Update(time);phaseAlpha=phaseQueue.Alpha(time);
	const bool phaseBusy=match_ui::Enabled()&&PhaseBusy(time);
	if(phaseBusy)purchaseNotice.Clear();
	const char *text=m_centerText;
	float start=m_centerStart,end=m_centerEnd,interruptAlpha=1;
	bool hostage=s_centerHostageHint;
	centerDrawSource="center";centerTextAlpha=0;
	if(phaseBusy)
	{
		const auto &notice=phaseQueue.current;
		text=notice.text.c_str();start=notice.start;end=notice.end;
		if(notice.cutStart>=0)
		{
			// Preserve the same animation while a higher-priority notice takes over.
			end=start+4.8f;interruptAlpha=phaseAlpha;
		}
		hostage=false;centerDrawSource="phase";
	}
	else if(!purchaseNotice.current.text.empty()&&time<purchaseNotice.current.end)
	{
		text=purchaseNotice.current.text.c_str();start=purchaseNotice.current.start;end=purchaseNotice.current.end;
		hostage=false;centerDrawSource="purchase";
	}
	if (!text[0] || time<start || time>=end || (gHUD.m_iHideHUDDisplay&HIDEHUD_ALL)) return 1;
	if (!g_pMenu) return 1;
	// Burst-fire, purchase and match notices share the original center animation.
	const float height=hostage ? max(20.0f,ScreenHeight*.024f) : float(max(12,gHUD.GetCharHeight()));
	const float available=ScreenWidth*.9f, spacing=hostage ? 2.0f : 0.0f;
	const auto lines=hud_text_layout::Wrap(text,available,[height,spacing](const char *line){
		return hud_unicode::SpacedLabelWidth(line,height,spacing);
	});
	const float lineHeight=hud_unicode::LineHeight(height);
	float y=hud_presentation::CenterY(float(ScreenHeight));
	const float expansion=hud_presentation::CenterExpansion(time-start,end-time)*interruptAlpha;
	float widest=0;
	for (const auto &line:lines) widest=max(widest,hud_unicode::SpacedLabelWidth(line.c_str(),height,spacing));
	const float padding=20.0f/hud_unicode::LabelScale();
	// Preserve the anchor unless a translated multiline block reaches the
	// lower screen edge, as HudText's existing YPosition already does.
	y=max(padding,min(y,float(ScreenHeight)-float(lines.size())*lineHeight-padding));
	const int panelWidth=int((widest+2*padding)*expansion);
	const int panelX=int((ScreenWidth-panelWidth)*.5f),panelY=int(y-padding);
	const int panelHeight=int(lines.size()*lineHeight+2*padding);
	if (panelWidth>0)
	{
		// APK's flat fallback: black .6, white .5 edges, twenty-pixel padding.
		// The PC renderer has no center-text blur capture interface.
		FillRGBABlend(panelX,panelY,panelWidth,panelHeight,0,0,0,int(255*.6f*expansion));
		const int edge=min(panelWidth,max(1,int(2.0f/hud_unicode::LabelScale())));
		FillRGBABlend(panelX,panelY,edge,panelHeight,255,255,255,int(255*.5f*expansion));
		FillRGBABlend(panelX+panelWidth-edge,panelY,edge,panelHeight,255,255,255,int(255*.5f*expansion));
	}
	m_centerDrawTime=time;m_centerDrawY=y;m_centerDrawExpansion=expansion;
	// The backdrop retracts during its final 0.3 seconds. Remove the text
	// first so it never hangs outside the shrinking panel. For unusually
	// short center times, retain text for at least the first half.
	const float textDuration=end-start-min(.3f,(end-start)*.5f);
	centerTextAlpha=match_notice::Fade(time-start,textDuration,.2f,.2f)*interruptAlpha;
	if (centerTextAlpha>0)
		for (const auto &line:lines)
		{
			const float width=hud_unicode::SpacedLabelWidth(line.c_str(),height,spacing);
			const match_ui::Color color={255,255,255,int(255*centerTextAlpha)};
			if(hostage) hud_unicode::SpacedLabel(line.c_str(),(ScreenWidth-width)*.5f,y,height,color,spacing);
			else hud_unicode::FixedLabel(line.c_str(),(ScreenWidth-width)*.5f,y,height,color);
			y+=lineHeight;
		}
	++m_drawn;
	return 1;
}
void CHudTextMessage::UserCmd_PresentationDump()
{
	gEngfuncs.Con_Printf("hudpresentation_purchase: start=%.3f end=%.3f alpha=%.4f text=%s\n",purchaseNotice.current.start,purchaseNotice.current.end,purchaseNotice.Alpha(gHUD.m_flTime),purchaseNotice.current.text.c_str());
	gEngfuncs.Con_Printf("hudpresentation_phase: start=%.3f end=%.3f alpha=%.4f pending=%d text=%s\n",phaseQueue.current.start,phaseQueue.current.end,phaseAlpha,phaseQueue.count,phaseQueue.current.text.c_str());
	const auto rect=hud_presentation::BombRect(ScreenWidth*.5f,5,1,hud_presentation::Bomb(gHUD.m_Timer.BombState()));
	gEngfuncs.Con_Printf("hudpresentation: time=%.3f bomb=%d alpha=%d apk_rect=%.1f,%.1f,%.1f,%.1f received=%u suppressed=%u center_draws=%u token=%s dest=%d center_start=%.3f center_end=%.3f center=%s\n",
		gHUD.m_flTime,gHUD.m_Timer.BombState(),hud_presentation::BombAlpha(gHUD.m_flTime),rect.x,rect.y,rect.width,rect.height,
		m_received,m_suppressed,m_drawn,m_lastToken,m_lastDestination,m_centerStart,m_centerEnd,m_centerText);
	gHUD.m_Timer.DumpPresentation();
	gEngfuncs.Con_Printf("hudpresentation_center: draw_time=%.3f y=%.3f expansion=%.4f height=%d hud_scale=%.4f source=%s text_alpha=%.4f\n",
		m_centerDrawTime,m_centerDrawY,m_centerDrawExpansion,ScreenHeight,gHUD.m_flScale,centerDrawSource,centerTextAlpha);
}

// Searches through the string for any msg names (indicated by a '#')
// any found are looked up in titles.txt and the new message substituted
// the new value is pushed into dst_buffer
char *CHudTextMessage::LocaliseTextString( const char *msg, char *dst_buffer, int buffer_size )
{
	if (!dst_buffer || buffer_size<=0) return dst_buffer;
	if (!msg) { dst_buffer[0]=0;return dst_buffer; }
	if (const char *translated=hud_message_zh::Find(msg))
	{ hud_text_layout::Copy(dst_buffer,size_t(buffer_size),translated);return dst_buffer; }
	std::string result;
	for (const char *src=msg;*src;)
	{
		if (*src=='#')
		{
			const char *end=src+1;
			while ((*end>='A' && *end<='Z') || (*end>='a' && *end<='z') || (*end>='0' && *end<='9') || *end=='_') ++end;
			const std::string token(src,end);
			const char *replacement=hud_message_zh::Token(token.c_str());
			if (!replacement && token.size()>1)
			{
				const char *key=token.c_str()+1;
				if (const auto *title=TextMessageGet(key))
					replacement=title->pMessage && title->pMessage[0]=='#'?Localize(title->pMessage+1):title->pMessage;
				else
				{
					const char *localized=Localize(key);
					if (localized && strcmp(localized,key)) replacement=localized;
				}
			}
			result+=replacement?replacement:token;
			src=end;continue;
		}
		const size_t bytes=hud_text_layout::GlyphBytes(src);
		result.append(src,bytes);src+=bytes;
	}
	hud_text_layout::Copy(dst_buffer,size_t(buffer_size),result.c_str());
	return dst_buffer;
}

// As above, but with a local static buffer
char *CHudTextMessage::BufferedLocaliseTextString( const char *msg )
{
	static char dst_buffer[1024];
	LocaliseTextString( msg, dst_buffer, 1024 );
	return dst_buffer;
}

// Simplified version of LocaliseTextString;  assumes string is only one word
char *CHudTextMessage::LookupString( char *msg, int *msg_dest )
{
	if ( !msg )
		return (char*)"";
	// Retain title destination overrides before applying the fixed Chinese text.
	if (const char *translated=hud_message_zh::Token(msg))
	{
		const char *key=msg[0]=='#'?msg+1:msg;
		if (msg_dest) if (const auto *title=TextMessageGet(key))
			if (title->effect<0) *msg_dest=-title->effect;
		return const_cast<char*>(translated);
	}

	// '#' character indicates this is a reference to a string in titles.txt, and not the string itself
	if ( msg[0] == '#' ) 
	{
		// this is a message name, so look up the real message
		client_textmessage_t *clmsg = TextMessageGet( msg+1 );

		if ( !clmsg || !(clmsg->pMessage) )
			return (char*)msg; // lookup failed, so return the original string
				
		if ( msg_dest )
		{
			// check to see if titles.txt info overrides msg destination
			// if clmsg->effect is less than 0, then clmsg->effect holds -1 * message_destination
			if ( clmsg->effect < 0 )  // 
				*msg_dest = -clmsg->effect;
		}

		if( clmsg->pMessage[0] == '#')
			return (char *)Localize( clmsg->pMessage + 1);

		return (char*)clmsg->pMessage;
	}
	else
	{  // nothing special about this message, so just return the same string
		return (char*)msg;
	}
}

void StripEndNewlineFromString( char *str )
{
	if (!str || !*str) return;
	int s = strlen( str ) - 1;
	if ( str[s] == '\n' || str[s] == '\r' )
		str[s] = 0;
}

// converts all '\r' characters to '\n', so that the engine can deal with the properly
// returns a pointer to str
char* ConvertCRtoNL( char *str )
{
	char *write=str;
	for (char *read=str;*read;++read)
	{
		if (*read=='\r')
		{
			*write++='\n';
			if (read[1]=='\n') ++read;
		}
		else *write++=*read;
	}
	*write=0;
	return str;
}

// Message handler for text messages
// displays a string, looking them up from the titles.txt file, which can be localised
// parameters:
//   byte:   message direction  ( HUD_PRINTCONSOLE, HUD_PRINTNOTIFY, HUD_PRINTCENTER, HUD_PRINTTALK )
//   string: message
// optional parameters:
//   string: message parameter 1
//   string: message parameter 2
//   string: message parameter 3
//   string: message parameter 4
// any string that starts with the character '#' is a message name, and is used to look up the real message in titles.txt
// the next (optional) one to four strings are parameters for that string (which can also be message names if they begin with '#')
#define MAX_TEXTMSG_STRING 2048
int CHudTextMessage::MsgFunc_TextMsg( const char *pszName, int iSize, void *pbuf )
{
	BufferReader reader( pszName, pbuf, iSize );

	int msg_dest = reader.ReadByte();
	int clientIdx = -1;

	static char szBuf[6][MAX_TEXTMSG_STRING];
	char token[MAX_TEXTMSG_STRING];
	snprintf(token,sizeof(token),"%s",reader.ReadString());
	gHUD.m_Timer.NoteTextMessage(token);
	snprintf(m_lastToken,sizeof(m_lastToken),"%s",token);
	++m_received;
	char *msg_text = LookupString( token, &msg_dest );
	if (const char *translated=hud_message_zh::Find(token)) msg_text=const_cast<char*>(translated);
	m_lastDestination=msg_dest;
	if (!msg_text || !*msg_text) { ++m_suppressed;return 1; }
	hud_text_layout::Copy(szBuf[0],MAX_TEXTMSG_STRING,msg_text);
	msg_text=szBuf[0];

	// keep reading strings and using C format strings for substituting the strings into the localised text string
	for( int i = 1; i <= 4; i++ )
	{
		char *raw = reader.ReadString();
		
		// Don't replace the second format argument (player name)
		char *str = (i == 2) ? raw : LookupString( raw );
		const char *localized = (i == 2) ? str : Localize( str );

		hud_text_layout::Copy(szBuf[i],MAX_TEXTMSG_STRING,localized);

		// these strings are meant for substitution into the main strings, so cull the automatic end newlines
		StripEndNewlineFromString( szBuf[i] );
	}

	char *psz = szBuf[5];

	const char *args[4] = { szBuf[1], szBuf[2], szBuf[3], szBuf[4] };

	switch ( msg_dest )
	{
	case HUD_PRINTCENTER:
	{
		Localize_Format( psz, MAX_TEXTMSG_STRING, msg_text, args, 4 );
		const bool hostageHint = !strcmp(token,"Hold E to rescue the hostage");
		if(hostageHint)
		{
			const char *binding=gEngfuncs.Key_LookupBinding ? gEngfuncs.Key_LookupBinding("use") : nullptr;
			if(!binding && gEngfuncs.Key_LookupBinding) binding=gEngfuncs.Key_LookupBinding("+use");
			hostage_hint::Format(psz,MAX_TEXTMSG_STRING,binding);
		}

		ConvertCRtoNL( psz );
		if(!strcmp(token,"#Cant_buy")){ShowPurchaseExpired();break;}

		ShowCenter( psz );
		s_centerHostageHint=hostageHint;
		break;
	}
	case HUD_PRINTNOTIFY:
		if(match_ui::Enabled()&&FeedNotification(token))
		{
			Localize_Format(psz,MAX_TEXTMSG_STRING,msg_text,args,4);
			gHUD.m_SayText.SayTextPrint(ConvertCRtoNL(psz),MAX_TEXTMSG_STRING);
			break;
		}
		psz[0] = 1;  // mark this message to go into the notify buffer
		Localize_Format( psz + 1, MAX_TEXTMSG_STRING - 1, msg_text, args, 4 );
		ConsolePrint( ConvertCRtoNL( psz ) );
		break;

	case HUD_PRINTTALK:
		psz[0] = 2; // mark, so SayTextPrint will color it
		Localize_Format( psz + 1, MAX_TEXTMSG_STRING - 1, msg_text, args, 4 );
		gHUD.m_SayText.SayTextPrint( ConvertCRtoNL( psz ), 128 );
		break;

	case HUD_PRINTCONSOLE:
		Localize_Format( psz, MAX_TEXTMSG_STRING, msg_text, args, 4 );
		ConsolePrint( ConvertCRtoNL( psz ) );
		break;

	case HUD_PRINTRADIO:
		psz[0] = 2;
		// szBuf[1] is server-controlled and used as the format here.
		// Localize_Format only honors %s<n>, so a hostile format is inert.
		Localize_Format( psz + 1, MAX_TEXTMSG_STRING - 1, szBuf[1], &args[1], 3 );

		clientIdx = atoi( szBuf[0] );
		gHUD.m_SayText.SayTextPrint( ConvertCRtoNL( psz ), 128, clientIdx );
		break;
	}

	return 1;
}
