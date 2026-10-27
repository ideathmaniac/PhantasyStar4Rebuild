/*
 * 68000 interpreter. It is the reference the recompiled code is checked
 * against, and the fallback for code the static tracer never found
 * (object handlers called through pointers in RAM, RAM code).
 */
#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

cpu_t R;
recomp_fn (*cpu_lookup)(uint32_t pc);

static inline uint16_t fetch16(void) { uint16_t v = (uint16_t)R16(R.pc); R.pc += 2; return v; }
static inline uint32_t fetch32(void) { uint32_t v = R32(R.pc); R.pc += 4; return v; }

static inline uint32_t MASK(int s) { return s == 1 ? 0xFFu : s == 2 ? 0xFFFFu : 0xFFFFFFFFu; }
static inline uint32_t MSB(int s)  { return s == 1 ? 0x80u : s == 2 ? 0x8000u : 0x80000000u; }

static inline uint32_t rd(uint32_t a, int s) { return s == 1 ? R8(a) : s == 2 ? R16(a) : R32(a); }
static inline void wr(uint32_t a, int s, uint32_t v) { if (s == 1) W8(a, v); else if (s == 2) W16(a, v); else W32(a, v); }

static uint32_t index_ea(uint32_t base)
{
    uint16_t ext = fetch16();
    uint32_t xn = (ext & 0x8000) ? R.a[(ext >> 12) & 7] : R.d[(ext >> 12) & 7];
    if (!(ext & 0x0800)) xn = SX16(xn);
    return base + SX8(ext) + xn;
}

/* address of a memory operand (applies (An)+ / -(An) side effects) */
static uint32_t ea_addr(int mode, int reg, int size)
{
    switch (mode) {
    case 2: return R.a[reg];
    case 3: { uint32_t a = R.a[reg]; R.a[reg] += (size == 1 && reg == 7) ? 2 : size; return a; }
    case 4: R.a[reg] -= (size == 1 && reg == 7) ? 2 : size; return R.a[reg];
    case 5: { uint32_t b = R.a[reg]; return b + SX16(fetch16()); }
    case 6: return index_ea(R.a[reg]);
    default:
        switch (reg) {
        case 0: return SX16(fetch16());
        case 1: return fetch32();
        case 2: { uint32_t b = R.pc; return b + SX16(fetch16()); }
        case 3: { uint32_t b = R.pc; return index_ea(b); }
        }
    }
    return 0;
}

typedef struct { int mode, reg, size; uint32_t addr; } EA;

static void ea_prep(EA *e, int mode, int reg, int size)
{
    e->mode = mode; e->reg = reg; e->size = size; e->addr = 0;
    if (mode >= 2) {
        if (mode == 7 && reg == 4)  /* immediate: fetch now, keep the value */
            e->addr = size == 4 ? fetch32() : size == 2 ? fetch16() : (fetch16() & 0xFF);
        else
            e->addr = ea_addr(mode, reg, size);
    }
}
static uint32_t ea_get(EA *e)
{
    if (e->mode == 0) return R.d[e->reg] & MASK(e->size);
    if (e->mode == 1) return R.a[e->reg] & MASK(e->size);
    if (e->mode == 7 && e->reg == 4) return e->addr;
    return rd(e->addr, e->size);
}
static void ea_put(EA *e, uint32_t v)
{
    v &= MASK(e->size);
    if (e->mode == 0) { uint32_t m = MASK(e->size); R.d[e->reg] = (R.d[e->reg] & ~m) | v; return; }
    if (e->mode == 1) { R.a[e->reg] = v; return; }
    wr(e->addr, e->size, v);
}
static uint32_t ea_read(int mode, int reg, int size) { EA e; ea_prep(&e, mode, reg, size); return ea_get(&e); }

/* ---- exceptions ---- */
void m68k_exception(int vec, uint32_t pc_to_push)
{
    uint16_t sr = cpu_get_sr();
    cpu_set_sr((sr & 0x07FF & ~0x8000) | 0x2000 | (sr & 0x0700));
    PUSH32(pc_to_push);
    PUSH16(sr);
    R.pc = R32(vec * 4) & 0xFFFFFF;
}

