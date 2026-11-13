/* "Custom Race" editor (included by rrddraw.c after modscfg.inc): the Game Mode page's Custom Race item (src\ui\custom_race.c) opens it.  A road seen from
 * above, a straight line at first, START at the bottom and FINISH at the top; it is bent with control points (a Catmull-Rom curve through them), and every
 * segment (the road between two points) has a ramp (uphill / downhill) and a type (smooth, rolling hills, jumps, chicanes):
 *   drag a point              bends the road                             drag from the road      adds a point there
 *   click the road            selects that segment (also: the profile)   right-click a point     removes it (START and FINISH stay)
 *   PgUp / PgDn, the buttons  the segment's ramp                         1 .. 4, the buttons     the segment's type
 *   wheel / + -               zoom (at the mouse)                        drag the ground         moves the view          Home: the whole map
 *   Tab / Shift+Tab, arrows   selects / moves a point (Shift: faster)    Insert / Delete         adds a point after the selected one / removes it
 *   S / the Scenery button    the track the race takes its scenery, traffic and riders from (level 1: City has no split road)
 *   Enter / Race!             races it            Esc / Back            back to the Game Mode page
 * Only the shape counts: the line is spread over the base track's level 1 course (CR_LEN pieces from piece 48, as the course files have it), its heading
 * along the way becomes the road's curve per piece (heading += curve / CR_L per piece, as the course map draws it), the ramps and types its slope per
 * piece (the game's hill byte: the height changes by it from piece to piece); the elevation profile under the map shows the heights.  The game writes
 * both into the course at the race start (custom_race.c rr_CustomCoursePatch).  A bend sharper than the game's road allows (|curve| > CR_CMAX) is spread
 * over the next pieces and shown red; so are heights the course data cannot hold (|height| > CR_HMAX).  Kept in custom_track.ini next to the game (not
 * in tests).
 * Tests: RR_CUSTOM_POINTS="x,y;x,y;..." (0..1, START first) the road, RR_CUSTOM_RAMPS / RR_CUSTOM_TYPES="a,b,..." per segment, RR_CUSTOM_TRACK=1..5;
 * RR_TEST_CUSTOMRACE=race: straight back with Race (+ RR_DUMP_DIR: customcurve.txt, "curve slope" per row);  =shot (+ RR_DUMP_DIR): the screen saved as
 * customrace.bmp, then Back. */
#define CR_MAXP 32
#define CR_SPS 24                                                   /* curve samples per span */
#define CR_MX 150
#define CR_MY 46
#define CR_MS 336                                                   /* the map: a square, frame pixels */
#define CR_CMAX 90
#define CR_L 1400.0                                                 /* road units per piece for the heading: the course map's scale (2 road widths / 1.4, gpuscene.inc RR_GpuCourse) - so the map shows the editor's road */
#define CR_START 48                                                 /* the race starts at piece 48 of every course */
#define CR_RMAX 10                                                  /* a segment's ramp: -CR_RMAX .. CR_RMAX steps of CR_RSTEP slope (the game's own tracks stay within +-127) */
#define CR_RSTEP 6
#define CR_HMAX 30000                                               /* the course keeps heights in 16 bits */
#define CR_NT 4
#define CR_ZMAX 16.0f
#define CR_PX 18
#define CR_PY 392
#define CR_PW 614
#define CR_PH 48                                                    /* the elevation profile, frame pixels */
static const int g_crLen[5] = { 1771, 1760, 1768, 1665, 1812 };    /* level 1 course length in pieces (analysis\fork_catalog.ps1 -Level 1) */
static const char *const g_crTrackN[5] = { "City", "Sierra Nevada", "Peninsula", "Napa Valley", "Pacific Highway" };
static const char *const g_crTypeN[CR_NT] = { "Smooth", "Rolling", "Jumps", "Chicanes" };
static const COLORREF g_crTypeC[CR_NT] = { RGB(95,95,105), RGB(60,150,75), RGB(215,135,40), RGB(150,95,205) };
static float g_crP[CR_MAXP][2]; static int g_crN0, g_crTrack=1, g_crLoaded;   /* the control points (u right, v down, 0..1), START = [0] */
static signed char g_crRamp[CR_MAXP], g_crType[CR_MAXP];                      /* per segment: span i = points i .. i+1 */
static float g_crS[CR_MAXP*CR_SPS+1][2]; static int g_crNS; static unsigned char g_crRed[CR_MAXP*CR_SPS];   /* the curve sampled; samples that bend too sharply */
static signed char *g_crCurve, *g_crSlope; static int *g_crH; static unsigned char *g_crSpan, *g_crHRed; static int g_crRows; static int g_crShot;   /* per row: curve, slope, height before it, segment, height held */
static float g_crZoom=1.0f, g_crVx=0.5f, g_crVy=0.5f;                         /* the view: zoom around map point Vx, Vy */

static void CrStraight(void){ g_crN0=2; g_crP[0][0]=0.5f; g_crP[0][1]=0.97f; g_crP[1][0]=0.5f; g_crP[1][1]=0.03f; memset(g_crRamp,0,sizeof g_crRamp); memset(g_crType,0,sizeof g_crType); }
static void CrParse(const char *e)
{   int n=0; while(e && *e && n<CR_MAXP){ float u, v; if(sscanf(e,"%f,%f",&u,&v)!=2) break; g_crP[n][0]=u<0?0:(u>1?1:u); g_crP[n][1]=v<0?0:(v>1?1:v); n++;
        while(*e && *e!=';') e++; if(*e==';') e++; }
    if(n>=2) g_crN0=n; }
