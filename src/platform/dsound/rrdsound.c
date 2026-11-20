/* rrdsound: minimal DirectSound 1 implementation for Road Rash with a software mixer, output through SDL3 audio (or a WAV file for tests).
 * Replaces dsound.dll.  The game uses (see analysis\SDL_PLAN.md): IDirectSound { CreateSoundBuffer GetCaps SetCooperativeLevel Release }
 * IDirectSoundBuffer { Lock Unlock Play Stop SetCurrentPosition SetFormat SetVolume SetPan SetFrequency Release } (+ harmless defaults for the rest).
 * Environment:  RR_AUDIO=off          no output device (still mixes: use with RR_AUDIO_DUMP)
 *               RR_AUDIO_DUMP=<wav>   write everything that is mixed to a 44.1 kHz 16-bit stereo WAV (also used by headless tests); <wav>.start = the wall-clock
 *                                     time of its first sample (Unix seconds), for muxing with a screen recording
 *               RR_HEADLESS=1         implies RR_AUDIO=off (RR_AUDIO=on overrides: the device in a headless run)
 *               RR_LOG=<file>         call log (shared name with rrddraw; opened in append mode)
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <mmsystem.h>
#include <dsound.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <SDL3/SDL.h>

#define OUT_RATE 44100
#define BLOCK    441                   /* 10 ms */
typedef struct Buf { IDirectSoundBuffer itf; LONG ref; int primary; BYTE *data; DWORD bytes; WAVEFORMATEX fmt; int playing, looping; double pos; /* in source frames */
                     LONG vol, pan; DWORD freq; struct Buf *next; } Buf;
typedef struct DS  { IDirectSound itf; LONG ref; } DS;

static IDirectSoundVtbl dsv; static IDirectSoundBufferVtbl bv;
static CRITICAL_SECTION g_cs; static Buf *g_bufs; static int g_started;
static SDL_AudioStream *g_dev; static FILE *g_wav; static DWORD g_wavBytes; static int g_nodev;
static FILE *g_log;
#define LOG(...) do{ if(g_log){ fprintf(g_log,"[%lu] dsound: ",GetTickCount()); fprintf(g_log,__VA_ARGS__); fputc(10,g_log); fflush(g_log);} }while(0)

/* ---------- mixer ---------- */
static double Gain(LONG centibel){ if(centibel<=-10000) return 0.0; if(centibel>=0) return 1.0; return pow(10.0,centibel/2000.0); }
/* the music stream (RR_MusicPlay: songs, video soundtracks) follows Restroom > Sound Mix: Master x Game music, silent when either is muted.  The game hands over
   its channel table (src\audio\soundmix.c: 7 x 24 bytes, volume 0..100 at +16, muted byte at +20; 0 = Master, 4 = Game music) */
static Buf *g_music; static const BYTE *volatile g_mixTab;
__declspec(dllexport) void __stdcall RR_SetMixTable(const void *tab){ g_mixTab=(const BYTE*)tab; }
static double MusicGain(void)
{   const BYTE *t=g_mixTab; int mv, gv; if(!t) return 1.0;
    if(t[0*24+20] || t[4*24+20]) return 0.0;
    mv=*(const int*)(t+0*24+16); gv=*(const int*)(t+4*24+16); if(mv<0) mv=0; if(mv>100) mv=100; if(gv<0) gv=0; if(gv>100) gv=100;
    return (mv/100.0)*(gv/100.0); }
/* weather ambience of True 3D races (the ddraw shim's weather.inc): RR_Ambience(kind 0 none / 1 rain / 2 snow, volume 0..1) and RR_Thunder(strength) after a
 * lightning flash.  Synthesised here (no sound files): rain = band-limited hiss + random drop ticks, snow = gusting wind (filtered brown noise), thunder =
 * a long low rumble.  Faded in / out; scaled by the Sound Mix master volume.  Nothing plays unless the shim asks (Pseudo 3D, tests: never). */
static volatile int g_ambKind; static volatile float g_ambVol, g_ambThunder;
__declspec(dllexport) void __stdcall RR_Ambience(int kind,float vol){ g_ambKind=kind; g_ambVol=vol<0?0:(vol>1?1:vol); }
__declspec(dllexport) void __stdcall RR_Thunder(float strength){ if(strength>g_ambThunder) g_ambThunder=strength; }
static volatile float g_ambSplash, g_ambSplashPan=0.5f;   /* a tyre through a puddle (RR_Splash: strength 0..1, pan 0 left .. 1 right) */
__declspec(dllexport) void __stdcall RR_Splash(float strength,float pan){ if(strength>g_ambSplash){ g_ambSplash=strength>1?1:strength; g_ambSplashPan=pan<0?0:(pan>1?1:pan); } }
/* the player's tyres on cobbles (user, 2026-11-18: "when the bike rides over cobble stone it makes a cobble road noise"): RR_Cobble(speed 0..1) every frame it
   rides on them (road\course_edit.c rr_CourseEditCobble) - a rattle of thuds and clacks, one a stone, faster and louder with speed, over a low rumble; it fades
   out when the calls stop (~0.15 s) */
static volatile float g_cobSpd; static volatile DWORD g_cobT;
__declspec(dllexport) void __stdcall RR_Cobble(float speed){ g_cobSpd=speed<0?0:(speed>1?1:speed); g_cobT=GetTickCount(); }
/* a tram's bell (the ddraw shim's world3d.inc, a tram near the player - user, 2026-11-20 "when its near the player it should make a ding ding noise"): RR_Ding(volume
   0..1, pan 0 left .. 1 right) - two strikes 0.32 s apart, each a struck bell: inharmonic partials of ~1.1 kHz, the higher ones dying faster */
