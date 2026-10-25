/* Z80 interpreter, including the undocumented flags (bits 3 and 5),
 * the IX/IY half registers and DDCB/FDCB forms. */
#include "z80.h"

#define FS 0x80
#define FZ 0x40
#define FY 0x20
#define FH 0x10
#define FX 0x08
#define FP 0x04
#define FN 0x02
#define FC 0x01

static uint8_t szp[256], sz[256];
static int tables_ready;

static void init_tables(void)
{
    for (int i = 0; i < 256; i++) {
        int p = 0;
        for (int b = 0; b < 8; b++) p ^= (i >> b) & 1;
        sz[i] = (uint8_t)((i & (FS | FY | FX)) | (i == 0 ? FZ : 0));
        szp[i] = (uint8_t)(sz[i] | (p ? 0 : FP));
    }
    tables_ready = 1;
}

void z80_reset(z80_t *z)
{
    if (!tables_ready) init_tables();
    z->pc = 0; z->i = 0; z->r = 0; z->iff1 = z->iff2 = 0; z->im = 0; z->halted = 0;
    z->a = z->f = 0xFF; z->sp = 0xFFFF; z->ei_pending = 0; z->q = z->last_q = 0;
}

#define BC ((uint16_t)((z->b << 8) | z->c))
#define DE ((uint16_t)((z->d << 8) | z->e))
#define HL ((uint16_t)((z->h << 8) | z->l))
#define AF ((uint16_t)((z->a << 8) | z->f))
#define SETBC(v) do { uint16_t v_ = (v); z->b = v_ >> 8; z->c = (uint8_t)v_; } while (0)
#define SETDE(v) do { uint16_t v_ = (v); z->d = v_ >> 8; z->e = (uint8_t)v_; } while (0)
#define SETHL(v) do { uint16_t v_ = (v); z->h = v_ >> 8; z->l = (uint8_t)v_; } while (0)
#define SETF(v) do { z->f = (uint8_t)(v); z->q = z->f; } while (0)

static inline uint8_t rd(uint16_t a) { return z80_read(a); }
static inline void wr(uint16_t a, uint8_t v) { z80_write(a, v); }
static inline uint8_t fetch(z80_t *z) { return rd(z->pc++); }
static inline uint16_t fetch16(z80_t *z) { uint16_t lo = fetch(z); return (uint16_t)(lo | (fetch(z) << 8)); }
static inline uint16_t rd16(uint16_t a) { return (uint16_t)(rd(a) | (rd((uint16_t)(a + 1)) << 8)); }
static inline void wr16(uint16_t a, uint16_t v) { wr(a, (uint8_t)v); wr((uint16_t)(a + 1), (uint8_t)(v >> 8)); }
static inline void push(z80_t *z, uint16_t v) { z->sp -= 2; wr16(z->sp, v); }
static inline uint16_t pop(z80_t *z) { uint16_t v = rd16(z->sp); z->sp += 2; return v; }
static inline void inc_r(z80_t *z) { z->r = (uint8_t)((z->r & 0x80) | ((z->r + 1) & 0x7F)); }

