/*
 * Direct3D 11 picture output: the same filters as the OpenGL path
 * (win/fx_shaders.h, compiled at start-up with d3dcompiler_47.dll, which
 * Windows 10/11 include), shown through a flip-model swap chain (smooth
 * frame pacing, low latency, works with FreeSync/G-Sync in borderless
 * full screen). Falls back to the blt model on older Windows.
 *
 * Built without the Windows SDK: the few COM interfaces used are called
 * through their vtables by method index; the indices follow the
 * declaration order in d3d11.h / dxgi.h and are listed next to each call.
 */
#include <string.h>
#include <stdio.h>
#include "win32.h"
#include "d3dfx.h"
#include "glfx.h"
#include "fx_shaders.h"

typedef long HRESULT;
#define FAILED(h) ((HRESULT)(h) < 0)
#define VT(o) (*(void ***)(o))

typedef struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } GUID_;
static const GUID_ IID_Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };
static const GUID_ IID_Factory = { 0x7b7166ec, 0x21c7, 0x44ae, { 0xb2, 0x1a, 0xc9, 0xae, 0x32, 0x1a, 0xe3, 0x69 } };

typedef struct { UINT Width, Height, RefN, RefD, Format, ScanlineOrdering, Scaling; } MODE_DESC;
typedef struct {
    MODE_DESC BufferDesc; UINT SampleCount, SampleQuality; UINT BufferUsage, BufferCount;
    HWND OutputWindow; BOOL Windowed; UINT SwapEffect, Flags;
} SWAP_DESC;
typedef struct { UINT Width, Height, MipLevels, ArraySize, Format, SampleCount, SampleQuality, Usage, BindFlags, CPUAccessFlags, MiscFlags; } TEX2D_DESC;
typedef struct { UINT ByteWidth, Usage, BindFlags, CPUAccessFlags, MiscFlags, StructureByteStride; } BUF_DESC;
typedef struct { UINT Filter, AddressU, AddressV, AddressW; float MipLODBias; UINT MaxAnisotropy, ComparisonFunc; float Border[4]; float MinLOD, MaxLOD; } SAMP_DESC;
typedef struct { UINT FillMode, CullMode; BOOL FrontCCW; int DepthBias; float DepthBiasClamp, SlopeScaledDepthBias; BOOL DepthClip, Scissor, Multisample, AALine; } RAST_DESC;
typedef struct { float X, Y, W, H, MinD, MaxD; } VIEWPORT;
typedef struct { void *pData; UINT RowPitch, DepthPitch; } MAPPED;
typedef struct { UINT left, top, front, right, bottom, back; } BOX;

enum {
    FMT_UNKNOWN = 0, FMT_R8G8B8A8 = 28, FMT_B8G8R8A8 = 87,
    USAGE_DEFAULT = 0, USAGE_DYNAMIC = 2,
    BIND_CB = 0x4, BIND_SRV = 0x8, BIND_RT = 0x20, CPU_WRITE = 0x10000,
    MAP_WRITE_DISCARD = 4, TOPO_STRIP = 5,
    SWAP_DISCARD = 0, SWAP_FLIP_DISCARD = 4, USAGE_RT_OUTPUT = 0x20,
};

/* d3d11.dll / d3dcompiler entry points */
typedef HRESULT (*create_t)(void *adapter, UINT driver, HMODULE sw, UINT flags, const UINT *levels, UINT nlevels, UINT sdk,
                            const SWAP_DESC *sd, void **swap, void **dev, UINT *level, void **ctx);
typedef HRESULT (*compile_t)(const void *src, size_t len, const char *name, const void *defs, void *inc, const char *entry,
                             const char *target, UINT f1, UINT f2, void **code, void **errs);

