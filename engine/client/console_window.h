/* Native console window. Included by console.c; no VGUI or client DLL dependency. */
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
#define CON_WINDOW_GLYPHS 2048
#define CON_WINDOW_CANDIDATES 128
#define CON_WINDOW_ROWS (CON_MAXLINES * 8)

typedef struct { int line, begin, end; unsigned int serial; } con_window_row_t;
typedef struct { unsigned int code; int width, texture; } con_window_glyph_t;
typedef struct { string command, display; } con_window_candidate_t;
typedef struct
{
	int x, y, w, h, fontHeight, rowHeight;
	int screenWidth, screenHeight;
	int titleH, logX, logY, logW, logH, inputY, entryH, fieldX, fieldW, submitX;
	int mouseX, mouseY, capture, startX, startY, oldX, oldY, oldW, oldH;
	int scrollGrab;
	int focus, returnDest, rows, first, shown, cacheWidth, candidateCount, candidate;
	int selectionAByte, selectionBByte;
	unsigned int generation, cachedGeneration, selectionA, selectionB;
	qboolean initialized, historyMenu, completionVisible;
	con_window_row_t row[CON_WINDOW_ROWS];
	con_window_candidate_t candidates[CON_WINDOW_CANDIDATES];
	string partial;
	con_window_glyph_t glyphs[CON_WINDOW_GLYPHS];
#if XASH_WIN32
	HDC dc;
	HFONT font, oldFont;
	HBITMAP bitmap, oldBitmap;
	byte *bitmapPixels;
#endif
} con_window_t;

static con_window_t con_window;

static void Con_WindowFreeFont( void )
{
	for( int i = 0; i < CON_WINDOW_GLYPHS; i++ )
	{
		if( con_window.glyphs[i].texture && ref.initialized && ref.dllFuncs.GL_FreeTexture )
			ref.dllFuncs.GL_FreeTexture( con_window.glyphs[i].texture );
	}
	memset( con_window.glyphs, 0, sizeof( con_window.glyphs ));
#if XASH_WIN32
	if( con_window.dc )
	{
		SelectObject( con_window.dc, con_window.oldFont );
		SelectObject( con_window.dc, con_window.oldBitmap );
		DeleteObject( con_window.font );
		DeleteObject( con_window.bitmap );
		DeleteDC( con_window.dc );
		con_window.dc = NULL;
		con_window.bitmapPixels = NULL;
		con_window.font = con_window.oldFont = NULL;
		con_window.bitmap = con_window.oldBitmap = NULL;
	}
#endif
	con_window.fontHeight = 0;
	con_window.cachedGeneration = ~con_window.generation;
}

static void Con_WindowFont( int height )
{
	if( con_window.fontHeight == height ) return;
	Con_WindowFreeFont();
	con_window.fontHeight = height;
#if XASH_WIN32
	BITMAPINFO bi;
	memset( &bi, 0, sizeof( bi ));
	bi.bmiHeader.biSize = sizeof( BITMAPINFOHEADER );
	bi.bmiHeader.biWidth = 64;
	bi.bmiHeader.biHeight = -64;
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = 32;
	bi.bmiHeader.biCompression = BI_RGB;
	con_window.dc = CreateCompatibleDC( NULL );
	con_window.bitmap = CreateDIBSection( con_window.dc, &bi, DIB_RGB_COLORS, (void **)&con_window.bitmapPixels, NULL, 0 );
	con_window.font = CreateFontW( -height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		DEFAULT_PITCH, L"Microsoft YaHei UI" );
	if( !con_window.dc || !con_window.bitmap || !con_window.font ) return;
	con_window.oldBitmap = SelectObject( con_window.dc, con_window.bitmap );
	con_window.oldFont = SelectObject( con_window.dc, con_window.font );
	SetTextColor( con_window.dc, RGB( 255, 255, 255 ));
	SetBkColor( con_window.dc, RGB( 0, 0, 0 ));
	SetBkMode( con_window.dc, TRANSPARENT );
#endif
}

