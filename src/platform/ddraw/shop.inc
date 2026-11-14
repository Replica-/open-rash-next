/* "Hurt Hardware" - the shop on The Street (included by rrddraw.c after modscfg.inc; user, 2026-11-14).  The game opens it from The Street's fourth
 * item (src\race\arsenal.c rr_StreetShop -> RR_WeaponShop); the state - what the player owns, the upgrades, the weapon carried - is the game's g_arsenal
 * (bound at start-up: weapons.inc RR_ArsenalBind), the lists and prices are platform\arsenal_api.h.
 *
 *   tabs        WEAPONS (Bare / Brass Knuckles, Billy Club, Claw Hammer, Tire Iron, Bike Chain), GEAR (Boots, Armoured Vest, Insurance),
 *               SUPPLIES (Medikit, Adrenaline Shot, Police Scanner, Nitro Pack), CHARMS (four)
 *   the grid    the tab's goods as thumbnail tiles (3 x 2): the picture, the name, a badge (price, OWNED / CARRYING, how many)
 *   the panel   for the tile picked: name, what it does, its bars, and the buttons - a weapon's Buy / Carry, damage, reach, backhand boost; the boots'
 *               two upgrades; the vest; the insurance plans; a supply's Buy 1 / Buy 3; a charm's Buy (label left, price right)
 *   Leave the shop (under the grid)
 * Keys: arrows move over the grid (Left / Right past its edge: the next counter, Tab too), Down from the bottom row = Leave; Enter on a tile steps into
 * its buttons (Up / Down there, Enter does it), Left or Esc steps back; Esc on the grid leaves.  The mouse picks any tile, button or tab.
 * Same look as the other own screens (Game Options, the message box).  Purchases take the player's cash at once; the game marks the player file as
 * changed.  RR_TEST_SHOP=<tab> + RR_DUMP_DIR: that tab shown, the screen saved as <RR_DUMP_DIR>\shop.bmp and closed.
 */
enum { G_W0 = 0, G_BOOTS = RRA_N, G_VEST, G_INS, G_MED, G_ADR, G_SCAN, G_NOS, G_CH0, G_N = G_CH0 + RRC_N };
static const char *const g_shTab[4] = { "WEAPONS", "GEAR", "SUPPLIES", "CHARMS" };
static const int g_shTabFirst[5] = { G_W0, G_BOOTS, G_MED, G_CH0, G_N };
#define SH_A 100       /* sel: action row SH_A + j */
#define SH_LEAVE 200   /* sel: Leave the shop */
typedef struct ShState { int tab, sel, cur; DWORD msgT; char msg[120]; } ShState;   /* sel = goods row k (0..), SH_A + j or SH_LEAVE; cur = the goods shown */
static int ShGoodsY(int k) { return 112 + k * 26; }
static int ShActY(int j) { return 256 + j * 24; }
#define SH_LEAVE_Y 356
static int ShIsAction(int sel) { return sel >= SH_A && sel < SH_LEAVE; }
static int ShWeapon(int g) { return g < RRA_N; }
static int ShCharm(int g) { return g >= G_CH0 && g < G_N; }
static int ShSupply(int g) { return g >= G_MED && g <= G_NOS; }
static int ShTabOf(int g) { int t; for (t = 0; t < 4 && g >= g_shTabFirst[t + 1]; t++) {} return t; }
static int ShGoodsN(int tab) { return g_shTabFirst[tab + 1] - g_shTabFirst[tab]; }

static void ShMoney(char *b, size_t n, int v) { if (v >= 1000) _snprintf(b, n, "$%d,%03d", v / 1000, v % 1000); else _snprintf(b, n, "$%d", v); b[n - 1] = 0; }
static int ShCash(void) { return g_ars && g_ars->cash ? *g_ars->cash : 0; }
static void ShMsg(ShState *s, const char *t) { strncpy(s->msg, t, sizeof s->msg - 1); s->msg[sizeof s->msg - 1] = 0; s->msgT = GetTickCount(); }

static const char *ShName(int g)
{   static const char *const n[] = { "Boots", "Armoured Vest", "Insurance", "Medikit", "Adrenaline Shot", "Police Scanner", "Nitro Pack" };
    if (ShWeapon(g)) return RraName(&g_ars->s, g);
    if (ShCharm(g)) return g_rraCharms[g - G_CH0].name;
    return n[g - G_BOOTS]; }
static int ShSupplyPrice(int g) { return g == G_MED ? RRA_MEDIKIT_PRICE : g == G_ADR ? RRA_ADREN_PRICE : g == G_SCAN ? RRA_SCAN_PRICE : RRA_NITRO_PRICE; }
static int ShSupplyMax(int g) { return g == G_MED ? RRA_MEDIKIT_MAX : g == G_ADR ? RRA_ADREN_MAX : g == G_SCAN ? RRA_SCAN_MAX : RRA_NITRO_MAX; }
static unsigned char *ShSupplyHave(int g) { RrArsenalSave *a = &g_ars->s; return g == G_MED ? &a->medikits : g == G_ADR ? &a->adrenaline : g == G_SCAN ? &a->scanners : &a->nitroPacks; }
static const char *ShBlurb(int g)
{   static char b[200];
    if (ShWeapon(g)) return g == 0 && g_ars->s.dmgLv[0] ? "Knuckle dusters: punches that land like a hammer.  Nobody can knock them out of your hand." : g_rraWeapons[g].blurb;
    if (ShCharm(g)) return g_rraCharms[g - G_CH0].blurb;
    switch (g) {
    case G_BOOTS: return "Your kicks.  Steel toes make them hurt; spring heels shove riders off the road.";
    case G_VEST: return "Kevlar under the leather: every hit you take does a quarter less.";
    case G_INS: return "Get busted or total your bike and the insurance pays the bill - all but the excess.  The premium comes off after every race.";
    case G_MED: _snprintf(b, sizeof b, "Gauze, tape and something that might be morphine.  Press %c in a race: full health again.", g_ars->medKey ? g_ars->medKey : 'H'); b[sizeof b - 1] = 0; return b;
    case G_ADR: _snprintf(b, sizeof b, "Press %c in a race: ten seconds of hitting half as hard again and feeling half the pain.", g_ars->adrKey ? g_ars->adrKey : 'J'); b[sizeof b - 1] = 0; return b;
    case G_SCAN: return "Tuned to the police band for one race: the cops on your course map and how far off they are.";
    default: return "A bottle of the good stuff: three more nitro shots, used after your bike's own.  No nitro on the bike, no use."; } }
