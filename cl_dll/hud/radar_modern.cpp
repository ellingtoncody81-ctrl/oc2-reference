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
// Overview handedness override. The mobile build gets this from the
// ROTATED field of the .txt; third-party overviews on PC often leave that
// bit wrong, so it stays user adjustable.
//   bit 0 - transpose the world X/Y axes (the ROTATED branch of CalcPoint)
//   bit 1 - negate the tile Y axis
static cvar_t *s_cl_radar_orient     = NULL;

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
	// how much of the map the radar window covers (1.0 = whole map width,
	// smaller = see more of it)
	s_cl_radar_view       = CVAR_CREATE( "cl_radar_view", "1", FCVAR_ARCHIVE );
	s_cl_radar_orient     = CVAR_CREATE( "cl_radar_orient", "0", FCVAR_ARCHIVE );

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
	m_iOverviewTilesX = 0;
	m_iOverviewTilesY = 0;
	m_flPixelsPerTile = 0.0f;
	m_flTileA = 1.0f;
	m_flTileB = 0.0f;
	m_bTileRotated = false;
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
UpdateRadarTransform

The one place that turns the overview description into a screen scale.

CHudSpectator::DrawOverviewLayer shows how GoldSrc lays the overview image
over the world: the image is 2*4096/zoom world units wide and
2*4096/(zoom*aspect) tall. The tiled sprite is a MAPSPRITE_SIZE (128 px)
grid, so the whole image is (tilesX*128) x (tilesY*128) overview pixels and

    overview pixels per world unit = (tilesX * 128) * zoom / 8192
    world units per overview pixel = 8192 / ((tilesX * 128) * zoom)

The radar shows a *window* of the map, not the whole map: the window is
2*radius*view pixels wide, centred on the player, which gives

    screen pixels per world unit = 2 * radius * view * zoom / 8192

Note how tilesX cancels out - the scale follows from zoom alone, so the
same numbers work for any overview resolution.
========================
*/
void CHudRadarModern::UpdateRadarTransform( int cx, int cy, int radius )
{
	m_iOverviewTilesX = 0;
	m_iOverviewTilesY = 0;
	m_flPixelsPerTile = 0.0f;

	// Grid basis: cos(theta), sin(theta) with theta == 0. The mobile build
	// drives theta from the player's pitch so the radar squashes when you
	// look up or down; at theta 0 the mapping is a plain north-up scale,
	// which is what we use here.
	m_flTileA = 1.0f;
	m_flTileB = 0.0f;

	int iOrient = s_cl_radar_orient ? (int)s_cl_radar_orient->value : 0;
	m_bTileRotated = ( m_OverviewData.rotated != 0 ) || ( ( iOrient & 1 ) != 0 );

	model_t *pSprite = gHUD.m_Spectator.GetMapSprite();
	if( !pSprite || pSprite->numframes <= 0 )
		return;

	int i = (int)sqrt( (float)( pSprite->numframes / (4 * 3) ) );
	if( i < 1 )
		i = 1;

	m_iOverviewTilesX = i * 4;
	m_iOverviewTilesY = i * 3;

	float flView = s_flRadarViewFraction;
	if( flView <= 0.0f || flView > 1.0f )
		flView = 1.0f;

	// The window is 2*radius*view pixels wide and spans the whole map
	// width, i.e. m_iOverviewTilesX tiles, so one tile is that divided by
	// the column count. This is the only free scale parameter - the world
	// to tile mapping itself is fixed by the overview description.
	m_flPixelsPerTile = ( (float)radius * 2.0f * flView ) / (float)m_iOverviewTilesX;
}