static con_window_glyph_t *Con_WindowGlyph( unsigned int code, qboolean draw )
{
	unsigned int slot = (code * 2654435761u) % CON_WINDOW_GLYPHS;
	for( int n = 0; n < CON_WINDOW_GLYPHS; n++, slot = (slot + 1) % CON_WINDOW_GLYPHS )
	{
		con_window_glyph_t *g = &con_window.glyphs[slot];
		if( g->code && g->code != code ) continue;
		if( !g->code )
		{
			g->code = code;
#if XASH_WIN32
			SIZE size;
			WCHAR wc = code <= 0xffff ? (WCHAR)code : L'?';
			if( con_window.dc && GetTextExtentPoint32W( con_window.dc, &wc, 1, &size ))
				g->width = bound( 1, size.cx, 60 );
			else
#endif
				g->width = code < 128 && con.curFont ? con.curFont->charWidths[code] : con_window.fontHeight;
		}
#if XASH_WIN32
		if( draw && !g->texture && code > 32 && con_window.dc && con_window.bitmapPixels )
		{
			byte pixels[64 * 64 * 4];
			WCHAR wc = code <= 0xffff ? (WCHAR)code : L'?';
			char name[64];
			memset( con_window.bitmapPixels, 0, sizeof( pixels ));
			TextOutW( con_window.dc, 1, 0, &wc, 1 );
			GdiFlush();
			for( int i = 0; i < 64 * 64; i++ )
			{
				pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
				pixels[i * 4 + 3] = con_window.bitmapPixels[i * 4];
			}
			Q_snprintf( name, sizeof( name ), "*console_%u_%d", code, con_window.fontHeight );
			g->texture = ref.dllFuncs.GL_CreateTexture( name, 64, 64, pixels, TF_NOMIPMAP|TF_CLAMP|TF_HAS_ALPHA );
		}
#endif
		return g;
	}
	return NULL;
}

static unsigned int Con_WindowDecode( const char *s, int *bytes )
{
	utfstate_t state = { 0 };
	unsigned int code = 0;
	*bytes = 0;
	do { code = Q_DecodeUTF8( &state, (byte)s[(*bytes)++] ); }
	while( !code && s[*bytes] && *bytes < 4 );
	return code ? code : '?';
}

/* Shared measurement/render path keeps wrapping, hit regions and the caret aligned. */
static int Con_WindowText( int x, int y, const char *s, int bytes, int maxWidth, const rgba_t tint, qboolean draw )
{
	int width = 0, offset = 0;
	rgba_t color;
	memcpy( color, tint, sizeof( color ));
	while( s[offset] && (bytes < 0 || offset < bytes) )
	{
		if( IsColorString( s + offset ) && (bytes < 0 || offset + 1 < bytes) )
		{
			memcpy( color, g_color_table[ColorIndex( s[offset + 1] )], 3 );
			offset += 2;
			continue;
		}
		int count;
		unsigned int code = Con_WindowDecode( s + offset, &count );
		if( bytes >= 0 && offset + count > bytes ) break;
		con_window_glyph_t *g = Con_WindowGlyph( code, draw );
		int advance = code == '\t' ? con_window.fontHeight * 2 : (g ? g->width : con_window.fontHeight);
		if( code < 32 && code != '\t' ) { offset += count; continue; }
		if( width + advance > maxWidth ) break;
		if( draw && g )
		{
			if( g->texture )
			{
				ref.dllFuncs.GL_SetRenderMode( kRenderTransTexture );
				ref.dllFuncs.Color4ub( color[0], color[1], color[2], color[3] );
				ref.dllFuncs.R_DrawStretchPic( x + width - 1, y, advance + 2,
					con_window.fontHeight + 4, 0, 0, (advance + 2) / 64.0f,
					(con_window.fontHeight + 4) / 64.0f, g->texture );
			}
			else if( con.curFont && code < 256 )
				CL_DrawCharacter( x + width, y, code, color, con.curFont, 0 );
		}
		width += advance;
		offset += count;
	}
	return width;
}

static const rgba_t con_window_white = { 216, 222, 211, 255 };

static qboolean Con_WindowInside( int x, int y, int w, int h )
{
	return con_window.mouseX >= x && con_window.mouseX < x + w &&
		con_window.mouseY >= y && con_window.mouseY < y + h;
}

static void Con_WindowFill( int x, int y, int w, int h, byte r, byte g, byte b, byte a )
{
	if( w > 0 && h > 0 ) ref.dllFuncs.FillRGBA( kRenderTransTexture, x, y, w, h, r, g, b, a );
}

static void Con_WindowLayout( void )
{
	con_window_t *v = &con_window;
	Con_WindowFont( bound( 14, Q_rint( refState.height / 60.0f ), 24 ));
	v->rowHeight = v->fontHeight + 4;
	if( !v->initialized )
	{
		// CSSO GameConsole::Initialize: most of the right half of the screen.
		int offsetX = Q_rint( refState.height * (16.0f / 480.0f) );
		int offsetY = Q_rint( refState.height * (64.0f / 480.0f) );
		v->w = refState.width / 2;
		v->h = refState.height - 2 * offsetY;
		v->x = refState.width / 2 - offsetX;
		v->y = offsetY;
		v->returnDest = key_menu;
		v->candidate = -1;
		v->initialized = true;
	}
	else if( v->screenWidth > 0 && v->screenHeight > 0 &&
		(v->screenWidth != refState.width || v->screenHeight != refState.height) )
	{
		v->x = v->x * refState.width / v->screenWidth;
		v->y = v->y * refState.height / v->screenHeight;
		v->w = v->w * refState.width / v->screenWidth;
		v->h = v->h * refState.height / v->screenHeight;
	}
	v->screenWidth = refState.width;
	v->screenHeight = refState.height;
	v->w = bound( Q_min( 320, refState.width ), v->w, refState.width );
	v->h = bound( Q_min( 180, refState.height ), v->h, refState.height );
	v->x = bound( 0, v->x, refState.width - v->w );
	v->y = bound( 0, v->y, refState.height - v->h );
	// ConsolePanel: inset history, then a single Entry/Submit row at the bottom.
	v->titleH = Q_max( 28, v->rowHeight + 10 );
	v->entryH = Q_max( 24, v->rowHeight + 4 );
	v->inputY = v->y + v->h - v->entryH - 8;
	v->logX = v->x + 12;
	v->logW = v->w - 24;
	v->logY = v->y + v->titleH + 12;
	v->logH = Q_max( v->rowHeight, v->inputY - v->logY - 8 );
	v->shown = Q_max( 1, v->logH / v->rowHeight );
	v->submitX = v->x + v->w - 83;
	v->fieldX = v->x + 16;
	v->fieldW = v->submitX - v->fieldX - 16;
	con.input.widthInChars = Q_max( 8, v->fieldW / Q_max( 1, v->fontHeight / 2 ));
}