static int ShBadge(int g, char *b, size_t n, COLORREF *c)   /* the goods row's right column */
{   const RrArsenalSave *a = &g_ars->s; char m[24];
    *c = RGB(235, 235, 240);
    if (ShWeapon(g)) {
        if (a->equipped == g) { _snprintf(b, n, "CARRYING"); *c = RGB(140, 235, 120); }
        else if (RraOwns(a, g)) { _snprintf(b, n, "OWNED"); *c = RGB(150, 200, 255); }
        else { ShMoney(b, n, g_rraWeapons[g].price); if (g_rraWeapons[g].price > ShCash()) *c = RGB(200, 110, 100); } }
    else if (ShCharm(g)) { if (a->charms >> (g - G_CH0) & 1) { _snprintf(b, n, "OWNED"); *c = RGB(150, 200, 255); } else ShMoney(b, n, g_rraCharms[g - G_CH0].price); }
    else if (ShSupply(g)) { ShMoney(m, sizeof m, ShSupplyPrice(g)); _snprintf(b, n, "%d/%d   %s", *ShSupplyHave(g), ShSupplyMax(g), m); if (*ShSupplyHave(g)) *c = RGB(150, 200, 255); }
    else if (g == G_BOOTS) _snprintf(b, n, "%d/%d  %d/%d", a->bootDmg, RRA_MAXLV, a->bootPush, RRA_MAXLV);
    else if (g == G_VEST) { if (a->vest) { _snprintf(b, n, "WEARING"); *c = RGB(140, 235, 120); } else ShMoney(b, n, RRA_VEST_PRICE); }
    else { _snprintf(b, n, "%s", g_rraPlans[a->insurance].name); if (a->insurance) *c = RGB(140, 235, 120); }
    b[n - 1] = 0; return 1; }

/* the action buttons of goods g: label, price ("" = none), usable now (1) or not (0); returns how many */
static int ShActions(int g, char t[4][48], char pr[4][16], int en[4])
{   const RrArsenalSave *a = &g_ars->s; int n = 0, i;
    for (i = 0; i < 4; i++) t[i][0] = pr[i][0] = 0;
    if (ShWeapon(g)) {
        static const char *const what[3] = { "Damage", "Reach", "Backhand" };
        if (g == 0) { _snprintf(t[n], 48, a->equipped == 0 ? "Carrying" : "Carry fists"); en[n++] = a->equipped != 0; }
        else if (!RraOwns(a, g)) { _snprintf(t[n], 48, "Buy"); ShMoney(pr[n], 16, g_rraWeapons[g].price); en[n++] = 1; }
        else if (a->equipped == g) { _snprintf(t[n], 48, "Carrying"); en[n++] = 0; }
        else { _snprintf(t[n], 48, "Carry it"); en[n++] = 1; }
        for (i = 0; i < 3; i++) {
            int lv = i == 0 ? a->dmgLv[g] : i == 1 ? a->reachLv[g] : a->backLv[g];
            _snprintf(t[n], 48, "%s %d/%d", g == 0 && i == 0 ? "Brass" : what[i], lv, RRA_MAXLV);
            if (lv >= RRA_MAXLV) { _snprintf(pr[n], 16, "max"); en[n++] = 0; continue; }
            ShMoney(pr[n], 16, i == 2 ? RRA_BACK_PRICE(lv) : RraUpgradeCost(g, lv)); en[n++] = RraOwns(a, g); } }
    else if (g == G_BOOTS) {
        int lv[2] = { a->bootDmg, a->bootPush }; static const char *const what[2] = { "Steel toes", "Spring heels" };
        for (i = 0; i < 2; i++) { _snprintf(t[n], 48, "%s %d/%d", what[i], lv[i], RRA_MAXLV);
            if (lv[i] >= RRA_MAXLV) { _snprintf(pr[n], 16, "max"); en[n++] = 0; } else { ShMoney(pr[n], 16, RRA_BOOT_PRICE(lv[i])); en[n++] = 1; } } }
    else if (g == G_VEST) { if (a->vest) { _snprintf(t[n], 48, "Wearing it"); en[n++] = 0; } else { _snprintf(t[n], 48, "Buy"); ShMoney(pr[n], 16, RRA_VEST_PRICE); en[n++] = 1; } }
    else if (g == G_INS) {
        for (i = 1; i <= 3; i++) { char e[16]; ShMoney(e, sizeof e, g_rraPlans[i].excess); _snprintf(t[n], 48, "%s%s, excess %s", a->insurance == i ? "* " : "", g_rraPlans[i].name, e);
            ShMoney(pr[n], 16, g_rraPlans[i].premium); strcat(pr[n], "/race"); en[n++] = a->insurance != i; }
        _snprintf(t[n], 48, "Cancel cover"); en[n++] = a->insurance != 0; }
    else if (ShSupply(g)) {
        int have = *ShSupplyHave(g), mx = ShSupplyMax(g), k;
        for (k = 1; k <= 3; k += 2) { _snprintf(t[n], 48, "Buy %d", k); ShMoney(pr[n], 16, ShSupplyPrice(g) * k); en[n++] = have + k <= mx; } }
    else {
        int c = g - G_CH0;
        if (a->charms >> c & 1) { _snprintf(t[n], 48, "Owned - it works"); en[n++] = 0; }
        else { _snprintf(t[n], 48, "Buy"); ShMoney(pr[n], 16, g_rraCharms[c].price); en[n++] = 1; } }
    for (i = 0; i < n; i++) { t[i][47] = 0; pr[i][15] = 0; }
    return n; }
/* the pictures: the club and the chain made here, the hammer and the tire iron are weapons.inc's (g_wp 1 / 2) */
static WpImg g_shClub, g_shChain;
static void ShMakeClub(WpImg *m)   /* a billy club: black lacquered hardwood, a ridged grip, the wrist strap */
{   int W = 30, H = 120, x, y; DWORD *p = (DWORD *)calloc((size_t)W * H, 4);
    strcpy(m->name, "Billy Club"); m->w = W; m->h = H; m->px = p; if (!p) return;
    for (y = 4; y < H - 6; y++) { float r = y < 84 ? 6.5f - (84 - y) * 0.012f : 5.0f; for (x = 0; x < W; x++) { float d = fabsf(x + 0.5f - 15); int s;
        if (d > r) continue; s = 34 + (int)((1 - d / r) * 30) + (x == 12 || x == 13 ? 55 : 0); if (y >= 84 && (y / 3) & 1) s -= 14; if (y < 7) s += 20;
        p[y * W + x] = 0xff000000u | ((DWORD)(s + 6) << 16) | ((DWORD)s << 8) | (DWORD)(s - 4 > 0 ? s - 4 : 0); } }
    for (y = H - 6; y < H; y++) for (x = 9; x < 21; x++) p[y * W + x] = 0xff3a2a1cu;   /* (the end cap) */
    for (y = 96; y < 118; y++) for (x = 0; x < W; x++) { float ex = (x - 15) / 13.0f, ey = (y - 107) / 11.0f, e = ex * ex + ey * ey; if (e > 0.75f && e < 1.0f && x < 9) p[y * W + x] = 0xff6b4220u; } }
