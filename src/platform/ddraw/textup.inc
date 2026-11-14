/* Sharp text (included by rrddraw.c before Present): the game draws all its text with GDI (DrawTextExA / DrawTextA / TextOutA) into the 640 x 480 surfaces,
 * and the whole picture is then scaled up to the window - so the text was as soft as the pictures.  Here those calls are hooked (the game's imports;
 * the shim's own screens call TxTextOutA directly): each one still draws into the surface exactly as before (the game state, the test hashes and the dumps
 * do not change), and is also recorded - string, rectangle / position, flags, font, colour, the DC's origin, the pixels it covered (before) and a copy of
 * its area afterwards.  At a present, an item whose area still shows that copy is still on the screen: the copy that is shown gets its "before" pixels
 * back (newest first, so overlapping text - shadows - unwinds to the background), and the same calls are replayed with the font at the window's own
 * scale into a transparent layer drawn over the frame (TrueType glyphs at full resolution, grey-scale anti-aliased; resampled across to the width the
 * text had at 640 x 480, TxDraw).  Anything else that draws over a text (a blit, a lock, a fill) changes its area and drops it; text drawn later over an
 * older one updates the older one's copy.  The game draws its menu text on g_screenSurface and copies it to the primary in update rectangles: the items
 * go along with 1:1 blits (TxBltBegin / TxBltEnd).
 * Off: RR_HITEXT=0 (comparisons); RR_HITEXT_LOG=1 logs every item.  Not yet: the race HUD (drawn on the back buffer during races: not recorded, g_inRace). */
typedef struct TxItem { Surf *s; RECT bb; BYTE *before, *after; int kind; char *str; int n; RECT rc; UINT fmt; int hasDtp; DRAWTEXTPARAMS dtp;
                        int x, y; UINT align; LOGFONTA lf; COLORREF col; POINT vorg; float w0; } TxItem;   /* w0 = its one-line width at 640 x 480 (TxWidth) */   /* kind 0 = DrawTextEx (DrawText: no params), 1 = TextOut; bb in surface pixels */
#define TX_MAX 1024
static TxItem *g_tx[TX_MAX]; static int g_txN; static unsigned g_txGen; static int g_txOn=-1; static CRITICAL_SECTION g_txLk; static LONG g_txLkInit;
static Surf *g_txDc[32];                                  /* surfaces whose DC the game got (S_GetDC): text on them is recorded */
static BYTE *g_txSnap; static size_t g_txSnapN;           /* the surface just before a text call */
static int (WINAPI *g_rDrawTextExA)(HDC,LPSTR,int,LPRECT,UINT,LPDRAWTEXTPARAMS);
static int (WINAPI *g_rDrawTextA)(HDC,LPCSTR,int,LPRECT,UINT);
static BOOL (WINAPI *g_rTextOutA)(HDC,int,int,LPCSTR,int);

static void TxLock(void){ if(InterlockedCompareExchange(&g_txLkInit,1,0)==0){ InitializeCriticalSection(&g_txLk); g_txLkInit=2; } while(g_txLkInit!=2) Sleep(0); EnterCriticalSection(&g_txLk); }
static void TxUnlock(void){ LeaveCriticalSection(&g_txLk); }
static int TxEnabled(void){ if(g_txOn<0){ const char *e=getenv("RR_HITEXT"); g_txOn=!(e && *e=='0'); } return g_txOn && g_ren && !g_headless; }
static void TxNoteDC(Surf *s){ int i, f=-1; for(i=0;i<32;i++){ if(g_txDc[i]==s) return; if(!g_txDc[i] && f<0) f=i; } if(f>=0) g_txDc[f]=s; }
static Surf *TxSurfOfDC(HDC dc){ int i; if(!dc) return NULL; for(i=0;i<32;i++) if(g_txDc[i] && g_txDc[i]->dc==dc) return g_txDc[i]; return NULL; }
/* an item is still on its surface while its area shows what it showed after the text was drawn (kept in t->after; TxKeep takes it again) */
static int TxValid(const TxItem *t)
{   int y, w=(t->bb.right-t->bb.left)*t->s->bytes; if(!t->after) return 0;
    for(y=t->bb.top;y<t->bb.bottom;y++) if(memcmp(t->s->bits+y*t->s->pitch+t->bb.left*t->s->bytes,t->after+(size_t)(y-t->bb.top)*w,w)) return 0;
    return 1; }
static void TxKeep(TxItem *t)
{   int y, w=(t->bb.right-t->bb.left)*t->s->bytes; if(!t->after) t->after=(BYTE*)malloc((size_t)w*(t->bb.bottom-t->bb.top)); if(!t->after) return;
    for(y=t->bb.top;y<t->bb.bottom;y++) memcpy(t->after+(size_t)(y-t->bb.top)*w,t->s->bits+y*t->s->pitch+t->bb.left*t->s->bytes,w); }
static void TxDrop(int i){ TxItem *t=g_tx[i]; free(t->before); free(t->after); free(t->str); free(t); memmove(g_tx+i,g_tx+i+1,(g_txN-i-1)*sizeof g_tx[0]); g_txN--; g_txGen++; }
/* items of s that are no longer on it (their area changed by something else) go */
static void TxPrune(const Surf *s){ int i; for(i=g_txN-1;i>=0;i--) if(g_tx[i]->s==s && !TxValid(g_tx[i])) TxDrop(i); }
static void TxPruneRows(const Surf *s,int y0,int y1){ int i; for(i=g_txN-1;i>=0;i--) if(g_tx[i]->s==s && g_tx[i]->bb.bottom>y0 && g_tx[i]->bb.top<y1 && !TxValid(g_tx[i])) TxDrop(i); }
static void TxForgetSurf(Surf *s)   /* FreeSurf */
{   int i; if(!g_txLkInit) return; TxLock(); for(i=g_txN-1;i>=0;i--) if(g_tx[i]->s==s) TxDrop(i); for(i=0;i<32;i++) if(g_txDc[i]==s) g_txDc[i]=NULL; TxUnlock(); }