/* ---- 8-bit ALU ---- */
static void add8(z80_t *z, uint8_t v, int cy)
{
    int r = z->a + v + cy;
    uint8_t f = (uint8_t)(sz[r & 0xFF] | ((r >> 8) & FC) | ((z->a ^ v ^ r) & FH) |
                          (((v ^ z->a ^ 0x80) & (v ^ r) & 0x80) ? FP : 0));
    z->a = (uint8_t)r; SETF(f);
}
static void sub8(z80_t *z, uint8_t v, int cy, int store)
{
    int r = z->a - v - cy;
    uint8_t res = (uint8_t)r;
    uint8_t f = (uint8_t)((res & FS) | (res ? 0 : FZ) | FN | ((r >> 8) & FC) | ((z->a ^ v ^ r) & FH) |
                          (((v ^ z->a) & (z->a ^ r) & 0x80) ? FP : 0));
    if (store) { z->a = res; f |= res & (FY | FX); }
    else f |= v & (FY | FX);                    /* CP takes bits 3/5 from the operand */
    SETF(f);
}
static void and8(z80_t *z, uint8_t v) { z->a &= v; SETF(szp[z->a] | FH); }
static void xor8(z80_t *z, uint8_t v) { z->a ^= v; SETF(szp[z->a]); }
static void or8(z80_t *z, uint8_t v)  { z->a |= v; SETF(szp[z->a]); }
static void alu(z80_t *z, int op, uint8_t v)
{
    switch (op) {
    case 0: add8(z, v, 0); break;
    case 1: add8(z, v, z->f & FC); break;
    case 2: sub8(z, v, 0, 1); break;
    case 3: sub8(z, v, z->f & FC, 1); break;
    case 4: and8(z, v); break;
    case 5: xor8(z, v); break;
    case 6: or8(z, v); break;
    default: sub8(z, v, 0, 0); break;
    }
}
static uint8_t inc8(z80_t *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    SETF((z->f & FC) | sz[r] | ((r & 0x0F) == 0 ? FH : 0) | (v == 0x7F ? FP : 0));
    return r;
}
static uint8_t dec8(z80_t *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    SETF((z->f & FC) | sz[r] | FN | ((v & 0x0F) == 0 ? FH : 0) | (v == 0x80 ? FP : 0));
    return r;
}

/* ---- 16-bit ALU ---- */
static uint16_t add16(z80_t *z, uint16_t a, uint16_t b)
{
    uint32_t r = a + b;
    z->wz = (uint16_t)(a + 1);
    SETF((z->f & (FS | FZ | FP)) | ((r >> 16) & FC) | (((a ^ b ^ r) >> 8) & FH) | ((r >> 8) & (FY | FX)));
    return (uint16_t)r;
}
static void adc16(z80_t *z, uint16_t b)
{
    uint16_t a = HL;
    uint32_t r = a + b + (z->f & FC);
    z->wz = (uint16_t)(a + 1);
    uint16_t res = (uint16_t)r;
    SETF(((res >> 8) & (FS | FY | FX)) | (res ? 0 : FZ) | ((r >> 16) & FC) | (((a ^ b ^ r) >> 8) & FH) |
         (((~(a ^ b)) & (a ^ r) & 0x8000) ? FP : 0));
    SETHL(res);
}
static void sbc16(z80_t *z, uint16_t b)
{
    uint16_t a = HL;
    uint32_t r = a - b - (z->f & FC);
    z->wz = (uint16_t)(a + 1);
    uint16_t res = (uint16_t)r;
    SETF(((res >> 8) & (FS | FY | FX)) | (res ? 0 : FZ) | ((r >> 16) & FC) | (((a ^ b ^ r) >> 8) & FH) | FN |
         (((a ^ b) & (a ^ r) & 0x8000) ? FP : 0));
    SETHL(res);
}

/* ---- CB rotates/shifts ---- */
static uint8_t rot(z80_t *z, int op, uint8_t v)
{
    uint8_t r, c;
    switch (op) {
    case 0: c = v >> 7; r = (uint8_t)((v << 1) | c); break;                 /* RLC */
    case 1: c = v & 1;  r = (uint8_t)((v >> 1) | (c << 7)); break;          /* RRC */
    case 2: c = v >> 7; r = (uint8_t)((v << 1) | (z->f & FC)); break;       /* RL */
    case 3: c = v & 1;  r = (uint8_t)((v >> 1) | ((z->f & FC) << 7)); break;/* RR */
    case 4: c = v >> 7; r = (uint8_t)(v << 1); break;                       /* SLA */
    case 5: c = v & 1;  r = (uint8_t)((v >> 1) | (v & 0x80)); break;        /* SRA */
    case 6: c = v >> 7; r = (uint8_t)((v << 1) | 1); break;                 /* SLL */
    default: c = v & 1; r = (uint8_t)(v >> 1); break;                       /* SRL */
    }
    SETF(szp[r] | c);
    return r;
}

static void daa(z80_t *z)
{
    uint8_t a = z->a, corr = 0, c = z->f & FC;
    if ((z->f & FH) || (a & 0x0F) > 9) corr |= 0x06;
    if (c || a > 0x99) { corr |= 0x60; c = FC; }
    uint8_t r = (z->f & FN) ? (uint8_t)(a - corr) : (uint8_t)(a + corr);
    uint8_t h = (z->f & FN) ? (((z->f & FH) && (a & 0x0F) < 6) ? FH : 0) : (((a & 0x0F) > 9) ? FH : 0);
    z->a = r;
    SETF(szp[r] | c | h | (z->f & FN));
}