static void ShMakeChain(WpImg *m)   /* a bike chain hanging down: steel links, side plates alternating with rollers */
{   int W = 26, H = 124, x, y, k; DWORD *p = (DWORD *)calloc((size_t)W * H, 4);
    strcpy(m->name, "Bike Chain"); m->w = W; m->h = H; m->px = p; if (!p) return;
    for (k = 0; k < 11; k++) { int cy = 6 + k * 11; float sway = sinf(k * 0.6f) * 3; for (y = cy - 7; y <= cy + 7; y++) for (x = 0; x < W; x++) {
        float ex = (x - 13 - sway) / ((k & 1) ? 6.0f : 8.0f), ey = (y - cy) / 7.0f, e = ex * ex + ey * ey; int s;
        if (y < 0 || y >= H || e > 1 || e < ((k & 1) ? 0.30f : 0.42f)) continue;
        s = 120 + (int)((1 - e) * 90) - (k & 1) * 30 + (ex < -0.3f ? 40 : 0); if (s > 235) s = 235;
        p[y * W + x] = 0xff000000u | ((DWORD)s << 16) | ((DWORD)s << 8) | (DWORD)(s + 12 > 255 ? 255 : s + 12); } } }

/* small drawing helpers for the other pictures (ARGB, row 0 = top): filled ellipses, boxes and thick lines with a light from the upper left */
static DWORD ShRgb(int r, int g, int b) { r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b; return 0xff000000u | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b; }
static int ShNew(WpImg *m, const char *name, int w, int h) { strcpy(m->name, name); m->w = w; m->h = h; m->px = (DWORD *)calloc((size_t)w * h, 4); return m->px != NULL; }
static void ShEll(WpImg *m, float cx, float cy, float rx, float ry, int r, int g, int b)   /* a shaded ellipse */
{   int x, y; for (y = 0; y < m->h; y++) for (x = 0; x < m->w; x++) { float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, e = dx * dx + dy * dy; int s;
        if (e > 1) continue; s = (int)(40 * (1 - e) - 30 * (dx + dy) * 0.5f); m->px[y * m->w + x] = ShRgb(r + s, g + s, b + s); } }
static void ShBox(WpImg *m, int x0, int y0, int x1, int y1, int r, int g, int b)   /* a shaded box: lighter top / left edge */
{   int x, y; for (y = y0; y < y1 && y < m->h; y++) for (x = x0; x < x1 && x < m->w; x++) { int s = (y == y0 || x == x0) ? 40 : (y == y1 - 1 || x == x1 - 1) ? -40 : (y1 - y) * 20 / (y1 - y0 + 1);
        if (x >= 0 && y >= 0) m->px[y * m->w + x] = ShRgb(r + s, g + s, b + s); } }
static void ShLine(WpImg *m, float x0, float y0, float x1, float y1, float wd, int r, int g, int b)   /* a thick line, rounded ends */
{   int x, y; float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    for (y = 0; y < m->h; y++) for (x = 0; x < m->w; x++) { float px = x + 0.5f - x0, py = y + 0.5f - y0, t = l2 > 0 ? (px * dx + py * dy) / l2 : 0, qx, qy, d;
        if (t < 0) t = 0; if (t > 1) t = 1; qx = px - t * dx; qy = py - t * dy; d = sqrtf(qx * qx + qy * qy);
        if (d <= wd * 0.5f) { int s = (int)(30 * (1 - d / (wd * 0.5f + 0.01f))); m->px[y * m->w + x] = ShRgb(r + s, g + s, b + s); } } }
