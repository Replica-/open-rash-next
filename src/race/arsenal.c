/* race\arsenal.c - the player's weapons in a Big Game (user, 2026-11-14): Hurt Hardware on The Street sells weapons and upgrades (damage, reach),
 * a new rider starts with bare knuckles (the computer riders keep the level's weapons), the riders in the Klub's Schmooze offer deals and bounties.
 *
 *   state        g_arsenal (platform\arsenal_api.h: the weapon list, RrArsenalSave), saved after the player file's 0xA0 bytes (system\savegame.c)
 *   The Street   a fourth item "Hurt Hardware" (ui\menutrim.c, strings.c): rr_StreetShop opens the shim's shop screen (ddraw shop.inc RR_WeaponShop)
 *   races        rr_ArsenalPlayerTick (player_control.c): the carried weapon's class for the player at the start and when it changes (Esc pause row);
 *                rr_ArsenalStrength / rr_ArsenalReach (ai.c): the player's hit strength and reach with it
 *   Schmooze     rr_ArsenalSchmoozeChar (race_messages.c: who talks), rr_ArsenalScreen (menu_screen.c): now and then that rider offers a cheap weapon,
 *                an upgrade, or a bounty - knock <rival> off the bike in the next race for $, fail and pay half; asked with the shim's message box
 *   bounty       rr_ArsenalHit (ai.c: the player hit someone) + rr_ArsenalDown (rider.c: someone crashed) = the target taken out; rr_ArsenalRaceEnd
 *                (shutdown.c) pays or collects; the course map marks the target (render\gpu_scene.c)
 * Only in a Big Game (g_gameMode 2): demo / test races (mode 0) never see any of it.  Test switches: RR_SCHMOOZE_OFFER=bounty|weapon|upgrade (every
 * Schmooze offers that), RR_ARSLOG=<file> (1 = arsenal.log in the working folder): offers, the bounty's target, the player's weapon per race. */
#include "../rr.h"
#include "../data/records.h"
#include "../platform/arsenal_api.h"
#include "../platform/gpu_api.h"
#include <string.h>

RR_DATA(int,   g_gameMode);
RR_DATA(char,  g_unsavedChanges);
RR_DATA(int,   g_playerRider);
RR_DATA(int,   g_riderCount);
RR_DATA(HWND,  g_mainWindow);

#define I32(p, o) (*(int *)((char *)(p) + (o)))

int __cdecl sprintf(char *dst, const char *fmt, ...);
int __cdecl rand(void);
char * __fastcall rr_CharacterNameOf(unsigned char slot);   /* ui\rider_names.c */
unsigned __fastcall rr_RiderHudObject(int obj);             /* ui\hud.c: a rider's name */
int RrRiderAt(int i);                                        /* rider_slots.c */
void __stdcall rr_MenuTextRedraw(void);                      /* render\menu_text.c */

RrArsenal g_arsenal;
static int g_schmoozeChar;              /* who talks on the Schmooze page (1..8) */
static int g_offerMade;                 /* an offer was made since the last race / visit to The Street */
static char g_note[400], g_noteTitle[48];   /* a message for the next menu screen (the bounty's outcome) */
static int g_bountyOwn, g_bountyDone, g_bountyIn;    /* this race: the target's rider record, taken out, in the race */
static unsigned g_bountyHitT;           /* race time of the player's last hit on the target (+1; 0 = none) */

static void ArsLog(const char *t)
{
    static int on = -1;
    static char path[MAX_PATH];
    if (on < 0) { on = GetEnvironmentVariableA("RR_ARSLOG", path, sizeof path) > 0; if (on && !strcmp(path, "1")) strcpy(path, "arsenal.log"); }
    if (on) { HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL); DWORD n;
        if (h != INVALID_HANDLE_VALUE) { WriteFile(h, t, (DWORD)strlen(t), &n, NULL); WriteFile(h, "\r\n", 2, &n, NULL); CloseHandle(h); } }
}

static int ShimCall(const char *name)
{
    HMODULE sh = GetModuleHandleA("ddraw.dll");
    int (__stdcall *f)(void) = sh ? (int (__stdcall *)(void))GetProcAddress(sh, name) : 0;
    return f ? f() : 0;
}
static int Ask(const char *text, const char *title, unsigned type)   /* the shim's message box (msgbox.inc): IDYES / IDNO / IDOK */
{
    HMODULE sh = GetModuleHandleA("ddraw.dll");
    int (__stdcall *f)(const char *, const char *, unsigned, unsigned) = sh ? (int (__stdcall *)(const char *, const char *, unsigned, unsigned))GetProcAddress(sh, "RR_MessageBox") : 0;
    return f ? f(text, title, type, 0) : IDNO;
}

