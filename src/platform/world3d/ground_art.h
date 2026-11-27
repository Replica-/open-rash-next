/* ground_art.h - extra ground pictures for painted cells (user, 2026-11-18: "another ground texture please, called REDWOOD ... massive redwood
 * trees spawning up ... really higher and spaced out on the tile ... the texture abit dirt / bark mix ... 2d redwood sprites"; made with Fooocus:
 * analysis\redwood\redwood.py).  <mod>\hd\ground\<NAME>.png (512 x 512, tiling) and, if there, <NAME>_TREE.png (256 x 1024 RGBA, the foot at the
 * bottom middle, alpha < 0.5 cut out): the trees course3d.h C3_GroundTrees stands on every cell painted with it; <NAME>_HEDGE.png (512 x 256 RGBA,
 * tiling across, its foot the bottom row: analysis\redwood\hedge.py): a tall hedge along the inner edge of a "beyond" cell painted with it (C3_GroundHedge).  Painted as RRC_BAND | RRC_GEN | n
 * (n = its place in k_c3Ground; course3d.h C3_PaintTex).  Used by the game (world3d.inc) and the Course Editor; needs SDL3 (SDL_LoadPNG). */
#ifndef C3_GROUND_ART_H
#define C3_GROUND_ART_H
#include "rail_art.h"
#define C3_NGROUND 30
static const char *const k_c3Ground[C3_NGROUND] = { "REDWOOD", "WATER", "HOLE", "BRIDGE", "DUNES", "CLIFF", "SANDW", "SANDG", "SANDR", "SANDPATH", "SLABS", "TILES", "SHRUB", "FOREST", "DHOLE", "SEACLIFF", "SIDEST", "TLIGHTS", "STOPSIGN", "SHRUBG", "CONCRETE", "DUFF", "FENCE", "TRAIL", "BOARDWALK", "GRAVEL", "WOODCHIP", "LOGYARD", "SNOW", "SCREE" };   /* (SNOW, SCREE: the high Sierra - user, 2026-11-27 "more snow like terrain the further we go up the mountain"; SNOW.png / SCREE.png, Fooocus) */   /* (only ever added to: a painted cell keeps its number - RRC_GEN | place) */
static const unsigned k_c3GroundCol[C3_NGROUND] = { 0, 0, 0, 0, 0xd6c4a0u, 0x6e645au, 0xe1d7c3u, 0xcdaa69u, 0xb96944u, 0xd4b68au, 0, 0, 0x3c5a2eu, 0x2a4428u, 0, 0, 0, 0, 0, 0x26402au, 0, 0, 0, 0, 0, 0, 0, 0, 0xe8eef4u, 0x86837eu };   /* (no picture: a mottled ground of this colour, RGB) */   /* (HOLE: course3d.h C3_GHOLE - sunk in the game, C3_HoleRow) */   /* (WATER: course3d.h C3_GWATER - the cells painted with it are water, C3_WaterRow) */
/* collections (user, 2026-11-23: "put a tag on them that makes them a collection so we can identify them later, even give them all a little symbol"): the
   blocks made to be used together - C3_GroundColl(n) for a ground, the Course Editor's BlockColl for its own blocks; each shows its collection's badge (editor ThumbColl) */
enum { C3_COLL_NONE = 0, C3_COLL_JUNCTION = 1, C3_NCOLL };
static const char *const k_c3CollName[C3_NCOLL] = { "", "Junction" };
static int C3_GroundColl(int n) { return n >= 0 && n < C3_NGROUND && (!strcmp(k_c3Ground[n], "SIDEST") || !strcmp(k_c3Ground[n], "TLIGHTS") || !strcmp(k_c3Ground[n], "STOPSIGN")) ? C3_COLL_JUNCTION : C3_COLL_NONE; }
static int C3_GroundSharp(int n) { return C3_GroundColl(n) || (n >= 0 && n < C3_NGROUND && (!strcmp(k_c3Ground[n], "CONCRETE") || !strcmp(k_c3Ground[n], "FENCE") || !strcmp(k_c3Ground[n], "BOARDWALK"))); }   /* (laid / built: its edges never torn into the ground beside - course3d.h C3_IsJunctionTex) */
static int C3_GroundVergeOnly(int n) { return n >= 0 && n < C3_NGROUND && (!strcmp(k_c3Ground[n], "TLIGHTS") || !strcmp(k_c3Ground[n], "STOPSIGN")); }   /* (a post at the kerb: the verge only) */
static int C3_GroundIsPath(int n) { return n >= 0 && n < C3_NGROUND && (!strcmp(k_c3Ground[n], "SANDPATH") || !strcmp(k_c3Ground[n], "SLABS") || !strcmp(k_c3Ground[n], "TILES")
    || !strcmp(k_c3Ground[n], "TRAIL") || !strcmp(k_c3Ground[n], "BOARDWALK") || !strcmp(k_c3Ground[n], "GRAVEL") || !strcmp(k_c3Ground[n], "WOODCHIP")); }   /* (the forest footpaths - user, 2026-11-26 "footpaths that are suitable for ... sierra nevada red wood / forest") */   /* (a footpath's picture: the Course Editor's Footpaths list - SANDPATH the sand footpath, DUNES its ground: user, 2026-11-19 "two version of sand") */
static const char *C3_GroundLabel(int n) { return n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "SIDEST") ? "SIDE STREET" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "TLIGHTS") ? "TRAFFIC LIGHTS" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "STOPSIGN") ? "STOP SIGN" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "SHRUBG") ? "GREEN SHRUB" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "DUFF") ? "REDWOOD DUFF" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "FENCE") ? "RAIL FENCE" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "HOLE") ? "CLIFF HOLE" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "DHOLE") ? "DIRT HOLE" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "SEACLIFF") ? "SEA CLIFF" : n >= 0 && n < C3_NGROUND && !strcmp(k_c3Ground[n], "LOGYARD") ? "LOG YARD" : n >= 0 && n < C3_NGROUND ? k_c3Ground[n] : "?"; }   /* (the Course Editor's name for it - user,
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
static int g_c3GroundSymbols;   /* 1 = the Course Editor: TRAFFIC LIGHTS / STOP SIGN pictures carry their symbol (its flat view); the game: plain concrete under the post */
static unsigned *C3G_Junction(int S, int kind)   /* (the Junction collection, no PNG: 0 SIDEST a side street's asphalt; 1 TLIGHTS / 2 STOPSIGN the kerb's concrete with the
                                                    post's symbol on it - the editor's flat P3D view shows where they stand; the game stands the post there: course3d.h C3_JunctionPost) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { unsigned h = ((unsigned)x * 374761393u + (unsigned)y * 668265263u); int g, r, b; double u = (x + 0.5) / S - 0.5, v = (y + 0.5) / S - 0.5;
        h = (h ^ (h >> 13)) * 1274126177u;
        if (kind == 0) { g = 72 + (int)((h >> 26) % 12); r = g; b = g + 3; }   /* asphalt, as the road's own */
        else { g = 150 + (int)((h >> 24) % 20) - 10 - ((x % (S / 2)) < 2 || (y % (S / 2)) < 2 ? 35 : 0); r = g + 2; b = g - 2;   /* concrete slabs */
            if (kind == 1 && fabs(u) < 0.13 && fabs(v) < 0.34) { r = g = b = 30;   /* the traffic light's head: red, amber, green */
                if (u * u + (v + 0.21) * (v + 0.21) < 0.0081) { r = 230; g = 40; b = 30; } else if (u * u + v * v < 0.0081) { r = 240; g = 170; b = 20; } else if (u * u + (v - 0.21) * (v - 0.21) < 0.0081) { r = 40; g = 200; b = 70; } }
            if (kind == 2) { double a = fabs(u) > fabs(v) ? fabs(u) : fabs(v), d = fabs(u) + fabs(v), oc = a > d * 0.7071 ? a : d * 0.7071;   /* an octagon: red, a white rim */
                if (oc < 0.40) { if (oc > 0.34) { r = g = b = 235; } else { r = 200; g = 28; b = 30; } } } }
        px[y * S + x] = C3G_Px(r, g, b); }
    return px; }
