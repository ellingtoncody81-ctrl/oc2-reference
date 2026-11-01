#include "hud.h"
#include "cl_util.h"
#include "draw_util.h"
#include "triangleapi.h"
#include "render_api.h"
#include "match_ui.h"
#include "oc2_cold_perf.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

namespace match_ui
{
namespace
{
cvar_t *s_enabled = nullptr;
std::map<std::string, int> s_textures;
std::map<std::string, hud_icon::Bounds> s_silhouettes;

int Byte(int value) { return value < 0 ? 0 : value > 255 ? 255 : value; }
float Positive(float value) { return std::isfinite(value) && value > 0 ? value : 0; }

void SetColor(Color color)
{
	// Xash's legacy Color4ub ignores vertex alpha. This existing API forwards
	// RGBA unchanged; the draw itself keeps standard texture-alpha blending.
	auto *tri = gEngfuncs.pTriAPI;
	if (tri->Color4fRendermode)
		tri->Color4fRendermode(Byte(color.r)/255.0f, Byte(color.g)/255.0f,
			Byte(color.b)/255.0f, Byte(color.a)/255.0f, kRenderTransAlpha);
	else tri->Color4ub(Byte(color.r), Byte(color.g), Byte(color.b), Byte(color.a));
}

void Quad(float x, float y, float width, float height, float u0, float v0,
	float u1, float v1, Color color)
{
	auto *tri = gEngfuncs.pTriAPI;
	if (!tri || !Positive(width) || !Positive(height) || color.a <= 0) return;
	const float scale = gHUD.m_flScale > 0 ? gHUD.m_flScale : 1.0f;
	tri->RenderMode(kRenderTransTexture);
	tri->CullFace(TRI_NONE);
	SetColor(color);
	tri->Begin(TRI_QUADS);
	tri->TexCoord2f(u0, v0); tri->Vertex3f(x*scale, y*scale, 0);
	tri->TexCoord2f(u0, v1); tri->Vertex3f(x*scale, (y+height)*scale, 0);
	tri->TexCoord2f(u1, v1); tri->Vertex3f((x+width)*scale, (y+height)*scale, 0);
	tri->TexCoord2f(u1, v0); tri->Vertex3f((x+width)*scale, y*scale, 0);
	tri->End();
	tri->Color4ub(255, 255, 255, 255);
	tri->RenderMode(kRenderNormal);
	tri->CullFace(TRI_FRONT);
}

bool ScalableFont()
{
	return g_iMobileAPIVersion && gMobileAPI.pfnDrawScaledCharacter;
}

float FontFactor(float height)
{
	if (!ScalableFont()) return 1.0f;
	const float base = max(1, gHUD.GetCharHeight());
	// The engine's scaled-font cache only refreshes for changes above 0.1.
	// Quarter steps make rendering and width measurements use the same cache.
	const float factor = Positive(height) ? height / base : 1.0f;
	return max(0.25f, min(4.0f, std::floor(factor*4.0f + 0.5f) * 0.25f));
}

int SequenceBytes(const unsigned char *p)
{
	if (*p < 0x80) return 1;
	const int bytes = *p >= 0xC2 && *p <= 0xDF ? 2
		: *p >= 0xE0 && *p <= 0xEF ? 3 : *p >= 0xF0 && *p <= 0xF4 ? 4 : 1;
	for (int i = 1; i < bytes; ++i)
		if (!p[i] || (p[i] & 0xC0) != 0x80) return 1;
	if ((*p == 0xE0 && p[1] < 0xA0) || (*p == 0xED && p[1] >= 0xA0)
		|| (*p == 0xF0 && p[1] < 0x90) || (*p == 0xF4 && p[1] > 0x8F)) return 1;
	return bytes;
}

std::string CleanText(const char *text)
{
	std::string clean;
	if (!text) return clean;
	for (const unsigned char *p = (const unsigned char *)text; *p && clean.size() < 512;)
	{
		if (*p == '\n' || *p == '\r') break;
		if (*p < 32) { ++p; continue; }
		if (*p == '^' && p[1] >= '0' && p[1] <= '9') { p += 2; continue; }
		if (*p == '\\' && p[1] && std::strchr("yrwd", p[1])) { p += 2; continue; }
		const int bytes = SequenceBytes(p);
		if (clean.size() + bytes > 512) break;
		if (bytes == 1 && *p >= 0x80) clean += '?';
		else clean.append((const char *)p, bytes);
		p += bytes;
	}
	return clean;
}

float Measure(const std::string &text, float factor)
{
	float width = 0;
	// Use the existing engine-compatible UTF-8 to codepage decoder. This does
	// not add a Unicode font or promise glyphs missing from the HUD font atlas.
	Con_UtfProcessChar(0);
	for (unsigned char byte : text)
	{
		const int ch = Con_UtfProcessChar(byte);
		if (ch) width += ScalableFont() ? int(gHUD.GetCharWidth((unsigned char)ch)*factor)
			: gHUD.GetCharWidth((unsigned char)ch);
	}
	Con_UtfProcessChar(0);
	return width;
}

int DigitTexture(char digit)
{
	if (digit < '0' || digit > '9') return 0;
	char path[48];
	std::snprintf(path, sizeof(path), "gfx/vgui/number/%c.tga", digit);
	return Texture(path);
}

float DigitWidth(char digit, float height)
{
	const int texture = DigitTexture(digit);
	if (texture && gRenderAPI.RenderGetParm)
	{
		const int w = gRenderAPI.RenderGetParm(PARM_TEX_SRC_WIDTH, texture);
		const int h = gRenderAPI.RenderGetParm(PARM_TEX_SRC_HEIGHT, texture);
		if (w > 0 && h > 0) return height * w / h;
	}
	char text[] = {digit, 0};
	return max(1.0f, TextWidth(text, height));
}

std::string Digits(int value, int minDigits)
{
	char text[32];
	const int digits = max(1, min(10, minDigits));
	std::snprintf(text, sizeof(text), "%0*d", digits + (value < 0 ? 1 : 0), value);
	return text;
}
}

void Init()
{
	if (!s_enabled) s_enabled = CVAR_CREATE("hud_csgo", "1", FCVAR_ARCHIVE);
}

void Shutdown()
{
	for (const auto &texture : s_textures)
		if (texture.second && gRenderAPI.GL_FreeTexture) gRenderAPI.GL_FreeTexture(texture.second);
	s_textures.clear();
	s_silhouettes.clear();
}

void VidInit() { Shutdown(); }
bool Enabled() { return s_enabled && s_enabled->value != 0; }

float Scale()
{
	return max(0.25f, min(2.0f, float(max(1, ScreenHeight)) / 1080.0f));
}

int Texture(const char *path)
{
	if (!path || !*path || std::strlen(path) >= 256 || std::strstr(path, "..")
		|| std::strchr(path, ':') || std::strchr(path, '\\') || !gRenderAPI.GL_LoadTexture)
		return 0;
	const auto cached = s_textures.find(path);
	if (cached != s_textures.end()) return cached->second;
	if (s_textures.size() >= 256) return 0;
	OC2ColdTimer cold("hud_texture",path);
	const int texture = gRenderAPI.GL_LoadTexture(path, nullptr, 0, TF_NOMIPMAP | TF_CLAMP);
	s_textures[path] = texture;
	return texture;
}

void Picture(int texture, float x, float y, float width, float height, Color color, bool crop, bool flipX)
{
	if (!texture || !gRenderAPI.GL_Bind || !gEngfuncs.pTriAPI) return;
	float u = 0, v = 0;
	if (crop && gRenderAPI.RenderGetParm && Positive(width) && Positive(height))
	{
		const int sourceWidth = gRenderAPI.RenderGetParm(PARM_TEX_SRC_WIDTH, texture);
		const int sourceHeight = gRenderAPI.RenderGetParm(PARM_TEX_SRC_HEIGHT, texture);
		if (sourceWidth > 0 && sourceHeight > 0)
		{
			const float sourceAspect = float(sourceWidth)/sourceHeight;
			const float targetAspect = width/height;
			if (sourceAspect > targetAspect) u = (1.0f - targetAspect/sourceAspect)*0.5f;
			else v = (1.0f - sourceAspect/targetAspect)*0.5f;
		}
	}
	if (gRenderAPI.GL_SelectTexture) gRenderAPI.GL_SelectTexture(0);
	gRenderAPI.GL_Bind(0, texture);
	Quad(x, y, width, height, flipX ? 1-u : u, v, flipX ? u : 1-u, 1-v, color);
}

hud_icon::Bounds SilhouetteBounds(const char *path)
{
	const int texture = Texture(path); // validates the resource path first
	if (!texture) return {};
	const auto found = s_silhouettes.find(path);
	if (found != s_silhouettes.end()) return found->second;
	OC2ColdTimer cold("hud_bounds",path);
	hud_icon::Bounds bounds;
	int bytes = 0;
	if (gEngfuncs.COM_LoadFile && gEngfuncs.COM_FreeFile)
	{
		auto *data = gEngfuncs.COM_LoadFile(const_cast<char *>(path), 5, &bytes);
		if (data)
		{
			if (bytes > 0 && bytes <= 16*1024*1024) bounds = hud_icon::ScanTga(data, bytes);
			gEngfuncs.COM_FreeFile(data);
		}
	}
	if (!bounds.valid && gRenderAPI.RenderGetParm)
	{
		bounds.width = bounds.right = gRenderAPI.RenderGetParm(PARM_TEX_SRC_WIDTH,texture);
		bounds.height = bounds.bottom = gRenderAPI.RenderGetParm(PARM_TEX_SRC_HEIGHT,texture);
		bounds.valid = bounds.width > 0 && bounds.height > 0;
	}
	s_silhouettes[path] = bounds;
	return bounds;
}

bool Silhouette(const char *path, float x, float y, float width, float height, Color color, bool flipX)
{
	if (!Positive(width) || !Positive(height) || !gRenderAPI.GL_Bind) return false;
	const int texture = Texture(path);
	const auto bounds = SilhouetteBounds(path);
	if (!texture || !bounds.valid) return false;
	const float visibleW = float(bounds.right-bounds.left), visibleH = float(bounds.bottom-bounds.top);
	const float fit = min(width/visibleW,height/visibleH);
	x += (width-visibleW*fit)*0.5f; y += (height-visibleH*fit)*0.5f;
	if (gRenderAPI.GL_SelectTexture) gRenderAPI.GL_SelectTexture(0);
	gRenderAPI.GL_Bind(0,texture);
	const float u0 = float(bounds.left)/bounds.width, u1 = float(bounds.right)/bounds.width;
	Quad(x,y,visibleW*fit,visibleH*fit,flipX ? u1 : u0,float(bounds.top)/bounds.height,
		flipX ? u0 : u1,float(bounds.bottom)/bounds.height,color);
	return true;
}

void Circle(int texture, float x, float y, float diameter, Color color)
{
	auto *tri = gEngfuncs.pTriAPI;
	if (!texture || !tri || !gRenderAPI.GL_Bind || !Positive(diameter) || color.a <= 0) return;
	if (gRenderAPI.GL_SelectTexture) gRenderAPI.GL_SelectTexture(0);
	gRenderAPI.GL_Bind(0, texture);
	tri->RenderMode(kRenderTransTexture);
	tri->CullFace(TRI_NONE);
	SetColor(color);
	const float scale = gHUD.m_flScale > 0 ? gHUD.m_flScale : 1.0f;
	const float radius = diameter*0.5f;
	const float cx = x+radius, cy = y+radius;
	tri->Begin(TRI_TRIANGLE_FAN);
	tri->TexCoord2f(0.5f, 0.5f);
	tri->Vertex3f(cx*scale, cy*scale, 0);
	// APK mask: radius 40, 62 segments at its 80px reference size.
	for (int i=0; i<=62; ++i)
	{
		const float angle = float(i)*(6.28318530718f/62.0f);
		const float u = std::cos(angle), v = std::sin(angle);
		tri->TexCoord2f(0.5f+0.5f*u, 0.5f+0.5f*v);
		tri->Vertex3f((cx+radius*u)*scale, (cy+radius*v)*scale, 0);
	}
	tri->End();
	tri->Color4ub(255,255,255,255);
	tri->RenderMode(kRenderNormal);
	tri->CullFace(TRI_FRONT);
}

void Sprite(int hudSpriteIndex, float x, float y, float width, float height, Color color, bool flipX)
{
	if (hudSpriteIndex < 0 || hudSpriteIndex >= gHUD.SpriteCount() || !gEngfuncs.pTriAPI) return;
	const HSPRITE sprite = gHUD.GetSprite(hudSpriteIndex);
	const auto *model = gEngfuncs.GetSpritePointer(sprite);
	const int atlasWidth = SPR_Width(sprite, 0), atlasHeight = SPR_Height(sprite, 0);
	if (!model || atlasWidth <= 0 || atlasHeight <= 0) return;
	if (gRenderAPI.GL_SelectTexture) gRenderAPI.GL_SelectTexture(0);
	if (!gEngfuncs.pTriAPI->SpriteTexture(const_cast<model_s *>(model), 0)) return;
	const wrect_t &rect = gHUD.GetSpriteRect(hudSpriteIndex);
	const float u0 = float(flipX ? rect.right : rect.left)/atlasWidth;
	const float u1 = float(flipX ? rect.left : rect.right)/atlasWidth;
	Quad(x, y, width, height, u0, float(rect.top)/atlasHeight,
		u1, float(rect.bottom)/atlasHeight, color);
}

float NumberWidth(int value, float height, int minDigits, float advance)
{
	height = Positive(height);
	if (!height) return 0;
	const auto text = Digits(value, minDigits);
	advance = Positive(advance) ? advance : max(1.0f, height - 10.0f*Scale());
	return max(0.0f, float(text.size()-1))*advance + DigitWidth(text.back(), height);
}

void Number(int value, float x, float y, float height, Color color, bool rightAlign, int minDigits, float advance)
{
	height = Positive(height);
	if (!height || color.a <= 0) return;
	advance = Positive(advance) ? advance : max(1.0f, height - 10.0f*Scale());
	if (rightAlign) x -= NumberWidth(value, height, minDigits, advance);
	for (char digit : Digits(value, minDigits))
	{
		const float width = DigitWidth(digit, height);
		const int texture = DigitTexture(digit);
		if (texture) Picture(texture, x, y, width, height, color);
		else { char text[] = {digit, 0}; Text(text, x, y, height, color); }
		x += advance;
	}
}

float TextWidth(const char *text, float height)
{
	return Measure(CleanText(text), FontFactor(height));
}

void Text(const char *input, float x, float y, float height, Color color, bool rightAlign, float maxWidth)
{
	if (color.a <= 0) return;
	std::string text = CleanText(input);
	const float factor = FontFactor(height);
	if (maxWidth > 0 && Measure(text, factor) > maxWidth)
	{
		std::string clipped;
		const float suffix = Measure("...", factor);
		for (size_t i = 0; i < text.size();)
		{
			const int bytes = SequenceBytes((const unsigned char *)text.c_str()+i);
			const std::string candidate = clipped + text.substr(i, bytes);
			if (Measure(candidate, factor) + suffix > maxWidth) break;
			clipped = candidate;
			i += bytes;
		}
		text = suffix <= maxWidth ? clipped + "..." : std::string();
	}
	if (rightAlign) x -= Measure(text, factor);
	const int r = Byte(color.r)*Byte(color.a)/255;
	const int g = Byte(color.g)*Byte(color.a)/255;
	const int b = Byte(color.b)*Byte(color.a)/255;
	Con_UtfProcessChar(0);
	for (unsigned char byte : text)
	{
		// CL_DrawCharacter scales its quad via SPR_AdjustSize, but returns the
		// unscaled font advance. Keep x in HUD units; dividing it would overlap.
		if (ScalableFont()) x += gMobileAPI.pfnDrawScaledCharacter(int(x), int(y), byte, r, g, b, factor);
		else x += gEngfuncs.pfnDrawCharacter(int(x), int(y), byte, r, g, b);
	}
	Con_UtfProcessChar(0);
}
}
