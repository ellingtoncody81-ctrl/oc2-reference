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
	pRadarBindTexture       = ref.dllFuncs.GL_BindRadarTexture;
	pRadarFreeCapture       = ref.dllFuncs.GL_FreeRadarCapture;
	pRadarApplyBoxBlur      = ref.dllFuncs.GL_ApplyBoxBlur;

	if( !pRadarInitCapture || !pRadarCaptureBackground || !pRadarGetTexture
		|| !pRadarBindTexture || !pRadarFreeCapture || !pRadarApplyBoxBlur )
		return false;

	// renderer predates the radar API, or is a stub renderer
	return true;
}

qboolean CL_InitRadarInterface( void )
{
	pfnRadarInterface ExportToClient;
	static radar_engfuncs_t radarapi;
	qboolean bBlur;

	Con_Printf( "RadarAPI: entered, ref.initialized=%d, blurCvar=%d\n",
		(int)ref.initialized, ref.dllFuncs.GL_SetRadarBlurCvar ? 1 : 0 );

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
		radarapi.pfnCaptureRadarBackground = bBlur ? pRadarCaptureBackground : NULL;
		radarapi.pfnGetRadarBackgroundTexture = bBlur ? pRadarGetTexture : NULL;
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
	pRadarGetTexture        = NULL;
	pRadarBindTexture       = NULL;
	pRadarFreeCapture       = NULL;
	pRadarApplyBoxBlur      = NULL;
}
