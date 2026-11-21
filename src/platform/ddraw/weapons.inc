/* weapons.inc - True 3D races: the riders' weapon (the overlay picture the game draws over a rider while he swings, render\rider_sprites.c
 * rr_PlayerSpriteDraw -> RR_WeaponFrame) can be drawn as another object.  Included by rrddraw.c after weather.inc.  Drawing only.
 * Test switches: RR_WPLOG=1 logs the weapon records and the player's pictures; RR_WPDUMP=<folder> writes those pictures as .bmp. */
static int WpLogOn(void) { static int v = -1; if (v < 0) v = getenv("RR_WPLOG") != NULL; return v; }
static void WpDump(const char *tag, const BYTE *bits, int w, int h, int pitch)
{   const char *dir = getenv("RR_WPDUMP"); char fn[MAX_PATH]; FILE *o; BITMAPFILEHEADER bh; BITMAPINFOHEADER ih; int x, y; const PALETTEENTRY *pe;
    if (!dir || !bits || w <= 0 || h <= 0 || !g_primary || !g_primary->pal) return;
    pe = g_primary->pal->e;
    _snprintf(fn, sizeof fn, "%s\\%s_%p_%dx%d.bmp", dir, tag, (const void *)bits, w, h); fn[sizeof fn - 1] = 0;
    if (!(o = fopen(fn, "wb"))) return;
    memset(&bh, 0, sizeof bh); memset(&ih, 0, sizeof ih); bh.bfType = 0x4d42; bh.bfOffBits = sizeof bh + sizeof ih; bh.bfSize = bh.bfOffBits + w * h * 4;
    ih.biSize = sizeof ih; ih.biWidth = w; ih.biHeight = h; ih.biPlanes = 1; ih.biBitCount = 32;
    fwrite(&bh, sizeof bh, 1, o); fwrite(&ih, sizeof ih, 1, o);
    for (y = h - 1; y >= 0; y--) for (x = 0; x < w; x++) { int p = bits[y * (pitch ? pitch : w) + x]; DWORD c = p ? ((DWORD)pe[p].peRed << 16) | ((DWORD)pe[p].peGreen << 8) | pe[p].peBlue : 0xff00ff; fwrite(&c, 4, 1, o); }
    fclose(o); }
/* the game: this frame's weapon overlay of a rider (spr = the sprite record SpriteQueue gets, n = frame 1.., key = the weapon's kind) */
__declspec(dllexport) void __stdcall RR_WeaponFrame(const void *spr, int n, int key)
{   static const void *seen[128]; static int ns; int i;
    if (!WpLogOn() || !spr) return;
    for (i = 0; i < ns; i++) if (seen[i] == spr) return;
    if (ns < 128) seen[ns++] = spr;
    {   const int *w = (const int *)spr; const int *src = (const int *)w[2];
        LOG("weapon spr %p n %d key %d: %08x %08x %08x %08x  src %08x %08x", spr, n, key, w[0], w[1], w[2], w[3], src ? src[0] : 0, src ? src[1] : 0); } }
/* every distinct picture drawn for the player (W3Sprite) - to find which one the weapon is */
static void WpSeePlayer(const GpuTex *tx)
{   static const BYTE *seen[512]; static int ns; int i;
    if (!WpLogOn() || !tx || !tx->bits) return;
    for (i = 0; i < ns; i++) if (seen[i] == tx->bits) return;
    if (ns < 512) seen[ns++] = tx->bits;
    LOG("player picture %p %dx%d", (const void *)tx->bits, tx->w, tx->h);
    WpDump("pl", tx->bits, tx->w, tx->h, tx->pitch); }

/* ---- the weapons to choose from (Esc pause: WEAPON): 0 = the game's own, 1 = a hammer (made here), then every picture in mod\weapons\*.png (any
   size, the object drawn upright: grip at the bottom, the striking end at the top; transparent background) ---- */
