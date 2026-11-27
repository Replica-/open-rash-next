/* world3d\rrx_assets.h - the track's textures for the true-3D renderer, from an RR_EXTRACT file (version 2 chunks; format in analysis\TRUE3D_PROJECT.md).
 * Header-only.  Resolves the texture of a road piece's strips the way the game's draw code does (src\render\trackside_draw.c, wheel_marks.c):
 *   - lane strips: the track's global draw objects tbl_roadTexLanes[0..2] (PLAIN / WHITE / YELLOW lane variants), tbl_roadTexStart.. (START), g_roadTexFinish (FINISH)
 *   - roadside codes (RoadSection.sideTex / sideArg / kindArg[1]): bits 6..7 = sprite-set slot, bits 0..5 = entry; the set in that slot is the one whose
 *     marker range (course block's marker table: state 1 opens, state 2 closes, positions in block segments) holds the piece;
 *     lookup = set category c -> entry (code & 0x3f) -> row b (BankEntry / BankEntry2 / BankEntry3) -> 'PGLC' group -> ' BCC' draw object -> texture.
 * Textures are 8-bit palette indices; colour-key textures (flag 0x1000) have index 0 transparent.
 */
#ifndef RR_RRX_ASSETS_H
#define RR_RRX_ASSETS_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../road/course_edit.h"   /* (RrcExtra: an edited course's CEXT chunk) */

typedef struct RxSet { unsigned id, base, size; unsigned char *buf; } RxSet;
typedef struct RxMarks { unsigned img; int count; const unsigned char *rec; } RxMarks;   /* records { pos, state, slot, -, id } of 12 bytes */
typedef struct RxTex {                         /* one texture object */
    unsigned addr; char name[9]; int w, h; unsigned flags;
    const unsigned char *bits;                 /* w * h palette indices */
    int gpu;                                   /* renderer texture index (-1 = not made yet) */
    int key;                                   /* index 0 transparent (colour-key flag 0x1000, or used as a sprite) */
    int alpha;                                 /* opacity of the visible texels (0 = 255): translucent textures (the sprite shadow) */
    int holes;                                 /* some texel has index 0 (see-through where the texture is colour-keyed) */
    const unsigned *gen;                       /* a generated texture's RGBA (world3d.inc W3GenFacades; bits = NULL) */
    int facade;                                /* used as a building facade (course3d.h C3_Building): its windows light up at night */
    int gkind;                                 /* as ground (course3d.h C3_TexDirt): 0 not looked at yet, 1 dirt, 2 something else */
    float shopV;                               /* a facade picture of the City set (world3d.inc W3LoadCityArt): its shop line, v from the top (0 = not one) */
    int baked;                                 /* (course3d.h C3_TexBaked) 0 not looked at, 1 a band painted in at its road side (DARK4's tarmac), 2 none */
    int edgeOf, edgeDir;                       /* the ragged edge of texture edgeOf - 1 (course3d.h C3_Blend): its pixels cut from that one's (RX_EdgeCut) as it is
                                                  drawn (the HD picture where there is one); edgeDir 0 across (its first quarter), 1 along (all of it), 2..5 across 1..4 copies of it */
    int edge[6];                               /* this texture's edges by edgeDir (tex index + 1; 0 = not made yet) */
} RxTex;

