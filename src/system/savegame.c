/* Player file (*.RRN) save and load - SavePlayerFile (save) and LoadPlayerFile (load), first hand-converted from the original's .RRS file.
 *
 * The .RRN file (user, 2026-11-15: "save all the settings to the save file please like purchases, progress ... rename the extension for the save
 * files to RRN and do not allow the old saves to be loadable"): everything that is the player's in one block, little endian -
 *   0x00  uint32  magic      "RRN1"
 *   0x04  uint32  version    RRN_VERSION
 *   0x08  uint32  size       sizeof (RrnFile)
 *   0x0c  uint32  checksum   Checksum() over the bytes after it
 *   0x10  uint32  player[8]  the live player block g_playerRecord: character, level, races won, bike, cash, name ... (byte [1] selects the record set)
 *   0x30  uint32  rec[10][3] the player's ten 12-byte records from the table at g_recordsTable (record set * 12 bytes, 0x204 bytes between records)
 *   0xa8  RrArsenalSave      the purchases: weapons and their upgrades, gear, supplies, charms, insurance, the bounty (race\arsenal.c)
 *         RrPlayerCfg        the player's settings: rider colours, Game Options, time of day / weather / fog, the weapon picture (the shim's: RR_PlayerCfg)
 * The save screen's tiles read the first 0x30 bytes (platform\ddraw\savedlg.inc).  An old .RRS file (magic 0xCAFEDEAD) is not loaded.
 * The file name is in g_playerFilePath (set by the menus when a player is chosen/created).
 */
#include "../rr.h"
#include "../data/records.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../platform/arsenal_api.h"
#include "../platform/playercfg_api.h"

#define RRN_MAGIC     0x314E5252u   /* "RRN1" */
#define RRN_VERSION   1
#define REC_COUNT     10
#define REC_STRIDE    0x204      /* bytes between consecutive records of the 10 */
#define REC_SIZE      12

typedef struct RrnFile {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t checksum;
    uint32_t player[8];
    uint32_t rec[REC_COUNT][3];
    RrArsenalSave arsenal;
    RrPlayerCfg cfg;
} RrnFile;
#define RRN_SUMMED(f) ((unsigned char *)(f) + 16)
#define RRN_NSUMMED   ((unsigned)(sizeof (RrnFile) - 16))

extern RrArsenal g_arsenal;      /* race\arsenal.c */
void rr_ArsenalReset(void);
void rr_ArsenalSet(const RrArsenalSave *s);

RR_DATA(char,    g_playerFilePath);  /* path of the current player file */
RR_DATA(char,    g_unsavedChanges);  /* "unsaved changes" flag */
RR_DATA(int,     g_configBlock);  /* pointer to the string/config block */

/* original helpers (called by their bare original names; the originals or their replacements are used) */
unsigned int __fastcall Checksum(unsigned char *data, unsigned int count);      /* checksum (already converted) */
#pragma comment(linker, "/alternatename:@Checksum@8=Checksum")
void __cdecl CrtStrerror(int err);                                                 /* remember last error text */
#pragma comment(linker, "/alternatename:_CrtStrerror=CrtStrerror")
void __cdecl MessageBoxShow(LPCSTR title, char *fmt, ...);                            /* message box / log (src\game\messages.c) */
#pragma comment(linker, "/alternatename:_MessageBoxShow=MessageBoxShow")
void __stdcall SkillTablesRegenerate(void);                                                  /* reset player state before a load */
#pragma comment(linker, "/alternatename:_SkillTablesRegenerate@0=SkillTablesRegenerate")
int  __fastcall DirectionMirror(char v);                                               /* maps player byte -> record set */
#pragma comment(linker, "/alternatename:@DirectionMirror@4=DirectionMirror")
void __fastcall MenuEnableBuild(int mode);                                             /* refresh after load */
#pragma comment(linker, "/alternatename:@MenuEnableBuild@4=MenuEnableBuild")

static uint32_t *record_ptr(int set, int i)
{
    return (uint32_t *)(&(*(uint8_t *)&rec_sndPlayerEngine.g_recordsTable) + set * REC_SIZE + i * REC_STRIDE);
}

/* the shim's player settings (platform\ddraw\playercfg.inc): set = 0 read them into c, 1 put c in; 0 = no shim */
static int player_cfg(RrPlayerCfg *c, int set)
{
    HMODULE sh = GetModuleHandleA("rrddraw.dll");
    int (__stdcall *f)(RrPlayerCfg *, int) = sh ? (int (__stdcall *)(RrPlayerCfg *, int))GetProcAddress(sh, "RR_PlayerCfg") : 0;
    return f ? f(c, set) : 0;
}

/* the player's whole state into f */
static void rrn_make(RrnFile *f)
{
    int i, k;
    memset(f, 0, sizeof *f);
    for (i = 0; i < 8; i++)
        f->player[i] = (&(*(uint32_t *)&rec_playerRecord.g_playerRecord))[i];
    for (i = 0; i < REC_COUNT; i++) {
        const uint32_t *src = record_ptr(rec_fadeFactors.g_selectedRecordSet, i);
        for (k = 0; k < 3; k++)
            f->rec[i][k] = src[k];
    }
    f->arsenal = g_arsenal.s;
    if (!player_cfg(&f->cfg, 0)) memset(&f->cfg, 0, sizeof f->cfg);   /* (cfg.size 0: no settings, a load keeps the current ones) */
    f->magic = RRN_MAGIC;
    f->version = RRN_VERSION;
    f->size = sizeof *f;
    f->checksum = Checksum(RRN_SUMMED(f), RRN_NSUMMED);
}

