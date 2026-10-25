/* HD packs, see hdpack.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hdpack.h"
#include "png.h"
#include "md.h"

static hd_io_t io;
void hd_set_io(const hd_io_t *p) { io = *p; }
static void say(const char *fmt, ...);

/* ------------------------------------------------------------ keys */
static uint64_t fnv(const uint8_t *p, int n, uint64_t h)
{
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}
#define FNV0 0xCBF29CE484222325ull
static uint64_t pattern_hash(int tile) { return fnv(&V.vram[(tile & 0x7FF) * 32], 32, FNV0); }
static uint16_t c9(uint16_t c) { return (uint16_t)(((c >> 1) & 7) | (((c >> 5) & 7) << 3) | (((c >> 9) & 7) << 6)); }
static uint64_t palette_hash_of(const uint16_t *cols)
{
    uint8_t b[32];
    for (int i = 0; i < 16; i++) { b[i * 2] = (uint8_t)cols[i]; b[i * 2 + 1] = (uint8_t)(cols[i] >> 8); }
    return fnv(b, 32, FNV0 ^ 0x5A5A);
}
static void palette_now(int pal, uint16_t out[16]) { for (int i = 0; i < 16; i++) out[i] = c9(V.cram[(pal & 3) * 16 + i]); }
static uint32_t rgb9(uint16_t c)
{
    static const uint8_t lv[8] = { 0, 52, 87, 116, 144, 172, 206, 255 };
    return ((uint32_t)lv[c & 7] << 16) | ((uint32_t)lv[(c >> 3) & 7] << 8) | lv[(c >> 6) & 7];
}
static int pattern_empty(int tile) { const uint8_t *p = &V.vram[(tile & 0x7FF) * 32]; for (int i = 0; i < 32; i++) if (p[i]) return 0; return 1; }

/* ------------------------------------------------------------ the pack */
typedef struct {
    uint64_t pat, pal;
    uint16_t cap[16]; int has_cap;         /* the palette the tile was captured with */
    uint8_t *rgba;                         /* (8S)^2 pixels, R G B A */
    uint8_t *idx;                          /* nearest captured palette entry per pixel (recolouring) */
    int next_same_pat;
} tile_t;

static tile_t *T;
static int nt, capt, S;
#define HBITS 16
static int32_t *htab, *ptab;                /* (pat, pal) -> tile, pat -> first tile */
static uint64_t mix(uint64_t a, uint64_t b) { return a * 0x9E3779B97F4A7C15ull ^ (b + (a >> 29)); }

static void tables_reset(void)
{
    if (!htab) { htab = malloc(sizeof(int32_t) << HBITS); ptab = malloc(sizeof(int32_t) << HBITS); }
    for (int i = 0; i < (1 << HBITS); i++) htab[i] = ptab[i] = -1;
    for (int i = 0; i < nt; i++) { free(T[i].rgba); free(T[i].idx); }
    nt = 0;
}
static int find(uint64_t pat, uint64_t pal)
{
    if (!htab) return -1;
    uint32_t h = (uint32_t)(mix(pat, pal) >> (64 - HBITS));
    for (int k = 0; k < (1 << HBITS); k++, h = (h + 1) & ((1 << HBITS) - 1)) {
        int i = htab[h];
        if (i < 0) return -1;
        if (T[i].pat == pat && T[i].pal == pal) return i;
    }
    return -1;
}
static int find_pat(uint64_t pat)
{
    if (!ptab) return -1;
    uint32_t h = (uint32_t)((pat * 0x9E3779B97F4A7C15ull) >> (64 - HBITS));
    for (int k = 0; k < (1 << HBITS); k++, h = (h + 1) & ((1 << HBITS) - 1)) {
        int i = ptab[h];
        if (i < 0) return -1;
        if (T[i].pat == pat) return i;
    }
    return -1;
}
static void make_idx(tile_t *t)
{
    int n = 64 * S * S;
    t->idx = malloc((size_t)n);
    for (int i = 0; i < n; i++) {
        const uint8_t *p = t->rgba + i * 4;
        int best = 1, bd = 1 << 30;
        for (int k = 1; k < 16; k++) {
            uint32_t c = rgb9(t->cap[k]);
            int dr = p[0] - (int)(c >> 16), dg = p[1] - (int)((c >> 8) & 255), db = p[2] - (int)(c & 255);
            int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
            if (d < bd) { bd = d; best = k; }
        }
        t->idx[i] = (uint8_t)best;
    }
}
/* takes ownership of rgba */
static int add_tile(uint64_t pat, uint64_t pal, const uint16_t *cap, uint8_t *rgba)
{
    if (nt >= (1 << HBITS) / 2) { free(rgba); return -1; }
    if (nt == capt) { capt = capt ? capt * 2 : 1024; T = realloc(T, sizeof(tile_t) * (size_t)capt); }
    tile_t *t = &T[nt];
    memset(t, 0, sizeof *t);
    t->pat = pat; t->pal = pal; t->rgba = rgba; t->next_same_pat = -1;
    if (cap) { memcpy(t->cap, cap, sizeof t->cap); t->has_cap = 1; make_idx(t); }
    uint32_t h = (uint32_t)(mix(pat, pal) >> (64 - HBITS));
    while (htab[h] >= 0) h = (h + 1) & ((1 << HBITS) - 1);
    htab[h] = nt;
    int first = find_pat(pat);
    if (first < 0) {
        uint32_t g = (uint32_t)((pat * 0x9E3779B97F4A7C15ull) >> (64 - HBITS));
        while (ptab[g] >= 0) g = (g + 1) & ((1 << HBITS) - 1);
        ptab[g] = nt;
    } else {
        while (T[first].next_same_pat >= 0) first = T[first].next_same_pat;
        T[first].next_same_pat = nt;
    }
    return nt++;
}