typedef struct RxAssets {
    unsigned pal[256];                         /* PALETTEENTRY { r, g, b, flags } */
    int npiece; unsigned (*pcur)[6];           /* per piece: { block, marker image, block segment } for section source 0 (half A) and 1 (half B) */
    RxSet set[256]; int nset;
    RxMarks mark[256]; int nmark;
    RxTex tex[8192]; int ntex;
    int global[64];                            /* TEXG index -> tex[] index (-1); 32 = the sea (g_seaObject) */
    unsigned slot0[4];                         /* SLOT: the set in each slot at the race set-up (before a slot's first marker) */
    unsigned short (*bind)[2][4];              /* per piece, section source, slot: the sprite set id (RX_Bind) */
    unsigned char *file;                       /* the chunk payloads stay here */
    int gen0, ngen;                            /* generated facades: tex[gen0 .. gen0 + ngen - 1] (course3d.h C3_FacadeVary) */
    int rockTex;                               /* the rocks' texture (world3d.inc W3RockTex; -1 = none: course3d.h C3_Rock flat colour) */
    int sign0, nsign;                          /* projecting signs (the City set): tex[sign0 .. sign0 + nsign - 1] (course3d.h C3_BladeSign) */
    int ground0, nground;                      /* extra ground pictures (ground_art.h: REDWOOD): tex[ground0 + n] = RRC_BAND | RRC_GEN | n painted on a cell */
    int groundTree[32];                        /* (16: every ground of ground_art.h k_c3Ground - FOREST / SHRUB too) each one's tree (tex index + 1; 0 none): stands on the cells painted with it (course3d.h C3_GroundTrees) */
    int road0, nroad;                          /* the extra road surfaces (ground_art.h k_c3Road: COBBLE ...): painted as RRC_BAND | RRC_GEN | n in RrcExtra.road (course3d.h C3_Lanes) */
    int groundHedge[32];                       /* each one's hedge (tex index + 1; 0 none): along the inner edge of a "beyond" cell painted with it (course3d.h C3_GroundHedge) */
    int forestTex;                             /* FOREST's blocks: its wall of trees (FOREST_HEDGE.png, tex index + 1; 0 none - course3d.h C3_Shrub) */
    int dholeTex;                              /* DIRT HOLE's walls: a dirt cliff face (DHOLE_HEDGE.png, tex index + 1; 0 none: CLIFF's rock - course3d.h C3_HoleWall) */
    int shrubGTex;                             /* SHRUBG's blocks: SHRUB's hedge in dark green (made at load: ground_art.h C3_GroundArt; tex index + 1; 0 none) */
    int shrubTex;                              /* SHRUB's blocks (ground_art.h): the hedge picture on their sides (tex index + 1; 0 none - course3d.h C3_Shrub) */
    int cab0, ncab;                            /* the forest buildings (ground_art.h C3_CabinArt): their materials tex[cab0 + C3C_*], each kind's thumbnail tex[cab0 + C3C_NTEX + kind]; ncab 0 = none */
    int railTex;                               /* the original guard rail picture RRRAIL1 (rail_art.h: for a track whose sets have none - course3d.h C3_SideStructs; tex index + 1, 0 none) */
    int opt[4];                                /* an edited course's settings (COPT chunk, road_build.c): snow starts / is full at piece + 1 (0 none), traffic percent, the horizon (a track's 1..5: hdsky.inc HsCourse) (weather.inc WxCourseSnow) */
    RrcExtra *ext; int next;                   /* per piece: an edited course's extras (CEXT: painted terrain bands, course3d.h C3_ProfileTex; NULL = none) - its own memory */
} RxAssets;

static void RX_Bind(RxAssets *a);
static RxTex *RX_AddTex(RxAssets *a, unsigned addr, const unsigned *t, const unsigned char *bits)
{
    int i; RxTex *x;
    for (i = 0; i < a->ntex; i++) if (a->tex[i].addr == addr) return &a->tex[i];
    if (a->ntex >= 8192) return NULL;
    x = &a->tex[a->ntex++];
    x->addr = addr; memcpy(x->name, &t[4], 8); x->name[8] = 0; x->w = (int)t[8]; x->h = (int)t[9]; x->flags = t[3]; x->bits = bits; x->gpu = -1;
    x->holes = 0; if (bits) { int n = x->w * x->h, j; for (j = 0; j < n; j++) if (!bits[j]) { x->holes = 1; break; } }
    return x;
}