void m68k_take_irq(int level)
{
    uint16_t sr = cpu_get_sr();
    R.stopped = 0;
    cpu_set_sr((sr & 0x00FF) | 0x2000 | (level << 8));
    PUSH32(R.pc);
    PUSH16(sr);
    md_irq_ack(level);
    R.pc = R32((24 + level) * 4) & 0xFFFFFF;
}

void m68k_reset(void)
{
    for (int i = 0; i < 8; i++) R.d[i] = R.a[i] = 0;
    R.usp = 0; R.srh = 0x27; R.x = R.n = R.z = R.v = R.c = 0; R.stopped = 0;
    R.a[7] = R32(0);
    R.pc = R32(4) & 0xFFFFFF;
}

uint32_t g_ipc;   /* address of the instruction being executed */
static void illegal(void) { m68k_exception(4, g_ipc); }
static int priv(void) { if (!(R.srh & 0x20)) { m68k_exception(8, g_ipc); return 0; } return 1; }

/* generic size dispatch for the flag helpers */
static uint32_t do_add(int s, uint32_t a, uint32_t b) { return s == 1 ? ADD8(a, b) : s == 2 ? ADD16(a, b) : ADD32(a, b); }
static uint32_t do_sub(int s, uint32_t a, uint32_t b) { return s == 1 ? SUB8(a, b) : s == 2 ? SUB16(a, b) : SUB32(a, b); }
static void     do_cmp(int s, uint32_t a, uint32_t b) { if (s == 1) CMP8(a, b); else if (s == 2) CMP16(a, b); else CMP32(a, b); }
static uint32_t do_addx(int s, uint32_t a, uint32_t b) { return s == 1 ? ADDX8(a, b) : s == 2 ? ADDX16(a, b) : ADDX32(a, b); }
static uint32_t do_subx(int s, uint32_t a, uint32_t b) { return s == 1 ? SUBX8(a, b) : s == 2 ? SUBX16(a, b) : SUBX32(a, b); }
static uint32_t do_logic(int s, uint32_t r) { return s == 1 ? LOGIC8(r) : s == 2 ? LOGIC16(r) : LOGIC32(r); }
static uint32_t do_neg(int s, uint32_t a) { return s == 1 ? NEG8(a) : s == 2 ? NEG16(a) : NEG32(a); }
static uint32_t do_negx(int s, uint32_t a) { return s == 1 ? NEGX8(a) : s == 2 ? NEGX16(a) : NEGX32(a); }
static uint32_t do_shift(int type, int left, int s, uint32_t v, int n)
{
#define SH(OP) (s == 1 ? OP##8(v, n) : s == 2 ? OP##16(v, n) : OP##32(v, n))
    switch (type * 2 + left) {
    case 0: return SH(ASR);  case 1: return SH(ASL);
    case 2: return SH(LSR);  case 3: return SH(LSL);
    case 4: return SH(ROXR); case 5: return SH(ROXL);
    case 6: return SH(ROR);  default: return SH(ROL);
    }
#undef SH
}

static const int SZ[4] = { 1, 2, 4, 0 };

static void op_bit(uint16_t op, uint32_t bit)
{
    int mode = (op >> 3) & 7, reg = op & 7, type = (op >> 6) & 3;
    if (mode == 0) {
        bit &= 31;
        uint32_t m = 1u << bit;
        R.z = (R.d[reg] & m) == 0;
        if (type == 1) R.d[reg] ^= m; else if (type == 2) R.d[reg] &= ~m; else if (type == 3) R.d[reg] |= m;
        return;
    }
    bit &= 7;
    EA e; ea_prep(&e, mode, reg, 1);
    uint32_t v = ea_get(&e), m = 1u << bit;
    R.z = (v & m) == 0;
    if (type == 0) return;
    if (type == 1) v ^= m; else if (type == 2) v &= ~m; else v |= m;
    ea_put(&e, v);
}

