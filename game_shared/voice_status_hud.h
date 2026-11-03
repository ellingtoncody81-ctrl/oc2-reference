//========= Copyright � 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

#ifndef VOICE_STATUS_HUD_H
#define VOICE_STATUS_HUD_H
#pragma once

#include <utlvector.h>
// #include <vgui_controls/Panel.h>
// #include <vgui_controls/Label.h>
// #include <vgui_controls/ImagePanel.h>
// #include <color.h>
// #include <vgui/IScheme.h>
// #include <vgui_controls/Controls.h>

#ifdef CZERO
#include "vgui/hl_base/iviewport.h"
#else
// #include <game_controls/iviewport.h>
#endif

#include "voice_common.h"
#include "cl_entity.h"
#include "voice_banmgr.h"
#include "draw_util.h"
#include "triangleapi.h"

extern int g_VoiceLabelIcon;

//-----------------------------------------------------------------------------
// Purpose: a label which displays a name and a speaking icon
//-----------------------------------------------------------------------------
class CVoiceLabel
{
public:
	CVoiceLabel()
	{
		// m_pLabel = new VoiceVGUILabel( /*NULL,*/ "VoiceLabel", "" );
		// m_pLabel->SetParent( gViewPortInterface->GetViewPortPanel() );
		// m_pLabel->SetProportional(true);
		// m_pLabel->SetScheme("ClientScheme");
		// vgui::SETUP_PANEL(m_pLabel);
		m_clientindex = -1; // -1 means unassigned
		m_locationString = NULL;
		m_playerName = NULL;
	}

	~CVoiceLabel()
	{
		// m_pLabel->MarkForDeletion();
	}

	// pass throughs for various label calls
	void SetFgColor( RGBA c ) { m_fgColor = c; }
	void SetBgColor( RGBA c ) { m_bgColor = c; }
	void SetVisible( bool state ) { m_visible = state; }
	bool GetVisible() { return m_visible; }

	void GetContentSize( int &wide, int &tall )
	{
		// m_pLabel->GetContentSize( wide, tall );
		wide = DrawUtils::HudStringLen( m_buf );

		tall = gHUD.GetCharHeight();

		if ( tall < 32 )
			tall = 32;

		wide += tall - 2;
	}

	void SetBounds( int x, int y )
	{
		// m_pLabel->SetPos( x, y );
		this->x = x;
		this->y = y;
		// int wide, tall;
		// m_pLabel->GetContentSize( wide, tall );
		// m_pLabel->SetSize( wide, tall );
		GetContentSize( wide, tall );
	}

	void Draw()
	{
		if ( !GetVisible() )
			return;

		int offset = 1;
		int iconsize = tall - offset * 2;

		gEngfuncs.pfnFillRGBABlend( x, y, wide, tall, m_bgColor.r, m_bgColor.g, m_bgColor.b, m_bgColor.a );

		// Horizontal runs of equal alpha from the original 16x16 speaker4.tga.
		// FillRGBABlend applies the HUD scale itself, just like the label background.
		struct SpeakerRun { unsigned char row, first, end, alpha; };
		static const SpeakerRun speaker[] = {
			{2,12,13,227},
			{3,5,6,227}, {3,13,14,227},
			{4,4,6,227}, {4,10,11,227}, {4,13,14,227},
			{5,3,6,227}, {5,10,11,227}, {5,14,15,227},
			{6,0,4,227}, {6,4,5,255}, {6,5,6,227}, {6,7,8,227}, {6,11,12,227}, {6,14,15,227},
			{7,0,5,255}, {7,5,6,227}, {7,8,9,227}, {7,11,12,227}, {7,14,15,227},
			{8,0,5,255}, {8,5,6,227}, {8,8,9,227}, {8,11,12,227}, {8,14,15,227},
			{9,0,4,227}, {9,4,5,255}, {9,5,6,227}, {9,7,8,227}, {9,11,12,227}, {9,14,15,227},
			{10,3,6,227}, {10,10,11,227}, {10,14,15,227},
			{11,4,6,227}, {11,10,11,227}, {11,13,14,227},
			{12,5,6,227}, {12,13,14,227},
			{13,12,13,227}
		};
		for ( unsigned int i = 0; i < sizeof( speaker ) / sizeof( speaker[0] ); ++i )
		{
			const SpeakerRun &run = speaker[i];
			int left = x + iconsize * run.first / 16;
			int right = x + iconsize * run.end / 16;
			int top = y + offset + iconsize * run.row / 16;
			int bottom = y + offset + iconsize * ( run.row + 1 ) / 16;
			if ( right > left && bottom > top )
				gEngfuncs.pfnFillRGBABlend( left, top, right - left, bottom - top, 255, 255, 255, run.alpha );
		}

		int textx = x + iconsize + offset;
		int texty = y + ( tall - gHUD.GetCharHeight() ) / 2;
		DrawUtils::DrawHudString( textx, texty, textx + wide, m_buf, m_fgColor.r, m_fgColor.g, m_fgColor.b );
	}