static int RX_Load(RxAssets *a, const char *path)
{
    unsigned hdr[16], ch[2]; FILE *f = fopen(path, "rb"); long at; size_t total = 0, cap = 0; int i;
    memset(a, 0, sizeof *a);
    for (i = 0; i < 64; i++) a->global[i] = -1;
    if (!f) return 0;
    if (fread(hdr, 4, 16, f) != 16 || hdr[0] != 0x31585252 || hdr[1] < 2) { fclose(f); return 0; }
    at = (long)(64 + (size_t)hdr[3] * hdr[2] + hdr[10] * 4);
    fseek(f, 0, SEEK_END); cap = (size_t)ftell(f) - (size_t)at; fseek(f, at, SEEK_SET);
    a->file = (unsigned char *)malloc(cap + 16);
    while (fread(ch, 4, 2, f) == 2 && ch[0] != 0x20444e45) {
        unsigned char *p = a->file + total; unsigned *w = (unsigned *)p;
        if (total + ch[1] > cap || fread(p, 1, ch[1], f) != ch[1]) break;
        total += (ch[1] + 3) & ~3u;
        if (ch[0] == 0x52554350) { a->pcur = (unsigned (*)[6])p; a->npiece = (int)(ch[1] / 24); }
        else if (ch[0] == 0x54504f43 && ch[1] >= 8) memcpy(a->opt, p, ch[1] < sizeof a->opt ? ch[1] : sizeof a->opt);
        else if (ch[0] == 0x54584543 && !a->ext && (a->ext = (RrcExtra *)malloc(ch[1] + 1)) != NULL) { memcpy(a->ext, p, ch[1]); a->next = (int)(ch[1] / sizeof(RrcExtra)); }
        else if (ch[0] == 0x4b52414d && a->nmark < 256) { RxMarks *m = &a->mark[a->nmark++]; m->img = w[0]; m->count = (int)w[3]; m->rec = p + 4 + 0x18; }
        else if (ch[0] == 0x204c4150) memcpy(a->pal, p, 1024);
        else if (ch[0] == 0x544f4c53) memcpy(a->slot0, p, sizeof a->slot0);
        else if (ch[0] == 0x47584554 && w[0] < 64) {   /* TEXG { index, object, name[2], w, h, flags, levels, average } + bits */
            unsigned t[16]; RxTex *x; memset(t, 0, sizeof t); t[4] = w[2]; t[5] = w[3]; t[8] = w[4]; t[9] = w[5]; t[3] = w[6];
            x = RX_AddTex(a, w[1], t, p + 36);
            if (x) a->global[w[0]] = (int)(x - a->tex);
        }
        else if (ch[0] == 0x20544553 && a->nset < 256) { RxSet *s = &a->set[a->nset++]; s->id = w[0]; s->base = w[1]; s->size = w[2]; s->buf = p + 12; }
    }
    fclose(f);
    {   /* g_targetPalette holds the palette at half intensity (the game sets it doubled: live entry 100 = 48,96,0, stored 24,48,0) */
        unsigned mx = 0;
        for (i = 0; i < 256; i++) { unsigned c = a->pal[i]; if ((c & 0xff) > mx) mx = c & 0xff; if (((c >> 8) & 0xff) > mx) mx = (c >> 8) & 0xff; if (((c >> 16) & 0xff) > mx) mx = (c >> 16) & 0xff; }
        if (mx < 128) for (i = 0; i < 256; i++) a->pal[i] = (a->pal[i] & 0xff000000u) | ((a->pal[i] & 0x7f7f7fu) << 1);
    }
    if (a->pcur) RX_Bind(a);
    return a->pcur != NULL;
}

/* ---- sprite sets ---- */
static const unsigned *RX_W(const RxSet *s, unsigned addr, unsigned n) { return (s && addr >= s->base && addr + n <= s->base + s->size) ? (const unsigned *)(s->buf + (addr - s->base)) : NULL; }
static RxSet *RX_SetById(RxAssets *a, unsigned id) { int i; for (i = 0; i < a->nset; i++) if (a->set[i].id == id) return &a->set[i]; return NULL; }

/* which set each slot holds for each piece and section source (src 0 = half A, 1 = half B), in course order: in its block's marker table (sorted by
   position) the last range of the slot opened (state 1) at or before the piece; none in this block yet = what the slot held for the piece before (the
   game keeps a set in its slot until another one is loaded there) */
static void RX_Bind(RxAssets *a)
{
    int k, src, slot, i;
    a->bind = (unsigned short (*)[2][4])calloc((size_t)a->npiece + 1, sizeof *a->bind);
    for (k = 0; k < a->npiece; k++) for (src = 0; src < 2; src++) {
        const RxMarks *m = NULL; int w = (int)a->pcur[k][src * 3 + 2];
        for (i = 0; i < a->nmark; i++) if (a->mark[i].img == a->pcur[k][src * 3 + 1]) { m = &a->mark[i]; break; }
        for (slot = 0; slot < 4; slot++) {
            unsigned id = k ? a->bind[k - 1][src][slot] : a->slot0[slot];
            if (m) for (i = 0; i < m->count; i++) {
                const unsigned char *r = m->rec + i * 12;
                if (r[5] == slot && r[4] == 1 && *(const int *)r <= w) id = *(const unsigned *)(r + 8) & 0xffff;
            }
            a->bind[k][src][slot] = (unsigned short)id;
        }
    }
}
static RxSet *RX_SetFor(RxAssets *a, int k, int src, int slot)
{
    if (!a->bind || k < 0 || k >= a->npiece) return NULL;
    return a->bind[k][src & 1][slot & 3] ? RX_SetById(a, a->bind[k][src & 1][slot & 3]) : NULL;
}

