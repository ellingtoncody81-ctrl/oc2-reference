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
#include "oc2_hostage.h"
#include "hud_spectator.h"	// overviewInfo_t / CHudSpectator overview parser
#include "com_model.h"		// model_t, used for the overview map sprite
#include "r_studioint.h"		// player userid and current network state
#include "cl_util.h"
#include "csprite.h"
#include "draw_util.h"
#include "triangleapi.h"
#include "event_api.h"		// EV_PlayerTrace and friends
#include "pm_defs.h"			// PM_STUDIO_BOX / PM_GLASS_IGNORE / pmtrace_t
#include "radar_int.h"
#include "hud/radar_modern.h"
#include "view_ads.h"		// V_CurrentADSWorldFOV: the cone must follow the scope
#include "radar_fullmap_layout.h"
#include "oc2_observer.h"
#include "spectator_layout.h"
#include "oc2_spectator_data.h"
#include "match_ui.h"

#include <math.h>
#include <cmath>
#include <ctype.h>

static int RadarTeamOf( int i );
static void RadarDumpCommand() { gHUD.m_RadarModern.DumpState(); }

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// filled in by HUD_RadarInterface() in cdll_int.cpp
extern radar_engfuncs_t gRadarAPI;
extern int g_iRadarAPIVersion;
extern engine_studio_api_t IEngineStudio;
#include "oc2_player_snapshot.h"

#define RADAR_MASK_SEGMENTS  63
#define BLIP_SIZE            20
#define RADAR_SCALE          32.0f	// same as CHudRadar::WorldToRadar

/*
How much of the map the radar window covers.

The mobile build lays the overview out at 80% of the screen: the constants
recovered from Draw are 819.2 and -614.4, i.e. 1024x768 * 0.8. Its radar
circle is cl_newradar_size of the screen height, so in that same design space
the diameter is 2 * 0.175 * 768 = 268.8 against an 819.2 wide overview:

    268.8 / 819.2 = 0.328125

so the window is a bit under a third of the map, not the whole thing.
Showing the full 1024 px in a 378 px circle pinned every tile inside the
disc, which is why the map appeared frozen while walking.
*/
#define RADAR_MAP_FRACTION   0.328125f

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
// Box-blur radius applied to the captured screen block. 0 or less picks a
// fraction of the capture instead: the block is sized from the radar, so a
// fixed pixel count would mean something different at every screen size.
static cvar_t *s_cl_radar_blur_radius = NULL;
// how opaque the blurred backdrop sits under the map. 0.59 matches the
// 0x96 the mobile build uses.
static cvar_t *s_cl_radar_blur_alpha = NULL;
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
// Residual tile offset, in tile units, applied after the mapping. The
// world -> tile formula itself is measured against de_dust2.bmp (variance
// 15565 on real map detail vs 0 for every alternative), but a residual
// horizontal shift of roughly three tiles has not been fully pinned down
// from screenshots alone. Exposing it as a cvar means it can be dialled in
// at runtime instead of guessing and rebuilding.
static cvar_t *s_cl_radar_shiftx      = NULL;
static cvar_t *s_cl_radar_shifty      = NULL;
// Lock the radar to the player's facing (CS:GO behaviour). 0 = plain
// north-up, which is handy for comparing against the overview bitmap.
static cvar_t *s_cl_radar_rotate      = NULL;
// Gate enemy blips behind a real line-of-sight test. 0 restores the old
// behaviour (every live enemy entity is drawn wherever it is).
static cvar_t *s_cl_radar_enemy_los    = NULL;
// one-shot "no map bounds" complaint
static bool    s_bWarnedNoBounds      = false;

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
	ResetRound();
	m_flRadarVisLast    = 0;
	m_iPlayerNum        = 0;
	m_bFullMap          = false;
	m_flLayoutDrawTime  = -1.0f;
	m_iSelfDrawX = m_iSelfDrawY = -1;
	m_iObjectiveCount   = 0;
	m_szObjectiveMap[0] = 0;
	m_bObjectivesLoaded = false;
	m_pObjectiveEntities = NULL;

	// defaults lifted verbatim from the mobile build's CHudRadarModern ctor
	s_cl_newradar_size    = CVAR_CREATE( "cl_newradar_size", "0.175", FCVAR_ARCHIVE );
	s_cl_newradar_r       = CVAR_CREATE( "cl_newradar_r",    "1",     FCVAR_ARCHIVE );
	s_cl_newradar_g       = CVAR_CREATE( "cl_newradar_g",    "1",     FCVAR_ARCHIVE );
	s_cl_newradar_b       = CVAR_CREATE( "cl_newradar_b",    "1",     FCVAR_ARCHIVE );
	s_cl_newradar_a       = CVAR_CREATE( "cl_newradar_a",    "0.75",  FCVAR_ARCHIVE );
	s_cl_radar_pos_x      = CVAR_CREATE( "cl_radar_pos_x",   "0",     FCVAR_ARCHIVE );
	s_cl_radar_pos_y      = CVAR_CREATE( "cl_radar_pos_y",   "0",     FCVAR_ARCHIVE );
	// The blurred background needs a working engine-side capture. It used to
	// default off because the capture was broken; now that the chain works
	// it defaults on, and falls back to the flat plate on its own whenever
	// the engine has no texture to give (see DrawBlurredBackground).
	s_cl_blur_radar_bg    = CVAR_CREATE( "cl_blur_radar_bg", "0",     FCVAR_ARCHIVE );
	s_cl_blur_update_rate = CVAR_CREATE( "cl_blur_update_rate", "0.5", FCVAR_ARCHIVE );
	s_cl_radar_blur_radius = CVAR_CREATE( "cl_radar_blur_radius", "0", FCVAR_ARCHIVE );
	s_cl_radar_blur_alpha  = CVAR_CREATE( "cl_radar_blur_alpha", "0.59", FCVAR_ARCHIVE );
	s_cl_radar_modern     = CVAR_CREATE( "cl_radar_modern", "1", FCVAR_ARCHIVE );
	s_cl_radar_debug      = CVAR_CREATE( "cl_radar_debug", "0", FCVAR_ARCHIVE );
	// how much of the map the radar window covers (1.0 = whole map width,
	// smaller = see more of it)
	s_cl_radar_view       = CVAR_CREATE( "cl_radar_view", "1", FCVAR_ARCHIVE );
	s_cl_radar_orient     = CVAR_CREATE( "cl_radar_orient", "0", FCVAR_ARCHIVE );
	s_cl_radar_shiftx     = CVAR_CREATE( "cl_radar_shiftx", "0", FCVAR_ARCHIVE );
	s_cl_radar_shifty     = CVAR_CREATE( "cl_radar_shifty", "0", FCVAR_ARCHIVE );
	s_cl_radar_rotate     = CVAR_CREATE( "cl_radar_rotate", "1", FCVAR_ARCHIVE );
	s_cl_radar_enemy_los  = CVAR_CREATE( "cl_radar_enemy_los", "1", FCVAR_ARCHIVE );
	gEngfuncs.pfnAddCommand( "radar_dump", RadarDumpCommand );

	m_bRadarGating = false;
	memset( m_aRadarEnemyVisible, 0, sizeof( m_aRadarEnemyVisible ) );
	m_flRadarVisNext = 0.0f;

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
	m_bBoundsValid = false;
	m_flMapMinX = 0.0f;
	m_flMapMaxY = 0.0f;
	m_flTilesPerUnitX = 0.0f;
	m_flTilesPerUnitY = 0.0f;
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
		delete s_spr[i];
		s_spr[i] = new CClientSprite();
		// HUD_GetSpriteIndexByName returns -1 on failure, not 0. Testing the
		// value directly treated -1 as success, skipped the direct load below
		// and left every radar sprite empty - players, the bomb and hostages
		// all fell back to plain squares.
		if( index >= 0 )
		{
			s_spr[i]->SetSpriteByName( s_radarSpriteNames[i] );
			continue;
		}
		// These stock radar sprites ship as separate .spr files and are not
		// listed in hud.txt. Load them directly before falling back to shapes.
		char path[96];
		snprintf( path, sizeof( path ), "sprites/%s.spr", s_radarSpriteNames[i] );
		s_spr[i]->spr = SPR_Load( path );
		if( s_spr[i]->spr )
		{
			s_spr[i]->rect.left = s_spr[i]->rect.top = 0;
			s_spr[i]->rect.right = SPR_Width( s_spr[i]->spr, 0 );
			s_spr[i]->rect.bottom = SPR_Height( s_spr[i]->spr, 0 );
		}
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
	ResetRound();
}

void CHudRadarModern::Reset( void )
{
	m_flLastCapture     = 0;
	m_flLastBombPlanted = 0;
	// ResetHUD may be a mid-round fullupdate. Preserve deaths and C4 state.
	// A fullupdate can itself send a BombDrop sync, so do not mistake that
	// immediate message for the weapon box's periodic broadcast.
	if( m_bBombDropActive && m_iBombDropTeam == TEAM_TERRORIST )
		m_flBombLastDropMessage = gEngfuncs.GetClientTime();
	m_bBombDrawn = false;
}

void CHudRadarModern::ResetRound()
{
	m_bBombDropActive = false;
	m_bBombDropPositionKnown = false;
	m_iBombDropTeam = 0;
	m_flBombLastDropMessage = 0.0f;
	m_iBombDropMessages = 0;
	m_vecBombMessage = Vector( 0, 0, 0 );
	m_vecBombAccepted = Vector( 0, 0, 0 );
	m_vecBombDrawOrigin = Vector( 0, 0, 0 );
	m_bBombDrawn = false;
	m_flBombDrawTime = 0.0f;

	// InitHUD and HudRound are map/round boundaries; ResetHUD is not.
	m_flRadarVisNext = 0;
	m_flRadarVisLast = 0;
	m_iRadarTrackedLocalTeam = 0;
	memset( m_aRadarTrackedTeam, 0, sizeof( m_aRadarTrackedTeam ) );
	memset( m_aRadarEnemyVisible, 0, sizeof( m_aRadarEnemyVisible ) );
	memset( m_aRadarEnemySeen, 0, sizeof( m_aRadarEnemySeen ) );
	memset( m_aRadarEnemyConfirmed, 0, sizeof( m_aRadarEnemyConfirmed ) );
	memset( m_aRadarEnemyLastSeen, 0, sizeof( m_aRadarEnemyLastSeen ) );
	memset( m_aRadarTeammateUserid, 0, sizeof( m_aRadarTeammateUserid ) );
	memset( m_aRadarEnemyConfirm, 0, sizeof( m_aRadarEnemyConfirm ) );
	memset( m_aRadarEnemyHold, 0, sizeof( m_aRadarEnemyHold ) );
	memset( m_aRadarPlayerDeadAt, 0, sizeof( m_aRadarPlayerDeadAt ) );
	memset( m_aRadarHasConfirmedOrigin, 0, sizeof( m_aRadarHasConfirmedOrigin ) );
	memset( m_aRadarConfirmedLocalTeam, 0, sizeof( m_aRadarConfirmedLocalTeam ) );
	memset( m_aRadarConfirmedVictimTeam, 0, sizeof( m_aRadarConfirmedVictimTeam ) );
	for( int i = 0; i <= kRadarMaxPlayers; ++i )
	{
		m_aRadarEnemyLastOrigin[i] = Vector( 0, 0, 0 );
		m_aRadarConfirmedOrigin[i] = Vector( 0, 0, 0 );
		m_aRadarPlayerDeadOrigin[i] = Vector( 0, 0, 0 );
		m_aRadarPlayerDeadPrevOrigin[i] = Vector( 0, 0, 0 );
		m_aRadarPlayerDeadMoveStreak[i] = 0.0f;
	}
}

void CHudRadarModern::NoteBombDrop( int flag, const Vector &origin, float time )
{
	const int team = RadarTeamOf( gHUD.m_Scoreboard.m_iPlayerNum );
	// Flag 0 may be an unconditional fullupdate, including for CTs.
	// Only flag 2 certifies a server-side CT sighting.
	if( ( team == TEAM_CT && flag == 0 ) || ( flag == 2 && team != TEAM_CT ) )
		return;
	m_vecBombMessage = origin;
	m_bBombDrawn = false;
	if( flag == 1 )
	{
		m_bBombDropActive = false;
		m_bBombDropPositionKnown = false;
		m_iBombDropMessages = 0;
		return;
	}
	if( !m_bBombDropActive || m_iBombDropTeam != team )
	{
		m_bBombDropActive = true;
		m_bBombDropPositionKnown = false;
		m_iBombDropMessages = 1;
		m_iBombDropTeam = team;
	}
	else
	{
		++m_iBombDropMessages;
		// The server schedules BombThink one second after the throw. A rapid
		// sync/duplicate is not proof of a box broadcast. Wait for a later
		// periodic message rather than drawing the thrower's first position.
		if( time - m_flBombLastDropMessage >= 0.9f )
		{
			m_bBombDropPositionKnown = true;
			m_vecBombAccepted = origin;
		}
	}
	// Unlike the T throw notification, a CT drop message comes only from a
	// server-confirmed spotting of the actual weapon box.
	if( flag == 2 )
	{
		m_bBombDropPositionKnown = true;
		m_vecBombAccepted = origin;
	}
	m_flBombLastDropMessage = time;
}

void CHudRadarModern::NoteBombPickup()
{
	m_bBombDropActive = false;
	m_bBombDropPositionKnown = false;
	m_iBombDropTeam = 0;
	m_flBombLastDropMessage = 0.0f;
	m_iBombDropMessages = 0;
	m_bBombDrawn = false;
}

