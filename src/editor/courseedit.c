/* editor\courseedit.c - the Course Editor (user, 2026-11-16: "make a new application please that lets me open the course tracks and edit them ... every
 * feature editable").  A Windows program of its own (scripts\build_editor.ps1 -> analysis\build\editor\CourseEdit.exe), not part of the game.
 *
 * What it edits: a course = one track at one level, as the game's road generator builds it - one record per road segment (road\road_types.h RoadPiece,
 * both carriageways' RoadSection).  It opens the course's extract (world3d\t<track>_l<level>_1r.rrx; made with the game's own extractor, the original
 * course, when it is not there yet), lays an edited course over it (mods\courses\t<track>_l<level>.rrc, road\course_edit.h) and saves edits there: the
 * game puts them into the road as it generates it (road\course_edit.c), so riders, traffic, Pseudo 3D and True 3D all race the edited course.
 * The course keeps the original's length and its split roads where they are; every field of every piece is editable.
 *
 * Window: the pieces (left; several can be chosen), their fields (right: value, original, what it shows), the plan and the height profile (bottom:
 * click to choose a piece), the course in 3D as True 3D draws it (middle: drag to look round, wheel = distance, click = choose).  A field: double-click
 * it for a picker (thumbnails, the kinds, the flag bits, a slider), Enter to type a value, "+=n" to add n, or "a..b" to ramp from a to b over the chosen
 * pieces.  "Game renderer" (F6 = again with the edits as they are now): the game itself draws the course in the 3D panel - the race started with the
 * edits, saved or not (RR_COURSEEDITFILE), held still, with the editor's camera (src\platform\viewer_api.h).  Ctrl+Z / Ctrl+Y undo / redo, Ctrl+S save, Ctrl+C / Ctrl+V a piece's
 * sections onto the chosen pieces, F5 test drive (the game straight into a race on the saved course).
 *
 *   CourseEdit.exe [-data <the game's data folder>] [-game <OpenRash.exe>] [-track 1..5] [-level 1..5] [-piece <n>]   (tests: [-pick <field>] [-gamerenderer 1] [-testclick x y])
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL3/SDL.h>

static FILE *g_log;
#define LOG(...) do { if (g_log) { fprintf(g_log, __VA_ARGS__); fputc(10, g_log); fflush(g_log); } } while (0)
static int g_pfOn = -1; static double g_pfGpu[3]; static int g_pfGpuN;   /* (render3d.h's GPU timer: off) */
#include "../platform/world3d/course3d.h"
#include "../platform/world3d/render3d.h"
#include "../platform/world3d/course_marks.h"
#include "../road/course_edit.h"
#include "../platform/viewer_api.h"

#define CE_L   (2.07 * 320.0)   /* segment length in lateral units (as True 3D: world3d.inc W3_L) */
#define CE_RANGE 200            /* pieces built round the 3D view's piece */

/* ---------------------------------------------------------------- the course */
static C3Course g_c; static RxAssets g_ra; static RoadPiece *g_orig; static int g_piece0, g_track = 1, g_level = 1, g_open, g_dirty;
static char g_dataDir[MAX_PATH], g_gameExe[MAX_PATH], g_status[256];
static int *g_sel, g_nsel, g_focus;   /* the chosen pieces (ascending), the piece the views show */
static int g_half;                    /* the carriageway edited: 0 = A, 1 = B (split roads) */
static C3Course g_gc; static RxAssets g_gra; static int g_haveGame, g_gameView;   /* the course the game built from the edits (the game's renderer shows it; G marks) */
static unsigned char *g_gameDiff;   /* per piece: 1 = the game generated it differently from the editor's values (marked "G" in the list) */
static C3Course *g_vc = &g_c; static RxAssets *g_vra = &g_ra; static int g_vslot;  /* the course the 3D view shows: 0 = the edited pieces, 1 = the game's */
static void TexReset(int slot);   /* (below) */

static const unsigned char g_atan[264] = { 0,0,1,2,2,3,4,4,5,6,6,7,7,8,9,9,10,11,11,12,13,13,14,14,15,16,16,17,18,18,19,19,20,21,21,22,23,23,24,24,
    25,26,26,27,28,28,29,29,30,31,31,32,32,33,34,34,35,36,36,37,37,38,39,39,40,40,41,42,42,43,43,44,44,45,46,46,47,47,48,49,
    49,50,50,51,51,52,53,53,54,54,55,55,56,57,57,58,58,59,59,60,60,61,62,62,63,63,64,64,65,65,66,66,67,68,68,69,69,70,70,71,
    71,72,72,73,73,74,74,75,75,76,76,77,77,78,78,79,79,80,80,81,81,82,82,83,83,84,84,85,85,86,86,87,87,88,88,88,89,89,90,90,
    91,91,92,92,93,93,93,94,94,95,95,96,96,97,97,97,98,98,99,99,100,100,100,101,101,102,102,103,103,103,104,104,105,105,105,106,106,107,107,107,
    108,108,109,109,109,110,110,111,111,111,112,112,112,113,113,114,114,114,115,115,115,116,116,116,117,117,118,118,118,119,119,119,120,120,120,121,121,121,122,122,
    122,123,123,123,124,124,124,125,125,125,126,126,126,127,127,127,128 };
static int R88(int x) { if (x < 0) return -R88(-x); if (x < 0x101) return g_atan[x]; if (x < 0x10000) return 0x100 - R88((int)(0x10000 / (long long)x)); return 0x100; }   /* (game: system\fixmath.c rr_Reciprocal88) */

static RoadSection *Sec(int k, int half) { RoadPiece *p = &g_c.piece[k]; return (half && p->layout == 2) ? &p->halfB : &p->halfA; }
static RoadSection *OSec(int k, int half) { RoadPiece *p = &g_orig[k]; return (half && p->layout == 2) ? &p->halfB : &p->halfA; }

/* ---------------------------------------------------------------- the fields */
typedef struct Fld { const char *name; int off; char type; const char *help; } Fld;   /* type: 'i' int, 'b' unsigned byte */
static const Fld g_fld[] = {
    { "kind", 0x3c, 'b', "0 open road (verges, kerbs), 1 / 2 walls or cliffs, 3 walls + tunnel arch, 4 terrain profile, 5 side structures" },
    { "kind flags", 0x37, 'b', "8 / 0x10 banked verge left / right, 0x40 forces a tunnel, 0x80 a pit / line object" },
    { "kind arg 0", 0x38, 'b', "kind stream argument 0 (7 = a marker)" }, { "kind arg 1", 0x39, 'b', "kind stream argument 1 (tunnel lane-line style)" },
    { "kind arg 2", 0x3a, 'b', "kind stream argument 2 (side structure offset)" }, { "kind arg 3", 0x3b, 'b', "kind stream argument 3 (side structure sides)" },
    { "curve", 0xb8, 'i', "change of direction per segment (+ right, - left)" },
    { "hill", 0xbc, 'i', "change of height per segment (+ up, - down)" },
    { "lanes right", 0x2c, 'i', "lanes right of the centre line, 256 = one lane (edges follow)" },
    { "lanes left", 0x30, 'i', "lanes left of the centre line, 256 = one lane (edges follow)" },
    { "lane offset", 0xcc, 'i', "lateral offset of the lane grid (edges follow)" },
    { "road width", 0xc4, 'i', "lane edge right - lane edge left (follows the lanes)" },
    { "lane edge left", 0xe0, 'i', "lane-line edge left" }, { "lane edge right", 0xe4, 'i', "lane-line edge right" },
    { "road edge left", 0xd8, 'i', "edge of the road surface left" }, { "road edge right", 0xdc, 'i', "edge of the road surface right" },
    { "extent left", 0xd0, 'i', "outer lane extent left" }, { "extent right", 0xd4, 'i', "outer lane extent right" },
    { "verge width left", 0x138, 'i', "verge / sidewalk width left (outer edge follows)" }, { "verge width right", 0x13c, 'i', "verge / sidewalk width right" },
    { "verge outer left", 0xe8, 'i', "outer edge of the verge left" }, { "verge outer right", 0xec, 'i', "outer edge of the verge right" },
    { "kerb height left", 0x140, 'i', "verge / sidewalk height left" }, { "kerb height right", 0x144, 'i', "verge / sidewalk height right" },
    { "side texture left", 0x148, 'b', "roadside sprite group left (255 = none)" }, { "side texture right", 0x149, 'b', "roadside sprite group right" },
    { "side arg left", 0x14a, 'b', "roadside group argument left" }, { "side arg right", 0x14b, 'b', "roadside group argument right" },
    { "wall left x0", 0x14c, 'i', "wall / cliff left: foot (lateral)" }, { "wall left x1", 0x150, 'i', "wall left: top (lateral)" }, { "wall left height", 0x154, 'i', "wall left height (- = a drop: the coast)" },
    { "wall right x0", 0x158, 'i', "wall right: foot" }, { "wall right x1", 0x15c, 'i', "wall right: top" }, { "wall right height", 0x160, 'i', "wall right height (- = a drop)" },
    { "profile L0 x", 0xf0, 'i', "terrain point 1 left: lateral (kind 4)" }, { "profile L0 h", 0xf4, 'i', "terrain point 1 left: height" },
    { "profile R0 x", 0xf8, 'i', "terrain point 1 right: lateral" }, { "profile R0 h", 0xfc, 'i', "terrain point 1 right: height" },
    { "profile L1 x", 0x100, 'i', "terrain point 2 left" }, { "profile L1 h", 0x104, 'i', "" }, { "profile R1 x", 0x108, 'i', "terrain point 2 right" }, { "profile R1 h", 0x10c, 'i', "" },
    { "profile L2 x", 0x110, 'i', "terrain point 3 left" }, { "profile L2 h", 0x114, 'i', "" }, { "profile R2 x", 0x118, 'i', "terrain point 3 right" }, { "profile R2 h", 0x11c, 'i', "" },
    { "sprites", 0x3d, 'b', "roadside sprites on this piece (0..2)" },
    { "sprite 1 set code", 0x40, 'b', "sprite 1: set code (slot * 64 + entry)" }, { "sprite 1 flags", 0x41, 'b', "1 mirrored, 2 right of the centre" }, { "sprite 1 type", 0x42, 'b', "draw pass" },
    { "sprite 1 x", 0x48, 'i', "sprite 1 lateral position" }, { "sprite 1 ground", 0x4c, 'i', "sprite 1 ground height" },
    { "sprite 2 set code", 0x50, 'b', "sprite 2: set code" }, { "sprite 2 flags", 0x51, 'b', "" }, { "sprite 2 type", 0x52, 'b', "" },
    { "sprite 2 x", 0x58, 'i', "sprite 2 lateral position" }, { "sprite 2 ground", 0x5c, 'i', "" },
    { "object row count", 0x60, 'b', "roadside objects in the row (0..8: lamp posts, trees, ...)" }, { "object row scale", 0x62, 'b', "world units per texel x 4" },
    { "object row code", 0x63, 'b', "the objects' set code" }, { "object row flags", 0x64, 'b', "1 mirrored, 2 right side" },
    { "object 1 x", 0x6c, 'i', "lateral position (between the lane edges: an obstacle on the road)" }, { "object 2 x", 0x70, 'i', "" }, { "object 3 x", 0x74, 'i', "" }, { "object 4 x", 0x78, 'i', "" },
    { "object 5 x", 0x7c, 'i', "" }, { "object 6 x", 0x80, 'i', "" }, { "object 7 x", 0x84, 'i', "" }, { "object 8 x", 0x88, 'i', "" },
    { "object 1 ground", 0x8c, 'i', "ground height under it" }, { "object 2 ground", 0x90, 'i', "" }, { "object 3 ground", 0x94, 'i', "" }, { "object 4 ground", 0x98, 'i', "" },
    { "object 5 ground", 0x9c, 'i', "" }, { "object 6 ground", 0xa0, 'i', "" }, { "object 7 ground", 0xa4, 'i', "" }, { "object 8 ground", 0xa8, 'i', "" },
    { "depth term", 0xc0, 'i', "wall projection term" },
};
#define NFLD ((int)(sizeof g_fld / sizeof g_fld[0]))
static int FGet(const RoadSection *s, const Fld *f) { const unsigned char *b = (const unsigned char *)s + f->off; return f->type == 'b' ? *b : *(const int *)b; }
static void FSet(RoadSection *s, const Fld *f, int v) { unsigned char *b = (unsigned char *)s + f->off; if (f->type == 'b') *b = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); else *(int *)b = v; }
static const char *KindName(int k) { static const char *const n[6] = { "open", "walls", "walls 2", "tunnel", "terrain", "side" }; return k >= 0 && k < 6 ? n[k] : "?"; }

/* the values the generator works out from others (road\road_build.c rr_RoadSectionFill), after field off changed */
static int D256(int x) { return x / 256; }
static void Derive(RoadSection *s, int off)
{
    if (off == 0xb8) s->curveRaw = (int)((unsigned)R88(s->curve) << 14);
    if (off == 0xbc) s->hillRaw = (int)((unsigned)R88(s->hill) << 14);
    if (off == 0x2c || off == 0x30 || off == 0xcc) {
        s->lanesR = (unsigned char)((unsigned)(s->laneR + 0xff) >> 8); s->lanesL = (unsigned char)((unsigned)(s->laneL + 0xff) >> 8);
        s->extL = s->offset + D256(-0xff - s->laneL) * 0xfa; s->extR = s->offset + D256(s->laneR + 0xff) * 0xfa;
        s->edgeL = s->extL; s->edgeR = s->extR;
        s->laneEdgeL = s->offset + D256(s->laneL * -0xfa); s->laneEdgeR = s->offset + D256(s->laneR * 0xfa);
        s->width = s->laneEdgeR - s->laneEdgeL; off = 0x138; }
    if (off == 0x138 || off == 0x13c || off == 0xe0 || off == 0xe4) { s->outerL = s->laneEdgeL - s->shoulderW[0]; s->outerR = s->laneEdgeR + s->shoulderW[1]; s->width = s->laneEdgeR - s->laneEdgeL; }
}

/* ---------------------------------------------------------------- undo */
typedef struct Undo { int n; int *idx; RoadPiece *before, *after; } Undo;
static Undo g_undo[100]; static int g_nundo, g_nredo;
static Undo *g_pkUndo;   /* a picker's slider being dragged: one undo step for the whole drag (ended by PickLiveEnd or any other change) */
static void UndoEnd(Undo *u);
static void PickLiveEnd0(void) { if (g_pkUndo) { Undo *u = g_pkUndo; g_pkUndo = NULL; UndoEnd(u); } }
static void UndoFree(Undo *u) { free(u->idx); free(u->before); free(u->after); memset(u, 0, sizeof *u); }
static Undo *UndoBegin(const int *idx, int n)
{   Undo *u; int i;
    PickLiveEnd0();
    for (i = g_nundo; i < g_nundo + g_nredo && i < 100; i++) UndoFree(&g_undo[i]); g_nredo = 0;
    if (g_nundo == 100) { UndoFree(&g_undo[0]); memmove(g_undo, g_undo + 1, sizeof(Undo) * 99); g_nundo--; }
    u = &g_undo[g_nundo++]; u->n = n; u->idx = (int *)malloc(sizeof(int) * n); u->before = (RoadPiece *)malloc(sizeof(RoadPiece) * n); u->after = (RoadPiece *)malloc(sizeof(RoadPiece) * n);
    for (i = 0; i < n; i++) { u->idx[i] = idx[i]; u->before[i] = g_c.piece[idx[i]]; }
    return u; }
static void UndoEnd(Undo *u) { int i; for (i = 0; i < u->n; i++) u->after[i] = g_c.piece[u->idx[i]]; g_dirty = 1; }
static void UndoStep(int redo)
{   Undo *u; int i;
    PickLiveEnd0();
    if (redo ? !g_nredo : !g_nundo) return;
    if (redo) { u = &g_undo[g_nundo]; g_nundo++; g_nredo--; } else { u = &g_undo[--g_nundo]; g_nredo++; }
    for (i = 0; i < u->n; i++) g_c.piece[u->idx[i]] = redo ? u->after[i] : u->before[i];
    g_dirty = 1; }

/* ---------------------------------------------------------------- files */
static void Status(const char *fmt, ...);
static HANDLE g_runKeep;   /* RunAndWait with wait 2: the started process (not waited for, kept) */
static int RunAndWait(const char *exe, const char *envAdd, int wait)   /* the game with these RR_ settings ("A=1\0B=2\0"), working folder the data folder; wait 2 = keep it in g_runKeep */
{   char *src = GetEnvironmentStringsA(), *p, *env, *o; size_t n = 0; STARTUPINFOA si; PROCESS_INFORMATION pi; DWORD code = 1; const char *q;
    for (p = src; *p; p += strlen(p) + 1) n += strlen(p) + 1;
    env = (char *)malloc(n + 4096); o = env;
    for (p = src; *p; p += strlen(p) + 1) if (!((p[0] == 'R' || p[0] == 'r') && (p[1] == 'R' || p[1] == 'r') && p[2] == '_')) { strcpy(o, p); o += strlen(p) + 1; }
    FreeEnvironmentStringsA(src);
    for (q = envAdd; *q; q += strlen(q) + 1) { strcpy(o, q); o += strlen(q) + 1; }
    *o = 0;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessA(exe, NULL, NULL, NULL, FALSE, wait == 1 ? CREATE_NO_WINDOW : 0, env, g_dataDir, &si, &pi)) { free(env); return -1; }
    free(env);
    if (wait == 1) { WaitForSingleObject(pi.hProcess, 180000); GetExitCodeProcess(pi.hProcess, &code); }
    CloseHandle(pi.hThread); if (wait == 2) g_runKeep = pi.hProcess; else CloseHandle(pi.hProcess);
    return wait == 1 ? (int)code : 0; }