/* follow 'PGLC' group / ' BCC' object to a texture object; returns its address (0 = none) */
static unsigned RX_Leaf(const RxSet *s, unsigned p)
{
    int guard;
    for (guard = 0; guard < 6; guard++) {
        const unsigned *w = RX_W(s, p, 0x40);
        if (!w) return 0;
        if (w[0] == 0xbc614e) return p;
        if (w[0] == 0x434c4750) {
            const unsigned char *b = (const unsigned char *)w;
            if ((b[0x14] & 3) == 0) { const unsigned *l = RX_W(s, w[b[0x15] + b[0x19]], 4u * (b[0x18] + 1)); p = l ? l[b[0x18]] : 0; }   /* SlotRecordCopy */
            else p = w[b[0x15] + b[0x18]];                                                                                         /* ScriptTableEntry, most detailed */
            continue;
        }
        if ((w[0] >> 16) == 0x7fee || (w[0] >> 16) == 0x7f66 || (w[0] >> 16) == 0x7fe6) { p = w[2]; continue; }   /* draw object: [2] = texture */
        return 0;
    }
    return 0;
}

/* set lookup: category cat, entry (code & 0x3f), then nested rows (rows[] of n): the entry ('PGLC' group / draw object), 0 = none */
static unsigned RX_Entry(const RxSet *s, int cat, unsigned code, const int *rows, int n)
{
    const unsigned *root = RX_W(s, s ? s->base : 0, 8), *t; unsigned p; int i;
    if (!root || cat < 0 || (unsigned)cat >= root[0]) return 0;
    p = root[1 + cat];
    if (!(t = RX_W(s, p - 4, 4)) || (code & 0x3f) >= t[0]) return 0;
    p = RX_W(s, p, 4 * ((code & 0x3f) + 1))[code & 0x3f];
    for (i = 0; i < n; i++) {
        if (!(t = RX_W(s, p - 4, 4)) || (unsigned)rows[i] >= t[0]) return 0;
        p = RX_W(s, p, 4u * (rows[i] + 1))[rows[i]];
    }
    return p;
}
static unsigned RX_Lookup(const RxSet *s, int cat, unsigned code, const int *rows, int n) { return RX_Leaf(s, RX_Entry(s, cat, code, rows, n)); }

/* facade height of a roadside code (kind 0): the lane-table fill WheelLaneEntriesFill writes group byte 0x16 * 16 to section +0xf4 / +0xfc (<= 16 = no facade) */
static int RX_FacadeHeight(RxAssets *a, const RoadSection *sec, int k, int h, int side)
{
    unsigned code = sec->sideTex[side]; RxSet *s; const unsigned char *g;
    if (code == 0xff) return 0;
    s = RX_SetFor(a, k, h, (int)(code >> 6) & 3);
    g = (const unsigned char *)RX_W(s, RX_Entry(s, 1, code, NULL, 0), 0x20);
    if (!g || *(const unsigned *)g != 0x434c4750 || g[0x16] * 16 <= 0x10) return 0;
    return g[0x16] * 16;
}

/* texture record of a texture object in a set (added on first use); -1 = none */
static int RX_TexIn(RxAssets *a, const RxSet *s, unsigned addr)
{
    const unsigned *t = RX_W(s, addr, 0x40); const unsigned char *bits; RxTex *x;
    if (!t || t[0] != 0xbc614e || t[8] - 1 >= 2048 || t[9] - 1 >= 2048) return -1;
    if (!RX_W(s, t[0xe], t[8] * t[9])) return -1;
    bits = (const unsigned char *)RX_W(s, t[0xe], t[8] * t[9]);
    x = RX_AddTex(a, addr, t, bits);
    return x ? (int)(x - a->tex) : -1;
}

/* roadside texture of piece k's half h (0 / 1), side (0 left / 1 right): what = 0 facade (category 1, sideTex), 1 sidewalk top, 2 curb face (category 5,
   sideArg rows 0 / 1); -1 = none */
static int RX_SideTex(RxAssets *a, const RoadSection *sec, int k, int h, int side, int what)
{
    unsigned code = what == 0 ? sec->sideTex[side] : sec->sideArg[side]; RxSet *s; unsigned t; int row = what - 1;
    if (code == 0xff) return -1;
    s = RX_SetFor(a, k, h, (int)(code >> 6) & 3);
    t = what == 0 ? RX_Lookup(s, 1, code, NULL, 0) : RX_Lookup(s, 5, code, &row, 1);
    return t ? RX_TexIn(a, s, t) : -1;
}

