/* viewer.inc - the Course Editor's game viewer (src\platform\viewer_api.h; the editor: src\editor\courseedit.c).
 *
 * RR_VIEWER=<name> (with RR_AUTORACE / RR_LEVEL / RR_WORLD3D=1 / RR_HEADLESS=2 and the edits in RR_COURSEEDITFILE): the race starts as usual, off-screen.
 * Once its 3D world has been drawn for a second the race is held still (RR_GameSpeed 0: mainloop.c waits) and the window goes into the editor's 3D panel.
 * From then on the paused loop calls ViewerIdle: the last race frame composed again (GpuCompose) with the editor's camera (W3Draw: ViewerCamera), without
 * the game's polygons, its overlay, the HUD or the countdown; the chosen pieces framed and the piece numbers on top (ViewerOverlay).  The renderer is the
 * game's own: its HD textures and sprites, the generated facades, grass, lights, weather.
 */
#include "../world3d/course_marks.h"   /* (the fields / the paint grid over the picture: the editor's own view draws them with the same code) */
static int g_vwOn = -1; static unsigned long g_vwF0; static DWORD g_vwLast;   /* (g_vw, g_vwHold, g_vwVp ...: rrddraw.c, before gpuscene.inc) */
static int ViewerOn(void)
{   if (g_vwOn < 0) { const char *e = getenv("RR_VIEWER"); HANDLE m; g_vwOn = 0;
        if (e && *e && (m = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, e)) != NULL) {
            g_vw = (RrViewer *)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RrViewer));
            if (g_vw && g_vw->magic == RRV_MAGIC) { g_vwOn = 1; g_vw->hwnd = (unsigned)(UINT_PTR)g_hwnd; LOG("viewer: on (%s)", e); } } }
    return g_vwOn; }
/* the course piece the editor looks at (W3Draw's vp: the live stretch is built round it) */
static double ViewerPiece(void)
{   int k = (int)g_vw->camS; if (k < 0) k = 0; if (k > g_w3c.n - 2) k = g_w3c.n - 2; return k; }
/* the editor's orbit camera round the focus piece (its Render3D's): eye, forward and the projection for the 3D picture tw x th */
static void ViewerCamera(float *eye, float *fwd, float *proj, int tw, int th)
{   float at[3], len; double hd, s = g_vw->camS; int k = (int)ViewerPiece(); const C3Frame *F = g_vw->half && g_w3c.frameB ? g_w3c.frameB : g_w3c.frame;
    if (s < k) s = k; if (s > k + 1) s = k + 1;
    C3_PointF(&g_w3c, F, s, (g_vw->half ? W3MidB(s) : C3_RoadMid(&g_w3c, s)) + g_vw->camU, 0, at); at[1] += 150;
    hd = F[k].heading + (F[k + 1].heading - F[k].heading) * (s - k) + g_vw->yaw;   /* (the road's heading there, between the pieces: no steps while WASD moves along) */
    eye[0] = at[0] - (float)(sin(hd) * cos(g_vw->pitch) * g_vw->dist); eye[2] = at[2] - (float)(cos(hd) * cos(g_vw->pitch) * g_vw->dist); eye[1] = at[1] + (float)(sin(g_vw->pitch) * g_vw->dist);
    fwd[0] = at[0] - eye[0]; fwd[1] = at[1] - eye[1]; fwd[2] = at[2] - eye[2]; len = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]); if (len > 0) { fwd[0] /= len; fwd[1] /= len; fwd[2] /= len; }
    R3_Persp(proj, tw * 0.5f / 0.62f, tw * 0.5f / 0.62f, tw * 0.5f, th * 0.5f, tw, th, (float)W3_ZN, (float)W3Far()); }
static int ViewerProject(const float *pt, float *sx, float *sy)   /* world -> render coordinates (the rect the 3D picture is drawn in) */
{   const float *m = g_vwVp; float X = pt[0] * m[0] + pt[1] * m[4] + pt[2] * m[8] + m[12], Y = pt[0] * m[1] + pt[1] * m[5] + pt[2] * m[9] + m[13], W = pt[0] * m[3] + pt[1] * m[7] + pt[2] * m[11] + m[15];
    if (W < 1) return 0; *sx = g_vwRect.x + (X / W + 1) * 0.5f * g_vwRect.w; *sy = g_vwRect.y + (1 - Y / W) * 0.5f * g_vwRect.h; return 1; }
static void ViewerPoint(int k, double u, float *pt) { const C3Frame *F = g_vw->half && g_w3c.frameB ? g_w3c.frameB : g_w3c.frame; C3_PointF(&g_w3c, F, k, u, 6, pt); }
static const RoadSection *ViewerSec(int k) { const RoadPiece *p = &g_w3c.piece[k]; return g_vw->half && p->layout == 2 ? &p->halfB : &p->halfA; }
/* the editor's changes since the race loaded (its pieces memory): into the course and the live copy, the stretch built again (the frames too when a
   curve / hill changed) - so an edit shows at once, no reload */
