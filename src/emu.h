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
/* which layer drew each pixel of the frame (same layout): 0 backdrop, 1 plane B, 2 plane A, 3 sprite, 4 window */
const uint8_t *emu_layers(void);
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
int  emu_text_printing(void);      /* a message is being typed out (for faster text) */
void emu_set_sound_hold(int on);  /* 1: the Z80 and sound chips do not advance (speed-up frames after the first) */
struct tip_s;
struct shop_s;
const uint8_t *emu_work_ram(void);
/* the field map around the party at 1/4 size (w x h pixels) and the party at (*mx, *my);
 * 0 off the field, 1 the field under windows, 2 the field alone */
int emu_minimap(uint32_t *out, int w, int h, int *mx, int *my);
/* the world map explored so far (one pixel per 8x8 tile) while on a planet's map; 0 elsewhere */
int emu_world_image(uint32_t *out, int maxw, int maxh, int *w, int *h, int *px, int *py);   /* planet number + 1; px < 0: party elsewhere */
void emu_world_pick(int planet);   /* the planet the map screen shows, -1: where the party is */
int emu_world_known(int planet);   /* that planet's map has been seen */
int emu_world_towns(int world, int *place, int *tx, int *ty, int max);   /* towns on that planet (tiles), place = the game's place number */
uint8_t *emu_world_data(int *size);   /* to keep in a file */
void emu_set_fog(int on);              /* minimap shows only the blocks seen so far */
uint8_t *emu_fog_data(int *size);      /* the seen blocks of every map, to keep in a file */   /* the 64 KB work RAM ($FF0000) */
#include "bestiary.h"
int  emu_place_of_map(int map);         /* the town (place number) of a map, -1 if none */
int  emu_known_rom(void);              /* the ROM the helpers know (bestiary, shops, maps) */
int  emu_bestiary(int id, best_info_t *b);   /* monster id 0..BEST_MAX-1: data and counts, 0 if none */
int  emu_shop(struct shop_s *s);       /* shop buy list: the highlighted item compared with the party (shop.h) */
int  emu_skill_tip(struct tip_s *t);  /* technique / skill under the cursor of an open list (tips.h) */
uint32_t emu_cpu_pc(void);   /* last 68000 instruction started */      /* the last frame showed the walkable field map, no windows */

#endif
