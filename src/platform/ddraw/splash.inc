/* ---- the opening splash (the logo picture shown while the game loads, src\system\startup.c): "NEXT" under the ROAD RASH logo, and the version bottom right (from Present, window pixels) ----
 * The logo's own look: tall condensed square letters, red with black cracks running through them, on a black panel with a thin red edge (as ROAD).
 * Drawn over the picture where the logo is (640 x 480 picture units, scaled to where SDL put the picture), dimmed with the game's palette fades: the
 * logo's brightness now against the brightest seen while the splash is up.  The version is RR_VERSION (scripts\build_ddraw.ps1: 0.1.<build number>). */
#include "rr_version.h"
#define SP_X 368   /* the NEXT panel in picture units: under RASH, its right edge with the logo's */
#define SP_Y 237
#define SP_W 137
#define SP_H 38
static SDL_Texture *g_spTex, *g_spVer; static int g_spTexW, g_spVerPx, g_spVerW, g_spVerH; static float g_spMax;
static float SpHash(int x, int y) { unsigned u = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u; u ^= u >> 13; u *= 1274126177u; u ^= u >> 16; return (u & 0xffff) / 65535.0f; }
static float SpNoise(float x, float y)   /* value noise, smooth */
{   int ix = (int)floorf(x), iy = (int)floorf(y); float fx = x - ix, fy = y - iy, a, b, c, d;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    a = SpHash(ix, iy); b = SpHash(ix + 1, iy); c = SpHash(ix, iy + 1); d = SpHash(ix + 1, iy + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy; }
static float SpCrack(float x, float y)   /* 1 = a crack at picture point x, y: thin veins where the noise crosses its middle */
{   float n = SpNoise(x * 0.11f, y * 0.11f) * 0.65f + SpNoise(x * 0.31f + 17, y * 0.31f + 5) * 0.35f, d = fabsf(n - 0.5f), m = SpNoise(x * 0.05f + 40, y * 0.05f);
    float w = 0.012f + 0.02f * m;   /* (some stretches thicker) */
    return d < w ? 1 - d / w : 0; }
static void SpMake(int w)   /* the NEXT panel at w window pixels wide */
{   HDC dc = CreateCompatibleDC(NULL); HFONT f, o; SIZE sz; int h = (int)(w * (float)SP_H / SP_W + 0.5f), x, y, b; float s = w / (float)SP_W; BYTE *m = NULL; DWORD *out;
    if (g_spTex) { SDL_DestroyTexture(g_spTex); g_spTex = NULL; }
    if (!dc) return;
    f = CreateFontA(-(int)(27 * s / 0.70f), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_BAHN_SEMI));   /* (cap height 27 units, as RASH) */
    o = (HFONT)SelectObject(dc, f); SetTextCharacterExtra(dc, (int)(3 * s)); GetTextExtentPoint32A(dc, "NEXT", 4, &sz);
    free(SgMask(dc, "NEXT", 1, 1, 0, 0));   /* (warm-up: the first DIB of a fresh DC) */
    {   TEXTMETRICA tm; GetTextMetricsA(dc, &tm);   /* centred on the caps: the cap top sits at the internal leading + (ascent - cap height) */
        m = SgMask(dc, "NEXT", w, h, (w - sz.cx + (int)(3 * s)) / 2, (int)(h / 2.0f - (tm.tmAscent - 27 * s) - 27 * s / 2)); }
    b = (int)(1.6f * s + 0.5f); if (b < 1) b = 1;
    if (m && (out = (DWORD *)malloc((size_t)w * h * 4)) != NULL) {
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            float px = x / s, py = y / s, a = m[y * w + x] / 255.0f, r, g, k, edge;
            edge = (x < b || y < b || x >= w - b || y >= h - b) ? 1.0f : 0.0f;   /* the panel's red edge */
            k = 0.82f + 0.18f * SpNoise(px * 0.4f, py * 0.4f);   /* the red a little uneven */
            r = 228 * k; g = 24 * k;
            a *= 1 - 0.9f * SpCrack(px, py);   /* black cracks through the letters */
            if (SpHash((int)px, (int)py) > 0.985f) a *= 0.3f;   /* (and specks) */
            if (edge) a = 0.85f * (1 - 0.6f * SpCrack(px + 50, py));
            out[y * w + x] = 0xff000000u | ((DWORD)(r * a) << 16) | ((DWORD)(g * a) << 8) | (DWORD)(g * 0.8f * a); }
        g_spTex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (g_spTex) { SDL_UpdateTexture(g_spTex, NULL, out, w * 4); SDL_SetTextureBlendMode(g_spTex, SDL_BLENDMODE_BLEND); g_spTexW = w; }
        free(out); }
    free(m); SelectObject(dc, o); DeleteObject(f); DeleteDC(dc); }
