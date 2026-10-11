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

	// World position -> fractional tile index in the overview image.
	// Reverse engineered from CHudRadarModern::CalcPoint step 1; the same
	// expression appears verbatim in Draw's own instruction stream
	// (VMLA/ VMLS on d10/d13 with 1/1024 from DAT_000c5050).
	void WorldToTile( const Vector &vecWorld, float &tx, float &ty ) const;

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
	float m_flPixelsPerTile;   // screen pixels per overview tile

	// Grid basis. The mobile build computes these as cos(theta) and
	// sin(theta) from the player's view angle. With the radar locked to the
	// player's facing the basis is (sin yaw, -cos yaw) - at yaw 0 (facing
	// +X / east) that puts east up and north to the right, which is the
	// CS:GO layout. Kept as members so the term can be dialled in.
	float m_flTileA;
	float m_flTileB;
	bool  m_bTileRotated;      // overview_t.rotated, or cl_radar_flip bit 1

	// ---- world -> overview mapping, rebuilt every frame from the BSP world
	//      model bounds. The overview .txt zoom/origin are the CS spectator
	//      framing convention, not a description of the bitmap, so they
	//      cannot be used here (see UpdateRadarTransform).
	bool  m_bBoundsValid;
	float m_flMapMinX;         // world X of the bitmap's left edge
	float m_flMapMaxY;         // world Y of the bitmap's top edge
	float m_flTilesPerUnitX;   // overview tiles per world unit along X
	float m_flTilesPerUnitY;   // overview tiles per world unit along Y
};

extern CHudRadarModern gHudRadarModern;

#endif // RADAR_MODERN_H
