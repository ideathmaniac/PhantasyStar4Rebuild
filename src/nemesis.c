/* Nemesis decompression, the game's format for compressed tiles (its own
 * routine is at $041708). Written from the format: a header word (bit 15:
 * rows are XORed with the row above; low 15 bits: number of 8x8 tiles), a
 * table of prefix codes, each standing for a run of one colour index, and
 * the bit stream, most significant bit first. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "nemesis.h"

typedef struct { const uint8_t *p, *end; uint32_t buf; int nbits; int bad; } br_t;
static int rbyte(br_t *b) { if (b->p >= b->end) { b->bad = 1; return 0; } return *b->p++; }
static void fill(br_t *b) { while (b->nbits <= 24) { b->buf |= (uint32_t)rbyte(b) << (24 - b->nbits); b->nbits += 8; } }
static unsigned peek(br_t *b, int n) { fill(b); return b->buf >> (32 - n); }
static void skip(br_t *b, int n) { b->buf <<= n; b->nbits -= n; }

size_t nemesis_decode(const uint8_t *src, size_t srclen, uint8_t *out, size_t outmax, size_t *used)
{
    if (srclen < 3) return 0;
    const uint8_t *p = src, *end = src + srclen;
    unsigned hdr = (unsigned)(p[0] << 8 | p[1]); p += 2;
    int xor_mode = (hdr & 0x8000) != 0;
    size_t tiles = hdr & 0x7FFF, total = tiles * 32;
    if (!tiles || total > outmax) return 0;
    /* the code table: for each 8-bit prefix, the code's length, colour and run */
    uint8_t len8[256] = { 0 }, nib8[256], run8[256];
    int nibble = 0;
    if (p >= end) return 0;
    int x = *p++;
    while (x != 0xFF) {
        if (x & 0x80) { nibble = x & 0x0F; if (p >= end) return 0; x = *p++; }
        int run = ((x >> 4) & 7) + 1, clen = x & 0x0F;
        if (p >= end) return 0;
        int code = *p++;
        if (clen < 1 || clen > 8) return 0;
        int shift = 8 - clen, base = (code << shift) & 0xFF;
        for (int k = 0; k < (1 << shift); k++) { len8[base + k] = (uint8_t)clen; nib8[base + k] = (uint8_t)nibble; run8[base + k] = (uint8_t)run; }
        if (p >= end) return 0;
        x = *p++;
    }
    br_t b = { p, end, 0, 0, 0 };
    size_t n = 0; uint32_t row = 0, prev = 0; int rn = 0;
    while (n < total) {
        int nib, run;
        if (peek(&b, 6) == 0x3F) {               /* inline: 3 bits run - 1, 4 bits colour */
            skip(&b, 6); run = (int)peek(&b, 3) + 1; skip(&b, 3); nib = (int)peek(&b, 4); skip(&b, 4);
        } else {
            unsigned k = peek(&b, 8);
            if (!len8[k]) return 0;
            nib = nib8[k]; run = run8[k]; skip(&b, len8[k]);
        }
        if (b.bad) return 0;
        while (run-- > 0 && n < total) {
            row = row << 4 | (uint32_t)nib;
            if (++rn == 8) {
                if (xor_mode) { row ^= prev; prev = row; }
                out[n] = (uint8_t)(row >> 24); out[n + 1] = (uint8_t)(row >> 16); out[n + 2] = (uint8_t)(row >> 8); out[n + 3] = (uint8_t)row;
                n += 4; row = 0; rn = 0;
            }
        }
    }
    if (used) *used = (size_t)(b.p - src);
    return n;
}