static void grp0(uint16_t op)
{
    int mode = (op >> 3) & 7, reg = op & 7;
    switch (op) {
    case 0x003C: cpu_set_ccr(cpu_get_sr() | (fetch16() & 0x1F)); return;
    case 0x023C: cpu_set_ccr(cpu_get_sr() & fetch16() & 0x1F); return;
    case 0x0A3C: cpu_set_ccr((cpu_get_sr() ^ fetch16()) & 0x1F); return;
    case 0x007C: { uint16_t v = fetch16(); if (priv()) cpu_set_sr(cpu_get_sr() | v); return; }
    case 0x027C: { uint16_t v = fetch16(); if (priv()) cpu_set_sr(cpu_get_sr() & v); return; }
    case 0x0A7C: { uint16_t v = fetch16(); if (priv()) cpu_set_sr(cpu_get_sr() ^ v); return; }
    }
    if ((op & 0x0138) == 0x0108) {            /* MOVEP */
        int dn = (op >> 9) & 7, om = (op >> 6) & 7;
        uint32_t a = R.a[reg] + SX16(fetch16());
        switch (om) {
        case 4: R.d[dn] = (R.d[dn] & 0xFFFF0000u) | (R8(a) << 8) | R8(a + 2); break;
        case 5: R.d[dn] = (R8(a) << 24) | (R8(a + 2) << 16) | (R8(a + 4) << 8) | R8(a + 6); break;
        case 6: W8(a, R.d[dn] >> 8); W8(a + 2, R.d[dn]); break;
        case 7: W8(a, R.d[dn] >> 24); W8(a + 2, R.d[dn] >> 16); W8(a + 4, R.d[dn] >> 8); W8(a + 6, R.d[dn]); break;
        }
        return;
    }
    if (op & 0x0100) { op_bit(op, R.d[(op >> 9) & 7]); return; }
    int sub = (op >> 9) & 7;
    if (sub == 4) { uint32_t b = fetch16() & 0xFF; op_bit(op, b); return; }
    int s = SZ[(op >> 6) & 3];
    if (!s || sub == 7) { illegal(); return; }
    uint32_t imm = s == 4 ? fetch32() : s == 2 ? fetch16() : (fetch16() & 0xFF);
    EA e; ea_prep(&e, mode, reg, s);
    uint32_t d = ea_get(&e);
    switch (sub) {
    case 0: ea_put(&e, do_logic(s, d | imm)); break;
    case 1: ea_put(&e, do_logic(s, d & imm)); break;
    case 2: ea_put(&e, do_sub(s, imm, d)); break;
    case 3: ea_put(&e, do_add(s, imm, d)); break;
    case 5: ea_put(&e, do_logic(s, d ^ imm)); break;
    case 6: do_cmp(s, imm, d); break;
    default: illegal();
    }
}

static void grp_move(uint16_t op)
{
    int s = (op >> 12) == 1 ? 1 : (op >> 12) == 3 ? 2 : 4;
    uint32_t v = ea_read((op >> 3) & 7, op & 7, s);
    int dm = (op >> 6) & 7, dr = (op >> 9) & 7;
    if (dm == 1) { R.a[dr] = s == 2 ? SX16(v) : v; return; }
    do_logic(s, v);
    EA e; ea_prep(&e, dm, dr, s);
    ea_put(&e, v);
}

