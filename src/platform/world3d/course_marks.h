/* course_marks.h - drawn over a 3D view of a course: the Course Editor's own view (src\editor\courseedit.c) and the game renderer it shows the course
 * with (src\platform\ddraw\viewer.inc) - one code for both.  Needs SDL3 and course3d.h (C3_PointF); pj projects a world point to the renderer's
 * coordinates (0 = behind the eye).
 *
 *   CM_Marks: the fields of one piece where they are - the road edge, the lane-line edge, the extent, the kerb (its height), the verge's outer edge,
 *             the wall / cliff (foot to top), the terrain profile (a cross-section), the side texture and side arg - each labelled.
 *   CM_Grid:  the paint grid: a square per piece and side (the roadside, from the road's edge 900 out), those whose field already has the brush's value
 *             filled.
 */
#ifndef RR_COURSE_MARKS_H
#define RR_COURSE_MARKS_H

typedef int (*CM_Proj)(const float *pt, float *sx, float *sy);

static void CM_Line(SDL_Renderer *r, C3Course *c, const C3Frame *F, CM_Proj pj, double s0, double u0, double h0, double s1, double u1, double h1)
{   float a[3], b[3], ax, ay, bx, by;
    C3_PointF(c, F, s0, u0, h0, a); C3_PointF(c, F, s1, u1, h1, b);
    if (!pj(a, &ax, &ay) || !pj(b, &bx, &by)) return;
    SDL_RenderLine(r, ax, ay, bx, by); SDL_RenderLine(r, ax + 1, ay, bx + 1, by); }
static void CM_Label(SDL_Renderer *r, C3Course *c, const C3Frame *F, CM_Proj pj, double s, double u, double h, const char *t, int dy)
{   float p[3], x, y; SDL_FRect bx; Uint8 cr, cg, cb, ca;
    C3_PointF(c, F, s, u, h, p); if (!pj(p, &x, &y)) return;
    y += (float)dy;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    bx.x = x - 2; bx.y = y - 2; bx.w = (float)strlen(t) * 8 + 4; bx.h = 12;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 170); SDL_RenderFillRect(r, &bx);
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255); SDL_RenderDebugText(r, x, y, t); }

/* piece k's section s (F: the frames of its carriageway) */
static void CM_Marks(SDL_Renderer *r, C3Course *c, const C3Frame *F, const RoadSection *s, int k, CM_Proj pj)
{   int side; char t[48]; double a = k, b = k + 1, m = k + 0.5;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (side = 0; side < 2; side++) {
        double sg = side ? 1 : -1, edge = side ? s->edgeR : s->edgeL, lane = side ? s->laneEdgeR : s->laneEdgeL, ext = side ? s->extR : s->extL,
               outer = side ? s->outerR : s->outerL, kerb = s->shoulderH[side];
        int dy = side ? 0 : 0;
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255); CM_Line(r, c, F, pj, a, edge, 0, b, edge, 0); CM_Label(r, c, F, pj, a + 0.12, edge, 0, "road edge", 2);
        if (lane != edge) { SDL_SetRenderDrawColor(r, 170, 170, 170, 255); CM_Line(r, c, F, pj, a, lane, 0, b, lane, 0); CM_Label(r, c, F, pj, a + 0.3, lane, 0, "lane edge", 2); }
        if (ext != edge) { SDL_SetRenderDrawColor(r, 80, 220, 255, 255); CM_Line(r, c, F, pj, a, ext, 0, b, ext, 0); CM_Label(r, c, F, pj, a + 0.45, ext, 0, "extent", 2); }
        if (s->kind == 0) {   /* open road: the kerb (its height) and the verge / sidewalk out to its outer edge */
            SDL_SetRenderDrawColor(r, 255, 230, 60, 255); CM_Line(r, c, F, pj, m, edge, 0, m, edge, kerb + 1);
            sprintf(t, "kerb %d", (int)kerb); CM_Label(r, c, F, pj, m, edge, kerb, t, -14 + dy);
            SDL_SetRenderDrawColor(r, 90, 255, 120, 255); CM_Line(r, c, F, pj, a, outer, kerb, b, outer, kerb); CM_Line(r, c, F, pj, m, edge, kerb, m, outer, kerb);
            sprintf(t, "verge %d", s->shoulderW[side]); CM_Label(r, c, F, pj, a + 0.7, outer, kerb, t, 2); }
        if (s->kind >= 1 && s->kind <= 3) {   /* walls / cliffs: the face from (x0, road level) to (x1, height), and its top along the piece */
            double x0 = s->wall[side * 3], x1 = s->wall[side * 3 + 1], wh = s->wall[side * 3 + 2];
            SDL_SetRenderDrawColor(r, 255, 80, 80, 255); CM_Line(r, c, F, pj, m, x0, 0, m, x1, wh); CM_Line(r, c, F, pj, a, x1, wh, b, x1, wh);
            sprintf(t, "wall %d", (int)wh); CM_Label(r, c, F, pj, m, x1, wh, t, -14); }
        if (s->kind == 4) {   /* terrain profile: the cross-section through its 3 points */
            int p; double px = edge, ph = 0;
            SDL_SetRenderDrawColor(r, 255, 150, 40, 255);
            for (p = 0; p < 3; p++) { double nx = s->profile[p * 4 + side * 2], nh = s->profile[p * 4 + side * 2 + 1];
                if (nx == 0 && nh == 0) break;
                CM_Line(r, c, F, pj, m, px, ph, m, nx, nh); px = nx; ph = nh;
                sprintf(t, "P%d", p); CM_Label(r, c, F, pj, m, nx, nh, t, -14); }
            CM_Label(r, c, F, pj, a + 0.2, edge + sg * 400, 0, "profile", 2); }
        SDL_SetRenderDrawColor(r, 255, 120, 255, 255);   /* the roadside codes */
        sprintf(t, "side tex %d", s->sideTex[side] == 0xff ? -1 : s->sideTex[side]); CM_Label(r, c, F, pj, m, outer + sg * 350, kerb, t, -14);
        sprintf(t, "side arg %d", s->sideArg[side] == 0xff ? -1 : s->sideArg[side]); CM_Label(r, c, F, pj, m, outer + sg * 350, kerb, t, 0); } }

