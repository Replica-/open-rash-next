/* world3d\render3d.h - the true-3D world renderer's Direct3D 11 back end (analysis\TRUE3D_PROJECT.md M3 / M4).  Header-only (static functions).
 *
 * Draws on the ID3D11Device of the SDL3 renderer (SDL_PROP_RENDERER_D3D11_DEVICE_POINTER) into the ID3D11Texture2D of an SDL target texture, with its
 * own depth buffer.  Commands are recorded on a deferred context and executed with RestoreContextState = TRUE, so the SDL renderer's own device state is
 * untouched (call SDL_FlushRenderer first so SDL's queued drawing comes before ours).  Without deferred-context support it draws on the immediate context;
 * SDL_FlushRenderer invalidates SDL's cached state, so SDL sets everything again on its next draw.
 * Shaders are HLSL compiled at start-up by d3dcompiler_47.dll (part of Windows).
 * Textures: RGBA8 with a full mip chain, sampled anisotropic 8x; colour key = alpha < 0.5 discarded (sprites / billboards).
 */
#ifndef RR_RENDER3D_H
#define RR_RENDER3D_H
#define COBJMACROS
#define CINTERFACE
#include <d3d11.h>
#include <d3dcompiler.h>
#include <string.h>
#include <stdlib.h>

typedef struct R3Vert { float x, y, z; float u, v; unsigned rgba; float tex; float ox, oy; } R3Vert;   /* = C3Vert (course3d.h); ox, oy = billboard offset (camera right, world up) */
enum { R3_MAXTEX = 4096, R3_MAXEXT = 2048, R3_EXT = 0x100000 };

typedef struct R3 {
    ID3D11Device *dev;
    ID3D11DeviceContext *imm, *ctx;     /* ctx = deferred context (or imm) */
    int deferred;
    ID3D11VertexShader *vs; ID3D11PixelShader *ps, *psGlow, *psLens; ID3D11InputLayout *il; ID3D11BlendState *bsAdd, *bsShadow, *bsLens; ID3D11DepthStencilState *dsShadow;
    ID3D11Buffer *cb, *vbStatic, *vbDyn, *vbLive; int nStatic, capDyn, nLive, capLive;
    ID3D11RasterizerState *rs; ID3D11DepthStencilState *dsOn, *dsTest; ID3D11BlendState *bsOff, *bsAlpha; ID3D11SamplerState *smp;
    ID3D11Texture2D *depth; ID3D11DepthStencilView *dsv; int dw, dh;
    ID3D11Texture2D *rfDepth; ID3D11DepthStencilView *rfDsv; ID3D11RenderTargetView *rfRtv; ID3D11ShaderResourceView *rfSrv; int rw, rh;   /* the puddles' mirror picture (R3Frame.reflect), half size */
    ID3D11ShaderResourceView *tex[R3_MAXTEX]; int ntex;
    ID3D11ShaderResourceView *ext[R3_MAXEXT]; int next;   /* views of textures owned by others (R3_External), batch tex = R3_EXT + index; released by R3_ExternalClear */
    ID3D11ShaderResourceView *white;
    ID3D11ShaderResourceView *waterN;   /* the sea's bump map (R3_WaterNormals), t4 */
    ID3D11Texture2D *shTex; ID3D11DepthStencilView *shDsv; ID3D11ShaderResourceView *shSrv; ID3D11SamplerState *smpCmp; ID3D11VertexShader *vsSh; ID3D11PixelShader *psSh;   /* the sun's shadow map (R3Frame.shadow) */
    ID3D11Query *qDis[4], *qTs[4][4]; int qi, qMade;   /* GPU timestamps of the passes (RR_PROFILE: R3_GpuTimes) */
    char err[512];
} R3;

typedef struct R3Batch { int first, count, tex; int alpha; } R3Batch;   /* tex = index from R3_Texture (-1 = flat colour); alpha = blended, depth not written (3 = added: light halos; 4 = a shadow: darkens, once per pixel - stencil) */