static void CrParseInts(const char *e,signed char *dst,int lo,int hi)
{   int n=0; while(e && *e && n<CR_MAXP){ int v=atoi(e); dst[n++]=(signed char)(v<lo?lo:(v>hi?hi:v)); while(*e && *e!=',') e++; if(*e==',') e++; } }
static void CrLoad(void)
{   FILE *f; char line[1024]; const char *e;
    if(g_crLoaded) return; g_crLoaded=1; CrStraight();
    if((e=getenv("RR_CUSTOM_POINTS"))!=NULL){ CrParse(e); if((e=getenv("RR_CUSTOM_TRACK"))!=NULL && atoi(e)>=1 && atoi(e)<=5) g_crTrack=atoi(e);
        CrParseInts(getenv("RR_CUSTOM_RAMPS"),g_crRamp,-CR_RMAX,CR_RMAX); CrParseInts(getenv("RR_CUSTOM_TYPES"),g_crType,0,CR_NT-1); return; }
    if(!g_vidPersist || !(f=fopen("custom_track.ini","r"))) return;
    while(fgets(line,sizeof line,f)){ if(!strncmp(line,"track=",6)){ int t=atoi(line+6); if(t>=1 && t<=5) g_crTrack=t; } else if(!strncmp(line,"points=",7)) CrParse(line+7);
        else if(!strncmp(line,"ramps=",6)) CrParseInts(line+6,g_crRamp,-CR_RMAX,CR_RMAX); else if(!strncmp(line,"types=",6)) CrParseInts(line+6,g_crType,0,CR_NT-1); }
    fclose(f); }
static void CrSave(void)
{   FILE *f; int i; if(!g_vidPersist || !(f=fopen("custom_track.ini","w"))) return;
    fprintf(f,"# Road Rash custom race (Game Mode > Custom Race): the scenery track 1..5, the control points u,v (0..1, START first, FINISH last),\n"
              "# per segment the ramp (-%d .. %d) and the type (0 smooth, 1 rolling hills, 2 jumps, 3 chicanes)\ntrack=%d\npoints=",CR_RMAX,CR_RMAX,g_crTrack);
    for(i=0;i<g_crN0;i++) fprintf(f,"%s%.4f,%.4f",i?";":"",g_crP[i][0],g_crP[i][1]);
    fprintf(f,"\nramps="); for(i=0;i<g_crN0-1;i++) fprintf(f,"%s%d",i?",":"",g_crRamp[i]);
    fprintf(f,"\ntypes="); for(i=0;i<g_crN0-1;i++) fprintf(f,"%s%d",i?",":"",g_crType[i]);
    fprintf(f,"\n"); fclose(f); }

/* the curve through the points (Catmull-Rom, the end points doubled), CR_SPS samples per span */
static void CrSample(void)
{   int i, k; g_crNS=0;
    for(i=0;i<g_crN0-1;i++){ const float *p0=g_crP[i>0?i-1:0], *p1=g_crP[i], *p2=g_crP[i+1], *p3=g_crP[i+2<g_crN0?i+2:g_crN0-1];
        for(k=0;k<CR_SPS;k++){ float t=(float)k/CR_SPS, t2=t*t, t3=t2*t; int c;
            for(c=0;c<2;c++) g_crS[g_crNS][c]=0.5f*((2*p1[c])+(-p0[c]+p2[c])*t+(2*p0[c]-5*p1[c]+4*p2[c]-p3[c])*t2+(-p0[c]+3*p1[c]-3*p2[c]+p3[c])*t3);
            g_crNS++; } }
    g_crS[g_crNS][0]=g_crP[g_crN0-1][0]; g_crS[g_crNS][1]=g_crP[g_crN0-1][1]; g_crNS++; }
/* the curve at parameter u (0 .. points - 1: span floor(u), t = the rest): point and direction (the Catmull-Rom polynomial and its derivative) */
static void CrAt(double u,double *x,double *y,double *dx,double *dy)
{   int i=(int)u, c; double t, t2; float v[2], dv[2];
    if(i>g_crN0-2) i=g_crN0-2; if(i<0) i=0; t=u-i; if(t<0) t=0; if(t>1) t=1; t2=t*t;
    {   const float *p0=g_crP[i>0?i-1:0], *p1=g_crP[i], *p2=g_crP[i+1], *p3=g_crP[i+2<g_crN0?i+2:g_crN0-1];
        for(c=0;c<2;c++){ double b=-p0[c]+p2[c], cc=2*p0[c]-5*p1[c]+4*p2[c]-p3[c], d=-p0[c]+3*p1[c]-3*p2[c]+p3[c];
            v[c]=(float)(0.5*(2*p1[c]+b*t+cc*t2+d*t2*t)); dv[c]=(float)(0.5*(b+2*cc*t+3*d*t2)); } }
    *x=v[0]; *y=v[1]; *dx=dv[0]; *dy=dv[1]; }
static void CrBox(double *v,double *tmp,int n,int r,int passes)   /* box-smooths v[1..n] (radius r, `passes` times) */
{   int pass, k; for(pass=0;pass<passes;pass++){ for(k=1;k<=n;k++){ int a=k-r<1?1:k-r, b=k+r>n?n:k+r, j; double sum=0; for(j=a;j<=b;j++) sum+=v[j]; tmp[k]=sum/(b-a+1); } for(k=1;k<=n;k++) v[k]=tmp[k]; } }
/* the road per row (0 = up the map, + = to the right), smooth: the curve's own direction at every piece (its derivative, not the drawn line's straight
   pieces - those turned the road in steps), the arc length measured finely, the turn per piece lightly smoothed (two 9-piece box passes), and the
   rounding to the game's whole curve values carried over to the next piece; chicanes add a weave.  The slope: each segment's ramp, its changes eased
   over ~60 pieces, plus rolling hills / jumps, faded in and out at the segment's ends.  Marks the samples where a bend had to be spread
   (|curve| > CR_CMAX) and the pieces where the height had to be held (|height| > CR_HMAX) */
