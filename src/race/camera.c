/* race\camera.c - functions of the original's code range 0x413ae0-0x413b90, 0x437c30-0x437df0,
   regrouped from the hand ports (port_small12.c, port_small26.c).
   Each RR_REPLACE*(FUN_x, rr_FUN_x, n) replaces the original function at link time. */
#include "../rr.h"
#include "../data/records.h"
#include "../road/road_ring.h"
#include "../legacy_decls.h"
#include "../road/course_edit.h"

RR_DATA(int,      g_playerRider);
RR_DATA(int,      g_startCountdown);
RR_DATA(char,     tbl_ringSlots);

#define I32(p, o) (*(int *)((char *)(p) + (o)))

/* ---- steering ---- */
/* camera yaw rate: offset of the rider's lane from the road centre at the road's current angle, as a 16.16 value */
RR_REPLACE_FAST(CameraYawRate, rr_CameraYawRate, 12);
void __fastcall rr_CameraYawRate(int obj, int *out, int lane)
{
    unsigned ang;
    int base, yaw, d;
    if (I32(obj, 0xc4) == 8) ang = I32(lane, 0x18) * 2 + (*(unsigned *)(obj + 0x44) & 0xff) * 2;
    else                     ang = (*(unsigned *)(obj + 0x44) & 0xff) + I32(lane, 0x18);
    base = I32(obj, 0x68);
    yaw = I32(lane, 0x1c);
    d = (int)VectorAngle((int)ang, (I32(lane, 0x10) + ((I32(obj, 0x50) + ((I32(obj, 0x50) >> 31) & 0xff)) >> 8)) - rec_stepSaved.g_camLateral);
    d = d * 0x10000;
    if (0x800000 < yaw) yaw -= 0x1000000;
    if (0x800000 < d)   d -= 0x1000000;
    out[1] = (yaw - d) + base;
    I32(obj, 0xd0) = d;
    out[0] = 0;
    out[2] = I32(obj, 0x6c);
}

/* ---- camera ---- */
/* follow camera: aim at the middle of the rider's road section, damp the lateral offset, pull the view distance back while braking / crashed, and publish the camera globals */
RR_REPLACE_STD(CameraFollow, rr_CameraFollow, 0);
void __stdcall rr_CameraFollow(void)
{
    unsigned oldCam = (*(unsigned *)&rec_featureAccum1.g_camDist);
    int *ownerp = (int *)(g_playerRider + 0x2fc);
    int *sec = *(int **)(*ownerp + 0x2c);
    int mid = (sec[0x38] + sec[0x39]) / 2;
    int off, adj, a, b;
    if (rr_CeForkSplit(I32(sec[10], 8), &off, &adj)) {   /* a Course Editor fork (user, 2026-11-23 "it was the camera that jolted" near 518 / 540): its course blocks each open / close
                                                       the split, so the blend below swung the camera on B to A's lanes and back at every block change (~117 units); here the blend is
                                                       to the single road's middle where the fork ends - its join ahead, its turn-off behind - over the same 16 pieces (the join had jumped ~60) */
        int pos = I32(sec[10], 8), d, t = 0;
        for (d = 1; d < 0x10 && !t; d++) { if (!rr_CeForkSplit(pos + d * 0x100, &off, &adj)) t = pos + d * 0x100; else if (!rr_CeForkSplit(pos - d * 0x100, &off, &adj)) t = pos - d * 0x100; }
        if (t) { int row = I32((int)RR_RING(t), 0xc); d--;
            row = (I32(row, 0xe0) + I32(row, 0xe4)) / 2; a = (mid - row) * d; mid = row + ((a + ((a >> 31) & 0xf)) >> 4); }
    } else if (*(char *)(sec[10] + 0x1c) == 2) {
        switch (I32(sec[10], 0x2c)) {
        case 2:
        case 5:
            (*(unsigned *)&rec_featureAccum1.g_camDist) = -(sec[1] >> 8) - 1;
            break;
        default:
            break;
        case 4:
        case 6:
            (*(unsigned *)&rec_featureAccum1.g_camDist) = I32(*sec, 0x14) - (sec[1] >> 8);
        }
        a = (int)(((*(unsigned *)&rec_featureAccum1.g_camDist) ^ (unsigned)((int)(*(unsigned *)&rec_featureAccum1.g_camDist) >> 31)) - (unsigned)((int)(*(unsigned *)&rec_featureAccum1.g_camDist) >> 31));
        if (a < 0x10) {
            int row = I32((int)RR_RING(I32(sec[10], 8) + (*(unsigned *)&rec_featureAccum1.g_camDist) * 0x100), 0xc);
            row = (I32(row, 0xe0) + I32(row, 0xe4)) / 2;
            a = (mid - row) * a;
            mid = row + ((a + ((a >> 31) & 0xf)) >> 4);
        }
    }
    off = ((I32(*ownerp, 0x28) + ((I32(*ownerp, 0x28) >> 31) & 0xff)) >> 8) - mid;
    adj = off - ((off + ((off >> 31) & 3)) >> 2);
    if (off - adj < 0xc9) {
        if (off - adj < -200) adj = off + 200;
    } else {
        adj = off - 200;
    }
    if ((*(unsigned char *)(g_playerRider + 0xc0) & 0x10) != 0 || g_startCountdown != 0) {
        if (rec_roadTexStart.g_camIntroRise < 300) rec_roadTexStart.g_camIntroRise++;
        if (rec_roadTexStart.g_camIntroPullBack < 900) rec_roadTexStart.g_camIntroPullBack += 3;
    }
    rec_featureAccum1.g_camAlt = adj + mid;
    rec_featureAccum1.g_camY = I32(*ownerp, 0x24) + rec_roadTexStart.g_camIntroRise;
    if (rec_featureAccum1.g_camY < 0x97) rec_featureAccum1.g_camY = 0x96;
    (*(unsigned *)&rec_featureAccum1.g_camDist) = (unsigned)((I32(*ownerp, 0x138) - rec_roadTexStart.g_camIntroPullBack) - 0x100);
    rec_featureAccum1.g_camSide = (int)((*(unsigned *)&rec_featureAccum1.g_camDist) - oldCam) * 0x100;
    (void)b;
}
