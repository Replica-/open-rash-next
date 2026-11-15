/* The player's own settings in the save file (included by rrddraw.c after ridercol.inc; platform\playercfg_api.h; the file: src\system\savegame.c).
 * RR_PlayerCfg(c, 0) fills c in from the shim's settings (rider colours, Game Options, the Esc panel's time of day / weather / fog, the weapon picture);
 * RR_PlayerCfg(c, 1) puts a loaded save's settings in and writes them to video.ini / rider_colours.ini, so they stay the current ones.  A save loaded
 * at start-up (a file named on the command line: startup.c) comes before the shim read video.ini (DirectDrawCreate): it is kept and put in after
 * that (PcAfterVidLoad), or video.ini would overwrite it - and VidSave would write a video.ini of defaults. */
#include "../playercfg_api.h"
static RrPlayerCfg g_pcPending; static int g_pcHave, g_pcVidLoaded;
static void PcPut(const RrPlayerCfg *c)
{   int i;
    if (!g_rcLoaded) RcLoad();
    for (i = 0; i < RRP_PARTS; i++) { g_rcPart[i] = c->rcPart[i] >= RC_OWN && c->rcPart[i] <= RC_CUSTOM ? c->rcPart[i] : RC_OWN; g_rcRGB[i] = c->rcRGB[i] & 0xffffff; }
    g_rcRow = c->rcRow >= -1 && c->rcRow < 15 ? c->rcRow : -1;
    g_vid.opp = c->opponents; g_vid.tod = c->timeOfDay; g_vid.weather = c->weather; g_vid.dayMin = c->dayMinutes; g_vid.fog = c->fog; g_vid.weapon = c->weapon;
    VidSanitize(); VidSave(); RcSave();
    LOG("player settings from the save: opponents %d, time of day %d, weather %d, fog %d, weapon %d, colours %d %d %d %d %d", g_vid.opp, g_vid.tod, g_vid.weather, g_vid.fog, g_vid.weapon, g_rcPart[0], g_rcPart[1], g_rcPart[2], g_rcPart[3], g_rcPart[4]); }
__declspec(dllexport) int __stdcall RR_PlayerCfg(RrPlayerCfg *c, int set)
{   int i;
    if (!c) return 0;
    if (set) {
        if (c->size != sizeof *c) return 0;
        if (g_pcVidLoaded) PcPut(c); else { g_pcPending = *c; g_pcHave = 1; }
        return 1; }
    if (g_pcHave) { *c = g_pcPending; return 1; }   /* (not put in yet: what the save had) */
    if (!g_rcLoaded) RcLoad();
    memset(c, 0, sizeof *c); c->size = sizeof *c;
    for (i = 0; i < RRP_PARTS; i++) { c->rcPart[i] = g_rcPart[i]; c->rcRGB[i] = g_rcRGB[i]; }
    c->rcRow = g_rcRow; c->opponents = g_vid.opp; c->timeOfDay = g_vid.tod; c->weather = g_vid.weather; c->dayMinutes = g_vid.dayMin; c->fog = g_vid.fog; c->weapon = g_vid.weapon;
    return 1; }
/* DirectDrawCreate, after video.ini was read (or not: quiet / test runs) */
static void PcAfterVidLoad(void)
{   g_pcVidLoaded = 1;
    if (g_pcHave) { g_pcHave = 0; PcPut(&g_pcPending); } }
