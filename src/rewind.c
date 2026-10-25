/*
 * Rewind: a ring of XOR deltas between consecutive save states, compressed
 * as runs of zero / literal bytes. XOR is its own inverse, so going back one
 * step is cur ^= delta. Only the newest full state is kept.
 */
#include <stdlib.h>
#include <string.h>
#include "rewind.h"
#include "emu.h"

static uint8_t *cur, *tmp, *ring;
static size_t ssize, cap, head, used;      /* ring bytes: [head - used, head) */
static int count;
/* each record: payload, then u32 payload length (so we can walk backwards) */

void rw_init(size_t bytes)
{
    free(cur); free(tmp); free(ring);
    ssize = emu_state_size();
    cur = malloc(ssize); tmp = malloc(ssize);
    cap = bytes; ring = malloc(cap);
    head = used = 0; count = 0;
    emu_state_save(cur);
}

void rw_clear(void) { head = used = 0; count = 0; if (cur) emu_state_save(cur); }
int rw_count(void) { return count; }

static void ring_put(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) { ring[head] = p[i]; head = (head + 1) % cap; }
    used += n;
}
static void ring_get_back(uint8_t *p, size_t n, size_t end)  /* bytes [end - n, end) */
{
    size_t s = (end + cap - n) % cap;
    for (size_t i = 0; i < n; i++) p[i] = ring[(s + i) % cap];
}

/* drop the oldest records until n more bytes fit */
static void make_room(size_t n)
{
    while (used + n > cap && count > 0) {
        /* walk from the newest record back to find the oldest one's size */
        size_t end = head, rem = used, last = 0;
        for (int i = 0; i < count; i++) {
            uint32_t len; ring_get_back((uint8_t *)&len, 4, end);
            last = len + 4;
            end = (end + cap - last) % cap;
            rem -= last;
        }
        used -= last; count--;
        (void)rem;
    }
}

void rw_push(void)
{
    if (!ring) return;
    emu_state_save(tmp);
    /* encode delta = tmp ^ cur as [u16 zero run][u16 literal len][literals]... */
    static uint8_t *enc; static size_t enc_cap;
    if (enc_cap < ssize * 2 + 16) { free(enc); enc_cap = ssize * 2 + 16; enc = malloc(enc_cap); }
    size_t o = 0, i = 0;
    while (i < ssize) {
        size_t z = 0;
        while (i + z < ssize && z < 65535 && (tmp[i + z] ^ cur[i + z]) == 0) z++;
        i += z;
        size_t l = 0;
        while (i + l < ssize && l < 65535 && (tmp[i + l] ^ cur[i + l]) != 0) l++;
        enc[o++] = (uint8_t)z; enc[o++] = (uint8_t)(z >> 8);
        enc[o++] = (uint8_t)l; enc[o++] = (uint8_t)(l >> 8);
        for (size_t k = 0; k < l; k++) enc[o++] = tmp[i + k] ^ cur[i + k];
        i += l;
    }
    if (o + 4 > cap) return;
    make_room(o + 4);
    uint32_t len = (uint32_t)o;
    ring_put(enc, o);
    ring_put((const uint8_t *)&len, 4);
    count++;
    memcpy(cur, tmp, ssize);
}

/* restores the most recently pushed state and forgets it */
int rw_pop(void)
{
    if (!count) return 0;
    emu_state_load(cur, ssize);
    uint32_t len; ring_get_back((uint8_t *)&len, 4, head);
    size_t end = (head + cap - 4) % cap;
    static uint8_t *enc; static size_t enc_cap;
    if (enc_cap < len) { free(enc); enc_cap = len; enc = malloc(enc_cap); }
    ring_get_back(enc, len, end);
    size_t o = 0, i = 0;
    while (o + 4 <= len) {
        size_t z = enc[o] | (enc[o + 1] << 8), l = enc[o + 2] | (enc[o + 3] << 8);
        o += 4; i += z;
        for (size_t k = 0; k < l && i + k < ssize; k++) cur[i + k] ^= enc[o + k];
        o += l; i += l;
    }
    head = (head + cap - (len + 4)) % cap;
    used -= len + 4;
    count--;
    return 1;
}
