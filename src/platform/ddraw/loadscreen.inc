/* The loading screen of a race start (included by rrddraw.c after musicbox.inc; user, 2026-11-15: "a proper loading indicator for assets please while
 * the screen says Loading").  The game's own "GAME LOADING / Get ready to race!" page stayed frozen while the race was set up - nothing is presented
 * then.  Now every stage of the set-up reports how far it is (LsStep: permille 0..1000 and what it is doing) and the window shows, drawn here straight
 * with the renderer: the track's name, a progress bar, the percentage and the stage.  The stages (their share of the bar):
 *   game (system\race_session.c rr_RaceSetup, render\world3d_feed.c): track resources 0-8 %, race sprites 8-20 %, the road 20-25 %, the course file
 *     (made the first time a course is raced: the course extractor, a second copy of the game - crawls 25-45 % while it runs),
 *   shim (world3d.inc RR_World3DLoad): reading the course 45-50 %, the 3D course piece by piece 50-90 % (course3d.h C3_progress), lights, grass,
 *     shadows 90-97 %, then the game finishes the set-up (100 %).
 * The next frame the game presents (the race) replaces it.  Not in quiet / headless / deterministic test runs (their pictures and frame records stay
 * the same); test switch RR_LOADSCREEN=1: shown anyway, RR_LOADSCREEN=0: never; RR_LOADSHOT=1 + RR_DUMP_GPU: a picture of each stage. */
static int g_lsOn = -1;   /* -1 = not decided yet */
static DWORD g_lsLast; static char g_lsWhat[64]; static int g_lsPm, g_lsTrack, g_lsLevel;   /* (the track 1..5 and level 1..5: the title) */
static SDL_Texture *g_lsTex[4]; static int g_lsTexW[4], g_lsTexH[4], g_lsTexPx[4]; static char g_lsTexStr[4][64];   /* (0 title, 1 track, 2 stage, 3 percentage) */
static SDL_Texture *g_lsBg; static int g_lsBgW, g_lsBgH, g_lsBgTried;   /* the background: one of the game's cartoon pictures (user, 2026-11-23: "during loading screen it should just
                                                                         default to one of the wacky backgrounds the game has instead of that plain background") - a new pick each race start */
__declspec(dllexport) unsigned char * __stdcall RR_DecodeJpeg(const void *data, unsigned size, int *w, int *h);   /* jpeg.c */
__declspec(dllexport) void __stdcall RR_FreeImage(void *p);
static void LsBgLoad(void)
{   static const char *const pic[] = { "Images\\Street.RRI", "Images\\Panzer.RRI", "Images\\Olleys.RRI", "Images\\Credits.RRI" };   /* (the street, the Panzer Klub, Olley's, the gang) */
    FILE *f; long n; unsigned char *buf, *px; int w, h, i;
    if (g_lsBgTried) return; g_lsBgTried = 1;
    for (i = 0; i < 4 && !g_lsBg; i++) { const char *p = pic[(GetTickCount() / 7 + i) % 4];   /* (the next one when a picture is missing) */
        if (!(f = fopen(p, "rb"))) continue;
        fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); buf = (unsigned char *)malloc(n > 0 ? n : 1);
        if (!buf || fread(buf, 1, n, f) != (size_t)n) { fclose(f); free(buf); continue; } fclose(f);
        px = RR_DecodeJpeg(buf, (unsigned)n, &w, &h); free(buf); if (!px) continue;
        if ((g_lsBg = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STATIC, w, h)) != NULL) {   /* (the decoder's B, G, R bytes) */
            SDL_UpdateTexture(g_lsBg, NULL, px, w * 3); SDL_SetTextureScaleMode(g_lsBg, SDL_SCALEMODE_LINEAR); g_lsBgW = w; g_lsBgH = h; }
        RR_FreeImage(px); } }
static void LsBgFree(void) { if (g_lsBg) SDL_DestroyTexture(g_lsBg); g_lsBg = NULL; g_lsBgTried = 0; }   /* (the race is on: the next load picks again) */
static int LsWanted(void)
{   if (g_lsOn < 0) { const char *e = getenv("RR_LOADSCREEN"); g_lsOn = (e && *e) ? *e == '1' : !(g_headless || getenv("RR_DETERMINISTIC")); }
    return g_lsOn && g_ren && !g_vpPlaying; }