int hd_tiles(void) { return nt; }
int hd_scale(void) { return nt ? S : 0; }

/* ------------------------------------------------------------ small text helpers */
static void hex64(char *o, uint64_t v) { static const char *x = "0123456789abcdef"; for (int i = 15; i >= 0; i--) { o[i] = x[v & 15]; v >>= 4; } o[16] = 0; }
static int parse_hex64(const char *s, uint64_t *v)
{
    uint64_t r = 0;
    for (int i = 0; i < 16; i++) {
        char c = s[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return 0;
        r = r << 4 | (uint64_t)d;
    }
    *v = r;
    return 1;
}
typedef struct { char *d; size_t n, cap; } sbuf_t;
static void sb_add(sbuf_t *b, const char *fmt, ...);
#include <stdarg.h>
static void sb_add(sbuf_t *b, const char *fmt, ...)
{
    char tmp[512];
    va_list a; va_start(a, fmt); int k = vsnprintf(tmp, sizeof tmp, fmt, a); va_end(a);
    if (k < 0) return;
    if (k > (int)sizeof tmp - 1) k = (int)sizeof tmp - 1;
    if (b->n + (size_t)k + 1 > b->cap) { b->cap = (b->cap + (size_t)k + 1) * 2; b->d = realloc(b->d, b->cap); }
    memcpy(b->d + b->n, tmp, (size_t)k); b->n += (size_t)k; b->d[b->n] = 0;
}
static void say(const char *fmt, ...)
{
    char tmp[400];
    va_list a; va_start(a, fmt); vsnprintf(tmp, sizeof tmp, fmt, a); va_end(a);
    if (io.log) io.log(tmp);
}

/* ------------------------------------------------------------ "seen" keys (auto capture) */
static uint64_t *seen; static int seen_cap, seen_n;
static int seen_has(uint64_t k)
{
    if (!seen) return 0;
    uint32_t h = (uint32_t)(k >> 40) & (seen_cap - 1);
    while (seen[h]) { if (seen[h] == k) return 1; h = (h + 1) & (seen_cap - 1); }
    return 0;
}
static void seen_add(uint64_t k)
{
    if (!k) k = 1;
    if (seen_n * 2 >= seen_cap) {
        int oc = seen_cap; uint64_t *old = seen;
        seen_cap = seen_cap ? seen_cap * 2 : 65536; seen = calloc((size_t)seen_cap, 8); seen_n = 0;
        for (int i = 0; i < oc; i++) if (old[i]) seen_add(old[i]);
        free(old);
    }
    if (seen_has(k)) return;
    uint32_t h = (uint32_t)(k >> 40) & (seen_cap - 1);
    while (seen[h]) h = (h + 1) & (seen_cap - 1);
    seen[h] = k; seen_n++;
}

/* ------------------------------------------------------------ map files */
typedef struct { uint64_t pat, pal; int hv; } cell_t;
typedef struct { int cols, rows; cell_t *c; int npal; uint64_t palkey[64]; uint16_t palcol[64][16]; } map_t;

static int parse_map(const char *txt, map_t *m)
{
    memset(m, 0, sizeof *m);
    const char *p = txt;
    if (strncmp(p, "PS4HDMAP 1", 10)) return 0;
    p = strchr(p, '\n'); if (!p) return 0; p++;
    if (sscanf(p, "size %d %d", &m->cols, &m->rows) != 2 || m->cols <= 0 || m->rows <= 0 || m->cols > 1024 || m->rows > 1024) return 0;
    m->c = calloc((size_t)m->cols * m->rows, sizeof(cell_t));
    p = strchr(p, '\n'); if (!p) return 0; p++;
    for (int y = 0; y < m->rows; y++)
        for (int x = 0; x < m->cols; x++) {
            while (*p == ' ' || *p == '\n' || *p == '\r') p++;
            if (!*p) return 0;
            cell_t *c = &m->c[y * m->cols + x];
            if (*p == '-') { p++; continue; }
            if (!parse_hex64(p, &c->pat) || p[16] != '_' || !parse_hex64(p + 17, &c->pal) || p[33] != '/') return 0;
            c->hv = p[34] - '0';
            p += 35;
        }
    while (*p) {
        while (*p == ' ' || *p == '\n' || *p == '\r') p++;
        if (!strncmp(p, "pal ", 4) && m->npal < 64) {
            p += 4;
            if (!parse_hex64(p, &m->palkey[m->npal])) break;
            p += 16;
            for (int i = 0; i < 16; i++) { unsigned v = 0; int k = 0; sscanf(p, " %x%n", &v, &k); p += k; m->palcol[m->npal][i] = (uint16_t)v; }
            m->npal++;
        } else {
            const char *e = strchr(p, '\n'); if (!e) break; p = e;
        }
    }
    return 1;
}
static const uint16_t *map_palette(const map_t *m, uint64_t key)
{
    for (int i = 0; i < m->npal; i++) if (m->palkey[i] == key) return m->palcol[i];
    return 0;
}

/* ------------------------------------------------------------ capture */
static int scene_no;

static void list_count_cb(const char *name, void *u)
{
    int n = 0;
    if (name[0] == 's' && sscanf(name + 1, "%d", &n) == 1 && n > *(int *)u) *(int *)u = n;
}

typedef struct {
    uint8_t *img; int w, h;                      /* RGBA */
    int cols, rows; cell_t *cells;
    uint64_t palkey[4]; uint16_t palcol[4][16]; int pal_used[4];
} layer_t;

static void layer_init(layer_t *L, int cols, int rows)
{
    memset(L, 0, sizeof *L);
    L->cols = cols; L->rows = rows; L->w = cols * 8; L->h = rows * 8;
    L->img = calloc((size_t)L->w * L->h, 4);
    L->cells = calloc((size_t)cols * rows, sizeof(cell_t));
    for (int p = 0; p < 4; p++) { palette_now(p, L->palcol[p]); L->palkey[p] = palette_hash_of(L->palcol[p]); }
}
static void layer_free(layer_t *L) { free(L->img); free(L->cells); }

/* a tile as shown (flips applied) into cell (cx, cy) */
static void layer_cell(layer_t *L, int cx, int cy, uint16_t ent)
{
    int tile = ent & 0x7FF, pal = (ent >> 13) & 3, hf = (ent >> 11) & 1, vf = (ent >> 12) & 1;
    if (cx < 0 || cy < 0 || cx >= L->cols || cy >= L->rows) return;
    if (pattern_empty(tile)) return;
    cell_t *c = &L->cells[cy * L->cols + cx];
    c->pat = pattern_hash(tile); c->pal = L->palkey[pal]; c->hv = hf | (vf << 1);
    L->pal_used[pal] = 1;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint8_t v = vdp_tile_pixel((uint16_t)(ent & 0x1FFF), x, y);  /* flips, no priority/palette bits needed */
            int ci = v & 15;
            uint8_t *o = L->img + ((size_t)(cy * 8 + y) * L->w + cx * 8 + x) * 4;
            if (!ci) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            uint32_t rgb = rgb9(L->palcol[pal][ci]);
            o[0] = (uint8_t)(rgb >> 16); o[1] = (uint8_t)(rgb >> 8); o[2] = (uint8_t)rgb; o[3] = 255;
        }
}

