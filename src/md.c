/* Mega Drive memory map, I/O ports and line timing. */
#include <stdio.h>
#include <string.h>
#include "md.h"
#include "sound.h"

uint8_t  g_rom[0x400000];
uint32_t g_rom_size;
uint8_t  g_ram[0x10000];
uint8_t  g_zram[0x2000];
uint8_t  g_sram[0x4000];
int      g_sram_dirty;
static uint8_t sram_on;          /* $A130F1 bit 0 */

uint64_t g_cycles, g_frame, g_line_end;
int      g_line;
uint16_t g_pad[2];
uint8_t  g_z80_busreq, g_z80_reset;
static uint8_t io_data[3], io_ctrl[3];
void (*md_frame_cb)(void);

int md_load_rom(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    memset(g_rom, 0xFF, sizeof g_rom);
    g_rom_size = (uint32_t)fread(g_rom, 1, sizeof g_rom, f);
    fclose(f);
    return g_rom_size > 0x200;
}

void md_reset(void)
{
    memset(g_ram, 0, sizeof g_ram);
    memset(g_zram, 0, sizeof g_zram);
    memset(io_data, 0x7F, sizeof io_data);
    memset(io_ctrl, 0, sizeof io_ctrl);
    sram_on = 0;
    g_z80_busreq = 0; g_z80_reset = 0;
    g_cycles = 0; g_frame = 0; g_line = 0;
    g_line_end = MD_LINE_CYC;
    vdp_reset();
    snd_reset();
}

/* ---- 3-button pad, TH protocol ---- */
static uint8_t pad_read(int port)
{
    uint8_t out = io_data[port] & io_ctrl[port] & 0x7F; /* output bits read back */
    uint8_t th = (io_data[port] & io_ctrl[port] & 0x40) || !(io_ctrl[port] & 0x40) ? 1 : 0;
    uint16_t b = port < 2 ? g_pad[port] : 0;
    uint8_t in;
    if (th)
        in = 0x40 | (~((b & PAD_C ? 0x20 : 0) | (b & PAD_B ? 0x10 : 0) | (b & PAD_RIGHT ? 8 : 0) |
                       (b & PAD_LEFT ? 4 : 0) | (b & PAD_DOWN ? 2 : 0) | (b & PAD_UP ? 1 : 0)) & 0x3F);
    else
        in = (~((b & PAD_START ? 0x20 : 0) | (b & PAD_A ? 0x10 : 0) | 0x0C |
                (b & PAD_DOWN ? 2 : 0) | (b & PAD_UP ? 1 : 0)) & 0x3F);
    return (uint8_t)((in & ~io_ctrl[port] & 0x7F) | out | (io_data[port] & 0x80));
}

static uint32_t io_read8(uint32_t a)
{
    switch (a & 0x1F) {
    case 0x01: return 0x20;                 /* Japan, NTSC, no expansion unit */
    case 0x03: return pad_read(0);
    case 0x05: return pad_read(1);
    case 0x07: return pad_read(2);
    case 0x09: return io_ctrl[0];
    case 0x0B: return io_ctrl[1];
    case 0x0D: return io_ctrl[2];
    default:   return 0;
    }
}

static void io_write8(uint32_t a, uint32_t v)
{
    switch (a & 0x1F) {
    case 0x03: io_data[0] = (uint8_t)v; break;
    case 0x05: io_data[1] = (uint8_t)v; break;
    case 0x07: io_data[2] = (uint8_t)v; break;
    case 0x09: io_ctrl[0] = (uint8_t)v; break;
    case 0x0B: io_ctrl[1] = (uint8_t)v; break;
    case 0x0D: io_ctrl[2] = (uint8_t)v; break;
    }
}

/* ---- Z80 area as seen from the 68000 ---- */
static uint32_t z80area_read8(uint32_t a)
{
    a &= 0xFFFF;
    if (a < 0x4000) return g_zram[a & 0x1FFF];
    if (a < 0x6000) return ym_status();
    return 0xFF;
}
static void z80area_write8(uint32_t a, uint32_t v);
void (*md_ym_write)(int port, uint8_t v);   /* set by the sound module */
static void z80area_write8(uint32_t a, uint32_t v)
{
    a &= 0xFFFF;
    if (a < 0x4000) g_zram[a & 0x1FFF] = (uint8_t)v;
    else if (a < 0x6000) { if (md_ym_write) md_ym_write(a & 3, (uint8_t)v); }
}

