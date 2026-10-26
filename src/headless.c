/*
 * Headless runner: ps4_headless ROM FRAMES [-i inputs.txt] [-o outdir] [-s every]
 *                  [-l frames.log] [-p interp_pcs.txt]
 * inputs.txt: lines "<frame> <buttons>" where buttons is a string of
 * U D L R A B C S (or '-' for none), applied from that frame on.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "run.h"
#include "sound.h"
#include "hdpack.h"
#include "qol.h"
#include "tips.h"
#include "shop.h"
#include "bestiary.h"
#include "md.h"
#include <dirent.h>
#include <sys/stat.h>

/* HD pack file access for the headless runner (HD=<pack folder>) */
static char hd_root[400];
static int hdw(const char *rel, const void *d, size_t n)
{
    char p[800]; snprintf(p, sizeof p, "%s/%s", hd_root, rel);
    for (char *c = p + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(p, 0755); *c = '/'; }
    FILE *f = fopen(p, "wb"); if (!f) return 0;
    size_t w = fwrite(d, 1, n, f); fclose(f); return w == n;
}
static uint8_t *hdr(const char *rel, size_t *n)
{
    char p[800]; snprintf(p, sizeof p, "%s/%s", hd_root, rel);
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(sz > 0 ? (size_t)sz : 1); *n = fread(b, 1, (size_t)(sz > 0 ? sz : 0), f); fclose(f); return b;
}
static void hdl(const char *rel, void (*cb)(const char *, void *), void *u)
{
    char p[800]; snprintf(p, sizeof p, "%s/%s", hd_root, rel);
    DIR *d = opendir(p); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.') cb(e->d_name, u);
    closedir(d);
}
static void hdlog(const char *m) { fprintf(stderr, "%s\n", m); }
static uint32_t hd_frame[224 * 4][448 * 4];

static struct { uint64_t frame; uint16_t pad; } inp[4096];
static int ninp, inp_pos;

static void load_inputs(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    char line[256];
    while (fgets(line, sizeof line, f) && ninp < 4096) {
        unsigned long long fr; char b[64] = "-";
        if (line[0] == '#' || sscanf(line, "%llu %63s", &fr, b) < 1) continue;
        uint16_t p = 0;
        for (char *c = b; *c; c++) switch (*c) {
            case 'U': p |= PAD_UP; break; case 'D': p |= PAD_DOWN; break;
            case 'L': p |= PAD_LEFT; break; case 'R': p |= PAD_RIGHT; break;
            case 'A': p |= PAD_A; break; case 'B': p |= PAD_B; break;
            case 'C': p |= PAD_C; break; case 'S': p |= PAD_START; break;
        }
        inp[ninp].frame = fr; inp[ninp].pad = p; ninp++;
    }
    fclose(f);
}

/* POKE="from-to:ADDR=VAL[b],..." writes RAM at the start of those frames (b: byte, else word) */
static struct { unsigned from, to, addr, val; int byte; } pokes[16]; static int npokes;
static void frame_cb(void)
{
    for (int i = 0; i < npokes; i++) if (g_frame >= pokes[i].from && g_frame <= pokes[i].to) {
        unsigned a = pokes[i].addr & 0xFFFF, v = pokes[i].val;
        if (pokes[i].byte) g_ram[a] = (uint8_t)v; else { g_ram[a] = (uint8_t)(v >> 8); g_ram[(a + 1) & 0xFFFF] = (uint8_t)v; }
    }
    while (inp_pos < ninp && inp[inp_pos].frame <= g_frame) g_pad[0] = inp[inp_pos++].pad;
}