static void op_movem(uint16_t op)
{
    int s = (op & 0x40) ? 4 : 2, mode = (op >> 3) & 7, reg = op & 7;
    uint16_t mask = fetch16();
    if (!(op & 0x0400)) {                       /* registers to memory */
        if (mode == 4) {
            uint32_t a = R.a[reg];
            for (int i = 0; i < 16; i++) if (mask & (1 << i)) {
                uint32_t v = i < 8 ? R.a[7 - i] : R.d[15 - i];
                a -= s; wr(a, s, v);
            }
            R.a[reg] = a;
        } else {
            uint32_t a = ea_addr(mode, reg, s);
            for (int i = 0; i < 16; i++) if (mask & (1 << i)) {
                uint32_t v = i < 8 ? R.d[i] : R.a[i - 8];
                wr(a, s, v); a += s;
            }
        }
    } else {                                    /* memory to registers */
        uint32_t a = mode == 3 ? R.a[reg] : ea_addr(mode, reg, s);
        for (int i = 0; i < 16; i++) if (mask & (1 << i)) {
            uint32_t v = rd(a, s);
            if (s == 2) v = SX16(v);
            if (i < 8) R.d[i] = v; else R.a[i - 8] = v;
            a += s;
        }
        if (mode == 3) R.a[reg] = a;
    }
}

static void grp4(uint16_t op)
{
    int mode = (op >> 3) & 7, reg = op & 7;
    switch (op) {
    case 0x4E70: priv(); return;                 /* RESET */
    case 0x4E71: return;
    case 0x4E72: { uint16_t v = fetch16(); if (priv()) { cpu_set_sr(v); R.stopped = 1; } return; }
    case 0x4E73: if (priv()) { uint16_t sr = (uint16_t)POP16(); uint32_t pc = POP32(); cpu_set_sr(sr); R.pc = pc & 0xFFFFFF; } return;
    case 0x4E75: R.pc = POP32() & 0xFFFFFF; return;
    case 0x4E76: if (R.v) m68k_exception(7, R.pc); return;
    case 0x4E77: { uint16_t c = (uint16_t)POP16(); cpu_set_ccr(c); R.pc = POP32() & 0xFFFFFF; return; }
    case 0x4AFC: illegal(); return;
    }
    if ((op & 0xFFF0) == 0x4E40) { m68k_exception(32 + (op & 15), R.pc); return; }
    if ((op & 0xFFF8) == 0x4E50) { int16_t d = (int16_t)fetch16(); PUSH32(reg == 7 ? R.a[7] - 4 : R.a[reg]); R.a[reg] = R.a[7]; R.a[7] += (uint32_t)(int32_t)d; return; }
    if ((op & 0xFFF8) == 0x4E58) { R.a[7] = R.a[reg]; R.a[reg] = POP32(); return; }
    if ((op & 0xFFF0) == 0x4E60) { if (priv()) { if (op & 8) R.a[reg] = R.usp; else R.usp = R.a[reg]; } return; }
    if ((op & 0xFFC0) == 0x4E80) { uint32_t a = ea_addr(mode, reg, 4); PUSH32(R.pc); R.pc = a & 0xFFFFFF; return; }
    if ((op & 0xFFC0) == 0x4EC0) { uint32_t a = ea_addr(mode, reg, 4); R.pc = a & 0xFFFFFF; return; }
    if ((op & 0xF1C0) == 0x41C0) { R.a[(op >> 9) & 7] = ea_addr(mode, reg, 4); return; }
    if ((op & 0xF1C0) == 0x4180) {               /* CHK.W */
        int16_t bound = (int16_t)ea_read(mode, reg, 2), dn = (int16_t)R.d[(op >> 9) & 7];
        R.z = dn == 0; R.v = 0; R.c = 0;
        if (dn < 0) { R.n = 1; m68k_exception(6, R.pc); }
        else if (dn > bound) { R.n = 0; m68k_exception(6, R.pc); }
        return;
    }
    if ((op & 0xFFB8) == 0x4880) {               /* EXT */
        if (op & 0x40) { R.d[reg] = SX16(R.d[reg]); NZ32(R.d[reg]); }
        else { R.d[reg] = (R.d[reg] & 0xFFFF0000u) | (SX8(R.d[reg]) & 0xFFFF); NZ16(R.d[reg]); }
        R.v = R.c = 0; return;
    }
    if ((op & 0xFB80) == 0x4880) { op_movem(op); return; }
    if ((op & 0xFFF8) == 0x4840) { R.d[reg] = (R.d[reg] >> 16) | (R.d[reg] << 16); LOGIC32(R.d[reg]); return; }
    if ((op & 0xFFC0) == 0x4840) { uint32_t a = ea_addr(mode, reg, 4); PUSH32(a); return; }
    if ((op & 0xFFC0) == 0x4800) { EA e; ea_prep(&e, mode, reg, 1); ea_put(&e, SBCD8(ea_get(&e), 0)); return; }
    if ((op & 0xFFC0) == 0x40C0) { EA e; ea_prep(&e, mode, reg, 2); ea_put(&e, cpu_get_sr()); return; }
    if ((op & 0xFFC0) == 0x44C0) { cpu_set_ccr(ea_read(mode, reg, 2)); return; }
    if ((op & 0xFFC0) == 0x46C0) { uint32_t v = ea_read(mode, reg, 2); if (priv()) cpu_set_sr(v); return; }
    if ((op & 0xFFC0) == 0x4AC0) {               /* TAS */
        EA e; ea_prep(&e, mode, reg, 1);
        uint32_t v = ea_get(&e); LOGIC8(v); ea_put(&e, v | 0x80); return;
    }
    int s = SZ[(op >> 6) & 3];
    if (!s) { illegal(); return; }
    switch (op & 0xFF00) {
    case 0x4000: { EA e; ea_prep(&e, mode, reg, s); ea_put(&e, do_negx(s, ea_get(&e))); return; }
    case 0x4200: { EA e; ea_prep(&e, mode, reg, s); ea_put(&e, 0); R.n = 0; R.z = 1; R.v = 0; R.c = 0; return; }
    case 0x4400: { EA e; ea_prep(&e, mode, reg, s); ea_put(&e, do_neg(s, ea_get(&e))); return; }
    case 0x4600: { EA e; ea_prep(&e, mode, reg, s); ea_put(&e, do_logic(s, ~ea_get(&e))); return; }
    case 0x4A00: do_logic(s, ea_read(mode, reg, s)); return;
    }
    illegal();
}