static int layer_save(layer_t *L, const char *name)
{
    int any = 0;
    for (int i = 0; i < L->cols * L->rows; i++) if (L->cells[i].pat) { any = 1; break; }
    if (!any) return 0;
    char path[128];
    size_t pn; uint8_t *png = png_encode(L->img, L->w, L->h, &pn);
    snprintf(path, sizeof path, "scenes/%s.png", name);
    int ok = io.write(path, png, pn);
    free(png);
    sbuf_t m = { 0, 0, 0 };
    sb_add(&m, "PS4HDMAP 1\nsize %d %d\n", L->cols, L->rows);
    for (int y = 0; y < L->rows; y++) {
        for (int x = 0; x < L->cols; x++) {
            cell_t *c = &L->cells[y * L->cols + x];
            if (!c->pat) sb_add(&m, x ? " -" : "-");
            else {
                char a[17], b[17]; hex64(a, c->pat); hex64(b, c->pal);
                sb_add(&m, "%s%s_%s/%d", x ? " " : "", a, b, c->hv);
                seen_add(mix(c->pat, c->pal));
            }
        }
        sb_add(&m, "\n");
    }
    for (int p = 0; p < 4; p++) {
        if (!L->pal_used[p]) continue;
        char k[17]; hex64(k, L->palkey[p]);
        sb_add(&m, "pal %s", k);
        for (int i = 0; i < 16; i++) sb_add(&m, " %03x", L->palcol[p][i]);
        sb_add(&m, "\n");
    }
    snprintf(path, sizeof path, "scenes/%s.map", name);
    ok &= io.write(path, m.d, m.n);
    free(m.d);
    return ok;
}

