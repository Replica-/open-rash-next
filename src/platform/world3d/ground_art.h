/* ground_art.h - extra ground pictures for painted cells (user, 2026-11-18: "another ground texture please, called REDWOOD ... massive redwood
 * trees spawning up ... really higher and spaced out on the tile ... the texture abit dirt / bark mix ... 2d redwood sprites"; made with Fooocus:
 * analysis\redwood\redwood.py).  <mod>\hd\ground\<NAME>.png (512 x 512, tiling) and, if there, <NAME>_TREE.png (256 x 1024 RGBA, the foot at the
 * bottom middle, alpha < 0.5 cut out): the trees course3d.h C3_GroundTrees stands on every cell painted with it; <NAME>_HEDGE.png (512 x 256 RGBA,
 * tiling across, its foot the bottom row: analysis\redwood\hedge.py): a tall hedge along the inner edge of a "beyond" cell painted with it (C3_GroundHedge).  Painted as RRC_BAND | RRC_GEN | n
 * (n = its place in k_c3Ground; course3d.h C3_PaintTex).  Used by the game (world3d.inc) and the Course Editor; needs SDL3 (SDL_LoadPNG). */
#ifndef C3_GROUND_ART_H
#define C3_GROUND_ART_H
#define C3_NGROUND 15
static const char *const k_c3Ground[C3_NGROUND] = { "REDWOOD", "WATER", "HOLE", "BRIDGE", "DUNES", "CLIFF", "SANDW", "SANDG", "SANDR", "SANDPATH", "SLABS", "TILES", "SHRUB", "FOREST", "DHOLE" };   /* (only ever added to: a painted cell keeps its number - RRC_GEN | place) */
static const unsigned k_c3GroundCol[C3_NGROUND] = { 0, 0, 0, 0, 0xd6c4a0u, 0x6e645au, 0xe1d7c3u, 0xcdaa69u, 0xb96944u, 0xd4b68au, 0, 0, 0x3c5a2eu, 0x2a4428u, 0 };   /* (no picture: a mottled ground of this colour, RGB) */   /* (HOLE: course3d.h C3_GHOLE - sunk in the game, C3_HoleRow) */   /* (WATER: course3d.h C3_GWATER - the cells painted with it are water, C3_WaterRow) */
static int C3_GroundIsPath(int n) { return n >= 0 && n < C3_NGROUND && (!strcmp(k_c3Ground[n], "SANDPATH") || !strcmp(k_c3Ground[n], "SLABS") || !strcmp(k_c3Ground[n], "TILES")); }   /* (a footpath's picture: the Course Editor's Footpaths list - SANDPATH the sand footpath, DUNES its ground: user, 2026-11-19 "two version of sand") */
static const char *C3_GroundLabel(int n) { return n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "HOLE") ? "CLIFF HOLE" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "DHOLE") ? "DIRT HOLE" : n >= 0 && n < C3_NGROUND ? k_c3Ground[n] : "?"; }   /* (the Course Editor's name for it - user,
                                                                 2026-11-19 "rename hole as CLIFF HOLE": k_c3Ground keeps HOLE, its picture's file name) */
#define C3_NROAD 4
static const char *const k_c3Road[C3_NROAD] = { "COBBLE", "RAINBOW", "TRAM", "SPDBUMP" };   /* (TRAM: a lane with tram rails - a tram runs on it, world3d.inc W3Trams; SPDBUMP: a speed bump across it - a hump in the game, course3d.h C3_Lanes: user, 2026-11-20) */   /* (RAINBOW: user, 2026-11-19 - an animated road of coloured squares in waves: render3d.h vertex alpha 235) */   /* extra road surfaces (user, 2026-11-18: "for road can you add cobblestone texture"): <hd>\road\<NAME>.png
                                                                 512 x 512, tiling (analysis\redwood\cobble.py) - COBBLE: the shader's cobbles (vertex alpha 240, render3d.h),
                                                                 its rattle (road\course_edit.c rr_CourseEditCobble) */
