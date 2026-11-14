/* ui\game_mode.c - functions of the original's code range 0x445070-0x4450e0,
   regrouped from the hand ports (legacy_t.c).
   Each RR_REPLACE*(FUN_x, rr_FUN_x, n) replaces the original function at link time. */
#include "../rr.h"
#include "../data/records.h"
#include "../legacy_decls.h"

RR_DATA(int,  g_gameMode);

void __fastcall rr_GameModeSwitch(int page);

/* switches the game mode (2 = Big Game, the only one left: Thrash Mode and Mano-A-Mano are removed): a different mode starts with a fresh character
   (player, level, bike record set) before the page's items are rebuilt */
RR_REPLACE_FAST(GameModeSwitch, rr_GameModeSwitch, 4);
void __fastcall rr_GameModeSwitch(int page)
{
    if (page != 2) return;
    if (g_gameMode != page) {
        rec_playerRecord.g_playerLevel = 0; rec_playerRecord.g_tracksWonMask = 0; rec_playerRecord.g_playerCharacter = 0; rec_fadeFactors.g_selectedRecordSet = 0;
        SkillTablesRegenerate();
        { void rr_ArsenalReset(void); rr_ArsenalReset(); }   /* bare knuckles, no bounty (race\arsenal.c) */
    }
    MenuEnableBuild(page);
}

/* the Game Mode page's third item (was Mano-A-Mano, record 2 of tbl_gameModeMenu; menutrim.c rr_MenuTrimInit points it here, strings.c renames it):
   "Load Game" - the load screen (the Restroom's Load Game, options.c rr_GameLoad); after a load (Big Game mode, a character) straight on to the
   Big Game hub, the page character select leads to (g_bigGameHub); else stay */
RR_DATA(int, g_bigGameHub);
int __cdecl rr_GameLoad(void);
int __fastcall rr_GameModeLoad(int *item)
{
    (void)item;
    rec_playerRecord.g_playerCharacter = 0;   /* (a load sets it: success = a character again) */
    rr_GameLoad();
    return (g_gameMode == 2 && rec_playerRecord.g_playerCharacter != 0) ? g_bigGameHub : 0;
}

/* the "Big Game Mode" item of the Game Mode page: select the mode, return the item's target page */
RR_REPLACE_FAST(BigGameModePick, rr_BigGameModePick, 4);
int __fastcall rr_BigGameModePick(int item) { rr_GameModeSwitch(2); return ((int *)item)[1]; }
