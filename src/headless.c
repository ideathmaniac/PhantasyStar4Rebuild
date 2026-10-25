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
#ifdef PS4_RECOMP
    { void ps4_recomp_install(void); if (!getenv("INTERP_ONLY")) ps4_recomp_install(); }
#endif
    if (getenv("WS")) { void ps4_ws_install(void); ps4_ws_install(); g_ws_ext = atoi(getenv("WS")); }
    FILE *log = logp ? fopen(logp, "w") : NULL;
    FILE *wav = wavp ? fopen(wavp, "wb") : NULL;
    long wav_frames = 0;
    if (wav) fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0\x04\0\x10\0data\0\0\0\0", 1, 44, wav);
    if (getenv("SNDMASK")) g_snd_mask = atoi(getenv("SNDMASK"));
    g_snd_enabled = wav != NULL || getenv("SND") != NULL;
    long frames = atol(argv[2]);
    for (long f = 0; f < frames; f++) {
        run_frame();
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
        }
    }
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
    return 0;
}
