/* HD race dashboard (GPU races, with the widescreen art folder): the cockpit picture the game blits under the race view (Images\Bikes\<class>\<k|m>HiDash.BOB,
 * 640 x 119..121) is drawn from  <dir>\hd\hud\<CLASS>\<NAME>.png  instead (any size, stretched over the dashboard's place; made by analysis\hud_hd: AI-redrawn
 * at 4x, the gauge lettering drawn fresh).  Where the dashboard is: its BltFast from the panel surface (rr_IntroStripBlit) and, on a wide frame, RR_HudEnd's
 * shrink to its share of the width.  What the HUD draws over it (needles, bars, figures) stays on top: every dashboard pixel of the frame that is not the
 * dashboard's own colour there is drawn again over the HD picture.  The game and its frame are unchanged.  RR_HDHUD=0: off (comparisons). ---- */
static char g_hhWant[MAX_PATH], g_hhCur[MAX_PATH]; static BYTE *g_hhIdx, *g_hhIdxWant; static int g_hhH, g_hhHWant; static SRWLOCK g_hhLk=SRWLOCK_INIT;
static SDL_Texture *g_hhTex, *g_hhOv; static int g_hhOvW, g_hhOvH;
static RECT g_hhDst; static int g_hhSeen;   /* where the dashboard went in this frame (frame pixels); set by the blit, cleared at the scene's start */
static int HhOn(void){ static int env=-2; if(env==-2){ const char *e=getenv("RR_HDHUD"); env=(e && *e)?(*e!='0'):-1; } return env>=0?env:VidHdArt(); }
/* the game opened a file for reading: a dashboard -> its HD picture and its own pixels (to tell the HUD's from the dashboard's) */
static void HhNoteOpen(const char *rel)
{   char cls[64], nm[64], cand[MAX_PATH]; const char *b, *c; size_t n=strlen(rel); int i; DWORD at, got; HANDLE h; BYTE *px=NULL; int rows=0;
    if(n<12 || _stricmp(rel+n-10,"HiDash.BOB")) return;
    b=rel+n; while(b>rel && b[-1]!='/' && b[-1]!=0x5C) b--;                  /* <name>.BOB */
    c=b-1; if(c<=rel) return; while(c>rel && c[-1]!='/' && c[-1]!=0x5C) c--;   /* <class>\ */
    if(b-1-c<=0 || b-1-c>=(int)sizeof cls || strlen(b)-4>=sizeof nm) return;
    memcpy(cls,c,b-1-c); cls[b-1-c]=0; memcpy(nm,b,strlen(b)-4); nm[strlen(b)-4]=0; _strupr(cls); _strupr(nm);
    cand[0]=0;
    if(HhOn()){ if(g_mwNDir<0) MwDirs();
        for(i=0;i<g_mwNDir;i++){ char p[MAX_PATH]; _snprintf(p,sizeof p-1,"%s\\hd\\hud\\%s\\%s.png",g_mwDir[i],cls,nm); p[sizeof p-1]=0;
            at=GetFileAttributesA(p); if(at!=INVALID_FILE_ATTRIBUTES && !(at&FILE_ATTRIBUTE_DIRECTORY)){ strcpy(cand,p); break; } } }
    if(cand[0] && (h=g_realCreateFileA(rel,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL))!=INVALID_HANDLE_VALUE){   /* (the game's own file, not a mod's) */
        DWORD sz=GetFileSize(h,NULL); rows=(int)(sz/640);
        if(rows>0 && rows<=256 && (px=(BYTE*)malloc((size_t)rows*640))!=NULL && !(ReadFile(h,px,rows*640,&got,NULL) && got==(DWORD)rows*640)){ free(px); px=NULL; }
        CloseHandle(h); }
    AcquireSRWLockExclusive(&g_hhLk); free(g_hhIdxWant); g_hhIdxWant=px; g_hhHWant=px?rows:0; strcpy(g_hhWant,px?cand:""); ReleaseSRWLockExclusive(&g_hhLk); }
static RECT g_hhDst0; static float g_hhNd[2][4], g_hhNdShow[2][4], g_hhNdSc; static int g_hhNdOk[2], g_hhNdHas[2];   /* the dashboard's place as blitted (before a shrink); the needles as the game drew them (frame pixels) */
static void HhSceneBegin(void){ g_hhSeen=0; g_hhNdOk[0]=g_hhNdOk[1]=0; }   /* RR_GpuSceneBegin: a new frame */
/* the game drew the speedometer (0) / tachometer (1) needle with LineTo on dc (src\ui\hud.c): kept to draw our own needle and leave the game's out */
__declspec(dllexport) void __stdcall RR_HudNeedle(HDC dc, int which, int x0, int y0, int x1, int y1)
{   POINT o; o.x=o.y=0; if(which<0 || which>1) return; if(dc) GetViewportOrgEx(dc,&o);
    g_hhNd[which][0]=(float)(x0+o.x); g_hhNd[which][1]=(float)(y0+o.y); g_hhNd[which][2]=(float)(x1+o.x); g_hhNd[which][3]=(float)(y1+o.y); g_hhNdOk[which]=1; }
