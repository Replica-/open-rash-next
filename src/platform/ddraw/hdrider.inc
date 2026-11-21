/* hdrider.inc - Game Options > HD Rider (included by rrddraw.c after hdtex.inc; True 3D races only, drawing only - nothing here changes the game state).
 * Every rider tile (RrGpuQuad.riderSprite = its HiBob sprite) is drawn from a 4x picture instead of the game's 8-bit tile:
 *   - the pictures: the sprites redrawn with Fooocus (analysis\rider_hd: rider.py prep / post), all in one colour scheme (row 0), true colour;
 *   - colours: not the palette's.  Each picture comes with a part map (helmet, jacket, stripes, trousers, bike - from the template's slots, ridercol.inc
 *     g_rcLo / g_rcHi; the tail lamp and the fixed slots keep the picture's own colours) and every part is repainted in the rider's own bright colour,
 *     shaded by the picture's brightness (relative to the part's mean brightness in the pictures, hdrider.txt "yref").  Computer riders: a bright scheme
 *     per colour row (RrGpuQuad.riderRow), so riders are told apart at a glance; the player: Change clothes (ridercol.inc RcPartPick) - a custom colour
 *     as it is, a scheme or "own" as that row's bright scheme;
 *   - moves: when a rider's sprite changes from A to B, the next frames show in-between pictures (RIFE, analysis\rider_hd: rife.py) before B itself -
 *     one (the half-way picture) for steering and fights, up to three spread over the frames B is usually held for slower moves (tumbling, running back,
 *     lifting the bike); pairs whose pictures do not overlap well (unrelated poses) cut as before.  An in-between picture is drawn in B's frame on a
 *     larger canvas (B's origin at ox, oy): the quad is extended over it, symmetrically across (W3MapObj centres the player on its lateral) and never
 *     below B's foot, so the rider does not move; GpuTex lox / loy tell the lamp code where B's own pixels are.
 * Files (mod\hdrider: RR_HDRIDER_DIR, <exe folder>\mod\hdrider, <cwd>\mod\hdrider): hdrider.txt (index), s<NNN>.png / l<NNN>.png (picture / part map
 * of sprite NNN, exactly 4x), m<A>_<B>_<k>.png / n<A>_<B>_<k>.png (in-between k of A -> B and its part map, 4x the canvas).
 * Part maps: grey value = part * 40 (0 = keep the picture's colour, 1 helmet, 2 jacket, 3 stripes, 4 trousers, 5 bike).
 * Test switch RR_HDRIDER=0 / 1 instead of the setting; RR_HRLOG=1 logs the pictures as they are made. */
#define HR_SPR 326
#define HR_PARTS 6
#define HR_MIDS 2048
#define HR_TEX 384                  /* textures (one per picture and colours; ~0.7 MB each with its mip levels), reused after 120 scenes unused */
#define HR_LOADED 48                /* pictures held decoded (a 32-bit process: the least recently used is dropped, read again when needed) */
#define HR_RIDERS 128
typedef struct HrPic { int w, h, ox, oy; DWORD *px; BYTE *lab; int bad; unsigned used; } HrPic;   /* w, h, ox, oy in sprite pixels; px / lab: 4w x 4h */
static int RcPartPick(int part, unsigned *rgb);   /* ridercol.inc */
typedef struct HrMid { short a, b, n; HrPic pic[3]; } HrMid;
typedef struct HrTex { GpuTex t; DWORD key; } HrTex;
typedef struct HrRider { unsigned id; int cur, prev; unsigned start, seen; } HrRider;
static char g_hrDir[MAX_PATH]; static int g_hrState;   /* 0 = not looked yet, 1 = installed, -1 = not */
static HrPic g_hrSpr[HR_SPR]; static float g_hrYref[HR_PARTS];
static HrMid g_hrMid[HR_MIDS]; static int g_hrNMid;
static HrTex g_hrTex[HR_TEX]; static HrRider g_hrRider[HR_RIDERS];

