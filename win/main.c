/*
 * Phantasy Star IV — native Windows front end for the static recompilation.
 * Plain Win32: GDI (double-buffered) for video and the overlay menu, waveOut
 * for sound, XInput and WinMM joysticks for gamepads.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32.h"
#include "../src/emu.h"
#include "../src/md.h"
#include "../src/hdscale.h"
#include "../src/rewind.h"

int _fltused = 0x9875;

#define APP_TITLE L"Phantasy Star IV — Звезда Фантазии"

/* ======================================================================
 * Settings
 * ====================================================================== */
enum { A_UP, A_DOWN, A_LEFT, A_RIGHT, A_A, A_B, A_C, A_START, A_MENU, A_TURBO, A_REWIND, A_QSAVE, A_QLOAD, NACT };
static const wchar_t *act_name[NACT] = {
    L"Вверх", L"Вниз", L"Влево", L"Вправо", L"Кнопка A", L"Кнопка B (отмена)", L"Кнопка C (подтвердить)", L"Start (меню игры)",
    L"Меню паузы", L"Ускорение (держать)", L"Перемотка (держать)", L"Быстрое сохранение", L"Быстрая загрузка" };
#define NKEY 2
#define NPAD 4

typedef struct {
    uint32_t magic, version;
    int32_t ws_mode, hd, scale_mode, smooth, battle_speed, turbo_speed, rewind, autosave_min, volume;
    int32_t win_w, win_h, fullscreen, slot;
    uint16_t keys[NACT][NKEY];
    uint16_t pad[NACT][NPAD];
    wchar_t rom[600];
    int32_t pad_mode, vsync, use_gl;       /* added in 0.3 */
} cfg_t;
#define CFG_MAGIC 0x32475043u
#define CFG_VERSION 2
#define CFG_V2_SIZE (offsetof(cfg_t, pad_mode))
static cfg_t C;

/* pad input codes */
#define XI_BTN(b) (0x100 + (b))      /* XInput button bit 0..15 */
#define XI_LT 0x120
#define XI_RT 0x121
#define XI_STICK(d) (0x130 + (d))    /* 0 up 1 down 2 left 3 right */
#define JOY_BTN(b) (0x200 + (b))     /* WinMM button 0..31 */
#define JOY_POV(d) (0x240 + (d))
#define JOY_AXIS(d) (0x250 + (d))
#define NCODES 0x260

static void cfg_defaults(void)
{
    wchar_t rom[600]; wcscpy(rom, C.rom);
    memset(&C, 0, sizeof C);
    wcscpy(C.rom, rom);
    C.magic = CFG_MAGIC; C.version = CFG_VERSION;
    C.ws_mode = 2; C.hd = 0; C.scale_mode = 0; C.smooth = 0; C.battle_speed = 1; C.turbo_speed = 3;
    C.rewind = 1; C.autosave_min = 5; C.volume = 100; C.slot = 1;
    C.pad_mode = 0; C.vsync = 1; C.use_gl = 1;
    static const uint16_t k[NACT][NKEY] = {
        { VK_UP, 'W' }, { VK_DOWN, 'S' }, { VK_LEFT, 'A' }, { VK_RIGHT, 'D' },
        { 'Z', 'J' }, { 'X', 'K' }, { 'C', VK_SPACE }, { VK_RETURN, 0 },
        { VK_ESCAPE, 0 }, { VK_TAB, 0 }, { VK_BACK, 0 }, { VK_F1 + 4, 0 }, { VK_F1 + 7, 0 } };
    static const uint16_t p[NACT][NPAD] = {
        { XI_BTN(0), XI_STICK(0), JOY_POV(0), JOY_AXIS(0) },
        { XI_BTN(1), XI_STICK(1), JOY_POV(1), JOY_AXIS(1) },
        { XI_BTN(2), XI_STICK(2), JOY_POV(2), JOY_AXIS(2) },
        { XI_BTN(3), XI_STICK(3), JOY_POV(3), JOY_AXIS(3) },
        { XI_BTN(14), XI_BTN(15), JOY_BTN(0), JOY_BTN(3) },   /* A: X / Y,  Square / Triangle */
        { XI_BTN(13), JOY_BTN(2), 0, 0 },                      /* B: B,      Circle */
        { XI_BTN(12), JOY_BTN(1), 0, 0 },                      /* C: A,      Cross */
        { XI_BTN(4), JOY_BTN(9), 0, 0 },                       /* Start */
        { XI_BTN(5), JOY_BTN(8), 0, 0 },                       /* menu: Back / Select */
        { XI_RT, JOY_BTN(7), 0, 0 },                           /* turbo: RT / R2 */
        { XI_LT, JOY_BTN(6), 0, 0 },                           /* rewind: LT / L2 */
        { 0 }, { 0 } };
    memcpy(C.keys, k, sizeof k);
    memcpy(C.pad, p, sizeof p);
}

/* ======================================================================
 * Globals, helpers
 * ====================================================================== */
static HWND hwnd;
static wchar_t exe_dir[600];
static int quit, menu_open, fullscreen;
static RECT saved_rect;
static uint8_t keys[256];
static wchar_t status_msg[200];
static uint64_t status_until, frame_no;
static int64_t qpc_freq;
static int ws_ext_for_mode[3] = { 0, 32, 53 };

static uint64_t now_ms(void) { int64_t t; QueryPerformanceCounter(&t); return (uint64_t)(t * 1000 / qpc_freq); }
static void path_in_exe_dir(wchar_t *out, const wchar_t *name) { wcscpy(out, exe_dir); wcscat(out, name); }
static void set_status(const wchar_t *msg) { wcscpy(status_msg, msg); status_until = now_ms() + 2500; }

static uint8_t *read_file(const wchar_t *path, size_t *n)
{
    FILE *f = _wfopen(path, L"rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return 0; }
    uint8_t *b = malloc((size_t)sz);
    *n = fread(b, 1, (size_t)sz, f);
    fclose(f);
    return b;
}

/* write via a temporary file, so a crash never leaves a half-written save */
static int write_file(const wchar_t *path, const void *data, size_t n)
{
    wchar_t tmp[700]; wcscpy(tmp, path); wcscat(tmp, L".tmp");
    FILE *f = _wfopen(tmp, L"wb");
    if (!f) return 0;
    size_t w = fwrite(data, 1, n, f);
    fclose(f);
    if (w != n) return 0;
    return MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING) != 0;
}

static void cfg_save(void)
{
    RECT r; GetWindowRect(hwnd, &r);
    if (!fullscreen) { C.win_w = r.right - r.left; C.win_h = r.bottom - r.top; }
    C.fullscreen = fullscreen;
    wchar_t p[700]; path_in_exe_dir(p, L"ps4recomp.cfg");
    write_file(p, &C, sizeof C);
}

static void cfg_load(void)
{
    cfg_defaults();
    wchar_t p[700]; path_in_exe_dir(p, L"ps4recomp.cfg");
    size_t n = 0; uint8_t *b = read_file(p, &n);
    if (!b) return;
    if ((n == sizeof C || n == CFG_V2_SIZE) && ((cfg_t *)b)->magic == CFG_MAGIC && ((cfg_t *)b)->version == CFG_VERSION)
        memcpy(&C, b, n);                      /* a 0.2 file keeps the new fields at their defaults */
    else if (n >= 20 + 1200 && *(uint32_t *)b == 0x31475043u)   /* version 1 file: keep the ROM path */
        memcpy(C.rom, b + 20, 1200);
    free(b);
}

/* ======================================================================
 * Saves
 * ====================================================================== */
