/* weather.inc - time of day and weather of True 3D races (included by rrddraw.c after hdsky.inc; nothing here changes the game state, Pseudo 3D is untouched).
 *
 *   Time of day   Cycle (a day every g_vid.dayMin minutes of unpaused race time, carried on from race to race) / Dawn / Day / Sunset / Night
 *   Weather       Clear / Rain (overcast, grey fog, streaks, lightning) / Snow (white fog, flakes, snow on what faces up, banks by the road) / Heavy rain / Heavy snow / Torrential rain (three times the heavy rain, pea-soup fog, 20 % darker)
 * Both are changed in the race's Esc pause (the game's "quit?" prompt, race_session.c rr_PromptKey asks RR_PauseKey first): Up / Down = row,
 * Left / Right = value; written to video.ini (time_of_day, weather, day_minutes).  Test switches: RR_TOD=0..4, RR_WEATHER=0..2, RR_TOD_CLOCK=<0..1> (a fixed
 * time of the cycle: 0 = midnight, 0.25 sunrise, 0.5 noon, 0.75 sunset).
 * One colour model (WxCompute) feeds every layer: the sky (WxSky, from HsDraw: tint, graded sky, stars, sun / moon, clouds and skyline tinted), the 3D
 * world (WxWorld, from W3Draw: render3d.h tint / saturation / fog / snow / point lights - the lamps and police lights are world3d.inc's), the game's
 * remaining polygons (WxBatchMod) and the rain / snow in front of it all (WxParticles, before the HUD).  Day + Clear = the picture as before. */
static const char *WpRowName(void); static void WpRowStep(int d); static int WpTorch(void);   /* weapons.inc (a Big Game: the weapons the player owns; the Miner's Lamp charm) */
static float g_wxShelter;   /* 0..1: the camera under a roof (a tunnel, world3d.inc g_w3InTunnel): the rain / snow fades out, its sound muffled */
static int g_wxPause, g_wxSel, g_wxDirty;   /* the Esc prompt is up / its row / a setting changed (saved when it closes) */
static double g_wxClock = 0.30, g_wxTime;  /* the cycle's time of day (0..1, starts mid-morning); seconds of unpaused True 3D race time (animation) */
static LARGE_INTEGER g_wxLast;
typedef struct WxState {
    float t, e, day, tw, dark;             /* time of day, sun height (-1 midnight .. 1 noon), daylight 0..1, twilight 0..1, how much lights show 0..1 */
    float tint[3], sat;                    /* the world's colour multiplier and saturation */
    float skyTop[3], skyHor[3], skyA;      /* sky gradient laid over the panorama (0..1 colours) and its cover */
    float stars, cloud[3], cloudA;         /* stars 0..1; clouds' colour / opacity */
    float fog[3], fogStart, fogEnd, fogMax, mist;   /* fog colour, start / full in segments, strength; 1 = it lies low (ground mist) */
    float snow, flash;                     /* snow cover; lightning 0..1 */
    float hor[3], glint, rain;             /* the colour low in the sky (haze; fog, what water reflects), sun glints on water, rain (puddles) */
} WxState;
static WxState g_wx;
static int g_wxBoltN;   /* lightning strikes so far (the bolt's shape) */
static float g_wxBolt = 6, g_wxFlashT = 9, g_wxThunderIn, g_wxThunderAmp;   /* seconds to the next lightning, since the last one; to its thunder, how loud */
/* the weather's sound (dsound.dll RR_Ambience / RR_Thunder, synthesised there): kind 0 / 1 rain / 2 snow with a volume, or kind -1 = a thunder of strength v */
static void WxSound(int kind, float v)
{   static void (__stdcall *amb)(int, float); static void (__stdcall *thu)(float); static int init, lastK = -9; static float lastV = -1;
    if (!init) { HMODULE m = GetModuleHandleA("rrdsound.dll"); init = 1; if (m) { amb = (void (__stdcall *)(int, float))GetProcAddress(m, "RR_Ambience"); thu = (void (__stdcall *)(float))GetProcAddress(m, "RR_Thunder"); } }
    if (kind < 0) { if (thu) thu(v); return; }
    if (amb && (kind != lastK || fabsf(v - lastV) > 0.01f)) { amb(kind, v); lastK = kind; lastV = v; } }

static int WxEnv(const char *n, int v, int lo, int hi) { const char *e = getenv(n); if (e && *e) { v = atoi(e); if (v < lo) v = lo; if (v > hi) v = hi; } return v; }
static int WxTod(void) { return WxEnv("RR_TOD", g_vid.tod, 0, 4); }
static int WxWeatherRaw(void) { return WxEnv("RR_WEATHER", g_vid.weather, 0, 5); }   /* the setting: 0 clear, 1 rain, 2 snow, 3 heavy rain, 4 heavy snow, 5 torrential rain */
static int WxWeather(void) { int r = WxWeatherRaw(); return r == 3 || r == 5 ? 1 : r == 4 ? 2 : r; }   /* its kind: 0 clear, 1 rain, 2 snow */
static int WxHeavy(void) { return WxWeatherRaw() >= 3; }
static int WxTorrent(void) { return WxWeatherRaw() == 5; }   /* torrential rain (also heavy) */
/* the Fog setting: 0 auto (with the weather), 1 off, 2 light, 3 heavy, 4 haze, 5 medium, 6 pea soup, 7 ground mist (the stored numbers; the pause
   steps through them in WX_FOGORD order, thinnest to thickest) */
#define WX_NFOG 8
static const int g_wxFogOrd[WX_NFOG] = { 0, 1, 4, 2, 5, 3, 6, 7 };
static const char *const g_wxFogName[WX_NFOG] = { "Auto", "Off", "Light", "Heavy", "Haze", "Medium", "Pea soup", "Ground mist" };
static const struct { float start, end, max, sky; } g_wxFogSet[WX_NFOG] = {   /* start / full (segments), strength, how much the sky hazes over */
    { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 4, 60, 0.65f, 0.55f }, { 1, 16, 0.97f, 0.95f }, { 14, 140, 0.45f, 0.3f }, { 2, 32, 0.84f, 0.75f }, { 0.3f, 6, 1.0f, 1.0f }, { 1, 22, 0.95f, 0.25f } };
static int WxFog(void) { return WxEnv("RR_FOG", g_vid.fog, 0, WX_NFOG - 1); }
static double WxT(void)   /* time of day now */
{   static const double fixed[5] = { 0, 0.262, 0.5, 0.738, 0.0 }; const char *e = getenv("RR_TOD_CLOCK"); int tod = WxTod();
    if (e && *e) return atof(e) - floor(atof(e));
    return tod ? fixed[tod] : g_wxClock; }
static float WxS(float a, float b, float x) { float t = (x - a) / (b - a); if (t < 0) t = 0; if (t > 1) t = 1; return t * t * (3 - 2 * t); }
static float WxL(float a, float b, float t) { return a + (b - a) * t; }
static void WxL3(float *o, const float *a, const float *b, float t) { int i; for (i = 0; i < 3; i++) o[i] = WxL(a[i], b[i], t); }
static unsigned WxRnd(unsigned *s) { *s = *s * 1664525u + 1013904223u; return *s >> 8; }
static float WxRf(unsigned *s) { return (WxRnd(s) & 0xffff) / 65535.0f; }