static const char r3_hlsl[] =
    "cbuffer C : register(b0) { row_major float4x4 vp; float4 fogc; float4 fogp; float4 right; float4 eye; float4 tint; float4 lit; float4 lp[16]; float4 lc[16];\n"
    "  float4 spP[6]; float4 spD[6]; float4 spC[6]; float4 trk; float4 fad; float4 wat; float4 rfl; float4 sun; float4 opt; row_major float4x4 svp; float4 shp; };\n"   /* svp / shp: the sun's shadow map (R3Frame.shadow): its view-projection, strength, 1 / size, bias; spot lights (the bikes' headlights): position + range, direction + cos outer, colour + cos inner; trk.x = 1 / the tracks map's size, y = their strength, z = spots, w = night;
                                                                                                fad = distance fade start, 1 / its length (0 = none), rain puddles, time (s); wat = the sky colour water reflects, sun glints;
                                                                                                rfl = the mirror plane's height, 1 = this is the mirror pass, 1 / target size (0 = no mirror picture: puddles show the sky only) */
    "#define FXD (opt.z > 0 ? opt.z : 7000)\n"   /* the effects' reach (R3Frame.opt[2]: True 3D detail > Effects distance; 0 = not given: 7000) */
    "struct VI { float3 p : POSITION; float2 t : TEXCOORD0; float4 c : COLOR; float k : TEXCOORD1; float2 o : TEXCOORD2; };\n"
    "struct VO { float4 p : SV_Position; float2 t : TEXCOORD0; float4 c : COLOR; float d : TEXCOORD1; float3 w : TEXCOORD2; };\n"
    "Texture2D T : register(t0); Texture2D K : register(t1); Texture2D Rf : register(t2); Texture2D Sm : register(t3); Texture2D Wn : register(t4); SamplerState S : register(s0); SamplerComparisonState SC : register(s1);\n"
    "VO vsshadow(VI i) { VO o; o.t = i.t; o.c = i.c; o.d = 0; o.w = i.p;\n"   /* the shadow map's pass (the sun's view, svp in vp): only the buildings' walls cast (vertex */
    "  o.p = abs(i.c.a - 0.996) < 0.002 && all(i.o == 0) ? mul(float4(i.p, 1), vp) : float4(0, 0, -1, 1); return o; }\n"   /* alpha 254, not billboards: the trees / lamps have their own) */
    "float4 psshadow(VO i) : SV_Target { if (T.Sample(S, i.t).a * i.c.a < 0.5) discard; return 0; }\n"
    "VO vsmain(VI i) { VO o; float3 w = i.p + right.xyz * i.o.x + float3(0, i.o.y, 0);\n"   /* billboards: upright, facing the camera */
    "  if (lit.w > 0) { o.p = float4(i.p, 1); o.w = float3(i.k, i.o.x, i.o.y); o.d = length(o.w - eye.xyz); o.t = i.t; o.c = i.c; return o; }\n"   /* lit.w: clip-space vertices (the sprites pass), (k, o) = their place in the world (lights, fog) */
    "  if (eye.w > 0 && any(i.o != 0)) { float3 d = w - eye.xyz; float l = length(d), bb = abs(i.c.a - 0.9294) < 0.0015 ? 25 : eye.w; w = eye.xyz + d * (max(l - bb, l * 0.5) / l); }\n"   /* (grass tufts, vertex alpha 237 (world3d.inc W3Grass): only 25 - the rails and fences in front of them still cover them) */   /* billboards: eye.w units nearer along the view ray (same place on screen): the ground / a rail just in front of the foot cannot cover them (the game draws sprites over their surroundings) */
    "  else if (eye.w > 0 && abs(i.c.a - 0.984) < 0.002) { float3 d = w - eye.xyz; float l = length(d); w = eye.xyz + d * (max(l - 160, l * 0.7) / l); }\n"   /* a shadow on the ground (world3d.inc): a little nearer too - the hillside beside the road rises above the caster's foot */
    "  o.p = mul(float4(w, 1), vp); o.t = i.t; o.c = i.c; o.d = o.p.w; o.w = w; return o; }\n"
    "float H(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }\n"
    "float Fog(float d, float3 w) { float f = saturate((d - fogp.x) * fogp.y) * fogp.z; if (fogc.w != 0) f *= saturate((eye.y + fogc.w - w.y) / 420) * 0.85 + saturate((d - fogp.x) * fogp.y * 0.25) * 0.15; return f; }\n"   /* fogc.w != 0: ground mist, lying up to fogc.w over the eye (a little haze above it far off) */
    "float N(float2 p) { float2 f = frac(p), q = floor(p); f = f * f * (3 - 2 * f); return lerp(lerp(H(q), H(q + float2(1, 0)), f.x), lerp(H(q + float2(0, 1)), H(q + 1), f.x), f.y); }\n"
    "float Hu(float2 q) { uint2 u = (uint2)(int2)q; uint h = u.x * 374761393u + u.y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; return (h & 65535u) / 65535.0; }\n"   /* integer hash noise: the same */
    "float Nu(float2 p) { float2 f = frac(p), q = floor(p); f = f * f * (3 - 2 * f); return lerp(lerp(Hu(q), Hu(q + float2(1, 0)), f.x), lerp(Hu(q + float2(0, 1)), Hu(q + 1), f.x), f.y); }\n"   /* in C (world3d.inc W3PuddleAt) */
    "float4 psmain(VO i) : SV_Target { float2 sz, dx = ddx(i.t), dy = ddy(i.t), uv = i.t; float3 n = cross(ddx(i.w), ddy(i.w)); T.GetDimensions(sz.x, sz.y);\n"
    "  if (abs(i.c.a - 0.984) < 0.002) { float2 o1 = (abs(dx) + abs(dy)) * 1.5; float s = (T.SampleGrad(S, i.t, dx, dy).a * 2 + T.SampleGrad(S, i.t + o1, dx, dy).a + T.SampleGrad(S, i.t - o1, dx, dy).a\n"   /* a shadow (vertex alpha 251, world3d.inc */
    "    + T.SampleGrad(S, i.t + float2(o1.x, -o1.y), dx, dy).a + T.SampleGrad(S, i.t - float2(o1.x, -o1.y), dx, dy).a) / 6; if (s < 0.2) discard;\n"                                   /* W3Shadows): the sprite's coverage, a */
    "    return float4(0, 0, 0, saturate(s * 1.4) * i.c.r * (1 - Fog(i.d, i.w))); }\n"                                                                                                 /* little soft; i.c.r = how dark */
    "  if (abs(i.c.a - 0.980) < 0.0015 && lit.y <= 0) discard;\n"   /* a snow bank by the road (vertex alpha 250, course3d.h C3_SnowBank): only while it snows */
    "  if (right.w > 0) { float2 tc = i.t * sz; float m = max(length(dx * sz), length(dy * sz));\n"   /* right.w = crisp: a magnified texel (bigger than a pixel) */
    "    uv = lerp(i.t, (floor(tc) + 0.5) / sz, saturate((1 - m) * 4)); dx *= 0.4; dy *= 0.4; }\n"       /* in its own colour, as the game draws (unfiltered); minified: a sharper mip (the game */
                                                                                                       /* has none: its ground's grain is the full texture's) */
    "  if (abs(i.c.a - 0.9098) < 0.0015) { float m = 0.5 * max(abs(dx.y), abs(dy.y)) + 0.5 / sz.y; uv.y = clamp(uv.y, m, 1 - m); }\n"   /* a cut-out picture standing up (vertex alpha 232, course3d.h C3_CUTV): v kept a footprint off its top / bottom - the wrap took the other edge's texels in (a thin dark line over far hedges) */
    "  float4 c = T.SampleGrad(S, uv, dx, dy) * i.c;\n"
    "  if (abs(i.c.a - 0.9255) < 0.0015 && lit.w == 0 && rfl.y <= 0) { float b = smoothstep(0.32, 0.68, Nu(i.w.xz / 2300 + 31)); if (b > 0.002) c = lerp(c, T.SampleGrad(S, uv + float2(0.37, 0.61), dx, dy) * i.c, b);\n"   /* the ground (vertex alpha 236, course3d.h C3_GROUNDA - not sprites): less alike */
    "    c.rgb *= 0.9 + 0.2 * Nu(i.w.xz / 5200 + 7); }\n"   /* from copy to copy (user, 2026-11-26 "less monotonous"): the same picture moved half over itself where a slow noise says, and light / dark patches */
    "  else if (abs(i.c.a - 0.9725) < 0.0015) c.rgb *= 0.95 + 0.1 * Nu(i.w.xz / 3600 + 3);\n"   /* (the road: patches only - its lane lines stay put) */
    "  if (abs(i.c.a - 0.9294) < 0.0015) c.a = min(c.a * (1 + opt.w * max(0, log2(max(length(dx * sz), length(dy * sz))))), i.c.a);\n"   /* grass tufts (vertex alpha 237): far off the smaller mips average the thin blades with the gaps between them and the cut-out lost them - the tufts thinned out with distance until the rider came near (user, 2026-11-15); alpha raised by the mip level */
    "  if (c.a < 0.5 || (rfl.y > 0 && (i.w.y < rfl.x - 40 || abs(normalize(n).y) > 0.8))) discard;\n"   /* (the mirror pass: only what stands up above the mirror - buildings, hillsides; not the road / ground) */
    "  float y = dot(c.rgb, float3(0.299, 0.587, 0.114)); float3 a = tint.rgb; int k; c.rgb = lerp(y.xxx, c.rgb, tint.w);\n"   /* time of day / weather: saturation, then the light */
    "  bool road = abs(i.c.a - 0.9725) < 0.0015;\n"   /* the road surface (vertex alpha 248, course3d.h C3_Lanes) */
    "  if (abs(i.c.a - 0.9333) < 0.0015) { float3 rn2 = normalize(n); if (dot(rn2, eye.xyz - i.w) < 0) rn2 = -rn2; float3 ld = sun.w > 0 ? sun.xyz : normalize(float3(0.3, 1, 0.2));\n"   /* a rock (vertex alpha 238, */
    "    c.rgb *= (0.55 + 0.65 * saturate(dot(rn2, ld))) * (0.9 + 0.2 * Nu(i.w.xz / 6 + i.w.y / 9)); }\n"   /* course3d.h C3_Rock): its facets lit and shaded, a grain */
    "  float facK = 0, facLit = 0; float3 facE = 0, facN = 0;\n"   /* the City set's facades (CITY_SCENERY.md, world3d.inc W3LoadCityArt): texels (0,0) / (1,0) = the layout */
    "  if (abs(i.c.a - 0.996) < 0.002) { float4 L0 = T.Load(int3(0, 0, 0)), L1 = T.Load(int3(1, 0, 0));\n"
    "    if (abs(L0.a - 0.996) < 0.003 && abs(L1.a - 0.996) < 0.003) {\n"
    "      float F = max(1, round(L0.r * 255)), C = max(1, round(L0.g * 255)), ct = L0.b, sv = L1.r, B = max(1, round(L1.g * 255));\n"   /* floors, bays, cornice / shop line (v), shop bays */
    "      float2 tu = ddx(i.t), tv = ddy(i.t); float det = tu.x * tv.y - tv.x * tu.y; det = abs(det) < 1e-14 ? 1e-14 : det;\n"
    "      float3 eu = (ddx(i.w) * tv.y - ddy(i.w) * tu.y) / det, ev = (ddy(i.w) * tu.x - ddx(i.w) * tv.x) / det, nf = normalize(n); if (dot(nf, eye.xyz - i.w) < 0) nf = -nf;\n"   /* world per u / v; facing the eye */
    "      float wg = abs(c.a - 0.698) < 0.02 ? 1 : abs(c.a - 0.600) < 0.02 ? 2 : 0;\n"   /* window glass (0xB2) / a shop window (0x99) */
    "      if (wg > 0 && i.d < FXD && opt.y > 0) { float U = i.t.x, base = floor(U), fu = U - base, v = i.t.y, cells = wg > 1 ? B : C, cu = min(floor(fu * cells), cells - 1), u0 = cu / cells, u1 = (cu + 1) / cells, v0, v1, fl;\n"
    "        if (wg > 1) { v0 = sv + 0.04; v1 = 0.988; fl = 99; } else { float fh = (sv - ct) / F; fl = clamp(floor((v - ct) / fh), 0, F - 1); v0 = ct + fl * fh; v1 = v0 + fh; }\n"   /* the room: its bay, its floor */
    "        float3 r = normalize(i.w - eye.xyz); float lu2 = max(dot(eu, eu), 1e-6), lv2 = max(dot(ev, ev), 1e-6), ru = dot(r, eu) / lu2, rv = dot(r, ev) / lv2, rz = max(dot(r, -nf), 0.04);\n"   /* the view ray in (u, v, depth) */
    "        float D = (u1 - u0) * sqrt(lu2) * (wg > 1 ? 1.3 : 0.9), su = ru > 0 ? (u1 - fu) / max(ru, 1e-9) : (u0 - fu) / min(ru, -1e-9), sw = rv > 0 ? (v1 - v) / max(rv, 1e-9) : (v0 - v) / min(rv, -1e-9), sd = D / rz, s = min(min(su, sw), sd);\n"
    "        float hu = saturate((fu + ru * s - u0) / (u1 - u0)), hv = saturate((v + rv * s - v0) / (v1 - v0)), hz = saturate(rz * s / D), rs = Hu(float2(cu + base * 11 + (nf.x > 0 ? 500 : 0), fl * 3 + (nf.z > 0 ? 77 : 0))), rs2 = frac(rs * 7.31 + 0.17);\n"   /* where it meets the room (interior mapping) */
    "        float3 wc = rs < 0.3 ? float3(0.62, 0.68, 0.74) : rs < 0.6 ? float3(0.84, 0.74, 0.58) : rs < 0.8 ? float3(0.70, 0.76, 0.62) : float3(0.86, 0.84, 0.80), col; float sh;\n"   /* each room's walls a colour of its own */
    "        if (s == sd) { col = wc; sh = 0.95;\n"   /* the back wall: a picture, or (a shop) shelves of goods */
    "          if (wg < 2 && rs2 > 0.45 && abs(hu - 0.5) < 0.17 && abs(hv - 0.45) < 0.12) col = lerp(float3(0.20, 0.26, 0.32), float3(0.72, 0.50, 0.30), rs2);\n"
    "          if (wg > 1) { float2 g = floor(float2(hu * 16, hv * 5)); col = frac(hv * 5) < 0.12 ? float3(0.36, 0.30, 0.25) : float3(Hu(g + rs * 9), Hu(g + 3.3), Hu(g + 7.7)) * 0.7 + 0.18; } }\n"
    "        else if (s == su) { col = wc * 0.82; sh = 0.75; }\n"   /* a side wall */
    "        else if (rv > 0) { col = wg > 1 ? float3(0.56, 0.53, 0.49) : (rs2 < 0.5 ? float3(0.44, 0.31, 0.20) : float3(0.36, 0.37, 0.42)); sh = 0.65; }\n"   /* the floor (wood / carpet) */
    "        else { col = float3(0.90, 0.89, 0.86); sh = 0.85; if (length(float2(hu - 0.5, hz - 0.5)) < 0.09) col = float3(1.5, 1.4, 1.2); }\n"   /* the ceiling, a lamp */
    "        col *= sh * lerp(1.0, 0.55, hz);\n"   /* darker deeper in */
    "        { float rc = 7 / rz; float ra = T.SampleLevel(S, float2(U + ru * rc, v + rv * rc), 0).a; bool rev = abs(ra - c.a) > 0.03 && i.d < FXD * 0.72;\n"   /* the glass set back 7 units: at a slant the */
    "          float3 tx = c.rgb; c.rgb = rev ? T.SampleLevel(S, float2(U + ru * rc * 1.4, v + rv * rc * 1.4), 0).rgb * 0.45 : lerp(col * (wg > 1 ? 1.0 : 0.72), tx, wg > 1 ? 0.10 : 0.25);\n"   /* reveal (the wall's side, in shade); the room, the pane's own (curtains) */
    "          facLit = rev ? 0 : wg > 1 ? step(0.12, rs2) : step(rs, 0.5) * step(0.25, H(floor(i.w.xz / 900) + 3.1)); }\n"   /* night: half the rooms of three buildings in four; nearly every shop */
    "        facE = col * (rs2 < 0.3 ? float3(0.75, 0.85, 1.0) : float3(1.0, 0.80, 0.52)) * (wg > 1 ? 1.5 : 1.25); facK = wg; }\n"   /* warm or cool light */
    "      else if ((trk.w > 0 || (wg > 0 && opt.y <= 0)) && c.a < 0.985 && c.a > 0.55 && (i.t.y < sv || i.t.y > sv + 0.045)) { if (wg > 0 && opt.y <= 0) c.rgb *= 0.6; float U = i.t.x, base = floor(U), fu = U - base, v = i.t.y, sp = v > sv ? 1 : 0;\n"   /* far off at night (user: "the window lights */
    "        float cells = sp > 0 ? B : C, cu = min(floor(fu * cells), cells - 1), fl = sp > 0 ? 99 : clamp(floor((v - ct) / ((sv - ct) / F)), 0, F - 1);\n"   /* dont show until the player is close"): the */
    "        float rs = Hu(float2(cu + base * 11 + (nf.x > 0 ? 500 : 0), fl * 3 + (nf.z > 0 ? 77 : 0))), rs2 = frac(rs * 7.31 + 0.17), f = saturate((1 - c.a) / (sp > 0 ? 0.40 : 0.302));\n"   /* mips blend glass into wall: */
    "        facK = 3; facLit = (sp > 0 ? step(0.12, rs2) : step(rs, 0.5) * step(0.25, H(floor(i.w.xz / 900) + 3.1))) * f;\n"   /* how much of the texel is glass (from its alpha) */
    "        facE = (rs2 < 0.3 ? float3(0.75, 0.85, 1.0) : float3(1.0, 0.80, 0.52)) * (sp > 0 ? 0.75 : 0.62); }\n"   /* lit by the same room rule, no rooms drawn */
    "      else if (wg == 0 && c.a > 0.9 && i.d < FXD * 0.5) { float2 o1 = float2(1 / sz.x, 0), o2 = float2(0, 1 / sz.y); float3 lw = float3(0.3, 0.59, 0.11);\n"   /* a wall: bump mapped from its brightness */
    "        float h0 = dot(c.rgb, lw), h1 = dot(T.SampleGrad(S, uv + o1, dx, dy).rgb, lw), h2 = dot(T.SampleGrad(S, uv + o2, dx, dy).rgb, lw), bk = 5 * saturate(1 - i.d / 3500);\n"   /* (mortar, joints, ornament: */
    "        facN = normalize(nf + (normalize(eu) * (h0 - h1) + normalize(ev) * (h0 - h2)) * bk); } } }\n"   /* darker = lower) */
    "  if (abs(i.c.a - 0.9608) < 0.0015) { float3 V = normalize(eye.xyz - i.w); float th = N(i.w.xz / 45) * 0.6 + N(i.w.xz / 14 + 2.1) * 0.4 + V.y * 1.8;\n"   /* an oil patch (vertex alpha 245, course3d.h C3_Patch): */
    "    float3 ir = 0.5 + 0.5 * cos(6.2832 * (th + float3(0, 0.33, 0.67))); float gz = pow(1 - saturate(V.y), 2);\n"   /* black, a thin film's rainbow that shifts as */
    "    c.rgb = float3(0.025, 0.025, 0.03) + ir * (0.10 + 0.22 * gz) * saturate(1 - 0.5 * (1 - y)); if (sun.w > 0) c.rgb += pow(saturate(dot(reflect(-V, float3(0, 1, 0)), sun.xyz)), 30) * 0.5 * sun.w; }\n"   /* you move, a glossy shine */
    "  if (abs(i.c.a - 0.9569) < 0.0015 || abs(i.c.a - 0.9529) < 0.0015) { float3 V = normalize(eye.xyz - i.w), Rw = reflect(-V, normalize(float3((N(i.w.xz / 30 + fad.w * 0.3) - 0.5) * 0.08, 1, (N(i.w.xz / 30 + 5.1) - 0.5) * 0.08)));\n"   /* a water / ice patch (vertex alpha 244 / 243): */
    "    bool ice = abs(i.c.a - 0.9529) < 0.0015; float fr = 0.25 + 0.6 * pow(1 - saturate(V.y), 4); float3 sk = lerp(fogc.rgb, float3(0.30, 0.46, 0.75) * max(tint.r, 0.35), saturate(Rw.y * 2.5)) / max(tint.rgb, 0.25);\n"   /* a puddle - the sky in it, more */
    "    c.rgb = lerp(ice ? float3(0.70, 0.78, 0.86) : float3(0.05, 0.06, 0.07), sk, ice ? fr * 0.5 : fr);\n"   /* the flatter you look; ice pale and frosted */
    "    if (sun.w > 0) c.rgb += pow(saturate(dot(Rw, sun.xyz)), ice ? 40 : 120) * (ice ? 0.6 : 1.4) * sun.w * float3(1, 0.96, 0.88) / max(tint.rgb, 0.25); }\n"   /* the sun's glint */
    "  float3 rn = float3(0, 1, 0); float pol = 0;\n"   /* the road's surface normal and polish for the sun (the improved road: bumped) */
    "  if (opt.x > 0 && road) { float2 q = i.w.xz; float mk = saturate(smoothstep(0.42, 0.55, y) + saturate((c.r - c.b) * 3 - 0.2)), gn = saturate(1 - i.d / 2600);\n"   /* the improved road (opt.x): the course's own */
    "    float mott = Nu(q / 600) * 0.6 + Nu(q / 170 + 2.2) * 0.4, gr = Nu(q / 2.2 + 5.1) * 0.5 + Nu(q / 5.5 + 1.7) * 0.5, u = i.t.x;\n"   /* asphalt kept (its colour, its gradient), with wear */
    "    pol = (exp(-pow((u - 0.28) / 0.08, 2)) + exp(-pow((u - 0.72) / 0.08, 2))) * (0.7 + 0.3 * Nu(q / 90 + 4.4));\n"   /* the wheel tracks in each lane: darker, polished */
    "    float3 asp = c.rgb * (0.90 + 0.18 * mott) * lerp(1, 0.86 + 0.28 * gr, gn) * (1 - 0.10 * pol);\n"   /* patches, a fine grain near, the tracks */
    "    c.rgb = lerp(asp, c.rgb, mk);\n"   /* the paint as it was (no tar-sealed cracks: user, 2026-11-19 "what are the squiggles doing on the road? ... remove them") */
    "    { float e = 0.35, hx = Nu((q + float2(e, 0)) / 0.9) - Nu((q - float2(e, 0)) / 0.9) + 0.6 * (Nu((q + float2(e, 0)) / 2 + 3.3) - Nu((q - float2(e, 0)) / 2 + 3.3)),\n"   /* the bump map: a fine */
    "        hz = Nu((q + float2(0, e)) / 0.9) - Nu((q - float2(0, e)) / 0.9) + 0.6 * (Nu((q + float2(0, e)) / 2 + 3.3) - Nu((q - float2(0, e)) / 2 + 3.3)), bn = saturate(1 - i.d / 1500);\n"   /* aggregate (~1 and 2 units: a lane is ~250) */
    "      rn = normalize(float3(-hx * 1.1 * bn * (1 - 0.6 * pol), 1, -hz * 1.1 * bn * (1 - 0.6 * pol))); } }\n"   /* -> a normal per pixel (the polished tracks smoother), near only */
    "  bool tile = abs(i.c.a - 0.9490) < 0.0015, walk = abs(i.c.a - 0.9647) < 0.0015 || tile, kerb = abs(i.c.a - 0.9686) < 0.0015, gut = abs(i.c.a - 0.9451) < 0.0015; float gloss = 0;\n"   /* the footpath (concrete 246 / tiles 242), the curb (247), the gutter (241): course3d.h C3_Roadside */
    "  if (abs(i.c.a - 0.9373) < 0.0015 || abs(i.c.a - 0.9176) < 0.0015) { float2 tu = ddx(i.t), tv = ddy(i.t); float det = tu.x * tv.y - tv.x * tu.y; float3 wu = (ddx(i.w) * tv.y - ddy(i.w) * tu.y) / det, wv = (ddy(i.w) * tu.x - ddx(i.w) * tv.x) / det;\n"   /* dirt / sand (vertex alpha 239 / 234: course3d.h C3_TexDirt, the dunes; user, 2026-11-19 "give it a bump mapping finish"): */
    "    if (abs(det) < 1e-14 || any(abs(wu) > 1e5) || any(abs(wv) > 1e5)) { wu = float3(1, 0, 0); wv = float3(0, 0, 1); }\n"
    "    float2 e = float2(1.0 / 300, 0); float bn = saturate(1 - i.d / 3000), hx = dot(T.SampleGrad(S, i.t + e.xy, tu, tv).rgb - T.SampleGrad(S, i.t - e.xy, tu, tv).rgb, 0.333), hy = dot(T.SampleGrad(S, i.t + e.yx, tu, tv).rgb - T.SampleGrad(S, i.t - e.yx, tu, tv).rgb, 0.333);\n"   /* the picture's light / dark as height, near only, */
    "    float3 nn = normalize(n); if (nn.y < 0) nn = -nn; float3 du = normalize(wu - dot(wu, nn) * nn), dv = normalize(wv - dot(wv, nn) * nn), bnr = normalize(nn - (du * hx + dv * hy) * 5 * bn);\n"
    "    float3 ld = sun.w > 0 ? sun.xyz : normalize(float3(0.3, 1, 0.2)); c.rgb *= 0.82 + 0.36 * saturate(dot(bnr, ld)) / max(saturate(dot(nn, ld)), 0.25); }\n"   /* lit by the sun: grains and ripples in relief */
    "  bool cob = abs(i.c.a - 0.9412) < 0.0015;\n"   /* cobbles (vertex alpha 240, course3d.h C3_Lanes - ground_art.h COBBLE; user, 2026-11-18: "a shine bump mapping like streets but for each cobble stone"): */
    "  if (cob) { float2 tu = ddx(i.t), tv = ddy(i.t); float det = tu.x * tv.y - tv.x * tu.y; float3 wu = (ddx(i.w) * tv.y - ddy(i.w) * tu.y) / det, wv = (ddy(i.w) * tu.x - ddx(i.w) * tv.x) / det;\n"   /* world per texture u / v */
    "    if (abs(det) < 1e-14 || any(abs(wu) > 1e5) || any(abs(wv) > 1e5)) { wu = float3(1, 0, 0); wv = float3(0, 0, 1); }\n"
    "    float2 e = float2(1.0 / 160, 0); float2 gu = tu * 2.5 + e * 0.5, gv = tv * 2.5 + e.yx * 0.5; float bn = saturate(1 - i.d / 2500);\n"   /* the picture a little blurred: each stone a dome, */
    "    float hx = dot(T.SampleGrad(S, i.t + e.xy, gu, gv).rgb - T.SampleGrad(S, i.t - e.xy, gu, gv).rgb, 0.333), hy = dot(T.SampleGrad(S, i.t + e.yx, gu, gv).rgb - T.SampleGrad(S, i.t - e.yx, gu, gv).rgb, 0.333);\n"   /* the joints (dark) low */
    "    float3 nn = normalize(n); if (nn.y < 0) nn = -nn; float3 du = normalize(wu - dot(wu, nn) * nn), dv = normalize(wv - dot(wv, nn) * nn); float lum = dot(c.rgb, 0.333);\n"
    "    rn = normalize(nn - (du * hx + dv * hy) * 7 * bn); gloss = 0.55 * smoothstep(0.18, 0.42, lum) * bn; }\n"   /* -> its normal; the stones polished, the joints not */
    "  if (opt.x > 0 && (walk || kerb)) { float2 tu = ddx(i.t), tv = ddy(i.t); float det = tu.x * tv.y - tv.x * tu.y, fp = length(ddx(i.w)) + length(ddy(i.w));\n"   /* improved street textures: */
    "    float3 wu = (ddx(i.w) * tv.y - ddy(i.w) * tu.y) / det, wv = (ddy(i.w) * tu.x - ddx(i.w) * tv.x) / det; float lu = length(wu), lv = length(wv);\n"   /* world units per texture u / v */
    "    if (abs(det) < 1e-14 || lu > 1e5 || lv > 1e5) { lu = 512; lv = 512; }\n"                                                                          /* (from the screen derivatives) */
    "    float gr = (Hu(floor(i.w.xz / 3 + i.w.y / 7)) - 0.5) * 0.07 * saturate(1 - i.d / 2500), st = 0.86 + 0.14 * Nu(i.w.xz / 90 + 4.1);\n"            /* grain, stains */
    "    if (walk) { float ku = i.t.x * lu, kl = max(1, round(lv / 300)), sz = tile ? 30 : 62, ns = max(1, round(lv / sz)), nx = max(1, round(max(lu - 40, 1) / sz));\n"
    "      float2 g = float2((ku - 40) / max(lu - 40, 1) * nx, i.t.y * ns), f = frac(g), id = floor(g); float3 p;\n"   /* slabs ~62 units (~0.9 m: a lane is ~250), a granite kerb top 40 wide at the road */
    "      float j = min(min(f.x, 1 - f.x) * max(lu - 40, 1) / nx, min(f.y, 1 - f.y) * lv / ns), jt;\n"
    "      float3 ctr = i.w + wu * ((id.x + 0.5 - g.x) * max(lu - 40, 1) / nx / lu) + wv * ((id.y + 0.5 - g.y) / ns);\n"
    "      float h = Hu(floor(ctr.xz / 20)), h2 = Hu(floor(ctr.xz / 20) + 7.3), ag = Nu(i.w.xz / 1.1 + 2.7) * 0.6 + Nu(i.w.xz / 3 + 8.1) * 0.4;\n"
    "      if (tile) { float hb = Hu(floor(ctr.xz / 2500) + 1.9); float3 tb = hb < 0.34 ? float3(0.40, 0.40, 0.43) : hb < 0.67 ? float3(0.60, 0.37, 0.27) : float3(0.78, 0.74, 0.66);\n"   /* glossy tiles ~30 units: a block of */
    "        p = tb * (0.91 + 0.14 * h) + (h2 - 0.5) * 0.03; p *= 0.95 + 0.10 * Hu(floor(i.w.xz * 0.8)) * saturate(1 - i.d / 1500); gloss = 1; }\n"   /* polished granite, terracotta or cream stone, each tile a shade, a fine speckle */
    "      else { p = float3(0.63, 0.62, 0.59) * (0.95 + 0.08 * h) + float3(0.012, 0.006, -0.008) * h2;\n"   /* light concrete in the road's grey family, each slab a shade of its own, */
    "        p *= 0.93 + 0.13 * lerp(0.5, ag, saturate(1 - i.d / 1800)); }\n"   /* the road's fine aggregate grain */
    "      if (ku < 40) { float fk = frac(i.t.y * kl); j = min(min(fk, 1 - fk) * lv / kl, 40 - ku); p = float3(0.68, 0.67, 0.65) * (1.03 - 0.07 * smoothstep(28, 40, ku)); }\n"   /* the kerb top: granite */
    "      jt = 1 - smoothstep(1.2 - fp * 0.5, 1.2 + fp * 0.5 + 0.8, j); c.rgb = (p * (tile ? 1 : st) + (tile ? 0 : gr)) * (1 - (tile ? 0.45 : 0.32) * jt); gloss *= 1 - jt;\n"   /* the joints, thin (the tiles' grout darker) */
    "      { float3 du = normalize(wu), dv = normalize(wv); float bn = saturate(1 - i.d / 2500), sl = ku >= 40 ? 1 : 0;\n"   /* the bump map (visible at a distance: a lane is ~250 units): */
    "        float ju = min(f.x, 1 - f.x) * max(lu - 40, 1) / nx, jv = min(f.y, 1 - f.y) * lv / ns;\n"
    "        float bu = (1 - smoothstep(0, 4, ju)) * (f.x < 0.5 ? -1 : 1) * sl, bv = (1 - smoothstep(0, 4, jv)) * (f.y < 0.5 ? -1 : 1);\n"   /* each slab's edges rounded off towards its joints, */
    "        float rg = cos(6.2832 * i.t.y * lv / 3) * 0.30 * sl; float2 q = i.w.xz; float e = 0.5;\n"   /* broom-finish ridges across the slab every 3 units, */
    "        float gx = (Nu((q + float2(e, 0)) / 3) - Nu((q - float2(e, 0)) / 3)) * 3 + (Nu((q + float2(e, 0)) / 8 + 4.4) - Nu((q - float2(e, 0)) / 8 + 4.4)) * 4,\n"   /* the concrete's own unevenness */
    "              gz = (Nu((q + float2(0, e)) / 3) - Nu((q - float2(0, e)) / 3)) * 3 + (Nu((q + float2(0, e)) / 8 + 4.4) - Nu((q - float2(0, e)) / 8 + 4.4)) * 4;\n"   /* (3 and 8 units) */
    "        rn = normalize(float3(0, 1, 0) + ((du * bu + dv * bv) * (tile ? 0.25 : 0.6) + (tile ? 0 : 1) * (dv * rg + float3(-gx, 0, -gz) * 0.22)) * bn); } }\n"   /* (the tiles: flat, only their edges eased) */
    "    else { float nk = max(1, round(lu / 300)), fk = frac(i.t.x * nk), j = min(fk, 1 - fk) * lu / nk, v = i.t.y, jt = 1 - smoothstep(2 - fp * 0.5, 3.5 + fp * 0.5, j);\n"
    "      float3 p = float3(0.64, 0.63, 0.61) * (0.92 + 0.12 * Hu(floor(i.t.x * nk) + 31.0 * floor(i.w.xz / 900)));\n"   /* the curb face: granite kerbstones ~300 units long, */
    "      p *= v < 0.14 ? 1.14 : 1.0; p *= 1 - 0.32 * smoothstep(0.55, 1.0, v); c.rgb = (p * st + gr) * (1 - 0.45 * jt); } }\n"   /* a bright chamfer on top, road grime at the foot */
    "  if (opt.x > 0 && gut) { float u = i.t.x, pc = floor(i.t.y), fv = frac(i.t.y), gh = Hu(float2(pc, 3.7)), run = Hu(float2(floor(pc / 6), 9.1)) < 0.25 ? 1 : 0, glo = Hu(float2(pc, 5.3)) > 0.4 ? 0.02 : 0.16;\n"   /* the gutter (u 0 at the curb .. 1 out, v = piece) */
    "    float2 tu = ddx(i.t), tv = ddy(i.t); float det = tu.x * tv.y - tv.x * tu.y; float3 wu = (ddx(i.w) * tv.y - ddy(i.w) * tu.y) / (abs(det) < 1e-14 ? 1 : det), du = normalize(wu + 1e-6);\n"   /* (world direction of u) */
    "    float3 p = float3(0.40, 0.40, 0.41) * (0.94 + 0.10 * Nu(i.w.xz / 9 + 1.3)) * (1 - 0.22 * smoothstep(0.35, 0.0, u));\n"   /* a pan in the asphalt's grey, grimy at the curb, */
    "    float step0 = smoothstep(0.84, 0.97, u), sh = (1 - step0) * smoothstep(0.70, 0.86, u), dish = sin(saturate(u / 0.86) * 3.1416);\n"   /* recessed: a shallow dish, then a step up to */
    "    p *= (1 - 0.18 * exp(-pow((u - 0.12) / 0.05, 2))) * (1 - 0.10 * dish) * (1 - 0.25 * sh) * (1 + 0.10 * step0 * (1 - step0) * 4); gloss = 0.35;\n"   /* the road (its shade, a lit lip); a damp line */
    "    rn = normalize(float3(0, 1, 0) + du * (0.9 * (step0 * (1 - step0) * 4) - 0.12 * cos(saturate(u / 0.86) * 3.1416)));\n"   /* (the normal: the dish's slopes, the step) */
    "    if (run > 0 && u > 0.14 && u < 0.86) { float gu = (u - 0.14) / 0.72, fr = gu < 0.08 || gu > 0.92 ? 1 : 0, bar = step(0.5, frac(fv * 24));\n"   /* some stretches: a slot grate along the whole */
    "      p = fr > 0 ? float3(0.30, 0.29, 0.28) : bar > 0 ? float3(0.23, 0.21, 0.19) * (0.9 + 0.2 * Hu(floor(i.w.xz))) : float3(0.025, 0.025, 0.03); gloss = fr > 0 || bar > 0 ? 0.8 : 0; }\n"   /* gutter */
    "    else if (gh > 0.55 && fv > 0.38 && fv < 0.62 && u > glo && u < 0.86) { float gv = (fv - 0.38) / 0.24, gu = (u - glo) / (0.86 - glo), fr = gv < 0.09 || gv > 0.91 || gu < 0.08 || gu > 0.92 ? 1 : 0;\n"   /* else a drain on about */
    "      float bar = step(0.55, frac(gv * 10)); p = fr > 0 ? float3(0.32, 0.31, 0.30) : bar > 0 ? float3(0.24, 0.22, 0.20) * (0.9 + 0.2 * Hu(floor(i.w.xz))) : float3(0.025, 0.025, 0.03);\n"   /* half the pieces: an iron frame, */
    "      gloss = fr > 0 || bar > 0 ? 0.8 : 0; }\n"   /* bars across, the dark drop under them */
    "    if (fad.z > 0) { float wm = smoothstep(0.62, 0.42, u) * saturate(fad.z) * (run > 0 && u > 0.14 ? 0.3 : 1), fl = Nu(float2(u * 5, i.t.y * 9 - fad.w * 2.2)) - 0.5;\n"   /* rain: water running in it */
    "      float3 V = normalize(eye.xyz - i.w); float fz = 0.25 + 0.6 * pow(1 - saturate(V.y), 3);\n"   /* - dark, the sky in it, ripples flowing along */
    "      p = lerp(p, p * 0.35 + fogc.rgb * tint.rgb * fz * 0.6 + fl * 0.05, wm); gloss = lerp(gloss, 1.2, wm); rn = normalize(lerp(rn, float3(fl * 0.15, 1, fl * 0.1), wm)); }\n"
    "    c.rgb = p; }\n"
    "  if (abs(i.c.a - 0.988) < 0.002) { float2 q = float2((i.w.x + i.w.z) / 70, i.w.y / 26), st; float cr, ny = abs(normalize(n).y);\n"   /* a cliff face (vertex alpha 252, course3d.h): strata, */
    "    st = float2(N(float2(i.w.y / 55, (i.w.x - i.w.z) / 900)), N(q * float2(0.3, 1)));\n"                                                           /* cracks running down it, */
    "    cr = smoothstep(0.47, 0.5, N(float2((i.w.x + i.w.z) / 45, i.w.y / 700))) * smoothstep(0.53, 0.5, N(float2((i.w.x + i.w.z) / 45, i.w.y / 700)));\n"
    "    c.rgb *= (0.78 + 0.32 * st.x + 0.14 * st.y) * (1 - 0.55 * cr);\n"
    "    c.rgb = lerp(c.rgb, c.rgb * float3(1.12, 1.08, 0.95) + 0.05, saturate((ny - 0.35) * 3)); }\n"                                                     /* ledges catching the light */
    "  if (lit.z > 0) {\n"
    "    if (lit.y > 0 && abs(i.c.a - 0.976) > 0.0015) { float up = saturate((abs(normalize(n).y) - 0.55) * 4) * lit.y * (0.8 + 0.2 * N(i.w.xz / 260)), tr = saturate(K.SampleLevel(S, i.w.xz * trk.x, 0).r * (0.6 + 0.7 * N(i.w.xz / 7))) * trk.y;\n"   /* a light, patchy snow on what faces up */
    "      c.rgb = lerp(c.rgb, float3(0.93, 0.95, 1.0) * (0.72 + 0.28 * y), up * (1 - tr)); }\n"   /* ... except in the tracks the riders / cars leave (the tracks map, world x / z) */
    "    [loop] for (k = 0; k < 16; k++) { if (k >= (int)lit.x) break; { float3 d = lp[k].xyz - i.w; float q = saturate(1 - dot(d, d) / (lp[k].w * lp[k].w)); a += lc[k].rgb * q * q; } }\n"   /* point lights (lamps, police) */
    "    [loop] for (k = 0; k < 6; k++) { if (k >= (int)trk.z) break; { float3 d = i.w - spP[k].xyz; float l = length(d), q = saturate(1 - l / spP[k].w);\n"
    "      a += spC[k].rgb * q * q * smoothstep(spD[k].w, spC[k].w, dot(d, spD[k].xyz) / max(l, 1)); } } }\n"   /* the headlights' cones */
    "  if (sun.w > 0 && abs(i.c.a - 0.996) < 0.002) { float3 nn = normalize(n); if (dot(nn, eye.xyz - i.w) < 0) nn = -nn; if (any(facN != 0)) nn = facN;\n"   /* a building's wall (vertex alpha 254): lit by the sun - */
    "    c.rgb *= lerp(1, 0.66 + 0.52 * saturate(dot(nn, sun.xyz)), sun.w);\n"                                                                  /* brighter facing it, in shade away from it, */
    "    { float3 V = normalize(eye.xyz - i.w); float sr = saturate(dot(reflect(-V, nn), sun.xyz)), gl = 0.6 + 1.6 * saturate(0.45 - dot(c.rgb, float3(0.3, 0.59, 0.11)));\n"   /* and a sheen: the sun caught */
    "      c.rgb += ((pow(sr, 10) * 0.12 + pow(sr, 70) * 0.30) * float3(1, 0.96, 0.88) + pow(1 - saturate(abs(dot(V, nn))), 4) * 0.10 * fogc.rgb) * gl * sun.w; }\n"   /* towards it, the sky at a glancing angle - the dark glass more than the brick */
    "    if (abs(c.a - 0.75) < 0.03 || facK > 0) { float3 V2 = normalize(eye.xyz - i.w), Rg = reflect(-V2, facK > 0 ? normalize(n) * sign(dot(normalize(n), V2)) : nn); float fz = 0.30 + 0.55 * pow(1 - saturate(abs(dot(V2, nn))), 3), pv = Hu(floor(i.w.xz / 120 + i.w.y / 160)) * 0.14;\n"   /* a window in the sun (texel alpha 0.75): */
    "      float3 sky = lerp(fogc.rgb, float3(0.30, 0.48, 0.80) * max(max(tint.r, tint.g), 0.4), saturate(Rg.y * 2.5)) * (0.86 + pv);\n"   /* reflective glass - the sky it faces, a pane a shade of its own, */
    "      sky = lerp(sky, float3(0.95, 0.96, 1.0) * max(tint.r, 0.5), smoothstep(0.55, 0.78, N(Rg.xz / max(Rg.y, 0.06) * 2.5 + 1.3)) * saturate(Rg.y * 4) * 0.55);\n"   /* the clouds in it, */
    "      c.rgb = lerp(c.rgb, sky / max(tint.rgb, 0.25), fz * sun.w * (facK > 1 ? 0.28 : facK > 0 ? 0.5 : 1)) + pow(saturate(dot(Rg, sun.xyz)), 250) * 2.4 * sun.w * float3(1, 0.95, 0.85) / max(tint.rgb, 0.25); } }\n"   /* more at a glancing angle (Fresnel), the sun's glint */
    "  if (abs(i.c.a - 0.992) < 0.002) { float2 p = i.w.xz, g, w1, w2, w3; float t = fad.w, h, fr, sp, fo; float3 V = normalize(eye.xyz - i.w), Nw, Rv, sk, L;\n"   /* water (vertex alpha 253, course3d.h C3_Sea): a sea, */
    "    { float3 wa = Wn.Sample(S, p / 2600 + t * float2(0.050, 0.020)).rgb, wb = Wn.Sample(S, p / 2300 + float2(0.37, 0.61) + t * float2(0.034, -0.040)).rgb;\n"   /* bump mapped (R3_WaterNormals, t4), animated (user: "make it look like its water"): */
    "      w1 = wa.rg + wb.rg - 1; h = (wa.b + wb.b) * 0.5; }\n"                                                                                                                         /* each scale twice, moving different ways at about the speed of */
    "    w2 = Wn.Sample(S, p / 820 + t * float2(-0.090, 0.110)).rg + Wn.Sample(S, p / 760 + float2(0.21, 0.83) + t * float2(0.120, 0.070)).rg - 1;\n"                         /* a wave that size - the sum keeps changing shape instead of */
    "    w3 = Wn.Sample(S, p / 280 + t * float2(0.300, -0.220)).rg + Wn.Sample(S, p / 310 + float2(0.55, 0.17) + t * float2(-0.250, -0.330)).rg - 1;\n"                       /* sliding: swells, a cross sea, chop (mipmapped: calm far off) */
    "    g = w1 * 0.26 + w2 * 0.18 + w3 * 0.11 * saturate(1.3 - i.d / 9000);\n"
    "    Nw = normalize(float3(-g.x, 1, -g.y)); Rv = reflect(-V, Nw); fr = 0.02 + 0.98 * pow(1 - saturate(dot(Nw, V)), 5);\n"                                                        /* -> normal; Fresnel */
    "    sk = lerp(wat.rgb * 0.6 + float3(0.02, 0.05, 0.08), float3(0.16, 0.30, 0.50) * (0.4 + 0.6 * max(max(wat.r, wat.g), wat.b)), saturate(Rv.y * 3));\n"   /* the sky it reflects: a deeper blue than the haze */
    "    { float3 body = lerp(float3(0.03, 0.19, 0.23), float3(0.02, 0.09, 0.18), saturate(i.d / 25000));\n"                                                                        /* the water's own colour: teal near, navy far */
    "      body += float3(0.02, 0.10, 0.09) * saturate(h * 1.4 - 0.4) * saturate(1 - V.y);\n"                                                                                     /* light through the crests */
    "      c.rgb = lerp(body, sk, min(fr * 0.9, 0.4)); }\n"                                                                                                                        /* Fresnel, capped: never a mirror (user: "a little bit less shiny") */
    "    L = normalize(float3(-V.x, 0.22, -V.z)); sp = min(pow(saturate(dot(Rv, L)), 500) * 0.6, 0.4);\n"                                                                           /* the sun's glints off the bumps, softer */
    "    fo = smoothstep(0.86, 0.97, h) * saturate(1 - i.d / 7000) * 0.25;\n"                                                                                                    /* a touch of foam on the highest crests, near */
    "    c.rgb = lerp(c.rgb, float3(0.8, 0.84, 0.86), fo) + sp * wat.w * float3(1, 0.95, 0.82) / max(tint.rgb, 0.2); c.a = 1; }\n"
    "  if (shp.x > 0 && lit.w == 0 && rfl.y == 0 && sun.w > 0) { float3 gn = normalize(n); if (dot(gn, eye.xyz - i.w) < 0) gn = -gn;\n"   /* the buildings' shadows (the sun's shadow map): */
    "    if (dot(gn, sun.xyz) > 0.02) { float4 sp = mul(float4(i.w + gn * (3 + i.d * 0.0008), 1), svp); float2 su = float2(sp.x * 0.5 + 0.5, 0.5 - sp.y * 0.5); int ix, iy;\n"   /* on whatever faces the sun (the road, */
    "      if (all(su > 0.002) && all(su < 0.998) && sp.z > 0) { float s2 = 0; [unroll] for (iy = -1; iy <= 1; iy++) [unroll] for (ix = -1; ix <= 1; ix++) s2 += Sm.SampleCmpLevelZero(SC, su + float2(ix, iy) * shp.y, sp.z + shp.z);\n"   /* the footpath, */
    "        c.rgb *= 1 - shp.x * (1 - s2 / 9) * saturate(min(min(su.x, su.y), min(1 - su.x, 1 - su.y)) * 25); } } }\n"   /* the other buildings), soft edged (3 x 3), faded at the map's edge */
    "  { float2 p = i.w.xz; float nn = Nu(p / 300 + 17.3) * 0.72 + Nu(p / 85 + 3.3) * 0.28, fl = saturate((abs(normalize(n).y) - 0.8) * 8), on = fad.z > 0 && lit.w == 0 && rfl.y == 0 && abs(i.c.a - 0.992) >= 0.002 && abs(i.c.a - 0.976) > 0.0015 ? min(fad.z, 1) * fl : 0, th = 0.59 - saturate(fad.z - 1) * 0.06;\n"   /* rain: puddles on what lies flat */
    "    float pd = smoothstep(th, th + 0.025, nn) * on, wet = smoothstep(th - 0.11, th + 0.01, nn) * on; float3 V = normalize(eye.xyz - i.w); c.rgb *= a;\n"                                      /* (irregular, a damp rim round them) */
    "    if (pd > 0) { float2 rg = 0, cl, q; float t = fad.w, fr; float3 sk = fogc.rgb * tint.rgb * 0.35, bd;\n"
    "      [unroll] for (k = 0; k < 2; k++) { float2 pp = p / 70 + k * 0.5; cl = floor(pp); q = frac(pp) - 0.5 - (float2(H(cl + 1.7), H(cl + 5.3)) - 0.5) * 0.4;\n"   /* raindrops: rings spreading */
    "        { float ph = frac(t * 0.9 + H(cl + k * 11.1)), r = length(q), w = sin((r - ph * 0.45) * 60) * saturate(1 - abs(r - ph * 0.45) * 12) * (1 - ph); rg += q / max(r, 0.001) * w; } }\n"   /* on the water */
    "      if (rfl.z > 0) { float4 R = Rf.SampleLevel(S, i.p.xy * rfl.zw + rg * 0.006, 0); sk = R.rgb + sk * (1 - R.a); }\n"                                                         /* the world mirrored in it */
    "      fr = (0.08 + 0.22 * pow(1 - saturate(V.y), 4)) * 0.7;\n"                                                                                                                          /* more mirror the flatter you look */
    "      bd = c.rgb * 0.6 * float3(0.9, 0.93, 1.0);\n"                                                                                                                            /* the road under the water, darker */
    "      c.rgb = lerp(c.rgb * (1 - 0.2 * wet), bd + sk * fr + (a - tint.rgb) * 0.35 + length(rg) * 0.02 * tint.rgb, pd); }\n"                                                    /* + the lights; the ripples catch the light */
    "    else c.rgb *= 1 - 0.2 * wet; }\n"                                                                                                                                          /* (damp road) */
    "  if (fad.z > 0 && lit.w == 0 && rfl.y == 0 && i.d < FXD * 0.72 && abs(i.c.a - 0.976) > 0.0015 && abs(i.c.a - 0.992) >= 0.002) { float2 p = i.w.xz / 45, q, cl; float rh = 0; int m;\n"   /* rain hitting the */
    "    [unroll] for (m = 0; m < 2; m++) { float2 pp = p + m * 0.5; cl = floor(pp); q = frac(pp) - 0.5 - (float2(Hu(cl + 2.1), Hu(cl + 4.7)) - 0.5) * 0.5;\n"                          /* ground: in each cell a */
    "      { float ph = frac(fad.w * 1.6 + Hu(cl + m * 3.3)), r = length(q) * 45, rr = ph * 60; rh += step(ph, 0.22) * saturate(1 - abs(r - rr) / 2.5) * (1 - ph / 0.22); } }\n"   /* drop's ring now and then, */
    "    c.rgb += rh * 0.07 * tint.rgb * saturate((abs(normalize(n).y) - 0.8) * 8) * saturate(1 - i.d / 5000); }\n"                                                                  /* faint, near only */
    "  if (trk.w > 0 && abs(i.c.a - 0.996) < 0.002 && abs(c.a - 0.75) < 0.03) { float3 ec = H(floor(i.w.xz / 700)) < 0.3 ? float3(0.75, 0.85, 1.0) : float3(1.0, 0.76, 0.42);\n"   /* a facade's window (alpha 0.75, world3d.inc): lit at night in */
    "    c.rgb = lerp(c.rgb, ec * saturate(0.3 + 1.5 * y) * 0.85, trk.w * step(0.25, H(floor(i.w.xz / 900) + 3.1))); }\n"                                        /* three buildings out of four, warm or cool */
    "  if (trk.w > 0 && abs(i.c.a - 0.996) < 0.002 && abs(c.a - 0.65) < 0.03) c.rgb = lerp(c.rgb, c.rgb * 1.6 / max(max(tint.r, tint.g), 0.3) + 0.08, trk.w);\n"                 /* a shop sign (alpha 0.65): glowing in its own colour */
    "  if (trk.w > 0 && facK > 0) c.rgb = lerp(c.rgb, facE, trk.w * facLit);\n"   /* the City set at night: the lit rooms / shops (facLit, above) glow */
    "  if (abs(i.c.a - 0.996) < 0.002 && abs(c.a - 0.549) < 0.02) { float3 nc = c.rgb / max(max(max(c.r, c.g), c.b), 0.02);\n"   /* a neon tube (0x8C): its own colour, */
    "    nc = pow(saturate(nc), 4); c.rgb = lerp(c.rgb * 1.15, nc * 1.8 + 0.10, saturate(trk.w * 1.2 + 0.2)); }\n"   /* faintly by day, blazing at night */
    "  if ((road || walk || gut || cob) && sun.w > 0) { float3 V = normalize(eye.xyz - i.w), R = reflect(-V, rn); float d2 = saturate(dot(R, sun.xyz)), gk = walk || gut || cob ? (tile ? 0.6 : 1.0) : (opt.x > 0 ? 1.25 : 0.85) * (1 + 0.6 * pol);\n"   /* the road / footpath / gutter in the sun: a sheen off the (bumped) surface, */
    "    c.rgb *= 1 + (walk || cob ? 0.65 : 0.30) * (dot(rn, sun.xyz) - sun.y) * sun.w;\n"   /* the bumps lit and shaded by the sun (the footpath's more), */
    "    c.rgb += min(((pow(d2, 2) * 0.10 + pow(d2, 8) * 0.22 + pow(d2, 60) * 0.35 + pow(d2, 400) * 0.6) * float3(1, 0.97, 0.9) + pow(1 - saturate(V.y * 4), 3) * 0.07 * fogc.rgb) * sun.w * gk, 0.28);\n"   /* towards the sun, glossy far off (the course's own road a little less); capped (user, 2026-11-15: "cap the shine ... still excessive at some points of the day") */
    "    c.rgb += min((pow(d2, 140) * 1.4 + pow(d2, 24) * 0.25) * gloss * sun.w * float3(1, 0.97, 0.92), 0.45); }\n"   /* glossy (the tiles, the wet gutter, the grates' iron): a sharp glint, capped */
    "  if ((walk || gut || cob) && lit.w == 0) { float3 V = normalize(eye.xyz - i.w); float fr = pow(1 - saturate(dot(V, rn)), tile ? 3 : 4); c.rgb += fr * (tile ? 0.45 : 0.22) * fogc.rgb * (0.5 + 0.5 * sun.w); }\n"   /* the footpath: the sky in it at a glancing angle, whichever way (daytime; the tiles more) */
    "  if (abs(i.c.a - 0.9216) < 0.0015) { float2 g = i.t * float2(4, 4), ce = floor(g), fq = frac(g); float t = fad.w, wv = 0.5 + 0.5 * sin(ce.y * 0.45 - t * 3.0 + ce.x * 0.6),\n"   /* the rainbow road (vertex alpha 235, course3d.h C3_Lanes - ground_art.h RAINBOW; user, 2026-11-19): */
    "      hu = frac(ce.y * 0.035 + ce.x * 0.06 - t * 0.15), ed = smoothstep(0.0, 0.09, min(min(fq.x, 1 - fq.x), min(fq.y, 1 - fq.y)));\n"   /* squares, each a hue that drifts along the road, */
    "      float3 rc = saturate(abs(frac(hu + float3(0, 2.0 / 3, 1.0 / 3)) * 6 - 3) - 1); c.rgb = rc * (0.35 + 0.75 * wv) * (0.15 + 0.85 * ed) + rc * ed * 0.35 * wv * wv; }\n"   /* brightening in waves running down the road, dark gaps */
    "  float f = Fog(i.d, i.w), fa = 1 - saturate((i.d - fad.x) * fad.y); c.rgb = lerp(c.rgb, fogc.rgb, f) * fogp.w;\n"   /* fogp.w = brightness (fades) */
    "  return float4(c.rgb * fa, fa); }\n"   /* towards the draw distance the world fades into the sky behind it (premultiplied: GpuCompose) - nothing pops in */
    "float4 psglow(VO i) : SV_Target { float4 c = T.Sample(S, i.t) * i.c; float f = Fog(i.d, i.w);\n"   /* light halos (batch alpha 3): added, the target's alpha kept */
    "  return float4(c.rgb * c.a * (1 - f * 0.85) * fogp.w, 0); }\n"
    "float4 pslens(VO i) : SV_Target { float4 c = T.Sample(S, i.t) * i.c; float f = Fog(i.d, i.w);\n"   /* a lamp lens (batch alpha 5): its own light, laid over (alpha blend), the target's alpha kept */
    "  return float4(lerp(c.rgb, fogc.rgb, f * 0.85) * fogp.w, c.a); }\n";

