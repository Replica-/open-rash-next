/* "Video Options" screen (included by rrddraw.c, after inputcfg.inc).
 *
 * Replaces the game's old Graphics Options page of the Restroom (src\game\legacy_b.c calls RR_VideoConfig for it; the old quality choices are gone, the game always runs at its highest quality).
 * Drawn over the darkened game picture, in the style of the Configure Input screen.  Every change is applied at once and written to video.ini (winscale.inc).
 *
 *   Display mode        Window / Borderless fullscreen / Exclusive fullscreen (switches the monitor to the chosen resolution)
 *   Window size         Automatic (largest whole multiple of 640x480 that fits) or a fixed size (4:3, 16:9, 16:10, 21:9 that fit the desktop) - Enter: the list; picking one switches to Window
 *   Fullscreen resolution   Desktop or any mode the monitor reports - Enter: the list; picking one switches to Exclusive fullscreen
 *   (no scaling / race view rows (2026-11-11, user): widescreen is automatic - menus Keep 4:3 with the widescreen mod's side strips when it is installed, races
 *   widescreen whenever the window is wider than 4:3; winscale.inc VidScale, rrddraw.c RR_RaceBegin)
 *   (the race world is always drawn by the GPU at the window's resolution, gpuscene.inc)
 *   Race 3D             Pseudo 3D (default: the game's own view redrawn by the GPU, original distance, the game's road forks) / True 3D (the race world as a real
 *                       3D world, world3d.inc, Far distance, every split road its left road only - RR_ForkPick, race/physics.c).  Test switch RR_WORLD3D=0 / 1
 *   (races show the traffic as its sprites - upscaled with the widescreen mod - and the HD sky picture where there is one, hdsky.inc - no rows; RR_CARS3D=1 = 3D car models, RR_HDSKY=0 = no HD sky)
 *   FPS counter         On / Off   (gpuscene.inc; the course map is on the Game Options screen, modscfg.inc)
 *   Smoothing           Sharp pixels / Smooth (bilinear)
 *   V-Sync              Off / On
 *
 *   Up/Down = row     Left/Right or Enter/click = change     Esc / Done = leave     F11 / Alt+Enter = window <-> fullscreen
 * RR_TEST_VIDEOCFG=1 opens the screen by itself after 4 s of the time clock (tests; needs RR_CLOCK=time).  With RR_DUMP_DIR too: row <n> of
 * RR_TEST_VIDEOCFG=<n> is selected, the screen saved as <RR_DUMP_DIR>\videocfg.bmp and closed (a picture of the screen for layout checks); with RR_DUMP_GPU
 * also the window as shown (gpu902.bmp: the readme picture).  RR_TEST_VIDEOCFG_LIST=1 with a size row (1 / 2): its list open in the picture.
 */
#define VC_ROWS 11
#define VC_NVAL 9                                        /* rows with a value; then Reset to defaults, Done */
#define VC_Y0 58
#define VC_DY 18
#define VC_HD 21                                                   /* room for a group heading */
#define VC_VX 236                                                  /* value column */
enum { VC_MODE, VC_WINSIZE, VC_FSRES, VC_SMOOTH, VC_VSYNC, VC_VIDEOS, VC_DETAIL, VC_FPS, VC_SEG, VC_RESET, VC_DONE };   /* in groups: Display, Menus, Races */
typedef struct VcState { int sel; DWORD msgT; char msg[96]; int list, lsel, ltop; } VcState;   /* list = the row whose size list is open (VC_WINSIZE / VC_FSRES; 0 = none), lsel / ltop = its selected / first shown entry */
typedef struct VcMode { int w, h, hz; } VcMode;
static VcMode g_vcFs[130]; static int g_vcNFs; static int g_vcWin[40][2], g_vcNWin;
static const char *const g_vcLabel[VC_ROWS] = { "Display mode","Window size","Fullscreen resolution","Smoothing","V-Sync","Videos","True 3D detail","FPS counter","Segment tracker","Reset to defaults","Done" };
/* the group headings: the first row of each group and its title */
static const struct { int row; const char *title; } g_vcGroup[3] = { { VC_MODE, "Display" }, { VC_VIDEOS, "Menus" }, { VC_DETAIL, "Races" } };
static int VcRowY(int i){ return VC_Y0+i*VC_DY+VC_HD+(i>=VC_VIDEOS?VC_HD:0)+(i>=VC_DETAIL?VC_HD:0)+(i>=VC_RESET?8:0); }

static void VcMsg(VcState *s,const char *t){ strncpy(s->msg,t,sizeof s->msg-1); s->msg[sizeof s->msg-1]=0; s->msgT=GetTickCount(); }