static void RrcPath(char *out) { _snprintf(out, MAX_PATH - 1, "%s\\mods\\courses\\t%d_l%d.rrc", g_dataDir, g_track, g_level); out[MAX_PATH - 1] = 0; }
static int LoadRrc(void)   /* the saved edits over the original pieces: how many pieces */
{   char path[MAX_PATH]; FILE *f; RrcHeader h; RrcPiece *r; int i, n = 0;
    RrcPath(path); if (!(f = fopen(path, "rb"))) return 0;
    if (fread(&h, sizeof h, 1, f) == 1 && h.magic == RRC_MAGIC && h.version == RRC_VERSION && h.secSize == RRC_SECSIZE && h.n > 0 && h.n < 65536 && (r = (RrcPiece *)malloc(sizeof(RrcPiece) * h.n)) != NULL) {
        if (fread(r, sizeof(RrcPiece), h.n, f) == (size_t)h.n)
            for (i = 0; i < h.n; i++) { int k = (h.piece0 + i * 0x100 - g_piece0) >> 8; if (k < 0 || k >= g_c.n) continue;
                if (r[i].used & 1) memcpy((unsigned char *)&g_c.piece[k].halfA + RRC_FROM, r[i].a + RRC_FROM, RRC_SECSIZE - RRC_FROM);
                if (r[i].used & 2) memcpy((unsigned char *)&g_c.piece[k].halfB + RRC_FROM, r[i].b + RRC_FROM, RRC_SECSIZE - RRC_FROM);
                n++; }
        free(r); }
    fclose(f); return n; }
static int SaveRrcTo(const char *path)
{   FILE *f; RrcHeader h; RrcPiece *r; int i;
    if (!(r = (RrcPiece *)calloc(g_c.n, sizeof(RrcPiece)))) return 0;
    for (i = 0; i < g_c.n; i++) {   /* only the changed halves: the game adjusts some fields after the copy, so an unchanged piece is left to the game */
        r[i].used = (memcmp((unsigned char *)&g_c.piece[i].halfA + RRC_FROM, (unsigned char *)&g_orig[i].halfA + RRC_FROM, RRC_SECSIZE - RRC_FROM) ? 1 : 0)
                  | (g_c.piece[i].layout == 2 && memcmp((unsigned char *)&g_c.piece[i].halfB + RRC_FROM, (unsigned char *)&g_orig[i].halfB + RRC_FROM, RRC_SECSIZE - RRC_FROM) ? 2 : 0);
        memcpy(r[i].a, &g_c.piece[i].halfA, RRC_SECSIZE); memcpy(r[i].b, &g_c.piece[i].halfB, RRC_SECSIZE); }
    memset(&h, 0, sizeof h); h.magic = RRC_MAGIC; h.version = RRC_VERSION; h.track = g_track; h.level = g_level; h.n = g_c.n; h.piece0 = g_piece0; h.secSize = RRC_SECSIZE;
    h.checksum = RrcHash(r, (unsigned)(sizeof(RrcPiece) * g_c.n));
    if (!(f = fopen(path, "wb"))) { free(r); return 0; }
    fwrite(&h, sizeof h, 1, f); fwrite(r, sizeof(RrcPiece), g_c.n, f); fclose(f); free(r);
    return 1; }
static int SaveRrc(void)
{   char path[MAX_PATH], dir[MAX_PATH];
    _snprintf(dir, sizeof dir - 1, "%s\\mods", g_dataDir); CreateDirectoryA(dir, NULL); strcat(dir, "\\courses"); CreateDirectoryA(dir, NULL);
    RrcPath(path); if (!SaveRrcTo(path)) return 0;
    g_dirty = 0; return 1; }

/* the game's extractor (the course as the game generates it) into rrx: edits = an edited course file for it (RR_COURSEEDITFILE), NULL = the original */
static void Extract(const char *rrx, const char *edits)
{   char env[2048], cfg[MAX_PATH], *o = env; int i;
    _snprintf(cfg, sizeof cfg - 1, "%s\\world3d\\extract.editor.cfg", g_dataDir);
    { char d[MAX_PATH]; _snprintf(d, sizeof d - 1, "%s\\world3d", g_dataDir); CreateDirectoryA(d, NULL); }
    DeleteFileA(rrx);
    o += sprintf(o, "RR_EXTRACT=%s", rrx) + 1; o += sprintf(o, "RR_AUTORACE=%d", g_track) + 1; o += sprintf(o, "RR_LEVEL=%d", g_level) + 1;
    o += sprintf(o, "RR_CONFIG=%s", cfg) + 1;
    if (edits) o += sprintf(o, "RR_COURSEEDITFILE=%s", edits) + 1; else o += sprintf(o, "RR_COURSEEDIT=0") + 1;
    { static const char *const fixed[] = { "RR_HEADLESS=1", "RR_AUDIO=off", "RR_DETERMINISTIC=1", "RR_NOREGIMPORT=1", "RR_ONEROAD=1", "RR_SEED=1" };
      for (i = 0; i < 6; i++) o += sprintf(o, "%s", fixed[i]) + 1; }
    *o = 0;
    RunAndWait(g_gameExe, env, 1); DeleteFileA(cfg);
    { char live[MAX_PATH + 8]; sprintf(live, "%s.live", rrx); DeleteFileA(live); } }
static int g_meshLo = -1, g_meshHi = -1, g_meshDirty = 1, g_texReady;
static void FreeGame(void)
{   free(g_gameDiff); g_gameDiff = NULL; free(g_gc.piece); free(g_gc.frame); free(g_gc.frameB); free(g_gc.v); free(g_gc.vpiece); free(g_gc.bdead); free(g_gc.pairA); free(g_gc.pairB); free(g_gc.bldg); memset(&g_gc, 0, sizeof g_gc);
    free(g_gra.file); free(g_gra.bind); memset(&g_gra, 0, sizeof g_gra); g_haveGame = 0; }   /* (pcur points into file: rrx_assets.h RX_Load) */
static void FreeCourse(void)
{   FreeGame(); free(g_c.piece); free(g_c.frame); free(g_c.frameB); free(g_c.v); free(g_c.vpiece); free(g_c.bdead); free(g_c.pairA); free(g_c.pairB); free(g_c.bldg); memset(&g_c, 0, sizeof g_c);
    free(g_ra.file); free(g_ra.bind); memset(&g_ra, 0, sizeof g_ra);   /* (pcur points into file) */ free(g_orig); g_orig = NULL; g_open = 0; }
static int OpenCourse(int track, int level)
{   char rrx[MAX_PATH], env[1024], cfg[MAX_PATH]; unsigned hdr[16]; FILE *f; int i, edits;
    FreeCourse(); g_track = track; g_level = level;
    _snprintf(rrx, sizeof rrx - 1, "%s\\world3d\\t%d_l%d_1r.rrx", g_dataDir, track, level);
    (void)env; (void)cfg;
    if (GetFileAttributesA(rrx) == INVALID_FILE_ATTRIBUTES) {   /* the original course: the game's extractor (as render\world3d_feed.c CourseFile, but never an edited one) */
        Status("Extracting track %d level %d with the game (the first time only) ...", track, level);
        Extract(rrx, NULL); }
    if (!(f = fopen(rrx, "rb"))) { Status("No course file %s (is -game the OpenRash.exe next to its DLLs?)", rrx); return 0; }
    if (fread(hdr, 4, 16, f) != 16) { fclose(f); return 0; } fclose(f);
    g_piece0 = (int)hdr[5];
    if (!C3_LoadRrx(&g_c, rrx) || !RX_Load(&g_ra, rrx)) { Status("Cannot read %s", rrx); FreeCourse(); return 0; }
    g_ra.rockTex = -1;
    g_c.L = CE_L; C3_Frames(&g_c); C3_FramesB(&g_c); g_c.fr = g_c.frame; C3_OneRoadWidth(&g_c); g_c.forkPick = 1;
    g_orig = (RoadPiece *)malloc(sizeof(RoadPiece) * g_c.n); memcpy(g_orig, g_c.piece, sizeof(RoadPiece) * g_c.n);
    edits = LoadRrc();
    for (i = 0; i < 100; i++) UndoFree(&g_undo[i]); g_nundo = g_nredo = 0;
    g_open = 1; g_dirty = 0; g_meshDirty = 1; g_texReady = 0;
    free(g_sel); g_sel = (int *)malloc(sizeof(int)); g_sel[0] = g_c.start > 0 ? g_c.start : 0; g_nsel = 1; g_focus = g_sel[0];
    Status("Track %d level %d: %d pieces, start %d, finish %d%s", track, level, g_c.n, g_c.start, g_c.finish, edits ? " - the saved edits loaded" : " - the original course");
    return 1; }

/* ---------------------------------------------------------------- windows */
enum { ID_TRACK = 100, ID_LEVEL, ID_OPEN, ID_SAVE, ID_UNDO, ID_REDO, ID_REVSEL, ID_REVALL, ID_COPY, ID_PASTE, ID_DRIVE, ID_HALF, ID_LIST, ID_PROPS, ID_EDIT, ID_STATUS, ID_GAMEVIEW, ID_REGEN, ID_TPL, ID_TSAVE, ID_TPASTE, ID_TDEL, ID_PART, ID_PAINT, ID_BRUSH, ID_BVAL, ID_BPICK, ID_MARKS };
static HWND g_main, g_list, g_props, g_plan, g_prof, g_view, g_statusw, g_edit, g_cbTrack, g_cbLevel, g_half2, g_gameChk; static HFONT g_font; static int g_editRow = -1;
static SDL_Window *g_sw; static SDL_Renderer *g_ren; static SDL_Texture *g_rt; static int g_rtw, g_rth; static R3 g_r; static int g_r3ok;
static int *g_gtexs[2]; static RoadPiece g_clip; static int g_haveClip;
static float g_yaw, g_pitch = 0.18f, g_dist = 2600; static int g_drag, g_dx, g_dy;
static double g_camS = -1, g_camU;   /* the point the 3D camera orbits: road position in pieces (fractions too), lateral offset from the road's middle - WASD
                                       moves it, choosing a piece puts it on that piece again */
static int g_camFor = -1;           /* (the focus g_camS was put on) */
static int g_startPiece = -1, g_testPaint[4] = { -1, 0, 0, 0 };   /* -testpaint <piece> <side 1|2> <value> <brush 0..4>: the brush painted there once the game's renderer shows (tests) */   /* -piece <n>: start on that piece */
static const char *g_pickArg; static int g_startViewer, g_testClick[2] = { -1, -1 };   /* -testclick x y: a click in the 3D panel once the game's renderer shows (tests) */   /* -gamerenderer 1: start with the game's renderer */   /* -pick "<field name>": open that field's picker at the start */
/* ---------------------------------------------------------------- parts of a piece
   What Paste and the templates carry: the whole piece (everything from the lanes on), or a roadside - the left side, the right side, both sides -
   without the road itself (lanes, curve, hill, kind).  A side: its verge (width, outer edge, kerb height, the banked-verge flag), its roadside textures
   (side texture / side arg), its wall or cliff, its terrain profile points, the roadside sprites on it and the object row if it is on that side.  In the
   3D view a click on the road chooses the whole piece, left or right of the road that side (the frame shows the part). */
static int g_part;   /* 0 the whole piece, 1 the left side, 2 the right side, 3 both sides */
static HWND g_cbPart;
static const char *const g_partName[4] = { "the whole piece", "the left side", "the right side", "both sides" };
static const short g_sideI[2][12] = { { 0x138, 0xe8, 0x140, 0x14c, 0x150, 0x154, 0xf0, 0xf4, 0x100, 0x104, 0x110, 0x114 },     /* (int fields: verge, kerb, wall, profile) */
                                      { 0x13c, 0xec, 0x144, 0x158, 0x15c, 0x160, 0xf8, 0xfc, 0x108, 0x10c, 0x118, 0x11c } };
static const short g_sideB[2][2] = { { 0x148, 0x14a }, { 0x149, 0x14b } };   /* (byte fields: side texture, side arg) */
static void SideCopy(unsigned char *d, const unsigned char *s, int side)   /* one roadside of section s onto section d (0 left, 1 right) */
{   int i, n = 0; unsigned char spr[2][16]; int bit = side ? 0x10 : 0x08;
    for (i = 0; i < 12; i++) memcpy(d + g_sideI[side][i], s + g_sideI[side][i], 4);
    for (i = 0; i < 2; i++) d[g_sideB[side][i]] = s[g_sideB[side][i]];
    d[0x37] = (unsigned char)((d[0x37] & ~bit) | (s[0x37] & bit));   /* (kind flags: the banked verge on that side) */
    for (i = 0; i < d[0x3d] && i < 2; i++) if (((d[0x40 + i * 16 + 1] & 2) != 0) != side) memcpy(spr[n++], d + 0x40 + i * 16, 16);   /* sprites: d's on the other side + s's on this one */
    for (i = 0; i < s[0x3d] && i < 2 && n < 2; i++) if (((s[0x40 + i * 16 + 1] & 2) != 0) == side) memcpy(spr[n++], s + 0x40 + i * 16, 16);
    for (i = 0; i < n; i++) memcpy(d + 0x40 + i * 16, spr[i], 16);
    d[0x3d] = (unsigned char)n;
    if (s[0x60] && ((s[0x64] & 2) != 0) == side) memcpy(d + 0x60, s + 0x60, 0xac - 0x60);   /* the object row: s's when it is on this side, */
    else if (d[0x60] && ((d[0x64] & 2) != 0) == side) d[0x60] = 0; }                          /* else d's on this side goes */
static void PartCopy(RoadPiece *dst, const RoadSection *srcA, const RoadSection *srcB, int srcLayout, int part)   /* part of a piece (its sections) onto piece dst (the half being edited) */
{   if (part == 0) {
        memcpy((unsigned char *)&dst->halfA + RRC_FROM, (const unsigned char *)srcA + RRC_FROM, RRC_SECSIZE - RRC_FROM);
        if (dst->layout == 2 && srcLayout == 2) memcpy((unsigned char *)&dst->halfB + RRC_FROM, (const unsigned char *)srcB + RRC_FROM, RRC_SECSIZE - RRC_FROM);
        else if (dst->layout != 2) dst->halfB = dst->halfA;
        return; }
    {   RoadSection *d = g_half && dst->layout == 2 ? &dst->halfB : &dst->halfA; const RoadSection *s = g_half && srcLayout == 2 ? srcB : srcA;
        if (part & 1) SideCopy((unsigned char *)d, (const unsigned char *)s, 0);
        if (part & 2) SideCopy((unsigned char *)d, (const unsigned char *)s, 1);
        if (dst->layout != 2) dst->halfB = dst->halfA; } }
static void PartSet(int part) { g_part = part & 3; if (g_cbPart) SendMessageA(g_cbPart, CB_SETCURSEL, g_part, 0); }
static int g_bulk; static int g_anchor = -1;   /* the piece a choice started at (a range runs from it; the focus) */ static int g_cx, g_cy;   /* (g_cx / g_cy: where a click in the 3D view started) */   /* 1 while SelectPiece sets many list items: their change notices are not each a new choice (SelFromList once at the end) */

static void Status(const char *fmt, ...)
{   va_list a; va_start(a, fmt); _vsnprintf(g_status, sizeof g_status - 1, fmt, a); va_end(a); g_status[sizeof g_status - 1] = 0;
    if (g_statusw) { SendMessageA(g_statusw, SB_SETTEXTA, 0, (LPARAM)g_status); UpdateWindow(g_statusw); } LOG("%s", g_status); }
static void Title(void) { char t[200]; sprintf(t, "Open Rash Course Editor - track %d level %d%s", g_track, g_level, g_dirty ? " *" : ""); SetWindowTextA(g_main, t); }

static void Redraw(void)   /* everything that shows the course */
{   if (g_list) { ListView_SetItemCountEx(g_list, g_open ? g_c.n : 0, LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL); InvalidateRect(g_list, NULL, FALSE); }
    if (g_open) { C3_Frames(&g_c); C3_FramesB(&g_c); C3_OneRoadWidth(&g_c); }
    InvalidateRect(g_plan, NULL, FALSE); InvalidateRect(g_prof, NULL, FALSE); g_meshDirty = 1; Title(); }

static void PickFill(void);   /* (below: the open picker follows the choice) */
static void PropsFill(void)
{   int i, j; char t[64]; LVITEMA it;
    ListView_DeleteAllItems(g_props);
    if (!g_open || !g_nsel) return;
    for (i = 0; i < NFLD; i++) {
        const Fld *f = &g_fld[i]; int v = FGet(Sec(g_sel[0], g_half), f), mixed = 0, o = FGet(OSec(g_sel[0], g_half), f);
        for (j = 1; j < g_nsel && !mixed; j++) if (FGet(Sec(g_sel[j], g_half), f) != v) mixed = 1;
        memset(&it, 0, sizeof it); it.mask = LVIF_TEXT; it.iItem = i; it.pszText = (char *)f->name; ListView_InsertItem(g_props, &it);
        if (mixed) strcpy(t, "(several)"); else sprintf(t, f->off == 0x3c ? "%d  %s" : "%d", v, KindName(v));
        ListView_SetItemText(g_props, i, 1, t);
        sprintf(t, "%d", o); ListView_SetItemText(g_props, i, 2, v != o || mixed ? t : "");
        t[0] = 0;
        {   RoadSection *s = Sec(g_sel[0], g_half); int ax, ay, tx = -1, h = g_half && g_c.piece[g_sel[0]].layout == 2;
            if (f->off == 0x40 || f->off == 0x50) tx = RX_NarrTex(&g_ra, g_sel[0], h, s->sprite[f->off == 0x50].code, 4, 1, &ax, &ay);
            if (f->off == 0x63) tx = RX_NarrTex(&g_ra, g_sel[0], h, s->rowCode, 0, 0, &ax, &ay);
            if (tx >= 0) sprintf(t, "%.8s", g_ra.tex[tx].name); else if (f->help) _snprintf(t, sizeof t - 1, "%s", f->help); t[sizeof t - 1] = 0; }
        ListView_SetItemText(g_props, i, 3, t); }
    PickFill(); }

static void SelFromList(void)
{   int i, n = ListView_GetSelectedCount(g_list), k = -1, m = 0;
    if (!g_open) return;
    free(g_sel); g_sel = (int *)malloc(sizeof(int) * (n > 0 ? n : 1)); g_nsel = 0;
    while ((k = ListView_GetNextItem(g_list, k, LVNI_SELECTED)) >= 0 && m < n) g_sel[m++] = k;
    g_nsel = m; if (!m) { g_sel[0] = g_focus; g_nsel = 1; }
    for (i = 0; i < m && g_sel[i] != g_anchor; i++) ;
    if (i == m) g_anchor = g_sel[0];   /* (a choice made in the list: its first piece) */
    g_focus = g_anchor;   /* (the focus: where the choice started - the camera stays there while a range is dragged out) */
    if (g_focus != g_camFor) { g_camFor = g_focus; g_camS = g_focus + 0.5; g_camU = 0; }   /* (a piece chosen: the camera on it) */
    PropsFill(); InvalidateRect(g_plan, NULL, FALSE); InvalidateRect(g_prof, NULL, FALSE); }
