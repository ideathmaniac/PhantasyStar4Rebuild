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
static const char *VS =
    "varying vec2 uv;\n"
    "void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex; uv = gl_MultiTexCoord0.xy; }\n";

/* common part of every fragment shader */
#define FS_HEAD \
    "uniform sampler2D src;\n" \
    "uniform vec2 tsize;\n"      /* texture size in texels */ \
    "uniform vec2 fsize;\n"      /* picture size in pixels */ \
    "varying vec2 uv;\n" \
    "vec3 F(vec2 p) { p = clamp(p, vec2(0.0), fsize - 1.0); return texture2D(src, (p + 0.5) / tsize).rgb; }\n"

static const char *FS_DITHER = FS_HEAD
    "float D(vec3 a, vec3 b) { vec3 d = abs(a - b); return d.r + d.g + d.b; }\n"
    "void main() {\n"
    "  vec2 p = floor(uv * tsize);\n"
    "  vec3 c = F(p), l = F(p + vec2(-1.0, 0.0)), r = F(p + vec2(1.0, 0.0));\n"
    "  vec3 l2 = F(p + vec2(-2.0, 0.0)), r2 = F(p + vec2(2.0, 0.0));\n"
    "  vec3 o = c;\n"
    "  if (D(l, r) < 0.01 && D(c, l) > 0.01 && D(c, l) < 0.9 && (D(c, l2) < 0.01 || D(c, r2) < 0.01)) o = mix(c, l, 0.5);\n"
    "  gl_FragColor = vec4(o, 1.0);\n"
    "}\n";

/* pass 1 of FX_SMOOTH: per corner 0 = no edge, 0.5 = blend toward the F
 * neighbour, 1.0 = toward the H neighbour (names as in src/hdscale.c) */
static const char *FS_EDGE = FS_HEAD
    "vec3 Y(vec3 c) { c *= 255.0; return vec3(dot(c, vec3(0.299, 0.587, 0.114)), dot(c, vec3(-0.169, -0.331, 0.5)), dot(c, vec3(0.5, -0.419, -0.081))); }\n"
    "float dist(vec3 a, vec3 b) { vec3 d = abs(Y(a) - Y(b)); return d.x * 48.0 + d.y * 7.0 + d.z * 6.0; }\n"
    "vec2 rot(int k, vec2 t) {\n"
    "  if (k == 0) return t;\n"
    "  if (k == 1) return vec2(-t.y, t.x);\n"
    "  if (k == 2) return -t;\n"
    "  return vec2(t.y, -t.x);\n"
    "}\n"
    "float corner(vec2 p, int k) {\n"
    "  vec3 e = F(p), f = F(p + rot(k, vec2(1.0, 0.0))), h = F(p + rot(k, vec2(0.0, 1.0)));\n"
    "  if (f == e || h == e) return 0.0;\n"
    "  vec3 i = F(p + rot(k, vec2(1.0, 1.0))), c = F(p + rot(k, vec2(1.0, -1.0))), g = F(p + rot(k, vec2(-1.0, 1.0)));\n"
    "  vec3 b = F(p + rot(k, vec2(0.0, -1.0)));\n"
    "  vec3 f4 = F(p + rot(k, vec2(2.0, 0.0))), h5 = F(p + rot(k, vec2(0.0, 2.0)));\n"
    "  vec3 d5 = F(p + rot(k, vec2(-1.0, 2.0))), i5 = F(p + rot(k, vec2(1.0, 2.0))), i4 = F(p + rot(k, vec2(2.0, 1.0)));\n"
    "  float wd1 = dist(e, c) + dist(e, g) + dist(i, f4) + dist(i, h5) + 4.0 * dist(h, f);\n"
    "  float wd2 = dist(h, d5) + dist(h, i5) + dist(f, i4) + dist(f, b) + 4.0 * dist(e, i);\n"
    "  if (wd1 >= wd2) return 0.0;\n"
    "  return dist(e, f) <= dist(e, h) ? 0.5 : 1.0;\n"
    "}\n"
    "void main() {\n"
    "  vec2 p = floor(uv * tsize);\n"
    "  gl_FragColor = vec4(corner(p, 0), corner(p, 1), corner(p, 2), corner(p, 3));\n"
    "}\n";

