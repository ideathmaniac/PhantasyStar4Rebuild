/*
 * 68000 CPU state and the condition-code helpers shared by the interpreter
 * (src/m68k.c) and the recompiled code (generated/ files). Both sides use these
 * exact functions, so they cannot disagree on flags.
 */
#ifndef PS4_CPU_H
#define PS4_CPU_H
#include <stdint.h>
#include "md.h"

typedef struct {
    uint32_t d[8], a[8];
    uint32_t usp;            /* the inactive stack pointer */
    uint32_t pc;
    uint8_t x, n, z, v, c;   /* condition codes as 0/1 */
    uint8_t srh;             /* SR bits 8-15 (T . S . . I2 I1 I0), masked 0xA7 */
    uint8_t stopped;         /* STOP executed, waiting for an interrupt */
} cpu_t;

extern cpu_t R;

static inline uint16_t cpu_get_sr(void)
{
    return (uint16_t)((R.srh << 8) | (R.x << 4) | (R.n << 3) | (R.z << 2) | (R.v << 1) | R.c);
}
static inline void cpu_set_ccr(uint32_t v)
{
    R.x = (v >> 4) & 1; R.n = (v >> 3) & 1; R.z = (v >> 2) & 1; R.v = (v >> 1) & 1; R.c = v & 1;
}
static inline void cpu_set_sr(uint32_t v)
{
    uint8_t nh = (uint8_t)((v >> 8) & 0xA7);
    if ((nh ^ R.srh) & 0x20) { uint32_t t = R.a[7]; R.a[7] = R.usp; R.usp = t; }
    R.srh = nh;
    cpu_set_ccr(v);
}

#define R8(a)  md_read8(a)
#define R16(a) md_read16(a)
#define R32(a) md_read32(a)
#define W8(a, v)  md_write8((a), (v))
#define W16(a, v) md_write16((a), (v))
#define W32(a, v) md_write32((a), (v))

static inline void PUSH32(uint32_t v) { R.a[7] -= 4; W32(R.a[7], v); }
static inline void PUSH16(uint32_t v) { R.a[7] -= 2; W16(R.a[7], v); }
static inline uint32_t POP32(void) { uint32_t v = R32(R.a[7]); R.a[7] += 4; return v; }
static inline uint32_t POP16(void) { uint32_t v = R16(R.a[7]); R.a[7] += 2; return v; }

/* condition tests, index = the 4-bit cc field */
static inline int cpu_cond(int cc)
{
    switch (cc & 15) {
    case 0: return 1;                 case 1: return 0;
    case 2: return !R.c && !R.z;      case 3: return R.c || R.z;
    case 4: return !R.c;              case 5: return R.c;
    case 6: return !R.z;              case 7: return R.z;
    case 8: return !R.v;              case 9: return R.v;
    case 10: return !R.n;             case 11: return R.n;
    case 12: return R.n == R.v;       case 13: return R.n != R.v;
    case 14: return !R.z && R.n == R.v;
    default: return R.z || R.n != R.v;
    }
}

/* ---- arithmetic with flags, one set per operand size ---- */
#define DEF_SIZE(S, MASK, MSB)                                                          \
static inline void NZ##S(uint32_t r) { R.n = (r & MSB) != 0; R.z = (r & MASK) == 0; }   \
static inline uint32_t LOGIC##S(uint32_t r) { r &= MASK; NZ##S(r); R.v = 0; R.c = 0; return r; } \
static inline uint32_t ADD##S(uint32_t s, uint32_t d) {                                 \
    s &= MASK; d &= MASK; uint32_t r = (d + s) & MASK;                                  \
    R.c = R.x = (((s & d) | (~r & (s | d))) & MSB) != 0;                                \
    R.v = (((s ^ r) & (d ^ r)) & MSB) != 0; NZ##S(r); return r; }                       \
static inline uint32_t SUB##S(uint32_t s, uint32_t d) {                                 \
    s &= MASK; d &= MASK; uint32_t r = (d - s) & MASK;                                  \
    R.c = R.x = (((s & ~d) | (r & ~d) | (s & r)) & MSB) != 0;                           \
    R.v = (((s ^ d) & (r ^ d)) & MSB) != 0; NZ##S(r); return r; }                       \