typedef HRESULT (WINAPI *R3Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob **, ID3DBlob **);

static ID3DBlob *R3_Compile(R3 *r, R3Compile cc, const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *msg = NULL;
    HRESULT hr = cc(r3_hlsl, sizeof r3_hlsl - 1, "world3d", NULL, NULL, entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &msg);
    if (FAILED(hr)) {
        if (msg) { lstrcpynA(r->err, (const char *)ID3D10Blob_GetBufferPointer(msg), sizeof r->err); ID3D10Blob_Release(msg); }
        return NULL;
    }
    if (msg) ID3D10Blob_Release(msg);
    return code;
}

static ID3D11ShaderResourceView *R3_MakeTexture(R3 *r, const unsigned *rgba, int w, int h)
{
    /* full mip chain, box-filtered here (colour-key alpha kept as coverage: a texel is opaque when most of its source texels are) */
    D3D11_TEXTURE2D_DESC td; D3D11_SUBRESOURCE_DATA sd[16]; ID3D11Texture2D *t = NULL; ID3D11ShaderResourceView *v = NULL;
    unsigned *lv[16]; int n = 0, cw = w, ch = h, i;
    lv[0] = (unsigned *)rgba;
    while (n < 15) {
        sd[n].pSysMem = lv[n]; sd[n].SysMemPitch = cw * 4; sd[n].SysMemSlicePitch = 0;
        n++;
        if (cw == 1 && ch == 1) break;
        {   int nw = cw > 1 ? cw / 2 : 1, nh = ch > 1 ? ch / 2 : 1, x, y;
            unsigned *dst = (unsigned *)malloc((size_t)nw * nh * 4);
            for (y = 0; y < nh; y++) for (x = 0; x < nw; x++) {
                unsigned s[4], acc[4] = { 0, 0, 0, 0 }, k, cov = 0, wsum = 0;
                int x0 = x * 2 < cw ? x * 2 : cw - 1, x1 = x * 2 + 1 < cw ? x * 2 + 1 : cw - 1, y0 = y * 2 < ch ? y * 2 : ch - 1, y1 = y * 2 + 1 < ch ? y * 2 + 1 : ch - 1;
                s[0] = lv[n - 1][y0 * cw + x0]; s[1] = lv[n - 1][y0 * cw + x1]; s[2] = lv[n - 1][y1 * cw + x0]; s[3] = lv[n - 1][y1 * cw + x1];
                for (k = 0; k < 4; k++) {
                    unsigned a = s[k] >> 24;
                    if (a >= 128) { cov++; wsum++; acc[3] += a; acc[0] += s[k] & 0xff; acc[1] += (s[k] >> 8) & 0xff; acc[2] += (s[k] >> 16) & 0xff; }
                }
                if (!wsum) { dst[y * nw + x] = s[0] & 0x00ffffff; continue; }
                acc[3] = cov >= 2 ? acc[3] / wsum : 0;   /* the covered texels' mean opacity (translucent textures stay translucent) */
                dst[y * nw + x] = (acc[0] / wsum) | ((acc[1] / wsum) << 8) | ((acc[2] / wsum) << 16) | (acc[3] << 24);
            }
            cw = nw; ch = nh; lv[n] = dst;
        }
    }
    memset(&td, 0, sizeof td);
    td.Width = w; td.Height = h; td.MipLevels = n; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (SUCCEEDED(ID3D11Device_CreateTexture2D(r->dev, &td, sd, &t))) {
        ID3D11Device_CreateShaderResourceView(r->dev, (ID3D11Resource *)t, NULL, &v);
        ID3D11Texture2D_Release(t);
    }
    for (i = 1; i < n; i++) free(lv[i]);
    return v;
}