static void WxCompute(float dt)
{   static const float nTop[3] = { 0.02f, 0.03f, 0.10f }, nHor[3] = { 0.08f, 0.10f, 0.20f }, tTop[3] = { 0.16f, 0.20f, 0.42f },
        dawnHor[3] = { 1.00f, 0.66f, 0.47f }, duskHor[3] = { 1.00f, 0.47f, 0.24f }, nTint[3] = { 0.22f, 0.26f, 0.42f }, twTint[3] = { 1.0f, 0.68f, 0.52f },
        one[3] = { 1, 1, 1 }, nCloud[3] = { 0.16f, 0.18f, 0.28f }, twCloud[3] = { 1.0f, 0.70f, 0.60f };
    WxState *w = &g_wx; float t = (float)WxT(), tw, k, L; int wx = WxWeather(), i;
    w->t = t; w->e = (float)-cos(2 * 3.14159265358979 * t);
    w->day = WxS(-0.2f, 0.3f, w->e); tw = (float)exp(-(w->e / 0.2f) * (w->e / 0.2f)); w->tw = tw;
    w->dark = 1 - WxS(-0.12f, 0.18f, w->e);
    WxL3(w->tint, nTint, one, w->day); for (i = 0; i < 3; i++) w->tint[i] *= WxL(1, twTint[i], tw * 0.75f);
    w->sat = WxL(0.55f, 1, w->day);
    k = WxS(-0.45f, -0.05f, w->e);   /* night -> twilight colours */
    WxL3(w->skyTop, nTop, tTop, k); WxL3(w->skyHor, nHor, t < 0.5f ? dawnHor : duskHor, k);
    w->skyA = 1 - w->day; w->stars = 1 - WxS(-0.35f, -0.05f, w->e);
    WxL3(w->cloud, nCloud, one, w->day); for (i = 0; i < 3; i++) w->cloud[i] *= WxL(1, twCloud[i], tw * 0.8f); w->cloudA = 1;
    memcpy(w->fog, w->skyHor, sizeof w->fog); w->fogStart = 25; w->fogEnd = 150; w->fogMax = 0.55f * (1 - w->day);
    w->snow = 0;
    if (wx == 0) { static const float warm[3] = { 1.0f, 0.95f, 0.84f }; for (i = 0; i < 3; i++) w->tint[i] *= WxL(1, warm[i], w->day); w->sat *= WxL(1, 1.08f, w->day); }   /* sunny: warmer, a little richer light */
    L = WxL(0.10f, 1, w->day);   /* overcast light */
    if (wx == 1) {   /* rain: low grey cloud, wet dull colours, grey fog */
        static const float rTop[3] = { 0.36f, 0.38f, 0.43f }, rHor[3] = { 0.55f, 0.57f, 0.60f }, rTint[3] = { 0.70f, 0.72f, 0.80f };
        for (i = 0; i < 3; i++) { w->skyTop[i] = WxL(w->skyTop[i], rTop[i] * L, 0.85f); w->skyHor[i] = WxL(w->skyHor[i], rHor[i] * L, 0.85f); w->tint[i] *= rTint[i]; w->cloud[i] = 0.55f * L + 0.10f; }
        w->skyA = WxL(w->skyA, 1, 0.9f); w->sat *= 0.7f; w->stars = 0; w->cloudA = 1;
        memcpy(w->fog, w->skyHor, sizeof w->fog); w->fogStart = 3; w->fogEnd = 70; w->fogMax = 0.82f;
        if (w->dark < 0.35f) w->dark = 0.35f;
        if (WxHeavy()) { for (i = 0; i < 3; i++) { w->skyTop[i] *= 0.72f; w->skyHor[i] *= 0.8f; w->tint[i] *= 0.85f; w->cloud[i] *= 0.75f; } memcpy(w->fog, w->skyHor, sizeof w->fog);   /* heavy rain: darker, closer fog */
                         w->fogStart = 1.5f; w->fogEnd = 38; w->fogMax = 0.9f; if (w->dark < 0.5f) w->dark = 0.5f; }
        g_wxBolt -= dt; g_wxFlashT += dt;
        if (getenv("RR_WX_BOLT") && g_wxFlashT > 1.0f) g_wxBolt = 0;   /* test switch RR_WX_BOLT=1: a strike every second */
        if (g_wxBolt <= 0) { static unsigned s = 77; g_wxFlashT = 0; g_wxBoltN++; g_wxBolt = WxHeavy() ? 2.5f + 7 * WxRf(&s) : 5 + 14 * WxRf(&s); g_wxThunderIn = 0.4f + 2.2f * WxRf(&s); g_wxThunderAmp = 0.5f + 0.5f * WxRf(&s); }
        if (g_wxThunderIn > 0 && (g_wxThunderIn -= dt) <= 0) WxSound(-1, g_wxThunderAmp);   /* the thunder follows the flash (sound is slower than light) */
        {   float f = g_wxFlashT; w->flash = f < 0.07f ? 1 : f < 0.15f ? 0.25f : f < 0.22f ? 0.8f : (f < 0.6f ? 0.55f * (1 - (f - 0.22f) / 0.38f) : 0); } }
    else w->flash = 0;
    if (wx == 2) {   /* snow: pale overcast, white fog, cold colours, snow cover */
        static const float sTop[3] = { 0.66f, 0.69f, 0.74f }, sHor[3] = { 0.82f, 0.84f, 0.88f }, sTint[3] = { 0.90f, 0.93f, 1.0f };
        for (i = 0; i < 3; i++) { w->skyTop[i] = WxL(w->skyTop[i], sTop[i] * L, 0.85f); w->skyHor[i] = WxL(w->skyHor[i], sHor[i] * L, 0.85f); w->tint[i] *= sTint[i]; w->cloud[i] = 0.85f * L + 0.05f; }
        w->skyA = WxL(w->skyA, 1, 0.88f); w->sat *= 0.75f; w->stars = 0; w->cloudA = 1;
        memcpy(w->fog, w->skyHor, sizeof w->fog); w->fogStart = 2; w->fogEnd = 45; w->fogMax = 0.86f; w->snow = 0.88f;
        if (w->dark < 0.3f) w->dark = 0.3f;
        if (WxHeavy()) { for (i = 0; i < 3; i++) { w->skyTop[i] *= 0.88f; w->tint[i] *= 0.9f; } w->fogStart = 0.6f; w->fogEnd = 12; w->fogMax = 0.97f; w->snow = 1.0f; for (i = 0; i < 3; i++) w->fog[i] = WxL(w->fog[i], 0.92f * L, 0.5f); if (w->dark < 0.4f) w->dark = 0.4f; } }   /* heavy snow: a white-out */
    {   static const float haze[3] = { 0.70f, 0.78f, 0.90f };   /* the colour low in the sky: day haze / the sky's own (dusk, night, overcast) */
        if (wx) memcpy(w->hor, w->skyHor, sizeof w->hor); else WxL3(w->hor, w->skyHor, haze, w->day);
        if (!wx) memcpy(w->fog, w->hor, sizeof w->fog);
        w->glint = wx ? 0 : w->day; w->rain = wx == 1 ? 1.0f : 0.0f; }
    w->mist = 0;
    if (WxFog() == 1) w->fogMax = 0;   /* the pause's Fog setting */
    else if (WxFog() >= 2) {   /* a fog setting: its distances, and the sky hazes over towards the fog's colour too */
        float k2 = g_wxFogSet[WxFog()].sky;
        w->fogStart = g_wxFogSet[WxFog()].start; w->fogEnd = g_wxFogSet[WxFog()].end; if (w->fogMax < g_wxFogSet[WxFog()].max) w->fogMax = g_wxFogSet[WxFog()].max;
        if (WxFog() == 7) {   /* ground mist: pale, lying low (render3d.h: thick on the road, thin above the riders' heads; the valleys fill with it) */
            static const float pale[3] = { 0.80f, 0.83f, 0.88f }; float L2 = WxL(0.12f, 1, w->day); w->mist = 1;
            for (i = 0; i < 3; i++) w->fog[i] = WxL(w->fog[i], pale[i] * L2, 0.6f); }
        for (i = 0; i < 3; i++) { w->skyTop[i] = WxL(w->skyTop[i], w->fog[i], k2); w->skyHor[i] = WxL(w->skyHor[i], w->fog[i], k2); }
        if (w->skyA < k2) w->skyA = k2; w->stars *= 1 - k2; w->cloudA = 1 - k2 * 0.8f; }
    if (WxTorrent()) {   /* torrential rain: pea-soup fog whatever the Fog setting, and everything 20 % darker */
        float k2 = g_wxFogSet[6].sky; w->fogStart = g_wxFogSet[6].start; w->fogEnd = g_wxFogSet[6].end; w->fogMax = g_wxFogSet[6].max; w->mist = 0;
        for (i = 0; i < 3; i++) { w->skyTop[i] = WxL(w->skyTop[i], w->fog[i], k2); w->skyHor[i] = WxL(w->skyHor[i], w->fog[i], k2); }
        if (w->skyA < k2) w->skyA = k2; w->stars = 0; w->cloudA = 1 - k2 * 0.8f;
        for (i = 0; i < 3; i++) { w->tint[i] *= 0.8f; w->skyTop[i] *= 0.8f; w->skyHor[i] *= 0.8f; w->fog[i] *= 0.8f; w->cloud[i] *= 0.8f; w->hor[i] *= 0.8f; }
        if (w->dark < 0.6f) w->dark = 0.6f; }
    if (WpTorch()) {   /* the Miner's Lamp charm (Big Game, race\arsenal.c): the fog pushed back, the night a little less black */
        float nk = 1 - w->day;
        w->fogStart *= 1.6f; w->fogEnd *= 1.6f; w->fogMax *= 0.85f;
        for (i = 0; i < 3; i++) w->tint[i] *= 1 + 0.35f * nk; }
    if (w->flash > 0) for (i = 0; i < 3; i++) { float b = w->flash * (WxTorrent() ? 0.35f : 1.0f);   /* (in the torrent's fog a flash only lights it up a little) */
        w->tint[i] += b * 0.9f; w->skyTop[i] = WxL(w->skyTop[i], 0.80f, b * 0.8f); w->skyHor[i] = WxL(w->skyHor[i], 0.90f, b * 0.8f); w->fog[i] = WxL(w->fog[i], 0.85f, b * 0.6f); }
}
/* once per composed frame of a GPU race (GpuCompose) */
static void WxUpdate(void)
{   LARGE_INTEGER n, fq; double dt;
    QueryPerformanceCounter(&n); QueryPerformanceFrequency(&fq);
    dt = g_wxLast.QuadPart ? (double)(n.QuadPart - g_wxLast.QuadPart) / fq.QuadPart : 0; g_wxLast = n;
    if (dt < 0 || dt > 0.25) dt = 0;
    if (!g_w3On) { WxSound(0, 0); return; }
    if (getenv("RR_TEST_PAUSE") && !g_wxPause && g_wxTime > atof(getenv("RR_TEST_PAUSE"))) { g_wxPause = 1; g_wxSel = 1; }   /* test switch RR_TEST_PAUSE=<s>: the pause panel shown (a picture of it) */
    WxSound(WxWeather(), (g_wxPause ? 0.25f : WxHeavy() ? 1.0f : 0.8f) * (1 - 0.7f * g_wxShelter));   /* (quieter in the pause, louder when heavy) */
    g_wxTime += dt;   /* (on in the pause too: the rain / snow, clouds, water and a setting changed there keep moving behind the panel - only the day stands still) */
    if (!WxTod() && !g_wxPause) g_wxClock = fmod(g_wxClock + dt / (g_vid.dayMin * 60.0), 1.0);
    WxCompute((float)dt); }
static float WxDark(void) { return g_w3On ? g_wx.dark : 0; }   /* how much the lights show (world3d.inc W3Lights) */
/* the riders' shadows (world3d.inc W3Shadows): the sun's bearing from the view's forward (radians, + = right) - where WxSky draws it on the panorama,
   which turns once round with the view every g_hsPeriod columns - and the shadow's length per unit of height (1 / tan elevation); returns how dark it
   is.  No sun (overcast, night): a faint short shadow straight away from the camera, under the bike */
/* True 3D: the sun stands still in the world - a fixed bearing (WX_SUNAZ, world x / z) that sweeps east to west through the day - so the shadows lie
   still on the road while the view turns; WxView (world3d.inc W3Draw) hands over the view's heading each frame and the sky draws the sun there too */
#define WX_SUNAZ 0.6f
static double g_wxHd; static int g_wxHdOn;
static void WxView(double hd) { g_wxHd = hd; g_wxHdOn = 1; }
static float WxWrapPi(float b) { b = (float)fmod(b + 3.14159265f, 6.2831853f); if (b < 0) b += 6.2831853f; return b - 3.14159265f; }
static float WxSunBear(float tt, int P)   /* the sun's bearing from the view's forward (radians, + = right) */
{   if (g_w3On && g_wxHdOn) return WxWrapPi(WX_SUNAZ + (tt - 0.5f) * 2.4f - (float)g_wxHd);
    return WxWrapPi((g_hsLeft + (0.22f + 0.56f * tt) * P + g_hsX - (float)g_w3cam.centreX) / P * 6.2831853f); }   /* (where the panorama has it) */
