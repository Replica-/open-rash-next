/* road\course_edit.c - an edited course (road\course_edit.h; the Course Editor, src\editor\courseedit.c) put into the road as the game generates it.
 * rr_CourseEditApply is called by rr_RoadSectionFill once a section is filled from the course data, before its obstacles are placed and it is linked:
 * the edited values replace the generated ones, so the road, the riders, the traffic, Pseudo 3D and the True 3D extract all see the edited course. */
#include "../rr.h"
#include "../data/records.h"
#include "course_edit.h"
#include "road_types.h"
#include "road_ring.h"

static void Log(const char *s) { void (__stdcall *dl)(const char *); HMODULE sh = GetModuleHandleA("rrddraw.dll"); dl = sh ? (void (__stdcall *)(const char *))GetProcAddress(sh, "RR_DbgLog") : 0; if (dl) dl(s); }   /* (the shim's log) */
static RrcPiece *g_rrc;           /* the loaded course (NULL = none) */
static RrcExtra *g_rrcEx;         /* its extras (version 2; NULL = none) */
static RrcHeader g_rrcHdr;
static int g_rrcTrack = -1;
int rr_courseEdited;              /* 1 = this race's course comes from an edited file (render\world3d_feed.c: its own True 3D extract) */
unsigned rr_courseEditKey;        /* the file's checksum (the extract's name) */

/* the edited course file of a track (one road, every level - as the original: course_edit.h): 1 = there is one (path out) */
int rr_CourseEditPath(int track, char *out)
{
    char e[4];
    if (GetEnvironmentVariableA("RR_COURSEEDIT", e, sizeof e) > 0 && e[0] == '0') return 0;
    if (GetEnvironmentVariableA("RR_COURSEEDITFILE", out, MAX_PATH) > 0) return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;   /* (the Course Editor's game-generated view: its edits, not saved yet) */
    wsprintfA(out, "mods\\courses\\t%d.rrc", track);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

/* (re)load for the race being set up: track 1..5, level 1..5 (the same edits at every level) */
void rr_CourseEditLoad(int track, int level)
{
    char path[MAX_PATH]; HANDLE f; DWORD got = 0, size;
    if (track == g_rrcTrack && g_rrc) return;
    if (g_rrc) { HeapFree(GetProcessHeap(), 0, g_rrc); g_rrc = 0; }
    if (g_rrcEx) { HeapFree(GetProcessHeap(), 0, g_rrcEx); g_rrcEx = 0; }
    g_rrcTrack = track; rr_courseEdited = 0; rr_courseEditKey = 0;
    if (!rr_CourseEditPath(track, path)) return;
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    if (ReadFile(f, &g_rrcHdr, sizeof g_rrcHdr, &got, NULL) && got == sizeof g_rrcHdr && g_rrcHdr.magic == RRC_MAGIC && (g_rrcHdr.version >= 1 && g_rrcHdr.version <= RRC_VERSION)
        && g_rrcHdr.secSize == RRC_SECSIZE && g_rrcHdr.n > 0 && g_rrcHdr.n < 65536 && g_rrcHdr.track == track && (g_rrcHdr.level == 0 || g_rrcHdr.level == level)) {
        DWORD xsize = g_rrcHdr.version >= 2 ? (DWORD)g_rrcHdr.n * RrcExtraSize(g_rrcHdr.version) : 0; unsigned h;   /* (as in the file) */
        size = (DWORD)g_rrcHdr.n * sizeof(RrcPiece);
        g_rrc = (RrcPiece *)HeapAlloc(GetProcessHeap(), 0, size);
        if (xsize) g_rrcEx = (RrcExtra *)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)g_rrcHdr.n * sizeof(RrcExtra));
        if (g_rrc && ReadFile(f, g_rrc, size, &got, NULL) && got == size && (!xsize || (g_rrcEx && ReadFile(f, g_rrcEx, xsize, &got, NULL) && got == xsize))) {
            h = RrcHash(g_rrc, size); if (xsize) h = RrcHashMore(h, g_rrcEx, xsize);
            if (h != g_rrcHdr.checksum) { HeapFree(GetProcessHeap(), 0, g_rrc); g_rrc = 0; }
            else if (xsize) RrcExtraWiden(g_rrcEx, g_rrcHdr.n, RrcExtraSize(g_rrcHdr.version)); }
        else if (g_rrc) { HeapFree(GetProcessHeap(), 0, g_rrc); g_rrc = 0; }
        if (!g_rrc && g_rrcEx) { HeapFree(GetProcessHeap(), 0, g_rrcEx); g_rrcEx = 0; }
    }
    CloseHandle(f);
    if (g_rrc) { rr_courseEdited = 1; rr_courseEditKey = g_rrcHdr.checksum ^ 0x70000u; }   /* (its extract's name: ^ the extras' layout, version 7 - an extract made
                                                                                       from the same file before holds the old RrcExtra) */
}

/* the extras of the piece at road position pos (course_edit.h RrcExtra; NULL = none): the course file's CEXT chunk (road\road_build.c CourseExtract) */
/* an edited course's own settings (RrcHeader.spare[2..4], the Course Editor's - 0 = none): 0 traffic, percent of the usual (user, 2026-11-26 "reduce the amount of cars
   please in this map"), 1 / 2 the piece where snow starts / is full, + 1 (user: "start snow at marker so then the snow gradually builds up to the max level") */
int rr_CourseEditOpt(int i) { extern int rr_courseEdited; return rr_courseEdited && i >= 0 && i < 5 ? g_rrcHdr.spare[2 + i] : 0; }   /* (3: the horizon - a track's, 1..5; 0 its own; 4: the vehicles left out - bit n = traffic variant n) */
const RrcExtra *rr_CourseEditExtra(unsigned pos)
{
    int k;
    if (!g_rrc || !g_rrcEx) return 0;
    k = (int)(pos - (unsigned)g_rrcHdr.piece0) >> 8;
    return k >= 0 && k < g_rrcHdr.n ? &g_rrcEx[k] : 0;
}