static void grp5(uint16_t op)
{
    int mode = (op >> 3) & 7, reg = op & 7, sz = (op >> 6) & 3;
    if (sz == 3) {
        int cc = (op >> 8) & 15;
        if (mode == 1) {                        /* DBcc */
            uint32_t base = R.pc; int16_t d = (int16_t)fetch16();
            if (!cpu_cond(cc)) {
                uint16_t c = (uint16_t)(R.d[reg] - 1);
                R.d[reg] = (R.d[reg] & 0xFFFF0000u) | c;
                if (c != 0xFFFF) R.pc = (base + d) & 0xFFFFFF;
            }
            return;
        }
        EA e; ea_prep(&e, mode, reg, 1);
        ea_put(&e, cpu_cond(cc) ? 0xFF : 0);
        return;
    }
    int s = SZ[sz];
    uint32_t q = (op >> 9) & 7; if (!q) q = 8;
    if (mode == 1) { if (op & 0x100) R.a[reg] -= q; else R.a[reg] += q; return; }
    EA e; ea_prep(&e, mode, reg, s);
    uint32_t d = ea_get(&e);
    ea_put(&e, (op & 0x100) ? do_sub(s, q, d) : do_add(s, q, d));
}

static void grp6(uint16_t op)
{
    int cc = (op >> 8) & 15;
    uint32_t base = R.pc;
    int32_t d = (int8_t)(op & 0xFF);
    if (!d) d = (int16_t)fetch16();
    if (cc == 1) { PUSH32(R.pc); R.pc = (base + d) & 0xFFFFFF; return; }
    if (cpu_cond(cc)) R.pc = (base + d) & 0xFFFFFF;
}

