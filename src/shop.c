/*
 * Shop helper for Phantasy Star IV (known ROM only): while the buy list of a
 * shop is open, describe the highlighted item and compare it with what each
 * party member wears now.
 *
 * Found in the game's code (shop routine at $06518A):
 *   menu windows   $FFEC90 = id of the top window, $FFEC94 = the current
 *                  window table (8 bytes per id: ?, ?, x, y, handler.l);
 *                  the buy list is the window whose handler is $06518A
 *   shop list      $FFE3F0.l = list of item ids in ROM, $FFE3F4 = count,
 *                  cursor $FFECA0
 *   items          ROM $2AA288 + (id - 1) * 22: mask.w at +8 (bit = character
 *                  record), type at +10 (1-4 weapons, 3/4 two-handed,
 *                  5 shield, 6 head, 7 body), attack +15, defence +16,
 *                  price.w +20; full names at ROM $2FE500 (FE separated)
 *   characters     records $FFF500 + n * $80: name at +0, equipment at
 *                  +$4C..$4F (right hand, left hand, head, body);
 *                  party $FFF40A (FF ends), inventory $FFF410 (40 ids),
 *                  money $FFF438.l
 */
#include <string.h>
#include <stdio.h>
#include "md.h"
#include "shop.h"

#define ITEMS 0x2AA288
#define INAMES 0x2FE500
#define NITEMS 160
#define BUY_HANDLER 0x06518A

static uint16_t rw(uint32_t a) { return (uint16_t)(g_ram[a & 0xFFFF] << 8 | g_ram[(a + 1) & 0xFFFF]); }
static uint32_t rl(uint32_t a) { return (uint32_t)rw(a) << 16 | rw(a + 2); }
static uint32_t roml(uint32_t a) { return a + 3 < g_rom_size ? (uint32_t)g_rom[a] << 24 | g_rom[a + 1] << 16 | g_rom[a + 2] << 8 | g_rom[a + 3] : 0; }

/* the game's text -> UTF-16; capitals with an alternative glyph included */
static int glyph(uint8_t c)
{
    static const uint16_t caps[25] = { 0, 0x410, 0x411, 0x412, 0x413, 0x414, 0x415, 0x416, 0x417, 0x418, 0x41A, 0x41B,
                                       0x41C, 0x41D, 0x41E, 0x41F, 0x420, 0x421, 0x422, 0x423, 0x427, 0x425, 0x428, 0x429, 0x42D };
    static const uint16_t alt[0x63 - 0x4E] = {   /* $4E-$62: first letters drawn wider */
        0x411, 0x413, 0x415, 0x41A, 0x41B, 0x41C, 0x41E, 0x41F, 0x428, 0x421, 0x422, 0x42D, 0x426, 0x417, 0x41A,
        0x41B, 0x41E, 0x41F, 0x421, 0x41A, 0x41F };
    if (c >= 0x1F && c < 0x1F + 31) {
        static const uint16_t sm[31] = { 0x430, 0x431, 0x432, 0x433, 0x434, 0x435, 0x436, 0x437, 0x438, 0x43A, 0x43B, 0x43C,
                                         0x43D, 0x43E, 0x43F, 0x440, 0x441, 0x442, 0x443, 0x444, 0x445, 0x446, 0x447, 0x448,
                                         0x449, 0x44A, 0x44B, 0x44C, 0x44D, 0x44E, 0x44F };
        return sm[c - 0x1F];
    }
    if (c >= 1 && c < 25) return caps[c];
    if (c >= 0x4E && c <= 0x62) return alt[c - 0x4E];
    switch (c) {
    case 0x00: return ' '; case 0x19: return 0x42C; case 0x1A: return 0x42F; case 0x1B: return '-';
    case 0x45: return 0x424; case 0x46: return 0x426; case 0x47: return 0x42E; case 0x48: return 0x42B;
    case 0x4D: return 0x416;
    }
    return '?';
}

static int utf8(char *o, int k, int n, int cp)
{
    if (cp < 0x80) { if (k + 1 < n) o[k++] = (char)cp; }
    else if (k + 2 < n) { o[k++] = (char)(0xC0 | cp >> 6); o[k++] = (char)(0x80 | (cp & 0x3F)); }
    return k;
}

