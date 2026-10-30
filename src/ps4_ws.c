/*
 * Phantasy Star IV widescreen: paints the columns outside the console's
 * 320-pixel view from the game's own field map data.
 *
 * Field maps (towns, dungeons, the world map) are grids of 32x32 blocks:
 *   $FFA000  plane A (upper layer) block map, row stride ($FFEC64)+1
 *   $FFB000  plane B (ground) block map, row stride ($FFEC66)+1
 *   $FF6000  block definitions, 16 tile words (4x4) per block
 * The world map keeps only 32 rows of 128 blocks, as a ring (4 KB).
 * Camera in pixels: plane A $FFEF94 (x), $FFEF90 (y); plane B $FFEF9C, $FFEF98
 * (the vertical blank handler at $000440 writes them to the VDP). Most maps
 * scroll both alike; some (the Bio-plant...) move plane B at another speed.
 * Tile words: plane A & $BFFF; plane B & $BFFF, or & $1FFF when $FFEC4F != 0
 * (the game's own block writers, $054506 and $054556).
 *
 * Every frame we check that the visible part of both planes matches what
 * the map data says (it does, 100%, while walking). A matching frame
 * refreshes a snapshot of the map; painting uses the snapshot, so dialogs
 * and menus drawn over the planes (which may reuse map RAM for portraits)
 * keep the sides intact. When the screen is not the field map (battles,
 * cutscenes), nothing matches and the frame is drawn 4:3.
 *
 * Battles (top-level game mode $FFEF00 = $14, dispatched at $0003DC) draw their background on plane B, one
 * 320-pixel picture, and everything else (enemies, windows) on plane A and
 * sprites. The sides there show plane B mirrored at the screen edges, which
 * continues the walls and floors without a seam; plane A stays empty. If the
 * background is wider than the screen, its real columns are shown instead.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "md.h"

static uint8_t snap_maps[0x2000];       /* $FFA000-$FFBFFF */
/* the tile patterns of the last clear frame: the game's menus load their
 * own graphics over some of the map's tiles, which would show as squares
 * at the sides while a menu is open */
static uint8_t snap_vram[0x10000];
static uint8_t snap_tile_pixel(uint16_t ent, int px, int py)
{
    int tile = ent & 0x7FF;
    if (ent & 0x0800) px = 7 - px;
    if (ent & 0x1000) py = 7 - py;
    uint8_t b = snap_vram[(tile * 32 + py * 4 + (px >> 1)) & 0xFFFF];
    int c = (px & 1) ? (b & 15) : (b >> 4);
    if (!c) return 0;
    return (uint8_t)(((ent >> 8) & 0x80) | (((ent >> 13) & 3) << 4) | c);
}
static uint16_t snap_blocks[0x1000];    /* $FF6000-$FF7FFF as words */
static int snap_valid, snap_wa, snap_ha, snap_wb, snap_hb, snap_maskb;
static int cam_x, cam_y;                /* world position of screen (0,0) this frame, plane A */
static int camb_x, camb_y;              /* the same for plane B (it scrolls on its own in some maps) */
static int battle, battle_mirror;       /* this frame is a battle; its background is one screen wide */
int ps4_field_live;                     /* last frame showed the field map unobstructed */
static void fog_mark(void);
static void world_capture(void);
static int ps4_field_live_any;          /* the field, maybe under windows */
int g_ws_probe;                         /* run the field check even at 4:3 */
int g_ws_battle = 1;                    /* widen battles too */
int g_ws_cutscene;                      /* last frame was neither the field nor a battle (story pictures, title) */

static inline uint16_t ramw(uint32_t a) { a &= 0xFFFF; return (uint16_t)((g_ram[a] << 8) | g_ram[a + 1]); }

static int plane_hs(int plane)
{
    uint32_t hsb = (V.reg[13] & 0x3F) << 10;
    uint32_t a = (hsb + plane * 2) & 0xFFFF;
    return ((V.vram[a] << 8) | V.vram[a | 1]) & 0x3FF;
}

