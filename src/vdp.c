/* Mega Drive VDP: ports, DMA, status/HV counters and a per-line renderer. */
#include <string.h>
#include "md.h"

vdp_t V;
uint32_t g_fb[MD_VIS_LINES][MD_W_MAX];
int g_fb_width = 320;

void vdp_reset(void)
{
    memset(&V, 0, sizeof V);
    V.reg[12] = 0x81;
    V.reg[15] = 2;
    V.hint_counter = 0;
}

static inline int h40(void) { return (V.reg[12] & 0x01) != 0; }

/* ---- writes into VDP memories ---- */
static void mem_write(uint16_t v)
{
    uint16_t a = V.addr;
    switch (V.code & 0x0F) {
    case 1:                                     /* VRAM */
        if (a & 1) v = (uint16_t)((v >> 8) | (v << 8));
        V.vram[a & 0xFFFE] = (uint8_t)(v >> 8);
        V.vram[(a & 0xFFFE) | 1] = (uint8_t)v;
        break;
    case 3: V.cram[(a >> 1) & 0x3F] = v & 0x0EEE; break;
    case 5: if (((a >> 1) & 0x3F) < 40) V.vsram[(a >> 1) & 0x3F] = v & 0x07FF; break;
    }
    V.addr = (uint16_t)(a + V.reg[15]);
}

static uint32_t dma_src(void) { return ((V.reg[23] & 0x7F) << 17) | (V.reg[22] << 9) | (V.reg[21] << 1); }
static uint32_t dma_len(void) { uint32_t n = (V.reg[20] << 8) | V.reg[19]; return n ? n : 0x10000; }

#include <stdio.h>
#include <stdlib.h>
static int vlog = -1;
#define VLOG(...) do { if (vlog < 0) vlog = getenv("VDP_LOG") != NULL; if (vlog) fprintf(stderr, __VA_ARGS__); } while (0)
static void dma_68k(void)
{
    uint32_t src = dma_src(), n = dma_len();
    VLOG("f%llu l%d DMA68k src=%06X len=%X dst=%04X code=%X inc=%X\n", (unsigned long long)g_frame, g_line, src, n, V.addr, V.code & 15, V.reg[15]);
    uint8_t code = V.code;
    while (n--) {
        uint16_t w = (uint16_t)md_read16(src);
        V.code = code;
        mem_write(w);
        src = (src & 0xFE0000) | ((src + 2) & 0x1FFFF);
    }
    V.reg[19] = V.reg[20] = 0;
    V.reg[21] = (uint8_t)(src >> 1); V.reg[22] = (uint8_t)(src >> 9);
    V.reg[23] = (uint8_t)((V.reg[23] & 0x80) | ((src >> 17) & 0x7F));
}

static void dma_fill(uint16_t data)
{
    uint32_t n = dma_len();
    VLOG("f%llu FILL len=%X dst=%04X data=%04X inc=%X\n", (unsigned long long)g_frame, n, V.addr, data, V.reg[15]);
    do {
        if ((V.code & 0x0F) == 1) V.vram[V.addr ^ 1] = (uint8_t)(data >> 8);
        else { V.code = V.code; mem_write(data); V.addr = (uint16_t)(V.addr - V.reg[15]); }
        V.addr = (uint16_t)(V.addr + V.reg[15]);
    } while (--n);
    V.reg[19] = V.reg[20] = 0;
}

static void dma_copy(void)
{
    uint16_t src = (uint16_t)((V.reg[22] << 8) | V.reg[21]);
    uint32_t n = dma_len();
    VLOG("f%llu COPY len=%X src=%04X dst=%04X\n", (unsigned long long)g_frame, n, src, V.addr);
    while (n--) {
        V.vram[V.addr] = V.vram[src];
        src++;
        V.addr = (uint16_t)(V.addr + V.reg[15]);
    }
    V.reg[19] = V.reg[20] = 0;
    V.reg[21] = (uint8_t)src; V.reg[22] = (uint8_t)(src >> 8);
}