/* before a text call: the surface the DC draws on (NULL = not recorded), its items brought up to date, the picture kept - only the rows the text can
   touch (ly0..ly1 in the DC's logical units, a few rows of margin; the credits redraw a whole page of text every frame) */
static int g_txY0, g_txY1;
static Surf *TxBegin(HDC dc,int ly0,int ly1)
{   Surf *s; POINT o; int y;
    if(!TxEnabled() || g_inRace || !(s=TxSurfOfDC(dc)) || (s->bpp!=8 && s->bpp!=16) || GetBkMode(dc)==OPAQUE || (GetTextAlign(dc)&TA_UPDATECP)) return NULL;
    GetViewportOrgEx(dc,&o); g_txY0=ly0+o.y-4; g_txY1=ly1+o.y+4; if(g_txY0<0) g_txY0=0; if(g_txY1>s->h) g_txY1=s->h;
    if(g_txY1<=g_txY0) return NULL;
    GdiFlush(); TxLock(); TxPruneRows(s,g_txY0,g_txY1);   /* (only what this text can overlap: the rest is checked at the present) */
    if(g_txSnapN<(size_t)s->pitch*s->h){ free(g_txSnap); g_txSnapN=(size_t)s->pitch*s->h; g_txSnap=(BYTE*)malloc(g_txSnapN); if(!g_txSnap) g_txSnapN=0; }
    if(!g_txSnap){ TxUnlock(); return NULL; }
    for(y=g_txY0;y<g_txY1;y++) memcpy(g_txSnap+y*s->pitch,s->bits+y*s->pitch,(size_t)s->w*s->bytes);
    return s; }   /* (stays locked until TxEnd) */
/* the rows a TextOut at y can touch (its alignment: top, baseline or bottom) */
static void TxRowsTextOut(HDC dc,int y,int *y0,int *y1)
{   TEXTMETRICA tm; UINT a=GetTextAlign(dc); if(!GetTextMetricsA(dc,&tm)){ *y0=-100000; *y1=100000; return; }
    if((a&TA_BASELINE)==TA_BASELINE){ *y0=y-tm.tmAscent; *y1=y+tm.tmDescent; } else if(a&TA_BOTTOM){ *y0=y-tm.tmHeight; *y1=y; } else { *y0=y; *y1=y+tm.tmHeight; } }
static int TxSame(const TxItem *a,const TxItem *b);
/* the item's text as one line in dc's font, in logical units (the width it needs) */
static float TxWidth(HDC dc,const TxItem *t)
{   if(t->kind==1){ SIZE z; return GetTextExtentPoint32A(dc,t->str,t->n,&z) ? (float)z.cx : 0; }
    { RECT r; DRAWTEXTPARAMS p=t->dtp; r.left=0; r.top=0; r.right=32767; r.bottom=0; p.iLeftMargin=p.iRightMargin=0;   /* (the text alone: margins are not scaled like glyphs) */
      g_rDrawTextExA(dc,t->str,t->n,&r,DT_CALCRECT|DT_SINGLELINE|(t->fmt&(DT_NOPREFIX|DT_EXPANDTABS|DT_TABSTOP|DT_HIDEPREFIX)),t->hasDtp?&p:NULL); return (float)(r.right-r.left); } }
