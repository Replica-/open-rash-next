/* world3d\course3d.h - true-3D course model (analysis\TRUE3D_PROJECT.md M3 / M4): turns the game's road pieces (src\road\road_types.h) into a 3D
 * course - a frame (position, heading) per piece and a triangle mesh of road, shoulders, walls and terrain.  Header-only (static functions); used by
 * the viewer (analysis\rrx\viewer3d.c) and the ddraw shim.
 *
 * The original draws the road pseudo-3D: per segment the lateral slope grows by the curve and the lateral position by the slope (a shear), the height by
 * the hill value.  Here the slope becomes a heading: heading(k) = slope(k) / L radians with a constant segment length L (in the game's lateral units;
 * L / focal length is what the original's near-field perspective fixes, see the tracker), height(k) = sum of hill.  Cross-sections stay as the game has
 * them: lateral / height offsets from the carriageway's centre line in the piece's frame (x right, y up, z forward; left-handed like Direct3D).
 */
#ifndef RR_COURSE3D_H
#define RR_COURSE3D_H
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "../../road/road_types.h"
#include "rrx_assets.h"

#define C3_PI 3.14159265358979323846
typedef struct C3Vert { float x, y, z; float u, v; unsigned rgba; float tex; float ox, oy; } C3Vert;   /* tex < 0 = flat colour; rgba = ABGR (little-endian RGBA); ox, oy = billboard offset (0 = fixed geometry) */

typedef struct C3Frame { double x, y, z, heading; } C3Frame;   /* centre line of carriageway A at the start of a piece */

typedef struct C3Bldg { float f[4][3]; float h; int piece; } C3Bldg;   /* a building (C3_Building): its foot corners - front / back at pa, back / front at pb - and height (world3d.inc W3BldgShadows) */

typedef struct C3Course {
    int n;                     /* pieces */
    int start, finish;          /* piece index of the start line / finish line (-1) */
    double L;                   /* segment length */
    RoadPiece *piece;           /* n copies (their pointers are not valid: use halfA / halfB) */
    C3Frame *frame;             /* n + 1 frames */
    C3Frame *frameB;            /* n + 1 frames of carriageway B's centre line (= frame outside split stretches; C3_FramesB) */
    const C3Frame *fr;          /* the frames C3_Point uses (frame, or frameB for half B) */
    C3Vert *v; int nv, cap;     /* triangle list */
    int *vpiece, cur;           /* piece of every vertex (C3_Push: cur); C3_Batches keeps the pieces in order within a batch */
    int hide;                   /* C3_BuildRange: 1 + the split-road half left out where it is far from the other one (0 = both; C3_HideHalf) */
    int darkStrip;              /* C3_Strip: this cross-section span is drawn darker (-1 none): the median wedge under the rails at a fork */
    int skOn, skind[2];         /* C3_BuildRange: skOn = the piece's sides' own kinds (C3_SideKind) in skind[left / right] for C3_Strip / C3_Skirt */
    RxAssets *skRa;             /* C3_BuildRange: the assets (their CEXT: the sides' kinds) for C3_GroundAt */
    int bldKind;                /* the buildings C3_Building is putting up: 0 a street's (C3_Roadside), 1 / 2 on a terrain side from that band (C3_BandBuildings) */
    int skirts;                 /* C3_BuildRange: build the skirts (C3_Skirt) - only the live stretch round the camera, never the whole course */
    int skirtEnd;               /* C3_BuildRange: > 0 = skirts only on the pieces before it (the game's ring: the far part of the one mesh has none, as the course file's had not) */
    int forkPick;               /* 1 = forks off: a split is only its left road A, built as an ordinary road (no B / rails / median); 0 = both */
    int noGround;               /* 1 = no rocks, fallen leaves (Video Options > True 3D detail > Ground detail off: C3_Leaves, C3_DirtRocks) */
    unsigned char *bdead;       /* per piece: 1 = carriageway B has already rejoined A (its real end is earlier than A's: C3_BTail); not built */
    int *pairA, *pairB;         /* per piece: the A piece beside B piece k / the B piece beside A piece k (they drift apart when B is shorter: C3_FramesB) */
    int soft;                   /* forks on, the original's way (C3_SoftFrames): hide = 1 + the carriageway the camera is not on, built round the camera each frame */
    unsigned char *other;       /* soft: per piece, 1 = that other carriageway is built (the game's view has the split there and it is near) */
    const C3Frame *tintFr;      /* test: geometry built in these frames is tinted red (C3_Push) */
    int camK;                   /* soft: the camera's piece (C3_SoftFrames) */
    C3Bldg *bldg; int nbldg, capBldg, recBldg;   /* the buildings in piece order (recorded by C3_Build only: recBldg) */
} C3Course;

/* ---- frames ---- */
static void C3_Frames(C3Course *c)
{
    int k; double slope = 0, x = 0, y = 0, z = 0;
    c->frame = (C3Frame *)realloc(c->frame, sizeof(C3Frame) * (c->n + 1));
    for (k = 0; k <= c->n; k++) {
        const RoadSection *a = &c->piece[k < c->n ? k : c->n - 1].halfA;
        double th;
        slope += a->curve;                      /* slope(k) = sum of curve up to and including piece k (rr_RoadProjectSegments order) */
        th = slope / c->L;
        c->frame[k].x = x; c->frame[k].y = y; c->frame[k].z = z; c->frame[k].heading = th;
        x += c->L * sin(th); z += c->L * cos(th);
        y += k < c->n ? a->hill : 0;
    }
}

/* point (lateral u, height h) in frames fr at piece position p (fractional piece index) */
static void C3_PointF(const C3Course *c, const C3Frame *fr, double p, double u, double h, float *out)
{
    int k = (int)floor(p); double f, th, x, y, z;
    if (k < 0) k = 0;
    if (k >= c->n) k = c->n - 1;
    f = p - k;
    th = fr[k].heading + (fr[k + 1].heading - fr[k].heading) * f;
    x = fr[k].x + (fr[k + 1].x - fr[k].x) * f;
    y = fr[k].y + (fr[k + 1].y - fr[k].y) * f;
    z = fr[k].z + (fr[k + 1].z - fr[k].z) * f;
    out[0] = (float)(x + u * cos(th)); out[1] = (float)(y + h); out[2] = (float)(z - u * sin(th));
}
static void C3_Point(const C3Course *c, double p, double u, double h, float *out) { C3_PointF(c, c->fr ? c->fr : c->frame, p, u, h, out); }

/* carriageway B of the split stretches as a road of its own.  The original's view builder (rr_RoadProjectSegments) starts B from A's projection state +
   splitX (lateral) / splitSlope (slope) and then advances each half with its own curve; the data keeps splitSlope(k) = slope_B(k) - slope_A(k) and splitX
   its running sum - in the original's shear space, where B is splitX units beside A (up to 1.6 million on Sierra: B turns a whole loop more than A).
   Here B's heading is (slope_A + splitSlope) / L, its path integrated from that, its height from its own hills.  At the join (the first single piece
   after the stretch, where the original simply puts B back on A) B has to meet A, but B's own curves end it far away (Sierra: 300 000 units over a
   417 000-unit stretch) - so the correction is a bend, not a shift (a shift would skew every cross-section against the path): the end heading error is
   spread linearly, then c1 sin(pi t) + c2 sin(2 pi t) (zero at both ends) is added to the heading, c1 / c2 solved (Gauss-Newton) so that the path ends
   on A; of the whole-turn choices (B keeps its extra loop or not) the one that needs the smaller bend.  The height error is spread linearly */
enum { C3_NB = 6 };   /* bend terms sin(j pi t), j = 1 .. C3_NB */
static double C3_Bend(const double *cb, double t) { int j; double s = 0; for (j = 0; j < C3_NB; j++) s += cb[j] * sin((j + 1) * C3_PI * t); return s; }
/* hardware mode only (user decision 2026-11-08): B is ONE precomputed path that joins A naturally - it differs from the game's view of B (software
   follows B's own curves and simply puts B back on A at the join).  Every correction (heading spread, bend, residual position) is weighted by the smooth
   step S(t) = t^2 (3 - 2t) of the whole stretch: zero turn added at the fork (B leaves on its own curves - Sierra: to the right), none at the join
   (B arrives along A), the most in the middle where A is out of view.  W = the bend's argument (also S: the bend adds no curvature step at the ends) */
static double C3_S(double t, int n) { (void)n; if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3 - 2 * t); }
static double C3_W(double t, int n) { return C3_S(t, n); }
static void C3_PathB(const C3Course *c, int ks, int kj, const double *h0, const double *cb, double x0, double z0, double *ox, double *oz)
{
    int k; double x = x0, z = z0, n = kj - ks;
    for (k = ks; k < kj; k++) { double h = h0[k - ks] + C3_Bend(cb, C3_W((k - ks + 0.5) / n, kj - ks)); x += c->L * sin(h); z += c->L * cos(h); }
    *ox = x; *oz = z;
}
/* the bend cb that ends the path at (tx, tz): minimum-norm Gauss-Newton steps (2 equations, C3_NB unknowns) with a backtracking line search; returns the miss */
static double C3_SolveB(const C3Course *c, int ks, int kj, const double *h0, double x0, double z0, double tx, double tz, double *cb)
{
    int it, j; double x, z, e;
    for (j = 0; j < C3_NB; j++) cb[j] = 0;
    C3_PathB(c, ks, kj, h0, cb, x0, z0, &x, &z); e = sqrt((tx - x) * (tx - x) + (tz - z) * (tz - z));
    for (it = 0; it < 300 && e > 0.5; it++) {
        double J[2][C3_NB], rx = tx - x, rz = tz - z, A[3] = { 0, 0, 0 }, det, y0, y1, d[C3_NB], step = 1, eps = 1e-5, t[C3_NB];
        for (j = 0; j < C3_NB; j++) { double x1, z1; memcpy(t, cb, sizeof t); t[j] += eps; C3_PathB(c, ks, kj, h0, t, x0, z0, &x1, &z1); J[0][j] = (x1 - x) / eps; J[1][j] = (z1 - z) / eps; }
        for (j = 0; j < C3_NB; j++) { A[0] += J[0][j] * J[0][j]; A[1] += J[0][j] * J[1][j]; A[2] += J[1][j] * J[1][j]; }   /* J J^T */
        det = A[0] * A[2] - A[1] * A[1];
        if (fabs(det) < 1e-12) break;
        y0 = (A[2] * rx - A[1] * rz) / det; y1 = (-A[1] * rx + A[0] * rz) / det;
        for (j = 0; j < C3_NB; j++) d[j] = J[0][j] * y0 + J[1][j] * y1;   /* J^T (J J^T)^-1 r */
        for (; step > 1e-4; step *= 0.5) {   /* the map is far from linear: take the step only as far as it helps */
            double x1, z1, e1;
            for (j = 0; j < C3_NB; j++) t[j] = cb[j] + d[j] * step;
            C3_PathB(c, ks, kj, h0, t, x0, z0, &x1, &z1); e1 = sqrt((tx - x1) * (tx - x1) + (tz - z1) * (tz - z1));
            if (e1 < e) { memcpy(cb, t, sizeof t); x = x1; z = z1; e = e1; break; }
        }
        if (step <= 1e-4) break;
    }
    return e;
}
/* B's real length.  The course file pairs B with A piece by piece over A's split stretch, but in the game B is a road of its own and can be shorter
   (Sierra: 20 pieces, Peninsula: 16) or longer (Pacific Highway: 20 - not in the file at all).  Where B is shorter, the file's last B entries just repeat
   B's last values (the extractor's B cursor had run out): a run of identical curve / hill ending at the join.  Returns the pieces of that tail beyond
   B's real end (run - 1: its first piece is still a real one; 0 when there is no run) */
static int C3_BTail(const C3Course *c, int ks, int kj)
{
    int j = kj - 1, run;
    const RoadSection *e = &c->piece[kj - 1].halfB;
    while (j > ks && c->piece[j - 1].halfB.curve == e->curve && c->piece[j - 1].halfB.hill == e->hill) j--;
    run = kj - j;
    if (run < 3 || run > (kj - ks) / 4) return 0;
    return run - 1;
}
/* near the fork and the join B leaves / meets A so gradually that B's inner rail (at its left outer edge) stands on A's (at A's right outer edge) - one
   rail instead of software's two, side by side with the dark wedge between.  B is pushed outwards along A's right vector there: by what it needs to keep
   its outer edge C3_RAILGAP right of A's (the gap ramping up from 0 over C3_RAILRAMP pieces from the fork, and down to the join), the push faded out over
   C3_PUSHFADE pieces beyond (A is out of view there, C3_HideHalf) */
#define C3_SEPK 24   /* pieces after the fork / before the join where both carriageways are shown (C3_NearForkJoin) */
#define C3_RAILGAP 120.0
#define C3_RAILRAMP 4
#define C3_PUSHFADE 40
static void C3_PushB(C3Course *c, int ks, int kb, int tail)
{
    int n = kb - ks, i, j; double *need, *push;
    if (n < 4 || !(need = (double *)calloc(n + 1, sizeof(double)))) return;
    if (!(push = (double *)calloc(n + 1, sizeof(double)))) { free(need); return; }
    for (i = 0; i <= n; i++) {
        int k = ks + i, ka = k + (i > n / 2 ? tail : 0);   /* (the A piece beside it: C3_FramesB's pairing) */
        double th, d, gap, ramp;
        if (ka > c->n || k >= c->n || (i > C3_SEPK && i < n - C3_SEPK)) continue;   /* (only where both carriageways are shown: the middle of B's made-up path may be anywhere) */
        th = c->frame[ka].heading;
        d = (c->frameB[k].x - c->frame[ka].x) * cos(th) - (c->frameB[k].z - c->frame[ka].z) * sin(th);   /* B's centre right of A's */
        ramp = i < n / 2 ? (double)i / C3_RAILRAMP : (double)(n - i) / C3_RAILRAMP; if (ramp > 1) ramp = 1;
        gap = C3_RAILGAP * ramp;
        need[i] = (c->piece[ka].halfA.outerR + gap) - (d + c->piece[k].halfB.outerL);
        if (need[i] < 0) need[i] = 0;
    }
    for (i = 0; i <= n; i++) {   /* push(i) = max over j of need(j) faded by the distance |i - j| (smooth step) */
        double best = 0;
        for (j = i - C3_PUSHFADE; j <= i + C3_PUSHFADE; j++) { double f, t; if (j < 0 || j > n || need[j] <= 0) continue; t = 1.0 - fabs((double)(i - j)) / C3_PUSHFADE; f = t * t * (3 - 2 * t); if (need[j] * f > best) best = need[j] * f; }
        push[i] = best;
    }
    for (i = 1; i < n; i++) {   /* (B's first / last frame stay A's / the join) */
        int k = ks + i, ka = k + (i > n / 2 ? tail : 0); double th;
        if (ka > c->n) continue;
        th = c->frame[ka].heading;
        c->frameB[k].x += push[i] * cos(th); c->frameB[k].z -= push[i] * sin(th);
    }
    free(need); free(push);
}

static void C3_FramesB(C3Course *c)
{
    int k, ks, kj, kb, j;
    c->frameB = (C3Frame *)realloc(c->frameB, sizeof(C3Frame) * (c->n + 1));
    memcpy(c->frameB, c->frame, sizeof(C3Frame) * (c->n + 1));
    c->bdead = (unsigned char *)realloc(c->bdead, c->n + 1); c->pairA = (int *)realloc(c->pairA, sizeof(int) * (c->n + 1)); c->pairB = (int *)realloc(c->pairB, sizeof(int) * (c->n + 1));
    if (!c->bdead || !c->pairA || !c->pairB) return;
    for (k = 0; k <= c->n; k++) { c->bdead[k] = 0; c->pairA[k] = c->pairB[k] = k; }
    for (ks = 0; ks < c->n; ks = kj) {
        double dth, raw, turn = 0, best = 1e30, cbest[C3_NB] = { 0 }, ex, ey, ez, *h0; float p0[3]; C3Frame *b = c->frameB; int m, n, tail;
        if (c->piece[ks].layout != 2) { kj = ks + 1; continue; }
        for (kj = ks; kj < c->n && c->piece[kj].layout == 2; kj++) {}
        if (kj >= c->n) break;   /* (no join: B as far as A goes) */
        tail = C3_BTail(c, ks, kj); kb = kj - tail;   /* B's pieces ks .. kb - 1; B piece kb's start = A's join (piece kj) */
        for (k = kb; k < kj; k++) c->bdead[k] = 1;
        for (k = ks + (kb - ks) / 2; k < kb; k++) { c->pairA[k] = k + tail; c->pairB[k + tail] = k; }   /* (second half: B beside A + tail; first half beside the same index) */
        n = kb - ks;
        if (!(h0 = (double *)malloc(sizeof(double) * n))) break;
        for (k = ks; k < kb; k++) b[k].heading += c->piece[k].splitSlope / c->L;   /* (b = A's frames, headings shifted by the earlier stretches' whole turns) */
        C3_PointF(c, c->frame, ks, c->piece[ks].splitX, 0, p0);
        b[ks].x = p0[0]; b[ks].y = p0[1]; b[ks].z = p0[2];
        raw = b[kj].heading - b[kb - 1].heading;
        for (m = -1; m <= 1; m++) {   /* the whole-turn choices around the nearest: the one with the smaller bend (residual first, then |c1| + |c2|) */
            double tn = (floor(raw / (2 * C3_PI) + 0.5) + m) * 2 * C3_PI, cb[C3_NB], e, cost = 0; int j2;
            for (k = ks; k < kb; k++) h0[k - ks] = b[k].heading + (raw - tn) * C3_S((double)(k - ks + 1) / n, n);
            e = C3_SolveB(c, ks, kb, h0, b[ks].x, b[ks].z, c->frame[kj].x, c->frame[kj].z, cb);
            for (j2 = 0; j2 < C3_NB; j2++) cost += fabs(cb[j2]) * (j2 + 1);   /* ~ the largest added curvature */
            if (e > 10) cost += 1e6 + e;
#ifdef C3_DEBUGB
            printf("  split %d..%d turn %+.0f: bend %.3f %.3f %.3f %.3f %.3f %.3f residual %.1f\n", ks, kj, tn / (2 * C3_PI), cb[0], cb[1], cb[2], cb[3], cb[4], cb[5], e);
#endif
            if (cost < best) { best = cost; turn = tn; memcpy(cbest, cb, sizeof cb); }
        }
        dth = raw - turn;
        for (k = ks; k < kb; k++) b[k].heading += dth * C3_S((double)(k - ks + 1) / n, n) + C3_Bend(cbest, C3_W((k - ks + 0.5) / n, n));
        for (j = kj; j <= c->n; j++) b[j].heading -= turn;   /* from the join on: the same directions, continuous with B's (interpolation) */
        for (k = ks; k < kb; k++) {
            b[k + 1].x = b[k].x + c->L * sin(b[k].heading); b[k + 1].z = b[k].z + c->L * cos(b[k].heading);
            b[k + 1].y = b[k].y + c->piece[k].halfB.hill;
        }
        ex = c->frame[kj].x - b[kb].x; ey = c->frame[kj].y - b[kb].y; ez = c->frame[kj].z - b[kb].z;   /* (x / z: what the solver left - up to ~20000 units on Peninsula) */
        for (j = ks + 1; j <= kb; j++) { double t = (double)(j - ks) / n, s = C3_S(t, n); b[j].x += ex * s; b[j].y += ey * t; b[j].z += ez * s; }   /* (x / z: smooth, none added at either end) */
        if (kb < kj) b[kb].heading = b[kj].heading;   /* B's end = A's join (position from the spread above, heading A's) */
        if (!c->forkPick && !c->soft) C3_PushB(c, ks, kb, kj - kb);   /* (forks off: nothing to keep apart; the original's way: B is the camera's path, on its own curves) */
        for (j = kb + 1; j < kj; j++) b[j] = b[kb];   /* (B's phantom tail: not built) */
        free(h0);
    }
}

/* lateral position of the middle of carriageway A's road surface at piece position p (the edges are not always symmetric: -250 / 750 ...) */
static double C3_RoadMid(const C3Course *c, double p)
{
    int k = (int)floor(p); double f = p - k;
    const RoadSection *a, *b;
    if (k < 0) k = 0;
    if (k >= c->n - 1) k = c->n - 2;
    a = &c->piece[k].halfA; b = &c->piece[k + 1].halfA;
    return (a->edgeL + a->edgeR) * 0.5 * (1 - f) + (b->edgeL + b->edgeR) * 0.5 * f;
}

/* ---- cross-sections ---- */
enum { C3_NP = 15 };            /* points per carriageway cross-section, left to right */
enum { M_FAR, M_TERRAIN, M_SLOPE, M_VERGE, M_WALK, M_CURB, M_ROAD, M_WALL };
typedef struct C3Row { double u[C3_NP], h[C3_NP]; int mat[C3_NP - 1]; unsigned char wm[2]; double wl[2]; unsigned char hm[2]; double hl[2]; } C3Row;   /* (hm / hl: each side's hole cells, their water level - C3_HoleRow) */   /* (wm / wl: each side's water - C3_WaterRow) */

static int g_c3Editor;   /* 1 = the Course Editor's own 3D view (courseedit.c, P3D): water, holes, dunes, cliffs, curbs, footpaths and gutters drawn flat, as painted */
#define C3_TOP 1000.0    /* a cliff's top: rr_NearWallDraw steps it outwards from the face's top edge in tiles of 4 x lane record +0x24 = 1000 units (row 1, TOP1A) */
#define C3_PLAT 4000.0   /* ... to the screen edge; here 1 + 4 tiles of a flat plateau at the face's height */
#define C3_FAR 2500.0   /* the flat extension beyond the last profile point (longer ones stick out through other parts of a winding course) */
/* the kind of piece k's half h on side 0 left / 1 right: the piece's - or, edited (course_edit.h RrcExtra.sideKind 2), terrain on that side of an open-road
   piece (on a terrain piece the mark stays: its ground found further off where the sets loaded have none, C3_FarGround) */
static int C3_SideKind(const RxAssets *ra, const RoadSection *s, int k, int h, int side)
{
    if (s->kind == SEC_OPEN && ra && ra->ext && k >= 0 && k < ra->next && ra->ext[k].sideKind[h & 1][side & 1] == 2) return SEC_PROFILE;
    return s->kind;
}
/* buildings on piece k's terrain side (RrcExtra.bld): from band 1 / 2 outwards (3: past P2, 4: from P0 - the verge's edge), 0 none */
static int C3_SideBld(const RxAssets *ra, const RoadSection *s, int k, int h, int side)
{
    int b;
    if (!ra || !ra->ext || k < 0 || k >= ra->next || C3_SideKind(ra, s, k, h, side) != SEC_PROFILE) return 0;
    b = ra->ext[k].bld[h & 1][side & 1];
    return b >= 1 && b <= 4 ? b : 0;
}
/* a footpath on piece k's terrain side (RrcExtra.walk): 1 + its set code, 0 none */
static int C3_SideWalk(const RxAssets *ra, const RoadSection *s, int k, int h, int side)
{
    if (!ra || !ra->ext || k < 0 || k >= ra->next || C3_SideKind(ra, s, k, h, side) != SEC_PROFILE) return 0;
    return ra->ext[k].walk[h & 1][side & 1];
}
#define C3_KERB 20.0   /* a footpath's kerb (the City's) */
/* the edits that shape a side: its buildings (C3_SideBld, & 7) | 8 = a footpath (C3_SideWalk) | 16 = its road edge painted (RrcExtra.edge: user, 2026-11-18 "i still cant
   paint curbs" - a terrain side has none, its edge at road level: a kerb then, C3_KERB high) */
static int C3_SideCells(const RxAssets *ra, const RoadSection *s, int k, int h, int side, int g);
static int C3_SideEd(const RxAssets *ra, const RoadSection *s, int k, int h, int side)
{
    int e = C3_SideBld(ra, s, k, h, side) | (C3_SideWalk(ra, s, k, h, side) ? 8 : 0);
    if (ra && ra->ext && k >= 0 && k < ra->next && (ra->ext[k].lift[h & 1][side & 1][0] | ra->ext[k].lift[h & 1][side & 1][1])) e |= 32;   /* (heights lifted: RrcExtra.lift) */
    if (ra && ra->ext && k >= 0 && k < ra->next && C3_SideKind(ra, s, k, h, side) == SEC_PROFILE && (ra->ext[k].edge[h & 1][side & 1] & RRC_BAND)) e |= 16;   /* (an original course: no extras) */
    if (!g_c3Editor) e |= C3_SideCells(ra, s, k, h, side, 2) << 6;   /* (the game: its holes - a hole ends square at its piece's end, a wall closing it: user, 2026-11-19
                                                                              "should just go all the way down on 52"; bits 6.. read by nothing else) */
    return e;
}
/* a side that ends square at piece k1 (user, 2026-11-17: "these should just be straight ... really they should just be rectangles"): an edited side (terrain painted
   on, buildings, a footpath) next to a side of another kind / shape keeps its own cross-section to the end of its piece, not drawn out to the other's */
static int C3_SideSig(const RxAssets *ra, const RoadSection *s, int k, int h, int side, int *ed)
{
    int m = ra && ra->ext && k >= 0 && k < ra->next && ra->ext[k].sideKind[h & 1][side & 1] == 2, e = C3_SideEd(ra, s, k, h, side);
    *ed = m || e;
    return C3_SideKind(ra, s, k, h, side) | e << 4 | m << 20;
}
static int C3_SideBreak(const RxAssets *ra, const RoadSection *s0, int k0, int h0, const RoadSection *s1, int k1, int h1, int side)
{
    int e0, e1, a = C3_SideSig(ra, s0, k0, h0, side, &e0), b = C3_SideSig(ra, s1, k1, h1, side, &e1);
    return (e0 || e1) && a != b;
}
/* one side (s = -1 left, +1 right) of a section, from the road edge outwards: 7 points (edge at road level, edge top, outer, 3 terrain, far); kind: that side's;
   ed: its edits (C3_SideEd) - & 7 buildings from that band on (C3_SideBld; 4 from P0): the terrain beyond its inner point flat, a building's depth (C3_BLDG) a band;
   & 4 a footpath: road edge -> outer edge at kerb height */
static void C3_Side(const RoadSection *sec, int s, int kind, int ed, double *u, double *h, int *mat)
{
    int bld = ed & 7;
    int r = s > 0, i;
    double edge = r ? (sec->laneEdgeR < sec->edgeR ? sec->laneEdgeR : sec->edgeR) : (sec->laneEdgeL > sec->edgeL ? sec->laneEdgeL : sec->edgeL), outer = r ? sec->outerR : sec->outerL, top = 0, t[3], th[3];
    /* (the road surface ends at the lane edge when that lies inside the road edge - a lane being added / dropped: Sierra 876..887, Napa 114..128 - the
       verge (outer = lane edge + shoulder) follows it, as the original draws it; the road edge jumps in one piece there, so using it left the verge
       collapsed and a sliver of terrain across the road) */
    if (kind >= SEC_WALL1 && kind <= SEC_WALL_SPLIT) outer = edge;   /* walls: one shoulder strip from the road edge to the wall foot (SideStripDraw), no separate verge */
    if (kind == SEC_OPEN) {
        top = sec->shoulderH[r];
        for (i = 0; i < 3; i++) { t[i] = outer + s * (500.0 + 1500.0 * i); th[i] = top; }
        mat[2] = M_VERGE; mat[3] = M_TERRAIN; mat[4] = M_TERRAIN;
    } else if (kind == SEC_PROFILE) {
        for (i = 0; i < 3; i++) { t[i] = sec->profile[(i * 2 + r) * 2]; th[i] = sec->profile[(i * 2 + r) * 2 + 1];
            if ((t[i] - outer) * s < 0) { t[i] = outer; th[i] = 0; } }   /* (a terrain point inside the verge - the verge widens with a lane being added, Sierra 880..887: on its
                                                                           outer edge at verge level, not a step standing in the verge) */
        if (ed & 24) top = C3_KERB;   /* (a footpath: the verge; a kerb painted) */
        if (bld == 4) for (i = 0; i < 3; i++) { t[i] = outer + s * 1500.0 * (i + 1); th[i] = top; }   /* (from P0: flat from the verge's edge) */
        else if (bld > 0) for (i = bld; i < 3; i++) { t[i] = t[bld - 1] + s * 1500.0 * (i - bld + 1); th[i] = th[bld - 1]; }
        mat[2] = M_TERRAIN; mat[3] = M_SLOPE; mat[4] = M_SLOPE;
    } else {   /* walls (rr_NearWallDraw): the face from (x0, road level) to (x1, height) */
        const int *w = sec->wall + r * 3;
        if (w[2] > 0) { t[0] = w[0]; th[0] = 0; t[1] = w[1]; th[1] = w[2]; t[2] = w[1] + s * C3_TOP; th[2] = w[2]; }   /* a cliff: the face, then its top: flat at the face's height (row 1) */
        else { double dx = w[1] - w[0]; t[0] = w[0]; th[0] = 0; t[1] = w[0] + 0.4 * dx; th[1] = 0.25 * w[2]; t[2] = w[0] + 0.8 * dx; th[2] = 0.85 * w[2]; }   /* a drop (the coast): the face slopes down to (x1, depth) - here an S curve: */
        mat[2] = M_VERGE; mat[3] = M_WALL; mat[4] = M_TERRAIN;                                                                                                       /* rounded at the road, flattening into the sea (C3_Sea) */
    }
    u[0] = edge; h[0] = 0;
    u[1] = edge; h[1] = top;            /* curb face */
    u[2] = outer; h[2] = top;           /* sidewalk / verge */
    for (i = 0; i < 3; i++) { u[3 + i] = t[i]; h[3 + i] = th[i]; }
    u[6] = s * C3_FAR; h[6] = h[5];
    if (kind != SEC_OPEN && kind != SEC_PROFILE) {
        const int *w = sec->wall + r * 3;
        if (w[2] > 0) u[6] = u[5] + s * C3_PLAT;                           /* the plateau */
        else { u[6] = w[0] + 1.1 * (w[1] - w[0]); h[6] = w[2]; }           /* the drop's foot at sea level */
    }
    mat[0] = M_CURB; mat[1] = M_WALK; mat[5] = M_FAR;
    if (kind != SEC_OPEN) mat[1] = M_VERGE;
    if (ed & 56) mat[0] = mat[1] = M_VERGE;   /* (a footpath, a kerb, a lift: drawn by the strip - C3_Roadside draws a street's own) */   /* (a footpath: textured by the build - not C3_Roadside's kerb / path, skipped on open-road pieces) */
    for (i = 1; i < 7; i++) if ((u[i] - u[i - 1]) * s < 0) u[i] = u[i - 1];   /* keep the section going outwards */
}

