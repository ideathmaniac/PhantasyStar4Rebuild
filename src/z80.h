/* Z80 interpreter (the Mega Drive's sound CPU). */
#ifndef PS4_Z80_H
#define PS4_Z80_H
#include <stdint.h>

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
    uint16_t ix, iy, sp, pc, wz;
    uint8_t i, r, iff1, iff2, im, halted;
    uint8_t ei_pending;    /* EI: interrupts accepted after the next instruction */
    uint8_t q, last_q;     /* flag-change tracking for SCF/CCF X/Y */
} z80_t;

/* memory and I/O callbacks, provided by the system */
uint8_t z80_read(uint16_t a);
void    z80_write(uint16_t a, uint8_t v);
uint8_t z80_in(uint16_t port);
void    z80_out(uint16_t port, uint8_t v);

void z80_reset(z80_t *z);
int  z80_step(z80_t *z);            /* one instruction; returns T-states */
int  z80_interrupt(z80_t *z);       /* maskable interrupt (data bus $FF); 0 if not accepted */

#endif