static unsigned *g_c3RoadPx[C3_NROAD];
static unsigned C3G_Px(int r, int g, int b) { return 0xff000000u | (unsigned)(b < 0 ? 0 : b > 255 ? 255 : b) << 16 | (unsigned)(g < 0 ? 0 : g > 255 ? 255 : g) << 8 | (unsigned)(r < 0 ? 0 : r > 255 ? 255 : r); }
static unsigned *C3G_Road(int S, int kind)   /* (no TRAM.png / SPDBUMP.png: asphalt with two steel rails at 30 / 70 % across, along v / a yellow and black striped band across the middle) */
{   unsigned *p = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!p) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { unsigned g = (unsigned)(x * 73 + y * 151) * 2654435761u; int v = 74 + (int)((g >> 26) % 12), c = v, r = v, b = v + 2; double u = (double)x / S, w = (double)y / S;
        if (kind == 0) { double d0 = fabs(u - 0.30), d1 = fabs(u - 0.70), d = d0 < d1 ? d0 : d1;
            if (d < 0.022) r = c = b = 182 + (int)((g >> 27) % 16); else if (d < 0.034) r = c = b = 34; else if (d < 0.08) { r = c = 106; b = 102; } }
        else if (w > 0.42 && w < 0.58) { int st = ((int)((u + w) * 12)) & 1; if (st) { r = 226; c = 186; b = 30; } else { r = c = b = 28; } }
        p[y * S + x] = C3G_Px(r, c, b); }
    return p; }
static unsigned *C3G_Cobble(int S)   /* (no COBBLE.png: rows of grey setts, dark joints) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) {
        int row = y * 8 / S, xs = (x + row * S / 13) % S, col = xs * 10 / S; double fy = (y * 8.0 / S) - row, fx = (xs * 10.0 / S) - col,
            e = fy < fx ? (fy < 1 - fy ? fy : 1 - fy) : (fx < 1 - fx ? fx : 1 - fx), d = e < 0.12 ? e / 0.12 : 1;
        unsigned h = (unsigned)(row * 31 + col) * 2654435761u; int g = (int)((70 + (h >> 24) % 50) * (0.35 + 0.65 * d));
        px[y * S + x] = 0xff000000u | (unsigned)(g - 4) << 16 | (unsigned)g << 8 | (unsigned)(g + 3); }
    return px; }
static unsigned *C3G_Hole(int S)   /* (no HOLE.png: yellow and black hazard stripes - the editor's P3D view; the game draws the hole's floor black: course3d.h C3_Strip) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) px[y * S + x] = (((x + y) / (S / 4)) & 1) ? 0xff10d8f0u : 0xff141414u;
    return px; }
static unsigned *C3G_Bridge(int S)   /* (no BRIDGE.png: a bridge deck - weathered concrete, a joint across every quarter, a darker edge) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { unsigned h = ((unsigned)x * 374761393u + (unsigned)y * 668265263u); int g; h = (h ^ (h >> 13)) * 1274126177u;
        g = 150 + (int)((h >> 24) % 24) - 12 - ((y % (S / 4)) < 2 ? 50 : 0) - (x < 3 || x > S - 4 ? 30 : 0);
        px[y * S + x] = 0xff000000u | (unsigned)(g + 2) << 16 | (unsigned)g << 8 | (unsigned)(g - 2); }
    return px; }
static unsigned *C3G_Flat(int S, unsigned rgb)   /* (a ground of colour rgb with a soft mottle and grain) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { unsigned h = ((unsigned)x * 374761393u + (unsigned)y * 668265263u); double m; int r, g, b; h = (h ^ (h >> 13)) * 1274126177u;
        m = 0.9 + 0.08 * sin(x * 6.2831853 / S * 2 + 1.1) * sin(y * 6.2831853 / S * 3) + ((int)((h >> 24) % 20) - 10) / 255.0;
        r = (int)(((rgb >> 16) & 255) * m); g = (int)(((rgb >> 8) & 255) * m); b = (int)((rgb & 255) * m);
        px[y * S + x] = 0xff000000u | (unsigned)(b < 255 ? b : 255) << 16 | (unsigned)(g < 255 ? g : 255) << 8 | (unsigned)(r < 255 ? r : 255); }
    return px; }
static unsigned *C3G_Rainbow(int S)   /* (RAINBOW's picture - the editor's thumbnail, P3D: squares in rainbow bands, dark gaps; the game animates it) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { int cx = x * 4 / S, cy = y * 4 / S, fx = x % (S / 4), fy = y % (S / 4), e = fx < 2 || fy < 2 || fx > S / 4 - 3 || fy > S / 4 - 3;
        double hh = fmod(cy * 0.25 + cx * 0.06, 1.0) * 6; int sx = (int)hh; double f = hh - sx, r, g, b;
        switch (sx) { case 0: r = 1; g = f; b = 0; break; case 1: r = 1 - f; g = 1; b = 0; break; case 2: r = 0; g = 1; b = f; break; case 3: r = 0; g = 1 - f; b = 1; break; case 4: r = f; g = 0; b = 1; break; default: r = 1; g = 0; b = 1 - f; }
        if (e) r = g = b = 0.08;
        px[y * S + x] = 0xff000000u | (unsigned)(b * 255) << 16 | (unsigned)(g * 255) << 8 | (unsigned)(r * 255); }
    return px; }
static unsigned *C3G_Water(int S)   /* (no WATER.png: a picture of water for its block - teal ripples, darker in the troughs) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) {
        double a = 6.2831853 / S, w = 0.5 + 0.25 * sin(a * (2 * x + 3 * y)) + 0.15 * sin(a * (5 * x - 2 * y) + 1.3) + 0.1 * sin(a * (-3 * x + 7 * y) + 0.4);
        int r = (int)(14 + 40 * w), g = (int)(70 + 70 * w), b = (int)(92 + 70 * w);
        px[y * S + x] = 0xff000000u | (unsigned)b << 16 | (unsigned)g << 8 | (unsigned)r; }
    return px; }
/* (SLABS / TILES: the City's two footpaths, which the game draws itself - render3d.h, vertex alpha 246 concrete slabs / 242 glossy tiles; user, 2026-11-20 "theres
   actually two types of side walk that isnt here".  These pictures are what the Course Editor shows: light concrete slabs with joints / small tiles in three stones) */
