/* Native console window. Included by console.c; no VGUI or client DLL dependency. */
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif
#define CON_WINDOW_GLYPHS 2048
#define CON_WINDOW_CANDIDATES 128
#define CON_WINDOW_ROWS (CON_MAXLINES * 8)

typedef struct { int line, begin, end; unsigned int serial; } con_window_row_t;
typedef struct { unsigned int code; int width, texture; } con_window_glyph_t;
typedef struct
{
	int x, y, w, h, fontHeight, rowHeight;
	int screenWidth, screenHeight;
	int titleY, toolsY, logY, logH, inputY, statusY, fieldX, fieldW;
	int mouseX, mouseY, capture, startX, startY, oldX, oldY, oldW, oldH;
	int focus, returnDest, severity, rows, first, shown, cacheWidth, candidateCount, candidate;
	unsigned int generation, cachedGeneration, selectionA, selectionB;
	qboolean initialized, historyMenu;
	field_t filter;
	con_window_row_t row[CON_WINDOW_ROWS];
	string candidates[CON_WINDOW_CANDIDATES];
	string partial, status;
	char lastExport[MAX_QPATH];
	double statusUntil;
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

static const rgba_t con_window_white = { 220, 228, 232, 255 };
static const rgba_t con_window_muted = { 147, 162, 171, 255 };
static const rgba_t con_window_accent = { 116, 205, 226, 255 };

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
	v->rowHeight = v->fontHeight + 6;
	if( !v->initialized )
	{
		v->w = refState.width * 0.72f;
		v->h = refState.height * 0.72f;
		v->x = (refState.width - v->w) / 2;
		v->y = (refState.height - v->h) / 2;
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
	v->w = bound( Q_min( 560, refState.width ), v->w, refState.width );
	v->h = bound( Q_min( 320, refState.height ), v->h, refState.height );
	v->x = bound( 0, v->x, refState.width - v->w );
	v->y = bound( 0, v->y, refState.height - v->h );
	v->titleY = v->y;
	v->toolsY = v->y + v->rowHeight + 16;
	v->inputY = v->y + v->h - v->rowHeight * 2 - 22;
	v->statusY = v->y + v->h - v->rowHeight - 6;
	v->logY = v->toolsY + v->rowHeight + 15;
	v->logH = Q_max( v->rowHeight, v->inputY - v->logY - 10 );
	v->shown = Q_max( 1, v->logH / v->rowHeight );
	v->fieldX = v->x + 64;
	v->fieldW = v->w - 224;
	con.input.widthInChars = Q_max( 8, v->fieldW / Q_max( 1, v->fontHeight / 2 ));
	v->filter.widthInChars = Q_max( 8, (v->w - 326) / Q_max( 1, v->fontHeight / 2 ));
}

static void Con_WindowMessage( const char *message )
{
	Q_strncpy( con_window.status, message, sizeof( con_window.status ));
	con_window.statusUntil = host.realtime + 5.0;
}

static int Con_WindowPlain( char *out, int size, const char *in )
{
	int n = 0;
	for( int i = 0; in[i] && n < size - 1; i++ )
	{
		if( IsColorString( in + i )) { i++; continue; }
		if( (byte)in[i] >= 32 || in[i] == '\t' ) out[n++] = in[i];
	}
	out[n] = 0;
	return n;
}

static qboolean Con_WindowMatch( const con_lineinfo_t *line )
{
	char plain[MAX_PRINT_MSG];
	if( !line->start || *line->start == '\1' ) return false;
	Con_WindowPlain( plain, sizeof( plain ), line->start );
	if( con_window.severity == 1 && !Q_stristr( plain, "warn" ) && !Q_stristr( plain, "警告" )) return false;
	if( con_window.severity == 2 && !Q_stristr( plain, "error" ) && !Q_stristr( plain, "fail" ) &&
		!Q_stristr( plain, "fatal" ) && !Q_stristr( plain, "错误" )) return false;
	return !con_window.filter.buffer[0] || Q_stristr( plain, con_window.filter.buffer ) != NULL;
}

static void Con_WindowRows( void )
{
	con_window_t *v = &con_window;
	int width = v->w - 48;
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
		if( !Con_WindowMatch( line )) continue;
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

static void Con_WindowSelection( qboolean all )
{
	con_window_t *v = &con_window;
	if( !all && (!v->selectionA || !v->selectionB) )
	{
		Con_WindowMessage( "请先在日志中拖选要复制的行" );
		return;
	}
	char *text = Mem_Malloc( host.mempool, CON_TEXTSIZE + CON_MAXLINES * 2 + 1 );
	int bytes = 0, count = 0;
	unsigned int low = Q_min( v->selectionA, v->selectionB ), high = Q_max( v->selectionA, v->selectionB );
	for( int i = 0; i < CON_LINES_COUNT; i++ )
	{
		con_lineinfo_t *line = &CON_LINES( i );
		if( !Con_WindowMatch( line ) || (!all && (line->serial < low || line->serial > high)) ) continue;
		bytes += Con_WindowPlain( text + bytes, CON_TEXTSIZE + CON_MAXLINES * 2 + 1 - bytes, line->start );
		text[bytes++] = '\r'; text[bytes++] = '\n';
		count++;
	}
	text[bytes] = 0;
	if( count ) Platform_SetClipboardText( text );
	Mem_Free( text );
	Con_WindowMessage( count ? (all ? "已复制全部筛选结果" : "已复制选中的日志行") : "当前没有可复制的日志行" );
}

static void Con_WindowExport( void )
{
	char path[64], plain[MAX_PRINT_MSG];
	file_t *file = NULL;
	qboolean written = true;
	for( int i = 0; i < 10000; i++ )
	{
		Q_snprintf( path, sizeof( path ), "console/console_%04d.txt", i );
		if( !g_fsapi.FileExists( path, true )) { file = FS_Open( path, "wb", true ); break; }
	}
	if( !file ) { Con_WindowMessage( "日志导出失败：无法创建新文件" ); return; }
	for( int i = 0; i < CON_LINES_COUNT; i++ )
	{
		con_lineinfo_t *line = &CON_LINES( i );
		if( !Con_WindowMatch( line )) continue;
		int length = Con_WindowPlain( plain, sizeof( plain ), line->start );
		if( FS_Write( file, plain, length ) != length || FS_Write( file, "\r\n", 2 ) != 2 )
		{ written = false; break; }
	}
	FS_Close( file );
	if( !written ) { Con_WindowMessage( "日志写入失败，已保留未完成的文件" ); return; }
	Q_strncpy( con_window.lastExport, path, sizeof( con_window.lastExport ));
	Q_snprintf( con_window.status, sizeof( con_window.status ), "已导出筛选结果：%s", path );
	con_window.statusUntil = host.realtime + 8.0;
}

static void Con_WindowClose( void )
{
	con_window.capture = 0;
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
	return Q_stricmp( a, b );
}

static void Con_WindowAddCandidate( const char *key, const void *value, const void *buffer, void *opaque )
{
	con_window_t *v = &con_window;
	const char *prefix = v->partial;
	while( *prefix == '\\' || *prefix == '/' ) prefix++;
	if( key[0] == '@' || Q_strnicmp( key, prefix, Q_strlen( prefix ))) return;
	for( int i = 0; i < v->candidateCount; i++ ) if( !Q_stricmp( key, v->candidates[i] )) return;
	if( v->candidateCount < CON_WINDOW_CANDIDATES )
		Q_strncpy( v->candidates[v->candidateCount++], key, sizeof( string ));
}

static void Con_WindowCompletion( void )
{
	con_window_t *v = &con_window;
	v->candidate = -1;
	v->candidateCount = 0;
	Q_strncpy( v->partial, con.input.buffer, sizeof( v->partial ));
	if( v->historyMenu )
	{
		for( int i = con.history.next - 1; i >= Q_max( 0, con.history.next - CON_HISTORY ) && v->candidateCount < CON_WINDOW_CANDIDATES; i-- )
			Q_strncpy( v->candidates[v->candidateCount++], con.history.lines[i % CON_HISTORY].buffer, sizeof( string ));
	}
	else if( v->partial[0] && !Q_strpbrk( v->partial, " \t;\n\r\"" ) )
	{
		Cmd_LookupCmds( NULL, NULL, Con_WindowAddCandidate );
		Cvar_LookupVars( 0, NULL, NULL, Con_WindowAddCandidate );
		qsort( v->candidates, v->candidateCount, sizeof( string ), Con_WindowCandidateSort );
	}
}

static void Con_WindowAcceptCandidate( int candidate )
{
	con_window_t *v = &con_window;
	if( candidate < 0 || candidate >= v->candidateCount ) return;
	Q_strncpy( con.input.buffer, v->candidates[candidate], sizeof( con.input.buffer ));
	if( !v->historyMenu ) Q_strncat( con.input.buffer, " ", sizeof( con.input.buffer ));
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
	qboolean hover = Con_WindowInside( x, y, w, con_window.rowHeight + 4 );
	Con_WindowFill( x, y, w, con_window.rowHeight + 4, active ? 35 : 26,
		active || hover ? 72 : 36, active || hover ? 88 : 44, 242 );
	int width = Con_WindowText( 0, 0, label, -1, w, con_window_white, false );
	Con_WindowText( x + (w - width) / 2, y + 2, label, -1, w, con_window_white, true );
}

static int Con_WindowCandidateCount( void )
{
	return Q_min( con_window.candidateCount, Q_min( 6, Q_max( 1,
		(con_window.inputY - con_window.logY) / con_window.rowHeight - 1 )));
}

static int Con_WindowCandidateFirst( void )
{
	return Q_max( 0, con_window.candidate - Con_WindowCandidateCount() + 1 );
}

static void Con_WindowDraw( void )
{
	con_window_t *v = &con_window;
	Con_WindowLayout();
	Con_WindowRows();
	Con_WindowFill( v->x + 6, v->y + 8, v->w, v->h, 0, 0, 0, 90 );
	Con_WindowFill( v->x, v->y, v->w, v->h, 13, 19, 25, 238 );
	Con_WindowFill( v->x, v->y, v->w, v->rowHeight + 12, 27, 39, 48, 248 );
	Con_WindowFill( v->x, v->y, v->w, 2, 82, 159, 180, 255 );
	Con_WindowText( v->x + 14, v->y + 6, "开发者控制台", -1, v->w - 100, con_window_white, true );
	Con_WindowButton( v->x + v->w - 42, v->y + 5, 32, "×", false );
	int searchW = v->w - 318;
	Con_WindowFill( v->x + 12, v->toolsY, searchW, v->rowHeight + 4, 9, 14, 19, 245 );
	if( !v->filter.buffer[0] && v->focus != 1 )
		Con_WindowText( v->x + 20, v->toolsY + 2, "搜索日志  Ctrl+F", -1, searchW - 12, con_window_muted, true );
	else Con_WindowDrawField( &v->filter, v->x + 20, v->toolsY + 2, searchW - 16, v->focus == 1 );
	Con_WindowButton( v->x + v->w - 298, v->toolsY, 60, v->severity == 1 ? "警告" : v->severity == 2 ? "错误" : "全部", v->severity != 0 );
	Con_WindowButton( v->x + v->w - 230, v->toolsY, 94, "复制结果", false );
	Con_WindowButton( v->x + v->w - 128, v->toolsY, 54, "导出", false );
	Con_WindowButton( v->x + v->w - 66, v->toolsY, 54, "清空", false );
	Con_WindowFill( v->x + 12, v->logY - 3, v->w - 24, v->logH + 4, 7, 12, 17, 185 );
	v->first = Q_max( 0, v->rows - con.backscroll - v->shown );
	for( int i = v->first; i < Q_min( v->rows, v->first + v->shown ); i++ )
	{
		con_window_row_t *row = &v->row[i];
		con_lineinfo_t *line = &CON_LINES( row->line );
		int y = v->logY + (i - v->first) * v->rowHeight;
		if( v->selectionA && line->serial >= Q_min( v->selectionA, v->selectionB ) && line->serial <= Q_max( v->selectionA, v->selectionB ))
			Con_WindowFill( v->x + 14, y, v->w - 44, v->rowHeight, 44, 85, 107, 195 );
		Con_WindowText( v->x + 20, y, line->start + row->begin, row->end - row->begin, v->w - 48, con_window_white, true );
	}
	if( !v->rows ) Con_WindowText( v->x + 24, v->logY + 12, "没有匹配的日志", -1, v->w - 48, con_window_muted, true );
	int trackX = v->x + v->w - 25;
	Con_WindowFill( trackX, v->logY, 10, v->logH, 25, 34, 42, 200 );
	int thumb = Q_max( 24, v->logH * Q_min( v->shown, v->rows ) / Q_max( 1, v->rows ));
	int thumbY = v->logY + (v->logH - thumb) * v->first / Q_max( 1, v->rows - v->shown );
	Con_WindowFill( trackX + 2, thumbY, 6, thumb, 100, 137, 151, 230 );
	Con_WindowText( v->x + 16, v->inputY + 4, "命令", -1, 42, con_window_muted, true );
	Con_WindowFill( v->fieldX - 6, v->inputY, v->fieldW + 12, v->rowHeight + 6, 5, 11, 16, 250 );
	Con_WindowFill( v->fieldX - 6, v->inputY + v->rowHeight + 5, v->fieldW + 12, 1, 70, 137, 157, 255 );
	Con_WindowDrawField( &con.input, v->fieldX, v->inputY + 3, v->fieldW, v->focus == 0 );
	Con_WindowButton( v->x + v->w - 144, v->inputY, 60, "历史", v->historyMenu );
	Con_WindowButton( v->x + v->w - 76, v->inputY, 64, "执行", false );
	if( v->focus == 0 && v->candidateCount && con_showcompletion.value )
	{
		int first = Con_WindowCandidateFirst(), count = Con_WindowCandidateCount();
		int top = v->inputY - (count + 1) * v->rowHeight - 6;
		Con_WindowFill( v->fieldX - 6, top, v->fieldW + 12, (count + 1) * v->rowHeight, 19, 31, 40, 252 );
		Con_WindowText( v->fieldX, top + 2, v->historyMenu ? "历史记录 · 点击填入" : "命令补全 · Tab / Shift+Tab", -1, v->fieldW, con_window_accent, true );
		for( int i = 0; i < count; i++ )
		{
			int y = top + (i + 1) * v->rowHeight;
			if( first + i == v->candidate ) Con_WindowFill( v->fieldX - 4, y, v->fieldW + 8, v->rowHeight, 40, 79, 98, 255 );
			Con_WindowText( v->fieldX, y, v->candidates[first + i], -1, v->fieldW, con_window_white, true );
		}
	}
	const char *status = v->statusUntil > host.realtime ? v->status :
		(v->focus == 1 ? "筛选仅影响日志显示 · Enter 返回命令" : "拖选日志行 · Ctrl+C 复制选中行 · Ctrl+S 导出");
	Con_WindowText( v->x + 14, v->statusY, status, -1, v->w - 32, con_window_muted, true );
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
		if( v->capture & 1 ) left = bound( 0, v->oldX + x - v->startX, right - Q_min( 560, refState.width ));
		if( v->capture & 2 ) right = bound( left + Q_min( 560, refState.width ), v->oldX + v->oldW + x - v->startX, refState.width );
		if( v->capture & 4 ) top = bound( 0, v->oldY + y - v->startY, bottom - Q_min( 320, refState.height ));
		if( v->capture & 8 ) bottom = bound( top + Q_min( 320, refState.height ), v->oldY + v->oldH + y - v->startY, refState.height );
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
		if( row < v->rows ) v->selectionB = CON_LINES( v->row[row].line ).serial;
	}
	else if( v->capture == 64 )
	{
		Con_WindowRows();
		con.backscroll = Q_max( 0, v->rows - v->shown ) * (v->logY + v->logH - bound( v->logY, y, v->logY + v->logH )) / Q_max( 1, v->logH );
	}
	int edge = Con_WindowResizeEdges();
	Platform_SetCursorType( edge & 3 ? (edge & 12 ? ((edge == 5 || edge == 10) ? dc_sizenwse : dc_sizenesw) : dc_sizewe) :
		edge & 12 ? dc_sizens : Con_WindowInside( v->x + 8, v->y + 6, v->w - 60, v->rowHeight + 4 ) ? dc_sizeall : dc_arrow );
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
	v->capture = Con_WindowResizeEdges();
	if( v->capture ) return;
	if( Con_WindowInside( v->x + v->w - 42, v->y + 5, 32, v->rowHeight + 4 )) { Con_WindowClose(); return; }
	if( Con_WindowInside( v->x, v->y, v->w, v->rowHeight + 12 )) { v->capture = 16; return; }
	if( Con_WindowInside( v->x + 12, v->toolsY, v->w - 318, v->rowHeight + 4 ))
	{ v->focus = 1; Con_WindowPlaceCaret( &v->filter, v->x + 20 ); return; }
	if( Con_WindowInside( v->x + v->w - 298, v->toolsY, 60, v->rowHeight + 4 ))
	{ v->severity = (v->severity + 1) % 3; v->generation++; con.backscroll = 0; return; }
	if( Con_WindowInside( v->x + v->w - 230, v->toolsY, 94, v->rowHeight + 4 )) { Con_WindowSelection( true ); return; }
	if( Con_WindowInside( v->x + v->w - 128, v->toolsY, 54, v->rowHeight + 4 )) { Con_WindowExport(); return; }
	if( Con_WindowInside( v->x + v->w - 66, v->toolsY, 54, v->rowHeight + 4 )) { Con_Clear_f(); return; }
	if( v->focus == 0 && v->candidateCount && con_showcompletion.value )
	{
		int first = Con_WindowCandidateFirst(), count = Con_WindowCandidateCount();
		int top = v->inputY - (count + 1) * v->rowHeight - 6;
		if( Con_WindowInside( v->fieldX - 6, top + v->rowHeight, v->fieldW + 12, count * v->rowHeight ))
		{
			Con_WindowAcceptCandidate( first + (y - top) / v->rowHeight - 1 );
			v->candidateCount = 0; v->historyMenu = false; return;
		}
	}
	if( Con_WindowInside( v->x + v->w - 144, v->inputY, 60, v->rowHeight + 6 ))
	{ v->focus = 0; v->historyMenu = !v->historyMenu; Con_WindowCompletion(); return; }
	if( Con_WindowInside( v->x + v->w - 76, v->inputY, 64, v->rowHeight + 6 ))
	{ v->focus = 0; Key_Console( K_ENTER ); return; }
	if( Con_WindowInside( v->fieldX - 6, v->inputY, v->fieldW + 12, v->rowHeight + 6 ))
	{ v->focus = 0; Con_WindowPlaceCaret( &con.input, v->fieldX ); return; }
	if( Con_WindowInside( v->x + v->w - 27, v->logY, 14, v->logH ))
	{ v->capture = 64; Con_MouseMove( x, y ); return; }
	if( Con_WindowInside( v->x + 12, v->logY, v->w - 42, v->logH ))
	{
		int row = v->first + (y - v->logY) / v->rowHeight;
		v->focus = 2; v->candidateCount = 0; v->historyMenu = false;
		if( row < v->rows ) v->selectionA = v->selectionB = CON_LINES( v->row[row].line ).serial;
		else v->selectionA = v->selectionB = 0;
		v->capture = 32;
	}
}

static qboolean Con_WindowKey( int key )
{
	con_window_t *v = &con_window;
	// Control-letter actions arrive through Con_CharEvent on SDL text input.
	// Consume a key event on backends that also send one, without executing twice.
	if( Key_IsDown( K_CTRL ) && (key == 'f' || key == 's' ||
		(v->focus == 2 && (key == 'c' || key == 'a'))) ) return true;
	if( v->focus == 2 && (key == K_ENTER || key == K_KP_ENTER || key == K_TAB) )
	{ v->focus = 0; return true; }
	if( v->focus == 2 && (key == K_UPARROW || key == K_DOWNARROW || key == K_BACKSPACE ||
		key == K_DEL || key == K_LEFTARROW || key == K_RIGHTARROW) ) v->focus = 0;
	if( v->focus == 1 )
	{
		if( key == K_ESCAPE || key == K_ENTER || key == K_KP_ENTER || key == K_TAB ) { v->focus = 0; return true; }
		if( key != K_MWHEELUP && key != K_MWHEELDOWN && key != K_PGUP && key != K_PGDN )
		{ Field_KeyDownEvent( &v->filter, key ); v->generation++; con.backscroll = 0; return true; }
	}
	if( key == K_ESCAPE && (v->historyMenu || v->candidateCount) )
	{ v->historyMenu = false; v->candidateCount = 0; return true; }
	if( key == K_TAB && v->candidateCount && con_showcompletion.value )
	{
		int candidate = v->candidate + (Key_IsDown( K_SHIFT ) ? -1 : 1);
		if( candidate < 0 ) candidate = v->candidateCount - 1;
		if( candidate >= v->candidateCount ) candidate = 0;
		Con_WindowAcceptCandidate( candidate ); return true;
	}
	return false;
}

static qboolean Con_WindowChar( int key )
{
	con_window_t *v = &con_window;
	if( key == 6 ) { v->focus = 1; return true; } // Ctrl+F
	if( key == 19 ) { Con_WindowExport(); return true; } // Ctrl+S
	if( key == 3 && v->focus == 2 ) { Con_WindowSelection( false ); return true; }
	if( key == 1 && v->focus == 2 )
	{
		Con_WindowRows();
		if( v->rows )
		{
			v->selectionA = CON_LINES( v->row[0].line ).serial;
			v->selectionB = CON_LINES( v->row[v->rows - 1].line ).serial;
		}
		return true;
	}
	if( v->focus == 1 )
	{ Field_CharEvent( &v->filter, key ); v->generation++; con.backscroll = 0; return true; }
	if( key >= 32 ) { v->focus = 0; v->historyMenu = false; }
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

/* Explicit local diagnostics; strings are encoded so observing a filter cannot match itself. */
static void Con_WindowDump_f( void )
{
	con_window_t *v = &con_window;
	char text[3][MAX_STRING], input[MAX_STRING], filter[MAX_STRING], path[MAX_QPATH];
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
	Q_strncpy( filter, v->filter.buffer, sizeof( filter ));
	Q_strncpy( path, v->lastExport, sizeof( path ));
	Con_Printf( "console_window: dest=%d focus=%d return=%d rect=%d,%d,%d,%d screen=%d,%d "
		"tools_y=%d log=%d,%d,%d,%d input_rect=%d,%d,%d,%d status_y=%d row=%d "
		"rows=%d first=%d shown=%d backscroll=%d severity=%d candidates=%d candidate=%d "
		"selection=%u,%u capture=%d mouse=%d,%d history=%d,%d visible=%d\n",
		cls.key_dest, v->focus, v->returnDest, v->x, v->y, v->w, v->h, refState.width, refState.height,
		v->toolsY, v->x + 12, v->logY, v->w - 24, v->logH, v->fieldX, v->inputY + 3, v->fieldW, v->rowHeight,
		v->statusY, v->rowHeight, v->rows, v->first, v->shown, con.backscroll, v->severity,
		v->candidateCount, v->candidate, v->selectionA, v->selectionB, v->capture, v->mouseX, v->mouseY,
		con.history.line, con.history.next, cls.key_dest == key_console );
	Con_WindowHex( hex, sizeof( hex ), input );
	Con_Printf( "console_window_input: cursor=%d scroll=%d valuehex=%s\n", con.input.cursor, con.input.scroll, hex );
	Con_WindowHex( hex, sizeof( hex ), filter );
	Con_Printf( "console_window_filter: cursor=%d scroll=%d valuehex=%s\n", v->filter.cursor, v->filter.scroll, hex );
	Con_WindowHex( hex, sizeof( hex ), path );
	Con_Printf( "console_window_export: pathhex=%s\n", hex );
	int firstCandidate = Con_WindowCandidateFirst();
	int candidates = Con_WindowCandidateCount();
	for( int i = 0; i < candidates; i++ )
	{
		Con_WindowHex( hex, sizeof( hex ), v->candidates[firstCandidate + i] );
		Con_Printf( "console_window_candidate: index=%d valuehex=%s\n", firstCandidate + i, hex );
	}
	for( int i = 0; i < count; i++ )
	{
		Con_WindowHex( hex, sizeof( hex ), text[i] );
		Con_Printf( "console_window_row: screen_row=%d index=%d serial=%u begin=%d end=%d valuehex=%s\n",
			i, indexes[i], serial[i], begin[i], end[i], hex );
	}
}