static void SpMakeVer(int px)   /* "v0.1.n": white, a soft shadow (as the now-playing title) */
{   HDC dc = CreateCompatibleDC(NULL); HFONT f, o; SIZE sz; int w, h, sh = px / 10 + 1, x, y, k; BYTE *m = NULL; DWORD *out; static const char t[] = "v" RR_VERSION;
    if (g_spVer) { SDL_DestroyTexture(g_spVer); g_spVer = NULL; }
    if (!dc) return;
    f = CreateFontA(-px, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_BAHN_SEMI));
    o = (HFONT)SelectObject(dc, f); GetTextExtentPoint32A(dc, t, (int)strlen(t), &sz);
    w = sz.cx + 2 * sh + 4; h = sz.cy + 2 * sh + 4;
    free(SgMask(dc, t, 1, 1, 0, 0));
    m = SgMask(dc, t, w, h, 2, 2);
    if (m && (out = (DWORD *)malloc((size_t)w * h * 4)) != NULL) {
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
            int c = m[y * w + x], s = 0, a, lum;
            for (k = 1; k <= sh; k++) if (x - k >= 0 && y - k >= 0 && m[(y - k) * w + x - k] > s) s = m[(y - k) * w + x - k];
            a = c + (s * 170 / 255) * (255 - c) / 255; lum = a ? c * 225 / a : 0;
            out[y * w + x] = ((DWORD)a << 24) | ((DWORD)lum << 16) | ((DWORD)lum << 8) | (DWORD)lum; }
        g_spVer = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (g_spVer) { SDL_UpdateTexture(g_spVer, NULL, out, w * 4); SDL_SetTextureBlendMode(g_spVer, SDL_BLENDMODE_BLEND); g_spVerW = w; g_spVerH = h; }
        free(out); }
    free(m); SelectObject(dc, o); DeleteObject(f); DeleteDC(dc); }
/* after the 640x480 menu frame (px) and its strips were drawn */
static void SpDraw(const DWORD *px)
{   SDL_FRect pic, r; int ow = 0, oh = 0, x, y, vpx, n = 0, logo = 0; float sum = 0, fade; Uint8 c;
    if (g_sgScreen >= 0 && g_sgScreen != 7 && g_sgScreen != 0x23) { g_spMax = 0; return; }   /* (the logo is up at start-up, before the first menu screen) */
    if (g_inRace || g_w != 640 || g_h != 480 || !g_ren) return;
    for (y = 170; y < 230; y += 4) for (x = 140; x < 500; x += 4) { DWORD p = px[y * 640 + x]; int R = (p >> 16) & 255, G = (p >> 8) & 255, B = p & 255;
        sum += R + G + B; n++; if ((R > 2 * G && R > 2 * B && R > 30) || R + G + B < 60) logo++; }   /* the logo's brightness; red or black all over = the logo is up */
    if (logo < n * 3 / 4) return;   /* (the credits scroll over the same picture: no logo there) */
    if (sum > g_spMax) g_spMax = sum;
    fade = g_spMax > 0 ? sum / g_spMax : 0; if (fade < 0.02f) return;
    c = (Uint8)(fade * 255);
    if (!SDL_GetRenderLogicalPresentationRect(g_ren, &pic)) pic.w = 0;
    SDL_SetRenderLogicalPresentation(g_ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED); SDL_SetRenderScale(g_ren, 1.0f, 1.0f);
    SDL_GetRenderOutputSize(g_ren, &ow, &oh);
    if (pic.w <= 0) { pic.x = 0; pic.y = 0; pic.w = (float)ow; pic.h = (float)oh; }
    {   int w = (int)(SP_W * pic.w / 640.0f + 0.5f); if (w < 8) w = 8;
        if (!g_spTex || g_spTexW != w) SpMake(w);
        if (g_spTex) { r.x = pic.x + SP_X * pic.w / 640.0f; r.y = pic.y + SP_Y * pic.h / 480.0f; r.w = (float)w; r.h = SP_H * pic.h / 480.0f;
            SDL_SetTextureColorMod(g_spTex, c, c, c); SDL_RenderTexture(g_ren, g_spTex, NULL, &r); } }
    vpx = oh / 42; if (vpx < 11) vpx = 11;
    if (!g_spVer || g_spVerPx != vpx) { SpMakeVer(vpx); g_spVerPx = vpx; }
    if (g_spVer) { r.w = (float)g_spVerW; r.h = (float)g_spVerH; r.x = ow - r.w - oh / 80.0f; r.y = oh - r.h - oh / 100.0f;
        SDL_SetTextureAlphaMod(g_spVer, c); SDL_RenderTexture(g_ren, g_spVer, NULL, &r); }
    WsApplyRenderer(); }
