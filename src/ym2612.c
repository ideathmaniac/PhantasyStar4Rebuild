/*
 * YM2612 (OPN2) FM synthesiser. Generates one sample per 144 FM clocks
 * (53,267 Hz on an NTSC console). Implemented from the chip's documented
 * behaviour: log-sine/exp tables, 10-bit attenuation envelopes clocked every
 * third sample, detune/multiple phase increments, the eight algorithms,
 * operator-1 feedback, LFO AM/PM, channel 3 special mode, timers and the DAC.
 */
#include <math.h>
#include <string.h>
#include "sound.h"

/* ---- tables ---- */
static uint16_t logsin[256];      /* quarter wave, attenuation in 1/256 dB-ish units (4.8 fixed) */
static uint16_t exptab[256];
static int tables_ready;

static const uint8_t dt_tab[4][32] = {
    { 0 },
    { 0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7 },
    { 1,1,1,1,2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,16,16,16 },
    { 2,2,2,2,2,3,3,3,4,4,4,5,5,6,6,7,8,8,9,10,11,12,13,14,16,17,19,20,22,22,22,22 },
};
/* envelope increment patterns, indexed by rate (0..63) and cycle position */
static const uint8_t eg_inc[19 * 8] = {
    0,1,0,1,0,1,0,1,  0,1,0,1,1,1,0,1,  0,1,1,1,0,1,1,1,  0,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,  1,1,1,2,1,1,1,2,  1,2,1,2,1,2,1,2,  1,2,2,2,1,2,2,2,
    2,2,2,2,2,2,2,2,  2,2,2,4,2,2,2,4,  2,4,2,4,2,4,2,4,  2,4,4,4,2,4,4,4,
    4,4,4,4,4,4,4,4,  4,4,4,8,4,4,4,8,  4,8,4,8,4,8,4,8,  4,8,8,8,4,8,8,8,
    8,8,8,8,8,8,8,8,  16,16,16,16,16,16,16,16,  0,0,0,0,0,0,0,0,
};
#define O(a) ((a) * 8)
static const uint8_t eg_rate_select[64 + 32] = {
    O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),
    O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),O(18),
    O(18),O(18),O(2),O(3),O(0),O(1),O(2),O(3),O(0),O(1),O(2),O(3),O(0),O(1),O(2),O(3),
    O(0),O(1),O(2),O(3),O(0),O(1),O(2),O(3),O(0),O(1),O(2),O(3),O(0),O(1),O(2),O(3),
    O(0),O(1),O(2),O(3),O(0),O(1),O(2),O(3),O(0),O(1),O(2),O(3),O(0),O(1),O(2),O(3),
    O(4),O(5),O(6),O(7),O(8),O(9),O(10),O(11),O(12),O(13),O(14),O(15),O(16),O(16),O(16),O(16),
};
#undef O
/* rates are used as index+32 so 0 maps to "never" */
static const uint8_t eg_rate_shift[64 + 32] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    11,11,11,11,10,10,10,10,9,9,9,9,8,8,8,8,7,7,7,7,6,6,6,6,5,5,5,5,4,4,4,4,
    3,3,3,3,2,2,2,2,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const uint16_t lfo_period[8] = { 108, 77, 71, 67, 62, 44, 8, 5 };
static const uint8_t lfo_ams_shift[4] = { 8, 3, 1, 0 };
/* vibrato depth in 1/1000 of the frequency for each FMS setting at full LFO swing */
static const float lfo_pm_depth[8] = { 0.f, 0.0034f, 0.0067f, 0.01f, 0.014f, 0.02f, 0.04f, 0.08f };

static void init_tables(void)
{
    for (int i = 0; i < 256; i++) {
        double s = sin((i * 2 + 1) * M_PI / 1024.0);
        double att = -log(s) / log(2.0) * 256.0;
        logsin[i] = (uint16_t)(att + 0.5);
        exptab[i] = (uint16_t)(pow(2.0, (255 - i) / 256.0) * 1024.0 + 0.5) - 1024;
    }
    tables_ready = 1;
}

enum { EG_ATT, EG_DEC, EG_SUS, EG_REL };

typedef struct {
    uint8_t dt, mul, tl, rs, ar, am, d1r, d2r, d1l, rr, ssg;
    uint32_t phase, inc;
    int env, state, key;
    int kc;
} op_t;

typedef struct {
    op_t op[4];                       /* in register order: S1, S3, S2, S4 */
    uint16_t fnum; uint8_t block, fhi_latch;
    uint8_t alg, fb, l, r, ams, fms;
    int32_t fbout[2];
    int16_t out;
} ch_t;