#define WP_MAX 32
typedef struct WpImg { char name[40]; int w, h; DWORD *px; } WpImg;   /* px: ARGB, row 0 = top */
static WpImg g_wp[WP_MAX]; static int g_wpN = -1;
static void WpHammer(WpImg *m)   /* a claw hammer: wooden handle, steel head across the top */
{   int W = 40, H = 112, x, y; DWORD *p = (DWORD *)calloc((size_t)W * H, 4);
    strcpy(m->name, "Hammer"); m->w = W; m->h = H; m->px = p; if (!p) return;
    for (y = 22; y < H; y++) for (x = 16; x < 24; x++) {   /* the handle: varnished wood, a darker grip near the end */
        int g = ((x * 7 + y * 3) % 11) < 2 ? -18 : 0, r = 150 + g + (x == 16 || x == 23 ? -40 : x == 18 ? 30 : 0), gg = 98 + g + (x == 18 ? 20 : 0), b = 52 + g;
        if (y > H - 30) { r = 40 + (x == 18 ? 25 : 0); gg = 36; b = 34; if ((y / 3) & 1) { r -= 10; gg -= 10; b -= 10; } }
        p[y * W + x] = 0xff000000u | ((DWORD)r << 16) | ((DWORD)gg << 8) | (DWORD)b; }
    for (y = 6; y < 24; y++) for (x = 2; x < 38; x++) {   /* the head: a face on the left, the split claw curving down on the right */
        int in = 0, shade;
        if (x < 24) in = y >= 6 && y < 24 && !(x < 6 && (y < 8 || y > 21));
        else { float cx = (x - 24) / 14.0f; int top = 6 + (int)(cx * cx * 10), bot = 15 + (int)(cx * cx * 12); in = y >= top && y < bot && !(x > 30 && y > top + 3 && y < bot - 2 && (x & 1)); }
        if (!in) continue;
        shade = 150 + (24 - y) * 3 - (x < 6 ? 30 : 0) + (y < 9 ? 40 : 0);
        if (shade > 235) shade = 235;
        p[y * W + x] = 0xff000000u | ((DWORD)(shade - 6) << 16) | ((DWORD)shade << 8) | (DWORD)(shade + 12 > 255 ? 255 : shade + 12); } }
static void WpTireIron(WpImg *m)   /* a tire iron: a long black steel bar, the lug wrench socket at the top bent off to one side, a flat pry end at the bottom */
{   int W = 36, H = 124, x, y; DWORD *p = (DWORD *)calloc((size_t)W * H, 4);
    strcpy(m->name, "Tire Iron"); m->w = W; m->h = H; m->px = p; if (!p) return;
    for (y = 20; y < H - 2; y++) for (x = 15; x < 21; x++) {   /* the bar: dark steel with a highlight down one side */
        int s = 58 + (x == 16 ? 70 : x == 17 ? 40 : x == 20 ? -18 : 0) + ((y * 5) % 17 < 2 ? 12 : 0);
        p[y * W + x] = 0xff000000u | ((DWORD)s << 16) | ((DWORD)s << 8) | (DWORD)(s + 10); }
    for (y = H - 12; y < H; y++) for (x = 13; x < 23; x++) if (x - 13 < (H - y) + 2 && 22 - x < (H - y) + 2) {   /* the flat pry end */
        int s = 90 + (y & 1) * 10; p[y * W + x] = 0xff000000u | ((DWORD)s << 16) | ((DWORD)s << 8) | (DWORD)(s + 8); }
    for (y = 2; y < 26; y++) for (x = 4; x < 32; x++) {   /* the bend and the socket: a short arm out to the left with the hex socket at its end */
        float bx = 18 - (26 - y) * 0.45f, d = fabsf(x - bx); int in = d < 3.2f && y >= 8, sock = x >= 4 && x < 15 && y >= 2 && y < 13; int s;
        if (!in && !sock) continue;
        s = sock ? ((x == 4 || x == 14 || y == 2 || y == 12) ? 40 : 105 + (x - 4) * 6) : 70 + (d < 1 ? 60 : 0);
        p[y * W + x] = 0xff000000u | ((DWORD)s << 16) | ((DWORD)s << 8) | (DWORD)(s + 10); }
    for (y = 5; y < 10; y++) for (x = 7; x < 12; x++) p[y * W + x] = 0xff141418u; }   /* (the socket's hole) */
