/* "Game Options" screen (the Mods screen) (included by rrddraw.c after videocfg.inc): Restroom > Game Options (was Select Character - Big Game Mode goes to the character select by itself,
 * options.c).  The widescreen mod (the optional art folder mod\widescreen: menu side strips, upscaled sprites, HD skies, clouds) has no options any more
 * (2026-11-11, user): everything in it is on whenever the folder is there (menuwide.inc VidHdArt); the line under the title says whether it is installed.
 *
 *   Opponents           1..50 computer riders (14 = the original field, the default; Left / Right, Shift: 5 at a time) - src\race\rider_slots.c, RR_Opponents
 *   Traffic             Default (the original) / More / Heavy / Rush hour: cars spawn 1.5 / 2 / 3 times as often - srcoadoad_features.c, RR_Traffic
 *   Police              Default (the original) / Sometimes / Often / Relentless: a police rider every 90 / 45 / 20 s while none is about - src\system\game_step.c, RR_Police
 *   Course map          On (default) / Off - races: a map of the course right of the race view (gpuscene.inc; was in Video Options)
 *   Change clothes      opens the rider colour editor (ridercol.inc RR_RiderColours) - was a Restroom item
 *
 * Same look and keys as Video Options (videocfg.inc: VcState, VcMsg, the Ic* drawing helpers); saved to video.ini (winscale.inc).
 * RR_TEST_MODSCFG=<row> + RR_DUMP_DIR: that row selected, the screen saved as <RR_DUMP_DIR>\modscfg.bmp and closed (a picture for layout checks); with RR_DUMP_GPU
 * also the window as shown (gpu903.bmp: the readme picture).  RR_TEST_MODSCFG_OPEN=1 opens the screen by itself, like RR_TEST_VIDEOCFG=1 (McTestHook).
 */
#define MC_ROWS 7
#define MC_NVAL 5                                           /* rows with a value; then Reset to defaults, Done */
#define MC_Y0 104
#define MC_DY 26
#define MC_VX 236
enum { MC_OPP, MC_TRAFFIC, MC_POLICE, MC_MAP, MC_CLOTHES, MC_RESET, MC_DONE };
static const char *const g_mcLabel[MC_ROWS] = { "Opponents","Traffic","Police","Course map","Change clothes","Reset to defaults","Done" };
static int McRowY(int i){ return MC_Y0+i*MC_DY+(i>=MC_RESET?12:0); }
static int McIndent(int i){ (void)i; return 36; }

static void McValue(int row,char *b,size_t n)
{   switch(row){
    case MC_OPP: _snprintf(b,n,g_vid.opp==14?"%d  (the original field)":"%d",g_vid.opp); break;
    case MC_TRAFFIC: _snprintf(b,n,g_vid.traffic?"%s  (cars %s as often)":"%s  (the original)",g_trafficName[VidClamp(g_vid.traffic,0,3)],g_vid.traffic==1?"1.5 times":g_vid.traffic==2?"twice":"3 times"); break;
    case MC_POLICE: _snprintf(b,n,g_vid.police?"%s  (one every %d s)":"%s  (the original)",g_policeName[VidClamp(g_vid.police,0,3)],g_policeSecs[VidClamp(g_vid.police,0,3)]); break;
    case MC_MAP: _snprintf(b,n,"%s",g_vid.map?"On  (right of the race view)":"Off"); break;
    case MC_CLOTHES: _snprintf(b,n,"Press Enter  (your rider's colours)"); break;
    default: b[0]=0; }
    b[n-1]=0; }
static int McRowEnabled(int row){ (void)row; return 1; }

static void McAdjust(VcState *s,int row,int dir)
{   if(row==MC_OPP){ int st=(GetKeyState(VK_SHIFT)&0x8000)?5:1; g_vid.opp+=dir*st; if(g_vid.opp<1) g_vid.opp=50; if(g_vid.opp>50) g_vid.opp=1;   /* (wraps round) */
        VcMsg(s,g_vid.opp>14?"More riders than the original: extra ones get new names and random colours (from the next race)":"Applies from the next race"); VidSave(); return; }
    if(row==MC_POLICE){ g_vid.police=(g_vid.police+dir+4)%4; VcMsg(s,"Applies from the next race"); VidSave(); return; }   /* (wraps round) */
    if(row==MC_TRAFFIC){ g_vid.traffic=(g_vid.traffic+dir+4)%4; VcMsg(s,"Applies from the next race"); VidSave(); return; }   /* (wraps round) */
    if(row!=MC_MAP) return;   /* (Change clothes: Enter opens it, McActivate) */
    g_vid.map=!g_vid.map; VcMsg(s,"Applies from the next race");
    VidSave(); }
static void McReset(VcState *s){ g_vid.opp=14; g_vid.traffic=0; g_vid.police=0; g_vid.map=1; VidSave(); VcMsg(s,"Defaults restored"); }

