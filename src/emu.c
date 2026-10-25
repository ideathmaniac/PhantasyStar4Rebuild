#include <string.h>
#include "emu.h"
#include "cpu.h"
#include "run.h"
#include "sound.h"

const char EMU_ROM_SHA1[] = "d2820fd0103de2f98b9f742dd076c8897ddd74d0";
#ifdef PS4_RECOMP
void ps4_recomp_install(void);
#endif
void ps4_ws_install(void);

/* ---- SHA-1 ---- */
static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
void emu_sha1_hex(const uint8_t *data, size_t n, char out[41])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint64_t bits = (uint64_t)n * 8;
    size_t total = ((n + 8) / 64 + 1) * 64;
    for (size_t off = 0; off < total; off += 64) {
        uint8_t blk[64];
        for (int i = 0; i < 64; i++) {
            size_t p = off + i;
            if (p < n) blk[i] = data[p];
            else if (p == n) blk[i] = 0x80;
            else if (p >= total - 8) blk[i] = (uint8_t)(bits >> (8 * (total - 1 - p)));
            else blk[i] = 0;
        }
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = (blk[i * 4] << 24) | (blk[i * 4 + 1] << 16) | (blk[i * 4 + 2] << 8) | blk[i * 4 + 3];
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    static const char hx[] = "0123456789abcdef";
    for (int i = 0; i < 20; i++) { uint8_t v = (uint8_t)(h[i / 4] >> (24 - 8 * (i % 4))); out[i * 2] = hx[v >> 4]; out[i * 2 + 1] = hx[v & 15]; }
    out[40] = 0;
}

static int rom_kind;

int emu_load_rom(const uint8_t *data, size_t size)
{
    if (size < 0x200 || size > sizeof g_rom) return EMU_ROM_BAD;
    if (memcmp(data + 0x100, "SEGA", 4) != 0 && memcmp(data + 0x101, "SEGA", 4) != 0) return EMU_ROM_BAD;
    memset(g_rom, 0xFF, sizeof g_rom);
    memcpy(g_rom, data, size);
    g_rom_size = (uint32_t)size;
    char h[41];
    emu_sha1_hex(data, size, h);
    rom_kind = strcmp(h, EMU_ROM_SHA1) == 0 ? EMU_ROM_RECOMPILED : EMU_ROM_INTERPRETED;
    cpu_lookup = 0;
    if (rom_kind == EMU_ROM_RECOMPILED) {
        /* the known ROM: its own features (widescreen, battles, story pictures)
         * work with or without the recompiled code; without it (the build
         * that ships no game code) the interpreter runs everything */
#ifdef PS4_RECOMP
        ps4_recomp_install();
#endif
        ps4_ws_install();
        { extern int g_ws_probe; g_ws_probe = 1; }
    }
    return rom_kind;
}

void emu_reset(void)
{
    uint8_t keep[sizeof g_sram];
    memcpy(keep, g_sram, sizeof keep);       /* battery RAM survives a reset */
    run_reset();
    memcpy(g_sram, keep, sizeof keep);
    g_snd_enabled = 1;
}

void emu_set_pad(int port, uint16_t b) { if (port >= 0 && port < 2) g_pad[port] = b; }
void emu_run_frame(void) { run_frame(); }
const uint32_t *emu_framebuffer(int *w, int *h) { *w = g_fb_width; *h = MD_VIS_LINES; return &g_fb[0][0]; }   /* pitch MD_W_MAX */
int emu_audio(int16_t *out, int max) { return snd_take(out, max); }

#define STATE_MAGIC 0x34535031u   /* "1PS4" */
size_t emu_state_size(void) { return 8 + sizeof R + md_state_size(); }
void emu_state_save(uint8_t *out)
{
    uint32_t m = STATE_MAGIC, sz = (uint32_t)emu_state_size();
    memcpy(out, &m, 4); memcpy(out + 4, &sz, 4);
    memcpy(out + 8, &R, sizeof R);
    md_state_save(out + 8 + sizeof R);
}
int emu_state_load(const uint8_t *in, size_t n)
{
    uint32_t m, sz;
    if (n < 8) return 0;
    memcpy(&m, in, 4); memcpy(&sz, in + 4, 4);
    if (m != STATE_MAGIC || sz != emu_state_size() || n < sz) return 0;
    memcpy(&R, in + 8, sizeof R);
    md_state_load(in + 8 + sizeof R);
    return 1;
}

void emu_set_widescreen(int ext)
{
    if (rom_kind != EMU_ROM_RECOMPILED) ext = 0;
    g_ws_ext = ext < 0 ? 0 : ext > 64 ? 64 : ext;
    if (rom_kind == EMU_ROM_RECOMPILED) { void ps4_ws_patch_rom(int); ps4_ws_patch_rom(g_ws_ext); }
}

int emu_has_recompiled_code(void)
{
#ifdef PS4_RECOMP
    return 1;
#else
    return 0;
#endif
}

void emu_set_widescreen_battles(int on) { extern int g_ws_battle; g_ws_battle = on; }
int emu_game_mode_top(void) { return (g_ram[0xEF00] << 8) | g_ram[0xEF01]; }
uint32_t emu_meseta(void) { return ((uint32_t)g_ram[0xF438] << 24) | ((uint32_t)g_ram[0xF439] << 16) | ((uint32_t)g_ram[0xF43A] << 8) | g_ram[0xF43B]; }
void emu_set_cutscene_detect(int on) { extern int g_ws_probe; if (on) g_ws_probe = 1; }   /* already on for the recompiled ROM (emu_on_field) */
int emu_is_cutscene(void) { extern int g_ws_cutscene; return rom_kind == EMU_ROM_RECOMPILED && g_ws_cutscene; }
int emu_in_battle(void) { return ((g_ram[0xEF00] << 8) | g_ram[0xEF01]) == 0x14; }   /* top-level game mode ($0003DC) */
void emu_set_audio_tone(int level) { void snd_set_tone(int); snd_set_tone(level); }
int emu_game_mode(void) { return (g_ram[0xEC20] << 8) | g_ram[0xEC21]; }
uint32_t emu_cpu_pc(void) { extern uint32_t cpu_cur_pc; return cpu_cur_pc; }
int emu_on_field(void) { extern int ps4_field_live; return ps4_field_live; }

uint8_t *emu_sram(size_t *size) { *size = sizeof g_sram; return g_sram; }
int emu_sram_dirty(void) { int d = g_sram_dirty; g_sram_dirty = 0; return d; }
