/*
 * Software drawing for the launcher screen, on 32-bit pixel buffers
 * (0x00RRGGBB, as Windows DIB sections and the emulator's framebuffer use).
 * Plain C, so tests/ui_test.c can render it on Linux; text is drawn by the
 * front end with GDI on top.
 */
#ifndef PS4_UI_SOFT_H
#define PS4_UI_SOFT_H
#include <stdint.h>

typedef struct { uint32_t *px; int w, h, pitch; } ui_img;

/* translucent rounded rectangle (alpha 0-255), antialiased corners */
void ui_panel(ui_img *d, int x, int y, int w, int h, int radius, uint32_t rgb, int alpha);
/* horizontal gradient from rgb at alpha a0 (left) to alpha a1 (right) */
void ui_hgrad(ui_img *d, int x, int y, int w, int h, uint32_t rgb, int a0, int a1);
/* a picture with per-pixel alpha (rgba: R G B A bytes), scaled by an integer factor */
void ui_blit_rgba(ui_img *d, int x, int y, const uint8_t *rgba, int sw, int sh, int scale);
/* the same, with a soft glow of the given colour around opaque pixels */
void ui_glow_rgba(ui_img *d, int x, int y, const uint8_t *rgba, int sw, int sh, int scale, uint32_t rgb, int strength);
/* all pixels times f/256 (fades) */
void ui_darken(ui_img *d, int f);

/* the launcher background: the title screen blurred, dimmed, with darker
 * edges, as a small picture (bw x bh) to be stretched smoothly */
void ui_backdrop(const uint32_t *fb, int fw, int fh, int fpitch, uint32_t *out, int bw, int bh);
/* the game logo keyed out of the title screen (gold letters); returns a
 * malloc'ed RGBA picture or 0 if the title does not look as expected */
uint8_t *ui_logo(const uint32_t *fb, int fw, int fh, int fpitch, int *lw, int *lh);

/* Phantasy Star IV character names from their RAM record (the game's own
 * text encoding) as UTF-16 */
void ui_ps4_name(const uint8_t *rec, wchar_t *out, int n);

#endif
