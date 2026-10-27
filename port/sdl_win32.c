/*
 * The part of the Win32 API that the Windows front end (win/main.c) uses,
 * implemented over SDL2, so the same front end runs on Linux and the Steam
 * Deck (and, with OpenGL ES, later on Android):
 *   window and messages   -> SDL window and events (keys as Windows VK codes)
 *   GDI memory DCs        -> plain 32-bit pixel buffers; text by stb_truetype
 *                            with the DejaVu fonts built into the program
 *   WGL / OpenGL 1.1      -> an SDL OpenGL context and the system libGL
 *   waveOut               -> an SDL audio callback over the queued buffers
 *   XInput                -> SDL game controllers (Steam Deck controls too)
 *   threads and events    -> SDL threads, mutexes and conditions
 *   files                 -> POSIX, "\" in paths becomes "/"
 */
#define PORT_GL_IMPL
#include "win32.h"
#include <SDL.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <fnmatch.h>
#include <wctype.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

/* ---------------------------------------------------------------- strings */
static void w2u(const wchar_t *w, char *out, int n)          /* UTF-32 -> UTF-8 */
{
    int k = 0;
    for (; w && *w && k < n - 4; w++) {
        uint32_t c = (uint32_t)*w;
        if (c < 0x80) out[k++] = (char)c;
        else if (c < 0x800) { out[k++] = (char)(0xC0 | c >> 6); out[k++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { out[k++] = (char)(0xE0 | c >> 12); out[k++] = (char)(0x80 | ((c >> 6) & 63)); out[k++] = (char)(0x80 | (c & 63)); }
        else { out[k++] = (char)(0xF0 | c >> 18); out[k++] = (char)(0x80 | ((c >> 12) & 63)); out[k++] = (char)(0x80 | ((c >> 6) & 63)); out[k++] = (char)(0x80 | (c & 63)); }
    }
    out[k] = 0;
}
static int u2w(const char *s, int len, wchar_t *out, int n)   /* UTF-8 -> UTF-32, returns count incl. 0 if len < 0 */
{
    int k = 0;
    const unsigned char *p = (const unsigned char *)s, *e = len < 0 ? 0 : p + len;
    while ((e ? p < e : *p) && (!out || k < n - 1 || (!out))) {
        uint32_t c = *p++;
        if (c >= 0xF0) { c = (c & 7) << 18; c |= (uint32_t)(*p++ & 63) << 12; c |= (uint32_t)(*p++ & 63) << 6; c |= *p++ & 63; }
        else if (c >= 0xE0) { c = (c & 15) << 12; c |= (uint32_t)(*p++ & 63) << 6; c |= *p++ & 63; }
        else if (c >= 0xC0) { c = (c & 31) << 6; c |= *p++ & 63; }
        if (out) out[k] = (wchar_t)c;
        k++;
        if (out && k >= n - 1) break;
    }
    if (len < 0) { if (out) out[k] = 0; return k + 1; }
    return k;
}
int MultiByteToWideChar(UINT cp, DWORD fl, const char *s, int len, LPWSTR out, int n)
{
    (void)cp; (void)fl;
    if (!out || n == 0) return u2w(s, len, 0, 0x7FFFFFFF);
    return u2w(s, len, out, n);
}
int WideCharToMultiByte(UINT cp, DWORD fl, LPCWSTR w, int len, char *out, int n, const char *d, BOOL *u)
{
    (void)cp; (void)fl; (void)d; (void)u; (void)len;
    static char tmp[4096];
    w2u(w, out ? out : tmp, out ? n : (int)sizeof tmp);
    return (int)strlen(out ? out : tmp) + 1;
}
/* a Windows path (wide, with "\") as a POSIX one */
static void path_u(LPCWSTR w, char *out, int n)
{
    w2u(w, out, n);
    for (char *c = out; *c; c++) if (*c == '\\') *c = '/';
}
FILE *_wfopen(const wchar_t *path, const wchar_t *mode)
{
    char p[1024], m[16]; path_u(path, p, sizeof p); w2u(mode, m, sizeof m);
    return fopen(p, m);
}

/* ---------------------------------------------------------------- kernel32 */
static int g_argc; static char **g_argv;
static wchar_t g_cmdline[2048];
HMODULE GetModuleHandleW(LPCWSTR n) { (void)n; return (HMODULE)1; }
DWORD GetModuleFileNameW(HMODULE m, LPWSTR out, DWORD n)
{
    (void)m;
    char p[1024];
#ifdef __ANDROID__
    /* the app's own folder on the shared storage (Android/data/<app>/files):
     * saves, settings and screenshots can be copied from there over USB */
    const char *base = SDL_AndroidGetExternalStoragePath();
    if (!base) base = SDL_AndroidGetInternalStoragePath();
    snprintf(p, sizeof p, "%s/PS4", base ? base : ".");
    ssize_t k = (ssize_t)strlen(p);
#else
    ssize_t k = readlink("/proc/self/exe", p, sizeof p - 1);
    if (k <= 0) { char *b = SDL_GetBasePath(); snprintf(p, sizeof p, "%sPS4", b ? b : "./"); SDL_free(b); k = (ssize_t)strlen(p); }
    p[k] = 0;
#endif
    u2w(p, -1, out, (int)n);
    for (wchar_t *c = out; *c; c++) if (*c == L'/') *c = L'\\';   /* the front end looks for the last "\" */
    return (DWORD)wcslen(out);
}
LPWSTR GetCommandLineW(void) { return g_cmdline; }
LPWSTR *CommandLineToArgvW(LPCWSTR c, int *n)
{
    (void)c;
    LPWSTR *v = calloc((size_t)g_argc + 1, sizeof *v);
    for (int i = 0; i < g_argc; i++) { v[i] = calloc(1024, sizeof(wchar_t)); u2w(g_argv[i], -1, v[i], 1024); }
    *n = g_argc;
    return v;
}
HLOCAL LocalFree(HLOCAL h) { (void)h; return 0; }   /* a few hundred bytes, once */
void ExitProcess(UINT c) { SDL_Quit(); exit((int)c); }
void Sleep(DWORD ms) { SDL_Delay(ms); }
BOOL QueryPerformanceCounter(int64_t *v) { *v = (int64_t)SDL_GetPerformanceCounter(); return 1; }
BOOL QueryPerformanceFrequency(int64_t *v) { *v = (int64_t)SDL_GetPerformanceFrequency(); return 1; }
uint64_t GetTickCount64(void) { return SDL_GetTicks64(); }
DWORD GetLastError(void) { return 0; }
HANDLE CreateMutexW(void *a, BOOL o, LPCWSTR n) { (void)a; (void)o; (void)n; return (HANDLE)1; }
DWORD GetEnvironmentVariableW(LPCWSTR n, LPWSTR out, DWORD sz)
{
    char k[256]; w2u(n, k, sizeof k);
    const char *v = getenv(k);
    if (!v) return 0;
    u2w(v, -1, out, (int)sz);
    return (DWORD)wcslen(out);
}
BOOL CreateDirectoryW(LPCWSTR p, void *a) { (void)a; char u[1024]; path_u(p, u, sizeof u); return mkdir(u, 0755) == 0; }
BOOL DeleteFileW(LPCWSTR p) { char u[1024]; path_u(p, u, sizeof u); return unlink(u) == 0; }
BOOL MoveFileExW(LPCWSTR a, LPCWSTR b, DWORD f) { (void)f; char x[1024], y[1024]; path_u(a, x, sizeof x); path_u(b, y, sizeof y); return rename(x, y) == 0; }
DWORD GetFileAttributesW(LPCWSTR p)
{
    char u[1024]; path_u(p, u, sizeof u);
    struct stat st;
    if (stat(u, &st) != 0) return 0xFFFFFFFF;
    return S_ISDIR(st.st_mode) ? 0x10 : 0x80;
}
static void unix_to_ft(time_t t, FILETIME_ *ft)
{
    uint64_t v = ((uint64_t)t + 11644473600ull) * 10000000ull;
    ft->lo = (DWORD)(v & 0xFFFFFFFFu); ft->hi = (DWORD)(v >> 32);
}
BOOL GetFileAttributesExW(LPCWSTR p, int lvl, void *out)
{
    (void)lvl;
    char u[1024]; path_u(p, u, sizeof u);
    struct stat st;
    if (stat(u, &st) != 0) return 0;
    WIN32_FILE_ATTRIBUTE_DATA *d = out; memset(d, 0, sizeof *d);
    d->dwFileAttributes = S_ISDIR(st.st_mode) ? 0x10 : 0x80;
    unix_to_ft(st.st_mtime, &d->ftLastWriteTime);
    d->nFileSizeLow = (DWORD)st.st_size;
    return 1;
}
BOOL FileTimeToLocalFileTime(const FILETIME_ *a, FILETIME_ *b) { *b = *a; return 1; }   /* localtime() below does the zone */
BOOL FileTimeToSystemTime(const FILETIME_ *ft, SYSTEMTIME *st)
{
    uint64_t v = ((uint64_t)ft->hi << 32) | ft->lo;
    time_t t = (time_t)(v / 10000000ull - 11644473600ull);
    struct tm tm; localtime_r(&t, &tm);
    st->wYear = (WORD)(tm.tm_year + 1900); st->wMonth = (WORD)(tm.tm_mon + 1); st->wDay = (WORD)tm.tm_mday;
    st->wHour = (WORD)tm.tm_hour; st->wMinute = (WORD)tm.tm_min; st->wSecond = (WORD)tm.tm_sec;
    st->wDayOfWeek = (WORD)tm.tm_wday; st->wMilliseconds = 0;
    return 1;
}
void GetLocalTime(SYSTEMTIME *st)
{
    FILETIME_ ft; unix_to_ft(time(0), &ft); FileTimeToSystemTime(&ft, st);
}
/* folder listing: "dir\*" or "dir\*.ext" */
typedef struct { DIR *d; char pat[256]; char dir[1024]; } finder_t;
static int find_next(finder_t *f, WIN32_FIND_DATAW *fd)
{
    struct dirent *e;
    while ((e = readdir(f->d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (fnmatch(f->pat, e->d_name, 0)) continue;
        memset(fd, 0, sizeof *fd);
        char full[1400]; snprintf(full, sizeof full, "%s/%s", f->dir, e->d_name);
        struct stat st; if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) fd->dwFileAttributes = 0x10; else fd->dwFileAttributes = 0x80;
        u2w(e->d_name, -1, fd->cFileName, 260);
        return 1;
    }
    return 0;
}
HANDLE FindFirstFileW(LPCWSTR p, WIN32_FIND_DATAW *fd)
{
    char u[1024]; path_u(p, u, sizeof u);
    char *sl = strrchr(u, '/');
    finder_t *f = calloc(1, sizeof *f);
    if (sl) { *sl = 0; snprintf(f->dir, sizeof f->dir, "%s", u); snprintf(f->pat, sizeof f->pat, "%s", sl + 1); }
    else { strcpy(f->dir, "."); snprintf(f->pat, sizeof f->pat, "%s", u); }
    f->d = opendir(f->dir);
    if (!f->d || !find_next(f, fd)) { if (f->d) closedir(f->d); free(f); return INVALID_HANDLE_VALUE; }
    return f;
}
BOOL FindNextFileW(HANDLE h, WIN32_FIND_DATAW *fd) { return find_next(h, fd); }
BOOL FindClose(HANDLE h) { finder_t *f = h; if (f && f != INVALID_HANDLE_VALUE) { closedir(f->d); free(f); } return 1; }

/* threads and events (the HD pack workers) */
typedef struct { LPTHREAD_START_ROUTINE fn; void *arg; } thr_t;
static int thread_main(void *p) { thr_t t = *(thr_t *)p; free(p); return (int)t.fn(t.arg); }
HANDLE CreateThread(void *a, size_t s, LPTHREAD_START_ROUTINE fn, void *arg, DWORD fl, DWORD *id)
{
    (void)a; (void)s; (void)fl; if (id) *id = 0;
    thr_t *t = malloc(sizeof *t); t->fn = fn; t->arg = arg;
    return SDL_CreateThread(thread_main, "worker", t);
}
typedef struct { SDL_mutex *m; SDL_cond *c; int set, manual; } event_t;
HANDLE CreateEventW(void *a, BOOL manual, BOOL init, LPCWSTR n)
{
    (void)a; (void)n;
    event_t *e = calloc(1, sizeof *e); e->m = SDL_CreateMutex(); e->c = SDL_CreateCond(); e->set = init; e->manual = manual;
    return e;
}
BOOL SetEvent(HANDLE h) { event_t *e = h; SDL_LockMutex(e->m); e->set = 1; SDL_CondBroadcast(e->c); SDL_UnlockMutex(e->m); return 1; }
DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{
    event_t *e = h;
    SDL_LockMutex(e->m);
    while (!e->set) {
        if (ms == INFINITE) SDL_CondWait(e->c, e->m);
        else if (SDL_CondWaitTimeout(e->c, e->m, ms) == SDL_MUTEX_TIMEDOUT) { SDL_UnlockMutex(e->m); return 0x102; }
    }
    if (!e->manual) e->set = 0;
    SDL_UnlockMutex(e->m);
    return 0;
}
DWORD WaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD ms)
{
    (void)all;
    for (DWORD i = 0; i < n; i++) if (WaitForSingleObject(h[i], ms) != 0) return 0x102;
    return 0;
}
DWORD GetActiveProcessorCount(WORD g) { (void)g; return (DWORD)SDL_GetCPUCount(); }
BOOL CloseHandle(HANDLE h) { (void)h; return 1; }

/* XInput through LoadLibrary: SDL game controllers */
static SDL_GameController *pads[4];
static int touch_enabled(void); static WORD touch_buttons;
static DWORD xinput_get_state(DWORD user, XINPUT_STATE *st)
{
    if (user >= 4) return 1167;
    if (user == 0 && touch_enabled()) {
        memset(st, 0, sizeof *st);
        st->Gamepad.wButtons = touch_buttons;
        return 0;
    }
    /* the n-th connected controller */
    int k = 0; SDL_GameController *gc = 0;
    for (int i = 0; i < SDL_NumJoysticks() && !gc; i++) {
        if (!SDL_IsGameController(i)) continue;
        if (k++ != (int)user) continue;
        for (int j = 0; j < 4; j++) if (pads[j] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pads[j])) == SDL_JoystickGetDeviceInstanceID(i)) gc = pads[j];
        if (!gc) for (int j = 0; j < 4; j++) if (!pads[j]) { gc = pads[j] = SDL_GameControllerOpen(i); break; }
    }
    if (!gc || !SDL_GameControllerGetAttached(gc)) return 1167;   /* ERROR_DEVICE_NOT_CONNECTED */
    memset(st, 0, sizeof *st);
    static const struct { int b; WORD x; } m[] = {
        { SDL_CONTROLLER_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT },
        { SDL_CONTROLLER_BUTTON_START, XINPUT_GAMEPAD_START }, { SDL_CONTROLLER_BUTTON_BACK, XINPUT_GAMEPAD_BACK },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER },
        { SDL_CONTROLLER_BUTTON_A, XINPUT_GAMEPAD_A }, { SDL_CONTROLLER_BUTTON_B, XINPUT_GAMEPAD_B },
        { SDL_CONTROLLER_BUTTON_X, XINPUT_GAMEPAD_X }, { SDL_CONTROLLER_BUTTON_Y, XINPUT_GAMEPAD_Y },
        { SDL_CONTROLLER_BUTTON_LEFTSTICK, 0x0040 }, { SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0x0080 } };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++) if (SDL_GameControllerGetButton(gc, m[i].b)) st->Gamepad.wButtons |= m[i].x;
    st->Gamepad.bLeftTrigger = (BYTE)(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
    st->Gamepad.bRightTrigger = (BYTE)(SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
    st->Gamepad.sThumbLX = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    int ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
    st->Gamepad.sThumbLY = (SHORT)(ly == -32768 ? 32767 : -ly);   /* XInput: up is positive */
    return 0;
}
HMODULE LoadLibraryW(LPCWSTR n) { char u[256]; w2u(n, u, sizeof u); return strstr(u, "xinput") ? (HMODULE)2 : 0; }
FARPROC GetProcAddress(HMODULE m, const char *n) { return m == (HMODULE)2 && !strcmp(n, "XInputGetState") ? (FARPROC)xinput_get_state : 0; }
UINT joyGetNumDevs(void) { return 0; }
MMRESULT joyGetPosEx(UINT i, JOYINFOEX *j) { (void)i; (void)j; return 167; }
MMRESULT timeBeginPeriod(UINT p) { (void)p; return 0; }
MMRESULT timeEndPeriod(UINT p) { (void)p; return 0; }

/* diagnostics the front end calls outside diag.c: none needed */
TOP_LEVEL_EXCEPTION_FILTER SetUnhandledExceptionFilter(TOP_LEVEL_EXCEPTION_FILTER f) { (void)f; return 0; }

/* ---------------------------------------------------------------- audio */
typedef struct { UINT wType; DWORD sample; DWORD pad; } MMTIME_;
static SDL_AudioDeviceID adev;
static WAVEHDR *aq[64]; static int aq_n, aq_off;
static uint64_t a_played_total;
static void audio_cb(void *u, Uint8 *out, int len)
{
    (void)u;
    while (len > 0) {
        if (!aq_n) { memset(out, 0, (size_t)len); return; }
        WAVEHDR *h = aq[0];
        int left = (int)h->dwBufferLength - aq_off, take = left < len ? left : len;
        memcpy(out, h->lpData + aq_off, (size_t)take);
        out += take; len -= take; aq_off += take; a_played_total += (uint64_t)take / 4;
        if (aq_off >= (int)h->dwBufferLength) {
            h->dwFlags |= WHDR_DONE;
            memmove(aq, aq + 1, sizeof aq[0] * (size_t)--aq_n);
            aq_off = 0;
        }
    }
}
MMRESULT waveOutOpen(HWAVEOUT *h, UINT dev, const WAVEFORMATEX *f, DWORD_PTR cb, DWORD_PTR inst, DWORD fl)
{
    (void)dev; (void)cb; (void)inst; (void)fl;
    SDL_AudioSpec want, have; SDL_zero(want);
    want.freq = (int)f->nSamplesPerSec; want.format = AUDIO_S16SYS; want.channels = (Uint8)f->nChannels;
    want.samples = 512; want.callback = audio_cb;
    adev = SDL_OpenAudioDevice(0, 0, &want, &have, 0);
    if (!adev) return 1;
    SDL_PauseAudioDevice(adev, 0);
    *h = (HWAVEOUT)(uintptr_t)adev;
    return 0;
}
MMRESULT waveOutPrepareHeader(HWAVEOUT w, WAVEHDR *h, UINT s) { (void)w; (void)s; h->dwFlags |= 2; return 0; }
MMRESULT waveOutUnprepareHeader(HWAVEOUT w, WAVEHDR *h, UINT s) { (void)w; (void)s; h->dwFlags &= ~2u; return 0; }
MMRESULT waveOutWrite(HWAVEOUT w, WAVEHDR *h, UINT s)
{
    (void)w; (void)s;
    SDL_LockAudioDevice(adev);
    if (aq_n >= 64) { SDL_UnlockAudioDevice(adev); return 1; }
    h->dwFlags &= ~(DWORD)WHDR_DONE;
    aq[aq_n++] = h;
    SDL_UnlockAudioDevice(adev);
    return 0;
}
MMRESULT waveOutReset(HWAVEOUT w)
{
    (void)w;
    SDL_LockAudioDevice(adev);
    for (int i = 0; i < aq_n; i++) aq[i]->dwFlags |= WHDR_DONE;
    aq_n = 0; aq_off = 0; a_played_total = 0;
    SDL_UnlockAudioDevice(adev);
    return 0;
}
MMRESULT waveOutClose(HWAVEOUT w) { (void)w; SDL_CloseAudioDevice(adev); return 0; }
MMRESULT waveOutGetPosition(HWAVEOUT w, MMTIME_ *t, UINT s)
{
    (void)w; (void)s;
    SDL_LockAudioDevice(adev);
    t->wType = 2; t->sample = (DWORD)a_played_total;
    SDL_UnlockAudioDevice(adev);
    return 0;
}

/* ---------------------------------------------------------------- fonts */
extern const unsigned char font_regular[], font_bold[];
extern const unsigned int font_regular_len, font_bold_len;
typedef struct { int h, bold; stbtt_fontinfo *fi; float scale; int ascent; } font_t;
static stbtt_fontinfo fi_reg, fi_bold; static int fonts_ready;
static void fonts_init(void)
{
    if (fonts_ready) return;
    stbtt_InitFont(&fi_reg, font_regular, 0);
    stbtt_InitFont(&fi_bold, font_bold, 0);
    fonts_ready = 1;
}
/* glyph cache per font object */
typedef struct glyph_s { wchar_t c; int w, h, xo, yo, adv; uint8_t *bm; struct glyph_s *next; } glyph_t;
typedef struct { int kind; font_t f; glyph_t *g[256]; } fontobj_t;
static glyph_t *glyph(fontobj_t *o, wchar_t c)
{
    glyph_t *g = o->g[c & 255];
    for (; g; g = g->next) if (g->c == c) return g;
    g = calloc(1, sizeof *g); g->c = c;
    int adv, lsb; stbtt_GetCodepointHMetrics(o->f.fi, (int)c, &adv, &lsb);
    g->adv = (int)(adv * o->f.scale + 0.5f);
    g->bm = stbtt_GetCodepointBitmap(o->f.fi, 0, o->f.scale, (int)c, &g->w, &g->h, &g->xo, &g->yo);
    g->next = o->g[c & 255]; o->g[c & 255] = g;
    return g;
}

/* ---------------------------------------------------------------- GDI */
/* Without OpenGL (Android, or no GL driver) the front end falls back to its
 * GDI path: the picture and the overlay boxes are drawn to the window's DC.
 * Here those calls become textured rectangles on an SDL renderer (GPU scaled,
 * OpenGL ES underneath on phones), shown when the DC is released. */
static SDL_Window *win;
static SDL_Renderer *ren; static SDL_Texture *t_frame, *t_ovl; static int tf_w, tf_h, to_w, to_h, ren_dirty;
#if defined(PS4_NO_GL)
static int no_gl = 1;
#else
static int no_gl;
#endif
static SDL_Renderer *renderer(void)
{
    if (!ren && win) {
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    }
    return ren;
}
static SDL_Texture *stream_tex(SDL_Texture **t, int *tw, int *th, int w, int h)
{
    if (*t && *tw >= w && *th >= h) return *t;
    if (*t) SDL_DestroyTexture(*t);
    *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
    *tw = w; *th = h;
    return *t;
}
static void touch_draw(void);
static void touch_draw_gl(void);
static int gles_active;     /* an OpenGL ES context drives the picture (PS4_GLES) */
static void layout(int *W, int *H, int *gh);
static int can_panel(void) { return ren != 0 || gles_active; }
void gles_set_offset(int x, int y); int gles_init(void); void *gles_proc(const char *name);
/* first drawing of a frame on the renderer: clear everything, then keep to
 * the picture's part of the window */
static void ren_frame(void)
{
    if (ren_dirty) return;
    int W, H, gh; layout(&W, &H, &gh);
    SDL_RenderSetViewport(ren, 0);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_Rect g = { 0, 0, W, gh };
    SDL_RenderSetViewport(ren, &g);
}
enum { OBJ_BITMAP = 1, OBJ_FONT, OBJ_BRUSH, OBJ_DC };
typedef struct { int kind; int w, h; uint32_t *px; } bitmap_t;
typedef struct { int kind; DWORD color; } brush_t;
typedef struct { int kind; bitmap_t *bm; fontobj_t *font; DWORD text; int window; } dc_t;
static int stretch_mode;
static brush_t black_brush = { OBJ_BRUSH, 0 };
HDC CreateCompatibleDC(HDC d) { (void)d; dc_t *c = calloc(1, sizeof *c); c->kind = OBJ_DC; return c; }
BOOL DeleteDC(HDC d) { free(d); return 1; }
HGDIOBJ CreateDIBSection(HDC d, const BITMAPINFO *bi, UINT u, void **bits, HANDLE s, DWORD o)
{
    (void)d; (void)u; (void)s; (void)o;
    bitmap_t *b = calloc(1, sizeof *b); b->kind = OBJ_BITMAP;
    b->w = bi->bmiHeader.biWidth; b->h = bi->bmiHeader.biHeight < 0 ? -bi->bmiHeader.biHeight : bi->bmiHeader.biHeight;
    b->px = calloc((size_t)b->w * b->h, 4);
    *bits = b->px;
    return b;
}
HGDIOBJ SelectObject(HDC d, HGDIOBJ o)
{
    dc_t *c = d; int k = *(int *)o; void *old = 0;
    if (k == OBJ_BITMAP) { old = c->bm; c->bm = o; }
    else if (k == OBJ_FONT) { old = c->font; c->font = o; }
    return old;
}
BOOL DeleteObject(HGDIOBJ o)
{
    if (!o || o == &black_brush) return 1;
    int k = *(int *)o;
    if (k == OBJ_BITMAP) { free(((bitmap_t *)o)->px); free(o); }
    else if (k == OBJ_BRUSH) free(o);
    /* fonts stay: their glyph caches are cheap and fonts are reused */
    return 1;
}
HGDIOBJ CreateFontW(int h, int w, int e, int o, int weight, DWORD it, DWORD un, DWORD so, DWORD cs, DWORD op, DWORD cp, DWORD q, DWORD pf, LPCWSTR face)
{
    (void)w; (void)e; (void)o; (void)it; (void)un; (void)so; (void)cs; (void)op; (void)cp; (void)q; (void)pf; (void)face;
    fonts_init();
    fontobj_t *f = calloc(1, sizeof *f); f->kind = OBJ_FONT;
    if (h < 0) h = -h;
    f->f.h = h; f->f.bold = weight >= 600; f->f.fi = f->f.bold ? &fi_bold : &fi_reg;
    /* GDI's height is the cell (ascent + descent), as stb's pixel height */
    f->f.scale = stbtt_ScaleForPixelHeight(f->f.fi, (float)h);
    int asc, desc, gap; stbtt_GetFontVMetrics(f->f.fi, &asc, &desc, &gap);
    f->f.ascent = (int)(asc * f->f.scale + 0.5f);
    return f;
}
DWORD SetTextColor(HDC d, DWORD c) { dc_t *x = d; DWORD o = x->text; x->text = c; return o; }
int SetBkMode(HDC d, int m) { (void)d; (void)m; return 1; }
HBRUSH CreateSolidBrush(DWORD c) { brush_t *b = calloc(1, sizeof *b); b->kind = OBJ_BRUSH; b->color = c; return b; }
HGDIOBJ GetStockObject(int i) { (void)i; return &black_brush; }
static uint32_t colorref_to_px(DWORD c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }
int FillRect(HDC d, const RECT *r, HBRUSH br)
{
    dc_t *c = d; bitmap_t *b = c ? c->bm : 0;
    if (c && c->window) {
        if (!renderer()) return 0;
        ren_frame();
        DWORD k = ((brush_t *)br)->color;
        SDL_SetRenderDrawColor(ren, k & 255, (k >> 8) & 255, (k >> 16) & 255, 255);
        SDL_Rect q = { r->left, r->top, r->right - r->left, r->bottom - r->top };
        SDL_RenderFillRect(ren, &q);
        ren_dirty = 1;
        return 1;
    }
    if (!b) return 0;
    uint32_t col = colorref_to_px(((brush_t *)br)->color);
    int x0 = r->left < 0 ? 0 : (int)r->left, y0 = r->top < 0 ? 0 : (int)r->top;
    int x1 = r->right > b->w ? b->w : (int)r->right, y1 = r->bottom > b->h ? b->h : (int)r->bottom;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) b->px[(size_t)y * b->w + x] = col;
    return 1;
}
BOOL TextOutW(HDC d, int x, int y, LPCWSTR s, int n)
{
    dc_t *c = d; bitmap_t *b = c ? c->bm : 0; fontobj_t *f = c ? c->font : 0;
    if (!b || !f) return 0;
    uint32_t col = colorref_to_px(c->text);
    int cr = (col >> 16) & 255, cg = (col >> 8) & 255, cb = col & 255;
    int pen = x, base = y + f->f.ascent;
    for (int i = 0; i < n; i++) {
        glyph_t *g = glyph(f, s[i]);
        for (int gy = 0; gy < g->h; gy++) {
            int py = base + g->yo + gy;
            if (py < 0 || py >= b->h) continue;
            for (int gx = 0; gx < g->w; gx++) {
                int px = pen + g->xo + gx, a = g->bm[gy * g->w + gx];
                if (px < 0 || px >= b->w || !a) continue;
                uint32_t *p = &b->px[(size_t)py * b->w + px];
                int r = (*p >> 16) & 255, gg = (*p >> 8) & 255, bb = *p & 255;
                r += (cr - r) * a / 255; gg += (cg - gg) * a / 255; bb += (cb - bb) * a / 255;
                *p = (uint32_t)(r << 16 | gg << 8 | bb);
            }
        }
        pen += g->adv;
        if (i + 1 < n) pen += (int)(stbtt_GetCodepointKernAdvance(f->f.fi, (int)s[i], (int)s[i + 1]) * f->f.scale);
    }
    return 1;
}
BOOL GetTextExtentPoint32W(HDC d, LPCWSTR s, int n, SIZE *sz)
{
    dc_t *c = d; fontobj_t *f = c ? c->font : 0;
    sz->cx = 0; sz->cy = f ? f->f.h : 0;
    if (!f) return 0;
    int w = 0;
    for (int i = 0; i < n; i++) {
        w += glyph(f, s[i])->adv;
        if (i + 1 < n) w += (int)(stbtt_GetCodepointKernAdvance(f->f.fi, (int)s[i], (int)s[i + 1]) * f->f.scale);
    }
    sz->cx = w;
    return 1;
}
int SetStretchBltMode(HDC d, int m) { (void)d; int o = stretch_mode; stretch_mode = m; return o; }
BOOL SetBrushOrgEx(HDC d, int x, int y, POINT *p) { (void)d; (void)x; (void)y; (void)p; return 1; }
BOOL GdiFlush(void) { return 1; }
/* src rectangle of a top-down or bottom-up 32-bit picture into a DC's bitmap */
static void stretch(bitmap_t *b, int dx, int dy, int dw, int dh, const uint32_t *src, int sw_total, int sh_total, int bottom_up,
                    int sx, int sy, int sw, int sh)
{
    if (!b || dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    int smooth = stretch_mode == HALFTONE && (dw < sw || dh < sh || dw > sw * 2);
    for (int y = 0; y < dh; y++) {
        int ty = dy + y; if (ty < 0 || ty >= b->h) continue;
        double fy = sy + (y + 0.5) * sh / dh - 0.5;
        for (int x = 0; x < dw; x++) {
            int tx = dx + x; if (tx < 0 || tx >= b->w) continue;
            double fx = sx + (x + 0.5) * sw / dw - 0.5;
            uint32_t c;
            #define SRC(xx, yy) src[(size_t)(bottom_up ? sh_total - 1 - (yy) : (yy)) * sw_total + (xx)]
            if (smooth) {
                int x0 = (int)fx, y0 = (int)fy; if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
                int x1 = x0 + 1 < sw_total ? x0 + 1 : x0, y1 = y0 + 1 < sh_total ? y0 + 1 : y0;
                double ax = fx - x0, ay = fy - y0; if (ax < 0) ax = 0; if (ay < 0) ay = 0;
                uint32_t p00 = SRC(x0, y0), p10 = SRC(x1, y0), p01 = SRC(x0, y1), p11 = SRC(x1, y1);
                c = 0;
                for (int k = 0; k < 24; k += 8) {
                    double v = (((p00 >> k) & 255) * (1 - ax) + ((p10 >> k) & 255) * ax) * (1 - ay) +
                               (((p01 >> k) & 255) * (1 - ax) + ((p11 >> k) & 255) * ax) * ay;
                    c |= (uint32_t)(v + 0.5) << k;
                }
            } else {
                int xx = (int)(fx + 0.5), yy = (int)(fy + 0.5);
                if (xx < 0) xx = 0; if (yy < 0) yy = 0; if (xx >= sw_total) xx = sw_total - 1; if (yy >= sh_total) yy = sh_total - 1;
                c = SRC(xx, yy);
            }
            #undef SRC
            b->px[(size_t)ty * b->w + tx] = c & 0xFFFFFF;
        }
    }
}
int StretchDIBits(HDC d, int dx, int dy, int dw, int dh, int sx, int sy, int sw, int sh, const void *bits, const BITMAPINFO *bi, UINT u, DWORD rop)
{
    (void)u; (void)rop;
    dc_t *c = d;
    int W = bi->bmiHeader.biWidth, H = bi->bmiHeader.biHeight;
    if (c && c->window) {
        if (!renderer() || !stream_tex(&t_frame, &tf_w, &tf_h, W, H < 0 ? -H : H)) return 0;
        ren_frame();
        SDL_Rect sr = { sx, sy, sw, sh }, dr = { dx, dy, dw, dh };
        SDL_UpdateTexture(t_frame, &sr, (const uint32_t *)bits + (size_t)sy * W + sx, W * 4);
        SDL_SetTextureScaleMode(t_frame, stretch_mode == HALFTONE ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        SDL_RenderCopyEx(ren, t_frame, &sr, &dr, 0, 0, H > 0 ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE);
        ren_dirty = 1;
        return dh;
    }
    stretch(c ? c->bm : 0, dx, dy, dw, dh, bits, W, H < 0 ? -H : H, H > 0, sx, sy, sw, sh);
    return dh;
}
BOOL StretchBlt(HDC d, int dx, int dy, int dw, int dh, HDC s, int sx, int sy, int sw, int sh, DWORD rop)
{
    (void)rop;
    dc_t *a = d, *b = s;
    if (!b || !b->bm) return 0;
    if (a && a->window) {
        bitmap_t *m = b->bm;
        if (!renderer() || !stream_tex(&t_ovl, &to_w, &to_h, m->w, m->h)) return 0;
        ren_frame();
        if (sx < 0 || sy < 0 || sx + sw > m->w || sy + sh > m->h) return 0;
        SDL_Rect sr = { sx, sy, sw, sh }, dr = { dx, dy, dw, dh };
        SDL_UpdateTexture(t_ovl, &sr, m->px + (size_t)sy * m->w + sx, m->w * 4);
        SDL_SetTextureScaleMode(t_ovl, SDL_ScaleModeNearest);
        SDL_RenderCopy(ren, t_ovl, &sr, &dr);
        ren_dirty = 1;
        return 1;
    }
    stretch(a ? a->bm : 0, dx, dy, dw, dh, b->bm->px, b->bm->w, b->bm->h, 0, sx, sy, sw, sh);
    return 1;
}
BOOL BitBlt(HDC d, int dx, int dy, int w, int h, HDC s, int sx, int sy, DWORD rop) { return StretchBlt(d, dx, dy, w, h, s, sx, sy, w, h, rop); }

/* ---------------------------------------------------------------- window */
static WNDPROC wproc; static SDL_GLContext glc;
static int fullscreen_style;
static dc_t window_dc = { OBJ_DC, 0, 0, 0, 1 };
UINT RegisterClassExW(const WNDCLASSEXW *c) { wproc = c->lpfnWndProc; return 1; }
HWND CreateWindowExW(DWORD ex, LPCWSTR cls, LPCWSTR title, DWORD style, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, void *l)
{
    (void)ex; (void)cls; (void)style; (void)x; (void)y; (void)p; (void)m; (void)i; (void)l;
    char t[256]; w2u(title, t, sizeof t);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef PS4_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif
    Uint32 fl = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN | (no_gl ? 0 : SDL_WINDOW_OPENGL);
#ifdef __ANDROID__
    fl = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_ALLOW_HIGHDPI | (no_gl ? 0 : SDL_WINDOW_OPENGL);
#endif
    { const char *e = getenv("PS4_WINDOW"); int a, b; if (e && sscanf(e, "%dx%d", &a, &b) == 2) { w = a; h = b; } }   /* tests */
    win = SDL_CreateWindow(t, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, fl);
    if (win && no_gl) renderer();
    return win;
}
BOOL ShowWindow(HWND h, int c) { (void)c; SDL_ShowWindow(h); return 1; }
BOOL UpdateWindow(HWND h) { (void)h; return 1; }
BOOL DestroyWindow(HWND h) { if (glc) SDL_GL_DeleteContext(glc); SDL_DestroyWindow(h); return 1; }
BOOL SetWindowTextW(HWND h, LPCWSTR t) { char u[256]; w2u(t, u, sizeof u); SDL_SetWindowTitle(h, u); return 1; }
LRESULT DefWindowProcW(HWND h, UINT m, WPARAM w, LPARAM l) { (void)h; (void)m; (void)w; (void)l; return 0; }
BOOL GetClientRect(HWND h, RECT *r)
{
    int w = 0, hh = 0;
    if (h) {
        int H; layout(&w, &H, &hh);
        if (gles_active) gles_set_offset(0, H - hh);   /* GL counts from the bottom: the picture stays on top */
    }
    r->left = r->top = 0; r->right = w; r->bottom = hh;
    return 1;
}
BOOL GetWindowRect(HWND h, RECT *r)
{
    int x, y, w, hh; SDL_GetWindowPosition(h, &x, &y); SDL_GetWindowSize(h, &w, &hh);
    r->left = x; r->top = y; r->right = x + w; r->bottom = y + hh;
    return 1;
}
BOOL AdjustWindowRect(RECT *r, DWORD s, BOOL m) { (void)r; (void)s; (void)m; return 1; }
LONG_PTR GetWindowLongPtrW(HWND h, int i) { (void)h; (void)i; return fullscreen_style ? WS_POPUP : WS_OVERLAPPEDWINDOW; }
LONG_PTR SetWindowLongPtrW(HWND h, int i, LONG_PTR v)
{
    (void)i;
    fullscreen_style = (v & WS_POPUP) != 0;
    SDL_SetWindowFullscreen(h, fullscreen_style ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    return 0;
}
BOOL SetWindowPos(HWND h, HWND a, int x, int y, int w, int hh, UINT f)
{
    (void)a;
    if (fullscreen_style) return 1;
    if (!(f & 2)) SDL_SetWindowPosition(h, x, y);       /* SWP_NOMOVE */
    if (!(f & 1) && w > 0 && hh > 0) SDL_SetWindowSize(h, w, hh);   /* SWP_NOSIZE */
    return 1;
}
HMONITOR MonitorFromWindow(HWND h, DWORD f) { (void)f; return h; }
BOOL GetMonitorInfoW(HMONITOR m, MONITORINFO *mi)
{
    SDL_Rect r; SDL_GetDisplayBounds(SDL_GetWindowDisplayIndex(m), &r);
    mi->rcMonitor.left = r.x; mi->rcMonitor.top = r.y; mi->rcMonitor.right = r.x + r.w; mi->rcMonitor.bottom = r.y + r.h;
    mi->rcWork = mi->rcMonitor;
    return 1;
}
BOOL SetProcessDPIAware(void) { return 1; }
int ShowCursor(BOOL s) { SDL_ShowCursor(s ? SDL_ENABLE : SDL_DISABLE); return 0; }
HCURSOR SetCursor(HCURSOR c) { SDL_ShowCursor(c ? SDL_ENABLE : SDL_DISABLE); return 0; }
HCURSOR LoadCursorW(HINSTANCE i, LPCWSTR n) { (void)i; (void)n; return (HCURSOR)1; }
HICON LoadIconW(HINSTANCE i, LPCWSTR n) { (void)i; (void)n; return (HICON)1; }
HDC GetDC(HWND h) { (void)h; return &window_dc; }
int ReleaseDC(HWND h, HDC d)
{
    (void)h;
    if (d == &window_dc && ren && ren_dirty) { touch_draw(); SDL_RenderPresent(ren); ren_dirty = 0; }
    return 1;
}
HDC BeginPaint(HWND h, PAINTSTRUCT *p) { (void)h; memset(p, 0, sizeof *p); return &window_dc; }
BOOL EndPaint(HWND h, const PAINTSTRUCT *p) { (void)h; (void)p; return 1; }
BOOL InvalidateRect(HWND h, const RECT *r, BOOL e) { (void)h; (void)r; (void)e; return 1; }
UINT_PTR SetTimer(HWND h, UINT_PTR id, UINT ms, void *f) { (void)h; (void)ms; (void)f; return id; }
BOOL KillTimer(HWND h, UINT_PTR id) { (void)h; (void)id; return 1; }
int MessageBoxW(HWND h, LPCWSTR text, LPCWSTR cap, UINT type)
{
    (void)h;
    char t[2048], c[256]; w2u(text, t, sizeof t); w2u(cap, c, sizeof c);
    if ((type & 0xF) == MB_YESNO) {
        const SDL_MessageBoxButtonData b[2] = { { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, IDYES, "Да" }, { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 7, "Нет" } };
        const SDL_MessageBoxData d = { SDL_MESSAGEBOX_INFORMATION, win, c, t, 2, b, 0 };
        int id = 7;
        if (SDL_ShowMessageBox(&d, &id) != 0) { fprintf(stderr, "%s\n%s\n", c, t); return 7; }
        return id;
    }
    if (SDL_ShowSimpleMessageBox((type & MB_ICONERROR) ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, c, t, win) != 0)
        fprintf(stderr, "%s\n%s\n", c, t);
    return 1;
}

/* keys: SDL key codes as Windows virtual keys */
static int vk_of(SDL_Keycode k)
{
    if (k >= 'a' && k <= 'z') return k - 'a' + 'A';
    if (k >= '0' && k <= '9') return (int)k;
    if (k >= SDLK_F1 && k <= SDLK_F12) return 0x70 + (k - SDLK_F1);
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN;
    case SDLK_ESCAPE: return VK_ESCAPE; case SDLK_TAB: return VK_TAB; case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_SPACE: return VK_SPACE; case SDLK_DELETE: return 0x2E; case SDLK_INSERT: return 0x2D;
    case SDLK_LEFT: return VK_LEFT; case SDLK_RIGHT: return VK_RIGHT; case SDLK_UP: return VK_UP; case SDLK_DOWN: return VK_DOWN;
    case SDLK_HOME: return 0x24; case SDLK_END: return 0x23; case SDLK_PAGEUP: return 0x21; case SDLK_PAGEDOWN: return 0x22;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT; case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    case SDLK_LALT: case SDLK_RALT: return VK_MENU; case SDLK_PAUSE: return VK_PAUSE;
    case SDLK_MINUS: return 0xBD; case SDLK_EQUALS: return 0xBB; case SDLK_COMMA: return 0xBC; case SDLK_PERIOD: return 0xBE;
    case SDLK_SEMICOLON: return 0xBA; case SDLK_SLASH: return 0xBF; case SDLK_BACKQUOTE: return 0xC0;
    case SDLK_LEFTBRACKET: return 0xDB; case SDLK_RIGHTBRACKET: return 0xDD; case SDLK_BACKSLASH: return 0xDC; case SDLK_QUOTE: return 0xDE;
    }
    if (k >= SDLK_KP_1 && k <= SDLK_KP_9) return 0x61 + (k - SDLK_KP_1);
    if (k == SDLK_KP_0) return 0x60;
    return 0;
}
static SDL_Keycode sdl_of(int vk)
{
    for (SDL_Keycode k = 8; k < 128; k++) if (vk_of(k) == vk) return k;
    static const SDL_Keycode extra[] = { SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F8, SDLK_F9, SDLK_F10, SDLK_F11, SDLK_F12,
        SDLK_LEFT, SDLK_RIGHT, SDLK_UP, SDLK_DOWN, SDLK_HOME, SDLK_END, SDLK_PAGEUP, SDLK_PAGEDOWN, SDLK_INSERT, SDLK_LSHIFT, SDLK_LCTRL, SDLK_LALT, SDLK_PAUSE };
    for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++) if (vk_of(extra[i]) == vk) return extra[i];
    return 0;
}
UINT MapVirtualKeyW(UINT vk, UINT t) { (void)t; return (UINT)vk; }
int GetKeyNameTextW(LONG l, LPWSTR out, int n)
{
    SDL_Keycode k = sdl_of((int)((l >> 16) & 0xFFFF));
    if (!k) return 0;
    const char *name = SDL_GetKeyName(k);
    if (!name || !*name) return 0;
    u2w(name, -1, out, n);
    return (int)wcslen(out);
}
short GetKeyState(int vk)
{
    const Uint8 *ks = SDL_GetKeyboardState(0);
    SDL_Keycode k = sdl_of(vk);
    return k && ks[SDL_GetScancodeFromKey(k)] ? (short)0x8000 : 0;
}

/* the message loop */
static MSG pending[64]; static int npending;
static void post(UINT m, WPARAM w, LPARAM l)
{
    if (npending < 64) { MSG *x = &pending[npending++]; memset(x, 0, sizeof *x); x->hwnd = win; x->message = m; x->wParam = w; x->lParam = l; }
}
static int quit_posted;
static void drawable_full(int *w, int *h) { if (ren) SDL_GetRendererOutputSize(ren, w, h); else SDL_GL_GetDrawableSize(win, w, h); }
#define drawable drawable_full
static int touch_event(const SDL_Event *e);
/* PS4_KEYS="ms:vk,ms:vk,..." (vk in hex): scripted key presses, for tests */
static void scripted_keys(void)
{
    static const char *p; static int init; static Uint64 t0;
    if (!init) { init = 1; p = getenv("PS4_KEYS"); t0 = SDL_GetTicks64(); }
    while (p && *p) {
        unsigned ms, vk; int n = 0;
        if (sscanf(p, "%u:%x%n", &ms, &vk, &n) != 2) { p = 0; break; }
        if (SDL_GetTicks64() - t0 < ms) break;
        post(WM_KEYDOWN, vk, 0); post(WM_KEYUP, vk, 0);
        p += n; if (*p == ',') p++;
    }
}
static void pump(void)
{
    SDL_Event e;
    scripted_keys();
    while (npending < 60 && SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: post(WM_CLOSE, 0, 0); break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_CLOSE) post(WM_CLOSE, 0, 0);
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) post(WM_ACTIVATE, 0, 0);
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) post(WM_ACTIVATE, 1, 0);
            break;
        case SDL_KEYDOWN: case SDL_KEYUP: {
            int vk = e.key.keysym.sym == SDLK_AC_BACK ? VK_ESCAPE : vk_of(e.key.keysym.sym);
            if (!vk) break;
            int alt = (e.key.keysym.mod & KMOD_ALT) != 0;
            LPARAM l = (e.key.repeat ? (1 << 30) : 0) | (alt ? (1 << 29) : 0);
            if (e.type == SDL_KEYDOWN) post(alt ? WM_SYSKEYDOWN : WM_KEYDOWN, (WPARAM)vk, l);
            else post(alt ? WM_SYSKEYUP : WM_KEYUP, (WPARAM)vk, l);
            break; }
        case SDL_MOUSEMOTION: {
            /* window points -> drawable pixels (HiDPI) */
            int ww, wh, dw, dh; SDL_GetWindowSize(win, &ww, &wh); drawable(&dw, &dh);
            int x = ww ? e.motion.x * dw / ww : e.motion.x, y = wh ? e.motion.y * dh / wh : e.motion.y;
            post(WM_SETCURSOR, 0, 1);
            post(WM_MOUSEMOVE, 0, (LPARAM)((x & 0xFFFF) | ((y & 0xFFFF) << 16)));
            break; }
        case SDL_MOUSEBUTTONDOWN: {
            int ww, wh, dw, dh; SDL_GetWindowSize(win, &ww, &wh); drawable(&dw, &dh);
            int x = ww ? e.button.x * dw / ww : e.button.x, y = wh ? e.button.y * dh / wh : e.button.y;
            LPARAM l = (LPARAM)((x & 0xFFFF) | ((y & 0xFFFF) << 16));
            if (e.button.button == SDL_BUTTON_LEFT) post(WM_LBUTTONDOWN, 0, l);
            else if (e.button.button == SDL_BUTTON_RIGHT) post(WM_RBUTTONDOWN, 0, l);
            break; }
        case SDL_MOUSEWHEEL: post(WM_MOUSEWHEEL, (WPARAM)(((e.wheel.y * 120) & 0xFFFF) << 16), 0); break;
        case SDL_APP_WILLENTERBACKGROUND: post(0x8001, 0, 0); return;   /* WM_APP_SUSPEND: save now, the system may end us */
        case SDL_APP_DIDENTERFOREGROUND: post(0x8002, 0, 0); break;     /* WM_APP_RESUME */
        case SDL_FINGERDOWN: case SDL_FINGERUP: case SDL_FINGERMOTION: touch_event(&e); break;
        case SDL_CONTROLLERDEVICEADDED: case SDL_CONTROLLERDEVICEREMOVED: break;   /* polled in xinput_get_state */
        }
    }
}
BOOL PeekMessageW(MSG *m, HWND h, UINT a, UINT b, UINT f)
{
    (void)h; (void)a; (void)b; (void)f;
    if (quit_posted) { memset(m, 0, sizeof *m); m->message = WM_QUIT; quit_posted = 0; return 1; }
    if (!npending) pump();
    if (!npending) return 0;
    *m = pending[0];
    memmove(pending, pending + 1, sizeof pending[0] * (size_t)--npending);
    return 1;
}
BOOL TranslateMessage(const MSG *m) { (void)m; return 1; }
LRESULT DispatchMessageW(const MSG *m) { return wproc ? wproc(m->hwnd, m->message, m->wParam, m->lParam) : 0; }
void PostQuitMessage(int c) { (void)c; quit_posted = 1; }


