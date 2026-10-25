/* SN76489 PSG (Sega variant: 16-bit noise LFSR, taps at bits 0 and 3). */
#include <string.h>
#include "sound.h"

static struct {
    uint16_t tone[3]; uint8_t vol[4]; uint8_t noise; uint8_t latch;
    int cnt[3]; uint8_t out[3];
    int ncnt; uint16_t lfsr; uint8_t nout;
    uint32_t frac;
} P;

static const int16_t voltab[16] = { 2048, 1627, 1292, 1026, 815, 647, 514, 408, 324, 257, 204, 162, 129, 102, 81, 0 };

void psg_reset(void)
{
    memset(&P, 0, sizeof P);
    for (int i = 0; i < 4; i++) P.vol[i] = 15;
    P.lfsr = 0x8000;
}

void psg_write(uint8_t v)
{
    if (v & 0x80) {
        P.latch = (v >> 4) & 7;
        int ch = P.latch >> 1;
        if (P.latch & 1) P.vol[ch] = v & 15;
        else if (ch < 3) P.tone[ch] = (uint16_t)((P.tone[ch] & 0x3F0) | (v & 15));
        else { P.noise = v & 7; P.lfsr = 0x8000; }
    } else {
        int ch = P.latch >> 1;
        if (P.latch & 1) P.vol[ch] = v & 15;
        else if (ch < 3) P.tone[ch] = (uint16_t)((P.tone[ch] & 15) | ((v & 0x3F) << 4));
        else { P.noise = v & 7; P.lfsr = 0x8000; }
    }
}

/* PSG clock = mclk/15/16 = 223,722 Hz; YM sample rate = mclk/7/144 = 53,267 Hz:
 * 4.2 PSG ticks per sample (7*144 / (15*16) = 4.2). */
int16_t psg_sample(void)
{
    P.frac += 42;
    int ticks = (int)(P.frac / 10);
    P.frac %= 10;
    int acc[4] = { 0 };
    for (int t = 0; t < ticks; t++) {
        for (int c = 0; c < 3; c++) {
            if (--P.cnt[c] <= 0) { P.cnt[c] = P.tone[c] ? P.tone[c] : 1; P.out[c] ^= 1; }
            if (P.tone[c] <= 1) P.out[c] = 1;
            acc[c] += P.out[c];
        }
        if (--P.ncnt <= 0) {
            int r = P.noise & 3;
            P.ncnt = r == 3 ? (P.tone[2] ? P.tone[2] : 1) : (0x10 << r);
            P.nout ^= 1;
            if (P.nout) {
                int fb = (P.noise & 4) ? (((P.lfsr >> 0) ^ (P.lfsr >> 3)) & 1) : (P.lfsr & 1);
                P.lfsr = (uint16_t)((P.lfsr >> 1) | (fb << 15));
            }
        }
        acc[3] += P.lfsr & 1;
    }
    if (!ticks) ticks = 1;
    int s = 0;
    for (int c = 0; c < 4; c++) s += acc[c] * voltab[P.vol[c]] / ticks;
    return (int16_t)s;
}

size_t psg_state_size(void) { return sizeof P; }
void psg_state_save(uint8_t *out) { memcpy(out, &P, sizeof P); }
void psg_state_load(const uint8_t *in) { memcpy(&P, in, sizeof P); }