/* the sea's bump map (user, 2026-11-15: "bump map the water instead of whatever that weird effect is"): a 256 x 256 tile of wind waves - sines whose
   wave vectors are whole numbers of cycles across the tile (so it repeats seamlessly), spread +-70 degrees round the wind, amplitude falling with the
   wave number - stored as its slopes (r = d/dx, g = d/dz: 0.5 + slope / 2) and its height (b); a full mip chain (R3_MakeTexture), so it never shimmers
   far off.  The shader (water, vertex alpha 253) lays it three times at different scales, drifting different ways */
static ID3D11ShaderResourceView *R3_WaterNormals(R3 *r)
{
    enum { WN = 256, NW = 40 }; static float kx[NW], ky[NW], am[NW], ph[NW]; unsigned *px = (unsigned *)malloc(sizeof(unsigned) * WN * WN), s = 12345u; int i, x, y;
    ID3D11ShaderResourceView *v; float hmin = 1e9f, hmax = -1e9f, *hh = (float *)malloc(sizeof(float) * WN * WN), *gx = (float *)malloc(sizeof(float) * WN * WN), *gz = (float *)malloc(sizeof(float) * WN * WN), gmax = 0;
#define R3_WRND() ((s = s * 1664525u + 1013904223u) >> 8) / 16777216.0f
    if (!px || !hh || !gx || !gz) { free(px); free(hh); free(gx); free(gz); return NULL; }
    for (i = 0; i < NW; i++) {   /* wave numbers 2..20 (cycles per tile), directions round the wind (+x), amplitude ~ k^-1.6 */
        float k = 2 + 18 * R3_WRND() * R3_WRND(), a = (R3_WRND() - 0.5f) * 2.4f;
        kx[i] = floorf(k * cosf(a) + 0.5f); ky[i] = floorf(k * sinf(a) + 0.5f);
        if (kx[i] == 0 && ky[i] == 0) kx[i] = 2;
        am[i] = powf(sqrtf(kx[i] * kx[i] + ky[i] * ky[i]), -1.6f); ph[i] = R3_WRND() * 6.2832f; }
#undef R3_WRND
    for (y = 0; y < WN; y++) for (x = 0; x < WN; x++) {
        float h = 0, dx = 0, dz = 0;
        for (i = 0; i < NW; i++) { float w = 6.2832f * (kx[i] * x + ky[i] * y) / WN + ph[i], c = cosf(w), sh = sinf(w);
            h += am[i] * sh; dx += am[i] * c * kx[i]; dz += am[i] * c * ky[i]; }
        hh[y * WN + x] = h; gx[y * WN + x] = dx; gz[y * WN + x] = dz;
        if (h < hmin) hmin = h; if (h > hmax) hmax = h; if (fabsf(dx) > gmax) gmax = fabsf(dx); if (fabsf(dz) > gmax) gmax = fabsf(dz); }
    for (i = 0; i < WN * WN; i++) {
        int R = (int)(127.5f + 127.5f * gx[i] / gmax), G = (int)(127.5f + 127.5f * gz[i] / gmax), B = (int)(255 * (hh[i] - hmin) / (hmax - hmin + 1e-6f));
        px[i] = 0xff000000u | (unsigned)B << 16 | (unsigned)G << 8 | (unsigned)R; }
    v = R3_MakeTexture(r, px, WN, WN);
    free(px); free(hh); free(gx); free(gz);
    return v;
}

