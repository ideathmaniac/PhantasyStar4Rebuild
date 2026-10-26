/*
 * Technique and skill descriptions for Phantasy Star IV (known ROM only).
 *
 * The game shows a technique's name and cost; this finds which entry of an
 * open technique or skill list is highlighted and builds a short
 * description from the game's own data:
 *   names      ROM $2AC4E6: 40 techniques, then (after FF padding) skills,
 *              each ended by FE; {49}{9B}/{49}{9C} are the "+1"/"+2" marks
 *   properties ROM $2AB048, 8 bytes per entry (techniques 0-39, skills from 40):
 *              effect, TP cost, target, power, ...
 * RAM (found with save states, see tests): battle mode $FFEF00 == $14;
 *   $FF410A 2 = technique list open, 3 = skill list open; $FF41A3 the chosen
 *   command (1 techniques, 2 skills); $FF412E the technique list as shown
 *   (ids + 1), cursor $FF41AF; the skill list is the acting character's
 *   record +$62 (ids + 1, shown in reverse order), cursor $FF41BB; the actor
 *   is party slot $FF4107 (party list $FFF40A, records $FFF500 + n * $80).
 *   Field: mode $0C, window $FFEC90 == $56 is the technique list
 *   ($FFE210, cursor $FFECA0).
 */
#include <string.h>
#include <stdio.h>
#include "md.h"
#include "tips.h"

#define NAMES 0x2AC4E6
#define PROPS 0x2AB048
#define NTECH 40
#define NSKILL 54

/* effects after StrategyWiki "Phantasy Star IV/Techniques and Skills" and
 * the Phantasy Star Wiki; they agree with the effect codes in the ROM table */
static const char *tech_desc[NTECH] = {
    "Огонь по одному врагу.",
    "Сильный огонь по одному врагу.",
    "Самый сильный огонь по одному врагу.",
    "Лёд по одному врагу.",
    "Сильный лёд по одному врагу.",
    "Самый сильный лёд по одному врагу.",
    "Удар светом по одному врагу.",
    "Сильный удар светом по одному врагу.",
    "Самый сильный удар светом по одному врагу.",
    "Вакуумные лезвия по всем врагам.",
    "Сильные вакуумные лезвия по всем врагам.",
    "Самые сильные вакуумные лезвия по всем врагам.",
    "Гравитация давит всех врагов.",
    "Сильная гравитация по всем врагам.",
    "Самая сильная гравитация по всем врагам.",
    "Высшая техника: огромный урон всем врагам.",
    "Шанс мгновенно уничтожить каждого врага.",
    "Шанс мгновенно уничтожить одного врага.",
    "Шанс мгновенно уничтожить каждого врага.",
    "Снижает атаку всех врагов.",
    "Снижает ловкость всех врагов.",
    "Запечатывает врагам техники.",
    "Парализует всех врагов.",
    "Восстанавливает ЕЖ одному союзнику.",
    "Восстанавливает больше ЕЖ одному союзнику.",
    "Восстанавливает очень много ЕЖ одному союзнику.",
    "Восстанавливает ЕЖ всему отряду.",
    "Восстанавливает больше ЕЖ всему отряду.",
    "Восстанавливает очень много ЕЖ всему отряду.",
    "Повышает атаку союзника (не андроида).",
    "Повышает ловкость всего отряда.",
    "Повышает защиту всего отряда.",
    "Почти бесполезна; обычным путём в игре не изучается.",
    "Излечивает союзника от яда.",
    "Излечивает союзника от паралича.",
    "Оживляет павшего союзника с 1/4 ЕЖ.",
    "Лечит яд, сон, паралич или оживляет с полным ЕЖ.",
    "Будит спящих: весь отряд.",
    "Переносит отряд в уже посещённый город.",
    "Выводит отряд из подземелья.",
};