static unsigned *C3G_Paving(int S, int tiles)
{   unsigned *p = (unsigned *)malloc((size_t)S * S * 4); int x, y, n = tiles ? 8 : 4;
    if (!p) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { int cx = x * n / S, cy = y * n / S, jx = (x * n) % S, jy = (y * n) % S, j = jx < S / 32 + 1 || jy < S / 32 + 1;
        unsigned h = (unsigned)(cx * 73 + cy * 151) * 2654435761u, g = (unsigned)(x * 31 + y * 17) * 2246822519u; int r, gg, b, sh = (int)((h >> 24) % 14) - 7, gr = (int)((g >> 27) % 7) - 3;
        if (tiles) { int st = (int)((h >> 20) % 3); r = st == 0 ? 104 : st == 1 ? 156 : 200; gg = st == 0 ? 104 : st == 1 ? 96 : 190; b = st == 0 ? 112 : st == 1 ? 70 : 170; }
        else { r = 162; gg = 160; b = 152; }
        r += sh + gr; gg += sh + gr; b += sh + gr; if (j) { r = r * 6 / 10; gg = gg * 6 / 10; b = b * 6 / 10; }
        p[y * S + x] = 0xff000000u | (unsigned)(b < 0 ? 0 : b > 255 ? 255 : b) << 16 | (unsigned)(gg < 0 ? 0 : gg > 255 ? 255 : gg) << 8 | (unsigned)(r < 0 ? 0 : r > 255 ? 255 : r); }
    return p; }
static unsigned *g_c3GroundPx[C3_NGROUND * 3];
static unsigned *C3G_LoadPng(const char *path, int w, int h)   /* ABGR texels of a w x h PNG, NULL = none / another size */
{   SDL_Surface *s = SDL_LoadPNG(path), *c; unsigned *px = NULL; int x, y;
    if (!s) return NULL;
    c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888); SDL_DestroySurface(s);
    if (!c) return NULL;
    if (c->w == w && c->h == h && (px = (unsigned *)malloc((size_t)w * h * 4)) != NULL)
        for (y = 0; y < h; y++) { const unsigned *r = (const unsigned *)((const unsigned char *)c->pixels + (size_t)y * c->pitch);
            for (x = 0; x < w; x++) { unsigned q = r[x]; px[y * w + x] = (q & 0xff00ff00u) | ((q >> 16) & 0xffu) | ((q & 0xffu) << 16); } }
    SDL_DestroySurface(c); return px; }
