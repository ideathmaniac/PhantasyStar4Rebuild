#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "../src/hdscale.h"
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); int w, h, mx; fscanf(f, "P6 %d %d %d", &w, &h, &mx); fgetc(f);
    uint32_t *s = malloc(4 * w * h), *d = malloc(4 * 16 * w * h);
    for (int i = 0; i < w * h; i++) { int r = fgetc(f), g = fgetc(f), b = fgetc(f); s[i] = (r << 16) | (g << 8) | b; }
    fclose(f);
    clock_t t = clock();
    for (int k = 0; k < 20; k++) hd_scale4(s, w, h, w, d, w * 4);
    fprintf(stderr, "%.2f ms per frame\n", (double)(clock() - t) * 1000 / CLOCKS_PER_SEC / 20);
    f = fopen(argv[2], "wb"); fprintf(f, "P6\n%d %d\n255\n", w * 4, h * 4);
    for (int i = 0; i < 16 * w * h; i++) { fputc(d[i] >> 16, f); fputc(d[i] >> 8, f); fputc(d[i], f); }
    fclose(f);
}
