#include <stdlib.h>
#include <stdio.h>
/* Sound system: Z80 bus, Z80 timing per video line, YM2612 + PSG mixing,
 * resampling from the YM rate (53,267 Hz) to 48 kHz. */
#include <string.h>
#include "md.h"
#include "z80.h"
#include "sound.h"
#include "resample.h"
#include <math.h>

int g_snd_enabled = 1;
int g_snd_mask;
int g_snd_hold;   /* 1: the sound chips and the Z80 stand still (extra frames of a speed-up) */
static z80_t Z;
static uint32_t z80_bank;              /* 9-bit bank, address bits 15-23 */
static int z80_cyc_debt;               /* Z80 cycles owed (fractional per line) */
static int z80_irq_line;               /* IRQ held for the current line */
static uint32_t ym_frac;               /* YM samples per line accumulator (x 1000) */
static resampler_t RS;                 /* 53,267 -> 48,000 Hz */
static int rs_ready;
static float lp_a, lp_l, lp_r;         /* optional low-pass (tone) at 48 kHz */
void snd_set_tone(int level)
{
    /* 0: clean, 1: softer (8 kHz), 2: muffled like an old TV set (3.4 kHz) */
    double fc = level == 1 ? 8000.0 : level == 2 ? 3400.0 : 0;
    lp_a = fc > 0 ? (float)(1.0 - exp(-2.0 * 3.14159265358979 * fc / 48000.0)) : 0;
}
static int32_t hp_xl, hp_yl, hp_xr, hp_yr, hp_l, hp_r;

#define OUTBUF 16384
static int16_t outbuf[OUTBUF * 2];
static int out_w, out_r;

/* ---- Z80 memory map ---- */
uint8_t z80_read(uint16_t a)
{
    if (a < 0x4000) return g_zram[a & 0x1FFF];
    if (a < 0x6000) return ym_status();
    if (a >= 0x8000) return (uint8_t)md_read8((z80_bank << 15) | (a & 0x7FFF));
    return 0xFF;
}
unsigned long g_ym_w68, g_ym_wz80;   /* debug counters */
void z80_write(uint16_t a, uint8_t v)
{
    if (a < 0x4000) { g_zram[a & 0x1FFF] = v; return; }
    if (a < 0x6000) { g_ym_wz80++; ym_write(a & 3, v); return; }
    if (a < 0x6100) { z80_bank = ((z80_bank >> 1) | ((v & 1) << 8)) & 0x1FF; return; }
    if (a == 0x7F11 || a == 0x7F13 || a == 0x7F15 || a == 0x7F17) { psg_write(v); return; }
    if (a >= 0x8000) md_write8((z80_bank << 15) | (a & 0x7FFF), v);
}
uint8_t z80_in(uint16_t p) { (void)p; return 0xFF; }
void z80_out(uint16_t p, uint8_t v) { (void)p; (void)v; }

/* the 68000 writes the chips directly too */
static void ym_from_68k(int port, uint8_t v) {
    g_ym_w68++;
    if (getenv("YM_PC")) { extern uint32_t g_ipc; static uint32_t seen[64]; static int ns; int k; for (k = 0; k < ns; k++) if (seen[k] == g_ipc) break; if (k == ns && ns < 64) { seen[ns++] = g_ipc; fprintf(stderr, "ym68k pc %06X\n", g_ipc); } }
    ym_write(port, v); }
static void psg_from_68k(uint8_t v) { psg_write(v); }
extern void (*md_ym_write)(int port, uint8_t v);
extern void (*md_psg_write)(uint8_t v);

void snd_reset(void)
{
    z80_reset(&Z);
    ym_reset();
    psg_reset();
    z80_bank = 0; z80_cyc_debt = 0; ym_frac = 0;
    rs_init(&RS, 53693175.0 / (7 * 144), 48000.0); rs_ready = 1;
    md_ym_write = ym_from_68k;
    md_psg_write = psg_from_68k;
}

static int z80_was_reset = 1;

