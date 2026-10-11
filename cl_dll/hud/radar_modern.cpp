/*
radar_modern.cpp - CS:GO style radar (CHudRadarModern)

Reconstructed from the Android build of Offensive Classic 2
(apk/lib/armeabi-v7a/libclient.so), symbols CHudRadarModern::* and
DrawRadarWithBlurredBackground. See cl_dll/include/hud/radar_modern.h.

The mobile build talks to OpenGL directly; this build goes through the
engine's triangle API like every other HUD element does. Observable
behaviour is reproduced, not the exact GL call sequence.

Measurements taken from the decompiled binary:
  * radar radius  = ScreenHeight * cl_newradar_size (0.175)
  * circular mask = 63 segments (0x3f in the binary)
  * blurred background sampled with texcoords -0.1 .. 1.1
  * warning frame = red, alpha flips 255 / 100 at 2 Hz
  * player blip   = 20x20 solid square when no sprite applies
*/

#include "hud.h"
#include "hud_spectator.h"	// overviewInfo_t / CHudSpectator overview parser
#include "com_model.h"		// model_t, used for the overview map sprite
#include "cl_util.h"
#include "csprite.h"
#include "draw_util.h"
#include "triangleapi.h"
#include "radar_int.h"
#include "hud/radar_modern.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// filled in by HUD_RadarInterface() in cdll_int.cpp
extern radar_engfuncs_t gRadarAPI;
extern int g_iRadarAPIVersion;

#define RADAR_MASK_SEGMENTS  63
#define BLIP_SIZE            20
#define RADAR_SCALE          32.0f	// same as CHudRadar::WorldToRadar

CHudRadarModern gHudRadarModern;

// the six cvars recovered from the mobile build's constructor, plus the
// blur switches it registered inside CHud::Init
static cvar_t *s_cl_newradar_size = NULL;
static cvar_t *s_cl_newradar_r    = NULL;
static cvar_t *s_cl_newradar_g    = NULL;
static cvar_t *s_cl_newradar_b    = NULL;
static cvar_t *s_cl_newradar_a    = NULL;
static cvar_t *s_cl_radar_pos_x   = NULL;
static cvar_t *s_cl_radar_pos_y   = NULL;
static cvar_t *s_cl_blur_radar_bg = NULL;
static cvar_t *s_cl_blur_update_rate = NULL;
static cvar_t *s_cl_radar_modern     = NULL;
static cvar_t *s_cl_radar_debug      = NULL;
static float  s_flRadarViewFraction  = 1.0f;
static cvar_t *s_cl_radar_view        = NULL;

// sprite slots live in a plain table so LoadSprites() can fill them without
// needing member-pointer gymnastics
static CClientSprite *s_spr[11];

static const char *const s_radarSpriteNames[11] =
{
	"iplayer", "iplayerblue", "iplayerred", "iplayerdead", "iplayervip",
	"iplayerc4", "ibackpack", "camera", "iradio", "ic4", "ihostage",
};

enum
{
	SPR_PLAYER, SPR_PLAYER_BLUE, SPR_PLAYER_RED, SPR_PLAYER_DEAD, SPR_PLAYER_VIP,
	SPR_PLAYER_C4, SPR_BACKPACK, SPR_CAMERA, SPR_RADAR, SPR_C4, SPR_HOSTAGE
};

