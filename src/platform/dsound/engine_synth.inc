/* HD engine (included by rrdsound.c; user, 2026-11-21: "a hd engine option in sound options ... rat bikes, super bikes etc need their own variety of sound
 * depending on their cc level ... include like the gear changing"; then "cant seem to match the original style"): the game's own engine sounds, made clean
 * and alive, one engine for each bike, with gear changes.
 *
 * The game (src\audio\engine_sound.c, Sound Mix > HD engine on) calls RR_EngineVoice every frame for each engine it hears - voice 0 the player's, 1 / 2 the two
 * nearest rivals - with the bike model (0..14), its gear (0..6), its revs (+0x2dc: 0x2000 idle .. 0xffff), the Doppler factor (0x100 = none) and the volume / pan
 * the original loop would have had (hundredths of dB, as DirectSound).  It returns 0 when there is no HD engine (the loops below could not be read): the game
 * then plays its own loops.  A voice fades out when its calls stop.
 *
 * The sound: the class's original loop (AUDIO\EFFECTS\ENGRAT / ENGSPORT / ENGSUPER.WAV, 26 / 26 / 52 ms of 11 kHz 8-bit; ENGNITRO with nitro) is taken apart
 * into its harmonics once and rebuilt as a smooth 2048-point wave, as loud as the original, - the same timbre without the 8-bit grit and the stepped playback - and played at the
 * original's pitch (the game's: playback rate = revs x Doppler / 512, one loop a cycle).  Each bike then has its own (g_engProf, by cc from the shop's text):
 * its pitch within the class (big engines lower), its brightness, how much one cycle differs from the next (a real engine never repeats exactly), a breath of
 * exhaust noise.
 *   load      from the revs: falling revs = off the throttle (quieter, darker, overrun pops); rising or steady = on it
 *   gears     a change of the gear the game reports: up = a 70 ms ignition cut, the gearbox's clunk, a bark when it fires again (the revs drop is the game's
 *             own, smoothed); down = a clunk and a throttle blip, then a pop or two
 *   limiter   at the redline the ignition cuts in and out
 *   flags     1 = nitro (the ENGNITRO loop), 2 = in a tunnel (two echoes) */
#define ENG_VOICES 3
#define ENG_ECHO   4096          /* echo line (samples, ~93 ms) */
#define ENG_TAB    2048          /* points of a rebuilt loop */
#define ENG_MAKEUP 2.5f          /* +8 dB over the game's engine volume: at 100 % the game's curve leaves the engine at -8.5 dB (user, 2026-11-21: "its 100% and its the softer than the other sounds") */
enum { ENG_RAT, ENG_SPORT, ENG_SUPER, ENG_NITRO, ENG_SRC };
typedef struct EngProf {
    int cc, cls;                 /* cls: the loop (ENG_RAT / SPORT / SUPER) */
    float pitch;                 /* x the class's pitch */
    float bright;                /* low pass at full load (Hz) */
    float rough;                 /* cycle-to-cycle variation */
    float breath;                /* exhaust noise */
    float gain;
} EngProf;
/* 0..14 = Rat 0..4, Sport 5..9, Super 10..14 (src\ui\menu_pages.c rr_BikeClassSelect; the names and cc from the game's shop text) */
static const EngProf g_engProf[15] = {
    { 450,  ENG_RAT,   0.95f,  9000, 0.07f, 0.05f, 1.00f },   /* Rat    Banzai Sport 450 */
    { 400,  ENG_RAT,   1.00f, 10000, 0.05f, 0.04f, 1.00f },   /* Rat    Corsair Swallow 400 */
    { 250,  ENG_RAT,   1.08f, 11000, 0.06f, 0.06f, 0.95f },   /* Rat    Kamikaze ZYX 250 */
    { 125,  ENG_RAT,   1.15f, 11000, 0.08f, 0.07f, 0.90f },   /* Rat    Perro Pico 125 */
    { 500,  ENG_RAT,   0.92f,  8500, 0.10f, 0.05f, 1.00f },   /* Rat    Killer Rat 500 */
    { 750,  ENG_SPORT, 0.96f,  8500, 0.06f, 0.04f, 1.00f },   /* Sport  Diablo Vampiro 750 */
    { 750,  ENG_SPORT, 1.00f,  9500, 0.04f, 0.04f, 1.00f },   /* Sport  Kamikaze ZYX 750 */
    { 600,  ENG_SPORT, 1.05f, 10000, 0.04f, 0.04f, 0.97f },   /* Sport  Stiletto Aggressore 600 */
    { 1000, ENG_SPORT, 0.88f,  7500, 0.09f, 0.05f, 1.05f },   /* Sport  DMG M10 1000 */
    { 250,  ENG_SPORT, 1.12f, 10500, 0.07f, 0.06f, 0.95f },   /* Sport  Perro Grande 250 */
    { 1100, ENG_SUPER, 0.92f,  8000, 0.05f, 0.04f, 1.05f },   /* Super  Banzai Super Sport 1100 */
    { 1000, ENG_SUPER, 0.95f,  7500, 0.07f, 0.04f, 1.05f },   /* Super  Diablo Vipera N 1000 */
    { 900,  ENG_SUPER, 1.00f,  8500, 0.05f, 0.04f, 1.00f },   /* Super  Stiletto Assassino N 900 */
    { 600,  ENG_SUPER, 1.08f,  9500, 0.04f, 0.04f, 1.00f },   /* Super  Corsair Raven N 600 */
    { 750,  ENG_SUPER, 1.03f,  9000, 0.05f, 0.04f, 1.00f } }; /* Super  Kamikaze ZYX 750N */