#define CR_FINE 256
static void CrCurve(void)
{   int n=g_crLen[g_crTrack-1], rows=CR_START+n+64, spans, M, m, k, sp, h=0; double *cum, *turn, *tmp, *slope, total, px, py, dx, dy, acc=0, want=0, prev=0; const double L=CR_L;
    int sStart[CR_MAXP], sEnd[CR_MAXP];
    CrSample(); memset(g_crRed,0,sizeof g_crRed);
    free(g_crCurve); free(g_crSlope); free(g_crH); free(g_crSpan); free(g_crHRed);
    g_crCurve=(signed char*)calloc(rows,1); g_crSlope=(signed char*)calloc(rows,1); g_crH=(int*)calloc(rows+1,sizeof(int)); g_crSpan=(unsigned char*)calloc(rows,1); g_crHRed=(unsigned char*)calloc(rows,1);
    g_crRows=(g_crCurve && g_crSlope && g_crH && g_crSpan && g_crHRed)?rows:0;
    spans=g_crN0-1; M=spans*CR_FINE;
    cum=(double*)malloc(sizeof(double)*(M+1)); turn=(double*)malloc(sizeof(double)*(n+1)); tmp=(double*)malloc(sizeof(double)*(n+1)); slope=(double*)malloc(sizeof(double)*(n+1));
    if(!g_crRows || !cum || !turn || !tmp || !slope || spans<1){ free(cum); free(turn); free(tmp); free(slope); g_crRows=0; return; }
    CrAt(0,&px,&py,&dx,&dy); cum[0]=0;
    for(m=1;m<=M;m++){ double x, y; CrAt((double)m/CR_FINE,&x,&y,&dx,&dy); cum[m]=cum[m-1]+hypot(x-px,y-py); px=x; py=y; }
    total=cum[M]; if(total<=1e-9){ free(cum); free(turn); free(tmp); free(slope); g_crRows=0; return; }
    for(sp=0;sp<spans;sp++){ sStart[sp]=n+1; sEnd[sp]=-1; }
    for(k=0,m=0;k<=n;k++){ double s=total*k/n, f, u, th, d;
        while(m<M-1 && cum[m+1]<s) m++;
        f=cum[m+1]>cum[m]?(s-cum[m])/(cum[m+1]-cum[m]):0; u=(m+f)/CR_FINE;
        sp=(int)u; if(sp>spans-1) sp=spans-1; if(sp<0) sp=0;
        if(k>0){ g_crSpan[CR_START+k]=(unsigned char)sp; if(k<sStart[sp]) sStart[sp]=k; if(k>sEnd[sp]) sEnd[sp]=k; }
        CrAt(u,&px,&py,&dx,&dy);
        th=(dx*dx+dy*dy)>1e-12?atan2(dx,-dy):prev;   /* (the direction where the curve stands still: the last one) */
        if(k==0){ prev=th; continue; }
        d=th-prev; while(d>3.14159265358979) d-=6.28318530717959; while(d<-3.14159265358979) d+=6.28318530717959;   /* (unwrapped) */
        prev=th; turn[k]=d; slope[k]=g_crRamp[sp]*CR_RSTEP; }
    turn[0]=0; slope[0]=0;
    CrBox(turn,tmp,n,4,2); CrBox(slope,tmp,n,15,2);
    for(k=1;k<=n;k++){ int c, r=CR_START+k, a, w; double fade, ph; sp=g_crSpan[r];
        a=sStart[sp]; w=sEnd[sp]-sStart[sp]+1; ph=k-a;   /* (the piece within its segment) */
        fade=ph<20?ph/20.0:1.0; if(sEnd[sp]-k<20) fade=fade<(sEnd[sp]-k)/20.0?fade:(sEnd[sp]-k)/20.0; if(fade<0) fade=0; fade=fade*fade*(3-2*fade);
        want+=turn[k]*L; c=(int)floor(want-acc+0.5); acc+=c;
        if(g_crType[sp]==3) c+=(int)floor(26*sin(6.28318530717959*ph/56)*fade+0.5);   /* chicanes: a weave, left - right */
        if(c>CR_CMAX || c<-CR_CMAX){ c=c>0?CR_CMAX:-CR_CMAX; { int j=(int)((double)k/n*(g_crNS-1)); if(j>=0 && j<g_crNS) g_crRed[j]=1; } }
        g_crCurve[r]=(signed char)c;
        {   double v=slope[k]; int b;
            if(g_crType[sp]==1) v+=48*sin(6.28318530717959*ph/64)*fade;                          /* rolling hills */
            if(g_crType[sp]==2 && w>=90){ int cyc=(int)ph/90; if(a+(cyc+1)*90<=sEnd[sp]+1){ int q=(int)ph%90; v+=q<30?80:-40; } }   /* jumps: a 30-piece ramp up, then a crest (the slope flips at once: air) and down again */
            b=(int)floor(v+0.5); if(b>127) b=127; if(b<-127) b=-127;
            if(h+b>CR_HMAX){ b=CR_HMAX-h; g_crHRed[r]=1; } else if(h+b<-CR_HMAX){ b=-CR_HMAX-h; g_crHRed[r]=1; }
            g_crSlope[r]=(signed char)b; g_crH[r]=h; h+=b; } }
    for(k=CR_START+n+1;k<=rows;k++) g_crH[k]=h;
    free(cum); free(turn); free(tmp); free(slope); }