static void saves_dir(void) { wchar_t p[700]; path_in_exe_dir(p, L"saves"); CreateDirectoryW(p, 0); }
static void sram_path(wchar_t *p) { path_in_exe_dir(p, L"saves\\PS4_battery.srm"); }
static void state_path(wchar_t *p, int s)
{
    wchar_t n[64];
    if (s == 0) wcscpy(n, L"saves\\PS4_auto.sav");
    else _snwprintf(n, 63, L"saves\\PS4_state%d.sav", s);
    path_in_exe_dir(p, n);
}
static void sram_write(void) { saves_dir(); wchar_t p[700]; sram_path(p); size_t n; uint8_t *s = emu_sram(&n); write_file(p, s, n); }
static void sram_read(void)
{
    wchar_t p[700]; sram_path(p);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    if (!b) return;
    size_t sz; uint8_t *s = emu_sram(&sz);
    memcpy(s, b, n < sz ? n : sz);
    free(b);
}
static int state_save(int slot, int quiet)
{
    saves_dir();
    wchar_t p[700]; state_path(p, slot);
    size_t n = emu_state_size();
    uint8_t *b = malloc(n);
    emu_state_save(b);
    int ok = write_file(p, b, n);
    free(b);
    if (!quiet) {
        wchar_t m[100];
        if (slot == 0) _snwprintf(m, 99, ok ? L"автосохранение записано" : L"не удалось записать автосохранение");
        else _snwprintf(m, 99, ok ? L"сохранено в слот %d" : L"не удалось сохранить слот %d", slot);
        set_status(m);
    }
    return ok;
}
static int state_load(int slot)
{
    wchar_t p[700]; state_path(p, slot);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    wchar_t m[100];
    int ok = b && emu_state_load(b, n);
    if (slot == 0) wcscpy(m, ok ? L"загружено автосохранение" : L"автосохранения нет");
    else _snwprintf(m, 99, ok ? L"загружен слот %d" : L"слот %d пуст", slot);
    set_status(m);
    free(b);
    if (ok) rw_clear();
    return ok;
}

/* ======================================================================
 * Audio (waveOut; the queue length paces the game)
 * ====================================================================== */
#define NHDR 32
#define HDR_FRAMES 4096
static HWAVEOUT wo;
static WAVEHDR hdr[NHDR];
static int16_t *abuf[NHDR];
static int audio_ok;

