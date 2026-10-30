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
// death notice
//
#include "hud.h"
#include "oc2_hostage.h"
#include "cl_util.h"
#include "parsemsg.h"

#include <string.h>
#include <stdio.h>
#include "draw_util.h"
#include "strl.h"
#include "kill_feedback.h"
#include "accessory_client.h"
#include "match_ui.h"
#include "weapon_hud_layout.h"
#include "hud_readability.h"
#include "oc2_ragdoll.h"
#include "../common/oc2_knife_icons.h"
#include <cmath>

float color[3];

struct DeathNoticeItem {
	char szKiller[MAX_PLAYER_NAME_LENGTH*2];
	char szVictim[MAX_PLAYER_NAME_LENGTH*2];
	int iId;	// the index number of the associated sprite
	bool bSuicide;
	bool bTeamKill;
	bool bNonPlayerKill;
	float flDisplayTime;
	float *KillerColor;
	float *VictimColor;
	int iHeadShotId;
	int killerTeam, victimTeam;
	bool localKiller, localVictim;
	int killerIndex, victimIndex;
	char weaponResource[64]; // fixed directory plus an ASCII-validated resource name
};

#define MAX_DEATHNOTICES	15 // CSGO csgo_huddeathnotice.cpp:43, bounded oldest-first queue
static int DEATHNOTICE_DISPLAY_TIME = 6;

#define DEATHNOTICE_TOP		32

DeathNoticeItem rgDeathNoticeList[ MAX_DEATHNOTICES + 1 ];