static int vlog_addr = -1, vlog_note;
void vdp_ctrl_write(uint16_t v)
{
    if (vlog_addr < 0) vlog_addr = getenv("VDP_ADDRLOG") != NULL;
    if (V.pending) {
        V.pending = 0;
        V.addr = (uint16_t)((V.addr & 0x3FFF) | ((v & 3) << 14));
        V.code = (uint8_t)((V.code & 0x03) | ((v >> 2) & 0x3C));
        if (vlog_addr && (V.code & 0xF) == 1) { extern uint32_t g_ipc; fprintf(stderr, "f%llu l%d VRAMW %04X pc=%06X inc=%d%s\n", (unsigned long long)g_frame, g_line, V.addr, g_ipc, V.reg[15], (V.code & 0x20) && (V.reg[1] & 0x10) ? " DMA" : ""); }
        if ((V.code & 0x20) && (V.reg[1] & 0x10)) {
            switch (V.reg[23] >> 6) {
            case 0: case 1: dma_68k(); break;
            case 2: V.fill_pending = 1; break;
            case 3: dma_copy(); break;
            }
        }
        return;
    }
    if ((v & 0xC000) == 0x8000) {
        int r = (v >> 8) & 0x1F;
        if (r < 24) V.reg[r] = (uint8_t)v;
        V.code = 0;
        return;
    }
    V.addr = (uint16_t)((V.addr & 0xC000) | (v & 0x3FFF));
    V.code = (uint8_t)((V.code & 0x3C) | (v >> 14));
    V.pending = 1;
    if (vlog_addr) { extern uint32_t g_ipc; vlog_note = 1; }
}

void vdp_data_write(uint16_t v)
{
    V.pending = 0;
    mem_write(v);
    if (V.fill_pending) { V.fill_pending = 0; dma_fill(v); }
}

uint16_t vdp_data_read(void)
{
    V.pending = 0;
    uint16_t a = V.addr, r = 0;
    switch (V.code & 0x0F) {
    case 0: r = (uint16_t)((V.vram[a & 0xFFFE] << 8) | V.vram[(a & 0xFFFE) | 1]); break;
    case 8: r = V.cram[(a >> 1) & 0x3F]; break;
    case 4: r = ((a >> 1) & 0x3F) < 40 ? V.vsram[(a >> 1) & 0x3F] : 0; break;
    }
    V.addr = (uint16_t)(a + V.reg[15]);
    return r;
}

static int line_pos(void)   /* cycles into the current line, 0..487 */
{
    int64_t p = (int64_t)g_cycles - (int64_t)(g_line_end - MD_LINE_CYC);
    if (p < 0) p = 0;
    if (p >= MD_LINE_CYC) p = MD_LINE_CYC - 1;
    return (int)p;
}

uint16_t vdp_status_read(void)
{
    V.pending = 0;
    uint16_t s = 0x3400 | 0x0200;              /* FIFO empty */
    if (V.vint_pending) s |= 0x80;
    if (g_line >= MD_VIS_LINES || !(V.reg[1] & 0x40)) s |= 0x08;
    if (line_pos() >= 400) s |= 0x04;          /* hblank */
    return s;
}

uint16_t vdp_hv_read(void)
{
    int v = g_line <= 0xEA ? g_line : g_line - 6;
    int h = (line_pos() * 0xD0 / MD_LINE_CYC + 0x85) & 0xFF;
    return (uint16_t)(((v & 0xFF) << 8) | h);
}

/* ---- renderer ---- */
static uint32_t rgb_tab[3][512];               /* normal, shadow, highlight */
static int rgb_init;

static void init_rgb(void)
{
    static const uint8_t lv[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };
    for (int c = 0; c < 512; c++) {
        int r = c & 7, g = (c >> 3) & 7, b = (c >> 6) & 7;
        rgb_tab[0][c] = (lv[r] << 16) | (lv[g] << 8) | lv[b];
        rgb_tab[1][c] = ((lv[r] / 2) << 16) | ((lv[g] / 2) << 8) | (lv[b] / 2);
        rgb_tab[2][c] = ((128 + lv[r] / 2) << 16) | ((128 + lv[g] / 2) << 8) | (128 + lv[b] / 2);
    }
    rgb_init = 1;
}
static inline uint32_t cram_rgb(int idx, int lvl)
{
    uint16_t c = V.cram[idx & 63];
    int c9 = ((c >> 1) & 7) | (((c >> 5) & 7) << 3) | (((c >> 9) & 7) << 6);
    return rgb_tab[lvl][c9];
}

int32_t g_ws_ext;
int g_ws_frame_ext;
int (*g_ws_begin)(void);
uint8_t (*g_ws_pixel)(int plane, int sx, int line);