/* register access by 3-bit index; idx: 0 = HL, 1 = IX, 2 = IY (for H/L halves) */
static uint8_t getr(z80_t *z, int r, int idx)
{
    switch (r) {
    case 0: return z->b; case 1: return z->c; case 2: return z->d; case 3: return z->e;
    case 4: return idx == 1 ? (uint8_t)(z->ix >> 8) : idx == 2 ? (uint8_t)(z->iy >> 8) : z->h;
    case 5: return idx == 1 ? (uint8_t)z->ix : idx == 2 ? (uint8_t)z->iy : z->l;
    case 7: return z->a;
    }
    return 0;
}
static void setr(z80_t *z, int r, int idx, uint8_t v)
{
    switch (r) {
    case 0: z->b = v; break; case 1: z->c = v; break; case 2: z->d = v; break; case 3: z->e = v; break;
    case 4: if (idx == 1) z->ix = (uint16_t)((z->ix & 0xFF) | (v << 8)); else if (idx == 2) z->iy = (uint16_t)((z->iy & 0xFF) | (v << 8)); else z->h = v; break;
    case 5: if (idx == 1) z->ix = (uint16_t)((z->ix & 0xFF00) | v); else if (idx == 2) z->iy = (uint16_t)((z->iy & 0xFF00) | v); else z->l = v; break;
    case 7: z->a = v; break;
    }
}
static uint16_t getrp(z80_t *z, int p, int idx)
{
    switch (p) {
    case 0: return BC; case 1: return DE;
    case 2: return idx == 1 ? z->ix : idx == 2 ? z->iy : HL;
    default: return z->sp;
    }
}
static void setrp(z80_t *z, int p, int idx, uint16_t v)
{
    switch (p) {
    case 0: SETBC(v); break; case 1: SETDE(v); break;
    case 2: if (idx == 1) z->ix = v; else if (idx == 2) z->iy = v; else SETHL(v); break;
    default: z->sp = v; break;
    }
}
static int cond(z80_t *z, int y)
{
    switch (y) {
    case 0: return !(z->f & FZ); case 1: return (z->f & FZ) != 0;
    case 2: return !(z->f & FC); case 3: return (z->f & FC) != 0;
    case 4: return !(z->f & FP); case 5: return (z->f & FP) != 0;
    case 6: return !(z->f & FS); default: return (z->f & FS) != 0;
    }
}

/* ---- CB prefix ---- */
static int exec_cb(z80_t *z, int idx, uint16_t addr)
{
    /* idx 0: opcode fetched normally; idx 1/2: DDCB/FDCB with displacement already read */
    uint8_t op;
    if (!idx) { op = fetch(z); inc_r(z); }
    else op = fetch(z);
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
    if (idx) {
        uint8_t v = rd(addr), res;
        if (x == 1) {
            uint8_t f = (z->f & FC) | FH | ((v & (1 << y)) ? 0 : (FZ | FP)) | ((y == 7 && (v & 0x80)) ? FS : 0);
            f |= (addr >> 8) & (FY | FX);
            SETF(f);
            return 20;
        }
        if (x == 0) res = rot(z, y, v);
        else if (x == 2) res = (uint8_t)(v & ~(1 << y));
        else res = (uint8_t)(v | (1 << y));
        wr(addr, res);
        if (r != 6) setr(z, r, 0, res);
        return 23;
    }
    if (r == 6) {
        uint16_t a = HL;
        uint8_t v = rd(a);
        if (x == 1) {
            uint8_t f = (z->f & FC) | FH | ((v & (1 << y)) ? 0 : (FZ | FP)) | ((y == 7 && (v & 0x80)) ? FS : 0);
            f |= (z->wz >> 8) & (FY | FX);
            SETF(f);
            return 12;
        }
        uint8_t res = x == 0 ? rot(z, y, v) : x == 2 ? (uint8_t)(v & ~(1 << y)) : (uint8_t)(v | (1 << y));
        wr(a, res);
        return 15;
    }
    uint8_t v = getr(z, r, 0);
    if (x == 0) setr(z, r, 0, rot(z, y, v));
    else if (x == 1) {
        uint8_t f = (z->f & FC) | FH | ((v & (1 << y)) ? 0 : (FZ | FP)) | ((y == 7 && (v & 0x80)) ? FS : 0) | (v & (FY | FX));
        SETF(f);
    } else if (x == 2) setr(z, r, 0, (uint8_t)(v & ~(1 << y)));
    else setr(z, r, 0, (uint8_t)(v | (1 << y)));
    return 8;
}