static volatile float g_dingVol, g_dingPan=0.5f; static volatile LONG g_dingReq;
__declspec(dllexport) void __stdcall RR_Ding(float vol,float pan){ g_dingVol=vol<0?0:(vol>1?1:vol); g_dingPan=pan<0?0:(pan>1?1:pan); InterlockedExchange(&g_dingReq,1); }
static double MasterGain(void)
{   const BYTE *t=g_mixTab; int gv; if(!t) return 1.0; if(t[4*24+20]) return 0.0; gv=*(const int*)(t+4*24+16); if(gv<0) gv=0; if(gv>100) gv=100; return gv/100.0; }
static void AmbMix(float *acc,int frames)
{   static unsigned s=12345; static float lvl[3], lpL, lpR, lp2L, lp2R, drop, dropPan, br[2], bl[2], gust, gustT, th, thLp, thLp2, thEnv; static int curKind;
    float want[3]; double mg=MasterGain(); int i, k; static float spEnv, spPan, spA, spB, spC;
    want[0]=0; want[1]=g_ambKind==1?g_ambVol:0; want[2]=g_ambKind==2?g_ambVol:0; (void)curKind;
    if(g_ambThunder>0){ thEnv+=g_ambThunder; if(thEnv>1.2f) thEnv=1.2f; g_ambThunder=0; }
    if(g_ambSplash>0){ if(g_ambSplash>spEnv) spEnv=g_ambSplash; spPan=g_ambSplashPan; g_ambSplash=0; }
    {   static float cbL, cbW, cbPh, cbTh, cbCl, cbA, cbB, cbR, cbR2; float tgt=(GetTickCount()-g_cobT<150)?g_cobSpd:0;   /* (cobbles: RR_Cobble) */
        if(tgt>0.01f || cbL>0.0005f){ double cg=MasterGain();
            for(i=0;i<frames;i++){ float w, v; cbL+=(tgt-cbL)*0.0005f;
                cbPh+=(7.0f+38.0f*cbL)/OUT_RATE;   /* stones a second */
                s=s*1664525u+1013904223u; w=((s>>9)&0xffff)/32768.0f-1;
                if(cbPh>=1){ cbPh-=1+(((s>>5)&255)/255.0f-0.5f)*0.35f; cbTh=0.7f+((s>>13)&255)/600.0f; cbCl=0.4f+((s>>21)&255)/500.0f; }   /* the next stone: a thud, a clack */
                cbA+=(w-cbA)*0.012f; cbB+=(cbA-cbB)*0.03f;   /* low (thud) */
                cbW+=(w-cbW)*0.45f;                            /* high-ish (clack) */
                cbR=cbR*0.9985f+w*0.02f; cbR2+=(cbR-cbR2)*0.01f; /* rumble */
                v=((cbA-cbB)*cbTh*3.0f+(w-cbW)*cbCl*0.35f+cbR2*1.4f)*cbL*0.9f;
                cbTh*=0.9975f; cbCl*=0.992f;
                acc[i*2]+=(float)(v*cg); acc[i*2+1]+=(float)(v*cg); } } }
    {   static double ph[4], env[4], t2; static int left; static float dv, dp; static const double fr[4]={ 1120.0, 2980.0, 5540.0, 8410.0 }, dk[4]={ 1.6, 2.6, 4.6, 7.5 }, am[4]={ 0.55, 0.30, 0.16, 0.08 };   /* (the tram's bell: RR_Ding) */
        if(InterlockedExchange(&g_dingReq,0)){ int q; dv=g_dingVol; dp=g_dingPan; for(q=0;q<4;q++) env[q]=am[q]; left=1; t2=0; }
        if(env[0]>0.0005){ double cg=MasterGain();
            for(i=0;i<frames;i++){ double v=0; int q;
                if(left){ t2+=1.0/OUT_RATE; if(t2>=0.32){ for(q=0;q<4;q++) env[q]+=am[q]; left=0; } }   /* (the second strike) */
                for(q=0;q<4;q++){ ph[q]+=fr[q]/OUT_RATE; if(ph[q]>=1) ph[q]-=1; v+=sin(ph[q]*6.283185307)*env[q]; env[q]*=1.0-dk[q]/OUT_RATE; }
                v*=dv*0.45*cg; acc[i*2]+=(float)(v*(1.2-dp)); acc[i*2+1]+=(float)(v*(0.2+dp)); } } }
    if(lvl[1]<0.0005f && lvl[2]<0.0005f && want[1]==0 && want[2]==0 && thEnv<0.001f && spEnv<0.001f) return;
    for(i=0;i<frames;i++){ float wl, wr, l=0, r=0;
        for(k=1;k<3;k++) lvl[k]+=(want[k]-lvl[k])*0.00004f;   /* ~0.5 s fades */
        s=s*1664525u+1013904223u; wl=((s>>9)&0xffff)/32768.0f-1; s=s*1664525u+1013904223u; wr=((s>>9)&0xffff)/32768.0f-1;
        if(lvl[1]>0.0005f){   /* rain: hiss between ~300 Hz and ~5 kHz, plus drops */
            lpL+=(wl-lpL)*0.5f; lpR+=(wr-lpR)*0.5f; lp2L+=(lpL-lp2L)*0.04f; lp2R+=(lpR-lp2R)*0.04f;
            if(((s>>7)&4095)<3){ drop=0.5f+((s>>3)&255)/400.0f; dropPan=((s>>13)&255)/255.0f; }
            drop*=0.9965f;
            l+=lvl[1]*((lpL-lp2L)*0.32f+wl*drop*0.10f*(1-dropPan)); r+=lvl[1]*((lpR-lp2R)*0.32f+wr*drop*0.10f*dropPan); }
        if(lvl[2]>0.0005f){   /* snow: wind - brown noise through a soft band, slow gusts */
            if((i&255)==0){ gustT+=256.0f/OUT_RATE; gust=0.55f+0.45f*(float)sin(gustT*0.7)*(float)sin(gustT*0.23+1.3); }
            br[0]=br[0]*0.998f+wl*0.02f; br[1]=br[1]*0.998f+wr*0.02f; bl[0]+=(br[0]-bl[0])*0.08f; bl[1]+=(br[1]-bl[1])*0.08f;
            l+=lvl[2]*bl[0]*1.6f*gust; r+=lvl[2]*bl[1]*1.6f*gust; }
        if(thEnv>0.001f){   /* thunder: a low rumble that dies away over ~3 s */
            th=th*0.999f+wl*0.03f; thLp+=(th-thLp)*0.01f; thLp2+=(thLp-thLp2)*0.02f;
            l+=thLp2*thEnv*5.0f; r+=thLp2*thEnv*5.0f; thEnv*=0.99996f; }
        if(spEnv>0.001f){   /* a splash: noise in a ~0.6 .. 3 kHz band, quick swell, ~0.2 s tail, with a soft low slosh */
            spA+=(wl-spA)*0.35f; spB+=(spA-spB)*0.08f; spC+=(wr-spC)*0.004f;
            { float v=((spA-spB)*0.9f+spC*2.5f)*spEnv; l+=v*(1.2f-spPan); r+=v*(0.2f+spPan); } spEnv*=0.99988f; }
        acc[i*2]+=(float)(l*mg); acc[i*2+1]+=(float)(r*mg); } }