static void HrIndex(void)
{   FILE *f; char p[MAX_PATH + 16], l[160]; int i;
    for (i = 1; i < HR_PARTS; i++) g_hrYref[i] = 0.5f;
    _snprintf(p, sizeof p - 1, "%s\\hdrider.txt", g_hrDir); p[sizeof p - 1] = 0;
    if (!(f = fopen(p, "r"))) return;
    while (fgets(l, sizeof l, f)) { int a, b, n, ox, oy, w, h; float y;
        if (sscanf(l, "sprite %d %d %d", &a, &w, &h) == 3 && a >= 0 && a < HR_SPR) { g_hrSpr[a].w = w; g_hrSpr[a].h = h; }
        else if (sscanf(l, "yref %d %f", &a, &y) == 2 && a > 0 && a < HR_PARTS && y > 0.02f) g_hrYref[a] = y;
        else if (sscanf(l, "mid %d %d %d %d %d %d %d", &a, &b, &n, &ox, &oy, &w, &h) == 7 && g_hrNMid < HR_MIDS && n >= 1 && n <= 3) {
            HrMid *m = &g_hrMid[g_hrNMid++]; m->a = (short)a; m->b = (short)b; m->n = (short)n;
            for (i = 0; i < n; i++) { m->pic[i].w = w; m->pic[i].h = h; m->pic[i].ox = ox; m->pic[i].oy = oy; } } }
    fclose(f);
    LOG("hdrider: %s, %d in-between sets", g_hrDir, g_hrNMid); }
static int HrInstalled(void)
{   if (!g_hrState) { const char *e = getenv("RR_HDRIDER_DIR"); char c[3][MAX_PATH]; int i, n = 0;
        if (e && *e) { strncpy(c[n], e, MAX_PATH - 1); c[n++][MAX_PATH - 1] = 0; }
        { char ex[MAX_PATH], *s; GetModuleFileNameA(NULL, ex, sizeof ex); if ((s = strrchr(ex, '\\')) != NULL) { *s = 0; _snprintf(c[n], MAX_PATH - 1, "%s\\mod\\hdrider", ex); c[n++][MAX_PATH - 1] = 0; } }
        strcpy(c[n++], "mod\\hdrider");
        g_hrState = -1;
        for (i = 0; i < n; i++) { char p[MAX_PATH + 16]; _snprintf(p, sizeof p - 1, "%s\\hdrider.txt", c[i]); p[sizeof p - 1] = 0;
            if (GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES) { strcpy(g_hrDir, c[i]); g_hrState = 1; HrIndex(); break; } } }
    return g_hrState > 0; }
static int HrOn(void)
{   static int env = -2;
    if (env == -2) { const char *e = getenv("RR_HDRIDER"); env = (e && *e) ? (*e != '0') : -1; }
    return (env >= 0 ? env : g_vid.hdRider) && g_w3On && HrInstalled(); }

static HrPic *g_hrLoaded[HR_LOADED]; static int g_hrNLoaded;
static int HrLoad(HrPic *p, const char *pic, const char *lab)   /* 1 = loaded (a missing / wrong file is not tried again) */
{   char a[MAX_PATH + 32], b[MAX_PATH + 32]; DWORD *l; int i, n;
    if (p->px) { p->used = g_gpuFrame; return 1; }
    if (p->bad || p->w <= 0 || p->h <= 0) return 0;
    p->bad = 1;
    if (g_hrNLoaded == HR_LOADED) { int o = 0; for (i = 1; i < HR_LOADED; i++) if (g_gpuFrame - g_hrLoaded[i]->used > g_gpuFrame - g_hrLoaded[o]->used) o = i;
        free(g_hrLoaded[o]->px); free(g_hrLoaded[o]->lab); g_hrLoaded[o]->px = NULL; g_hrLoaded[o]->lab = NULL; g_hrLoaded[o] = g_hrLoaded[--g_hrNLoaded]; }
    _snprintf(a, sizeof a - 1, "%s\\%s.png", g_hrDir, pic); a[sizeof a - 1] = 0; _snprintf(b, sizeof b - 1, "%s\\%s.png", g_hrDir, lab); b[sizeof b - 1] = 0;
    if (!(p->px = HdLoad(a, p->w, p->h))) return 0;
    if (!(l = HdLoad(b, p->w, p->h)) || !(p->lab = (BYTE *)malloc((size_t)16 * p->w * p->h))) { free(l); free(p->px); p->px = NULL; return 0; }
    for (i = 0, n = 16 * p->w * p->h; i < n; i++) { int v = ((l[i] & 255) + 20) / 40; p->lab[i] = (BYTE)(v < HR_PARTS ? v : 0); }
    free(l); p->bad = 0; p->used = g_gpuFrame; g_hrLoaded[g_hrNLoaded++] = p; return 1; }