/* the whole cross-section of a carriageway: 7 left points (outside in), 1 centre point, 7 right points; kl / kr: the sides' kinds (C3_SideKind), bl / br their
   edits (C3_SideEd) */
static void C3_SectionK(const RoadSection *sec, double ofs, C3Row *row, int kl, int kr, int bl, int br)
{
    double u[7], h[7]; int m[6], i;
    C3_Side(sec, -1, kl, bl, u, h, m);
    for (i = 0; i < 7; i++) { row->u[6 - i] = u[i] + ofs; row->h[6 - i] = h[i]; }
    for (i = 0; i < 6; i++) row->mat[5 - i] = m[i];
    row->u[7] = (sec->edgeL + sec->edgeR) * 0.5 + ofs; row->h[7] = 0;
    row->mat[6] = M_ROAD; row->mat[7] = M_ROAD; row->wm[0] = row->wm[1] = 0; row->hm[0] = row->hm[1] = 0; row->wl[0] = row->wl[1] = 0;
    C3_Side(sec, 1, kr, br, u, h, m);
    for (i = 0; i < 7; i++) { row->u[8 + i] = u[i] + ofs; row->h[8 + i] = h[i]; }
    for (i = 0; i < 6; i++) row->mat[8 + i] = m[i];
}
static void C3_Section(const RoadSection *sec, double ofs, C3Row *row) { C3_SectionK(sec, ofs, row, sec->kind, sec->kind, 0, 0); }
/* water (user, 2026-11-18: "how do i use water, like how pacific highway has it?" - "can we make water a block on the ground list"; 2026-11-19: "the water should
   act more like a texture with no depth ... just baked in"): the ground picture WATER (ground_art.h, k_c3Ground[C3_GWATER]) painted on a terrain side's cells
   P0, P1, P2 or past P2 (1..4) - the cell drawn as water where it lies (C3_Strip: the sea's water shading, vertex alpha 253, render3d.h), nothing to set; past P2
   painted: on out to the horizon from its far end, as Pacific Highway's sea (C3_Water, C3_SEA) */
#define C3_GWATER 1
static int C3_IsWaterTex(const RxAssets *ra, int t) { return ra && ra->nground > C3_GWATER && t == ra->ground0 + C3_GWATER; }
static int C3_IsBridgeTex(const RxAssets *ra, int t) { return ra && ra->nground > 3 && t == ra->ground0 + 3; }
static int C3_IsPitTex(const RxAssets *ra, int t);
static int C3_NotPitTex(const RxAssets *ra, const int *tex, int i)   /* (a hole's sides / its end wall: the picture of the nearest strip on its side that is no pit - towards the
                                                                       road first, then outwards; -1 none) */
{
    int left = i < C3_NP / 2, d, j;
    for (d = 1; d < C3_NP; d++) { j = left ? i + d : i - d; if (j < 0 || j >= C3_NP - 1 || (j < C3_NP / 2) != left) break; if (tex[j] >= 0 && !C3_IsPitTex(ra, tex[j])) return tex[j]; }
    for (d = 1; d < C3_NP; d++) { j = left ? i - d : i + d; if (j < 0 || j >= C3_NP - 1 || (j < C3_NP / 2) != left) break; if (tex[j] >= 0 && !C3_IsPitTex(ra, tex[j])) return tex[j]; }
    return -1;
}
static int C3_IsPitTex(const RxAssets *ra, int t) { return C3_IsWaterTex(ra, t) || (ra && ra->nground > 2 && t == ra->ground0 + 2); }   /* (water or a hole - C3_GHOLE: the ground round it torn over its edge, never it over the ground) */
static int C3_SideCells(const RxAssets *ra, const RoadSection *s, int k, int h, int side, int g)   /* bits 0..4: the cells (verge, P0..P2, past P2) of piece k's side painted
                                                                                                  extra ground picture g (ground_art.h) */
{
    const RrcExtra *x; unsigned w = RRC_BAND | RRC_GEN | (unsigned)g; int c, m = 0;
    if (!ra || !ra->ext || k < 0 || k >= ra->next || ra->nground <= g || C3_SideKind(ra, s, k, h, side) != SEC_PROFILE) return 0;
    x = &ra->ext[k];
    if (x->cell[h & 1][side & 1][0] == w) m |= 1;
    for (c = 1; c <= 3; c++) if (x->band[h & 1][side & 1][c - 1] == w) m |= 1 << c;
    if (x->cell[h & 1][side & 1][1] == w) m |= 16;
    return m;
}
static int C3_SideWater(const RxAssets *ra, const RoadSection *s, int k, int h, int side) { return C3_SideCells(ra, s, k, h, side, C3_GWATER) & ~1; }   /* bits 1..4: water */
/* a hole (user, 2026-11-19: "a hole texture which ... just in 3d mode will just reduce the height of the cell to -1000 ... in editing mode ... just another texture"): the
   ground picture HOLE (k_c3Ground[C3_GHOLE]) painted on a terrain side's cell - in the game (not the Course Editor's own view: g_c3Editor) the cell's corners sunk to
   C3_HOLEH (the cells beside it slope down into it); the player in it loses (road\course_edit.c rr_CourseEditWater) */
#define C3_GHOLE 2
#define C3_GBRIDGE 3
#define C3_GDUNES 4    /* (DUNES: sand cells rolled into dunes in the game - C3_Dunes; flat in the editor) */
#define C3_GCLIFF 5    /* (CLIFF: cells raised into a cliff in the game - C3_CliffRow; flat in the editor) */   /* (BRIDGE: a deck painted on the verge - concrete, its railing on the outer edge: C3_Railing) */
#define C3_HOLEH (-2000.0)
#define C3_ROCKPER 900.0   /* (a hole's rock wall: units per copy of the CLIFF picture, both ways - its sides and ends alike) */
#define C3_HLIP 220.0   /* (a hole's wall: this far across at most - steep, not sheer: a lip) */
#define C3_HOLEWD 1500.0   /* (the water in a hole: this far below the ground - 500 above its floor - unless set: RrcExtra.holeWater) */   /* (doubled: user, 2026-11-19) */
static void C3_HoleRow(const RxAssets *ra, const RoadSection *sec, int k, int h, int side, C3Row *row)
{
    int m, c, i;
    if (g_c3Editor || !(m = C3_SideCells(ra, sec, k, h, side, C3_GHOLE))) return;
    {   int fc = 0, wv = ra->ext[k].holeWater[h & 1][side & 1]; while (fc < 4 && !(m & (1 << fc))) fc++;   /* (the water: below the ground inside the first hole cell) */
        row->hm[side] = (unsigned char)(wv < 0 ? 0 : m); row->hl[side] = row->h[side ? 8 + fc + 1 : 6 - (fc + 1)] - (wv > 0 ? wv : C3_HOLEWD); }
    /* each run of hole cells c0 .. c1: its points sunk; its walls where it meets ground (user, 2026-11-19: "a sheer edge" for a BRIDGE, then "it naturally drops down not
       just drops down on a sudden incline", "the terrain is missing in beyond p2"): the boundary point on the ground's side stays up, the first point inside drawn in
       towards it - a steep wall with a lip (C3_HLIP), the ground's ragged edge torn over it (C3_Blend); a single cell: its inner wall only, the cell outside sloping up */
    {   double base[7], sg = side ? 1 : -1; int c0, c1;
        for (i = 0; i < 7; i++) base[i] = row->h[side ? 8 + i : 6 - i];
#define C3_HP(i) (side ? 8 + (i) : 6 - (i))
        for (c0 = 0; c0 <= 4; c0++) if (m & (1 << c0)) {
            int inUp, outUp, n;
            c1 = c0; while (c1 < 4 && (m & (1 << (c1 + 1)))) c1++;
            for (i = c0 + 1; i <= c1 + 2; i++) row->h[C3_HP(i)] = C3_HOLEH;
            inUp = c0 >= 1; outUp = c1 < 4 && c1 > c0; n = c1 - c0;   /* (n: points inside the run) */
            if (inUp) row->h[C3_HP(c0 + 1)] = base[c0 + 1];
            if (outUp) row->h[C3_HP(c1 + 2)] = base[c1 + 2];
            if (inUp && (n >= 2 || n == 0 || !outUp)) { double w = fabs(row->u[C3_HP(c0 + 2)] - row->u[C3_HP(c0 + 1)]), l = w * 0.3 < C3_HLIP ? w * 0.3 : C3_HLIP;
                row->u[C3_HP(c0 + 2)] = row->u[C3_HP(c0 + 1)] + sg * l; }
            if (outUp && (n >= 2 || !inUp)) { double w = fabs(row->u[C3_HP(c1 + 2)] - row->u[C3_HP(c1 + 1)]), l = w * 0.3 < C3_HLIP ? w * 0.3 : C3_HLIP;
                row->u[C3_HP(c1 + 1)] = row->u[C3_HP(c1 + 2)] - sg * l; }
            c0 = c1; }
#undef C3_HP
    }
}
/* cliffs (user, 2026-11-19: "cliff textures that are flat in P3D but actually form full cliffs ... the cliff textures should dynamically join together"): the cells painted
   CLIFF - in the game their points raised C3_CliffH (varying slowly along the road, a little more for each cliff cell further out: the cells next to each other one rock
   mass, rising away from the road); where a cliff starts outside a cell that is not one, its face steep - the raised point drawn in towards the foot (C3_Strip: rock) */
static double C3_CliffH(int k, int n) { return 1500.0 + 600.0 * (0.5 + 0.5 * sin(k * 0.23 + 0.7 * sin(k * 0.071))) + 350.0 * n; }
static void C3_CliffRow(const RxAssets *ra, const RoadSection *sec, int k, int h, int side, C3Row *row)
{
    int m, c, i, n = 0; double sg = side ? 1 : -1, base[7];
    if (g_c3Editor || !(m = C3_SideCells(ra, sec, k, h, side, C3_GCLIFF))) return;
    for (i = 0; i < 7; i++) base[i] = row->h[side ? 8 + i : 6 - i];
    for (c = 0; c <= 4; c++) if (m & (1 << c)) { int first = c == 0 || !(m & (1 << (c - 1)));
        double H = C3_CliffH(k, n++);
        for (i = first ? c + 2 : c + 1; i <= c + 2; i++) { double t = base[i] + H; if (row->h[side ? 8 + i : 6 - i] < t) row->h[side ? 8 + i : 6 - i] = t; }
        if (first) {   /* (the face: steep - its top drawn in towards its foot) */
            double u0 = row->u[side ? 8 + c + 1 : 6 - (c + 1)], u1 = row->u[side ? 8 + c + 2 : 6 - (c + 2)], w = fabs(u1 - u0), in = w * 0.75;
            if (w - in > 220) in = w - 220;
            row->u[side ? 8 + c + 2 : 6 - (c + 2)] = u1 - sg * in; } }
}
static void C3_WaterRow(const RxAssets *ra, const RoadSection *sec, int k, int h, int side, C3Row *row)   /* the side's water cells: row->wm (bits 1..4), wl the far end's height
                                                                                                         (flat, where it lies: user, 2026-11-19 - 500 deeper "looks odd") */
{
    row->wm[side] = (unsigned char)C3_SideWater(ra, sec, k, h, side); row->wl[side] = row->h[side ? 14 : 0];
}
static void C3_EdgeStyle(const RxAssets *ra, int k, int h, int side, double *kmul, double *roll, double *gw, double *gd);
static void C3_SectionAt(const RxAssets *ra, const RoadSection *sec, int k, int h, double ofs, C3Row *row)   /* (piece k's: its sides' own kinds; the editor's heights -
                                                                                                         RrcExtra.lift: the curb's top, the verge's outer edge - an open-road
                                                                                                         side's flat ground beyond with it) */
{
    int side, i;
    C3_SectionK(sec, ofs, row, C3_SideKind(ra, sec, k, h, 0), C3_SideKind(ra, sec, k, h, 1), C3_SideEd(ra, sec, k, h, 0), C3_SideEd(ra, sec, k, h, 1));
    for (side = 0; side < 2; side++) { int sk = C3_SideKind(ra, sec, k, h, side); double km, rl, gw, gd, sg = side ? 1 : -1;   /* (the curb's style: C3_EdgeStyle) */
        if (sk != SEC_OPEN && sk != SEC_PROFILE) continue;
        C3_EdgeStyle(ra, k, h, side, &km, &rl, &gw, &gd);
        if (km != 1) for (i = 1; i <= (sk == SEC_OPEN ? 6 : 2); i++) row->h[side ? 8 + i : 6 - i] *= km;
        if (rl > 0) { row->u[side ? 9 : 5] += sg * rl; if ((row->u[side ? 10 : 4] - row->u[side ? 9 : 5]) * sg < 0) row->u[side ? 10 : 4] = row->u[side ? 9 : 5]; } }
    if (!ra || !ra->ext || k < 0 || k >= ra->next) return;
    for (side = 0; side < 2; side++) { const short *L = ra->ext[k].lift[h & 1][side]; int sk = C3_SideKind(ra, sec, k, h, side);
        if (!(L[0] | L[1]) || (sk != SEC_OPEN && sk != SEC_PROFILE)) continue;
        row->h[side ? 9 : 5] += L[0]; row->h[side ? 10 : 4] += L[1];
        if (sk == SEC_OPEN) for (i = 3; i <= 6; i++) row->h[side ? 8 + i : 6 - i] += L[1]; }
    for (side = 0; side < 2; side++) { C3_WaterRow(ra, sec, k, h, side, row); C3_HoleRow(ra, sec, k, h, side, row); C3_CliffRow(ra, sec, k, h, side, row); }
}
/* the road edge's style (user, 2026-11-19: "more variations of side gutters and curbs. they should come out flat on P3D but done properly in T3D"; RrcExtra.gutter
   bits 2..4 the curb, 5..6 the gutter): the curb's height times kmul (standard 1, low 0.5, high 2, rolled 1 - its top roll units out from the road: a sloped face -
   flush 0), the gutter gw wide, gd deep (standard 45 / 6, wide 90 / 6, a deep channel 60 / 18); in the editor's P3D all flat (user: footpaths "flat and joined to the road") */
static void C3_EdgeStyle(const RxAssets *ra, int k, int h, int side, double *kmul, double *roll, double *gw, double *gd)
{
    int g = ra && ra->ext && k >= 0 && k < ra->next ? ra->ext[k].gutter[h & 1][side & 1] : 0, ks = (g >> 2) & 7, gs = (g >> 5) & 3;
    static const double km[5] = { 1, 0.5, 2, 1, 0 }, gws[3] = { 45, 90, 60 }, gds[3] = { 6, 6, 18 };
    *kmul = ks < 5 ? km[ks] : 1; *roll = ks == 3 ? 45 : 0; *gw = gws[gs < 3 ? gs : 0]; *gd = gds[gs < 3 ? gs : 0];
    if (g_c3Editor) { *kmul = 0; *roll = 0; *gd = 0; }
}
static double C3_Lift(const RxAssets *ra, int k, int h, int side, int i) { return ra && ra->ext && k >= 0 && k < ra->next ? ra->ext[k].lift[h & 1][side & 1][i] : 0; }   /* (RrcExtra.lift) */

/* ---- mesh ---- */
static unsigned C3_Colour(int mat, int kind, int k)
{
    static const unsigned base[] = { 0x3d6b3a, 0x4a7f3f, 0x5f7a3c, 0x6f8f52, 0xa8a8a0, 0xc8c8c0, 0x5a5a5e, 0x8a7a68 };
    unsigned c = base[mat];
    if (kind == SEC_OPEN && (mat == M_TERRAIN || mat == M_FAR)) c = 0x80807a;          /* town: paved ground */
    if (mat == M_ROAD && (k & 1)) c = 0x56565a;                                        /* alternate segments a little (shows the motion) */
    return 0xff000000u | ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);       /* RGB -> ABGR */
}

static void C3_Push(C3Course *c, const float *p, unsigned rgba)
{
    C3Vert *v;
    if (c->nv == c->cap) {   /* (grown only when both blocks could be: in the 32-bit address space a big doubling - 80 MB and more for a whole course -
                                can fail after a few races; the old code wrote on through the NULL it got back and crashed: user, 2026-11-15) */
        int nc = c->cap ? c->cap * 2 : 65536; C3Vert *nv2 = (C3Vert *)realloc(c->v, sizeof(C3Vert) * nc); int *np;
        if (nv2) c->v = nv2;
        np = nv2 ? (int *)realloc(c->vpiece, sizeof(int) * nc) : NULL;
        if (!np) { static int told; if (!told++) LOG("world3d: out of memory at %d vertices - the rest of this mesh is left out", c->nv); return; }   /* (the vertex dropped; the mesh so far stays) */
        c->vpiece = np; c->cap = nc; }
    c->vpiece[c->nv] = c->cur;
    v = &c->v[c->nv++];
    v->x = p[0]; v->y = p[1]; v->z = p[2]; v->u = 0; v->v = 0; v->rgba = rgba; v->tex = -1; v->ox = 0; v->oy = 0;
    if (c->tintFr && c->fr == c->tintFr) v->rgba = (rgba & 0xff0000ffu) | 0x00202000u;   /* (test: these frames' geometry drawn red) */
}

static unsigned C3_Shade(unsigned rgba, const float *a, const float *b, const float *d)
{
    /* Lambert with a fixed sun from the upper left front, ambient 0.55 */
    float e1[3], e2[3], n[3], l, sh; static const float sun[3] = { -0.35f, 0.85f, 0.4f };
    int i; unsigned out = rgba & 0xff000000u;
    for (i = 0; i < 3; i++) { e1[i] = b[i] - a[i]; e2[i] = d[i] - a[i]; }
    n[0] = e1[1] * e2[2] - e1[2] * e2[1]; n[1] = e1[2] * e2[0] - e1[0] * e2[2]; n[2] = e1[0] * e2[1] - e1[1] * e2[0];
    l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l <= 0) return rgba;
    if (n[1] < 0) l = -l;               /* face up */
    sh = (n[0] * sun[0] + n[1] * sun[1] + n[2] * sun[2]) / l;
    sh = ((rgba | 0x01000000u) == 0xffffffffu) ? 0.85f + 0.15f * (sh > 0 ? sh : 0)   /* textured (alpha 254: a facade, below): the original is unlit, only a hint of shape */
                               : 0.55f + 0.45f * (sh > 0 ? sh : 0);
    for (i = 0; i < 3; i++) { float ch = (float)((rgba >> (i * 8)) & 0xff) * sh; out |= (unsigned)(ch > 255 ? 255 : ch) << (i * 8); }
    return out;
}

/* one quad: corners a, b (near row, left / right), cc, d (far row, right / left); tex = RxTex index (-1 flat); uv per corner in texture repeats */
static void C3_QuadTint(C3Course *c, const float *a, const float *b, const float *cc, const float *d, unsigned rgba, int tex, const float *uv, unsigned tint)   /* tint: textured faces (white = the texture as is) */
{
    const float *p[4]; int i, o[6] = { 0, 3, 1, 1, 3, 2 };
    unsigned s = C3_Shade(tex >= 0 ? tint : rgba, a, b, d);
    p[0] = a; p[1] = b; p[2] = cc; p[3] = d;
    for (i = 0; i < 6; i++) {
        C3Vert *v;
        C3_Push(c, p[o[i]], s);
        v = &c->v[c->nv - 1];
        v->tex = (float)tex;
        if (uv) { v->u = uv[o[i] * 2]; v->v = uv[o[i] * 2 + 1]; }
    }
}
static void C3_QuadT(C3Course *c, const float *a, const float *b, const float *cc, const float *d, unsigned rgba, int tex, const float *uv) { C3_QuadTint(c, a, b, cc, d, rgba, tex, uv, 0xffffffffu); }
static void C3_Quad(C3Course *c, const float *a, const float *b, const float *cc, const float *d, unsigned rgba) { C3_QuadT(c, a, b, cc, d, rgba, -1, NULL); }
static void C3_Mark(C3Course *c, int from, unsigned a)   /* vertices from .. now: vertex alpha a (a surface the shader knows), where it is still 255 */
{   int j; for (j = from; j < c->nv; j++) if ((c->v[j].rgba >> 24) == 0xff) c->v[j].rgba = (c->v[j].rgba & 0xffffffu) | (a << 24); }

/* a strip between two cross-section points (u, h) of piece k and the same two points of piece k + 1, textured with u across (0..1) and v along
   (0..1 per segment), or, vertical = 1, u along and v down the face */
static void C3_Span(C3Course *c, int k, double u0, double h0, double u1, double h1, double w0, double g0, double w1, double g1, unsigned rgba, int tex, int vertical)
{
    float a[3], b[3], cc[3], d[3], uv[8];
    C3_Point(c, k, u0, h0, a); C3_Point(c, k, u1, h1, b); C3_Point(c, k + 1, w1, g1, cc); C3_Point(c, k + 1, w0, g0, d);
    if (vertical) { uv[0] = 0; uv[1] = 1; uv[2] = 0; uv[3] = 0; uv[4] = 1; uv[5] = 0; uv[6] = 1; uv[7] = 1; }   /* a = foot, b = top (h1 > h0) */
    else { uv[0] = 0; uv[1] = 1; uv[2] = 1; uv[3] = 1; uv[4] = 1; uv[5] = 0; uv[6] = 0; uv[7] = 0; }
    C3_QuadT(c, a, b, cc, d, rgba, tex, uv);
}

/* the road surface of a carriageway: one quad per lane (rr_RoadsideStripDraw), textured with the track's lane strips (PieceTablePick: tbl_roadTexLanes[0..2] =
   PLAIN / WHITE / YELLOW; TEXG indexes 17..19): the white line is on a lane's left edge, the yellow line on the centre line (first lane right of it).
   The lane grid is anchored on the centre line: it starts lanesL whole lanes left of it (rr_RoadsideStripDraw / PieceTablePick count from there and give lane
   lanesL the yellow), so the centre line stays at lateral 0 - where the traffic keeps to - while a lane is being added on one side or the road narrows
   (fractional laneL / laneR).  Each lane is clipped to the lane edges of its end of the piece, u with it, so the lines keep their places. */
/* the gutter along a town street's curbs (C3_Roadside draws its concrete pan): C3_GUTW units of road beside the curb slope down to C3_GUTD below the road
   at the curb's foot (user, 2026-11-15: "make the city gutters dip a little bit") - the outermost lanes end in that slope (C3_Lanes, gut) */
#define C3_GUTW 45.0
#define C3_GUTD 6.0
static void C3_Lanes(C3Course *c, int k, const RoadSection *s0, double o0, const RoadSection *s1, double o1, const RxAssets *ra, double gut)
{
    int nl = (int)s0->lanesL + (int)s0->lanesR, j;
    double l0 = -250.0 * s0->lanesL, w0 = 250;   /* rr_RoadsideStripDraw: lanes of a fixed 250 units (lane record +0x24 = scale * 0xfa); one grid for the whole piece */
    l0 += s0->laneEdgeL + s0->laneL * (250.0 / 256);   /* the centre line = left lane edge + laneL (8.8 lanes): 0 on carriageway A, but a split road's B lies off its own
                                                        centre (Sierra: lanes 250..750, laneL 0; Peninsula: 0..750, laneL 1) - its centre line is 250 */
    /* a lane being added counts from the next piece on (lanesR rounds up its laneR) while this piece's far end already reaches into it - Sierra 882: 3
       whole lanes (to 500), far lane edge 541: the wedge between had no road (a hole along the new lane).  The grid covers the lanes of both ends
       (each lane is clipped to the lane edges of each end, so the extra ones only fill what the edges reach) */
    {   int exL = (int)s1->lanesL - (int)s0->lanesL, exR = (int)s1->lanesR - (int)s0->lanesR;
        if (exL < 0) exL = 0;
        if (exR < 0) exR = 0;
        j = -exL; nl += exR; }
    for (; j < nl; j++) {
        double x0 = l0 + j * w0, x1 = x0 + w0;
        double el0 = s0->laneEdgeL > s0->edgeL ? s0->laneEdgeL : s0->edgeL, er0 = s0->laneEdgeR < s0->edgeR ? s0->laneEdgeR : s0->edgeR;   /* (lanes end at the road edge too: */
        double el1 = s1->laneEdgeL > s1->edgeL ? s1->laneEdgeL : s1->edgeL, er1 = s1->laneEdgeR < s1->edgeR ? s1->laneEdgeR : s1->edgeR;   /*  C3_OneRoadWidth narrows the road only) */
        double n0 = x0 > el0 ? x0 : el0, n1 = x1 < er0 ? x1 : er0;   /* near end (piece k), clipped */
        double f0 = x0 > el1 ? x0 : el1, f1 = x1 < er1 ? x1 : er1;   /* far end (piece k + 1) */
        int tex = -1, cob = 0, m0 = c->nv;
        if (ra) {   /* PieceTablePick: finish line (half flag 0x20) -> g_roadTexFinish; start grid (kind flags 8 / 0x10) -> tbl_roadTexStart[v]; cross street
                       (0x20) -> tbl_roadTexPlain (plain); else tbl_roadTexLanes[v] (TEXG index = (address - 0x4c3c9c) / 4).  v: lane 0 plain, lane lanesL
                       yellow; while the left side is fractional lanes 0..1 plain (the line of the lane being added is not drawn yet); the lanes past
                       a fractional right side plain; else white */
            int v = 0;
            if (j > 0) {
                if (j == (int)s0->lanesL) v = 2;
                else if ((s0->laneL & 0xff) != 0 && j <= 1) v = 0;
                else if ((s0->laneR & 0xff) == 0) v = 1;
                else v = (s0->laneR >> 8) + (int)s0->lanesL > j;
            }
            if (s0->half & 0x20) tex = ra->global[9];
            else if (s0->kindFlags & 0x18) tex = ra->global[13 + v];
            else if (s0->kindFlags & 0x20) tex = ra->global[5];
            else tex = ra->global[17 + v];
            if (tex < 0) tex = ra->global[17 + v];
            if (ra->ext && k >= 0 && k < ra->next) {   /* the editor's road surface (RrcExtra.road: the lanes left / right of the centre line) */
                unsigned rv = ra->ext[k].road[s0 == &c->piece[k].halfB][j < (int)s0->lanesL ? 0 : 1];
                if (j >= 0 && j < 8 && ra->ext[k].lane[s0 == &c->piece[k].halfB][j]) rv = ra->ext[k].lane[s0 == &c->piece[k].halfB][j];   /* (this lane's own: RrcExtra.lane) */
                if ((rv & (RRC_BAND | RRC_TEXG)) == (RRC_BAND | RRC_TEXG) && ra->global[(rv >> 2) & 63] >= 0) tex = ra->global[(rv >> 2) & 63];
                else if ((rv & (RRC_BAND | RRC_GEN | RRC_TEXG)) == (RRC_BAND | RRC_GEN) && (int)(rv & 0xff) < ra->nroad) { tex = ra->road0 + (int)(rv & 0xff); cob = 1 + (int)(rv & 0xff); } }   /* (an extra surface: ground_art.h k_c3Road - COBBLE) */
        }
        if (n1 - n0 < 0.5 && f1 - f0 < 0.5) continue;   /* the lane is off the road at both ends */
        if (n1 < n0) n1 = n0;
        if (f1 < f0) f1 = f0;
        {   float a[3], b[3], cc[3], d[3], uv[8], e = (ra && tex >= 0 && tex < ra->ntex && ra->tex[tex].w > 0) ? 0.5f / ra->tex[tex].w : 0.0f;   /* u half a texel inside: the wrap must not bleed the line of the far edge into a lane that ends in the road */
            /* across the lane in up to 3 parts: the gutter's slope where the lane ends at a curb (gut > 0), the flat road between */
            double nx[4], fx[4], hy[4]; int np = 0, q;
            int gl = gut > 0 && n0 <= el0 + 0.01 && f0 <= el1 + 0.01, gr = gut > 0 && n1 >= er0 - 0.01 && f1 >= er1 - 0.01; double gwl = C3_GUTW, gwr = C3_GUTW, gdl = gut, gdr = gut;
            if (gut > 0) { double km, rl; int hh = s0 == &c->piece[k].halfB; C3_EdgeStyle(ra, k, hh, 0, &km, &rl, &gwl, &gdl); C3_EdgeStyle(ra, k, hh, 1, &km, &rl, &gwr, &gdr);   /* (each side's gutter style) */
                if (ra && ra->ext && k < ra->next) { if ((ra->ext[k].gutter[hh][0] & 3) == 2) gl = 0; if ((ra->ext[k].gutter[hh][1] & 3) == 2) gr = 0; } }   /* ("gutter: none": the lane flat to the curb) */
            if (gl * gwl + gr * gwr >= (n1 - n0 < f1 - f0 ? n1 - n0 : f1 - f0)) gl = gr = 0;   /* (a lane narrower than its gutter: flat) */
            nx[np] = n0; fx[np] = f0; hy[np++] = gl ? -gdl : 0;
            if (gl) { nx[np] = n0 + gwl; fx[np] = f0 + gwl; hy[np++] = 0; }
            if (gr) { nx[np] = n1 - gwr; fx[np] = f1 - gwr; hy[np++] = 0; }
            nx[np] = n1; fx[np] = f1; hy[np++] = gr ? -gdr : 0;
#define C3_LU(x) (float)((x) - x0 <= 0 ? e : ((x) - x0 >= w0 ? 1 - e : ((x) - x0) / w0))   /* u of lateral x in this lane */
            for (q = 0; q + 1 < np; q++) {
                C3_Point(c, k, nx[q] + o0, hy[q], a); C3_Point(c, k, nx[q + 1] + o0, hy[q + 1], b); C3_Point(c, k + 1, fx[q + 1] + o1, hy[q + 1], cc); C3_Point(c, k + 1, fx[q] + o1, hy[q], d);
                uv[0] = C3_LU(nx[q]); uv[1] = 1; uv[2] = C3_LU(nx[q + 1]); uv[3] = 1; uv[4] = C3_LU(fx[q + 1]); uv[5] = 0; uv[6] = C3_LU(fx[q]); uv[7] = 0;
                if (s0->kindFlags & 0x10) { uv[1] = uv[3] = 0; uv[5] = uv[7] = 1; }   /* rr_RoadsideStripDraw swaps the near and far edges of a 0x10 piece: the crossing
                                                                                       after a junction is the one before it turned round (zebra at the junction, the lane lines after it) */
                if (cob) { double rep = c->L / (w0 > 1 ? w0 : 1), b0 = -fmod((k + 1) * rep, 64.0); uv[5] = uv[7] = (float)b0; uv[1] = uv[3] = (float)(b0 + rep); }   /* (cobbles: square, on from piece to piece) */
                C3_QuadT(c, a, b, cc, d, C3_Colour(M_ROAD, 0, k), tex, uv); }
#undef C3_LU
            if (cob) C3_Mark(c, m0, cob == 2 ? 0xeb : 0xf0);   /* (cobbles: vertex alpha 240 - render3d.h, each stone bumped and glossy; the rainbow road: 235, animated) */
        }
    }
}