/* ---- byte access ---- */
uint32_t md_read8(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a < 0x400000) {
        if (sram_on && a >= 0x200000 && a < 0x208000) return (a & 1) ? g_sram[(a >> 1) & 0x3FFF] : 0xFF;
        return g_rom[a];
    }
    if (a >= 0xE00000) return g_ram[a & 0xFFFF];
    if (a >= 0xC00000 && a < 0xC00020) {
        uint16_t w = md_read16(a & ~1u);
        return (a & 1) ? (w & 0xFF) : (w >> 8);
    }
    if (a >= 0xA00000 && a < 0xA10000) return z80area_read8(a);
    if (a >= 0xA10000 && a < 0xA10020) return io_read8(a);
    if (a == 0xA11100 || a == 0xA11101) return (a & 1) ? 0 : (g_z80_busreq ? 0 : 1);
    return 0;
}

uint32_t md_read16(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a < 0x400000) {
        if (sram_on && a >= 0x200000 && a < 0x208000) return 0xFF00 | g_sram[(a >> 1) & 0x3FFF];
        return (g_rom[a & ~1u] << 8) | g_rom[a | 1];
    }
    if (a >= 0xE00000) { a &= 0xFFFE; return (g_ram[a] << 8) | g_ram[a + 1]; }
    if (a >= 0xC00000 && a < 0xC00020) {
        switch (a & 0x1E) {
        case 0x00: case 0x02: return vdp_data_read();
        case 0x04: case 0x06: return vdp_status_read();
        case 0x08: case 0x0A: case 0x0C: case 0x0E: return vdp_hv_read();
        default: return 0;
        }
    }
    if (a >= 0xA00000 && a < 0xA10000) { uint32_t b = z80area_read8(a); return (b << 8) | b; }
    if (a >= 0xA10000 && a < 0xA10020) { uint32_t b = io_read8(a | 1); return (b << 8) | b; }
    if (a == 0xA11100) return g_z80_busreq ? 0x0000 : 0x0100;
    return 0;
}

uint32_t md_read32(uint32_t a) { return (md_read16(a) << 16) | md_read16(a + 2); }

extern uint32_t g_ipc;
uint32_t g_watch_lo = 1, g_watch_hi = 0;
#define WATCH(a, v, n) do { if ((a) >= g_watch_lo && (a) <= g_watch_hi) fprintf(stderr, "f%llu W%d %06X=%X pc=%06X\n", (unsigned long long)g_frame, n, a, v, g_ipc); } while (0)
void md_write8(uint32_t a, uint32_t v)
{
    a &= 0xFFFFFF; v &= 0xFF;
    WATCH(a, v, 1);
    if (a >= 0xE00000) { g_ram[a & 0xFFFF] = (uint8_t)v; return; }
    if (a < 0x400000) {
        if (sram_on && a >= 0x200000 && a < 0x208000 && (a & 1)) { g_sram[(a >> 1) & 0x3FFF] = (uint8_t)v; g_sram_dirty = 1; }
        return;
    }
    if (a >= 0xC00000 && a < 0xC00020) {
        if ((a & 0x1F) >= 0x10) return;     /* PSG: sound module later */
        md_write16(a & ~1u, v | (v << 8));
        return;
    }
    if (a >= 0xA00000 && a < 0xA10000) { z80area_write8(a, v); return; }
    if (a >= 0xA10000 && a < 0xA10020) { io_write8(a, v); return; }
    if (a == 0xA11100) { g_z80_busreq = v & 1; return; }
    if (a == 0xA11200) { g_z80_reset = v & 1; return; }
    if (a == 0xA130F1) { sram_on = v & 1; return; }
}

extern void (*md_psg_write)(uint8_t v);
void (*md_psg_write)(uint8_t v);