/* ---- the screen ---- */
enum { CR_BRACE, CR_BTRACK, CR_BRESET, CR_BBACK, CR_BRDN, CR_BRUP, CR_BT0, CR_BT1, CR_BT2, CR_BT3, CR_NB };
static const char *const g_crBtn[CR_NB] = { "Race!", "Scenery", "Straight line", "Back", "-", "+", "", "", "", "" };
typedef struct CrState { int sel, drag, hover, span, press, pan, pressX, pressY, mx, my; float panVx, panVy; DWORD msgT; char msg[96]; } CrState;
static void CrMsg(CrState *s,const char *t){ strncpy(s->msg,t,sizeof s->msg-1); s->msg[sizeof s->msg-1]=0; s->msgT=GetTickCount(); }
static void CrToFrame(const float *p,int *x,int *y)
{   *x=(int)floor(CR_MX+CR_MS*0.5+(p[0]-g_crVx)*CR_MS*g_crZoom+0.5); *y=(int)floor(CR_MY+CR_MS*0.5+(p[1]-g_crVy)*CR_MS*g_crZoom+0.5); }
static void CrFromFrame(int x,int y,float *u,float *v){ *u=g_crVx+(float)(x-(CR_MX+CR_MS*0.5))/(CR_MS*g_crZoom); *v=g_crVy+(float)(y-(CR_MY+CR_MS*0.5))/(CR_MS*g_crZoom); }
static int CrInMap(int x,int y){ return x>=CR_MX-8 && x<CR_MX+CR_MS+8 && y>=CR_MY-8 && y<CR_MY+CR_MS+8; }
static int CrInProfile(int x,int y){ return x>=CR_PX && x<CR_PX+CR_PW && y>=CR_PY && y<CR_PY+CR_PH; }
static void CrViewClamp(void)
{   float h; if(g_crZoom<1.0f) g_crZoom=1.0f; if(g_crZoom>CR_ZMAX) g_crZoom=CR_ZMAX; h=0.5f/g_crZoom;
    if(g_crVx<h) g_crVx=h; if(g_crVx>1-h) g_crVx=1-h; if(g_crVy<h) g_crVy=h; if(g_crVy>1-h) g_crVy=1-h; }
static void CrZoomAt(int x,int y,float f)   /* zoom by f keeping the map point under x, y in place */
{   float u, v; CrFromFrame(x,y,&u,&v); g_crZoom*=f; CrViewClamp();
    g_crVx=u-(float)(x-(CR_MX+CR_MS*0.5))/(CR_MS*g_crZoom); g_crVy=v-(float)(y-(CR_MY+CR_MS*0.5))/(CR_MS*g_crZoom); CrViewClamp(); }
static RECT CrBtnRect(int b)
{   static const int top[4]={ 60, 116, 160, 196 }; RECT r;
    if(b<4){ r.left=496; r.right=630; r.top=top[b]; r.bottom=r.top+28; return r; }   /* (room above the scenery button for its caption) */
    if(b==CR_BRDN || b==CR_BRUP){ r.left=b==CR_BRDN?496:596; r.right=r.left+34; r.top=268; r.bottom=r.top+26; return r; }
    b-=CR_BT0; r.left=(b&1)?565:496; r.right=r.left+65; r.top=(b&2)?336:304; r.bottom=r.top+28; return r; }
static int CrPointAt(int x,int y){ int i, px, py; if(!CrInMap(x,y)) return -1; for(i=g_crN0-1;i>=0;i--){ CrToFrame(g_crP[i],&px,&py); if((px-x)*(px-x)+(py-y)*(py-y)<=10*10) return i; } return -1; }
/* the curve near frame position x, y: its span (-1 = not near) */
static int CrCurveAt(int x,int y)
{   int k, best=-1; double bd=8.0*8.0; if(!CrInMap(x,y)) return -1;
    for(k=0;k+1<g_crNS;k++){ int ax, ay, bx, by; double dx, dy, t, ex, ey, d; CrToFrame(g_crS[k],&ax,&ay); CrToFrame(g_crS[k+1],&bx,&by);
        dx=bx-ax; dy=by-ay; t=(dx*dx+dy*dy)>0?((x-ax)*dx+(y-ay)*dy)/(dx*dx+dy*dy):0; if(t<0) t=0; if(t>1) t=1;
        ex=ax+t*dx-x; ey=ay+t*dy-y; d=ex*ex+ey*ey; if(d<bd){ bd=d; best=k/CR_SPS; } }
    if(best>g_crN0-2) best=g_crN0-2;
    return best; }
static int CrInsert(int span,float u,float v)   /* (the segment splits in two, both keep its ramp and type) */
{   int i; if(g_crN0>=CR_MAXP || span<0 || span>=g_crN0-1) return -1;
    for(i=g_crN0;i>span+1;i--){ g_crP[i][0]=g_crP[i-1][0]; g_crP[i][1]=g_crP[i-1][1]; }
    for(i=g_crN0-1;i>span;i--){ g_crRamp[i]=g_crRamp[i-1]; g_crType[i]=g_crType[i-1]; }
    g_crP[span+1][0]=u; g_crP[span+1][1]=v; g_crN0++; return span+1; }
static void CrDelete(CrState *s,int i)   /* (the two segments become one: the first one's ramp and type) */
{   int k; if(i<=0 || i>=g_crN0-1){ CrMsg(s,"START and FINISH stay - drag them instead"); return; }
    for(k=i;k<g_crN0-1;k++){ g_crP[k][0]=g_crP[k+1][0]; g_crP[k][1]=g_crP[k+1][1]; }
    for(k=i;k<g_crN0-2;k++){ g_crRamp[k]=g_crRamp[k+1]; g_crType[k]=g_crType[k+1]; }
    g_crN0--; if(s->sel>=g_crN0) s->sel=g_crN0-1; if(s->span>g_crN0-2) s->span=g_crN0-2; }
static void CrMove(int i,float du,float dv){ if(i<0 || i>=g_crN0) return; g_crP[i][0]+=du; g_crP[i][1]+=dv; if(g_crP[i][0]<0) g_crP[i][0]=0; if(g_crP[i][0]>1) g_crP[i][0]=1; if(g_crP[i][1]<0) g_crP[i][1]=0; if(g_crP[i][1]>1) g_crP[i][1]=1; }
static void CrRampText(char *t,int n,int r){ if(r==0) _snprintf(t,n,"Flat"); else _snprintf(t,n,"%s %d%%",r>0?"Up":"Down",(r>0?r:-r)*10); t[n-1]=0; }
static COLORREF CrLighter(COLORREF c){ return RGB((GetRValue(c)+255)/2,(GetGValue(c)+255)/2,(GetBValue(c)+255)/2); }