static RrvPiece *g_vwPm; static unsigned *g_vwPmVer;   /* the editor's pieces memory (all the course's pieces as the editor has them) */
static RrvPiece *ViewerPm(void)
{   static int tried; char nm[80]; HANDLE m;
    if (g_vwPm || tried || g_w3c.n <= 0) return g_vwPm;
    tried = 1; _snprintf(nm, sizeof nm - 1, "%sP", getenv("RR_VIEWER")); nm[sizeof nm - 1] = 0;
    if ((m = OpenFileMappingA(FILE_MAP_READ, FALSE, nm)) != NULL) g_vwPm = (RrvPiece *)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
    g_vwPmVer = (unsigned *)calloc(g_w3c.n, sizeof(unsigned)); if (!g_vwPm || !g_vwPmVer) { LOG("viewer: no pieces memory %s", nm); g_vwPm = NULL; }
    return g_vwPm; }
/* piece k's section as the EDITOR has it (the paint grid's values: the course here has the City's footpaths lowered) - or the course's */
static const RoadSection *ViewerEdSec(int k) { RrvPiece *pm = ViewerPm(); const RoadPiece *p = &g_w3c.piece[k]; int b = g_vw->half && p->layout == 2;
    return pm ? (const RoadSection *)(b ? pm[k].b : pm[k].a) : ViewerSec(k); }
static void ViewerEdits(void)
{   static unsigned seen; RrvPiece *pm; unsigned *ver; int k, n = g_w3c.n, refr = 0, any = 0;
    if (g_vw->editSeq == seen || n <= 0) return;
    seen = g_vw->editSeq;
    if (!(pm = ViewerPm())) return;
    ver = g_vwPmVer;
    for (k = 0; k < n; k++) { RoadPiece *p = &g_w3c.piece[k]; int h;
        if (pm[k].ver == ver[k]) continue;
        ver[k] = pm[k].ver;
        if (pm[k].layout != p->layout) continue;   /* (a split / single road cannot change here) */
        for (h = 0; h < (p->layout == 2 ? 2 : 1); h++) { RoadSection *s = h ? &p->halfB : &p->halfA; const RoadSection *e = (const RoadSection *)(h ? pm[k].b : pm[k].a);
            if (e->curve != s->curve || e->hill != s->hill) refr = 1;
            memcpy((unsigned char *)s + 0x2c, (const unsigned char *)e + 0x2c, RRV_SEC - 0x2c);   /* (from the lanes on: as an edited course file, road\course_edit.c) */
            if (g_w3hdTrack == 1 && s->kind == SEC_OPEN) { int sd; for (sd = 0; sd < 2; sd++) if (s->shoulderH[sd] > 0) s->shoulderH[sd] = (s->shoulderH[sd] * 9 + 5) / 10; } }   /* (the City's footpaths, as loaded) */
        if (p->layout != 2) p->halfB = p->halfA;
        if (g_w3l.piece) g_w3l.piece[k] = *p;
        if (!g_w3a.ext && (g_w3a.ext = (RrcExtra *)calloc(n, sizeof(RrcExtra))) != NULL) g_w3a.next = n;   /* (the painted terrain bands: course3d.h C3_ProfileTex) */
        if (g_w3a.ext && k < g_w3a.next) memcpy(g_w3a.ext[k].band, pm[k].band, sizeof pm[k].band);
        any++; }
    if (refr) { C3_Frames(&g_w3c); C3_FramesB(&g_w3c); }
    if (any) { g_w3lo = -1; LOG("viewer: %d piece(s) changed by the editor%s", any, refr ? " (frames again)" : ""); } }
