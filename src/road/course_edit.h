/* road\course_edit.h - a course edited in the Course Editor (src\editor\courseedit.c; user, 2026-11-16: "a proper course editor ... every feature
 * editable").  Shared by the game (road\course_edit.c: applies it while the road is generated) and the editor (writes it).
 *
 * File <data>\mods\courses\t<track>_l<level>.rrc (track 1..5, level 1..5), little endian:
 *   RrcHeader, then n records RrcPiece - piece i = road position piece0 + i * 0x100 (the course file's numbering: the extractor's header word 5).
 * Each record holds both carriageways' sections as the game's generator would leave them (road_types.h RoadSection, 0x164 bytes): the game copies
 * everything from laneR (0x2c) on over the section it has just generated (rr_RoadSectionFill), except the draw-time sprite caches.  The editor starts
 * from the course's extract (world3d\t<track>_l<level>_1r.rrx: the generator's own values), so an untouched piece is the original exactly.
 * A course is the same length and has its splits where the original has them: the pieces' contents are edited, not the course's block structure.
 * Test switch RR_COURSEEDIT=0: no edited course is used (the editor's extract of the original); RR_COURSEEDITFILE=<file>: the edited course is read
 * from that file instead (the editor's "Game-generated 3D": its edits, not saved yet, through the game's own generator). */
#ifndef RR_COURSE_EDIT_H
#define RR_COURSE_EDIT_H

#define RRC_MAGIC   0x31435252u   /* "RRC1" */
#define RRC_VERSION 1
#define RRC_SECSIZE 0x164
#define RRC_FROM    0x2c          /* the copied part of a section: from laneR to the end */

typedef struct RrcHeader {
    unsigned magic, version;
    int track, level;             /* 1..5 */
    int n;                        /* records */
    int piece0;                   /* road position (24.8) of record 0 */
    int secSize;                  /* RRC_SECSIZE */
    unsigned checksum;            /* FNV-1a of the records */
    int spare[8];
} RrcHeader;

typedef struct RrcPiece {
    unsigned used;                /* bit 0: half A, bit 1: half B (a split road) - the halves the game takes from here */
    int spare[3];
    unsigned char a[RRC_SECSIZE], b[RRC_SECSIZE];   /* RoadSection halfA / halfB (pointers inside are meaningless here) */
} RrcPiece;

static unsigned RrcHash(const void *p, unsigned n) { const unsigned char *b = (const unsigned char *)p; unsigned h = 2166136261u; while (n--) h = (h ^ *b++) * 16777619u; return h; }

#endif