/* ---------------------------------------------------------------- touch controls */
/* On phones and tablets: a Mega Drive pad (d-pad, A B C, Start, the menu and
 * turbo), sized in millimetres from the screen's density. When the screen is
 * much squarer than the picture (a Fold opened, a tablet held upright-ish) the
 * picture moves to the top and the pad gets the free panel under it;
 * otherwise it is drawn over the picture, half transparent. Buttons act as an
 * XInput pad, so they follow the pad bindings; touches elsewhere are mouse
 * clicks for the menus. Hidden while a real controller is connected. */
#ifdef __ANDROID__
static int touch_on = 1;
#else
static int touch_on = -1;   /* PS4_TOUCH=1 to try it on a desktop */
#endif
enum { TB_DPAD, TB_A, TB_B, TB_C, TB_START, TB_MENU, TB_TURBO, TB_N };
typedef struct { float x, y, r; const char *label; } tbtn_t;
static tbtn_t tb[TB_N];
static SDL_FingerID tf_id[10]; static int tf_btn[10], tf_used[10];
static WORD touch_buttons; static int turbo_held;
static int panel_h;                  /* height of the panel under the picture (0: drawn over it) */
static SDL_Texture *t_label[TB_N]; static unsigned g_label[TB_N]; static int label_w[TB_N], label_h[TB_N], label_px[TB_N];
static int touch_enabled(void)
{
    if (touch_on < 0) { const char *e = getenv("PS4_TOUCH"); touch_on = e && *e == '1'; }
    if (!touch_on) return 0;
    for (int i = 0; i < 4; i++) if (pads[i] && SDL_GameControllerGetAttached(pads[i])) return 0;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) return 0;
    return 1;
}
static float dpi(void)
{
    static float d;
    if (!d) {
        float dd = 0, hd = 0, vd = 0;
        const char *e = getenv("PS4_DPI");
        if (e && atof(e) > 30) d = (float)atof(e);
        else if (SDL_GetDisplayDPI(0, &dd, &hd, &vd) == 0 && hd > 60) d = hd;
#ifdef __ANDROID__
        else { int w, h; drawable(&w, &h); d = (h < w ? h : w) / 2.6f; }   /* a phone is ~65 mm across */
#else
        else d = 96;
#endif
    }
    return d;
}
static float mm(float v) { return v * dpi() / 25.4f; }
/* the full drawable size, and the part the picture gets */
static int can_panel(void);
static void layout(int *W, int *H, int *gh)
{
    int w, h; drawable_full(&w, &h);
    *W = w; *H = h;
    panel_h = 0;
    if (touch_enabled() && can_panel()) {
        int pic = (int)(w * 9.0f / 16.0f + 0.5f);         /* a 16:9 picture across the width */
        int free = h - pic;
        if (free >= mm(22) && free >= h / 7) panel_h = free;
    }
    *gh = h - panel_h;
}
static void touch_layout(int W, int H)
{
    float m = mm(3.5f);
    float rb = mm(6.0f), rd = mm(13.0f), rs = mm(4.2f);
    if (panel_h) {
        float top = (float)(H - panel_h), cy = top + panel_h / 2.0f;
        if (rd > panel_h * 0.42f) rd = panel_h * 0.42f;
        if (rb > panel_h * 0.19f) rb = panel_h * 0.19f;
        if (rs > panel_h * 0.13f) rs = panel_h * 0.13f;
        float s = rb * 2.35f;
        tb[TB_DPAD]  = (tbtn_t){ m * 2 + rd, cy, rd, "" };
        tb[TB_C]     = (tbtn_t){ W - m * 2 - rb, cy - s * 0.35f, rb, "C" };
        tb[TB_B]     = (tbtn_t){ tb[TB_C].x - s, cy, rb, "B" };
        tb[TB_A]     = (tbtn_t){ tb[TB_B].x - s, cy + s * 0.35f, rb, "A" };
        float mid = W / 2.0f, gap = rs * 3.0f;
        tb[TB_MENU]  = (tbtn_t){ mid - gap, cy + rs * 1.2f, rs, "МЕНЮ" };
        tb[TB_START] = (tbtn_t){ mid, cy + rs * 1.2f, rs, "START" };
        tb[TB_TURBO] = (tbtn_t){ mid + gap, cy + rs * 1.2f, rs, "ТУРБО" };
    } else {
        float lim = (H < W ? H : W);
        if (rd > lim * 0.2f) rd = lim * 0.2f;
        if (rb > lim * 0.085f) rb = lim * 0.085f;
        if (rs > lim * 0.06f) rs = lim * 0.06f;
        float s = rb * 2.35f;
        tb[TB_DPAD]  = (tbtn_t){ m + rd, H - m - rd, rd, "" };
        tb[TB_C]     = (tbtn_t){ W - m - rb, H - m - rb - s * 0.75f, rb, "C" };
        tb[TB_B]     = (tbtn_t){ tb[TB_C].x - s, tb[TB_C].y + s * 0.35f, rb, "B" };
        tb[TB_A]     = (tbtn_t){ tb[TB_B].x - s, tb[TB_B].y + s * 0.35f, rb, "A" };
        /* the small ones at the edges, clear of the game's windows at the bottom */
        tb[TB_TURBO] = (tbtn_t){ W - m - rs * 1.4f, tb[TB_C].y - rb - mm(9), rs, "ТУРБО" };
        tb[TB_START] = (tbtn_t){ W - m - rs * 1.4f, tb[TB_TURBO].y - rs * 3.4f, rs, "START" };
        tb[TB_MENU]  = (tbtn_t){ m + rs * 1.4f, m + rs * 0.9f, rs, "МЕНЮ" };
    }
}
/* d-pad bits from a point (8 directions, a dead zone in the middle) */
static WORD dpad_bits(float dx, float dy, float r)
{
    float d = SDL_sqrtf(dx * dx + dy * dy);
    if (d < r * 0.18f) return 0;
    float a = SDL_atan2f(dy, dx) * 57.29578f;   /* 0 = right, 90 = down */
    WORD b = 0;
    if (a > -67.5f && a < 67.5f) b |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (a > 112.5f || a < -112.5f) b |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (a > 22.5f && a < 157.5f) b |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (a < -22.5f && a > -157.5f) b |= XINPUT_GAMEPAD_DPAD_UP;
    return b;
}
static int hit(float x, float y)
{
    int best = -1; float bd = 1e30f;
    for (int i = 0; i < TB_N; i++) {
        float dx = x - tb[i].x, dy = y - tb[i].y, r = tb[i].r * (i == TB_DPAD ? 1.15f : i <= TB_C ? 1.3f : 1.6f);
        float d = dx * dx + dy * dy;
        if (d <= r * r && d / (r * r) < bd) { bd = d / (r * r); best = i; }
    }
    return best;
}
static void touch_recompute(float *fx, float *fy)
{
    WORD b = 0; int turbo = 0;
    for (int k = 0; k < 10; k++) {
        if (!tf_used[k] || tf_btn[k] < 0) continue;
        switch (tf_btn[k]) {
        case TB_DPAD: b |= dpad_bits(fx[k] - tb[TB_DPAD].x, fy[k] - tb[TB_DPAD].y, tb[TB_DPAD].r); break;
        case TB_A: b |= XINPUT_GAMEPAD_X; break;        /* the default bindings: X = A, B = B, A = C */
        case TB_B: b |= XINPUT_GAMEPAD_B; break;
        case TB_C: b |= XINPUT_GAMEPAD_A; break;
        case TB_START: b |= XINPUT_GAMEPAD_START; break;
        case TB_MENU: b |= XINPUT_GAMEPAD_BACK; break;
        case TB_TURBO: turbo = 1; break;
        }
    }
    touch_buttons = b;
    if (turbo != turbo_held) { post(turbo ? WM_KEYDOWN : WM_KEYUP, VK_TAB, 0); turbo_held = turbo; }
}
static int touch_event(const SDL_Event *e)
{
    if (!touch_enabled()) return 0;
    static float fx[10], fy[10];
    int W, H, gh; layout(&W, &H, &gh);
    touch_layout(W, H);
    float x = e->tfinger.x * W, y = e->tfinger.y * H;
    int k = -1;
    for (int i = 0; i < 10; i++) if (tf_used[i] && tf_id[i] == e->tfinger.fingerId) k = i;
    if (e->type == SDL_FINGERDOWN) {
        if (k < 0) for (int i = 0; i < 10 && k < 0; i++) if (!tf_used[i]) k = i;
        if (k < 0) return 1;
        tf_used[k] = 1; tf_id[k] = e->tfinger.fingerId; tf_btn[k] = hit(x, y);
        if (tf_btn[k] < 0 && y < gh) {   /* not a button: a click for the menus */
            LPARAM l = (LPARAM)(((int)x & 0xFFFF) | (((int)y & 0xFFFF) << 16));
            post(WM_MOUSEMOVE, 0, l); post(WM_LBUTTONDOWN, 0, l);
        }
    } else if (k < 0) return 1;
    else if (e->type == SDL_FINGERUP) tf_used[k] = 0;
    else if (tf_btn[k] >= TB_A && tf_btn[k] <= TB_C) {   /* sliding between A, B and C */
        int h2 = hit(x, y);
        if (h2 >= TB_A && h2 <= TB_C) tf_btn[k] = h2;
    }
    if (k >= 0) { fx[k] = x; fy[k] = y; }
    touch_recompute(fx, fy);
    return 1;
}

