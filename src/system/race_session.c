/* system\race_session.c - functions of the original's code range 0x441940-0x4435f0,
   regrouped from the hand ports (legacy_u.c, legacy_v.c, legacy_a.c, legacy_o.c, port_small10.c).
   Each RR_REPLACE*(FUN_x, rr_FUN_x, n) replaces the original function at link time. */
#include "../rr.h"
#include "../data/records.h"
#include "../legacy_decls.h"
#include <string.h>
#include <stdarg.h>
#include <commctrl.h>
#include <stdbool.h>

RR_DATA(int,              g_playerRider);
RR_DATA(int,              tbl_trackCarFiles);
RR_DATA(char,             s_Data_Courses);
RR_DATA(char,             s_dotCRS);
RR_DATA(char,             s_Small_Fonts);
RR_DATA(char,             s_New_Courier);
RR_DATA(char,             s_pctspctspcts);
RR_DATA(char,             s_pcts_pctspctspcts);
RR_DATA(char,             g_hiResRace);
RR_DATA(char,             g_windowed);
RR_DATA(char,             g_raceStartDone);
RR_DATA(char,             g_raceRunning);
RR_DATA(int,              g_frameRate);
RR_DATA(int,              g_frameRateSet);
RR_DATA(int,              g_startCountdown);
RR_DATA(char,             g_allocContext);
RR_DATA(void *,           g_flipSurface);
RR_DATA(void *,           g_frameSurface);
RR_DATA(int *,            g_ddPalette);
RR_DATA(int,              g_raceResFile);
RR_DATA(int,              g_raceResLoaded);
RR_DATA(char,             g_pathBuf);
RR_DATA(char,             tbl_gamePalette);
RR_DATA(CRITICAL_SECTION, g_raceLock);
RR_DATA(int,              g_nearestObj);
RR_DATA(HFONT,            g_fontBig);
RR_DATA(HFONT,            g_fontSymbols);
RR_DATA(HFONT,            g_fontCourierSmall);
RR_DATA(HFONT,            g_hudFont);
RR_DATA(int,              g_cheatSpeed);
RR_DATA(HFONT,            g_fontMedium);
RR_DATA(int,              g_raceSeed);
#define g_raceTime_as_int (*(int *)&(*(unsigned *)&rec_featureAccum1.g_raceTime))   /* the same variable with the type another part of this file uses */
#define g_fontBig_as_HGDIOBJ (*(HGDIOBJ *)&g_fontBig)   /* the same variable with the type another part of this file uses */
#define g_fontSymbols_as_HGDIOBJ (*(HGDIOBJ *)&g_fontSymbols)   /* the same variable with the type another part of this file uses */
#define g_fontMedium_as_HGDIOBJ (*(HGDIOBJ *)&g_fontMedium)   /* the same variable with the type another part of this file uses */
#define g_ddPalette_as_int (*(int *)&g_ddPalette)   /* the same variable with the type another part of this file uses */
#define tbl_gamePalette_as_int (*(int *)&tbl_gamePalette)   /* the same variable with the type another part of this file uses */

#define VT(o, off) (((void ***)(o))[0][(off) / 4])
#define VCALL(obj, off) ((*(int (__stdcall ***)())(obj))[(off) / 4])

int __cdecl crt_vsprintf(char *buf, const char *fmt, char *va);  __pragma(comment(linker, "/alternatename:_crt_vsprintf=_rr_vsprintf"))
void rr_TestTick(void);   /* src\test\test_switches.c */
void rr_GpuRaceBegin(void);   /* src\render\gpu_scene.c */
int __cdecl sprintf(char *dst, const char *fmt, ...);  /* the game's own statically linked CRT */

void __fastcall rr_GameExit(int code);
void __stdcall rr_BankClose(void);
void __stdcall rr_InputFree(void);