/* tile word from a map (live RAM or snapshot) */
static int map_tile(const uint8_t *maps, const uint16_t *blocks, int plane, int w, int h, int mask, int wx, int wy)
{
    int bx = wx >> 5, by = wy >> 5;
    int ring_rows = w ? 4096 / w : 0;
    int wraps = w * h > 4096;                     /* the world map */
    if (wraps) {
        bx %= w; if (bx < 0) bx += w;
        by %= ring_rows; if (by < 0) by += ring_rows;
    } else if (wx < 0 || wy < 0 || bx >= w || by >= h) return -1;
    int blk = maps[(plane ? 0x1000 : 0) + ((by * w + bx) & 0xFFF)];
    return blocks[(blk * 16 + ((wy >> 3) & 3) * 4 + ((wx >> 3) & 3)) & 0xFFF] & mask;
}

static int verify(const uint8_t *maps, const uint16_t *blocks, int wa, int ha, int wb, int hb, int maskb)
{
    static const int sz[4] = { 32, 64, 32, 128 };
    int pw = sz[V.reg[16] & 3], ph = sz[(V.reg[16] >> 4) & 3];
    int ok = 0, n = 0;
    for (int plane = 0; plane < 2; plane++) {
        uint32_t base = plane ? (V.reg[4] & 7) << 13 : (V.reg[2] & 0x38) << 10;
        int w = plane ? wb : wa, h = plane ? hb : ha, mask = plane ? maskb : 0xBFFF;
        for (int ty = 0; ty < 28; ty += 2)
            for (int tx = 0; tx < 40; tx += 2) {
                int wx = (plane ? camb_x : cam_x) + tx * 8, wy = (plane ? camb_y : cam_y) + ty * 8;
                int exp = map_tile(maps, blocks, plane, w, h, mask, wx, wy);
                uint32_t ea = (base + ((((wy >> 3) & (ph - 1)) * pw) + ((wx >> 3) & (pw - 1))) * 2) & 0xFFFF;
                int got = (V.vram[ea] << 8) | V.vram[ea | 1];
                n++;
                if (exp >= 0 && exp == got) ok++;
            }
    }
    return n ? ok * 100 / n : 0;
}

static int battle_begin(void)
{
    static const int sz[4] = { 32, 64, 32, 128 };
    int pw = sz[V.reg[16] & 3], ph = sz[(V.reg[16] >> 4) & 3];
    uint32_t base = (V.reg[4] & 7) << 13;
    battle_mirror = 1;
    if (pw > 40)
        for (int y = 0; y < ph && battle_mirror; y++)
            for (int x = 40; x < pw; x++) {
                uint32_t ea = (base + (y * pw + x) * 2) & 0xFFFF;
                if (V.vram[ea] | V.vram[ea | 1]) { battle_mirror = 0; break; }
            }
    return 1;
}

static int ws_classify(void);
static int ws_begin(void)
{
    int r = ws_classify();
    g_ws_cutscene = !battle && !r && !ps4_field_live_any;
    return r;
}
static int ws_classify(void)
{
    battle = 0;
    ps4_field_live_any = 0;
    int in_battle = ((g_ram[0xEF00] << 8) | g_ram[0xEF01]) == 0x14;
    if (in_battle && (!g_ws_ext || !g_ws_battle)) { battle = 1; ps4_field_live = 0; return 0; }
    if (g_ws_ext && g_ws_battle && in_battle && (V.reg[12] & 1)) {
        ps4_field_live = 0;
        battle = 1;
        return battle_begin();
    }
    /* world camera, kept consistent with what the VDP is scrolling */
    {
        int rx = ramw(0xEF94), ry = ramw(0xEF90);
        int vx = (-plane_hs(0)) & 511, vy = V.vsram[0] & 255;
        cam_x = rx + ((((vx - rx) % 512) + 512 + 256) % 512 - 256);
        cam_y = ry + ((((vy - ry) % 256) + 256 + 128) % 256 - 128);
        rx = ramw(0xEF9C); ry = ramw(0xEF98);
        vx = (-plane_hs(1)) & 511; vy = V.vsram[1] & 255;
        camb_x = rx + ((((vx - rx) % 512) + 512 + 256) % 512 - 256);
        camb_y = ry + ((((vy - ry) % 256) + 256 + 128) % 256 - 128);
    }
    ps4_field_live = 0;
    if ((V.reg[11] & 7) != 0) return 0;          /* field maps use full-screen scrolling */

    int wa = g_ram[0xEC64] + 1, ha = g_ram[0xEC65] + 1, wb = g_ram[0xEC66] + 1, hb = g_ram[0xEC67] + 1;
    int maskb = g_ram[0xEC4F] ? 0x1FFF : 0xBFFF;
    if (wa < 8 || wb < 8) return 0;
    if (g_ws_ext == 0 && !g_ws_probe) { ps4_field_live_any = 1; return 0; }   /* not checked: assume the field */
    static uint16_t live_blocks[0x1000];
    for (int i = 0; i < 0x1000; i++) live_blocks[i] = ramw(0x6000 + i * 2);
    int live = verify(&g_ram[0xA000], live_blocks, wa, ha, wb, hb, maskb);
    ps4_field_live = live >= 98;
    if (live >= 98) {
        ps4_field_live_any = 1;
        memcpy(snap_maps, &g_ram[0xA000], sizeof snap_maps);
        memcpy(snap_blocks, live_blocks, sizeof snap_blocks);
        snap_wa = wa; snap_ha = ha; snap_wb = wb; snap_hb = hb; snap_maskb = maskb;
        memcpy(snap_vram, V.vram, sizeof snap_vram);
        snap_valid = 1;
        fog_mark();
        world_capture();
        return 1;
    }
    if (!snap_valid) return 0;
    /* windows/dialogs over the field: keep painting from the snapshot */
    int ok = verify(snap_maps, snap_blocks, snap_wa, snap_ha, snap_wb, snap_hb, snap_maskb) >= 40;
    if (g_ws_ext == 0) { ps4_field_live_any = ok; return 0; }   /* probing only */
    return ok;
}