static WpImg g_shPic[G_N];
static void ShMakePic(int g, WpImg *m)
{   int i;
    switch (g) {
    case 0:   /* brass knuckles (the upgraded fists): four rings over a grip bar */
        if (!ShNew(m, "Brass", 96, 60)) return;
        ShBox(m, 8, 34, 88, 50, 190, 150, 60);
        for (i = 0; i < 4; i++) { ShEll(m, 18 + i * 20.0f, 22, 11, 13, 200, 160, 70); ShEll(m, 18 + i * 20.0f, 22, 6, 8, 0, 0, 0); }
        for (i = 0; i < m->w * m->h; i++) if (m->px[i] == 0xff000000u) m->px[i] = 0;   /* (the finger holes see-through) */
        break;
    case G_BOOTS:   /* a biker boot: black leather shaft, a steel toe cap, a thick heel */
        if (!ShNew(m, "Boots", 80, 100)) return;
        ShBox(m, 20, 4, 50, 66, 52, 42, 36); ShBox(m, 18, 2, 52, 8, 70, 58, 48);   /* (the shaft, its collar) */
        ShEll(m, 44, 74, 28, 14, 52, 42, 36); ShBox(m, 20, 60, 50, 84, 52, 42, 36); ShEll(m, 64, 77, 10, 9, 140, 145, 155);   /* (the foot, the steel toe cap) */
        ShBox(m, 14, 84, 78, 92, 28, 24, 22); ShBox(m, 16, 92, 34, 100, 34, 30, 26);   /* (the sole, the heel) */
        for (i = 0; i < 5; i++) ShLine(m, 26, 14 + i * 10.0f, 44, 18 + i * 10.0f, 2, 150, 130, 100);   /* (the laces) */
        ShLine(m, 52, 30, 52, 62, 3, 170, 170, 180);   /* (the side buckle strap) */
        break;
    case G_VEST:   /* an armoured vest: dark panels, straps, plates */
        if (!ShNew(m, "Vest", 90, 104)) return;
        ShBox(m, 12, 22, 78, 100, 48, 56, 44); ShBox(m, 12, 4, 30, 26, 48, 56, 44); ShBox(m, 60, 4, 78, 26, 48, 56, 44);
        for (i = 0; i < 3; i++) { ShBox(m, 18, 34 + i * 22, 42, 52 + i * 22, 70, 78, 64); ShBox(m, 48, 34 + i * 22, 72, 52 + i * 22, 70, 78, 64); }
        ShBox(m, 44, 22, 46, 100, 20, 20, 20);
        break;
    case G_INS:   /* insurance: a shield with a tick */
        if (!ShNew(m, "Insurance", 90, 104)) return;
        ShEll(m, 45, 40, 38, 36, 40, 90, 170); ShLine(m, 10, 42, 45, 100, 6, 40, 90, 170); ShLine(m, 80, 42, 45, 100, 6, 40, 90, 170);
        for (i = 40; i < 100; i++) { int x; float hw = 36 * (1 - (i - 40) / 62.0f); for (x = (int)(45 - hw); x < (int)(45 + hw); x++) if (x >= 0 && x < 90) m->px[i * 90 + x] = ShRgb(40, 90 - (i - 40) / 3, 170 - (i - 40)); }
        ShLine(m, 26, 50, 40, 66, 9, 240, 240, 240); ShLine(m, 40, 66, 66, 32, 9, 240, 240, 240);
        break;
    case G_MED:   /* a first-aid tin: white with a red cross, a handle on top */
        if (!ShNew(m, "Medikit", 64, 56)) return;
        ShBox(m, 20, 0, 44, 10, 80, 80, 88); ShBox(m, 24, 4, 40, 10, 0, 0, 0); for (i = 0; i < m->w * 10; i++) if (m->px[i] == 0xff000000u || m->px[i] == ShRgb(0, 0, 0)) m->px[i] = 0;
        ShBox(m, 0, 8, 64, 56, 225, 225, 225); ShBox(m, 26, 16, 38, 50, 210, 30, 30); ShBox(m, 14, 27, 50, 39, 210, 30, 30);
        break;
    case G_ADR:   /* an adrenaline syringe: barrel, yellow fluid, plunger, needle */
        if (!ShNew(m, "Adrenaline", 40, 120)) return;
        ShLine(m, 20, 100, 20, 118, 2, 200, 200, 210); ShBox(m, 11, 30, 29, 100, 205, 215, 225); ShBox(m, 13, 56, 27, 98, 240, 200, 40);
        ShBox(m, 17, 8, 23, 34, 150, 150, 160); ShBox(m, 6, 2, 34, 10, 90, 90, 100); ShBox(m, 4, 28, 36, 34, 120, 120, 130);
        break;
    case G_SCAN:   /* a police scanner: a handheld radio, an antenna, a red light */
        if (!ShNew(m, "Scanner", 60, 120)) return;
        ShLine(m, 42, 4, 42, 40, 5, 40, 40, 44); ShBox(m, 8, 34, 52, 116, 52, 54, 60); ShBox(m, 14, 44, 46, 70, 60, 120, 80);
        for (i = 0; i < 4; i++) ShBox(m, 14, 78 + i * 8, 46, 82 + i * 8, 30, 30, 34); ShEll(m, 20, 40, 3, 3, 240, 40, 40);
        break;
    case G_NOS:   /* a nitrous bottle: blue cylinder, a white band, a chrome valve */
        if (!ShNew(m, "Nitro", 34, 110)) return;
        ShBox(m, 12, 0, 22, 18, 170, 175, 185); ShEll(m, 17, 30, 16, 12, 30, 80, 190); ShBox(m, 1, 30, 33, 110, 30, 80, 190); ShBox(m, 1, 46, 33, 56, 225, 225, 230);
        break;
    case G_CH0 + RRC_NITRO:   /* the Spark Plug Charm: a spark plug on a cord, a blue spark */
        if (!ShNew(m, "Spark", 56, 120)) return;
        ShLine(m, 28, 0, 28, 20, 2, 60, 50, 40); ShBox(m, 20, 18, 36, 52, 235, 235, 230); ShBox(m, 16, 52, 40, 68, 170, 170, 175); ShBox(m, 22, 68, 34, 96, 150, 150, 155);
        ShLine(m, 28, 96, 28, 104, 3, 120, 120, 125); ShLine(m, 28, 106, 18, 114, 3, 120, 190, 255); ShLine(m, 28, 106, 40, 116, 3, 120, 190, 255); ShEll(m, 28, 106, 4, 4, 200, 230, 255);
        break;
    case G_CH0 + RRC_BOOST:   /* the Rabbit's Foot: a furry foot on a chain */
        if (!ShNew(m, "Rabbit", 56, 120)) return;
        for (i = 0; i < 4; i++) ShEll(m, 28, 6 + i * 9.0f, 4, 5, 190, 180, 120);
        ShEll(m, 28, 64, 16, 26, 220, 205, 180); ShEll(m, 28, 96, 13, 18, 230, 215, 190); ShEll(m, 22, 108, 5, 5, 200, 170, 160); ShEll(m, 34, 108, 5, 5, 200, 170, 160);
        break;
    case G_CH0 + RRC_LEECH:   /* the Vampire Fang: a curved white fang with a red tip, on a cord */
        if (!ShNew(m, "Fang", 56, 120)) return;
        ShLine(m, 28, 0, 28, 24, 2, 60, 50, 40); ShEll(m, 28, 30, 9, 8, 200, 170, 60);
        for (i = 0; i < 70; i++) { float t = i / 70.0f, x = 28 + 14 * t * t, y = 36 + i * 1.1f, r = 10 * (1 - t) + 1; ShEll(m, x, y, r, 2, i > 56 ? 200 : 235, i > 56 ? 30 : 230, i > 56 ? 30 : 215); }
        break;
    case G_CH0 + RRC_TORCH:   /* the Miner's Lamp: a brass lamp with a bright lens */
        if (!ShNew(m, "Lamp", 80, 110)) return;
        ShLine(m, 40, 2, 40, 18, 3, 140, 110, 60); ShBox(m, 22, 18, 58, 30, 170, 130, 60); ShBox(m, 18, 30, 62, 92, 180, 140, 70);
        ShEll(m, 40, 60, 16, 16, 255, 245, 190); ShEll(m, 40, 60, 9, 9, 255, 255, 235); ShBox(m, 14, 92, 66, 104, 150, 115, 55);
        break; } }
static const WpImg *ShPicture(int g)
{   WpList();
    if (ShWeapon(g) && g > 0) {
        if (g == 1) { if (!g_shClub.px) ShMakeClub(&g_shClub); return &g_shClub; }
        if (g == 4) { if (!g_shChain.px) ShMakeChain(&g_shChain); return &g_shChain; }
        { int i = g_rraWeapons[g].pic; return i > 0 && i < g_wpN ? &g_wp[i] : NULL; } }
    if (g == 0 && !g_ars->s.dmgLv[0]) {   /* bare knuckles: a clenched fist (skin, four knuckles, the thumb across) */
        static WpImg fist; int i;
        if (!fist.px && ShNew(&fist, "Fist", 90, 80)) {
            ShEll(&fist, 46, 46, 38, 30, 225, 170, 130);
            for (i = 0; i < 4; i++) ShEll(&fist, 18 + i * 19.0f, 26, 11, 13, 230, 178, 138);
            ShLine(&fist, 14, 56, 56, 52, 14, 215, 160, 120); }
        return fist.px ? &fist : NULL; }
    if (!g_shPic[g].px) ShMakePic(g, &g_shPic[g]);
    return g_shPic[g].px ? &g_shPic[g] : NULL; }