static const char *const g_mcHelp[MC_ROWS][2] = {
    [MC_OPP]={ "How many computer riders race against you: 1 to 50 (Left / Right, Shift: 5 at a time).  14 is the original field;",
      "extra riders ride the original characters' bikes with names of their own and random colours.  The results list the first 15." },
    [MC_TRAFFIC]={ "How busy the roads are: Default is the original; More, Heavy and Rush hour make the cars - both ways -",
      "turn up 1.5, 2 and 3 times as often.  Applies from the next race." },
    [MC_POLICE]={ "Default is the original's police.  Sometimes, Often and Relentless send a police rider up behind you every",
      "90, 45 or 20 seconds while there is none about (one at a time).  Applies from the next race." },
    [MC_MAP]={ "A map of the whole course on the right of the race view: your place, the other riders,",
      "split roads, the finish line and how far it is.  Applies from the next race." },
    [MC_CLOTHES]={ "Your rider's colours: helmet, jacket, stripes, trousers and bike, each from the game's colour schemes or any colour",
      "(from the next race).  Enter opens the editor." },
    [MC_RESET]={ "Puts the options back to their defaults: 14 opponents, Default traffic and police, the course map on.", "" },
    [MC_DONE]={ "Saves everything and returns to the Restroom.", "" } };
static void McDraw(Surf *tmp,const Surf *base,const VcState *s,DWORD now)
{   HDC dc; HFONT title,row,fSm,fMid,old; int i; char t[128];
    memcpy(tmp->bits,base->bits,(size_t)tmp->pitch*tmp->h); SdDarken(tmp,0,0,tmp->w,tmp->h); SdDarken(tmp,8,8,632,472);
    S_GetDC(&tmp->itf,&dc); SetBkMode(dc,TRANSPARENT);
    title=CreateFontA(44,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_IMPACT)); row=CreateFontA(20,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_IMPACT));
    fSm=CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_ARIAL)); fMid=CreateFontA(15,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_ARIAL));
    old=(HFONT)SelectObject(dc,title); IcText(dc,18,8,"Game Options",RGB(255,96,40),TA_LEFT|TA_TOP);
    SelectObject(dc,fMid); IcText(dc,20,62,VidHdArtInstalled()?"Widescreen mod installed: widescreen menus, upscaled sprites and clouds are on":"Widescreen mod: not installed - copy the widescreen folder into the game's mod folder",RGB(190,190,215),TA_LEFT|TA_TOP);
    for(i=0;i<MC_ROWS;i++){ int y=McRowY(i), on=(i==s->sel), en=McRowEnabled(i), x=McIndent(i); COLORREF c=!en?RGB(110,110,130):(on?RGB(255,235,80):RGB(235,235,240));
        if(on){ POINT a[3]; a[0].x=x-20; a[0].y=y+4; a[1].x=x-20; a[1].y=y+16; a[2].x=x-10; a[2].y=y+10; IcPoly(dc,a,3,RGB(255,235,80),RGB(255,235,80)); }
        SelectObject(dc,row); IcText(dc,x,y,g_mcLabel[i],c,TA_LEFT|TA_TOP);
        if(i<MC_NVAL){ McValue(i,t,sizeof t); SelectObject(dc,fMid);
            if(on && en){ IcText(dc,MC_VX-18,y+2,"<",RGB(255,235,80),TA_LEFT|TA_TOP); IcText(dc,MC_VX,y+2,t,RGB(255,235,80),TA_LEFT|TA_TOP); { SIZE sz; GetTextExtentPoint32A(dc,t,(int)strlen(t),&sz); IcText(dc,MC_VX+sz.cx+8,y+2,">",RGB(255,235,80),TA_LEFT|TA_TOP); } }
            else IcText(dc,MC_VX,y+2,t,en?RGB(190,230,255):RGB(100,100,120),TA_LEFT|TA_TOP); } }
    SelectObject(dc,fSm); IcBox(dc,18,394,614,48,RGB(16,16,34),RGB(90,90,130),8);
    IcText(dc,28,400,g_mcHelp[s->sel][0],RGB(225,225,240),TA_LEFT|TA_TOP); IcText(dc,28,417,g_mcHelp[s->sel][1],RGB(225,225,240),TA_LEFT|TA_TOP);
    SelectObject(dc,fMid); if(s->msg[0] && now-s->msgT<3500) IcText(dc,18,446,s->msg,RGB(255,200,90),TA_LEFT|TA_TOP);
    SelectObject(dc,fSm); IcText(dc,18,462,"Up/Down: row   Left/Right or Enter: change   F11 / Alt+Enter: window <-> fullscreen   Esc: done",RGB(150,150,175),TA_LEFT|TA_TOP);
    SelectObject(dc,old); DeleteObject(title); DeleteObject(row); DeleteObject(fSm); DeleteObject(fMid); GdiFlush(); }

