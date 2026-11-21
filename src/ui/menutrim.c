/* Menu trimming: MenuEnableBuild is the "which menu items are enabled" builder (hand-converted from the disassembly): for the restroom / game-mode / pub pages it walks a
 * Y/N string and sets the enable flag (+0x28 from tbl_menuEnable, i.e. dword 3 of each 0x38-byte menu record) of one record per character.  The Game Mode page's old Mano-A-Mano
 * item is Load Game, its Thrash Mode and Custom Race items are gone (rr_MenuTrimInit); the Restroom's New Game starts a Big Game (options.c rr_GameModePageOpen). */
#include "../rr.h"
#include "../data/records.h"
#include "../legacy_decls.h"
#include <string.h>

RR_DATA(char, tbl_menuEnable);  RR_DATA(int, g_gameMode);  RR_DATA(char, g_playerFilePath);  
RR_DATA(char *, g_menuEnableDefault);RR_DATA(char *, g_menuEnableSavedPlayer);  RR_DATA(char *, g_menuEnableUnsavedPlayer);  RR_DATA(char *, g_menuEnableNoCharacter);
RR_DATA(char *, g_menuEnablePage3);  RR_DATA(char *, g_menuYesNo6);  RR_DATA(char *, g_menuYesNo7);

/* start-up: Restroom record 2 (was "Save Game As...", then "Change clothes" - now on the Game Options screen, modscfg.inc) off and blank (strings.c); the Game Mode
   page's third item (was Mano-A-Mano: tbl_gameModeMenu record 2, enable byte +0x28, action +0x2c) becomes "Load Game" (game_mode.c rr_GameModeLoad);
   the Restroom's record 7 (was Select Character: Big Game Mode now leads to the character select by itself, options.c rr_OptionPageClose) becomes "Game Options"
   (options.c rr_RestroomMods, always on) */
RR_DATA(unsigned char, tbl_gameModeMenu);
int __fastcall rr_GameModeLoad(int *item);
int __fastcall rr_RestroomMods(int *item);

/* the Restroom's order (user, 2026-11-10; Player Level removed 2026-11-13): Load Game, Save Game | New Game, Player Name | Video Options, Game Options, Sound Mix,
   Configure Input, (a blank row) Done, Go Home - "|" = half a row of space.  Each 0x38-byte record keeps its index (its action, text, enable flag); only its y (+0x10)
   and its keyboard links change (+0x1e up, +0x1f down, +0x20 next, +0x21 previous: record indexes, wrapping round).  Record 2 (was Change clothes, now on Game
   Options), record 5 (Player Level) and record 8 (Jukebox) are left out: below the screen, off, and no link leads to them */
static const unsigned char g_rrOrder[] = { 0, 1, 3, 6, 4, 7, 9, 10, 11, 12 };
static const short g_rrY[] = { 114, 142, 184, 212, 254, 282, 310, 338, 394, 422 };
static const unsigned char g_rrGone[] = { 2, 5, 8 };   /* (off in every mode: rr_MenuEnableBuild) */
static void RestroomLayout(void)
{
    int n = (int)sizeof g_rrOrder, k;
    for (k = 0; k < n; k++) {
        unsigned char *r = (unsigned char *)&tbl_menuEnable + g_rrOrder[k] * 0x38;
        *(int *)(r + 0x10) = g_rrY[k];
        r[0x1e] = r[0x21] = g_rrOrder[(k + n - 1) % n];
        r[0x1f] = r[0x20] = g_rrOrder[(k + 1) % n];
    }
    for (k = 0; k < (int)sizeof g_rrGone; k++) {
        *(int *)((char *)&tbl_menuEnable + g_rrGone[k] * 0x38 + 0x10) = 520;
        *(&tbl_menuEnable + g_rrGone[k] * 0x38 + 0x28) = 0;
    }
}

/* The Street (screen 0xf): a fourth item, Hurt Hardware (race\arsenal.c rr_StreetShop; label / help strings.c g_shopTextId), and a fifth, the Restroom
   (user, 2026-11-15: "on the street can you put the restroom there too": the Game Mode page's Restroom item - tbl_gameModeMenu record 3: its target
   screen, label, help; its Done comes back here, options.c rr_MenuDoneAction).  The screen's three 0x38-byte records (Der
   Panzer Klub, Olley's Skoot-A-Rama, Go Home - one row of 213 px wide items) are copied into g_street with the new ones as records 3 and 4, laid out
   in three rows on the Klub's columns: Klub | Restroom, Olley's | Hurt Hardware, Go Home.  Links: +0x1c left, +0x1d right, +0x1e up, +0x1f down,
   +0x20 next, +0x21 previous (Left / Right from the Klub still reach Olley's) */
