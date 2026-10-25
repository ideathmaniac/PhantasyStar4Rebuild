/*
 * 4x pixel-art upscaler (edge-directed, xBR level-1 style), written for this
 * project. For every source pixel and each of its four corners it compares
 * two weighted sums of colour distances in a 5x5 neighbourhood to decide
 * whether an edge runs diagonally through that corner; if so the corner's
 * sub-pixels are blended toward the neighbour across the edge.
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "hdscale.h"

static int16_t *yuv;          /* per source pixel: y, u, v */
static int yuv_cap;

static inline int dist(const int16_t *a, const int16_t *b)
{
    int dy = a[0] - b[0], du = a[1] - b[1], dv = a[2] - b[2];
    if (dy < 0) dy = -dy; if (du < 0) du = -du; if (dv < 0) dv = -dv;
    return dy * 48 + du * 7 + dv * 6;
}

static inline uint32_t mix(uint32_t a, uint32_t b, int wb /* 0..256 */)
{
    int wa = 256 - wb;
    uint32_t r = ((((a >> 16) & 255) * wa + ((b >> 16) & 255) * wb) >> 8);
    uint32_t g = ((((a >> 8) & 255) * wa + ((b >> 8) & 255) * wb) >> 8);
    uint32_t bl = (((a & 255) * wa + (b & 255) * wb) >> 8);
    return (r << 16) | (g << 8) | bl;
}

/* sub-pixel weights for the bottom-right corner of a 4x4 block (x + y) */
static const int corner_w[7] = { 0, 0, 0, 0, 64, 192, 256 };

void hd_prepare(const uint32_t *src, int w, int h, int pitch)
{
    if (w * h > yuv_cap) { free(yuv); yuv_cap = w * h; yuv = malloc(sizeof(int16_t) * 3 * (size_t)yuv_cap); }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t c = src[y * pitch + x];
            int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            int16_t *p = &yuv[(y * w + x) * 3];
            p[0] = (int16_t)((r * 299 + g * 587 + b * 114) / 1000);
            p[1] = (int16_t)((b * 500 - r * 169 - g * 331) / 1000);
            p[2] = (int16_t)((r * 500 - g * 419 - b * 81) / 1000);
        }
}

void hd_rows(const uint32_t *src, int w, int h, int pitch, uint32_t *dst, int dpitch, int y0, int y1)
{
    /* rotations: (u, v) in the bottom-right template -> neighbour offset */
    static const int rot[4][2][2] = {
        { { 1, 0 }, { 0, 1 } },     /* BR: dx = u,  dy = v  */
        { { 0, -1 }, { 1, 0 } },    /* BL: dx = -v, dy = u  */
        { { -1, 0 }, { 0, -1 } },   /* TL: dx = -u, dy = -v */
        { { 0, 1 }, { -1, 0 } },    /* TR: dx = v,  dy = -u */
    };
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t E = src[y * pitch + x];
            uint32_t out[16];
            for (int i = 0; i < 16; i++) out[i] = E;
#define NB(u, v, cx, cy) do { int dx_ = R[0][0] * (u) + R[0][1] * (v), dy_ = R[1][0] * (u) + R[1][1] * (v); \
            int xx_ = x + dx_, yy_ = y + dy_; if (xx_ < 0) xx_ = 0; if (xx_ >= w) xx_ = w - 1; if (yy_ < 0) yy_ = 0; if (yy_ >= h) yy_ = h - 1; \
            cx = &yuv[(yy_ * w + xx_) * 3]; cy = src[yy_ * pitch + xx_]; } while (0)
            for (int k = 0; k < 4; k++) {
                const int (*R)[2] = rot[k];
                const int16_t *pe, *pf, *ph, *pi, *pc, *pg, *pb, *pd, *pf4, *ph5, *pd5, *pi5, *pi4;
                uint32_t ce, cf, chh, ci, cc, cg, cb, cd, t;
                NB(0, 0, pe, ce); NB(1, 0, pf, cf); NB(0, 1, ph, chh); NB(1, 1, pi, ci);
                NB(1, -1, pc, cc); NB(-1, 1, pg, cg); NB(0, -1, pb, cb); NB(-1, 0, pd, cd);
                NB(2, 0, pf4, t); NB(0, 2, ph5, t); NB(-1, 2, pd5, t); NB(1, 2, pi5, t); NB(2, 1, pi4, t);
                (void)cc; (void)cg; (void)cb; (void)cd; (void)ci;
                if (cf == ce || chh == ce) continue;
                int wd1 = dist(pe, pc) + dist(pe, pg) + dist(pi, pf4) + dist(pi, ph5) + 4 * dist(ph, pf);
                int wd2 = dist(ph, pd5) + dist(ph, pi5) + dist(pf, pi4) + dist(pf, pb) + 4 * dist(pe, pi);
                if (wd1 >= wd2) continue;
                uint32_t nc = dist(pe, pf) <= dist(pe, ph) ? cf : chh;
                for (int sy = 0; sy < 4; sy++)
                    for (int sx = 0; sx < 4; sx++) {
                        int wgt = corner_w[sx + sy];
                        if (!wgt) continue;
                        /* template sub-pixel (sx, sy) rotated into this corner */
                        int ox, oy;
                        switch (k) {
                        case 0: ox = sx; oy = sy; break;
                        case 1: ox = 3 - sy; oy = sx; break;
                        case 2: ox = 3 - sx; oy = 3 - sy; break;
                        default: ox = sy; oy = 3 - sx; break;
                        }
                        out[oy * 4 + ox] = mix(out[oy * 4 + ox], nc, wgt);
                    }
            }
#undef NB
            uint32_t *d = dst + (size_t)(y * 4) * dpitch + x * 4;
            for (int sy = 0; sy < 4; sy++) memcpy(d + (size_t)sy * dpitch, &out[sy * 4], 16);
        }
    }
}

void hd_scale4(const uint32_t *src, int w, int h, int pitch, uint32_t *dst, int dpitch)
{
    hd_prepare(src, w, h, pitch);
    hd_rows(src, w, h, pitch, dst, dpitch, 0, h);
}