/* the elevation profile: the height along the race (START left, FINISH right), coloured by segment type, the selected segment lit, held heights red */
static void CrProfile(HDC dc,const CrState *s)
{   int n=g_crLen[g_crTrack-1], x, lo=0, hi=0, k, base;
    IcBox(dc,CR_PX-4,CR_PY-4,CR_PW+8,CR_PH+8,RGB(16,16,34),RGB(90,90,130),6);
    if(!g_crRows) return;
    for(k=CR_START;k<=CR_START+n;k++){ if(g_crH[k]<lo) lo=g_crH[k]; if(g_crH[k]>hi) hi=g_crH[k]; }
    if(hi-lo<2000){ int mid=(hi+lo)/2; lo=mid-1000; hi=mid+1000; }
    SelectObject(dc,GetStockObject(DC_PEN)); base=CR_PY+CR_PH-1;
    for(x=0;x<CR_PW;x++){ int r=CR_START+1+(int)((double)x*(n-1)/(CR_PW-1)), y, sp; COLORREF c;
        if(r>=g_crRows) r=g_crRows-1; sp=g_crSpan[r];
        y=base-(int)((double)(g_crH[r]-lo)*(CR_PH-6)/(hi-lo))-2;
        c=g_crHRed[r]?RGB(200,50,40):g_crTypeC[g_crType[sp]]; if(sp==s->span) c=CrLighter(c);
        SetDCPenColor(dc,c); MoveToEx(dc,CR_PX+x,base+1,NULL); LineTo(dc,CR_PX+x,y);
        SetDCPenColor(dc,sp==s->span?RGB(255,235,80):RGB(235,235,240)); MoveToEx(dc,CR_PX+x,y,NULL); LineTo(dc,CR_PX+x,y+2); } }
static int CrProfileSpan(int x){ int n=g_crLen[g_crTrack-1], r=CR_START+1+(int)((double)(x-CR_PX)*(n-1)/(CR_PW-1)); if(!g_crRows) return 0; if(r<0) r=0; if(r>=g_crRows) r=g_crRows-1; return g_crSpan[r]; }

