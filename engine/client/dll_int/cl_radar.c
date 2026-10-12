/*
cl_radar.c - hands the CS:GO radar blur API to the client

The renderer (ref/gl/gl_radar.c) implements the capture + box blur, but the
renderer DLL only exposes symbols through its GetRefAPI table. Rather than
grow that ABI we resolve the functions by name via GL_GetProcAddress, which
the engine already ships, and package them into the radar_engfuncs_t the
client consumes -- the same shape Xash3D uses for mobile_engfuncs_t.
*/

#include "common.h"
#include "client.h"
#include "library.h"
#include "radar_int.h"

CVAR_DEFINE_AUTO( cl_radar_blur_resolution, "64", FCVAR_ARCHIVE,
	"side of the square of screen pixels sampled for the radar background blur" );

CVAR_DEFINE_AUTO( cl_radar_blur_interval, "0", FCVAR_ARCHIVE,
	"seconds between radar background captures; 0 = every frame. A readPixels "
	"stalls the pipeline, so raise this if the frame rate suffers -- the "
	"backdrop then lags behind the view by that much" );

typedef void (*pfnSetRadarCaptureParams)( int blurRadius, int cx, int cy, int size );
typedef int (*pfnGetRadarCaptureRect)( int *x, int *y, int *w, int *h );

pfnSetRadarCaptureParams pRadarSetParams;
pfnGetRadarCaptureRect pRadarGetCaptureRect;

// what the client asked for last frame; the capture runs at the post-view
// hook, one frame after the client filled these in
static int i_radar_req[4];
static double fl_radar_last_capture;

// resolved out of the renderer at init
pfnInitRadarCapture       pRadarInitCapture;
pfnCaptureRadarBackground pRadarCaptureBackground;
pfnGetRadarBackgroundTex  pRadarGetTexture;
pfnBindRadarTexture       pRadarBindTexture;
pfnFreeRadarCapture       pRadarFreeCapture;
pfnApplyBoxBlur           pRadarApplyBoxBlur;

/*
========================
CL_RadarGetMapBounds
========================
The world model carries the map's brushwork bounding box, which is exactly the
region the overview .bmp was rendered from. de_dust2: mins (-2816 -1664 -320),
maxs (2048 3936 640) - 4864 x 5600 world units stretched across 1024 x 768
image pixels.

The overview .txt cannot supply this: its zoom/origin are the CS spectator
framing convention (the image is laid over a fixed 8192/zoom world square), not
a description of the bitmap, so anything derived from them lands the map in the
wrong place. Hence the export.
*/
static int CL_RadarGetMapBounds( float *mins, float *maxs )
{
	if( !cl.worldmodel || !mins || !maxs )
		return 0;

	VectorCopy( cl.worldmodel->mins, mins );
	VectorCopy( cl.worldmodel->maxs, maxs );
	return 1;
}

static qboolean R_RadarResolveSymbols( void )
{
	if( !ref.initialized )
		return false;

	pRadarInitCapture       = ref.dllFuncs.GL_InitRadarCapture;
	pRadarCaptureBackground = ref.dllFuncs.GL_CaptureRadarBackground;
	pRadarGetTexture        = ref.dllFuncs.GL_GetRadarBackgroundTexture;
	pRadarGetCaptureRect     = ref.dllFuncs.GL_GetRadarCaptureRect;
	pRadarSetParams          = ref.dllFuncs.GL_SetRadarCaptureParams;
	pRadarBindTexture       = ref.dllFuncs.GL_BindRadarTexture;
	pRadarFreeCapture       = ref.dllFuncs.GL_FreeRadarCapture;
	pRadarApplyBoxBlur      = ref.dllFuncs.GL_ApplyBoxBlur;

	// GL_GetRadarCaptureRect only exists in the ref_gl that ships with radar API
	// v3, and GL_SetRadarCaptureParams only in v4. An older renderer fills the
	// table with fewer entries and leaves the tail at whatever was there, so
	// check explicitly: without them the client would sample the backdrop
	// through an assumed rectangle, and the engine would never capture at the
	// point where the buffer is still just the world.
	if( !pRadarInitCapture || !pRadarCaptureBackground || !pRadarGetTexture
		|| !pRadarGetCaptureRect || !pRadarSetParams || !pRadarBindTexture
		|| !pRadarFreeCapture || !pRadarApplyBoxBlur )
	{
		if( pRadarInitCapture && pRadarApplyBoxBlur )
			Con_Reportf( S_ERROR "%s: renderer is older than radar API v%d, "
				"redeploy ref_gl.dll\n", __func__, RADAR_API_VERSION );
		return false;
	}

	// renderer predates the radar API, or is a stub renderer
	return true;
}

/*
========================
CL_RadarSetCaptureParams

Called by the client from its HUD pass. Only records the request -- the
buffer already has the radar drawn on it here, so reading it now would feed
the radar back into its own backdrop.
========================
*/
void CL_RadarSetCaptureParams( int blurRadius, int cx, int cy, int size )
{
	if( !pRadarSetParams )
		return;

	i_radar_req[0] = blurRadius;
	i_radar_req[1] = cx;
	i_radar_req[2] = cy;
	i_radar_req[3] = size;
}