static double C3_Dist(const float *a, const float *b) { double x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrt(x * x + y * y + z * z); }

/* cross-section strips, skipping the spans drawn elsewhere (road; for SEC_OPEN with textures the curb / sidewalk spans); tex[span] >= 0 = textured
   (u across the span, v along the road, one repeat per segment), else flat colour */
#define C3_ALONG 4.0f
/* dirt ground (user, 2026-11-14: "render dirt terrain make it look more like dirt terrain and put rocks and stuff maybe in it"): a ground texture
   whose average colour is brown - red over green over blue, not too pale (sand) nor green (grass) - once per texture (RxTex.gkind) */
/* ground pictures whose HD picture was redone without the band of road painted in at its side (user, 2026-11-19: "remove the baked in street blend" -
   analysis\redwood\retex.py): dirt (C3_TexDirt), no band (C3_TexBaked) whatever the original's pixels say */
static int C3_TexRedone(const char *n) { return !strcmp(n, "SAND") || !strcmp(n, "DARK1L"); }
static int C3_TexDirt(const RxAssets *ra, int t)
{
    RxTex *x; unsigned *px; double r = 0, g = 0, b = 0; int i, n = 0;
    if (!ra || t < 0 || t >= ra->ntex) return 0;
    x = (RxTex *)&ra->tex[t];
    if (x->gkind) return x->gkind == 1;
    x->gkind = 2;
    if (C3_TexRedone(x->name)) { x->gkind = 1; return 1; }   /* (redone as plain dirt / sand: the shader's bumps - user, 2026-11-19 "give it a bump mapping finish") */
    if (x->gen || !(px = RX_Rgba(ra, x))) return 0;
    for (i = 0; i < x->w * x->h; i += 2) if (px[i] >> 24) { r += px[i] & 255; g += (px[i] >> 8) & 255; b += (px[i] >> 16) & 255; n++; }
    free(px);
    if (n) { r /= n; g /= n; b /= n; if (r > g * 1.03 && g > b * 1.08 && r - b > 22 && r + g + b < 3 * 175 && r + g + b > 3 * 45) x->gkind = 1; }
    return x->gkind == 1;
}
/* a rock lying on the dirt: a low dome of NS sides, jittered, half sunk (vertex alpha 238: render3d.h shades it by its facets), at piece position p,
   lateral x, ground height y; size r (lateral units), seed s */
static void C3_Rock(C3Course *c, double p, double x, double y, double r, unsigned s, unsigned rgba, int tex)   /* (tex: the rock texture, -1 = flat colour) */
{
    enum { NS = 7 }; float ring[NS][3], mid[NS][3], top[3]; double h = r * (0.45 + ((s >> 9) % 40) / 100.0), sq = 0.75 + ((s >> 3) % 50) / 100.0; int i, m0 = c->nv;
    for (i = 0; i < NS; i++) {
        double a = 6.2831853 * i / NS + ((s >> (i % 8)) & 7) * 0.05, j = 0.8 + (((s * (i + 3)) >> 11) % 40) / 100.0;
        C3_Point(c, p + sin(a) * r * j * sq / c->L, x + cos(a) * r * j, y - r * 0.08, ring[i]);
        C3_Point(c, p + sin(a) * r * j * sq * 0.62 / c->L, x + cos(a) * r * j * 0.62, y + h * (0.62 + (((s * (i + 7)) >> 13) % 20) / 100.0), mid[i]); }
    C3_Point(c, p, x + r * 0.08, y + h, top);
    for (i = 0; i < NS; i++) { int q = (i + 1) % NS;
        float u0 = (float)(i * 2.0 / NS + (s & 15) / 16.0), u1 = (float)((i + 1) * 2.0 / NS + (s & 15) / 16.0), w = (float)(r / 40.0), uv[8], ut[8];   /* (the texture round it: 2 turns, ~40 units a repeat) */
        uv[0] = u0 * w; uv[1] = w; uv[2] = u1 * w; uv[3] = w; uv[4] = u1 * w; uv[5] = w * 0.45f; uv[6] = u0 * w; uv[7] = w * 0.45f;
        ut[0] = u0 * w; ut[1] = w * 0.45f; ut[2] = u1 * w; ut[3] = w * 0.45f; ut[4] = (u0 + u1) * 0.5f * w; ut[5] = 0; ut[6] = ut[4]; ut[7] = 0;
        C3_QuadT(c, ring[i], ring[q], mid[q], mid[i], rgba, tex, tex >= 0 ? uv : NULL); C3_QuadT(c, mid[i], mid[q], top, top, rgba, tex, tex >= 0 ? ut : NULL); }
    C3_Mark(c, m0, 0xee);
}
/* the rocks of one dirt quad (piece k .. k + 1, across u0 .. u1 at heights h0 .. h1): pebbles to small rocks, now and then a boulder - the same
   wherever this piece is built (the live stretch too) */
static void C3_DirtRocks(C3Course *c, int k, double u0, double u1, double h0, double h1, double v0, double v1, double g0, double g1, int stones, int tex)   /* (row k: u, h; row k + 1: v, g; stones: stones only - a verge) */
{
    double w = fabs(u1 - u0); unsigned s = (unsigned)k * 2654435761u ^ (unsigned)(int)(u0 * 7.0) * 40503u; int i, n;
    if (c->noGround) return;
    if (w < 60) return;
    n = (int)(w / (stones ? 120.0 : 260.0) + ((s >> 12) % 100) / 100.0); if (n > 10) n = 10;
    for (i = 0; i < n; i++) {
        double f, pp, r, x; unsigned col; int g;
        s = s * 1664525u + 1013904223u; f = (s >> 8) / 16777216.0; s = s * 1664525u + 1013904223u; pp = k + (s >> 8) / 16777216.0;
        s = s * 1664525u + 1013904223u; r = stones ? 5 + (s >> 8) % 9 : (s >> 24) < 10 ? 70 + (s >> 8) % 60 : 9 + (s >> 8) % 26;   /* (1 in 25: a boulder) */
        { double t = pp - k, a0 = u0 + (v0 - u0) * t, a1 = u1 + (v1 - u1) * t, y0 = h0 + (g0 - h0) * t, y1 = h1 + (g1 - h1) * t; x = a0 + (a1 - a0) * f; if (fabs(x - a0) < r || fabs(x - a1) < r) continue; f = y0 + (y1 - y0) * f; }
        g = 175 + (int)((s >> 16) % 60); col = 0xff000000u | (unsigned)(g * 0.86) << 16 | (unsigned)(g * 0.93) << 8 | (unsigned)g;   /* (ABGR: grey with a warm cast) */
        C3_Rock(c, pp, x, f, r, s, col, tex);   /* (f: the ground height there) */
    }
}
/* the trees of an extra ground picture (ground_art.h: REDWOOD - user, 2026-11-18 "massive redwood trees ... really higher and spaced out on the tile")
   on one quad of it (piece k .. k + 1, across u0 .. u1 at heights h0 .. h1; row k + 1: v, g): about one a 1100 x 1100 square, 4200 .. 7000 high,
   kept off the quad's sides; each two crossed upright quads of the tree picture (its foot at the bottom middle), both faces, 40 sunk into the
   ground - the same wherever this piece is built */
static void C3_GroundTrees(C3Course *c, int k, double u0, double u1, double h0, double h1, double v0, double v1, double g0, double g1, int tex)
{
    double w = fabs(u1 - u0); unsigned s = (unsigned)k * 2246822519u ^ (unsigned)(int)(u0 * 3.0) * 3266489917u; int i, n;
    static const float uv[8] = { 0, 1, 1, 1, 1, 0, 0, 0 };
    if (w < 300) return;
    s = (s ^ (s >> 15)) * 2654435761u;
    n = (int)(w * c->L / (1100.0 * 1100.0) + ((s >> 8) % 1000) / 1000.0); if (n > 6) n = 6;
    for (i = 0; i < n; i++) {
        double f, pp, x, y, ht, hw, t, a0, a1, y0, y1; float a[3], b[3], cc[3], d[3]; int pl;
        s = s * 1664525u + 1013904223u; f = 0.12 + 0.76 * ((s >> 8) / 16777216.0);
        s = s * 1664525u + 1013904223u; pp = k + 0.1 + 0.8 * ((s >> 8) / 16777216.0);
        s = s * 1664525u + 1013904223u; ht = 4200 + (s >> 8) % 2800; hw = ht * 0.125;
        t = pp - k; a0 = u0 + (v0 - u0) * t; a1 = u1 + (v1 - u1) * t; y0 = h0 + (g0 - h0) * t; y1 = h1 + (g1 - h1) * t;
        x = a0 + (a1 - a0) * f; y = y0 + (y1 - y0) * f - 40;
        for (pl = 0; pl < 2; pl++) {   /* across the road, then along it */
            if (pl == 0) { C3_Point(c, pp, x - hw, y, a); C3_Point(c, pp, x + hw, y, b); C3_Point(c, pp, x + hw, y + ht, cc); C3_Point(c, pp, x - hw, y + ht, d); }
            else { double dp = hw / c->L; C3_Point(c, pp - dp, x, y, a); C3_Point(c, pp + dp, x, y, b); C3_Point(c, pp + dp, x, y + ht, cc); C3_Point(c, pp - dp, x, y + ht, d); }
            C3_QuadTint(c, a, b, cc, d, 0xffffffffu, tex, uv, 0xffffffffu); C3_QuadTint(c, b, a, d, cc, 0xffffffffu, tex, uv + 0, 0xffffffffu); } }
}
/* the hedge of an extra ground picture (ground_art.h: REDWOOD_HEDGE - user, 2026-11-18 "for redwood for the beyond piece ... a hedge of bushes ... like a wall
   of shrubbery ... it should be tall"): on a "beyond" cell (strip i: 0 left, C3_NP - 2 right) painted with it, an upright wall C3_HEDGEH high along its inner
   edge (P2), C3_HEDGEIN out into it; the picture across C3_HEDGEH * 2 a copy, running on from piece to piece; both faces */
#define C3_HEDGEH 1300.0
#define C3_HEDGEIN 80.0
static void C3_GroundHedge(C3Course *c, int k, int i, const C3Row *r0, const C3Row *r1, int tex)
{
    int in = i == 0 ? 1 : i, out = i == 0 ? 0 : i + 1, rr; float p[2][2][3], uv[8]; double rep = c->L / (2 * C3_HEDGEH);
    for (rr = 0; rr < 2; rr++) {
        const C3Row *R = rr ? r1 : r0; double w = fabs(R->u[out] - R->u[in]), f = w > 1 ? (C3_HEDGEIN < 0.4 * w ? C3_HEDGEIN : 0.4 * w) / w : 0,
            u = R->u[in] + (R->u[out] - R->u[in]) * f, h = R->h[in] + (R->h[out] - R->h[in]) * f - 20;
        C3_Point(c, k + rr, u, h, p[rr][0]); C3_Point(c, k + rr, u, h + C3_HEDGEH, p[rr][1]); }
    uv[0] = (float)fmod(k * rep, 64.0); uv[1] = 1; uv[2] = uv[0] + (float)rep; uv[3] = 1; uv[4] = uv[2]; uv[5] = 0; uv[6] = uv[0]; uv[7] = 0;
    C3_QuadTint(c, p[0][0], p[1][0], p[1][1], p[0][1], 0xffffffffu, tex, uv, 0xffffffffu);
    {   float ub[8]; ub[0] = uv[2]; ub[1] = 1; ub[2] = uv[0]; ub[3] = 1; ub[4] = uv[0]; ub[5] = 0; ub[6] = uv[2]; ub[7] = 0;   /* (the other face) */
        C3_QuadTint(c, p[1][0], p[0][0], p[0][1], p[1][1], 0xffffffffu, tex, ub, 0xffffffffu); }
}
/* a bridge's railing (user, 2026-11-19: "side railings like a bridge would have using the verge maybe to form side walls"): on a verge painted BRIDGE, along its outer
   edge (row point 2): a concrete parapet C3_PARH high, steel posts every quarter piece and a rail on top C3_RAILH high; both faces */
#define C3_PARH 45.0
#define C3_RAILH 110.0
static void C3_Railing(C3Course *c, int k, const C3Row *r0, const C3Row *r1, int side)
{
    int j = side ? 10 : 4, q; double sg = side ? 1 : -1; float a[3], b[3], cc[3], d[3];
    const unsigned con = 0xff9aa0a4u, stl = 0xff5a5e64u;   /* (ABGR: concrete grey, steel) */
#define C3_RQ(u0, u1, ha, hb, col) { C3_Point(c, k, u0, r0->h[j] + (ha), a); C3_Point(c, k + 1, u1, r1->h[j] + (ha), b); C3_Point(c, k + 1, u1, r1->h[j] + (hb), cc); C3_Point(c, k, u0, r0->h[j] + (hb), d); \
        C3_Quad(c, a, b, cc, d, col); C3_Quad(c, d, cc, b, a, col); }
    C3_RQ(r0->u[j] - sg * 12, r1->u[j] - sg * 12, 0, C3_PARH, con);   /* the parapet's inner face, */
    C3_RQ(r0->u[j], r1->u[j], 0, C3_PARH, con);                       /* its outer face, */
    {   C3_Point(c, k, r0->u[j] - sg * 12, r0->h[j] + C3_PARH, a); C3_Point(c, k + 1, r1->u[j] - sg * 12, r1->h[j] + C3_PARH, b); C3_Point(c, k + 1, r1->u[j], r1->h[j] + C3_PARH, cc); C3_Point(c, k, r0->u[j], r0->h[j] + C3_PARH, d);
        C3_Quad(c, a, b, cc, d, con); C3_Quad(c, d, cc, b, a, con); }   /* its top */
    C3_RQ(r0->u[j] - sg * 6, r1->u[j] - sg * 6, C3_RAILH - 14, C3_RAILH, stl);   /* the rail */
    for (q = 0; q < 4; q++) { double t = (q + 0.5) / 4, u = r0->u[j] + (r1->u[j] - r0->u[j]) * t - sg * 6, hh = r0->h[j] + (r1->h[j] - r0->h[j]) * t, dp = 4.0 / c->L;
        C3_Point(c, k + t - dp, u, hh + C3_PARH, a); C3_Point(c, k + t + dp, u, hh + C3_PARH, b); C3_Point(c, k + t + dp, u, hh + C3_RAILH, cc); C3_Point(c, k + t - dp, u, hh + C3_RAILH, d);
        C3_Quad(c, a, b, cc, d, stl); C3_Quad(c, d, cc, b, a, stl); }   /* the posts */
#undef C3_RQ
}
/* grounds meeting each other (user, 2026-11-18: "any terrain that touches up against another terrain should dynamically just mesh in what that terrain"):
   where two ground strips with different pictures meet - side by side across the road, or one piece after another along it - the inner / earlier one's
   picture runs on over the other's edge, torn raggedly (RX_EdgeCut: as DARK4's tarmac is painted into its dirt), on a quad C3_BLIFT over the ground.
   The tear is the picture's own texels past its edge (it repeats on), so it joins without a seam; across C3_BDEEP deep (a quarter of a wide copy, or
   up to 4 copies of a small one) but less than half the strip it lies on; along one copy (C3_ALONG a piece).  Test switch RR_W3NOBLEND=1: none */
#define C3_BDEEP 160.0   /* (softer: user, 2026-11-19 "the blending looks abit aggressive ... not protrude so much" - was 300) */
#define C3_BLIFT 3.0
/* a torn edge's point at u, h on a face running du, dh across: lifted out of that face, not straight up (user, 2026-11-19: a hole's wall "wobbling ... while i move
   the camera" - lifted up, the edge lay in the steep wall's own plane and fought it for the depth); steeper: further, 4x on a wall */
static void C3_BPoint(C3Course *c, double k, double u, double h, double du, double dh, float *out)
{   double l = sqrt(du * du + dh * dh), nu = 0, nh = 1, st;
    if (l > 1e-6) { nu = -dh / l; nh = du / l; if (nh < 0) { nu = -nu; nh = -nh; } }
    st = C3_BLIFT * (1 + 3 * (1 - nh));
    C3_Point(c, k, u + nu * st, h + nh * st, out); }
#define C3_WBDEEP 130.0   /* (land onto water across: at most this far; at the water's ends 3/4 of a picture along - a little more: user, 2026-11-19 "abit more dramatic on the sides but not too much") */
static void C3_ProfileTex(RxAssets *ra, const RoadSection *s, int k, int h, int *tex);
static int C3_WalkTex(RxAssets *ra, const RoadSection *s, int k, int h, int side, int row);
static void C3_CellOverride(RxAssets *ra, const RoadSection *s, int k, int h, int *tex);
static void C3_PieceTex(RxAssets *ra, const RoadSection *s0, int k, int h, int *tex)   /* piece k's strip pictures (C3_NP - 1): the profile's, its footpath, the editor's cells */
{
    int sd;
    C3_ProfileTex(ra, s0, k, h, tex);
    for (sd = 0; sd < 2; sd++) if (C3_SideWalk(ra, s0, k, h, sd)) {   /* a footpath (C3_SideWalk): the kerb face, road edge -> outer edge */
        int w = C3_WalkTex(ra, s0, k, h, sd, 0), cb = C3_WalkTex(ra, s0, k, h, sd, 1); if (cb < 0) cb = w;
        if (sd) { tex[8] = cb; tex[9] = w; } else { tex[5] = cb; tex[4] = w; } }
    C3_CellOverride(ra, s0, k, h, tex);   /* (the editor's cells painted) */
}
static int C3_IsGround(int m) { return m == M_FAR || m == M_TERRAIN || m == M_SLOPE || m == M_VERGE; }
static int C3_EdgeTex(const RxAssets *cra, int t, int dir)   /* (dir: RxTex.edgeDir) texture t's ragged edge (made once: its pixels when drawn - RX_EdgeCut); -1 none */
{
    RxAssets *ra = (RxAssets *)cra; RxTex *x, *b;
    if (!ra || t < 0 || t >= ra->ntex) return -1;
    b = &ra->tex[t];
    if (b->edgeOf || b->key || b->holes) return -1;   /* (not of an edge, nor of a cut-out) */
    if (b->edge[dir]) return b->edge[dir] - 1;
    if (ra->ntex >= 8192) return -1;
    x = &ra->tex[ra->ntex]; memset(x, 0, sizeof *x);
    _snprintf(x->name, sizeof x->name, "%c%.7s", "~|2345"[dir], b->name); x->name[8] = 0;
    x->w = dir == 1 ? b->w : dir >= 2 ? b->w * (dir - 1) : (b->w / 4 > 1 ? b->w / 4 : 1); x->h = dir == 1 ? b->h : 4 * b->h;   /* (RX_EdgeCut's size) */ x->edgeOf = t + 1; x->edgeDir = dir; x->key = 1; x->gpu = -1;
    b->edge[dir] = ra->ntex + 1;
    if (getenv("RR_W3LOGBLEND")) LOG("c3 blend: edge %d of %s (%dx%d) dir %d", ra->ntex, b->name, b->w, b->h, dir);   /* (test switch) */
    return ra->ntex++;
}
static int C3_TexBaked(const RxAssets *ra, int t)   /* ground texture t has a band painted in at its road side (DARK4: tarmac then dirt) - its first fifth's
                                                    colour far from the rest's; once per texture */
{
    RxTex *x; unsigned *px; double a[3] = { 0, 0, 0 }, b[3] = { 0, 0, 0 }, na = 0, nb = 0, d; int X, Y, j;
    if (!ra || t < 0 || t >= ra->ntex) return 0;
    x = (RxTex *)&ra->tex[t];
    if (x->baked) return x->baked == 1;
    x->baked = 2;
    if (C3_TexRedone(x->name)) return 0;
    if (x->w < 10 || !(px = RX_Rgba(ra, x))) return 0;
    for (Y = 0; Y < x->h; Y++) for (X = 0; X < x->w; X++) { unsigned c = px[Y * x->w + X]; double *s = X < x->w / 5 ? a : X >= x->w / 2 ? b : NULL;
        if (!s || !(c >> 24)) continue; for (j = 0; j < 3; j++) s[j] += (c >> (8 * j)) & 255; if (s == a) na++; else nb++; }
    free(px);
    if (na && nb) { d = 0; for (j = 0; j < 3; j++) d += fabs(a[j] / na - b[j] / nb); if (d > 70) x->baked = 1; }
    return x->baked == 1;
}
static double C3_TexPer(const C3Course *c, const RxAssets *ra, int t)   /* units across one copy of ground texture t (as C3_Strip lays it) */
{
    return ra->tex[t].w * (c->L / C3_ALONG / ra->tex[t].h);
}
#define C3_RDEEP C3_GUTW   /* (the verge over the road's edge: no further than a gutter is wide - user, 2026-11-18 "it shouldnt cross the gutter width") */
static int C3_BlendNot(int i, int sf)   /* strip i never blends: the road edge's kerb face (5 / 8), a footpath (4 / 9 - user, 2026-11-18: the footpath's grey was torn into the grass) */
{
    return i == 5 || i == 8 || (i == 4 && (sf & 1)) || (i == 9 && (sf & 2));
}
/* sf: & 1 / 2 the left / right side has a footpath, & 4 / 8 its verge (or footpath) may run onto the road (a terrain side, no gutter, no kerb painted - user, 2026-11-18: "the blending
   should only happen if the gutters are off and the face matches the road face": the verge's road-side edge at the road edge's height, checked here) */
static void C3_Blend(C3Course *c, int k, const C3Row *r0, const C3Row *r1, const int *tex, const int *prev, const RxAssets *ra, int sf)
{
    static int off = -1, only = -1; int i, sd;   /* (test switch RR_W3BLENDONLY=1 across / 2 along) */
    if (off < 0) { off = getenv("RR_W3NOBLEND") != NULL; only = getenv("RR_W3BLENDONLY") ? atoi(getenv("RR_W3BLENDONLY")) : 0; }
    if (off || !ra) return;
    /* the verge onto the road: its picture torn over the road's edge (as across below, mirrored: the verge the inner strip, the road the outer) */
    for (sd = 0; sd < 2 && only != 2; sd++) {
        int vs = sd ? 9 : 4, ep = sd ? 8 : 6, tp = sd ? 9 : 5, ip = sd ? 10 : 4, e = -1, rr, ok = 1, dir = 0;   /* (the verge strip; the road edge, the edge's top, the verge's far point) */
        const C3Row *R[2]; float P[2][2][3], s[2]; R[0] = r0; R[1] = r1;
        if (!(sf & (4 << sd))) continue;
        if (vs == c->darkStrip || tex[vs] < 0 || !C3_IsGround(r0->mat[vs]) || C3_TexBaked(ra, tex[vs]) || C3_IsPitTex(ra, tex[vs])) continue;
        for (rr = 0; rr < 2 && ok; rr++) {
            double wIn = fabs(R[rr]->u[ip] - R[rr]->u[tp]), wRd = fabs(R[rr]->u[7] - R[rr]->u[ep]), per = C3_TexPer(c, ra, tex[vs]), copy = wIn / per <= 1.5 ? wIn : per, w = C3_RDEEP, in, fi, span;
            if (fabs(R[rr]->h[tp] - R[rr]->h[ep]) > 1 || fabs(R[rr]->u[tp] - R[rr]->u[ep]) > 1) { ok = 0; break; }   /* (a step or a kerb between them: a hard edge) */
            if (w > 0.2 * wRd) w = 0.2 * wRd;
            if (wIn < 1 || copy < 1) { ok = 0; break; }
            in = copy / 10.0; fi = in / wIn; if (fi > 0.3) fi = 0.3, in = 0.3 * wIn;
            if (rr == 0) { dir = w + in <= 0.25 * copy ? 0 : 2; if ((e = C3_EdgeTex(ra, tex[vs], dir)) < 0) { ok = 0; break; } }   /* (a quarter of a copy, or one whole copy) */
            span = dir ? copy : 0.25 * copy;
            if (w + in > 0.98 * span) w = 0.98 * span - in;   /* (never past its edge picture's end: it would repeat - a second, opaque band out on the road) */
            if (w < 8) { ok = 0; break; }
            s[rr] = (float)((w + in) / span);
            C3_Point(c, k + rr, R[rr]->u[tp] + (R[rr]->u[ip] - R[rr]->u[tp]) * fi, R[rr]->h[tp] + (R[rr]->h[ip] - R[rr]->h[tp]) * fi + C3_BLIFT, P[rr][0]);
            C3_Point(c, k + rr, R[rr]->u[ep] + (R[rr]->u[7] > R[rr]->u[ep] ? w : -w), R[rr]->h[ep] + C3_BLIFT, P[rr][1]); }
        if (ok) { float uv[8];
            if (sd) { uv[0] = s[0]; uv[1] = C3_ALONG / 4; uv[2] = 0.01f; uv[3] = C3_ALONG / 4; uv[4] = 0.01f; uv[5] = 0; uv[6] = s[1]; uv[7] = 0;   /* (the tear towards the left: as a left side's across) */
                C3_QuadTint(c, P[0][1], P[0][0], P[1][0], P[1][1], 0xffffffffu, e, uv, 0xffffffffu); }
            else { uv[0] = 0.01f; uv[1] = C3_ALONG / 4; uv[2] = s[0]; uv[3] = C3_ALONG / 4; uv[4] = s[1]; uv[5] = 0; uv[6] = 0.01f; uv[7] = 0;
                C3_QuadTint(c, P[0][0], P[0][1], P[1][1], P[1][0], 0xffffffffu, e, uv, 0xffffffffu); } } }
    for (i = 0; i < C3_NP - 1; i++) {
        int left = i < C3_NP / 2, kind = c->skOn ? c->skind[left ? 0 : 1] : SEC_PROFILE;
        if (kind >= SEC_WALL1 && kind <= SEC_WALL_SPLIT) continue;
        if (i == c->darkStrip || tex[i] < 0 || !C3_IsGround(r0->mat[i]) || C3_BlendNot(i, sf) || C3_IsBridgeTex(ra, tex[i])) continue;   /* (a bridge deck: its edge sharp) */
        /* across: strip i the inner one, the next one out the outer (left: i - 1, right: i + 1) - the inner picture over the outer strip's edge; painted water (user,
           2026-11-19: "the neighbouring texture should just blend right into the water"): never torn itself - the land's picture over the water's edge, whichever
           side of it the water is (mir: the outer strip's picture inwards over water inside it) */
        {   int o = left ? i - 1 : i + 1, bp = left ? i : i + 1, fpN = left ? i - 1 : i + 2, ipN = left ? i + 1 : i, mir;
            if (only != 2 && o >= 0 && o < C3_NP - 1 && (o < C3_NP / 2) == left && o != c->darkStrip && tex[o] >= 0 && tex[o] != tex[i] && C3_IsGround(r0->mat[o]) && !C3_BlendNot(o, sf))
            for (mir = 0; mir < 2; mir++) {
                int src = mir ? o : i, ip = mir ? fpN : ipN, fp = mir ? ipN : fpN, lw = mir ? !left : left, t = tex[src], e = -1;
                const C3Row *R[2]; float P[2][2][3], Pm[2][3], s[2], sm[2]; int rr, ok = 1, dir = 0; R[0] = r0; R[1] = r1;
                if (mir ? !(C3_IsPitTex(ra, tex[i]) && !C3_IsPitTex(ra, tex[o])) : C3_IsPitTex(ra, tex[i])) continue;
                if (C3_EdgeTex(ra, t, 0) < 0) continue;
                for (rr = 0; rr < 2; rr++) {
                    double wIn = fabs(R[rr]->u[bp] - R[rr]->u[ip]), wOut = fabs(R[rr]->u[fp] - R[rr]->u[bp]), per = C3_TexPer(c, ra, t), copy = wIn / per <= 1.5 ? wIn : per, w = C3_BDEEP, f;
                    if (w > 0.3 * wOut) w = 0.3 * wOut;
                    if (C3_IsPitTex(ra, tex[i]) || C3_IsPitTex(ra, tex[o])) { if (w > C3_WBDEEP) w = C3_WBDEEP; if (w > 0.22 * wOut) w = 0.22 * wOut; }   /* (onto water, along the road: gentler - user, 2026-11-19 "too dramatic make it match the other side") */
                    if (C3_TexBaked(ra, t) && w > 0.6 * copy) w = 0.6 * copy;   /* (DARK4 ...: never as far as its road-side band - the reflection would bring it back) */
                    if (w < 8 || wOut < 1 || copy < 1) { ok = 0; break; }
                    if (rr == 0) { int n = (int)ceil(w / copy - 1e-6); dir = w <= 0.25 * copy ? 0 : 1 + (n < 1 ? 1 : n > 4 ? 4 : n); if (dir >= 2 && w > copy * (dir - 1)) w = copy * (dir - 1);
                        if ((e = C3_EdgeTex(ra, t, dir)) < 0) { ok = 0; break; } }
                    else if (w > (dir ? copy * (dir - 1) : 0.25 * copy)) w = dir ? copy * (dir - 1) : 0.25 * copy;
                    {   double in = copy / 10.0, fi = wIn > 1 ? in / wIn : 0;   /* (RX_EDGEIN: the reflection's start, inside the inner strip) */
                        double span = dir ? copy * (dir - 1) : 0.25 * copy;
                        if (fi > 0.3) fi = 0.3, in = 0.3 * wIn;
                        if (w + in > 0.98 * span) { w = 0.98 * span - in; if (w < 4) w = 4; }   /* (never past the edge picture's end: it would repeat, opaque again) */
                        f = w / wOut; s[rr] = (float)((w + in) / span); sm[rr] = 0.01f + (s[rr] - 0.01f) * (float)(in / (w + in));
                        C3_BPoint(c, k + rr, R[rr]->u[bp] + (R[rr]->u[ip] - R[rr]->u[bp]) * fi, R[rr]->h[bp] + (R[rr]->h[ip] - R[rr]->h[bp]) * fi, R[rr]->u[ip] - R[rr]->u[bp], R[rr]->h[ip] - R[rr]->h[bp], P[rr][0]); }
                    C3_BPoint(c, k + rr, R[rr]->u[bp] + (R[rr]->u[fp] - R[rr]->u[bp]) * f, R[rr]->h[bp] + (R[rr]->h[fp] - R[rr]->h[bp]) * f, R[rr]->u[fp] - R[rr]->u[bp], R[rr]->h[fp] - R[rr]->h[bp], P[rr][1]);
                    {   float q0[3], q1[3]; int j;   /* (the strips' meeting line: lifted off both faces - the two halves bend with the ground there; one flat quad across a bank going
                                                        down to the water cut under its crease and the water showed through as a dark line: user, 2026-11-19 "it has a line in the middle") */
                        C3_BPoint(c, k + rr, R[rr]->u[bp], R[rr]->h[bp], R[rr]->u[ip] - R[rr]->u[bp], R[rr]->h[ip] - R[rr]->h[bp], q0);
                        C3_BPoint(c, k + rr, R[rr]->u[bp], R[rr]->h[bp], R[rr]->u[fp] - R[rr]->u[bp], R[rr]->h[fp] - R[rr]->h[bp], q1);
                        for (j = 0; j < 3; j++) Pm[rr][j] = (q0[j] + q1[j]) / 2; Pm[rr][1] += (float)C3_BLIFT; } }
                if (ok) { float uv[8];
                    if (lw) { uv[0] = sm[0]; uv[1] = C3_ALONG / 4; uv[2] = 0.01f; uv[3] = C3_ALONG / 4; uv[4] = 0.01f; uv[5] = 0; uv[6] = sm[1]; uv[7] = 0;   /* (its 4 copies down: one piece) */
                        C3_QuadTint(c, Pm[0], P[0][0], P[1][0], Pm[1], 0xffffffffu, e, uv, 0xffffffffu);
                        uv[0] = s[0]; uv[2] = sm[0]; uv[4] = sm[1]; uv[6] = s[1]; C3_QuadTint(c, P[0][1], Pm[0], Pm[1], P[1][1], 0xffffffffu, e, uv, 0xffffffffu); }
                    else { uv[0] = 0.01f; uv[1] = C3_ALONG / 4; uv[2] = sm[0]; uv[3] = C3_ALONG / 4; uv[4] = sm[1]; uv[5] = 0; uv[6] = 0.01f; uv[7] = 0;
                        C3_QuadTint(c, P[0][0], Pm[0], Pm[1], P[1][0], 0xffffffffu, e, uv, 0xffffffffu);
                        uv[0] = sm[0]; uv[2] = s[0]; uv[4] = s[1]; uv[6] = sm[1]; C3_QuadTint(c, Pm[0], P[0][1], P[1][1], Pm[1], 0xffffffffu, e, uv, 0xffffffffu); } } } }
        /* along: the piece before had another picture in this strip - its picture on over this piece's start, one copy long */
        if (only != 1 && prev && prev[i] >= 0 && prev[i] != tex[i] && !C3_BlendNot(i, sf >> 4) && !C3_IsPitTex(ra, prev[i]) && !C3_TexBaked(ra, prev[i]) && !C3_TexBaked(ra, tex[i])) { int e = C3_EdgeTex(ra, prev[i], 1);   /* (DARK4 ...: its band would run on) */
            if (e >= 0) { double g = (C3_IsPitTex(ra, tex[i]) ? 0.75 : 0.5) / C3_ALONG, per = C3_TexPer(c, ra, prev[i]), wa = fabs(r0->u[i + 1] - r0->u[i]), q = wa / per <= 1.5 ? 1 : wa / per;
                float a[3], b[3], cc[3], d[3], uv[8];
                C3_Point(c, k, r0->u[i], r0->h[i] + C3_BLIFT, a); C3_Point(c, k, r0->u[i + 1], r0->h[i + 1] + C3_BLIFT, b);
                C3_Point(c, k + g, r0->u[i + 1] + (r1->u[i + 1] - r0->u[i + 1]) * g, r0->h[i + 1] + (r1->h[i + 1] - r0->h[i + 1]) * g + C3_BLIFT, cc);
                C3_Point(c, k + g, r0->u[i] + (r1->u[i] - r0->u[i]) * g, r0->h[i] + (r1->h[i] - r0->h[i]) * g + C3_BLIFT, d);
                if (left) { uv[0] = (float)q; uv[2] = 0; uv[4] = 0; uv[6] = (float)q; } else { uv[0] = 0; uv[2] = (float)q; uv[4] = (float)q; uv[6] = 0; }
                uv[1] = uv[3] = 0.99f; uv[5] = uv[7] = 0;
                C3_QuadTint(c, a, b, cc, d, 0xffffffffu, e, uv, 0xffffffffu); } }
        /* along, onto water ending (user, 2026-11-19: "it didnt work here" - water in the piece before, land in this one): this piece's picture back over the water's
           end, one copy long (the piece before's rows: this one's start, a straight run back) */
        if (only != 1 && prev && prev[i] >= 0 && C3_IsPitTex(ra, prev[i]) && !C3_IsPitTex(ra, tex[i]) && !C3_TexBaked(ra, tex[i])) { int e = C3_EdgeTex(ra, tex[i], 1);
            if (e >= 0) { double g = 0.75 / C3_ALONG, per = C3_TexPer(c, ra, tex[i]), wa = fabs(r0->u[i + 1] - r0->u[i]), q = wa / per <= 1.5 ? 1 : wa / per;
                float a[3], b[3], cc[3], d[3], uv[8];
                C3_Point(c, k, r0->u[i + 1], r0->h[i + 1] + C3_BLIFT, a); C3_Point(c, k, r0->u[i], r0->h[i] + C3_BLIFT, b);
                C3_Point(c, k - g, r0->u[i], r0->h[i] + C3_BLIFT, cc); C3_Point(c, k - g, r0->u[i + 1], r0->h[i + 1] + C3_BLIFT, d);
                if (left) { uv[0] = 0; uv[2] = (float)q; uv[4] = (float)q; uv[6] = 0; } else { uv[0] = (float)q; uv[2] = 0; uv[4] = 0; uv[6] = (float)q; }
                uv[1] = uv[3] = 0.99f; uv[5] = uv[7] = 0;
                C3_QuadTint(c, a, b, cc, d, 0xffffffffu, e, uv, 0xffffffffu); } }
    }
}
/* dunes (user, 2026-11-19: "sand dunes in T3D but just do flat textures in P3D"): a cell painted DUNES drawn as a grid of 6 x 6 quads, rolled into dunes - long ridges
   across the wind, the same function of the place on every piece (they run on from piece to piece), none at the cell's sides (it joins its neighbours) nor at its ends where
   the piece before / after has no dunes there; sand (vertex alpha 239: the dirt bumps) */