/* a line of text as a white texture with a soft dark outline (font 0 = Impact, 1 = Arial bold), cached per slot */
static SDL_Texture *LsText(int slot, const char *s, int px, int font)
{   HDC dc; HFONT f, o; SIZE sz; int w, h, R = px / 14 + 1, x, y, i, j; BYTE *m; DWORD *out;
    if (g_lsTex[slot] && g_lsTexPx[slot] == px && !strcmp(g_lsTexStr[slot], s)) return g_lsTex[slot];
    if (g_lsTex[slot]) { SDL_DestroyTexture(g_lsTex[slot]); g_lsTex[slot] = NULL; }
    if (!*s || !(dc = CreateCompatibleDC(NULL))) return NULL;
    f = CreateFontA(-px, 0, 0, 0, font ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(font ? RR_FONT_ARIAL : RR_FONT_IMPACT)); o = (HFONT)SelectObject(dc, f);
    GetTextExtentPoint32A(dc, s, (int)strlen(s), &sz); w = sz.cx + 2 * R + 4; h = sz.cy + 2 * R + 4;
    m = SgMask(dc, s, w, h, R + 2, R + 2);
    if (m && (out = (DWORD *)malloc((size_t)w * h * 4)) != NULL) {
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) { int t = m[y * w + x], ol = 0, a;
            for (j = -R; j <= R && ol < 255; j++) for (i = -R; i <= R; i++) { int xx = x + i, yy = y + j; if (i * i + j * j > R * R || xx < 0 || yy < 0 || xx >= w || yy >= h) continue; if (m[yy * w + xx] > ol) ol = m[yy * w + xx]; }
            a = t + ol * 160 / 255 * (255 - t) / 255;   /* white letters over a soft dark rim */
            { int l = a ? t * 255 / a : 0; out[y * w + x] = ((DWORD)a << 24) | ((DWORD)l << 16) | ((DWORD)l << 8) | (DWORD)l; } }
        if ((g_lsTex[slot] = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h)) != NULL) {
            SDL_UpdateTexture(g_lsTex[slot], NULL, out, w * 4); SDL_SetTextureBlendMode(g_lsTex[slot], SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(g_lsTex[slot], SDL_SCALEMODE_LINEAR);
            g_lsTexW[slot] = w; g_lsTexH[slot] = h; g_lsTexPx[slot] = px; lstrcpynA(g_lsTexStr[slot], s, sizeof g_lsTexStr[slot]); }
        free(out); }
    free(m); SelectObject(dc, o); DeleteObject(f); DeleteDC(dc);
    return g_lsTex[slot]; }
static void LsBlit(int slot, float cx, float y, float r, float g, float b)   /* centred on cx */
{   SDL_FRect d; if (!g_lsTex[slot]) return;
    d.w = (float)g_lsTexW[slot]; d.h = (float)g_lsTexH[slot]; d.x = cx - d.w / 2; d.y = y;
    SDL_SetTextureColorModFloat(g_lsTex[slot], r, g, b); SDL_RenderTexture(g_ren, g_lsTex[slot], NULL, &d); }