/* "$1,250" */
static const char *Money(char *b, int v)
{
    if (v >= 1000) sprintf(b, "$%d,%03d", v / 1000, v % 1000); else sprintf(b, "$%d", v);
    return b;
}

/* start-up (shutdown.c rr_WinMainLoop): the shim gets the state */
void rr_ArsenalInit(void)
{
    HMODULE sh = GetModuleHandleA("ddraw.dll");
    void (__stdcall *bind)(RrArsenal *) = sh ? (void (__stdcall *)(RrArsenal *))GetProcAddress(sh, "RR_ArsenalBind") : 0;
    g_arsenal.cash = &rec_playerRecord.g_playerCash;
    if (bind) bind(&g_arsenal);
}

/* a new rider (game_mode.c: a fresh Big Game; savegame.c: a player file without the block) */
void rr_ArsenalReset(void) { memset(&g_arsenal.s, 0, sizeof g_arsenal.s); g_offerMade = 0; g_note[0] = 0; }
void rr_ArsenalSet(const RrArsenalSave *s)
{
    int w;
    g_arsenal.s = *s;
    if (g_arsenal.s.medikits > RRA_MEDIKIT_MAX) g_arsenal.s.medikits = RRA_MEDIKIT_MAX;
    if (g_arsenal.s.nitroPacks > RRA_NITRO_MAX) g_arsenal.s.nitroPacks = RRA_NITRO_MAX;
    if (g_arsenal.s.equipped >= RRA_N || !RraOwns(&g_arsenal.s, g_arsenal.s.equipped)) g_arsenal.s.equipped = 0;
    for (w = 0; w < RRA_N; w++) {
        if (g_arsenal.s.dmgLv[w] > RRA_MAXLV) g_arsenal.s.dmgLv[w] = RRA_MAXLV;
        if (g_arsenal.s.reachLv[w] > RRA_MAXLV) g_arsenal.s.reachLv[w] = RRA_MAXLV;
        if (g_arsenal.s.backLv[w] > RRA_MAXLV) g_arsenal.s.backLv[w] = RRA_MAXLV;
    }
    if (g_arsenal.s.bootDmg > RRA_MAXLV) g_arsenal.s.bootDmg = RRA_MAXLV;
    if (g_arsenal.s.bootPush > RRA_MAXLV) g_arsenal.s.bootPush = RRA_MAXLV;
    if (g_arsenal.s.adrenaline > RRA_ADREN_MAX) g_arsenal.s.adrenaline = RRA_ADREN_MAX;
    if (g_arsenal.s.scanners > RRA_SCAN_MAX) g_arsenal.s.scanners = RRA_SCAN_MAX;
    if (g_arsenal.s.insurance > 3) g_arsenal.s.insurance = 0;
    g_arsenal.s.vest = g_arsenal.s.vest ? 1 : 0;
    g_arsenal.s.charms &= (1 << RRC_N) - 1;
    if (g_arsenal.s.bountyChar > 8) g_arsenal.s.bountyChar = 0;
}

static int BigGame(void) { return g_gameMode == 2; }

/* ---- supplies: nitro packs and medikits ---- */
/* which bikes have nitro: the bike's spec chunk (tbl_riderSpecChunks[bike]) holds the nitro block the race set-up loads (ParamBlockLoad, chunk + 0x14c:
   boost step, boost factor, meter cap, charges +0xc, meter needed to fire, refill) - all zero on a bike without.  Found: models 6..9, 10 charges each.
   Before the chunks are loaded: those four */
static int g_nitroBase, g_nitroAdded;   /* this race: the bike's own charges, the charges from packs put on top */
static int BikeHasNitro(int bike)
{
    int c;
    if (bike < 0 || bike > 14) return 0;
    c = (&(*(int *)&g_songState.tbl_riderSpecChunks))[bike];
    return c ? *(int *)(c + 0x14c + 0xc) > 0 : bike >= 6 && bike <= 9;
}
/* rider.c race set-up: the player's bike's nitro block was just loaded (g_hudFlashCount = its charges for this race, 0 = no nitro) */
void rr_ArsenalNitroSetup(int bike)
{
    char b[96];
    g_nitroBase = rec_hudScoreStep.g_hudFlashCount; g_nitroAdded = 0;
    if (BigGame() && g_nitroBase > 0 && g_arsenal.s.nitroPacks) { g_nitroAdded = g_arsenal.s.nitroPacks * RRA_NITRO_SHOTS; rec_hudScoreStep.g_hudFlashCount += g_nitroAdded; }
    sprintf(b, "race: bike %d, nitro charges %d (+%d from packs)", bike, g_nitroBase, g_nitroAdded);
    ArsLog(b);
}
/* the item keys: the first of H, M, J, K, L, G, B, V not bound to one of the game's controls (Configure Input) for the medikit, the next one for
   adrenaline - decided at each race start (the menus' binding table is not the race's) */