static HrPic *HrSprite(int spr)
{   char a[16], b[16];
    if (spr < 0 || spr >= HR_SPR) return NULL;
    sprintf(a, "s%03d", spr); sprintf(b, "l%03d", spr);
    return HrLoad(&g_hrSpr[spr], a, b) ? &g_hrSpr[spr] : NULL; }
static HrMid *HrMidFor(int a, int b)
{   int i; for (i = 0; i < g_hrNMid; i++) if (g_hrMid[i].a == a && g_hrMid[i].b == b) return &g_hrMid[i]; return NULL; }
static HrPic *HrMidPic(HrMid *m, int k)
{   char a[32], b[32];
    sprintf(a, "m%03d_%03d_%d", m->a, m->b, k); sprintf(b, "n%03d_%03d_%d", m->a, m->b, k);
    return HrLoad(&m->pic[k], a, b) ? &m->pic[k] : NULL; }

/* ---- colours */
static unsigned HrHsv(float h, float s, float v)
{   float r, g, b, f, p, q, t; int i; h = fmodf(h, 1.0f); if (h < 0) h += 1; i = (int)(h * 6); f = h * 6 - i; p = v * (1 - s); q = v * (1 - s * f); t = v * (1 - s * (1 - f));
    switch (i % 6) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break; case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
    return ((unsigned)(r * 255 + 0.5f) << 16) | ((unsigned)(g * 255 + 0.5f) << 8) | (unsigned)(b * 255 + 0.5f); }
/* the bright scheme of colour row r: parts 1 helmet, 2 jacket, 3 stripes, 4 trousers, 5 bike */
static unsigned HrScheme(int r, int part)
{   static const float jh[15] = { 0.0f, 0.60f, 0.33f, 0.15f, 0.08f, 0.83f, 0.50f, 0.75f, 0.25f, 0.92f, 0.46f, 0.12f, 0.55f, 0.97f, 0.70f };   /* red, blue, green, yellow, orange, magenta, cyan, purple, lime, pink, teal, gold, sky, crimson, violet */
    float h = jh[(r % 15 + 15) % 15]; int light = h > 0.1f && h < 0.56f;   /* (yellow .. cyan: light jackets get dark stripes) */
    switch (part) {
    case 1: return HrHsv(h + 0.5f, 0.85f, 1.0f);   /* helmet: the opposite hue */
    case 2: return HrHsv(h, 0.95f, 1.0f);
    case 3: return light ? 0x181818u : 0xf4f4f4u;
    case 4: return light ? 0x222226u : HrHsv(h, 0.9f, 0.45f);   /* trousers: the jacket's hue, dark (light jackets: black - a dark yellow is olive) */
    default: return HrHsv(h + 0.04f, 0.9f, 0.95f); } }
static void HrColours(const RrGpuQuad *q, unsigned *c)   /* c[1..5] */
{   int p, row = q->riderRow >= 0 ? q->riderRow : 0;
    for (p = 1; p < HR_PARTS; p++) { unsigned rgb = 0; int k = q->objKind == RRO_PLAYER ? RcPartPick(p - 1, &rgb) : -1;
        c[p] = k == 15 ? rgb : HrScheme(k >= 0 && k < 15 ? k : row, p); } }
/* the picture in the rider's colours: a part's pixel = its colour shaded by the picture's brightness there (brighter than the part's mean: towards white);
   the parts blend over 3 x 3 pixels (a quarter of an original pixel each way) */