static unsigned C3G_H(unsigned x, unsigned y, unsigned z) { unsigned h = x * 374761393u + y * 668265263u + z * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
static unsigned *C3G_Concrete(int S)   /* (no CONCRETE.png: poured pavement - pale grey slabs, a sawn joint across every half, a broomed grain across, soft stains; user, 2026-11-24 photo) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { unsigned h = C3G_H((unsigned)x, (unsigned)y, 7); int g = 182, slab = (y / (S / 2)) * 3 + (x / (S / 2)), jy = y % (S / 2), jx = x % (S / 2);
        g += (int)(h % 9) - 4;                                                    /* grain */
        g += (int)((C3G_H((unsigned)(x / 2), (unsigned)y, 3) % 5)) - 2;            /* broomed across (x) */
        g += (int)(C3G_H((unsigned)slab, 1, 9) % 12) - 6;                          /* each slab its own shade */
        g -= (int)(10 * (0.5 + 0.5 * sin(x * 0.045 + y * 0.03 + slab)) * (0.5 + 0.5 * sin(y * 0.07)));   /* soft stains */
        if (jy < 2 || jx < 1) g -= 58 - (jy == 1 || jx == 0 ? 20 : 0);              /* the joints: dark, a lighter edge */
        else if (jy < 4 || jx < 3) g += 8;                                         /* (the slab's tooled edge, a little brighter) */
        px[y * S + x] = C3G_Px(g + 2, g + 1, g - 3); }
    return px; }
static unsigned *C3G_Duff(int S, int fence)   /* (no DUFF.png: a redwood forest floor - rust-brown needle litter, needles in every direction, dark gaps, a few twigs; fence: the editor's
                                                 symbol on it - a split-rail fence) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y, n;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { unsigned h = C3G_H((unsigned)x, (unsigned)y, 11); int v = (int)(h % 30);
        px[y * S + x] = C3G_Px(84 + v, 52 + v / 2, 36 + v / 3); }
    for (n = 0; n < S * S / 10; n++) {   /* needles: short strokes, rust to tan, wrapping */
        unsigned h = C3G_H((unsigned)n, 5, 13); double a = (h % 628) / 100.0, len = 4 + (h >> 10) % 8; int x0 = (int)((h >> 3) % (unsigned)S), y0 = (int)((h >> 17) % (unsigned)S), t, tone = (int)((h >> 23) % 60);
        for (t = 0; t < (int)len; t++) { int xx = ((x0 + (int)(t * cos(a))) % S + S) % S, yy = ((y0 + (int)(t * sin(a))) % S + S) % S;
            px[yy * S + xx] = C3G_Px(128 + tone * 7 / 10, 80 + tone / 3, 52 + tone / 5); } }
    for (n = 0; n < 6; n++) {   /* twigs */
        unsigned h = C3G_H((unsigned)n, 9, 17); double a = (h % 628) / 100.0; int x0 = (int)(h % (unsigned)S), y0 = (int)((h >> 12) % (unsigned)S), t;
        for (t = 0; t < S / 6; t++) { int xx = ((x0 + (int)(t * cos(a))) % S + S) % S, yy = ((y0 + (int)(t * sin(a))) % S + S) % S; px[yy * S + xx] = C3G_Px(70, 46, 30); } }
    if (fence) for (y = 0; y < S; y++) for (x = 0; x < S; x++) { int r1 = abs(y - S / 3) < 2, r2 = abs(y - 2 * S / 3) < 2, p = (x % (S / 2)) < 4 && y > S / 5 && y < 4 * S / 5;
        if (r1 || r2 || p) px[y * S + x] = C3G_Px(120, 108, 92); }
    return px; }
/* the forest footpaths (user, 2026-11-26: "footpaths that are suitable for that" - a Sierra Nevada redwood forest; no PNG: drawn here, x across the path, y along it):
   0 TRAIL a packed-earth trail - worn paler down the middle, needle litter thick at its sides, pebbles, a root across now and then; 1 BOARDWALK weathered redwood planks
   laid across, dark gaps, two nail heads at each end of a plank; 2 GRAVEL decomposed granite - tan and grey grit, a few bigger stones; 3 WOODCHIP bark chips, rust to tan */
static unsigned *C3G_Path(int S, int kind)
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y, n;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { unsigned h = C3G_H((unsigned)x, (unsigned)y, 31u + (unsigned)kind); int v = (int)(h % 21) - 10, r, g, b;
        if (kind == 0) { double m = fabs(x - S * 0.5) / (S * 0.5), wear = 1 - m * m; int lo = (int)(C3G_H((unsigned)(x / 6), (unsigned)(y / 6), 5) % 14);
            r = 112 + (int)(34 * wear) + v + lo; g = 86 + (int)(26 * wear) + v + lo; b = 60 + (int)(16 * wear) + v / 2 + lo / 2; }
        else if (kind == 1) { int pl = y / (S / 8), py = y % (S / 8), tone = (int)(C3G_H((unsigned)pl, 3, 41) % 34) - 17, grain = (int)(C3G_H((unsigned)(x / 9 + pl * 7), (unsigned)y, 43) % 13) - 6;
            r = 138 + tone + grain + v / 3; g = 104 + tone * 3 / 4 + grain + v / 3; b = 82 + tone / 2 + grain / 2 + v / 4;
            if (py < 2) { r = r * 30 / 100; g = g * 30 / 100; b = b * 30 / 100; }                            /* the gap between two planks */
            else if (py == 2 || py == S / 8 - 1) { r = r * 80 / 100; g = g * 80 / 100; b = b * 80 / 100; }   /* (its rounded edges) */
            if ((abs(x - S / 12) <= 1 || abs(x - S + S / 12) <= 1) && (py == S / 32 + 3 || py == S / 8 - S / 32 - 2)) { r = 62; g = 60; b = 58; } }   /* (nail heads) */
        else if (kind == 2) { int gr = (int)(C3G_H((unsigned)(x / 2), (unsigned)(y / 2), 47) % 60), grey = (h >> 9) % 3 == 0;
            r = grey ? 118 + gr / 2 : 146 + gr / 2; g = grey ? 118 + gr / 2 : 144 + gr / 2; b = grey ? 120 + gr / 2 : 140 + gr / 2; r += v; g += v; b += v; }   /* (grey granite grit, not sand: user, 2026-11-26) */
        else { r = 70 + v; g = 42 + v / 2; b = 28 + v / 3; }
        px[y * S + x] = C3G_Px(r, g, b); }
    if (kind == 0) {   /* needles along the trail's sides, pebbles, roots across */
        for (n = 0; n < S * S / 14; n++) { unsigned h = C3G_H((unsigned)n, 7, 53); double a = (h % 628) / 100.0; int side = (h >> 9) & 1, x0 = side ? S - 1 - (int)(((h >> 10) % (unsigned)(S / 4)) * ((h >> 20) % 100) / 100) : (int)(((h >> 10) % (unsigned)(S / 4)) * ((h >> 20) % 100) / 100), y0 = (int)((h >> 15) % (unsigned)S), t, tone = (int)((h >> 23) % 50);
            for (t = 0; t < 6; t++) { int xx = x0 + (int)(t * cos(a)), yy = ((y0 + (int)(t * sin(a))) % S + S) % S; if (xx >= 0 && xx < S) px[yy * S + xx] = C3G_Px(122 + tone, 74 + tone / 3, 48 + tone / 5); } }
        for (n = 0; n < 50; n++) { unsigned h = C3G_H((unsigned)n, 11, 57); int cx = (int)(h % (unsigned)S), cy = (int)((h >> 8) % (unsigned)S), r = 1 + (int)((h >> 16) % 3), g = 120 + (int)((h >> 20) % 60), dx, dy;
            for (dy = -r; dy <= r; dy++) for (dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r) { int xx = (cx + dx + S) % S, yy = (cy + dy + S) % S, l = g + (dy < 0 ? 18 : -10); px[yy * S + xx] = C3G_Px(l, l - 4, l - 12); } }
        {   unsigned h = C3G_H(1, 13, 59); int y0 = (int)(h % (unsigned)S), t;   /* a root: a faint winding band from one side, gone by the middle */
            for (t = 0; t < S * 2 / 5; t++) { int yy = ((y0 + (int)(4 * sin(t * 0.11) + 2 * sin(t * 0.37))) % S + S) % S, d; unsigned o = px[yy * S + t];
                for (d = 0; d <= (t < S / 5); d++) { int y2 = (yy + d) % S, j; unsigned q = 0xff000000u; o = px[y2 * S + t];
                    for (j = 0; j < 3; j++) q |= (unsigned)(((o >> (8 * j)) & 255) * 78 / 100) << (8 * j);
                    px[y2 * S + t] = q; } } } }
    if (kind == 2) for (n = 0; n < 70; n++) { unsigned h = C3G_H((unsigned)n, 17, 61); int cx = (int)(h % (unsigned)S), cy = (int)((h >> 8) % (unsigned)S), r = 1 + (int)((h >> 16) % 2), g = 110 + (int)((h >> 20) % 90), dx, dy;
        for (dy = -r; dy <= r; dy++) for (dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r) { int l = g + (dy < 0 ? 22 : -16); px[((cy + dy + S) % S) * S + (cx + dx + S) % S] = C3G_Px(l, l - 3, l - 8); } }
    if (kind == 3) for (n = 0; n < S * S / 9; n++) {   /* chips: little slivers lying every way, each its own tone */
        unsigned h = C3G_H((unsigned)n, 19, 67); double a = (h % 314) / 100.0; int x0 = (int)((h >> 3) % (unsigned)S), y0 = (int)((h >> 13) % (unsigned)S), len = 3 + (int)((h >> 22) % 6), t, w, tone = (int)((h >> 25) % 70);
        for (t = 0; t < len; t++) for (w = 0; w < 2; w++) { int xx = ((x0 + (int)(t * cos(a)) + w) % S + S) % S, yy = ((y0 + (int)(t * sin(a))) % S + S) % S;
            px[yy * S + xx] = C3G_Px(116 + tone, 66 + tone * 2 / 3 - (w ? 12 : 0), 40 + tone / 3 - (w ? 10 : 0)); } }
    return px; }