/* loads the track's resource file (SoundBankLoad) and fills the resource table at tbl_raceResources (5 dwords per entry: destination pointer, tag, size, scratch, data): data blocks are allocated, 'LEC ' blocks decoded, 'MINA' blocks registered; returns RelocTreeRoot's result */
RR_REPLACE_STD(RaceResourcesLoad, rr_RaceResourcesLoad, 0);
int __stdcall rr_RaceResourcesLoad(void)
{
    int *e;
    int i;
    char *name = (char *)PathFind((int)&s_Data_Courses, (&tbl_trackCarFiles)[(*(signed char *)&rec_fadeFactors.g_trackIndex)], (int)&s_dotCRS);
    g_raceResFile = SoundBankLoad((int)name, (unsigned char)g_allocContext);
    if (!g_raceResFile) rr_GameExit(rec_sndPlayerEngine.g_streamRecord);
    if ((*(int *)&rec_raceResources.tbl_raceResources)) {
        e = &(*(int *)&rec_raceResources.tbl_raceResources);
        do {
            if (e[4] == 0) {
                int p = ResourceGet(e[1], e[2] + 1, 8);
                e[3] = p;
                if (p) e[4] = p;
            }
            e += 5;
        } while (e[0] != 0);
    }
    i = 0;
    if ((*(int *)&rec_raceResources.tbl_raceResources)) {
        e = &(*(int *)&rec_raceResources.tbl_raceResources);
        do {
            if (e[4]) {
                if (e[1] == 0x43454c20) {
                    e[4] = ChunkScan(e[4], 0);
                    if (!e[4]) rr_GameExit(i + 1);
                } else if (e[1] == 0x414e494d) ModelBlobFix(e[4]);
                *(int *)e[0] = e[4];
            }
            e += 5; i++;
        } while (e[0] != 0);
    }
    g_raceSeed = RelocTreeRoot();
    return g_raceSeed;
}

/* clear the "pending" pairs of a 5-dword table, then release the object */
RR_REPLACE(RaceResourcesFree, rr_RaceResourcesFree);
unsigned int __cdecl rr_RaceResourcesFree(void)
{
    if (rec_raceResources.g_raceResourcesLoaded) {
        int *p = &(*(int *)&rec_raceResources.tbl_raceResources1);
        int next;
        do {
            if (*p) { p[-1] = 0; *p = 0; }
            next = p[4];
            p += 5;
        } while (next);
    }
    return SpriteSetFree(g_raceResFile);
}

RR_REPLACE_STD(RaceTablesInit, rr_RaceTablesInit, 0);
unsigned __stdcall rr_RaceTablesInit(void) { SkillTablesRegenerate(); SoundFlowTick(); return BankOpen(); }

RR_REPLACE_STD(RaceTablesFree, rr_RaceTablesFree, 0);
void __stdcall rr_RaceTablesFree(void)
{
    rr_BankClose();
    rr_InputFree();
    ChannelMemFree();
}

/* the race start's loading screen (the shim's loadscreen.inc, RR_LoadProgress): how far the set-up is, 0..1000, and what it is doing (NULL = the same);
   the track (1..5) and level (1..5) for its title */
void rr_LoadStep(int permille, const char *what)
{
    static void (__stdcall *f)(int, const char *, int, int); static int looked;
    if (!looked) { HMODULE sh = GetModuleHandleA("rrddraw.dll"); f = sh ? (void (__stdcall *)(int, const char *, int, int))GetProcAddress(sh, "RR_LoadProgress") : 0; looked = 1; }
    if (f) f(permille, what, (*(int *)&rec_demoPage.g_raceTrack), (*(unsigned char *)&rec_playerRecord.g_playerLevel) + 1);
}

/* start of a race: resets the shared state, loads the track resources (RaceResourcesLoad), builds the UI fonts (sizes depend on g_hiResRace = high-res UI) */
RR_REPLACE(RaceSetup, rr_RaceSetup);
void __cdecl rr_RaceSetup(void)
{
    int *obj, h;
    rr_LoadStep(0, "Track resources");
    PictureStateReset();
    PoolMark1();
    g_raceResLoaded = 1;
    (&g_cheatSpeed)[0] = 0; (&g_cheatSpeed)[1] = 0; (&g_cheatSpeed)[2] = 0; (&g_cheatSpeed)[3] = 0;
    TrackResourcesLoad();
    rr_LoadStep(80, "Race sprites and sounds");
    {   /* widescreen races (automatic when the window is wider than 4:3): the shim widens the frame (returns its width, 0 = normal 640); the projection centre / culling limit g_projWidth is the frame width in half-pixels (hi-res frames only) */
        int (__stdcall *rb)(void) = g_hiResRace ? (int (__stdcall *)(void))GetProcAddress(GetModuleHandleA("rrddraw.dll"), "RR_RaceBegin") : 0;
        int ws = rb ? rb() : 0;
        rec_Controls.g_screenMidX = rec_Controls.g_projWidth = ws ? ws / 2 : 320;
        rr_GpuRaceBegin();   /* the world is drawn by the shim's GPU renderer (src\render\gpu_scene.c) */
    }
    obj = (int *)rr_RaceResourcesLoad();
    if (obj && obj[4] && *(int *)(obj[4] + 0xc)) AssetSystemStart(*(int *)(obj[4] + 0xc));
    rr_LoadStep(200, "The road");   /* (RaceReset builds it; then the course and the True 3D world: render\world3d_feed.c, the shim) */
    ScreenOffsetSet(rec_Controls.g_projWidth, rec_Controls.g_screenMidY);
    g_nearestObj = 0; (*(unsigned *)&rec_featureAccum1.g_raceTime) = 0;
    RoadViewReset();
    RaceReset((int)obj);
    FeatureCountersReset();
    if (g_hiResRace) {
        g_fontMedium = CreateFontA(0x20, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 0, 4, &(*(char *)&s_fontNames.s_Badloc));
        g_fontBig = CreateFontA(0x80, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 0, 4, &(*(char *)&s_fontNames.s_Badloc));
        h = 0x12;
    } else {
        g_fontMedium = CreateFontA(0x12, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 0, 4, &(*(char *)&s_fontNames.s_Badloc));
        g_fontBig = CreateFontA(0x48, 0, 0, 0, 400, 0, 0, 0, 0, 4, 0, 0, 4, &(*(char *)&s_fontNames.s_Badloc));
        h = 0xc;
    }
    g_fontSymbols = CreateFontA(h, 0, 0, 0, 400, 0, 0, 0, 2, 4, 0, 0, 4, &(*(char *)&s_fontNames.s_Wingdings));
    rr_LoadStep(1000, "Get ready to race!");
}