RR_REPLACE_STD(SavePlayerFile, rr_SavePlayerFile, 0);
void __stdcall rr_SavePlayerFile(void)
{
    HANDLE h;
    DWORD written;
    RrnFile f;

    h = CreateFileA(&g_playerFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;
    rrn_make(&f);
    if (!WriteFile(h, &f, sizeof f, &written, NULL) || written != sizeof f) {
        CrtStrerror((int)GetLastError());
        MessageBoxShow(NULL, *(char **)(g_configBlock + 0x5bc));
    } else
        g_unsavedChanges = 0;
    CloseHandle(h);
}

RR_REPLACE_STD(LoadPlayerFile, rr_LoadPlayerFile, 0);
void __stdcall rr_LoadPlayerFile(void)
{
    HANDLE h;
    DWORD got = 0;
    int i, k;
    RrnFile f;

    h = CreateFileA(&g_playerFilePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return;

    memset(&f, 0, sizeof f);
    if (!ReadFile(h, &f, sizeof f, &got, NULL)) {
        CrtStrerror((int)GetLastError());
        MessageBoxShow(NULL, *(char **)(g_configBlock + 0x5c0));
    } else if (got != sizeof f || f.magic != RRN_MAGIC || f.version != RRN_VERSION || f.size != sizeof f) {   /* (an old .RRS file too) */
        MessageBoxShow(NULL, *(char **)(g_configBlock + 0x5c4));
    } else if (Checksum(RRN_SUMMED(&f), RRN_NSUMMED) != f.checksum) {
        MessageBoxShow(NULL, *(char **)(g_configBlock + 0x5c8));
    } else {
        SkillTablesRegenerate();
        for (i = 0; i < 8; i++)
            (&(*(uint32_t *)&rec_playerRecord.g_playerRecord))[i] = f.player[i];
        rec_fadeFactors.g_selectedRecordSet = (char)DirectionMirror(rec_playerRecord.g_playerCharacter);
        for (i = 0; i < REC_COUNT; i++) {
            uint32_t *dst = record_ptr(rec_fadeFactors.g_selectedRecordSet, i);
            for (k = 0; k < 3; k++)
                dst[k] = f.rec[i][k];
        }
        rr_ArsenalSet(&f.arsenal);
        if (f.cfg.size == sizeof f.cfg) player_cfg(&f.cfg, 1);
        MenuEnableBuild(2);
        g_unsavedChanges = 0;
    }
    CloseHandle(h);
}

/* test switch RR_TEST_SAVEAS=<file>: at start-up, after the player file named on the command line was loaded, the player is saved to <file> at once
 * (a save / load round trip: the two files must be the same) */
void rr_TestSaveAs(void)
{
    char path[MAX_PATH], keep[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("RR_TEST_SAVEAS", path, sizeof path);
    if (n == 0 || n >= sizeof path) return;
    lstrcpynA(keep, &g_playerFilePath, sizeof keep);
    lstrcpyA(&g_playerFilePath, path);
    rr_SavePlayerFile();
    lstrcpyA(&g_playerFilePath, keep);
}

/* Auto save (the first two slots of the save screen, platform\ddraw\savedlg.inc): after every Big Game race the player file is also written to
 * saves\Autosave1.RRN or saves\Autosave2.RRN, whichever is missing or older.  The game's own state (the "unsaved changes" flag, the current file
 * name) is left as it was.  Not in the automated tests (they never write files). */
RR_DATA(int, g_gameMode);      /* 2 = Big Game (rr_CharacterStartMoney) */
void rr_AutoSave(void)
{
    static const char *const name[2] = { "saves\\Autosave1.RRN", "saves\\Autosave2.RRN" };
    WIN32_FILE_ATTRIBUTE_DATA fa[2];
    int have[2], i, pick;
    RrnFile f;
    HANDLE h;
    DWORD written;
    char e[4];
    if (g_gameMode != 2 || (unsigned char)rec_playerRecord.g_playerCharacter < 1 || (unsigned char)rec_playerRecord.g_playerCharacter > 8) return;
    if (GetEnvironmentVariableA("RR_AUTORACE", e, sizeof e) || GetEnvironmentVariableA("RR_SCRIPT", e, sizeof e) || GetEnvironmentVariableA("RR_HEADLESS", e, sizeof e)) return;
    CreateDirectoryA("saves", NULL);
    for (i = 0; i < 2; i++) have[i] = GetFileAttributesExA(name[i], GetFileExInfoStandard, &fa[i]) != 0;
    pick = !have[0] ? 0 : !have[1] ? 1 : (CompareFileTime(&fa[0].ftLastWriteTime, &fa[1].ftLastWriteTime) <= 0 ? 0 : 1);
    rrn_make(&f);
    h = CreateFileA(name[pick], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, &f, sizeof f, &written, NULL);
    CloseHandle(h);
}