static void audio_init(void)
{
    WAVEFORMATEX f; memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = 48000;
    f.wBitsPerSample = 16; f.nBlockAlign = 4; f.nAvgBytesPerSec = 48000 * 4;
    if (waveOutOpen(&wo, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != 0) return;
    for (int i = 0; i < NHDR; i++) { abuf[i] = calloc(HDR_FRAMES, 4); memset(&hdr[i], 0, sizeof hdr[i]); hdr[i].dwFlags = WHDR_DONE; }
    audio_ok = 1;
}
static int audio_queued(void)
{
    int q = 0;
    for (int i = 0; i < NHDR; i++) if (!(hdr[i].dwFlags & WHDR_DONE)) q += (int)(hdr[i].dwBufferLength / 4);
    return q;
}
static void audio_submit(int16_t *s, int frames)
{
    if (!audio_ok || frames <= 0) return;
    if (C.volume < 100) for (int i = 0; i < frames * 2; i++) s[i] = (int16_t)(s[i] * C.volume / 100);
    for (int i = 0; i < NHDR; i++) {
        if (!(hdr[i].dwFlags & WHDR_DONE)) continue;
        if (hdr[i].dwFlags & 2) waveOutUnprepareHeader(wo, &hdr[i], sizeof hdr[i]);
        if (frames > HDR_FRAMES) frames = HDR_FRAMES;
        memcpy(abuf[i], s, (size_t)frames * 4);
        memset(&hdr[i], 0, sizeof hdr[i]);
        hdr[i].lpData = (char *)abuf[i]; hdr[i].dwBufferLength = (DWORD)frames * 4;
        waveOutPrepareHeader(wo, &hdr[i], sizeof hdr[i]);
        waveOutWrite(wo, &hdr[i], sizeof hdr[i]);
        return;
    }
}
static void audio_close(void)
{
    if (!audio_ok) return;
    waveOutReset(wo);
    for (int i = 0; i < NHDR; i++) if (hdr[i].dwFlags & 2) waveOutUnprepareHeader(wo, &hdr[i], sizeof hdr[i]);
    waveOutClose(wo);
}

/* ======================================================================
 * Input: keyboard + XInput + WinMM joysticks
 * ====================================================================== */
typedef DWORD (*xinput_get_t)(DWORD, XINPUT_STATE *);
static xinput_get_t xinput_get;
static int xi_user = -1, joy_id = -1;
static uint64_t xi_retry, joy_retry;
static uint8_t code_down[NCODES], code_prev[NCODES];
static uint8_t act_down[NACT], act_prev[NACT];     /* keyboard + pad (game buttons, turbo, rewind) */
static uint8_t pact_down[NACT], pact_prev[NACT];   /* pad only (menu navigation, pad hotkeys) */
static wchar_t pad_name[64] = L"не подключён";
/* Inputs already held when a device connects are ignored until released
 * (some devices report a pressed button or a zero axis all the time). */
static uint8_t code_stuck[NCODES];
static int stuck_arm;                       /* next poll marks held codes as stuck */
static DWORD joy_rest_x, joy_rest_y;

static void input_init(void)
{
    static const wchar_t *dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (int i = 0; i < 3 && !xinput_get; i++) {
        HMODULE m = LoadLibraryW(dlls[i]);
        if (m) xinput_get = (xinput_get_t)GetProcAddress(m, "XInputGetState");
    }
}

static void poll_pads(void)
{
    memcpy(code_prev, code_down, sizeof code_down);
    memset(code_down, 0, sizeof code_down);
    uint64_t t = now_ms();
    /* XInput: the first connected controller */
    if (xinput_get && C.pad_mode != 2) {
        XINPUT_STATE st;
        if (xi_user < 0 && t >= xi_retry) {
            for (int u = 0; u < 4; u++) { memset(&st, 0, sizeof st); if (xinput_get((DWORD)u, &st) == 0) { xi_user = u; break; } }
            if (xi_user < 0) xi_retry = t + 2000;
            else { _snwprintf(pad_name, 63, L"Xbox-совместимый (XInput %d)", xi_user + 1); set_status(L"геймпад подключён"); stuck_arm = 1; }
        }
        if (xi_user >= 0) {
            memset(&st, 0, sizeof st);
            if (xinput_get((DWORD)xi_user, &st) != 0) { xi_user = -1; xi_retry = t + 500; wcscpy(pad_name, L"не подключён"); set_status(L"геймпад отключён"); }
            else {
                for (int b = 0; b < 16; b++) if (st.Gamepad.wButtons & (1 << b)) code_down[XI_BTN(b)] = 1;
                if (st.Gamepad.bLeftTrigger > 100) code_down[XI_LT] = 1;
                if (st.Gamepad.bRightTrigger > 100) code_down[XI_RT] = 1;
                if (st.Gamepad.sThumbLY > 14000) code_down[XI_STICK(0)] = 1;
                if (st.Gamepad.sThumbLY < -14000) code_down[XI_STICK(1)] = 1;
                if (st.Gamepad.sThumbLX < -14000) code_down[XI_STICK(2)] = 1;
                if (st.Gamepad.sThumbLX > 14000) code_down[XI_STICK(3)] = 1;
            }
        }
    }
    /* WinMM (DirectInput-class pads: DualShock, generic USB). Skipped while an
     * XInput pad is active: Xbox pads show up here too, with other button numbers. */
    if (xi_user < 0 && C.pad_mode == 0) {
        JOYINFOEX ji;
        if (joy_id < 0 && t >= joy_retry) {
            UINT n = joyGetNumDevs(); if (n > 16) n = 16;
            for (UINT i = 0; i < n; i++) { memset(&ji, 0, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL; if (joyGetPosEx(i, &ji) == 0) { joy_id = (int)i; break; } }
            if (joy_id < 0) joy_retry = t + 2000;
            else {
                _snwprintf(pad_name, 63, L"геймпад DirectInput %d", joy_id + 1); set_status(L"геймпад подключён");
                joy_rest_x = ji.dwXpos; joy_rest_y = ji.dwYpos; stuck_arm = 1;
            }
        }
        if (joy_id >= 0) {
            memset(&ji, 0, sizeof ji); ji.dwSize = sizeof ji; ji.dwFlags = JOY_RETURNALL;
            if (joyGetPosEx((UINT)joy_id, &ji) != 0) { joy_id = -1; joy_retry = t + 500; wcscpy(pad_name, L"не подключён"); set_status(L"геймпад отключён"); }
            else {
                for (int b = 0; b < 32; b++) if (ji.dwButtons & (1u << b)) code_down[JOY_BTN(b)] = 1;
                if (ji.dwPOV != 0xFFFF && ji.dwPOV <= 36000) {
                    DWORD p = ji.dwPOV;
                    if (p >= 31500 || p <= 4500) code_down[JOY_POV(0)] = 1;
                    if (p >= 13500 && p <= 22500) code_down[JOY_POV(1)] = 1;
                    if (p >= 22500 && p <= 31500) code_down[JOY_POV(2)] = 1;
                    if (p >= 4500 && p <= 13500) code_down[JOY_POV(3)] = 1;
                }
                /* axes relative to where they rested at connection */
                long dy = (long)ji.dwYpos - (long)joy_rest_y, dx = (long)ji.dwXpos - (long)joy_rest_x;
                if (dy < -16000) code_down[JOY_AXIS(0)] = 1;
                if (dy > 16000) code_down[JOY_AXIS(1)] = 1;
                if (dx < -16000) code_down[JOY_AXIS(2)] = 1;
                if (dx > 16000) code_down[JOY_AXIS(3)] = 1;
            }
        }
    }
    if (stuck_arm) { for (int c = 0; c < NCODES; c++) code_stuck[c] = code_down[c]; stuck_arm = 0; }
    for (int c = 0; c < NCODES; c++) {
        if (!code_down[c]) code_stuck[c] = 0;
        else if (code_stuck[c]) code_down[c] = 0;
    }
    if (C.pad_mode == 2) memset(code_down, 0, sizeof code_down);
    memcpy(act_prev, act_down, sizeof act_down);
    memcpy(pact_prev, pact_down, sizeof pact_down);
    for (int a = 0; a < NACT; a++) {
        int d = 0, pd = 0;
        /* keyboard hotkeys (menu, quick save/load) are handled on WM_KEYDOWN */
        if (a != A_MENU && a != A_QSAVE && a != A_QLOAD)
            for (int i = 0; i < NKEY; i++) if (C.keys[a][i] && keys[C.keys[a][i] & 255]) d = 1;
        for (int i = 0; i < NPAD; i++) if (C.pad[a][i] && C.pad[a][i] < NCODES && code_down[C.pad[a][i]]) pd = 1;
        act_down[a] = (uint8_t)(d | pd);
        pact_down[a] = (uint8_t)pd;
    }
}
static int pressed(int a) { return act_down[a] && !act_prev[a]; }
static int ppressed(int a) { return pact_down[a] && !pact_prev[a]; }

static uint16_t game_pad(void)
{
    uint16_t p = 0;
    if (act_down[A_UP]) p |= PAD_UP;
    if (act_down[A_DOWN]) p |= PAD_DOWN;
    if (act_down[A_LEFT]) p |= PAD_LEFT;
    if (act_down[A_RIGHT]) p |= PAD_RIGHT;
    if (act_down[A_A]) p |= PAD_A;
    if (act_down[A_B]) p |= PAD_B;
    if (act_down[A_C]) p |= PAD_C;
    if (act_down[A_START]) p |= PAD_START;
    return p;
}

static void key_name(int vk, wchar_t *out, int n)
{
    if (!vk) { wcscpy(out, L"—"); return; }
    switch (vk) {
    case VK_UP: wcscpy(out, L"↑"); return;
    case VK_DOWN: wcscpy(out, L"↓"); return;
    case VK_LEFT: wcscpy(out, L"←"); return;
    case VK_RIGHT: wcscpy(out, L"→"); return;
    case VK_RETURN: wcscpy(out, L"Enter"); return;
    case VK_SPACE: wcscpy(out, L"Пробел"); return;
    case VK_ESCAPE: wcscpy(out, L"Esc"); return;
    case VK_TAB: wcscpy(out, L"Tab"); return;
    case VK_BACK: wcscpy(out, L"Backspace"); return;
    }
    if (vk >= VK_F1 && vk <= VK_F1 + 11) { _snwprintf(out, n, L"F%d", vk - VK_F1 + 1); return; }
    UINT sc = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_VSC);
    if (!sc || GetKeyNameTextW((LONG)(sc << 16), out, n) <= 0) _snwprintf(out, n, L"#%d", vk);
}

static void pad_code_name(int c, wchar_t *out, int n)
{
    static const wchar_t *xb[16] = { L"Xbox ↑", L"Xbox ↓", L"Xbox ←", L"Xbox →", L"Xbox Start", L"Xbox Back", L"Xbox L3", L"Xbox R3",
                                     L"Xbox LB", L"Xbox RB", L"?", L"?", L"Xbox A", L"Xbox B", L"Xbox X", L"Xbox Y" };
    static const wchar_t *dir[4] = { L"↑", L"↓", L"←", L"→" };
    if (!c) wcscpy(out, L"—");
    else if (c >= 0x100 && c < 0x110) wcscpy(out, xb[c - 0x100]);
    else if (c == XI_LT) wcscpy(out, L"Xbox LT");
    else if (c == XI_RT) wcscpy(out, L"Xbox RT");
    else if (c >= 0x130 && c < 0x134) _snwprintf(out, n, L"Стик %ls", dir[c - 0x130]);
    else if (c >= 0x200 && c < 0x220) _snwprintf(out, n, L"Кнопка %d", c - 0x200 + 1);
    else if (c >= 0x240 && c < 0x244) _snwprintf(out, n, L"Крестовина %ls", dir[c - 0x240]);
    else if (c >= 0x250 && c < 0x254) _snwprintf(out, n, L"Ось %ls", dir[c - 0x250]);
    else _snwprintf(out, n, L"#%X", c);
}

/* ======================================================================
 * Video: back buffer, HD threads, presentation
 * ====================================================================== */
static HDC back_dc;
static void *back_bits;
static HGDIOBJ back_bmp, back_old;
static int back_w, back_h;
static uint32_t *hd_buf;
static BITMAPINFO bmi_fb, bmi_hd;
static HGDIOBJ font_big, font_small;
static int font_h;

#define MAXTHREADS 8
static int hd_threads;
static HANDLE hd_go[MAXTHREADS], hd_done[MAXTHREADS];
static struct { const uint32_t *src; int w, h; } hd_job;

static DWORD hd_worker(void *arg)
{
    int i = (int)(intptr_t)arg;
    for (;;) {
        WaitForSingleObject(hd_go[i], INFINITE);
        int y0 = hd_job.h * i / hd_threads, y1 = hd_job.h * (i + 1) / hd_threads;
        hd_rows(hd_job.src, hd_job.w, hd_job.h, MD_W_MAX, hd_buf, MD_W_MAX * 4, y0, y1);
        SetEvent(hd_done[i]);
    }
    return 0;
}

static void video_init(void)
{
    memset(&bmi_fb, 0, sizeof bmi_fb);
    bmi_fb.bmiHeader.biSize = sizeof bmi_fb.bmiHeader; bmi_fb.bmiHeader.biWidth = MD_W_MAX; bmi_fb.bmiHeader.biHeight = -224;
    bmi_fb.bmiHeader.biPlanes = 1; bmi_fb.bmiHeader.biBitCount = 32; bmi_fb.bmiHeader.biCompression = BI_RGB;
    bmi_hd = bmi_fb; bmi_hd.bmiHeader.biWidth = MD_W_MAX * 4; bmi_hd.bmiHeader.biHeight = -224 * 4;
    hd_buf = calloc((size_t)MD_W_MAX * 4 * 224 * 4, 4);
    int n = (int)GetActiveProcessorCount(0xFFFF);
    hd_threads = n < 1 ? 1 : n > MAXTHREADS ? MAXTHREADS : n;
    for (int i = 0; i < hd_threads; i++) {
        hd_go[i] = CreateEventW(0, 0, 0, 0); hd_done[i] = CreateEventW(0, 0, 0, 0);
        CreateThread(0, 0, hd_worker, (void *)(intptr_t)i, 0, 0);
    }
}

static void ensure_back(int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (back_dc && w == back_w && h == back_h) return;
    HDC wdc = GetDC(hwnd);
    if (!back_dc) back_dc = CreateCompatibleDC(wdc);
    if (back_bmp) { SelectObject(back_dc, back_old); DeleteObject(back_bmp); }
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = 0;
    back_bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    back_old = SelectObject(back_dc, back_bmp);
    back_bits = bits;
    ReleaseDC(hwnd, wdc);
    back_w = w; back_h = h;
    if (font_big) DeleteObject(font_big);
    if (font_small) DeleteObject(font_small);
    font_h = h / 26; if (font_h < 14) font_h = 14;
    font_big = CreateFontW(font_h, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    font_small = CreateFontW(font_h * 4 / 5, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
}

/* where the picture goes; the scale is based on the configured wide width so
 * it does not jump when a frame is drawn 4:3 (battles) */
static void dest_rect(int cw, int ch, int W, int *x, int *y, int *w, int *h)
{
    int Wt = 320 + 2 * ws_ext_for_mode[C.ws_mode];
    if (W > Wt) Wt = W;
    double sx, sy;
    if (C.scale_mode == 1) {
        int s = cw / Wt < ch / 224 ? cw / Wt : ch / 224;
        if (s < 1) s = 1;
        sx = sy = s;
    } else if (C.scale_mode == 2) {
        sx = (double)cw / Wt; sy = (double)ch / 224;
    } else {
        /* the console's pixels are not square: 320 px cover a 4:3 picture */
        double par = (4.0 / 3.0 * 224.0) / 320.0;
        sy = (double)ch / 224; sx = sy * par;
        if (sx * Wt > cw) { sx = (double)cw / Wt; sy = sx / par; }
    }
    *w = (int)(W * sx + 0.5); *h = (int)(224 * sy + 0.5);
    *x = (cw - *w) / 2; *y = (ch - *h) / 2;
}

static void draw_text(int x, int y, const wchar_t *s, DWORD color, HGDIOBJ font)
{
    SelectObject(back_dc, font);
    SetBkMode(back_dc, TRANSPARENT);
    SetTextColor(back_dc, RGBC(0, 0, 0));
    TextOutW(back_dc, x + 2, y + 2, s, (int)wcslen(s));
    SetTextColor(back_dc, color);
    TextOutW(back_dc, x, y, s, (int)wcslen(s));
}
static int text_w(const wchar_t *s, HGDIOBJ font)
{
    SIZE sz; SelectObject(back_dc, font);
    GetTextExtentPoint32W(back_dc, s, (int)wcslen(s), &sz);
    return sz.cx;
}
static void fill(int x, int y, int w, int h, DWORD color)
{
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(color);
    FillRect(back_dc, &r, b);
    DeleteObject(b);
}

static void draw_menu(void);
static void draw_status(void);
static int menu_rect[4], status_rect[4];   /* x, y, w, h of overlay boxes drawn this frame */

/* ---------- OpenGL path (GPU scaling, vsync) ---------- */
static HDC gl_dc;
static HGLRC gl_rc;
static int gl_ok, gl_npot;
static GLuint tex_frame, tex_hd, tex_ovl;
static int ovl_tw, ovl_th;
typedef BOOL (*swap_interval_t)(int);
static swap_interval_t wgl_swap_interval;
static int vsync_applied = -1;

static int pot(int v) { int p = 1; while (p < v) p <<= 1; return p; }

static void gl_new_tex(GLuint *t, int w, int h)
{
    if (!*t) glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0);
}

static int gl_init(void)
{
    gl_dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd; memset(&pfd, 0, sizeof pfd);
    pfd.nSize = sizeof pfd; pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = 0; pfd.cColorBits = 32;
    int pf = ChoosePixelFormat(gl_dc, &pfd);
    if (!pf || !SetPixelFormat(gl_dc, pf, &pfd)) return 0;
    gl_rc = wglCreateContext(gl_dc);
    if (!gl_rc || !wglMakeCurrent(gl_dc, gl_rc)) return 0;
    const char *ver = (const char *)glGetString(GL_VERSION);
    if (!ver) return 0;
    gl_npot = ver[0] >= '2' && ver[0] <= '9';
    wgl_swap_interval = (swap_interval_t)wglGetProcAddress("wglSwapIntervalEXT");
    gl_new_tex(&tex_frame, 512, 256);
    gl_new_tex(&tex_hd, 2048, 1024);
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    return glGetError() == 0;
}

static void gl_quad(GLuint t, int tw, int th, int sx, int sy, int sw, int sh, int x, int y, int w, int h, int smooth)
{
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    float u0 = (float)sx / tw, v0 = (float)sy / th, u1 = (float)(sx + sw) / tw, v1 = (float)(sy + sh) / th;
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f((float)x, (float)y);
    glTexCoord2f(u1, v0); glVertex2f((float)(x + w), (float)y);
    glTexCoord2f(u1, v1); glVertex2f((float)(x + w), (float)(y + h));
    glTexCoord2f(u0, v1); glVertex2f((float)x, (float)(y + h));
    glEnd();
}

static void run_hd(const uint32_t *fb, int W, int H)
{
    hd_prepare(fb, W, H, MD_W_MAX);
    hd_job.src = fb; hd_job.w = W; hd_job.h = H;
    for (int i = 0; i < hd_threads; i++) SetEvent(hd_go[i]);
    WaitForMultipleObjects((DWORD)hd_threads, hd_done, 1, INFINITE);
}

static void present(void)
{
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left, ch = cr.bottom - cr.top;
    if (cw < 1 || ch < 1) return;
    ensure_back(cw, ch);
    int W, H; const uint32_t *fb = emu_framebuffer(&W, &H);
    int x, y, w, h;
    dest_rect(cw, ch, W, &x, &y, &w, &h);
    if (C.hd) run_hd(fb, W, H);

    /* overlays are drawn with GDI into the back buffer, then copied */
    menu_rect[2] = status_rect[2] = 0;
    if (menu_open || status_msg[0]) {
        if (menu_open) draw_menu();
        draw_status();
    }

    if (gl_ok) {
        if (vsync_applied != C.vsync && wgl_swap_interval) { wgl_swap_interval(C.vsync ? 1 : 0); vsync_applied = C.vsync; }
        glViewport(0, 0, cw, ch);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, cw, ch, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (C.hd) {
            glBindTexture(GL_TEXTURE_2D, tex_hd);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, MD_W_MAX * 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W * 4, H * 4, GL_BGRA_EXT, GL_UNSIGNED_BYTE, hd_buf);
            gl_quad(tex_hd, 2048, 1024, 0, 0, W * 4, H * 4, x, y, w, h, C.smooth);
        } else {
            glBindTexture(GL_TEXTURE_2D, tex_frame);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, MD_W_MAX);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_BGRA_EXT, GL_UNSIGNED_BYTE, fb);
            gl_quad(tex_frame, 512, 256, 0, 0, W, H, x, y, w, h, C.smooth);
        }
        int *rects[2] = { menu_rect, status_rect };
        for (int k = 0; k < 2; k++) {
            int *r = rects[k];
            if (r[2] <= 0 || r[3] <= 0) continue;
            GdiFlush();
            int tw = gl_npot ? cw : pot(cw), th = gl_npot ? ch : pot(ch);
            if (!tex_ovl || tw != ovl_tw || th != ovl_th) { gl_new_tex(&tex_ovl, tw, th); ovl_tw = tw; ovl_th = th; }
            glBindTexture(GL_TEXTURE_2D, tex_ovl);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, back_w);
            glTexSubImage2D(GL_TEXTURE_2D, 0, r[0], r[1], r[2], r[3], GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                            (const uint32_t *)back_bits + (size_t)r[1] * back_w + r[0]);
            gl_quad(tex_ovl, ovl_tw, ovl_th, r[0], r[1], r[2], r[3], r[0], r[1], r[2], r[3], 0);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        SwapBuffers(gl_dc);
        return;
    }

    /* GDI fallback: picture straight to the window, overlays copied on top */
    HDC dc = GetDC(hwnd);
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT bars[4] = { { 0, 0, cw, y }, { 0, y + h, cw, ch }, { 0, y, x, y + h }, { x + w, y, cw, y + h } };
    for (int i = 0; i < 4; i++) if (bars[i].right > bars[i].left && bars[i].bottom > bars[i].top) FillRect(dc, &bars[i], black);
    SetStretchBltMode(dc, C.smooth ? HALFTONE : COLORONCOLOR);
    if (C.smooth) SetBrushOrgEx(dc, 0, 0, 0);
    if (C.hd) StretchDIBits(dc, x, y, w, h, 0, 0, W * 4, H * 4, hd_buf, &bmi_hd, DIB_RGB_COLORS, SRCCOPY);
    else StretchDIBits(dc, x, y, w, h, 0, 0, W, H, fb, &bmi_fb, DIB_RGB_COLORS, SRCCOPY);
    int *rects[2] = { menu_rect, status_rect };
    for (int k = 0; k < 2; k++) if (rects[k][2] > 0) BitBlt(dc, rects[k][0], rects[k][1], rects[k][2], rects[k][3], back_dc, rects[k][0], rects[k][1], SRCCOPY);
    ReleaseDC(hwnd, dc);
}