/*
========================
Init
========================
*/
int CHudRadarModern::Init( void )
{
	m_bSpritesLoaded    = false;
	m_flLastCapture     = 0;
	m_flLastBombPlanted = 0;
	m_iPlayerNum        = 0;

	// defaults lifted verbatim from the mobile build's CHudRadarModern ctor
	s_cl_newradar_size    = CVAR_CREATE( "cl_newradar_size", "0.175", FCVAR_ARCHIVE );
	s_cl_newradar_r       = CVAR_CREATE( "cl_newradar_r",    "1",     FCVAR_ARCHIVE );
	s_cl_newradar_g       = CVAR_CREATE( "cl_newradar_g",    "1",     FCVAR_ARCHIVE );
	s_cl_newradar_b       = CVAR_CREATE( "cl_newradar_b",    "1",     FCVAR_ARCHIVE );
	s_cl_newradar_a       = CVAR_CREATE( "cl_newradar_a",    "0.75",  FCVAR_ARCHIVE );
	s_cl_radar_pos_x      = CVAR_CREATE( "cl_radar_pos_x",   "0",     FCVAR_ARCHIVE );
	s_cl_radar_pos_y      = CVAR_CREATE( "cl_radar_pos_y",   "0",     FCVAR_ARCHIVE );
	// The blurred background needs a working engine-side FBO capture; while
	// GL_CaptureRadarBackground yields nothing usable it draws an opaque
	// black quad over the whole radar. Default it off.
	s_cl_blur_radar_bg    = CVAR_CREATE( "cl_blur_radar_bg", "1",     FCVAR_ARCHIVE );
	s_cl_blur_update_rate = CVAR_CREATE( "cl_blur_update_rate", "0.5", FCVAR_ARCHIVE );
	s_cl_radar_modern     = CVAR_CREATE( "cl_radar_modern", "1", FCVAR_ARCHIVE );
	s_cl_radar_debug      = CVAR_CREATE( "cl_radar_debug", "1", FCVAR_ARCHIVE );
	// how much of the map the radar window covers (1.0 = whole map, zoomed in
	// only means the radar shows a larger scale). Smaller = see more.
	s_cl_radar_view       = CVAR_CREATE( "cl_radar_view", "1", FCVAR_ARCHIVE );

	m_bEnabled = true;

	m_pNewRadarSize = s_cl_newradar_size;
	m_pNewRadarR    = s_cl_newradar_r;
	m_pNewRadarG    = s_cl_newradar_g;
	m_pNewRadarB    = s_cl_newradar_b;
	m_pNewRadarA    = s_cl_newradar_a;
	m_pRadarPosX    = s_cl_radar_pos_x;
	m_pRadarPosY    = s_cl_radar_pos_y;

	// CHud::Redraw only calls Draw() on elements whose HUD_DRAW bit is set,
	// and HudListMsg/Shutdown likewise iterate m_pHudList. Without this the
	// radar is silently never drawn.
	m_iFlags = HUD_DRAW;

	// HUD_Init runs *after* HUD_VidInit (cdll_int.cpp), so an element added
	// to the HUD list from here has already missed its VidInit() pass and
	// would never load sprites. Make Draw() self-heal on the first frame.
	m_bSpritesLoaded = false;
	m_bTriedLoad     = false;
	m_bOverviewValid = false;
	memset( m_szMapName, 0, sizeof( m_szMapName ) );
	memset( &m_OverviewData, 0, sizeof( m_OverviewData ) );

	gHUD.AddHudElem( this );

	return 1;
}

/*
========================
VidInit
========================
*/
int CHudRadarModern::VidInit( void )
{
	Shutdown();
	LoadSprites();
	m_bTriedLoad = true;

	if( g_iRadarAPIVersion && gRadarAPI.pfnInitRadarCapture )
		gRadarAPI.pfnInitRadarCapture();

	return 1;
}

void CHudRadarModern::LoadSprites( void )
{
	size_t i;

	for( i = 0; i < 11; i++ )
	{
		int index = HUD_GetSpriteIndexByName( s_radarSpriteNames[i] );
		if( !index )
		{
			gEngfuncs.Con_Printf( "GetSpriteIndex: %s sprite not found\n",
				s_radarSpriteNames[i] );
			continue;
		}

		delete s_spr[i];
		s_spr[i] = new CClientSprite( s_radarSpriteNames[i] );
	}

	m_bSpritesLoaded = true;
}

void CHudRadarModern::Shutdown( void )
{
	size_t i;

	for( i = 0; i < 11; i++ )
	{
		delete s_spr[i];
		s_spr[i] = NULL;
	}

	if( g_iRadarAPIVersion && gRadarAPI.pfnFreeRadarCapture )
		gRadarAPI.pfnFreeRadarCapture();

	m_bSpritesLoaded = false;
}

void CHudRadarModern::InitHUDData( void )
{
	m_flLastCapture = 0;
}

void CHudRadarModern::Reset( void )
{
	m_flLastCapture     = 0;
	m_flLastBombPlanted = 0;
}

void CHudRadarModern::Think( void )
{
}

void CHudRadarModern::SetLegacyState( int iPlayerNum )
{
	m_iPlayerNum = iPlayerNum;
}