/* ---- vtable calls (index = position in the interface, IUnknown is 0-2) ---- */
static ULONG Release(void *o) { return o ? ((ULONG (*)(void *))VT(o)[2])(o) : 0; }
/* ID3DBlob: 3 GetBufferPointer, 4 GetBufferSize */
static void *BlobPtr(void *b) { return ((void *(*)(void *))VT(b)[3])(b); }
static size_t BlobSize(void *b) { return ((size_t (*)(void *))VT(b)[4])(b); }
/* IDXGIObject: 6 GetParent; IDXGIFactory: 8 MakeWindowAssociation */
static HRESULT O_GetParent(void *o, const GUID_ *id, void **out) { return ((HRESULT (*)(void *, const GUID_ *, void **))VT(o)[6])(o, id, out); }
static HRESULT F_MakeWindowAssociation(void *f, HWND w, UINT fl) { return ((HRESULT (*)(void *, HWND, UINT))VT(f)[8])(f, w, fl); }
/* IDXGISwapChain: 8 Present, 9 GetBuffer, 13 ResizeBuffers */
static HRESULT SC_Present(void *s, UINT sync, UINT fl) { return ((HRESULT (*)(void *, UINT, UINT))VT(s)[8])(s, sync, fl); }
static HRESULT SC_GetBuffer(void *s, UINT i, const GUID_ *id, void **o) { return ((HRESULT (*)(void *, UINT, const GUID_ *, void **))VT(s)[9])(s, i, id, o); }
static HRESULT SC_ResizeBuffers(void *s, UINT n, UINT w, UINT h, UINT f, UINT fl) { return ((HRESULT (*)(void *, UINT, UINT, UINT, UINT, UINT))VT(s)[13])(s, n, w, h, f, fl); }
/* ID3D11Device: 3 CreateBuffer, 5 CreateTexture2D, 7 CreateShaderResourceView, 9 CreateRenderTargetView,
 * 12 CreateVertexShader, 15 CreatePixelShader, 22 CreateRasterizerState, 23 CreateSamplerState */
static HRESULT D_CreateBuffer(void *d, const BUF_DESC *de, const void *init, void **o) { return ((HRESULT (*)(void *, const BUF_DESC *, const void *, void **))VT(d)[3])(d, de, init, o); }
static HRESULT D_CreateTexture2D(void *d, const TEX2D_DESC *de, const void *init, void **o) { return ((HRESULT (*)(void *, const TEX2D_DESC *, const void *, void **))VT(d)[5])(d, de, init, o); }
static HRESULT D_CreateSRV(void *d, void *res, const void *de, void **o) { return ((HRESULT (*)(void *, void *, const void *, void **))VT(d)[7])(d, res, de, o); }
static HRESULT D_CreateRTV(void *d, void *res, const void *de, void **o) { return ((HRESULT (*)(void *, void *, const void *, void **))VT(d)[9])(d, res, de, o); }
static HRESULT D_CreateVS(void *d, const void *bc, size_t n, void *lk, void **o) { return ((HRESULT (*)(void *, const void *, size_t, void *, void **))VT(d)[12])(d, bc, n, lk, o); }
static HRESULT D_CreatePS(void *d, const void *bc, size_t n, void *lk, void **o) { return ((HRESULT (*)(void *, const void *, size_t, void *, void **))VT(d)[15])(d, bc, n, lk, o); }
static HRESULT D_CreateRast(void *d, const RAST_DESC *de, void **o) { return ((HRESULT (*)(void *, const RAST_DESC *, void **))VT(d)[22])(d, de, o); }
static HRESULT D_CreateSampler(void *d, const SAMP_DESC *de, void **o) { return ((HRESULT (*)(void *, const SAMP_DESC *, void **))VT(d)[23])(d, de, o); }
/* ID3D11DeviceContext (3-6 are ID3D11DeviceChild): 7 VSSetConstantBuffers, 8 PSSetShaderResources,
 * 9 PSSetShader, 10 PSSetSamplers, 11 VSSetShader, 13 Draw, 14 Map, 15 Unmap, 16 PSSetConstantBuffers,
 * 17 IASetInputLayout, 24 IASetPrimitiveTopology, 33 OMSetRenderTargets, 43 RSSetState,
 * 44 RSSetViewports, 48 UpdateSubresource, 50 ClearRenderTargetView */