static inline void CMP##S(uint32_t s, uint32_t d) {                                     \
    s &= MASK; d &= MASK; uint32_t r = (d - s) & MASK;                                  \
    R.c = (((s & ~d) | (r & ~d) | (s & r)) & MSB) != 0;                                 \
    R.v = (((s ^ d) & (r ^ d)) & MSB) != 0; NZ##S(r); }                                 \
static inline uint32_t ADDX##S(uint32_t s, uint32_t d) {                                \
    s &= MASK; d &= MASK; uint32_t r = (d + s + R.x) & MASK;                            \
    R.c = R.x = (((s & d) | (~r & (s | d))) & MSB) != 0;                                \
    R.v = (((s ^ r) & (d ^ r)) & MSB) != 0; R.n = (r & MSB) != 0;                      \
    if (r) R.z = 0; return r; }                                                         \
static inline uint32_t SUBX##S(uint32_t s, uint32_t d) {                                \
    s &= MASK; d &= MASK; uint32_t r = (d - s - R.x) & MASK;                            \
    R.c = R.x = (((s & ~d) | (r & ~d) | (s & r)) & MSB) != 0;                           \
    R.v = (((s ^ d) & (r ^ d)) & MSB) != 0; R.n = (r & MSB) != 0;                      \
    if (r) R.z = 0; return r; }                                                         \
static inline uint32_t NEG##S(uint32_t d) { return SUB##S(d, 0); }                      \
static inline uint32_t NEGX##S(uint32_t d) { return SUBX##S(d, 0); }                    \
static inline uint32_t ASL##S(uint32_t v, int n) {                                      \
    v &= MASK; uint32_t c = 0; int vf = 0;                                              \
    for (int i = 0; i < n; i++) { c = (v & MSB) != 0; uint32_t nv = (v << 1) & MASK;    \
        if ((nv ^ v) & MSB) vf = 1; v = nv; }                                           \
    if (n) { R.c = R.x = (uint8_t)c; } else R.c = 0;                                    \
    R.v = (uint8_t)vf; NZ##S(v); return v; }                                            \
static inline uint32_t LSL##S(uint32_t v, int n) {                                      \
    v &= MASK; uint32_t c = 0;                                                          \
    for (int i = 0; i < n; i++) { c = (v & MSB) != 0; v = (v << 1) & MASK; }            \
    if (n) { R.c = R.x = (uint8_t)c; } else R.c = 0;                                    \
    R.v = 0; NZ##S(v); return v; }                                                      \
static inline uint32_t LSR##S(uint32_t v, int n) {                                      \
    v &= MASK; uint32_t c = 0;                                                          \
    for (int i = 0; i < n; i++) { c = v & 1; v >>= 1; }                                 \
    if (n) { R.c = R.x = (uint8_t)c; } else R.c = 0;                                    \
    R.v = 0; NZ##S(v); return v; }                                                      \
static inline uint32_t ASR##S(uint32_t v, int n) {                                      \
    v &= MASK; uint32_t c = 0;                                                          \
    for (int i = 0; i < n; i++) { c = v & 1; v = (v >> 1) | (v & MSB); }                \
    if (n) { R.c = R.x = (uint8_t)c; } else R.c = 0;                                    \
    R.v = 0; NZ##S(v); return v; }                                                      \
static inline uint32_t ROL##S(uint32_t v, int n) {                                      \
    v &= MASK; uint32_t c = 0;                                                          \
    for (int i = 0; i < n; i++) { c = (v & MSB) != 0; v = ((v << 1) | c) & MASK; }      \
    R.c = n ? (uint8_t)c : 0; R.v = 0; NZ##S(v); return v; }                            \
static inline uint32_t ROR##S(uint32_t v, int n) {                                      \
    v &= MASK; uint32_t c = 0;                                                          \
    for (int i = 0; i < n; i++) { c = v & 1; v = (v >> 1) | (c ? MSB : 0); }            \
    R.c = n ? (uint8_t)c : 0; R.v = 0; NZ##S(v); return v; }                            \
static inline uint32_t ROXL##S(uint32_t v, int n) {                                     \
    v &= MASK;                                                                          \
    for (int i = 0; i < n; i++) { uint32_t c = (v & MSB) != 0;                          \
        v = ((v << 1) | R.x) & MASK; R.x = (uint8_t)c; }                                \
    R.c = R.x; R.v = 0; NZ##S(v); return v; }                                           \