static void bcd_or_x(uint16_t op, int kind) /* kind: 0 sbcd 1 abcd 2 subx 3 addx */
{
    int rx = (op >> 9) & 7, ry = op & 7;
    int s = (kind < 2) ? 1 : SZ[(op >> 6) & 3];
    uint32_t src, dst, r;
    if (op & 8) {
        R.a[ry] -= (s == 1 && ry == 7) ? 2 : s; src = rd(R.a[ry], s);
        R.a[rx] -= (s == 1 && rx == 7) ? 2 : s; dst = rd(R.a[rx], s);
    } else { src = R.d[ry] & MASK(s); dst = R.d[rx] & MASK(s); }
    switch (kind) {
    case 0: r = SBCD8(src, dst); break;
    case 1: r = ABCD8(src, dst); break;
    case 2: r = do_subx(s, src, dst); break;
    default: r = do_addx(s, src, dst); break;
    }
    if (op & 8) wr(R.a[rx], s, r);
    else R.d[rx] = (R.d[rx] & ~MASK(s)) | (r & MASK(s));
}

static void grp8C(uint16_t op)  /* OR / DIV / SBCD  and  AND / MUL / ABCD / EXG */
{
    int isC = (op >> 12) == 0xC, mode = (op >> 3) & 7, reg = op & 7, dn = (op >> 9) & 7;
    if ((op & 0x00C0) == 0x00C0) {
        uint32_t src = ea_read(mode, reg, 2);
        if (isC) {
            if (op & 0x100) R.d[dn] = (uint32_t)((int32_t)(int16_t)R.d[dn] * (int32_t)(int16_t)src);
            else R.d[dn] = (R.d[dn] & 0xFFFF) * (src & 0xFFFF);
            LOGIC32(R.d[dn]);
        } else {
            int ok = (op & 0x100) ? DIVS(dn, src) : DIVU(dn, src);
            if (!ok) m68k_exception(5, R.pc);
        }
        return;
    }
    if ((op & 0x01F0) == 0x0100) { bcd_or_x(op, isC ? 1 : 0); return; }
    if (isC && ((op & 0x01F8) == 0x0140 || (op & 0x01F8) == 0x0148 || (op & 0x01F8) == 0x0188)) {
        uint32_t t;
        switch (op & 0x01F8) {
        case 0x0140: t = R.d[dn]; R.d[dn] = R.d[reg]; R.d[reg] = t; break;
        case 0x0148: t = R.a[dn]; R.a[dn] = R.a[reg]; R.a[reg] = t; break;
        default:     t = R.d[dn]; R.d[dn] = R.a[reg]; R.a[reg] = t; break;
        }
        return;
    }
    int s = SZ[(op >> 6) & 3];
    if (op & 0x100) {
        EA e; ea_prep(&e, mode, reg, s);
        uint32_t d = ea_get(&e);
        ea_put(&e, do_logic(s, isC ? (d & R.d[dn]) : (d | R.d[dn])));
    } else {
        uint32_t v = ea_read(mode, reg, s);
        uint32_t r = do_logic(s, isC ? (v & R.d[dn]) : (v | R.d[dn]));
        R.d[dn] = (R.d[dn] & ~MASK(s)) | r;
    }
}

static void grp9D(uint16_t op)  /* SUB / ADD family */
{
    int add = (op >> 12) == 0xD, mode = (op >> 3) & 7, reg = op & 7, rn = (op >> 9) & 7, om = (op >> 6) & 7;
    if (om == 3 || om == 7) {
        uint32_t v = ea_read(mode, reg, om == 3 ? 2 : 4);
        if (om == 3) v = SX16(v);
        R.a[rn] = add ? R.a[rn] + v : R.a[rn] - v;
        return;
    }
    int s = SZ[om & 3];
    if (om >= 4 && mode < 2) { bcd_or_x(op, add ? 3 : 2); return; }
    if (om < 4) {
        uint32_t v = ea_read(mode, reg, s);
        uint32_t r = add ? do_add(s, v, R.d[rn]) : do_sub(s, v, R.d[rn]);
        R.d[rn] = (R.d[rn] & ~MASK(s)) | r;
    } else {
        EA e; ea_prep(&e, mode, reg, s);
        uint32_t d = ea_get(&e);
        ea_put(&e, add ? do_add(s, R.d[rn], d) : do_sub(s, R.d[rn], d));
    }
}

