#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/emu.h"
#include "../src/md.h"
#include "../src/rewind.h"
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); static uint8_t rom[0x400000]; size_t n = fread(rom, 1, sizeof rom, f); fclose(f);
    emu_load_rom(rom, n); emu_reset();
    for (int i = 0; i < 2000; i++) { emu_set_pad(0, i > 1300 && i < 1310 ? PAD_START : 0); emu_run_frame(); int16_t a[4096]; while (emu_audio(a, 2048) > 0) {} }
    rw_init(argc > 2 ? (size_t)atoi(argv[2]) : (16 << 20));
    size_t ss = emu_state_size(); uint8_t *ref[300];
    for (int i = 0; i < 300; i++) {
        ref[i] = malloc(ss); emu_state_save(ref[i]);
        rw_push();
        emu_set_pad(0, (i / 20) & 1 ? PAD_C : PAD_DOWN); emu_run_frame(); int16_t a[4096]; while (emu_audio(a, 2048) > 0) {}
    }
    int bad = 0; uint8_t *s = malloc(ss);
    int avail = rw_count(); printf("records kept: %d\n", avail);
    for (int i = 299; i >= 300 - avail; i--) {
        if (!rw_pop()) { printf("pop failed at %d\n", i); return 1; }
        emu_state_save(s);
        if (memcmp(s, ref[i], ss)) bad++;
    }
    printf("rewind 300 steps: %d mismatches, pop at end: %d\n", bad, rw_pop());
    return 0;
}