void md_write16(uint32_t a, uint32_t v)
{
    a &= 0xFFFFFF; v &= 0xFFFF;
    WATCH(a, v, 2);
    if (a >= 0xE00000) { a &= 0xFFFE; g_ram[a] = (uint8_t)(v >> 8); g_ram[a + 1] = (uint8_t)v; return; }
    if (a < 0x400000) { md_write8(a | 1, v & 0xFF); return; }
    if (a >= 0xC00000 && a < 0xC00020) {
        switch (a & 0x1E) {
        case 0x00: case 0x02: vdp_data_write((uint16_t)v); return;
        case 0x04: case 0x06: vdp_ctrl_write((uint16_t)v); return;
        case 0x10: case 0x12: case 0x14: case 0x16: if (md_psg_write) md_psg_write((uint8_t)v); return;
        default: return;
        }
    }
    if (a >= 0xA00000 && a < 0xA10000) { z80area_write8(a, v >> 8); return; }
    if (a >= 0xA10000 && a < 0xA10020) { io_write8(a | 1, v & 0xFF); return; }
    if (a == 0xA11100) { g_z80_busreq = (v >> 8) & 1; return; }
    if (a == 0xA11200) { g_z80_reset = (v >> 8) & 1; return; }
    if (a == 0xA130F0) { sram_on = v & 1; return; }
}

void md_write32(uint32_t a, uint32_t v) { md_write16(a, v >> 16); md_write16(a + 2, v & 0xFFFF); }

/* ---- line timing and interrupts ---- */
void md_irq_ack(int level)
{
    if (level == 6) V.vint_pending = 0;
    else if (level == 4) V.hint_pending = 0;
}

int md_end_line(void)
{
    int newframe = 0;
    g_line++;
    if (g_line >= MD_LINES) { g_line = 0; g_frame++; newframe = 1; }
    g_line_end += MD_LINE_CYC;

    if (g_line < MD_VIS_LINES) vdp_render_line(g_line);
    /* H-int counter: counts down on every active line and on line 224;
     * reloaded during vblank. */
    if (g_line <= MD_VIS_LINES) {
        if (--V.hint_counter < 0) { V.hint_counter = V.reg[10]; V.hint_pending = 1; }
    } else V.hint_counter = V.reg[10];
    if (g_line == MD_VIS_LINES) V.vint_pending = 1;
    snd_run_line();
    if (newframe && md_frame_cb) md_frame_cb();
    return newframe;
}

/* ---- save states ---- */
size_t md_state_size(void)
{
    return sizeof g_ram + sizeof g_zram + sizeof g_sram + sizeof V + sizeof io_data + sizeof io_ctrl +
           sizeof sram_on + sizeof g_cycles + sizeof g_frame + sizeof g_line + sizeof g_line_end +
           sizeof g_z80_busreq + sizeof g_z80_reset + snd_state_size();
}
#define SAVE(x) do { memcpy(out, &(x), sizeof(x)); out += sizeof(x); } while (0)
#define LOAD(x) do { memcpy(&(x), in, sizeof(x)); in += sizeof(x); } while (0)
void md_state_save(uint8_t *out)
{
    SAVE(g_ram); SAVE(g_zram); SAVE(g_sram); SAVE(V); SAVE(io_data); SAVE(io_ctrl); SAVE(sram_on);
    SAVE(g_cycles); SAVE(g_frame); SAVE(g_line); SAVE(g_line_end); SAVE(g_z80_busreq); SAVE(g_z80_reset);
    snd_state_save(out);
}
void md_state_load(const uint8_t *in)
{
    LOAD(g_ram); LOAD(g_zram); LOAD(g_sram); LOAD(V); LOAD(io_data); LOAD(io_ctrl); LOAD(sram_on);
    LOAD(g_cycles); LOAD(g_frame); LOAD(g_line); LOAD(g_line_end); LOAD(g_z80_busreq); LOAD(g_z80_reset);
    snd_state_load(in);
}

uint32_t md_crc32(const void *p, size_t n)
{
    static uint32_t t[256];
    if (!t[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; }
    uint32_t c = 0xFFFFFFFFu; const uint8_t *b = p;
    while (n--) c = t[(c ^ *b++) & 0xFF] ^ (c >> 8);
    return ~c;
}
