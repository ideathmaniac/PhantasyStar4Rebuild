/* png_test dec in.png out.rgba | enc in.rgba w h out.png */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/png.h"
static uint8_t *rd(const char *p, size_t *n) { FILE *f = fopen(p, "rb"); fseek(f, 0, 2); *n = ftell(f); fseek(f, 0, 0); uint8_t *b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b; }
int main(int c, char **v)
{
    size_t n; uint8_t *b = rd(v[2], &n);
    if (!strcmp(v[1], "dec")) {
        int w, h; const char *e = 0; uint8_t *o = png_decode(b, n, &w, &h, &e);
        if (!o) { printf("ERR %s\n", e); return 1; }
        FILE *f = fopen(v[3], "wb"); fwrite(o, 1, (size_t)w * h * 4, f); fclose(f); printf("%d %d\n", w, h);
    } else {
        int w = atoi(v[3]), h = atoi(v[4]); size_t on; uint8_t *o = png_encode(b, w, h, &on);
        FILE *f = fopen(v[5], "wb"); fwrite(o, 1, on, f); fclose(f); printf("%zu bytes\n", on);
    }
    return 0;
}