#define C3_DUNEH 650.0   /* (larger: user, 2026-11-19) */
static double C3_DuneAt(double p, double u) { double d = 0.5 + 0.5 * sin(u / 950.0 + p * 0.3 + 1.7 * sin(p * 0.07 + u / 3800.0)); return C3_DUNEH * d * d; }
static void C3_Dunes(C3Course *c, int k, int i, const C3Row *r0, const C3Row *r1, int tex, const float *uv, const RxAssets *ra)
{
    enum { NA = 6, NC = 6 }; int left = i < C3_NP / 2, cell = left ? 4 - i : i - 9, side = !left, ea, ec, m0 = c->nv, pv = 0, nx = 0;
    if (cell >= 0 && cell <= 4) {
        if (k > 0) pv = (C3_SideCells(ra, &c->piece[k - 1].halfA, k - 1, 0, side, C3_GDUNES) >> cell) & 1;
        if (k + 1 < c->n) nx = (C3_SideCells(ra, &c->piece[k + 1].halfA, k + 1, 0, side, C3_GDUNES) >> cell) & 1; }
    for (ea = 0; ea < NA; ea++) for (ec = 0; ec < NC; ec++) { float P[4][3], q[8]; int v;
        for (v = 0; v < 4; v++) { double ta = (double)(ea + (v >= 2)) / NA, tc = (double)(ec + (v == 1 || v == 2)) / NC,
                u = (r0->u[i] + (r0->u[i + 1] - r0->u[i]) * tc) * (1 - ta) + (r1->u[i] + (r1->u[i + 1] - r1->u[i]) * tc) * ta,
                hh = (r0->h[i] + (r0->h[i + 1] - r0->h[i]) * tc) * (1 - ta) + (r1->h[i] + (r1->h[i + 1] - r1->h[i]) * tc) * ta,
                fa = (pv || ta >= 0.5 ? 1 : sin(3.14159265 * ta)) * (nx || ta <= 0.5 ? 1 : sin(3.14159265 * ta)), fc = sin(3.14159265 * tc);
            C3_Point(c, k + ta, u, hh + C3_DuneAt(k + ta, u) * fa * fc, P[v]);
            {   double ua = uv[0] + (uv[2] - uv[0]) * tc, va = uv[1] + (uv[3] - uv[1]) * tc, ub = uv[6] + (uv[4] - uv[6]) * tc, vb = uv[7] + (uv[5] - uv[7]) * tc;   /* (the cell's uv, across then along) */
                q[v * 2] = (float)(ua + (ub - ua) * ta); q[v * 2 + 1] = (float)(va + (vb - va) * ta); } }
        C3_QuadTint(c, P[0], P[1], P[2], P[3], 0xffffffffu, tex, q, 0xffffffffu); }
    C3_Mark(c, m0, 0xea);   /* (sand: vertex alpha 234 - render3d.h, bumped; not 239: that grows dry scrub, world3d.inc W3GrassCollect) */
}
static void C3_Strip(C3Course *c, int k, const C3Row *r0, const C3Row *r1, int kind, int skipWalk, const int *tex, unsigned ground, const RxAssets *ra)
{
    int i, kind0 = kind, tl[C3_NP - 1], rk[C3_NP - 1] = { 0 };
    memcpy(tl, tex, sizeof tl);   /* a hole's sides (user, 2026-11-19: "this texture here should be inherited from the neighbour"): a HOLE strip not lying wholly at the
                                     bottom - its end climbing out, the ground beside it - its neighbour's picture across (outwards, else inwards); only the floor black */
    for (i = 0; i < C3_NP - 1; i++) if (!g_c3Editor && ra && ra->nground > C3_GHOLE && tex[i] == ra->ground0 + C3_GHOLE
            && (r0->h[i] > C3_HOLEH + 1 || r0->h[i + 1] > C3_HOLEH + 1 || r1->h[i] > C3_HOLEH + 1 || r1->h[i + 1] > C3_HOLEH + 1)) {
        int o = i < C3_NP / 2 ? i - 1 : i + 1;
        if (o >= 0 && o < C3_NP - 1 && tex[o] >= 0 && !C3_IsPitTex(ra, tex[o])) tl[i] = tex[o]; else { int t = C3_NotPitTex(ra, tex, i); if (t >= 0) tl[i] = t; }   /* (between holes: the nearest ground) */
        if (tl[i] >= 0 && !C3_IsBridgeTex(ra, tl[i]) && !C3_TexBaked(ra, tl[i]) && ra->nground > C3_GCLIFF) { tl[i] = ra->ground0 + C3_GCLIFF; rk[i] = 1; } }   /* (user, 2026-11-19: rock - "replace this
                                                        texture is a suitable cliff texture ... only replace the texture where the texture can be blended": a bridge's face keeps its deck) */
    tex = tl;
    for (i = 0; i < C3_NP - 1; i++) {
        float a[3], b[3], cc[3], d[3];
        kind = c->skOn ? c->skind[i < C3_NP / 2 ? 0 : 1] : kind0;   /* (each side its own kind) */
        if (r0->mat[i] == M_ROAD) continue;
        if (skipWalk && (r0->mat[i] == M_CURB || r0->mat[i] == M_WALK)) continue;
        if (tex && tex[i] == -2) continue;   /* (nothing there) */
        C3_Point(c, k, r0->u[i], r0->h[i], a); C3_Point(c, k, r0->u[i + 1], r0->h[i + 1], b);
        C3_Point(c, k + 1, r1->u[i + 1], r1->h[i + 1], cc); C3_Point(c, k + 1, r1->u[i], r1->h[i], d);
        if (!g_c3Editor && tex && ra && ra->nground > C3_GHOLE && tex[i] == ra->ground0 + C3_GHOLE) { C3_Quad(c, a, b, cc, d, 0xff050505u); continue; }   /* (a hole's floor in the game: black - its sides took
                                                                                                                         their neighbour's picture above; any branch: the slopes' too) */
        if (tex && tex[i] >= 0) {
            /* u runs from the road outwards on both sides (the game mirrors the left side's strips: SANDL's white line lies on the road edge) */
            /* along the road C3_ALONG copies per segment (the game's terrain grain: one copy per segment looked stretched beside it); across
               one copy, or on a wide span (the hillside above a cliff, a far verge) as many as square texels give */
            int wall = kind >= SEC_WALL1 && kind <= SEC_WALL_SPLIT;
            double q0 = 1, q1 = 1; float uv[8], V = wall ? 1.0f : C3_ALONG;   /* walls: the face keeps one copy per segment (its strata) */
            if (ra && tex[i] < ra->ntex && ra->tex[tex[i]].w > 0 && ra->tex[tex[i]].h > 0) {
                double per = ra->tex[tex[i]].w * (c->L / V / ra->tex[tex[i]].h);
                if (wall && r0->h[i] > 0 && r0->h[i] == r0->h[i + 1]) per = C3_TOP;   /* a cliff's top: tiles of 1000 units across, one per segment along (rr_NearWallDraw) */
                q0 = C3_Dist(a, b) / per; q1 = C3_Dist(d, cc) / per;
                if (q0 <= 1.5 && q1 <= 1.5) q0 = q1 = 1; }
            if (rk[i]) { q0 = C3_Dist(a, b) / C3_ROCKPER; q1 = C3_Dist(d, cc) / C3_ROCKPER; V = (float)(C3_Dist(a, d) / C3_ROCKPER); }   /* (a hole's side: rock as big as on its ends - user, 2026-11-19) */
            if (i < C3_NP / 2) { uv[0] = (float)q0; uv[1] = V; uv[2] = 0; uv[3] = V; uv[4] = 0; uv[5] = 0; uv[6] = (float)q1; uv[7] = 0; }
            else { uv[0] = 0; uv[1] = V; uv[2] = (float)q0; uv[3] = V; uv[4] = (float)q1; uv[5] = 0; uv[6] = 0; uv[7] = 0; }
            if (wall && (r0->h[i] == 0) != (r0->h[i + 1] == 0) && r0->h[i] >= 0 && r0->h[i + 1] >= 0 && (r0->h[i] > 300 || r0->h[i + 1] > 300)) {
                /* a cliff face: bands up the face, ledges and recesses pushed back into the rock (never towards the road), the same per piece edge on both
                   sides of it (a function of the piece and the height) - the fixed sun (C3_Shade) shades each facet; vertex alpha 252: rock (render3d.h) */
                int j, NS = 7; float p0[2][3], p1[2][3], u2[8];
                for (j = 0; j < NS; j++) {
                    int e;
                    for (e = 0; e < 2; e++) {   /* band edge j + e on row 0 (piece k) and row 1 (piece k + 1) */
                        int rr; for (rr = 0; rr < 2; rr++) {
                            const C3Row *R = rr ? r1 : r0; int kk = k + rr, ft = R->h[i] == 0 ? i : i + 1, tp = ft == i ? i + 1 : i;
                            double f = (double)(j + e) / NS, H = R->h[tp] * f, sgn = R->u[ft] >= 0 ? 1.0 : -1.0, n, dsp;
                            n = 0.5 * sin(kk * 0.83 + f * 5.3 + sgn) + 0.3 * sin(kk * 2.11 + f * 13.7 + 0.7) + 0.2 * sin(kk * 0.29 + f * 2.3 + 2.1);
                            dsp = sgn * sin(3.14159265 * f) * (0.12 + 0.28 * (0.5 + 0.5 * n)) * R->h[tp];   /* (outwards, 12 .. 40 % of the face's height) */
                            C3_Point(c, kk, R->u[ft] + (R->u[tp] - R->u[ft]) * f + dsp, H, rr ? p1[e] : p0[e]); } }
                    {   /* texture across: from the foot's end to the top's end of this strip's uv */
                        int footA = r0->h[i] == 0; float fa = (float)j / NS, fb = (float)(j + 1) / NS, ua0, ub0, ua1, ub1;
                        float uFootRow0 = footA ? uv[0] : uv[2], uTopRow0 = footA ? uv[2] : uv[0], uFootRow1 = footA ? uv[6] : uv[4], uTopRow1 = footA ? uv[4] : uv[6];
                        ua0 = uFootRow0 + (uTopRow0 - uFootRow0) * fa; ub0 = uFootRow0 + (uTopRow0 - uFootRow0) * fb;
                        ua1 = uFootRow1 + (uTopRow1 - uFootRow1) * fa; ub1 = uFootRow1 + (uTopRow1 - uFootRow1) * fb;
                        u2[0] = ua0; u2[1] = V; u2[2] = ub0; u2[3] = V; u2[4] = ub1; u2[5] = 0; u2[6] = ua1; u2[7] = 0; }
                    C3_QuadTint(c, p0[0], p0[1], p1[1], p1[0], 0xffffffffu, tex[i], u2, i == c->darkStrip ? 0xfc8a8a8au : 0xfcffffffu); }
                continue; }
            {   int m0 = c->nv, m = r0->mat[i], dirt = (m == M_TERRAIN || m == M_FAR || m == M_SLOPE || m == M_VERGE) && kind != SEC_OPEN && C3_TexDirt(ra, tex[i]);
                if (C3_IsWaterTex(ra, tex[i])) { C3_QuadTint(c, a, b, cc, d, 0xfd8a5a2au, tex[i], uv, 0xfdffffffu); continue; }
                if (!g_c3Editor && ra && ra->nground > C3_GHOLE && tex[i] == ra->ground0 + C3_GHOLE) { C3_QuadTint(c, a, b, cc, d, 0xff000000u, tex[i], uv, 0xff000000u); continue; }
                if (!g_c3Editor && ra && ra->nground > C3_GDUNES && tex[i] == ra->ground0 + C3_GDUNES) { C3_Dunes(c, k, i, r0, r1, tex[i], uv, ra); continue; }   /* (dunes in the game) */   /* (a hole's floor in the game: black, not its stripes) */   /* (painted water: the sea's shading, as it lies) */
                C3_QuadTint(c, a, b, cc, d, 0xffffffffu, tex[i], uv, i == c->darkStrip ? 0xff8a8a8au : 0xffffffffu);
                if (!g_c3Editor && ra && ra->nground > C3_GCLIFF && tex[i] == ra->ground0 + C3_GCLIFF && (fabs(r0->h[i + 1] - r0->h[i]) > fabs(r0->u[i + 1] - r0->u[i]) * 0.8 || fabs(r1->h[i + 1] - r1->h[i]) > fabs(r1->u[i + 1] - r1->u[i]) * 0.8))
                    C3_Mark(c, m0, 0xfc);   /* (a cliff's face: rock - render3d.h vertex alpha 252, strata) */
                if (ra && tex[i] >= ra->ground0 && tex[i] < ra->ground0 + ra->nground && tex[i] - ra->ground0 < 4 && ra->groundTree[tex[i] - ra->ground0])   /* (REDWOOD: its trees) */
                    C3_GroundTrees(c, k, r0->u[i], r0->u[i + 1], r0->h[i], r0->h[i + 1], r1->u[i], r1->u[i + 1], r1->h[i], r1->h[i + 1], ra->groundTree[tex[i] - ra->ground0] - 1);
                if (ra && (i == 0 || i == C3_NP - 2) && tex[i] >= ra->ground0 && tex[i] < ra->ground0 + ra->nground && tex[i] - ra->ground0 < 4 && ra->groundHedge[tex[i] - ra->ground0])   /* (REDWOOD beyond P2: its hedge) */
                    C3_GroundHedge(c, k, i, r0, r1, ra->groundHedge[tex[i] - ra->ground0] - 1);
                if (dirt) { C3_Mark(c, m0, 0xef);   /* dirt ground (vertex alpha 239: render3d.h - mottled, pebbles, bumps) and rocks on the flatter of it */
                    if (fabs(r0->h[i + 1] - r0->h[i]) < fabs(r0->u[i + 1] - r0->u[i]) * 0.6) C3_DirtRocks(c, k, r0->u[i], r0->u[i + 1], r0->h[i], r0->h[i + 1], r1->u[i], r1->u[i + 1], r1->h[i], r1->h[i + 1], m == M_VERGE, ra ? ra->rockTex : -1); } }
        } else C3_Quad(c, a, b, cc, d, (ground && r0->mat[i] != M_CURB && r0->mat[i] != M_WALK) ? ground : C3_Colour(r0->mat[i], kind, k));
    }
}

/* snow banks along both road edges (piece k .. k + 1, rows r0 / r1): a bumpy ridge from the gutter over the curb onto the sidewalk / verge, 4 bumps a
   piece.  Always in the mesh, vertex alpha 250: the world shader shows them only while it snows (render3d.h) */
static void C3_SnowBank(C3Course *c, int k, const C3Row *r0, const C3Row *r1)
{
    enum { NB = 4, NP = 5 }; int side, j, i;
    for (side = 0; side < 2; side++) {
        int ie = side ? 8 : 6, it = side ? 9 : 5; double s = side ? 1.0 : -1.0; float pa[NP][3], pb[NP][3];
        for (j = 0; j <= NB; j++) {
            double t = (double)j / NB, p = k + t, e = r0->u[ie] + (r1->u[ie] - r0->u[ie]) * t, top = r0->h[it] + (r1->h[it] - r0->h[it]) * t, x = p * 1.7 + side * 31.0, bump, wid;
            static const double fu[NP] = { -60, -18, 22, 70, 110 }, fh[NP] = { 0, 0.55, 1.0, 0.7, 0 };   /* across: gutter, front, crest, back, foot */
            bump = 16 + 30 * (0.5 + 0.25 * sin(x * 2.3) + 0.15 * sin(x * 5.1 + 1.3) + 0.1 * sin(x * 11.7 + 0.4));
            wid = 0.8 + 0.35 * (0.5 + 0.5 * sin(x * 1.1 + 2.0));
            if (top < 0) top = 0;
            for (i = 0; i < NP; i++) {
                double u = e + s * fu[i] * wid, hh = i == 0 ? 1 : (i == NP - 1 ? top + 1 : (i == 1 ? top * 0.55 + bump * fh[i] : top + bump * fh[i]));
                C3_Point(c, p, u, hh, pb[i]); }
            if (j) for (i = 0; i < NP - 1; i++) C3_Quad(c, pa[i], pa[i + 1], pb[i + 1], pb[i], 0xfaf2f4f8u);
            memcpy(pa, pb, sizeof pa); } }
}

/* skirts: the terrain does not end C3_FAR beyond the last profile point in the original - it paints the ground / hillside to the screen edges, so a
   wide view or the median of a split road shows terrain where the 3D strip ends and the backdrop shows through.  Each side gets a flat skirt from its
   last point C3_SKIRT units outwards at that point's height, in that span's texture.  A long flat strip would cut through other parts of a winding
   course - and a skirt higher than the camera (a hill above it, the other carriageway passing over) is a sheet over the sky - so skirts exist only
   in the live stretch round the camera (c->skirts; there the far carriageway is left out, C3_HideHalf) and go into background batches (vertex tex + C3_BG): the renderer draws them after the world with their depth squeezed into the far end
   of the range (render3d.h alpha 2) - they show only where nothing else is, and still sort among themselves.  (Sea sides: the sea covers it.) */
#define C3_BG (1 << 14)   /* > any texture count (R3_MAXTEX 4096); C3_Batches' counting sort spans 0 .. C3_BG + ntex */
#define C3_SKIRT 60000.0
static void C3_Skirt(C3Course *c, int k, const C3Row *r0, const C3Row *r1, int kind, const int *tex, unsigned ground, const RoadSection *s0, const RxAssets *ra)
{
    int side, kind0 = kind;
    for (side = 0; side < 2; side++) {
        int i = side ? C3_NP - 1 : 0, sp = side ? C3_NP - 2 : 0, t = tex ? tex[sp] : -1, j; float a[3], b[3], cc[3], d[3], uv[8]; unsigned col;
        double per = C3_TOP, q;
        kind = c->skOn ? c->skind[side] : kind0;
        if (kind >= SEC_WALL1 && kind <= SEC_WALL_SPLIT && s0->wall[side * 3 + 2] < 0) continue;   /* (drop to the sea) */
        if (side) { C3_Point(c, k, r0->u[i], r0->h[i], a); C3_Point(c, k, r0->u[i] + C3_SKIRT, r0->h[i], b); C3_Point(c, k + 1, r1->u[i] + C3_SKIRT, r1->h[i], cc); C3_Point(c, k + 1, r1->u[i], r1->h[i], d); }
        else { C3_Point(c, k, r0->u[i] - C3_SKIRT, r0->h[i], a); C3_Point(c, k, r0->u[i], r0->h[i], b); C3_Point(c, k + 1, r1->u[i], r1->h[i], cc); C3_Point(c, k + 1, r1->u[i] - C3_SKIRT, r1->h[i], d); }
        if (t >= 0 && ra && t < ra->ntex && ra->tex[t].w > 0 && ra->tex[t].h > 0) per = ra->tex[t].w * (c->L / C3_ALONG / ra->tex[t].h);
        q = C3_SKIRT / per;
        if (side) { uv[0] = 0; uv[1] = C3_ALONG; uv[2] = (float)q; uv[3] = C3_ALONG; uv[4] = (float)q; uv[5] = 0; uv[6] = 0; uv[7] = 0; }
        else { uv[0] = (float)q; uv[1] = C3_ALONG; uv[2] = 0; uv[3] = C3_ALONG; uv[4] = 0; uv[5] = 0; uv[6] = (float)q; uv[7] = 0; }
        col = t >= 0 ? 0xffffffffu : (ground ? ground : C3_Colour(r0->mat[side ? C3_NP - 2 : 0], kind, k));
        if (!g_c3Editor && ra && ra->nground > C3_GHOLE && t == ra->ground0 + C3_GHOLE) { t = -1; col = 0xff050505u; }   /* (beyond a hole: the abyss, black - not its stripes) */
        C3_QuadT(c, a, b, cc, d, col, t, uv);
        for (j = c->nv - 6; j < c->nv; j++) c->v[j].tex += (float)C3_BG;   /* (also flat colour: -1 + C3_BG) */
    }
}

/* roadside textures of walls (kinds 1..3, see below) and SEC_PROFILE terrain (rr_StripTableDraw -> LaneLookupMax / LaneLookupFloor: BankEntry2(sideTex[side], 1, span)): span 0 = outer edge -> P0,
   1 = P0 -> P1, 2 = P1 -> P2 (the far extension repeats span 2); in row terms (C3_Section) left spans 3 / 2 / 1 (+ 0), right 10 / 11 / 12 (+ 13).
   A span painted in the Course Editor (ra->ext: course_edit.h RrcExtra.band) takes that row of that group instead - terrain only */
static int C3_FarGround(RxAssets *ra, int k, int h, unsigned code, int row)   /* ground picture code / row from the sprite sets loaded nearest piece k that
                                                                                have it (-1 none): an edited terrain side (RrcExtra.sideKind) where the sets
                                                                                loaded have no ground (downtown) */
{
    int r, d;
    for (r = 1; r < ra->next; r++) for (d = -1; d <= 1; d += 2) {
        int j = k + d * r, rw = row; RxSet *st; unsigned t;
        if (j < 0 || j >= ra->next) continue;
        st = RX_SetFor(ra, j, h, (int)(code >> 6) & 3); t = st ? RX_Lookup(st, 1, code, &rw, 1) : 0;
        if (t) return RX_TexIn(ra, st, t);
    }
    return -1;
}
/* a painted picture (course_edit.h RRC_BAND value: RRC_CAT5 | code << 2 | row) from set sid (RxSet.id; 0: the code's slot at piece k) -> texture, -1 none */
static int C3_PaintTex(RxAssets *ra, int k, int h, unsigned v, unsigned sid)
{
    unsigned cd = (v >> 2) & 0xff, t; int row = (int)(v & 3); RxSet *st;
    if (!(v & RRC_BAND)) return -1;
    if ((v & (RRC_GEN | RRC_TEXG | RRC_CAT5)) == RRC_GEN) return (int)(v & 0xff) < ra->nground ? ra->ground0 + (int)(v & 0xff) : -1;   /* (an extra ground picture: ground_art.h) */
    st = sid ? RX_SetById(ra, sid) : RX_SetFor(ra, k, h, (int)(cd >> 6) & 3);
    t = st ? RX_Lookup(st, (v & RRC_CAT5) ? 5 : 1, cd, &row, 1) : 0;
    return t ? RX_TexIn(ra, st, t) : -1;
}
/* the picture painted on a cell of piece k's side (the editor's grid: 0 road side, 1..3 the bands P0..P2, 4 past P2, 5 the road edge - RrcExtra.band / cell / edge);
   -1 none */