static int h40(void) { return (V.reg[12] & 1) != 0; }

static void capture_plane(layer_t *L, uint32_t base, int pw, int ph)
{
    layer_init(L, pw, ph);
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            uint32_t ea = (base + (y * pw + x) * 2) & 0xFFFF;
            layer_cell(L, x, y, (uint16_t)((V.vram[ea] << 8) | V.vram[ea | 1]));
        }
}

static void capture_sprites(layer_t *L)
{
    /* every sprite in the link list, once per distinct look, laid out on a
     * 64-cell wide sheet with one empty cell between sprites */
    enum { SW = 64, SH = 64 };
    layer_init(L, SW, SH);
    uint32_t sat = h40() ? ((V.reg[5] & 0x7E) << 9) : ((V.reg[5] & 0x7F) << 9);
    int max_spr = h40() ? 80 : 64, idx = 0, cx = 0, cy = 0, row_h = 0;
    uint32_t done[80]; int nd = 0;
    for (int count = 0; count < max_spr; count++) {
        uint32_t e = (sat + idx * 8) & 0xFFFF;
        int vs = (V.vram[e + 2] & 3) + 1, hs = ((V.vram[e + 2] >> 2) & 3) + 1;
        int link = V.vram[e + 3] & 0x7F;
        uint16_t at = (uint16_t)((V.vram[e + 4] << 8) | V.vram[e + 5]);
        uint32_t look = ((uint32_t)at << 4) | (uint32_t)((hs - 1) << 2) | (uint32_t)(vs - 1);
        int dup = 0;
        for (int i = 0; i < nd; i++) if (done[i] == look) dup = 1;
        if (!dup && nd < 80) {
            done[nd++] = look;
            if (cx + hs > SW) { cx = 0; cy += row_h + 1; row_h = 0; }
            if (cy + vs <= SH) {
                for (int c = 0; c < hs; c++)
                    for (int r = 0; r < vs; r++) {
                        int col = (at & 0x0800) ? hs - 1 - c : c, rr = (at & 0x1000) ? vs - 1 - r : r;
                        uint16_t ent = (uint16_t)((at & 0xF800) | ((at + col * vs + rr) & 0x7FF));
                        layer_cell(L, cx + c, cy + r, ent);
                    }
                cx += hs + 1;
                if (vs > row_h) row_h = vs;
            }
        }
        if (!link || link >= max_spr) break;
        idx = link;
    }
    int used = cy + row_h;                        /* keep only the rows in use */
    if (used < 1) used = 1;
    if (used < L->rows) { L->rows = used; L->h = used * 8; }
}