/* over the 3D picture: a frame round each chosen piece (its start and end lines, both sides; the focus orange) and the piece numbers along the road */
static void ViewerOverlay(void)
{   static SDL_FRect used[600]; int nu = 0, d, lo, hi, k0;
    if (!g_vwHaveVp || g_w3c.n < 2) return;
    lo = g_vw->selLo; hi = g_vw->selHi; k0 = (int)ViewerPiece();
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    if (lo >= 0) { int k; for (k = lo < k0 - 300 ? k0 - 300 : lo; k <= hi && k <= k0 + 600 && k + 1 < g_w3c.n; k++) {
        const RoadSection *a = ViewerSec(k), *b = ViewerSec(k + 1); int rg, part = g_vw->part;
        for (rg = 0; rg < 2; rg++) {   /* the part's frame(s): the whole piece over the road; a side from the road's edge 900 out; both sides two frames */
            float q[4][3], x[5], y[5]; int j, ok = 1; double a0, a1, b0, b1;
            if (part == 0) { if (rg) break; a0 = a->edgeL - 30; a1 = a->edgeR + 30; b0 = b->edgeL - 30; b1 = b->edgeR + 30; }
            else if (part == 1 || (part == 3 && rg == 0)) { if (part == 1 && rg) break; a0 = a->edgeL - 900; a1 = a->edgeL; b0 = b->edgeL - 900; b1 = b->edgeL; }
            else { if (part == 2 && rg) break; if (part == 2 && rg == 0) rg = 1; a0 = a->edgeR; a1 = a->edgeR + 900; b0 = b->edgeR; b1 = b->edgeR + 900; }
            ViewerPoint(k, a0, q[0]); ViewerPoint(k, a1, q[1]); ViewerPoint(k + 1, b1, q[2]); ViewerPoint(k + 1, b0, q[3]);
            for (j = 0; j < 4; j++) ok &= ViewerProject(q[j], &x[j], &y[j]);
            if (!ok) continue;
            x[4] = x[0]; y[4] = y[0];
            if (k == g_vw->focus) SDL_SetRenderDrawColor(g_ren, 255, 110, 30, 255); else SDL_SetRenderDrawColor(g_ren, 255, 220, 40, 255);
            for (j = 0; j < 4; j++) { SDL_RenderLine(g_ren, x[j], y[j], x[j + 1], y[j + 1]); SDL_RenderLine(g_ren, x[j], y[j] + 1, x[j + 1], y[j + 1] + 1); } } } }
    {   const C3Frame *F = g_vw->half && g_w3c.frameB ? g_w3c.frameB : g_w3c.frame;
        if (g_vw->paint) CM_Grid(g_ren, &g_w3c, F, k0 - 60, k0 + 160, ViewerProject, ViewerEdSec, g_vw->paintOff, g_vw->paintSize, g_vw->paintStep, g_vw->paintValue);
        if (g_vw->marks && g_vw->focus >= 0 && g_vw->focus + 1 < g_w3c.n) CM_Marks(g_ren, &g_w3c, F, ViewerSec(g_vw->focus), g_vw->focus, ViewerProject); }
    for (d = 0; d < 400 && nu < 600; d++) { int s2;   /* every piece's number, out from the focus; one that would cover a nearer one is left out */
        for (s2 = 0; s2 < (d ? 2 : 1); s2++) { int j = s2 ? k0 - d : k0 + d, u; float pt[3], sx, sy, tw; char lb[16]; SDL_FRect r;
            if (j < 0 || j + 1 >= g_w3c.n) continue;
            { const C3Frame *F = g_vw->half && g_w3c.frameB ? g_w3c.frameB : g_w3c.frame; C3_PointF(&g_w3c, F, j + 0.5, (ViewerSec(j)->laneEdgeL + ViewerSec(j)->laneEdgeR) * 0.5, 40, pt); }
            if (!ViewerProject(pt, &sx, &sy) || sx < g_vwRect.x || sy < g_vwRect.y || sx > g_vwRect.x + g_vwRect.w || sy > g_vwRect.y + g_vwRect.h) continue;
            sprintf(lb, "%d", j); tw = (float)strlen(lb) * 8; r.x = sx - tw / 2 - 1; r.y = sy - 5; r.w = tw + 2; r.h = 10;
            for (u = 0; u < nu; u++) if (r.x < used[u].x + used[u].w && used[u].x < r.x + r.w && r.y < used[u].y + used[u].h && used[u].y < r.y + r.h) break;
            if (u < nu) continue; used[nu++] = r;
            SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 140); SDL_RenderFillRect(g_ren, &r);
            if (j == g_vw->focus) SDL_SetRenderDrawColor(g_ren, 255, 120, 40, 255); else if (j >= lo && j <= hi && lo >= 0) SDL_SetRenderDrawColor(g_ren, 255, 230, 60, 255); else SDL_SetRenderDrawColor(g_ren, 255, 255, 255, 255);
            SDL_RenderDebugText(g_ren, r.x + 1, r.y + 1, lb); } } }
