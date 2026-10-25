/* The small part of the Win32 API this front end uses, declared by hand
 * (x64 layouts) so the program builds without a Windows SDK. */
#ifndef W_WIN32_H
#define W_WIN32_H
#include <stdint.h>
#include <stddef.h>

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned long DWORD;          /* 32-bit on Windows (LLP64) */
typedef unsigned int UINT;
typedef long LONG;
typedef short SHORT;
typedef uintptr_t WPARAM, UINT_PTR, DWORD_PTR, ULONG_PTR;
typedef intptr_t LPARAM, LRESULT, LONG_PTR, INT_PTR;
typedef void *HANDLE, *HWND, *HINSTANCE, *HMODULE, *HDC, *HICON, *HCURSOR, *HBRUSH, *HMENU, *HGDIOBJ, *HMONITOR, *HWAVEOUT, *HLOCAL;
typedef const wchar_t *LPCWSTR;
typedef wchar_t *LPWSTR;
typedef void *FARPROC;
typedef UINT MMRESULT;
#define WINAPI
#define CALLBACK

typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct { LONG x, y; } POINT;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; DWORD lPrivate; } MSG;
typedef struct {
    UINT cbSize, style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance;
    HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName; HICON hIconSm;
} WNDCLASSEXW;
typedef struct { HDC hdc; BOOL fErase; RECT rcPaint; BOOL fRestore, fIncUpdate; BYTE rgbReserved[32]; } PAINTSTRUCT;
typedef struct {
    DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount; DWORD biCompression, biSizeImage;
    LONG biXPelsPerMeter, biYPelsPerMeter; DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER;
typedef struct { BYTE b, g, r, a; } RGBQUAD;
typedef struct { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO;
typedef struct { DWORD cbSize; RECT rcMonitor, rcWork; DWORD dwFlags; } MONITORINFO;
typedef struct { WORD wFormatTag, nChannels; DWORD nSamplesPerSec, nAvgBytesPerSec; WORD nBlockAlign, wBitsPerSample, cbSize; } WAVEFORMATEX;
typedef struct wavehdr_tag {
    char *lpData; DWORD dwBufferLength, dwBytesRecorded; DWORD_PTR dwUser; DWORD dwFlags, dwLoops;
    struct wavehdr_tag *lpNext; DWORD_PTR reserved;
} WAVEHDR;
typedef struct {
    DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance; LPCWSTR lpstrFilter; LPWSTR lpstrCustomFilter;
    DWORD nMaxCustFilter, nFilterIndex; LPWSTR lpstrFile; DWORD nMaxFile; LPWSTR lpstrFileTitle; DWORD nMaxFileTitle;
    LPCWSTR lpstrInitialDir, lpstrTitle; DWORD Flags; WORD nFileOffset, nFileExtension; LPCWSTR lpstrDefExt;
    LPARAM lCustData; void *lpfnHook; LPCWSTR lpTemplateName; void *pvReserved; DWORD dwReserved, FlagsEx;
} OPENFILENAMEW;
typedef struct { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XINPUT_GAMEPAD;
typedef struct { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; } XINPUT_STATE;

typedef struct { DWORD dwSize, dwFlags, dwXpos, dwYpos, dwZpos, dwRpos, dwUpos, dwVpos, dwButtons, dwButtonNumber, dwPOV, dwReserved1, dwReserved2; } JOYINFOEX;
typedef struct { LONG cx, cy; } SIZE;
typedef DWORD (*LPTHREAD_START_ROUTINE)(void *);

typedef struct {
    WORD nSize, nVersion; DWORD dwFlags;
    BYTE iPixelType, cColorBits, cRedBits, cRedShift, cGreenBits, cGreenShift, cBlueBits, cBlueShift,
         cAlphaBits, cAlphaShift, cAccumBits, cAccumRedBits, cAccumGreenBits, cAccumBlueBits, cAccumAlphaBits,
         cDepthBits, cStencilBits, cAuxBuffers, iLayerType, bReserved;
    DWORD dwLayerMask, dwVisibleMask, dwDamageMask;
} PIXELFORMATDESCRIPTOR;
typedef void *HGLRC;

/* kernel32 */
HANDLE CreateThread(void *, size_t, LPTHREAD_START_ROUTINE, void *, DWORD, DWORD *);
HANDLE CreateEventW(void *, BOOL, BOOL, LPCWSTR);
BOOL SetEvent(HANDLE);
DWORD WaitForSingleObject(HANDLE, DWORD);
DWORD WaitForMultipleObjects(DWORD, const HANDLE *, BOOL, DWORD);
DWORD GetActiveProcessorCount(WORD);
int WideCharToMultiByte(UINT, DWORD, LPCWSTR, int, char *, int, const char *, BOOL *);
int MultiByteToWideChar(UINT, DWORD, const char *, int, LPWSTR, int);
BOOL MoveFileExW(LPCWSTR, LPCWSTR, DWORD);
HMODULE GetModuleHandleW(LPCWSTR);
DWORD GetModuleFileNameW(HMODULE, LPWSTR, DWORD);
LPWSTR GetCommandLineW(void);
void ExitProcess(UINT);
void Sleep(DWORD);
BOOL QueryPerformanceCounter(int64_t *);
BOOL QueryPerformanceFrequency(int64_t *);
HMODULE LoadLibraryW(LPCWSTR);
FARPROC GetProcAddress(HMODULE, const char *);
HLOCAL LocalFree(HLOCAL);
BOOL CreateDirectoryW(LPCWSTR, void *);
/* user32 */
UINT RegisterClassExW(const WNDCLASSEXW *);
HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void *);
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
BOOL ShowWindow(HWND, int);
BOOL UpdateWindow(HWND);
BOOL PeekMessageW(MSG *, HWND, UINT, UINT, UINT);
BOOL TranslateMessage(const MSG *);
LRESULT DispatchMessageW(const MSG *);
void PostQuitMessage(int);
BOOL GetClientRect(HWND, RECT *);
BOOL GetWindowRect(HWND, RECT *);
BOOL AdjustWindowRect(RECT *, DWORD, BOOL);
BOOL SetWindowTextW(HWND, LPCWSTR);
int MessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT);
HCURSOR LoadCursorW(HINSTANCE, LPCWSTR);
HICON LoadIconW(HINSTANCE, LPCWSTR);
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
HDC BeginPaint(HWND, PAINTSTRUCT *);
BOOL EndPaint(HWND, const PAINTSTRUCT *);
int FillRect(HDC, const RECT *, HBRUSH);
BOOL InvalidateRect(HWND, const RECT *, BOOL);
LONG_PTR SetWindowLongPtrW(HWND, int, LONG_PTR);
LONG_PTR GetWindowLongPtrW(HWND, int);
BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT);
HMONITOR MonitorFromWindow(HWND, DWORD);
BOOL GetMonitorInfoW(HMONITOR, MONITORINFO *);
BOOL SetProcessDPIAware(void);
BOOL DestroyWindow(HWND);
int ShowCursor(BOOL);
short GetKeyState(int);
HCURSOR SetCursor(HCURSOR);
UINT MapVirtualKeyW(UINT, UINT);
int GetKeyNameTextW(LONG, LPWSTR, int);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
/* gdi32 */
HDC CreateCompatibleDC(HDC);
HGDIOBJ CreateDIBSection(HDC, const BITMAPINFO *, UINT, void **, HANDLE, DWORD);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL DeleteObject(HGDIOBJ);
BOOL DeleteDC(HDC);
HGDIOBJ CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCWSTR);
DWORD SetTextColor(HDC, DWORD);
int SetBkMode(HDC, int);
BOOL TextOutW(HDC, int, int, LPCWSTR, int);
BOOL GetTextExtentPoint32W(HDC, LPCWSTR, int, SIZE *);
HBRUSH CreateSolidBrush(DWORD);
int ChoosePixelFormat(HDC, const PIXELFORMATDESCRIPTOR *);
BOOL SetPixelFormat(HDC, int, const PIXELFORMATDESCRIPTOR *);
BOOL SwapBuffers(HDC);
BOOL GdiFlush(void);
/* opengl32 (OpenGL 1.1) */
HGLRC wglCreateContext(HDC);
BOOL wglMakeCurrent(HDC, HGLRC);
BOOL wglDeleteContext(HGLRC);
void *wglGetProcAddress(const char *);
typedef unsigned int GLenum, GLuint, GLbitfield; typedef int GLint, GLsizei; typedef float GLfloat, GLclampf; typedef double GLdouble;
void glViewport(GLint, GLint, GLsizei, GLsizei);
void glClearColor(GLclampf, GLclampf, GLclampf, GLclampf);
void glClear(GLbitfield);
void glGenTextures(GLsizei, GLuint *);
void glBindTexture(GLenum, GLuint);
void glTexParameteri(GLenum, GLenum, GLint);
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
void glEnable(GLenum);
void glDisable(GLenum);
void glBlendFunc(GLenum, GLenum);
void glBegin(GLenum);
void glEnd(void);
void glTexCoord2f(GLfloat, GLfloat);
void glVertex2f(GLfloat, GLfloat);
void glColor4f(GLfloat, GLfloat, GLfloat, GLfloat);
void glMatrixMode(GLenum);
void glLoadIdentity(void);
void glOrtho(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
void glPixelStorei(GLenum, GLint);
const unsigned char *glGetString(GLenum);
GLenum glGetError(void);
BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD);
int StretchDIBits(HDC, int, int, int, int, int, int, int, int, const void *, const BITMAPINFO *, UINT, DWORD);
int SetStretchBltMode(HDC, int);
BOOL SetBrushOrgEx(HDC, int, int, POINT *);
HGDIOBJ GetStockObject(int);
/* winmm */
MMRESULT waveOutOpen(HWAVEOUT *, UINT, const WAVEFORMATEX *, DWORD_PTR, DWORD_PTR, DWORD);
MMRESULT waveOutPrepareHeader(HWAVEOUT, WAVEHDR *, UINT);
MMRESULT waveOutUnprepareHeader(HWAVEOUT, WAVEHDR *, UINT);
MMRESULT waveOutWrite(HWAVEOUT, WAVEHDR *, UINT);
MMRESULT waveOutReset(HWAVEOUT);
MMRESULT waveOutClose(HWAVEOUT);
MMRESULT timeBeginPeriod(UINT);
MMRESULT timeEndPeriod(UINT);
UINT joyGetNumDevs(void);
MMRESULT joyGetPosEx(UINT, JOYINFOEX *);
/* comdlg32 / shell32 */
BOOL GetOpenFileNameW(OPENFILENAMEW *);
LPWSTR *CommandLineToArgvW(LPCWSTR, int *);

