/*
radar_int.h - CS:GO radar background blur interface (engine -> client)

Reconstructed from the Android build of Offensive Classic 2
(apk/lib/armeabi-v7a/libxash.so), where the engine exposes the radar
capture helpers to the client through the same mechanism Xash3D already
uses for mobility_int.h.

The radar background is NOT rendered through an FBO. The engine reads the
on-screen pixels around the radar back into a CPU buffer, box-blurs them,
and uploads the result as a texture the client then samples.

This file is placed under the Unlicense, like its mobility_int.h sibling.
*/

#pragma once
#ifndef RADAR_INT_H
#define RADAR_INT_H

#ifdef __cplusplus
extern "C" {
#endif

#define RADAR_API_VERSION   4
#define RADAR_CLIENT_EXPORT "HUD_RadarInterface"

// signatures of the renderer-side implementations (ref/gl/gl_radar.c);
// resolved by name through GL_GetProcAddress, so both sides can share them
typedef void  (*pfnInitRadarCapture)( void );
typedef void  (*pfnCaptureRadarBackground)( void );
typedef unsigned int (*pfnGetRadarBackgroundTex)( void );
typedef void  (*pfnBindRadarTexture)( void );
typedef void  (*pfnFreeRadarCapture)( void );
typedef int   (*pfnApplyBoxBlur)( const unsigned char *src, unsigned char *dst,
	int width, int height, int radius );

// lets the command layer hand its cl_radar_blur_resolution to the renderer,
// which reads ->value directly on every capture
typedef void  (*pfnSetRadarBlurCvar)( void *cv );

typedef struct radar_engfuncs_s
{
	// must equal RADAR_API_VERSION
	int version;

	// Allocate the 256 KB scratch buffer. Idempotent.
	void (*pfnInitRadarCapture)( void );

	// v4: the client registers where the radar is and how hard to blur, and
	// the engine does the capture itself at the one point in the frame where
	// the colour buffer still holds nothing but the 3D scene -- after the
	// view render, before CL_DrawHUD.
	//
	// Capturing during the HUD pass cannot work. The only complete image
	// available then is the previous frame, and the previous frame already
	// has the radar composited at that exact spot, so the backdrop comes
	// back as a blurred copy of the radar and looks like the flat plate it
	// was meant to replace. The engine throttles this on
	// cl_radar_blur_interval.
	void (*pfnSetRadarCaptureParams)( int blurRadius, int cx, int cy, int size );

	// Returns the GL texture holding the blurred background, 0 if none.
	unsigned int (*pfnGetRadarBackgroundTexture)( void );

	// v3: the screen rectangle (HUD coordinates, origin top-left) that the
	// background texture actually covers. The capture is a res*res block
	// centred on the radar, clamped to stay inside the framebuffer, so it is
	// only equal to the requested window when that window fits. The client
	// maps its quad through this rectangle instead of assuming it, which is
	// what keeps the backdrop aligned when the radar sits near an edge.
	// Returns 0 when nothing has been captured yet.
	int (*pfnGetRadarCaptureRect)( int *x, int *y, int *w, int *h );

	// glEnable + glBindTexture for the radar background, no-op if unset.
	void (*pfnBindRadarTexture)( void );

	// Release the scratch buffer and the texture.
	void (*pfnFreeRadarCapture)( void );

	// Separable two-pass box blur over an RGBA8888 buffer.
	// `radius` is clamped at the borders; each channel is averaged with
	// integer division. Exposed because CS:GO uses it for other HUD
	// elements too (scoreboard, death notice).
	int (*pfnApplyBoxBlur)( const unsigned char *src, unsigned char *dst,
		int width, int height, int radius );

	// v2: world-space bounding box of the loaded map, as stored in the BSP
	// world model. The overview .txt zoom/origin describe the *spectator*
	// framing convention (a fixed 8192/zoom world window), NOT how the .bmp
	// actually covers the map, so the radar cannot derive the overview
	// mapping from them. Returns 0 when no map is loaded.
	int (*pfnGetRadarMapBounds)( float *mins, float *maxs );
} radar_engfuncs_t;

// exported by the client, called by the engine once at init
// returns 0 on success, non-zero on error
typedef int (*pfnRadarInterface)( radar_engfuncs_t *radarapi );

#ifdef __cplusplus
}
#endif

#endif // RADAR_INT_H
