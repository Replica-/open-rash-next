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
#define RRC_VERSION 7             /* 2: n RrcExtra follow the records (version 1 files: none - read as all zero); 3: RrcExtra.bandSet .. road; 4: RrcExtra.fac; 5: lift; 6: lane; 7: holeWater
                                     (older files: their extras RrcExtraSize(version) bytes each, the rest read as zero - RrcExtraWiden) */
#define RRC_SECSIZE 0x164
#define RRC_FROM    0x2c          /* the copied part of a section: from laneR to the end */
#define RRC_BAND    0x8000u       /* RrcExtra.band / cell: set = this texture (RRC_BAND | set code << 2 | row; | RRC_CAT5: from category 5, the footpath / kerb
                                     pictures, else 1, the ground) */
#define RRC_CAT5    0x400u
#define RRC_TEXG    0x800u
#define RRC_GEN     0x1000u       /* (RrcExtra.fac) RRC_BAND | RRC_GEN | n: the track's generated facade n (RxAssets.gen0 + n: course3d.h C3_FacadeVary) */        /* (RrcExtra.road) RRC_BAND | RRC_TEXG | index << 2: the track's global texture (TEXG) index - the road surfaces */

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
    unsigned char sideKind[2][2];   /* half A / B, side left / right (user, 2026-11-17: "make it so the left and right are independent"): 0 = the piece's
                                       kind, 2 = terrain (kind 4: its profile points / bands on this side) on an open-road piece - the other side keeps its
                                       buildings - or painted terrain on a terrain piece (True 3D: course3d.h C3_SideKind, C3_FarGround; the game's own
                                       renderer draws the piece's kind) */
    unsigned char bld[2][2];        /* half A / B, side left / right (user, 2026-11-17: "i want to put terrain in p0, but in p1 i want to put buildings"): on a
                                       terrain side, buildings from band 1 (P0 -> P1) or 2 (P1 -> P2) outwards - their front on that band's inner profile point,
                                       the ground behind it flat (3: from past P2 - their front on P2; 4: from P0 - their front on the verge's outer edge); 0 none (True 3D: course3d.h C3_SideBld, C3_BandBuildings) */
    unsigned char bldCode[2][2];    /* their facade (a set code, category 1 as RoadSection.sideTex: from the sets loaded nearest the piece that have it) */
    unsigned char walk[2][2];       /* half A / B, side left / right (user, 2026-11-17: "set a foot path on the center and also have terrain on p1"): on a terrain
                                       side, a footpath along the road edge (road edge -> outer edge, the verge) at kerb height - the bands beyond free (user: "the footpath needs to be roadedge") - 1 + its set code (category 5 as RoadSection.sideArg: row 0 the
                                       path, 1 the kerb face); 0 none (True 3D: course3d.h C3_SideWalk) */
    unsigned short bandSet[2][2][3]; /* (version 3) the sprite set of each painted band's code (its id, RxSet.id: user, 2026-11-17 "i want the full list of textures
                                       there" - any ground picture of the course, not only the sets loaded at the piece); 0 = the set in the code's slot there */
    unsigned short cell[2][2][2];   /* (version 3) half A / B, side left / right: the picture of the road side (0: road edge -> outer edge, the footpath / verge) and of the
                                       ground past P2 (1) - as band (user, 2026-11-17: "retexture road side, p0, p1, p2 and past it individually"); 0 = its own */
    unsigned short cellSet[2][2][2]; /* their sets (as bandSet) */
    unsigned short edge[2][2];      /* (version 3) half A / B, side left / right: the road edge's picture - the kerb face (user, 2026-11-18: "show the road edge as an
                                       item i can edit"), as band; 0 = its own */
    unsigned short edgeSet[2][2];   /* its set (as bandSet) */
    unsigned char gutter[2][2];     /* the gutter along the road edge: 0 as the side has it (a street's), 1 one (a terrain side's too), 2 none */
    unsigned short road[2][2];      /* (version 3) half A / B, the lanes left / right of the centre line: their surface (RRC_BAND | RRC_TEXG | index << 2; user, 2026-11-18
                                       "road, curbs and ..." - the road its own category); 0 = as the piece has it (course3d.h C3_Lanes) */
    unsigned short fac[2][2];       /* (version 4) half A / B, side left / right: the buildings' facade (user, 2026-11-18: "list all the building types by texture please so i
                                       can place them accordingly") - RRC_BAND | code << 2 (a category 1 facade of set facSet) or RRC_BAND | RRC_GEN | n; 0 = the course's
                                       own mix (course3d.h C3_FacPick) */
    unsigned short facSet[2][2];
    short lift[2][2][2];            /* (version 5) half A / B, side left / right: the height added at the curb's top (0) and the verge's outer edge (1) - the editor's Raise /
                                       Lower / Level (user, 2026-11-18: "with the verge and the road side i want to configure the height of it"; course3d.h C3_SectionAt,
                                       C3_Roadside) */
    unsigned short lane[2][8];      /* (version 6) half A / B, lane 0 .. 7 from the road's left edge: its own surface (user, 2026-11-18: "how do i nominate where the yellow lane
                                       is and where white lanes are" - the lane picture's line on its left edge), as road; 0 = the side's (road[]) or the game's own (course3d.h C3_Lanes) */
    short holeWater[2][2];          /* (version 7) half A / B, side left / right: the water in its holes (user, 2026-11-19 "for all hole textures in T3D, just have water fill
                                       the bottom of them at a certain height ... if water is enabled or disabled"): 0 = as default (course3d.h C3_HOLEWD below the ground),
                                       -1 none, else this far below the ground (1 .. 2000) - one setting for a hole: the editor sets the run of pieces with holes on that side */
} RrcExtra;