/* ---- ED prefix ---- */
static int exec_ed(z80_t *z)
{
    uint8_t op = fetch(z);
    inc_r(z);
    int x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;
    if (x == 1) {
        switch (zz) {
        case 0: { uint8_t v = z80_in(BC); z->wz = (uint16_t)(BC + 1); if (y != 6) setr(z, y, 0, v); SETF((z->f & FC) | szp[v]); return 12; }
        case 1: z80_out(BC, y == 6 ? 0 : getr(z, y, 0)); z->wz = (uint16_t)(BC + 1); return 12;
        case 2: if (q) adc16(z, getrp(z, p, 0)); else sbc16(z, getrp(z, p, 0)); return 15;
        case 3: { uint16_t a = fetch16(z); if (q) setrp(z, p, 0, rd16(a)); else wr16(a, getrp(z, p, 0)); z->wz = (uint16_t)(a + 1); return 20; }
        case 4: { uint8_t a = z->a; z->a = 0; sub8(z, a, 0, 1); return 8; }       /* NEG */
        case 5: z->pc = pop(z); z->wz = z->pc; z->iff1 = z->iff2; return 14;      /* RETN/RETI */
        case 6: z->im = (uint8_t)((y & 3) == 0 || (y & 3) == 1 ? 0 : (y & 3) - 1); return 8;
        case 7:
            switch (y) {
            case 0: z->i = z->a; return 9;
            case 1: z->r = z->a; return 9;
            case 2: z->a = z->i; SETF((z->f & FC) | sz[z->a] | (z->iff2 ? FP : 0)); return 9;
            case 3: z->a = z->r; SETF((z->f & FC) | sz[z->a] | (z->iff2 ? FP : 0)); return 9;
            case 4: { uint8_t v = rd(HL); wr(HL, (uint8_t)((z->a << 4) | (v >> 4))); z->a = (uint8_t)((z->a & 0xF0) | (v & 0x0F));
                      z->wz = (uint16_t)(HL + 1); SETF((z->f & FC) | szp[z->a]); return 18; }   /* RRD */
            case 5: { uint8_t v = rd(HL); wr(HL, (uint8_t)((v << 4) | (z->a & 0x0F))); z->a = (uint8_t)((z->a & 0xF0) | (v >> 4));
                      z->wz = (uint16_t)(HL + 1); SETF((z->f & FC) | szp[z->a]); return 18; }   /* RLD */
            default: return 8;
            }
        }
    }
    if (x == 2 && y >= 4 && zz <= 3) {
        int dec = y & 1, rep = y >= 6;
        int t = 16;
        switch (zz) {
        case 0: {                                           /* LDI/LDD/LDIR/LDDR */
            uint8_t v = rd(HL);
            wr(DE, v);
            SETHL((uint16_t)(HL + (dec ? -1 : 1))); SETDE((uint16_t)(DE + (dec ? -1 : 1))); SETBC((uint16_t)(BC - 1));
            uint8_t n = (uint8_t)(v + z->a);
            uint8_t f = (z->f & (FS | FZ | FC)) | (BC ? FP : 0) | (n & FX) | ((n & 0x02) ? FY : 0);
            if (rep && BC) { z->pc -= 2; z->wz = (uint16_t)(z->pc + 1); f = (uint8_t)((f & ~(FY | FX)) | ((z->pc >> 8) & (FY | FX))); t = 21; }
            SETF(f);
            return t;
        }
        case 1: {                                           /* CPI/CPD/CPIR/CPDR */
            uint8_t v = rd(HL);
            uint8_t r = (uint8_t)(z->a - v);
            uint8_t h = (uint8_t)((z->a ^ v ^ r) & FH);
            SETHL((uint16_t)(HL + (dec ? -1 : 1))); SETBC((uint16_t)(BC - 1));
            z->wz = (uint16_t)(z->wz + (dec ? -1 : 1));
            uint8_t n = (uint8_t)(r - (h ? 1 : 0));
            uint8_t f = (z->f & FC) | (r & FS) | (r ? 0 : FZ) | h | FN | (BC ? FP : 0) | (n & FX) | ((n & 0x02) ? FY : 0);
            if (rep && BC && r) { z->pc -= 2; z->wz = (uint16_t)(z->pc + 1); f = (uint8_t)((f & ~(FY | FX)) | ((z->pc >> 8) & (FY | FX))); t = 21; }
            SETF(f);
            return t;
        }
        case 2: {                                           /* INI/IND/INIR/INDR */
            uint8_t v = z80_in(BC);
            z->wz = (uint16_t)(BC + (dec ? -1 : 1));
            wr(HL, v);
            z->b--;
            SETHL((uint16_t)(HL + (dec ? -1 : 1)));
            uint16_t k = (uint16_t)(v + (uint8_t)(z->c + (dec ? -1 : 1)));
            uint8_t f = sz[z->b] | ((v & 0x80) ? FN : 0) | (k > 255 ? (FH | FC) : 0) | (szp[(uint8_t)((k & 7) ^ z->b)] & FP);
            if (rep && z->b) { z->pc -= 2; t = 21; }
            SETF(f);
            return t;
        }
        default: {                                          /* OUTI/OUTD/OTIR/OTDR */
            uint8_t v = rd(HL);
            z->b--;
            z->wz = (uint16_t)(BC + (dec ? -1 : 1));
            z80_out(BC, v);
            SETHL((uint16_t)(HL + (dec ? -1 : 1)));
            uint16_t k = (uint16_t)(v + z->l);
            uint8_t f = sz[z->b] | ((v & 0x80) ? FN : 0) | (k > 255 ? (FH | FC) : 0) | (szp[(uint8_t)((k & 7) ^ z->b)] & FP);
            if (rep && z->b) { z->pc -= 2; t = 21; }
            SETF(f);
            return t;
        }
        }
    }
    return 8;                                               /* undefined ED: NOP */
}