void snd_run_line(void)
{
    if (g_snd_hold) return;
    /* Z80: 3420 master clocks per line / 15 = 228 cycles */
    if (!g_z80_reset) { z80_was_reset = 1; }
    else {
        if (z80_was_reset) { z80_reset(&Z); z80_was_reset = 0; }
        if (!g_z80_busreq) {
            z80_cyc_debt += 228;
            if (g_line == MD_VIS_LINES) z80_irq_line = 1;
            while (z80_cyc_debt > 0) {
                if (z80_irq_line) { int t = z80_interrupt(&Z); if (t) { z80_irq_line = 0; z80_cyc_debt -= t; continue; } }
                z80_cyc_debt -= z80_step(&Z);
            }
            z80_irq_line = 0;
        } else z80_cyc_debt = 0;
    }
    /* YM samples, 3420 / (7 * 144) = 3.3929 per line; always clocked: the timers pace the Z80 driver */
    ym_frac += 33929;
    while (ym_frac >= 10000) {
        ym_frac -= 10000;
        int16_t l, r;
        ym_clock_sample(&l, &r);
        int ps = psg_sample();
        if (g_snd_mask & 4) ps = 0;
        if (!g_snd_enabled) continue;
        /* DC blocker (the console's output capacitor), ~10 Hz high-pass */
        hp_l = l + ps - hp_xl + (int32_t)((int64_t)hp_yl * 65427 / 65536); hp_xl = l + ps;  hp_yl = hp_l;
        hp_r = r + ps - hp_xr + (int32_t)((int64_t)hp_yr * 65427 / 65536); hp_xr = r + ps;  hp_yr = hp_r;
        int L = hp_l, Rr = hp_r;
        if (L > 32767) L = 32767; else if (L < -32768) L = -32768;
        if (Rr > 32767) Rr = 32767; else if (Rr < -32768) Rr = -32768;
        /* band-limited resampling to 48 kHz (src/resample.c) */
        if (!rs_ready) { rs_init(&RS, 53693175.0 / (7 * 144), 48000.0); rs_ready = 1; }
        float o[4];
        int m = rs_push(&RS, (float)L, (float)Rr, o);
        for (int k = 0; k < m; k++) {
            float ol = o[k * 2], orr = o[k * 2 + 1];
            if (lp_a > 0) { lp_l += lp_a * (ol - lp_l); lp_r += lp_a * (orr - lp_r); ol = lp_l; orr = lp_r; }
            int il = (int)lrintf(ol), ir = (int)lrintf(orr);
            if (il > 32767) il = 32767; else if (il < -32768) il = -32768;
            if (ir > 32767) ir = 32767; else if (ir < -32768) ir = -32768;
            int nw = (out_w + 1) % OUTBUF;
            if (nw != out_r) { outbuf[out_w * 2] = (int16_t)il; outbuf[out_w * 2 + 1] = (int16_t)ir; out_w = nw; }
        }
    }
}

int snd_take(int16_t *out, int max_frames)
{
    int n = 0;
    while (n < max_frames && out_r != out_w) {
        out[n * 2] = outbuf[out_r * 2]; out[n * 2 + 1] = outbuf[out_r * 2 + 1];
        out_r = (out_r + 1) % OUTBUF; n++;
    }
    return n;
}

size_t snd_state_size(void) { return sizeof Z + sizeof z80_bank + sizeof z80_cyc_debt + sizeof z80_was_reset + ym_state_size() + psg_state_size(); }
void snd_state_save(uint8_t *o)
{
    memcpy(o, &Z, sizeof Z); o += sizeof Z;
    memcpy(o, &z80_bank, sizeof z80_bank); o += sizeof z80_bank;
    memcpy(o, &z80_cyc_debt, sizeof z80_cyc_debt); o += sizeof z80_cyc_debt;
    memcpy(o, &z80_was_reset, sizeof z80_was_reset); o += sizeof z80_was_reset;
    ym_state_save(o); o += ym_state_size();
    psg_state_save(o);
}
void snd_state_load(const uint8_t *i)
{
    memcpy(&Z, i, sizeof Z); i += sizeof Z;
    memcpy(&z80_bank, i, sizeof z80_bank); i += sizeof z80_bank;
    memcpy(&z80_cyc_debt, i, sizeof z80_cyc_debt); i += sizeof z80_cyc_debt;
    memcpy(&z80_was_reset, i, sizeof z80_was_reset); i += sizeof z80_was_reset;
    ym_state_load(i); i += ym_state_size();
    psg_state_load(i);
}