namespace
{
kill_feedback::State s_KillFeedback;
float s_LastThinkTime = -1.0f;

int PlayerTeam(int slot)
{
	if (slot < 1 || slot > MAX_PLAYERS) return TEAM_UNASSIGNED;
	// TeamInfo is authoritative; optional userinfo may be absent or older.
	if (g_PlayerExtraInfo[slot].teamname[0]) return g_PlayerExtraInfo[slot].teamnumber;
	return g_PlayerInfoList[slot].team >= 0 ? g_PlayerInfoList[slot].team : TEAM_UNASSIGNED;
}

void ExpireNotices(float time)
{
	for (int i = 0; i < MAX_DEATHNOTICES && rgDeathNoticeList[i].iId; ++i)
	{
		if (time >= rgDeathNoticeList[i].flDisplayTime)
		{
			memmove(&rgDeathNoticeList[i], &rgDeathNoticeList[i+1], sizeof(DeathNoticeItem) * (MAX_DEATHNOTICES-i));
			--i;
		}
	}
}

match_ui::Color TeamColor(int team)
{
	if (team == TEAM_CT) return match_ui::Color(115,161,209,255);
	if (team == TEAM_TERRORIST) return match_ui::Color(237,199,97,255);
	return match_ui::Color(210,210,210,255);
}

match_ui::Color NoticeNameColor(int team)
{
	// The notice uses red/blue names; the bottom card keeps its team theme.
	if (team == TEAM_CT) return match_ui::Color(110,160,192,255);
	if (team == TEAM_TERRORIST) return match_ui::Color(168,43,48,255);
	return match_ui::Color(190,190,190,255);
}

void NoticeBackground(float x, float y, float width, float height, const DeathNoticeItem& item, int alpha)
{
	// The shipped killbg/deathbg pictures contain blue/orange RGB, rather
	// than a neutral mask. Use the reference's black translucent panel.
	// CSGO's local involvement remains visible through the darker panel;
	// no bright team stripe competes with the white weapon silhouette.
	const int opacity = item.localKiller || item.localVictim ? 235 : 215;
	FillRGBABlend((int)x,(int)y,(int)width,(int)height,0,0,0,opacity*alpha/255);
}

bool NoticeIconFlips(const char *path)
{
	// APK guns and knives point left; the notice points toward the victim.
	// Grenades, bomb/world symbols and headshots keep their orientation.
	static const char *directional[] = {"ak47","aug","awp","deagle","elite","famas","fiveseven",
		"g3sg1","galil","glock18","m249","m3","m4a1","mac10","mp5navy","mp7",
		"p228","p90","scout","sg550","sg552","tmp","ump45","usp","xm1014","zeus",
		"knife","knife_t"};
	for (const char *name : directional)
	{
		char candidate[64]; snprintf(candidate,sizeof(candidate),"gfx/vgui/death/d_%s.tga",name);
		if (!strcmp(path,candidate)) return true;
	}
	for (const auto &knife : oc2_knife_icons::entries)
	{
		char candidate[64]; snprintf(candidate,sizeof(candidate),"gfx/vgui/death/d_%s.tga",knife.deathToken);
		if (!strcmp(path,candidate)) return true;
	}
	return false;
}

float NoticeIconWidth(const char *path, int sprite, float scale)
{
	const auto bounds = match_ui::SilhouetteBounds(path);
	float aspect = 1.0f;
	if (bounds.valid) aspect = float(bounds.right-bounds.left)/(bounds.bottom-bounds.top);
	else if (sprite >= 0 && sprite < gHUD.SpriteCount())
	{
		const wrect_t &rect = gHUD.GetSpriteRect(sprite);
		if (rect.bottom > rect.top) aspect = float(rect.right-rect.left)/(rect.bottom-rect.top);
	}
	return max(26.0f,min(90.0f,33.0f*aspect))*scale;
}

void DeathIconDump()
{
	static const char *names[] = {"ak47","aug","awp","deagle","elite","famas","fiveseven",
		"g3sg1","galil","glock18","m249","m3","m4a1","mac10","mp7","p228","p90",
		"scout","sg550","sg552","tmp","ump45","usp","xm1014","zeus","knife","knife_t",
		"hegrenade","flashbang","smokegrenade","molotov","incendiary","c4","worldspawn"};
	for (const char *name : names)
	{
		char path[64]; snprintf(path,sizeof(path),"gfx/vgui/death/d_%s.tga",name);
		const auto b = match_ui::SilhouetteBounds(path);
		gEngfuncs.Con_Printf("deathicon: name=%s valid=%d source=%dx%d contour=%d,%d,%d,%d width=%.2f height=33 flip=%d\n",
			name,int(b.valid),b.width,b.height,b.left,b.top,b.right,b.bottom,NoticeIconWidth(path,-1,1),int(NoticeIconFlips(path)));
	}
	for (const auto &knife : oc2_knife_icons::entries)
	{
		char path[64]; snprintf(path,sizeof(path),"gfx/vgui/death/d_%s.tga",knife.deathToken);
		const auto b = match_ui::SilhouetteBounds(path);
		gEngfuncs.Con_Printf("deathknife: definition=%d name=\"%s\" token=%s path=%s valid=%d source=%dx%d contour=%d,%d,%d,%d width=%.2f height=33 flip=%d\n",
			knife.definition,knife.name,knife.deathToken,path,int(b.valid),b.width,b.height,b.left,b.top,b.right,b.bottom,NoticeIconWidth(path,-1,1),int(NoticeIconFlips(path)));
	}
}

void DeathNoticeDump()
{
	for (int i=0;i<MAX_DEATHNOTICES && rgDeathNoticeList[i].iId;++i)
	{
		const auto &item=rgDeathNoticeList[i];
		gEngfuncs.Con_Printf("deathnotice: index=%d killer=%d victim=%d local_killer=%d local_victim=%d path=%s sprite=%d expires=%.3f\n",
			i,item.killerIndex,item.victimIndex,int(item.localKiller),int(item.localVictim),item.weaponResource,item.iId,item.flDisplayTime);
	}
}

void DrawModernNotice(const DeathNoticeItem& item, int row, int skull, int headshot, float time)
{
	// Fit the visible contour, not the common 89x32 canvas. In particular,
	// the bottle/canister artwork occupies only its rightmost 11-20 pixels.
	const float scale = hud_readability::Scale(match_ui::Scale());
	const int id = item.iId >= 0 ? item.iId : skull;
	const float weaponW = NoticeIconWidth(item.weaponResource,id,scale), weaponH = 33.0f*scale;
	const float iconW = 33.0f*scale, padding = 6.0f*scale;
	const float textH = 28.0f*scale;
	const float maxName = min(240.0f*scale, ScreenWidth*0.24f);
	const float killerW = item.bSuicide ? 0.0f : min(maxName,match_ui::TextWidth(item.szKiller,textH));
	const float victimW = item.bNonPlayerKill ? 0.0f : min(maxName,match_ui::TextWidth(item.szVictim,textH));
	const float width = killerW+victimW+weaponW+padding*2
		+ (item.bSuicide ? 0.0f : padding) + (item.bNonPlayerKill ? 0.0f : padding)
		+ (item.iHeadShotId ? iconW+padding : 0.0f);
	const float x = ScreenWidth-32.0f*scale-width;
	const float y = std::floor(ScreenHeight/480.0f*32.0f+0.5f)+50.0f*scale+row*(70.0f/1.5f)*scale;
	// The queue remains bounded at 15; tiny viewports draw only complete rows.
	if (y < 0 || y+weaponH+8.0f*scale > ScreenHeight-20.0f*scale) return;
	// CSGO starts fade after the lifetime and deletes one second later
	// (csgo_huddeathnotice.cpp:182-188,555-559). Legacy drawing keeps its
	// original expiry; modern pictures, text and background share this opacity.
	const int alpha = (int)(255.0f*kill_feedback::NoticeOpacity(time,item.flDisplayTime));
	NoticeBackground(x,y,width,weaponH+8.0f*scale,item,alpha);
	match_ui::Color killerColor = NoticeNameColor(item.killerTeam), victimColor = NoticeNameColor(item.victimTeam);
	killerColor.a = victimColor.a = alpha;
	const match_ui::Color iconColor(255,255,255,alpha);
	float cursor = x+padding;
	if (!item.bSuicide)
	{
		match_ui::Text(item.szKiller,cursor,y+(weaponH-textH)*0.5f+4.0f*scale,textH,killerColor,false,maxName);
		cursor += killerW+padding;
	}
	if (!match_ui::Silhouette(item.weaponResource,cursor,y+4.0f*scale,weaponW,weaponH,iconColor,NoticeIconFlips(item.weaponResource)))
	{
		// Native GoldSrc sprites already face right. Contain the fallback too.
		if (id >= 0 && id < gHUD.SpriteCount())
		{
			const wrect_t &rect = gHUD.GetSpriteRect(id);
			const float fit = min(weaponW/max(1,rect.right-rect.left),weaponH/max(1,rect.bottom-rect.top));
			const float w = (rect.right-rect.left)*fit, h = (rect.bottom-rect.top)*fit;
			match_ui::Sprite(id,cursor+(weaponW-w)*0.5f,y+4*scale+(weaponH-h)*0.5f,w,h,iconColor,false);
		}
		else match_ui::Text("X",cursor,y+4.0f*scale,textH,iconColor);
	}
	cursor += weaponW;
	if (item.iHeadShotId)
	{
		cursor += padding;
		if (!match_ui::Silhouette("gfx/vgui/death/headshot.tga",cursor,y+4.0f*scale,iconW,weaponH,iconColor)
			&& headshot >= 0) match_ui::Sprite(headshot,cursor,y+4.0f*scale,iconW,weaponH,iconColor);
		cursor += iconW;
	}
	if (!item.bNonPlayerKill)
		match_ui::Text(item.szVictim,cursor+padding,y+(weaponH-textH)*0.5f+4.0f*scale,textH,victimColor,false,maxName);
}

void DrawKillCard(float time)
{
	if (!match_ui::Enabled() || !s_KillFeedback.card_visible || s_KillFeedback.round_kills < 1 ||
		g_iUser1 || gHUD.m_Health.m_iHealth <= 0 || !(gHUD.m_iWeaponBits & (1<<WEAPON_SUIT)) ||
		(gHUD.m_iHideHUDDisplay & (HIDEHUD_HEALTH|HIDEHUD_WEAPONS|HIDEHUD_ALL))) return;
	const int frame = kill_feedback::CardFrame(s_KillFeedback,time);
	if (frame < 0) return;
	// CS2-style artwork/OC2 geometry, driven by CS:GO-compatible confirmed
	// round kills. Group 6 is an A-card resource and is deliberately unused;
	// beyond five kills, retain group 5 and report the true count separately.
	const int group = min(s_KillFeedback.round_kills,5);
	char path[80];
	snprintf(path,sizeof(path),"gfx/vgui/killcard/%d/%04d.tga",group,frame+1);
	const float scale = match_ui::Scale(), center = ScreenWidth*0.5f;
	const int texture = match_ui::Texture(path);
	const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
	const int team = local ? PlayerTeam(local->index) : TEAM_UNASSIGNED;
	match_ui::Color color = TeamColor(team);
	const float flash = HUD_LocalKillFlash(time);
	color.r += (int)((255-color.r)*flash);
	color.g += (int)((255-color.g)*flash);
	color.b += (int)((255-color.b)*flash);
	using namespace weapon_hud_layout;
	const float cardTop = ScreenHeight-(cardHeight+cardBottomMargin)*scale;
	if (texture) match_ui::Picture(texture,center-cardWidth*scale*0.5f,cardTop,cardWidth*scale,cardHeight*scale,color);
	else
	{
		FillRGBABlend((int)(center-120*scale),(int)(cardTop+24*scale),(int)(240*scale),(int)(72*scale),12,16,22,185);
		match_ui::Text("KILL",center-match_ui::TextWidth("KILL",cardCountHeight*scale)*0.5f,cardTop+30*scale,cardCountHeight*scale,color);
	}
	if (s_KillFeedback.round_kills > 5 || !texture)
	{
		char count[24]; snprintf(count,sizeof(count),"x%d",s_KillFeedback.round_kills);
		const float height = cardCountHeight*scale;
		match_ui::Text(count,center-match_ui::TextWidth(count,height)*0.5f,cardTop+cardCountOffset*scale,height,color);
	}
}
}