/* a plane B pixel at screen position (sx, line), with the VDP's scrolling */
static uint8_t plane_b_pixel(int sx, int line)
{
    static const int sz[4] = { 32, 64, 32, 128 };
    int pw = sz[V.reg[16] & 3], ph = sz[(V.reg[16] >> 4) & 3];
    uint32_t hsb = (V.reg[13] & 0x3F) << 10;
    int hmode = V.reg[11] & 3, hidx = hmode == 0 ? 0 : hmode == 2 ? (line & ~7) : line;
    uint32_t ha = (hsb + hidx * 4 + 2) & 0xFFFF;
    int hs = ((V.vram[ha] << 8) | V.vram[ha | 1]) & 0x3FF;
    int vs = V.vsram[1];
    int py = (line + vs) & (ph * 8 - 1), px = (sx - hs) & (pw * 8 - 1);
    uint32_t ea = (((V.reg[4] & 7) << 13) + ((py >> 3) * pw + (px >> 3)) * 2) & 0xFFFF;
    uint16_t ent = (uint16_t)((V.vram[ea] << 8) | V.vram[ea | 1]);
    if (g_hd_track) g_ws_src = vdp_make_src(ent, px & 7, py & 7);
    return vdp_tile_pixel(ent, px & 7, py & 7);
}

static uint8_t ws_pixel(int plane, int sx, int line)
{
    if (battle) {
        if (plane == 0) return 0;
        if (battle_mirror) sx = sx < 0 ? -1 - sx : 639 - sx;
        return plane_b_pixel(sx, line);
    }
    int wx = (plane ? camb_x : cam_x) + sx, wy = (plane ? camb_y : cam_y) + line;
    int t = map_tile(snap_maps, snap_blocks, plane, plane ? snap_wb : snap_wa, plane ? snap_hb : snap_ha,
                     plane ? snap_maskb : 0xBFFF, wx, wy);
    if (t < 0) return 0;
    if (g_hd_track) g_ws_src = vdp_make_src((uint16_t)t, wx & 7, wy & 7);
    return ps4_field_live ? vdp_tile_pixel((uint16_t)t, wx & 7, wy & 7) : snap_tile_pixel((uint16_t)t, wx & 7, wy & 7);
}

/* The game culls sprite pieces outside the 320-pixel view; widen the test
 * (hints.json "patches", which the recompiler builds into the generated
 * code). Written into the ROM image too, for the interpreter. */
void ps4_ws_patch_rom(int ext)
{
    static const uint32_t lo[3] = { 0x044C08, 0x044C86, 0x044D4E }, hi[3] = { 0x044C0C, 0x044C8A, 0x044D52 };
    for (int i = 0; i < 3; i++) {
        uint16_t a = (uint16_t)(0x41 - ext), b = (uint16_t)(0x17F + 2 * ext);
        g_rom[lo[i]] = (uint8_t)(a >> 8); g_rom[lo[i] + 1] = (uint8_t)a;
        g_rom[hi[i]] = (uint8_t)(b >> 8); g_rom[hi[i] + 1] = (uint8_t)b;
    }
}

