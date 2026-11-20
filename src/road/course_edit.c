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
   B's last, the block after }: ui\custom_race.c).  A course whose map does not fit its blocks is raced without the edits. */
/* a new fork (user, 2026-11-20: "forking the road ... allows the player and ai take it"): normal block q of the main road made a split road - A = the block as it
   is, B = a copy of it (its own header, road rows and scenery tables) between a split's opening and closing nodes { 1 / 2, 6, A, B, the block before / after,
   0x3c00, 0x10000, 0 } (as the game's own: test switch RR_CHAINLOG); B's curve per piece from curveB (NULL: A's).  Both roads alike for the SPLIT_SAME pieces at
   each end (ui\custom_race.c - TreePrepare copies them).  The course is loaded fresh for every race: nothing to put back.  1 = made */
#define CE_SPLIT_SAME 0x51
static int CeForkBlock(int *q, const signed char *curveB)
{
    int *s1, *s2, *b, *hb, *ha, len, i; unsigned char *rb;
    if (!q || q[0] != 0 || !q[3] || !q[2]) return 0;
    len = q[5]; ha = (int *)q[4];
    if (len < 2 * CE_SPLIT_SAME + 8 || !ha || !ha[1]) return 0;
    s1 = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32); s2 = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32); b = (int *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 32);
    hb = (int *)HeapAlloc(GetProcessHeap(), 0, 0x34); rb = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, 0x10 + 2 * len);
    if (!s1 || !s2 || !b || !hb || !rb) return 0;
    memcpy(hb, ha, 0x34); memcpy(rb, (void *)ha[1], 0x10 + 2 * len); hb[1] = (int)rb;
    {   /* B's own copies of the block's other tables (header words 2 .. 7: the scenery, its draw-time sprite-slot caches - shared with A, a slot one road freed was
           left stale in the other's: ScenerySpriteDraw > StripCellPick read a null entry).  They lie one after another (as the rows: 0x10 + 2 x length up to word 2),
           each as long as the gap to the next one (word 7: to word 9) */
        int t, u; for (t = 2; t <= 7; t++) { unsigned a0 = (unsigned)ha[t], a1 = 0xffffffffu, sz; void *cp;
            if (!a0) continue;
            for (u = 1; u <= 9; u++) if (u != t && (unsigned)ha[u] > a0 && (unsigned)ha[u] < a1) a1 = (unsigned)ha[u];
            sz = a1 - a0; if (a1 == 0xffffffffu || sz > 0x100000) continue;
            if ((cp = HeapAlloc(GetProcessHeap(), 0, sz)) != NULL) { memcpy(cp, (void *)a0, sz); hb[t] = (int)cp; } } }
    if (curveB) for (i = CE_SPLIT_SAME; i < len - CE_SPLIT_SAME; i++) rb[0x10 + 2 * i] = (unsigned char)curveB[i];
    s1[0] = 1; s1[1] = 6; s1[2] = (int)q; s1[3] = (int)b; s1[4] = q[3]; s1[5] = 0x3c00; s1[6] = 0x10000;
    s2[0] = 2; s2[1] = 6; s2[2] = (int)q; s2[3] = (int)b; s2[4] = q[2]; s2[5] = 0x3c00; s2[6] = 0x10000;
    b[0] = 0; b[1] = 6; b[2] = (int)s2; b[3] = (int)s1; b[4] = (int)hb; b[5] = len;
    {   int *prev = (int *)q[3], *next = (int *)q[2];   /* (the block before / after: their links to the split nodes now) */
        if (prev[0] == 0) prev[2] = (int)s1; else if (prev[0] == 2) prev[4] = (int)s1;
        if (next[0] == 0) next[3] = (int)s2; else if (next[0] == 1) next[4] = (int)s2;
        q[2] = (int)s2; q[3] = (int)s1; }
    return 1;
}
/* test switch RR_FORKMAKE="<block> <bend> <quarter>": the main road's normal block number <block> (0 = the first) forked - B bent by +bend for <quarter> pieces, -bend
   for twice that, +bend again in the middle of the block: out to the side and back (bend 1, quarter 45: ~2000 units out) */