/* the pictures from <hdDir>\ground into ra's textures (after RX_Load and the generated sets; again on every course load) - 0 = none there.  An
   absent picture still takes its place (a flat colour: course3d.h draws the cell as before) so the codes stay the same */
static int C3_GroundArt(RxAssets *ra, const char *hdDir)
{   char p[520]; int n, kind, found = 0;
    ra->ground0 = ra->ntex; ra->nground = 0; memset(ra->groundTree, 0, sizeof ra->groundTree); memset(ra->groundHedge, 0, sizeof ra->groundHedge); ra->road0 = ra->ntex; ra->nroad = 0;
    for (n = 0; n < C3_NGROUND * 3; n++) { free(g_c3GroundPx[n]); g_c3GroundPx[n] = NULL; }
    if (!hdDir) hdDir = "";   /* (no mod folder: the built-in pictures - WATER, HOLE, COBBLE - still; user's courses must look the same without the mod) */
    for (kind = 0; kind < 3; kind++) for (n = 0; n < C3_NGROUND && ra->ntex < 8192; n++) { RxTex *x; unsigned *px; int W = kind == 1 ? 256 : 512, H = kind == 1 ? 1024 : kind ? 256 : 512;
        _snprintf(p, sizeof p - 1, "%s\\ground\\%s%s.png", hdDir, k_c3Ground[n], kind == 1 ? "_TREE" : kind ? "_HEDGE" : ""); p[sizeof p - 1] = 0;
        px = hdDir[0] ? C3G_LoadPng(p, W, H) : NULL;
        if (!px && kind == 2 && hdDir[0] && (px = C3G_LoadPng(p, 512, 512)) != NULL) H = 512;   /* (a square hedge: FOREST's tall wall of trees) */
        if (kind && !px) continue;   /* (no tree / hedge) */
        if (!kind && !px && !strcmp(k_c3Ground[n], "WATER") && (px = C3G_Water(64)) != NULL) W = H = 64;
        if (!kind && !px && (!strcmp(k_c3Ground[n], "HOLE") || !strcmp(k_c3Ground[n], "DHOLE")) && (px = C3G_Hole(64)) != NULL) W = H = 64;
        if (!kind && !px && !strcmp(k_c3Ground[n], "BRIDGE") && (px = C3G_Bridge(128)) != NULL) W = H = 128;
        if (!kind && !px && (!strcmp(k_c3Ground[n], "SLABS") || !strcmp(k_c3Ground[n], "TILES")) && (px = C3G_Paving(128, n && !strcmp(k_c3Ground[n], "TILES"))) != NULL) W = H = 128;
        if (!kind && !px && k_c3GroundCol[n] && (px = C3G_Flat(64, k_c3GroundCol[n])) != NULL) W = H = 64;
        if (!kind && !px && (px = (unsigned *)malloc(4)) != NULL) { *px = 0xff2a3a5au; W = H = 1; }   /* (no picture: a dark bark brown) */
        if (!px) continue;
        g_c3GroundPx[kind * C3_NGROUND + n] = px;
        x = &ra->tex[ra->ntex]; memset(x, 0, sizeof *x);
        _snprintf(x->name, sizeof x->name, "%.6s%s", k_c3Ground[n], kind == 1 ? "TR" : kind ? "HG" : ""); x->name[8] = 0; x->w = W; x->h = H; x->gen = px; x->gpu = -1;
        if (kind) { x->key = 1; if (n < 4) { if (kind == 1) ra->groundTree[n] = ra->ntex + 1; else ra->groundHedge[n] = ra->ntex + 1; } } else { ra->nground++; found += W > 1; }        ra->ntex++; }
    ra->road0 = ra->ntex; ra->nroad = 0;
    {   int ns; ra->shrubTex = 0;   /* SHRUB (user, 2026-11-20: "make it a terrain please shubbery and in P3D it stays flat but in T3D it turns into that shubbery and it has 4 sides"):
                                       its blocks' sides are SHRUB_HEDGE.png if there, else REDWOOD's hedge */
        for (ns = 0; ns < C3_NGROUND && strcmp(k_c3Ground[ns], "SHRUB"); ns++) ;
        for (n = 0; n < ra->ntex && ns < C3_NGROUND; n++) if (ra->tex[n].gen && ra->tex[n].gen == g_c3GroundPx[2 * C3_NGROUND + ns]) ra->shrubTex = n + 1;
        if (!ra->shrubTex) ra->shrubTex = ra->groundHedge[0];
        ra->forestTex = 0; for (ns = 0; ns < C3_NGROUND && strcmp(k_c3Ground[ns], "FOREST"); ns++) ;   /* (FOREST: FOREST_HEDGE.png only - user, 2026-11-20 "the wall of tree effects ... quite tall") */
        for (n = 0; n < ra->ntex && ns < C3_NGROUND; n++) if (ra->tex[n].gen && ra->tex[n].gen == g_c3GroundPx[2 * C3_NGROUND + ns]) ra->forestTex = n + 1;
        ra->dholeTex = 0; for (ns = 0; ns < C3_NGROUND && strcmp(k_c3Ground[ns], "DHOLE"); ns++) ;   /* (DIRT HOLE: its walls - DHOLE_HEDGE.png, a dirt cliff face; none: CLIFF's rock) */
        for (n = 0; n < ra->ntex && ns < C3_NGROUND; n++) if (ra->tex[n].gen && ra->tex[n].gen == g_c3GroundPx[2 * C3_NGROUND + ns]) ra->dholeTex = n + 1; }
    {   int nh, nc; for (nh = 0; nh < C3_NGROUND && strcmp(k_c3Ground[nh], "HOLE"); nh++) ; for (nc = 0; nc < C3_NGROUND && strcmp(k_c3Ground[nc], "CLIFF"); nc++) ;
        if (nh < ra->nground && nc < ra->nground && ra->tex[ra->ground0 + nh].w == 64 && g_c3GroundPx[nh] && g_c3GroundPx[nc]) {   /* (the built-in HOLE: its stripes on CLIFF's rock - user, 2026-11-19
                                                                 "CLIFF HOLE with STRIPS and that cliff texture": the yellow stripes stay, the black ones are the cliff, darker; the game draws a hole's floor black) */
            const RxTex *c = &ra->tex[ra->ground0 + nc]; unsigned *px = g_c3GroundPx[nh]; int x, y, S = 64;
            for (y = 0; y < S; y++) for (x = 0; x < S; x++) if (!(((x + y) / (S / 4)) & 1)) { unsigned q = g_c3GroundPx[nc][(y * c->h / S) * c->w + x * c->w / S]; int j; unsigned o = 0xff000000u;
                for (j = 0; j < 3; j++) o |= (unsigned)(((q >> (8 * j)) & 255) * 7 / 10) << (8 * j);
                px[y * S + x] = o; } }
        for (nh = 0; nh < C3_NGROUND && strcmp(k_c3Ground[nh], "DHOLE"); nh++) ;   /* (DIRT HOLE, user 2026-11-21 "another hole texture but with dirt cliff": the stripes on its dirt
                                                                                       wall's picture - else a plain earth brown - darker, as CLIFF HOLE's on rock) */
        if (nh < ra->nground && ra->tex[ra->ground0 + nh].w == 64 && g_c3GroundPx[nh]) {
            const unsigned *dp = g_c3GroundPx[2 * C3_NGROUND + nh]; unsigned *px = g_c3GroundPx[nh]; int x, y, S = 64, dw = 512, dh = 512;
            for (n = 0; n < ra->ntex; n++) if (dp && ra->tex[n].gen == dp) { dw = ra->tex[n].w; dh = ra->tex[n].h; }
            for (y = 0; y < S; y++) for (x = 0; x < S; x++) if (!(((x + y) / (S / 4)) & 1)) { unsigned q = dp ? dp[(y * dh / S) * dw + x * dw / S] : 0xff304a6eu; int j; unsigned o = 0xff000000u;
                for (j = 0; j < 3; j++) o |= (unsigned)(((q >> (8 * j)) & 255) * 7 / 10) << (8 * j);
                px[y * S + x] = o; } } }
    for (n = 0; n < C3_NROAD && ra->ntex < 8192; n++) { RxTex *x; unsigned *px; int W = 512, H = 512;   /* (the road surfaces: in order - their codes are their places) */
        free(g_c3RoadPx[n]); g_c3RoadPx[n] = NULL;
        _snprintf(p, sizeof p - 1, "%s\\road\\%s.png", hdDir, k_c3Road[n]); p[sizeof p - 1] = 0;
        if (!(px = hdDir[0] ? C3G_LoadPng(p, W, H) : NULL) && (px = !strcmp(k_c3Road[n], "RAINBOW") ? C3G_Rainbow(256) : !strcmp(k_c3Road[n], "TRAM") ? C3G_Road(256, 0) : !strcmp(k_c3Road[n], "SPDBUMP") ? C3G_Road(256, 1) : C3G_Cobble(256)) != NULL) W = H = 256;
        if (!px) break;
        g_c3RoadPx[n] = px; x = &ra->tex[ra->ntex]; memset(x, 0, sizeof *x);
        _snprintf(x->name, sizeof x->name, "%.8s", k_c3Road[n]); x->name[8] = 0; x->w = W; x->h = H; x->gen = px; x->gpu = -1;
        ra->nroad++; ra->ntex++; }
    return found; }