/* pieces put in / taken out (course_edit.h): per course block the original piece (in the block) of each of its pieces now */
typedef struct CeMap { int *blk; int vlen, slen, fin0, fin1; int *src; } CeMap;
static CeMap g_map[256]; static int g_nmap; static int *g_mapCourse;
static int *g_finRec[64], g_finWas[64], g_finNow[64], g_nfin;   /* the levels' finishes (type 6 entries of a block's table) moved with their pieces: as they were / now */
int rr_ceForks;                   /* the edited course's forks made this race (CeForkEdited: the shim turns True 3D's forks on for them - RR_SetForks) */
int rr_ceMaps;                    /* blocks with a map (0: the course data is stepped as it is - render\anim_cursor.c) */

static const CeMap *MapOf(int blk) { int i; for (i = 0; i < g_nmap; i++) if ((int)g_map[i].blk == blk) return &g_map[i]; return 0; }
/* block blk's road position v (24.8, in the block as edited) -> the course data's own: a piece put in reads the end of its original (a later step
   of the course data's cursors then crosses one piece, as the original's would) */
int rr_CourseEditSrc(int blk, int v)
{
    const CeMap *m = MapOf(blk); int i;
    if (!m) return v;
    if (v >= m->vlen << 8) return (m->slen << 8) + v - (m->vlen << 8);
    if (v < 0) return v;
    i = v >> 8;
    if (i > 0 && m->src[i] == m->src[i - 1]) return (m->src[i] << 8) + 0xff;
    return (m->src[i] << 8) + (v & 0xff);
}

static void MapUndo(void)   /* (the same course set up again without being loaded again: its blocks as they were first) */
{
    int i;
    for (i = 0; i < g_nmap; i++) { CeMap *m = &g_map[i];
        if (g_mapCourse && m->blk[5] == m->vlen) { m->blk[5] = m->slen; if (m->fin1 && *(int *)(m->blk[4] + 0x2c) == m->fin1) *(int *)(m->blk[4] + 0x2c) = m->fin0; }
        HeapFree(GetProcessHeap(), 0, m->src); }
    for (i = 0; i < g_nfin; i++) if (g_mapCourse && *g_finRec[i] == g_finNow[i]) *g_finRec[i] = g_finWas[i];
    g_nmap = 0; g_nfin = 0; rr_ceMaps = 0;
}

/* block m's table of finishes, one per level (render\anim_cursor.c rr_LevelEntryFind: entries of 12 bytes from +0x14, type 6, level 1..5; the
   offset in pieces from the block's first): each to the piece it was on, as the block is now - every level, whichever is raced */
static void MapFinishes(const CeMap *m)
{
    int tbl = *(int *)(m->blk[4] + 0x24), *rec, left, i, j;
    if (!tbl) return;
    for (rec = (int *)(tbl + 0x14), left = *(int *)(tbl + 8); left > 0 && g_nfin < 64; rec += 3, left--) {
        if (*(char *)(rec + 2) != 6 || rec[0] <= 0) continue;
        for (j = 0; j < g_nfin && g_finRec[j] != rec; j++) ;   /* (a table two blocks share: once) */
        if (j < g_nfin) continue;
        for (i = 0; i < m->vlen && m->src[i] < rec[0]; i++) ;
        g_finRec[g_nfin] = rec; g_finWas[g_nfin] = rec[0]; g_finNow[g_nfin] = i > 0 ? i : 1; g_nfin++;
    }
}

/* the race set-up (RoadRingBuild, before the game prepares the course): the edited course's pieces put in / taken out into the course `course` (its
   first block - the course is a chain of blocks { 0, ?, next, prev, header, length }, a split { 1, ?, A's first, B's first } / { 2, ?, A's last,
   B's last, the block after }).  A course whose map does not fit its blocks is raced without the edits. */
/* a new fork (user, 2026-11-20: "forking the road ... allows the player and ai take it ... i should be able to join it back up again later"): pieces a .. b - 1 of
   normal block q of the main road (a >= 1) made a split road.  The block is cut in three, each part reading the same course data through its own piece map
   (rr_CourseEditSrc - as pieces put in / taken out; an edited block's own map composed in): X1 = q itself, pieces 0 .. a - 1 (the course's head stays its first
   node: even the first block can fork); A = pieces a .. b - 1; X3 = b .. end.  B = a copy of A with its own scenery tables (shared, a sprite slot one road freed
   was left stale in the other's: ScenerySpriteDraw > StripCellPick read a null entry), its curves from curveB (index 0 = piece a).  Between them a split's opening
   and closing nodes { 1 / 2, 6, A, B, the block before / after, 0x3c00, 0x10000, 0 } (as the game's own: test switch RR_CHAINLOG).  Every part has its own header
   and road rows (their heights at its ends: rows +0xc / +0xe), scenery tables (shared by two parts in sight at once, the draw state's caches went
   stale: ScenerySpriteDraw > OwnerEntriesInRange) and finish table (header +0x24, as MapFinishes: the finishes on its pieces, as offsets from its
   first).  Undone (CeForkUndo) before the course is set up again.  B's bends from the fork's first piece to its last (as the original's: B turns off at
   once); only the heights are alike for the SPLIT_SAME pieces at each end (TreePrepare copies A's slopes onto B there: SpriteColumnsCopy, row +0x11) */
#define CE_SPLIT_SAME 0x51
static struct { int *p, v; } g_fkw[1024]; static int g_nfkw;   /* (every word the forks changed - in the chain's nodes, the maps: as it was) */
static void CeW(int *p, int v) { if (g_nfkw < 1024) { g_fkw[g_nfkw].p = p; g_fkw[g_nfkw].v = *p; g_nfkw++; } *p = v; }
/* how far past a fork / before a join the ring keeps both roads (node +5; the game's TreePrepare / RoadGenBuild set it to 0x5100, 81 pieces - its look-ahead of the split):
   in between it builds only the player's road, and whatever was on the other one carried on along it at its place across that road - on A's ground between the roads
   (user, 2026-11-23: "cars materialising or coming in from nowhere from the right to the left"), and where it split again 81 pieces before the join every car and
   rider took A (one link).  The Course Editor's forks: their whole length, both roads all the way (render\scene_tree.c rr_TreePrepare; road_build.c RoadGenStep) */