/*
========================
CalcPoint

World -> radar pixel mapping, same shape as CHudRadar::WorldToRadar:
scale the planar delta, rotate it into view space and clip onto the
circle. Returns true when the point had to be clipped.
========================
*/
bool CHudRadarModern::CalcPoint( const Vector &vecWorld, int &x, int &y, int &radius )
{
	Vector2D diff = vecWorld.Make2D() - gHUD.m_vecOrigin.Make2D();

	// supply epsilons to avoid divide-by-zero, like the legacy radar
	if( diff.x == 0 ) diff.x = 0.00001f;
	if( diff.y == 0 ) diff.y = 0.00001f;

	float flOffset = DEG2RAD( gHUD.m_vecAngles.y - RAD2DEG( atan2f( diff.y, diff.x ) ) );

	radius = m_iRadius;

	float iRadius = min( diff.Length() / RADAR_SCALE, (float)m_iRadius );

	float fx = (float)( iRadius * sinf( flOffset ) );
	float fy = (float)( iRadius * -cosf( flOffset ) );

	bool bClipped = ( diff.Length() / RADAR_SCALE ) > (float)m_iRadius;

	x = m_iCenterX + (int)fx;
	y = m_iCenterY + (int)fy;

	return bClipped;
}

/*
========================
LoadOverview

CHudRadarModern::Reset() in the mobile build does:

    name = GetLevelName();                 // "de_dust.bsp"
    if( strcmp(this->m_szMapName, name) ) {
        strip extension
        path = "overviews/" + name + ".txt"
        LoadOverviewInfo( path, &this->overview );
    }

so the overview description is re-read only when the map changes. The
format matches what CHudSpectator::ParseOverviewFile already understands,
so reuse that parser instead of duplicating it.
========================
*/
void CHudRadarModern::LoadOverview( const char *pszMapName )
{
	if( !pszMapName )
		return;

	if( !strcmp( m_szMapName, pszMapName ) && m_bOverviewValid )
		return;

	strncpy( m_szMapName, pszMapName, sizeof( m_szMapName ) - 1 );
	m_szMapName[sizeof( m_szMapName ) - 1 ] = 0;

	// the spectator owns the parsed overview data and the loaded map sprite
	if( strcmp( gHUD.m_Spectator.m_OverviewData.map, m_szMapName ) )
	{
		if( !gHUD.m_Spectator.ParseOverviewFile() )
		{
			m_bOverviewValid = false;
			return;
		}
	}

	// always make sure the tile sprite exists, even if the description was
	// already parsed by the spectator for this map
	if( !gHUD.m_Spectator.GetMapSprite() )
		gHUD.m_Spectator.LoadMapSprites();

	if( gHUD.m_Spectator.m_OverviewData.layers > 0 && gHUD.m_Spectator.GetMapSprite() )
	{
		m_bOverviewValid = true;
		m_OverviewData    = gHUD.m_Spectator.m_OverviewData;
	}
	else
	{
		m_bOverviewValid = false;
	}
}