/* a live item of s that draws exactly what t draws, at the same place (-1 = none) */
static int TxFind(const Surf *s,const TxItem *t)
{   int i; for(i=0;i<g_txN;i++) if(g_tx[i]!=t && g_tx[i]->s==s && g_tx[i]->vorg.x==t->vorg.x && g_tx[i]->vorg.y==t->vorg.y && TxSame(g_tx[i],t)) return i; return -1; }
/* after it: the changed area becomes an item (t: the call's parameters, filled by the hook) */
static void TxEnd(Surf *s,HDC dc,TxItem *t,const char *str,int n)
{   RECT b; int y, x, bpp=s->bytes, top=-1, bot=-1, l=s->w, r=-1, i; TxItem *it; HGDIOBJ f;
    GdiFlush();
    for(y=g_txY0;y<g_txY1;y++){ const BYTE *a=g_txSnap+y*s->pitch, *c=s->bits+y*s->pitch; if(!memcmp(a,c,(size_t)s->w*bpp)) continue;
        if(top<0) top=y; bot=y;
        for(x=0;x<s->w;x++) if(memcmp(a+x*bpp,c+x*bpp,bpp)){ if(x<l) l=x; break; }
        for(x=s->w-1;x>r;x--) if(memcmp(a+x*bpp,c+x*bpp,bpp)){ r=x; break; } }
    if(top<0 || g_txN>=TX_MAX){ if(getenv("RR_HITEXT_LOG")) LOG("hitext: unchanged '%.40s' (items %d)",str,g_txN); TxUnlock(); return; }
    b.left=l>0?l-1:0; b.top=top>0?top-1:0; b.right=r+2<s->w?r+2:s->w; b.bottom=bot+2<s->h?bot+2:s->h;   /* (a pixel of margin: anti-aliased edges that matched) */
    for(i=0;i<g_txN;i++){ RECT o; if(g_tx[i]->s==s && IntersectRect(&o,&g_tx[i]->bb,&b)) TxKeep(g_tx[i]); }   /* older text under it: still there */
    it=(TxItem*)malloc(sizeof *it); if(!it){ TxUnlock(); return; }
    *it=*t; it->s=s; it->bb=b; if(n<0) n=(int)strlen(str);
    it->str=(char*)malloc(n+1); it->before=(BYTE*)malloc((size_t)(b.right-b.left)*(b.bottom-b.top)*bpp);
    if(!it->str || !it->before){ free(it->str); free(it->before); free(it); TxUnlock(); return; }
    memcpy(it->str,str,n); it->str[n]=0; it->n=n;
    for(y=b.top;y<b.bottom;y++) memcpy(it->before+(y-b.top)*(b.right-b.left)*bpp,g_txSnap+y*s->pitch+b.left*bpp,(size_t)(b.right-b.left)*bpp);
    TxKeep(it); f=GetCurrentObject(dc,OBJ_FONT); memset(&it->lf,0,sizeof it->lf); if(f) GetObjectA(f,sizeof it->lf,&it->lf);
    it->col=GetTextColor(dc); GetViewportOrgEx(dc,&it->vorg); it->w0=-1;   /* (its width at 640 x 480: when it is first drawn sharp, TxWidthLo) */
    if((i=TxFind(s,it))>=0){ TxKeep(g_tx[i]); free(it->str); free(it->before); free(it->after); free(it); TxUnlock(); return; }   /* (the same text again on top: the old item keeps the real background) */
    if(getenv("RR_HITEXT_LOG")) LOG("hitext: rec '%s' on %p bb %ld,%ld-%ld,%ld",it->str,(void*)s,b.left,b.top,b.right,b.bottom);
    g_tx[g_txN++]=it; g_txGen++; TxUnlock(); }

/* a 1:1 blit (or Flip) copies text to another surface - the game draws its menu text on g_screenSurface and blits it to the primary: every live item that
   lies wholly inside the source rectangle goes along, moved by the blit's offset.  Its "before" is the source's, except where that was the transparent colour
   (a keyed blit): there it is what the destination had.  TxBltBegin (before the copy: 1 = TxBltEnd must follow, the lock is held) keeps the destination area */
static BYTE *g_txBlt; static size_t g_txBltN;
/* both rectangles cut to both surfaces (a 1:1 copy: they move together - the game's update rectangles are in window pixels and run past the 640 x 480 frame) */
static void TxBltClip(const Surf *d,RECT *dr,const Surf *s,RECT *sr)
{   int c;
    c=0; if(dr->left<0) c=-dr->left; if(sr->left+c<0) c=-sr->left; dr->left+=c; sr->left+=c;
    c=0; if(dr->top<0) c=-dr->top; if(sr->top+c<0) c=-sr->top; dr->top+=c; sr->top+=c;
    c=0; if(dr->right>d->w) c=dr->right-d->w; if(sr->right-c>s->w) c=sr->right-s->w; dr->right-=c; sr->right-=c;
    c=0; if(dr->bottom>d->h) c=dr->bottom-d->h; if(sr->bottom-c>s->h) c=sr->bottom-s->h; dr->bottom-=c; sr->bottom-=c; }
/* area a of s shows the same as area b of d (key: the source's transparent pixels are not compared - a keyed copy leaves d's own there) */
static int TxSameArea(const Surf *s,RECT a,const Surf *d,RECT b,int key)
{   int y, x, w=a.right-a.left, bpp=s->bytes;
    for(y=0;y<a.bottom-a.top;y++){ const BYTE *p=s->bits+(a.top+y)*s->pitch+a.left*bpp, *q=d->bits+(b.top+y)*d->pitch+b.left*bpp;
        if(!key){ if(memcmp(p,q,(size_t)w*bpp)) return 0; continue; }
        for(x=0;x<w;x++){ DWORD v=bpp==2?((const WORD*)p)[x]:p[x]; if(!(v>=s->ckLo && v<=s->ckHi) && memcmp(p+x*bpp,q+x*bpp,bpp)) return 0; } }
    return 1; }
static int TxBltBegin(Surf *d,RECT dr,Surf *s,RECT sr)
{   int i, y; size_t n;
    if(!g_txLkInit || !TxEnabled() || !d || !s || d==s || d->bpp!=s->bpp || dr.right-dr.left!=sr.right-sr.left || dr.bottom-dr.top!=sr.bottom-sr.top) return 0;
    TxBltClip(d,&dr,s,&sr); if(dr.right<=dr.left || dr.bottom<=dr.top) return 0;
    TxLock();
    for(i=0;i<g_txN;i++) if(g_tx[i]->s==s) break;
    if(i==g_txN){ TxUnlock(); return 0; }
    n=(size_t)(dr.right-dr.left)*(dr.bottom-dr.top)*d->bytes; if(g_txBltN<n){ free(g_txBlt); g_txBlt=(BYTE*)malloc(n); g_txBltN=g_txBlt?n:0; }
    if(!g_txBlt){ TxUnlock(); return 0; }
    for(y=dr.top;y<dr.bottom;y++) memcpy(g_txBlt+(size_t)(y-dr.top)*(dr.right-dr.left)*d->bytes,d->bits+y*d->pitch+dr.left*d->bytes,(size_t)(dr.right-dr.left)*d->bytes);
    return 1; }