static int FreeKey(int skip)
{
    static const char keys[] = "HMJKLGBV";
    int i;
    for (i = 0; keys[i]; i++) if (keys[i] != skip && !(&rec_sndPlayerEngine.tbl_joyBindings[0])[(unsigned char)keys[i]]) return keys[i];
    return skip == 'H' ? 'J' : 'H';
}
static unsigned g_adrUntil;   /* race time the adrenaline shot wears off (0 = none) */
static int g_scanOn;          /* a police scanner works in this race */
static unsigned g_healT;      /* race time of the last medikit (+1): "+HEALTH" on the HUD */
static unsigned RaceT(void) { return (*(unsigned *)&rec_featureAccum1.g_raceTime); }
static int Adrenal(void) { return g_adrUntil && RaceT() < g_adrUntil; }
/* rr_ArsenalPlayerTick: the medikit key (an edge) = one kit, health full again (rider record +0x34 full, +0x38 the cap the regeneration climbs to,
   +0x3c now: the race set-up fills all three the same, RiderPutOnGrid); the adrenaline key = one shot, RRA_ADREN_TICKS of harder hits dealt, softer taken */
static void Supplies(int self)
{
    static int wasM, wasA;
    BYTE keys[256];
    int dm = 0, da = 0, rec = I32(self, 0x1e0);
    char b[96];
    {   /* test switch RR_HURT=1: the player's health halved once, 10 s into the race (something for a medikit to mend) */
        static int hurt = -1; char e[4];
        if (hurt < 0) hurt = GetEnvironmentVariableA("RR_HURT", e, sizeof e) > 0;
        if (hurt == 1 && rec && RaceT() > 300) { I32(rec, 0x3c) = I32(rec, 0x38) = I32(rec, 0x34) / 2; hurt = 2; ArsLog("race: RR_HURT: health halved"); }
    }
    if (GetKeyboardState(keys)) { dm = (keys[(unsigned char)g_arsenal.medKey] & 0x80) != 0; da = (keys[(unsigned char)g_arsenal.adrKey] & 0x80) != 0; }
    if (dm && !wasM && g_arsenal.s.medikits && rec && I32(rec, 0x3c) < I32(rec, 0x34)) {
        sprintf(b, "race: medikit used (health %d of %d)", I32(rec, 0x3c), I32(rec, 0x34)); ArsLog(b);
        I32(rec, 0x38) = I32(rec, 0x34);
        I32(rec, 0x3c) = I32(rec, 0x34);
        g_arsenal.s.medikits--;
        g_healT = RaceT() + 1;
    }
    if (da && !wasA && g_arsenal.s.adrenaline && !Adrenal()) {
        g_adrUntil = RaceT() + RRA_ADREN_TICKS;
        g_arsenal.s.adrenaline--;
        ArsLog("race: adrenaline shot");
    }
    wasM = dm; wasA = da;
}
/* render\gpu_scene.c (every race frame): the item HUD - what the player has on board (medikits -1 = not a Big Game: no HUD) */
void rr_ArsenalHud(RrCourseProgress *p)
{
    p->medikits = BigGame() ? g_arsenal.s.medikits : -1;
    p->nitro = BigGame() && g_nitroBase > 0 ? rec_hudScoreStep.g_hudFlashCount : -1;
    p->medKey = g_arsenal.medKey ? g_arsenal.medKey : 'H';
    p->adrenaline = g_arsenal.s.adrenaline; p->adrKey = g_arsenal.adrKey ? g_arsenal.adrKey : 'J';
    p->adrLeft = Adrenal() ? (int)(g_adrUntil - RaceT()) : 0;
    p->healAge = g_healT ? (int)(RaceT() + 1 - g_healT) : -1;
    p->scanner = BigGame() && g_scanOn;
}
/* the shop: does the player's bike have nitro; the item keys as the last race chose them */
static void ShopFacts(void)
{
    g_arsenal.bikeNitro = BikeHasNitro((unsigned char)rec_playerRecord.g_playerBike);
    if (!g_arsenal.medKey) g_arsenal.medKey = 'H';
    if (!g_arsenal.adrKey) g_arsenal.adrKey = 'J';
}