static int McRowAt(int x,int y){ int i; if(x<0 || x>=632) return -1; for(i=0;i<MC_ROWS;i++){ int yy=McRowY(i); if(y>=yy && y<yy+MC_DY-4) return i; } return -1; }
static int McActivate(VcState *s,int row)   /* 1 = finished */
{   if(row==MC_RESET){ McReset(s); return 0; } if(row==MC_DONE) return 1;
    if(row==MC_CLOTHES){ RR_RiderColours(); return 0; }   /* (the editor, then back here) */
    McAdjust(s,row,1); return 0; }
static int McKeyDown(VcState *s,int vk)
{   if(vk==VK_ESCAPE) return 1;
    if(vk==VK_UP){ s->sel=(s->sel+MC_ROWS-1)%MC_ROWS; return 0; } if(vk==VK_DOWN || vk==VK_TAB){ s->sel=(s->sel+1)%MC_ROWS; return 0; }
    if(vk==VK_LEFT || vk==VK_RIGHT){ McAdjust(s,s->sel,vk==VK_LEFT?-1:1); return 0; }
    if(vk==VK_RETURN || vk==VK_SPACE) return McActivate(s,s->sel);
    return 0; }

/* Restroom > Game Options (options.c rr_RestroomMods): always returns 1 (handled); 0 when the screen cannot be shown */
__declspec(dllexport) int __stdcall RR_ModsConfig(void)
{   Surf *tmp,base; MSG m; VcState s; int done=0, dirty=1, gpuShot=0; Surf *prevModal=g_modalSurf; DWORD lastDraw=0;
    if(!g_primary || !(tmp=NewSurf(g_primary->w,g_primary->h,16))) return 0;
    base=*tmp; base.bits=(BYTE*)malloc((size_t)tmp->pitch*tmp->h); if(!base.bits){ FreeSurf(tmp); return 0; }
    { Surf *keep=g_modalSurf; g_modalSurf=NULL; SdBaseFrame(&base); g_modalSurf=keep; }
    g_modalSurf=tmp; memset(&s,0,sizeof s); LOG("modscfg: open");
    while(!done){
        while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){
            if(m.message==WM_QUIT){ PostQuitMessage((int)m.wParam); done=1; break; }
            if(m.message==WM_SYSKEYDOWN && m.wParam==VK_RETURN){ VidToggleFullscreen(); dirty=1; continue; }
            if(m.message==WM_KEYDOWN && m.wParam==VK_F11){ VidToggleFullscreen(); dirty=1; continue; }
            if(m.message==WM_KEYDOWN || m.message==WM_SYSKEYDOWN){ if(McKeyDown(&s,(int)m.wParam)) done=1; dirty=1; continue; }
            if(MSG_LDOWN(m) || MSG_RDOWN(m)){ int x, y, r; MsgFramePos(&m,&x,&y); if((r=McRowAt(x,y))>=0){ s.sel=r; if(McActivate(&s,r)) done=1; dirty=1; } continue; }
            if(m.message==WM_MOUSEMOVE){ int x, y, r; MsgFramePos(&m,&x,&y); if(m.wParam==0 && (r=McRowAt(x,y))>=0 && r!=s.sel){ s.sel=r; dirty=1; } continue; }
            if(m.message==WM_KEYUP || m.message==WM_SYSKEYUP || m.message==WM_CHAR || m.message==WM_LBUTTONUP || m.message==WM_RBUTTONUP) continue;
            DispatchMessageA(&m); }
        if(GetTickCount()-lastDraw>250 || SgTickerUp()) dirty=1;
        if(dirty && !done){ McDraw(tmp,&base,&s,GetTickCount()); Present(tmp); dirty=0; lastDraw=GetTickCount();
            { const char *dd=getenv("RR_DUMP_DIR"), *t=getenv("RR_TEST_MODSCFG");
              if(t && dd && *t>='0' && *t<='9'){ char p[MAX_PATH]; int r=atoi(t); if(r>=0 && r<MC_ROWS && s.sel!=r){ s.sel=r; dirty=1; continue; } if(g_dumpGpu[0] && !gpuShot){ gpuShot=1; g_gpuReq=903; dirty=1; continue; } _snprintf(p,sizeof p,"%s\\modscfg.bmp",dd); p[sizeof p-1]=0; DumpBmp(tmp,p); done=1; } } }
        Sleep(15); }
    VidSave(); ModalEatClick();
    g_modalSurf=prevModal; g_lastShown=g_primary; free(base.bits); FreeSurf(tmp);
    if(g_hwnd) InvalidateRect(g_hwnd,NULL,FALSE);
    if(g_primary && !prevModal) Present(g_primary);
    LOG("modscfg: closed"); return 1; }

/* test hook: RR_TEST_MODSCFG_OPEN=1 opens the screen once, 4 s after the start of the time clock (as VcTestHook) */
static void McTestHook(void)
{   static int fired; char *e=getenv("RR_TEST_MODSCFG_OPEN");
    if(fired || !e || !g_primary || !g_clockMode) return; { DWORD fr=(GetTickCount()-g_t0)*30/1000; if(fr<120) return; }
    fired=1; RR_ModsConfig(); }