/*
========================
/*
========================
WorldToTile

World position -> fractional tile index in the overview image
(128 overview pixels per tile, so tile = pixel / 128).

    s  = zoom / 1024                       (DAT_000c5050 == 0.0009765625)
    tx = 4 + s * (world.x - origin.x)
    ty =     s * (world.y - origin.y)

Read off the Thumb instruction stream of Draw, the non-rotated path
(the one de_dust2 takes, offsets +556..+640):

    s4   = *(gHUD + 0xc9c)                ; zoom
    d2   = s4 * 1/1024
    d3   = -1.0 + d2*(origin.y - world.x)
    d10  =  3.0 + d2*(origin.y - world.y)
    d13  = -d3 = 1.0 - d2*(origin.y - world.x)

and that reproduces the measured overview pixel exactly:

    u = 1024/2 + (wx - ox) * zoom/8        ; variance 15565 on real map
    v =         (wy - oy) * zoom/8

which is the same as tx*128 and ty*128 for the formula above. The other
candidate - the crossed-axes form that Ghidra's C output shows - lands on
the flat background instead (variance 533), because that C output is the
*ROTATED* branch, reached only via the `beq` at +554.

The axes are NOT crossed: world X picks the tile column, world Y picks the
tile row, each against its own origin component.
========================
*/
void CHudRadarModern::WorldToTile( const Vector &vecWorld, float &tx, float &ty ) const
{
	float flZoom = m_OverviewData.zoom > 0.0f ? m_OverviewData.zoom : 1.0f;
	float s = flZoom * ( 1.0f / 1024.0f );

	if( !m_bTileRotated )
	{
		tx = 4.0f + s * ( vecWorld[0] - m_OverviewData.origin[0] );
		ty =        s * ( vecWorld[1] - m_OverviewData.origin[1] );
	}
	else
	{
		// ROTATED branch, read off the +736 block. Not measured - none of
		// the maps we ship set ROTATED - so keep the description's own
		// flag rather than inventing a different transpose.
		tx = 3.0f + s * ( m_OverviewData.origin[1] - vecWorld[0] );
		ty = 4.0f + s * ( m_OverviewData.origin[0] - vecWorld[1] );
	}

	// user overrides on top, for overviews whose description lies
	int iOrient = s_cl_radar_orient ? (int)s_cl_radar_orient->value : 0;
	if( iOrient & 1 )
	{
		float t = tx;
		tx = ty;
		ty = t;
	}
	if( iOrient & 2 )
		ty = -ty;
}

/*
========================
CalcPoint

Tile index -> radar pixel, using the grid basis (cos t, sin t) and the same
scale the background tiles use, minus the local player's own tile so the
player always sits exactly on the radar centre. That is step 2 of the
reverse engineered CalcPoint.

Returns true when the point fell outside the circle and was clipped onto it.
========================
*/
bool CHudRadarModern::CalcPoint( const Vector &vecWorld, int &x, int &y, int &radius )
{
	radius = m_iRadius;

	if( m_flPixelsPerTile <= 0.0f )
	{
		// no overview: fall back to "point is at the centre"
		x = m_iCenterX;
		y = m_iCenterY;
		return false;
	}

	float tx, ty, selfX, selfY;
	WorldToTile( vecWorld, tx, ty );
	WorldToTile( gHUD.m_vecOrigin, selfX, selfY );

	float flDX = ( tx - selfX ) * m_flPixelsPerTile;
	float flDY = ( ty - selfY ) * m_flPixelsPerTile;

	// grid basis: neutral is (1, 0), i.e. plain north-up
	float fx = (float)m_iCenterX + ( flDX * m_flTileA - flDY * m_flTileB );
	float fy = (float)m_iCenterY + ( flDX * m_flTileB + flDY * m_flTileA );

	x = (int)fx;
	y = (int)fy;

	float flCX = fx - (float)m_iCenterX;
	float flCY = fy - (float)m_iCenterY;

	return ( flCX * flCX + flCY * flCY ) > (float)( m_iRadius * m_iRadius );
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

		// A rotated overview is stored turned 90 degrees; the CS:GO radar
		// path below assumes the standard north-up image, so say so instead
		// of silently drawing the map sideways.
		if( m_OverviewData.rotated )
		{
			gEngfuncs.Con_Printf( "radar: overview '%s' is ROTATED, "
				"drawn north-up anyway (unsupported)\n", m_szMapName );
		}
	}
	else
	{
		m_bOverviewValid = false;
	}
}