#define WX_SHMAXLEN 2.5f   /* the shadows' longest, per unit of height (low sun: was 5 - too far stretched at sunset, user 2026-11-15) */
static float WxSunShadowRaw(float *bear, float *len);
static float WxSunShadow(float *bear, float *len)
{   float op = WxSunShadowRaw(bear, len); if (*len > WX_SHMAXLEN) *len = WX_SHMAXLEN; return op; }
static float WxSunShadowRaw(float *bear, float *len)   /* (the sun's real elevation as a shadow length, up to 5: WxSunDir lights the walls with it) */
{   const WxState *w = &g_wx; int P = g_hsPeriod; float up = WxS(-0.04f, 0.10f, w->e);
    *bear = 3.14159265f; *len = 0.35f;
    if (!g_w3On) return 0;
    if (WxWeather() == 0 && P > 0 && up > 0) {
        float tt = (w->t - 0.25f) / 0.5f, b, el; if (tt < 0) tt = 0; if (tt > 1) tt = 1;
        b = WxSunBear(tt, P);
        el = (float)asin(w->e > 0.08f ? (w->e > 1 ? 1 : w->e) * 0.92f : 0.074f); if (el < 0.12f) el = 0.12f;
        *bear = b; *len = (float)(cos(el) / sin(el)); if (*len > 5) *len = 5;
        return 0.5f * (0.55f * up * (1 - 0.6f * w->fogMax) + 0.12f * (1 - up)); }   /* (half: user) */
    return 0.5f * 0.30f * (0.4f + 0.6f * w->day) + 0.12f; }   /* (no sun: a soft contact shadow under the bike, also at night) */
/* the sun for the buildings' walls (WxWorld -> render3d.h sun): its direction in the world (from the view's forward / right, as WxSunShadow) and how
   strongly it shapes them (0: no sun - overcast, night) */
static float WxSunDir(const float *fwd, const float *right, float *dir)
{   const WxState *w = &g_wx; float bear, len, el, up = WxS(-0.04f, 0.10f, w->e), ch;
    dir[0] = 0; dir[1] = 1; dir[2] = 0;
    if (!g_w3On || WxWeather() != 0 || up <= 0 || g_hsPeriod <= 0) return 0;
    WxSunShadowRaw(&bear, &len); el = (float)atan(1.0 / (len > 0.01f ? len : 0.01f)); ch = cosf(el);
    dir[0] = (fwd[0] * cosf(bear) + right[0] * sinf(bear)) * ch; dir[2] = (fwd[2] * cosf(bear) + right[2] * sinf(bear)) * ch; dir[1] = sinf(el);
    return up * (1 - 0.7f * w->fogMax); }
static double WxSeconds(void) { return g_wxTime; }
/* anything to change in the picture (Day + Clear = nothing) */
static int WxOn(void) { const WxState *w = &g_wx; return g_w3On && (w->skyA > 0.002f || w->fogMax > 0.002f || w->sat < 0.999f || w->tint[0] < 0.999f || w->tint[1] < 0.999f || w->tint[2] < 0.999f || w->flash > 0 || w->snow > 0); }

/* ---- the 3D world (W3Draw / W3Sprites) ---- */
static void WxWorld(R3Frame *f)
{   const WxState *w = &g_wx;
    if (!g_w3On) return;
    f->useTint = 1; f->tint[0] = w->tint[0]; f->tint[1] = w->tint[1]; f->tint[2] = w->tint[2]; f->tint[3] = w->sat;
    f->fogColour[0] = w->fog[0]; f->fogColour[1] = w->fog[1]; f->fogColour[2] = w->fog[2];
    f->fogColour[3] = 0;   /* (render3d.h fogc.w: ground mist's top, over the eye; 0 = the fog everywhere) */
    if (w->mist > 0) { f->fogColour[3] = (float)(520 - g_w3cam.camY); if (fabsf(f->fogColour[3]) < 1) f->fogColour[3] = 1; }
    f->fogStart = (float)(w->fogStart * W3_L); f->fogScale = (float)(1.0 / ((w->fogEnd - w->fogStart) * W3_L)); f->fogMax = w->fogMax;
    f->snow = w->snow; f->night = w->dark;
    f->fadeStart = (float)(W3Far() * 0.62); f->fadeEnd = (float)(W3Far() * 0.97);   /* the far world fades into the sky: no pop-in at the draw distance */
    f->opt[0] = 1.0f;   /* the improved street textures: road, sidewalk, curb (render3d.h) - always on (user, 2026-11-14: no option) */
    f->opt[1] = g_vid.interior ? 1.0f : 0.0f;   /* True 3D detail > Window interiors: the rooms behind the City set's windows (render3d.h) */
    f->opt[3] = g_grMip;   /* the grass tufts' far alpha boost (render3d.h; the F10 panel) */
    f->opt[2] = (float)W3FxDist();   /* True 3D detail > Effects distance: how far the windows' rooms and depth, the walls' relief, rain on the road reach (as the grass) */
    f->puddles = w->rain * (WxTorrent() ? 2.0f : WxHeavy() ? 1.6f : 1.0f); f->time = (float)fmod(g_wxTime, 3600.0); f->water[0] = w->hor[0]; f->water[1] = w->hor[1]; f->water[2] = w->hor[2]; f->water[3] = w->glint;
    if (getenv("RR_W3WATERDBG")) { f->water[0] = 1; f->water[1] = f->water[2] = 0; } }   /* test switch: the water red */
/* the game's remaining 2D polygons of the race picture */
static void WxBatchMod(SDL_Texture *t, int on)
{   if (!t || !WxOn()) return;
    if (on) SDL_SetTextureColorModFloat(t, g_wx.tint[0] > 1 ? 1 : g_wx.tint[0], g_wx.tint[1] > 1 ? 1 : g_wx.tint[1], g_wx.tint[2] > 1 ? 1 : g_wx.tint[2]);
    else SDL_SetTextureColorModFloat(t, 1, 1, 1); }

/* ---- the sky (HsDraw, inside the panorama's clip cr) ---- */
static SDL_Texture *g_wxGlow;   /* soft round spot (sun / moon halo, snowflakes) */
static void WxGlowMake(void)
{   int x, y, S = 64; DWORD px[64 * 64];
    for (y = 0; y < S; y++) for (x = 0; x < S; x++) { float dx = (x + 0.5f) / S * 2 - 1, dy = (y + 0.5f) / S * 2 - 1, d = dx * dx + dy * dy, a = d < 1 ? (float)(0.55 * exp(-d * 9) + 0.45 * exp(-d * 2.5)) * (1 - d) : 0;
        px[y * S + x] = ((DWORD)(a * 255) << 24) | 0xFFFFFFu; }
    g_wxGlow = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, S, S);
    if (g_wxGlow) { SDL_UpdateTexture(g_wxGlow, NULL, px, S * 4); SDL_SetTextureBlendMode(g_wxGlow, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(g_wxGlow, SDL_SCALEMODE_LINEAR); } }
static int WxSkyOn(void) { return WxOn() || (g_w3On && WxWeather() == 0 && g_wx.e > -0.12f); }   /* (a clear day: the sun) */
static int WxCloudMul(void) { return (g_w3On && WxWeather()) ? 3 : 1; }   /* rain / snow: three times the clouds, the extra ones larger (hdsky.inc ClDraw) */
static void WxSpot(float x, float y, float r, float cr, float cg, float cb, float a, SDL_BlendMode m)
{   SDL_FRect d; if (!g_wxGlow) return; d.x = x - r; d.y = y - r; d.w = d.h = 2 * r;
    SDL_SetTextureBlendMode(g_wxGlow, m); SDL_SetTextureColorModFloat(g_wxGlow, cr, cg, cb); SDL_SetTextureAlphaModFloat(g_wxGlow, a); SDL_RenderTexture(g_ren, g_wxGlow, NULL, &d); }
/* panorama column u (0..P) -> every frame column it shows at inside the viewport (like ClDraw) */
#define WX_NSTAR 420
/* the sun / the moon: ONE copy (user, 2026-11-15: "when the sky wraps i see the moon / sun twice" - the panorama repeats every P, narrower than a wide
   view).  True 3D: where its bearing from the view's forward puts it (b, radians, + = right; nothing when it is behind), else the panorama column u's copy
   nearest the view's centre.  X = its frame column, or < -1e8 for none */
static float WxOnce(float u, int P, int threeD, float b)
{   float c = (g_gpuClip[0] + g_gpuClip[2]) * 0.5f, best = -1e9f; int k;
    if (threeD) { float F = (float)(W3_F * (g_w3cam.pxPerUnit > 0 ? g_w3cam.pxPerUnit : 1)); return fabsf(b) < 1.45f ? (float)g_w3cam.centreX + tanf(b) * F : -1e9f; }
    for (k = -1; k <= 2; k++) { float X = g_hsLeft + u + g_hsX + (float)k * P; if (fabsf(X - c) < fabsf(best - c)) best = X; }
    return best; }