void ps4_ws_install(void)
{
    g_ws_begin = ws_begin;
    g_ws_pixel = ws_pixel;
    snap_valid = 0;
}

/* ---- fog of war: the 32x32 blocks of each map seen so far ----
 * Map number $FFEC28 (the loader at $05AFDE); up to 64x64 blocks per map. */
#define FOG_MAPS 512
static uint8_t fog[FOG_MAPS][512];
int g_fog_on;
static void fog_mark(void)
{
    int id = ramw(0xEC28);
    if (id >= FOG_MAPS || snap_wa * snap_ha > 4096) return;   /* the world map is not fogged */
    int w = snap_wa, h = snap_ha;
    int cx = ramw(0xEF94), cy = ramw(0xEF90);
    for (int by = (cy >> 5) - 1; by <= ((cy + 224) >> 5) + 1; by++)
        for (int bx = (cx >> 5) - 1; bx <= ((cx + 320) >> 5) + 1; bx++)
            if (bx >= 0 && by >= 0 && bx < w && by < h && bx < 64 && by < 64) fog[id][(by * 64 + bx) >> 3] |= (uint8_t)(1 << (bx & 7));
}
static int fog_seen(int id, int bx, int by)
{
    if (!g_fog_on || id >= FOG_MAPS) return 1;
    if (bx < 0 || by < 0 || bx >= 64 || by >= 64) return 0;
    return (fog[id][(by * 64 + bx) >> 3] >> (bx & 7)) & 1;
}
uint8_t *ps4_fog_data(int *size) { *size = sizeof fog; return &fog[0][0]; }

/* ---- minimap: the field map around the party at 1/4 size ----
 * Each output pixel is one pixel of the map (plane A over plane B) sampled
 * every 4 world pixels; the party leader is object $FFC000 (x.w at +$30,
 * y.w at +$34). 1 when the field is on screen and the map is known. */
static uint32_t tile_pixel(int t, int x, int y, int *opaque)
{
    int pat = t & 0x7FF, pal = (t >> 13) & 3;
    if (t & 0x800) x = 7 - x;
    if (t & 0x1000) y = 7 - y;
    uint8_t b = V.vram[(pat * 32 + y * 4 + (x >> 1)) & 0xFFFF];
    int ci = (x & 1) ? (b & 15) : (b >> 4);
    *opaque = ci != 0;
    return vdp_color(pal * 16 + ci);
}
int ps4_ws_minimap(uint32_t *out, int ow, int oh, int *mx, int *my, int *full_w, int *full_h)
{
    if (!snap_valid || !ps4_field_live_any || ramw(0xEF00) != 0x0C) return 0;
    int clear = ps4_field_live;   /* 2: nothing drawn over the field */
    int px = ramw(0xC030), py = ramw(0xC034);
    int wa = snap_wa, ha = snap_ha;
    int wraps = wa * ha > 4096;
    int mw = wa * 32, mh = ha * 32;                  /* map size in world pixels */
    if (full_w) *full_w = wraps ? 0 : mw / 4;
    if (full_h) *full_h = wraps ? 0 : mh / 4;
    /* window origin in world pixels: centred on the party, kept inside small maps */
    int x0 = px - ow * 2, y0 = py - oh * 2;
    if (!wraps) {
        if (ow * 4 >= mw) x0 = (mw - ow * 4) / 2; else { if (x0 < 0) x0 = 0; if (x0 > mw - ow * 4) x0 = mw - ow * 4; }
        if (oh * 4 >= mh) y0 = (mh - oh * 4) / 2; else { if (y0 < 0) y0 = 0; if (y0 > mh - oh * 4) y0 = mh - oh * 4; }
    }
    uint32_t bd = vdp_color(V.reg[7] & 0x3F);
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            int wx = x0 + x * 4 + 2, wy = y0 + y * 4 + 2;
            uint32_t c = 0x000000;
            int ta = map_tile(snap_maps, snap_blocks, 0, snap_wa, snap_ha, 0xBFFF, wx, wy);
            int tb = map_tile(snap_maps, snap_blocks, 1, snap_wb, snap_hb, snap_maskb, wx + camb_x - cam_x, wy + camb_y - cam_y);
            if (ta < 0 && tb < 0) { out[y * ow + x] = 0x000000; continue; }
            if (!wraps && !fog_seen(ramw(0xEC28), wx >> 5, wy >> 5)) { out[y * ow + x] = 0x14141E; continue; }
            int op = 0;
            if (ta >= 0) c = tile_pixel(ta, wx & 7, wy & 7, &op);
            if (!op) { c = bd; if (tb >= 0) { int ob; uint32_t cb = tile_pixel(tb, (wx + camb_x - cam_x) & 7, (wy + camb_y - cam_y) & 7, &ob); if (ob) c = cb; } }
            out[y * ow + x] = c;
        }
    *mx = (px - x0) / 4; *my = (py - y0) / 4;
    return clear ? 2 : 1;
}