static const char *target_name(int t)
{
    switch (t) {
    case 0x11: return "один враг";
    case 0x12: return "все враги";
    case 0x14: case 0x34: case 0x38: return "один союзник";
    case 0x15: case 0x19: case 0x35: case 0x39: return "весь отряд";
    case 0x13: case 0x33: return "только на себя";
    case 0x20: return "только вне боя";
    }
    return 0;
}

/* the game's text encoding -> UTF-8 */
static int put(char *o, int k, int n, const char *s)
{
    int l = (int)strlen(s);
    if (k + l < n) { memcpy(o + k, s, (size_t)l); k += l; }
    return k;
}
static void decode(uint32_t a, char *out, int n)
{
    static const char *caps[25] = { 0, "А", "Б", "В", "Г", "Д", "Е", "Ж", "З", "И", "К", "Л", "М", "Н", "О", "П",
                                    "Р", "С", "Т", "У", "Ч", "Х", "Ш", "Щ", "Э" };
    static const char *small[31] = { "а", "б", "в", "г", "д", "е", "ж", "з", "и", "к", "л", "м", "н", "о", "п", "р",
                                     "с", "т", "у", "ф", "х", "ц", "ч", "ш", "щ", "ъ", "ы", "ь", "э", "ю", "я" };
    int k = 0;
    for (int i = 0; i < 24 && a + i < g_rom_size; i++) {
        uint8_t c = g_rom[a + i];
        if (c == 0xFE || c == 0xFF || c == 0) break;
        if (c == 0xF1 && g_rom[a + i + 1] == 0x09) { k = put(out, k, n, "Й"); i++; continue; }
        if (c == 0x49 && g_rom[a + i + 1] == 0x9B) { k = put(out, k, n, "+1"); i++; continue; }
        if (c == 0x49 && g_rom[a + i + 1] == 0x9C) { k = put(out, k, n, "+2"); i++; continue; }
        const char *s = "?";
        if (c >= 0x1F && c < 0x1F + 31) s = small[c - 0x1F];
        else if (c >= 1 && c < 25) s = caps[c];
        else switch (c) {
            case 0x19: s = "Ь"; break; case 0x1A: s = "Я"; break; case 0x1B: s = "-"; break;
            case 0x45: s = "Ф"; break; case 0x46: s = "Ц"; break; case 0x47: s = "Ю"; break;
            case 0x48: s = "Ы"; break; case 0x4D: s = "Ж"; break; case 0x56: s = "Ш"; break;
            case 0x64: s = "Л"; break; case 0x65: s = "Ч"; break;
        }
        k = put(out, k, n, s);
    }
    out[k] = 0;
}

/* address of name number i (techniques first, then skills); 0 if not found */
static uint32_t name_addr(int i)
{
    static uint32_t addr[NTECH + NSKILL];
    static int done;
    if (!done) {
        done = 1;
        uint32_t a = NAMES;
        for (int k = 0; k < NTECH + NSKILL && a < g_rom_size; k++) {
            if (k == NTECH) while (a < g_rom_size && (g_rom[a] == 0xFF || g_rom[a] == 0x00)) a++;
            addr[k] = a;
            while (a < g_rom_size && g_rom[a] != 0xFE) a++;
            a++;
        }
    }
    return (i >= 0 && i < NTECH + NSKILL) ? addr[i] : 0;
}