static int C3_CellTex(RxAssets *ra, int k, int h, int side, int cell)
{
    const RrcExtra *x;
    if (!ra || !ra->ext || k < 0 || k >= ra->next) return -1;
    x = &ra->ext[k];
    if (cell >= 1 && cell <= 3) return C3_PaintTex(ra, k, h, x->band[h & 1][side & 1][cell - 1], x->bandSet[h & 1][side & 1][cell - 1]);
    if (cell == 5) return C3_PaintTex(ra, k, h, x->edge[h & 1][side & 1], x->edgeSet[h & 1][side & 1]);
    return C3_PaintTex(ra, k, h, x->cell[h & 1][side & 1][cell ? 1 : 0], x->cellSet[h & 1][side & 1][cell ? 1 : 0]);
}
static void C3_ProfileTex(RxAssets *ra, const RoadSection *s, int k, int h, int *tex)
{
    int side, span, i;
    for (i = 0; i < C3_NP - 1; i++) tex[i] = -1;
    if (!ra || s->kind > SEC_PROFILE) return;
    for (side = 0; side < 2; side++) {
        unsigned code = s->sideTex[side]; RxSet *set; int sk = C3_SideKind(ra, s, k, h, side);
        const unsigned short *bd = sk == SEC_PROFILE && ra->ext && k >= 0 && k < ra->next ? ra->ext[k].band[h & 1][side] : NULL;
        if (bd && !((bd[0] | bd[1] | bd[2]) & RRC_BAND)) bd = NULL;
        if (code == 0xff && !bd) continue;
        if (sk == SEC_OPEN) continue;   /* (an open-road side: its facade, C3_Roadside) */
        set = code != 0xff ? RX_SetFor(ra, k, h, (int)(code >> 6) & 3) : NULL;
        if (sk != SEC_PROFILE) {   /* walls (OwnerTableFill / OctagonalDistance): row 0 = the face, row 1 = the rest (top + beyond) */
            int r0 = 0, r1 = 1; unsigned t0 = RX_Lookup(set, 1, code, &r0, 1), t1 = RX_Lookup(set, 1, code, &r1, 1);
            int x0 = t0 ? RX_TexIn(ra, set, t0) : -1, x1 = t1 ? RX_TexIn(ra, set, t1) : -1;
            {   int pl = RX_SideTex(ra, s, k, h, side, 1);   /* the shoulder road edge -> wall foot: SideStripDraw, set category 5 sideArg row 0 (SANDL: sand with the white edge line) */
                if (pl < 0) pl = ra->global[17];
                int up = s->wall[side * 3 + 2] > 0;   /* cliff: the face, then its top (row 1, TOP1A: green); drop: the slope down to the sea in the face texture */
                int a = x0, b = up && x1 >= 0 ? x1 : x0, f = b;
                if (side == 0) { tex[2] = a; tex[1] = b; tex[0] = f; tex[3] = tex[4] = pl; } else { tex[11] = a; tex[12] = b; tex[13] = f; tex[10] = tex[9] = pl; } }
            continue;
        }
        for (span = 0; span < 3; span++) {
            unsigned cd = code, t; int row = span, x; RxSet *st = set;
            if (bd && (bd[span] & RRC_BAND)) { unsigned sid = ra->ext[k].bandSet[h & 1][side][span]; cd = (bd[span] >> 2) & 0xff; row = bd[span] & 3;   /* (painted: from its own set, */
                st = sid ? RX_SetById(ra, sid) : RX_SetFor(ra, k, h, (int)(cd >> 6) & 3); }                                                         /* or the slot's here) */
            {   int row0 = row; t = st && cd != 0xff ? RX_Lookup(st, bd && (bd[span] & RRC_BAND) && (bd[span] & RRC_CAT5) ? 5 : 1, cd, &row, 1) : 0;
                x = t ? RX_TexIn(ra, st, t) : -1;
                if (x < 0 && cd != 0xff && ra->ext && k >= 0 && k < ra->next && ra->ext[k].sideKind[h & 1][side] == 2) x = C3_FarGround(ra, k, h, cd, row0); }   /* (edited: see C3_FarGround) */
            if (side == 0) tex[3 - span] = x; else tex[10 + span] = x;
        }
        {   int vg = RX_SideTex(ra, s, k, h, side, 1);   /* the verge road edge -> outer edge: SideStripDraw, set category 5 sideArg row 0 (else like span 0 - its own
                                                            picture, not one painted on P0: user, 2026-11-18 "when setting p0 it shouldn't automatically apply to the verge") */
            if (vg < 0 && bd && (bd[0] & RRC_BAND)) { int row = 0; unsigned t = set && code != 0xff ? RX_Lookup(set, 1, code, &row, 1) : 0; vg = t ? RX_TexIn(ra, set, t) : -1;
                if (vg < 0 && code != 0xff && ra->ext && k >= 0 && k < ra->next && ra->ext[k].sideKind[h & 1][side] == 2) vg = C3_FarGround(ra, k, h, code, 0); }
            int fr = side ? tex[12] : tex[1];   /* (past P2: P2's own picture too, not one painted there) */
            if (bd && (bd[2] & RRC_BAND)) { int row = 2; unsigned t = set && code != 0xff ? RX_Lookup(set, 1, code, &row, 1) : 0; int x = t ? RX_TexIn(ra, set, t) : -1;
                if (x < 0 && code != 0xff && ra->ext && k >= 0 && k < ra->next && ra->ext[k].sideKind[h & 1][side] == 2) x = C3_FarGround(ra, k, h, code, 2);
                if (x >= 0) fr = x; }
            if (side == 0) { tex[0] = fr; tex[4] = vg >= 0 ? vg : tex[3]; } else { tex[13] = fr; tex[9] = vg >= 0 ? vg : tex[10]; } }
    }
}

/* the cells painted (C3_CellTex) onto the strip's textures tex (C3_ProfileTex): open-road and terrain sides - a street's road side is C3_Roadside's */
static void C3_CellOverride(RxAssets *ra, const RoadSection *s, int k, int h, int *tex)
{
    int side, c, t, sk;
    for (side = 0; side < 2; side++) {
        if ((sk = C3_SideKind(ra, s, k, h, side)) != SEC_OPEN && sk != SEC_PROFILE) continue;
        for (c = sk == SEC_OPEN ? 1 : 0; c < (sk == SEC_OPEN ? 5 : 6); c++) if ((t = C3_CellTex(ra, k, h, side, c)) >= 0) {
            int i = c == 5 ? (side ? 8 : 5) : c == 4 ? (side ? 13 : 0) : c == 0 ? (side ? 9 : 4) : (side ? 9 + c : 4 - c);   /* (the strip's segment: C3_ProfileTex) */
            tex[i] = t; }
    }
}

/* piece k's half h is a cross street (SEC_OPEN, kind flags 0x20: at road level, no sidewalk) */
static int C3_IsCross(const C3Course *c, int k, int h)
{
    const RoadSection *s;
    if (k < 0 || k >= c->n || (h && c->piece[k].layout != 2)) return 0;
    s = h ? &c->piece[k].halfB : &c->piece[k].halfA;
    return s->kind == SEC_OPEN && (s->kindFlags & 0x20);
}

/* facade height of piece k's half h on one side (0 = none: no piece, not SEC_OPEN, a cross street, no facade texture) */
/* more variety in a row of buildings (ra->ngen generated facades, world3d.inc W3GenFacades - urban tracks only): per block of 8 pieces on each side, every
   building gets one of the generated facades in place of the course's (shop fronts, offices, houses) and a height 1 .. 1.6 times the course's.  The same for every caller
   (C3_FacadeH compares neighbours with it: end walls where the heights differ) */
/* render3d.h's Hu (an integer hash of a float2's whole parts, 0..1): the gutter's drains are placed with it there, their kerb inlets here */
static float C3_Hu(double x, double y)
{   unsigned u = (unsigned)(int)x * 374761393u + (unsigned)(int)y * 668265263u; u = (u ^ (u >> 13)) * 1274126177u; u ^= u >> 16; return (u & 65535u) / 65535.0f; }
/* (the City set, ra->sign0 / nsign > 0 or its facades: CITY_SCENERY.md) buildings of 1 .. 6 pieces, not blocks of 8 - a new building starts at
   piece k where C3_Hu(k, side) < 0.36; its facade and its height (0.85 .. 1.9 times) from its first piece; no two neighbours alike */
static int C3_BldgStart(int k, int h, int side)
{   int j; for (j = k; j > k - 5; j--) if (C3_Hu(j, side * 7 + h * 3 + 0.5) < 0.36) return j; return j; }
static void C3_FacadeVary(const RxAssets *ra, int k, int h, int side, int *fac, double *fh)
{   unsigned u = (unsigned)((k + side * 3) / 8) * 2654435761u ^ (unsigned)(side * 40503 + h * 977 + 12345);
    if (ra->ngen <= 0 || *fac < 0) return;
    if (ra->tex[ra->gen0].shopV > 0) {   /* the City set */
        int b = C3_BldgStart(k, h, side), pb = C3_BldgStart(b - 1, h, side); unsigned v = (unsigned)b * 2654435761u ^ (unsigned)(side * 40503 + h * 977 + 777), w;
        v ^= v >> 15; v *= 2246822519u; v ^= v >> 13;
        w = (unsigned)pb * 2654435761u ^ (unsigned)(side * 40503 + h * 977 + 777); w ^= w >> 15; w *= 2246822519u; w ^= w >> 13;
        { int f = (int)((v >> 8) % (unsigned)ra->ngen); if (f == (int)((w >> 8) % (unsigned)ra->ngen)) f = (f + 1) % ra->ngen;   /* (not the one before) */
          *fac = ra->gen0 + f; }
        *fh *= 0.85 + ((v >> 20) % 106) / 100.0; return; }
    u ^= u >> 15; u *= 2246822519u; u ^= u >> 13;
    *fac = ra->gen0 + (int)((u >> 8) % (unsigned)ra->ngen); *fh *= 1.0 + ((u >> 20) % 61) / 100.0; }
/* a footpath's texture (C3_SideWalk): row 0 the path, 1 the kerb face - from the sprite sets loaded nearest piece k that have it; -1 none */
static int C3_WalkTex(RxAssets *ra, const RoadSection *s, int k, int h, int side, int row)
{
    int w = C3_SideWalk(ra, s, k, h, side), r, d; unsigned code;
    if (!w) return -1;
    code = (unsigned)(w - 1);
    for (r = 0; r < ra->next; r++) for (d = -1; d <= 1; d += 2) {
        int j = k + d * r, rw = row, x; RxSet *st; unsigned t;
        if ((r == 0 && d > 0) || j < 0 || j >= ra->next || !(st = RX_SetFor(ra, j, h, (int)(code >> 6) & 3))) continue;
        if ((t = RX_Lookup(st, 5, code, &rw, 1)) && (x = RX_TexIn(ra, st, t)) >= 0) return x;
    }
    return -1;
}
/* the facade chosen for piece k's side in the editor (RrcExtra.fac) into *fac: 1, else 0 (the course's own mix) */
static int C3_FacPick(RxAssets *ra, int k, int h, int side, int *fac)
{
    unsigned v; int x;
    if (!ra || !ra->ext || k < 0 || k >= ra->next || !((v = ra->ext[k].fac[h & 1][side & 1]) & RRC_BAND)) return 0;
    if (v & RRC_GEN) { if ((int)(v & 0xff) >= ra->ngen) return 0; *fac = ra->gen0 + (int)(v & 0xff); return 1; }
    {   RxSet *st = ra->ext[k].facSet[h & 1][side & 1] ? RX_SetById(ra, ra->ext[k].facSet[h & 1][side & 1]) : RX_SetFor(ra, k, h, (int)(v >> 8) & 3); unsigned t;
        if (!st || !(t = RX_Lookup(st, 1, (v >> 2) & 0xff, NULL, 0)) || (x = RX_TexIn(ra, st, t)) < 0) return 0;
        *fac = x; return 1; }
}
/* the facade of the buildings on piece k's terrain side (C3_SideBld): RrcExtra.bldCode from the sprite sets loaded nearest k that have it as a facade; 0 none */
static int C3_BandFacade(RxAssets *ra, const RoadSection *s, int k, int h, int side, int *fac, double *fh)
{
    unsigned code; int r, d;
    if (!C3_SideBld(ra, s, k, h, side)) return 0;
    code = ra->ext[k].bldCode[h & 1][side & 1];
    for (r = 0; r < ra->next; r++) for (d = -1; d <= 1; d += 2) {
        int j = k + d * r; RxSet *st; unsigned t; const unsigned char *g;
        if ((r == 0 && d > 0) || j < 0 || j >= ra->next || !(st = RX_SetFor(ra, j, h, (int)(code >> 6) & 3))) continue;
        t = RX_Lookup(st, 1, code, NULL, 0); g = (const unsigned char *)RX_W(st, RX_Entry(st, 1, code, NULL, 0), 0x20);
        if (!t || !g || *(const unsigned *)g != 0x434c4750 || g[0x16] * 16 <= 0x10 || (*fac = RX_TexIn(ra, st, t)) < 0) continue;
        *fh = g[0x16] * 16; C3_FacadeVary(ra, k, h, side, fac, fh); C3_FacPick(ra, k, h, side, fac); return 1;
    }
    return 0;
}
static double C3_FacadeH(C3Course *c, RxAssets *ra, int k, int h, int side)
{
    const RoadSection *s;
    if (k < 0 || k >= c->n || (h && c->piece[k].layout != 2)) return 0;
    s = h ? &c->piece[k].halfB : &c->piece[k].halfA;
    if (C3_SideKind(ra, s, k, h, side) != SEC_OPEN || (s->kindFlags & 0x20) || RX_SideTex(ra, s, k, h, side, 0) < 0) return 0;
    {   int fac = RX_SideTex(ra, s, k, h, side, 0); double fh = RX_FacadeHeight(ra, s, k, h, side); C3_FacadeVary(ra, k, h, side, &fac, &fh); return fh; }
}
/* the height of piece k's buildings on that side when they stand on the same line as those being put up (kind: c->bldKind - a street's, or a terrain side's
   from the same band); 0 otherwise: the row ends there, its end wall drawn (user, 2026-11-17: "the buildings should always just have closed faces") */
static double C3_FacadeLike(C3Course *c, RxAssets *ra, int k, int h, int side, int kind)
{
    const RoadSection *s; int fac; double fh = 0;
    if (k < 0 || k >= c->n || (h && c->piece[k].layout != 2)) return 0;
    if (!kind) return C3_FacadeH(c, ra, k, h, side);
    s = h ? &c->piece[k].halfB : &c->piece[k].halfA;
    return C3_SideBld(ra, s, k, h, side) == kind && C3_BandFacade(ra, s, k, h, side, &fac, &fh) ? fh : 0;
}

/* a building behind a facade (the game only draws the front, rr_SkidMarkDraw): the facade, a roof and a back wall C3_BLDG deep, and an end wall
   where the piece before / after has no facade or another height (rows of buildings end at cross streets).  It runs from piece position pa to pb
   (k .. k + 1, shorter beside a cross street: C3_Corner); corner[end] = an end wall there whatever the neighbour */
#define C3_BLDG 1500.0
static void C3_Building(C3Course *c, int k, int h, int side, double pa, double pb, double x0, double x1, double t0, double t1, double fh, int fac,
                        RxAssets *ra, const int *corner)
{
    double sg = side ? 1 : -1, b0 = x0 + sg * C3_BLDG, b1 = x1 + sg * C3_BLDG;
    RxTex *tx = &ra->tex[fac];
    /* the game puts the whole texture on each segment's facade (one sprite per view segment): stretched near the camera, narrow far out where its
       depth is squeezed.  Here half the copies of square texels: 0.5 or 1 per segment (compared side by side with the original) */
    double texel = fh / (tx->h > 0 ? tx->h : 1), sq = c->L / ((tx->w > 0 ? tx->w : 64) * texel), n = sq < 1.5 ? 0.5 : 1, per = c->L / n;
    float uk0 = (float)(pa * n), uk1 = (float)(pb * n), ud = (float)(C3_BLDG / per);
    tx->key = 1; tx->facade = 1;   /* drawn as sprites by the game: index 0 is see-through (roof outlines ...); facade: lit windows at night (world3d.inc W3GpuTex) */
    unsigned roof = 0xff3c3a38u; int end;
    {   float a[3], b[3], cc[3], d[3], uv[8];
        uv[0] = uk0; uv[1] = 1; uv[2] = uk0; uv[3] = 0; uv[4] = uk1; uv[5] = 0; uv[6] = uk1; uv[7] = 1;   /* a / b = foot / top at pa, cc / d = top / foot at pb */
        C3_Point(c, pa, x0, t0, a); C3_Point(c, pa, x0, t0 + fh, b); C3_Point(c, pb, x1, t1 + fh, cc); C3_Point(c, pb, x1, t1, d);
        C3_QuadTint(c, a, b, cc, d, 0xffffffffu, fac, uv, 0xfeffffffu);   /* facade (vertex alpha 254: its windows may light up at night, render3d.h) */
        C3_Point(c, pa, b0, t0, a); C3_Point(c, pa, b0, t0 + fh, b); C3_Point(c, pb, b1, t1 + fh, cc); C3_Point(c, pb, b1, t1, d);
        C3_QuadTint(c, a, b, cc, d, 0xffffffffu, fac, uv, 0xfeffffffu);   /* back wall */
        if (!tx->holes) {   /* a facade with see-through texels (gables, ornate tops) has no roof: the game shows the sky there, not a slab seen from below */
            C3_Point(c, pa, x0, t0 + fh, a); C3_Point(c, pa, b0, t0 + fh, b); C3_Point(c, pb, b1, t1 + fh, cc); C3_Point(c, pb, x1, t1 + fh, d);
            C3_Quad(c, a, b, cc, d, roof);
            {   /* snow lying on the roof, bumpy (drifts), thin at the edges - vertex alpha 250: seen only while it snows (render3d.h, as C3_SnowBank) */
                enum { NA = 4, NC = 6 }; float g[NA + 1][NC + 1][3]; int ia, ic;
                for (ia = 0; ia <= NA; ia++) for (ic = 0; ic <= NC; ic++) {
                    double ta = (double)ia / NA, tc = (double)ic / NC, p = pa + (pb - pa) * ta, xf = x0 + (x1 - x0) * ta, xb = b0 + (b1 - b0) * ta, top = t0 + (t1 - t0) * ta + fh;
                    double edge = (ic == 0 || ic == NC) ? 0.15 : 1.0, x = p * 2.9 + tc * 4.1 + side * 7.0;
                    double bump = 4 + edge * (18 + 48 * (0.5 + 0.3 * sin(x * 1.7) + 0.2 * sin(x * 4.3 + tc * 9.0)));
                    C3_Point(c, p, xf + (xb - xf) * tc, top + bump, g[ia][ic]); }
                for (ia = 0; ia < NA; ia++) for (ic = 0; ic < NC; ic++) C3_Quad(c, g[ia][ic], g[ia][ic + 1], g[ia + 1][ic + 1], g[ia + 1][ic], 0xfaf2f4f8u); } }
    }
    if (ra->nsign > 0 && tx->shopV > 0 && pb - pa > 0.4 && C3_Hu(k * 3 + 1, side * 11 + h * 5 + 2) < 0.30) {
        /* a projecting (blade) sign on about every third piece of a City-set building (CITY_SCENERY.md): upright, square to the facade, from 14 to
           134 units out over the footpath, its foot 30 over the shop line; a dark bracket to the wall at its top.  Both faces (the back one mirrored so the
           lettering reads from either way); vertex alpha 254 like the walls (the shader: its neon tubes / lettering glow at night) */
        int sg2 = ra->sign0 + (int)(C3_Hu(k * 5 + 3, side + h * 2 + 9) * ra->nsign) % ra->nsign; double f = 0.3 + 0.4 * C3_Hu(k + 7, side + 3.5), p = pa + (pb - pa) * f;
        double xw = x0 + (x1 - x0) * f, tg = t0 + (t1 - t0) * f, out = -sg, xa = xw + out * 14, xb = xw + out * 134, y0 = tg + fh * (1 - tx->shopV) + 30, y1 = y0 + 360;
        if (y1 < tg + fh - 20) {
            float a[3], b[3], cc[3], d[3], uv[8] = { 0, 1, 0, 0, 1, 0, 1, 1 };
            C3_Point(c, p, xa, y0, a); C3_Point(c, p, xa, y1, b); C3_Point(c, p, xb, y1, cc); C3_Point(c, p, xb, y0, d);
            C3_QuadTint(c, a, b, cc, d, 0xffffffffu, sg2, uv, 0xfeffffffu); C3_QuadTint(c, d, cc, b, a, 0xffffffffu, sg2, uv, 0xfeffffffu);
            {   double dp = 4.0 / c->L; unsigned br = 0xff303234u;   /* the bracket: a bar from the wall over the sign's top, a stay down to its middle */
                C3_Point(c, p - dp, xw, y1 + 10, a); C3_Point(c, p - dp, xb, y1 + 10, b); C3_Point(c, p + dp, xb, y1 + 10, cc); C3_Point(c, p + dp, xw, y1 + 10, d); C3_Quad(c, a, b, cc, d, br); C3_Quad(c, d, cc, b, a, br);
                C3_Point(c, p, xw, y1 + 2, a); C3_Point(c, p, xw, y1 + 12, b); C3_Point(c, p, xb, y1 + 12, cc); C3_Point(c, p, xb, y1 + 2, d); C3_Quad(c, a, b, cc, d, br); C3_Quad(c, d, cc, b, a, br); } } }
    if (c->recBldg) {   /* (its footprint: its shadow) */
        if (c->nbldg == c->capBldg) { int nc = c->capBldg ? c->capBldg * 2 : 1024; C3Bldg *nb = (C3Bldg *)realloc(c->bldg, sizeof(C3Bldg) * nc); if (nb) { c->bldg = nb; c->capBldg = nc; } }
        if (c->nbldg < c->capBldg) { C3Bldg *B = &c->bldg[c->nbldg++];
            C3_Point(c, pa, x0, t0, B->f[0]); C3_Point(c, pa, b0, t0, B->f[1]); C3_Point(c, pb, b1, t1, B->f[2]); C3_Point(c, pb, x1, t1, B->f[3]); B->h = (float)fh; B->piece = k; } }
    for (end = 0; end < 2; end++) {   /* end walls at pa (before) / pb (after) */
        int nb = end ? k + 1 : k - 1; double p = end ? pb : pa, xa = end ? x1 : x0, xb = end ? b1 : b0, t = end ? t1 : t0;
        float a[3], b[3], cc[3], d[3], uv[8];
        uv[0] = 0; uv[1] = 1; uv[2] = ud; uv[3] = 1; uv[4] = ud; uv[5] = 0; uv[6] = 0; uv[7] = 0;   /* square texels across the building's depth too */
        if (!corner[end] && C3_FacadeLike(c, ra, nb, h, side, c->bldKind) == fh) continue;
        C3_Point(c, p, xa, t, a); C3_Point(c, p, xb, t, b); C3_Point(c, p, xb, t + fh, cc); C3_Point(c, p, xa, t + fh, d);
        C3_QuadTint(c, a, b, cc, d, 0xffffffffu, fac, uv, 0xfec8c8c8u);   /* the side wall a little in shade (the sun shades it more, or less: render3d.h) */
    }
}

/* the sidewalk corner beside a cross street (kind flags 8 = the piece before it, 0x10 = the piece after; rr_WallStripesDraw with the corner point the
   projection puts at section +0xa0 / +0xa8, road_view.c): the sidewalk turns into the cross street - 16 tiles of 256 units outwards from its outer
   edge (set category 5, sideArg row 2), shoulderW / 256 of the piece deep at the cross street's end, with their face down to the cross street (row 3).
   The facade only starts behind the corner.  The sidewalk's own end face (lane edge -> outer edge, rr_EdgeStripDraw flag 0x10: row 1) closes the
   step too - without these faces the step was a slot through the world (the background showed through as noise at the foot of the buildings).
   Returns the depth of the corner in pieces (0 = no corner) and its piece position pc (the cross street's end) */
static double C3_Corner(C3Course *c, int k, int h, int side, const RoadSection *s0, double e0, double x0, double t0, double e1, double x1, double t1,
                        RxAssets *ra, double *pc)
{
    int corner = (s0->kindFlags & 0x18) != 0, after = corner ? (s0->kindFlags & 0x10) != 0 : C3_IsCross(c, k - 1, h), n, top, face, curb;
    double sg = side ? 1 : -1, f = corner ? s0->shoulderW[side] / 256.0 : 0, pe, pi, e, x, t, xi, ti;
    float a[3], b[3], cc[3], d[3], uv[8];
    if (!C3_IsCross(c, after ? k - 1 : k + 1, h)) return 0;   /* (no corner flags: only the end face) */
    if (f < 0) f = -f;
    if (f > 1) f = 1;
    pe = after ? k : k + 1; pi = after ? k + f : k + 1 - f; *pc = pe;
    e = after ? e0 : e1; x = after ? x0 : x1; t = after ? t0 : t1;
    xi = x0 + (x1 - x0) * (pi - k); ti = t0 + (t1 - t0) * (pi - k);
    top = RX_SideTex(ra, s0, k, h, side, 3); face = RX_SideTex(ra, s0, k, h, side, 4); curb = RX_SideTex(ra, s0, k, h, side, 2);
    if (top < 0) top = RX_SideTex(ra, s0, k, h, side, 1);
    if (face < 0) face = curb;
    if (t > 0) {   /* the sidewalk's end face */
        float w = (float)(fabs(x - e) / 256.0);
        C3_Point(c, pe, e, 0, a); C3_Point(c, pe, e, t, b); C3_Point(c, pe, x, t, cc); C3_Point(c, pe, x, 0, d);
        uv[0] = 0; uv[1] = 1; uv[2] = 0; uv[3] = 0; uv[4] = w; uv[5] = 0; uv[6] = w; uv[7] = 1;   /* as the curb face: v down the face */
        { int m0 = c->nv; C3_QuadT(c, a, b, cc, d, C3_Colour(M_CURB, 0, k), curb, uv); C3_Mark(c, m0, 0xf7); }
    }
    if (f <= 0) return 0;
    for (n = 0; n < 16; n++) {   /* (1 unit above the sidewalk height: the town ground beyond the sidewalk lies at it - the same plane flickered) */
        double u0 = sg * 256 * n, u1 = sg * 256 * (n + 1);
        C3_Point(c, pe, x + u0, t + 1, a); C3_Point(c, pe, x + u1, t + 1, b); C3_Point(c, pi, xi + u1, ti + 1, cc); C3_Point(c, pi, xi + u0, ti + 1, d);
        uv[0] = 0; uv[1] = 1; uv[2] = 1; uv[3] = 1; uv[4] = 1; uv[5] = 0; uv[6] = 0; uv[7] = 0;
        { int m0 = c->nv; C3_QuadT(c, a, b, cc, d, C3_Colour(M_WALK, 0, k), top, uv); C3_Mark(c, m0, 0xf6); }   /* tile */
        if (t > 0) {
            C3_Point(c, pe, x + u0, 0, a); C3_Point(c, pe, x + u0, t + 1, b); C3_Point(c, pe, x + u1, t + 1, cc); C3_Point(c, pe, x + u1, 0, d);
            uv[0] = 0; uv[1] = 1; uv[2] = 0; uv[3] = 0; uv[4] = 1; uv[5] = 0; uv[6] = 1; uv[7] = 1;
            { int m0 = c->nv; C3_QuadT(c, a, b, cc, d, C3_Colour(M_CURB, 0, k), face, uv); C3_Mark(c, m0, 0xf7); }   /* its face */
        }
    }
    return f;
}


/* the buildings on a terrain side (C3_SideBld): their front on the band's inner profile point, rows r0 / r1 (C3_SectionAt) */
static void C3_BandBuildings(C3Course *c, int k, int h, const RoadSection *s0, const C3Row *r0, const C3Row *r1, RxAssets *ra)
{
    int side;
    for (side = 0; side < 2; side++) {
        int fb = C3_SideBld(ra, s0, k, h, side), fac, i, corner[2] = { 0, 0 }; double fh;
        if (!fb || !C3_BandFacade(ra, s0, k, h, side, &fac, &fh) || fh <= 0) continue;
        {   int pt = fb == 4 ? 2 : 2 + fb; i = side ? 8 + pt : 6 - pt; }   /* (their front's point in the row: the verge's edge / profile point fb - 1 - C3_SectionK) */
        c->bldKind = fb; C3_Building(c, k, h, side, k, k + 1, r0->u[i], r1->u[i], r0->h[i], r1->h[i], fh, fac, ra, corner); c->bldKind = 0;
    }
}

/* the gutter along a road edge (vertex alpha 241: render3d.h draws the concrete pan and the drains): a strip ~45 units wide on the road at the curb's foot,
   u = 0 at the curb .. 1 out, v = the piece number (the drains' places); e0 / e1 the edge at k / k + 1, t0 / t1 the kerb's height there; lift: the road not
   sunk at its edges (C3_Lanes: a terrain side's gutter, RrcExtra.gutter 1) - the strip that much higher */
static void C3_Gutter(C3Course *c, int k, int side, double e0, double e1, double t0, double t1, double lift, double gw, double gdp)
{
    float a[3], b[3], cc[3], d[3], uv[8]; double ga, gb, gc, gd, ya, yb; int m0 = c->nv;   /* (sunk: C3_GUTD at the curb, the road's level at its outer edge) */
    if (side == 0) { ga = e0; gb = e0 + gw; gc = e1 + gw; gd = e1; uv[0] = 0; uv[2] = 1; uv[4] = 1; uv[6] = 0; ya = 0.6 - gdp + lift; yb = 0.6 + lift; }
    else { ga = e0 - gw; gb = e0; gc = e1; gd = e1 - gw; uv[0] = 1; uv[2] = 0; uv[4] = 0; uv[6] = 1; ya = 0.6 + lift; yb = 0.6 - gdp + lift; }
    uv[1] = (float)k; uv[3] = (float)k; uv[5] = (float)(k + 1); uv[7] = (float)(k + 1);
    C3_Point(c, k, ga, ya, a); C3_Point(c, k, gb, yb, b); C3_Point(c, k + 1, gc, yb, cc); C3_Point(c, k + 1, gd, ya, d);
    C3_QuadT(c, a, b, cc, d, 0xff8a8a86u, -1, uv); C3_Mark(c, m0, 0xf1);
    if (C3_Hu(k, 3.7) > 0.55f && C3_Hu(k / 6, 9.1) >= 0.25f && C3_Hu(k, 5.3) > 0.4f) {   /* a drain (render3d.h: Hu(pc, 3.7) > 0.55, not a grate run) with a
                                                                            kerb inlet (Hu(pc, 5.3) > 0.4, its grate then reaching the curb): the dark mouth in the curb face behind it */
        double f0 = 0.39, f1 = 0.61, xa = e0 + (e1 - e0) * f0 + (side ? -1 : 1), xb = e0 + (e1 - e0) * f1 + (side ? -1 : 1), y1 = (t0 + (t1 - t0) * 0.5) * 0.42;
        if (y1 > 1.5) { C3_Point(c, k + f0, xa, 0.6 - gdp + lift, a); C3_Point(c, k + f0, xa, y1, b); C3_Point(c, k + f1, xb, y1, cc); C3_Point(c, k + f1, xb, 0.6 - gdp + lift, d);
            C3_Quad(c, a, b, cc, d, 0xff0c0d0eu); C3_Quad(c, d, cc, b, a, 0xff0c0d0eu); } }
}

