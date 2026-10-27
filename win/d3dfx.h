/* Direct3D 11 picture output (see d3dfx.c). */
#ifndef PS4_D3DFX_H
#define PS4_D3DFX_H
#include <stdint.h>

int  d3d_init(void *hwnd);                 /* 0: not available, see d3d_error() */
const char *d3d_error(void);
const char *d3d_info(void);                /* feature level and swap model, for the log */
/* the frame into the destination rectangle of a cw x ch window (clears the rest) */
void d3d_frame(const uint32_t *fb, int W, int H, int pitch, int x, int y, int w, int h, int cw, int ch,
               int mode, int bilinear, int dither, int backdrop);
/* copies a box of a 32-bit picture the size of the window (menu, messages) on top */
void d3d_overlay(const uint32_t *bits, int bw, int bh, int rx, int ry, int rw, int rh);
int  d3d_present(int vsync);
void d3d_set_effects(float shadows, float bloom, int look);   /* see glfx_set_effects */               /* 0 when the device was lost */

#endif