#define WX_WRAP(u, ...) { int kk_; for (kk_ = -1; kk_ <= 2; kk_++) { float X = g_hsLeft + (u) + g_hsX + (float)kk_ * P; if (X < g_gpuClip[0] - 64 || X > g_gpuClip[2] + 64) continue; __VA_ARGS__ } }
static void WxSky(int P, int R, int y0, int y1, int fore, const SDL_Rect *cr)
{   const WxState *w = &g_wx; SDL_Rect vr; SDL_FRect fr; SDL_Vertex v[6]; static const int ix[12] = { 0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4 };
    float ym = y0 + (y1 - y0) * 0.6f, aBot = w->skyA * (fore ? 1.0f : 0.3f), aMid = w->skyA * (fore ? 1.0f : 0.85f), hr = g_hsTop + R * 0.80f; int i;
    if (!g_wxGlow) WxGlowMake();
    vr.x = g_gpuClip[0]; vr.y = g_gpuClip[1]; vr.w = g_gpuClip[2] - g_gpuClip[0]; vr.h = g_gpuClip[3] - g_gpuClip[1] + 1;   /* the whole background darkened / coloured */
    SDL_SetRenderClipRect(g_ren, &vr); SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_MOD);
    SDL_SetRenderDrawColorFloat(g_ren, w->tint[0] > 1 ? 1 : w->tint[0], w->tint[1] > 1 ? 1 : w->tint[1], w->tint[2] > 1 ? 1 : w->tint[2], 1);
    fr.x = (float)vr.x; fr.y = (float)vr.y; fr.w = (float)vr.w; fr.h = (float)vr.h; SDL_RenderFillRect(g_ren, &fr);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND); SDL_SetRenderClipRect(g_ren, &vr);   /* (geometry without a texture uses the draw blend mode) */
    if (w->skyA > 0.002f) {   /* the graded sky: top colour -> horizon colour (without the HD skyline picture it thins out low down, where the hills are) */
        float xs[2] = { (float)g_gpuClip[0], (float)g_gpuClip[2] }, ys[3] = { (float)g_gpuClip[1], ym, (float)y1 }, as[3] = { w->skyA, aMid, aBot };   /* (from the view's top: above the panorama too) */
        for (i = 0; i < 6; i++) { int r = i / 2; float t = (float)r / 2; v[i].position.x = xs[i & 1]; v[i].position.y = ys[r];
            v[i].color.r = WxL(w->skyTop[0], w->skyHor[0], t * 1.6f > 1 ? 1 : t * 1.6f); v[i].color.g = WxL(w->skyTop[1], w->skyHor[1], t * 1.6f > 1 ? 1 : t * 1.6f); v[i].color.b = WxL(w->skyTop[2], w->skyHor[2], t * 1.6f > 1 ? 1 : t * 1.6f);
            v[i].color.a = as[r]; v[i].tex_coord.x = v[i].tex_coord.y = 0; }
        SDL_RenderGeometry(g_ren, NULL, v, 6, ix, 12); }
    if (w->stars > 0.01f) {   /* fixed stars on the panorama (they turn with it), twinkling */
        static SDL_Vertex sv[WX_NSTAR * 4]; static int si[WX_NSTAR * 6]; int n = 0; unsigned s = 4242; float hmax = (fore ? 0.75f : 0.45f) * R;
        for (i = 0; i < WX_NSTAR; i++) { float u = WxRf(&s) * P, vv = WxRf(&s) * hmax, b = 0.45f + 0.55f * WxRf(&s), tw = 0.7f + 0.3f * (float)sin(g_wxTime * (1.5 + 3 * WxRf(&s)) + i), sz = b > 0.93f ? 1.6f : (b > 0.7f ? 1.1f : 0.8f), a = w->stars * b * tw;
            WX_WRAP(u, { if (n < WX_NSTAR) { int q; for (q = 0; q < 4; q++) { SDL_Vertex *o = &sv[n * 4 + q]; o->position.x = X + ((q & 1) ? sz : 0); o->position.y = g_hsTop + vv + ((q & 2) ? sz : 0);
                o->color.r = 0.9f; o->color.g = 0.93f; o->color.b = 1; o->color.a = a; o->tex_coord.x = o->tex_coord.y = 0; }
                si[n * 6] = n * 4; si[n * 6 + 1] = n * 4 + 1; si[n * 6 + 2] = n * 4 + 2; si[n * 6 + 3] = n * 4 + 1; si[n * 6 + 4] = n * 4 + 3; si[n * 6 + 5] = n * 4 + 2; n++; } }) }
        if (n) SDL_RenderGeometry(g_ren, NULL, sv, n * 4, si, n * 6); }
    if (WxWeather() == 0) {
        if (w->e > -0.12f) {   /* the sun: rises in the east, crosses the sky, sets in the west - low and orange at dawn / dusk, high and pale at noon (behind the skyline: drawn before it) */
            float tt = (w->t - 0.25f) / 0.5f, u, y, low = 1 - WxS(0.0f, 0.4f, w->e), vis = WxS(-0.12f, -0.02f, w->e), el = (w->e + 0.04f) * 1.6f, gr, gl;
            if (tt < 0) tt = 0; if (tt > 1) tt = 1; if (el > 1) el = 1;
            u = (0.22f + 0.56f * tt) * P; y = hr - el * R * 0.74f;
            if (g_w3On && g_wxHdOn) { u = (float)g_w3cam.centreX + WxSunBear(tt, P) / 6.2831853f * P - g_hsLeft - g_hsX; u = (float)fmod(u, (float)P); if (u < 0) u += P; }   /* (True 3D: where the shadows say it is) */ gr = R * WxL(0.70f, 0.55f, low); gl = WxL(0.30f, 0.55f, low);
            { float X = WxOnce(u, P, g_w3On && g_wxHdOn, WxSunBear(tt, P)); if (X > -1e8f) { WxSpot(X, y, gr, 1.0f, WxL(0.95f, 0.55f, low), WxL(0.78f, 0.30f, low), gl * vis, SDL_BLENDMODE_ADD);
                         WxSpot(X, y, R * WxL(0.06f, 0.075f, low), 1.0f, WxL(1.0f, 0.75f, low), WxL(0.92f, 0.5f, low), 1.0f * vis, SDL_BLENDMODE_BLEND);
                         WxSpot(X, y, R * WxL(0.06f, 0.075f, low), 1.0f, 1.0f, 0.9f, 0.9f * vis, SDL_BLENDMODE_ADD); } } }
        if (w->stars > 0.2f) {   /* the moon */
            float u = 0.55f * P, y = g_hsTop + R * 0.22f, a = WxS(0.2f, 0.6f, w->stars);
            float X = WxOnce(u, P, g_w3On && g_wxHdOn, WxWrapPi(WX_SUNAZ + 3.14159265f - (float)g_wxHd));   /* (True 3D: across the sky from the noon sun) */
            if (X > -1e8f) { WxSpot(X, y, R * 0.42f, 0.55f, 0.62f, 0.85f, 0.4f * a, SDL_BLENDMODE_ADD); WxSpot(X, y, R * 0.075f, 0.95f, 0.96f, 1.0f, a, SDL_BLENDMODE_BLEND); WxSpot(X, y, R * 0.065f, 1, 1, 1, 0.85f * a, SDL_BLENDMODE_ADD); } } }
    if (g_wxGlow) { SDL_SetTextureBlendMode(g_wxGlow, SDL_BLENDMODE_BLEND); SDL_SetTextureColorModFloat(g_wxGlow, 1, 1, 1); SDL_SetTextureAlphaModFloat(g_wxGlow, 1); }
    SDL_SetRenderClipRect(g_ren, cr); SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE); }
/* the moving clouds / the HD skyline drawn again over them: in the light of the hour (on = 0: back to normal) */
static void WxCloudMod(int on)
{   int i; const WxState *w = &g_wx; int act = on && WxOn();
    for (i = 0; i < CL_NTEX; i++) if (g_clTex[i]) { SDL_SetTextureColorModFloat(g_clTex[i], act ? (w->cloud[0] > 1 ? 1 : w->cloud[0]) : 1, act ? (w->cloud[1] > 1 ? 1 : w->cloud[1]) : 1, act ? (w->cloud[2] > 1 ? 1 : w->cloud[2]) : 1); SDL_SetTextureAlphaModFloat(g_clTex[i], act ? w->cloudA : 1); } }
static void WxForeMod(SDL_Texture *t, int on)
{   const WxState *w = &g_wx; int act = on && WxOn();
    float k = !act ? 0 : WxTorrent() ? g_wxFogSet[6].sky : WxFog() >= 2 ? g_wxFogSet[WxFog()].sky : 0;   /* (a thick fog hides the skyline too) */
    if (t) { SDL_SetTextureColorModFloat(t, act ? (w->tint[0] > 1 ? 1 : w->tint[0]) : 1, act ? (w->tint[1] > 1 ? 1 : w->tint[1]) : 1, act ? (w->tint[2] > 1 ? 1 : w->tint[2]) : 1); SDL_SetTextureAlphaModFloat(t, 1 - 0.95f * k * k); } }
/* over the tinted skyline (hdsky.inc HsDraw, True 3D): haze = 0 its grey copy, as much as the world's colours are washed out (1 - saturation, in the
   light's colour), haze = 1 its white silhouette in the fog / haze colour, as far as the distant world fades into it (a little on a clear day, most of
   the way in rain, snow, fog).  Returns 0 when there is nothing to draw */
static int WxForeLayer(SDL_Texture *t, int haze)
{   const WxState *w = &g_wx; float k, vis, a; int i; float c[3];
    if (!t || !WxOn()) return 0;
    k = WxTorrent() ? g_wxFogSet[6].sky : WxFog() >= 2 ? g_wxFogSet[WxFog()].sky : 0; vis = 1 - 0.95f * k * k;   /* (as WxForeMod) */
    if (!haze) { a = 1 - w->sat; for (i = 0; i < 3; i++) c[i] = w->tint[i] > 1 ? 1 : w->tint[i]; }
    else { a = 0.18f + 0.75f * w->fogMax; if (a > 0.92f) a = 0.92f; for (i = 0; i < 3; i++) c[i] = w->fog[i] > 1 ? 1 : w->fog[i] < 0 ? 0 : w->fog[i]; }
    a *= vis; if (a < 0.01f) return 0;
    SDL_SetTextureColorModFloat(t, c[0], c[1], c[2]); SDL_SetTextureAlphaModFloat(t, a); return 1; }
/* the skyline's night layers (hdsky.inc g_hsNight, added): beacon = 0 the lit windows, coming on as it gets dark; 1 the roof lights, blinking.  Both
   dimmed by the haze / fog as the skyline itself.  Returns 0 when there is nothing to draw */
static int WxNightLayer(SDL_Texture *t, int beacon)
{   const WxState *w = &g_wx; float k, a, d = WxDark();
    if (!t || !g_w3On) return 0;
    k = WxTorrent() ? g_wxFogSet[6].sky : WxFog() >= 2 ? g_wxFogSet[WxFog()].sky : 0;
    a = WxS(beacon ? 0.2f : 0.3f, beacon ? 0.55f : 0.75f, d) * (1 - 0.95f * k * k) * (1 - 0.6f * w->fogMax);
    if (beacon) a *= fmod(WxSeconds(), 1.5) < 0.9 ? 1.0f : 0.1f;
    if (a < 0.01f) return 0;
    SDL_SetTextureColorModFloat(t, 1, 1, 1); SDL_SetTextureAlphaModFloat(t, a > 1 ? 1 : a); return 1; }

