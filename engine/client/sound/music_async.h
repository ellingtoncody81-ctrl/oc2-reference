#pragma once

#if XASH_SDL == 2
qboolean BGM_Start( const char *intro, const char *loop, int position );
void BGM_Stop( void );
qboolean BGM_Active( void );
qboolean BGM_Finished( void );
void BGM_Update( float volume, qboolean paused );
void BGM_Fade( float seconds );
void BGM_Activate( qboolean active );
void BGM_Current( char *name, size_t size, int *position );
void BGM_Dump( void );
void BGM_DebugStall( void );
#else
static qboolean BGM_Start( const char *a, const char *b, int p ) { return false; }
static void BGM_Stop( void ) {}
static qboolean BGM_Active( void ) { return false; }
static qboolean BGM_Finished( void ) { return false; }
static void BGM_Update( float v, qboolean p ) {}
static void BGM_Fade( float s ) {}
static void BGM_Activate( qboolean a ) {}
static void BGM_Current( char *n, size_t s, int *p ) {}
static void BGM_Dump( void ) {}
#endif