static void dd_clear_and_flip(void)
{
    unsigned int fx[25];
    typedef long (__stdcall *BltFn)(void *, void *, void *, void *, unsigned, void *);
    typedef long (__stdcall *FlipFn)(void *, void *, unsigned);
    typedef long (__stdcall *StatusFn)(void *, unsigned);
    memset(fx, 0, sizeof fx);
    fx[0] = 0x64;
    ((BltFn)VT(g_frameSurface, 0x14))(g_frameSurface, 0, 0, 0, 0x1000400, fx);
    ((FlipFn)VT(g_flipSurface, 0x2c))(g_flipSurface, 0, 1);
    while (((StatusFn)VT(g_flipSurface, 0x48))(g_flipSurface, 2) == (long)0x8876021c) Sleep(0x19);
}

RR_REPLACE(RaceTeardown, rr_RaceTeardown);
void __cdecl rr_RaceTeardown(void)
{
    g_startCountdown = 0;
    FinishOrderEstimate(g_raceTime_as_int);
    RoadViewNoop();
    ShutdownStep();
    StreamerShutdown();
    dd_clear_and_flip();
    dd_clear_and_flip();
    dd_clear_and_flip();
    WaitForSingleObject((*(HANDLE *)&rec_featureAccum1.g_loaderThread), 0xffffffff);
    CloseHandle((*(HANDLE *)&rec_featureAccum1.g_loaderThread));
    RaceResourcesFree();
    TrackResourcesFree();
    DeleteObject(g_fontSymbols_as_HGDIOBJ);
    DeleteObject(g_fontBig_as_HGDIOBJ);
    DeleteObject(g_fontMedium_as_HGDIOBJ);
    PoolRewind1();
}

RR_REPLACE_STD(RaceTimersReset, rr_RaceTimersReset, 0);
unsigned __stdcall rr_RaceTimersReset(void)
{
    g_raceStartDone = 0; g_startCountdown = 0; (*(int *)&rec_featureAccum1.g_inputBits) = 0;
    g_frameRate = 0xb4;
    g_frameRateSet = g_frameRate;
    return KeyBindingsAnySet();
}

/* the per-frame pump of the race / menu loop: ages the timers, drains the Windows messages (WM_QUIT sets the quit flag), polls the joystick / key state, runs the sound
 * and screen-fade steps; returns the quit flag */