static void SelectPiece(int k, int extend)   /* extend: the range from the anchor (the piece a choice started at) to k */
{   if (!g_open) return; if (k < 0) k = 0; if (k >= g_c.n) k = g_c.n - 1;
    g_bulk = 1;
    if (!extend || g_anchor < 0) { ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED); g_anchor = k; }
    else { int a = g_anchor < k ? g_anchor : k, b = g_anchor < k ? k : g_anchor, i; ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED); for (i = a; i <= b; i++) ListView_SetItemState(g_list, i, LVIS_SELECTED, LVIS_SELECTED); }
    ListView_SetItemState(g_list, k, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); ListView_EnsureVisible(g_list, k, FALSE);
    g_bulk = 0; SelFromList(); }

/* a field of the chosen pieces: "v", "+=d", "a..b" (ramp over the choice) */
static void ApplyField(int fi, const char *txt)
{   const Fld *f = &g_fld[fi]; int i, a, b, add = 0, ramp = 0; const char *dd; Undo *u;
    if (!g_open || !g_nsel || fi < 0 || fi >= NFLD) return;
    while (*txt == ' ') txt++;
    if (txt[0] == '+' && txt[1] == '=') { add = 1; a = atoi(txt + 2); b = a; }
    else if ((dd = strstr(txt, "..")) != NULL) { ramp = 1; a = atoi(txt); b = atoi(dd + 2); }
    else { if (!txt[0] || (txt[0] != '-' && (txt[0] < '0' || txt[0] > '9'))) return; a = b = (int)strtol(txt, NULL, 0); }
    u = UndoBegin(g_sel, g_nsel);
    for (i = 0; i < g_nsel; i++) { RoadSection *s = Sec(g_sel[i], g_half); int v = ramp ? (g_nsel > 1 ? a + (int)((long long)(b - a) * i / (g_nsel - 1)) : a) : add ? FGet(s, f) + a : a;
        FSet(s, f, v); Derive(s, f->off);
        if (g_c.piece[g_sel[i]].layout != 2 && g_half == 0) g_c.piece[g_sel[i]].halfB = g_c.piece[g_sel[i]].halfA; }
    UndoEnd(u); Status("%s set on %d piece%s", f->name, g_nsel, g_nsel == 1 ? "" : "s"); PropsFill(); Redraw(); }
static void RevertSel(void)
{   Undo *u; int i; if (!g_open || !g_nsel) return;
    u = UndoBegin(g_sel, g_nsel); for (i = 0; i < g_nsel; i++) g_c.piece[g_sel[i]] = g_orig[g_sel[i]]; UndoEnd(u);
    Status("%d piece%s back to the original", g_nsel, g_nsel == 1 ? "" : "s"); PropsFill(); Redraw(); }
static void Paste(void)
{   Undo *u; int i; if (!g_open || !g_nsel || !g_haveClip) return;
    u = UndoBegin(g_sel, g_nsel);
    for (i = 0; i < g_nsel; i++) PartCopy(&g_c.piece[g_sel[i]], &g_clip.halfA, &g_clip.halfB, g_clip.layout, g_part);
    UndoEnd(u); Status("%s of the copied piece pasted onto %d piece%s", g_partName[g_part], g_nsel, g_nsel == 1 ? "" : "s"); PropsFill(); Redraw(); }

static void GameCourseLoad(const char *rrx)   /* the course file the game built from the edits (the game's renderer shows it): where it differs from the editor's values -> G */
{   FreeGame();
    if (!C3_LoadRrx(&g_gc, rrx) || !RX_Load(&g_gra, rrx)) { FreeGame(); return; }
    g_gra.rockTex = -1; g_gc.L = CE_L; C3_Frames(&g_gc); C3_FramesB(&g_gc); g_gc.fr = g_gc.frame; C3_OneRoadWidth(&g_gc); g_gc.forkPick = 1;
    g_haveGame = 1;
    {   int k, i, npc = 0, first = -1; char fl[200] = ""; unsigned char fd[sizeof g_fld / sizeof g_fld[0]];   /* where the game's course differs from the editor's */
        memset(fd, 0, sizeof fd); free(g_gameDiff); g_gameDiff = (unsigned char *)calloc(g_c.n + 1, 1);
        for (k = 0; k < g_c.n && k < g_gc.n; k++) { int h, d = 0;
            for (h = 0; h < (g_c.piece[k].layout == 2 ? 2 : 1); h++) { RoadSection *a = h ? &g_c.piece[k].halfB : &g_c.piece[k].halfA, *b = h ? &g_gc.piece[k].halfB : &g_gc.piece[k].halfA;
                for (i = 0; i < NFLD; i++) if (FGet(a, &g_fld[i]) != FGet(b, &g_fld[i])) { fd[i] = 1; d = 1; } }
            if (d) { g_gameDiff[k] = 1; npc++; if (first < 0) first = k; } }
        for (i = 0; i < NFLD && strlen(fl) < 150; i++) if (fd[i]) { if (fl[0]) strcat(fl, ", "); strcat(fl, g_fld[i].name); }
        if (npc) Status("Game renderer: the game's own True 3D.  It worked out %d piece%s itself (first: %d: %s) - marked G.  F6 = again after more edits", npc, npc == 1 ? "" : "s", first, fl);
        else Status("Game renderer: the game's own True 3D (HD textures, sprites, facades, grass, lights).  Click: choose (hold: several), right-drag: look round, wheel: distance, WASD: move.  F6 = again after more edits"); }
    ListView_SetItemCountEx(g_list, g_c.n, LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL); InvalidateRect(g_list, NULL, FALSE); }
/* ---------------------------------------------------------------- painting
   Paint (the toolbar's second row): the brush - a field of the roadside and its value (Pick...: thumbnails / slider) - painted onto the sides of the
   pieces: in the 3D view a grid, a square per piece and side; click / drag over the squares (the road itself is left alone).  A stroke is one undo
   step.  With the game renderer the game takes each change at once. */
typedef struct Brush { const char *name; int off, size, step, derive; } Brush;   /* off: the left side's field, + step = the right side's */
static const Brush g_brush[5] = { { "Side texture", 0x148, 1, 1, 0 }, { "Side arg", 0x14a, 1, 1, 0 }, { "Kerb height", 0x140, 4, 4, 0 },
                                  { "Verge width", 0x138, 4, 4, 1 }, { "Wall height", 0x154, 4, 0xc, 0 } };
static int g_paint, g_brushI, g_showMarks = 1; static HWND g_paintChk, g_cbBrush, g_brushVal, g_marksChk;
static int *g_stIdx; static RoadPiece *g_stBefore; static int g_stN, g_stCap, g_stroke;   /* the stroke being painted: its pieces as they were */
static int BrushValue(int *v) { char t[32]; GetWindowTextA(g_brushVal, t, sizeof t); if (!t[0]) return 0; *v = (int)strtol(t, NULL, 0); return 1; }
static void PaintBegin(void) { g_stN = 0; g_stroke = 1; }
static void PaintAt(int k, int part)   /* the brush onto piece k's side (part 1 left, 2 right) */
{   const Brush *b = &g_brush[g_brushI]; int v, i, side = part - 1; RoadPiece *pc; RoadSection *s; unsigned char *q;
    if (!g_open || !g_stroke || part < 1 || part > 2 || k < 0 || k >= g_c.n) return;
    if (!BrushValue(&v)) { Status("Paint: give the brush a value first (type it, or Pick...)"); return; }
    for (i = 0; i < g_stN && g_stIdx[i] != k; i++) ;
    if (i == g_stN) { if (g_stN == g_stCap) { int nc = g_stCap ? g_stCap * 2 : 64; int *ni = (int *)realloc(g_stIdx, sizeof(int) * nc); RoadPiece *nb = (RoadPiece *)realloc(g_stBefore, sizeof(RoadPiece) * nc);
            if (ni) g_stIdx = ni; if (nb) g_stBefore = nb; if (!ni || !nb) return; g_stCap = nc; }
        g_stIdx[g_stN] = k; g_stBefore[g_stN] = g_c.piece[k]; g_stN++; }
    pc = &g_c.piece[k]; s = g_half && pc->layout == 2 ? &pc->halfB : &pc->halfA; q = (unsigned char *)s + b->off + side * b->step;
    if (b->size == 1) *q = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); else *(int *)q = v;
    if (b->derive) Derive(s, 0x138);
    if (pc->layout != 2) pc->halfB = pc->halfA;
    g_dirty = 1; Redraw(); }
static void PaintEnd(void)   /* the stroke: one undo step */
{   Undo *u; int i;
    if (!g_stroke) return; g_stroke = 0;
    if (!g_stN) return;
    u = UndoBegin(g_stIdx, g_stN);
    for (i = 0; i < g_stN; i++) u->before[i] = g_stBefore[i];
    UndoEnd(u); PropsFill(); Status("Painted %s on %d piece%s (Ctrl+Z: undo the stroke)", g_brush[g_brushI].name, g_stN, g_stN == 1 ? "" : "s"); g_stN = 0; }
/* ---------------------------------------------------------------- the game's renderer (src\platform\viewer_api.h; the game: the ddraw shim's viewer.inc)
   "Game renderer": the game itself draws the course in the 3D panel - the race started off-screen with the edits, held still, its window put in the
   panel.  This side writes the camera (the focus piece, the orbit) and the choice; a click is passed on and the game answers with the piece.  The
   editor's own 3D view is drawn again when it is off.  F6 starts it again with the edits as they are now. */
static RrViewer *g_vw; static HANDLE g_vwMap, g_vwProc; static int g_vwShown, g_vwPickDone; static DWORD g_vwT0;
static void Pick3D(int x, int y, int extend);
static int g_lastW;   /* (the last 3D frame's width: below) */
static int busy3D;   /* Render3D drew something that moves (the game renderer's progress bar): the main loop draws again at once */
static int g_vwAsked;   /* the click sent to the game not answered yet (its number) */
static void ViewPick(int x, int y, int extend)   /* a choice by the mouse in the 3D view (window points) */
{   int ww = 0, wh = 0; SDL_GetWindowSize(g_sw, &ww, &wh);
    if (g_vwShown && g_vw) { if (g_vwAsked && (extend || g_paint)) return;   /* (the game still finding the last one: a range waits for its start) */
        g_vw->clickX = x; g_vw->clickY = y; g_vw->clickW = ww; g_vw->clickH = wh; g_vw->clickShift = extend; g_vwAsked = ++g_vw->clickSeq; }
    else { float sc = ww > 0 && g_lastW > 0 ? (float)g_lastW / ww : 1.0f; Pick3D((int)(x * sc), (int)(y * sc), extend); } }
static void ViewerStop(void)
{   if (g_vwProc) { if (g_vw) g_vw->quit = 1; if (WaitForSingleObject(g_vwProc, 1500) != WAIT_OBJECT_0) TerminateProcess(g_vwProc, 0); CloseHandle(g_vwProc); g_vwProc = NULL; }
    g_vwShown = 0; if (g_vw) { g_vw->state = 0; g_vw->quit = 0; } }
static void ViewerSync(void)   /* the camera and the choice for the game (every frame) */
{   int i, lo = -1, hi = -1;
    if (!g_vw) return;
    g_vw->focus = g_focus; g_vw->camS = (float)g_camS; g_vw->camU = (float)g_camU; g_vw->half = g_half; g_vw->yaw = g_yaw; g_vw->pitch = g_pitch; g_vw->dist = g_dist;
    for (i = 0; i < g_nsel; i++) { if (lo < 0 || g_sel[i] < lo) lo = g_sel[i]; if (g_sel[i] > hi) hi = g_sel[i]; }
    g_vw->selLo = lo; g_vw->selHi = hi; g_vw->part = g_part; g_vw->marks = g_showMarks;
    {   int v = 0, has = g_paint && BrushValue(&v); const Brush *b = &g_brush[g_brushI];
        g_vw->paint = g_paint; g_vw->paintOff = has ? b->off : 0; g_vw->paintSize = b->size; g_vw->paintStep = b->step; g_vw->paintValue = v; } }
static RrvPiece *g_vwPieces; static HANDLE g_vwPMap; static RoadPiece *g_vwSent; static int g_vwPN; static DWORD g_vwEditT;
static void ViewerEditsSend(void)   /* the pieces changed since the game loaded / last heard: into the pieces memory, the game takes them (viewer.inc ViewerEdits) */
{   int k, any = 0;
    if (!g_vwPieces || !g_vwSent || GetTickCount() - g_vwEditT < 80) return;
    g_vwEditT = GetTickCount();
    for (k = 0; k < g_vwPN && k < g_c.n; k++) { RoadPiece *p = &g_c.piece[k], *o = &g_vwSent[k];
        if (!memcmp((unsigned char *)&p->halfA + RRC_FROM, (unsigned char *)&o->halfA + RRC_FROM, RRC_SECSIZE - RRC_FROM) && !memcmp((unsigned char *)&p->halfB + RRC_FROM, (unsigned char *)&o->halfB + RRC_FROM, RRC_SECSIZE - RRC_FROM)) continue;
        *o = *p; g_vwPieces[k].layout = p->layout; memcpy(g_vwPieces[k].a, &p->halfA, RRV_SEC); memcpy(g_vwPieces[k].b, &p->halfB, RRV_SEC); g_vwPieces[k].ver++; any = 1; }
    if (any) g_vw->editSeq++; }
static void ViewerStart(void)
{   char name[64], rrc[MAX_PATH], cfg[MAX_PATH], env[2048], *o = env; RECT r; int i, edited = 0;
    if (!g_open) return;
    ViewerStop();
    if (!g_vwMap) { sprintf(name, "OpenRashViewer%lu", GetCurrentProcessId()); g_vwMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(RrViewer), name);
        if (g_vwMap) g_vw = (RrViewer *)MapViewOfFile(g_vwMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RrViewer)); }
    if (!g_vw) { Status("Game renderer: no shared memory"); return; }
    memset(g_vw, 0, sizeof *g_vw); g_vwAsked = 0; g_vw->magic = RRV_MAGIC; g_vw->parent = (unsigned)(UINT_PTR)g_view; g_vw->pickSeq = g_vwPickDone = 0; ViewerSync();
    for (i = 0; i < g_c.n && !edited; i++) edited = memcmp((unsigned char *)&g_c.piece[i].halfA + RRC_FROM, (unsigned char *)&g_orig[i].halfA + RRC_FROM, RRC_SECSIZE - RRC_FROM) || memcmp((unsigned char *)&g_c.piece[i].halfB + RRC_FROM, (unsigned char *)&g_orig[i].halfB + RRC_FROM, RRC_SECSIZE - RRC_FROM);
    _snprintf(rrc, sizeof rrc - 1, "%s\\world3d\\editor_preview.rrc", g_dataDir); rrc[sizeof rrc - 1] = 0;
    _snprintf(cfg, sizeof cfg - 1, "%s\\world3d\\viewer.editor.cfg", g_dataDir); cfg[sizeof cfg - 1] = 0;
    if (edited && !SaveRrcTo(rrc)) { Status("Could not write %s", rrc); return; }
    {   char pn[80]; sprintf(pn, "OpenRashViewer%luP", GetCurrentProcessId());   /* the pieces memory: the course as the game loads it now; later changes go there */
        if (g_vwPieces) { UnmapViewOfFile(g_vwPieces); g_vwPieces = NULL; } if (g_vwPMap) { CloseHandle(g_vwPMap); g_vwPMap = NULL; }
        g_vwPN = g_c.n; g_vwPMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)(sizeof(RrvPiece) * g_vwPN), pn);
        if (g_vwPMap) g_vwPieces = (RrvPiece *)MapViewOfFile(g_vwPMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        free(g_vwSent); g_vwSent = (RoadPiece *)malloc(sizeof(RoadPiece) * g_vwPN); if (g_vwSent) memcpy(g_vwSent, g_c.piece, sizeof(RoadPiece) * g_vwPN);
        if (g_vwPieces) for (i = 0; i < g_vwPN; i++) { g_vwPieces[i].ver = 0; g_vwPieces[i].layout = g_c.piece[i].layout; memcpy(g_vwPieces[i].a, &g_c.piece[i].halfA, RRV_SEC); memcpy(g_vwPieces[i].b, &g_c.piece[i].halfB, RRV_SEC); } }   /* (every piece: the game's grid reads the editor's values) */
    GetClientRect(g_view, &r); if (r.right < 320) r.right = 320; if (r.bottom < 240) r.bottom = 240;
    sprintf(name, "OpenRashViewer%lu", GetCurrentProcessId());
    o += sprintf(o, "RR_VIEWER=%s", name) + 1; o += sprintf(o, "RR_AUTORACE=%d", g_track) + 1; o += sprintf(o, "RR_LEVEL=%d", g_level) + 1; o += sprintf(o, "RR_CONFIG=%s", cfg) + 1;
    if (edited) o += sprintf(o, "RR_COURSEEDITFILE=%s", rrc) + 1; else o += sprintf(o, "RR_COURSEEDIT=0") + 1;
    o += sprintf(o, "RR_QUIET_SIZE=%dx%d", (int)r.right, (int)r.bottom) + 1;
    { static const char *const fixed[] = { "RR_WORLD3D=1", "RR_HEADLESS=2", "RR_AUDIO=off", "RR_NOREGIMPORT=1", "RR_ONEROAD=1", "RR_SEED=1", "RR_NOCOPS=1", "RR_W3MESH=0" };   /* (no compiled mesh: the live stretch takes the edits) */
      for (i = 0; i < 8; i++) o += sprintf(o, "%s", fixed[i]) + 1; }
    *o = 0;
    g_runKeep = NULL;
    if (RunAndWait(g_gameExe, env, 2) < 0 || !g_runKeep) { Status("Game renderer: could not start %s", g_gameExe); return; }
    g_vwProc = g_runKeep; g_vwT0 = GetTickCount();
    Status("Game renderer: the game is loading track %d level %d%s ...", g_track, g_level, edited ? " with the edits (an edited course: the game makes its course file first)" : ""); }