static void MixBlock(short *out,int frames)
{   static float acc[BLOCK*2]; Buf *b; int i; memset(acc,0,sizeof(float)*frames*2);
    AmbMix(acc,frames);
    EnterCriticalSection(&g_cs);
    for(b=g_bufs;b;b=b->next){
        if(!b->playing||b->primary||!b->data||!b->bytes) continue;
        { int ch=b->fmt.nChannels?b->fmt.nChannels:1, bits=b->fmt.wBitsPerSample?b->fmt.wBitsPerSample:8, bps=bits/8, frameSize=ch*bps; DWORD total=b->bytes/frameSize;
          double step=(double)(b->freq?b->freq:b->fmt.nSamplesPerSec)/OUT_RATE; double gl=Gain(b->vol), gr=gl;
          if(b->pan>0) gl*=Gain(-b->pan); else if(b->pan<0) gr*=Gain(b->pan);
          if(b==g_music){ double mg=MusicGain(); gl*=mg; gr*=mg; }
          if(!total) continue;
          for(i=0;i<frames;i++){ DWORD idx=(DWORD)b->pos; float l,r; const BYTE *p;
              if(idx>=total){ if(b->looping){ b->pos-=total; idx=(DWORD)b->pos; } else { b->playing=0; b->pos=0; break; } }   /* a finished one-shot buffer returns to its start (DirectSound semantics): the game re-triggers effects with a plain Play() */
              p=b->data+(size_t)idx*frameSize;
              if(bits==16){ l=((short*)p)[0]/32768.0f; r=ch>1?((short*)p)[1]/32768.0f:l; } else { l=(p[0]-128)/128.0f; r=ch>1?(p[1]-128)/128.0f:l; }
              acc[i*2]+=(float)(l*gl); acc[i*2+1]+=(float)(r*gr); b->pos+=step; } } }
    LeaveCriticalSection(&g_cs);
    for(i=0;i<frames*2;i++){ float v=acc[i]; if(v>1.0f) v=1.0f; if(v<-1.0f) v=-1.0f; out[i]=(short)(v*32767.0f); }
}
static void WriteWavHeader(void){ DWORD d=g_wavBytes, riff=36+d, fs=16, rate=OUT_RATE, br=OUT_RATE*4; WORD fmt=1,ch=2,ba=4,bp=16; if(!g_wav) return; fseek(g_wav,0,SEEK_SET);
    fwrite("RIFF",1,4,g_wav); fwrite(&riff,4,1,g_wav); fwrite("WAVEfmt ",1,8,g_wav); fwrite(&fs,4,1,g_wav); fwrite(&fmt,2,1,g_wav); fwrite(&ch,2,1,g_wav); fwrite(&rate,4,1,g_wav);
    fwrite(&br,4,1,g_wav); fwrite(&ba,2,1,g_wav); fwrite(&bp,2,1,g_wav); fwrite("data",1,4,g_wav); fwrite(&d,4,1,g_wav); fseek(g_wav,0,SEEK_END); }