/*
========================
DrawOverviewLayer

Ported from CHudSpectator::DrawOverviewLayer, which is the same tiled
blit the mobile build performs in its Draw() (numframes/(4*3), sqrt,
xTiles*4 by yTiles*3). Difference: we project into 2D screen space around
the radar centre instead of a skybox, and clip to the radar circle.
========================
*/
void CHudRadarModern::DrawOverviewLayer( int cx, int cy, int radius )
{
	// CHudSpectator already loaded the overview image into a sprite whose
	// frames are the individual tiles ("each tile is a sprite frame").
	model_t *pSprite = gHUD.m_Spectator.GetMapSprite();

	if( !pSprite || pSprite->numframes <= 0 )
		return;

	int i = (int)sqrt( (float)( pSprite->numframes / (4 * 3) ) );
	if( i < 1 )
		i = 1;
	int xTiles = i * 4;
	int yTiles = i * 3;

	float flZoom = m_OverviewData.zoom;
	if( flZoom <= 0.0f )
		flZoom = 1.0f;

	float flXs = m_OverviewData.origin[0];
	float flYs = m_OverviewData.origin[1];

	// The overview sprite covers the whole map, whose world half-extent is
	// 4096/zoom (CHudSpectator::DrawOverviewLayer). The radar only shows a
	// window around the player, so scale the world->pixel factor by how much
	// of the map that window is (RadarViewFraction of the full width).
	const float kView = 4096.0f;
	float flMapHalf = kView / flZoom;                  // world units, map half size
	float flPxPerWorld = ( (float)radius * 2.0f * s_flRadarViewFraction ) / ( 2.0f * flMapHalf );

	// tiles exactly span the radar diameter
	float xStep = ( (float)radius * 2.0f ) / (float)xTiles;
	float yStep = ( (float)radius * 2.0f ) / (float)yTiles;

	// centre the window on the player: the map point under the player must
	// land on the radar centre, so the grid origin sits one radius back.
	float x = cx - radius - ( gHUD.m_vecOrigin[0] - flXs ) * flPxPerWorld;
	float y = cy - radius - ( gHUD.m_vecOrigin[1] - flYs ) * flPxPerWorld;

	if( m_OverviewData.rotated )
	{
		float tmp = x - cx;
		x = cx + ( y - cy );
		y = cy - tmp;
	}

	int frame = 0;
	int ix, iy;

	// remember the row origin so every row restarts from the same column
	const float flRowX = x;

	gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4f( 1.0f, 1.0f, 1.0f, 0.85f );

	for( iy = 0; iy < yTiles; iy++ )
	{
		float ty = y;

		for( ix = 0; ix < xTiles; ix++ )
		{
			gEngfuncs.pTriAPI->SpriteTexture( pSprite, frame );

			gEngfuncs.pTriAPI->Begin( TRI_QUADS );
			gEngfuncs.pTriAPI->TexCoord2f( 0, 0 );
			gEngfuncs.pTriAPI->Vertex3f( x, ty, 0 );
			gEngfuncs.pTriAPI->TexCoord2f( 1, 0 );
			gEngfuncs.pTriAPI->Vertex3f( x + xStep, ty, 0 );
			gEngfuncs.pTriAPI->TexCoord2f( 1, 1 );
			gEngfuncs.pTriAPI->Vertex3f( x + xStep, ty + yStep, 0 );
			gEngfuncs.pTriAPI->TexCoord2f( 0, 1 );
			gEngfuncs.pTriAPI->Vertex3f( x, ty + yStep, 0 );
			gEngfuncs.pTriAPI->End();

			frame++;
			x += xStep;
		}

		x = flRowX;
		y += yStep;
	}

	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
}

/*
========================
DrawBlurredBackground

The engine hands back a texture holding the screen area around the radar,
already box-blurred. Drawing it with texture coordinates outside 0..1 gives
the CS:GO soft, slightly oversized edge.
========================
*/
void CHudRadarModern::DrawBlurredBackground( int cx, int cy, int size )
{
	const float s = -0.1f;	// overscan, matches the original
	const float e = 1.1f;

	gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( 255, 255, 255, 150 );	// original alpha 0x96

	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
	gEngfuncs.pTriAPI->TexCoord2f( s, s );
	gEngfuncs.pTriAPI->Vertex3f( cx - size, cy - size, 0 );
	gEngfuncs.pTriAPI->TexCoord2f( e, s );
	gEngfuncs.pTriAPI->Vertex3f( cx + size, cy - size, 0 );
	gEngfuncs.pTriAPI->TexCoord2f( e, e );
	gEngfuncs.pTriAPI->Vertex3f( cx + size, cy + size, 0 );
	gEngfuncs.pTriAPI->TexCoord2f( s, e );
	gEngfuncs.pTriAPI->Vertex3f( cx - size, cy + size, 0 );
	gEngfuncs.pTriAPI->End();
}

/*
========================
DrawRadarMask

63 segments, same as the mobile build.
========================
*/
void CHudRadarModern::DrawRadarMask( int cx, int cy, int radius )
{
	int i;
	float flStep = ( 2.0f * 3.14159265358979f ) / RADAR_MASK_SEGMENTS;
	gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( 0, 0, 0, 0 );
	gEngfuncs.pTriAPI->Begin( TRI_LINES );

	for( i = 0; i < RADAR_MASK_SEGMENTS; i++ )
	{
		float a = flStep * i;
		gEngfuncs.pTriAPI->Vertex3f( (float)cx, (float)cy, 0 );
		gEngfuncs.pTriAPI->Vertex3f(
			cx + cosf( a ) * radius, cy + sinf( a ) * radius, 0 );
	}

	gEngfuncs.pTriAPI->End();
}