static void draw_status(void)
{
    if (!status_msg[0]) return;
    if (now_ms() > status_until) { status_msg[0] = 0; return; }
    int tw = text_w(status_msg, font_small);
    int bx = font_h / 2, bh = font_h * 3 / 2, by = back_h - bh - font_h / 2, bw = tw + font_h;
    fill(bx, by, bw, bh, RGBC(16, 28, 74));
    draw_text(bx + font_h / 2, by + (bh - font_h) / 2, status_msg, RGBC(255, 230, 120), font_small);
    status_rect[0] = bx; status_rect[1] = by; status_rect[2] = bw; status_rect[3] = bh;
}

static void toggle_fullscreen(void)
{
    if (!fullscreen) {
        GetWindowRect(hwnd, &saved_rect);
        MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        fullscreen = 1;
    } else {
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd, HWND_TOP, saved_rect.left, saved_rect.top, saved_rect.right - saved_rect.left,
                     saved_rect.bottom - saved_rect.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        fullscreen = 0;
    }
}

/* window size that fits the chosen picture shape at 3x */
static void fit_window(void)
{
    if (fullscreen) return;
    int Wt = 320 + 2 * ws_ext_for_mode[C.ws_mode];
    RECT r = { 0, 0, (LONG)(Wt * 3 * (4.0 / 3.0 * 224.0 / 320.0) + 0.5), 224 * 3 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, 0);
    RECT cur; GetWindowRect(hwnd, &cur);
    SetWindowPos(hwnd, HWND_TOP, cur.left, cur.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
}

/* ======================================================================
 * Overlay menu
 * ====================================================================== */
enum { M_MAIN, M_VIDEO, M_SPEED, M_KEYS, M_PAD, M_HELP };
static int m_page, m_sel, m_wait_bind = -1;   /* action index waiting for a key / pad input */
static int m_confirm_quit;

typedef struct { wchar_t label[80]; wchar_t value[80]; } item_t;
static item_t items[24];
static int nitems;
static wchar_t m_title[80];

static void add(const wchar_t *l, const wchar_t *v) { wcscpy(items[nitems].label, l); wcscpy(items[nitems].value, v ? v : L""); nitems++; }

static const wchar_t *onoff(int v) { return v ? L"вкл" : L"выкл"; }

static void build_menu(void)
{
    nitems = 0;
    wchar_t v[80];
    switch (m_page) {
    case M_MAIN:
        wcscpy(m_title, L"Пауза");
        add(L"Продолжить", 0);
        _snwprintf(v, 79, L"%d", C.slot); add(L"Слот сохранения", v);
        add(L"Сохранить в слот", 0);
        add(L"Загрузить из слота", 0);
        add(L"Загрузить автосохранение", 0);
        add(L"Экран и графика", L"▸");
        add(L"Скорость, перемотка, автосохранение", L"▸");
        add(L"Управление: клавиатура", L"▸");
        add(L"Управление: геймпад", L"▸");
        _snwprintf(v, 79, L"%d%%", C.volume); add(L"Громкость", v);
        add(L"Справка", L"▸");
        add(L"Сброс консоли", 0);
        add(m_confirm_quit ? L"Выйти? Нажмите ещё раз" : L"Выход", 0);
        break;
    case M_VIDEO: {
        static const wchar_t *wsn[3] = { L"4:3 (оригинал)", L"16:10", L"16:9" };
        static const wchar_t *scn[3] = { L"по размеру окна", L"целочисленный", L"растянуть" };
        wcscpy(m_title, L"Экран и графика");
        add(L"Формат кадра", wsn[C.ws_mode]);
        add(L"HD-графика (сглаживание ×4)", onoff(C.hd));
        add(L"Масштаб", scn[C.scale_mode]);
        add(L"Фильтр при масштабировании", onoff(C.smooth));
        add(L"Полный экран", onoff(fullscreen));
        add(L"Вертикальная синхронизация", onoff(C.vsync));
        add(L"Вывод изображения", C.use_gl ? (gl_ok ? L"OpenGL" : L"OpenGL (недоступен, GDI)") : L"GDI (после перезапуска)");
        add(L"Назад", 0);
        break; }
    case M_SPEED: {
        static const wchar_t *bs[4] = { L"", L"выкл", L"×2", L"×3" };
        wcscpy(m_title, L"Скорость и сохранения");
        add(L"Ускорение боёв", bs[C.battle_speed]);
        _snwprintf(v, 79, L"×%d", C.turbo_speed); add(L"Ускорение по кнопке", v);
        add(L"Перемотка назад", onoff(C.rewind));
        if (C.autosave_min) _snwprintf(v, 79, L"каждые %d мин", C.autosave_min); else wcscpy(v, L"выкл");
        add(L"Автосохранение", v);
        add(L"Назад", 0);
        break; }
    case M_KEYS:
        wcscpy(m_title, L"Клавиатура (Enter — назначить, Del — очистить)");
        for (int a = 0; a < NACT; a++) {
            wchar_t k1[40], k2[40];
            key_name(C.keys[a][0], k1, 39); key_name(C.keys[a][1], k2, 39);
            if (m_wait_bind == a) wcscpy(v, L"нажмите клавишу…");
            else if (C.keys[a][1]) _snwprintf(v, 79, L"%ls, %ls", k1, k2);
            else wcscpy(v, k1);
            add(act_name[a], v);
        }
        add(L"Сбросить по умолчанию", 0);
        add(L"Назад", 0);
        break;
    case M_PAD:
        _snwprintf(m_title, 79, L"Геймпад: %ls", pad_name);
        for (int a = 0; a < NACT; a++) {
            v[0] = 0;
            if (m_wait_bind == a) wcscpy(v, L"нажмите кнопку…");
            else for (int i = 0; i < NPAD; i++) if (C.pad[a][i]) {
                wchar_t n[40]; pad_code_name(C.pad[a][i], n, 39);
                if (v[0]) wcscat(v, L", ");
                if (wcslen(v) + wcslen(n) < 75) wcscat(v, n);
            }
            if (!v[0]) wcscpy(v, L"—");
            add(act_name[a], v);
        }
        add(L"Сбросить по умолчанию", 0);
        add(L"Назад", 0);
        add(L"Какие геймпады слушать", C.pad_mode == 0 ? L"все" : C.pad_mode == 1 ? L"только Xbox (XInput)" : L"никакие");
        break;
    case M_HELP:
        wcscpy(m_title, L"Справка");
        add(L"Esc / Back на геймпаде", L"меню паузы");
        add(L"Tab / правый курок (держать)", L"ускорение");
        add(L"Backspace / левый курок (держать)", L"перемотка назад");
        add(L"F5 / F8", L"быстрое сохранение / загрузка");
        add(L"F11 или Alt+Enter", L"полный экран");
        add(L"C, Пробел / Xbox A", L"подтвердить, говорить");
        add(L"X / Xbox B", L"отмена");
        add(L"Enter / Start", L"меню игры");
        add(L"Обычные сохранения игры", L"в церкви, файл saves\\PS4_battery.srm");
        add(L"Назад", 0);
        break;
    }
}

static int item_y[32], item_h, item_first, item_count, item_x0, item_x1;
static void draw_menu(void)
{
    build_menu();
    int lh = font_h * 13 / 10;
    int pw = back_w * 3 / 4, ph = lh * (nitems + 2) + font_h;
    if (ph > back_h - 20) ph = back_h - 20;
    int px = (back_w - pw) / 2, py = (back_h - ph) / 2;
    menu_rect[0] = px - 3; menu_rect[1] = py - 3; menu_rect[2] = pw + 6; menu_rect[3] = ph + 6;
    item_h = lh; item_x0 = px; item_x1 = px + pw; item_count = 0;
    fill(px - 3, py - 3, pw + 6, ph + 6, RGBC(170, 180, 220));
    fill(px, py, pw, ph, RGBC(16, 28, 74));
    draw_text(px + font_h, py + font_h / 2, m_title, RGBC(255, 255, 255), font_big);
    int first = 0, rows = (ph - lh * 2) / lh;
    if (m_sel >= rows) first = m_sel - rows + 1;
    item_first = first;
    for (int i = first; i < nitems && i - first < rows; i++) {
        int yy = py + lh * 2 + (i - first) * lh;
        item_y[item_count++] = yy;
        if (i == m_sel) fill(px + font_h / 2, yy - 2, pw - font_h, lh, RGBC(60, 90, 170));
        draw_text(px + font_h, yy, items[i].label, i == m_sel ? RGBC(255, 255, 160) : RGBC(225, 230, 255), font_small);
        if (items[i].value[0]) {
            int tw = text_w(items[i].value, font_small);
            draw_text(px + pw - font_h - tw, yy, items[i].value, RGBC(160, 220, 255), font_small);
        }
    }
}

static void menu_set_open(int o)
{
    menu_open = o; m_page = M_MAIN; m_sel = 0; m_wait_bind = -1; m_confirm_quit = 0;
    if (!o) cfg_save();
}

static void apply_video(void) { emu_set_widescreen(ws_ext_for_mode[C.ws_mode]); }

/* dir: 0 activate, -1/+1 left/right */
static void menu_act(int dir)
{
    build_menu();
    int i = m_sel;
    if (m_page == M_MAIN) {
        if (i != 12) m_confirm_quit = 0;
        switch (i) {
        case 0: if (!dir) menu_set_open(0); break;
        case 1: C.slot += dir ? dir : 1; if (C.slot < 1) C.slot = 9; if (C.slot > 9) C.slot = 1; break;
        case 2: if (!dir) { state_save(C.slot, 0); menu_set_open(0); } break;
        case 3: if (!dir) { state_load(C.slot); menu_set_open(0); } break;
        case 4: if (!dir) { state_load(0); menu_set_open(0); } break;
        case 5: if (!dir) { m_page = M_VIDEO; m_sel = 0; } break;
        case 6: if (!dir) { m_page = M_SPEED; m_sel = 0; } break;
        case 7: if (!dir) { m_page = M_KEYS; m_sel = 0; } break;
        case 8: if (!dir) { m_page = M_PAD; m_sel = 0; } break;
        case 9: C.volume += (dir ? dir : 1) * 10; if (C.volume > 100) C.volume = dir ? 100 : 0; if (C.volume < 0) C.volume = 0; break;
        case 10: if (!dir) { m_page = M_HELP; m_sel = 0; } break;
        case 11: if (!dir) { emu_reset(); rw_clear(); set_status(L"сброс"); menu_set_open(0); } break;
        case 12: if (!dir) { if (m_confirm_quit) quit = 1; else m_confirm_quit = 1; } break;
        }
    } else if (m_page == M_VIDEO) {
        int d = dir ? dir : 1;
        switch (i) {
        case 0: C.ws_mode = (C.ws_mode + 3 + d) % 3; apply_video(); fit_window(); break;
        case 1: C.hd = !C.hd; break;
        case 2: C.scale_mode = (C.scale_mode + 3 + d) % 3; break;
        case 3: C.smooth = !C.smooth; break;
        case 4: toggle_fullscreen(); break;
        case 5: C.vsync = !C.vsync; break;
        case 6: C.use_gl = !C.use_gl; set_status(L"вступит в силу после перезапуска"); break;
        case 7: if (!dir) { m_page = M_MAIN; m_sel = 5; } break;
        }
    } else if (m_page == M_SPEED) {
        int d = dir ? dir : 1;
        static const int ts[4] = { 2, 3, 4, 8 };
        switch (i) {
        case 0: C.battle_speed += d; if (C.battle_speed > 3) C.battle_speed = 1; if (C.battle_speed < 1) C.battle_speed = 3; break;
        case 1: { int k = 0; for (int j = 0; j < 4; j++) if (ts[j] == C.turbo_speed) k = j; k = (k + 4 + d) % 4; C.turbo_speed = ts[k]; break; }
        case 2: C.rewind = !C.rewind; rw_clear(); break;
        case 3: C.autosave_min = C.autosave_min == 0 ? (d > 0 ? 5 : 15) : C.autosave_min + 5 * d; if (C.autosave_min > 15 || C.autosave_min < 0) C.autosave_min = 0; break;
        case 4: if (!dir) { m_page = M_MAIN; m_sel = 6; } break;
        }
    } else if (m_page == M_PAD && i == NACT + 2) {
        C.pad_mode = (C.pad_mode + 3 + (dir ? dir : 1)) % 3;
        xi_user = joy_id = -1; xi_retry = joy_retry = 0;
    } else if (m_page == M_KEYS || m_page == M_PAD) {
        if (dir) return;
        if (i < NACT) { m_wait_bind = i; memset(keys, 0, sizeof keys); memcpy(code_prev, code_down, sizeof code_down); }
        else if (i == NACT) {
            cfg_t save = C; cfg_defaults();
            if (m_page == M_KEYS) { uint16_t p[NACT][NPAD]; memcpy(p, save.pad, sizeof p); cfg_t d = C; C = save; memcpy(C.keys, d.keys, sizeof C.keys); }
            else { cfg_t d = C; C = save; memcpy(C.pad, d.pad, sizeof C.pad); }
            set_status(L"назначения сброшены");
        } else { m_sel = m_page == M_KEYS ? 7 : 8; m_page = M_MAIN; }
    } else if (m_page == M_HELP) {
        if (!dir) { m_page = M_MAIN; m_sel = 10; }
    }
}

static void menu_back(void)
{
    if (m_wait_bind >= 0) { m_wait_bind = -1; return; }
    if (m_page == M_MAIN) menu_set_open(0);
    else { int from = m_page; m_page = M_MAIN; m_sel = from == M_VIDEO ? 5 : from == M_SPEED ? 6 : from == M_KEYS ? 7 : from == M_PAD ? 8 : 10; }
}

/* keyboard in the menu */
static void menu_key(int vk)
{
    if (m_wait_bind >= 0 && m_page == M_KEYS) {
        if (vk == VK_ESCAPE) { m_wait_bind = -1; return; }
        uint16_t *k = C.keys[m_wait_bind];
        if (k[0] != vk) { k[1] = k[0]; k[0] = (uint16_t)vk; }
        m_wait_bind = -1;
        return;
    }
    if (m_wait_bind >= 0) { if (vk == VK_ESCAPE) m_wait_bind = -1; return; }
    build_menu();
    switch (vk) {
    case VK_UP: m_sel = (m_sel + nitems - 1) % nitems; break;
    case VK_DOWN: m_sel = (m_sel + 1) % nitems; break;
    case VK_LEFT: menu_act(-1); break;
    case VK_RIGHT: menu_act(1); break;
    case VK_RETURN: case VK_SPACE: menu_act(0); break;
    case VK_ESCAPE: case VK_BACK: menu_back(); break;
    case 0x2E: /* Delete: clear a binding */
        if (m_page == M_KEYS && m_sel < NACT) memset(C.keys[m_sel], 0, sizeof C.keys[m_sel]);
        if (m_page == M_PAD && m_sel < NACT) memset(C.pad[m_sel], 0, sizeof C.pad[m_sel]);
        break;
    }
}

/* gamepad in the menu (called once per frame while the menu is open) */
static void menu_pad(void)
{
    if (m_wait_bind >= 0 && m_page == M_PAD) {
        for (int c = 1; c < NCODES; c++) if (code_down[c] && !code_prev[c]) {
            uint16_t *p = C.pad[m_wait_bind];
            int dup = 0;
            for (int i = 0; i < NPAD; i++) if (p[i] == c) dup = 1;
            if (!dup) { for (int i = NPAD - 1; i > 0; i--) p[i] = p[i - 1]; p[0] = (uint16_t)c; }
            m_wait_bind = -1;
            /* swallow this press so it does not also activate the menu */
            memcpy(pact_prev, pact_down, sizeof pact_down);
            return;
        }
        return;
    }
    if (m_wait_bind >= 0) return;
    build_menu();
    if (ppressed(A_UP)) m_sel = (m_sel + nitems - 1) % nitems;
    if (ppressed(A_DOWN)) m_sel = (m_sel + 1) % nitems;
    if (ppressed(A_LEFT)) menu_act(-1);
    if (ppressed(A_RIGHT)) menu_act(1);
    if (ppressed(A_C) || ppressed(A_START)) menu_act(0);
    if (ppressed(A_B)) menu_back();
    if (ppressed(A_MENU)) menu_set_open(0);
}

/* ======================================================================
 * Window procedure
 * ====================================================================== */
static void on_key(int vk, int alt)
{
    if (vk == VK_RETURN && alt) { toggle_fullscreen(); return; }
    if (vk == VK_F1 + 10) { toggle_fullscreen(); return; }
    if (menu_open) { menu_key(vk); return; }
    if (vk == VK_F1) { menu_set_open(1); m_page = M_HELP; return; }
    for (int i = 0; i < NKEY; i++) {
        if (C.keys[A_MENU][i] == vk) { menu_set_open(1); return; }
        if (C.keys[A_QSAVE][i] == vk) { state_save(C.slot, 0); return; }
        if (C.keys[A_QLOAD][i] == vk) { state_load(C.slot); return; }
    }
    if (vk == VK_F1 + 5) { if (--C.slot < 1) C.slot = 9; wchar_t m[50]; _snwprintf(m, 49, L"слот %d", C.slot); set_status(m); }
    if (vk == VK_F1 + 6) { if (++C.slot > 9) C.slot = 1; wchar_t m[50]; _snwprintf(m, 49, L"слот %d", C.slot); set_status(m); }
    if (vk == VK_PAUSE) menu_set_open(1);
}

static int menu_hit(int mx, int my)
{
    if (!menu_open || mx < item_x0 || mx >= item_x1) return -1;
    for (int k = 0; k < item_count; k++)
        if (my >= item_y[k] - 2 && my < item_y[k] - 2 + item_h) return item_first + k;
    return -1;
}

static LRESULT wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int mx = (int)(short)(l & 0xFFFF), my = (int)(short)((l >> 16) & 0xFFFF);
    switch (m) {
    case WM_SETCURSOR:
        if ((l & 0xFFFF) == 1) {                /* HTCLIENT */
            SetCursor(fullscreen && !menu_open ? 0 : LoadCursorW(0, IDC_ARROW));
            return 1;
        }
        break;
    case WM_MOUSEMOVE: {
        static int lx = -1, ly = -1;
        if (mx == lx && my == ly) return 0;     /* Windows repeats this message without movement */
        lx = mx; ly = my;
        int i = menu_hit(mx, my);
        if (i >= 0 && m_wait_bind < 0) m_sel = i;
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (menu_open && m_wait_bind < 0) { int i = menu_hit(mx, my); if (i >= 0) { m_sel = i; menu_act(0); } }
        return 0;
    case WM_RBUTTONDOWN:
        if (menu_open) menu_back(); else menu_set_open(1);
        return 0;
    case WM_MOUSEWHEEL:
        if (menu_open && m_wait_bind < 0) {
            build_menu();
            int d = (short)((w >> 16) & 0xFFFF);
            m_sel = (m_sel + nitems + (d > 0 ? -1 : 1)) % nitems;
        }
        return 0;
    case WM_CLOSE: quit = 1; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); if (h == hwnd) present(); return 0; }
    case WM_ACTIVATE:
        if ((w & 0xFFFF) == 0) memset(keys, 0, sizeof keys);
        return DefWindowProcW(h, m, w, l);     /* the default handler gives the window keyboard focus */
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (!(l & (1 << 30))) on_key((int)w, (l >> 29) & 1);
        keys[w & 255] = 1;
        if (m == WM_SYSKEYDOWN && w != VK_RETURN && w != VK_F1 + 9) return DefWindowProcW(h, m, w, l);
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
        keys[w & 255] = 0;
        return m == WM_SYSKEYUP ? DefWindowProcW(h, m, w, l) : 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ======================================================================
 * ROM selection
 * ====================================================================== */
static int pick_rom(void)
{
    OPENFILENAMEW o; memset(&o, 0, sizeof o);
    wchar_t file[600] = { 0 };
    o.lStructSize = sizeof o; o.hwndOwner = hwnd;
    o.lpstrFilter = L"ROM Mega Drive (*.gen;*.bin;*.md;*.smd)\0*.gen;*.bin;*.md;*.smd\0Все файлы\0*.*\0";
    o.lpstrFile = file; o.nMaxFile = 599;
    o.lpstrTitle = L"Выберите ROM Phantasy Star IV (рус. перевод, fix v7)";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&o)) return 0;
    wcscpy(C.rom, file);
    return 1;
}