/* ---- a sunny day (clear weather, sun up): the sun's glare.  Drawing only ---- */
static float WxSunUp(float *el)   /* how strongly the sun shines (0: none - weather, night) and its elevation (radians) */
{   const WxState *w = &g_wx; float bear, len, up = WxS(-0.04f, 0.10f, w->e);
    *el = 0; if (!g_w3On || WxWeather() != 0 || up <= 0 || g_hsPeriod <= 0) return 0;
    WxSunShadowRaw(&bear, &len); *el = (float)atan(1.0 / (len > 0.01f ? len : 0.01f));
    return up * (1 - w->fogMax); }
/* is the sun behind a building?  The ray from the eye towards it against the buildings' boxes (course3d.h C3_Building footprints, height) */
static int WxSunBlocked(const float *eye, const float *dir)
{   int i, e; float dl = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);
    if (dl < 1e-4f) return 0;
    for (i = 0; i < g_w3c.nbldg; i++) {
        const C3Bldg *B = &g_w3c.bldg[i]; float cx = 0, cz = 0, t0 = 0, t1 = 60000, dx = B->f[0][0] - eye[0], dz = B->f[0][2] - eye[2];
        if (dx * dx + dz * dz > 60000.0f * 60000.0f) continue;
        for (e = 0; e < 4; e++) { cx += B->f[e][0] * 0.25f; cz += B->f[e][2] * 0.25f; }
        for (e = 0; e < 4 && t0 <= t1; e++) {   /* clip the ray to each side's half plane (the side the middle is on) */
            const float *a = B->f[e], *b = B->f[(e + 1) & 3]; float nx = -(b[2] - a[2]), nz = b[0] - a[0], sg, num, den;
            sg = (cx - a[0]) * nx + (cz - a[2]) * nz; if (sg < 0) { nx = -nx; nz = -nz; }
            num = (eye[0] - a[0]) * nx + (eye[2] - a[2]) * nz; den = dir[0] * nx + dir[2] * nz;   /* inside: num + den t >= 0 */
            if (fabsf(den) < 1e-6f) { if (num < 0) t1 = -1; }
            else if (den > 0) { float tt = -num / den; if (tt > t0) t0 = tt; }
            else { float tt = -num / den; if (tt < t1) t1 = tt; } }
        if (t0 <= t1 && eye[1] + dir[1] * t0 < B->f[0][1] + B->h) return 1; }
    return 0; }
/* the glare: a warm bloom round the sun and a few faint lens ghosts across the picture towards its middle, when the sun is in the view and not
   behind a building (eased in and out) */
static void WxGlare(int fw, int fh)
{   static float g; static double lastT; const WxState *w = &g_wx; float el, s = WxSunUp(&el), bear, len, X, Y, R = (float)g_hsRows, want = 0, edge, dt, dir[3];
    int P = g_hsPeriod, k;
    dt = (float)(WxSeconds() - lastT); lastT = WxSeconds(); if (dt < 0 || dt > 0.5f) dt = 0.5f;
    if (s > 0 && R > 0 && P > 0 && g_vid.glare && !getenv("RR_NOGLARE")) {   /* (test switch RR_NOGLARE=1) */
        float tt = (w->t - 0.25f) / 0.5f, e2 = (w->e + 0.04f) * 1.6f; if (tt < 0) tt = 0; if (tt > 1) tt = 1; if (e2 > 1) e2 = 1;
        WxSunShadow(&bear, &len);
        X = (float)g_w3cam.centreX + bear / 6.2831853f * P; Y = g_hsTop + R * 0.80f - e2 * R * 0.74f;   /* where WxSky draws it */
        edge = 1 - WxS(0.35f, 0.6f, fabsf(X - fw * 0.5f) / fw) ; if (Y < -0.2f * fh || Y > fh) edge = 0;
        if (edge > 0 && WxSunDir(g_w3fw, g_w3rt, dir) > 0 && !WxSunBlocked(g_w3eye, dir)) want = s * edge; }
    g += (want - g) * (dt * 6 > 1 ? 1 : dt * 6);
    if (g < 0.01f) return;
    if (!g_wxGlow) WxGlowMake();
    {   float cx = fw * 0.5f, cy = fh * 0.45f, H = (float)fh; static const float gk[5] = { 0.45f, 0.75f, 1.2f, 1.55f, 1.9f }, gr[5] = { 0.035f, 0.06f, 0.025f, 0.08f, 0.045f };
        static const float gc[5][3] = { { 1.0f, 0.75f, 0.35f }, { 0.5f, 0.9f, 0.6f }, { 1.0f, 0.55f, 0.3f }, { 0.45f, 0.6f, 1.0f }, { 0.9f, 0.85f, 0.5f } };
        WxSpot(X, Y, H * 0.34f, 1.0f, 0.88f, 0.62f, 0.18f * g, SDL_BLENDMODE_ADD);   /* the bloom */
        WxSpot(X, Y, H * 0.12f, 1.0f, 0.96f, 0.85f, 0.30f * g, SDL_BLENDMODE_ADD);
        for (k = 0; k < 5; k++) WxSpot(X + (cx - X) * gk[k], Y + (cy - Y) * gk[k], H * gr[k], gc[k][0], gc[k][1], gc[k][2], 0.14f * g, SDL_BLENDMODE_ADD); } }   /* the ghosts */
/* ---- rain / snow: particles in the camera's space (x right, y up from the eye, z ahead; lateral units), moved by the fall and the ride, drawn over the
 * world and under the HUD (GpuCompose) ---- */
#define WX_NP 9600
typedef struct WxP { float x, y, z, ph; } WxP;
static WxP g_wxp[WX_NP]; static int g_wxpKind = -1; static unsigned g_wxpSeed = 991;
#define WX_ZN 60.0f
#define WX_ZF 7000.0f
#define WX_XW 5000.0f
#define WX_YT 3200.0f
static void WxPSpawn(WxP *p, float ground, int back)
{   p->x = (WxRf(&g_wxpSeed) * 2 - 1) * WX_XW; p->ph = WxRf(&g_wxpSeed) * 6.283f;
    p->z = WX_ZN + WxRf(&g_wxpSeed) * (WX_ZF - WX_ZN); p->y = ground + WxRf(&g_wxpSeed) * (WX_YT - ground); (void)back; }   /* evenly through the whole volume */
/* the wind the rain falls in (world units / s along the ground, in the view's x / z): a steady wind - strong in torrential rain, some in heavy rain,
   light in rain - in gusts (overlapping slow waves), and every 4 .. 14 s a gust front swings it round to a new direction (eased in).  The direction is
   kept in the world, so the rain slants across the picture differently as the road turns */
static float g_wxWx, g_wxWz;   /* this frame's wind (WxParticles) */
static void WxWind(float dt, double hd, float *wx, float *wz)
{   static float dir = 0.7f, want = 0.7f, next = 6; static unsigned s = 4711; float t = (float)g_wxTime, base, gust;
    base = WxTorrent() ? 1700.0f : WxHeavy() ? 700.0f : 260.0f;
    if ((next -= dt) <= 0) { next = 4 + 10 * WxRf(&s); want = dir + (WxRf(&s) * 2 - 1) * (WxTorrent() ? 1.4f : 0.8f); }   /* a gust front */
    dir += (want - dir) * (dt * 0.6f > 1 ? 1 : dt * 0.6f);
    gust = 0.55f * sinf(t * 0.31f) + 0.30f * sinf(t * 0.77f + 1.3f) + 0.15f * sinf(t * 1.9f + 2.1f);   /* -1 .. 1 */
    base *= 1 + 0.6f * gust; if (base < 0) base = 0;
    *wx = base * sinf(dir - (float)hd); *wz = base * cosf(dir - (float)hd); }
