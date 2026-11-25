/* road\scenery_spawn.c - functions of the original's code range 0x41eca0-0x41f040,
   regrouped from the hand ports (port_small30.c, legacy_j.c).
   Each RR_REPLACE*(FUN_x, rr_FUN_x, n) replaces the original function at link time. */
#include "../rr.h"
#include "../data/records.h"
#include "../legacy_decls.h"
int rr_CeForkSplit(int pos, int *sx, int *ss);   /* road\course_edit.c: a Course Editor fork piece (its B offsets) */

RR_DATA(int,            g_sceneryNodePool);
RR_DATA(unsigned *,     g_sceneryGroupPool);
RR_DATA(int,            tbl_sideKinds);
RR_DATA(char,           tbl_sceneryGroupSpec);
RR_DATA(int,            g_roadRingBack);
RR_DATA(int,            g_ringFront);

#define I32(p, o) (*(int *)((char *)(p) + (o)))

/* ---- road-side object group spawner ---- */
/* o = spawn request { +0: first slot, +4: last slot, +8: type (low byte = 1..3 phase) } for road object `self`; creates the group record (NodeFromDescription) and up to four slot objects (RoadObjectCreateBetween) beside the section found at the player's position, then, for a request with flag +0xb clear, resets the "age" word (+0xdc) of each slot */
RR_REPLACE_FAST(RoadGroupSpawn, rr_RoadGroupSpawn, 8);
void __fastcall rr_RoadGroupSpawn(int *o, int self)
{
    int cVar11 = (char)o[2] + '\x01';       /* phase */
    int isFirst = *(char *)((int)o + 0xb) == '\0';
    int special;
    int base, secA, secB, pos, sec;
    char count, lanes;
    int *g, *r, *slots, noLeft = 0, noRight = 0, noPost = 0;
    int n;

    if ('\x03' < (char)cVar11)
        return;
    special = (*(char *)(I32(self, 0x28) + 0x1c) == '\x02' && *(char *)(self + 0x3c) == '\x04') ? 1 : 0;
    base = (*(unsigned *)(I32(self, 0x28) + 8) & 0xffffff00) - (*(unsigned *)(self + 4) & 0xffffff00);
    n = o[1];
    secA = (int)RoadSectionAt(base + *o * 0x100, self);
    secB = (int)RoadSectionAt(n * 0x100 + -0x100 + base, secA);
    count = ((char)o[1] - (char)*o) + -2;
    lanes = (char)(I32(secA, 0x2c) + (I32(secA, 0x2c) >> 0x1f & 0xffU) >> 8) +
            (char)(I32(secA, 0x30) + (I32(secA, 0x30) >> 0x1f & 0xffU) >> 8);
    {   extern int rr_courseEdited; int q, cross = 0, s;   /* a junction's group (its traffic-light posts at the kerbs) with no cross street near it: none (user, 2026-11-23 "remove the
                                                                     junctions please in the test map" - New track takes a course's cross streets, kind flag 0x20, but the
                                                                     course data still asked for their groups: traffic lights in the dunes).  Edited courses only */
        for (q = *o - 8; q <= n + 8 && !cross; q++) { s = (int)RoadSectionAt(base + q * 0x100, secA); if ((unsigned)s >= 0x10000u && (*(unsigned char *)(s + 0x37) & 0x20)) cross = 1; }
        { static int on = -1; char e[4], b[120]; void (__stdcall *dl)(const char *); if (on < 0) on = GetEnvironmentVariableA("RR_SPAWNLOG", e, sizeof e) > 0;
          if (on && (dl = (void (__stdcall *)(const char *))GetProcAddress(GetModuleHandleA("rrddraw.dll"), "RR_DbgLog")) != 0) { wsprintfA(b, "spawn group piece %d slots %d..%d cross %d edited %d", I32(I32(secA, 0x28), 8) >> 8, *o, n, cross, rr_courseEdited); dl(b); } }
        if (rr_courseEdited && !cross) return; }
    pos = I32(I32(self, 0x28), 8);
    sec = g_ringFront;
    if (g_ringFront < pos || (sec = g_roadRingBack, pos < g_roadRingBack))
        pos = sec;
    pos = (int)RoadSectionAt(pos, self);
    g = (int *)NodeFromDescription((int)g_sceneryGroupPool, (int)&tbl_sceneryGroupSpec, I32(pos, 0xcc), 0, pos);
    if (g == 0)
        return;
    r = g + 0x42;
    *r = *o;
    g[0x43] = o[1];
    g[0x44] = o[2];
    *(char *)(g + 0x44) = (char)cVar11;
    slots = g + 0x3b;
    *r = *r * 0x100 + base;
    g[0x43] = g[0x43] * 0x100 + base;
    *slots = 0;
    g[0x3d] = 0;
    g[0x3c] = 0;
    g[0x3e] = 0;
    {   int sx_, ss_, half = *(unsigned char *)(secA + 0x36) & 3;   /* on a Course Editor fork: no pedestrians on its road's inner side - A's right, B's left: the ground
                                                                    between the two roads, the other road (user, 2026-11-23: "pedestrians just materialised out of nowhere and i was
                                                                    hitting invisible objects" - left out where the game makes them, not hidden in the picture) */
        if (*(char *)(I32(secA, 0x28) + 0x1c) == 2 && rr_CeForkSplit(I32(I32(secA, 0x28), 8), &sx_, &ss_)) { noRight = half == 1; noLeft = half == 2; noPost = 1; }   /* (and no signal posts - slots 0 / 2, standing at the kerbs: a crossing's
                                                                    traffic lights, made 13 pieces ahead, stood on the fork's grass out of nowhere, on one road only; user, 2026-11-23: "This traffic light comes from no where ... fix the traffic lights") */
        { static int on = -1; char e[4], b[200]; void (__stdcall *dl)(const char *); if (on < 0) on = GetEnvironmentVariableA("RR_SPAWNLOG", e, sizeof e) > 0;   /* (test switch RR_SPAWNLOG=1: each group spawn) */
          if (on && (dl = (void (__stdcall *)(const char *))GetProcAddress(GetModuleHandleA("rrddraw.dll"), "RR_DbgLog")) != 0) { wsprintfA(b, "spawn piece %d secA %d half %x nh %d noL %d noR %d phase %d special %d first %d", I32(I32(secA, 0x28), 8) >> 8, I32(secA, 4) >> 8, *(unsigned char *)(secA + 0x36), *(char *)(I32(secA, 0x28) + 0x1c), noLeft, noRight, cVar11, special, isFirst); dl(b); } } }
    if (special && isFirst) {
        g[0x3f] = 0x4b;
        g[0x40] = 0;
        g[0x41] = 0xf0;
    } else {
        g[0x3f] = 600;
        g[0x40] = 0xb4;
        g[0x41] = 0x30c;
    }
    if (!noRight && !noPost && 0 < (int)(I32(secA, 0x2c) + (I32(secA, 0x2c) >> 0x1f & 0xffU)) >> 8) {
        r = (int *)RoadObjectCreateBetween(0, 1, 0x50, secB, count, (int)g);
        *slots = (int)r;
        if (r == 0)
            goto done;
    }
    if (!noLeft && !noPost && 0 < (int)(I32(secB, 0x30) + (I32(secB, 0x30) >> 0x1f & 0xffU)) >> 8) {
        r = (int *)RoadObjectCreateBetween(2, 0, 0xb0, secA, count, (int)g);
        g[0x3d] = (int)r;
        if (r == 0)
            goto done;
    }
    if (!special) {
        if (!noLeft && ((char)cVar11 == '\x01' || (char)cVar11 == '\x02')) {
            r = (int *)RoadObjectCreateBetween(3, 0, 0x50, secB, lanes, (int)g);
            g[0x3e] = (int)r;
            if (r == 0)
                goto done;
        }
        if (!noRight && ((char)cVar11 == '\x01' || (char)cVar11 == '\x03')) {
            r = (int *)RoadObjectCreateBetween(1, 1, 0xb0, secA, lanes, (int)g);
            g[0x3c] = (int)r;
        }
    }
done:
    if (isFirst) {
        n = 4;
        do {
            if (*slots != 0)
                I32(*slots, 0xdc) = 0;
            slots++;
            n--;
        } while (n != 0);
    }
}

