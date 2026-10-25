/*
 * Phantasy Star IV widescreen: paints the columns outside the console's
 * 320-pixel view from the game's own field map data.
 *
 * Field maps (towns, dungeons, the world map) are grids of 32x32 blocks:
 *   $FFA000  plane A (upper layer) block map, row stride ($FFEC64)+1
 *   $FFB000  plane B (ground) block map, row stride ($FFEC66)+1
 *   $FF6000  block definitions, 16 tile words (4x4) per block
 * The world map keeps only 32 rows of 128 blocks, as a ring (4 KB).
 * Camera in pixels: $FFEF94 (x), $FFEF90 (y).
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
#include "md.h"

static uint8_t snap_maps[0x2000];       /* $FFA000-$FFBFFF */
static uint16_t snap_blocks[0x1000];    /* $FF6000-$FF7FFF as words */
static int snap_valid, snap_wa, snap_ha, snap_wb, snap_hb, snap_maskb;
static int cam_x, cam_y;                /* world position of screen (0,0) this frame */
static int battle, battle_mirror;       /* this frame is a battle; its background is one screen wide */
int ps4_field_live;                     /* last frame showed the field map unobstructed */
int g_ws_probe;                         /* run the field check even at 4:3 */
int g_ws_battle = 1;                    /* widen battles too */

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
                int wx = cam_x + tx * 8, wy = cam_y + ty * 8;
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

static int ws_begin(void)
{
    battle = 0;
    if (g_ws_ext && g_ws_battle && ((g_ram[0xEF00] << 8) | g_ram[0xEF01]) == 0x14 && (V.reg[12] & 1)) {
        ps4_field_live = 0;
        battle = 1;
        return battle_begin();
    }
    /* world camera, kept consistent with what the VDP is scrolling */
    int rx = ramw(0xEF94), ry = ramw(0xEF90);
    int vx = (-plane_hs(1)) & 511, vy = V.vsram[1] & 255;
    cam_x = rx + ((((vx - rx) % 512) + 512 + 256) % 512 - 256);
    cam_y = ry + ((((vy - ry) % 256) + 256 + 128) % 256 - 128);
    ps4_field_live = 0;
    if ((V.reg[11] & 7) != 0) return 0;          /* field maps use full-screen scrolling */

    int wa = g_ram[0xEC64] + 1, ha = g_ram[0xEC65] + 1, wb = g_ram[0xEC66] + 1, hb = g_ram[0xEC67] + 1;
    int maskb = g_ram[0xEC4F] ? 0x1FFF : 0xBFFF;
    if (wa < 8 || wb < 8) return 0;
    if (g_ws_ext == 0 && !g_ws_probe) return 0;
    static uint16_t live_blocks[0x1000];
    for (int i = 0; i < 0x1000; i++) live_blocks[i] = ramw(0x6000 + i * 2);
    int live = verify(&g_ram[0xA000], live_blocks, wa, ha, wb, hb, maskb);
    ps4_field_live = live >= 98;
    if (live >= 98) {
        memcpy(snap_maps, &g_ram[0xA000], sizeof snap_maps);
        memcpy(snap_blocks, live_blocks, sizeof snap_blocks);
        snap_wa = wa; snap_ha = ha; snap_wb = wb; snap_hb = hb; snap_maskb = maskb;
        snap_valid = 1;
        return 1;
    }
    if (!snap_valid) return 0;
    /* windows/dialogs over the field: keep painting from the snapshot */
    return verify(snap_maps, snap_blocks, snap_wa, snap_ha, snap_wb, snap_hb, snap_maskb) >= 40;
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
    int wx = cam_x + sx, wy = cam_y + line;
    int t = map_tile(snap_maps, snap_blocks, plane, plane ? snap_wb : snap_wa, plane ? snap_hb : snap_ha,
                     plane ? snap_maskb : 0xBFFF, wx, wy);
    if (t < 0) return 0;
    if (g_hd_track) g_ws_src = vdp_make_src((uint16_t)t, wx & 7, wy & 7);
    return vdp_tile_pixel((uint16_t)t, wx & 7, wy & 7);
}

void ps4_ws_install(void)
{
    g_ws_begin = ws_begin;
    g_ws_pixel = ws_pixel;
    snap_valid = 0;
}
