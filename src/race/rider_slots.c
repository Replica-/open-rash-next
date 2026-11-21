/* race\rider_slots.c - more (or fewer) opponents (not part of the original game; user 2026-11-12: Game Options > Opponents, 1..50).
 *
 * The original races 16 rider slots: 0 = the player, 1..14 = the computer riders, 15 = the police (rec_riders: tbl_riders[15] + g_spawnedRider,
 * tbl_rankRiders[15] + g_copRec), and every slot ("kind") has blocks of its own: the kind parameters (rec_riderKindParams, 0x40 bytes), the rider record
 * (0x204 bytes, RiderRecordBind: slots 0..8 g_recordsTable, 9..14 tbl_riderRecords, 15 g_copRecord), a pair of start values (tbl_bikePairs: lateral
 * position, road position) and a bike per level (tbl_thrashBikes).
 *   up to 14 opponents  the original slots, nothing else changes (slots past the count stay empty, as the Big Game leaves the player's character's)
 *   more                rr_slots = opponents + 2: the police take the last slot, the extra riders 15 .. rr_slots - 2.  The rider arrays and the blocks of
 *                       slots 15 and up are this file's (RrRiders / RrRankRiders, RrKindParams, RrRecord); an extra rider borrows a computer
 *                       rider's character (RrSlotChar: specs, sprites, bike, record values) but gets a name of its own (RrRiderName) and a random
 *                       colour scheme of the 15 (RrSlotColour).  The original data is never written then, so 14 opponents is the original exactly.
 * The count comes from the shim (Game Options, RR_Opponents) or the test switch RR_OPPONENTS=<n>. */
#include "../rr.h"
#include "../data/records.h"
#include "../legacy_decls.h"

RR_DATA(int, g_riderCount);
RR_DATA(int, tbl_bikePairsLow);  RR_DATA(int, tbl_bikePairsHigh);

#include "rider_slots.h"   /* (RR_SLOTS_MAX: the player, 50 computer riders, the police) */
int rr_slots = 16;                            /* this race's slots (g_riderCount) */
int rr_opponents = 14;                        /* this race's computer riders */
static int g_riders[RR_SLOTS_MAX], g_rank[RR_SLOTS_MAX];
static char g_kind[RR_SLOTS_MAX][0x40];
static char g_rec[RR_SLOTS_MAX][0x204];
static unsigned char g_colour[RR_SLOTS_MAX];

static int Wanted(void)
{
    char e[8]; int n = -1, i;
    if (GetEnvironmentVariableA("RR_OPPONENTS", e, sizeof e) > 0) { n = 0; for (i = 0; e[i] >= '0' && e[i] <= '9'; i++) n = n * 10 + (e[i] - '0'); }
    else { HMODULE sh = GetModuleHandleA("rrddraw.dll"); int (__stdcall *f)(void) = sh ? (int (__stdcall *)(void))GetProcAddress(sh, "RR_Opponents") : 0; if (f) n = f(); }
    if (n < 1 || n > RR_SLOTS_MAX - 2) n = 14;
    return n;
}