static volatile int g_frameMode;   /* set by RR_SndStep: the game thread drives the mixer one video frame at a time (deterministic tests) */
static DWORD WINAPI MixThread(LPVOID p)
{   short blk[BLOCK*2]; DWORD next=GetTickCount(); int n=0; (void)p;
    {   char *e=getenv("RR_AUDIO_DUMP"); char sp[MAX_PATH+8]; FILE *f; FILETIME ft;   /* <wav>.start: the wall-clock time of the WAV's first sample (Unix seconds), for muxing it with a */
        if(g_wav && e && strlen(e)<MAX_PATH){ GetSystemTimePreciseAsFileTime(&ft);                /* screen recording (analysis\record_road_rash.ps1); the blocks below keep a 10 ms wall-clock pace */
            sprintf(sp,"%s.start",e); if((f=fopen(sp,"w"))){ fprintf(f,"%.6f\n",((((unsigned long long)ft.dwHighDateTime<<32)|ft.dwLowDateTime)-116444736000000000ULL)/1e7); fclose(f); } } }
    for(;;){ Sleep(5); if(g_frameMode){ next=GetTickCount(); continue; } { DWORD now=GetTickCount(); while((int)(now-next)>=0){ next+=10;
            MixBlock(blk,BLOCK);
            {   /* test switch RR_SYNC_MARK=1 (see rrddraw.c Present): a 1 kHz beep in the first 100 ms of every even wall-clock second (this block's time:
                   the mixer keeps a 10 ms wall-clock pace from the WAV's first sample) */
                static int mark=-1; static unsigned long long t0; if(mark<0){ char *e=getenv("RR_SYNC_MARK"); FILETIME ft; mark=(e && *e=='1'); GetSystemTimePreciseAsFileTime(&ft); t0=(((unsigned long long)ft.dwHighDateTime<<32)|ft.dwLowDateTime)/10000ULL; }
                if(mark){ static unsigned long long nb; unsigned long long ms=(t0+nb*10ULL)%2000ULL; int i; nb++;
                    if(ms<100) for(i=0;i<BLOCK;i++){ short v=(short)(12000.0*sin(6.283185307*1000.0*((ms*44.1+i)/44100.0))); blk[i*2]=v; blk[i*2+1]=v; } } }
            if(g_wav){ fwrite(blk,4,BLOCK,g_wav); g_wavBytes+=BLOCK*4; if(++n%50==0){ WriteWavHeader(); fflush(g_wav); } }
            if(g_dev && SDL_GetAudioStreamQueued(g_dev)<OUT_RATE*4/5) SDL_PutAudioStreamData(g_dev,blk,BLOCK*4); } } } return 0; }
/* Deterministic mode: called by the ddraw shim once per presented frame while the frame-locked race runs (RR_DETERMINISTIC).  The mixer then advances exactly 1/60 s of audio
 * per call, synchronously on the game thread, so the playback position the game polls (GetStatus / GetCurrentPosition: countdown beeps, "sound finished" tests) no longer depends
 * on wall-clock time or thread scheduling. */
__declspec(dllexport) void __stdcall RR_SndRealtime(void){ g_frameMode=0; }   /* end of the frame-locked race: the mixer thread runs on the wall clock again (the end-of-race screens wait for sounds to finish) */
__declspec(dllexport) void __stdcall RR_SndStep(void)
{   static double acc; static short blk[BLOCK*2]; int n; static unsigned long calls; if(!g_started) return;
    if(!g_frameMode){ Buf *b; EnterCriticalSection(&g_cs); for(b=g_bufs;b;b=b->next){ if(b->primary) continue; b->pos=0; if(!b->looping) b->playing=0; } LeaveCriticalSection(&g_cs); g_frameMode=1; acc=0; }   /* deterministic start: whatever was playing at a wall-clock dependent position is rewound / stopped once */
    acc+=OUT_RATE/60.0; n=(int)acc; acc-=n; calls++;
    while(n>0){ int c=n>BLOCK?BLOCK:n; MixBlock(blk,c);
        if(g_wav){ fwrite(blk,4,c,g_wav); g_wavBytes+=c*4; if(calls%50==0){ WriteWavHeader(); fflush(g_wav); } }
        if(g_dev && SDL_GetAudioStreamQueued(g_dev)<OUT_RATE*4/5) SDL_PutAudioStreamData(g_dev,blk,c*4); n-=c; } }
static void StartMixer(void)
{   char *e; if(g_started) return; g_started=1; InitializeCriticalSection(&g_cs);
    g_nodev=((e=getenv("RR_AUDIO")) && !_stricmp(e,"off")) || ((e=getenv("RR_HEADLESS")) && (*e=='1' || *e=='2'));
    if((e=getenv("RR_AUDIO")) && !_stricmp(e,"on")) g_nodev=0;   /* RR_AUDIO=on: the sound device even in a headless run (a hidden-window recording test: analysis\rec\rec_obs_test.ps1) */
    if((e=getenv("RR_AUDIO_DUMP"))){ g_wav=fopen(e,"wb"); if(g_wav){ g_wavBytes=0; WriteWavHeader(); } }
    /* SDL3: an audio stream on the default playback device; the mixer pushes 16-bit stereo blocks into it (SDL converts to the device format) */
    if(!g_nodev){ SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES,"1024");
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)){ SDL_AudioSpec want; SDL_zero(want); want.freq=OUT_RATE; want.format=SDL_AUDIO_S16; want.channels=2;
            g_dev=SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&want,NULL,NULL); if(g_dev) SDL_ResumeAudioStreamDevice(g_dev);
            LOG("audio stream %p (%d Hz)%s%s",(void*)g_dev,OUT_RATE,g_dev?"":": ",g_dev?"":SDL_GetError()); }
        else LOG("SDL audio init: %s",SDL_GetError()); }
    CreateThread(NULL,0,MixThread,NULL,0,NULL); }