static struct {
    ch_t ch[6];
    uint8_t addr[2];
    uint8_t reg[2][256];
    uint16_t ch3_fnum[3]; uint8_t ch3_block[3]; uint8_t ch3_latch;
    uint8_t mode27;
    uint8_t dac_en; uint8_t dac;
    uint8_t lfo_en, lfo_freq; int lfo_cnt, lfo_step;
    uint32_t eg_cnt; int eg_sub;
    int ta, tb, ta_cnt, tb_cnt, status;
} Y;

static void op_update_inc(ch_t *c, int chn, int slot)
{
    op_t *o = &c->op[slot];
    uint16_t fn = c->fnum; uint8_t blk = c->block;
    if (chn == 2 && (Y.mode27 & 0xC0) && slot != 3) {
        /* special mode: S1 <- $A9, S2 <- $AA, S3 <- $A8 (slot index: 0=S1,1=S3,2=S2) */
        int idx = slot == 0 ? 1 : slot == 2 ? 2 : 0;
        fn = Y.ch3_fnum[idx]; blk = Y.ch3_block[idx];
    }
    int f11 = (fn >> 10) & 1, f10 = (fn >> 9) & 1, f9 = (fn >> 8) & 1, f8 = (fn >> 7) & 1;
    int kc = (blk << 2) | (f11 << 1) | (f11 ? (f10 | f9 | f8) : (f10 & f9 & f8));
    o->kc = kc;
    uint32_t base = ((uint32_t)fn << blk) >> 1;
    int d = dt_tab[o->dt & 3][kc];
    if (o->dt & 4) base -= d; else base += d;
    base &= 0x1FFFF;
    o->inc = o->mul ? base * o->mul : base >> 1;
}

static void ch_update(int chn)
{
    for (int s = 0; s < 4; s++) op_update_inc(&Y.ch[chn], chn, s);
}

static void key(int chn, int slot, int on)
{
    op_t *o = &Y.ch[chn].op[slot];
    if (on && !o->key) {
        o->phase = 0;
        o->state = EG_ATT;
        if ((o->ar ? 2 * o->ar + (o->kc >> (3 - o->rs)) : 0) >= 62) o->env = 0;
    } else if (!on && o->key) {
        o->state = EG_REL;
    }
    o->key = on;
}

void ym_reset(void)
{
    if (!tables_ready) init_tables();
    memset(&Y, 0, sizeof Y);
    for (int c = 0; c < 6; c++) {
        Y.ch[c].l = Y.ch[c].r = 1;
        for (int s = 0; s < 4; s++) { Y.ch[c].op[s].env = 0x3FF; Y.ch[c].op[s].state = EG_REL; }
    }
}

static void write_reg(int port, uint8_t r, uint8_t v)
{
    Y.reg[port][r] = v;
    if (port == 0 && r < 0x30) {
        switch (r) {
        case 0x22: Y.lfo_en = (v >> 3) & 1; Y.lfo_freq = v & 7; if (!Y.lfo_en) { Y.lfo_step = 0; Y.lfo_cnt = 0; } break;
        case 0x24: Y.ta = (Y.ta & 3) | (v << 2); break;
        case 0x25: Y.ta = (Y.ta & 0x3FC) | (v & 3); break;
        case 0x26: Y.tb = v; break;
        case 0x27:
            if ((v & 1) && !(Y.mode27 & 1)) Y.ta_cnt = 1024 - Y.ta;
            if ((v & 2) && !(Y.mode27 & 2)) Y.tb_cnt = (256 - Y.tb) * 16;
            if (v & 0x10) Y.status &= ~1;
            if (v & 0x20) Y.status &= ~2;
            if ((v ^ Y.mode27) & 0xC0) { Y.mode27 = v; ch_update(2); }
            Y.mode27 = v;
            break;
        case 0x28: {
            int c = v & 3;
            if (c == 3) break;
            if (v & 4) c += 3;
            key(c, 0, (v >> 4) & 1); key(c, 2, (v >> 5) & 1); key(c, 1, (v >> 6) & 1); key(c, 3, (v >> 7) & 1);
            break;
        }
        case 0x2A: Y.dac = v; break;
        case 0x2B: Y.dac_en = v >> 7; break;
        }
        return;
    }
    int c = r & 3;
    if (c == 3) return;
    int chn = c + (port ? 3 : 0);
    ch_t *ch = &Y.ch[chn];
    if (r >= 0x30 && r < 0xA0) {
        int slot = (r >> 2) & 3;            /* register order S1 S3 S2 S4 */
        op_t *o = &ch->op[slot];
        switch (r & 0xF0) {
        case 0x30: o->dt = (v >> 4) & 7; o->mul = v & 15; op_update_inc(ch, chn, slot); break;
        case 0x40: o->tl = v & 0x7F; break;
        case 0x50: o->rs = v >> 6; o->ar = v & 31; break;
        case 0x60: o->am = v >> 7; o->d1r = v & 31; break;
        case 0x70: o->d2r = v & 31; break;
        case 0x80: o->d1l = v >> 4; o->rr = v & 15; break;
        case 0x90: o->ssg = v & 15; break;
        }
        return;
    }
    switch (r & 0xFC) {
    case 0xA0: ch->fnum = (uint16_t)(((ch->fhi_latch & 7) << 8) | v); ch->block = (ch->fhi_latch >> 3) & 7; ch_update(chn); break;
    case 0xA4: ch->fhi_latch = v & 0x3F; break;
    case 0xA8:
        if (port == 0) { Y.ch3_fnum[c] = (uint16_t)(((Y.ch3_latch & 7) << 8) | v); Y.ch3_block[c] = (Y.ch3_latch >> 3) & 7; ch_update(2); }
        break;
    case 0xAC: if (port == 0) Y.ch3_latch = v & 0x3F; break;
    case 0xB0: ch->fb = (v >> 3) & 7; ch->alg = v & 7; break;
    case 0xB4: ch->l = v >> 7; ch->r = (v >> 6) & 1; ch->ams = (v >> 4) & 3; ch->fms = v & 7; break;
    }
}