void CHudRadarModern::DumpState() const
{
	const int local = gHUD.m_Scoreboard.m_iPlayerNum;
	const extra_player_info_t &bomb = g_PlayerExtraInfo[33];
	int count = gEngfuncs.GetMaxClients();
	if( count > kRadarMaxPlayers ) count = kRadarMaxPlayers;
	gEngfuncs.Con_Printf( "radar_dump: time=%.6f local=%d team=%d los_cvar=%.2f gating=%d sweep_time=%.6f enabled=%d hud_dead=%d hide=%d\n",
		gEngfuncs.GetClientTime(), local, RadarTeamOf( local ),
		s_cl_radar_enemy_los ? s_cl_radar_enemy_los->value : 0.0f,
		int( m_bRadarGating ), m_flRadarVisLast, int( IsEnabled() ),
		int( gHUD.m_fPlayerDead ), gHUD.m_iHideHUDDisplay );
	gEngfuncs.Con_Printf( "radar_layout: full=%d x=%d y=%d size=%d tiles=%d,%d px_per_tile=%.3f draw_time=%.3f scoreboard=%d observer=%d self_x=%d self_y=%d\n",
		int(m_bFullMap),m_iCenterX-m_iRadius,m_iCenterY-m_iRadius,m_iRadius*2,
		m_iOverviewTilesX,m_iOverviewTilesY,m_flPixelsPerTile,m_flLayoutDrawTime,
		int(gHUD.m_Scoreboard.ShouldDrawScoreboard()),g_iUser1,m_iSelfDrawX,m_iSelfDrawY );
	for( int i = 1; i <= count; ++i )
	{
		const cl_entity_t *entity=gEngfuncs.GetEntityByIndex(i);
		gEngfuncs.Con_Printf("radar_snapshot: slot=%d current=%d message=%d last_seen=%.6f ghost_age=%.6f entity_origin=%.1f,%.1f,%.1f\n",
			i,int(OC2_CurrentPlayerSnapshot(entity)),entity?entity->curstate.messagenum:0,
			m_aRadarEnemyLastSeen[i],gEngfuncs.GetClientTime()-m_aRadarEnemyLastSeen[i],
			entity?entity->origin.x:0,entity?entity->origin.y:0,entity?entity->origin.z:0);
		gEngfuncs.Con_Printf( "radar_player: slot=%d team=%d visible=%d confirmed=%d confirm=%.3f hold=%.3f dead=%d dead_at=%.3f has_last=%d has_confirmed_last=%d last=%.1f,%.1f,%.1f dead_origin=%.1f,%.1f,%.1f\n",
			i, RadarTeamOf( i ), int( m_aRadarEnemyVisible[i] ),
			int( m_aRadarEnemyConfirmed[i] ), m_aRadarEnemyConfirm[i],
			m_aRadarEnemyHold[i], int( IsPlayerDead( i ) ), m_aRadarPlayerDeadAt[i],
			int( m_aRadarEnemySeen[i] ), int( m_aRadarHasConfirmedOrigin[i] ),
			m_aRadarEnemyLastOrigin[i].x, m_aRadarEnemyLastOrigin[i].y,
			m_aRadarEnemyLastOrigin[i].z, m_aRadarPlayerDeadOrigin[i].x,
			m_aRadarPlayerDeadOrigin[i].y, m_aRadarPlayerDeadOrigin[i].z );
	}
	gEngfuncs.Con_Printf( "radar_bomb: flag=%d dead=%d flashes=%d drop_active=%d position_known=%d messages=%d message_time=%.6f message=%.1f,%.1f,%.1f accepted=%.1f,%.1f,%.1f last_drawn=%d draw_time=%.6f draw_origin=%.1f,%.1f,%.1f\n",
		bomb.playerclass, int( bomb.dead ), bomb.radarflashes, int( m_bBombDropActive ),
		int( m_bBombDropPositionKnown ), m_iBombDropMessages, m_flBombLastDropMessage,
		m_vecBombMessage.x, m_vecBombMessage.y, m_vecBombMessage.z,
		m_vecBombAccepted.x, m_vecBombAccepted.y, m_vecBombAccepted.z,
		int( m_bBombDrawn ), m_flBombDrawTime, m_vecBombDrawOrigin.x,
		m_vecBombDrawOrigin.y, m_vecBombDrawOrigin.z );
	for( int i = 0; i < m_iObjectiveCount; ++i )
	{
		const Objective &site = m_Objectives[i];
		if( site.bomb ) continue;
		float x[64], y[64];
		const int count = ProjectRescueRegion( site.boundsMin, site.boundsMax, x, y );
		gEngfuncs.Con_Printf( "radar_region: slot=%d label=%c exact=%d min=%.1f,%.1f,%.1f max=%.1f,%.1f,%.1f vertices=%d\n",
			i, site.label ? site.label : '-', int( site.exactBounds ),
			site.boundsMin.x, site.boundsMin.y, site.boundsMin.z,
			site.boundsMax.x, site.boundsMax.y, site.boundsMax.z, count );
	}
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

The one place that turns the loaded map into an overview mapping.

Overview bitmaps are rendered by the BSP compiler from the world model's
bounding box, stretched to fill the image - NOT fitted preserving aspect and
NOT described by the overview .txt. For de_dust2 the world model is

    mins (-2816, -1664, -320)   maxs (2048, 3936, 640)
    X span 4864, Y span 5600   ->   1024 x 768 image pixels

so the two axes need *different* pixel-per-unit scales (0.2105 and 0.1371),
the image's +u runs with world +X and its +v runs against world +Y. Anything
derived from the .txt zoom/origin is off: those are the CS spectator framing
convention, which lays the bitmap over a fixed 8192/zoom world square
regardless of what the bitmap actually shows, and CHudSpectator's own
DrawOverviewLayer uses it. It puts the whole map somewhere else, and the error
grows as the player walks - which is exactly the "map doesn't follow me" bug.

The window itself: a circle of `radius` screen pixels shows
2*radius/pxPerTile tiles across, so one tile is 2*radius*view/tilesX pixels.
========================
*/
void CHudRadarModern::UpdateRadarTransform( int cx, int cy, int radius )
{
	m_iOverviewTilesX = 0;
	m_iOverviewTilesY = 0;
	m_flPixelsPerTile = 0.0f;
	m_bBoundsValid = false;

	// Overview handedness. The mobile build reads this one flag for BOTH the
	// tile axes (WorldToTile) and the rotation angle below, so it has to be
	// settled before the basis is computed.
	int iOrient = s_cl_radar_orient ? (int)s_cl_radar_orient->value : 0;
	m_bTileRotated = ( m_OverviewData.rotated != 0 ) || ( ( iOrient & 1 ) != 0 );

	/*
	========================
	Grid basis

	========================
	The angle is NOT the bare view yaw. CalcPoint builds it like this
	(0x4c52..0x4c82 of CHudRadarModern::CalcPoint):

	    0x4c52  ldrb  r0,[r11,#0x130]   ; the overview's rotated flag
	    0x4c56  vldr  s2,[r9,#0x28]    ; gHUD view yaw
	    0x4c60  vadd  s0,s2,s0         ; + 90
	    0x4c6c  vmul  d0,d0,d1         ; * DEG2RAD, 0.0174532925
	    0x4c74  beq  0x4c86            ; skip the next two when NOT rotated
	    0x4c7e  vadd  d0,d0,d1         ; - pi/2, 1.5707963268
	    0x4c82  vcvt  s28,d0

	so a = yaw + 90deg for a plain overview and a = yaw for a rotated one.
	The basis keeps the same (sin a, -cos a) shape - only the angle was wrong.

	That 90 degrees is the whole "turning feels wrong" bug. Checked by
	running the transform rather than by eye: take a world point 100 units
	north of the player and push its tile delta (-dy, -dx) through the basis.
	With a = yaw it lands on (0,+100) at yaw 0 and (-100,0) at yaw 90 - the
	map is simply rotated a quarter turn off, so walking forward slides the
	map sideways. With a = yaw + 90 it lands on (-100,0) at yaw 0 and
	(0,-100) at yaw 90, i.e. the point you are facing sits straight above the
	player at every yaw.

	cl_radar_rotate 0 keeps the raw tile orientation, for comparing the
	drawing against the overview bitmap directly.
	========================
	*/
	float flAngle = DEG2RAD( gHUD.m_vecAngles[1] );
	if( !m_bTileRotated )
		flAngle += (float)( M_PI * 0.5 );

	if( s_cl_radar_rotate && s_cl_radar_rotate->value == 0.0f )
	{
		m_flTileA = 1.0f;
		m_flTileB = 0.0f;
	}
	else
	{
		m_flTileA =  sinf( flAngle );
		m_flTileB = -cosf( flAngle );
	}

	model_t *pSprite = gHUD.m_Spectator.GetMapSprite();
	if( !pSprite || pSprite->numframes <= 0 )
		return;

	int i = (int)sqrt( (float)( pSprite->numframes / (4 * 3) ) );
	if( i < 1 )
		i = 1;

	m_iOverviewTilesX = i * 4;
	m_iOverviewTilesY = i * 3;

	// world bounds straight out of the loaded BSP
	float mins[3], maxs[3];
	if( g_iRadarAPIVersion >= 2 && gRadarAPI.pfnGetRadarMapBounds
		&& gRadarAPI.pfnGetRadarMapBounds( mins, maxs )
		&& ( maxs[0] - mins[0] ) > 1.0f && ( maxs[1] - mins[1] ) > 1.0f )
	{
		m_flMapMinX      = mins[0];
		m_flMapMaxY      = maxs[1];
		m_flTilesPerUnitX = (float)m_iOverviewTilesX / ( maxs[0] - mins[0] );
		m_flTilesPerUnitY = (float)m_iOverviewTilesY / ( maxs[1] - mins[1] );
		m_bBoundsValid   = true;
	}
	else if( !s_bWarnedNoBounds )
	{
		s_bWarnedNoBounds = true;
		gEngfuncs.Con_Printf( "radar: engine has no map-bounds export, falling back "
			"to the overview .txt (map will be misplaced)\n" );
	}

	float flView = s_flRadarViewFraction;
	if( flView <= 0.0f || flView > 1.0f )
		flView = 1.0f;

	// The window covers RADAR_MAP_FRACTION of the map width (see the define),
	// scaled by cl_radar_view. On 1080p that is 2.625 of de_dust2's 8 columns
	// across 378 px, i.e. 144 px per tile instead of 47.
	float flFraction = RADAR_MAP_FRACTION * flView;
	if( flFraction <= 0.0f || flFraction > 4.0f )
		flFraction = RADAR_MAP_FRACTION;

	m_flPixelsPerTile = ( (float)radius * 2.0f ) / ( (float)m_iOverviewTilesX * flFraction );
	if( m_bFullMap )
	{
		m_flTileA = 1.0f; m_flTileB = 0.0f;
		m_flPixelsPerTile = radar_fullmap_layout::TileScale(radius * 2, m_iOverviewTilesX, m_iOverviewTilesY);
	}
}

/*
========================
/*
========================
WorldToTile

World position -> fractional tile index in the overview image
(128 overview pixels per tile, so tile = pixel / 128).

Primary path, taken from the map's own world model bounds:

    tx = (wx - bounds.mins[0]) * tilesX / (bounds.maxs[0] - bounds.mins[0])
    ty = (bounds.maxs[1] - wy) * tilesY / (bounds.maxs[1] - bounds.mins[1])

The image's row 0 is the NORTH edge of the map, so ty grows as wy falls.
de_dust2 gives 0.00164474 tiles per X unit and 0.00107143 per Y unit.

Verified against the game: at player (103.28, 2141.21) this yields
tile (4.801, 1.923), putting the tile-grid origin 18.5 px above where the
old formula did - and the video shows the map's top edge at y=99 against
98.1 predicted, a 1 px match. The old zoom/origin formula predicted 116.7.

Fallback path (engine without the bounds export) keeps the .txt reading so
the radar still draws, but it is known to be misplaced - see the comment in
UpdateRadarTransform.
========================
*/
void CHudRadarModern::WorldToTile( const Vector &vecWorld, float &tx, float &ty ) const
{
	float flZoom = m_OverviewData.zoom > 0.0f ? m_OverviewData.zoom : 1.0f;
	float s = flZoom * ( 1.0f / 1024.0f );

	// crossed and negated - see the instruction listing above.
	//   0x4258  vmls.f64 d13,d2,d0   d13 = 4.0 + s*O1 - s*world.y  (tile column)
	//   0x425c  vmls.f64 d10,d2,d1   d10 = 3.0 + s*O2 - s*world.x  (tile row)
	if( m_bTileRotated )
	{
		// ROTATED overview stores the image turned a quarter turn, so the column
		// takes world.x instead of world.y AND its sign flips; the row takes
		// world.y. It is not a plain swap of the two.
		//
		// Cross-checked against NextClient's RadarView_ComputeTransform
		// (nextclient/client_mini/src/hud/radar/render/RadarView.cpp:138-142),
		// which expresses the same thing as "offset = -offset.Perp()" with
		// Perp(v) = {-y, x}: rotating the centre offset a quarter turn is not the
		// same as exchanging its two components. Replicating that transform and
		// running it over de_dust2's real parameters reproduces these three lines
		// exactly at every sampled world point, and its own unit test
		// (RadarViewTests.cpp: RotatedOverviewProjectsMarkersConsistently) passes.
		tx = 4.0f + s * ( vecWorld[0] - m_OverviewData.origin[0] );
		ty = 3.0f + s * ( m_OverviewData.origin[1] - vecWorld[1] );
	}
	else
	{
		tx = 4.0f + s * ( m_OverviewData.origin[1] - vecWorld[1] );
		ty = 3.0f + s * ( m_OverviewData.origin[0] - vecWorld[0] );
	}

	// user override, for overviews whose description lies about the crossing
	int iOrient = s_cl_radar_orient ? (int)s_cl_radar_orient->value : 0;
	if( iOrient & 1 )
	{
		float t = tx;
		tx = ty;
		ty = t;
	}
	if( iOrient & 2 )
		ty = -ty;

	tx += s_cl_radar_shiftx ? s_cl_radar_shiftx->value : 0.0f;
	ty += s_cl_radar_shifty ? s_cl_radar_shifty->value : 0.0f;
}

/*
========================
WorldDirToTileDir

The derivative of WorldToTile: same axis pairing, same signs, no constants and
no zoom. The self marker uses it instead of a hand written facing vector so the
two can never disagree - the ROTATED overview re-pairs the axes and flips a
sign, and a hardcoded vector then pins the arrow to (-1,0) at every yaw.
========================
*/
void CHudRadarModern::WorldDirToTileDir( float dx, float dy, float &tx, float &ty ) const
{
	if( m_bTileRotated )
	{
		tx = dx;
		ty = -dy;
	}
	else
	{
		tx = -dy;
		ty = -dx;
	}
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
	if( m_bFullMap )
	{
		float fx, fy;
		radar_fullmap_layout::Project(tx,ty,m_iOverviewTilesX,m_iOverviewTilesY,m_iCenterX,m_iCenterY,m_flPixelsPerTile,fx,fy);
		x = (int)fx; y = (int)fy;
		return fabsf(fx-m_iCenterX) > m_iRadius || fabsf(fy-m_iCenterY) > m_iRadius;
	}
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
	}
	else
	{
		m_bOverviewValid = false;
	}
}