void HUD_DrawKillCard(float time) { DrawKillCard(time); }

float HUD_LocalKillFlash(float time)
{
	if (!s_KillFeedback.card_visible || s_KillFeedback.local_dead || gHUD.m_Health.m_iHealth <= 0 ||
		!std::isfinite(time) || s_KillFeedback.last_kill_time < 0.0f) return 0.0f;
	const float elapsed = time-s_KillFeedback.last_kill_time;
	return elapsed >= 0.0f && elapsed < 1.0f ? 1.0f-elapsed : 0.0f;
}

void HUD_KillFeedbackDump()
{
	gEngfuncs.Con_Printf("killfeedback: round=%d hs=%d card=%d dead=%d notices=",s_KillFeedback.round_kills,
		s_KillFeedback.round_headshots,(int)s_KillFeedback.card_visible,(int)s_KillFeedback.local_dead);
	int count=0; while(count<MAX_DEATHNOTICES && rgDeathNoticeList[count].iId) ++count;
	gEngfuncs.Con_Printf("%d\n",count);
	const float scale=match_ui::Scale();
	gEngfuncs.Con_Printf("killcardlayout: width=%.3f height=%.3f x=%.3f y=%.3f scale=%.5f frame=%d\n",
		weapon_hud_layout::cardWidth*scale,weapon_hud_layout::cardHeight*scale,
		(ScreenWidth-weapon_hud_layout::cardWidth*scale)*0.5f,
		ScreenHeight-(weapon_hud_layout::cardHeight+weapon_hud_layout::cardBottomMargin)*scale,
		gHUD.m_flScale,kill_feedback::CardFrame(s_KillFeedback,gHUD.m_flTime));
}