static void Con_WindowRows( void )
{
	con_window_t *v = &con_window;
	int width = v->logW - 24;
	if( v->cachedGeneration == v->generation && v->cacheWidth == width )
	{
		con.backscroll = bound( 0, con.backscroll, Q_max( 0, v->rows - v->shown ));
		return;
	}
	unsigned int anchor = 0;
	int anchorBegin = 0;
	if( con.backscroll > 0 && v->rows && v->cacheWidth == width )
	{
		int first = bound( 0, v->rows - con.backscroll - v->shown, v->rows - 1 );
		anchor = v->row[first].serial;
		anchorBegin = v->row[first].begin;
	}
	v->rows = 0;
	for( int i = 0; i < CON_LINES_COUNT; i++ )
	{
		con_lineinfo_t *line = &CON_LINES( i );
		if( !line->start || *line->start == '\1' ) continue;
		int begin = 0, at = 0, pixels = 0;
		do
		{
			int bytes = 0, advance = 0;
			if( line->start[at] )
			{
				if( IsColorString( line->start + at )) bytes = 2;
				else
				{
					unsigned int code = Con_WindowDecode( line->start + at, &bytes );
					con_window_glyph_t *g = Con_WindowGlyph( code, false );
					advance = code == '\t' ? v->fontHeight * 2 : (g ? g->width : v->fontHeight);
				}
			}
			if( !bytes || (pixels + advance > width && at > begin) )
			{
				if( v->rows < CON_WINDOW_ROWS ) v->row[v->rows++] = (con_window_row_t){ i, begin, at, line->serial };
				begin = at;
				pixels = 0;
				if( !bytes ) break;
			}
			pixels += advance;
			at += bytes;
		} while( true );
	}
	// Keep the original visible line, including while variable-sized ring lines
	// are evicted. A positive row-count delta alone drifts under a busy log.
	if( anchor )
	{
		for( int i = 0; i < v->rows; i++ )
		{
			con_window_row_t *row = &v->row[i];
			if( row->serial > anchor || (row->serial == anchor &&
				(row->begin == anchorBegin || (row->begin < anchorBegin && row->end > anchorBegin))) )
			{
				con.backscroll = Q_max( 0, v->rows - v->shown - i );
				break;
			}
		}
	}
	con.backscroll = bound( 0, con.backscroll, Q_max( 0, v->rows - v->shown ));
	v->cachedGeneration = v->generation;
	v->cacheWidth = width;
}

static void Con_WindowSelection( void )
{
	con_window_t *v = &con_window;
	if( !v->selectionA || !v->selectionB ) return;
	char *text = Mem_Malloc( host.mempool, CON_TEXTSIZE + CON_MAXLINES * 2 + 1 );
	int bytes = 0, first = v->selectionAByte, last = v->selectionBByte;
	unsigned int low = v->selectionA, high = v->selectionB;
	if( low > high || (low == high && first > last) )
	{ unsigned int swap = low; low = high; high = swap; int at = first; first = last; last = at; }
	for( int i = 0; i < CON_LINES_COUNT; i++ )
	{
		con_lineinfo_t *line = &CON_LINES( i );
		if( !line->start || *line->start == '\1' || line->serial < low || line->serial > high ) continue;
		int end = line->serial == high ? Q_min( last, Q_strlen( line->start )) : Q_strlen( line->start );
		for( int at = line->serial == low ? first : 0; at < end; at++ )
		{
			if( IsColorString( line->start + at )) { at++; continue; }
			if( (byte)line->start[at] >= 32 || line->start[at] == '\t' ) text[bytes++] = line->start[at];
		}
		if( line->serial < high ) { text[bytes++] = '\r'; text[bytes++] = '\n'; }
	}
	text[bytes] = 0;
	if( bytes ) Platform_SetClipboardText( text );
	Mem_Free( text );
}

