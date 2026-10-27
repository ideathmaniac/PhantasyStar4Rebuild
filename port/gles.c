/*
 * OpenGL ES 2 under the front end's OpenGL path (PS4_GLES, phones):
 *  - the few desktop GL 1.1 calls it makes (glBegin/glEnd quads, glOrtho,
 *    glColor) are emulated with client-side vertex arrays and, when no
 *    shader program is in use, a small built-in one;
 *  - textures are always RGBA: BGRA pictures are swizzled on upload, and
 *    GL_UNPACK_ROW_LENGTH (not in ES 2) is done by copying the rows;
 *  - the viewport of the window is moved up when the touch controls take a
 *    panel under the picture (port/sdl_win32.c, gles_set_offset);
 *  - helpers to draw the touch controls on top before the buffers swap.
 * Every GL function comes from SDL_GL_GetProcAddress, so nothing links
 * against a GL library.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

typedef unsigned int GLenum, GLuint, GLbitfield; typedef int GLint, GLsizei; typedef float GLfloat, GLclampf;
typedef double GLdouble; typedef unsigned char GLboolean; typedef char GLchar;

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP 0x2900
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_BGRA_EXT 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PROJECTION 0x1701
#define GL_QUADS 0x0007
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_FAN 0x0006
#define GL_FLOAT 0x1406
#define GL_FRAMEBUFFER 0x8D40
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_TEXTURE0 0x84C0
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800

static struct {
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*ClearColor)(GLclampf, GLclampf, GLclampf, GLclampf);
    void (*Clear)(GLbitfield);
    void (*GenTextures)(GLsizei, GLuint *);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*BlendFunc)(GLenum, GLenum);
    void (*PixelStorei)(GLenum, GLint);
    const unsigned char *(*GetString)(GLenum);
    GLenum (*GetError)(void);
    void (*GetIntegerv)(GLenum, GLint *);
    void (*BindFramebuffer)(GLenum, GLuint);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
    void (*CompileShader)(GLuint);
    GLuint (*CreateProgram)(void);
    void (*AttachShader)(GLuint, GLuint);
    void (*BindAttribLocation)(GLuint, GLuint, const GLchar *);
    void (*LinkProgram)(GLuint);
    void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar *);
    void (*Uniform1i)(GLint, GLint);
    void (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat *);
    void (*EnableVertexAttribArray)(GLuint);
    void (*DisableVertexAttribArray)(GLuint);
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*ActiveTexture)(GLenum);
    void (*DeleteTextures)(GLsizei, const GLuint *);
} G;
static int ready;

int gles_init(void)
{
#define P(f) if (!(*(void **)&G.f = SDL_GL_GetProcAddress("gl" #f))) return 0
    P(Viewport); P(ClearColor); P(Clear); P(GenTextures); P(BindTexture); P(TexParameteri); P(TexImage2D);
    P(TexSubImage2D); P(Enable); P(Disable); P(BlendFunc); P(PixelStorei); P(GetString); P(GetError);
    P(GetIntegerv); P(BindFramebuffer); P(CreateShader); P(ShaderSource); P(CompileShader); P(CreateProgram);
    P(AttachShader); P(BindAttribLocation); P(LinkProgram); P(UseProgram); P(GetUniformLocation); P(Uniform1i);
    P(Uniform4f); P(UniformMatrix4fv); P(EnableVertexAttribArray); P(DisableVertexAttribArray);
    P(VertexAttribPointer); P(DrawArrays); P(ActiveTexture); P(DeleteTextures);
#undef P
    ready = 1;
    return 1;
}

/* ---------------------------------------------------------------- state */
static int off_x, off_y;          /* where the picture's area starts in the window (GL: from the bottom) */
static GLuint cur_fbo;
static int row_length;
static int mat_mode;
static float proj[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static float col[4] = { 1, 1, 1, 1 };

void gles_set_offset(int x, int y) { off_x = x; off_y = y; }

static void bind_fb(GLenum t, GLuint f) { cur_fbo = f; G.BindFramebuffer(t, f); }
/* glfx asks for glBindFramebuffer through the proc-address function */
void *gles_proc(const char *name)
{
    if (!strcmp(name, "glBindFramebuffer")) return (void *)bind_fb;
    return SDL_GL_GetProcAddress(name);
}

void port_glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    if (!cur_fbo) { x += off_x; y += off_y; }
    G.Viewport(x, y, w, h);
}
void port_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) { G.ClearColor(r, g, b, a); }
void port_glClear(GLbitfield m) { G.Clear(m); }
void port_glGenTextures(GLsizei n, GLuint *t) { G.GenTextures(n, t); }
void port_glBindTexture(GLenum a, GLuint t) { G.BindTexture(a, t); }
void port_glTexParameteri(GLenum t, GLenum p, GLint v)
{
    if (v == GL_CLAMP) v = GL_CLAMP_TO_EDGE;
    G.TexParameteri(t, p, v);
}
/* rows of a BGRA picture as tightly packed RGBA */
static uint32_t *conv; static size_t conv_n;
static const void *to_rgba(const void *data, int w, int h, GLenum fmt)
{
    if (!data) return 0;
    int stride = row_length > 0 ? row_length : w;
    if (fmt != GL_BGRA_EXT && stride == w) return data;
    size_t n = (size_t)w * h;
    if (n > conv_n) { free(conv); conv = malloc(n * 4); conv_n = conv ? n : 0; if (!conv) return 0; }
    const uint32_t *s = data;
    for (int y = 0; y < h; y++) {
        const uint32_t *r = s + (size_t)y * stride; uint32_t *o = conv + (size_t)y * w;
        if (fmt == GL_BGRA_EXT)
            for (int x = 0; x < w; x++) { uint32_t p = r[x]; o[x] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16); }
        else memcpy(o, r, (size_t)w * 4);
    }
    return conv;
}
void port_glTexImage2D(GLenum t, GLint lvl, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum fmt, GLenum type, const void *d)
{
    (void)ifmt;
    G.TexImage2D(t, lvl, GL_RGBA, w, h, b, GL_RGBA, type, to_rgba(d, w, h, fmt));
}
void port_glTexSubImage2D(GLenum t, GLint lvl, GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void *d)
{
    if (w <= 0 || h <= 0) return;
    G.TexSubImage2D(t, lvl, x, y, w, h, GL_RGBA, type, to_rgba(d, w, h, fmt));
}
void port_glEnable(GLenum c) { if (c != GL_TEXTURE_2D) G.Enable(c); }
void port_glDisable(GLenum c) { if (c != GL_TEXTURE_2D) G.Disable(c); }
void port_glBlendFunc(GLenum a, GLenum b) { G.BlendFunc(a, b); }
void port_glPixelStorei(GLenum p, GLint v)
{
    if (p == GL_UNPACK_ROW_LENGTH) { row_length = v; return; }
    G.PixelStorei(p, v);
}
const unsigned char *port_glGetString(GLenum n) { return G.GetString(n); }
GLenum port_glGetError(void) { return G.GetError(); }