/* picture m into the 16-bit surface: fitted into the box, turned by `ang` radians round its middle, 3 x 3 samples a pixel, blended by alpha */
static void ShBlit(Surf *s, const WpImg *m, int bx, int by, int bw, int bh, float ang)
{   float ca = cosf(ang), sa = sinf(ang), ew, eh, sc, cx = bx + bw * 0.5f, cy = by + bh * 0.5f; int x, y;
    if (!m || !m->px || s->bpp != 16) return;
    ew = fabsf(m->w * ca) + fabsf(m->h * sa); eh = fabsf(m->w * sa) + fabsf(m->h * ca); sc = (float)bw / ew;
    if ((float)bh / eh < sc) sc = (float)bh / eh;
    for (y = by; y < by + bh && y < s->h; y++) { WORD *row = (WORD *)(s->bits + (size_t)y * s->pitch); for (x = bx; x < bx + bw && x < s->w; x++) {
        int k, n = 0; float r = 0, g = 0, b = 0, al = 0;
        for (k = 0; k < 9; k++) { float dx = (x + (k % 3 + 0.5f) / 3 - cx) / sc, dy = (y + (k / 3 + 0.5f) / 3 - cy) / sc; int u = (int)floorf(dx * ca + dy * sa + m->w * 0.5f), v = (int)floorf(-dx * sa + dy * ca + m->h * 0.5f); DWORD c;
            if (u < 0 || u >= m->w || v < 0 || v >= m->h) continue; c = m->px[v * m->w + u]; if ((c >> 24) < 128) continue;
            r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
        if (!n) continue; al = n / 9.0f; r /= n; g /= n; b /= n;
        {   WORD o = row[x]; float orr = (float)((o >> 11) << 3), og = (float)(((o >> 5) & 63) << 2), ob = (float)((o & 31) << 3);
            r = orr + (r - orr) * al; g = og + (g - og) * al; b = ob + (b - ob) * al;
            row[x] = (WORD)((((int)r >> 3) << 11) | (((int)g >> 2) << 5) | ((int)b >> 3)); } } } }

static void ShBar(HDC dc, int x, int y, int w, float v, float next, COLORREF c)   /* a stat bar: v (0..1) filled, up to next lighter (the next upgrade) */
{   IcBox(dc, x, y, w, 10, RGB(30, 30, 52), RGB(80, 80, 120), 4);
    if (next > v) IcBox(dc, x + 1, y + 1, (int)((w - 2) * (next > 1 ? 1 : next)), 8, IcDim(c, 40), IcDim(c, 40), 3);
    IcBox(dc, x + 1, y + 1, (int)((w - 2) * (v > 1 ? 1 : v)), 8, c, c, 3); }
static void ShWrap(HDC dc, int x, int y, int w, const char *t, COLORREF c, int lh)   /* words wrapped into width w */
{   char line[160]; int n = 0; const char *p = t;
    while (*p) { const char *e = p; int k; SIZE sz; while (*e == ' ') e++; k = 0; while (e[k] && e[k] != ' ') k++;
        if (n + 1 + k >= (int)sizeof line) break;
        { char tr[160]; memcpy(tr, line, n); tr[n] = 0; if (n) strcat(tr, " "); strncat(tr, e, k); GetTextExtentPoint32A(dc, tr, (int)strlen(tr), &sz);
          if (sz.cx > w && n) { line[n] = 0; IcText(dc, x, y, line, c, TA_LEFT | TA_TOP); y += lh; n = 0; continue; } strcpy(line, tr); n = (int)strlen(line); }
        p = e + k; }
    if (n) { line[n] = 0; IcText(dc, x, y, line, c, TA_LEFT | TA_TOP); } }


/* the layout (frame pixels): tabs, a 3 x 2 grid of tiles, the detail panel on the right with its buttons, Leave under the grid */
#define SH_COLS 3
static void ShTabRect(int t, RECT *r) { SetRect(r, 16 + t * 100, 62, 16 + t * 100 + 96, 82); }
static void ShTileRect(int k, RECT *r) { int x = 16 + (k % SH_COLS) * 134, y = 90 + (k / SH_COLS) * 118; SetRect(r, x, y, x + 128, y + 112); }
static void ShButRect(int j, RECT *r) { SetRect(r, 432, 282 + j * 24, 624, 282 + j * 24 + 21); }
static void ShLeaveRect(RECT *r) { SetRect(r, 16, 332, 176, 362); }
static const char *ShHelp(const ShState *s)   /* one line for the selected button (the tiles explain themselves) */
{   static char b[160]; int g = s->cur, j = s->sel - SH_A;
    b[0] = 0;
    if (!ShIsAction(s->sel)) return b;
    if (ShWeapon(g)) _snprintf(b, sizeof b, j == 0 ? "You ride into every race with the weapon you carry (in a race: Esc > WEAPON)." : j == 1 ? (g ? "+10 %% hitting power a level." : "Brass on your knuckles: +25 %% punching power a level.") : j == 2 ? "Hit riders a little further away." : "A backhand that lands (the attack to your other side) gives you a burst of speed.");
    else if (g == G_BOOTS) _snprintf(b, sizeof b, j == 0 ? "Steel toes: kicks do +30 %% damage a level." : "Spring heels: kicks shove riders +40 %% further a level.");
    else if (g == G_INS) _snprintf(b, sizeof b, "Lower excess, higher premium.  Busted or totalled: you pay the excess, the insurer the rest.  Premium after every race.");
    else if (g == G_NOS) _snprintf(b, sizeof b, g_ars->bikeNitro ? "%d nitro shots a pack, used after your bike's own." : "%d shots a pack - but your bike has NO nitro: they wait for one that does.", RRA_NITRO_SHOTS);
    else if (g == G_MED || g == G_ADR) _snprintf(b, sizeof b, "Carried until used: press %c in a race.", g == G_MED ? (g_ars->medKey ? g_ars->medKey : 'H') : (g_ars->adrKey ? g_ars->adrKey : 'J'));
    else if (g == G_SCAN) _snprintf(b, sizeof b, "One is used up by each race you ride.");
    b[sizeof b - 1] = 0; return b; }

static void ShDraw(Surf *tmp, const Surf *base, const ShState *s, DWORD now)
{   HDC dc; HFONT title, fSm, fMid, fBig, fTab, old; int i, g = s->cur, na, en[4], first = g_shTabFirst[s->tab], ng = ShGoodsN(s->tab); char t[128], m[24], at[4][48], pr[4][16]; const RrArsenalSave *a = &g_ars->s; RECT r;
    static const int clsBase[3] = { 256, 320, 384 };
    memcpy(tmp->bits, base->bits, (size_t)tmp->pitch * tmp->h); SdDarken(tmp, 0, 0, tmp->w, tmp->h); SdDarken(tmp, 8, 8, 632, 472); SdDarken(tmp, 8, 8, 632, 472);   /* (the Street's labels must not show through the grid) */
    S_GetDC(&tmp->itf, &dc); SetBkMode(dc, TRANSPARENT);
    title = CreateFontA(40, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_IMPACT)); fTab = CreateFontA(17, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_IMPACT));
    fBig = CreateFontA(24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_IMPACT));
    fSm = CreateFontA(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_ARIAL)); fMid = CreateFontA(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_ARIAL));
    old = (HFONT)SelectObject(dc, title); IcText(dc, 18, 10, "Hurt Hardware", RGB(255, 96, 40), TA_LEFT | TA_TOP);
    SelectObject(dc, fBig); ShMoney(m, sizeof m, ShCash()); _snprintf(t, sizeof t, "Cash: %s", m); t[sizeof t - 1] = 0; IcText(dc, 620, 20, t, RGB(140, 235, 120), TA_RIGHT | TA_TOP);
    SelectObject(dc, fTab);
    for (i = 0; i < 4; i++) { int on = i == s->tab; ShTabRect(i, &r);   /* the tabs */
        IcBox(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, on ? RGB(70, 58, 16) : RGB(26, 26, 50), on ? RGB(255, 235, 80) : RGB(80, 80, 120), 6);
        IcText(dc, (r.left + r.right) / 2, r.top + 1, g_shTab[i], on ? RGB(255, 235, 80) : RGB(170, 170, 195), TA_CENTER | TA_TOP); }
    for (i = 0; i < ng; i++) {   /* the tiles: a plate, the badge top right, the name under the picture (drawn after the text, below) */
        int gg = first + i, on = s->sel == i, cur = gg == g; COLORREF bc;
        ShTileRect(i, &r);
        IcBox(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, on ? RGB(60, 52, 22) : RGB(20, 20, 42), on ? RGB(255, 235, 80) : cur ? RGB(200, 160, 90) : RGB(70, 70, 110), 10);
        ShBadge(gg, t, sizeof t, &bc); SelectObject(dc, fMid); IcText(dc, r.right - 6, r.top + 4, t, bc, TA_RIGHT | TA_TOP);
        SelectObject(dc, fMid); IcText(dc, (r.left + r.right) / 2, r.bottom - 19, ShName(gg), on ? RGB(255, 235, 80) : RGB(225, 225, 235), TA_CENTER | TA_TOP); }
    ShLeaveRect(&r);   /* Leave the shop */
    IcBox(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, s->sel == SH_LEAVE ? RGB(60, 52, 22) : RGB(26, 26, 50), s->sel == SH_LEAVE ? RGB(255, 235, 80) : RGB(80, 80, 120), 8);
    SelectObject(dc, fTab); IcText(dc, (r.left + r.right) / 2, r.top + 5, "Leave the shop", s->sel == SH_LEAVE ? RGB(255, 235, 80) : RGB(225, 225, 235), TA_CENTER | TA_TOP);
    IcBox(dc, 422, 88, 206, 300, RGB(18, 18, 38), RGB(90, 90, 130), 12);   /* the detail panel */
    SelectObject(dc, fBig); IcText(dc, 432, 94, ShName(g), RGB(255, 235, 80), TA_LEFT | TA_TOP);
    SelectObject(dc, fSm); ShWrap(dc, 432, 124, 186, ShBlurb(g), RGB(215, 215, 230), 14);
    SelectObject(dc, fSm);
    if (ShWeapon(g)) {   /* three bars: damage, reach, backhand boost */
        int cls = g_rraWeapons[g].cls; float dmg = clsBase[cls] * RraDamage(a, g) / 100.0f, rch = (float)RraReach(a, g);
        IcText(dc, 432, 202, "Damage", RGB(190, 190, 215), TA_LEFT | TA_TOP); ShBar(dc, 500, 205, 118, dmg / 560.0f, a->dmgLv[g] < RRA_MAXLV ? clsBase[cls] * (RraDamage(a, g) + (g ? RRA_DMGSTEP : 25)) / 100.0f / 560.0f : 0, RGB(235, 90, 60));
        IcText(dc, 432, 222, "Reach", RGB(190, 190, 215), TA_LEFT | TA_TOP); ShBar(dc, 500, 225, 118, rch / 110.0f, a->reachLv[g] < RRA_MAXLV ? (rch + RRA_REACHSTEP) / 110.0f : 0, RGB(90, 170, 255));
        IcText(dc, 432, 242, "Backhand", RGB(190, 190, 215), TA_LEFT | TA_TOP); ShBar(dc, 500, 245, 118, a->backLv[g] / 3.0f, a->backLv[g] < RRA_MAXLV ? (a->backLv[g] + 1) / 3.0f : 0, RGB(120, 230, 120)); }
    else if (g == G_BOOTS) {
        IcText(dc, 432, 222, "Kick hurt", RGB(190, 190, 215), TA_LEFT | TA_TOP); ShBar(dc, 500, 225, 118, (1 + a->bootDmg) / 4.0f, a->bootDmg < RRA_MAXLV ? (2 + a->bootDmg) / 4.0f : 0, RGB(235, 90, 60));
        IcText(dc, 432, 242, "Kick shove", RGB(190, 190, 215), TA_LEFT | TA_TOP); ShBar(dc, 500, 245, 118, (1 + a->bootPush) / 4.0f, a->bootPush < RRA_MAXLV ? (2 + a->bootPush) / 4.0f : 0, RGB(90, 170, 255)); }
    else if (g == G_INS) { SelectObject(dc, fMid); _snprintf(t, sizeof t, "Your cover: %s", g_rraPlans[a->insurance].name); t[sizeof t - 1] = 0; IcText(dc, 432, 250, t, a->insurance ? RGB(140, 235, 120) : RGB(255, 200, 90), TA_LEFT | TA_TOP); }
    else if (ShSupply(g)) { SelectObject(dc, fMid); _snprintf(t, sizeof t, "You have %d of %d", *ShSupplyHave(g), ShSupplyMax(g)); t[sizeof t - 1] = 0; IcText(dc, 432, 250, t, RGB(150, 200, 255), TA_LEFT | TA_TOP);
        if (g == G_NOS && !g_ars->bikeNitro) { SelectObject(dc, fSm); IcText(dc, 432, 266, "Your bike has no nitro", RGB(255, 120, 100), TA_LEFT | TA_TOP); } }
    na = ShActions(g, at, pr, en);
    for (i = 0; i < na; i++) {   /* the buttons: label left, price right */
        int on = s->sel == SH_A + i; COLORREF c = !en[i] ? RGB(110, 110, 130) : on ? RGB(255, 235, 80) : RGB(235, 235, 240);
        ShButRect(i, &r);
        IcBox(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, on ? RGB(70, 58, 16) : RGB(30, 30, 58), on ? RGB(255, 235, 80) : RGB(70, 70, 110), 6);
        SelectObject(dc, fMid); IcText(dc, r.left + 8, r.top + 3, at[i], c, TA_LEFT | TA_TOP); IcText(dc, r.right - 8, r.top + 3, pr[i], c, TA_RIGHT | TA_TOP); }
    if (a->bountyChar) { SelectObject(dc, fSm); IcText(dc, 190, 340, "A bounty is waiting: see your HUD in the next race.", RGB(255, 120, 100), TA_LEFT | TA_TOP); }
    SelectObject(dc, fSm); IcText(dc, 18, 404, ShHelp(s), RGB(225, 225, 240), TA_LEFT | TA_TOP);
    SelectObject(dc, fMid); if (s->msg[0] && now - s->msgT < 3500) IcText(dc, 18, 428, s->msg, RGB(255, 200, 90), TA_LEFT | TA_TOP);
    SelectObject(dc, fSm); IcText(dc, 18, 462, ShIsAction(s->sel) ? "Up/Down: choose   Enter: buy / do it   Left or Esc: back to the goods" : "Arrows: choose (Left / Right past the edge: the next counter)   Enter: buy or upgrade   Esc: leave", RGB(150, 150, 175), TA_LEFT | TA_TOP);
    SelectObject(dc, old); DeleteObject(title); DeleteObject(fTab); DeleteObject(fBig); DeleteObject(fSm); DeleteObject(fMid); GdiFlush();
    for (i = 0; i < ng; i++) { int gg = first + i, big = ShWeapon(gg) && gg > 0; const WpImg *p = ShPicture(gg); ShTileRect(i, &r);   /* the thumbnails */
        if (p) ShBlit(tmp, p, r.left + 10, r.top + 20, 108, 70, big && gg != 4 ? 1.2f : 0.0f);
        else { HDC d2; S_GetDC(&tmp->itf, &d2); SetBkMode(d2, TRANSPARENT); IcText(d2, (r.left + r.right) / 2, r.top + 46, "(fists)", RGB(150, 150, 175), TA_CENTER | TA_TOP); GdiFlush(); } } }