static void CrDraw(Surf *tmp,const Surf *base,const CrState *s,DWORD now)
{   HDC dc; HFONT title,fRow,fSm,fMid,old; HPEN pen,op; HBRUSH br,ob; int i, k, x, y, wid; char t[128];
    CrCurve();
    memcpy(tmp->bits,base->bits,(size_t)tmp->pitch*tmp->h); SdDarken(tmp,0,0,tmp->w,tmp->h); SdDarken(tmp,8,8,632,472);
    S_GetDC(&tmp->itf,&dc); SetBkMode(dc,TRANSPARENT);
    title=CreateFontA(44,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,"Impact"); fRow=CreateFontA(20,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,"Impact");
    fSm=CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,"Arial"); fMid=CreateFontA(15,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,"Arial");
    old=(HFONT)SelectObject(dc,title); IcText(dc,18,0,"Custom Race",RGB(255,96,40),TA_LEFT|TA_TOP);
    IcBox(dc,CR_MX-8,CR_MY-8,CR_MS+16,CR_MS+16,RGB(20,34,20),RGB(70,110,70),6);   /* the ground */
    SaveDC(dc); IntersectClipRect(dc,CR_MX-6,CR_MY-6,CR_MX+CR_MS+6,CR_MY+CR_MS+6);
    if(g_crZoom>1.01f){   /* (a grid, so a zoomed view still shows where it is) */
        pen=CreatePen(PS_SOLID,1,RGB(32,52,32)); op=(HPEN)SelectObject(dc,pen);
        for(i=0;i<=10;i++){ float a[2], b[2]; int ax, ay, bx, by; a[0]=b[0]=i/10.0f; a[1]=0; b[1]=1; CrToFrame(a,&ax,&ay); CrToFrame(b,&bx,&by); MoveToEx(dc,ax,ay,NULL); LineTo(dc,bx,by);
            a[1]=b[1]=i/10.0f; a[0]=0; b[0]=1; CrToFrame(a,&ax,&ay); CrToFrame(b,&bx,&by); MoveToEx(dc,ax,ay,NULL); LineTo(dc,bx,by); }
        SelectObject(dc,op); DeleteObject(pen); }
    /* the road: the colour of its segment's type, the selected segment outlined; red where a bend is too sharp for the game */
    wid=(int)(9*(g_crZoom<3?g_crZoom:3));
    for(k=0;k+1<g_crNS;k++){ int ax, ay, bx, by; if(k/CR_SPS!=s->span) continue; CrToFrame(g_crS[k],&ax,&ay); CrToFrame(g_crS[k+1],&bx,&by);
        pen=CreatePen(PS_SOLID,wid+6,RGB(255,235,80)); op=(HPEN)SelectObject(dc,pen); MoveToEx(dc,ax,ay,NULL); LineTo(dc,bx,by); SelectObject(dc,op); DeleteObject(pen); }
    for(k=0;k+1<g_crNS;k++){ int ax, ay, bx, by, sp=k/CR_SPS; COLORREF c=g_crRed[k]?RGB(200,50,40):g_crTypeC[g_crType[sp]]; CrToFrame(g_crS[k],&ax,&ay); CrToFrame(g_crS[k+1],&bx,&by);
        pen=CreatePen(PS_SOLID,wid,c); op=(HPEN)SelectObject(dc,pen); MoveToEx(dc,ax,ay,NULL); LineTo(dc,bx,by); SelectObject(dc,op); DeleteObject(pen); }
    pen=CreatePen(PS_SOLID,1,RGB(235,235,235)); op=(HPEN)SelectObject(dc,pen);
    for(k=0;k+1<g_crNS;k++){ int ax, ay, bx, by; if((k/3)&1) continue; CrToFrame(g_crS[k],&ax,&ay); CrToFrame(g_crS[k+1],&bx,&by); MoveToEx(dc,ax,ay,NULL); LineTo(dc,bx,by); }
    SelectObject(dc,op); DeleteObject(pen);
    /* each segment's ramp next to its middle */
    SelectObject(dc,fSm);
    for(i=0;i<g_crN0-1;i++){ int j=i*CR_SPS+CR_SPS/2; if(!g_crRamp[i] || j>=g_crNS) continue; CrToFrame(g_crS[j],&x,&y); CrRampText(t,sizeof t,g_crRamp[i]);
        IcText(dc,x+wid/2+4,y-7,t,g_crRamp[i]>0?RGB(255,200,120):RGB(140,200,255),TA_LEFT|TA_TOP); }
    /* the points: START green, FINISH white, the others orange, the selected one ringed yellow */
    for(i=0;i<g_crN0;i++){ COLORREF c=i==0?RGB(60,200,80):(i==g_crN0-1?RGB(240,240,240):RGB(255,170,40)); int r=(i==s->sel)?8:6;
        CrToFrame(g_crP[i],&x,&y); br=CreateSolidBrush(c); ob=(HBRUSH)SelectObject(dc,br);
        pen=CreatePen(PS_SOLID,2,i==s->sel?RGB(255,255,80):RGB(20,20,20)); op=(HPEN)SelectObject(dc,pen); Ellipse(dc,x-r,y-r,x+r+1,y+r+1);
        SelectObject(dc,op); DeleteObject(pen); SelectObject(dc,ob); DeleteObject(br); }
    SelectObject(dc,fMid);
    CrToFrame(g_crP[0],&x,&y); IcText(dc,x+12,y-8,"START",RGB(120,230,130),TA_LEFT|TA_TOP);
    CrToFrame(g_crP[g_crN0-1],&x,&y); IcText(dc,x+12,y-8,"FINISH",RGB(240,240,240),TA_LEFT|TA_TOP);
    RestoreDC(dc,-1);
    SelectObject(dc,fSm); if(g_crZoom>1.01f){ _snprintf(t,sizeof t,"Zoom x%.1f  (Home: whole map)",g_crZoom); t[sizeof t-1]=0; IcText(dc,CR_MX,CR_MY-26,t,RGB(170,170,195),TA_LEFT|TA_TOP); }
    /* buttons */
    for(i=0;i<=CR_BBACK;i++){ RECT r=CrBtnRect(i); int on=(i==s->hover);
        IcBox(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,on?RGB(70,60,20):RGB(28,28,48),on?RGB(255,235,80):RGB(90,90,130),6);
        SelectObject(dc,i==CR_BRACE?fRow:fMid); if(i==CR_BTRACK) _snprintf(t,sizeof t,"%s",g_crTrackN[g_crTrack-1]); else _snprintf(t,sizeof t,"%s",g_crBtn[i]); t[sizeof t-1]=0;
        IcText(dc,(r.left+r.right)/2,r.top+(i==CR_BRACE?2:5),t,on?RGB(255,235,80):RGB(235,235,240),TA_CENTER|TA_TOP); }
    SelectObject(dc,fSm); IcText(dc,563,99,"Scenery (S):",RGB(170,170,195),TA_CENTER|TA_TOP);
    /* the selected segment: its ramp and its type */
    SelectObject(dc,fMid); _snprintf(t,sizeof t,"Segment %d of %d",s->span+1,g_crN0-1); t[sizeof t-1]=0; IcText(dc,563,234,t,RGB(255,235,80),TA_CENTER|TA_TOP);
    SelectObject(dc,fSm); IcText(dc,563,252,"Ramp (PgUp / PgDn):",RGB(170,170,195),TA_CENTER|TA_TOP);
    for(i=CR_BRDN;i<=CR_BRUP;i++){ RECT r=CrBtnRect(i); int on=(i==s->hover);
        IcBox(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,on?RGB(70,60,20):RGB(28,28,48),on?RGB(255,235,80):RGB(90,90,130),6);
        SelectObject(dc,fRow); IcText(dc,(r.left+r.right)/2,r.top+1,g_crBtn[i],on?RGB(255,235,80):RGB(235,235,240),TA_CENTER|TA_TOP); }
    SelectObject(dc,fMid); CrRampText(t,sizeof t,g_crRamp[s->span]); IcText(dc,563,273,t,g_crRamp[s->span]>0?RGB(255,200,120):(g_crRamp[s->span]<0?RGB(140,200,255):RGB(235,235,240)),TA_CENTER|TA_TOP);
    SelectObject(dc,fSm); IcText(dc,563,289,"Type (1 - 4):",RGB(170,170,195),TA_CENTER|TA_TOP);
    for(i=0;i<CR_NT;i++){ RECT r=CrBtnRect(CR_BT0+i); int on=(CR_BT0+i==s->hover), cur=(g_crType[s->span]==i);
        IcBox(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,cur||on?CrLighter(g_crTypeC[i]):g_crTypeC[i],cur?RGB(255,235,80):(on?RGB(235,235,240):RGB(20,20,30)),6);
        SelectObject(dc,fMid); IcText(dc,(r.left+r.right)/2,r.top+6,g_crTypeN[i],cur?RGB(20,20,20):RGB(235,235,240),TA_CENTER|TA_TOP); }
    /* how to */
    {   static const char *const how[] = { "Drag a point:", "  bend the road", "Drag from the road:", "  add a point", "Click the road:", "  pick a segment",
                                           "Right-click a point:", "  remove it", "Wheel, + -: zoom", "Drag the ground:", "  move the view", "Tab, arrows:", "  select, move",
                                           "Insert / Delete:", "  add / remove", "S: scenery", "Enter: race", "Esc: back" };
        SelectObject(dc,fSm); for(i=0;i<(int)(sizeof how/sizeof how[0]);i++) IcText(dc,18,52+i*18,how[i],how[i][0]==' '?RGB(170,170,195):RGB(225,225,240),TA_LEFT|TA_TOP); }
    CrProfile(dc,s);
    SelectObject(dc,fSm); IcText(dc,CR_PX+4,CR_PY-3,"Elevation",RGB(170,170,195),TA_LEFT|TA_TOP);
    if(s->msg[0] && now-s->msgT<3500){ SelectObject(dc,fMid); IcText(dc,18,446,s->msg,RGB(255,200,90),TA_LEFT|TA_TOP); }
    else { int red=0, hred=0; for(k=0;k+1<g_crNS;k++) red|=g_crRed[k]; for(k=0;k<g_crRows;k++) hred|=g_crHRed[k];
        if(red || hred) _snprintf(t,sizeof t,"Red: %s - %s.",red?"a bend sharper than the road allows":"higher or lower than the course can go",red?"the turn is spread out there":"the road levels off there");
        else _snprintf(t,sizeof t,"Level 1 race on the %s scenery: %d pieces of road from START to FINISH, %d segments.",g_crTrackN[g_crTrack-1],g_crLen[g_crTrack-1],g_crN0-1);
        t[sizeof t-1]=0; SelectObject(dc,fSm); IcText(dc,18,448,t,(red || hred)?RGB(255,140,120):RGB(225,225,240),TA_LEFT|TA_TOP); }
    SelectObject(dc,old); DeleteObject(title); DeleteObject(fRow); DeleteObject(fSm); DeleteObject(fMid); GdiFlush(); }