static void Con_WindowClose( void )
{
	con_window.capture = 0;
	con_window.completionVisible = false;
	con.showlines = con.vislines = 0;
	if( con_window.returnDest == key_menu || cls.state != ca_active || Con_BackgroundMapActive() )
	{
		if( UI_IsVisible() ) Key_SetKeyDest( key_menu );
		else UI_SetActiveMenu( true );
	}
	else Key_SetKeyDest( key_game );
}

static int Con_WindowCandidateSort( const void *a, const void *b )
{
	return Q_stricmp( ((const con_window_candidate_t *)a)->command, ((const con_window_candidate_t *)b)->command );
}

static void Con_WindowAddCandidate( const char *key, const void *value, const void *buffer, void *opaque )
{
	con_window_t *v = &con_window;
	const char *prefix = v->partial;
	while( *prefix == '\\' || *prefix == '/' ) prefix++;
	if( key[0] == '@' || Q_strnicmp( key, prefix, Q_strlen( prefix ))) return;
	for( int i = 0; i < v->candidateCount; i++ ) if( !Q_stricmp( key, v->candidates[i].command )) return;
	if( v->candidateCount < CON_WINDOW_CANDIDATES )
	{
		con_window_candidate_t *item = &v->candidates[v->candidateCount++];
		Q_strncpy( item->command, key, sizeof( item->command ));
		if( opaque ) Q_snprintf( item->display, sizeof( item->display ), "%s %s", key, (const char *)value );
		else Q_strncpy( item->display, key, sizeof( item->display ));
	}
}

static void Con_WindowCompletion( void )
{
	con_window_t *v = &con_window;
	v->candidate = -1;
	v->candidateCount = 0;
	Q_strncpy( v->partial, con.input.buffer, sizeof( v->partial ));
	v->historyMenu = !v->partial[0];
	if( v->historyMenu )
	{
		for( int i = Q_max( 0, con.history.next - CON_HISTORY ); i < con.history.next && v->candidateCount < CON_WINDOW_CANDIDATES; i++ )
		{
			con_window_candidate_t *item = &v->candidates[v->candidateCount++];
			Q_strncpy( item->command, con.history.lines[i % CON_HISTORY].buffer, sizeof( item->command ));
			Q_strncpy( item->display, item->command, sizeof( item->display ));
		}
	}
	else if( v->partial[0] && !Q_strpbrk( v->partial, " \t;\n\r\"" ) )
	{
		Cmd_LookupCmds( NULL, NULL, Con_WindowAddCandidate );
		Cvar_LookupVars( 0, NULL, v, Con_WindowAddCandidate );
		qsort( v->candidates, v->candidateCount, sizeof( v->candidates[0] ), Con_WindowCandidateSort );
	}
	v->completionVisible = v->candidateCount > 0;
}

static void Con_WindowAcceptCandidate( int candidate )
{
	con_window_t *v = &con_window;
	if( candidate < 0 || candidate >= v->candidateCount ) return;
	Q_strncpy( con.input.buffer, v->candidates[candidate].command, sizeof( con.input.buffer ));
	if( !Q_strchr( con.input.buffer, ' ' )) Q_strncat( con.input.buffer, " ", sizeof( con.input.buffer ));
	con.input.cursor = Q_strlen( con.input.buffer );
	con.input.scroll = 0;
	v->candidate = candidate;
	v->focus = 0;
}

static void Con_WindowDrawField( field_t *field, int x, int y, int width, qboolean cursor )
{
	int start = bound( 0, field->scroll, field->cursor );
	while( field->buffer[start] && ((byte)field->buffer[start] & 0xc0) == 0x80 ) start++;
	while( start < field->cursor && Con_WindowText( 0, 0, field->buffer + start,
		field->cursor - start, 65536, con_window_white, false ) > width - 12 )
	{
		int bytes; Con_WindowDecode( field->buffer + start, &bytes ); start += bytes;
	}
	field->scroll = start;
	Con_WindowText( x, y, field->buffer + start, -1, width, con_window_white, true );
	if( cursor && !((int)(host.realtime * 2) & 1) )
	{
		int caret = Con_WindowText( 0, 0, field->buffer + start, field->cursor - start, width, con_window_white, false );
		Con_WindowFill( x + caret, y + 2, 1, con_window.fontHeight, 130, 218, 236, 255 );
	}
}

static void Con_WindowPlaceCaret( field_t *field, int fieldX )
{
	int at = field->scroll, last = at;
	int target = Q_max( 0, con_window.mouseX - fieldX );
	while( field->buffer[at] )
	{
		int bytes;
		Con_WindowDecode( field->buffer + at, &bytes );
		int width = Con_WindowText( 0, 0, field->buffer + field->scroll,
			at + bytes - field->scroll, 65536, con_window_white, false );
		if( width > target ) break;
		last = at += bytes;
	}
	field->cursor = last;
}