static void LsDraw(void)
{   static const char *const track[6] = { "", "The City", "Sierra Nevada", "The Peninsula", "Napa Valley", "Pacific Coast Highway" };
    SDL_FRect r; SDL_RendererLogicalPresentation lm = SDL_LOGICAL_PRESENTATION_DISABLED; int lw = 0, lh = 0, ow = 0, oh = 0, k, t = g_lsTrack; char tl[64]; float bw, bh, bx, by, f = g_lsPm / 1000.0f; char pc[16];
    SDL_SetRenderTarget(g_ren, NULL);
    SDL_GetRenderLogicalPresentation(g_ren, &lw, &lh, &lm);
    if (lm != SDL_LOGICAL_PRESENTATION_DISABLED) SDL_SetRenderLogicalPresentation(g_ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderScale(g_ren, 1.0f, 1.0f); SDL_SetRenderViewport(g_ren, NULL); SDL_SetRenderClipRect(g_ren, NULL);
    SDL_GetCurrentRenderOutputSize(g_ren, &ow, &oh);
    if (ow > 0 && oh > 0) {
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE);
        LsBgLoad(); SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255); SDL_RenderClear(g_ren);
        if (g_lsBg) {   /* the picture: the window's height, centred (black bars beside it on a wide window), a dark band behind the text and the bar */
            r.h = (float)oh; r.w = (float)oh * g_lsBgW / g_lsBgH; r.x = (ow - r.w) / 2; r.y = 0; SDL_RenderTexture(g_ren, g_lsBg, NULL, &r);
            SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
            for (k = 0; k < 24; k++) { float q = k / 23.0f, a = 0.72f * (1 - (2 * q - 1) * (2 * q - 1) * (2 * q - 1) * (2 * q - 1));   /* (soft top and bottom edges) */
                float y0 = (float)floor(oh * (0.24f + 0.50f * k / 24.0f)), y1 = (float)floor(oh * (0.24f + 0.50f * (k + 1) / 24.0f));   /* (whole rows, no overlap: overlapping slices showed as stripes) */
                r.x = 0; r.w = (float)ow; r.y = y0; r.h = y1 - y0; SDL_SetRenderDrawColorFloat(g_ren, 0, 0, 0, a); SDL_RenderFillRect(g_ren, &r); }
            SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE); }
        else for (k = 0; k < 32; k++) {   /* (no picture: night blue at the top to near black, in bands) */
            float q = k / 31.0f; r.x = 0; r.y = oh * k / 32.0f; r.w = (float)ow; r.h = oh / 32.0f + 1;
            SDL_SetRenderDrawColorFloat(g_ren, 0.10f - 0.08f * q, 0.09f - 0.07f * q, 0.20f - 0.16f * q, 1); SDL_RenderFillRect(g_ren, &r); }
        LsText(0, "LOADING", oh / 9, 0); _snprintf(tl, sizeof tl, t >= 1 && t <= 5 ? (g_lsLevel >= 1 && g_lsLevel <= 5 ? "%s  -  Level %d" : "%s") : "", track[t >= 1 && t <= 5 ? t : 0], g_lsLevel); tl[sizeof tl - 1] = 0; LsText(1, tl, oh / 22, 0);
        LsBlit(0, ow / 2.0f, oh * 0.30f, 1.0f, 0.38f, 0.16f);   /* (the menus' orange title) */
        LsBlit(1, ow / 2.0f, oh * 0.30f + (g_lsTex[0] ? g_lsTexH[0] : 0), 0.92f, 0.92f, 0.96f);
        bw = ow * 0.56f; bh = oh / 34.0f; if (bh < 8) bh = 8; bx = (ow - bw) / 2; by = oh * 0.62f;
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        r.x = bx - 3; r.y = by - 3; r.w = bw + 6; r.h = bh + 6; SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 170); SDL_RenderFillRect(g_ren, &r);   /* the trough */
        r.x = bx; r.y = by; r.w = bw; r.h = bh; SDL_SetRenderDrawColor(g_ren, 40, 36, 70, 255); SDL_RenderFillRect(g_ren, &r);
        if (f > 0) { r.w = bw * (f > 1 ? 1 : f); SDL_SetRenderDrawColor(g_ren, 255, 120, 40, 255); SDL_RenderFillRect(g_ren, &r);   /* the fill, a lighter band on top */
            r.h = bh * 0.35f; SDL_SetRenderDrawColor(g_ren, 255, 200, 120, 120); SDL_RenderFillRect(g_ren, &r); }
        r.x = bx - 1; r.y = by - 1; r.w = bw + 2; r.h = bh + 2; SDL_SetRenderDrawColor(g_ren, 255, 235, 80, 200); SDL_RenderRect(g_ren, &r);
        _snprintf(pc, sizeof pc, "%d%%", g_lsPm / 10); pc[sizeof pc - 1] = 0;
        LsText(2, g_lsWhat, oh / 34, 1); LsBlit(2, ow / 2.0f, by + bh + oh / 60.0f, 0.82f, 0.82f, 0.90f);
        if (LsText(3, pc, oh / 30, 1)) {   /* the percentage, right of the bar */
            r.w = (float)g_lsTexW[3]; r.h = (float)g_lsTexH[3]; r.x = bx + bw + oh / 50.0f; r.y = by + bh / 2 - r.h / 2;
            SDL_SetTextureColorModFloat(g_lsTex[3], 1, 0.92f, 0.32f); SDL_RenderTexture(g_ren, g_lsTex[3], NULL, &r); }
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE); }
    {   static char shot[64];   /* test switch RR_LOADSHOT=1 (with RR_DUMP_GPU): a picture of each stage, gpu<7000 + percent>.bmp */
        if (g_dumpGpu[0] && getenv("RR_LOADSHOT") && strcmp(shot, g_lsWhat)) { lstrcpynA(shot, g_lsWhat, sizeof shot); DumpGpu(7000 + g_lsPm / 10); } }
    SDL_RenderPresent(g_ren);
    if (lm != SDL_LOGICAL_PRESENTATION_DISABLED) SDL_SetRenderLogicalPresentation(g_ren, lw, lh, lm);
    {   MSG m; PeekMessageA(&m, NULL, 0, 0, PM_NOREMOVE); }   /* (a look at the queue, nothing taken: Windows does not turn a long load into a "not responding" ghost window) */ }
/* a stage of the race set-up: how far (permille) and what (NULL = the same as before); drawn at most every 30 ms, and at once for a new stage */
static void LsStep(int permille, const char *what)
{   DWORD now = GetTickCount(); int newWhat = what && strcmp(what, g_lsWhat);
    if (ViewerOn()) { g_vw->progress = permille < 0 ? 0 : permille > 1000 ? 1000 : permille; if (what) lstrcpynA(g_vw->stage, what, sizeof g_vw->stage); }   /* (the Course Editor's game renderer: its progress bar - viewer.inc) */
    if (!LsWanted()) return;
    if (permille < 0) permille = 0; if (permille > 1000) permille = 1000;
    if (newWhat) lstrcpynA(g_lsWhat, what, sizeof g_lsWhat);
    g_lsPm = permille;
    if (!newWhat && now - g_lsLast < 30 && permille < 1000) return;
    g_lsLast = now; LsDraw();
    if (permille >= 1000) LsBgFree(); }
/* the game's stages (system\race_session.c, render\world3d_feed.c) */
__declspec(dllexport) void __stdcall RR_LoadProgress(int permille, const char *what, int track, int level) { g_lsTrack = track; g_lsLevel = level; LsStep(permille, what); }