static void C_VSSetCB(void *c, UINT s, UINT n, void *const *b) { ((void (*)(void *, UINT, UINT, void *const *))VT(c)[7])(c, s, n, b); }
static void C_PSSetSRV(void *c, UINT s, UINT n, void *const *v) { ((void (*)(void *, UINT, UINT, void *const *))VT(c)[8])(c, s, n, v); }
static void C_PSSetShader(void *c, void *ps) { ((void (*)(void *, void *, void *const *, UINT))VT(c)[9])(c, ps, 0, 0); }
static void C_PSSetSamplers(void *c, UINT s, UINT n, void *const *v) { ((void (*)(void *, UINT, UINT, void *const *))VT(c)[10])(c, s, n, v); }
static void C_VSSetShader(void *c, void *vs) { ((void (*)(void *, void *, void *const *, UINT))VT(c)[11])(c, vs, 0, 0); }
static void C_Draw(void *c, UINT n, UINT start) { ((void (*)(void *, UINT, UINT))VT(c)[13])(c, n, start); }
static HRESULT C_Map(void *c, void *res, UINT sub, UINT type, UINT fl, MAPPED *m) { return ((HRESULT (*)(void *, void *, UINT, UINT, UINT, MAPPED *))VT(c)[14])(c, res, sub, type, fl, m); }
static void C_Unmap(void *c, void *res, UINT sub) { ((void (*)(void *, void *, UINT))VT(c)[15])(c, res, sub); }
static void C_PSSetCB(void *c, UINT s, UINT n, void *const *b) { ((void (*)(void *, UINT, UINT, void *const *))VT(c)[16])(c, s, n, b); }
static void C_IASetInputLayout(void *c, void *l) { ((void (*)(void *, void *))VT(c)[17])(c, l); }
static void C_IASetTopology(void *c, UINT t) { ((void (*)(void *, UINT))VT(c)[24])(c, t); }
static void C_OMSetRT(void *c, UINT n, void *const *rtv, void *dsv) { ((void (*)(void *, UINT, void *const *, void *))VT(c)[33])(c, n, rtv, dsv); }
static void C_RSSetState(void *c, void *s) { ((void (*)(void *, void *))VT(c)[43])(c, s); }
static void C_RSSetViewports(void *c, UINT n, const VIEWPORT *v) { ((void (*)(void *, UINT, const VIEWPORT *))VT(c)[44])(c, n, v); }
static void C_Update(void *c, void *res, UINT sub, const BOX *b, const void *data, UINT rp, UINT dp) { ((void (*)(void *, void *, UINT, const BOX *, const void *, UINT, UINT))VT(c)[48])(c, res, sub, b, data, rp, dp); }
static void C_ClearRTV(void *c, void *rtv, const float col[4]) { ((void (*)(void *, void *, const float *))VT(c)[50])(c, rtv, col); }

/* ---- state ---- */
#define TW 512
#define TH 256
static char err[700], info[200];
const char *d3d_error(void) { return err; }
const char *d3d_info(void) { return info; }

static void *dev, *ctx, *swap, *bb_rtv;
static int bb_w, bb_h, flip_model;
static void *tex_src, *srv_src;                   /* the frame (dynamic, CPU writes) */
static void *tex_big, *srv_big;                   /* HD pack pictures, up to 2048 x 1024 */
static int tw = TW, th = TH;                      /* size of the texture holding the current picture */
static void *tex_dith, *srv_dith, *rtv_dith;      /* dithering blended */
static void *tex_edge, *srv_edge, *rtv_edge;      /* FX_SMOOTH pass 1 */
static void *tex_ovl, *srv_ovl;                   /* overlays (menu), window sized */
static int ovl_w, ovl_h;
static void *cbuf, *samp, *rast, *vs;
static void *ps_dither, *ps_edge, *ps_xbr, *ps_pixels, *ps_crt, *ps_copy, *ps_back, *ps_down;
static void *tex_bd, *srv_bd, *rtv_bd;            /* the picture shrunk for the side bars */

/* constant buffer, as declared in HLSL_HEAD */
typedef struct { float rect[4], uvx[4], tsize[2], fsize[2], scale[2], extra, pad; } consts_t;

/* HLSL spelling of the shared shader dialect (win/fx_shaders.h) */
static const char *HLSL_HEAD =
    "cbuffer K : register(b0) { float4 rect; float4 uvx; float2 tsize; float2 fsize; float2 scale; float extra; float kpad; };\n"
    "Texture2D src_t : register(t0);\nTexture2D edge_t : register(t1);\nSamplerState smp : register(s0);\n"
    "#define EQ3(a, b) all((a) == (b))\n"
    "#define TEX(u) src_t.SampleLevel(smp, (u), 0)\n#define TEXE(u) edge_t.SampleLevel(smp, (u), 0)\n"
    "static float2 uv;\nstatic float4 fragpos;\n#define FRAGX fragpos.x\n";
static const char *HLSL_TAIL =
    "float4 ps(float4 pos : SV_Position, float2 t : TEXCOORD0) : SV_Target { uv = t; fragpos = pos; return body(); }\n";