static void grpB(uint16_t op)
{
    int mode = (op >> 3) & 7, reg = op & 7, rn = (op >> 9) & 7, om = (op >> 6) & 7;
    if (om == 3 || om == 7) {
        uint32_t v = ea_read(mode, reg, om == 3 ? 2 : 4);
        if (om == 3) v = SX16(v);
        CMP32(v, R.a[rn]);
        return;
    }
    int s = SZ[om & 3];
    if (om < 4) { do_cmp(s, ea_read(mode, reg, s), R.d[rn]); return; }
    if (mode == 1) {                            /* CMPM (Ay)+,(Ax)+ */
        uint32_t src = rd(R.a[reg], s); R.a[reg] += (s == 1 && reg == 7) ? 2 : s;
        uint32_t dst = rd(R.a[rn], s);  R.a[rn] += (s == 1 && rn == 7) ? 2 : s;
        do_cmp(s, src, dst);
        return;
    }
    EA e; ea_prep(&e, mode, reg, s);
    ea_put(&e, do_logic(s, ea_get(&e) ^ R.d[rn]));
}

static void grpE(uint16_t op)
{
    int sz = (op >> 6) & 3, left = (op >> 8) & 1;
    if (sz == 3) {
        int type = (op >> 9) & 3;
        EA e; ea_prep(&e, (op >> 3) & 7, op & 7, 2);
        ea_put(&e, do_shift(type, left, 2, ea_get(&e), 1));
        return;
    }
    int s = SZ[sz], reg = op & 7, type = (op >> 3) & 3;
    int n = (op & 0x20) ? (int)(R.d[(op >> 9) & 7] & 63) : ((((op >> 9) & 7) + 7) & 7) + 1;
    uint32_t r = do_shift(type, left, s, R.d[reg], n);
    if (n == 0 && type == 2) R.c = R.x;          /* ROXL/ROXR by 0: C = X */
    R.d[reg] = (R.d[reg] & ~MASK(s)) | (r & MASK(s));
}

int m68k_step(void)
{
    g_ipc = R.pc;
#ifdef PS4_TRACE_PC   /* debugging: registers each time the code reaches one address */
    { static long tp = -2; if (tp == -2) { const char *e = getenv("TRACE_PC"); tp = e ? strtol(e, 0, 16) : -1; }
      if ((long)R.pc == tp) { extern uint64_t g_frame; uint32_t sp = R.a[7]; 
        fprintf(stderr, "f%llu TRACE %06X a0=%06X a1=%06X a4=%06X d0=%X d1=%X ret=%02X%02X%02X%02X\n", (unsigned long long)g_frame, R.pc, R.a[0], R.a[1], R.a[4], R.d[0], R.d[1],
                md_read8(sp), md_read8(sp + 1), md_read8(sp + 2), md_read8(sp + 3)); } }
#endif
    uint16_t op = fetch16();
    switch (op >> 12) {
    case 0x0: grp0(op); break;
    case 0x1: case 0x2: case 0x3: grp_move(op); break;
    case 0x4: grp4(op); break;
    case 0x5: grp5(op); break;
    case 0x6: grp6(op); break;
    case 0x7: if (op & 0x100) illegal(); else { R.d[(op >> 9) & 7] = SX8(op); LOGIC32(SX8(op)); } break;
    case 0x8: case 0xC: grp8C(op); break;
    case 0x9: case 0xD: grp9D(op); break;
    case 0xB: grpB(op); break;
    case 0xE: grpE(op); break;
    case 0xA: m68k_exception(10, g_ipc); break;
    default:  m68k_exception(11, g_ipc); break;
    }
    return g_cyc[op];
}

/* ---- approximate cycle costs, by opcode word only ---- */
static uint8_t cyc_tab[65536];
const uint8_t *g_cyc = cyc_tab;

