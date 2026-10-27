/* Linux / Steam Deck / Android: the Win32 declarations of win/include/win32.h,
 * implemented over SDL2 by port/sdl_win32.c, plus the MS C library names the
 * front end uses. */
#ifndef PORT_WIN32_H
#define PORT_WIN32_H
#include <wchar.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "port_gl.h"
#include "../../win/include/win32.h"

#define _snwprintf swprintf
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _strdup strdup
#define _wcsicmp wcscasecmp
FILE *_wfopen(const wchar_t *path, const wchar_t *mode);

/* the entry point of the front end (win/main.c) */
void Start(void);

#endif