#define RRC_EXTRA2 ((unsigned)(size_t)&((RrcExtra *)0)->bandSet)   /* a version 2 file's RrcExtra size */
#define RRC_EXTRA3 ((unsigned)(size_t)&((RrcExtra *)0)->fac)       /* a version 3 file's */
#define RRC_EXTRA4 ((unsigned)(size_t)&((RrcExtra *)0)->lift)      /* a version 4 file's */
#define RRC_EXTRA5 ((unsigned)(size_t)&((RrcExtra *)0)->lane)      /* a version 5 file's */
#define RRC_EXTRA6 ((unsigned)(size_t)&((RrcExtra *)0)->holeWater) /* a version 6 file's */
static unsigned RrcExtraSize(unsigned version) { return version >= 7 ? (unsigned)sizeof(RrcExtra) : version == 6 ? RRC_EXTRA6 : version == 5 ? RRC_EXTRA5 : version == 4 ? RRC_EXTRA4 : version == 3 ? RRC_EXTRA3 : RRC_EXTRA2; }   /* (a file's extras, each) */
static void RrcExtraWiden(RrcExtra *x, int n, unsigned size)   /* n extras read `size` bytes each (an older file) at the start of x: spread out, the new fields zero */
{ int i; unsigned j; if (size >= sizeof(RrcExtra)) return;
  for (i = n - 1; i >= 0; i--) { unsigned char *d = (unsigned char *)&x[i], *s = (unsigned char *)x + (unsigned)i * size;   /* (back to front: no copy over one not moved yet) */
    for (j = size; j-- > 0; ) d[j] = s[j]; for (j = size; j < sizeof(RrcExtra); j++) d[j] = 0; } }
static unsigned RrcHashMore(unsigned h, const void *p, unsigned n) { const unsigned char *b = (const unsigned char *)p; while (n--) h = (h ^ *b++) * 16777619u; return h; }
static unsigned RrcHash(const void *p, unsigned n) { return RrcHashMore(2166136261u, p, n); }   /* (version 2: over the records, then on over the extras) */

#endif