/* pass 2 of FX_SMOOTH */
static const char *FS_XBR = FS_HEAD
    "uniform sampler2D edge;\n"
    "vec2 rot(int k, vec2 t) {\n"
    "  if (k == 0) return t;\n"
    "  if (k == 1) return vec2(-t.y, t.x);\n"
    "  if (k == 2) return -t;\n"
    "  return vec2(t.y, -t.x);\n"
    "}\n"
    "vec3 apply(vec3 col, vec2 ip, vec2 f, int k, float flag, float s) {\n"
    "  if (flag < 0.25) return col;\n"
    "  vec3 nc = F(ip + rot(k, flag > 0.75 ? vec2(0.0, 1.0) : vec2(1.0, 0.0)));\n"
    "  return mix(col, nc, clamp((s - 1.125) * 2.0, 0.0, 1.0));\n"
    "}\n"
    "void main() {\n"
    "  vec2 p = clamp(uv * tsize, vec2(0.0), fsize - 0.001);\n"
    "  vec2 ip = floor(p), f = p - ip;\n"
    "  vec4 e = texture2D(edge, (ip + 0.5) / tsize);\n"
    "  vec3 col = F(ip);\n"
    "  col = apply(col, ip, f, 0, e.r, f.x + f.y);\n"
    "  col = apply(col, ip, f, 1, e.g, (1.0 - f.x) + f.y);\n"
    "  col = apply(col, ip, f, 2, e.b, 2.0 - f.x - f.y);\n"
    "  col = apply(col, ip, f, 3, e.a, f.x + (1.0 - f.y));\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

static const char *FS_PIXELS = FS_HEAD
    "uniform vec2 scale;\n"      /* output pixels per source pixel */
    "uniform float bilinear;\n"
    "void main() {\n"
    "  vec2 p = uv * tsize, q = p;\n"
    "  if (bilinear < 0.5) {\n"
    "    vec2 i = floor(p), t = p - i - 0.5;\n"
    "    vec2 r = max(0.5 - 0.5 / scale, 0.0);\n"
    "    q = i + (t - clamp(t, -r, r)) * scale + 0.5;\n"
    "  }\n"
    "  q = clamp(q, vec2(0.5), fsize - 0.5);\n"
    "  gl_FragColor = vec4(texture2D(src, q / tsize).rgb, 1.0);\n"
    "}\n";