static void WxParticles(int fw, int fh)
{   static double lastT = -1; static int lastPos; static double lastHd; static SDL_Vertex *vx; static int *ix, cap;
    int kind = WxWeather(), n = kind == 1 ? (WxTorrent() ? 9600 : WxHeavy() ? 3200 : 1400) : (WxHeavy() ? 2800 : 1200), i, nq = 0; float dt, F, cx, hy, ground, fall, speed, dh, ca, sa; double hd;
    (void)fw; (void)fh;
    if (!g_vid.wxfx) { g_wxpKind = -1; return; } if (g_vid.wxfx == 1) n = n * 2 / 5;   /* (True 3D detail > Weather particles: off / low 40 % / high) */
    if (!g_w3On || !g_w3HaveCam || !kind) { g_wxpKind = -1; return; }
    if (!g_wxGlow) WxGlowMake();
    F = (float)(W3_F * (g_w3cam.pxPerUnit > 0 ? g_w3cam.pxPerUnit : 1)); cx = (float)g_w3cam.centreX; hy = (float)g_w3cam.horizonY;
    ground = (float)-g_w3cam.camY; if (ground > -50) ground = -50;
    {   double vp = (g_w3cam.viewPos - g_w3Piece0) / 256.0; int kk = (int)floor(vp); const C3Frame *Fr = W3Fr(); double fr = vp - kk;   /* the camera's heading */
        if (kk < 0) kk = 0; if (kk >= g_w3c.n) kk = g_w3c.n - 1; hd = Fr[kk].heading + (Fr[kk + 1].heading - Fr[kk].heading) * fr; }
    if (kind != g_wxpKind) { for (i = 0; i < WX_NP; i++) WxPSpawn(&g_wxp[i], ground, 0); g_wxpKind = kind; lastT = -1; }
    dt = lastT < 0 ? 0 : (float)(g_wxTime - lastT); if (dt > 0.1f) dt = 0.1f;
    speed = lastT < 0 || dt <= 0 ? 0 : (float)((g_w3cam.viewPos - lastPos) / 256.0 * W3_L);   /* units ridden since the last frame */
    if (speed < 0 || speed > 4000) speed = 0;   /* (a re-found road offset, W3Track) */
    dh = lastT < 0 ? 0 : (float)(hd - lastHd); if (dh > 0.3f || dh < -0.3f) dh = 0;
    lastT = g_wxTime; lastPos = g_w3cam.viewPos; lastHd = hd;
    g_wxShelter += ((g_w3InTunnel ? 1.0f : 0.0f) - g_wxShelter) * (dt * 4 > 1 ? 1 : dt * 4);   /* (in a tunnel: none falls - faded over ~0.25 s) */
    fall = kind == 1 ? 2600.0f : 230.0f; ca = (float)cos(dh); sa = (float)sin(dh);
    {   float wcx = 0, wcz = 0; if (kind == 1) WxWind(dt, hd, &wcx, &wcz); g_wxWx = wcx; g_wxWz = wcz; }   /* rain: blown by the wind (WxWind) */
    if (cap < n) { cap = n; vx = (SDL_Vertex *)realloc(vx, sizeof(SDL_Vertex) * 4 * cap); ix = (int *)realloc(ix, sizeof(int) * 6 * cap); if (!vx || !ix) { cap = 0; return; } }
    for (i = 0; i < n; i++) {
        WxP *p = &g_wxp[i]; float x, z, sx0, sy0, sx1, sy1, wd, a; SDL_FColor c;
        x = p->x * ca - p->z * sa; z = p->x * sa + p->z * ca; p->x = x; p->z = z;   /* the view turned by dh */
        p->z -= speed; p->y -= fall * dt;
        if (kind == 1) { p->x += (g_wxWx * (0.85f + 0.15f * sinf(p->ph)) + sinf((float)g_wxTime * 2.7f + p->ph * 3) * 60) * dt; p->z += g_wxWz * dt; }   /* (a little turbulence each) */
        if (kind == 2) p->x += (float)sin(g_wxTime * 1.3 + p->ph) * 60 * dt;
        /* wrapped round the volume (not respawned in one place): the density stays even - no waves of rain */
        if (p->z < WX_ZN) { p->z += WX_ZF - WX_ZN; p->x = (WxRf(&g_wxpSeed) * 2 - 1) * WX_XW; } else if (p->z > WX_ZF) p->z -= WX_ZF - WX_ZN;
        if (p->x < -WX_XW) p->x += 2 * WX_XW; else if (p->x > WX_XW) p->x -= 2 * WX_XW;
        if (p->y < ground) { p->y += WX_YT - ground; p->x = (WxRf(&g_wxpSeed) * 2 - 1) * WX_XW; }
        sx0 = cx + F * p->x / p->z; sy0 = hy - F * p->y / p->z;
        if (sx0 < g_gpuClip[0] - 20 || sx0 > g_gpuClip[2] + 20 || sy0 < g_gpuClip[1] - 40 || sy0 > g_gpuClip[3] + 40) continue;
        if (kind == 1) {   /* a streak: where the drop was 1/30 s ago (higher, further) to where it is */
            float zb = p->z + speed * 1.0f + 1 - g_wxWz / 30.0f, yb = p->y + fall / 30.0f, xb = p->x - g_wxWx / 30.0f, dx, dy, l;   /* (along its real path: fall, wind, the ride) */
            if (zb < WX_ZN) zb = WX_ZN; sx1 = cx + F * xb / zb; sy1 = hy - F * yb / zb;
            wd = F * 2.6f / p->z; if (wd < 0.45f) wd = 0.45f; if (wd > 1.8f) wd = 1.8f;
            a = (0.30f + 0.25f * (1 - p->z / WX_ZF)) * (1 - g_wxShelter); c.r = 0.78f; c.g = 0.82f; c.b = 0.92f; c.a = a;
            dx = sx1 - sx0; dy = sy1 - sy0; l = (float)sqrt(dx * dx + dy * dy); if (l < 0.5f) { dx = 0; dy = -0.5f; l = 0.5f; }
            dx = -dy / l * wd * 0.5f; dy = (sx1 - sx0) / l * wd * 0.5f;
            vx[nq * 4 + 0].position.x = sx0 - dx; vx[nq * 4 + 0].position.y = sy0 - dy; vx[nq * 4 + 1].position.x = sx0 + dx; vx[nq * 4 + 1].position.y = sy0 + dy;
            vx[nq * 4 + 2].position.x = sx1 - dx; vx[nq * 4 + 2].position.y = sy1 - dy; vx[nq * 4 + 3].position.x = sx1 + dx; vx[nq * 4 + 3].position.y = sy1 + dy;
            {   int q; for (q = 0; q < 4; q++) { vx[nq * 4 + q].color = c; if (q >= 2) vx[nq * 4 + q].color.a = 0; vx[nq * 4 + q].tex_coord.x = 0.5f; vx[nq * 4 + q].tex_coord.y = 0.5f; } } }
        else {   /* a soft flake */
            wd = F * 11.0f / p->z; if (wd < 0.7f) wd = 0.7f; if (wd > 7) wd = 7;
            a = (0.55f + 0.4f * (1 - p->z / WX_ZF)) * (1 - g_wxShelter); c.r = c.g = 1; c.b = 1; c.a = a;
            {   int q; for (q = 0; q < 4; q++) { SDL_Vertex *o = &vx[nq * 4 + q]; o->position.x = sx0 + ((q & 1) ? wd : -wd); o->position.y = sy0 + ((q & 2) ? wd : -wd); o->color = c; o->tex_coord.x = (q & 1) ? 1.0f : 0.0f; o->tex_coord.y = (q & 2) ? 1.0f : 0.0f; } } }
        ix[nq * 6] = nq * 4; ix[nq * 6 + 1] = nq * 4 + 1; ix[nq * 6 + 2] = nq * 4 + 2; ix[nq * 6 + 3] = nq * 4 + 1; ix[nq * 6 + 4] = nq * 4 + 3; ix[nq * 6 + 5] = nq * 4 + 2; nq++;
    }
    {   SDL_Rect vr; vr.x = g_gpuClip[0]; vr.y = g_gpuClip[1]; vr.w = g_gpuClip[2] - g_gpuClip[0]; vr.h = g_gpuClip[3] - g_gpuClip[1] + 1;
        SDL_SetRenderClipRect(g_ren, &vr);
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        if (nq) { if (kind == 2 && g_wxGlow) { SDL_SetTextureBlendMode(g_wxGlow, SDL_BLENDMODE_BLEND); SDL_SetTextureColorModFloat(g_wxGlow, 1, 1, 1); SDL_SetTextureAlphaModFloat(g_wxGlow, 1); }
                  SDL_RenderGeometry(g_ren, kind == 2 ? g_wxGlow : NULL, vx, nq * 4, ix, nq * 6); }
        if (g_wx.flash > 0.02f) {   /* lightning: the whole view lit up a moment */
            SDL_FRect fr; fr.x = (float)vr.x; fr.y = (float)vr.y; fr.w = (float)vr.w; fr.h = (float)vr.h;
            SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_ADD); { float fk = g_wx.flash * (WxTorrent() ? 0.35f : 1.0f); SDL_SetRenderDrawColorFloat(g_ren, 0.55f * fk, 0.58f * fk, 0.7f * fk, 1); }   /* (dimmer in the torrent's fog) */ SDL_RenderFillRect(g_ren, &fr);
            SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE); }
        SDL_SetRenderClipRect(g_ren, NULL); } }

/* ---- the race's Esc pause: the settings panel (drawn with GDI at twice the frame's resolution, shown under the game's own question) ---- */
static void IcBox(HDC dc, int x, int y, int w, int h, COLORREF fill, COLORREF frame, int r);   /* inputcfg.inc */
static void IcPoly(HDC dc, const POINT *pt, int n, COLORREF fill, COLORREF frame);
static SDL_Texture *g_ppTex; static char g_ppKey[96]; static SDL_FRect g_ppRect; static int g_wxBtn, g_wxHot = -1, g_wxCur;   /* panel: where it is drawn; the button row's choice (0 ride on, 1 exit race); the item under the mouse; cursor shown */
/* the panel's picture (W x H, drawn at half size): the setting rows (y 66 + 58 r, arrows boxes at WX_LA / WX_RA), then the button row */
enum { WX_NROW = 4, WX_PW = 900, WX_LA = 552, WX_RA = 834, WX_BY = 66 + WX_NROW * 58 + 12, WX_BH = 56, WX_B0 = 50, WX_B1 = 470, WX_BW = 380, WX_PH = WX_BY + WX_BH + 64 };   /* (setting rows, then the buttons) */
static void WxText(HDC dc, int x, int y, const char *t, COLORREF c, UINT al) { SetTextColor(dc, c); SetTextAlign(dc, al); TextOutA(dc, x, y, t, (int)strlen(t)); SetTextAlign(dc, TA_LEFT | TA_TOP); }
static void WxPanelMake(const char *const *val)
{   enum { W = WX_PW, H = WX_PH }; BITMAPINFO bi; void *bits = NULL; HDC dc; HBITMAP bm; HGDIOBJ ob, of; HFONT ft, fr, fh; int i, r; DWORD *px;
    static const char *lab[WX_NROW] = { "TIME OF DAY", "WEATHER", "FOG", "WEAPON" };   /* (BUILDING SHADOWS removed - user, 2026-11-15) */
    memset(&bi, 0, sizeof bi); bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    if (!(dc = CreateCompatibleDC(NULL))) return;
    if (!(bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0)) || !bits) { DeleteDC(dc); return; }
    ob = SelectObject(dc, bm); px = (DWORD *)bits;
    for (i = 0; i < W * H; i++) px[i] = 0x00FF00FFu;   /* (key: transparent) */
    SetBkMode(dc, TRANSPARENT);
    IcBox(dc, 0, 0, W, H, RGB(16, 16, 34), RGB(90, 90, 130), 28);
    ft = CreateFontA(40, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_IMPACT));
    fr = CreateFontA(34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_ARIAL));
    fh = CreateFontA(21, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_ARIAL));
    of = SelectObject(dc, ft); WxText(dc, 28, 12, "TRUE 3D WORLD", RGB(255, 96, 40), TA_LEFT | TA_TOP);
    for (r = 0; r < WX_NROW; r++) { int y = 66 + r * 58, on = r == g_wxSel; COLORREF c = on ? RGB(255, 235, 80) : RGB(225, 225, 240);
        if (on) { POINT a[3]; IcBox(dc, 16, y - 6, W - 32, 50, RGB(70, 58, 16), RGB(255, 235, 80), 16); a[0].x = 30; a[0].y = y + 7; a[1].x = 30; a[1].y = y + 31; a[2].x = 44; a[2].y = y + 19; IcPoly(dc, a, 3, c, c); }
        SelectObject(dc, fr); WxText(dc, 58, y, lab[r], c, TA_LEFT | TA_TOP);
        {   int k; for (k = 0; k < 2; k++) {   /* the arrows: boxes the mouse can click */
                int ax = k ? WX_RA : WX_LA, hot = g_wxHot == 10 + 2 * r + k; POINT a[3];
                IcBox(dc, ax, y + 1, 40, 36, hot ? RGB(120, 100, 30) : RGB(40, 40, 70), c, 10);
                a[0].x = ax + (k ? 14 : 26); a[0].y = y + 9; a[1].x = a[0].x; a[1].y = y + 29; a[2].x = ax + (k ? 28 : 12); a[2].y = y + 19; IcPoly(dc, a, 3, c, c); }
            WxText(dc, (WX_LA + 40 + WX_RA) / 2, y, val[r], c, TA_CENTER | TA_TOP); } }
    for (r = 0; r < 2; r++) {   /* the buttons: ride on / exit race */
        int x = r ? WX_B1 : WX_B0, on = g_wxSel == WX_NROW && g_wxBtn == r, hot = g_wxHot == 40 + r; COLORREF c = on ? RGB(255, 235, 80) : RGB(225, 225, 240);
        IcBox(dc, x, WX_BY, WX_BW, WX_BH, on ? (r ? RGB(120, 30, 20) : RGB(30, 90, 40)) : hot ? RGB(60, 60, 95) : RGB(36, 36, 62), on ? c : RGB(110, 110, 150), 16);
        SelectObject(dc, fr); WxText(dc, x + WX_BW / 2, WX_BY + 9, r ? "EXIT RACE" : "CLOSE", c, TA_CENTER | TA_TOP); }
    SelectObject(dc, fh); WxText(dc, W / 2, H - 38, "Arrows choose / change  -  Enter or click a button  -  Esc exits the race", RGB(150, 150, 175), TA_CENTER | TA_TOP);
    SelectObject(dc, of); DeleteObject(ft); DeleteObject(fr); DeleteObject(fh); GdiFlush();
    for (i = 0; i < W * H; i++) px[i] = (px[i] & 0xFFFFFFu) == 0xFF00FFu ? 0 : (px[i] & 0xFFFFFFu) | 0xF0000000u;
    if (!g_ppTex) { g_ppTex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, W, H); if (g_ppTex) { SDL_SetTextureBlendMode(g_ppTex, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(g_ppTex, SDL_SCALEMODE_LINEAR); } }
    if (g_ppTex) SDL_UpdateTexture(g_ppTex, NULL, px, W * 4);
    SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc); }