/* ---- main table (idx: 0 HL, 1 IX, 2 IY) ---- */
static int exec_main(z80_t *z, uint8_t op, int idx)
{
    int x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;
    int xt = idx ? 4 : 0;                                   /* extra T-states for the prefix */

    /* (HL) / (IX+d) operand address */
#define MEMADDR() (idx ? (uint16_t)(z->wz = (uint16_t)((idx == 1 ? z->ix : z->iy) + (int8_t)fetch(z))) : HL)

    switch (x) {
    case 0:
        switch (zz) {
        case 0:
            switch (y) {
            case 0: return 4;                               /* NOP */
            case 1: { uint8_t t = z->a; z->a = z->a2; z->a2 = t; t = z->f; z->f = z->f2; z->f2 = t; return 4; }
            case 2: { int8_t d = (int8_t)fetch(z); z->b--; if (z->b) { z->pc = (uint16_t)(z->pc + d); z->wz = z->pc; return 13; } return 8; }
            case 3: { int8_t d = (int8_t)fetch(z); z->pc = (uint16_t)(z->pc + d); z->wz = z->pc; return 12; }
            default: { int8_t d = (int8_t)fetch(z); if (cond(z, y - 4)) { z->pc = (uint16_t)(z->pc + d); z->wz = z->pc; return 12; } return 7; }
            }
        case 1:
            if (!q) { setrp(z, p, idx, fetch16(z)); return 10 + xt; }
            setrp(z, 2, idx, add16(z, getrp(z, 2, idx), getrp(z, p, idx))); return 11 + xt;
        case 2:
            switch (y) {
            case 0: wr(BC, z->a); z->wz = (uint16_t)(((BC + 1) & 0xFF) | (z->a << 8)); return 7;
            case 1: z->a = rd(BC); z->wz = (uint16_t)(BC + 1); return 7;
            case 2: wr(DE, z->a); z->wz = (uint16_t)(((DE + 1) & 0xFF) | (z->a << 8)); return 7;
            case 3: z->a = rd(DE); z->wz = (uint16_t)(DE + 1); return 7;
            case 4: { uint16_t a = fetch16(z); wr16(a, getrp(z, 2, idx)); z->wz = (uint16_t)(a + 1); return 16 + xt; }
            case 5: { uint16_t a = fetch16(z); setrp(z, 2, idx, rd16(a)); z->wz = (uint16_t)(a + 1); return 16 + xt; }
            case 6: { uint16_t a = fetch16(z); wr(a, z->a); z->wz = (uint16_t)(((a + 1) & 0xFF) | (z->a << 8)); return 13; }
            default: { uint16_t a = fetch16(z); z->a = rd(a); z->wz = (uint16_t)(a + 1); return 13; }
            }
        case 3:
            setrp(z, p, idx, (uint16_t)(getrp(z, p, idx) + (q ? -1 : 1))); return 6 + xt;
        case 4: case 5:
            if (y == 6) {
                uint16_t a = MEMADDR();
                uint8_t v = rd(a);
                wr(a, zz == 4 ? inc8(z, v) : dec8(z, v));
                return idx ? 23 : 11;
            }
            setr(z, y, idx, zz == 4 ? inc8(z, getr(z, y, idx)) : dec8(z, getr(z, y, idx)));
            return 4 + xt;
        case 6:
            if (y == 6) {
                uint16_t a = MEMADDR();
                wr(a, fetch(z));
                return idx ? 19 : 10;
            }
            setr(z, y, idx, fetch(z)); return 7 + xt;
        default:
            switch (y) {
            case 0: { uint8_t c = z->a >> 7; z->a = (uint8_t)((z->a << 1) | c); SETF((z->f & (FS | FZ | FP)) | (z->a & (FY | FX)) | c); return 4; }
            case 1: { uint8_t c = z->a & 1; z->a = (uint8_t)((z->a >> 1) | (c << 7)); SETF((z->f & (FS | FZ | FP)) | (z->a & (FY | FX)) | c); return 4; }
            case 2: { uint8_t c = z->a >> 7; z->a = (uint8_t)((z->a << 1) | (z->f & FC)); SETF((z->f & (FS | FZ | FP)) | (z->a & (FY | FX)) | c); return 4; }
            case 3: { uint8_t c = z->a & 1; z->a = (uint8_t)((z->a >> 1) | ((z->f & FC) << 7)); SETF((z->f & (FS | FZ | FP)) | (z->a & (FY | FX)) | c); return 4; }
            case 4: daa(z); return 4;
            case 5: z->a = (uint8_t)~z->a; SETF((z->f & (FS | FZ | FP | FC)) | FH | FN | (z->a & (FY | FX))); return 4;
            case 6: SETF((z->f & (FS | FZ | FP)) | FC | (((z->last_q ^ z->f) | z->a) & (FY | FX))); return 4;  /* SCF */
            default: SETF((z->f & (FS | FZ | FP)) | ((z->f & FC) ? FH : FC) | (((z->last_q ^ z->f) | z->a) & (FY | FX))); return 4; /* CCF */
            }
        }
    case 1:
        if (op == 0x76) { z->halted = 1; return 4; }
        if (y == 6) { uint16_t a = MEMADDR(); wr(a, getr(z, zz, 0)); return idx ? 19 : 7; }
        if (zz == 6) { uint16_t a = MEMADDR(); setr(z, y, 0, rd(a)); return idx ? 19 : 7; }
        setr(z, y, idx, getr(z, zz, idx)); return 4 + xt;
    case 2:
        if (zz == 6) { uint16_t a = MEMADDR(); alu(z, y, rd(a)); return idx ? 19 : 7; }
        alu(z, y, getr(z, zz, idx)); return 4 + xt;
    default:
        switch (zz) {
        case 0: if (cond(z, y)) { z->pc = pop(z); z->wz = z->pc; return 11; } return 5;
        case 1:
            if (!q) {
                uint16_t v = pop(z);
                if (p == 3) { z->a = v >> 8; z->f = (uint8_t)v; z->q = 0; }
                else setrp(z, p, idx, v);
                return 10 + xt;
            }
            switch (p) {
            case 0: z->pc = pop(z); z->wz = z->pc; return 10;
            case 1: { uint8_t t;
                t = z->b; z->b = z->b2; z->b2 = t; t = z->c; z->c = z->c2; z->c2 = t;
                t = z->d; z->d = z->d2; z->d2 = t; t = z->e; z->e = z->e2; z->e2 = t;
                t = z->h; z->h = z->h2; z->h2 = t; t = z->l; z->l = z->l2; z->l2 = t; return 4; }
            case 2: z->pc = getrp(z, 2, idx); return 4 + xt;
            default: z->sp = getrp(z, 2, idx); return 6 + xt;
            }
        case 2: { uint16_t a = fetch16(z); z->wz = a; if (cond(z, y)) z->pc = a; return 10; }
        case 3:
            switch (y) {
            case 0: z->pc = fetch16(z); z->wz = z->pc; return 10;
            case 1: return exec_cb(z, 0, 0);
            case 2: { uint8_t n = fetch(z); z80_out((uint16_t)(n | (z->a << 8)), z->a); z->wz = (uint16_t)(((n + 1) & 0xFF) | (z->a << 8)); return 11; }
            case 3: { uint8_t n = fetch(z); uint16_t port = (uint16_t)(n | (z->a << 8)); z->a = z80_in(port); z->wz = (uint16_t)(port + 1); return 11; }
            case 4: { uint16_t v = rd16(z->sp); wr16(z->sp, getrp(z, 2, idx)); setrp(z, 2, idx, v); z->wz = v; return 19 + xt; }
            case 5: { uint16_t t = DE; SETDE(HL); SETHL(t); return 4; }
            case 6: z->iff1 = z->iff2 = 0; return 4;
            default: z->iff1 = z->iff2 = 1; z->ei_pending = 1; return 4;
            }
        case 4: { uint16_t a = fetch16(z); z->wz = a; if (cond(z, y)) { push(z, z->pc); z->pc = a; return 17; } return 10; }
        case 5:
            if (!q) {
                uint16_t v = p == 3 ? AF : getrp(z, p, idx);
                push(z, v);
                return 11 + xt;
            }
            if (p == 0) { uint16_t a = fetch16(z); z->wz = a; push(z, z->pc); z->pc = a; return 17; }
            return 4;   /* DD/ED/FD handled by the caller */
        case 6: alu(z, y, fetch(z)); return 7;
        default: push(z, z->pc); z->pc = (uint16_t)(y * 8); z->wz = z->pc; return 11;
        }
    }
#undef MEMADDR
}