/* a needle in the dashboard's place as shown (after a shrink): from the blit's place to the shown one */
static void HhMapNd(int k, float *p)
{   float sx=(float)(g_hhDst.right-g_hhDst.left)/(g_hhDst0.right-g_hhDst0.left), sy=(float)(g_hhDst.bottom-g_hhDst.top)/(g_hhDst0.bottom-g_hhDst0.top); int i;
    for(i=0;i<2;i++){ p[i*2]=g_hhDst.left+(g_hhNd[k][i*2]+0.5f-g_hhDst0.left)*sx; p[i*2+1]=g_hhDst.top+(g_hhNd[k][i*2+1]+0.5f-g_hhDst0.top)*sy; } }
static float HhSegDist(float px, float py, const float *s)
{   float dx=s[2]-s[0], dy=s[3]-s[1], l2=dx*dx+dy*dy, t=l2>0?((px-s[0])*dx+(py-s[1])*dy)/l2:0; if(t<0) t=0; if(t>1) t=1;
    dx=s[0]+dx*t-px; dy=s[1]+dy*t-py; return (float)sqrt(dx*dx+dy*dy); }
static void HhPoly(const float *xy, int n, SDL_FColor c)   /* a convex polygon, frame pixels */
{   SDL_Vertex v[40]; int idx[120], i, ni=0; if(n>40) n=40;
    for(i=0;i<n;i++){ v[i].position.x=xy[i*2]; v[i].position.y=xy[i*2+1]; v[i].color=c; v[i].tex_coord.x=v[i].tex_coord.y=0; }
    for(i=1;i+1<n;i++){ idx[ni++]=0; idx[ni++]=i; idx[ni++]=i+1; }
    SDL_RenderGeometry(g_ren,NULL,v,n,idx,ni); }
/* our needle: a tapered red pointer from a little behind the hub to the game's tip, a soft shadow under it, a dark hub with a bright rim */
static void HhNeedle(const float *s, float sc)
{   float dx=s[2]-s[0], dy=s[3]-s[1], l=(float)sqrt(dx*dx+dy*dy), ux, uy, nx, ny, wb=1.25f*sc, wt=0.35f*sc, tail=l*0.2f, q[8], hub[40]; int k, i;
    SDL_FColor shadow={0,0,0,0.35f}, body={0.86f,0.10f,0.04f,1}, light={1.0f,0.42f,0.30f,1}, hubc={0.10f,0.10f,0.11f,1}, rim={0.62f,0.62f,0.64f,1};
    if(l<1) return; ux=dx/l; uy=dy/l; nx=-uy; ny=ux;
    for(k=0;k<2;k++){ float ox=k?0:0.55f*sc, oy=k?0:0.75f*sc;   /* 0: shadow, 1: the needle */
        q[0]=s[0]-ux*tail+nx*wb+ox; q[1]=s[1]-uy*tail+ny*wb+oy; q[2]=s[2]+nx*wt+ox; q[3]=s[3]+ny*wt+oy;
        q[4]=s[2]-nx*wt+ox; q[5]=s[3]-ny*wt+oy; q[6]=s[0]-ux*tail-nx*wb+ox; q[7]=s[1]-uy*tail-ny*wb+oy;
        HhPoly(q,4,k?body:shadow); }
    q[0]=s[0]-ux*tail; q[1]=s[1]-uy*tail; q[2]=s[2]; q[3]=s[3]; q[4]=s[2]+nx*wt; q[5]=s[3]+ny*wt; q[6]=s[0]-ux*tail+nx*wb; q[7]=s[1]-uy*tail+ny*wb;
    HhPoly(q,4,light);   /* (its lit half: a ridge down the middle) */
    for(k=0;k<2;k++){ float r=(k?1.45f:2.0f)*sc; for(i=0;i<20;i++){ hub[i*2]=s[0]+r*(float)cos(i*0.314159f); hub[i*2+1]=s[1]+r*(float)sin(i*0.314159f); } HhPoly(hub,20,k?hubc:rim); } }
/* S_BltFast: a blit of the dashboard into the race frame (its middle row is the dashboard's) */
static void HhNoteBlt(const Surf *s, RECT s2, RECT d2)
{   int y;
    if(!g_gpuOn || !g_hhIdx || s->bpp!=8 || s->w!=640 || s2.left!=0 || s2.right!=640 || s2.top!=0 || s2.bottom!=g_hhH) return;
    y=g_hhH/2; if(memcmp(s->bits+y*s->pitch,g_hhIdx+y*640,640)) return;
    g_hhDst=g_hhDst0=d2; g_hhSeen=1; }