void ym_write(int a, uint8_t v)
{
    int port = (a >> 1) & 1;
    if (!(a & 1)) Y.addr[port] = v;
    else write_reg(port, Y.addr[port], v);
}

uint8_t ym_status(void) { return (uint8_t)Y.status; }

/* operator output: phase index in 1024ths of a cycle, attenuation in 1/32 dB-ish units (10 bit env << 2) */
static inline int op_out(uint32_t phase10, int att)
{
    int p = phase10 & 1023;
    int q = p & 255;
    if (p & 256) q = 255 - q;
    int l = logsin[q] + (att << 2);               /* attenuation in 1/256 octave units */
    int out = l >= 0x1A00 ? 0 : ((exptab[l & 255] + 1024) << 2) >> (l >> 8);
    return (p & 512) ? -out : out;                 /* ~13-bit signed */
}

static void eg_step(op_t *o, int ch_kc_unused)
{
    (void)ch_kc_unused;
    int ks = o->kc >> (3 - o->rs);
    int rate;
    switch (o->state) {
    case EG_ATT: rate = o->ar ? 2 * o->ar + ks : 0; break;
    case EG_DEC: rate = o->d1r ? 2 * o->d1r + ks : 0; break;
    case EG_SUS: rate = o->d2r ? 2 * o->d2r + ks : 0; break;
    default: rate = 2 * (o->rr * 2 + 1) + ks; break;
    }
    if (rate > 63) rate = 63;
    int shift = eg_rate_shift[rate + 32];
    if (Y.eg_cnt & ((1u << shift) - 1)) return;
    int inc = eg_inc[eg_rate_select[rate + 32] + ((Y.eg_cnt >> shift) & 7)];
    if (o->state == EG_ATT) {
        if (rate >= 62) o->env = 0;
        else o->env += (~o->env * inc) >> 4;
        if (o->env <= 0) { o->env = 0; o->state = EG_DEC; }
        return;
    }
    o->env += inc;
    if (o->env > 0x3FF) o->env = 0x3FF;
    int sl = o->d1l == 15 ? 0x3E0 : o->d1l << 5;
    if (o->state == EG_DEC && o->env >= sl) o->state = EG_SUS;
}