/* a view of a texture owned by someone else (an SDL texture's ID3D11Texture2D) for this frame's batches: returns R3_EXT + index, or -1 */
static int R3_External(R3 *r, ID3D11Texture2D *t)
{
    int i; ID3D11ShaderResourceView *v = NULL; ID3D11Resource *res;
    if (!t) return -1;
    for (i = 0; i < r->next; i++) { ID3D11ShaderResourceView_GetResource(r->ext[i], &res); ID3D11Resource_Release(res); if (res == (ID3D11Resource *)t) return R3_EXT + i; }
    if (r->next >= R3_MAXEXT || FAILED(ID3D11Device_CreateShaderResourceView(r->dev, (ID3D11Resource *)t, NULL, &v))) return -1;
    r->ext[r->next] = v;
    return R3_EXT + r->next++;
}
static void R3_ExternalClear(R3 *r) { int i; for (i = 0; i < r->next; i++) ID3D11ShaderResourceView_Release(r->ext[i]); r->next = 0; }

/* add a texture (RGBA8, alpha < 128 = transparent); returns its index or -1 */
static int R3_Texture(R3 *r, const unsigned *rgba, int w, int h)
{
    ID3D11ShaderResourceView *v;
    if (r->ntex >= R3_MAXTEX || (v = R3_MakeTexture(r, rgba, w, h)) == NULL) return -1;
    r->tex[r->ntex] = v;
    return r->ntex++;
}

