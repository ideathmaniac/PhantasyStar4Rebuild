#ifndef PS4_RUN_H
#define PS4_RUN_H
#include <stdint.h>
extern int g_force_interp, g_log_interp;
extern uint64_t g_interp_insns;
void run_reset(void);
void run_frame(void);
void run_dump_interp_pcs(const char *path);
#endif