/* pixel format in line buffers: bit 7 priority, bits 0-5 colour index (0 = transparent) */
uint8_t vdp_tile_pixel(uint16_t ent, int px, int py);
static inline uint8_t tile_pixel(uint16_t ent, int px, int py)
{
    int tile = ent & 0x7FF;
    if (ent & 0x0800) px = 7 - px;
    if (ent & 0x1000) py = 7 - py;
    uint8_t b = V.vram[(tile * 32 + py * 4 + (px >> 1)) & 0xFFFF];
    int c = (px & 1) ? (b & 15) : (b >> 4);
    if (!c) return 0;
    return (uint8_t)(((ent >> 8) & 0x80) | (((ent >> 13) & 3) << 4) | c);
}

uint8_t vdp_tile_pixel(uint16_t ent, int px, int py) { return tile_pixel(ent, px, py); }

/* out[i] is screen x = i - E, for i in [0, W + 2E) */
static void draw_plane(uint8_t *out, int line, int W, int E, uint32_t base, int plane /*0 A,1 B*/)
{
    static const int sz[4] = { 32, 64, 32, 128 };
    int pw = sz[V.reg[16] & 3], ph = sz[(V.reg[16] >> 4) & 3];
    uint32_t hsb = (V.reg[13] & 0x3F) << 10;
    int hmode = V.reg[11] & 3, hidx = hmode == 0 ? 0 : hmode == 2 ? (line & ~7) : line;
    uint32_t ha = (hsb + hidx * 4 + plane * 2) & 0xFFFF;
    int hs = ((V.vram[ha] << 8) | V.vram[ha | 1]) & 0x3FF;
    int vcol = (V.reg[11] & 4) != 0;
    for (int x = -E; x < W + E; x++) {
        if ((x < 0 || x >= W) && g_ws_pixel) { out[x + E] = g_ws_pixel(plane, x, line); continue; }
        int col = x < 0 ? 0 : x >= W ? W - 1 : x;
        int vs = vcol ? V.vsram[((col >> 4) * 2 + plane) % 40] : V.vsram[plane];
        int py = (line + vs) & (ph * 8 - 1);
        int px = (x - hs) & (pw * 8 - 1);
        uint32_t ea = (base + ((py >> 3) * pw + (px >> 3)) * 2) & 0xFFFF;
        uint16_t ent = (uint16_t)((V.vram[ea] << 8) | V.vram[ea | 1]);
        out[x + E] = tile_pixel(ent, px & 7, py & 7);
    }
}

static void draw_window_span(uint8_t *out, int line, int x0, int x1)
{
    uint32_t base = h40() ? ((V.reg[3] & 0x3C) << 10) : ((V.reg[3] & 0x3E) << 10);
    int pw = h40() ? 64 : 32;
    for (int x = x0; x < x1; x++) {
        uint32_t ea = (base + ((line >> 3) * pw + (x >> 3)) * 2) & 0xFFFF;
        uint16_t ent = (uint16_t)((V.vram[ea] << 8) | V.vram[ea | 1]);
        out[x] = tile_pixel(ent, x & 7, line & 7);
    }
}

static void draw_sprites(uint8_t *out, int line, int W, int E)
{
    memset(out, 0, (size_t)(W + 2 * E));
    uint32_t sat = h40() ? ((V.reg[5] & 0x7E) << 9) : ((V.reg[5] & 0x7F) << 9);
    int max_spr = h40() ? 80 : 64, max_line = h40() ? 20 : 16, max_px = W;
    if (E) { max_line = 80; max_px = 4096; }      /* wider view: no per-line sprite limit */
    int idx = 0, count = 0, onl = 0, pix = 0, masked = 0, prev_nonzero = 0;
    while (count++ < max_spr) {
        uint32_t e = (sat + idx * 8) & 0xFFFF;
        int y = (((V.vram[e] << 8) | V.vram[e + 1]) & 0x3FF) - 128;
        int vs = (V.vram[e + 2] & 3) + 1, hs = ((V.vram[e + 2] >> 2) & 3) + 1;
        int link = V.vram[e + 3] & 0x7F;
        if (line >= y && line < y + vs * 8) {
            uint16_t at = (uint16_t)((V.vram[e + 4] << 8) | V.vram[e + 5]);
            int xr = ((V.vram[e + 6] << 8) | V.vram[e + 7]) & 0x1FF;
            if (++onl > max_line) break;
            if (xr == 0 && prev_nonzero) masked = 1;
            if (xr) prev_nonzero = 1;
            int x = xr - 128;
            int row = line - y;
            if (at & 0x1000) row = vs * 8 - 1 - row;
            for (int c = 0; c < hs && !masked; c++) {
                int col = (at & 0x0800) ? hs - 1 - c : c;
                uint16_t ent = (uint16_t)((at & 0xF800 & ~0x1800) | ((at + col * vs + (row >> 3)) & 0x7FF) | (at & 0x6000));
                for (int p = 0; p < 8; p++) {
                    int sx = x + c * 8 + p;
                    if (++pix > max_px * 2) break;
                    if (sx < -E || sx >= W + E || out[sx + E]) continue;
                    int tp = (at & 0x0800) ? 7 - p : p;
                    uint8_t px = tile_pixel((uint16_t)(ent & ~0x1800), tp, row & 7);
                    if (px) out[sx + E] = px;
                }
            }
        }
        if (!link || link >= max_spr) break;
        idx = link;
    }
}