int hd_capture(void)
{
    if (!io.write) return 0;
    if (!scene_no) { int mx = 0; if (io.list) io.list("scenes", list_count_cb, &mx); scene_no = mx; }
    int no = ++scene_no;
    static const int sz[4] = { 32, 64, 32, 128 };
    int pw = sz[V.reg[16] & 3], ph = sz[(V.reg[16] >> 4) & 3];
    char name[32];
    layer_t L;
    int saved = 0;
    capture_plane(&L, (V.reg[2] & 0x38) << 10, pw, ph);
    snprintf(name, sizeof name, "s%04d_a", no); saved += layer_save(&L, name); layer_free(&L);
    capture_plane(&L, (V.reg[4] & 7) << 13, pw, ph);
    snprintf(name, sizeof name, "s%04d_b", no); saved += layer_save(&L, name); layer_free(&L);
    if ((V.reg[17] & 0x9F) || (V.reg[18] & 0x9F)) {
        uint32_t wb = h40() ? ((V.reg[3] & 0x3C) << 10) : ((V.reg[3] & 0x3E) << 10);
        capture_plane(&L, wb, h40() ? 64 : 32, 32);
        snprintf(name, sizeof name, "s%04d_w", no); saved += layer_save(&L, name); layer_free(&L);
    }
    capture_sprites(&L);
    snprintf(name, sizeof name, "s%04d_s", no); saved += layer_save(&L, name); layer_free(&L);
    say("hd: captured scene %d (%d layers)", no, saved);
    return saved ? no : 0;
}

void hd_auto_capture(void)
{
    static int tick;
    if (++tick < 30 || !g_hd_track) return;
    tick = 0;
    /* count the distinct tiles on screen that no capture has seen yet */
    static uint32_t mark[2048 * 4], gen;
    gen++;
    uint64_t palkey[4];
    for (int p = 0; p < 4; p++) { uint16_t c[16]; palette_now(p, c); palkey[p] = palette_hash_of(c); }
    int fresh = 0;
    for (int y = 0; y < MD_VIS_LINES; y++)
        for (int x = 0; x < g_fb_width; x++) {
            uint32_t s = g_src[y][x];
            if (!s) continue;
            int k = VSRC_TILE(s) * 4 + VSRC_PAL(s);
            if (mark[k] == gen) continue;
            mark[k] = gen;
            if (!seen_has(mix(pattern_hash(VSRC_TILE(s)), palkey[VSRC_PAL(s)])) && find(pattern_hash(VSRC_TILE(s)), palkey[VSRC_PAL(s)]) < 0) fresh++;
        }
    if (fresh >= 24) hd_capture();
}

/* ------------------------------------------------------------ load */
typedef struct { uint64_t pat, pal; uint16_t cols[16]; } palrec_t;
static palrec_t *pr; static int npr;
static const uint16_t *index_palette(uint64_t pat, uint64_t pal)
{
    for (int i = 0; i < npr; i++) if (pr[i].pat == pat && pr[i].pal == pal) return pr[i].cols;
    return 0;
}

typedef struct { char **names; int n, cap; } names_t;
static void names_cb(const char *name, void *u)
{
    names_t *v = u;
    if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 256; v->names = realloc(v->names, sizeof(char *) * (size_t)v->cap); }
    v->names[v->n++] = strdup(name);
}
static void names_free(names_t *v) { for (int i = 0; i < v->n; i++) free(v->names[i]); free(v->names); memset(v, 0, sizeof *v); }

static uint8_t *read_text(const char *rel)
{
    size_t n; uint8_t *b = io.read ? io.read(rel, &n) : 0;
    if (!b) return 0;
    uint8_t *t = realloc(b, n + 1); t[n] = 0;
    return t;
}

static void seen_from_scenes(void)
{
    names_t v = { 0, 0, 0 };
    if (io.list) io.list("scenes", names_cb, &v);
    for (int i = 0; i < v.n; i++) {
        size_t L = strlen(v.names[i]);
        if (L < 5 || strcmp(v.names[i] + L - 4, ".map")) continue;
        char path[300]; snprintf(path, sizeof path, "scenes/%s", v.names[i]);
        uint8_t *t = read_text(path);
        map_t m;
        if (t && parse_map((char *)t, &m))
            for (int k = 0; k < m.cols * m.rows; k++) if (m.c[k].pat) seen_add(mix(m.c[k].pat, m.c[k].pal));
        if (t) free(m.c);
        free(t);
    }
    names_free(&v);
}