static int R3_Init(R3 *r, ID3D11Device *dev)
{
    HMODULE dc = LoadLibraryA("rr_d3dcompiler_47.dll");   /* the release's own copy of Windows' compiler under a name Wine / Proton has no built-in of (the Steam */
    if (!dc) dc = LoadLibraryA("d3dcompiler_47.dll");     /* Deck: no DLL override needed); else the system's */
    R3Compile cc = dc ? (R3Compile)GetProcAddress(dc, "D3DCompile") : NULL;
    ID3DBlob *vsb, *psb;
    D3D11_INPUT_ELEMENT_DESC ie[5] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 } };
    D3D11_BUFFER_DESC bd; D3D11_RASTERIZER_DESC rd; D3D11_DEPTH_STENCIL_DESC dd; D3D11_BLEND_DESC bl; D3D11_SAMPLER_DESC sm;
    unsigned whitePx[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
    memset(r, 0, sizeof *r);
    if (!dev) { lstrcpyA(r->err, "no D3D11 device"); return 0; }
    if (!cc) { lstrcpyA(r->err, "d3dcompiler_47.dll not found"); return 0; }
    r->dev = dev; ID3D11Device_AddRef(dev);
    ID3D11Device_GetImmediateContext(dev, &r->imm);
    r->deferred = SUCCEEDED(ID3D11Device_CreateDeferredContext(dev, 0, &r->ctx));
    if (!r->deferred) { r->ctx = r->imm; ID3D11DeviceContext_AddRef(r->ctx); }
    if (!(vsb = R3_Compile(r, cc, "vsmain", "vs_4_0")) || !(psb = R3_Compile(r, cc, "psmain", "ps_4_0"))) return 0;
    ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &r->vs);
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &r->ps);
    ID3D11Device_CreateInputLayout(dev, ie, 5, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), &r->il);
    ID3D10Blob_Release(vsb); ID3D10Blob_Release(psb);
    if (!(psb = R3_Compile(r, cc, "psglow", "ps_4_0"))) return 0;
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &r->psGlow); ID3D10Blob_Release(psb);
    if (!(psb = R3_Compile(r, cc, "pslens", "ps_4_0"))) return 0;
    ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &r->psLens); ID3D10Blob_Release(psb);
    if ((vsb = R3_Compile(r, cc, "vsshadow", "vs_4_0")) != NULL) { ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &r->vsSh); ID3D10Blob_Release(vsb); }
    if ((psb = R3_Compile(r, cc, "psshadow", "ps_4_0")) != NULL) { ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &r->psSh); ID3D10Blob_Release(psb); }
    memset(&bd, 0, sizeof bd); bd.ByteWidth = 284 * 4;   /* (R3_Render: the constants) */ bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(dev, &bd, NULL, &r->cb);
    memset(&rd, 0, sizeof rd); rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState(dev, &rd, &r->rs);
    memset(&dd, 0, sizeof dd); dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dd.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;   /* reversed depth (R3_Persp) */
    ID3D11Device_CreateDepthStencilState(dev, &dd, &r->dsOn);
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    ID3D11Device_CreateDepthStencilState(dev, &dd, &r->dsTest);
    dd.StencilEnable = TRUE; dd.StencilReadMask = dd.StencilWriteMask = 0xff;   /* shadows: only where none fell yet this frame (stencil 0 -> 1) */
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL; dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR_SAT; dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP; dd.BackFace = dd.FrontFace;
    ID3D11Device_CreateDepthStencilState(dev, &dd, &r->dsShadow);
    memset(&bl, 0, sizeof bl); bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11Device_CreateBlendState(dev, &bl, &r->bsOff);
    bl.RenderTarget[0].BlendEnable = TRUE; bl.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    ID3D11Device_CreateBlendState(dev, &bl, &r->bsAlpha);
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;   /* the same, the target's alpha kept (lamp lenses) */
    ID3D11Device_CreateBlendState(dev, &bl, &r->bsLens);
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bl.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;   /* added colour, alpha kept (GpuCompose: the target is premultiplied) */
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ZERO; bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    ID3D11Device_CreateBlendState(dev, &bl, &r->bsAdd);
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_ZERO; bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;   /* shadows: colour x (1 - a), alpha kept */
    ID3D11Device_CreateBlendState(dev, &bl, &r->bsShadow);
    memset(&sm, 0, sizeof sm); sm.Filter = D3D11_FILTER_ANISOTROPIC; sm.MaxAnisotropy = 16; sm.MipLODBias = -0.5f;   /* crisper ground at grazing angles (the game draws unfiltered texels) */ sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sm.MaxLOD = D3D11_FLOAT32_MAX; sm.ComparisonFunc = D3D11_COMPARISON_NEVER;
    ID3D11Device_CreateSamplerState(dev, &sm, &r->smp);
    memset(&sm, 0, sizeof sm); sm.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;   /* the shadow map: compared, */
    sm.BorderColor[0] = 0; sm.ComparisonFunc = D3D11_COMPARISON_GREATER_EQUAL; sm.MaxLOD = D3D11_FLOAT32_MAX;   /* lit where nothing nearer the sun (reversed depth: greater = nearer) */
    ID3D11Device_CreateSamplerState(dev, &sm, &r->smpCmp);
    r->white = R3_MakeTexture(r, whitePx, 2, 2);
    r->waterN = R3_WaterNormals(r);
    return r->vs && r->ps && r->psGlow && r->psLens && r->il && r->cb && r->bsAdd && r->white;
}

/* the translucent batches (alpha) after the opaque ones (they test the depth the opaque ones wrote, without writing it) - order kept otherwise */
static void R3_AlphaLast(R3Batch *b, int n)
{
    int i, k = 0; R3Batch *t = (R3Batch *)malloc(sizeof(R3Batch) * (n > 0 ? n : 1));
    if (!t) return;
    for (i = 0; i < n; i++) if (!b[i].alpha) t[k++] = b[i];
    for (i = 0; i < n; i++) if (b[i].alpha) t[k++] = b[i];
    memcpy(b, t, sizeof(R3Batch) * n); free(t);
}

static ID3D11Buffer *R3_Buffer(R3 *r, const R3Vert *v, int n, int dynamic)
{
    D3D11_BUFFER_DESC bd; D3D11_SUBRESOURCE_DATA sd; ID3D11Buffer *b = NULL;
    memset(&bd, 0, sizeof bd); bd.ByteWidth = (UINT)(sizeof(R3Vert) * (n > 0 ? n : 1)); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.Usage = dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_IMMUTABLE; bd.CPUAccessFlags = dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    sd.pSysMem = v; sd.SysMemPitch = sd.SysMemSlicePitch = 0;
    ID3D11Device_CreateBuffer(r->dev, &bd, v ? &sd : NULL, &b);
    return b;
}

/* the static world (course mesh) */
static void R3_SetStatic(R3 *r, const R3Vert *v, int n)
{
    if (r->vbStatic) ID3D11Buffer_Release(r->vbStatic);
    r->vbStatic = n > 0 ? R3_Buffer(r, v, n, 0) : NULL; r->nStatic = r->vbStatic ? n : 0;
}
/* the part of the course rebuilt from the game's live road (drawn instead of that range of the static mesh); replaced whenever it changes */
static void R3_SetLive(R3 *r, const R3Vert *v, int n)   /* (one dynamic buffer, written over each time - a new immutable one every q pieces was a hitch: world3d.inc W3Live) */
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (n <= 0) { r->nLive = 0; return; }
    if (n > r->capLive || !r->vbLive) { if (r->vbLive) ID3D11Buffer_Release(r->vbLive); r->capLive = n + n / 4; r->vbLive = R3_Buffer(r, NULL, r->capLive, 1); if (!r->vbLive) r->capLive = 0; }
    r->nLive = 0;
    if (r->vbLive && SUCCEEDED(ID3D11DeviceContext_Map(r->ctx, (ID3D11Resource *)r->vbLive, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        memcpy(m.pData, v, sizeof(R3Vert) * n); ID3D11DeviceContext_Unmap(r->ctx, (ID3D11Resource *)r->vbLive, 0); r->nLive = n; }
}

static void R3_Ensure(R3 *r, int w, int h)
{
    D3D11_TEXTURE2D_DESC td;
    if (r->dsv && r->dw == w && r->dh == h) return;
    if (r->dsv) { ID3D11DepthStencilView_Release(r->dsv); ID3D11Texture2D_Release(r->depth); r->dsv = NULL; }
    memset(&td, 0, sizeof td); td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_D32_FLOAT_S8X24_UINT; td.SampleDesc.Count = 1;   /* (+ stencil: shadows) */
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(ID3D11Device_CreateTexture2D(r->dev, &td, NULL, &r->depth))) ID3D11Device_CreateDepthStencilView(r->dev, (ID3D11Resource *)r->depth, NULL, &r->dsv);
    r->dw = w; r->dh = h;
}

typedef struct R3Frame {
    ID3D11Texture2D *target;            /* render target (an SDL target texture's D3D11 texture) */
    int x, y, w, h;                     /* viewport in the target */
    float vp[16];                       /* view-projection, row-major, row vectors (v * M) */
    float clear[4]; int doClear;        /* colour clear of the viewport (0 = keep: draw over what is there) */
    int keepDepth;                      /* 1 = keep the depth buffer of the previous pass (a second pass into the same target) */
    float fogColour[4], fogStart, fogScale, fogMax;
    float right[3];                     /* camera right vector, horizontal (billboards) */
    float eye[3], bias;                 /* camera position and the billboards' depth bias (0 = none) */
    float bright; int useBright;        /* colour multiplier (fades); useBright 0 = 1 */
    int crisp;                          /* 1 = magnified texels unfiltered (the game's look; the world), 0 = filtered */
    float tint[4]; int useTint;         /* time of day / weather: colour multiplier rgb + saturation (useTint 0 = 1, 1, 1, 1) */
    float snow; int clipSpace;          /* snow cover of up-facing surfaces (0..1); clipSpace 1 = vertices in clip space, R3Vert.tex = their distance (fog) */
    float light[16][4], lightCol[16][4]; int nLights;   /* point lights: position + radius, colour (added to the tint) */
    float spot[6][3][4]; int nSpots;     /* headlight cones: position + range, direction + cos outer, colour + cos inner */
    float night;                        /* 0..1: facades' windows (texture alpha 0.75) and signs (0.65) light up */
    float fadeStart, fadeEnd;           /* distance fade into the sky (fadeEnd 0 = none) */
    float puddles, time, water[4];      /* rain puddles 0..1, seconds (water waves), the sky colour water reflects + sun glints */
    float opt[4];                       /* options: [0] 1 = the new road surface (render3d.h, road vertex alpha 248); [1] 1 = window interiors; [2] the effects' reach (0: 7000) */
    float sun[4];                       /* the sun: direction to it (world) + how strongly it shapes the buildings' walls (0 = none) */
    int reflect; float reflectY;        /* 1 = the puddles mirror the world: it is drawn mirrored in the plane y = reflectY first (half size) */
    ID3D11ShaderResourceView *tracks; float trackScale, trackAmt;   /* snow tracks map (world x / z, wrapping every 1 / trackScale units), how much it clears the snow */
    float shadow; float shadowVP[16];   /* the buildings' shadows: how dark (0 = none), the sun's view-projection (orthographic, reversed depth): R3_Render draws the walls' depth map first */
    const R3Batch *staticBatches; int nStatic;
    const R3Batch *liveBatches; int nLive;   /* the live part (R3_SetLive) */
    const R3Vert *dyn; int nDyn; const R3Batch *dynBatches; int nDynBatches;   /* per-frame geometry (billboards) */
} R3Frame;