static int ea_cost(int mode, int reg, int s)
{
    int l = s == 4 ? 4 : 0;
    switch (mode) {
    case 0: case 1: return 0;
    case 2: case 3: return 4 + l;
    case 4: return 6 + l;
    case 5: return 8 + l;
    case 6: return 10 + l;
    default:
        switch (reg) { case 0: return 8 + l; case 1: return 12 + l; case 2: return 8 + l; case 3: return 10 + l; default: return 4 + l; }
    }
}

static int cyc_of(uint16_t op)
{
    int hi = op >> 12, mode = (op >> 3) & 7, reg = op & 7, sz = (op >> 6) & 3;
    int s = sz == 0 ? 1 : sz == 1 ? 2 : 4;
    switch (hi) {
    case 0:
        if (op & 0x0100 || (op & 0x0F00) == 0x0800) return 8 + ea_cost(mode, reg, 1);
        return (s == 4 ? 16 : 8) + ea_cost(mode, reg, s) + (mode ? 4 : 0);
    case 1: case 2: case 3: {
        int ms = hi == 1 ? 1 : hi == 3 ? 2 : 4;
        int dm = (op >> 6) & 7, dr = (op >> 9) & 7;
        int dc = dm == 4 ? ea_cost(2, 0, ms) : ea_cost(dm, dr, ms);
        return 4 + ea_cost(mode, reg, ms) + dc;
    }
    case 4:
        if (op == 0x4E75) return 16;
        if (op == 0x4E73) return 20;
        if ((op & 0xFFC0) == 0x4E80) return 16 + ea_cost(mode, reg, 2) / 2;
        if ((op & 0xFFC0) == 0x4EC0) return 8 + ea_cost(mode, reg, 2) / 2;
        if ((op & 0xF1C0) == 0x41C0) return 4 + ea_cost(mode, reg, 2) / 2;
        if ((op & 0xFB80) == 0x4880 && mode >= 2) return 8 + 4 * 6;   /* MOVEM, assume ~6 regs */
        if ((op & 0xFFF0) == 0x4E40) return 34;
        if ((op & 0xFFF8) == 0x4E50) return 16;
        if ((op & 0xFFF8) == 0x4E58) return 12;
        if ((op & 0xFFC0) == 0x4840) return mode ? 12 + ea_cost(mode, reg, 2) / 2 : 4;
        return (s == 4 ? 6 : 4) + ea_cost(mode, reg, s) + (mode ? 4 : 0);
    case 5:
        if (sz == 3) return mode == 1 ? 10 : 6 + ea_cost(mode, reg, 1);
        return (s == 4 ? 8 : 4) + ea_cost(mode, reg, s) + (mode > 1 ? 4 : 0);
    case 6: return (op >> 8 & 15) == 1 ? 18 : 10;
    case 7: return 4;
    case 8: case 0xC:
        if ((op & 0x00C0) == 0x00C0) return (hi == 8 ? 140 : 70) + ea_cost(mode, reg, 2);
        return (s == 4 ? 8 : 4) + ea_cost(mode, reg, s) + ((op & 0x100) && mode > 1 ? 4 : 0);
    case 9: case 0xD: case 0xB:
        if (sz == 3) return 8 + ea_cost(mode, reg, (op & 0x100) ? 4 : 2);
        return (s == 4 ? 8 : 4) + ea_cost(mode, reg, s) + ((op & 0x100) && mode > 1 ? 4 : 0);
    case 0xE:
        if (sz == 3) return 8 + ea_cost(mode, reg, 2);
        return (s == 4 ? 8 : 6) + 2 * (((op >> 9) & 7) ? ((op >> 9) & 7) : 4);
    default: return 34;
    }
}

void m68k_init_tables(void)
{
    for (int i = 0; i < 65536; i++) { int c = cyc_of((uint16_t)i); cyc_tab[i] = (uint8_t)(c > 255 ? 255 : c); }
}