static void WxPanel(int w, int h)
{   static const char *tods[5] = { "Cycle", "Dawn", "Day", "Sunset", "Night" }, *wxs[6] = { "Clear", "Rain", "Snow", "Heavy rain", "Heavy snow", "Torrential rain" }; const char *const *fogs = g_wxFogName;
    char v0[48], key[96]; SDL_FRect d; (void)h;
    if (!g_w3On || !g_wxPause || !g_ren) return;
    if (!WxTod()) { int m = (int)(WxT() * 1440) % 1440; _snprintf(v0, sizeof v0, "Cycle %02d:%02d", m / 60, m % 60); } else strcpy(v0, tods[WxTod()]); v0[sizeof v0 - 1] = 0;
    {   const char *vals[WX_NROW]; vals[0] = v0; vals[1] = wxs[WxWeatherRaw()]; vals[2] = fogs[WxFog()]; vals[3] = WpRowName();
        _snprintf(key, sizeof key, "%d|%d|%d|%s|%s|%s|%s", g_wxSel, g_wxBtn, g_wxHot, vals[0], vals[1], vals[2], vals[3]); key[sizeof key - 1] = 0;
        if (!g_ppTex || strcmp(key, g_ppKey)) { WxPanelMake(vals); strcpy(g_ppKey, key); } }
    if (!g_ppTex) return;
    d.w = WX_PW / 2; d.h = WX_PH / 2; d.x = (w - d.w) / 2; d.y = g_gpuClip[3] - d.h - 24; if (d.y < g_gpuClip[1]) d.y = (float)g_gpuClip[1];
    g_ppRect = d;
    SDL_RenderTexture(g_ren, g_ppTex, NULL, &d); }
static void WxChange(int row, int d)   /* a setting row one step on (d = 1) or back (-1) */
{   if (row == 0) g_vid.tod = (g_vid.tod + d + 5) % 5;
    else if (row == 1) { static const int ord[6] = { 0, 1, 3, 5, 2, 4 }; int k; for (k = 0; k < 6 && ord[k] != g_vid.weather; k++) {} g_vid.weather = ord[((k < 6 ? k : 0) + d + 6) % 6]; }   /* clear, rain, heavy rain, snow, heavy snow */
    else if (row == 3) WpRowStep(d);   /* the weapon (weapons.inc: a Big Game's owned weapons, else the pictures) */
    else { int k; for (k = 0; k < WX_NFOG && g_wxFogOrd[k] != g_vid.fog; k++) {} g_vid.fog = g_wxFogOrd[((k < WX_NFOG ? k : 0) + d + WX_NFOG) % WX_NFOG]; }
    g_wxDirty = 1; WxCompute(0); }
static void WxCursor(int on)   /* the mouse pointer while the panel is up (the race hides it) */
{   if (on && !g_wxCur) { int n = 0; while (ShowCursor(TRUE) < 0 && n < 20) n++; g_wxCur = n + 1; SetCursor(LoadCursorA(NULL, (LPCSTR)IDC_ARROW)); }
    else if (!on && g_wxCur) { while (g_wxCur-- > 0) ShowCursor(FALSE); g_wxCur = 0; } }
/* the game's Esc prompt (race_session.c rr_PromptKey): -1 = it opens, -2 = it closes, else a released key.  Returns 0 = not taken (the game's answer),
   1 = taken here (the prompt stays), 2 = close the prompt and ride on, 3 = exit the race */
__declspec(dllexport) int __stdcall RR_PauseKey(int code)
{   if (code == -1) { g_wxPause = 1; g_wxSel = 0; g_wxBtn = 0; g_wxHot = -1; if (g_w3On) WxCursor(1); return g_w3On; }
    if (code == -2) { g_wxPause = 0; WxCursor(0); if (g_wxDirty) { VidSave(); g_wxDirty = 0; } return 0; }
    if (!g_w3On || !g_wxPause) return 0;
    switch (code) {
    case VK_UP: g_wxSel = (g_wxSel + WX_NROW) % (WX_NROW + 1); return 1;
    case VK_DOWN: g_wxSel = (g_wxSel + 1) % (WX_NROW + 1); return 1;
    case VK_LEFT: case VK_RIGHT:
        if (g_wxSel == WX_NROW) g_wxBtn ^= 1; else WxChange(g_wxSel, code == VK_RIGHT ? 1 : -1);
        return 1;
    case VK_RETURN: return g_wxSel == WX_NROW ? 2 + g_wxBtn : 2;
    default: return 0; } }
/* a mouse message during the prompt (rr_PromptKey): hovering lights the item, a click acts on it.  Returns as RR_PauseKey */
__declspec(dllexport) int __stdcall RR_PauseMouse(const MSG *m)
{   int x, y, px, py, hit = -1, r;
    if (!g_w3On || !g_wxPause || !m || g_ppRect.w <= 0) return 0;
    MsgFramePos(m, &x, &y);
    px = (int)((x - g_ppRect.x) * WX_PW / g_ppRect.w); py = (int)((y - g_ppRect.y) * WX_PH / g_ppRect.h);
    for (r = 0; r < WX_NROW; r++) { int ry = 66 + r * 58;
        if (py >= ry - 6 && py < ry + 44) { hit = r; if (px >= WX_LA && px < WX_LA + 40) hit = 10 + 2 * r; else if (px >= WX_RA && px < WX_RA + 40) hit = 11 + 2 * r; } }
    if (py >= WX_BY && py < WX_BY + WX_BH) { if (px >= WX_B0 && px < WX_B0 + WX_BW) hit = 40; else if (px >= WX_B1 && px < WX_B1 + WX_BW) hit = 41; }   /* (buttons 40 / 41: the rows' arrows are 10 + 2 r ..) */
    if (m->message == WM_MOUSEMOVE) { g_wxHot = hit >= 10 ? hit : -1; if (hit >= 40) { g_wxSel = WX_NROW; g_wxBtn = hit - 40; } SetCursor(LoadCursorA(NULL, (LPCSTR)IDC_ARROW)); return 1; }
    if (m->message != WM_LBUTTONUP) return 1;   /* (the other mouse messages: nothing to the game) */
    if (hit >= 40) return 2 + (hit - 40);
    if (hit >= 10) { g_wxSel = (hit - 10) / 2; WxChange(g_wxSel, (hit & 1) ? 1 : -1); return 1; }
    if (hit >= 0) g_wxSel = hit;
    return 1; }

/* a lightning strike's bolt in the sky (HsDraw: over the clouds, behind the skyline): a jagged line from the cloud base down to the horizon with a branch or
   two, white with a blue glow, flickering with the flash */
static void WxBoltSeg(SDL_Vertex *v, int *ix, int *n, float x0, float y0, float x1, float y1, float wd, float r, float g, float b, float a)
{   float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy), nx, ny; int q, k = *n;
    if (l < 0.01f) return; nx = -dy / l * wd * 0.5f; ny = dx / l * wd * 0.5f;
    v[k].position.x = x0 - nx; v[k].position.y = y0 - ny; v[k + 1].position.x = x0 + nx; v[k + 1].position.y = y0 + ny;
    v[k + 2].position.x = x1 - nx; v[k + 2].position.y = y1 - ny; v[k + 3].position.x = x1 + nx; v[k + 3].position.y = y1 + ny;
    for (q = 0; q < 4; q++) { v[k + q].color.r = r; v[k + q].color.g = g; v[k + q].color.b = b; v[k + q].color.a = a; v[k + q].tex_coord.x = v[k + q].tex_coord.y = 0; }
    ix[k / 4 * 6] = k; ix[k / 4 * 6 + 1] = k + 1; ix[k / 4 * 6 + 2] = k + 2; ix[k / 4 * 6 + 3] = k + 1; ix[k / 4 * 6 + 4] = k + 3; ix[k / 4 * 6 + 5] = k + 2; *n = k + 4; }
