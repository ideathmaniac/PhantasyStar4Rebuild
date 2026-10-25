#ifndef PS4_REWIND_H
#define PS4_REWIND_H
#include <stddef.h>
void rw_init(size_t bytes);   /* call after the game is running */
void rw_clear(void);
void rw_push(void);           /* record the current state */
int  rw_pop(void);            /* go back one record; 0 if none */
int  rw_count(void);
#endif
