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
 *   $FF4109 the command menu state, 8 = technique list open, $11 = skill
 *   list open ($FF410A / $FF41A3 keep the previous character's choice, so
 *   they cannot be used); $FF412E the technique list as shown
 *   (ids + 1), cursor $FF41AF + 2 * slot; the skill list is the acting character's
 *   record +$62 (ids + 1, shown in reverse order), cursor $FF41BB + 2 * slot; the actor
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


/* Combination attacks (the party's macro uses them in order), after the
 * squarefaction.ru article "Макросы, техники и навыки в русской версии
 * Phantasy Star IV". Members are indices into the name/property tables
 * (techniques 0-39, skills 40+); a group lists interchangeable entries. */
typedef struct { const char *label; short idx[4]; } cgroup;
static const cgroup G[] = {
    { "ФОЙ", { 0, 1, 2, -1 } },      /* 0 */
    { "ЗАН", { 9, 10, 11, -1 } },    /* 1 */
    { "БУРЯ", { 49, 89, -1 } },      /* 2 */
    { "ФЛАЕЛИ", { 43, 88, -1 } },    /* 3 */
    { "ВАТ", { 3, 4, 5, -1 } },      /* 4 */
    { "ВОЛНЫ", { 56, -1 } },         /* 5 */
    { "ПОЛОСА (Чаз)", { 47, -1 } },  /* 6 */
    { "ФОЙ", { 0, -1 } },            /* 7 base tiers only */
    { "ВАТ", { 3, -1 } },            /* 8 */
    { "ТСУ", { 6, -1 } },            /* 9 */
    { "КРЕСТ", { 40, -1 } },         /* 10 */
    { "ЭФЕСС", { 51, -1 } },         /* 11 */
    { "ЗАЛП", { 53, -1 } },          /* 12 */
    { "ФОЙ+2", { 2, -1 } },          /* 13 */
    { "РАСКОЛ", { 41, -1 } },        /* 14 */
    { "АСТРАЛ", { 46, -1 } },        /* 15 */
    { "СМЕРТЬ", { 60, 63, 65, -1 } },/* 16 */
    { "ИЛЛЮЗИЯ", { 67, -1 } },       /* 17 */
    { "СТЕРЕТЬ", { 66, -1 } },       /* 18 */
    { "ГРА+2", { 14, -1 } },         /* 19 */
    { "ПОМЕХИ", { 71, -1 } },        /* 20 */
    { "ТАНДЕМ", { 50, 90, -1 } },    /* 21 */
    { "ВАЛ+1", { 18, -1 } },         /* 22 */
    { "ДИЕМ", { 61, -1 } },          /* 23 */
    { "МОЛИТВА", { 64, -1 } },       /* 24 */
    { "ДЕБАН", { 31, -1 } },         /* 25 */
    { "МЕГИД", { 15, -1 } },         /* 26 */
    { "ЛЕГИОН", { 52, -1 } },        /* 27 */
    { "ШКВАЛ", { 54, -1 } },         /* 28 */
    { "ВАТ+2", { 5, -1 } },          /* 29 */
};
typedef struct { const char *name, *cond; signed char g[4]; } calt;   /* g: groups in macro order, -1 ends */
static const calt CA[] = {
    { "Ураган огня", 0, { 1, 0, -1 } }, { "Ураган огня", 0, { 0, 2, -1 } },
    { "Ураган огня", 0, { 3, 1, -1 } }, { "Ураган огня", 0, { 3, 2, -1 } },
    { "Метель", 0, { 4, 1, -1 } }, { "Метель", 0, { 4, 2, -1 } },
    { "Волна", 0, { 5, 6, -1 } },
    { "Три стихии", "только ФОЙ, ВАТ, ТСУ без +1/+2", { 8, 7, 9, -1 } },
    { "Рубка", 0, { 10, 11, -1 } },
    { "Звезда", 0, { 12, 13, -1 } },
    { "Пароудар", 0, { 14, 15, -1 } },
    { "Последний образ", 0, { 16, 17, -1 } },
    { "Чёрная дыра", 0, { 18, 19, -1 } },
    { "Перелом", "против машин", { 20, 21, -1 } },
    { "Холокост", "против живых врагов", { 22, 23, -1 } },
    { "Свет небес", 0, { 24, 11, -1 } },
    { "Разрушение", "строго по порядку", { 25, 26, 27, 28 } },
    { "Удар грома", 0, { 29, 21, -1 } },
};

static int in_group(int g, int idx)
{
    for (int k = 0; k < 4 && G[g].idx[k] >= 0; k++) if (G[g].idx[k] == idx) return 1;
    return 0;
}

/* "Ураган огня: + ЗАН или БУРЯ; Метель: + ВАТ" for entry idx */
/* entries chosen this round by the characters who already gave orders */
static int chosen[5], nchosen;
static int group_chosen(int g)
{
    for (int i = 0; i < nchosen; i++) if (in_group(g, chosen[i])) return 1;
    return 0;
}