static void TxBltEnd(Surf *d,RECT dr,Surf *s,RECT sr,int key)
{   int i, n0, dx, dy, bpp=d->bytes, dw;
    TxBltClip(d,&dr,s,&sr); n0=g_txN; dx=dr.left-sr.left; dy=dr.top-sr.top; dw=dr.right-dr.left;
    TxPrune(d);   /* (what the copy covered on d is gone) */
    for(i=0;i<n0 && i<g_txN;i++){ TxItem *t=g_tx[i], *c; int w, h, x, y; RECT b, o;
        if(t->s!=s || !IntersectRect(&o,&t->bb,&sr) || !TxValid(t) || g_txN>=TX_MAX) continue;
        b=t->bb; OffsetRect(&b,dx,dy); w=b.right-b.left; h=b.bottom-b.top;
        if(b.left<0 || b.top<0 || b.right>d->w || b.bottom>d->h || !TxSameArea(s,t->bb,d,b,key)) continue;   /* (the whole text is on d now - this copy may have brought only part of it: the update rectangles slice through texts) */
        { TxItem probe=*t; int k; probe.s=d; probe.vorg.x+=dx; probe.vorg.y+=dy;   /* (copied again over itself - a repaint: the item already there stays) */
          if((k=TxFind(d,&probe))>=0){ TxKeep(g_tx[k]); continue; } }
        c=(TxItem*)malloc(sizeof *c); if(!c) break; *c=*t; c->after=NULL; c->s=d; c->bb=b; c->vorg.x+=dx; c->vorg.y+=dy;
        c->str=(char*)malloc(t->n+1); c->before=(BYTE*)malloc((size_t)w*h*bpp);
        if(!c->str || !c->before){ free(c->str); free(c->before); free(c); break; }
        memcpy(c->str,t->str,t->n+1); memcpy(c->before,t->before,(size_t)w*h*bpp);
        if(key) for(y=0;y<h;y++) for(x=0;x<w;x++){ BYTE *p=c->before+((size_t)y*w+x)*bpp; DWORD v=bpp==2?*(WORD*)p:*p;
            if(v>=s->ckLo && v<=s->ckHi) memcpy(p,g_txBlt+((size_t)(b.top+y-dr.top)*dw+(b.left+x-dr.left))*bpp,bpp); }
        if(getenv("RR_HITEXT_LOG")) LOG("hitext: copy '%s' %p -> %p",c->str,(void*)s,(void*)d);
        TxKeep(c); g_tx[g_txN++]=c; g_txGen++; }
    TxUnlock(); }

static void TxReal(void)
{   HMODULE u=GetModuleHandleA("user32.dll"), g=GetModuleHandleA("gdi32.dll");
    if(!g_rDrawTextExA) g_rDrawTextExA=(int (WINAPI*)(HDC,LPSTR,int,LPRECT,UINT,LPDRAWTEXTPARAMS))GetProcAddress(u,"DrawTextExA");
    if(!g_rDrawTextA) g_rDrawTextA=(int (WINAPI*)(HDC,LPCSTR,int,LPRECT,UINT))GetProcAddress(u,"DrawTextA");
    if(!g_rTextOutA) g_rTextOutA=(BOOL (WINAPI*)(HDC,int,int,LPCSTR,int))GetProcAddress(g,"TextOutA"); }
static int WINAPI TxDrawTextExA(HDC dc,LPSTR str,int n,LPRECT rc,UINT fmt,LPDRAWTEXTPARAMS p)
{   Surf *s; TxItem t; int r; TxReal();
    if((fmt&DT_CALCRECT) || !str || !rc || !(s=TxBegin(dc,rc->top,(fmt&DT_NOCLIP)?100000:rc->bottom))) return g_rDrawTextExA(dc,str,n,rc,fmt,p);
    memset(&t,0,sizeof t); t.kind=0; t.rc=*rc; t.fmt=fmt; if(p){ t.hasDtp=1; t.dtp=*p; }
    r=g_rDrawTextExA(dc,str,n,rc,fmt,p); TxEnd(s,dc,&t,str,n); return r; }
static int WINAPI TxDrawTextA(HDC dc,LPCSTR str,int n,LPRECT rc,UINT fmt)
{   Surf *s; TxItem t; int r; TxReal();
    if((fmt&DT_CALCRECT) || !str || !rc || !(s=TxBegin(dc,rc->top,(fmt&DT_NOCLIP)?100000:rc->bottom))) return g_rDrawTextA(dc,str,n,rc,fmt);
    memset(&t,0,sizeof t); t.kind=0; t.rc=*rc; t.fmt=fmt;
    r=g_rDrawTextA(dc,str,n,rc,fmt); TxEnd(s,dc,&t,str,n); return r; }
static BOOL WINAPI TxTextOutA(HDC dc,int x,int y,LPCSTR str,int n)
{   Surf *s; TxItem t; BOOL r; int y0, y1; TxReal();
    if(!str || n<=0) return g_rTextOutA(dc,x,y,str,n);
    TxRowsTextOut(dc,y,&y0,&y1); if(!(s=TxBegin(dc,y0,y1))) return g_rTextOutA(dc,x,y,str,n);
    memset(&t,0,sizeof t); t.kind=1; t.x=x; t.y=y; t.align=GetTextAlign(dc);
    r=g_rTextOutA(dc,x,y,str,n); TxEnd(s,dc,&t,str,n); return r; }