/* the projection only: the front end leaves the model-view matrix at identity */
void port_glMatrixMode(GLenum m) { mat_mode = m == GL_PROJECTION; }
void port_glLoadIdentity(void)
{
    if (!mat_mode) return;
    memset(proj, 0, sizeof proj); proj[0] = proj[5] = proj[10] = proj[15] = 1;
}
void port_glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    if (!mat_mode) return;
    memset(proj, 0, sizeof proj);
    proj[0] = (float)(2 / (r - l)); proj[5] = (float)(2 / (t - b)); proj[10] = (float)(-2 / (f - n));
    proj[12] = (float)(-(r + l) / (r - l)); proj[13] = (float)(-(t + b) / (t - b)); proj[14] = (float)(-(f + n) / (f - n));
    proj[15] = 1;
}
void port_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { col[0] = r; col[1] = g; col[2] = b; col[3] = a; }

/* ---------------------------------------------------------------- programs */
static GLuint P_TEX, P_COL;
static GLint P_TEX_mvp, P_TEX_col, P_TEX_src, P_COL_mvp, P_COL_col;
static GLuint build(const char *vs, const char *fs)
{
    GLuint v = G.CreateShader(GL_VERTEX_SHADER), f = G.CreateShader(GL_FRAGMENT_SHADER), p;
    G.ShaderSource(v, 1, &vs, 0); G.CompileShader(v);
    G.ShaderSource(f, 1, &fs, 0); G.CompileShader(f);
    p = G.CreateProgram();
    G.AttachShader(p, v); G.AttachShader(p, f);
    G.BindAttribLocation(p, 0, "a_pos"); G.BindAttribLocation(p, 1, "a_tc");
    G.LinkProgram(p);
    return p;
}
static void programs(void)
{
    if (P_TEX) return;
    static const char *vs =
        "attribute vec4 a_pos; attribute vec2 a_tc; uniform mat4 u_mvp; varying vec2 uv;\n"
        "void main() { gl_Position = u_mvp * a_pos; uv = a_tc; }\n";
    P_TEX = build(vs, "precision mediump float; uniform sampler2D src; uniform vec4 u_col; varying vec2 uv;\n"
                      "void main() { gl_FragColor = texture2D(src, uv) * u_col; }\n");
    P_COL = build(vs, "precision mediump float; uniform vec4 u_col; varying vec2 uv;\n"
                      "void main() { gl_FragColor = u_col; }\n");
    P_TEX_mvp = G.GetUniformLocation(P_TEX, "u_mvp"); P_TEX_col = G.GetUniformLocation(P_TEX, "u_col");
    P_TEX_src = G.GetUniformLocation(P_TEX, "src");
    P_COL_mvp = G.GetUniformLocation(P_COL, "u_mvp"); P_COL_col = G.GetUniformLocation(P_COL, "u_col");
}