static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int W = g_fb_width;
    fprintf(f, "P6\n%d %d\n255\n", W, MD_VIS_LINES);
    for (int y = 0; y < MD_VIS_LINES; y++)
        for (int x = 0; x < W; x++) {
            uint32_t p = g_fb[y][x];
            fputc((p >> 16) & 255, f); fputc((p >> 8) & 255, f); fputc(p & 255, f);
        }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s ROM FRAMES [-i inputs] [-o dir] [-s every] [-l log] [-p pcs]\n", argv[0]); return 1; }
    const char *outdir = NULL, *logp = NULL, *pcs = NULL, *wavp = NULL;
    int every = 0;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "-i")) load_inputs(argv[i + 1]);
        else if (!strcmp(argv[i], "-o")) outdir = argv[i + 1];
        else if (!strcmp(argv[i], "-s")) every = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "-l")) logp = argv[i + 1];
        else if (!strcmp(argv[i], "-p")) pcs = argv[i + 1];
        else if (!strcmp(argv[i], "-w")) wavp = argv[i + 1];
    }
    if (!md_load_rom(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    g_log_interp = pcs != NULL;
    { extern uint32_t g_watch_lo, g_watch_hi; if (getenv("WATCH")) sscanf(getenv("WATCH"), "%x-%x", &g_watch_lo, &g_watch_hi); }
    md_frame_cb = frame_cb;
    if (getenv("POKE")) {
        const char *p = getenv("POKE");
        while (*p && npokes < 16) {
            int n = 0; char t = 0;
            if (sscanf(p, "%u-%u:%x=%x%n", &pokes[npokes].from, &pokes[npokes].to, &pokes[npokes].addr, &pokes[npokes].val, &n) != 4) break;
            p += n; if (*p == 'b') { pokes[npokes].byte = 1; p++; } (void)t;
            npokes++;
            if (*p == ',') p++;
        }
    }
    run_reset();
    if (getenv("MINIMAP")) { extern int g_ws_probe; g_ws_probe = 1; }
    if (getenv("SRAM_IN")) { FILE *sf = fopen(getenv("SRAM_IN"), "rb"); if (sf) { fread(g_sram, 1, sizeof g_sram, sf); fclose(sf); } }
#ifdef PS4_RECOMP
    { void ps4_recomp_install(void); if (!getenv("INTERP_ONLY")) ps4_recomp_install(); }
#endif
    if (getenv("STATE_IN")) {                 /* start from a save state of the Windows build */
        FILE *sf = fopen(getenv("STATE_IN"), "rb");
        if (sf) {
            fseek(sf, 0, SEEK_END); long n = ftell(sf); fseek(sf, 0, SEEK_SET);
            uint8_t *b = malloc((size_t)n); fread(b, 1, (size_t)n, sf); fclose(sf);
            int emu_state_load(const uint8_t *, size_t);
            fprintf(stderr, "state load: %d\n", emu_state_load(b, (size_t)n));
            free(b);
        }
    }
    if (getenv("WS")) { void ps4_ws_install(void); ps4_ws_install(); g_ws_ext = atoi(getenv("WS")); extern int g_ws_probe; g_ws_probe = 1;
                        void ps4_ws_patch_rom(int); ps4_ws_patch_rom(g_ws_ext); }
    if (getenv("HD")) {
        snprintf(hd_root, sizeof hd_root, "%s", getenv("HD"));
        hd_io_t io = { hdw, hdr, hdl, hdlog };
        hd_set_io(&io);
        g_hd_track = 1;
        hd_load();
        if (getenv("HD_IMPORT")) { char r[300]; hd_import(r, sizeof r); fprintf(stderr, "import: %s\n", r); hd_load(); }
    }
    FILE *log = logp ? fopen(logp, "w") : NULL;
    FILE *wav = wavp ? fopen(wavp, "wb") : NULL;
    long wav_frames = 0;
    if (wav) fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0\x04\0\x10\0data\0\0\0\0", 1, 44, wav);
    if (getenv("SNDMASK")) g_snd_mask = atoi(getenv("SNDMASK"));
    g_snd_enabled = wav != NULL || getenv("SND") != NULL;
    long frames = atol(argv[2]);
    if (getenv("QOL")) { int e = 0, x = 1, m = 1; sscanf(getenv("QOL"), "%d,%d,%d", &e, &x, &m); qol_set(e, x, m); qol_resync(); }
    for (long f = 0; f < frames; f++) {
        if (getenv("SND_HOLD") && atoi(getenv("SND_HOLD")) > 0) {   /* speed-up sound test */
            extern uint32_t g_skip_sub; g_snd_hold = f % atoi(getenv("SND_HOLD")) != 0; g_skip_sub = g_snd_hold ? 0x0D0000u : 0xFFFFFFFFu; }
        run_frame();
        if (getenv("QOL")) qol_after_frame();
        bestiary_after_frame();
        if (wav) { int16_t buf[4096]; int n; while ((n = snd_take(buf, 2048)) > 0) { fwrite(buf, 4, n, wav); wav_frames += n; } }
        if (log) {
            fprintf(log, "%llu c=%llu pc=%06X sr=%04X d=%08X %08X %08X %08X %08X %08X %08X %08X a=%08X %08X %08X %08X %08X %08X %08X %08X ram=%08X vram=%08X cram=%08X\n",
                    (unsigned long long)g_frame, (unsigned long long)g_cycles, R.pc, cpu_get_sr(),
                    R.d[0], R.d[1], R.d[2], R.d[3], R.d[4], R.d[5], R.d[6], R.d[7],
                    R.a[0], R.a[1], R.a[2], R.a[3], R.a[4], R.a[5], R.a[6], R.a[7],
                    md_crc32(g_ram, sizeof g_ram), md_crc32(V.vram, sizeof V.vram), md_crc32(V.cram, sizeof V.cram));
        }
        if (getenv("DUMP_EVERY") && (f + 1) % atoi(getenv("DUMP_EVERY")) == 0 && f + 1 >= atol(getenv("DUMP_FROM") ? getenv("DUMP_FROM") : "0")) {
            char p[512]; snprintf(p, sizeof p, "%s/d%06ld.bin", getenv("DUMP_DIR"), f + 1);
            FILE *d = fopen(p, "wb");
            fwrite(V.vram, 1, sizeof V.vram, d); fwrite(V.cram, 2, 64, d); fwrite(V.vsram, 2, 40, d); fwrite(V.reg, 1, 32, d);
            fwrite(g_ram, 1, sizeof g_ram, d); fclose(d);
        }
        if (outdir && every && (f + 1) % every == 0) {
            char p[512]; snprintf(p, sizeof p, "%s/f%06ld.ppm", outdir, f + 1);
            save_ppm(p);
            int hw, hh;
            if (hd_root[0] && hd_compose(&hd_frame[0][0], 448 * 4, &hw, &hh)) {
                snprintf(p, sizeof p, "%s/h%06ld.ppm", outdir, f + 1);
                FILE *hf = fopen(p, "wb");
                fprintf(hf, "P6\n%d %d\n255\n", hw, hh);
                for (int y = 0; y < hh; y++) for (int x = 0; x < hw; x++) { uint32_t c = hd_frame[y][x]; fputc(c >> 16, hf); fputc((c >> 8) & 255, hf); fputc(c & 255, hf); }
                fclose(hf);
                fprintf(stderr, "frame %ld: hd coverage %.1f%%\n", f + 1, hd_coverage() * 100);
            }
        }
        if (getenv("KIND_LOG")) {                    /* cutscene detection changes, with a picture of each */
            extern int g_ws_cutscene; static int lastk = -1;
            if (g_ws_cutscene != lastk) {
                fprintf(stderr, "frame %ld: %s\n", f + 1, g_ws_cutscene ? "CUTSCENE" : "game");
                lastk = g_ws_cutscene;
                if (outdir) { char p[512]; snprintf(p, sizeof p, "%s/k%06ld_%d.ppm", outdir, f + 1, lastk); save_ppm(p); }
            }
        }
        if (getenv("TIP_LOG")) {                     /* technique / skill descriptions */
            static char last[64]; tip_t t; char key[64] = "";
            if (tips_current(&t)) snprintf(key, sizeof key, "%d.%d", t.skill, t.id);
            if (strcmp(key, last)) {
                strcpy(last, key);
                if (key[0]) fprintf(stderr, "frame %ld: %s %s | cost %d | %s | power %d | %s %s\n", f + 1, t.skill ? "skill" : "tech",
                                    t.name, t.cost, t.target, t.power, t.desc, t.note), t.combo[0] ? fprintf(stderr, "    combo [%x]: %s\n", t.combo_ready, t.combo) : 0;
                else fprintf(stderr, "frame %ld: (no list)\n", f + 1);
            }
        }
        if (getenv("SHOP_LOG")) {                    /* shop helper */
            static int last = -2; shop_t sh; int k = (shop_current(&sh) || shop_inventory(&sh)) ? sh.id : -1;
            if (k != last) {
                last = k;
                if (k < 0) fprintf(stderr, "frame %ld: (no shop list)\n", f + 1);
                else {
                    fprintf(stderr, "frame %ld: %s | %s | atk %d def %d | %d mes (have %u) | in bag %d | %s\n", f + 1, sh.name, sh.kind,
                            sh.atk, sh.def, sh.price, sh.money, sh.owned, sh.use);
                    for (int i = 0; i < sh.nrow; i++)
                        fprintf(stderr, "    %s: %s%s%s datk %+d ddef %+d\n", sh.row[i].who, sh.row[i].can ? "" : "НЕ МОЖЕТ ",
                                sh.row[i].now, sh.row[i].same ? " (same)" : "", sh.row[i].datk, sh.row[i].ddef);
                }
            }
        }
        if (getenv("ENC_LOG")) {
            static int ls = -1;
            if (g_ram[0xECE3] != ls) { ls = g_ram[0xECE3]; fprintf(stderr, "step %d enc %d mode %02X\n", ls, g_ram[0xECE4], g_ram[0xEF01]); }
        }
        if (getenv("MODE_LOG")) {                    /* top-level game mode changes */
            static int lastm = -1; int m = (g_ram[0xEF00] << 8) | g_ram[0xEF01];
            if (m != lastm) { fprintf(stderr, "frame %ld: mode %02X\n", f + 1, m); lastm = m; }
        }
        if (getenv("SHOT_MODE") && outdir) {         /* save frames while the game mode is this value */
            static long last = -100000; int mode = (g_ram[0xEC20] << 8) | g_ram[0xEC21];
            if (mode == (int)strtol(getenv("SHOT_MODE"), 0, 16) && f - last >= 120) {
                last = f; char p[512]; snprintf(p, sizeof p, "%s/m%06ld.ppm", outdir, f + 1); save_ppm(p);
                if (getenv("SHOT_DUMP")) { snprintf(p, sizeof p, "%s/m%06ld.bin", outdir, f + 1); FILE *d = fopen(p, "wb");
                    fwrite(V.vram, 1, sizeof V.vram, d); fwrite(V.cram, 2, 64, d); fwrite(V.vsram, 2, 40, d); fwrite(V.reg, 1, 32, d);
                    fwrite(g_ram, 1, sizeof g_ram, d); fclose(d); }
            }
        }
        if (hd_root[0]) {
            if (getenv("HD_AUTO")) hd_auto_capture();
            const char *cl = getenv("HD_CAPTURE");
            for (const char *c = cl; c && *c;) { long v = atol(c); if (v == f + 1) hd_capture(); c = strchr(c, ','); if (c) c++; }
        }
    }
    if (getenv("SRAM_OUT")) { FILE *sf = fopen(getenv("SRAM_OUT"), "wb"); fwrite(g_sram, 1, sizeof g_sram, sf); fclose(sf); fprintf(stderr, "sram dirty %d\n", g_sram_dirty); }
    if (getenv("YMDUMP")) { void ym_debug_dump(void); ym_debug_dump(); }
    if (log) fclose(log);
    if (wav) {
        uint32_t d = (uint32_t)wav_frames * 4, r = d + 36;
        fseek(wav, 4, SEEK_SET); fwrite(&r, 4, 1, wav); fseek(wav, 40, SEEK_SET); fwrite(&d, 4, 1, wav); fclose(wav);
    }
    if (getenv("VDP_DUMP")) {
        FILE *d = fopen(getenv("VDP_DUMP"), "wb");
        fwrite(V.vram, 1, sizeof V.vram, d); fwrite(V.cram, 2, 64, d); fwrite(V.vsram, 2, 40, d); fwrite(V.reg, 1, 32, d);
        fwrite(g_ram, 1, sizeof g_ram, d); fclose(d);
    }
    if (pcs) run_dump_interp_pcs(pcs);
    fprintf(stderr, "frames %llu  pc %06X  interpreted instructions %llu\n", (unsigned long long)g_frame, R.pc,
            (unsigned long long)g_interp_insns);
    if (getenv("BEST_LOG")) {
        for (int id = 0; id < BEST_MAX; id++) {
            best_info_t b;
            if (!bestiary_info(id, &b)) { if (getenv("BEST_ALL")) fprintf(stderr, "%3d: -\n", id); continue; }
            if (b.seen || getenv("BEST_ALL"))
                fprintf(stderr, "%3d %s: HP %d atk %d def %d agi %d exp %d mes %d | seen %u killed %u\n", id, b.name, b.hp, b.attack, b.defence, b.agility, b.exp, b.meseta, b.seen, b.killed);
        }
    }
    { extern unsigned long g_ym_w68, g_ym_wz80; if (getenv("YM_COUNT")) fprintf(stderr, "ym writes: 68k %lu, z80 %lu\n", g_ym_w68, g_ym_wz80); }
    if (getenv("MINIMAP")) {   /* write the minimap of the last frame */
        int ps4_ws_minimap(uint32_t *out, int ow, int oh, int *mx, int *my, int *fw, int *fh);
        static uint32_t mm[160 * 120]; int mx, my, fw, fh;
        if (ps4_ws_minimap(mm, 160, 120, &mx, &my, &fw, &fh)) {
            FILE *o = fopen(getenv("MINIMAP"), "wb"); fprintf(o, "P6\n160 120\n255\n");
            for (int k = 0; k < 160 * 120; k++) { int x = k % 160, y = k / 160; uint32_t c = (abs(x - mx) <= 1 && abs(y - my) <= 1) ? 0xFF0000 : mm[k]; fputc(c >> 16, o); fputc((c >> 8) & 255, o); fputc(c & 255, o); }
            fclose(o); fprintf(stderr, "minimap: player %d,%d map %dx%d\n", mx, my, fw, fh);
        } else fprintf(stderr, "minimap: not available\n");
    }
    return 0;
}