/* the City set (CITY_SCENERY.md): <cityDir>\FAC<nn>.png (512 x 1024 facades) and SGN<nn>.png (256 x 768 projecting signs) into ra's textures as its generated
   facades (gen0 / ngen) and signs (sign0 / nsign) - the game's urban tracks (world3d.inc W3GenFacades) and the Course Editor (user, 2026-11-20: "for the building
   textures can you show the HD texture instead of the low res one ... i dont need both versions").  The pictures are read once per folder and shared.  Returns ngen */
static int C3_CityArt(RxAssets *ra, const char *cityDir)
{   static unsigned *px[96]; static int pk[96], n = -1; static char at[520]; char p[520]; int k, i;
    ra->gen0 = ra->sign0 = ra->ntex; ra->ngen = ra->nsign = 0;
    if (!cityDir || !cityDir[0]) return 0;
    if (n < 0 || strcmp(at, cityDir)) {
        for (i = 0; i < n; i++) free(px[i]); n = 0; strncpy(at, cityDir, sizeof at - 1);
        for (k = 0; k < 96 && n < 96; k++) { int kind = k >= 64, j = kind ? k - 64 : k; unsigned *q;   /* (the facades, then the signs) */
            if (kind && j >= 32) break;
            _snprintf(p, sizeof p - 1, "%s\\%s%02d.png", cityDir, kind ? "SGN" : "FAC", j); p[sizeof p - 1] = 0;
            if (!(q = C3G_LoadPng(p, kind ? 256 : 512, kind ? 768 : 1024))) continue;
            px[n] = q; pk[n++] = k; } }
    for (i = 0; i < n && ra->ntex < 8192; i++) { int kind = pk[i] >= 64; RxTex *x;
        if (kind && !ra->nsign) ra->sign0 = ra->ntex;
        x = &ra->tex[ra->ntex++]; memset(x, 0, sizeof *x);
        _snprintf(x->name, sizeof x->name, "%s%02d", kind ? "CITYSG" : "CITYFC", kind ? pk[i] - 64 : pk[i]); x->name[8] = 0; x->w = kind ? 256 : 512; x->h = kind ? 768 : 1024; x->gen = px[i]; x->gpu = -1;
        if (kind) { x->key = 1; ra->nsign++; }
        else { x->facade = 1; x->shopV = (px[i][1] >> 24) == 0xFE ? (px[i][1] & 0xff) / 255.0f : 0.8f; ra->ngen++; } }
    if (!ra->nsign) ra->sign0 = ra->ntex;
    return ra->ngen; }
#endif
