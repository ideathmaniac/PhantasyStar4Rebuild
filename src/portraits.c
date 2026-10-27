/*
 * Character portraits for the party panel, read from the ROM in use (nothing
 * from the game is built into the program). The game keeps 40 dialogue
 * portraits, 6x6 tiles (48x48) each, Nemesis-compressed, behind a table of
 * pointers at ROM $06A5B2 (entry 0 empty; entries 1-11 are the playable
 * characters in record order: Chaz, Alys, Hahn, Rune, Gryz, Rika, Demi,
 * Wren, Raja, Kyra, Seth). They share one 16-colour palette, at ROM $296300
 * (the game copies it to palette line 2 for dialogues).
 */
#include <string.h>
#include <stdlib.h>
#include "md.h"
#include "nemesis.h"
#include "portraits.h"

#define TABLE 0x06A5B2u
#define PALETTE 0x296300u
#define NPORT 40

static uint32_t pix[NPORT][PORTRAIT_W * PORTRAIT_W];
static int ok[NPORT], loaded = -1;

static uint32_t rd32(uint32_t a) { return (uint32_t)g_rom[a] << 24 | (uint32_t)g_rom[a + 1] << 16 | (uint32_t)g_rom[a + 2] << 8 | g_rom[a + 3]; }

static void load(void)
{
    loaded = 0;
    memset(ok, 0, sizeof ok);
    if (g_rom_size < PALETTE + 32) return;
    uint32_t pal[16];
    for (int i = 0; i < 16; i++) {
        int c = g_rom[PALETTE + i * 2] << 8 | g_rom[PALETTE + i * 2 + 1];
        int r = (c & 0xE) * 18, g = ((c >> 4) & 0xE) * 18, b = ((c >> 8) & 0xE) * 18;
        pal[i] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
    }
    static uint8_t tiles[36 * 32 + 64];
    int good = 0;
    for (int i = 1; i < NPORT; i++) {
        uint32_t p = rd32(TABLE + 4 * i);
        if (p < 0x200 || p >= g_rom_size) continue;
        size_t n = nemesis_decode(g_rom + p, g_rom_size - p, tiles, sizeof tiles, 0);
        if (n != 36 * 32) continue;
        for (int k = 0; k < 36; k++)
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    int v = (tiles[k * 32 + y * 4 + x / 2] >> (x & 1 ? 0 : 4)) & 15;
                    pix[i][((k / 6) * 8 + y) * PORTRAIT_W + (k % 6) * 8 + x] = pal[v];
                }
        ok[i] = 1; good++;
    }
    loaded = good >= 11;   /* the table is not where expected: another ROM */
}

const uint32_t *portrait_pixels(int rec)
{
    if (loaded < 0) load();
    int i = rec + 1;
    if (!loaded || rec < 0 || i >= NPORT || !ok[i]) return 0;
    return pix[i];
}

void portraits_reset(void) { loaded = -1; }