static double C3G_Noise(int x, int y, int S, unsigned seed)   /* (soft value noise 0..1, wrapping on S: two octaves) */
{   double v = 0; int o; for (o = 0; o < 2; o++) { int P = o ? 16 : 6, cs = S / P, xi = x / cs, yi = y / cs; double fx = (double)(x % cs) / cs, fy = (double)(y % cs) / cs, a[4]; int j;
        for (j = 0; j < 4; j++) a[j] = (C3G_H((unsigned)((xi + (j & 1)) % P), (unsigned)((yi + (j >> 1)) % P), seed + (unsigned)o) % 1000) / 1000.0;
        fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); v += (o ? 0.35 : 0.65) * ((a[0] + (a[1] - a[0]) * fx) * (1 - fy) + (a[2] + (a[3] - a[2]) * fx) * fy); }
    return v; }
/* GRAVEL (user note 2026-11-27: "the gravel should be abit more bump mapped and less pixelated please. it should fit in more with the road"): S x S (256 - a texel a unit, course3d.h
   maps it 256 units a tile) - a soft grey grit of road dust (two octaves of noise, a fine grain), on it stones of 1 .. 4 texels each its own grey (now and then warm or dark),
   shaded as domes lit from the upper left with a shadow at the lower right; its mean near the asphalt's.  Its light / dark is the height the world shader bumps it with
   (vertex alpha 239: render3d.h, the dirt) */
static unsigned *C3G_Gravel(int S)
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y, n, N = S * S / 24;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { double m = C3G_Noise(x, y, S, 401u), f = C3G_Noise(x * 4 % S, y * 4 % S, S, 409u); int v = (int)(C3G_H((unsigned)x, (unsigned)y, 413u) % 13) - 6, l = 82 + (int)(18 * m + 8 * f) + v;
        px[y * S + x] = C3G_Px(l + 2, l + 1, l - 1); }
    for (n = 0; n < N; n++) { unsigned h = C3G_H((unsigned)n, 23u, 419u), h2 = C3G_H((unsigned)n, 29u, 421u); double cx = (h % (unsigned)(S * 16)) / 16.0, cy = ((h >> 12) % (unsigned)(S * 16)) / 16.0;
        double rx = 1.0 + (h2 % 100) / 100.0 * ((h2 >> 7) % 5 == 0 ? 3.0 : 1.6), ry = rx * (0.65 + ((h2 >> 10) % 35) / 100.0), a = ((h2 >> 16) % 314) / 100.0, ca = cos(a), sa = sin(a);
        int tone = (int)((h2 >> 20) % 100), r0, g0, b0, dx, dy, R = (int)(rx + 2);
        if (tone < 8) { r0 = 70; g0 = 68; b0 = 66; } else if (tone < 18) { r0 = 128; g0 = 116; b0 = 102; } else if (tone < 24) { r0 = 118; g0 = 98; b0 = 84; } else { int g = 92 + (int)((h >> 24) % 40); r0 = g; g0 = g; b0 = g + 3; }
        for (dy = -R; dy <= R + 1; dy++) for (dx = -R; dx <= R + 1; dx++) { double ox = (int)cx + dx + 0.5 - cx, oy = (int)cy + dy + 0.5 - cy, u = (ox * ca + oy * sa) / rx, w = (-ox * sa + oy * ca) / ry, d = u * u + w * w;
            int xx = (((int)cx + dx) % S + S) % S, yy = (((int)cy + dy) % S + S) % S; unsigned o = px[yy * S + xx];
            if (d < 1) { double nz = sqrt(1 - d), nx = u * ca - w * sa, ny = u * sa + w * ca, lt = (-0.55 * nx - 0.60 * ny + 0.58 * nz) / 1.0; double k = 0.72 + 0.40 * (lt < 0 ? 0 : lt), e = d > 0.75 ? (d - 0.75) * 4 : 0;
                int r = (int)(r0 * k), g = (int)(g0 * k), b = (int)(b0 * k); r = (int)(r + ((int)(o & 255) - r) * e * 0.5); g = (int)(g + ((int)(o >> 8 & 255) - g) * e * 0.5); b = (int)(b + ((int)(o >> 16 & 255) - b) * e * 0.5);
                px[yy * S + xx] = C3G_Px(r, g, b); }
            else { double sx = ox - 1.1, sy = oy - 1.1, su = (sx * ca + sy * sa) / rx, sw = (-sx * sa + sy * ca) / ry;   /* (its shadow, down right) */
                if (su * su + sw * sw < 1) px[yy * S + xx] = C3G_Px((int)(o & 255) * 80 / 100, (int)(o >> 8 & 255) * 80 / 100, (int)(o >> 16 & 255) * 80 / 100); } } }
    return px; }