/* ---- world maps: the ring of 32 block rows in RAM is copied into a whole
 * map as the party travels, with the colours of each block sampled once
 * (one per 8x8 tile) while its tiles are in VRAM ---- */
#define WORLDS 4
#define WROWS 256
typedef struct {
    int id, w, h;                       /* map number, size in blocks */
    uint8_t blk[2][WROWS][128];         /* plane A, plane B block ids */
    uint8_t known[WROWS][16];           /* rows / columns seen */
    uint32_t col[2][256][16];           /* block colours, top bit set = opaque (plane A) */
    uint8_t col_ok[2][256 / 8];
} world_t;
static world_t worlds[WORLDS];
static world_t *world_for(int id, int w, int h)
{
    for (int i = 0; i < WORLDS; i++) if (worlds[i].w && worlds[i].id == id) return &worlds[i];
    for (int i = 0; i < WORLDS; i++) if (!worlds[i].w) { memset(&worlds[i], 0, sizeof worlds[i]); worlds[i].id = id; worlds[i].w = w; worlds[i].h = h; return &worlds[i]; }
    return 0;
}
static void block_colours(world_t *W, int plane, int blk, int mask)
{
    if (W->col_ok[plane][blk >> 3] & (1 << (blk & 7))) return;
    for (int t = 0; t < 16; t++) {
        int tw = snap_blocks[(blk * 16 + t) & 0xFFF] & mask, op;
        uint32_t c = tile_pixel(tw, 3, 3, &op);
        W->col[plane][blk][t] = c | (op ? 0x80000000u : 0);
    }
    W->col_ok[plane][blk >> 3] |= (uint8_t)(1 << (blk & 7));
}
/* The two planets' maps are kept whole in the ROM, 8 block rows (1 KB) per
 * piece, behind tables of pointers (the row loader at $053C36 / $053C8C):
 * Motavia (map 0) 16 pieces, Dezolis (map 1) 8. Read from there, the world
 * map is whole and exact; what the party has seen is only marked. */