cvar_t *cl_killsound;
cvar_t *cl_killsound_path;

int CHudDeathNotice :: Init( void )
{
	gHUD.AddHudElem( this );

	HOOK_MESSAGE( gHUD.m_DeathNotice, DeathMsg );
	HOOK_MESSAGE( gHUD.m_DeathNotice, HudRound );
	gEngfuncs.pfnAddCommand("death_icon_dump", DeathIconDump);
	gEngfuncs.pfnAddCommand("death_notice_dump", DeathNoticeDump);

	hud_deathnotice_time = CVAR_CREATE( "hud_deathnotice_time", "6", FCVAR_ARCHIVE );
	cl_killsound = CVAR_CREATE( "cl_killsound", "0", FCVAR_ARCHIVE );
	cl_killsound_path = CVAR_CREATE( "cl_killsound_path", "buttons/bell1.wav", FCVAR_ARCHIVE );
	m_iFlags = HUD_THINK;

	return 1;
}


void CHudDeathNotice :: InitHUDData( void )
{
	memset( rgDeathNoticeList, 0, sizeof(rgDeathNoticeList) );
	kill_feedback::ClearMap(s_KillFeedback);
	s_LastThinkTime = -1.0f;
	m_iFlags = HUD_THINK;
}

void CHudDeathNotice::Reset()
{
	// ResetHUD also arrives for fullupdate/demo recording within a round.
	// Retain confirmed kills and the card; HudRound owns the round boundary.
	s_LastThinkTime = -1.0f;
}