/* SEC_OPEN roadside with the track's textures: curb face and sidewalk (rr_EdgeStripDraw: set category 5, sideArg rows 1 / 0) and the facade on the
   sidewalk's outer edge (rr_SkidMarkDraw: category 1, sideTex; height from the group, WheelLaneEntriesFill) */
static void C3_Roadside(C3Course *c, int k, int h, const RoadSection *s0, double o0, const RoadSection *s1, double o1, RxAssets *ra)
{
    int side;
    if (s0->kindFlags & 0x20) {   /* a cross street (rr_WheelEdgeDraw): from the lane edge outwards 16 strips of 512 units at road level - the
                                     crosswalk (g_roadTexCrosswalk = TEXG 10), then side lanes (tbl_roadTexSideLanes[(flags & 7) - 1] = TEXG 7 / 8) */
        int tab = (s0->kindFlags & 7) ? (s0->kindFlags & 7) - 1 : 0, n;
        for (side = 0; side < 2; side++) {
            double sg = side ? 1 : -1, e0 = (side ? s0->laneEdgeR : s0->laneEdgeL) + o0, e1 = (side ? s1->laneEdgeR : s1->laneEdgeL) + o1;
            for (n = 0; n < 16; n++) {
                int t = ra->global[n == 0 ? 10 : 7 + (tab & 1)];
                double a0 = e0 + sg * 512 * n, a1 = e0 + sg * 512 * (n + 1), b0 = e1 + sg * 512 * n, b1 = e1 + sg * 512 * (n + 1);
                if (side) C3_Span(c, k, a0, 0, a1, 0, b0, 0, b1, 0, C3_Colour(M_ROAD, 0, k), t, 0);
                else C3_Span(c, k, a1, 0, a0, 0, b1, 0, b0, 0, C3_Colour(M_ROAD, 0, k), t, 0);
            }
        }
        return;
    }
    for (side = 0; side < 2; side++) {   /* (the curb where C3_Side ends the road: the inner of lane edge and road edge - forks off, C3_OneRoadWidth pulls the
                                            road edge in beside a split and leaves the lane edge, City 3263..3302 after the join: a hole down to the backdrop) */
        if (C3_SideKind(ra, s0, k, h, side) != SEC_OPEN) continue;   /* (a terrain side of an open-road piece: C3_Strip's hillside) */
        {
        double e0 = (side ? (s0->laneEdgeR < s0->edgeR ? s0->laneEdgeR : s0->edgeR) : (s0->laneEdgeL > s0->edgeL ? s0->laneEdgeL : s0->edgeL)) + o0;
        double e1 = (side ? (s1->laneEdgeR < s1->edgeR ? s1->laneEdgeR : s1->edgeR) : (s1->laneEdgeL > s1->edgeL ? s1->laneEdgeL : s1->edgeL)) + o1;
        double x0 = (side ? s0->outerR : s0->outerL) + o0, x1 = (side ? s1->outerR : s1->outerL) + o1;
        double t0 = s0->shoulderH[side], t1 = s1->shoulderH[side];
        double km, rl, gw, gdp, sgn = side ? 1 : -1;
        C3_EdgeStyle(ra, k, h, side, &km, &rl, &gw, &gdp); t0 *= km;   /* (the curb's style: C3_EdgeStyle - P3D flat) */
        { double k1, r1, g1, d1; C3_EdgeStyle(ra, k + 1, h, side, &k1, &r1, &g1, &d1); t1 *= k1; }   /* (its end: the next piece's - a flush footpath meeting a raised one ramps
                                                                                                    up along the piece, no step with the sky through it: user, 2026-11-19) */
        double t0e = t0 + C3_Lift(ra, k, h, side, 0), t1e = t1 + C3_Lift(ra, k + 1, h, side, 0), t0x = t0 + C3_Lift(ra, k, h, side, 1), t1x = t1 + C3_Lift(ra, k + 1, h, side, 1);   /* (the editor's heights: the curb's top, the footpath's outer edge) */
        if (C3_SideBreak(ra, s0, k, h, s1, k + 1, h, side)) { C3Row rn; C3_SectionAt(ra, s1, k + 1, h, o1, &rn); t1e = rn.h[side ? 9 : 5]; t1x = rn.h[side ? 10 : 4]; }   /* (the next piece's side another kind
                                                                          - terrain: the footpath down / up to its edge's heights, no step between them; user's note 2026-11-18) */
        int curb = RX_SideTex(ra, s0, k, h, side, 2), walk = RX_SideTex(ra, s0, k, h, side, 1), fac = RX_SideTex(ra, s0, k, h, side, 0);
        { int o = C3_CellTex(ra, k, h, side, 0); if (o >= 0) walk = o; if ((o = C3_CellTex(ra, k, h, side, 5)) >= 0) curb = o; }   /* (its road side / road edge painted:
                                                                                                                                   the editor's cells 0 / 5) */
        double fh = fac >= 0 ? RX_FacadeHeight(ra, s0, k, h, side) : 0;
        C3_FacadeVary(ra, k, h, side, &fac, &fh); if (fac >= 0) C3_FacPick(ra, k, h, side, &fac);   /* (the editor's facade) */
        { int m0 = c->nv; C3_Span(c, k, e0, -gdp, e0 + sgn * rl, t0e, e1, -gdp, e1 + sgn * rl, t1e, C3_Colour(M_CURB, 0, k), curb, 1); C3_Mark(c, m0, 0xf7); }   /* (rolled: its top rl out) */   /* (vertex alpha 247: the curb, 246: the sidewalk - render3d.h, the improved street textures) */
        {   /* the footpath: concrete slabs (vertex alpha 246) or, a block of ~24 pieces at a time, glossy tiles (242) - render3d.h; painted SLABS / TILES (the Course
               Editor's Footpaths list, ground_art.h): that one */
            unsigned bh = ((unsigned)(k / 24) * 2654435761u + (unsigned)side * 40503u) >> 16; int tiles = (bh & 3) == 1 || (bh & 3) == 2;
            if (walk >= 0 && walk < ra->ntex && ra->tex[walk].gen) { if (!strcmp(ra->tex[walk].name, "TILES")) tiles = 1; else if (!strcmp(ra->tex[walk].name, "SLABS")) tiles = 0; }
            int m0 = c->nv; C3_Span(c, k, e0 + sgn * rl, t0e, x0, t0x, e1 + sgn * rl, t1e, x1, t1x, C3_Colour(M_WALK, 0, k), walk, 0); C3_Mark(c, m0, tiles ? 0xf2 : 0xf6); }
        {   int g = ra->ext && k < ra->next ? ra->ext[k].gutter[h & 1][side] & 3 : 0; if (g != 2) C3_Gutter(c, k, side, e0, e1, t0e, t1e, 0, gw, gdp); }   /* (RrcExtra.gutter 2: none) */
        {   double pc = 0, f = C3_Corner(c, k, h, side, s0, e0, x0, t0x, e1, x1, t1x, ra, &pc), pa = k, pb = k + 1, xa = x0, xb = x1, ta = t0x, tb = t1x;
            int corner[2] = { 0, 0 };
            if (f > 0) {   /* the building stops short of the cross street by the corner's depth */
                if (pc == k) { pa = k + f; corner[0] = 1; xa = x0 + (x1 - x0) * f; ta = t0x + (t1x - t0x) * f; }
                else { pb = k + 1 - f; corner[1] = 1; xb = x0 + (x1 - x0) * (1 - f); tb = t0x + (t1x - t0x) * (1 - f); }
            }
            if (fac >= 0 && fh > 0 && pb - pa > 1e-3) C3_Building(c, k, h, side, pa, pb, xa, xb, ta, tb, fh, fac, ra, corner);
        }
        }
    }
}

/* the section's roadside sprites (RoadSection.sprite[], rr_ScenerySpriteDraw) as upright camera-facing billboards: 2 world units per texel (the original
   draws texel steps of 8 * scale / 4), the anchor (foot point, in texels) on the ground at the sprite's lateral position, halfway along the segment */
/* (cut = 1: only the part above the anchor row - a tree in a pit: what the picture has under its ground line, the foot of the trunk and the painted
   shadow, would show over the pit, billboards being drawn over the ground in front of them) */
static void C3_BillboardEx(C3Course *c, double p, double x, double ground, int t, int ax, int ay, float unit, int mirror, const RxAssets *ra, int cut);
static void C3_Billboard(C3Course *c, double p, double x, double ground, int t, int ax, int ay, float unit, int mirror, const RxAssets *ra) { C3_BillboardEx(c, p, x, ground, t, ax, ay, unit, mirror, ra, 0); }
static void C3_BillboardEx(C3Course *c, double p, double x, double ground, int t, int ax, int ay, float unit, int mirror, const RxAssets *ra, int cut)
{
    float pt[3], x0, x1, u0, u1, top, bot, vb = 1.0f; const RxTex *tx = &ra->tex[t]; int j;
    static const int o6[6] = { 0, 3, 1, 1, 3, 2 };
    C3_Point(c, p, x, ground, pt);
    if (mirror) { x0 = -unit * (tx->w - ax); x1 = unit * ax; u0 = 1; u1 = 0; }   /* mirrored: drawn leftwards from the anchor */
    else { x0 = -unit * ax; x1 = unit * (tx->w - ax); u0 = 0; u1 = 1; }
    top = unit * ay; bot = unit * (ay - tx->h);
    if (cut && ay > 0 && ay < tx->h) { bot = 0.01f; vb = (float)ay / tx->h; }
    for (j = 0; j < 6; j++) {   /* corners 0 = bottom left, 1 = bottom right, 2 = top right, 3 = top left */
        int q = o6[j]; C3Vert *v;
        C3_Push(c, pt, 0xffffffffu);
        v = &c->v[c->nv - 1];
        v->tex = (float)t;
        v->ox = (q == 0 || q == 3) ? x0 : x1; v->oy = (q < 2) ? bot : top;
        v->u = (q == 0 || q == 3) ? u0 : u1; v->v = (q < 2) ? vb : 0.0f;
    }
}
/* height of the 3D ground (the cross-section mesh) at lateral x halfway along piece k, half h: the sprites stand on it (the game's own ground height
   +0x8c / +0x4c is taken under its squeezed view and the mesh interpolates between profile points, so a bush could sink into the hillside) */
static double C3_RowHeight(const C3Row *r, double x)
{
    int i; double best = -1e30;
    for (i = 0; i < C3_NP - 1; i++) {
        double a = r->u[i], b = r->u[i + 1], lo = a < b ? a : b, hi = a < b ? b : a, y;
        if (x < lo || x > hi) continue;
        y = hi > lo ? r->h[i] + (r->h[i + 1] - r->h[i]) * (x - a) / (b - a) : (r->h[i] > r->h[i + 1] ? r->h[i] : r->h[i + 1]);
        if (y > best) best = y;
    }
    return best > -1e29 ? best : 0;
}
static void C3_InnerClamp(const C3Course *c, int k, int h, C3Row *r);   /* below */
static double C3_GroundAt(C3Course *c, int k, int h, double x)
{
    C3Row r0, r1; int k1 = k + 1 < c->n ? k + 1 : k;
    const RoadPiece *p0 = &c->piece[k], *p1 = &c->piece[k1];
    const RoadSection *s0 = h ? &p0->halfB : &p0->halfA, *s1 = h ? (p1->layout == 2 ? &p1->halfB : &p1->halfA) : &p1->halfA;
    C3_SectionAt(c->skRa, s0, k, h, 0, &r0); C3_SectionAt(c->skRa, s1, k1, h, 0, &r1);   /* (half B: in its own frames; each side its own kind) */
    if (p0->layout == 2) { C3_InnerClamp(c, k, h, &r0); if (p1->layout == 2) C3_InnerClamp(c, k1, h, &r1); }   /* (as the mesh: beside a fork / join the inner side is
                                                                                                         the flat median, not the hillside - objects there stood in it) */
    return (C3_RowHeight(&r0, x) + C3_RowHeight(&r1, x)) * 0.5;
}

/* a sprite's lateral position: on wall pieces (kinds 1..3) not beyond the wall foot - the game draws its sprites over the cliff, in 3D the rock
   would cover one standing inside its slope */
static double C3_SpriteX(const RoadSection *s, double x)
{
    if (s->kind >= SEC_WALL1 && s->kind <= SEC_WALL_SPLIT) {
        if (s->wall[2] > 0 && x < s->wall[0]) x = s->wall[0];
        if (s->wall[5] > 0 && x > s->wall[3]) x = s->wall[3];
    }
    return x;
}
/* a tree or bush standing on a city sidewalk (user: "like a normal city would have"): a small tree pit at the curb, the tree moved into it - a square
   of dark soil a little below a raised concrete rim, the rest of the footpath free for the pedestrians.  p = piece position, x = lateral (the pit's
   middle), half = half its side, g = the sidewalk's height there.  Lateral units; along the road p +- size / L */
#define C3_RIM 6.0     /* the rim's width (a footpath is ~250 units wide, 20 high) */
static void C3_PitTop(C3Course *c, double p0, double p1, double x0, double x1, double y, unsigned rgba)   /* a level quad, facing up */
{   float a[3], b[3], cc[3], d[3];
    C3_Point(c, p0, x0, y, a); C3_Point(c, p0, x1, y, b); C3_Point(c, p1, x1, y, cc); C3_Point(c, p1, x0, y, d);
    C3_Quad(c, a, b, cc, d, rgba); }
static void C3_PitFace(C3Course *c, double pa, double xa, double pb, double xb, double y0, double y1, unsigned rgba)   /* an upright face, both ways round */
{   float a[3], b[3], cc[3], d[3];
    C3_Point(c, pa, xa, y0, a); C3_Point(c, pa, xa, y1, b); C3_Point(c, pb, xb, y1, cc); C3_Point(c, pb, xb, y0, d);
    C3_Quad(c, a, b, cc, d, rgba); C3_Quad(c, d, cc, b, a, rgba); }
static void C3_TreePit(C3Course *c, double p, double x, double half, double g)
{
    double dp = half / c->L, rp = C3_RIM / c->L, h = 3, pa = p - dp - rp, pb = p + dp + rp, xa = x - half - C3_RIM, xb = x + half + C3_RIM;
    unsigned rim = 0xff8c9496u, soil = 0xff1a2836u, mulch = 0xff1e3448u;   /* (ABGR: pale concrete, dark soil, bark mulch) */
    C3_PitTop(c, pa, pb, xa, x - half, g + h, rim); C3_PitTop(c, pa, pb, x + half, xb, g + h, rim);   /* the rim: left and right bars, then front and back */
    C3_PitTop(c, pa, p - dp, x - half, x + half, g + h, rim); C3_PitTop(c, p + dp, pb, x - half, x + half, g + h, rim);
    C3_PitFace(c, pa, xa, pb, xa, g, g + h, rim); C3_PitFace(c, pa, xb, pb, xb, g, g + h, rim);   /* its outer sides down to the paving */
    C3_PitFace(c, pa, xa, pa, xb, g, g + h, rim); C3_PitFace(c, pb, xa, pb, xb, g, g + h, rim);
    C3_PitTop(c, p - dp, p + dp, x - half, x + half, g + 1.2, soil);   /* the soil, sunk below the rim, a patch of mulch round the trunk */
    C3_PitTop(c, p - dp * 0.45, p + dp * 0.45, x - half * 0.45, x + half * 0.45, g + 1.5, mulch);
}
/* the colour of a tree picture's foliage (ABGR): the mean of its leafy texels - opaque, not dark (trunk, painted shadow), not grey */
static unsigned C3_LeafColour(const RxAssets *ra, int t)
{
    const RxTex *tx = &ra->tex[t]; unsigned *px = RX_Rgba(ra, tx); double r = 0, g = 0, b = 0, n = 0; int i;
    if (px) for (i = 0; i < tx->w * tx->h; i++) {
        unsigned q = px[i]; int cr = q & 0xff, cg = (q >> 8) & 0xff, cb = (q >> 16) & 0xff, mx = cr > cg ? (cr > cb ? cr : cb) : (cg > cb ? cg : cb), mn = cr < cg ? (cr < cb ? cr : cb) : (cg < cb ? cg : cb);
        if ((q >> 24) < 128 || cr + cg + cb < 90 || mx - mn < 18) continue;
        r += cr; g += cg; b += cb; n++; }
    free(px);
    if (n < 8) return 0xff2c6a3au;   /* (none: a leaf green) */
    return 0xff000000u | (unsigned)(b / n) << 16 | (unsigned)(g / n) << 8 | (unsigned)(r / n);
}
/* fallen leaves round a tree pit (user: "randomly strewed around like how they were dropped ... a bit into the gutter, the tree's colour"): small flat
   leaf-shaped diamonds, most on the paving and the pit round the tree, thinning out with distance, some blown over the curb into the gutter's line.
   p, x, half, g as C3_TreePit; curb = the curb's lateral position (the road beyond it), col = the foliage colour (C3_LeafColour) */
static void C3_Leaves(C3Course *c, double p, double x, double half, double g, double curb, unsigned col)
{
    unsigned s = (unsigned)(int)(p * 977.0) * 2654435761u ^ (unsigned)(int)(x * 13.0) * 40503u; int i, n = 70;
    double dir = curb > x ? 1 : -1, rimO = half + C3_RIM;
    if (c->noGround) return;   /* (True 3D detail > Ground detail off) */
    if (getenv("RR_W3LOGPIT")) { static int m; if (m++ < 40) LOG("w3leaves piece %.1f x %.0f curb %.0f colour %08x", p, x, curb, col); }   /* (RR_W3LOGPIT=1: where the pit trees are) */
#define C3_RND() ((s = s * 1664525u + 1013904223u) >> 8) / 16777216.0
    for (i = 0; i < n; i++) {
        double lx, lz, y, ang = C3_RND() * 6.2832, len = 18 + C3_RND() * 12, wid = len * (0.45 + C3_RND() * 0.15), ca, sa, k = 0.72 + C3_RND() * 0.42, dry = C3_RND() < 0.22 ? 0.45 : 0;
        float pt[4][3]; double ox[4], oz[4]; int j; unsigned r, gg, b;
        if (i < 20) {   /* in the gutter: along the curb's foot, spread further along the road, a drift close in */
            double d = C3_RND(); lx = curb + dir * (2 + d * d * 34); lz = (C3_RND() - 0.5) * 2 * (half * 3.2); y = 1.0 - C3_GUTD * (1 - (2 + d * d * 34) / C3_GUTW);   /* (down in the gutter's slope) */
        } else {   /* on the paving / the pit: round the trunk, denser near it */
            double rr = (0.25 + C3_RND() * C3_RND() * 2.2) * rimO, a = C3_RND() * 6.2832;
            lx = x + cos(a) * rr; lz = sin(a) * rr;
            if ((lx - curb) * dir > -2) lx = curb - dir * 2;   /* (not over the curb's edge) */
            y = (fabs(lx - x) < half && fabs(lz) < half) ? g + 1.7 : (fabs(lx - x) < rimO && fabs(lz) < rimO) ? g + 3.3 : g + 0.4;
        }
        ca = cos(ang); sa = sin(ang);   /* the diamond: tip, side, tip, side; wound like C3_PitTop's quads (facing up) */
        ox[0] = -ca * len * 0.5; oz[0] = -sa * len * 0.5; ox[2] = -ox[0]; oz[2] = -oz[0];
        ox[1] = sa * wid * 0.5; oz[1] = -ca * wid * 0.5; ox[3] = -ox[1]; oz[3] = -oz[1];
        if ((ox[1] - ox[0]) * (oz[3] - oz[0]) - (oz[1] - oz[0]) * (ox[3] - ox[0]) < 0) { double tx = ox[1], tz = oz[1]; ox[1] = ox[3]; oz[1] = oz[3]; ox[3] = tx; oz[3] = tz; }
        { double curl = 3 + C3_RND() * 7, ly[4]; ly[0] = curl; ly[1] = curl * 0.45; ly[2] = curl * 0.15; ly[3] = curl * 0.45;   /* (curled, as a dry leaf lies: one tip up - flat, at the camera's low angle, a leaf is a sliver under a pixel high) */
          for (j = 0; j < 4; j++) C3_Point(c, p + (lz + oz[j]) / c->L, lx + ox[j], y + ly[j], pt[j]); }
        r = col & 0xff; gg = (col >> 8) & 0xff; b = (col >> 16) & 0xff;   /* the tree's colour, lighter or darker, some leaves dried towards brown */
        r = (unsigned)((r * (1 - dry) + 0x8a * dry) * k); gg = (unsigned)((gg * (1 - dry) + 0x62 * dry) * k); b = (unsigned)((b * (1 - dry) + 0x30 * dry) * k);
        if (r > 255) r = 255; if (gg > 255) gg = 255; if (b > 255) b = 255;
        C3_Quad(c, pt[0], pt[1], pt[2], pt[3], 0xff000000u | b << 16 | gg << 8 | r);
    }
#undef C3_RND
}
/* a tree at lateral x (the section's own frame) beyond the curb of a town piece with the track's textures (not a cross street): 1 = it goes in a pit at
   the curb, *px = the pit's middle (the tree moved there), *half = half the pit's side (small: ~92 units on a 250-unit footpath, at most 104), *gy = the
   sidewalk's height (the pit and the tree stand on it) */
static int C3_PitAtCurb(const RoadSection *s, const RxAssets *ra, double x, double *px, double *half, double *gy, double *curb)
{
    double el = s->laneEdgeL > s->edgeL ? s->laneEdgeL : s->edgeL, er = s->laneEdgeR < s->edgeR ? s->laneEdgeR : s->edgeR, wl = el - s->outerL, wr = s->outerR - er, w;
    if (getenv("RR_W3LOGPIT")) { static int n; if (n++ < 16) LOG("w3pit x %.0f el %.0f er %.0f outer %.0f..%.0f kind %d flags %x shoulder %d %d L-side %d", x, el, er, (double)s->outerL, (double)s->outerR, s->kind, s->kindFlags, (int)s->shoulderH[0], (int)s->shoulderH[1], x < el); }   /* (test switch RR_W3LOGPIT=1: the first street trees' numbers) */
    if (!ra || s->kind != SEC_OPEN || (s->kindFlags & 0x20)) return 0;
    if (x <= el - 5) { w = wl; *gy = s->shoulderH[0]; } else if (x >= er + 5) { w = wr; *gy = s->shoulderH[1]; } else return 0;   /* (beyond the curb - on the sidewalk or further out in the building line: to the curb; the pit on the paving, at the sidewalk's own height) */
    *half = w * 0.184 < 52 ? w * 0.184 : 52; if (*half < 15) return 0;   /* (a pit ~92 units square on a 250-unit footpath) */
    *curb = x < el ? el : er; *px = x < el ? el - 10 - C3_RIM - *half : er + 10 + C3_RIM + *half;   /* (10 units of kerb left free in front of it) */
    return 1;
}
/* the road patches the game draws as standing pictures (user: "water should just be a puddle, oil an oil effect"): 1 = oil (OILP*), 2 = water (WATP*,
   WTRP*), 3 = ice (ICEPD*), 4 = gravel / sand (GRVP*, GRAVPL*, SNDP*); 0 = something else */
static int C3_PatchKind(const RxAssets *ra, int t)
{   const char *n = ra->tex[t].name;
    if (!strncmp(n, "OILP", 4)) return 1;
    if (!strncmp(n, "WATP", 4) || !strncmp(n, "WTRP", 4)) return 2;
    if (!strncmp(n, "ICEPD", 5)) return 3;
    if (!strncmp(n, "GRVP", 4) || !strncmp(n, "GRAVPL", 6) || !strncmp(n, "SNDP", 4)) return 4;
    return 0; }
/* such a patch laid flat on the ground instead: the picture's outline (its alpha) as the shape, as wide as the picture, 0.8 of that along the road,
   centred where the picture stood; oil / water / ice marked for the shader (vertex alpha 245 / 244 / 243, render3d.h: an oil film, a puddle, ice) */
static void C3_Patch(C3Course *c, double p, double x, double g, int t, int ax, float unit, int kind, const RxAssets *ra)
{
    const RxTex *tx = &ra->tex[t]; double w = unit * tx->w, cx = x + unit * (tx->w * 0.5 - ax), dp = w * 0.4 / c->L, y = g + 3;
    float a[3], b[3], cc[3], d[3], uv[8] = { 0, 0, 1, 0, 1, 1, 0, 1 }; int m0 = c->nv;
    C3_Point(c, p - dp, cx - w * 0.5, y, a); C3_Point(c, p - dp, cx + w * 0.5, y, b); C3_Point(c, p + dp, cx + w * 0.5, y, cc); C3_Point(c, p + dp, cx - w * 0.5, y, d);
    C3_QuadT(c, a, b, cc, d, 0xffffffffu, t, uv);
    if (kind <= 3) C3_Mark(c, m0, 0xf6 - kind);
}
static int C3_IsTree(const RxAssets *ra, int t) { const char *n = ra->tex[t].name; return !strncmp(n, "TREE", 4) || !strncmp(n, "BUSH", 4) || !strncmp(n, "BSH", 3); }
static void C3_Sprites(C3Course *c, int k, int h, const RoadSection *s, double o, RxAssets *ra)
{
    int i, ax, ay, t, fb[2]; unsigned col = 0;   /* (the row's tree colour, C3_LeafColour: once) */
    double fx[2];   /* (buildings on a terrain side: their front - nothing stands behind it, C3_SideBld) */
    for (i = 0; i < 2; i++) { fb[i] = C3_SideBld(ra, s, k, h, i); fx[i] = fb[i] ? (fb[i] == 4 ? (i ? s->outerR : s->outerL) : s->profile[((fb[i] - 1) * 2 + i) * 2]) + o + (i ? -100 : 100) : 0; }
#define C3_INBLDG(gx) ((fb[0] && (gx) < fx[0]) || (fb[1] && (gx) > fx[1]))
    for (i = 0; i < s->spriteN && i < 2; i++)   /* the section's own sprites (rr_MixChannelRefresh, kind 1): texel = 8 / 4 units */
        if ((t = RX_NarrTex(ra, k, h, s->sprite[i].code, 4, 1, &ax, &ay)) >= 0 && !C3_INBLDG(C3_SpriteX(s, s->sprite[i].x) + o))
            { double gx = C3_SpriteX(s, s->sprite[i].x) + o, gh = C3_GroundAt(c, k, h, gx), px, hp; if (gh < s->sprite[i].h) gh = s->sprite[i].h;
              double cx; int pit = C3_IsTree(ra, t) && C3_PitAtCurb(s, ra, s->sprite[i].x, &px, &hp, &gh, &cx); if (pit) { gx = px + o; C3_TreePit(c, k + 0.5, gx, hp, gh); C3_Leaves(c, k + 0.5, gx, hp, gh, cx + o, C3_LeafColour(ra, t)); }
              if (C3_PatchKind(ra, t)) C3_Patch(c, k + 0.5, gx, gh, t, ax, 2.0f, C3_PatchKind(ra, t), ra);   /* (a road patch: flat) */
              else C3_BillboardEx(c, k + 0.5, gx, gh, t, ax, ay, 2.0f, s->sprite[i].flags & 1, ra, pit); }   /* (in a pit: cut at its ground line) */
    if (s->rowN && (s->rowCode & 0x3f) != 0x3f && (t = RX_NarrTex(ra, k, h, s->rowCode, 0, 0, &ax, &ay)) >= 0)   /* the object row (rr_OwnerEntriesInRange, kind 0) */
        if (!(C3_IsCross(c, k, h) && (strstr(ra->tex[t].name, "LMP") || strstr(ra->tex[t].name, "LAMP"))))   /* no lamp posts standing in a cross street (City piece 84: PRKGLMP2) */
        for (i = 0; i < s->rowN && i < 8; i++)
            { double gx = C3_SpriteX(s, s->rowX[i]) + o, gh = C3_GroundAt(c, k, h, gx), g0 = s->rowH[i] > 0x3e6 ? 0 : s->rowH[i], px, hp; if (gh < g0) gh = g0;
              double pp = k + 0.5, cx; int pit = 0;
              if (C3_INBLDG(gx)) continue;
              if (C3_IsTree(ra, t) && C3_PitAtCurb(s, ra, s->rowX[i], &px, &hp, &gh, &cx)) { pit = 1; gx = px + o; pp = k + 0.5 + (i % 3 - 1) * 0.3; C3_TreePit(c, pp, gx, hp, gh); C3_Leaves(c, pp, gx, hp, gh, cx + o, col ? col : (col = C3_LeafColour(ra, t))); }   /* (several in a row: spread along the kerb, not stacked) */
              if (C3_PatchKind(ra, t)) { C3_Patch(c, k + 0.5, gx, gh, t, ax, s->rowScale * 0.25f, C3_PatchKind(ra, t), ra); continue; }   /* (a road patch: flat) */
              C3_BillboardEx(c, pp, gx, gh, t, ax, ay, s->rowScale * 0.25f, s->rowFlags & 1, ra, pit); }   /* (in a pit: cut at its ground line) */
#undef C3_INBLDG
}

/* side structures of kindArg[0] == 5 pieces (rr_AiZoneActions -> GroundMarkQueue, per side when bit 1 / 2 of kindArg[3]): a vertical wall parallel to the
   road at (signed) kindArg[2] beyond the outer edge, from road level up to the group's height, textured from set category 6 entry kindArg[1] */
static void C3_SideStructs(C3Course *c, int k, int h, const RoadSection *s0, double o0, const RoadSection *s1, double o1, RxAssets *ra)
{
    int side, height, t;
    if (s0->kindArg[0] != 5 || !(s0->kindArg[3] & 3)) return;
    if ((t = RX_SideStructTex(ra, s0, k, h, &height)) < 0 || height <= 0) return;
    for (side = 0; side < 2; side++) {
        double off = (signed char)s0->kindArg[2], x0, x1;
        if (!(s0->kindArg[3] & (1 << side))) continue;
        if (c->forkPick && c->piece[k].layout == 2 && side == 1) continue;   /* forks off: no rail on A's inner (median) side */
        if (side == 0) off = -off;
        x0 = (side ? s0->outerR : s0->outerL) + off + o0; x1 = (side ? s1->outerR : s1->outerL) + off + o1;        C3_Span(c, k, x0, 0, x0, height, x1, 0, x1, height, 0xffffffffu, t, 1);
    }
}

