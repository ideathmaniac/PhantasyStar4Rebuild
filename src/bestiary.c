/*
 * Bestiary for Phantasy Star IV (known ROM only): counts the monsters met
 * and defeated, and reads their data from the game's own tables.
 *
 * Found in the battle setup code ($007E30):
 *   $FF41F7 + 2 * n   ids of this battle's monsters (FF ends the list)
 *   $FF4200 + n * $80 their battle records: name (14 bytes), HP.w at +$0E
 *                     (signed; below 1 = defeated), id.w at +$68
 *   ROM $2815D4 + id * 48: HP.w +0, strength +4, mental +5, agility +6,
 *                     dexterity +7, attack.w +8, defence +10, experience.w
 *                     +44, meseta.w +46
 *   ROM $2F5B30       names, ended by FE or FF, in id order
 */
#include <string.h>
#include "md.h"
#include "bestiary.h"

#define ENEMIES 0x2815D4
#define ENAMES 0x2F5B30

static uint32_t seen[BEST_MAX], killed[BEST_MAX];
static uint8_t state[8];   /* per battle slot: 0 not yet counted, 1 counted alive, 2 counted defeated */

static uint16_t rw(uint32_t a) { return (uint16_t)(g_ram[a & 0xFFFF] << 8 | g_ram[(a + 1) & 0xFFFF]); }

void bestiary_after_frame(void)
{
    if (rw(0xEF00) != 0x14) { memset(state, 0, sizeof state); return; }
    for (int k = 0; k < 8; k++) {
        int id = g_ram[0x41F7 + 2 * k];
        if (id == 0xFF) break;
        uint32_t rec = 0x4200 + k * 0x80;
        if (id >= BEST_MAX || rw(rec + 0x68) != id) continue;   /* record not set up yet */
        int hp = (int16_t)rw(rec + 0x0E);
        if (state[k] == 0 && hp > 0) { state[k] = 1; seen[id]++; }
        else if (state[k] == 1 && hp <= 0) { state[k] = 2; killed[id]++; }
    }
}

void bestiary_get(int id, uint32_t *s, uint32_t *k) { *s = id >= 0 && id < BEST_MAX ? seen[id] : 0; *k = id >= 0 && id < BEST_MAX ? killed[id] : 0; }
void bestiary_set(int id, uint32_t s, uint32_t k) { if (id >= 0 && id < BEST_MAX) { seen[id] = s; killed[id] = k; } }

/* the game's text -> UTF-8 (capitals, small letters, digits $9A-$A3, space) */
static int put(char *o, int k, int n, const char *s)
{
    int l = (int)strlen(s);
    if (k + l < n) { memcpy(o + k, s, (size_t)l); k += l; }
    return k;
}
static int name_utf8(uint32_t a, char *out, int n)
{
    static const char *caps[25] = { 0, "А", "Б", "В", "Г", "Д", "Е", "Ж", "З", "И", "К", "Л", "М", "Н", "О", "П",
                                    "Р", "С", "Т", "У", "Ч", "Х", "Ш", "Щ", "Э" };
    static const char *lower[25] = { 0, "а", "б", "в", "г", "д", "е", "ж", "з", "и", "к", "л", "м", "н", "о", "п",
                                     "р", "с", "т", "у", "ч", "х", "ш", "щ", "э" };
    int k = 0, ok = 1, first = 1;
    for (int i = 0; i < 24 && a + i < g_rom_size; i++) {
        uint8_t c = g_rom[a + i];
        if (c >= 0xFE) break;
        const char *s = 0;
        if (c == 0xF1 && g_rom[a + i + 1] == 0x09) { s = first ? "Й" : "й"; i++; }
        else if (c == 0xF0) { s = first ? "Ё" : "ё"; i++; }
        else if (c >= 1 && c < 25) s = first ? caps[c] : lower[c];
        else if (c >= 0x9A && c <= 0xA3) { static const char *dg[10] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" }; s = dg[c - 0x9A]; }
        else switch (c) {
            case 0x00: s = " "; break;
            case 0x19: s = first ? "Ь" : "ь"; break; case 0x1A: s = first ? "Я" : "я"; break; case 0x1B: s = "-"; break;
            case 0x45: s = first ? "Ф" : "ф"; break; case 0x46: s = first ? "Ц" : "ц"; break; case 0x47: s = first ? "Ю" : "ю"; break;
            case 0x48: s = first ? "Ы" : "ы"; break; case 0x4D: s = first ? "Ж" : "ж"; break;
            default: s = "?"; ok = 0; break;
        }
        k = put(out, k, n, s);
        first = 0;   /* only the very first letter stays capital */
    }
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
    return ok && k > 0;
}

int bestiary_info(int id, best_info_t *b)
{
    if (id < 0 || id >= BEST_MAX) return 0;
    memset(b, 0, sizeof *b);
    /* the name: skip id entries, as the game does ($2807AA) */
    uint32_t a = ENAMES;
    for (int i = 0; i < id && a < g_rom_size; i++) { while (a < g_rom_size && g_rom[a] < 0xFE) a++; a++; }
    if (!name_utf8(a, b->name, sizeof b->name)) return 0;
    char *z = strstr(b->name, " зио");   /* the one proper name inside other names */
    if (z) memcpy(z + 1, "З", 2);
    const uint8_t *e = &g_rom[ENEMIES + id * 48];
    b->hp = e[0] << 8 | e[1];
    if (!b->hp) return 0;
    b->strength = e[4]; b->mental = e[5]; b->agility = e[6]; b->dexterity = e[7];
    b->attack = e[8] << 8 | e[9]; b->defence = e[10];
    b->exp = e[44] << 8 | e[45]; b->meseta = e[46] << 8 | e[47];
    bestiary_get(id, &b->seen, &b->killed);
    return 1;
}

int bestiary_battle(int *ids, int *hp, int *hpmax, int max)
{
    if (rw(0xEF00) != 0x14) return 0;
    int n = 0;
    for (int k = 0; k < 8 && n < max; k++) {
        int id = g_ram[0x41F7 + 2 * k];
        if (id == 0xFF) break;
        uint32_t rec = 0x4200 + k * 0x80;
        if (id >= BEST_MAX || rw(rec + 0x68) != id) continue;
        ids[n] = id; hp[n] = (int16_t)rw(rec + 0x0E); hpmax[n] = (int16_t)rw(rec + 0x10);
        n++;
    }
    return n;
}