static void VcBuildLists(void)
{   static const int pre[][2] = { {640,480},{800,600},{960,720},{1024,768},{1280,720},{1280,800},{1280,960},{1366,768},{1440,900},{1440,1080},{1600,900},{1600,1200},{1680,1050},
                                  {1920,1080},{1920,1200},{1920,1440},{2560,1080},{2560,1440},{2560,1600},{2560,1920},{3200,1800},{3200,2400},{3440,1440},{3840,1600},{3840,2160},{3840,2880} };   /* 4:3, 16:9, 16:10, 21:9 - by width */
    MONITORINFOEXA mi; DEVMODEA dm; int i, j, ok=VidMonitor(&mi); RECT wa; const char *dev=ok?mi.szDevice:NULL;
    if(ok) wa=mi.rcWork; else { wa.left=0; wa.top=0; wa.right=GetSystemMetrics(SM_CXSCREEN); wa.bottom=GetSystemMetrics(SM_CYSCREEN); }
    g_vcNWin=0; g_vcWin[g_vcNWin][0]=0; g_vcWin[g_vcNWin][1]=0; g_vcNWin++;
    for(i=0;i<(int)(sizeof pre/sizeof pre[0]);i++) if(VidFitsWork(pre[i][0],pre[i][1],&wa)){ g_vcWin[g_vcNWin][0]=pre[i][0]; g_vcWin[g_vcNWin][1]=pre[i][1]; g_vcNWin++; }
    if(g_vid.winW>0){ for(i=0;i<g_vcNWin;i++) if(g_vcWin[i][0]==g_vid.winW && g_vcWin[i][1]==g_vid.winH) break; if(i==g_vcNWin && g_vcNWin<40){ g_vcWin[g_vcNWin][0]=g_vid.winW; g_vcWin[g_vcNWin][1]=g_vid.winH; g_vcNWin++; } }
    g_vcNFs=0; g_vcFs[g_vcNFs].w=0; g_vcFs[g_vcNFs].h=0; g_vcFs[g_vcNFs].hz=0; g_vcNFs++;                                  /* 0 = desktop */
    for(i=0;;i++){ memset(&dm,0,sizeof dm); dm.dmSize=sizeof dm; if(!EnumDisplaySettingsA(dev,i,&dm)) break;
        if(dm.dmBitsPerPel!=32 || dm.dmPelsWidth<640 || dm.dmPelsHeight<480) continue;
        for(j=1;j<g_vcNFs;j++) if(g_vcFs[j].w==(int)dm.dmPelsWidth && g_vcFs[j].h==(int)dm.dmPelsHeight) break;
        if(j==g_vcNFs){ if(g_vcNFs>=(int)(sizeof g_vcFs/sizeof g_vcFs[0])) continue; g_vcFs[j].w=(int)dm.dmPelsWidth; g_vcFs[j].h=(int)dm.dmPelsHeight; g_vcFs[j].hz=0; g_vcNFs++; }
        if((int)dm.dmDisplayFrequency>g_vcFs[j].hz) g_vcFs[j].hz=(int)dm.dmDisplayFrequency; }
    for(i=2;i<g_vcNFs;i++){ VcMode t=g_vcFs[i]; for(j=i-1;j>=1 && (g_vcFs[j].w>t.w || (g_vcFs[j].w==t.w && g_vcFs[j].h>t.h));j--) g_vcFs[j+1]=g_vcFs[j]; g_vcFs[j+1]=t; }
    if(g_vid.fsW>0){ for(i=1;i<g_vcNFs;i++) if(g_vcFs[i].w==g_vid.fsW && g_vcFs[i].h==g_vid.fsH) break; if(i==g_vcNFs && g_vcNFs<(int)(sizeof g_vcFs/sizeof g_vcFs[0])){ g_vcFs[g_vcNFs].w=g_vid.fsW; g_vcFs[g_vcNFs].h=g_vid.fsH; g_vcFs[g_vcNFs].hz=g_vid.fsHz; g_vcNFs++; } } }
static int VcWinIdx(void){ int i; if(g_vid.winW<=0) return 0; for(i=0;i<g_vcNWin;i++) if(g_vcWin[i][0]==g_vid.winW && g_vcWin[i][1]==g_vid.winH) return i; return 0; }
static int VcFsIdx(void){ int i; if(g_vid.fsW<=0) return 0; for(i=1;i<g_vcNFs;i++) if(g_vcFs[i].w==g_vid.fsW && g_vcFs[i].h==g_vid.fsH) return i; return 0; }

/* size of the area the picture is shown in, and the picture inside it (the same rules as WsCompute) */
static void VcTarget(int *tw,int *th)
{   MONITORINFOEXA mi; RECT cr;
    if(g_hwnd && !g_headless && !g_quiet && g_vidCur==g_vid.mode && GetClientRect(g_hwnd,&cr) && cr.right>0){ *tw=cr.right; *th=cr.bottom; return; }
    if(VidMonitor(&mi)){
        if(g_vid.mode==VM_WINDOW){ VidWinClient(&mi.rcWork,tw,th); return; }
        if(g_vid.mode==VM_EXCLUSIVE && g_vid.fsW>0){ *tw=g_vid.fsW; *th=g_vid.fsH; return; }
        *tw=mi.rcMonitor.right-mi.rcMonitor.left; *th=mi.rcMonitor.bottom-mi.rcMonitor.top; return; }
    *tw=g_wsCw; *th=g_wsCh; }

static void VcValue(int row,char *b,size_t n)
{   static const char *const modeN[3]={"Window","Borderless fullscreen","Exclusive fullscreen"};
    switch(row){
    case VC_MODE: _snprintf(b,n,"%s",modeN[g_vid.mode]); break;
    case VC_WINSIZE: if(g_vid.winW>0) _snprintf(b,n,"%d x %d",g_vid.winW,g_vid.winH); else { int tw,th; MONITORINFOEXA mi; if(VidMonitor(&mi)){ VidWinClient(&mi.rcWork,&tw,&th); _snprintf(b,n,"Automatic (%d x %d)",tw,th); } else _snprintf(b,n,"Automatic"); } break;
    case VC_FSRES: if(g_vid.fsW>0) { int i=VcFsIdx(); if(i>0 && g_vcFs[i].hz>0) _snprintf(b,n,"%d x %d @ %d Hz",g_vid.fsW,g_vid.fsH,g_vcFs[i].hz); else _snprintf(b,n,"%d x %d",g_vid.fsW,g_vid.fsH); } else _snprintf(b,n,"Desktop resolution"); break;
    case VC_FPS: _snprintf(b,n,"%s",g_vid.fps?"On  (top right)":"Off"); break;
    case VC_DETAIL: { static const char *const pn[3]={"High  (RTX 3080)","Steam Deck","Custom"}; _snprintf(b,n,"%s   - Enter: all the settings",pn[VidGfxWhich()]); } break;
    case VC_SEG: _snprintf(b,n,"%s",g_vid.segpos?"On  (top left: track, segment, lane)":"Off"); break;
    case VC_SMOOTH: _snprintf(b,n,"%s",g_vid.filter?"Smooth":"Sharp pixels"); break;
    case VC_VSYNC: _snprintf(b,n,"%s",g_vid.vsync?"On":"Off"); break;
    case VC_VIDEOS: _snprintf(b,n,"%s",g_vid.videos?"On  (intro, race and Jukebox videos)":"Off"); break;
    default: b[0]=0; }
    b[n-1]=0; }