static void TxHook(void)
{   HookImportByName("user32.dll","DrawTextExA",(void*)TxDrawTextExA,(void**)&g_rDrawTextExA);
    HookImportByName("user32.dll","DrawTextA",(void*)TxDrawTextA,(void**)&g_rDrawTextA);
    HookImportByName("gdi32.dll","TextOutA",(void*)TxTextOutA,(void**)&g_rTextOutA); LOG("hitext: hooks DrawTextExA=%p DrawTextA=%p TextOutA=%p",(void*)g_rDrawTextExA,(void*)g_rDrawTextA,(void*)g_rTextOutA); TxReal(); }

/* ---- present ---- */
/* the copy of s that is shown: the background back under every text still on it (NULL = no text: show s itself) */
static Surf g_txView; static BYTE *g_txViewBits; static size_t g_txViewN;
static int TxScale(const Surf *s,float *kx,float *ky)
{   SDL_FRect r; *kx=g_wsSx; *ky=g_wsSy;
    if(SDL_GetRenderLogicalPresentationRect(g_ren,&r) && r.w>0 && r.h>0 && s->w>0 && s->h>0){ *kx=r.w/s->w; *ky=r.h/s->h; }
    if(*kx>8) *kx=8; if(*ky>8) *ky=8;
    return *kx>1.1f || *ky>1.1f; }   /* (at about 1:1 the frame's own text is already sharp) */
static Surf *TxClean(Surf *s)
{   int i, y, any=0; size_t n; float kx, ky;
    if(!g_txLkInit || !TxEnabled() || !TxScale(s,&kx,&ky)) return NULL;
    TxLock(); TxPrune(s);
    for(i=0;i<g_txN;i++) if(g_tx[i]->s==s){ any=1; break; }
    n=(size_t)s->pitch*s->h;
    if(!any || (g_txViewN<n && (free(g_txViewBits), g_txViewBits=(BYTE*)malloc(n), g_txViewN=g_txViewBits?n:0, !g_txViewBits))){ TxUnlock(); return NULL; }
    memcpy(g_txViewBits,s->bits,n); g_txView=*s; g_txView.bits=g_txViewBits;
    for(i=g_txN-1;i>=0;i--){ TxItem *t=g_tx[i]; int w=(t->bb.right-t->bb.left)*s->bytes; if(t->s!=s) continue;
        for(y=t->bb.top;y<t->bb.bottom;y++) memcpy(g_txViewBits+y*s->pitch+t->bb.left*s->bytes,t->before+(y-t->bb.top)*w,w); }
    TxUnlock(); return &g_txView; }

/* the text layer: every live item of s replayed at the window's scale, white on black (the coverage), blended in its colour into a premultiplied layer */
static RECT g_txDirty, g_txUp;   /* the layer's texts' area last time / what this rebuild clears and uploads */
static SDL_Texture *g_txTex; static int g_txTexW, g_txTexH; static unsigned g_txTexGen=~0u; static const Surf *g_txTexSurf;
static HDC g_txDcHi; static HBITMAP g_txBmp, g_txOldBmp; static DWORD *g_txCov, *g_txAcc; static int g_txBw, g_txBh;
static int TxSame(const TxItem *a,const TxItem *b)
{   return a->kind==b->kind && a->n==b->n && !strcmp(a->str,b->str) && a->col==b->col && EqualRect(&a->rc,&b->rc) && a->x==b->x && a->y==b->y && a->fmt==b->fmt
        && a->lf.lfHeight==b->lf.lfHeight && a->lf.lfWeight==b->lf.lfWeight && !strcmp(a->lf.lfFaceName,b->lf.lfFaceName); }
/* the width t had at 640 x 480: its font at its own size on a private DC (the same as on the game's DC - the advance widths do not depend on the bitmap) */
static HDC g_txDcLo;
static float TxWidthLo(const TxItem *t)
{   HFONT f, of; float w; if(!g_txDcLo) g_txDcLo=CreateCompatibleDC(NULL); if(!g_txDcLo || !(f=CreateFontIndirectA(&t->lf))) return 0;
    of=(HFONT)SelectObject(g_txDcLo,f); w=TxWidth(g_txDcLo,t); SelectObject(g_txDcLo,of); DeleteObject(f); return w; }
/* a left-aligned word-wrapped DrawTextEx item's text with its line breaks as the game's 640 x 480 drawing made them (the font at its own size, the
   rectangle's width less the margins) - so the sharp copy breaks where the game broke, whatever its own widths (malloc'd; NULL = not such an item) */
static char *TxBreakLo(const TxItem *t)
{   HFONT f, of; char *o; int i = 0, k = 0, ls = 0, w; SIZE z;
    if (t->kind != 0 || !(t->fmt & DT_WORDBREAK) || (t->fmt & (DT_CENTER | DT_RIGHT | DT_SINGLELINE)) || t->n <= 0 || t->n > 4000) return NULL;
    w = t->rc.right - t->rc.left - (t->hasDtp ? t->dtp.iLeftMargin + t->dtp.iRightMargin : 0); if (w <= 0) return NULL;
    if (!g_txDcLo) g_txDcLo = CreateCompatibleDC(NULL); if (!g_txDcLo || !(f = CreateFontIndirectA(&t->lf))) return NULL;
    if (!(o = (char *)malloc((size_t)t->n * 2 + 2))) { DeleteObject(f); return NULL; }
    of = (HFONT)SelectObject(g_txDcLo, f);
    while (i < t->n) {   /* (greedy, as GDI: a word that does not fit goes to the next line; the spaces at a break are dropped) */
        int j = i, e;
        if (t->str[i] == '\r' || t->str[i] == '\n') { if (t->str[i] == '\r' && i + 1 < t->n && t->str[i + 1] == '\n') i++; o[k++] = '\n'; i++; ls = k; continue; }
        while (j < t->n && t->str[j] == ' ') j++;
        e = j; while (e < t->n && t->str[e] != ' ' && t->str[e] != '\r' && t->str[e] != '\n') e++;
        memcpy(o + k, t->str + i, (size_t)(e - i));
        if (k > ls && GetTextExtentPoint32A(g_txDcLo, o + ls, k + (e - i) - ls, &z) && z.cx > w) { o[k++] = '\n'; ls = k; memcpy(o + k, t->str + j, (size_t)(e - j)); k += e - j; }
        else k += e - i;
        i = e; }
    o[k] = 0;
    SelectObject(g_txDcLo, of); DeleteObject(f); return o; }