#define CE_NFN 128
#define CE_FNALL 0x40000000   /* (past any position: RoadGenStep compares it with the cursor's place in the course data of the block it is in, not the fork's own pieces) */
static struct { const int *node; int len; } g_fn[CE_NFN]; static int g_nfn;
int rr_CeForkNodeLen(const int *node) { int i; for (i = 0; i < g_nfn; i++) if (g_fn[i].node == node) return g_fn[i].len; return 0; }
static void CeForkUndo(void)
{   while (g_nfkw > 0) { g_nfkw--; *g_fkw[g_nfkw].p = g_fkw[g_nfkw].v; } g_nfn = 0; }
/* a part's header: a copy of ha with its own rows (heights hs / he at its ends) and finish table (the finishes on the course data's pieces a .. b - 1) - tables:
   its own scenery tables too (B) */
static int *CeHdr(const int *ha, int lenOrig, int hs, int he, int a, int b, int tables)
{   int *h = (int *)HeapAlloc(GetProcessHeap(), 0, 0x34); unsigned char *r; int t, u;
    if (!h) return 0;
    memcpy(h, ha, 0x34);
    if ((r = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, 0x10 + 2 * lenOrig)) != NULL) { memcpy(r, (void *)ha[1], 0x10 + 2 * lenOrig); *(short *)(r + 0xc) = (short)hs; *(short *)(r + 0xe) = (short)he; h[1] = (int)r; }
    if (ha[9]) { int n = *(int *)(ha[9] + 8), *rec; unsigned sz = 0x14 + 12 * (n > 0 ? n : 0); unsigned char *f;
        if (n >= 0 && n < 4096 && (f = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, sz)) != NULL) { memcpy(f, (void *)ha[9], sz); h[9] = (int)f;
            for (rec = (int *)(f + 0x14), t = 0; t < n; t++, rec += 3) if (*(char *)(rec + 2) == 6 && rec[0] > 0) {   /* (a level's finish: on this part's pieces, else none) */
                if (rec[0] >= a && rec[0] < b) rec[0] = rec[0] - a > 0 ? rec[0] - a : 1; else *(char *)(rec + 2) = 0; } } }
    if (tables) for (t = 2; t <= 7; t++) { unsigned a0 = (unsigned)ha[t], a1 = 0xffffffffu, sz; void *cp;   /* (the tables lie one after another: each as long as the gap to the next) */
        if (!a0) continue;
        for (u = 1; u <= 9; u++) if (u != t && (unsigned)ha[u] > a0 && (unsigned)ha[u] < a1) a1 = (unsigned)ha[u];
        sz = a1 - a0; if (a1 == 0xffffffffu || sz > 0x100000) continue;
        if ((cp = HeapAlloc(GetProcessHeap(), 0, sz)) != NULL) { memcpy(cp, (void *)a0, sz); h[t] = (int)cp; } }
    return h; }
static int CeMapAdd(int *blk, const int *src, int n, int slen)   /* a part's piece map (src: the course data's own piece of each) */
{   CeMap *m; if (g_nmap >= 256) return 0; m = &g_map[g_nmap];
    if (!(m->src = (int *)HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(int)))) return 0;
    memcpy(m->src, src, n * sizeof(int)); m->blk = blk; m->vlen = n; m->slen = slen; m->fin0 = m->fin1 = 0; g_nmap++; return 1; }
/* pieces a (of block q) .. b - 1 (of the nb - 1'th normal block after q along the main road, no split between) forked - a fork over several course blocks
   (user, 2026-11-20, "go for it": forks across course blocks): q cut at a (X1 = q, its pieces before a; A's first part a new block), the blocks between are
   A as they are, the last cut at b (A's last part = that block, X3 a new block after it); B a copy of every part of A, curveB over all of them (index 0 =
   piece a).  Every word changed in the chain's nodes and maps logged (CeW) for CeForkUndo */