void ym_clock_sample(int16_t *left, int16_t *right)
{
    /* timers (one tick per sample for A, B counts in 16-sample units) */
    if (Y.mode27 & 1) { if (--Y.ta_cnt <= 0) { Y.ta_cnt = 1024 - Y.ta; if (Y.mode27 & 4) Y.status |= 1; } }
    if (Y.mode27 & 2) { if (--Y.tb_cnt <= 0) { Y.tb_cnt = (256 - Y.tb) * 16; if (Y.mode27 & 8) Y.status |= 2; } }

    /* LFO */
    int lfo_am = 0; float lfo_pm = 0;
    if (Y.lfo_en) {
        if (++Y.lfo_cnt >= lfo_period[Y.lfo_freq]) { Y.lfo_cnt = 0; Y.lfo_step = (Y.lfo_step + 1) & 127; }
        lfo_am = Y.lfo_step < 64 ? (63 - Y.lfo_step) * 2 : (Y.lfo_step - 64) * 2;
        float t = Y.lfo_step / 128.0f;
        lfo_pm = t < 0.25f ? t * 4 : t < 0.75f ? 2 - t * 4 : t * 4 - 4;
    }

    /* envelopes advance every third sample */
    if (++Y.eg_sub >= 3) {
        Y.eg_sub = 0; Y.eg_cnt++;
        for (int c = 0; c < 6; c++) for (int s = 0; s < 4; s++) eg_step(&Y.ch[c].op[s], 0);
    }

    int L = 0, Rr = 0;
    for (int c = 0; c < 6; c++) {
        ch_t *ch = &Y.ch[c];
        int out;
        if (c == 5 && Y.dac_en) out = (g_snd_mask & 2) ? 0 : ((int)Y.dac - 128) << 6;
        else {
            int att[4]; uint32_t ph[4];
            for (int s = 0; s < 4; s++) {
                op_t *o = &ch->op[s];
                uint32_t inc = o->inc;
                if (ch->fms && Y.lfo_en) inc = (uint32_t)(inc * (1.0f + lfo_pm * lfo_pm_depth[ch->fms]));
                o->phase = (o->phase + inc) & 0xFFFFF;
                ph[s] = o->phase >> 10;
                int a = o->env + (o->tl << 3) + ((o->am && ch->ams) ? (lfo_am >> lfo_ams_shift[ch->ams]) : 0);
                att[s] = a > 0x3FF ? 0x3FF : a;
            }
            /* slots: 0=S1 1=S3 2=S2 3=S4 */
            int fbm = ch->fb ? (ch->fbout[0] + ch->fbout[1]) >> (10 - ch->fb) : 0;
            int s1 = op_out(ph[0] + fbm, att[0]);
            ch->fbout[1] = ch->fbout[0]; ch->fbout[0] = s1;
            int s2, s3, s4;
#define M(x) ((x) >> 1)
            switch (ch->alg) {
            case 0: s2 = op_out(ph[2] + M(s1), att[2]); s3 = op_out(ph[1] + M(s2), att[1]); s4 = op_out(ph[3] + M(s3), att[3]); out = s4; break;
            case 1: s2 = op_out(ph[2], att[2]); s3 = op_out(ph[1] + M(s1 + s2), att[1]); s4 = op_out(ph[3] + M(s3), att[3]); out = s4; break;
            case 2: s2 = op_out(ph[2], att[2]); s3 = op_out(ph[1] + M(s2), att[1]); s4 = op_out(ph[3] + M(s1 + s3), att[3]); out = s4; break;
            case 3: s2 = op_out(ph[2] + M(s1), att[2]); s3 = op_out(ph[1], att[1]); s4 = op_out(ph[3] + M(s2 + s3), att[3]); out = s4; break;
            case 4: s2 = op_out(ph[2] + M(s1), att[2]); s3 = op_out(ph[1], att[1]); s4 = op_out(ph[3] + M(s3), att[3]); out = s2 + s4; break;
            case 5: s2 = op_out(ph[2] + M(s1), att[2]); s3 = op_out(ph[1] + M(s1), att[1]); s4 = op_out(ph[3] + M(s1), att[3]); out = s2 + s3 + s4; break;
            case 6: s2 = op_out(ph[2] + M(s1), att[2]); s3 = op_out(ph[1], att[1]); s4 = op_out(ph[3], att[3]); out = s2 + s3 + s4; break;
            default: s2 = op_out(ph[2], att[2]); s3 = op_out(ph[1], att[1]); s4 = op_out(ph[3], att[3]); out = s1 + s2 + s3 + s4; break;
            }
#undef M
            if (out > 8191) out = 8191; else if (out < -8192) out = -8192;
        }
        if ((g_snd_mask & 1) && !(c == 5 && Y.dac_en)) out = 0;
        if (ch->l) L += out;
        if (ch->r) Rr += out;
    }
    if (L > 32767) L = 32767; else if (L < -32768) L = -32768;
    if (Rr > 32767) Rr = 32767; else if (Rr < -32768) Rr = -32768;
    *left = (int16_t)L; *right = (int16_t)Rr;
}

size_t ym_state_size(void) { return sizeof Y; }
void ym_state_save(uint8_t *out) { memcpy(out, &Y, sizeof Y); }
void ym_state_load(const uint8_t *in) { memcpy(&Y, in, sizeof Y); }

#include <stdio.h>
void ym_debug_dump(void)
{
    for (int c = 0; c < 6; c++) {
        ch_t *ch = &Y.ch[c];
        fprintf(stderr, "ch%d alg%d fb%d fnum%03X blk%d LR%d%d |", c, ch->alg, ch->fb, ch->fnum, ch->block, ch->l, ch->r);
        for (int s = 0; s < 4; s++) { op_t *o = &ch->op[s];
            fprintf(stderr, " [mul%d dt%d tl%02X ar%d d1r%d d2r%d sl%d rr%d env%03X st%d k%d inc%05X ssg%X]", o->mul, o->dt, o->tl, o->ar, o->d1r, o->d2r, o->d1l, o->rr, o->env, o->state, o->key, o->inc, o->ssg); }
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "mode27 %02X lfo %d/%d dac_en %d\n", Y.mode27, Y.lfo_en, Y.lfo_freq, Y.dac_en);
}