int hd_load(void)
{
    tables_reset();
    if (!io.read) return 0;
    S = 4;
    uint8_t *t = read_text("tiles/pack.txt");
    if (t) { int s = 0; if (sscanf((char *)t, "scale %d", &s) == 1 && s >= 1 && s <= 4) S = s; free(t); }
    free(pr); pr = 0; npr = 0;
    t = read_text("tiles/index.txt");
    if (t) {
        for (char *p = (char *)t; *p;) {
            palrec_t r;
            if (parse_hex64(p, &r.pat) && p[16] == '_' && parse_hex64(p + 17, &r.pal)) {
                char *q = p + 33;
                for (int i = 0; i < 16; i++) { unsigned v = 0; int k = 0; sscanf(q, " %x%n", &v, &k); q += k; r.cols[i] = (uint16_t)v; }
                pr = realloc(pr, sizeof(palrec_t) * (size_t)(npr + 1)); pr[npr++] = r;
            }
            char *e = strchr(p, '\n'); if (!e) break; p = e + 1;
        }
        free(t);
    }
    names_t v = { 0, 0, 0 };
    if (io.list) io.list("tiles", names_cb, &v);
    int bad = 0;
    for (int i = 0; i < v.n; i++) {
        const char *nm = v.names[i];
        uint64_t pat, pal;
        if (strlen(nm) < 37 || !parse_hex64(nm, &pat) || nm[16] != '_' || !parse_hex64(nm + 17, &pal)) continue;
        char path[300]; snprintf(path, sizeof path, "tiles/%s", nm);
        size_t n; uint8_t *b = io.read(path, &n);
        if (!b) continue;
        int w, h; const char *err = 0;
        uint8_t *rgba = png_decode(b, n, &w, &h, &err);
        free(b);
        if (!rgba || w != 8 * S || h != 8 * S) { free(rgba); bad++; continue; }
        if (find(pat, pal) >= 0) { free(rgba); continue; }
        add_tile(pat, pal, index_palette(pat, pal), rgba);
    }
    names_free(&v);
    seen_from_scenes();
    say("hd: %d tiles loaded, scale %d%s", nt, S, bad ? " (some files skipped: wrong size or not PNG)" : "");
    return nt;
}

/* ------------------------------------------------------------ compose */
static float coverage;
float hd_coverage(void) { return coverage; }

int hd_compose(uint32_t *out, int pitch, int *ow, int *oh)
{
    if (!nt) return 0;
    int W = g_fb_width, H = MD_VIS_LINES, s = S;
    /* this frame's tiles: (tile, palette) -> pack entry, computed once */
    static int32_t ent[2048 * 4]; static uint8_t recol[2048 * 4]; static uint32_t gen_of[2048 * 4], gen;
    static uint64_t phash[2048]; static uint32_t pgen[2048];
    gen++;
    uint16_t pc[4][16]; uint64_t pk[4];
    for (int p = 0; p < 4; p++) { palette_now(p, pc[p]); pk[p] = palette_hash_of(pc[p]); }
    long hits = 0, total = 0;
    for (int y = 0; y < H; y++) {
        const uint32_t *srow = g_src[y], *frow = g_fb[y];
        for (int x = 0; x < W; x++) {
            uint32_t sv = srow[x], nat = frow[x];
            uint32_t *o = out + (size_t)y * s * pitch + (size_t)x * s;
            const tile_t *t = 0; int rc = 0;
            if (sv) {
                total++;
                int tile = VSRC_TILE(sv), pal = VSRC_PAL(sv), k = tile * 4 + pal;
                if (gen_of[k] != gen) {
                    gen_of[k] = gen;
                    if (pgen[tile] != gen) { pgen[tile] = gen; phash[tile] = pattern_hash(tile); }
                    int i = find(phash[tile], pk[pal]);
                    recol[k] = 0;
                    if (i < 0) {               /* same pattern, other colours: recolour a captured version */
                        for (int j = find_pat(phash[tile]); j >= 0; j = T[j].next_same_pat)
                            if (T[j].has_cap) { i = j; recol[k] = 1; break; }
                    }
                    ent[k] = i;
                }
                if (ent[k] >= 0 && !VSRC_LVL(sv)) { t = &T[ent[k]]; rc = recol[k]; hits++; }
            }
            if (!t) {
                for (int sy = 0; sy < s; sy++) for (int sx = 0; sx < s; sx++) o[(size_t)sy * pitch + sx] = nat;
                continue;
            }
            int cx = VSRC_CX(sv), cy = VSRC_CY(sv), hf = VSRC_HFLIP(sv), vf = VSRC_VFLIP(sv), ts = 8 * s;
            const uint16_t *cur = pc[VSRC_PAL(sv)];
            for (int sy = 0; sy < s; sy++) {
                int hy = cy * s + (vf ? s - 1 - sy : sy);
                for (int sx = 0; sx < s; sx++) {
                    int hx = cx * s + (hf ? s - 1 - sx : sx), pi = hy * ts + hx;
                    const uint8_t *p = t->rgba + (size_t)pi * 4;
                    uint32_t c;
                    if (p[3] < 128) c = nat;
                    else if (!rc) c = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                    else {
                        int k2 = t->idx[pi];
                        uint32_t a = rgb9(t->cap[k2]), b = rgb9(cur[k2]);
                        int ch[3];
                        for (int q = 0; q < 3; q++) {
                            int ca = (int)((a >> (16 - 8 * q)) & 255), cb = (int)((b >> (16 - 8 * q)) & 255), v = p[q];
                            int r = ca ? v * cb / ca : cb + v;
                            ch[q] = r > 255 ? 255 : r;
                        }
                        c = ((uint32_t)ch[0] << 16) | ((uint32_t)ch[1] << 8) | (uint32_t)ch[2];
                    }
                    o[(size_t)sy * pitch + sx] = c;
                }
            }
        }
    }
    coverage = total ? (float)hits / (float)total : 0;
    *ow = W * s; *oh = H * s;
    return s;
}