#define CE_FORKBLK 16
typedef struct CeFkBlk { int *q, L, Lo, *src, *hs; CeMap *m; } CeFkBlk;
static int CeFkH(const CeFkBlk *k, int i) { return k->hs[k->src[i] < k->Lo ? k->src[i] : k->Lo]; }   /* (the height before piece i of the block, as it is now) */
static int CeForkSpan(int *q, int a, int nb, int b, const signed char *curveB)
{
    CeFkBlk k[CE_FORKBLK]; int *nA[CE_FORKBLK], *nB[CE_FORKBLK], *s1 = 0, *s2 = 0, *n3 = 0, *nx, *qe, i, j, ok = 1, n;
    if (!q || q[0] != 0 || nb < 1 || nb > CE_FORKBLK || a < 1) return 0;
    memset(k, 0, sizeof k);
    for (i = 0, qe = q; i < nb; i++) { int *h;   /* the blocks: their lengths, maps */
        if (!qe || qe[0] != 0 || !(h = (int *)qe[4]) || !h[1]) return 0;
        k[i].q = qe; k[i].L = qe[5]; k[i].m = (CeMap *)MapOf((int)qe); k[i].Lo = k[i].m ? k[i].m->slen : k[i].L;
        if (i < nb - 1) qe = (int *)qe[2]; }
    nx = (int *)qe[2];
    n = nb == 1 ? b - a : k[0].L - a + b; for (i = 1; i < nb - 1; i++) n += k[i].L;   /* (the fork's length) */
    if (!nx || n < 2 * CE_SPLIT_SAME + 8 || b < 1 || b > k[nb - 1].L || a >= k[0].L) return 0;
    for (i = 0; i < nb && ok; i++) { unsigned char *rows = (unsigned char *)((int *)k[i].q[4])[1];   /* their pieces' course data, heights */
        k[i].src = (int *)HeapAlloc(GetProcessHeap(), 0, (k[i].L + 1) * sizeof(int)); k[i].hs = (int *)HeapAlloc(GetProcessHeap(), 0, (k[i].Lo + 1) * sizeof(int));
        if (!k[i].src || !k[i].hs) { ok = 0; break; }
        for (j = 0; j < k[i].L; j++) k[i].src[j] = k[i].m ? k[i].m->src[j] : j;
        k[i].src[k[i].L] = k[i].Lo;
        k[i].hs[0] = *(short *)(rows + 0xc); for (j = 0; j < k[i].Lo; j++) k[i].hs[j + 1] = k[i].hs[j] + (signed char)rows[0x11 + 2 * j]; }   /* (the slopes added up) */
    if (ok) { s1 = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32); s2 = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32); if (!s1 || !s2) ok = 0; }
    if (ok && b < k[nb - 1].L && !(n3 = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32))) ok = 0;
    for (i = 0; i < nb && ok; i++) {   /* B's parts (new blocks, own headers and tables), B's bends; A's first part a new block */
        const CeFkBlk *K = &k[i]; int *ha = (int *)K->q[4], p0 = i == 0 ? a : 0, p1 = i == nb - 1 ? b : K->L, base;
        nA[i] = i == 0 ? (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32) : K->q; nB[i] = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32);
        if (!nA[i] || !nB[i] || !(nB[i][4] = (int)CeHdr(ha, K->Lo, CeFkH(K, p0), CeFkH(K, p1), K->src[p0], K->src[p1], 1))) { ok = 0; break; }
        if (i == 0 && !(nA[0][4] = (int)CeHdr(ha, K->Lo, CeFkH(K, p0), CeFkH(K, p1), K->src[p0], K->src[p1], 1))) { ok = 0; break; }
        for (base = i == 0 ? -a : k[0].L - a, j = 1; j < i; j++) base += k[j].L;   /* (piece j of this block: the fork's piece base + j) */
        if (curveB) { unsigned char *rb = (unsigned char *)((int *)nB[i][4])[1];
            for (j = p0; j < p1; j++) { int f = base + j; if (f >= 0 && f < n && K->src[j] < K->Lo) rb[0x10 + 2 * K->src[j]] = (unsigned char)curveB[f]; } } }
    if (ok && n3) { CeFkBlk *E = &k[nb - 1]; if (!(n3[4] = (int)CeHdr((int *)E->q[4], E->Lo, CeFkH(E, b), CeFkH(E, E->L), E->src[b], E->Lo, 1))) ok = 0; }
    if (!ok) { for (i = 0; i < nb; i++) { if (k[i].src) HeapFree(GetProcessHeap(), 0, k[i].src); if (k[i].hs) HeapFree(GetProcessHeap(), 0, k[i].hs); } return 0; }
    /* the maps: A's first part, B's parts, X3 - each part's pieces' course data */
    CeMapAdd(nA[0], k[0].src + a, (nb == 1 ? b : k[0].L) - a, k[0].src[nb == 1 ? b : k[0].L]);
    for (i = 0; i < nb; i++) { const CeFkBlk *K = &k[i]; int p0 = i == 0 ? a : 0, p1 = i == nb - 1 ? b : K->L; CeMapAdd(nB[i], K->src + p0, p1 - p0, K->src[p1]); }
    if (n3) { CeFkBlk *E = &k[nb - 1]; n3[0] = 0; n3[1] = 6; n3[5] = E->L - b; CeMapAdd(n3, E->src + b, E->L - b, E->Lo); }
    /* the cuts: the last block A's last part (its pieces 0 .. b - 1), X1 = q (0 .. a - 1) - their own headers (rows, finishes), maps */
    if (nb > 1) { CeFkBlk *E = &k[nb - 1]; int *h2 = CeHdr((int *)E->q[4], E->Lo, CeFkH(E, 0), CeFkH(E, b), E->src[0], E->src[b], 1);
        if (h2) CeW(&E->q[4], (int)h2);
        if (E->m) { CeW(&E->m->vlen, b); CeW(&E->m->slen, E->src[b]); } else CeMapAdd(E->q, E->src, b, E->src[b]);
        CeW(&E->q[5], b); }
    {   CeFkBlk *K = &k[0]; int *h1 = CeHdr((int *)q[4], K->Lo, CeFkH(K, 0), CeFkH(K, a), K->src[0], K->src[a], 1);
        if (h1) CeW(&q[4], (int)h1);
        if (K->m) { CeW(&K->m->vlen, a); CeW(&K->m->slen, K->src[a]); } else CeMapAdd(q, K->src, a, K->src[a]);
        CeW(&q[5], a); }
    /* the chain: X1 > open { 1, 6, A, B, X1 } > A's parts / B's parts > close { 2, 6, A's last, B's last, X3 or the block after } > X3 > the block after */
    s1[0] = 1; s1[1] = 6; s1[2] = (int)nA[0]; s1[3] = (int)nB[0]; s1[4] = (int)q; s1[5] = 0x3c00; s1[6] = 0x10000;
    s2[0] = 2; s2[1] = 6; s2[2] = (int)nA[nb - 1]; s2[3] = (int)nB[nb - 1]; s2[4] = n3 ? (int)n3 : (int)nx; s2[5] = 0x3c00; s2[6] = 0x10000;
    if (g_nfn + 2 <= CE_NFN) { g_fn[g_nfn].node = s1; g_fn[g_nfn++].len = CE_FNALL; g_fn[g_nfn].node = s2; g_fn[g_nfn++].len = CE_FNALL; }   /* (both roads all the way: rr_CeForkNodeLen) */
    for (i = 0; i < nb; i++) { int *pa = i == 0 ? s1 : nA[i - 1], *pb = i == 0 ? s1 : nB[i - 1], *xa = i == nb - 1 ? s2 : nA[i + 1], *xb = i == nb - 1 ? s2 : nB[i + 1];
        nB[i][0] = 0; nB[i][1] = 6; nB[i][2] = (int)xb; nB[i][3] = (int)pb; nB[i][5] = (i == nb - 1 ? b : k[i].L) - (i == 0 ? a : 0);
        if (i == 0) { nA[0][0] = 0; nA[0][1] = 6; nA[0][2] = (int)xa; nA[0][3] = (int)pa; nA[0][5] = nB[0][5]; }
        else { CeW(&nA[i][2], (int)xa); CeW(&nA[i][3], (int)pa); } }
    if (n3) { n3[2] = (int)nx; n3[3] = (int)s2; }
    CeW(&nx[nx[0] == 1 ? 4 : 3], n3 ? (int)n3 : (int)s2);
    CeW(&q[2], (int)s1);
    rr_ceMaps = g_nmap;
    for (i = 0; i < nb; i++) { HeapFree(GetProcessHeap(), 0, k[i].src); HeapFree(GetProcessHeap(), 0, k[i].hs); }
    return 1;
}
/* test switch RR_FORKMAKE="<block> <bend> <quarter> [<first> <end> [<blocks>]]": the main road's normal block number <block> (0 = the first) forked from its piece
   <first> (default 1) to piece <end> - 1 of the <blocks>'th block from it (default 1: the same one; <end> 0 = that block's end) - B bent by +bend for <quarter>
   pieces, -bend for twice that, +bend again in the middle: out to the side and back (bend 1, quarter 45: ~2000 units out) */
