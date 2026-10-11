/*
gl_radar.c - CS:GO radar background blur

Reconstructed from the Android build of Offensive Classic 2
(apk/lib/armeabi-v7a/libxash.so: GL_InitRadarCapture,
GL_CaptureRadarBackground, GL_GetRadarBackgroundTexture,
GL_BindRadarTexture, GL_FreeRadarCapture, GL_ApplyBoxBlur).

The radar background is not an FBO render. The engine reads the pixels
already on screen around the radar back into a CPU buffer, box-blurs them
and uploads the result as a texture; the client then samples that texture
slightly oversized to get the CS:GO soft edge.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "gl_local.h"

#define RADAR_PIXEL_BUFFER_SIZE 0x40000	// 256 KB, caps res^2*4

static byte *p_radar_pixels;
static unsigned int i_radar_texture;
static poolhandle_t r_radar_pool;

// resolution of the blur sample buffer, owned by the command layer
// (see engine/client/dll_int/cl_radar.c); defaults to 64 if unset
convar_t *cl_radar_blur_resolution;

void GL_SetRadarBlurCvar( convar_t *cv )
{
	cl_radar_blur_resolution = cv;
}/*
====================
GL_ApplyBoxBlur

Separable two-pass box blur over an RGBA8888 buffer.
Pass 1 walks rows (horizontal), pass 2 walks columns (vertical).
Borders are clamped rather than mirrored and every channel is averaged
with integer division -- that integer division is what the Android build
does, and it shows up as slight darkening on bright edges.
====================
*/
int GL_ApplyBoxBlur( const byte *src, byte *dst, int width, int height, int radius )
{
	byte *tmp;
	int x, y, i;
	int rsum, gsum, bsum, asum, n;
	int x0, x1, y0, y1;
	const byte *p;
	byte *o;

	if( radius < 1 )
		return radius;

	if( !src || !dst || width <= 0 || height <= 0 )
		return radius;

	if( !r_radar_pool )
		r_radar_pool = Mem_AllocPool( "Radar Blur" );

	tmp = (byte *)Mem_Malloc( r_radar_pool, width * height * 4 );
	if( !tmp )
		return radius;

	// ---- pass 1: horizontal
	for( y = 0; y < height; y++ )
	{
		const byte *const_row = src + (size_t)y * width * 4;

		for( x = 0; x < width; x++ )
		{
			x0 = x - radius; if( x0 < 0 ) x0 = 0;
			x1 = x + radius; if( x1 > width - 1 ) x1 = width - 1;

			o = tmp + ((size_t)y * width + x) * 4;

			if( x1 < x0 )
			{
				o[0] = o[1] = o[2] = o[3] = 0;
				continue;
			}

			rsum = gsum = bsum = asum = n = 0;
			p = const_row + (size_t)x0 * 4;
			for( i = x0; i <= x1; i++, p += 4 )
			{
				rsum += p[0];
				gsum += p[1];
				bsum += p[2];
				asum += p[3];
				n++;
			}

			o[0] = rsum / n;
			o[1] = gsum / n;
			o[2] = bsum / n;
			o[3] = asum / n;
		}
	}

	// ---- pass 2: vertical
	for( y = 0; y < height; y++ )
	{
		y0 = y - radius; if( y0 < 0 ) y0 = 0;
		y1 = y + radius; if( y1 > height - 1 ) y1 = height - 1;

		for( x = 0; x < width; x++ )
		{
			o = dst + ((size_t)y * width + x) * 4;

			if( y1 < y0 )
			{
				o[0] = o[1] = o[2] = o[3] = 0;
				continue;
			}

			rsum = gsum = bsum = asum = n = 0;
			p = tmp + ((size_t)y0 * width + x) * 4;			for( i = y0; i <= y1; i++, p += (size_t)width * 4 )
			{
				rsum += p[0];
				gsum += p[1];
				bsum += p[2];
				asum += p[3];
				n++;
			}

			o[0] = rsum / n;
			o[1] = gsum / n;
			o[2] = bsum / n;
			o[3] = asum / n;
		}
	}

	Mem_Free( tmp );
	return radius;
}