static int VcRowEnabled(int row){ (void)row; return 1; }   /* (the size rows switch the display mode themselves: VcListPick) */

/* ---- the size lists (Window size / Fullscreen resolution): Enter or a click opens the row's whole list; picking an entry applies it and switches to the
   display mode it belongs to (a window size -> Window, a fullscreen resolution -> Exclusive fullscreen); Left / Right step through the same list ---- */
#define VC_LVIS 15                                                 /* entries shown at once (the rest scroll) */
#define VC_LX 220
#define VC_LY 64
#define VC_LW 300
#define VC_LDY 18
static int VcListN(int row){ return row==VC_WINSIZE ? g_vcNWin : g_vcNFs; }
static const char *VcAspect(int w,int h)
{   int r=h>0?w*100/h:0;
    return r==133?"4:3":(r==125?"5:4":(r==160?"16:10":((r>=177 && r<=178)?"16:9":(r>=233 && r<=240?"21:9":"")))); }
/* entry i of a list: its text, and its aspect ratio label for the column beside it ("" for the Automatic / Desktop entry) */
static const char *VcListText(int row,int i,char *b,size_t n)
{   if(row==VC_WINSIZE){
        if(i==0){ int tw,th; MONITORINFOEXA mi; if(VidMonitor(&mi)){ VidWinClient(&mi.rcWork,&tw,&th); _snprintf(b,n,"Automatic  (%d x %d)",tw,th); } else _snprintf(b,n,"Automatic"); b[n-1]=0; return ""; }
        _snprintf(b,n,"%d x %d",g_vcWin[i][0],g_vcWin[i][1]); b[n-1]=0; return VcAspect(g_vcWin[i][0],g_vcWin[i][1]); }
    if(i==0){ _snprintf(b,n,"Desktop resolution"); b[n-1]=0; return ""; }
    if(g_vcFs[i].hz>0) _snprintf(b,n,"%d x %d @ %d Hz",g_vcFs[i].w,g_vcFs[i].h,g_vcFs[i].hz); else _snprintf(b,n,"%d x %d",g_vcFs[i].w,g_vcFs[i].h);
    b[n-1]=0; return VcAspect(g_vcFs[i].w,g_vcFs[i].h); }
static void VcListPick(VcState *s,int row,int i)
{   char t[96];
    if(row==VC_WINSIZE){ g_vid.winW=g_vcWin[i][0]; g_vid.winH=g_vcWin[i][1]; g_vid.mode=VM_WINDOW; VidApply(); VcMsg(s,g_vidErr[0]?g_vidErr:"Window size changed"); }
    else { g_vid.fsW=g_vcFs[i].w; g_vid.fsH=g_vcFs[i].h; g_vid.fsHz=g_vcFs[i].hz; g_vid.mode=VM_EXCLUSIVE; g_vid.fsPref=VM_EXCLUSIVE; VidApply();
        if(g_vidErr[0]) VcMsg(s,g_vidErr); else { _snprintf(t,sizeof t,"%s",g_vid.fsW?"Exclusive fullscreen at this resolution":"Exclusive fullscreen at the desktop resolution"); VcMsg(s,t); } }
    VidSave(); }
static void VcListShow(VcState *s)   /* keeps the selected entry in view */
{   int n=VcListN(s->list);
    if(s->lsel<0) s->lsel=0; if(s->lsel>=n) s->lsel=n-1;
    if(s->lsel<s->ltop) s->ltop=s->lsel; if(s->lsel>=s->ltop+VC_LVIS) s->ltop=s->lsel-VC_LVIS+1;
    if(s->ltop>n-VC_LVIS) s->ltop=n-VC_LVIS; if(s->ltop<0) s->ltop=0; }
static void VcListOpen(VcState *s,int row){ VcBuildLists(); s->list=row; s->lsel=row==VC_WINSIZE?VcWinIdx():VcFsIdx(); s->ltop=s->lsel-VC_LVIS/2; VcListShow(s); }
static int VcListH(int n){ return 26+(n<VC_LVIS?n:VC_LVIS)*VC_LDY+6; }
/* entry under frame position x, y: its index; -2 = inside the box but no entry; -1 = outside */
static int VcListAt(const VcState *s,int x,int y)
{   int n=VcListN(s->list), k;
    if(x<VC_LX || x>=VC_LX+VC_LW || y<VC_LY || y>=VC_LY+VcListH(n)) return -1;
    k=(y-VC_LY-26)/VC_LDY; if(y<VC_LY+26 || k<0 || k>=VC_LVIS || s->ltop+k>=n) return -2;
    return s->ltop+k; }
static void VcListDraw(HDC dc,const VcState *s,HFONT fMid,HFONT fSm)
{   int n=VcListN(s->list), k; char t[96];
    IcBox(dc,VC_LX,VC_LY,VC_LW,VcListH(n),RGB(14,14,30),RGB(255,235,80),8);
    SelectObject(dc,fSm); IcText(dc,VC_LX+10,VC_LY+6,s->list==VC_WINSIZE?"Window size":"Fullscreen resolution",RGB(255,150,90),TA_LEFT|TA_TOP);
    if(s->ltop>0) IcText(dc,VC_LX+VC_LW-12,VC_LY+6,"^",RGB(190,190,215),TA_RIGHT|TA_TOP);
    if(s->ltop+VC_LVIS<n) IcText(dc,VC_LX+VC_LW-24,VC_LY+6,"more v",RGB(190,190,215),TA_RIGHT|TA_TOP);
    SelectObject(dc,fMid);
    for(k=0;k<VC_LVIS && s->ltop+k<n;k++){ int i=s->ltop+k, y=VC_LY+26+k*VC_LDY, cur=(s->list==VC_WINSIZE?VcWinIdx():VcFsIdx())==i;
        if(i==s->lsel) IcBox(dc,VC_LX+4,y-1,VC_LW-8,VC_LDY,RGB(70,60,20),RGB(70,60,20),4);
        { const char *a=VcListText(s->list,i,t,sizeof t); COLORREF c=i==s->lsel?RGB(255,235,80):(cur?RGB(160,255,160):RGB(225,225,240));
          IcText(dc,VC_LX+14,y+1,t,c,TA_LEFT|TA_TOP); if(a[0]) IcText(dc,VC_LX+175,y+1,a,i==s->lsel?c:RGB(150,170,200),TA_LEFT|TA_TOP); }
        if(cur) IcText(dc,VC_LX+VC_LW-14,y+1,"current",RGB(160,255,160),TA_RIGHT|TA_TOP); } }
