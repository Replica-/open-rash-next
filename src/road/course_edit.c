/* road\course_edit.c - an edited course (road\course_edit.h; the Course Editor, src\editor\courseedit.c) put into the road as the game generates it.
 * rr_CourseEditApply is called by rr_RoadSectionFill once a section is filled from the course data, before its obstacles are placed and it is linked:
 * the edited values replace the generated ones, so the road, the riders, the traffic, Pseudo 3D and the True 3D extract all see the edited course. */
#include "../rr.h"
#include "../data/records.h"
#include "course_edit.h"

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
    if (ReadFile(f, &g_rrcHdr, sizeof g_rrcHdr, &got, NULL) && got == sizeof g_rrcHdr && g_rrcHdr.magic == RRC_MAGIC && (g_rrcHdr.version == 1 || g_rrcHdr.version == RRC_VERSION)
        && g_rrcHdr.secSize == RRC_SECSIZE && g_rrcHdr.n > 0 && g_rrcHdr.n < 65536 && g_rrcHdr.track == track && (g_rrcHdr.level == 0 || g_rrcHdr.level == level)) {
        DWORD xsize = g_rrcHdr.version >= 2 ? (DWORD)g_rrcHdr.n * sizeof(RrcExtra) : 0; unsigned h;
        size = (DWORD)g_rrcHdr.n * sizeof(RrcPiece);
        g_rrc = (RrcPiece *)HeapAlloc(GetProcessHeap(), 0, size);
        if (xsize) g_rrcEx = (RrcExtra *)HeapAlloc(GetProcessHeap(), 0, xsize);
        if (g_rrc && ReadFile(f, g_rrc, size, &got, NULL) && got == size && (!xsize || (g_rrcEx && ReadFile(f, g_rrcEx, xsize, &got, NULL) && got == xsize))) {
            h = RrcHash(g_rrc, size); if (xsize) h = RrcHashMore(h, g_rrcEx, xsize);
            if (h != g_rrcHdr.checksum) { HeapFree(GetProcessHeap(), 0, g_rrc); g_rrc = 0; } }
        else if (g_rrc) { HeapFree(GetProcessHeap(), 0, g_rrc); g_rrc = 0; }
        if (!g_rrc && g_rrcEx) { HeapFree(GetProcessHeap(), 0, g_rrcEx); g_rrcEx = 0; }
    }
    CloseHandle(f);
    if (g_rrc) { rr_courseEdited = 1; rr_courseEditKey = g_rrcHdr.checksum; }
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
void rr_CourseEditMap(int *course)
{
    int *q = course, p = 0, g = 0, r = 0, split = 0, b0, ok = 1, i, lo, hi;
    if (course != g_mapCourse) g_nmap = g_nfin = 0;   /* (another course: its blocks are not ours to put back) */
    MapUndo(); g_mapCourse = course;
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
