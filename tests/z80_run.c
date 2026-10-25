/* Runs src/z80.c against converted SingleStepTests Z80 vectors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/z80.h"

static uint8_t mem[65536];
static uint8_t in_vals[8]; static int in_n, in_pos;
uint8_t z80_read(uint16_t a) { return mem[a]; }
void z80_write(uint16_t a, uint8_t v) { mem[a] = v; }
uint8_t z80_in(uint16_t p) { (void)p; return in_pos < in_n ? in_vals[in_pos++] : 0xFF; }
void z80_out(uint16_t p, uint8_t v) { (void)p; (void)v; }

typedef struct { uint16_t r[26]; uint16_t n; uint8_t ram[64][4]; } st_t;
static int rs(FILE *f, st_t *s)
{
    if (fread(s->r, 2, 26, f) != 26 || fread(&s->n, 2, 1, f) != 1) return 0;
    if (s->n > 64) { fseek(f, 4L * s->n, SEEK_CUR); s->n = 0; return 1; }
    return fread(s->ram, 4, s->n, f) == s->n;
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    int maxp = argc > 2 ? atoi(argv[2]) : 10;
    int strict_xy = argc > 3;
    char name[32];
    long total = 0, fail = 0;
    static long fails_by_op[512];
    while (fread(name, 1, 32, f) == 32) {
        st_t a, b; uint8_t np;
        if (!rs(f, &a) || !rs(f, &b) || fread(&np, 1, 1, f) != 1) break;
        in_n = in_pos = 0;
        for (int i = 0; i < np; i++) { uint8_t pp[4]; fread(pp, 1, 4, f); if (pp[3] && in_n < 8) in_vals[in_n++] = pp[2]; }
        total++;
        for (int i = 0; i < a.n; i++) mem[a.ram[i][0] | (a.ram[i][1] << 8)] = a.ram[i][2];
        z80_t z; z80_reset(&z);
        uint16_t *r = a.r;
        z.pc = r[0]; z.sp = r[1]; z.a = r[2]; z.b = r[3]; z.c = r[4]; z.d = r[5]; z.e = r[6]; z.f = r[7];
        z.h = r[8]; z.l = r[9]; z.i = r[10]; z.r = r[11]; z.ei_pending = r[12]; z.wz = r[13]; z.ix = r[14]; z.iy = r[15];
        z.a2 = r[16] >> 8; z.f2 = r[16]; z.b2 = r[17] >> 8; z.c2 = r[17]; z.d2 = r[18] >> 8; z.e2 = r[18]; z.h2 = r[19] >> 8; z.l2 = r[19];
        z.im = r[20]; z.last_q = 0; z.q = r[22]; z.iff1 = r[23]; z.iff2 = r[24];
        /* q from the test: the flags written by the previous instruction */
        uint8_t q0 = (uint8_t)r[22];
        z80_step(&z);
        (void)q0;
        uint16_t g[26] = { z.pc, z.sp, z.a, z.b, z.c, z.d, z.e, z.f, z.h, z.l, z.i, z.r, 0, z.wz, z.ix, z.iy,
            (uint16_t)((z.a2 << 8) | z.f2), (uint16_t)((z.b2 << 8) | z.c2), (uint16_t)((z.d2 << 8) | z.e2), (uint16_t)((z.h2 << 8) | z.l2),
            z.im, 0, 0, z.iff1, z.iff2, 0 };
        static const char *nm[26] = { "pc","sp","a","b","c","d","e","f","h","l","i","r","ei","wz","ix","iy","af'","bc'","de'","hl'","im","p","q","iff1","iff2","" };
        int bad = 0; char why[400] = "";
        for (int i = 0; i < 25; i++) {
            if (i == 12 || i == 21 || i == 22) continue;
            if (i == 13 && !strict_xy) continue;               /* WZ is internal; checked only in strict mode */
            uint16_t want = b.r[i], got = g[i];
            if (i == 7 && !strict_xy) { want &= 0xD7; got &= 0xD7; }
            if (want != got) { bad = 1; size_t l = strlen(why); snprintf(why + l, sizeof why - l, " %s=%X(want %X)", nm[i], got, want); }
        }
        for (int i = 0; i < b.n; i++) {
            uint16_t ad = (uint16_t)(b.ram[i][0] | (b.ram[i][1] << 8));
            if (mem[ad] != b.ram[i][2]) { bad = 1; size_t l = strlen(why); snprintf(why + l, sizeof why - l, " [%04X]=%02X(want %02X)", ad, mem[ad], b.ram[i][2]); break; }
        }
        if (bad) { fail++; if (fail <= maxp) printf("FAIL %s:%s\n", name, why); }
    }
    printf("%s: %ld/%ld passed\n", argv[1], total - fail, total);
    return 0;
}
