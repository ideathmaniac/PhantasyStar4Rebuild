/*
 * The picture filters, written once for both OpenGL (GLSL 1.10) and
 * Direct3D 11 (HLSL, shader model 4). The bodies use HLSL names (float2,
 * lerp, frac) and a few macros; each back end puts its own header in front:
 *   TEX(uv), TEXE(uv)  sample the picture / the edge texture
 *   EQ3(a, b)          all three components equal
 *   FRAGX              output pixel x
 *   uv                 texture coordinate of this output pixel
 * Uniforms: tsize (texture size), fsize (picture size in pixels), scale
 * (output pixels per picture pixel), extra (mode switch of each filter).
 * Every body defines float4 body(). See win/glfx.c for what each one does.
 * Names avoided because HLSL reserves them: line, point, sample, linear.
 */
#ifndef PS4_FX_SHADERS_H
#define PS4_FX_SHADERS_H

#define FX_COMMON \
    "float3 F(float2 p) { p = clamp(p, float2(0.0, 0.0), fsize - 1.0); return TEX((p + 0.5) / tsize).rgb; }\n"

#define FX_ROT \
    "float2 rot(int k, float2 t) {\n" \
    "  if (k == 0) return t;\n" \
    "  if (k == 1) return float2(-t.y, t.x);\n" \
    "  if (k == 2) return -t;\n" \
    "  return float2(t.y, -t.x);\n" \
    "}\n"

static const char *SH_DITHER = FX_COMMON
    "float D(float3 a, float3 b) { float3 d = abs(a - b); return d.r + d.g + d.b; }\n"
    "float4 body() {\n"
    "  float2 p = floor(uv * tsize);\n"
    "  float3 c = F(p), l = F(p + float2(-1.0, 0.0)), r = F(p + float2(1.0, 0.0));\n"
    "  float3 l2 = F(p + float2(-2.0, 0.0)), r2 = F(p + float2(2.0, 0.0));\n"
    "  float3 o = c;\n"
    "  if (D(l, r) < 0.01 && D(c, l) > 0.01 && D(c, l) < 0.9 && (D(c, l2) < 0.01 || D(c, r2) < 0.01)) o = lerp(c, l, 0.5);\n"
    "  return float4(o, 1.0);\n"
    "}\n";

/* pass 1 of FX_SMOOTH: per corner 0 = no edge, 0.5 = blend toward the F
 * neighbour, 1.0 = toward the H neighbour (names as in src/hdscale.c) */
static const char *SH_EDGE = FX_COMMON FX_ROT
    "float3 Y(float3 c) { c *= 255.0; return float3(dot(c, float3(0.299, 0.587, 0.114)), dot(c, float3(-0.169, -0.331, 0.5)), dot(c, float3(0.5, -0.419, -0.081))); }\n"
    "float dist(float3 a, float3 b) { float3 d = abs(Y(a) - Y(b)); return d.x * 48.0 + d.y * 7.0 + d.z * 6.0; }\n"
    "float corner(float2 p, int k) {\n"
    "  float3 e = F(p), f = F(p + rot(k, float2(1.0, 0.0))), h = F(p + rot(k, float2(0.0, 1.0)));\n"
    "  if (EQ3(f, e) || EQ3(h, e)) return 0.0;\n"
    "  float3 i = F(p + rot(k, float2(1.0, 1.0))), c = F(p + rot(k, float2(1.0, -1.0))), g = F(p + rot(k, float2(-1.0, 1.0)));\n"
    "  float3 b = F(p + rot(k, float2(0.0, -1.0)));\n"
    "  float3 f4 = F(p + rot(k, float2(2.0, 0.0))), h5 = F(p + rot(k, float2(0.0, 2.0)));\n"
    "  float3 d5 = F(p + rot(k, float2(-1.0, 2.0))), i5 = F(p + rot(k, float2(1.0, 2.0))), i4 = F(p + rot(k, float2(2.0, 1.0)));\n"
    "  float wd1 = dist(e, c) + dist(e, g) + dist(i, f4) + dist(i, h5) + 4.0 * dist(h, f);\n"
    "  float wd2 = dist(h, d5) + dist(h, i5) + dist(f, i4) + dist(f, b) + 4.0 * dist(e, i);\n"
    "  if (wd1 >= wd2) return 0.0;\n"
    "  return dist(e, f) <= dist(e, h) ? 0.5 : 1.0;\n"
    "}\n"
    "float4 body() {\n"
    "  float2 p = floor(uv * tsize);\n"
    "  return float4(corner(p, 0), corner(p, 1), corner(p, 2), corner(p, 3));\n"
    "}\n";

/* pass 2 of FX_SMOOTH */
static const char *SH_XBR = FX_COMMON FX_ROT
    "float3 blendc(float3 col, float2 ip, int k, float flag, float s) {\n"
    "  if (flag < 0.25) return col;\n"
    "  float3 nc = F(ip + rot(k, flag > 0.75 ? float2(0.0, 1.0) : float2(1.0, 0.0)));\n"
    "  return lerp(col, nc, clamp((s - 1.125) * 2.0, 0.0, 1.0));\n"
    "}\n"
    "float4 body() {\n"
    "  float2 p = clamp(uv * tsize, float2(0.0, 0.0), fsize - 0.001);\n"
    "  float2 ip = floor(p), f = p - ip;\n"
    "  float4 e = TEXE((ip + 0.5) / tsize);\n"
    "  float3 col = F(ip);\n"
    "  col = blendc(col, ip, 0, e.r, f.x + f.y);\n"
    "  col = blendc(col, ip, 1, e.g, (1.0 - f.x) + f.y);\n"
    "  col = blendc(col, ip, 2, e.b, 2.0 - f.x - f.y);\n"
    "  col = blendc(col, ip, 3, e.a, f.x + (1.0 - f.y));\n"
    "  return float4(col, 1.0);\n"
    "}\n";