/* ---------- IDirectSoundBuffer ---------- */
static HRESULT __stdcall B_QI(IDirectSoundBuffer *t,REFIID i,LPVOID *o){ *o=NULL; return E_NOINTERFACE; }
static ULONG __stdcall B_AddRef(IDirectSoundBuffer *t){ return InterlockedIncrement(&((Buf*)t)->ref); }
static ULONG __stdcall B_Release(IDirectSoundBuffer *t){ Buf *b=(Buf*)t; LONG r=InterlockedDecrement(&b->ref);
    if(!r){ Buf **pp; EnterCriticalSection(&g_cs); for(pp=&g_bufs;*pp;pp=&(*pp)->next) if(*pp==b){ *pp=b->next; break; } LeaveCriticalSection(&g_cs); free(b->data); free(b); } return r; }
static HRESULT __stdcall B_GetCaps(IDirectSoundBuffer *t,LPDSBCAPS c){ Buf *b=(Buf*)t; c->dwFlags=DSBCAPS_CTRLVOLUME|DSBCAPS_CTRLPAN|DSBCAPS_CTRLFREQUENCY; c->dwBufferBytes=b->bytes; c->dwUnlockTransferRate=0; c->dwPlayCpuOverhead=0; return DS_OK; }
static HRESULT __stdcall B_GetCurrentPosition(IDirectSoundBuffer *t,LPDWORD pl,LPDWORD wr){ Buf *b=(Buf*)t; int fs=b->fmt.nBlockAlign?b->fmt.nBlockAlign:1; DWORD p=(DWORD)b->pos*fs; if(b->bytes && p>=b->bytes) p%=b->bytes; if(pl) *pl=p; if(wr) *wr=p; return DS_OK; }
static HRESULT __stdcall B_GetFormat(IDirectSoundBuffer *t,LPWAVEFORMATEX f,DWORD n,LPDWORD w){ Buf *b=(Buf*)t; if(f&&n>=sizeof(WAVEFORMATEX)) *f=b->fmt; if(w) *w=sizeof(WAVEFORMATEX); return DS_OK; }
static HRESULT __stdcall B_GetVolume(IDirectSoundBuffer *t,LPLONG v){ *v=((Buf*)t)->vol; return DS_OK; }
static HRESULT __stdcall B_GetPan(IDirectSoundBuffer *t,LPLONG v){ *v=((Buf*)t)->pan; return DS_OK; }
static HRESULT __stdcall B_GetFrequency(IDirectSoundBuffer *t,LPDWORD v){ *v=((Buf*)t)->freq; return DS_OK; }
static HRESULT __stdcall B_GetStatus(IDirectSoundBuffer *t,LPDWORD s){ Buf *b=(Buf*)t; *s=b->playing?(DSBSTATUS_PLAYING|(b->looping?DSBSTATUS_LOOPING:0)):0; return DS_OK; }
static HRESULT __stdcall B_Initialize(IDirectSoundBuffer *t,LPDIRECTSOUND d,LPCDSBUFFERDESC c){ return DSERR_ALREADYINITIALIZED; }
static HRESULT __stdcall B_Lock(IDirectSoundBuffer *t,DWORD off,DWORD n,LPVOID *p1,LPDWORD l1,LPVOID *p2,LPDWORD l2,DWORD fl)
{   Buf *b=(Buf*)t; if(!b->data||!b->bytes) return DSERR_INVALIDCALL; if(fl&DSBLOCK_ENTIREBUFFER){ off=0; n=b->bytes; } if(off>=b->bytes) off%=b->bytes; if(n>b->bytes) n=b->bytes;
    if(off+n<=b->bytes){ *p1=b->data+off; *l1=n; if(p2) *p2=NULL; if(l2) *l2=0; } else { *p1=b->data+off; *l1=b->bytes-off; if(p2) *p2=b->data; if(l2) *l2=n-*l1; } return DS_OK; }
static HRESULT __stdcall B_Play(IDirectSoundBuffer *t,DWORD r,DWORD pr,DWORD fl){ Buf *b=(Buf*)t; b->looping=(fl&DSBPLAY_LOOPING)!=0; b->playing=1; LOG("Play %p bytes=%lu loop=%d vol=%ld pan=%ld freq=%lu",(void*)b,b->bytes,b->looping,b->vol,b->pan,b->freq); return DS_OK; }
static HRESULT __stdcall B_SetCurrentPosition(IDirectSoundBuffer *t,DWORD p){ Buf *b=(Buf*)t; int fs=b->fmt.nBlockAlign?b->fmt.nBlockAlign:1; b->pos=(double)(p/fs); return DS_OK; }
static HRESULT __stdcall B_SetFormat(IDirectSoundBuffer *t,LPCWAVEFORMATEX f){ Buf *b=(Buf*)t; if(f){ b->fmt=*f; if(!b->freq) b->freq=f->nSamplesPerSec; LOG("SetFormat %p %lu Hz %u ch %u bit",(void*)b,f->nSamplesPerSec,f->nChannels,f->wBitsPerSample); } return DS_OK; }
static HRESULT __stdcall B_SetVolume(IDirectSoundBuffer *t,LONG v){ ((Buf*)t)->vol=v; return DS_OK; }
static HRESULT __stdcall B_SetPan(IDirectSoundBuffer *t,LONG v){ ((Buf*)t)->pan=v; return DS_OK; }
static HRESULT __stdcall B_SetFrequency(IDirectSoundBuffer *t,DWORD v){ Buf *b=(Buf*)t; b->freq=v?v:b->fmt.nSamplesPerSec; return DS_OK; }
static HRESULT __stdcall B_Stop(IDirectSoundBuffer *t){ ((Buf*)t)->playing=0; return DS_OK; }
static HRESULT __stdcall B_Unlock(IDirectSoundBuffer *t,LPVOID p1,DWORD l1,LPVOID p2,DWORD l2){ return DS_OK; }
static HRESULT __stdcall B_Restore(IDirectSoundBuffer *t){ return DS_OK; }