#define SCREEN_RECS(s) (*(unsigned char **)((char *)&g_menuScreens + (s) * 0x14 + 4))
#define SCREEN_COUNT(s) (*(int *)((char *)&g_menuScreens + (s) * 0x14))
static unsigned char g_street[5 * 0x38];
/* the Street's Restroom item: its Done comes back to The Street, not to the Klub (options.c rr_MenuDoneAction) */
int g_restroomFromStreet;
static int __fastcall StreetRestroom(int *item) { g_restroomFromStreet = 1; return item[1]; }
static void StreetLayout(void)
{
    extern int g_shopTextId;
    int __fastcall rr_StreetShop(int *item);
    /* three rows of two (user, 2026-11-15: "make a new row please and organise them"), the Klub's columns (80 / 360, 200 wide), the Restroom top right as
       in the Klub; rows 26 px apart from just under the rider's status line:  Klub | Restroom, Olley's | Hurt Hardware, Go Home */
    static const short pos[5][3] = { { 80, 398, 200 }, { 80, 424, 200 }, { 80, 450, 200 }, { 360, 424, 200 }, { 360, 398, 200 } };   /* (x, y, width) */
    static const unsigned char link[5][6] = {   /* left, right, up, down, next, previous - reading order Klub, Restroom, Olley's, Hurt Hardware, Go Home */
        { 4, 4, 2, 1, 4, 2 }, { 3, 3, 0, 2, 3, 4 }, { 2, 2, 1, 0, 0, 3 }, { 1, 1, 4, 4, 2, 1 }, { 0, 0, 3, 3, 1, 0 } };
    const unsigned char *rest = &tbl_gameModeMenu + 3 * 0x38;
    int k;
    if (!g_shopTextId || SCREEN_COUNT(0xf) != 3) return;
    memcpy(g_street, SCREEN_RECS(0xf), 3 * 0x38);
    memcpy(g_street + 3 * 0x38, g_street, 0x38);   /* (the Klub's record as the pattern: flags, style) */
    *(int *)(g_street + 3 * 0x38 + 4) = 0;   /* (no target screen: the action returns it) */
    *(int *)(g_street + 3 * 0x38 + 8) = g_shopTextId;
    *(int *)(g_street + 3 * 0x38 + 0x30) = g_shopTextId + 1;
    *(unsigned *)(g_street + 3 * 0x38 + 0x2c) = (unsigned)rr_StreetShop;
    memcpy(g_street + 4 * 0x38, g_street, 0x38);   /* the Restroom: the Klub's look, the Game Mode item's target, text and action */
    *(int *)(g_street + 4 * 0x38 + 4) = *(const int *)(rest + 4);
    *(int *)(g_street + 4 * 0x38 + 8) = *(const int *)(rest + 8);
    *(int *)(g_street + 4 * 0x38 + 0x30) = *(const int *)(rest + 0x30);
    *(unsigned *)(g_street + 4 * 0x38 + 0x2c) = (unsigned)StreetRestroom;
    for (k = 0; k < 5; k++) {
        unsigned char *r = g_street + k * 0x38;
        *(int *)(r + 0xc) = pos[k][0]; *(int *)(r + 0x10) = pos[k][1]; *(int *)(r + 0x14) = pos[k][2]; *(int *)(r + 0x18) = 0x1a;
        memcpy(r + 0x1c, link[k], 6);
    }
    SCREEN_RECS(0xf) = g_street;
    SCREEN_COUNT(0xf) = 5;
}