/* the sea beyond a drop (walls with a negative height: Pacific Highway's coast).  The game fills the screen between the coast and the horizon with its
   sea texture (RoadEdgeWalk, g_seaObject = TEXG 32); here the sea surface runs C3_SEA units out from the foot of the drop's slope (C3_Side), the
   texture tiled every 1024 units */
#define C3_SEA 40000.0
static void C3_Sea(C3Course *c, int k, const RoadSection *s0, double o0, const RoadSection *s1, double o1, RxAssets *ra)
{
    int side;
    for (side = 0; side < 2; side++) {
        const int *w1 = s1->wall + side * 3;
        int drop1 = s1->kind >= SEC_WALL1 && s1->kind <= SEC_WALL_SPLIT && w1[2] < 0;   /* the next piece a drop too (else: this piece's values) */
        double sg = side ? 1 : -1, u0[7], h0[7], u1[7], h1[7], d0, d1, e0, e1, f0, f1; int m[6];
        int sea = ra->global[32];
        float a[3], b[3], cc[3], d[3], uv[8]; float rep = (float)(C3_SEA / 1024.0);
        if (s0->wall[side * 3 + 2] >= 0) continue;
        C3_Side(s0, side ? 1 : -1, s0->kind, 0, u0, h0, m); C3_Side(drop1 ? s1 : s0, side ? 1 : -1, (drop1 ? s1 : s0)->kind, 0, u1, h1, m);
        d0 = h0[6]; d1 = h1[6]; e0 = u0[6] + o0; e1 = u1[6] + o1; f0 = e0 + sg * C3_SEA; f1 = e1 + sg * C3_SEA;
        C3_Point(c, k, side ? e0 : f0, d0, a); C3_Point(c, k, side ? f0 : e0, d0, b); C3_Point(c, k + 1, side ? f1 : e1, d1, cc); C3_Point(c, k + 1, side ? e1 : f1, d1, d);
        uv[0] = 0; uv[1] = 1; uv[2] = rep; uv[3] = 1; uv[4] = rep; uv[5] = 0; uv[6] = 0; uv[7] = 0;
        C3_QuadTint(c, a, b, cc, d, 0xfd8a5a2au, sea, uv, 0xfdffffffu);   /* (vertex alpha 253: water - the world renderer draws a water surface there, render3d.h) */
    }
}
/* painted water past P2 (C3_WaterRow): from its far end on out C3_SEA at that height - the sea of Pacific Highway (C3_Sea) */
static void C3_Water(C3Course *c, int k, const C3Row *r0, const C3Row *r1, const RxAssets *ra)
{
    int side;
    for (side = 0; side < 2; side++) if ((r0->wm[side] | r1->wm[side]) & 16) {
        float P[2][2][3], uv[8], a0[3], b0[3], a1[3], b1[3]; int rr;
        for (rr = 0; rr < 2; rr++) { const C3Row *R = rr ? r1 : r0; double u = R->u[side ? 14 : 0];
            C3_Point(c, k + rr, u, R->wl[side], P[rr][0]); C3_Point(c, k + rr, u + (side ? C3_SEA : -C3_SEA), R->wl[side], P[rr][1]); }
        uv[0] = 0; uv[1] = 1; uv[2] = 8; uv[3] = 1; uv[4] = 8; uv[5] = 0; uv[6] = 0; uv[7] = 0;
        if (side) { memcpy(a0, P[0][0], 12); memcpy(b0, P[0][1], 12); memcpy(b1, P[1][1], 12); memcpy(a1, P[1][0], 12); }   /* (as C3_Sea: the smaller u first) */
        else { memcpy(a0, P[0][1], 12); memcpy(b0, P[0][0], 12); memcpy(b1, P[1][0], 12); memcpy(a1, P[1][1], 12); }
        C3_QuadTint(c, a0, b0, b1, a1, 0xfd8a5a2au, ra ? ra->ground0 + C3_GWATER : -1, uv, 0xfdffffffu); }
}

/* the water in a hole (user, 2026-11-19): over each side's hole cells, at the row's water level (C3_HoleRow: hl), from the hole's inner edge to its outer - the sea's
   water (vertex alpha 253); a row without the hole: the other row's */
static void C3_HoleWater(C3Course *c, int k, const C3Row *r0, const C3Row *r1, const RxAssets *ra)
{
    int side;
    for (side = 0; side < 2; side++) { unsigned m = (unsigned)(r0->hm[side] | r1->hm[side]); int c0 = 0, c1 = 4, rr; float P[2][2][3], uv[8], a0[3], b0[3], a1[3], b1[3];
        if (!m) continue;
        while (c0 < 4 && !(m & (1u << c0))) c0++;
        while (c1 > 0 && !(m & (1u << c1))) c1--;
        for (rr = 0; rr < 2; rr++) { const C3Row *R = rr ? r1 : r0, *O = rr ? r0 : r1; double lv = R->hm[side] ? R->hl[side] : O->hl[side], ui = R->u[side ? 8 + c0 + 1 : 6 - (c0 + 1)], uo = R->u[side ? 8 + c1 + 2 : 6 - (c1 + 2)];
            if (c1 == 4) uo += side ? C3_SEA : -C3_SEA;   /* (the hole on to the horizon: the water too) */
            C3_Point(c, k + rr, ui, lv, P[rr][0]); C3_Point(c, k + rr, uo, lv, P[rr][1]); }
        uv[0] = 0; uv[1] = 1; uv[2] = 8; uv[3] = 1; uv[4] = 8; uv[5] = 0; uv[6] = 0; uv[7] = 0;
        if (side) { memcpy(a0, P[0][0], 12); memcpy(b0, P[0][1], 12); memcpy(b1, P[1][1], 12); memcpy(a1, P[1][0], 12); }
        else { memcpy(a0, P[0][1], 12); memcpy(b0, P[0][0], 12); memcpy(b1, P[1][0], 12); memcpy(a1, P[1][1], 12); }
        C3_QuadTint(c, a0, b0, b1, a1, 0xfd8a5a2au, ra ? ra->ground0 + C3_GWATER : -1, uv, 0xfdffffffu); }
}
/* split roads: the two carriageways have their own paths and hills (C3_FramesB), which can cross over or under each other far from the fork (Sierra
   ~1450: A overhead; Pacific Highway ~1500: A's terrain as a ceiling).  The original only shows the other carriageway while it is near (its view is
   one carriageway wide plus the shear offset).  B's closed path weaves around A (Pacific Highway 1560..1680: centre lines 350..5600 apart, heights
   +-500), so no distance test separates them.  With c->hide = 1 + h, half h of piece k is left out except within C3_SEPK pieces of the split's
   start or end, where the carriageways run side by side */
#define C3_EDGELINE 24.0   /* width of the solid white edge line beside the fork's dark shoulders (C3_BuildRange) */
#define C3_BRIDGE 6000.0   /* centre lines closer than this: one median surface between the carriageways (C3_Median) */
#define C3_DARKGAP 2500.0  /* forks on: the dark shoulders under the inner rails only where the centre lines are closer than this (C3_BuildRange) */
#define C3_JOINFILL 600.0 /* forks on (C3_SoftFrames): outer edges closer than this - the median is road surface (C3_Median) */
static double C3_Gap(const C3Course *c, int k, int h)   /* distance of the two centre lines beside split piece k of half h (1e30 = unknown) */
{
    int ka = h && c->pairA ? c->pairA[k] : k, kb = !h && c->pairB ? c->pairB[k] : k; double dx, dz;
    if (!c->frameB || ka > c->n || kb > c->n || (c->bdead && c->bdead[kb])) return 1e30;
    dx = c->frame[ka].x - c->frameB[kb].x; dz = c->frame[ka].z - c->frameB[kb].z;
    return sqrt(dx * dx + dz * dz);
}
static int C3_NearForkJoin(const C3Course *c, int k, int h)   /* split piece near the fork or the join (B: of its own, earlier end, C3_BTail) or with the roads close: both carriageways shown */
{
    if (k < 0 || k >= c->n || c->piece[k].layout != 2 || (h && c->bdead && c->bdead[k]) || c->forkPick) return 0;
    if (c->soft) return c->other && c->other[k] && C3_Gap(c, k, h) < C3_BRIDGE;   /* (the original's way: wherever both are built and close) */
    if (!(k - C3_SEPK < 0 || c->piece[k - C3_SEPK].layout != 2 || k + C3_SEPK >= c->n || c->piece[k + C3_SEPK].layout != 2 || (h && c->bdead && c->bdead[k + C3_SEPK]))) return 0;
    return C3_Gap(c, k, h) < C3_BRIDGE;   /* (and still close: never where the made-up B path passes A far from the fork - user: once the fork is gone, cull the other road) */
}
static int C3_HideHalf(const C3Course *c, int k, int h)
{
    if (c->forkPick && c->piece[k].layout == 2) return h;   /* forks off: B is never built */
    if (c->soft && c->piece[k].layout == 2 && k < c->camK - 1) return 1;   /* (like the original's view, nothing of a split behind the camera: after the join
                                                                                the other road's last pieces put their hillside across the view) */
    if (c->soft && c->piece[k].layout == 2 && c->hide == 1 + h) return !(c->other && c->other[k]) || (h && c->bdead && c->bdead[k]);   /* (C3_SoftFrames) */
    return (h && c->bdead && c->bdead[k]) || (c->hide == 1 + h && c->piece[k].layout == 2 && !C3_NearForkJoin(c, k, h));   /* (B's phantom tail: never) */
}

/* where both carriageways are shown, each one's inner side (A's right, B's left: B always lies right of A, its lanes 250..750 of its own centre)
   would run across the other road (Sierra 900..905: A's verge and hillside over B's lanes and B's over A's - driving through them on A).  The inner
   side ends at the road's own outer edge (verge kept, terrain collapsed onto it); the ground between the two carriageways is one surface, C3_Median */
static void C3_InnerClamp(const C3Course *c, int k, int h, C3Row *r)
{
    int i;
    if (!C3_NearForkJoin(c, k, h) || !c->frameB) return;
    if (!h) { for (i = 11; i < C3_NP; i++) { r->u[i] = r->u[10]; r->h[i] = r->h[10]; } }   /* (row 10 = A's right outer edge) */
    else { for (i = 0; i < 4; i++) { r->u[i] = r->u[4]; r->h[i] = r->h[4]; } }             /* (row 4 = B's left outer edge) */
}
/* the median between the carriageways where both are shown (built with half A, piece k): one strip from A's right outer edge to B's left outer edge
   (the piece beside it, c->pairB), its height going linearly from one to the other, in A's inner terrain texture tiled in world size - instead of two
   hillsides meeting in a ridge or a step */
static void C3_Median(C3Course *c, int k, RxAssets *ra)
{
    int k1 = k + 1, b0, b1, tex[C3_NP - 1], t; C3Row a0, a1, r0, r1; float p[4][3], uv[8]; double per = 1024, wa, wb;
    if (!c->pairB || k1 >= c->n || c->piece[k1].layout != 2) return;
    b0 = c->pairB[k]; b1 = c->pairB[k1];
    if (b0 > c->n || b1 > c->n || (c->bdead && (c->bdead[b0] || c->bdead[b1]))) return;
    C3_Section(&c->piece[k].halfA, 0, &a0); C3_Section(&c->piece[k1].halfA, 0, &a1);
    C3_Section(&c->piece[b0].halfB, 0, &r0); C3_Section(&c->piece[b1].halfB, 0, &r1);
    C3_PointF(c, c->frame, k, a0.u[10], a0.h[10], p[0]); C3_PointF(c, c->frameB, b0, r0.u[4], r0.h[4], p[1]);
    C3_PointF(c, c->frameB, b1, r1.u[4], r1.h[4], p[2]); C3_PointF(c, c->frame, k1, a1.u[10], a1.h[10], p[3]);
    {   double th0 = c->frame[k].heading, th1 = c->frame[k1].heading;   /* the carriageways overlap (at the join, forks on: C3_SoftFrames): no ground between them */
        if ((p[1][0] - p[0][0]) * cos(th0) - (p[1][2] - p[0][2]) * sin(th0) <= 0 || (p[2][0] - p[3][0]) * cos(th1) - (p[2][2] - p[3][2]) * sin(th1) <= 0) return; }
    C3_ProfileTex(ra, &c->piece[k].halfA, k, 0, tex);
    t = tex[11] >= 0 ? tex[11] : tex[10];
    {   const RoadSection *sa = &c->piece[k].halfA, *sb = &c->piece[b0].halfB;   /* a rail on either inner side (GroundMarkQueue, kindArg[0] 5): the dark grey wedge between the
                                                                                   two rails of software (Sierra's fork), road surface darkened like c->darkStrip */
        (void)sa; if (sb->kindArg[0] == 5 && (sb->kindArg[3] & 1) && ra->global[17] >= 0) t = -2 - ra->global[17]; }
    if (c->soft && ra->global[17] >= 0 && C3_Dist(p[0], p[1]) < C3_JOINFILL && C3_Dist(p[3], p[2]) < C3_JOINFILL) {   /* (forks on: the roads about to meet - the original
                                                                                                    paints the narrow gap as road, its merge wedge: plain road surface) */
        int td = ra->global[17]; uv[0] = 0; uv[1] = 1; uv[2] = (float)(C3_Dist(p[0], p[1]) / 250.0); uv[3] = 1; uv[4] = (float)(C3_Dist(p[3], p[2]) / 250.0); uv[5] = 0; uv[6] = 0; uv[7] = 0;
        C3_QuadTint(c, p[0], p[1], p[2], p[3], 0xffffffffu, td, uv, 0xffd0d0d0u); return; }   /* (B's inner rail: Sierra 898..903 - the wedge ends with it; A's runs on) */
    if (t <= -2) { int td = -2 - t; float q = 1.0f;
        if (td < ra->ntex && ra->tex[td].w > 0) q = (float)(C3_Dist(p[0], p[1]) / 250.0);
        uv[0] = 0; uv[1] = 1; uv[2] = q; uv[3] = 1; uv[4] = (float)(C3_Dist(p[3], p[2]) / 250.0); uv[5] = 0; uv[6] = 0; uv[7] = 0;
        C3_QuadTint(c, p[0], p[1], p[2], p[3], 0xffffffffu, td, uv, 0xff8a8a8au); return; }
    if (t >= 0 && t < ra->ntex && ra->tex[t].w > 0 && ra->tex[t].h > 0) per = ra->tex[t].w * (c->L / C3_ALONG / ra->tex[t].h);
    wa = C3_Dist(p[0], p[1]) / per; wb = C3_Dist(p[3], p[2]) / per;
    uv[0] = 0; uv[1] = C3_ALONG; uv[2] = (float)wa; uv[3] = C3_ALONG; uv[4] = (float)wb; uv[5] = 0; uv[6] = 0; uv[7] = 0;
    if (t >= 0) C3_QuadT(c, p[0], p[1], p[2], p[3], 0xffffffffu, t, uv);
    else C3_Quad(c, p[0], p[1], p[2], p[3], C3_Colour(M_TERRAIN, c->piece[k].halfA.kind, k));
}

/* forks on, the original's way (hardware mode, Draw distance = Original; the user: "match the software version look").  The original's view builder
   (rr_RoadProjectSegments) starts from the camera's piece: the camera's carriageway as it is, the other one splitX (lateral, shear space) / splitSlope (slope)
   beside it - B = A + splitX, or A = B - splitX on B - and then advances each with its own curves and hills, from the camera's height; at the join the
   road goes on from the camera's carriageway and the other one's last segment is pulled onto it (link 1).  So the other carriageway is not a fixed
   path: it is built again each frame round the camera from frames g (the camera's own: A = g->frame, B = g->frameB, C3_FramesB), anchored at the camera's
   piece kc and kc + 1 and blended by the fraction f (no jump from piece to piece).  At the join it bends onto the joined road over the last C3_SEPK
   pieces when it ends within C3_BRIDGE of it; else its last piece is left out (software's sliver across the road).  It is built only where the game's
   own ring has the split (ring[k], 1 = split there; on B the game builds B alone once past the fork) and within maxGap units of the camera's carriageway */
static void C3_SoftDerive(const C3Course *c, const C3Course *g, int camB, int ks, int ke, int a, C3Frame *d)   /* d[k - ks], k = ks .. ke; anchor piece a */
{
    const C3Frame *cf = camB ? g->frameB : g->frame; int k; double s = camB ? -1 : 1, L = c->L;
    const RoadPiece *pa = &c->piece[a];
    d[a - ks].heading = cf[a].heading + s * pa->splitSlope / L;
    d[a - ks].x = cf[a].x + s * pa->splitX * cos(cf[a].heading); d[a - ks].z = cf[a].z - s * pa->splitX * sin(cf[a].heading); d[a - ks].y = cf[a].y;
    for (k = a + 1; k <= ke; k++) {   /* (frame k's heading includes piece k's curve, C3_Frames) */
        const RoadSection *o = camB ? &c->piece[k - 1].halfA : &c->piece[k - 1].halfB; C3Frame *p = &d[k - 1 - ks], *q = &d[k - ks];
        q->x = p->x + L * sin(p->heading); q->z = p->z + L * cos(p->heading); q->y = p->y + o->hill;
        q->heading = p->heading + (k < c->n ? (camB ? c->piece[k].halfA.curve : c->piece[k].halfB.curve) : 0) / L;
    }
    for (k = a - 1; k >= ks; k--) {
        const RoadSection *o = camB ? &c->piece[k].halfA : &c->piece[k].halfB; C3Frame *p = &d[k + 1 - ks], *q = &d[k - ks];
        q->heading = p->heading - (camB ? c->piece[k + 1].halfA.curve : c->piece[k + 1].halfB.curve) / L;
        q->x = p->x - L * sin(q->heading); q->z = p->z - L * cos(q->heading); q->y = p->y - o->hill;
    }
}
static void C3_SoftFrames(C3Course *c, const C3Course *g, int camB, int kc, double f, const unsigned char *ring, double maxGap)
{
    int ks, kj, k;
    memcpy(c->frame, g->frame, sizeof(C3Frame) * (c->n + 1)); memcpy(c->frameB, g->frameB, sizeof(C3Frame) * (c->n + 1));
    memset(c->other, 0, c->n);
    c->hide = camB ? 1 : 2; c->camK = kc;
    for (ks = 0; ks < c->n; ks = kj) {
        int kb, ke, a, m; C3Frame *d0, *d1, J; double dh, mx, my, mz, miss;
        if (c->piece[ks].layout != 2) { kj = ks + 1; continue; }
        for (kj = ks; kj < c->n && c->piece[kj].layout == 2; kj++) {}
        if (kj >= c->n) break;
        for (kb = ks; kb < kj && !(c->bdead && c->bdead[kb]); kb++) {}   /* B's real end (C3_BTail) */
        ke = kb;   /* the other carriageway's pieces ks .. ke - 1, its frames ks .. ke (on B the pieces are numbered as B's: the game's ring there has A's
                      end counted back from the join, W3Live) */
        if (ke - ks < 2 || kc > ke + 64 || kc < ks - 400) continue;   /* (far from the camera: never built) */
        if (!(d0 = (C3Frame *)malloc(sizeof(C3Frame) * 2 * (ke - ks + 1)))) return;
        d1 = d0 + (ke - ks + 1);
        a = kc < ks ? ks : (kc > ke - 1 ? ke - 1 : kc);
        if (ring && !ring[a]) { for (k = a; k < ke && !ring[k]; k++) {} if (k < ke) a = k; }   /* (the camera's piece not split in the game's view: the view builder
                                                                                                   starts the other carriageway at the first split piece ahead, from its splitX) */
        C3_SoftDerive(c, g, camB, ks, ke, a, d0);
        if (a == kc && a + 1 <= ke - 1 && f > 0 && (!ring || ring[a + 1])) {   /* (between pieces: the anchors at kc and kc + 1 blended) */
            C3_SoftDerive(c, g, camB, ks, ke, a + 1, d1);
            for (k = 0; k <= ke - ks; k++) { d0[k].x += (d1[k].x - d0[k].x) * f; d0[k].y += (d1[k].y - d0[k].y) * f; d0[k].z += (d1[k].z - d0[k].z) * f; d0[k].heading += (d1[k].heading - d0[k].heading) * f; } }
        J = g->frame[kj];   /* the join: where the road goes on (the camera's carriageway ends there: g->frameB[kb] = g->frame[kj]) */
        m = (int)floor((J.heading - d0[ke - ks].heading) / (2 * C3_PI) + 0.5);   /* (B can turn whole loops more than A: the same directions as the join's) */
        for (k = 0; k <= ke - ks; k++) d0[k].heading += m * 2 * C3_PI;
        mx = J.x - d0[ke - ks].x; my = J.y - d0[ke - ks].y; mz = J.z - d0[ke - ks].z; dh = J.heading - d0[ke - ks].heading;
        miss = sqrt(mx * mx + mz * mz);
        if (miss < C3_BRIDGE) for (k = ke - C3_SEPK < ks ? ks : ke - C3_SEPK; k <= ke; k++) {   /* onto the join (link 1), smoothly */
            double t = (double)(k - (ke - C3_SEPK)) / C3_SEPK, w = C3_S(t, 0);
            d0[k - ks].x += mx * w; d0[k - ks].y += my * w; d0[k - ks].z += mz * w; d0[k - ks].heading += dh * w; }
        else d0[ke - ks] = J;   /* (its last piece is left out below) */
        for (k = ks; k <= ke; k++) {
            if (camB && (k == ks || k == kj)) continue;   /* (A's frames at the fork / the join are also the single road's ends) */
            if (camB) c->frame[k] = d0[k - ks]; else c->frameB[k] = d0[k - ks]; }
        for (k = ks; k < ke; k++) {
            double dx = c->frame[k].x - c->frameB[k].x, dz = c->frame[k].z - c->frameB[k].z, ex = c->frame[k + 1].x - c->frameB[k + 1].x, ez = c->frame[k + 1].z - c->frameB[k + 1].z;
            c->other[k] = (!ring || ring[k]) && dx * dx + dz * dz < maxGap * maxGap && ex * ex + ez * ez < maxGap * maxGap && !(k == ke - 1 && miss >= C3_BRIDGE); }
        free(d0);
    }
}

/* the piece before a fork ends on both carriageways' full width (the fork piece's A left side, B right side; at the fork B's frame is A's): else its
   lanes and right side narrow to A alone and leave a wedge open before B's road starts (Sierra 897) */
static void C3_ForkMerge(const RoadPiece *p, RoadSection *out)
{
    const RoadSection *b = &p->halfB;
    *out = p->halfA;
    out->edgeR = b->edgeR; out->laneEdgeR = b->laneEdgeR; out->outerR = b->outerR; out->extR = b->extR;
    out->shoulderW[1] = b->shoulderW[1]; out->shoulderH[1] = b->shoulderH[1]; out->sideTex[1] = b->sideTex[1]; out->sideArg[1] = b->sideArg[1];
    out->profile[2] = b->profile[2]; out->profile[3] = b->profile[3]; out->profile[6] = b->profile[6]; out->profile[7] = b->profile[7]; out->profile[10] = b->profile[10]; out->profile[11] = b->profile[11];
    out->wall[3] = b->wall[3]; out->wall[4] = b->wall[4]; out->wall[5] = b->wall[5];
}

/* the mesh of pieces k0 .. k1 - 1 (each piece reaches to the next one's start), appended; with ra (track textures) the road and roadside are textured */
/* a tunnel: between two SEC_WALL_SPLIT pieces the game draws an arch over the road (rr_GateDraw, from wheel_marks.c's wall pass) - six quads through
   the cross-section the projector builds for kind 3 (road_view.c rr_SectionProject): the outer edges at the section's base heights (shoulderH), straight up to
   256, sloping in to 1/4 and 3/4 of the lane width at 512, the ceiling between them (two halves).  The heights / fractions are the game's table at
   g_shoulderW0 .. tbl_viewInitEnd (256, 64, 512, 192, 512, 256, 256, 256 at run time).  Textures: set category 3, entry kindArg[1], row 0, column 2 =
   the walls, 1 = the slopes, 0 = the ceiling (BikeHandleTable -> BankEntry3) */
static int C3_ArchTex(RxAssets *ra, const RoadSection *s, int k, int h, int col)
{
    unsigned code = s->kindArg[1], t; RxSet *set; int rows[2]; rows[0] = 0; rows[1] = col;
    if (!ra) return -1;
    set = RX_SetFor(ra, k, h, (int)(code >> 6) & 3);
    t = RX_Lookup(set, 3, code, rows, 2);
    return t ? RX_TexIn(ra, set, t) : -1;
}
/* The mountain over a tunnel: the original draws no such thing - its projection swings the far tunnel segments' left cliff (walls kind 3: a cliff left,
   a drop to the sea right) across the picture, so the entrance looks cut into a mountain; in true 3D that cliff stays beside the road and the tube lay
   bare along the shore.  Outline per piece, left to right (lateral, height): the cliff's foot and top (wall[0..2]), over the tube at least C3_TUNNEL_ROOF
   high, down to the drop's edge (right wall[3]) at road level.  The rock cover joins it piece to piece; at the first / last tunnel piece a portal face
   (the outline against the tube's cross-section, in the cliff texture) closes the gap round the opening */
#define C3_TUNNEL_ROOF 750.0
#define C3_TUNNEL_FRAME 70.0   /* the frame round a portal's opening (lit; the rock face beyond it in shade, as the original's portal looks) */
static void C3_TunnelOutline(const RoadSection *s, double o, double *u, double *hh)
{
    double xl = s->wall[1] + o, hl = s->wall[2] > 0 ? s->wall[2] : C3_TUNNEL_ROOF, xr = (s->wall[5] < 0 ? s->wall[3] : s->wall[4]) + o, top = hl * 0.6;
    if (top < C3_TUNNEL_ROOF) top = C3_TUNNEL_ROOF;
    if (xr < s->outerR + o + 50) xr = s->outerR + o + 50;
    u[0] = s->wall[0] + o;               hh[0] = 0;              /* (the cliff face: the wall strip has it already) */
    u[1] = xl;                           hh[1] = hl;
    u[2] = (xl + s->outerL + o) * 0.5;   hh[2] = (hl + top) * 0.5;
    u[3] = (s->laneEdgeL + s->laneEdgeR) * 0.5 + o; hh[3] = top;
    u[4] = (s->outerR + o + xr) * 0.5;   hh[4] = top * 0.8;
    u[5] = xr;                           hh[5] = top * 0.35;
    u[6] = xr;                           hh[6] = 0;
}
/* a tunnel's footpath texture (rr_EdgeStripDraw with style 0 -> SlotLookupNear kind 2 / 3 -> BankEntry2(kindArg[1], 3, kind)): set category 3, entry kindArg[1],
   row 2 = the path's top (SHLDRTOP), row 3 = its curb face; rows 0 / 1 are the arch's (C3_ArchTex) */