/*
========================
DrawRadarDisc

Flat fill inside the circle. Everything the map does not cover - and the
outside of the overview image - shows this instead of the game behind the
HUD. The mobile build gets that area from its blurred background capture;
this keeps the shape correct when blurring is off.

The white texture has to be bound explicitly: the tri API keeps whatever
the previous HUD element left bound, and sampling that turns the disc
black.
========================
*/
void CHudRadarModern::DrawRadarDisc( int cx, int cy, int radius )
{
	int i;
	const int kSegs = 48;
	float flStep = ( 2.0f * 3.14159265358979f ) / kSegs;

	if( gHUD.m_WhiteTex )
	{
		gRenderAPI.GL_SelectTexture( 0 );
		gRenderAPI.GL_Bind( 0, gHUD.m_WhiteTex );
	}

	gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	// the mobile radar sits on a dark navy plate, not a light one
	gEngfuncs.pTriAPI->Color4ub( 22, 28, 48, 210 );
	gEngfuncs.pTriAPI->Begin( TRI_TRIANGLES );

	float flPrevX = cx + (float)radius;
	float flPrevY = cy;

	for( i = 1; i <= kSegs; i++ )
	{
		float flAng = flStep * i;
		float flX = cx + cosf( flAng ) * (float)radius;
		float flY = cy + sinf( flAng ) * (float)radius;

		gEngfuncs.pTriAPI->TexCoord2f( 0.5f, 0.5f );
		gEngfuncs.pTriAPI->Vertex3f( cx, cy, 0 );
		gEngfuncs.pTriAPI->TexCoord2f( 0.5f, 0.5f );
		gEngfuncs.pTriAPI->Vertex3f( flPrevX, flPrevY, 0 );
		gEngfuncs.pTriAPI->TexCoord2f( 0.5f, 0.5f );
		gEngfuncs.pTriAPI->Vertex3f( flX, flY, 0 );

		flPrevX = flX;
		flPrevY = flY;
	}

	gEngfuncs.pTriAPI->End();
}

/*
========================
ClipPolyToCircle

Sutherland-Hodgman against the radar circle approximated by a convex
polygon. The overview tiles are square, so without this the map spills
outside the round window - the mobile build bounds it to the circle.

The texture coordinate travels along as a per-vertex attribute: the
polygon is rotated before it is clipped, and the clip points land between
two corners, so their texcoords have to be interpolated here rather than
recovered from the final screen position afterwards.
========================
*/
#define RADAR_CLIP_SEGS  32
#define RADAR_CLIP_MAX   64