/* ---- races ---- */
/* the race's bounty target: the rider whose name is the character's (the eight characters ride in every Big Game race) */
static void RaceBegin(void)
{
    char b[96];
    int i;
    g_bountyOwn = 0; g_bountyDone = 0; g_bountyIn = 0; g_bountyHitT = 0;
    g_arsenal.bigGame = BigGame();
    g_arsenal.medKey = (char)FreeKey(0);
    g_arsenal.adrKey = (char)FreeKey(g_arsenal.medKey);
    g_adrUntil = 0; g_healT = 0; g_scanOn = 0;
    if (BigGame() && g_arsenal.s.scanners) { g_arsenal.s.scanners--; g_scanOn = 1; ArsLog("race: police scanner on"); }   /* (one a race) */
    if (!BigGame() || !g_arsenal.s.bountyChar) return;
    for (i = 1; i < g_riderCount; i++) {
        int o = RrRiderAt(i), own;
        const char *nm;
        if (!o || !(own = I32(o, 0x2fc))) continue;
        nm = (const char *)rr_RiderHudObject(own);
        if (nm && !strcmp(nm, rr_CharacterNameOf(g_arsenal.s.bountyChar))) { g_bountyOwn = own; g_bountyIn = 1; break; }
    }
    sprintf(b, "race: bounty on %s: %s", rr_CharacterNameOf(g_arsenal.s.bountyChar), g_bountyIn ? "in the race" : "not found");
    ArsLog(b);
}

/* every racing frame of the player (player_control.c; self = the player's rider record): at the race start and when the choice changes, the
   player's weapon class - in a Big Game the carried weapon's (bare knuckles = 0), else the shim's Esc choice (weapons.inc RR_PlayerWeapon:
   0 = the game's own, 1 = its club); a rival can still steal it for the rest of the race */
void rr_ArsenalPlayerTick(int self)
{
    static int (__stdcall *pw)(void); static int init, last = -2, lastSelf; static unsigned lastT;
    unsigned t = (*(unsigned *)&rec_featureAccum1.g_raceTime);
    int start = self != lastSelf || t < lastT, w;
    if (!init) { HMODULE m = GetModuleHandleA("ddraw.dll"); init = 1; pw = m ? (int (__stdcall *)(void))GetProcAddress(m, "RR_PlayerWeapon") : 0; }
    if (start) RaceBegin();
    if (BigGame()) w = g_rraWeapons[g_arsenal.s.equipped < RRA_N ? g_arsenal.s.equipped : 0].cls;
    else { w = pw ? pw() : 0; if (w <= 0) w = -1; }
    if (start || w != last) { if (w >= 0) { I32(self, 0x1c8) = w; I32(self, 0x1d4) = w; } last = w; lastSelf = self;
        if (BigGame()) { char b[96]; int e = g_arsenal.s.equipped; sprintf(b, "race: player carries %s (class %d, damage %d%%, reach %d)", RraName(&g_arsenal.s, e), w, RraDamage(&g_arsenal.s, e), RraReach(&g_arsenal.s, e)); ArsLog(b); } }
    lastT = t;
    if (BigGame()) Supplies(self);
}

/* the carried weapon when the player swings with class cls (-1: another weapon - a stolen one, a kick) */
static int Carried(int cls)
{
    int w = g_arsenal.s.equipped < RRA_N ? g_arsenal.s.equipped : 0;
    return BigGame() && cls == g_rraWeapons[w].cls ? w : -1;
}
static int Pct(int t, int pct) { return t / 100 * pct + t % 100 * pct / 100; }
/* ai.c rr_RiderHitBonus: the strength t of a hit by rider object a on b with class cls - dealt by the player: the carried weapon's damage (a kick:
   the boots' steel toes), +50 % on adrenaline; taken by the player: -25 % in the vest, -50 % on adrenaline */
int rr_ArsenalStrength(int a, int b, int cls, int t)
{
    int w;
    if (!BigGame()) return t;
    if (a == g_playerRider) {
        if (cls == 3) t = Pct(t, 100 + 30 * g_arsenal.s.bootDmg);
        else if ((w = Carried(cls)) >= 0) t = Pct(t, RraDamage(&g_arsenal.s, w));
        if (Adrenal()) t = Pct(t, 150);
    } else if (b == g_playerRider) {
        if (g_arsenal.s.vest) t = Pct(t, 75);
        if (Adrenal()) t = Pct(t, 50);
    }
    return t;
}
/* ai.c rr_AiAttackResolve: the sideways shove v of a kick by rider object a (the boots' spring heels: +40 % a level) */
int rr_ArsenalKickPush(int a, int v)
{
    return BigGame() && a == g_playerRider ? Pct(v, 100 + 40 * g_arsenal.s.bootPush) : v;
}
/* ai.c rr_RidersAlongside: the reach (x 0x1000 lateral units, the class's own = limit) of rider object a swinging class cls */
int rr_ArsenalReach(int a, int cls, int limit)
{
    int w;
    if (a != g_playerRider || (w = Carried(cls)) < 0) return limit * 0x1000;
    return RraReach(&g_arsenal.s, w) * 0x1000 / 10;
}