static int TxBuffers(int W,int H)
{   BITMAPINFO bi;
    if(g_txDcHi && g_txBw==W && g_txBh==H) return 1;
    if(g_txDcHi){ SelectObject(g_txDcHi,g_txOldBmp); DeleteObject(g_txBmp); DeleteDC(g_txDcHi); g_txDcHi=NULL; } free(g_txAcc); g_txAcc=NULL;
    memset(&bi,0,sizeof bi); bi.bmiHeader.biSize=sizeof bi.bmiHeader; bi.bmiHeader.biWidth=W; bi.bmiHeader.biHeight=-H; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32; bi.bmiHeader.biCompression=BI_RGB;
    g_txDcHi=CreateCompatibleDC(NULL); g_txBmp=g_txDcHi?CreateDIBSection(g_txDcHi,&bi,DIB_RGB_COLORS,(void**)&g_txCov,NULL,0):NULL; g_txAcc=(DWORD*)malloc((size_t)W*H*4);
    if(!g_txDcHi || !g_txBmp || !g_txAcc){ if(g_txBmp) DeleteObject(g_txBmp); if(g_txDcHi) DeleteDC(g_txDcHi); g_txDcHi=NULL; free(g_txAcc); g_txAcc=NULL; return 0; }
    g_txOldBmp=SelectObject(g_txDcHi,g_txBmp); g_txBw=W; g_txBh=H; return 1; }
/* rendered texts, kept by what they draw (string, font, flags, sizes, scale, the area relative to the text's origin - not where): moving text (the
   credits) and redrawn screens reuse them, only new texts go through GDI */
typedef struct TxTile { unsigned long long key; int w, h; BYTE *cov; } TxTile;
#define TX_TILES 256
static TxTile g_txTiles[TX_TILES]; static int g_txTileNext;
static unsigned long long TxMix(unsigned long long h,const void *p,size_t n){ const BYTE *b=(const BYTE*)p; while(n--) h=(h^*b++)*1099511628211ull; return h; }
static unsigned long long TxKey(const TxItem *t,float kx,float ky)
{   unsigned long long h=14695981039346656037ull; int v[12]; LOGFONTA lf=t->lf; size_t fl=strlen(lf.lfFaceName);
    int ox=(t->kind==1?t->x:t->rc.left)+t->vorg.x, oy=(t->kind==1?t->y:t->rc.top)+t->vorg.y;
    memset(lf.lfFaceName+fl,0,sizeof lf.lfFaceName-fl);
    v[0]=t->kind; v[1]=(int)t->fmt; v[2]=(int)t->align; v[3]=t->rc.right-t->rc.left; v[4]=t->rc.bottom-t->rc.top; v[5]=t->bb.left-ox; v[6]=t->bb.top-oy;
    v[7]=t->bb.right-t->bb.left; v[8]=t->bb.bottom-t->bb.top; v[9]=t->hasDtp; v[10]=0; v[11]=t->n;
    h=TxMix(h,v,sizeof v); h=TxMix(h,&kx,sizeof kx); h=TxMix(h,&ky,sizeof ky); h=TxMix(h,&lf,sizeof lf); h=TxMix(h,t->str,t->n);
    if(t->hasDtp) h=TxMix(h,&t->dtp,sizeof t->dtp);
    return h; }
/* one text's coverage (w x h, from layer position R) blended in its colour into the premultiplied layer */
static void TxBlend(const TxItem *t,const BYTE *cov,int w,int h,RECT R,int W)
{   DWORD cr=GetRValue(t->col), cg=GetGValue(t->col), cb=GetBValue(t->col), lut[256]; int x, y, xe=R.left+w<R.right?R.left+w:R.right, ye=R.top+h<R.bottom?R.top+h:R.bottom;
#define TX_D255(v) (((v)+128+(((v)+128)>>8))>>8)   /* v / 255, rounded */
    for(x=0;x<256;x++) lut[x]=((DWORD)x<<24) | (TX_D255(cr*x)<<16) | (TX_D255(cg*x)<<8) | TX_D255(cb*x);   /* (the colour at coverage x, premultiplied) */
    for(y=R.top;y<ye;y++){ const BYTE *c=cov+(size_t)(y-R.top)*w; DWORD *a=g_txAcc+(size_t)y*W;
        for(x=R.left;x<xe;x++){ DWORD cv=c[x-R.left], o, ia; if(!cv) continue;
            o=a[x]; if(!o || cv==255){ a[x]=lut[cv]; continue; }   /* (nothing under it yet / fully covered) */
            {   DWORD l=lut[cv], ch[4], k; ia=255-cv;
                for(k=0;k<4;k++){ ch[k]=(l>>(k*8)&255)+TX_D255((o>>(k*8)&255)*ia); if(ch[k]>255) ch[k]=255; }
                a[x]=ch[0] | (ch[1]<<8) | (ch[2]<<16) | (ch[3]<<24); } } } }
