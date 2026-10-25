/* Crash and hang reports for the Windows build (see diag.c). */
#ifndef PS4_DIAG_H
#define PS4_DIAG_H
#include <stdint.h>

void diag_init(const wchar_t *dir, const char *version);
void diag_set_window(void *hwnd);
void diag_thread_init(void);                    /* call first in every thread we create */
void diag_heartbeat(uint64_t frame);            /* main loop, every iteration */
void diag_pause(int on);                        /* around modal dialogs: no hang reports */
void diag_note(const char *line);               /* remembered for the report (last 32 lines) */
extern const char *(*diag_info)(void);          /* settings summary for the report */

#endif