static void Con_WindowButton( int x, int y, int w, const char *label, qboolean active )
{
	qboolean hover = Con_WindowInside( x, y, w, con_window.entryH );
	Con_WindowFill( x, y, w, con_window.entryH, 79, 84, 86, 255 );
	Con_WindowFill( x + 1, y + 1, w - 2, con_window.entryH - 2,
		hover || active ? 64 : 44, hover || active ? 69 : 49, hover || active ? 71 : 51, 255 );
	int width = Con_WindowText( 0, 0, label, -1, w, con_window_white, false );
	Con_WindowText( x + (w - width) / 2, y + (con_window.entryH - con_window.fontHeight) / 2 - 1,
		label, -1, w, con_window_white, true );
}

static int Con_WindowCandidateCount( void )
{
	return Q_min( con_window.candidateCount, Q_min( 10, Q_max( 1,
		(refState.height - 8) / con_window.rowHeight )));
}

static int Con_WindowCandidateFirst( void )
{
	return Q_max( 0, con_window.candidate - Con_WindowCandidateCount() + 1 );
}

static void Con_WindowCandidateRect( int *x, int *y, int *w, int *h )
{
	con_window_t *v = &con_window;
	*x = v->fieldX - 4;
	*w = v->fieldW + 8;
	*h = Con_WindowCandidateCount() * v->rowHeight + 4;
	// Source's nonfocusable Menu opens below the entry and flips at the screen edge.
	*y = v->inputY + v->entryH;
	if( *y + *h > refState.height ) *y = v->inputY - *h;
	*y = Q_max( 0, *y );
}

static int Con_WindowRowByte( const con_window_row_t *row, int mouseX )
{
	const char *text = CON_LINES( row->line ).start;
	int at = row->begin, pixels = con_window.logX + 4;
	while( at < row->end )
	{
		if( IsColorString( text + at )) { at += 2; continue; }
		int bytes;
		unsigned int code = Con_WindowDecode( text + at, &bytes );
		con_window_glyph_t *g = Con_WindowGlyph( code, false );
		int width = code == '\t' ? con_window.fontHeight * 2 : (g ? g->width : con_window.fontHeight);
		if( mouseX < pixels + width / 2 ) break;
		pixels += width;
		at += bytes;
	}
	return at;
}

static int Con_WindowScrollThumb( void )
{
	con_window_t *v = &con_window;
	return Q_min( v->logH, Q_max( 24, v->logH * Q_min( v->shown, v->rows ) / Q_max( 1, v->rows )));
}

