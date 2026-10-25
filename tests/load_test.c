/* Mimics the Windows front end around a state load, under AddressSanitizer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/emu.h"
#include "../src/md.h"
#include "../src/rewind.h"
#include "../src/hdscale.h"
static uint8_t rom[0x400000];
static uint32_t hd[MD_W_MAX * 4 * 224 * 4];
static void frame(uint16_t p) { emu_set_pad(0, p); emu_run_frame(); int16_t a[8192]; while (emu_audio(a, 4096) > 0) {} int W, H; const uint32_t *fb = emu_framebuffer(&W, &H); hd_scale4(fb, W, H, MD_W_MAX, hd, MD_W_MAX * 4); }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); size_t n = fread(rom, 1, sizeof rom, f); fclose(f);
    emu_load_rom(rom, n); emu_reset(); emu_set_widescreen(53); rw_init(48 << 20);
    for (int i = 0; i < 5200; i++) { uint16_t p = 0; if (i == 1300 || i == 1500) p = PAD_START; if (i > 1600 && i % 100 < 8) p = PAD_C; frame(p); if (i & 1) rw_push(); }
    size_t ss = emu_state_size(); uint8_t *st = malloc(ss); emu_state_save(st);
    f = fopen("out/slot1.sav", "wb"); fwrite(st, 1, ss, f); fclose(f);
    printf("saved state %zu bytes at frame %llu\n", ss, (unsigned long long)g_frame);
    /* "fresh start", then load */
    emu_load_rom(rom, n); emu_reset(); emu_set_widescreen(53); rw_init(48 << 20);
    for (int i = 0; i < 100; i++) frame(0);
    f = fopen("out/slot1.sav", "rb"); uint8_t *b = malloc(ss); size_t m = fread(b, 1, ss, f); fclose(f);
    printf("load: %d\n", emu_state_load(b, m)); rw_clear();
    for (int i = 0; i < 2000; i++) { frame((i / 30) & 1 ? PAD_LEFT : PAD_DOWN); if (i & 1) rw_push(); if (i % 500 == 499) { for (int k = 0; k < 100; k++) rw_pop(); } }
    printf("ok, frame %llu, fb width %d\n", (unsigned long long)g_frame, g_fb_width);
    return 0;
}