void rr_MenuTrimInit(void)
{
    RestroomLayout();
    StreetLayout();
    *(&tbl_menuEnable + 2 * 0x38 + 0x28) = 0;
    *(&tbl_gameModeMenu + 2 * 0x38 + 0x28) = 1;
    *(unsigned *)(&tbl_gameModeMenu + 2 * 0x38 + 0x2c) = (unsigned)rr_GameModeLoad;
    {   /* the Game Mode row: Big Game Mode, Load Game, Restroom.  Thrash Mode is gone: record 0 (first, where the cursor starts) becomes
           Big Game Mode (target +4 the character select, label +8, action +0x2c); record 1 (the original Big Game Mode; Custom Race until 2026-11-21, user:
           every race is True 3D) is off, below the screen, and no link leads to it.  The three left are spread over the row: x +0x0c, width +0x14, keyboard
           links +0x1c left, +0x1d right, +0x20 next, +0x21 previous */
        static const int rec[3] = { 0, 2, 3 }, x[4] = { 0, 260, 480, 640 };   /* (the labels sit at the left of their box: Restroom stays where the original had it) */
        int __fastcall rr_BigGameModePick(int item);
        unsigned char *gm = &tbl_gameModeMenu;
        int k;
        *(int *)(gm + 4) = 14; *(int *)(gm + 8) = 4; *(int *)(gm + 0x30) = 5; *(unsigned *)(gm + 0x2c) = (unsigned)rr_BigGameModePick; gm[0x28] = 1;   /* (+0x30: the help string) */
        *(int *)(gm + 0x38 + 0x10) = 520; gm[0x38 + 0x28] = 0;
        for (k = 0; k < 3; k++) {
            unsigned char *r = gm + rec[k] * 0x38;
            *(int *)(r + 0x0c) = x[k]; *(int *)(r + 0x14) = x[k + 1] - x[k];
            r[0x1c] = r[0x21] = (unsigned char)rec[(k + 2) % 3];
            r[0x1d] = r[0x20] = (unsigned char)rec[(k + 1) % 3];
        }
    }
    *(unsigned *)(&tbl_menuEnable + 7 * 0x38 + 4) = 0;   /* (no target screen: the action returns it) */
    *(unsigned *)(&tbl_menuEnable + 7 * 0x38 + 0x2c) = (unsigned)rr_RestroomMods;
    *(&tbl_menuEnable + 7 * 0x38 + 0x28) = 1;
    {   /* record 3 = "New Game" (was Game Mode; the page of that name keeps its title): its own label string, strings.c */
        extern int g_newGameTextId;
        if (g_newGameTextId) *(unsigned *)(&tbl_menuEnable + 3 * 0x38 + 8) = (unsigned)g_newGameTextId;
    }
}

RR_REPLACE_FAST(MenuEnableBuild, rr_MenuEnableBuild, 4);
unsigned __fastcall rr_MenuEnableBuild(int page)
{
    char *flag = &tbl_menuEnable + 0x0;
    const char *yn;
    if (page == 2) {
        if (rec_playerRecord.g_playerCharacter == 0) yn = g_menuEnableNoCharacter;
        else if (g_playerFilePath != 0) yn = g_menuEnableSavedPlayer;
        else yn = g_menuEnableUnsavedPlayer;
    } else if (page == 3) {
        yn = g_menuEnablePage3;
    } else yn = g_menuEnableDefault;
    while (*yn != 0) {
        char c = *yn++;
        if (c == 'N') flag[0x28] = 0;
        else if (c == 'Y') flag[0x28] = 1;
        flag += 0x38;
    }
    {   /* Restroom (page = the game mode, the records are the Restroom's in every mode): record 1 = Save Game (GameSave), record 2 = was Save Game As...,
           now blank and always off (Change clothes is on the Game Options screen).  "Save Game" is on whenever either save item was */
        char *save = &tbl_menuEnable + 1 * 0x38 + 0x28, *saveAs = &tbl_menuEnable + 2 * 0x38 + 0x28;
        *save = (char)(*save || *saveAs);
        *saveAs = 0;
        *(&tbl_menuEnable + 7 * 0x38 + 0x28) = 1;   /* record 7 = Game Options, in every mode */
        *(&tbl_menuEnable + 5 * 0x38 + 0x28) = 0;   /* record 5 = Player Level, record 8 = Jukebox: not on the Restroom any more (RestroomLayout) */
        *(&tbl_menuEnable + 8 * 0x38 + 0x28) = 0;
    }
    g_gameMode = page;
    return ((unsigned (__fastcall *)(int))RRCODE_DialogControlsRefresh)(0);
}
