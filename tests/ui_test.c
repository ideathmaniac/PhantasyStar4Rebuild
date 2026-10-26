/* Renders the launcher's software layers to a PPM for checking on Linux:
 * ui_test title.ppm thumb.ppm out.ppm  (text is added by the preview script) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../win/ui_soft.h"
static uint32_t *ppm(const char *p, int *w, int *h)
{
    FILE *f = fopen(p, "rb"); int mx; if (fscanf(f, "P6 %d %d %d", w, h, &mx) != 3) exit(1); fgetc(f);
    uint32_t *b = malloc((size_t)*w * *h * 4); unsigned char c[3];
    for (int i = 0; i < *w * *h; i++) { if (fread(c, 1, 3, f) != 3) exit(1); b[i] = (c[0] << 16) | (c[1] << 8) | c[2]; }
    fclose(f); return b;
}
int main(int argc, char **argv)
{
    int tw, th, sw, sh; uint32_t *t = ppm(argv[1], &tw, &th), *s = ppm(argv[2], &sw, &sh);
    int W = 1600, H = 900;
    ui_img d = { calloc((size_t)W * H, 4), W, H, W };
    /* backdrop 64x36 stretched bilinear (the front end lets GDI HALFTONE do it) */
    int bw = 64, bh = 36; uint32_t *bk = malloc((size_t)bw * bh * 4);
    ui_backdrop(t, tw, th, tw, bk, bw, bh);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float fx = (x + 0.5f) * bw / W - 0.5f, fy = (y + 0.5f) * bh / H - 0.5f;
        int x0 = fx < 0 ? 0 : (int)fx, y0 = fy < 0 ? 0 : (int)fy, x1 = x0 + 1 < bw ? x0 + 1 : x0, y1 = y0 + 1 < bh ? y0 + 1 : y0;
        float ax = fx - x0, ay = fy - y0; if (ax < 0) ax = 0; if (ay < 0) ay = 0;
        uint32_t c = 0;
        for (int k = 0; k < 3; k++) {
            int sh_ = 16 - 8 * k;
            float v = (((bk[y0 * bw + x0] >> sh_) & 255) * (1 - ax) + ((bk[y0 * bw + x1] >> sh_) & 255) * ax) * (1 - ay) +
                      (((bk[y1 * bw + x0] >> sh_) & 255) * (1 - ax) + ((bk[y1 * bw + x1] >> sh_) & 255) * ax) * ay;
            c |= (uint32_t)v << sh_;
        }
        d.px[y * W + x] = c;
    }
    int lw, lh; uint8_t *logo = ui_logo(t, tw, th, tw, &lw, &lh);
    if (!logo) { fprintf(stderr, "no logo\n"); return 1; }
    ui_glow_rgba(&d, 80, 60, logo, lw, lh, 3, 0xFFB43C, 90);
    /* menu: selected item */
    ui_hgrad(&d, 110, 470, 420, 74, 0x5A78FF, 120, 0);
    ui_panel(&d, 110, 470, 4, 74, 0, 0xFFD76A, 255);
    /* card */
    int cx = 820, cy = 150, cw = 670, ch = 530;
    ui_panel(&d, cx, cy, cw, ch, 16, 0x0E1228, 190);
    ui_panel(&d, cx, cy, cw, 1, 0, 0xA0B4FF, 60);
    /* thumbnail: nearest 2x of the half-size picture (GDI HALFTONE does it smoothly) */
    int dw = cw - 44, dh = dw * sh * 7 / (sw * 8);
    for (int y = 0; y < dh; y++) for (int x = 0; x < dw; x++) d.px[(cy + 22 + y) * W + cx + 22 + x] = s[(y * sh / dh) * sw + x * sw / dw];
    for (int k = 0; k < 3; k++) ui_panel(&d, cx + 22 + k * ((cw - 44 - 20) / 3 + 10), cy + 22 + dh + 130, (cw - 44 - 20) / 3, 34, 8, 0xFFFFFF, 14);
    FILE *f = fopen(argv[3], "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) { unsigned char c[3] = { d.px[i] >> 16, d.px[i] >> 8, d.px[i] }; fwrite(c, 1, 3, f); }
    fclose(f);
    wchar_t nm[16]; uint8_t rec[6] = { 0x14, 0x1F, 0x26, 0xFE }; ui_ps4_name(rec, nm, 16);
    printf("name ok: %d\n", wcscmp(nm, L"Чаз") == 0);
    return 0;
}
