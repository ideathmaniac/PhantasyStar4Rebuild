/*
 * GPU picture filters (OpenGL 2.0 shaders, GLSL 1.10), written for this
 * project:
 *  - FX_PIXELS: sharp pixels. "Sharp bilinear": every source pixel is a solid
 *    block and only the one-pixel seam between blocks is blended, so pixels
 *    keep the same size at any window size (plain nearest makes some rows and
 *    columns wider than others).
 *  - FX_SMOOTH: edge-directed pixel-art smoothing, the GPU version of
 *    src/hdscale.c (xBR level-1 style) but resolution independent: pass 1
 *    decides per source pixel and corner whether a diagonal edge runs through
 *    it and toward which neighbour; pass 2 draws every output pixel from that.
 *  - FX_CRT / FX_CRT_CURVED: a picture-tube look. Gaussian scanlines whose
 *    width grows with brightness, an aperture-grille mask, a little glow, and
 *    optionally a curved screen.
 *  - dither: Mega Drive games fake transparency and extra colours with
 *    one-pixel stripes that a TV's composite signal blurred together; this
 *    pass blends such alternating columns back into one colour.
 */
#include <string.h>
#include <stdio.h>
#include "glfx.h"

#ifdef _WIN32
#include "win32.h"
#else
typedef unsigned int GLenum, GLuint, GLbitfield; typedef int GLint, GLsizei; typedef float GLfloat, GLclampf; typedef double GLdouble;
void glViewport(GLint, GLint, GLsizei, GLsizei);
void glGenTextures(GLsizei, GLuint *);
void glBindTexture(GLenum, GLuint);
void glTexParameteri(GLenum, GLenum, GLint);
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
void glEnable(GLenum);
void glDisable(GLenum);
void glBegin(GLenum);
void glEnd(void);
void glTexCoord2f(GLfloat, GLfloat);
void glVertex2f(GLfloat, GLfloat);
void glColor4f(GLfloat, GLfloat, GLfloat, GLfloat);
void glMatrixMode(GLenum);
void glLoadIdentity(void);
void glOrtho(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble);
void glPixelStorei(GLenum, GLint);
const unsigned char *glGetString(GLenum);
GLenum glGetError(void);
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA8 0x8058
#define GL_BGRA_EXT 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_QUADS 0x0007
#define GL_PROJECTION 0x1701
#define GL_MODELVIEW 0x1700
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_BLEND 0x0BE2
#endif
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1