/* drawing, on the SDL renderer or on OpenGL ES (port/gles.c) */
void gles_overlay_begin(int w, int h); void gles_overlay_end(void);
void gles_tris(const float *xy, int n, int r, int g, int b, int a);
unsigned gles_texture(const uint32_t *argb, int w, int h); void gles_texture_free(unsigned t);
void gles_image(unsigned t, float x, float y, float w, float h, int alpha);
static int draw_gl;
static void tris(const float *xy, int n, SDL_Color c)
{
    if (draw_gl) { gles_tris(xy, n, c.r, c.g, c.b, c.a); return; }
    SDL_Vertex v[256];
    for (int i = 0; i < n && i < 256; i++) v[i] = (SDL_Vertex){ { xy[i * 2], xy[i * 2 + 1] }, c, { 0, 0 } };
    SDL_RenderGeometry(ren, 0, v, n < 256 ? n : 256, 0, 0);
}
static void fill_circle(float cx, float cy, float r, SDL_Color c)
{
    float xy[48 * 6]; int n = 0;
    for (int i = 0; i < 48; i++) {
        float a0 = i * 6.2831853f / 48, a1 = (i + 1) * 6.2831853f / 48;
        xy[n++] = cx; xy[n++] = cy;
        xy[n++] = cx + r * SDL_cosf(a0); xy[n++] = cy + r * SDL_sinf(a0);
        xy[n++] = cx + r * SDL_cosf(a1); xy[n++] = cy + r * SDL_sinf(a1);
    }
    tris(xy, n / 2, c);
}
static void fill_rect(float x, float y, float w, float h, SDL_Color c)
{
    float xy[12] = { x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h };
    tris(xy, 6, c);
}
static void label(int i, int px, float cx, float cy, int alpha)
{
    if (label_px[i] != px) {   /* (re)make it at this size */
        if (t_label[i]) { SDL_DestroyTexture(t_label[i]); t_label[i] = 0; }
        if (g_label[i]) { gles_texture_free(g_label[i]); g_label[i] = 0; }
        label_px[i] = px;
    }
    if (draw_gl ? !g_label[i] : !t_label[i]) {
        fonts_init();
        fontobj_t *f = CreateFontW(px, 0, 0, 0, 700, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        wchar_t w[16]; u2w(tb[i].label, -1, w, 16);
        dc_t dc = { OBJ_DC, 0, f, 0xFFFFFF, 0 };
        SIZE sz; GetTextExtentPoint32W(&dc, w, (int)wcslen(w), &sz);
        bitmap_t bm = { OBJ_BITMAP, (int)sz.cx + 2, (int)sz.cy + 2, calloc((size_t)(sz.cx + 2) * (sz.cy + 2), 4) };
        dc.bm = &bm;
        TextOutW(&dc, 1, 1, w, (int)wcslen(w));
        for (int k = 0; k < bm.w * bm.h; k++) { uint32_t g = bm.px[k] & 255; bm.px[k] = g << 24 | 0xFFFFFF; }   /* white, coverage as alpha */
        if (draw_gl) g_label[i] = gles_texture(bm.px, bm.w, bm.h);
        else {
            t_label[i] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, bm.w, bm.h);
            SDL_UpdateTexture(t_label[i], 0, bm.px, bm.w * 4);
            SDL_SetTextureBlendMode(t_label[i], SDL_BLENDMODE_BLEND);
        }
        label_w[i] = bm.w; label_h[i] = bm.h;
        free(bm.px);
    }
    SDL_FRect r = { cx - label_w[i] / 2.0f, cy - label_h[i] / 2.0f, (float)label_w[i], (float)label_h[i] };
    if (draw_gl) gles_image(g_label[i], r.x, r.y, r.w, r.h, alpha);
    else { SDL_SetTextureAlphaMod(t_label[i], (Uint8)alpha); SDL_RenderCopyF(ren, t_label[i], 0, &r); }
}
/* a filled rounded rectangle (r = h/2: a pill, r = w/2 = h/2: a circle),
 * turned by ang radians around its centre; one fan, so no part is drawn
 * twice (matters when it is half transparent) */