static float g_engWave[ENG_SRC][ENG_TAB + 1];   /* the rebuilt loops (+1: the first point again, for the interpolation) */
static int g_engLen[ENG_SRC];                   /* the original loop's samples (its pitch: a loop per playback rate / len) */
static volatile int g_engTab;                   /* 0 not tried, 1 ready, -1 a loop missing */

/* an original loop (8- or 16-bit mono WAV) -> its harmonics -> g_engWave[src]: harmonics below 1/100 of the strongest (the 8-bit noise floor) left out,
   the top fifth rolled off, the loop's own loudness (RMS) */
static int EngLoadLoop(int src,const char *name)
{   char path[64]; FILE *f; static BYTE raw[65536]; DWORD n=0, pos=12, sz, bits=8, ch=1; float *s, mx=0, *re, *im; int N, k, j, K;
    sprintf(path,"AUDIO\\EFFECTS\\%s.WAV",name); if(!(f=fopen(path,"rb"))) return 0; n=(DWORD)fread(raw,1,sizeof raw,f); fclose(f);
    if(n<44 || memcmp(raw,"RIFF",4)) return 0;
    for(;;){ if(pos+8>n) return 0; sz=*(DWORD*)(raw+pos+4);
        if(!memcmp(raw+pos,"fmt ",4)){ ch=*(WORD*)(raw+pos+10); bits=*(WORD*)(raw+pos+22); }
        else if(!memcmp(raw+pos,"data",4)){ pos+=8; if(pos+sz>n) sz=n-pos; break; }
        pos+=8+sz+(sz&1); }
    N=(int)(sz/(bits/8)/ch); if(N<16 || N>8192) return 0;
    s=(float*)malloc(sizeof(float)*N*3); re=s+N; im=re+N; if(!s) return 0;
    for(j=0;j<N;j++){ const BYTE *q=raw+pos+(size_t)j*ch*(bits/8); s[j]=bits==16?*(const short*)q/32768.0f:(q[0]-128)/128.0f; }
    K=N/2; for(k=1;k<K;k++){ double a=0, b=0; for(j=0;j<N;j++){ double w=6.283185307179586*k*j/N; a+=s[j]*cos(w); b-=s[j]*sin(w); } re[k]=(float)(a*2/N); im[k]=(float)(b*2/N);
        if(sqrt(a*a+b*b)*2/N>mx) mx=(float)(sqrt(a*a+b*b)*2/N); }
    for(k=1;k<K;k++){ float m=sqrtf(re[k]*re[k]+im[k]*im[k]), roll=k>K*4/5?(float)(K-k)/(K/5):1.0f; if(m<mx*0.01f) roll=0; re[k]*=roll; im[k]*=roll; }
    {   static float ct[ENG_TAB]; double e0=0, e1=0; float m=0; for(j=0;j<ENG_TAB;j++) ct[j]=(float)cos(6.283185307179586*j/ENG_TAB);
        for(j=0;j<N;j++) m+=s[j]/N; for(j=0;j<N;j++) e0+=(s[j]-m)*(s[j]-m)/N;
        for(j=0;j<ENG_TAB;j++){ float v=0; for(k=1;k<K;k++) if(re[k]!=0 || im[k]!=0){ int a=(int)(((long long)k*j)%ENG_TAB); v+=re[k]*ct[a]-im[k]*ct[(a+ENG_TAB*3/4)%ENG_TAB]; }   /* (cos(x - 90 deg) = sin x) */
            g_engWave[src][j]=v; e1+=(double)v*v/ENG_TAB; }
        if(e1>0) for(j=0;j<ENG_TAB;j++) g_engWave[src][j]*=(float)sqrt(e0/e1);   /* (as loud as the original loop) */
        g_engWave[src][ENG_TAB]=g_engWave[src][0]; }
    free(s); g_engLen[src]=N; return 1; }