static void CeForkMake(int *course)
{
    char e[64]; int blk = -1, bend = 1, quarter = 45, i = 0, g = 0, *q = course, len; signed char *cv;
    if (!GetEnvironmentVariableA("RR_FORKMAKE", e, sizeof e) || !q) return;
    {   int v[3] = { -1, 1, 45 }, nv = 0, neg = 0, cur = 0, in = 0; const char *c;   /* (three whole numbers - no sscanf here: the game is linked without stdio's) */
        for (c = e; ; c++) { if (*c == '-') neg = 1; else if (*c >= '0' && *c <= '9') { cur = cur * 10 + (*c - '0'); in = 1; } else { if (in && nv < 3) v[nv++] = neg ? -cur : cur; cur = 0; in = 0; neg = 0; if (!*c) break; } }
        blk = v[0]; bend = v[1]; quarter = v[2]; if (blk < 0 || quarter < 1) return; }
    while (q && g++ < 4096) {   /* the main road's normal blocks (the splits passed along A) */
        if (q[0] == 0) { if (i++ == blk) break; q = (int *)q[2]; }
        else if (q[0] == 1) q = (int *)q[2]; else if (q[0] == 2) q = (int *)q[4]; else { q = 0; break; } }
    if (!q || q[0] != 0) { Log("fork make: no such block"); return; }
    len = q[5];
    if (len < 2 * CE_SPLIT_SAME + 4 * quarter || !(cv = (signed char *)HeapAlloc(GetProcessHeap(), 0, len))) { Log("fork make: the block is too short"); return; }
    for (i = 0; i < len; i++) cv[i] = (signed char)((unsigned char *)((int *)q[4])[1])[0x10 + 2 * i];
    for (i = 0; i < 4 * quarter; i++) { int k = CE_SPLIT_SAME + (len - 2 * CE_SPLIT_SAME - 4 * quarter) / 2 + i, c = cv[k] + (i < quarter || i >= 3 * quarter ? bend : -bend); cv[k] = (signed char)(c > 127 ? 127 : c < -127 ? -127 : c); }
    {   char m[120]; wsprintfA(m, "fork make: block %d (%d pieces) %s - B bent by %d over %d pieces", blk, len, CeForkBlock(q, cv) ? (rr_ceForks++, "made a split road") : "NOT forked", bend, 4 * quarter); Log(m); }
    HeapFree(GetProcessHeap(), 0, cv);
}
/* the edited course's own forks (the Course Editor's "Fork here": RrcPiece.spare[1] bit 0 on the pieces of a block made a split road, B's road in their b halves):
   each main-road block whose middle record is marked, forked with B's curves from the records - after the map (the blocks' lengths as edited) */
static void CeForkEdited(int *course)
{
    int *q = course, p = 0, g = 0, b0 = g_rrcHdr.piece0 >> 8, made = 0;
    while (q && g++ < 4096) {
        if (q[0] == 0) { int len = q[5], *next = (int *)q[2], mid = p + len / 2 - b0;
            if (mid >= 0 && mid < g_rrcHdr.n && (g_rrc[mid].spare[1] & 1)) { signed char *cv = (signed char *)HeapAlloc(GetProcessHeap(), 0, len); int i;
                if (cv) { for (i = 0; i < len; i++) { int r2 = p + i - b0; cv[i] = (signed char)((r2 >= 0 && r2 < g_rrcHdr.n) ? ((const RoadSection *)g_rrc[r2].b)->curve : 0); }
                    if (CeForkBlock(q, cv)) { char m[96]; made++; wsprintfA(m, "course edit: a fork made over pieces %d..%d", p, p + len - 1); Log(m); }
                    HeapFree(GetProcessHeap(), 0, cv); } }
            p += len; q = next; }
        else if (q[0] == 1) { int *a = (int *)q[2], *e2 = a; while (e2 && e2[0] == 0 && g++ < 4096) { p += e2[5]; e2 = (int *)e2[2]; } q = e2 && e2[0] == 2 ? (int *)e2[4] : 0; }   /* (a split: along A to its join) */
        else if (q[0] == 2) q = (int *)q[4];
        else break; }
    rr_ceForks = made;
}
/* the shim told whether this race has the editor's forks (rr_ceForks): True 3D then has forks on - every rider free to take either road (world3d.inc RR_SetForks) */
static void CeForksTell(void)
{   static void (__stdcall *f)(int); static int looked;
    if (!looked) { HMODULE sh = GetModuleHandleA("rrddraw.dll"); f = sh ? (void (__stdcall *)(int))GetProcAddress(sh, "RR_SetForks") : 0; looked = 1; }
    if (f) f(rr_ceForks > 0); }