/* ---------- IDirectSound ---------- */
static HRESULT __stdcall D_QI(IDirectSound *t,REFIID i,LPVOID *o){ *o=NULL; return E_NOINTERFACE; }
static ULONG __stdcall D_AddRef(IDirectSound *t){ return InterlockedIncrement(&((DS*)t)->ref); }
static ULONG __stdcall D_Release(IDirectSound *t){ DS *d=(DS*)t; LONG r=InterlockedDecrement(&d->ref); if(!r) free(d); return r; }
static HRESULT __stdcall D_CreateSoundBuffer(IDirectSound *t,LPCDSBUFFERDESC desc,LPDIRECTSOUNDBUFFER *out,LPUNKNOWN u)
{   Buf *b=(Buf*)calloc(1,sizeof *b); *out=NULL; if(!b) return DSERR_OUTOFMEMORY; b->itf.lpVtbl=&bv; b->ref=1; b->primary=(desc->dwFlags&DSBCAPS_PRIMARYBUFFER)!=0;
    if(!b->primary){ if(!desc->lpwfxFormat||!desc->dwBufferBytes){ free(b); return DSERR_INVALIDPARAM; } b->fmt=*desc->lpwfxFormat; b->bytes=desc->dwBufferBytes; b->data=(BYTE*)calloc(1,b->bytes);
        if(!b->data){ free(b); return DSERR_OUTOFMEMORY; } if(b->fmt.wBitsPerSample==8) memset(b->data,128,b->bytes); b->freq=b->fmt.nSamplesPerSec; }
    EnterCriticalSection(&g_cs); b->next=g_bufs; g_bufs=b; LeaveCriticalSection(&g_cs);
    LOG("CreateSoundBuffer %p primary=%d flags=%lx bytes=%lu %lu Hz %u ch %u bit",(void*)b,b->primary,desc->dwFlags,desc->dwBufferBytes,b->fmt.nSamplesPerSec,b->fmt.nChannels,b->fmt.wBitsPerSample);
    *out=&b->itf; return DS_OK; }
static HRESULT __stdcall D_GetCaps(IDirectSound *t,LPDSCAPS c){ DWORD sz=c->dwSize; memset(c,0,sz); c->dwSize=sz; c->dwFlags=DSCAPS_PRIMARY16BIT|DSCAPS_PRIMARYSTEREO|DSCAPS_SECONDARY16BIT|DSCAPS_SECONDARYSTEREO|DSCAPS_SECONDARYMONO|DSCAPS_SECONDARY8BIT|DSCAPS_PRIMARY8BIT|DSCAPS_PRIMARYMONO;
    c->dwMinSecondarySampleRate=100; c->dwMaxSecondarySampleRate=200000; c->dwPrimaryBuffers=1; return DS_OK; }
static HRESULT __stdcall D_Dup(IDirectSound *t,LPDIRECTSOUNDBUFFER s,LPDIRECTSOUNDBUFFER *o){ *o=NULL; return DSERR_UNSUPPORTED; }
static HRESULT __stdcall D_SetCoop(IDirectSound *t,HWND h,DWORD l){ LOG("SetCooperativeLevel %lu",l); return DS_OK; }
static HRESULT __stdcall D_Compact(IDirectSound *t){ return DS_OK; }
static HRESULT __stdcall D_GetSpeaker(IDirectSound *t,LPDWORD c){ *c=0; return DS_OK; }
static HRESULT __stdcall D_SetSpeaker(IDirectSound *t,DWORD c){ return DS_OK; }
static HRESULT __stdcall D_Init(IDirectSound *t,LPCGUID g){ return DS_OK; }

HRESULT WINAPI DirectSoundCreate(LPCGUID guid,LPDIRECTSOUND *out,LPUNKNOWN outer)
{   static int built; DS *d; if(!built){ char *lg=getenv("RR_LOG"); if(lg && !g_log) g_log=fopen(lg,"a");
        dsv.QueryInterface=D_QI; dsv.AddRef=D_AddRef; dsv.Release=D_Release; dsv.CreateSoundBuffer=D_CreateSoundBuffer; dsv.GetCaps=D_GetCaps; dsv.DuplicateSoundBuffer=D_Dup; dsv.SetCooperativeLevel=D_SetCoop;
        dsv.Compact=D_Compact; dsv.GetSpeakerConfig=D_GetSpeaker; dsv.SetSpeakerConfig=D_SetSpeaker; dsv.Initialize=D_Init;
        bv.QueryInterface=B_QI; bv.AddRef=B_AddRef; bv.Release=B_Release; bv.GetCaps=B_GetCaps; bv.GetCurrentPosition=B_GetCurrentPosition; bv.GetFormat=B_GetFormat; bv.GetVolume=B_GetVolume; bv.GetPan=B_GetPan;
        bv.GetFrequency=B_GetFrequency; bv.GetStatus=B_GetStatus; bv.Initialize=B_Initialize; bv.Lock=B_Lock; bv.Play=B_Play; bv.SetCurrentPosition=B_SetCurrentPosition; bv.SetFormat=B_SetFormat; bv.SetVolume=B_SetVolume;
        bv.SetPan=B_SetPan; bv.SetFrequency=B_SetFrequency; bv.Stop=B_Stop; bv.Unlock=B_Unlock; bv.Restore=B_Restore; built=1; StartMixer(); LOG("DirectSoundCreate"); }
    d=(DS*)calloc(1,sizeof *d); if(!d) return DSERR_OUTOFMEMORY; d->itf.lpVtbl=&dsv; d->ref=1; *out=&d->itf; return DS_OK; }

