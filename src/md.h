/*
 * Mega Drive hardware model: memory map, I/O, VDP, line timing.
 *
 * Time is counted in 68000 cycles. Each instruction costs g_cyc[opcode]
 * (an approximation that depends only on the opcode word), so the
 * interpreter and the recompiled code advance hardware time identically.
 * A frame is 262 lines of 488 cycles (NTSC).
 */
#ifndef PS4_MD_H
#define PS4_MD_H
#include <stdint.h>
#include <stddef.h>

#define MD_LINE_CYC   488
#define MD_LINES      262
#define MD_VIS_LINES  224
#define MD_W_MAX      448      /* 320 + 2 * 64 widescreen */

extern uint8_t  g_rom[0x400000];
extern uint32_t g_rom_size;
extern uint8_t  g_ram[0x10000];
extern uint8_t  g_zram[0x2000];
extern uint8_t  g_sram[0x4000];
extern int      g_sram_dirty;

/* VDP state */
typedef struct {
    uint8_t  vram[0x10000];
    uint16_t cram[64];
    uint16_t vsram[40];
    uint8_t  reg[32];
    uint16_t addr;
    uint8_t  code;
    uint8_t  pending;      /* first control word written */
    uint8_t  fill_pending; /* DMA fill armed, waits for data write */
    uint8_t  vint_pending, hint_pending;
    int      hint_counter;
    uint16_t read_buf;
} vdp_t;
extern vdp_t V;

/* timing */
extern uint64_t g_cycles;        /* 68000 cycles since reset */
extern uint64_t g_frame;         /* completed frames */
extern int      g_line;          /* current line 0..261 */
extern uint64_t g_line_end;      /* g_cycles value at which the line ends */

/* framebuffer: one 32-bit pixel (0x00RRGGBB) per dot, H40 = 320 wide */
extern uint32_t g_fb[MD_VIS_LINES][MD_W_MAX];
extern uint8_t g_layer[MD_VIS_LINES][MD_W_MAX];
/* HD packs: per screen pixel, the tile pixel it shows (0 = backdrop / none) */
#define VSRC_VALID 0x80000000u
#define VSRC_TILE(s) ((s) & 0x7FF)
#define VSRC_PAL(s) (((s) >> 11) & 3)
#define VSRC_HFLIP(s) (((s) >> 13) & 1)
#define VSRC_VFLIP(s) (((s) >> 14) & 1)
#define VSRC_CX(s) (((s) >> 15) & 7)       /* pixel inside the tile pattern, unflipped */
#define VSRC_CY(s) (((s) >> 18) & 7)
#define VSRC_LVL(s) (((s) >> 21) & 3)      /* shadow / highlight */
extern int g_hd_track;
extern uint32_t g_src[MD_VIS_LINES][MD_W_MAX];
extern uint32_t g_ws_src;
uint32_t vdp_make_src(uint16_t ent, int px, int py);
uint32_t vdp_color(int idx);

extern int      g_fb_width;      /* 256 or 320 for the last frame */

/* pads: bit set = pressed */
enum { PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8,
       PAD_B = 16, PAD_C = 32, PAD_A = 64, PAD_START = 128 };
extern uint16_t g_pad[2];

/* Z80 bus (sound CPU added later: for now the 68000 sees RAM and a granted bus) */
extern uint8_t g_z80_busreq, g_z80_reset;

int  md_load_rom(const char *path);
void md_reset(void);

uint32_t md_read8(uint32_t a);
uint32_t md_read16(uint32_t a);
uint32_t md_read32(uint32_t a);
void md_write8(uint32_t a, uint32_t v);
void md_write16(uint32_t a, uint32_t v);
void md_write32(uint32_t a, uint32_t v);

/* VDP (vdp.c) */
void     vdp_reset(void);
void     vdp_ctrl_write(uint16_t v);
void     vdp_data_write(uint16_t v);
uint16_t vdp_data_read(void);
uint16_t vdp_status_read(void);
uint16_t vdp_hv_read(void);
void     vdp_render_line(int line);
uint8_t  vdp_tile_pixel(uint16_t ent, int px, int py);

/* Widescreen. g_ws_ext: extra pixels requested on each side (0 = 4:3, also
 * read by the recompiled sprite-culling patches). Each frame the renderer
 * asks g_ws_begin() whether this frame can be drawn wider (the scrolling
 * field map is showing); if so, pixels outside the 320-wide VDP view come
 * from g_ws_pixel() (painted from the game's map data). */
extern int32_t g_ws_ext;
extern int     g_ws_side_sprites;                    /* 0: keep the widened columns free of sprites (set by g_ws_begin) */
extern int     g_ws_frame_ext;                       /* ext used for the frame being drawn */
extern int   (*g_ws_begin)(void);                    /* 1: draw this frame wide */
extern uint8_t (*g_ws_pixel)(int plane, int sx, int line);

/* Interrupt level the VDP is asserting now (0, 4 or 6), honouring enables. */
static inline int md_irq_level(void)
{
    if (V.vint_pending && (V.reg[1] & 0x20)) return 6;
    if (V.hint_pending && (V.reg[0] & 0x10)) return 4;
    return 0;
}
void md_irq_ack(int level);

/* Advance hardware to the end of the current line (render, counters, irqs).
 * Called by the execution loop whenever g_cycles >= g_line_end. Returns 1
 * when a new frame begins. */
int  md_end_line(void);

/* Host callbacks */
extern void (*md_frame_cb)(void);   /* at the start of each frame (line 0) */

/* Save states */
size_t md_state_size(void);
void   md_state_save(uint8_t *out);
void   md_state_load(const uint8_t *in);
uint32_t md_crc32(const void *p, size_t n);

#endif