/* ------------------------------------------------------------ import */
typedef struct { const char *base; char found[300]; } upfind_t;
static void upfind_cb(const char *name, void *u)
{
    upfind_t *f = u;
    size_t L = strlen(f->base), n = strlen(name);
    if (f->found[0] || n < L + 4 || strncmp(name, f->base, L)) return;
    char c = name[L];
    if (c != '.' && c != '_' && c != '-' && c != ' ' && c != '(') return;   /* s0001_a must not match s0001_ab */
    const char *ext = name + n - 4;
    if (strcmp(ext, ".png") && strcmp(ext, ".PNG")) return;
    snprintf(f->found, sizeof f->found, "%s", name);
}

static int write_index(void)
{
    sbuf_t b = { 0, 0, 0 };
    for (int i = 0; i < nt; i++) {
        if (!T[i].has_cap) continue;
        char a[17], c[17]; hex64(a, T[i].pat); hex64(c, T[i].pal);
        sb_add(&b, "%s_%s", a, c);
        for (int k = 0; k < 16; k++) sb_add(&b, " %03x", T[i].cap[k]);
        sb_add(&b, "\n");
    }
    int ok = io.write("tiles/index.txt", b.d ? b.d : "", b.n);
    free(b.d);
    char p[32]; int k = snprintf(p, sizeof p, "scale %d\n", S);
    ok &= io.write("tiles/pack.txt", p, (size_t)k);
    return ok;
}