static void EngLoad(void)
{   static const char *const nm[ENG_SRC]={ "ENGRAT", "ENGSPORT", "ENGSUPER", "ENGNITRO" }; int i, ok=1;
    for(i=0;i<ENG_SRC;i++) if(!EngLoadLoop(i,nm[i])) ok=0;
    LOG("hd engine: %s",ok?"loops rebuilt":"a loop could not be read - the game's own engine sounds"); g_engTab=ok?1:-1; }

typedef struct EngVoice {
    /* from the game (RR_EngineVoice) */
    volatile int bike, gear, revs, dop, volCb, panCb, flags, id; volatile LONG seq;
    /* synth state (mixer thread) */
    LONG seen; int on, gear0, id0; float age, level;
    double ph;                   /* the loop's phase 0..1 */
    float f0, rev01, revPrev, load, loadT, cycAmp, cycPitch;
    float cut, blip, bark, popT, pop; /* gear change: ignition cut left (s), throttle blip left (s), bark left (s), pops time left (s); the current pop */
    float clunk, click;          /* the gearbox's clunk */
    double thPh;
    float cl[2], lp, lp2, nlp, dc;
    float echo[ENG_ECHO]; int echoPos;
    unsigned rnd;
} EngVoice;
static EngVoice g_eng[ENG_VOICES];

/* id: which bike it is (its object) - the nearest riders change places: a voice taken over by another bike starts afresh, no gear change heard */
__declspec(dllexport) int __stdcall RR_EngineVoice(int voice,int bike,int gear,int revs,int dop,int volCb,int panCb,int flags,int id)
{   EngVoice *v; if(voice<0 || voice>=ENG_VOICES) return 0;
    if(!g_engTab) EngLoad();
    if(g_engTab<0) return 0;
    v=&g_eng[voice];
    v->bike=bike<0?0:(bike>14?14:bike); v->gear=gear; v->revs=revs; v->dop=dop; v->volCb=volCb; v->panCb=panCb; v->flags=flags; v->id=id; InterlockedIncrement(&v->seq); return 1; }

static float EngRand(EngVoice *v){ v->rnd=v->rnd*1664525u+1013904223u; return ((v->rnd>>9)&0xffff)/32768.0f-1.0f; }   /* -1 .. 1 */
static float EngRes(float *z,float x,float hz,float q)   /* a two-pole resonator (band pass, peak gain ~1) */
{   float w=6.2831853f*hz/OUT_RATE, r, y; if(w>2.8f) w=2.8f; r=1.0f-w/(2.0f*q); if(r<0.5f) r=0.5f;
    y=(1.0f-r)*2.0f*sinf(w)*x+2.0f*r*cosf(w)*z[0]-r*r*z[1]; z[1]=z[0]; z[0]=y; return y; }

