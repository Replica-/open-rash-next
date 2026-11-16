/* road\course_edit.h - a course edited in the Course Editor (src\editor\courseedit.c; user, 2026-11-16: "a proper course editor ... every feature
 * editable").  Shared by the game (road\course_edit.c: applies it while the road is generated) and the editor (writes it).
 *
 * File <data>\mods\courses\t<track>.rrc (track 1..5), little endian - one road per track, raced at every level as in the original (the levels
 * differ only in where they finish: each course block's table of finishes, type 6 entries, render\anim_cursor.c rr_LevelEntryFind):
 *   RrcHeader (level 0 = every level), then n records RrcPiece - piece i = road position piece0 + i * 0x100 (the course file's numbering: the
 * extractor's header word 5; the same at every level) - then (version 2) n RrcExtra, the same pieces.
 * Each record holds both carriageways' sections as the game's generator would leave them (road_types.h RoadSection, 0x164 bytes): the game copies
 * everything from laneR (0x2c) on over the section it has just generated (rr_RoadSectionFill), except the draw-time sprite caches and the finish
 * marker bit (half 0x20: set for this race's level only).  The editor starts from the track's level 5 extract (world3d\t<track>_l5.rrx: the
 * generator's own values, the whole road), so an untouched piece is the original exactly.
 * A course has its splits where the original has them.  Pieces can be put in or taken out (the editor's plan: a point dragged, the stretches either side
 * given the pieces their new length needs - the rest of the course as it was): then header spare[0] = 1 and each record's spare[0] = 1 + the original
 * piece it is made from (the same one twice = a piece put in, one missed out = a piece taken out; never in a split, never before the start).  The game
 * steps the course data's cursors through that map (road\course_edit.c rr_CourseEditMap, render\anim_cursor.c rr_CursorBankMove): a course block
 * gets the edited length, a piece put in reads its original's data again, and every level's finish offset moves with its piece.
 * Test switch RR_COURSEEDIT=0: no edited course is used (the editor's extract of the original); RR_COURSEEDITFILE=<file>: the edited course is read
 * from that file instead (the editor's "Game-generated 3D": its edits, not saved yet, through the game's own generator). */
#ifndef RR_COURSE_EDIT_H
#define RR_COURSE_EDIT_H

#define RRC_MAGIC   0x31435252u   /* "RRC1" */
#define RRC_VERSION 2             /* 2: n RrcExtra follow the records (version 1 files: none - read as all zero) */
#define RRC_SECSIZE 0x164
#define RRC_FROM    0x2c          /* the copied part of a section: from laneR to the end */
#define RRC_BAND    0x8000u       /* RrcExtra.band: set = this texture (RRC_BAND | set code << 2 | row) */

typedef struct RrcHeader {
    unsigned magic, version;
    int track, level;             /* 1..5; level 0 = every level */
    int n;                        /* records */
    int piece0;                   /* road position (24.8) of record 0 */
    int secSize;                  /* RRC_SECSIZE */
    unsigned checksum;            /* FNV-1a of the records (and the extras) */
    int spare[8];
} RrcHeader;

typedef struct RrcPiece {
    unsigned used;                /* bit 0: half A, bit 1: half B (a split road) - the halves the game takes from here */
    int spare[3];
    unsigned char a[RRC_SECSIZE], b[RRC_SECSIZE];   /* RoadSection halfA / halfB (pointers inside are meaningless here) */
} RrcPiece;

/* what the original's road data cannot hold, per piece (user, 2026-11-16: terrain bands painted one by one, a forest): the game does not copy it into
   its road - True 3D reads it from the course file's CEXT chunk (road\road_build.c CourseExtract; platform\world3d\rrx_assets.h RxAssets.ext) */
typedef struct RrcExtra {
    unsigned short band[2][2][3];   /* half A / B, side left / right, terrain band (kind 4) 0 edge -> P0, 1 P0 -> P1, 2 P1 -> P2: 0 = the side texture's own
                                       (course3d.h C3_ProfileTex: row band of its group); RRC_BAND | code << 2 | row = row of group code (a set code) */
    unsigned forest[2];             /* half A / B, the editor's forest (it fills the object row: courseedit.c ForestFill): bits 0..2 trees at left P0..P2,
                                       3..5 right P0..P2, 8..15 the size (row scale), 16..23 the seed */
    unsigned char forestCode[2][4]; /* the trees (object row codes, 0xff none): each piece one of them */
    int spare[4];
} RrcExtra;

static unsigned RrcHashMore(unsigned h, const void *p, unsigned n) { const unsigned char *b = (const unsigned char *)p; while (n--) h = (h ^ *b++) * 16777619u; return h; }
static unsigned RrcHash(const void *p, unsigned n) { return RrcHashMore(2166136261u, p, n); }   /* (version 2: over the records, then on over the extras) */

#endif
