#pragma once

// Coordinates are HUD logical units. Only raw textured quads apply m_flScale.
namespace match_ui
{
struct Color
{
	int r, g, b, a;
	Color(int red = 255, int green = 255, int blue = 255, int alpha = 255)
		: r(red), g(green), b(blue), a(alpha) {}
};

void Init();
void VidInit();
void Shutdown();
bool Enabled();
// Responsive presentation adaptation; the original resource layout used scale 1.
float Scale();
int Texture(const char *path);
void Picture(int texture, float x, float y, float width, float height,
	Color color = Color(), bool crop = false, bool flipX = false);
// Circular UV crop, equivalent to the APK's stencil disc without a new renderer API.
void Circle(int texture, float x, float y, float diameter, Color color = Color());
void Sprite(int hudSpriteIndex, float x, float y, float width, float height,
	Color color = Color(), bool flipX = false);
float NumberWidth(int value, float height, int minDigits = 1, float advance = 0.0f);
void Number(int value, float x, float y, float height, Color color = Color(),
	bool rightAlign = false, int minDigits = 1, float advance = 0.0f);
float TextWidth(const char *text, float height);
// Truncation preserves whole UTF-8 characters. maxWidth <= 0 leaves it unclipped.
void Text(const char *text, float x, float y, float height, Color color = Color(),
	bool rightAlign = false, float maxWidth = 0.0f);
}
