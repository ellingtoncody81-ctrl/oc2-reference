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

	Con_Printf( "RadarAPI: entered, ref.initialized=%d, blurCvar=%d\n",
		(int)ref.initialized, ref.dllFuncs.GL_SetRadarBlurCvar ? 1 : 0 );

	// tell the renderer where the cvar lives, it reads it directly
	if( ref.dllFuncs.GL_SetRadarBlurCvar )
		ref.dllFuncs.GL_SetRadarBlurCvar( &cl_radar_blur_resolution );

	if( !R_RadarResolveSymbols() )
	{
		Con_Reportf( S_WARN "%s: radar blur unavailable in this renderer\n", __func__ );
		return false;
	}

	if(( ExportToClient = (pfnRadarInterface)COM_GetProcAddress( clgame.hInstance, RADAR_CLIENT_EXPORT )))
	{
		radarapi.version    = RADAR_API_VERSION;
		radarapi.pfnInitRadarCapture       = pRadarInitCapture;
		radarapi.pfnCaptureRadarBackground = pRadarCaptureBackground;
		radarapi.pfnGetRadarBackgroundTexture = pRadarGetTexture;
		radarapi.pfnBindRadarTexture       = pRadarBindTexture;
		radarapi.pfnFreeRadarCapture       = pRadarFreeCapture;
		radarapi.pfnApplyBoxBlur           = pRadarApplyBoxBlur;

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