/* ai.c rr_AiAttackResolve: rider object a lands a hit on rider object b */
void rr_ArsenalHit(int a, int b)
{
    int own, rec;
    if (a != g_playerRider || !b) return;
    if (g_bountyOwn && I32(b, 0x2fc) == g_bountyOwn) g_bountyHitT = RaceT() + 1;
    if (!BigGame()) return;
    own = I32(a, 0x2fc); rec = own ? I32(own, 0x1e0) : 0;
    {   /* a backhand (input bit 0x80: the attack to the other side) that lands: a burst of speed from the carried weapon's backhand upgrade and
           the Rabbit's Foot charm */
        int w = g_arsenal.s.equipped < RRA_N ? g_arsenal.s.equipped : 0, boost = 0;
        if ((*(unsigned *)&rec_featureAccum1.g_inputBits) & 0x80) {
            boost = RRA_BACK_SPEED * g_arsenal.s.backLv[w] + ((g_arsenal.s.charms >> RRC_BOOST & 1) ? 500 : 0);
            if (boost) { char m[64]; I32(a, 0xec) += boost; sprintf(m, "race: backhand boost +%d", boost); ArsLog(m); }
        }
    }
    if (g_arsenal.s.charms >> RRC_NITRO & 1) {   /* Spark Plug Charm: the nitro meter (24.8) up by a quarter of its cap */
        int cap = rec_hudScoreStep.g_hudScoreBase << 8;
        if (cap > 0) { (*(int *)&rec_sndPlayerEngine.g_hudScore) += cap / 4; if ((*(int *)&rec_sndPlayerEngine.g_hudScore) > cap) (*(int *)&rec_sndPlayerEngine.g_hudScore) = cap; }
    }
    if ((g_arsenal.s.charms >> RRC_LEECH & 1) && rec) {   /* Vampire Fang: 4 % of full health back (the cap the regeneration climbs to as well) */
        int full = I32(rec, 0x34), add = full / 25;
        I32(rec, 0x3c) += add; if (I32(rec, 0x3c) > full) I32(rec, 0x3c) = full;
        if (I32(rec, 0x38) < I32(rec, 0x3c)) I32(rec, 0x38) = I32(rec, 0x3c);
    }
}
/* rider.c rr_RiderApplyHit: rider object r comes off the bike.  The target down within 4 s of the player's last hit on it = taken out */
void rr_ArsenalDown(int r)
{
    unsigned t = (*(unsigned *)&rec_featureAccum1.g_raceTime);
    if (!g_bountyOwn || g_bountyDone || I32(r, 0x2fc) != g_bountyOwn || !g_bountyHitT || t + 1 - g_bountyHitT > 240) return;
    g_bountyDone = 1;
    ArsLog("race: bounty target taken out");
}
/* render\gpu_scene.c (course map): 1 = rider object o is the bounty's target; *done = taken out already */
int rr_ArsenalIsTarget(int o, int *done)
{
    *done = g_bountyDone;
    return g_bountyOwn && o && I32(o, 0x2fc) == g_bountyOwn;
}
const char *rr_ArsenalTargetName(void) { return g_arsenal.s.bountyChar ? rr_CharacterNameOf(g_arsenal.s.bountyChar) : ""; }

/* menu_pages.c (the bill after a bust - the fine - or a wreck - the repair): with insurance the player pays the plan's excess at most */
static int g_insPaid;   /* what the insurance paid on the last bill (the race-end message) */
unsigned rr_ArsenalInsure(unsigned cost, int bust)
{
    char b[96];
    unsigned ex;
    g_insPaid = 0;
    if (!BigGame() || !g_arsenal.s.insurance) return cost;
    ex = (unsigned)g_rraPlans[g_arsenal.s.insurance].excess;
    if (cost <= ex) return cost;
    g_insPaid = (int)(cost - ex);
    sprintf(b, "insurance: %s bill %u, paid %d, excess %u", bust ? "bust" : "wreck", cost, g_insPaid, ex); ArsLog(b);
    return ex;
}