static void CeForkMake(int *course)
{
    char e[80]; int v[6] = { -1, 1, 45, 1, 0, 1 }, i = 0, g = 0, *q = course, *qe, len, n, f0, f1, nb, j; signed char *cv;
    if (!GetEnvironmentVariableA("RR_FORKMAKE", e, sizeof e) || !q) return;
    {   int nv = 0, neg = 0, cur = 0, in = 0; const char *c;   /* (whole numbers - no sscanf here: the game is linked without stdio's) */
        for (c = e; ; c++) { if (*c == '-') neg = 1; else if (*c >= '0' && *c <= '9') { cur = cur * 10 + (*c - '0'); in = 1; } else { if (in && nv < 6) v[nv++] = neg ? -cur : cur; cur = 0; in = 0; neg = 0; if (!*c) break; } } }
    if (v[0] < 0 || v[2] < 1 || v[5] < 1 || v[5] > CE_FORKBLK) return;
    while (q && g++ < 4096) {   /* the main road's normal blocks (the splits passed along A) */
        if (q[0] == 0) { if (i++ == v[0]) break; q = (int *)q[2]; }
        else if (q[0] == 1) q = (int *)q[2]; else if (q[0] == 2) q = (int *)q[4]; else { q = 0; break; } }
    if (!q || q[0] != 0) { Log("fork make: no such block"); return; }
    for (nb = 1, qe = q, len = q[5]; nb < v[5]; nb++) { qe = (int *)qe[2]; if (!qe || qe[0] != 0) { Log("fork make: a split before the last block"); return; } len += qe[5]; }
    f0 = v[3]; f1 = v[4] > 0 ? v[4] : qe[5]; n = len - qe[5] + f1 - f0;   /* (f1: in the last block) */
    if (f0 < 1 || f0 >= q[5] || f1 > qe[5] || n < 2 * CE_SPLIT_SAME + 4 * v[2] || !(cv = (signed char *)HeapAlloc(GetProcessHeap(), 0, n))) { Log("fork make: the stretch is too short (or not in the blocks)"); return; }
    for (i = 0, qe = q, j = f0; i < n; i++, j++) { if (j >= qe[5]) { qe = (int *)qe[2]; j = 0; } cv[i] = (signed char)((unsigned char *)((int *)qe[4])[1])[0x10 + 2 * j]; }   /* (the road's own bends) */
    for (i = 0; i < 4 * v[2]; i++) { int k = CE_SPLIT_SAME + (n - 2 * CE_SPLIT_SAME - 4 * v[2]) / 2 + i, c = cv[k] + (i < v[2] || i >= 3 * v[2] ? v[1] : -v[1]); cv[k] = (signed char)(c > 127 ? 127 : c < -127 ? -127 : c); }
    {   char m[160]; wsprintfA(m, "fork make: block %d + %d more (%d pieces), from its piece %d to piece %d of the last, %d pieces: %s - B bent by %d over %d pieces", v[0], v[5] - 1, len, f0, f1 - 1, n,
            CeForkSpan(q, f0, v[5], f1, cv) ? (rr_ceForks++, "made a split road") : "NOT forked", v[1], 4 * v[2]); Log(m); }
    HeapFree(GetProcessHeap(), 0, cv);
}
/* the edited course's own forks (the Course Editor's "Fork here" / "Join here": RrcPiece.spare[1] bit 0 on the pieces of a fork, B's road in their b halves):
   each run of marked records made a fork from the main-road block it leaves to the one it joins in (CeForkSpan) - after the map (the blocks as edited) */
#define CE_NFORK 64
static int g_forkRec[2 * CE_NFORK];   /* (each fork made: its first record, the record after its last) */
/* each fork's B offsets off A per record, from the edited curves (as the editor keeps them: courseedit.c PlanForkFields - splitSlope(k) = splitSlope(k - 1) +
   B.curve - A.curve, splitX(k + 1) = splitX(k) + splitSlope(k), both from 0 at the fork), worked out when the forks are made and put on the road's pieces as they
   are built (road_build.c rr_RoadBlockBuild: rr_CeForkSplit) - the game's cursor summed the course data's rows, whose A curves are the original's, not the edited
   ones: its offsets crept off (City 128..727: 35 degrees at the join) and True 3D had to correct them every race (user, 2026-11-23: "the fork is known up front"
   / "during level creation cant it plot everything where it needs to") */
static int *g_fkSX, *g_fkSS, g_fkN;
int rr_CeForkSplit(int pos, int *sx, int *ss)
{   int r = (pos >> 8) - (g_rrcHdr.piece0 >> 8), i;
    if (!g_fkSX || r < 0 || r >= g_fkN) return 0;
    for (i = 0; i < rr_ceForks && i < CE_NFORK; i++) if (r >= g_forkRec[2 * i] && r < g_forkRec[2 * i + 1]) { *sx = g_fkSX[r]; *ss = g_fkSS[r]; return 1; }
    return 0; }
