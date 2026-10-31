#pragma once

// The scoreboard occupies the centre of the screen. Keep the full overview
// in its left gutter and fit the entire overview image into a square.
namespace radar_fullmap_layout
{
struct Layout { int x, y, size; };
inline Layout Make(int width, int height)
{
    const int margin = width / 100 * 3 > 4 ? width / 100 * 3 : 4;
    int size = int(width * 0.17f);
    const int maxHeight = height - margin * 2;
    if (size > maxHeight) size = maxHeight;
    if (size < 2) size = 2;
    const int y = int(height * 0.075f);
    return {margin, y + size <= height - margin ? y : margin, size};
}
inline float TileScale(int size, int tilesX, int tilesY)
{
    const int longest = tilesX > tilesY ? tilesX : tilesY;
    return longest > 0 ? float(size) * 0.90f / float(longest) : 0.0f;
}
inline void Project(float tileX, float tileY, int tilesX, int tilesY,
                    int centerX, int centerY, float scale, float &x, float &y)
{
    x = centerX + (tileX - tilesX * 0.5f) * scale;
    y = centerY + (tileY - tilesY * 0.5f) * scale;
}
}