/* the paint grid round piece k0 (lo..hi): a square per piece and side; fill = the field at off (size 1 / 4 bytes, +step for the right side) has the
   brush's value already.  sec(k) gives piece k's section (the carriageway shown) */
static void CM_Grid(SDL_Renderer *r, C3Course *c, const C3Frame *F, int lo, int hi, CM_Proj pj, const RoadSection *(*sec)(int k),
                    int off, int size, int step, int value)
{   int k, side;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (k = lo; k <= hi; k++) { const RoadSection *s0, *s1;
        if (k < 0 || k + 1 >= c->n) continue;
        s0 = sec(k); s1 = sec(k + 1);
        for (side = 0; side < 2; side++) {
            double u0a = side ? s0->edgeR : s0->edgeL - 900, u1a = side ? s0->edgeR + 900 : s0->edgeL, u0b = side ? s1->edgeR : s1->edgeL - 900, u1b = side ? s1->edgeR + 900 : s1->edgeL;
            float p[4][3], x[4], y[4]; int j, ok = 1, have = 0;
            C3_PointF(c, F, k, u0a, 4, p[0]); C3_PointF(c, F, k, u1a, 4, p[1]); C3_PointF(c, F, k + 1, u1b, 4, p[2]); C3_PointF(c, F, k + 1, u0b, 4, p[3]);
            for (j = 0; j < 4; j++) ok &= pj(p[j], &x[j], &y[j]);
            if (!ok) continue;
            if (off > 0) { const unsigned char *q = (const unsigned char *)s0 + off + side * step; have = (size == 1 ? *q : *(const int *)q) == value; }
            if (have) {   /* (filled: the brush's value is there) */
                SDL_Vertex v[4]; int idx[6] = { 0, 1, 2, 0, 2, 3 }; SDL_FColor fc = { 1.0f, 0.55f, 0.1f, 0.38f };
                for (j = 0; j < 4; j++) { v[j].position.x = x[j]; v[j].position.y = y[j]; v[j].color = fc; v[j].tex_coord.x = v[j].tex_coord.y = 0; }
                SDL_RenderGeometry(r, NULL, v, 4, idx, 6); }
            SDL_SetRenderDrawColor(r, 255, 255, 255, 150);
            for (j = 0; j < 4; j++) SDL_RenderLine(r, x[j], y[j], x[(j + 1) & 3], y[(j + 1) & 3]); } } }

#endif
