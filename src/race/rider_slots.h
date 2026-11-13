/* race\rider_slots.h - more (or fewer) opponents: the rider slots past the original 16 (race\rider_slots.c) */
#ifndef RIDER_SLOTS_H
#define RIDER_SLOTS_H
#define RR_SLOTS_MAX 52
extern int rr_slots, rr_opponents;
int RrExtended(void);
int RrCopKind(void);
int RrSlotChar(int kind);
int *RrRiders(void);
int *RrRankRiders(void);
int RrRiderAt(int i);
int RrRankAt(int i);
char *RrKindParams(int kind);
char *RrRecord(int slot);
int RrSlotColour(int slot);
int RrSlotBike(int kind, int level);
void RrSlotStart(int i, int high, int *lat, int *road);
const char *RrRiderName(const char *rec);
void RrSlotsBegin(void);
void RrSlotsEnd(void);
#endif