static inline uint32_t ROXR##S(uint32_t v, int n) {                                     \
    v &= MASK;                                                                          \
    for (int i = 0; i < n; i++) { uint32_t c = v & 1;                                   \
        v = (v >> 1) | (R.x ? MSB : 0); R.x = (uint8_t)c; }                             \
    R.c = R.x; R.v = 0; NZ##S(v); return v; }

DEF_SIZE(8, 0xFFu, 0x80u)
DEF_SIZE(16, 0xFFFFu, 0x8000u)
DEF_SIZE(32, 0xFFFFFFFFu, 0x80000000u)

#define SX8(v)  ((uint32_t)(int32_t)(int8_t)(v))
#define SX16(v) ((uint32_t)(int32_t)(int16_t)(v))

/* DIVU/DIVS: returns 0 on divide by zero (the caller raises the exception). */
static inline int DIVU(int dn, uint32_t src)
{
    src &= 0xFFFF;
    if (!src) { R.n = R.z = R.v = R.c = 0; return 0; }
    uint32_t q = R.d[dn] / src, rem = R.d[dn] % src;
    R.c = 0;
    if (q < 0x10000) {
        R.z = q == 0; R.n = (q & 0x8000) != 0; R.v = 0;
        R.d[dn] = (q & 0xFFFF) | (rem << 16);
    } else R.v = 1;   /* overflow: N and Z are left unchanged */
    return 1;
}
static inline int DIVS(int dn, uint32_t src)
{
    int32_t s = (int16_t)src;
    if (!s) { R.n = R.z = R.v = R.c = 0; return 0; }
    R.c = 0;
    if (R.d[dn] == 0x80000000u && s == -1) { R.z = 0; R.n = 0; R.v = 0; R.d[dn] = 0; return 1; }
    int32_t q = (int32_t)R.d[dn] / s, rem = (int32_t)R.d[dn] % s;
    if (q == (int16_t)q) {
        R.z = q == 0; R.n = (q & 0x8000) != 0; R.v = 0;
        R.d[dn] = ((uint32_t)q & 0xFFFF) | ((uint32_t)rem << 16);
    } else R.v = 1;   /* overflow: N and Z are left unchanged */
    return 1;
}

/* BCD helpers: Flamewing's hardware-verified formulas (they also define the
 * "undefined" V and the results for invalid BCD digits). */
static inline uint32_t ABCD8(uint32_t s, uint32_t d)
{
    s &= 0xFF; d &= 0xFF;
    uint32_t ss = s + d + R.x;
    uint32_t bc = ((s & d) | (~ss & (s | d))) & 0x88;
    uint32_t dc = (((ss + 0x66) ^ ss) & 0x110) >> 1;
    uint32_t corf = (bc | dc) - ((bc | dc) >> 2);
    uint32_t res = ss + corf;
    R.c = R.x = ((bc | (ss & ~res)) >> 7) & 1;
    R.v = ((~ss & res) >> 7) & 1;
    res &= 0xFF;
    R.n = res >> 7;
    if (res) R.z = 0;
    return res;
}
static inline uint32_t SBCD8(uint32_t s, uint32_t d)
{
    s &= 0xFF; d &= 0xFF;
    uint32_t dd = d - s - R.x;
    uint32_t bc = ((~d & s) | (dd & ~d) | (dd & s)) & 0x88;
    uint32_t corf = bc - (bc >> 2);
    uint32_t res = dd - corf;
    R.c = R.x = ((bc | (~dd & res)) >> 7) & 1;
    R.v = ((dd & ~res) >> 7) & 1;
    res &= 0xFF;
    R.n = res >> 7;
    if (res) R.z = 0;
    return res;
}

/* ---- execution environment ---- */
typedef void (*recomp_fn)(uint32_t entry);
extern recomp_fn (*cpu_lookup)(uint32_t pc);   /* recompiled code table (NULL: none) */

void m68k_reset(void);
int  m68k_step(void);                 /* interpret one instruction; returns cycles */
void m68k_exception(int vec, uint32_t pc_to_push);
void m68k_take_irq(int level);        /* push state and jump to the autovector */
extern const uint8_t *g_cyc;          /* approximate cycles per opcode word */
void m68k_init_tables(void);

#endif