/* text at p (ROM or RAM) up to FE/FF -> UTF-8; title: "Лёгкий шлем" instead of "ЛЁГКИЙ ШЛЕМ" */
static void decode(const uint8_t *p, int max, char *out, int n, int title)
{
    int k = 0, first = 1;
    for (int i = 0; i < max; i++) {
        uint8_t c = p[i];
        if (c == 0xFE || c == 0xFF) break;
        int cp;
        if (c == 0xF0 && i + 1 < max) { cp = 0x401; i++; }                         /* Ё (mark + Е) */
        else if (c == 0xF1 && i + 1 < max && p[i + 1] == 0x09) { cp = 0x419; i++; } /* Й */
        else cp = glyph(c);
        if (title) {
            if (!first && cp >= 0x410 && cp <= 0x42F) cp += 0x20;
            else if (!first && cp == 0x401) cp = 0x451;
            first = cp == ' ' && k == 0;
        }
        k = utf8(out, k, n, cp);
    }
    out[k] = 0;
    /* trailing spaces */
    while (k > 0 && out[k - 1] == ' ') out[--k] = 0;
}

static const uint8_t *item(int id) { return &g_rom[ITEMS + (id - 1) * 22]; }

static void item_name(int id, char *out, int n)
{
    static uint32_t addr[NITEMS + 1];
    static int done;
    if (!done) {
        done = 1;
        uint32_t a = INAMES;
        for (int i = 1; i <= NITEMS && a < g_rom_size; i++) {
            addr[i] = a;
            while (a < g_rom_size && g_rom[a] != 0xFE) a++;
            a++;
        }
    }
    out[0] = 0;
    if (id < 1 || id > NITEMS) return;
    decode(&g_rom[addr[id]], 40, out, n, 1);
}

static int describe(shop_t *s, int id, int priced);
static const char *type_name(int t)
{
    switch (t) {
    case 1: case 2: return "оружие (одна рука)";
    case 3: case 4: return "оружие (две руки)";
    case 5: return "щит";
    case 6: return "головной убор";
    case 7: return "одежда, броня";
    }
    return "предмет";
}

int shop_current(shop_t *s)
{
    if (rw(0xEF00) != 0x0C) return 0;
    uint32_t table = rl(0xEC94);
    uint32_t e = table + (uint32_t)rw(0xEC90) * 8;
    if (table >= g_rom_size || roml(e + 4) != BUY_HANDLER) return 0;
    uint32_t list = rl(0xE3F0);
    int count = rw(0xE3F4), cur = g_ram[0xECA0];
    if (list >= g_rom_size || count < 1 || count > 16 || cur >= count) return 0;
    int id = g_rom[list + cur];
    if (id < 1 || id > NITEMS) return 0;
    return describe(s, id, 1);
}

/* consumables and other items that are not worn (after their effect codes,
 * which are the techniques' codes: $12 heal, $13 poison, $14 paralysis,
 * $15/$16 revive, $19/$1A teleport) */
static const char *use_text(int id)
{
    switch (id) {
    case 125: return "Немного лечит одного героя (кроме андроидов).";
    case 126: return "Лечит одного героя сильнее Монолеча.";
    case 127: return "Сильно лечит одного героя.";
    case 128: return "Излечивает героя от яда.";
    case 129: return "Снимает с героя паралич.";
    case 130: return "Возвращает к жизни павшего героя.";
    case 131: return "Лечит весь отряд.";
    case 132: return "Переносит отряд в уже посещённый город.";
    case 133: return "Выводит отряд из подземелья.";
    case 134: return "Оживляет героя или лечит яд, сон и паралич.";
    case 139: return "Взрывчатка: урон врагам в бою.";
    case 144: return "Ремонтный набор для андроидов.";
    case 145: case 147: return "Выпечка: нужна для побочного задания.";
    }
    return 0;
}