static void R3_DrawPass(R3 *r, ID3D11Buffer *vb, const R3Batch *b, int nb, int pass)   /* pass -1 = all batches, 0 = the opaque ones, 1 = the translucent ones, 2 = background (opaque, alpha 2) */
{
    UINT stride = sizeof(R3Vert), off = 0; int i;
    ID3D11DeviceContext_IASetVertexBuffers(r->ctx, 0, 1, &vb, &stride, &off);
    for (i = 0; i < nb; i++) {
        if (pass >= 0 && b[i].alpha != pass) continue;
        if (pass < 0 && b[i].alpha == 2) continue;
        if (b[i].count <= 0) continue;
        ID3D11ShaderResourceView *t = (b[i].tex >= R3_EXT && b[i].tex < R3_EXT + r->next) ? r->ext[b[i].tex - R3_EXT] : ((b[i].tex >= 0 && b[i].tex < r->ntex) ? r->tex[b[i].tex] : r->white);
        ID3D11DeviceContext_PSSetShaderResources(r->ctx, 0, 1, &t);
        if (b[i].alpha == 1) { ID3D11DeviceContext_OMSetBlendState(r->ctx, r->bsAlpha, NULL, 0xffffffff); ID3D11DeviceContext_OMSetDepthStencilState(r->ctx, r->dsTest, 0); }
        else if (b[i].alpha == 3) { ID3D11DeviceContext_OMSetBlendState(r->ctx, r->bsAdd, NULL, 0xffffffff); ID3D11DeviceContext_OMSetDepthStencilState(r->ctx, r->dsTest, 0); ID3D11DeviceContext_PSSetShader(r->ctx, r->psGlow, NULL, 0); }
        else if (b[i].alpha == 5) { ID3D11DeviceContext_OMSetBlendState(r->ctx, r->bsLens, NULL, 0xffffffff); ID3D11DeviceContext_OMSetDepthStencilState(r->ctx, r->dsTest, 0); ID3D11DeviceContext_PSSetShader(r->ctx, r->psLens, NULL, 0); }
        else if (b[i].alpha == 4) { ID3D11DeviceContext_OMSetBlendState(r->ctx, r->bsShadow, NULL, 0xffffffff); ID3D11DeviceContext_OMSetDepthStencilState(r->ctx, r->dsShadow, 0); }
        else { ID3D11DeviceContext_OMSetBlendState(r->ctx, r->bsOff, NULL, 0xffffffff); ID3D11DeviceContext_OMSetDepthStencilState(r->ctx, r->dsOn, 0); }
        ID3D11DeviceContext_Draw(r->ctx, b[i].count, b[i].first);
        if (b[i].alpha == 3 || b[i].alpha == 5) ID3D11DeviceContext_PSSetShader(r->ctx, r->ps, NULL, 0);
    }
}
static void R3_Draw(R3 *r, ID3D11Buffer *vb, const R3Batch *b, int nb) { R3_DrawPass(r, vb, b, nb, -1); }

/* the immediate context's state this renderer changes (no deferred context: SDL's device is single-threaded); SDL_FlushRenderer only invalidates
   part of the SDL renderer's cached state (it sets the input layout, constant buffers ... once), so all of it is put back */
typedef struct R3Saved {
    ID3D11InputLayout *il; D3D11_PRIMITIVE_TOPOLOGY topo; ID3D11Buffer *vb, *ib; UINT vbStride, vbOff, ibOff; DXGI_FORMAT ibFmt;
    ID3D11VertexShader *vs; ID3D11PixelShader *ps; ID3D11Buffer *vcb[2], *pcb[2]; ID3D11ShaderResourceView *srv[2]; ID3D11SamplerState *smp[2];
    ID3D11RasterizerState *rs; D3D11_VIEWPORT vp[4]; UINT nvp; D3D11_RECT sc[4]; UINT nsc;
    ID3D11BlendState *bs; FLOAT bf[4]; UINT sm; ID3D11DepthStencilState *ds; UINT sref;
    ID3D11RenderTargetView *rtv; ID3D11DepthStencilView *dsv;
} R3Saved;
static void R3_Save(ID3D11DeviceContext *c, R3Saved *s)
{
    memset(s, 0, sizeof *s);
    ID3D11DeviceContext_IAGetInputLayout(c, &s->il); ID3D11DeviceContext_IAGetPrimitiveTopology(c, &s->topo);
    ID3D11DeviceContext_IAGetVertexBuffers(c, 0, 1, &s->vb, &s->vbStride, &s->vbOff); ID3D11DeviceContext_IAGetIndexBuffer(c, &s->ib, &s->ibFmt, &s->ibOff);
    ID3D11DeviceContext_VSGetShader(c, &s->vs, NULL, NULL); ID3D11DeviceContext_PSGetShader(c, &s->ps, NULL, NULL);
    ID3D11DeviceContext_VSGetConstantBuffers(c, 0, 2, s->vcb); ID3D11DeviceContext_PSGetConstantBuffers(c, 0, 2, s->pcb);
    ID3D11DeviceContext_PSGetShaderResources(c, 0, 2, s->srv); ID3D11DeviceContext_PSGetSamplers(c, 0, 2, s->smp);
    ID3D11DeviceContext_RSGetState(c, &s->rs); s->nvp = 4; ID3D11DeviceContext_RSGetViewports(c, &s->nvp, s->vp); s->nsc = 4; ID3D11DeviceContext_RSGetScissorRects(c, &s->nsc, s->sc);
    ID3D11DeviceContext_OMGetBlendState(c, &s->bs, s->bf, &s->sm); ID3D11DeviceContext_OMGetDepthStencilState(c, &s->ds, &s->sref);
    ID3D11DeviceContext_OMGetRenderTargets(c, 1, &s->rtv, &s->dsv);
}
#define R3_REL(p) do { if (p) IUnknown_Release((IUnknown *)(p)); } while (0)
static void R3_Restore(ID3D11DeviceContext *c, R3Saved *s)
{
    int i;
    ID3D11DeviceContext_IASetInputLayout(c, s->il); ID3D11DeviceContext_IASetPrimitiveTopology(c, s->topo);
    ID3D11DeviceContext_IASetVertexBuffers(c, 0, 1, &s->vb, &s->vbStride, &s->vbOff); ID3D11DeviceContext_IASetIndexBuffer(c, s->ib, s->ibFmt, s->ibOff);
    ID3D11DeviceContext_VSSetShader(c, s->vs, NULL, 0); ID3D11DeviceContext_PSSetShader(c, s->ps, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(c, 0, 2, s->vcb); ID3D11DeviceContext_PSSetConstantBuffers(c, 0, 2, s->pcb);
    ID3D11DeviceContext_PSSetShaderResources(c, 0, 2, s->srv); ID3D11DeviceContext_PSSetSamplers(c, 0, 2, s->smp);
    ID3D11DeviceContext_RSSetState(c, s->rs); ID3D11DeviceContext_RSSetViewports(c, s->nvp, s->vp); ID3D11DeviceContext_RSSetScissorRects(c, s->nsc, s->sc);
    ID3D11DeviceContext_OMSetBlendState(c, s->bs, s->bf, s->sm); ID3D11DeviceContext_OMSetDepthStencilState(c, s->ds, s->sref);
    ID3D11DeviceContext_OMSetRenderTargets(c, 1, &s->rtv, s->dsv);
    R3_REL(s->il); R3_REL(s->vb); R3_REL(s->ib); R3_REL(s->vs); R3_REL(s->ps);
    for (i = 0; i < 2; i++) { R3_REL(s->vcb[i]); R3_REL(s->pcb[i]); R3_REL(s->srv[i]); R3_REL(s->smp[i]); }
    R3_REL(s->rs); R3_REL(s->bs); R3_REL(s->ds); R3_REL(s->rtv); R3_REL(s->dsv);
}

static void R3_Mul(float *o, const float *a, const float *b);   /* (below) */
/* the puddles' mirror picture: a colour target the main pass samples and its own depth buffer */
static int R3_EnsureRefl(R3 *r, int w, int h)
{
    D3D11_TEXTURE2D_DESC td; ID3D11Texture2D *t = NULL;
    if (r->rfRtv && r->rw == w && r->rh == h) return 1;
    R3_REL(r->rfRtv); R3_REL(r->rfSrv); R3_REL(r->rfDsv); R3_REL(r->rfDepth); r->rfRtv = NULL; r->rfSrv = NULL; r->rfDsv = NULL; r->rfDepth = NULL; r->rw = r->rh = 0;
    memset(&td, 0, sizeof td); td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(r->dev, &td, NULL, &t))) return 0;
    ID3D11Device_CreateRenderTargetView(r->dev, (ID3D11Resource *)t, NULL, &r->rfRtv); ID3D11Device_CreateShaderResourceView(r->dev, (ID3D11Resource *)t, NULL, &r->rfSrv);
    ID3D11Texture2D_Release(t);
    td.Format = DXGI_FORMAT_D32_FLOAT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(ID3D11Device_CreateTexture2D(r->dev, &td, NULL, &r->rfDepth))) ID3D11Device_CreateDepthStencilView(r->dev, (ID3D11Resource *)r->rfDepth, NULL, &r->rfDsv);
    if (!r->rfRtv || !r->rfSrv || !r->rfDsv) return 0;
    r->rw = w; r->rh = h; return 1;
}

/* the sun's shadow map: a depth texture R3_SHMAP square, drawn from the sun (R3Frame.shadowVP) and compared in the main pass (t3 / s1) */
#define R3_SHMAP 2048
static int R3_EnsureShadow(R3 *r)
{
    D3D11_TEXTURE2D_DESC td; D3D11_DEPTH_STENCIL_VIEW_DESC dv; D3D11_SHADER_RESOURCE_VIEW_DESC sv;
    if (r->shSrv) return 1;
    if (!r->vsSh || !r->psSh || !r->smpCmp) return 0;
    memset(&td, 0, sizeof td); td.Width = td.Height = R3_SHMAP; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_TYPELESS; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(ID3D11Device_CreateTexture2D(r->dev, &td, NULL, &r->shTex))) return 0;
    memset(&dv, 0, sizeof dv); dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    memset(&sv, 0, sizeof sv); sv.Format = DXGI_FORMAT_R32_FLOAT; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = 1;
    if (FAILED(ID3D11Device_CreateDepthStencilView(r->dev, (ID3D11Resource *)r->shTex, &dv, &r->shDsv)) || FAILED(ID3D11Device_CreateShaderResourceView(r->dev, (ID3D11Resource *)r->shTex, &sv, &r->shSrv))) {
        R3_REL(r->shDsv); R3_REL(r->shSrv); R3_REL(r->shTex); r->shDsv = NULL; r->shSrv = NULL; r->shTex = NULL; return 0; }
    return 1;
}
/* GPU time of the world passes (RR_PROFILE, user 2026-11-15: the Steam Deck's 50 fps did not change with the CPU clock): timestamp queries round the
   shadow map, the puddle mirror and the world, read back 3 frames later without waiting; the milliseconds go to the shim's profiler (g_pfGpu[0..2],
   rrddraw.c).  r3mark: 0 = start (disjoint begun), 1 = after the shadow map, 2 = after the mirror, 3 = after the world (disjoint ended, oldest read) */