	void SetClientIndex( int in ) { m_clientindex = in; }
	int GetClientIndex() { return m_clientindex; }

	void SetLocation( const char *location );
	void SetPlayerName( const char *name );

private:
	//-----------------------------------------------------------------------------
	// Purpose: inner class that overrides ApplySchemeSettings() for the label so an image can be loaded, also saves colors away
	//   so ApplySchemeSettings() doesn't override them
	//-----------------------------------------------------------------------------
	// class VoiceVGUILabel // : public vgui::Label
	// {
	// public:
	// 	VoiceVGUILabel( /* vgui::Panel *parent,*/ const char *name, const char *text ) /*: Label(parent, name, text)*/ { }

	// private:
	// VGUI2 overrides
	// virtual void ApplySchemeSettings(vgui::IScheme *pScheme)
	// {
	// 	Label::ApplySchemeSettings(pScheme);
	// 	SetTextImageIndex(1);
	// 	SetImagePreOffset( 1, 2); // shift the text over a little
	// 	// you need to load the image here, after Label::ApplySchemeSettings()(as applysettings nulls out all existing images)
	// 	SetImageAtIndex( 0, vgui::scheme()->GetImage( "gfx/vgui/speaker4", false), 1 );
	// }
	// };

	void RebuildLabelText();

	// VoiceVGUILabel *m_pLabel; // the label with the user name and icon
	int m_clientindex;      // Client index of the speaker. -1 if this label isn't being used.
	char *m_locationString; // localized location string.  NULL if the location is "".
	char *m_playerName;

	RGBA m_fgColor;
	RGBA m_bgColor;
	bool m_visible;

	int x, y, wide, tall;
	char m_buf[512];
	int iconwidth;
};

//-----------------------------------------------------------------------------
// Purpose: Handles the displaying of labels on the hud and icons above players in game when they talk
//-----------------------------------------------------------------------------
class CVoiceStatusHud : public IVoiceHud, public CHudBase
{
public:
	CVoiceStatusHud();
	virtual ~CVoiceStatusHud();

	// CHudBase overrides.
	// Initialize the cl_dll's voice manager.
	virtual int Init( IVoiceStatusHelper *pHelper, IVoiceStatus *pStatus );

	// ackPosition is the bottom position of where CVoiceStatus will draw the voice acknowledgement labels.
	virtual int VidInit();

	// Call from the HUD_CreateEntities function so it can add sprites above player heads.
	void CreateEntities();

	void UpdateLocation( int entindex, const char *location );

	void UpdateSpeakerStatus( int entindex, bool bTalking );

	CVoiceLabel *FindVoiceLabel( int clientindex ); // Find a CVoiceLabel representing the specified speaker.
	                                                // Returns NULL if none.
	                                                // entindex can be -1 if you want a currently-unused voice label.
	CVoiceLabel *GetFreeVoiceLabel();               // Get an unused voice label. Returns NULL if none.
	void RepositionLabels();

	void Shutdown( void );
	int Draw( float flTime );

private:
	cl_entity_s m_VoiceHeadModels[VOICE_MAX_PLAYERS]; // These aren't necessarily in the order of players. They are just
	                                                  // a place for it to put data in during CreateEntities.
	HSPRITE m_VoiceHeadModel;                         // Voice head model (goes above players who are speaking).
	float m_VoiceHeadModelHeight;                     // Height above their head to place the model.

	IVoiceStatusHelper *m_pHelper;
	IVoiceStatus *m_pStatus;

	// Labels telling who is speaking.
	CUtlVector<CVoiceLabel *> m_Labels;

	// vgui::ImagePanel *m_pLocalPlayerTalkIcon;
	int m_pLocalPlayerTalkIcon;

	bool m_LocalPlayerTalkIconVisible;
	int m_LocalPlayerTalkIconXPos, m_LocalPlayerTalkIconYPos;
	int m_LocalPlayerTalkIconXSize, m_LocalPlayerTalkIconYSize;
};

#endif // VOICE_STATUS_HUD_H