void rr_CourseEditMap(int *course)
{
    int *q = course, p = 0, g = 0, r = 0, split = 0, b0, ok = 1, i, lo, hi;
    if (course != g_mapCourse) g_nmap = g_nfin = 0;   /* (another course: its blocks are not ours to put back) */
    rr_ceForks = 0; CeForkMake(course); CeForksTell();   /* (test switch RR_FORKMAKE: a new fork) */
    if (GetEnvironmentVariableA("RR_CHAINLOG", 0, 0) && course) {   /* (test switch RR_CHAINLOG=1: the course's chain of blocks - each node's first words, a block's header) */
        int *c2 = course, g3 = 0, *seen[64], ns = 0, k3; char b[300];
        while (c2 && g3++ < 200) { for (k3 = 0; k3 < ns && seen[k3] != c2; k3++) ; if (k3 < ns) break; if (ns < 64) seen[ns++] = c2;
            wsprintfA(b, "chain %08x: %d %x %08x %08x %08x %x %x %x", (unsigned)c2, c2[0], c2[1], c2[2], c2[3], c2[4], c2[5], c2[6], c2[7]); Log(b);
            if (c2[0] == 0) { int *h = (int *)c2[4]; wsprintfA(b, "   header %08x: %x %x %x %x %x %x %x %x %x %x %x %x", (unsigned)h, h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11]); Log(b); c2 = (int *)c2[2]; }
            else if (c2[0] == 1) { Log("   (split: on along A)"); c2 = (int *)c2[2]; }
            else if (c2[0] == 2) c2 = (int *)c2[4];
            else break; } }
    MapUndo(); g_mapCourse = course;
    if (g_rrc && !g_rrcHdr.spare[0] && q && q[0] == 0) { CeForkEdited(course); CeForksTell(); return; }   /* (no pieces put in / taken out: only the forks, if any) */
    if (!g_rrc || !g_rrcHdr.spare[0] || !q || q[0] != 0) return;
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
    if (!(g_rrc[k].used & (1u << half))) return;
    src = half ? g_rrc[k].b : g_rrc[k].a;
    frame[0] = *(int *)(sec + 0x44); frame[1] = *(int *)(sec + 0x54);   /* (the sprites' draw-time caches stay the game's) */
    memcpy(sec + RRC_FROM, src + RRC_FROM, RRC_SECSIZE - RRC_FROM);
    *(int *)(sec + 0x44) = frame[0]; *(int *)(sec + 0x54) = frame[1];
    mark = sec[0x38] == 7 && (unsigned)sec[0x39] - (unsigned)(*(unsigned char *)&rec_playerRecord.g_playerLevel) == 1 ? 0x20 : 0;   /* (the finish marker bit: this race's level's marker
                                                         only, as rr_RoadSectionFill sets it - the file is every level's) */
    sec[0x36] = (unsigned char)((sec[0x36] & ~0x20) | mark);
}

/* painted water (user, 2026-11-18: "if the player lands in the water its instant game lost") or a hole (2026-11-19: "if the player falls into this they immediately lose"): 1 = lateral position lat at road position pos lies in a cell
   painted WATER (course3d.h C3_WaterRow: k_c3Ground[C3_GWATER], RRC_BAND | RRC_GEN | 1 on the bands P0 .. P2 / past P2 of a terrain side) - the cells as
   course3d.h C3_Side lays them (verge's outer edge, the profile points, then on out) */
