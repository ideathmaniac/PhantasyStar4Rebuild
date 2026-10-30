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
#include "../src/portraits.h"
#include "../src/music.h"
#include "../src/png.h"
#include "ui_soft.h"
#include "icons.h"

int _fltused = 0x9875;

#define APP_VERSION_W L"v0.48"
#define APP_TITLE L"Phantasy Star IV: The End of the Millennium"
#define GAME_NAME L"Phantasy Star IV: The End of the Millennium"

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
    int32_t party_hud;                     /* added in 0.39: party portraits with HP / TP at the top left */
    int32_t fx_shadow, fx_glow, fx_look;   /* added in 0.41: shadows under sprites, glow (0 off, 1 soft, 2 strong); colours 0-3 */
    int32_t quickbar;                      /* added in 0.42: quick access buttons under the party panel */
    int32_t show_fps;                      /* added in 0.44: frame rate and where the time goes */
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
#define CFG_V17_SIZE (offsetof(cfg_t, party_hud))
#define CFG_V18_SIZE (offsetof(cfg_t, fx_shadow))
#define CFG_V19_SIZE (offsetof(cfg_t, quickbar))
#define CFG_V20_SIZE (offsetof(cfg_t, show_fps))
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
    C.encounters = 0; C.exp_mul = 1; C.mes_mul = 1; C.tips = 1; C.shop_help = 1; C.enemy_hp = 1; C.text_speed = 1; C.minimap = 1; C.fog = 1; C.fast_walk = 1; C.party_hud = 1; C.fx_shadow = 1; C.fx_glow = 1; C.fx_look = 0; C.quickbar = 1;
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
static wchar_t status_msg[400];
static uint64_t status_until, frame_no;
static int64_t qpc_freq;
static int ws_ext_for_mode[3] = { 0, 32, 53 };

static uint64_t now_ms(void) { int64_t t; QueryPerformanceCounter(&t); return (uint64_t)(t * 1000 / qpc_freq); }
static void path_in_exe_dir(wchar_t *out, const wchar_t *name) { wcscpy(out, exe_dir); wcscat(out, name); }
static void set_status(const wchar_t *msg) { wcsncpy(status_msg, msg, 399); status_msg[399] = 0; status_until = now_ms() + 2500; }