static void CeForkEdited(int *course)
{
    int r = 0, b0 = g_rrcHdr.piece0 >> 8, made = 0;
    while (r < g_rrcHdr.n) {
        int r0, r1, p0, p1, *q = course, p = 0, g = 0, ok = 0; char m[120];
        if (!(g_rrc[r].spare[1] & 1)) { r++; continue; }
        for (r0 = r; r < g_rrcHdr.n && (g_rrc[r].spare[1] & 1); r++) ; r1 = r;
        p0 = b0 + r0; p1 = b0 + r1;   /* (the course's pieces p0 .. p1 - 1) */
        while (q && g++ < 4096) {
            if (q[0] == 0) { int len = q[5], *next = (int *)q[2];
                if (p0 >= p && p0 < p + len) {   /* (the block it leaves from: on along the normal blocks after it to the one it joins in) */
                    int nb = 1, e = p + len, *qe = q; signed char *cv;
                    while (p1 > e && nb < CE_FORKBLK && qe[2] && ((int *)qe[2])[0] == 0) { qe = (int *)qe[2]; e += qe[5]; nb++; }
                    if (p1 <= e && (cv = (signed char *)HeapAlloc(GetProcessHeap(), 0, p1 - p0)) != NULL) { int i;
                        for (i = 0; i < p1 - p0; i++) cv[i] = (signed char)((const RoadSection *)g_rrc[r0 + i].b)->curve;
                        ok = CeForkSpan(q, p0 - p, nb, p1 - (e - qe[5]), cv); HeapFree(GetProcessHeap(), 0, cv); }
                    break; }
                p += len; q = next; }
            else if (q[0] == 1) { int *e2 = (int *)q[2]; while (e2 && e2[0] == 0 && g++ < 4096) { p += e2[5]; e2 = (int *)e2[2]; } q = e2 && e2[0] == 2 ? (int *)e2[4] : 0; }   /* (a split: along A to its join) */
            else if (q[0] == 2) q = (int *)q[4];
            else break; }
        if (ok && made < CE_NFORK) { g_forkRec[2 * made] = r0; g_forkRec[2 * made + 1] = r1; }   /* (the course file's records: the pieces True 3D numbers so - CeForksTell) */
        made += ok;
        wsprintfA(m, ok ? "course edit: a fork made over pieces %d..%d" : "course edit: the fork over pieces %d..%d NOT made (not on normal course blocks of the main road, or too short)", p0, p1 - 1); Log(m); }
    rr_ceForks = made;
    if (made && (g_fkN = g_rrcHdr.n, g_fkSX = (int *)HeapReAlloc(GetProcessHeap(), 0, g_fkSX ? g_fkSX : HeapAlloc(GetProcessHeap(), 0, 8), sizeof(int) * 2 * g_fkN)) != NULL) {   /* (the offsets: rr_CeForkSplit) */
        int f; g_fkSS = g_fkSX + g_fkN;
        for (f = 0; f < made && f < CE_NFORK; f++) { int k, s = 0, x = 0;
            for (k = g_forkRec[2 * f]; k < g_forkRec[2 * f + 1] && k < g_fkN; k++) { g_fkSX[k] = x; s += ((const RoadSection *)g_rrc[k].b)->curve - ((const RoadSection *)g_rrc[k].a)->curve; g_fkSS[k] = s; x += s; } } }
}
/* the shim told whether this race has the editor's forks (rr_ceForks): True 3D then has forks on - every rider free to take either road (world3d.inc RR_SetForks) -
   and which pieces they are (RR_SetForkPieces: B's offsets from its curves there, course3d.h C3_EdForkFix) */
static void CeForksTell(void)
{   static void (__stdcall *f)(int); static void (__stdcall *fp)(int, const int *); static int looked;
    if (!looked) { HMODULE sh = GetModuleHandleA("rrddraw.dll"); f = sh ? (void (__stdcall *)(int))GetProcAddress(sh, "RR_SetForks") : 0;
        fp = sh ? (void (__stdcall *)(int, const int *))GetProcAddress(sh, "RR_SetForkPieces") : 0; looked = 1; }
    if (f) f(rr_ceForks > 0);
    if (fp) fp(rr_ceForks < CE_NFORK ? rr_ceForks : CE_NFORK, g_forkRec);
    if (GetEnvironmentVariableA("RR_FORKMAKE", 0, 0)) { char m[80]; wsprintfA(m, "fork tell: %d fork(s), shim %d", rr_ceForks, f != 0); Log(m); } }

/* the track's own forks taken out (RrcHeader.spare[7] bit 0, the Course Editor's port of a course onto another track - user, 2026-11-27 "replace those forks
   compleetely please": Sierra's two split roads under a course made for The City): each split of the main road made its road A - the node before linked to A's first
   part, A's last to the node after the join; B left out of the chain.  Every word changed logged (CeW) - CeForkUndo puts them back.  Before the map and the editor's forks */
static void CeUnsplitTrack(int *course)
{   int *q = course, g = 0, n = 0; char m[80];
    while (q && g++ < 4096) {
        if (q[0] == 0) { q = (int *)q[2]; continue; }
        if (q[0] == 2) { q = (int *)q[4]; continue; }
        if (q[0] != 1) break;
        {   int *prev = (int *)q[4], *af = (int *)q[2], *e = af, *al = 0, *join = 0, *next, g2 = 0;
            while (e && g2++ < 4096) { if (e[0] == 0) { al = e; e = (int *)e[2]; } else if (e[0] == 2) { join = e; break; } else { e = 0; break; } }   /* (A's parts to the join) */
            if (!prev || !af || af[0] != 0 || !al || !join) break;
            next = (int *)join[4];
            if (prev[0] == 0) CeW(&prev[2], (int)af); else if (prev[0] == 2) CeW(&prev[4], (int)af); else break;
            CeW(&af[3], (int)prev);
            CeW(&al[2], (int)next);
            if (next) { if (next[0] == 0) CeW(&next[3], (int)al); else if (next[0] == 1) CeW(&next[4], (int)al); }
            n++; q = next; } }
    wsprintfA(m, "course edit: %d of the track's own split(s) made single road", n); Log(m); }
