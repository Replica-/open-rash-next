/* mesh3d.h - small helpers that emit lit, textured triangles of a model into the true-3D world pass (src\platform\ddraw\world3d.inc: the trams).
 * The model is made in local coordinates (x to its right, y up from the road, z forward; road units) and put on the road by the caller's transform
 * (Mesh3DXf), so it follows hills and curves like the road under it.  (What is left of the 3D traffic cars, car3d.h - removed, user 2026-11-23.)
 */
#ifndef MESH3D_H
#define MESH3D_H

#include <math.h>

/* local point -> world point (the caller puts the model on the road) */
typedef void (*Mesh3DXf)(void *ctx, const float *local, float *world);

/* where the triangles go */
typedef struct Mesh3DOut { R3Vert *v; int n, cap; Mesh3DXf xf; void *ctx; } Mesh3DOut;

/* one triangle: local points p[3], texture coordinates t[3], outward normal n (local); shaded with the world's sun */
static void Mesh3D_Tri(Mesh3DOut *o, const float *p0, const float *p1, const float *p2, const float *t0, const float *t1, const float *t2, const float *n)
{
    static const float sun[3] = { -0.35f, 0.85f, 0.4f };   /* = course3d.h C3_Shade; ambient 0.78 (not the world's 0.55): the texture is lit already */
    const float *p[3], *t[3]; float w[3], pc[3], pn[3], wn[3], l, sh; int i, k; unsigned grey;
    if (o->n + 3 > o->cap) return;
    p[0] = p0; p[1] = p1; p[2] = p2; t[0] = t0; t[1] = t1; t[2] = t2;
    for (k = 0; k < 3; k++) { pc[k] = (p0[k] + p1[k] + p2[k]) / 3; pn[k] = pc[k] + n[k] * 10.0f; }   /* the normal through the transform */
    o->xf(o->ctx, pc, w); o->xf(o->ctx, pn, wn);
    for (k = 0; k < 3; k++) wn[k] -= w[k];
    l = sqrtf(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
    sh = l > 0 ? (wn[0] * sun[0] + wn[1] * sun[1] + wn[2] * sun[2]) / l : 0;
    sh = 0.78f + 0.3f * (sh > 0 ? sh : 0); if (sh > 1) sh = 1;
    grey = (unsigned)(sh * 255); grey = 0xff000000u | (grey << 16) | (grey << 8) | grey;
    for (i = 0; i < 3; i++) { R3Vert *v = &o->v[o->n++]; float q[3];
        o->xf(o->ctx, p[i], q);
        v->x = q[0]; v->y = q[1]; v->z = q[2]; v->u = t[i][0]; v->v = t[i][1]; v->rgba = grey; v->tex = 0; v->ox = v->oy = 0; }
}
static void Mesh3D_Quad(Mesh3DOut *o, const float *a, const float *b, const float *c, const float *d, const float *ta, const float *tb, const float *tc, const float *td, const float *n)
{   Mesh3D_Tri(o, a, b, c, ta, tb, tc, n); Mesh3D_Tri(o, a, c, d, ta, tc, td, n); }

#endif
