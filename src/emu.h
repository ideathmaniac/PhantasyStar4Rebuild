/* The game as a library: what a front end (Windows, headless) needs. */
#ifndef PS4_EMU_H
#define PS4_EMU_H
#include <stdint.h>
#include <stddef.h>

#define EMU_FPS_NUM 53693175.0           /* NTSC master clock */
#define EMU_FPS (EMU_FPS_NUM / (3420.0 * 262.0))   /* 59.92 Hz */

enum { EMU_ROM_BAD = 0, EMU_ROM_RECOMPILED = 1 /* the known ROM */, EMU_ROM_INTERPRETED = 2 /* any other ROM */ };
int  emu_has_recompiled_code(void);   /* 0 in the build that ships no game code */

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
const uint8_t *emu_state_ram(const uint8_t *st, size_t n);   /* the 64 KB work RAM inside a state, or 0 */

uint8_t *emu_sram(size_t *size);
int      emu_sram_dirty(void);                  /* returns and clears the flag */
void     emu_sha1_hex(const uint8_t *data, size_t n, char out[41]);
extern const char EMU_ROM_SHA1[];

/* Widescreen: extra pixels on each side of the 320-pixel field view
 * (0 = 4:3; 32 = 16:10; 53 = 16:9; at most 64). Only for the recompiled ROM.
 * The framebuffer is wider (emu_framebuffer's w) on frames that show the
 * field map or a battle; cutscenes stay 320 wide. */
void emu_set_widescreen(int ext);
void emu_set_widescreen_battles(int on);   /* battles too (background mirrored at the sides) */
int  emu_in_battle(void);     /* Phantasy Star IV top-level mode $FFEF00 == $14 */
int  emu_on_field(void);
int  emu_game_mode(void);
void emu_set_audio_tone(int level);
/* encounters: 0 normal, 1 half as often, 2 none; experience / meseta from battles x1..x8 */
void emu_set_conveniences(int encounters, int exp_mult, int meseta_mult);
int  emu_game_mode_top(void);         /* $FFEF00: $0C field, $14 battle */
uint32_t emu_meseta(void);            /* money, $FFF438 */
void emu_set_cutscene_detect(int on); /* run the field check every frame, also at 4:3 */
int  emu_is_cutscene(void);           /* last frame: neither the field nor a battle (story pictures, title) */  /* 0 clean, 1 softer, 2 like an old TV */     /* $FFEC20 */
struct tip_s;
int  emu_skill_tip(struct tip_s *t);  /* technique / skill under the cursor of an open list (tips.h) */
uint32_t emu_cpu_pc(void);   /* last 68000 instruction started */      /* the last frame showed the walkable field map, no windows */

#endif