static const char *FS_CRT = FS_HEAD
    "uniform vec2 scale;\n"
    "uniform float curve;\n"
    "vec3 lin(vec3 c) { return c * c; }\n"
    "vec3 line(float x, float y) {\n"            /* one scanline, softly filtered across */
    "  float px = x - 0.5, i = floor(px), t = px - i;\n"
    "  t = t * t * (3.0 - 2.0 * t);\n"
    "  y = clamp(y, 0.0, fsize.y - 1.0);\n"
    "  return lin(mix(F(vec2(i, y)), F(vec2(i + 1.0, y)), t));\n"
    "}\n"
    "vec3 beam(float d, vec3 c) {\n"
    "  vec3 s = mix(vec3(0.26), vec3(0.42), sqrt(c));\n"
    "  return c * exp(-d * d / (2.0 * s * s)) / (s * 2.2);\n"
    "}\n"
    "void main() {\n"
    "  vec2 n = uv * tsize / fsize;\n"
    "  float edge = 1.0;\n"
    "  if (curve > 0.5) {\n"
    "    vec2 c = n * 2.0 - 1.0;\n"
    "    c.x *= 1.0 + c.y * c.y * 0.035;\n"
    "    c.y *= 1.0 + c.x * c.x * 0.05;\n"
    "    vec2 a = abs(c);\n"
    "    if (a.x > 1.0 || a.y > 1.0) { gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"
    "    vec2 v = 1.0 - smoothstep(vec2(0.985), vec2(1.0), a);\n"
    "    edge = v.x * v.y;\n"
    "    n = c * 0.5 + 0.5;\n"
    "  }\n"
    "  vec2 p = n * fsize;\n"
    "  float py = p.y - 0.5, y0 = floor(py), dy = py - y0;\n"
    "  vec3 a = line(p.x, y0), b = line(p.x, y0 + 1.0);\n"
    "  vec3 crt = beam(dy, a) + beam(1.0 - dy, b);\n"
    "  vec3 soft = mix(a, b, dy);\n"
    /* scanlines only where they can be drawn (enough output lines per line) */
    "  vec3 col = mix(soft, crt, smoothstep(1.5, 3.0, scale.y));\n"
    "  if (scale.x >= 2.5) {\n"
    "    float m = mod(floor(gl_FragCoord.x), 3.0);\n"
    "    vec3 mask = vec3(m < 0.5 ? 1.0 : 0.72, (m > 0.5 && m < 1.5) ? 1.0 : 0.72, m > 1.5 ? 1.0 : 0.72);\n"
    "    col *= mask * 1.22;\n"
    "  }\n"
    "  vec3 glow = lin(F(p - 0.5 + vec2(-1.5, 0.0))) + lin(F(p - 0.5 + vec2(1.5, 0.0))) +\n"
    "              lin(F(p - 0.5 + vec2(0.0, -1.5))) + lin(F(p - 0.5 + vec2(0.0, 1.5)));\n"
    "  col += glow * 0.03;\n"
    "  gl_FragColor = vec4(sqrt(clamp(col, 0.0, 1.0)) * edge, 1.0);\n"
    "}\n";

/* ---------------------------------------------------------------- setup */
typedef struct { GLuint prog; GLint src, tsize, fsize, scale, extra, edge; } prog_t;
static prog_t P_DITHER, P_EDGE, P_XBR, P_PIXELS, P_CRT;
static GLuint tex_src, tex_dith, tex_edge, fbo_dith, fbo_edge;
static int ready;

static GLuint compile(GLenum type, const char *s, const char *name)
{
    GLuint sh = pCreateShader(type);
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

static int link(prog_t *p, const char *fs, const char *name, const char *extra)
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
    p->extra = extra ? pGetUniformLocation(p->prog, extra) : -1;
    p->edge = pGetUniformLocation(p->prog, "edge");
    return 1;
}

static void new_tex(GLuint *t, int filter)
{
    glGenTextures(1, t);
    glBindTexture(GL_TEXTURE_2D, *t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TW, TH, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0);
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
    if (!link(&P_DITHER, FS_DITHER, "dither", 0) || !link(&P_EDGE, FS_EDGE, "edge", 0) || !link(&P_XBR, FS_XBR, "xbr", 0) ||
        !link(&P_PIXELS, FS_PIXELS, "pixels", "bilinear") || !link(&P_CRT, FS_CRT, "crt", "curve"))
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
    pUniform2f(p->tsize, TW, TH);
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
    quad(0, 0, (float)W, (float)H, (float)W / TW, (float)H / TH);
}

void glfx_draw(const uint32_t *fb, int W, int H, int pitch, int x, int y, int w, int h, int cw, int ch,
               int mode, int bilinear, int dither)
{
    if (!ready) return;
    if (W > TW) W = TW;
    if (H > TH) H = TH;
    glDisable(GL_BLEND);
    glEnable(GL_TEXTURE_2D);
    glColor4f(1, 1, 1, 1);
    glBindTexture(GL_TEXTURE_2D, tex_src);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_BGRA_EXT, GL_UNSIGNED_BYTE, fb);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    GLuint src = tex_src;
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
    quad((float)x, (float)y, (float)w, (float)h, (float)W / TW, (float)H / TH);
    pUseProgram(0);
    pActiveTexture(GL_TEXTURE0);
}