/* a roadside sprite (rr_ScenerySpriteDraw): kind 1 = RoadSection.sprite[i]: category 4, entry code, row 0; kind 0 = the object row: category 0, entry rowCode.
   The entry is a 'NARR' record (relocated by ModelBlobFix: frames of 7 words from +0x1c, frame count at (word [2]) + 0x10); frame 2
   (SoundParamsCopy(rec, 2)): [0] draw object, [1] texture, [2] -> anchor { x, y } in texels = the foot point.  Returns the texture (-1) and the anchor */
static int RX_NarrTex(RxAssets *a, int k, int h, unsigned code, int cat, int nrow, int *ax, int *ay)
{
    unsigned rec; RxSet *s; const unsigned *n, *fr, *first, *anc; int row = 0, frame = 2, cnt;
    s = RX_SetFor(a, k, h, (int)(code >> 6) & 3);
    rec = RX_Entry(s, cat, code, &row, nrow);
    if (!(n = RX_W(s, rec, 0x1c + 3 * 0x1c)) || n[0] != 0x5252414e) return -1;
    first = RX_W(s, n[2], 0x14);
    cnt = first ? (int)first[4] : 0;
    if (frame >= cnt) frame = cnt - 1;
    if (frame < 0 || !(fr = RX_W(s, rec + 0x1c + frame * 0x1c, 0x1c))) return -1;
    anc = RX_W(s, fr[2], 8);
    *ax = anc ? (int)anc[0] : 0; *ay = anc ? (int)anc[1] : 0;
    { int t = RX_TexIn(a, s, fr[1]); if (t >= 0) a->tex[t].key = 1; return t; }
}

/* side structure of a kindArg[0] == 5 piece (GroundMarkQueue: walls / fences / tree rows parallel to the road): set category 6, entry kindArg[1]; height
   from the group ((1 << byte 0x1b) << byte 0x19) * 4.  Returns the texture (-1) and the height */
static int RX_SideStructTex(RxAssets *a, const RoadSection *sec, int k, int h, int *height)
{
    unsigned code = sec->kindArg[1], e; RxSet *s; const unsigned char *g; unsigned t;
    *height = 0;
    s = RX_SetFor(a, k, h, (int)(code >> 6) & 3);
    e = RX_Entry(s, 6, code, NULL, 0);
    if (!(g = (const unsigned char *)RX_W(s, e, 0x20)) || *(const unsigned *)g != 0x434c4750) return -1;
    *height = ((1 << (g[0x1b] & 0x1f)) << (g[0x19] & 0x1f)) * 4;
    t = RX_Leaf(s, e);
    {   int x = t ? RX_TexIn(a, s, t) : -1;
        if (x >= 0) a->tex[x].key = 1;   /* guard rails / fences have see-through parts (index 0) */
        return x; }
}

/* the shadow under the roadside sprites (the game's sprites have a dark patch on the ground): an ellipse in the palette's darkest colour, 55 % opaque,
   made here once (name "SHADOW") */
static int RX_ShadowTex(RxAssets *a)
{
    static unsigned char bits[64 * 32]; static const unsigned t[16] = { 0xbc614e, 0, 0, 0x1000, 0x44414853, 0x00574f, 0, 0, 64, 32 };
    int i, dark = 1, best = 1 << 30, x, y; RxTex *x0;
    for (i = 0; i < a->ntex; i++) if (!memcmp(a->tex[i].name, "SHADOW", 6)) return i;
    for (i = 1; i < 256; i++) { unsigned c = a->pal[i]; int s = (int)(c & 0xff) + (int)((c >> 8) & 0xff) + (int)((c >> 16) & 0xff); if (s < best) { best = s; dark = i; } }
    for (y = 0; y < 32; y++) for (x = 0; x < 64; x++) { double dx = (x - 31.5) / 32.0, dy = (y - 15.5) / 16.0; bits[y * 64 + x] = (dx * dx + dy * dy <= 1.0) ? (unsigned char)dark : 0; }
    x0 = RX_AddTex(a, 0xffff0001u, t, bits);
    if (!x0) return -1;
    x0->key = 1; x0->alpha = 140;
    return (int)(x0 - a->tex);
}