/* the race is over (shutdown.c, before the auto save): the insurance premium, the bounty paid or collected, the message for the next menu screen */
void rr_ArsenalRaceEnd(void)
{
    char m1[24], m2[24];
    const char *by, *tg;
    g_offerMade = 0;   /* (Schmooze offers: one round per race) */
    g_adrUntil = 0;
    if (BigGame() && g_arsenal.s.insurance) {   /* the premium; not paid = the policy lapses */
        int p = g_rraPlans[g_arsenal.s.insurance].premium;
        if (rec_playerRecord.g_playerCash >= p) rec_playerRecord.g_playerCash -= p;
        else { sprintf(g_note, "You couldn't pay the %s premium of %s: your insurance has lapsed.", g_rraPlans[g_arsenal.s.insurance].name, Money(m1, p)); strcpy(g_noteTitle, "Insurance"); g_arsenal.s.insurance = 0; }
        g_unsavedChanges = 1;
    }
    if (BigGame() && g_nitroAdded) {   /* the nitro packs burnt: the charges used beyond the bike's own */
        int used = g_nitroAdded - rec_hudScoreStep.g_hudFlashCount, packs;   /* (base + added - left, less the base: the bike's own go first) */
        char b[64];
        if (used > g_nitroAdded) used = g_nitroAdded;
        packs = used > 0 ? (used + RRA_NITRO_SHOTS - 1) / RRA_NITRO_SHOTS : 0;   /* (a pack opened is a pack gone) */
        if (packs) { g_arsenal.s.nitroPacks = (unsigned char)(g_arsenal.s.nitroPacks - packs); g_unsavedChanges = 1; }
        sprintf(b, "race end: %d pack charges used, %d packs gone", used > 0 ? used : 0, packs); ArsLog(b);
        g_nitroAdded = 0;
    }
    if (!BigGame() || !g_arsenal.s.bountyChar) return;
    by = rr_CharacterNameOf(g_arsenal.s.bountyBy); tg = rr_CharacterNameOf(g_arsenal.s.bountyChar);
    if (!g_bountyIn) {
        sprintf(g_note, "%s never showed up for that race.  The deal with %s stands for your next one.", tg, by);
        strcpy(g_noteTitle, "Bounty");
        return;
    }
    if (g_bountyDone) {
        rec_playerRecord.g_playerCash += g_arsenal.s.bountyReward;
        sprintf(g_note, "You put %s on the pavement.  %s slips you %s.", tg, by, Money(m1, g_arsenal.s.bountyReward));
        strcpy(g_noteTitle, "Bounty collected");
    } else {
        int pay = g_arsenal.s.bountyPenalty < rec_playerRecord.g_playerCash ? g_arsenal.s.bountyPenalty : rec_playerRecord.g_playerCash;
        rec_playerRecord.g_playerCash -= pay;
        sprintf(g_note, "%s is still standing.  %s wanted %s for the failed job and takes %s.", tg, by, Money(m1, g_arsenal.s.bountyPenalty), Money(m2, pay));
        strcpy(g_noteTitle, "Bounty failed");
    }
    ArsLog(g_note);
    g_arsenal.s.bountyChar = 0; g_arsenal.s.bountyBy = 0; g_arsenal.s.bountyReward = g_arsenal.s.bountyPenalty = 0;
    g_unsavedChanges = 1;
}

/* ---- The Street: Hurt Hardware (menutrim.c points the item's action here) ---- */
int __fastcall rr_StreetShop(int *item)
{
    (void)item;
    g_arsenal.bigGame = BigGame();
    g_arsenal.unsaved = 0;
    ShopFacts();
    if (!ShimCall("RR_WeaponShop")) return 0;
    if (g_arsenal.unsaved) g_unsavedChanges = 1;
    return (*(int *)&rec_menuScreen.g_menuScreen);   /* (rebuilt: the cash line) */
}

/* ---- Schmooze: an offer in the rider's own words, under the quote, answered on the page (Yes / No) ---- */
static struct { int kind, price, what, extra, sel, answered; char text[200], reply[120]; } g_offer;   /* kind 0 = none on this page */
RR_DATA(int, tbl_controlRecords);
void __fastcall rr_MenuItemDraw(unsigned *it, unsigned *it2);   /* ui\menu_widgets.c */

/* the offer of character `by`: 1 = a bounty, 2 = a weapon, 3 = an upgrade (0 = nothing to offer); fills text, *price, *what, *extra.  Short: it is
   drawn under the rider's quote in the page's text box */