static void Con_WindowDraw( void )
{
	con_window_t *v = &con_window;
	Con_WindowLayout();
	Con_WindowRows();
	Con_WindowFill( v->x + 6, v->y + 8, v->w, v->h, 0, 0, 0, 90 );
	Con_WindowFill( v->x, v->y, v->w, v->h, 75, 80, 82, 255 );
	Con_WindowFill( v->x + 1, v->y + 1, v->w - 2, v->h - 2, 31, 36, 40, 241 );
	Con_WindowFill( v->x + 1, v->y + 1, v->w - 2, v->titleH, 42, 47, 51, 250 );
	Con_WindowText( v->x + 12, v->y + 6, "控制台", -1, v->w - 70, con_window_white, true );
	Con_WindowButton( v->x + v->w - 34, v->y + 4, 26, "×", false );
	Con_WindowFill( v->logX - 1, v->logY - 1, v->logW + 2, v->logH + 2, 10, 14, 17, 255 );
	Con_WindowFill( v->logX, v->logY, v->logW, v->logH, 16, 21, 25, 236 );
	v->first = Q_max( 0, v->rows - con.backscroll - v->shown );
	unsigned int low = v->selectionA, high = v->selectionB;
	int firstByte = v->selectionAByte, lastByte = v->selectionBByte;
	if( low > high || (low == high && firstByte > lastByte) )
	{ unsigned int swap = low; low = high; high = swap; int at = firstByte; firstByte = lastByte; lastByte = at; }
	for( int i = v->first; i < Q_min( v->rows, v->first + v->shown ); i++ )
	{
		con_window_row_t *row = &v->row[i];
		con_lineinfo_t *line = &CON_LINES( row->line );
		int y = v->logY + (i - v->first) * v->rowHeight;
		if( low && line->serial >= low && line->serial <= high )
		{
			int a = line->serial == low ? Q_max( row->begin, firstByte ) : row->begin;
			int b = line->serial == high ? Q_min( row->end, lastByte ) : row->end;
			if( b > a )
			{
				int x = Con_WindowText( 0, 0, line->start + row->begin, a - row->begin, 65536, con_window_white, false );
				int width = Con_WindowText( 0, 0, line->start + a, b - a, 65536, con_window_white, false );
				Con_WindowFill( v->logX + 4 + x, y, width, v->rowHeight, 72, 98, 117, 240 );
			}
		}
		Con_WindowText( v->logX + 4, y, line->start + row->begin, row->end - row->begin, v->logW - 24, con_window_white, true );
	}
	int trackX = v->logX + v->logW - 14;
	Con_WindowFill( trackX, v->logY, 14, v->logH, 33, 38, 42, 245 );
	int thumb = Con_WindowScrollThumb();
	int thumbY = v->logY + (v->logH - thumb) * v->first / Q_max( 1, v->rows - v->shown );
	Con_WindowFill( trackX + 2, thumbY, 10, thumb, 96, 105, 111, 255 );
	Con_WindowFill( v->fieldX - 5, v->inputY - 1, v->fieldW + 10, v->entryH + 2, 9, 13, 16, 255 );
	Con_WindowFill( v->fieldX - 4, v->inputY, v->fieldW + 8, v->entryH, 16, 21, 25, 255 );
	Con_WindowDrawField( &con.input, v->fieldX, v->inputY + 3, v->fieldW, v->focus == 0 );
	Con_WindowButton( v->submitX, v->inputY, 64, "提交", false );
	if( v->focus == 0 && v->completionVisible && v->candidateCount && con_showcompletion.value )
	{
		int first = Con_WindowCandidateFirst(), count = Con_WindowCandidateCount();
		int x, top, w, h; Con_WindowCandidateRect( &x, &top, &w, &h );
		Con_WindowFill( x, top, w, h, 78, 84, 89, 255 );
		Con_WindowFill( x + 1, top + 1, w - 2, h - 2, 30, 35, 39, 255 );
		for( int i = 0; i < count; i++ )
		{
			int y = top + 2 + i * v->rowHeight;
			if( first + i == v->candidate || Con_WindowInside( x, y, w, v->rowHeight ))
				Con_WindowFill( x + 1, y, w - 2, v->rowHeight, 72, 98, 117, 255 );
			Con_WindowText( v->fieldX, y, v->candidates[first + i].display, -1, v->fieldW, con_window_white, true );
		}
	}
	Con_WindowFill( v->x + v->w - 12, v->y + v->h - 4, 8, 1, 112, 150, 165, 255 );
	Con_WindowFill( v->x + v->w - 8, v->y + v->h - 8, 4, 1, 112, 150, 165, 255 );
	ref.dllFuncs.Color4ub( 255, 255, 255, 255 );
}

static int Con_WindowResizeEdges( void )
{
	con_window_t *v = &con_window;
	if( !Con_WindowInside( v->x, v->y, v->w, v->h )) return 0;
	return (v->mouseX < v->x + 7 ? 1 : 0) | (v->mouseX >= v->x + v->w - 7 ? 2 : 0) |
		(v->mouseY < v->y + 7 ? 4 : 0) | (v->mouseY >= v->y + v->h - 7 ? 8 : 0);
}

void Con_MouseMove( int x, int y )
{
	con_window_t *v = &con_window;
	v->mouseX = x; v->mouseY = y;
	if( cls.key_dest != key_console ) { v->capture = 0; return; }
	Con_WindowLayout();
	if( v->capture == 16 )
	{
		v->x = v->oldX + x - v->startX; v->y = v->oldY + y - v->startY;
		Con_WindowLayout();
	}
	else if( v->capture > 0 && v->capture < 16 )
	{
		int left = v->oldX, right = left + v->oldW, top = v->oldY, bottom = top + v->oldH;
		if( v->capture & 1 ) left = bound( 0, v->oldX + x - v->startX, right - Q_min( 320, refState.width ));
		if( v->capture & 2 ) right = bound( left + Q_min( 320, refState.width ), v->oldX + v->oldW + x - v->startX, refState.width );
		if( v->capture & 4 ) top = bound( 0, v->oldY + y - v->startY, bottom - Q_min( 180, refState.height ));
		if( v->capture & 8 ) bottom = bound( top + Q_min( 180, refState.height ), v->oldY + v->oldH + y - v->startY, refState.height );
		v->x = left; v->y = top; v->w = right - left; v->h = bottom - top;
		Con_WindowLayout();
	}
	else if( v->capture == 32 )
	{
		Con_WindowRows();
		if( y < v->logY ) con.backscroll = Q_min( Q_max( 0, v->rows - v->shown ), con.backscroll + 1 );
		if( y > v->logY + v->logH ) con.backscroll = Q_max( 0, con.backscroll - 1 );
		v->first = Q_max( 0, v->rows - con.backscroll - v->shown );
		int row = v->first + bound( 0, (y - v->logY) / v->rowHeight, v->shown - 1 );
		if( row < v->rows )
		{ v->selectionB = v->row[row].serial; v->selectionBByte = Con_WindowRowByte( &v->row[row], x ); }
	}
	else if( v->capture == 64 )
	{
		Con_WindowRows();
		int travel = v->logH - Con_WindowScrollThumb();
		int top = bound( 0, y - v->logY - v->scrollGrab, travel );
		con.backscroll = Q_max( 0, v->rows - v->shown ) * (travel - top) / Q_max( 1, travel );
	}
	int edge = Con_WindowResizeEdges();
	Platform_SetCursorType( edge & 3 ? (edge & 12 ? ((edge == 5 || edge == 10) ? dc_sizenwse : dc_sizenesw) : dc_sizewe) :
		edge & 12 ? dc_sizens : Con_WindowInside( v->x + 8, v->y + 6, v->w - 50, v->titleH - 6 ) ? dc_sizeall : dc_arrow );
}

