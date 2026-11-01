/*
FontManager.cpp - font manager
Copyright (C) 2017 a1batross

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/
#include <locale.h>
#include "FontManager.h"
#include "BaseMenu.h"
#include "Utils.h"

#include "BaseFontBackend.h"

#if defined(MAINUI_USE_FREETYPE)
#include "FreeTypeFont.h"
#elif defined(MAINUI_USE_STB)
#include "StbFont.h"
#elif defined(_WIN32)
#include "WinAPIFont.h"
#endif

#include "BitmapFont.h"
#include "utflib.h"

// OC2_WINDOWS_MENU_FONT_BEGIN
#if defined(_WIN32) && defined(MAINUI_USE_CUSTOM_FONT_RENDER) && !defined(MAINUI_USE_STB) && !defined(MAINUI_USE_FREETYPE)
#define OC2_WINDOWS_CJK_FONT
#define DEFAULT_MENUFONT "Microsoft YaHei UI"
#define DEFAULT_CONFONT  "Microsoft YaHei UI"
// Upload actual frontend labels, including punctuation, once for every
// existing font size. A whole CJK block would inflate every blur atlas.
static const uint32_t kPanoramaGlyphs[] =
{
    0x00B7, 0x00D7, 0x201C, 0x201D, 0x2026, 0x203A, 0x3001, 0x3002, 0x4E00, 0x4E09, 0x4E0A, 0x4E0B,
    0x4E0D, 0x4E0E, 0x4E13, 0x4E1A, 0x4E22, 0x4E2A, 0x4E2D, 0x4E3A, 0x4E3B, 0x4E49, 0x4E4B, 0x4E50,
    0x4E60, 0x4E61, 0x4E70, 0x4E86, 0x4E8C, 0x4E8E, 0x4EA1, 0x4EAE, 0x4EBA, 0x4EC5, 0x4EC7, 0x4ECD,
    0x4ED3, 0x4ED6, 0x4EE4, 0x4EE5, 0x4EEA, 0x4EF6, 0x4EFB, 0x4F0A, 0x4F0D, 0x4F17, 0x4F1A, 0x4F24,
    0x4F3D, 0x4F4D, 0x4F4F, 0x4F53, 0x4F59, 0x4F5C, 0x4F60, 0x4F7F, 0x4F8B, 0x4F9B, 0x4FA7, 0x4FC4,
    0x4FDD, 0x4FE1, 0x4FEE, 0x4FEF, 0x503C, 0x505C, 0x5141, 0x5143, 0x5148, 0x5149, 0x514B, 0x514D,
    0x5165, 0x5168, 0x516C, 0x516D, 0x5171, 0x5173, 0x5175, 0x5176, 0x5177, 0x5178, 0x5179, 0x5185,
    0x518D, 0x5199, 0x519B, 0x51B2, 0x51B5, 0x51C6, 0x51E4, 0x51F0, 0x51FA, 0x51FB, 0x5200, 0x5206,
    0x5207, 0x5217, 0x5219, 0x5220, 0x5229, 0x522B, 0x5230, 0x5236, 0x5237, 0x523A, 0x524D, 0x5251,
    0x5269, 0x526F, 0x529E, 0x529F, 0x52A0, 0x52A1, 0x52A8, 0x52B1, 0x52BF, 0x52D2, 0x52E4, 0x5315,
    0x5316, 0x5317, 0x5339, 0x533A, 0x5341, 0x534A, 0x5355, 0x5371, 0x5373, 0x5378, 0x5385, 0x539F,
    0x53A6, 0x53BB, 0x53C2, 0x53CA, 0x53CB, 0x53CC, 0x53CD, 0x53D1, 0x53D6, 0x53D8, 0x53E0, 0x53E3,
    0x53E4, 0x53EA, 0x53EF, 0x53F0, 0x53F3, 0x53F7, 0x5404, 0x5408, 0x540C, 0x540D, 0x540E, 0x5411,
    0x5426, 0x542C, 0x542F, 0x544A, 0x5458, 0x547D, 0x548C, 0x54C1, 0x54C8, 0x5668, 0x56DB, 0x56DE,
    0x56E0, 0x56E2, 0x56ED, 0x56F0, 0x56F4, 0x56FA, 0x56FD, 0x56FE, 0x5728, 0x5730, 0x573A, 0x5747,
    0x5782, 0x578B, 0x57CE, 0x57DF, 0x57FA, 0x5821, 0x5854, 0x586B, 0x5883, 0x5885, 0x58EB, 0x58F0,
    0x58F3, 0x5904, 0x5907, 0x590D, 0x5916, 0x591A, 0x591C, 0x5927, 0x5929, 0x5931, 0x5934, 0x5956,
    0x5957, 0x59CB, 0x5B50, 0x5B57, 0x5B58, 0x5B88, 0x5B89, 0x5B8C, 0x5B99, 0x5B9A, 0x5B9E, 0x5BA2,
    0x5BA4, 0x5BAA, 0x5BB3, 0x5BB6, 0x5BDF, 0x5BF8, 0x5BF9, 0x5BFC, 0x5C01, 0x5C04, 0x5C06, 0x5C0F,
    0x5C14, 0x5C1A, 0x5C31, 0x5C3A, 0x5C3C, 0x5C40, 0x5C4F, 0x5DE5, 0x5DE6, 0x5DF1, 0x5DF2, 0x5DF7,
    0x5E26, 0x5E27, 0x5E45, 0x5E73, 0x5E76, 0x5E78, 0x5E84, 0x5E8F, 0x5E93, 0x5E94, 0x5E95, 0x5EA6,
    0x5EF6, 0x5F00, 0x5F02, 0x5F03, 0x5F0F, 0x5F20, 0x5F2F, 0x5F39, 0x5F3A, 0x5F53, 0x5F55, 0x5F62,
    0x5F71, 0x5F80, 0x5F85, 0x5F92, 0x5F97, 0x5FAE, 0x5FB7, 0x5FC3, 0x5FC5, 0x5FEB, 0x6001, 0x6016,
    0x6027, 0x6050, 0x6062, 0x606F, 0x60C5, 0x610F, 0x6162, 0x620F, 0x6210, 0x6211, 0x6216, 0x6218,
    0x6240, 0x624B, 0x624D, 0x6253, 0x6258, 0x626C, 0x627E, 0x627F, 0x6295, 0x6297, 0x6298, 0x62A4,
    0x62A5, 0x62B5, 0x62C6, 0x62C9, 0x62D6, 0x62E5, 0x62E9, 0x62FE, 0x6301, 0x6302, 0x6307, 0x6309,
    0x635F, 0x6362, 0x63A5, 0x63A7, 0x63A9, 0x63B7, 0x63CF, 0x63D0, 0x63D2, 0x63F4, 0x641C, 0x642D,
    0x643A, 0x6446, 0x6469, 0x64A4, 0x64CD, 0x652F, 0x6536, 0x6539, 0x653B, 0x653E, 0x6548, 0x654C,
    0x654F, 0x6551, 0x6570, 0x6574, 0x6587, 0x65AD, 0x65AF, 0x65B0, 0x65B9, 0x65CB, 0x65E0, 0x65F6,
    0x660E, 0x661F, 0x662F, 0x663E, 0x666E, 0x666F, 0x6682, 0x6697, 0x66B4, 0x66F4, 0x66FF, 0x6700,
    0x6709, 0x670D, 0x672A, 0x672C, 0x673A, 0x6740, 0x6751, 0x675F, 0x6765, 0x677F, 0x6781, 0x679C,
    0x67AA, 0x67D3, 0x67E5, 0x6807, 0x680F, 0x6837, 0x6838, 0x683C, 0x6846, 0x6848, 0x6863, 0x68B0,
    0x68C0, 0x6905, 0x6984, 0x69FD, 0x6A21, 0x6A44, 0x6B21, 0x6B3E, 0x6B62, 0x6B63, 0x6B64, 0x6B65,
    0x6B66, 0x6B7B, 0x6B92, 0x6BB5, 0x6BC1, 0x6BCF, 0x6BD4, 0x6BD9, 0x6C11, 0x6C14, 0x6C34, 0x6C42,
    0x6C99, 0x6CA1, 0x6CD5, 0x6CE2, 0x6D3B, 0x6D41, 0x6D6A, 0x6D77, 0x6D82, 0x6D88, 0x6DEC, 0x6DF1,
    0x6DF7, 0x6DFB, 0x6E05, 0x6E10, 0x6E32, 0x6E38, 0x6E90, 0x6ED1, 0x6EDA, 0x6EE1, 0x6EE4, 0x6F14,
    0x6F20, 0x6FC0, 0x706B, 0x706D, 0x7075, 0x7099, 0x70B8, 0x70B9, 0x70BC, 0x70DF, 0x70E7, 0x70ED,
    0x7126, 0x7136, 0x718A, 0x71C3, 0x7206, 0x722A, 0x7247, 0x7248, 0x7259, 0x7269, 0x7279, 0x72B6,
    0x72D9, 0x72EC, 0x72F1, 0x730E, 0x7387, 0x739B, 0x73A9, 0x73AF, 0x73B0, 0x7406, 0x745E, 0x74E6,
    0x74F6, 0x751F, 0x7528, 0x7531, 0x7533, 0x7535, 0x753B, 0x7559, 0x767D, 0x7684, 0x76AE, 0x76D4,
    0x76D8, 0x76EE, 0x76F4, 0x770B, 0x7784, 0x77E5, 0x77ED, 0x7814, 0x7834, 0x7840, 0x786C, 0x786E,
    0x793A, 0x7968, 0x7981, 0x79BB, 0x79CD, 0x79D2, 0x79F0, 0x79FB, 0x7A3F, 0x7A76, 0x7A7A, 0x7A7F,
    0x7A81, 0x7A97, 0x7ACB, 0x7AD9, 0x7B2C, 0x7B49, 0x7B80, 0x7B97, 0x7C7B, 0x7C97, 0x7CBE, 0x7CFB,
    0x7D22, 0x7D2B, 0x7D2F, 0x7EA2, 0x7EB8, 0x7EB9, 0x7EBF, 0x7EC3, 0x7EC4, 0x7EC6, 0x7ECF, 0x7ED1,
    0x7ED3, 0x7EE7, 0x7EED, 0x7EF3, 0x7EF4, 0x7EFF, 0x7F16, 0x7F29, 0x7F51, 0x7F57, 0x7F6E, 0x7F8E,
    0x8005, 0x800C, 0x804A, 0x80A0, 0x80A4, 0x80DC, 0x80FD, 0x8131, 0x81EA, 0x81F3, 0x81F4, 0x8272,
    0x8282, 0x82F1, 0x8349, 0x836F, 0x83B7, 0x83DC, 0x8425, 0x84DD, 0x853D, 0x85CF, 0x864E, 0x8774,
    0x8776, 0x8840, 0x884C, 0x8861, 0x8863, 0x8868, 0x8896, 0x88AB, 0x88C5, 0x88F9, 0x897F, 0x8981,
    0x89C2, 0x89C4, 0x89C6, 0x89C8, 0x89D2, 0x89E3, 0x8B66, 0x8BA1, 0x8BA4, 0x8BA9, 0x8BB0, 0x8BB8,
    0x8BBE, 0x8BD5, 0x8BDD, 0x8BE5, 0x8BED, 0x8BF1, 0x8BF4, 0x8BF7, 0x8BFB, 0x8C03, 0x8C79, 0x8D1D,
    0x8D25, 0x8D28, 0x8D2D, 0x8D34, 0x8D44, 0x8D5B, 0x8D70, 0x8D77, 0x8DB3, 0x8DC3, 0x8DD1, 0x8DDD,
    0x8DDF, 0x8DF3, 0x8E2A, 0x8E72, 0x8F66, 0x8F6C, 0x8F6E, 0x8F7D, 0x8F91, 0x8F93, 0x8FA8, 0x8FB9,
    0x8FBE, 0x8FC7, 0x8FD0, 0x8FD1, 0x8FD4, 0x8FD8, 0x8FD9, 0x8FDB, 0x8FDE, 0x8FDF, 0x8FFD, 0x9000,
    0x9001, 0x9003, 0x9009, 0x900F, 0x901A, 0x901F, 0x906D, 0x907F, 0x90A3, 0x90E8, 0x914D, 0x91CD,
    0x91CE, 0x91CF, 0x91D1, 0x949F, 0x94A2, 0x94B1, 0x94B3, 0x9500, 0x9501, 0x950B, 0x952E, 0x952F,
    0x9547, 0x955C, 0x957F, 0x95EA, 0x95ED, 0x95F4, 0x961F, 0x9632, 0x9635, 0x963B, 0x963F, 0x9644,
    0x9645, 0x9650, 0x9664, 0x968F, 0x96BE, 0x96C6, 0x96F6, 0x96F7, 0x96FE, 0x9700, 0x9752, 0x9759,
    0x9762, 0x9769, 0x97F3, 0x9875, 0x9879, 0x987B, 0x9884, 0x9886, 0x9891, 0x989C, 0x989D, 0x98CE,
    0x9975, 0x9988, 0x9996, 0x9A6C, 0x9A76, 0x9A7E, 0x9AB7, 0x9AC5, 0x9AD8, 0x9C8D, 0x9E70, 0x9EA6,
    0x9EC4, 0x9ED1, 0x9ED8, 0x9F20, 0x9F7F, 0xFF01, 0xFF08, 0xFF09, 0xFF0C, 0xFF1A, 0xFF1B, 0xFF1F,
};
#else
#define DEFAULT_MENUFONT "Trebuchet MS"
#define DEFAULT_CONFONT  "Tahoma"
#endif
// OC2_WINDOWS_MENU_FONT_END
#define DEFAULT_WEIGHT   500

CFontManager *g_FontMgr;

CFontManager::CFontManager()
{
#ifdef MAINUI_USE_FREETYPE
	FT_Init_FreeType( &CFreeTypeFont::m_Library );
#endif
	m_Fonts.EnsureCapacity( 4 );
	m_FontFiles.EnsureCapacity( 2 );
}

CFontManager::~CFontManager()
{
	DeleteAllFonts();
#ifdef MAINUI_USE_FREETYPE
	FT_Done_FreeType( CFreeTypeFont::m_Library );
	CFreeTypeFont::m_Library = NULL;
#endif

	FOR_EACH_HASHMAP( m_FontFiles, i )
	{
		byte *p = m_FontFiles.Element( i ).data;
		EngFuncs::COM_FreeFile( p );
	}

	m_FontFiles.Purge();
}

void CFontManager::VidInit( void )
{
	static float prevScale = 0;

	float scale = uiStatic.scaleY;

	if( !prevScale
#ifndef SCALE_FONTS // complete disables font re-rendering
	|| fabs( scale - prevScale ) > 0.1f
#endif
	)
	{
		DeleteAllFonts();
		uiStatic.hDefaultFont = CFontBuilder( DEFAULT_MENUFONT, UI_MED_CHAR_HEIGHT * scale, DEFAULT_WEIGHT )
			.SetHandleNum( QM_DEFAULTFONT )
			.Create();
		uiStatic.hSmallFont   = CFontBuilder( DEFAULT_MENUFONT, UI_SMALL_CHAR_HEIGHT * scale, DEFAULT_WEIGHT )
			.SetHandleNum( QM_SMALLFONT )
			.Create();
		uiStatic.hSmallerFont = CFontBuilder( DEFAULT_MENUFONT, UI_SMALLER_CHAR_HEIGHT * scale, DEFAULT_WEIGHT )
			.SetHandleNum( QM_SMALLERFONT )
			.Create();
		uiStatic.hTinyFont    = CFontBuilder( DEFAULT_MENUFONT, UI_TINY_CHAR_HEIGHT * scale, DEFAULT_WEIGHT )
			.SetHandleNum( QM_TINYFONT )
			.Create();
		uiStatic.hBigFont     = CFontBuilder( DEFAULT_MENUFONT, UI_BIG_CHAR_HEIGHT * scale, DEFAULT_WEIGHT )
			.SetHandleNum( QM_BIGFONT )
			.Create();
		uiStatic.hBoldFont = CFontBuilder( DEFAULT_MENUFONT, UI_MED_CHAR_HEIGHT * scale, 1000 )
			.SetHandleNum( QM_BOLDFONT )
			.Create();

		if( !uiStatic.lowmemory )
		{
			uiStatic.hLightBlur = CFontBuilder( DEFAULT_MENUFONT, UI_MED_CHAR_HEIGHT * scale, DEFAULT_WEIGHT )
				.SetBlurParams( 2 * scale, 1.25f )
				.Create();

			uiStatic.hHeavyBlur = CFontBuilder( DEFAULT_MENUFONT, UI_MED_CHAR_HEIGHT * scale, DEFAULT_WEIGHT )
				.SetBlurParams( 8 * scale, 2.0f )
				.Create();
		}

		uiStatic.hConsoleFont = CFontBuilder( DEFAULT_CONFONT, UI_CONSOLE_CHAR_HEIGHT * scale, 500 )
			.SetOutlineSize()
			.Create();
		prevScale = scale;
	}
}

void CFontManager::DeleteAllFonts()
{
	for( int i = 0; i < m_Fonts.Count(); i++ )
	{
		delete m_Fonts[i];
	}
	m_Fonts.RemoveAll();
}

void CFontManager::DeleteFont(HFont hFont)
{
	CBaseFont *font = GetIFontFromHandle(hFont);
	if( font )
	{
		m_Fonts[hFont-1] = NULL;

		delete font;
	}
}

CBaseFont *CFontManager::GetIFontFromHandle(HFont font)
{
	if( m_Fonts.IsValidIndex( font - 1 ) )
		return m_Fonts[font-1];

	return NULL;
}

int CFontManager::GetEllipsisWide(HFont font)
{
	if( m_Fonts.IsValidIndex( font - 1 ) )
		return m_Fonts[font-1]->GetEllipsisWide();
	return 0;
}

void CFontManager::GetCharABCWide(HFont font, int ch, int &a, int &b, int &c)
{
	CBaseFont *pFont = GetIFontFromHandle( font );
	if( pFont )
		pFont->GetCharABCWidths( ch, a, b, c );
	else
		a = b = c = 0;
}

int CFontManager::GetCharacterWidth(HFont font, int ch)
{
	int a, b, c;
	GetCharABCWide( font, ch, a, b, c );
	return a + b + c;
}

int CFontManager::GetCharacterWidthScaled(HFont font, int ch, int height)
{
	return GetCharacterWidth( font, ch )
#ifdef SCALE_FONTS
		* ((float)height / (float)GetFontTall( font ))
#endif
	;
}

HFont CFontManager::GetFontByName(const char *name)
{
	for( int i = 0; i < m_Fonts.Count(); i++ )
	{
		if( !stricmp( name, m_Fonts[i]->GetName() ) )
			return i;
	}
	return -1;
}

int CFontManager::GetFontTall(HFont font)
{
	CBaseFont *pFont = GetIFontFromHandle( font );
	if( pFont )
		return pFont->GetTall();
	return 0;
}

int CFontManager::GetFontAscent(HFont font)
{
	CBaseFont *pFont = GetIFontFromHandle( font );
	if( pFont )
		return pFont->GetAscent();
	return 0;
}

bool CFontManager::GetFontUnderlined(HFont font)
{
	CBaseFont *pFont = GetIFontFromHandle( font );
	if( pFont )
		return pFont->GetFlags() & FONT_UNDERLINE;
	return false;
}

void CFontManager::GetTextSize(HFont fontHandle, const char *text, int *wide, int *tall, int size )
{
	CBaseFont *font = GetIFontFromHandle( fontHandle );

	if( !font || !text || !text[0] )
	{
		if( wide ) *wide = 0;
		if( tall ) *tall = 0;
		return;
	}

	int fontTall = font->GetHeight(), x = 0;
	int _wide = 0, _tall;
	const char *ch = text;
	_tall = fontTall;
	int i = 0;
	utfstate_t state;

	while( *ch && ( size < 0 || i < size ) )
	{
		// Skip colorcodes
		if( IsColorString( ch ) )
		{
			ch += 2;
			continue;
		}

		int uch = state.Decode((uint8_t)*ch );

		if( uch )
		{
			if( uch == '\n' && *( ch + 1 ) != '\0' )
			{
				_tall += fontTall;
				x = 0;
			}
			else
			{
				int a, b, c;
				font->GetCharABCWidths( uch, a, b, c );
				x += a + b + c;
				if( x > _wide )
					_wide = x;
			}
		}
		i++;
		ch++;
	}

	if( tall ) *tall = _tall;
	if( wide ) *wide = _wide;
}

int CFontManager::CutText(HFont fontHandle, const char *text, int height, int visibleSize, bool reverse, bool stopAtWhitespace, int *wide, bool *remaining )
{
	CBaseFont *font = GetIFontFromHandle( fontHandle );

	if( remaining )
		*remaining = false;

	if( !font || !text || !text[0] || visibleSize <= 0 )
		return 0;

	int _wide = 0;
	const char *ch = text;
	utfstate_t state;

#ifdef SCALE_FONTS
	visibleSize  = (float)visibleSize / (float)height * (float)font->GetTall();
#endif

	int whiteSpacePos = 0;

	// calculate full text wide
	while( *ch )
	{
		// skip colorcodes
		if( IsColorString( ch ) )
		{
			ch += 2;
			continue;
		}

		int uch = state.Decode((uint8_t)*ch );
		int x = 0;
		if( uch )
		{
			if( uch == '\n' )
			{
				ch++;
				break; //
			}

			int a, b, c;
			font->GetCharABCWidths( uch, a, b, c );
			x = a + b + c;

			if( uch == ' ' )
			{
				whiteSpacePos = ch - text;
			}
		}

		if( !reverse && _wide + x >= visibleSize )
			break;

		ch++;
		_wide += x;
	}

	if( !reverse )
	{
		if( *ch && remaining ) *remaining = true;
		if( wide ) *wide = _wide;
		if( stopAtWhitespace && whiteSpacePos )
			return whiteSpacePos;
		return ch - text;
	}

	if( _wide < visibleSize )
	{
		if( remaining ) *remaining = false;
		if( wide ) *wide = _wide;
		return 0;
	}

	ch = text;

	whiteSpacePos = 0;

	// now remove character one by one to fit
	state.Reset();
	while( *ch && _wide > visibleSize )
	{
		// skip colorcodes
		if( IsColorString( ch ) )
		{
			ch += 2;
			continue;
		}

		int uch = state.Decode((uint8_t)*ch );
		if( uch )
		{
			// we don't need check for newlines here, it's only done for oneline Field widget
			int a, b, c;
			font->GetCharABCWidths( uch, a, b, c );
			_wide -= a + b + c;

			if( uch == ' ' )
			{
				whiteSpacePos = ch - text;
			}
		}
		ch++;
	}

	if( remaining ) *remaining = true;
	if( wide ) *wide = _wide;
	if( stopAtWhitespace && whiteSpacePos ) return whiteSpacePos;
	return ch - text;

}

int CFontManager::GetTextWide(HFont font, const char *text, int size)
{
	int wide;

	GetTextSize( font, text, &wide, NULL, size );

	return wide;
}

int CFontManager::GetTextHeight(HFont fontHandle, const char *text, int size )
{
	CBaseFont *font = GetIFontFromHandle( fontHandle );
	if( !font || !text || !text[0] )
	{
		return 0;
	}

	int height = font->GetHeight();

	// lightweight variant only for getting text height
	int i = 0;
	while( *text&&( size < 0 || i < size ) )
	{
		if( *text == '\n' )
			height += height;

		text++;
		i++;
	}
	return height;
}

int CFontManager::GetTextHeightExt( HFont fontHandle, const char *text, int height, int w, int size )
{
	CBaseFont *font = GetIFontFromHandle( fontHandle );
	if( !font || !text || !text[0] || !w )
	{
		return 0;
	}

	const char *text2 = text;
	int y = 0;

	while( *text2 && ( size < 0 || text2 - text < size ) )
	{
		int pos = CutText( fontHandle, text2, height, w, false, true );
		if( pos == 0 )
			break;

		y += height;
		text2 += pos;
	}

	return y;

}

int CFontManager::GetTextWideScaled(HFont font, const char *text, const int height, int size)
{
	CBaseFont *pFont = GetIFontFromHandle( font );
	if( pFont )
	{
		return GetTextWide( font, text, size )
#ifdef SCALE_FONTS
			* ((float)height / (float)pFont->GetTall())
#endif
		;
	}

	return 0;
}

void CFontManager::UploadTextureForFont(CBaseFont *font)
{
	// upload only latin needed for english and cyrillic needed for russian
	// maybe it would be extended someday...

	charRange_t range[] =
	{
	{ 0x0021, 0x007E, NULL, 0 }, // ascii printable range
	{ 0x00C0, 0x00FF, NULL, 0 }, // latin-1 supplement (letters only)
	{ 0x0100, 0x017F, NULL, 0 }, // latin extended a
	{ 0x0400, 0x045F, NULL, 0 }, // cyrillic range
	{ 0, 0, table_cp1251, V_ARRAYSIZE( table_cp1251 ) }, // cp1251
#ifdef OC2_WINDOWS_CJK_FONT
    { 0, 0, kPanoramaGlyphs, V_ARRAYSIZE( kPanoramaGlyphs ) },
#endif
	};

	font->UploadGlyphsForRanges( range, V_ARRAYSIZE( range ) );
}

int CFontManager::DrawCharacter(HFont fontHandle, int ch, Point pt, int charH, const unsigned int color, bool forceAdditive )
{
	CBaseFont *font = GetIFontFromHandle( fontHandle );

	if( !font )
		return 0;

	return font->DrawCharacter( ch, pt, charH, color, forceAdditive );
}

void CFontManager::DebugDraw(HFont fontHandle)
{
	CBaseFont *font = GetIFontFromHandle(fontHandle);

	font->DebugDraw();
}


HFont CFontBuilder::Create()
{
	CBaseFont *font;

	// check existing font at first
	if( !m_hForceHandle )
	{
		for( int i = 0; i < g_FontMgr->m_Fonts.Count(); i++ )
		{
			font = g_FontMgr->m_Fonts[i];

			if( font->IsEqualTo( m_szName, m_iTall, m_iWeight, m_iBlur, m_iFlags ) )
				return i + 1;
		}
	}

#if defined(MAINUI_USE_FREETYPE)
	font = new CFreeTypeFont();
#elif defined(MAINUI_USE_STB)
	font = new CStbFont();
#elif defined(_WIN32) && defined(MAINUI_USE_CUSTOM_FONT_RENDER)
	font = new CWinAPIFont();
#else
	font = new CBitmapFont();
#endif

	double starttime = EngFuncs::DoubleTime();

	if( !font->Create( m_szName, m_iTall, m_iWeight, m_iBlur, m_fBrighten, m_iOutlineSize, m_iScanlineOffset, m_fScanlineScale, m_iFlags ) )
	{
		delete font;

		// fallback to bitmap font
		font = new CBitmapFont();

		// should never fail
		if( !font->Create( "Bitmap Font", m_iTall, m_iWeight, m_iBlur, m_fBrighten, m_iOutlineSize, m_iScanlineOffset, m_fScanlineScale, m_iFlags ) )
		{
			delete font;
			return -1;
		}
	}

	g_FontMgr->UploadTextureForFont( font );

	double endtime = EngFuncs::DoubleTime();

	Con_DPrintf( "Rendering %s(%i, %i) took %f seconds\n", font->GetName(), m_iTall, m_iWeight, endtime - starttime );

	if( m_hForceHandle != -1 && g_FontMgr->m_Fonts.Count() != m_hForceHandle )
	{
		if( g_FontMgr->m_Fonts.IsValidIndex( m_hForceHandle ) )
		{
			g_FontMgr->m_Fonts.FastRemove( m_hForceHandle );
			return g_FontMgr->m_Fonts.InsertBefore( m_hForceHandle, font );
		}
	}

	return g_FontMgr->m_Fonts.AddToTail(font) + 1;
}

bool CFontManager::FindFontDataFile( const char *name, int tall, int weight, int flags, char *dataFile, size_t dataFileChars )
{
	if( !strcmp( name, "Trebuchet MS" ))
	{
		Q_strncpy( dataFile, "gfx/fonts/FiraSans-Regular.ttf", dataFileChars );
		return true;
	}
	else if( !strcmp( name, "Tahoma" ))
	{
		Q_strncpy( dataFile, "gfx/fonts/tahoma.ttf", dataFileChars );
		return true;
	}

	return false;
}

byte *CFontManager::LoadFontDataFile( const char *vfspath, int *plen )
{
	int i = m_FontFiles.Find( vfspath );
	if( i != m_FontFiles.InvalidIndex( ))
	{
		if( plen )
			*plen = m_FontFiles[i].length;

		return m_FontFiles[i].data;
	}
	int len = 0;
	byte *p = EngFuncs::COM_LoadFile( vfspath, &len );
	if( p != nullptr )
	{
		if( plen )
			*plen = len;

		font_file file = { len, p };
		m_FontFiles.Insert( vfspath, file );
	}

	return p;
}
