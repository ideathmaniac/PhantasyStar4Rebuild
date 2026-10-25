/*
 * HD packs: replacement graphics for the game's 8x8 tiles.
 *
 * Folder layout (relative to the pack root, e.g. hdpack\ next to PS4.exe):
 *   scenes\    captures of the screen layers: sNNNN_a/_b/_w/_s.png (plane A,
 *              plane B, window, sprite sheet) and a .map beside each, telling
 *              which tile (pattern + palette, flips) sits in every 8x8 cell
 *   upscaled\  the same pictures enlarged 2-8x by the user (an AI upscaler or
 *              by hand), same file names (anything may follow the name)
 *   tiles\     the pack: one PNG per tile, <pattern>_<palette>.png, made by
 *              hd_import() from upscaled\, plus index.txt (palettes) and
 *              pack.txt (scale)
 * At run time every screen pixel knows which tile pixel it shows (vdp.c,
 * g_src); hd_compose() draws the frame at the pack scale from the HD tiles,
 * the original pixels wherever a tile has no replacement. A tile shown with
 * other colours than it was captured with (palette fades, recoloured
 * enemies) is recoloured from the captured palette.
 */
#ifndef PS4_HDPACK_H
#define PS4_HDPACK_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    /* paths are relative to the pack root, with '/' separators */
    int (*write)(const char *rel, const void *data, size_t n);        /* creates folders as needed */
    uint8_t *(*read)(const char *rel, size_t *n);                      /* malloc'ed, 0 if missing */
    void (*list)(const char *reldir, void (*cb)(const char *name, void *u), void *u);  /* file names */
    void (*log)(const char *msg);
} hd_io_t;

void hd_set_io(const hd_io_t *io);
int  hd_load(void);                 /* (re)loads tiles\ ; returns the number of tiles */
int  hd_tiles(void);
int  hd_scale(void);                /* 0 while the pack is empty */

/* draws the last frame at the pack scale; out must hold (W*S) x (224*S)
 * pixels with the given pitch; returns 0 (nothing drawn) if the pack is empty */
int  hd_compose(uint32_t *out, int pitch, int *ow, int *oh);
float hd_coverage(void);            /* share of the last composed frame's tile pixels that were replaced */

int  hd_capture(void);              /* writes a scene now, returns its number (0 on failure) */
void hd_auto_capture(void);         /* call once per frame (with g_hd_track on): captures new graphics */
int  hd_import(char *report, size_t n);  /* builds tiles\ from upscaled\ ; returns tiles added */

#endif
