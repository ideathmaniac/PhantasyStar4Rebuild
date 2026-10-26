/*
 * Phantasy Star IV — native Windows front end for the static recompilation.
 * Plain Win32: GDI (double-buffered) for video and the overlay menu, waveOut
 * for sound, XInput and WinMM joysticks for gamepads.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include "win32.h"
#include "../src/emu.h"
#include "../src/tips.h"
#include "../src/shop.h"
#include "../src/md.h"
#include "../src/hdscale.h"
#include "../src/rewind.h"
#include "diag.h"
#include "glfx.h"
#include "d3dfx.h"
#include "../src/hdpack.h"
#include "../src/png.h"
#include "ui_soft.h"

int _fltused = 0x9875;

#define APP_VERSION_W L"v0.36"
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
    int32_t fx_mode, fx_dither;            /* added in 0.9 */
    int32_t renderer;                      /* added in 0.10: 0 Direct3D 11, 1 OpenGL, 2 GDI */
    int32_t hd_pack, hd_auto;              /* added in 0.12 */
    int32_t tone;                          /* added in 0.13: 0 clean, 1 softer, 2 old TV */
    int32_t ws_battle;                     /* added in 0.14 */
    int32_t backdrop;                      /* added in 0.15: blurred picture in the side bars */
    int32_t cine;                          /* added in 0.16: picture-tube look for story pictures */
    int32_t encounters, exp_mul, mes_mul;  /* added in 0.17 */
    int32_t tips;                          /* added in 0.19: technique descriptions */
    int32_t shop_help;                     /* added in 0.27: shop comparison panel */
    int32_t fast_sound;                    /* added in 0.31: sound during speed-ups: 0 normal tempo, 1 off, 2 sped up */
    int32_t enemy_hp, text_speed;          /* added in 0.32: monsters' HP in battle; text 0 normal, 1 fast, 2 very fast */
    int32_t minimap;                       /* added in 0.34: 0 off, 1 small, 2 large */
    int32_t fog, fast_walk;                /* added in 0.35: minimap fog of war; walking 0 normal, 1 x2 holding B, 2 x2 always */
} cfg_t;
#define CFG_MAGIC 0x32475043u
#define CFG_VERSION 2
#define CFG_V2_SIZE (offsetof(cfg_t, pad_mode))
#define CFG_V3_SIZE (offsetof(cfg_t, fx_mode))
#define CFG_V4_SIZE (offsetof(cfg_t, renderer))
#define CFG_V5_SIZE (offsetof(cfg_t, hd_pack))
#define CFG_V6_SIZE (offsetof(cfg_t, tone))
#define CFG_V7_SIZE (offsetof(cfg_t, ws_battle))
#define CFG_V8_SIZE (offsetof(cfg_t, backdrop))
#define CFG_V9_SIZE (offsetof(cfg_t, cine))
#define CFG_V10_SIZE (offsetof(cfg_t, encounters))
#define CFG_V11_SIZE (offsetof(cfg_t, tips))
#define CFG_V12_SIZE (offsetof(cfg_t, shop_help))
#define CFG_V13_SIZE (offsetof(cfg_t, fast_sound))
#define CFG_V14_SIZE (offsetof(cfg_t, enemy_hp))
#define CFG_V15_SIZE (offsetof(cfg_t, minimap))
#define CFG_V16_SIZE (offsetof(cfg_t, fog))
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
    C.fx_mode = FX_PIXELS; C.fx_dither = 1; C.renderer = 0;
    C.hd_pack = 1; C.hd_auto = 0; C.ws_battle = 1; C.backdrop = 1; C.cine = 1;
    C.encounters = 0; C.exp_mul = 1; C.mes_mul = 1; C.tips = 1; C.shop_help = 1; C.enemy_hp = 1; C.text_speed = 1; C.minimap = 1; C.fog = 1; C.fast_walk = 1;
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

/* log.txt next to the exe: what happened, for bug reports */
static FILE *logf;
static void logw(const char *fmt, ...)
{
    if (!logf) { wchar_t p[700]; wcscpy(p, exe_dir); wcscat(p, L"log.txt"); logf = _wfopen(p, L"w"); }
    if (!logf) return;
    va_list a; va_start(a, fmt);
    char b[512]; _vsnprintf(b, sizeof b - 1, fmt, a); b[sizeof b - 1] = 0;
    va_end(a);
    fprintf(logf, "[%lu] %s\n", (unsigned long)(frame_no), b);
    fflush(logf);
    char nb[200]; _snprintf(nb, 199, "[%lu] %s", (unsigned long)frame_no, b); nb[199] = 0;
    diag_note(nb);
}

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
    if ((n == sizeof C || n == CFG_V2_SIZE || n == CFG_V3_SIZE || n == CFG_V4_SIZE || n == CFG_V5_SIZE || n == CFG_V6_SIZE || n == CFG_V7_SIZE || n == CFG_V8_SIZE || n == CFG_V9_SIZE || n == CFG_V10_SIZE || n == CFG_V11_SIZE || n == CFG_V12_SIZE || n == CFG_V13_SIZE || n == CFG_V14_SIZE || n == CFG_V15_SIZE || n == CFG_V16_SIZE) && ((cfg_t *)b)->magic == CFG_MAGIC && ((cfg_t *)b)->version == CFG_VERSION) {
        memcpy(&C, b, n);                      /* an older file keeps the new fields at their defaults */
        if (n < CFG_V4_SIZE && C.hd) C.fx_mode = FX_SMOOTH;
        if (n < CFG_V5_SIZE) C.renderer = C.use_gl ? 0 : 2;
        if (n < CFG_V8_SIZE) C.ws_battle = 1;
        if (n < CFG_V9_SIZE) C.backdrop = 1;
        if (n < CFG_V10_SIZE) C.cine = 1;
        if (n < CFG_V11_SIZE) { C.encounters = 0; C.exp_mul = 1; C.mes_mul = 1; }
        if (n < CFG_V12_SIZE) C.tips = 1;
        if (n < CFG_V13_SIZE) C.shop_help = 1;
        if (n < CFG_V14_SIZE) C.fast_sound = 0;
        if (n < CFG_V15_SIZE) { C.enemy_hp = 1; C.text_speed = 1; }
        if (n < CFG_V16_SIZE) C.minimap = 1;
        if (n < sizeof C) { C.fog = 1; C.fast_walk = 1; }
    } else if (n >= 20 + 1200 && *(uint32_t *)b == 0x31475043u)   /* version 1 file: keep the ROM path */
        memcpy(C.rom, b + 20, 1200);
    if (C.fx_mode < 0 || C.fx_mode >= FX_COUNT) C.fx_mode = FX_PIXELS;
    if (C.renderer < 0 || C.renderer > 2) C.renderer = 0;
    if (C.tone < 0 || C.tone > 2) C.tone = 0;
    if (C.encounters < 0 || C.encounters > 2) C.encounters = 0;
    if (C.exp_mul < 1 || C.exp_mul > 4) C.exp_mul = 1;
    if (C.mes_mul < 1 || C.mes_mul > 4) C.mes_mul = 1;
    C.tips = C.tips != 0;
    C.shop_help = C.shop_help != 0;
    if (C.fast_sound < 0 || C.fast_sound > 2) C.fast_sound = 0;
    C.enemy_hp = C.enemy_hp != 0;
    if (C.text_speed < 0 || C.text_speed > 2) C.text_speed = 1;
    if (C.minimap < 0 || C.minimap > 2) C.minimap = 1;
    C.fog = C.fog != 0;
    if (C.fast_walk < 0 || C.fast_walk > 2) C.fast_walk = 1;
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
/* play statistics, saves\\PS4_stats.txt */
static struct { uint64_t play_frames, battle_frames, battles, saves, loads; } ST;
static uint64_t session_frames;
static void stats_path(wchar_t *p) { path_in_exe_dir(p, L"saves\\PS4_stats.txt"); }
static void stats_load(void)
{
    wchar_t p[700]; stats_path(p);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    if (!b) return;
    char *t = realloc(b, n + 1); t[n] = 0;
    unsigned long long v[5] = { 0, 0, 0, 0, 0 };
    sscanf(t, "play_frames=%llu battle_frames=%llu battles=%llu saves=%llu loads=%llu", &v[0], &v[1], &v[2], &v[3], &v[4]);
    ST.play_frames = v[0]; ST.battle_frames = v[1]; ST.battles = v[2]; ST.saves = v[3]; ST.loads = v[4];
    free(t);
}
static void stats_save(void)
{
    saves_dir();
    char b[300];
    int k = _snprintf(b, 299, "play_frames=%llu battle_frames=%llu battles=%llu saves=%llu loads=%llu\n",
                      (unsigned long long)ST.play_frames, (unsigned long long)ST.battle_frames, (unsigned long long)ST.battles,
                      (unsigned long long)ST.saves, (unsigned long long)ST.loads);
    wchar_t p[700]; stats_path(p);
    if (k > 0) write_file(p, b, (size_t)k);
}
/* bestiary counts: saves\PS4_bestiary.txt, "id seen defeated" per line */
static void best_path(wchar_t *p) { path_in_exe_dir(p, L"saves\\PS4_bestiary.txt"); }
static void best_load(void)
{
    wchar_t p[700]; best_path(p);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    if (!b) return;
    char *t = realloc(b, n + 1); t[n] = 0;
    for (char *l = t; l && *l; ) {
        unsigned id, se, k;
        if (sscanf(l, "%u %u %u", &id, &se, &k) == 3) bestiary_set((int)id, se, k);
        l = strchr(l, '\n'); if (l) l++;
    }
    free(t);
}
static void fog_path(wchar_t *p) { path_in_exe_dir(p, L"saves\\PS4_minimap.bin"); }
static void fog_load(void)
{
    wchar_t p[700]; fog_path(p);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    int sz; uint8_t *f = emu_fog_data(&sz);
    if (b && f && n == (size_t)sz) memcpy(f, b, n);
    free(b);
}
static void world_path(wchar_t *p) { path_in_exe_dir(p, L"saves\\PS4_world.bin"); }
static void world_load(void)
{
    wchar_t p[700]; world_path(p);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    int sz; uint8_t *f = emu_world_data(&sz);
    if (b && f && n == (size_t)sz) memcpy(f, b, n);
    free(b);
}
static void world_save(void)
{
    int sz; uint8_t *f = emu_world_data(&sz);
    if (!f) return;
    saves_dir();
    wchar_t p[700]; world_path(p);
    write_file(p, f, (size_t)sz);
}
static void fog_save(void)
{
    int sz; uint8_t *f = emu_fog_data(&sz);
    if (!f) return;
    saves_dir();
    wchar_t p[700]; fog_path(p);
    write_file(p, f, (size_t)sz);
}
static void best_save(void)
{
    saves_dir();
    static char b[BEST_MAX * 32]; int k = 0;
    for (int id = 0; id < BEST_MAX; id++) {
        uint32_t se, kd; bestiary_get(id, &se, &kd);
        if (se || kd) k += _snprintf(b + k, sizeof b - k, "%d %u %u\n", id, se, kd);
    }
    wchar_t p[700]; best_path(p);
    write_file(p, b, (size_t)k);
}
/* ---------- achievements: saves\\PS4_achievements.txt, "key YYYYMMDD" per line ---------- */
typedef struct { const char *key; const wchar_t *name, *how; } ach_t;
static const ach_t ACH[] = {
    { "first_win",  L"Первая победа",        L"победить первого монстра" },
    { "kills100",   L"Охотник",              L"победить 100 монстров" },
    { "kills1000",  L"Истребитель",          L"победить 1000 монстров" },
    { "seen100",    L"Энциклопедист",        L"встретить 100 разных монстров" },
    { "lv10",       L"Опытный",              L"герой отряда достиг 10 уровня" },
    { "lv30",       L"Ветеран",              L"герой отряда достиг 30 уровня" },
    { "lv50",       L"Легенда Альгола",      L"герой отряда достиг 50 уровня" },
    { "party5",     L"Полный отряд",         L"собрать отряд из пяти героев" },
    { "rich",       L"Состоятельный",        L"накопить 10 000 месет" },
    { "tycoon",     L"Магнат",               L"накопить 100 000 месет" },
    { "hours10",    L"Долгий путь",          L"провести в игре 10 часов" },
    { "boss_hive",  L"Подвал академии",      L"победить Улей" },
    { "boss_jut",   L"Крепость Зио",         L"победить Джута" },
    { "boss_minion",L"Башня Ладеа",          L"победить Приспешника тьмы (2580 ЕЖ)" },
    { "boss_zio",   L"Конец Зио",            L"победить Зио" },
};
#define NACH ((int)(sizeof ACH / sizeof ACH[0]))
static long long ach_time[NACH];      /* date as YYYYMMDD, 0 = locked */
static wchar_t ach_toast[120]; static uint64_t ach_toast_until;
static int ach_queue[NACH], ach_nqueue;   /* unlocked, waiting for their popup */
static void ach_path(wchar_t *p) { path_in_exe_dir(p, L"saves\\PS4_achievements.txt"); }
static void ach_load(void)
{
    wchar_t p[700]; ach_path(p);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    if (!b) return;
    char *t = realloc(b, n + 1); t[n] = 0;
    for (char *l = t; l && *l; ) {
        char key[40]; long long tm;
        if (sscanf(l, "%39s %lld", key, &tm) == 2)
            for (int i = 0; i < NACH; i++) if (!strcmp(key, ACH[i].key)) ach_time[i] = tm;
        l = strchr(l, '\n'); if (l) l++;
    }
    free(t);
}
static void ach_save(void)
{
    saves_dir();
    char b[NACH * 64]; int k = 0;
    for (int i = 0; i < NACH; i++) if (ach_time[i]) k += _snprintf(b + k, sizeof b - k, "%s %lld\n", ACH[i].key, ach_time[i]);
    wchar_t p[700]; ach_path(p);
    write_file(p, b, (size_t)k);
}
static void ach_unlock(int i)
{
    if (ach_time[i]) return;
    SYSTEMTIME st; GetLocalTime(&st);
    ach_time[i] = st.wYear * 10000LL + st.wMonth * 100 + st.wDay;
    if (ach_nqueue < NACH) ach_queue[ach_nqueue++] = i;
    ach_save();
}
static int ach_index(const char *key) { for (int i = 0; i < NACH; i++) if (!strcmp(ACH[i].key, key)) return i; return -1; }
static void ach_check(void)   /* twice a second */
{
    { best_info_t t; if (!emu_bestiary(9, &t)) return; }   /* known ROM only */
    uint32_t kills = 0, kinds = 0;
    for (int id = 0; id < BEST_MAX; id++) {
        uint32_t se, kd; bestiary_get(id, &se, &kd);
        kills += kd; if (se) kinds++;
    }
    #define ACH_IF(cond, key) do { if (cond) ach_unlock(ach_index(key)); } while (0)
    ACH_IF(kills >= 1, "first_win"); ACH_IF(kills >= 100, "kills100"); ACH_IF(kills >= 1000, "kills1000");
    ACH_IF(kinds >= 100, "seen100");
    ACH_IF(ST.play_frames >= (uint64_t)(EMU_FPS * 36000), "hours10");
    { uint32_t s, k;
      bestiary_get(12, &s, &k); ACH_IF(k > 0, "boss_hive");
      bestiary_get(114, &s, &k); ACH_IF(k > 0, "boss_jut");
      bestiary_get(117, &s, &k); ACH_IF(k > 0, "boss_minion");
      uint32_t z = 0; bestiary_get(139, &s, &k); z += k; bestiary_get(140, &s, &k); z += k; bestiary_get(152, &s, &k); z += k;
      ACH_IF(z > 0, "boss_zio"); }
    int m = emu_game_mode_top();
    if (m != 0x0C && m != 0x14) return;   /* party data is only meaningful in play */
    const uint8_t *r = emu_work_ram();
    int n = 0, lv = 0;
    for (int p = 0; p < 5; p++) {
        int rec = r[0xF40A + p];
        if (rec == 0xFF || rec > 15) break;
        n++;
        int l = r[0xF500 + rec * 0x80 + 8] << 8 | r[0xF500 + rec * 0x80 + 9];
        if (l > lv && l < 100) lv = l;
    }
    ACH_IF(lv >= 10, "lv10"); ACH_IF(lv >= 30, "lv30"); ACH_IF(lv >= 50, "lv50");
    ACH_IF(n >= 5, "party5");
    uint32_t mes = emu_meseta();
    ACH_IF(mes >= 10000 && mes < 10000000, "rich"); ACH_IF(mes >= 100000 && mes < 10000000, "tycoon");
    #undef ACH_IF
}
/* one emulated frame of play */
static void stats_frame(void)
{
    static int was_battle;
    int b = emu_in_battle();
    ST.play_frames++; session_frames++;
    if (b) ST.battle_frames++;
    if (b && !was_battle) ST.battles++;
    was_battle = b;
}
static void fmt_time(wchar_t *o, int n, uint64_t frames)
{
    uint64_t s = (uint64_t)(frames / EMU_FPS);
    _snwprintf(o, n, L"%llu ч %02llu мин", (unsigned long long)(s / 3600), (unsigned long long)(s / 60 % 60));
}

