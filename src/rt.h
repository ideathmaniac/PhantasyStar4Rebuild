/*
 * Runtime contract between the recompiled code (generated/ps4_*.c) and the
 * interpreter. Per instruction both sides do exactly:
 *   1. frame boundary?  -> yield to the host (cpu_yield)
 *   2. interrupt pending above the mask? -> take it and run the handler
 *   3. execute the instruction
 *   4. ACC: add its cycles, finish any lines that ended (render, IRQs)
 * so a run with recompiled code is bit-identical to a pure interpreter run.
 */
#ifndef PS4_RT_H
#define PS4_RT_H
#include "cpu.h"

extern int g_yield;
void cpu_yield(uint32_t pc);                     /* longjmp back to run_frame */
int  cpu_irq_run(uint32_t pc);                   /* 1: handler did not return to pc */
int  cpu_run_until(uint32_t ret, uint32_t sp0);  /* 1: stack unwound past sp0 */
int  cpu_trap_run(int vec, uint32_t next);        /* exception that returns (TRAP) */
void cpu_interp_exec(void);                      /* one interpreted instruction at R.pc, with ACC */
extern uint32_t cpu_cur_pc;

#define ACC(op) do { g_cycles += g_cyc[op]; while (g_cycles >= g_line_end) if (md_end_line()) g_yield = 1; } while (0)
extern int g_trace;
void cpu_trace(uint32_t pc);
#define I(pc) do { cpu_cur_pc = (pc); \
    for (;;) { if (g_yield) cpu_yield(pc); \
        if (md_irq_level() > (R.srh & 7)) { if (cpu_irq_run(pc)) return; continue; } break; } \
    if (g_trace) cpu_trace(pc); } while (0)

/* calls: after PUSH32(ret), returns from the C function if the callee left
 * the 68000 somewhere else (the dispatcher above us takes it from there) */
#define CALL_KNOWN(fn, target, ret) do { uint32_t sp0_ = R.a[7] + 4; R.pc = (target); fn(target); \
    if (R.pc != (ret) || R.a[7] != sp0_) { if (cpu_run_until((ret), sp0_)) return; } } while (0)
#define CALL_ANY(target, ret) do { uint32_t sp0_ = R.a[7] + 4; R.pc = (target) & 0xFFFFFF; \
    if (cpu_run_until((ret), sp0_)) return; } while (0)

#endif