/* ---------------- music replacement (WAV) ----------------
 * RR_MusicPlay(path, loop): decode a PCM .wav (8/16-bit, mono/stereo, any rate) fully into memory and play it through the mixer as one extra buffer
 * (replaces the previous music buffer).  RR_MusicStop(): stop it.  Exported so the game exe (src\game\music.c) can call them via GetProcAddress.
 * Returns 1 on success, 0 if the file cannot be read/decoded (the caller then falls back to the original MIDI path). */

/* ---------- Media Foundation decoder: MP3, AAC (.m4a/.mp4), FLAC, WMA ... (whatever Windows can decode) -> 16-bit stereo 44.1 kHz PCM ---------- */
static int DecodeMF(const char *path, BYTE **pcmOut, DWORD *bytesOut)
{
    static int mfOk; IMFSourceReader *rd=NULL; IMFMediaType *mt=NULL; wchar_t w[MAX_PATH]; BYTE *buf=NULL; size_t used=0, cap=0; HRESULT hr; int ok=0; const DWORD AUDIO=0xFFFFFFFDu, ALL=0xFFFFFFFEu;
    if(!mfOk){ CoInitializeEx(NULL,COINIT_MULTITHREADED); if(FAILED(MFStartup(MF_VERSION,MFSTARTUP_LITE))) return 0; mfOk=1; }
    MultiByteToWideChar(CP_ACP,0,path,-1,w,MAX_PATH);
    hr=MFCreateSourceReaderFromURL(w,NULL,&rd); if(FAILED(hr)){ LOG("MF: cannot open %s (0x%lx)",path,(unsigned long)hr); return 0; }
    if(FAILED(MFCreateMediaType(&mt))){ IMFSourceReader_Release(rd); return 0; }
    IMFMediaType_SetGUID(mt,&MF_MT_MAJOR_TYPE,&MFMediaType_Audio); IMFMediaType_SetGUID(mt,&MF_MT_SUBTYPE,&MFAudioFormat_PCM);
    IMFMediaType_SetUINT32(mt,&MF_MT_AUDIO_NUM_CHANNELS,2); IMFMediaType_SetUINT32(mt,&MF_MT_AUDIO_SAMPLES_PER_SECOND,44100); IMFMediaType_SetUINT32(mt,&MF_MT_AUDIO_BITS_PER_SAMPLE,16);
    IMFMediaType_SetUINT32(mt,&MF_MT_AUDIO_BLOCK_ALIGNMENT,4); IMFMediaType_SetUINT32(mt,&MF_MT_AUDIO_AVG_BYTES_PER_SECOND,44100*4);
    IMFSourceReader_SetStreamSelection(rd,ALL,FALSE); IMFSourceReader_SetStreamSelection(rd,AUDIO,TRUE);
    hr=IMFSourceReader_SetCurrentMediaType(rd,AUDIO,NULL,mt); IMFMediaType_Release(mt);
    if(FAILED(hr)){ LOG("MF: no PCM conversion for %s (0x%lx)",path,(unsigned long)hr); IMFSourceReader_Release(rd); return 0; }
    for(;;){ DWORD idx=0, fl=0; LONGLONG ts=0; IMFSample *s=NULL; IMFMediaBuffer *mb=NULL; BYTE *p=NULL; DWORD len=0;
        hr=IMFSourceReader_ReadSample(rd,AUDIO,0,&idx,&fl,&ts,&s); if(FAILED(hr)) break; if(fl&MF_SOURCE_READERF_ENDOFSTREAM){ if(s) IMFSample_Release(s); ok=1; break; }
        if(!s) continue;
        if(SUCCEEDED(IMFSample_ConvertToContiguousBuffer(s,&mb))){ if(SUCCEEDED(IMFMediaBuffer_Lock(mb,&p,NULL,&len))){
                if(used+len>cap){ size_t nc=cap?cap*2:(4u<<20); BYTE *nb; while(nc<used+len) nc*=2; if(nc>(400u<<20)){ IMFMediaBuffer_Unlock(mb); IMFMediaBuffer_Release(mb); IMFSample_Release(s); break; } nb=(BYTE*)realloc(buf,nc); if(!nb){ IMFMediaBuffer_Unlock(mb); IMFMediaBuffer_Release(mb); IMFSample_Release(s); break; } buf=nb; cap=nc; }
                memcpy(buf+used,p,len); used+=len; IMFMediaBuffer_Unlock(mb); }
            IMFMediaBuffer_Release(mb); }
        IMFSample_Release(s); }
    IMFSourceReader_Release(rd);
    if(!ok || !used){ free(buf); return 0; }
    *pcmOut=buf; *bytesOut=(DWORD)(used&~3u); LOG("MF: decoded %s -> %lu bytes (%.1f s)",path,(unsigned long)*bytesOut,*bytesOut/176400.0); return 1;
}
__declspec(dllexport) int __stdcall RR_MusicStop(void)
{   Buf *b=g_music; g_music=NULL; if(b){ EnterCriticalSection(&g_cs); b->playing=0; LeaveCriticalSection(&g_cs); B_Release(&b->itf); } return 1; }
static int RR_MusicPlayWav(const char *path,int loop)
{   FILE *f; unsigned char hdr[12]; WAVEFORMATEX fmt; BYTE *pcm=NULL; DWORD pcmBytes=0; int haveFmt=0; Buf *b;
    if(!g_started) StartMixer();
    f=fopen(path,"rb"); if(!f) return 0;
    if(fread(hdr,1,12,f)!=12 || memcmp(hdr,"RIFF",4) || memcmp(hdr+8,"WAVE",4)){ fclose(f); return 0; }
    memset(&fmt,0,sizeof fmt);
    for(;;){ unsigned char ck[8]; DWORD sz; if(fread(ck,1,8,f)!=8) break; sz=*(DWORD*)(ck+4);
        if(!memcmp(ck,"fmt ",4)){ unsigned char tmp[40]; DWORD n=sz<sizeof tmp?sz:(DWORD)sizeof tmp; if(fread(tmp,1,n,f)!=n) break; if(sz>n) fseek(f,sz-n,SEEK_CUR);
            if(*(WORD*)tmp!=1 && *(WORD*)tmp!=0xFFFE){ fclose(f); return 0; } fmt.wFormatTag=1; fmt.nChannels=*(WORD*)(tmp+2); fmt.nSamplesPerSec=*(DWORD*)(tmp+4); fmt.wBitsPerSample=*(WORD*)(tmp+14);
            fmt.nBlockAlign=(WORD)(fmt.nChannels*fmt.wBitsPerSample/8); haveFmt=1; }
        else if(!memcmp(ck,"data",4)){ if(!haveFmt || sz>400u*1024*1024){ fclose(f); return 0; } pcm=(BYTE*)malloc(sz); if(!pcm){ fclose(f); return 0; } pcmBytes=(DWORD)fread(pcm,1,sz,f); break; }
        else fseek(f,(sz+1)&~1u,SEEK_CUR); }
    fclose(f);
    if(!pcm || !haveFmt || !pcmBytes || (fmt.wBitsPerSample!=8 && fmt.wBitsPerSample!=16) || fmt.nChannels<1 || fmt.nChannels>2){ free(pcm); return 0; }
    b=(Buf*)calloc(1,sizeof *b); if(!b){ free(pcm); return 0; } b->itf.lpVtbl=&bv; b->ref=1; b->fmt=fmt; b->data=pcm; b->bytes=pcmBytes-(pcmBytes%fmt.nBlockAlign); b->freq=fmt.nSamplesPerSec; b->looping=loop!=0;
    RR_MusicStop();
    EnterCriticalSection(&g_cs); b->next=g_bufs; g_bufs=b; b->playing=1; LeaveCriticalSection(&g_cs); g_music=b;
    LOG("music: %s %lu Hz %u ch %u bit, %lu bytes, loop=%d",path,fmt.nSamplesPerSec,fmt.nChannels,fmt.wBitsPerSample,b->bytes,loop); return 1; }