/* log.txt next to the exe: what happened, for bug reports */
static FILE *g_logf;
static void logw(const char *fmt, ...)
{
    if (!g_logf) { wchar_t p[700]; wcscpy(p, exe_dir); wcscat(p, L"log.txt"); g_logf = _wfopen(p, L"w"); }
    if (!g_logf) return;
    va_list a; va_start(a, fmt);
    char b[512]; _vsnprintf(b, sizeof b - 1, fmt, a); b[sizeof b - 1] = 0;
    va_end(a);
    fprintf(g_logf, "[%lu] %s\n", (unsigned long)(frame_no), b);
    fflush(g_logf);
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
    if ((n == sizeof C || n == CFG_V2_SIZE || n == CFG_V3_SIZE || n == CFG_V4_SIZE || n == CFG_V5_SIZE || n == CFG_V6_SIZE || n == CFG_V7_SIZE || n == CFG_V8_SIZE || n == CFG_V9_SIZE || n == CFG_V10_SIZE || n == CFG_V11_SIZE || n == CFG_V12_SIZE || n == CFG_V13_SIZE || n == CFG_V14_SIZE || n == CFG_V15_SIZE || n == CFG_V16_SIZE || n == CFG_V17_SIZE || n == CFG_V18_SIZE || n == CFG_V19_SIZE || n == CFG_V20_SIZE) && ((cfg_t *)b)->magic == CFG_MAGIC && ((cfg_t *)b)->version == CFG_VERSION) {
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
        if (n < CFG_V17_SIZE) { C.fog = 1; C.fast_walk = 1; }
        if (n < CFG_V18_SIZE) C.party_hud = 1;
        if (n < CFG_V19_SIZE) { C.fx_shadow = 1; C.fx_glow = 1; C.fx_look = 0; }
        if (n < CFG_V20_SIZE) C.quickbar = 1;
        if (n < sizeof C) C.show_fps = 0;
    } else if (n >= 20 + 1200 && *(uint32_t *)b == 0x31475043u)   /* version 1 file: keep the ROM path */
        memcpy(C.rom, b + 20, 1200);
    if (C.fx_mode < 0 || C.fx_mode >= FX_COUNT) C.fx_mode = FX_PIXELS;
    if (C.renderer < 0 || C.renderer > 2) C.renderer = 0;
    if (C.tone < 0 || C.tone > 2) C.tone = 0;
    if (C.fx_shadow < 0 || C.fx_shadow > 2) C.fx_shadow = 1;
    if (C.fx_glow < 0 || C.fx_glow > 2) C.fx_glow = 1;
    if (C.fx_look < 0 || C.fx_look > 3) C.fx_look = 0;
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
/* shops the party has opened: the game's shop number -> the map it is on
 * (saves\PS4_shops.txt, "shop map" per line), for the equipment advice */
static int shop_map[128];
static void shops_path(wchar_t *p) { path_in_exe_dir(p, L"saves\\PS4_shops.txt"); }
static void shops_load(void)
{
    wchar_t p[700]; shops_path(p);
    size_t n = 0; uint8_t *b = read_file(p, &n);
    if (!b) return;
    char *t = realloc(b, n + 1); t[n] = 0;
    for (char *l = t; l && *l; ) {
        unsigned sh, mp;
        if (sscanf(l, "%u %u", &sh, &mp) == 2 && sh < 128) shop_map[sh] = (int)mp + 1;
        l = strchr(l, '\n'); if (l) l++;
    }
    free(t);
}
static void shops_save(void)
{
    char b[128 * 16]; int k = 0;
    for (int i = 0; i < 128; i++) if (shop_map[i]) k += _snprintf(b + k, sizeof b - k, "%d %d\n", i, shop_map[i] - 1);
    if (!k) return;
    saves_dir();
    wchar_t p[700]; shops_path(p);
    write_file(p, b, (size_t)k);
}
static void shops_track(void)
{
    if (!emu_known_rom()) return;
    int sh = shop_open_id();
    if (sh < 0 || sh >= 128) return;
    const uint8_t *r = emu_work_ram();
    int map = r[0xEC28] << 8 | r[0xEC29];
    if (shop_map[sh] != map + 1) { shop_map[sh] = map + 1; shops_save(); }
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
    if (music_playing()) {   /* the launcher's music fading out over the game */
        static int16_t mus[2 * 4200];
        music_mix(mus, m, 48000, 85);
        for (int i = 0; i < m * 2; i++) { int v = buf[i] + mus[i]; buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
    }
    audio_submit(buf, m);
}
/* the launcher's music: menu_music.ogg (or .wav) next to the program */
static void music_open(void)
{
    static const wchar_t *names[2] = { L"menu_music.ogg", L"menu_music.wav" };
    for (int i = 0; i < 2 && !music_loaded(); i++) {
        wchar_t p[700]; path_in_exe_dir(p, names[i]);
        size_t n = 0; uint8_t *d = read_file(p, &n);
        if (d) { if (music_load(d, n)) logw("menu music: %ls", names[i]); free(d); }
    }
}
/* while the game is not running (launcher), only the music goes out */
static void music_feed(void)
{
    if (!audio_ok || !music_playing()) return;
    int q = audio_queued();
    static int16_t mus[2 * 1024];
    while (q < A_TARGET * 2) { music_mix(mus, 1024, 48000, 85); audio_submit(mus, 1024); q += 1024; }
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

/* one of the icons of icons.h, size x size pixels (the 12 x 12 mask scaled) */
static void draw_icon(int x0, int y0, int size, int id, int dim)
{
    if (id <= IC_NONE || id >= IC_COUNT || !ICONS[id].rows[0] || size < 6) return;
    uint32_t c = ICONS[id].color, *dst = (uint32_t *)back_bits;
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    if (dim) { r = r * 3 / 5; g = g * 3 / 5; b = b * 3 / 5; }
    uint32_t ink = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    uint32_t light = (uint32_t)(r * 3 / 5 + 38) << 16 | (uint32_t)(g * 3 / 5 + 38) << 8 | (uint32_t)(b * 3 / 5 + 38);
    for (int y = 0; y < size; y++) {
        int yy = y0 + y; if (yy < 0 || yy >= back_h) continue;
        const char *row = ICONS[id].rows[y * 12 / size];
        for (int x = 0; x < size; x++) {
            int xx = x0 + x; if (xx < 0 || xx >= back_w) continue;
            char ch = row[x * 12 / size];
            if (ch == '#') dst[(size_t)yy * back_w + xx] = ink;
            else if (ch == '+') dst[(size_t)yy * back_w + xx] = light;
        }
    }
}

static void draw_menu(void);
static void draw_status(void);
static int menu_rect[4], status_rect[4], tip_rect[4], ach_rect[4], hp_rect[4], mm_rect[4];   /* x, y, w, h of overlay boxes drawn this frame */
static int party_rect[5][4], quick_rect[4], qtip_rect[4], fps_rect[4], turn_rect[4], hptip_rect[4], quick_bottom;
static void draw_fps(void);
static int g_mouse_x = -1, g_mouse_y = -1;
static int party_col_right, party_col_bottom;   /* the party panel and quick bar of the last frame (0 when not shown) */
/* boxes at the top left move right of the party panel when they would cover it */
static void avoid_party(int *bx, int *bw, int by, int bh, int right_limit)
{
    if (!party_col_right || by > party_col_bottom || by + bh < 0) return;
    if (*bx >= party_col_right) return;
    int x = party_col_right, w = *bw;
    if (x + w > right_limit) w = right_limit - x;
    if (w < *bw / 2) return;   /* no room: rather cover it than squeeze the text */
    *bx = x; *bw = w;
}
/* every overlay box of this frame, for copying over the picture */
static int overlay_rects(int **r)
{
    int n = 0;
    r[n++] = menu_rect; r[n++] = status_rect; r[n++] = tip_rect; r[n++] = ach_rect; r[n++] = hp_rect; r[n++] = mm_rect;
    for (int k = 0; k < 5; k++) r[n++] = party_rect[k];
    r[n++] = quick_rect; r[n++] = qtip_rect; r[n++] = fps_rect; r[n++] = turn_rect; r[n++] = hptip_rect;
    return n;
}
static void draw_party(int px, int py, int pw, int ph);
static void draw_quickbar(int py, int ph);
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
    party_col_right = party_col_bottom = 0;
    for (int k = 0; k < 5; k++) { if (party_rect[k][2]) { int r = party_rect[k][0] + party_rect[k][2] + font_h / 2, b = party_rect[k][1] + party_rect[k][3];
        if (r > party_col_right) party_col_right = r; if (b > party_col_bottom) party_col_bottom = b; } }
    if (quick_rect[2]) { int r = quick_rect[0] + quick_rect[2] + font_h / 2, b = quick_rect[1] + quick_rect[3];
        if (r > party_col_right) party_col_right = r; if (b > party_col_bottom) party_col_bottom = b; }
    for (int k = 0; k < 5; k++) party_rect[k][2] = 0;
    if (turn_rect[2]) { int r = turn_rect[0] + turn_rect[2] + font_h / 2, b = turn_rect[1] + turn_rect[3];
        if (r > party_col_right) party_col_right = r; if (b > party_col_bottom) party_col_bottom = b; }
    quick_rect[2] = 0; qtip_rect[2] = 0; quick_bottom = 0; fps_rect[2] = 0; turn_rect[2] = 0; hptip_rect[2] = 0;
    if (launcher_open) draw_launcher(cw, ch);
    else if (world_open && !menu_open) draw_world(cw, ch);
    else if (!menu_open) {
        if (C.tips) draw_tip(x, y, w, h);
        if (C.shop_help && !tip_rect[2]) draw_shop(x, y, w, h);
        if (C.enemy_hp && !tip_rect[2]) draw_enemy_hp(x, y, w, h);   /* the technique panel uses the top in battle */
        if (C.minimap && !tip_rect[2]) draw_minimap(x, y, w, h);
        if (C.party_hud) draw_party(x, y, w, h);
        if (C.quickbar) draw_quickbar(y, h);
    }
    if (!launcher_open && !world_open) draw_ach_toast();
    if (!launcher_open) draw_fps();
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
    /* the effects (shadows, glow, colours) need to know which layer drew
     * each pixel: it travels to the shaders in the alpha channel */
    {
        static const float sh[3] = { 0, 0.85f, 1.4f }, gl[3] = { 0, 0.35f, 0.7f };
        int on = fx_ok && (C.fx_shadow || C.fx_glow || C.fx_look);
        if (d3d_ok) d3d_set_effects(on ? sh[C.fx_shadow] : 0, on ? gl[C.fx_glow] : 0, on ? C.fx_look : 0);
        else if (gl_ok) glfx_set_effects(on ? sh[C.fx_shadow] : 0, on ? gl[C.fx_glow] : 0, on ? C.fx_look : 0);
        if (on && (C.fx_shadow || C.fx_glow) && PW == W && PH == H && W <= MD_W_MAX) {
            static uint32_t withlayers[MD_W_MAX * 224];
            static const uint32_t code[8] = { 0, 0x55000000u, 0xAA000000u, 0xFF000000u, 0x88000000u, 0, 0, 0 };
            const uint8_t *lay = emu_layers();
            for (int yy = 0; yy < H; yy++)
                for (int xx = 0; xx < W; xx++)
                    withlayers[yy * MD_W_MAX + xx] = (pic[(size_t)yy * ppitch + xx] & 0xFFFFFFu) | code[lay[yy * MD_W_MAX + xx] & 7];
            pic = withlayers; ppitch = MD_W_MAX;
        }
    }
    if (d3d_ok) {
        d3d_frame(pic, PW, PH, ppitch, x, y, w, h, cw, ch, cine_on ? FX_CINE : C.fx_mode, C.smooth, cine_on || C.fx_dither, C.backdrop);
        int *rects[32]; int nrects = overlay_rects(rects);
        if (cw <= back_w && ch <= back_h)
            for (int k = 0; k < nrects; k++) {
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
        int *rects[32]; int nrects = overlay_rects(rects);
        /* an overlay box is uploaded again only when it changed: the party
         * panel and the quick bar are on screen all the time, and on phones
         * every texture upload can stall the GPU */
        static int last_box[16][4]; static uint32_t last_hash[16]; static int last_tw, last_th;
        int tw = gl_npot ? cw : pot(cw), th = gl_npot ? ch : pot(ch);
        int same_set = tw == last_tw && th == last_th;
        for (int k = 0; k < nrects && same_set; k++) if (memcmp(last_box[k], rects[k], sizeof last_box[k])) same_set = 0;
        for (int k = 0; k < nrects; k++) {
            int *r = rects[k];
            memcpy(last_box[k], r, sizeof last_box[k]);
            /* keep the copied box inside the back buffer */
            if (r[0] < 0) { r[2] += r[0]; r[0] = 0; }
            if (r[1] < 0) { r[3] += r[1]; r[1] = 0; }
            if (r[0] + r[2] > back_w) r[2] = back_w - r[0];
            if (r[1] + r[3] > back_h) r[3] = back_h - r[1];
            if (r[2] <= 0 || r[3] <= 0 || cw > back_w || ch > back_h) continue;
            GdiFlush();
            if (!tex_ovl || tw != ovl_tw || th != ovl_th) { gl_new_tex(&tex_ovl, tw, th); ovl_tw = tw; ovl_th = th; same_set = 0; }
            uint32_t hsh = 2166136261u;
            for (int yy = 0; yy < r[3]; yy++) {
                const uint32_t *row = (const uint32_t *)back_bits + (size_t)(r[1] + yy) * back_w + r[0];
                for (int xx = 0; xx < r[2]; xx++) hsh = (hsh ^ row[xx]) * 16777619u;
            }
            if (!same_set || hsh != last_hash[k]) {
                glBindTexture(GL_TEXTURE_2D, tex_ovl);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, back_w);
                glTexSubImage2D(GL_TEXTURE_2D, 0, r[0], r[1], r[2], r[3], GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                                (const uint32_t *)back_bits + (size_t)r[1] * back_w + r[0]);
                last_hash[k] = hsh;
            }
            gl_quad(tex_ovl, ovl_tw, ovl_th, r[0], r[1], r[2], r[3], r[0], r[1], r[2], r[3], 0);
        }
        last_tw = tw; last_th = th;
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
    int *rects[32]; int nrects = overlay_rects(rects);
    for (int k = 0; k < nrects; k++) if (rects[k][2] > 0) BitBlt(dc, rects[k][0], rects[k][1], rects[k][2], rects[k][3], back_dc, rects[k][0], rects[k][1], SRCCOPY);
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
    avoid_party(&bx, &bw, by, bh, px + pw);
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
    avoid_party(&bx, &bw, by, bh, px + pw);
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

/* world map screen (key N, the quick button): the whole planet (from the
 * ROM; parts not visited yet are darker), the towns with their names, the
 * party and the goal of "where next". Zoom: + / -, the wheel or the buttons;
 * a click or tap centres the view there, the arrows move it */
static const wchar_t *PLACE[52] = {
    L"Пиата", L"Миле", L"Зема", L"Долина жизни", L"Био-завод", L"Молкум", L"Круп", L"Пещера", L"Тоное", L"Подвал",
    L"Налия", L"Руины", L"Аейдо", L"Пещера", L"Кадари", L"Замок Зио", L"Нервус", L"Гараж", L"Монсен", L"Тектоническая система",
    L"Терми", L"Башня Ладей", L"Зелан", L"Куран", L"Храм Раджа", L"Рион", L"Тайлер", L"Ангар", L"Зоша", L"",
    L"Система управления погодой", L"Рашель", L"Мииса", L"Лес", L"Особняк целителей", L"Джут", L"Оружейный завод", L"Храм Гумбеос", L"Воздушный замок", L"Башня Гаруба",
    L"Пещера", L"Храм воина", L"Джуза", L"Торинко", L"Оружейный завод", L"Башня тишины", L"Башня храбрости", L"Башня силы", L"Пещера", L"Мост тьмы",
    L"Космопорт", L"Башня гнева" };
static const wchar_t *place_name(int i) { return i >= 0 && i < 52 ? PLACE[i] : L""; }
static int story_goal(void);   /* the place "where next" points to, -1 if none */
static int world_zoom = 1, world_cx = -1, world_cy = -1, world_planet = -1;
static int wbtn[4][4];          /* the zoom in / out / to the party / close buttons */
static int world_view[6];       /* x0, y0, dw, dh of the map on screen; source x, y */
static double world_scale;
static void world_reset_view(void) { world_zoom = 1; world_cx = world_cy = -1; }
static void world_open_map(void) { world_open = 1; world_reset_view(); emu_world_pick(-1); }
static int ptab[3][4];   /* the planet tabs on the map screen */
static void world_pick(int planet) { if (emu_world_known(planet)) { emu_world_pick(planet); world_reset_view(); } }
static void world_zoom_by(int d, int at_x, int at_y)
{
    int z = world_zoom;
    world_zoom = d > 0 ? (z >= 8 ? 8 : z * 2) : (z <= 1 ? 1 : z / 2);
    if (at_x >= 0 && world_scale > 0 && world_zoom != z) {   /* keep the point under the cursor in place */
        double mx = world_view[4] + (at_x - world_view[0]) / world_scale, my = world_view[5] + (at_y - world_view[1]) / world_scale;
        world_cx = (int)mx; world_cy = (int)my;
    }
}
static void world_click(int x, int y)
{
    for (int k = 0; k < 3; k++)
        if (ptab[k][2] && x >= ptab[k][0] && x < ptab[k][0] + ptab[k][2] && y >= ptab[k][1] && y < ptab[k][1] + ptab[k][3]) { world_pick(k); return; }
    for (int k = 0; k < 4; k++)
        if (x >= wbtn[k][0] && x < wbtn[k][0] + wbtn[k][2] && y >= wbtn[k][1] && y < wbtn[k][1] + wbtn[k][3]) {
            if (k == 0) world_zoom_by(1, -1, -1); else if (k == 1) world_zoom_by(-1, -1, -1); else if (k == 2) world_reset_view(); else world_open = 0;
            return;
        }
    if (world_scale > 0 && x >= world_view[0] && y >= world_view[1] && x < world_view[0] + world_view[2] && y < world_view[1] + world_view[3]) {
        world_cx = world_view[4] + (int)((x - world_view[0]) / world_scale);
        world_cy = world_view[5] + (int)((y - world_view[1]) / world_scale);
        if (world_zoom == 1) world_zoom = 2;
    }
}
static void world_pan(int dx, int dy)
{
    if (world_scale <= 0) return;
    int step = (int)(world_view[2] / world_scale / 6) + 1;
    if (world_cx < 0) { world_cx = world_view[4] + (int)(world_view[2] / world_scale / 2); world_cy = world_view[5] + (int)(world_view[3] / world_scale / 2); }
    world_cx += dx * step; world_cy += dy * step;
}
static void draw_world(int cw, int ch)
{
    static uint32_t img[512 * 1024];
    int w, h, mx, my;
    int planet = emu_world_image(img, 512, 1024, &w, &h, &mx, &my);
    if (!planet) { world_open = 0; set_status(L"карта мира появится, когда отряд выйдет на карту планеты"); return; }
    planet--;
    if (planet != world_planet) { world_planet = planet; world_reset_view(); }
    fill(0, 0, cw, ch, RGBC(8, 8, 16));
    int top = font_h * 2, bot = font_h * 5 / 2, side = font_h;
    int aw = cw - side * 2, ah = ch - top - bot;
    if (aw < 50 || ah < 50) return;
    double fit = (double)aw / w < (double)ah / h ? (double)aw / w : (double)ah / h;
    if (fit >= 2) fit = (int)fit;   /* whole pixels when it costs little room */
    double sc = fit * world_zoom;
    /* the part shown: centred on the chosen point, else on the party */
    int vw = (int)(aw / sc), vh = (int)(ah / sc);
    if (vw > w) vw = w; if (vh > h) vh = h;
    int cx = world_cx >= 0 ? world_cx : mx >= 0 ? mx : w / 2, cy = world_cy >= 0 ? world_cy : my >= 0 ? my : h / 2;
    int sx = cx - vw / 2, sy = cy - vh / 2;
    if (sx < 0) sx = 0; if (sy < 0) sy = 0; if (sx > w - vw) sx = w - vw; if (sy > h - vh) sy = h - vh;
    if (world_cx >= 0) { world_cx = sx + vw / 2; world_cy = sy + vh / 2; }   /* kept inside after panning */
    int dw = (int)(vw * sc), dh = (int)(vh * sc), x0 = side + (aw - dw) / 2, y0 = top + (ah - dh) / 2;
    {   /* the part shown, copied out (a source rectangle of a top-down bitmap is read differently by systems) */
        static uint32_t part[512 * 1024];
        for (int yy = 0; yy < vh; yy++) memcpy(part + (size_t)yy * vw, img + (size_t)(sy + yy) * w + sx, (size_t)vw * 4);
        BITMAPINFO bi; memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = vw; bi.bmiHeader.biHeight = -vh;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(back_dc, sc >= 1 ? COLORONCOLOR : HALFTONE); SetBrushOrgEx(back_dc, 0, 0, 0);
        StretchDIBits(back_dc, x0, y0, dw, dh, 0, 0, vw, vh, part, &bi, DIB_RGB_COLORS, SRCCOPY);
    }
    world_view[0] = x0; world_view[1] = y0; world_view[2] = dw; world_view[3] = dh; world_view[4] = sx; world_view[5] = sy;
    world_scale = sc;
#define WX(px) (x0 + (int)(((px) - sx) * sc))
#define WY(py) (y0 + (int)(((py) - sy) * sc))
#define WIN(px, py) ((px) >= sx && (px) < sx + vw && (py) >= sy && (py) < sy + vh)
    /* towns */
    int place[48], tx[48], ty[48], nt = emu_world_towns(planet, place, tx, ty, 48), goal = story_goal();
    int ms = font_h / 2 > 6 ? font_h / 2 : 6;
    int lab[48][4], nl = 0;   /* names already written: the next ones go where they do not overlap */
    for (int pass = 0; pass < 2; pass++)   /* the goal's name first */
    for (int i = 0; i < nt; i++) {
        if ((place[i] == goal) != (pass == 0) || !WIN(tx[i], ty[i])) continue;
        int x = WX(tx[i]), y = WY(ty[i]);
        fill(x - ms / 2 - 1, y - ms / 2 - 1, ms + 2, ms + 2, RGBC(0, 0, 0));
        fill(x - ms / 2, y - ms / 2, ms, ms, place[i] == goal ? RGBC(255, 210, 80) : RGBC(240, 240, 255));
        const wchar_t *n = place_name(place[i]);
        int tw = text_w(n, font_small) + 6, th = font_h, gx = place[i] == goal ? ms + 8 : 0, cand[4][2] = { { x + ms + gx, y - th / 2 }, { x - ms - tw, y - th / 2 }, { x - tw / 2, y + ms }, { x - tw / 2, y - ms - th } };
        for (int c = 0; c < 4; c++) {
            int lx = cand[c][0], ly = cand[c][1], ok = lx >= x0 && lx + tw <= x0 + dw && ly >= y0 && ly + th <= y0 + dh;
            for (int k = 0; k < nl && ok; k++)
                if (lx < lab[k][0] + lab[k][2] && lx + tw > lab[k][0] && ly < lab[k][1] + lab[k][3] && ly + th > lab[k][1]) ok = 0;
            if (!ok) continue;
            fill(lx, ly, tw, th, RGBC(10, 12, 28));
            draw_text(lx + 3, ly, n, place[i] == goal ? RGBC(255, 220, 120) : RGBC(225, 230, 255), font_small);
            if (nl < 48) { lab[nl][0] = lx; lab[nl][1] = ly; lab[nl][2] = tw; lab[nl][3] = th; nl++; }
            break;
        }
    }
    /* the goal of "where next": a pulsing ring */
    for (int i = 0; i < nt; i++) if (place[i] == goal && WIN(tx[i], ty[i])) {
        int x = WX(tx[i]), y = WY(ty[i]), r = ms + 3 + (int)((now_ms() / 120) % 6);
        for (int k = 0; k < 2; k++) {
            fill(x - r - k, y - r - k, 2 * (r + k) + 1, 1, RGBC(255, 200, 60)); fill(x - r - k, y + r + k, 2 * (r + k) + 1, 1, RGBC(255, 200, 60));
            fill(x - r - k, y - r - k, 1, 2 * (r + k) + 1, RGBC(255, 200, 60)); fill(x + r + k, y - r - k, 1, 2 * (r + k) + 1, RGBC(255, 200, 60));
        }
    }
    /* the party */
    if (mx >= 0 && WIN(mx, my)) {
        int on = (now_ms() / 300) & 1, m = ms + 2;
        int px = WX(mx), py = WY(my);
        fill(px - m / 2 - 1, py - m / 2 - 1, m + 2, m + 2, RGBC(0, 0, 0));
        fill(px - m / 2, py - m / 2, m, m, on ? RGBC(255, 60, 60) : RGBC(255, 140, 120));
    }
#undef WX
#undef WY
#undef WIN
    static const wchar_t *pn[3] = { L"Мотавия", L"Дезолис", L"Рикрос" };
    {   /* the title, then a tab for each planet seen (click, or keys 1-3) */
        HGDIOBJ fb = font_big ? font_big : font_small;
        int x = font_h, y = font_h / 2;
        draw_text(x, y, L"Карта мира:", RGBC(255, 240, 180), fb);
        x += text_w(L"Карта мира:", fb) + font_h / 2;
        for (int k = 0; k < 3; k++) {
            ptab[k][2] = 0;
            if (!emu_world_known(k)) continue;
            int tw = text_w(pn[k], fb) + font_h, th = font_h * 5 / 4 + 2, on = k == planet;
            int hov = g_mouse_x >= x && g_mouse_x < x + tw && g_mouse_y >= y - 2 && g_mouse_y < y - 2 + th;
            fill(x, y - 2, tw, th, on ? RGBC(214, 176, 92) : hov ? RGBC(70, 96, 170) : RGBC(28, 34, 70));
            draw_text(x + font_h / 2, y, pn[k], on ? RGBC(20, 20, 30) : RGBC(225, 230, 255), fb);
            ptab[k][0] = x; ptab[k][1] = y - 2; ptab[k][2] = tw; ptab[k][3] = th;
            x += tw + font_h / 3;
        }
        if (mx < 0) draw_text(x + font_h / 2, y + 2, L"отряд не на этой карте", RGBC(170, 180, 220), font_small);
    }
    {   /* the legend and the controls */
        int lx = font_h, ly = ch - bot + font_h / 3, q = font_h * 2 / 3;
        static const struct { DWORD c; const wchar_t *t; } L[3] = {
            { RGBC(255, 60, 60), L"отряд" }, { RGBC(255, 210, 80), L"цель «Куда идти»" }, { RGBC(240, 240, 255), L"город" } };
        for (int k = 0; k < 3; k++) {
            fill(lx, ly + (font_h - q) / 2, q, q, L[k].c);
            draw_text(lx + q + font_h / 3, ly, L[k].t, RGBC(200, 205, 230), font_small);
            lx += q + font_h / 3 + text_w(L[k].t, font_small) + font_h;
        }
        draw_text(lx, ly, L"тёмное — ещё не пройдено", RGBC(150, 156, 190), font_small);
        draw_text(font_h, ly + font_h, L"Щелчок — приблизить · колесо, +/− — масштаб · стрелки — сдвиг · 1–3 — планета · Esc — закрыть",
                  RGBC(150, 156, 190), font_small);
    }
    /* buttons: + − and back to the party */
    static const wchar_t *bl[4] = { L"+", L"−", L"◎", L"✕" };
    int bs = font_h * 3 / 2 + 4;
    for (int k = 0; k < 4; k++) {
        int bx = cw - side - (4 - k) * (bs + 6) - (k == 3 ? 0 : font_h / 2), by = font_h / 4;
        int hov = g_mouse_x >= bx && g_mouse_x < bx + bs && g_mouse_y >= by && g_mouse_y < by + bs;
        fill(bx, by, bs, bs, hov ? (k == 3 ? RGBC(190, 70, 60) : RGBC(70, 96, 170)) : (k == 3 ? RGBC(110, 40, 40) : RGBC(28, 34, 70)));
        fill(bx, by, bs, 1, RGBC(110, 120, 170));
        draw_text(bx + (bs - text_w(bl[k], font_big ? font_big : font_small)) / 2, by + (bs - font_h * 5 / 4) / 2, bl[k], RGBC(240, 240, 255), font_big ? font_big : font_small);
        wbtn[k][0] = bx; wbtn[k][1] = by; wbtn[k][2] = bs; wbtn[k][3] = bs;
    }
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
/* the party panel, top left: for each member a portrait (from the ROM,
 * src/portraits.c) with the level, then the HP bar with numbers and a thin
 * TP bar; the leader's frame is gold, a fallen member is greyed out */
static void scale_portrait(const uint32_t *src, uint32_t *dst, int s, int grey);
/* scaled portraits are kept (a few sizes and the greyed versions), so the
 * panel costs a copy per frame */
static void put_portrait(const uint32_t *src, int x0, int y0, int s, int grey)
{
    static struct { const uint32_t *src; int s, grey; uint32_t *pix; } cache[16];
    static int next;
    uint32_t *pix = 0;
    for (int i = 0; i < 16; i++) if (cache[i].pix && cache[i].src == src && cache[i].s == s && cache[i].grey == grey) pix = cache[i].pix;
    if (!pix) {
        int i = next++ & 15;
        free(cache[i].pix);
        cache[i].pix = pix = malloc((size_t)s * s * 4);
        if (!pix) return;
        cache[i].src = src; cache[i].s = s; cache[i].grey = grey;
        scale_portrait(src, pix, s, grey);
    }
    uint32_t *dst = (uint32_t *)back_bits;
    for (int y = 0; y < s; y++) {
        int yy = y0 + y; if (yy < 0 || yy >= back_h) continue;
        int x = 0, w = s;
        if (x0 < 0) { x = -x0; } if (x0 + w > back_w) w = back_w - x0;
        if (w > x) memcpy(dst + (size_t)yy * back_w + x0 + x, pix + (size_t)y * s + x, (size_t)(w - x) * 4);
    }
}
static void scale_portrait(const uint32_t *src, uint32_t *out, int s, int grey)
{
    int x0 = 0, y0 = 0;
    for (int y = 0; y < s; y++) {
        int yy = y0 + y;
        float fy = (y + 0.5f) * PORTRAIT_W / s - 0.5f; int iy = (int)fy; float ay = fy - iy;
        if (fy < 0) { iy = 0; ay = 0; }
        if (iy >= PORTRAIT_W - 1) { iy = PORTRAIT_W - 2; ay = 1; }
        for (int x = 0; x < s; x++) {
            int xx = x0 + x;
            float fx = (x + 0.5f) * PORTRAIT_W / s - 0.5f; int ix = (int)fx; float ax = fx - ix;
            if (fx < 0) { ix = 0; ax = 0; }
            if (ix >= PORTRAIT_W - 1) { ix = PORTRAIT_W - 2; ax = 1; }
            /* sharpened bilinear: the pixel art stays crisp, every pixel the same size */
            ax = ax < 0.3f ? 0 : ax > 0.7f ? 1 : (ax - 0.3f) / 0.4f;
            ay = ay < 0.3f ? 0 : ay > 0.7f ? 1 : (ay - 0.3f) / 0.4f;
            const uint32_t *p = src + iy * PORTRAIT_W + ix;
            float c[3];
            for (int k = 0; k < 3; k++) {
                int sh = 16 - 8 * k;
                float a = ((p[0] >> sh) & 255) * (1 - ax) + ((p[1] >> sh) & 255) * ax;
                float b = ((p[PORTRAIT_W] >> sh) & 255) * (1 - ax) + ((p[PORTRAIT_W + 1] >> sh) & 255) * ax;
                c[k] = a * (1 - ay) + b * ay;
            }
            if (grey) { float l = (c[0] * 0.3f + c[1] * 0.59f + c[2] * 0.11f) * 0.55f; c[0] = c[1] = c[2] = l; }
            out[(size_t)yy * s + xx] = (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2];
        }
    }
}
static void draw_turns(int py, int ph);
static void draw_party(int px, int py, int pw, int ph)
{
    (void)px; (void)pw; (void)ph;
    int m = emu_game_mode_top();
    if (m == 0x14) { draw_turns(py, ph); return; }   /* in battle: the order of turns instead */
    if (m != 0x0C) return;
    const uint8_t *r = emu_work_ram();
    int s = back_h * 10 / 100; if (s < 52) s = 52; if (s > 128) s = 128;
    int sh = font_h * 4 / 5 + 2, tp = font_h / 4 > 3 ? font_h / 4 : 3, gap = font_h / 2;
    int x0 = font_h / 2 + 3, y = (py > font_h / 2 ? py : font_h / 2) + 3;
    for (int k = 0; k < 5; k++) {
        int rec = r[0xF40A + k];
        if (rec == 0xFF || rec > 10) break;
        const uint8_t *c = r + 0xF500 + rec * 0x80;
        int lv = c[8] << 8 | c[9], hp = (int16_t)(c[0x0E] << 8 | c[0x0F]), hpm = c[0x10] << 8 | c[0x11];
        int tpv = (int16_t)(c[0x12] << 8 | c[0x13]), tpm = c[0x14] << 8 | c[0x15];
        if (hpm <= 0 || hpm > 9999 || lv <= 0 || lv > 99) break;   /* not a real record yet */
        int dead = hp <= 0;
        const uint32_t *pic = portrait_pixels(rec);
        int bx = x0 - 3, by = y - 3, bw = s + 6, bhh = s + 6 + sh + tp + 5;
        fill(bx, by, bw, bhh, RGBC(14, 16, 30));
        fill(x0 - 2, y - 2, s + 4, s + 4, k == 0 ? RGBC(214, 176, 92) : RGBC(92, 104, 150));
        if (pic) put_portrait(pic, x0, y, s, dead);
        else {
            fill(x0, y, s, s, RGBC(40, 46, 80));
            wchar_t nm[16]; ui_ps4_name(c, nm, 16); nm[2] = 0;
            draw_text(x0 + s / 4, y + s / 3, nm, RGBC(230, 230, 255), font_big);
        }
        { /* the level: a badge at the bottom left of the portrait */
            wchar_t t[8]; _snwprintf(t, 7, L"%d", lv); t[7] = 0;
            int tw = text_w(t, font_small) + font_h / 2, th = font_h * 4 / 5 + 2;
            fill(x0, y + s - th, tw, th, RGBC(18, 20, 40));
            draw_text(x0 + font_h / 4, y + s - th, t, RGBC(255, 220, 130), font_small);
        }
        /* HP: the bar with the numbers on it */
        int hy = y + s + 3;
        fill(x0, hy, s, sh, RGBC(48, 20, 24));
        int hv = hp < 0 ? 0 : hp > hpm ? hpm : hp;
        DWORD hc = hv * 4 <= hpm ? RGBC(200, 58, 48) : hv * 2 <= hpm ? RGBC(206, 160, 52) : RGBC(62, 160, 78);
        fill(x0, hy, s * hv / hpm, sh, hc);
        {
            wchar_t t[24]; _snwprintf(t, 23, L"%d/%d", hv, hpm); t[23] = 0;
            if (text_w(t, font_small) > s - 2) { _snwprintf(t, 23, L"%d", hv); t[23] = 0; }
            draw_text(x0 + (s - text_w(t, font_small)) / 2, hy, t, RGBC(255, 255, 255), font_small);
        }
        /* TP: a thin bar */
        int ty = hy + sh + 2;
        fill(x0, ty, s, tp, RGBC(22, 28, 60));
        if (tpm > 0) { int tv = tpv < 0 ? 0 : tpv > tpm ? tpm : tpv; fill(x0, ty, s * tv / tpm, tp, RGBC(86, 150, 255)); }
        party_rect[k][0] = bx; party_rect[k][1] = by; party_rect[k][2] = bw; party_rect[k][3] = bhh;
        quick_bottom = by + bhh;
        y += bhh + gap;
        if (y + bhh > back_h) break;
    }
}

/* battle: the order of turns, top left, in place of the party panel.
 * The game's fighters are 64-byte entries at $FF4400 (1-5 the party in
 * order, 6-9 the monsters; +0 non-zero when present, +4 the address of the
 * character's or monster's record). When the orders are given ($0053F8) it
 * fills $FFEFB0 with (fighter, initiative) words, highest first, ending
 * with 0: initiative = the record's byte +$20 (agility) plus a random part
 * up to half the highest; who acts is $FF4140 (1 = the first). While the
 * game waits for orders ($FF4109 non-zero) that table is the last round's,
 * so the panel shows the likely order by agility instead. */
static int draw_thumb(int bx, int by, int bw, int bh, int id);
/* the table at $FFEFB0 is left from the last battle until the first round
 * is ordered: it counts once it changes (checked every frame) */
static int turns_fresh;
static void turns_track(void)
{
    static int inb; static uint8_t snap[40];
    const uint8_t *r = emu_work_ram();
    if (!emu_in_battle()) { inb = 0; turns_fresh = 0; return; }
    if (!inb) { inb = 1; memcpy(snap, r + 0xEFB0, 40); turns_fresh = 0; return; }
    if (!turns_fresh && memcmp(snap, r + 0xEFB0, 40)) turns_fresh = 1;
}
static void draw_turns(int py, int ph)
{
    const uint8_t *r = emu_work_ram();
    int ordering = r[0x4109] != 0 || !turns_fresh;
    int ids[9], n = 0, cur = -1;
    if (!ordering) {
        for (int k = 0; k < 9; k++) { int id = r[0xEFB0 + k * 4] << 8 | r[0xEFB1 + k * 4]; if (id < 1 || id > 9) break; ids[n++] = id; }
        cur = (r[0x4140] << 8 | r[0x4141]) - 1;
    }
    if (ordering || !n) {
        int ag[9]; n = 0;
        for (int i = 0; i < 9; i++) {
            const uint8_t *e = r + 0x4400 + i * 64;
            if (!(e[0] | e[1])) continue;
            int a = e[6] << 8 | e[7];
            if (r[(a + 22) & 0xFFFF] & 0x6E) continue;   /* down, asleep...: no turn */
            int v = r[(a + 32) & 0xFFFF], k = n++;
            while (k > 0 && ag[k - 1] < v) { ag[k] = ag[k - 1]; ids[k] = ids[k - 1]; k--; }
            ag[k] = v; ids[k] = i + 1;
        }
        cur = -1;
    }
    if (!n) return;
    int x0 = font_h / 2 + 3, y = (py > font_h / 2 ? py : font_h / 2) + 3;
    int hdr = font_h + 2, gap = font_h / 4 + 1, hb = font_h / 4 > 3 ? font_h / 4 : 3;
    int s = back_h * 8 / 100; if (s < 40) s = 40; if (s > 96) s = 96;
    int avail = back_h - y - hdr - font_h / 2;     /* the game's windows at the bottom start further right */
    while (s > 28 && n * (s + 4 + hb + 2 + gap) > avail) s -= 2;
    int aw = s / 6 + 3, col = s + 8 + aw;
    const wchar_t *title = ordering ? L"Ходы ≈" : L"Ходы";
    if (col < text_w(title, font_small) + 8) col = text_w(title, font_small) + 8;
    fill(x0 - 3, y - 3, col, hdr + 3 + n * (s + 4 + hb + 2 + gap), RGBC(14, 16, 30));
    { int tw = text_w(title, font_small); draw_text(x0 + (tw < s ? (s - tw) / 2 : 0), y - 1, title, RGBC(255, 220, 130), font_small); }
    turn_rect[0] = x0 - 3; turn_rect[1] = y - 3; turn_rect[2] = col; turn_rect[3] = hdr + 3 + n * (s + 4 + hb + 2 + gap);
    quick_bottom = turn_rect[1] + turn_rect[3];
    y += hdr;
    for (int k = 0; k < n; k++) {
        const uint8_t *e = r + 0x4400 + (ids[k] - 1) * 64;
        int a = e[6] << 8 | e[7], hp = 0, hpm = 1, out = (r[(a + 22) & 0xFFFF] & 0x6E) != 0, done = !ordering && cur >= 0 && k < cur;
        int now = k == cur;
        DWORD frame = now ? RGBC(255, 210, 80) : RGBC(70, 78, 120);
        fill(x0 - 2, y - 2, s + 4, s + 4, frame);
        fill(x0, y, s, s, RGBC(26, 30, 56));
        if (a >= 0xF500 && a < 0xF500 + 11 * 0x80) {           /* a character */
            int rec = (a - 0xF500) / 0x80;
            const uint8_t *c = r + a;
            hp = (int16_t)(c[0x0E] << 8 | c[0x0F]); hpm = c[0x10] << 8 | c[0x11];
            const uint32_t *pic = portrait_pixels(rec);
            if (pic) put_portrait(pic, x0, y, s, out || hp <= 0 || done);
            else { wchar_t nm[16]; ui_ps4_name(c, nm, 16); nm[2] = 0; draw_text(x0 + s / 4, y + s / 3, nm, RGBC(230, 230, 255), font_big); }
        } else if (a >= 0x4200 && a < 0x4600) {                  /* a monster */
            int id = r[a + 0x68] << 8 | r[a + 0x69];
            hp = (int16_t)(r[a + 0x0E] << 8 | r[a + 0x0F]); hpm = (int16_t)(r[a + 0x10] << 8 | r[a + 0x11]);
            if (!draw_thumb(x0 + 2, y + 2, s - 4, s - 4, id)) { int is = s * 2 / 3; draw_icon(x0 + (s - is) / 2, y + (s - is) / 2, is, IC_SKULL, 0); }
            if (out || hp <= 0 || done) {   /* dimmed: a dark veil over every other pixel */
                uint32_t *dst = (uint32_t *)back_bits;
                for (int yy = y; yy < y + s && yy < back_h; yy++) for (int xx = x0 + (yy & 1); xx < x0 + s && xx < back_w; xx += 2) dst[(size_t)yy * back_w + xx] = 0x101020;
            }
        }
        if (now) {   /* who acts now: an arrow on the right */
            int ah = aw * 2; for (int i = 0; i < aw; i++) fill(x0 + s + 3 + i, y + s / 2 - ah / 2 + i, 1, ah - 2 * i, RGBC(255, 210, 80));
        }
        { wchar_t t[8]; _snwprintf(t, 7, L"%d", k + 1); t[7] = 0;   /* the place in the order */
          int tw = text_w(t, font_small) + font_h / 3, th = font_h * 4 / 5;
          fill(x0, y, tw, th, RGBC(18, 20, 40)); draw_text(x0 + font_h / 6, y - 1, t, now ? RGBC(255, 220, 130) : RGBC(200, 205, 230), font_small); }
        int hy = y + s + 3;
        fill(x0, hy, s, hb, RGBC(48, 20, 24));
        if (hpm > 0 && hp > 0) { int v = hp > hpm ? hpm : hp; fill(x0, hy, s * v / hpm, hb, v * 4 <= hpm ? RGBC(200, 58, 48) : v * 2 <= hpm ? RGBC(206, 160, 52) : RGBC(62, 160, 78)); }
        y += s + 4 + hb + 2 + gap;
    }
}

/* quick access buttons, under the party panel (or at the top left without it):
 * journal, bestiary, world map, where next, party, save, the pause menu */
static const struct { int icon; const wchar_t *name; } QB[7] = {
    { IC_BOOK, L"Журнал" }, { IC_SKULL, L"Бестиарий" }, { IC_MAP, L"Карта мира" }, { IC_COMPASS, L"Куда идти" },
    { IC_PARTY, L"Отряд" }, { IC_SAVE, L"Сохранить" }, { IC_MENU, L"Меню" } };
static int qb_x[7], qb_y[7], qb_s;
static void draw_quickbar(int py, int ph)
{
    int m = emu_game_mode_top();
    if (m != 0x0C && m != 0x14) return;
    int s = font_h * 7 / 4; if (s < 24) s = 24;
    int gap = font_h / 4 + 1, cols = 4;
    int x0 = font_h / 2, y0 = quick_bottom ? quick_bottom + font_h / 2 : (py > font_h / 2 ? py : font_h / 2) + 3;
    if (y0 + 2 * (s + gap) > back_h) return;
    /* in battle the game's windows fill the bottom: the buttons only where they stay clear of them */
    if (m == 0x14 && y0 + 2 * (s + gap) > py + ph * 72 / 100) return;
    qb_s = s;
    int hover = -1;
    { int pw = cols * (s + gap) - gap, ph = 2 * (s + gap) - gap;   /* the panel under the buttons, gaps included */
      fill(x0 - 3, y0 - 3, pw + 6, ph + 6, RGBC(14, 16, 30)); }
    for (int k = 0; k < 7; k++) {
        int x = x0 + (k % cols) * (s + gap), y = y0 + (k / cols) * (s + gap);
        qb_x[k] = x; qb_y[k] = y;
        int on = g_mouse_x >= x && g_mouse_x < x + s && g_mouse_y >= y && g_mouse_y < y + s;
        if (on) hover = k;
        fill(x, y, s, s, on ? RGBC(70, 96, 170) : RGBC(18, 24, 52));
        fill(x, y, s, 1, RGBC(92, 104, 150)); fill(x, y + s - 1, s, 1, RGBC(10, 12, 26));
        int isz = s * 2 / 3;
        draw_icon(x + (s - isz) / 2, y + (s - isz) / 2, isz, QB[k].icon, 0);
    }
    int w = cols * (s + gap) - gap, h = 2 * (s + gap) - gap;
    quick_rect[0] = x0 - 3; quick_rect[1] = y0 - 3; quick_rect[2] = w + 6; quick_rect[3] = h + 6;
    if (hover >= 0) {   /* the name of the button under the mouse */
        int tw = text_w(QB[hover].name, font_small) + font_h, th = font_h + 4;
        int tx = x0 + w + font_h / 3, ty = qb_y[hover] + (s - th) / 2;
        fill(tx, ty, tw, th, RGBC(16, 28, 74));
        draw_text(tx + font_h / 2, ty + 2, QB[hover].name, RGBC(255, 240, 180), font_small);
        qtip_rect[0] = tx; qtip_rect[1] = ty; qtip_rect[2] = tw; qtip_rect[3] = th;
    }
}
static int quickbar_hit(int mx, int my)
{
    if (!quick_rect[2]) return -1;
    for (int k = 0; k < 7; k++) if (mx >= qb_x[k] && mx < qb_x[k] + qb_s && my >= qb_y[k] && my < qb_y[k] + qb_s) return k;
    return -1;
}
static void quickbar_act(int k);

/* monster pictures for the HP panel and the bestiary, taken from the battle
 * picture itself (the ROM in use; kept in saves\monsters\<id>.png). In
 * battle the monsters are drawn on plane A (low priority; the party is on
 * high-priority plane A, the scenery on plane B). While the game waits for
 * orders and every monster is unhurt, those pixels in the upper part of the
 * picture (the VDP's layer map) fall into groups of columns, one per
 * monster, left to right in the game's order. */
#define MON_PIX(l) ((l) == 2 || (l) == 3)
typedef struct { int tried, w, h; uint32_t *px; } mthumb_t;   /* px: 0xAARRGGBB, alpha 0 or 255 */
static mthumb_t mthumb[BEST_MAX];
static void mthumb_path(wchar_t *p, int id) { wchar_t n[64]; _snwprintf(n, 63, L"saves\\monsters\\%03d.png", id); n[63] = 0; path_in_exe_dir(p, n); }
static const mthumb_t *monster_thumb(int id)
{
    if (id < 0 || id >= BEST_MAX) return 0;
    mthumb_t *t = &mthumb[id];
    if (!t->px && !t->tried) {
        t->tried = 1;
        wchar_t p[700]; mthumb_path(p, id);
        size_t n = 0; uint8_t *b = read_file(p, &n);
        if (b) {
            int w, h; const char *e; uint8_t *rgba = png_decode(b, n, &w, &h, &e);
            if (rgba && w > 0 && h > 0 && w <= 256 && h <= 256) {
                t->px = malloc((size_t)w * h * 4); t->w = w; t->h = h;
                for (int i = 0; i < w * h; i++)
                    t->px[i] = (rgba[i * 4 + 3] > 127 ? 0xFF000000u : 0) | (uint32_t)rgba[i * 4] << 16 | (uint32_t)rgba[i * 4 + 1] << 8 | rgba[i * 4 + 2];
            }
            free(rgba); free(b);
        }
    }
    return t->px ? t : 0;
}
/* the rows of a column group's picture: from its first pixel down to a gap of 3 empty rows */
static int mon_rows(const uint8_t *lay, int sx, int ex, int top, int bottom, int *y0, int *y1)
{
    int gap = 0; *y0 = *y1 = -1;
    for (int y = top; y < bottom; y++) {
        int any = 0;
        for (int x = sx; x <= ex; x++) if (MON_PIX(lay[y * MD_W_MAX + x])) { any = 1; break; }
        if (any) { if (*y0 < 0) *y0 = y; *y1 = y; gap = 0; }
        else if (*y0 >= 0 && ++gap >= 3) break;
    }
    return *y0 >= 0 && *y1 - *y0 + 1 >= 12 && ex - sx + 1 <= 200;
}
static void monster_save(int id, const uint32_t *fb, const uint8_t *lay, int sx, int ex, int y0, int y1)
{
    int w = ex - sx + 1, h = y1 - y0 + 1;
    uint8_t *rgba = calloc((size_t)w * h, 4);
    if (!rgba) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int o = (y0 + y) * MD_W_MAX + sx + x;
            if (!MON_PIX(lay[o])) continue;
            uint32_t c = fb[o];
            uint8_t *q = rgba + ((size_t)y * w + x) * 4;
            q[0] = (uint8_t)(c >> 16); q[1] = (uint8_t)(c >> 8); q[2] = (uint8_t)c; q[3] = 255;
        }
    size_t pn; uint8_t *png = png_encode(rgba, w, h, &pn);
    free(rgba);
    if (png) {
        saves_dir();
        { wchar_t d[700]; path_in_exe_dir(d, L"saves\\monsters"); CreateDirectoryW(d, 0); }
        wchar_t p[700]; mthumb_path(p, id);
        if (write_file(p, png, pn)) { mthumb[id].tried = 0; free(mthumb[id].px); mthumb[id].px = 0; monster_thumb(id); logw("monster picture %d: %dx%d", id, w, h); }
        free(png);
    }
}
/* while the game waits for orders nobody flashes or moves: the monsters'
 * columns are grouped; when there are as many groups as monsters they go in
 * order, else groups the size of pictures already known are set aside and
 * a single group left goes to the single monster still without a picture
 * (a boss beside its helpers, one of them hidden behind a window) */
static void monster_capture(void)
{
    static uint64_t last;
    if (frame_no - last < 15) return;
    last = frame_no;
    int ids[8], hp[8], mx[8];
    int n = bestiary_battle(ids, hp, mx, 8);
    if (n <= 0 || emu_work_ram()[0x4109] == 0) return;   /* not waiting for orders */
    int alive[8], na = 0, need = 0, unknown = -1, nunknown = 0;
    for (int i = 0; i < n; i++) if (hp[i] > 0) {
        alive[na++] = i;
        if (ids[i] >= 0 && ids[i] < BEST_MAX && !monster_thumb(ids[i])) {
            need = 1;
            if (unknown != ids[i]) { if (unknown < 0) { unknown = ids[i]; nunknown = 1; } else nunknown = 2; }
        }
    }
    if (!na || !need) return;
    int W, H; const uint32_t *fb = emu_framebuffer(&W, &H);
    const uint8_t *lay = emu_layers();
    int top = 4, zone = H * 56 / 100, bottom = H * 74 / 100;
    /* columns with monster pixels in the upper part, grouped (gaps up to 4) */
    static uint8_t col[MD_W_MAX];
    for (int x = 0; x < W; x++) { col[x] = 0; for (int y = top; y < zone; y++) if (MON_PIX(lay[y * MD_W_MAX + x])) { col[x] = 1; break; } }
    int sx[8], ex[8], ns = 0;
    for (int x = 0; x < W; ) {
        if (!col[x]) { x++; continue; }
        int a = x, b = x;
        while (b + 1 < W && (col[b + 1] || (b + 2 < W && col[b + 2]) || (b + 3 < W && col[b + 3]) || (b + 4 < W && col[b + 4]))) { b++; }
        while (b > a && !col[b]) b--;
        if (b - a + 1 >= 12) { if (ns == 8) return; sx[ns] = a; ex[ns] = b; ns++; }
        x = b + 1;
    }
    if (ns == na) {
        for (int k = 0; k < ns; k++) {
            int id = ids[alive[k]], y0, y1;
            if (id < 0 || id >= BEST_MAX || monster_thumb(id)) continue;
            if (mon_rows(lay, sx[k], ex[k], top, bottom, &y0, &y1)) monster_save(id, fb, lay, sx[k], ex[k], y0, y1);
        }
        return;
    }
    if (nunknown != 1) return;
    int pick = -1, npick = 0;
    for (int k = 0; k < ns; k++) {
        int y0, y1;
        if (!mon_rows(lay, sx[k], ex[k], top, bottom, &y0, &y1)) continue;
        int w = ex[k] - sx[k] + 1, h = y1 - y0 + 1, known = 0;
        for (int j = 0; j < na; j++) {
            const mthumb_t *t = monster_thumb(ids[alive[j]]);
            if (t && abs(t->w - w) <= 2 && abs(t->h - h) <= 2) known = 1;
        }
        if (known) continue;
        /* cut by a window (or the picture's edge) at its sides: not whole */
        int cut = sx[k] < 2 || ex[k] > W - 3;
        for (int y = y0; y <= y1 && !cut; y++) {
            if (sx[k] >= 2 && lay[y * MD_W_MAX + sx[k] - 2] == 4) cut = 1;
            if (ex[k] + 2 < W && lay[y * MD_W_MAX + ex[k] + 2] == 4) cut = 1;
        }
        if (cut) continue;
        pick = k; npick++;
    }
    if (npick == 1) {
        int y0, y1;
        mon_rows(lay, sx[pick], ex[pick], top, bottom, &y0, &y1);
        monster_save(unknown, fb, lay, sx[pick], ex[pick], y0, y1);
    }
}
/* a monster picture fitted into a box (keeping its shape, whole pixels when possible) */
static int draw_thumb(int bx, int by, int bw, int bh, int id)
{
    const mthumb_t *t = monster_thumb(id);
    if (!t) return 0;
    double sc = (double)bw / t->w; if ((double)bh / t->h < sc) sc = (double)bh / t->h;
    if (sc >= 1) sc = (int)sc;
    int dw = (int)(t->w * sc), dh = (int)(t->h * sc); if (dw < 1) dw = 1; if (dh < 1) dh = 1;
    int x0 = bx + (bw - dw) / 2, y0 = by + (bh - dh) / 2;
    uint32_t *dst = (uint32_t *)back_bits;
    for (int y = 0; y < dh; y++) {
        int yy = y0 + y; if (yy < 0 || yy >= back_h) continue;
        const uint32_t *row = t->px + (size_t)(y * t->h / dh) * t->w;
        for (int x = 0; x < dw; x++) {
            int xx = x0 + x; if (xx < 0 || xx >= back_w) continue;
            uint32_t c = row[x * t->w / dw];
            if (c >> 24) dst[(size_t)yy * back_w + xx] = c & 0xFFFFFF;
        }
    }
    return 1;
}

/* the elements in a bestiary text ("огонь, лёд (очень)") as icons; flag:
 * "(очень)" (very weak) or "(не берёт)" (not hurt at all) */
static const struct { const char *name; int icon; const wchar_t *wname; } ELEM[8] = {
    { "ветер", IC_WIND, L"ветер" }, { "энергия", IC_ENERGY, L"энергия" }, { "огонь", IC_FIRE, L"огонь" }, { "гравитация", IC_GRAVITY, L"гравитация" },
    { "лёд", IC_ICE, L"лёд" }, { "свет", IC_LIGHT, L"свет" }, { "гром", IC_THUNDER, L"гром" }, { "святость", IC_HOLY, L"святость" } };
static int elem_icons(const char *text, int *ic, int *flag, int max)
{
    int n = 0;
    const char *p = text;
    while (p && *p && n < max) {
        while (*p == ',' || *p == ' ') p++;
        for (int e = 0; e < 8; e++) {
            size_t l = strlen(ELEM[e].name);
            if (!strncmp(p, ELEM[e].name, l)) {
                ic[n] = e;
                flag[n] = !strncmp(p + l, " (", 2);
                n++; break;
            }
        }
        p = strchr(p, ',');
    }
    return n;
}

/* monsters' HP in battle, top right: the monster's picture (or an icon),
 * the name, the bar with numbers, then the elements it is weak to (green;
 * a gold frame: very weak) and those it resists (red; crossed: not hurt at
 * all). The panel keeps clear of the game's own windows at the top: it
 * gets narrower, or goes below them. Pointing at an element names it. */
static void draw_enemy_hp(int px, int py, int pw, int ph)
{
    int ids[8], hp[8], mx[8];
    int n = bestiary_battle(ids, hp, mx, 8);
    if (n <= 0) return;
    monster_capture();
    wchar_t nm[8][48]; int alive = 0, wname = 0, nw[8], wic[8][8], wfl[8][8], nr[8], ric[8][8], rfl[8][8], mach[8], maxw = 0, maxr = 0, danger[8];
    /* the party's average attack, defence and HP: the monsters' strength is judged against them
     * (the game gives monsters no levels) */
    int pa = 0, pd = 0, ph2 = 0, pn = 0;
    { const uint8_t *r = emu_work_ram();
      for (int k = 0; k < 5; k++) { int rec = r[0xF40A + k]; if (rec == 0xFF || rec > 10) break;
          const uint8_t *c = r + 0xF500 + rec * 0x80; pa += c[0x24] << 8 | c[0x25]; pd += c[0x28] << 8 | c[0x29]; ph2 += c[0x10] << 8 | c[0x11]; pn++; } }
    for (int i = 0; i < n; i++) {
        best_info_t b;
        if (!emu_bestiary(ids[i], &b)) return;
        danger[i] = 1;
        if (pn) {
            int A = pa / pn, D = pd / pn, HP = ph2 / pn > 0 ? ph2 / pn : 1;
            int per = A - b.defence / 2; if (per < 1) per = 1;
            int hits = (b.hp + per - 1) / per, dmg = b.attack - D / 2; if (dmg < 1) dmg = 1;
            danger[i] = hits >= 6 || dmg * 4 >= HP ? 2 : hits <= 2 && dmg * 10 < HP ? 0 : 1;
        }
        MultiByteToWideChar(65001, 0, b.name, -1, nm[i], 48);
        nw[i] = elem_icons(b.weak, wic[i], wfl[i], 8);
        nr[i] = elem_icons(b.resist, ric[i], rfl[i], 8);
        mach[i] = b.machine;
        if (hp[i] > 0) { alive++; int w = text_w(nm[i], font_small); if (w > wname) wname = w;
                         if (nw[i] > maxw) maxw = nw[i]; if (nr[i] > maxr) maxr = nr[i]; }
    }
    if (!alive) return;
    if (maxr > 4) maxr = 4;   /* more: "+N" */
    int thumbs = 0;
    for (int i = 0; i < n; i++) if (hp[i] > 0 && monster_thumb(ids[i])) thumbs = 1;
    int lh = thumbs ? font_h * 5 / 2 : font_h * 6 / 5, pad = font_h / 3;
    int isz = font_h * 9 / 10, ist = isz + font_h / 4, pic = thumbs ? lh - 4 : isz, pst = pic + font_h * 3 / 4;
    int head = (maxw || maxr) ? font_h * 4 / 5 + 2 : 0;
    const wchar_t *hw = L"слабость", *hr = L"стойкость";
    int colw = maxw ? (maxw * ist > text_w(hw, font_small) ? maxw * ist : text_w(hw, font_small)) : 0;
    int colr = maxr ? (maxr * ist + (maxr == 4 ? ist : 0) > text_w(hr, font_small) ? maxr * ist + (maxr == 4 ? ist : 0) : text_w(hr, font_small)) : 0;
    int bh = head + alive * lh + pad * 2;
    /* the game's windows (layer 4) in the top part of the picture */
    int W, H; emu_framebuffer(&W, &H); if (W < 1 || W > MD_W_MAX) W = 320; if (H < 1 || H > 224) H = 224;
    const uint8_t *lay = emu_layers();
    int bx = 0, by = py + font_h / 3, bw = 0, compact = 0, barw = 0, numw = 0;
    for (int mode = 0; mode < 3; mode++) {
        compact = mode >= 1;
        if (mode == 2) {   /* below the windows, over the monsters: as low as it gets */
            lh = font_h * 6 / 5; pic = isz; pst = pic + font_h * 3 / 4; bh = head + alive * lh + pad * 2;
        }
        barw = compact ? font_h * 2 : font_h * 4;
        numw = compact ? text_w(L"0000", font_small) : text_w(L"0000/0000", font_small);
        bw = pad * 3 + pst + wname + font_h / 2 + barw + font_h / 2 + numw + (colw ? font_h / 2 + colw : 0) + (colr ? font_h / 2 + colr : 0);
        bx = px + pw - bw - font_h / 3;
        by = py + font_h / 3;
        int gx0 = (bx - px) * W / pw, gx1 = (bx + bw - px) * W / pw, gy1 = (by + bh - py) * H / ph, bottom = -1;
        if (gx0 < 0) gx0 = 0; if (gx1 > W) gx1 = W; if (gy1 > H * 45 / 100) gy1 = H * 45 / 100;
        for (int y = 0; y < H * 45 / 100; y++)
            for (int x = gx0; x < gx1; x++) if (lay[y * MD_W_MAX + x] == 4) { bottom = y; break; }
        if (bottom < 0) break;                               /* clear */
        if (mode == 2) by = py + (bottom + 3) * ph / H;       /* still in the way: below the windows */
    }
    fill(bx, by, bw, bh, RGBC(150, 160, 200));
    fill(bx + 1, by + 1, bw - 2, bh - 2, RGBC(10, 20, 60));
    int xbar = bx + pad * 2 + pst + wname + font_h / 2, xw = xbar + barw + font_h / 2 + numw + font_h / 2, xr = xw + (colw ? colw + font_h / 2 : 0);
    if (head) {
        if (colw) draw_text(xw, by + pad - 2, hw, RGBC(140, 230, 150), font_small);
        if (colr) draw_text(xr, by + pad - 2, hr, RGBC(240, 140, 130), font_small);
    }
    int ty = by + pad + head, tip_e = -1, tip_kind = 0, tip_fl = 0;
    for (int i = 0; i < n; i++) {
        if (hp[i] <= 0) continue;
        int m = mx[i] > 0 ? mx[i] : hp[i];
        int iy = ty + (lh - isz) / 2 - 1, tyy = ty + (lh - font_h) / 2;
        if (!draw_thumb(bx + pad, ty + 2, pic, lh - 4, ids[i])) draw_icon(bx + pad + (pic - isz) / 2, iy, isz, mach[i] ? IC_GEAR : IC_SKULL, 0);
        {   /* strength against this party: a dot before the name, and the name's colour */
            static const DWORD dc[3] = { RGBC(110, 220, 120), RGBC(240, 210, 90), RGBC(250, 96, 80) };
            int d = font_h / 3 + 2, dx = bx + pad + pst - d - 3, dy = ty + (lh - d) / 2;
            fill(dx, dy, d, d, dc[danger[i]]);
            draw_text(bx + pad + pst, tyy, nm[i], danger[i] == 2 ? RGBC(255, 190, 180) : RGBC(230, 230, 255), font_small);
            if (g_mouse_x >= bx + pad && g_mouse_x < bx + pad + pst + wname && g_mouse_y >= ty && g_mouse_y < ty + lh) { tip_e = 100 + danger[i]; tip_kind = 3; }
        }
        int yb = ty + lh / 2 - font_h / 6, hb = font_h / 3;
        int f = m ? barw * (hp[i] > m ? m : hp[i]) / m : 0;
        DWORD col = hp[i] * 4 <= m ? RGBC(240, 80, 70) : hp[i] * 2 <= m ? RGBC(240, 200, 70) : RGBC(90, 220, 110);
        fill(xbar, yb, barw, hb, RGBC(40, 40, 70));
        if (f > 0) fill(xbar, yb, f, hb, col);
        wchar_t t[24];
        if (compact) _snwprintf(t, 23, L"%d", hp[i]); else _snwprintf(t, 23, L"%d/%d", hp[i], m);
        t[23] = 0;
        draw_text(xbar + barw + font_h / 2, tyy, t, RGBC(200, 210, 240), font_small);
        for (int k = 0; k < nw[i]; k++) {
            int x = xw + k * ist;
            fill(x - 2, iy - 2, isz + 4, isz + 4, wfl[i][k] ? RGBC(214, 176, 92) : RGBC(40, 120, 64));
            fill(x - 1, iy - 1, isz + 2, isz + 2, RGBC(16, 48, 36));
            draw_icon(x, iy, isz, ELEM[wic[i][k]].icon, 0);
            if (g_mouse_x >= x - 2 && g_mouse_x < x + isz + 2 && g_mouse_y >= iy - 2 && g_mouse_y < iy + isz + 2) { tip_e = wic[i][k]; tip_kind = 1; tip_fl = wfl[i][k]; }
        }
        for (int k = 0; k < nr[i] && k < 4; k++) {
            int x = xr + k * ist;
            fill(x - 2, iy - 2, isz + 4, isz + 4, RGBC(130, 50, 46));
            fill(x - 1, iy - 1, isz + 2, isz + 2, RGBC(52, 18, 22));
            draw_icon(x, iy, isz, ELEM[ric[i][k]].icon, 1);
            if (rfl[i][k]) for (int d = 0; d < isz; d++) { fill(x + d, iy + d, 2, 1, RGBC(240, 70, 60)); fill(x + isz - 1 - d, iy + d, 2, 1, RGBC(240, 70, 60)); }
            if (g_mouse_x >= x - 2 && g_mouse_x < x + isz + 2 && g_mouse_y >= iy - 2 && g_mouse_y < iy + isz + 2) { tip_e = ric[i][k]; tip_kind = 2; tip_fl = rfl[i][k]; }
        }
        if (nr[i] > 4) { wchar_t more[8]; _snwprintf(more, 7, L"+%d", nr[i] - 4); more[7] = 0; draw_text(xr + 4 * ist, tyy, more, RGBC(240, 140, 130), font_small); }
        ty += lh;
    }
    hp_rect[0] = bx; hp_rect[1] = by; hp_rect[2] = bw; hp_rect[3] = bh;
    if (tip_e >= 0) {   /* the element under the mouse, in words */
        wchar_t t[120];
        static const wchar_t *dn[3] = { L"слабее отряда", L"примерно на равных", L"опасен для отряда" };
        if (tip_kind == 3) _snwprintf(t, 119, L"Сила: %ls (оценка по атаке, защите и ЕЖ)", dn[tip_e - 100]);
        else _snwprintf(t, 119, L"%ls: %ls", ELEM[tip_e].wname, tip_kind == 1 ? (tip_fl ? L"очень сильный урон" : L"сильный урон")
                                                               : (tip_fl ? L"не действует" : L"слабый урон"));
        t[119] = 0;
        int tw = text_w(t, font_small) + font_h, th = font_h + 4, tx = bx + bw - tw, tyy = by + bh + 2;
        fill(tx, tyy, tw, th, RGBC(16, 28, 74));
        draw_text(tx + font_h / 2, tyy + 2, t, RGBC(255, 240, 180), font_small);
        hptip_rect[0] = tx; hptip_rect[1] = tyy; hptip_rect[2] = tw; hptip_rect[3] = th;
    }
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
enum { M_MAIN, M_VIDEO, M_SPEED, M_KEYS, M_PAD, M_HELP, M_HD, M_SAVE, M_STATS, M_BEST, M_ACH, M_SET, M_SOUND, M_HINTS, M_GOAL, M_PARTY, M_CHAR, M_PLAY, M_GEAR };
static int m_char, m_char_from_panel;   /* the character card: party slot, opened by a click on the party panel */
static int m_best_page, m_best_next = -1, m_best_ids[10];   /* bestiary page shown; index of the "next page" item */
static int m_save_load;                     /* M_SAVE lists slots to load (1) or to save into (0) */
static int m_page, m_sel, m_wait_bind = -1;   /* action index waiting for a key / pad input */
static int m_confirm_quit;

typedef struct { wchar_t label[128]; wchar_t value[80]; int icon, info; } item_t;   /* info: text only, not selectable */
static item_t items[64];
static int nitems;
static wchar_t m_title[80];

static int m_icon, m_info;   /* for the next add(): its icon, and whether it is a text-only line */
static int party_row[5][5];  /* the party page: level, HP, max HP, TP, max TP of each row */
static void add(const wchar_t *l, const wchar_t *v)
{
    if (nitems >= 64) return;
    wcsncpy(items[nitems].label, l, 127); items[nitems].label[127] = 0;
    wcsncpy(items[nitems].value, v ? v : L"", 79); items[nitems].value[79] = 0;
    items[nitems].icon = m_icon; items[nitems].info = m_info;
    m_icon = 0; m_info = 0;
    nitems++;
}
static void add_i(int icon, const wchar_t *l, const wchar_t *v) { m_icon = icon; add(l, v); }
static void add_info(int icon, const wchar_t *l, const wchar_t *v) { m_icon = icon; m_info = 1; add(l, v); }
/* the selection moves over text-only lines */
static void menu_step(int d)
{
    for (int t = 0; t < nitems; t++) { m_sel = (m_sel + nitems + d) % nitems; if (!items[m_sel].info) break; }
}

static const wchar_t *onoff(int v) { return v ? L"вкл" : L"выкл"; }

/* "where next": the story stage guessed from who is in the party and the key
 * items carried or worn (after the PSCave walkthrough, parts I-III).
 * Records: 0 Chaz 1 Alys 2 Hahn 3 Rune 4 Gryz 5 Rika 6 Demi 7 Wren 8 Raja 9 Kyra 10 Seth */
static const wchar_t *story_hint_g(int *goal)
{
    *goal = -1;
    int m = emu_game_mode_top();
    if (m != 0x0C && m != 0x14) return L"Подсказка появится, когда вы будете в игре (на карте или в бою).";
    const uint8_t *r = emu_work_ram();
    int in[16] = { 0 }, has[256] = { 0 };
    for (int p = 0; p < 5; p++) { int rec = r[0xF40A + p]; if (rec == 0xFF) break; if (rec < 16) in[rec] = 1; }
    for (int i = 0; i < 40; i++) has[r[0xF410 + i]] = 1;
    for (int rec = 0; rec < 11; rec++) for (int k = 0; k < 4; k++) has[r[0xF500 + rec * 0x80 + 0x4C + k]] = 1;
    int ring = has[155] || has[156] || has[157] || has[158] || has[159];
    if (has[119]) return L"У Чаза меч Элсидеон. На Рикросе: Башня гнева (к юго-западу от Башни тишины) — Чаз должен отказаться от силы гнева. "
                         L"Затем разлом к северу от Пиаты: финальная битва с Бездонной Тьмой. Три формы подряд, берегите ЕТ и навыки, перед боем — полный отдых.";
    if (ring) return L"Кольца получены. Вернитесь к Ле Руфу в Башню тишины, затем в Особняк целителей на Дезолисе: в пещере у статуи Алис Чаз получит меч Элсидеон.";
    if (has[143]) return L"Воздушный кристалл у вас — летите на Рикрос. Башня тишины (Ле Руф), затем Башня храбрости (на юго-востоке, нужен гидрофоил) "
                         L"и Башня силы (на северо-западе): в них Кольца звёзд.";
    if (in[10]) { *goal = 6; return L"Храм воина у Крупа (на гидрофоиле от Деми): Воздушный кристалл на вершине столовой горы. "
                         L"Сет окажется Тёмной силой — бой примерно на 8000 ЕЖ."; }
    if (has[142]) { *goal = 32; return L"Факел затмения у вас. Хищный лес к северо-востоку от Миисы: факел расчистит путь к Башне Гаруба. "
                         L"Глаза на 2-м и 5-м этажах открывают проходы, на 7-м — Тёмная сила (Эфесс Руна очень помогает)."; }
    if (in[9]) { *goal = 34; return L"Особняк целителей (к юго-востоку от Миисы): в подвале шар телепатии. Затем Храм Гумбеос (на запад) за Факелом затмения; "
                         L"его украдут — нужен Воздушный замок Лашиека (корабль из ангара у Тайлера)."; }
    if (in[8]) { *goal = 25; return L"Рион (поговорите с барменом), затем Тайлер: надгробие на западе открывает пещеру с кораблём Ландейл. "
                         L"На нём летите на Куран и отключите главный терминал (в конце — Тёмная сила). Потом Зоша, Центр климата (Система управления погодой) и Мииса."; }
    if (in[7]) { *goal = 24; return L"После крушения на Дезолисе: в Храме Раджа он присоединится к отряду, дальше на север — Рион."; }
    if (has[57]) { *goal = 14; return L"Пси-посох у вас. Замок Зио — к югу от Кадари; Нервус под ним. В первом ходу боя с Зио используйте Пси-посох "
                         L"как предмет, иначе его чёрная энергия не даст нанести урон."; }
    if (in[6] && !in[1]) { *goal = 18; return L"С Деми: Гараж к югу от Крупа (там лендровер), затем Монсен и Тектоническая система — остановите землетрясения. "
                         L"Потом Терми и Башня Ладей к югу от него: наверху Приспешник тьмы, у него Пси-посох."; }
    if (in[5] && in[1]) { *goal = 10; return L"После Био-завода: Налия и место крушения корабля рядом с ней, затем Аейдо "
                         L"(гильдия охотников, дом Элис — бесплатный отдых). Проход на север ведёт к Кадари, Замок Зио — к югу от Кадари."; }
    if (in[4]) { *goal = has[141] ? 2 : 8;
                 return has[141] ? L"Элшлайн у вас: отнесите его в Зему, чтобы расколдовать жителей. Затем дверь в глубине пещеры Долины жизни ведёт к Био-заводу."
                                 : L"Тоное: Элшлайн лежит в северном подвале. Потом возвращайтесь в Зему."; }
    if (in[3]) { *goal = 6; return L"С Руном: Круп (родной город Хана), затем Долина-лабиринт к северу от Крупа — Рун разобьёт валун. Дальше Тоное."; }
    if (in[2]) { *goal = 2; return L"Подвал Академии Пиаты (босс — Улей), затем Миле (снаряжение получше) и Зема: жители окаменели, "
                         L"нужен Элшлайн — идите в Молкум."; }
    *goal = 0;
    return L"Академия Пиаты: поговорите с директором, затем спуститесь в подвал.";
}
static const wchar_t *story_hint(void) { int g; return story_hint_g(&g); }
static int story_goal(void) { int g; story_hint_g(&g); return g; }
/* where a place is from the party, in words ("к северо-востоку, близко") */
static int goal_direction(int goal, wchar_t *out, int n)
{
    static uint32_t img[512 * 1024];
    int w, h, mx, my, planet = emu_world_image(img, 512, 1024, &w, &h, &mx, &my);
    if (!planet || mx < 0) return 0;
    int place[48], tx[48], ty[48], nt = emu_world_towns(planet - 1, place, tx, ty, 48);
    for (int i = 0; i < nt; i++) if (place[i] == goal) {
        int dx = tx[i] - mx, dy = ty[i] - my, d = (int)sqrt((double)dx * dx + (double)dy * dy);
        if (d < 12) { _snwprintf(out, n - 1, L"%ls — совсем рядом", place_name(goal)); out[n - 1] = 0; return 1; }
        static const wchar_t *dn[8] = { L"к востоку", L"к юго-востоку", L"к югу", L"к юго-западу", L"к западу", L"к северо-западу", L"к северу", L"к северо-востоку" };
        double ang = atan2((double)dy, (double)dx);   /* y grows southwards */
        int k = ((int)floor(ang / (3.14159265 / 4) + 0.5) + 8) % 8;
        _snwprintf(out, n - 1, L"%ls — %ls от отряда, %ls", place_name(goal), dn[k], d < 60 ? L"недалеко" : d < 160 ? L"на среднем расстоянии" : L"далеко");
        out[n - 1] = 0; return 1;
    }
    return 0;
}


static void menu_back(void);
/* the character card (pause menu → Отряд, or a click on the party panel):
 * stats, equipment, techniques and skills (Enter shows the description),
 * and what in the bag this character can wear */
static int m_row_kind[64], m_row_arg[64];   /* 1 technique, 2 skill, 3 item; the id */
static void build_char_card(void)
{
    static wchar_t lab[64][80], val[64][80];
    int nr = 0;
#define ROW(kind, arg, ...) do { if (nitems < 63) { m_row_kind[nitems] = kind; m_row_arg[nitems] = arg; \
        _snwprintf(val[nr], 79, __VA_ARGS__); val[nr][79] = 0; add(lab[nr], val[nr]); nr++; } } while (0)
    for (int i = 0; i < 64; i++) m_row_kind[i] = 0;
    const uint8_t *r = emu_work_ram();
    int m = emu_game_mode_top(), rec = r[0xF40A + (m_char < 5 ? m_char : 0)];
    if ((m != 0x0C && m != 0x14) || rec > 10) { wcscpy(m_title, L"Отряд"); add(L"Назад", 0); return; }
    const uint8_t *c = r + 0xF500 + rec * 0x80;
    wchar_t nm[40]; ui_ps4_name(c, nm, 40);
    uint32_t exp = (uint32_t)c[10] << 24 | (uint32_t)c[11] << 16 | (uint32_t)c[12] << 8 | c[13];
    _snwprintf(m_title, 79, L"%ls — уровень %d, опыт %lu", nm, c[8] << 8 | c[9], (unsigned long)exp); m_title[79] = 0;
    { /* experience to the next level: the game's own growth table (as on its status screen):
       * per character a first level ($05D846) and a table pointer ($05D852), 22-byte rows whose
       * first long is the experience needed for the next level */
      int lv = c[8] << 8 | c[9];
      if (lv >= 99) { wcscpy(lab[nr], L"Уровень"); m_icon = IC_UP; m_info = 1; ROW(0, 0, L"%ls", L"максимальный"); }
      else if (g_rom_size > 0x2B0000) {
          int base = g_rom[0x05D846 + rec];
          uint32_t tab = (uint32_t)g_rom[0x05D852 + rec * 4] << 24 | (uint32_t)g_rom[0x05D853 + rec * 4] << 16 | (uint32_t)g_rom[0x05D854 + rec * 4] << 8 | g_rom[0x05D855 + rec * 4];
          uint32_t row = tab + (uint32_t)(lv - base) * 22;
          if (lv >= base && row + 4 < g_rom_size) {
              uint32_t need = (uint32_t)g_rom[row] << 24 | (uint32_t)g_rom[row + 1] << 16 | (uint32_t)g_rom[row + 2] << 8 | g_rom[row + 3];
              _snwprintf(lab[nr], 79, L"До уровня %d", lv + 1); lab[nr][79] = 0;
              m_icon = IC_UP; m_info = 1; ROW(0, 0, L"%lu опыта", (unsigned long)(need > exp ? need - exp : 0));
          }
      }
    }
    wcscpy(lab[nr], L"ЕЖ / ЕТ"); m_icon = IC_HEART; m_info = 1;
    ROW(0, 0, L"%d/%d · %d/%d", (int16_t)(c[14] << 8 | c[15]), c[16] << 8 | c[17], (int16_t)(c[18] << 8 | c[19]), c[20] << 8 | c[21]);
    wcscpy(lab[nr], L"Атака / защита"); m_icon = IC_SWORD; m_info = 1;
    ROW(0, 0, L"%d / %d", c[0x24] << 8 | c[0x25], c[0x28] << 8 | c[0x29]);
    wcscpy(lab[nr], L"Сила · разум · ловкость · меткость"); m_icon = IC_FIST; m_info = 1;
    ROW(0, 0, L"%d · %d · %d · %d", c[0x18], c[0x1B], c[0x1E], c[0x21]);
    static const wchar_t *slot[4] = { L"Правая рука", L"Левая рука", L"Голова", L"Тело" };
    static const int slot_icon[4] = { IC_HAND, IC_SHIELD, IC_HELM, IC_ARMOR };
    for (int k = 0; k < 4; k++) {
        int id = c[0x4C + k];
        shop_t it; wcscpy(lab[nr], slot[k]); m_icon = slot_icon[k];
        if (id && shop_item(id, &it)) {
            wchar_t w[48]; MultiByteToWideChar(65001, 0, it.name, -1, w, 48);
            if (it.atk && it.def) ROW(3, id, L"%ls (атака %d, защита %d)", w, it.atk, it.def);
            else if (it.atk) ROW(3, id, L"%ls (атака %d)", w, it.atk);
            else if (it.def) ROW(3, id, L"%ls (защита %d)", w, it.def);
            else ROW(3, id, L"%ls", w);
        } else { m_info = 1; ROW(0, 0, L"—"); }
    }
    /* techniques ($52, 16 ids + 1) and skills ($62, 8 ids + 1, last first) */
    for (int i = 0; i < 16; i++) {
        int id = c[0x52 + i]; tip_t t;
        if (!id || !tips_info(0, id - 1, &t)) continue;
        MultiByteToWideChar(65001, 0, t.name, -1, lab[nr], 80);
        m_icon = IC_SPARK;
        ROW(1, id - 1, L"%d ЕТ  ▸", t.cost);
    }
    for (int i = 7; i >= 0; i--) {
        int id = c[0x62 + i]; tip_t t;
        if (!id || !tips_info(1, id - 1, &t)) continue;
        MultiByteToWideChar(65001, 0, t.name, -1, lab[nr], 80);
        m_icon = IC_STAR;
        ROW(2, id - 1, L"навык  ▸");
    }
    /* the bag: what this character can wear, and what it would change */
    int seen[256] = { 0 }, first = 1;
    for (int i = 0; i < 40; i++) {
        int id = r[0xF410 + i]; shop_t it;
        if (!id || seen[id]++ || !shop_item(id, &it) || it.type < 1 || it.type > 7) continue;
        for (int k = 0; k < it.nrow; k++) {
            shop_row_t *w = &it.row[k];
            if (k != m_char || !w->can || w->same) continue;
            wchar_t nmw[48]; MultiByteToWideChar(65001, 0, it.name, -1, nmw, 48);
            _snwprintf(lab[nr], 79, first ? L"Можно надеть: %ls" : L"%ls", nmw); lab[nr][79] = 0; first = 0;
            m_icon = IC_BAG;
            ROW(3, id, L"атака %+d, защита %+d", w->datk, w->ddef);
        }
    }
    add(L"Назад", 0);
#undef ROW
}
static void char_card_act(int i)
{
    if (i < 0 || i >= nitems) return;
    if (i == nitems - 1) { menu_back(); return; }
    int k = m_row_kind[i], id = m_row_arg[i];
    wchar_t t[400]; t[0] = 0;
    if (k == 1 || k == 2) {
        tip_t tp;
        if (!tips_info(k == 2, id, &tp)) return;
        wchar_t nm[40], d[200]; MultiByteToWideChar(65001, 0, tp.name, -1, nm, 40); MultiByteToWideChar(65001, 0, tp.desc, -1, d, 200);
        _snwprintf(t, 399, L"%ls: %ls", nm, d);
    } else if (k == 3) {
        shop_t it;
        if (!shop_item(id, &it)) return;
        wchar_t nm[48], kd[40]; MultiByteToWideChar(65001, 0, it.name, -1, nm, 48); MultiByteToWideChar(65001, 0, it.kind, -1, kd, 40);
        int n = _snwprintf(t, 399, L"%ls — %ls", nm, kd);
        for (int r = 0; r < it.nrow && n > 0 && n < 380; r++) {
            wchar_t who[24]; MultiByteToWideChar(65001, 0, it.row[r].who, -1, who, 24);
            int a = _snwprintf(t + n, 399 - n, L"; %ls: %ls", who, it.row[r].can ? (it.row[r].same ? L"надето" : L"может") : L"нет");
            if (a > 0) n += a;
        }
    } else return;
    t[399] = 0; set_status(t);
}

static void build_menu_pages(void);
/* the current page's lines; the selection never rests on a text-only line */
static void build_menu(void)
{
    build_menu_pages();
    if (m_sel >= nitems) m_sel = nitems - 1;
    if (m_sel < 0) m_sel = 0;
    if (nitems && items[m_sel].info) menu_step(1);
}
static void build_menu_pages(void)
{
    nitems = 0;
    wchar_t v[80];
    switch (m_page) {
    case M_MAIN:
        wcscpy(m_title, GAME_NAME);
        add_i(IC_PLAY, L"Продолжить", 0);
        add_i(IC_SAVE, L"Сохранить", L"▸");
        add_i(IC_LOAD, L"Загрузить", L"▸");
        add_i(IC_GEAR, L"Настройки", L"▸");
        add_i(IC_PARTY, L"Отряд: снаряжение, техники, характеристики", L"▸");
        add_i(IC_BOOK, L"Журнал: куда идти, снаряжение, карта, статистика", L"▸");
        add_i(IC_SKULL, L"Бестиарий", L"▸");
        add_i(IC_HELP, L"Справка", L"▸");
        add_i(IC_HOME, L"Выйти в главное меню", 0);
        add_i(IC_EXIT, m_confirm_quit ? L"Выйти? Нажмите ещё раз" : L"Выход", 0);
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
#ifdef __ANDROID__
        add(L"Полный экран", L"всегда");
#else
        add(L"Полный экран", onoff(fullscreen));
#endif
        add(L"Вертикальная синхронизация", onoff(C.vsync));
        {
#ifdef _WIN32
            static const wchar_t *rn[3] = { L"Direct3D 11", L"OpenGL", L"GDI (без фильтров)" };
            int active = d3d_ok ? 0 : gl_ok ? 1 : 2, same = C.renderer == active;
#else       /* no Direct3D here: OpenGL (ES on phones) or the plain output */
            static const wchar_t *rn[3] = { L"OpenGL", L"OpenGL", L"простой (без фильтров)" };
            int active = gl_ok ? 1 : 2, same = (C.renderer == 2) == (active == 2);
#endif
            static wchar_t rv[80];
            if (same) wcscpy(rv, rn[active]);
            else _snwprintf(rv, 79, L"%ls (после перезапуска)", rn[C.renderer]);
            add(L"Вывод изображения", rv);
        }
        add(L"Широкий экран в боях", C.ws_mode ? onoff(C.ws_battle) : L"нужен формат 16:10 или 16:9");
        add(L"Поля по бокам", fx_ok ? (C.backdrop ? L"размытая картинка" : L"чёрные") : L"чёрные");
        add(L"Заставки как на кинескопе", fx_ok ? onoff(C.cine) : L"нужен Direct3D 11 или OpenGL 2.0");
        { static const wchar_t *lv[3] = { L"выкл", L"мягкие", L"сильные" }, *gw[3] = { L"выкл", L"мягкое", L"сильное" },
              *lk[4] = { L"обычные", L"живые", L"кино", L"мягкие" };
          add(L"Тени под персонажами", fx_ok ? lv[C.fx_shadow] : L"нужен Direct3D 11 или OpenGL 2.0");
          add(L"Свечение ярких мест", fx_ok ? gw[C.fx_glow] : L"нужен Direct3D 11 или OpenGL 2.0");
          add(L"Цвета", fx_ok ? lk[C.fx_look] : L"нужен Direct3D 11 или OpenGL 2.0"); }
        add(L"HD-пакет графики", L"▸");
        add(L"Назад", 0);
        break; }
    case M_STATS: {
        wcscpy(m_title, L"Журнал");
        {   /* the current goal in a line */
            static wchar_t g[80]; int goal = story_goal();
            if (goal >= 0) { _snwprintf(g, 79, L"Цель: %ls", place_name(goal)); g[79] = 0; add_info(IC_COMPASS, g, 0); }
        }
        add_i(IC_COMPASS, L"Куда идти дальше", L"▸");
        add_i(IC_SWORD, L"Снаряжение получше: где взять", L"▸");
        add_i(IC_MAP, L"Карта мира (клавиша N)", L"▸");
        { static wchar_t a2[40]; int got = 0; for (int i = 0; i < NACH; i++) got += ach_time[i] != 0;
          _snwprintf(a2, 39, L"%d из %d  ▸", got, NACH); add_i(IC_TROPHY, L"Достижения", a2); }
        add_i(IC_CLOCK, L"Статистика", L"▸");
        add(L"Назад", 0);
        break; }
    case M_GEAR: {
        /* for each member and each kind of gear, the best one known: from the shops
         * already opened (the game's shop lists in the ROM) or the bag */
        wcscpy(m_title, L"Снаряжение получше: где взять");
        static wchar_t rows[48][96];
        int nr = 0, m = emu_game_mode_top();
        if (!emu_known_rom() || (m != 0x0C && m != 0x14)) { add_info(0, L"Совет появится, когда вы будете в игре.", 0); add(L"Назад", 0); break; }
        int cid[200], cplace[200], nc = 0;   /* candidates; place -2: the bag, -1: a shop of a map without a name */
        const uint8_t *r = emu_work_ram();
        for (int i = 0; i < 40 && nc < 200; i++) { int id = r[0xF410 + i]; if (id >= 1 && id <= 160) { cid[nc] = id; cplace[nc] = -2; nc++; } }
        int nshops = 0;
        for (int sh = 0; sh < 128; sh++) {
            if (!shop_map[sh]) continue;
            nshops++;
            int ids[32], k = shop_list_items(sh, ids, 32), pl = emu_place_of_map(shop_map[sh] - 1);
            for (int j = 0; j < k && nc < 200; j++) { cid[nc] = ids[j]; cplace[nc] = pl; nc++; }
        }
        static const wchar_t *kind[4] = { L"оружие", L"щит", L"шлем", L"броня" };
        int any = 0;
        for (int p = 0; p < 5; p++) {
            int rec = r[0xF40A + p];
            if (rec == 0xFF || rec > 10) break;
            wchar_t who[24]; ui_ps4_name(r + 0xF500 + rec * 0x80, who, 24);
            for (int kd = 0; kd < 4; kd++) {
                int best = -1, bestgain = 0, bestprice = 0;
                for (int c = 0; c < nc; c++) {
                    shop_t it;
                    if (!shop_item(cid[c], &it) || p >= it.nrow) continue;
                    int t = it.type, want = kd == 0 ? (t >= 1 && t <= 4) : t == 4 + kd;
                    if (!want || !it.row[p].can || it.row[p].same) continue;
                    int gain = kd == 0 ? it.row[p].datk : it.row[p].ddef;
                    int price = cplace[c] == -2 ? 0 : 1;
                    if (gain > bestgain || (gain == bestgain && gain > 0 && price < bestprice)) { best = c; bestgain = gain; bestprice = price; }
                }
                if (best < 0 || nr >= 47) continue;
                char nm8[64]; wchar_t nm[64]; shop_item_name(cid[best], nm8, 64); MultiByteToWideChar(65001, 0, nm8, -1, nm, 64);
                const wchar_t *where = cplace[best] == -2 ? L"уже в сумке — наденьте" : cplace[best] >= 0 ? place_name(cplace[best]) : L"в магазине, где вы были";
                _snwprintf(rows[nr], 95, L"%ls, %ls: %ls (+%d %ls) — %ls", who, kind[kd], nm, bestgain, kd == 0 ? L"к атаке" : L"к защите", where);
                rows[nr][95] = 0;
                add_info(kd == 0 ? IC_SWORD : kd == 1 ? IC_SHIELD : kd == 2 ? IC_HELM : IC_ARMOR, rows[nr], 0); nr++; any = 1;
            }
        }
        if (!any) add_info(0, nshops ? L"Лучше надетого ничего не нашлось в сумке и в магазинах, где вы были." : L"Пока не открыт ни один магазин: совет строится по магазинам, которые вы посетили.", 0);
        add_info(0, L"", 0);
        { static wchar_t t[96]; _snwprintf(t, 95, L"Учтены сумка и магазины, где вы открывали список покупок (%d).", nshops); t[95] = 0; add_info(0, t, 0); }
        add(L"Назад", 0);
        break; }
    case M_PLAY: {
        wcscpy(m_title, L"Статистика");
        static wchar_t a[80], b2[80], c[80], d[80], e[80];
        fmt_time(a, 79, ST.play_frames); add_info(IC_CLOCK, L"Время в игре (всего)", a);
        fmt_time(b2, 79, session_frames); add_info(IC_CLOCK, L"За этот запуск", b2);
        _snwprintf(c, 79, L"%llu", (unsigned long long)ST.battles);
        add_info(IC_SWORDS, L"Боёв", c);
        static wchar_t bt[80]; fmt_time(bt, 79, ST.battle_frames);
        add_info(IC_CLOCK, L"Время в боях", bt);
        int m = emu_game_mode_top();
        if (m == 0x0C || m == 0x14) _snwprintf(d, 79, L"%lu", (unsigned long)emu_meseta()); else wcscpy(d, L"—");
        add_info(IC_COIN, L"Месеты сейчас", d);
        _snwprintf(e, 79, L"%llu / %llu", (unsigned long long)ST.saves, (unsigned long long)ST.loads);
        add_info(IC_SAVE, L"Быстрых сохранений / загрузок", e);
        add(L"Назад", 0);
        break; }
    case M_PARTY: {
        wcscpy(m_title, L"Отряд");
        const uint8_t *r = emu_work_ram();
        int m = emu_game_mode_top(), n = 0;
        static wchar_t lab[5][40], val[5][60];
        for (int k = 0; k < 5 && (m == 0x0C || m == 0x14); k++) {
            int rec = r[0xF40A + k];
            if (rec == 0xFF || rec > 10) break;
            const uint8_t *c = r + 0xF500 + rec * 0x80;
            ui_ps4_name(c, lab[k], 40);
            m_icon = 2000 + rec;   /* the portrait */
            party_row[k][0] = c[8] << 8 | c[9]; party_row[k][1] = (int16_t)(c[14] << 8 | c[15]); party_row[k][2] = c[16] << 8 | c[17];
            party_row[k][3] = (int16_t)(c[18] << 8 | c[19]); party_row[k][4] = c[20] << 8 | c[21];
            val[k][0] = 0;
            add(lab[k], val[k]); n++;
        }
        if (!n) add(L"Отряд появится, когда вы будете в игре.", 0);
        add(L"Назад", 0);
        break; }
    case M_CHAR: build_char_card(); break;
    case M_GOAL: {
        wcscpy(m_title, L"Куда идти дальше");
        static wchar_t rows[14][80], dir[120];
        int goal; const wchar_t *t = story_hint_g(&goal);
        int nr = 0, len = (int)wcslen(t), pos = 0;
        while (pos < len && nr < 12) {
            int take = len - pos < 60 ? len - pos : 60;
            if (pos + take < len) { int k = take; while (k > 30 && t[pos + k] != L' ') k--; if (k > 30) take = k; }
            wcsncpy(rows[nr], t + pos, (size_t)take); rows[nr][take] = 0;
            add_info(0, rows[nr], 0); nr++;
            pos += take; while (pos < len && t[pos] == L' ') pos++;
        }
        add_info(0, L"", 0);
        if (goal >= 0 && goal_direction(goal, dir, 120)) add_info(IC_COMPASS, dir, 0);
        add_info(0, L"Подсказка по составу отряда и ключевым предметам (прохождение PSCave).", 0);
        add_i(IC_MAP, goal >= 0 ? L"Показать цель на карте мира" : L"Карта мира", L"▸");
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
            add_i(b.machine ? IC_GEAR : IC_SKULL, lab[i], val[i]);
            items[nitems - 1].icon = 1000 + ids[m_best_page * 10 + i];   /* its picture, once seen in battle */
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
            else _snwprintf(lab, 39, L"Слот %d%ls", k, k == C.slot ? L"  (F5/F8)" : L"");
            add(lab, slot_info[k].valid ? slot_info[k].date : L"пусто");
        }
        add(L"Назад", 0);
        break; }
    case M_HD: {
        wcscpy(m_title, L"HD-пакет графики");
        static wchar_t v1[80], v2[80];
        if (!fx_ok) wcscpy(v1, L"нужен Direct3D 11 или OpenGL 2.0");
        else if (!hd_tiles()) wcscpy(v1, L"пакет пуст");
        else _snwprintf(v1, 79, L"%ls (плиток: %d, ×%d)", C.hd_pack ? L"вкл" : L"выкл", hd_tiles(), hd_scale());
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
        add(L"Панель отряда (портреты, ЕЖ, ЕТ)", onoff(C.party_hud));
        add(L"Кнопки быстрого доступа", onoff(C.quickbar));
        add(L"Показывать FPS и время кадра", onoff(C.show_fps));
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

static int item_y[64], item_h, item_first, item_count, item_x0, item_x1;
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
            if (ls == 0) _snwprintf(subs[0], 59, L"Автосохранение · %ls", slot_info[ls].date);
            else _snwprintf(subs[0], 59, L"Слот %d · %ls", ls, slot_info[ls].date);
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
    l_text(lx + (int)(8 * u), logo_bottom, GAME_NAME, RGBC(255, 222, 140), l_font_card);
    logo_bottom += (int)(30 * u);
#ifdef __ANDROID__
    l_text(lx + (int)(8 * u), logo_bottom, L"НАТИВНАЯ ВЕРСИЯ ДЛЯ ANDROID · " APP_VERSION_W, RGBC(159, 176, 255), l_font_small);
#else
    l_text(lx + (int)(8 * u), logo_bottom, L"НАТИВНАЯ ВЕРСИЯ ДЛЯ ПК · " APP_VERSION_W, RGBC(159, 176, 255), l_font_small);
#endif
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
    music_play(0);
    l_sel = latest_slot() >= 0 ? 0 : 2;
    l_opened = now_ms();
}

static void launcher_start_game(void)
{
    launcher_open = 0;
    music_stop();   /* fades out over the game's own sound */
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
    int lh = m_page == M_PARTY ? font_h * 3 : font_h * 13 / 10;
    int hdr = font_h * 13 / 10 * 2;   /* the title's room */
    int pw = back_w * 3 / 4, ph = lh * nitems + hdr + font_h;
    if (ph > back_h - 20) ph = back_h - 20;
    int px = (back_w - pw) / 2, py = (back_h - ph) / 2;
    menu_rect[0] = px - 3; menu_rect[1] = py - 3; menu_rect[2] = pw + 6; menu_rect[3] = ph + 6;
    int lw = m_page == M_SAVE ? pw * 11 / 20 : pw;   /* the save list leaves room for the picture */
    item_h = lh; item_x0 = px; item_x1 = px + lw; item_count = 0;
    fill(px - 3, py - 3, pw + 6, ph + 6, RGBC(170, 180, 220));
    fill(px, py, pw, ph, RGBC(16, 28, 74));
    if (m_page == M_SAVE) {
        int slot = (m_save_load ? 0 : 1) + m_sel;
        int ax = px + lw + font_h / 2, aw = pw - lw - font_h, ay = py + hdr, ah = ph - hdr - font_h / 2;
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
    int first = 0, rows = (ph - hdr) / lh;
    if (m_sel >= rows) first = m_sel - rows + 1;
    item_first = first;
    for (int i = first; i < nitems && i - first < rows && item_count < 64; i++) {
        int yy = py + hdr + (i - first) * lh;
        item_y[item_count++] = yy;
        if (i == m_sel && !items[i].info) fill(px + font_h / 2, yy - 2, lw - font_h, lh, RGBC(60, 90, 170));
        int tx = px + font_h;
        if (items[i].icon >= 2000) {   /* a character's portrait, and level, HP and TP as bars */
            int isz = lh - 6;
            const uint32_t *pic = portrait_pixels(items[i].icon - 2000);
            fill(tx - 2, yy - 2, isz + 4, isz + 4, i == m_sel ? RGBC(214, 176, 92) : RGBC(92, 104, 150));
            if (pic) put_portrait(pic, tx, yy, isz, party_row[i][1] <= 0); else fill(tx, yy, isz, isz, RGBC(40, 46, 80));
            tx += isz + font_h / 2;
            draw_text(tx, yy + lh / 2 - font_h, items[i].label, i == m_sel ? RGBC(255, 255, 160) : RGBC(225, 230, 255), font_big);
            if (i < 5) {
                const int *v = party_row[i];
                wchar_t t[40];
                _snwprintf(t, 39, L"уровень %d", v[0]); t[39] = 0;
                draw_text(tx, yy + lh / 2 + 1, t, RGBC(255, 220, 130), font_small);
                int bx = px + lw / 2, bw = lw / 2 - font_h * 3, bh = font_h * 3 / 4, by1 = yy + lh / 2 - bh - 2, by2 = yy + lh / 2 + 3;
                for (int b = 0; b < 2; b++) {
                    int cur = b ? v[3] : v[1], mx = b ? v[4] : v[2], by = b ? by2 : by1;
                    if (cur < 0) cur = 0; if (cur > mx) cur = mx;
                    const wchar_t *lab = b ? L"ЕТ" : L"ЕЖ";
                    draw_text(bx - text_w(lab, font_small) - font_h / 3, by - 2, lab, RGBC(200, 208, 236), font_small);
                    fill(bx, by, bw, bh, b ? RGBC(22, 28, 60) : RGBC(48, 20, 24));
                    DWORD col = b ? RGBC(86, 150, 255) : cur * 4 <= mx ? RGBC(200, 58, 48) : cur * 2 <= mx ? RGBC(206, 160, 52) : RGBC(62, 160, 78);
                    if (mx > 0) fill(bx, by, bw * cur / mx, bh, col);
                    _snwprintf(t, 39, L"%d / %d", cur, mx); t[39] = 0;
                    draw_text(bx + (bw - text_w(t, font_small)) / 2, by - 2, t, RGBC(255, 255, 255), font_small);
                }
                draw_text(px + lw - font_h * 3 / 2, yy + lh / 2 - font_h / 2, L"▸", RGBC(160, 220, 255), font_small);
            }
            continue;
        } else if (items[i].icon >= 1000) {   /* a monster's picture (the bestiary), or a skull until one is taken */
            int isz = lh;
            if (!draw_thumb(tx, yy - 2, isz, isz, items[i].icon - 1000)) draw_icon(tx + (isz - font_h) / 2, yy + (lh - font_h) / 2, font_h * 9 / 10, IC_SKULL, 1);
            tx += isz + font_h / 2;
        } else if (items[i].icon) { int isz = font_h * 9 / 10; draw_icon(tx, yy + (lh - isz) / 2 - 2, isz, items[i].icon, 0); tx += isz + font_h / 2; }
        draw_text(tx, yy + (lh - font_h * 13 / 10) / 2, items[i].label, items[i].info ? RGBC(200, 208, 236) : i == m_sel ? RGBC(255, 255, 160) : RGBC(225, 230, 255), font_small);
        if (items[i].value[0]) {
            int tw = text_w(items[i].value, font_small);
            draw_text(px + lw - font_h - tw, yy, items[i].value, RGBC(160, 220, 255), font_small);
        }
    }
}

static void menu_set_open(int o)
{
    menu_open = o; m_page = M_MAIN; m_sel = 0; m_wait_bind = -1; m_confirm_quit = 0; m_char_from_panel = 0;
    if (!o) cfg_save();
}

static void apply_video(void) { emu_set_widescreen(ws_ext_for_mode[C.ws_mode]); emu_set_widescreen_battles(C.ws_battle); }

/* journal items: shifted by the goal line when there is one */
static int journal_sel(int k) { return k + (story_goal() >= 0); }
/* the world map centred on the goal of "where next" */
static void world_show_goal(void)
{
    static uint32_t img[512 * 1024];
    int w, h, mx, my, planet = emu_world_image(img, 512, 1024, &w, &h, &mx, &my), goal = story_goal();
    if (!planet || goal < 0) return;
    int place[48], tx[48], ty[48], nt = emu_world_towns(planet - 1, place, tx, ty, 48);
    for (int i = 0; i < nt; i++) if (place[i] == goal) { world_cx = tx[i]; world_cy = ty[i]; world_zoom = 2; }
}

/* dir: 0 activate, -1/+1 left/right */
static void menu_act(int dir)
{
    build_menu();
    int i = m_sel;
    logw("menu page %d item %d %s", m_page, i, dir ? (dir < 0 ? "left" : "right") : "select");
    if (m_page == M_MAIN) {
        if (i != 9) m_confirm_quit = 0;
        switch (i) {
        case 0: if (!dir) menu_set_open(0); break;
        case 1: case 2:
            if (!dir) { slots_refresh(); m_save_load = i == 2; m_page = M_SAVE; m_sel = m_save_load ? C.slot : C.slot - 1; }
            break;
        case 3: if (!dir) { m_page = M_SET; m_sel = 0; } break;
        case 4: if (!dir) { m_page = M_PARTY; m_sel = 0; } break;
        case 5: if (!dir) { m_page = M_STATS; m_sel = journal_sel(0); } break;
        case 6: if (!dir) { m_page = M_BEST; m_best_page = 0; m_sel = 0; } break;
        case 7: if (!dir) { m_page = M_HELP; m_sel = 0; } break;
        case 8: if (!dir) { menu_set_open(0); launcher_reopen(); } break;
        case 9: if (!dir) { if (m_confirm_quit) quit = 1; else m_confirm_quit = 1; } break;
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
        case 5: C.party_hud = !C.party_hud; break;
        case 6: C.quickbar = !C.quickbar; break;
        case 7: C.show_fps = !C.show_fps; break;
        case 8: if (!dir) { m_page = M_SET; m_sel = 3; } break;
        }
    } else if (m_page == M_VIDEO) {
        int d = dir ? dir : 1;
        switch (i) {
        case 0: C.ws_mode = (C.ws_mode + 3 + d) % 3; apply_video(); fit_window(); break;
        case 1: C.fx_mode = (C.fx_mode + FX_COUNT + d) % FX_COUNT; C.hd = C.fx_mode == FX_SMOOTH; break;
        case 2: C.scale_mode = (C.scale_mode + 3 + d) % 3; break;
        case 3: C.smooth = !C.smooth; break;
        case 4: C.fx_dither = !C.fx_dither; break;
        case 5:
#ifndef __ANDROID__
            toggle_fullscreen();
#endif
            break;
        case 6: C.vsync = !C.vsync; break;
        case 7:
#ifdef _WIN32
            C.renderer = (C.renderer + 3 + d) % 3;
#else
            C.renderer = C.renderer == 2 ? 0 : 2;
#endif
            C.use_gl = C.renderer != 2; set_status(L"вступит в силу после перезапуска"); break;
        case 8: C.ws_battle = !C.ws_battle; apply_video(); break;
        case 9: C.backdrop = !C.backdrop; break;
        case 10: C.cine = !C.cine; emu_set_cutscene_detect(C.cine); break;
        case 11: C.fx_shadow = (C.fx_shadow + 3 + d) % 3; break;
        case 12: C.fx_glow = (C.fx_glow + 3 + d) % 3; break;
        case 13: C.fx_look = (C.fx_look + 4 + d) % 4; break;
        case 14: if (!dir) { m_page = M_HD; m_sel = 0; } break;
        case 15: if (!dir) { m_page = M_SET; m_sel = 0; } break;
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
        if (!dir) { m_page = M_MAIN; m_sel = 7; }
    } else if (m_page == M_STATS) {
        int o = story_goal() >= 0;   /* the goal line on top */
        if (!dir && i == o + 0) { m_page = M_GOAL; m_sel = 0; }
        if (!dir && i == o + 1) { m_page = M_GEAR; m_sel = 0; }
        if (!dir && i == o + 2) { menu_set_open(0); world_open_map(); }
        if (!dir && i == o + 3) { m_page = M_ACH; m_sel = NACH; }
        if (!dir && i == o + 4) { m_page = M_PLAY; m_sel = 0; }
        if (!dir && i == o + 5) { m_page = M_MAIN; m_sel = 5; }
    } else if (m_page == M_PLAY) {
        if (!dir) { m_page = M_STATS; m_sel = journal_sel(4); }
    } else if (m_page == M_GEAR) {
        if (!dir) { m_page = M_STATS; m_sel = journal_sel(1); }
    } else if (m_page == M_PARTY) {
        if (!dir) { if (i == nitems - 1) { m_page = M_MAIN; m_sel = 4; } else { m_char = i; m_char_from_panel = 0; m_page = M_CHAR; m_sel = 0; } }
    } else if (m_page == M_CHAR) {
        if (!dir) char_card_act(i);
    } else if (m_page == M_GOAL) {
        if (!dir && i == nitems - 2) { menu_set_open(0); world_open_map(); world_show_goal(); }
        else if (!dir) { m_page = M_STATS; m_sel = journal_sel(0); }
    } else if (m_page == M_ACH) {
        if (!dir && i == NACH) { m_page = M_STATS; m_sel = journal_sel(3); }
    } else if (m_page == M_BEST) {
        if (dir || i == m_best_next) { if (m_best_next < 0) return; m_best_page += dir < 0 ? -1 : 1; if (m_best_page < 0) m_best_page = 0; build_menu(); if (m_sel >= nitems) m_sel = nitems - 1; }
        else if (!dir && i == nitems - 1) { m_page = M_MAIN; m_sel = 6; }
        else if (!dir && i < 10 && (m_best_next < 0 ? i < nitems - 1 : i < m_best_next)) {   /* details of one monster */
            best_info_t b;
            if (emu_bestiary(m_best_ids[i], &b)) {
                wchar_t nm[48], t[400]; MultiByteToWideChar(65001, 0, b.name, -1, nm, 48);
                wchar_t wk[260], rs[400];
                MultiByteToWideChar(65001, 0, b.weak, -1, wk, 260); MultiByteToWideChar(65001, 0, b.resist, -1, rs, 400);
                _snwprintf(t, 399, L"%ls%ls: атака %d, защита %d, ловкость %d%ls%ls%ls%ls", nm, b.machine ? L" (механизм)" : L"",
                           b.attack, b.defence, b.agility, wk[0] ? L"; слаб: " : L"", wk, rs[0] ? L"; устойчив: " : L"", rs);
                t[399] = 0; set_status(t);
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
        case 6: if (!dir) { m_page = M_VIDEO; m_sel = 14; } break;
        }
    }
}

static void menu_back(void)
{
    if (m_wait_bind >= 0) { m_wait_bind = -1; return; }
    if (m_page == M_MAIN) menu_set_open(0);
    else if (m_page == M_HD) { m_page = M_VIDEO; m_sel = 14; }
    else if (m_page == M_BEST) { m_page = M_MAIN; m_sel = 6; }
    else if (m_page == M_PARTY) { m_page = M_MAIN; m_sel = 4; }
    else if (m_page == M_CHAR) { if (m_char_from_panel) menu_set_open(0); else { m_page = M_PARTY; m_sel = m_char; } }
    else if (m_page == M_ACH) { m_page = M_STATS; m_sel = journal_sel(3); }
    else if (m_page == M_GOAL) { m_page = M_STATS; m_sel = journal_sel(0); }
    else if (m_page == M_PLAY) { m_page = M_STATS; m_sel = journal_sel(4); }
    else if (m_page == M_GEAR) { m_page = M_STATS; m_sel = journal_sel(1); }
    else if (m_page == M_VIDEO || m_page == M_SOUND || m_page == M_SPEED || m_page == M_HINTS || m_page == M_KEYS || m_page == M_PAD) {
        int from = m_page; m_page = M_SET;
        m_sel = from == M_VIDEO ? 0 : from == M_SOUND ? 1 : from == M_SPEED ? 2 : from == M_HINTS ? 3 : from == M_KEYS ? 4 : 5;
    }
    else { int from = m_page; m_page = M_MAIN;
           m_sel = from == M_SET ? 3 : from == M_SAVE ? (m_save_load ? 2 : 1) : from == M_STATS ? 5 : 7; }
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
    case VK_UP: menu_step(-1); break;
    case VK_DOWN: menu_step(1); break;
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
    if (ppressed(A_UP)) menu_step(-1);
    if (ppressed(A_DOWN)) menu_step(1);
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
    _snwprintf(name, 799, L"%ls\\PS4_%04d%02d%02d_%02d%02d%02d.png", dir, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
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
    if (world_open) {
        if (vk == 0xBB || vk == 0x6B) world_zoom_by(1, -1, -1);
        else if (vk == 0xBD || vk == 0x6D) world_zoom_by(-1, -1, -1);
        else if (vk == 0x24) world_reset_view();
        else if (vk >= '1' && vk <= '3') world_pick(vk - '1');
        return;
    }
    if (vk == 'N') {   /* world map screen (unless N is bound to something) */
        int bound = 0;
        for (int a = 0; a < NACT; a++) for (int i = 0; i < NKEY; i++) if (C.keys[a][i] == 'N') bound = 1;
        if (!bound) { world_open_map(); return; }
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
        if (my >= item_y[k] - 2 && my < item_y[k] - 2 + item_h) return items[item_first + k].info ? -1 : item_first + k;
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
/* the frame rate and where the time goes (setting "Показывать FPS") */
static double perf_emu, perf_out, perf_shown_emu, perf_shown_out; static int perf_frames, perf_fps; static uint64_t perf_t0;
static double perf_now(void) { static int64_t f; int64_t c; if (!f) QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (double)c * 1000.0 / (double)f; }
static void perf_frame(double emu_ms, double out_ms)
{
    perf_emu += emu_ms; perf_out += out_ms; perf_frames++;
    uint64_t t = now_ms();
    if (!perf_t0) perf_t0 = t;
    if (t - perf_t0 >= 1000) {
        perf_fps = (int)(perf_frames * 1000 / (t - perf_t0));
        perf_shown_emu = perf_emu / perf_frames; perf_shown_out = perf_out / perf_frames;
        perf_emu = perf_out = 0; perf_frames = 0; perf_t0 = t;
    }
}
static void draw_fps(void)
{
    if (!C.show_fps || !perf_fps) return;
    wchar_t t[120]; _snwprintf(t, 119, L"%d кадр/с · эмуляция %.1f мс · вывод %.1f мс", perf_fps, perf_shown_emu, perf_shown_out); t[119] = 0;
    int tw = text_w(t, font_small) + font_h, th = font_h + 4, x = (back_w - tw) / 2, y = 2;
    fill(x, y, tw, th, RGBC(0, 0, 0));
    draw_text(x + font_h / 2, y + 2, t, perf_fps >= 55 ? RGBC(140, 255, 150) : perf_fps >= 40 ? RGBC(255, 220, 120) : RGBC(255, 130, 120), font_small);
    fps_rect[0] = x; fps_rect[1] = y; fps_rect[2] = tw; fps_rect[3] = th;
}

static void game_frames(int fast)
{
    double perf_a = perf_now();
    g_hd_track = hd_on() || C.hd_auto;
    emu_set_pad(0, game_pad());
    int frames = fast ? C.turbo_speed : 1;
    /* battle speed-up: not while the game waits for orders ($FF4109 non-zero), or the
     * cursor in its menus would run away (the round itself is played at speed) */
    if (!fast && C.battle_speed > 1 && emu_in_battle() && emu_work_ram()[0x4109] == 0) frames = C.battle_speed;
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
        turns_track();
        if ((frame_no & 15) == 0) shops_track();
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
    double perf_b = perf_now();
    present();
    perf_frame(perf_b - perf_a, perf_now() - perf_b);
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

static void quickbar_act(int k)
{
    switch (k) {
    case 0: menu_set_open(1); m_page = M_STATS; m_sel = journal_sel(0); break;
    case 1: menu_set_open(1); m_page = M_BEST; m_best_page = 0; m_sel = 0; break;
    case 2: world_open_map(); break;
    case 3: menu_set_open(1); m_page = M_GOAL; m_sel = 0; break;
    case 4: menu_set_open(1); m_page = M_PARTY; m_sel = 0; break;
    case 5: menu_set_open(1); slots_refresh(); m_save_load = 0; m_page = M_SAVE; m_sel = C.slot - 1; break;
    case 6: menu_set_open(1); break;
    }
}

/* the "running" marker (see Start) and the phone's background: Android ends
 * a program in the background without warning, so everything a clean exit
 * writes is written when the app goes there (port/sdl_win32.c posts these) */
#define WM_APP_SUSPEND 0x8001
#define WM_APP_RESUME 0x8002
static wchar_t g_marker[700];
static void app_suspend(void)
{
    sram_write();
    if (C.autosave_min && !launcher_open) state_save(0, 1);
    stats_save(); best_save(); fog_save(); world_save(); cfg_save();
    DeleteFileW(g_marker);
    logw("suspended: everything saved");
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
        g_mouse_x = mx; g_mouse_y = my;
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
        else if (world_open) world_click(mx, my);
        else if (quickbar_hit(mx, my) >= 0) quickbar_act(quickbar_hit(mx, my));
        else if (!world_open)
            for (int k = 0; k < 5; k++) {   /* a portrait of the party panel: that character's card */
                int *pr = party_rect[k];
                if (pr[2] > 0 && mx >= pr[0] && mx < pr[0] + pr[2] && my >= pr[1] && my < pr[1] + pr[3]) {
                    menu_set_open(1); m_page = M_CHAR; m_char = k; m_char_from_panel = 1; m_sel = 0; break;
                }
            }
        return 0;
    case WM_RBUTTONDOWN:
        if (menu_open) menu_back(); else if (launcher_open) launcher_back(); else menu_set_open(1);
        return 0;
    case WM_MOUSEWHEEL:
        if (menu_open && m_wait_bind < 0) {
            build_menu();
            int d = (short)((w >> 16) & 0xFFFF);
            menu_step(d > 0 ? -1 : 1);
        } else if (world_open) world_zoom_by((short)((w >> 16) & 0xFFFF) > 0 ? 1 : -1, g_mouse_x, g_mouse_y);
        return 0;
    case WM_ENTERSIZEMOVE: in_sizemove = 1; SetTimer(h, 1, 8, 0); return 0;
    case WM_EXITSIZEMOVE: in_sizemove = 0; KillTimer(h, 1); return 0;
    case WM_TIMER: if (in_sizemove && h == hwnd) sizemove_tick(); return 0;
    case WM_CLOSE: quit = 1; return 0;
    case WM_APP_SUSPEND: app_suspend(); return 0;
    case WM_APP_RESUME: { FILE *mf = _wfopen(g_marker, L"w"); if (mf) fclose(mf); } return 0;
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
    diag_init(exe_dir, "0.48");
    diag_info = settings_info;
    logw("start, version 0.48");
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
    wcscpy(g_marker, marker);

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
    music_open();
    if (launcher_open) music_play(0);
    emu_set_audio_tone(C.tone);
    emu_set_cutscene_detect(C.cine);
    emu_set_conveniences(C.encounters, C.exp_mul, C.mes_mul);
    stats_load();
    best_load();
    ach_load();
    fog_load(); emu_set_fog(C.fog); world_load(); shops_load();
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
            if (ppressed(A_LEFT)) world_pan(-1, 0); if (ppressed(A_RIGHT)) world_pan(1, 0);
            if (ppressed(A_UP)) world_pan(0, -1); if (ppressed(A_DOWN)) world_pan(0, 1);
            if (ppressed(A_A)) world_zoom_by(1, -1, -1); if (ppressed(A_C)) world_zoom_by(-1, -1, -1);
            if (ppressed(A_START)) for (int k = 1; k <= 3; k++) { int pl = (world_planet + k) % 3; if (emu_world_known(pl)) { world_pick(pl); break; } }
            present();
            Sleep(16);
            pace_reset();
            continue;
        }
        if (launcher_open || menu_open) {
            if (launcher_open) music_feed();
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