static void EngVoiceMix(EngVoice *v,float *acc,int frames)
{   const EngProf *p; const float *wave; int i, upd=0, src; float gl, gr, want, dt=1.0f/OUT_RATE, tgtF0, tunnel;
    LONG s=v->seq;
    if(g_engTab!=1) return;
    if(s!=v->seen){ v->seen=s; v->age=0; upd=1; } else v->age+=frames*dt;
    if(!v->on){ if(!upd) return; memset((char*)v+offsetof(EngVoice,on),0,sizeof(EngVoice)-offsetof(EngVoice,on)); v->on=1; v->gear0=v->gear; v->id0=v->id; v->rnd=0x9e3779b9u*(unsigned)(v-g_eng+1); v->rev01=-1; v->load=1; v->cycAmp=1; v->cycPitch=1; }
    p=&g_engProf[v->bike];
    src=(v->flags&1)?ENG_NITRO:p->cls; wave=g_engWave[src];
    want=(v->age<0.15f && v->volCb>-9000)?1.0f:0.0f;
    if(want==0 && v->level<0.0005f){ v->on=0; return; }
    gl=(float)Gain(v->volCb)*ENG_MAKEUP; gr=gl; if(v->panCb>0) gl*=(float)Gain(-v->panCb); else if(v->panCb<0) gr*=(float)Gain(v->panCb);
    tunnel=(v->flags&2)?1.0f:0.0f;
    if(upd){   /* a new frame from the game: load from the revs' slope, gear changes */
        float r=(v->revs-0x2000)/(float)(0xffff-0x2000), d;
        if(r<0) r=0; if(r>1.02f) r=1.02f;
        if(v->id!=v->id0){ v->id0=v->id; v->gear0=v->gear; v->rev01=-1; }   /* another bike in this voice */
        if(v->rev01<0){ v->rev01=r; v->revPrev=r; v->f0=0; }
        d=r-v->revPrev; v->revPrev=r; v->rev01=r;
        v->loadT = d<-0.0005f ? 0.15f : (r<0.04f ? 0.5f : 1.0f);
        if(v->gear!=v->gear0){
            if(v->gear>v->gear0 && v->rev01>0.15f){ v->cut=0.07f; v->bark=0.11f; v->clunk=1.0f; v->click=1.0f; }
            else if(v->gear<v->gear0){ v->blip=0.12f; v->clunk=0.7f; v->click=0.8f; v->popT=0.45f; }
            v->gear0=v->gear; } }
    /* the original's pitch: playback rate revs x Doppler / 512 (engine_sound.c), one loop each len samples of it */
    tgtF0=(float)v->revs*(v->dop>0?v->dop:256)/512.0f/g_engLen[src]*p->pitch*(v->blip>0?1.08f:1.0f);
    if(v->f0<=0) v->f0=tgtF0;
    for(i=0;i<frames;i++){
        float out, x, amp, lpHz, c, k=v->rev01;
        double inc, pos; int j;
        v->level+=(want-v->level)*0.0015f;
        v->f0+=(tgtF0-v->f0)*0.0012f;   /* ~20 ms: the game's revs step a frame at a time */
        v->load+=((v->blip>0?1.0f:v->loadT)-v->load)*0.0004f;
        if(v->cut>0) v->cut-=dt; else if(v->bark>0) v->bark-=dt;
        if(v->blip>0) v->blip-=dt; if(v->popT>0) v->popT-=dt;
        /* the loop, one cycle after another, each a little different */
        inc=(double)v->f0*v->cycPitch*dt; v->ph+=inc;
        if(v->ph>=1.0){ v->ph-=1.0; if(v->ph>=1.0) v->ph=0; v->cycAmp=1.0f+p->rough*EngRand(v); v->cycPitch=1.0f+p->rough*0.25f*EngRand(v);
            if(v->cut<=0 && (v->load<0.35f || v->popT>0) && k>0.3f && EngRand(v)>(v->popT>0?0.3f:0.75f)) v->pop=1.0f; }   /* an overrun pop */
        pos=v->ph*ENG_TAB; j=(int)pos; if(j>=ENG_TAB) j=ENG_TAB-1;
        x=wave[j]+(wave[j+1]-wave[j])*(float)(pos-j);
        amp=v->cycAmp*(0.55f+0.45f*v->load);
        if(v->cut>0) amp*=0.15f;
        else if(v->bark>0) amp*=1.25f;
        if(k>0.985f && ((int)(v->thPh*18.0)&1)) amp*=0.25f;   /* the rev limiter */
        v->thPh+=dt;
        x*=amp;
        /* a breath of exhaust noise, louder on the throttle; a pop */
        {   float n=EngRand(v); v->nlp+=(n-v->nlp)*0.2f;
            x+=v->nlp*p->breath*(0.4f+0.6f*v->load)*(0.6f+0.4f*fabsf(x));
            if(v->pop>0.01f){ x+=n*v->pop*0.9f; v->pop*=0.9985f; } }
        /* darker off the throttle and low in the revs */
        lpHz=2500.0f+(p->bright-2500.0f)*(0.35f+0.65f*v->load)*(0.6f+0.4f*(k<1?k:1));
        c=1.0f-(float)exp(-6.2831853*lpHz/OUT_RATE); v->lp+=(x-v->lp)*c; v->lp2+=(v->lp-v->lp2)*c;
        out=v->lp2;
        /* the gearbox's clunk: a low thump and a metallic click */
        if(v->clunk>0.001f){ out+=(float)sin(v->thPh*6.2831853*68.0)*v->clunk*0.35f; v->clunk*=0.99935f; }
        if(v->click>0.001f){ out+=EngRes(v->cl,EngRand(v),2300.0f,6.0f)*v->click*0.5f; v->click*=0.9965f; }
        /* gain, a soft limit only above full scale, DC out */
        out*=p->gain*0.9f; if(out>1.0f) out=2.0f-1.0f/out; else if(out<-1.0f) out=-2.0f-1.0f/out;
        v->dc+=(out-v->dc)*0.0005f; out-=v->dc;
        /* a tunnel: two echoes off the walls */
        { int e1=(v->echoPos-1900+ENG_ECHO)%ENG_ECHO, e2=(v->echoPos-3300+ENG_ECHO)%ENG_ECHO; float wet=tunnel*(v->echo[e1]*0.42f+v->echo[e2]*0.26f);
          v->echo[v->echoPos]=out+wet*0.35f; v->echoPos=(v->echoPos+1)%ENG_ECHO; out+=wet; }
        out*=v->level;
        acc[i*2]+=out*gl; acc[i*2+1]+=out*gr; } }

static void EngMix(float *acc,int frames){ int i; for(i=0;i<ENG_VOICES;i++) EngVoiceMix(&g_eng[i],acc,frames); }