static void R3_GpuMark(R3 *r, int k)
{   int i;
    if (g_pfOn <= 0) return;
    if (!r->qMade) { D3D11_QUERY_DESC qd; r->qMade = 1; memset(&qd, 0, sizeof qd);
        for (i = 0; i < 4; i++) { int j; qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT; ID3D11Device_CreateQuery(r->dev, &qd, &r->qDis[i]);
            qd.Query = D3D11_QUERY_TIMESTAMP; for (j = 0; j < 4; j++) ID3D11Device_CreateQuery(r->dev, &qd, &r->qTs[i][j]); } }
    if (!r->qDis[r->qi] || !r->qTs[r->qi][k]) return;
    if (k == 0) ID3D11DeviceContext_Begin(r->ctx, (ID3D11Asynchronous *)r->qDis[r->qi]);
    ID3D11DeviceContext_End(r->ctx, (ID3D11Asynchronous *)r->qTs[r->qi][k]);
    if (k == 3) { int o = (r->qi + 1) & 3; D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 t[4];
        ID3D11DeviceContext_End(r->ctx, (ID3D11Asynchronous *)r->qDis[r->qi]);
        r->qi = o;   /* (the oldest slot: written 3 frames ago - read it, then reuse it) */
        if (ID3D11DeviceContext_GetData(r->imm, (ID3D11Asynchronous *)r->qDis[o], &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint && dj.Frequency) {
            for (i = 0; i < 4; i++) if (ID3D11DeviceContext_GetData(r->imm, (ID3D11Asynchronous *)r->qTs[o][i], &t[i], sizeof t[i], D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) return;
            for (i = 0; i < 3; i++) g_pfGpu[i] += (double)(t[i + 1] - t[i]) * 1000.0 / (double)dj.Frequency;
            g_pfGpuN++; } } }
/* the frame's constants (render3d.h r3_hlsl cbuffer C); vpm = the view-projection to draw with, rfl = its rfl row */
static void R3_Consts(R3 *r, const R3Frame *f, const float *vpm, const float *rfl)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(r->ctx, (ID3D11Resource *)r->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float *c = (float *)m.pData;
        memcpy(c, vpm, 64); memcpy(c + 16, f->fogColour, 16); c[20] = f->fogStart; c[21] = f->fogScale; c[22] = f->fogMax; c[23] = f->useBright ? f->bright : 1.0f; c[24] = f->right[0]; c[25] = f->right[1]; c[26] = f->right[2]; c[27] = f->crisp ? 1.0f : 0.0f; c[28] = f->eye[0]; c[29] = f->eye[1]; c[30] = f->eye[2]; c[31] = f->bias;
        if (f->useTint) memcpy(c + 32, f->tint, 16); else c[32] = c[33] = c[34] = c[35] = 1.0f;
        c[36] = (float)(f->nLights < 16 ? f->nLights : 16); c[37] = f->snow; c[38] = 1.0f; c[39] = f->clipSpace ? 1.0f : 0.0f;
        memcpy(c + 40, f->light, 256); memcpy(c + 104, f->lightCol, 256); { int s; for (s = 0; s < 6; s++) { memcpy(c + 168 + s * 4, f->spot[s][0], 16); memcpy(c + 192 + s * 4, f->spot[s][1], 16); memcpy(c + 216 + s * 4, f->spot[s][2], 16); } }
        c[240] = f->trackScale; c[241] = f->tracks ? f->trackAmt : 0.0f; c[242] = (float)(f->nSpots < 6 ? f->nSpots : 6); c[243] = f->night;
        c[244] = f->fadeStart; c[245] = f->fadeEnd > f->fadeStart ? 1.0f / (f->fadeEnd - f->fadeStart) : 0.0f; c[246] = f->puddles; c[247] = f->time; memcpy(c + 248, f->water, 16);
        memcpy(c + 252, rfl, 16); memcpy(c + 256, f->sun, 16); memcpy(c + 260, f->opt, 16);
        memcpy(c + 264, f->shadowVP, 64); c[280] = r->shSrv ? f->shadow : 0.0f; c[281] = 1.0f / R3_SHMAP; c[282] = 0.0004f; c[283] = 0;
        ID3D11DeviceContext_Unmap(r->ctx, (ID3D11Resource *)r->cb, 0);
    }
}
/* the world's batches into the bound target, viewport vpt: the opaque batches of both parts, the background (skirts), the translucent ones (they test
   the depth the others wrote), then the per-frame geometry (dyn: uploaded already) */
static void R3_DrawWorld(R3 *r, const R3Frame *f, const D3D11_VIEWPORT *vpt, int dyn)
{
    int k; static const int order[3] = { 0, 2, 1 };
    for (k = 0; k < 3; k++) { int pass = order[k];
        if (pass == 2) { D3D11_VIEWPORT bv = *vpt; bv.MinDepth = 0.0f; bv.MaxDepth = 1e-6f; ID3D11DeviceContext_RSSetViewports(r->ctx, 1, &bv); }   /* (reversed depth: the far end; behind all the world) */
        if (r->vbStatic && f->nStatic) R3_DrawPass(r, r->vbStatic, f->staticBatches, f->nStatic, pass);
        if (r->vbLive && f->nLive) R3_DrawPass(r, r->vbLive, f->liveBatches, f->nLive, pass);
        if (pass == 2) ID3D11DeviceContext_RSSetViewports(r->ctx, 1, vpt);
    }
    if (dyn) R3_Draw(r, r->vbDyn, f->dynBatches, f->nDynBatches);
}

static int R3_Render(R3 *r, const R3Frame *f)
{
    ID3D11RenderTargetView *rtv = NULL; D3D11_RENDER_TARGET_VIEW_DESC rv; D3D11_TEXTURE2D_DESC td; D3D11_VIEWPORT vpt; D3D11_MAPPED_SUBRESOURCE m;
    D3D11_RECT sc; R3Saved saved; float rfl[4] = { 0, 0, 0, 0 }; int dyn = 0, mirror; ID3D11ShaderResourceView *none = NULL;
    if (!f->target) return 0;
    ID3D11Texture2D_GetDesc(f->target, &td);
    memset(&rv, 0, sizeof rv); rv.Format = td.Format; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(ID3D11Device_CreateRenderTargetView(r->dev, (ID3D11Resource *)f->target, &rv, &rtv))) return 0;
    if (!r->deferred) R3_Save(r->imm, &saved);
    R3_Ensure(r, td.Width, td.Height);
    mirror = f->reflect && f->puddles > 0 && !f->clipSpace && R3_EnsureRefl(r, (td.Width + 1) / 2, (td.Height + 1) / 2);
    ID3D11DeviceContext_RSSetState(r->ctx, r->rs);
    ID3D11DeviceContext_IASetInputLayout(r->ctx, r->il);
    ID3D11DeviceContext_IASetPrimitiveTopology(r->ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(r->ctx, r->vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(r->ctx, r->ps, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(r->ctx, 0, 1, &r->cb);
    ID3D11DeviceContext_PSSetConstantBuffers(r->ctx, 0, 1, &r->cb);
    ID3D11DeviceContext_PSSetSamplers(r->ctx, 0, 1, &r->smp);
    { ID3D11ShaderResourceView *k = f->tracks ? f->tracks : r->white; ID3D11DeviceContext_PSSetShaderResources(r->ctx, 1, 1, &k); }
    { ID3D11ShaderResourceView *k = r->waterN ? r->waterN : r->white; ID3D11DeviceContext_PSSetShaderResources(r->ctx, 4, 1, &k); }   /* (the sea's bump map) */
    ID3D11DeviceContext_PSSetShaderResources(r->ctx, 2, 1, &none);
    if (f->nDyn > 0 && f->nDynBatches > 0) {
        if (f->nDyn > r->capDyn) { if (r->vbDyn) ID3D11Buffer_Release(r->vbDyn); r->capDyn = f->nDyn * 2; r->vbDyn = R3_Buffer(r, NULL, r->capDyn, 1); }
        if (r->vbDyn && SUCCEEDED(ID3D11DeviceContext_Map(r->ctx, (ID3D11Resource *)r->vbDyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, f->dyn, sizeof(R3Vert) * f->nDyn);
            ID3D11DeviceContext_Unmap(r->ctx, (ID3D11Resource *)r->vbDyn, 0);
            dyn = 1;
        }
    }
    ID3D11DeviceContext_PSSetShaderResources(r->ctx, 3, 1, &none);
    if (!f->clipSpace) R3_GpuMark(r, 0);   /* (RR_PROFILE: GPU time per pass) */
    if (f->shadow > 0 && !f->clipSpace && R3_EnsureShadow(r)) {   /* the buildings' walls' depth from the sun (vsshadow / psshadow), no colour */
        D3D11_VIEWPORT sv; float z4[4] = { 0, 0, 0, 0 };
        R3_Consts(r, f, f->shadowVP, z4);
        ID3D11DeviceContext_OMSetRenderTargets(r->ctx, 0, NULL, r->shDsv);
        ID3D11DeviceContext_ClearDepthStencilView(r->ctx, r->shDsv, D3D11_CLEAR_DEPTH, 0.0f, 0);   /* (reversed: 0 = far, nothing) */
        sv.TopLeftX = sv.TopLeftY = 0; sv.Width = sv.Height = (float)R3_SHMAP; sv.MinDepth = 0; sv.MaxDepth = 1;
        ID3D11DeviceContext_RSSetViewports(r->ctx, 1, &sv);
        { D3D11_RECT ss; ss.left = ss.top = 0; ss.right = ss.bottom = R3_SHMAP; ID3D11DeviceContext_RSSetScissorRects(r->ctx, 1, &ss); }
        ID3D11DeviceContext_VSSetShader(r->ctx, r->vsSh, NULL, 0); ID3D11DeviceContext_PSSetShader(r->ctx, r->psSh, NULL, 0);
        if (r->vbStatic && f->nStatic) R3_DrawPass(r, r->vbStatic, f->staticBatches, f->nStatic, 0);
        if (r->vbLive && f->nLive) R3_DrawPass(r, r->vbLive, f->liveBatches, f->nLive, 0);
        ID3D11DeviceContext_OMSetRenderTargets(r->ctx, 0, NULL, NULL);
        ID3D11DeviceContext_VSSetShader(r->ctx, r->vs, NULL, 0); ID3D11DeviceContext_PSSetShader(r->ctx, r->ps, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(r->ctx, 3, 1, &r->shSrv); ID3D11DeviceContext_PSSetSamplers(r->ctx, 1, 1, &r->smpCmp);
    }
    if (!f->clipSpace) R3_GpuMark(r, 1);
    if (mirror) {   /* the world mirrored in the plane y = reflectY (p' = p * M * vp, M: y -> 2 reflectY - y), what is above it only, at half size */
        float mm[16], mvp[16], clr[4] = { 0, 0, 0, 0 }; D3D11_VIEWPORT hv; D3D11_RECT hs;
        memset(mm, 0, sizeof mm); mm[0] = 1; mm[5] = -1; mm[10] = 1; mm[13] = 2 * f->reflectY; mm[15] = 1;
        R3_Mul(mvp, mm, f->vp);
        rfl[0] = f->reflectY; rfl[1] = 1;
        R3_Consts(r, f, mvp, rfl);
        ID3D11DeviceContext_ClearRenderTargetView(r->ctx, r->rfRtv, clr); ID3D11DeviceContext_ClearDepthStencilView(r->ctx, r->rfDsv, D3D11_CLEAR_DEPTH, 0.0f, 0);
        hv.TopLeftX = f->x * 0.5f; hv.TopLeftY = f->y * 0.5f; hv.Width = f->w * 0.5f; hv.Height = f->h * 0.5f; hv.MinDepth = 0; hv.MaxDepth = 1;
        hs.left = f->x / 2; hs.top = f->y / 2; hs.right = (f->x + f->w + 1) / 2; hs.bottom = (f->y + f->h + 1) / 2;
        ID3D11DeviceContext_OMSetRenderTargets(r->ctx, 1, &r->rfRtv, r->rfDsv);
        ID3D11DeviceContext_RSSetViewports(r->ctx, 1, &hv); ID3D11DeviceContext_RSSetScissorRects(r->ctx, 1, &hs);
        R3_DrawWorld(r, f, &hv, 0);   /* (the world only: no cars, no light halos) */
        ID3D11DeviceContext_OMSetRenderTargets(r->ctx, 0, NULL, NULL);
        ID3D11DeviceContext_PSSetShaderResources(r->ctx, 2, 1, &r->rfSrv);
        rfl[1] = 0; rfl[2] = 1.0f / td.Width; rfl[3] = 1.0f / td.Height;
    }
    if (!f->clipSpace) R3_GpuMark(r, 2);
    if (f->doClear) {
        if (f->x == 0 && f->y == 0 && f->w == (int)td.Width && f->h == (int)td.Height) ID3D11DeviceContext_ClearRenderTargetView(r->ctx, rtv, f->clear);
    }
    if (!f->keepDepth) ID3D11DeviceContext_ClearDepthStencilView(r->ctx, r->dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);   /* reversed depth: 0 = far */
    R3_Consts(r, f, f->vp, rfl);
    vpt.TopLeftX = (float)f->x; vpt.TopLeftY = (float)f->y; vpt.Width = (float)f->w; vpt.Height = (float)f->h; vpt.MinDepth = 0; vpt.MaxDepth = 1;
    sc.left = f->x; sc.top = f->y; sc.right = f->x + f->w; sc.bottom = f->y + f->h;
    ID3D11DeviceContext_OMSetRenderTargets(r->ctx, 1, &rtv, r->dsv);
    ID3D11DeviceContext_RSSetViewports(r->ctx, 1, &vpt);
    ID3D11DeviceContext_RSSetScissorRects(r->ctx, 1, &sc);
    R3_DrawWorld(r, f, &vpt, dyn);
    if (!f->clipSpace) R3_GpuMark(r, 3);
    ID3D11DeviceContext_PSSetShaderResources(r->ctx, 2, 1, &none); ID3D11DeviceContext_PSSetShaderResources(r->ctx, 3, 1, &none);
    if (r->deferred) {
        ID3D11CommandList *cl = NULL;
        if (SUCCEEDED(ID3D11DeviceContext_FinishCommandList(r->ctx, FALSE, &cl))) { ID3D11DeviceContext_ExecuteCommandList(r->imm, cl, TRUE); ID3D11CommandList_Release(cl); }
    } else R3_Restore(r->imm, &saved);
    ID3D11RenderTargetView_Release(rtv);
    return 1;
}

/* ---- matrices (row vectors: p' = p * M) ---- */
static void R3_Mul(float *o, const float *a, const float *b)
{
    float t[16]; int i, j, k;
    for (i = 0; i < 4; i++) for (j = 0; j < 4; j++) { float s = 0; for (k = 0; k < 4; k++) s += a[i * 4 + k] * b[k * 4 + j]; t[i * 4 + j] = s; }
    memcpy(o, t, sizeof t);
}
/* view matrix: eye, forward and up (orthonormalised here), left-handed (x right, y up, z forward) */
static void R3_LookTo(float *m, const float *eye, const float *fwd, const float *up)
{
    float z[3], x[3], y[3], l; int i;
    l = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]); for (i = 0; i < 3; i++) z[i] = fwd[i] / l;
    x[0] = up[1] * z[2] - up[2] * z[1]; x[1] = up[2] * z[0] - up[0] * z[2]; x[2] = up[0] * z[1] - up[1] * z[0];
    l = sqrtf(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]); for (i = 0; i < 3; i++) x[i] /= l;
    y[0] = z[1] * x[2] - z[2] * x[1]; y[1] = z[2] * x[0] - z[0] * x[2]; y[2] = z[0] * x[1] - z[1] * x[0];
    m[0] = x[0]; m[1] = y[0]; m[2] = z[0]; m[3] = 0;
    m[4] = x[1]; m[5] = y[1]; m[6] = z[1]; m[7] = 0;
    m[8] = x[2]; m[9] = y[2]; m[10] = z[2]; m[11] = 0;
    m[12] = -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]);
    m[13] = -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]);
    m[14] = -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]); m[15] = 1;
}
/* perspective with focal lengths fx, fy in pixels of a w x h viewport and the projection centre (cx, cy) in pixels (the original's centre is not the
   middle of the viewport: horizon at 90 px) */
static void R3_Persp(float *m, float fx, float fy, float cx, float cy, int w, int h, float zn, float zf)
{
    memset(m, 0, 64);
    m[0] = 2 * fx / w;
    m[5] = 2 * fy / h;
    m[8] = 2 * cx / w - 1;              /* x_ndc = (x * 2fx/w) / z + (2cx/w - 1) */
    m[9] = 1 - 2 * cy / h;
    /* reversed depth: z = zn (zf - d) / (d (zf - zn)), 1 at the near plane, 0 at the far one (cleared to 0, tested GREATER_EQUAL).  With the
       float depth buffer this keeps its precision at any distance (the usual 0 = near puts it all next to the camera): faces a unit apart
       (a window on a car, the sidewalk corner over the ground) no longer flicker far away.  R3_Depth gives the same value for a distance */
    m[10] = -zn / (zf - zn); m[11] = 1;
    m[14] = zn * zf / (zf - zn);
}
static double R3_Depth(double d, double zn, double zf) { return zn * (zf - d) / (d * (zf - zn)); }   /* depth buffer value at distance d (R3_Persp) */
#endif