int z80_step(z80_t *z)
{
    if (z->halted) { inc_r(z); return 4; }
    z->last_q = z->q;
    z->q = 0;
    z->ei_pending = 0;
    uint8_t op = fetch(z);
    inc_r(z);
    int t = 0;
    for (;;) {
        if (op == 0xED) return t + exec_ed(z);
        if (op != 0xDD && op != 0xFD) return t + exec_main(z, op, 0);
        int idx = op == 0xDD ? 1 : 2;
        uint8_t op2 = fetch(z);
        inc_r(z);
        if (op2 == 0xDD || op2 == 0xFD || op2 == 0xED) { t += 4; op = op2; continue; }
        if (op2 == 0xCB) {
            uint16_t a = (uint16_t)((idx == 1 ? z->ix : z->iy) + (int8_t)fetch(z));
            z->wz = a;
            return t + exec_cb(z, idx, a);
        }
        /* instructions without HL/H/L use the prefix as a 4-cycle NOP */
        return t + exec_main(z, op2, idx) + ((op2 & 0xC0) == 0x40 && ((op2 & 7) == 6 || ((op2 >> 3) & 7) == 6) ? 0 : 0);
    }
}

int z80_interrupt(z80_t *z)
{
    if (!z->iff1 || z->ei_pending) return 0;
    z->halted = 0;
    z->iff1 = z->iff2 = 0;
    inc_r(z);
    push(z, z->pc);
    if (z->im == 2) { uint16_t v = (uint16_t)((z->i << 8) | 0xFF); z->pc = rd16(v); z->wz = z->pc; return 19; }
    z->pc = 0x38; z->wz = z->pc;
    return z->im == 0 ? 12 : 13;
}
