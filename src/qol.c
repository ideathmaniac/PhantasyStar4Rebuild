/*
 * Phantasy Star IV conveniences, done on the game's RAM after every frame
 * (so they work the same with recompiled code and with the interpreter).
 *
 * Random encounters ($05797A): every step clears bit 0 of $FFECE5 and
 * counts $FFECE4 down; when it reaches 0 it is set back to 1 and the game
 * rolls (random & $1F, or & $7F) for a battle, so from then on every step
 * rolls. Keeping the counter at 2 or more stops the rolls. The countdown is
 * restarted (to 10) after every battle; tripling it spaces battles out.
 *
 * Battle rewards: enemies add their experience and meseta to $FFFF41CE and
 * $FFFF41D0 (words) as they fall; the reward routine ($002F24) shares the
 * experience among the party and adds the meseta when the battle ends.
 * Every increase seen during a battle is multiplied before that.
 */
#include "md.h"
#include "qol.h"

static int enc_mode, exp_mul = 1, mes_mul = 1;
static uint16_t last_exp, last_mes;

void qol_set(int encounters, int exp_mult, int meseta_mult)
{
    enc_mode = encounters;
    exp_mul = exp_mult < 1 ? 1 : exp_mult > 8 ? 8 : exp_mult;
    mes_mul = meseta_mult < 1 ? 1 : meseta_mult > 8 ? 8 : meseta_mult;
}

static uint16_t rd16(uint32_t a) { return (uint16_t)((g_ram[a & 0xFFFF] << 8) | g_ram[(a + 1) & 0xFFFF]); }
static void wr16(uint32_t a, uint16_t v) { g_ram[a & 0xFFFF] = (uint8_t)(v >> 8); g_ram[(a + 1) & 0xFFFF] = (uint8_t)v; }

void qol_resync(void) { last_exp = rd16(0x41CE); last_mes = rd16(0x41D0); }

static uint16_t scale(uint32_t addr, uint16_t *last, int mul)
{
    uint16_t v = rd16(addr);
    if (v < *last) *last = 0;                          /* cleared: a new battle */
    if (v > *last && mul > 1) {
        uint32_t n = *last + (uint32_t)(v - *last) * (uint32_t)mul;
        if (n > 0xFFFF) n = 0xFFFF;
        wr16(addr, (uint16_t)n);
        v = (uint16_t)n;
    }
    *last = v;
    return v;
}

void qol_after_frame(void)
{
    int mode = rd16(0xEF00);
    if (enc_mode && mode == 0x0C) {
        uint8_t c = g_ram[0xECE4];
        if (enc_mode == 2) { if (c < 2) g_ram[0xECE4] = 2; }
        else {
            /* the game restarts the countdown (10 steps) after each battle
             * and on new maps: make that pause three times as long */
            static uint8_t last_c;
            if (c > last_c && c < 0x55) { c = (uint8_t)(c * 3); g_ram[0xECE4] = c; }
            last_c = c;
        }
    }
    if (mode == 0x14) { scale(0x41CE, &last_exp, exp_mul); scale(0x41D0, &last_mes, mes_mul); }
    else qol_resync();
}

/* 1 while the game types out a message letter by letter: the 68k is inside
 * the per-letter wait of the text routine (return address $06A20C on the
 * stack, see $06A1F6: wait $FFF440 + 1 frames per letter) */
#include "cpu.h"
int qol_text_printing(void)
{
    uint32_t sp = R.a[7] & 0xFFFF;
    for (uint32_t a = sp; a < sp + 0x80 && a + 3 < 0x10000; a += 2)
        if (g_ram[a] == 0x00 && g_ram[a + 1] == 0x06 && g_ram[a + 2] == 0xA2 && g_ram[a + 3] == 0x0C) return 1;
    return 0;
}
