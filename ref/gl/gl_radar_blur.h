/* CPU-only RGBA box blur shared with the x86 production differential test. */
#ifndef GL_RADAR_BLUR_H
#define GL_RADAR_BLUR_H

#include <limits.h>
#include <stddef.h>

/* The caller validates dimensions/radius and supplies a separate full-size
 * scratch image. Finish the horizontal pass before writing dst, including
 * when src == dst. Each pass truncates to bytes before the next pass. */
static void GL_RadarBoxBlurRGBA( const unsigned char *src, unsigned char *dst,
	unsigned char *tmp, int width, int height, int radius )
{
	int x, y, i, end, n;
	int rsum, gsum, bsum, asum;
	const int last_safe_center = INT_MAX - radius;
	const size_t stride = (size_t)width * 4;
	const unsigned char *p;
	unsigned char *o;

	end = radius < width - 1 ? radius : width - 1;
	for( y = 0; y < height; y++ )
	{
		const unsigned char *row = src + (size_t)y * stride;
		rsum = gsum = bsum = asum = 0;
		n = end + 1;
		for( i = 0; i <= end; i++ )
		{
			p = row + (size_t)i * 4;
			rsum += p[0]; gsum += p[1]; bsum += p[2]; asum += p[3];
		}
		for( x = 0; x < width; x++ )
		{
			o = tmp + (size_t)y * stride + (size_t)x * 4;
			/* Preserve the old x86 result when x + radius wraps negative,
			 * without evaluating the overflowing signed addition. */
			if( x > last_safe_center )
				o[0] = o[1] = o[2] = o[3] = 0;
			else
			{
				o[0] = rsum / n; o[1] = gsum / n;
				o[2] = bsum / n; o[3] = asum / n;
			}
			if( x >= radius )
			{
				p = row + (size_t)(x - radius) * 4;
				rsum -= p[0]; gsum -= p[1]; bsum -= p[2]; asum -= p[3];
				n--;
			}
			if( radius < width - 1 - x )
			{
				p = row + (size_t)(x + radius + 1) * 4;
				rsum += p[0]; gsum += p[1]; bsum += p[2]; asum += p[3];
				n++;
			}
		}
	}

	end = radius < height - 1 ? radius : height - 1;
	for( x = 0; x < width; x++ )
	{
		rsum = gsum = bsum = asum = 0;
		n = end + 1;
		for( i = 0; i <= end; i++ )
		{
			p = tmp + (size_t)i * stride + (size_t)x * 4;
			rsum += p[0]; gsum += p[1]; bsum += p[2]; asum += p[3];
		}
		for( y = 0; y < height; y++ )
		{
			o = dst + (size_t)y * stride + (size_t)x * 4;
			if( y > last_safe_center )
				o[0] = o[1] = o[2] = o[3] = 0;
			else
			{
				o[0] = rsum / n; o[1] = gsum / n;
				o[2] = bsum / n; o[3] = asum / n;
			}
			if( y >= radius )
			{
				p = tmp + (size_t)(y - radius) * stride + (size_t)x * 4;
				rsum -= p[0]; gsum -= p[1]; bsum -= p[2]; asum -= p[3];
				n--;
			}
			if( radius < height - 1 - y )
			{
				p = tmp + (size_t)(y + radius + 1) * stride + (size_t)x * 4;
				rsum += p[0]; gsum += p[1]; bsum += p[2]; asum += p[3];
				n++;
			}
		}
	}
}

#endif