/* where the mouse is: a tab (-10 - t), a tile (k), a button (SH_A + j), Leave (SH_LEAVE), nothing (-1) */
static int ShRowAt(const ShState *s, int x, int y)
{   int i, n, en[4]; char at[4][48], pr[4][16]; RECT r; POINT p; p.x = x; p.y = y;
    for (i = 0; i < 4; i++) { ShTabRect(i, &r); if (PtInRect(&r, p)) return -10 - i; }
    for (i = 0; i < ShGoodsN(s->tab); i++) { ShTileRect(i, &r); if (PtInRect(&r, p)) return i; }
    n = ShActions(s->cur, at, pr, en);
    for (i = 0; i < n; i++) { ShButRect(i, &r); if (PtInRect(&r, p)) return SH_A + i; }
    ShLeaveRect(&r); if (PtInRect(&r, p)) return SH_LEAVE;
    return -1; }
static void ShPick(ShState *s, int sel) { s->sel = sel; if (sel >= 0 && sel < SH_A) s->cur = g_shTabFirst[s->tab] + sel; }
static void ShTab(ShState *s, int t, int k) { s->tab = (t + 4) % 4; if (k >= ShGoodsN(s->tab)) k = ShGoodsN(s->tab) - 1; ShPick(s, k < 0 ? 0 : k); }
static int ShPay(ShState *s, int cost)   /* 1 = paid */
{   char t[96], m[24];
    if (cost > ShCash()) { ShMoney(m, sizeof m, cost - ShCash()); _snprintf(t, sizeof t, "Not enough cash: you need %s more.", m); t[sizeof t - 1] = 0; ShMsg(s, t); return 0; }
    *g_ars->cash -= cost; g_ars->unsaved = 1; return 1; }
