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

/* without desktop OpenGL (Android): the front end's GL path is never taken
 * (wglCreateContext fails), its calls go to empty stand-ins */
#ifdef PS4_NO_GL
#define glViewport port_glViewport
#define glClearColor port_glClearColor
#define glClear port_glClear
#define glGenTextures port_glGenTextures
#define glBindTexture port_glBindTexture
#define glTexParameteri port_glTexParameteri
#define glTexImage2D port_glTexImage2D
#define glTexSubImage2D port_glTexSubImage2D
#define glEnable port_glEnable
#define glDisable port_glDisable
#define glBlendFunc port_glBlendFunc
#define glBegin port_glBegin
#define glEnd port_glEnd
#define glTexCoord2f port_glTexCoord2f
#define glVertex2f port_glVertex2f
#define glColor4f port_glColor4f
#define glMatrixMode port_glMatrixMode
#define glLoadIdentity port_glLoadIdentity
#define glOrtho port_glOrtho
#define glPixelStorei port_glPixelStorei
#define glGetString port_glGetString
#define glGetError port_glGetError
#endif
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