void CHudRadarModern::DrawRadarFrame( int cx, int cy, int radius, int r, int g, int b, int a )
{
	// outline, not a filled block: a solid quad just hides the map behind it
	int x0 = cx - radius, y0 = cy - radius;
	int x1 = cx + radius, y1 = cy + radius;
	int t  = max( 1, radius / 32 );

	gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( r, g, b, a );

	gEngfuncs.pTriAPI->Begin( TRI_QUADS );
	// top
	gEngfuncs.pTriAPI->Vertex3f( x0, y0, 0 );            gEngfuncs.pTriAPI->Vertex3f( x1, y0 + t, 0 );
	gEngfuncs.pTriAPI->Vertex3f( x1, y0, 0 );            gEngfuncs.pTriAPI->Vertex3f( x0, y0 + t, 0 );
	// bottom
	gEngfuncs.pTriAPI->Vertex3f( x0, y1 - t, 0 );        gEngfuncs.pTriAPI->Vertex3f( x1, y1, 0 );
	gEngfuncs.pTriAPI->Vertex3f( x1, y1 - t, 0 );        gEngfuncs.pTriAPI->Vertex3f( x0, y1, 0 );
	// left
	gEngfuncs.pTriAPI->Vertex3f( x0, y0 + t, 0 );        gEngfuncs.pTriAPI->Vertex3f( x0 + t, y1 - t, 0 );
	gEngfuncs.pTriAPI->Vertex3f( x0 + t, y0 + t, 0 );    gEngfuncs.pTriAPI->Vertex3f( x0, y1 - t, 0 );
	// right
	gEngfuncs.pTriAPI->Vertex3f( x1 - t, y0 + t, 0 );    gEngfuncs.pTriAPI->Vertex3f( x1, y1 - t, 0 );
	gEngfuncs.pTriAPI->Vertex3f( x1 - t, y1 - t, 0 );    gEngfuncs.pTriAPI->Vertex3f( x1, y0 + t, 0 );
	gEngfuncs.pTriAPI->End();
}

/*
========================
DrawWarningFrame

Blinks red at 2 Hz, matching ((int)(time*2) & 1) ? 0xff : 100.
========================
*/
void CHudRadarModern::DrawWarningFrame( int cx, int cy, int radius, float flTime )
{
	if( !( gHUD.m_iWeaponBits & (1<<WEAPON_SUIT) ) )
		return;

	int iAlpha = ( (int)( flTime * 2 ) & 1 ) == 0 ? 255 : 100;
	DrawRadarFrame( cx, cy, radius, 255, 0, 0, iAlpha );
}

void CHudRadarModern::DrawPlayerBlip( int cx, int cy, int radius, int i, int team, float flTime )
{
	if( i == m_iPlayerNum || g_PlayerExtraInfo[i].dead )
		return;

	bool bFriend = ( team == g_PlayerExtraInfo[ m_iPlayerNum ].teamnumber );

	int r = bFriend ? 0 : 255;
	int g = bFriend ? 255 : 0;
	int b = 0;

	CClientSprite *pSprite = bFriend ? s_spr[SPR_PLAYER_BLUE] : s_spr[SPR_PLAYER_RED];

	int x, y;
	CalcPoint( g_PlayerExtraInfo[i].origin, x, y, radius );

	if( pSprite && pSprite->spr )
	{
		SPR_Set( pSprite->spr, r, g, b );
		SPR_DrawAdditive( x - BLIP_SIZE / 2, y - BLIP_SIZE / 2, 0, &pSprite->rect );
		return;
	}

	// no sprite -> the mobile build draws a 20x20 solid square
	FillRGBA( x - BLIP_SIZE / 2, y - BLIP_SIZE / 2, BLIP_SIZE, BLIP_SIZE,
		r, g, b, 255 );
}

/*
========================
Draw
========================
*/
bool CHudRadarModern::IsEnabled() const
{
	return m_bEnabled && s_cl_radar_modern
		&& s_cl_radar_modern->value != 0.0f
		&& m_pNewRadarSize && m_pNewRadarSize->value > 0.0f;
}