static void ViewerPoll(void)   /* (the main loop) */
{   if (!g_vwProc || !g_vw) return;
    ViewerSync();
    if (WaitForSingleObject(g_vwProc, 0) == WAIT_OBJECT_0) { ViewerStop(); g_meshDirty = 1; Status("Game renderer: the game stopped (see data\\rr_log if RR_LOG is set) - the editor's own 3D view"); SendMessageA(g_gameChk, BM_SETCHECK, BST_UNCHECKED, 0); return; }
    if (g_vw->state == 2) { ViewerStop(); g_meshDirty = 1; Status("Game renderer: the game drew no True 3D world (Direct3D 11 needed)"); SendMessageA(g_gameChk, BM_SETCHECK, BST_UNCHECKED, 0); return; }
    if (g_vw->state == 1 && !g_vwShown) { char rrx[MAX_PATH]; g_vwShown = 1;
        _snprintf(rrx, sizeof rrx - 1, "%s\\%s", g_dataDir, g_vw->course); rrx[sizeof rrx - 1] = 0;
        LOG("game renderer up after %lu ms: %s", GetTickCount() - g_vwT0, rrx);
        GameCourseLoad(g_vw->course[0] && g_vw->course[1] == ':' ? g_vw->course : rrx); }
    if (g_vwShown && g_testClick[0] >= 0 && GetTickCount() - g_vwT0 > 9000) { RECT r; GetClientRect(g_view, &r);
        g_vw->clickX = g_testClick[0]; g_vw->clickY = g_testClick[1]; g_vw->clickW = r.right; g_vw->clickH = r.bottom; g_vw->clickShift = 0; g_vw->clickSeq++; g_testClick[0] = -1; LOG("test click sent"); }
    if (g_vwShown && g_testPaint[0] >= 0 && GetTickCount() - g_vwT0 > 9000) { char t[16]; int k;
        g_brushI = g_testPaint[3] & 7; if (g_brushI > 4) g_brushI = 0; SendMessageA(g_cbBrush, CB_SETCURSEL, g_brushI, 0);
        sprintf(t, "%d", g_testPaint[2]); SetWindowTextA(g_brushVal, t); PaintBegin(); for (k = g_testPaint[0]; k < g_testPaint[0] + 6; k++) PaintAt(k, g_testPaint[1]); PaintEnd();
        g_paint = 1; SendMessageA(g_paintChk, BM_SETCHECK, BST_CHECKED, 0);   /* (paint mode on: the grid seen) */
        LOG("test paint: pieces %d..%d side %d = %d", g_testPaint[0], g_testPaint[0] + 5, g_testPaint[1], g_testPaint[2]); g_testPaint[0] = -1; }
    if (g_vwShown) ViewerEditsSend();
    if (g_vw->pickSeq != g_vwPickDone && g_vw->pickSeq == g_vwAsked) g_vwAsked = 0;
    if (g_vw->pickSeq != g_vwPickDone) { LOG("game renderer: piece %d picked (%s)", g_vw->pickPiece, g_partName[g_vw->pickPart & 3]); g_vwPickDone = g_vw->pickSeq;
        if (g_vw->pickPiece >= 0) { if (g_paint) PaintAt(g_vw->pickPiece, g_vw->pickPart);
            else { if (!g_vw->pickShift) PartSet(g_vw->pickPart); SelectPiece(g_vw->pickPiece, g_vw->pickShift); } } } }
/* ---------------------------------------------------------------- templates
   A run of pieces kept under a name (<data>\mods\templates\<name>.rrt) and laid onto a course again: Save chosen as template (the chosen pieces in
   order, the name typed in the Templates box), Paste template (Ctrl+T) - from the first chosen piece on, for the template's length, or repeated over
   all the chosen pieces when more are chosen.  Everything from the lanes on (as Paste); the texture / sprite codes are the track's own (set slots), so
   on another track they name that track's pictures. */
#define RRT_MAGIC 0x31545252u   /* "RRT1" */
typedef struct RrtHeader { unsigned magic, version; int n, track, level, secSize, part, spare[5]; } RrtHeader;   /* part: as g_part (version 1 files: 0, the whole piece) */
typedef struct RrtPiece { int layout, spare[3]; unsigned char a[RRC_SECSIZE], b[RRC_SECSIZE]; } RrtPiece;
static HWND g_cbTpl;
static void TplPath(const char *name, char *out) { _snprintf(out, MAX_PATH - 1, "%s\\mods\\templates\\%s.rrt", g_dataDir, name); out[MAX_PATH - 1] = 0; }
static void TplList(const char *keep)   /* the Templates box: every file, the typed / given name kept */
{   char pat[MAX_PATH], t[128]; WIN32_FIND_DATAA fd; HANDLE h;
    if (keep) strncpy(t, keep, sizeof t - 1), t[sizeof t - 1] = 0; else GetWindowTextA(g_cbTpl, t, sizeof t);
    SendMessageA(g_cbTpl, CB_RESETCONTENT, 0, 0);
    _snprintf(pat, sizeof pat - 1, "%s\\mods\\templates\\*.rrt", g_dataDir); pat[sizeof pat - 1] = 0;
    if ((h = FindFirstFileA(pat, &fd)) != INVALID_HANDLE_VALUE) { do { char *dot = strrchr(fd.cFileName, '.'); if (dot) *dot = 0; SendMessageA(g_cbTpl, CB_ADDSTRING, 0, (LPARAM)fd.cFileName); } while (FindNextFileA(h, &fd)); FindClose(h); }
    SetWindowTextA(g_cbTpl, t); }
static int TplName(char *name)   /* the name in the Templates box, made a file name: 0 = none */
{   char t[128]; int i, n = 0; GetWindowTextA(g_cbTpl, t, sizeof t);
    for (i = 0; t[i] && n < 60; i++) { char c = t[i]; if (strchr("\\/:*?\"<>|", c) || (unsigned char)c < 32) c = '_'; name[n++] = c; }
    while (n && name[n - 1] == ' ') n--; name[n] = 0; return n > 0; }
static void TplSave(void)
{   char name[64], path[MAX_PATH], dir[MAX_PATH]; FILE *f; RrtHeader h; RrtPiece *r; int i;
    if (!g_open || !g_nsel) return;
    if (!TplName(name)) { Status("Templates: type a name in the Templates box first, then Save chosen as template"); SetFocus(g_cbTpl); return; }
    TplPath(name, path);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) { char q[200]; sprintf(q, "A template \"%s\" is there already. Replace it with the %d chosen piece%s?", name, g_nsel, g_nsel == 1 ? "" : "s");
        if (MessageBoxA(g_main, q, "Course Editor", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return; }
    _snprintf(dir, sizeof dir - 1, "%s\\mods", g_dataDir); dir[sizeof dir - 1] = 0; CreateDirectoryA(dir, NULL); strcat(dir, "\\templates"); CreateDirectoryA(dir, NULL);
    if (!(r = (RrtPiece *)calloc(g_nsel, sizeof(RrtPiece)))) return;
    for (i = 0; i < g_nsel; i++) { RoadPiece *pc = &g_c.piece[g_sel[i]]; r[i].layout = pc->layout; memcpy(r[i].a, &pc->halfA, RRC_SECSIZE); memcpy(r[i].b, &pc->halfB, RRC_SECSIZE); }
    memset(&h, 0, sizeof h); h.magic = RRT_MAGIC; h.version = 1; h.n = g_nsel; h.track = g_track; h.level = g_level; h.secSize = RRC_SECSIZE; h.part = g_part;
    if (!(f = fopen(path, "wb"))) { free(r); Status("Could not write %s", path); return; }
    fwrite(&h, sizeof h, 1, f); fwrite(r, sizeof(RrtPiece), g_nsel, f); fclose(f); free(r);
    TplList(name); Status("Template \"%s\" saved: %s of %d piece%s from track %d level %d (%s).  Paste it with Paste template / Ctrl+T", name, g_partName[g_part], g_nsel, g_nsel == 1 ? "" : "s", g_track, g_level, path); }
static void TplPaste(void)
{   char name[64], path[MAX_PATH]; FILE *f; RrtHeader h; RrtPiece *r; int i, n, k0, *idx; Undo *u;
    if (!g_open || !g_nsel) return;
    if (!TplName(name)) { Status("Templates: choose one in the Templates box first"); return; }
    TplPath(name, path);
    if (!(f = fopen(path, "rb"))) { Status("Templates: no template \"%s\"", name); return; }
    if (fread(&h, sizeof h, 1, f) != 1 || h.magic != RRT_MAGIC || h.secSize != RRC_SECSIZE || h.n <= 0 || h.n > 20000) { fclose(f); Status("Templates: %s is not a template", path); return; }
    r = (RrtPiece *)malloc(sizeof(RrtPiece) * h.n);
    if (!r || fread(r, sizeof(RrtPiece), h.n, f) != (size_t)h.n) { fclose(f); free(r); Status("Templates: %s is cut short", path); return; }
    fclose(f);
    k0 = g_sel[0]; n = g_nsel > 1 ? g_nsel : h.n;   /* (one piece chosen: the template's length from it; several: those, the template repeated) */
    if (g_nsel <= 1 && k0 + n > g_c.n) n = g_c.n - k0;
    idx = (int *)malloc(sizeof(int) * n);
    for (i = 0; i < n; i++) idx[i] = g_nsel > 1 ? g_sel[i] : k0 + i;
    u = UndoBegin(idx, n);
    if (h.part < 0 || h.part > 3) h.part = 0;
    for (i = 0; i < n; i++) { const RrtPiece *t = &r[i % h.n]; PartCopy(&g_c.piece[idx[i]], (const RoadSection *)t->a, (const RoadSection *)t->b, t->layout, h.part); }
    UndoEnd(u); free(idx); free(r);
    Status("Template \"%s\" (%s, %d piece%s, from track %d) pasted onto pieces %d..%d%s", name, g_partName[h.part], h.n, h.n == 1 ? "" : "s", h.track, k0, k0 + n - 1, h.track != g_track ? "  - another track: its texture / sprite codes name this track's pictures" : "");
    PropsFill(); Redraw(); }
static void TplDelete(void)
{   char name[64], path[MAX_PATH], q[200];
    if (!TplName(name)) return; TplPath(name, path);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) { Status("Templates: no template \"%s\"", name); return; }
    sprintf(q, "Delete the template \"%s\"?", name); if (MessageBoxA(g_main, q, "Course Editor", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    DeleteFileA(path); TplList(""); Status("Template \"%s\" deleted", name); }
static void Save(void) { if (!g_open) return; if (SaveRrc()) { char p[MAX_PATH]; RrcPath(p); Status("Saved %s - the game races it from the next race on track %d level %d", p, g_track, g_level); } else Status("Could not save"); Title(); }
static void RevertAll(void)
{   char p[MAX_PATH]; if (!g_open) return;
    if (MessageBoxA(g_main, "Put the whole course back to the original and delete its saved edits?", "Course Editor", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    RrcPath(p); DeleteFileA(p); OpenCourse(g_track, g_level); PropsFill(); Redraw(); }
static void TestDrive(void)
{   char env[256], *o = env; if (!g_open) return;
    if (g_dirty) Save();
    o += sprintf(o, "RR_AUTORACE=%d", g_track) + 1; o += sprintf(o, "RR_LEVEL=%d", g_level) + 1; *o = 0;
    if (RunAndWait(g_gameExe, env, 0) < 0) Status("Could not start %s", g_gameExe); else Status("Test drive: track %d level %d started (the course file is made on the loading screen the first time)", g_track, g_level); }

/* the plan (top-down) and the height profile */
static void PaintPlan(HWND w, HDC dc, RECT rc)
{   double minx = 1e30, maxx = -1e30, minz = 1e30, maxz = -1e30, sc; int i, W = rc.right, H = rc.bottom; HPEN pens[7]; POINT pt;
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH)); (void)w;
    if (!g_open || !g_c.frame) return;
    for (i = 0; i <= g_c.n; i++) { const C3Frame *F = &g_c.frame[i]; if (F->x < minx) minx = F->x; if (F->x > maxx) maxx = F->x; if (F->z < minz) minz = F->z; if (F->z > maxz) maxz = F->z; }
    sc = (W - 20) / (maxx - minx + 1); if ((H - 20) / (maxz - minz + 1) < sc) sc = (H - 20) / (maxz - minz + 1);
    { static const COLORREF kc[7] = { RGB(150, 150, 150), RGB(190, 140, 90), RGB(190, 140, 90), RGB(220, 110, 60), RGB(110, 180, 90), RGB(120, 140, 210), RGB(255, 230, 60) };
      for (i = 0; i < 7; i++) pens[i] = CreatePen(PS_SOLID, i == 6 ? 4 : 2, kc[i]); }
#define PX(F) (int)(10 + ((F)->x - minx) * sc)
#define PZ(F) (int)(H - 10 - ((F)->z - minz) * sc)
    for (i = 0; i < g_c.n; i++) { const C3Frame *a = &g_c.frame[i], *b = &g_c.frame[i + 1]; int kd = g_c.piece[i].halfA.kind;
        int on = 0, j; for (j = 0; j < g_nsel; j++) if (g_sel[j] == i) { on = 1; break; }
        SelectObject(dc, pens[on ? 6 : (kd >= 0 && kd < 6 ? kd : 0)]); MoveToEx(dc, PX(a), PZ(a), &pt); LineTo(dc, PX(b), PZ(b)); }
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(120, 255, 120));
    if (g_c.start >= 0 && g_c.start <= g_c.n) TextOutA(dc, PX(&g_c.frame[g_c.start]) + 4, PZ(&g_c.frame[g_c.start]) - 8, "START", 5);
    SetTextColor(dc, RGB(255, 120, 120));
    if (g_c.finish >= 0 && g_c.finish <= g_c.n) TextOutA(dc, PX(&g_c.frame[g_c.finish]) + 4, PZ(&g_c.frame[g_c.finish]) - 8, "FINISH", 6);
    SetTextColor(dc, RGB(200, 200, 220)); TextOutA(dc, 6, 4, "Plan  (click: choose a piece, Shift: a range)", 46);
    for (i = 0; i < 7; i++) DeleteObject(pens[i]);
    SetWindowLongPtrA(w, GWLP_USERDATA, 0); }
static int PlanPick(int mx, int my, RECT rc)
{   double minx = 1e30, maxx = -1e30, minz = 1e30, maxz = -1e30, sc, best = 1e30; int i, k = -1, W = rc.right, H = rc.bottom;
    for (i = 0; i <= g_c.n; i++) { const C3Frame *F = &g_c.frame[i]; if (F->x < minx) minx = F->x; if (F->x > maxx) maxx = F->x; if (F->z < minz) minz = F->z; if (F->z > maxz) maxz = F->z; }
    sc = (W - 20) / (maxx - minx + 1); if ((H - 20) / (maxz - minz + 1) < sc) sc = (H - 20) / (maxz - minz + 1);
    for (i = 0; i < g_c.n; i++) { double dx = PX(&g_c.frame[i]) - mx, dz = PZ(&g_c.frame[i]) - my, d = dx * dx + dz * dz; if (d < best) { best = d; k = i; } }
    return k; }
static void PaintProf(HDC dc, RECT rc)
{   double miny = 1e30, maxy = -1e30; int i, W = rc.right, H = rc.bottom; POINT pt; HPEN pn, ps, pk;
    FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    if (!g_open || !g_c.frame) return;
    for (i = 0; i <= g_c.n; i++) { double y = g_c.frame[i].y; if (y < miny) miny = y; if (y > maxy) maxy = y; }
    if (maxy - miny < 1) maxy = miny + 1;
    pn = CreatePen(PS_SOLID, 2, RGB(120, 200, 255)); ps = CreatePen(PS_SOLID, 1, RGB(255, 230, 60)); pk = CreatePen(PS_SOLID, 1, RGB(80, 80, 90));
    SelectObject(dc, ps); for (i = 0; i < g_nsel; i++) { int x = (int)(10 + (double)g_sel[i] * (W - 20) / g_c.n); MoveToEx(dc, x, 0, &pt); LineTo(dc, x, H); }
    SelectObject(dc, pn);
    for (i = 0; i <= g_c.n; i++) { int x = (int)(10 + (double)i * (W - 20) / g_c.n), y = (int)(H - 10 - (g_c.frame[i].y - miny) * (H - 30) / (maxy - miny)); if (!i) MoveToEx(dc, x, y, &pt); else LineTo(dc, x, y); }
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(200, 200, 220));
    { char t[128]; sprintf(t, "Height profile  (%.0f .. %.0f)   click: choose a piece", miny, maxy); TextOutA(dc, 6, 4, t, (int)strlen(t)); }
    DeleteObject(pn); DeleteObject(ps); DeleteObject(pk); }
#undef PX
#undef PZ

static LRESULT CALLBACK PanelProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{   if (m == WM_PAINT) { PAINTSTRUCT ps; RECT rc; HDC dc = BeginPaint(w, &ps), mdc; HBITMAP bm; GetClientRect(w, &rc);
        mdc = CreateCompatibleDC(dc); bm = CreateCompatibleBitmap(dc, rc.right, rc.bottom); SelectObject(mdc, bm); SelectObject(mdc, g_font);
        if (w == g_plan) PaintPlan(w, mdc, rc); else PaintProf(mdc, rc);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mdc, 0, 0, SRCCOPY); DeleteObject(bm); DeleteDC(mdc); EndPaint(w, &ps); return 0; }
    if (m == WM_ERASEBKGND) return 1;
    if ((m == WM_LBUTTONDOWN || (m == WM_MOUSEMOVE && (wp & MK_LBUTTON))) && g_open) { RECT rc; int k, x = (short)LOWORD(lp), y = (short)HIWORD(lp); GetClientRect(w, &rc);
        k = w == g_plan ? PlanPick(x, y, rc) : (int)((double)(x - 10) * g_c.n / (rc.right - 20));
        SelectPiece(k, (wp & MK_SHIFT) != 0 || m == WM_MOUSEMOVE); return 0; }
    return DefWindowProcA(w, m, wp, lp); }

/* ---------------------------------------------------------------- the mod's HD pictures
   As the game draws them (src\platform\ddraw\hdtex.inc): <data>\mod\widescreen\hd\t<track>\<NAME>.png at 4 x the texture's size; hashes.txt lists
   "<file> <hash>" with the hash (FNV-1a) of the original texture's palette indices - a track can have two textures of one name.  None = the original. */