/* keys while a list is open; returns 1 = handled */
static int VcListKey(VcState *s,int vk)
{   int n=VcListN(s->list);
    switch(vk){
    case VK_ESCAPE: s->list=0; return 1;
    case VK_UP: s->lsel--; break; case VK_DOWN: case VK_TAB: s->lsel++; break;
    case VK_PRIOR: s->lsel-=VC_LVIS-1; break; case VK_NEXT: s->lsel+=VC_LVIS-1; break;
    case VK_HOME: s->lsel=0; break; case VK_END: s->lsel=n-1; break;
    case VK_RETURN: case VK_SPACE: { int row=s->list; s->list=0; VcListPick(s,row,s->lsel); return 1; }
    default: return 1; }
    VcListShow(s); return 1; }

/* ---- Video Options > True 3D detail (user, 2026-11-14: "video options level of detail setting and draw distance 'Optimised for Steam Deck' ... expose
   options like all distance rendering options ... a preset for '3080 GPU' vs 'Steam deck'"): one page of the True 3D detail settings (winscale.inc
   VidGfxPreset - the values, what they change: world3d.inc / render3d.h), a preset row on top, Back.  Left / Right change a row, Enter opens / leaves */
enum { GD_PRESET, GD_DIST, GD_GRASS, GD_GDIST, GD_OSHADOW, GD_REFL, GD_INTERIOR, GD_GROUND, GD_HALOS, GD_WXFX, GD_GLARE, GD_SPRAY, GD_SCALE, GD_BACK, GD_ROWS };
static const char *const g_gdLabel[GD_ROWS] = { "Preset","Draw distance","Grass","Effects distance","Object shadows","Puddle reflections","Window interiors","Ground detail","Light halos","Weather particles","Sun glare","Spray and splashes","3D resolution","Back" };
static const char *const g_gdHelp[GD_ROWS][2] = {
    [GD_PRESET]={ "High (RTX 3080): everything on, the far view.  Steam Deck: the near view with every optional effect off",
      "- turn on what the Deck can afford.  Changing any row below makes it Custom." },
    [GD_DIST]={ "How far ahead the 3D world is drawn: near 70, medium 105, far 150 road segments.  The biggest cost: buildings,",
      "trees and traffic out to there.  The world fades into the sky at the end either way." },
    [GD_GRASS]={ "Tufts of grass on the green ground, thickest by the road.  Low draws 40 % of them (14000 at most), high 35000.",
      "" },
    [GD_GDIST]={ "How far out the effects reach - all of them alike: the grass, the rooms behind the windows, the windows' depth,",
      "the walls' relief, rain on the road." },
    [GD_HALOS]={ "The soft glow round street lamps, lit windows and headlights at night and in fog.",
      "The lights still light the road with this off." },
    [GD_WXFX]={ "The rain and snow falling in front of you: low draws 40 % of the drops (torrential rain draws 9600).",
      "Off: wet roads and puddles, no drops." },
    [GD_GLARE]={ "The lens flare when you look towards the sun.",
      "" },
    [GD_SPRAY]={ "Water from the tyres in puddles, dirt and grass thrown up off the road, the water of broken fire hydrants.",
      "" },
    [GD_OSHADOW]={ "Shadows of trees, lamp posts and poles on the ground in the sun.",
      "" },
    [GD_REFL]={ "In the rain the puddles mirror the buildings and the sky: the world drawn twice.  Off: they show the sky only.",
      "" },
    [GD_INTERIOR]={ "Rooms behind the City's windows (walls, floors, lamps, shop shelves) instead of flat glass.",
      "Lit at night either way." },
    [GD_GROUND]={ "Rocks on the dirt, fallen leaves round the street trees, dry scrub.  From the next race.",
      "" },
    [GD_SCALE]={ "The 3D picture drawn with fewer pixels and stretched to the window: 50 % is a quarter of the work.",
      "The HUD and the riders' sprites stay sharp." },
    [GD_BACK]={ "Back to Video Options (everything is saved).", "" } };
static int GdRowY(int i){ return VC_Y0+28+i*VC_DY+(i>=GD_DIST?12:0)+(i>=GD_BACK?10:0); }
static void GdValue(int row,char *b,size_t n)
{   static const char *const pn[3]={"High  (RTX 3080)","Steam Deck","Custom"}, *const dn[3]={"Near","Medium","Far"}, *const gn[3]={"Off","Low","High"}, *const sn[3]={"50 %","75 %","100 %"};
    switch(row){
    case GD_PRESET: _snprintf(b,n,"%s",pn[VidGfxWhich()]); break;
    case GD_DIST: _snprintf(b,n,"%s",dn[g_vid.dist]); break;
    case GD_GRASS: _snprintf(b,n,"%s",gn[g_vid.grass]); break;
    case GD_GDIST: _snprintf(b,n,"%s",dn[g_vid.gdist]); break;
    case GD_HALOS: _snprintf(b,n,"%s",g_vid.halos?"On":"Off"); break;
    case GD_WXFX: _snprintf(b,n,"%s",gn[g_vid.wxfx]); break;
    case GD_GLARE: _snprintf(b,n,"%s",g_vid.glare?"On":"Off"); break;
    case GD_SPRAY: _snprintf(b,n,"%s",g_vid.spray?"On":"Off"); break;
    case GD_OSHADOW: _snprintf(b,n,"%s",g_vid.oshadow?"On":"Off"); break;
    case GD_REFL: _snprintf(b,n,"%s",g_vid.refl?"On":"Off"); break;
    case GD_INTERIOR: _snprintf(b,n,"%s",g_vid.interior?"On":"Off"); break;
    case GD_GROUND: _snprintf(b,n,"%s",g_vid.ground?"On":"Off"); break;
    case GD_SCALE: _snprintf(b,n,"%s",sn[g_vid.rscale]); break;
    default: b[0]=0; } }