static void combos_for(int idx, char *out, int n, unsigned *ready)
{
    int k = 0, line = 0; out[0] = 0; *ready = 0;
    const char *done[8]; int nd = 0;
    for (size_t a = 0; a < sizeof CA / sizeof CA[0]; a++) {
        int seen = 0;
        for (int d = 0; d < nd; d++) if (done[d] == CA[a].name) seen = 1;
        if (seen) continue;
        /* all variants of this combination that include idx */
        char parts[4][120]; int np = 0; const char *cond = 0;
        char have[120] = "";   /* the partners already chosen, when a variant is complete */
        for (size_t b = a; b < sizeof CA / sizeof CA[0]; b++) {
            if (strcmp(CA[b].name, CA[a].name)) continue;
            int mine = -1;
            for (int j = 0; j < 4 && CA[b].g[j] >= 0; j++) if (in_group(CA[b].g[j], idx)) mine = j;
            if (mine < 0) continue;
            char part[120]; int pl = 0;
            for (int j = 0; j < 4 && CA[b].g[j] >= 0; j++) {
                if (j == mine) continue;
                pl += snprintf(part + pl, sizeof part - pl, "%s%s", pl ? " + " : "", G[(int)CA[b].g[j]].label);
            }
            if (CA[b].g[2] >= 0) {   /* longer macros: the whole chain in order */
                pl = 0;
                for (int j = 0; j < 4 && CA[b].g[j] >= 0; j++)
                    pl += snprintf(part + pl, sizeof part - pl, "%s%s", pl ? " → " : "", G[(int)CA[b].g[j]].label);
            }
            int all = 1, hl = 0; char h[120] = "";
            for (int j = 0; j < 4 && CA[b].g[j] >= 0; j++) {
                if (j == mine) continue;
                if (!group_chosen(CA[b].g[j])) all = 0;
                hl += snprintf(h + hl, sizeof h - hl, "%s%s", hl ? ", " : "", G[(int)CA[b].g[j]].label);
            }
            if (all && !have[0]) snprintf(have, sizeof have, "%s", h);
            int dup = 0;
            for (int q = 0; q < np; q++) if (!strcmp(parts[q], part)) dup = 1;
            if (!dup && np < 4) snprintf(parts[np++], 120, "%s", part);
            if (CA[b].cond) cond = CA[b].cond;
        }
        if (!np) continue;
        if (nd < 8) done[nd++] = CA[a].name;
        if (have[0]) {   /* the rest of the combination is already ordered this round */
            if (line < 32) *ready |= 1u << line;
            k += snprintf(out + k, n - k, "%s%s: уже выбрано %s", k ? "; " : "", CA[a].name, have);
            if (cond && k < n) k += snprintf(out + k, n - k, " (%s)", cond);
            line++;
            if (k >= n) { out[n - 1] = 0; break; }
            continue;
        }
        line++;
        k += snprintf(out + k, n - k, "%s%s: %s", k ? "; " : "", CA[a].name, CA[a].g[2] >= 0 ? "" : "+ ");
        for (int q = 0; q < np && k < n; q++) k += snprintf(out + k, n - k, "%s%s", q ? " или " : "", parts[q]);
        if (cond && k < n) k += snprintf(out + k, n - k, " (%s)", cond);
        if (k >= n) { out[n - 1] = 0; break; }
    }
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
    combos_for(idx, t->combo, sizeof t->combo, &t->combo_ready);
    return 1;
}

static uint16_t rw(uint32_t a) { return (uint16_t)(g_ram[a & 0xFFFF] << 8 | g_ram[(a + 1) & 0xFFFF]); }

int tips_current(tip_t *t)
{
    uint16_t mode = rw(0xEF00);
    if (mode == 0x14) {
        uint8_t state = g_ram[0x4109];     /* command menu state: 8 technique list, $11 skill list */
        int slot = g_ram[0x4107];          /* acting party slot; each slot keeps its own cursors */
        if (slot > 4) return 0;
        /* orders given this round: $FF410A + 4 * slot = command (2 technique, 3 skill), +1 = id + 1 */
        nchosen = 0;
        for (int s = 0; s < slot; s++) {
            int cmd = g_ram[0x410A + 4 * s], v = g_ram[0x410B + 4 * s];
            if (!v) continue;
            if (cmd == 2 && v <= NTECH) chosen[nchosen++] = v - 1;
            else if (cmd == 3 && v <= NSKILL) chosen[nchosen++] = NTECH + v - 1;
        }
        if (state == 0x08) {
            int cur = g_ram[0x41AF + 2 * slot];
            if (cur > 15) return 0;
            int v = g_ram[0x412E + cur];
            return v ? fill(t, 0, v - 1) : 0;
        }
        if (state == 0x11) {
            int rec = g_ram[0xF40A + slot];
            if (rec > 15) return 0;
            const uint8_t *r = &g_ram[0xF500 + rec * 0x80 + 0x62];
            int list[8], n = 0;
            for (int i = 7; i >= 0; i--) if (r[i]) list[n++] = r[i];
            int cur = g_ram[0x41BB + 2 * slot];
            return cur < n ? fill(t, 1, list[cur] - 1) : 0;
        }
        return 0;
    }
    nchosen = 0;
    if (mode == 0x0C && rw(0xEC90) == 0x56) {
        int cur = g_ram[0xECA0];
        if (cur > 15) return 0;
        int v = g_ram[0xE210 + cur];
        return v ? fill(t, 0, v - 1) : 0;
    }
    return 0;
}