int CHudRadarModern::Draw( float flTime )
{
	if( !IsEnabled() )
		return 1;

	if( ( gHUD.m_iHideHUDDisplay & HIDEHUD_HEALTH )
		|| gEngfuncs.IsSpectateOnly()
		|| !( gHUD.m_iWeaponBits & (1<<WEAPON_SUIT) )
		|| gHUD.m_fPlayerDead )
		return 1;

	if( !m_bSpritesLoaded || !m_pNewRadarSize )
	{
		// first frame after a resolution change, or the missed VidInit()
		if( !m_bTriedLoad )
		{
			m_bTriedLoad = true;
			LoadSprites();
		}
	}

	if( !m_bSpritesLoaded || !m_pNewRadarSize )
		return 1;

	m_iPlayerNum = gHUD.m_Scoreboard.m_iPlayerNum;

	// The mobile build computes the centre as
	//     centre = (cl_radar_pos_x + radius) - layoutBase
	// so cl_radar_pos_x/y are an *offset* from the fixed corner placement,
	// not an absolute centre. Reproduce that: anchor the radar at the
	// top-left corner and let the cvars nudge it.
	m_iRadius   = (int)( ScreenHeight * m_pNewRadarSize->value );
	m_iCenterX  = m_iRadius + (int)m_pRadarPosX->value;
	m_iCenterY  = m_iRadius + (int)m_pRadarPosY->value;

	// ---- overview background: re-read only when the map changes,
	//        exactly like the mobile build's Reset()
	LoadOverview( gEngfuncs.pfnGetLevelName() );

	// ---- map tiles
	if( s_cl_radar_view )
	{
		s_flRadarViewFraction = s_cl_radar_view->value;
		if( s_flRadarViewFraction <= 0.0f || s_flRadarViewFraction > 1.0f )
			s_flRadarViewFraction = 1.0f;
	}

	if( m_bOverviewValid )
		DrawOverviewLayer( m_iCenterX, m_iCenterY, m_iRadius );

	// temporary on-screen diagnostics (removed once the radar is confirmed)
	if( s_cl_radar_debug )
	{
		char szDbg[256];
		int nFrames = gHUD.m_Spectator.GetMapSprite()
			? gHUD.m_Spectator.GetMapSprite()->numframes : -1;
		snprintf( szDbg, sizeof( szDbg ),
			"radar: valid=%d frames=%d zoom=%g org=%g,%g pl=%g,%g view=%g r=%d c=%d,%d",
			m_bOverviewValid, nFrames, m_OverviewData.zoom,
			m_OverviewData.origin[0], m_OverviewData.origin[1],
			gHUD.m_vecOrigin[0], gHUD.m_vecOrigin[1],
			s_flRadarViewFraction, m_iRadius, m_iCenterX, m_iCenterY );
		DrawUtils::DrawConsoleString( 4, 260, szDbg );
	}

	// ---- blurred screen background, throttled by cl_blur_update_rate
	if( g_iRadarAPIVersion
		&& gRadarAPI.pfnCaptureRadarBackground
		&& gRadarAPI.pfnGetRadarBackgroundTexture
		&& gRadarAPI.pfnGetRadarBackgroundTexture()
		&& s_cl_blur_radar_bg->value == 0.0f )
	{
		float flInterval = s_cl_blur_update_rate ? s_cl_blur_update_rate->value : 0.5f;
		if( flTime - m_flLastCapture >= flInterval )
		{
			gRadarAPI.pfnCaptureRadarBackground( 2, m_iCenterX, m_iCenterY, m_iRadius );
			m_flLastCapture = flTime;
		}

		DrawBlurredBackground( m_iCenterX, m_iCenterY, m_iRadius );
	}

	// ---- radar body
	int r = (int)( m_pNewRadarR->value * 255 );
	int g = (int)( m_pNewRadarG->value * 255 );
	int b = (int)( m_pNewRadarB->value * 255 );
	int a = (int)( m_pNewRadarA->value * 255 );

	if( s_spr[SPR_RADAR] && s_spr[SPR_RADAR]->spr )
	{
		// iradio.spr is only a 32x32 icon; the mobile build scales the radar
		// body to the computed radius, but SPR_DrawAdditive cannot scale, so
		// keep the plain call and let the frame carry the size.
		SPR_Set( s_spr[SPR_RADAR]->spr, r, g, b );
		SPR_DrawAdditive( m_iCenterX - m_iRadius, m_iCenterY - m_iRadius, 0,
			&s_spr[SPR_RADAR]->rect );
	}
	else
	{
		// no sprite: outline in the configured colour so the position and
		// size are still verifiable on screen
		DrawRadarFrame( m_iCenterX, m_iCenterY, m_iRadius, r, g, b, a );
	}

	// ---- circular mask
	DrawRadarMask( m_iCenterX, m_iCenterY, m_iRadius );

	// ---- player blips
	int iTeam = g_PlayerExtraInfo[ m_iPlayerNum ].teamnumber;
	for( int i = 1; i < 32; i++ )
		DrawPlayerBlip( m_iCenterX, m_iCenterY, m_iRadius, i, iTeam, flTime );

	// ---- warning frame while alive with a bomb in hand
	DrawWarningFrame( m_iCenterX, m_iCenterY, m_iRadius, flTime );

	return 1;
}