void vdp_render_line(int line)
{
    if (!rgb_init) init_rgb();
    int W = h40() ? 320 : 256;
    if (line == 0) {
        int e = g_ws_ext > 64 ? 64 : g_ws_ext < 0 ? 0 : g_ws_ext;
        int wide = (W == 320 && g_ws_begin) ? g_ws_begin() : 0;   /* also updates the field detector */
        g_ws_frame_ext = (e && wide) ? e : 0;
    }
    int E = W == 320 ? g_ws_frame_ext : 0;
    int TW = W + 2 * E;
    g_fb_width = TW;
    uint32_t *dst = g_fb[line];
    uint32_t bd = cram_rgb(V.reg[7] & 0x3F, 0);
    if (!(V.reg[1] & 0x40)) { for (int x = 0; x < MD_W_MAX; x++) dst[x] = bd; return; }

    static uint8_t la[MD_W_MAX], lb[MD_W_MAX], ls[MD_W_MAX];
    draw_plane(lb, line, W, E, (V.reg[4] & 7) << 13, 1);
    draw_plane(la, line, W, E, (V.reg[2] & 0x38) << 10, 0);
    /* window replaces plane A in its area (only inside the real 320 px view) */
    int vp = (V.reg[18] & 0x1F) * 8, down = V.reg[18] & 0x80;
    int hp = (V.reg[17] & 0x1F) * 16, right = V.reg[17] & 0x80;
    uint8_t *law = la + E;
    if (down ? line >= vp : line < vp) draw_window_span(law, line, 0, W);
    else if (hp) {
        if (right) { if (hp < W) draw_window_span(law, line, hp, W); }
        else draw_window_span(law, line, 0, hp < W ? hp : W);
    }
    draw_sprites(ls, line, W, E);

    int sh = (V.reg[12] & 0x08) != 0;
    for (int x = 0; x < TW; x++) {
        uint8_t a = la[x], b = lb[x], s = ls[x];
        uint8_t pix = 0; int lvl = 0;
        /* priority order: S hi, A hi, B hi, S lo, A lo, B lo */
        uint8_t order[6] = { 0 };
        int n = 0;
        if (s & 0x80) order[n++] = s;
        if (a & 0x80) order[n++] = a;
        if (b & 0x80) order[n++] = b;
        if (s && !(s & 0x80)) order[n++] = s;
        if (a && !(a & 0x80)) order[n++] = a;
        if (b && !(b & 0x80)) order[n++] = b;
        if (sh) {
            lvl = ((a & 0x80) || (b & 0x80)) ? 0 : 1;
            for (int i = 0; i < n; i++) {
                uint8_t p = order[i];
                if (p == s && (p & 0x3F) == 0x3E) { if (lvl < 2) lvl++; continue; }  /* pal 3 col 14: highlight */
                if (p == s && (p & 0x3F) == 0x3F) { lvl = lvl == 2 ? 0 : 1; continue; } /* pal 3 col 15: shadow */
                pix = p;
                if (p == s && (s & 0x80)) lvl = 0;
                break;
            }
            int col = pix ? (pix & 0x3F) : (V.reg[7] & 0x3F);
            dst[x] = cram_rgb(col, lvl == 1 ? 1 : lvl == 2 ? 2 : 0);
        } else {
            pix = n ? order[0] : 0;
            dst[x] = pix ? cram_rgb(pix & 0x3F, 0) : bd;
        }
    }
    for (int x = TW; x < MD_W_MAX; x++) dst[x] = 0;
}
