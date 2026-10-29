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
	void LoadObjectives( const char *pszMapName );
	void DrawObjectives( int localTeam );
	void DrawObjectiveLabels();
	void DrawSiteLetter( int cx, int cy, char letter, int scale );
	void DrawBomb( int localTeam, float flTime );
	void DrawHostages( int localTeam, float flTime );
	bool ObjectivePoint( const Vector &world, int halfSize, int &x, int &y );

	// Recomputes the world<->radar scale from the overview description.
	// Must run before DrawOverviewLayer() and CalcPoint().
	void UpdateRadarTransform( int cx, int cy, int radius );

	// World position -> fractional tile index in the overview image.
	// Reverse engineered from CHudRadarModern::CalcPoint step 1; the same
	// expression appears verbatim in Draw's own instruction stream
	// (VMLA/ VMLS on d10/d13 with 1/1024 from DAT_000c5050).
	void WorldToTile( const Vector &vecWorld, float &tx, float &ty ) const;

	// World DIRECTION -> tile direction. Same axis pairing and signs as
	// WorldToTile, minus the scale and the constants.
	//
	// The self marker must go through this instead of carrying its own
	// hardcoded facing vector: the ROTATED overview changes which axis feeds
	// which tile column and flips one sign, and a hardcoded vector silently
	// pins the arrow to (-1,0) at every yaw. Deriving it here means the arrow
	// cannot drift away from the map again if the mapping is ever touched.
	void WorldDirToTileDir( float dx, float dy, float &tx, float &ty ) const;

	// Rebuilds m_aRadarEnemyVisible. Throttled; cheap to call every frame.
	void UpdateEnemyVisibility( float flTime );

	void DrawOverviewLayer( int cx, int cy, int radius );
	void DrawRadarDisc( int cx, int cy, int radius );
	void DrawSelfMarker( int cx, int cy, int radius );
	bool DrawBlurredBackground( int cx, int cy, int radius );
	void DrawRadarMask( int cx, int cy, int radius );
	void DrawRadarFrame( int cx, int cy, int radius, int r, int g, int b, int a, bool additive );
	void DrawWarningFrame( int cx, int cy, int radius, float flTime );
	void DrawPlayerBlip( int cx, int cy, int radius, int i, float flTime );
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
	struct Objective
	{
		Vector origin;
		bool bomb;
		char label;
		char group[64];
		int sources;
	};
	Objective m_Objectives[32];
	int m_iObjectiveCount;
	int m_iObjectiveSource;   // 0 none, 1 world model entities, 2 BSP file
	char m_szObjectiveMap[64];
	bool m_bObjectivesLoaded;
	const char *m_pObjectiveEntities;
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

	// ---- enemy line-of-sight gating ---------------------------------------
	// An enemy blip is only drawn when the local player can actually see that
	// enemy: inside the view cone, and with a clear trace to the feet and to
	// the head. Without this every enemy that ever entered the PVS stays on
	// the radar for as long as its entity survives, which reads as "enemies
	// are always visible in a radius around you".
	//
	// Modeled on NextClient's RadarVisibility (nextclient/client_mini/src/hud/
	// radar/policy/Visibility.cpp): a 20 Hz sweep, view cone then trace.
	// Their version also lends the eyes of teammates the server revealed; we
	// have no such reveal on this server, so only the local player is a viewer.
	bool  m_bRadarGating;                       // false = draw everything
	bool  m_aRadarEnemyVisible[32];            // engine caps maxclients at 32
	float m_flRadarVisNext;                      // throttled to 20 Hz
	float m_flRadarVisLast;                      // previous sweep, for the step

	// Last-seen tracker. The ghost blip and the death X both need a position
	// captured while the entity still existed: a dead player's entity is gone
	// and g_PlayerExtraInfo[i].origin is never filled in by this client.
	bool  m_aRadarEnemySeen[32];               // a live position was recorded
	bool  m_aRadarEnemyConfirmed[32];          // sight held long enough to show
	Vector m_aRadarEnemyLastOrigin[32];
	float m_aRadarEnemyLastSeen[32];            // last clear sight, seconds
	float m_aRadarEnemyConfirm[32];             // continuous sight, seconds
	float m_aRadarEnemyHold[32];                // seconds since sight was lost
	float m_aRadarPlayerDeadAt[32];             // 0 = alive
	Vector m_aRadarPlayerDeadOrigin[32];

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
