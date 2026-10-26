/* Software drawing for the launcher, see ui_soft.h. */
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "ui_soft.h"

static inline uint32_t blend(uint32_t dst, uint32_t src, int a)
{
    if (a <= 0) return dst;
    if (a >= 255) return src & 0xFFFFFF;
    uint32_t rb = ((src & 0xFF00FF) * (uint32_t)a + (dst & 0xFF00FF) * (uint32_t)(255 - a)) / 255;
    uint32_t g = ((src & 0x00FF00) * (uint32_t)a + (dst & 0x00FF00) * (uint32_t)(255 - a)) / 255;
    /* the two channels in rb do not overflow into each other: each is at most 255*255/255 */
    uint32_t r = ((((src >> 16) & 255) * (uint32_t)a + ((dst >> 16) & 255) * (uint32_t)(255 - a)) / 255) << 16;
    uint32_t b = (((src & 255) * (uint32_t)a + (dst & 255) * (uint32_t)(255 - a)) / 255);
    (void)rb;
    return r | (g & 0xFF00) | b;
}

void ui_panel(ui_img *d, int x, int y, int w, int h, int r, uint32_t rgb, int alpha)
{
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= d->h) continue;
        uint32_t *row = d->px + (size_t)yy * d->pitch;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= d->w) continue;
            int a = alpha;
            /* corners: coverage from the distance to the corner circle centre */
            int cx = i < r ? r - i : i >= w - r ? i - (w - r - 1) : 0;
            int cy = j < r ? r - j : j >= h - r ? j - (h - r - 1) : 0;
            if (cx && cy) {
                float dx = cx - 0.5f, dy = cy - 0.5f, dd = dx * dx + dy * dy, rr = (float)r;
                if (dd >= (rr + 0.5f) * (rr + 0.5f)) continue;
                if (dd > (rr - 0.5f) * (rr - 0.5f)) {
                    /* about one pixel of antialiasing */
                    float t = (rr + 0.5f) * (rr + 0.5f) - dd, span = 2 * rr;
                    a = (int)(a * (t / span > 1 ? 1 : t / span));
                }
            }
            row[xx] = blend(row[xx], rgb, a);
        }
    }
}

void ui_hgrad(ui_img *d, int x, int y, int w, int h, uint32_t rgb, int a0, int a1)
{
    if (w <= 0) return;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= d->h) continue;
        uint32_t *row = d->px + (size_t)yy * d->pitch;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= d->w) continue;
            row[xx] = blend(row[xx], rgb, a0 + (a1 - a0) * i / w);
        }
    }
}

void ui_blit_rgba(ui_img *d, int x, int y, const uint8_t *s, int sw, int sh, int sc)
{
    for (int j = 0; j < sh * sc; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= d->h) continue;
        uint32_t *row = d->px + (size_t)yy * d->pitch;
        const uint8_t *srow = s + (size_t)(j / sc) * sw * 4;
        for (int i = 0; i < sw * sc; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= d->w) continue;
            const uint8_t *p = srow + (i / sc) * 4;
            if (p[3]) row[xx] = blend(row[xx], ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2], p[3]);
        }
    }
}

