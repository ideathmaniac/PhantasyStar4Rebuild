/* emu API test: save-state round trip must reproduce the same frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/emu.h"
#include "../src/md.h"

static uint32_t frame_crc(void)
{
    int w, h; const uint32_t *fb = emu_framebuffer(&w, &h);
    return md_crc32(fb, sizeof(uint32_t) * 320 * h) ^ md_crc32(g_ram, sizeof g_ram);
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    static uint8_t rom[0x400000];
    size_t n = fread(rom, 1, sizeof rom, f); fclose(f);
    int k = emu_load_rom(rom, n);
    printf("rom kind %d (1 = recompiled)\n", k);
    emu_reset();
    /* get into the game: Start at 1300, then C presses */
    for (int i = 0; i < 2600; i++) {
        uint16_t p = 0;
        if (i >= 1300 && i < 1310) p = PAD_START;
        if (i >= 1500 && i < 1510) p = PAD_START;
        if (i > 1600 && (i % 100) < 8) p = PAD_C;
        emu_set_pad(0, p);
        emu_run_frame();
        int16_t a[4096]; while (emu_audio(a, 2048) > 0) {}
    }
    size_t ss = emu_state_size();
    uint8_t *st = malloc(ss);
    emu_state_save(st);
    uint32_t c1[600], c2[600];
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1 && !emu_state_load(st, ss)) { printf("load failed\n"); return 1; }
        for (int i = 0; i < 600; i++) {
            uint16_t p = (i % 40) < 20 ? PAD_RIGHT : PAD_DOWN;
            if (i % 90 == 5) p = PAD_C;
            emu_set_pad(0, p);
            emu_run_frame();
            int16_t a[4096]; while (emu_audio(a, 2048) > 0) {}
            (pass ? c2 : c1)[i] = frame_crc();
        }
    }
    int bad = 0;
    for (int i = 0; i < 600; i++) if (c1[i] != c2[i]) { if (!bad) printf("first mismatch at +%d\n", i); bad++; }
    printf("state size %zu bytes; replay after load: %s (%d/600 frames differ)\n", ss, bad ? "MISMATCH" : "identical", bad);
    char h[41]; emu_sha1_hex((const uint8_t *)"abc", 3, h); printf("sha1(abc) = %s\n", h);
    return bad != 0;
}