/* creates a 0x104-byte-ish object through NodeFromDescription from two interpolated end points and tags it (fields 0xec..0x104) */
RR_REPLACE_FAST(RoadObjectCreateBetween, rr_RoadObjectCreateBetween, 24);
int __fastcall rr_RoadObjectCreateBetween(int c, int k, int a1, int a2, int a3, int a4)
{
    int *B = (int *)a2;
    int *P = (int *)B[k + 2];
    int s1 = B[k + 0x38] + B[k + 0x3a];
    int s2 = P[k + 0x38] + P[k + 0x3a];
    int e1 = ((s1 - (s1 >> 31)) >> 1) << 8;
    int e2 = ((s2 - (s2 >> 31)) >> 1) << 8;
    int v = (e2 - e1) * a1, x;
    char *n;
    (*(int *)&rec_sceneryNodeSpec.g_sceneryKind) = (&tbl_sideKinds)[c];
    x = ((v + (v < 0 ? 0xff : 0)) >> 8) + e1;
    n = (char *)NodeFromDescription(g_sceneryNodePool, (int)&(*(char *)&rec_sceneryNodeSpec.tbl_sceneryNodeSpec), x, a1, a2);
    if (n == 0) return 0;
    *(int *)(n + 0xec) = a4;
    *(int *)(n + 0xf0) = ((~c) & 1) * 2;
    *(int *)(n + 0xf4) = 0x30c;
    *(int *)(n + 0xf8) = c;
    n[0xfc] = (char)a3;
    *(int *)(n + 0x100) = 1;
    *(unsigned short *)(n + 0x104) = (*(unsigned short *)&rec_sndPlayerEngine.g_scenerySerial);
    (*(unsigned short *)&rec_sndPlayerEngine.g_scenerySerial)++;
    return (int)n;
}
