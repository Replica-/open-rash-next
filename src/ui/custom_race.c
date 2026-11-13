/* ui\custom_race.c - Custom Race (not part of the original game; user 2026-11-12): the Game Mode page's item next to Big Game Mode (menutrim.c, the old
   Thrash Mode record) opens the shim's road editor (src\platform\ddraw\customrace.inc RR_CustomRaceEditor): a straight road bent with control points, its
   segments given ramps and types.  When the player picks Race there, a level 1 race starts on the chosen track's scenery (City by default: no split roads
   at level 1) with the editor's road: at the race start (RoadRingBuild, road\road_build.c) rr_CustomCoursePatch writes its curve and slope into the
   course's road rows - the same data the game's own tracks give, so every part of the game (road, heights, riders) sees the editor's road.  It runs as
   game mode 0 (no career: no prize, no level change); afterwards GameQuit (system\session.c) puts the game mode and the player's level back and returns to
   the Game Mode page.  Without the shim nothing happens. */
#include "../rr.h"
#include "../data/records.h"
#include "../legacy_decls.h"

RR_DATA(int, g_gameMode);

#define I32(p, o) (*(int *)((char *)(p) + (o)))

int rr_customRace;                            /* 1 = the race being run is a custom race */
static int g_prevMode, g_prevLevel;
static const signed char *g_curve, *g_slope; static int g_curveN;   /* the editor's road per piece of the course (the shim's memory) */
static int *g_height;                         /* the height before each piece (g_curveN + 1): the slopes added up */

static FARPROC Shim(const char *name) { HMODULE sh = GetModuleHandleA("ddraw.dll"); return sh ? GetProcAddress(sh, name) : 0; }

/* the Game Mode page's Custom Race item: the editor, then (Race) the race on the base track - as the track page does it (ChoiceScreenPick) */
int __fastcall rr_CustomRacePick(int *item)
{
    int (__stdcall *editor)(void) = (int (__stdcall *)(void))Shim("RR_CustomRaceEditor");
    const signed char *(__stdcall *curve)(int *) = (const signed char *(__stdcall *)(int *))Shim("RR_CustomCurve");
    const signed char *(__stdcall *slope)(int *) = (const signed char *(__stdcall *)(int *))Shim("RR_CustomSlope");
    int track, n = 0, i;
    (void)item;
    if (!editor || !curve || !slope) return 0;
    track = editor();
    if (track < 1 || track > 5) return 0;     /* (Back: stay on the page) */
    g_curve = curve(&g_curveN); g_slope = slope(&n);
    if (!g_curve || !g_slope || g_curveN <= 0 || n != g_curveN) return 0;
    if (g_height) HeapFree(GetProcessHeap(), 0, g_height);
    g_height = (int *)HeapAlloc(GetProcessHeap(), 0, (g_curveN + 1) * sizeof(int));
    if (!g_height) return 0;
    g_height[0] = 0;
    for (i = 0; i < g_curveN; i++) g_height[i + 1] = g_height[i] + g_slope[i];
    g_prevMode = g_gameMode; g_prevLevel = rec_playerRecord.g_playerLevel;
    g_gameMode = 0; rec_playerRecord.g_playerLevel = 0;   /* (level 1: the lengths the editor maps its road onto) */
    rr_customRace = 1;
    MenuTablesReset(track);
    return 8;
}

static int Height(int p) { return g_height[p < 0 ? 0 : (p > g_curveN ? g_curveN : p)]; }
static int Curve(int p) { return (p >= 0 && p < g_curveN) ? g_curve[p] : 0; }

/* A course is a chain of blocks { 0, ?, next, prev, header, length }: header +4 = the block's road rows: +0xc / +0xe the height at its start / end
   (16 bits), from +0x10 { curve, slope } per piece.  A split road { 1, ?, A's first block, B's first, the block before } opens two carriageways that
   meet again at { 2, ?, A's last block, B's last, the block after }.  Every rider takes A in a custom race (the shim's RR_ForkPick), so the editor's road
   runs along A; B gets the same road stretched to its own length. */
#define SPLIT_SAME 0x51   /* a split's first and last 0x51 pieces: both roads alike (TreePrepare copies one road's slopes onto the other there: SpriteColumnsCopy) */

/* the editor's piece for piece i of a block: c = { first piece } (A) or { the split's first piece, A's length, B's length, the block's offset in B } (B) */
static int MapA(int i, const int *c) { return c[0] + i; }
static int MapB(int i, const int *c)
{
    int off = c[3] + i, la = c[1], lb = c[2], s = SPLIT_SAME;
    if (la <= 2 * s || lb <= 2 * s) return c[0] + (int)((__int64)off * la / lb);
    if (off < s) return c[0] + off;
    if (off >= lb - s) return c[0] + la - (lb - off);
    return c[0] + s + (int)((__int64)(off - s) * (la - 2 * s) / (lb - 2 * s));
}

/* one block's rows from the editor's road (the slope = the height difference, so both roads of a split end at the same height) */
static void BlockWrite(int *blk, int (*map)(int, const int *), const int *c)
{
    unsigned char *row = (unsigned char *)I32(blk[4], 4);
    int len = blk[5], i;
    if (!row || len <= 0) return;
    *(short *)(row + 0xc) = (short)Height(map(0, c));
    *(short *)(row + 0xe) = (short)Height(map(len, c));
    for (i = 0; i < len; i++) {
        int d = Height(map(i + 1, c)) - Height(map(i, c));
        row[0x10 + 2 * i] = (unsigned char)(signed char)Curve(map(i, c));
        row[0x11 + 2 * i] = (unsigned char)(signed char)(d > 127 ? 127 : (d < -127 ? -127 : d));
    }
}

static int BranchLength(int *b) { int n = 0, g = 0; for (; b && b[0] == 0 && g < 1024; b = (int *)b[2], g++) n += b[5]; return n; }

/* the race start (RoadRingBuild, before the game prepares the course): a custom race's road into the course `course` (its first block; the course is
   loaded fresh for every race, so nothing has to be put back) */
void rr_CustomCoursePatch(int *course)
{
    int *q = course, p = 0, g = 0;
    if (!rr_customRace || !g_height || !q || q[0] != 0) return;
    while (q && g++ < 4096) {
        if (q[0] == 0) { int c[1]; c[0] = p; BlockWrite(q, MapA, c); p += q[5]; q = (int *)q[2]; }
        else if (q[0] == 1) {
            int c[4], *b, k = 0;
            c[0] = p; c[1] = BranchLength((int *)q[2]); c[2] = BranchLength((int *)q[3]); c[3] = 0;
            if (c[2] > 0) for (b = (int *)q[3]; b && b[0] == 0 && k < 1024; b = (int *)b[2], k++) { BlockWrite(b, MapB, c); c[3] += b[5]; }
            q = (int *)q[2];
        }
        else if (q[0] == 2) q = (int *)q[4];
        else break;   /* (the course's head: the end) */
    }
}

/* the race is over (GameQuit): back to the game as it was; returns 1 when it was a custom race */
int rr_CustomRaceEnd(void)
{
    void (__stdcall *done)(void) = (void (__stdcall *)(void))Shim("RR_CustomRaceDone");
    if (!rr_customRace) return 0;
    rr_customRace = 0;
    g_gameMode = g_prevMode; rec_playerRecord.g_playerLevel = (unsigned char)g_prevLevel;
    if (done) done();
    return 1;
}