static DWORD *HrPaint(const HrPic *p, const unsigned *c)
{   int W = 4 * p->w, H = 4 * p->h, x, y; DWORD *o = (DWORD *)malloc((size_t)W * H * 4);
    if (!o) return NULL;
    for (y = 0; y < H; y++) for (x = 0; x < W; x++) { DWORD s = p->px[y * W + x]; int i, j, n = 0; float r = 0, g = 0, b = 0, Y;
        if (!(s >> 24)) { o[y * W + x] = s; continue; }
        Y = (0.299f * ((s >> 16) & 255) + 0.587f * ((s >> 8) & 255) + 0.114f * (s & 255)) / 255.0f;
        for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) { int xx = x + i, yy = y + j, k; unsigned cc; float f;
            if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
            k = p->lab[yy * W + xx]; n++;
            if (!k) { r += (s >> 16) & 255; g += (s >> 8) & 255; b += s & 255; continue; }
            cc = c[k]; f = Y / g_hrYref[k];
            if (f <= 1) { r += ((cc >> 16) & 255) * f; g += ((cc >> 8) & 255) * f; b += (cc & 255) * f; }
            else { float t = (f - 1) * 0.6f; if (t > 1) t = 1;
                r += ((cc >> 16) & 255) + (255 - ((cc >> 16) & 255)) * t; g += ((cc >> 8) & 255) + (255 - ((cc >> 8) & 255)) * t; b += (cc & 255) + (255 - (cc & 255)) * t; } }
        o[y * W + x] = (s & 0xff000000u) | ((DWORD)(r / n + 0.5f) << 16) | ((DWORD)(g / n + 0.5f) << 8) | (DWORD)(b / n + 0.5f); }
    return o; }

static GpuTex *HrTexFor(const HrPic *p, int spr, DWORD key, const unsigned *c, int wpn)
{   unsigned k = key % HR_TEX, i; HrTex *e, *fr = NULL; DWORD *px; int w, h, l;
    for (i = 0; i < 32; i++) { e = &g_hrTex[(k + i) % HR_TEX];
        if (!e->t.nl) { if (!fr) fr = e; break; }
        if (e->key == key && e->t.rspr == spr && e->t.w == p->w && e->t.h == p->h) { e->t.used = g_gpuFrame; return &e->t; }
        if (!fr && g_gpuFrame - e->t.used > 120) fr = e; }
    if (!fr) return NULL;
    for (l = 0; l < fr->t.nl; l++) if (fr->t.lv[l]) SDL_DestroyTexture(fr->t.lv[l]);
    memset(fr, 0, sizeof *fr);
    if (!(px = HrPaint(p, c))) return NULL;
    w = 4 * p->w; h = 4 * p->h;
    if (wpn > 0) WpClubSwap(px, w, h, spr, wpn);   /* (the player's weapon on the club, as GpuTexFor does on the tile) */
    fr->key = key; fr->t.rspr = spr; fr->t.w = p->w; fr->t.h = p->h; fr->t.lox = p->ox; fr->t.loy = p->oy; fr->t.hd = 2; fr->t.used = g_gpuFrame; fr->t.hdg = g_hdGen;
    for (l = 0; l < RRG_MAXMIP + 4; l++) { DWORD *nx; int nw, nh; fr->t.lv[l] = GpuUpload(px, w, h); fr->t.nl = l + 1; if (!fr->t.lv[l] || (w == 1 && h == 1)) break; nx = GpuHalve(px, w, h, &nw, &nh); free(px); px = nx; w = nw; h = nh; if (!px) break; }
    free(px);
    if (getenv("RR_HRLOG")) LOG("hdrider scene %u: sprite %d %dx%d at %d,%d key %08lx", g_gpuFrame, spr, p->w, p->h, p->ox, p->oy, (unsigned long)key);
    return &fr->t; }

/* the rider's sprite history: which picture this frame shows (B, or an in-between of A -> B) */
static const HrPic *HrPick(const RrGpuQuad *q, int *pid)
{   HrRider *r = NULL, *fr = NULL; int i, j; HrMid *m;
    for (i = 0; i < HR_RIDERS; i++) { HrRider *e = &g_hrRider[i]; if (e->id == q->objId) { r = e; break; } if (!fr || g_gpuFrame - e->seen > g_gpuFrame - fr->seen) fr = e; }
    if (!r) { r = fr; r->id = q->objId; r->cur = r->prev = q->riderSprite; r->start = r->seen = g_gpuFrame; }
    if (g_gpuFrame - r->seen > 2) { r->prev = r->cur = q->riderSprite; r->start = g_gpuFrame; }   /* (out of sight for a while: no in-between) */
    if (r->cur != q->riderSprite) { r->prev = r->cur; r->cur = q->riderSprite; r->start = g_gpuFrame; }
    r->seen = g_gpuFrame;
    j = (int)(g_gpuFrame - r->start);
    if (r->prev != r->cur && (m = HrMidFor(r->prev, r->cur)) != NULL && j < m->n) { const HrPic *p = HrMidPic(m, j); if (p) { *pid = 1000 + (int)(m - g_hrMid) * 4 + j; return p; } }
    *pid = q->riderSprite; return HrSprite(q->riderSprite); }