void ui_glow_rgba(ui_img *d, int x, int y, const uint8_t *s, int sw, int sh, int sc, uint32_t rgb, int strength)
{
    /* a blurred copy of the alpha channel at source resolution, drawn
     * smoothly upscaled (bilinear) under the picture */
    int pad = 6, gw = sw + 2 * pad, gh = sh + 2 * pad;
    float *a = calloc((size_t)gw * gh, sizeof(float)), *t = calloc((size_t)gw * gh, sizeof(float));
    for (int j = 0; j < sh; j++) for (int i = 0; i < sw; i++) a[(size_t)(j + pad) * gw + i + pad] = s[((size_t)j * sw + i) * 4 + 3] / 255.0f;
    for (int pass = 0; pass < 3; pass++) {           /* three box blurs ~ a gaussian */
        for (int j = 0; j < gh; j++) for (int i = 0; i < gw; i++) {
            float acc = 0; int n = 0;
            for (int k = -2; k <= 2; k++) { int ii = i + k; if (ii >= 0 && ii < gw) { acc += a[(size_t)j * gw + ii]; n++; } }
            t[(size_t)j * gw + i] = acc / n;
        }
        for (int j = 0; j < gh; j++) for (int i = 0; i < gw; i++) {
            float acc = 0; int n = 0;
            for (int k = -2; k <= 2; k++) { int jj = j + k; if (jj >= 0 && jj < gh) { acc += t[(size_t)jj * gw + i]; n++; } }
            a[(size_t)j * gw + i] = acc / n;
        }
    }
    int ox = x - pad * sc, oy = y - pad * sc;
    for (int j = 0; j < gh * sc; j++) {
        int yy = oy + j;
        if (yy < 0 || yy >= d->h) continue;
        float fy = (j + 0.5f) / sc - 0.5f; int j0 = (int)(fy < 0 ? 0 : fy); float wy = fy - j0; if (wy < 0) wy = 0;
        int j1 = j0 + 1 < gh ? j0 + 1 : j0;
        uint32_t *row = d->px + (size_t)yy * d->pitch;
        for (int i = 0; i < gw * sc; i++) {
            int xx = ox + i;
            if (xx < 0 || xx >= d->w) continue;
            float fx = (i + 0.5f) / sc - 0.5f; int i0 = (int)(fx < 0 ? 0 : fx); float wx = fx - i0; if (wx < 0) wx = 0;
            int i1 = i0 + 1 < gw ? i0 + 1 : i0;
            float v = (a[(size_t)j0 * gw + i0] * (1 - wx) + a[(size_t)j0 * gw + i1] * wx) * (1 - wy) +
                      (a[(size_t)j1 * gw + i0] * (1 - wx) + a[(size_t)j1 * gw + i1] * wx) * wy;
            int al = (int)(v * strength);
            if (al > 0) row[xx] = blend(row[xx], rgb, al > 255 ? 255 : al);
        }
    }
    free(a); free(t);
    ui_blit_rgba(d, x, y, s, sw, sh, sc);
}

void ui_darken(ui_img *d, int f)
{
    if (f >= 256) return;
    if (f < 0) f = 0;
    for (int j = 0; j < d->h; j++) {
        uint32_t *row = d->px + (size_t)j * d->pitch;
        for (int i = 0; i < d->w; i++) {
            uint32_t c = row[i];
            row[i] = ((((c & 0xFF00FF) * (uint32_t)f) >> 8) & 0xFF00FF) | ((((c & 0x00FF00) * (uint32_t)f) >> 8) & 0x00FF00);
        }
    }
}