static unsigned *C3G_LogYard(int S)   /* (no LOGYARD.png: a logging camp's yard - churned dark earth, two tyre ruts along it, sawdust in pale drifts, bark chips; y along the road) */
{   unsigned *px = (unsigned *)malloc((size_t)S * S * 4); int x, y, n;
    if (!px) return NULL;
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { unsigned h = C3G_H((unsigned)x, (unsigned)y, 131); int v = (int)(h % 24) - 12, lo = (int)(C3G_H((unsigned)(x / 8), (unsigned)(y / 8), 137) % 26) - 13;
        double rut = 0.5 + 0.5 * cos((x - S * 0.3) * 6.2831853 / (S * 0.12)), rut2 = 0.5 + 0.5 * cos((x - S * 0.7) * 6.2831853 / (S * 0.12)), dust = C3G_Noise(x, y, S, 141);
        int r = 86 + v + lo, g = 66 + v + lo, b = 48 + v / 2 + lo / 2;
        if ((abs(x - (int)(S * 0.3)) < S / 16 && rut > 0.6) || (abs(x - (int)(S * 0.7)) < S / 16 && rut2 > 0.6)) { r -= 26; g -= 22; b -= 16; if ((y / 6 + x / 3) % 4 == 0) { r -= 10; g -= 8; b -= 6; } }   /* (the ruts, tread marks) */
        if (dust > 0.66) { double k = (dust - 0.66) / 0.34; r += (int)(110 * k); g += (int)(90 * k); b += (int)(60 * k); }   /* (sawdust) */
        px[y * S + x] = C3G_Px(r, g, b); }
    for (n = 0; n < S * S / 40; n++) { unsigned h = C3G_H((unsigned)n, 23, 139); int x0 = (int)(h % (unsigned)S), y0 = (int)((h >> 9) % (unsigned)S), l = 2 + (int)((h >> 18) % 5), t, tone = (int)((h >> 24) % 40);
        for (t = 0; t < l; t++) { int xx = (x0 + t) % S, yy = (y0 + t / 2) % S; px[yy * S + xx] = C3G_Px(110 + tone, 56 + tone / 2, 34); } }   /* (bark chips) */
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
/* the forest buildings' materials (user, 2026-11-26: "more types of houses/buildings ... suitable for sierra nevada red wood / forest type scenario. maybe cabins?" - course3d.h
   C3_Cabin builds them): drawn here, no PNG.  C3C_LOGS round logs with pale chinking, 4 a repeat; C3C_BOARD board and batten (grey-brown: tinted per building);
   C3C_SHINGLE cedar shakes in rows; C3C_METAL a standing-seam roof (pale: tinted); C3C_STONE river stones in mortar; C3C_WINDOW a four-pane window in a cream frame, its
   glass texel alpha 0xBF (render3d.h: lit at night on a wall of vertex alpha 254); C3C_DOOR a plank door, a Z brace, an iron latch.  Then a thumbnail of each kind of
   building (the Course Editor's Buildings list) */
static const char *const k_c3CabName[C3_NCABCODE] = { "LOG CABIN", "A-FRAME", "LODGE", "RANGER STATION", "WOODSHED", "CABINS", "LOG PILE", "SAWMILL", "LOGGING CAMP" };
static const int k_c3CabTexW[C3C_NTEX] = { 128, 128, 128, 64, 128, 64, 64, 128 }, k_c3CabTexH[C3C_NTEX] = { 128, 128, 128, 64, 128, 64, 128, 128 };
static unsigned *g_c3CabPx[C3C_NTEX + C3_NCABCODE + 2];   /* (+ 1: the GUARD RAIL block's thumbnail - the Course Editor's Curbs list) */
static unsigned C3G_Pa(int r, int g, int b, int a) { return (C3G_Px(r, g, b) & 0xffffffu) | (unsigned)a << 24; }
static unsigned *C3G_CabTex(int t)
{   int S = k_c3CabTexW[t], H = k_c3CabTexH[t], x, y, n; unsigned *px = (unsigned *)malloc((size_t)S * H * 4);
    if (!px) return NULL;
    for (y = 0; y < H; y++) for (x = 0; x < S; x++) { unsigned h = C3G_H((unsigned)x, (unsigned)y, 71u + (unsigned)t); int v = (int)(h % 15) - 7, r = 0, g = 0, b = 0, a = 255;
        if (t == C3C_LOGS) { int lg = y / (H / 4), ly = y % (H / 4), L = H / 4; double f = (ly + 0.5) / L, rnd = sin(f * 3.14159265);
            int grain = (int)(C3G_H((unsigned)(x / 11 + lg * 5), (unsigned)(y / 2), 73) % 17) - 8, tone = (int)(C3G_H((unsigned)lg, (unsigned)(x / 64), 79) % 24) - 12;
            double l = 0.45 + 0.6 * rnd; r = (int)((170 + tone + grain) * l) + v; g = (int)((116 + tone * 3 / 4 + grain) * l) + v; b = (int)((70 + tone / 2 + grain / 2) * l) + v / 2;
            if (ly < 3 || ly > L - 3) { int cc = 196 + v; r = cc; g = cc - 6; b = cc - 20; if (ly == 0 || ly == L - 1) { r -= 40; g -= 40; b -= 40; } }   /* the chinking */
            if (C3G_H((unsigned)(x / 7), (unsigned)lg, 83) % 97 == 0 && ly > L / 3 && ly < 2 * L / 3) { r = r * 55 / 100; g = g * 55 / 100; b = b * 55 / 100; } }   /* (a knot) */
        else if (t == C3C_BOARD) { int bx = x % (S / 4), grain = (int)(C3G_H((unsigned)x, (unsigned)(y / 9 + x / (S / 4) * 3), 89) % 15) - 7, tone = (int)(C3G_H((unsigned)(x / (S / 4)), 1, 97) % 20) - 10;
            r = 156 + tone + grain + v / 2; g = 140 + tone + grain + v / 2; b = 124 + tone + grain + v / 2;
            if (bx < 7) { r += bx < 2 ? 26 : 12; g += bx < 2 ? 26 : 12; b += bx < 2 ? 26 : 12; }   /* the batten over the joint, lit on its left */
            else if (bx < 10) { r -= 46; g -= 46; b -= 46; } }                                    /* (its shadow) */
        else if (t == C3C_SHINGLE) { int row = y / 16, ry = y % 16, off = (int)(C3G_H((unsigned)row, 5, 101) % 16), xs = (x + off) % S, cell = 0, cx = 0, w, tone;
            while (cx + (w = 10 + (int)(C3G_H((unsigned)cell, (unsigned)row, 103) % 14)) <= xs) { cx += w; cell++; }
            tone = (int)(C3G_H((unsigned)cell, (unsigned)row, 107) % 40) - 20;
            r = 128 + tone + v; g = 98 + tone * 4 / 5 + v; b = 76 + tone * 3 / 5 + v;
            r = r * (60 + ry * 40 / 16) / 100; g = g * (60 + ry * 40 / 16) / 100; b = b * (60 + ry * 40 / 16) / 100;   /* (each row's top in the shade of the one above) */
            if (xs - cx < 1 || ry > 14) { r = r * 45 / 100; g = g * 45 / 100; b = b * 45 / 100; } }
        else if (t == C3C_METAL) { int sx = x % 16; r = g = b = 206 + v / 2 - (sx == 0 ? 60 : sx == 1 ? -26 : sx == 15 ? 20 : 0); }
        else if (t == C3C_STONE) { int gx, gy, NX = S / 16, NY = H / 16; double d1 = 1e9, d2 = 1e9; unsigned best = 0;   /* (a jittered point a 16 square, wrapping: mortar where the two nearest are near alike) */
            for (gy = -1; gy <= 1; gy++) for (gx = -1; gx <= 1; gx++) { int cx = x / 16 + gx, cy = y / 16 + gy, wx = (cx + NX) % NX, wy = (cy + NY) % NY;
                unsigned q = C3G_H((unsigned)wx, (unsigned)wy, 109); double qx = cx * 16 + 3 + (q % 10), qy = cy * 16 + 3 + ((q >> 8) % 10), d = (x - qx) * (x - qx) * 0.6 + (y - qy) * (y - qy) * 1.4;
                if (d < d1) { d2 = d1; d1 = d; best = q; } else if (d < d2) d2 = d; }
            {   int tone = (int)((best >> 16) % 60), warm = (best >> 24) % 3; r = 112 + tone + (warm == 0 ? 14 : 0) + v; g = 108 + tone + v; b = 100 + tone - (warm == 0 ? 8 : 0) + v;
                if (sqrt(d2) - sqrt(d1) < 1.6) { r = 84 + v; g = 80 + v; b = 74 + v; } } }
        else if (t == C3C_WINDOW) { int fr = x < 6 || x >= S - 6 || y < 6 || y >= H - 6, mu = abs(x - S / 2) < 2 || abs(y - H / 2) < 2;
            if (fr || mu) { int e = (x < 2 || y < 2) ? 18 : (x >= S - 2 || y >= H - 2) ? -30 : 0; r = 222 + e + v / 2; g = 214 + e + v / 2; b = 192 + e + v / 2; }
            else { double sky = 1.0 - (double)y / H; r = (int)(62 + 56 * sky) + v / 2; g = (int)(78 + 62 * sky) + v / 2; b = (int)(88 + 78 * sky) + v / 2; a = 0xBF;   /* (the sky in it, darker below) */
                if (((x + y) % 23) < 3 && x % (S / 2) > 8) { r += 40; g += 40; b += 40; } } }   /* (a glint) */
        else { int pl = x / (S / 4), grain = (int)(C3G_H((unsigned)x, (unsigned)(y / 7 + pl * 3), 113) % 13) - 6, e = x % (S / 4) == 0 ? -40 : 0;
            r = 96 + grain + e + v / 2; g = 60 + grain + e + v / 2; b = 38 + grain / 2 + e + v / 3;
            if (x < 3 || x >= S - 3 || y < 3) { r = 70; g = 44; b = 28; } }
        px[y * S + x] = C3G_Pa(r, g, b, a); }
    if (t == C3C_FERN) {   /* a sword fern (the redwoods' undergrowth): fronds from the foot at the bottom middle, arching out, leaflets along them; alpha 0 between (cut out) */
        int f; for (n = 0; n < S * H; n++) px[n] = 0;
        for (f = 0; f < 11; f++) { unsigned hf = C3G_H((unsigned)f, 3, 127); double ang = -1.35 + 2.7 * (f + 0.5) / 11 + ((hf % 100) - 50) / 400.0, len = H * (0.62 + (hf >> 8) % 30 / 100.0), bend = 0.9 + (hf >> 16) % 40 / 100.0, tt;
            int sh = (int)((hf >> 20) % 40);
            for (tt = 0; tt < 1; tt += 0.004) { double a = ang * (1 + bend * tt * tt), x0 = S * 0.5 + sin(a) * len * tt, y0 = H - 1 - cos(a) * len * tt * (1 - 0.35 * tt * fabs(ang)), lf = (1 - tt) * 12 + 2, k;
                int xi = (int)x0, yi = (int)y0; if (xi >= 0 && xi < S && yi >= 0 && yi < H) px[yi * S + xi] = C3G_Pa(48 + sh / 2, 92 + sh, 30, 255);   /* (the stem) */
                if (fmod(tt * 60, 1.0) < 0.5) for (k = 1; k < lf; k += 0.7) { int s2; for (s2 = -1; s2 <= 1; s2 += 2) {   /* (a pair of leaflets, slanting forward) */
                    int xx = (int)(x0 + s2 * cos(a) * k + sin(a) * k * 0.35), yy = (int)(y0 + s2 * sin(a) * k - cos(a) * k * 0.35); int l = (int)(k * 4);
                    if (xx >= 0 && xx < S && yy >= 0 && yy < H) px[yy * S + xx] = C3G_Pa(46 + sh + l / 2, 104 + sh + l, 36 + sh / 2, 255); } } } } }
    if (t == C3C_DOOR) {   /* the Z brace and the latch */
        for (n = 0; n < 3; n++) { int y0 = n == 0 ? H / 5 : n == 1 ? 4 * H / 5 : 0, yy, xx;
            if (n < 2) { for (yy = y0 - 4; yy < y0 + 4; yy++) for (xx = 5; xx < S - 5; xx++) px[yy * S + xx] = C3G_Px(112, 72, 46); }
            else for (xx = 5; xx < S - 5; xx++) { int yc = 4 * H / 5 - (xx - 5) * (3 * H / 5) / (S - 10); for (yy = yc - 4; yy < yc + 4; yy++) if (yy >= 0 && yy < H) px[yy * S + xx] = C3G_Px(108, 70, 44); } }
        for (y = H / 2 - 3; y < H / 2 + 3; y++) for (x = S - 16; x < S - 8; x++) px[y * S + x] = C3G_Px(40, 40, 42); }
    return px; }
/* a thumbnail: the building seen from the front against the sky (pictures from the materials, tinted) - a rectangle or (tri) a triangle of the 128 square, placed at ox, oy and scaled sc */
static void C3G_TFill(unsigned *c, double ox, double oy, double sc, int x0, int y0, int x1, int y1, int t, unsigned tint, int tri, int x2, int y2)
{   const unsigned *tp = g_c3CabPx[t]; int tw = k_c3CabTexW[t], th = k_c3CabTexH[t], X, Y, X0, X1, Y0, Y1, whole = t == C3C_WINDOW || t == C3C_DOOR;
    double ax = ox + x0 * sc, ay = oy + y0 * sc, bx = ox + x1 * sc, by = oy + y1 * sc, cx = ox + x2 * sc, cy = oy + y2 * sc;
    if (!tp) return;
    X0 = (int)floor(tri ? fmin(ax, fmin(bx, cx)) : fmin(ax, bx)); X1 = (int)ceil(tri ? fmax(ax, fmax(bx, cx)) : fmax(ax, bx));
    Y0 = (int)floor(tri ? fmin(ay, fmin(by, cy)) : fmin(ay, by)); Y1 = (int)ceil(tri ? fmax(ay, fmax(by, cy)) : fmax(ay, by));
    for (Y = Y0 < 0 ? 0 : Y0; Y < Y1 && Y < 128; Y++) for (X = X0 < 0 ? 0 : X0; X < X1 && X < 128; X++) { unsigned q; int j; unsigned o = 0xff000000u;
        if (tri) { double px = X + 0.5, py = Y + 0.5, d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by), d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy), d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
            if (((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0))) continue; }
        if (whole) q = tp[((Y - Y0) * th / (Y1 - Y0 > 0 ? Y1 - Y0 : 1)) % th * tw + ((X - X0) * tw / (X1 - X0 > 0 ? X1 - X0 : 1)) % tw];   /* (a window, a door: its whole picture) */
        else q = tp[((int)((Y - Y0) / sc * 0.5) % th) * tw + (int)((X - X0) / sc * 0.5) % tw];
        for (j = 0; j < 3; j++) o |= (unsigned)(((q >> (8 * j)) & 255) * ((tint >> (8 * j)) & 255) / 255) << (8 * j);
        c[Y * 128 + X] = o; } }