int hd_import(char *report, size_t rn)
{
    if (!io.read || !io.write || !io.list) return 0;
    if (!htab) tables_reset();
    if (!nt) S = 0;                              /* an empty pack takes the scale of the first picture */
    names_t v = { 0, 0, 0 };
    io.list("scenes", names_cb, &v);
    int added = 0, pictures = 0, missing = 0, wrong = 0;
    char wrong_name[200] = "";
    for (int i = 0; i < v.n; i++) {
        size_t L = strlen(v.names[i]);
        if (L < 5 || strcmp(v.names[i] + L - 4, ".map")) continue;
        char base[256]; snprintf(base, sizeof base, "%.*s", (int)(L - 4), v.names[i]);
        upfind_t f; f.base = base; f.found[0] = 0;
        io.list("upscaled", upfind_cb, &f);
        if (!f.found[0]) { missing++; continue; }
        char path[600];
        snprintf(path, sizeof path, "scenes/%s", v.names[i]);
        uint8_t *mt = read_text(path);
        map_t m;
        if (!mt || !parse_map((char *)mt, &m)) { free(mt); continue; }
        free(mt);
        snprintf(path, sizeof path, "upscaled/%s", f.found);
        size_t n; uint8_t *b = io.read(path, &n);
        int w = 0, h = 0; const char *err = 0;
        uint8_t *up = b ? png_decode(b, n, &w, &h, &err) : 0;
        free(b);
        int k = m.cols ? w / (m.cols * 8) : 0;
        if (!up || k < 1 || k > 16 || w != m.cols * 8 * k || h != m.rows * 8 * k) {
            wrong++; snprintf(wrong_name, sizeof wrong_name, "%s", f.found);
            free(up); free(m.c); continue;
        }
        pictures++;
        /* upscalers that drop transparency fill it with a colour: take the
         * transparent areas from the original picture then */
        {
            int has_alpha = 0;
            for (size_t q = 0; q < (size_t)w * h && !has_alpha; q++) if (up[q * 4 + 3] < 128) has_alpha = 1;
            if (!has_alpha) {
                snprintf(path, sizeof path, "scenes/%s.png", base);
                size_t nn; uint8_t *nb = io.read(path, &nn);
                int w0 = 0, h0 = 0;
                uint8_t *nat = nb ? png_decode(nb, nn, &w0, &h0, &err) : 0;
                free(nb);
                if (nat && w0 * k == w && h0 * k == h)
                    for (int yy = 0; yy < h; yy++)
                        for (int xx = 0; xx < w; xx++)
                            up[((size_t)yy * w + xx) * 4 + 3] = nat[((size_t)(yy / k) * w0 + xx / k) * 4 + 3];
                free(nat);
            }
        }
        if (!S) S = k > 4 ? 4 : k;
        int ts = 8 * S;
        for (int cy = 0; cy < m.rows; cy++)
            for (int cx = 0; cx < m.cols; cx++) {
                cell_t *c = &m.c[cy * m.cols + cx];
                if (!c->pat || find(c->pat, c->pal) >= 0) continue;
                uint8_t *tile = malloc((size_t)ts * ts * 4);
                int opaque = 0;
                for (int y = 0; y < ts; y++)
                    for (int x = 0; x < ts; x++) {
                        /* the cell as drawn was flipped: read it back unflipped */
                        int dx = (c->hv & 1) ? ts - 1 - x : x, dy = (c->hv & 2) ? ts - 1 - y : y;
                        /* source block in the upscaled picture (box filter when it is larger than S) */
                        int x0 = (cx * 8 * k) + dx * k / S, x1 = (cx * 8 * k) + (dx + 1) * k / S;
                        int y0 = (cy * 8 * k) + dy * k / S, y1 = (cy * 8 * k) + (dy + 1) * k / S;
                        if (x1 <= x0) x1 = x0 + 1;
                        if (y1 <= y0) y1 = y0 + 1;
                        int acc[4] = { 0, 0, 0, 0 }, cnt = 0;
                        for (int yy = y0; yy < y1; yy++)
                            for (int xx = x0; xx < x1; xx++) {
                                const uint8_t *p = up + ((size_t)yy * w + xx) * 4;
                                for (int q = 0; q < 4; q++) acc[q] += p[q];
                                cnt++;
                            }
                        uint8_t *o = tile + ((size_t)y * ts + x) * 4;
                        for (int q = 0; q < 4; q++) o[q] = (uint8_t)(acc[q] / cnt);
                        if (o[3] >= 128) opaque++;
                    }
                if (!opaque) { free(tile); continue; }
                size_t pn; uint8_t *png = png_encode(tile, ts, ts, &pn);
                char a[17], pp[17]; hex64(a, c->pat); hex64(pp, c->pal);
                snprintf(path, sizeof path, "tiles/%s_%s.png", a, pp);
                int ok = io.write(path, png, pn);
                free(png);
                if (!ok) { free(tile); continue; }
                add_tile(c->pat, c->pal, map_palette(&m, c->pal), tile);
                added++;
            }
        free(up); free(m.c);
    }
    names_free(&v);
    if (nt) write_index();
    int len = snprintf(report, rn, "новых плиток: %d (картинок: %d)", added, pictures);
    if (missing && len > 0 && (size_t)len < rn) len += snprintf(report + len, rn - (size_t)len, ", без увеличенной версии: %d", missing);
    if (wrong && len > 0 && (size_t)len < rn) snprintf(report + len, rn - (size_t)len, ", не подошёл размер: %d (например %s)", wrong, wrong_name);
    say("hd import: %d tiles added from %d pictures, %d scenes without upscaled picture, %d of wrong size", added, pictures, missing, wrong);
    return added;
}