/* extra: 1 = plain bilinear, 0 = sharp pixels */
static const char *SH_PIXELS = FX_COMMON
    "float4 body() {\n"
    "  float2 p = uv * tsize, q = p;\n"
    "  if (extra < 0.5) {\n"
    "    float2 i = floor(p), t = p - i - 0.5;\n"
    "    float2 r = max(0.5 - 0.5 / scale, 0.0);\n"
    "    q = i + (t - clamp(t, -r, r)) * scale + 0.5;\n"
    "  }\n"
    "  q = clamp(q, float2(0.5, 0.5), fsize - 0.5);\n"
    "  return float4(TEX(q / tsize).rgb, 1.0);\n"
    "}\n";

/* extra: 1 = curved screen */
static const char *SH_CRT = FX_COMMON
    "float3 lin(float3 c) { return c * c; }\n"
    "float3 scanl(float x, float y) {\n"            /* one scanline, softly filtered across */
    "  float px = x - 0.5, i = floor(px), t = px - i;\n"
    "  t = t * t * (3.0 - 2.0 * t);\n"
    "  y = clamp(y, 0.0, fsize.y - 1.0);\n"
    "  return lin(lerp(F(float2(i, y)), F(float2(i + 1.0, y)), t));\n"
    "}\n"
    "float3 beam(float d, float3 c) {\n"
    "  float3 s = lerp(float3(0.26, 0.26, 0.26), float3(0.42, 0.42, 0.42), sqrt(c));\n"
    "  return c * exp(-d * d / (2.0 * s * s)) / (s * 2.2);\n"
    "}\n"
    "float4 body() {\n"
    "  float2 n = uv * tsize / fsize;\n"
    "  float vig = 1.0;\n"
    "  if (extra > 0.5) {\n"
    "    float2 c = n * 2.0 - 1.0;\n"
    "    c.x *= 1.0 + c.y * c.y * 0.035;\n"
    "    c.y *= 1.0 + c.x * c.x * 0.05;\n"
    "    float2 a = abs(c);\n"
    "    if (a.x > 1.0 || a.y > 1.0) return float4(0.0, 0.0, 0.0, 1.0);\n"
    "    float2 v = 1.0 - smoothstep(float2(0.985, 0.985), float2(1.0, 1.0), a);\n"
    "    vig = v.x * v.y;\n"
    "    n = c * 0.5 + 0.5;\n"
    "  }\n"
    "  float2 p = n * fsize;\n"
    "  float py = p.y - 0.5, y0 = floor(py), dy = py - y0;\n"
    "  float3 a0 = scanl(p.x, y0), b0 = scanl(p.x, y0 + 1.0);\n"
    "  float3 crt = beam(dy, a0) + beam(1.0 - dy, b0);\n"
    "  float3 soft = lerp(a0, b0, dy);\n"
    /* scanlines only where they can be drawn (enough output lines per line) */
    "  float3 col = lerp(soft, crt, smoothstep(1.5, 3.0, scale.y));\n"
    "  if (scale.x >= 2.5) {\n"
    "    float m = fmod(floor(FRAGX), 3.0);\n"
    "    float3 mask = float3(m < 0.5 ? 1.0 : 0.72, (m > 0.5 && m < 1.5) ? 1.0 : 0.72, m > 1.5 ? 1.0 : 0.72);\n"
    "    col *= mask * 1.22;\n"
    "  }\n"
    "  float3 glow = lin(F(p - 0.5 + float2(-1.5, 0.0))) + lin(F(p - 0.5 + float2(1.5, 0.0))) +\n"
    "                lin(F(p - 0.5 + float2(0.0, -1.5))) + lin(F(p - 0.5 + float2(0.0, 1.5)));\n"
    "  col += glow * 0.03;\n"
    "  return float4(sqrt(clamp(col, 0.0, 1.0)) * vig, 1.0);\n"
    "}\n";

/* the empty bars beside the picture: the picture shrunk (block averages,
 * extra = block size in pixels) and drawn blurred and dimmed */
static const char *SH_DOWN = FX_COMMON
    "float4 body() {\n"
    "  float2 p = uv * tsize;\n"
    "  float3 acc = float3(0.0, 0.0, 0.0);\n"
    "  for (int j = 0; j < 4; j++)\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "      float2 q = clamp(p + (float2(float(i), float(j)) - 1.5) * extra / 4.0, float2(0.5, 0.5), fsize - 0.5);\n"
    "      acc += TEX(q / tsize).rgb;\n"
    "    }\n"
    "  return float4(acc / 16.0, 1.0);\n"
    "}\n";
static const char *SH_BACKDROP = FX_COMMON
    "float4 body() {\n"
    "  float2 p = uv * tsize;\n"
    "  float3 acc = float3(0.0, 0.0, 0.0);\n"
    "  for (int j = -1; j <= 1; j++)\n"
    "    for (int i = -1; i <= 1; i++) {\n"
    "      float2 q = clamp(p + float2(float(i), float(j)) * 0.9, float2(0.5, 0.5), fsize - 0.5);\n"
    "      acc += TEX(q / tsize).rgb;\n"
    "    }\n"
    "  acc = acc / 9.0;\n"
    "  float l = dot(acc, float3(0.299, 0.587, 0.114));\n"
    "  acc = lerp(float3(l, l, l), acc, 0.75) * 0.45;\n"
    "  return float4(acc, 1.0);\n"
    "}\n";

/* plain copy (overlays: menu, messages) */
static const char *SH_COPY =
    "float4 body() { return float4(TEX(uv).rgb, 1.0); }\n";

#endif