static int ClipPolyToCircle( const float *inX, const float *inY,
                             const float *inU, const float *inV, int n,
                             float *outX, float *outY,
                             float *outU, float *outV,
                             float cx, float cy, float radius )
{
	float tX[RADAR_CLIP_MAX], tY[RADAR_CLIP_MAX];
	float tU[RADAR_CLIP_MAX], tV[RADAR_CLIP_MAX];
	int cur = n;
	int e, i, m;

	if( cur > RADAR_CLIP_MAX )
		cur = RADAR_CLIP_MAX;

	for( i = 0; i < cur; i++ )
	{
		tX[i] = inX[i];
		tY[i] = inY[i];
		tU[i] = inU[i];
		tV[i] = inV[i];
	}

	for( e = 0; e < RADAR_CLIP_SEGS && cur > 0; e++ )
	{
		float a0 = ( 2.0f * 3.14159265358979f ) * (float)e / (float)RADAR_CLIP_SEGS;
		float a1 = ( 2.0f * 3.14159265358979f ) * (float)( e + 1 ) / (float)RADAR_CLIP_SEGS;
		float ax = cx + cosf( a0 ) * radius;
		float ay = cy + sinf( a0 ) * radius;
		float bx = cx + cosf( a1 ) * radius;
		float by = cy + sinf( a1 ) * radius;

		float ex = bx - ax, ey = by - ay;
		float len = sqrtf( ex * ex + ey * ey );
		float nx, ny;

		if( len < 0.0001f )
			continue;

		nx = -ey / len;
		ny =  ex / len;		// inward normal for a CCW polygon

		m = 0;
		for( i = 0; i < cur; i++ )
		{
			int j = ( i + 1 ) % cur;
			float d0 = ( tX[i] - ax ) * nx + ( tY[i] - ay ) * ny;
			float d1 = ( tX[j] - ax ) * nx + ( tY[j] - ay ) * ny;

			if( d0 >= 0.0f )
			{
				outX[m] = tX[i];
				outY[m] = tY[i];
				outU[m] = tU[i];
				outV[m] = tV[i];
				m++;
			}

			if( ( d0 >= 0.0f ) != ( d1 >= 0.0f ) )
			{
				float t = d0 / ( d0 - d1 );
				outX[m] = tX[i] + ( tX[j] - tX[i] ) * t;
				outY[m] = tY[i] + ( tY[j] - tY[i] ) * t;
				outU[m] = tU[i] + ( tU[j] - tU[i] ) * t;
				outV[m] = tV[i] + ( tV[j] - tV[i] ) * t;
				m++;
			}
		}

		for( i = 0; i < m; i++ )
		{
			tX[i] = outX[i];
			tY[i] = outY[i];
			tU[i] = outU[i];
			tV[i] = outV[i];
		}
		cur = m;
	}

	for( i = 0; i < cur; i++ )
	{
		outX[i] = tX[i];
		outY[i] = tY[i];
		outU[i] = tU[i];
		outV[i] = tV[i];
	}

	return cur;
}