/* RGBA (ABGR words) of a texture through the palette; colour-key textures: index 0 transparent */
/* a ground picture's ragged edge (user, 2026-11-18: "any terrain that touches up against another terrain should dynamically just mesh in"): from
   W x H RGBA px - dir 0: a quarter of its width, dir 2..5: 1..4 widths - its columns from the outer edge inwards, mirrored on (a reflection at the boundary) -
   4 copies down (one piece along) - opaque at column 0 (the boundary), torn away towards the far side; dir 1: all of it, opaque at the bottom row (the boundary along the road), torn
   away upwards.  The tear wavers along the boundary in whole periods (it repeats as the picture does), in broad tongues with a little fraying, and
   falls at the same place at any resolution (the HD picture or the original) */
#define RX_EDGEIN(W) ((W) / 10)   /* (across: reflected this many columns in from the outer edge - its last columns are often a seam line; course3d.h
                                         C3_Blend starts the edge as far into the inner strip) */
static unsigned *RX_EdgeCut(const unsigned *px, int W, int H, int dir, int *ow, int *oh)
{
    int w = dir == 1 ? W : dir >= 2 ? W * (dir - 1) : (W / 4 > 1 ? W / 4 : 1), h = dir == 1 ? H : 4 * H, x, y; unsigned *o = (unsigned *)malloc((size_t)w * h * 4);
    if (!o) return NULL;   /* (across: 4 copies down it - one piece along, C3_ALONG - so the tear repeats once a piece, not once a copy) */
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        double t = dir == 1 ? (double)x / W : (double)y / h, s = dir == 1 ? (double)(H - 1 - y) / (H > 1 ? H - 1 : 1) : (double)x / (w > 1 ? w - 1 : 1), n; unsigned c, hq; int sp = W - RX_EDGEIN(W), m = x % (2 * sp), col = dir == 1 ? x : m < sp ? sp - 1 - m : m - sp;   /* (across: its outer columns mirrored at the
                                                                                                    boundary - side pictures run once road -> field (DARK4: tarmac on the road side), so what lies at
                                                                                                    the outer edge is what goes on) */
        c = px[(size_t)(y % H) * W + col];
        hq = (unsigned)(int)(t * 160) * 2654435761u ^ (unsigned)(int)(s * 40) * 2246822519u; hq ^= hq >> 15; hq *= 2654435761u; hq ^= hq >> 13;
        n = 0.5 + 0.6 * (0.21 * sin(6.2831853 * t + 1.3) + 0.13 * sin(6.2831853 * 2 * t + 0.4) + 0.07 * sin(6.2831853 * 5 * t + 2.2) + 0.035 * sin(6.2831853 * 11 * t + 0.9))   /* (0.6: gentler - user, 2026-11-19) */
              + ((hq & 1023) / 1023.0 - 0.5) * 0.04;
        o[(size_t)y * w + x] = (s < 0.05 || (s < n && s < 0.95)) && (c >> 24) ? c | 0xff000000u : c & 0x00ffffffu; }   /* (torn away: its colour kept, no dark fringe when filtered) */
    *ow = w; *oh = h; return o;
}
static unsigned *RX_Rgba(const RxAssets *a, const RxTex *t)
{
    if (t->edgeOf > 0 && t->edgeOf <= a->ntex) {   /* (a ragged edge: cut from its picture) */
        const RxTex *b = &a->tex[t->edgeOf - 1]; unsigned *bp = RX_Rgba(a, b), *e; int w, h;
        if (!bp) return NULL;
        e = RX_EdgeCut(bp, b->w, b->h, t->edgeDir, &w, &h); free(bp); return e; }
    {
    unsigned *o = (unsigned *)malloc((size_t)t->w * t->h * 4); int i, key = (t->flags & 0x1000) != 0 || t->key;
    if (!o) return NULL;
    if (t->gen) { memcpy(o, t->gen, (size_t)t->w * t->h * 4); return o; }
    if (!t->bits) { memset(o, 0, (size_t)t->w * t->h * 4); return o; }
    for (i = 0; i < t->w * t->h; i++) {
        unsigned c = a->pal[t->bits[i]];
        o[i] = (key && t->bits[i] == 0) ? 0 : ((unsigned)(t->alpha ? t->alpha : 255) << 24 | (c & 0xffffff));   /* PALETTEENTRY r, g, b = bytes 0, 1, 2 = RGBA in memory */
    }
    return o;
    }
}
#endif