int rr_CourseEditWater(unsigned pos, int lat)
{
    const RrcExtra *x = rr_CourseEditExtra(pos); const RoadSection *s; int side, c, sg, b[6], i; unsigned w = RRC_BAND | RRC_GEN | 1u, ho = RRC_BAND | RRC_GEN | 2u;   /* (WATER, HOLE: ground_art.h) */
    if (!x) return 0;
    s = (const RoadSection *)RR_RING(pos)[3];
    if (!s) return 0;
    side = lat > (s->edgeL + s->edgeR) / 2; sg = side ? 1 : -1;
    if (!(s->kind == SEC_PROFILE || (s->kind == SEC_OPEN && x->sideKind[0][side] == 2))) return 0;   /* (a terrain side: course3d.h C3_SideKind) */
    b[0] = side ? s->edgeR : s->edgeL; b[1] = side ? s->outerR : s->outerL;   /* (cell c from b[c] to b[c + 1]: the verge, P0, P1, P2, past P2) */
    for (i = 0; i < 3; i++) { b[i + 2] = s->profile[(i * 2 + side) * 2]; if ((b[i + 2] - b[i + 1]) * sg < 0) b[i + 2] = b[i + 1]; }
    b[5] = b[4] + sg * 1000000;
    for (c = 0; c <= 4; c++) {
        unsigned v = c == 0 ? x->cell[0][side][0] : c <= 3 ? x->band[0][side][c - 1] : x->cell[0][side][1];
        if ((v == ho || (v == w && c >= 1)) && (lat - b[c]) * sg > 0 && (b[c + 1] - lat) * sg >= 0) { char m[96]; wsprintfA(m, "%s: the player in it at %u (lateral %d, cell %d)", v == ho ? "hole" : "water", pos >> 8, lat, c); Log(m); return 1; } }
    return 0;
}

/* cobbles (user, 2026-11-18: "when the bike rides over cobble stone it makes a cobble road noise"): 1 = lateral position lat at road position pos lies on the road
   (between its edges) where the lanes on that side of the centre line are painted COBBLE (RrcExtra.road = RRC_BAND | RRC_GEN | 0: ground_art.h k_c3Road[0]) */
int rr_CourseEditCobble(unsigned pos, int lat)
{
    const RrcExtra *x = rr_CourseEditExtra(pos); const RoadSection *s; int j; unsigned v;
    if (!x) return 0;
    s = (const RoadSection *)RR_RING(pos)[3];
    if (!s || lat < s->edgeL || lat > s->edgeR) return 0;
    j = (lat + 250 * (int)s->lanesL) / 250;   /* (course3d.h C3_Lanes: the lanes from -250 x lanesL, the centre line at 0) */
    v = j >= 0 && j < 8 && x->lane[0][j] ? x->lane[0][j] : x->road[0][lat < 0 ? 0 : 1];   /* (the lane's own surface, else its side's) */
    if (v != (RRC_BAND | RRC_GEN)) return 0;
    {   static unsigned said; if (pos >> 8 != said + 1 && pos >> 8 != said) { char m[80]; wsprintfA(m, "cobbles: the player on them at %u (lane %d)", pos >> 8, j + 1); Log(m); } said = pos >> 8; }   /* (each time onto them) */
    return 1;
}
/* a speed bump (user, 2026-11-20: speed bumps you feel): 1 = lateral position lat at road position pos lies on a lane painted SPDBUMP (RrcExtra.road / lane =
   RRC_BAND | RRC_GEN | 3: ground_art.h k_c3Road[3]) within its hump - the middle of the piece, as course3d.h C3_Lanes draws it (C3_BUMPW: 90 of the piece's ~662) */
int rr_CourseEditBump(unsigned pos, int lat)
{
    const RrcExtra *x = rr_CourseEditExtra(pos); const RoadSection *s; int j, f = (int)(pos & 255); unsigned v;
    if (!x || f < 110 || f > 146) return 0;
    s = (const RoadSection *)RR_RING(pos)[3];
    if (!s || lat < s->edgeL || lat > s->edgeR) return 0;
    j = (lat + 250 * (int)s->lanesL) / 250;
    v = j >= 0 && j < 8 && x->lane[0][j] ? x->lane[0][j] : x->road[0][lat < 0 ? 0 : 1];
    return v == (RRC_BAND | RRC_GEN | 3u);
}