/* RR_HudEnd shrank the dashboard strip (wide frame): it now fills x0, y0, w2 x h2 */
static void HhNoteShrink(int x0, int y0, int w2, int h2){ if(g_hhSeen){ g_hhDst.left=x0; g_hhDst.top=y0; g_hhDst.right=x0+w2; g_hhDst.bottom=y0+h2; } }
/* GpuCompose, after the overlay: the HD dashboard, then the HUD's pixels over it (frame s, its palette lut) */
static void HhDraw(const Surf *s, const DWORD *lut)
{   int dw, dh, x, y; DWORD *ov; SDL_FRect r;
    AcquireSRWLockShared(&g_hhLk);
    if(strcmp(g_hhWant,g_hhCur)){ SDL_Surface *sf;
        if(g_hhTex){ SDL_DestroyTexture(g_hhTex); g_hhTex=NULL; } strcpy(g_hhCur,g_hhWant);
        free(g_hhIdx); g_hhIdx=NULL; g_hhH=0; if(g_hhIdxWant){ g_hhIdx=(BYTE*)malloc((size_t)g_hhHWant*640); if(g_hhIdx){ memcpy(g_hhIdx,g_hhIdxWant,(size_t)g_hhHWant*640); g_hhH=g_hhHWant; } }
        if(g_hhWant[0]){ if((sf=SDL_LoadPNG(g_hhWant))!=NULL){ g_hhTex=SDL_CreateTextureFromSurface(g_ren,sf); SDL_DestroySurface(sf);
                if(g_hhTex){ SDL_SetTextureBlendMode(g_hhTex,SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(g_hhTex,SDL_SCALEMODE_LINEAR); LOG("HD dashboard: %s",g_hhWant); } }
            else LOG("HD dashboard: %s: %s",g_hhWant,SDL_GetError()); } }
    ReleaseSRWLockShared(&g_hhLk);
    if(!g_hhSeen || !g_hhTex || !g_hhIdx || !HhOn()) return;
    dw=g_hhDst.right-g_hhDst.left; dh=g_hhDst.bottom-g_hhDst.top;
    if(dw<=0 || dh<=0 || g_hhDst.left<0 || g_hhDst.top<0 || g_hhDst.right>s->w || g_hhDst.bottom>s->h) return;
    r.x=(float)g_hhDst.left; r.y=(float)g_hhDst.top; r.w=(float)dw; r.h=(float)dh;
    SDL_RenderTexture(g_ren,g_hhTex,NULL,&r);
    if(!(ov=(DWORD*)malloc((size_t)dw*dh*4))) return;
    {   float nd[2][4], sc=(float)dw/(g_hhDst0.right-g_hhDst0.left), rr=2.6f*sc; int k, has[2];   /* the game's needles (3-pixel pen) stay out: ours are drawn instead */
        for(k=0;k<2;k++){ has[k]=g_hhNdOk[k]; if(has[k]) HhMapNd(k,nd[k]); }
        for(y=0;y<dh;y++){ const BYTE *fr=s->bits+(size_t)(g_hhDst.top+y)*s->pitch+g_hhDst.left, *dr=g_hhIdx+(size_t)(y*g_hhH/dh)*640;   /* (the same nearest sampling as the shrink) */
            for(x=0;x<dw;x++){ int d=dr[x*640/dw], f=fr[x]; DWORD c=(d==0 || f==d)?0:lut[f];   /* (0 = the dashboard's colour key: the world shows there) */
                if(c) for(k=0;k<2;k++) if(has[k] && HhSegDist(g_hhDst.left+x+0.5f,g_hhDst.top+y+0.5f,nd[k])<rr){ c=0; break; }
                ov[y*dw+x]=c; } }
        memcpy(g_hhNdShow,nd,sizeof nd); g_hhNdHas[0]=has[0]; g_hhNdHas[1]=has[1]; g_hhNdSc=sc; }
    if(!g_hhOv || g_hhOvW!=dw || g_hhOvH!=dh){ if(g_hhOv) SDL_DestroyTexture(g_hhOv); g_hhOv=SDL_CreateTexture(g_ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,dw,dh); g_hhOvW=dw; g_hhOvH=dh;
        if(g_hhOv) SDL_SetTextureBlendMode(g_hhOv,SDL_BLENDMODE_BLEND); }
    if(g_hhOv){ SDL_SetTextureScaleMode(g_hhOv,g_vid.filter?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST); SDL_UpdateTexture(g_hhOv,NULL,ov,dw*4); SDL_RenderTexture(g_ren,g_hhOv,NULL,&r); }
    free(ov);
    SDL_SetRenderDrawBlendMode(g_ren,SDL_BLENDMODE_BLEND);
    for(x=0;x<2;x++) if(g_hhNdHas[x]) HhNeedle(g_hhNdShow[x],g_hhNdSc);
    SDL_SetRenderDrawBlendMode(g_ren,SDL_BLENDMODE_NONE); }