typedef struct HdHash { char file[40]; char name[9]; unsigned hash; } HdHash;
static HdHash *g_hd; static int g_nhd, g_hdTrack = -1; static char g_hdDir[MAX_PATH];
static void HdDirLoad(int track)
{   char p[MAX_PATH], line[128]; FILE *f;
    free(g_hd); g_hd = NULL; g_nhd = 0; g_hdTrack = track;
    _snprintf(g_hdDir, sizeof g_hdDir - 1, "%s\\mod\\widescreen\\hd\\t%d", g_dataDir, track); g_hdDir[sizeof g_hdDir - 1] = 0;
    _snprintf(p, sizeof p - 1, "%s\\hashes.txt", g_hdDir); p[sizeof p - 1] = 0;
    if (!(f = fopen(p, "r"))) return;
    while (fgets(line, sizeof line, f)) { HdHash x; unsigned hv; char *at; memset(&x, 0, sizeof x);
        if (sscanf(line, "%39s %x", x.file, &hv) != 2) continue;
        x.hash = hv; strncpy(x.name, x.file, 8); if ((at = strchr(x.name, '@')) != NULL) *at = 0;
        if (!(g_nhd & 255)) { HdHash *q = (HdHash *)realloc(g_hd, sizeof(HdHash) * (g_nhd + 256)); if (!q) break; g_hd = q; }
        g_hd[g_nhd++] = x; }
    fclose(f); LOG("HD pictures: %s (%d)", g_hdDir, g_nhd); }
static unsigned *HdPicture(const RxTex *t, int *ow, int *oh)   /* the 4x RGBA (in memory R, G, B, A) of texture t, NULL = none */
{   char nm[9], p[MAX_PATH]; unsigned hs = 2166136261u; int i, named = 0, found = 0; SDL_Surface *sf, *c; unsigned *o = NULL;
    if (!t->bits || t->gen || t->w <= 0 || t->h <= 0) return NULL;
    if (g_hdTrack != g_track) HdDirLoad(g_track);
    for (i = 0; i < 8 && t->name[i] && t->name[i] != ' '; i++) nm[i] = (char)toupper((unsigned char)t->name[i]); nm[i] = 0; if (!i) return NULL;
    for (i = 0; i < t->w * t->h; i++) hs = (hs ^ t->bits[i]) * 16777619u;
    for (i = 0; i < g_nhd; i++) if (!strcmp(g_hd[i].name, nm)) { named = 1; if (g_hd[i].hash == hs) { _snprintf(p, sizeof p - 1, "%s\\%s.png", g_hdDir, g_hd[i].file); found = 1; break; } }
    if (!found) { if (named) return NULL; _snprintf(p, sizeof p - 1, "%s\\%s.png", g_hdDir, nm); }
    p[sizeof p - 1] = 0;
    if (GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES || !(sf = SDL_LoadPNG(p))) return NULL;
    c = SDL_ConvertSurface(sf, SDL_PIXELFORMAT_ABGR8888); SDL_DestroySurface(sf);   /* (ABGR8888: R, G, B, A in memory - as RX_Rgba) */
    if (c && c->w == 4 * t->w && c->h == 4 * t->h && (o = (unsigned *)malloc((size_t)c->w * c->h * 4)) != NULL) {
        int y; for (y = 0; y < c->h; y++) memcpy(o + (size_t)y * c->w, (const unsigned char *)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 4);
        if (t->alpha) for (i = 0; i < c->w * c->h; i++) { unsigned a = o[i] >> 24; if (a > (unsigned)t->alpha) o[i] = (o[i] & 0xffffff) | ((unsigned)t->alpha << 24); }   /* (translucent: the sprite shadow) */
        *ow = c->w; *oh = c->h; }
    if (c) SDL_DestroySurface(c);
    return o; }

/* ---------------------------------------------------------------- 3D */
static int GpuTex(int t)   /* the renderer's texture of the view course's texture t (each course its own: the two extracts can number them differently) */
{   int *c;
    if (t < 0 || t >= g_vra->ntex) return -1;
    if (!g_gtexs[g_vslot]) { int i; g_gtexs[g_vslot] = (int *)malloc(sizeof(int) * 8192); for (i = 0; i < 8192; i++) g_gtexs[g_vslot][i] = -2; }
    c = g_gtexs[g_vslot];
    if (c[t] == -2) { int w = 0, h = 0; unsigned *px = HdPicture(&g_vra->tex[t], &w, &h);   /* (the mod's HD picture where there is one, as the game draws it) */
        if (!px) { px = RX_Rgba(g_vra, &g_vra->tex[t]); w = g_vra->tex[t].w; h = g_vra->tex[t].h; }
        c[t] = px ? R3_Texture(&g_r, px, w, h) : -1; free(px); }
    return c[t]; }
static void TexReset(int slot)   /* (a course opened / generated again: its textures made again; the renderer's old ones stay - few) */
{   int i; if (g_gtexs[slot]) for (i = 0; i < 8192; i++) g_gtexs[slot][i] = -2; }
static R3Batch g_rb[4096]; static int g_nrb;
static float g_lastVp[16]; static int g_lastW, g_lastH, g_lastOk;   /* the last 3D frame's view-projection (picking, labels) */
static int Project(const float *pt, float *sx, float *sy)   /* world -> 3D view pixels; 0 = behind the eye */
{   const float *m = g_lastVp; float X = pt[0] * m[0] + pt[1] * m[4] + pt[2] * m[8] + m[12], Y = pt[0] * m[1] + pt[1] * m[5] + pt[2] * m[9] + m[13], W = pt[0] * m[3] + pt[1] * m[7] + pt[2] * m[11] + m[15];
    if (W < 1) return 0; *sx = (X / W + 1) * 0.5f * g_lastW; *sy = (1 - Y / W) * 0.5f * g_lastH; return 1; }
static const RoadSection *ViewSec(int k) { const RoadPiece *p = &g_vc->piece[k]; return g_half && p->layout == 2 ? &p->halfB : &p->halfA; }   /* (the view course's section of the half being edited) */
static int IsSel(int k) { int a = 0, b = g_nsel - 1; while (a <= b) { int m = (a + b) / 2; if (g_sel[m] == k) return 1; if (g_sel[m] < k) a = m + 1; else b = m - 1; } return 0; }
static void SelectPiece(int k, int extend);   /* (below) */
static int g_lsel;   /* the left button is held in the 3D view: moving chooses a range */
static int PickAt(int x, int y, int *part)   /* the piece whose ground is nearest the 3D view's pixel x, y (-1 none), and where on it: 0 the road, 1 left, 2 right */
{   int k, best = -1, bpart = 0; float bd = 90.0f * 90.0f;
    if (!g_lastOk || !g_open) return -1;
    for (k = g_meshLo; k <= g_meshHi && k < g_vc->n; k++) { float pt[3], sx, sy, d; int j;
        for (j = 0; j < 9; j++) { RoadSection *s = &g_vc->piece[k].halfA; static const double out[3] = { 1200, 700, 300 };   /* (3 points left of the road, 3 on it, 3 right of it) */
            double u = j < 3 ? s->edgeL - out[j] : j == 3 ? s->laneEdgeL : j == 4 ? (s->laneEdgeL + s->laneEdgeR) * 0.5 : j == 5 ? s->laneEdgeR : s->edgeR + out[8 - j];
            C3_PointF(g_vc, g_vc->frame, k + 0.5, u, 0, pt); if (!Project(pt, &sx, &sy)) continue;
            d = (sx - x) * (sx - x) + (sy - y) * (sy - y); if (d < bd) { bd = d; best = k; bpart = j < 3 ? 1 : j < 6 ? 0 : 2; } } }
    *part = bpart; return best; }
static void Pick3D(int x, int y, int extend)   /* a click in the 3D view: choose the piece there (and the part: the road, left / right of it) - or paint it */
{   int part, best = PickAt(x, y, &part);
    if (best < 0) return;
    if (g_paint) { PaintAt(best, part); return; }
    if (!extend) PartSet(part); SelectPiece(best, extend); }
/* the chosen pieces on the road: a frame round each (its start and end lines and both sides; the focus orange, the others yellow), as per-frame geometry */
static R3Vert *g_band; static R3Batch g_bandB; static int g_nband;
static void BandBuild(void)
{   int i, n = 0, cap = 48 * 1200;
    if (!g_band) g_band = (R3Vert *)malloc(sizeof(R3Vert) * cap);
    g_nband = 0; if (!g_band) return;
    for (i = 0; i < g_nsel && n + 24 <= cap; i++) { int k = g_sel[i], j, e; static const int o6[6] = { 0, 1, 2, 0, 2, 3 }; double l0, l1, r0, r1;
        unsigned col = k == g_focus ? 0xff1e6effu : 0xff28dcffu;   /* (R, G, B, A in memory) */
        if (k < g_meshLo || k > g_meshHi || k + 1 >= g_vc->n) continue;
      { int rg; for (rg = 0; rg < 2 && n + 24 <= cap; rg++) {   /* the part's frame(s): the whole piece over the road; a side from the road's edge 900 out; both sides two */
        const RoadSection *sa = &g_vc->piece[k].halfA, *sb = &g_vc->piece[k + 1].halfA;
        if (g_part == 0) { if (rg) break; l0 = sa->edgeL - 30; r0 = sa->edgeR + 30; l1 = sb->edgeL - 30; r1 = sb->edgeR + 30; }
        else if (g_part == 1 || (g_part == 3 && rg == 0)) { if (g_part == 1 && rg) break; l0 = sa->edgeL - 900; r0 = sa->edgeL; l1 = sb->edgeL - 900; r1 = sb->edgeL; }
        else { if (g_part == 2 && rg) break; l0 = sa->edgeR; r0 = sa->edgeR + 900; l1 = sb->edgeR; r1 = sb->edgeR + 900; }
        for (e = 0; e < 4; e++) { float q[4][3]; double t0 = k, t1 = k + 1;   /* 0 = the start line, 1 = the end line, 2 = the left side, 3 = the right side */
            if (e == 0) { t1 = k + 0.08; C3_PointF(g_vc, g_vc->frame, t0, l0, 6, q[0]); C3_PointF(g_vc, g_vc->frame, t0, r0, 6, q[1]); C3_PointF(g_vc, g_vc->frame, t1, r0, 6, q[2]); C3_PointF(g_vc, g_vc->frame, t1, l0, 6, q[3]); }
            else if (e == 1) { t0 = k + 0.92; C3_PointF(g_vc, g_vc->frame, t0, l1, 6, q[0]); C3_PointF(g_vc, g_vc->frame, t0, r1, 6, q[1]); C3_PointF(g_vc, g_vc->frame, t1, r1, 6, q[2]); C3_PointF(g_vc, g_vc->frame, t1, l1, 6, q[3]); }
            else { double u0 = e == 2 ? l0 : r0 - 50, u1 = e == 2 ? l1 : r1 - 50;
                C3_PointF(g_vc, g_vc->frame, t0, u0, 6, q[0]); C3_PointF(g_vc, g_vc->frame, t0, u0 + 50, 6, q[1]); C3_PointF(g_vc, g_vc->frame, t1, u1 + 50, 6, q[2]); C3_PointF(g_vc, g_vc->frame, t1, u1, 6, q[3]); }
            for (j = 0; j < 6; j++) { R3Vert *v = &g_band[n++]; const float *pp = q[o6[j]]; memset(v, 0, sizeof *v); v->x = pp[0]; v->y = pp[1]; v->z = pp[2]; v->rgba = col; v->tex = -1; } } } } }
    g_nband = n; g_bandB.first = 0; g_bandB.count = n; g_bandB.tex = -1; g_bandB.alpha = 0; }
static void BuildMesh(int k)
{   static C3Batch cb[4096]; int nb, i, lo = k - 40, hi = k + CE_RANGE;
    if (lo < 0) lo = 0; if (hi > g_vc->n - 1) hi = g_vc->n - 1;
    g_vc->nv = 0; g_vc->skirts = 1; g_vc->skirtEnd = 0; g_vc->fr = g_vc->frame;
    C3_BuildRange(g_vc, lo, hi, g_vra);
    nb = C3_Batches(g_vc, cb, 4096); g_nrb = 0;
    for (i = 0; i < nb; i++) { int t = cb[i].tex, bg = t >= C3_BG - 1; if (bg) t -= C3_BG;
        g_rb[g_nrb].first = cb[i].first; g_rb[g_nrb].count = cb[i].count; g_rb[g_nrb].tex = GpuTex(t);
        g_rb[g_nrb].alpha = bg ? 2 : ((t >= 0 && t < g_vra->ntex && g_vra->tex[t].alpha) ? 1 : 0); g_nrb++; }
    R3_SetLive(&g_r, (const R3Vert *)g_vc->v, g_vc->nv);
    g_meshLo = lo; g_meshHi = hi; g_meshDirty = 0; }
static void Render3D(void)
{   int w = 0, h = 0, k, kc; R3Frame f; float view[16], proj[16], eye[3], at[3], fwd[3], up[3] = { 0, 1, 0 }, len; ID3D11Texture2D *t; double hd;
    if (!g_ren) return;
    SDL_GetCurrentRenderOutputSize(g_ren, &w, &h);
    SDL_SetRenderDrawColor(g_ren, 30, 32, 40, 255); SDL_RenderClear(g_ren);
    if (!g_open || !g_r3ok || w < 16 || h < 16) { SDL_RenderPresent(g_ren); return; }
    if (!g_rt || g_rtw != w || g_rth != h) { if (g_rt) SDL_DestroyTexture(g_rt); g_rt = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h); g_rtw = w; g_rth = h; }
    if (!g_rt) return;
    {   int want = g_gameView && g_haveGame; if (want != g_vslot) { g_vslot = want; g_vc = want ? &g_gc : &g_c; g_vra = want ? &g_gra : &g_ra; g_meshDirty = 1; } }
    k = g_focus < g_vc->n ? g_focus : g_vc->n - 1;
    if (g_camS < 0) g_camS = k + 0.5; if (g_camS > g_vc->n - 2) g_camS = g_vc->n - 2; kc = (int)g_camS;   /* (kc: the piece the camera is at) */
    if (g_meshDirty || kc < g_meshLo + 20 || kc > g_meshHi - 120) BuildMesh(kc);
    C3_PointF(g_vc, g_vc->frame, g_camS, C3_RoadMid(g_vc, g_camS) + g_camU, 0, at); at[1] += 150;
    hd = g_vc->frame[kc].heading + (g_vc->frame[kc + 1].heading - g_vc->frame[kc].heading) * (g_camS - kc) + g_yaw;
    eye[0] = at[0] - (float)(sin(hd) * cos(g_pitch) * g_dist); eye[2] = at[2] - (float)(cos(hd) * cos(g_pitch) * g_dist); eye[1] = at[1] + (float)(sin(g_pitch) * g_dist);
    fwd[0] = at[0] - eye[0]; fwd[1] = at[1] - eye[1]; fwd[2] = at[2] - eye[2]; len = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]); fwd[0] /= len; fwd[1] /= len; fwd[2] /= len;
    R3_LookTo(view, eye, fwd, up);
    R3_Persp(proj, w * 0.5f / 0.62f, w * 0.5f / 0.62f, w * 0.5f, h * 0.5f, w, h, 20.0f, 160000.0f);
    t = (ID3D11Texture2D *)SDL_GetPointerProperty(SDL_GetTextureProperties(g_rt), SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, NULL);
    if (!t) return;
    memset(&f, 0, sizeof f);
    f.target = t; f.w = w; f.h = h; R3_Mul(f.vp, view, proj);
    f.clear[0] = 0.55f; f.clear[1] = 0.70f; f.clear[2] = 0.92f; f.clear[3] = 1; f.doClear = 1;
    f.fogColour[0] = 0.62f; f.fogColour[1] = 0.74f; f.fogColour[2] = 0.90f; f.fogColour[3] = 1; f.fogStart = 60000; f.fogScale = 1.0f / 90000; f.fogMax = 0.8f;
    f.right[0] = fwd[2]; f.right[2] = -fwd[0]; { float l2 = sqrtf(f.right[0] * f.right[0] + f.right[2] * f.right[2]); if (l2 > 0) { f.right[0] /= l2; f.right[2] /= l2; } }
    f.eye[0] = eye[0]; f.eye[1] = eye[1]; f.eye[2] = eye[2]; f.bias = 400; f.bright = 1; f.useBright = 1; f.crisp = 1;
    f.sun[0] = -0.4f; f.sun[1] = 0.8f; f.sun[2] = 0.45f; f.sun[3] = 0.6f;
    f.liveBatches = g_rb; f.nLive = g_nrb;
    BandBuild(); if (g_nband) { f.dyn = g_band; f.nDyn = g_nband; f.dynBatches = &g_bandB; f.nDynBatches = 1; }
    memcpy(g_lastVp, f.vp, sizeof g_lastVp); g_lastW = w; g_lastH = h; g_lastOk = 1;
    SDL_FlushRenderer(g_ren);
    R3_Render(&g_r, &f);
    SDL_SetTextureBlendMode(g_rt, SDL_BLENDMODE_NONE); SDL_RenderTexture(g_ren, g_rt, NULL, NULL);
    {   static SDL_FRect used[1200]; int nu = 0, d; SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);  /* every piece's number on the road (small); out from the focus, a label that would cover a nearer one is left out */
        for (d = 0; d <= g_meshHi - g_meshLo + 1 && nu < 1200; d++) { int s2;
            for (s2 = 0; s2 < (d ? 2 : 1); s2++) { int j = s2 ? kc - d : kc + d, on = j == k, sel, u; float pt[3], sx, sy, tw; char lb[24]; SDL_FRect r;
                if (j < g_meshLo || j > g_meshHi || j >= g_vc->n) continue;
                C3_PointF(g_vc, g_vc->frame, j + 0.5, C3_RoadMid(g_vc, j + 0.5), 40, pt); if (!Project(pt, &sx, &sy) || sx < 0 || sy < 24 || sx > w || sy > h) continue;
                sprintf(lb, "%d", j); tw = (float)strlen(lb) * 8; r.x = sx - tw / 2 - 1; r.y = sy - 5; r.w = tw + 2; r.h = 10;
                for (u = 0; u < nu; u++) if (r.x < used[u].x + used[u].w && used[u].x < r.x + r.w && r.y < used[u].y + used[u].h && used[u].y < r.y + r.h) break;
                if (u < nu) continue; used[nu++] = r; sel = IsSel(j);
                SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 140); SDL_RenderFillRect(g_ren, &r);
                if (on) SDL_SetRenderDrawColor(g_ren, 255, 120, 40, 255); else if (sel) SDL_SetRenderDrawColor(g_ren, 255, 230, 60, 255); else SDL_SetRenderDrawColor(g_ren, 255, 255, 255, 255);
                SDL_RenderDebugText(g_ren, r.x + 1, r.y + 1, lb); } } }
    {   const C3Frame *F = g_half && g_vc->frameB ? g_vc->frameB : g_vc->frame;   /* the paint grid, the focus piece's fields (course_marks.h - the game renderer draws the same) */
        if (g_paint) { int v = 0, has = BrushValue(&v); const Brush *b = &g_brush[g_brushI]; CM_Grid(g_ren, g_vc, F, kc - 60, kc + 160, Project, ViewSec, has ? b->off : 0, b->size, b->step, v); }
        if (g_showMarks && k + 1 < g_vc->n) CM_Marks(g_ren, g_vc, F, ViewSec(k), k, Project); }
    {   char tx[220]; RoadSection *s = Sec(k, g_half);
        sprintf(tx, "%s   piece %d (%s)  curve %d  hill %d  lanes %d+%d   |  click: choose a piece (hold and move / Shift: several)   right-drag: look round   wheel: distance   WASD: move",
            g_vslot ? "AS THE GAME BUILDS IT" : "EDITOR", k, KindName(s->kind), s->curve, s->hill, s->lanesL, s->lanesR);
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 255); SDL_FRect bg = { 0, 0, (float)w, 22 }; SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND); SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 150); SDL_RenderFillRect(g_ren, &bg);
        SDL_SetRenderDrawColor(g_ren, g_vslot ? 120 : 255, 255, g_vslot ? 140 : 255, 255); SDL_RenderDebugText(g_ren, 8, 7, tx); }
    if (g_vwProc && !g_vwShown && g_vw) {   /* the game's renderer is loading: a progress bar (the game's own loading stages) over the editor's view */
        float bw = w * 0.6f, bx = (w - bw) * 0.5f, by = h * 0.5f - 10, pc = g_vw->progress / 1000.0f; DWORD el = (GetTickCount() - g_vwT0) / 1000; char t1[200], t2[64];
        SDL_FRect panel = { bx - 20, by - 46, bw + 40, 98 }, track = { bx, by, bw, 20 }, fill = { bx, by, bw * pc, 20 };
        if (pc <= 0) { float ph = (float)((GetTickCount() / 8) % (unsigned)(bw > 60 ? bw - 60 : 1)); fill.x = bx + ph; fill.w = 60; }   /* (no stage yet - the game starting: a moving block, so it is seen working) */
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_ren, 10, 12, 18, 215); SDL_RenderFillRect(g_ren, &panel);
        SDL_SetRenderDrawColor(g_ren, 60, 64, 76, 255); SDL_RenderFillRect(g_ren, &track);
        SDL_SetRenderDrawColor(g_ren, 120, 255, 140, 255); SDL_RenderFillRect(g_ren, &fill);
        SDL_SetRenderDrawColor(g_ren, 200, 205, 215, 255); SDL_RenderRect(g_ren, &track);
        _snprintf(t1, sizeof t1 - 1, "GAME RENDERER  -  %s", g_vw->stage[0] ? g_vw->stage : "starting the game"); t1[sizeof t1 - 1] = 0;
        sprintf(t2, pc > 0 ? "%d %%   %lu s" : "%lu s", pc > 0 ? (int)(pc * 100) : (int)el, el);
        if (pc <= 0) sprintf(t2, "%lu s", el);
        SDL_SetRenderDrawColor(g_ren, 255, 255, 255, 255); SDL_RenderDebugText(g_ren, bx, by - 26, t1);
        SDL_RenderDebugText(g_ren, bx, by + 32, t2); busy3D = 1; }
    SDL_RenderPresent(g_ren); }