static int load_rom_file(const wchar_t *path, int quiet)
{
    size_t n = 0;
    uint8_t *b = read_file(path, &n);
    if (!b) { if (!quiet) MessageBoxW(hwnd, L"Не удалось прочитать файл ROM.", L"Ошибка", MB_OK | MB_ICONERROR); return 0; }
    int kind = emu_load_rom(b, n);
    free(b);
    if (kind == EMU_ROM_BAD) {
        if (!quiet) MessageBoxW(hwnd, L"Это не похоже на ROM Mega Drive.", L"Ошибка", MB_OK | MB_ICONERROR);
        return 0;
    }
    if (kind == EMU_ROM_INTERPRETED)
        MessageBoxW(hwnd,
            L"Это не та версия ROM, из которой собрана рекомпиляция\n"
            L"(нужен Phantasy Star IV (J) [T+Rus1.1 bybyc9lc9l]_fix_v7, SHA-1 d2820fd0…).\n\n"
            L"Игра запустится на встроенном интерпретаторе 68000: всё будет работать, "
            L"но без рекомпилированного кода и без широкого экрана.", L"Другая версия ROM", MB_OK | MB_ICONINFORMATION);
    return 1;
}

/* ======================================================================
 * Main loop
 * ====================================================================== */
static int run(void)
{
    QueryPerformanceFrequency(&qpc_freq);
    SetProcessDPIAware();
    GetModuleFileNameW(0, exe_dir, 599);
    wchar_t *sl = wcsrchr(exe_dir, L'\\');
    if (sl) sl[1] = 0; else exe_dir[0] = 0;

    HINSTANCE inst = GetModuleHandleW(0);
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; wc.lpfnWndProc = wndproc;
    wc.hInstance = inst; wc.hCursor = LoadCursorW(0, IDC_ARROW); wc.hIcon = LoadIconW(inst, (LPCWSTR)(uintptr_t)1);
    if (!wc.hIcon) wc.hIcon = LoadIconW(0, IDI_APPLICATION);
    wc.lpszClassName = L"PS4Recomp";
    RegisterClassExW(&wc);

    cfg_load();
    int first_run = C.win_w == 0;
    int Wt = 320 + 2 * ws_ext_for_mode[C.ws_mode];
    RECT r = { 0, 0, (LONG)(Wt * 3 * (4.0 / 3.0 * 224.0 / 320.0) + 0.5), 224 * 3 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    if (C.win_w > 200 && C.win_h > 150) { ww = C.win_w; wh = C.win_h; }
    hwnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, ww, wh, 0, 0, inst, 0);
    if (!hwnd) return 1;
    video_init();
    gl_ok = C.use_gl && gl_init();

    int argc = 0; LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int loaded = 0;
    if (argc > 1) { wcscpy(C.rom, argv[1]); loaded = load_rom_file(C.rom, 0); }
    if (!loaded && C.rom[0]) loaded = load_rom_file(C.rom, 1);
    if (!loaded) { path_in_exe_dir(C.rom, L"ps4_rus.bin"); loaded = load_rom_file(C.rom, 1); }
    while (!loaded) {
        if (!pick_rom()) return 0;
        loaded = load_rom_file(C.rom, 0);
    }
    if (argv) LocalFree(argv);

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    if (C.fullscreen) toggle_fullscreen();
    timeBeginPeriod(1);
    audio_init();
    input_init();
    sram_read();
    emu_reset();
    apply_video();
    rw_init((size_t)48 << 20);
    cfg_save();
    if (first_run) { menu_set_open(1); m_page = M_HELP; }

    const double frame_ms = 1000.0 / EMU_FPS;
    double next_frame = (double)now_ms();
    int sram_pending = 0;
    uint64_t last_autosave = now_ms();
    static int16_t snd[8192];

    while (!quit) {
        MSG msg;
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit = 1;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) break;
        uint64_t t = now_ms();

        if (menu_open) {
            poll_pads();
            menu_pad();
            present();
            Sleep(16);
            next_frame = (double)now_ms();
            continue;
        }

        poll_pads();
        if (ppressed(A_MENU)) { menu_set_open(1); continue; }
        if (ppressed(A_QSAVE)) state_save(C.slot, 0);
        if (ppressed(A_QLOAD)) state_load(C.slot);
        int fast = act_down[A_TURBO];
        int rewinding = C.rewind && act_down[A_REWIND];

        /* pacing: the sound queue (only one frame of sound is queued per
         * displayed frame, so turbo and battle speed-up run N times faster) */
        if (!rewinding) {
            if (audio_ok) {
                if (audio_queued() > 2400) { Sleep(1); continue; }
            } else {
                if ((double)t < next_frame) { Sleep(1); continue; }
                next_frame += frame_ms;
                if ((double)t - next_frame > 100) next_frame = (double)t;
            }
        }

        if (rewinding) {
            /* two steps back per displayed frame; no sound while rewinding */
            if (rw_pop()) rw_pop(); else set_status(L"дальше назад нельзя");
            emu_set_pad(0, 0);
            emu_run_frame();                        /* draws the picture of that moment */
            int n; while ((n = emu_audio(snd, 4096)) > 0) {}
            present();
            Sleep(16);
            continue;
        }

        emu_set_pad(0, game_pad());
        int frames = fast ? C.turbo_speed : 1;
        if (!fast && C.battle_speed > 1 && emu_in_battle()) frames = C.battle_speed;
        for (int i = 0; i < frames; i++) {
            emu_run_frame();
            frame_no++;
            if (C.rewind && (frame_no & 1)) rw_push();
            int n;
            while ((n = emu_audio(snd, 4096)) > 0)
                if (i == 0) audio_submit(snd, n);
        }
        present();

        if (emu_sram_dirty()) sram_pending = 1;
        if (sram_pending && frame_no % 120 == 0) { sram_write(); sram_pending = 0; }
        if (C.autosave_min && t - last_autosave > (uint64_t)C.autosave_min * 60000 && emu_on_field() && !emu_in_battle()) {
            state_save(0, 0);
            last_autosave = t;
        }
    }
    sram_write();
    if (C.autosave_min) state_save(0, 1);
    cfg_save();
    audio_close();
    timeEndPeriod(1);
    DestroyWindow(hwnd);
    return 0;
}

void Start(void)
{
    ExitProcess((UINT)run());
}
