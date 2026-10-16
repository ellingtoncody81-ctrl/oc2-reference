#pragma once

#include <cstddef>

// Small, bounded alpha scan for the existing APK TGA silhouettes. Coordinates
// are top-left image pixels; both TGA origin bits and RLE packets are respected.
// Invalid/unsupported files fall back to the renderer's full texture extent.
namespace hud_icon
{
struct Bounds
{
	int width = 0, height = 0, left = 0, top = 0, right = 0, bottom = 0;
	bool valid = false;
};

inline Bounds ScanTga(const unsigned char *data, std::size_t size)
{
	Bounds result;
	if (!data || size < 18 || data[1] != 0 || (data[2] != 2 && data[2] != 10)
		|| (data[16] != 24 && data[16] != 32)) return result;
	const int width = data[12] | data[13] << 8, height = data[14] | data[15] << 8;
	if (width < 1 || height < 1 || width > 2048 || height > 2048) return result;
	const unsigned int pixels = unsigned(width)*unsigned(height);
	const unsigned int bytes = data[16]/8;
	std::size_t cursor = 18u+data[0];
	if (cursor > size) return result;
	int left = width, top = height, right = 0, bottom = 0;
	unsigned int pixel = 0;
	while (pixel < pixels)
	{
		unsigned int run = 1;
		bool repeat = false;
		if (data[2] == 10)
		{
			if (cursor >= size) return result;
			const unsigned char packet = data[cursor++];
			run = (packet & 127)+1; repeat = (packet & 128) != 0;
		}
		if (run > pixels-pixel || (repeat ? bytes : run*bytes) > size-cursor) return result;
		for (unsigned int i = 0; i < run; ++i, ++pixel)
		{
			const auto *sample = data+cursor+(repeat ? 0 : i*bytes);
			if (bytes == 4 && sample[3] < 8) continue;
			int x = pixel%width, y = pixel/width;
			if (data[17] & 16) x = width-1-x;
			if (!(data[17] & 32)) y = height-1-y;
			if (x < left) left = x;
			if (y < top) top = y;
			if (x+1 > right) right = x+1;
			if (y+1 > bottom) bottom = y+1;
		}
		cursor += repeat ? bytes : run*bytes;
	}
	if (left >= right || top >= bottom) return result;
	// Keep one texel of transparent padding for bilinear edge filtering.
	result.width = width; result.height = height;
	result.left = left > 0 ? left-1 : 0; result.top = top > 0 ? top-1 : 0;
	result.right = right < width ? right+1 : width;
	result.bottom = bottom < height ? bottom+1 : height;
	result.valid = true;
	return result;
}
}