#define C3G_R(x0, y0, x1, y1, t, tint) C3G_TFill(c, ox, oy, sc, x0, y0, x1, y1, t, tint, 0, 0, 0)
#define C3G_T(x0, y0, x1, y1, x2, y2, t, tint) C3G_TFill(c, ox, oy, sc, x0, y0, x1, y1, t, tint, 1, x2, y2)
static void C3G_Disc(unsigned *c, double cx, double cy, double r)   /* (a thumbnail's log end: bark ring, pale wood, a darker heart) */
{   int x, y; for (y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++) for (x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) { double d = sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
        if (x < 0 || x >= 128 || y < 0 || y >= 128 || d > r) continue; c[y * 128 + x] = d > r * 0.82 ? C3G_Px(70, 46, 32) : d < r * 0.3 ? C3G_Px(150, 110, 70) : C3G_Px(200, 160, 110); } }
static void C3G_CabShape(unsigned *c, int kind, double ox, double oy, double sc)   /* kind's front on the 128 square, its ground line at y 112 (then moved / scaled) */
{   const unsigned W = 0xffffffffu;
    if (kind == C3_CAB_LOG) { C3G_R(18, 70, 110, 112, C3C_LOGS, W); C3G_R(86, 30, 98, 60, C3C_STONE, W);
        C3G_T(8, 72, 120, 72, 98, 42, C3C_SHINGLE, W); C3G_T(8, 72, 98, 42, 30, 42, C3C_SHINGLE, W);
        C3G_R(57, 84, 71, 112, C3C_DOOR, W); C3G_R(28, 80, 46, 98, C3C_WINDOW, W); C3G_R(82, 80, 100, 98, C3C_WINDOW, W); C3G_R(16, 108, 112, 113, C3C_STONE, W); }
    else if (kind == C3_CAB_AFRAME) { C3G_T(12, 112, 116, 112, 64, 18, C3C_METAL, 0xff3c5a3cu); C3G_T(30, 112, 98, 112, 64, 50, C3C_WINDOW, W); C3G_R(58, 92, 70, 112, C3C_DOOR, W); C3G_R(26, 108, 102, 113, C3C_BOARD, 0xff4a6a8cu); }
    else if (kind == C3_CAB_LODGE) { int i; C3G_R(8, 52, 120, 112, C3C_BOARD, 0xff4a6a8cu); C3G_T(2, 54, 126, 54, 104, 24, C3C_SHINGLE, W); C3G_T(2, 54, 104, 24, 24, 24, C3C_SHINGLE, W);
        for (i = 0; i < 4; i++) { C3G_R(16 + i * 27, 60, 32 + i * 27, 76, C3C_WINDOW, W); if (i != 1) C3G_R(16 + i * 27, 86, 32 + i * 27, 102, C3C_WINDOW, W); }
        C3G_R(44, 86, 58, 112, C3C_DOOR, W); C3G_R(4, 80, 124, 84, C3C_LOGS, W); C3G_R(98, 18, 110, 40, C3C_STONE, W); }
    else if (kind == C3_CAB_RANGER) { C3G_R(16, 66, 108, 112, C3C_BOARD, 0xff587c9cu); C3G_T(8, 68, 116, 68, 96, 44, C3C_METAL, 0xff3c6a46u); C3G_T(8, 68, 96, 44, 28, 44, C3C_METAL, 0xff3c6a46u);
        C3G_R(26, 78, 44, 96, C3C_WINDOW, 0xff90c8a0u); C3G_R(80, 78, 98, 96, C3C_WINDOW, 0xff90c8a0u); C3G_R(56, 84, 69, 112, C3C_DOOR, W); C3G_R(118, 18, 120, 112, C3C_METAL, W); C3G_R(120, 20, 127, 28, C3C_METAL, 0xff3c6a46u); }
    else if (kind == C3_CAB_SHED) { int i, j; C3G_R(30, 78, 98, 112, C3C_BOARD, 0xff5a7090u); C3G_R(36, 84, 92, 112, C3C_BOARD, 0xff303030u); C3G_T(24, 80, 104, 80, 104, 70, C3C_SHINGLE, W); C3G_T(24, 80, 104, 70, 24, 74, C3C_SHINGLE, W);
        for (j = 0; j < 4; j++) for (i = 0; i < 7; i++) C3G_R(38 + i * 8, 86 + j * 6, 44 + i * 8, 91 + j * 6, C3C_LOGS, 0xffa0d0f0u); }
    else if (kind == C3_CAB_LOGPILE) { int row, i, R = 9;   /* (log ends in a pyramid, an orange strap) */
        for (row = 0; row < 5; row++) for (i = 0; i < 5 - row; i++) C3G_Disc(c, ox + (20 + R + row * R + i * 2 * R) * sc, oy + (112 - R - row * R * 1.72) * sc, R * sc);
        C3G_R(60, 30, 64, 112, C3C_BOARD, 0xff1c7ae0u); }
    else if (kind == C3_CAB_SAWMILL) { C3G_T(6, 54, 122, 54, 64, 26, C3C_METAL, 0xff2e4a8au); C3G_R(14, 54, 114, 112, C3C_BOARD, 0xff303030u);
        C3G_R(14, 54, 22, 112, C3C_BOARD, 0xff4a5a6au); C3G_R(60, 54, 68, 112, C3C_BOARD, 0xff4a5a6au); C3G_R(106, 54, 114, 112, C3C_BOARD, 0xff4a5a6au);
        C3G_R(24, 92, 80, 104, C3C_LOGS, 0xffa0c0e0u); C3G_Disc(c, ox + 92 * sc, oy + 90 * sc, 12 * sc); } }