static int MakeOffer(int by, char *text, int *price, int *what, int *extra)
{
    char e[16], m1[24], m2[24];
    int kinds[4], nk = 0, k, forced = 0, cash = rec_playerRecord.g_playerCash, w, i, lvl = (unsigned char)rec_playerRecord.g_playerLevel;
    int cand[RRA_N], nc, charm[RRC_N], nch;
    if (GetEnvironmentVariableA("RR_SCHMOOZE_OFFER", e, sizeof e) > 0) forced = e[0] == 'b' ? 1 : e[0] == 'w' ? 2 : e[0] == 'u' ? 3 : e[0] == 'c' ? 4 : 0;
    if (!g_arsenal.s.bountyChar) kinds[nk++] = 1;
    for (nc = 0, w = 1; w < RRA_N; w++) if (!RraOwns(&g_arsenal.s, w) && g_rraWeapons[w].price * 60 / 100 <= cash) cand[nc++] = w;
    if (nc) kinds[nk++] = 2;
    w = g_arsenal.s.equipped;
    if (w > 0 && (g_arsenal.s.dmgLv[w] < RRA_MAXLV || g_arsenal.s.reachLv[w] < RRA_MAXLV) && RraUpgradeCost(w, 0) / 2 <= cash) kinds[nk++] = 3;
    for (nch = 0, i = 0; i < RRC_N; i++) if (!(g_arsenal.s.charms >> i & 1) && g_rraCharms[i].price * 60 / 100 <= cash) charm[nch++] = i;
    if (nch) kinds[nk++] = 4;
    if (forced) { for (i = 0; i < nk && kinds[i] != forced; i++) {} if (i == nk) return 0; k = forced; }
    else { if (!nk || rand() % 100 >= 55) return 0; k = kinds[rand() % nk]; }
    if (k == 1) {
        int t, c[8], n = 0;
        for (t = 1; t <= 8; t++) if (t != by && t != rec_playerRecord.g_playerCharacter) c[n++] = t;
        if (!n) return 0;
        t = c[rand() % n];
        *what = t;
        *price = 400 + lvl * 300 + rand() % 5 * 50;
        *extra = *price / 2;
        sprintf(text, "Knock %s off the bike next race and %s is yours.  Blow it, you owe me %s.", rr_CharacterNameOf((unsigned char)t), Money(m1, *price), Money(m2, *extra));
    } else if (k == 2) {
        w = cand[rand() % nc];
        *what = w;
        *price = g_rraWeapons[w].price * (55 + rand() % 3 * 5) / 100 / 10 * 10;
        sprintf(text, "Got a %s that fell off a truck.  %s, cash.  Hurt Hardware wants %s.", g_rraWeapons[w].name, Money(m1, *price), Money(m2, g_rraWeapons[w].price));
    } else if (k == 4) {
        int c = charm[rand() % nch];
        *what = c;
        *price = g_rraCharms[c].price * (50 + rand() % 3 * 5) / 100 / 10 * 10;
        sprintf(text, "This %s brought me luck.  Yours for %s.", g_rraCharms[c].name, Money(m1, *price));
    } else {
        int dmg = g_arsenal.s.dmgLv[w] < RRA_MAXLV && (g_arsenal.s.reachLv[w] >= RRA_MAXLV || rand() % 2);
        int lv = dmg ? g_arsenal.s.dmgLv[w] : g_arsenal.s.reachLv[w];
        *what = dmg ? 0 : 1;
        *price = RraUpgradeCost(w, lv) / 2 / 10 * 10;
        if (*price > cash) return 0;
        sprintf(text, dmg ? "I can weight that %s of yours so it hits harder.  %s." : "I can stretch that %s of yours for more reach.  %s.", g_rraWeapons[w].name, Money(m1, *price));
    }
    return k;
}

/* race_messages.c (CommentaryText, the Schmooze page): character c talks - in a Big Game, once between races, maybe with an offer */
void rr_ArsenalSchmoozeChar(int c)
{
    char b[300];
    g_schmoozeChar = c;
    memset(&g_offer, 0, sizeof g_offer);
    if (!BigGame() || g_offerMade || c < 1 || c > 8) return;
    g_offer.kind = MakeOffer(c, g_offer.text, &g_offer.price, &g_offer.what, &g_offer.extra);
    if (!g_offer.kind) return;
    g_offerMade = 1;
    sprintf(b, "schmooze: offer %d from %d (what %d, $%d): %s", g_offer.kind, c, g_offer.what, g_offer.price, g_offer.text);
    ArsLog(b);
}

/* ui\menu_widgets.c (the Schmooze page's text): the offer under the quote, and the last line - "[ Yes ]   No" / "Yes   [ No ]" (*sel = the one
   picked) or the reply.  0 = no offer on this page */
int rr_ArsenalOffer(const char **text, const char **last, int *sel)
{
    if (!g_offer.kind || (*(int *)&rec_menuScreen.g_menuScreen) != 0x1a) return 0;
    *text = g_offer.text; *sel = g_offer.answered ? -1 : g_offer.sel;
    *last = g_offer.answered ? g_offer.reply : g_offer.sel ? "  Yes          [ No ]" : "[ Yes ]          No";
    return 1;
}
static int g_yesNo[4];   /* the Yes / No line as laid out in the frame (left, top, right, bottom; the window's sharp text may wrap a line more or less) */
void rr_ArsenalOfferPlaces(const int *line) { memcpy(g_yesNo, line, sizeof g_yesNo); }

