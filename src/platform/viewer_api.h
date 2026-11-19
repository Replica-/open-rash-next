/* viewer_api.h - the Course Editor's game viewer (src\editor\courseedit.c <-> the ddraw shim's viewer.inc).
 *
 * The editor shows a course with the game's own True 3D renderer by running the game: OpenRash.exe with RR_VIEWER=<name of this shared memory>
 * (+ RR_AUTORACE / RR_LEVEL / RR_COURSEEDITFILE = the edits, RR_WORLD3D=1, RR_HEADLESS=2) starts the race, holds it still once the 3D world is up and
 * puts its window into the editor's 3D panel (parent).  From then on it draws the course from the camera the editor writes here (orbiting the focus
 * piece) without the riders, the HUD or the countdown, and the chosen pieces framed.  A click in the panel comes here; the game answers with the piece.
 */
#ifndef RR_VIEWER_API_H
#define RR_VIEWER_API_H

#define RRV_MAGIC 0x36565252u   /* "RRV6" (the layout changes: a new number - a game and an editor that do not match ignore each other) */

typedef struct RrViewer {
    unsigned magic;             /* RRV_MAGIC (the editor) */
    unsigned parent;            /* the editor's 3D panel (HWND): the game's window goes in it */
    /* editor -> game: the camera and the choice (any time) */
    int focus;                  /* the chosen piece (orange) */
    float camS, camU;           /* the point the camera orbits: road position in pieces (fractions too) and lateral offset from the road's middle (WASD moves it) */
    int half;                   /* 1 = carriageway B of a split */
    float yaw, pitch, dist;     /* orbit: yaw from the road's heading, pitch up, distance (world units) */
    int selLo, selHi;           /* the chosen pieces (a range; -1 = none) */
    int part;                   /* the part of them chosen: 0 the whole piece, 1 the left side, 2 the right side, 3 both sides (the frame shows it) */
    volatile int clickSeq, clickX, clickY, clickW, clickH, clickShift;   /* a click in the panel (its client pixels, the panel's size) */
    int marks;                  /* 1 = the focus piece's fields drawn where they are (course_marks.h CM_Marks) */
    int paint, paintOff, paintSize, paintStep, paintValue;   /* paint mode: the grid (CM_Grid), the squares whose field (offset of its left side, size, + step
                                                                for the right side) has the brush's value filled */
    volatile unsigned editSeq;  /* +1 when the editor changed pieces since the game loaded: their sections are in the pieces memory (<name>P: RrvPiece[n]) */
    volatile int quit;          /* 1 = the game exits */
    /* game -> editor */
    volatile int state;         /* 0 loading, 1 showing (the window is in the panel), 2 no 3D world (failed) */
    volatile unsigned hwnd;     /* the game's window */
    volatile int pickSeq, pickPiece, pickShift;   /* the answer to clickSeq: the piece clicked (-1 none) */
    volatile int pickPart;      /* where on it: 0 the road, 1 left of it, 2 right of it */
    volatile int npieces;
    volatile int progress;      /* the race's loading, 0..1000 (the game's loading-screen stages: loadscreen.inc LsStep) */
    char stage[96];             /* what it is doing ("Making the course file ...", "Building the 3D course") */
    char course[260];           /* the course file the game built and shows (world3d\...rrx) */
} RrViewer;

/* the pieces memory (<name>P): every piece of the course as the editor has it now; ver +1 at each change - the game takes it into its 3D world
   (band: the painted terrain bands, road\course_edit.h RrcExtra.band) */
#define RRV_SEC 0x164
#define RRV_EXT 1024   /* (room for the piece's RrcExtra - road\course_edit.h; both sides check it fits) */
typedef struct RrvPiece { volatile unsigned ver; int layout; unsigned char a[RRV_SEC], b[RRV_SEC]; unsigned char ext[RRV_EXT]; } RrvPiece;   /* (ext: ALL the piece's editor settings -
    user, 2026-11-19: the curb / gutter blocks "none of them make a difference" - only its painted bands went over, the rest waited for Reload game) */

#endif