static void WxBolt(int P, int R, int y0, int y1)
{   static SDL_Vertex v[4 * 3 * 64]; static int ix[6 * 3 * 64]; float px[64], py[64]; int n = 0, np = 0, i, br; unsigned s = 9001u + (unsigned)g_wxBoltN * 7919u;
    float f = g_wx.flash, x, y, step = R * 0.07f, bottom = g_hsTop + R * 0.86f; (void)P;
    if (!g_w3On || WxWeather() != 1 || f < 0.12f || g_wxFlashT > 0.35f) return;
    x = g_gpuClip[0] + (0.15f + 0.7f * WxRf(&s)) * (g_gpuClip[2] - g_gpuClip[0]); y = (float)(y0 > g_hsTop ? y0 : g_hsTop) + R * 0.04f;
    while (y < bottom && np < 40) { px[np] = x; py[np] = y; np++; x += (WxRf(&s) * 2 - 1) * R * 0.05f; y += step * (0.6f + 0.8f * WxRf(&s)); }
    px[np] = x; py[np] = bottom; np++;
    for (i = 0; i + 1 < np; i++) { WxBoltSeg(v, ix, &n, px[i], py[i], px[i + 1], py[i + 1], 7.0f, 0.45f, 0.55f, 1.0f, 0.35f * f); WxBoltSeg(v, ix, &n, px[i], py[i], px[i + 1], py[i + 1], 1.8f, 1, 1, 1, f); }
    for (br = 0; br < 2; br++) {   /* branches off the main bolt */
        int a = 1 + (int)(WxRf(&s) * (np / 2)); float bx = px[a], by = py[a], dir = WxRf(&s) < 0.5f ? -1.0f : 1.0f; int k;
        for (k = 0; k < 4 && n + 8 <= 4 * 3 * 64; k++) { float nx2 = bx + dir * R * (0.03f + 0.04f * WxRf(&s)), ny2 = by + step * (0.4f + 0.6f * WxRf(&s));
            WxBoltSeg(v, ix, &n, bx, by, nx2, ny2, 4.0f, 0.45f, 0.55f, 1.0f, 0.25f * f); WxBoltSeg(v, ix, &n, bx, by, nx2, ny2, 1.0f, 0.9f, 0.92f, 1, 0.8f * f); bx = nx2; by = ny2; } }
    (void)y1;
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_ADD); SDL_RenderGeometry(g_ren, NULL, v, n, ix, n / 4 * 6); SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_NONE); }

/* ---- the race start: "3, 2, 1" (Windows speech, SAPI, on a thread of its own; not in silent / headless runs) and the countdown undimmed ---- */
#include <sapi.h>
static const GUID WX_CLSID_SpVoice = { 0x96749377, 0x3391, 0x11D2, { 0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96 } };
static const GUID WX_IID_ISpVoice = { 0x6C44DF74, 0x72B9, 0x4992, { 0xA1, 0xEC, 0xEF, 0x99, 0x6E, 0x04, 0x22, 0xD4 } };
static HANDLE g_vxEv; static const wchar_t *volatile g_vxSay;
static DWORD WINAPI WxVoiceThread(LPVOID p)
{   ISpVoice *v = NULL; (void)p;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(&WX_CLSID_SpVoice, NULL, CLSCTX_ALL, &WX_IID_ISpVoice, (void **)&v)) || !v) { LOG("voice: no speech (SAPI)"); return 0; }
    ISpVoice_SetVolume(v, 100);
    for (;;) { const wchar_t *s; WaitForSingleObject(g_vxEv, INFINITE); s = g_vxSay; g_vxSay = NULL;
        if (s) ISpVoice_Speak(v, s, SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_XML, NULL); } }
static void WxSay(const wchar_t *s)
{   const char *e = getenv("RR_AUDIO");
    if ((e && !_stricmp(e, "off")) || g_headless || g_quiet) return;
    if (!g_vxEv) { g_vxEv = CreateEventA(NULL, FALSE, FALSE, NULL); if (!g_vxEv) return; CloseHandle(CreateThread(NULL, 0, WxVoiceThread, NULL, 0, NULL)); }
    g_vxSay = s; SetEvent(g_vxEv); }
/* race_session.c rr_FramePump, every frame of the start countdown: ticks left (180 .. 0) and the race palette */
static volatile int g_cdLeft; static double g_cdGoT = -10;   /* (the fancy countdown, CdDraw) */
static double CdNow(void) { LARGE_INTEGER n, f; QueryPerformanceCounter(&n); QueryPerformanceFrequency(&f); return (double)n.QuadPart / f.QuadPart; }
/* the HUD (src\ui\hud.c) asks: draw the start countdown's number ourselves (CdDraw) - not in the deterministic test runs (their pictures stay the game's) */
__declspec(dllexport) int __stdcall RR_FancyCountdown(void) { static int det = -1; if (det < 0) det = getenv("RR_DETERMINISTIC") != NULL; return g_ren && !det; }
static BYTE *SgMask(HDC dc, const char *t, int w, int h, int x, int y);   /* musicbox.inc */
static SDL_Texture *g_cdTex[4]; static int g_cdW[4], g_cdH[4];
static void CdMake(int k)   /* "1" "2" "3" "GO!": white, a thick dark outline, 220 px high */
{   static const char *txt[4] = { "1", "2", "3", "GO!" }; HDC dc = CreateCompatibleDC(NULL); HFONT f, o; SIZE sz; int w, h, x, y, R = 10, i, j; BYTE *m; DWORD *out;
    if (!dc) return;
    f = CreateFontA(-220, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, RrFont(RR_FONT_IMPACT)); o = (HFONT)SelectObject(dc, f);
    GetTextExtentPoint32A(dc, txt[k], (int)strlen(txt[k]), &sz); w = sz.cx + 2 * R + 8; h = sz.cy + 2 * R + 8;
    free(SgMask(dc, txt[k], 1, 1, 0, 0)); m = SgMask(dc, txt[k], w, h, R + 4, R + 4);
    if (m && (out = (DWORD *)malloc((size_t)w * h * 4)) != NULL) {
        for (y = 0; y < h; y++) for (x = 0; x < w; x++) { int t = m[y * w + x], ol = 0, a, l;
            for (j = -R; j <= R && ol < 255; j += 2) for (i = -R; i <= R; i += 2) { int xx = x + i, yy = y + j; if (i * i + j * j > R * R || xx < 0 || yy < 0 || xx >= w || yy >= h) continue; if (m[yy * w + xx] > ol) ol = m[yy * w + xx]; }
            a = t + ol * 220 / 255 * (255 - t) / 255; l = a ? t * 255 / a : 0;   /* white letters over a dark rim */
            l = l * (200 + 55 * (h - y) / h) / 255;   /* (a little lighter at the top) */
            out[y * w + x] = ((DWORD)a << 24) | ((DWORD)l << 16) | ((DWORD)l << 8) | (DWORD)l; }
        g_cdTex[k] = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (g_cdTex[k]) { SDL_UpdateTexture(g_cdTex[k], NULL, out, w * 4); SDL_SetTextureBlendMode(g_cdTex[k], SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(g_cdTex[k], SDL_SCALEMODE_LINEAR); g_cdW[k] = w; g_cdH[k] = h; }
        free(out); }
    free(m); SelectObject(dc, o); DeleteObject(f); DeleteDC(dc); }
/* the fancy start countdown (gpuscene.inc GpuCompose, over the HUD): each number punches in big, settles, then swells and fades - 3 red, 2 amber,
   1 yellow - then GO! in green, growing as it goes; a soft glow of its colour behind */
static void CdDraw(int fw, int fh)
{   static const float col[4][3] = { { 1.0f, 0.92f, 0.25f }, { 1.0f, 0.62f, 0.12f }, { 1.0f, 0.22f, 0.15f }, { 0.35f, 1.0f, 0.35f } };
    int left = g_cdLeft, k; float p, sc, a; double go = CdNow() - g_cdGoT; SDL_FRect d;
    if (!RR_FancyCountdown()) return;
    if (left > 0) { k = (left - 1) / 60; if (k > 2) k = 2; p = 1 - (float)((left - 1) % 60 + 1) / 60.0f; }
    else if (go >= 0 && go < 0.9) { k = 3; p = (float)(go / 0.9); }
    else return;
    if (!g_cdTex[k]) CdMake(k); if (!g_cdTex[k]) return;
    if (k < 3) { float q = p < 0.22f ? p / 0.22f : 1; q = 1 - (1 - q) * (1 - q) * (1 - q);
        sc = p < 0.22f ? 2.6f + (1.0f - 2.6f) * q : (p > 0.8f ? 1 + (p - 0.8f) * 1.8f : 1 + 0.03f * sinf(p * 25));
        a = p < 0.12f ? p / 0.12f : (p > 0.8f ? 1 - (p - 0.8f) / 0.2f : 1); }
    else { sc = 1 + p * 0.9f; a = p < 0.5f ? 1 : 1 - (p - 0.5f) / 0.5f; }
    {   float hgt = fh * 0.30f * sc, wid = hgt * g_cdW[k] / g_cdH[k], cx = fw * 0.5f, cy = fh * 0.36f;
        if (!g_wxGlow) WxGlowMake();
        WxSpot(cx, cy, hgt * 0.9f, col[k][0], col[k][1], col[k][2], 0.35f * a, SDL_BLENDMODE_ADD);
        d.x = cx - wid / 2; d.y = cy - hgt / 2; d.w = wid; d.h = hgt;
        SDL_SetTextureColorModFloat(g_cdTex[k], col[k][0], col[k][1], col[k][2]); SDL_SetTextureAlphaModFloat(g_cdTex[k], a < 0 ? 0 : a);
        SDL_RenderTexture(g_ren, g_cdTex[k], NULL, &d); } }
__declspec(dllexport) void __stdcall RR_RaceCountdown(int left, const void *pal)
{   static int last;
    if (left <= 0 && last > 0) g_cdGoT = CdNow();
    g_cdLeft = left > 0 ? left : 0;
    g_cdPal = left > 0 ? (const PALETTEENTRY *)pal : NULL;
    if (left > 0 && last <= 0) WxSay(L"<pitch middle='3'>Three</pitch>");
    else if (last > 120 && left <= 120) WxSay(L"<pitch middle='4'>Two</pitch>");
    else if (last > 60 && left <= 60) WxSay(L"<pitch middle='5'>One</pitch>");   /* (no spoken "Go!" - user, 2026-11-15: "get rid of the speech Go") */
    last = left; }