static int fill(tip_t *t, int skill, int id /* 0-based within its kind */)
{
    int idx = skill ? NTECH + id : id;
    if (id < 0 || (skill ? id >= NSKILL : id >= NTECH)) return 0;
    const uint8_t *p = &g_rom[PROPS + idx * 8];
    memset(t, 0, sizeof *t);
    t->skill = skill; t->id = id;
    uint32_t na = name_addr(idx);
    if (na) decode(na, t->name, sizeof t->name);
    t->cost = skill ? -1 : p[1];
    const char *tg = target_name(p[2]);
    if (tg) snprintf(t->target, sizeof t->target, "%s", tg);
    int eff = p[0];
    if ((eff == 0x01 || eff == 0x12) && p[3]) t->power = 1 + p[3] * 4 / 255;
    const char *d;
    if (!skill) d = tech_desc[id];
    else {
        int all = p[2] == 0x12;
        switch (eff) {
        case 0x01: d = all ? "Урон всем врагам." : "Урон одному врагу."; break;
        case 0x02: d = all ? "Шанс мгновенно уничтожить каждого врага." : "Шанс мгновенно уничтожить одного врага."; break;
        case 0x03: d = all ? "Снижает атаку всех врагов." : "Снижает атаку врага."; break;
        case 0x06: d = all ? "Снижает ловкость всех врагов." : "Снижает ловкость врага."; break;
        case 0x07: d = all ? "Парализует всех врагов." : "Парализует одного врага."; break;
        case 0x08: d = "Запечатывает врагам техники."; break;
        case 0x09: d = "Повышает атаку."; break;
        case 0x0A: d = "Повышает защиту."; break;
        case 0x0B: d = "Повышает защиту от техник."; break;
        case 0x0C: d = "Повышает ловкость."; break;
        case 0x0F: d = "Снимает паралич и восстанавливает ЕТ (кроме андроидов)."; break;
        case 0x12: d = "Восстанавливает ЕЖ."; break;
        case 0x17: d = "Лечит вредные состояния и оживляет павших (кроме андроидов)."; break;
        case 0x26: d = "Повышает меткость отряда (кроме андроидов)."; break;
        default: d = "Особый навык."; break;
        }
    }
    snprintf(t->desc, sizeof t->desc, "%s", d);
    /* how techniques work, after the Steam guide "Всё про Phantasy Star IV" */
    const char *note = 0;
    if (skill) note = "ЕТ не тратит; применения восстанавливаются отдыхом в гостинице.";
    else if (eff == 0x01) note = "Не промахивается; урон растёт с Менталом и слабостью врага к стихии.";
    else if (eff == 0x02) note = "Срабатывает не всегда.";
    else if (eff == 0x03 || eff == 0x06 || eff == 0x07 || eff == 0x08) note = "Может не сработать: шанс зависит от Силы или Ментала врага.";
    else if (eff == 0x12 || eff == 0x15 || eff == 0x16) note = "Не действует на андроидов (Рен, Деми).";
    else if (id < 38) note = "Не промахивается.";
    if (note) snprintf(t->note, sizeof t->note, "%s", note);
    return 1;
}

static uint16_t rw(uint32_t a) { return (uint16_t)(g_ram[a & 0xFFFF] << 8 | g_ram[(a + 1) & 0xFFFF]); }

int tips_current(tip_t *t)
{
    uint16_t mode = rw(0xEF00);
    if (mode == 0x14) {
        uint8_t open = g_ram[0x410A], cmd = g_ram[0x41A3];
        if (open == 2 && cmd == 1) {
            int cur = g_ram[0x41AF];
            if (cur > 15) return 0;
            int v = g_ram[0x412E + cur];
            return v ? fill(t, 0, v - 1) : 0;
        }
        if (open == 3 && cmd == 2) {
            int slot = g_ram[0x4107];
            if (slot > 4) return 0;
            int rec = g_ram[0xF40A + slot];
            if (rec > 15) return 0;
            const uint8_t *r = &g_ram[0xF500 + rec * 0x80 + 0x62];
            int list[8], n = 0;
            for (int i = 7; i >= 0; i--) if (r[i]) list[n++] = r[i];
            int cur = g_ram[0x41BB];
            return cur < n ? fill(t, 1, list[cur] - 1) : 0;
        }
        return 0;
    }
    if (mode == 0x0C && rw(0xEC90) == 0x56) {
        int cur = g_ram[0xECA0];
        if (cur > 15) return 0;
        int v = g_ram[0xE210 + cur];
        return v ? fill(t, 0, v - 1) : 0;
    }
    return 0;
}