static int CrBtnAt(int x,int y){ int i; for(i=0;i<CR_NB;i++){ RECT r=CrBtnRect(i); if(x>=r.left && x<r.right && y>=r.top && y<r.bottom) return i; } return -1; }
static void CrRamp(CrState *s,int d){ int r=g_crRamp[s->span]+d; if(r>CR_RMAX) r=CR_RMAX; if(r<-CR_RMAX) r=-CR_RMAX; g_crRamp[s->span]=(signed char)r; }
/* a button: 1 = race, -1 = back, 0 = stay */
static int CrButton(CrState *s,int b,int dir)
{   switch(b){
    case CR_BRACE: return 1;
    case CR_BTRACK: g_crTrack=(g_crTrack-1+dir+5)%5+1; CrMsg(s,g_crTrack==1?"City: no split road at level 1":"This scenery has a split road: every rider takes the left one"); return 0;
    case CR_BRESET: CrStraight(); s->sel=0; s->span=0; CrMsg(s,"A straight road again"); return 0;
    case CR_BBACK: return -1;
    case CR_BRDN: CrRamp(s,-dir); return 0;
    case CR_BRUP: CrRamp(s,dir); return 0;
    default: if(b>=CR_BT0 && b<CR_BT0+CR_NT) g_crType[s->span]=(signed char)(b-CR_BT0); return 0; } }
static int CrKey(CrState *s,int vk)
{   float st=((GetKeyState(VK_SHIFT)&0x8000)?0.04f:0.01f)/g_crZoom;
    switch(vk){
    case VK_ESCAPE: return -1;
    case VK_RETURN: case VK_SPACE: return 1;
    case VK_TAB: s->sel=(GetKeyState(VK_SHIFT)&0x8000)?(s->sel+g_crN0-1)%g_crN0:(s->sel+1)%g_crN0; s->span=s->sel<g_crN0-1?s->sel:g_crN0-2; break;
    case VK_LEFT: CrMove(s->sel,-st,0); break; case VK_RIGHT: CrMove(s->sel,st,0); break;
    case VK_UP: CrMove(s->sel,0,-st); break; case VK_DOWN: CrMove(s->sel,0,st); break;
    case VK_DELETE: CrDelete(s,s->sel); break;
    case VK_INSERT: { int a=s->sel<g_crN0-1?s->sel:g_crN0-2, n; n=CrInsert(a,(g_crP[a][0]+g_crP[a+1][0])*0.5f,(g_crP[a][1]+g_crP[a+1][1])*0.5f); if(n>=0){ s->sel=n; s->span=n; } else CrMsg(s,"No more points"); break; }
    case VK_PRIOR: CrRamp(s,1); break; case VK_NEXT: CrRamp(s,-1); break;
    case '1': case '2': case '3': case '4': g_crType[s->span]=(signed char)(vk-'1'); break;
    case VK_ADD: case VK_OEM_PLUS: CrZoomAt(CR_MX+CR_MS/2,CR_MY+CR_MS/2,1.25f); break;
    case VK_SUBTRACT: case VK_OEM_MINUS: CrZoomAt(CR_MX+CR_MS/2,CR_MY+CR_MS/2,0.8f); break;
    case VK_HOME: g_crZoom=1.0f; g_crVx=g_crVy=0.5f; break;
    case 'S': return CrButton(s,CR_BTRACK,1), 0;
    case 'R': return CrButton(s,CR_BRESET,1), 0; }
    return 0; }

static void CrDump(void)   /* (test: the road per row) */
{   char p[MAX_PATH]; FILE *f; int r; if(!getenv("RR_DUMP_DIR")) return; _snprintf(p,sizeof p,"%s\\customcurve.txt",getenv("RR_DUMP_DIR")); p[sizeof p-1]=0;
    if((f=fopen(p,"w"))!=NULL){ for(r=0;r<g_crRows;r++) fprintf(f,"%d %d\n",g_crCurve[r],g_crSlope[r]); fclose(f); } }