int CHudDeathNotice::MsgFunc_HudRound(const char *pszName, int iSize, void *pbuf)
{
	oc2_hostage::RoundReset();
	// GoldSrc bridge for CS:GO CCSPlayer::RoundRespawn (cs_player.cpp:8363-8364).
	kill_feedback::ResetRound(s_KillFeedback);
	gHUD.m_Timer.ResetRound();
	gHUD.m_RadarModern.ResetRound();
	// The real round boundary: the server sends this once per round restart, so
	// the planted C4 marker can be cleared here instead of on every ResetHUD.
	// ResetHUD also arrives mid round for fullupdate, and clearing there used to
	// erase an already planted bomb from the radar and the timer.
	g_PlayerExtraInfo[33].dead = true;
	g_PlayerExtraInfo[33].radarflashes = 0;
	g_PlayerExtraInfo[33].playerclass = 0;
	g_PlayerExtraInfo[33].origin = Vector( 0, 0, 0 );
	return 1;
}

void CHudDeathNotice::Think()
{
	if (s_LastThinkTime >= 0.0f && gHUD.m_flTime < s_LastThinkTime) InitHUDData();
	s_LastThinkTime = gHUD.m_flTime;
	ExpireNotices(gHUD.m_flTime); // CHud::Think runs even when rendering is hidden
	if (rgDeathNoticeList[0].iId || s_KillFeedback.card_visible) m_iFlags |= HUD_DRAW;
	else m_iFlags &= ~HUD_DRAW;
}


int CHudDeathNotice :: VidInit( void )
{
	m_HUD_d_skull = gHUD.GetSpriteIndex( "d_skull" );
	m_HUD_d_headshot = gHUD.GetSpriteIndex("d_headshot");

	return 1;
}