/*
====================
GL_InitRadarCapture
====================
*/
void GL_InitRadarCapture( void )
{
	if( p_radar_pixels )
		return;

	if( !r_radar_pool )
		r_radar_pool = Mem_AllocPool( "Radar Blur" );

	p_radar_pixels = (byte *)Mem_Malloc( r_radar_pool, RADAR_PIXEL_BUFFER_SIZE );
	if( p_radar_pixels )
		memset( p_radar_pixels, 0, RADAR_PIXEL_BUFFER_SIZE );
}

/*
====================
GL_CaptureRadarBackground

Read the on-screen block centred on the radar, blur it, upload it as a
texture. (radius, cx, cy, size) -- the client passes the radar centre
and the radius it is about to draw at.
====================
*/
void GL_CaptureRadarBackground( int radius, int cx, int cy, int size )
{
	int res, x, y, scr_w, scr_h;
	size_t bufsize;
	byte *buf;
	GLint prevTex;

	if( !p_radar_pixels )
		GL_InitRadarCapture();
	if( !p_radar_pixels )
		return;

	res = cl_radar_blur_resolution ? (int)cl_radar_blur_resolution->value : 64;
	if( res < 8 )   res = 8;
	if( res > 512 ) res = 512;

	GL_GetSceneTargetSize( &scr_w, &scr_h );

	bufsize = (size_t)res * res * 4;
	buf = (byte *)Mem_Malloc( r_radar_pool, bufsize );
	if( !buf )
		return;

	pglReadBuffer( GL_BACK );
	pglPixelStorei( GL_PACK_ALIGNMENT, 1 );

	// GL screen origin is bottom-left; the Android code computes X first.
	x = scr_w - ( cy + size / 2 ) - res / 2;
	if( x < 0 ) x = 0;
	if( scr_w < x + res ) x = scr_w - res;

	y = cx - res / 2 + size / 2;
	if( y < 0 ) y = 0;
	if( scr_h < y + res ) y = scr_h - res;

	pglReadPixels( x, y, res, res, GL_RGBA, GL_UNSIGNED_BYTE, buf );

	if( radius < 1 )
		memcpy( p_radar_pixels, buf, bufsize );
	else
		GL_ApplyBoxBlur( buf, p_radar_pixels, res, res, radius );

	Mem_Free( buf );

	// (re)upload the blurred block
	pglGetIntegerv( GL_TEXTURE_BINDING_2D, &prevTex );

	if( !i_radar_texture )
		pglGenTextures( 1, &i_radar_texture );

	pglBindTexture( GL_TEXTURE_2D, i_radar_texture );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP );
	pglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP );
	pglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, res, res, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, p_radar_pixels );

	pglBindTexture( GL_TEXTURE_2D, prevTex );
	pglReadBuffer( GL_FRONT );
}

/*
====================
GL_GetRadarBackgroundTexture
====================
*/
unsigned int GL_GetRadarBackgroundTexture( void )
{
	return i_radar_texture;
}

/*
====================
GL_BindRadarTexture
====================
*/
void GL_BindRadarTexture( void )
{
	if( !i_radar_texture )
		return;

	pglEnable( GL_TEXTURE_2D );
	pglBindTexture( GL_TEXTURE_2D, i_radar_texture );
}

/*
====================
GL_FreeRadarCapture
====================
*/
void GL_FreeRadarCapture( void )
{
	if( p_radar_pixels )
	{
		Mem_Free( p_radar_pixels );
		p_radar_pixels = NULL;
	}

	if( i_radar_texture )
	{
		pglDeleteTextures( 1, &i_radar_texture );
		i_radar_texture = 0;
	}
}