static const uint32_t WTAB[2][2] = { { 0x107DC2, 0x10BE02 }, { 0x115584, 0x1175C4 } };
static int world_rom_ok[2] = { -1, -1 };
static const uint8_t *world_rom_row(int id, int plane, int by)
{
    if (id < 0 || id > 1) return 0;
    if (world_rom_ok[id] < 0) {   /* not checked against the game yet: the known ROM's tables are trusted */
        static const uint32_t first[2][2] = { { 0x107E02, 0x10BE42 }, { 0x1155C4, 0x117604 } };
        int ok = g_rom_size >= 0x120000;
        for (int pl = 0; pl < 2 && ok; pl++) {
            uint32_t t = WTAB[id][pl];
            ok = ((uint32_t)g_rom[t] << 24 | (uint32_t)g_rom[t + 1] << 16 | (uint32_t)g_rom[t + 2] << 8 | g_rom[t + 3]) == first[id][pl];
        }
        if (ok) world_rom_ok[id] = 1;
    }
    if (world_rom_ok[id] != 1) return 0;
    uint32_t t = WTAB[id][plane] + (uint32_t)(by >> 3) * 4;
    uint32_t a = (uint32_t)g_rom[t] << 24 | (uint32_t)g_rom[t + 1] << 16 | (uint32_t)g_rom[t + 2] << 8 | g_rom[t + 3];
    return g_rom + a + (by & 7) * 128;
}
/* the ROM tables are used once the rows the game loaded match them */
static void world_rom_check(int id, int w, int h)
{
    if (id < 0 || id > 1 || world_rom_ok[id] >= 0) return;
    int pieces = h / 8;
    if (w != 128 || pieces < 1 || pieces > 16 || g_rom_size < 0x120000) { world_rom_ok[id] = 0; return; }
    for (int pl = 0; pl < 2; pl++)
        for (int i = 0; i < pieces; i++) {
            uint32_t t = WTAB[id][pl] + i * 4;
            uint32_t a = (uint32_t)g_rom[t] << 24 | (uint32_t)g_rom[t + 1] << 16 | (uint32_t)g_rom[t + 2] << 8 | g_rom[t + 3];
            if (a < 0x200 || a + 1024 > g_rom_size) { world_rom_ok[id] = 0; return; }
        }
    /* compare the rows in view (the camera's) with the ROM */
    int cy = ramw(0xEF90) >> 5, ring = 4096 / w, same = 0, n = 0;
    world_rom_ok[id] = 1;
    for (int by = cy; by < cy + 7 && by < h; by++) {
        const uint8_t *ra = world_rom_row(id, 0, by);
        for (int bx = 0; bx < w; bx++, n++) same += snap_maps[((by % ring) * w + bx) & 0xFFF] == ra[bx];
    }
    world_rom_ok[id] = n && same * 100 >= n * 97 ? 1 : -1;   /* -1: look again later */
}
static int last_world = -1, last_wx = -1, last_wy = -1;
static void world_capture(void)
{
    int w = snap_wa, h = snap_ha, id = ramw(0xEC28);
    /* the planets: Motavia 0 and Dezolis 1 (wrapping, streamed), Rykros 2 (whole in memory) */
    if (id > 2 || w > 128 || h > WROWS) return;
    int wraps = w * h > 4096;
    world_t *W = world_for(id, w, h);
    if (!W) return;
    last_world = id; last_wx = ramw(0xC030); last_wy = ramw(0xC034);
    if (wraps) world_rom_check(id, w, h);
    /* only the rows on screen are surely loaded (the ring keeps older rows) */
    int ring = wraps ? 4096 / w : h, cy = ramw(0xEF90) >> 5;
    for (int by = cy - 1; by <= cy + 8; by++) {
        if (by < 0 || by >= h) continue;
        int r = by % ring;
        for (int bx = 0; bx < w; bx++) {
            int a = snap_maps[(r * w + bx) & 0xFFF], b = snap_maps[0x1000 + ((r * w + bx) & 0xFFF)];
            W->blk[0][by][bx] = (uint8_t)a; W->blk[1][by][bx] = (uint8_t)b;
            W->known[by][bx >> 3] |= (uint8_t)(1 << (bx & 7));
        }
    }
    /* the planet's whole set of blocks is in memory: colour them all once */
    for (int k = 0; k < 256; k++) { block_colours(W, 0, k, 0xBFFF); block_colours(W, 1, k, snap_maskb); }
}
int ps4_world_pick = -1;   /* the planet to show, -1: where the party is */
/* 1 if that planet's map has been seen */
int ps4_world_known(int planet) { for (int i = 0; i < WORLDS; i++) if (worlds[i].w && worlds[i].id == planet) return 1; return 0; }
/* the whole world map, one pixel per tile (w*4 x h*4): the planet the party
 * is on, or the last one visited; unexplored parts darker. *px < 0: the
 * party is not on it. 0 if no planet map is known */