static void Redraw(void)
{
    unsigned *it = (unsigned *)(&tbl_controlRecords)[0];
    if (it) rr_MenuItemDraw(it, it);
    InvalidateRect(g_mainWindow, NULL, TRUE);
}
static void Answer(int yes)
{
    char b[160], m[24];
    const char *tg = g_offer.kind == 1 ? rr_CharacterNameOf((unsigned char)g_offer.what) : "";
    g_offer.answered = 1;
    if (!yes) strcpy(g_offer.reply, "Suit yourself.");
    else if (g_offer.kind != 1 && g_offer.price > rec_playerRecord.g_playerCash) { sprintf(g_offer.reply, "You're %s short.  Come back with cash.", Money(m, g_offer.price - rec_playerRecord.g_playerCash)); yes = 0; }
    else {
        if (g_offer.kind == 1) {
            g_arsenal.s.bountyChar = (unsigned char)g_offer.what; g_arsenal.s.bountyBy = (unsigned char)g_schmoozeChar;
            g_arsenal.s.bountyReward = g_offer.price; g_arsenal.s.bountyPenalty = g_offer.extra;
            sprintf(g_offer.reply, "Deal.  %s is marked on your course map.", tg);
        } else {
            int w = g_arsenal.s.equipped;
            rec_playerRecord.g_playerCash -= g_offer.price;
            if (g_offer.kind == 4) { g_arsenal.s.charms |= (unsigned char)(1 << g_offer.what); sprintf(g_offer.reply, "Look after the %s.  It works.", g_rraCharms[g_offer.what].name); }
            else if (g_offer.kind == 2) { g_arsenal.s.owned |= (unsigned char)(1 << g_offer.what); g_arsenal.s.equipped = (unsigned char)g_offer.what; sprintf(g_offer.reply, "Pleasure doing business.  You carry the %s now.", g_rraWeapons[g_offer.what].name); }
            else { if (g_offer.what == 0) g_arsenal.s.dmgLv[w]++; else g_arsenal.s.reachLv[w]++; sprintf(g_offer.reply, "Done.  Your %s is better already.", g_rraWeapons[w].name); }
        }
        g_unsavedChanges = 1;
    }
    sprintf(b, "schmooze: answered %s", yes ? "yes" : "no");
    ArsLog(b);
    Redraw();
}
/* window.c (a key on a menu item): on an open offer Left / Right pick Yes / No, Y / N answer at once.  1 = taken */
int rr_ArsenalOfferKey(int vk)
{
    if (!g_offer.kind || g_offer.answered || (*(int *)&rec_menuScreen.g_menuScreen) != 0x1a) return 0;
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == 0x64 || vk == 0x66) { g_offer.sel ^= 1; Redraw(); return 1; }
    if (vk == 'Y' || vk == 'N') { Answer(vk == 'Y'); return 1; }
    return 0;
}
/* menu_pages.c rr_ResultPageStep (Enter / a click on the page): an open offer is answered with the choice instead of the next rider.  1 = taken */
int rr_ArsenalOfferEnter(void)
{
    if (!g_offer.kind || g_offer.answered || (*(int *)&rec_menuScreen.g_menuScreen) != 0x1a) return 0;
    Answer(g_offer.sel == 0);
    return 1;
}
/* window.c (a click on the Schmooze page, frame pixels x, y): on an open offer only a click on the Yes / No line counts - its left part Yes, the rest
   No (a line's height of slack: the window's text may wrap a line more or less).  1 = taken (answered or ignored) */
int rr_ArsenalOfferClick(int x, int y)
{
    int h = g_yesNo[3] - g_yesNo[1];
    if (!g_offer.kind || g_offer.answered || (*(int *)&rec_menuScreen.g_menuScreen) != 0x1a) return 0;
    if (x >= g_yesNo[0] - 6 && x < g_yesNo[2] && y >= g_yesNo[1] - h && y < g_yesNo[3] + h) { g_offer.sel = x >= g_yesNo[0] + 90; Answer(g_offer.sel == 0); }
    return 1;
}

static void CALLBACK ArsTimer(HWND h, UINT msg, UINT_PTR id, DWORD t)
{
    static int busy;
    (void)msg; (void)t;
    KillTimer(h, id);
    if (busy || !g_note[0]) return;
    busy = 1;
    {   char n[sizeof g_note], tt[sizeof g_noteTitle];
        strcpy(n, g_note); strcpy(tt, g_noteTitle); g_note[0] = 0;
        Ask(n, tt, MB_OK); }
    busy = 0;
}

/* menu_screen.c rr_MenuScreenShow: screen s is up.  A message left by the race comes on the first screen
   after it (a timer: once the page is drawn) */
void rr_ArsenalScreen(int s)
{
    if (s != 0x1a) memset(&g_offer, 0, sizeof g_offer);
    if (g_note[0]) SetTimer(g_mainWindow, 0x5a11, 400, ArsTimer);
}