static unsigned *C3G_CabThumb(int kind)
{   unsigned *c = (unsigned *)malloc(128 * 128 * 4); int x, y;
    if (!c) return NULL;
    for (y = 0; y < 128; y++) for (x = 0; x < 128; x++) c[y * 128 + x] = y < 112 ? C3G_Px(150 + y / 3, 182 + y / 4, 206 + y / 6) : C3G_Px(84, 60, 40);   /* (sky, the duff) */
    for (y = 0; y < 112; y++) for (x = 0; x < 128; x++) if (abs(x - 4) < 5 || abs(x - 124) < 4) c[y * 128 + x] = C3G_Px(110 + (x & 3) * 6, 52, 34);   /* (two redwood trunks) */
    if (kind < C3_CAB_MIX || kind == C3_CAB_LOGPILE || kind == C3_CAB_SAWMILL) C3G_CabShape(c, kind, 0, 0, 1);
    else if (kind == C3_CAB_CAMP) { C3G_CabShape(c, C3_CAB_SAWMILL, 44, 45, 0.62); C3G_CabShape(c, C3_CAB_LOGPILE, -4, 45, 0.6); }   /* (the camp: a mill, a log deck) */
    else { C3G_CabShape(c, C3_CAB_AFRAME, 50, 45, 0.6); C3G_CabShape(c, C3_CAB_LOG, -2, 45, 0.6); }   /* (the mix: two of them) */
    return c; }
#undef C3G_R
#undef C3G_T
static unsigned *C3G_SnowThumb(void)   /* (SNOW STARTS / FULL SNOW's thumbnail: a snowy slope under a grey sky, flakes) */
{   unsigned *c = (unsigned *)malloc(128 * 128 * 4); int x, y, n;
    if (!c) return NULL;
    for (y = 0; y < 128; y++) for (x = 0; x < 128; x++) { int g = 150 + y / 3, sl = 70 + (int)(18 * sin(x * 0.05)); c[y * 128 + x] = y > sl ? C3G_Px(232 - (y - sl) / 4, 238 - (y - sl) / 5, 248) : C3G_Px(g, g + 4, g + 12); }
    for (n = 0; n < 90; n++) { unsigned h = C3G_H((unsigned)n, 5, 157); int cx = (int)(h % 128), cy = (int)((h >> 8) % 128), r = 1 + (int)((h >> 16) % 2), dx, dy;
        for (dy = -r; dy <= r; dy++) for (dx = -r; dx <= r; dx++) if (dx * dx + dy * dy <= r * r && cx + dx >= 0 && cx + dx < 128 && cy + dy >= 0 && cy + dy < 128) c[(cy + dy) * 128 + cx + dx] = C3G_Px(255, 255, 255); }
    return c; }