static void GdAdjust(int row,int dir)
{   switch(row){
    case GD_PRESET: { int p=VidGfxWhich(); VidGfxPreset(p==2 ? (dir>0?0:1) : !p); } break;   /* (High <-> Steam Deck; from Custom: High right, Steam Deck left) */
    case GD_DIST: g_vid.dist=(g_vid.dist+dir+3)%3; break;
    case GD_GRASS: g_vid.grass=(g_vid.grass+dir+3)%3; break;
    case GD_GDIST: g_vid.gdist=(g_vid.gdist+dir+3)%3; break;
    case GD_HALOS: g_vid.halos^=1; break;
    case GD_WXFX: g_vid.wxfx=(g_vid.wxfx+dir+3)%3; break;
    case GD_GLARE: g_vid.glare^=1; break;
    case GD_SPRAY: g_vid.spray^=1; break;
    case GD_OSHADOW: g_vid.oshadow^=1; break;
    case GD_REFL: g_vid.refl^=1; break;
    case GD_INTERIOR: g_vid.interior^=1; break;
    case GD_GROUND: g_vid.ground^=1; break;
    case GD_SCALE: g_vid.rscale=(g_vid.rscale+dir+3)%3; break;
    default: return; }
    VidSave(); }
static void GdDraw(Surf *tmp,const Surf *base,int sel)
{   HDC dc; HFONT title,row,fSm,fMid,old; int i; char t[96];
    memcpy(tmp->bits,base->bits,(size_t)tmp->pitch*tmp->h); SdDarken(tmp,0,0,tmp->w,tmp->h); SdDarken(tmp,8,8,632,472);
    S_GetDC(&tmp->itf,&dc); SetBkMode(dc,TRANSPARENT);
    title=CreateFontA(44,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_IMPACT)); row=CreateFontA(20,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_IMPACT));
    fSm=CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_ARIAL)); fMid=CreateFontA(15,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_ARIAL));
    old=(HFONT)SelectObject(dc,title); IcText(dc,18,8,"True 3D Detail",RGB(255,96,40),TA_LEFT|TA_TOP);
    SelectObject(dc,fSm); IcText(dc,22,58,"Changes apply from the next race on.",RGB(170,170,195),TA_LEFT|TA_TOP);
    for(i=0;i<GD_ROWS;i++){ int y=GdRowY(i), on=(i==sel), x=(i<GD_BACK)?40:20; COLORREF c=on?RGB(255,235,80):RGB(235,235,240);
        if(on){ POINT a[3]; a[0].x=x-20; a[0].y=y+4; a[1].x=x-20; a[1].y=y+16; a[2].x=x-10; a[2].y=y+10; IcPoly(dc,a,3,RGB(255,235,80),RGB(255,235,80)); }
        SelectObject(dc,row); IcText(dc,x,y,g_gdLabel[i],c,TA_LEFT|TA_TOP);
        if(i<GD_BACK){ GdValue(i,t,sizeof t); SelectObject(dc,fMid);
            if(on){ SIZE sz; IcText(dc,VC_VX-18,y+2,"<",RGB(255,235,80),TA_LEFT|TA_TOP); IcText(dc,VC_VX,y+2,t,RGB(255,235,80),TA_LEFT|TA_TOP);
                GetTextExtentPoint32A(dc,t,(int)strlen(t),&sz); IcText(dc,VC_VX+sz.cx+8,y+2,">",RGB(255,235,80),TA_LEFT|TA_TOP); }
            else IcText(dc,VC_VX,y+2,t,i==GD_PRESET?RGB(255,200,120):RGB(190,230,255),TA_LEFT|TA_TOP); } }
    SelectObject(dc,fSm); IcBox(dc,18,394,614,48,RGB(16,16,34),RGB(90,90,130),8);
    IcText(dc,28,400,g_gdHelp[sel][0],RGB(225,225,240),TA_LEFT|TA_TOP); IcText(dc,28,417,g_gdHelp[sel][1],RGB(225,225,240),TA_LEFT|TA_TOP);
    IcText(dc,18,462,"Up/Down: row   Left/Right or a click: change   Enter on Back or Esc: back to Video Options",RGB(150,150,175),TA_LEFT|TA_TOP);
    SelectObject(dc,old); DeleteObject(title); DeleteObject(row); DeleteObject(fSm); DeleteObject(fMid); GdiFlush(); }