int CHudDeathNotice :: Draw( float flTime )
{
	int x, y, r, g, b, i;
	ExpireNotices(flTime);
	const bool modern = match_ui::Enabled();
	int firstVisible = 0;
	if (!modern)
	{
		int count = 0;
		while (count < MAX_DEATHNOTICES && rgDeathNoticeList[count].iId) ++count;
		firstVisible = max(0,count-4); // preserve the legacy four newest visible rows
	}

	for( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( rgDeathNoticeList[i].iId == 0 )
			break;  // we've gone through them all
		if (modern)
		{
			if (!(gHUD.m_iHideHUDDisplay & HIDEHUD_ALL)) DrawModernNotice(rgDeathNoticeList[i],i,m_HUD_d_skull,m_HUD_d_headshot,flTime);
			continue;
		}
		if (i < firstVisible) continue;

		if ( rgDeathNoticeList[i].flDisplayTime < flTime )
		{ // display time has expired
			// remove the current item from the list
			memmove( &rgDeathNoticeList[i], &rgDeathNoticeList[i+1], sizeof(DeathNoticeItem) * (MAX_DEATHNOTICES - i) );
			i--;  // continue on the next item;  stop the counter getting incremented
			continue;
		}

		rgDeathNoticeList[i].flDisplayTime = min( rgDeathNoticeList[i].flDisplayTime, flTime + DEATHNOTICE_DISPLAY_TIME );

		// Hide when scoreboard drawing. It will break triapi
		//if ( gViewPort && gViewPort->AllowedToPrintText() )
		//if ( !gHUD.m_iNoConsolePrint )
		{
			// Draw the death notice
			if( !g_iUser1 )
			{
				y = YRES(DEATHNOTICE_TOP) + 2 + (20 * (i-firstVisible));  //!!!
			}
			else
			{
				y = ScreenHeight / 5 + 2 + (20 * (i-firstVisible));
			}

			int id = (rgDeathNoticeList[i].iId == -1) ? m_HUD_d_skull : rgDeathNoticeList[i].iId;
			x = ScreenWidth - DrawUtils::ConsoleStringLen(rgDeathNoticeList[i].szVictim) - (gHUD.GetSpriteRect(id).Width());
			if( rgDeathNoticeList[i].iHeadShotId )
				x -= gHUD.GetSpriteRect(m_HUD_d_headshot).Width();

			if ( !rgDeathNoticeList[i].bSuicide )
			{
				x -= (5 + DrawUtils::ConsoleStringLen( rgDeathNoticeList[i].szKiller ) );

				// Draw killers name
				if ( rgDeathNoticeList[i].KillerColor )
					DrawUtils::SetConsoleTextColor( rgDeathNoticeList[i].KillerColor[0], rgDeathNoticeList[i].KillerColor[1], rgDeathNoticeList[i].KillerColor[2] );
				x = 5 + DrawUtils::DrawConsoleString( x, y, rgDeathNoticeList[i].szKiller );
			}

			r = 255;  g = 80;	b = 0;
			if ( rgDeathNoticeList[i].bTeamKill )
			{
				r = 10;	g = 240; b = 10;  // display it in sickly green
			}

			// Draw death weapon
			SPR_Set( gHUD.GetSprite(id), r, g, b );
			SPR_DrawAdditive( 0, x, y, &gHUD.GetSpriteRect(id) );

			x += (gHUD.GetSpriteRect(id).Width());

			if( rgDeathNoticeList[i].iHeadShotId)
			{
				SPR_Set( gHUD.GetSprite(m_HUD_d_headshot), r, g, b );
				SPR_DrawAdditive( 0, x, y, &gHUD.GetSpriteRect(m_HUD_d_headshot));
				x += (gHUD.GetSpriteRect(m_HUD_d_headshot).Width());
			}

			// Draw victims name (if it was a player that was killed)
			if (!rgDeathNoticeList[i].bNonPlayerKill)
			{
				if ( rgDeathNoticeList[i].VictimColor )
					DrawUtils::SetConsoleTextColor( rgDeathNoticeList[i].VictimColor[0], rgDeathNoticeList[i].VictimColor[1], rgDeathNoticeList[i].VictimColor[2] );
				x = DrawUtils::DrawConsoleString( x, y, rgDeathNoticeList[i].szVictim );
			}
		}
	}

	if( i == 0 && !s_KillFeedback.card_visible )
		m_iFlags &= ~HUD_DRAW; // disable hud item

	return 1;
}

