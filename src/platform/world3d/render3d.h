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
    ID3D11Buffer *cb, *vbStatic, *vbDyn, *vbLive; int nStatic, capDyn, nLive;
    ID3D11RasterizerState *rs; ID3D11DepthStencilState *dsOn, *dsTest; ID3D11BlendState *bsOff, *bsAlpha; ID3D11SamplerState *smp;
    ID3D11Texture2D *depth; ID3D11DepthStencilView *dsv; int dw, dh;
    ID3D11Texture2D *rfDepth; ID3D11DepthStencilView *rfDsv; ID3D11RenderTargetView *rfRtv; ID3D11ShaderResourceView *rfSrv; int rw, rh;   /* the puddles' mirror picture (R3Frame.reflect), half size */
    ID3D11ShaderResourceView *tex[R3_MAXTEX]; int ntex;
    ID3D11ShaderResourceView *ext[R3_MAXEXT]; int next;   /* views of textures owned by others (R3_External), batch tex = R3_EXT + index; released by R3_ExternalClear */
    ID3D11ShaderResourceView *white;
    char err[512];
} R3;

typedef struct R3Batch { int first, count, tex; int alpha; } R3Batch;   /* tex = index from R3_Texture (-1 = flat colour); alpha = blended, depth not written (3 = added: light halos; 4 = a shadow: darkens, once per pixel - stencil) */

