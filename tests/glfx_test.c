/* Renders game frames (PPM) through win/glfx.c with Mesa (EGL pbuffer) and
 * writes PPMs: glfx_test in.ppm out.ppm mode dither w h [bilinear] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../win/glfx.h"
typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface; typedef int EGLint; typedef unsigned int EGLenum, EGLBoolean;
EGLDisplay eglGetDisplay(void *); EGLBoolean eglInitialize(EGLDisplay, EGLint *, EGLint *);
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *);
EGLBoolean eglBindAPI(EGLenum); EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const EGLint *);
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext); void *eglGetProcAddress(const char *);
EGLDisplay eglGetPlatformDisplay(EGLenum, void *, const intptr_t *);
EGLint eglGetError(void);
void glReadPixels(int, int, int, int, unsigned, unsigned, void *); void glClearColor(float, float, float, float); void glClear(unsigned);
const unsigned char *glGetString(unsigned);
static void *gp(const char *n) { return eglGetProcAddress(n); }
int main(int argc, char **argv)
{
    if (argc < 7) return 1;
    int mode = atoi(argv[3]), dith = atoi(argv[4]), cw = atoi(argv[5]), ch = atoi(argv[6]), bil = argc > 7 ? atoi(argv[7]) : 0;
    FILE *f = fopen(argv[1], "rb"); int W, H, mx; fscanf(f, "P6 %d %d %d", &W, &H, &mx); fgetc(f);
    static uint32_t fb[256 * 448]; unsigned char px[3];
    for (int i = 0; i < W * H; i++) { fread(px, 1, 3, f); fb[(i / W) * 448 + i % W] = (px[0] << 16) | (px[1] << 8) | px[2]; }
    fclose(f);
    EGLDisplay d = eglGetPlatformDisplay(0x31DD /* SURFACELESS_MESA */, 0, 0);
    if (!eglInitialize(d, 0, 0)) { fprintf(stderr, "egl init %x\n", eglGetError()); return 1; }
    EGLint ca[] = { 0x3033, 0x0001 /* PBUFFER */, 0x3040, 0x0008 /* OPENGL_BIT */, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3038 };
    EGLConfig cfg; EGLint n = 0; eglChooseConfig(d, ca, &cfg, 1, &n);
    if (!n) { fprintf(stderr, "no config\n"); return 1; }
    eglBindAPI(0x30A2);
    EGLContext c = eglCreateContext(d, cfg, 0, 0);
    EGLint sa[] = { 0x3057, cw, 0x3056, ch, 0x3038 };
    EGLSurface s = eglCreatePbufferSurface(d, cfg, sa);
    if (!eglMakeCurrent(d, s, s, c)) { fprintf(stderr, "makecurrent %x\n", eglGetError()); return 1; }
    if (!glfx_init(gp)) { fprintf(stderr, "glfx: %s\n", glfx_error()); return 1; }
    glClearColor(0, 0, 0, 1); glClear(0x4000);
    /* same placement as the front end: 4:3 pixel aspect, centred */
    double par = (4.0 / 3.0 * 224.0) / 320.0, sy = (double)ch / 224, sx = sy * par;
    if (sx * W > cw) { sx = (double)cw / W; sy = sx / par; }
    int w = (int)(W * sx + 0.5), h = (int)(224 * sy + 0.5), x = (cw - w) / 2, y = (ch - h) / 2;
    glfx_draw(fb, W, H, 448, x, y, w, h, cw, ch, mode, bil, dith);
    unsigned char *o = malloc((size_t)cw * ch * 4);
    glReadPixels(0, 0, cw, ch, 0x1908, 0x1401, o);
    f = fopen(argv[2], "wb"); fprintf(f, "P6\n%d %d\n255\n", cw, ch);
    for (int yy = ch - 1; yy >= 0; yy--) for (int xx = 0; xx < cw; xx++) fwrite(o + ((size_t)yy * cw + xx) * 4, 1, 3, f);
    fclose(f);
    return 0;
}