/*
========================
DrawOverviewLayer

The tiled overview blit, but anchored on the *player* instead of the radar
rectangle, so only the part of the map around the player is on screen and
it scrolls as the player walks. This is the CS:GO behaviour the mobile
build reproduces.

Tile grid and frame order are identical to CHudSpectator::DrawOverviewLayer
(n = sqrt(numframes/12), then n*4 columns by n*3 rows, frames row major);
only the anchor differs.

The image anchor is always "one player-offset back from the centre" so the
player lands exactly on the radar centre. Which way the image itself faces
is a property of the bitmap, not of the anchor, so cl_radar_flip mirrors
the tile grid rather than moving the anchor.
========================
*/
/*
========================
DrawOverviewLayer

The tiled overview blit, anchored in TILE space rather than pixels.

Step 2 of the reverse engineered CalcPoint says the grid origin is the
player's own tile index pulled back from the radar centre, so:

    gridOrigin = radarCentre - selfTile * pixelsPerTile

and every tile is then drawn at gridOrigin + tile * pixelsPerTile. Because
selfTile comes from the same WorldToTile() the blips use, the background
and the player dots cannot drift apart.

The grid basis is (cos theta, sin theta); at theta 0 - what the mobile
build uses when the player is not looking up or down - that is (1, 0), a
plain north-up scale, so no rotation is applied here. Set m_flTileA/B to
something else to skew it.

Tile grid and frame order match CHudSpectator::DrawOverviewLayer
(n = sqrt(numframes/12), then n*4 columns by n*3 rows, frames row major).
========================
*/
void CHudRadarModern::DrawOverviewLayer( int cx, int cy, int radius )
{
	model_t *pSprite = gHUD.m_Spectator.GetMapSprite();

	if( !pSprite || pSprite->numframes <= 0 || m_flPixelsPerTile <= 0.0f )
		return;

	int xTiles = m_iOverviewTilesX;
	int yTiles = m_iOverviewTilesY;
	if( xTiles < 1 || yTiles < 1 )
		return;

	float flTile = m_flPixelsPerTile;
	float flR = (float)radius;
	float flCX = (float)cx;
	float flCY = (float)cy;

	// grid origin: the player's own tile, pulled back from the centre
	float selfX, selfY;
	WorldToTile( gHUD.m_vecOrigin, selfX, selfY );

	float flGridX = flCX - selfX * flTile;
	float flGridY = flCY - selfY * flTile;

	// tile index range that covers the radar square, clamped to the image
	// so a player near a map edge simply runs out of map
	int firstX = (int)floorf( ( flCX - flR - flGridX ) / flTile );
	int firstY = (int)floorf( ( flCY - flR - flGridY ) / flTile );
	int lastX  = (int)ceilf ( ( flCX + flR - flGridX ) / flTile );
	int lastY  = (int)ceilf ( ( flCY + flR - flGridY ) / flTile );

	if( firstX < 0 ) firstX = 0;
	if( firstY < 0 ) firstY = 0;
	if( lastX > xTiles ) lastX = xTiles;
	if( lastY > yTiles ) lastY = yTiles;

	gEngfuncs.pTriAPI->RenderMode( kRenderTransTexture );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	// overviews ship as greyscale bmp; the mobile radar tints them cool so
	// the map reads as blue-on-navy rather than grey-on-navy
	gEngfuncs.pTriAPI->Color4ub( 150, 180, 215, 255 );

	for( int ty = firstY; ty < lastY; ty++ )
	{
		for( int tx = firstX; tx < lastX; tx++ )
		{
			int frame = ty * xTiles + tx;

			if( frame < 0 || frame >= pSprite->numframes )
				continue;

			// tile corners in screen space, through the grid basis
			float bx = flGridX + (float)tx * flTile;
			float by = flGridY + (float)ty * flTile;

			float cX[RADAR_CLIP_MAX], cY[RADAR_CLIP_MAX];
			float cU[RADAR_CLIP_MAX], cV[RADAR_CLIP_MAX];

			const float ux[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
			const float uy[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

			for( int k = 0; k < 4; k++ )
			{
				float lx = bx + ux[k] * flTile;
				float ly = by + uy[k] * flTile;

				// basis applied about the radar centre
				float dx = lx - flCX;
				float dy = ly - flCY;

				cX[k] = flCX + ( dx * m_flTileA - dy * m_flTileB );
				cY[k] = flCY + ( dx * m_flTileB + dy * m_flTileA );

				cU[k] = ux[k];
				cV[k] = uy[k];
			}

			// clip to the round window; the texcoord travels along as a
			// per-vertex attribute so the cut corners keep the right pixel
			int n = ClipPolyToCircle( cX, cY, cU, cV, 4, cX, cY, cU, cV,
				flCX, flCY, flR );
			if( n < 3 )
				continue;

			gEngfuncs.pTriAPI->SpriteTexture( pSprite, frame );
			gEngfuncs.pTriAPI->Begin( TRI_TRIANGLES );

			for( int i = 2; i < n; i++ )
			{
				const int idx[3] = { 0, i - 1, i };

				for( int k = 0; k < 3; k++ )
				{
					int v = idx[k];
					gEngfuncs.pTriAPI->TexCoord2f( cU[v], cV[v] );
					gEngfuncs.pTriAPI->Vertex3f( cX[v], cY[v], 0 );
				}
			}

			gEngfuncs.pTriAPI->End();
		}
	}

	gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
}

/*
========================
DrawSelfMarker

The local player sits at the radar centre. The mobile build shows it even
though every other blip goes through DrawPlayerBlip, which skips the local
player - otherwise the centre of the radar is just an empty map pixel and
there is nothing to tell the viewer which way they are facing.
========================
*/
void CHudRadarModern::DrawSelfMarker( int cx, int cy, int radius )
{
	CClientSprite *pSprite = s_spr[SPR_PLAYER];

	// an arrow-ish marker: a small triangle in the facing direction, so it
	// reads the same as the CS:GO one even when no sprite is available
	int iTeam = g_PlayerExtraInfo[ m_iPlayerNum ].teamnumber;
	int r = ( iTeam == 1 ) ? 0 : 255;
	int g = ( iTeam == 1 ) ? 255 : 0;
	int b = 0;
	int iSize = max( 4, radius / 14 );

	if( pSprite && pSprite->spr )
	{
		SPR_Set( pSprite->spr, r, g, b );
		SPR_DrawAdditive( cx - iSize, cy - iSize, 0, &pSprite->rect );
		return;
	}

	float flAng = DEG2RAD( gHUD.m_vecAngles[1] ) + 1.5707963f;
	float flTipX = cx + cosf( flAng ) * (float)iSize;
	float flTipY = cy + sinf( flAng ) * (float)iSize;
	float flBX = cx + cosf( flAng + 2.0944f ) * (float)iSize;
	float flBY = cy + sinf( flAng + 2.0944f ) * (float)iSize;
	float flCX = cx + cosf( flAng - 2.0944f ) * (float)iSize;
	float flCY = cy + sinf( flAng - 2.0944f ) * (float)iSize;

	gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( r, g, b, 255 );
	gEngfuncs.pTriAPI->Begin( TRI_TRIANGLES );
	gEngfuncs.pTriAPI->Vertex3f( flTipX, flTipY, 0 );
	gEngfuncs.pTriAPI->Vertex3f( flBX, flBY, 0 );
	gEngfuncs.pTriAPI->Vertex3f( flCX, flCY, 0 );
	gEngfuncs.pTriAPI->End();
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

	// ---- how much of the map the window covers, and the resulting scale
	if( s_cl_radar_view )
	{
		s_flRadarViewFraction = s_cl_radar_view->value;
		if( s_flRadarViewFraction <= 0.0f || s_flRadarViewFraction > 1.0f )
			s_flRadarViewFraction = 1.0f;
	}

	// everything below shares this one transform
	UpdateRadarTransform( m_iCenterX, m_iCenterY, m_iRadius );

	// flat disc first, so map-less areas read as background instead of
	// showing the game through the HUD
	DrawRadarDisc( m_iCenterX, m_iCenterY, m_iRadius );

	// ---- map tiles, anchored on the player
	if( m_bOverviewValid )
		DrawOverviewLayer( m_iCenterX, m_iCenterY, m_iRadius );

	// temporary on-screen diagnostics (removed once the radar is confirmed)
	if( s_cl_radar_debug )
	{
		char szDbg[256];
		int nFrames = gHUD.m_Spectator.GetMapSprite()
			? gHUD.m_Spectator.GetMapSprite()->numframes : -1;
		float selfX, selfY;
		WorldToTile( gHUD.m_vecOrigin, selfX, selfY );
		snprintf( szDbg, sizeof( szDbg ),
			"radar: valid=%d frames=%d tiles=%dx%d zoom=%g org=%g,%g "
			"tile=(%.2f,%.2f) px/tile=%g view=%g orient=%d rot=%d r=%d c=%d,%d",
			m_bOverviewValid, nFrames,
			m_iOverviewTilesX, m_iOverviewTilesY,
			m_OverviewData.zoom,
			m_OverviewData.origin[0], m_OverviewData.origin[1],
			selfX, selfY, m_flPixelsPerTile, s_flRadarViewFraction,
			s_cl_radar_orient ? (int)s_cl_radar_orient->value : 0,
			m_bTileRotated ? 1 : 0,
			m_iRadius, m_iCenterX, m_iCenterY );
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

	// ---- the local player, pinned to the centre
	DrawSelfMarker( m_iCenterX, m_iCenterY, m_iRadius );

	// ---- player blips
	int iTeam = g_PlayerExtraInfo[ m_iPlayerNum ].teamnumber;
	for( int i = 1; i < 32; i++ )
		DrawPlayerBlip( m_iCenterX, m_iCenterY, m_iRadius, i, iTeam, flTime );

	// ---- warning frame while alive with a bomb in hand
	DrawWarningFrame( m_iCenterX, m_iCenterY, m_iRadius, flTime );

	return 1;
}

