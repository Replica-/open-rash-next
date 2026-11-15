/* The player's own settings kept in the save file (user, 2026-11-15: "save all the settings to the save file please like purchases, progress and stuff
 * like that") - shared by the game (src\system\savegame.c: the .RRN file) and the shim (src\platform\ddraw\playercfg.inc: RR_PlayerCfg, the shim's
 * own copies in video.ini / rider_colours.ini).  The machine's settings (window, video, True 3D detail, controls, sound) are not in it: a save
 * loaded on a Steam Deck keeps the Deck's. */
#ifndef PLAYERCFG_API_H
#define PLAYERCFG_API_H

#define RRP_PARTS 5                         /* rider colour parts: helmet, jacket, stripes, trousers, bike (ridercol.inc) */

typedef struct RrPlayerCfg {
    int size;                               /* sizeof (RrPlayerCfg) when filled in (0 = none: the shim was not there) */
    int rcPart[RRP_PARTS];                  /* Game Options > Change clothes: -1 = the rider's own, 0..14 = that scheme's shades, 15 = rcRGB */
    unsigned rcRGB[RRP_PARTS];              /* the parts' own colours (0x00RRGGBB) */
    int rcRow;                              /* the player's colour scheme in the last race (the editor's preview) */
    int opponents;                          /* Game Options > Opponents (1..50) */
    int timeOfDay, weather, dayMinutes, fog;   /* True 3D races, the Esc panel (weather.inc) */
    int weapon;                             /* the club's picture (weapons.inc) */
    int traffic;                            /* Game Options > Traffic (0 Default .. 3 Rush hour) */
    int police;                             /* Game Options > Police (0 Default .. 3 Relentless) */
    int reserved[6];
} RrPlayerCfg;

#endif