static int ShActivate(ShState *s, int sel)   /* 1 = leave */
{   RrArsenalSave *a = &g_ars->s; int g = s->cur, j = sel - SH_A, n, en[4]; char at[4][48], pr[4][16], t[120];
    if (sel == SH_LEAVE) return 1;
    if (sel < SH_A) { ShPick(s, sel); n = ShActions(g, at, pr, en); for (j = 0; j < n && !en[j]; j++) {} s->sel = SH_A + (j < n ? j : 0); return 0; }   /* (Enter on a tile: its first usable button) */
    n = ShActions(g, at, pr, en);
    if (j < 0 || j >= n) return 0;
    if (!en[j]) { if (ShWeapon(g) && !RraOwns(a, g)) ShMsg(s, "Buy it first."); return 0; }
    if (ShWeapon(g)) {
        if (j == 0) {
            if (!RraOwns(a, g)) { if (!ShPay(s, g_rraWeapons[g].price)) return 0; a->owned |= (unsigned char)(1 << g); }
            a->equipped = (unsigned char)g; g_ars->unsaved = 1; _snprintf(t, sizeof t, "You'll ride with the %s.", ShName(g)); }
        else { unsigned char *lv = j == 1 ? &a->dmgLv[g] : j == 2 ? &a->reachLv[g] : &a->backLv[g]; if (!ShPay(s, j == 3 ? RRA_BACK_PRICE(*lv) : RraUpgradeCost(g, *lv))) return 0; (*lv)++;
            _snprintf(t, sizeof t, j == 1 ? (g ? "The %s hits harder now." : "Now: %s.") : j == 2 ? "The %s reaches further now." : "Backhands with the %s give you more speed now.", ShName(g)); } }
    else if (g == G_BOOTS) { unsigned char *lv = j == 0 ? &a->bootDmg : &a->bootPush; if (!ShPay(s, RRA_BOOT_PRICE(*lv))) return 0; (*lv)++; _snprintf(t, sizeof t, j == 0 ? "Steel toes fitted: your kicks hurt more." : "Spring heels fitted: your kicks shove harder."); }
    else if (g == G_VEST) { if (!ShPay(s, RRA_VEST_PRICE)) return 0; a->vest = 1; _snprintf(t, sizeof t, "Strapped in: hits do a quarter less to you."); }
    else if (g == G_INS) { a->insurance = (unsigned char)(j < 3 ? j + 1 : 0); g_ars->unsaved = 1; if (a->insurance) _snprintf(t, sizeof t, "Signed up: %s cover from the next race.", g_rraPlans[a->insurance].name); else _snprintf(t, sizeof t, "Insurance cancelled."); }
    else if (ShSupply(g)) { int k = j == 0 ? 1 : 3; if (!ShPay(s, ShSupplyPrice(g) * k)) return 0; *ShSupplyHave(g) = (unsigned char)(*ShSupplyHave(g) + k);
        _snprintf(t, sizeof t, g == G_NOS && !g_ars->bikeNitro ? "Sold - but your bike has no nitro: they wait for one that does." : "Sold!  You have %d now.", *ShSupplyHave(g)); }
    else { if (!ShPay(s, g_rraCharms[g - G_CH0].price)) return 0; a->charms |= (unsigned char)(1 << (g - G_CH0)); _snprintf(t, sizeof t, "The %s is yours.", ShName(g)); }
    t[sizeof t - 1] = 0; ShMsg(s, t); LOG("shop: %s: %s %s (cash %d)", ShName(g), at[j], pr[j], ShCash());
    return 0; }