static int describe(shop_t *s, int id, int priced)
{
    memset(s, 0, sizeof *s);
    const uint8_t *it = item(id);
    s->id = id;
    item_name(id, s->name, sizeof s->name);
    s->type = it[10];
    snprintf(s->kind, sizeof s->kind, "%s", type_name(s->type));
    s->atk = it[15]; s->def = it[16];
    s->price = priced ? it[20] << 8 | it[21] : -1;
    s->money = rl(0xF438);
    for (int i = 0; i < 40; i++) if (g_ram[0xF410 + i] == id) s->owned++;
    if (s->type < 1 || s->type > 7) {   /* not equipment: what it does instead of a comparison */
        const char *u = use_text(id);
        if (u) snprintf(s->use, sizeof s->use, "%s", u);
        else if (s->type == 9 || s->type == 10) snprintf(s->use, sizeof s->use, "%s", "Сюжетный предмет.");
        snprintf(s->kind, sizeof s->kind, "%s", s->type == 8 ? "расходуемый предмет" : "особый предмет");
        return 1;
    }

    int mask = it[8] << 8 | it[9];
    for (int p = 0; p < 5; p++) {
        int rec = g_ram[0xF40A + p];
        if (rec == 0xFF || rec > 15) break;
        shop_row_t *r = &s->row[s->nrow++];
        const uint8_t *c = &g_ram[0xF500 + rec * 0x80];
        decode(c, 8, r->who, sizeof r->who, 0);
        r->can = (mask >> rec) & 1;
        if (!r->can) continue;
        /* what the new item would replace */
        const uint8_t *eq = c + 0x4C;   /* right hand, left hand, head, body */
        int old[2] = { 0, 0 }, nold = 0;
        if (s->type == 6) { if (eq[2]) old[nold++] = eq[2]; }
        else if (s->type == 7) { if (eq[3]) old[nold++] = eq[3]; }
        else if (s->type == 5) {   /* a shield replaces a shield, a two-handed weapon, or the weaker of two weapons */
            for (int h = 0; h < 2; h++) if (eq[h] && item(eq[h])[10] == 5) old[nold++] = eq[h];
            if (!nold) for (int h = 0; h < 2; h++) if (eq[h] && (item(eq[h])[10] == 3 || item(eq[h])[10] == 4)) old[nold++] = eq[h];
            if (!nold && eq[0] && eq[1]) old[nold++] = item(eq[1])[15] <= item(eq[0])[15] ? eq[1] : eq[0];
        } else if (s->type == 3 || s->type == 4) {   /* two hands: everything held */
            for (int h = 0; h < 2; h++) if (eq[h]) old[nold++] = eq[h];
        } else {   /* one hand: the weakest weapon held, or a two-handed one */
            int best = -1;
            for (int h = 0; h < 2; h++) if (eq[h] && item(eq[h])[10] >= 1 && item(eq[h])[10] <= 4)
                if (best < 0 || item(eq[h])[15] < item(eq[best])[15]) best = h;
            if (best >= 0) old[nold++] = eq[best];
        }
        int oatk = 0, odef = 0, k = 0;
        for (int i = 0; i < nold; i++) {
            char nm[48]; item_name(old[i], nm, sizeof nm);
            k += snprintf(r->now + k, sizeof r->now - k, "%s%s", i ? " + " : "", nm);
            oatk += item(old[i])[15]; odef += item(old[i])[16];
            if (old[i] == id) r->same = 1;
        }
        if (!nold) snprintf(r->now, sizeof r->now, "%s", "слот пуст");
        r->datk = s->atk - oatk; r->ddef = s->def - odef;
    }
    return 1;
}

/* the field menu's inventory list: window handler $05B4B2, page $FFE206
 * (8 items each), cursor $FFEC9F, items $FFF410 */
int shop_inventory(shop_t *s)
{
    if (rw(0xEF00) != 0x0C) return 0;
    uint32_t table = rl(0xEC94);
    if (table >= g_rom_size || roml(table + (uint32_t)rw(0xEC90) * 8 + 4) != 0x05B4B2) return 0;
    int page = rw(0xE206), cur = g_ram[0xEC9F];
    if (page > 4 || cur > 7) return 0;
    int id = g_ram[0xF410 + page * 8 + cur];
    if (id < 1 || id > NITEMS) return 0;
    return describe(s, id, 0);
}

/* the battle item list: menu state $FF4109 == $17, cursor $FF41C5 + 2 * slot
 * (index into the whole list, 4 rows a page): the acting character's worn
 * items (right hand, left hand, head, body) followed by the bag */
int shop_battle_item(shop_t *s)
{
    if (rw(0xEF00) != 0x14 || g_ram[0x4109] != 0x17) return 0;
    int slot = g_ram[0x4107];
    if (slot > 4) return 0;
    int rec = g_ram[0xF40A + slot];
    if (rec > 15) return 0;
    int list[44], n = 0;
    for (int k = 0; k < 4; k++) { int v = g_ram[0xF500 + rec * 0x80 + 0x4C + k]; if (v) list[n++] = v; }
    for (int k = 0; k < 40; k++) { int v = g_ram[0xF410 + k]; if (v) list[n++] = v; }
    int cur = g_ram[0x41C5 + 2 * slot];
    if (cur >= n) return 0;
    if (!describe(s, list[cur], 0)) return 0;
    s->nrow = 0;   /* no wardrobe comparison in a battle */
    s->battle = 1;
    return 1;
}