void Con_MouseEvent( int key, qboolean down )
{
	con_window_t *v = &con_window;
	if( key != K_MOUSE1 ) return;
	if( !down ) { v->capture = 0; return; }
	if( cls.key_dest != key_console ) return;
	Con_WindowLayout(); Con_WindowRows();
	int x, y; Platform_GetMousePos( &x, &y ); Con_MouseMove( x, y );
	v->startX = x; v->startY = y; v->oldX = v->x; v->oldY = v->y; v->oldW = v->w; v->oldH = v->h;
	// The completion menu is a popup: hit-test it before the frame or history below it.
	if( v->focus == 0 && v->completionVisible && v->candidateCount && con_showcompletion.value )
	{
		int cx, cy, cw, ch; Con_WindowCandidateRect( &cx, &cy, &cw, &ch );
		if( Con_WindowInside( cx, cy, cw, ch ))
		{
			Con_WindowAcceptCandidate( Con_WindowCandidateFirst() + bound( 0,
				(y - cy - 2) / v->rowHeight, Con_WindowCandidateCount() - 1 ));
			v->completionVisible = false;
			return;
		}
	}
	v->capture = Con_WindowResizeEdges();
	if( v->capture ) return;
	if( Con_WindowInside( v->x + v->w - 34, v->y + 4, 26, v->entryH )) { Con_WindowClose(); return; }
	v->completionVisible = false;
	if( Con_WindowInside( v->x, v->y, v->w, v->titleH )) { v->capture = 16; return; }
	if( Con_WindowInside( v->submitX, v->inputY, 64, v->entryH ))
	{ v->focus = 0; Key_Console( K_ENTER ); return; }
	if( Con_WindowInside( v->fieldX - 4, v->inputY, v->fieldW + 8, v->entryH ))
	{ v->focus = 0; Con_WindowPlaceCaret( &con.input, v->fieldX ); return; }
	if( Con_WindowInside( v->logX + v->logW - 14, v->logY, 14, v->logH ))
	{
		int thumb = Con_WindowScrollThumb();
		int top = v->logY + (v->logH - thumb) * Q_max( 0, v->rows - con.backscroll - v->shown ) / Q_max( 1, v->rows - v->shown );
		if( y >= top && y < top + thumb ) { v->capture = 64; v->scrollGrab = y - top; }
		else con.backscroll = bound( 0, con.backscroll + (y < top ? v->shown : -v->shown), Q_max( 0, v->rows - v->shown ));
		return;
	}
	if( Con_WindowInside( v->logX, v->logY, v->logW - 14, v->logH ))
	{
		int row = v->first + (y - v->logY) / v->rowHeight;
		v->focus = 2;
		if( row < v->rows )
		{
			v->selectionA = v->selectionB = v->row[row].serial;
			v->selectionAByte = v->selectionBByte = Con_WindowRowByte( &v->row[row], x );
		}
		else v->selectionA = v->selectionB = 0;
		v->capture = 32;
	}
}

static qboolean Con_WindowKey( int key )
{
	con_window_t *v = &con_window;
	// Control-letter actions arrive through Con_CharEvent on SDL text input.
	// Consume a key event on backends that also send one, without executing twice.
	if( Key_IsDown( K_CTRL ) && v->focus == 2 && (key == 'c' || key == 'a') ) return true;
	if( v->focus == 2 && (key == K_ENTER || key == K_KP_ENTER || key == K_TAB) )
	{ v->focus = 0; return true; }
	if( v->focus == 2 && (key == K_UPARROW || key == K_DOWNARROW || key == K_BACKSPACE ||
		key == K_DEL || key == K_LEFTARROW || key == K_RIGHTARROW) ) v->focus = 0;
	if( v->focus == 0 && (key == K_TAB || key == K_UPARROW || key == K_DOWNARROW) )
	{
		if( !v->candidateCount ) Con_WindowCompletion();
		if( !v->candidateCount ) return key != K_TAB; // preserve engine parameter/file completion
		int step = key == K_UPARROW || (key == K_TAB && Key_IsDown( K_SHIFT )) ? -1 : 1;
		int candidate = v->candidate + step;
		if( candidate < 0 ) candidate = v->candidateCount - 1;
		if( candidate >= v->candidateCount ) candidate = 0;
		Con_WindowAcceptCandidate( candidate );
		v->completionVisible = true;
		return true;
	}
	return false;
}