/* constants */
#define CS_HREDRAW 2
#define CS_VREDRAW 1
#define CS_OWNDC 0x20
#define WS_OVERLAPPEDWINDOW 0x00CF0000L
#define WS_POPUP 0x80000000L
#define WS_VISIBLE 0x10000000L
#define CW_USEDEFAULT ((int)0x80000000)
#define SW_SHOW 5
#define PM_REMOVE 1
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_ERASEBKGND 0x0014
#define WM_ACTIVATE 0x0006
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_QUIT 0x0012
#define WM_DROPFILES 0x0233
#define GWL_STYLE (-16)
#define SWP_NOZORDER 0x0004
#define SWP_FRAMECHANGED 0x0020
#define SWP_NOOWNERZORDER 0x0200
#define HWND_TOP ((HWND)0)
#define MONITOR_DEFAULTTONEAREST 2
#define MB_OK 0
#define MB_ICONERROR 0x10
#define MB_ICONINFORMATION 0x40
#define IDC_ARROW ((LPCWSTR)(uintptr_t)32512)
#define IDI_APPLICATION ((LPCWSTR)(uintptr_t)32512)
#define BLACK_BRUSH 4
#define DIB_RGB_COLORS 0
#define SRCCOPY 0x00CC0020
#define BI_RGB 0
#define COLORONCOLOR 3
#define HALFTONE 4
#define WAVE_MAPPER ((UINT)-1)
#define WAVE_FORMAT_PCM 1
#define CALLBACK_NULL 0
#define WHDR_DONE 1
#define OFN_FILEMUSTEXIST 0x1000
#define OFN_PATHMUSTEXIST 0x0800
#define OFN_NOCHANGEDIR 0x0008
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_F1 0x70
#define XINPUT_GAMEPAD_DPAD_UP 0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN 0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT 0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT 0x0008
#define XINPUT_GAMEPAD_START 0x0010
#define XINPUT_GAMEPAD_BACK 0x0020
#define XINPUT_GAMEPAD_LEFT_SHOULDER 0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200
#define XINPUT_GAMEPAD_A 0x1000
#define XINPUT_GAMEPAD_B 0x2000
#define XINPUT_GAMEPAD_X 0x4000
#define XINPUT_GAMEPAD_Y 0x8000

#define PFD_DOUBLEBUFFER 1
#define PFD_DRAW_TO_WINDOW 4
#define PFD_SUPPORT_OPENGL 0x20
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP 0x2900
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_BGRA_EXT 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_QUADS 0x0007
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_VERSION 0x1F02
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_RBUTTONDOWN 0x0204
#define WM_MOUSEWHEEL 0x020A
#define WM_SETCURSOR 0x0020
#define TRANSPARENT 1
#define RGBC(r, g, b) ((DWORD)((r) | ((g) << 8) | ((b) << 16)))
#define INFINITE 0xFFFFFFFF
#define CP_UTF8 65001
#define JOY_RETURNALL 0xFF
#define MAPVK_VK_TO_VSC 0
#define MOVEFILE_REPLACE_EXISTING 1
#define ANTIALIASED_QUALITY 4
#define CLEARTYPE_QUALITY 5
#define FW_BOLD 700
#define FW_NORMAL 400
#define DEFAULT_CHARSET 1
#endif