static const char *HLSL_VS =
    "struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VO vs(uint id : SV_VertexID) {\n"
    "  float2 t = float2(id & 1, id >> 1);\n"
    "  VO o;\n"
    "  o.pos = float4(lerp(rect.x, rect.z, t.x), lerp(rect.y, rect.w, t.y), 0.0, 1.0);\n"
    "  o.uv = uvx.zw + t * uvx.xy;\n"
    "  return o;\n"
    "}\n";

static compile_t d3dcompile;

static void *compile(const char *body, const char *name, int is_vs)
{
    static char src[16384];
    snprintf(src, sizeof src, "%s%s%s", HLSL_HEAD, body, is_vs ? "" : HLSL_TAIL);
    void *code = 0, *errs = 0, *sh = 0;
    HRESULT h = d3dcompile(src, strlen(src), name, 0, 0, is_vs ? "vs" : "ps", is_vs ? "vs_4_0" : "ps_4_0", 1 << 15, 0, &code, &errs);
    if (FAILED(h) || !code) {
        snprintf(err, sizeof err, "shader %s: %.500s", name, errs ? (const char *)BlobPtr(errs) : "compile failed");
        Release(errs);
        return 0;
    }
    Release(errs);
    h = is_vs ? D_CreateVS(dev, BlobPtr(code), BlobSize(code), 0, &sh) : D_CreatePS(dev, BlobPtr(code), BlobSize(code), 0, &sh);
    Release(code);
    if (FAILED(h)) { snprintf(err, sizeof err, "create shader %s: %08lX", name, (unsigned long)h); return 0; }
    return sh;
}

static int make_tex(void **tex, void **srv, void **rtv, UINT w, UINT h, UINT fmt, int dynamic)
{
    TEX2D_DESC d; memset(&d, 0, sizeof d);
    d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = fmt; d.SampleCount = 1;
    d.Usage = dynamic ? USAGE_DYNAMIC : USAGE_DEFAULT;
    d.BindFlags = BIND_SRV | (rtv ? BIND_RT : 0);
    d.CPUAccessFlags = dynamic ? CPU_WRITE : 0;
    HRESULT hr = D_CreateTexture2D(dev, &d, 0, tex);
    if (FAILED(hr)) { snprintf(err, sizeof err, "CreateTexture2D %ux%u: %08lX", w, h, (unsigned long)hr); return 0; }
    if (FAILED(D_CreateSRV(dev, *tex, 0, srv))) { snprintf(err, sizeof err, "CreateShaderResourceView failed"); return 0; }
    if (rtv && FAILED(D_CreateRTV(dev, *tex, 0, rtv))) { snprintf(err, sizeof err, "CreateRenderTargetView failed"); return 0; }
    return 1;
}

static int make_backbuffer_rtv(void)
{
    void *bb = 0;
    HRESULT hr = SC_GetBuffer(swap, 0, &IID_Texture2D, &bb);
    if (FAILED(hr)) { snprintf(err, sizeof err, "GetBuffer: %08lX", (unsigned long)hr); return 0; }
    hr = D_CreateRTV(dev, bb, 0, &bb_rtv);
    Release(bb);
    if (FAILED(hr)) { snprintf(err, sizeof err, "back buffer view: %08lX", (unsigned long)hr); return 0; }
    return 1;
}

static int init2(void *hwnd);
int d3d_init(void *hwnd)
{
    if (init2(hwnd)) return 1;
    /* give the window back clean, so OpenGL can take it */
    Release(ctx); Release(swap); Release(dev);
    ctx = swap = dev = 0;
    return 0;
}