static void TxDraw(const Surf *s)
{   float kx, ky; int W, H, i, j, live=0;
    if(!g_txLkInit || !TxEnabled() || !TxScale(s,&kx,&ky)) return;
    W=(int)(s->w*kx+0.5f); H=(int)(s->h*ky+0.5f); if(W<=0 || H<=0 || W>8192 || H>8192) return;
    TxLock();
    for(i=0;i<g_txN;i++) if(g_tx[i]->s==s){ live=1; break; }
    if(!live){ TxUnlock(); return; }
    if(g_txTexGen!=g_txGen || g_txTexSurf!=s || g_txTexW!=W || g_txTexH!=H || !g_txTex){
        HRGN clip; LARGE_INTEGER tq0, tq1, tqf; QueryPerformanceCounter(&tq0);
        if(!TxBuffers(W,H)){ TxUnlock(); return; }
        {   /* only what changed is cleared and uploaded: the area of this layer's texts and of the last one's (U, g_txDirty) */
            RECT U; int full=(g_txTexSurf!=s || g_txTexW!=W || g_txTexH!=H || !g_txTex), y; SetRectEmpty(&U);
            for(i=0;i<g_txN;i++) if(g_tx[i]->s==s){ const TxItem *t=g_tx[i]; RECT r;
                r.left=(int)((t->bb.left-1)*kx); r.top=(int)((t->bb.top-1)*ky); r.right=(int)((t->bb.right+1)*kx+0.999f); r.bottom=(int)((t->bb.bottom+1)*ky+0.999f); UnionRect(&U,&U,&r); }
            if(U.left<0) U.left=0; if(U.top<0) U.top=0; if(U.right>W) U.right=W; if(U.bottom>H) U.bottom=H;
            if(full) SetRect(&g_txUp,0,0,W,H); else UnionRect(&g_txUp,&U,&g_txDirty);
            for(y=g_txUp.top;y<g_txUp.bottom;y++) memset(g_txAcc+(size_t)y*W+g_txUp.left,0,(size_t)(g_txUp.right-g_txUp.left)*4);
            g_txDirty=U; }
        SetBkMode(g_txDcHi,TRANSPARENT); SetTextColor(g_txDcHi,RGB(255,255,255));
        for(i=0;i<g_txN;i++){ TxItem *t=g_tx[i]; RECT R, Ru; LOGFONTA lf; HFONT f, of; int x, y, clamped; unsigned long long key; TxTile *tile=NULL; BYTE *cov;
            if(t->s!=s) continue;
            for(j=i+1;j<g_txN;j++) if(g_tx[j]->s==s && TxSame(t,g_tx[j])) break; if(j<g_txN) continue;   /* (the same text drawn again on top: once is enough) */
            Ru.left=(int)((t->bb.left-1)*kx); Ru.top=(int)((t->bb.top-1)*ky); Ru.right=(int)((t->bb.right+1)*kx+0.999f); Ru.bottom=(int)((t->bb.bottom+1)*ky+0.999f);   /* (+ a source pixel: the sharp glyphs' edges) */
            R=Ru; if(R.left<0) R.left=0; if(R.top<0) R.top=0; if(R.right>W) R.right=W; if(R.bottom>H) R.bottom=H; if(R.right<=R.left || R.bottom<=R.top) continue;
            clamped=!EqualRect(&R,&Ru); key=TxKey(t,kx,ky);
            if(!clamped) for(j=0;j<TX_TILES;j++) if(g_txTiles[j].cov && g_txTiles[j].key==key){ tile=&g_txTiles[j]; break; }
            if(tile){ TxBlend(t,tile->cov,tile->w,tile->h,R,W); continue; }   /* (drawn before - the credits move the same lines every frame) */
            cov=(BYTE*)malloc((size_t)(R.right-R.left)*(R.bottom-R.top)); if(!cov) continue;
            {   /* the font at the window's scale, drawn at its own width, then resampled across to exactly the width the text had at 640 x 480 (hinting
                   makes small sizes narrower or wider than a plain scale), around the text's anchor - so the text covers what it covered, a wrap (the
                   rectangle is widened by the same factor) breaks where it broke, and what the game placed beside it with the small width still fits */
                float w1, wf=1.0f, ax; RECT rc=t->rc, E; DRAWTEXTPARAMS p=t->dtp;
                lf=t->lf; lf.lfQuality=ANTIALIASED_QUALITY; lf.lfHeight=(LONG)(lf.lfHeight*ky+(lf.lfHeight<0?-0.5f:0.5f)); lf.lfWidth=(LONG)(lf.lfWidth*kx+0.5f);
                f=CreateFontIndirectA(&lf); of=f?(HFONT)SelectObject(g_txDcHi,f):NULL;
                if(t->w0<0) t->w0=TxWidthLo(t);
                w1=TxWidth(g_txDcHi,t); if(t->w0>0 && w1>0){ wf=t->w0*kx/w1; if(wf<0.6f) wf=0.6f; if(wf>1.6f) wf=1.6f; }
                rc.left=(LONG)(rc.left*kx+0.5f); rc.right=(LONG)(rc.right*kx+0.5f); rc.top=(LONG)(rc.top*ky+0.5f); rc.bottom=(LONG)(rc.bottom*ky+0.5f);
                ax = t->kind==1 ? t->x*kx : ((t->fmt&DT_CENTER) ? (rc.left+rc.right)*0.5f : ((t->fmt&DT_RIGHT) ? (float)rc.right : (float)rc.left));   /* (in the DC's coordinates) */
                if(getenv("RR_HITEXT_LOG")) LOG("hitext: item '%s' kind %d bb %ld,%ld-%ld,%ld x %d fmt %x w0 %.1f hi %.1f factor %.3f font %s %ld",t->str,t->kind,t->bb.left,t->bb.top,t->bb.right,t->bb.bottom,t->x,t->fmt,t->w0,w1/kx,wf,t->lf.lfFaceName,t->lf.lfHeight);
                rc.left=(LONG)(ax+(rc.left-ax)/wf); rc.right=(LONG)(ax+(rc.right-ax)/wf+0.5f);
                ax+=t->vorg.x*kx;   /* (now in buffer pixels) */
                E.left=(LONG)(ax+(R.left-ax)/wf)-2; E.right=(LONG)(ax+(R.right-ax)/wf)+3; E.top=R.top; E.bottom=R.bottom;   /* the unsqueezed source of R */
                if(E.left<0) E.left=0; if(E.right>W) E.right=W;
                for(y=E.top;y<E.bottom;y++) memset(g_txCov+(size_t)y*W+E.left,0,(size_t)(E.right-E.left)*4);
                SetTextAlign(g_txDcHi,t->kind==1?t->align:(TA_LEFT|TA_TOP));
                SetViewportOrgEx(g_txDcHi,(int)(t->vorg.x*kx+0.5f),(int)(t->vorg.y*ky+0.5f),NULL);
                clip=CreateRectRgnIndirect(&E); SelectClipRgn(g_txDcHi,clip); DeleteObject(clip);
                if(t->kind==1) g_rTextOutA(g_txDcHi,(int)(t->x*kx+0.5f),(int)(t->y*ky+0.5f),t->str,t->n);
                else { char *br=TxBreakLo(t);   /* (a wrapped text: the game's own line breaks, the rectangle no longer breaks it) */
                    p.iLeftMargin=(int)(p.iLeftMargin*kx/wf+0.5f); p.iRightMargin=(int)(p.iRightMargin*kx/wf+0.5f);
                    if(br){ rc.right+=(rc.right-rc.left); g_rDrawTextExA(g_txDcHi,br,-1,&rc,t->fmt&~DT_WORDBREAK,t->hasDtp?&p:NULL); free(br); }
                    else g_rDrawTextExA(g_txDcHi,t->str,t->n,&rc,t->fmt,t->hasDtp?&p:NULL); }
                SetViewportOrgEx(g_txDcHi,0,0,NULL); SelectClipRgn(g_txDcHi,NULL);
                if(of) SelectObject(g_txDcHi,of); if(f) DeleteObject(f);
                GdiFlush();
                for(y=R.top;y<R.bottom;y++){ const DWORD *c=g_txCov+(size_t)y*W; BYTE *d=cov+(size_t)(y-R.top)*(R.right-R.left);
                    for(x=R.left;x<R.right;x++){ float sx=ax+(x+0.5f-ax)/wf-0.5f; int x0=(int)floorf(sx); float fr=sx-x0; DWORD v0, v1, cv;
                        v0=(x0>=E.left && x0<E.right)?c[x0]:0; v1=(x0+1>=E.left && x0+1<E.right)?c[x0+1]:0;
                        cv=(DWORD)((((v0>>16&255)+(v0>>8&255)+(v0&255))*(1.0f-fr)+((v1>>16&255)+(v1>>8&255)+(v1&255))*fr)/3.0f+0.5f);   /* (grey-scale: the channels agree) */
                        d[x-R.left]=(BYTE)(cv>255?255:cv); } } }
            TxBlend(t,cov,R.right-R.left,R.bottom-R.top,R,W);
            if(clamped) free(cov);   /* (cut by the layer's edge: not kept) */
            else { TxTile *n=&g_txTiles[g_txTileNext]; g_txTileNext=(g_txTileNext+1)%TX_TILES; free(n->cov); n->key=key; n->cov=cov; n->w=R.right-R.left; n->h=R.bottom-R.top; } }
        if(!g_txTex || g_txTexW!=W || g_txTexH!=H){ if(g_txTex) SDL_DestroyTexture(g_txTex); g_txTex=SDL_CreateTexture(g_ren,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,W,H);
            if(g_txTex){ SDL_SetTextureBlendMode(g_txTex,SDL_BLENDMODE_BLEND_PREMULTIPLIED); SDL_SetTextureScaleMode(g_txTex,SDL_SCALEMODE_LINEAR); } g_txTexW=W; g_txTexH=H; }
        if(g_txTex && !IsRectEmpty(&g_txUp)){ SDL_Rect ur; ur.x=g_txUp.left; ur.y=g_txUp.top; ur.w=g_txUp.right-g_txUp.left; ur.h=g_txUp.bottom-g_txUp.top;
            SDL_UpdateTexture(g_txTex,&ur,g_txAcc+(size_t)g_txUp.top*W+g_txUp.left,W*4); }
        g_txTexGen=g_txGen; g_txTexSurf=s;
        if(getenv("RR_HITEXT_LOG")){ QueryPerformanceCounter(&tq1); QueryPerformanceFrequency(&tqf); LOG("hitext: layer %dx%d rebuilt in %.2f ms",W,H,(tq1.QuadPart-tq0.QuadPart)*1000.0/tqf.QuadPart); } }
    TxUnlock();
    if(g_txTex) SDL_RenderTexture(g_ren,g_txTex,NULL,&g_fullRect); }
