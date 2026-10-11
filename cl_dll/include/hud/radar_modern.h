/*
radar_modern.h - CS:GO style radar (CHudRadarModern)

Reconstructed from the Android build of Offensive Classic 2
(apk/lib/armeabi-v7a/libclient.so). The mobile build ships two radar
implementations side by side:

    CHudRadar          factory
    +- CHudRadarLegacy  classic radar -- this is what the PC build has
    +- CHudRadarModern  CS:GO radar     -- implemented here

Key behaviours taken from the decompiled binary:

  * circular mask built from 63 arc segments (0x3f)
  * optional blurred screen background sampled slightly oversized
    (texture coords run -0.1 .. 1.1) for the CS:GO soft edge
  * red warning frame that blinks at 2 Hz once the bomb is planted
  * radar geometry driven entirely by cvars, see Init()
*/
#pragma once
#ifndef RADAR_MODERN_H
#define RADAR_MODERN_H

#include "hud.h"

class CClientSprite;

class CHudRadarModern : public CHudBase
{
public:
	virtual int Init();
	virtual int VidInit();
	virtual int Draw( float flTime );
	virtual void Think();
	virtual void InitHUDData();
	virtual void Reset();
	virtual void Shutdown();

	// cl_radar_modern 0 restores the classic radar; anything else keeps the
	// CS:GO one. Defaults to on, matching the mobile build.
	bool IsEnabled() const;

	// the legacy radar still owns the net messages (player/bomb/hostage),
	// so they stay registered on CHudRadar and we only read their state
	void SetLegacyState( int iPlayerNum );

private:
	void LoadSprites();
	void LoadOverview( const char *pszMapName );

	// Recomputes the world<->radar scale from the overview description.
	// Must run before DrawOverviewLayer() and CalcPoint().
	void UpdateRadarTransform( int cx, int cy, int radius );

	// Rotates a screen point about the radar centre so the direction the
	// player is looking at ends up pointing up. The map tiles and the
	// blips both go through this, which is what keeps them locked together
	// while the player turns.
	void ApplyRadarRotation( float &x, float &y ) const;

	void DrawOverviewLayer( int cx, int cy, int radius );
	void DrawRadarDisc( int cx, int cy, int radius );
	void DrawSelfMarker( int cx, int cy, int radius );
	void DrawBlurredBackground( int cx, int cy, int size );
	void DrawRadarMask( int cx, int cy, int radius );
	void DrawRadarFrame( int cx, int cy, int radius, int r, int g, int b, int a );
	void DrawWarningFrame( int cx, int cy, int radius, float flTime );
	void DrawPlayerBlip( int cx, int cy, int radius, int i, int team, float flTime );
	bool  CalcPoint( const Vector &vecWorld, int &x, int &y, int &radius );

public:
	// ---- the six cvars recovered from the mobile build's constructor
	cvar_t *m_pNewRadarSize;   // cl_newradar_size  "0.175"
	cvar_t *m_pNewRadarR;      // cl_newradar_r     "1"
	cvar_t *m_pNewRadarG;      // cl_newradar_g     "1"
	cvar_t *m_pNewRadarB;      // cl_newradar_b     "1"
	cvar_t *m_pNewRadarA;      // cl_newradar_a     "0.75"
	cvar_t *m_pRadarPosX;      // cl_radar_pos_x    "0"
	cvar_t *m_pRadarPosY;      // cl_radar_pos_y    "0"

	// ---- sprites are kept in a module-local table in radar_modern.cpp

	bool  m_bSpritesLoaded;
	bool  m_bTriedLoad;
	bool  m_bOverviewValid;
	char  m_szMapName[64];
	overviewInfo_t m_OverviewData;
	bool  m_bEnabled;
	float m_flLastCapture;   // throttle for the blurred background
	float m_flLastBombPlanted;
	int   m_iPlayerNum;
	int   m_iCenterX;
	int   m_iCenterY;
	int   m_iRadius;

	// ---- overview -> radar transform, refreshed every frame by
	//      UpdateRadarTransform(). m_flPixelsPerWorld is the single number
	//      both the background tiles and the player blips go through, which
	//      is what keeps them locked together.
	int   m_iOverviewTilesX;   // tile columns of the overview image
	int   m_iOverviewTilesY;   // tile rows
	float m_flPixelsPerWorld;  // screen pixels per world unit
	bool  m_bRadarRotate;      // cl_radar_rotate: turn the map with the view
};

extern CHudRadarModern gHudRadarModern;

#endif // RADAR_MODERN_H
