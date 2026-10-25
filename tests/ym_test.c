#include <stdio.h>
#include <stdlib.h>
#include "../src/sound.h"
int g_snd_mask;
int main(int argc, char **argv)
{
    ym_reset();
    int alg = argc > 1 ? atoi(argv[1]) : 7, fb = argc > 2 ? atoi(argv[2]) : 0;
    /* fnum for 440 Hz: f = fnum * 53267 / 2^(21-block) ; block 4 -> fnum = 440*2^17/53267 */
    int fnum = 1083, blk = 4;
    for (int s = 0; s < 4; s++) {
        int base = 0x30 + s * 4;
        ym_write(0, base); ym_write(1, 0x01);                 /* MUL 1 */
        ym_write(0, base + 0x10); ym_write(1, s == 3 ? 0 : (alg == 7 ? 0x7F : 0x20)); /* TL */
        ym_write(0, base + 0x20); ym_write(1, 0x1F);          /* AR 31 */
        ym_write(0, base + 0x30); ym_write(1, 0);
        ym_write(0, base + 0x40); ym_write(1, 0);
        ym_write(0, base + 0x50); ym_write(1, 0x0F);
    }
    ym_write(0, 0xB0); ym_write(1, (fb << 3) | alg);
    ym_write(0, 0xB4); ym_write(1, 0xC0);
    ym_write(0, 0xA4); ym_write(1, (blk << 3) | (fnum >> 8));
    ym_write(0, 0xA0); ym_write(1, fnum & 0xFF);
    ym_write(0, 0x28); ym_write(1, 0xF0);
    FILE *f = fopen(argv[3], "wb");
    for (int i = 0; i < 53267; i++) { int16_t l, r; ym_clock_sample(&l, &r); fwrite(&l, 2, 1, f); }
    fclose(f);
    return 0;
}
