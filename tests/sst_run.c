/* Runs the interpreter against converted SingleStepTests vectors.
 * Usage: sst_run file.bin [max_fail_print] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/cpu.h"

static uint8_t mem[1 << 24];
vdp_t V;
uint32_t md_read8(uint32_t a) { return mem[a & 0xFFFFFF]; }
uint32_t md_read16(uint32_t a) { a &= 0xFFFFFF; return (mem[a] << 8) | mem[(a + 1) & 0xFFFFFF]; }
uint32_t md_read32(uint32_t a) { return (md_read16(a) << 16) | md_read16(a + 2); }
void md_write8(uint32_t a, uint32_t v) { mem[a & 0xFFFFFF] = (uint8_t)v; }
void md_write16(uint32_t a, uint32_t v) { a &= 0xFFFFFF; mem[a] = (uint8_t)(v >> 8); mem[(a + 1) & 0xFFFFFF] = (uint8_t)v; }
void md_write32(uint32_t a, uint32_t v) { md_write16(a, v >> 16); md_write16(a + 2, v); }
void md_irq_ack(int l) { (void)l; }

typedef struct { uint32_t r[19]; uint16_t pf[2]; uint32_t n; uint32_t (*ram)[2]; } st_t;

static int read_state(FILE *f, st_t *s)
{
    if (fread(s->r, 4, 19, f) != 19) return 0;
    if (fread(s->pf, 2, 2, f) != 2) return 0;
    if (fread(&s->n, 4, 1, f) != 1) return 0;
    s->ram = malloc(8 * (s->n + 1));
    return fread(s->ram, 8, s->n, f) == s->n;
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    int maxp = argc > 2 ? atoi(argv[2]) : 5;
    m68k_init_tables();
    char name[64];
    long total = 0, fail = 0;
    while (fread(name, 1, 64, f) == 64) {
        st_t a, b;
        if (!read_state(f, &a) || !read_state(f, &b)) break;
        total++;
        for (uint32_t i = 0; i < a.n; i++) mem[a.ram[i][0] & 0xFFFFFF] = (uint8_t)a.ram[i][1];
        uint32_t pc = a.r[18];
        md_write16(pc, a.pf[0]); md_write16(pc + 2, a.pf[1]);
        for (int i = 0; i < 8; i++) R.d[i] = a.r[i];
        for (int i = 0; i < 7; i++) R.a[i] = a.r[8 + i];
        R.srh = 0; R.stopped = 0;
        uint16_t sr = (uint16_t)a.r[17];
        R.srh = (uint8_t)((sr >> 8) & 0xA7); cpu_set_ccr(sr);
        if (R.srh & 0x20) { R.a[7] = a.r[16]; R.usp = a.r[15]; } else { R.a[7] = a.r[15]; R.usp = a.r[16]; }
        R.pc = pc;
        m68k_step();
        uint32_t got[19];
        for (int i = 0; i < 8; i++) got[i] = R.d[i];
        for (int i = 0; i < 7; i++) got[8 + i] = R.a[i];
        if (R.srh & 0x20) { got[16] = R.a[7]; got[15] = R.usp; } else { got[15] = R.a[7]; got[16] = R.usp; }
        got[17] = cpu_get_sr(); got[18] = R.pc;
        int bad = 0; char why[512] = "";
        static const char *nm[19] = { "d0","d1","d2","d3","d4","d5","d6","d7","a0","a1","a2","a3","a4","a5","a6","usp","ssp","sr","pc" };
        for (int i = 0; i < 19; i++) {
            uint32_t want = b.r[i], g = got[i];
            if (i == 18) { want &= 0xFFFFFF; g &= 0xFFFFFF; }
            if (i == 17) { want &= 0xA71F; g &= 0xA71F; }
            if (want != g) { bad = 1; size_t l = strlen(why); snprintf(why + l, sizeof why - l, " %s=%X(want %X)", nm[i], g, want); }
        }
        for (uint32_t i = 0; i < b.n; i++) {
            uint32_t ad = b.ram[i][0] & 0xFFFFFF;
            if (mem[ad] != (uint8_t)b.ram[i][1]) { bad = 1; size_t l = strlen(why); snprintf(why + l, sizeof why - l, " [%06X]=%02X(want %02X)", ad, mem[ad], b.ram[i][1]); break; }
        }
        if (bad) { fail++; if (fail <= maxp) printf("FAIL %s:%s\n", name, why); }
        free(a.ram); free(b.ram);
    }
    printf("%s: %ld/%ld passed\n", argv[1], total - fail, total);
    return 0;
}