static unsigned *C3G_RailThumb(void)   /* (GUARD RAIL's thumbnail: a W-beam on posts beside the road, sky behind) */
{   unsigned *c = (unsigned *)malloc(128 * 128 * 4); int x, y;
    if (!c) return NULL;
    for (y = 0; y < 128; y++) for (x = 0; x < 128; x++) { unsigned v = C3G_H((unsigned)x, (unsigned)y, 151) % 10;
        c[y * 128 + x] = y < 70 ? C3G_Px(150 + y / 2, 182 + y / 3, 210) : y < 96 ? C3G_Px(110 + v, 100 + v, 80 + v) : C3G_Px(70 + v, 72 + v, 76 + v); }   /* (sky, the verge, the road) */
    for (y = 96; y < 99; y++) for (x = 0; x < 128; x++) c[y * 128 + x] = C3G_Px(220, 220, 214);   /* (the edge line) */
    for (x = 10; x < 128; x += 36) for (y = 58; y < 96; y++) { int k; for (k = 0; k < 6; k++) if (x + k < 128) c[y * 128 + x + k] = C3G_Px(96, 100, 106); }   /* (posts) */
    for (y = 60; y < 80; y++) for (x = 0; x < 128; x++) { int b = y - 60, g = b < 6 ? 205 - b * 4 : b < 13 ? 140 + (b - 6) * 3 : 200 - (b - 13) * 5; c[y * 128 + x] = C3G_Px(g, g + 3, g + 7); }   /* (the beam: lip, groove, lip) */
    return c; }
/* the materials and the thumbnails into ra's textures: tex[cab0 + C3C_*], thumbnails tex[cab0 + C3C_NTEX + kind] (C3_NCAB: the mix); drawn once, shared */
static void C3_CabinArt(RxAssets *ra)
{   static const char *const nm[C3C_NTEX] = { "CABLOGS", "CABBOARD", "CABSHING", "CABMETAL", "CABSTONE", "CABWIN", "CABDOOR", "CABFERN" }; int t;
    ra->railTex = 0;
    if (ra->ntex < 8192) { RxTex *x = &ra->tex[ra->ntex];   /* (the original guard rail picture: rail_art.h) */
        memset(x, 0, sizeof *x); _snprintf(x->name, sizeof x->name, "RRRAIL1"); x->name[8] = 0; x->w = 64; x->h = 16; x->gen = k_c3RailPx; x->gpu = -1; x->key = 1; ra->railTex = ++ra->ntex; }
    ra->cab0 = ra->ntex; ra->ncab = 0;
    for (t = 0; t < C3C_NTEX; t++) if (!g_c3CabPx[t]) g_c3CabPx[t] = C3G_CabTex(t);
    if (!g_c3CabPx[C3C_NTEX + C3_NCABCODE]) g_c3CabPx[C3C_NTEX + C3_NCABCODE] = C3G_RailThumb();
    if (!g_c3CabPx[C3C_NTEX + C3_NCABCODE + 1]) g_c3CabPx[C3C_NTEX + C3_NCABCODE + 1] = C3G_SnowThumb();
    for (t = 0; t < C3_NCABCODE; t++) if (!g_c3CabPx[C3C_NTEX + t]) g_c3CabPx[C3C_NTEX + t] = C3G_CabThumb(t);
    for (t = 0; t < C3C_NTEX + C3_NCABCODE + 2; t++) if (!g_c3CabPx[t]) return;
    if (ra->ntex + C3C_NTEX + C3_NCABCODE + 2 > 8192) return;
    for (t = 0; t < C3C_NTEX + C3_NCABCODE + 2; t++) { RxTex *x = &ra->tex[ra->ntex++];
        memset(x, 0, sizeof *x);
        if (t < C3C_NTEX) _snprintf(x->name, sizeof x->name, "%s", nm[t]); else _snprintf(x->name, sizeof x->name, "CABTHM%d", t - C3C_NTEX);
        x->name[8] = 0; x->w = t < C3C_NTEX ? k_c3CabTexW[t] : 128; x->h = t < C3C_NTEX ? k_c3CabTexH[t] : 128; x->gen = g_c3CabPx[t]; x->gpu = -1; x->facade = t == C3C_WINDOW; x->key = t == C3C_FERN; }
    ra->ncab = C3_NCABCODE; }
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
static int g_c3GroundTrack;   /* the track (1..5) whose own ground pictures come first: hd\t<track>\ground\ before hd\ground\ (user, 2026-11-27: The City's course a Sierra - its CLIFF
                                 granite, not the shared sandstone); 0 none.  Set before C3_GroundArt (world3d.inc W3GroundArt, courseedit.c) */
