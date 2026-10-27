#ifndef PS4_PORTRAITS_H
#define PS4_PORTRAITS_H
#include <stdint.h>
#define PORTRAIT_W 48
/* the 48x48 portrait (0xAARRGGBB) of character record rec (0 Chaz .. 10 Seth),
 * decoded from the loaded ROM; 0 if the ROM has no such table */
const uint32_t *portrait_pixels(int rec);
void portraits_reset(void);   /* after loading another ROM */
#endif
