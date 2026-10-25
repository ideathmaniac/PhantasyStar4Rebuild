/* Execution: dispatcher shared by recompiled code and the interpreter. */
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include "rt.h"
#include "run.h"

int g_yield;
int g_trace;
uint64_t g_trace_from = ~0ull, g_trace_to;
static FILE *trace_fp;
void cpu_trace(uint32_t pc)
{
    if (g_frame < g_trace_from || g_frame > g_trace_to) return;
    if (!trace_fp) trace_fp = fopen(getenv("TRACE_FILE") ? getenv("TRACE_FILE") : "trace.txt", "w");
    fprintf(trace_fp, "%06X c=%llu sr=%04X d=%X,%X,%X,%X,%X,%X,%X,%X a=%X,%X,%X,%X,%X,%X,%X,%X\n", pc,
            (unsigned long long)g_cycles, cpu_get_sr(), R.d[0], R.d[1], R.d[2], R.d[3], R.d[4], R.d[5], R.d[6], R.d[7],
            R.a[0], R.a[1], R.a[2], R.a[3], R.a[4], R.a[5], R.a[6], R.a[7]);
}
int g_force_interp, g_log_interp;
uint64_t g_interp_insns;
uint32_t cpu_cur_pc;
static uint8_t seen_interp[0x400000 / 2];
static jmp_buf frame_jmp;
static int frame_active;

void run_reset(void)
{
    if (getenv("TRACE")) { unsigned long long a = 0, b = 0; sscanf(getenv("TRACE"), "%llu-%llu", &a, &b); g_trace_from = a; g_trace_to = b; g_trace = 1; }
    m68k_init_tables();
    md_reset();
    m68k_reset();
    g_yield = 0;
}

void cpu_yield(uint32_t pc)
{
    R.pc = pc;
    g_yield = 0;
    if (frame_active) longjmp(frame_jmp, 1);
}

/* One instruction by the interpreter, with the same steps as I()/ACC(). */
static void interp_one(void)
{
    uint32_t pc = R.pc;
    cpu_cur_pc = pc;
    if (g_yield) cpu_yield(pc);
    int lvl = md_irq_level();
    if (lvl > (R.srh & 7)) { m68k_take_irq(lvl); return; }
    if (R.stopped) {                            /* STOP: idle until the next line event */
        g_cycles = g_line_end;
        while (g_cycles >= g_line_end) if (md_end_line()) g_yield = 1;
        return;
    }
    if (g_log_interp && pc < 0x400000) seen_interp[pc >> 1] = 1;
    if (g_trace) cpu_trace(pc);
    uint16_t op = (uint16_t)R16(pc);
    m68k_step();
    g_interp_insns++;
    ACC(op);
}

void cpu_interp_exec(void)
{
    uint16_t op = (uint16_t)R16(R.pc);
    /* the I() prologue already traced this instruction */
    if (g_log_interp && R.pc < 0x400000) seen_interp[R.pc >> 1] = 1;
    m68k_step();
    g_interp_insns++;
    ACC(op);
}

static inline void step_any(void)
{
    recomp_fn fn = (!g_force_interp && cpu_lookup) ? cpu_lookup(R.pc) : 0;
    if (fn) fn(R.pc); else interp_one();
}

int cpu_run_until(uint32_t ret, uint32_t sp0)
{
    for (;;) {
        if (R.pc == ret && R.a[7] == sp0) return 0;
        if (R.a[7] > sp0) return 1;
        step_any();
    }
}

int cpu_irq_run(uint32_t pc)
{
    uint32_t sp0 = R.a[7];
    R.pc = pc;                                  /* recompiled code does not keep R.pc current */
    m68k_take_irq(md_irq_level());
    if (cpu_run_until(pc, sp0)) return 1;
    return 0;
}

int cpu_trap_run(int vec, uint32_t next)
{
    uint32_t sp0 = R.a[7];
    m68k_exception(vec, next);
    /* the stack pointer before the exception may be the other stack if we
     * were in user mode; the game runs in supervisor mode, so sp0 holds */
    return cpu_run_until(next, sp0);
}

void run_frame(void)
{
    if (setjmp(frame_jmp)) { frame_active = 0; return; }
    frame_active = 1;
    for (;;) step_any();
}

void run_dump_interp_pcs(const char *path)
{
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    for (uint32_t i = 0; i < sizeof seen_interp; i++) if (seen_interp[i]) fprintf(fp, "%06X\n", i * 2);
    fclose(fp);
}