/* a click in the editor's panel: the piece whose ground there is nearest, and where on it - the road, left of it, right of it */
static void ViewerPick(void)
{   static int done; float lx, ly, bd = 90.0f * 90.0f; int k, best = -1, bpart = 0, k0;
    if (g_vw->clickSeq == done || !g_vwHaveVp) return;
    done = g_vw->clickSeq; k0 = (int)ViewerPiece();
    {   int cw = g_vw->clickW, ch = g_vw->clickH; RECT cr; float wx = (float)g_vw->clickX, wy = (float)g_vw->clickY;   /* (the panel's pixels -> this window's: it fills the panel) */
        GetClientRect(g_hwnd, &cr); if (cw > 0 && ch > 0) { wx = wx * cr.right / cw; wy = wy * cr.bottom / ch; }
        if (!SDL_RenderCoordinatesFromWindow(g_ren, wx, wy, &lx, &ly)) { lx = wx; ly = wy; } }
    for (k = k0 - 300 < 0 ? 0 : k0 - 300; k <= k0 + 600 && k + 1 < g_w3c.n; k++) { int j; const RoadSection *s = ViewerSec(k);
        for (j = 0; j < 9; j++) { float pt[3], sx, sy, dd; static const double out[3] = { 1200, 700, 300 };   /* (3 points left of the road, 3 on it, 3 right of it) */
            double u = j < 3 ? s->edgeL - out[j] : j == 3 ? s->laneEdgeL : j == 4 ? (s->laneEdgeL + s->laneEdgeR) * 0.5 : j == 5 ? s->laneEdgeR : s->edgeR + out[8 - j];
            const C3Frame *F = g_vw->half && g_w3c.frameB ? g_w3c.frameB : g_w3c.frame; C3_PointF(&g_w3c, F, k + 0.5, u, 0, pt);
            if (!ViewerProject(pt, &sx, &sy)) continue; dd = (sx - lx) * (sx - lx) + (sy - ly) * (sy - ly); if (dd < bd) { bd = dd; best = k; bpart = j < 3 ? 1 : j < 6 ? 0 : 2; } } }
    g_vw->pickPiece = best; g_vw->pickPart = bpart; g_vw->pickShift = g_vw->clickShift; g_vw->pickSeq = done; }
/* into the editor's panel (once the race is held): a child window, disabled - the editor's panel keeps the mouse (orbit, clicks) */
static void ViewerEmbed(void)
{   HWND par = (HWND)(UINT_PTR)g_vw->parent; RECT r;
    if (!par || !IsWindow(par) || !g_hwnd) return;
    SetWindowLongA(g_hwnd, GWL_EXSTYLE, 0);
    SetWindowLongA(g_hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_DISABLED | WS_CLIPSIBLINGS);
    SetParent(g_hwnd, par); GetClientRect(par, &r);
    SetWindowPos(g_hwnd, HWND_TOP, 0, 0, r.right, r.bottom, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    LOG("viewer: in the editor's panel %dx%d", (int)r.right, (int)r.bottom); }
/* the held race: the last frame again with the editor's camera (about 60 a second) */
static void ViewerIdle(void)
{   DWORD t = GetTickCount();
    if (t - g_vwLast < 15) return;
    g_vwLast = t;
    {   int c = ShowCursor(TRUE); while (c < 0) c = ShowCursor(TRUE); if (c > 0) ShowCursor(FALSE); }   /* the cursor seen: the race hid it (ShowCursor(FALSE), audio\sound.c), and in the
                                                                                                          editor's panel this window shares the editor's input state - its cursor went too */
    {   HWND par = (HWND)(UINT_PTR)g_vw->parent; RECT pr, wr; if (par && IsWindow(par) && GetParent(g_hwnd) == par) { GetClientRect(par, &pr); GetClientRect(g_hwnd, &wr);   /* (the panel resized: follow it) */
            if (pr.right != wr.right || pr.bottom != wr.bottom) SetWindowPos(g_hwnd, HWND_TOP, 0, 0, pr.right, pr.bottom, SWP_NOACTIVATE); } }
    ViewerEdits();
    AcquireSRWLockShared(&g_surfLk);
    if (g_lastShown && g_ren) { WinFit(); if (GpuCompose(g_lastShown)) { ViewerPick(); SDL_RenderPresent(g_ren); } }
    ReleaseSRWLockShared(&g_surfLk); }
/* RR_GameSpeed in viewer mode: the race runs until its 3D world has been drawn for a second, then it is held (0) for good */
static int ViewerSpeed(void)
{   if (g_vw->quit || (g_vw->parent && !IsWindow((HWND)(UINT_PTR)g_vw->parent))) { LOG("viewer: quit (%s)", g_vw->quit ? "asked" : "the editor is gone"); ExitProcess(0); }   /* (an editor that crashed or was killed leaves no game behind) */
    if (!g_vwHold) {
        if (!g_w3On || !g_w3HaveCam) { if (!g_vwF0) g_vwF0 = g_frame; if (g_frame - g_vwF0 > 1500 && g_vw->state == 0) { g_vw->state = 2; LOG("viewer: no 3D world"); } return 100; }
        { static unsigned long f3; if (!f3) f3 = g_frame; if (g_frame - f3 < 60) return 100; }   /* (a second of race frames first: the 3D world and its textures made) */
        g_vwHold = 1; g_vw->npieces = g_w3c.n; ViewerEmbed(); g_vw->state = 1; }
    ViewerIdle(); return 0; }