static int GdRowAt(int x,int y){ int i; if(x<0 || x>=632) return -1; for(i=0;i<GD_ROWS;i++){ int yy=GdRowY(i); if(y>=yy && y<yy+VC_DY-4) return i; } return -1; }
static void VcDetailPage(void)   /* the page, over the screen behind Video Options; returns on Back / Esc */
{   Surf *tmp=g_modalSurf, base; MSG m; int sel=0, done=0, dirty=1;
    if(!tmp) return;
    base=*tmp; base.bits=(BYTE*)malloc((size_t)tmp->pitch*tmp->h); if(!base.bits) return;
    { Surf *keep=g_modalSurf; g_modalSurf=NULL; SdBaseFrame(&base); g_modalSurf=keep; }
    LOG("videocfg: True 3D detail");
    while(!done){
        while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){
            if(m.message==WM_QUIT){ PostQuitMessage((int)m.wParam); done=1; break; }
            if(m.message==WM_SYSKEYDOWN && m.wParam==VK_RETURN){ VidToggleFullscreen(); dirty=1; continue; }
            if(m.message==WM_KEYDOWN){ int vk=(int)m.wParam; dirty=1;
                if(vk==VK_ESCAPE) done=1;
                else if(vk==VK_F11) VidToggleFullscreen();
                else if(vk==VK_UP) sel=(sel+GD_ROWS-1)%GD_ROWS; else if(vk==VK_DOWN || vk==VK_TAB) sel=(sel+1)%GD_ROWS;
                else if(vk==VK_LEFT) GdAdjust(sel,-1); else if(vk==VK_RIGHT) GdAdjust(sel,1);
                else if(vk==VK_RETURN || vk==VK_SPACE){ if(sel==GD_BACK) done=1; else GdAdjust(sel,1); }
                continue; }
            if(MSG_LDOWN(m) || MSG_RDOWN(m)){ int x, y, r; MsgFramePos(&m,&x,&y); if((r=GdRowAt(x,y))>=0){ sel=r; if(r==GD_BACK) done=1; else GdAdjust(r,MSG_RDOWN(m)?-1:1); dirty=1; } continue; }
            if(m.message==WM_MOUSEMOVE){ int x, y, r; MsgFramePos(&m,&x,&y); if(m.wParam==0 && (r=GdRowAt(x,y))>=0 && r!=sel){ sel=r; dirty=1; } continue; }
            if(m.message==WM_KEYUP || m.message==WM_SYSKEYUP || m.message==WM_CHAR || m.message==WM_LBUTTONUP || m.message==WM_RBUTTONUP || m.message==WM_MOUSEWHEEL) continue;
            DispatchMessageA(&m); }
        if(dirty && !done){ GdDraw(tmp,&base,sel); Present(tmp); dirty=0;
            { const char *dd=getenv("RR_DUMP_DIR"), *t=getenv("RR_TEST_GFXCFG");   /* test: RR_TEST_GFXCFG=<row> + RR_DUMP_DIR: that row selected, the page saved as gfxcfg.bmp, closed */
              if(t && dd){ char p[MAX_PATH]; int r=atoi(t); if(r>=0 && r<GD_ROWS && sel!=r){ sel=r; dirty=1; continue; } _snprintf(p,sizeof p-1,"%s\\gfxcfg.bmp",dd); p[sizeof p-1]=0; DumpBmp(tmp,p); done=1; } } }
        Sleep(15); }
    VidSave(); free(base.bits); ModalEatClick(); }
static void VcAdjust(VcState *s,int row,int dir)
{   int i; if(row<0 || row>=VC_NVAL) return;
    switch(row){
    case VC_MODE: g_vid.mode=(g_vid.mode+dir+3)%3; if(g_vid.mode!=VM_WINDOW) g_vid.fsPref=g_vid.mode; VidApply(); if(g_vidErr[0]) VcMsg(s,g_vidErr); break;
    case VC_WINSIZE: i=(VcWinIdx()+dir+g_vcNWin)%g_vcNWin; VcListPick(s,row,i); return;
    case VC_FSRES: i=(VcFsIdx()+dir+g_vcNFs)%g_vcNFs; VcListPick(s,row,i); return;
    case VC_DETAIL: { int p=VidGfxWhich(); VidGfxPreset(p==2 ? (dir>0?0:1) : !p); VcMsg(s,VidGfxWhich()?"Steam Deck preset  (from the next race)":"High preset  (from the next race)"); } break;
    case VC_FPS: g_vid.fps=!g_vid.fps; break;
    case VC_SEG: g_vid.segpos=!g_vid.segpos; break;
    case VC_SMOOTH: g_vid.filter=!g_vid.filter; break;
    case VC_VSYNC: g_vid.vsync=!g_vid.vsync; break;
    case VC_VIDEOS: g_vid.videos=!g_vid.videos; break; }
    VidSave(); }
static void VcReset(VcState *s)
{   g_vid.mode=VM_WINDOW; g_vid.winW=g_vid.winH=0; g_vid.fsW=g_vid.fsH=g_vid.fsHz=0; g_vid.fps=1; g_vid.segpos=0; g_vid.true3d=1; g_vid.videos=0; g_vid.filter=0; g_vid.vsync=0; g_vid.fsPref=VM_BORDERLESS; VidGfxPreset(VidOnDeck()); VidApply(); VidSave(); VcMsg(s,"Defaults restored"); }

/* ---- drawing ---- */
static const char *const g_vcHelp[VC_ROWS][2] = {
    [VC_MODE]={ "Window: a normal window.  Borderless fullscreen: covers the desktop, switches instantly.",
      "Exclusive fullscreen: switches the monitor to the resolution below.  F11 / Alt+Enter toggle." },
    [VC_WINSIZE]={ "Size of the game window: Enter or a click lists them all (picking one switches to a window).  Automatic picks",
      "the largest whole multiple of 640 x 480 that fits the desktop.  You can also drag the window edges at any time." },
    [VC_FSRES]={ "Resolution for exclusive fullscreen: Enter or a click lists every mode of the monitor (picking one switches to",
      "exclusive fullscreen at it).  Desktop keeps the current mode.  Lower resolutions can run faster on slow machines." },
    [VC_SMOOTH]={ "Sharp pixels keeps the hard edges of the original graphics.  Smooth blends them when scaled up.",
      "" },
    [VC_FPS]={ "Shows the frames per second in the top right corner of the race view.",
      "" },
    [VC_DETAIL]={ "How much the True 3D world draws: distance, grass, shadows, reflections, window rooms, ground detail, resolution.",
      "Left / Right: the High (RTX 3080) or the Steam Deck preset.  Enter: every setting on its own." },
    [VC_SEG]={ "Shows where you are on the course, top left during a race: the track, the road segment (SEG, as the test tools use",
      "it) and the lateral position.  For testing and reporting places on the road; off for normal play." },
    [VC_VSYNC]={ "Waits for the monitor's refresh before showing each frame: avoids tearing.  Turn it off if the",
      "game feels sluggish." },
    [VC_VIDEOS]={ "The game's full-motion videos: the intro, the win / lose / busted clips and the Jukebox rock videos (VIDEO_UP).",
      "Off skips them all.  Esc, Enter, Space or a click skips one while it plays." },
    [VC_RESET]={ "Puts all the video options back to their defaults.", "" },
    [VC_DONE]={ "Saves everything and returns to the Restroom.", "" } };