/* RR_GpuQuad: a rider tile in an HD Rider race - drawn here (1), else the normal way (0) */
static int HrQuad(const RrGpuQuad *q)
{   const HrPic *p; GpuTex *tx; RrGpuQuad q2; unsigned c[HR_PARTS]; DWORD key; int pid, k, wpn;
    if (q->riderSprite < 0 || q->riderSprite >= HR_SPR || q->objDepth < 0 || !HrOn()) return 0;
    if (q->w != g_hrSpr[q->riderSprite].w || q->h != g_hrSpr[q->riderSprite].h) return 0;   /* (another sprite set than the pictures': LoBob) */
    wpn = (q->objKind == RRO_PLAYER && WpClubSprite(q->riderSprite)) ? WpPicture() : 0;
    if (!(p = HrPick(q, &pid))) return 0;
    if (wpn > 0 && pid != q->riderSprite) { if (!(p = HrSprite(q->riderSprite))) return 0; pid = q->riderSprite; }   /* (the weapon goes on B's own picture only) */
    HrColours(q, c);
    key = 2166136261u; for (k = 1; k < HR_PARTS; k++) key = (key ^ c[k]) * 16777619u; key = (key ^ (DWORD)pid) * 16777619u; key = (key ^ (DWORD)(wpn > 0 ? wpn : 0)) * 16777619u;
    if (!(tx = HrTexFor(p, q->riderSprite, key, c, wpn > 0 ? wpn : 0))) return 0;
    q2 = *q;
    if (p->ox || p->oy || p->w != q->w || p->h != q->h) {   /* an in-between on its canvas: the quad extended over it (bilinear in the tile's texel coordinates) */
        float uL = q->u[0], uR = q->u[0], vT = q->v[0], vB = q->v[0], du, dv;
        for (k = 1; k < 4; k++) { if (q->u[k] < uL) uL = q->u[k]; if (q->u[k] > uR) uR = q->u[k]; if (q->v[k] < vT) vT = q->v[k]; if (q->v[k] > vB) vB = q->v[k]; }
        du = uR - uL; dv = vB - vT; if (du < 0.5f || dv < 0.5f) return 0;
        {   int c00 = 0, c10 = 0, c11 = 0, c01 = 0;   /* the corners at (s, t) = (0, 0), (1, 0), (1, 1), (0, 1) */
            for (k = 0; k < 4; k++) { int s = q->u[k] > (uL + uR) * 0.5f, t = q->v[k] > (vT + vB) * 0.5f; if (!s && !t) c00 = k; else if (s && !t) c10 = k; else if (s && t) c11 = k; else c01 = k; }
            for (k = 0; k < 4; k++) { int s = q->u[k] > (uL + uR) * 0.5f, t = q->v[k] > (vT + vB) * 0.5f;
                float U = s ? uR + (p->w - p->ox - q->w) : uL - p->ox, V = t ? vB + (p->h - p->oy - q->h) : vT - p->oy, S = (U - uL) / du, T = (V - vT) / dv;
                q2.x[k] = (1 - S) * (1 - T) * q->x[c00] + S * (1 - T) * q->x[c10] + S * T * q->x[c11] + (1 - S) * T * q->x[c01];
                q2.y[k] = (1 - S) * (1 - T) * q->y[c00] + S * (1 - T) * q->y[c10] + S * T * q->y[c11] + (1 - S) * T * q->y[c01];
                q2.u[k] = U + p->ox; q2.v[k] = V + p->oy; } } }
    return W3Sprite(&q2, tx); }