/* RR_MusicPlay: .wav -> built-in PCM reader (falls back to Media Foundation if the wav is not plain PCM); any other extension (mp3, m4a, mp4, aac, flac, wma ...) -> Media Foundation.
 * RR_MUSIC_DECODER=mf forces Media Foundation for everything (used by the tests). */
__declspec(dllexport) int __stdcall RR_MusicPlay(const char *path,int loop)
{   const char *dot=strrchr(path,46); int isWav=dot && (!_stricmp(dot,".wav") || !_stricmp(dot,".rra")); char *force=getenv("RR_MUSIC_DECODER");   /* (.RRA: the game's songs, AUDIO\MUSIC - plain RIFF WAVE) */ BYTE *pcm=NULL; DWORD bytes=0; WAVEFORMATEX fmt; Buf *b;
    if(isWav && !(force && !_stricmp(force,"mf")) && RR_MusicPlayWav(path,loop)) return 1;
    if(!g_started) StartMixer();
    if(!DecodeMF(path,&pcm,&bytes)) return 0;
    memset(&fmt,0,sizeof fmt); fmt.wFormatTag=1; fmt.nChannels=2; fmt.nSamplesPerSec=44100; fmt.wBitsPerSample=16; fmt.nBlockAlign=4; fmt.nAvgBytesPerSec=44100*4;
    b=(Buf*)calloc(1,sizeof *b); if(!b){ free(pcm); return 0; } b->itf.lpVtbl=&bv; b->ref=1; b->fmt=fmt; b->data=pcm; b->bytes=bytes; b->freq=44100; b->looping=loop!=0;
    RR_MusicStop();
    EnterCriticalSection(&g_cs); b->next=g_bufs; g_bufs=b; b->playing=1; LeaveCriticalSection(&g_cs); g_music=b;
    LOG("music (MF): %s, %lu bytes, loop=%d",path,(unsigned long)bytes,loop); return 1; }
/* pause / resume of the WAV music (keeps the play position); both return 1 when a WAV music is loaded, 0 otherwise (caller then uses the MIDI path) */
__declspec(dllexport) int __stdcall RR_MusicPause(void){ Buf *b=g_music; if(!b) return 0; EnterCriticalSection(&g_cs); b->playing=0; LeaveCriticalSection(&g_cs); return 1; }
__declspec(dllexport) int __stdcall RR_MusicResume(void){ Buf *b=g_music; if(!b) return 0; EnterCriticalSection(&g_cs); b->playing=1; LeaveCriticalSection(&g_cs); return 1; }
/* 0 = no music or the (one-shot) music has finished (the mixer stops it and rewinds it to 0), 1 = paused part-way, 2 = playing (the song player, ddraw musicbox.inc) */
__declspec(dllexport) int __stdcall RR_MusicState(void){ Buf *b=g_music; int s; if(!b) return 0; EnterCriticalSection(&g_cs); s=b->playing?2:(b->pos>0?1:0); LeaveCriticalSection(&g_cs); return s; }