/* save slots: a small picture and the date of each */
typedef struct { int valid; wchar_t date[40]; uint32_t *pix; int w, h; uint64_t when; uint32_t meseta; int nparty; wchar_t name[5][12]; int level[5]; } slot_info_t;
static slot_info_t slot_info[10];
static void thumb_path(wchar_t *p, int slot)
{
    state_path(p, slot);
    size_t L = wcslen(p);
    if (L > 4) wcscpy(p + L - 4, L".png");
}
static void slot_refresh(int slot)
{
    slot_info_t *si = &slot_info[slot];
    free(si->pix); memset(si, 0, sizeof *si);
    wchar_t p[700]; state_path(p, slot);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(p, 0, &fa)) return;
    si->valid = 1;
    si->when = ((uint64_t)fa.ftLastWriteTime.hi << 32) | fa.ftLastWriteTime.lo;
    {   /* money and party from the saved RAM */
        size_t sn = 0; uint8_t *sb = read_file(p, &sn);
        const uint8_t *ram = sb ? emu_state_ram(sb, sn) : 0;
        if (ram) {
            si->meseta = ((uint32_t)ram[0xF438] << 24) | ((uint32_t)ram[0xF439] << 16) | ((uint32_t)ram[0xF43A] << 8) | ram[0xF43B];
            for (int k = 0; k < 5; k++) {
                int c = ram[0xF40A + k];
                if (c > 10) break;
                const uint8_t *rec = ram + 0xF500 + c * 0x80;
                ui_ps4_name(rec, si->name[si->nparty], 12);
                si->level[si->nparty] = (rec[8] << 8) | rec[9];
                si->nparty++;
            }
        }
        free(sb);
    }
    FILETIME_ lt; SYSTEMTIME st;
    if (FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lt) && FileTimeToSystemTime(&lt, &st))
        _snwprintf(si->date, 39, L"%02d.%02d.%04d %02d:%02d", st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute);
    thumb_path(p, slot);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    if (!b) return;
    int w, h; const char *e;
    uint8_t *rgba = png_decode(b, n, &w, &h, &e);
    free(b);
    if (!rgba) return;
    si->pix = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) si->pix[i] = ((uint32_t)rgba[i * 4] << 16) | ((uint32_t)rgba[i * 4 + 1] << 8) | rgba[i * 4 + 2];
    si->w = w; si->h = h;
    free(rgba);
}
static void slots_refresh(void) { for (int i = 0; i < 10; i++) slot_refresh(i); }
static void thumb_write(int slot)
{
    int W, H; const uint32_t *fb = emu_framebuffer(&W, &H);
    int tw = W / 2, th = H / 2;
    uint8_t *t = malloc((size_t)tw * th * 4);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            int acc[3] = { 0, 0, 0 };
            for (int k = 0; k < 4; k++) {
                uint32_t c = fb[(size_t)(y * 2 + (k >> 1)) * MD_W_MAX + x * 2 + (k & 1)];
                acc[0] += (c >> 16) & 255; acc[1] += (c >> 8) & 255; acc[2] += c & 255;
            }
            uint8_t *o = t + ((size_t)y * tw + x) * 4;
            o[0] = (uint8_t)(acc[0] / 4); o[1] = (uint8_t)(acc[1] / 4); o[2] = (uint8_t)(acc[2] / 4); o[3] = 255;
        }
    size_t pn; uint8_t *png = png_encode(t, tw, th, &pn);
    free(t);
    wchar_t p[700]; thumb_path(p, slot);
    write_file(p, png, pn);
    free(png);
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
    if (ok) { thumb_write(slot); slot_refresh(slot); if (!quiet) ST.saves++; stats_save(); }
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
    logw("state_load slot %d", slot);
    wchar_t p[700]; state_path(p, slot);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    wchar_t m[100];
    int ok = b && emu_state_load(b, n);
    logw("state_load: file %s, %lu bytes, loaded %d", b ? "found" : "missing", (unsigned long)n, ok);
    if (slot == 0) wcscpy(m, ok ? L"загружено автосохранение" : L"автосохранения нет");
    else _snwprintf(m, 99, ok ? L"загружен слот %d" : L"слот %d пуст", slot);
    set_status(m);
    free(b);
    if (ok) { rw_clear(); ST.loads++; }
    return ok;
}

/* ======================================================================
 * HD packs: files under hdpack\ next to PS4.exe
 * ====================================================================== */