static int C3_Shoulder(RxAssets *ra, const RoadSection *s, int k, int h, int what)
{
    unsigned code = s->kindArg[1], t; RxSet *set; int row = what == 1 ? 2 : 3;
    if (!ra) return -1;
    set = RX_SetFor(ra, k, h, (int)(code >> 6) & 3);
    t = RX_Lookup(set, 3, code, &row, 1);
    return t ? RX_TexIn(ra, set, t) : -1;
}
static void C3_Tunnel(C3Course *c, int k, int h, const RoadSection *s0, double o0, const RoadSection *s1, double o1, RxAssets *ra, int rock)
{
    static const int col[6] = { 2, 1, 0, 0, 1, 2 };
    double u0[7], h0[7], u1[7], h1[7], a0[7], b0[7], a1[7], b1[7]; int i, t, end;
    unsigned col0 = rock >= 0 ? 0xffffffffu : C3_Colour(M_WALL, s0->kind, k);
    const RoadSection *s[2]; double o[2], *u[2], *hh[2];
    C3_TunnelOutline(s0, o0, a0, b0); C3_TunnelOutline(s1, o1, a1, b1);
    {   float pa[3], pb[3], pc[3], pd[3], uv[8];
        for (i = 1; i < 6; i++) {   /* the cover: one copy per segment along, per C3_TOP across */
            double q = sqrt((a0[i + 1] - a0[i]) * (a0[i + 1] - a0[i]) + (b0[i + 1] - b0[i]) * (b0[i + 1] - b0[i])) / C3_TOP;
            C3_Point(c, k, a0[i], b0[i], pa); C3_Point(c, k, a0[i + 1], b0[i + 1], pb); C3_Point(c, k + 1, a1[i + 1], b1[i + 1], pc); C3_Point(c, k + 1, a1[i], b1[i], pd);
            uv[0] = 0; uv[1] = 0; uv[2] = 0; uv[3] = (float)q; uv[4] = 1; uv[5] = (float)q; uv[6] = 1; uv[7] = 0;
            C3_QuadT(c, pa, pb, pc, pd, col0, rock, uv);
        }
    }
    s[0] = s0; s[1] = s1; o[0] = o0; o[1] = o1; u[0] = u0; u[1] = u1; hh[0] = h0; hh[1] = h1;
    for (i = 0; i < 2; i++) {
        double w = s[i]->laneEdgeR - s[i]->laneEdgeL, l = s[i]->laneEdgeL + o[i];
        u[i][0] = s[i]->outerL + o[i]; hh[i][0] = s[i]->shoulderH[0];
        u[i][1] = s[i]->outerL + o[i]; hh[i][1] = 256;
        u[i][2] = l + w * 64 / 256;    hh[i][2] = 512;
        u[i][3] = l + w * 128 / 256;   hh[i][3] = 512;   /* (the middle: the game draws the ceiling in two halves) */
        u[i][4] = l + w * 192 / 256;   hh[i][4] = 512;
        u[i][5] = s[i]->outerR + o[i]; hh[i][5] = 256;
        u[i][6] = s[i]->outerR + o[i]; hh[i][6] = s[i]->shoulderH[1];
    }
    for (i = 0; i < 6; i++) {   /* u along the road (one copy per segment); v from texture row 0 at a span's lower / inner end to the last row at its upper /
                                   outer end, as the game maps them (METL21RW: row 64 at the top of the wall) - the right side's spans taken from their lower end */
        int a = i < 4 ? i : i + 1, b = i < 4 ? i + 1 : i; float pa[3], pb[3], pc[3], pd[3], uv[8];
        t = C3_ArchTex(ra, s0, k, h, col[i]);
        C3_Point(c, k, u0[a], h0[a], pa); C3_Point(c, k, u0[b], h0[b], pb); C3_Point(c, k + 1, u1[b], h1[b], pc); C3_Point(c, k + 1, u1[a], h1[a], pd);
        if (i == 2 || i == 3) {   /* the ceiling (METL21CC, 64 x 32): u across each half, mirrored in the right one (the halves meet in the light), v along: one copy per segment */
            float ua = i == 2 ? 0.0f : 1.0f, ub = 1.0f - ua;
            uv[0] = ua; uv[1] = 0; uv[2] = ub; uv[3] = 0; uv[4] = ub; uv[5] = 1; uv[6] = ua; uv[7] = 1;
        } else { uv[0] = 0; uv[1] = 0; uv[2] = 0; uv[3] = 1; uv[4] = 1; uv[5] = 1; uv[6] = 1; uv[7] = 0; }
        C3_QuadT(c, pa, pb, pc, pd, t >= 0 ? 0xffffffffu : C3_Colour(M_WALL, s0->kind, k), t, uv);
    }
    for (i = 0; i < 2; i++) {   /* the footpaths (rr_EdgeStripDraw): from the road edge to the tube wall at the shoulder height, its curb face at the road edge */
        double e0 = (i ? s0->edgeR : s0->edgeL) + o0, e1 = (i ? s1->edgeR : s1->edgeL) + o1, w0 = (i ? s0->outerR : s0->outerL) + o0, w1 = (i ? s1->outerR : s1->outerL) + o1;
        double g0 = s0->shoulderH[i], g1 = s1->shoulderH[i];
        int tp = C3_Shoulder(ra, s0, k, h, 1), tc = C3_Shoulder(ra, s0, k, h, 2); float pa[3], pb[3], pc[3], pd[3], uv[8];
        if (g0 <= 0 && g1 <= 0) continue;
        C3_Point(c, k, e0, g0, pa); C3_Point(c, k, w0, g0, pb); C3_Point(c, k + 1, w1, g1, pc); C3_Point(c, k + 1, e1, g1, pd);
        uv[0] = 0; uv[1] = 0; uv[2] = 1; uv[3] = 0; uv[4] = 1; uv[5] = 1; uv[6] = 0; uv[7] = 1;
        C3_QuadT(c, pa, pb, pc, pd, tp >= 0 ? 0xffffffffu : C3_Colour(M_WALK, 0, k), tp, uv);
        C3_Point(c, k, e0, 0, pa); C3_Point(c, k, e0, g0, pb); C3_Point(c, k + 1, e1, g1, pc); C3_Point(c, k + 1, e1, 0, pd);
        uv[0] = 1; uv[1] = 0; uv[2] = 0; uv[3] = 0; uv[4] = 0; uv[5] = 1; uv[6] = 1; uv[7] = 1;   /* (LIP, 64 x 16: u across the curb's height, top 0; v along, one copy per segment) */
        C3_QuadT(c, pa, pb, pc, pd, tc >= 0 ? 0xffffffffu : C3_Colour(M_CURB, 0, k), tc, uv);
    }
    for (end = 0; end < 2; end++) {   /* the portals: at the first piece (the one before is no tunnel) and at the last (the one after the next is none) */
        int kk = k + end, nb = end ? k + 2 : k - 1; const RoadSection *sn; const double *ou = end ? a1 : a0, *oh = end ? b1 : b0, *iu = end ? u1 : u0, *ih = end ? h1 : h0;
        if (nb < 0 || nb >= c->n) continue;
        sn = h && c->piece[nb].layout == 2 ? &c->piece[nb].halfB : &c->piece[nb].halfA;
        if (sn->kind == SEC_WALL_SPLIT) continue;
        double fu[7], fh[7], cu = (iu[0] + iu[6]) * 0.5;
        for (i = 0; i < 7; i++) {   /* the frame round the opening: the tube's outline pushed C3_TUNNEL_FRAME outwards from its middle (the feet only sideways) */
            double dx = iu[i] - cu, dy = ih[i] - 256, l = sqrt(dx * dx + dy * dy);
            if (i == 0 || i == 6) { fu[i] = iu[i] + (dx < 0 ? -C3_TUNNEL_FRAME : C3_TUNNEL_FRAME); fh[i] = 0; }
            else { fu[i] = iu[i] + (l > 0 ? dx / l : 0) * C3_TUNNEL_FRAME; fh[i] = ih[i] + (l > 0 ? dy / l : 1) * C3_TUNNEL_FRAME; }
        }
        {   int lip = C3_Shoulder(ra, s0, k, h, 2); unsigned lc = lip >= 0 ? 0xffffffffu : C3_Colour(M_CURB, 0, k);
            for (i = 0; i < 6; i++) {   /* point i .. i + 1 of the tube against the frame's: the frame (LIP: u across the band from the outside in, v along each side once -
                                           the ceiling's two halves share one copy); of the frame against the outline's: the rock face (the cliff texture as the cover) */
                float pa[3], pb[3], pc[3], pd[3], uv[8], v0 = i == 3 ? 0.5f : 0.0f, v1 = i == 2 ? 0.5f : 1.0f;
                C3_Point(c, kk, iu[i], ih[i], pa); C3_Point(c, kk, iu[i + 1], ih[i + 1], pb); C3_Point(c, kk, fu[i + 1], fh[i + 1], pc); C3_Point(c, kk, fu[i], fh[i], pd);
                uv[0] = 1; uv[1] = v0; uv[2] = 1; uv[3] = v1; uv[4] = 0; uv[5] = v1; uv[6] = 0; uv[7] = v0;
                C3_QuadT(c, pa, pb, pc, pd, lc, lip, uv);
                C3_Point(c, kk, fu[i], fh[i], pa); C3_Point(c, kk, fu[i + 1], fh[i + 1], pb); C3_Point(c, kk, ou[i + 1], oh[i + 1], pc); C3_Point(c, kk, ou[i], oh[i], pd);
                uv[0] = (float)(fu[i] / C3_TOP); uv[1] = (float)(-fh[i] / C3_TOP); uv[2] = (float)(fu[i + 1] / C3_TOP); uv[3] = (float)(-fh[i + 1] / C3_TOP);
                uv[4] = (float)(ou[i + 1] / C3_TOP); uv[5] = (float)(-oh[i + 1] / C3_TOP); uv[6] = (float)(ou[i] / C3_TOP); uv[7] = (float)(-oh[i] / C3_TOP);
                C3_QuadT(c, pa, pb, pc, pd, col0, rock, uv);
            }
            {   const RoadSection *se = end ? s1 : s0; double oe = end ? o1 : o0;   /* the footpaths' ends: a step at the opening (LIP, u across the path, v down) */
                for (i = 0; i < 2; i++) {
                    double e = (i ? se->edgeR : se->edgeL) + oe, w = (i ? se->outerR : se->outerL) + oe, g = se->shoulderH[i];
                    float pa[3], pb[3], pc[3], pd[3], uv[8];
                    if (g <= 0) continue;
                    C3_Point(c, kk, e, g, pa); C3_Point(c, kk, w, g, pb); C3_Point(c, kk, w, 0, pc); C3_Point(c, kk, e, 0, pd);
                    uv[0] = 0; uv[1] = 0; uv[2] = 1; uv[3] = 0; uv[4] = 1; uv[5] = 1; uv[6] = 0; uv[7] = 1;
                    C3_QuadT(c, pa, pb, pc, pd, lc, lip, uv);
                }
            }
        }
    }
}

static void (*C3_progress)(int done, int all);   /* while the whole course is built (C3_Build): every 32 pieces, how far (the loading screen; NULL = none) */
static void C3_BuildRange(C3Course *c, int k0, int k1, RxAssets *ra)
{
    int k;
    if (k0 < 0) k0 = 0;
    if (k1 > c->n - 1) k1 = c->n - 1;
    for (k = k0; k < k1; k++) {
        const RoadPiece *p0 = &c->piece[k], *p1 = &c->piece[k + 1];
        if (C3_progress && !((k - k0) & 31)) C3_progress(k - k0, k1 - k0);
        C3Row r0, r1; int h, brk[2], nbrk; double bu[2][7], bh[2][7];
        c->cur = k;
        for (h = 0; h < (p0->layout == 2 ? 2 : 1); h++) {   /* split road: half B in its own frames (C3_FramesB; at the join they are A's) */
            const RoadSection *s0 = h ? &p0->halfB : &p0->halfA, *s1 = h ? (p1->layout == 2 ? &p1->halfB : &p1->halfA) : &p1->halfA;
            double o0 = 0, o1 = 0; RoadSection merged;
            int walk = ra && s0->kind == SEC_OPEN, tex[C3_NP - 1];
            int nv0;
            if (C3_HideHalf(c, k, h)) continue;
            nv0 = c->nv;
            if (p0->layout != 2 && p1->layout == 2 && !c->forkPick) { C3_ForkMerge(p1, &merged); s1 = &merged; }
            c->fr = h ? c->frameB : c->frame;
            C3_SectionAt(ra, s0, k, h, o0, &r0); C3_SectionAt(ra, s1, k + 1, h, o1, &r1);
            {   int sd, i; nbrk = 0;   /* (an edited side next to another: square to the end of the piece - its shape moved to the road edge there) */
                for (sd = 0; sd < 2; sd++) if (C3_SideBreak(ra, s0, k, h, s1, k + 1, h, sd)) { C3Row t; double dx; int e = sd ? 8 : 6;
                    C3_SectionAt(ra, s0, k, h, o1, &t); dx = r1.u[e] - t.u[e];
                    r1.hm[sd] = t.hm[sd]; r1.hl[sd] = t.hl[sd];   /* (a square end: its hole's water too) */
                brk[nbrk] = sd; for (i = 0; i < 7; i++) { int j = sd ? 8 + i : i; bu[nbrk][i] = r1.u[j]; bh[nbrk][i] = r1.h[j]; r1.u[j] = t.u[j] + dx; r1.h[j] = t.h[j]; }
                    for (i = 0; i < 6; i++) r1.mat[sd ? 8 + i : i] = t.mat[sd ? 8 + i : i];
                    nbrk++; } }
            c->skRa = ra; c->skOn = 1; c->skind[0] = C3_SideKind(ra, s0, k, h, 0); c->skind[1] = C3_SideKind(ra, s0, k, h, 1);
            if (p0->layout == 2) { C3_InnerClamp(c, k, h, &r0); if (p1->layout == 2) C3_InnerClamp(c, c->soft ? k : k + 1, h, &r1); }   /* (forks on: the piece's end as its start -
                                                                                     the other road's last piece clamped at one end only fanned its hillside across the road) */
            C3_PieceTex(ra, s0, k, h, tex);
            c->darkStrip = -1;
            if (ra && C3_NearForkJoin(c, k, h) && ra->global[18] >= 0 && (!c->soft || C3_Gap(c, k, h) < C3_DARKGAP)) {   /* (forks on: only where the roads are close -
                                                                                                                          Peninsula's join has its plain verge further out) */   /* where B's inner rail stands (Sierra 898..903) the inner shoulders are asphalt with the white edge line at
                                                                            the road (TEXG lane texture WHITE: the line on its u = 0 edge = the road side) - software's fork; the dark
                                                                            wedge beyond is the median (C3_Median) */
                const RoadSection *sb = &c->piece[h ? k : (c->pairB ? c->pairB[k] : k)].halfB;
                if (sb->kindArg[0] == 5 && (sb->kindArg[3] & 1) && ra->global[17] >= 0) { c->darkStrip = h ? 4 : 9; tex[c->darkStrip] = ra->global[17]; } }
                /* near a fork / join, where the inner side (A's right, B's left) has its rail (GroundMarkQueue side structure on the shoulder's outer edge), the shoulder
                   is dark road surface: the wedge the rails stand on in software (Sierra 898..903 on B) */
            if (!(walk && (s0->kindFlags & 0x20))) C3_Strip(c, k, &r0, &r1, s0->kind, walk, tex, walk ? 0xff000000u | (ra->pal[0xfd] & 0xffffffu) : 0, ra);
            if (ra && !(walk && (s0->kindFlags & 0x20))) {   /* (grounds meeting: torn into each other - C3_Blend; the piece before's pictures for the seam along) */
                int pv[C3_NP - 1], hp = h && k > 0 && c->piece[k - 1].layout == 2, sd, sf = 0; const RoadSection *sp = k > 0 ? (hp ? &c->piece[k - 1].halfB : &c->piece[k - 1].halfA) : NULL;
                if (sp && !(sp->kind == SEC_OPEN && (sp->kindFlags & 0x20))) C3_PieceTex(ra, sp, k - 1, hp, pv);
                for (sd = 0; sd < 2; sd++) {
                    if (sp && C3_SideWalk(ra, sp, k - 1, hp, sd)) sf |= 16 << sd;   /* (the piece before's footpath: not torn on along) */
                    if (C3_SideWalk(ra, s0, k, h, sd)) sf |= 1 << sd;   /* (a footpath lowered to the road - its curb's height 0 - runs on too) */
                    if (C3_SideKind(ra, s0, k, h, sd) == SEC_PROFILE && !(C3_SideEd(ra, s0, k, h, sd) & 16) && !(ra->ext && k < ra->next && (ra->ext[k].gutter[h & 1][sd] & 3) == 1)) sf |= 4 << sd; }
                C3_Blend(c, k, &r0, &r1, tex, sp && !(sp->kind == SEC_OPEN && (sp->kindFlags & 0x20)) ? pv : NULL, ra, sf);
                if (r0.wm[0] | r0.wm[1] | r1.wm[0] | r1.wm[1]) C3_Water(c, k, &r0, &r1, ra);
                for (sd = 0; sd < 2; sd++) if (C3_SideCells(ra, s0, k, h, sd, C3_GBRIDGE) & 1) C3_Railing(c, k, &r0, &r1, sd); }
                if (r0.hm[0] | r0.hm[1] | r1.hm[0] | r1.hm[1]) C3_HoleWater(c, k, &r0, &r1, ra);   /* (the water in the holes) */   /* (a bridge's railing) */   /* (painted water's surface) */
            {   int b, i, texN[C3_NP - 1], hasTn = 0; if (ra && nbrk) { C3_PieceTex(ra, s1, k + 1, h, texN); hasTn = 1; }   /* (the next piece's pictures: a hole's end wall) */
                for (b = 0; b < nbrk; b++) for (i = 0; i < 6; i++) {   /* the step where a square end meets the next piece's side: closed (both faces) */
                    int j = brk[b] ? 8 + i : 6 - i, jn = brk[b] ? j + 1 : j - 1; float a[3], bb[3], cc[3], d[3]; unsigned col = C3_Colour(M_TERRAIN, SEC_PROFILE, k);
                    C3_Point(c, k + 1, r1.u[j], r1.h[j], a); C3_Point(c, k + 1, r1.u[jn], r1.h[jn], bb);
                    C3_Point(c, k + 1, bu[b][brk[b] ? jn - 8 : jn], bh[b][brk[b] ? jn - 8 : jn], cc); C3_Point(c, k + 1, bu[b][brk[b] ? j - 8 : j], bh[b][brk[b] ? j - 8 : j], d);
                    {   int sg = brk[b] ? j : jn, t = sg >= 0 && sg < C3_NP - 1 ? tex[sg] : -1, tn = sg >= 0 && sg < C3_NP - 1 && hasTn ? texN[sg] : -1;   /* (the ground of the band it closes: no flat-coloured seam) */
                        if ((t >= 0 && C3_IsPitTex(ra, t)) || (tn >= 0 && C3_IsPitTex(ra, tn))) {   /* a hole's end wall (user, 2026-11-19: "between 53, 52 ... a better cliff face texture ...
                                                                    on both sides of the hole"): the ground beyond the hole's end (this piece or the next one), rock if that can blend */
                            if (t < 0 || C3_IsPitTex(ra, t)) t = tn >= 0 && !C3_IsPitTex(ra, tn) ? tn : C3_NotPitTex(ra, tex, sg);
                            if (t >= 0 && !C3_IsBridgeTex(ra, t) && !C3_TexBaked(ra, t) && ra->nground > C3_GCLIFF) t = ra->ground0 + C3_GCLIFF; }
                        if (t >= 0) { float uv[8] = { 0, 1, 1, 1, 1, 0, 0, 0 }, ub[8];
                            if (ra->nground > C3_GCLIFF && t == ra->ground0 + C3_GCLIFF) { uv[1] = (float)(C3_Dist(a, d) / C3_ROCKPER); uv[2] = (float)(C3_Dist(a, bb) / C3_ROCKPER);
                                uv[3] = (float)(C3_Dist(bb, cc) / C3_ROCKPER); uv[4] = (float)(C3_Dist(d, cc) / C3_ROCKPER); }   /* (rock: the world's scale) */
                            ub[0] = uv[0]; ub[1] = uv[1]; ub[2] = uv[6]; ub[3] = uv[7]; ub[4] = uv[4]; ub[5] = uv[5]; ub[6] = uv[2]; ub[7] = uv[3];
                            C3_QuadTint(c, a, bb, cc, d, 0xffffffffu, t, uv, 0xffffffffu); C3_QuadTint(c, a, d, cc, bb, 0xffffffffu, t, ub, 0xffffffffu); }   /* (the back face: the same
                                                                    diagonal, the same mapping - before, mirrored and split the other way, the two fought for the depth) */
                        else { C3_Quad(c, a, bb, cc, d, col); C3_Quad(c, d, cc, bb, a, col); } } }
                for (b = 0; b < nbrk; b++) {   /* beyond P2 (user, 2026-11-19: "beyond p2 using hole the cliff face should be filled here too"): the skirts' step, where the
                                                  outer cell is a hole on one side of the break - rock from the ground's skirt down to the abyss's */
                    int o = brk[b] ? C3_NP - 2 : 0, j = brk[b] ? C3_NP - 1 : 0, bi = brk[b] ? 6 : 0, tn = hasTn ? texN[o] : -1; double sg = brk[b] ? C3_SKIRT : -C3_SKIRT;
                    float a[3], bb[3], cc[3], d[3], uv[8], ub[8];
                    if (!(tex[o] >= 0 && C3_IsPitTex(ra, tex[o])) == !(tn >= 0 && C3_IsPitTex(ra, tn))) continue;
                    if (fabs(r1.h[j] - bh[b][bi]) < 1 || ra->nground <= C3_GCLIFF) continue;
                    C3_Point(c, k + 1, r1.u[j], r1.h[j], a); C3_Point(c, k + 1, r1.u[j] + sg, r1.h[j], bb);
                    C3_Point(c, k + 1, bu[b][bi] + sg, bh[b][bi], cc); C3_Point(c, k + 1, bu[b][bi], bh[b][bi], d);
                    uv[0] = 0; uv[1] = (float)(C3_Dist(a, d) / C3_ROCKPER); uv[2] = (float)(C3_SKIRT / C3_ROCKPER); uv[3] = uv[1]; uv[4] = uv[2]; uv[5] = 0; uv[6] = 0; uv[7] = 0;
                    ub[0] = uv[0]; ub[1] = uv[1]; ub[2] = uv[6]; ub[3] = uv[7]; ub[4] = uv[4]; ub[5] = uv[5]; ub[6] = uv[2]; ub[7] = uv[3];
                    C3_QuadTint(c, a, bb, cc, d, 0xffffffffu, ra->ground0 + C3_GCLIFF, uv, 0xffffffffu); C3_QuadTint(c, a, d, cc, bb, 0xffffffffu, ra->ground0 + C3_GCLIFF, ub, 0xffffffffu); } }
            if (!(walk && (s0->kindFlags & 0x20)) && s0->kind != SEC_WALL_SPLIT) C3_SnowBank(c, k, &r0, &r1);   /* (seen only in snow; not in tunnels / across cross streets; no drifts on the road itself) */
            if (c->darkStrip >= 0) {   /* the solid white edge line at the road side of that dark shoulder (no TEXG texture has one: drawn as a strip) */
                double e0 = h ? s0->edgeL : s0->edgeR, e1 = h ? s1->edgeL : s1->edgeR, w = h ? -C3_EDGELINE : C3_EDGELINE; float a[3], b[3], cc[3], d[3];
                C3_Point(c, k, e0, 1, a); C3_Point(c, k, e0 + w, 1, b); C3_Point(c, k + 1, e1 + w, 1, cc); C3_Point(c, k + 1, e1, 1, d);
                C3_Quad(c, a, b, cc, d, 0xffe8e8e8u); }
            if (ra && !h && p0->layout == 2 && C3_NearForkJoin(c, k, 0)) C3_Median(c, k, ra);   /* the ground between the carriageways (built once, with A) */
            if (ra && c->skirts && (c->skirtEnd <= 0 || k < c->skirtEnd) && !(p0->layout == 2 && c->hide == 1 + h)) C3_Skirt(   /* (split roads: only the camera's carriageway - the other one's, at its own height, roofed the join) */c, k, &r0, &r1, s0->kind, tex, walk ? 0xff000000u | (ra->pal[0xfd] & 0xffffffu) : 0, s0, ra);   /* town ground beyond the sidewalk: the flat colour 0xfd of WheelTrackPolyDraw */   /* (cross street: C3_Roadside covers both sides) */
            c->skOn = 0;
            if (ra && s0->kind >= SEC_WALL1 && s0->kind <= SEC_WALL_SPLIT) C3_Sea(c, k, s0, o0, s1, o1, ra);
            if (s0->kind == SEC_WALL_SPLIT && s1->kind == SEC_WALL_SPLIT) C3_Tunnel(c, k, h, s0, o0, s1, o1, ra, tex[2]);   /* (Peninsula 385..413, 475..499; the rock: the left cliff face's texture) */
            {   int nr0 = c->nv, j; C3_Lanes(c, k, s0, o0, s1, o1, ra, walk && !(s0->kindFlags & 0x20) ? C3_GUTD : 0);   /* the road surface: vertex alpha 248 (render3d.h: Video / Esc "Road" = new asphalt, a sheen in the sun) */
                for (j = nr0; j < c->nv; j++) if ((c->v[j].rgba >> 24) == 0xff) c->v[j].rgba = (c->v[j].rgba & 0xffffffu) | 0xf8000000u; }
            if (walk) C3_Roadside(c, k, h, s0, o0, s1, o1, ra);
            if (ra) C3_BandBuildings(c, k, h, s0, &r0, &r1, ra);
            if (ra && ra->ext && k < ra->next) { int sd; for (sd = 0; sd < 2; sd++) if ((ra->ext[k].gutter[h & 1][sd] & 3) == 1 && C3_SideKind(ra, s0, k, h, sd) == SEC_PROFILE) {   /* (a terrain side's gutter) */
                double km, rl, gw, gdp; C3_EdgeStyle(ra, k, h, sd, &km, &rl, &gw, &gdp);
                double kb = C3_SideWalk(ra, s0, k, h, sd) ? C3_KERB * km : 0, e0 = (sd ? (s0->laneEdgeR < s0->edgeR ? s0->laneEdgeR : s0->edgeR) : (s0->laneEdgeL > s0->edgeL ? s0->laneEdgeL : s0->edgeL)) + o0;
                double e1 = (sd ? (s1->laneEdgeR < s1->edgeR ? s1->laneEdgeR : s1->edgeR) : (s1->laneEdgeL > s1->edgeL ? s1->laneEdgeL : s1->edgeL)) + o1;
                double k1, r1, g1, d1, kb1; C3_EdgeStyle(ra, k + 1, h, sd, &k1, &r1, &g1, &d1); kb1 = C3_SideWalk(ra, s1, k + 1, h, sd) ? C3_KERB * k1 : 0;   /* (its end: the next piece's curb) */
                C3_Gutter(c, k, sd, e0, e1, kb, kb1, gdp, gw, gdp); } }
            if (ra) { C3_Sprites(c, k, h, s0, o0, ra); C3_SideStructs(c, k, h, s0, o0, s1, o1, ra); }
            if (s0->kind == SEC_WALL_SPLIT && s1->kind == SEC_WALL_SPLIT) { int j; for (j = nv0; j < c->nv; j++) if ((c->v[j].rgba >> 24) == 0xff || (c->v[j].rgba >> 24) == 0xf8) c->v[j].rgba = (c->v[j].rgba & 0xffffffu) | 0xf9000000u; }   /* under a roof (a tunnel: vertex alpha 249) - no rain, puddles or snow there (render3d.h) */
        }
    }
    c->fr = c->frame;
}

/* forks off (forkPick): the original widens the road on the right before a split (room for B) and joins B back on after it (full width at the join,
   then its lanes merge away over ~20 pieces) - with no B both are a pointless bulge.  The right side (road edge, outer edge, extent; the lane edges stay -
   the lane grid is anchored on them, C3_Lanes clips at the road edge) is kept to A's width at the split, blending (smooth step) back into the original's
   own width over the last C3_ONEROADIN pieces before the fork (the game pulls the riders over there, race/physics.c ForkKeep) and C3_ONEROAD after the join */
#define C3_ONEROAD 40
#define C3_ONEROADIN 16
static void C3_OneRoadWidth(C3Course *c)
{
    int ks, kj, side, d;
    if (!c->forkPick) return;
    for (ks = 0; ks < c->n; ks = kj) {
        if (c->piece[ks].layout != 2) { kj = ks + 1; continue; }
        for (kj = ks; kj < c->n && c->piece[kj].layout == 2; kj++) {}
        for (side = 0; side < 2; side++) {   /* 0: before the fork, 1: after the join */
            int n = side ? C3_ONEROAD : C3_ONEROADIN;
            RoadSection a = c->piece[side ? kj - 1 : ks].halfA;   /* A's width at that end of the split */
            if (a.laneEdgeR < a.edgeR) a.edgeR = a.laneEdgeR;   /* (the last split piece already has the joined road's edge - Sierra 1527: edge 750, lane edge 250, outer 351:
                                                                    the road then ran past the verge and the rail after the join) */
            for (d = 1; d <= n; d++) {
                int k = side ? kj - 1 + d : ks - d, h; double t = (double)d / n, s = t * t * (3 - 2 * t);
                if (k < 0 || k >= c->n || c->piece[k].layout == 2) break;
                for (h = 0; h < 2; h++) { RoadSection *p = h ? &c->piece[k].halfB : &c->piece[k].halfA;
#define C3_CLAMPR(f) do { double lim = a.f + (p->f - a.f) * s; if (p->f > lim) p->f = (int)lim; } while (0)
                    C3_CLAMPR(edgeR); C3_CLAMPR(outerR); C3_CLAMPR(extR);
#undef C3_CLAMPR
                }
            }
        }
    }
}

/* build frames + the whole mesh for segment length L */
static void C3_Build(C3Course *c, double L, RxAssets *ra)
{
    c->L = L; c->nv = 0;
    C3_Frames(c); C3_FramesB(c); c->fr = c->frame;
    C3_OneRoadWidth(c);
    c->nbldg = 0; c->recBldg = 1;
    C3_BuildRange(c, 0, c->n - 1, ra);
    c->recBldg = 0;
}

/* sort the triangle list by texture into batches (the renderer draws one texture per batch), stable: within a batch the triangles stay in piece order
   (vpiece ascending, as built), so a range of pieces is one run of every batch */
typedef struct C3Batch { int first, count, tex; } C3Batch;   /* tex >= C3_BG - 1: a background batch (skirts, C3_Skirt) of texture tex - C3_BG */
static int C3_Batches(C3Course *c, C3Batch *out, int max)
{
    int i, n = 0, t, lo = 0x7fffffff, hi = -1, nt, *start; C3Vert *sorted; int *sp;
    if (c->nv <= 0) return 0;
    for (i = 0; i < c->nv; i += 3) { t = (int)c->v[i].tex; if (t < lo) lo = t; if (t > hi) hi = t; }
    nt = hi - lo + 1;
    start = (int *)calloc(nt + 1, sizeof(int)); sorted = (C3Vert *)malloc(sizeof(C3Vert) * c->nv); sp = (int *)malloc(sizeof(int) * c->nv);
    if (!start || !sorted || !sp) { free(start); free(sorted); free(sp); return 0; }
    for (i = 0; i < c->nv; i += 3) start[(int)c->v[i].tex - lo + 1] += 3;
    for (t = 0; t < nt; t++) start[t + 1] += start[t];
    for (t = 0; t < nt && n < max; t++) if (start[t + 1] > start[t]) { out[n].first = start[t]; out[n].count = start[t + 1] - start[t]; out[n].tex = t + lo; n++; }
    for (i = 0; i < c->nv; i += 3) {
        int o = start[(int)c->v[i].tex - lo]; start[(int)c->v[i].tex - lo] += 3;
        sorted[o] = c->v[i]; sorted[o + 1] = c->v[i + 1]; sorted[o + 2] = c->v[i + 2]; sp[o] = sp[o + 1] = sp[o + 2] = c->vpiece[i];
    }
    memcpy(c->v, sorted, sizeof(C3Vert) * c->nv); memcpy(c->vpiece, sp, sizeof(int) * c->nv);
    free(start); free(sorted); free(sp);
    for (t = 0; t < n; t++) if (out[t].tex >= C3_BG - 1) for (i = out[t].first; i < out[t].first + out[t].count; i++) c->v[i].tex -= (float)C3_BG;   /* (the shader sees the texture / -1) */
    return n;
}

/* the first vertex of batch b whose piece is >= k (the batch is in piece order) */
static int C3_BatchCut(const C3Course *c, const C3Batch *b, int k)
{
    int lo = b->first / 3, hi = (b->first + b->count) / 3;   /* in triangles */
    while (lo < hi) { int m = (lo + hi) / 2; if (c->vpiece[m * 3] < k) lo = m + 1; else hi = m; }
    return lo * 3;
}

/* ---- loading an RR_EXTRACT file (RRX1, format in the tracker) ---- */
static int C3_LoadRrx(C3Course *c, const char *path)
{
    unsigned hdr[16]; FILE *f = fopen(path, "rb");
    memset(c, 0, sizeof *c);
    if (!f) return 0;
    if (fread(hdr, 4, 16, f) != 16 || hdr[0] != 0x31585252 || hdr[2] != sizeof(RoadPiece)) { fclose(f); return 0; }
    c->n = (int)hdr[3]; c->start = (int)(hdr[4] - hdr[5]) >> 8; c->finish = (int)hdr[6];
    c->piece = (RoadPiece *)malloc(sizeof(RoadPiece) * c->n);
    if (fread(c->piece, sizeof(RoadPiece), c->n, f) != (size_t)c->n) { fclose(f); return 0; }
    fclose(f);
    return 1;
}
#endif