static void round_shape(float cx, float cy, float w, float h, float r, float ang, SDL_Color c)
{
    float pt[4 * 13][2]; int n = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    float ox[4] = { w / 2 - r, -(w / 2 - r), -(w / 2 - r), w / 2 - r }, oy[4] = { h / 2 - r, h / 2 - r, -(h / 2 - r), -(h / 2 - r) };
    for (int k = 0; k < 4; k++)
        for (int i = 0; i <= 12; i++) {
            float t = (k * 90 + i * 7.5f) * 0.0174533f;
            pt[n][0] = ox[k] + r * SDL_cosf(t); pt[n][1] = oy[k] + r * SDL_sinf(t); n++;
        }
    float cs = SDL_cosf(ang), sn = SDL_sinf(ang), xy[4 * 13 * 6]; int m = 0;
    for (int i = 0; i < n; i++) {
        float *p0 = pt[i], *p1 = pt[(i + 1) % n];
        xy[m++] = cx; xy[m++] = cy;
        xy[m++] = cx + p0[0] * cs - p0[1] * sn; xy[m++] = cy + p0[0] * sn + p0[1] * cs;
        xy[m++] = cx + p1[0] * cs - p1[1] * sn; xy[m++] = cy + p1[0] * sn + p1[1] * cs;
    }
    tris(xy, m / 2, c);
}
static SDL_Color rgba(int r, int g, int b, int a) { SDL_Color c = { (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a }; return c; }

/* the pad: a retro console controller look of our own (dark body, dished
 * d-pad, glossy round A B C on a slanted bed, slanted Start-style pills) */
static void touch_draw_common(int W, int H)
{
    touch_layout(W, H);
    int op = panel_h ? 255 : 105;               /* over the picture: half transparent */
#define A(x) ((x) * op / 255)
    if (panel_h) {   /* the body, with a lighter top edge */
        float m = mm(1.5f), top = (float)(H - panel_h) + m, bh = panel_h - 2 * m;
        round_shape(W / 2.0f, top + bh / 2, W - 2 * m, bh, bh * 0.35f, 0, rgba(46, 46, 52, 255));
        round_shape(W / 2.0f, top + bh / 2 + mm(0.6f), W - 2 * m - mm(1.2f), bh - mm(1.2f), bh * 0.34f, 0, rgba(24, 24, 28, 255));
    }
    /* d-pad: a dished disc and a cross on it */
    tbtn_t *d = &tb[TB_DPAD];
    round_shape(d->x, d->y, d->r * 2.05f, d->r * 2.05f, d->r * 1.03f, 0, rgba(12, 12, 14, A(230)));
    round_shape(d->x, d->y, d->r * 1.9f, d->r * 1.9f, d->r * 0.95f, 0, rgba(44, 44, 50, A(235)));
    round_shape(d->x, d->y - d->r * 0.05f, d->r * 1.7f, d->r * 1.7f, d->r * 0.85f, 0, rgba(36, 36, 41, A(235)));
    float q = d->r * 0.31f, l = d->r * 0.86f;
    for (int pass = 0; pass < 2; pass++) {      /* a shadow, then the cross */
        float sh = pass ? 0 : d->r * 0.06f;
        SDL_Color c = pass ? rgba(70, 70, 78, A(245)) : rgba(8, 8, 10, A(200));
        round_shape(d->x, d->y + sh, 2 * q, 2 * l, q * 0.3f, 0, c);
        round_shape(d->x, d->y + sh, 2 * l, 2 * q, q * 0.3f, 0, c);
    }
    WORD bits[4] = { XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT };
    float ax[4] = { 0, 0, -1, 1 }, ay[4] = { -1, 1, 0, 0 };
    for (int i = 0; i < 4; i++) {
        float cx = d->x + ax[i] * (l + q) / 2, cy = d->y + ay[i] * (l + q) / 2;
        float w = ax[i] ? l - q : 2 * q * 0.8f, h = ax[i] ? 2 * q * 0.8f : l - q;
        if (touch_buttons & bits[i]) round_shape(cx, cy, w, h, q * 0.25f, 0, rgba(30, 30, 34, A(255)));
        else {   /* a small arrow-like ridge on each arm */
            float r = q * 0.28f;
            round_shape(cx + ax[i] * (l - q) * 0.18f, cy + ay[i] * (l - q) * 0.18f, r, r, r / 2, 0, rgba(110, 110, 120, A(200)));
        }
    }
    round_shape(d->x, d->y, q * 0.9f, q * 0.9f, q * 0.45f, 0, rgba(56, 56, 62, A(255)));
    /* A B C: a slanted bed, then each button with a shadow and a gloss */
    float slant = SDL_atan2f(tb[TB_C].y - tb[TB_A].y, tb[TB_C].x - tb[TB_A].x);
    float bx = (tb[TB_A].x + tb[TB_C].x) / 2, by = (tb[TB_A].y + tb[TB_C].y) / 2;
    float blen = SDL_sqrtf((tb[TB_C].x - tb[TB_A].x) * (tb[TB_C].x - tb[TB_A].x) + (tb[TB_C].y - tb[TB_A].y) * (tb[TB_C].y - tb[TB_A].y));
    float rb = tb[TB_A].r;
    round_shape(bx, by, blen + rb * 2.7f, rb * 2.7f, rb * 1.35f, slant, rgba(12, 12, 14, A(220)));
    round_shape(bx, by, blen + rb * 2.45f, rb * 2.45f, rb * 1.22f, slant, rgba(30, 30, 35, A(230)));
    WORD bb[TB_N] = { 0, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK, 0 };
    for (int i = TB_A; i <= TB_C; i++) {
        tbtn_t *b = &tb[i]; int on = (touch_buttons & bb[i]) != 0;
        float sink = on ? b->r * 0.06f : 0;
        if (!on) round_shape(b->x, b->y + b->r * 0.1f, b->r * 2, b->r * 2, b->r, 0, rgba(4, 4, 6, A(220)));
        round_shape(b->x, b->y + sink, b->r * 2, b->r * 2, b->r, 0, on ? rgba(34, 34, 40, A(255)) : rgba(58, 58, 66, A(255)));
        round_shape(b->x, b->y + sink + b->r * 0.04f, b->r * 1.7f, b->r * 1.7f, b->r * 0.85f, 0, on ? rgba(28, 28, 33, A(255)) : rgba(44, 44, 51, A(255)));
        if (!on) round_shape(b->x - b->r * 0.32f, b->y - b->r * 0.38f, b->r * 0.7f, b->r * 0.34f, b->r * 0.17f, -0.5f, rgba(255, 255, 255, A(40)));
        label(i, (int)(b->r * 0.95f) < 8 ? 8 : (int)(b->r * 0.95f), b->x, b->y + sink + b->r * 0.03f, on ? A(150) : A(215));
    }
    /* Меню, Start, turbo: slanted pills, the name under each */
    for (int i = TB_START; i < TB_N; i++) {
        tbtn_t *b = &tb[i];
        int on = i == TB_TURBO ? turbo_held : (touch_buttons & bb[i]) != 0;
        float w = b->r * 2.3f, h = b->r * 0.95f;
        round_shape(b->x, b->y, w + b->r * 0.35f, h + b->r * 0.35f, (h + b->r * 0.35f) / 2, -0.35f, rgba(10, 10, 12, A(220)));
        round_shape(b->x, b->y + (on ? b->r * 0.05f : 0), w, h, h / 2, -0.35f, on ? rgba(36, 36, 42, A(255)) : rgba(72, 72, 80, A(255)));
        int px = (int)(b->r * 0.55f); if (px < 8) px = 8;
        label(i, px, b->x, b->y + b->r * 1.1f, A(200));
    }
#undef A
}
static void touch_draw(void)        /* SDL renderer */
{
    if (!touch_enabled() || !ren) return;
    int W, H, gh; layout(&W, &H, &gh);
    SDL_RenderSetViewport(ren, 0);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    draw_gl = 0;
    touch_draw_common(W, H);
}
static void touch_draw_gl(void)     /* OpenGL ES, before the buffers swap */
{
    if (!touch_enabled()) return;
    int W, H, gh; layout(&W, &H, &gh);
    gles_overlay_begin(W, H);
    draw_gl = 1;
    touch_draw_common(W, H);
    gles_overlay_end();
}

/* ---------------------------------------------------------------- WGL */
int ChoosePixelFormat(HDC d, const PIXELFORMATDESCRIPTOR *p) { (void)d; (void)p; return 1; }
BOOL SetPixelFormat(HDC d, int f, const PIXELFORMATDESCRIPTOR *p) { (void)d; (void)f; (void)p; return 1; }
HGLRC wglCreateContext(HDC d)
{
    (void)d;
    if (no_gl) return 0;
    glc = SDL_GL_CreateContext(win);
#ifdef PS4_GLES
    if (glc && (SDL_GL_MakeCurrent(win, glc) != 0 || !gles_init())) { SDL_GL_DeleteContext(glc); glc = 0; }
    gles_active = glc != 0;
#endif
    return glc;
}
BOOL wglMakeCurrent(HDC d, HGLRC c) { (void)d; return SDL_GL_MakeCurrent(win, c) == 0; }
BOOL wglDeleteContext(HGLRC c) { SDL_GL_DeleteContext(c); return 1; }
static BOOL swap_interval(int i) { return SDL_GL_SetSwapInterval(i) == 0; }
void *wglGetProcAddress(const char *n)
{
    if (!strcmp(n, "wglSwapIntervalEXT")) return (void *)swap_interval;
#ifdef PS4_GLES
    return gles_proc(n);
#else
    return SDL_GL_GetProcAddress(n);
#endif
}
BOOL SwapBuffers(HDC d) { (void)d; if (gles_active) touch_draw_gl(); SDL_GL_SwapWindow(win); return 1; }

/* ---------------------------------------------------------------- shell */
static int run_cmd(const char *cmd, char *out, int n)
{
    FILE *p = popen(cmd, "r");
    if (!p) return 0;
    int k = out && fgets(out, n, p) ? 1 : 0;
    int rc = pclose(p);
    if (out && k) { size_t l = strlen(out); while (l && (out[l - 1] == '\n' || out[l - 1] == '\r')) out[--l] = 0; }
    return rc == 0 && (!out || out[0]);
}
#ifdef __ANDROID__
#include <jni.h>
/* the system file picker (PS4Activity.pickRom): the chosen ROM is copied into
 * the app's folder, and its path comes back (null if cancelled) */
BOOL GetOpenFileNameW(OPENFILENAMEW *o)
{
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject act = SDL_AndroidGetActivity();
    if (!env || !act) return 0;
    wchar_t dirw[1024]; GetModuleFileNameW(0, dirw, 1024);
    char dest[1024]; path_u(dirw, dest, sizeof dest);
    char *sl = strrchr(dest, '/'); if (sl) strcpy(sl + 1, "ps4_rom.bin");
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID mid = (*env)->GetMethodID(env, cls, "pickRom", "(Ljava/lang/String;)Ljava/lang/String;");
    BOOL ok = 0;
    if (mid) {
        jstring js = (*env)->NewStringUTF(env, dest);
        jstring r = (jstring)(*env)->CallObjectMethod(env, act, mid, js);
        if (r) {
            const char *c = (*env)->GetStringUTFChars(env, r, 0);
            u2w(c, -1, o->lpstrFile, (int)o->nMaxFile);
            (*env)->ReleaseStringUTFChars(env, r, c);
            (*env)->DeleteLocalRef(env, r);
            ok = 1;
        }
        (*env)->DeleteLocalRef(env, js);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return ok;
}
HINSTANCE ShellExecuteW(HWND h, LPCWSTR op, LPCWSTR file, LPCWSTR a, LPCWSTR d, int s)
{
    (void)h; (void)op; (void)a; (void)d; (void)s;
    char u[1024]; w2u(file, u, sizeof u);
    if (!strncmp(u, "http", 4)) SDL_OpenURL(u);
    return (HINSTANCE)33;
}
#else
BOOL GetOpenFileNameW(OPENFILENAMEW *o)
{
    char path[1024] = "";
    /* the desktop's file dialog (zenity on GNOME and SteamOS, kdialog on KDE) */
    if (!run_cmd("zenity --file-selection --title='Phantasy Star IV: ROM' 2>/dev/null", path, sizeof path) &&
        !run_cmd("kdialog --getopenfilename ~ 2>/dev/null", path, sizeof path)) {
        MessageBoxW(0, L"Положите файл ROM рядом с программой под именем ps4_rus.bin\n"
                       L"или запустите её с путём к ROM: ./PS4 путь/к/rom.bin", L"Phantasy Star IV", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    u2w(path, -1, o->lpstrFile, (int)o->nMaxFile);
    return 1;
}
HINSTANCE ShellExecuteW(HWND h, LPCWSTR op, LPCWSTR file, LPCWSTR a, LPCWSTR d, int s)
{
    (void)h; (void)op; (void)a; (void)d; (void)s;
    char u[1024], cmd[1200]; path_u(file, u, sizeof u);
    snprintf(cmd, sizeof cmd, "xdg-open '%s' >/dev/null 2>&1 &", u);
    if (system(cmd)) {}
    return (HINSTANCE)33;
}
#endif

/* ---------------------------------------------------------------- entry */
int main(int argc, char **argv)
{
    g_argc = argc; g_argv = argv;
    SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
    if (touch_enabled()) SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   /* touches are handled in touch_event */
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    Start();
    return 0;
}

/* ---------------------------------------------------------------- no D3D, no crash reports */
#include "../win/diag.h"
#include "../win/d3dfx.h"
static const char *no_info(void) { return ""; }
const char *(*diag_info)(void) = no_info;
void diag_init(const wchar_t *dir, const char *version) { (void)dir; (void)version; }
void diag_set_window(void *hwnd) { (void)hwnd; }
void diag_thread_init(void) {}
void diag_heartbeat(uint64_t frame) { (void)frame; }
void diag_pause(int on) { (void)on; }
void diag_note(const char *line) { (void)line; }
int d3d_init(void *hwnd) { (void)hwnd; return 0; }
const char *d3d_error(void) { return "Direct3D is Windows only"; }
const char *d3d_info(void) { return ""; }
void d3d_frame(const uint32_t *fb, int W, int H, int pitch, int x, int y, int w, int h, int cw, int ch, int mode, int bilinear, int dither, int backdrop)
{ (void)fb; (void)W; (void)H; (void)pitch; (void)x; (void)y; (void)w; (void)h; (void)cw; (void)ch; (void)mode; (void)bilinear; (void)dither; (void)backdrop; }
void d3d_overlay(const uint32_t *bits, int bw, int bh, int rx, int ry, int rw, int rh) { (void)bits; (void)bw; (void)bh; (void)rx; (void)ry; (void)rw; (void)rh; }
int d3d_present(int vsync) { (void)vsync; return 0; }

#ifdef PS4_NO_GL
#include "../win/glfx.h"
void port_glViewport(GLint a, GLint b, GLsizei c, GLsizei d) { (void)a; (void)b; (void)c; (void)d; }
void port_glClearColor(GLclampf a, GLclampf b, GLclampf c, GLclampf d) { (void)a; (void)b; (void)c; (void)d; }
void port_glClear(GLbitfield a) { (void)a; }
void port_glGenTextures(GLsizei n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = 0; }
void port_glBindTexture(GLenum a, GLuint b) { (void)a; (void)b; }
void port_glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
void port_glTexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void *i) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; }
void port_glTexSubImage2D(GLenum a, GLint b, GLint c, GLint d, GLsizei e, GLsizei f, GLenum g, GLenum h, const void *i) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; }
void port_glEnable(GLenum a) { (void)a; }
void port_glDisable(GLenum a) { (void)a; }
void port_glBlendFunc(GLenum a, GLenum b) { (void)a; (void)b; }
void port_glBegin(GLenum a) { (void)a; }
void port_glEnd(void) {}
void port_glTexCoord2f(GLfloat a, GLfloat b) { (void)a; (void)b; }
void port_glVertex2f(GLfloat a, GLfloat b) { (void)a; (void)b; }
void port_glColor4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { (void)a; (void)b; (void)c; (void)d; }
void port_glMatrixMode(GLenum a) { (void)a; }
void port_glLoadIdentity(void) {}
void port_glOrtho(GLdouble a, GLdouble b, GLdouble c, GLdouble d, GLdouble e, GLdouble f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void port_glPixelStorei(GLenum a, GLint b) { (void)a; (void)b; }
const unsigned char *port_glGetString(GLenum a) { (void)a; return 0; }
GLenum port_glGetError(void) { return 1; }
int glfx_init(glfx_getproc_t g) { (void)g; return 0; }
const char *glfx_error(void) { return "no OpenGL"; }
void glfx_draw(const uint32_t *fb, int W, int H, int pitch, int x, int y, int w, int h, int cw, int ch, int mode, int bilinear, int dither, int backdrop)
{ (void)fb; (void)W; (void)H; (void)pitch; (void)x; (void)y; (void)w; (void)h; (void)cw; (void)ch; (void)mode; (void)bilinear; (void)dither; (void)backdrop; }
#endif

#ifndef PS4_GLES   /* the touch controls' OpenGL ES drawing is only used with PS4_GLES */
void gles_set_offset(int x, int y) { (void)x; (void)y; }
int gles_init(void) { return 0; }
void *gles_proc(const char *n) { return SDL_GL_GetProcAddress(n); }
void gles_overlay_begin(int w, int h) { (void)w; (void)h; }
void gles_overlay_end(void) {}
void gles_tris(const float *xy, int n, int r, int g, int b, int a) { (void)xy; (void)n; (void)r; (void)g; (void)b; (void)a; }
unsigned gles_texture(const uint32_t *p, int w, int h) { (void)p; (void)w; (void)h; return 0; }
void gles_texture_free(unsigned t) { (void)t; }
void gles_image(unsigned t, float x, float y, float w, float h, int a) { (void)t; (void)x; (void)y; (void)w; (void)h; (void)a; }
#endif