/*
========================
CL_RadarCapturePending

Called from V_PostRender, before the HUD is composited. At that point the
colour buffer holds the finished 3D scene and nothing else, which is the only
place in the frame a clean backdrop can be sampled from.

Throttled because glReadPixels is a pipeline stall.
========================
*/
void CL_RadarCapturePending( void )
{
	if( !pRadarCaptureBackground || !pRadarSetParams )
		return;

	// nothing requested yet, or the client turned the feature off and never
	// registered a rect
	if( i_radar_req[3] <= 0 )
		return;

	if( host.realtime - fl_radar_last_capture < cl_radar_blur_interval.value )
		return;

	fl_radar_last_capture = host.realtime;
	pRadarSetParams( i_radar_req[0], i_radar_req[1], i_radar_req[2], i_radar_req[3] );
	pRadarCaptureBackground();
}

qboolean CL_InitRadarInterface( void )
{
	pfnRadarInterface ExportToClient;
	static radar_engfuncs_t radarapi;
	qboolean bBlur;

	Con_Printf( "RadarAPI: entered, ref.initialized=%d, blurCvar=%d\n",
		(int)ref.initialized, ref.dllFuncs.GL_SetRadarBlurCvar ? 1 : 0 );

	// CVAR_DEFINE_AUTO only declares the variable and marks it; nothing walks
	// the binary looking for those, so an unregistered convar keeps the
	// 0.0f that the macro initialised and never picks up the default string.
	// That is what silently pinned cl_radar_blur_resolution to 0, which the
	// renderer then clamps up to its 8x8 floor.
	Cvar_RegisterVariable( &cl_radar_blur_resolution );
	Cvar_RegisterVariable( &cl_radar_blur_interval );

	// tell the renderer where the cvar lives, it reads it directly
	if( ref.dllFuncs.GL_SetRadarBlurCvar )
		ref.dllFuncs.GL_SetRadarBlurCvar( &cl_radar_blur_resolution );

	bBlur = R_RadarResolveSymbols();

	// The map-bounds getter is engine-side and has nothing to do with the
	// renderer, so hand it over even when the blur half is unavailable.
	if( !bBlur )
		Con_Reportf( S_WARN "%s: radar blur unavailable in this renderer\n", __func__ );

	if(( ExportToClient = (pfnRadarInterface)COM_GetProcAddress( clgame.hInstance, RADAR_CLIENT_EXPORT )))
	{
		memset( &radarapi, 0, sizeof( radarapi ));

		radarapi.version    = RADAR_API_VERSION;
		radarapi.pfnInitRadarCapture       = bBlur ? pRadarInitCapture : NULL;
		// Point the client at the ENGINE wrapper, not at the renderer's
		// GL_SetRadarCaptureParams. The engine wrapper owns the request and
		// decides when to replay it at the post-view hook; handing the client
		// the raw renderer entry would fill the renderer's copy and leave
		// the engine's request permanently empty, so the capture would never
		// run -- with no error anywhere.
		radarapi.pfnSetRadarCaptureParams = bBlur ? CL_RadarSetCaptureParams : NULL;
		radarapi.pfnGetRadarBackgroundTexture = bBlur ? pRadarGetTexture : NULL;
		radarapi.pfnGetRadarCaptureRect   = bBlur ? pRadarGetCaptureRect : NULL;
		radarapi.pfnBindRadarTexture       = bBlur ? pRadarBindTexture : NULL;
		radarapi.pfnFreeRadarCapture       = bBlur ? pRadarFreeCapture : NULL;
		radarapi.pfnApplyBoxBlur           = bBlur ? pRadarApplyBoxBlur : NULL;
		radarapi.pfnGetRadarMapBounds     = CL_RadarGetMapBounds;

		if( !ExportToClient( &radarapi ))
		{
			Con_Reportf( "^2%s: initialized RadarAPI ^7ver. %i\n", __func__, RADAR_API_VERSION );
			return true;
		}
	}

	// client has no radar support -- the engine keeps working, radar blur is just off
	Con_Reportf( S_WARN "%s: client has no radar interface, blur disabled\n", __func__ );
	return false;
}

void CL_ShutdownRadarInterface( void )
{
	if( pRadarFreeCapture )
		pRadarFreeCapture();

	pRadarInitCapture       = NULL;
	pRadarCaptureBackground = NULL;
	memset( i_radar_req, 0, sizeof( i_radar_req ));
	fl_radar_last_capture = 0.0f;

	pRadarGetTexture        = NULL;
	pRadarGetCaptureRect    = NULL;
	pRadarSetParams         = NULL;
	pRadarBindTexture       = NULL;
	pRadarFreeCapture       = NULL;
	pRadarApplyBoxBlur      = NULL;
}