static int init2(void *hwnd)
{
    err[0] = 0;
    HMODULE m = LoadLibraryW(L"d3d11.dll");
    create_t create = m ? (create_t)GetProcAddress(m, "D3D11CreateDeviceAndSwapChain") : 0;
    if (!create) { snprintf(err, sizeof err, "d3d11.dll not found"); return 0; }
    HMODULE cm = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!cm) cm = LoadLibraryW(L"d3dcompiler_43.dll");
    d3dcompile = cm ? (compile_t)GetProcAddress(cm, "D3DCompile") : 0;
    if (!d3dcompile) { snprintf(err, sizeof err, "d3dcompiler_47.dll not found"); return 0; }

    RECT r; GetClientRect((HWND)hwnd, &r);
    bb_w = r.right - r.left; bb_h = r.bottom - r.top;
    if (bb_w < 1) bb_w = 1;
    if (bb_h < 1) bb_h = 1;
    static const UINT levels[3] = { 0xb000, 0xa100, 0xa000 };   /* 11_0, 10_1, 10_0 */
    UINT got = 0;
    HRESULT hr = -1;
    for (int attempt = 0; attempt < 2 && FAILED(hr); attempt++) {
        SWAP_DESC sd; memset(&sd, 0, sizeof sd);
        sd.BufferDesc.Width = (UINT)bb_w; sd.BufferDesc.Height = (UINT)bb_h; sd.BufferDesc.Format = FMT_B8G8R8A8;
        sd.SampleCount = 1; sd.BufferUsage = USAGE_RT_OUTPUT; sd.OutputWindow = (HWND)hwnd; sd.Windowed = 1;
        flip_model = attempt == 0;
        sd.BufferCount = flip_model ? 2 : 1;
        sd.SwapEffect = flip_model ? SWAP_FLIP_DISCARD : SWAP_DISCARD;
        hr = create(0, 1 /* HARDWARE */, 0, 0, levels, 3, 7 /* SDK version */, &sd, &swap, &dev, &got, &ctx);
    }
    if (FAILED(hr)) { snprintf(err, sizeof err, "D3D11CreateDeviceAndSwapChain: %08lX", (unsigned long)hr); return 0; }
    snprintf(info, sizeof info, "feature level %X.%X, %s", got >> 12, (got >> 8) & 15, flip_model ? "flip model" : "blt model");

    /* Alt+Enter is ours (borderless full screen), not DXGI's exclusive mode */
    void *factory = 0;
    if (!FAILED(O_GetParent(swap, &IID_Factory, &factory)) && factory) {
        F_MakeWindowAssociation(factory, (HWND)hwnd, 2 /* DXGI_MWA_NO_ALT_ENTER */);
        Release(factory);
    }
    if (!make_backbuffer_rtv()) return 0;
    if (!make_tex(&tex_src, &srv_src, 0, TW, TH, FMT_B8G8R8A8, 1)) return 0;
    if (!make_tex(&tex_dith, &srv_dith, &rtv_dith, TW, TH, FMT_R8G8B8A8, 0)) return 0;
    if (!make_tex(&tex_edge, &srv_edge, &rtv_edge, TW, TH, FMT_R8G8B8A8, 0)) return 0;
    if (!make_tex(&tex_bd, &srv_bd, &rtv_bd, TW, TH, FMT_R8G8B8A8, 0)) return 0;

    BUF_DESC bd; memset(&bd, 0, sizeof bd);
    bd.ByteWidth = sizeof(consts_t); bd.Usage = USAGE_DYNAMIC; bd.BindFlags = BIND_CB; bd.CPUAccessFlags = CPU_WRITE;
    if (FAILED(D_CreateBuffer(dev, &bd, 0, &cbuf))) { snprintf(err, sizeof err, "constant buffer"); return 0; }
    SAMP_DESC sm; memset(&sm, 0, sizeof sm);
    sm.Filter = 0x15 /* MIN_MAG_MIP_LINEAR */; sm.AddressU = sm.AddressV = sm.AddressW = 3 /* CLAMP */;
    sm.MaxAnisotropy = 1; sm.ComparisonFunc = 1 /* NEVER */; sm.MaxLOD = 3.4e38f;
    if (FAILED(D_CreateSampler(dev, &sm, &samp))) { snprintf(err, sizeof err, "sampler"); return 0; }
    RAST_DESC rd; memset(&rd, 0, sizeof rd);
    rd.FillMode = 3 /* SOLID */; rd.CullMode = 1 /* NONE */; rd.DepthClip = 1;
    if (FAILED(D_CreateRast(dev, &rd, &rast))) { snprintf(err, sizeof err, "rasterizer state"); return 0; }

    if (!(vs = compile(HLSL_VS, "vs", 1))) return 0;
    if (!(ps_dither = compile(SH_DITHER, "dither", 0)) || !(ps_edge = compile(SH_EDGE, "edge", 0)) ||
        !(ps_xbr = compile(SH_XBR, "xbr", 0)) || !(ps_pixels = compile(SH_PIXELS, "pixels", 0)) ||
        !(ps_crt = compile(SH_CRT, "crt", 0)) || !(ps_copy = compile(SH_COPY, "copy", 0)) ||
        !(ps_back = compile(SH_BACKDROP, "backdrop", 0)) || !(ps_down = compile(SH_DOWN, "down", 0)))
        return 0;
    return 1;
}