static int ShKeyDown(ShState *s, int vk)
{   int n, en[4], ng = ShGoodsN(s->tab), k; char at[4][48], pr[4][16];
    if (ShIsAction(s->sel)) {
        n = ShActions(s->cur, at, pr, en);
        if (vk == VK_ESCAPE || vk == VK_LEFT) { ShPick(s, s->cur - g_shTabFirst[s->tab]); return 0; }
        if (vk == VK_UP) { s->sel = SH_A + (s->sel - SH_A + n - 1) % n; return 0; }
        if (vk == VK_DOWN || vk == VK_TAB) { s->sel = SH_A + (s->sel - SH_A + 1) % n; return 0; }
        if (vk == VK_RETURN || vk == VK_SPACE) return ShActivate(s, s->sel);
        return 0; }
    if (vk == VK_ESCAPE) return 1;
    if (s->sel == SH_LEAVE) {   /* (Leave: Up back into the grid, Enter leaves) */
        if (vk == VK_UP || vk == VK_DOWN) ShPick(s, vk == VK_UP ? (ng > SH_COLS ? SH_COLS : 0) : 0);
        else if (vk == VK_RETURN || vk == VK_SPACE) return 1;
        return 0; }
    k = s->sel;
    if (vk == VK_LEFT) { if (k % SH_COLS == 0) ShTab(s, s->tab - 1, (k / SH_COLS) * SH_COLS + SH_COLS - 1); else ShPick(s, k - 1); return 0; }
    if (vk == VK_RIGHT) { if (k % SH_COLS == SH_COLS - 1 || k + 1 >= ng) ShTab(s, s->tab + 1, (k / SH_COLS) * SH_COLS); else ShPick(s, k + 1); return 0; }
    if (vk == VK_UP) { if (k >= SH_COLS) ShPick(s, k - SH_COLS); return 0; }
    if (vk == VK_DOWN) { if (k + SH_COLS < ng) ShPick(s, k + SH_COLS); else ShPick(s, SH_LEAVE); return 0; }
    if (vk == VK_TAB) { ShTab(s, s->tab + 1, 0); return 0; }
    if (vk == VK_RETURN || vk == VK_SPACE) return ShActivate(s, s->sel);
    return 0; }

/* the game's menus (window.c, the Schmooze page's Yes / No): a mouse message's position (lParam of window h) in frame pixels */
__declspec(dllexport) void __stdcall RR_FramePos(HWND h, LPARAM lp, int *x, int *y) { MSG m; memset(&m, 0, sizeof m); m.hwnd = h; m.lParam = lp; MsgFramePos(&m, x, y); }

/* The Street > Hurt Hardware (arsenal.c rr_StreetShop): 1 = shown; 0 = the screen cannot be shown */
__declspec(dllexport) int __stdcall RR_WeaponShop(void)
{   Surf *tmp, base; MSG m; ShState s; int done = 0, dirty = 1, i; Surf *prevModal = g_modalSurf; DWORD lastDraw = 0;
    if (!g_ars || !g_primary || !(tmp = NewSurf(g_primary->w, g_primary->h, 16))) return 0;
    base = *tmp; base.bits = (BYTE *)malloc((size_t)tmp->pitch * tmp->h); if (!base.bits) { FreeSurf(tmp); return 0; }
    { Surf *keep = g_modalSurf; g_modalSurf = NULL; SdBaseFrame(&base); g_modalSurf = keep; }
    g_modalSurf = tmp; memset(&s, 0, sizeof s);
    for (i = 1; i < RRA_N && RraOwns(&g_ars->s, i); i++) {}   /* (starts on the first weapon not owned yet, else the carried one) */
    ShPick(&s, i < RRA_N ? i : g_ars->s.equipped);
    LOG("shop: open (cash %d)", ShCash());
    while (!done) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); done = 1; break; }
            if (m.message == WM_SYSKEYDOWN && m.wParam == VK_RETURN) { VidToggleFullscreen(); dirty = 1; continue; }
            if (m.message == WM_KEYDOWN && m.wParam == VK_F11) { VidToggleFullscreen(); dirty = 1; continue; }
            if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN) { if (ShKeyDown(&s, (int)m.wParam)) done = 1; dirty = 1; continue; }
            if (MSG_LDOWN(m)) { int x, y, r; MsgFramePos(&m, &x, &y); r = ShRowAt(&s, x, y);
                if (r <= -10) ShTab(&s, -10 - r, 0); else if (r >= 0 && r < SH_A && r != s.sel) ShPick(&s, r); else if (r >= 0 && ShActivate(&s, r)) done = 1;
                dirty = 1; continue; }
            if (m.message == WM_MOUSEMOVE) { int x, y, r; MsgFramePos(&m, &x, &y); if (m.wParam == 0 && (r = ShRowAt(&s, x, y)) >= 0 && r != s.sel) { ShPick(&s, r); dirty = 1; } continue; }
            if (m.message == WM_KEYUP || m.message == WM_SYSKEYUP || m.message == WM_CHAR || m.message == WM_LBUTTONUP || m.message == WM_RBUTTONUP || MSG_RDOWN(m)) continue;
            DispatchMessageA(&m); }
        if (GetTickCount() - lastDraw > 250) dirty = 1;
        if (dirty && !done) { ShDraw(tmp, &base, &s, GetTickCount()); Present(tmp); dirty = 0; lastDraw = GetTickCount();
            {   const char *dd = getenv("RR_DUMP_DIR"), *t = getenv("RR_TEST_SHOP");
                if (t && dd && *t >= '0' && *t <= '3') { char p[MAX_PATH]; int tb = *t - '0'; if (s.tab != tb) { ShTab(&s, tb, 0); dirty = 1; continue; }
                    _snprintf(p, sizeof p, "%s\\shop.bmp", dd); p[sizeof p - 1] = 0; DumpBmp(tmp, p); done = 1; } } }
        Sleep(15); }
    ModalEatClick();
    g_modalSurf = prevModal; g_lastShown = g_primary; free(base.bits); FreeSurf(tmp);
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
    if (g_primary && !prevModal) Present(g_primary);
    LOG("shop: closed (cash %d)", ShCash()); return 1; }