void ui_backdrop(const uint32_t *fb, int fw, int fh, int fp, uint32_t *out, int bw, int bh)
{
    /* box-average down to bw x bh (cover: crop to the output aspect) */
    double sa = (double)fw * (8.0 / 7.0) / fh, oa = (double)bw / bh;   /* console pixels are 8:7 wide */
    double cx0 = 0, cw = fw, cy0 = 0, ch = fh;
    if (oa > sa) { ch = fh * sa / oa; cy0 = (fh - ch) / 2; } else { cw = fw * oa / sa; cx0 = (fw - cw) / 2; }
    float *r = malloc(sizeof(float) * 3 * (size_t)bw * bh), *t = malloc(sizeof(float) * 3 * (size_t)bw * bh);
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            int x0 = (int)(cx0 + cw * x / bw), x1 = (int)(cx0 + cw * (x + 1) / bw);
            int y0 = (int)(cy0 + ch * y / bh), y1 = (int)(cy0 + ch * (y + 1) / bh);
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            float acc[3] = { 0, 0, 0 }; int n = 0;
            for (int yy = y0; yy < y1 && yy < fh; yy++)
                for (int xx = x0; xx < x1 && xx < fw; xx++) {
                    uint32_t c = fb[(size_t)yy * fp + xx];
                    acc[0] += (c >> 16) & 255; acc[1] += (c >> 8) & 255; acc[2] += c & 255; n++;
                }
            for (int k = 0; k < 3; k++) r[((size_t)y * bw + x) * 3 + k] = n ? acc[k] / n : 0;
        }
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) for (int k = 0; k < 3; k++) {
            float acc = 0; int n = 0;
            for (int d = -2; d <= 2; d++) { int xx = x + d; if (xx >= 0 && xx < bw) { acc += r[((size_t)y * bw + xx) * 3 + k]; n++; } }
            t[((size_t)y * bw + x) * 3 + k] = acc / n;
        }
        for (int y = 0; y < bh; y++) for (int x = 0; x < bw; x++) for (int k = 0; k < 3; k++) {
            float acc = 0; int n = 0;
            for (int d = -2; d <= 2; d++) { int yy = y + d; if (yy >= 0 && yy < bh) { acc += t[((size_t)yy * bw + x) * 3 + k]; n++; } }
            r[((size_t)y * bw + x) * 3 + k] = acc / n;
        }
    }
    for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
            /* dim, a little more saturated, darker towards the edges (centre left of middle) */
            float dx = (x + 0.5f) / bw - 0.4f, dy = (y + 0.5f) / bh - 0.45f;
            float vig = 1.0f - 1.6f * (dx * dx + dy * dy);
            if (vig < 0.15f) vig = 0.15f;
            float *p = &r[((size_t)y * bw + x) * 3];
            float l = p[0] * 0.299f + p[1] * 0.587f + p[2] * 0.114f;
            uint32_t c = 0;
            for (int k = 0; k < 3; k++) {
                float v = (l + (p[k] - l) * 1.3f) * 0.62f * vig;
                int iv = (int)(v < 0 ? 0 : v > 255 ? 255 : v);
                c |= (uint32_t)iv << (16 - 8 * k);
            }
            out[(size_t)y * bw + x] = c;
        }
    free(r); free(t);
}

uint8_t *ui_logo(const uint32_t *fb, int fw, int fh, int fp, int *lw, int *lh)
{
    /* the gold title letters of the title screen, rows 40-121, columns 100-299 */
    int x0 = 100, y0 = 40, w = 200, h = 82, gold = 0;
    if (fw < x0 + w || fh < y0 + h) return 0;
    uint8_t *o = calloc((size_t)w * h, 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = fb[(size_t)(y0 + y) * fp + x0 + x];
            int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            uint8_t *p = o + ((size_t)y * w + x) * 4;
            if (r + g > 260 && r > b + 30) { p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = 255; gold++; }
        }
    if (gold < 800) { free(o); return 0; }            /* not the title screen */
    *lw = w; *lh = h;
    return o;
}

void ui_ps4_name(const uint8_t *rec, wchar_t *out, int n)
{
    /* capitals at $01-$18 (with gaps), small letters from $1F in alphabet order */
    static const wchar_t caps[25] = L"\x0000АБВГДЕЖЗИКЛМНОПРСТУЧХ??Э";
    static const wchar_t small[] = L"абвгдежзиклмнопрстуфхцчшщъыьэюя";
    int k = 0;
    for (int i = 0; i < 8 && k < n - 1; i++) {
        uint8_t c = rec[i];
        if (c == 0xFE || c == 0xFF || c == 0) break;
        if (c >= 0x1F && c < 0x1F + (int)(sizeof small / sizeof small[0]) - 1) out[k++] = small[c - 0x1F];
        else if (c < 25 && caps[c] != L'?') out[k++] = caps[c];
        else out[k++] = L'?';
    }
    out[k] = 0;
}