static wchar_t hd_dir[700];
static void hd_wpath(wchar_t *out, size_t cap, const char *rel)
{
    wcscpy(out, hd_dir);
    size_t n = wcslen(out);
    int k = MultiByteToWideChar(65001, 0, rel, -1, out + n, (int)(cap - n));
    if (k <= 0) out[n] = 0;
    for (wchar_t *c = out + n; *c; c++) if (*c == L'/') *c = L'\\';
}
static int hd_io_write(const char *rel, const void *d, size_t n)
{
    wchar_t p[900]; hd_wpath(p, 900, rel);
    CreateDirectoryW(hd_dir, 0);
    for (wchar_t *c = p + wcslen(hd_dir); *c; c++) if (*c == L'\\') { *c = 0; CreateDirectoryW(p, 0); *c = L'\\'; }
    FILE *f = _wfopen(p, L"wb");
    if (!f) return 0;
    size_t w = fwrite(d, 1, n, f);
    fclose(f);
    return w == n;
}
static uint8_t *hd_io_read(const char *rel, size_t *n)
{
    wchar_t p[900]; hd_wpath(p, 900, rel);
    *n = 0;
    return read_file(p, n);
}
static void hd_io_list(const char *rel, void (*cb)(const char *, void *), void *u)
{
    wchar_t p[900]; hd_wpath(p, 900, rel);
    wcscat(p, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(p, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & 0x10) continue;          /* folders */
        char name[600];
        if (WideCharToMultiByte(65001, 0, fd.cFileName, -1, name, sizeof name, 0, 0) > 0) cb(name, u);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static void hd_io_log(const char *m) { logw("%s", m); }
static uint32_t hd_img[224 * 4 * 448 * 4];
static int fx_ok_ext(void);
static int cine_on;
static int launcher_open;
static double game_dim;
static uint64_t game_fade_from;
static void draw_launcher(int cw, int ch);
static void pace_reset_ext(void);
static void menu_set_open(int o);
static void launcher_reopen(void);
static int rom_kind_loaded;
static void hd_capture_now(void);
static void screenshot(void);
static int hd_on(void) { return C.hd_pack && fx_ok_ext() && hd_tiles() > 0; }

/* ======================================================================
 * Audio (waveOut). The game is paced by the clock (see run()); the sound
 * is resampled by up to 0.5% so the queue stays near A_TARGET, which keeps
 * both the picture and the sound even at any monitor refresh rate.
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
#define A_TARGET 3600                         /* queued sound to aim for: 75 ms */
static uint64_t a_written, a_played, a_moved_ms;
static DWORD a_lastpos;
typedef struct { UINT wType; DWORD sample; DWORD pad; } MMTIME_;
MMRESULT waveOutGetPosition(HWAVEOUT, MMTIME_ *, UINT);
static void audio_reset(void)
{
    waveOutReset(wo);
    a_lastpos = 0; a_played = a_written;
}
/* sound written but not played yet, in sample frames */
static int audio_queued(void)
{
    MMTIME_ mt; memset(&mt, 0, sizeof mt); mt.wType = 2;   /* TIME_SAMPLES */
    if (waveOutGetPosition(wo, &mt, sizeof mt) == 0 && mt.wType == 2) {
        DWORD d = mt.sample - a_lastpos;
        if (d) a_moved_ms = now_ms();
        a_played += d; a_lastpos = mt.sample;
        if (a_played > a_written) a_played = a_written;
        return (int)(a_written - a_played);
    }
    int q = 0;                                  /* driver without positions: count the buffers */
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
        if (waveOutWrite(wo, &hdr[i], sizeof hdr[i]) == 0) a_written += (uint64_t)frames;
        return;
    }
}

/* one emulated frame of sound: resampled toward the target queue length */
static void audio_push(const int16_t *in, int n)
{
    if (!audio_ok || n <= 0) return;
    int q = audio_queued();
    uint64_t t = now_ms();
    if (q > 0 && t - a_moved_ms > 1000 && a_moved_ms) {
        logw("sound device stalled, resetting it"); audio_reset(); q = 0; a_moved_ms = t;
    }
    if (q > A_TARGET * 4) return;               /* far too much queued (after a stall): skip */
    static int16_t buf[2 * 4200];
    if (q < 400) {                              /* ran dry: refill with silence at once */
        int z = A_TARGET - q - n;
        if (z > 4096) z = 4096;
        if (z > 0) { memset(buf, 0, (size_t)z * 4); audio_submit(buf, z); }
    }
    double d = (double)(A_TARGET - q) / A_TARGET * 0.005;
    if (d > 0.005) d = 0.005;
    if (d < -0.005) d = -0.005;
    double step = 1.0 / (1.0 + d);
    /* linear interpolation over [previous sample, in...] */
    static double pos;
    static int16_t prev[2];
    int m = 0;
    while (pos < n && m < 4200) {
        int i = (int)pos; double f = pos - i;
        const int16_t *a = i == 0 ? prev : in + (i - 1) * 2, *b = in + i * 2;
        buf[m * 2] = (int16_t)(a[0] + (b[0] - a[0]) * f);
        buf[m * 2 + 1] = (int16_t)(a[1] + (b[1] - a[1]) * f);
        m++; pos += step;
    }
    pos -= n;
    prev[0] = in[(n - 1) * 2]; prev[1] = in[(n - 1) * 2 + 1];
    audio_submit(buf, m);
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
    diag_thread_init();
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
static int menu_rect[4], status_rect[4], tip_rect[4], ach_rect[4], hp_rect[4], mm_rect[4];   /* x, y, w, h of overlay boxes drawn this frame */
static void draw_tip(int px, int py, int pw, int ph);
static void draw_shop(int px, int py, int pw, int ph);
static void draw_enemy_hp(int px, int py, int pw, int ph);
static void draw_minimap(int px, int py, int pw, int ph);
static int world_open;
static void draw_world(int cw, int ch);
static void draw_ach_toast(void);

/* ---------- OpenGL path (GPU scaling, vsync) ---------- */
static HDC gl_dc;
static HGLRC gl_rc;
static int gl_ok, gl_npot, fx_ok, d3d_ok;
static int fx_ok_ext(void) { return fx_ok; }
static void *gl_getproc(const char *n)
{
    void *p = wglGetProcAddress(n);
    return ((uintptr_t)p <= 3 || (intptr_t)p == -1) ? 0 : p;
}
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
    if (W < 1 || W > MD_W_MAX) W = 320;
    if (H < 1 || H > 224) H = 224;
    int x, y, w, h;
    dest_rect(cw, ch, W, &x, &y, &w, &h);
    int cpu_hd = !fx_ok && C.fx_mode == FX_SMOOTH;
    if (cpu_hd) run_hd(fb, W, H);

    /* overlays are drawn with GDI into the back buffer, then copied */
    menu_rect[2] = status_rect[2] = tip_rect[2] = ach_rect[2] = hp_rect[2] = mm_rect[2] = 0;
    if (launcher_open) draw_launcher(cw, ch);
    else if (world_open && !menu_open) draw_world(cw, ch);
    else if (!menu_open) {
        if (C.tips) draw_tip(x, y, w, h);
        if (C.shop_help && !tip_rect[2]) draw_shop(x, y, w, h);
        if (C.enemy_hp && !tip_rect[2]) draw_enemy_hp(x, y, w, h);   /* the technique panel uses the top in battle */
        if (C.minimap && !tip_rect[2]) draw_minimap(x, y, w, h);
    }
    if (!launcher_open) draw_ach_toast();
    if (menu_open || status_msg[0]) {
        int lr[4]; memcpy(lr, menu_rect, sizeof lr);
        if (menu_open) draw_menu();
        draw_status();
        if (launcher_open) memcpy(menu_rect, lr, sizeof lr);   /* the launcher overlay already covers the window */
    }

    /* HD pack: the picture drawn from the replacement tiles, k times larger */
    const uint32_t *pic = fb; int PW = W, PH = H, ppitch = MD_W_MAX;
    if (hd_on()) {
        int hw, hh;
        if (hd_compose(hd_img, 448 * 4, &hw, &hh)) { pic = hd_img; PW = hw; PH = hh; ppitch = 448 * 4; }
    }
    /* the picture dims behind the pause menu and fades in after the launcher */
    {
        double target = menu_open ? 0.55 : 1.0;
        game_dim += (target - game_dim) * 0.25;
        if (fabs(target - game_dim) < 0.01) game_dim = target;
        double f = game_dim;
        if (game_fade_from) {
            uint64_t a = now_ms() - game_fade_from;
            if (a >= 450) game_fade_from = 0; else f *= (double)a / 450;
        }
        if (launcher_open) f = 1.0;
        if (f < 0.999) {
            static uint32_t dimmed[224 * 4 * 448 * 4];
            int k = (int)(f * 256);
            ui_img di = { dimmed, PW, PH, PW };
            for (int yy = 0; yy < PH; yy++) memcpy(dimmed + (size_t)yy * PW, pic + (size_t)yy * ppitch, (size_t)PW * 4);
            ui_darken(&di, k);
            pic = dimmed; ppitch = PW;
        }
    }
    if (d3d_ok) {
        d3d_frame(pic, PW, PH, ppitch, x, y, w, h, cw, ch, cine_on ? FX_CINE : C.fx_mode, C.smooth, cine_on || C.fx_dither, C.backdrop);
        int *rects[6] = { menu_rect, status_rect, tip_rect, ach_rect, hp_rect, mm_rect };
        if (cw <= back_w && ch <= back_h)
            for (int k = 0; k < 6; k++) {
                int *r = rects[k];
                if (r[0] < 0) { r[2] += r[0]; r[0] = 0; }
                if (r[1] < 0) { r[3] += r[1]; r[1] = 0; }
                if (r[0] + r[2] > back_w) r[2] = back_w - r[0];
                if (r[1] + r[3] > back_h) r[3] = back_h - r[1];
                if (r[2] <= 0 || r[3] <= 0) continue;
                GdiFlush();
                d3d_overlay((const uint32_t *)back_bits, back_w, back_h, r[0], r[1], r[2], r[3]);
            }
        if (!d3d_present(C.vsync)) {
            static int told;
            if (!told) { logw("direct3d: %s", d3d_error()); told = 1; }
        }
        return;
    }
    if (gl_ok) {
        if (vsync_applied != C.vsync && wgl_swap_interval) { wgl_swap_interval(C.vsync ? 1 : 0); vsync_applied = C.vsync; }
        glViewport(0, 0, cw, ch);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, cw, ch, 0, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (fx_ok) {
            glfx_draw(pic, PW, PH, ppitch, x, y, w, h, cw, ch, cine_on ? FX_CINE : C.fx_mode, C.smooth, cine_on || C.fx_dither, C.backdrop);
        } else if (cpu_hd) {
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
        int *rects[6] = { menu_rect, status_rect, tip_rect, ach_rect, hp_rect, mm_rect };
        for (int k = 0; k < 6; k++) {
            int *r = rects[k];
            /* keep the copied box inside the back buffer */
            if (r[0] < 0) { r[2] += r[0]; r[0] = 0; }
            if (r[1] < 0) { r[3] += r[1]; r[1] = 0; }
            if (r[0] + r[2] > back_w) r[2] = back_w - r[0];
            if (r[1] + r[3] > back_h) r[3] = back_h - r[1];
            if (r[2] <= 0 || r[3] <= 0 || cw > back_w || ch > back_h) continue;
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
    if (cpu_hd) StretchDIBits(dc, x, y, w, h, 0, 0, W * 4, H * 4, hd_buf, &bmi_hd, DIB_RGB_COLORS, SRCCOPY);
    else StretchDIBits(dc, x, y, w, h, 0, 0, W, H, fb, &bmi_fb, DIB_RGB_COLORS, SRCCOPY);
    int *rects[6] = { menu_rect, status_rect, tip_rect, ach_rect, hp_rect, mm_rect };
    for (int k = 0; k < 6; k++) if (rects[k][2] > 0) BitBlt(dc, rects[k][0], rects[k][1], rects[k][2], rects[k][3], back_dc, rects[k][0], rects[k][1], SRCCOPY);
    ReleaseDC(hwnd, dc);
}

static void draw_ach_toast(void)
{
    if (ach_toast[0] && now_ms() > ach_toast_until) ach_toast[0] = 0;
    if (!ach_toast[0] && ach_nqueue) {   /* the next one */
        int i = ach_queue[0];
        memmove(ach_queue, ach_queue + 1, sizeof ach_queue[0] * (size_t)--ach_nqueue);
        _snwprintf(ach_toast, 119, L"Достижение: %ls — %ls", ACH[i].name, ACH[i].how);
        ach_toast[119] = 0;
        ach_toast_until = now_ms() + 4500;
    }
    if (!ach_toast[0]) return;
    int tw = text_w(ach_toast, font_small);
    int bh = font_h * 2, bw = tw + font_h * 2 + font_h;
    int bx = back_w - bw - font_h / 2, by = font_h / 2;
    fill(bx, by, bw, bh, RGBC(230, 190, 90));
    fill(bx + 2, by + 2, bw - 4, bh - 4, RGBC(40, 30, 10));
    draw_text(bx + font_h / 2, by + (bh - font_h) / 2, L"★", RGBC(255, 210, 90), font_big);
    draw_text(bx + font_h * 2, by + (bh - font_h * 4 / 5) / 2, ach_toast, RGBC(255, 240, 200), font_small);
    ach_rect[0] = bx; ach_rect[1] = by; ach_rect[2] = bw; ach_rect[3] = bh;
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

/* technique / skill description while the game shows such a list: at the
 * top of the picture in battle (the list is low there), at the bottom on
 * the field (the menus are at the top) */
static void draw_tip(int px, int py, int pw, int ph)
{
    static tip_t shown, seen; static int have, seeing; static uint64_t since;
    tip_t t;
    if (!emu_skill_tip(&t)) { have = seeing = 0; return; }
    if (!seeing || seen.skill != t.skill || seen.id != t.id) { seen = t; seeing = 1; since = now_ms(); }
    if (now_ms() - since >= 50) { shown = seen; have = 1; }   /* skip one-frame flicker while the cursor moves */
    if (!have) return;
    wchar_t name[40], desc[200], note[200], line3[200], tg[60], cost[40];
    MultiByteToWideChar(65001, 0, shown.name, -1, name, 40);
    MultiByteToWideChar(65001, 0, shown.desc, -1, desc, 200);
    MultiByteToWideChar(65001, 0, shown.note, -1, note, 200);
    MultiByteToWideChar(65001, 0, shown.target, -1, tg, 60);
    if (shown.cost >= 0) _snwprintf(cost, 39, L"%d ЕТ", shown.cost); else wcscpy(cost, L"навык");
    line3[0] = 0;
    if (tg[0]) _snwprintf(line3, 199, L"Цель: %ls", tg);
    if (shown.power) {
        wchar_t st[8]; int k;
        for (k = 0; k < 5; k++) st[k] = k < shown.power ? L'●' : L'○';
        st[5] = 0;
        size_t l = wcslen(line3);
        _snwprintf(line3 + l, 199 - l, L"%lsМощность: %ls", l ? L"     " : L"", st);
    }
    /* combination attacks: one line each ("Ураган огня: + ЗАН или БУРЯ") */
    wchar_t combo[400], *cl[4]; int ncl = 0;
    MultiByteToWideChar(65001, 0, shown.combo, -1, combo, 400);
    for (wchar_t *c = combo; *c && ncl < 4; ) {
        cl[ncl++] = c;
        wchar_t *e = wcsstr(c, L"; ");
        if (!e) break;
        *e = 0; c = e + 2;
    }
    int lh = font_h * 6 / 5, pad = font_h / 2;
    int lines = 2 + (line3[0] != 0) + (note[0] != 0) + ncl;
    int bw = pw * 9 / 10, bh = lines * lh + pad * 2;
    int need = text_w(desc, font_small) + pad * 2;
    if (note[0] && text_w(note, font_small) + pad * 2 > need) need = text_w(note, font_small) + pad * 2;
    for (int i = 0; i < ncl; i++) { wchar_t b[420]; _snwprintf(b, 419, L"◆ Комбо %ls", cl[i]); b[419] = 0; if (text_w(b, font_small) + pad * 2 > need) need = text_w(b, font_small) + pad * 2; }
    if (need > bw) bw = need < pw ? need : pw;
    int bx = px + (pw - bw) / 2;
    int by = emu_in_battle() ? py + ph / 40 : py + ph - bh - ph / 40;
    fill(bx, by, bw, bh, RGBC(200, 205, 220));
    fill(bx + 2, by + 2, bw - 4, bh - 4, RGBC(10, 30, 96));
    int ty = by + pad;
    draw_text(bx + pad, ty, name, RGBC(255, 230, 120), font_big);
    draw_text(bx + bw - pad - text_w(cost, font_small), ty + (font_h - font_h * 4 / 5) / 2, cost, RGBC(170, 200, 255), font_small);
    ty += lh;
    draw_text(bx + pad, ty, desc, RGBC(255, 255, 255), font_small); ty += lh;
    if (line3[0]) { draw_text(bx + pad, ty, line3, RGBC(170, 200, 255), font_small); ty += lh; }
    if (note[0]) { draw_text(bx + pad, ty, note, RGBC(170, 200, 255), font_small); ty += lh; }
    for (int i = 0; i < ncl; i++) {
        int rd = (shown.combo_ready >> i) & 1;   /* the other parts are already ordered this round */
        wchar_t b[420]; _snwprintf(b, 419, rd ? L"★ Комбо %ls" : L"◆ Комбо %ls", cl[i]); b[419] = 0;
        if (rd) fill(bx + pad / 2, ty - lh / 10, bw - pad, lh, RGBC(20, 90, 50));
        draw_text(bx + pad, ty, b, rd ? RGBC(140, 255, 150) : RGBC(255, 200, 120), font_small); ty += lh;
    }
    tip_rect[0] = bx; tip_rect[1] = by; tip_rect[2] = bw; tip_rect[3] = bh;
}

/* shop helper: the highlighted item and what it changes for each party
 * member; drawn over the bottom of the picture, where the game's own
 * one-line description is */
static void draw_shop(int px, int py, int pw, int ph)
{
    shop_t s;
    if (!emu_shop(&s)) return;
    wchar_t name[64], kind[48], l1[160], l2[200], rows[5][240];
    DWORD rcol[5];
    MultiByteToWideChar(65001, 0, s.name, -1, name, 64);
    MultiByteToWideChar(65001, 0, s.kind, -1, kind, 48);
    long left = (long)s.money - s.price;
    if (s.price < 0) { left = 0; wcscpy(l1, L"в сумке"); }   /* the inventory, not a shop */
    else if (left >= 0) _snwprintf(l1, 159, L"%d мес. — останется %ld", s.price, left);
    else _snwprintf(l1, 159, L"%d мес. — не хватает %ld", s.price, -left);
    l1[159] = 0;
    wchar_t use[130]; MultiByteToWideChar(65001, 0, s.use, -1, use, 130);
    {
        int k = _snwprintf(l2, 199, L"%ls", kind);
        if (s.atk) k += _snwprintf(l2 + k, 199 - k, L", атака %d", s.atk);
        if (s.def) k += _snwprintf(l2 + k, 199 - k, L", защита %d", s.def);
        if (s.owned && s.price >= 0) _snwprintf(l2 + k, 199 - k, L"   (в сумке: %d)", s.owned);
        l2[199] = 0;
    }
    int nr = 0;
    for (int i = 0; i < s.nrow; i++) {
        shop_row_t *r = &s.row[i];
        wchar_t who[32], now[110];
        MultiByteToWideChar(65001, 0, r->who, -1, who, 32);
        MultiByteToWideChar(65001, 0, r->now, -1, now, 110);
        if (!r->can) { _snwprintf(rows[nr], 239, L"%ls: не может носить", who); rcol[nr] = RGBC(130, 140, 170); }
        else if (r->same) { _snwprintf(rows[nr], 239, s.price < 0 ? L"%ls: надето такое же" : L"%ls: уже надето", who); rcol[nr] = RGBC(200, 210, 230); }
        else {
            wchar_t d[80] = L""; int k = 0;
            if (r->datk) k += _snwprintf(d + k, 79 - k, L"   %ls %+d атаки", r->datk > 0 ? L"▲" : L"▼", r->datk);
            if (r->ddef) k += _snwprintf(d + k, 79 - k, L"   %ls %+d защиты", r->ddef > 0 ? L"▲" : L"▼", r->ddef);
            if (!k) wcscpy(d, L"   без изменений");
            _snwprintf(rows[nr], 239, L"%ls: %ls →%ls", who, now, d);
            int better = r->datk + r->ddef;
            rcol[nr] = better > 0 ? RGBC(140, 255, 150) : better < 0 ? RGBC(255, 150, 140) : RGBC(230, 230, 230);
        }
        rows[nr][239] = 0; nr++;
    }
    int lh = font_h * 6 / 5, pad = font_h / 2;
    int lines = 2 + nr + (use[0] != 0);
    int bh = lines * lh + pad * 2, bw = pw * 9 / 10;
    int need = text_w(name, font_big) + text_w(l1, font_small) + pad * 4;
    if (text_w(l2, font_small) + pad * 2 > need) need = text_w(l2, font_small) + pad * 2;
    if (use[0] && text_w(use, font_small) + pad * 2 > need) need = text_w(use, font_small) + pad * 2;
    for (int i = 0; i < nr; i++) if (text_w(rows[i], font_small) + pad * 2 > need) need = text_w(rows[i], font_small) + pad * 2;
    if (need > bw) bw = need < pw ? need : pw;
    int bx = px + (pw - bw) / 2, by = s.battle ? py + ph / 40 : py + ph - bh - ph / 40;   /* the battle list is low on screen */
    fill(bx, by, bw, bh, RGBC(200, 205, 220));
    fill(bx + 2, by + 2, bw - 4, bh - 4, RGBC(10, 30, 96));
    int ty = by + pad;
    draw_text(bx + pad, ty, name, RGBC(255, 230, 120), font_big);
    if (!s.battle) draw_text(bx + bw - pad - text_w(l1, font_small), ty + (font_h - font_h * 4 / 5) / 2, l1,
              left >= 0 ? RGBC(170, 200, 255) : RGBC(255, 150, 140), font_small);
    ty += lh;
    draw_text(bx + pad, ty, l2, RGBC(255, 255, 255), font_small); ty += lh;
    if (use[0]) { draw_text(bx + pad, ty, use, RGBC(140, 255, 150), font_small); ty += lh; }
    for (int i = 0; i < nr; i++) { draw_text(bx + pad, ty, rows[i], rcol[i], font_small); ty += lh; }
    tip_rect[0] = bx; tip_rect[1] = by; tip_rect[2] = bw; tip_rect[3] = bh;
}

/* world map screen (key N): the planet explored so far, over the whole window */
static void draw_world(int cw, int ch)
{
    static uint32_t img[512 * 1024];
    int w, h, mx, my;
    if (!emu_world_image(img, 512, 1024, &w, &h, &mx, &my)) { world_open = 0; set_status(L"карта мира доступна на карте планеты"); return; }
    fill(0, 0, cw, ch, RGBC(8, 8, 16));
    int sc = (ch - font_h * 3) / h; if ((cw - font_h * 2) / w < sc) sc = (cw - font_h * 2) / w;
    int dw, dh;
    if (sc >= 1) { dw = w * sc; dh = h * sc; } else { dh = ch - font_h * 3; dw = w * dh / h; if (dw > cw) { dw = cw; dh = h * dw / w; } }
    int x0 = (cw - dw) / 2, y0 = font_h * 2;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(back_dc, sc >= 1 ? COLORONCOLOR : HALFTONE); SetBrushOrgEx(back_dc, 0, 0, 0);
    StretchDIBits(back_dc, x0, y0, dw, dh, 0, 0, w, h, img, &bi, DIB_RGB_COLORS, SRCCOPY);
    int on = (now_ms() / 300) & 1, m = font_h / 2 > 4 ? font_h / 2 : 4;
    int cx = x0 + mx * dw / w, cy = y0 + my * dh / h;
    fill(cx - m / 2 - 1, cy - m / 2 - 1, m + 2, m + 2, RGBC(0, 0, 0));
    fill(cx - m / 2, cy - m / 2, m, m, on ? RGBC(255, 60, 60) : RGBC(255, 230, 120));
    draw_text(font_h, font_h / 2, L"Карта мира — открывается по мере путешествия.  N или Esc — закрыть", RGBC(225, 230, 255), font_small);
    menu_rect[0] = 0; menu_rect[1] = 0; menu_rect[2] = cw; menu_rect[3] = ch;
}

/* minimap: the field map around the party, top right of the picture */
static void draw_minimap(int px, int py, int pw, int ph)
{
    static uint32_t img[160 * 120];
    static uint64_t last_clear;
    int big = C.minimap == 2, w = big ? 160 : 112, h = big ? 120 : 84, mx, my;
    int r = emu_minimap(img, w, h, &mx, &my);
    if (!r) return;
    if (r == 2) last_clear = now_ms();
    else if (now_ms() - last_clear > 300) return;   /* hidden while windows or dialogs cover the field */
    int sc = ph / 224; if (sc < 1) sc = 1;               /* one map pixel = one console pixel */
    if (big) sc = sc * 3 / 2 > 1 ? sc * 3 / 2 : 1;
    int dw = w * sc, dh = h * sc, b = sc > 1 ? sc : 2;
    int bx = px + pw - dw - b * 2 - ph / 60, by = py + ph / 60;
    fill(bx, by, dw + b * 2, dh + b * 2, RGBC(170, 180, 220));
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(back_dc, COLORONCOLOR);
    StretchDIBits(back_dc, bx + b, by + b, dw, dh, 0, 0, w, h, img, &bi, DIB_RGB_COLORS, SRCCOPY);
    /* the party: a blinking marker */
    int on = (now_ms() / 300) & 1, m = sc * 3 > 4 ? sc * 3 : 4;
    int cx = bx + b + mx * sc, cy = by + b + my * sc;
    if (mx >= 0 && my >= 0 && mx < w && my < h) {
        fill(cx - m / 2 - 1, cy - m / 2 - 1, m + 2, m + 2, RGBC(0, 0, 0));
        fill(cx - m / 2, cy - m / 2, m, m, on ? RGBC(255, 60, 60) : RGBC(255, 230, 120));
    }
    mm_rect[0] = bx; mm_rect[1] = by; mm_rect[2] = dw + b * 2; mm_rect[3] = dh + b * 2;
}

/* monsters' HP in battle: a small list in the top right corner of the picture */
static void draw_enemy_hp(int px, int py, int pw, int ph)
{
    (void)ph;
    int ids[8], hp[8], mx[8];
    int n = bestiary_battle(ids, hp, mx, 8);
    if (n <= 0) return;
    wchar_t nm[8][48], wk[8][110]; int alive = 0, wname = 0, wweak = 0;
    for (int i = 0; i < n; i++) {
        best_info_t b;
        if (!emu_bestiary(ids[i], &b)) return;
        MultiByteToWideChar(65001, 0, b.name, -1, nm[i], 48);
        wk[i][0] = 0;
        if (b.weak[0]) { wchar_t w2[100]; MultiByteToWideChar(65001, 0, b.weak, -1, w2, 100); _snwprintf(wk[i], 109, L"слаб: %ls", w2); wk[i][109] = 0; }
        if (hp[i] > 0) { alive++; int w = text_w(nm[i], font_small); if (w > wname) wname = w;
                         int w3 = wk[i][0] ? text_w(wk[i], font_small) + font_h / 2 : 0; if (w3 > wweak) wweak = w3; }
    }
    if (!alive) return;
    int lh = font_h, pad = font_h / 3, barw = font_h * 4, numw = text_w(L"0000/0000", font_small);
    int bw = pad * 3 + wname + font_h / 2 + barw + font_h / 2 + numw + wweak, bh = alive * lh + pad * 2;
    int bx = px + pw - bw - font_h / 3, by = py + font_h / 3;
    fill(bx, by, bw, bh, RGBC(150, 160, 200));
    fill(bx + 1, by + 1, bw - 2, bh - 2, RGBC(10, 20, 60));
    int ty = by + pad;
    for (int i = 0; i < n; i++) {
        if (hp[i] <= 0) continue;
        int m = mx[i] > 0 ? mx[i] : hp[i];
        draw_text(bx + pad, ty, nm[i], RGBC(230, 230, 255), font_small);
        int x = bx + pad * 2 + wname + font_h / 2, yb = ty + lh / 3, hb = lh / 3;
        int f = m ? barw * (hp[i] > m ? m : hp[i]) / m : 0;
        DWORD col = hp[i] * 4 <= m ? RGBC(240, 80, 70) : hp[i] * 2 <= m ? RGBC(240, 200, 70) : RGBC(90, 220, 110);
        fill(x, yb, barw, hb, RGBC(40, 40, 70));
        if (f > 0) fill(x, yb, f, hb, col);
        wchar_t t[24]; _snwprintf(t, 23, L"%d/%d", hp[i], m); t[23] = 0;
        draw_text(x + barw + font_h / 2, ty, t, RGBC(200, 210, 240), font_small);
        if (wk[i][0]) draw_text(x + barw + font_h / 2 + numw + font_h / 2, ty, wk[i], RGBC(140, 255, 150), font_small);
        ty += lh;
    }
    hp_rect[0] = bx; hp_rect[1] = by; hp_rect[2] = bw; hp_rect[3] = bh;
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
enum { M_MAIN, M_VIDEO, M_SPEED, M_KEYS, M_PAD, M_HELP, M_HD, M_SAVE, M_STATS, M_BEST, M_ACH, M_SET, M_SOUND, M_HINTS };
static int m_best_page, m_best_next = -1, m_best_ids[10];   /* bestiary page shown; index of the "next page" item */
static int m_save_load;                     /* M_SAVE lists slots to load (1) or to save into (0) */
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
        add(L"Сохранить", L"▸");
        add(L"Загрузить", L"▸");
        add(L"Настройки", L"▸");
        add(L"Журнал: статистика, бестиарий, достижения", L"▸");
        add(L"Справка", L"▸");
        add(L"Выйти в главное меню", 0);
        add(m_confirm_quit ? L"Выйти? Нажмите ещё раз" : L"Выход", 0);
        break;
    case M_VIDEO: {
        static const wchar_t *wsn[3] = { L"4:3 (оригинал)", L"16:10", L"16:9" };
        static const wchar_t *scn[3] = { L"по размеру окна", L"целочисленный", L"растянуть" };
        wcscpy(m_title, L"Экран и графика");
        add(L"Формат кадра", wsn[C.ws_mode]);
        static const wchar_t *fxn[FX_COUNT] = { L"чёткие пиксели", L"сглаживание пиксель-арта", L"ЭЛТ-телевизор", L"ЭЛТ-телевизор, выпуклый" };
        static const wchar_t *fxn_cpu[FX_COUNT] = { L"чёткие пиксели", L"сглаживание ×4 (процессор)", L"ЭЛТ (нужен OpenGL 2.0)", L"ЭЛТ (нужен OpenGL 2.0)" };
        add(L"Фильтр изображения", fx_ok ? fxn[C.fx_mode] : fxn_cpu[C.fx_mode]);
        add(L"Масштаб", scn[C.scale_mode]);
        add(L"Размытие при масштабировании", onoff(C.smooth));
        add(L"Смешивать дизеринг (как на ТВ)", fx_ok ? onoff(C.fx_dither) : L"нужен OpenGL 2.0");
        add(L"Полный экран", onoff(fullscreen));
        add(L"Вертикальная синхронизация", onoff(C.vsync));
        {
            static const wchar_t *rn[3] = { L"Direct3D 11", L"OpenGL", L"GDI (без фильтров)" };
            int active = d3d_ok ? 0 : gl_ok ? 1 : 2;
            static wchar_t rv[80];
            if (C.renderer == active) wcscpy(rv, rn[active]);
            else _snwprintf(rv, 79, L"%s (после перезапуска)", rn[C.renderer]);
            add(L"Вывод изображения", rv);
        }
        add(L"Широкий экран в боях", C.ws_mode ? onoff(C.ws_battle) : L"нужен формат 16:10 или 16:9");
        add(L"Поля по бокам", fx_ok ? (C.backdrop ? L"размытая картинка" : L"чёрные") : L"чёрные");
        add(L"Заставки как на кинескопе", fx_ok ? onoff(C.cine) : L"нужен Direct3D 11 или OpenGL 2.0");
        add(L"HD-пакет графики", L"▸");
        add(L"Назад", 0);
        break; }
    case M_STATS: {
        wcscpy(m_title, L"Журнал");
        static wchar_t a[80], b2[80], c[80], d[80], e[80];
        fmt_time(a, 79, ST.play_frames); add(L"Время в игре (всего)", a);
        fmt_time(b2, 79, session_frames); add(L"За этот запуск", b2);
        { wchar_t t[40]; fmt_time(t, 39, ST.battle_frames); _snwprintf(c, 79, L"%llu, %s", (unsigned long long)ST.battles, t); }
        add(L"Боёв (и время в боях)", c);
        int m = emu_game_mode_top();
        if (m == 0x0C || m == 0x14) _snwprintf(d, 79, L"%lu", (unsigned long)emu_meseta()); else wcscpy(d, L"—");
        add(L"Месеты сейчас", d);
        _snwprintf(e, 79, L"%llu / %llu", (unsigned long long)ST.saves, (unsigned long long)ST.loads);
        add(L"Быстрых сохранений / загрузок", e);
        add(L"Бестиарий", L"▸");
        { static wchar_t a2[40]; int got = 0; for (int i = 0; i < NACH; i++) got += ach_time[i] != 0;
          _snwprintf(a2, 39, L"%d из %d  ▸", got, NACH); add(L"Достижения", a2); }
        add(L"Карта мира (клавиша N)", L"▸");
        add(L"Назад", 0);
        break; }
    case M_ACH: {
        int got = 0; for (int i = 0; i < NACH; i++) got += ach_time[i] != 0;
        _snwprintf(m_title, 79, L"Достижения: %d из %d", got, NACH);
        static wchar_t val[NACH][80];
        for (int i = 0; i < NACH; i++) {
            long long t = ach_time[i];
            if (t) _snwprintf(val[i], 79, L"★ %02d.%02d.%04d", (int)(t % 100), (int)(t / 100 % 100), (int)(t / 10000));
            else _snwprintf(val[i], 79, L"%ls", ACH[i].how);
            val[i][79] = 0;
            add(ACH[i].name, val[i]);
        }
        add(L"Назад", 0);
        break; }
    case M_BEST: {
        /* the monsters met so far, 10 per page, in the game's order */
        static int ids[BEST_MAX]; int n = 0, total = 0;
        for (int id = 0; id < BEST_MAX; id++) {
            best_info_t b;
            if (!emu_bestiary(id, &b) || !strcmp(b.name, "Пусто")) continue;
            total++;
            if (b.seen) ids[n++] = id;
        }
        int pages = (n + 9) / 10; if (pages < 1) pages = 1;
        if (m_best_page >= pages) m_best_page = 0;
        _snwprintf(m_title, 79, L"Бестиарий: встречено %d из %d (стр. %d/%d)", n, total, m_best_page + 1, pages);
        static wchar_t lab[10][48], val[10][80];
        for (int i = 0; i < 10; i++) m_best_ids[i] = -1;
        for (int i = 0; i < 10 && m_best_page * 10 + i < n; i++) {
            best_info_t b; emu_bestiary(ids[m_best_page * 10 + i], &b);
            MultiByteToWideChar(65001, 0, b.name, -1, lab[i], 48);
            _snwprintf(val[i], 79, L"ЕЖ %d · опыт %d · %d мес · побеждено %u", b.hp, b.exp, b.meseta, b.killed);
            m_best_ids[i] = ids[m_best_page * 10 + i];
            val[i][79] = 0;
            add(lab[i], val[i]);
        }
        if (!n) add(L"Пока никого — загляните сюда после первого боя", 0);
        m_best_next = -1;
        if (pages > 1) { m_best_next = nitems; add(L"Следующая страница (←/→ — листать)", L"▸"); }
        add(L"Назад", 0);
        break; }
    case M_SAVE: {
        wcscpy(m_title, m_save_load ? L"Загрузить" : L"Сохранить");
        for (int k = m_save_load ? 0 : 1; k <= 9; k++) {
            wchar_t lab[40];
            if (k == 0) wcscpy(lab, L"Автосохранение");
            else _snwprintf(lab, 39, L"Слот %d%s", k, k == C.slot ? L"  (F5/F8)" : L"");
            add(lab, slot_info[k].valid ? slot_info[k].date : L"пусто");
        }
        add(L"Назад", 0);
        break; }
    case M_HD: {
        wcscpy(m_title, L"HD-пакет графики");
        static wchar_t v1[80], v2[80];
        if (!fx_ok) wcscpy(v1, L"нужен Direct3D 11 или OpenGL 2.0");
        else if (!hd_tiles()) wcscpy(v1, L"пакет пуст");
        else _snwprintf(v1, 79, L"%s (плиток: %d, ×%d)", C.hd_pack ? L"вкл" : L"выкл", hd_tiles(), hd_scale());
        add(L"Использовать HD-пакет", v1);
        if (hd_on()) _snwprintf(v2, 79, L"%d%%", (int)(hd_coverage() * 100 + 0.5)); else wcscpy(v2, L"—");
        add(L"Заменено на этом экране", v2);
        add(L"Снимок графики сейчас (F9)", 0);
        add(L"Автоснимки новой графики", onoff(C.hd_auto));
        add(L"Собрать пакет из hdpack\\upscaled", 0);
        add(L"Открыть папку hdpack", 0);
        add(L"Назад", 0);
        break; }
    case M_SET:
        wcscpy(m_title, L"Настройки");
        add(L"Экран и графика", L"▸");
        add(L"Звук", L"▸");
        add(L"Игра и бои", L"▸");
        add(L"Подсказки", L"▸");
        add(L"Управление: клавиатура", L"▸");
        add(L"Управление: геймпад", L"▸");
        add(L"Назад", 0);
        break;
    case M_SOUND: {
        wcscpy(m_title, L"Звук");
        _snwprintf(v, 79, L"%d%%", C.volume); add(L"Громкость", v);
        { static const wchar_t *tn[3] = { L"чистый", L"мягче", L"как старый телевизор" }; add(L"Звучание", tn[C.tone]); }
        { static const wchar_t *fs[3] = { L"музыка в обычном темпе", L"без звука", L"ускоренный" }; add(L"Звук при ускорении", fs[C.fast_sound]); }
        add(L"Назад", 0);
        break; }
    case M_HINTS:
        wcscpy(m_title, L"Подсказки");
        add(L"Описания техник и навыков", onoff(C.tips));
        add(L"Помощник в магазине и инвентаре", onoff(C.shop_help));
        add(L"Здоровье монстров в бою", onoff(C.enemy_hp));
        { static const wchar_t *mm[3] = { L"выкл", L"маленькая", L"большая" }; add(L"Миникарта (клавиша M)", mm[C.minimap]); }
        add(L"Миникарта: только пройденное", onoff(C.fog));
        add(L"Назад", 0);
        break;
    case M_SPEED: {
        static const wchar_t *bs[4] = { L"", L"выкл", L"×2", L"×3" };
        wcscpy(m_title, L"Игра и бои");
        add(L"Ускорение боёв", bs[C.battle_speed]);
        _snwprintf(v, 79, L"×%d", C.turbo_speed); add(L"Ускорение по кнопке", v);
        { static const wchar_t *tx[3] = { L"обычная", L"быстрая (×3)", L"очень быстрая (×8)" }; add(L"Скорость текста", tx[C.text_speed]); }
        { static const wchar_t *fw[3] = { L"обычная", L"×2, пока зажата B", L"×2 всегда" }; add(L"Ходьба по карте", fw[C.fast_walk]); }
        add(L"Перемотка назад", onoff(C.rewind));
        if (C.autosave_min) _snwprintf(v, 79, L"каждые %d мин", C.autosave_min); else wcscpy(v, L"выкл");
        add(L"Автосохранение", v);
        { static const wchar_t *en[3] = { L"как в оригинале", L"реже (в 2–3 раза)", L"нет (сюжетные остаются)" }; add(L"Случайные бои", en[C.encounters]); }
        _snwprintf(v, 79, L"×%d", C.exp_mul); add(L"Опыт за бои", v);
        { wchar_t v2[20]; _snwprintf(v2, 19, L"×%d", C.mes_mul); add(L"Месеты за бои", v2); }
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
        add(L"F12", L"снимок экрана (папка screenshots)");
        add(L"C, Пробел / Xbox A", L"подтвердить, говорить");
        add(L"X / Xbox B", L"отмена");
        add(L"Enter / Start", L"меню игры");
        add(L"Обычные сохранения игры", L"в церкви, файл saves\\PS4_battery.srm");
        add(L"Назад", 0);
        break;
    }
}

static int item_y[32], item_h, item_first, item_count, item_x0, item_x1;
/* ======================================================================
 * Launcher: the first screen (Continue, Load, New game, Settings, Exit)
 * ====================================================================== */
static int launcher_open = 1, l_page, l_sel, l_load_sel, l_hover = -1;
static uint32_t l_back[64 * 36];
static uint8_t *l_logo; static int l_lw, l_lh, l_ready;
static uint64_t l_opened;
static int l_item_y[16], l_item_h, l_item_x0, l_item_x1, l_nitems;
static HGDIOBJ l_font_menu, l_font_small, l_font_card; static int l_font_for;
static double game_dim = 1.0;          /* picture brightness, animated (pause menu, launcher fade) */
static uint64_t game_fade_from;        /* fade the game in after the launcher */
static int latest_slot(void)
{
    int best = -1;
    for (int i = 0; i < 10; i++) if (slot_info[i].valid && (best < 0 || slot_info[i].when > slot_info[best].when)) best = i;
    return best;
}

/* the title screen, for the background and the logo: taken once by running
 * the game silently to its title, then kept in saves\launcher_title.png */
static void launcher_prepare(void)
{
    wchar_t p[700]; path_in_exe_dir(p, L"saves\\launcher_title.png");
    uint32_t *title = 0; int tw = 0, th = 0;
    size_t n = 0; uint8_t *b = read_file(p, &n);
    if (b) {
        const char *e; uint8_t *rgba = png_decode(b, n, &tw, &th, &e);
        free(b);
        if (rgba) {
            title = malloc((size_t)tw * th * 4);
            for (int i = 0; i < tw * th; i++) title[i] = ((uint32_t)rgba[i * 4] << 16) | ((uint32_t)rgba[i * 4 + 1] << 8) | rgba[i * 4 + 2];
            free(rgba);
        }
    }
    if (!title && rom_kind_loaded == EMU_ROM_RECOMPILED) {
        logw("launcher: taking the title picture");
        static int16_t snd2[8192];
        emu_reset();
        int W, H;
        /* the title appears after the intro, about 2100 frames in; wait a
         * second more for its fade-in to finish */
        int seen = -1;
        for (int f = 0; f < 3200; f++) {
            emu_set_pad(0, 0);
            emu_run_frame();
            while (emu_audio(snd2, 4096) > 0) {}
            if (seen < 0 && f >= 1500 && f % 20 == 0) {
                const uint32_t *fb = emu_framebuffer(&W, &H);
                int lw, lh; uint8_t *lg = ui_logo(fb, W, H, MD_W_MAX, &lw, &lh);
                if (lg) { free(lg); seen = f; }
            }
            if (seen >= 0 && f == seen + 60) {
                const uint32_t *fb = emu_framebuffer(&W, &H);
                int lw, lh; uint8_t *lg = ui_logo(fb, W, H, MD_W_MAX, &lw, &lh);
                if (lg) {
                    free(lg);
                    tw = W; th = H; title = malloc((size_t)W * H * 4);
                    for (int y = 0; y < H; y++) memcpy(title + (size_t)y * W, fb + (size_t)y * MD_W_MAX, (size_t)W * 4);
                    uint8_t *rgba = malloc((size_t)W * H * 4);
                    for (int i = 0; i < W * H; i++) { rgba[i * 4] = (uint8_t)(title[i] >> 16); rgba[i * 4 + 1] = (uint8_t)(title[i] >> 8); rgba[i * 4 + 2] = (uint8_t)title[i]; rgba[i * 4 + 3] = 255; }
                    size_t pn; uint8_t *png = png_encode(rgba, W, H, &pn);
                    saves_dir(); write_file(p, png, pn);
                    free(png); free(rgba);
                    break;
                }
            }
        }
        emu_reset();
    }
    if (title) {
        ui_backdrop(title, tw, th, tw, l_back, 64, 36);
        l_logo = ui_logo(title, tw, th, tw, &l_lw, &l_lh);
        free(title);
    } else for (int i = 0; i < 64 * 36; i++) l_back[i] = 0x080A18;
    slots_refresh();
    l_sel = latest_slot() >= 0 ? 0 : 2;
    l_ready = 1;
    l_opened = now_ms();
}

static const wchar_t *l_items[5] = { L"Продолжить", L"Загрузить", L"Новая игра", L"Настройки", L"Выход" };
static int l_enabled(int i) { return i != 0 || latest_slot() >= 0; }
/* which save the card shows */
static int l_card_slot(void)
{
    if (l_page == 1) return l_load_sel <= 9 ? l_load_sel : -1;
    return latest_slot();
}

static void l_text(int x, int y, const wchar_t *t, DWORD col, HGDIOBJ f)
{
    SelectObject(back_dc, f); SetBkMode(back_dc, TRANSPARENT); SetTextColor(back_dc, col);
    TextOutW(back_dc, x, y, t, (int)wcslen(t));
}
static int l_tw_(const wchar_t *t, HGDIOBJ f) { SIZE sz; SelectObject(back_dc, f); GetTextExtentPoint32W(back_dc, t, (int)wcslen(t), &sz); return sz.cx; }

static void draw_launcher(int cw, int ch)
{
    if (!l_ready) return;
    double u = ch / 900.0;
    if (l_font_for != ch) {
        if (l_font_menu) { DeleteObject(l_font_menu); DeleteObject(l_font_small); DeleteObject(l_font_card); }
        l_font_menu = CreateFontW((int)(34 * u), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        l_font_small = CreateFontW((int)(17 * u), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        l_font_card = CreateFontW((int)(21 * u), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        l_font_for = ch;
    }
    /* background: the blurred title, stretched smoothly */
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 64; bi.bmiHeader.biHeight = -36;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(back_dc, HALFTONE); SetBrushOrgEx(back_dc, 0, 0, 0);
    StretchDIBits(back_dc, 0, 0, cw, ch, 0, 0, 64, 36, l_back, &bi, DIB_RGB_COLORS, SRCCOPY);
    GdiFlush();
    ui_img d = { (uint32_t *)back_bits, back_w, back_h, back_w };
    int lx = (int)(110 * u);
    /* logo */
    int logo_bottom = (int)(60 * u);   /* the subtitle goes under the logo as actually drawn */
    if (l_logo) {
        /* whole-pixel scale that fits above the list (it starts lower on the main page than on the
         * other pages) and across the window; no logo at all when even 1x does not fit */
        double room = (l_page == 0 ? 470 : 200) * u - 60 * u - 40 * u;
        if (room > ch * 0.27) room = ch * 0.27;
        int sc = (int)(room / l_lh), wsc = (cw - lx) / (l_lw > 0 ? l_lw : 1);
        if (sc > wsc) sc = wsc;
        if (sc >= 1) {
            ui_glow_rgba(&d, lx - (int)(30 * u), (int)(60 * u), l_logo, l_lw, l_lh, sc, 0xFFB43C, 90);
            logo_bottom = (int)(60 * u) + l_lh * sc + (int)(12 * u);
        }
    }
    /* menu */
    int my = (int)(470 * u), ih = (int)(62 * u), sub = (int)(22 * u);
    l_item_x0 = lx; l_item_x1 = lx + (int)(440 * u); l_item_h = ih;
    const wchar_t *labels[12]; wchar_t subs[12][60]; int n = 0;
    if (l_page == 0) {
        for (int i = 0; i < 5; i++) { labels[n] = l_items[i]; subs[n][0] = 0; n++; }
        int ls = latest_slot();
        if (ls >= 0) {
            if (ls == 0) _snwprintf(subs[0], 59, L"Автосохранение · %s", slot_info[ls].date);
            else _snwprintf(subs[0], 59, L"Слот %d · %s", ls, slot_info[ls].date);
        }
    } else {
        static wchar_t lab[11][30];
        for (int k = 0; k <= 9; k++) {
            if (k == 0) wcscpy(lab[k], L"Автосохранение"); else _snwprintf(lab[k], 29, L"Слот %d", k);
            labels[n] = lab[k];
            if (slot_info[k].valid) wcscpy(subs[n], slot_info[k].date); else wcscpy(subs[n], L"пусто");
            n++;
        }
        labels[n] = L"Назад"; subs[n][0] = 0; n++;
        my = (int)(200 * u); ih = (int)(52 * u); l_item_h = ih;
    }
    l_nitems = n;
    int sel = l_page == 0 ? l_sel : l_load_sel;
    int y = my;
    for (int i = 0; i < n; i++) {
        int hgt = (l_page == 0 && subs[i][0]) ? ih + sub : ih;
        l_item_y[i] = y;
        if (i == sel) {
            ui_hgrad(&d, lx, y, (int)(440 * u), hgt, 0x5A78FF, 120, 0);
            ui_panel(&d, lx, y, (int)(4 * u) > 2 ? (int)(4 * u) : 2, hgt, 0, 0xFFD76A, 255);
        }
        y += hgt + (int)(4 * u);
    }
    l_item_y[n] = y;
    /* card */
    int cx = (int)(820 * u), cy = (int)(150 * u), cwid = cw - cx - (int)(110 * u);
    if (cwid > (int)(760 * u)) { cx += cwid - (int)(760 * u); cwid = (int)(760 * u); }
    int slot = l_card_slot();
    slot_info_t *si = slot >= 0 ? &slot_info[slot] : 0;
    int pad = (int)(22 * u), iw = cwid - 2 * pad, ihh = si && si->pix ? iw * si->h * 7 / (si->w * 8) : iw * 7 / 16;
    int chh = pad + ihh + (int)(150 * u) + pad;
    if (si && si->valid) {
        ui_panel(&d, cx, cy, cwid, chh, (int)(16 * u), 0x0E1228, 190);
        ui_panel(&d, cx + (int)(16 * u), cy, cwid - (int)(32 * u), 1, 0, 0xA0B4FF, 70);
        for (int k = 0; k < si->nparty && k < 5; k++) {
            int pw = (iw - (si->nparty - 1) * (int)(10 * u)) / si->nparty;
            ui_panel(&d, cx + pad + k * (pw + (int)(10 * u)), cy + pad + ihh + (int)(108 * u), pw, (int)(34 * u), (int)(8 * u), 0xFFFFFF, 16);
        }
    }
    /* fade in */
    uint64_t age = now_ms() - l_opened;
    /* GDI: picture and text */
    if (si && si->valid && si->pix) {
        BITMAPINFO tb; memset(&tb, 0, sizeof tb);
        tb.bmiHeader.biSize = sizeof tb.bmiHeader; tb.bmiHeader.biWidth = si->w; tb.bmiHeader.biHeight = -si->h;
        tb.bmiHeader.biPlanes = 1; tb.bmiHeader.biBitCount = 32; tb.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(back_dc, HALFTONE); SetBrushOrgEx(back_dc, 0, 0, 0);
        StretchDIBits(back_dc, cx + pad, cy + pad, iw, ihh, 0, 0, si->w, si->h, si->pix, &tb, DIB_RGB_COLORS, SRCCOPY);
    }
    if (si && si->valid) {
        int ty = cy + pad + ihh + (int)(14 * u), row = (int)(32 * u);
        wchar_t a[60], v[60];
        if (slot == 0) wcscpy(a, L"Автосохранение"); else _snwprintf(a, 59, L"Слот %d", slot);
        l_text(cx + pad, ty, a, RGBC(207, 215, 255), l_font_card);
        l_text(cx + cwid - pad - l_tw_(si->date, l_font_card), ty, si->date, RGBC(255, 215, 106), l_font_card);
        l_text(cx + pad, ty + row, L"Месеты", RGBC(207, 215, 255), l_font_card);
        _snwprintf(v, 59, L"%lu", (unsigned long)si->meseta);
        l_text(cx + cwid - pad - l_tw_(v, l_font_card), ty + row, v, RGBC(255, 215, 106), l_font_card);
        { wchar_t t[40]; fmt_time(t, 39, ST.play_frames); l_text(cx + pad, ty + row * 2, L"Время в игре (всего)", RGBC(207, 215, 255), l_font_card);
          l_text(cx + cwid - pad - l_tw_(t, l_font_card), ty + row * 2, t, RGBC(255, 215, 106), l_font_card); }
        for (int k = 0; k < si->nparty && k < 5; k++) {
            int pw = (iw - (si->nparty - 1) * (int)(10 * u)) / si->nparty, px = cx + pad + k * (pw + (int)(10 * u)), py = cy + pad + ihh + (int)(108 * u);
            l_text(px + (int)(10 * u), py + (int)(7 * u), si->name[k], RGBC(225, 230, 255), l_font_small);
            _snwprintf(v, 59, L"Ур %d", si->level[k]);
            l_text(px + pw - (int)(10 * u) - l_tw_(v, l_font_small), py + (int)(7 * u), v, RGBC(255, 215, 106), l_font_small);
        }
    } else if (l_page == 1 && slot >= 0) {
        l_text(cx + pad, cy + pad, L"Этот слот пуст", RGBC(150, 160, 200), l_font_card);
    }
    l_text(lx + (int)(8 * u), logo_bottom, L"НАТИВНАЯ ВЕРСИЯ ДЛЯ ПК · " APP_VERSION_W, RGBC(159, 176, 255), l_font_small);
    y = my;
    for (int i = 0; i < n; i++) {
        int hgt = (l_page == 0 && subs[i][0]) ? ih + sub : ih;
        DWORD col = !(l_page == 0 && !l_enabled(i)) ? (i == sel ? RGBC(255, 255, 255) : RGBC(201, 210, 255)) : RGBC(100, 108, 140);
        l_text(lx + (int)(30 * u), y + (l_page == 0 ? (int)(10 * u) : (int)(8 * u)), labels[i], col, l_page == 0 ? l_font_menu : l_font_card);
        if (subs[i][0]) {
            if (l_page == 0) l_text(lx + (int)(32 * u), y + ih - (int)(6 * u), subs[i], RGBC(154, 166, 216), l_font_small);
            else l_text(lx + (int)(440 * u) - (int)(20 * u) - l_tw_(subs[i], l_font_small), y + (int)(14 * u), subs[i], RGBC(154, 166, 216), l_font_small);
        }
        y += hgt + (int)(4 * u);
    }
    const wchar_t *hint = L"↑↓  выбор     Enter / A  начать     Esc / B  назад     мышь и геймпад тоже работают";
    l_text((cw - l_tw_(hint, l_font_small)) / 2, ch - (int)(50 * u), hint, RGBC(135, 146, 196), l_font_small);
    GdiFlush();
    if (age < 400) ui_darken(&d, (int)(256 * age / 400));
    menu_rect[0] = 0; menu_rect[1] = 0; menu_rect[2] = cw; menu_rect[3] = ch;
}

static void launcher_reopen(void)
{
    slots_refresh();
    launcher_open = 1; l_page = 0;
    l_sel = latest_slot() >= 0 ? 0 : 2;
    l_opened = now_ms();
}

static void launcher_start_game(void)
{
    launcher_open = 0;
    game_fade_from = now_ms();
    pace_reset_ext();
}

static void launcher_act(int i)
{
    if (l_page == 0) {
        switch (i) {
        case 0: { int ls = latest_slot(); if (ls >= 0 && state_load(ls)) { if (ls) C.slot = ls; launcher_start_game(); } } break;
        case 1: l_page = 1; l_load_sel = latest_slot() >= 0 ? latest_slot() : 1; break;
        case 2: emu_reset(); rw_clear(); launcher_start_game(); break;
        case 3: menu_set_open(1); m_page = M_SET; m_sel = 0; break;
        case 4: quit = 1; break;
        }
    } else {
        if (i > 9) { l_page = 0; l_sel = 1; return; }
        if (slot_info[i].valid && state_load(i)) { if (i) C.slot = i; launcher_start_game(); }
        else set_status(L"этот слот пуст");
    }
}
static void launcher_move(int d)
{
    if (l_page == 0) { do l_sel = (l_sel + 5 + d) % 5; while (!l_enabled(l_sel)); }
    else l_load_sel = (l_load_sel + 11 + d) % 11;
}
static void launcher_back(void) { if (l_page == 1) { l_page = 0; l_sel = 1; } }
static void launcher_key(int vk)
{
    switch (vk) {
    case VK_UP: launcher_move(-1); break;
    case VK_DOWN: launcher_move(1); break;
    case VK_RETURN: case VK_SPACE: launcher_act(l_page == 0 ? l_sel : l_load_sel); break;
    case VK_ESCAPE: case VK_BACK: launcher_back(); break;
    }
}
static void launcher_pad(void)
{
    if (ppressed(A_UP)) launcher_move(-1);
    if (ppressed(A_DOWN)) launcher_move(1);
    if (ppressed(A_C) || ppressed(A_START)) launcher_act(l_page == 0 ? l_sel : l_load_sel);
    if (ppressed(A_B) || ppressed(A_MENU)) launcher_back();
}
static int launcher_hit(int mx, int my)
{
    if (mx < l_item_x0 || mx >= l_item_x1) return -1;
    for (int i = 0; i < l_nitems; i++) if (my >= l_item_y[i] && my < l_item_y[i + 1]) return i;
    return -1;
}

static void draw_menu(void)
{
    build_menu();
    int lh = font_h * 13 / 10;
    int pw = back_w * 3 / 4, ph = lh * (nitems + 2) + font_h;
    if (ph > back_h - 20) ph = back_h - 20;
    int px = (back_w - pw) / 2, py = (back_h - ph) / 2;
    menu_rect[0] = px - 3; menu_rect[1] = py - 3; menu_rect[2] = pw + 6; menu_rect[3] = ph + 6;
    int lw = m_page == M_SAVE ? pw * 11 / 20 : pw;   /* the save list leaves room for the picture */
    item_h = lh; item_x0 = px; item_x1 = px + lw; item_count = 0;
    fill(px - 3, py - 3, pw + 6, ph + 6, RGBC(170, 180, 220));
    fill(px, py, pw, ph, RGBC(16, 28, 74));
    if (m_page == M_SAVE) {
        int slot = (m_save_load ? 0 : 1) + m_sel;
        int ax = px + lw + font_h / 2, aw = pw - lw - font_h, ay = py + lh * 2, ah = ph - lh * 2 - font_h / 2;
        if (slot <= 9 && slot_info[slot].pix && aw > 10 && ah > 10) {
            slot_info_t *si = &slot_info[slot];
            /* the picture at the console's 4:3 pixel shape, as large as fits */
            double sc = (double)aw / (si->w * 8.0 / 7.0);
            if (si->h * sc > ah) sc = (double)ah / si->h;
            int dw = (int)(si->w * 8.0 / 7.0 * sc), dh = (int)(si->h * sc);
            BITMAPINFO bi; memset(&bi, 0, sizeof bi);
            bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = si->w; bi.bmiHeader.biHeight = -si->h;
            bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(back_dc, HALFTONE); SetBrushOrgEx(back_dc, 0, 0, 0);
            fill(ax + (aw - dw) / 2 - 2, ay - 2, dw + 4, dh + 4, RGBC(170, 180, 220));
            StretchDIBits(back_dc, ax + (aw - dw) / 2, ay, dw, dh, 0, 0, si->w, si->h, si->pix, &bi, DIB_RGB_COLORS, SRCCOPY);
        } else if (slot <= 9) {
            const wchar_t *t = slot_info[slot].valid ? L"без картинки" : L"пусто";
            draw_text(ax + aw / 2 - text_w(t, font_small) / 2, ay + ah / 3, t, RGBC(140, 150, 190), font_small);
        }
    }
    draw_text(px + font_h, py + font_h / 2, m_title, RGBC(255, 255, 255), font_big);
    int first = 0, rows = (ph - lh * 2) / lh;
    if (m_sel >= rows) first = m_sel - rows + 1;
    item_first = first;
    for (int i = first; i < nitems && i - first < rows; i++) {
        int yy = py + lh * 2 + (i - first) * lh;
        item_y[item_count++] = yy;
        if (i == m_sel) fill(px + font_h / 2, yy - 2, lw - font_h, lh, RGBC(60, 90, 170));
        draw_text(px + font_h, yy, items[i].label, i == m_sel ? RGBC(255, 255, 160) : RGBC(225, 230, 255), font_small);
        if (items[i].value[0]) {
            int tw = text_w(items[i].value, font_small);
            draw_text(px + lw - font_h - tw, yy, items[i].value, RGBC(160, 220, 255), font_small);
        }
    }
}

static void menu_set_open(int o)
{
    menu_open = o; m_page = M_MAIN; m_sel = 0; m_wait_bind = -1; m_confirm_quit = 0;
    if (!o) cfg_save();
}

static void apply_video(void) { emu_set_widescreen(ws_ext_for_mode[C.ws_mode]); emu_set_widescreen_battles(C.ws_battle); }

/* dir: 0 activate, -1/+1 left/right */
static void menu_act(int dir)
{
    build_menu();
    int i = m_sel;
    logw("menu page %d item %d %s", m_page, i, dir ? (dir < 0 ? "left" : "right") : "select");
    if (m_page == M_MAIN) {
        if (i != 7) m_confirm_quit = 0;
        switch (i) {
        case 0: if (!dir) menu_set_open(0); break;
        case 1: case 2:
            if (!dir) { slots_refresh(); m_save_load = i == 2; m_page = M_SAVE; m_sel = m_save_load ? C.slot : C.slot - 1; }
            break;
        case 3: if (!dir) { m_page = M_SET; m_sel = 0; } break;
        case 4: if (!dir) { m_page = M_STATS; m_sel = 5; } break;
        case 5: if (!dir) { m_page = M_HELP; m_sel = 0; } break;
        case 6: if (!dir) { menu_set_open(0); launcher_reopen(); } break;
        case 7: if (!dir) { if (m_confirm_quit) quit = 1; else m_confirm_quit = 1; } break;
        }
    } else if (m_page == M_SET) {
        static const int to[6] = { M_VIDEO, M_SOUND, M_SPEED, M_HINTS, M_KEYS, M_PAD };
        if (!dir) { if (i < 6) { m_page = to[i]; m_sel = 0; } else { m_page = M_MAIN; m_sel = 3; } }
    } else if (m_page == M_SOUND) {
        int d = dir ? dir : 1;
        switch (i) {
        case 0: C.volume += d * 10; if (C.volume > 100) C.volume = dir ? 100 : 0; if (C.volume < 0) C.volume = 0; break;
        case 1: C.tone = (C.tone + 3 + d) % 3; emu_set_audio_tone(C.tone); break;
        case 2: C.fast_sound = (C.fast_sound + 3 + d) % 3; break;
        case 3: if (!dir) { m_page = M_SET; m_sel = 1; } break;
        }
    } else if (m_page == M_HINTS) {
        switch (i) {
        case 0: C.tips = !C.tips; break;
        case 1: C.shop_help = !C.shop_help; break;
        case 2: C.enemy_hp = !C.enemy_hp; break;
        case 3: C.minimap = (C.minimap + 3 + (dir ? dir : 1)) % 3; break;
        case 4: C.fog = !C.fog; emu_set_fog(C.fog); break;
        case 5: if (!dir) { m_page = M_SET; m_sel = 3; } break;
        }
    } else if (m_page == M_VIDEO) {
        int d = dir ? dir : 1;
        switch (i) {
        case 0: C.ws_mode = (C.ws_mode + 3 + d) % 3; apply_video(); fit_window(); break;
        case 1: C.fx_mode = (C.fx_mode + FX_COUNT + d) % FX_COUNT; C.hd = C.fx_mode == FX_SMOOTH; break;
        case 2: C.scale_mode = (C.scale_mode + 3 + d) % 3; break;
        case 3: C.smooth = !C.smooth; break;
        case 4: C.fx_dither = !C.fx_dither; break;
        case 5: toggle_fullscreen(); break;
        case 6: C.vsync = !C.vsync; break;
        case 7: C.renderer = (C.renderer + 3 + d) % 3; C.use_gl = C.renderer != 2; set_status(L"вступит в силу после перезапуска"); break;
        case 8: C.ws_battle = !C.ws_battle; apply_video(); break;
        case 9: C.backdrop = !C.backdrop; break;
        case 10: C.cine = !C.cine; emu_set_cutscene_detect(C.cine); break;
        case 11: if (!dir) { m_page = M_HD; m_sel = 0; } break;
        case 12: if (!dir) { m_page = M_SET; m_sel = 0; } break;
        }
    } else if (m_page == M_SPEED) {
        int d = dir ? dir : 1;
        static const int ts[4] = { 2, 3, 4, 8 };
        switch (i) {
        case 0: C.battle_speed += d; if (C.battle_speed > 3) C.battle_speed = 1; if (C.battle_speed < 1) C.battle_speed = 3; break;
        case 1: { int k = 0; for (int j = 0; j < 4; j++) if (ts[j] == C.turbo_speed) k = j; k = (k + 4 + d) % 4; C.turbo_speed = ts[k]; break; }
        case 2: C.text_speed = (C.text_speed + 3 + d) % 3; break;
        case 3: C.fast_walk = (C.fast_walk + 3 + d) % 3; break;
        case 4: C.rewind = !C.rewind; rw_clear(); break;
        case 5: C.autosave_min = C.autosave_min == 0 ? (d > 0 ? 5 : 15) : C.autosave_min + 5 * d; if (C.autosave_min > 15 || C.autosave_min < 0) C.autosave_min = 0; break;
        case 6: C.encounters = (C.encounters + 3 + d) % 3; break;
        case 7: C.exp_mul = (C.exp_mul - 1 + 4 + d) % 4 + 1; break;
        case 8: C.mes_mul = (C.mes_mul - 1 + 4 + d) % 4 + 1; break;
        case 9: if (!dir) { m_page = M_SET; m_sel = 2; } break;
        }
        emu_set_conveniences(C.encounters, C.exp_mul, C.mes_mul);
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
        } else { m_sel = m_page == M_KEYS ? 4 : 5; m_page = M_SET; }
    } else if (m_page == M_HELP) {
        if (!dir) { m_page = M_MAIN; m_sel = 5; }
    } else if (m_page == M_STATS) {
        if (!dir && i == 5) { m_page = M_BEST; m_best_page = 0; m_sel = 0; }
        if (!dir && i == 6) { m_page = M_ACH; m_sel = NACH; }
        if (!dir && i == 7) { menu_set_open(0); world_open = 1; }
        if (!dir && i == 8) { m_page = M_MAIN; m_sel = 4; }
    } else if (m_page == M_ACH) {
        if (!dir && i == NACH) { m_page = M_STATS; m_sel = 6; }
    } else if (m_page == M_BEST) {
        if (dir || i == m_best_next) { if (m_best_next < 0) return; m_best_page += dir < 0 ? -1 : 1; if (m_best_page < 0) m_best_page = 0; build_menu(); if (m_sel >= nitems) m_sel = nitems - 1; }
        else if (!dir && i == nitems - 1) { m_page = M_STATS; m_sel = 5; }
        else if (!dir && i < 10 && (m_best_next < 0 ? i < nitems - 1 : i < m_best_next)) {   /* details of one monster */
            best_info_t b;
            if (emu_bestiary(m_best_ids[i], &b)) {
                wchar_t nm[48], t[200]; MultiByteToWideChar(65001, 0, b.name, -1, nm, 48);
                wchar_t wk[100], rs[130];
                MultiByteToWideChar(65001, 0, b.weak, -1, wk, 100); MultiByteToWideChar(65001, 0, b.resist, -1, rs, 130);
                _snwprintf(t, 199, L"%ls%ls: атака %d, защита %d, ловкость %d%ls%ls%ls%ls", nm, b.machine ? L" (механизм)" : L"",
                           b.attack, b.defence, b.agility, wk[0] ? L"; слаб: " : L"", wk, rs[0] ? L"; устойчив: " : L"", rs);
                t[199] = 0; set_status(t);
            }
        }
    } else if (m_page == M_SAVE) {
        if (!dir) {
            int first = m_save_load ? 0 : 1, slot = first + i;
            if (slot > 9) { m_page = M_MAIN; m_sel = m_save_load ? 2 : 1; }
            else if (m_save_load) {
                if (slot_info[slot].valid && state_load(slot)) { if (slot) C.slot = slot; menu_set_open(0); }
                else set_status(L"этот слот пуст");
            } else { state_save(slot, 0); C.slot = slot; menu_set_open(0); }
        }
    } else if (m_page == M_HD) {
        switch (i) {
        case 0: C.hd_pack = !C.hd_pack; break;
        case 2: if (!dir) { menu_set_open(0); hd_capture_now(); } break;
        case 3: C.hd_auto = !C.hd_auto; set_status(C.hd_auto ? L"новая графика будет сниматься сама" : L"автоснимки выключены"); break;
        case 4: if (!dir) {
            char r[300]; hd_import(r, sizeof r);
            wchar_t m[300]; MultiByteToWideChar(65001, 0, r, -1, m, 299);
            set_status(m);
        } break;
        case 5: if (!dir) { CreateDirectoryW(hd_dir, 0); ShellExecuteW(0, L"open", hd_dir, 0, 0, 1); } break;
        case 6: if (!dir) { m_page = M_VIDEO; m_sel = 11; } break;
        }
    }
}

static void menu_back(void)
{
    if (m_wait_bind >= 0) { m_wait_bind = -1; return; }
    if (m_page == M_MAIN) menu_set_open(0);
    else if (m_page == M_HD) { m_page = M_VIDEO; m_sel = 11; }
    else if (m_page == M_BEST) { m_page = M_STATS; m_sel = 5; }
    else if (m_page == M_ACH) { m_page = M_STATS; m_sel = 6; }
    else if (m_page == M_VIDEO || m_page == M_SOUND || m_page == M_SPEED || m_page == M_HINTS || m_page == M_KEYS || m_page == M_PAD) {
        int from = m_page; m_page = M_SET;
        m_sel = from == M_VIDEO ? 0 : from == M_SOUND ? 1 : from == M_SPEED ? 2 : from == M_HINTS ? 3 : from == M_KEYS ? 4 : 5;
    }
    else { int from = m_page; m_page = M_MAIN;
           m_sel = from == M_SET ? 3 : from == M_SAVE ? (m_save_load ? 2 : 1) : from == M_STATS ? 4 : 5; }
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
/* F12: the picture as the console makes it (and the HD pack version) to screenshots\ */
static void screenshot(void)
{
    wchar_t dir[700]; path_in_exe_dir(dir, L"screenshots");
    CreateDirectoryW(dir, 0);
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t name[800];
    int W, H; const uint32_t *fb = emu_framebuffer(&W, &H);
    const uint32_t *src = fb; int pw = W, ph = H, pitch = MD_W_MAX, hw, hh;
    if (hd_on() && hd_compose(hd_img, 448 * 4, &hw, &hh)) { src = hd_img; pw = hw; ph = hh; pitch = 448 * 4; }
    uint8_t *rgba = malloc((size_t)pw * ph * 4);
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            uint32_t c = src[(size_t)y * pitch + x]; uint8_t *o = rgba + ((size_t)y * pw + x) * 4;
            o[0] = (uint8_t)(c >> 16); o[1] = (uint8_t)(c >> 8); o[2] = (uint8_t)c; o[3] = 255;
        }
    size_t n; uint8_t *png = png_encode(rgba, pw, ph, &n);
    free(rgba);
    _snwprintf(name, 799, L"%s\\PS4_%04d%02d%02d_%02d%02d%02d.png", dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    int ok = write_file(name, png, n);
    free(png);
    set_status(ok ? L"снимок экрана сохранён в папку screenshots" : L"не удалось сохранить снимок экрана");
}

static void hd_capture_now(void)
{
    int was = g_hd_track;
    int no = hd_capture();
    g_hd_track = was;
    wchar_t m[100];
    if (no) _snwprintf(m, 99, L"снимок графики %d сохранён в hdpack\\scenes", no);
    else wcscpy(m, L"не удалось сохранить снимок графики");
    set_status(m);
}

static void on_key(int vk, int alt)
{
    if (vk == VK_RETURN && alt) { toggle_fullscreen(); return; }
    if (vk == VK_F1 + 10) { toggle_fullscreen(); return; }
    if (menu_open) { menu_key(vk); return; }
    if (launcher_open) { launcher_key(vk); return; }
    if (vk == VK_F1) { menu_set_open(1); m_page = M_HELP; return; }
    if (world_open && (vk == 'N' || vk == VK_ESCAPE)) { world_open = 0; return; }
    if (vk == 'N') {   /* world map screen (unless N is bound to something) */
        int bound = 0;
        for (int a = 0; a < NACT; a++) for (int i = 0; i < NKEY; i++) if (C.keys[a][i] == 'N') bound = 1;
        if (!bound) { world_open = 1; return; }
    }
    if (vk == 'M') {   /* minimap: off, small, large (unless M is bound to something) */
        int bound = 0;
        for (int a = 0; a < NACT; a++) for (int i = 0; i < NKEY; i++) if (C.keys[a][i] == 'M') bound = 1;
        if (!bound) {
            static const wchar_t *mm[3] = { L"миникарта выключена", L"миникарта: маленькая", L"миникарта: большая" };
            C.minimap = (C.minimap + 1) % 3; set_status(mm[C.minimap]); return;
        }
    }
    for (int i = 0; i < NKEY; i++) {
        if (C.keys[A_MENU][i] == vk) { menu_set_open(1); return; }
        if (C.keys[A_QSAVE][i] == vk) { state_save(C.slot, 0); return; }
        if (C.keys[A_QLOAD][i] == vk) { state_load(C.slot); return; }
    }
    if (vk == VK_F1 + 5) { if (--C.slot < 1) C.slot = 9; wchar_t m[50]; _snwprintf(m, 49, L"слот %d", C.slot); set_status(m); }
    if (vk == VK_F1 + 6) { if (++C.slot > 9) C.slot = 1; wchar_t m[50]; _snwprintf(m, 49, L"слот %d", C.slot); set_status(m); }
    if (vk == VK_PAUSE) menu_set_open(1);
    if (vk == VK_F1 + 8) hd_capture_now();
    if (vk == VK_F1 + 11) screenshot();
}

static int menu_hit(int mx, int my)
{
    if (!menu_open || mx < item_x0 || mx >= item_x1) return -1;
    for (int k = 0; k < item_count; k++)
        if (my >= item_y[k] - 2 && my < item_y[k] - 2 + item_h) return item_first + k;
    return -1;
}

/* story pictures: switch the look after 8 frames in a row, so one-frame
 * transitions do not flicker */
static int cine_run;
static void cine_update(void)
{
    int c = C.cine && emu_is_cutscene();
    if (c == cine_on) { cine_run = 0; return; }
    if (++cine_run >= 8) { cine_on = c; cine_run = 0; }
}

/* one displayed frame: 1 or more emulated frames, their sound, the picture */
static int16_t snd[8192];
static void game_frames(int fast)
{
    g_hd_track = hd_on() || C.hd_auto;
    emu_set_pad(0, game_pad());
    int frames = fast ? C.turbo_speed : 1;
    if (!fast && C.battle_speed > 1 && emu_in_battle()) frames = C.battle_speed;
    /* faster text: while a message is being typed out, run several frames per displayed one */
    /* faster walking on field maps: two frames per displayed one, music at its tempo */
    if (!fast && C.fast_walk && frames < 2 && emu_on_field() && emu_game_mode_top() == 0x0C &&
        (C.fast_walk == 2 || (game_pad() & PAD_B))) frames = 2;
    static int text_left;
    if (C.text_speed && text_left > 0) { int t = C.text_speed == 1 ? 3 : 8; if (frames < t) frames = t; text_left--; }
    for (int i = 0; i < frames; i++) {
        /* speed-up: by default the sound runs only in the first emulated frame of each displayed
         * one, so the music keeps its tempo (some sound effects may be skipped) */
        emu_set_sound_hold(i > 0 && C.fast_sound == 0);
        emu_run_frame();
        if (C.text_speed && emu_text_printing()) text_left = 4;
        frame_no++;
        stats_frame();
        cine_update();
        if (C.rewind && (frame_no & 1)) rw_push();
        int n;
        while ((n = emu_audio(snd, 4096)) > 0)
            if (i == 0) {
                if (frames > 1 && C.fast_sound == 1) memset(snd, 0, (size_t)n * 4);   /* silence, still paced */
                audio_push(snd, n);
            }
    }
    emu_set_sound_hold(0);
    if (C.hd_auto) hd_auto_capture();
    present();
    static int last_mode = -1;
    int mode = emu_game_mode();
    if (mode != last_mode) { logw("game mode %02X, 68k pc %06X", mode, emu_cpu_pc()); last_mode = mode; }
    if (frame_no % 3600 == 0) logw("alive, game mode %02X, 68k pc %06X", mode, emu_cpu_pc());
}

/* pacing: one emulated frame every 1/59.92 s by the performance counter */
static double pace_next;
static const double frame_ms = 1000.0 / EMU_FPS;
static double clock_ms(void) { int64_t t; QueryPerformanceCounter(&t); return (double)t * 1000.0 / (double)qpc_freq; }
static void pace_reset(void) { pace_next = clock_ms(); }
static void pace_reset_ext(void) { pace_reset(); }
/* 1 when the next frame is due (waits the last few milliseconds precisely) */
static int pace_due(int may_wait)
{
    double now = clock_ms();
    if (now < pace_next) {
        if (!may_wait || pace_next - now > 3.0) return 0;
        while ((now = clock_ms()) < pace_next) { if (pace_next - now > 1.2) Sleep(1); else Sleep(0); }
    }
    pace_next += frame_ms;
    if (now - pace_next > 100) pace_next = now;   /* fell far behind (a stall): do not race */
    return 1;
}

/* Windows runs its own loop while the window is dragged or resized; a timer
 * keeps the game going meanwhile */
static int in_sizemove;
static void sizemove_tick(void)
{
    diag_heartbeat(frame_no);
    if (menu_open) { present(); pace_reset(); return; }
    for (int k = 0; k < 3 && pace_due(0); k++) game_frames(0);
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
        if (!menu_open && launcher_open) {
            int k = launcher_hit(mx, my);
            if (k >= 0 && (l_page || l_enabled(k))) { if (l_page) l_load_sel = k; else l_sel = k; }
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (menu_open && m_wait_bind < 0) { int i = menu_hit(mx, my); if (i >= 0) { m_sel = i; menu_act(0); } }
        else if (launcher_open) { int k = launcher_hit(mx, my); if (k >= 0 && (l_page || l_enabled(k))) launcher_act(k); }
        return 0;
    case WM_RBUTTONDOWN:
        if (menu_open) menu_back(); else if (launcher_open) launcher_back(); else menu_set_open(1);
        return 0;
    case WM_MOUSEWHEEL:
        if (menu_open && m_wait_bind < 0) {
            build_menu();
            int d = (short)((w >> 16) & 0xFFFF);
            m_sel = (m_sel + nitems + (d > 0 ? -1 : 1)) % nitems;
        }
        return 0;
    case WM_ENTERSIZEMOVE: in_sizemove = 1; SetTimer(h, 1, 8, 0); return 0;
    case WM_EXITSIZEMOVE: in_sizemove = 0; KillTimer(h, 1); return 0;
    case WM_TIMER: if (in_sizemove && h == hwnd) sizemove_tick(); return 0;
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

static int rom_kind_loaded;
static int load_rom_file(const wchar_t *path, int quiet)
{
    size_t n = 0;
    uint8_t *b = read_file(path, &n);
    if (!b) { if (!quiet) MessageBoxW(hwnd, L"Не удалось прочитать файл ROM.", L"Ошибка", MB_OK | MB_ICONERROR); return 0; }
    int kind = emu_load_rom(b, n);
    free(b);
    rom_kind_loaded = kind;
    logw("rom %lu bytes, kind %d, recompiled code %s", (unsigned long)n, kind, emu_has_recompiled_code() ? "yes" : "no (interpreter build)");
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
static const char *settings_info(void)
{
    static char b[200];
    _snprintf(b, 199, "hd %d/%d/%d ws %d fx %d/%d d3d %d gl %d vsync %d full %d scale %d smooth %d rewind %d battle x%d hd-threads %d rom-kind %d",
              C.hd_pack, hd_tiles(), C.hd_auto, C.ws_mode, C.fx_mode, fx_ok, d3d_ok, gl_ok, C.vsync, fullscreen, C.scale_mode, C.smooth, C.rewind, C.battle_speed, hd_threads, rom_kind_loaded);
    b[199] = 0;
    return b;
}

static int run(void)
{
    QueryPerformanceFrequency(&qpc_freq);
    SetProcessDPIAware();
    GetModuleFileNameW(0, exe_dir, 599);
    wchar_t *sl = wcsrchr(exe_dir, L'\\');
    if (sl) sl[1] = 0; else exe_dir[0] = 0;
    diag_init(exe_dir, "0.36");
    diag_info = settings_info;
    logw("start, version 0.36");
    /* another copy still running (for example stuck after a crash) */
    CreateMutexW(0, 0, L"PS4Recomp.single");
    if (GetLastError() == 183 /* ERROR_ALREADY_EXISTS */) {
        logw("another copy is running");
        if (MessageBoxW(0, L"Phantasy Star IV уже запущена (или зависла после сбоя и осталась в памяти).\n\n"
                           L"Если окна не видно, закройте PS4.exe в Диспетчере задач (Ctrl+Shift+Esc).\n\n"
                           L"Всё равно запустить ещё одну копию?", L"Phantasy Star IV",
                        MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND) != IDYES) return 0;
    }
    /* a "running" marker that only a clean exit removes */
    wchar_t marker[700]; path_in_exe_dir(marker, L"running.flag");
    int crashed_before = GetFileAttributesW(marker) != 0xFFFFFFFF;
    { FILE *mf = _wfopen(marker, L"w"); if (mf) fclose(mf); }

    HINSTANCE inst = GetModuleHandleW(0);
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC; wc.lpfnWndProc = wndproc;
    wc.hInstance = inst; wc.hCursor = LoadCursorW(0, IDC_ARROW); wc.hIcon = LoadIconW(inst, (LPCWSTR)(uintptr_t)1);
    if (!wc.hIcon) wc.hIcon = LoadIconW(0, IDI_APPLICATION);
    wc.lpszClassName = L"PS4Recomp";
    RegisterClassExW(&wc);

    cfg_load();
    if (crashed_before && MessageBoxW(0, L"Прошлый запуск завершился аварийно.\n\nСбросить настройки (графика, управление, полный экран)? "
                                       L"Сохранения игры не затрагиваются.", L"Phantasy Star IV", MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND) == IDYES) {
        cfg_defaults();
        logw("settings reset after a crash");
    }
    int first_run = C.win_w == 0;
    int Wt = 320 + 2 * ws_ext_for_mode[C.ws_mode];
    RECT r = { 0, 0, (LONG)(Wt * 3 * (4.0 / 3.0 * 224.0 / 320.0) + 0.5), 224 * 3 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    if (C.win_w > 200 && C.win_h > 150) { ww = C.win_w; wh = C.win_h; }
    hwnd = CreateWindowExW(0, wc.lpszClassName, APP_TITLE, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, ww, wh, 0, 0, inst, 0);
    if (!hwnd) return 1;
    diag_set_window(hwnd);
    video_init();
    if (C.renderer == 0) {
        d3d_ok = d3d_init(hwnd);
        if (d3d_ok) { fx_ok = 1; logw("direct3d 11 ok, %s", d3d_info()); }
        else logw("direct3d 11 failed: %s", d3d_error());
    }
    gl_ok = !d3d_ok && C.renderer != 2 && gl_init();
    if (gl_ok) {
        fx_ok = glfx_init(gl_getproc);
        logw("shaders %s", fx_ok ? "ok" : glfx_error());
    }
    logw("window ok, opengl %d (%s), hd threads %d", gl_ok, gl_ok ? (const char *)glGetString(GL_VERSION) : "-", hd_threads);

    path_in_exe_dir(hd_dir, L"hdpack\\");
    { hd_io_t hio = { hd_io_write, hd_io_read, hd_io_list, hd_io_log }; hd_set_io(&hio); }
    hd_load();

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
    emu_set_audio_tone(C.tone);
    emu_set_cutscene_detect(C.cine);
    emu_set_conveniences(C.encounters, C.exp_mul, C.mes_mul);
    stats_load();
    best_load();
    ach_load();
    fog_load(); emu_set_fog(C.fog); world_load();
    input_init();
    sram_read();
    emu_reset();
    apply_video();
    rw_init((size_t)48 << 20);
    cfg_save();
    launcher_prepare();
    rw_clear();
    (void)first_run;

    pace_reset();
    int sram_pending = 0;
    uint64_t last_autosave = now_ms();
    while (!quit) {
        diag_heartbeat(frame_no);
        MSG msg;
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) quit = 1;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) break;
        uint64_t t = now_ms();

        if (world_open && !launcher_open && !menu_open) {   /* the world map screen pauses the game */
            poll_pads();
            if (ppressed(A_MENU) || ppressed(A_B)) world_open = 0;
            present();
            Sleep(16);
            pace_reset();
            continue;
        }
        if (launcher_open || menu_open) {
            poll_pads();
            if (menu_open) menu_pad(); else launcher_pad();
            present();
            Sleep(16);
            pace_reset();
            continue;
        }

        poll_pads();
        if (ppressed(A_MENU)) { menu_set_open(1); continue; }
        if (ppressed(A_QSAVE)) state_save(C.slot, 0);
        if (ppressed(A_QLOAD)) state_load(C.slot);
        int fast = act_down[A_TURBO];
        int rewinding = C.rewind && act_down[A_REWIND];

        /* pacing by the clock (turbo and battle speed-up run several emulated
         * frames per displayed one; only the first one's sound is played) */
        if (!pace_due(1)) { Sleep(1); continue; }

        if (rewinding) {
            /* two steps back per displayed frame; no sound while rewinding */
            if (rw_pop()) rw_pop(); else set_status(L"дальше назад нельзя");
            emu_set_pad(0, 0);
            emu_run_frame();                        /* draws the picture of that moment */
            int n; while ((n = emu_audio(snd, 4096)) > 0) {}
            present();
            continue;
        }

        game_frames(fast);

        if (emu_sram_dirty()) sram_pending = 1;
        if (sram_pending && frame_no % 120 == 0) { sram_write(); sram_pending = 0; stats_save(); }
        if (frame_no % 1800 == 0) { best_save(); fog_save(); world_save(); }
        if (frame_no % 30 == 0) ach_check();
        if (C.autosave_min && t - last_autosave > (uint64_t)C.autosave_min * 60000 && emu_on_field() && !emu_in_battle()) {
            state_save(0, 0);
            last_autosave = t;
        }
    }
    sram_write();
    if (C.autosave_min && !launcher_open) state_save(0, 1);   /* never over the real autosave from the launcher */
    stats_save();
    best_save();
    fog_save();
    world_save();
    cfg_save();
    audio_close();
    DeleteFileW(marker);
    logw("clean exit");
    timeEndPeriod(1);
    DestroyWindow(hwnd);
    return 0;
}

void Start(void)
{
    ExitProcess((UINT)run());
}