int ps4_world_image(uint32_t *out, int maxw, int maxh, int *ow, int *oh, int *px, int *py)
{
    int id = ramw(0xEC28), here = 1;
    world_t *W = 0;
    if (ps4_world_pick >= 0 && ps4_world_pick != id) {   /* another planet chosen on the map screen */
        for (int i = 0; i < WORLDS; i++) if (worlds[i].w && worlds[i].id == ps4_world_pick) W = &worlds[i];
        if (W) here = 0;
    }
    if (!W) for (int i = 0; i < WORLDS; i++) if (worlds[i].w && worlds[i].id == id) W = &worlds[i];
    if (!W) {
        here = 0;
        for (int i = 0; i < WORLDS; i++) if (worlds[i].w && worlds[i].id == last_world) W = &worlds[i];
        for (int i = 0; i < WORLDS && !W; i++) if (worlds[i].w) W = &worlds[i];
    }
    if (!W || W->w * 4 > maxw || W->h * 4 > maxh) return 0;
    int H = W->h;
    if (W->id <= 1) world_rom_row(W->id, 0, 0);
    if (W->id <= 1 && world_rom_ok[W->id] == 1) {   /* Dezolis: the last pieces of its table repeat one of sea, not shown */
        uint32_t t = WTAB[W->id][1];
        int pieces = W->h / 8, k = pieces - 1;
        while (k > 0 && !memcmp(g_rom + t + (k - 1) * 4, g_rom + t + k * 4, 4)) k--;
        if (pieces - k >= 3) H = k * 8;
    }
    *ow = W->w * 4; *oh = H * 4;
    for (int by = 0; by < H; by++) {
        const uint8_t *ra = world_rom_row(W->id, 0, by), *rb = world_rom_row(W->id, 1, by);
        for (int bx = 0; bx < W->w; bx++) {
            int k = (W->known[by][bx >> 3] >> (bx & 7)) & 1;
            int ba = ra ? ra[bx] : W->blk[0][by][bx], bb = rb ? rb[bx] : W->blk[1][by][bx];
            int okb = (W->col_ok[1][bb >> 3] >> (bb & 7)) & 1, oka = (W->col_ok[0][ba >> 3] >> (ba & 7)) & 1;
            for (int t = 0; t < 16; t++) {
                uint32_t c = 0x10101A;
                if (k || ra) {
                    uint32_t a = oka ? W->col[0][ba][t] : 0;
                    c = (a & 0x80000000u) ? a & 0xFFFFFF : okb ? W->col[1][bb][t] & 0xFFFFFF : 0x10101A;
                    if (!k) c = ((c >> 2) & 0x3F3F3F) + ((c >> 3) & 0x1F1F1F) + 0x0A0A12;   /* not seen yet: dim */
                }
                out[(by * 4 + t / 4) * *ow + bx * 4 + (t & 3)] = c;
            }
        }
    }
    if (here) { *px = ramw(0xC030) / 8; *py = ramw(0xC034) / 8; }
    else if (W->id == last_world && last_wx >= 0) { *px = last_wx / 8; *py = last_wy / 8; }
    else *px = *py = -1;
    if (*py >= H * 4) *px = *py = -1;
    return W->id + 1;
}
/* towns on a planet's map, from the game's table of places (ROM $061260:
 * place number, map, x, y in tiles, 8 bytes each). Place numbers are those
 * of the game's list of names. Motavia: the places before the first on
 * Dezolis (Храм Раджа, 24). */
int ps4_world_towns(int world, int *place, int *tx, int *ty, int max)
{
    static const uint8_t sig[8] = { 0x00, 0x00, 0x00, 0x10, 0x00, 0x5C, 0x01, 0x1E };
    const uint32_t T = 0x061260;
    if (g_rom_size < T + 8 * 40 || memcmp(g_rom + T, sig, 8)) return 0;
    int n = 0, planet = 0;
    for (int i = 0; i < 40 && n < max; i++) {
        const uint8_t *e = g_rom + T + i * 8;
        int pl = e[0], x = e[4] << 8 | e[5], y = e[6] << 8 | e[7];
        if (!x && !y) break;
        if (pl == 24) planet = 1;
        if (pl == 45) planet = 2;   /* Rykros: not a map we keep */
        if (planet != world) continue;
        place[n] = pl; tx[n] = x; ty[n] = y; n++;
    }
    return n;
}
/* the place (the game's place number) whose town map this is, -1 if none:
 * the place table's second word is the town's map */
int ps4_place_of_map(int map)
{
    static const uint8_t sig[8] = { 0x00, 0x00, 0x00, 0x10, 0x00, 0x5C, 0x01, 0x1E };
    const uint32_t T = 0x061260;
    if (g_rom_size < T + 8 * 40 || memcmp(g_rom + T, sig, 8)) return -1;
    for (int i = 0; i < 40; i++) {
        const uint8_t *e = g_rom + T + i * 8;
        if (!(e[4] | e[5] | e[6] | e[7])) break;
        if ((e[2] << 8 | e[3]) == map) return e[0];
    }
    return -1;
}
uint8_t *ps4_world_data(int *size) { *size = sizeof worlds; return (uint8_t *)worlds; }