static qboolean Con_WindowChar( int key )
{
	con_window_t *v = &con_window;
	if( key == 12 ) { Cbuf_AddText( "clear\n" ); return true; }
	if( key == 16 || key == 14 ) { Key_Console( key == 16 ? 'p' : 'n' ); return true; }
	if( key == 3 && v->focus == 2 ) { Con_WindowSelection(); return true; }
	if( key == 1 && v->focus == 2 )
	{
		Con_WindowRows();
		if( v->rows )
		{
			v->selectionA = CON_LINES( v->row[0].line ).serial;
			v->selectionB = CON_LINES( v->row[v->rows - 1].line ).serial;
			v->selectionAByte = 0;
			v->selectionBByte = Q_strlen( CON_LINES( v->row[v->rows - 1].line ).start );
		}
		return true;
	}
	if( key >= 32 ) v->focus = 0;
	return false;
}

static void Con_WindowHex( char *out, size_t size, const char *in )
{
	static const char digits[] = "0123456789abcdef";
	size_t n = 0;
	for( ; *in && n + 2 < size; in++ )
	{
		out[n++] = digits[((byte)*in) >> 4];
		out[n++] = digits[((byte)*in) & 15];
	}
	out[n] = 0;
}

/* Read-only local diagnostics. Text is encoded to keep one record per line. */
static void Con_WindowDump_f( void )
{
	con_window_t *v = &con_window;
	char text[3][MAX_STRING], input[MAX_STRING];
	unsigned int serial[3] = { 0 };
	int indexes[3] = { 0 }, begin[3] = { 0 }, end[3] = { 0 };
	char hex[MAX_STRING * 2 + 1];
	Con_WindowLayout(); Con_WindowRows();
	v->first = Q_max( 0, v->rows - con.backscroll - v->shown );
	int count = Q_min( 3, Q_min( v->shown, v->rows - v->first ));
	for( int i = 0; i < count; i++ )
	{
		con_window_row_t *row = &v->row[v->first + i];
		con_lineinfo_t *line = &CON_LINES( row->line );
		serial[i] = line->serial; indexes[i] = row->line; begin[i] = row->begin; end[i] = row->end;
		Q_strncpy( text[i], line->start, sizeof( text[i] ));
	}
	Q_strncpy( input, con.input.buffer, sizeof( input ));
	int cx, cy, cw, ch; Con_WindowCandidateRect( &cx, &cy, &cw, &ch );
	Con_Printf( "console_window: dest=%d focus=%d return=%d rect=%d,%d,%d,%d screen=%d,%d "
		"title_h=%d log=%d,%d,%d,%d input_rect=%d,%d,%d,%d submit=%d,%d,64,%d row=%d "
		"rows=%d first=%d shown=%d backscroll=%d candidates=%d candidate=%d popup=%d popup_rect=%d,%d,%d,%d "
		"selection=%u,%u selection_bytes=%d,%d capture=%d mouse=%d,%d history=%d,%d visible=%d\n",
		cls.key_dest, v->focus, v->returnDest, v->x, v->y, v->w, v->h, refState.width, refState.height,
		v->titleH, v->logX, v->logY, v->logW, v->logH, v->fieldX, v->inputY + 3, v->fieldW, v->rowHeight,
		v->submitX, v->inputY, v->entryH, v->rowHeight, v->rows, v->first, v->shown, con.backscroll,
		v->candidateCount, v->candidate, v->completionVisible && v->focus == 0 && con_showcompletion.value,
		cx, cy, cw, ch, v->selectionA, v->selectionB, v->selectionAByte, v->selectionBByte,
		v->capture, v->mouseX, v->mouseY,
		con.history.line, con.history.next, cls.key_dest == key_console );
	Con_WindowHex( hex, sizeof( hex ), input );
	Con_Printf( "console_window_input: cursor=%d scroll=%d valuehex=%s\n", con.input.cursor, con.input.scroll, hex );
	int firstCandidate = Con_WindowCandidateFirst();
	int candidates = Con_WindowCandidateCount();
	for( int i = 0; i < candidates; i++ )
	{
		Con_WindowHex( hex, sizeof( hex ), v->candidates[firstCandidate + i].display );
		Con_Printf( "console_window_candidate: index=%d valuehex=%s\n", firstCandidate + i, hex );
	}
	for( int i = 0; i < count; i++ )
	{
		Con_WindowHex( hex, sizeof( hex ), text[i] );
		Con_Printf( "console_window_row: screen_row=%d index=%d serial=%u begin=%d end=%d valuehex=%s\n",
			i, indexes[i], serial[i], begin[i], end[i], hex );
	}
}