static char RadarSiteLabel( const char *name )
{
	if( !name || !name[0] ) return 0;
	char lower[64];
	size_t i = 0;
	for( ; i + 1 < sizeof( lower ) && name[i]; ++i )
		lower[i] = (char)tolower( (unsigned char)name[i] );
	lower[i] = 0;
	for( i = 0; lower[i]; ++i )
	{
		if( ( lower[i] == 'a' || lower[i] == 'b' ) &&
			( i == 0 || !isalnum( (unsigned char)lower[i - 1] ) ) &&
			!isalnum( (unsigned char)lower[i + 1] ) )
			return (char)toupper( (unsigned char)lower[i] );
	}
	const size_t length = strlen( lower );
	if( length && ( lower[length - 1] == 'a' || lower[length - 1] == 'b' ) &&
		( strstr( lower, "target" ) || strstr( lower, "crates" ) ) )
		return (char)toupper( (unsigned char)lower[length - 1] );
	return 0;
}

void CHudRadarModern::LoadObjectives( const char *mapName )
{
	if( !mapName || !mapName[0] ) return;
	if( strcmp( m_szObjectiveMap, mapName ) )
	{
		strncpy( m_szObjectiveMap, mapName, sizeof( m_szObjectiveMap ) - 1 );
		m_szObjectiveMap[sizeof( m_szObjectiveMap ) - 1] = 0;
		m_iObjectiveCount = 0;
		m_bObjectivesLoaded = false;
		m_pObjectiveEntities = NULL;
	}
	cl_entity_t *worldEntity = gEngfuncs.GetEntityByIndex( 0 );
	if( !worldEntity || !worldEntity->model ) return;
	model_t *world = worldEntity->model;
	if( m_bObjectivesLoaded ) return;
	m_iObjectiveCount = 0;
	m_iObjectiveSource = 0;
	// The world model's entity string is not reachable on every engine build;
	// here it comes back empty, which left the radar with no bomb sites and no
	// hostage rescue areas at all. Fall back to reading the BSP entity lump
	// from disk and feed the same parser.
	char *fileData = NULL;
	int fileLength = 0;
	{
		// Load the BSP unconditionally: the entity string may be valid while
		// model_t submodel bounds are not, and the site centres need both.
		char *file = (char *)gEngfuncs.COM_LoadFile( mapName, -1, &fileLength );
		if( file && fileLength > 12 )
		{
			const int version = *(int *)file;
			const int offset = *(int *)( file + 4 );
			const int lump = *(int *)( file + 8 );
			if( version == 30 && offset > 0 && lump > 0 && offset + lump <= fileLength )
				fileData = file;
		}
		if( !fileData && file ) gEngfuncs.COM_FreeFile( file );
	}
	char *entities = fileData ? fileData + *(int *)( fileData + 4 ) : NULL;
	if( !entities ) entities = world->entities;
	if( !entities || !entities[0] )
	{
		if( fileData ) gEngfuncs.COM_FreeFile( fileData );
		return;
	}
	m_iObjectiveSource = ( entities == world->entities ) ? 1 : 2;
	m_pObjectiveEntities = entities;
	char *data = entities;
	char token[1024];
	Vector ctSpawns[64];
	int ctSpawnCount = 0;
	bool hasHostages = false;
	for( int entity = 0; entity < 4096 && data; ++entity )
	{
		data = gEngfuncs.COM_ParseFile( data, token );
		if( !data || token[0] != '{' ) break;
		char classname[64] = {}, model[64] = {}, origin[96] = {}, target[64] = {};
		bool closed = false;
		for( int key = 0; key < 128 && data; ++key )
		{
			data = gEngfuncs.COM_ParseFile( data, token );
			if( !data ) break;
			if( token[0] == '}' ) { closed = true; break; }
			char name[64];
			strncpy( name, token, sizeof( name ) - 1 );
			name[sizeof( name ) - 1] = 0;
			data = gEngfuncs.COM_ParseFile( data, token );
			if( !data || token[0] == '}' ) break;
			char *dst = NULL;
			size_t capacity = 0;
			if( !strcmp( name, "classname" ) ) { dst = classname; capacity = sizeof( classname ); }
			else if( !strcmp( name, "model" ) ) { dst = model; capacity = sizeof( model ); }
			else if( !strcmp( name, "origin" ) ) { dst = origin; capacity = sizeof( origin ); }
			else if( !strcmp( name, "target" ) ) { dst = target; capacity = sizeof( target ); }
			if( dst ) { strncpy( dst, token, capacity - 1 ); dst[capacity - 1] = 0; }
		}
		if( !closed ) break;
		if( !strcmp( classname, "hostage_entity" ) ) { hasHostages = true; continue; }
		const bool bomb = !strcmp( classname, "func_bomb_target" ) || !strcmp( classname, "info_bomb_target" );
		const bool rescue = !strcmp( classname, "func_hostage_rescue" ) || !strcmp( classname, "info_hostage_rescue" );
		const bool ctSpawn = !strcmp( classname, "info_player_start" );
		if( !bomb && !rescue && !ctSpawn ) continue;
		Vector center;
		bool haveCenter = false;
		Vector boundsMin, boundsMax;
		bool haveBounds = false;
		if( model[0] == '*' && model[1] >= '0' && model[1] <= '9' )
		{
			const int index = atoi( model + 1 );
			// Prefer the BSP file. The client model can expose submodels whose
			// bounds are zeroed on this build, which collapsed every site onto
			// the world origin and left them all outside the radar circle.
			if( fileData )
			{
				int subOffset = 0, subLength = 0;
				memcpy( &subOffset, fileData + 4 + 14 * 8, sizeof( subOffset ) );
				memcpy( &subLength, fileData + 8 + 14 * 8, sizeof( subLength ) );
				if( index > 0 && subOffset > 0 && subLength >= ( index + 1 ) * 64 &&
					subOffset + subLength <= fileLength )
				{
					const char *entry = fileData + subOffset + index * 64;
					float mins[3], maxs[3];
					memcpy( mins, entry, sizeof( mins ) );
					memcpy( maxs, entry + 12, sizeof( maxs ) );
					for( int axis = 0; axis < 3; ++axis )
					{
						center[axis] = ( mins[axis] + maxs[axis] ) * 0.5f;
						boundsMin[axis] = mins[axis]; boundsMax[axis] = maxs[axis];
					}
					haveBounds = true;
					haveCenter = true;
				}
			}
			if( !haveCenter && world->submodels && index > 0 && index < world->numsubmodels )
			{
				for( int axis = 0; axis < 3; ++axis )
				{
					boundsMin[axis] = world->submodels[index].mins[axis];
					boundsMax[axis] = world->submodels[index].maxs[axis];
					center[axis] = ( boundsMin[axis] + boundsMax[axis] ) * 0.5f;
				}
				haveBounds = true;
				haveCenter = true;
			}
		}
		if( !haveCenter && sscanf( origin, "%f %f %f", &center[0], &center[1], &center[2] ) == 3 )
			haveCenter = true;
		if( !haveCenter ) continue;
		if( !std::isfinite( center[0] ) || !std::isfinite( center[1] ) || !std::isfinite( center[2] ) ) continue;
		if( ctSpawn )
		{
			if( ctSpawnCount < 64 ) ctSpawns[ctSpawnCount++] = center;
			continue;
		}
		// Several brush triggers may make up one named plant site.
		if( bomb && target[0] )
		{
			bool grouped = false;
			for( int i = 0; i < m_iObjectiveCount; ++i )
				if( m_Objectives[i].bomb && !strcmp( m_Objectives[i].group, target ) )
				{
					Objective &site = m_Objectives[i];
					for( int axis = 0; axis < 3; ++axis )
						site.origin[axis] = ( site.origin[axis] * site.sources + center[axis] ) / ( site.sources + 1 );
					++site.sources;
					grouped = true;
					break;
				}
			if( grouped ) continue;
		}
		if( m_iObjectiveCount >= 32 ) continue;
		Objective &site = m_Objectives[m_iObjectiveCount++];
		site.origin = center;
		site.boundsMin = haveBounds ? boundsMin : center - Vector( 256, 256, 256 );
		site.boundsMax = haveBounds ? boundsMax : center + Vector( 256, 256, 256 );
		site.exactBounds = haveBounds;
		site.bomb = bomb;
		site.label = bomb ? RadarSiteLabel( target ) : 0;
		strncpy( site.group, target, sizeof( site.group ) - 1 );
		site.group[sizeof( site.group ) - 1] = 0;
		site.sources = 1;
	}
	bool hasRescue = false;
	for( int i = 0; i < m_iObjectiveCount; ++i )
		if( !m_Objectives[i].bomb ) hasRescue = true;
	// The server uses CT spawn points as 256-unit rescue circles when a
	// hostage map has no explicit rescue entity (for example cs_assault).
	if( hasHostages && !hasRescue && ctSpawnCount )
	{
		for( int i = 0; i < ctSpawnCount; ++i )
		{
			bool grouped = false;
			for( int j = 0; j < m_iObjectiveCount; ++j )
			{
				Objective &site = m_Objectives[j];
				if( strcmp( site.group, "#ctspawn" ) ) continue;
				const Vector delta = site.origin - ctSpawns[i];
				if( DotProduct( delta, delta ) > 512.0f * 512.0f ) continue;
				for( int axis = 0; axis < 3; ++axis )
				{
					site.origin[axis] = ( site.origin[axis] * site.sources + ctSpawns[i][axis] ) / ( site.sources + 1 );
					site.boundsMin[axis] = min( site.boundsMin[axis], ctSpawns[i][axis] - 256.0f );
					site.boundsMax[axis] = max( site.boundsMax[axis], ctSpawns[i][axis] + 256.0f );
				}
				++site.sources;
				grouped = true;
				break;
			}
			if( grouped || m_iObjectiveCount >= 32 ) continue;
			Objective &site = m_Objectives[m_iObjectiveCount++];
			site.origin = ctSpawns[i];
			site.boundsMin = ctSpawns[i] - Vector( 256, 256, 256 );
			site.boundsMax = ctSpawns[i] + Vector( 256, 256, 256 );
			site.exactBounds = false;
			site.bomb = false;
			site.label = 0;
			strcpy( site.group, "#ctspawn" );
			site.sources = 1;
		}
	}
	int rescueIndex = 0;
	for( int i = 0; i < m_iObjectiveCount; ++i )
		if( !m_Objectives[i].bomb )
			m_Objectives[i].label = rescueIndex < 2 ? (char)( 'A' + rescueIndex++ ) : 0;
	// Letter fallback. GoldSrc maps name the plant trigger inconsistently
	// (tgt_a, target01, empty), and the radar has to show A and B on bomb
	// maps, so: a trailing digit 1/2 maps to A/B, and when neither site of
	// a two-site map carries a letter, entity order decides.
	int bombCount = 0, bombIndex[2] = { -1, -1 };
	for( int i = 0; i < m_iObjectiveCount; ++i )
		if( m_Objectives[i].bomb && bombCount < 2 ) bombIndex[bombCount++] = i;
	for( int i = 0; i < m_iObjectiveCount; ++i )
	{
		Objective &site = m_Objectives[i];
		if( !site.bomb || site.label ) continue;
		const size_t length = strlen( site.group );
		if( length && ( site.group[length - 1] == '1' || site.group[length - 1] == '2' ) )
			site.label = site.group[length - 1] == '1' ? 'A' : 'B';
	}
	if( bombCount == 2 && !m_Objectives[bombIndex[0]].label && !m_Objectives[bombIndex[1]].label )
	{
		m_Objectives[bombIndex[0]].label = 'A';
		m_Objectives[bombIndex[1]].label = 'B';
	}
	if( bombCount == 2 && m_Objectives[bombIndex[0]].label && m_Objectives[bombIndex[0]].label == m_Objectives[bombIndex[1]].label )
		m_Objectives[bombIndex[1]].label = m_Objectives[bombIndex[0]].label == 'A' ? 'B' : 'A';
	if( fileData )
	{
		// m_pObjectiveEntities only exists as a change marker; never leave it
		// pointing into the buffer that is about to be freed.
		m_pObjectiveEntities = NULL;
		gEngfuncs.COM_FreeFile( fileData );
	}
	m_bObjectivesLoaded = true;
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

// Attribute-preserving rectangular clipping for full-map tiles and zones.
static int ClipPolyToSquare(float *x, float *y, float *u, float *v, int n, float cx, float cy, float radius)
{
	float ax[RADAR_CLIP_MAX], ay[RADAR_CLIP_MAX], au[RADAR_CLIP_MAX], av[RADAR_CLIP_MAX];
	for(int edge = 0; edge < 4 && n > 0; ++edge)
	{
		int count = 0;
		for(int i = 0; i < n; ++i)
		{
			const int j = (i + 1) % n;
			const float d0 = edge == 0 ? x[i]-(cx-radius) : edge == 1 ? (cx+radius)-x[i] : edge == 2 ? y[i]-(cy-radius) : (cy+radius)-y[i];
			const float d1 = edge == 0 ? x[j]-(cx-radius) : edge == 1 ? (cx+radius)-x[j] : edge == 2 ? y[j]-(cy-radius) : (cy+radius)-y[j];
			if(d0 >= 0 && count < RADAR_CLIP_MAX) { ax[count]=x[i]; ay[count]=y[i]; au[count]=u[i]; av[count++]=v[i]; }
			if((d0 >= 0) != (d1 >= 0) && count < RADAR_CLIP_MAX)
			{
				const float t = d0 / (d0-d1);
				ax[count]=x[i]+(x[j]-x[i])*t; ay[count]=y[i]+(y[j]-y[i])*t;
				au[count]=u[i]+(u[j]-u[i])*t; av[count++]=v[i]+(v[j]-v[i])*t;
			}
		}
		n=count; for(int i=0;i<n;++i) { x[i]=ax[i]; y[i]=ay[i]; u[i]=au[i]; v[i]=av[i]; }
	}
	return n;
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
	if( m_bFullMap ) { selfX = xTiles * 0.5f; selfY = yTiles * 0.5f; }
	else WorldToTile( gHUD.m_vecOrigin, selfX, selfY );

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
			int n = m_bFullMap ? ClipPolyToSquare(cX,cY,cU,cV,4,flCX,flCY,flR)
				: ClipPolyToCircle( cX, cY, cU, cV, 4, cX, cY, cU, cV, flCX, flCY, flR );
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
	// Deliberately a fixed colour rather than the player's team colour: it is
	// the one marker that always means "you", so it must not be mistaken for
	// either a teammate (blue) or an enemy (red). Kept yellow.
	int r = 255;
	int g = 220;
	int b = 0;
	if( m_iPlayerNum > 0 && m_iPlayerNum <= MAX_PLAYERS &&
		g_PlayerExtraInfo[m_iPlayerNum].has_c4 )
	{
		// Carrying the bomb: a deeper amber, not the old 255,200,40. Against the
		// plain 255,220,0 that was a 20/40 step and read as the same colour, so
		// the state change was invisible in game.
		r = 255;
		g = 140;
		b = 0;
	}

	// Always draw the geometry, never rely on a sprite being present: the
	// whole point of this marker is to show where the player sits on the
	// radar, and a missing hud.txt entry used to leave the centre empty.
	int iSize = max( 5, radius / 12 );

	// The arrow lives in the same frame as the map, so push the facing
	// direction through the grid basis rather than recomputing an angle -
	// that way it can never drift relative to the map.
	//
	// The facing has to be expressed in TILE space. Take it straight from the
	// mapping itself (WorldDirToTileDir is the derivative of WorldToTile) instead
	// of hardcoding it: on a ROTATED overview the column takes world.x with the
	// opposite sign and the row takes world.y, so the old fixed vector
	// (-sin yaw, -cos yaw) came out as (-1, 0) at EVERY yaw - the arrow pointed
	// left no matter which way the player turned. Deriving it keeps the two in
	// step by construction.
	float flYaw = DEG2RAD( gHUD.m_vecAngles[1] );
	float flFDX, flFDY;
	WorldDirToTileDir( cosf( flYaw ), sinf( flYaw ), flFDX, flFDY );

	float flAX = flFDX * m_flTileA - flFDY * m_flTileB;
	float flAY = flFDX * m_flTileB + flFDY * m_flTileA;
	float flLen = (float)sqrt( flAX * flAX + flAY * flAY );
	if( flLen < 0.0001f )
	{
		flAX = 0.0f;
		flAY = -1.0f;
	}
	else
	{
		flAX /= flLen;
		flAY /= flLen;
	}

	const float kSpread = 2.0943951f;	// 120 degrees, same as before
	float flTipX = cx + flAX * (float)iSize * 1.6f;
	float flTipY = cy + flAY * (float)iSize * 1.6f;
	float flBX = cx + ( flAX * cosf( kSpread ) - flAY * sinf( kSpread ) ) * (float)iSize;
	float flBY = cy + ( flAX * sinf( kSpread ) + flAY * cosf( kSpread ) ) * (float)iSize;
	float flCX = cx + ( flAX * cosf( -kSpread ) - flAY * sinf( -kSpread ) ) * (float)iSize;
	float flCY = cy + ( flAX * sinf( -kSpread ) + flAY * cosf( -kSpread ) ) * (float)iSize;

	// solid backing so the arrow reads over any map content (blend, not the
	// additive FillRGBA, which would draw nothing for pure black)
	FillRGBABlend( cx - 4, cy - 4, 8, 8, 0, 0, 0, 190 );

	// Same trap as DrawRadarDisc: whatever texture the previous HUD element
	// left bound is still active in kRenderTransAlpha, and these vertices
	// carry no texcoords, so the arrow was being modulated by a random texel
	// of the overview and vanished against it. Bind the white texture and
	// give every vertex a coordinate inside it.
	if( gHUD.m_WhiteTex )
	{
		gRenderAPI.GL_SelectTexture( 0 );
		gRenderAPI.GL_Bind( 0, gHUD.m_WhiteTex );
	}

	gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( r, g, b, 255 );
	gEngfuncs.pTriAPI->Begin( TRI_TRIANGLES );

	float flPX[3] = { flTipX, flBX, flCX };
	float flPY[3] = { flTipY, flBY, flCY };
	for( int i = 0; i < 3; i++ )
	{
		gEngfuncs.pTriAPI->TexCoord2f( 0.5f, 0.5f );
		gEngfuncs.pTriAPI->Vertex3f( flPX[i], flPY[i], 0 );
	}

	gEngfuncs.pTriAPI->End();
}

/*
========================
DrawBlurredBackground

Paints the engine's blurred copy of the screen behind the radar, clipped to
the radar circle. Returns false when there is nothing to show yet, so the
caller can fall back to the flat plate instead of leaving a hole.

Three things this has to get right:

  * BIND THE TEXTURE. The tri API carries whatever the previous HUD element
    bound on unit 0. Without an explicit bind this quad samples that, which
    is how the backdrop ended up "not working" with no error anywhere: the
    capture was fine, the draw was reading someone else's texture.

  * TRUST THE RECT, DON'T ASSUME IT. The engine captures a square block
    centred on the radar and clamps it to stay inside the framebuffer, so it
    is only the requested window when that window fits. The engine hands back
    the rectangle it actually got; deriving it here instead would be a second
    copy of the same coordinate arithmetic, and the Y flip between HUD and GL
    origin is exactly the kind of thing that goes wrong silently.

  * FLIP V. glReadPixels fills memory bottom-row first, so texture row 0 is
    the BOTTOM of the captured region. Screen top is therefore v=1.

The polygon is clipped to the circle with the same helper the map tiles use,
so the backdrop and the map share one window edge instead of the map being
round on a square backdrop.
========================
*/
bool CHudRadarModern::DrawBlurredBackground( int cx, int cy, int radius )
{
	int rx = 0, ry = 0, rw = 0, rh = 0;
	int i, n;
	int iAlpha;
	float inX[4], inY[4], inU[4], inV[4];
	float outX[RADAR_CLIP_MAX], outY[RADAR_CLIP_MAX];
	float outU[RADAR_CLIP_MAX], outV[RADAR_CLIP_MAX];

	// an engine older than radar API v3 cannot report what it captured
	if( !gRadarAPI.pfnGetRadarCaptureRect )
		return false;

	// 0 means "nothing captured yet", which is not the same as a black frame
	if( !gRadarAPI.pfnGetRadarCaptureRect( &rx, &ry, &rw, &rh ) || rw < 1 || rh < 1 )
		return false;

	if( !gRadarAPI.pfnBindRadarTexture )
		return false;

	gRadarAPI.pfnBindRadarTexture();

	// the captured block, mapped 1:1 onto the screen it came from
	inX[0] = (float)rx;            inY[0] = (float)ry;        inU[0] = 0.0f; inV[0] = 1.0f;
	inX[1] = (float)( rx + rw );   inY[1] = (float)ry;        inU[1] = 1.0f; inV[1] = 1.0f;
	inX[2] = (float)( rx + rw );   inY[2] = (float)( ry + rh ); inU[2] = 1.0f; inV[2] = 0.0f;
	inX[3] = (float)rx;            inY[3] = (float)( ry + rh ); inU[3] = 0.0f; inV[3] = 0.0f;

	n = ClipPolyToCircle( inX, inY, inU, inV, 4,
		outX, outY, outU, outV, (float)cx, (float)cy, (float)radius );
	if( n < 3 )
		return false;

	iAlpha = s_cl_radar_blur_alpha ? (int)( s_cl_radar_blur_alpha->value * 255.0f ) : 150;
	if( iAlpha < 0 )   iAlpha = 0;
	if( iAlpha > 255 ) iAlpha = 255;

	gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( 255, 255, 255, iAlpha );

	// the clipped result is convex, so a fan off the first vertex covers it
	gEngfuncs.pTriAPI->Begin( TRI_TRIANGLES );

	for( i = 1; i < n - 1; i++ )
	{
		gEngfuncs.pTriAPI->TexCoord2f( outU[0], outV[0] );
		gEngfuncs.pTriAPI->Vertex3f( outX[0], outY[0], 0 );
		gEngfuncs.pTriAPI->TexCoord2f( outU[i], outV[i] );
		gEngfuncs.pTriAPI->Vertex3f( outX[i], outY[i], 0 );
		gEngfuncs.pTriAPI->TexCoord2f( outU[i + 1], outV[i + 1] );
		gEngfuncs.pTriAPI->Vertex3f( outX[i + 1], outY[i + 1], 0 );
	}

	gEngfuncs.pTriAPI->End();
	return true;
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

void CHudRadarModern::DrawRadarFrame( int cx, int cy, int radius, int r, int g, int b, int a, bool additive )
{
	// A ring, not a rectangle. The disc underneath is round and the map tiles
	// are clipped to the circle, so four straight edges left the corners of the
	// outline hanging over nothing while cutting across the arc it was supposed
	// to follow. Same 63 segments as DrawRadarMask so the two agree.
	const int iSegments = RADAR_MASK_SEGMENTS;
	const float flOuter = (float)radius;
	const float flInner = flOuter - (float)max( 1, radius / 32 );
	const float flStep = ( 2.0f * 3.14159265358979f ) / (float)iSegments;

	// The engine's TriColor4ub forces alpha to 1.0 (cl_game.c:3237), so an alpha
	// argument here is silently dropped. Fold it into the colour only for the
	// opaque ring (the bomb warning): its dim phase used to be identical to the
	// bright one, so the ring never blinked. The additive border keeps full
	// brightness on purpose - dimming it there would change the normal look.
	if( !additive && a < 255 )
	{
		const int ai = ( a < 0 ) ? 0 : a;
		r = r * ai / 255;
		g = g * ai / 255;
		b = b * ai / 255;
	}
	a = 255;

	// untextured, but still has to be told so: the last bound texture is
	// still live here and would tint the whole frame
	if( gHUD.m_WhiteTex )
	{
		gRenderAPI.GL_SelectTexture( 0 );
		gRenderAPI.GL_Bind( 0, gHUD.m_WhiteTex );
	}

	gEngfuncs.pTriAPI->RenderMode( additive ? kRenderTransAdd : kRenderTransTexture );
	gEngfuncs.pTriAPI->CullFace( TRI_NONE );
	gEngfuncs.pTriAPI->Color4ub( r, g, b, a );

	gEngfuncs.pTriAPI->Begin( TRI_QUADS );

	for( int i = 0; i < iSegments; i++ )
	{
		float f0 = flStep * (float)i;
		float f1 = flStep * (float)( i + 1 );

		float c0 = cosf( f0 ), s0 = sinf( f0 );
		float c1 = cosf( f1 ), s1 = sinf( f1 );

		gEngfuncs.pTriAPI->TexCoord2f( 0.5f, 0.5f );
		gEngfuncs.pTriAPI->Vertex3f( cx + c0 * flInner, cy + s0 * flInner, 0 );

		gEngfuncs.pTriAPI->TexCoord2f( 0.5f, 0.5f );
		gEngfuncs.pTriAPI->Vertex3f( cx + c0 * flOuter, cy + s0 * flOuter, 0 );

		gEngfuncs.pTriAPI->TexCoord2f( 0.5f, 0.5f );
		gEngfuncs.pTriAPI->Vertex3f( cx + c1 * flOuter, cy + s1 * flOuter, 0 );

		gEngfuncs.pTriAPI->TexCoord2f( 0.5f, 0.5f );
		gEngfuncs.pTriAPI->Vertex3f( cx + c1 * flInner, cy + s1 * flInner, 0 );
	}

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
	const extra_player_info_t &bomb = g_PlayerExtraInfo[33];
	if( bomb.dead || !bomb.radarflashes || bomb.playerclass != 1 ||
		( bomb.origin[0] == 0 && bomb.origin[1] == 0 && bomb.origin[2] == 0 ) )
		return;

	int iAlpha = ( (int)( flTime * 2 ) & 1 ) == 0 ? 255 : 100;
	DrawRadarFrame( cx, cy, radius, 255, 0, 0, iAlpha, false );
}

/*
========================
UpdateEnemyVisibility

Decides which enemies the radar is allowed to show.

Without this every enemy that ever entered the PVS stays on the radar for as
long as its client entity survives, which reads as "enemies are permanently
visible in a radius around you" - the map walls do not hide anything.

Modeled on NextClient's RadarVisibility
(nextclient/client_mini/src/hud/radar/policy/Visibility.cpp): a 20 Hz sweep,
view cone first (cheap) then a trace (expensive). Their version also lends the
eyes of teammates the server revealed; this server has no such reveal, so the
local player is the only viewer.

Two points are traced per enemy - the feet and just above eye level - because
testing only the feet hides a player whose upper body is already exposed.
========================
*/
// The radar keeps its own death stamp, driven by DeathMsg, which is the only
// death edge this server broadcasts to every client. The ScoreAttrib dead flag
// is compiled out in some builds, and cl_entity_t::curstate.health reads 0 even
// for living players, so neither can be the death test on its own.
bool CHudRadarModern::IsPlayerDead( int i ) const
{
	return m_aRadarPlayerDeadAt[i] > 0.0f;
}

void CHudRadarModern::NotePlayerAlive( int player )
{
	if(player<1||player>kRadarMaxPlayers||m_aRadarPlayerDeadAt[player]<=0)return;
	m_aRadarPlayerDeadAt[player]=0;m_aRadarPlayerDeadMoveStreak[player]=0;
	m_aRadarPlayerDeadPrevOrigin[player]=Vector(0,0,0);
	m_aRadarEnemyVisible[player]=m_aRadarEnemySeen[player]=m_aRadarEnemyConfirmed[player]=false;
	m_aRadarEnemyConfirm[player]=m_aRadarEnemyHold[player]=0;m_aRadarHasConfirmedOrigin[player]=false;
}

void CHudRadarModern::NotePlayerDeath( int victim, float time )
{
	if( victim <= 0 || victim > kRadarMaxPlayers )
		return;
	if( m_aRadarPlayerDeadAt[victim] != 0.0f )
		return;
	const int localTeam = RadarTeamOf( gHUD.m_Scoreboard.m_iPlayerNum );
	const int victimTeam = RadarTeamOf( victim );
	const bool teammate = localTeam > 0 && localTeam == victimTeam;
	Vector origin( 0, 0, 0 );
	const player_info_t *info=IEngineStudio.PlayerInfo?IEngineStudio.PlayerInfo(victim-1):NULL;
	if(teammate&&info&&info->userid>0&&m_aRadarTeammateUserid[victim]==info->userid&&m_aRadarTeammateTeam[victim]==victimTeam)
		origin=m_aRadarTeammateOrigin[victim];
	else if( teammate && m_aRadarEnemySeen[victim] )
		origin = m_aRadarEnemyLastOrigin[victim];
	else if( !teammate && localTeam > 0 && victimTeam > 0 &&
		m_aRadarHasConfirmedOrigin[victim] &&
		m_aRadarConfirmedLocalTeam[victim] == localTeam &&
		m_aRadarConfirmedVictimTeam[victim] == victimTeam )
		origin = m_aRadarConfirmedOrigin[victim];
	if( teammate && origin == Vector( 0, 0, 0 ) )
	{
		cl_entity_t *pEnt = gEngfuncs.GetEntityByIndex( victim );
		if( OC2_CurrentPlayerSnapshot(pEnt) )
			origin = pEnt->origin;
	}
	// Stamp the death even when there is no usable position. m_aRadarPlayerDeadAt
	// is the only gate that stops the radar drawing a living blip for a corpse,
	// and a stamp without a position simply draws no X (guarded at draw time).
	// Returning early here marked never-seen enemies as alive forever, which is
	// exactly the "killed him but he is still on the radar" report.
	m_aRadarPlayerDeadAt[victim] = time;
	m_aRadarPlayerDeadOrigin[victim] = origin;
	m_aRadarPlayerDeadPrevOrigin[victim] = Vector( 0, 0, 0 );
	m_aRadarPlayerDeadMoveStreak[victim] = 0.0f;
}

void CHudRadarModern::NoteTeammatePosition( int player, const Vector &origin )
{
	if(player<1||player>kRadarMaxPlayers||!IEngineStudio.PlayerInfo)return;
	const player_info_t *info=IEngineStudio.PlayerInfo(player-1);
	if(!info||info->userid<=0)return;
	m_aRadarTeammateOrigin[player]=origin;m_aRadarTeammateUserid[player]=info->userid;
	m_aRadarTeammateTeam[player]=RadarTeamOf(player);
}

void CHudRadarModern::UpdateEnemyVisibility( float flTime )
{
	cl_entity_t *pLocal = gEngfuncs.GetLocalPlayer();
	int iLocalTeam = ( m_iPlayerNum > 0 && m_iPlayerNum <= kRadarMaxPlayers )
		? RadarTeamOf( m_iPlayerNum ) : -1;

	// Without a known team "enemy" is undefined, and a spectator has no PVS of
	// their own, so both cases fall back to drawing everything.
	m_bRadarGating = pLocal && iLocalTeam > 0 && !g_iUser1 && !gEngfuncs.IsSpectateOnly()
		&& s_cl_radar_enemy_los && s_cl_radar_enemy_los->value != 0.0f;
	if( m_iRadarTrackedLocalTeam != iLocalTeam )
	{
		m_iRadarTrackedLocalTeam = iLocalTeam;
		memset( m_aRadarTrackedTeam, 0, sizeof( m_aRadarTrackedTeam ) );
		memset( m_aRadarEnemyVisible, 0, sizeof( m_aRadarEnemyVisible ) );
		memset( m_aRadarEnemyConfirmed, 0, sizeof( m_aRadarEnemyConfirmed ) );
		memset( m_aRadarEnemySeen, 0, sizeof( m_aRadarEnemySeen ) );
		memset( m_aRadarEnemyConfirm, 0, sizeof( m_aRadarEnemyConfirm ) );
		memset( m_aRadarEnemyHold, 0, sizeof( m_aRadarEnemyHold ) );
		memset( m_aRadarHasConfirmedOrigin, 0, sizeof( m_aRadarHasConfirmedOrigin ) );
	}

	if( flTime < m_flRadarVisNext )
		return;
	m_flRadarVisNext = flTime + 0.05f;

	// Use the real elapsed time: a fixed step stretched the 0.5 s windows on
	// low frame rates.
	float flStep = ( m_flRadarVisLast > 0.0f ) ? ( flTime - m_flRadarVisLast ) : 0.05f;
	m_flRadarVisLast = flTime;
	if( flStep < 0.01f ) flStep = 0.01f;
	if( flStep > 0.25f ) flStep = 0.25f;

	// pm_shared hull table: 0 standing, 1 ducking. Eye heights are the values
	// the player code uses; v_origin is not exported here and the view bob is
	// a few units, which a wall is not going to care about.
	vec3_t vecEye = Vector( 0, 0, 0 );
	if( pLocal )
	{
		vecEye = pLocal->origin;
		vecEye[2] += ( pLocal->curstate.usehull == 1 ) ? 12.0f : 8.0f;
	}

	vec3_t vecFwd;
	AngleVectors( (const float *)&gHUD.m_vecAngles, vecFwd, NULL, NULL );

	// m_iFOV is the 4:3 horizontal FOV. The cone is tested against a single
	// point per target, so pad it - a widescreen aspect and a target sitting at
	// the screen edge both fall outside fov/2 otherwise.
	// It is the hip-fire value: this client implements ADS by changing
	// clientdata->fov only, so m_iFOV never narrows and a scoped player used to
	// keep the full 90 degree cone. Prefer the live ADS world FOV when the
	// weapon is actually scoped.
	float flFov = ( gHUD.m_iFOV > 1 ) ? (float)gHUD.m_iFOV : 90.0f;
	const float flAdsFov = V_CurrentADSWorldFOV();
	if( flAdsFov > 1.0f && flAdsFov < flFov )
		flFov = flAdsFov;
	float flCosHalf = cosf( DEG2RAD( flFov * 0.5f + 10.0f ) );

	// The PM state has to be armed before any trace, or every trace reports
	// startsolid and nothing is ever visible.
	if( m_bRadarGating )
	{
		gEngfuncs.pEventAPI->EV_SetUpPlayerPrediction( FALSE, TRUE );
		gEngfuncs.pEventAPI->EV_PushPMStates();
		gEngfuncs.pEventAPI->EV_SetTraceHull( 2 );
	}

	int iMax = gEngfuncs.GetMaxClients();
	if( iMax > kRadarMaxPlayers )
		iMax = kRadarMaxPlayers;

	for( int i = 1; i <= iMax; i++ )
	{
		if( i == m_iPlayerNum )
			continue;

		cl_entity_t *pEnt = gEngfuncs.GetEntityByIndex( i );
		const bool live = OC2_CurrentPlayerSnapshot( pEnt );
		const int trackedTeam = RadarTeamOf( i );
		if( m_aRadarTrackedTeam[i] != trackedTeam )
		{
			m_aRadarTrackedTeam[i] = trackedTeam;
			m_aRadarEnemyVisible[i] = false;
			m_aRadarEnemySeen[i] = false;
			m_aRadarEnemyConfirmed[i] = false;
			m_aRadarEnemyConfirm[i] = 0.0f;
			m_aRadarEnemyHold[i] = 0.0f;
			m_aRadarHasConfirmedOrigin[i] = false;
		}

		if( IsPlayerDead( i ) )
		{
			// Only a discontinuous jump counts as a respawn. The sweep runs at
			// 20 Hz, so a corpse that slides, falls or gets blasted moves at most
			// tens of units per sample, while a respawn relocates it outright.
			// The old rule cleared the stamp as soon as the body had drifted 64
			// units from where it died, which is how a killed enemy came back as
			// a living blip a few seconds later.
			if( live )
			{
				const Vector &prevOrigin = m_aRadarPlayerDeadPrevOrigin[i];
				const float deadFor = flTime - m_aRadarPlayerDeadAt[i];
				const float flMoved = ( prevOrigin == Vector( 0, 0, 0 ) )
					? 0.0f : ( pEnt->origin - prevOrigin ).Length();
				bool bRespawn = false;

				if( prevOrigin != Vector( 0, 0, 0 ) && deadFor > 0.5f && flMoved > 256.0f )
				{
					bRespawn = true;
				}
				else
				{
					// A respawn can also land close to the corpse, where the jump
					// test sees nothing. A corpse stops moving after a few samples,
					// a player who is back in the round does not.
					if( flMoved > 8.0f )
						m_aRadarPlayerDeadMoveStreak[i] += flStep;
					else
						m_aRadarPlayerDeadMoveStreak[i] = 0.0f;

					if( deadFor > 6.0f && m_aRadarPlayerDeadMoveStreak[i] > 0.6f &&
						( pEnt->origin - m_aRadarPlayerDeadOrigin[i] ).Length() > 128.0f )
						bRespawn = true;
					else
						m_aRadarPlayerDeadPrevOrigin[i] = pEnt->origin;
				}

				if( bRespawn )
				{
					m_aRadarPlayerDeadAt[i] = 0.0f;
					m_aRadarPlayerDeadPrevOrigin[i] = Vector( 0, 0, 0 );
					m_aRadarPlayerDeadMoveStreak[i] = 0.0f;
					m_aRadarHasConfirmedOrigin[i] = false;
					continue;   // let the next sweep treat him as alive again
				}
			}
			m_aRadarEnemyVisible[i] = false;
			m_aRadarEnemyConfirm[i] = 0.0f;
			m_aRadarEnemyHold[i] = 0.0f;
			m_aRadarEnemyConfirmed[i] = false;
			continue;
		}

		if( !m_bRadarGating )
		{
			m_aRadarEnemyVisible[i] = true;
			m_aRadarEnemyConfirm[i] = 0.0f;
			m_aRadarEnemyHold[i] = 0.0f;
			m_aRadarEnemyConfirmed[i] = false;
		}

		// Teammates keep the ungated behaviour, but their last position is
		// still recorded so a dead teammate can keep a fading X.
		if( RadarTeamOf( i ) == iLocalTeam )
		{
			if( live )
			{
				m_aRadarEnemyLastOrigin[i] = pEnt->origin;
				m_aRadarEnemyLastSeen[i] = flTime;
				m_aRadarEnemySeen[i] = true;
			}
			continue;
		}
		if( !m_bRadarGating )
			continue;

		if( !live )
		{
			// Out of the PVS: the ghost still needs "was confirmed once", so
			// confirmed is deliberately kept here; only the death branch above
			// clears it.
			m_aRadarEnemyVisible[i] = false;
			m_aRadarEnemyConfirm[i] = 0.0f;
			m_aRadarEnemyHold[i] += flStep;
			continue;
		}

		vec3_t vecTo;
		VectorSubtract( pEnt->origin, vecEye, vecTo );
		float flDist = VectorLength( vecTo );
		bool bClear = false;
		if( flDist < 1.0f || ( DotProduct( vecTo, vecFwd ) / flDist ) >= flCosHalf )
		{
			vec3_t vecTarget[2];
			vecTarget[0] = pEnt->origin;
			vecTarget[1] = vecTarget[0];
			vecTarget[1][2] += 13.0f;   // just above the standing eye height

			for( int k = 0; k < 2 && !bClear; k++ )
			{
				pmtrace_t tr;
				memset( &tr, 0, sizeof( tr ) );
				// PM_STUDIO_BOX makes props clip by their bounding box instead of
				// their studio hulls; PM_GLASS_IGNORE lets you see through windows,
				// which is what the server's own visibility does.
				gEngfuncs.pEventAPI->EV_PlayerTrace( vecEye, vecTarget[k],
					PM_STUDIO_BOX | PM_GLASS_IGNORE, -1, &tr );
				if( tr.fraction >= 0.99f && !tr.startsolid && !tr.allsolid )
					bClear = true;
			}
		}

		if( bClear )
		{
			m_aRadarEnemyConfirm[i] += flStep;
			m_aRadarEnemyHold[i] = 0.0f;
			m_aRadarEnemyLastOrigin[i] = pEnt->origin;
			m_aRadarEnemyLastSeen[i] = flTime;
			m_aRadarEnemySeen[i] = true;
			if( m_aRadarEnemyConfirm[i] >= 0.5f )
				m_aRadarEnemyConfirmed[i] = true;
			const int targetTeam = RadarTeamOf( i );
			if( m_aRadarEnemyConfirmed[i] && targetTeam > 0 && targetTeam != iLocalTeam )
			{
				m_aRadarConfirmedOrigin[i] = pEnt->origin;
				m_aRadarConfirmedLocalTeam[i] = iLocalTeam;
				m_aRadarConfirmedVictimTeam[i] = targetTeam;
				m_aRadarHasConfirmedOrigin[i] = true;
			}
		}
		else
		{
			// A short break must not erase progress towards the 0.5 s confirm:
			// at the edge of the cone or behind a railing the trace flickers.
			// Only half a second of continuous loss resets the counter.
			m_aRadarEnemyHold[i] += flStep;
			if( m_aRadarEnemyConfirm[i] < 0.5f && m_aRadarEnemyHold[i] > 0.5f )
				m_aRadarEnemyConfirm[i] = 0.0f;
		}

		m_aRadarEnemyVisible[i] = m_aRadarEnemyConfirmed[i] &&
			( bClear || m_aRadarEnemyHold[i] <= 0.5f );
	}

	if( m_bRadarGating )
		gEngfuncs.pEventAPI->EV_PopPMStates();
}

// Marker discs scale with the radar, so 720p and 1080p keep the same visual
// weight. A fixed 8 px disc was 1.6x the self arrow at 1080p while it matched
// at 720p, because the arrow scales with radius and the disc did not.
static int BlipRadiusFor( int radius )
{
	int iR = radius / 19;
	if( iR < 5 ) iR = 5;
	if( iR > 10 ) iR = 10;
	return iR;
}

/*
========================
FillDisc

Row-by-row filled disc through the blend path. Use this instead of
SPR_Set + SPR_DrawAdditive for anything that has to hold one colour: the
additive pass adds its RGB to whatever ground is underneath, which made the
same enemy read red on a wall and pink on the tan floor, and flipped a
teammate between cyan and lavender on the same radar.
========================
*/
static void FillDisc( int cx, int cy, int radius, int r, int g, int b, int a )
{
	for( int dy = -radius; dy <= radius; ++dy )
	{
		const int w = (int)( sqrtf( (float)( radius * radius - dy * dy ) ) + 0.5f );
		if( w > 0 )
			FillRGBABlend( cx - w, cy + dy, w * 2, 1, r, g, b, a );
	}
}

// One team source for the visibility gate and for the draw colours. The engine
// snapshot g_PlayerInfoList[].team is refreshed only when the scoreboard is
// drawn or a TeamInfo message arrives, so fall back to the value the scoreboard
// stores for the same player. Unknown (0) stays unknown rather than becoming an
// enemy: both sides comparing equal keeps a teammate cyan and visible.
static int RadarTeamOf( int i )
{
	if( i <= 0 || i > MAX_PLAYERS )
		return 0;
	// TeamInfo is pushed on every team change, while the engine snapshot is a
	// cache filled only when the scoreboard is drawn, so the message value wins
	// when both are set.
	const int iMsgTeam = g_PlayerExtraInfo[i].teamnumber;
	if( iMsgTeam > 0 )
		return iMsgTeam;
	return g_PlayerInfoList[i].team;
}

void CHudRadarModern::DrawPlayerBlip( int cx, int cy, int radius, int i, float flTime )
{
	if( i == m_iPlayerNum )
		return;
	// Observer snapshots are server positions and remain valid outside the PVS.
	if( g_iUser1 )
	{
		const oc2_spec::Radar *snapshot = oc2_spec::GetRadar(i);
		if( snapshot )
		{
			const Vector origin(snapshot->x, snapshot->y, snapshot->z);
			int x, y, pointRadius;
			if( CalcPoint(origin, x, y, pointRadius) ) return;
			const int size = BlipRadiusFor(radius);
			FillDisc(x, y, size + 1, 0, 0, 0, 180);
			FillDisc(x, y, size, snapshot->team == TEAM_CT ? 60 : 200,
				snapshot->team == TEAM_CT ? 124 : 145, snapshot->team == TEAM_CT ? 168 : 60, 255);
			return;
		}
	}

	const int iLocalTeam = RadarTeamOf( m_iPlayerNum );
	const int iTheirTeam = RadarTeamOf( i );
	// >= 0, not > 0: while the team is still unassigned on both sides they
	// compare equal and stay teammates, so the dot keeps its colour and stays
	// visible. Treating an unknown as an enemy turned teammates red.
	const bool bFriend = ( iLocalTeam >= 0 && iTheirTeam >= 0 && iTheirTeam == iLocalTeam );

	cl_entity_t *pEnt = gEngfuncs.GetEntityByIndex( i );
	const bool live = OC2_CurrentPlayerSnapshot( pEnt );
	const player_info_t *info=IEngineStudio.PlayerInfo?IEngineStudio.PlayerInfo(i-1):NULL;
	const bool teammatePosition=bFriend&&info&&info->userid>0&&
		m_aRadarTeammateUserid[i]==info->userid&&m_aRadarTeammateTeam[i]==iTheirTeam;

	// The X is drawn at the last position captured while the player was alive.
	if( IsPlayerDead( i ) )
	{
		const float deadFor = flTime - m_aRadarPlayerDeadAt[i];
		if( deadFor < 0.0f || deadFor >= 4.0f )
			return;
		// Stamped without a position (the victim died before this client ever
		// had it on the radar): draw no X, but still draw no living blip.
		if( m_aRadarPlayerDeadOrigin[i] == Vector( 0, 0, 0 ) )
			return;
		int x, y, deadRadius;
		if( CalcPoint( m_aRadarPlayerDeadOrigin[i], x, y, deadRadius ) )
			return;
		const int alpha = (int)( 255.0f * ( 1.0f - deadFor / 4.0f ) );
		static const unsigned char kX[5] = { 0x11, 0x0A, 0x04, 0x0A, 0x11 };
		// Exactly this side's blip colour, only dimmer. The old 190,210,235 /
		// 255,45,45 were whitened variants, so the X read as a different
		// colour from the dot the player had been tracking a second earlier.
		const int cr = bFriend ? 0 : 255;
		const int cg = bFriend ? 120 : 0;
		const int cb = bFriend ? 255 : 0;
		// 4 px per dot (20x20). Both passes share one grid: the outline dot is
		// only two pixels larger and shifted by -1, which is what yields a 1 px
		// rim. Scaling the pitch as well made the rim 2-6 px wide and off
		// centre. Rims first, then the colour, so no rim covers a dot.
		const int s = 4;
		const int ox = x - ( 5 * s ) / 2;
		const int oy = y - ( 5 * s ) / 2;
		const int rimAlpha = ( alpha * 3 ) / 4;
		for( int row = 0; row < 5; ++row )
			for( int col = 0; col < 5; ++col )
				if( kX[row] & ( 0x10 >> col ) )
					FillRGBABlend( ox + col * s - 1, oy + row * s - 1, s + 2, s + 2,
						0, 0, 0, rimAlpha );
		for( int row = 0; row < 5; ++row )
			for( int col = 0; col < 5; ++col )
				if( kX[row] & ( 0x10 >> col ) )
					FillRGBABlend( ox + col * s, oy + row * s, s, s, cr, cg, cb, alpha );
		return;
	}

	// Ghost first: the entity may already be out of the PVS, and the whole
	// point of a ghost is that it survives the entity going away.
	const float ghostAge = flTime - m_aRadarEnemyLastSeen[i];
	const bool ghost = !bFriend && m_bRadarGating && m_aRadarEnemyConfirmed[i] &&
		!m_aRadarEnemyVisible[i] && m_aRadarEnemySeen[i] && ghostAge >= 0.0f && ghostAge < 6.0f;

	if( !live && !ghost && !teammatePosition )
		return;
	if( !bFriend && m_bRadarGating && !m_aRadarEnemyVisible[i] && !ghost )
		return;

	// One colour per side, one size per marker: teammates cyan, enemies red,
	// the bomb carrier gold, all hand drawn through the blend path.
	const bool c4Carrier = bFriend &&
		iLocalTeam == TEAM_TERRORIST &&
		g_PlayerExtraInfo[i].has_c4;
	const int r = c4Carrier ? 255 : ( bFriend ? 0 : 255 );
	const int g = c4Carrier ? 200 : ( bFriend ? 120 : 0 );
	const int b = c4Carrier ? 40 : ( bFriend ? 255 : 0 );

	// A ghost fades by alpha now that the dot is blended: dimming the RGB on
	// top of an additive pass shifted the hue as it aged.
	int alpha = 255;
	if( !bFriend && !m_aRadarEnemyVisible[i] )
		alpha = (int)( 255.0f * ( 1.0f - ghostAge / 6.0f ) );

	int x, y;
	// NOT g_PlayerExtraInfo[i].origin: nothing in this client ever writes
	// that field, so it stays (0,0,0) and every blip piled up on the same
	// map pixel a third of the way up from the centre. The live entity is
	// the only populated source, same as CHudSpectator uses; a ghost uses the
	// position captured on the last clear sight.
	const Vector &vecBlip = !live&&teammatePosition ? m_aRadarTeammateOrigin[i] : ( !live || ( !bFriend && !m_aRadarEnemyVisible[i] ) )
		? m_aRadarEnemyLastOrigin[i] : pEnt->origin;
	if( CalcPoint( vecBlip, x, y, radius ) )
		return;

	// Dark rim first: it is what keeps a light dot readable on a light floor.
	const int iBlipR = BlipRadiusFor( radius );
	FillDisc( x, y, iBlipR + 1, 0, 0, 0, ( alpha * 3 ) / 5 );
	FillDisc( x, y, iBlipR, r, g, b, alpha );
	if( c4Carrier )
		FillRGBABlend( x - 2, y - 2, 5, 5, 255, 255, 255, ( alpha * 4 ) / 5 );
}

bool CHudRadarModern::ObjectivePoint( const Vector &world, int halfSize, int &x, int &y )
{
	if( !m_bOverviewValid || m_flPixelsPerTile <= 0.0f || m_iRadius <= halfSize ) return false;
	int radius;
	if( CalcPoint( world, x, y, radius ) ) return false;
	const int dx = x - m_iCenterX, dy = y - m_iCenterY;
	const int inset = radius - halfSize;
	if( m_bFullMap ) return abs(dx) <= inset && abs(dy) <= inset;
	return dx * dx + dy * dy <= inset * inset;
}

void CHudRadarModern::DrawObjectives( int localTeam )
{
	if( localTeam != TEAM_CT ) return;
	for( int i = 0; i < m_iObjectiveCount; ++i )
	{
		const Objective &site = m_Objectives[i];
		if( site.bomb ) continue;
		float x[RADAR_CLIP_MAX], y[RADAR_CLIP_MAX], u[RADAR_CLIP_MAX], v[RADAR_CLIP_MAX];
		const int n = ProjectRescueRegion( site.boundsMin, site.boundsMax, x, y, u, v );
		if( n < 3 ) continue;
		float low = y[0], high = y[0];
		for( int k = 1; k < n; ++k ) { low = min( low, y[k] ); high = max( high, y[k] ); }
		// FillRGBABlend preserves real alpha; triAPI forces alpha to 1 on this
		// engine and would make a translucent region an opaque block.
		for( int row = (int)ceilf( low ); row <= (int)floorf( high ); ++row )
		{
			const float scan = row + 0.5f;
			float left = 1e9f, right = -1e9f;
			for( int k = 0; k < n; ++k )
			{
				const int j = ( k + 1 ) % n;
				if( ( y[k] <= scan && y[j] > scan ) || ( y[j] <= scan && y[k] > scan ) )
				{
					const float cross = x[k] + ( scan - y[k] ) * ( x[j] - x[k] ) / ( y[j] - y[k] );
					left = min( left, cross ); right = max( right, cross );
				}
			}
			const int from = (int)ceilf( left ), to = (int)floorf( right );
			if( from <= to ) FillRGBABlend( from, row, to - from + 1, 1, 25, 175, 165, 38 );
		}
		if( gHUD.m_WhiteTex )
		{
			gRenderAPI.GL_SelectTexture( 0 );
			gRenderAPI.GL_Bind( 0, gHUD.m_WhiteTex );
		}
		gEngfuncs.pTriAPI->RenderMode( kRenderTransAlpha );
		gEngfuncs.pTriAPI->CullFace( TRI_NONE );
		for( int pass = 0; pass < 2; ++pass )
		{
			const float half = pass ? 1.0f : 2.0f;
			gEngfuncs.pTriAPI->Color4ub( pass ? 45 : 0, pass ? 230 : 0, pass ? 215 : 0, 255 );
			gEngfuncs.pTriAPI->Begin( TRI_QUADS );
			for( int k = 0; k < n; ++k )
			{
				const int j = ( k + 1 ) % n;
				// UVs identify the original rectangle sides. Circle-clipping
				// edges are viewport cuts, not physical rescue-zone borders.
				const bool originalEdge =
					( fabsf( u[k] ) < 0.0001f && fabsf( u[j] ) < 0.0001f ) ||
					( fabsf( u[k] - 1 ) < 0.0001f && fabsf( u[j] - 1 ) < 0.0001f ) ||
					( fabsf( v[k] ) < 0.0001f && fabsf( v[j] ) < 0.0001f ) ||
					( fabsf( v[k] - 1 ) < 0.0001f && fabsf( v[j] - 1 ) < 0.0001f );
				if( !originalEdge ) continue;
				const float dx = x[j] - x[k], dy = y[j] - y[k];
				const float length = sqrtf( dx * dx + dy * dy );
				if( length < 0.001f ) continue;
				const float nx = -dy * half / length, ny = dx * half / length;
				gEngfuncs.pTriAPI->Vertex3f( x[k] + nx, y[k] + ny, 0 );
				gEngfuncs.pTriAPI->Vertex3f( x[j] + nx, y[j] + ny, 0 );
				gEngfuncs.pTriAPI->Vertex3f( x[j] - nx, y[j] - ny, 0 );
				gEngfuncs.pTriAPI->Vertex3f( x[k] - nx, y[k] - ny, 0 );
			}
			gEngfuncs.pTriAPI->End();
		}
		gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
	}
}

int CHudRadarModern::ProjectRescueRegion( const Vector &mins, const Vector &maxs, float *x, float *y,
	float *edgeU, float *edgeV ) const
{
	if( !m_bOverviewValid || m_flPixelsPerTile <= 0.0f || m_iRadius <= 8 || !x || !y ) return 0;
	for( int axis = 0; axis < 3; ++axis )
		if( !std::isfinite( mins[axis] ) || !std::isfinite( maxs[axis] ) || maxs[axis] < mins[axis] ) return 0;
	if( maxs[0] <= mins[0] || maxs[1] <= mins[1] ) return 0;
	float selfX, selfY;
	if( m_bFullMap ) { selfX = m_iOverviewTilesX * 0.5f; selfY = m_iOverviewTilesY * 0.5f; }
	else WorldToTile( gHUD.m_vecOrigin, selfX, selfY );
	float u[RADAR_CLIP_MAX] = {}, v[RADAR_CLIP_MAX] = {};
	u[1] = u[2] = 1.0f; v[2] = v[3] = 1.0f;
	for( int k = 0; k < 4; ++k )
	{
		const Vector corner( k == 0 || k == 3 ? mins[0] : maxs[0],
			k < 2 ? mins[1] : maxs[1], mins[2] );
		float tx, ty;
		WorldToTile( corner, tx, ty );
		const float dx = ( tx - selfX ) * m_flPixelsPerTile;
		const float dy = ( ty - selfY ) * m_flPixelsPerTile;
		x[k] = m_iCenterX + dx * m_flTileA - dy * m_flTileB;
		y[k] = m_iCenterY + dx * m_flTileB + dy * m_flTileA;
		if( !std::isfinite( x[k] ) || !std::isfinite( y[k] ) ) return 0;
	}
	const int n = m_bFullMap ? ClipPolyToSquare(x,y,u,v,4,(float)m_iCenterX,(float)m_iCenterY,(float)m_iRadius-5.0f)
		: ClipPolyToCircle( x, y, u, v, 4, x, y, u, v, (float)m_iCenterX, (float)m_iCenterY, (float)m_iRadius - 5.0f );
	if( edgeU ) memcpy( edgeU, u, n * sizeof( float ) );
	if( edgeV ) memcpy( edgeV, v, n * sizeof( float ) );
	return n >= 3 ? n : 0;
}

/*
========================
DrawObjectiveLabels

Runs after the bomb and hostage blips so a planted C4 icon can never hide
the site letter. White halo first, dark glyph on top, so the letter stays
legible over the gold marker and over a player blip.
========================
*/
void CHudRadarModern::DrawSiteLetter( int cx, int cy, char letter, int scale, int red, int green, int blue )
{
	static const unsigned char kA[7] = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
	static const unsigned char kB[7] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
	const unsigned char *rows = ( letter == 'A' ) ? kA : kB;
	const int w = 5 * scale;
	const int h = 7 * scale;
	const int ox = cx - w / 2;
	const int oy = cy - h / 2;

	// Outline: each glyph dot painted one pixel larger, offset by -1, so the
	// black forms a 1 px rim. FillRGBA is additive in this engine, so pure
	// black contributed nothing and the rim never showed; the blend path is
	// the only one that can actually darken the overview underneath.
	for( int row = 0; row < 7; ++row )
		for( int col = 0; col < 5; ++col )
			if( rows[row] & ( 0x10 >> col ) )
				FillRGBABlend( ox + col * scale - 1, oy + row * scale - 1,
					scale + 2, scale + 2, 0, 0, 0, 190 );

	for( int row = 0; row < 7; ++row )
		for( int col = 0; col < 5; ++col )
			if( rows[row] & ( 0x10 >> col ) )
				FillRGBA( ox + col * scale, oy + row * scale,
					scale, scale, red, green, blue, 245 );
}

void CHudRadarModern::DrawObjectiveLabels()
{
	for( int i = 0; i < m_iObjectiveCount; ++i )
	{
		const Objective &site = m_Objectives[i];
		if( site.label != 'A' && site.label != 'B' ) continue;
		if( !site.bomb )
		{
			if( RadarTeamOf( m_iPlayerNum ) != TEAM_CT ) continue;
			// The saved origin is the world-space centre of these bounds.
			// Cull the whole glyph at the edge; never move it with the clipped area.
			int x, y;
			if( !ObjectivePoint( site.origin, 12, x, y ) ) continue;
			DrawSiteLetter( x, y, site.label, 3, 180, 255, 245 );
			continue;
		}
		int x, y;
		if( !ObjectivePoint( site.origin, 12, x, y ) ) continue;
		DrawSiteLetter( x, y, site.label, 3 );
	}
}

void CHudRadarModern::DrawBomb( int localTeam, float flTime )
{
	const extra_player_info_t &bomb = g_PlayerExtraInfo[33];
	if( bomb.dead || !bomb.radarflashes ) return;
	const bool planted = bomb.playerclass == 1;
	if( !planted && ( localTeam != m_iBombDropTeam ||
		( localTeam != TEAM_TERRORIST && localTeam != TEAM_CT ) ) ) return;
	// T requires a later box update; CT requires a confirmed sighting.
	if( !planted && !m_bBombDropPositionKnown ) return;
	float dropAlpha = 1.0f;
	if( !planted && localTeam == TEAM_CT )
	{
		const float age = flTime - m_flBombLastDropMessage;
		if( age < 0.0f || age >= 8.0f ) return;
		dropAlpha = 1.0f - age / 8.0f;
	}
	const Vector vecBomb = planted ? bomb.origin : m_vecBombAccepted;
	if( vecBomb == Vector( 0, 0, 0 ) ) return;
	CClientSprite *sprite = s_spr[SPR_C4];
	// The planted icon keeps its native 32x32 size, so its half size is the
	// culling margin; everything else is blip sized.
	const int halfSize = planted
		? ( ( sprite && sprite->spr ) ? max( sprite->rect.Width(), sprite->rect.Height() ) / 2 : 11 )
		: BlipRadiusFor( m_iRadius ) + 1;
	int x, y;
	if( !ObjectivePoint( vecBomb, halfSize, x, y ) ) return;
	m_vecBombDrawOrigin = vecBomb;
	m_bBombDrawn = true;
	m_flBombDrawTime = flTime;
	const bool bright = ( (int)( flTime * 2.0f ) & 1 ) == 0;

	if( !planted )
	{
		// Dropped: amber, the same size as a player dot, no backing plate.
		const int gg = bright ? 175 : 115;
		const int iBlipR = BlipRadiusFor( m_iRadius );
		FillDisc( x, y, iBlipR + 1, 0, 0, 0, int( 140 * dropAlpha ) );
		FillDisc( x, y, iBlipR, 255, gg, 20, int( 240 * dropAlpha ) );
		FillRGBABlend( x - 2, y - 2, 5, 5, 255, 255, 255, int( 235 * dropAlpha ) );
		return;
	}

	// Planted: the original C4 icon, centred on the bomb and with no plate.
	// The 34x34 backing was the "black block"; the icon itself was fine, it
	// only flew to the screen edge while the pixel x was being passed into
	// the frame argument of SPR_DrawAdditive.
	const int gg = bright ? 50 : 110;
	if( sprite && sprite->spr )
	{
		// The ic4 image is stored mirrored left-to-right, so the engine blit
		// draws it backwards. Draw the quad by hand with the u axis reversed;
		// v stays top-to-bottom because the vertical order is correct.
		const model_t *model = gEngfuncs.GetSpritePointer( sprite->spr );
		const int atlasW = SPR_Width( sprite->spr, 0 );
		const int atlasH = SPR_Height( sprite->spr, 0 );
		const wrect_t &rc = sprite->rect;
		const float flW = (float)( rc.right - rc.left );
		const float flH = (float)( rc.bottom - rc.top );
		if( model && atlasW > 0 && atlasH > 0 && flW > 0.0f && flH > 0.0f )
		{
			if( gRenderAPI.GL_SelectTexture ) gRenderAPI.GL_SelectTexture( 0 );
			if( gEngfuncs.pTriAPI->SpriteTexture( const_cast<model_s *>( model ), 0 ) )
			{
				const float flLeft = (float)x - flW * 0.5f;
				const float flTop = (float)y - flH * 0.5f;
				const float u0 = (float)rc.left / (float)atlasW;
				const float u1 = (float)rc.right / (float)atlasW;
				const float v0 = (float)rc.top / (float)atlasH;
				const float v1 = (float)rc.bottom / (float)atlasH;
				gEngfuncs.pTriAPI->RenderMode( kRenderTransAdd );
				gEngfuncs.pTriAPI->CullFace( TRI_NONE );
				gEngfuncs.pTriAPI->Color4ub( 255, gg, 30, 255 );
				gEngfuncs.pTriAPI->Begin( TRI_QUADS );
				gEngfuncs.pTriAPI->TexCoord2f( u1, v0 );
				gEngfuncs.pTriAPI->Vertex3f( flLeft, flTop, 0 );
				gEngfuncs.pTriAPI->TexCoord2f( u1, v1 );
				gEngfuncs.pTriAPI->Vertex3f( flLeft, flTop + flH, 0 );
				gEngfuncs.pTriAPI->TexCoord2f( u0, v1 );
				gEngfuncs.pTriAPI->Vertex3f( flLeft + flW, flTop + flH, 0 );
				gEngfuncs.pTriAPI->TexCoord2f( u0, v0 );
				gEngfuncs.pTriAPI->Vertex3f( flLeft + flW, flTop, 0 );
				gEngfuncs.pTriAPI->End();
				gEngfuncs.pTriAPI->Color4ub( 255, 255, 255, 255 );
				gEngfuncs.pTriAPI->RenderMode( kRenderNormal );
				gEngfuncs.pTriAPI->CullFace( TRI_FRONT );
				return;
			}
		}
		// No sprite handle: the engine blit still shows the state, mirrored.
		SPR_Set( sprite->spr, 255, gg, 30 );
		SPR_DrawAdditive( 0, x - sprite->rect.Width() / 2,
			y - sprite->rect.Height() / 2, &sprite->rect );
		return;
	}

	// Icon missing on disk: a pulsing ring still carries the state.
	for( int dy = -10; dy <= 10; ++dy )
	{
		const int outer = (int)( sqrtf( 100.0f - (float)( dy * dy ) ) + 0.5f );
		const int inner = ( dy > -7 && dy < 7 )
			? (int)( sqrtf( 49.0f - (float)( dy * dy ) ) + 0.5f ) : 0;
		if( outer <= inner ) continue;
		if( inner > 0 )
		{
			FillRGBABlend( x - outer, y + dy, outer - inner, 1, 255, gg, 30, 245 );
			FillRGBABlend( x + inner, y + dy, outer - inner, 1, 255, gg, 30, 245 );
		}
		else
			FillRGBABlend( x - outer, y + dy, outer * 2, 1, 255, gg, 30, 245 );
	}
}

void CHudRadarModern::DrawHostages( int localTeam, float flTime )
{
	if( localTeam != TEAM_CT && localTeam != TEAM_TERRORIST ) return;
	const float now = gEngfuncs.GetClientTime();
	for( int i = 1; i <= MAX_HOSTAGES; ++i )
	{
		const oc2_hostage::State &hostage = oc2_hostage::Get(i);
		if( !hostage.valid || !hostage.hasPosition || (hostage.expires > 0 && now >= hostage.expires) ) continue;
		const bool dead = hostage.status == oc2_hostage::Dead;
		const bool carried = hostage.status == oc2_hostage::Carried;
		const bool following = hostage.status == oc2_hostage::Following;
		const bool rescued = hostage.status == oc2_hostage::Rescued;
		const bool terminal = dead || rescued || hostage.status == oc2_hostage::LegacyTerminal;
		const float duration = dead ? 10.0f : rescued ? 2.5f : 1.5f;
		int alpha = terminal ? (int)(255.0f * (hostage.expires - now) / duration) : 255;
		if( alpha < 0 ) alpha = 0; if( alpha > 255 ) alpha = 255;
		const int dot = max( 3, BlipRadiusFor(m_iRadius) / 2 );
		const int halfSize = dead ? 8 : dot + 3;
		Vector drawOrigin = hostage.origin;
		if( carried )
		{
			const int slot = hostage.carrier;
			if( !hostage.extended || slot < 1 || slot > 32 || slot > gEngfuncs.GetMaxClients() ||
				hostage.userid <= 0 || RadarTeamOf( slot ) != TEAM_CT || !IEngineStudio.PlayerInfo ) continue;
			const player_info_t *info = IEngineStudio.PlayerInfo( slot - 1 );
			if( !info || info->userid != hostage.userid ) continue;
			if( slot == m_iPlayerNum )
				drawOrigin = gHUD.m_vecOrigin; // predicted local position, same as radar centre
			else if( IEngineStudio.GetPlayerState )
			{
				const cl_entity_t *local = gEngfuncs.GetLocalPlayer();
				const cl_entity_t *carrier = gEngfuncs.GetEntityByIndex( slot );
				const entity_state_t *current = IEngineStudio.GetPlayerState( slot - 1 );
				const entity_state_t *localState = local && local->index > 0 && local->index <= gEngfuncs.GetMaxClients()
					? IEngineStudio.GetPlayerState( local->index - 1 ) : NULL;
				// Do not use a stale PVS entity after its last network frame.
				if( carrier && carrier->player && current && localState && localState->messagenum > 0 &&
					current->number == slot && current->messagenum == localState->messagenum &&
					carrier->curstate.messagenum == current->messagenum )
					drawOrigin = carrier->origin;
			}
		}
		int x, y;
		if( !ObjectivePoint( drawOrigin, halfSize, x, y ) ) continue;
		if( dead )
		{
			for( int k = -5; k <= 5; ++k )
			{
				FillRGBABlend( x+k-1, y+k-1, 3, 3, 255, 35, 35, alpha );
				FillRGBABlend( x+k-1, y-k-1, 3, 3, 255, 35, 35, alpha );
			}
		}
		else
		{
			const int r = carried ? 45 : rescued ? 70 : 205;
			const int g = carried ? 235 : rescued ? 225 : 210;
			const int b = carried ? 255 : rescued ? 95 : 210;
			FillDisc( x, y, dot, r, g, b, alpha );
			if( carried || following )
				for( int n = 0; n < 32; ++n )
				{
					const float a = n * (2.0f * (float)M_PI / 32.0f);
					FillRGBABlend( x + (int)(cosf(a)*(dot+3)), y + (int)(sinf(a)*(dot+3)), 1, 1, r, g, b, alpha );
				}
		}
		oc2_hostage::MarkShown(i);
	}
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
	oc2_hostage::BeginRadarFrame(gEngfuncs.GetClientTime());
	m_bBombDrawn = false;
	if( !IsEnabled() )
		return 1;

	const bool observing = g_iUser1 != 0 || gEngfuncs.IsSpectateOnly();
	if( !observing && ( ( gHUD.m_iHideHUDDisplay & HIDEHUD_HEALTH )
		|| !( gHUD.m_iWeaponBits & (1<<WEAPON_SUIT) ) || gHUD.m_fPlayerDead ) )
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
	// Source's spectator modes use the square overview even with Tab released.
	m_bFullMap = (observing && OC2_ObserverFullMap()) || gHUD.m_Scoreboard.ShouldDrawScoreboard();

	// The mobile build computes the centre as
	//     centre = (cl_radar_pos_x + radius) - layoutBase
	// so cl_radar_pos_x/y are an *offset* from the fixed corner placement,
	// not an absolute centre. Reproduce that: anchor the radar at the
	// top-left corner and let the cvars nudge it.
	m_iRadius   = (int)( ScreenHeight * m_pNewRadarSize->value );
	m_iCenterX  = m_iRadius + (int)m_pRadarPosX->value;
	m_iCenterY  = m_iRadius + (int)m_pRadarPosY->value;
	if( m_bFullMap )
	{
		if( observing && !gHUD.m_Scoreboard.ShouldDrawScoreboard() )
		{
			const auto layout = spectator_layout::Make(ScreenWidth,ScreenHeight).radar;
			m_iRadius = int(layout.w) / 2;
			m_iCenterX = int(layout.x) + m_iRadius;
			m_iCenterY = int(layout.y) + m_iRadius;
		}
		else
		{
			const auto layout = radar_fullmap_layout::Make(ScreenWidth, ScreenHeight);
			m_iRadius = layout.size / 2;
			m_iCenterX = layout.x + m_iRadius;
			m_iCenterY = layout.y + m_iRadius;
		}
	}

	// ---- overview background: re-read only when the map changes,
	//        exactly like the mobile build's Reset()
	LoadOverview( gEngfuncs.pfnGetLevelName() );
	LoadObjectives( gEngfuncs.pfnGetLevelName() );

	// ---- how much of the map the window covers, and the resulting scale
	if( s_cl_radar_view )
	{
		s_flRadarViewFraction = s_cl_radar_view->value;
		if( s_flRadarViewFraction <= 0.0f || s_flRadarViewFraction > 1.0f )
			s_flRadarViewFraction = 1.0f;
	}

	// everything below shares this one transform
	UpdateRadarTransform( m_iCenterX, m_iCenterY, m_iRadius );
	m_flLayoutDrawTime = gEngfuncs.GetClientTime();

	// ---- background: the blurred copy of the screen behind the radar, or a
	// flat plate when blur is unavailable. Exactly one of the two, and the
	// background always goes down first -- it was previously drawn *after*
	// the disc and the map tiles, which covered both.
	//
	// The capture reads the previous frame, so it is unaffected by whatever
	// we draw this frame; throttle it because a readPixels plus a box blur is
	// not free.
	bool bHasBackground = false;

	// Registration must NOT test for an existing background texture: the
	// texture is created by the first capture, and the first capture only
	// happens once the client has registered its parameters. Gating on the
	// texture makes that a chicken-and-egg deadlock -- the feature can never
	// switch itself on. DrawBlurredBackground reports whether it had
	// anything to show, which is the only thing that should fall back to the
	// flat plate.
	if( !m_bFullMap && g_iRadarAPIVersion >= 4
		&& gRadarAPI.pfnSetRadarCaptureParams
		&& s_cl_blur_radar_bg->value == 0.0f )
	{
		// Tell the engine where the radar is; it does the capture itself at
		// the post-view hook, while the buffer still holds only the 3D
		// scene. Capturing from here would read back a frame that already
		// has the radar on it, which blurs the radar into its own backdrop.
		int iBlur = s_cl_radar_blur_radius ? (int)s_cl_radar_blur_radius->value : 0;
		gRadarAPI.pfnSetRadarCaptureParams( iBlur, m_iCenterX, m_iCenterY, m_iRadius );

		bHasBackground = DrawBlurredBackground( m_iCenterX, m_iCenterY, m_iRadius );
	}

	// flat disc first, so map-less areas read as background instead of
	// showing the game through the HUD
	if( m_bFullMap )
		FillRGBABlend(m_iCenterX-m_iRadius,m_iCenterY-m_iRadius,m_iRadius*2,m_iRadius*2,12,18,24,
			observing && !gHUD.m_Scoreboard.ShouldDrawScoreboard() ? 140 : 225);
	else if( !bHasBackground )
		DrawRadarDisc( m_iCenterX, m_iCenterY, m_iRadius );

	// ---- map tiles, anchored on the player
	if( m_bOverviewValid )
		DrawOverviewLayer( m_iCenterX, m_iCenterY, m_iRadius );

	// temporary on-screen diagnostics (removed once the radar is confirmed)
	if( s_cl_radar_debug && s_cl_radar_debug->value > 0 )
	{
		char szDbg[256];
		float selfX, selfY;
		WorldToTile( gHUD.m_vecOrigin, selfX, selfY );
		const cl_entity_t *view = gEngfuncs.GetLocalPlayer();
		snprintf( szDbg, sizeof( szDbg ),
			"radar: obj=%d src=%d pl=%.0f,%.0f pv=%.0f,%.0f tile=%.2f,%.2f px/t=%.0f h=%d,%d rot=%d",
			m_iObjectiveCount, m_iObjectiveSource,
			gHUD.m_vecOrigin[0], gHUD.m_vecOrigin[1],
			view ? view->origin[0] : 0.0f, view ? view->origin[1] : 0.0f,
			selfX, selfY, m_flPixelsPerTile, g_HostageInfo[1].dead ? 1 : 0, g_HostageInfo[1].radarflashes,
			s_cl_radar_orient ? (int)s_cl_radar_orient->value : 0 );
		DrawUtils::DrawConsoleString( 4, 260, szDbg );
	}

	// ---- blurred screen background: captured and painted at the top of this
	// function, before the disc and the map tiles.

	// ---- radar body
	int r = (int)( m_pNewRadarR->value * 255 );
	int g = (int)( m_pNewRadarG->value * 255 );
	int b = (int)( m_pNewRadarB->value * 255 );
	int a = (int)( m_pNewRadarA->value * 255 );

	// Border in the configured colour. The radar's iradio icon is hidden.
	// Skipped while the bomb warning is up: that ring is drawn at alpha 100 in
	// its dim phase and a white border underneath turns it pink.
	{
		const extra_player_info_t &bombInfo = g_PlayerExtraInfo[33];
		const bool bWarning = !bombInfo.dead && bombInfo.radarflashes &&
			bombInfo.playerclass == 1 &&
			!( bombInfo.origin[0] == 0 && bombInfo.origin[1] == 0 && bombInfo.origin[2] == 0 );
		if( m_bFullMap )
		{
			const int left=m_iCenterX-m_iRadius, top=m_iCenterY-m_iRadius, size=m_iRadius*2;
			const bool observerFrame=observing && !gHUD.m_Scoreboard.ShouldDrawScoreboard();
			const int edge=observerFrame ? 1 : 2, alpha=observerFrame ? 70 : a;
			FillRGBABlend(left,top,size,edge,r,g,b,alpha); FillRGBABlend(left,top+size-edge,size,edge,r,g,b,alpha);
			FillRGBABlend(left,top,edge,size,r,g,b,alpha); FillRGBABlend(left+size-edge,top,edge,size,r,g,b,alpha);
		}
		else if( !bWarning )
			DrawRadarFrame( m_iCenterX, m_iCenterY, m_iRadius, r, g, b, a, true );
	}

	// ---- circular mask
	if( !m_bFullMap ) DrawRadarMask( m_iCenterX, m_iCenterY, m_iRadius );
	// One source for the local team as well: RadarTeamOf prefers the TeamInfo
	// value and falls back to the engine snapshot. The old inline copy here
	// preferred the snapshot and disagreed with the blip colours.
	const int localTeam = RadarTeamOf( m_iPlayerNum );
	// Rescue footprints tint only the map; players and state markers stay on top.
	DrawObjectives( localTeam );

	// ---- the local player, pinned to the centre
	m_iSelfDrawX = m_iSelfDrawY = -1;
	if( !observing )
	{
		int x=m_iCenterX, y=m_iCenterY, radius=m_iRadius;
		if( !m_bFullMap || !CalcPoint(gHUD.m_vecOrigin,x,y,radius) )
		{
			m_iSelfDrawX=x; m_iSelfDrawY=y;
			DrawSelfMarker(x,y,radius);
		}
	}

	// ---- player blips
	UpdateEnemyVisibility( flTime );

	int maxPlayers = gEngfuncs.GetMaxClients();
	if( maxPlayers > kRadarMaxPlayers ) maxPlayers = kRadarMaxPlayers;
	for( int i = 1; i <= maxPlayers; i++ )
		DrawPlayerBlip( m_iCenterX, m_iCenterY, m_iRadius, i, flTime );
	DrawHostages( localTeam, flTime );
	// Planted icon first, site letters on top: the icon is a native 32x32 and
	// painted last used to cover the 15x21 glyph, which is the only thing that
	// says which site it is.
	DrawBomb( localTeam, flTime );
	DrawObjectiveLabels();
	if( observing && m_bFullMap && m_szMapName[0] )
	{
		const char *name = strrchr(m_szMapName, '/');
		name = name ? name + 1 : m_szMapName;
		char mapLabel[64];
		snprintf(mapLabel, sizeof(mapLabel), "%s", name);
		char *extension = strrchr(mapLabel, '.');
		if( extension ) *extension = '\0';
		const float scale=spectator_layout::Make(ScreenWidth,ScreenHeight).scale;
		const float left=m_iCenterX-m_iRadius+3*scale, width=2*m_iRadius-6*scale;
		const float bottom=m_iCenterY+m_iRadius-3*scale, height=27*scale;
		FillRGBABlend(int(left),int(bottom-height),int(width),int(height),14,16,18,185);
		match_ui::Text(mapLabel,left+7*scale,bottom-height+4*scale,18*scale,{230,233,238},false,width-14*scale);
	}

	// ---- warning frame while alive with a bomb in hand
	if( !m_bFullMap ) DrawWarningFrame( m_iCenterX, m_iCenterY, m_iRadius, flTime );

	return 1;
}