// This message handler may be better off elsewhere
int CHudDeathNotice :: MsgFunc_DeathMsg( const char *pszName, int iSize, void *pbuf )
{
	m_iFlags |= HUD_DRAW;

	BufferReader reader( pszName, pbuf, iSize );

	int killer = reader.ReadByte();
	int victim = reader.ReadByte();
	int headshot = reader.ReadByte();

	char killedwith[32];
	const char *weaponName = reader.ReadString();
	strlcpy( killedwith, "d_", sizeof( killedwith ) );
	strlcat( killedwith, weaponName, sizeof( killedwith ) );
	// ClCorpse arrives after the server's death animation. Start physical
	// simulation from the last live pose at the actual kill event instead.
	OC2_RagdollSpawnDeath(victim, killer, headshot != 0);
	// The radar keeps its own death stamp: ScoreAttrib is not always broadcast.
	gHUD.m_RadarModern.NotePlayerDeath(victim, gHUD.m_flTime);

	//if (gViewPort)
	//	gViewPort->DeathMsg( killer, victim );
	gHUD.m_Scoreboard.DeathMsg( killer, victim );

	gHUD.m_Spectator.DeathMessage(victim);
	int i;
	for ( i = 0; i < MAX_DEATHNOTICES; i++ )
	{
		if ( rgDeathNoticeList[i].iId == 0 )
			break;
	}
	if ( i == MAX_DEATHNOTICES )
	{ // move the rest of the list forward to make room for this item
		memmove( rgDeathNoticeList, rgDeathNoticeList+1, sizeof(DeathNoticeItem) * MAX_DEATHNOTICES );
		i = MAX_DEATHNOTICES - 1;
	}
	// A reused queue entry must not inherit suicide/TK flags or colors.
	rgDeathNoticeList[i] = DeathNoticeItem();
	rgDeathNoticeList[i].killerIndex = killer;
	rgDeathNoticeList[i].victimIndex = victim;
	// Resource alias only: retain the original event weapon name for kill state.
	const char *resourceName = !strcmp(weaponName,"mp5navy") || !strcmp(weaponName,"mp5") ? "mp7" : weaponName;
	if (!strcmp(resourceName,"taser")) resourceName = "zeus";
	if (!strcmp(resourceName,"incgrenade")) resourceName = "incendiary";
	if (!strcmp(resourceName,"grenade")) resourceName = "hegrenade";
	if (!strcmp(resourceName,"world")) resourceName = "worldspawn";
	if (kill_feedback::WeaponResourceName(resourceName))
		snprintf(rgDeathNoticeList[i].weaponResource,sizeof(rgDeathNoticeList[i].weaponResource),"gfx/vgui/death/d_%s.tga",resourceName);

	//if (gViewPort)
		//gViewPort->GetAllPlayersInfo();
	gHUD.m_Scoreboard.GetAllPlayersInfo();

	// Get the Killer's name
	const char *killer_name = NULL;
	bool killer_this_player = false;
	if ( killer >= 1 && killer <= MAX_PLAYERS )
	{
		killer_name = g_PlayerInfoList[killer].name;
		killer_this_player = g_PlayerInfoList[killer].thisplayer;
	}

	if ( !killer_name )
	{
		killer_name = "";
		rgDeathNoticeList[i].szKiller[0] = 0;
	}
	else
	{
		rgDeathNoticeList[i].KillerColor = GetClientColor( killer );
		strlcpy( rgDeathNoticeList[i].szKiller, killer_name, sizeof( rgDeathNoticeList[i].szKiller ) );
	}

	// Get the Victim's name
	const char *victim_name = NULL;

	if ( victim >= 1 && victim <= MAX_PLAYERS )
		victim_name = g_PlayerInfoList[ victim ].name;

	if ( !victim_name )
	{
		victim_name = "";
		rgDeathNoticeList[i].szVictim[0] = 0;
	}
	else
	{
		rgDeathNoticeList[i].VictimColor = GetClientColor( victim );
		strlcpy( rgDeathNoticeList[i].szVictim, victim_name, sizeof( rgDeathNoticeList[i].szVictim ) );
	}

	// Is it a non-player object kill?
	// If victim is 255, the killer killed a specific, non-player object (like a sentrygun)
	if( victim == 255 )
	{
		rgDeathNoticeList[i].bNonPlayerKill = true;

		// Store the object's name in the Victim slot (skip the d_ bit)
		strlcpy( rgDeathNoticeList[i].szVictim, killedwith+2, sizeof( rgDeathNoticeList[i].szVictim ) );
	}
	else
	{
		if ( killer == victim || killer == 0 )
			rgDeathNoticeList[i].bSuicide = true;

		if ( !strncmp( killedwith, "d_teammate", sizeof(killedwith)  ) )
			rgDeathNoticeList[i].bTeamKill = true;
	}

	rgDeathNoticeList[i].iHeadShotId = headshot;
	rgDeathNoticeList[i].killerTeam = PlayerTeam(killer);
	rgDeathNoticeList[i].victimTeam = PlayerTeam(victim);
	if (killer != victim && killer >= 1 && killer <= MAX_PLAYERS && victim >= 1 && victim <= MAX_PLAYERS &&
		(rgDeathNoticeList[i].killerTeam == TEAM_CT || rgDeathNoticeList[i].killerTeam == TEAM_TERRORIST) &&
		rgDeathNoticeList[i].killerTeam == rgDeathNoticeList[i].victimTeam)
		rgDeathNoticeList[i].bTeamKill = true;
	const cl_entity_t *localPlayer = gEngfuncs.GetLocalPlayer();
	const int localIndex = localPlayer ? localPlayer->index : 0;
	rgDeathNoticeList[i].localKiller = localIndex == killer && killer != victim;
	rgDeathNoticeList[i].localVictim = localIndex == victim;
	kill_feedback::Event event;
	event.time = gHUD.m_flTime; event.local = localIndex; event.killer = killer; event.victim = victim;
	event.killer_team = rgDeathNoticeList[i].killerTeam;
	event.victim_team = rgDeathNoticeList[i].bTeamKill ? event.killer_team : rgDeathNoticeList[i].victimTeam;
	event.headshot = headshot != 0;
	const bool confirmedKill = kill_feedback::OnDeath(s_KillFeedback,event);
	OC2_AccessoryKill(weaponName,killer,victim,event.killer_team,event.victim_team);

	// Find the sprite in the list
	int spr = gHUD.GetSpriteIndex( killedwith );
	// APK knife variants have TGA artwork but may have no legacy HUD sprite.
	// The event token is immutable: never substitute the observer's loadout.
	if (spr < 0 && oc2_knife_icons::FindDeathToken(weaponName))
		spr = gHUD.GetSpriteIndex("d_knife");

	rgDeathNoticeList[i].iId = spr;

	float lifetime = hud_deathnotice_time->value;
	if (!std::isfinite(lifetime)) lifetime = 6.0f;
	lifetime = max(0.0f,lifetime);
	// CSGO csgo_huddeathnotice.cpp:536 extends local killer/victim notices.
	if (match_ui::Enabled() && (rgDeathNoticeList[i].localKiller || rgDeathNoticeList[i].localVictim)) lifetime *= 1.5f;
	const float fadeTime = match_ui::Enabled() && lifetime > 0.0f ? 1.0f : 0.0f;
	rgDeathNoticeList[i].flDisplayTime = gHUD.m_flTime + lifetime + fadeTime;
	if (confirmedKill) HUD_KillFeedbackDump();

	// Play kill sound
	if ((killer_this_player || g_iUser2 == killer) &&
		!rgDeathNoticeList[i].bNonPlayerKill &&
		!rgDeathNoticeList[i].bSuicide &&
		cl_killsound->value > 0.0f)
	{
		PlaySound(cl_killsound_path->string, cl_killsound->value);
	}

	if (rgDeathNoticeList[i].bNonPlayerKill)
	{
		ConsolePrint( rgDeathNoticeList[i].szKiller );
		ConsolePrint( " killed a " );
		ConsolePrint( rgDeathNoticeList[i].szVictim );
		ConsolePrint( "\n" );
	}
	else
	{
		// record the death notice in the console
		if ( rgDeathNoticeList[i].bSuicide )
		{
			ConsolePrint( rgDeathNoticeList[i].szVictim );

			if ( !strncmp( killedwith, "d_world", sizeof(killedwith)  ) )
			{
				ConsolePrint( " died" );
			}
			else
			{
				ConsolePrint( " killed self" );
			}
		}
		else if ( rgDeathNoticeList[i].bTeamKill )
		{
			ConsolePrint( rgDeathNoticeList[i].szKiller );
			ConsolePrint( " killed his teammate " );
			ConsolePrint( rgDeathNoticeList[i].szVictim );
		}
		else
		{
			if( headshot )
				ConsolePrint( "*** ");
			ConsolePrint( rgDeathNoticeList[i].szKiller );
			ConsolePrint( " killed " );
			ConsolePrint( rgDeathNoticeList[i].szVictim );
		}

		if ( *killedwith && (*killedwith > 13 ) && strncmp( killedwith, "d_world", sizeof(killedwith) ) && !rgDeathNoticeList[i].bTeamKill )
		{
			if ( headshot )
				ConsolePrint(" with a headshot from ");
			else
				ConsolePrint(" with ");

			ConsolePrint( killedwith+2 ); // skip over the "d_" part
		}

		if( headshot ) ConsolePrint( " ***");
		ConsolePrint( "\n" );
	}

	return 1;
}