static void VcDraw(Surf *tmp,const Surf *base,const VcState *s,DWORD now)
{   HDC dc; HFONT title,row,fSm,fMid,old; int i; char t[96];
    memcpy(tmp->bits,base->bits,(size_t)tmp->pitch*tmp->h); SdDarken(tmp,0,0,tmp->w,tmp->h); SdDarken(tmp,8,8,632,472);
    S_GetDC(&tmp->itf,&dc); SetBkMode(dc,TRANSPARENT);
    title=CreateFontA(44,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_IMPACT)); row=CreateFontA(20,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_IMPACT));
    fSm=CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_ARIAL)); fMid=CreateFontA(15,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,RrFont(RR_FONT_ARIAL));
    old=(HFONT)SelectObject(dc,title); IcText(dc,18,8,"Video Options",RGB(255,96,40),TA_LEFT|TA_TOP);
    SelectObject(dc,row); for(i=0;i<3;i++) IcText(dc,20,VcRowY(g_vcGroup[i].row)-VC_HD+1,g_vcGroup[i].title,RGB(255,150,90),TA_LEFT|TA_TOP);   /* group headings */
    for(i=0;i<VC_ROWS;i++){ int y=VcRowY(i), on=(i==s->sel), en=VcRowEnabled(i), x=(i<VC_RESET)?40:20; COLORREF c=!en?RGB(110,110,130):(on?RGB(255,235,80):RGB(235,235,240));
        if(on){ POINT a[3]; a[0].x=x-20; a[0].y=y+4; a[1].x=x-20; a[1].y=y+16; a[2].x=x-10; a[2].y=y+10; IcPoly(dc,a,3,RGB(255,235,80),RGB(255,235,80)); }
        SelectObject(dc,row); IcText(dc,x,y,g_vcLabel[i],c,TA_LEFT|TA_TOP);
        if(i<VC_NVAL){ VcValue(i,t,sizeof t); SelectObject(dc,fMid);
            if(on && en){ IcText(dc,VC_VX-18,y+2,"<",RGB(255,235,80),TA_LEFT|TA_TOP); IcText(dc,VC_VX,y+2,t,RGB(255,235,80),TA_LEFT|TA_TOP); { SIZE sz; GetTextExtentPoint32A(dc,t,(int)strlen(t),&sz); IcText(dc,VC_VX+sz.cx+8,y+2,">",RGB(255,235,80),TA_LEFT|TA_TOP); } }
            else IcText(dc,VC_VX,y+2,t,en?RGB(190,230,255):RGB(100,100,120),TA_LEFT|TA_TOP); } }
    /* help for the selected row */
    SelectObject(dc,fSm); IcBox(dc,18,394,614,48,RGB(16,16,34),RGB(90,90,130),8);
    IcText(dc,28,400,g_vcHelp[s->sel][0],RGB(225,225,240),TA_LEFT|TA_TOP); IcText(dc,28,417,g_vcHelp[s->sel][1],RGB(225,225,240),TA_LEFT|TA_TOP);
    SelectObject(dc,fMid); if(s->msg[0] && now-s->msgT<3500) IcText(dc,18,446,s->msg,RGB(255,200,90),TA_LEFT|TA_TOP);
    SelectObject(dc,fSm); IcText(dc,18,462,s->list?"Up/Down, PgUp/PgDn or the wheel: choose   Enter or click: pick   Esc or a click outside: back"
                                                  :"Up/Down: row   Left/Right: change   Enter: change / size list   F11 / Alt+Enter: fullscreen   Esc: done",RGB(150,150,175),TA_LEFT|TA_TOP);
    if(s->list) VcListDraw(dc,s,fMid,fSm);   /* (on top of the rows) */
    SelectObject(dc,old); DeleteObject(title); DeleteObject(row); DeleteObject(fSm); DeleteObject(fMid); GdiFlush(); }

/* ---- input ---- */
static int VcRowAt(int x,int y){ int i; if(x<0 || x>=632) return -1; for(i=0;i<VC_ROWS;i++){ int yy=VcRowY(i); if(y>=yy && y<yy+VC_DY-4) return i; } return -1; }
/* returns 1 when the screen is finished */
static int VcActivate(VcState *s,int row,int dir)
{   if(row==VC_RESET){ VcReset(s); return 0; } if(row==VC_DONE) return 1;
    if((row==VC_WINSIZE || row==VC_FSRES) && dir>0){ VcListOpen(s,row); return 0; }   /* (a right click still steps back) */
    if(row==VC_DETAIL && dir>0){ VcDetailPage(); return 0; }   /* (Enter / a click: the page; Left / Right: the presets) */
    VcAdjust(s,row,dir); return 0; }
static int VcKeyDown(VcState *s,int vk)
{   if(s->list) return VcListKey(s,vk), 0;
    if(vk==VK_ESCAPE) return 1;
    if(vk==VK_UP){ s->sel=(s->sel+VC_ROWS-1)%VC_ROWS; return 0; } if(vk==VK_DOWN || vk==VK_TAB){ s->sel=(s->sel+1)%VC_ROWS; return 0; }
    if(vk==VK_LEFT){ VcAdjust(s,s->sel,-1); return 0; } if(vk==VK_RIGHT){ VcAdjust(s,s->sel,1); return 0; }
    if(vk==VK_RETURN || vk==VK_SPACE) return VcActivate(s,s->sel,1);
    return 0; }