/* ---- drawing ---- */
static void resize_if_needed(int cw, int ch)
{
    if (cw == bb_w && ch == bb_h) return;
    C_OMSetRT(ctx, 0, 0, 0);
    Release(bb_rtv); bb_rtv = 0;
    HRESULT hr = SC_ResizeBuffers(swap, 0, (UINT)cw, (UINT)ch, FMT_UNKNOWN, 0);
    if (FAILED(hr)) snprintf(err, sizeof err, "ResizeBuffers: %08lX", (unsigned long)hr);
    bb_w = cw; bb_h = ch;
    make_backbuffer_rtv();
}

static void set_consts(float x0, float y0, float x1, float y1, float u, float v, float u0, float v0,
                       int W, int H, float sx, float sy, float extra)
{
    MAPPED m;
    if (FAILED(C_Map(ctx, cbuf, 0, MAP_WRITE_DISCARD, 0, &m))) return;
    consts_t *k = (consts_t *)m.pData;
    k->rect[0] = x0; k->rect[1] = y0; k->rect[2] = x1; k->rect[3] = y1;
    k->uvx[0] = u; k->uvx[1] = v; k->uvx[2] = u0; k->uvx[3] = v0;
    k->tsize[0] = (float)tw; k->tsize[1] = (float)th; k->fsize[0] = (float)W; k->fsize[1] = (float)H;
    k->scale[0] = sx; k->scale[1] = sy; k->extra = extra; k->pad = 0;
    C_Unmap(ctx, cbuf, 0);
}

/* one full-quad pass; the target is bound before the inputs, so a texture
 * that was the previous target is released from output first */
static void pass(void *rtv, int vw, int vh, void *ps, void *in0, void *in1)
{
    void *none[2] = { 0, 0 };
    C_PSSetSRV(ctx, 0, 2, none);
    C_OMSetRT(ctx, 1, &rtv, 0);
    VIEWPORT vp = { 0, 0, (float)vw, (float)vh, 0, 1 };
    C_RSSetViewports(ctx, 1, &vp);
    C_IASetInputLayout(ctx, 0);
    C_IASetTopology(ctx, TOPO_STRIP);
    C_RSSetState(ctx, rast);
    C_VSSetShader(ctx, vs);
    C_PSSetShader(ctx, ps);
    C_VSSetCB(ctx, 0, 1, &cbuf);
    C_PSSetCB(ctx, 0, 1, &cbuf);
    C_PSSetSamplers(ctx, 0, 1, &samp);
    void *in[2] = { in0, in1 };
    C_PSSetSRV(ctx, 0, 2, in);
    C_Draw(ctx, 4, 0);
}