/* ---------------------------------------------------------------- main window */
static void Layout(void)
{   RECT rc; int W, H, top = 68, sb = 22, lw = 330, pw = 470, bh = 230; GetClientRect(g_main, &rc); W = rc.right; H = rc.bottom;
    SendMessageA(g_statusw, WM_SIZE, 0, 0);
    MoveWindow(g_list, 0, top, lw, H - top - sb, TRUE);
    MoveWindow(g_props, W - pw, top, pw, H - top - sb, TRUE);
    MoveWindow(g_view, lw + 4, top, W - lw - pw - 8, H - top - sb - bh - 4, TRUE);
    MoveWindow(g_plan, lw + 4, H - sb - bh, (W - lw - pw - 8) / 2 - 2, bh, TRUE);
    MoveWindow(g_prof, lw + 4 + (W - lw - pw - 8) / 2 + 2, H - sb - bh, (W - lw - pw - 8) / 2 - 2, bh, TRUE); }
static HWND Button(const char *t, int id, int x, int w) { HWND b = CreateWindowA("BUTTON", t, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, x, 5, w, 26, g_main, (HMENU)(INT_PTR)id, NULL, NULL); SendMessageA(b, WM_SETFONT, (WPARAM)g_font, 0); return b; }
static void EditStart(int row)
{   RECT r; char t[64]; if (!g_open || row < 0 || row >= NFLD) return;
    ListView_GetSubItemRect(g_props, row, 1, LVIR_BOUNDS, &r);
    ListView_GetItemText(g_props, row, 1, t, sizeof t); if (!strcmp(t, "(several)")) t[0] = 0; else { char *sp = strchr(t, ' '); if (sp) *sp = 0; }
    g_editRow = row; SetWindowTextA(g_edit, t); MoveWindow(g_edit, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE); ShowWindow(g_edit, SW_SHOW); SetFocus(g_edit); SendMessageA(g_edit, EM_SETSEL, 0, -1); }
static WNDPROC g_editOld;
static LRESULT CALLBACK EditProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{   if (m == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) { if (wp == VK_RETURN) { char t[64]; GetWindowTextA(w, t, sizeof t); ApplyField(g_editRow, t); } ShowWindow(w, SW_HIDE); SetFocus(g_props); return 0; }
    if (m == WM_KILLFOCUS) ShowWindow(w, SW_HIDE);
    if (m == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
    return CallWindowProcA(g_editOld, w, m, wp, lp); }
/* ---------------------------------------------------------------- the visual pickers
   Double-click a field: a picker for it - the textures / sprites / object rows it can name (thumbnails from the piece's own texture sets, the mod's HD
   pictures where there are some), the kinds, the flag bits, or a slider for a number (the 3D view follows while it moves).  Enter on a field: type the value */
enum { PK_THUMB = 1, PK_KIND, PK_FLAGS, PK_NUM };
enum { IDP_ICONS = 300, IDP_BOX, IDP_TRACK, IDP_EDIT, IDP_SET, IDP_CLOSE, IDP_INFO, IDP_BIT0 = 310 };
#define PK_TS 84   /* thumbnail size */
static HWND g_pick, g_pkIcons, g_pkBox, g_pkTrack, g_pkEdit, g_pkSet, g_pkInfo, g_pkClose, g_pkBits[8]; static HIMAGELIST g_pkImg;
static int g_pkBrush;   /* 1 = the picker chooses the paint brush's value (not the chosen pieces') */
static int g_pkRow = -1, g_pkKind, g_pkSync, g_pkFor[3] = { -1, -1, -1 };   /* g_pkFor: the row / piece / half the thumbnails are of */
static HBITMAP g_thumb[8192], g_thumbNone; static const void *g_thumbOf;
static int PickKindOf(int off)
{   if (off == 0x148 || off == 0x149 || off == 0x14a || off == 0x14b || off == 0x40 || off == 0x50 || off == 0x63) return PK_THUMB;
    if (off == 0x3c) return PK_KIND;
    if (off == 0x37 || off == 0x41 || off == 0x51 || off == 0x64) return PK_FLAGS;
    return PK_NUM; }
static const char *BitName(int off, int b)
{   static const char *const kf[8] = { "bit 0x01", "bit 0x02", "bit 0x04", "banked verge left", "banked verge right", "cross street (open road: at road level, no sidewalk)", "forces a tunnel", "a pit / line object" };
    static const char *const sf[8] = { "mirrored", "right of the centre line", "bit 0x04", "bit 0x08", "bit 0x10", "bit 0x20", "bit 0x40", "bit 0x80" };
    static const char *const rf[8] = { "mirrored", "right side", "bit 0x04", "bit 0x08", "bit 0x10", "bit 0x20", "bit 0x40", "bit 0x80" };
    return off == 0x37 ? kf[b] : off == 0x64 ? rf[b] : sf[b]; }
/* the picture of code (slot * 64 + entry) for field off at piece k / half h: a texture, -2 = an entry without a picture of its own, -1 = no such entry */
static int PickTex(int off, unsigned code, int k, int h)
{   RxSet *st = RX_SetFor(&g_ra, k, h, (int)(code >> 6) & 3); int r0 = 0, ax, ay; unsigned t = 0;
    if (!st) return -1;
    if (off == 0x40 || off == 0x50) { int x = RX_NarrTex(&g_ra, k, h, code, 4, 1, &ax, &ay); return x >= 0 ? x : RX_Entry(st, 4, code, &r0, 1) ? -2 : -1; }
    if (off == 0x63) { int x = RX_NarrTex(&g_ra, k, h, code, 0, 0, &ax, &ay); return x >= 0 ? x : RX_Entry(st, 0, code, NULL, 0) ? -2 : -1; }
    if (off == 0x14a || off == 0x14b) { if (!RX_Entry(st, 5, code, NULL, 0)) return -1; t = RX_Lookup(st, 5, code, &r0, 1); }
    else { if (!RX_Entry(st, 1, code, NULL, 0)) return -1; t = RX_Lookup(st, 1, code, NULL, 0); if (!t) t = RX_Lookup(st, 1, code, &r0, 1); }
    if (t) { int x = RX_TexIn(&g_ra, st, t); if (x >= 0) return x; }
    return -2; }
static HBITMAP ThumbMake(int tx)   /* texture tx fitted into PK_TS x PK_TS on a chequer (the see-through parts) */
{   BITMAPINFO bi; unsigned *d = NULL, *px = NULL; HBITMAP b; int w = 0, h = 0, x, y; HDC dc = GetDC(NULL);
    memset(&bi, 0, sizeof bi); bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = PK_TS; bi.bmiHeader.biHeight = -PK_TS; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    b = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&d, NULL, 0); ReleaseDC(NULL, dc); if (!b || !d) return b;
    for (x = 0; x < PK_TS * PK_TS; x++) d[x] = (((x % PK_TS) / 7 + (x / PK_TS) / 7) & 1) ? 0xffd6d6d6 : 0xffbcbcbc;
    if (tx >= 0 && tx < g_ra.ntex) { const RxTex *t = &g_ra.tex[tx]; px = HdPicture(t, &w, &h); if (!px) { px = RX_Rgba(&g_ra, t); w = t->w; h = t->h; } }
    if (px && w > 0 && h > 0) { double sc = (double)(PK_TS - 4) / (w > h ? w : h); int ow = (int)(w * sc), oh = (int)(h * sc), ox = (PK_TS - ow) / 2, oy = (PK_TS - oh) / 2;
        if (ow < 1) ow = 1; if (oh < 1) oh = 1;
        for (y = 0; y < oh; y++) for (x = 0; x < ow; x++) { int sx = (int)(x / sc), sy = (int)(y / sc); unsigned c; if (sx >= w) sx = w - 1; if (sy >= h) sy = h - 1;
            c = px[sy * w + sx]; if ((c >> 24) >= 128) d[(oy + y) * PK_TS + ox + x] = 0xff000000u | ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff); } }
    else { for (x = 0; x < PK_TS; x++) { d[x * PK_TS + x] = 0xff606060; d[x * PK_TS + PK_TS - 1 - x] = 0xff606060; } }   /* (no picture: a cross) */
    free(px); return b; }
static HBITMAP Thumb(int tx)
{   int i;
    if (g_thumbOf != g_ra.file) { for (i = 0; i < 8192; i++) if (g_thumb[i]) { DeleteObject(g_thumb[i]); g_thumb[i] = NULL; } g_thumbOf = g_ra.file; }
    if (tx < 0 || tx >= 8192) { if (!g_thumbNone) g_thumbNone = ThumbMake(-1); return g_thumbNone; }
    if (!g_thumb[tx]) g_thumb[tx] = ThumbMake(tx);
    return g_thumb[tx]; }
/* set field fi of the chosen pieces to v without an undo step of its own (the slider: one step per drag) */
static void PickLive(int v)
{   const Fld *f = &g_fld[g_pkRow]; int i;
    if (!g_pkUndo) { Undo *u = UndoBegin(g_sel, g_nsel); g_pkUndo = u; }
    for (i = 0; i < g_nsel; i++) { RoadSection *sc = Sec(g_sel[i], g_half); FSet(sc, f, v); Derive(sc, f->off); if (g_c.piece[g_sel[i]].layout != 2 && g_half == 0) g_c.piece[g_sel[i]].halfB = g_c.piece[g_sel[i]].halfA; }
    g_dirty = 1; Redraw(); }
static void PickLiveEnd(void) { if (g_pkUndo) { PickLiveEnd0(); PropsFill(); Redraw(); } }
static void FlagSet(int fi, int bit, int on)   /* one flag bit of field fi on / off on all the chosen pieces */
{   const Fld *f = &g_fld[fi]; int i; Undo *u; if (!g_open || !g_nsel) return;
    u = UndoBegin(g_sel, g_nsel);
    for (i = 0; i < g_nsel; i++) { RoadSection *sc = Sec(g_sel[i], g_half); int v = FGet(sc, f); FSet(sc, f, on ? v | bit : v & ~bit); Derive(sc, f->off);
        if (g_c.piece[g_sel[i]].layout != 2 && g_half == 0) g_c.piece[g_sel[i]].halfB = g_c.piece[g_sel[i]].halfA; }
    UndoEnd(u); Status("%s: %s %s on %d piece%s", f->name, BitName(f->off, 0) ? "" : "", on ? "set" : "cleared", g_nsel, g_nsel == 1 ? "" : "s"); PropsFill(); Redraw(); }
static void PickLayout(void)
{   RECT rc; int W, H, i; if (!g_pick) return; GetClientRect(g_pick, &rc); W = rc.right; H = rc.bottom;
    MoveWindow(g_pkInfo, 8, 6, W - 16, 72, TRUE);
    MoveWindow(g_pkIcons, 8, 82, W - 16, H - 82 - 40, TRUE); MoveWindow(g_pkBox, 8, 82, W - 16, H - 82 - 40, TRUE);
    for (i = 0; i < 8; i++) MoveWindow(g_pkBits[i], 12, 86 + i * 28, W - 24, 24, TRUE);
    MoveWindow(g_pkTrack, 8, 86, W - 16, 34, TRUE); MoveWindow(g_pkEdit, 8, 128, 160, 24, TRUE); MoveWindow(g_pkSet, 176, 127, 90, 26, TRUE);
    MoveWindow(g_pkClose, W - 98, H - 34, 90, 28, TRUE); }
