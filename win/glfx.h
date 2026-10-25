/*
 * GPU picture filters (OpenGL 2.0 shaders), shared by the Windows front end
 * and the Linux test harness (tests/glfx_test.c).
 */
#ifndef PS4_GLFX_H
#define PS4_GLFX_H
#include <stdint.h>

enum { FX_PIXELS = 0, FX_SMOOTH = 1, FX_CRT = 2, FX_CRT_CURVED = 3, FX_COUNT };

typedef void *(*glfx_getproc_t)(const char *);

/* Compiles the shaders; 0 if the driver lacks OpenGL 2.0 / framebuffer
 * objects (the caller then keeps its old drawing). Needs a current context. */
int  glfx_init(glfx_getproc_t getproc);
const char *glfx_error(void);

/* Draws the frame (0x00RRGGBB, pitch in pixels) into the destination
 * rectangle (x, y, w, h) of a cw x ch framebuffer, top-left origin.
 * bilinear: soft scaling in FX_PIXELS mode. dither: blend Mega Drive
 * dithering stripes. Leaves GL state as the fixed-function path expects:
 * program 0, texture unit 0, default framebuffer, orthographic matrices. */
void glfx_draw(const uint32_t *fb, int W, int H, int pitch, int x, int y, int w, int h, int cw, int ch,
               int mode, int bilinear, int dither, int backdrop);
/* backdrop: fill the bars beside the picture with a blurred copy of it.
 * glfx_cover: the share of the picture (fx, fy) that covers a cw x ch window */
void glfx_cover(int w, int h, int cw, int ch, float *fx, float *fy);

#endif