static const char r3_hlsl[] =
    "cbuffer C : register(b0) { row_major float4x4 vp; float4 fogc; float4 fogp; float4 right; float4 eye; float4 tint; float4 lit; float4 lp[16]; float4 lc[16];\n"
    "  float4 spP[6]; float4 spD[6]; float4 spC[6]; float4 trk; float4 fad; float4 wat; float4 rfl; float4 sun; float4 opt; };\n"   /* spot lights (the bikes' headlights): position + range, direction + cos outer, colour + cos inner; trk.x = 1 / the tracks map's size, y = their strength, z = spots, w = night;
                                                                                                fad = distance fade start, 1 / its length (0 = none), rain puddles, time (s); wat = the sky colour water reflects, sun glints;
                                                                                                rfl = the mirror plane's height, 1 = this is the mirror pass, 1 / target size (0 = no mirror picture: puddles show the sky only) */
    "struct VI { float3 p : POSITION; float2 t : TEXCOORD0; float4 c : COLOR; float k : TEXCOORD1; float2 o : TEXCOORD2; };\n"
    "struct VO { float4 p : SV_Position; float2 t : TEXCOORD0; float4 c : COLOR; float d : TEXCOORD1; float3 w : TEXCOORD2; };\n"
    "Texture2D T : register(t0); Texture2D K : register(t1); Texture2D Rf : register(t2); SamplerState S : register(s0);\n"
    "VO vsmain(VI i) { VO o; float3 w = i.p + right.xyz * i.o.x + float3(0, i.o.y, 0);\n"   /* billboards: upright, facing the camera */
    "  if (lit.w > 0) { o.p = float4(i.p, 1); o.w = float3(i.k, i.o.x, i.o.y); o.d = length(o.w - eye.xyz); o.t = i.t; o.c = i.c; return o; }\n"   /* lit.w: clip-space vertices (the sprites pass), (k, o) = their place in the world (lights, fog) */
    "  if (eye.w > 0 && any(i.o != 0)) { float3 d = w - eye.xyz; float l = length(d); w = eye.xyz + d * (max(l - eye.w, l * 0.5) / l); }\n"   /* billboards: eye.w units nearer along the view ray (same place on screen): the ground / a rail just in front of the foot cannot cover them (the game draws sprites over their surroundings) */
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
    "  float4 c = T.SampleGrad(S, uv, dx, dy) * i.c; if (c.a < 0.5 || (rfl.y > 0 && (i.w.y < rfl.x - 40 || abs(normalize(n).y) > 0.8))) discard;\n"   /* (the mirror pass: only what stands up above the mirror - buildings, hillsides; not the road / ground) */
    "  float y = dot(c.rgb, float3(0.299, 0.587, 0.114)); float3 a = tint.rgb; int k; c.rgb = lerp(y.xxx, c.rgb, tint.w);\n"   /* time of day / weather: saturation, then the light */
    "  bool road = abs(i.c.a - 0.9725) < 0.0015;\n"   /* the road surface (vertex alpha 248, course3d.h C3_Lanes) */
    "  if (opt.x > 0 && road) { float2 q = i.w.xz; float mk = saturate(smoothstep(0.42, 0.55, y) + saturate((c.r - c.b) * 3 - 0.2)), nk;\n"   /* the new road (opt.x): the paint */
    "    float ag = Hu(floor(q / 4)) * 0.6 + Hu(floor(q / 9) + 3.7) * 0.4, pt = Nu(q / 420) * 0.6 + Nu(q / 130 + 2.2) * 0.4; nk = Nu(q / 260 + 9.1);\n"   /* kept, the rest fresh asphalt: */
    "    float3 asp = float3(0.18, 0.18, 0.19) * (0.78 + 0.35 * pt) + (ag - 0.5) * 0.06 * saturate(1 - i.d / 3000);\n"                                        /* fine aggregate (near), darker / */
    "    asp *= 1 - 0.45 * smoothstep(0.494, 0.5, nk) * smoothstep(0.506, 0.5, nk); c.rgb = lerp(asp, c.rgb, mk); }\n"                                            /* lighter patches, tar-sealed cracks */
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
    "  if (sun.w > 0 && abs(i.c.a - 0.996) < 0.002) { float3 nn = normalize(n); if (dot(nn, eye.xyz - i.w) < 0) nn = -nn;\n"   /* a building's wall (vertex alpha 254): lit by the sun - */
    "    c.rgb *= lerp(1, 0.66 + 0.52 * saturate(dot(nn, sun.xyz)), sun.w); }\n"                                                                /* brighter facing it, in shade away from it */
    "  if (abs(i.c.a - 0.992) < 0.002) { float2 p = i.w.xz, g = 0; float t = fad.w, h = 0, fr, sp, fo, lod = saturate(i.d / 30000); float3 V = normalize(eye.xyz - i.w), Nw, Rv, sk, L;\n"   /* water (vertex alpha 253, course3d.h C3_Sea): a sea */
    "    [unroll] for (k = 0; k < 6; k++) { float wl = 2200 * pow(0.6, k), am = wl * 0.022 * saturate(1.5 - i.d / (wl * 9)), an = k * 1.7 + 0.4, ph; float2 dd = float2(cos(an), sin(an));\n"   /* of directional swells, long to short (the short */
    "      ph = dot(p, dd) * 6.2832 / wl + t * sqrt(9.8 * 6.2832 / wl * 900); h += am * sin(ph); g += am * cos(ph) * 6.2832 / wl * dd; }\n"                                         /* ones fade with the distance), their slopes */
    "    g += (float2(N(p / 60 + t * 0.6), N(p / 60 - t * 0.5 + 7.3)) - 0.5) * 0.25 * saturate(1 - i.d / 6000);\n"                                                                                   /* + ripples */
    "    Nw = normalize(float3(-g.x, 1, -g.y)); Rv = reflect(-V, Nw); fr = 0.02 + 0.98 * pow(1 - saturate(dot(Nw, V)), 5);\n"                                                        /* -> normal; Fresnel */
    "    sk = lerp(wat.rgb * 0.6 + float3(0.02, 0.05, 0.08), float3(0.16, 0.30, 0.50) * (0.4 + 0.6 * max(max(wat.r, wat.g), wat.b)), saturate(Rv.y * 3));\n"   /* the sky it reflects: a deeper blue than the haze */
    "    { float pt = N(p / 5000 + t * 0.004) * 0.6 + N(p / 1400 - t * 0.01) * 0.4; float3 body = lerp(float3(0.02, 0.10, 0.20), float3(0.03, 0.20, 0.24), pt);\n"   /* the water's own colour: navy / teal patches */
    "      body += float3(0.02, 0.10, 0.09) * saturate(h / 80 + 0.3) * saturate(1 - V.y);\n"                                                                                                   /* light through the crests */
    "      c.rgb = lerp(body, sk, min(fr * 1.1, 0.55)); }\n"                                                                                                                                  /* Fresnel, capped: never a mirror */
    "    L = normalize(float3(-V.x, 0.22, -V.z)); sp = min(pow(saturate(dot(Rv, L)), 300) * 1.6, 1.2) * smoothstep(0.62, 0.85, N(p / 9 + t * 1.5));\n"                                                                                              /* the sun's glints, small */
    "    fo = smoothstep(0.85, 1.05, h / 110 + N(p / 35 + t * 0.3) * 0.3) * saturate(1 - i.d / 7000);\n"                                                                                     /* a few white caps */
    "    c.rgb = lerp(c.rgb, float3(0.8, 0.84, 0.86), fo * 0.35) + sp * wat.w * float3(1, 0.95, 0.82) / max(tint.rgb, 0.2); c.a = 1; }\n"
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
    "  if (fad.z > 0 && lit.w == 0 && rfl.y == 0 && i.d < 5000 && abs(i.c.a - 0.976) > 0.0015 && abs(i.c.a - 0.992) >= 0.002) { float2 p = i.w.xz / 45, q, cl; float rh = 0; int m;\n"   /* rain hitting the */
    "    [unroll] for (m = 0; m < 2; m++) { float2 pp = p + m * 0.5; cl = floor(pp); q = frac(pp) - 0.5 - (float2(Hu(cl + 2.1), Hu(cl + 4.7)) - 0.5) * 0.5;\n"                          /* ground: in each cell a */
    "      { float ph = frac(fad.w * 1.6 + Hu(cl + m * 3.3)), r = length(q) * 45, rr = ph * 60; rh += step(ph, 0.22) * saturate(1 - abs(r - rr) / 2.5) * (1 - ph / 0.22); } }\n"   /* drop's ring now and then, */
    "    c.rgb += rh * 0.07 * tint.rgb * saturate((abs(normalize(n).y) - 0.8) * 8) * saturate(1 - i.d / 5000); }\n"                                                                  /* faint, near only */
    "  if (trk.w > 0 && abs(i.c.a - 0.996) < 0.002 && abs(c.a - 0.75) < 0.03) { float3 ec = H(floor(i.w.xz / 700)) < 0.3 ? float3(0.75, 0.85, 1.0) : float3(1.0, 0.76, 0.42);\n"   /* a facade's window (alpha 0.75, world3d.inc): lit at night in */
    "    c.rgb = lerp(c.rgb, ec * saturate(0.3 + 1.5 * y) * 0.85, trk.w * step(0.25, H(floor(i.w.xz / 900) + 3.1))); }\n"                                        /* three buildings out of four, warm or cool */
    "  if (trk.w > 0 && abs(i.c.a - 0.996) < 0.002 && abs(c.a - 0.65) < 0.03) c.rgb = lerp(c.rgb, c.rgb * 1.6 / max(max(tint.r, tint.g), 0.3) + 0.08, trk.w);\n"                 /* a shop sign (alpha 0.65): glowing in its own colour */
    "  if (opt.x > 0 && road && sun.w > 0) { float3 V = normalize(eye.xyz - i.w), R = reflect(-V, float3(0, 1, 0)); float d2 = saturate(dot(R, sun.xyz));\n"   /* the new road in the sun: a */
    "    c.rgb += (pow(d2, 8) * 0.22 + pow(d2, 60) * 0.35 + pow(d2, 400) * 0.6) * sun.w * float3(1, 0.97, 0.9) + pow(1 - saturate(V.y * 4), 3) * 0.07 * sun.w * fogc.rgb; }\n"   /* sheen towards the sun, glossy far off */
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
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
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
    memset(&bd, 0, sizeof bd); bd.ByteWidth = 264 * 4;   /* (R3_Render: the constants) */ bd.Usage = D3D11_USAGE_DYNAMIC; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
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
    r->white = R3_MakeTexture(r, whitePx, 2, 2);
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
static void R3_SetLive(R3 *r, const R3Vert *v, int n)
{
    if (r->vbLive) ID3D11Buffer_Release(r->vbLive);
    r->vbLive = n > 0 ? R3_Buffer(r, v, n, 0) : NULL; r->nLive = r->vbLive ? n : 0;
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
    float opt[4];                       /* options: [0] 1 = the new road surface (render3d.h, road vertex alpha 248) */
    float sun[4];                       /* the sun: direction to it (world) + how strongly it shapes the buildings' walls (0 = none) */
    int reflect; float reflectY;        /* 1 = the puddles mirror the world: it is drawn mirrored in the plane y = reflectY first (half size) */
    ID3D11ShaderResourceView *tracks; float trackScale, trackAmt;   /* snow tracks map (world x / z, wrapping every 1 / trackScale units), how much it clears the snow */
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
    ID3D11DeviceContext_PSSetShaderResources(r->ctx, 2, 1, &none);
    if (f->nDyn > 0 && f->nDynBatches > 0) {
        if (f->nDyn > r->capDyn) { if (r->vbDyn) ID3D11Buffer_Release(r->vbDyn); r->capDyn = f->nDyn * 2; r->vbDyn = R3_Buffer(r, NULL, r->capDyn, 1); }
        if (r->vbDyn && SUCCEEDED(ID3D11DeviceContext_Map(r->ctx, (ID3D11Resource *)r->vbDyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
            memcpy(m.pData, f->dyn, sizeof(R3Vert) * f->nDyn);
            ID3D11DeviceContext_Unmap(r->ctx, (ID3D11Resource *)r->vbDyn, 0);
            dyn = 1;
        }
    }
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
    ID3D11DeviceContext_PSSetShaderResources(r->ctx, 2, 1, &none);
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