static void PickFill(void)   /* the picker for the field g_pkRow of the choice (again after any change) */
{   const Fld *f; int k, h, v, mixed = 0, j, kind; char t[600];
    if (!g_pick || !IsWindowVisible(g_pick) || g_pkRow < 0 || g_pkRow >= NFLD || !g_open || !g_nsel) return;
    if (g_pkUndo) return;   /* (while the slider is dragged: as it is) */
    f = &g_fld[g_pkRow]; k = g_sel[0]; h = g_half && g_c.piece[k].layout == 2; v = FGet(Sec(k, g_half), f); kind = PickKindOf(f->off);
    for (j = 1; j < g_nsel; j++) if (FGet(Sec(g_sel[j], g_half), f) != v) mixed = 1;
    g_pkSync = 1;
    if (g_pkBrush) sprintf(t, "Paint brush: %s (the values piece %d's sets offer) - a choice sets the brush", g_brush[g_brushI].name, k); else
    sprintf(t, "%s - piece %d%s", f->name, k, g_nsel > 1 ? " and the other chosen pieces" : ""); SetWindowTextA(g_pick, t);
    if (mixed) sprintf(t, "%s\r\nNow: several values (the chosen pieces differ)", f->help); else sprintf(t, "%s\r\nNow: %d", f->help, v);
    if (kind == PK_THUMB) strcat(t, "   -   click a picture to use it");
    SetWindowTextA(g_pkInfo, t);
    ShowWindow(g_pkIcons, kind == PK_THUMB ? SW_SHOW : SW_HIDE); ShowWindow(g_pkBox, kind == PK_KIND ? SW_SHOW : SW_HIDE);
    for (j = 0; j < 8; j++) ShowWindow(g_pkBits[j], kind == PK_FLAGS && (f->type == 'b' || j < 8) ? SW_SHOW : SW_HIDE);
    ShowWindow(g_pkTrack, kind == PK_NUM ? SW_SHOW : SW_HIDE); ShowWindow(g_pkEdit, kind == PK_NUM ? SW_SHOW : SW_HIDE); ShowWindow(g_pkSet, kind == PK_NUM ? SW_SHOW : SW_HIDE);
    if (kind == PK_THUMB) {
        if (g_pkFor[0] != g_pkRow || g_pkFor[1] != k || g_pkFor[2] != h || g_thumbOf != g_ra.file) {   /* the pictures this piece's texture sets offer */
            unsigned code; LVITEMA it; int n = 0;
            SetCursor(LoadCursor(NULL, IDC_WAIT));
            ListView_DeleteAllItems(g_pkIcons); if (g_pkImg) ImageList_Destroy(g_pkImg);
            g_pkImg = ImageList_Create(PK_TS, PK_TS, ILC_COLOR32, 64, 32); ListView_SetImageList(g_pkIcons, g_pkImg, LVSIL_NORMAL);
            if (f->off >= 0x148 && f->off <= 0x14b) { memset(&it, 0, sizeof it); it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM; it.iItem = n++; it.pszText = "255  none"; it.lParam = 255; it.iImage = ImageList_Add(g_pkImg, Thumb(-1), NULL); ListView_InsertItem(g_pkIcons, &it); }
            for (code = 0; code < 255; code++) { int tx = PickTex(f->off, code, k, h); char lb[40];
                if (tx == -1) continue;
                sprintf(lb, "%u  %.8s", code, tx >= 0 ? g_ra.tex[tx].name : "(no picture)");
                memset(&it, 0, sizeof it); it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM; it.iItem = n++; it.pszText = lb; it.lParam = (LPARAM)code; it.iImage = ImageList_Add(g_pkImg, Thumb(tx), NULL);
                ListView_InsertItem(g_pkIcons, &it); }
            g_pkFor[0] = g_pkRow; g_pkFor[1] = k; g_pkFor[2] = h; SetCursor(LoadCursor(NULL, IDC_ARROW));
            if (!n) SetWindowTextA(g_pkInfo, "This piece's texture sets offer nothing for this field."); }
        {   int i, n = ListView_GetItemCount(g_pkIcons); ListView_SetItemState(g_pkIcons, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            for (i = 0; i < n && !mixed; i++) { LVITEMA it; memset(&it, 0, sizeof it); it.mask = LVIF_PARAM; it.iItem = i; ListView_GetItem(g_pkIcons, &it);
                if ((int)it.lParam == v) { ListView_SetItemState(g_pkIcons, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); ListView_EnsureVisible(g_pkIcons, i, FALSE); break; } } } }
    else g_pkFor[0] = -1;
    if (kind == PK_KIND) SendMessageA(g_pkBox, LB_SETCURSEL, mixed || v > 5 ? -1 : v, 0);
    if (kind == PK_FLAGS) for (j = 0; j < 8; j++) { int all = 1, any = 0, i; for (i = 0; i < g_nsel; i++) { int b = (FGet(Sec(g_sel[i], g_half), f) >> j) & 1; all &= b; any |= b; }
        SetWindowTextA(g_pkBits[j], BitName(f->off, j)); SendMessageA(g_pkBits[j], BM_SETCHECK, all ? BST_CHECKED : any ? BST_INDETERMINATE : BST_UNCHECKED, 0); }
    if (kind == PK_NUM) {   /* the slider: the values the course uses for this field, and a quarter more each way */
        int lo = v, hi = v, i, sp; char e[32];
        for (i = 0; i < g_c.n; i++) { int x = FGet(&g_c.piece[i].halfA, f), y = FGet(&g_orig[i].halfA, f); if (x < lo) lo = x; if (x > hi) hi = x; if (y < lo) lo = y; if (y > hi) hi = y; }
        sp = hi - lo; if (sp < 16) sp = 16; lo -= sp / 4; hi += sp / 4; if (f->type == 'b') { if (lo < 0) lo = 0; if (hi > 255) hi = 255; }
        SendMessageA(g_pkTrack, TBM_SETRANGEMIN, FALSE, lo); SendMessageA(g_pkTrack, TBM_SETRANGEMAX, FALSE, hi); SendMessageA(g_pkTrack, TBM_SETTICFREQ, (hi - lo) / 20 > 0 ? (hi - lo) / 20 : 1, 0);
        SendMessageA(g_pkTrack, TBM_SETPAGESIZE, 0, (hi - lo) / 20 > 0 ? (hi - lo) / 20 : 1); SendMessageA(g_pkTrack, TBM_SETPOS, TRUE, v);
        sprintf(e, "%d", v); SetWindowTextA(g_pkEdit, mixed ? "" : e);
        sprintf(t, "%s\r\nNow: %s   (the slider: %d .. %d; drag it and watch the 3D view.  Or type a value, \"+=n\" or \"a..b\" (a ramp over the chosen pieces) and Set)", f->help, mixed ? "several values" : e, lo, hi);
        SetWindowTextA(g_pkInfo, t); }
    g_pkSync = 0; }
static LRESULT CALLBACK PickProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{   switch (m) {
    case WM_SIZE: PickLayout(); return 0;
    case WM_CLOSE: PickLiveEnd(); ShowWindow(w, SW_HIDE); return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDP_CLOSE) { PickLiveEnd(); ShowWindow(w, SW_HIDE); return 0; }
        if (LOWORD(wp) == IDP_SET || LOWORD(wp) == IDOK) { char t[64]; GetWindowTextA(g_pkEdit, t, sizeof t); if (g_pkBrush) { SetWindowTextA(g_brushVal, t); Status("Brush: %s = %s", g_brush[g_brushI].name, t); } else ApplyField(g_pkRow, t); return 0; }
        if (LOWORD(wp) == IDP_BOX && HIWORD(wp) == LBN_SELCHANGE && !g_pkSync) { int i = (int)SendMessageA(g_pkBox, LB_GETCURSEL, 0, 0); char t[16]; if (i >= 0) { sprintf(t, "%d", i); ApplyField(g_pkRow, t); } return 0; }
        if (LOWORD(wp) >= IDP_BIT0 && LOWORD(wp) < IDP_BIT0 + 8 && HIWORD(wp) == BN_CLICKED && !g_pkSync) { int b = LOWORD(wp) - IDP_BIT0;
            FlagSet(g_pkRow, 1 << b, SendMessageA(g_pkBits[b], BM_GETCHECK, 0, 0) != BST_CHECKED); return 0; }
        break;
    case WM_HSCROLL:
        if ((HWND)lp == g_pkTrack && !g_pkSync) { int pos = (int)SendMessageA(g_pkTrack, TBM_GETPOS, 0, 0); char t[32];
            if (LOWORD(wp) == TB_ENDTRACK) { PickLiveEnd(); return 0; }
            sprintf(t, "%d", pos); SetWindowTextA(g_pkEdit, t); if (g_pkBrush) { SetWindowTextA(g_brushVal, t); return 0; } PickLive(pos); Status("%s = %d on %d piece%s (release: done)", g_fld[g_pkRow].name, pos, g_nsel, g_nsel == 1 ? "" : "s"); }
        return 0;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        if (n->hwndFrom == g_pkIcons && n->code == LVN_ITEMCHANGED && !g_pkSync) { NMLISTVIEW *l = (NMLISTVIEW *)lp; char t[16];
            if ((l->uNewState & LVIS_SELECTED) && !(l->uOldState & LVIS_SELECTED)) { LVITEMA it; memset(&it, 0, sizeof it); it.mask = LVIF_PARAM; it.iItem = l->iItem; ListView_GetItem(g_pkIcons, &it);
                sprintf(t, "%d", (int)it.lParam);
                if (g_pkBrush) { SetWindowTextA(g_brushVal, t); Status("Brush: %s = %s - paint the sides in the 3D view", g_brush[g_brushI].name, t); }
                else if (FGet(Sec(g_sel[0], g_half), &g_fld[g_pkRow]) != (int)it.lParam || g_nsel > 1) ApplyField(g_pkRow, t); } }
        break; } }
    return DefWindowProcA(w, m, wp, lp); }
static void PickOpen(int row)
{   HINSTANCE inst = GetModuleHandleA(NULL); int i;
    if (!g_open || row < 0 || row >= NFLD) return;
    if (!g_pick) { RECT r; GetWindowRect(g_props, &r);
        g_pick = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, "CEPick", "", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, r.left - 640, r.top + 40, 630, 560, g_main, NULL, inst, NULL);
        g_pkInfo = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_pick, (HMENU)IDP_INFO, inst, NULL);
        g_pkIcons = CreateWindowA(WC_LISTVIEWA, "", WS_CHILD | WS_BORDER | WS_TABSTOP | LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | LVS_AUTOARRANGE, 0, 0, 10, 10, g_pick, (HMENU)IDP_ICONS, inst, NULL);
        ListView_SetExtendedListViewStyle(g_pkIcons, LVS_EX_DOUBLEBUFFER); ListView_SetIconSpacing(g_pkIcons, PK_TS + 26, PK_TS + 34);
        g_pkBox = CreateWindowA("LISTBOX", "", WS_CHILD | WS_BORDER | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 10, 10, g_pick, (HMENU)IDP_BOX, inst, NULL);
        {   static const char *const kn[6] = { "0   open road: verges and kerbs; in the city sidewalks and buildings", "1   walls or cliffs", "2   walls or cliffs (second style)", "3   walls and a tunnel arch", "4   terrain profile (the hills beside the road: profile points)", "5   side structures" };
            for (i = 0; i < 6; i++) SendMessageA(g_pkBox, LB_ADDSTRING, 0, (LPARAM)kn[i]); }
        for (i = 0; i < 8; i++) g_pkBits[i] = CreateWindowA("BUTTON", "", WS_CHILD | WS_TABSTOP | BS_3STATE, 0, 0, 10, 10, g_pick, (HMENU)(INT_PTR)(IDP_BIT0 + i), inst, NULL);
        g_pkTrack = CreateWindowA(TRACKBAR_CLASSA, "", WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 0, 0, 10, 10, g_pick, (HMENU)IDP_TRACK, inst, NULL);
        g_pkEdit = CreateWindowA("EDIT", "", WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, g_pick, (HMENU)IDP_EDIT, inst, NULL);
        g_pkSet = CreateWindowA("BUTTON", "Set", WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 10, 10, g_pick, (HMENU)IDP_SET, inst, NULL);
        g_pkClose = CreateWindowA("BUTTON", "Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10, g_pick, (HMENU)IDP_CLOSE, inst, NULL);
        {   HWND c[] = { g_pkInfo, g_pkIcons, g_pkBox, g_pkTrack, g_pkEdit, g_pkSet, g_pkClose }; for (i = 0; i < (int)(sizeof c / sizeof c[0]); i++) SendMessageA(c[i], WM_SETFONT, (WPARAM)g_font, 0);
            for (i = 0; i < 8; i++) SendMessageA(g_pkBits[i], WM_SETFONT, (WPARAM)g_font, 0); }
        PickLayout(); }
    PickLiveEnd(); g_pkBrush = 0;
    g_pkRow = row; g_pkFor[0] = -1;
    ShowWindow(g_pick, SW_SHOWNOACTIVATE); PickFill(); }
