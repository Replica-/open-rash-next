/* world3d.inc - the true-3D world of GPU races (analysis\TRUE3D_PROJECT.md M4; game side src\render\world3d_feed.c, interface src\platform\gpu_api.h).
 * The course (RR_EXTRACT file) becomes a 3D mesh (src\platform\world3d\course3d.h, textures rrx_assets.h) drawn by the Direct3D 11 back end
 * (render3d.h) on the SDL renderer's own device, every frame into a transparent target texture that GpuCompose lays between the background (sky)
 * and the polygons the game still hands over (riders, traffic, debris) + the overlay (HUD).
 * Camera = the game's: the original's projection is a true perspective near the camera (1/scale = 2.07 * (depth - 1.02 segments), the scale table
 * tbl_perspectiveScale), so with the focal length F = 320 frame pixels a segment is W3_L = 2.07 * F lateral units long and the eye stands 1.02 segments past
 * the view's start, camX beside and camY above the road (heights relative to the road at the view's start, like the game's projector).
 * Test switch / setting: RR_WORLD3D=0 turns it off (default on in GPU races on Direct3D 11). */
#include "../world3d/course3d.h"
#include "../world3d/ground_art.h"
#include "../world3d/render3d.h"

#define W3_F   320.0                    /* focal length in projection units (the projector's 320-wide space; x pxPerUnit = frame pixels) */
#define W3_L   (2.07 * W3_F)            /* segment length in lateral units */
#define W3_EYE 1.02                     /* the view depth (segments) at distance 0 = the eye: piece position view start + W3_EYE - 1 (W3Draw) */
#define W3_ZN  20.0                     /* near plane (lateral units) */
/* how far the 3D world is drawn: True 3D is always Far (W3_FAR = 150 segments) - without fog the far course would
   otherwise end as a thin sliver at the horizon, across the backdrop's mountains.  Test switch RR_W3DIST=<segments> */
static double W3Far(void) { const char *e = getenv("RR_W3DIST"); int n = e ? atoi(e) : g_vid.dist == 0 ? 70 : g_vid.dist == 1 ? 105 : W3_FAR; if (n < DD_NORMAL) n = W3_FAR; return n * W3_L; }   /* (Video Options > True 3D detail > Draw distance: near 70 / medium 105 / far 150) */

static C3Course g_w3c; static RxAssets g_w3a; static R3 g_w3r;
static int g_w3hdTrack;   /* the course file's track (t<n>_...): W3Tex looks for the hd\t<n> pictures there - the game's own track number is not set yet (or is the last race's) when the textures are made */
static R3Batch g_w3b[4096]; static int g_w3nb; static C3Batch g_w3cb[4096];
/* the stretch of the game's road ring (RrWorld3DCam.ring): a copy of the course with the ring's live decoration (wall corners, terrain heights - the
   file's differ: they come from time-animated records), its mesh rebuilt whenever that stretch or its decoration changes and drawn instead of the
   static mesh's pieces g_w3lo .. g_w3hi - 1 */
static C3Course g_w3l; static R3Batch g_w3lb[4096]; static int g_w3lnb, g_w3lo = -1, g_w3hi = -1;
static R3Batch g_w3sb[8192]; static int g_w3snb;   /* the static batches without that stretch */
static char g_w3meshTag[24];   /* on the segment tracker line (W3MeshTag) */
static int g_w3compiled;   /* this race: 1 = the compiled mesh (cut each frame: W3StaticCut), 0 = built on the fly (the static mesh + the live stretch, W3Live) */
/* g_w3fork (declared in gpuscene.inc): 1 = forks off, every split is the left road A only (True 3D), 0 = both roads (test switch RR_NOFORK=0) */
static int g_w3off, g_w3offB, g_w3camB;   /* g_w3camB: the camera follows B (W3Track; the game keeps its half flag on B long after B has rejoined A); the game's road position + g_w3off pieces = the file's piece on road A (g_w3offB: on road B) (W3Track) */
static int g_w3On, g_w3Init; static int g_w3Piece0;
/* the game asks which carriageway every rider must take at a split (race/physics.c): 1 = A, 0 = their own choice (the original) */
static int g_crActive;   /* a custom race is being run (customrace.inc): every rider takes A, the race is Pseudo 3D (its bends come from the editor) */
__declspec(dllexport) int __stdcall RR_ForkPick(void) { return g_crActive ? 1 : (g_w3On ? g_w3fork : 0); }
static int g_w3forkWant;   /* (the course has the Course Editor's forks: True 3D with forks on - road\course_edit.c CeForksTell, before RR_World3DLoad) */
__declspec(dllexport) void __stdcall RR_SetForks(int on) { g_w3forkWant = on; }
/* the game asks at the race start (road\road_build.c, before it builds the road): True 3D with forks off -> it builds ONE road, the course's splits never
   exist (its course file too: world3d\t<track>_l<level>_1r.rrx).  Test switch RR_NOFORK=0: the course as it is (the forks-on experiment) */
__declspec(dllexport) int __stdcall RR_World3DWanted(void);
__declspec(dllexport) int __stdcall RR_OneRoad(void)
{   const char *e = getenv("RR_NOFORK");
    if (e && *e == '0') return 0;
    return RR_World3DWanted(); }
static int g_w3texGen;   /* +1 whenever the renderer's textures are dropped (race start): textures made later (car models) know they must be made again */
static RrWorld3DCam g_w3cam; static int g_w3HaveCam;
static SDL_Texture *g_w3tex; static int g_w3tw, g_w3th;

__declspec(dllexport) int __stdcall RR_World3DWanted(void)
{   const char *nm;
    g_w3fork = 0;   /* (until RR_World3DLoad: a Pseudo 3D race after a True 3D one shows its forks on the course map) */
    if (g_crActive) return 0;   /* (a custom race: the 3D world is built from the course files, not the editor's road) */
    if (!W3Setting()) return 0;   /* Video Options > Race 3D = True 3D, or the test switch RR_WORLD3D (winscale.inc) */
    if (!g_gpuOn || !g_ren) return 0;
    nm = SDL_GetRendererName(g_ren); return nm && !strcmp(nm, "direct3d11"); }

/* names of every texture of the track (the course file's sprite sets + globals): a polygon the game queued in a world pass (RrGpuQuad.world) with one of
   these textures (or flat) is part of the world the 3D renderer draws; the riders / traffic the same passes sort in keep their own textures */
static unsigned long long *g_w3names; static int g_w3nn;
static int W3NameCmp(const void *a, const void *b) { unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b; return x < y ? -1 : x > y; }
static void W3Names(void)
{   int i, cap = 4096; unsigned o;
    free(g_w3names); g_w3nn = 0; g_w3names = (unsigned long long *)malloc(cap * 8);
    if (!g_w3names) return;
    for (i = 0; i < g_w3a.ntex && g_w3nn < cap; i++) memcpy(&g_w3names[g_w3nn++], g_w3a.tex[i].name, 8);   /* (globals: added at load) */
    for (i = 0; i < g_w3a.nset; i++) {
        const RxSet *s = &g_w3a.set[i];
        for (o = 0; o + 0x40 <= s->size && g_w3nn < cap; o += 4) {
            const unsigned *t = (const unsigned *)(s->buf + o);
            if (t[0] == 0xbc614e && t[8] - 1 < 2048 && t[9] - 1 < 2048) memcpy(&g_w3names[g_w3nn++], &t[4], 8);
        }
    }
    qsort(g_w3names, g_w3nn, 8, W3NameCmp); }
static int W3WorldName(const char *name)
{   unsigned long long k; memcpy(&k, name, 8);
    return g_w3names && bsearch(&k, g_w3names, g_w3nn, 8, W3NameCmp) != NULL; }
/* RR_GpuQuad: 1 = leave this polygon to the 3D world */
static int W3Replaces(const RrGpuQuad *q)
{   int r = g_w3On && q->world && (q->levels <= 0 || W3WorldName(q->name));
    if (g_w3On && r && q->levels > 0 && getenv("RR_W3LOGR")) {   /* test switch: the polygons the 3D world replaces (names + frame size, once each) */
        static unsigned long long seen[512]; static int ns; unsigned long long k = 0; int i;
        memcpy(&k, q->name, 8);
        for (i = 0; i < ns && seen[i] != k; i++) ;
        if (i == ns && ns < 512) { seen[ns++] = k; LOG("world3d 3D: '%.8s' %dx%d at %.0f,%.0f %.0f,%.0f %.0f,%.0f %.0f,%.0f", q->name, q->w, q->h, q->x[0], q->y[0], q->x[1], q->y[1], q->x[2], q->y[2], q->x[3], q->y[3]); }
    }
    if (g_w3On && !r && getenv("RR_W3LOGQ")) {   /* test switch: the polygons still drawn in 2D (names, once each) */
        static unsigned long long seen[512]; static int ns; unsigned long long k = 0; int i;
        if (q->levels > 0) memcpy(&k, q->name, 8); else k = 0x544146ull | ((unsigned long long)q->colour << 32) | ((unsigned long long)q->world << 40);
        for (i = 0; i < ns && seen[i] != k; i++) ;
        if (i == ns && ns < 512) { seen[ns++] = k; if (q->levels > 0) LOG("world3d 2D: '%.8s' world %d %dx%d", q->name, q->world, q->w, q->h); else LOG("world3d 2D: flat colour %d world %d", q->colour, q->world); }
    }
    return r; }

static unsigned char *g_w3ring;   /* forks on: per piece, the game's own ring has the split there (W3Live; course3d.h C3_SoftFrames) */
static void W3FreeSoft(void)   /* the live copy's own arrays (forks on) */
{   if (g_w3l.soft) { free(g_w3l.frame); free(g_w3l.frameB); free(g_w3l.pairA); free(g_w3l.pairB); }
    else { if (g_w3l.frame != g_w3c.frame) free(g_w3l.frame); if (g_w3l.frameB != g_w3c.frameB) free(g_w3l.frameB); if (g_w3l.pairA != g_w3c.pairA) free(g_w3l.pairA); if (g_w3l.pairB != g_w3c.pairB) free(g_w3l.pairB); }
    free(g_w3l.other); free(g_w3ring); g_w3ring = NULL;
    g_w3l.frame = g_w3l.frameB = NULL; g_w3l.pairA = g_w3l.pairB = NULL; g_w3l.other = NULL; g_w3l.soft = 0; }
static void W3Free(void)
{   W3FreeSoft(); free(g_w3l.piece); free(g_w3l.v); free(g_w3l.vpiece); memset(&g_w3l, 0, sizeof g_w3l); g_w3lnb = 0; g_w3lo = g_w3hi = -1;   /* (its frames / frameB are g_w3c's) */
    free(g_w3c.piece); free(g_w3c.frame); free(g_w3c.frameB); free(g_w3c.v); free(g_w3c.vpiece); free(g_w3c.bdead); free(g_w3c.pairA); free(g_w3c.pairB); free(g_w3c.bldg); memset(&g_w3c, 0, sizeof g_w3c);
    free(g_w3a.file); free(g_w3a.bind); free(g_w3a.ext); memset(&g_w3a, 0, sizeof g_w3a); }

/* ---- new buildings (urban tracks): generated facades in place of the course's (course3d.h C3_FacadeVary) - six kinds of wall (brick, stucco, concrete
   with balconies, stone with arched windows, a glass office tower, painted clapboard) in several colours, rows of windows (frames, sills, glass with
   the sky in it, blinds), a cornice, and a ground floor (a shop with its sign and awning, a cafe, a glass lobby, a garage).
   Texture alpha marks what lights up at night (render3d.h): 0.75 = a window (some of them, at random), 0.65 = a sign.  Also the course's own facades:
   their dark glass is found (W3WindowMask) and some windows marked the same way ---- */
#define GF_W 256
#define GF_H 512
#define GF_N 24
static unsigned *g_gfPx[GF_N];
static unsigned GfRgba(int r, int g, int b, int a)
{   if (r < 0) r = 0; if (r > 255) r = 255; if (g < 0) g = 0; if (g > 255) g = 255; if (b < 0) b = 0; if (b > 255) b = 255;
    return (unsigned)r | ((unsigned)g << 8) | ((unsigned)b << 16) | ((unsigned)a << 24); }
static float GfRnd(unsigned *s) { *s = *s * 1664525u + 1013904223u; return ((*s >> 8) & 0xffff) / 65535.0f; }
static int GfHash(int x, int y, unsigned s) { unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return (int)((h ^ (h >> 16)) & 255); }
static void GfPut(unsigned *p, int X, int Y, int r, int g, int b, int a) { if (X >= 0 && X < GF_W && Y >= 0 && Y < GF_H) p[Y * GF_W + X] = GfRgba(r, g, b, a); }
static void GfBox(unsigned *p, int x0, int y0, int x1, int y1, int r, int g, int b, int a) { int X, Y; for (Y = y0; Y <= y1; Y++) for (X = x0; X <= x1; X++) GfPut(p, X, Y, r, g, b, a); }
enum { GF_BRICK, GF_STUCCO, GF_CONCRETE, GF_STONE, GF_GLASS, GF_CLAP };
/* the wall's own texture: colour wc, kind st */
static void GfWall(unsigned *p, int st, const int *wc, unsigned s)
{   int X, Y;
    for (Y = 0; Y < GF_H; Y++) for (X = 0; X < GF_W; X++) {
        int n = GfHash(X, Y, s) % 17 - 8, r = wc[0], g = wc[1], b = wc[2], v = 0;
        switch (st) {
        case GF_BRICK: { int row = Y / 8, off = (row & 1) * 11, bx = (X + off) / 22;
            if ((Y % 8) == 7 || ((X + off) % 22) == 21) { r = 168; g = 162; b = 150; n /= 2; }   /* mortar */
            else v = GfHash(bx, row, s + 7) % 34 - 17; break; }   /* each brick a little different */
        case GF_STUCCO: v = GfHash(X / 24, Y / 24, s + 3) % 10 - 5; n = n * 2 / 3; break;
        case GF_CONCRETE: v = GfHash(X / 64, Y / 64, s + 5) % 14 - 7 - (GfHash(X / 3, 0, s) % 10) * (Y % 64) / 64;   /* panels, rain streaks */
            if ((X % 64) == 0 || (Y % 64) == 0) v -= 26; break;
        case GF_STONE: { int row = Y / 18, off = (row & 1) * 17, bx = (X + off) / 34;
            if ((Y % 18) == 17 || ((X + off) % 34) == 33) v = -30; else v = GfHash(bx, row, s + 9) % 20 - 10; break; }
        case GF_GLASS: v = (GF_H - Y) / 12 - 18; n /= 3; break;
        case GF_CLAP: { int k = Y % 9; v = k == 8 ? -40 : k == 0 ? 14 : 6 - k; n /= 2; break; } }
        p[Y * GF_W + X] = GfRgba(r + v + n, g + v + n, b + v + n, 255); } }
/* a window x0..x1 / y0..y1: frame, glass (the sky reflected, darker low down), maybe blinds, glazing bars; lit = marked to light up at night */
static void GfWindow(unsigned *p, int x0, int y0, int x1, int y1, const int *fc, int lit, int blinds, int arch, int bars, unsigned s)
{   int X, Y, w = x1 - x0, h = y1 - y0;
    GfBox(p, x0 - 3, y0 - 3, x1 + 3, y1 + 3, fc[0], fc[1], fc[2], 255);   /* frame */
    for (Y = y0; Y <= y1; Y++) for (X = x0; X <= x1; X++) {
        float k = (float)(Y - y0) / (h + 1), refl = ((X - x0) * 2 + (Y - y0)) % 40 < 7 ? 26.0f : 0.0f; int r, g, b;
        if (arch && Y - y0 < w / 2) { float dx = (X - x0 - w * 0.5f) / (w * 0.5f), dy = (Y - y0 - w * 0.5f) / (w * 0.5f); if (dx * dx + dy * dy > 1) continue; }   /* a round top */
        r = (int)(96 - 58 * k + refl); g = (int)(116 - 66 * k + refl); b = (int)(142 - 70 * k + refl);
        if (blinds && (Y - y0) < h * 0.55f) { int on = ((Y - y0) % 4) < 3; r = on ? 182 : 120; g = on ? 172 : 112; b = on ? 150 : 98; }   /* blinds half down */
        if (bars && (X == x0 + w / 2 || X == x0 + w / 2 + 1 || (bars > 1 && Y == y0 + h / 2))) { p[Y * GF_W + X] = GfRgba(fc[0], fc[1], fc[2], 255); continue; }
        p[Y * GF_W + X] = GfRgba(r + GfHash(X, Y, s) % 7, g + GfHash(X, Y, s) % 7, b, lit ? 0xBF : 255); } }
/* the ground floor y0 .. GF_H: kind gk (0 shop, 1 cafe, 2 lobby, 3 garage) */
static void GfGround(unsigned *p, int gk, int y0, const int *wc, int t, unsigned *s)
{   static const int awns[7][3] = { { 185, 32, 32 }, { 30, 115, 62 }, { 32, 64, 155 }, { 205, 145, 32 }, { 125, 42, 125 }, { 45, 45, 45 }, { 20, 120, 130 } };
    const int *aw = awns[(t * 3 + gk) % 7]; int X, Y, door = 16 + (int)(GfRnd(s) * (GF_W - 80)), sb[3];
    sb[0] = 25 + (int)(GfRnd(s) * 45); sb[1] = 20 + (int)(GfRnd(s) * 45); sb[2] = 30 + (int)(GfRnd(s) * 70);
    for (Y = y0; Y < GF_H; Y++) for (X = 0; X < GF_W; X++) {
        unsigned *q = &p[Y * GF_W + X]; int dy = Y - y0, n = GfHash(X, Y, (unsigned)t) % 9 - 4;
        if (gk == 3) {   /* a garage: a roll-up door of slats, a small door beside it, a lit name board */
            if (X >= 24 && X < 184 && dy >= 26) *q = ((dy - 26) % 9) == 8 ? GfRgba(70, 72, 74, 255) : GfRgba(132 + n, 134 + n, 136 + n, 255);
            else if (X >= 204 && X < 236 && dy >= 40) *q = X == 204 || X == 235 || dy == 40 ? GfRgba(50, 48, 46, 255) : GfRgba(92 + n, 70 + n, 52 + n, 255);
            else if (dy < 14) *q = GfRgba(sb[0] + 60, sb[1] + 60, sb[2] + 60, 0xA6);
            else *q = GfRgba(wc[0] - 30 + n, wc[1] - 30 + n, wc[2] - 30 + n, 255);
            continue; }
        if (gk == 2) {   /* a lobby: a canopy, tall glass doors (lit), a stone surround and step */
            if (dy < 18) *q = GfRgba(40 + n, 40 + n, 44 + n, 255);
            else if (Y >= GF_H - 10) *q = GfRgba(150 + n, 146 + n, 140 + n, 255);
            else if (X >= 60 && X < 196) *q = ((X - 60) % 34) < 3 ? GfRgba(180, 170, 140, 255) : GfRgba(150 + dy / 4, 140 + dy / 4, 110 + dy / 5, 0xBF);
            else *q = GfRgba(170 + n, 166 + n, 158 + n, 255);
            continue; }
        if (dy < 30) *q = GfRgba(sb[0], sb[1], sb[2], 255);   /* sign band */
        else if (dy < 52) {   /* awning: stripes (shop) or a scalloped solid one (cafe) */
            int sh = dy > 46 ? 40 : 0;
            if (gk == 1) { int sc = dy > 44 && ((X % 16) - 8) * ((X % 16) - 8) + (dy - 44) * (dy - 44) * 4 > 64; *q = sc ? GfRgba(wc[0] - 40, wc[1] - 40, wc[2] - 40, 255) : GfRgba(aw[0] - sh, aw[1] - sh, aw[2] - sh, 255); }
            else *q = ((X / 16) & 1) ? GfRgba(aw[0] - sh, aw[1] - sh, aw[2] - sh, 255) : GfRgba(235 - sh, 232 - sh, 225 - sh, 255); }
        else if (Y >= GF_H - 12) *q = GfRgba(40, 38, 36, 255);   /* kick plate */
        else if (X >= door && X < door + 44) *q = (X <= door + 1 || X >= door + 42 || dy < 58) ? GfRgba(60, 50, 45, 255) : GfRgba(74, 60, 48, (X - door) % 20 < 15 && dy < 116 ? 0xBF : 255);   /* door */
        else if ((X % 64) < 4 || dy < 58) *q = GfRgba(70, 66, 62, 255);   /* mullions */
        else { int item = ((X / 12) * 7 + (dy / 14) * 13 + t * 5) % 9;   /* display windows (lit): goods / tables */
            *q = item < 2 && dy > 96 ? GfRgba(150 + item * 40, 90 + t * 5, 70 + item * 60, 0xBF) : GfRgba(84 + dy / 5, 92 + dy / 5, 104 + dy / 5, 0xBF); } }
    if (gk <= 1) { int letters = 4 + (int)(GfRnd(s) * 6), sx = 18 + (int)(GfRnd(s) * 30), L;   /* the sign's lettering (lights up) */
        for (L = 0; L < letters; L++) for (Y = y0 + 7; Y < y0 + 23; Y++) for (X = sx + L * 21; X < sx + L * 21 + 15 && X < GF_W - 8; X++) {
            int lx = X - sx - L * 21, ly = Y - y0 - 7, shape = (L * 7 + t * 3) % 4, on;
            on = lx < 3 || ly < 2 || ly > 13 || (shape == 0 && (ly == 7 || ly == 8)) || (shape == 1 && lx > 11) || (shape == 2 && lx == ly) || (shape == 3 && (ly == 7 || lx > 11));
            if (on) p[Y * GF_W + X] = GfRgba(255 - (t * 37) % 90, 220 - (t * 53) % 140, 120 + (t * 29) % 130, 0xA6); } } }
/* the City set (user, 2026-11-14: "replace the buildings in the city ... a variety of different buildings ... signs that stick out, with some neon
   signs, get fooocus to generate them"; the rules: CITY_SCENERY.md): <mod>\hd\city\FAC<nn>.png (512 x 1024 facades) and SGN<nn>.png (256 x 768
   projecting signs), made by analysis\city_hd\scripts\facades.py.  RGBA, the alpha byte a texel code the shader reads (render3d.h: 255 wall,
   0xB2 window glass, 0x99 shop window, 0xA6 sign lettering, 0x8C neon tube, 0 none); a facade's texels (0,0) / (1,0) = its layout (alpha 0xFE).
   Returns 1 when facades were found (they replace the drawn GENFAC set).  Test switch RR_W3NOCITY=1: the drawn set */
static char g_mwDir[3][MAX_PATH]; static int g_mwNDir; static void MwDirs(void);   /* (menuwide.inc: the mod's folders) */
static int W3LoadCityArt(void)
{   char dir[MAX_PATH], p[MAX_PATH]; int d;
    if (getenv("RR_W3NOCITY")) return 0;
    if (g_mwNDir < 0) MwDirs();
    dir[0] = 0;
    for (d = 0; d < g_mwNDir && !dir[0]; d++) { DWORD at; _snprintf(p, sizeof p - 1, "%s\\hd\\city", g_mwDir[d]); p[sizeof p - 1] = 0;
        at = GetFileAttributesA(p); if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_DIRECTORY)) strcpy(dir, p); }
    if (!dir[0]) return 0;
    C3_CityArt(&g_w3a, dir);   /* (ground_art.h: the Course Editor loads the same) */
    LOG("world3d: City set from %s: %d facades, %d signs", dir, g_w3a.ngen, g_w3a.nsign);
    return g_w3a.ngen > 0; }
/* the rocks' texture (course3d.h C3_Rock, on the dirt: user, "make them actual rocks or models textured rocks"): 128 x 128, wrapping - a grey stone,
   blotched in two scales, dark cracks, light grains, a few lichen spots; the rock's own colour tints it */
static unsigned g_rockPx[128 * 128]; static unsigned W3Rgba(float r, float g, float b, float a);
static float W3RockN(float x, float y, int o)   /* wrapping value noise over a 128 cell period */
{   int xi = (int)floorf(x), yi = (int)floorf(y), P = o; float fx = x - xi, fy = y - yi, v[4]; int k;
    for (k = 0; k < 4; k++) { unsigned a = (unsigned)(((xi + (k & 1)) % P + P) % P), b = (unsigned)(((yi + (k >> 1)) % P + P) % P), h = a * 374761393u + b * 668265263u + (unsigned)o * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; v[k] = (h & 65535u) / 65535.0f; }
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    return (v[0] + (v[1] - v[0]) * fx) + ((v[2] + (v[3] - v[2]) * fx) - (v[0] + (v[1] - v[0]) * fx)) * fy; }
static void W3RockTex(void)
{   int x, y; RxTex *t;
    g_w3a.rockTex = -1;
    if (g_w3a.ntex >= 8192) return;
    for (y = 0; y < 128; y++) for (x = 0; x < 128; x++) {
        float b = W3RockN(x / 32.0f, y / 32.0f, 4) * 0.5f + W3RockN(x / 8.0f, y / 8.0f, 16) * 0.3f + W3RockN((float)x, (float)y, 128) * 0.2f;
        float cr = W3RockN(x / 16.0f + 3.3f, y / 16.0f, 8), crack = cr > 0.48f && cr < 0.52f ? 0.55f : 1.0f, li = W3RockN(x / 6.0f, y / 6.0f, 21);
        float r = (0.52f + 0.42f * b) * crack, g = (0.50f + 0.40f * b) * crack, bl = (0.47f + 0.36f * b) * crack;
        if (li > 0.82f) { r = r * 0.6f + 0.30f; g = g * 0.6f + 0.33f; bl = bl * 0.6f + 0.22f; }   /* (lichen) */
        g_rockPx[y * 128 + x] = W3Rgba(r, g, bl, 1); }
    t = &g_w3a.tex[g_w3a.ntex]; memset(t, 0, sizeof *t);
    strcpy(t->name, "GENROCK"); t->w = 128; t->h = 128; t->gen = g_rockPx; t->gpu = -1;
    g_w3a.rockTex = g_w3a.ntex++; }
static void W3GroundArt(void)   /* the extra ground pictures (ground_art.h: REDWOOD) from the mod's hd\ground */
{   char p[MAX_PATH]; int d, n;
    if (g_mwNDir < 0) MwDirs();
    for (d = 0; d < g_mwNDir; d++) { DWORD at; _snprintf(p, sizeof p - 1, "%s\\hd\\ground", g_mwDir[d]); p[sizeof p - 1] = 0;
        at = GetFileAttributesA(p); if (at == INVALID_FILE_ATTRIBUTES || !(at & FILE_ATTRIBUTE_DIRECTORY)) continue;
        _snprintf(p, sizeof p - 1, "%s\\hd", g_mwDir[d]); p[sizeof p - 1] = 0;
        n = C3_GroundArt(&g_w3a, p); LOG("world3d: %d extra ground pictures from %s\\ground (trees: %d)", n, p, g_w3a.groundTree[0] != 0); return; }
    C3_GroundArt(&g_w3a, NULL); }
static void W3GenFacades(int urban)
{   static const int pal[6][4][3] = {
        { { 150, 70, 54 }, { 122, 56, 44 }, { 176, 112, 72 }, { 104, 74, 64 } },          /* brick: red, dark red, buff, brown */
        { { 214, 204, 180 }, { 206, 170, 160 }, { 176, 196, 172 }, { 226, 222, 210 } },    /* stucco: cream, pink, sage, white */
        { { 150, 150, 152 }, { 126, 128, 132 }, { 170, 164, 154 }, { 112, 118, 124 } },    /* concrete */
        { { 196, 178, 140 }, { 170, 166, 158 }, { 182, 160, 128 }, { 140, 136, 130 } },    /* stone: sandstone, grey, ochre, slate */
        { { 54, 92, 104 }, { 48, 68, 98 }, { 96, 82, 62 }, { 70, 84, 90 } },               /* glass: teal, blue, bronze, grey */
        { { 150, 182, 208 }, { 220, 206, 140 }, { 160, 204, 180 }, { 186, 166, 204 } } };  /* clapboard: blue, yellow, mint, lavender */
    static const int trims[4][3] = { { 236, 232, 222 }, { 60, 56, 54 }, { 120, 40, 36 }, { 46, 70, 60 } };
    int t; g_w3a.gen0 = g_w3a.ntex; g_w3a.ngen = 0; g_w3a.sign0 = g_w3a.ntex; g_w3a.nsign = 0;
    if (!urban) return;
    if (W3LoadCityArt()) return;   /* the City set (Fooocus, CITY_SCENERY.md) in place of the drawn ones */
    for (t = 0; t < GF_N && g_w3a.ntex < 8192; t++) {
        unsigned s = 777u + (unsigned)t * 131u, *p; RxTex *x; int st = t % 6, ci = (t / 6) % 4, gk, top = 22, shop = 128, floors, cols, fl, f, c, X;
        const int *wc = pal[st][ci], *fc = st == GF_STUCCO || st == GF_CLAP ? trims[(t / 6 + st) % 4] : trims[(t + 1) % 2];
        if (!(p = (unsigned *)realloc(g_gfPx[t], GF_W * GF_H * 4))) break;
        g_gfPx[t] = p;
        gk = st == GF_GLASS ? 2 : (st == GF_CONCRETE && ci == 3) ? 3 : ((t * 7 / 3) % 2 ? 1 : 0);   /* the ground floor */
        if (st == GF_STONE && ci == 1) gk = 2;
        floors = st == GF_GLASS ? 9 : st == GF_CLAP ? 3 + (t & 1) : 5 + (int)(GfRnd(&s) * 3);
        cols = st == GF_GLASS ? 4 : st == GF_CLAP ? 2 + (t & 1) : 3 + (int)(GfRnd(&s) * 3);
        fl = (GF_H - shop - top) / floors;
        GfWall(p, st, wc, s);
        for (f = 0; f < floors; f++) for (c = 0; c < cols; c++) {
            int cw = GF_W / cols, y0 = top + f * fl, lit = GfRnd(&s) < 0.4f, blinds = GfRnd(&s) < 0.35f;
            if (st == GF_GLASS) {   /* curtain wall: a panel per bay, mullions and a spandrel band per floor */
                int x0 = c * cw + 3, x1 = (c + 1) * cw - 4, Y, XX;
                for (Y = y0; Y < y0 + fl; Y++) for (XX = c * cw; XX < (c + 1) * cw; XX++) {
                    int mull = XX < x0 || XX > x1, span = Y - y0 >= fl - 10, n = GfHash(XX, Y, s) % 5;
                    if (mull) GfPut(p, XX, Y, 78, 84, 90, 255);
                    else if (span) GfPut(p, XX, Y, wc[0] / 2 + 10 + n, wc[1] / 2 + 10 + n, wc[2] / 2 + 12 + n, 255);
                    else { float k = (float)(Y - y0) / fl, sky = 1.0f - (float)Y / GF_H; int rf = ((XX + Y) % 60) < 9 ? 24 : 0;
                        GfPut(p, XX, Y, (int)(wc[0] + 70 * sky - 30 * k) + rf + n, (int)(wc[1] + 80 * sky - 30 * k) + rf + n, (int)(wc[2] + 90 * sky - 30 * k) + rf + n, lit ? 0xBF : 255); } }
                continue; }
            {   int x0 = c * cw + cw / 4, x1 = c * cw + cw * 3 / 4, wy0 = y0 + fl / 5, wy1 = y0 + fl * 4 / 5, arch = st == GF_STONE && (f == floors - 1 || ci != 1);
                if (st == GF_CLAP) { x0 = c * cw + cw / 5; x1 = c * cw + cw * 4 / 5; }   /* wide windows with shutters */
                GfWindow(p, x0, wy0, x1, wy1, fc, lit, blinds, arch, x1 - x0 > 40 ? 1 + (st == GF_CLAP) : 0, s + (unsigned)(f * 31 + c));
                GfBox(p, x0 - 6, wy1 + 4, x1 + 6, wy1 + 7, fc[0] + 20, fc[1] + 20, fc[2] + 20, 255);   /* sill */
                if (st == GF_BRICK || st == GF_STONE) GfBox(p, x0 - 4, wy0 - 8, x1 + 4, wy0 - 5, wc[0] + 30, wc[1] + 30, wc[2] + 26, 255);   /* lintel */
                if (st == GF_CLAP) { GfBox(p, x0 - 14, wy0, x0 - 6, wy1, fc[0] / 2 + 30, fc[1] / 2 + 60, fc[2] / 2 + 40, 255); GfBox(p, x1 + 6, wy0, x1 + 14, wy1, fc[0] / 2 + 30, fc[1] / 2 + 60, fc[2] / 2 + 40, 255); }
                if (st == GF_CONCRETE && f > 0 && (c + f) % 2 == 0) {   /* a balcony: slab and railing */
                    int Y2; GfBox(p, x0 - 10, wy1 + 2, x1 + 10, wy1 + 6, 96, 96, 98, 255);
                    for (Y2 = wy1 - 14; Y2 < wy1 + 2; Y2++) for (X = x0 - 10; X <= x1 + 10; X++) if (Y2 == wy1 - 14 || (X % 6) == 0) GfPut(p, X, Y2, 52, 54, 58, 255); } } }
        for (X = 0; X < GF_W; X++) {   /* the cornice (stone: dentils) */
            int Y; for (Y = 0; Y < top; Y++) { int k = Y < 4 ? 36 : Y > top - 5 ? 24 : (st == GF_STONE && (X % 12) < 6 && Y > 8 && Y < 14) ? 30 : -24;
                GfPut(p, X, Y, wc[0] * 3 / 4 + k, wc[1] * 3 / 4 + k, wc[2] * 3 / 4 + k, 255); } }
        if (st == GF_BRICK && (t & 1)) { int px0 = 8 + (int)(GfRnd(&s) * 8); GfBox(p, px0, top, px0 + 3, GF_H - shop - 1, 70, 72, 74, 255); }   /* a downpipe */
        GfGround(p, gk, GF_H - shop, wc, t, &s);
        x = &g_w3a.tex[g_w3a.ntex++]; memset(x, 0, sizeof *x);
        _snprintf(x->name, sizeof x->name, "GENFAC%02d", t); x->name[8] = 0; x->w = GF_W; x->h = GF_H; x->gen = p; x->gpu = -1; x->facade = 1; g_w3a.ngen++; }
    if (getenv("RR_W3FACDUMP")) {   /* test switch RR_W3FACDUMP=<folder>: the generated facades as .bmp files */
        char fn[MAX_PATH]; int k; for (k = 0; k < g_w3a.ngen; k++) { FILE *o; BITMAPFILEHEADER bh; BITMAPINFOHEADER ih; int Y;
            _snprintf(fn, sizeof fn, "%s\\genfac%02d.bmp", getenv("RR_W3FACDUMP"), k); fn[sizeof fn - 1] = 0; if (!(o = fopen(fn, "wb"))) continue;
            memset(&bh, 0, sizeof bh); memset(&ih, 0, sizeof ih); bh.bfType = 0x4d42; bh.bfOffBits = sizeof bh + sizeof ih; bh.bfSize = bh.bfOffBits + GF_W * GF_H * 4;
            ih.biSize = sizeof ih; ih.biWidth = GF_W; ih.biHeight = GF_H; ih.biPlanes = 1; ih.biBitCount = 32;
            fwrite(&bh, sizeof bh, 1, o); fwrite(&ih, sizeof ih, 1, o);
            for (Y = GF_H - 1; Y >= 0; Y--) { int X2; for (X2 = 0; X2 < GF_W; X2++) { unsigned c = g_gfPx[k][Y * GF_W + X2], bgr = ((c & 255) << 16) | (c & 0xff00) | ((c >> 16) & 255);
                if ((c >> 24) == 0xBF) bgr = (bgr & 0xfefefe) / 2 + 0x606000; fwrite(&bgr, 4, 1, o); } }   /* (lit windows tinted yellow) */
            fclose(o); } }
    LOG("world3d: %d generated facades", g_w3a.ngen); }
/* the course's own facade pictures: dark glass (dark, not reddish) in pieces of window size; some of them (a fixed random choice) marked to light up */
static void W3WindowMask(unsigned *px, int w, int h, unsigned seed)
{   unsigned char *m = (unsigned char *)calloc((size_t)w * h, 1); int *st = (int *)malloc(sizeof(int) * w * h), *ls = (int *)malloc(sizeof(int) * w * h), i, n = w * h;
    if (!m || !st || !ls) { free(m); free(st); free(ls); return; }
    for (i = 0; i < n; i++) { unsigned c = px[i]; int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255; if ((c >> 24) == 255 && r + g + b < 230 && b + 6 >= r) m[i] = 1; }
    for (i = 0; i < n; i++) if (m[i] == 1) {   /* each piece of glass (flood fill): window sized -> lit or not */
        int sp = 0, cnt = 0, j, lit, bx0 = w, bx1 = 0, by0 = h, by1 = 0; st[sp++] = i; m[i] = 2;
        while (sp) { int k = st[--sp], x = k % w, y = k / w; ls[cnt++] = k; if (x < bx0) bx0 = x; if (x > bx1) bx1 = x; if (y < by0) by0 = y; if (y > by1) by1 = y;
            if (x > 0 && m[k - 1] == 1) { m[k - 1] = 2; st[sp++] = k - 1; } if (x < w - 1 && m[k + 1] == 1) { m[k + 1] = 2; st[sp++] = k + 1; }
            if (y > 0 && m[k - w] == 1) { m[k - w] = 2; st[sp++] = k - w; } if (y < h - 1 && m[k + w] == 1) { m[k + w] = 2; st[sp++] = k + w; } }
        {   int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;   /* a window: not too big, roughly a filled rectangle, not a long thin band */
            lit = cnt >= n / 2000 + 4 && cnt <= n / 45 && bw <= w / 3 && bh <= h / 3 && cnt * 10 >= bw * bh * 6 && bw <= bh * 3 && bh <= bw * 4 && GfRnd(&seed) < 0.38f; }   /* (bigger dark areas: doorways, shaded walls - not windows) */
        if (lit) for (j = 0; j < cnt; j++) px[ls[j]] = (px[ls[j]] & 0xFFFFFFu) | 0xBF000000u; }
    free(m); free(st); free(ls); }
/* trees, bushes, lamp posts, poles: their pictures have a shadow painted at the foot (a near-black blob trailing left).  It is taken out (W3GpuTex)
   and each of them casts a real one instead, away from the sun (W3CasterShadows) */
static void W3Casters(void);   /* (below) */
static void W3HydHideLive(void); static void W3Hydrants(void);   /* (below: the fire hydrants) */
static int W3IsCaster(const char *nm)
{   static const char *pre[] = { "TREE", "BUSH", "BSH", "LAMPPST", "LMPPST", "PRKGLMP", "STRTLTE", "TELE" }; int i;
    for (i = 0; i < (int)(sizeof pre / sizeof pre[0]); i++) if (!strncmp(nm, pre[i], strlen(pre[i]))) return 1;
    return 0; }
/* the painted shadow: dark navy (the original's 8,24,40 / 14,24,30; the HD pictures' the same), in the bottom fifth -> transparent.  Only the bluish
   texels: the dark foliage and trunks there are neutral black (user, 2026-11-15: "this bush is floating" - taking every dark texel cut its foot off) */
static void W3StripShadow(unsigned *px, int w, int h)
{   int x, y;
    for (y = h * 4 / 5; y < h; y++) for (x = 0; x < w; x++) { unsigned c = px[y * w + x]; int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
        if ((c >> 24) >= 128 && r + g + b < 330 && b >= g + 6 && b >= r + 10) px[y * w + x] = c & 0x00ffffffu; } }
/* the renderer texture of course texture t (made on first use; -1 = flat colour) */
static int W3GpuTex(int t)
{   RxTex *x;
    if (t < 0 || t >= g_w3a.ntex) return -1;
    x = &g_w3a.tex[t];
    if (x->gpu == -1 && x->edgeOf > 0 && x->edgeOf <= g_w3a.ntex) {   /* a ragged edge (course3d.h C3_Blend): cut from its picture as drawn - the HD one where there is one */
        RxTex *b = &g_w3a.tex[x->edgeOf - 1]; const DWORD *bh; unsigned *src = NULL, *e; int W = b->w, H = b->h, w, h, j;
        g_hdTrackHint = g_w3hdTrack; bh = HdTexOn() && !b->gen && b->bits ? HdTexFind(b->name, b->w, b->h, b->bits, b->w) : NULL; g_hdTrackHint = 0;
        if (bh && (src = (unsigned *)malloc((size_t)16 * b->w * b->h * 4)) != NULL) { W = 4 * b->w; H = 4 * b->h; for (j = 0; j < W * H; j++) { DWORD p = bh[j]; src[j] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16); } }
        if (!src) src = RX_Rgba(&g_w3a, b);
        e = src ? RX_EdgeCut(src, W, H, x->edgeDir, &w, &h) : NULL; free(src);
        x->gpu = e ? R3_Texture(&g_w3r, e, w, h) : -1; free(e);
        if (x->gpu < 0) x->gpu = -3;   /* (none: not tried again) */
        return x->gpu >= 0 ? x->gpu : -1; }
    if (x->edgeOf) return x->gpu >= 0 ? x->gpu : -1;
    if (x->gpu < 0) { const DWORD *hp; g_hdTrackHint = g_w3hdTrack;   /* (the course's track: hdtex.inc HdTrack) */
        hp = HdTexOn() && !x->gen ? HdTexFind(x->name, x->w, x->h, x->bits, x->w) : NULL;   /* "slightly upscaled" (hdtex.inc): the 4x picture, same palette as the file */
        g_hdTrackHint = 0;
        unsigned seed = 2166136261u; { const char *c; for (c = x->name; *c; c++) seed = (seed ^ (unsigned char)*c) * 16777619u; }   /* (a facade's lit windows: W3WindowMask) */
        if (hp) { unsigned *rgba = (unsigned *)malloc((size_t)16 * x->w * x->h * 4); int j;
            if (rgba) { for (j = 0; j < 16 * x->w * x->h; j++) { DWORD p = hp[j]; rgba[j] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16); }   /* ARGB -> ABGR */
                        if (x->facade) W3WindowMask(rgba, 4 * x->w, 4 * x->h, seed);
                        if (W3IsCaster(x->name)) W3StripShadow(rgba, 4 * x->w, 4 * x->h);
                        x->gpu = R3_Texture(&g_w3r, rgba, 4 * x->w, 4 * x->h); free(rgba); } }
        if (x->gpu < 0) { unsigned *rgba = RX_Rgba(&g_w3a, x); if (rgba && x->facade && !x->gen) W3WindowMask(rgba, x->w, x->h, seed); if (rgba && W3IsCaster(x->name)) W3StripShadow(rgba, x->w, x->h); x->gpu = R3_Texture(&g_w3r, rgba, x->w, x->h); free(rgba); } }
    return x->gpu; }
/* mesh batches -> renderer batches */
static void W3GpuBatches(const C3Batch *cb, int n, R3Batch *out)
{   int i;
    for (i = 0; i < n; i++) {
        int t = cb[i].tex, bg = t >= C3_BG - 1;
        if (bg) t -= C3_BG;   /* skirts: background batches (course3d.h C3_Skirt, render3d.h alpha 2) */
        out[i].first = cb[i].first; out[i].count = cb[i].count; out[i].tex = W3GpuTex(t); out[i].alpha = bg ? 2 : ((t >= 0 && t < g_w3a.ntex && g_w3a.tex[t].alpha) ? 1 : 0);
    } }


/* every frame: the ring's stretch of the course from the game's live pieces (RrWorld3DCam.ring); rebuilt when the stretch moved or its decoration changed */
static void W3GrassLive(void);   /* (below) */
static void W3Live(void)
{   static C3Batch lb[4096]; static int q; int lo = (g_w3cam.ringBack - g_w3Piece0) >> 8, hi, hiNeed, i, changed = 0, half;
    int vwk = g_vwHold ? (int)ViewerPiece() : -1;   /* the Course Editor's viewer (viewer.inc): the stretch round the piece it looks at, the draw distance both ways - not the held race's ring */
    if (vwk >= 0) lo = vwk - (int)(W3Far() / W3_L) - 2;
    /* the stretch starts on a multiple of q pieces and reaches q further: built again every q pieces, not every piece (user, 2026-11-15: the Steam Deck's
       frame rate - ~1.2 ms a frame on a Ryzen 5800X); its first few pieces are the ring's pieces of a moment ago.  Test switch RR_W3LIVEQ=1: every piece */
    if (!q) { q = getenv("RR_W3LIVEQ") ? atoi(getenv("RR_W3LIVEQ")) : 8; if (q < 1) q = 1; }
    lo -= ((lo % q) + q) % q; hi = lo + RRW_LIVE - 1 + (q - 1);
    /* ONE mesh (user, 2026-11-15: "make new meshes please and only reference one type of mesh"): this stretch reaches past the ring on to the draw
       distance - the ring's pieces with the game's own decoration, the rest as the course file has them - and the course file's static mesh is no
       longer drawn (it stays as data: grass, lamps, shadow casters are found in it).  Built again when the ring moves on q pieces or the view's end
       passes the stretch's (with q pieces to spare) */
    {   int kc = vwk >= 0 ? vwk : (g_w3cam.viewPos - g_w3Piece0) >> 8; hiNeed = kc + (int)(W3Far() / W3_L) + 2; if (hi < hiNeed + q) hi = hiNeed + q; }
    if (!g_w3l.piece || g_w3compiled) return;   /* (the compiled race: cut each frame instead - W3StaticCut) */
    {   /* a split road within the draw distance: the live stretch covers all of it - only the live mesh leaves out the carriageway the camera is not on
           (C3_HideHalf); the static mesh has both, and B's made-up path can pass over / beside A far from the fork */
        int kEnd = (vwk >= 0 ? vwk : (g_w3cam.viewPos - g_w3Piece0) >> 8) + (int)(W3Far() / W3_L) + 2, k;
        if (kEnd > lo + 400) kEnd = lo + 400;
        if (kEnd > g_w3c.n - 1) kEnd = g_w3c.n - 1;
        for (k = lo > 0 ? lo : 0; k <= kEnd; k++) if (g_w3c.piece[k].layout == 2) { if (kEnd > hi) hi = kEnd; break; } }
    if (lo < 0) lo = 0;
    if (hi > g_w3c.n - 1) hi = g_w3c.n - 1;
    if (hi <= lo) return;
    if (g_w3l.soft) for (i = lo; i <= hi; i++) {   /* (beyond the game's ring: as the file has it) */
        RoadPiece *p = &g_w3l.piece[i]; const RoadPiece *f = &g_w3c.piece[i];
        g_w3ring[i] = 1;
        if (f->layout == 2) { p->splitX = f->splitX; p->splitSlope = f->splitSlope; if (g_w3camB) p->halfA = f->halfA; else p->halfB = f->halfB; } }
    for (i = g_vwHold ? RRW_LIVE : 0; i < RRW_LIVE; i++) {   /* (the Course Editor's viewer: the course as the editor has it - not the held race's ring) */
        int k = (g_w3cam.ringBack - g_w3Piece0) / 256 + i; const RoadPiece *lp = (const RoadPiece *)g_w3cam.ring[i]; RoadPiece *p;
        if (k < 0 || k >= g_w3c.n || !lp) continue;
        p = &g_w3l.piece[k];
        if (g_w3l.soft) {   /* forks on: the game's view has the split here or not (on B it builds B alone once past the fork) - and where it has, the other
                               carriageway is the game's: on B its pieces hold A's END counted back from the join and splitX relative to B (Sierra ~8000; the
                               file's runs up to 1.6 million).  On B the ring is numbered as B (g_w3offB), A's halves there are A's pieces + the difference */
            int kb = k + (g_w3camB ? g_w3offB - g_w3off : 0); RoadPiece *pb;
            if (kb >= 0 && kb < g_w3c.n) { pb = &g_w3l.piece[kb];
                g_w3ring[kb] = lp->layout == 2;
                if (lp->layout == 2 && pb->layout == 2) { pb->splitX = lp->splitX; pb->splitSlope = lp->splitSlope; if (g_w3camB) pb->halfA = lp->halfA; else pb->halfB = lp->halfB; } } }
        if (getenv("RR_W3LOGB") && k >= atoi(getenv("RR_W3LOGB")) && k < atoi(getenv("RR_W3LOGB")) + 900) { static int seen[4096]; if (!seen[k]) { seen[k] = 1;   /* test switch RR_W3LOGB=<piece>: the game's ring pieces against the file's (900 pieces from there; after W3Track's offset) */
            LOG("w3liveB piece %d layout %d/%d: live splitX %d splitSlope %d link %d / file %d %d %d; live B curve %d A curve %d / file %d %d; half %d", k, lp->layout, p->layout, lp->splitX, lp->splitSlope, lp->link, p->splitX, p->splitSlope, p->link, lp->halfB.curve, lp->halfA.curve, p->halfB.curve, p->halfA.curve, g_w3cam.half); } }
        if (lp->halfA.curve != p->halfA.curve || lp->halfA.hill != p->halfA.hill || lp->halfA.kind != p->halfA.kind || lp->layout != p->layout) continue;   /* not the same piece: keep the file's */
        for (half = 0; half < (p->layout == 2 ? (g_w3offB == g_w3off ? 2 : 1) : 1); half++) {   /* (half B only where the game numbers B like A: W3Track) */
            const RoadSection *ls = half ? &lp->halfB : &lp->halfA; RoadSection *s = half ? &p->halfB : &p->halfA;
            /* the course file is the shape (user, 2026-11-15: "the terrain heights shouldnt change per race"): the game's ring is no longer copied into the
               mesh - the file is the game's own road, extracted once per track and level - so nothing changes shape as the ring reaches it.  Where the ring
               differs anyway, RR_W3LOG logs it (once a piece); test switch RR_W3LIVEDECO=1: copied as before */
            int dw = s->kind >= SEC_WALL1 && s->kind <= SEC_WALL_SPLIT && memcmp(s->wall, ls->wall, sizeof s->wall), dp = s->kind == SEC_PROFILE && memcmp(s->profile, ls->profile, sizeof s->profile);
            if ((dw || dp) && getenv("RR_W3LOG")) { static unsigned char told[8192]; if (k < 8192 * 8 && !(told[k >> 3] & (1 << (k & 7)))) { int b, nb = 0; const unsigned char *x = dw ? (const unsigned char *)s->wall : (const unsigned char *)s->profile, *y = dw ? (const unsigned char *)ls->wall : (const unsigned char *)ls->profile;
                told[k >> 3] |= (unsigned char)(1 << (k & 7)); for (b = 0; b < (int)(dw ? sizeof s->wall : sizeof s->profile); b++) nb += x[b] != y[b];
                LOG("world3d live: piece %d half %d: the game's %s differs from the course file's (%d bytes)%s", k, half, dw ? "wall corners" : "terrain profile", nb, getenv("RR_W3LIVEDECO") ? " - copied" : " - the file's kept"); } }
            if (getenv("RR_W3LIVEDECO")) {
                if (dw) { memcpy(s->wall, ls->wall, sizeof s->wall); changed = 1; }
                if (dp) { memcpy(s->profile, ls->profile, sizeof s->profile); changed = 1; } }
        }
    }
    half = g_vwHold ? 0 : 2 - (g_w3camB != 0);   /* the live stretch leaves out the carriageway the camera is not on where it is far away (course3d.h C3_HideHalf); the editor's viewer: neither */
    if (half != g_w3l.hide) { g_w3l.hide = half; changed = 1; }
    if (g_w3l.soft) {   /* forks on (Draw distance = Original): with a split in the stretch the other carriageway is placed round the camera every frame */
        for (i = lo; i < hi && g_w3c.piece[i].layout != 2; i++) {}
        if (i < hi) { int vp = g_w3cam.viewPos - g_w3Piece0;
            C3_SoftFrames(&g_w3l, &g_w3c, g_w3camB, vp >> 8, (vp & 255) / 256.0, g_w3ring, 2 * W3Far()); changed = 1;
            if (getenv("RR_W3XOTHER")) { if (*getenv("RR_W3XOTHER") == '2') g_w3l.tintFr = g_w3camB ? g_w3l.frame : g_w3l.frameB; else if (*getenv("RR_W3XOTHER") == '3') g_w3l.tintFr = g_w3camB ? g_w3l.frameB : g_w3l.frame; else memset(g_w3l.other, 0, g_w3c.n); }   /* (test: 1 = no other carriageway, 2 = drawn red) */
            if (getenv("RR_W3LOGS")) { static int ns; if (++ns % atoi(getenv("RR_W3LOGS")) == 0) {   /* test switch RR_W3LOGS=<k>: the other carriageway every k frames */
                int k = vp >> 8, j, no = 0, nr = 0, first = -1, last = -1; char s[400]; s[0] = 0;
                for (j = lo; j < hi; j++) { if (g_w3l.other[j]) { no++; if (first < 0) first = j; last = j; } if (g_w3c.piece[j].layout == 2 && g_w3ring[j]) nr++; }
                for (j = k; j < k + 12 && j < g_w3c.n; j += 2) { double dx = g_w3l.frame[j].x - g_w3l.frameB[j].x, dz = g_w3l.frame[j].z - g_w3l.frameB[j].z;
                    _snprintf(s + strlen(s), sizeof s - strlen(s) - 1, " %d:%s%d/%.0f/dy%.0f", j, g_w3ring[j] ? "r" : "-", g_w3l.piece[j].splitX, sqrt(dx * dx + dz * dz), g_w3l.frame[j].y - g_w3l.frameB[j].y); }
                LOG("w3soft cam %d+%d camX %d camB %d offs %d %d stretch %d..%d: other %d (%d..%d), ring split %d;%s", k, vp & 255, g_w3cam.camX, g_w3camB, g_w3off, g_w3offB, lo, hi, no, first, last, nr, s);
                for (j = k; j < k + 6 && j + 1 < g_w3c.n; j++) { const C3Frame *fa = &g_w3l.frame[j], *fb = &g_w3l.frameB[j];
                    LOG("w3soft  %d other %d hide %d: A %.0f %.0f %.0f h %.3f | B %.0f %.0f %.0f h %.3f", j, g_w3l.other[j], C3_HideHalf(&g_w3l, j, 0), fa->x, fa->y, fa->z, fa->heading, fb->x, fb->y, fb->z, fb->heading); }
                if (k >= 0 && k < g_w3c.n) { const RoadSection *a = &g_w3l.piece[k].halfA, *b = &g_w3l.piece[k].halfB, *fa = &g_w3c.piece[k].halfA;
                    LOG("w3soft  A edges %d %d lanes %d %d outer %d %d kind %d (file %d %d / %d %d) B edges %d %d outer %d %d kind %d; y A %.0f B %.0f", a->edgeL, a->edgeR, a->laneEdgeL, a->laneEdgeR, a->outerL, a->outerR, a->kind,
                        fa->edgeL, fa->edgeR, fa->outerL, fa->outerR, b->edgeL, b->edgeR, b->outerL, b->outerR, b->kind, g_w3l.frame[k].y, g_w3l.frameB[k].y); } } } } }
    if (changed || lo != g_w3lo || hiNeed > g_w3hi || g_w3lo < 0) {
        int nb; LARGE_INTEGER t0, t1, fq; static double sum; static int nr, nch;
        QueryPerformanceCounter(&t0);
        g_w3l.skirtEnd = lo + RRW_LIVE + q; g_w3l.nv = 0; C3_BuildRange(&g_w3l, lo, hi, &g_w3a);        nb = C3_Batches(&g_w3l, lb, 4096);
        W3GpuBatches(lb, nb, g_w3lb); g_w3lnb = nb;
        W3HydHideLive(); R3_SetLive(&g_w3r, (const R3Vert *)g_w3l.v, g_w3l.nv);   /* (broken hydrants stay gone) */
        g_w3lo = lo; g_w3hi = hi; W3GrassLive();   /* (grass on the live ground) */
        g_w3snb = 0;   /* (no static batches: the one mesh reaches the draw distance - above) */
        QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq); sum += (double)(t1.QuadPart - t0.QuadPart) / fq.QuadPart; nr++;
        if (changed) nch++;
        if (getenv("RR_W3LOG") && nr % 100 == 0) LOG("world3d live: %d rebuilds (%d for a change), %.2f ms each, %d vertices, %d batches", nr, nch, sum * 1000 / nr, g_w3l.nv, nb);
    } }

/* ---- lights (weather.inc WxDark: how much they show; police lights always): the course's lamps (sprite textures LAMPPST* / STRTLTE*) and parked police cars
   (COPCAR*), found once among the built billboards.  Each frame the nearest ones light the world (render3d.h point lights) and every one in sight gets a halo
   (a billboard added to the picture, batch alpha 3); the bike's headlight and a chasing police rider (RRO_COP) light it too ---- */
static float WxDark(void); static double WxSeconds(void); static void WxWorld(R3Frame *f); static float WxSunShadow(float *bear, float *len); static void WxView(double hd); static int WxEnv(const char *n, int v, int lo, int hi); static float WxSunDir(const float *fwd, const float *right, float *dir);   /* weather.inc */
typedef struct W3Lamp { float x, y, z, gox, goy, w; int piece, cop; } W3Lamp;
static W3Lamp *g_w3lamp; static int g_w3nlamp;
static int W3LampKind(const char *nm)   /* street lamps (LAMPPST, LMPPST3 / LMPPSTC, STRTLTE), parking-lot lamps (PRKGLMP); parked police cars (COPCAR) */
{   if (!strncmp(nm, "LAMPPST", 7) || !strncmp(nm, "LMPPST", 6) || !strncmp(nm, "STRTLTE", 7) || !strncmp(nm, "PRKGLMP", 7)) return 1;
    if (!strncmp(nm, "COPCAR", 6)) return 2; return 0; }
static int W3LampCmp(const void *a, const void *b) { return ((const W3Lamp *)a)->piece - ((const W3Lamp *)b)->piece; }
static void W3Bulb(int t, float *bu, float *bv)   /* the lamp's lens in its picture: the brightest pixels of the top 45 % (texture fractions) */
{   RxTex *x = &g_w3a.tex[t]; unsigned *p = RX_Rgba(&g_w3a, x); int X, Y, hy = x->h * 45 / 100, best = 0; double sx = 0, sy = 0, n = 0;
    *bu = 0.5f; *bv = 0.1f;
    if (!p) return;
    for (Y = 0; Y < hy; Y++) for (X = 0; X < x->w; X++) { unsigned c = p[Y * x->w + X]; int l = (c & 255) + ((c >> 8) & 255) + ((c >> 16) & 255); if ((c >> 24) >= 128 && l > best) best = l; }
    for (Y = 0; Y < hy; Y++) for (X = 0; X < x->w; X++) { unsigned c = p[Y * x->w + X]; int l = (c & 255) + ((c >> 8) & 255) + ((c >> 16) & 255); if ((c >> 24) >= 128 && l >= best - 40) { sx += X; sy += Y; n++; } }
    if (n) { *bu = (float)((sx / n + 0.5) / x->w); *bv = (float)((sy / n + 0.5) / x->h); }
    free(p); }
static void W3Lamps(void)
{   int i = 0, j, t, kind, cap = 0; float *bu = (float *)calloc(g_w3a.ntex + 1, sizeof(float) * 2); char *have = (char *)calloc(g_w3a.ntex + 1, 1);
    free(g_w3lamp); g_w3lamp = NULL; g_w3nlamp = 0;
    if (!bu || !have) { free(bu); free(have); return; }
    while (i < g_w3c.nv) {
        const C3Vert *v = &g_w3c.v[i]; float oxa = 1e9f, oxb = -1e9f, oya = 1e9f, oyb = -1e9f, uL = 0; W3Lamp *L;
        t = (int)v->tex;
        if ((v->ox == 0 && v->oy == 0) || t < 0 || t >= g_w3a.ntex || !(kind = W3LampKind(g_w3a.tex[t].name))) { i++; continue; }
        for (j = i; j < g_w3c.nv && j < i + 6 && g_w3c.v[j].x == v->x && g_w3c.v[j].y == v->y && g_w3c.v[j].z == v->z && (int)g_w3c.v[j].tex == t; j++) {   /* one billboard (C3_Billboard) */
            const C3Vert *q = &g_w3c.v[j];
            if (q->ox < oxa) { oxa = q->ox; uL = q->u; } if (q->ox > oxb) oxb = q->ox; if (q->oy < oya) oya = q->oy; if (q->oy > oyb) oyb = q->oy; }
        if (!have[t]) { have[t] = 1; if (kind == 1) W3Bulb(t, &bu[2 * t], &bu[2 * t + 1]); else { bu[2 * t] = 0.5f; bu[2 * t + 1] = 0.12f; } }
        if (g_w3nlamp == cap) { W3Lamp *nl = (W3Lamp *)realloc(g_w3lamp, sizeof(W3Lamp) * (cap = cap ? cap * 2 : 256)); if (!nl) break; g_w3lamp = nl; }
        L = &g_w3lamp[g_w3nlamp++];
        L->x = v->x; L->y = v->y; L->z = v->z; L->w = oxb - oxa; L->cop = kind == 2; L->piece = g_w3c.vpiece ? g_w3c.vpiece[i] : 0;
        L->gox = oxa + (oxb - oxa) * (uL < 0.5f ? bu[2 * t] : 1 - bu[2 * t]); L->goy = oyb - (oyb - oya) * bu[2 * t + 1];
        i = j;
    }
    if (getenv("RR_W3LOGT")) {   /* test switch RR_W3LOGT=1: every billboard picture of the course, how often, its size */
        int *cnt = (int *)calloc(g_w3a.ntex + 1, sizeof(int)); if (cnt) { char ln[400]; int k, n = 0;
            for (j = 0; j < g_w3c.nv; j++) { t = (int)g_w3c.v[j].tex; if ((g_w3c.v[j].ox != 0 || g_w3c.v[j].oy != 0) && t >= 0 && t < g_w3a.ntex) cnt[t]++; }
            ln[0] = 0; for (k = 0; k < g_w3a.ntex; k++) if (cnt[k]) { _snprintf(ln + strlen(ln), sizeof ln - strlen(ln) - 1, " %s:%d(%dx%d)", g_w3a.tex[k].name, cnt[k], g_w3a.tex[k].w, g_w3a.tex[k].h);
                if (strlen(ln) > 300 || ++n % 12 == 0) { LOG("w3tex%s", ln); ln[0] = 0; } }
            if (ln[0]) LOG("w3tex%s", ln); free(cnt); }
        {   int np = 0, last = -1000; char ln[400]; ln[0] = 0;   /* the road patches laid flat (course3d.h C3_Patch: vertex alpha 243..245), where */
            for (j = 0; j < g_w3c.nv; j++) { unsigned a = g_w3c.v[j].rgba >> 24; if (a >= 0xf3 && a <= 0xf5 && g_w3c.vpiece[j] != last) { last = g_w3c.vpiece[j]; np++; if (strlen(ln) < 300) _snprintf(ln + strlen(ln), sizeof ln - strlen(ln) - 1, " %d(%u)", last, a); } }
            LOG("w3patches %d:%s", np, ln); } }
    if (getenv("RR_W3TEXDUMP")) {   /* test switch RR_W3TEXDUMP=<folder>: the course's tree / bush / lamp pictures as .bmp (alpha 0 = magenta) */
        int k2; for (k2 = 0; k2 < g_w3a.ntex; k2++) { const RxTex *x = &g_w3a.tex[k2]; unsigned *px2; char fn[MAX_PATH]; FILE *o; int X, Y, gi, glob = -1;
            for (gi = 0; gi < 64; gi++) if (g_w3a.global[gi] == k2) glob = gi;   /* (and the road's own pictures: TEXG entries, named g<entry>_) */
            if (glob >= 0) { _snprintf(fn, sizeof fn, "%s\\g%02d_%s_%d.bmp", getenv("RR_W3TEXDUMP"), glob, x->name, k2); fn[sizeof fn - 1] = 0; }
            if (glob < 0 && strncmp(x->name, "TREE", 4) && strncmp(x->name, "BUSH", 4) && strncmp(x->name, "LAMPPST", 7) && strncmp(x->name, "LMPPST", 6) && strncmp(x->name, "PRKGLMP", 7) && strncmp(x->name, "STRTLTE", 7)) continue;
            if (!(px2 = RX_Rgba(&g_w3a, (RxTex *)x))) continue;
            if (glob < 0) { _snprintf(fn, sizeof fn, "%s\\%s_%d.bmp", getenv("RR_W3TEXDUMP"), x->name, k2); fn[sizeof fn - 1] = 0; }
            if ((o = fopen(fn, "wb"))) { BITMAPFILEHEADER bh; BITMAPINFOHEADER ih; memset(&bh, 0, sizeof bh); memset(&ih, 0, sizeof ih);
                bh.bfType = 0x4d42; bh.bfOffBits = sizeof bh + sizeof ih; bh.bfSize = bh.bfOffBits + x->w * x->h * 4; ih.biSize = sizeof ih; ih.biWidth = x->w; ih.biHeight = x->h; ih.biPlanes = 1; ih.biBitCount = 32;
                fwrite(&bh, sizeof bh, 1, o); fwrite(&ih, sizeof ih, 1, o);
                for (Y = x->h - 1; Y >= 0; Y--) for (X = 0; X < x->w; X++) { unsigned c = px2[Y * x->w + X], b = (c >> 24) < 128 ? 0xff00ff : (((c & 255) << 16) | (c & 0xff00) | ((c >> 16) & 255)); fwrite(&b, 4, 1, o); }
                fclose(o); }
            free(px2); } }
    free(bu); free(have);
    if (g_w3nlamp) qsort(g_w3lamp, g_w3nlamp, sizeof(W3Lamp), W3LampCmp);
    {   int j, lo = -1, hi = -1, nw = 0; for (j = 0; j < g_w3c.nv; j++) if ((g_w3c.v[j].rgba >> 24) == 0xfd) { int pc = g_w3c.vpiece ? g_w3c.vpiece[j] : 0; if (lo < 0 || pc < lo) lo = pc; if (pc > hi) hi = pc; nw++; }
        if (nw) LOG("world3d: water %d vertices, pieces %d..%d", nw, lo, hi); }
    LOG("world3d: %d lights (lamps, parked police cars), pieces %d..%d", g_w3nlamp, g_w3nlamp ? g_w3lamp[0].piece : -1, g_w3nlamp ? g_w3lamp[g_w3nlamp - 1].piece : -1); }
static int g_w3glow = -1, g_w3glowGen = -1;
static int W3GlowTex(void)   /* the halo picture (white, soft round; made again after the race start drops the renderer's textures) */
{   if (g_w3glowGen != g_w3texGen) { unsigned px[64 * 64]; int x, y;
        for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) { float dx = (x + 0.5f) / 32 - 1, dy = (y + 0.5f) / 32 - 1, d = dx * dx + dy * dy, a = d < 1 ? (float)(0.7 * exp(-d * 14) + 0.3 * exp(-d * 3)) * (1 - d) : 0;
            px[y * 64 + x] = ((unsigned)(a * 255) << 24) | 0xFFFFFFu; }
        g_w3glow = R3_Texture(&g_w3r, px, 64, 64); g_w3glowGen = g_w3texGen; }
    return g_w3glow; }
/* ---- grass (user, 2026-11-14: "if its green terrain can you put grass foliage"): tufts of grass blades as small billboards on the flat green ground.
   W3GrassInit (course load) collects the triangles: fixed geometry, unmarked (vertex alpha 255: not the road / footpath / snow / water), facing up,
   green (their colour, or their texture's average colour) - with their texture coordinates and shade.  W3Grass (every frame) scatters tufts on them
   out to the draw distance (W3Far, as the world fades into the sky) at hashed places (the same every frame: they stay put): ~1 per W3_GRCELL^2 units
   near the eye, beyond W3_GRNEAR fewer (1 / distance) and bigger.  A tuft takes the colour of the ground texel under it (times the ground's shade),
   so it matches the terrain; the blade picture is grey (light tips, dark roots).  Units: a lane ~250 wide.  Test switch RR_W3NOGRASS=1: none ---- */
static unsigned W3Rgba(float r, float g, float b, float a);   /* (below) */
static double W3Far(void);
#define W3_GRCELL 38.7f   /* (40 % fewer than 30) */
#define W3_GRNEAR 3000.0f   /* (user: "increase the distance ... its barely visible": was 900, with 1 / d^2 beyond) */
#define W3_GRMAX 600000   /* (user: "cap the tuffs please 20k", then "increase the tuff limit": the pop-in near the camera; Grass Extreme - 60000 - removed again;
                             then "increase the tuff count abit please 40%": 35000 -> 49000; 100000 tried, "too high"; then with the road strip "increase the cap by 400k":
                             449000 - g_grCap, the F10 panel's; this is the buffer's room, its most) */
/* True 3D detail > Effects distance: how far every distance-limited effect reaches - the grass (and how far it stays thick), the rooms behind the
   windows, their depth, the walls' relief, rain on the road (render3d.h opt.z) - all alike (user, 2026-11-15: "the same distance limit applied not
   individually").  Near / medium / far = 15 / 30 / 55 % of the draw distance (Extreme, 97 %, removed again: user, 2026-11-15) */
static double W3FxDist(void) { static const double fr[3] = { 0.15, 0.3, 0.55 }; return W3Far() * fr[g_vid.gdist < 0 ? 0 : g_vid.gdist > 2 ? 2 : g_vid.gdist]; }
/* what the camera can see this frame (user, 2026-11-15: "it shouldnt be rendered if they cant be seen"): the view's planes from its view-projection
   (W3Draw: W3ViewPlanes(f.vp)), and W3InView(x, y, z, r) = 1 when a sphere of radius r there reaches into the view.  The per-frame effects - grass,
   tree / lamp shadows, light halos, broken hydrants, drops and blood - make nothing for what is out of it */
static float g_w3pl[5][4]; static int g_w3plOk; static float g_w3fpxT = 1000;   /* (+ the focal length in the 3D picture's pixels) */
static void W3ViewPlanes(const float *vp)
{   int i, q; for (i = 0; i < 5; i++) {   /* left, right, bottom, top (w +- x, y), near (w - z: reversed depth, render3d.h R3_Persp); row vectors: clip = v * vp; inside >= 0 */
        int c = i < 2 ? 0 : i < 4 ? 1 : 2; float sg = (i & 1) || i == 4 ? -1.0f : 1.0f, l;
        for (q = 0; q < 4; q++) g_w3pl[i][q] = vp[q * 4 + 3] + sg * vp[q * 4 + c];
        l = sqrtf(g_w3pl[i][0] * g_w3pl[i][0] + g_w3pl[i][1] * g_w3pl[i][1] + g_w3pl[i][2] * g_w3pl[i][2]); if (l > 0) for (q = 0; q < 4; q++) g_w3pl[i][q] /= l; }
    g_w3plOk = 1; }
static int W3InView(float x, float y, float z, float r)
{   int i; if (!g_w3plOk) return 1;
    for (i = 0; i < 5; i++) if (g_w3pl[i][0] * x + g_w3pl[i][1] * y + g_w3pl[i][2] * z + g_w3pl[i][3] < -r) return 0;
    return 1; }
typedef struct W3GrTri { float a[3], b[3], c[3]; float ua, va, ub, vb, uc, vc; float area; int tex; unsigned rgba; float lat; int dry, piece; float fx, fz, fc, fs, eL, eR; } W3GrTri;   /* fx fz fc fs: its road piece's centre and heading (cos, sin): a point's distance from the centre line */   /* tex < 0: rgba = the colour; else rgba = the shade */
static W3GrTri *g_w3gr; static int g_w3ngr, g_w3grTex = -1, g_w3grGen = -1;
static unsigned **g_w3grPx; static int g_w3grNpx;   /* the green textures' pixels (ABGR), per course texture - blurred (W3GrBlur): the tufts' colours */
/* a green ground texture's pixels made the average of their 5 x 5 neighbourhood (wrapping; see-through ones left out), once: a tuft takes ONE pixel of
   it - the same colour at every distance (user, 2026-11-15: "the tuff changes colour the closer i ride to it": it had been 9 pixels near, 1 at mid
   range and the whole texture's average far off).  Frees px, returns the blurred copy (px itself if out of memory) */
static unsigned *W3GrBlur(unsigned *px, int w, int h)
{   unsigned *o; int x, y, dx, dy;
    if (!px || w <= 0 || h <= 0 || !(o = (unsigned *)malloc(sizeof(unsigned) * w * h))) return px;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        unsigned sr = 0, sg = 0, sb = 0, n = 0;
        for (dy = -2; dy <= 2; dy++) for (dx = -2; dx <= 2; dx++) { unsigned c = px[((y + dy + h) % h) * w + (x + dx + w) % w]; if (!(c >> 24)) continue; sr += c & 255; sg += (c >> 8) & 255; sb += (c >> 16) & 255; n++; }
        o[y * w + x] = n ? (sr / n) | ((sg / n) << 8) | ((sb / n) << 16) | 0xff000000u : 0; }
    free(px); return o; }
static int W3GrassTex(void)   /* a clump of blades: grey, dark at the root, light at the tips (the ground's colour comes per tuft), alpha cut-out */
{   if (g_w3grGen != g_w3texGen) { static unsigned px[64 * 128]; int x, y, k; unsigned s = 12345u;
        memset(px, 0, sizeof px);
        for (k = 0; k < 46; k++) {   /* blades: from the bottom, leaning a random way, curving, tapering */
            float x0, lean, curve, hgt, w0; s = s * 1103515245u + 12345u; x0 = 18 + (s >> 16) % 92; s = s * 1103515245u + 12345u; lean = ((int)((s >> 16) % 200) - 100) / 100.0f * 26;
            s = s * 1103515245u + 12345u; curve = ((int)((s >> 16) % 200) - 100) / 100.0f * 12; s = s * 1103515245u + 12345u; hgt = 30 + (s >> 16) % 33; w0 = 2.2f + (k % 3) * 0.6f;
            for (y = 0; y < (int)hgt; y++) { float t = y / hgt, cx = x0 + lean * t + curve * t * t, hw = w0 * (1 - t) + 0.35f; int xa = (int)floorf(cx - hw), xb = (int)ceilf(cx + hw), yy = 63 - y;
                for (x = xa; x <= xb; x++) { int sh; if (x < 0 || x >= 128 || yy < 0) continue; sh = (int)(150 + 105 * t) - (x < cx ? 12 : 0) + (k % 4) * 5; if (sh > 255) sh = 255;
                    px[yy * 128 + x] = 0xFF000000u | ((unsigned)sh << 16) | ((unsigned)sh << 8) | (unsigned)sh; } } }   /* (grey: the tuft's colour is the ground's) */
        g_w3grTex = R3_Texture(&g_w3r, px, 128, 64); g_w3grGen = g_w3texGen; }
    return g_w3grTex; }
static int W3IsGreen(unsigned rgba) { int r = rgba & 255, g = (rgba >> 8) & 255, b = (rgba >> 16) & 255; return g > 40 && g >= r * 0.93 && g > b * 1.15; }   /* (ABGR; green or dry yellow-green grass, not brown dirt) */
static unsigned char *g_w3texGreen;   /* per course texture: 2 = not looked at yet, 1 = green ground, 0 = not */
static W3GrTri *g_w3grL; static int g_w3ngrL, g_w3grLcap;   /* the live stretch's ground triangles (W3GrassLive) */
/* the ground triangles grass grows on of course mesh cc (the static one, or the live stretch round the camera - its ground is the game's own and can
   lie higher or lower than the file's: user, "the tuft isnt following the terrain properly") */
/* a big ground triangle (an edge over W3_GRSPLIT) in four (W3GrAdd): the grass's list holds small triangles only (user, 2026-11-15: "can we not draw
   big triangles and just draw small triangles") - each culled on its own, the far ground's huge ones no longer tried tuft by tuft.  0 = small enough */
#define W3_GRSPLIT 2000.0f
static int W3GrSplit(const W3GrTri *T, W3GrTri *out)
{   float e0 = (T->b[0] - T->a[0]) * (T->b[0] - T->a[0]) + (T->b[2] - T->a[2]) * (T->b[2] - T->a[2]), e1 = (T->c[0] - T->b[0]) * (T->c[0] - T->b[0]) + (T->c[2] - T->b[2]) * (T->c[2] - T->b[2]),
          e2 = (T->a[0] - T->c[0]) * (T->a[0] - T->c[0]) + (T->a[2] - T->c[2]) * (T->a[2] - T->c[2]), ab[5], bc[5], ca[5], A5[5], B5[5], C5[5];   /* (x y z u v of the corners and the edges' middles) */
    int k, j;
    if (e0 <= W3_GRSPLIT * W3_GRSPLIT && e1 <= W3_GRSPLIT * W3_GRSPLIT && e2 <= W3_GRSPLIT * W3_GRSPLIT) return 0;
    for (k = 0; k < 3; k++) { A5[k] = T->a[k]; B5[k] = T->b[k]; C5[k] = T->c[k]; }
    A5[3] = T->ua; A5[4] = T->va; B5[3] = T->ub; B5[4] = T->vb; C5[3] = T->uc; C5[4] = T->vc;
    for (k = 0; k < 5; k++) { ab[k] = (A5[k] + B5[k]) / 2; bc[k] = (B5[k] + C5[k]) / 2; ca[k] = (C5[k] + A5[k]) / 2; }
    {   const float *t4[4][3] = { { A5, ab, ca }, { ab, B5, bc }, { ca, bc, C5 }, { ab, bc, ca } };
        for (k = 0; k < 4; k++) { W3GrTri *q = &out[k]; *q = *T; q->area = T->area / 4;
            for (j = 0; j < 3; j++) { q->a[j] = t4[k][0][j]; q->b[j] = t4[k][1][j]; q->c[j] = t4[k][2][j]; }
            q->ua = t4[k][0][3]; q->va = t4[k][0][4]; q->ub = t4[k][1][3]; q->vb = t4[k][1][4]; q->uc = t4[k][2][3]; q->vc = t4[k][2][4]; } }
    return 1; }
/* the grass's tunables - the F10 panel in a True 3D race changes them live (user, 2026-11-15: "give me options to manipulate the tuffs please distance,
   density etc, if i push f10"; W3GrPanel) */
static float g_grShare = 0.10f, g_grStrip = 0.3f, g_grStripW = 2250, g_grFull = 0, g_grFall = 600, g_grReach = 0.35f, g_grSize = 3.0f, g_grMinPx = 1.0f, g_grMip = 0.6f, g_grCap = 8000;
/* (the defaults: the user's own from the F10 panel, 2026-11-15 "These are the settings i want, can you default the game to these settings please. can you
   cap 15k please"; then shot_20261115_025517_197 - reach 0.35, size 3, cap 8000 - "make it default for the High mode") */
static int g_grLast; static float g_grLastMs;   /* (the last frame's tufts and CPU time: the panel's figures) */
/* True 3D detail > Grass density: Off / Low / Medium / High - the panel's values are High's, the others scaled from them (user, 2026-11-15: "take
   shot_20261115_025517_197 as high and then ramp it down ... just calculate it dynamically"): spots, strip and cap x 1 / 0.6 / 0.3, the reach x 1 / 0.8 / 0.65 */
static float W3GrLevel(void) { static const float k[4] = { 0, 0.3f, 0.6f, 1.0f }; return k[g_vid.grass & 3]; }
static float W3GrSide(float lat);   /* (below) */
/* one ground triangle into the grass's list: big ones in small parts (W3GrSplit), the parts wholly outside the grass's strip by the road (W3GrSide 0)
   left out - the strip stays a few thousand small triangles, the far fields none */
static void W3GrAdd(W3GrTri **arr, int *nn, int *capp, W3GrTri *T, int depth)
{   float l0 = (T->a[0] - T->fx) * T->fc - (T->a[2] - T->fz) * T->fs, l1 = (T->b[0] - T->fx) * T->fc - (T->b[2] - T->fz) * T->fs, l2 = (T->c[0] - T->fx) * T->fc - (T->c[2] - T->fz) * T->fs;
    float latMin = (l0 > 0) == (l1 > 0) && (l1 > 0) == (l2 > 0) ? fminf(fabsf(l0), fminf(fabsf(l1), fabsf(l2))) : 0; W3GrTri q[4]; int k;
    if (latMin > 9000) return;   /* (well past the grass, whatever the F10 panel sets) */
    if (depth < 8 && W3GrSplit(T, q)) { for (k = 0; k < 4; k++) W3GrAdd(arr, nn, capp, &q[k], depth + 1); return; }
    {   float mx = (T->a[0] + T->b[0] + T->c[0]) / 3, mz = (T->a[2] + T->b[2] + T->c[2]) / 3; T->lat = (float)fabs((mx - T->fx) * T->fc - (mz - T->fz) * T->fs); }
    if (*nn == *capp) { W3GrTri *n2 = (W3GrTri *)realloc(*arr, sizeof(W3GrTri) * (*capp = *capp ? *capp * 2 : 4096)); if (!n2) return; *arr = n2; }
    (*arr)[(*nn)++] = *T; }
static void W3GrassCollect(const C3Course *cc, W3GrTri **arr, int *nn, int *capp)
{   int i;
    for (i = 0; i + 2 < cc->nv; i += 3) {
        const C3Vert *A = &cc->v[i], *B = &cc->v[i + 1], *C = &cc->v[i + 2]; float u[3], v[3], n[3], l; int t = (int)A->tex, green,
            dry = (A->rgba >> 24) == 0xef && (B->rgba >> 24) == 0xef && (C->rgba >> 24) == 0xef;   /* (dirt, course3d.h C3_TexDirt: sparse dry scrub) */
        if (A->ox != 0 || A->oy != 0 || B->ox != 0 || B->oy != 0 || (!dry && ((A->rgba >> 24) != 0xff || (B->rgba >> 24) != 0xff || (C->rgba >> 24) != 0xff))) continue;
        u[0] = B->x - A->x; u[1] = B->y - A->y; u[2] = B->z - A->z; v[0] = C->x - A->x; v[1] = C->y - A->y; v[2] = C->z - A->z;
        n[0] = u[1] * v[2] - u[2] * v[1]; n[1] = u[2] * v[0] - u[0] * v[2]; n[2] = u[0] * v[1] - u[1] * v[0]; l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l < 1e-3f || fabsf(n[1]) / l < 0.62f) continue;   /* (flat enough to grow on: up to ~50 degrees) */
        if (t < 0 && l * 0.5f < 400) continue;   /* (no tufts on the fallen leaves round the city's tree pits - user, 2026-11-15: course3d.h C3_Leaves, flat green
                                                     untextured diamonds of ~135 units at most; ground is never a flat-colour triangle smaller than one tuft) */
        if (t >= 0 && t < g_w3a.ntex && g_w3a.tex[t].edgeOf) continue;   /* (a torn blend edge, course3d.h C3_Blend: laid over ground that has its own tufts - its spots the same, each
                                                                          tuft twice, a little higher and another colour, fighting for the depth: user, 2026-11-19 "the grass tuffs is overlapping") */
        if (dry) green = 1;
        else if (t < 0 || t >= g_w3a.ntex) green = W3IsGreen(A->rgba);
        else {
            if (g_w3texGreen && g_w3texGreen[t] == 2) {   /* the texture's average colour, once; a green one's pixels kept for the tufts' colours */
                RxTex *x = &g_w3a.tex[t]; unsigned *rgba = RX_Rgba(&g_w3a, x); double r = 0, g = 0, b = 0; int k, n2 = 0;
                g_w3texGreen[t] = 0;
                if (rgba) { for (k = 0; k < x->w * x->h; k += 3) if (rgba[k] >> 24) { r += rgba[k] & 255; g += (rgba[k] >> 8) & 255; b += (rgba[k] >> 16) & 255; n2++; } }
                if (n2) g_w3texGreen[t] = (unsigned char)W3IsGreen((unsigned)(r / n2) | ((unsigned)(g / n2) << 8) | ((unsigned)(b / n2) << 16));
                if (g_w3texGreen[t] == 1 && g_w3grPx) g_w3grPx[t] = W3GrBlur(rgba, x->w, x->h); else free(rgba); }
            green = g_w3texGreen ? g_w3texGreen[t] == 1 : 0; }
        if (!green) continue;
        {   W3GrTri TT, *T = &TT; int pc = cc->vpiece ? cc->vpiece[i] : 0; const C3Frame *F;
            T->a[0] = A->x; T->a[1] = A->y; T->a[2] = A->z; T->b[0] = B->x; T->b[1] = B->y; T->b[2] = B->z; T->c[0] = C->x; T->c[1] = C->y; T->c[2] = C->z;
            T->ua = A->u; T->va = A->v; T->ub = B->u; T->vb = B->v; T->uc = C->u; T->vc = C->v; T->piece = pc;
            T->area = l * 0.5f; T->tex = (t >= 0 && t < g_w3a.ntex && g_w3grPx && g_w3grPx[t]) ? t : -1; T->rgba = A->rgba; T->dry = dry; if (dry) { T->tex = -1; T->rgba = 0xff527e9au; }
            F = g_w3c.frame ? &g_w3c.frame[pc < 0 ? 0 : pc >= g_w3c.n ? g_w3c.n - 1 : pc] : NULL;   /* (how far from the road's centre line: tufts thicker near it) */
            T->fx = F ? (float)F->x : 0; T->fz = F ? (float)F->z : 0; T->fc = F ? (float)cos(F->heading) : 0; T->fs = F ? (float)sin(F->heading) : 0;
            {   const RoadSection *rs = &cc->piece[pc < 0 ? 0 : pc >= cc->n ? cc->n - 1 : pc].halfA; T->eL = (float)rs->edgeL; T->eR = (float)rs->edgeR; }   /* (its piece's road edges: the strip beside them, W3Grass) */
            W3GrAdd(arr, nn, capp, T, 0); } }
}
/* the file's grass triangles sorted by road piece, g_w3grStart[k] = the first of piece k (k = 0 .. n): W3Grass looks only at the pieces in view - it had
   walked all of them five times a frame (the City: 85000, ~3 ms of CPU a frame; user, 2026-11-15: "is there something else taking up high cpu?") */
static int *g_w3grStart;
static int W3GrCmp(const void *a, const void *b) { return ((const W3GrTri *)a)->piece - ((const W3GrTri *)b)->piece; }
static void W3GrassIndex(void)
{   int k, i = 0;
    free(g_w3grStart); g_w3grStart = NULL;
    if (!g_w3ngr || !(g_w3grStart = (int *)malloc(sizeof(int) * (g_w3c.n + 2)))) return;
    qsort(g_w3gr, g_w3ngr, sizeof(W3GrTri), W3GrCmp);
    for (k = 0; k <= g_w3c.n + 1; k++) { while (i < g_w3ngr && g_w3gr[i].piece < k) i++; g_w3grStart[k] = i; } }
/* each grass triangle's tufts, worked out once as it comes into reach (user, 2026-11-21: "fix the grass please" - the spots of every grid cell under
   every triangle in reach had been hashed, tested and coloured again every frame, ~9 ms of CPU, 200 -> 70 fps on any grassy course): a tuft's place,
   threshold, density by the road, base size, shape and colour do not change - only what depends on the camera (distance, view, thinning, the
   2-pixel least size) is done per frame.  Per set (0 the file's triangles, 1 the live stretch's), per triangle: its spots (n -1 = not made yet);
   the file's freed once their pieces are out of reach, the live set's when the live stretch is built again, all when a grass setting changes */
#define W3_GRK 5   /* (spots per grid cell: W3GrSide gives at most 4 tufts a cell) */
typedef struct W3GrSp { float x, y, z, th, dens, sz, htf; unsigned rgba; int flip; } W3GrSp;
static W3GrSp **g_grSp[2]; static int *g_grSpN[2], g_grSpCap[2], g_grSpLo = 0, g_grSpHi = 0; static float g_grSpKey = -1;
static void W3GrSpFree(int set, int i) { if (i < g_grSpCap[set] && g_grSpN[set][i] >= 0) { free(g_grSp[set][i]); g_grSp[set][i] = NULL; g_grSpN[set][i] = -1; } }
static void W3GrSpFlush(int set) { int i; for (i = 0; i < g_grSpCap[set]; i++) W3GrSpFree(set, i); if (!set) g_grSpLo = g_grSpHi = 0; }
static int W3GrSpRoom(int set, int n)   /* room for n triangles' entries */
{   W3GrSp **a; int *c, i;
    if (n <= g_grSpCap[set]) return 1;
    if (!(a = (W3GrSp **)realloc(g_grSp[set], sizeof(W3GrSp *) * n))) return 0; g_grSp[set] = a;
    if (!(c = (int *)realloc(g_grSpN[set], sizeof(int) * n))) return 0; g_grSpN[set] = c;
    for (i = g_grSpCap[set]; i < n; i++) { a[i] = NULL; c[i] = -1; }
    g_grSpCap[set] = n; return 1; }
static float W3GrSide(float lat);   /* below */
static unsigned W3GrKey(const W3GrTri *T)   /* a triangle by its corners and texture (W3GrassLive: the same ground in the live stretch built again) */
{   const unsigned *w = (const unsigned *)T->a; unsigned h = 2166136261u; int i; for (i = 0; i < 9; i++) { h ^= w[i]; h *= 16777619u; } h ^= (unsigned)T->tex; h *= 16777619u; return h; }
static unsigned W3Rgba(float r, float g, float b, float a);   /* below */
static int W3GrSpots(int set, int i, const W3GrTri *T, float lowF, float share, const W3GrSp **out)   /* triangle i's tufts (made now if not yet); returns how many */
{   W3GrSp *sp = NULL; int n = 0, cap = 0, ix, iz, ix0, ix1, iz0, iz1, kk;
    float e1x = T->b[0] - T->a[0], e1z = T->b[2] - T->a[2], e2x = T->c[0] - T->a[0], e2z = T->c[2] - T->a[2], det = e1x * e2z - e2x * e1z, idet, dryF, densMax, latMin;
    *out = NULL;
    if (!W3GrSpRoom(set, i + 1)) return 0;
    if (g_grSpN[set][i] >= 0) { *out = g_grSp[set][i]; return g_grSpN[set][i]; }
    g_grSpN[set][i] = 0;
    if (fabsf(det) < 1e-3f) return 0; idet = 1 / det;
    {   float l0 = (T->a[0] - T->fx) * T->fc - (T->a[2] - T->fz) * T->fs, l1 = (T->b[0] - T->fx) * T->fc - (T->b[2] - T->fz) * T->fs, l2 = (T->c[0] - T->fx) * T->fc - (T->c[2] - T->fz) * T->fs;
        latMin = (l0 > 0) == (l1 > 0) && (l1 > 0) == (l2 > 0) ? fminf(fabsf(l0), fminf(fabsf(l1), fabsf(l2))) : 0; }   /* (its corner nearest the road: the most it holds) */
    dryF = T->dry ? (T->lat < 1100 || !g_vid.ground ? 0 : 0.15f) : 1;   /* (dirt: sparse dry scrub, none on the verge; Ground detail off: none) */
    densMax = W3GrSide(latMin) * dryF * lowF * share;
    if (!T->dry && latMin < fmaxf(fabsf(T->eL), fabsf(T->eR)) + g_grStripW && densMax < g_grStrip * lowF) densMax = g_grStrip * lowF;   /* (it reaches the strip by the road) */
    if (densMax <= 0) return 0;
    ix0 = (int)floorf(fminf(T->a[0], fminf(T->b[0], T->c[0])) / W3_GRCELL); ix1 = (int)floorf(fmaxf(T->a[0], fmaxf(T->b[0], T->c[0])) / W3_GRCELL);
    iz0 = (int)floorf(fminf(T->a[2], fminf(T->b[2], T->c[2])) / W3_GRCELL); iz1 = (int)floorf(fmaxf(T->a[2], fmaxf(T->b[2], T->c[2])) / W3_GRCELL);
    for (iz = iz0; iz <= iz1; iz++) for (ix = ix0; ix <= ix1; ix++) {
        unsigned hc = (unsigned)ix * 73856093u ^ (unsigned)iz * 19349663u;
        for (kk = 0; kk < W3_GRK; kk++) {
            unsigned h = hc ^ (unsigned)(kk + 1) * 2246822519u, col; float s1, s2, b1, b2, qx, qz, p[3], r, g, b, th, dens, sh;
            h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16;
            th = kk + ((h >> 22) & 1023) / 1024.0f;   /* (its threshold: drawn while the cell's tufts at its place are above it; rising with kk) */
            if (th >= densMax) break;   /* (this cell's other spots are higher still) */
            s1 = (h & 0x7ff) / 2048.0f; s2 = ((h >> 11) & 0x7ff) / 2048.0f;
            p[0] = (ix + s1) * W3_GRCELL; p[2] = (iz + s2) * W3_GRCELL;
            qx = p[0] - T->a[0]; qz = p[2] - T->a[2]; b1 = (qx * e2z - e2x * qz) * idet; b2 = (e1x * qz - qx * e1z) * idet;
            if (b1 < 0 || b2 < 0 || b1 + b2 > 1) continue;   /* (another triangle's spot) */
            p[1] = T->a[1] + (T->b[1] - T->a[1]) * b1 + (T->c[1] - T->a[1]) * b2;
            {   float ls = (p[0] - T->fx) * T->fc - (p[2] - T->fz) * T->fs, lat = fabsf(ls), de = fminf(fabsf(ls - T->eL), fabsf(ls - T->eR));
                dens = W3GrSide(lat) * dryF * lowF * share;   /* (thinner the further it is from the road) */
                if (!T->dry && de < g_grStripW && lat > fminf(fabsf(T->eL), fabsf(T->eR))) {   /* (the strip: thickest at the road's edge, fading out across its width -
                                                                                                  user, "keep the strip width but make it fall off alot earlier so it doesnt look so abrupt") */
                    float sd = g_grStrip * lowF * (de < 0.1f * g_grStripW ? 1.0f : expf(-(de - 0.1f * g_grStripW) / (0.18f * g_grStripW))); if (sd > dens) dens = sd; } }   /* (the strip just outside the road's edges: thick) */
            if (th >= dens) continue;   /* (never drawn: the per-frame thinning only lowers it) */
            sh = 0.85f + ((h >> 3) % 30) / 100.0f;
            if (T->tex >= 0) {   /* the (blurred: W3GrBlur) ground texel under the tuft, times the ground's shade - one look-up, the same at every distance */
                const RxTex *x = &g_w3a.tex[T->tex]; float uu = T->ua + (T->ub - T->ua) * b1 + (T->uc - T->ua) * b2, vv = T->va + (T->vb - T->va) * b1 + (T->vc - T->va) * b2; int tx, ty;
                uu -= floorf(uu); vv -= floorf(vv); tx = (int)(uu * x->w); ty = (int)(vv * x->h); if (tx >= x->w) tx = x->w - 1; if (ty >= x->h) ty = x->h - 1; if (tx < 0) tx = 0; if (ty < 0) ty = 0;
                col = g_w3grPx[T->tex][ty * x->w + tx]; if (!(col >> 24)) col = 0xff468c3cu;   /* (a see-through patch: a plain green) */
                r = (col & 255) / 255.0f * (T->rgba & 255) / 255.0f; g = ((col >> 8) & 255) / 255.0f * ((T->rgba >> 8) & 255) / 255.0f; b = ((col >> 16) & 255) / 255.0f * ((T->rgba >> 16) & 255) / 255.0f; }
            else { r = (T->rgba & 255) / 255.0f; g = ((T->rgba >> 8) & 255) / 255.0f; b = ((T->rgba >> 16) & 255) / 255.0f; }
            if (n == cap) { W3GrSp *ns = (W3GrSp *)realloc(sp, sizeof(W3GrSp) * (cap = cap ? cap * 2 : 16)); if (!ns) { cap = n; continue; } sp = ns; }
            {   W3GrSp *o = &sp[n++]; o->x = p[0]; o->y = p[1]; o->z = p[2]; o->th = th; o->dens = dens;
                o->sz = (20.0f + (h >> 7) % 16) * g_grSize; o->htf = 0.55f + ((h >> 11) % 30) / 100.0f; o->flip = h & 1;
                o->rgba = W3Rgba(r * 1.12f * sh, g * 1.12f * sh, b * 1.12f * sh, 237.0f / 255.0f); } } }   /* (the blade picture's mid grey brought back to the ground's brightness; vertex alpha 237: render3d.h - drawn behind the rails in front of them) */
    g_grSp[set][i] = sp; g_grSpN[set][i] = n; *out = sp; return n; }
static void W3GrassInit(void)
{   int i, cap = 0;
    free(g_w3gr); g_w3gr = NULL; g_w3ngr = 0; g_w3ngrL = 0;
    for (i = 0; i < g_w3grNpx; i++) free(g_w3grPx[i]); free(g_w3grPx); g_w3grPx = NULL; g_w3grNpx = 0; free(g_w3texGreen); g_w3texGreen = NULL;
    if (getenv("RR_W3NOGRASS")) return;
    if (g_w3a.ntex > 0 && (g_w3texGreen = (unsigned char *)calloc(g_w3a.ntex, 1)) != NULL) { memset(g_w3texGreen, 2, g_w3a.ntex); if ((g_w3grPx = (unsigned **)calloc(g_w3a.ntex, sizeof(unsigned *))) != NULL) g_w3grNpx = g_w3a.ntex; }
    W3GrSpFlush(0); W3GrSpFlush(1);   /* (the tufts worked out: another course's) */
    W3GrassCollect(&g_w3c, &g_w3gr, &g_w3ngr, &cap);
    W3GrassIndex();   /* (by road piece: each frame only the pieces in view are looked at) */
    LOG("world3d: grass on %d ground triangles", g_w3ngr); }
static void W3GrassLive(void)   /* the live stretch was built again (W3Live): its ground triangles */
{   int nOld = g_w3ngrL, i, j; unsigned *hk = NULL; W3GrSp **op = NULL; int *on = NULL;   /* (the tufts worked out handed on to the same triangles in the new stretch) */
    if (nOld > 0 && g_grSpCap[1] >= nOld && (hk = (unsigned *)malloc(sizeof(unsigned) * nOld)) != NULL && (op = (W3GrSp **)malloc(sizeof(W3GrSp *) * nOld)) != NULL && (on = (int *)malloc(sizeof(int) * nOld)) != NULL)
        for (i = 0; i < nOld; i++) { hk[i] = W3GrKey(&g_w3grL[i]); op[i] = g_grSp[1][i]; on[i] = g_grSpN[1][i]; g_grSp[1][i] = NULL; g_grSpN[1][i] = -1; }
    else W3GrSpFlush(1);
    g_w3ngrL = 0; if (g_w3ngr && g_w3l.v) W3GrassCollect(&g_w3l, &g_w3grL, &g_w3ngrL, &g_w3grLcap);
    if (on) {   /* (by key in a small open table; what is not found is made again when in reach, what is left over freed) */
        int tn = 1; int *tab; while (tn < nOld * 2) tn <<= 1;
        if ((tab = (int *)malloc(sizeof(int) * tn)) != NULL && W3GrSpRoom(1, g_w3ngrL > 0 ? g_w3ngrL : 1)) {
            for (j = 0; j < tn; j++) tab[j] = -1;
            for (i = 0; i < nOld; i++) if (on[i] >= 0) { j = hk[i] & (tn - 1); while (tab[j] >= 0) j = (j + 1) & (tn - 1); tab[j] = i; }
            for (i = 0; i < g_w3ngrL; i++) { unsigned k = W3GrKey(&g_w3grL[i]); j = k & (tn - 1);
                while (tab[j] >= 0) { int o = tab[j]; if (hk[o] == k && on[o] >= 0) { g_grSp[1][i] = op[o]; g_grSpN[1][i] = on[o]; on[o] = -1; break; } j = (j + 1) & (tn - 1); } } }
        for (i = 0; i < nOld; i++) if (on[i] >= 0) free(op[i]);
        free(tab); }
    free(hk); free(op); free(on);
    if (getenv("RR_W3LOGGR")) { static int n; if (n++ < 6) LOG("w3grass live %d..%d: %d triangles (static %d), live mesh %d vertices", g_w3lo, g_w3hi, g_w3ngrL, g_w3ngr, g_w3l.nv); } }   /* (test switch RR_W3LOGGR=1) */
/* this frame's tufts (returns the vertices written): on a fixed world grid (W3_GRCELL cells, W3_GRK spots each, hashed places and thresholds; the
   triangles only say which spots are on grass and how high the ground is there - the same tufts whichever triangles cover it).  A cell holds W3GrSide
   tufts by the road (thick on its sides, falling off steeply further out - user, "only on the sides of the roads"), dirt 0.15 of that and none on the
   verge, Grass Low 40 %, all times the share drawn - the same at every distance, adapted to the cap; a spot is drawn while that is above its threshold,
   full size (no growing in: user, "the grow effect is very distracting"), fewer towards the reach's end; one size near and far.  Triangles in bands
   of distance, nearest first (0, 600, 1300, 2400, 4000, the rest), so the cap never cuts the tufts at the camera's feet; the live stretch's triangles
   stand in for the file's there.  Only what the camera can see: a triangle wholly outside the view is skipped, and so is a tuft outside it (W3InView)
   - neither counts towards the cap (user, 2026-11-15: "exclude tuffs that the player cant see") */
/* (the strip along the road's edges on green ground - user, "a thin strip of tuff along sides of the road": g_grStrip tufts a cell, 3 then "3 times less
   dense"; g_grStripW wide outside each edge, 300 then "by double"; g_grShare: the part of the spots drawn elsewhere, where the cap had settled it) */
static float W3GrSide(float lat)   /* how thick the grass is by its distance from the road's centre line: on the sides of the road, falling off steeply further out
                                      - none past ~4000 (user, 2026-11-15: "only on the sides of the roads ... drop them off the further horizontally they are") */
{   float f = lat < g_grFull ? 4.0f : 4.0f * expf(-(lat - g_grFull) / (g_grFall > 1 ? g_grFall : 1)); return f < 0.03f ? 0 : f; }   /* (g_grFull, g_grFall: 1400, 550) */
static int W3Grass(R3Vert *o, int cap, const float *eye)
#define W3_GRNB 9   /* (distance bands, nearest first: what the cap leaves out is always the furthest) */
{   static const int o6[6] = { 0, 3, 1, 1, 3, 2 }; const float share = g_grShare; static float rThin = 1e9f;
    /* rThin: past it the tufts thin out as rThin / distance - only while the cap is reached (user, 2026-11-15: "it should reduce the tuffs from a
       distance if it goes beyond the cap"): it comes in a little each frame that fills the cap and goes out again once well under it (a dead band
       between, so nothing shifts while the count is comfortable); never nearer than 3000 */ int i, n = 0, tufts = 0, pass, set, nCull = 0; float maxD = 0, latMin, lowF = W3GrLevel();   /* (lowF: True 3D detail > Grass density - High 1, Medium, Low less) */
    /* share: the part of the grid's spots drawn - fixed (g_grShare), the same near and far and from frame to frame: a spot is drawn or not by where it
       is alone, so nothing fills in as the camera nears it (user, "disable that dense step") and nothing fades in or out as the view changes - it had
       been adapted to the cap every frame, and as the roadside in view changed with bends and hills the whole view thinned and thickened (user,
       2026-11-15: "why are they changing as im driving").  The cap (grMax) is a safety net only: the bands nearest first, the furthest cut */
    float D = (float)(W3FxDist() * g_grReach * (0.5f + 0.5f * lowF)); static const float band[W3_GRNB + 1] = { -1e9f, 600, 1300, 2400, 4000, 7000, 12000, 20000, 35000, 1e9f };   /* (filled nearest first: the cap never starves the ground at the camera's feet) */
    int grMax = (int)(g_grCap * lowF); if (grMax > W3_GRMAX) grMax = W3_GRMAX; if (!g_vid.grass) return 0;   /* (True 3D detail > Grass: off / low - 40 % of the tufts / high) */
    int bandN[W3_GRNB][2], nCand = 0, nTri = 0; memset(bandN, 0, sizeof bandN);
    {   float key = lowF * 1.7f + share * 3.1f + g_grStrip * 7.3f + g_grStripW * 0.0011f + g_grSize * 13.1f + g_grFull * 0.0017f + g_grFall * 0.0019f + (float)g_vid.ground * 17.3f;
        if (key != g_grSpKey) { W3GrSpFlush(0); W3GrSpFlush(1); g_grSpKey = key; } }   /* (a grass setting changed: the tufts worked out again) */
    if (g_w3grStart) { int kc = (g_w3cam.viewPos - g_w3Piece0) >> 8, lo = kc - 8, hi = kc + (int)(D / W3_L) + 4, a, b, k;   /* (the file's triangles in reach - as below; the ones that left it freed) */
        if (lo < 0) lo = 0; if (hi > g_w3c.n) hi = g_w3c.n; if (hi < lo) hi = lo; a = g_w3grStart[lo]; b = g_w3grStart[hi];
        for (k = g_grSpLo; k < g_grSpHi; k++) if (k < a || k >= b) W3GrSpFree(0, k);
        g_grSpLo = a; g_grSpHi = b; }
    {   /* the triangles in reach, once a frame, put in their distance band (where their nearest part is), nearest band first: so the cap leaves out
           the furthest - they had been walked once per band, 9 times a frame (~4 ms with the live stretch's 20000) */
        static int *cIdx, *cBand, *cOrd, cCap; int nc = 0, cnt[W3_GRNB + 1], k, b;
        for (set = 0; set < 2; set++) {
            const W3GrTri *arr = set ? g_w3grL : g_w3gr; int na = set ? (g_w3lo >= 0 ? g_w3ngrL : 0) : g_w3ngr, i0 = 0;
            if (!set && g_w3grStart) { int kc = (g_w3cam.viewPos - g_w3Piece0) >> 8, lo = kc - 8, hi = kc + (int)(D / W3_L) + 4;   /* (the file's triangles of the pieces in view only: 85000 in the City, a few thousand of them near) */
                if (lo < 0) lo = 0; if (hi > g_w3c.n) hi = g_w3c.n; if (hi < lo) hi = lo; i0 = g_w3grStart[lo]; na = g_w3grStart[hi]; }
            if (cCap < nc + (na - i0)) { int nn = (nc + na - i0) * 2 + 64; int *x1 = (int *)realloc(cIdx, sizeof(int) * nn), *x2, *x3; if (x1) cIdx = x1; x2 = (int *)realloc(cBand, sizeof(int) * nn); if (x2) cBand = x2; x3 = (int *)realloc(cOrd, sizeof(int) * nn); if (x3) cOrd = x3; if (!x1 || !x2 || !x3) return 0; cCap = nn; }
            for (i = i0; i < na; i++) { const W3GrTri *T = &arr[i]; float cx = (T->a[0] + T->b[0] + T->c[0]) / 3 - eye[0], cz = (T->a[2] + T->b[2] + T->c[2]) / 3 - eye[2], dc = sqrtf(cx * cx + cz * cz) - sqrtf(T->area);
                if (dc > D) continue;
                if (!set && g_w3lo >= 0 && T->piece >= g_w3lo && T->piece <= g_w3hi) continue;   /* (the live stretch's own ground instead) */
                for (b = 0; b < W3_GRNB - 1 && dc >= band[b + 1]; b++) ;
                cIdx[nc] = (set << 30) | i; cBand[nc] = b; nc++; } }
        memset(cnt, 0, sizeof cnt); for (k = 0; k < nc; k++) cnt[cBand[k] + 1]++;
        for (b = 0; b < W3_GRNB; b++) cnt[b + 1] += cnt[b];
        for (k = 0; k < nc; k++) cOrd[cnt[cBand[k]]++] = k;   /* (nearest band first, in the order found within a band) */
    for (k = 0; k < nc && tufts < grMax && n + 6 <= cap; k++) { int t0 = tufts, ci = cOrd[k]; const W3GrTri *arr; set = cIdx[ci] >> 30; i = cIdx[ci] & 0x3fffffff; pass = cBand[ci]; arr = set ? g_w3grL : g_w3gr;
      {   const W3GrTri *T = &arr[i]; float dryF, densMax;
        {   float l0 = (T->a[0] - T->fx) * T->fc - (T->a[2] - T->fz) * T->fs, l1 = (T->b[0] - T->fx) * T->fc - (T->b[2] - T->fz) * T->fs, l2 = (T->c[0] - T->fx) * T->fc - (T->c[2] - T->fz) * T->fs;
            latMin = (l0 > 0) == (l1 > 0) && (l1 > 0) == (l2 > 0) ? fminf(fabsf(l0), fminf(fabsf(l1), fabsf(l2))) : 0; }   /* (its corner nearest the road: the most it holds; each tuft its own share below) */
        dryF = T->dry ? (T->lat < 1100 || !g_vid.ground ? 0 : 0.15f) : 1;   /* (dirt: sparse dry scrub, none on the verge; Ground detail off: none) */
        densMax = W3GrSide(latMin) * dryF * lowF * share;   /* (the most tufts a cell of it holds) */
        if (!T->dry && latMin < fmaxf(fabsf(T->eL), fabsf(T->eR)) + g_grStripW && densMax < g_grStrip * lowF) densMax = g_grStrip * lowF;   /* (it reaches the strip by the road) */
        if (densMax <= 0) continue;
        {   float mx = (T->a[0] + T->b[0] + T->c[0]) / 3, my = (T->a[1] + T->b[1] + T->c[1]) / 3, mz = (T->a[2] + T->b[2] + T->c[2]) / 3, rad = 0, e2; int q;   /* (the triangle's bounding sphere, + a tuft's size) */
            const float *V3[3] = { T->a, T->b, T->c };
            for (q = 0; q < 3; q++) { e2 = (V3[q][0] - mx) * (V3[q][0] - mx) + (V3[q][1] - my) * (V3[q][1] - my) + (V3[q][2] - mz) * (V3[q][2] - mz); if (e2 > rad) rad = e2; }
            if (!W3InView(mx, my, mz, sqrtf(rad) + 150)) continue; }
        nTri++;
        {   /* the tufts: spots on a fixed world grid - W3_GRCELL square cells, W3_GRK spots each at hashed places with hashed thresholds - those inside this
               triangle, at its ground's height there (W3GrSpots: worked out once).  A spot belongs to the ground, not to a triangle: the live stretch's and the
               file's triangles, a big one split or whole, all give the same tufts (user, 2026-11-15: "the polygons are changing somehow between far and close") */
            const W3GrSp *S; int ns = W3GrSpots(set, i, T, lowF, share, &S), q;
            for (q = 0; q < ns && tufts < grMax && n + 6 <= cap; q++) { const W3GrSp *P = &S[q]; float d, dens, sz, ht; int j;
                d = sqrtf((P->x - eye[0]) * (P->x - eye[0]) + (P->z - eye[2]) * (P->z - eye[2]));
                if (d > D) continue;
                nCand++;
                if (!W3InView(P->x, P->y + 40, P->z, 110)) { nCull++; continue; }   /* (a tuft is up to ~105 wide, ~90 high) */
                dens = P->dens;
                if (d > rThin) dens *= rThin / d;   /* (over the cap: the far ones thinner) */
                if (d > D * 0.7f) dens *= (D - d) / (D * 0.3f);   /* (towards the reach's end fewer and fewer - the density drops off, the tufts keep their size) */
                if (P->th >= dens) continue;
                /* (every tuft full size: no growing in or out - user, 2026-11-15: "the grow effect is very distracting ... the density should drop off only") */
                sz = P->sz; if (sz < d / g_w3fpxT * g_grMinPx) sz = d / g_w3fpxT * g_grMinPx;   /* (one size at every distance - user, "just make them stay size" - but never under ~2 pixels across: a smaller cut-out flickers in and out of the pixels) */
                ht = sz * P->htf;
                if (sz < 0.5f) continue;
                if (T->dry) { sz *= 0.65f; ht *= 0.8f; }   /* (dry scrub on the dirt: low, sparse) */
                for (j = 0; j < 6; j++) { int c4 = o6[j]; R3Vert *v = &o[n++];
                    v->x = P->x; v->y = P->y - 1; v->z = P->z; v->rgba = P->rgba; v->tex = 0;
                    v->ox = (c4 == 0 || c4 == 3) ? -sz : sz; v->oy = c4 < 2 ? 0.01f : ht;
                    v->u = (c4 == 0 || c4 == 3) ? (P->flip ? 1.0f : 0.0f) : (P->flip ? 0.0f : 1.0f); v->v = c4 < 2 ? 1.0f : 0.0f; }
                if (d > maxD) maxD = d;
                tufts++; } } } bandN[pass][set] += tufts - t0; } }
    if (tufts >= grMax) { rThin = (rThin > D ? D : rThin) * 0.95f; if (rThin < 3000) rThin = 3000; }
    else if (tufts < grMax * 0.8f && rThin < 1e9f) { rThin *= 1.03f; if (rThin > D * 1.5f) rThin = 1e9f; }
    if (getenv("RR_W3LOGGR")) { static int nf; if (++nf % 200 == 0) LOG("w3grass frame: %d tufts (%d triangles in view, %d tried, %d out of view), furthest %.0f of %.0f, share drawn %.3f; bands file/live %d/%d %d/%d %d/%d %d/%d %d/%d", tufts, nTri, nCand, nCull, maxD, D, share, bandN[0][0], bandN[0][1], bandN[1][0], bandN[1][1], bandN[2][0], bandN[2][1], bandN[3][0], bandN[3][1], bandN[4][0], bandN[4][1]); }
    return n; }
static unsigned W3Rgba(float r, float g, float b, float a)
{   int R = (int)(r * 255), G = (int)(g * 255), B = (int)(b * 255), A = (int)(a * 255);
    if (R > 255) R = 255; if (G > 255) G = 255; if (B > 255) B = 255; if (A > 255) A = 255; if (R < 0) R = 0; if (G < 0) G = 0; if (B < 0) B = 0; if (A < 0) A = 0;
    return (unsigned)R | ((unsigned)G << 8) | ((unsigned)B << 16) | ((unsigned)A << 24); }
static R3Vert *W3Halo(R3Vert *o, const W3Lamp *L, float dx, float hs, unsigned rgba)   /* a halo billboard round the lamp's lens (dx: along the camera's right) */
{   static const int o6[6] = { 0, 3, 1, 1, 3, 2 }; int j;
    for (j = 0; j < 6; j++) { int q = o6[j];
        o->x = L->x; o->y = L->y; o->z = L->z; o->rgba = rgba; o->tex = 0;
        o->ox = L->gox + dx + ((q == 0 || q == 3) ? -hs : hs); o->oy = L->goy + (q < 2 ? -hs : hs);
        if (o->ox == 0 && o->oy == 0) o->ox = 0.01f;
        o->u = (q == 0 || q == 3) ? 0.0f : 1.0f; o->v = q < 2 ? 1.0f : 0.0f; o++; }
    return o; }
/* the sprites pass (clip-space vertices): the world point at frame pixel (px, py) and view distance d - the shader lights / fogs the sprites per pixel
   from it (render3d.h: R3Vert tex, ox, oy hold it).  wf = the world pass's frame (eye, right) */
static void W3Unproj(const R3Frame *wf, float px, float py, double d, float *o)
{   double F = W3_F * (g_w3cam.pxPerUnit > 0 ? g_w3cam.pxPerUnit : 1), a = (px - g_w3cam.centreX) / F * d, b = (g_w3cam.horizonY - py) / F * d;
    o[0] = (float)(wf->eye[0] - wf->right[2] * d + wf->right[0] * a); o[1] = (float)(wf->eye[1] + b); o[2] = (float)(wf->eye[2] + wf->right[0] * d + wf->right[2] * a); }   /* forward = (-right.z, 0, right.x) */
static int g_w3rect = -1, g_w3rectGen = -1;
static int W3RectTex(void)   /* a lamp lens picture: a soft-edged rectangle, bright across its middle (the bikes' tail / brake lights) */
{   if (g_w3rectGen != g_w3texGen) { unsigned px[64 * 32]; int x, y;
        for (y = 0; y < 32; y++) for (x = 0; x < 64; x++) { float dx = fabsf((x + 0.5f) / 32 - 1), dy = fabsf((y + 0.5f) / 16 - 1),
                ex = dx < 0.72f ? 1 : (1 - dx) / 0.28f, ey = dy < 0.55f ? 1 : (1 - dy) / 0.45f, a = ex * ey;
            a = a * a * (3 - 2 * a); px[y * 64 + x] = ((unsigned)(a * 255) << 24) | 0xFFFFFFu; }
        g_w3rect = R3_Texture(&g_w3r, px, 64, 32); g_w3rectGen = g_w3texGen; }
    return g_w3rect; }
static R3Vert *W3ClipRect(R3Vert *o, float cx, float cy, float rx, float ry, int w, int h, double d, unsigned rgba, const R3Frame *wf)   /* a halo rx x ry (half sizes) in the sprites pass */
{   static const int o6[6] = { 0, 1, 2, 0, 2, 3 }; float z = (float)R3_Depth(d, W3_ZN, W3Far()), wp[3]; int j;
    W3Unproj(wf, cx, cy, d, wp);
    for (j = 0; j < 6; j++) { int q = o6[j]; float x = (q == 0 || q == 3) ? cx - rx : cx + rx, y = q < 2 ? cy - ry : cy + ry;
        o->x = x / w * 2.0f - 1.0f; o->y = 1.0f - y / h * 2.0f; o->z = z; o->u = (q == 0 || q == 3) ? 0.0f : 1.0f; o->v = q < 2 ? 0.0f : 1.0f;
        o->rgba = rgba; o->tex = wp[0]; o->ox = wp[1]; o->oy = wp[2]; o++; }
    return o; }
static int W3LampTex(int spr)   /* a rider sprite's tail lamp as a picture: its lamp pixels (riderlamps.inc) white, in its box + a clear 1-pixel rim (soft edges) */
{   static int tex[RL_SPRITES], gen[RL_SPRITES]; const RlLamp *L;
    if (spr < 0 || spr >= RL_SPRITES || !(L = &g_rlLamp[spr])->w) return -1;
    if (gen[spr] != g_w3texGen + 1) { unsigned px[18 * 64]; int x, y, w = L->w + 2, h = L->h + 2;
        if (h > 64) return -1;
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) { int on = x > 0 && y > 0 && x <= L->w && y <= L->h && (g_rlRows[L->row + y - 1] >> (x - 1) & 1);
            px[y * w + x] = on ? 0xFFFFFFFFu : 0x00FFFFFFu; }
        tex[spr] = R3_Texture(&g_w3r, px, w, h); gen[spr] = g_w3texGen + 1; }   /* (+1: 0 = never made) */
    return tex[spr]; }
static void W3BoxBlur(float *p, int w, int h, int r)   /* separable box blur of radius r, in place */
{   float *t=(float*)malloc(sizeof(float)*(w>h?w:h)); int x, y, k; if(!t) return;
    for(y=0;y<h;y++){ for(x=0;x<w;x++){ float s=0; int n=0; for(k=-r;k<=r;k++) if(x+k>=0 && x+k<w){ s+=p[y*w+x+k]; n++; } t[x]=s/(2*r+1); (void)n; } for(x=0;x<w;x++) p[y*w+x]=t[x]; }
    for(x=0;x<w;x++){ for(y=0;y<h;y++){ float s=0; for(k=-r;k<=r;k++) if(y+k>=0 && y+k<h) s+=p[(y+k)*w+x]; t[y]=s/(2*r+1); } for(y=0;y<h;y++) p[y*w+x]=t[y]; }
    free(t); }
static void W3TexDump(const char *what, int spr, const unsigned *px, int w, int h)   /* (RR_W3LENSDUMP: a lens picture once, as <dir>\lenstex_<sprite>_<what>.bmp, on black) */
{   const char *dir = getenv("RR_W3LENSDUMP"); char fn[MAX_PATH]; FILE *o; BITMAPFILEHEADER bh; BITMAPINFOHEADER ih; int i;
    if (!dir) return; _snprintf(fn, sizeof fn, "%s\\lenstex_%03d_%s.bmp", dir, spr, what); fn[sizeof fn - 1] = 0;
    if ((o = fopen(fn, "wb")) == NULL) return;
    memset(&bh, 0, sizeof bh); memset(&ih, 0, sizeof ih); bh.bfType = 0x4d42; bh.bfOffBits = sizeof bh + sizeof ih; bh.bfSize = bh.bfOffBits + w * h * 4;
    ih.biSize = sizeof ih; ih.biWidth = w; ih.biHeight = -h; ih.biPlanes = 1; ih.biBitCount = 32; fwrite(&bh, sizeof bh, 1, o); fwrite(&ih, sizeof ih, 1, o);
    for (i = 0; i < w * h; i++) { unsigned c = px[i], al = c >> 24, r = (c & 255) * al / 255, g = (c >> 8 & 255) * al / 255, bl = (c >> 16 & 255) * al / 255, q = (bl) | (g << 8) | (r << 16); fwrite(&q, 4, 1, o); }   /* (as added over black) */
    fclose(o); }
static int W3LampTex3D(int spr, int *core)   /* the lamp as a lens (8x its pixels): red, its rim bevelled - lit along the top with a glint, unlit along the bottom -; *core = its
                                               white-hot middle while braking (soft, the rim left out, so the bevel still shows) */
{   static int tex[RL_SPRITES], texc[RL_SPRITES], gen[RL_SPRITES]; const RlLamp *L;
    *core = -1;
    if (spr < 0 || spr >= RL_SPRITES || !(L = &g_rlLamp[spr])->w) return -1;
    if (gen[spr] != g_w3texGen + 1) { const int S = 8; int w = (L->w + 2) * S, h = (L->h + 2) * S, x, y; float *cov = (float*)malloc(sizeof(float) * w * h), *ht = (float*)malloc(sizeof(float) * w * h), *cd = (float*)malloc(sizeof(float) * w * h), gmax = 1e-6f, hmax = 1e-6f;
        unsigned *px = (unsigned*)malloc(sizeof(unsigned) * w * h), *pc = (unsigned*)malloc(sizeof(unsigned) * w * h);
        if (!cov || !ht || !cd || !px || !pc) { free(cov); free(ht); free(cd); free(px); free(pc); return -1; }
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) { int lx = x / S - 1, ly = y / S - 1; cov[y * w + x] = (lx >= 0 && ly >= 0 && lx < L->w && ly < L->h && (g_rlRows[L->row + ly] >> lx & 1)) ? 1.0f : 0.0f; }
        memcpy(ht, cov, sizeof(float) * w * h); W3BoxBlur(cov, w, h, S / 3); W3BoxBlur(ht, w, h, S); W3BoxBlur(ht, w, h, S / 2);   /* (edges smoothed; the "height": a soft dome rim) */
        memcpy(cd, ht, sizeof(float) * w * h); W3BoxBlur(cd, w, h, S * 3 / 2); W3BoxBlur(cd, w, h, S * 3 / 2);   /* (the core: deeper inside, a soft hot middle) */
        for (y = 1; y < h - 1; y++) for (x = 0; x < w; x++) { float gy = ht[(y + 1) * w + x] - ht[(y - 1) * w + x]; if (fabsf(gy) > gmax) gmax = fabsf(gy); if (cd[y * w + x] > hmax) hmax = cd[y * w + x]; }
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) { int xa = x > 0 ? x - 1 : x, xb = x < w - 1 ? x + 1 : x, ya = y > 0 ? y - 1 : y, yb = y < h - 1 ? y + 1 : y;
            float gy = (ht[yb * w + x] - ht[ya * w + x]) / gmax, gx = (ht[y * w + xb] - ht[y * w + xa]) / gmax, sh = 0.80f + 0.30f * gy + 0.06f * gx + 0.16f * (0.5f - (float)y / h), sp = gy > 0.30f ? (gy - 0.30f) / 0.70f : 0, a = cov[y * w + x], hc;
            int R, G, B; sp = sp * sp * 0.55f; if (sh > 1) sh = 1; if (sh < 0.05f) sh = 0.05f;
            R = (int)(255 * (sh * 0.95f + sp)); G = (int)(255 * (sh * 0.09f + sp * 0.85f)); B = (int)(255 * (sh * 0.05f + sp * 0.75f)); if (R > 255) R = 255; if (G > 255) G = 255; if (B > 255) B = 255;
            px[y * w + x] = ((unsigned)(a * 255) << 24) | ((unsigned)B << 16) | ((unsigned)G << 8) | (unsigned)R;
            hc = cd[y * w + x] / hmax; hc = hc < 0.5f ? 0 : (hc - 0.5f) / 0.5f; hc = hc * hc * (3 - 2 * hc) * a;   /* (the core: the lens's middle, fading out before the rim) */
            pc[y * w + x] = ((unsigned)(hc * 255) << 24) | 0xFFFFFFu; }
        tex[spr] = R3_Texture(&g_w3r, px, w, h); texc[spr] = R3_Texture(&g_w3r, pc, w, h); gen[spr] = g_w3texGen + 1;
        W3TexDump("lens", spr, px, w, h); W3TexDump("core", spr, pc, w, h);
        free(cov); free(ht); free(cd); free(px); free(pc); }
    *core = texc[spr]; return tex[spr]; }
static R3Vert *W3ClipQuad(R3Vert *o, const float *qx, const float *qy, int w, int h, double d, unsigned rgba, const R3Frame *wf)   /* a picture on the screen corners qx / qy (its left/top, right/top, right/bottom, left/bottom) in the sprites pass */
{   static const int o6[6] = { 0, 1, 2, 0, 2, 3 }; float z = (float)R3_Depth(d, W3_ZN, W3Far()), wp[3]; int j;
    W3Unproj(wf, (qx[0] + qx[2]) * 0.5f, (qy[0] + qy[2]) * 0.5f, d, wp);
    for (j = 0; j < 6; j++) { int q = o6[j];
        o->x = qx[q] / w * 2.0f - 1.0f; o->y = 1.0f - qy[q] / h * 2.0f; o->z = z; o->u = (q == 0 || q == 3) ? 0.0f : 1.0f; o->v = q < 2 ? 0.0f : 1.0f;
        o->rgba = rgba; o->tex = wp[0]; o->ox = wp[1]; o->oy = wp[2]; o++; }
    return o; }
static R3Vert *W3ClipHalo(R3Vert *o, float cx, float cy, float r, int w, int h, double d, unsigned rgba, const R3Frame *wf)   /* a halo in the sprites pass (frame pixels, distance d) */
{   static const int o6[6] = { 0, 1, 2, 0, 2, 3 }; float z = (float)R3_Depth(d, W3_ZN, W3Far()), wp[3]; int j;
    W3Unproj(wf, cx, cy, d, wp);
    for (j = 0; j < 6; j++) { int q = o6[j]; float x = (q == 0 || q == 3) ? cx - r : cx + r, y = q < 2 ? cy - r : cy + r;
        o->x = x / w * 2.0f - 1.0f; o->y = 1.0f - y / h * 2.0f; o->z = z; o->u = (q == 0 || q == 3) ? 0.0f : 1.0f; o->v = q < 2 ? 0.0f : 1.0f;
        o->rgba = rgba; o->tex = wp[0]; o->ox = wp[1]; o->oy = wp[2]; o++; }
    return o; }
/* a bike sprite's lamps: the red tail-light square(s) in its bitmap (strong red texels in the band under the seat - not a red jacket or fairing above -, small, grouped into up to 2 clusters side by side), in texture fractions;
   cached by the bitmap's hash.  The headlights are straight ahead of them (the bike is seen from behind): one cone, or two for a bike with two lamps */
typedef struct W3Red { DWORD hash; int n; float u[2], v[2]; } W3Red;
static int W3IsLampRed(const PALETTEENTRY *c) { return c->peRed > 105 && c->peRed > c->peGreen * 3 && c->peRed > c->peBlue * 3; }   /* a tail lamp's red: also the darker ones, but pure (orange - green half the red - is a jacket, not a lamp) */
static const W3Red *W3RedSpots(const GpuTex *tx)
{   static W3Red cache[512], rl[RL_SPRITES]; static int nc; W3Red *e; int i, x, y, cols[512], c0, k; const PALETTEENTRY *pe;
    if (tx && tx->rspr >= 0 && tx->rspr < RL_SPRITES) {   /* a rider sprite (HiBob): its lamp hardcoded (riderlamps.inc), the middle of its box - no looking */
        const RlLamp *L = &g_rlLamp[tx->rspr]; e = &rl[tx->rspr];
        e->n = L->w && L->x + L->w <= tx->w && L->y + L->h <= tx->h; e->u[0] = (L->x + L->w * 0.5f) / tx->w; e->v[0] = (L->y + L->h * 0.5f) / tx->h;
        return e; }
    if (!tx || !tx->bits || tx->w > 512 || tx->h > 512 || tx->w * 2 >= tx->h * 3 || !g_primary || !g_primary->pal) return NULL;   /* (the rest - a police rider, raw tiles - by colour: wide low pictures - the shadows under the bikes, a fallen rider - are left out; a bike from behind is taller than wide) */
    for (i = 0; i < nc; i++) if (cache[i].hash == tx->hash) return &cache[i];
    e = &cache[nc < 512 ? nc++ : (int)(tx->hash & 511)]; memset(e, 0, sizeof *e); e->hash = tx->hash;
    pe = g_cdPal ? g_cdPal : g_primary->pal->e;
    {   /* the race palette as stored in the game's files (ridercol.inc RcRacePal), not the live one: that fades in and out, and a picture first seen dimmed
           would keep "no lamp" in the cache for the rest of the race */
        static PALETTEENTRY rp[256]; static int rpOk; const DWORD *rc;
        if (!rpOk && (rc = RcRacePal()) != NULL) { for (i = 0; i < 256; i++) { rp[i].peRed = (BYTE)(rc[i] >> 16); rp[i].peGreen = (BYTE)(rc[i] >> 8); rp[i].peBlue = (BYTE)rc[i]; rp[i].peFlags = 0; } rpOk = 1; }
        if (rpOk) pe = rp; }
    if (tx->key & 2) {   /* the player in custom colours (ridercol.inc): the bytes are colour-template indices, not palette indices - read in the player's
                            original colours (RcOrigLut: as without the custom parts; the lamp is never recoloured, a red helmet / orange jacket is) */
        static PALETTEENTRY tp[256]; const DWORD *lut = RcOrigLut();
        if (lut) { for (i = 0; i < 256; i++) { tp[i].peRed = (BYTE)(lut[i] >> 16); tp[i].peGreen = (BYTE)(lut[i] >> 8); tp[i].peBlue = (BYTE)lut[i]; tp[i].peFlags = 0; } pe = tp; } }
    {   /* the lamp texels in the band rows 40..85 % / columns 20..80 %, then their 4-connected patches (flood fill); the lamps are the best compact patches -
           not the first red columns from the left: a red helmet / jacket / stripe left of the lamp was taken before (user screenshots, 2026-11-13) */
        static BYTE m[512 * 512]; static int st[512 * 512]; int W = tx->w, H = tx->h, pitch = tx->pitch ? tx->pitch : tx->w, ya = H * 2 / 5, yb = H * 17 / 20, xa = W / 5, xb = W * 4 / 5;        int nb = 0; struct { double sx, sy, n, sat; int x0, x1, y0, y1; } bl[32]; (void)cols; (void)c0;
        memset(m, 0, (size_t)W * H);
        for (y = ya; y < yb; y++) for (x = xa; x < xb; x++) { int p = tx->bits[y * pitch + x];   /* (by its colour in the race palette) */
            if (!p) continue;
            if (W3IsLampRed(&pe[p])) m[y * W + x] = 1; }
        for (y = ya; y < yb; y++) for (x = xa; x < xb; x++) if (m[y * W + x] == 1 && nb < 32) {
            int sp = 0; bl[nb].sx = bl[nb].sy = bl[nb].n = bl[nb].sat = 0; bl[nb].x0 = bl[nb].x1 = x; bl[nb].y0 = bl[nb].y1 = y;
            m[y * W + x] = 2; st[sp++] = y * 512 + x;
            while (sp > 0) { int q = st[--sp], qy = q / 512, qx = q % 512, d; const PALETTEENTRY *c = &pe[tx->bits[qy * pitch + qx]]; int mx2 = c->peGreen > c->peBlue ? c->peGreen : c->peBlue;
                bl[nb].sx += qx; bl[nb].sy += qy; bl[nb].n++; bl[nb].sat += (double)(c->peRed - mx2) / c->peRed;
                if (qx < bl[nb].x0) bl[nb].x0 = qx; if (qx > bl[nb].x1) bl[nb].x1 = qx; if (qy < bl[nb].y0) bl[nb].y0 = qy; if (qy > bl[nb].y1) bl[nb].y1 = qy;
                for (d = 0; d < 4; d++) { int nx = qx + (d == 0) - (d == 1), ny = qy + (d == 2) - (d == 3);
                    if (nx >= xa && nx < xb && ny >= ya && ny < yb && m[ny * W + nx] == 1 && sp < 512 * 512) { m[ny * W + nx] = 2; st[sp++] = ny * 512 + nx; } } }
            nb++; }
        {   int b0 = -1, b1 = -1; double s0 = 0, s1 = 0;
            for (k = 0; k < nb; k++) { double n = bl[k].n, bw2 = bl[k].x1 - bl[k].x0 + 1, bh2 = bl[k].y1 - bl[k].y0 + 1, sc;
                if (n < 2 || n > W * H / 30 || bh2 > H / 6 + 2) continue;   /* (a lamp is a small patch, not tall) */
                sc = n * (bl[k].sat / n) * (n / (bw2 * bh2));   /* (size x purity x solidity) */
                if (sc > s0) { b1 = b0; s1 = s0; b0 = k; s0 = sc; } else if (sc > s1) { b1 = k; s1 = sc; } }
            if (b0 >= 0) { e->u[0] = (float)((bl[b0].sx / bl[b0].n + 0.5) / W); e->v[0] = (float)((bl[b0].sy / bl[b0].n + 0.5) / H); e->n = 1;
                if (b1 >= 0 && s1 > s0 * 0.5 && fabs(bl[b1].sy / bl[b1].n - bl[b0].sy / bl[b0].n) < H * 0.04 + 1 && fabs(bl[b1].sx / bl[b1].n - bl[b0].sx / bl[b0].n) > W * 0.12) {   /* twin lamps: a matching patch level with it, apart */
                    e->u[1] = (float)((bl[b1].sx / bl[b1].n + 0.5) / W); e->v[1] = (float)((bl[b1].sy / bl[b1].n + 0.5) / H); e->n = 2;
                    if (e->u[1] < e->u[0]) { float t = e->u[0]; e->u[0] = e->u[1]; e->u[1] = t; t = e->v[0]; e->v[0] = e->v[1]; e->v[1] = t; } } } } }
    return e; }
/* the screen point of texture fraction (tu, tv) on a sprite polygon (corners px / py with texture fractions u / v): the affine map of corners 0, 1, 3 */
static int W3TexToScreen(const float *u, const float *v, const float *px, const float *py, float tu, float tv, float *ox, float *oy)
{   double a1 = u[1] - u[0], b1 = u[3] - u[0], a2 = v[1] - v[0], b2 = v[3] - v[0], det = a1 * b2 - b1 * a2, du = tu - u[0], dv = tv - v[0], s, t;
    if (fabs(det) < 1e-9) return 0;
    s = (du * b2 - b1 * dv) / det; t = (a1 * dv - du * a2) / det;
    *ox = (float)(px[0] + (px[1] - px[0]) * s + (px[3] - px[0]) * t); *oy = (float)(py[0] + (py[1] - py[0]) * s + (py[3] - py[0]) * t);
    return 1; }
/* a rider's brake light: on while he slows down (his road position from frame to frame, smoothed), off a moment after */
typedef struct W3Rv { unsigned id; double q, v, a, t; float brake; } W3Rv;
static W3Rv g_w3rv[64];
static float W3Brake(unsigned id, double q, double now)
{   W3Rv *e = NULL; int i, old = 0; double dt, v;
    for (i = 0; i < 64; i++) { if (g_w3rv[i].id == id) { e = &g_w3rv[i]; break; } if (g_w3rv[i].t < g_w3rv[old].t) old = i; }
    if (!e) { e = &g_w3rv[old]; memset(e, 0, sizeof *e); e->id = id; e->q = q; e->t = now; return 0; }
    dt = now - e->t; if (dt < 0.004) return e->brake;
    v = (q - e->q) / dt;   /* pieces a second */
    if (fabs(v - e->v) > 60) { e->v = v; e->a = 0; }   /* (a jump: a crash, a re-found road offset) */
    else { double nv = e->v + (v - e->v) * (dt * 6 < 1 ? dt * 6 : 1); e->a += ((nv - e->v) / dt - e->a) * (dt * 5 < 1 ? dt * 5 : 1); e->v = nv; }
    e->q = q; e->t = now;
    if (e->a < -1.6 && e->v > 0.5) e->brake = 1; else { e->brake -= (float)dt * 2.5f; if (e->brake < 0) e->brake = 0; }
    return e->brake; }
/* a rider's heading against the road: his sideways speed over his forward speed (lateral units per piece), from his lateral and road position frame to
   frame, smoothed only a little (~50 ms) so the headlight turns with the bike */
typedef struct W3Hd { unsigned id; double q, u, t, r; } W3Hd;
static W3Hd g_w3hd[64];
static double W3Steer(unsigned id, double q, double u, double now)
{   W3Hd *e = NULL; int i, old = 0; double dt, dq, r, k;
    for (i = 0; i < 64; i++) { if (g_w3hd[i].id == id) { e = &g_w3hd[i]; break; } if (g_w3hd[i].t < g_w3hd[old].t) old = i; }
    if (!e || now - e->t > 0.5) { if (!e) e = &g_w3hd[old]; memset(e, 0, sizeof *e); e->id = id; e->q = q; e->u = u; e->t = now; return 0; }
    dt = now - e->t; if (dt < 0.004) return e->r;
    dq = q - e->q;
    if (fabs(dq) > 2 || fabs(u - e->u) > 400) r = e->r;   /* (a jump: a crash, a re-found road offset) */
    else if (dq < 0.004) r = 0;   /* (standing) */
    else { r = (u - e->u) / dq; if (r > 420) r = 420; if (r < -420) r = -420; }   /* (at most ~32 deg) */
    k = dt / 0.05; if (k > 1) k = 1;
    e->r += (r - e->r) * k; e->q = q; e->u = u; e->t = now;
    return e->r; }
static int W3CopFlash(double sec) { return (int)(sec * 6.0) & 1; }   /* police lights: red / blue, 3 times a second each */
/* ---- the compiled race (user, 2026-11-15: "i dont want it to calculate/rebuild on the fly, can you record it / compile the race please so then it can
   be streamed for next time"): forks off, the whole course is built once at race start - skirts included - and each frame only picks the stretch in
   view out of it (W3StaticCut): nothing is built while racing.  It is compiled ahead for a release only (user: "the compile should only be used for
   release"): analysis\compile_races.ps1 runs every track / level with RR_W3COMPILE=1, which saves it beside its course file as
   <file>.g<ground detail>.mesh (the vertices in batch order, their pieces, the batches, the buildings).  A race finding a matching one loads it;
   without one it is built on the fly as before - the static mesh at race start + the live stretch round the camera (W3Live) (user: "fallback to
   calculating on the fly ... if the race course compiled files are there it should use those").  Nothing else writes it; tests never load one
   (rrtest.ps1: RR_W3MESH=0 - user: "during your testing you need to disregard the compiled files"), and a rebuilt shim never matches an old file.
   The key: this build of the shim, the course file (size, time), its textures (names, sizes, facade data), the settings the mesh depends on ---- */
#define W3_MESHMAGIC 0x534d3352u   /* "R3MS" */
static unsigned long long W3MeshKey(const char *path)
{   unsigned long long h = 1469598103934665603ull; WIN32_FILE_ATTRIBUTE_DATA fa; int i; const char *v = __DATE__ " " __TIME__;
#define W3K(p, n) { const unsigned char *b_ = (const unsigned char *)(p); size_t i_; for (i_ = 0; i_ < (size_t)(n); i_++) { h ^= b_[i_]; h *= 1099511628211ull; } }
    W3K(v, strlen(v));
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) { W3K(&fa.nFileSizeLow, 4); W3K(&fa.ftLastWriteTime, 8); }
    W3K(&g_w3a.ntex, 4);
    for (i = 0; i < g_w3a.ntex; i++) { const RxTex *x = &g_w3a.tex[i]; W3K(x->name, 8); W3K(&x->w, 4); W3K(&x->h, 4); W3K(&x->facade, 4); W3K(&x->shopV, 4); W3K(&x->key, 4); W3K(&x->alpha, 4); }
    W3K(&g_w3c.n, 4); W3K(&g_w3c.noGround, 4); W3K(&g_w3c.forkPick, 4);
#undef W3K
    return h; }
static void W3MeshName(const char *path, char *out, int n) { _snprintf(out, n - 1, "%s.g%d.mesh", path, !g_w3c.noGround); out[n - 1] = 0; }   /* (Ground detail on / off: rocks, fallen leaves are in the mesh) */
static void W3MeshSave(const char *path, unsigned long long key)
{   char fn[MAX_PATH], tmp[MAX_PATH + 8]; FILE *o; unsigned hd[8]; int ok;
    W3MeshName(path, fn, sizeof fn); _snprintf(tmp, sizeof tmp - 1, "%s.tmp", fn); tmp[sizeof tmp - 1] = 0;
    if (!(o = fopen(tmp, "wb"))) { LOG("world3d: cannot write %s", tmp); return; }
    hd[0] = W3_MESHMAGIC; hd[1] = (unsigned)key; hd[2] = (unsigned)(key >> 32); hd[3] = (unsigned)g_w3c.n; hd[4] = (unsigned)g_w3c.nv; hd[5] = (unsigned)g_w3nb; hd[6] = (unsigned)g_w3c.nbldg; hd[7] = sizeof(C3Vert);
    ok = fwrite(hd, sizeof hd, 1, o) == 1 && fwrite(g_w3cb, sizeof(C3Batch), g_w3nb, o) == (size_t)g_w3nb && fwrite(g_w3c.v, sizeof(C3Vert), g_w3c.nv, o) == (size_t)g_w3c.nv
        && fwrite(g_w3c.vpiece, sizeof(int), g_w3c.nv, o) == (size_t)g_w3c.nv && (!g_w3c.nbldg || fwrite(g_w3c.bldg, sizeof(C3Bldg), g_w3c.nbldg, o) == (size_t)g_w3c.nbldg);
    ok = fclose(o) == 0 && ok;
    if (!ok || !MoveFileExA(tmp, fn, MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp); LOG("world3d: could not save %s", fn); return; }
    LOG("world3d: compiled %s (%d vertices, %d batches, %d buildings)", fn, g_w3c.nv, g_w3nb, g_w3c.nbldg); }
static const char *W3MeshTag(void)   /* the end of the segment tracker line in a True 3D race (rrddraw.c ShowRoadPos); test switch RR_MESHTAG=0: none */
{   const char *e = getenv("RR_MESHTAG"); return (g_w3On && g_w3meshTag[0] && !(e && *e == '0')) ? g_w3meshTag : NULL; }
/* ---- the F10 grass panel (True 3D races): F10 opens / closes it, Page Up / Page Down pick a row, [ / ] change it (held: repeats), Backspace puts the
   row back; the race goes on underneath (those keys are not the game's).  Closing it logs the values (RR_LOG) to bake in ---- */
typedef struct GrRow { const char *name; float *v; float step, lo, hi, def; const char *fmt; } GrRow;
static GrRow g_grRows[] = {
    { "Density (share of spots)", &g_grShare, 0.01f, 0, 1, 0.10f, "%.2f" }, { "Strip density (a cell)", &g_grStrip, 0.1f, 0, 5, 0.3f, "%.1f" },
    { "Strip width", &g_grStripW, 50, 0, 3000, 2250, "%.0f" }, { "Thick out to (from centre)", &g_grFull, 100, 0, 8000, 0, "%.0f" },
    { "Fall-off beyond it", &g_grFall, 50, 50, 4000, 600, "%.0f" }, { "Reach (x effects distance)", &g_grReach, 0.05f, 0.05f, 1.8f, 0.35f, "%.2f" },
    { "Tuft size", &g_grSize, 0.1f, 0.2f, 4, 3.0f, "%.1f" }, { "Smallest (pixels)", &g_grMinPx, 0.25f, 0, 4, 1.0f, "%.2f" },
    { "Far alpha boost", &g_grMip, 0.1f, 0, 3, 0.6f, "%.1f" }, { "Cap (tufts)", &g_grCap, 1000, 0, W3_GRMAX, 8000, "%.0f" } };
#define GR_NROW ((int)(sizeof g_grRows / sizeof g_grRows[0]))
static int g_grOpen, g_grSel;
static void W3GrPanel(int w, int h)   /* (gpuscene.inc GpuDrawExtras, each frame of a race) */
{   static const int keys[6] = { VK_F10, VK_PRIOR, VK_NEXT, VK_OEM_4, VK_OEM_6, VK_BACK }; static int down[6]; static DWORD rep[6]; int k, i; DWORD now = GetTickCount(); (void)h;
    if (!g_w3On) { g_grOpen = 0; return; }
    for (k = 0; k < 6; k++) {
        int d = (GetAsyncKeyState(keys[k]) & 0x8000) && RrOurFocus(), fire = 0;
        if (d && !down[k]) { fire = 1; rep[k] = now + 350; } else if (d && k >= 3 && k <= 4 && now >= rep[k]) { fire = 1; rep[k] = now + 60; }
        down[k] = d;
        if (!fire) continue;
        if (k == 0) { g_grOpen ^= 1; if (!g_grOpen) { char t[600]; int n = 0; for (i = 0; i < GR_NROW; i++) n += _snprintf(t + n, sizeof t - n, "%s%s %g", i ? ", " : "", g_grRows[i].name, *g_grRows[i].v); LOG("grass panel: %s", t); } continue; }
        if (!g_grOpen) continue;
        if (k == 1) g_grSel = (g_grSel + GR_NROW - 1) % GR_NROW;
        else if (k == 2) g_grSel = (g_grSel + 1) % GR_NROW;
        else { GrRow *R = &g_grRows[g_grSel]; float v = k == 5 ? R->def : *R->v + (k == 4 ? R->step : -R->step); *R->v = v < R->lo ? R->lo : v > R->hi ? R->hi : v; } }
    if (!g_grOpen) return;
    {   SDL_FRect bg; char t[96]; float x = 4, y = 22, rw = 340;
        bg.x = x; bg.y = y; bg.w = rw; bg.h = 14 * (GR_NROW + 3) + 8; SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 170); SDL_RenderFillRect(g_ren, &bg);
        SDL_SetRenderDrawColor(g_ren, 255, 255, 120, 255); SDL_RenderDebugText(g_ren, x + 6, y + 5, "GRASS (F10, High's values)  PgUp/PgDn row  [ ] change  Bksp reset");
        for (i = 0; i < GR_NROW; i++) { char val[24]; GrRow *R = &g_grRows[i]; _snprintf(val, sizeof val - 1, R->fmt, *R->v); val[sizeof val - 1] = 0;
            _snprintf(t, sizeof t - 1, "%c %-27s %s", i == g_grSel ? '>' : ' ', R->name, val); t[sizeof t - 1] = 0;
            if (i == g_grSel) SDL_SetRenderDrawColor(g_ren, 255, 235, 80, 255); else SDL_SetRenderDrawColor(g_ren, 225, 225, 235, 255);
            SDL_RenderDebugText(g_ren, x + 6, y + 21 + 14 * i, t); }
        _snprintf(t, sizeof t - 1, "  %s: %d tufts, %.2f ms CPU, reach %.0f", (const char *[]){ "Off", "Low", "Medium", "High" }[g_vid.grass & 3], g_grLast, g_grLastMs, W3FxDist() * g_grReach * (0.5f + 0.5f * W3GrLevel())); t[sizeof t - 1] = 0;
        SDL_SetRenderDrawColor(g_ren, 140, 230, 160, 255); SDL_RenderDebugText(g_ren, x + 6, y + 21 + 14 * GR_NROW + 6, t); (void)w; } }
static int W3GrPanelOpen(void) { return g_grOpen && g_w3On; }   /* (the jukebox leaves F10 to it in True 3D races) */
static int W3MeshLoad(const char *path, unsigned long long key)   /* 1 = g_w3c's mesh, g_w3cb / g_w3nb from the compiled file */
{   char fn[MAX_PATH]; FILE *f; unsigned hd[8]; int ok = 0;
    W3MeshName(path, fn, sizeof fn);
    if (getenv("RR_W3MESH") && *getenv("RR_W3MESH") == '0') return 0;
    if (!(f = fopen(fn, "rb"))) return 0;
    if (fread(hd, sizeof hd, 1, f) == 1 && hd[0] == W3_MESHMAGIC && hd[1] == (unsigned)key && hd[2] == (unsigned)(key >> 32) && hd[3] == (unsigned)g_w3c.n
        && hd[7] == sizeof(C3Vert) && hd[5] <= 4096 && hd[4] > 0) {
        int nv = (int)hd[4], nb = (int)hd[5], nbl = (int)hd[6];
        C3Vert *v = (C3Vert *)malloc(sizeof(C3Vert) * nv); int *vp = (int *)malloc(sizeof(int) * nv); C3Bldg *bl = nbl ? (C3Bldg *)malloc(sizeof(C3Bldg) * nbl) : NULL;
        if (v && vp && (!nbl || bl) && fread(g_w3cb, sizeof(C3Batch), nb, f) == (size_t)nb && fread(v, sizeof(C3Vert), nv, f) == (size_t)nv && fread(vp, sizeof(int), nv, f) == (size_t)nv
            && (!nbl || fread(bl, sizeof(C3Bldg), nbl, f) == (size_t)nbl)) {
            free(g_w3c.v); free(g_w3c.vpiece); free(g_w3c.bldg);
            g_w3c.v = v; g_w3c.vpiece = vp; g_w3c.nv = g_w3c.cap = nv; g_w3c.bldg = bl; g_w3c.nbldg = g_w3c.capBldg = nbl; g_w3nb = nb; ok = 1; }
        else { free(v); free(vp); free(bl); } }
    fclose(f);
    if (ok) LOG("world3d: %s: the compiled race (%d vertices) - not built", fn, g_w3c.nv);
    return ok; }
/* this frame's static batches: the stretch from the ring's back to the draw distance (nothing behind the ring - like the original's view - and no far
   part of the course that a bend brings near the camera); the skirts only round the camera, as far as the game's ring reaches (far off, a skirt above
   the camera is a sheet over the sky: course3d.h C3_Skirt).  A lookup in the compiled mesh (pieces in order within each batch), nothing built */
static void W3StaticCut(void)
{   int lo = (g_w3cam.ringBack - g_w3Piece0) >> 8, kEnd = ((g_w3cam.viewPos - g_w3Piece0) >> 8) + (int)(W3Far() / W3_L) + 2, kSk = lo + RRW_LIVE, i;
    if (lo < 0) lo = 0;
    g_w3snb = 0;
    for (i = 0; i < g_w3nb && g_w3snb < (int)(sizeof g_w3sb / sizeof g_w3sb[0]); i++) {
        int b = C3_BatchCut(&g_w3c, &g_w3cb[i], lo), e = C3_BatchCut(&g_w3c, &g_w3cb[i], g_w3cb[i].tex >= C3_BG - 1 ? kSk : kEnd);
        if (e > b) { g_w3sb[g_w3snb] = g_w3b[i]; g_w3sb[g_w3snb].first = b; g_w3sb[g_w3snb].count = e - b; g_w3snb++; } } }

/* race start: the course file + the game's own road (pieces 0 .. 127 must agree with the file) */
static void W3BuildProgress(int done, int all) { LsStep(500 + (int)(400.0 * done / (all > 0 ? all : 1)), NULL); }   /* (the loading screen: the course's mesh 50-90 %) */
__declspec(dllexport) int __stdcall RR_World3DLoad(const char *path, const RrWorld3DLive *live)
{   unsigned hdr[16]; FILE *f; int i, bad = 0;
    g_w3On = 0; g_w3HaveCam = 0; g_w3off = g_w3offB = g_w3camB = 0;
    if (!path || !live) return 0;
    LsStep(450, "Reading the course");   /* (the loading screen, loadscreen.inc) */
    W3Free();
    if (!(f = fopen(path, "rb"))) { LOG("world3d: no %s", path); return 0; }
    if (fread(hdr, 4, 16, f) != 16) { fclose(f); return 0; } fclose(f);
    if (!C3_LoadRrx(&g_w3c, path) || !RX_Load(&g_w3a, path)) { LOG("world3d: %s unreadable", path); W3Free(); return 0; }
    g_w3Piece0 = (int)hdr[5];
    if (ViewerOn()) lstrcpynA(g_vw->course, path, sizeof g_vw->course);   /* (the editor's viewer: the course file it shows) */
    if (g_w3Piece0 != live->piece0) bad = 1;
    for (i = 0; !bad && i < live->n && i < g_w3c.n; i++) {
        const RoadSection *a = &g_w3c.piece[i].halfA;
        if (a->curve != live->curve[i] || a->hill != live->hill[i] || a->edgeL != live->edgeL[i] || a->edgeR != live->edgeR[i]) bad = 1 + i;
    }
    if (bad) { LOG("world3d: %s does not match this race's road (%d: piece 0 at %d, the race's at %d; %d / %d pieces) - not used", path, bad, g_w3Piece0, live->piece0, g_w3c.n, live->n); W3Free(); return 0; }
    if (!g_w3Init) {
        ID3D11Device *dev = (ID3D11Device *)SDL_GetPointerProperty(SDL_GetRendererProperties(g_ren), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, NULL);
        if (!R3_Init(&g_w3r, dev)) { LOG("world3d: renderer: %s", g_w3r.err); W3Free(); return 0; }
        g_w3Init = 1;
    } else { for (i = 0; i < g_w3r.ntex; i++) if (g_w3r.tex[i]) ID3D11ShaderResourceView_Release(g_w3r.tex[i]); g_w3r.ntex = 0; }
    g_w3texGen++;
    if (getenv("RR_W3LOGROW")) for (i = 0; i < g_w3c.n; i++) {   /* RR_W3LOGROW=1: every piece's object row / roadside sprites (texture, lateral x) with its kind flags (0x20 = cross street) */
        const RoadSection *s = &g_w3c.piece[i].halfA; int ax, ay, t, j; char xs[96]; xs[0] = 0;
        for (j = 0; j < s->rowN && j < 8; j++) _snprintf(xs + strlen(xs), sizeof xs - strlen(xs) - 1, " %d", s->rowX[j]);
        if (s->rowN && (s->rowCode & 0x3f) != 0x3f && (t = RX_NarrTex(&g_w3a, i, 0, s->rowCode, 0, 0, &ax, &ay)) >= 0)
            LOG("w3row %d kind %d flags %x row %s code %x n %d x%s", i, s->kind, s->kindFlags, g_w3a.tex[t].name, s->rowCode, s->rowN, xs);
        for (j = 0; j < s->spriteN && j < 2; j++) if ((t = RX_NarrTex(&g_w3a, i, 0, s->sprite[j].code, 4, 1, &ax, &ay)) >= 0)
            LOG("w3spr %d kind %d flags %x spr %s x %d", i, s->kind, s->kindFlags, g_w3a.tex[t].name, s->sprite[j].x);
    }
    {   /* True 3D has no forks: every split road is its left road A only (Pseudo 3D keeps the game's forks).  Test switch RR_NOFORK=0: the
           game's forks in the 3D world, the other carriageway built round the camera like the original's view (course3d.h C3_SoftFrames; best with RR_W3DIST=33) */
        const char *e = getenv("RR_NOFORK");
        g_w3fork = (e && *e) ? *e != '0' : !g_w3forkWant;   /* (the editor's forks: on) */
        g_w3c.forkPick = g_w3fork; g_w3c.soft = !g_w3fork; g_w3c.noGround = !g_vid.ground;   /* (True 3D detail > Ground detail: rocks, fallen leaves) */
        {   const char *bt = getenv("RR_W3BTRUE"); if (!g_w3fork && (bt && *bt ? *bt == '1' : g_w3forkWant)) g_w3c.soft = 0; }   /* (a course with the Course Editor's forks: B on its
                                                                                                                  own path as the editor draws it - course3d.h C3_FramesB - not built round the camera like
                                                                                                                  the original's view; test switch RR_W3BTRUE=1 / 0: so for any course / not) */
        if (ViewerOn()) { g_w3c.forkPick = 0; g_w3c.soft = 0; }   /* (the Course Editor's viewer: the forks always seen - both carriageways, B on its own path: course3d.h C3_FramesB) */
        LOG(g_w3fork ? "world3d: forks off - the left road only" : "world3d: forks on - the other carriageway as the original's view places it"); }
    {   const char *b = strrchr(path, 0x5C), *b2 = strrchr(path, 0x2F); if (b2 > b) b = b2; b = b ? b + 1 : path;   /* t<track>_...: City (1) and Peninsula (3) are the towns */
        W3GenFacades(b[0] == 0x74 && (b[1] == 0x31 || b[1] == 0x33)); W3RockTex(); W3GroundArt();
        g_w3hdTrack = b[0] == 0x74 && b[1] >= 0x31 && b[1] <= 0x35 ? b[1] - 0x30 : 0; }
    if (g_w3hdTrack == 1) for (i = 0; i < g_w3c.n; i++) {   /* the City's footpaths 10 % lower (user, 2026-11-15: "slightly too high"): the course's own copy, so the
                                                             mesh, the kerbs, the tree pits, the ground under the objects and the live stretch all agree */
        int h2, s;
        for (h2 = 0; h2 < 2; h2++) { RoadSection *sc = h2 ? &g_w3c.piece[i].halfB : &g_w3c.piece[i].halfA;
            if (sc->kind == SEC_OPEN) for (s = 0; s < 2; s++) if (sc->shoulderH[s] > 0) sc->shoulderH[s] = (sc->shoulderH[s] * 9 + 5) / 10; } }
    g_w3compiled = 0;
    if (g_w3fork) {   /* forks off: the compiled race when there is one (or it is being compiled: RR_W3COMPILE=1), else built on the fly as ever */
        unsigned long long key = W3MeshKey(path);
        LsStep(500, "Loading the course's 3D mesh");
        if (W3MeshLoad(path, key)) { g_w3c.L = W3_L; C3_Frames(&g_w3c); C3_FramesB(&g_w3c); g_w3c.fr = g_w3c.frame; C3_OneRoadWidth(&g_w3c); g_w3compiled = 1; }   /* (what C3_Build does besides the mesh) */
        else if (getenv("RR_W3COMPILE")) {
            g_w3c.skirts = 1; C3_progress = W3BuildProgress; C3_Build(&g_w3c, W3_L, &g_w3a); C3_progress = NULL; g_w3c.skirts = 0;
            g_w3nb = C3_Batches(&g_w3c, g_w3cb, 4096); W3MeshSave(path, key); g_w3compiled = 1; } }
    if (!g_w3compiled) { LsStep(500, "Building the 3D course"); C3_progress = W3BuildProgress; C3_Build(&g_w3c, W3_L, &g_w3a); C3_progress = NULL; g_w3nb = C3_Batches(&g_w3c, g_w3cb, 4096); }
    strcpy(g_w3meshTag, g_w3compiled ? "FIXED MESH" : "DYNAMIC MESH");   /* (user, 2026-11-15: on the segment tracker line, "dynamic mesh" vs "fixed mesh"; dynamic = built on the fly: the live stretch round the camera, W3Live) */
    W3GpuBatches(g_w3cb, g_w3nb, g_w3b);   /* (same order as g_w3cb: W3Live cuts them; the renderer draws the opaque ones first) */
    R3_SetStatic(&g_w3r, (const R3Vert *)g_w3c.v, g_w3c.nv);
    memset(&g_w3l, 0, sizeof g_w3l);   /* the live copy: the file's pieces, g_w3c's frames */
    g_w3l.skirts = 1;   /* (the skirts round the camera only: course3d.h C3_Skirt) */
    g_w3l.forkPick = g_w3c.forkPick; g_w3l.noGround = g_w3c.noGround;
    g_w3l.bdead = g_w3c.bdead; g_w3l.pairA = g_w3c.pairA; g_w3l.pairB = g_w3c.pairB;   /* (shared, g_w3c's) */
    g_w3l.n = g_w3c.n; g_w3l.start = g_w3c.start; g_w3l.finish = g_w3c.finish; g_w3l.L = g_w3c.L; g_w3l.frame = g_w3c.frame; g_w3l.frameB = g_w3c.frameB; g_w3l.fr = g_w3l.frame;
    if ((g_w3l.piece = (RoadPiece *)malloc(sizeof(RoadPiece) * g_w3c.n)) != NULL) memcpy(g_w3l.piece, g_w3c.piece, sizeof(RoadPiece) * g_w3c.n);
    if (!g_w3fork && g_w3c.frameB) {   /* forks on: the live copy's own frames (C3_SoftFrames), side by side pairing, the game's ring per piece */
        int n1 = g_w3c.n + 1;
        g_w3l.frame = (C3Frame *)malloc(sizeof(C3Frame) * n1); g_w3l.frameB = (C3Frame *)malloc(sizeof(C3Frame) * n1);
        g_w3l.other = (unsigned char *)calloc(n1, 1); g_w3ring = (unsigned char *)calloc(n1, 1);
        g_w3l.pairA = (int *)malloc(sizeof(int) * n1); g_w3l.pairB = (int *)malloc(sizeof(int) * n1);
        if (g_w3l.frame && g_w3l.frameB && g_w3l.other && g_w3ring && g_w3l.pairA && g_w3l.pairB) {
            memcpy(g_w3l.frame, g_w3c.frame, sizeof(C3Frame) * n1); memcpy(g_w3l.frameB, g_w3c.frameB, sizeof(C3Frame) * n1);
            for (i = 0; i < n1; i++) g_w3l.pairA[i] = g_w3l.pairB[i] = i;
            g_w3l.fr = g_w3l.frame; g_w3l.soft = 1; }
        else { W3FreeSoft(); g_w3l.frame = g_w3c.frame; g_w3l.frameB = g_w3c.frameB; g_w3l.pairA = g_w3c.pairA; g_w3l.pairB = g_w3c.pairB; } }
    g_w3lo = g_w3hi = -1; g_w3lnb = 0; g_w3snb = 0; R3_SetLive(&g_w3r, NULL, 0);
    LsStep(900, "Lights, grass and shadows");
    W3Names(); W3Lamps(); W3Casters(); W3GrassInit(); W3Hydrants();
    LsStep(970, NULL);
    LOG("world3d: %s: %d pieces, %d vertices, %d batches, %d textures", path, g_w3c.n, g_w3c.nv, g_w3nb, g_w3a.ntex);
    g_w3On = 1;
    return 1; }

/* where the game really is in the course file.  The file numbers the pieces along A; a rider who takes B travels B's own length (Sierra 20 pieces
   shorter, Peninsula 16, Pacific Highway 20 longer), so after the join (Highway: already on B) the game's road positions are off from the file's
   piece numbers by the difference, for the rest of the race.  Each frame the game's ring pieces from the camera on are matched against the file's
   (curve + hill; on B the game builds B as single pieces = the file's half B) and g_w3off (pieces) is kept / re-found; the camera's view position
   and the ring's back are moved by it, so everything that maps road positions to file pieces follows */
static int W3Eq(const RoadSection *a, const RoadSection *b) { return a->curve == b->curve && a->hill == b->hill; }
static int W3LiveMatch(const RoadPiece *lp, const RoadPiece *p, int k, int b)   /* b = 0: A's road (A is numbered as in the file), 1: B's road; -1 = not comparable */
{   if (!b) return lp->layout == 2 ? p->layout == 2 && W3Eq(&lp->halfA, &p->halfA) : W3Eq(&lp->halfA, &p->halfA);
    if (p->layout != 2 || (g_w3c.bdead && g_w3c.bdead[k])) return -1;   /* (past B's end) */
    return W3Eq(lp->layout == 2 ? &lp->halfB : &lp->halfA, &p->halfB); }   /* (on B the game builds B as single pieces) */
static int W3OffScore(int o, int kr, int i0, int cnt, int b)   /* pieces matched with offset o (-1 = a mismatch) */
{   int i, m = 0;
    for (i = i0; i < i0 + cnt && i < RRW_LIVE; i++) { const RoadPiece *lp = (const RoadPiece *)g_w3cam.ring[i]; int k = kr + i + o, r;
        if (!lp || k < 0 || k >= g_w3c.n) continue;
        if ((r = W3LiveMatch(lp, &g_w3c.piece[k], k, b)) < 0) continue;
        if (!r) return -1;
        m++; }
    return m; }
static int W3Refind(int *off, int kr, int i0, int b)   /* keep (no mismatch, a few pieces agree) or re-find (12 agree) the offset of road b; 0 = no match */
{   int d, o;
    if (W3OffScore(*off, kr, i0, 24, b) >= 2) return 1;
    if (b) { int i, c = 0; for (i = i0; i < i0 + 24 && i < RRW_LIVE; i++) { const RoadPiece *lp = (const RoadPiece *)g_w3cam.ring[i]; int k = kr + i + *off;   /* (B's last pieces in view:
            fewer than 12 to compare - kept, not looked for; looked for, the only 12 that matched lay back in B (an editor's fork: B straight, as A, near its ends), the camera
            thrown back there over and over, the rider never past the join) */
            if (lp && k >= 0 && k < g_w3c.n && W3LiveMatch(lp, &g_w3c.piece[k], k, b) >= 0) c++; }
        if (c < 12) return 1; }
    for (d = 1; d <= 64; d++) for (o = *off - d; o <= *off + d; o += 2 * d)
        if (W3OffScore(o, kr, i0, 24, b) >= 12) { LOG("world3d: road %c of the game is %d pieces off the file at piece %d (was %d)", 'A' + b, o, kr + i0 + o, *off); *off = o; return 1; }
    return 0; }
static void W3Track(void)
{   int kr = (g_w3cam.ringBack - g_w3Piece0) >> 8, kc = (g_w3cam.viewPos - g_w3Piece0) >> 8, i0 = kc - kr, useB = 0, k;
    if (i0 < 0) i0 = 0;
    if (i0 > RRW_LIVE - 24) i0 = RRW_LIVE - 24;
    W3Refind(&g_w3off, kr, i0, 0);
    if (g_w3cam.half) {   /* the camera on B: B's own numbering while B lasts (it differs from A's near the join when B is shorter / longer) */
        if (W3Refind(&g_w3offB, kr, i0, 1)) { k = kc + g_w3offB; useB = k >= 0 && k < g_w3c.n && g_w3c.piece[k].layout == 2 && !(g_w3c.bdead && g_w3c.bdead[k]); } }
    else g_w3offB = g_w3off;
    g_w3camB = useB; g_w3cam.viewPos += (useB ? g_w3offB : g_w3off) << 8; g_w3cam.ringBack += g_w3off << 8;
    if (getenv("RR_W3LOGT")) { static int nt; if ((++nt % atoi(getenv("RR_W3LOGT"))) == 0) LOG("w3track %d: view piece %d (game %d) offA %d offB %d camB %d half %d", nt, (g_w3cam.viewPos - g_w3Piece0) >> 8, kc, g_w3off, g_w3offB, g_w3camB, g_w3cam.half); } }   /* test switch RR_W3LOGT=<k> */

__declspec(dllexport) void __stdcall RR_World3DFrame(const RrWorld3DCam *c)
{   static unsigned n;
    if (!c || !g_w3On) return;
    g_w3cam = *c; g_w3HaveCam = 1;
    W3Track();
    ++n;
    if (getenv("RR_W3LOG") && (n % 150 == 0 || (getenv("RR_W3LOGN") && n % atoi(getenv("RR_W3LOGN")) == 0))) {   /* test switch: the game's camera every 150 frames (RR_W3LOGN=<k>: every k) */
        int k = (c->viewPos - g_w3Piece0) >> 8; const RoadSection *a = (k >= 0 && k < g_w3c.n) ? &g_w3c.piece[k].halfA : NULL;
        LOG("world3d cam %u: view %x (piece %d + %d/256) camX %d camY %d half %d centre %d horizon %d frame %dx%d; piece curve %d hill %d edges %d %d",
            n, c->viewPos, k, c->viewPos & 0xff, c->camX, c->camY, c->half, c->centreX, c->horizonY, c->frameW, c->frameH,
            a ? a->curve : 0, a ? a->hill : 0, a ? a->edgeL : 0, a ? a->edgeR : 0); } }

static void WxSound(int kind, float v);   /* weather.inc */
static void W3BloodClear(void);   /* below */
static void W3RaceEnd(void) { g_w3On = 0; g_w3HaveCam = 0; WxSound(0, 0); W3BloodClear(); }
static int g_w3ns;   /* sprites this scene (below) */
static int g_w3InTunnel;   /* the camera is under a roof (a tunnel piece): no rain / snow falls there (weather.inc WxParticles) */
static void W3SceneBegin(void) { g_w3ns = 0; }

/* ---- the polygons the game still draws (riders, traffic, pedestrians, shadows) go into the 3D pass at the depth of the view segment they were queued
   in (RrGpuQuad.depth; the segment's near edge, so a sprite never sinks into the road under it), in the game's order, depth-tested against the world:
   whatever stands in front of them in 3D (a building on a curve) hides them ---- */
typedef struct W3Spr { float x[4], y[4], u[4], v[4]; SDL_Texture *t; unsigned rgba; float depth, objDepth; double dObj;
                       int kind; float yaw, lat; unsigned id; const GpuTex *tx; char name[8]; unsigned paint, oflags; } W3Spr;   /* kind .. paint: RrGpuQuad.objKind .. and the
                                                                                                                       sprite's body colour (traffic models, W3Cars) */
static W3Spr *g_w3s; static int g_w3scap;
static int g_w3NoShadow;   /* test switch RR_W3NOSHADOW=1: the game's own rider shadows (W3Sprites) */
static int W3IsGameShadow(const W3Spr *s)   /* a polygon of the game's own shadow under a bike: queued by its shadow pass (RrGpuQuad.objFlags RRQ_SHADOW, rider_sprites.c) -
                                              all of them left out, the 3D world casts the riders' shadows itself (user, 2026-11-15: no more guessing them by shape) */
{   return (s->oflags & RRQ_SHADOW) != 0; }
/* the player's brake light: on only while the brake is held (RrGpuQuad.objFlags RRQ_BRAKE) - not from the bike slowing down, which a bump or a punch
   from another rider does too (user, 2026-11-15) */
static float W3PlayerBrake(unsigned id)
{   int j; for (j = 0; j < g_w3ns; j++) if (g_w3s[j].id == id && (g_w3s[j].oflags & RRQ_BRAKE)) return 1.0f;
    return 0.0f; }
/* test switch RR_W3LENSDUMP=<dir>: every distinct picture of the player's rider once, as lens_<n>_<hash>_<w>x<h>_<what>.bmp - the picture in the colours it is
   drawn in, with the brake light lens where the sprites pass puts it (pick: the red spot used, -1 = both, -2 = none in this picture; lens size as a share of
   the picture's width, as of the bike's), red spots not used outlined yellow.  what: lamp / nolamp / shadow */
static void W3LensDump(const W3Spr *t, const W3Red *rd, int pick, int shadow)
{   static DWORD seen[4096]; static int ns, nf; const char *dir = getenv("RR_W3LENSDUMP"); const GpuTex *tx = t->tx; PALETTEENTRY tp[256]; const PALETTEENTRY *pe;
    char fn[MAX_PATH]; FILE *o; BITMAPFILEHEADER bh; BITMAPINFOHEADER ih; DWORD *px; int i, x, y, w, h, e;
    if (!dir || !tx || !tx->bits || !g_primary || !g_primary->pal) return;
    for (i = 0; i < ns; i++) if (seen[i] == tx->hash) return;
    if (ns < 4096) seen[ns++] = tx->hash;
    w = tx->w; h = tx->h; pe = g_cdPal ? g_cdPal : g_primary->pal->e;
    if (tx->key & 2) { const DWORD *lut = RcTrueLut(); if (lut) { for (i = 0; i < 256; i++) { tp[i].peRed = (BYTE)(lut[i] >> 16); tp[i].peGreen = (BYTE)(lut[i] >> 8); tp[i].peBlue = (BYTE)lut[i]; } pe = tp; } }
    if (!(px = (DWORD *)malloc((size_t)w * h * 4))) return;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) { int p = tx->bits[y * (tx->pitch ? tx->pitch : tx->w) + x];
        px[y * w + x] = !p ? 0x404850u : ((DWORD)pe[p].peRed << 16) | ((DWORD)pe[p].peGreen << 8) | pe[p].peBlue; }
    if (tx->rspr >= 0 && tx->rspr < RL_SPRITES) { const RlLamp *L = &g_rlLamp[tx->rspr];   /* a rider sprite: its hardcoded lamp pixels, as drawn */
        if (pick != -2) for (y = 0; y < L->h; y++) for (x = 0; x < L->w; x++) if (g_rlRows[L->row + y] >> x & 1 && L->x + x < w && L->y + y < h) px[(L->y + y) * w + L->x + x] = 0xff2010u;
        rd = NULL; }
    for (e = 0; rd && e < rd->n; e++) {   /* the lens: a filled red rectangle (as drawn: 0.17 x 0.075 of the width, 0.6 of that for twin lamps), the others an outline */
        int on = pick == -1 || pick == e, cx = (int)(rd->u[e] * w), cy = (int)(rd->v[e] * h); float sc = pick == -1 ? 0.6f : 1.0f;
        int rx = (int)(w * 0.17f * sc + 0.5f), ry = (int)(w * 0.075f + 0.5f); if (rx < 1) rx = 1; if (ry < 1) ry = 1;
        for (y = cy - ry; y <= cy + ry; y++) for (x = cx - rx; x <= cx + rx; x++) { int edge = y == cy - ry || y == cy + ry || x == cx - rx || x == cx + rx;
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            if (on) px[y * w + x] = edge ? 0xffffffu : (y == cy ? 0xffd0c0u : 0xff2010u); else if (edge) px[y * w + x] = 0xffff00u; } }
    _snprintf(fn, sizeof fn, "%s\\lens_%04d_%08lx_%dx%d_%s.bmp", dir, nf++, (unsigned long)tx->hash, w, h, shadow ? "shadow" : pick == -2 ? "nolamp" : "lamp"); fn[sizeof fn - 1] = 0;
    if ((o = fopen(fn, "wb")) != NULL) {
        memset(&bh, 0, sizeof bh); memset(&ih, 0, sizeof ih); bh.bfType = 0x4d42; bh.bfOffBits = sizeof bh + sizeof ih; bh.bfSize = bh.bfOffBits + w * h * 4;
        ih.biSize = sizeof ih; ih.biWidth = w; ih.biHeight = -h; ih.biPlanes = 1; ih.biBitCount = 32;
        fwrite(&bh, sizeof bh, 1, o); fwrite(&ih, sizeof ih, 1, o); fwrite(px, 4, (size_t)w * h, o); fclose(o); }
    free(px); }
static unsigned W3CarPaint(const GpuTex *tx);   /* below */
static void WpSeePlayer(const GpuTex *tx);   /* weapons.inc */
static int W3Sprite(const RrGpuQuad *q, const GpuTex *tx)
{   W3Spr *s; int k;
    if (!g_w3On || q->depth < 0) return 0;
    if (g_w3ns == g_w3scap) { int nc = g_w3scap ? g_w3scap * 2 : 1024; W3Spr *n = (W3Spr *)realloc(g_w3s, sizeof(W3Spr) * nc); if (!n) return 0; g_w3s = n; g_w3scap = nc; }
    s = &g_w3s[g_w3ns++];
    for (k = 0; k < 4; k++) { s->x[k] = q->x[k]; s->y[k] = q->y[k]; s->u[k] = tx ? q->u[k] / tx->w : 0; s->v[k] = tx ? q->v[k] / tx->h : 0; }
    s->t = tx ? tx->lv[0] : NULL; s->depth = q->depth; s->objDepth = q->objDepth; s->rgba = 0xffffffffu;
    s->kind = q->objDepth >= 0 ? q->objKind : RRO_NONE; s->yaw = q->objYaw; s->lat = q->objLat; s->id = q->objId; s->tx = tx; memcpy(s->name, q->name, sizeof s->name); s->oflags = q->objFlags;
    s->paint = (s->kind == RRO_CAR && tx) ? W3CarPaint(tx) : 0;   /* now, while the bitmap is the one this polygon shows */
    if (s->kind == RRO_PLAYER) WpSeePlayer(tx);   /* (weapons.inc: test log) */
    if (!tx) { PALETTEENTRY c; memset(&c, 0, sizeof c); if (g_primary && g_primary->pal) c = g_primary->pal->e[q->colour & 255];
        s->rgba = 0xff000000u | ((unsigned)c.peBlue << 16) | ((unsigned)c.peGreen << 8) | c.peRed; }
    return 1; }
/* the road ahead on screen this frame (W3Draw): every 1/8 segment from the eye, the frame row of the road's middle and its distance.  The game places
   its sprites with its own projection, which squeezes the distance (things further out are drawn lower / larger than true 3D would place them at that
   depth), so a sprite at its segment's depth would sink into the 3D road under it: it is put no further than the road at its lowest screen row */
enum { W3_ROWS = 33 * 8 };
static float g_w3rowY[W3_ROWS], g_w3rowD[W3_ROWS]; static int g_w3nrow;
static double W3RoadDistAt(double ybottom)
{   int j;
    for (j = 0; j < g_w3nrow; j++) if (g_w3rowD[j] > 0 && g_w3rowY[j] <= ybottom) return g_w3rowD[j];
    return 1e30; }
/* the frames of the carriageway the view follows (RrWorld3DCam.half: B has its own, C3_FramesB) - the game's camera and projection are relative to it */
static const C3Frame *W3Fr(void) { return g_w3camB && g_w3c.frameB ? g_w3c.frameB : g_w3c.frame; }   /* (on B: the precomputed B path, course3d.h C3_FramesB) */
/* a point of the road as C3_PointF, on a smooth curve through the pieces' ends (Catmull-Rom; *hd its heading there) - C3_PointF's straight pieces kink at every end,
   up to 8 degrees on a sharp bend, and the camera and the player's bike, on the road at two places, kinked at different times: the bike jolted across the picture
   (user, 2026-11-19: "on extreme turns the shakes are pretty bad, why are we shaking at all?").  Off the drawn road by a few units at most */
static void W3SmoothPt(const C3Frame *F, double p, double u, double h, float *out, double *hd)
{   int n = g_w3c.n, k = (int)floor(p), i, kk[4]; double f, x[4], y[4], z[4], f2, f3, a, b, c, d, da, db, dc, dd, X, Y, Z, DX, DZ, th;
    if (k < 0) k = 0; if (k >= n) k = n - 1; f = p - k;
    for (i = 0; i < 4; i++) { kk[i] = k - 1 + i; if (kk[i] < 0) kk[i] = 0; if (kk[i] > n) kk[i] = n; x[i] = F[kk[i]].x; y[i] = F[kk[i]].y; z[i] = F[kk[i]].z; }
    f2 = f * f; f3 = f2 * f;
    a = -0.5 * f3 + f2 - 0.5 * f; b = 1.5 * f3 - 2.5 * f2 + 1; c = -1.5 * f3 + 2 * f2 + 0.5 * f; d = 0.5 * f3 - 0.5 * f2;
    da = -1.5 * f2 + 2 * f - 0.5; db = 4.5 * f2 - 5 * f; dc = -4.5 * f2 + 4 * f + 0.5; dd = 1.5 * f2 - f;
    X = a * x[0] + b * x[1] + c * x[2] + d * x[3]; Y = a * y[0] + b * y[1] + c * y[2] + d * y[3]; Z = a * z[0] + b * z[1] + c * z[2] + d * z[3];
    DX = da * x[0] + db * x[1] + dc * x[2] + dd * x[3]; DZ = da * z[0] + db * z[1] + dc * z[2] + dd * z[3];
    th = (DX * DX + DZ * DZ > 1e-6) ? atan2(DX, DZ) : F[k].heading;
    out[0] = (float)(X + u * cos(th)); out[1] = (float)(Y + h); out[2] = (float)(Z - u * sin(th)); if (hd) *hd = th; }
static double W3MidB(double q)   /* the middle of carriageway B's surface, in its own lateral coordinates */
{   int k = (int)floor(q); const RoadSection *b;
    if (k < 0) k = 0; if (k >= g_w3c.n) k = g_w3c.n - 1;
    b = g_w3c.piece[k].layout == 2 ? &g_w3c.piece[k].halfB : &g_w3c.piece[k].halfA;
    return (b->edgeL + b->edgeR) * 0.5; }

/* the original's projection at depth D (segments) from the view segments' lane records (RrWorld3DCam.seg): screen x of lateral 0, road surface y and
   pixels per lateral unit, in frame pixels; 1 / scale (~ distance) and the screen positions interpolated linearly between segments */
static int W3OrigAt(double D, double *cx, double *ry, double *s)
{   int i, n = g_w3cam.nSeg; double ppu = (g_w3cam.pxPerUnit > 0 ? g_w3cam.pxPerUnit : 1) / 65536.0, d0, d1, t, i0, i1;
    if (n < 2) return 0;
    for (i = 0; i + 2 < n && g_w3cam.seg[i + 1].depth / 256.0 < D; i++) {}
    d0 = g_w3cam.seg[i].depth / 256.0; d1 = g_w3cam.seg[i + 1].depth / 256.0;
    if (d1 <= d0 || g_w3cam.seg[i].s <= 0 || g_w3cam.seg[i + 1].s <= 0) return 0;
    t = (D - d0) / (d1 - d0);
    i0 = 1.0 / g_w3cam.seg[i].s; i1 = 1.0 / g_w3cam.seg[i + 1].s;
    if (i0 + (i1 - i0) * t <= 0) return 0;
    *s = ppu / (i0 + (i1 - i0) * t);
    /* the road's centre and height between the rows: interpolated in road units (screen offset / scale), not on the screen - a straight screen line
       between two rows is no line on the road; near the eye, on a bend, that put a rider up to ~90 units beside where the game has him, a sway
       growing over each segment and snapping back at the next (user: "the bike is swaying left and right ... on certain stretches") */
    { static int old = -1; if (old < 0) old = getenv("RR_W3OLDMAP") != NULL;   /* (test switch RR_W3OLDMAP=1: the old on-screen interpolation, for comparison) */
      if (old) { *cx = ppu * (g_w3cam.seg[i].cx + (g_w3cam.seg[i + 1].cx - g_w3cam.seg[i].cx) * t); *ry = ppu * (g_w3cam.seg[i].ry + (g_w3cam.seg[i + 1].ry - g_w3cam.seg[i].ry) * t); return *s > 0; } }
    *cx = (g_w3cam.seg[i].cx * i0 + (g_w3cam.seg[i + 1].cx * i1 - g_w3cam.seg[i].cx * i0) * t) * *s;
    *ry = (g_w3cam.seg[i].ry * i0 + (g_w3cam.seg[i + 1].ry * i1 - g_w3cam.seg[i].ry * i0) * t) * *s;
    return *s > 0; }

/* the player's bike across the road: as far from the game's as the smoothed camera is from the game's camera (the camera code below) - the camera smoothed alone,
   the bike drawn where the game has it, the bike ran ahead of the camera as it slid out in a bend and swung back on the screen as the camera caught up
   (user, 2026-11-19: "why is the bike zig zagging so dramatically while the camera is following a turn") */
static double g_w3bikeOff, g_w3bikeDx;   /* (g_w3bikeDx: W3Place) */
/* the view this frame (the camera code): eye x, z, heading, its lateral and piece position - the player's bike is placed straight ahead of it, across by its lateral
   only, not round the road's bend (user, 2026-11-20: "the bike is definitely not drifting smoothly can you try disable this first just so i can see whats going on") -
   test switch RR_W3BIKECURVE=1: on the road's curve one piece ahead */
static double g_w3view[5];
/* an object's polygons (RrGpuQuad.objDepth): every vertex from the game's projection at the object's depth (lateral U, height H over the road there)
   to the same point of the true-3D course, so riders and cars stand on the 3D road, hidden by a crest or a building like the world */
static int W3MapObj(const W3Spr *s, int w, int h, const float *m, double vp, float *ox, float *oy, double *dist)
{   double cx, ry, sc, q = vp + s->objDepth - 1.0, dmin = 1e30, U[4], H[4], lift = 0; int k;
    if (!W3OrigAt(s->objDepth, &cx, &ry, &sc)) return 0;
    for (k = 0; k < 4; k++) { U[k] = (s->x[k] - cx) / sc; H[k] = (ry - s->y[k]) / sc; }
    if (s->kind == RRO_PLAYER) { double mu = (U[0] + U[1] + U[2] + U[3]) / 4; if (fabs(s->lat - mu) < 60) for (k = 0; k < 4; k++) U[k] += s->lat - mu; for (k = 0; k < 4; k++) U[k] += g_w3bikeOff; }   /* (near it only: on a split road's B
                                                       the game's lateral can be from A's centre) */   /* (the player: across the road where the game has it
                                                       (its lateral) - from the game's whole-pixel projection, its scale rising and falling with each piece, the bike shook a unit or two
                                                       every frame: user, 2026-11-19 "the player bike is abit awkward it wobbles around") */
    {   /* on top of the 3D terrain: the game's height is over ITS road (flat across); beside the road the 3D hillside can rise above that (pedestrians,
           signs, riders off the road sank into it).  The foot = the lowest corner; where the 3D ground under it (C3_GroundAt: the live copy inside the
           live stretch) is higher than the foot plus a tolerance (the game sinks car pictures 40..80 units into the road), the object is lifted onto it */
        int kf = 0, kp = (int)floor(q), half = g_w3camB ? 1 : 0; double fu, g; C3Course *cc;
        for (k = 1; k < 4; k++) if (H[k] < H[kf]) kf = k;
        fu = 0; for (k = 0; k < 4; k++) fu += U[k]; fu /= 4;   /* (its middle across) */
        cc = (g_w3l.piece && kp >= g_w3lo && kp < g_w3hi) ? &g_w3l : &g_w3c;
        if (kp >= 0 && kp < g_w3c.n - 1 && (!half || cc->piece[kp].layout == 2)) {
            g = C3_GroundAt(cc, kp, half && cc->piece[kp].layout == 2, fu);
            if (g > H[kf] + 80) lift = g - H[kf]; } }
    {   static double lastVp = -1; int logb = s->kind == RRO_PLAYER && getenv("RR_W3LOGBIKE") && vp != lastVp;   /* (test switch RR_W3LOGBIKE=1: the player's lateral as mapped (U) against the game's own (lat), and where it lands on the screen, each frame) */
    for (k = 0; k < 4; k++) { float pt[3]; double X, Y, W;
        static int curve = -1; if (curve < 0) curve = getenv("RR_W3BIKECURVE") != NULL;
        if (s->kind == RRO_PLAYER && !curve) { double c = cos(g_w3view[2]), sn = sin(g_w3view[2]), a = (q - g_w3view[4]) * W3_L, x = U[k] - g_w3view[3];
            W3SmoothPt(W3Fr(), q, U[k], H[k] + lift, pt, NULL);   /* (its height: the road's there) */
            pt[0] = (float)(g_w3view[0] + sn * a + c * x); pt[2] = (float)(g_w3view[1] + c * a - sn * x); }
        else if (s->kind == RRO_PLAYER) W3SmoothPt(W3Fr(), q, U[k], H[k] + lift, pt, NULL); else C3_PointF(&g_w3c, W3Fr(), q, U[k], H[k] + lift, pt);   /* (the player: on the smooth curve,
                                                                                                                                                      as the camera - W3SmoothPt) */
        X = pt[0] * m[0] + pt[1] * m[4] + pt[2] * m[8] + m[12];
        Y = pt[0] * m[1] + pt[1] * m[5] + pt[2] * m[9] + m[13];
        W = pt[0] * m[3] + pt[1] * m[7] + pt[2] * m[11] + m[15];
        if (W < 1) return 0;   /* behind the eye: leave it to the game's placement */
        ox[k] = (float)((X / W + 1.0) * 0.5 * w); oy[k] = (float)((1.0 - Y / W) * 0.5 * h);
        if (W < dmin) dmin = W; }
    if (logb) { double mx = (s->x[0] + s->x[1] + s->x[2] + s->x[3]) / 4, fu2 = (U[0] + U[1] + U[2] + U[3]) / 4; lastVp = vp;
        LOG("w3bike vp %.3f D %.3f U %.1f lat %.1f diff %.1f mx %.1f cx %.1f sc %.4f camX %d sx %.1f", vp, s->objDepth, fu2, s->lat, fu2 - s->lat, mx, cx, sc, g_w3cam.camX, (ox[0] + ox[1] + ox[2] + ox[3]) / 4); } }
    *dist = dmin;
    return 1; }

/* ---- traffic as 3D cars (Video Options > Traffic = 3D models; the models: src\platform\world3d\car3d.h) ----
   Every polygon a traffic car's draw hook queued arrives as a W3Spr with kind RRO_CAR, the car's record id, lateral position and heading
   (gpu_api.h).  W3Cars builds one model per car into the world pass (real depth, fog and fades like the buildings); W3Sprites then leaves those
   polygons out.  Test switch RR_CARS3D=0 / 1 overrides the setting; RR_W3LOGC=1 logs the cars' numbers (texture, depth, size on the road). */
#include "../world3d/car3d.h"
static int W3CarsOn(void) { const char *e = getenv("RR_CARS3D"); return (e && *e) ? *e != '0' : 0; }   /* traffic as 3D models: off (the car sprites, upscaled with Video Options > Sprites); RR_CARS3D=1 turns the models on */

/* paint of a car sprite bitmap, cached by the bitmap's hash (Car3D_Paint over the course file's full palette: the live one is dark during fades) */
static unsigned W3CarPaint(const GpuTex *tx)
{   static struct { DWORD hash; unsigned c; } cache[256]; static int n; int i;
    if (!tx->bits) return 0;
    for (i = 0; i < n; i++) if (cache[i].hash == tx->hash) return cache[i].c;
    i = n < 256 ? n++ : (int)(tx->hash & 255);
    cache[i].hash = tx->hash; cache[i].c = Car3D_Paint(tx->bits, tx->w, tx->h, tx->pitch ? tx->pitch : tx->w, g_w3a.pal, tx->key);
    return cache[i].c; }

/* a sprite family = the texture name without its view code (the game names a car's pictures <family><letter><frame>: 'TAXIG06', 'MZDRX7S0'), with
   what its sprites showed on the road so far (only from 3 to 28 segments away: nearer the game's scale is clamped, further the pictures are tiny).
   The model's size, from the car's heading against the road (the camera looks along it): pictures from behind / ahead give the width, from the
   side the length (kept to 1.8 .. 2.4 widths), all of them the height (the whole picture: the game sinks a car's picture some 40..80 units into
   the road; x 0.85 because the camera sees the roof from above).  Averages of the cars 4 .. 28 segments away (nearer the game's scale is
   clamped), so one odd frame does not matter.  Before a side view: 2.1 widths long.  The paint: the first sprite with one.
   The shape (Car3D_Style) is settled after 24 views; the family's texture (Car3D_MakeTexture) is made when it has a paint and again if the
   shape changes before it settles (and in every race: the renderer's textures are dropped at the race start, g_w3texGen) */
typedef struct W3CarFam { char name[9]; double wS, lS, hS; int wN, lN, hN; unsigned paint; int seen;
                          int shape, taxi, locked; int tex, texGen, texShape, texTaxi; unsigned texPaint; } W3CarFam;
static W3CarFam g_w3fam[64]; static int g_w3nfam;
static W3CarFam *W3CarFamily(const char *nm8)
{   char f[9]; int n = 0, i;
    while (n < 8 && nm8[n]) { f[n] = nm8[n]; n++; }
    while (n > 0 && f[n - 1] >= '0' && f[n - 1] <= '9') n--;   /* the frame number */
    if (n > 1) n--;                                              /* the view letter */
    f[n] = 0;
    for (i = 0; i < g_w3nfam; i++) if (!strcmp(g_w3fam[i].name, f)) return &g_w3fam[i];
    if (g_w3nfam == 64) return &g_w3fam[63];
    memset(&g_w3fam[g_w3nfam], 0, sizeof g_w3fam[0]); strcpy(g_w3fam[g_w3nfam].name, f); g_w3fam[g_w3nfam].tex = -1;
    return &g_w3fam[g_w3nfam++]; }

/* the family's texture in the renderer (made / remade as described above); -1 = none (flat grey then) */
static int W3CarTexture(W3CarFam *fam, const Car3D *car)
{   unsigned *rgba; unsigned h = 2166136261u; const char *p;
    if (fam->tex >= 0 && fam->texGen == g_w3texGen && fam->texShape == car->shape && fam->texTaxi == car->taxi && fam->texPaint == car->body) return fam->tex;
    if (!(rgba = (unsigned *)malloc(sizeof(unsigned) * CAR3D_TEX * CAR3D_TEX))) return -1;
    for (p = fam->name; *p; p++) h = (h ^ (unsigned char)*p) * 16777619u;
    { Car3D c = *car; c.seed = h; Car3D_MakeTexture(rgba, &c); }
    fam->tex = R3_Texture(&g_w3r, rgba, CAR3D_TEX, CAR3D_TEX); free(rgba);
    fam->texGen = g_w3texGen; fam->texShape = car->shape; fam->texTaxi = car->taxi; fam->texPaint = car->body;
    return fam->tex; }

/* local car coordinates -> world: turned by the heading, then bent along the road from the car's road position (q, lateral u0) */
typedef struct W3CarPose { double q, u0, c, s, L; } W3CarPose;
static void W3CarXf(void *ctx, const float *l, float *out)
{   const W3CarPose *p = (const W3CarPose *)ctx;
    double u = l[0] * p->c + l[2] * p->s, a = -l[0] * p->s + l[2] * p->c;   /* x right, z forward; yaw 0 = along the road, 0.5 = oncoming */
    C3_PointF(&g_w3c, W3Fr(), p->q + a / p->L, p->u0 + u, l[1], out); }

static float W3Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
/* the models of this frame's cars into v (capacity cap vertices), one batch per family texture into b (capacity nbMax); returns the batches.
   First every car's family, size and place, then the cars family by family (a batch draws one texture) */
typedef struct W3Car { W3CarFam *fam; Car3D car; W3CarPose pose; } W3Car;
static int W3Cars(R3Vert *v, int cap, R3Batch *b, int nbMax, double vp)
{   Car3DOut o; int i, j, k, nc = 0, nb = 0; static int frames; int logNow = getenv("RR_W3LOGC") && (++frames % 200) == 0; static W3Car cars[64];
    o.v = v; o.n = 0; o.cap = cap; o.xf = W3CarXf;
    for (i = 0; i < g_w3ns && nc < 64; i++) {
        const W3Spr *s = &g_w3s[i]; double cx, ry, sc, x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9, bestA = -1; unsigned paint = 0; const char *nm = s->name;
        W3CarFam *fam; Car3D car; W3CarPose pose;
        if (s->kind != RRO_CAR || !s->tx) continue;
        for (j = 0; j < i; j++) if (g_w3s[j].kind == RRO_CAR && g_w3s[j].tx && g_w3s[j].id == s->id) break;
        if (j < i) continue;   /* this car is built already */
        for (j = i; j < g_w3ns; j++) {   /* all its textured polygons: their box on screen, the biggest one's name, a paint */
            const W3Spr *t = &g_w3s[j]; double a, tx0 = 1e9, tx1 = -1e9, ty0 = 1e9, ty1 = -1e9;
            if (t->kind != RRO_CAR || !t->tx || t->id != s->id) continue;
            for (k = 0; k < 4; k++) { if (t->x[k] < tx0) tx0 = t->x[k]; if (t->x[k] > tx1) tx1 = t->x[k]; if (t->y[k] < ty0) ty0 = t->y[k]; if (t->y[k] > ty1) ty1 = t->y[k]; }
            if (tx0 < x0) x0 = tx0; if (tx1 > x1) x1 = tx1; if (ty0 < y0) y0 = ty0; if (ty1 > y1) y1 = ty1;
            a = (tx1 - tx0) * (ty1 - ty0); if (a > bestA) { bestA = a; nm = t->name; }
            if (!paint) paint = t->paint; }
        if (!W3OrigAt(s->objDepth, &cx, &ry, &sc)) continue;
        fam = W3CarFamily(nm);
        {   double wU = (x1 - x0) / sc, hU = (y1 - y0) / sc * 0.85, side = fabs(sin(s->yaw * 2 * 3.14159265358979));   /* picture size on the road */
            if (s->objDepth >= 4 && s->objDepth <= 28) {
                if (side < 0.25) { fam->wS += wU; fam->wN++; }        /* from behind / ahead: the width */
                else if (side > 0.85) { fam->lS += wU; fam->lN++; }   /* from the side: the length */
                fam->hS += hU; fam->hN++;
                fam->seen++; }
            /* no view from behind / ahead yet: this picture's width, less its length's share from an angle */
            car.W = W3Clamp(fam->wN ? (float)(fam->wS / fam->wN) : (float)(wU / (1 + 1.3 * side)), 140, 230);
            if (!fam->hN) { fam->hS = hU; fam->hN = 1; } }
        if (!fam->paint && paint) fam->paint = paint;
        car.L = fam->lN ? W3Clamp((float)(fam->lS / fam->lN), car.W * 1.8f, car.W * 2.4f) : car.W * 2.1f;   /* the game's cars are chunky: ~2 widths long */
        car.H = W3Clamp((float)(fam->hS / (fam->hN ? fam->hN : 1)), 85, 240);
        car.body = fam->paint ? fam->paint : 0xff909090u; car.seed = 0;
        if (fam->locked) { car.shape = fam->shape; car.taxi = fam->taxi; }
        else { Car3D_Style(&car, fam->name); fam->shape = car.shape; fam->taxi = car.taxi; fam->locked = fam->seen >= 24 && fam->paint; }
        if (logNow) LOG("w3 car model '%s' (%d views) L %.0f W %.0f H %.0f paint %06x shape %d taxi %d", fam->name, fam->seen, car.L, car.W, car.H, car.body & 0xffffff, car.shape, car.taxi);
        pose.q = vp + s->objDepth - 1.0; pose.u0 = s->lat; pose.L = g_w3c.L;   /* as W3MapObj: the view builder puts depth k at piece position vp + k - 1 */
        pose.c = cos(s->yaw * 2 * 3.14159265358979); pose.s = sin(s->yaw * 2 * 3.14159265358979);
        cars[nc].fam = fam; cars[nc].car = car; cars[nc].pose = pose; nc++;
    }
    for (i = 0; i < nc && nb < nbMax; i++) {   /* family by family */
        int first = o.n, t;
        for (j = 0; j < i; j++) if (cars[j].fam == cars[i].fam) break;
        if (j < i) continue;
        t = W3CarTexture(cars[i].fam, &cars[i].car);
        for (j = i; j < nc; j++) if (cars[j].fam == cars[i].fam) { o.ctx = &cars[j].pose; Car3D_Build(&o, &cars[j].car); }
        b[nb].first = first; b[nb].count = o.n - first; b[nb].tex = t; b[nb].alpha = 0; nb++;
    }
    return nb; }

/* ---- trams (user, 2026-11-20: "for the road, can we add tram tracks please, which a tram can spawn on. the tram probably should be 3d because it will be long.
   also when its near the player it should make a ding ding noise") ----
   A lane painted TRAM in the Course Editor (ground_art.h k_c3Road; RrcExtra.road / lane) is a tram line: each run of pieces with it, lane by lane, has a tram every
   W3_TRAMGAP pieces going the lane's way (left of the centre line oncoming), W3_TRAMV pieces a game frame, back to the run's start at its end.  The tram: a box
   W3_TRAML long bent along the road (W3CarXf, as the cars), its sides / ends from <mod>\hd\tram\SIDE.png (1024 x 256) / FRONT.png (256 x 256) - drawn ones
   without them.  Nearer than W3_TRAMDING pieces to the player: its bell (rrdsound.dll RR_Ding), once each time one comes near.  Not solid: the riders go through */
#define W3_TRAML 2600.0
#define W3_TRAMW 230.0
#define W3_TRAMH 330.0
#define W3_TRAMV 0.028
#define W3_TRAMGAP 120
#define W3_TRAMDING 8.0
#define W3_TRAMVX (16 * 8 * 30 * 3)   /* (room for the trams in sight: 16 x 8 slices x 30 vertices, x 3) */
typedef struct W3TramRun { int k0, k1, j, dir; } W3TramRun;
static W3TramRun g_w3tr[64]; static int g_w3ntr = -1; static const void *g_w3trExt; static int g_w3trN;
static struct { double mid, u; } g_w3tm[256]; static int g_w3ntm;   /* (every tram this frame: its middle (piece position) and lateral - RR_TramAt) */
static int W3TramTex(void)   /* the atlas: side (v 0 .. 0.5), front (u 0 .. 0.25, v 0.5 .. 1), roof grey (u 0.25 .. 1, v 0.5 .. 1) */
{   static int tex = -1, gen = -1; unsigned *a, *sd = NULL, *fr = NULL; int x, y, d;
    if (tex >= 0 && gen == g_w3texGen) return tex;
    if (!(a = (unsigned *)malloc(1024 * 512 * 4))) return -1;
    if (g_mwNDir < 0) MwDirs();
    for (d = 0; d < g_mwNDir && !sd; d++) { char p[MAX_PATH]; _snprintf(p, sizeof p - 1, "%s\\hd\\tram\\SIDE.png", g_mwDir[d]); p[sizeof p - 1] = 0; if ((sd = C3G_LoadPng(p, 1024, 256)) != NULL) {
        _snprintf(p, sizeof p - 1, "%s\\hd\\tram\\FRONT.png", g_mwDir[d]); p[sizeof p - 1] = 0; fr = C3G_LoadPng(p, 256, 256); } }
    for (y = 0; y < 512; y++) for (x = 0; x < 1024; x++) { unsigned c;
        if (y < 256) c = sd ? sd[y * 1024 + x] : (y < 40 ? 0xff3a3a3eu : y < 130 ? ((x % 92) < 70 && y > 60 ? 0xff3a2c24u : 0xffc4dee8u) : y > 226 ? 0xff202022u : 0xff2428c8u);   /* (drawn: cream band, windows, red body - ABGR) */
        else if (x < 256) c = fr ? fr[(y - 256) * 256 + x] : (y < 400 && y > 290 && x > 30 && x < 226 ? 0xff3a2c24u : 0xff2428c8u);
        else c = 0xff6a6a6eu;
        a[y * 1024 + x] = c; }
    free(sd); free(fr);
    tex = R3_Texture(&g_w3r, a, 1024, 512); gen = g_w3texGen; free(a); return tex; }
static void W3TramScan(void)   /* the course's tram lines (again when the course or its edits change) */
{   int k, j, tram = -1, i;
    g_w3ntr = 0; g_w3trExt = g_w3a.ext; g_w3trN = g_w3c.n;
    for (i = 0; i < g_w3a.nroad; i++) if (!strcmp(g_w3a.tex[g_w3a.road0 + i].name, "TRAM")) tram = i;
    if (tram < 0 || !g_w3a.ext) return;
    for (j = 0; j < 8; j++) { int run = -1;
        for (k = 0; k <= g_w3c.n && k < g_w3a.next; k++) { int on = 0;
            if (k < g_w3c.n) { const RoadSection *s = &g_w3c.piece[k].halfA; unsigned rv;
                if (j < (int)s->lanesL + (int)s->lanesR) { rv = g_w3a.ext[k].lane[0][j] ? g_w3a.ext[k].lane[0][j] : g_w3a.ext[k].road[0][j < (int)s->lanesL ? 0 : 1];
                    on = (rv & (RRC_BAND | RRC_GEN | RRC_TEXG)) == (RRC_BAND | RRC_GEN) && (int)(rv & 0xff) == tram; } }
            if (on && run < 0) run = k;
            if (!on && run >= 0) { if (k - run >= 8 && g_w3ntr < 64) { W3TramRun *r = &g_w3tr[g_w3ntr++]; r->k0 = run; r->k1 = k - 1; r->j = j; r->dir = j < (int)g_w3c.piece[run].halfA.lanesL ? -1 : 1; } run = -1; } } }
    LOG("world3d: %d tram line(s)", g_w3ntr); }
static double W3LaneMid(int k, int j)   /* lane j's middle across piece k (C3_Lanes' grid) */
{   const RoadSection *s = &g_w3c.piece[k < 0 ? 0 : k >= g_w3c.n ? g_w3c.n - 1 : k].halfA;
    return -250.0 * s->lanesL + s->laneEdgeL + s->laneL * (250.0 / 256) + (j + 0.5) * 250; }
static int W3Trams(R3Vert *v, int cap, double vp)   /* this frame's trams into v (W3TramTex's texture); returns the vertices */
{   Car3DOut o; int r, t, tex; static unsigned char near_[64][16]; double pl = vp + 1.0, L = g_w3c.L, TL = W3_TRAML / L;
    if (g_w3ntr < 0 || g_w3trExt != g_w3a.ext || g_w3trN != g_w3c.n) W3TramScan();
    if (!g_w3ntr || (tex = W3TramTex()) < 0) return 0;
    o.v = v; o.n = 0; o.cap = cap; o.xf = W3CarXf; g_w3ntm = 0;
    for (r = 0; r < g_w3ntr; r++) { const W3TramRun *R = &g_w3tr[r]; int len = R->k1 - R->k0 + 1, nt = len / W3_TRAMGAP + 1;
        if (len < TL + 2) continue;   /* (a run shorter than a tram: none) */
        for (t = 0; t < nt && t < 16; t++) {   /* (each tram wholly on its run - from it entering at the start to it leaving at the end; at the race start half a gap in:
                                                  not on the start line - user's test, 2026-11-20: one crashed the player on the grid) */
            double span = len - TL, ph = fmod(t * span / nt + span / nt * 0.5 + g_frame * W3_TRAMV, span), q = R->dir > 0 ? R->k0 + TL + ph : R->k1 + 1 - TL - ph, mid = q - R->dir * TL * 0.5, d = fabs(mid - pl);
            W3CarPose pose; int s, NS = 8; static const float uvr[3][4] = { { 0, 0, 1, 0.5f }, { 0, 0.5f, 0.25f, 1 }, { 0.25f, 0.5f, 1, 1 } };
            if (d < W3_TRAMDING && !near_[r][t]) { static void (__stdcall *ding)(float, float); near_[r][t] = 1;   /* the bell, once as it comes near */
                if (!ding) { HMODULE m = GetModuleHandleA("rrdsound.dll"); ding = m ? (void (__stdcall *)(float, float))GetProcAddress(m, "RR_Ding") : 0; }
                LOG("world3d: tram %d.%d near the player (%.1f pieces) - its bell", r, t, d);
                if (ding && !g_vwHold) { double lat = 0; int i; for (i = 0; i < g_w3ns; i++) if (g_w3s[i].kind == RRO_PLAYER) { lat = g_w3s[i].lat; break; }
                    ding((float)(1.0 - d / (W3_TRAMDING * 1.6)), (float)(0.5 + (W3LaneMid((int)mid, R->j) - lat) / 1200.0)); } }
            else if (d > W3_TRAMDING * 2.5) near_[r][t] = 0;
            if (g_w3ntm < 256) { g_w3tm[g_w3ntm].mid = mid; g_w3tm[g_w3ntm].u = W3LaneMid((int)mid, R->j); g_w3ntm++; }   /* (for RR_TramAt) */
            if (mid < vp - 4 || mid > vp + 140) continue;   /* (out of sight) */
            {   double um = W3LaneMid((int)mid, R->j), lat = 0, cp = vp + W3_EYE - 1.0; int i;   /* (not solid - the riders go through: while the camera or the player's bike is
                                                                                                    inside it, not drawn, rather than the screen filled with it) */
                for (i = 0; i < g_w3ns; i++) if (g_w3s[i].kind == RRO_PLAYER) { lat = g_w3s[i].lat; break; }
                if ((fabs(mid - cp) < TL * 0.5 + 0.4 && fabs(um - g_w3cam.camX) < W3_TRAMW * 0.5 + 80) || (fabs(mid - pl) < TL * 0.5 + 0.2 && fabs(um - lat) < W3_TRAMW * 0.5 + 60)) continue; }
            pose.q = mid; pose.u0 = W3LaneMid((int)mid, R->j); pose.L = L; pose.c = R->dir > 0 ? 1 : -1; pose.s = 0; o.ctx = &pose;
            for (s = 0; s < NS; s++) {   /* slices along it (bent with the road): sides, roof; the ends at the first / last */
                float z0 = (float)(-W3_TRAML * 0.5 + W3_TRAML * s / NS), z1 = (float)(-W3_TRAML * 0.5 + W3_TRAML * (s + 1) / NS), hw = (float)(W3_TRAMW * 0.5), y0 = 25, y1 = (float)W3_TRAMH;
                float u0 = uvr[0][0] + (uvr[0][2] - uvr[0][0]) * s / NS, u1 = uvr[0][0] + (uvr[0][2] - uvr[0][0]) * (s + 1) / NS, nR[3] = { 1, 0, 0 }, nL[3] = { -1, 0, 0 }, nU[3] = { 0, 1, 0 };
                float A[3] = { hw, y0, z0 }, B[3] = { hw, y0, z1 }, C[3] = { hw, y1, z1 }, D[3] = { hw, y1, z0 }, E[3] = { -hw, y0, z0 }, F[3] = { -hw, y0, z1 }, G[3] = { -hw, y1, z1 }, H2[3] = { -hw, y1, z0 };
                float ta[2] = { u0, 0.5f }, tb[2] = { u1, 0.5f }, tc[2] = { u1, 0 }, td[2] = { u0, 0 }, ra2[2] = { 0.3f, 0.6f }, rb[2] = { 0.9f, 0.6f }, rc[2] = { 0.9f, 0.95f }, rd[2] = { 0.3f, 0.95f };
                Car3D_Quad(&o, A, B, C, D, ta, tb, tc, td, nR); Car3D_Quad(&o, F, E, H2, G, tb, ta, td, tc, nL);   /* (each side reads front -> back the same way) */
                Car3D_Quad(&o, D, C, G, H2, ra2, rb, rc, rd, nU);
                if (s == 0 || s == NS - 1) { float z = s == 0 ? z0 : z1, P0[3] = { -hw, y0, z }, P1[3] = { hw, y0, z }, P2[3] = { hw, y1, z }, P3[3] = { -hw, y1, z }, nz[3] = { 0, 0, s == 0 ? -1.0f : 1.0f };
                    float f0[2] = { uvr[1][0], uvr[1][3] }, f1[2] = { uvr[1][2], uvr[1][3] }, f2[2] = { uvr[1][2], uvr[1][1] }, f3[2] = { uvr[1][0], uvr[1][1] };
                    if (s == 0) Car3D_Quad(&o, P1, P0, P3, P2, f0, f1, f2, f3, nz); else Car3D_Quad(&o, P0, P1, P2, P3, f0, f1, f2, f3, nz); } } } }
    return o.n; }
/* a speed bump crossed (race\player_control.c, rr_CourseEditBump): RR_Bump(strength 0..1) - the camera jolted up and down, dying away in ~14 game frames */
static float g_w3bumpS; static unsigned long g_w3bumpF;
__declspec(dllexport) void __stdcall RR_Bump(float s) { g_w3bumpS = 0.3f + 0.7f * (s < 0 ? 0 : s > 1 ? 1 : s); g_w3bumpF = (unsigned long)g_frame; LOG("world3d: a speed bump crossed (%.2f)", s); }
/* the trams are solid (user, 2026-11-20): RR_TramAt(road position, lateral) = 1 when that point is inside a tram as last drawn - the game's player control
   (race\player_control.c) crashes the player there */
__declspec(dllexport) int __stdcall RR_TramAt(unsigned pos, int lat)
{   double q = ((double)pos - g_w3Piece0) / 256.0; int i;
    if (!g_w3On) return 0;
    for (i = 0; i < g_w3ntm; i++) if (fabs(q - g_w3tm[i].mid) < W3_TRAML / g_w3c.L * 0.5 && fabs(lat - g_w3tm[i].u) < W3_TRAMW * 0.5 + 30) { LOG("world3d: the player rode into a tram at %.2f", q); return 1; }
    return 0; }
#define W3_HALOV (6 * 4 * 256)   /* the sprites pass: room for the road users' light halos after the sprites' own vertices */
/* where each sprite polygon is drawn this frame (P: x[4], y[4] frame pixels) and how far away (D; W3Spr.dObj per object) - made before the world pass,
   so the lights (W3Lights: the headlights' cones) start where the bikes are drawn */
static float *g_w3P; static double *g_w3D; static int g_w3Pcap;
static void W3Place(int w, int h, const float *m, double vp)
{   float *P; double *D; int i, k; static int mapOff = -1;
    if (mapOff < 0) mapOff = getenv("RR_W3NOMAP") != NULL;   /* test switch: objects where the game put them */
    if (g_w3Pcap < g_w3ns) { int nc = g_w3ns * 2; float *np = (float *)realloc(g_w3P, sizeof(float) * 8 * nc); double *nd;
        if (!np) return; g_w3P = np; if (!(nd = (double *)realloc(g_w3D, sizeof(double) * nc))) return; g_w3D = nd; g_w3Pcap = nc; }
    P = g_w3P; D = g_w3D;
    {   /* the player's bike: across the screen where it is on the 3D road (W3MapObj), at the game's size - the game's own place is relative to ITS road picture,
           shifted across in whole pixels as it bends: against the 3D road the bike zig-zagged through every bend, up to 22 pixels a frame (user, 2026-11-20:
           "make it so the bike doesnt get impacted by the camera following the road") - test switch RR_W3BIKEGAME=1: where the game has it */
        static int game = -1; float mx[4], my[4]; double md;
        if (game < 0) game = getenv("RR_W3BIKEGAME") != NULL;
        g_w3bikeDx = 0;
        int ref = -1, j;   /* measured on the bike's shadow (its middle is the bike's, whatever the rider does): from a picture's own middle, a punch or a kick
                              (the picture wider on one side) moved the whole bike across by it (user, 2026-11-20: "while punching and kicking the bike should not move with it") */
        for (j = 0; j < g_w3ns; j++) if (g_w3s[j].kind == RRO_PLAYER && g_w3s[j].objDepth >= 0) { if (W3IsGameShadow(&g_w3s[j])) { ref = j; break; } if (ref < 0) ref = j; }
        if (!game && !mapOff && ref >= 0) { const W3Spr *s = &g_w3s[ref]; static double lastDx; double gx = (s->x[0] + s->x[1] + s->x[2] + s->x[3]) / 4, wx = 0, wy = 0; int nw = 0, rs = -1;
            /* the bike's back wheel: its tail lamp's middle (riderlamps.inc, each picture's own - straight above the wheel) where the game draws it - the shadow is
               the picture's own outline, wider with an arm or a leg out, and the game's picture itself is placed by its box: through a punch or a kick the
               bike still slid across (user, 2026-11-20: "your point of reference should be the wheel of the bike. it simply cannot move during the animation") */
            for (j = 0; j < g_w3ns; j++) { const W3Spr *t = &g_w3s[j]; const W3Red *rd; float hx, hy, ux = 0; int e;
                if (t->kind != RRO_PLAYER || t->objDepth < 0 || W3IsGameShadow(t) || !t->tx || t->tx->rspr < 0 || !(rd = W3RedSpots(t->tx)) || !rd->n) continue;
                for (e = 0; e < rd->n && e < 2; e++) ux += rd->u[e]; ux /= e;
                if (W3TexToScreen(t->u, t->v, t->x, t->y, ux, rd->v[0], &hx, &hy)) { wx += hx; wy = hy; nw++; } rs = t->tx->rspr; break; }
            if (W3MapObj(s, w, h, m, vp, mx, my, &md)) {   /* (the shadow mapped: its middle = the bike's lateral on the 3D road, W3MapObj) */
                double tx = (mx[0] + mx[1] + mx[2] + mx[3]) / 4;
                g_w3bikeDx = nw ? tx - wx / nw : lastDx; lastDx = g_w3bikeDx;   /* (no lamp in this picture: as the frame before) */
                {   double cx, ry, sc; if (W3OrigAt(s->objDepth, &cx, &ry, &sc) && fabs(s->lat - (gx - cx) / sc) >= 60) g_w3bikeDx = lastDx = 0; } }   /* (on a split road's B the game's
                                                         lateral is from A's centre, not where its picture is: the bike was put off the screen - there the game's own place) */
            if (getenv("RR_W3LOGBIKE")) LOG("w3bikedraw vp %.3f x %.2f dx %.2f wheel %d %.2f shadowx %.2f wy %.2f frame %lu rspr %d", vp, nw ? wx / nw + g_w3bikeDx : gx + g_w3bikeDx, g_w3bikeDx, nw, nw ? wx / nw : 0, gx, wy, (unsigned long)g_frame, rs); } }
    for (i = 0; i < g_w3ns; i++) {   /* where each polygon goes (P: x[4], y[4]) and how far away it is (D) */
        const W3Spr *s = &g_w3s[i];
        double d = (s->depth - W3_EYE) * W3_L, yb = -1e30, road, md; float *px = P + 8 * i, *py = px + 4, mx[4], my[4]; int capped = 1;
        for (k = 0; k < 4; k++) { px[k] = s->x[k]; py[k] = s->y[k]; }
        if (s->kind == RRO_PLAYER) for (k = 0; k < 4; k++) px[k] += (float)g_w3bikeDx;   /* (all its polygons: bike, rider, shadow - above) */
        if (!mapOff && s->objDepth >= 0 && W3MapObj(s, w, h, m, vp, mx, my, &md)) {
            /* near the camera the game's own placement already agrees with the 3D road (its scale is clamped there, so the map would shrink the
               objects): the game's place up to 3 segments, the true-3D one from 6 */
            double t = (s->objDepth - 3.0) / 3.0;
            if (t < 0) t = 0; if (t > 1) t = 1;
            for (k = 0; k < 4; k++) { px[k] = (float)(px[k] + (mx[k] - px[k]) * t); py[k] = (float)(py[k] + (my[k] - py[k]) * t); }
            d = md - 150;   /* the foot stands on the road: just in front of it */
            capped = t < 1;   /* placed in true 3D: its foot is on the 3D road already, and behind a crest the road at its screen row is the crest, which must hide it */
        }
        for (k = 0; k < 4; k++) if (py[k] > yb) yb = py[k];
        road = W3RoadDistAt(yb) * 0.95;   /* a little in front of the road at its foot (the game's own placement) */
        if (capped && road < d) d = road;
        D[i] = d;
    }
    for (i = 0; i < g_w3ns; i++) if (g_w3s[i].objDepth >= 0) {   /* one depth for all the polygons of an object (bike, rider, shadow): the game's order decides between them */
        int j; double dm = D[i];
        for (j = 0; j < g_w3ns; j++) if (g_w3s[j].objDepth == g_w3s[i].objDepth && D[j] < dm) dm = D[j];
        g_w3s[i].dObj = dm; } }
static void W3Sprites(ID3D11Texture2D *target, int w, int h, int tw, int th, const float *m, double vp, const R3Frame *wf)
{   static R3Vert *v; static R3Batch *b; static int cap; float *P = g_w3P; double *D = g_w3D; R3Frame f; int i, k, nb = 0, nv = 0, last = -2;
    const double zn = W3_ZN, zf = W3Far();
    if (!g_w3ns || g_w3Pcap < g_w3ns) return;
    if (getenv("RR_W3LOGS")) { static int n; if ((++n % 500) == 0) { int j, c = 0;   /* test switch: the objects' depths */
        LOG("w3 objs %d: view %x seg depth %d %d %d (n %d) s %d %d", n, g_w3cam.viewPos, g_w3cam.seg[0].depth, g_w3cam.seg[1].depth, g_w3cam.seg[2].depth, g_w3cam.nSeg, g_w3cam.seg[0].s, g_w3cam.seg[1].s);
        for (j = 0; j < g_w3ns && c < 8; j++) if (g_w3s[j].objDepth >= 0) { c++; { double a, r0 = 0, r1 = 0, sc, yb = g_w3s[j].y[0]; int q; for (q = 1; q < 4; q++) if (g_w3s[j].y[q] > yb) yb = g_w3s[j].y[q];
            W3OrigAt(g_w3s[j].objDepth, &a, &r0, &sc); W3OrigAt(g_w3s[j].objDepth - 1, &a, &r1, &sc);
            LOG("  obj seg %.3f obj %.3f foot %.1f road at obj %.1f at obj-1 %.1f (w %d)", g_w3s[j].depth, g_w3s[j].objDepth, yb, r0, r1, w); } } } }
    if (getenv("RR_W3LOGC")) { static int n; if ((++n % 200) == 0) { int j;   /* test switch: the cars' numbers (car models) */
        for (j = 0; j < g_w3ns; j++) if (g_w3s[j].kind == RRO_CAR) { const W3Spr *s = &g_w3s[j]; double cx, ry, sc, x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9; int q;
            for (q = 0; q < 4; q++) { if (s->x[q] < x0) x0 = s->x[q]; if (s->x[q] > x1) x1 = s->x[q]; if (s->y[q] < y0) y0 = s->y[q]; if (s->y[q] > y1) y1 = s->y[q]; }
            if (!W3OrigAt(s->objDepth, &cx, &ry, &sc)) continue;
            LOG("w3 car %d id %08x '%.8s' tex %dx%d depth %.2f yaw %.3f lat %.0f footU %.0f footH %.0f size %.0f x %.0f (px %.0f x %.0f)", n, s->id, s->name,
                s->tx ? s->tx->w : 0, s->tx ? s->tx->h : 0, s->objDepth, s->yaw, s->lat, ((x0 + x1) / 2 - cx) / sc, (ry - y1) / sc, (x1 - x0) / sc, (y1 - y0) / sc, x1 - x0, y1 - y0); } } }
    if (getenv("RR_W3LOGO")) { static int no; if (++no == atoi(getenv("RR_W3LOGO"))) { int j;   /* test switch RR_W3LOGO=<n>: every object polygon of the 3D pass at its n-th frame */
        for (j = 0; j < g_w3ns; j++) { const W3Spr *s = &g_w3s[j];
            LOG("w3obj %d '%.8s' kind %d objDepth %.2f depth %.2f id %08x tex %dx%d rgba %08x xy %.0f,%.0f %.0f,%.0f %.0f,%.0f %.0f,%.0f", j, s->name, s->kind, s->objDepth, s->depth, s->id,
                s->tx ? s->tx->w : 0, s->tx ? s->tx->h : 0, s->rgba, s->x[0], s->y[0], s->x[1], s->y[1], s->x[2], s->y[2], s->x[3], s->y[3]); } } }
    if (cap < g_w3ns) { cap = g_w3ns * 2; v = (R3Vert *)realloc(v, sizeof(R3Vert) * (12 * cap + W3_HALOV)); b = (R3Batch *)realloc(b, sizeof(R3Batch) * (2 * cap + 4));
        if (!v || !b) { cap = 0; return; } }
    if (!g_w3NoShadow) {   /* the riders' shadows (batch alpha 4, first: the bikes are drawn over them) - each of a bike's polygons laid on the road away
                              from the sun (weather.inc WxSunShadow: its bearing, length per unit of height, darkness): every point of the picture moved
                              along the ground by its height over the bike's foot x the length */
        float bear, len, op = WxSunShadow(&bear, &len), fw[3], sd[3];
        fw[0] = -wf->right[2]; fw[1] = 0; fw[2] = wf->right[0];
        sd[0] = -(fw[0] * cosf(bear) + wf->right[0] * sinf(bear)); sd[2] = -(fw[2] * cosf(bear) + wf->right[2] * sinf(bear));   /* (away from the sun) */
        for (i = 0; i < g_w3ns && op > 0.01f; i++) {
            const W3Spr *s = &g_w3s[i]; static const int o6[6] = { 0, 1, 2, 0, 2, 3 }; int tex, j; double d; float foot = 1e30f, wp[4][3];
            if (!(s->kind == RRO_RIDER || s->kind == RRO_PLAYER || s->kind == RRO_COP) || !s->t || W3IsGameShadow(s)) continue;
            d = s->dObj > 30 ? s->dObj : 30;
            for (j = 0; j < g_w3ns; j++) if (g_w3s[j].id == s->id && !W3IsGameShadow(&g_w3s[j])) for (k = 0; k < 4; k++) {   /* the foot: the object's lowest point */
                float q[3]; W3Unproj(wf, P[8 * j + k], P[8 * j + 4 + k], d, q); if (q[1] < foot) foot = q[1]; }
            for (k = 0; k < 4; k++) { float hh; W3Unproj(wf, P[8 * i + k], P[8 * i + 4 + k], d, wp[k]); hh = wp[k][1] - foot; if (hh < 0) hh = 0;
                wp[k][0] += sd[0] * hh * len; wp[k][2] += sd[2] * hh * len; wp[k][1] = foot + 3; }
            tex = R3_External(&g_w3r, (ID3D11Texture2D *)SDL_GetPointerProperty(SDL_GetTextureProperties(s->t), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, NULL));
            if (tex < 0 || nb >= 2 * cap) continue;
            b[nb].first = nv; b[nb].count = 0; b[nb].tex = tex; b[nb].alpha = 4;
            for (k = 0; k < 6; k++) { R3Vert *o = &v[nv]; int c = o6[k]; const float *q = wp[c];
                double X = q[0] * m[0] + q[1] * m[4] + q[2] * m[8] + m[12], Y = q[0] * m[1] + q[1] * m[5] + q[2] * m[9] + m[13], Z = q[0] * m[2] + q[1] * m[6] + q[2] * m[10] + m[14], W = q[0] * m[3] + q[1] * m[7] + q[2] * m[11] + m[15];
                if (W < 1) break;
                o->x = (float)(X / W); o->y = (float)(Y / W); o->z = (float)(Z / W); o->u = s->u[c]; o->v = s->v[c];
                o->rgba = W3Rgba(op, 0, 0, 251.5f / 255.0f); o->tex = q[0]; o->ox = q[1]; o->oy = q[2]; nv++; }
            if (k < 6) { nv = b[nb].first; continue; }   /* (behind the eye) */
            b[nb].count = 6; nb++; }
        if (getenv("RR_W3LOGL")) { static int nl; if (++nl % atoi(getenv("RR_W3LOGL")) == 0) { int c2 = 0, j2; for (j2 = 0; j2 < nb; j2++) c2 += b[j2].alpha == 4; LOG("w3shadow op %.2f bear %.2f len %.2f batches %d (sprites %d)", op, bear, len, c2, g_w3ns); } }
        last = -2; }
    for (i = 0; i < g_w3ns; i++) {
        const W3Spr *s = &g_w3s[i]; static const int o6[6] = { 0, 1, 2, 0, 2, 3 }; int tex = -1;
        double d = s->objDepth >= 0 ? s->dObj : D[i], z; const float *px = P + 8 * i, *py = px + 4;
        if (s->kind == RRO_CAR && W3CarsOn()) continue;   /* drawn as a 3D car (W3Cars) */
        if (!g_w3NoShadow && W3IsGameShadow(s) && (s->kind == RRO_RIDER || s->kind == RRO_PLAYER || s->kind == RRO_COP)) continue;   /* (the game's own shadow: replaced above) */
        if (d < 30) d = 30;
        z = R3_Depth(d, zn, zf);   /* the depth buffer's value at distance d (reversed depth, render3d.h R3_Persp) */
        if (s->t) tex = R3_External(&g_w3r, (ID3D11Texture2D *)SDL_GetPointerProperty(SDL_GetTextureProperties(s->t), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, NULL));
        if (s->t && tex < 0) continue;
        if (tex != last || !nb) { b[nb].first = nv; b[nb].count = 0; b[nb].tex = tex; b[nb].alpha = 0; nb++; last = tex; }
        for (k = 0; k < 6; k++) { R3Vert *o = &v[nv++]; int c = o6[k];
            o->x = (float)(px[c] / w * 2.0 - 1.0); o->y = (float)(1.0 - py[c] / h * 2.0); o->z = (float)z;
            o->u = s->u[c]; o->v = s->v[c]; o->rgba = s->rgba;
            {   float wp[3]; W3Unproj(wf, px[c], py[c], d, wp); o->tex = wp[0]; o->ox = wp[1]; o->oy = wp[2]; } }   /* (its point in the world: lit per pixel, fogged) */
        b[nb - 1].count += 6;
    }
    {   /* lights on the road users (halos, batch alpha 3): traffic head / tail lights after dark, the riders' tail lights after dark and brake lights
           always, a police rider's red / blue lights always */
        typedef struct W3Lens { float x, y, rx, ry; double d; unsigned rgba, core, rgba3; int tex, tex3, texc; float qx[4], qy[4]; } W3Lens;   /* tex >= 0: the lamp's own pixels (W3LampTex) on the corners qx / qy, drawn as the lens tex3 (W3LampTex3D) */
        W3Lens lens[64]; int nr = 0;
        float dark = WxDark(); double now = WxSeconds(); int flash = W3CopFlash(now), first = nv, gt = W3GlowTex();
        for (i = 0; i < g_w3ns && gt >= 0 && nv + 24 <= 6 * cap + W3_HALOV; i++) {   /* (an object adds up to 4 halos) */
            const W3Spr *s = &g_w3s[i]; float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, bw, bh; double d; int j;
            if (s->kind != RRO_COP && s->kind != RRO_RIDER && s->kind != RRO_PLAYER && (s->kind != RRO_CAR || dark < 0.05f || W3CarsOn())) continue;
            for (j = 0; j < i; j++) if (g_w3s[j].kind == s->kind && g_w3s[j].id == s->id) break;
            if (j < i) continue;   /* (this object's lights are made already) */
            for (j = i; j < g_w3ns; j++) if (g_w3s[j].kind == s->kind && g_w3s[j].id == s->id && g_w3s[j].tx) { const float *px = P + 8 * j, *py = px + 4;
                for (k = 0; k < 4; k++) { if (px[k] < x0) x0 = px[k]; if (px[k] > x1) x1 = px[k]; if (py[k] < y0) y0 = py[k]; if (py[k] > y1) y1 = py[k]; } }
            if (x1 <= x0 || y1 <= y0) continue;
            bw = x1 - x0; bh = y1 - y0; d = s->dObj - 120; if (d < 30) d = 30;
            if (s->kind == RRO_COP) { float a = (dark > 0.6f ? dark : 0.6f) * 0.8f;   /* (20 % softer: user, 2026-11-15) */
                nv = (int)(W3ClipHalo(v + nv, x0 + bw * 0.3f, y0 + bh * 0.12f, bw * (flash ? 0.75f : 0.4f), w, h, d, W3Rgba(1.0f, 0.12f, 0.08f, a * (flash ? 1.0f : 0.3f)), wf) - v);
                nv = (int)(W3ClipHalo(v + nv, x1 - bw * 0.3f, y0 + bh * 0.12f, bw * (flash ? 0.4f : 0.75f), w, h, d, W3Rgba(0.15f, 0.35f, 1.0f, a * (flash ? 0.3f : 1.0f)), wf) - v); }
            else if (s->kind == RRO_RIDER || s->kind == RRO_PLAYER) {   /* a bike seen from behind: its tail light under the seat, bright and bigger while braking */
                float br = s->kind == RRO_PLAYER ? W3PlayerBrake(s->id) : W3Brake(s->id, vp + s->objDepth - 1.0, now), a = dark * 0.8f + br * (1 - dark * 0.8f), lx = (x0 + x1) * 0.5f, ly = y0 + bh * 0.62f, mx[2], my[2]; int nl = 0, e2, lj = -1, pick;
                for (j = i; j < g_w3ns; j++) { const W3Spr *t = &g_w3s[j]; const W3Red *rd;   /* on the sprite's own red square(s): one lamp, or two side by side */
                    if (t->id == s->id && t->tx && !W3IsGameShadow(t) && (rd = W3RedSpots(t->tx)) && rd->n) {
                        for (e2 = 0; e2 < rd->n && e2 < 2; e2++) if (W3TexToScreen(t->u, t->v, P + 8 * j, P + 8 * j + 4, rd->u[e2], rd->v[e2], &mx[nl], &my[nl])) nl++;
                        if (nl) { lj = j; break; } } }
                pick = nl == 2 ? -1 : nl ? 0 : -2;
                if (nl == 2 && s->yaw < 9.5f) { float cxm = (x0 + x1) * 0.5f; pick = 0; if (fabsf(mx[1] - cxm) < fabsf(mx[0] - cxm)) { mx[0] = mx[1]; my[0] = my[1]; pick = 1; } nl = 1; }   /* (two lamps only on a super bike, as its headlights: else the one in the middle) */
                if (s->kind == RRO_PLAYER && getenv("RR_W3LENSDUMP"))   /* (test switch: the player's pictures with their lens, W3LensDump) */
                    for (j = i; j < g_w3ns; j++) { const W3Spr *t = &g_w3s[j]; if (t->id == s->id && t->kind == s->kind && t->tx) { int sh = W3IsGameShadow(t);
                        W3LensDump(t, sh ? NULL : W3RedSpots(t->tx), j == lj ? pick : -2, sh); } }
                if (nl) { lx = nl == 2 ? (mx[0] + mx[1]) * 0.5f : mx[0]; ly = nl == 2 ? (my[0] + my[1]) * 0.5f : my[0]; }   /* (no lamp found in this picture: no lens - better none than a guessed one) */
                for (e2 = 0; e2 < nl && a > 0.03f && nr < 64; e2++) {   /* the lens itself: a rectangle (W3RectTex, drawn after the round glows), larger while braking */
                    W3Lens *L = &lens[nr++]; float sc2 = nl == 2 ? 0.6f : 1.0f; L->x = mx[e2]; L->y = my[e2]; L->rx = bw * (0.17f + 0.05f * br) * sc2; L->ry = bw * (0.075f + 0.025f * br); L->d = d;
                    L->rgba = W3Rgba(1.0f, 0.10f + 0.12f * br, 0.05f + 0.05f * br, a); L->core = br > 0.05f ? W3Rgba(1.0f, 0.6f, 0.5f, br) : 0; L->tex = -1;
                    if (lj >= 0 && g_w3s[lj].tx->rspr >= 0 && (L->tex = W3LampTex(g_w3s[lj].tx->rspr)) >= 0) {   /* a rider sprite: exactly its lamp's pixels, where the sprite is drawn */
                        L->tex3 = W3LampTex3D(g_w3s[lj].tx->rspr, &L->texc); L->rgba3 = W3Rgba(1.0f, 1.0f, 1.0f, a);
                        const W3Spr *t = &g_w3s[lj]; const RlLamp *rl = &g_rlLamp[t->tx->rspr]; float u0 = (rl->x - 1.0f) / t->tx->w, u1 = (rl->x + rl->w + 1.0f) / t->tx->w, v0 = (rl->y - 1.0f) / t->tx->h, v1 = (rl->y + rl->h + 1.0f) / t->tx->h; int c;
                        for (c = 0; c < 4; c++) if (!W3TexToScreen(t->u, t->v, P + 8 * lj, P + 8 * lj + 4, c == 0 || c == 3 ? u0 : u1, c < 2 ? v0 : v1, &L->qx[c], &L->qy[c])) L->tex = -1; } }
                if (nl && br > 0.05f && dark > 0.05f && nv + 18 <= 6 * cap + W3_HALOV) {   /* braking after dark: a strong red glow round it */
                    nv = (int)(W3ClipHalo(v + nv, lx, ly, bw * 1.15f, w, h, d, W3Rgba(1.0f, 0.06f, 0.03f, 1.0f * br * dark), wf) - v);
                    nv = (int)(W3ClipHalo(v + nv, lx, ly, bw * 0.45f, w, h, d, W3Rgba(1.0f, 0.12f, 0.06f, br), wf) - v); } }
            else { double c = cos(s->yaw * 2 * 3.14159265358979);
                if (c > 0.5) {   /* seen from behind: red tail lights */
                    nv = (int)(W3ClipHalo(v + nv, x0 + bw * 0.2f, y1 - bh * 0.38f, bw * 0.18f, w, h, d, W3Rgba(1.0f, 0.10f, 0.05f, dark), wf) - v);
                    nv = (int)(W3ClipHalo(v + nv, x1 - bw * 0.2f, y1 - bh * 0.38f, bw * 0.18f, w, h, d, W3Rgba(1.0f, 0.10f, 0.05f, dark), wf) - v); }
                else if (c < -0.5) {   /* oncoming: headlights */
                    nv = (int)(W3ClipHalo(v + nv, x0 + bw * 0.2f, y1 - bh * 0.3f, bw * 0.34f, w, h, d, W3Rgba(1.0f, 0.95f, 0.80f, dark), wf) - v);
                    nv = (int)(W3ClipHalo(v + nv, x1 - bw * 0.2f, y1 - bh * 0.3f, bw * 0.34f, w, h, d, W3Rgba(1.0f, 0.95f, 0.80f, dark), wf) - v); } }
        }
        if (nv > first && nb < cap) { b[nb].first = first; b[nb].count = nv - first; b[nb].tex = gt; b[nb].alpha = 3; nb++; }
        {   int rt = W3RectTex(), k2; first = nv;   /* the bikes' rectangular tail / brake light lenses, with a white-hot middle while braking */
            for (k2 = 0; k2 < nr && nv + 12 <= 6 * cap + W3_HALOV && nb + 3 < cap; k2++) { const W3Lens *L = &lens[k2];
                if (L->tex >= 0) {   /* the lamp's own pixels: a batch of its own (its own picture) */
                    if (nv > first) { b[nb].first = first; b[nb].count = nv - first; b[nb].tex = rt; b[nb].alpha = 3; nb++; }
                    first = nv; nv = (int)(W3ClipQuad(v + nv, L->qx, L->qy, w, h, L->d, L->tex3 >= 0 ? L->rgba3 : L->rgba, wf) - v);   /* the lens (bevelled, its colour in the picture) */
                    if (L->tex3 >= 0) { b[nb].first = first; b[nb].count = nv - first; b[nb].tex = L->tex3; b[nb].alpha = 5; nb++; first = nv; }   /* (laid over: its shading shows) */
                    if (L->core) nv = (int)(W3ClipQuad(v + nv, L->qx, L->qy, w, h, L->d, L->core, wf) - v);   /* braking: white-hot over the same pixels */
                    if (nv > first) { b[nb].first = first; b[nb].count = nv - first; b[nb].tex = L->tex3 >= 0 && L->texc >= 0 ? L->texc : L->tex; b[nb].alpha = 3; nb++; } first = nv; continue; }
                if (rt < 0) continue;
                nv = (int)(W3ClipRect(v + nv, L->x, L->y, L->rx, L->ry, w, h, L->d, L->rgba, wf) - v);
                if (L->core) nv = (int)(W3ClipRect(v + nv, L->x, L->y, L->rx * 0.7f, L->ry * 0.55f, w, h, L->d, L->core, wf) - v); }
            if (nv > first && nb < cap) { b[nb].first = first; b[nb].count = nv - first; b[nb].tex = rt; b[nb].alpha = 3; nb++; } }
    }
    memset(&f, 0, sizeof f);
    f.vp[0] = f.vp[5] = f.vp[10] = f.vp[15] = 1;   /* the vertices are in clip space already */
    WxWorld(&f); f.clipSpace = 1;   /* the light of the hour, the world's lights and the fog, per pixel at the sprites' world points */
    memcpy(f.eye, wf->eye, sizeof f.eye); memcpy(f.right, wf->right, sizeof f.right);
    f.nLights = wf->nLights; memcpy(f.light, wf->light, sizeof f.light); memcpy(f.lightCol, wf->lightCol, sizeof f.lightCol); memcpy(f.spot, wf->spot, sizeof f.spot); f.nSpots = wf->nSpots;
    f.target = target; f.w = tw; f.h = th; f.keepDepth = 1;
    f.dyn = v; f.nDyn = nv; f.dynBatches = b; f.nDynBatches = nb;
    R3_Render(&g_w3r, &f);
    R3_ExternalClear(&g_w3r); }

static void W3AddLight(R3Frame *f, const float *p, float radius, float r, float g, float b)
{   if (f->nLights >= 16) return;
    f->light[f->nLights][0] = p[0]; f->light[f->nLights][1] = p[1]; f->light[f->nLights][2] = p[2]; f->light[f->nLights][3] = radius;
    f->lightCol[f->nLights][0] = r; f->lightCol[f->nLights][1] = g; f->lightCol[f->nLights][2] = b; f->lightCol[f->nLights][3] = 0; f->nLights++; }
/* the frame's lights and halos: point lights into f, halo vertices into v (capacity cap); returns the halo vertices */
static int W3OnScreen(int i, int w, int h)   /* sprite polygon i (as drawn, W3Place) is inside the frame: its middle across in it, its foot not under it */
{   const float *P = g_w3P + 8 * i; float mx = (P[0] + P[1] + P[2] + P[3]) * 0.25f, top = P[4], bot = P[4]; int k;
    if (g_w3Pcap < g_w3ns) return 1;
    for (k = 1; k < 4; k++) { if (P[4 + k] < top) top = P[4 + k]; if (P[4 + k] > bot) bot = P[4 + k]; }
    return mx >= 0 && mx <= w && top < h && bot > 0; }
static int WpTorch(void);   /* weapons.inc */
static int W3Lights(R3Frame *f, R3Vert *v, int cap, double p, const float *eye, int w, int h)
{   float dark = WxDark(); double sec = WxSeconds(); int kc = (int)floor(p), lo = 0, hi = g_w3nlamp, i, nv = 0, best[15], nb = 0; float bd[15];
    int kEnd = kc + (int)(W3Far() / W3_L); int flash = W3CopFlash(sec);
    f->nLights = 0;
    while (lo < hi) { int m = (lo + hi) / 2; if (g_w3lamp[m].piece < kc - 1) lo = m + 1; else hi = m; }
    for (i = lo; i < g_w3nlamp && g_w3lamp[i].piece <= kEnd; i++) {
        const W3Lamp *L = &g_w3lamp[i]; float lx = L->x + f->right[0] * L->gox, ly = L->y + L->goy, lz = L->z + f->right[2] * L->gox, d2, a;
        if (!L->cop && dark < 0.02f) continue;
        d2 = (lx - eye[0]) * (lx - eye[0]) + (ly - eye[1]) * (ly - eye[1]) + (lz - eye[2]) * (lz - eye[2]);
        {   int fresh = nb < 15, k = fresh ? nb++ : 14;   /* the 15 nearest light the world (+ a police rider) */
            if (fresh || d2 < bd[14]) { while (k > 0 && bd[k - 1] > d2) { bd[k] = bd[k - 1]; best[k] = best[k - 1]; k--; } bd[k] = d2; best[k] = i; } }
        if (nv + 18 > cap || !W3InView(lx, ly, lz, L->cop ? L->w * 0.8f : (L->w * 0.72f < 168 ? 168 : L->w * 0.72f > 624 ? 624 : L->w * 0.72f))) continue;   /* (its halo out of view: the light still lights the road) */
        if (L->cop) {   /* the roof bar: red one side, blue the other, taking turns */
            float hs = L->w * 0.16f; a = dark > 0.5f ? dark : 0.5f;
            nv = (int)(W3Halo(v + nv, L, -L->w * 0.18f, hs * (flash ? 1.6f : 0.8f), W3Rgba(1.0f, 0.12f, 0.08f, a * (flash ? 1.0f : 0.35f))) - v);
            nv = (int)(W3Halo(v + nv, L, L->w * 0.18f, hs * (flash ? 0.8f : 1.6f), W3Rgba(0.15f, 0.35f, 1.0f, a * (flash ? 0.35f : 1.0f))) - v); }
        else {   /* a sodium lamp: wide soft halo + bright core */
            float hs = L->w * 0.30f; if (hs < 70) hs = 70; if (hs > 260) hs = 260;
            nv = (int)(W3Halo(v + nv, L, 0, hs * 2.4f, W3Rgba(1.0f, 0.70f, 0.36f, 0.32f * dark)) - v);
            nv = (int)(W3Halo(v + nv, L, 0, hs * 0.6f, W3Rgba(1.0f, 0.90f, 0.70f, 0.9f * dark)) - v); }
    }
    for (i = 0; i < nb; i++) { const W3Lamp *L = &g_w3lamp[best[i]]; float pt[3]; pt[0] = L->x + f->right[0] * L->gox; pt[1] = L->y + L->goy * 0.9f; pt[2] = L->z + f->right[2] * L->gox;
        if (L->cop) { float a = (dark > 0.4f ? dark : 0.4f) * 1.2f; W3AddLight(f, pt, 1400, flash ? a : 0.1f * a, 0.05f * a, flash ? 0.05f * a : a * 1.1f); }
        else { float rad = L->goy * 1.7f; if (rad < 1000) rad = 1000; W3AddLight(f, pt, rad, 0.80f * dark, 0.56f * dark, 0.28f * dark); } }   /* sodium: warm, a pool under each lamp */
    memset(f->spot, 0, sizeof f->spot); f->nSpots = 0;
    if (dark > 0.02f) {   /* headlights: a cone from each bike's front down the road ahead of it - the player's first, then the nearest others (6 at most) */
        int used[64], nu = 0, pass;
        for (pass = 0; pass < (getenv("RR_W3SPOT1") ? 1 : 2); pass++) for (;;) {
            int best = -1, j, k; double bdep = 1e9;
            for (i = 0; i < g_w3ns; i++) { const W3Spr *s = &g_w3s[i];
                if (!(pass ? (s->kind == RRO_RIDER || s->kind == RRO_COP || (s->kind == RRO_CAR && !W3CarsOn())) : s->kind == RRO_PLAYER) || s->objDepth < 0 || !W3OnScreen(i, w, h)) continue;   /* (a bike off the screen: no cone - its light would come from nowhere) */
                for (j = 0; j < nu && used[j] != (int)s->id; j++) {} if (j < nu) continue;
                if (s->objDepth < bdep) { bdep = s->objDepth; best = i; } }
            if (best < 0 || f->nSpots >= 6 || nu >= 64) break;
            used[nu++] = (int)g_w3s[best].id;
            if (g_w3s[best].kind == RRO_CAR) {   /* a car: one wide cone from its front along the road, the way it drives (oncoming: towards the camera) */
                const W3Spr *s = &g_w3s[best]; double q = p + (s->objDepth - W3_EYE), dir = cos(s->yaw * 2 * 3.14159265358979) < 0 ? -1.0 : 1.0; float a[3], b[3], d[3], l, (*sp)[4] = f->spot[f->nSpots++];
                C3_PointF(&g_w3c, W3Fr(), q + 0.35 * dir, s->lat, 70, a); C3_PointF(&g_w3c, W3Fr(), q + 3.2 * dir, s->lat, 0, b);
                for (k = 0; k < 3; k++) d[k] = b[k] - a[k]; l = (float)sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l < 1) l = 1;
                sp[0][0] = a[0]; sp[0][1] = a[1]; sp[0][2] = a[2]; sp[0][3] = 4600;
                sp[1][0] = d[0] / l; sp[1][1] = d[1] / l; sp[1][2] = d[2] / l; sp[1][3] = 0.9f;   /* cos 26 deg: wider than a bike's */
                sp[2][0] = 0.85f * dark; sp[2][1] = 0.82f * dark; sp[2][2] = 0.70f * dark; sp[2][3] = 0.975f;
                continue; }
            {   const W3Spr *s = &g_w3s[best]; double q = p + (s->objDepth - W3_EYE), U = s->lat, H = 110, cx, ry, sc, r; float g = s->kind == RRO_PLAYER ? (WpTorch() ? 1.6f : 1.0f) : 0.75f, tl[3];   /* (the Miner's Lamp charm: a brighter headlight) */ int twin = s->yaw >= 9.5f, lamp, drawn = 0;
                if (W3OrigAt(s->objDepth, &cx, &ry, &sc)) for (j = 0; j < g_w3ns; j++) {   /* the lamp: straight ahead of the sprite's red tail-light square */
                    const W3Spr *t = &g_w3s[j]; const W3Red *rd; float hx, hy;
                    if (t->id != s->id || !t->tx || W3IsGameShadow(t) || !(rd = W3RedSpots(t->tx)) || !rd->n) continue;
                    if (W3TexToScreen(t->u, t->v, t->x, t->y, rd->u[0], rd->v[0], &hx, &hy)) { U = (hx - cx) / sc; H = (ry - hy) / sc; if (H < 40) H = 40; if (H > 260) H = 260; }
                    if (g_w3Pcap >= g_w3ns && W3TexToScreen(t->u, t->v, g_w3P + 8 * j, g_w3P + 8 * j + 4, rd->u[0], rd->v[0], &hx, &hy)) {   /* where it is drawn (W3Place): */
                        W3Unproj(f, hx, hy, t->dObj > 30 ? t->dObj : 30, tl); drawn = 1; }   /* the tail light's world point as the sprites pass lights it (its depth) */
                    break; }
                if (s->kind == RRO_PLAYER) U += g_w3bikeOff;   /* (where the bike is drawn: W3MapObj) */
                r = W3Steer(s->id, q, s->lat, sec);   /* the bike's heading: the cone turns with it */
                if (getenv("RR_W3LOGL") && s->kind == RRO_PLAYER) { static int nh; if (++nh % atoi(getenv("RR_W3LOGL")) == 0) LOG("w3head q %.3f lat %.1f U %.1f H %.1f r %.1f drawn %d", q, s->lat, U, H, r, drawn); }
                for (lamp = 0; lamp < (twin ? 2 : 1) && f->nSpots < 6; lamp++) {   /* a super bike: two lamps side by side, their cones overlapping */
                    float a[3], b[3], d[3], l, (*sp)[4] = f->spot[f->nSpots++]; double du = twin ? (lamp ? 40 : -40) : 0, gg = twin ? g * 0.7 : g;
                    {   /* the lamp just ahead of the tail light where the bike is drawn, aimed along his heading (the road's direction where he is, turned
                           by his steering - not round the bend ahead): straight out and down to the road ~3.4 pieces on */
                        float t0[3], t1[3], hx, hz, hl, g0[3], gr[3], base[3]; double fw = 0.22 * W3_L, qv = q;
                        if (drawn) qv = p + ((tl[0] - eye[0]) * -f->right[2] + (tl[2] - eye[2]) * f->right[0]) / W3_L;   /* (the piece the drawn bike is at: its distance along the view) */
                        C3_PointF(&g_w3c, W3Fr(), qv + 0.4, U + du, 0, t0); C3_PointF(&g_w3c, W3Fr(), qv + 0.9, U + du + r * 0.5, 0, t1);   /* (a little into the bend, as a leaning bike's lamp) */
                        hx = t1[0] - t0[0]; hz = t1[2] - t0[2]; hl = (float)sqrt(hx * hx + hz * hz); if (hl < 1) hl = 1; hx /= hl; hz /= hl;
                        if (drawn) { base[0] = tl[0] + (float)(-hz * du); base[1] = tl[1]; base[2] = tl[2] + (float)(hx * du); }   /* (twin lamps: side by side across the heading) */
                        else C3_PointF(&g_w3c, W3Fr(), q, U + du, H, base);
                        a[0] = base[0] + hx * (float)fw; a[1] = base[1]; a[2] = base[2] + hz * (float)fw;
                        C3_PointF(&g_w3c, W3Fr(), qv, U, 0, g0); C3_PointF(&g_w3c, W3Fr(), qv + 3.4, U + r * 3.4, 0, gr);
                        b[0] = a[0] + hx * (float)(3.4 * W3_L); b[2] = a[2] + hz * (float)(3.4 * W3_L); b[1] = base[1] - (float)H + (gr[1] - g0[1]); }
                    if (getenv("RR_W3LOGL") && s->kind == RRO_PLAYER && lamp == 0) { static int ns; if (++ns % atoi(getenv("RR_W3LOGL")) == 0) { float sa[2], sb[2]; int e;
                        for (e = 0; e < 2; e++) { const float *P = e ? b : a; double X = P[0] * f->vp[0] + P[1] * f->vp[4] + P[2] * f->vp[8] + f->vp[12], Y = P[0] * f->vp[1] + P[1] * f->vp[5] + P[2] * f->vp[9] + f->vp[13], W = P[0] * f->vp[3] + P[1] * f->vp[7] + P[2] * f->vp[11] + f->vp[15];
                            (e ? sb : sa)[0] = (float)(W > 1 ? (X / W + 1) * 0.5 : -1); (e ? sb : sa)[1] = (float)(W > 1 ? (1 - Y / W) * 0.5 : -1); }
                        LOG("w3cone screen fractions: apex %.3f,%.3f  aim %.3f,%.3f  (sprite x %.0f..%.0f of %d)", sa[0], sa[1], sb[0], sb[1], s->x[0], s->x[1], g_w3cam.centreX * 2); } }
                    for (k = 0; k < 3; k++) d[k] = b[k] - a[k]; l = (float)sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l < 1) l = 1;
                    sp[0][0] = a[0]; sp[0][1] = a[1]; sp[0][2] = a[2]; sp[0][3] = 4200;
                    sp[1][0] = d[0] / l; sp[1][1] = d[1] / l; sp[1][2] = d[2] / l; sp[1][3] = 0.95f;   /* cos 18 deg: the cone's edge */
                    sp[2][0] = (float)(0.80 * dark * gg); sp[2][1] = (float)(0.77 * dark * gg); sp[2][2] = (float)(0.66 * dark * gg); sp[2][3] = 0.99f; } }   /* cos 8 deg: full inside */
            if (!pass) break; }
        }   /* test switch RR_W3SPOT1=1: the player's headlight only (the pass loop above) */
    for (i = 0; i < g_w3ns; i++) if (g_w3s[i].kind == RRO_COP) {   /* a police rider: the road round him flashes red / blue */
        double cx, ry, sc, mx = 0; int k; float pt[3]; const W3Spr *s = &g_w3s[i];
        if (!W3OrigAt(s->objDepth, &cx, &ry, &sc)) continue;
        for (k = 0; k < 4; k++) mx += s->x[k]; mx /= 4;
        C3_PointF(&g_w3c, W3Fr(), p + (s->objDepth - W3_EYE), (mx - cx) / sc, 260, pt);
        {   float a = (dark > 0.4f ? dark : 0.4f) * 1.8f * 0.8f; W3AddLight(f, pt, 1800, flash ? a : 0.08f * a, 0.05f * a, flash ? 0.05f * a : a * 1.2f); }   /* (police lights 20 % softer: user, 2026-11-15) */
        break; }
    if (getenv("RR_W3LOGL")) { static int nl; if (++nl % atoi(getenv("RR_W3LOGL")) == 0) { int kn[5] = { 0, 0, 0, 0, 0 }, j; for (j = 0; j < g_w3ns; j++) if (g_w3s[j].kind >= 0 && g_w3s[j].kind < 5) kn[g_w3s[j].kind]++;
        for (j = 0; j < g_w3ns; j++) if ((g_w3s[j].kind == RRO_PLAYER || g_w3s[j].kind == RRO_RIDER) && g_w3s[j].tx) { const W3Red *r = W3RedSpots(g_w3s[j].tx); if (r && r->n) LOG("w3red %.8s %dx%d kind %d: %d lamps %.2f,%.2f %.2f,%.2f", g_w3s[j].name, g_w3s[j].tx->w, g_w3s[j].tx->h, g_w3s[j].kind, r->n, r->u[0], r->v[0], r->u[1], r->v[1]); }
        LOG("w3lights %d: piece %d dark %.2f lamps %d..%d (first %d at piece %d) lit %d halo verts %d; sprites none %d car %d cop %d rider %d player %d spots %d", nl, kc, dark, lo, i, g_w3nlamp, lo < g_w3nlamp ? g_w3lamp[lo].piece : -1, f->nLights, nv, kn[0], kn[1], kn[2], kn[3], kn[4], f->nSpots); } }   /* test switch RR_W3LOGL=<k> */
    return nv; }

/* ---- fire hydrants (user, 2026-11-14: "when a rider rides over a fire hydrant and breaks it can you spray water up into the air"): the hydrant
   billboards (HYDRNT*) found once (W3Hydrants); a rider (any: the game's sprites RRO_RIDER / PLAYER / COP, where the 3D world puts them) within
   W3_HYDHIT of one breaks it (W3HydCheck): its billboard is taken out of the course mesh (collapsed: W3HideHyd, static and live; the live stretch
   again whenever it is rebuilt) and from then on a stump is drawn there - the mod's picture <NAME>_broken.png beside the hydrant's own (hdtex.inc
   HdTrackDir; made by analysis\trees_hd\scripts\trees.py broken) - with a jet of water: droplets thrown up, falling back, a mist round the foot.
   Drawing only: the game's own collisions are untouched.  Test switch RR_W3HYDTEST=1: every hydrant broken from the start ---- */
#define W3_HYDHIT 46.0f
typedef struct W3Hyd { float x, y, z, oxa, oxb, oya, oyb; int tex, broken; double t0; } W3Hyd;
static W3Hyd *g_w3hyd; static int g_w3nhyd;
static int g_w3hydTex[8], g_w3hydSrc[8], g_w3hydGen = -1, g_w3nhydTex;   /* the stumps' renderer textures, by the hydrant's course texture */
static const char *HdTrackDir(void);   /* hdtex.inc: the mod's picture folder of this track ("" = none) */
static void W3Hydrants(void)
{   int i = 0, j, t, cap = 0;
    free(g_w3hyd); g_w3hyd = NULL; g_w3nhyd = 0;
    while (i < g_w3c.nv) {
        const C3Vert *v = &g_w3c.v[i]; W3Hyd *H; float oxa = 1e9f, oxb = -1e9f, oya = 1e9f, oyb = -1e9f;
        t = (int)v->tex;
        if ((v->ox == 0 && v->oy == 0) || t < 0 || t >= g_w3a.ntex || strncmp(g_w3a.tex[t].name, "HYDRNT", 6)) { i++; continue; }
        for (j = i; j < g_w3c.nv && j < i + 6 && g_w3c.v[j].x == v->x && g_w3c.v[j].y == v->y && g_w3c.v[j].z == v->z && (int)g_w3c.v[j].tex == t; j++) {
            const C3Vert *q = &g_w3c.v[j]; if (q->ox < oxa) oxa = q->ox; if (q->ox > oxb) oxb = q->ox; if (q->oy < oya) oya = q->oy; if (q->oy > oyb) oyb = q->oy; }
        if (g_w3nhyd == cap) { W3Hyd *n = (W3Hyd *)realloc(g_w3hyd, sizeof(W3Hyd) * (cap = cap ? cap * 2 : 256)); if (!n) break; g_w3hyd = n; }
        H = &g_w3hyd[g_w3nhyd++]; memset(H, 0, sizeof *H);
        H->x = v->x; H->y = v->y; H->z = v->z; H->oxa = oxa; H->oxb = oxb; H->oya = oya; H->oyb = oyb; H->tex = t;
        H->broken = getenv("RR_W3HYDTEST") != NULL; H->t0 = GetTickCount() / 1000.0 - (g_w3nhyd % 7) * 0.31;
        i = j; }
    g_w3nhydTex = 0; g_w3hydGen = -1;
    LOG("world3d: %d fire hydrants", g_w3nhyd); }
static int W3HideHyd(C3Course *c, const W3Hyd *H)   /* collapse the hydrant's billboard in course c: 1 = found */
{   int j, n = 0;
    for (j = 0; j < c->nv; j++) { C3Vert *q = &c->v[j];
        if ((q->ox != 0 || q->oy != 0) && (int)q->tex == H->tex && fabsf(q->x - H->x) < 2 && fabsf(q->z - H->z) < 2 && fabsf(q->y - H->y) < 40) { q->ox = q->oy = 0; n++; } }   /* (one point, no size: nothing drawn) */
    return n > 0; }
static void W3HydHideLive(void) { int i; for (i = 0; i < g_w3nhyd; i++) if (g_w3hyd[i].broken) W3HideHyd(&g_w3l, &g_w3hyd[i]); }   /* (the live stretch, built again) */
static void W3HydBreak(W3Hyd *H)
{   H->broken = 1; H->t0 = GetTickCount() / 1000.0;
    if (W3HideHyd(&g_w3c, H)) R3_SetStatic(&g_w3r, (const R3Vert *)g_w3c.v, g_w3c.nv);
    if (g_w3lo >= 0 && W3HideHyd(&g_w3l, H)) R3_SetLive(&g_w3r, (const R3Vert *)g_w3l.v, g_w3l.nv);
    LOG("world3d: hydrant at %.0f %.0f %.0f broken", H->x, H->y, H->z); }
static void W3HydCheck(double p)   /* the riders where the 3D world puts them: one on a hydrant breaks it */
{   int i, k;
    if (!g_w3nhyd) return;
    if (getenv("RR_W3HYDTEST")) { static int once; if (!once) { once = 1; for (k = 0; k < g_w3nhyd; k++) if (g_w3hyd[k].broken) { g_w3hyd[k].broken = 0; W3HydBreak(&g_w3hyd[k]); } } }
    for (i = 0; i < g_w3ns; i++) { const W3Spr *s = &g_w3s[i]; float pt[3];
        if ((s->kind != RRO_RIDER && s->kind != RRO_PLAYER && s->kind != RRO_COP) || s->objDepth < 0 || s->objDepth > 6) continue;
        C3_PointF(&g_w3c, W3Fr(), p + (s->objDepth - W3_EYE), s->lat, 0, pt);
        for (k = 0; k < g_w3nhyd; k++) { W3Hyd *H = &g_w3hyd[k]; float dx = pt[0] - H->x, dz = pt[2] - H->z;
            if (!H->broken && dx * dx + dz * dz < W3_HYDHIT * W3_HYDHIT && fabsf(pt[1] - H->y) < 120) W3HydBreak(H); } } }
static int W3HydStumpTex(int tex)   /* the stump picture of hydrant texture tex (-1 = none) */
{   int i; char path[MAX_PATH]; const char *dir; SDL_Surface *s, *c;
    if (g_w3hydGen != g_w3texGen) { g_w3nhydTex = 0; g_w3hydGen = g_w3texGen; }
    for (i = 0; i < g_w3nhydTex; i++) if (g_w3hydSrc[i] == tex) return g_w3hydTex[i];
    if (g_w3nhydTex >= 8) return -1;
    g_w3hydSrc[g_w3nhydTex] = tex; g_w3hydTex[g_w3nhydTex] = -1;
    dir = HdTrackDir();
    if (dir && dir[0]) { char nm[9]; memcpy(nm, g_w3a.tex[tex].name, 8); nm[8] = 0; for (i = 0; i < 8 && nm[i] && nm[i] != ' '; i++) {} nm[i] = 0;
        _snprintf(path, sizeof path, "%s\\%s_broken.png", dir, nm); path[sizeof path - 1] = 0;
        if ((s = SDL_LoadPNG(path)) != NULL) {
            if ((c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888)) != NULL) { unsigned *px = (unsigned *)malloc((size_t)c->w * c->h * 4); int y, x;
                if (px) { for (y = 0; y < c->h; y++) for (x = 0; x < c->w; x++) { unsigned q = ((const unsigned *)((const BYTE *)c->pixels + (size_t)y * c->pitch))[x];
                        px[y * c->w + x] = (q & 0xFF00FF00u) | ((q >> 16) & 255) | ((q & 255) << 16); }   /* (ARGB -> ABGR, as the course's pictures) */
                    g_w3hydTex[g_w3nhydTex] = R3_Texture(&g_w3r, px, c->w, c->h); free(px); }
                SDL_DestroySurface(c); }
            SDL_DestroySurface(s); } }
    return g_w3hydTex[g_w3nhydTex++]; }
/* this frame's broken hydrants near the eye: stumps (batch alpha 0, by texture) and water (the glow picture, added: batch alpha 3) */
static int W3HydDraw(R3Vert *v, int start, int cap, R3Batch *cb, int *nb, int nbcap, const float *eye)
{   static const int o6[6] = { 0, 3, 1, 1, 3, 2 }; int i, k, j, nv = start, first; double now = GetTickCount() / 1000.0;
    for (i = 0; i < g_w3nhyd && *nb < nbcap - 1; i++) {   /* the stumps */
        const W3Hyd *H = &g_w3hyd[i]; int t; float dx = H->x - eye[0], dz = H->z - eye[2];
        if (!H->broken || dx * dx + dz * dz > 9000.0f * 9000.0f || nv + 6 > cap || !W3InView(H->x, H->y + H->oyb * 0.5f, H->z, H->oyb + 40) || (t = W3HydStumpTex(H->tex)) < 0) continue;
        first = nv;
        for (j = 0; j < 6; j++) { int q = o6[j]; R3Vert *o = &v[nv++];
            o->x = H->x; o->y = H->y; o->z = H->z; o->rgba = 0xffffffffu; o->tex = 0;
            o->ox = (q == 0 || q == 3) ? H->oxa : H->oxb; o->oy = q < 2 ? H->oya : H->oyb; if (o->ox == 0 && o->oy == 0) o->ox = 0.01f;
            o->u = (q == 0 || q == 3) ? 0.0f : 1.0f; o->v = q < 2 ? 1.0f : 0.0f; }
        cb[*nb].first = first; cb[*nb].count = 6; cb[*nb].tex = t; cb[*nb].alpha = 0; (*nb)++; }
    if (W3GlowTex() < 0 || !g_vid.spray) return nv - start;   /* (True 3D detail > Spray and splashes off: no water jets) */
    first = nv;
    for (i = 0; i < g_w3nhyd; i++) {   /* the water: droplets in a jet (~1.3 s up and down, each its own phase; 180 of them), a mist at the foot */
        const W3Hyd *H = &g_w3hyd[i]; float dx = H->x - eye[0], dz = H->z - eye[2], top = H->oyb * 0.42f; double age;
        if (!H->broken || dx * dx + dz * dz > 6000.0f * 6000.0f || !W3InView(H->x, H->y + 160, H->z, 260)) continue;   /* (the jet: ~270 high, ~100 across) */
        age = now - H->t0;
        for (k = 0; k < 210 && nv + 6 <= cap; k++) {
            unsigned hh = (unsigned)k * 2654435761u ^ (unsigned)i * 40503u; float r1, r2, r3, ph, T = 1.3f, t, vx, vz, vy, s, a, px, py, pz; unsigned col;
            hh ^= hh >> 13; hh *= 2246822519u; hh ^= hh >> 16; r1 = (hh & 1023) / 1023.0f; r2 = ((hh >> 10) & 1023) / 1023.0f; r3 = ((hh >> 20) & 1023) / 1023.0f;
            ph = (float)fmod(age / T + r1, 1.0); t = ph * T;
            if (k < 180) {   /* the jet: thrown up 170..260 units (a lane ~250: 2.5..3.5 m), spreading a little, falling back */
                vy = 580 + 140 * r2; vx = (r3 - 0.5f) * 70; vz = (r1 - 0.5f) * 70;
                px = H->x + vx * t; pz = H->z + vz * t; py = H->y + top + vy * t - 0.5f * 1000 * t * t;
                if (py < H->y) continue;
                s = 6 + 14 * ph + 5 * r3; a = 0.42f * (1 - ph * 0.55f); }
            else {   /* the mist round the foot */
                float an = r2 * 6.2832f, rr = 10 + 70 * ph; px = H->x + cosf(an) * rr; pz = H->z + sinf(an) * rr; py = H->y + 6 + 18 * ph; s = 10 + 26 * ph; a = 0.30f * (1 - ph); }
            col = W3Rgba(0.80f, 0.86f, 0.96f, a);   /* (added to the picture: the glow pass multiplies by alpha) */
            for (j = 0; j < 6; j++) { int q = o6[j]; R3Vert *o = &v[nv++];
                o->x = px; o->y = py; o->z = pz; o->rgba = col; o->tex = 0;
                o->ox = (q == 0 || q == 3) ? -s : s; o->oy = q < 2 ? -s : s;
                o->u = (q == 0 || q == 3) ? 0.0f : 1.0f; o->v = q < 2 ? 1.0f : 0.0f; } } }
    if (nv > first && *nb < nbcap) { cb[*nb].first = first; cb[*nb].count = nv - first; cb[*nb].tex = W3GlowTex(); cb[*nb].alpha = 3; (*nb)++; }
    return nv - start; }

/* the casters (trees, bushes, lamp posts, poles: W3IsCaster) - found once among the built billboards (W3Casters) - and their shadows each frame
   (W3CasterShadows): the picture laid on the ground from its foot away from the sun (weather.inc WxSunShadow), as a card turned square to the sun,
   batch alpha 4 (render3d.h: darkens once per pixel, the riders' shadows share the stencil) */
typedef struct W3Cst { float x, y, z, oxa, oxb, oya, oyb, uL, uR, vB, vT; int piece, tex; } W3Cst;
static W3Cst *g_w3cst; static int g_w3ncst;
static int W3CstCmp(const void *a, const void *b) { return ((const W3Cst *)a)->piece - ((const W3Cst *)b)->piece; }
static int W3BldgCmp(const void *a, const void *b) { return ((const C3Bldg *)a)->piece - ((const C3Bldg *)b)->piece; }
static void W3Casters(void)
{   int i = 0, j, t, cap = 0;
    free(g_w3cst); g_w3cst = NULL; g_w3ncst = 0;
    while (i < g_w3c.nv) {
        const C3Vert *v = &g_w3c.v[i]; W3Cst *C; float oxa = 1e9f, oxb = -1e9f, oya = 1e9f, oyb = -1e9f, uL = 0, uR = 1, vB = 1, vT = 0;
        t = (int)v->tex;
        if ((v->ox == 0 && v->oy == 0) || t < 0 || t >= g_w3a.ntex || !W3IsCaster(g_w3a.tex[t].name)) { i++; continue; }
        for (j = i; j < g_w3c.nv && j < i + 6 && g_w3c.v[j].x == v->x && g_w3c.v[j].y == v->y && g_w3c.v[j].z == v->z && (int)g_w3c.v[j].tex == t; j++) {
            const C3Vert *q = &g_w3c.v[j];
            if (q->ox < oxa) { oxa = q->ox; uL = q->u; } if (q->ox > oxb) { oxb = q->ox; uR = q->u; } if (q->oy < oya) { oya = q->oy; vB = q->v; } if (q->oy > oyb) { oyb = q->oy; vT = q->v; } }
        if (g_w3ncst == cap) { W3Cst *n = (W3Cst *)realloc(g_w3cst, sizeof(W3Cst) * (cap = cap ? cap * 2 : 1024)); if (!n) break; g_w3cst = n; }
        C = &g_w3cst[g_w3ncst++];
        C->x = v->x; C->y = v->y; C->z = v->z; C->oxa = oxa; C->oxb = oxb; C->oya = oya; C->oyb = oyb; C->uL = uL; C->uR = uR; C->vB = vB; C->vT = vT; C->tex = t;
        C->piece = g_w3c.vpiece ? g_w3c.vpiece[i] : 0;
        i = j; }
    if (g_w3ncst) qsort(g_w3cst, g_w3ncst, sizeof(W3Cst), W3CstCmp);
    if (g_w3c.nbldg) qsort(g_w3c.bldg, g_w3c.nbldg, sizeof(C3Bldg), W3BldgCmp);   /* (the buildings' shadows: W3BldgShadows) */
    LOG("world3d: %d shadow casters (trees, lamps, poles), %d buildings", g_w3ncst, g_w3c.nbldg); }
static int W3CstTexCmp(const void *a, const void *b) { return g_w3cst[*(const int *)a].tex - g_w3cst[*(const int *)b].tex; }
/* the height of the 3D ground at world (x, z), from piece k0 on (the casters' shadows lie on it - user, 2026-11-15: "they should project a shadow onto
   the surface ... i can see floating shadows": a flat card at the foot's height hung in the air past a kerb, sank into a rising verge): the piece the
   point is along, its lateral off that piece's centre line, the cross-section height there (C3_GroundAt's rows: the live copy inside the live stretch),
   between the piece's two ends.  Each piece's row is made once a frame (stamp).  0 = not known there (a split road, past the cross-section's ends) */
static int W3GroundY(float x, float z, int k0, unsigned stamp, float *y)
{   static C3Row row[64]; static int rk[64], init; static const C3Course *rc[64]; static unsigned rs[64];
    const C3Frame *fr = g_w3c.frame; int k = k0, it, e; double f = 0, th, cx, cz, u, h[2];
    if (!fr || g_w3c.n < 3) return 0;
    if (!init) { for (e = 0; e < 64; e++) rk[e] = -1; init = 1; }
    for (it = 0; it < 8; it++) {   /* (the piece the point is along) */
        if (k < 0) k = 0; if (k > g_w3c.n - 2) k = g_w3c.n - 2;
        th = fr[k].heading; f = ((x - fr[k].x) * sin(th) + (z - fr[k].z) * cos(th)) / g_w3c.L;
        if (f < 0 && k > 0) k--; else if (f >= 1 && k < g_w3c.n - 2) k++; else break; }
    if (f < 0) f = 0; if (f > 1) f = 1;
    th = fr[k].heading + (fr[k + 1].heading - fr[k].heading) * f; cx = fr[k].x + (fr[k + 1].x - fr[k].x) * f; cz = fr[k].z + (fr[k + 1].z - fr[k].z) * f;
    u = (x - cx) * cos(th) - (z - cz) * sin(th);
    for (e = 0; e < 2; e++) {
        int kk = k + e, s = kk & 63, i; double best = -1e30; C3Course *cc = (g_w3l.piece && kk >= g_w3lo && kk < g_w3hi) ? &g_w3l : &g_w3c; const C3Row *r = &row[s];
        if (cc->piece[kk].layout == 2) return 0;   /* (a split road: two carriageways - the shadow stays flat) */
        if (rk[s] != kk || rc[s] != cc || rs[s] != stamp) { C3_Section(&cc->piece[kk].halfA, 0, &row[s]); rk[s] = kk; rc[s] = cc; rs[s] = stamp; }
        for (i = 0; i < C3_NP - 1; i++) {   /* (C3_RowHeight, but "not here" past the ends) */
            double a = r->u[i], b = r->u[i + 1], lo = a < b ? a : b, hi = a < b ? b : a, yy;
            if (u < lo || u > hi) continue;
            yy = hi > lo ? r->h[i] + (r->h[i + 1] - r->h[i]) * (u - a) / (b - a) : (r->h[i] > r->h[i + 1] ? r->h[i] : r->h[i + 1]);
            if (yy > best) best = yy; }
        if (best < -1e29) return 0;
        h[e] = best; }
    *y = (float)(fr[k].y + (fr[k + 1].y - fr[k].y) * f + h[0] + (h[1] - h[0]) * f);
    return 1; }
#define W3_SHL 10   /* a caster's shadow: at most W3_SHL x W3_SHW cells, draped over the ground (W3GroundY) */
#define W3_SHW 3
static int W3CasterShadows(const R3Frame *f, R3Vert *v, int start, int cap, double p, R3Batch *cb, int *nb, int nbcap)   /* vertices v[start .. cap); returns how many */
{   static int sel[512]; static unsigned stamp; float bear, len, op = WxSunShadow(&bear, &len), fw[3], sd[3], ax[3]; int kc = (int)floor(p), lo = 0, hi = g_w3ncst, n = 0, i, k, nv = start, last = -1;
    if (!g_vid.oshadow) return 0;   /* (True 3D detail > Object shadows off) */
    if (op < 0.01f || g_w3NoShadow || !g_w3ncst) return 0;
    { float d3[3], fw3[3]; fw3[0] = -f->right[2]; fw3[1] = 0; fw3[2] = f->right[0]; if (WxSunDir(fw3, f->right, d3) <= 0) return 0; }   /* (only a real sun: no sun, no tree / lamp shadows) */
    fw[0] = -f->right[2]; fw[2] = f->right[0];
    sd[0] = -(fw[0] * cosf(bear) + f->right[0] * sinf(bear)); sd[2] = -(fw[2] * cosf(bear) + f->right[2] * sinf(bear));   /* away from the sun */
    ax[0] = sd[2]; ax[2] = -sd[0];   /* (square to it) */
    while (lo < hi) { int m = (lo + hi) / 2; if (g_w3cst[m].piece < kc - 1) lo = m + 1; else hi = m; }
    for (i = lo; i < g_w3ncst && g_w3cst[i].piece <= kc + 24 && n < 512; i++) sel[n++] = i;   /* (the next ~24 pieces) */
    qsort(sel, n, sizeof(int), W3CstTexCmp);
    stamp++;
    for (i = 0; i < n && nv + 6 * W3_SHL * W3_SHW <= cap; i++) {
        const W3Cst *C = &g_w3cst[sel[i]]; static const int o6[6] = { 0, 1, 2, 0, 2, 3 }; float half = (C->oxb - C->oxa) * 0.5f, mid = (C->oxa + C->oxb) * 0.5f, H = C->oyb - C->oya, bx, bz, by, c0[2], g[W3_SHW + 1][W3_SHL + 1][3];
        int gt = W3GpuTex(C->tex), nl, nw, a, b; unsigned rgba;
        if (gt < 0 || H <= 0) continue;
        bx = C->x + f->right[0] * mid; bz = C->z + f->right[2] * mid; by = C->y + 6.0f;
        if (!W3InView(bx + sd[0] * H * len * 0.5f, by, bz + sd[2] * H * len * 0.5f, half + H * len * 0.5f + 20)) continue;   /* (the shadow out of view) */   /* (test switch RR_W3SHTEST=1: lifted, black) the foot on the ground (the pictures are sunk a little into it: oya < 0), just above it */
        /* draped over the ground: a grid of cells along the shadow (one every ~60 units) and across it (~120), each corner 6 units above the 3D ground
           under it - over a kerb, down onto the road, up a verge (where the ground is not known: the foot's height, as before) */
        nl = (int)(H * len / 60); if (nl < 1) nl = 1; if (nl > W3_SHL) nl = W3_SHL;
        nw = (int)(2 * half / 120); if (nw < 1) nw = 1; if (nw > W3_SHW) nw = W3_SHW;
        c0[0] = bx - ax[0] * half; c0[1] = bz - ax[2] * half;
        for (a = 0; a <= nw; a++) for (b = 0; b <= nl; b++) { float *q = g[a][b], s = 2 * half * a / nw, t = H * len * b / nl, gy;
            q[0] = c0[0] + ax[0] * s + sd[0] * t; q[2] = c0[1] + ax[2] * s + sd[2] * t;
            q[1] = W3GroundY(q[0], q[2], C->piece, stamp, &gy) ? gy + 6.0f : by; }
        rgba = W3Rgba(getenv("RR_W3SHTEST") ? 1.0f : (op * 1.8f > 0.5f ? 0.5f : op * 1.8f), 0, 0, 251.5f / 255.0f);   /* (darker than the bikes' halved ones; RR_W3SHTEST=1: black) */
        if (gt != last) { if (*nb >= nbcap) break; cb[*nb].first = nv; cb[*nb].count = 0; cb[*nb].tex = gt; cb[*nb].alpha = 4; (*nb)++; last = gt; }
        for (a = 0; a < nw; a++) for (b = 0; b < nl; b++) for (k = 0; k < 6; k++) { R3Vert *o = &v[nv++]; int q = o6[k], ia = a + (q == 1 || q == 2), ib = b + (q >= 2);   /* (corners 0 foot-left, 1 foot-right, 2 tip-right, 3 tip-left) */
            o->x = g[ia][ib][0]; o->y = g[ia][ib][1]; o->z = g[ia][ib][2];
            o->u = C->uL + (C->uR - C->uL) * ia / nw; o->v = C->vB + (C->vT - C->vB) * ib / nl; o->ox = 0; o->oy = 0; o->tex = 0; o->rgba = rgba; }
        cb[*nb - 1].count += 6 * nl * nw; }
    if (getenv("RR_W3LOGL")) { static int nl; if (++nl % atoi(getenv("RR_W3LOGL")) == 0) LOG("w3casters %d near, %d vertices, op %.2f len %.2f; first oya %.0f oyb %.0f y %.0f", n, nv - start, op, len, n ? g_w3cst[sel[0]].oya : 0, n ? g_w3cst[sel[0]].oyb : 0, n ? g_w3cst[sel[0]].y : 0); }
    return nv - start; }
/* the buildings' shadows (course3d.h C3_Building records each one's footprint and height): the footprint swept away from the sun by height x the
   shadow's length, on the ground at its foot - the footprint, the footprint moved, and each of its four edges swept (their union; the stencil darkens
   every pixel once).  Only with a real sun, as the trees' */
#define W3_BLDGSH 256   /* (buildings at most per frame) */
static int g_w3solid = -1, g_w3solidGen = -1;
static int W3SolidTex(void)   /* plain white, opaque (made again after the race start drops the renderer's textures) */
{   if (g_w3solidGen != g_w3texGen) { unsigned px[16]; int i; for (i = 0; i < 16; i++) px[i] = 0xffffffffu; g_w3solid = R3_Texture(&g_w3r, px, 4, 4); g_w3solidGen = g_w3texGen; }
    return g_w3solid; }
static int W3BldgShadows(const R3Frame *f, R3Vert *v, int start, int cap, double p, R3Batch *cb, int *nb, int nbcap)   /* vertices v[start .. cap); returns how many */
{   float bear, len, op = WxSunShadow(&bear, &len), fw[3], sd[3], a; int kc = (int)floor(p), lo = 0, hi = g_w3c.nbldg, i, k, e, n = 0, nv = start, gt; unsigned rgba;
    if (op < 0.01f || g_w3NoShadow || !WxEnv("RR_BLDGSHADOW", g_vid.bldgShadow, 0, 1) || !g_w3c.nbldg || *nb >= nbcap || (gt = W3SolidTex()) < 0) return 0;   /* (the Esc row BUILDING SHADOWS, off by default; test switch RR_BLDGSHADOW=0 / 1) */
    { float d3[3], fw3[3]; fw3[0] = -f->right[2]; fw3[1] = 0; fw3[2] = f->right[0]; if (WxSunDir(fw3, f->right, d3) <= 0) return 0; }   /* (no sun, no shadows) */
    fw[0] = -f->right[2]; fw[2] = f->right[0];
    sd[0] = -(fw[0] * cosf(bear) + f->right[0] * sinf(bear)); sd[2] = -(fw[2] * cosf(bear) + f->right[2] * sinf(bear));   /* away from the sun (as W3CasterShadows) */
    a = getenv("RR_W3SHTEST") ? 1.0f : (op * 1.8f > 0.5f ? 0.5f : op * 1.8f); rgba = W3Rgba(a, 0, 0, 251.5f / 255.0f);   /* (test switch RR_W3SHTEST=1: black) */
    while (lo < hi) { int m = (lo + hi) / 2; if (g_w3c.bldg[m].piece < kc - 8) lo = m + 1; else hi = m; }   /* (a few behind too: long shadows reach forward) */
    cb[*nb].first = nv; cb[*nb].count = 0; cb[*nb].tex = gt; cb[*nb].alpha = 4;
    for (i = lo; i < g_w3c.nbldg && g_w3c.bldg[i].piece <= kc + 40 && n < W3_BLDGSH && nv + 36 <= cap; i++, n++) {
        const C3Bldg *B = &g_w3c.bldg[i]; float g[4][3], m[4][3], (*q[6][4])[3];   /* g: the foot (a little above the ground), m: moved away from the sun */
        for (k = 0; k < 4; k++) { g[k][0] = B->f[k][0]; g[k][1] = B->f[k][1] + 6.0f; g[k][2] = B->f[k][2];
            m[k][0] = g[k][0] + sd[0] * B->h * len; m[k][1] = g[k][1]; m[k][2] = g[k][2] + sd[2] * B->h * len; }
        for (k = 0; k < 4; k++) { q[0][k] = &g[k]; q[1][k] = &m[k]; }
        for (e = 0; e < 4; e++) { q[2 + e][0] = &g[e]; q[2 + e][1] = &g[(e + 1) & 3]; q[2 + e][2] = &m[(e + 1) & 3]; q[2 + e][3] = &m[e]; }
        for (k = 0; k < 6; k++) { static const int o6[6] = { 0, 1, 2, 0, 2, 3 }; int j;
            for (j = 0; j < 6; j++) { R3Vert *o = &v[nv++]; const float *c = *q[k][o6[j]];
                o->x = c[0]; o->y = c[1]; o->z = c[2]; o->u = 0.5f; o->v = 0.5f; o->ox = 0; o->oy = 0; o->tex = 0; o->rgba = rgba; } } }
    if (getenv("RR_W3LOGL")) { static int nl; if (++nl % atoi(getenv("RR_W3LOGL")) == 0) LOG("w3bldgsh %d near (piece %d .. %d), op %.2f len %.2f; first h %.0f foot %.0f,%.0f,%.0f", n, kc - 8, kc + 40, op, len, n ? g_w3c.bldg[lo].h : 0, n ? g_w3c.bldg[lo].f[0][0] : 0, n ? g_w3c.bldg[lo].f[0][1] : 0, n ? g_w3c.bldg[lo].f[0][2] : 0); }
    if (nv == start) return 0;
    cb[*nb].count = nv - start; (*nb)++;
    return nv - start; }
/* the buildings' shadows, reworked (user, 2026-11-14: "the shadows from the buildings are dreadful. the shadows should apply to the road and footpath"):
   a real shadow map.  The walls' depth is drawn from the sun (orthographic, R3_SHMAP square over a 2 W3_SHR wide box centred W3_SHAHEAD ahead of the
   eye, its texel grid fixed in the world so the edges do not crawl as the camera moves) and every lit surface that faces the sun - road, footpath,
   kerbs, the other buildings, the ground - is darkened where a wall stands between it and the sun (render3d.h).  The Esc row BUILDING SHADOWS /
   test switch RR_BLDGSHADOW=0 / 1 as before; only with a real sun */
#define W3_SHR 5200.0f
#define W3_SHAHEAD 3000.0f
static void W3SunShadowMap(R3Frame *f, const float *eye, const float *fwd)
{   float bear, len, op = WxSunShadow(&bear, &len), d[3], c[3], L[3], dn[3], up[3] = { 0, 1, 0 }, view[16], proj[16], l, ts, cx, cy; const float Z = 40000.0f;
    f->shadow = 0;
    if (getenv("RR_W3LOGSH")) { static int m; if (m++ % 300 == 0) LOG("w3sunshadow in: op %.2f sun.w %.2f noshadow %d option %d", op, f->sun[3], g_w3NoShadow, WxEnv("RR_BLDGSHADOW", g_vid.bldgShadow, 0, 1)); }
    if (op < 0.01f || f->sun[3] <= 0 || g_w3NoShadow || !WxEnv("RR_BLDGSHADOW", g_vid.bldgShadow, 0, 1)) return;
    d[0] = f->sun[0]; d[1] = f->sun[1]; d[2] = f->sun[2]; l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]); if (l < 1e-4f || d[1] <= 0.03f * l) return;
    d[0] /= l; d[1] /= l; d[2] /= l;
    { float fl = sqrtf(fwd[0] * fwd[0] + fwd[2] * fwd[2]); if (fl < 1e-4f) fl = 1; c[0] = eye[0] + fwd[0] / fl * W3_SHAHEAD; c[1] = eye[1]; c[2] = eye[2] + fwd[2] / fl * W3_SHAHEAD; }
    L[0] = c[0] + d[0] * Z * 0.5f; L[1] = c[1] + d[1] * Z * 0.5f; L[2] = c[2] + d[2] * Z * 0.5f; dn[0] = -d[0]; dn[1] = -d[1]; dn[2] = -d[2];
    if (d[1] > 0.995f) { up[0] = 1; up[1] = 0; }
    R3_LookTo(view, L, dn, up);
    ts = 2 * W3_SHR / R3_SHMAP;   /* (the texel grid fixed in the world: the box's centre snapped to whole texels in the sun's view) */
    cx = view[12]; cy = view[13]; view[12] = floorf(cx / ts) * ts; view[13] = floorf(cy / ts) * ts;
    memset(proj, 0, sizeof proj); proj[0] = 1.0f / W3_SHR; proj[5] = 1.0f / W3_SHR; proj[10] = -1.0f / Z; proj[14] = 1.0f; proj[15] = 1.0f;   /* (reversed depth: 1 at the sun's end) */
    R3_Mul(f->shadowVP, view, proj);
    f->shadow = op * 1.6f > 0.55f ? 0.55f : op * 1.6f;
    if (getenv("RR_W3LOGSH")) { static int n; if (n++ % 300 == 0) LOG("w3sunshadow op %.2f sun %.2f %.2f %.2f strength %.2f map %d", op, d[0], d[1], d[2], f->shadow, g_w3r.shSrv != NULL); } }   /* (test switch RR_W3LOGSH=1) */
/* ---- snow tracks (weather Snow): a map of the ground round the camera, TK_N x TK_N cells of TK_CELL units in world x / z, wrapping (render3d.h samples
   it at world x / z / (TK_N TK_CELL)).  Every rider / police rider / car marks the path of its wheels into it each frame and the shader clears the snow
   there.  The window follows the camera (centred ahead of it); cells scrolling in are cleared.  Drawing only - nothing the game reads ---- */
#define TK_N 2048
#define TK_CELL 6.0f   /* (a bike tyre ~ 1 .. 2 cells) */
static unsigned char g_tk[TK_N * TK_N]; static int g_tkOx, g_tkOz, g_tkInit, g_tkDirty, g_tkR0 = TK_N, g_tkR1 = -1;   /* (dirty texture rows R0 .. R1: only those are uploaded) */
static ID3D11Texture2D *g_tkTex; static ID3D11ShaderResourceView *g_tkSrv;
typedef struct TkPrev { unsigned id; int wheel; float x, z; double t; } TkPrev;
static TkPrev g_tkp[128];
static int TkIdx(int c) { return ((c % TK_N) + TK_N) % TK_N; }
static void TkCol(int c) { int i = TkIdx(c), r; for (r = 0; r < TK_N; r++) g_tk[r * TK_N + i] = 0; }
static void TkFollow(float x, float z)
{   int nx = (int)floor(x / TK_CELL) - TK_N / 2, nz = (int)floor(z / TK_CELL) - TK_N / 2, c;
    if (!g_tkInit || abs(nx - g_tkOx) >= TK_N || abs(nz - g_tkOz) >= TK_N) { memset(g_tk, 0, sizeof g_tk); memset(g_tkp, 0, sizeof g_tkp); g_tkOx = nx; g_tkOz = nz; g_tkInit = 1; g_tkDirty = 1; g_tkR0 = 0; g_tkR1 = TK_N - 1; return; }
    if (nx == g_tkOx && nz == g_tkOz) return;
    for (c = g_tkOx + TK_N; c < nx + TK_N; c++) TkCol(c);   /* columns / rows that scroll in */
    for (c = nx; c < g_tkOx; c++) TkCol(c);
    for (c = g_tkOz + TK_N; c < nz + TK_N; c++) memset(&g_tk[TkIdx(c) * TK_N], 0, TK_N);
    for (c = nz; c < g_tkOz; c++) memset(&g_tk[TkIdx(c) * TK_N], 0, TK_N);
    g_tkOx = nx; g_tkOz = nz; g_tkDirty = 1; g_tkR0 = 0; g_tkR1 = TK_N - 1; }
static void TkStamp(float x, float z, int wide)
{   int cx = (int)floor(x / TK_CELL), cz = (int)floor(z / TK_CELL), i, j;
    for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) { int X = cx + i, Z = cz + j, v; unsigned char *p;
        if (X < g_tkOx || X >= g_tkOx + TK_N || Z < g_tkOz || Z >= g_tkOz + TK_N) continue;
        v = (!i && !j) ? 255 : ((!i || !j) ? (wide ? 200 : 90) : (wide ? 120 : 0));   /* (a bike: one cell, a soft edge; a car wider) */
        p = &g_tk[TkIdx(Z) * TK_N + TkIdx(X)]; if (*p < v) { int r = TkIdx(Z); *p = (unsigned char)v; g_tkDirty = 1; if (r < g_tkR0) g_tkR0 = r; if (r > g_tkR1) g_tkR1 = r; } } }
static void TkWheel(unsigned id, int wheel, const float *pt, double now, int wide)
{   TkPrev *e = NULL; int i, old = 0;
    for (i = 0; i < 128; i++) { if (g_tkp[i].id == id && g_tkp[i].wheel == wheel) { e = &g_tkp[i]; break; } if (g_tkp[i].t < g_tkp[old].t) old = i; }
    if (e) { float dx = pt[0] - e->x, dz = pt[2] - e->z, l = sqrtf(dx * dx + dz * dz); int n, k;
        if (l < 1500) { n = (int)(l / (TK_CELL * 0.5f)) + 1; for (k = 1; k <= n; k++) TkStamp(e->x + dx * k / n, e->z + dz * k / n, wide); }   /* the path since the last frame */
        else TkStamp(pt[0], pt[2], wide); }
    else { e = &g_tkp[old]; e->id = id; e->wheel = wheel; TkStamp(pt[0], pt[2], wide); }
    e->x = pt[0]; e->z = pt[2]; e->t = now; }
static void TkUpdate(double vp, const float *eye, const float *fwd)
{   static int gen = -1; int i, j, k; double now = WxSeconds();
    if (gen != g_w3texGen) { gen = g_w3texGen; g_tkInit = 0; }   /* (a new race) */
    TkFollow(eye[0] + fwd[0] * 4000, eye[2] + fwd[2] * 4000);
    for (i = 0; i < g_w3ns; i++) {
        const W3Spr *s = &g_w3s[i]; double cx, ry, sc, x0 = 1e9, x1 = -1e9; int car = s->kind == RRO_CAR;
        if (s->kind != RRO_RIDER && s->kind != RRO_PLAYER && s->kind != RRO_CAR && s->kind != RRO_COP) continue;
        for (j = 0; j < i; j++) if (g_w3s[j].id == s->id && g_w3s[j].kind == s->kind) break;
        if (j < i || !W3OrigAt(s->objDepth, &cx, &ry, &sc)) continue;
        for (j = i; j < g_w3ns; j++) if (g_w3s[j].id == s->id && g_w3s[j].kind == s->kind && g_w3s[j].tx) for (k = 0; k < 4; k++) { if (g_w3s[j].x[k] < x0) x0 = g_w3s[j].x[k]; if (g_w3s[j].x[k] > x1) x1 = g_w3s[j].x[k]; }
        if (x1 <= x0) continue;
        {   double U = s->lat, q = vp + s->objDepth - 1.0, half = car ? (x1 - x0) / sc * 0.3 : 0; float pt[3];   /* (the game's own lateral place) a bike: one track; a car: two */
            for (k = 0; k < (car ? 2 : 1); k++) { C3_PointF(&g_w3c, W3Fr(), q, U + (k ? half : -half), 0, pt); TkWheel(s->id, k, pt, now, car); } } }
    if (!g_tkTex && g_w3r.dev) { D3D11_TEXTURE2D_DESC td; memset(&td, 0, sizeof td);
        td.Width = td.Height = TK_N; td.MipLevels = td.ArraySize = 1; td.Format = DXGI_FORMAT_R8_UNORM; td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (SUCCEEDED(ID3D11Device_CreateTexture2D(g_w3r.dev, &td, NULL, &g_tkTex))) ID3D11Device_CreateShaderResourceView(g_w3r.dev, (ID3D11Resource *)g_tkTex, NULL, &g_tkSrv);
        g_tkDirty = 1; g_tkR0 = 0; g_tkR1 = TK_N - 1; }
    if (g_tkTex && g_tkDirty && g_tkR1 >= g_tkR0) { D3D11_BOX bx; bx.left = 0; bx.right = TK_N; bx.top = (UINT)g_tkR0; bx.bottom = (UINT)g_tkR1 + 1; bx.front = 0; bx.back = 1;   /* (the rows that changed) */
        ID3D11DeviceContext_UpdateSubresource(g_w3r.imm, (ID3D11Resource *)g_tkTex, 0, &bx, g_tk + (size_t)g_tkR0 * TK_N, TK_N, 0); }
    g_tkDirty = 0; g_tkR0 = TK_N; g_tkR1 = -1; }

/* ---- blood: when a rider is hit (race\rider.c rr_RiderApplyHit -> RR_HitFx with the hit rider's object) a burst of droplets flies from him in the
   3D world (gravity, fading), drawn over the world (W3Blood, GpuCompose).  Drawing only ---- */
#define BL_N 1500
static SDL_Texture *g_wxGlow; static void WxGlowMake(void);   /* weather.inc: the soft round spot */
typedef struct W3Drop { float p[3], v[3], life, max, size; int water; float col[3]; } W3Drop;   /* (water: 1 a puddle splash, 2 a clod of dirt, 3 bits of grass - W3Splash; 0 blood; col: dirt / grass colour) */
static W3Drop g_bl[BL_N]; static int g_blN; static volatile unsigned g_blHit[8]; static volatile LONG g_blHitN;
static float g_w3eye[3], g_w3fw[3], g_w3rt[3];   /* this frame's camera (W3Draw), for W3Blood */
static void W3BloodClear(void) { g_blN = 0; g_blHitN = 0; }
__declspec(dllexport) void __stdcall RR_HitFx(unsigned obj) { LONG i = InterlockedIncrement(&g_blHitN) - 1; if (i < 8) g_blHit[i] = obj; }
/* blood on the camera: a hit close in front of it throws splats onto the screen round the rider (frame pixels), each running down a little, gone in
   about a second (W3Blood draws them) */
typedef struct W3Splat { float x, y, r, life, max, drip; } W3Splat;
static W3Splat g_bs[24]; static int g_bsN;
static void W3ScreenSplats(int i, unsigned *s)
{   float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, cx, cy; int j, k, n; double d = g_w3s[i].dObj;
    if (g_w3Pcap < g_w3ns || d > 1800) return;
    for (j = 0; j < g_w3ns; j++) if (g_w3s[j].id == g_w3s[i].id) for (k = 0; k < 4; k++) {
        float X = g_w3P[8 * j + k], Y = g_w3P[8 * j + 4 + k]; if (X < x0) x0 = X; if (X > x1) x1 = X; if (Y < y0) y0 = Y; if (Y > y1) y1 = Y; }
    if (x1 <= x0) return;
    cx = (x0 + x1) * 0.5f; cy = y0 + (y1 - y0) * 0.4f;
    n = 3 + (int)((1800 - d) / 300);   /* (the nearer, the more) */
    for (k = 0; k < n; k++) { W3Splat *b = g_bsN < 24 ? &g_bs[g_bsN++] : &g_bs[(int)(GfRnd(s) * 23.99f)]; float sp = (float)(g_gpuClip[2] - g_gpuClip[0]) * 0.22f;
        b->x = cx + (GfRnd(s) * 2 - 1) * sp; b->y = cy + (GfRnd(s) * 2 - 1) * sp * 0.6f;
        b->r = (float)(g_gpuClip[2] - g_gpuClip[0]) * (0.012f + 0.035f * GfRnd(s)) * (float)(1.6 - d / 1800); b->max = b->life = 0.6f + 0.5f * GfRnd(s); b->drip = 0; } }
static void W3ScreenBlood(float dt)
{   SDL_Vertex v[24 * 8]; int ix[24 * 12], n = 0, i, q; float dk = 1 - 0.5f * WxDark();
    if (!g_wxGlow) WxGlowMake();
    for (i = 0; i < g_bsN; i++) { W3Splat *b = &g_bs[i]; float a, rr[2], hh[2];
        b->life -= dt; if (b->life <= 0) { g_bs[i--] = g_bs[--g_bsN]; continue; }
        b->drip += dt * b->r * 1.4f;
        a = b->life / b->max; a = a > 0.6f ? 0.9f : 0.9f * a / 0.6f;   /* (held, then fades quickly) */
        rr[0] = b->r; hh[0] = b->r; rr[1] = b->r * 0.3f; hh[1] = b->r * 0.4f + b->drip;   /* the splat, its run */
        for (q = 0; q < 2; q++) { int c; float yc = q ? b->y + hh[1] * 0.5f : b->y;
            for (c = 0; c < 4; c++) { SDL_Vertex *o = &v[n * 4 + c]; o->position.x = b->x + ((c & 1) ? rr[q] : -rr[q]); o->position.y = yc + ((c & 2) ? hh[q] : -hh[q]) * (q ? 0.5f : 1.0f);
                o->color.r = 0.42f * dk; o->color.g = 0.0f; o->color.b = 0.01f; o->color.a = a * (q ? 0.8f : 1.0f); o->tex_coord.x = (c & 1) ? 1.0f : 0.0f; o->tex_coord.y = (c & 2) ? 1.0f : 0.0f; }
            ix[n * 6] = n * 4; ix[n * 6 + 1] = n * 4 + 1; ix[n * 6 + 2] = n * 4 + 2; ix[n * 6 + 3] = n * 4 + 1; ix[n * 6 + 4] = n * 4 + 3; ix[n * 6 + 5] = n * 4 + 2; n++; } }
    if (n && g_wxGlow) { SDL_Rect vr; vr.x = g_gpuClip[0]; vr.y = g_gpuClip[1]; vr.w = g_gpuClip[2] - g_gpuClip[0]; vr.h = g_gpuClip[3] - g_gpuClip[1] + 1; SDL_SetRenderClipRect(g_ren, &vr);
        SDL_SetTextureBlendMode(g_wxGlow, SDL_BLENDMODE_BLEND); SDL_SetTextureColorModFloat(g_wxGlow, 1, 1, 1); SDL_SetTextureAlphaModFloat(g_wxGlow, 1);
        SDL_RenderGeometry(g_ren, g_wxGlow, v, n * 4, ix, n * 6); SDL_SetRenderClipRect(g_ren, NULL); } }
static void W3BloodSpawn(double vp)
{   static unsigned s = 4711; int h, n = g_blHitN > 8 ? 8 : (int)g_blHitN, i, k;
    for (h = 0; h < n; h++) for (i = 0; i < g_w3ns; i++) if (g_w3s[i].id == g_blHit[h] && g_w3s[i].objDepth >= 0) {
        float c[3], ah[3], bk[3], sd[3], l; double q = vp + g_w3s[i].objDepth - 1.0;
        C3_PointF(&g_w3c, W3Fr(), q, g_w3s[i].lat, 280, c);   /* about the rider's chest */
        C3_PointF(&g_w3c, W3Fr(), q + 0.5, g_w3s[i].lat, 280, ah);   /* the way he rides (along the road): the blood flies back from him, the way he came */
        bk[0] = c[0] - ah[0]; bk[2] = c[2] - ah[2]; l = (float)sqrt(bk[0] * bk[0] + bk[2] * bk[2]); if (l < 1) l = 1; bk[0] /= l; bk[2] /= l;
        sd[0] = -bk[2]; sd[2] = bk[0];   /* (sideways) */
        for (k = 0; k < 70 && g_blN < BL_N; k++) { W3Drop *d = &g_bl[g_blN++]; float sp = 500 + 1100 * GfRnd(&s), sw = (GfRnd(&s) - 0.5f) * 0.7f;   /* a narrow fan, +-20 deg */
            d->p[0] = c[0] + (GfRnd(&s) - 0.5f) * 60; d->p[1] = c[1] + (GfRnd(&s) - 0.5f) * 80; d->p[2] = c[2] + (GfRnd(&s) - 0.5f) * 60;
            d->v[0] = (bk[0] + sd[0] * sw) * sp; d->v[2] = (bk[2] + sd[2] * sw) * sp; d->v[1] = -100 + 350 * GfRnd(&s);
            d->max = d->life = 0.5f + 0.7f * GfRnd(&s); d->size = 6 + 16 * GfRnd(&s); d->water = 0; }
        W3ScreenSplats(i, &s);   /* (close in front: on the screen too) */
        break; }
    g_blHitN = 0; }
/* ---- puddle splashes (rain): where a bike's tyres are in a puddle (W3PuddleAt: the shader's own puddle pattern, render3d.h Nu - an integer hash, the
   same here), water sprays back from them (the opposite of the way he rides) and out to the sides, in the blood's particles (water = 1); the player's
   bike (and a near rider, quieter) makes a splash sound on the way in (dsound.dll RR_Splash, synthesised there).  Drawing / sound only ---- */
static float W3Hu(float x, float y) { unsigned h = (unsigned)(int)x * 374761393u + (unsigned)(int)y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; return (h & 65535u) / 65535.0f; }
static float W3Nu(float x, float y)
{   float qx = floorf(x), qy = floorf(y), fx = x - qx, fy = y - qy, a, b;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    a = W3Hu(qx, qy) + (W3Hu(qx + 1, qy) - W3Hu(qx, qy)) * fx; b = W3Hu(qx, qy + 1) + (W3Hu(qx + 1, qy + 1) - W3Hu(qx, qy + 1)) * fx;
    return a + (b - a) * fy; }
static float W3PuddleAt(float x, float z, float puddles)   /* 0..1: how much puddle at world x / z (render3d.h psmain, fad.z = puddles) */
{   float nn = W3Nu(x / 300 + 17.3f, z / 300 + 17.3f) * 0.72f + W3Nu(x / 85 + 3.3f, z / 85 + 3.3f) * 0.28f, th = 0.59f - (puddles > 2 ? 1 : puddles > 1 ? puddles - 1 : 0) * 0.06f, t;
    if (puddles <= 0) return 0;
    t = (nn - th) / 0.025f; if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3 - 2 * t); }
static double W3Speed(unsigned id) { int i; for (i = 0; i < 64; i++) if (g_w3rv[i].id == id) return g_w3rv[i].v; return 0; }   /* pieces a second (W3Brake) */
/* what a tyre at world (x, z) rides on, off the tarmac (user, 2026-11-14: "when riding over dirt and grass show the dirt fly up from the rear tyre"):
   the ground triangle under it among the grass's (W3GrassCollect: the live stretch's, else the file's) - 2 = dirt (vertex alpha 239), 3 = grass, 0 = none;
   col = its colour (the dirt's, a little darker; the grass's texture there) */
static int W3GroundAt(float x, float z, float *col)
{   int set, i; const char *ts = getenv("RR_W3SOILALL");   /* (test switch RR_W3SOILALL=2 / 3: every tyre on dirt / grass) */
    if (ts && (*ts == 0x32 || *ts == 0x33)) { if (*ts == 0x32) { col[0] = 0.40f; col[1] = 0.30f; col[2] = 0.20f; return 2; } col[0] = 0.22f; col[1] = 0.40f; col[2] = 0.14f; return 3; }
    for (set = 0; set < 2; set++) {
        const W3GrTri *arr = set ? g_w3gr : g_w3grL; int na = set ? g_w3ngr : (g_w3lo >= 0 ? g_w3ngrL : 0);
        for (i = 0; i < na; i++) { const W3GrTri *T = &arr[i]; float d1, d2, d3; int neg, pos;
            if ((x < T->a[0] && x < T->b[0] && x < T->c[0]) || (x > T->a[0] && x > T->b[0] && x > T->c[0]) || (z < T->a[2] && z < T->b[2] && z < T->c[2]) || (z > T->a[2] && z > T->b[2] && z > T->c[2])) continue;
            d1 = (x - T->b[0]) * (T->a[2] - T->b[2]) - (T->a[0] - T->b[0]) * (z - T->b[2]); d2 = (x - T->c[0]) * (T->b[2] - T->c[2]) - (T->b[0] - T->c[0]) * (z - T->c[2]);
            d3 = (x - T->a[0]) * (T->c[2] - T->a[2]) - (T->c[0] - T->a[0]) * (z - T->a[2]); neg = d1 < 0 || d2 < 0 || d3 < 0; pos = d1 > 0 || d2 > 0 || d3 > 0;
            if (neg && pos) continue;
            if (T->dry) { col[0] = 0.40f; col[1] = 0.30f; col[2] = 0.20f; return 2; }
            if (T->tex >= 0 && g_w3grPx && g_w3grPx[T->tex]) { const RxTex *t = &g_w3a.tex[T->tex]; unsigned c = g_w3grPx[T->tex][(t->h / 2) * t->w + t->w / 2];
                col[0] = (c & 255) / 255.0f * 0.9f; col[1] = ((c >> 8) & 255) / 255.0f * 0.9f; col[2] = ((c >> 16) & 255) / 255.0f * 0.9f; }
            else { col[0] = 0.22f; col[1] = 0.40f; col[2] = 0.14f; }
            return 3; }
        if (!set && na) break; }   /* (in the live stretch: its own ground only) */
    return 0; }
static void W3Splash(const R3Frame *f, double vp, int w)
{   static unsigned s = 9917; static double last = -1; static struct { unsigned id; float in; } was[32]; static void (__stdcall *snd)(float, float); static int init;
    double now = WxSeconds(), dt = last < 0 ? 0 : now - last; int i, j, k;
    last = now; if (dt <= 0 || dt > 0.1) return;
    if (!init) { HMODULE m = GetModuleHandleA("rrdsound.dll"); init = 1; snd = m ? (void (__stdcall *)(float, float))GetProcAddress(m, "RR_Splash") : NULL; }
    if (g_w3Pcap < g_w3ns) return;   /* (puddles in the rain; dirt / grass off the tarmac any time) */
    for (i = 0; i < g_w3ns; i++) {
        const W3Spr *sp = &g_w3s[i]; float foot[3], fx = 0, fy = -1e9f, pd, t0[3], t1[3], bk[2], sd[2], l; double v, q; int n, slot = -1;
        if (!(sp->kind == RRO_RIDER || sp->kind == RRO_PLAYER || sp->kind == RRO_COP) || sp->objDepth < 0) continue;
        for (j = 0; j < i; j++) if (g_w3s[j].id == sp->id) break;
        if (j < i) continue;   /* (one per bike) */
        for (j = i; j < g_w3ns; j++) if (g_w3s[j].id == sp->id && !W3IsGameShadow(&g_w3s[j])) for (k = 0; k < 4; k++) if (g_w3P[8 * j + 4 + k] > fy) { fy = g_w3P[8 * j + 4 + k]; fx = (g_w3P[8 * j] + g_w3P[8 * j + 1] + g_w3P[8 * j + 2] + g_w3P[8 * j + 3]) / 4; }
        if (fy < -1e8f) continue;
        W3Unproj(f, fx, fy, sp->dObj > 30 ? sp->dObj : 30, foot);   /* the tyres on the ground */
        v = W3Speed(sp->id); pd = getenv("RR_W3SPLASHALL") ? 1.0f : f->puddles > 0 ? W3PuddleAt(foot[0], foot[2], f->puddles) : 0;   /* (test switch RR_W3SPLASHALL=1: every tyre in a puddle) */
if (getenv("RR_W3LOGL") && sp->kind == RRO_PLAYER) { static int nl; if (++nl % atoi(getenv("RR_W3LOGL")) == 0) LOG("w3splash v %.1f puddle %.2f at %.0f,%.0f drops %d", v, pd, foot[0], foot[2], g_blN); }
        for (j = 0; j < 32; j++) if (was[j].id == sp->id) { slot = j; break; }
        if (slot < 0) { for (j = 0; j < 32 && was[j].id; j++) {} slot = j < 32 ? j : (int)(sp->id % 32); was[slot].id = sp->id; was[slot].in = 0; }
        if (snd && pd > 0.5f && was[slot].in <= 0.5f && v > 1.5) {   /* into a puddle: the sound (the player full, a rider nearby quieter) */
            float st = (float)(v / 25.0); if (st > 1) st = 1; if (st < 0.35f) st = 0.35f;
            if (sp->kind != RRO_PLAYER) { double dd = sp->dObj; st *= dd < 3000 ? (float)(0.5 * (1 - dd / 3000)) : 0; }
            if (st > 0.02f) snd(st, w > 0 ? fx / w : 0.5f); }
        was[slot].in = pd;
        if (pd < 0.3f && v >= 1.5 && g_vid.spray && g_blN < BL_N - 8 && sp->dObj < 6000) {   /* off the tarmac: dirt / grass thrown up from the rear tyre */
            float col[3]; int gk = W3GroundAt(foot[0], foot[2], col);
            if (gk) {
                q = vp + sp->objDepth - 1.0;
                C3_PointF(&g_w3c, W3Fr(), q, sp->lat, 0, t0); C3_PointF(&g_w3c, W3Fr(), q + 0.5, sp->lat, 0, t1);
                bk[0] = t0[0] - t1[0]; bk[1] = t0[2] - t1[2]; l = (float)sqrt(bk[0] * bk[0] + bk[1] * bk[1]); if (l < 1) continue; bk[0] /= l; bk[1] /= l;
                sd[0] = -bk[1]; sd[1] = bk[0];
                n = (int)(v * (gk == 2 ? 14 : 11) * dt * 30); if (n < 2) n = 2; if (n > 22) n = 22;
                for (k = 0; k < n && g_blN < BL_N; k++) { W3Drop *d = &g_bl[g_blN++]; float sp2 = (float)(v * 4) * (0.3f + 0.7f * GfRnd(&s)), side = (GfRnd(&s) * 2 - 1), sh = 0.75f + 0.5f * GfRnd(&s);
                    if (sp2 > 380) sp2 = 380;
                    d->p[0] = foot[0] + sd[0] * side * 10; d->p[1] = foot[1] + 4; d->p[2] = foot[2] + sd[1] * side * 10;   /* (the rear tyre's contact patch) */
                    d->v[0] = bk[0] * sp2 + sd[0] * side * (40 + 100 * GfRnd(&s)); d->v[2] = bk[1] * sp2 + sd[1] * side * (40 + 100 * GfRnd(&s)); d->v[1] = 200 + 420 * GfRnd(&s);   /* (up and back, a rooster tail) */
                    d->max = d->life = 0.30f + 0.35f * GfRnd(&s); d->size = gk == 2 ? 3.0f + 6.0f * GfRnd(&s) : 2.5f + 3.5f * GfRnd(&s); d->water = gk;
                    d->col[0] = col[0] * sh; d->col[1] = col[1] * sh; d->col[2] = col[2] * sh;
                    if (gk == 2 && (k & 3) == 0 && g_blN < BL_N) { W3Drop *u = &g_bl[g_blN++]; *u = *d; u->water = 4; u->size = 14 + 16 * GfRnd(&s); u->v[0] *= 0.35f; u->v[2] *= 0.35f; u->v[1] = 120 + 160 * GfRnd(&s); u->max = u->life = 0.7f + 0.5f * GfRnd(&s);   /* (a dust cloud: big, soft, faint, hanging) */
                        u->col[0] = col[0] * 1.25f; u->col[1] = col[1] * 1.2f; u->col[2] = col[2] * 1.15f; } } } }
        if (pd < 0.3f || v < 1.5 || !g_vid.spray || g_blN >= BL_N - 8) continue;   /* (Spray and splashes off: the splash sound stays) */
        q = vp + sp->objDepth - 1.0;   /* the way he rides: along the road there */
        C3_PointF(&g_w3c, W3Fr(), q, sp->lat, 0, t0); C3_PointF(&g_w3c, W3Fr(), q + 0.5, sp->lat, 0, t1);
        bk[0] = t0[0] - t1[0]; bk[1] = t0[2] - t1[2]; l = (float)sqrt(bk[0] * bk[0] + bk[1] * bk[1]); if (l < 1) continue; bk[0] /= l; bk[1] /= l;
        sd[0] = -bk[1]; sd[1] = bk[0];
        n = (int)(v * 10 * dt * 30 * pd); if (n < 2) n = 2; if (n > 16) n = 16;   /* (a fine spray: many small drops) */
        for (k = 0; k < n && g_blN < BL_N; k++) { W3Drop *d = &g_bl[g_blN++]; float sp2 = (float)(v * 5) * (0.3f + 0.7f * GfRnd(&s)), side = (GfRnd(&s) * 2 - 1), tyre = GfRnd(&s) < 0.6f ? 0.0f : 140.0f;
            if (sp2 > 450) sp2 = 450;
            d->p[0] = foot[0] - bk[0] * tyre + sd[0] * side * 12; d->p[1] = foot[1] + 2; d->p[2] = foot[2] - bk[1] * tyre + sd[1] * side * 12;   /* the bottom of the rear / front tyre */
            d->v[0] = bk[0] * sp2 + sd[0] * side * (50 + 120 * GfRnd(&s)); d->v[2] = bk[1] * sp2 + sd[1] * side * (50 + 120 * GfRnd(&s)); d->v[1] = 300 + 450 * GfRnd(&s);   /* up and back, a narrow fan */
            d->max = d->life = 0.2f + 0.25f * GfRnd(&s); d->size = 2 + 3 * GfRnd(&s); d->water = 1; } } }
static void W3Blood(int fw, int fh)
{   static double last = -1; static SDL_Vertex v[BL_N * 4]; static int ix[BL_N * 6]; double now = WxSeconds(); float dt = last < 0 ? 0 : (float)(now - last), F, cx, hy; int i, n = 0;
    (void)fw; (void)fh; last = now;
    if (!g_w3On) return;
    if (!g_blN) { if (g_bsN) W3ScreenBlood(dt > 0.1f ? 0.1f : dt); return; }   /* (blood on the screen: below) */
    if (dt > 0.1f) dt = 0.1f;
    F = (float)(W3_F * (g_w3cam.pxPerUnit > 0 ? g_w3cam.pxPerUnit : 1)); cx = (float)g_w3cam.centreX; hy = (float)g_w3cam.horizonY;
    for (i = 0; i < g_blN; i++) {
        W3Drop *d = &g_bl[i]; float r[3], z, x, y, a, s; int q;
        d->life -= dt; if (d->life <= 0) { g_bl[i--] = g_bl[--g_blN]; continue; }
        d->v[1] -= (d->water == 4 ? 260 : 2600) * dt; if (d->water == 4) { d->v[0] *= 1 - 2.5f * dt; d->v[2] *= 1 - 2.5f * dt; d->size += 18 * dt; }   /* (dust: hangs, slows, spreads) */
        d->p[0] += d->v[0] * dt; d->p[1] += d->v[1] * dt; d->p[2] += d->v[2] * dt;
        r[0] = d->p[0] - g_w3eye[0]; r[1] = d->p[1] - g_w3eye[1]; r[2] = d->p[2] - g_w3eye[2];
        z = r[0] * g_w3fw[0] + r[2] * g_w3fw[2]; if (z < 40) continue;
        x = cx + F * (r[0] * g_w3rt[0] + r[2] * g_w3rt[2]) / z; y = hy - F * r[1] / z; s = F * d->size / z; if (s < 0.6f) s = 0.6f; if (s > (d->water == 4 ? 70 : 9)) s = d->water == 4 ? 70.0f : 9.0f;
        if (x < g_gpuClip[0] - 80 || x > g_gpuClip[2] + 80 || y < g_gpuClip[1] - 80 || y > g_gpuClip[3] + 80) continue;   /* (off the 3D view: not drawn, still moving) */
        a = d->life / d->max;
        if (d->water == 1) {   /* a water drop: a short streak along its flight (where it was ~35 ms ago -> now): spray, not blobs */
            float t2[3], z2, x2, y2, dx, dy, l, nx, ny; int c2;
            t2[0] = r[0] - d->v[0] * 0.035f; t2[1] = r[1] - d->v[1] * 0.035f; t2[2] = r[2] - d->v[2] * 0.035f;
            z2 = t2[0] * g_w3fw[0] + t2[2] * g_w3fw[2]; if (z2 < 40) z2 = 40;
            x2 = cx + F * (t2[0] * g_w3rt[0] + t2[2] * g_w3rt[2]) / z2; y2 = hy - F * t2[1] / z2;
            dx = x - x2; dy = y - y2; l = sqrtf(dx * dx + dy * dy); if (l < 0.5f) { dx = 0; dy = 0.5f; l = 0.5f; }
            nx = -dy / l * s * 0.5f; ny = dx / l * s * 0.5f;
            for (c2 = 0; c2 < 4; c2++) { SDL_Vertex *o = &v[n * 4 + c2]; float px2 = (c2 & 2) ? x : x2, py2 = (c2 & 2) ? y : y2, lw = 0.85f * (1 - 0.7f * WxDark());
                o->position.x = px2 + ((c2 & 1) ? nx : -nx); o->position.y = py2 + ((c2 & 1) ? ny : -ny);
                o->color.r = lw * 0.88f; o->color.g = lw * 0.93f; o->color.b = lw; o->color.a = 0.45f * a; o->tex_coord.x = (c2 & 1) ? 1.0f : 0.0f; o->tex_coord.y = (c2 & 2) ? 0.75f : 0.25f; } }
        else
        for (q = 0; q < 4; q++) { SDL_Vertex *o = &v[n * 4 + q]; o->position.x = x + ((q & 1) ? s : -s); o->position.y = y + ((q & 2) ? s * 1.3f : -s * 0.7f);
            if (d->water >= 2) { float l = 1 - 0.6f * WxDark(); o->color.r = d->col[0] * l; o->color.g = d->col[1] * l; o->color.b = d->col[2] * l; o->color.a = d->water == 4 ? 0.30f * a : (d->water == 2 ? 0.95f : 0.85f) * (a < 0.3f ? a / 0.3f : 1); }   /* (dirt / grass: solid till the end) */
            else if (d->water) { float l = 0.85f * (1 - 0.7f * WxDark()); o->color.r = l * 0.88f; o->color.g = l * 0.93f; o->color.b = l; o->color.a = 0.5f * a; } else { o->color.r = 0.62f * (1 - 0.55f * WxDark()); o->color.g = 0.02f; o->color.b = 0.02f; o->color.a = 0.9f * a; } o->tex_coord.x = (q & 1) ? 1.0f : 0.0f; o->tex_coord.y = (q & 2) ? 1.0f : 0.0f; }
        ix[n * 6] = n * 4; ix[n * 6 + 1] = n * 4 + 1; ix[n * 6 + 2] = n * 4 + 2; ix[n * 6 + 3] = n * 4 + 1; ix[n * 6 + 4] = n * 4 + 3; ix[n * 6 + 5] = n * 4 + 2; n++; }
    if (n) { SDL_Rect vr; vr.x = g_gpuClip[0]; vr.y = g_gpuClip[1]; vr.w = g_gpuClip[2] - g_gpuClip[0]; vr.h = g_gpuClip[3] - g_gpuClip[1] + 1; SDL_SetRenderClipRect(g_ren, &vr);
        if (!g_wxGlow) WxGlowMake();
        if (g_wxGlow) { SDL_SetTextureBlendMode(g_wxGlow, SDL_BLENDMODE_BLEND); SDL_SetTextureColorModFloat(g_wxGlow, 1, 1, 1); SDL_SetTextureAlphaModFloat(g_wxGlow, 1); }
        SDL_RenderGeometry(g_ren, g_wxGlow, v, n * 4, ix, n * 6); SDL_SetRenderClipRect(g_ren, NULL); }
    if (g_bsN) W3ScreenBlood(dt); }   /* the splats on the screen, over the droplets */

/* draw the world for frame size w x h into the window rect r (GpuCompose, after the background) */
static void W3Draw(int w, int h, const SDL_FRect *r)
{   R3Frame f; float eye[3], fwd[3], up[3] = { 0, 1, 0 }, view[16], proj[16], sx, sy;
    double vp, p, u, roadY; int tw, th;
    ID3D11Texture2D *t;
    if (!g_w3On || !g_w3HaveCam || !r) return;
    if (getenv("RR_W3LOG")) { static int n; if (++n == 300 && g_primary && g_primary->pal) { int i; double a = 0, b = 0;   /* RR_W3LOG: live vs course-file palette */
        for (i = 0; i < 256; i++) { a += g_primary->pal->e[i].peRed + g_primary->pal->e[i].peGreen + g_primary->pal->e[i].peBlue; b += (g_w3a.pal[i] & 0xff) + ((g_w3a.pal[i] >> 8) & 0xff) + ((g_w3a.pal[i] >> 16) & 0xff); }
        LOG("world3d palettes: live %.1f file %.1f; entry 100 live %d,%d,%d file %d,%d,%d", a / 768, b / 768, g_primary->pal->e[100].peRed, g_primary->pal->e[100].peGreen, g_primary->pal->e[100].peBlue, g_w3a.pal[100] & 0xff, (g_w3a.pal[100] >> 8) & 0xff, (g_w3a.pal[100] >> 16) & 0xff); } }
    {   float rs = g_vid.rscale == 0 ? 0.5f : g_vid.rscale == 1 ? 0.75f : 1.0f;   /* (True 3D detail > 3D resolution: the 3D picture smaller, stretched to the window) */
        tw = (int)(w * g_gpuOutScale * rs + 0.5f); th = (int)(h * g_gpuOutScale * rs + 0.5f); }
    if (tw < 16 || th < 16 || tw > 8192 || th > 8192) return;
    if (!g_w3tex || g_w3tw != tw || g_w3th != th) {
        if (g_w3tex) SDL_DestroyTexture(g_w3tex);
        g_w3tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, tw, th);
        if (!g_w3tex) return;
        SDL_SetTextureBlendMode(g_w3tex, SDL_BLENDMODE_BLEND_PREMULTIPLIED); g_w3tw = tw; g_w3th = th;   /* (what is drawn is opaque, alpha 1; the light halos add colour without alpha) */
    }
    t = (ID3D11Texture2D *)SDL_GetPointerProperty(SDL_GetTextureProperties(g_w3tex), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, NULL);
    if (!t) return;
    vp = (g_w3cam.viewPos - g_w3Piece0) / 256.0;            /* piece position of the view's start */
    if (g_vwHold) vp = ViewerPiece();   /* (the Course Editor's viewer: the live stretch round the piece it looks at, viewer.inc) */
    {   int kp = (int)floor(vp + W3_EYE - 1.0), j2, in = 0; for (j2 = 0; j2 < 2; j2++) { int kk = kp + j2; const RoadPiece *pc; const RoadSection *sc2;   /* (W3InTunnel) */
            if (kk < 0 || kk >= g_w3c.n) continue; pc = &g_w3c.piece[kk]; sc2 = (g_w3camB && pc->layout == 2) ? &pc->halfB : &pc->halfA; if (sc2->kind == SEC_WALL_SPLIT) in++; }
        g_w3InTunnel = in == 2; }
    p = vp + W3_EYE - 1.0;   /* the view builder draws piece floor(vp)'s start at depth 1 - frac (= piece k at depth k - vp + 1), so depth W3_EYE (distance 0) is piece position vp + W3_EYE - 1 */
    u = g_w3cam.camX;   /* (lateral to the carriageway the view follows: W3Fr) */
    {   /* the game's camera across the road (75 % of the rider's, whole units, at once) followed smoothly - two filters in a row, once a game frame: in the 2D picture
           its steps only slid the road, in 3D they moved the near world against the far, the view wobbled through every bend as the rider slid out and was steered back
           (user, 2026-11-19: "when turning is it possible to remove the wobble please") - test switch RR_W3CAMRAW=1: as the game has it.  The player's bike is drawn
           through the same filters (g_w3bikeOff): they are linear, so the bike against the camera is the game's own (the bike moves on the screen as in the game, a
           quarter of its slide, less at the road's edge where the game's camera stops) - only smoothly */
        static double f1, f2, g1, g2, lastVp = -1e9; static unsigned long lastF = (unsigned long)-1; static int raw = -1, gOk; double L = 0; int j, hasL = 0;
        if (raw < 0) raw = getenv("RR_W3CAMRAW") != NULL;
        for (j = 0; j < g_w3ns; j++) if (g_w3s[j].kind == RRO_PLAYER) { L = g_w3s[j].lat; hasL = 1; break; }
        g_w3bikeOff = 0;
        if (!raw && !g_vwHold) {
            if (fabs(vp - lastVp) > 4 || fabs(u - f2) > 1500 || (hasL && fabs(L - g2) > 1500)) { f1 = f2 = u; g1 = g2 = L; }   /* (a new race, a reset, the camera on another road: from there) */
            else if (g_frame != lastF) { f1 += (u - f1) * 0.45; f2 += (f1 - f2) * 0.45;
                if (hasL && !gOk) g1 = g2 = L; else if (hasL) { g1 += (L - g1) * 0.45; g2 += (g1 - g2) * 0.45; } }   /* (the bike not drawn for a while: from where it is) */
            lastF = g_frame; lastVp = vp; u = f2; gOk = hasL;
            if (hasL) g_w3bikeOff = g2 - L; } }
    { float a[3]; W3SmoothPt(W3Fr(), vp, 0, 0, a, NULL); roadY = a[1]; }
    W3SmoothPt(W3Fr(), p, u, 0, eye, NULL); eye[1] = (float)(roadY + g_w3cam.camY);
    if (g_w3bumpS > 0) { double t = (double)((unsigned long)g_frame - g_w3bumpF); if (t < 14) eye[1] += (float)(34 * g_w3bumpS * sin(t / 14 * 6.2831853 * 1.5) * exp(-t / 6)); else g_w3bumpS = 0; }   /* (a speed bump: RR_Bump) */   /* (on the smooth curve: no kink at the pieces' ends) */
    {   int kk = (int)floor(vp); double fr = vp - kk, th0, th1, hd; const C3Frame *F = W3Fr();   /* looking along the road's heading at the view's start, level */
        if (kk < 0) kk = 0; if (kk >= g_w3c.n) kk = g_w3c.n - 1;
        th0 = F[kk].heading; th1 = F[kk + 1].heading;
        {   double hm = kk > 0 ? F[kk - 1].heading : th0, hp = kk + 2 <= g_w3c.n ? F[kk + 2].heading : th1, m0, m1, f2 = fr * fr, f3 = f2 * fr;   /* (a smooth turn through the pieces' headings
                                                                       (Catmull-Rom) - straight from one to the next, the turn's rate stepped at every piece: the player's bike, standing on the
                                                                       road ahead, slid against it - user, 2026-11-19 "the player bike ... wobbles around abit, or looks like its stepping") */
            while (th1 - th0 > 3.14159265358979) th1 -= 6.28318530717959; while (th1 - th0 < -3.14159265358979) th1 += 6.28318530717959;
            while (hm - th0 > 3.14159265358979) hm -= 6.28318530717959; while (hm - th0 < -3.14159265358979) hm += 6.28318530717959;
            while (hp - th1 > 3.14159265358979) hp -= 6.28318530717959; while (hp - th1 < -3.14159265358979) hp += 6.28318530717959;
            m0 = (th1 - hm) * 0.5; m1 = (hp - th0) * 0.5;
            hd = (2 * f3 - 3 * f2 + 1) * th0 + (f3 - 2 * f2 + fr) * m0 + (-2 * f3 + 3 * f2) * th1 + (f3 - f2) * m1; }
        fwd[0] = (float)sin(hd); fwd[1] = 0; fwd[2] = (float)cos(hd); WxView(hd);   /* (the sun's bearing from it: weather.inc) */
        g_w3view[0] = eye[0]; g_w3view[1] = eye[2]; g_w3view[2] = hd; g_w3view[3] = u; g_w3view[4] = p;   /* (the player's bike straight ahead of it: W3MapObj) */
        if (getenv("RR_W3LOGCAM")) LOG("w3cam vp %.4f camX %d camY %d hd %.5f eye %.1f %.1f %.1f u %.2f half %d B %d", vp, g_w3cam.camX, g_w3cam.camY, hd, eye[0], eye[1], eye[2], u, g_w3cam.half, g_w3camB); }   /* (test switch RR_W3LOGCAM=1: the camera each frame) */
    R3_LookTo(view, eye, fwd, up);
    sx = (float)tw / w; sy = (float)th / h;
    {   double fpx = W3_F * (g_w3cam.pxPerUnit > 0 ? g_w3cam.pxPerUnit : 1);   /* focal length in frame pixels */
        g_w3fpxT = (float)(fpx * sx);   /* (in the 3D picture's pixels: W3Grass) */
        R3_Persp(proj, (float)(fpx * sx), (float)(fpx * sy), g_w3cam.centreX * sx, g_w3cam.horizonY * sy, tw, th, (float)W3_ZN, (float)W3Far()); }
    if (g_vwHold) { ViewerCamera(eye, fwd, proj, tw, th); R3_LookTo(view, eye, fwd, up); g_w3fpxT = tw * 0.5f / 0.62f; }   /* the editor's orbit camera (viewer.inc) */
    memset(&f, 0, sizeof f);
    R3_Mul(f.vp, view, proj); W3ViewPlanes(f.vp);
    f.right[0] = fwd[2]; f.right[1] = 0; f.right[2] = -fwd[0];
    if (g_vwHold) { float l = sqrtf(f.right[0] * f.right[0] + f.right[2] * f.right[2]); if (l > 0) { f.right[0] /= l; f.right[2] /= l; }   /* (the viewer's camera looks down: right kept unit length) */
        memcpy(g_vwVp, f.vp, sizeof g_vwVp); g_vwRect = *r; g_vwHaveVp = 1; }
    f.eye[0] = eye[0]; f.eye[1] = eye[1]; f.eye[2] = eye[2]; f.bias = 400.0f;   /* billboards drawn over the ground just in front of them */
    if (getenv("RR_W3LOGY")) { static int ny; if ((++ny % atoi(getenv("RR_W3LOGY"))) == 0) {   /* test switch RR_W3LOGY=<k>: every k frames, the game's road centre x (its projection, lateral 0) against the 3D one at depths 3..24 -> yaw (rad); on half B the game's lateral 0 is A's centre (huge values) */
        static const double Ds[4] = { 3, 6, 12, 24 }; char t[300]; int j; double fpx = W3_F * (g_w3cam.pxPerUnit > 0 ? g_w3cam.pxPerUnit : 1) * sx; t[0] = 0;
        for (j = 0; j < 4; j++) { double gx, ry, sc; float pt[3]; double q = vp + Ds[j] - 1, cx, cw;
            if (!W3OrigAt(Ds[j], &gx, &ry, &sc)) continue;
            C3_PointF(&g_w3c, W3Fr(), q, 0, 0, pt);
            cx = pt[0] * f.vp[0] + pt[1] * f.vp[4] + pt[2] * f.vp[8] + f.vp[12]; cw = pt[0] * f.vp[3] + pt[1] * f.vp[7] + pt[2] * f.vp[11] + f.vp[15];
            if (cw <= 1) continue;
            cx = (cx / cw + 1) * 0.5 * tw;
            _snprintf(t + strlen(t), sizeof t - strlen(t) - 1, "  D%.0f game %.0f 3d %.0f yaw %.3f", Ds[j], gx * sx, cx, atan((gx * sx - cx) / fpx)); }
        LOG("w3yaw %d piece %.2f half %d camX %d:%s", ny, vp, g_w3cam.half, g_w3cam.camX, t); } }
    for (g_w3nrow = 0; g_w3nrow < W3_ROWS; g_w3nrow++) {   /* the road ahead on screen (W3Sprites) */
        double q = p + g_w3nrow / 8.0, cw, cy; float pt[3];
        C3_PointF(&g_w3c, W3Fr(), q, g_w3camB ? W3MidB(q) : C3_RoadMid(&g_w3c, q), 0, pt);
        cw = pt[0] * f.vp[3] + pt[1] * f.vp[7] + pt[2] * f.vp[11] + f.vp[15];
        cy = pt[0] * f.vp[1] + pt[1] * f.vp[5] + pt[2] * f.vp[9] + f.vp[13];
        g_w3rowD[g_w3nrow] = (float)cw;   /* clip w = distance along the view direction */
        g_w3rowY[g_w3nrow] = cw > 1 ? (float)((1.0 - cy / cw) * 0.5 * h) : 1e9f;
    }
    if (g_w3ns) W3Place(w, h, f.vp, vp);   /* where the game's sprites are drawn (the lights below start at them; W3Sprites draws them) */
    /* the game's fades (race start / end) darken its palette: the 3D world follows (its textures are made with the course file's full palette) */
    f.bright = 1.0f; f.useBright = 1;
    if (g_primary && g_primary->pal) { int i; double a = 0, b = 0;
        for (i = 0; i < 256; i++) { a += g_primary->pal->e[i].peRed + g_primary->pal->e[i].peGreen + g_primary->pal->e[i].peBlue; b += (g_w3a.pal[i] & 0xff) + ((g_w3a.pal[i] >> 8) & 0xff) + ((g_w3a.pal[i] >> 16) & 0xff); }
        if (b > 0) { f.bright = (float)(a / b); if (f.bright > 1.0f) f.bright = 1.0f; if (f.bright < 0.0f) f.bright = 0.0f; } }
    if (g_cdPal || g_vwHold) f.bright = 1.0f;   /* (the start countdown, the editor's viewer: undimmed) */
    f.target = t; f.w = tw; f.h = th; f.doClear = 1;   /* transparent: the background (sky) shows through */
    f.crisp = getenv("RR_W3SOFT") == NULL;   /* the ground's grain as the game's (unfiltered near texels); test switch RR_W3SOFT=1: filtered */
    { PF_T0(pfL); W3Live(); PF_ADD(PF_LIVE, pfL); }
    if (g_w3lo >= 0) { f.staticBatches = g_w3sb; f.nStatic = g_w3snb; f.liveBatches = g_w3lb; f.nLive = g_w3lnb; }
    else if (g_w3compiled) { W3StaticCut(); f.staticBatches = g_w3sb; f.nStatic = g_w3snb; }   /* (the compiled race: the stretch in view) */
    else { f.staticBatches = g_w3b; f.nStatic = g_w3nb; }
    WxWorld(&f);   /* time of day / weather: tint, fog, snow (weather.inc) */
    f.sun[3] = WxSunDir(fwd, f.right, f.sun);   /* the sun on the buildings' walls (render3d.h) */
    f.shadow = 0;   /* (the buildings' shadows - the sun's shadow map, W3SunShadowMap - are off: removed for now, user 2026-11-15) */
    if (f.puddles > 0 && g_vid.refl && !getenv("RR_W3NOREFL")) {   /* rain: the puddles mirror the world, in the plane of the road a little ahead (test switch RR_W3NOREFL=1: the sky only) */
        float a[3]; C3_PointF(&g_w3c, W3Fr(), p + 2.0, g_w3camB ? W3MidB(p + 2.0) : C3_RoadMid(&g_w3c, p + 2.0), 0, a); f.reflect = 1; f.reflectY = a[1]; }
    {   /* per-frame geometry of the world pass: traffic as 3D cars, then the light halos (their point lights into f) */
        static R3Vert *cv; static int ccap; static R3Batch cb[66 + 256]; int nc = 0, i, nb = 0, nv = 0, need, nh;
        if (W3CarsOn()) for (i = 0; i < g_w3ns; i++) if (g_w3s[i].kind == RRO_CAR) nc++;
        need = nc * 2400 + W3_TRAMVX + 18 * 400 + 512 * 6 * W3_SHL * W3_SHW + W3_BLDGSH * 36 + W3_GRMAX * 6 + 6 * (16 + 210 * 12);   /* (cars, halos, the casters' and the buildings' shadows, grass, broken hydrants) */
        W3HydCheck(p);   /* (a rider on a fire hydrant breaks it) */
        if (ccap < need) { ccap = need; cv = (R3Vert *)realloc(cv, sizeof(R3Vert) * ccap); if (!cv) ccap = 0; }
        if (cv) {
            PF_T0(pfX);   /* (RR_PROFILE: extras - grass included) */
            if (nc && (nb = W3Cars(cv, ccap - 18 * 400 - W3_GRMAX * 6 - 6 * (16 + 210 * 12) - W3_TRAMVX, cb, 64, vp)) > 0) nv = cb[nb - 1].first + cb[nb - 1].count;
            {   int nt = W3Trams(cv + nv, W3_TRAMVX, vp); if (nt > 0) { cb[nb].first = nv; cb[nb].count = nt; cb[nb].tex = W3TramTex(); cb[nb].alpha = 0; nb++; nv += nt; } }   /* (trams) */
            if (g_w3ngr && W3GrassTex() >= 0) { PF_T0(pfG); LARGE_INTEGER g0, g1, gf; int ng; QueryPerformanceCounter(&g0); ng = W3Grass(cv + nv, W3_GRMAX * 6, eye); QueryPerformanceCounter(&g1); QueryPerformanceFrequency(&gf);
                g_grLast = ng / 6; g_grLastMs = (float)((g1.QuadPart - g0.QuadPart) * 1000.0 / gf.QuadPart); PF_ADD(PF_GRASS, pfG); if (ng > 0) { cb[nb].first = nv; cb[nb].count = ng; cb[nb].tex = W3GrassTex(); cb[nb].alpha = 0; nb++; nv += ng; } }   /* grass tufts on the green ground */
            nv += W3HydDraw(cv, nv, nv + 6 * (16 + 210 * 12), cb, &nb, 64 + 180, eye);   /* broken hydrants: the stumps, the water */
            nv += W3CasterShadows(&f, cv, nv, ccap - 18 * 400, p, cb, &nb, 64 + 200);   /* trees, lamps, poles: shadows on the ground (before the halos) */

            if ((nh = W3Lights(&f, cv + nv, ccap - nv, p, eye, w, h)) > 0 && g_vid.halos && W3GlowTex() >= 0) { cb[nb].first = nv; cb[nb].count = nh; cb[nb].tex = W3GlowTex(); cb[nb].alpha = 3; nb++; nv += nh; }
            PF_ADD(PF_EXTRAS, pfX);
            if (nb) { f.dyn = cv; f.nDyn = nv; f.dynBatches = cb; f.nDynBatches = nb; } }
    }
    SDL_FlushRenderer(g_ren);
    memcpy(g_w3eye, eye, sizeof g_w3eye); memcpy(g_w3fw, fwd, sizeof g_w3fw); g_w3rt[0] = f.right[0]; g_w3rt[1] = 0; g_w3rt[2] = f.right[2];
    if (getenv("RR_W3HITTEST")) { static int fc; int j; if (++fc % 60 == 0) for (j = 0; j < g_w3ns; j++) if (g_w3s[j].kind == RRO_PLAYER) { RR_HitFx(g_w3s[j].id); break; } }   /* test switch RR_W3HITTEST=1: the player hit every 60 frames */
    if (g_blHitN) W3BloodSpawn(vp);   /* blood from riders hit this frame */
    W3Splash(&f, vp, w);   /* rain: water from the tyres in puddles; dirt / grass off the tarmac */
    if (f.snow > 0) { TkUpdate(vp, eye, fwd); f.tracks = g_tkSrv; f.trackScale = 1.0f / (TK_N * TK_CELL); f.trackAmt = 0.85f; }   /* snow tracks (above) */
    else g_tkInit = 0;
    { PF_T0(pfR); R3_Render(&g_w3r, &f); PF_ADD(PF_RENDER, pfR); }
    { static int ns = -1; if (ns < 0) ns = getenv("RR_W3NOSHADOW") != NULL; g_w3NoShadow = ns; }
    if (!g_vwHold) { PF_T0(pfS); W3Sprites(t, w, h, tw, th, f.vp, vp, &f); PF_ADD(PF_SPRITES, pfS); } g_w3ns = 0;   /* the game's remaining polygons, depth-tested against the world */
    SDL_RenderTexture(g_ren, g_w3tex, NULL, r); }