/* Custom Race (src\ui\custom_race.c): the base track 1..5 to race (the road ready: RR_CustomCurve / RR_CustomSlope), 0 = back */
__declspec(dllexport) int __stdcall RR_CustomRaceEditor(void)
{   Surf *tmp,base; MSG m; CrState s; int done=0, dirty=1, res=0; Surf *prevModal=g_modalSurf; DWORD lastDraw=0; const char *test=getenv("RR_TEST_CUSTOMRACE");
    CrLoad();
    if(test && !_stricmp(test,"race")){ CrCurve(); g_crActive=1; CrDump(); LOG("customrace: test race, track %d, %d points",g_crTrack,g_crN0); return g_crTrack; }
    if(!g_primary || !(tmp=NewSurf(g_primary->w,g_primary->h,16))) return 0;
    base=*tmp; base.bits=(BYTE*)malloc((size_t)tmp->pitch*tmp->h); if(!base.bits){ FreeSurf(tmp); return 0; }
    { Surf *keep=g_modalSurf; g_modalSurf=NULL; SdBaseFrame(&base); g_modalSurf=keep; }
    g_modalSurf=tmp; memset(&s,0,sizeof s); s.drag=-1; s.hover=-1; s.mx=CR_MX+CR_MS/2; s.my=CR_MY+CR_MS/2; LOG("customrace: open");
    while(!done){
        while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){
            if(m.message==WM_QUIT){ PostQuitMessage((int)m.wParam); done=1; break; }
            if(m.message==WM_SYSKEYDOWN && m.wParam==VK_RETURN){ VidToggleFullscreen(); dirty=1; continue; }
            if(m.message==WM_KEYDOWN && m.wParam==VK_F11){ VidToggleFullscreen(); dirty=1; continue; }
            if(m.message==WM_KEYDOWN || m.message==WM_SYSKEYDOWN){ int r=CrKey(&s,(int)m.wParam); if(r){ res=r; done=1; } dirty=1; continue; }
            if(MSG_LDOWN(m)){ int x, y, i, b; MsgFramePos(&m,&x,&y);
                if((b=CrBtnAt(x,y))>=0){ int r=CrButton(&s,b,1); if(r){ res=r; done=1; } dirty=1; continue; }
                if(CrInProfile(x,y)){ s.span=CrProfileSpan(x); dirty=1; continue; }
                if((i=CrPointAt(x,y))>=0){ s.sel=i; s.drag=i; dirty=1; continue; }
                if((i=CrCurveAt(x,y))>=0){ s.span=i; s.press=1; s.pressX=x; s.pressY=y; dirty=1; continue; }   /* (a click selects the segment; dragging on adds a point) */
                if(CrInMap(x,y)){ s.pan=1; s.pressX=x; s.pressY=y; s.panVx=g_crVx; s.panVy=g_crVy; }
                continue; }
            if(MSG_RDOWN(m)){ int x, y, i, b; MsgFramePos(&m,&x,&y);
                if((b=CrBtnAt(x,y))==CR_BTRACK || b==CR_BRDN || b==CR_BRUP){ CrButton(&s,b,-1); dirty=1; continue; }
                if((i=CrPointAt(x,y))>=0){ CrDelete(&s,i); dirty=1; } continue; }
            if(m.message==WM_LBUTTONUP){ s.drag=-1; s.press=0; s.pan=0; dirty=1; continue; }
            if(m.message==WM_MOUSEWHEEL){ int d=(short)HIWORD(m.wParam); if(CrInMap(s.mx,s.my)){ CrZoomAt(s.mx,s.my,d>0?1.25f:0.8f); dirty=1; } continue; }
            if(m.message==WM_MOUSEMOVE){ int x, y, h; MsgFramePos(&m,&x,&y); s.mx=x; s.my=y;
                if(!(m.wParam&MK_LBUTTON)){ s.drag=-1; s.press=0; s.pan=0; }
                if(s.press && (abs(x-s.pressX)>4 || abs(y-s.pressY)>4)){ float u, v; int n; CrFromFrame(s.pressX,s.pressY,&u,&v); n=CrInsert(s.span,u,v); s.press=0;
                    if(n>=0){ s.sel=n; s.drag=n; } else CrMsg(&s,"No more points"); }
                if(s.drag>=0){ CrFromFrame(x,y,&g_crP[s.drag][0],&g_crP[s.drag][1]); CrMove(s.drag,0,0); dirty=1; }
                else if(s.pan){ g_crVx=s.panVx-(float)(x-s.pressX)/(CR_MS*g_crZoom); g_crVy=s.panVy-(float)(y-s.pressY)/(CR_MS*g_crZoom); CrViewClamp(); dirty=1; }
                h=CrBtnAt(x,y); if(h!=s.hover){ s.hover=h; dirty=1; } continue; }
            if(m.message==WM_KEYUP || m.message==WM_SYSKEYUP || m.message==WM_CHAR || m.message==WM_RBUTTONUP) continue;
            DispatchMessageA(&m); }
        if(GetTickCount()-lastDraw>250 || SgTickerUp()) dirty=1;
        if(dirty && !done){ CrDraw(tmp,&base,&s,GetTickCount()); Present(tmp); dirty=0; lastDraw=GetTickCount();
            if(test && !_stricmp(test,"shot") && getenv("RR_DUMP_DIR")){ char p[MAX_PATH]; if(g_dumpGpu[0] && !g_crShot){ g_crShot=1; g_gpuReq=904; dirty=1; continue; } _snprintf(p,sizeof p,"%s\\customrace.bmp",getenv("RR_DUMP_DIR")); p[sizeof p-1]=0; DumpBmp(tmp,p);
                res=-1; done=1; } }
        Sleep(15); }
    CrSave();
    g_modalSurf=prevModal; g_lastShown=g_primary; free(base.bits); FreeSurf(tmp);
    if(g_hwnd) InvalidateRect(g_hwnd,NULL,FALSE);
    if(g_primary && !prevModal) Present(g_primary);
    ModalEatClick(); LOG("customrace: closed (%s)",res>0?"race":"back");
    if(res>0){ CrCurve(); g_crActive=1; return g_crTrack; }
    return 0; }
/* the road per row for the game (src\ui\custom_race.c writes it into the course): the curve, and the slope (the height changes by it from row to row) */
__declspec(dllexport) const signed char *__stdcall RR_CustomCurve(int *n){ if(n) *n=g_crRows; return g_crCurve; }
__declspec(dllexport) const signed char *__stdcall RR_CustomSlope(int *n){ if(n) *n=g_crRows; return g_crSlope; }
/* the custom race is over (src\ui\custom_race.c) */
__declspec(dllexport) void __stdcall RR_CustomRaceDone(void){ g_crActive=0; LOG("customrace: race over"); }