static void OpenFromCombos(void)
{   int t = (int)SendMessageA(g_cbTrack, CB_GETCURSEL, 0, 0) + 1, l = (int)SendMessageA(g_cbLevel, CB_GETCURSEL, 0, 0) + 1;
    if (g_dirty && MessageBoxA(g_main, "The course has unsaved edits. Open another and lose them?", "Course Editor", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    g_gameView = 0; SendMessageA(g_gameChk, BM_SETCHECK, BST_UNCHECKED, 0); ViewerStop();
    SetCursor(LoadCursor(NULL, IDC_WAIT)); OpenCourse(t, l); TexReset(0); SetCursor(LoadCursor(NULL, IDC_ARROW)); Redraw(); SelectPiece(g_focus, 0); }
static LRESULT CALLBACK MainProc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{   switch (m) {
    case WM_SIZE: if (g_list) Layout(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OPEN: OpenFromCombos(); return 0;
        case ID_SAVE: Save(); return 0;
        case ID_UNDO: UndoStep(0); PropsFill(); Redraw(); return 0;
        case ID_REDO: UndoStep(1); PropsFill(); Redraw(); return 0;
        case ID_REVSEL: RevertSel(); return 0;
        case ID_REVALL: RevertAll(); return 0;
        case ID_COPY: if (g_open && g_nsel) { g_clip = g_c.piece[g_sel[0]]; g_haveClip = 1; Status("piece %d copied", g_sel[0]); } return 0;
        case ID_PASTE: Paste(); return 0;
        case ID_DRIVE: TestDrive(); return 0;
        case ID_GAMEVIEW: if (SendMessageA(g_gameChk, BM_GETCHECK, 0, 0) == BST_CHECKED) ViewerStart(); else { ViewerStop(); g_meshDirty = 1; Status("3D view: the editor's own (the game's renderer stopped)"); } return 0;
        case ID_REGEN: SendMessageA(g_gameChk, BM_SETCHECK, BST_CHECKED, 0); ViewerStart(); return 0;
        case ID_TSAVE: TplSave(); return 0;
        case ID_TPASTE: TplPaste(); return 0;
        case ID_TDEL: TplDelete(); return 0;
        case ID_PAINT: g_paint = SendMessageA(g_paintChk, BM_GETCHECK, 0, 0) == BST_CHECKED;
            Status(g_paint ? "Paint: click / drag over the grid squares beside the road (the road itself is left alone).  Brush: %s - Pick... for its values" : "Paint off: clicks choose pieces again", g_brush[g_brushI].name); return 0;
        case ID_BRUSH: if (HIWORD(wp) == CBN_SELCHANGE) { g_brushI = (int)SendMessageA(g_cbBrush, CB_GETCURSEL, 0, 0); if (g_brushI < 0 || g_brushI > 4) g_brushI = 0;
                if (g_open && g_nsel) { char t[16]; RoadSection *s = Sec(g_focus, g_half); const Brush *b = &g_brush[g_brushI]; unsigned char *q = (unsigned char *)s + b->off;   /* (its value on the focus piece's left side to start with) */
                    sprintf(t, "%d", b->size == 1 ? *q : *(int *)q); SetWindowTextA(g_brushVal, t); }
                Status("Brush: %s - type a value or Pick..., then paint", g_brush[g_brushI].name); } return 0;
        case ID_BPICK: { int fi; for (fi = 0; fi < NFLD && g_fld[fi].off != g_brush[g_brushI].off; fi++) ; if (fi < NFLD) { PickOpen(fi); g_pkBrush = 1; g_pkFor[0] = -1; PickFill(); } } return 0;
        case ID_MARKS: g_showMarks = SendMessageA(g_marksChk, BM_GETCHECK, 0, 0) == BST_CHECKED; return 0;
        case ID_PART: if (HIWORD(wp) == CBN_SELCHANGE) { g_part = (int)SendMessageA(g_cbPart, CB_GETCURSEL, 0, 0) & 3; Status("Part: %s - what Paste and Save chosen as template carry (in the 3D view: click the road = the whole piece, left / right of it = that side)", g_partName[g_part]); } return 0;
        case ID_TPL: if (HIWORD(wp) == CBN_DROPDOWN) TplList(NULL); return 0;
        case ID_HALF: g_half = SendMessageA(g_half2, BM_GETCHECK, 0, 0) == BST_CHECKED; PropsFill(); Status(g_half ? "Editing carriageway B (the second road of a split; single roads: A)" : "Editing carriageway A"); return 0; }
        break;
    case WM_NOTIFY: {
        NMHDR *n = (NMHDR *)lp;
        if (n->hwndFrom == g_list && n->code == LVN_GETDISPINFOA) { NMLVDISPINFOA *d = (NMLVDISPINFOA *)lp; static char t[64]; int k = d->item.iItem; RoadSection *s;
            if (!(d->item.mask & LVIF_TEXT) || !g_open || k < 0 || k >= g_c.n) return 0;
            s = &g_c.piece[k].halfA;
            switch (d->item.iSubItem) {
            case 0: sprintf(t, "%d%s%s%s", k, k == g_c.start ? " S" : k == g_c.finish ? " F" : "", g_haveGame && g_gameDiff && g_gameDiff[k] ? " G" : "", memcmp((unsigned char *)&g_c.piece[k] + 0x30 + RRC_FROM, (unsigned char *)&g_orig[k] + 0x30 + RRC_FROM, RRC_SECSIZE - RRC_FROM) || memcmp(&g_c.piece[k].halfB, &g_orig[k].halfB, RRC_SECSIZE) ? " *" : ""); break;
            case 1: sprintf(t, "%s%s", KindName(s->kind), g_c.piece[k].layout == 2 ? " (split)" : ""); break;
            case 2: sprintf(t, "%d", s->curve); break;
            case 3: sprintf(t, "%d", s->hill); break;
            case 4: sprintf(t, "%d + %d", s->lanesL, s->lanesR); break;
            case 5: sprintf(t, "%d", s->width); break;
            default: t[0] = 0; }
            d->item.pszText = t; return 0; }
        if (n->hwndFrom == g_list && (n->code == LVN_ITEMCHANGED || n->code == LVN_ODSTATECHANGED)) { if (!g_bulk) SelFromList(); return 0; }
        if (n->hwndFrom == g_props && n->code == NM_DBLCLK) { PickOpen(ListView_GetNextItem(g_props, -1, LVNI_SELECTED)); return 0; }
        if (n->hwndFrom == g_props && n->code == NM_RETURN) { EditStart(ListView_GetNextItem(g_props, -1, LVNI_SELECTED)); return 0; }
        if (n->hwndFrom == g_props && n->code == LVN_ITEMCHANGED) { int r = ListView_GetNextItem(g_props, -1, LVNI_SELECTED);
            if (r >= 0 && r < NFLD) { Status("%s: %s   (double-click: pick it visually; Enter: type it)", g_fld[r].name, g_fld[r].help); if (g_pick && IsWindowVisible(g_pick) && r != g_pkRow) PickOpen(r); } return 0; }
        break; }
    case WM_CLOSE:
        if (g_dirty) { int r = MessageBoxA(w, "Save the edits before closing?", "Course Editor", MB_YESNOCANCEL | MB_ICONQUESTION); if (r == IDCANCEL) return 0; if (r == IDYES) Save(); }
        DestroyWindow(w); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0; }
    return DefWindowProcA(w, m, wp, lp); }

/* a crash: <data>\courseedit_crash.dmp (a minidump: dbghelp, loaded when needed) and the address in courseedit.log */
typedef BOOL (WINAPI *MiniDumpFn)(HANDLE, DWORD, HANDLE, int, void *, void *, void *);
static LONG WINAPI Crashed(EXCEPTION_POINTERS *ep)
{   char p[MAX_PATH]; HANDLE f; HMODULE dh = LoadLibraryA("dbghelp.dll"); MiniDumpFn md = dh ? (MiniDumpFn)GetProcAddress(dh, "MiniDumpWriteDump") : NULL;
    struct { DWORD tid; EXCEPTION_POINTERS *ep; BOOL client; } mei;
    LOG("CRASH: exception %08lx at %p (CourseEdit.exe base %p)", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress, (void *)GetModuleHandleA(NULL));
    _snprintf(p, sizeof p - 1, "%s\\courseedit_crash.dmp", g_dataDir); p[sizeof p - 1] = 0;
    if (md && (f = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL)) != INVALID_HANDLE_VALUE) {
        mei.tid = GetCurrentThreadId(); mei.ep = ep; mei.client = FALSE;
        md(GetCurrentProcess(), GetCurrentProcessId(), f, 0x2 | 0x40, &mei, NULL, NULL); CloseHandle(f); }   /* (data segments + indirect memory) */
    return EXCEPTION_CONTINUE_SEARCH; }
static void Defaults(void)   /* the data folder and the game next to the editor: analysis\build\editor -> analysis\build\play\OpenRash.exe, <repo>\data */
{   char exe[MAX_PATH], *p; GetModuleFileNameA(NULL, exe, sizeof exe); if ((p = strrchr(exe, '\\')) != NULL) *p = 0;
    _snprintf(g_gameExe, sizeof g_gameExe - 1, "%s\\..\\play\\OpenRash.exe", exe);
    if (GetFileAttributesA(g_gameExe) == INVALID_FILE_ATTRIBUTES) _snprintf(g_gameExe, sizeof g_gameExe - 1, "%s\\OpenRash.exe", exe);
    _snprintf(g_dataDir, sizeof g_dataDir - 1, "%s\\..\\..\\..\\data", exe);
    if (GetFileAttributesA(g_dataDir) == INVALID_FILE_ATTRIBUTES) _snprintf(g_dataDir, sizeof g_dataDir - 1, "%s", exe);
    GetFullPathNameA(g_gameExe, MAX_PATH, exe, NULL); strcpy(g_gameExe, exe); GetFullPathNameA(g_dataDir, MAX_PATH, exe, NULL); strcpy(g_dataDir, exe); }

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{   WNDCLASSA wc; MSG msg; int i, argc = __argc; char **argv = __argv; LVCOLUMNA col; INITCOMMONCONTROLSEX ic; int x;
    (void)prev; (void)cmd;
    Defaults();
    for (i = 1; i + 1 < argc; i++) {
        if (!_stricmp(argv[i], "-data")) { GetFullPathNameA(argv[++i], MAX_PATH, g_dataDir, NULL); }
        else if (!_stricmp(argv[i], "-game")) { GetFullPathNameA(argv[++i], MAX_PATH, g_gameExe, NULL); }
        else if (!_stricmp(argv[i], "-track")) g_track = atoi(argv[++i]);
        else if (!_stricmp(argv[i], "-level")) g_level = atoi(argv[++i]);
        else if (!_stricmp(argv[i], "-pick")) g_pickArg = argv[++i];
        else if (!_stricmp(argv[i], "-gamerenderer")) g_startViewer = atoi(argv[++i]);
        else if (!_stricmp(argv[i], "-piece")) g_startPiece = atoi(argv[++i]);
        else if (!_stricmp(argv[i], "-testpaint") && i + 3 < argc) { g_testPaint[0] = atoi(argv[++i]); g_testPaint[1] = atoi(argv[++i]); g_testPaint[2] = atoi(argv[++i]); if (i + 1 < argc && argv[i + 1][0] != '-') g_testPaint[3] = atoi(argv[++i]); }
        else if (!_stricmp(argv[i], "-testclick") && i + 2 < argc) { g_testClick[0] = atoi(argv[++i]); g_testClick[1] = atoi(argv[++i]); } }
    if (g_track < 1 || g_track > 5) g_track = 1; if (g_level < 1 || g_level > 5) g_level = 1;
    SetCurrentDirectoryA(g_dataDir);
    { char lp[MAX_PATH]; sprintf(lp, "%s\\courseedit.log", g_dataDir); g_log = fopen(lp, "w"); }
    SetUnhandledExceptionFilter(Crashed);
    ic.dwSize = sizeof ic; ic.dwICC = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES; InitCommonControlsEx(&ic);
    g_font = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    memset(&wc, 0, sizeof wc); wc.hInstance = inst; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpfnWndProc = MainProc; wc.lpszClassName = "CEMain"; wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); RegisterClassA(&wc);
    wc.lpfnWndProc = PanelProc; wc.lpszClassName = "CEPanel"; wc.hbrBackground = NULL; RegisterClassA(&wc);
    wc.lpfnWndProc = DefWindowProcA; wc.lpszClassName = "CEView"; RegisterClassA(&wc);
    wc.lpfnWndProc = PickProc; wc.lpszClassName = "CEPick"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassA(&wc);
    g_main = CreateWindowA("CEMain", "Open Rash Course Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1700, 1000, NULL, NULL, inst, NULL);
    x = 6;
    g_cbTrack = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, x, 6, 190, 300, g_main, (HMENU)ID_TRACK, inst, NULL); x += 196;
    g_cbLevel = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, x, 6, 90, 300, g_main, (HMENU)ID_LEVEL, inst, NULL); x += 96;
    { static const char *const tn[5] = { "1  The City", "2  Sierra Nevada", "3  The Peninsula", "4  Napa Valley", "5  Pacific Coast Highway" }; char t[16];
      for (i = 0; i < 5; i++) { SendMessageA(g_cbTrack, CB_ADDSTRING, 0, (LPARAM)tn[i]); sprintf(t, "Level %d", i + 1); SendMessageA(g_cbLevel, CB_ADDSTRING, 0, (LPARAM)t); }
      SendMessageA(g_cbTrack, CB_SETCURSEL, g_track - 1, 0); SendMessageA(g_cbLevel, CB_SETCURSEL, g_level - 1, 0);
      SendMessageA(g_cbTrack, WM_SETFONT, (WPARAM)g_font, 0); SendMessageA(g_cbLevel, WM_SETFONT, (WPARAM)g_font, 0); }
    Button("Open", ID_OPEN, x, 60); x += 64; Button("Save  Ctrl+S", ID_SAVE, x, 96); x += 100; Button("Undo", ID_UNDO, x, 56); x += 60; Button("Redo", ID_REDO, x, 56); x += 64;
    Button("Copy piece", ID_COPY, x, 84); x += 88; Button("Paste onto chosen", ID_PASTE, x, 128); x += 136;
    Button("Revert chosen", ID_REVSEL, x, 104); x += 108; Button("Revert course", ID_REVALL, x, 104); x += 112;
    g_half2 = CreateWindowA("BUTTON", "Edit split road B", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, 7, 136, 24, g_main, (HMENU)ID_HALF, inst, NULL); SendMessageA(g_half2, WM_SETFONT, (WPARAM)g_font, 0); x += 144;
    Button("Test drive  F5", ID_DRIVE, x, 116); x += 124;
    g_gameChk = CreateWindowA("BUTTON", "Game renderer", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, 7, 124, 24, g_main, (HMENU)ID_GAMEVIEW, inst, NULL); SendMessageA(g_gameChk, WM_SETFONT, (WPARAM)g_font, 0); x += 128;
    Button("Reload game  F6", ID_REGEN, x, 124);
    {   HWND l = CreateWindowA("STATIC", "Templates:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 6, 42, 78, 20, g_main, NULL, inst, NULL); SendMessageA(l, WM_SETFONT, (WPARAM)g_font, 0);   /* the second row: templates */
        g_cbTpl = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL | CBS_SORT, 90, 38, 240, 300, g_main, (HMENU)ID_TPL, inst, NULL); SendMessageA(g_cbTpl, WM_SETFONT, (WPARAM)g_font, 0);
        {   HWND b; b = CreateWindowA("BUTTON", "Save chosen as template", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 338, 37, 176, 26, g_main, (HMENU)ID_TSAVE, inst, NULL); SendMessageA(b, WM_SETFONT, (WPARAM)g_font, 0);
            b = CreateWindowA("BUTTON", "Paste template  Ctrl+T", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 518, 37, 170, 26, g_main, (HMENU)ID_TPASTE, inst, NULL); SendMessageA(b, WM_SETFONT, (WPARAM)g_font, 0);
            b = CreateWindowA("BUTTON", "Delete template", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 692, 37, 120, 26, g_main, (HMENU)ID_TDEL, inst, NULL); SendMessageA(b, WM_SETFONT, (WPARAM)g_font, 0);
            b = CreateWindowA("STATIC", "Part:", WS_CHILD | WS_VISIBLE | SS_RIGHT, 820, 42, 40, 20, g_main, NULL, inst, NULL); SendMessageA(b, WM_SETFONT, (WPARAM)g_font, 0);   /* what Paste / templates carry */
            g_cbPart = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 866, 38, 170, 200, g_main, (HMENU)ID_PART, inst, NULL); SendMessageA(g_cbPart, WM_SETFONT, (WPARAM)g_font, 0);
            SendMessageA(g_cbPart, CB_ADDSTRING, 0, (LPARAM)"Whole piece"); SendMessageA(g_cbPart, CB_ADDSTRING, 0, (LPARAM)"Left side"); SendMessageA(g_cbPart, CB_ADDSTRING, 0, (LPARAM)"Right side"); SendMessageA(g_cbPart, CB_ADDSTRING, 0, (LPARAM)"Both sides");
            SendMessageA(g_cbPart, CB_SETCURSEL, 0, 0);
            g_paintChk = CreateWindowA("BUTTON", "Paint", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 1050, 39, 60, 24, g_main, (HMENU)ID_PAINT, inst, NULL); SendMessageA(g_paintChk, WM_SETFONT, (WPARAM)g_font, 0);   /* the paint brush */
            g_cbBrush = CreateWindowA("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 1112, 38, 124, 200, g_main, (HMENU)ID_BRUSH, inst, NULL); SendMessageA(g_cbBrush, WM_SETFONT, (WPARAM)g_font, 0);
            { int bi; for (bi = 0; bi < 5; bi++) SendMessageA(g_cbBrush, CB_ADDSTRING, 0, (LPARAM)g_brush[bi].name); } SendMessageA(g_cbBrush, CB_SETCURSEL, 0, 0);
            g_brushVal = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 1240, 39, 56, 24, g_main, (HMENU)ID_BVAL, inst, NULL); SendMessageA(g_brushVal, WM_SETFONT, (WPARAM)g_font, 0);
            b = CreateWindowA("BUTTON", "Pick...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 1300, 37, 64, 26, g_main, (HMENU)ID_BPICK, inst, NULL); SendMessageA(b, WM_SETFONT, (WPARAM)g_font, 0);
            g_marksChk = CreateWindowA("BUTTON", "Show fields", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 1376, 39, 100, 24, g_main, (HMENU)ID_MARKS, inst, NULL); SendMessageA(g_marksChk, WM_SETFONT, (WPARAM)g_font, 0);
            SendMessageA(g_marksChk, BM_SETCHECK, BST_CHECKED, 0); } }
    g_statusw = CreateWindowA(STATUSCLASSNAMEA, "", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, g_main, (HMENU)ID_STATUS, inst, NULL); SendMessageA(g_statusw, WM_SETFONT, (WPARAM)g_font, 0);
    g_list = CreateWindowA(WC_LISTVIEWA, "", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_OWNERDATA | LVS_SHOWSELALWAYS, 0, 0, 10, 10, g_main, (HMENU)ID_LIST, inst, NULL);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER); SendMessageA(g_list, WM_SETFONT, (WPARAM)g_font, 0);
    { static const char *const h[6] = { "Piece", "Kind", "Curve", "Hill", "Lanes", "Width" }; static const int cw[6] = { 66, 90, 46, 46, 50, 50 };
      for (i = 0; i < 6; i++) { memset(&col, 0, sizeof col); col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = (char *)h[i]; col.cx = cw[i]; ListView_InsertColumn(g_list, i, &col); } }
    g_props = CreateWindowA(WC_LISTVIEWA, "", WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, 0, 0, 10, 10, g_main, (HMENU)ID_PROPS, inst, NULL);
    ListView_SetExtendedListViewStyle(g_props, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER); SendMessageA(g_props, WM_SETFONT, (WPARAM)g_font, 0);
    { static const char *const h[4] = { "Field", "Value", "Original", "What it is" }; static const int cw[4] = { 130, 84, 64, 600 };
      for (i = 0; i < 4; i++) { memset(&col, 0, sizeof col); col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = (char *)h[i]; col.cx = cw[i]; ListView_InsertColumn(g_props, i, &col); } }
    g_edit = CreateWindowA("EDIT", "", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 10, 10, g_props, (HMENU)ID_EDIT, inst, NULL); SendMessageA(g_edit, WM_SETFONT, (WPARAM)g_font, 0);
    g_editOld = (WNDPROC)SetWindowLongPtrA(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    g_plan = CreateWindowA("CEPanel", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_main, NULL, inst, NULL);
    g_prof = CreateWindowA("CEPanel", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_main, NULL, inst, NULL);
    g_view = CreateWindowA("CEView", "", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, g_main, NULL, inst, NULL);
    Layout(); ShowWindow(g_main, show); UpdateWindow(g_main);
    if (SDL_Init(SDL_INIT_VIDEO)) {   /* the 3D view: an SDL window round our child window, the Direct3D 11 renderer, the True 3D renderer on its device */
        SDL_PropertiesID pr = SDL_CreateProperties(); ID3D11Device *dev;
        SDL_SetPointerProperty(pr, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, g_view);
        g_sw = SDL_CreateWindowWithProperties(pr); SDL_DestroyProperties(pr);
        if (g_sw) g_ren = SDL_CreateRenderer(g_sw, "direct3d11");
        if (g_ren && (dev = (ID3D11Device *)SDL_GetPointerProperty(SDL_GetRendererProperties(g_ren), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, NULL)) != NULL) g_r3ok = R3_Init(&g_r, dev);
        if (!g_r3ok) Status("No 3D view: %s %s", SDL_GetError(), g_r.err); }
    Status("Data %s   game %s", g_dataDir, g_gameExe);
    OpenCourse(g_track, g_level); Redraw(); SelectPiece(g_startPiece >= 0 ? g_startPiece : g_focus, 0); TplList("");
    if (g_startViewer) { SendMessageA(g_gameChk, BM_SETCHECK, BST_CHECKED, 0); ViewerStart(); }
    if (g_pickArg) { int fi; for (fi = 0; fi < NFLD; fi++) if (!_stricmp(g_fld[fi].name, g_pickArg)) { ListView_SetItemState(g_props, fi, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED); PickOpen(fi); break; } }
    for (;;) {
        int busy = 0;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            if (g_pick && IsWindowVisible(g_pick) && IsDialogMessageA(g_pick, &msg)) continue;   /* (the picker: Tab, Enter = Set) */
            if ((msg.message == WM_KEYDOWN || msg.message == WM_CHAR) && msg.hwnd == g_view && (msg.wParam == 'W' || msg.wParam == 'A' || msg.wParam == 'S' || msg.wParam == 'D' || msg.wParam == 'w' || msg.wParam == 'a' || msg.wParam == 's' || msg.wParam == 'd') && GetKeyState(VK_CONTROL) >= 0) continue;   /* (WASD: the loop below) */
            if (msg.message == WM_KEYDOWN && GetForegroundWindow() == g_main && msg.hwnd != g_edit) {
                int ctrl = GetKeyState(VK_CONTROL) < 0;
                if (ctrl && msg.wParam == 'S') { Save(); continue; } if (ctrl && msg.wParam == 'Z') { UndoStep(0); PropsFill(); Redraw(); continue; }
                if (ctrl && msg.wParam == 'Y') { UndoStep(1); PropsFill(); Redraw(); continue; }
                if (ctrl && msg.wParam == 'T') { TplPaste(); continue; }
                if (ctrl && msg.wParam == 'C') { SendMessageA(g_main, WM_COMMAND, ID_COPY, 0); continue; } if (ctrl && msg.wParam == 'V') { Paste(); continue; }
                if (msg.wParam == VK_F5) { TestDrive(); continue; } if (msg.wParam == VK_F6) { SendMessageA(g_main, WM_COMMAND, ID_REGEN, 0); continue; } }
            TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (g_sw) { SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) { SetFocus(g_view); g_lsel = 1; g_cx = (int)e.button.x; g_cy = (int)e.button.y;   /* left: choose the piece there (Shift: from the anchor) */
                    if (g_paint) PaintBegin();
                    ViewPick((int)e.button.x, (int)e.button.y, !g_paint && (SDL_GetModState() & SDL_KMOD_SHIFT) != 0); }
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) { g_lsel = 0; if (g_paint) PaintEnd(); }
                else if (e.type == SDL_EVENT_MOUSE_MOTION && g_lsel && (g_paint || abs((int)e.motion.x - g_cx) >= 4 || abs((int)e.motion.y - g_cy) >= 4)) { ViewPick((int)e.motion.x, (int)e.motion.y, !g_paint); busy = 1; }   /* held: the range out to here */
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) { g_drag = 1; g_dx = (int)e.button.x; g_dy = (int)e.button.y; SetFocus(g_view); }   /* right-drag: look round */
                else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) g_drag = 0;
                else if (e.type == SDL_EVENT_MOUSE_MOTION && g_drag) { g_yaw += (float)(e.motion.x - g_dx) * 0.006f; g_pitch += (float)(e.motion.y - g_dy) * 0.004f; if (g_pitch < -0.2f) g_pitch = -0.2f; if (g_pitch > 1.5f) g_pitch = 1.5f; g_dx = (int)e.motion.x; g_dy = (int)e.motion.y; busy = 1; }
                else if (e.type == SDL_EVENT_MOUSE_WHEEL) { g_dist *= e.wheel.y > 0 ? 0.87f : 1.15f; if (g_dist < 300) g_dist = 300; if (g_dist > 60000) g_dist = 60000; busy = 1; } } }
        if (g_open && GetForegroundWindow() == g_main && GetFocus() == g_view) {   /* WASD (the 3D view has the keyboard: click it): move the camera, Shift = 4 x */
            static LARGE_INTEGER last; LARGE_INTEGER now, fq; double dt, st, fw = 0, rt = 0;
            QueryPerformanceCounter(&now); QueryPerformanceFrequency(&fq); dt = last.QuadPart ? (double)(now.QuadPart - last.QuadPart) / fq.QuadPart : 0; last = now; if (dt > 0.1) dt = 0.1;
            if (GetAsyncKeyState('W') < 0) fw += 1; if (GetAsyncKeyState('S') < 0) fw -= 1; if (GetAsyncKeyState('D') < 0) rt += 1; if (GetAsyncKeyState('A') < 0) rt -= 1;
            if (fw || rt) { st = dt * (600 + g_dist * 0.9) * (GetAsyncKeyState(VK_SHIFT) < 0 ? 4 : 1);   /* (world units this frame: faster when far out) */
                g_camS += (fw * cos(g_yaw) - rt * sin(g_yaw)) * st / CE_L; g_camU += (fw * sin(g_yaw) + rt * cos(g_yaw)) * st;   /* (along the road / across it, from the view's yaw) */
                if (g_camS < 0.5) g_camS = 0.5; if (g_camS > g_c.n - 2.5) g_camS = g_c.n - 2.5; if (g_camU < -60000) g_camU = -60000; if (g_camU > 60000) g_camU = 60000; busy = 1; } }
        ViewerPoll();
        if (!g_vwShown) Render3D();   /* (the game's window covers the panel while it shows the course) */
        if (busy3D) { busy3D = 0; busy = 1; }   /* (the progress bar moving: no waiting) */
        if (!busy) MsgWaitForMultipleObjects(0, NULL, FALSE, 30, QS_ALLINPUT); }
done:
    ViewerStop();
    if (g_log) fclose(g_log);
    return 0; }