static void WpList(void)
{   char dirs[2][MAX_PATH]; int nd = 0, d;
    if (g_wpN >= 0) return;
    g_wpN = 0; strcpy(g_wp[g_wpN].name, "Original"); g_wpN++;   /* (0: the game's own picture) */
    WpHammer(&g_wp[g_wpN]); if (g_wp[g_wpN].px) g_wpN++;
    WpTireIron(&g_wp[g_wpN]); if (g_wp[g_wpN].px) g_wpN++;   /* (2: Hurt Hardware's tire iron - arsenal_api.h pic numbers) */
    if (GetModuleFileNameA(NULL, dirs[nd], MAX_PATH - 20)) { char *p = strrchr(dirs[nd], 0x5C); if (p) { strcpy(p + 1, "mod\\weapons"); nd++; } }
    strcpy(dirs[nd++], "mod\\weapons");
    for (d = 0; d < nd; d++) { WIN32_FIND_DATAA fd; char pat[MAX_PATH]; HANDLE h; _snprintf(pat, sizeof pat, "%s\\*.png", dirs[d]); pat[sizeof pat - 1] = 0;
        if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) continue;
        do { char path[MAX_PATH]; SDL_Surface *s, *c; WpImg *m; int y, k, dup = 0;
            if (g_wpN >= WP_MAX) break;
            for (k = 1; k < g_wpN; k++) if (!_strnicmp(g_wp[k].name, fd.cFileName, sizeof g_wp[k].name - 1)) dup = 1;
            if (dup) continue;
            _snprintf(path, sizeof path, "%s\\%s", dirs[d], fd.cFileName); path[sizeof path - 1] = 0;
            if (!(s = SDL_LoadPNG(path))) continue;
            c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888); SDL_DestroySurface(s); if (!c) continue;
            m = &g_wp[g_wpN]; memset(m, 0, sizeof *m); lstrcpynA(m->name, fd.cFileName, sizeof m->name); { char *e = strrchr(m->name, '.'); if (e) *e = 0; }
            m->w = c->w; m->h = c->h; m->px = (DWORD *)malloc((size_t)c->w * c->h * 4);
            if (m->px) { for (y = 0; y < c->h; y++) memcpy(m->px + y * c->w, (BYTE *)c->pixels + (size_t)y * c->pitch, (size_t)c->w * 4); g_wpN++; LOG("weapon picture: %s (%dx%d)", path, c->w, c->h); }
            SDL_DestroySurface(c); } while (FindNextFileA(h, &fd));
        FindClose(h); }
    LOG("weapons: %d to choose from", g_wpN); }
static int WpCount(void) { WpList(); return g_wpN; }
static const char *WpName(int i) { WpList(); return i >= 0 && i < g_wpN ? g_wp[i].name : "Original"; }
/* the player's weapon as chosen in the Esc pause (WEAPON): 0 = the game decides (Original); any other choice gives the player the game's weapon 1 - the
   pipe - whose pictures are then drawn as the chosen object (race\player_control.c rr_PlayerControlTick sets it when this changes and at a race start) */
static int WpChoice(void) { static int t = -2; if (t == -2) { const char *e = getenv("RR_WEAPON"); t = e ? atoi(e) : -1; } return t >= 0 ? t : g_vid.weapon; }   /* (test switch RR_WEAPON=<n>: that one) */
__declspec(dllexport) int __stdcall RR_PlayerWeapon(void) { return WpChoice() > 0 ? 1 : 0; }
/* a Big Game: the player's weapons (src\race\arsenal.c, bound at start-up) decide instead - the carried one's class and picture; the Esc pause row
   WEAPON goes through the weapons the player owns (the game gives the player the new one's class when it changes) */
static RrArsenal *g_ars;
__declspec(dllexport) void __stdcall RR_ArsenalBind(RrArsenal *a) { g_ars = a; }
static int WpBigGame(void) { return g_ars && g_ars->bigGame; }
static int WpPicture(void)   /* the picture over the player's club (0 = the game's own) */
{   if (WpBigGame()) { int w = g_ars->s.equipped; return w < RRA_N && g_rraWeapons[w].cls == 1 ? g_rraWeapons[w].pic : 0; }
    return WpChoice(); }
static int WpTorch(void) { return WpBigGame() && (g_ars->s.charms >> RRC_TORCH & 1); }   /* the Miner's Lamp charm (weather.inc WxCompute, world3d.inc headlight) */
static const char *WpRowName(void) { return WpBigGame() ? g_rraWeapons[g_ars->s.equipped < RRA_N ? g_ars->s.equipped : 0].name : WpName(g_vid.weapon); }
static void WpRowStep(int d)
{   if (WpBigGame()) { int w = g_ars->s.equipped, k; for (k = 0; k < RRA_N; k++) { w = (w + d + RRA_N) % RRA_N; if (RraOwns(&g_ars->s, w)) break; } g_ars->s.equipped = (unsigned char)w; g_ars->unsaved = 1; return; }
    { int n = WpCount(); g_vid.weapon = n > 0 ? (g_vid.weapon + d + n) % n : 0; } }