/* ---------------------------------------------------------------- glBegin / glEnd */
static float vpos[64][2], vtc[64][2], cur_tc[2];
static int nv, prim;
void port_glBegin(GLenum p) { prim = p; nv = 0; }
void port_glTexCoord2f(GLfloat u, GLfloat v) { cur_tc[0] = u; cur_tc[1] = v; }
void port_glVertex2f(GLfloat x, GLfloat y)
{
    if (nv >= 64) return;
    vpos[nv][0] = x; vpos[nv][1] = y; vtc[nv][0] = cur_tc[0]; vtc[nv][1] = cur_tc[1]; nv++;
}
static void attribs(const float *pos, const float *tc)
{
    G.EnableVertexAttribArray(0);
    G.VertexAttribPointer(0, 2, GL_FLOAT, 0, 0, pos);
    if (tc) { G.EnableVertexAttribArray(1); G.VertexAttribPointer(1, 2, GL_FLOAT, 0, 0, tc); }
    else G.DisableVertexAttribArray(1);
}
void port_glEnd(void)
{
    if (!ready || nv < 3) return;
    GLint prog = 0; G.GetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (!prog) {
        programs();
        G.UseProgram(P_TEX);
        G.UniformMatrix4fv(P_TEX_mvp, 1, 0, proj);
        G.Uniform4f(P_TEX_col, col[0], col[1], col[2], col[3]);
        G.Uniform1i(P_TEX_src, 0);
    } else {
        GLint m = G.GetUniformLocation((GLuint)prog, "u_mvp");
        if (m >= 0) G.UniformMatrix4fv(m, 1, 0, proj);
    }
    attribs(&vpos[0][0], &vtc[0][0]);
    if (prim == GL_QUADS) for (int i = 0; i + 3 < nv; i += 4) G.DrawArrays(GL_TRIANGLE_FAN, i, 4);
    else G.DrawArrays(GL_TRIANGLE_FAN, 0, nv);
    if (!prog) G.UseProgram(0);
    nv = 0;
}

/* ---------------------------------------------------------------- touch controls */
static int ov_w, ov_h;
/* start drawing over the whole window (pixels, y down) */
void gles_overlay_begin(int w, int h)
{
    if (!ready) return;
    programs();
    ov_w = w; ov_h = h;
    G.BindFramebuffer(GL_FRAMEBUFFER, 0); cur_fbo = 0;
    G.Viewport(0, 0, w, h);
    G.Enable(GL_BLEND); G.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    memset(proj, 0, sizeof proj);
    proj[0] = 2.0f / w; proj[5] = -2.0f / h; proj[10] = -1; proj[12] = -1; proj[13] = 1; proj[15] = 1;
}
void gles_overlay_end(void)
{
    if (!ready) return;
    G.Disable(GL_BLEND);
    G.UseProgram(0);
}
/* triangles in one colour (0..255 components) */
void gles_tris(const float *xy, int n, int r, int g, int b, int a)
{
    if (!ready || n < 3) return;
    G.UseProgram(P_COL);
    G.UniformMatrix4fv(P_COL_mvp, 1, 0, proj);
    G.Uniform4f(P_COL_col, r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    attribs(xy, 0);
    G.DrawArrays(GL_TRIANGLES, 0, n);
}
/* a texture from 32-bit ARGB pixels (alpha in the top byte) */
unsigned gles_texture(const uint32_t *argb, int w, int h)
{
    if (!ready) return 0;
    GLuint t; G.GenTextures(1, &t);
    G.BindTexture(GL_TEXTURE_2D, t);
    G.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    G.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    G.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    G.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    int save = row_length; row_length = 0;
    G.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, to_rgba(argb, w, h, GL_BGRA_EXT));
    row_length = save;
    return t;
}
void gles_texture_free(unsigned t) { if (ready && t) { GLuint x = t; G.DeleteTextures(1, &x); } }
void gles_image(unsigned t, float x, float y, float w, float h, int alpha)
{
    if (!ready) return;
    float xy[8] = { x, y, x + w, y, x + w, y + h, x, y + h }, uv[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    G.UseProgram(P_TEX);
    G.UniformMatrix4fv(P_TEX_mvp, 1, 0, proj);
    G.Uniform4f(P_TEX_col, 1, 1, 1, alpha / 255.0f);
    G.Uniform1i(P_TEX_src, 0);
    G.ActiveTexture(GL_TEXTURE0);
    G.BindTexture(GL_TEXTURE_2D, t);
    attribs(xy, uv);
    G.DrawArrays(GL_TRIANGLE_FAN, 0, 4);
}