RR_REPLACE_FAST(FramePump, rr_FramePump, 4);
unsigned __fastcall rr_FramePump(int step)
{
    MSG msg;
    rr_TestTick();
    rec_sndPlayerEngine.g_viewBehind = 0;
    if (g_startCountdown > 0) {
        rec_sndPlayerEngine.g_raceActive = 0;
        g_startCountdown -= step;
        if (g_startCountdown <= 0) g_raceStartDone = 1;
    }
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_raceStartDone = 1;
        else { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    (*(int *)&rec_featureAccum1.g_inputBits) = (int)InputPoll();
    ((unsigned (__stdcall *)(void))RRCODE_RaceSoundTick)();
    if (g_raceStartDone == 0) {
        if (g_frameRateSet > 0) {
            g_frameRateSet -= step;
            if (g_frameRateSet <= 0) {
                VCALL(g_ddPalette_as_int, 0x18)(g_ddPalette_as_int, 0, 0, 0x100, &tbl_gamePalette_as_int);   /* palette SetEntries */
                g_frameRateSet = 0;
            }
            {   /* the shim (weather.inc RR_RaceCountdown): the start countdown's ticks left and the race palette - it shows the countdown undimmed
                   (drawing only: the frame and the game state stay the original's) and says "3, 2, 1, Go!" */
                static void (__stdcall *cd)(int, const void *); static int init;
                if (!init) { HMODULE m = GetModuleHandleA("rrddraw.dll"); init = 1; cd = m ? (void (__stdcall *)(int, const void *))GetProcAddress(m, "RR_RaceCountdown") : 0; }
                if (cd) cd(g_frameRateSet, &tbl_gamePalette);
            }
        } else GameTicks(step);
    }
    return (unsigned char)g_raceStartDone;
}

RR_REPLACE(PromptKey, rr_PromptKey);
int __cdecl rr_PromptKey(int escCancels, char *fmt, ...)
{
    MSG msg;
    int result = 4;
    int (__stdcall *pause)(int), (__stdcall *pauseMouse)(const MSG *), pk;
    va_list ap;
    va_start(ap, fmt);
    crt_vsprintf(&(*(char *)&rec_sndPlayerEngine.g_logBuf), fmt, ap);
    va_end(ap);
    if ((*(char *)&rec_sndPlayerEngine.g_musicPlaying)) MidiSongPoll();
    SoundsStopAll();
    g_raceRunning = 1;
    if (g_frameRateSet == 0) ((HRESULT (__stdcall *)(void *, int, int, int, void *))((int *)*g_ddPalette)[0x18 / 4])(g_ddPalette, 0, 0, 0x100, &(*(char *)&rec_featureAccum1.g_targetPalette));
    {   /* the Esc prompt in a True 3D race also shows the shim's time of day / weather panel (weather.inc RR_PauseKey / RR_PauseMouse): the arrow keys
           and the mouse change those settings there instead of answering the prompt, and its Close / Exit Race buttons answer it; Pseudo 3D (the
           shim says no) is the original prompt */
        static int (__stdcall *pkf)(int); static int (__stdcall *pmf)(const MSG *); static int init;
        if (!init) { HMODULE m = GetModuleHandleA("rrddraw.dll"); init = 1; pkf = m ? (int (__stdcall *)(int))GetProcAddress(m, "RR_PauseKey") : 0;
                     pmf = m ? (int (__stdcall *)(const MSG *))GetProcAddress(m, "RR_PauseMouse") : 0; }
        pause = escCancels && pkf ? pkf : 0;
        pauseMouse = pause ? pmf : 0;
        if (pause) pause(-1);
    }
    do {
        GameFrameDraw();
        while (PeekMessageA(&msg, 0, 0, 0, 1)) {
            if (pauseMouse && msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) {   /* (2 = Close: ride on, 3 = Exit Race) */
                pk = pauseMouse(&msg);
                if (pk >= 2) { result = pk == 3 ? 1 : 2; break; }
                continue;
            }
            if (msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) {
                if (pause && (pk = pause((int)msg.wParam)) != 0) { if (pk >= 2) { result = pk == 3 ? 1 : 2; break; } continue; }
                if (msg.wParam == 0x79) { if (++(*(char *)&rec_fadeFactors.g_hudMode) > 2) (*(char *)&rec_fadeFactors.g_hudMode) = 0; }
                else if (msg.wParam == 0x2c) rec_sndPlayerEngine.g_modeChangePending = 1;
                else { result = 2; if (escCancels) result = (msg.wParam == 0x1b) ? 1 : 2; break; }
            }
        }
    } while (result == 4);
    if (pause) pause(-2);
    g_raceRunning = 0;
    if (result == 2) {
        if (g_frameRateSet == 0) ((HRESULT (__stdcall *)(void *, int, int, int, void *))((int *)*g_ddPalette)[0x18 / 4])(g_ddPalette, 0, 0, 0x100, &tbl_gamePalette);
        UnusedSoundsPauseAll();
        if ((*(char *)&rec_sndPlayerEngine.g_musicPlaying)) MidiSongResume();
    }
    rec_sndPlayerEngine.g_viewBehind = 1;
    return result;
}

/* probe the two candidate paths built from the format strings; returns the existing one (in the shared buffer) or 0 */
RR_REPLACE_FAST(PathFind, rr_PathFind, 12);
char * __fastcall rr_PathFind(int a, int b, int c)
{
    if (b == 0) return NULL;
    sprintf(&g_pathBuf, &s_pcts_pctspctspcts, &(*(char *)&rec_featureAccum1.g_gameDir), a, b, c);
    if (CrtAccess(&g_pathBuf, 0) == 0) return &g_pathBuf;
    sprintf(&g_pathBuf, &s_pctspctspcts, a, b, c);
    return CrtAccess(&g_pathBuf, 0) == 0 ? &g_pathBuf : NULL;
}