/* always returns 1 (handled); 0 when the screen cannot be shown */
__declspec(dllexport) int __stdcall RR_VideoConfig(void)
{   Surf *tmp,base; MSG m; VcState s; int done=0, dirty=1, gpuShot=0; Surf *prevModal=g_modalSurf; DWORD lastDraw=0;
    if(!g_primary || !(tmp=NewSurf(g_primary->w,g_primary->h,16))) return 0;
    base=*tmp; base.bits=(BYTE*)malloc((size_t)tmp->pitch*tmp->h); if(!base.bits){ FreeSurf(tmp); return 0; }
    { Surf *keep=g_modalSurf; g_modalSurf=NULL; SdBaseFrame(&base); g_modalSurf=keep; }
    g_modalSurf=tmp; memset(&s,0,sizeof s); VcBuildLists(); LOG("videocfg: open");
    if(getenv("RR_TEST_RIDERCOL")){ RR_RiderColours(); done=1; }   /* test: straight into the rider colour editor (its picture: ridercol.inc) */
    if(getenv("RR_TEST_GFXCFG")){ VcDetailPage(); done=1; }   /* test: straight into the True 3D detail page (RR_TEST_GFXCFG=<row> + RR_DUMP_DIR: gfxcfg.bmp) */
    while(!done){
        while(PeekMessageA(&m,NULL,0,0,PM_REMOVE)){
            if(m.message==WM_QUIT){ PostQuitMessage((int)m.wParam); done=1; break; }
            if(m.message==WM_SYSKEYDOWN && m.wParam==VK_RETURN){ VidToggleFullscreen(); dirty=1; continue; }
            if(m.message==WM_KEYDOWN && m.wParam==VK_F11){ VidToggleFullscreen(); dirty=1; continue; }
            if(m.message==WM_KEYDOWN || m.message==WM_SYSKEYDOWN){ if(VcKeyDown(&s,(int)m.wParam)) done=1; dirty=1; continue; }
            if(s.list){   /* a size list is open: it takes the mouse */
                if(MSG_LDOWN(m) || MSG_RDOWN(m)){ int x, y, i; MsgFramePos(&m,&x,&y); i=VcListAt(&s,x,y);
                    if(i>=0 && MSG_LDOWN(m)){ int row=s.list; s.list=0; VcListPick(&s,row,i); } else if(i==-1 || MSG_RDOWN(m)) s.list=0; dirty=1; continue; }
                if(m.message==WM_MOUSEMOVE){ int x, y, i; MsgFramePos(&m,&x,&y); if((i=VcListAt(&s,x,y))>=0 && i!=s.lsel){ s.lsel=i; dirty=1; } continue; }
                if(m.message==WM_MOUSEWHEEL){ int d=(short)HIWORD(m.wParam), n=VcListN(s.list); s.ltop-=d/WHEEL_DELTA*3; if(s.ltop>n-VC_LVIS) s.ltop=n-VC_LVIS; if(s.ltop<0) s.ltop=0;
                    if(s.lsel<s.ltop) s.lsel=s.ltop; if(s.lsel>=s.ltop+VC_LVIS) s.lsel=s.ltop+VC_LVIS-1; dirty=1; continue; } }
            if(MSG_LDOWN(m) || MSG_RDOWN(m)){ int x, y, r; MsgFramePos(&m,&x,&y); if((r=VcRowAt(x,y))>=0){ s.sel=r; if(VcActivate(&s,r,MSG_RDOWN(m)?-1:1)) done=1; dirty=1; } continue; }
            if(m.message==WM_MOUSEMOVE){ int x, y, r; MsgFramePos(&m,&x,&y); if(m.wParam==0 && (r=VcRowAt(x,y))>=0 && r!=s.sel){ s.sel=r; dirty=1; } continue; }
            if(m.message==WM_MOUSEWHEEL) continue;
            if(m.message==WM_KEYUP || m.message==WM_SYSKEYUP || m.message==WM_CHAR || m.message==WM_LBUTTONUP || m.message==WM_RBUTTONUP) continue;
            DispatchMessageA(&m); }
        if(GetTickCount()-lastDraw>250 || SgTickerUp()) dirty=1;      /* message timeout, a resized window; the song ticker scrolling */
        if(dirty && !done){ VcDraw(tmp,&base,&s,GetTickCount()); Present(tmp); dirty=0; lastDraw=GetTickCount();
            { const char *dd=getenv("RR_DUMP_DIR"), *t=getenv("RR_TEST_VIDEOCFG");   /* test: RR_TEST_VIDEOCFG=<row> + RR_DUMP_DIR: that row selected, the screen saved as videocfg.bmp, closed */
              if(t && dd && *t>='0' && *t<='9'){ char p[MAX_PATH]; int r=atoi(t); if(r>=0 && r<VC_ROWS && s.sel!=r){ s.sel=r; dirty=1; continue; }
                if(getenv("RR_TEST_VIDEOCFG_LIST") && (r==VC_WINSIZE || r==VC_FSRES) && !s.list){ VcListOpen(&s,r); dirty=1; continue; }   /* (+ RR_TEST_VIDEOCFG_LIST=1: that row's size list open) */ if(g_dumpGpu[0] && !gpuShot){ gpuShot=1; g_gpuReq=902; dirty=1; continue; } _snprintf(p,sizeof p,"%s\\videocfg.bmp",dd); p[sizeof p-1]=0; DumpBmp(tmp,p); done=1; } } }
        Sleep(15); }
    VidSave();
    g_modalSurf=prevModal; g_lastShown=g_primary; free(base.bits); FreeSurf(tmp);
    if(g_hwnd) InvalidateRect(g_hwnd,NULL,FALSE);
    if(g_primary && !prevModal) Present(g_primary);
    ModalEatClick(); LOG("videocfg: closed"); return 1; }

/* test hook: RR_TEST_VIDEOCFG=1 opens the screen once, 4 s after the start of the time clock */
static void VcTestHook(void)
{   static int fired; char *e=getenv("RR_TEST_VIDEOCFG");
    if(fired || !e || !g_primary || !g_clockMode) return; { DWORD fr=(GetTickCount()-g_t0)*30/1000; if(fr<120) return; }
    fired=1; RR_VideoConfig(); }