void rr_CourseEditMap(int *course)
{
    int *q = course, p = 0, g = 0, r = 0, split = 0, b0, ok = 1, i, lo, hi;
    if (course != g_mapCourse) { g_nmap = g_nfin = 0; g_nfkw = 0; }   /* (another course: its blocks are not ours to put back) */
    else CeForkUndo();   /* (the same course set up again: its forks undone first, then its map - MapUndo) */
    MapUndo(); g_mapCourse = course;
    rr_ceForks = 0; if (g_rrc && (g_rrcHdr.spare[7] & 1) && course) CeUnsplitTrack(course); CeForkMake(course);   /* (test switch RR_FORKMAKE: a new fork) */
    if (GetEnvironmentVariableA("RR_CHAINLOG", 0, 0) && course) {   /* (test switch RR_CHAINLOG=1: the course's chain of blocks - each node's first words, a block's header) */
        int *c2 = course, g3 = 0, *seen[64], ns = 0, k3; char b[300];
        while (c2 && g3++ < 200) { for (k3 = 0; k3 < ns && seen[k3] != c2; k3++) ; if (k3 < ns) break; if (ns < 64) seen[ns++] = c2;
            wsprintfA(b, "chain %08x: %d %x %08x %08x %08x %x %x %x", (unsigned)c2, c2[0], c2[1], c2[2], c2[3], c2[4], c2[5], c2[6], c2[7]); Log(b);
            if (c2[0] == 0) { int *h = (int *)c2[4]; wsprintfA(b, "   header %08x: %x %x %x %x %x %x %x %x %x %x %x %x", (unsigned)h, h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11]); Log(b); c2 = (int *)c2[2]; }
            else if (c2[0] == 1) { Log("   (split: on along A)"); c2 = (int *)c2[2]; }
            else if (c2[0] == 2) c2 = (int *)c2[4];
            else break; } }
    if (g_rrc && !g_rrcHdr.spare[0] && q && q[0] == 0) { CeForkEdited(course); CeForksTell(); return; }   /* (no pieces put in / taken out: only the forks, if any) */
    if (!g_rrc || !g_rrcHdr.spare[0] || !q || q[0] != 0) { CeForksTell(); return; }
    b0 = g_rrcHdr.piece0 >> 8;   /* (record i: piece b0 + i of the course; spare[0] - 1: its original, numbered the same way) */
    while (r < g_rrcHdr.n && b0 + r < 0) r++;
    lo = b0 + g_rrc[0].spare[0] - 1; hi = b0 + g_rrc[g_rrcHdr.n - 1].spare[0] - 1;   /* (the original pieces the file covers) */
    while (q && g++ < 4096 && ok) {
        if (q[0] == 0) {
            int len = q[5], r0 = r, nr, n = 0, same = 1, g2, *src;
            while (r < g_rrcHdr.n && b0 + g_rrc[r].spare[0] - 1 < p + len) r++;
            nr = r - r0;
            if (!(src = (int *)HeapAlloc(GetProcessHeap(), 0, (nr + len) * sizeof(int)))) { ok = 0; break; }
            for (g2 = p; g2 < p + len && g2 < lo; g2++) src[n++] = g2 - p;   /* (before the file's pieces / after them: the course as it is) */
            for (i = 0; i < nr; i++) src[n++] = g_rrc[r0 + i].spare[0] - 1 + b0 - p;
            for (g2 = hi + 1 > p ? hi + 1 : p; g2 < p + len; g2++) src[n++] = g2 - p;
            if (GetEnvironmentVariableA("RR_CEMAPLOG", 0, 0)) { char b[160]; wsprintfA(b, "course edit map: block %08x at %d, %d pieces, split %d: records %d..%d, %d pieces now", (unsigned)q, p, len, split, r0, r, n); Log(b); }
            for (i = 0; i < n && same; i++) same = src[i] == i;
            if (same && n == len) HeapFree(GetProcessHeap(), 0, src);
            else {
                CeMap *m = &g_map[g_nmap];
                if (split || n <= 0 || g_nmap >= 256) { HeapFree(GetProcessHeap(), 0, src); ok = 0; break; }
                m->src = src; m->blk = q; m->vlen = n; m->slen = len; m->fin0 = *(int *)(q[4] + 0x2c); m->fin1 = 0;
                if (m->fin0 > 0) { for (i = 0; i < n && m->src[i] < m->fin0; i++) ; m->fin1 = i > 0 ? i : 1; }
                g_nmap++; }
            p += len; q = (int *)q[2]; }
        else if (q[0] == 1) { split = 1; q = (int *)q[2]; }
        else if (q[0] == 2) { split = 0; q = (int *)q[4]; }
        else break;
    }
    if (!ok) { Log("course edit: the pieces put in / taken out do not fit the course's blocks - raced without the edits"); MapUndo(); HeapFree(GetProcessHeap(), 0, g_rrc); g_rrc = 0;
        if (g_rrcEx) { HeapFree(GetProcessHeap(), 0, g_rrcEx); g_rrcEx = 0; } rr_courseEdited = 0; g_rrcTrack = -1; return; }
    for (i = 0; i < g_nmap; i++) { g_map[i].blk[5] = g_map[i].vlen; if (g_map[i].fin1) *(int *)(g_map[i].blk[4] + 0x2c) = g_map[i].fin1; MapFinishes(&g_map[i]); }
    for (i = 0; i < g_nfin; i++) *g_finRec[i] = g_finNow[i];   /* (TreePrepare puts this level's into the block header next) */
    rr_ceMaps = g_nmap;
    CeForkEdited(course); CeForksTell();   /* (the editor's forks) */
    if (g_nmap) { char b[96]; wsprintfA(b, "course edit: %d course block(s) with pieces put in / taken out", g_nmap); Log(b); }
}

/* section p (just filled by rr_RoadSectionFill): its piece's edited values, if the course has them */
void rr_CourseEditApply(int *p)
{
    unsigned char *sec = (unsigned char *)p, *piece = (unsigned char *)p[10], *src, mark; int k, half; int frame[2];
    if (!g_rrc || !piece) return;
    k = (*(int *)(piece + 8) - g_rrcHdr.piece0) >> 8;   /* (the piece's road position, RoadPiece.pos - not the section's p[1]: that is the course data's own, which starts again with
                                                         the next course block - on track 1 level 1 at piece 992 - so the edits came back a second time further on) */
    if (k < 0 || k >= g_rrcHdr.n) return;
    half = sec - piece == 0x194 ? 1 : 0;   /* (RoadPiece halfA at 0x30, halfB at 0x194) */
    if (!half && sec[0x36] == 7 && (g_rrc[k].used & 2)) half = 1;   /* (a split's piece built as a single road - half 7, A's sections there say 0xd: the rider on B, the
                                                                game builds B as single pieces in the A slot - B's edits; A's on it bent B as A, True 3D lost the game's B: world3d.inc W3Track) */
    if (!(g_rrc[k].used & (1u << half))) return;
    src = half ? g_rrc[k].b : g_rrc[k].a;
    frame[0] = *(int *)(sec + 0x44); frame[1] = *(int *)(sec + 0x54);   /* (the sprites' draw-time caches stay the game's) */
    {   RoadSection keep; int nf = half && (g_rrc[k].spare[1] & 1);   /* (a fork made in the editor: its B painted as the editor has it, the fields the game works out for a
                                                                    carriageway of its own kept - half (2: B; the file's copy of A said 7, a single road: the riders stayed on A),
                                                                    the ground heights, the wall projection term, the walls) */
        int fk = g_rrc[k].spare[1] & 1;   /* (A too keeps the game's half on an editor fork: the file says 7, a single road - its pedestrians at the inner kerb were solid to a
                                             bike on B, which was lifted over nothing; user, 2026-11-23: "around segment 540 ... the bike just gets bumped or pushed for no reason") */
        if (fk) memcpy(&keep, sec, sizeof keep);
        memcpy(sec + RRC_FROM, src + RRC_FROM, RRC_SECSIZE - RRC_FROM);
        if (fk) ((RoadSection *)sec)->half = keep.half;
        if (nf) { RoadSection *d = (RoadSection *)sec; memcpy(d->rowH, keep.rowH, sizeof d->rowH); d->depthC0 = keep.depthC0; memcpy(d->wall, keep.wall, sizeof d->wall); } }
    *(int *)(sec + 0x44) = frame[0]; *(int *)(sec + 0x54) = frame[1];
    mark = sec[0x38] == 7 && (unsigned)sec[0x39] - (unsigned)(*(unsigned char *)&rec_playerRecord.g_playerLevel) == 1 ? 0x20 : 0;   /* (the finish marker bit: this race's level's marker
                                                         only, as rr_RoadSectionFill sets it - the file is every level's) */
    sec[0x36] = (unsigned char)((sec[0x36] & ~0x20) | mark);
}