typedef char GLchar;
static GLuint (*pCreateShader)(GLenum);
static void (*pShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
static void (*pCompileShader)(GLuint);
static void (*pGetShaderiv)(GLuint, GLenum, GLint *);
static void (*pGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static GLuint (*pCreateProgram)(void);
static void (*pAttachShader)(GLuint, GLuint);
static void (*pLinkProgram)(GLuint);
static void (*pGetProgramiv)(GLuint, GLenum, GLint *);
static void (*pGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static void (*pUseProgram)(GLuint);
static GLint (*pGetUniformLocation)(GLuint, const GLchar *);
static void (*pUniform1i)(GLint, GLint);
static void (*pUniform1f)(GLint, GLfloat);
static void (*pUniform2f)(GLint, GLfloat, GLfloat);
static void (*pActiveTexture)(GLenum);
static void (*pGenFramebuffers)(GLsizei, GLuint *);
static void (*pBindFramebuffer)(GLenum, GLuint);
static void (*pFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static GLenum (*pCheckFramebufferStatus)(GLenum);

static char err[600];
const char *glfx_error(void) { return err; }

#define TW 512
#define TH 256

/* ---------------------------------------------------------------- shaders */
#include "fx_shaders.h"

static const char *VS =
    "varying vec2 uv;\n"
    "void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; uv = gl_MultiTexCoord0.xy; }\n";

/* GLSL spelling of the shared shader dialect (win/fx_shaders.h) */
static const char *FS_HEAD =
    "#version 110\n"
    "#define float2 vec2\n#define float3 vec3\n#define float4 vec4\n"
    "#define lerp mix\n#define frac fract\n#define fmod mod\n"
    "#define EQ3(a, b) ((a) == (b))\n"
    "#define TEX(u) texture2D(src, (u))\n#define TEXE(u) texture2D(edge, (u))\n"
    "#define FRAGX gl_FragCoord.x\n"
    "uniform sampler2D src;\nuniform sampler2D edge;\n"
    "uniform vec2 tsize;\nuniform vec2 fsize;\nuniform vec2 scale;\nuniform float extra;\n"
    "varying vec2 uv;\n";
static const char *FS_TAIL = "void main() { gl_FragColor = body(); }\n";

/* ---------------------------------------------------------------- setup */
typedef struct { GLuint prog; GLint src, tsize, fsize, scale, extra, edge; } prog_t;
static prog_t P_DITHER, P_EDGE, P_XBR, P_PIXELS, P_CRT;
static GLuint tex_src, tex_dith, tex_edge, fbo_dith, fbo_edge;
static int ready;

static GLuint compile(GLenum type, const char *s, const char *name)
{
    GLuint sh = pCreateShader(type);
    if (type == GL_FRAGMENT_SHADER) {
        const char *parts[3] = { FS_HEAD, s, FS_TAIL };
        pShaderSource(sh, 3, parts, 0);
    } else
        pShaderSource(sh, 1, &s, 0);
    pCompileShader(sh);
    GLint ok = 0; pGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[400] = { 0 }; pGetShaderInfoLog(sh, sizeof log - 1, 0, log);
        snprintf(err, sizeof err, "shader %s: %s", name, log);
        return 0;
    }
    return sh;
}

static int link(prog_t *p, const char *fs, const char *name)
{
    GLuint v = compile(GL_VERTEX_SHADER, VS, "vertex"), f = compile(GL_FRAGMENT_SHADER, fs, name);
    if (!v || !f) return 0;
    p->prog = pCreateProgram();
    pAttachShader(p->prog, v); pAttachShader(p->prog, f);
    pLinkProgram(p->prog);
    GLint ok = 0; pGetProgramiv(p->prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[400] = { 0 }; pGetProgramInfoLog(p->prog, sizeof log - 1, 0, log);
        snprintf(err, sizeof err, "link %s: %s", name, log);
        return 0;
    }
    p->src = pGetUniformLocation(p->prog, "src");
    p->tsize = pGetUniformLocation(p->prog, "tsize");
    p->fsize = pGetUniformLocation(p->prog, "fsize");
    p->scale = pGetUniformLocation(p->prog, "scale");
    p->extra = pGetUniformLocation(p->prog, "extra");
    p->edge = pGetUniformLocation(p->prog, "edge");
    return 1;
}

static int tw = TW, th = TH;                /* size of the texture holding the current picture */
static GLuint tex_big;                     /* HD pack pictures, up to 2048 x 1024 */
static void new_tex_sized(GLuint *t, int filter, int w, int h);
static void new_tex(GLuint *t, int filter) { new_tex_sized(t, filter, TW, TH); }
static void new_tex_sized(GLuint *t, int filter, int w, int h)
{
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0);
}

static int new_fbo(GLuint *fbo, GLuint tex)
{
    pGenFramebuffers(1, fbo);
    pBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    pFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    int ok = pCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    pBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) snprintf(err, sizeof err, "framebuffer object incomplete");
    return ok;
}

int glfx_init(glfx_getproc_t gp)
{
    ready = 0; err[0] = 0;
#define L(v, n) do { *(void **)&v = gp(n); if (!v) { snprintf(err, sizeof err, "missing %s", n); return 0; } } while (0)
    L(pCreateShader, "glCreateShader"); L(pShaderSource, "glShaderSource"); L(pCompileShader, "glCompileShader");
    L(pGetShaderiv, "glGetShaderiv"); L(pGetShaderInfoLog, "glGetShaderInfoLog"); L(pCreateProgram, "glCreateProgram");
    L(pAttachShader, "glAttachShader"); L(pLinkProgram, "glLinkProgram"); L(pGetProgramiv, "glGetProgramiv");
    L(pGetProgramInfoLog, "glGetProgramInfoLog"); L(pUseProgram, "glUseProgram"); L(pGetUniformLocation, "glGetUniformLocation");
    L(pUniform1i, "glUniform1i"); L(pUniform1f, "glUniform1f"); L(pUniform2f, "glUniform2f"); L(pActiveTexture, "glActiveTexture");
    L(pGenFramebuffers, "glGenFramebuffers"); L(pBindFramebuffer, "glBindFramebuffer");
    L(pFramebufferTexture2D, "glFramebufferTexture2D"); L(pCheckFramebufferStatus, "glCheckFramebufferStatus");
#undef L
    if (!link(&P_DITHER, SH_DITHER, "dither") || !link(&P_EDGE, SH_EDGE, "edge") || !link(&P_XBR, SH_XBR, "xbr") ||
        !link(&P_PIXELS, SH_PIXELS, "pixels") || !link(&P_CRT, SH_CRT, "crt"))
        return 0;
    new_tex(&tex_src, GL_LINEAR);
    new_tex(&tex_dith, GL_LINEAR);
    new_tex(&tex_edge, GL_NEAREST);
    if (!new_fbo(&fbo_dith, tex_dith) || !new_fbo(&fbo_edge, tex_edge)) return 0;
    while (glGetError()) {}
    ready = 1;
    return 1;
}

/* ---------------------------------------------------------------- drawing */
static void use(prog_t *p, GLuint src, int W, int H, float sx, float sy)
{
    pUseProgram(p->prog);
    pActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    pUniform1i(p->src, 0);
    pUniform2f(p->tsize, (float)tw, (float)th);
    pUniform2f(p->fsize, (float)W, (float)H);
    if (p->scale >= 0) pUniform2f(p->scale, sx, sy);
}

static void quad(float x, float y, float w, float h, float u1, float v1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(u1, 0); glVertex2f(x + w, y);
    glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
    glTexCoord2f(0, v1); glVertex2f(x, y + h);
    glEnd();
}

/* draw the whole picture into an offscreen texture of the same size */
static void to_fbo(GLuint fbo, int W, int H)
{
    pBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, 0, H, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    quad(0, 0, (float)W, (float)H, (float)W / tw, (float)H / th);
}

void glfx_draw(const uint32_t *fb, int W, int H, int pitch, int x, int y, int w, int h, int cw, int ch,
               int mode, int bilinear, int dither)
{
    if (!ready) return;
    GLuint up = tex_src;
    tw = TW; th = TH;
    if (W > TW || H > TH) {                 /* an HD pack picture: sharp or soft scaling only */
        if (!tex_big) new_tex_sized(&tex_big, GL_LINEAR, 2048, 1024);
        up = tex_big; tw = 2048; th = 1024;
        mode = FX_PIXELS; dither = 0;
    }
    if (W > tw) W = tw;
    if (H > th) H = th;
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
    glBindTexture(GL_TEXTURE_2D, up);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_BGRA_EXT, GL_UNSIGNED_BYTE, fb);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    GLuint src = up;
    if (dither) {
        use(&P_DITHER, src, W, H, 1, 1);
        to_fbo(fbo_dith, W, H);
        src = tex_dith;
    }
    if (mode == FX_SMOOTH) {
        use(&P_EDGE, src, W, H, 1, 1);
        to_fbo(fbo_edge, W, H);
    }

    pBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, cw, ch);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, cw, ch, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    float sx = (float)w / W, sy = (float)h / H;
    if (mode == FX_SMOOTH) {
        use(&P_XBR, src, W, H, sx, sy);
        pActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, tex_edge);
        pUniform1i(P_XBR.edge, 1);
        pActiveTexture(GL_TEXTURE0);
    } else if (mode == FX_CRT || mode == FX_CRT_CURVED) {
        use(&P_CRT, src, W, H, sx, sy);
        pUniform1f(P_CRT.extra, mode == FX_CRT_CURVED ? 1.0f : 0.0f);
    } else {
        use(&P_PIXELS, src, W, H, sx, sy);
        pUniform1f(P_PIXELS.extra, bilinear ? 1.0f : 0.0f);
    }
    quad((float)x, (float)y, (float)w, (float)h, (float)W / tw, (float)H / th);
    pUseProgram(0);
    pActiveTexture(GL_TEXTURE0);
}