int RrExtended(void) { return rr_slots > 16; }
int RrCopKind(void) { return rr_slots - 1; }
/* the character an extra slot borrows (1..14; the police slot: 15) */
int RrSlotChar(int kind) { if (!RrExtended() || kind < 15) return kind; if (kind == rr_slots - 1) return 15; return 1 + (kind - 15) % 14; }
int *RrRiders(void) { return RrExtended() ? g_riders : &rec_riders.tbl_riders; }
int *RrRankRiders(void) { return RrExtended() ? g_rank : &rec_riders.tbl_rankRiders; }
/* slot i of the whole field, the police included (the original's index 15 is g_spawnedRider / g_copRec) */
int RrRiderAt(int i) { if (RrExtended() && i == rr_slots - 1) return rec_riders.g_spawnedRider; return RrRiders()[i]; }
int RrRankAt(int i) { if (RrExtended() && i == rr_slots - 1) return rec_riders.g_copRec; return RrRankRiders()[i]; }
char *RrKindParams(int kind) { if (!RrExtended() || kind < 15) return &(*(char *)&rec_riderKindParams.tbl_riderKindParams) + kind * 0x40; return g_kind[kind]; }
/* the rider record of a slot (RiderRecordBind) */
static char *OrigRecord(int slot)
{
    if (slot < 9) return (char *)(&(*(unsigned *)&rec_sndPlayerEngine.g_recordsTable) + slot * 0x81);
    if (slot == 15) return &(*(char *)&rec_sndPlayerEngine.g_copRecord);
    return &(*(char *)&rec_sndPlayerEngine.tbl_riderRecords) + slot * 0x204;
}
char *RrRecord(int slot)
{
    if (!RrExtended() || slot < 15) {
        if (slot >= 9 && slot < 15) (&(*(char *)&rec_sndPlayerEngine.tbl_riderRecordsSlot))[slot * 0x204] = (char)(slot + 1);   /* (as the original) */
        return OrigRecord(slot); }
    if (slot == rr_slots - 1) return OrigRecord(15);
    return g_rec[slot];
}
/* the colour scheme row of a slot (rankings.c: the sprite remap row; the original's row is the slot) */
int RrSlotColour(int slot) { return (RrExtended() && slot >= 15) ? g_colour[slot] : slot; }
/* the bike of a slot at the current level (tbl_thrashBikes: 5 levels per character) */
int RrSlotBike(int kind, int level) { return (&(*(unsigned char *)&rec_laneBikeIds.tbl_thrashBikes))[level + RrSlotChar(kind) * 5]; }
/* the start values of a computer rider: lateral position, road position (extra riders: spread over the original lanes and road, half a segment off) */
void RrSlotStart(int i, int high, int *lat, int *road)
{
    const int *t = high ? &tbl_bikePairsHigh : &tbl_bikePairsLow;
    static const int lanes[5] = { 8192, 16384, 20480, 36864, 40960 };
    if (!RrExtended() || i < 15) { *lat = t[2 * i]; *road = t[2 * i + 1]; return; }
    *lat = lanes[(i - 15) % 5]; *road = (60 + ((i - 15) * 7) % 60) * 256 + 128;
}

static const char *const g_names[] = { "Razor", "Mad Max Mona", "Spike", "Vinnie Volts", "Lucky Lou", "Scar", "Big Tex", "Dusty Rhodes", "Nails", "Rattler",
    "Smokey Joe", "Crash Kowalski", "Vixen", "Moe Torque", "Brass Knuckles", "Turbo Tina", "Gravel Gus", "Hammer", "Lady Luck", "Wrench",
    "Two-Stroke Tony", "Slick", "Bonebreaker", "Cherry Bomb", "Diesel", "Road Kill Ray", "Tiny", "Mongoose", "Wild Bill", "Sparks",
    "Ice Queen", "Grease Monkey", "Howlin' Hank", "Rust Bucket", "Lead Foot", "Zapata" };
/* the name of the rider of record `rec` (0 = not an extra rider: the original's name lookup) */
const char *RrRiderName(const char *rec)
{
    int s;
    if (!RrExtended()) return 0;
    for (s = 15; s < rr_slots - 1; s++) if (rec == g_rec[s]) return g_names[(s - 15) % (int)(sizeof g_names / sizeof g_names[0])];
    return 0;
}

/* race start (RaceSpawnRiders, before any rider is made): this race's count, the extra slots' blocks from their characters' */
void RrSlotsBegin(void)
{
    int s; unsigned r;
    rr_opponents = Wanted();
    rr_slots = rr_opponents > 14 ? rr_opponents + 2 : 16;
    if (!RrExtended()) return;
    memset(g_riders, 0, sizeof g_riders); memset(g_rank, 0, sizeof g_rank);
    r = GetTickCount() * 2654435761u;
    for (s = 15; s < rr_slots; s++) {
        int c = RrSlotChar(s);
        memcpy(g_kind[s], &(*(char *)&rec_riderKindParams.tbl_riderKindParams) + c * 0x40, 0x40);
        if (s < rr_slots - 1) { memcpy(g_rec[s], OrigRecord(c), 0x204); r = r * 1103515245u + 12345u; g_colour[s] = (unsigned char)(1 + (r >> 16) % 14); }
    }
}

/* race end (RidersFree): the extra slots' rider pointers go with the riders */
void RrSlotsEnd(void) { memset(g_riders, 0, sizeof g_riders); memset(g_rank, 0, sizeof g_rank); }