void d3d_frame(const uint32_t *fb, int W, int H, int pitch, int x, int y, int w, int h, int cw, int ch,
               int mode, int bilinear, int dither, int backdrop)
{
    if (!ctx) return;
    if (cw < 1 || ch < 1) return;
    resize_if_needed(cw, ch);
    if (!bb_rtv) return;
    void *up_tex = tex_src, *up_srv = srv_src;
    tw = TW; th = TH;
    if (W > TW || H > TH) {                       /* an HD pack picture: sharp or soft scaling only */
        if (!tex_big && !make_tex(&tex_big, &srv_big, 0, 2048, 1024, FMT_B8G8R8A8, 1)) { tex_big = 0; return; }
        up_tex = tex_big; up_srv = srv_big; tw = 2048; th = 1024;
        mode = FX_PIXELS; dither = 0;
    }
    if (W > tw) W = tw;
    if (H > th) H = th;

    MAPPED m;
    if (!FAILED(C_Map(ctx, up_tex, 0, MAP_WRITE_DISCARD, 0, &m))) {
        for (int r = 0; r < H; r++) memcpy((uint8_t *)m.pData + (size_t)r * m.RowPitch, fb + (size_t)r * pitch, (size_t)W * 4);
        C_Unmap(ctx, up_tex, 0);
    }
    float u = (float)W / tw, v = (float)H / th;
    void *src = up_srv;
    if (dither) {
        set_consts(-1, 1, 1, -1, u, v, 0, 0, W, H, 1, 1, 0);
        pass(rtv_dith, W, H, ps_dither, src, 0);
        src = srv_dith;
    }
    if (mode == FX_SMOOTH) {
        set_consts(-1, 1, 1, -1, u, v, 0, 0, W, H, 1, 1, 0);
        pass(rtv_edge, W, H, ps_edge, src, 0);
    }
    int bw = 0, bh = 0;
    if (backdrop && (w < cw - 1 || h < ch - 1)) {     /* the picture shrunk 8x for the bars */
        int B = 8 * (H / 224 > 0 ? H / 224 : 1);
        bw = (W + B - 1) / B; bh = (H + B - 1) / B;
        set_consts(-1, 1, 1, -1, u, v, 0, 0, W, H, 1, 1, (float)B);
        pass(rtv_bd, bw, bh, ps_down, up_srv, 0);
    }
    static const float black[4] = { 0, 0, 0, 1 };
    C_OMSetRT(ctx, 1, &bb_rtv, 0);
    C_ClearRTV(ctx, bb_rtv, black);
    float x0 = 2.0f * x / cw - 1, x1 = 2.0f * (x + w) / cw - 1, y0 = 1 - 2.0f * y / ch, y1 = 1 - 2.0f * (y + h) / ch;
    float sx = (float)w / W, sy = (float)h / H;
    if (bw) {
        float fx, fy; glfx_cover(w, h, cw, ch, &fx, &fy);
        int stw = tw, sth = th; tw = TW; th = TH;      /* the small picture lives in a 512 x 256 texture */
        float su = (float)bw / TW, sv = (float)bh / TH;
        set_consts(-1, 1, 1, -1, fx * su, fy * sv, (1 - fx) / 2 * su, (1 - fy) / 2 * sv, bw, bh, 1, 1, 0);
        tw = stw; th = sth;
        pass(bb_rtv, cw, ch, ps_back, srv_bd, 0);
    }
    if (mode == FX_SMOOTH) {
        set_consts(x0, y0, x1, y1, u, v, 0, 0, W, H, sx, sy, 0);
        pass(bb_rtv, cw, ch, ps_xbr, src, srv_edge);
    } else if (mode == FX_CRT || mode == FX_CRT_CURVED) {
        set_consts(x0, y0, x1, y1, u, v, 0, 0, W, H, sx, sy, mode == FX_CRT_CURVED ? 1.0f : 0.0f);
        pass(bb_rtv, cw, ch, ps_crt, src, 0);
    } else {
        set_consts(x0, y0, x1, y1, u, v, 0, 0, W, H, sx, sy, bilinear ? 1.0f : 0.0f);
        pass(bb_rtv, cw, ch, ps_pixels, src, 0);
    }
}

void d3d_overlay(const uint32_t *bits, int bw, int bh, int rx, int ry, int rw, int rh)
{
    if (!ctx || !bb_rtv || rw <= 0 || rh <= 0) return;
    if (!tex_ovl || ovl_w != bw || ovl_h != bh) {
        Release(srv_ovl); Release(tex_ovl); srv_ovl = tex_ovl = 0;
        if (!make_tex(&tex_ovl, &srv_ovl, 0, (UINT)bw, (UINT)bh, FMT_B8G8R8A8, 0)) { tex_ovl = 0; return; }
        ovl_w = bw; ovl_h = bh;
    }
    BOX b = { (UINT)rx, (UINT)ry, 0, (UINT)(rx + rw), (UINT)(ry + rh), 1 };
    C_Update(ctx, tex_ovl, 0, &b, bits + (size_t)ry * bw + rx, (UINT)bw * 4, 0);
    float x0 = 2.0f * rx / bb_w - 1, x1 = 2.0f * (rx + rw) / bb_w - 1, y0 = 1 - 2.0f * ry / bb_h, y1 = 1 - 2.0f * (ry + rh) / bb_h;
    set_consts(x0, y0, x1, y1, (float)rw / bw, (float)rh / bh, (float)rx / bw, (float)ry / bh, bw, bh, 1, 1, 0);
    pass(bb_rtv, bb_w, bb_h, ps_copy, srv_ovl, 0);
}

int d3d_present(int vsync)
{
    if (!swap) return 0;
    HRESULT hr = SC_Present(swap, vsync ? 1 : 0, 0);
    if (hr == (HRESULT)0x887A0005 || hr == (HRESULT)0x887A0007) {   /* device removed / reset */
        snprintf(err, sizeof err, "device lost: %08lX", (unsigned long)hr);
        return 0;
    }
    return 1;
}