/* painted water (user, 2026-11-18: "if the player lands in the water its instant game lost") or a hole (2026-11-19: "if the player falls into this they immediately lose"): 1 = lateral position lat at road position pos lies in a cell
   painted WATER (course3d.h C3_WaterRow: k_c3Ground[C3_GWATER], RRC_BAND | RRC_GEN | 1 on the bands P0 .. P2 / past P2 of a terrain side) - the cells as
   course3d.h C3_Side lays them (verge's outer edge, the profile points, then on out) */
int rr_CourseEditWater(unsigned pos, int lat, int half)   /* (half: the player's road - 1 = B of a split; user, 2026-11-23 "is there anything else like this?": B's rider was checked against A's cells) */
{
    const RrcExtra *x = rr_CourseEditExtra(pos); const RoadSection *s; int side, c, sg, b[6], i; unsigned w = RRC_BAND | RRC_GEN | 1u, ho = RRC_BAND | RRC_GEN | 2u, hd = RRC_BAND | RRC_GEN | 14u;   /* (WATER, HOLE, DHOLE: ground_art.h) */
    if (!x) return 0;
    s = (const RoadSection *)RR_RING(pos)[3 + half];
    if (!s) return 0;
    side = lat > (s->edgeL + s->edgeR) / 2; sg = side ? 1 : -1;
    if (!(s->kind == SEC_PROFILE || (s->kind == SEC_OPEN && x->sideKind[half][side] == 2))) return 0;   /* (a terrain side: course3d.h C3_SideKind) */
    b[0] = side ? s->edgeR : s->edgeL; b[1] = side ? s->outerR : s->outerL;   /* (cell c from b[c] to b[c + 1]: the verge, P0, P1, P2, past P2) */
    for (i = 0; i < 3; i++) { b[i + 2] = s->profile[(i * 2 + side) * 2]; if ((b[i + 2] - b[i + 1]) * sg < 0) b[i + 2] = b[i + 1]; }
    b[5] = b[4] + sg * 1000000;
    for (c = 0; c <= 4; c++) {
        unsigned v = c == 0 ? x->cell[half][side][0] : c <= 3 ? x->band[half][side][c - 1] : x->cell[half][side][1];
        if ((v == ho || v == hd || (v == w && c >= 1)) && (lat - b[c]) * sg > 0 && (b[c + 1] - lat) * sg >= 0) { char m[96]; wsprintfA(m, "%s: the player in it at %u (lateral %d, cell %d)", v == ho || v == hd ? "hole" : "water", pos >> 8, lat, c); Log(m); return 1; } }
    return 0;
}

/* cobbles (user, 2026-11-18: "when the bike rides over cobble stone it makes a cobble road noise"): 1 = lateral position lat at road position pos lies on the road
   (between its edges) where the lanes on that side of the centre line are painted COBBLE (RrcExtra.road = RRC_BAND | RRC_GEN | 0: ground_art.h k_c3Road[0]) */
int rr_CourseEditCobble(unsigned pos, int lat, int half)
{
    const RrcExtra *x = rr_CourseEditExtra(pos); const RoadSection *s; int j; unsigned v;
    if (!x) return 0;
    s = (const RoadSection *)RR_RING(pos)[3 + half];
    if (!s || lat < s->edgeL || lat > s->edgeR) return 0;
    j = (lat + 250 * (int)s->lanesL) / 250;   /* (course3d.h C3_Lanes: the lanes from -250 x lanesL, the centre line at 0) */
    v = j >= 0 && j < 8 && x->lane[half][j] ? x->lane[half][j] : x->road[half][lat < 0 ? 0 : 1];   /* (the lane's own surface, else its side's) */
    if (v != (RRC_BAND | RRC_GEN)) return 0;
    {   static unsigned said; if (pos >> 8 != said + 1 && pos >> 8 != said) { char m[80]; wsprintfA(m, "cobbles: the player on them at %u (lane %d)", pos >> 8, j + 1); Log(m); } said = pos >> 8; }   /* (each time onto them) */
    return 1;
}
/* a speed bump (user, 2026-11-20: speed bumps you feel): 1 = lateral position lat at road position pos lies on a lane painted SPDBUMP (RrcExtra.road / lane =
   RRC_BAND | RRC_GEN | 3: ground_art.h k_c3Road[3]) within its hump - the middle of the piece, as course3d.h C3_Lanes draws it (C3_BUMPW: 90 of the piece's ~662) */
int rr_CourseEditBump(unsigned pos, int lat, int half)
{
    const RrcExtra *x = rr_CourseEditExtra(pos); const RoadSection *s; int j, f = (int)(pos & 255); unsigned v;
    if (!x || f < 110 || f > 146) return 0;
    s = (const RoadSection *)RR_RING(pos)[3 + half];
    if (!s || lat < s->edgeL || lat > s->edgeR) return 0;
    j = (lat + 250 * (int)s->lanesL) / 250;
    v = j >= 0 && j < 8 && x->lane[half][j] ? x->lane[half][j] : x->road[half][lat < 0 ? 0 : 1];
    return v == (RRC_BAND | RRC_GEN | 3u);
}