static int C3_GroundArt(RxAssets *ra, const char *hdDir)
{   char p[520]; int n, kind, found = 0;
    ra->ground0 = ra->ntex; ra->nground = 0; memset(ra->groundTree, 0, sizeof ra->groundTree); memset(ra->groundHedge, 0, sizeof ra->groundHedge); ra->road0 = ra->ntex; ra->nroad = 0;
    for (n = 0; n < C3_NGROUND * 3; n++) { free(g_c3GroundPx[n]); g_c3GroundPx[n] = NULL; }
    if (!hdDir) hdDir = "";   /* (no mod folder: the built-in pictures - WATER, HOLE, COBBLE - still; user's courses must look the same without the mod) */
    for (kind = 0; kind < 3; kind++) for (n = 0; n < C3_NGROUND && ra->ntex < 8192; n++) { RxTex *x; unsigned *px; int W = kind == 1 ? 256 : 512, H = kind == 1 ? 1024 : kind ? 256 : 512;
        px = NULL;
        if (hdDir[0] && g_c3GroundTrack > 0) { _snprintf(p, sizeof p - 1, "%s\\t%d\\ground\\%s%s.png", hdDir, g_c3GroundTrack, k_c3Ground[n], kind == 1 ? "_TREE" : kind ? "_HEDGE" : ""); p[sizeof p - 1] = 0;
            px = C3G_LoadPng(p, W, H); }   /* (the track's own first) */
        _snprintf(p, sizeof p - 1, "%s\\ground\\%s%s.png", hdDir, k_c3Ground[n], kind == 1 ? "_TREE" : kind ? "_HEDGE" : ""); p[sizeof p - 1] = 0;
        if (!px) px = hdDir[0] ? C3G_LoadPng(p, W, H) : NULL;
        if (!px && kind == 2 && hdDir[0] && (px = C3G_LoadPng(p, 512, 512)) != NULL) H = 512;   /* (a square hedge: FOREST's tall wall of trees) */
        if (!px && kind == 1 && hdDir[0] && (px = C3G_LoadPng(p, 512, 512)) != NULL) W = H = 512;   /* (a square tree: SHRUB_TREE, a bush) */
        if (kind && !px) continue;   /* (no tree / hedge) */
        if (!kind && !px && hdDir[0] && !strcmp(k_c3Ground[n], "SEACLIFF")) { _snprintf(p, sizeof p - 1, "%s\\ground\\CLIFF.png", hdDir); p[sizeof p - 1] = 0; px = C3G_LoadPng(p, W, H); }   /* (no SEACLIFF.png: CLIFF's rock) */
        if (!kind && !px && !strcmp(k_c3Ground[n], "WATER") && (px = C3G_Water(64)) != NULL) W = H = 64;
        if (!kind && !px && (!strcmp(k_c3Ground[n], "HOLE") || !strcmp(k_c3Ground[n], "DHOLE")) && (px = C3G_Hole(64)) != NULL) W = H = 64;
        if (!kind && !px && !strcmp(k_c3Ground[n], "BRIDGE") && (px = C3G_Bridge(128)) != NULL) W = H = 128;
        if (!kind && !px && (!strcmp(k_c3Ground[n], "SLABS") || !strcmp(k_c3Ground[n], "TILES")) && (px = C3G_Paving(128, n && !strcmp(k_c3Ground[n], "TILES"))) != NULL) W = H = 128;
        if (!kind && !px && C3_GroundColl(n) == C3_COLL_JUNCTION && (px = C3G_Junction(64, !strcmp(k_c3Ground[n], "SIDEST") ? 0 : !g_c3GroundSymbols ? 3 : !strcmp(k_c3Ground[n], "TLIGHTS") ? 1 : 2)) != NULL) W = H = 64;
        if (!kind && !px && !strcmp(k_c3Ground[n], "CONCRETE") && (px = C3G_Concrete(256)) != NULL) W = H = 256;
        if (!kind && !px && (!strcmp(k_c3Ground[n], "DUFF") || !strcmp(k_c3Ground[n], "FENCE")) && (px = C3G_Duff(256, !strcmp(k_c3Ground[n], "FENCE") && g_c3GroundSymbols)) != NULL) W = H = 256;
        if (!kind && !px && (!strcmp(k_c3Ground[n], "TRAIL") || !strcmp(k_c3Ground[n], "BOARDWALK") || !strcmp(k_c3Ground[n], "GRAVEL") || !strcmp(k_c3Ground[n], "WOODCHIP"))
            && (px = !strcmp(k_c3Ground[n], "GRAVEL") ? C3G_Gravel(256) : C3G_Path(128, !strcmp(k_c3Ground[n], "TRAIL") ? 0 : !strcmp(k_c3Ground[n], "BOARDWALK") ? 1 : 3)) != NULL) W = H = !strcmp(k_c3Ground[n], "GRAVEL") ? 256 : 128;   /* (GRAVEL: C3G_Gravel) */
        if (!kind && !px && !strcmp(k_c3Ground[n], "LOGYARD") && (px = C3G_LogYard(256)) != NULL) W = H = 256;
        if (!kind && !px && k_c3GroundCol[n] && (px = C3G_Flat(64, k_c3GroundCol[n])) != NULL) W = H = 64;
        if (!kind && !px && (px = (unsigned *)malloc(4)) != NULL) { *px = 0xff2a3a5au; W = H = 1; }   /* (no picture: a dark bark brown) */
        if (!px) continue;
        g_c3GroundPx[kind * C3_NGROUND + n] = px;
        x = &ra->tex[ra->ntex]; memset(x, 0, sizeof *x);
        _snprintf(x->name, sizeof x->name, "%.6s%s", k_c3Ground[n], kind == 1 ? "TR" : kind ? "HG" : ""); x->name[8] = 0; x->w = W; x->h = H; x->gen = px; x->gpu = -1;
        if (kind) { x->key = 1; if (n < 32) { if (kind == 1) ra->groundTree[n] = ra->ntex + 1; else ra->groundHedge[n] = ra->ntex + 1; } } else { ra->nground++; found += W > 1; }        ra->ntex++; }
    ra->road0 = ra->ntex; ra->nroad = 0;
    {   int ns; ra->shrubTex = 0;   /* SHRUB (user, 2026-11-20: "make it a terrain please shubbery and in P3D it stays flat but in T3D it turns into that shubbery and it has 4 sides"):
                                       its blocks' sides are SHRUB_HEDGE.png if there, else REDWOOD's hedge */
        for (ns = 0; ns < C3_NGROUND && strcmp(k_c3Ground[ns], "SHRUB"); ns++) ;
        for (n = 0; n < ra->ntex && ns < C3_NGROUND; n++) if (ra->tex[n].gen && ra->tex[n].gen == g_c3GroundPx[2 * C3_NGROUND + ns]) ra->shrubTex = n + 1;
        if (!ra->shrubTex) ra->shrubTex = ra->groundHedge[0];
        ra->shrubGTex = 0;   /* GREEN SHRUB (user, 2026-11-24 "a green variation of it? a dark green shubbery wall"): SHRUB's hedge recoloured dark green (its light and shade kept), SHRUB's bushes */
        if (ra->shrubTex && ra->ntex < 8192) { const RxTex *sh = &ra->tex[ra->shrubTex - 1]; unsigned *gp = sh->gen ? (unsigned *)malloc((size_t)sh->w * sh->h * 4) : NULL; int ng, q;
            for (ng = 0; ng < C3_NGROUND && strcmp(k_c3Ground[ng], "SHRUBG"); ng++) ;
            if (gp && ng < C3_NGROUND) { RxTex *x = &ra->tex[ra->ntex];
                for (q = 0; q < sh->w * sh->h; q++) { unsigned o = sh->gen[q]; int r = o & 255, g = (o >> 8) & 255, b = (o >> 16) & 255, l = (r * 3 + g * 5 + b * 2) / 10;
                    gp[q] = (o & 0xff000000u) | C3G_Px(l * 30 / 100, l * 62 / 100 + 6, l * 32 / 100) & 0x00ffffffu; }
                free(g_c3GroundPx[2 * C3_NGROUND + ng]); g_c3GroundPx[2 * C3_NGROUND + ng] = gp;
                memset(x, 0, sizeof *x); _snprintf(x->name, sizeof x->name, "SHRUBGHG"); x->name[8] = 0; x->w = sh->w; x->h = sh->h; x->gen = gp; x->gpu = -1; x->key = 1; ra->shrubGTex = ++ra->ntex;
                ra->groundTree[ng] = ra->groundTree[ns < C3_NGROUND ? ns : 0]; } else free(gp); }
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
    ra->road0 = ra->ntex; ra->nroad = 0;   /* (after GREEN SHRUB's hedge) */
    for (n = 0; n < C3_NROAD && ra->ntex < 8192; n++) { RxTex *x; unsigned *px; int W = 512, H = 512;   /* (the road surfaces: in order - their codes are their places) */
        free(g_c3RoadPx[n]); g_c3RoadPx[n] = NULL;
        _snprintf(p, sizeof p - 1, "%s\\road\\%s.png", hdDir, k_c3Road[n]); p[sizeof p - 1] = 0;
        if (!(px = hdDir[0] ? C3G_LoadPng(p, W, H) : NULL) && (px = !strcmp(k_c3Road[n], "RAINBOW") ? C3G_Rainbow(256) : !strcmp(k_c3Road[n], "TRAM") ? C3G_Road(256, 0) : !strcmp(k_c3Road[n], "SPDBUMP") ? C3G_Road(256, 1) : C3G_Cobble(256)) != NULL) W = H = 256;
        if (!px) break;
        g_c3RoadPx[n] = px; x = &ra->tex[ra->ntex]; memset(x, 0, sizeof *x);
        _snprintf(x->name, sizeof x->name, "%.8s", k_c3Road[n]); x->name[8] = 0; x->w = W; x->h = H; x->gen = px; x->gpu = -1;
        ra->nroad++; ra->ntex++; }
    C3_CabinArt(ra);   /* (the forest buildings' materials: course3d.h C3_Cabin) */
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
