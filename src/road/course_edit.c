/* road\course_edit.c - an edited course (road\course_edit.h; the Course Editor, src\editor\courseedit.c) put into the road as the game generates it.
 * rr_CourseEditApply is called by rr_RoadSectionFill once a section is filled from the course data, before its obstacles are placed and it is linked:
 * the edited values replace the generated ones, so the road, the riders, the traffic, Pseudo 3D and the True 3D extract all see the edited course. */
#include "../rr.h"
#include "../data/records.h"
#include "course_edit.h"

static RrcPiece *g_rrc;           /* the loaded course (NULL = none) */
static RrcHeader g_rrcHdr;
static int g_rrcTrack = -1, g_rrcLevel = -1;
int rr_courseEdited;              /* 1 = this race's course comes from an edited file (render\world3d_feed.c: its own True 3D extract) */
unsigned rr_courseEditKey;        /* the file's checksum (the extract's name) */

/* the edited course file of a track / level: 1 = there is one (path out) */
int rr_CourseEditPath(int track, int level, char *out)
{
    char e[4];
    if (GetEnvironmentVariableA("RR_COURSEEDIT", e, sizeof e) > 0 && e[0] == '0') return 0;
    if (GetEnvironmentVariableA("RR_COURSEEDITFILE", out, MAX_PATH) > 0) return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;   /* (the Course Editor's game-generated view: its edits, not saved yet) */
    wsprintfA(out, "mods\\courses\\t%d_l%d.rrc", track, level);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

/* (re)load for the race being set up: track 1..5, level 1..5 */
void rr_CourseEditLoad(int track, int level)
{
    char path[MAX_PATH]; HANDLE f; DWORD got = 0, size;
    if (track == g_rrcTrack && level == g_rrcLevel && g_rrc) return;
    if (g_rrc) { HeapFree(GetProcessHeap(), 0, g_rrc); g_rrc = 0; }
    g_rrcTrack = track; g_rrcLevel = level; rr_courseEdited = 0; rr_courseEditKey = 0;
    if (!rr_CourseEditPath(track, level, path)) return;
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    if (ReadFile(f, &g_rrcHdr, sizeof g_rrcHdr, &got, NULL) && got == sizeof g_rrcHdr && g_rrcHdr.magic == RRC_MAGIC && g_rrcHdr.version == RRC_VERSION
        && g_rrcHdr.secSize == RRC_SECSIZE && g_rrcHdr.n > 0 && g_rrcHdr.n < 65536 && g_rrcHdr.track == track && g_rrcHdr.level == level) {
        size = (DWORD)g_rrcHdr.n * sizeof(RrcPiece);
        g_rrc = (RrcPiece *)HeapAlloc(GetProcessHeap(), 0, size);
        if (g_rrc && (!ReadFile(f, g_rrc, size, &got, NULL) || got != size || RrcHash(g_rrc, size) != g_rrcHdr.checksum)) { HeapFree(GetProcessHeap(), 0, g_rrc); g_rrc = 0; }
    }
    CloseHandle(f);
    if (g_rrc) { rr_courseEdited = 1; rr_courseEditKey = g_rrcHdr.checksum; }
}

/* section p (just filled by rr_RoadSectionFill): its piece's edited values, if the course has them */
void rr_CourseEditApply(int *p)
{
    unsigned char *sec = (unsigned char *)p, *piece = (unsigned char *)p[10], *src; int k, half; int frame[2];
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
}