/* the club in the riders' swing sprites (HiBob 140..142 one side, 159..161 the other; found with RR_WPSWING + RR_WPLOG, ends picked on
   analysis\tools\rider_club.ps1's pictures): sprite size, grip and tip in sprite pixels */
static const struct { int spr, sw, sh; float gx, gy, tx, ty; } g_wpClub[] = {
    { 140,  64, 162,  7, 21, 36,  0 }, { 141,  56, 156,  8, 12, 43,  0 }, { 142, 130, 132, 33, 46,  0, 36 },
    { 159,  62, 152, 47, 19, 16,  0 }, { 160,  60, 152, 37,  9,  0,  1 }, { 161, 128, 132, 98, 45, 126, 37 } };
static int WpClubSprite(int spr) { int i; for (i = 0; i < (int)(sizeof g_wpClub / sizeof g_wpClub[0]); i++) if (g_wpClub[i].spr == spr) return 1; return 0; }
/* the player's swing picture px (w x h ARGB, alpha 0 = see-through): the club taken out (every pixel within 2 of its line) and weapon wpn's picture
   drawn along the same line - upright picture, grip at the bottom, its length the club's + 8 % - 3 x 3 samples a pixel, opaque where half covered */
static void WpClubSwap(DWORD *px, int w, int h, int spr, int wpn)
{   int i, x, y, x0, x1, y0, y1; float sx, sy, gx, gy, tx, ty, ax, ay, L, Lw, nx, ny, pad; const WpImg *m;
    WpList(); if (wpn <= 0 || wpn >= g_wpN || !(m = &g_wp[wpn])->px || m->w <= 0 || m->h <= 0) return;
    for (i = 0; i < (int)(sizeof g_wpClub / sizeof g_wpClub[0]) && g_wpClub[i].spr != spr; i++) {}
    if (i == (int)(sizeof g_wpClub / sizeof g_wpClub[0])) return;
    sx = (float)w / g_wpClub[i].sw; sy = (float)h / g_wpClub[i].sh;   /* (the tile at another size than the sprite) */
    gx = g_wpClub[i].gx * sx + 0.5f; gy = g_wpClub[i].gy * sy + 0.5f; tx = g_wpClub[i].tx * sx + 0.5f; ty = g_wpClub[i].ty * sy + 0.5f;
    ax = tx - gx; ay = ty - gy; L = sqrtf(ax * ax + ay * ay); if (L < 4) return;
    ax /= L; ay /= L; nx = -ay; ny = ax; Lw = L * 1.08f;
    pad = Lw * m->w / m->h * 0.5f + 3;
    x0 = (int)floorf((gx < tx ? gx : tx) - pad); x1 = (int)ceilf((gx > tx ? gx : tx) + pad); y0 = (int)floorf((gy < ty ? gy : ty) - pad); y1 = (int)ceilf((gy > ty ? gy : ty) + pad);
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > w - 1) x1 = w - 1; if (y1 > h - 1) y1 = h - 1;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) {   /* the club out */
        float dx = x + 0.5f - gx, dy = y + 0.5f - gy, t = dx * ax + dy * ay, s = dx * nx + dy * ny;
        if (t > -1.0f && t < L + 2.0f * sx && fabsf(s) <= 2.0f * sx) px[y * w + x] = 0; }   /* (sx: an HD rider's 4x picture, hdrider.inc) */
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) {   /* the weapon in */
        int k, n = 0; float r = 0, g = 0, b = 0;
        for (k = 0; k < 9; k++) {
            float dx = x + (k % 3 + 0.5f) / 3 - gx, dy = y + (k / 3 + 0.5f) / 3 - gy, t = (dx * ax + dy * ay) / Lw, s = (dx * nx + dy * ny) / Lw * m->h;
            int u = (int)floorf(s + m->w * 0.5f), v = (int)floorf((1 - t) * m->h); DWORD c;
            if (u < 0 || u >= m->w || v < 0 || v >= m->h) continue;
            c = m->px[v * m->w + u]; if ((c >> 24) < 128) continue;
            r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
        if (n >= 5) px[y * w + x] = 0xff000000u | ((DWORD)(r / n) << 16) | ((DWORD)(g / n) << 8) | (DWORD)(b / n); } }
