/* The game as a library: what a front end (Windows, headless) needs. */
#ifndef PS4_EMU_H
#define PS4_EMU_H
#include <stdint.h>
#include <stddef.h>

#define EMU_FPS_NUM 53693175.0           /* NTSC master clock */
#define EMU_FPS (EMU_FPS_NUM / (3420.0 * 262.0))   /* 59.92 Hz */

enum { EMU_ROM_BAD = 0, EMU_ROM_RECOMPILED = 1, EMU_ROM_INTERPRETED = 2 };

/* Load a ROM image. Returns EMU_ROM_RECOMPILED for the exact dump the code
 * was generated from (Phantasy Star IV J + Russian translation fix v7),
 * EMU_ROM_INTERPRETED for any other Mega Drive ROM (it runs on the
 * interpreter instead), EMU_ROM_BAD if it is not a Mega Drive ROM. */
int  emu_load_rom(const uint8_t *data, size_t size);
void emu_reset(void);
void emu_set_pad(int port, uint16_t buttons);  /* PAD_* bits from md.h */
void emu_run_frame(void);
const uint32_t *emu_framebuffer(int *w, int *h); /* 0x00RRGGBB, pitch 320 pixels */
int  emu_audio(int16_t *out, int max_frames);   /* 48 kHz stereo */

size_t emu_state_size(void);
void   emu_state_save(uint8_t *out);
int    emu_state_load(const uint8_t *in, size_t n);

uint8_t *emu_sram(size_t *size);
int      emu_sram_dirty(void);                  /* returns and clears the flag */
void     emu_sha1_hex(const uint8_t *data, size_t n, char out[41]);
extern const char EMU_ROM_SHA1[];

/* Widescreen: extra pixels on each side of the 320-pixel field view
 * (0 = 4:3; 32 = 16:10; 53 = 16:9; at most 64). Only for the recompiled ROM.
 * The framebuffer is wider (emu_framebuffer's w) only on frames that show
 * the field map; battles and cutscenes stay 320 wide. */
void emu_set_widescreen(int ext);
int  emu_in_battle(void);     /* Phantasy Star IV game mode $FFEC20 == $14 */
int  emu_on_field(void);      /* the last frame showed the walkable field map, no windows */

#endif
