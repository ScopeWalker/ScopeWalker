// ScopeWalker - Win32/GDI
// Vectorscope + Waveform + Parade + Histogramme RGB d'une zone de l'ecran.
//
// Selection : maintenir ALT (par defaut) + clic gauche, puis glisser.
// Barre de menu : ON/OFF, Scopes, Preferences, Reset.
// Les scopes s'affichent par deux maximum sur une ligne ; au-dela ils passent
// a la ligne suivante. Ils peuvent aussi etre detaches ("Split scopes").

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_CANVAS   180
#define MAX_CANVAS   1200
#define MARGIN       14
#define MENUBAR_H    32
#define STATUS_H     26
#define SPLIT_W      8
#define MAX_SAMPLES  90000
#define RESIZE_EDGE  6

#define UI_BG_R 32
#define UI_BG_G 32
#define UI_BG_B 35
#define UI_BG        RGB(UI_BG_R,UI_BG_G,UI_BG_B)
#define UI_BG_DWORD  (((DWORD)UI_BG_R<<16)|((DWORD)UI_BG_G<<8)|UI_BG_B)

/* Chroma maximale d'une primaire saturee (le rouge et le cyan, |UV|=161.19).
   C'est elle qui definit le cercle 100% : cibles et nuage partagent ainsi la
   MEME echelle, donc une couleur pure tombe exactement sur son repere. */
static const double CHROMA_MAX = 161.19;

static const char *MAIN_CLASS   ="SWMainWnd";
static const char *OVERLAY_CLASS="SWOverlayWnd";
static const char *PREFS_CLASS  ="SWPrefsWnd";
static const char *SCOPES_CLASS ="SWScopesWnd";
static const char *SCOPEW_CLASS ="SWScopeWnd";

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* ================= scopes ================= */
enum { SC_VEC=0, SC_WAVE, SC_PARADE, SC_HIST, SC_COUNT };

typedef struct {
    HDC     dc;
    HBITMAP bmp;
    DWORD  *bits;
    DWORD  *tpl;
    int     w,h;
    RECT    rc;
    HWND    wnd;
    BOOL    on;
    double  prop;
} Scope;

static Scope g_sc[SC_COUNT];
static const char *SC_NAME[SC_COUNT]={"Vectorscope","Waveform","Parade","Histogramme RGB"};

static HWND  g_hMain=NULL,g_hOverlay=NULL,g_hPrefs=NULL,g_hScopes=NULL;
static POINT g_overlayOrigin={0,0};
static HICON g_icon=NULL;
static HINSTANCE g_inst=NULL;

static BOOL  g_dragging=FALSE;
static POINT g_dragStart;
static RECT  g_selRectScreen={0,0,0,0};

static HFONT g_hFont=NULL,g_hFontBold=NULL;

static char g_status[256]="ALT + clic gauche, puis glissez pour choisir une zone.";
static BOOL g_hasSelection=FALSE,g_paused=FALSE;
static int  g_refreshCounter=0,g_topmostCounter=0,g_cursorCounter=0;
#define REFRESH_TICKS 6

static BOOL     g_zoom2=FALSE;
static double   g_brightness=1.0;
static int      g_modVK=VK_MENU;
static int      g_pauseVK=VK_ESCAPE;
static COLORREF g_frameColor=RGB(255,255,255);
static BOOL     g_showSkin=TRUE;
static BOOL     g_rgbAtCursor=TRUE;
static BOOL     g_clipping=TRUE;
static BOOL     g_wfRGB=FALSE;
static BOOL     g_split=FALSE;
static BYTE     g_curR=0,g_curG=0,g_curB=0;

static DWORD g_lastHash=0;

enum { MI_ONOFF=0, MI_SCOPES, MI_PREFS, MI_RESET, MI_COUNT };
static RECT g_miRect[MI_COUNT];
static int  g_hoverMI=-1;
static BOOL g_tracking=FALSE;

static RECT g_zoomChkRc={0,0,0,0};
static BOOL g_zoomHover=FALSE;

/* separateurs : un par ligne, entre les deux colonnes */
#define MAX_ROWS 2
static RECT g_rowSplit[MAX_ROWS];
static int  g_rowSplitA[MAX_ROWS],g_rowSplitB[MAX_ROWS];
static int  g_splitDragRow=-1;
static int  g_splitDragX=0;
static double g_dragPropA=0,g_dragPropB=0;

static BYTE *g_sR=NULL,*g_sG=NULL,*g_sB=NULL;
static WORD *g_sCol=NULL;
static int   g_sCount=0,g_sCap=0,g_sCols=0;

static COLORREF g_swatch[6]={
    RGB(255,255,255),RGB(60,220,100),RGB(255,80,80),
    RGB(255,210,60),RGB(80,200,255),RGB(255,90,220)
};
static RECT g_swatchRc[6],g_modRc[4],g_pauseRc[3],g_wfModeRc[2];
static RECT g_skinRc,g_rgbCurRc,g_clipRc,g_sliderRc;
static BOOL g_sliderDrag=FALSE;
static RECT g_scChkRc[SC_COUNT],g_scSplitRc;

static HDC     g_pfDC=NULL;   static HBITMAP g_pfBmp=NULL;
static DWORD  *g_pfBits=NULL; static int g_pfW=0,g_pfH=0;

static void RenderAll(void);
static void RecomputeLayout(HWND);
static void RebuildTemplate(int idx);
static void ApplySplitMode(void);
static void FitWindowToContent(HWND);
static void EnsureScopeBmp(int idx,int w,int h);
static void RenderScope(int idx);

/* ================= PRNG (dithering anti-moire) ================= */
static unsigned int g_rng=2463534242u;
static inline void SeedRng(unsigned int s){ g_rng=s?s:2463534242u; }
static inline double Jitter(void)
{
    g_rng^=g_rng<<13; g_rng^=g_rng>>17; g_rng^=g_rng<<5;
    return (double)(g_rng>>8)*(1.0/16777216.0)-0.5;
}

static void ComputeUVf(double R,double G,double B,double*u,double*v)
{
    double Y=0.299*R+0.587*G+0.114*B;
    *u=(B-Y)*0.492;
    *v=(R-Y)*0.877;
}

/* ================= primitives AA ================= */
static void BlendPx(DWORD*buf,int W,int H,int x,int y,BYTE r,BYTE g,BYTE b,double a)
{
    if(x<0||x>=W||y<0||y>=H||a<=0.0) return;
    if(a>1.0)a=1.0;
    DWORD c=buf[y*W+x];
    BYTE cr=(BYTE)((c>>16)&0xFF),cg=(BYTE)((c>>8)&0xFF),cb=(BYTE)(c&0xFF);
    buf[y*W+x]=((DWORD)(BYTE)(r*a+cr*(1-a))<<16)
              |((DWORD)(BYTE)(g*a+cg*(1-a))<<8)
              | (DWORD)(BYTE)(b*a+cb*(1-a));
}

static void AACircle(DWORD*buf,int W,int H,int cx,int cy,int R,BYTE r,BYTE g,BYTE b,double a)
{
    int x0=cx-R-2,x1=cx+R+2,y0=cy-R-2,y1=cy+R+2;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>=W)x1=W-1;
    if(y1>=H)y1=H-1;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++){
        double d=sqrt((double)(x-cx)*(x-cx)+(double)(y-cy)*(y-cy));
        double cov=1.0-fabs(d-R);
        if(cov>0) BlendPx(buf,W,H,x,y,r,g,b,cov*a);
    }
}

static double SegDist(double px,double py,double ax,double ay,double bx,double by)
{
    double vx=bx-ax,vy=by-ay,wx=px-ax,wy=py-ay;
    double l2=vx*vx+vy*vy;
    double t=(l2>1e-9)?(vx*wx+vy*wy)/l2:0.0;
    if(t<0)t=0;
    if(t>1)t=1;
    double dx=px-(ax+t*vx),dy=py-(ay+t*vy);
    return sqrt(dx*dx+dy*dy);
}

static void AALine(DWORD*buf,int W,int H,double ax,double ay,double bx,double by,
                   double wid,BYTE r,BYTE g,BYTE b,double a)
{
    int x0=(int)floor(fmin(ax,bx)-wid-2),x1=(int)ceil(fmax(ax,bx)+wid+2);
    int y0=(int)floor(fmin(ay,by)-wid-2),y1=(int)ceil(fmax(ay,by)+wid+2);
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>=W)x1=W-1;
    if(y1>=H)y1=H-1;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++){
        double cov=(wid/2.0+0.5)-SegDist(x+0.5,y+0.5,ax,ay,bx,by);
        if(cov>0) BlendPx(buf,W,H,x,y,r,g,b,cov*a);
    }
}

static void AASquare(DWORD*buf,int W,int H,int cx,int cy,int half,BYTE r,BYTE g,BYTE b,double a)
{
    int x0=cx-half-2,x1=cx+half+2,y0=cy-half-2,y1=cy+half+2;
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>=W)x1=W-1;
    if(y1>=H)y1=H-1;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++){
        double dx=fmax(fabs(x-cx)-half,0.0),dy=fmax(fabs(y-cy)-half,0.0);
        double cov=1.0-sqrt(dx*dx+dy*dy);
        if(cov>0) BlendPx(buf,W,H,x,y,r,g,b,cov*a);
    }
}

static void AADisc(DWORD*buf,int W,int H,double cx,double cy,double R,BYTE r,BYTE g,BYTE b,double a)
{
    int x0=(int)floor(cx-R-1),x1=(int)ceil(cx+R+1);
    int y0=(int)floor(cy-R-1),y1=(int)ceil(cy+R+1);
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>=W)x1=W-1;
    if(y1>=H)y1=H-1;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++){
        double dx=(x+0.5)-cx,dy=(y+0.5)-cy;
        double cov=R+0.5-sqrt(dx*dx+dy*dy);
        if(cov>1.0)cov=1.0;
        if(cov>0) BlendPx(buf,W,H,x,y,r,g,b,cov*a);
    }
}

static void AARing(DWORD*buf,int W,int H,double cx,double cy,double R,double wid,
                   BYTE r,BYTE g,BYTE b,double a)
{
    int x0=(int)floor(cx-R-2),x1=(int)ceil(cx+R+2);
    int y0=(int)floor(cy-R-2),y1=(int)ceil(cy+R+2);
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>=W)x1=W-1;
    if(y1>=H)y1=H-1;
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++){
        double dx=(x+0.5)-cx,dy=(y+0.5)-cy;
        double d=sqrt(dx*dx+dy*dy);
        double cov=(wid/2.0+0.5)-fabs(d-R);
        if(cov>1.0)cov=1.0;
        if(cov>0) BlendPx(buf,W,H,x,y,r,g,b,cov*a);
    }
}

static inline void AddPx(DWORD*buf,int W,int x,int y,BYTE R,BYTE G,BYTE B,double gain)
{
    DWORD c=buf[y*W+x];
    int r=(int)((c>>16)&0xFF)+(int)(R*gain);
    int g=(int)((c>>8)&0xFF)+(int)(G*gain);
    int b=(int)(c&0xFF)+(int)(B*gain);
    if(r>255)r=255;
    if(g>255)g=255;
    if(b>255)b=255;
    buf[y*W+x]=((DWORD)r<<16)|((DWORD)g<<8)|(DWORD)b;
}

/* ================= graticules ================= */
static void TplVec(void)
{
    Scope*s=&g_sc[SC_VEC];
    int W=s->w,H=s->h;
    int D=(W<H)?W:H;
    int cx=W/2,cy=H/2;
    DWORD*buf=s->bits;
    for(int i=0;i<W*H;i++) buf[i]=UI_BG_DWORD;

    double R1=D*0.44;                 /* cercle 100% */
    double k=R1/CHROMA_MAX;           /* echelle unique cibles <-> nuage */

    AACircle(buf,W,H,cx,cy,(int)R1,100,100,106,0.6);
    AACircle(buf,W,H,cx,cy,(int)(R1*0.5),100,100,106,0.35);
    AALine(buf,W,H,0,cy,W,cy,1.0,88,88,94,0.35);
    AALine(buf,W,H,cx,0,cx,H,1.0,88,88,94,0.35);

    double sx2=0,sy2=0;
    if(g_showSkin){
        /* ligne des tons chair : direction du I-axis (~123 deg), longueur = R1 */
        double u,v; ComputeUVf(255,200,160,&u,&v);
        double len=sqrt(u*u+v*v);
        if(len>1e-4){
            double dx=u/len,dy=-v/len;
            sx2=cx+dx*R1; sy2=cy+dy*R1;
            AALine(buf,W,H,cx,cy,sx2,sy2,1.2,235,150,100,0.8);
        }
    }

    typedef struct{const char*n;BYTE r,g,b;}T;
    T t[6]={{"R",255,0,0},{"J",255,255,0},{"V",0,255,0},
            {"C",0,255,255},{"B",0,0,255},{"M",255,0,255}};
    int tx[6],ty[6];
    int half=(int)(D*0.011); if(half<3) half=3;
    for(int i=0;i<6;i++){
        /* position REELLE de la primaire : chaque cible a son propre rayon
           (R et C sur le cercle, V et M plus pres, J et B encore plus pres) */
        double u,v; ComputeUVf(t[i].r,t[i].g,t[i].b,&u,&v);
        tx[i]=(int)(cx+u*k);
        ty[i]=(int)(cy-v*k);
        AASquare(buf,W,H,tx[i],ty[i],half,t[i].r,t[i].g,t[i].b,0.95);
    }

    HGDIOBJ of=SelectObject(s->dc,g_hFont);
    SetBkMode(s->dc,TRANSPARENT);
    for(int i=0;i<6;i++){
        SetTextColor(s->dc,RGB(150,150,155));
        TextOutA(s->dc,tx[i]+half+5,ty[i]-8,t[i].n,(int)strlen(t[i].n));
    }
    SetTextColor(s->dc,RGB(120,120,124));
    TextOutA(s->dc,cx+5,3,"100%",4);
    if(g_showSkin){
        SetTextColor(s->dc,RGB(225,160,120));
        TextOutA(s->dc,(int)sx2+((sx2>=cx)?5:-34),(int)sy2+((sy2>=cy)?2:-16),"skin",4);
    }
    SelectObject(s->dc,of);
    memcpy(s->tpl,s->bits,(size_t)W*H*sizeof(DWORD));
}

static void TplWaveLike(int idx,BOOL parade)
{
    Scope*s=&g_sc[idx];
    int W=s->w,H=s->h;
    DWORD*buf=s->bits;
    for(int i=0;i<W*H;i++) buf[i]=UI_BG_DWORD;

    for(int p=0;p<=4;p++){
        int y=(int)((1.0-p/4.0)*(H-1));
        AALine(buf,W,H,0,y,W,y,1.0,88,88,94,0.30);
    }
    if(parade||(idx==SC_WAVE&&g_wfRGB))
        for(int k=1;k<3;k++){
            int x=k*W/3;
            AALine(buf,W,H,x,0,x,H,1.0,88,88,94,0.35);
        }

    HGDIOBJ of=SelectObject(s->dc,g_hFont);
    SetBkMode(s->dc,TRANSPARENT);
    SetTextColor(s->dc,RGB(120,120,124));
    const char*lbl[5]={"0","25","50","75","100"};
    for(int p=0;p<=4;p++){
        int y=(int)((1.0-p/4.0)*(H-1));
        int ty=y-14; if(ty<1) ty=1;
        TextOutA(s->dc,3,ty,lbl[p],(int)strlen(lbl[p]));
    }
    SetTextColor(s->dc,RGB(110,110,116));
    TextOutA(s->dc,W-62,3,parade?"parade":"waveform",parade?6:8);
    SelectObject(s->dc,of);
    memcpy(s->tpl,s->bits,(size_t)W*H*sizeof(DWORD));
}

static void TplHist(void)
{
    Scope*s=&g_sc[SC_HIST];
    int W=s->w,H=s->h;
    DWORD*buf=s->bits;
    for(int i=0;i<W*H;i++) buf[i]=UI_BG_DWORD;

    for(int p=0;p<=4;p++){
        int x=(int)((p/4.0)*(W-1));
        AALine(buf,W,H,x,0,x,H,1.0,88,88,94,0.28);
    }
    for(int p=1;p<4;p++){
        int y=(int)((p/4.0)*(H-1));
        AALine(buf,W,H,0,y,W,y,1.0,88,88,94,0.16);
    }
    AALine(buf,W,H,0,H-1,W,H-1,1.4,110,110,116,0.5);

    HGDIOBJ of=SelectObject(s->dc,g_hFont);
    SetBkMode(s->dc,TRANSPARENT);
    SetTextColor(s->dc,RGB(120,120,124));
    TextOutA(s->dc,4,H-18,"0",1);
    TextOutA(s->dc,W-30,H-18,"255",3);
    SetTextColor(s->dc,RGB(110,110,116));
    TextOutA(s->dc,4,3,"RGB",3);
    SelectObject(s->dc,of);
    memcpy(s->tpl,s->bits,(size_t)W*H*sizeof(DWORD));
}

static void RebuildTemplate(int idx)
{
    Scope*s=&g_sc[idx];
    if(!s->bits||!s->tpl||s->w<=0||s->h<=0) return;
    if(idx==SC_VEC) TplVec();
    else if(idx==SC_WAVE) TplWaveLike(SC_WAVE,FALSE);
    else if(idx==SC_PARADE) TplWaveLike(SC_PARADE,TRUE);
    else TplHist();
}

/* ================= rendu vectorscope ================= */
static void RenderVec(void)
{
    Scope*s=&g_sc[SC_VEC];
    if(!s->bits||!s->tpl||s->w<=0||s->h<=0) return;
    int W=s->w,H=s->h;
    /* W*H (et non W*W) : un bitmap non carre ne doit jamais deborder */
    memcpy(s->bits,s->tpl,(size_t)W*H*sizeof(DWORD));
    if(g_sCount<=0) return;

    int D=(W<H)?W:H;
    double cx0=W/2.0,cy0=H/2.0;
    double R1=D*0.44;
    /* MEME echelle que les cibles : une couleur pure tombe sur son repere */
    double k=(R1/CHROMA_MAX)*(g_zoom2?2.0:1.0);
    double gain=8000.0/(double)g_sCount;
    if(gain<0.05)gain=0.05;
    if(gain>0.9)gain=0.9;
    gain*=g_brightness;
    int dot=(D>=560)?2:1;

    SeedRng(0x1234567u);
    for(int i=0;i<g_sCount;i++){
        BYTE R=g_sR[i],G=g_sG[i],B=g_sB[i];
        double u,v;
        ComputeUVf(R+Jitter(),G+Jitter(),B+Jitter(),&u,&v);
        double dx=u*k,dy=-v*k;
        if(dx*dx+dy*dy>R1*R1) continue;   /* ecrete au cercle 100% */
        int px=(int)(cx0+dx),py=(int)(cy0+dy);
        if(px<0||py<0||px>=W-dot||py>=H-dot) continue;
        for(int oy=0;oy<dot;oy++)
            for(int ox=0;ox<dot;ox++)
                AddPx(s->bits,W,px+ox,py+oy,R,G,B,gain);
    }
}

/* ================= rendu waveform / parade ================= */
#define CLIP_MAX 2048
static void RenderWaveLike(int idx,BOOL parade)
{
    Scope*s=&g_sc[idx];
    if(!s->on||!s->bits||!s->tpl||s->w<=0) return;
    int W=s->w,H=s->h;
    memcpy(s->bits,s->tpl,(size_t)W*H*sizeof(DWORD));
    if(g_sCount<=0||g_sCols<=0) return;

    BOOL rgb = parade || (idx==SC_WAVE && g_wfRGB);

    double gain=6000.0/(double)g_sCount;
    if(gain<0.05)gain=0.05;
    if(gain>0.9)gain=0.9;
    gain*=g_brightness;

    int panelW=rgb?(W/3):W;
    if(panelW<2) return;

    double xs=(g_sCols>1)?((double)(panelW-1)/(double)(g_sCols-1)):0.0;
    double ys=(double)(H-1)/255.0;

    static BYTE clipLo[3*CLIP_MAX],clipHi[3*CLIP_MAX];
    int nPanels=rgb?3:1;
    BOOL doClip=(g_clipping && panelW<=CLIP_MAX);
    if(doClip){
        memset(clipLo,0,(size_t)nPanels*panelW);
        memset(clipHi,0,(size_t)nPanels*panelW);
    }

    SeedRng(0x7654321u);
    for(int i=0;i<g_sCount;i++){
        BYTE R=g_sR[i],G=g_sG[i],B=g_sB[i];
        double fx=(double)g_sCol[i]*xs+Jitter()*(xs>1.0?xs:1.0);
        int bx=(int)(fx+0.5);
        if(bx<0)bx=0;
        if(bx>=panelW)bx=panelW-1;

        if(!rgb){
            double lum=0.299*R+0.587*G+0.114*B+Jitter();
            int y=(int)((H-1)-lum*ys+0.5);
            if(y>=0&&y<H) AddPx(s->bits,W,bx,y,255,255,255,gain);
            if(doClip){
                if(R==0&&G==0&&B==0) clipLo[bx]=1;
                if(R==255&&G==255&&B==255) clipHi[bx]=1;
            }
        } else {
            int y;
            y=(int)((H-1)-(R+Jitter())*ys+0.5);
            if(y>=0&&y<H) AddPx(s->bits,W,bx,y,255,60,60,gain);
            y=(int)((H-1)-(G+Jitter())*ys+0.5);
            if(y>=0&&y<H) AddPx(s->bits,W,panelW+bx,y,60,255,60,gain);
            y=(int)((H-1)-(B+Jitter())*ys+0.5);
            if(y>=0&&y<H) AddPx(s->bits,W,2*panelW+bx,y,80,110,255,gain);
            if(doClip){
                if(R==0)   clipLo[bx]=1;
                if(R==255) clipHi[bx]=1;
                if(G==0)   clipLo[panelW+bx]=1;
                if(G==255) clipHi[panelW+bx]=1;
                if(B==0)   clipLo[2*panelW+bx]=1;
                if(B==255) clipHi[2*panelW+bx]=1;
            }
        }
    }

    /* clipping : blanc pur -> barre ROUGE en haut, noir pur -> barre BLEUE en bas */
    if(doClip){
        for(int p=0;p<nPanels;p++){
            for(int bx=0;bx<panelW;bx++){
                int i2=p*panelW+bx;
                int x=p*panelW+bx;
                if(x>=W) continue;
                if(clipHi[i2])
                    for(int k=0;k<3;k++) BlendPx(s->bits,W,H,x,k,255,50,50,0.95);
                if(clipLo[i2])
                    for(int k=0;k<3;k++) BlendPx(s->bits,W,H,x,H-1-k,70,130,255,0.95);
            }
        }
    }
}

/* ================= rendu histogramme (lisse + courbes de crete) ================= */
static void RenderHist(void)
{
    Scope*s=&g_sc[SC_HIST];
    if(!s->on||!s->bits||!s->tpl||s->w<=0) return;
    int W=s->w,H=s->h;
    memcpy(s->bits,s->tpl,(size_t)W*H*sizeof(DWORD));
    if(g_sCount<=0) return;

    static double bins[3][256],sm[3][256];
    memset(bins,0,sizeof(bins));
    for(int i=0;i<g_sCount;i++){
        bins[0][g_sR[i]]+=1.0;
        bins[1][g_sG[i]]+=1.0;
        bins[2][g_sB[i]]+=1.0;
    }

    /* lissage : noyau gaussien (rayon 3) -> courbe lisible, sans dents de scie */
    static const double kern[7]={0.06,0.12,0.20,0.24,0.20,0.12,0.06};
    for(int c=0;c<3;c++){
        for(int b=0;b<256;b++){
            double acc=0,wsum=0;
            for(int j=-3;j<=3;j++){
                int bb=b+j;
                if(bb<0||bb>255) continue;
                acc+=bins[c][bb]*kern[j+3];
                wsum+=kern[j+3];
            }
            sm[c][b]=(wsum>0)?acc/wsum:0;
        }
    }

    double maxB=1;
    for(int c=0;c<3;c++)
        for(int b=0;b<256;b++)
            if(sm[c][b]>maxB) maxB=sm[c][b];

    /* Echelle en puissance (gamma ~0.45), comme Lightroom / DaVinci.
       Une echelle logarithmique etait utilisee auparavant : elle exagerait
       enormement les tres faibles comptages (1 pixel noir sur 90000 donnait
       deja une barre a 9% de hauteur), ce qui simulait un noir bouche ou des
       blancs brules inexistants. Ici, un comptage negligeable reste negligeable. */
    static const double HIST_GAMMA=0.45;

    static const BYTE col[3][3]={{255,70,70},{70,235,110},{90,130,255}};
    double alpha=0.5*g_brightness;
    if(alpha>1.0) alpha=1.0;

    /* hauteur de la courbe pour chaque colonne de pixels */
    static double hgt[3][4096];
    int WW=(W>4096)?4096:W;
    for(int c=0;c<3;c++){
        for(int x=0;x<WW;x++){
            double fb=(WW>1)?((double)x*255.0/(double)(WW-1)):0.0;
            int b0=(int)fb; if(b0>255)b0=255;
            int b1=(b0<255)?b0+1:255;
            double f=fb-b0;
            double v=(1.0-f)*sm[c][b0]+f*sm[c][b1];
            double t=(v>0)?pow(v/maxB,HIST_GAMMA):0.0;
            if(t>1)t=1;
            hgt[c][x]=t*(H-3);
        }
    }

    /* remplissage doux sous la courbe */
    for(int c=0;c<3;c++){
        for(int x=0;x<WW;x++){
            int top=(int)(H-2-hgt[c][x]);
            for(int y=H-2;y>top;y--)
                AddPx(s->bits,W,x,y,col[c][0],col[c][1],col[c][2],alpha*0.22);
        }
    }

    /* courbe de crete, de la couleur du canal, anti-aliasee (facon Lightroom) */
    for(int c=0;c<3;c++){
        for(int x=0;x<WW-1;x++){
            double y0=H-2-hgt[c][x];
            double y1=H-2-hgt[c][x+1];
            AALine(s->bits,W,H,x,y0,x+1,y1,1.6,col[c][0],col[c][1],col[c][2],0.95);
        }
    }
}

static void RenderScope(int idx)
{
    if(idx==SC_VEC) RenderVec();
    else if(idx==SC_WAVE) RenderWaveLike(SC_WAVE,FALSE);
    else if(idx==SC_PARADE) RenderWaveLike(SC_PARADE,TRUE);
    else RenderHist();
}

static void RenderAll(void)
{
    for(int i=0;i<SC_COUNT;i++) RenderScope(i);
    InvalidateRect(g_hMain,NULL,FALSE);
    for(int i=SC_WAVE;i<SC_COUNT;i++)
        if(g_sc[i].wnd) InvalidateRect(g_sc[i].wnd,NULL,FALSE);
}

/* ================= capture ================= */
static void Capture(RECT r,BOOL force)
{
    int w=r.right-r.left,h=r.bottom-r.top;
    if(w<2||h<2) return;

    HDC scr=GetDC(NULL);
    HDC mem=CreateCompatibleDC(scr);
    BITMAPINFO bi; memset(&bi,0,sizeof(bi));
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=w;
    bi.bmiHeader.biHeight=-h;
    bi.bmiHeader.biPlanes=1;
    bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;

    void*bits=NULL;
    HBITMAP bmp=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,NULL,0);
    HGDIOBJ ob=SelectObject(mem,bmp);
    BitBlt(mem,0,0,w,h,scr,r.left,r.top,SRCCOPY|CAPTUREBLT);

    double area=(double)w*(double)h;
    int stride=(int)ceil(sqrt(area/(double)MAX_SAMPLES));
    if(stride<1) stride=1;

    int nx=(w+stride-1)/stride,ny=(h+stride-1)/stride;
    int cap=nx*ny;
    if(cap>g_sCap){
        free(g_sR); free(g_sG); free(g_sB); free(g_sCol);
        g_sR=(BYTE*)malloc(cap); g_sG=(BYTE*)malloc(cap);
        g_sB=(BYTE*)malloc(cap); g_sCol=(WORD*)malloc((size_t)cap*sizeof(WORD));
        g_sCap=cap;
    }
    if(!g_sR||!g_sG||!g_sB||!g_sCol){
        SelectObject(mem,ob); DeleteObject(bmp);
        DeleteDC(mem); ReleaseDC(NULL,scr);
        return;
    }
    g_sCount=0; g_sCols=nx;

    SeedRng(0x9E3779B9u);
    DWORD hash=2166136261u;
    int hstep=0;
    BYTE*px=(BYTE*)bits;
    for(int y=0;y<h;y+=stride){
        int ci=0;
        for(int x=0;x<w;x+=stride,ci++){
            int sx=x,sy=y;
            if(stride>1){
                sx=x+(int)((Jitter()+0.5)*stride);
                sy=y+(int)((Jitter()+0.5)*stride);
                if(sx>=w) sx=w-1;
                if(sy>=h) sy=h-1;
            }
            BYTE*row=px+(size_t)sy*w*4;
            BYTE B=row[sx*4+0],G=row[sx*4+1],R=row[sx*4+2];
            g_sR[g_sCount]=R; g_sG[g_sCount]=G; g_sB[g_sCount]=B;
            g_sCol[g_sCount]=(WORD)ci;
            g_sCount++;
            if((hstep++&3)==0){
                hash^=R; hash*=16777619u;
                hash^=G; hash*=16777619u;
                hash^=B; hash*=16777619u;
            }
        }
    }

    SelectObject(mem,ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL,scr);

    if(!force&&hash==g_lastHash) return;
    g_lastHash=hash;

    snprintf(g_status,sizeof(g_status),"Zone : %d x %d px  -  suivi actif",w,h);
    g_hasSelection=TRUE;
    RenderAll();
}

static void SetTracking(BOOL on)
{
    g_paused=!on;
    if(!g_hasSelection){ ShowWindow(g_hOverlay,SW_HIDE); return; }
    if(on){
        SetWindowPos(g_hOverlay,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        ShowWindow(g_hOverlay,SW_SHOWNOACTIVATE);
        g_refreshCounter=0;
        Capture(g_selRectScreen,TRUE);
    } else {
        ShowWindow(g_hOverlay,SW_HIDE);
        strcpy(g_status,"Suivi en pause - cliquez sur ON pour reprendre.");
    }
    InvalidateRect(g_hMain,NULL,FALSE);
}

static void ResetZone(void)
{
    g_hasSelection=FALSE; g_paused=FALSE;
    g_sCount=0; g_sCols=0; g_lastHash=0;
    ShowWindow(g_hOverlay,SW_HIDE);
    RenderAll();
    strcpy(g_status,"Zone reinitialisee. ALT + clic gauche pour en definir une nouvelle.");
    InvalidateRect(g_hMain,NULL,FALSE);
}

/* ================= overlay ================= */
static LRESULT CALLBACK OverlayProc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    switch(m){
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps);
        RECT rc; GetClientRect(h,&rc);
        HBRUSH k=CreateSolidBrush(RGB(255,0,255));
        FillRect(dc,&rc,k); DeleteObject(k);
        RECT lr=g_selRectScreen;
        OffsetRect(&lr,-g_overlayOrigin.x,-g_overlayOrigin.y);
        HPEN p=CreatePen(PS_SOLID,2,g_frameColor);
        HGDIOBJ op=SelectObject(dc,p),obr=SelectObject(dc,GetStockObject(NULL_BRUSH));
        Rectangle(dc,lr.left,lr.top,lr.right,lr.bottom);
        SelectObject(dc,obr); SelectObject(dc,op); DeleteObject(p);
        EndPaint(h,&ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProc(h,m,w,l);
}

/* ================= fenetres de scope detachees ================= */
static LRESULT CALLBACK ScopeWndProc(HWND h,UINT m,WPARAM wp,LPARAM lp)
{
    /* l'index est transmis par lpCreateParams et lu des WM_NCCREATE : les
       messages envoyes PENDANT CreateWindow (WM_NCCALCSIZE, WM_SIZE...)
       doivent deja voir le bon scope, sinon ils manipulent le vectorscope. */
    if(m==WM_NCCREATE){
        CREATESTRUCT*cs=(CREATESTRUCT*)lp;
        SetWindowLongPtr(h,GWLP_USERDATA,(LONG_PTR)cs->lpCreateParams);
        return DefWindowProc(h,m,wp,lp);
    }

    int idx=(int)(INT_PTR)GetWindowLongPtr(h,GWLP_USERDATA);
    /* une fenetre detachee ne porte jamais le vectorscope */
    if(idx<SC_WAVE||idx>=SC_COUNT) return DefWindowProc(h,m,wp,lp);

    switch(m){
    case WM_NCCALCSIZE:
        /* supprime toute zone non-client : plus de cadre systeme, donc plus de
           bande blanche ; la fenetre est entierement peinte par nos soins */
        if(wp) return 0;
        break;
    case WM_NCHITTEST:{
        POINT p={(short)LOWORD(lp),(short)HIWORD(lp)};
        RECT r; GetWindowRect(h,&r);
        int x=p.x-r.left,y=p.y-r.top;
        int w=r.right-r.left,hh=r.bottom-r.top;
        BOOL L=(x<RESIZE_EDGE),R=(x>=w-RESIZE_EDGE);
        BOOL T=(y<RESIZE_EDGE),B=(y>=hh-RESIZE_EDGE);
        if(T&&L) return HTTOPLEFT;
        if(T&&R) return HTTOPRIGHT;
        if(B&&L) return HTBOTTOMLEFT;
        if(B&&R) return HTBOTTOMRIGHT;
        if(L) return HTLEFT;
        if(R) return HTRIGHT;
        if(T) return HTTOP;
        if(B) return HTBOTTOM;
        return HTCAPTION;   /* le corps deplace la fenetre */
    }
    case WM_SIZE:{
        RECT rc; GetClientRect(h,&rc);
        if(rc.right>2&&rc.bottom>2){
            EnsureScopeBmp(idx,rc.right,rc.bottom);
            RenderScope(idx);
            InvalidateRect(h,NULL,FALSE);
        }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps);
        RECT rc; GetClientRect(h,&rc);

        /* on remplit TOUT le client avant le blit : aucun pixel non peint */
        HBRUSH b=CreateSolidBrush(UI_BG);
        FillRect(dc,&rc,b); DeleteObject(b);

        if(g_sc[idx].bits)
            BitBlt(dc,0,0,g_sc[idx].w,g_sc[idx].h,g_sc[idx].dc,0,0,SRCCOPY);

        HPEN p=CreatePen(PS_SOLID,1,RGB(70,70,76));
        HGDIOBJ op=SelectObject(dc,p),ob=SelectObject(dc,GetStockObject(NULL_BRUSH));
        Rectangle(dc,0,0,rc.right,rc.bottom);
        SelectObject(dc,ob); SelectObject(dc,op); DeleteObject(p);

        EndPaint(h,&ps);
        return 0;
    }
    case WM_CLOSE:
        g_sc[idx].on=FALSE;
        ApplySplitMode();
        FitWindowToContent(g_hMain);
        RecomputeLayout(g_hMain);
        return 0;
    case WM_DESTROY:
        g_sc[idx].wnd=NULL;
        return 0;
    }
    return DefWindowProc(h,m,wp,lp);
}

/* ================= bitmaps ================= */
static void FreeScopeBmp(int idx)
{
    Scope*s=&g_sc[idx];
    if(s->bmp){ DeleteObject(s->bmp); s->bmp=NULL; }
    free(s->tpl); s->tpl=NULL;
    s->bits=NULL; s->w=0; s->h=0;
}

static void EnsureScopeBmp(int idx,int w,int h)
{
    Scope*s=&g_sc[idx];
    if(w<8) w=8;
    if(h<8) h=8;
    if(s->bits&&s->w==w&&s->h==h) return;

    BITMAPINFO bi; memset(&bi,0,sizeof(bi));
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=w;
    bi.bmiHeader.biHeight=-h;
    bi.bmiHeader.biPlanes=1;
    bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    void*nb=NULL;
    HBITMAP nbm=CreateDIBSection(s->dc,&bi,DIB_RGB_COLORS,&nb,NULL,0);
    SelectObject(s->dc,nbm);
    FreeScopeBmp(idx);
    s->bmp=nbm; s->bits=(DWORD*)nb; s->w=w; s->h=h;
    s->tpl=(DWORD*)malloc((size_t)w*h*sizeof(DWORD));
    RebuildTemplate(idx);
}

/* ================= split ================= */
static void ApplySplitMode(void)
{
    for(int i=SC_WAVE;i<SC_COUNT;i++){
        Scope*s=&g_sc[i];
        BOOL want=(g_split&&s->on);
        if(want&&!s->wnd){
            RECT mr; GetWindowRect(g_hMain,&mr);
            int w=560,h=340;
            int x=mr.right+12,y=mr.top+(i-SC_WAVE)*(h+14);
            s->wnd=CreateWindowExA(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
                SCOPEW_CLASS,"",WS_POPUP|WS_THICKFRAME,
                x,y,w,h,NULL,NULL,g_inst,(LPVOID)(INT_PTR)i);
            /* force le recalcul du cadre supprime par WM_NCCALCSIZE */
            SetWindowPos(s->wnd,NULL,0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_FRAMECHANGED|SWP_NOACTIVATE);
            ShowWindow(s->wnd,SW_SHOWNOACTIVATE);
        } else if(!want&&s->wnd){
            HWND w=s->wnd;
            s->wnd=NULL;
            DestroyWindow(w);
        }
    }
}

/* ================= preferences ================= */
static const char *MOD_NAMES[4]={"Alt","Ctrl","Maj (Shift)","Win"};
static const int   MOD_VKS[4]={VK_MENU,VK_CONTROL,VK_SHIFT,VK_LWIN};
static const char *PAUSE_NAMES[3]={"Echap","F2","F3"};
static const int   PAUSE_VKS[3]={VK_ESCAPE,VK_F2,VK_F3};
static const char *WFMODE_NAMES[2]={"Blanc (luma)","RGB (parade)"};

static void ApplySlider(int x)
{
    double t=(double)(x-g_sliderRc.left)/(double)(g_sliderRc.right-g_sliderRc.left);
    if(t<0)t=0;
    if(t>1)t=1;
    g_brightness=0.25+t*2.75;
    RenderAll();
}

static void RadioAA(DWORD*buf,int W,int H,HDC dc,RECT r,BOOL sel,const char*label)
{
    double cx=r.left+9.0,cy=r.top+11.0;
    if(sel){
        AARing(buf,W,H,cx,cy,5.0,1.4,120,220,255,1.0);
        AADisc(buf,W,H,cx,cy,2.6,120,220,255,1.0);
    } else AARing(buf,W,H,cx,cy,5.0,1.2,125,125,132,1.0);
    SetTextColor(dc,sel?RGB(120,220,255):RGB(215,215,220));
    TextOutA(dc,r.left+22,r.top+2,label,(int)strlen(label));
}

static void CheckAA(DWORD*buf,int W,int H,HDC dc,RECT r,BOOL on,const char*label)
{
    double cx=r.left+9.0,cy=r.top+10.0;
    if(on){
        AASquare(buf,W,H,(int)cx,(int)cy,6,40,110,170,1.0);
        AARing(buf,W,H,cx,cy,7.6,1.0,90,180,255,0.6);
        AALine(buf,W,H,cx-3.2,cy+0.2,cx-0.8,cy+2.8,1.8,170,230,255,1.0);
        AALine(buf,W,H,cx-0.8,cy+2.8,cx+3.4,cy-3.0,1.8,170,230,255,1.0);
    } else {
        AASquare(buf,W,H,(int)cx,(int)cy,6,52,52,58,1.0);
        AARing(buf,W,H,cx,cy,7.6,1.0,125,125,132,0.9);
    }
    SetTextColor(dc,RGB(215,215,220));
    TextOutA(dc,r.left+24,r.top+2,label,(int)strlen(label));
}

static BOOL EnsurePopupBuf(HDC ref,int W,int H)
{
    if(!g_pfDC) g_pfDC=CreateCompatibleDC(ref);
    if(g_pfBits&&g_pfW==W&&g_pfH==H) return TRUE;
    if(g_pfBmp){ DeleteObject(g_pfBmp); g_pfBmp=NULL; }
    BITMAPINFO bi; memset(&bi,0,sizeof(bi));
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=W;
    bi.bmiHeader.biHeight=-H;
    bi.bmiHeader.biPlanes=1;
    bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    void*nb=NULL;
    g_pfBmp=CreateDIBSection(g_pfDC,&bi,DIB_RGB_COLORS,&nb,NULL,0);
    SelectObject(g_pfDC,g_pfBmp);
    g_pfBits=(DWORD*)nb; g_pfW=W; g_pfH=H;
    return g_pfBits!=NULL;
}

static void PopupBackground(int W,int H)
{
    for(int i=0;i<W*H;i++) g_pfBits[i]=0x00262629;
    for(int x=0;x<W;x++){ g_pfBits[x]=0x005F5F64; g_pfBits[(H-1)*W+x]=0x005F5F64; }
    for(int y=0;y<H;y++){ g_pfBits[y*W]=0x005F5F64; g_pfBits[y*W+W-1]=0x005F5F64; }
}

static LRESULT CALLBACK PrefsProc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    switch(m){
    case WM_CREATE:{
        int y=32;
        for(int i=0;i<4;i++){ SetRect(&g_modRc[i],14,y,236,y+21); y+=21; }
        y+=26;
        for(int i=0;i<3;i++){ SetRect(&g_pauseRc[i],14,y,236,y+21); y+=21; }
        y+=26;
        for(int i=0;i<2;i++){ SetRect(&g_wfModeRc[i],14,y,236,y+21); y+=21; }
        y+=14;
        SetRect(&g_skinRc,14,y,236,y+21);   y+=23;
        SetRect(&g_rgbCurRc,14,y,236,y+21); y+=23;
        SetRect(&g_clipRc,14,y,236,y+21);   y+=40;
        for(int i=0;i<6;i++) SetRect(&g_swatchRc[i],16+i*35,y,16+i*35+26,y+26);
        y+=54;
        SetRect(&g_sliderRc,18,y,228,y+8);
        return 0;
    }
    case WM_LBUTTONDOWN:{
        POINT p={(short)LOWORD(l),(short)HIWORD(l)};
        for(int i=0;i<4;i++) if(PtInRect(&g_modRc[i],p)){ g_modVK=MOD_VKS[i]; InvalidateRect(h,NULL,TRUE); return 0; }
        for(int i=0;i<3;i++) if(PtInRect(&g_pauseRc[i],p)){ g_pauseVK=PAUSE_VKS[i]; InvalidateRect(h,NULL,TRUE); return 0; }
        for(int i=0;i<2;i++) if(PtInRect(&g_wfModeRc[i],p)){
            g_wfRGB=(i==1);
            RebuildTemplate(SC_WAVE); RenderAll();
            InvalidateRect(h,NULL,TRUE); return 0;
        }
        if(PtInRect(&g_skinRc,p)){ g_showSkin=!g_showSkin; RebuildTemplate(SC_VEC); RenderAll(); InvalidateRect(h,NULL,TRUE); return 0; }
        if(PtInRect(&g_rgbCurRc,p)){ g_rgbAtCursor=!g_rgbAtCursor; InvalidateRect(g_hMain,NULL,FALSE); InvalidateRect(h,NULL,TRUE); return 0; }
        if(PtInRect(&g_clipRc,p)){ g_clipping=!g_clipping; RenderAll(); InvalidateRect(h,NULL,TRUE); return 0; }
        for(int i=0;i<6;i++) if(PtInRect(&g_swatchRc[i],p)){
            g_frameColor=g_swatch[i];
            if(g_hOverlay) InvalidateRect(g_hOverlay,NULL,FALSE);
            InvalidateRect(h,NULL,TRUE); return 0;
        }
        RECT hit=g_sliderRc; InflateRect(&hit,8,10);
        if(PtInRect(&hit,p)){ g_sliderDrag=TRUE; SetCapture(h); ApplySlider(p.x); InvalidateRect(h,NULL,TRUE); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if(g_sliderDrag){ ApplySlider((short)LOWORD(l)); InvalidateRect(h,NULL,TRUE); }
        return 0;
    case WM_LBUTTONUP:
        if(g_sliderDrag){ g_sliderDrag=FALSE; ReleaseCapture(); }
        return 0;
    case WM_ACTIVATE:
        if(LOWORD(w)==WA_INACTIVE&&!g_sliderDrag) DestroyWindow(h);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps);
        RECT rc; GetClientRect(h,&rc);
        int W=rc.right,H=rc.bottom;
        if(W<1||H<1||!EnsurePopupBuf(dc,W,H)){ EndPaint(h,&ps); return 0; }
        PopupBackground(W,H);
        SetBkMode(g_pfDC,TRANSPARENT);

        SelectObject(g_pfDC,g_hFontBold);
        SetTextColor(g_pfDC,RGB(145,145,152));
        TextOutA(g_pfDC,14,10,"Selection (maintenir + clic-glisser)",36);
        SelectObject(g_pfDC,g_hFont);
        for(int i=0;i<4;i++) RadioAA(g_pfBits,W,H,g_pfDC,g_modRc[i],g_modVK==MOD_VKS[i],MOD_NAMES[i]);

        SelectObject(g_pfDC,g_hFontBold);
        SetTextColor(g_pfDC,RGB(145,145,152));
        TextOutA(g_pfDC,14,g_pauseRc[0].top-19,"Touche pause / reprise",21);
        SelectObject(g_pfDC,g_hFont);
        for(int i=0;i<3;i++) RadioAA(g_pfBits,W,H,g_pfDC,g_pauseRc[i],g_pauseVK==PAUSE_VKS[i],PAUSE_NAMES[i]);

        SelectObject(g_pfDC,g_hFontBold);
        SetTextColor(g_pfDC,RGB(145,145,152));
        TextOutA(g_pfDC,14,g_wfModeRc[0].top-19,"Mode waveform",13);
        SelectObject(g_pfDC,g_hFont);
        for(int i=0;i<2;i++) RadioAA(g_pfBits,W,H,g_pfDC,g_wfModeRc[i],(g_wfRGB?1:0)==i,WFMODE_NAMES[i]);

        CheckAA(g_pfBits,W,H,g_pfDC,g_skinRc,g_showSkin,"Afficher la ligne des tons chair");
        CheckAA(g_pfBits,W,H,g_pfDC,g_rgbCurRc,g_rgbAtCursor,"RGB at cursor");
        CheckAA(g_pfBits,W,H,g_pfDC,g_clipRc,g_clipping,"Clipping");

        SelectObject(g_pfDC,g_hFontBold);
        SetTextColor(g_pfDC,RGB(145,145,152));
        TextOutA(g_pfDC,14,g_swatchRc[0].top-19,"Couleur du cadre",16);
        SelectObject(g_pfDC,g_hFont);
        for(int i=0;i<6;i++){
            RECT s=g_swatchRc[i];
            int scx=(s.left+s.right)/2,scy=(s.top+s.bottom)/2;
            int shalf=(s.right-s.left)/2-2;
            AASquare(g_pfBits,W,H,scx,scy,shalf,
                GetRValue(g_swatch[i]),GetGValue(g_swatch[i]),GetBValue(g_swatch[i]),1.0);
            if(g_swatch[i]==g_frameColor)
                AARing(g_pfBits,W,H,scx,scy,shalf+3.0,2.0,90,180,255,1.0);
        }

        SelectObject(g_pfDC,g_hFontBold);
        SetTextColor(g_pfDC,RGB(145,145,152));
        { char t[64]; snprintf(t,sizeof(t),"Luminosite du nuage : %.2fx",g_brightness);
          TextOutA(g_pfDC,14,g_sliderRc.top-23,t,(int)strlen(t)); }
        SelectObject(g_pfDC,g_hFont);
        {
            double ty=(g_sliderRc.top+g_sliderRc.bottom)/2.0;
            double x0=g_sliderRc.left,x1=g_sliderRc.right;
            double t=(g_brightness-0.25)/2.75;
            double kx=x0+t*(x1-x0);
            AALine(g_pfBits,W,H,x0,ty,x1,ty,6.0,66,66,72,1.0);
            AALine(g_pfBits,W,H,x0,ty,kx,ty,6.0,110,180,240,1.0);
            AADisc(g_pfBits,W,H,kx,ty,7.5,235,235,240,1.0);
            AARing(g_pfBits,W,H,kx,ty,7.5,1.2,150,150,158,0.8);
        }

        BitBlt(dc,0,0,W,H,g_pfDC,0,0,SRCCOPY);
        EndPaint(h,&ps);
        return 0;
    }
    case WM_DESTROY:
        g_hPrefs=NULL;
        InvalidateRect(g_hMain,NULL,FALSE);
        return 0;
    }
    return DefWindowProc(h,m,w,l);
}

/* ================= menu Scopes ================= */
static LRESULT CALLBACK ScopesProc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    switch(m){
    case WM_CREATE:{
        int y=14;
        for(int i=SC_WAVE;i<SC_COUNT;i++){
            SetRect(&g_scChkRc[i],14,y,206,y+21);
            y+=23;
        }
        SetRect(&g_scSplitRc,14,y+12,206,y+33);
        return 0;
    }
    case WM_LBUTTONDOWN:{
        POINT p={(short)LOWORD(l),(short)HIWORD(l)};
        BOOL changed=FALSE;
        for(int i=SC_WAVE;i<SC_COUNT;i++){
            if(PtInRect(&g_scChkRc[i],p)){ g_sc[i].on=!g_sc[i].on; changed=TRUE; break; }
        }
        if(!changed&&PtInRect(&g_scSplitRc,p)){ g_split=!g_split; changed=TRUE; }
        if(changed){
            ApplySplitMode();
            FitWindowToContent(g_hMain);
            RecomputeLayout(g_hMain);
            InvalidateRect(h,NULL,TRUE);
        }
        return 0;
    }
    case WM_ACTIVATE:
        if(LOWORD(w)==WA_INACTIVE) DestroyWindow(h);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps);
        RECT rc; GetClientRect(h,&rc);
        int W=rc.right,H=rc.bottom;
        if(W<1||H<1||!EnsurePopupBuf(dc,W,H)){ EndPaint(h,&ps); return 0; }
        PopupBackground(W,H);
        SetBkMode(g_pfDC,TRANSPARENT);
        SelectObject(g_pfDC,g_hFont);

        for(int i=SC_WAVE;i<SC_COUNT;i++)
            CheckAA(g_pfBits,W,H,g_pfDC,g_scChkRc[i],g_sc[i].on,SC_NAME[i]);

        int sy=g_scSplitRc.top-7;
        AALine(g_pfBits,W,H,12,sy,W-12,sy,1.0,90,90,96,0.6);
        CheckAA(g_pfBits,W,H,g_pfDC,g_scSplitRc,g_split,"Split scopes");

        BitBlt(dc,0,0,W,H,g_pfDC,0,0,SRCCOPY);
        EndPaint(h,&ps);
        return 0;
    }
    case WM_DESTROY:
        g_hScopes=NULL;
        InvalidateRect(g_hMain,NULL,FALSE);
        return 0;
    }
    return DefWindowProc(h,m,w,l);
}

static void OpenPopup(HWND owner,const char*cls,HWND*slot,int mi,int w,int h)
{
    if(*slot){ DestroyWindow(*slot); *slot=NULL; return; }
    POINT p={g_miRect[mi].left,g_miRect[mi].bottom+2};
    ClientToScreen(owner,&p);
    *slot=CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls,"",
        WS_POPUP,p.x,p.y,w,h,owner,NULL,g_inst,NULL);
    ShowWindow(*slot,SW_SHOW);
    SetForegroundWindow(*slot);
}

/* ================= barre de menu ================= */
static void MenuLabel(int i,char*out,int n)
{
    switch(i){
    case MI_ONOFF: snprintf(out,n,"%s",(g_hasSelection&&!g_paused)?"ON":"OFF"); break;
    case MI_SCOPES:snprintf(out,n,"Scopes"); break;
    case MI_PREFS: snprintf(out,n,"Preferences"); break;
    case MI_RESET: snprintf(out,n,"Reset"); break;
    default: out[0]=0;
    }
}

static void DrawTopBar(HDC dc,RECT client)
{
    RECT bar={0,0,client.right,MENUBAR_H};
    HBRUSH bg=CreateSolidBrush(UI_BG);
    FillRect(dc,&bar,bg); DeleteObject(bg);
    HPEN sep=CreatePen(PS_SOLID,1,RGB(58,58,63));
    HGDIOBJ op=SelectObject(dc,sep);
    MoveToEx(dc,0,MENUBAR_H-1,NULL); LineTo(dc,client.right,MENUBAR_H-1);
    SelectObject(dc,op); DeleteObject(sep);

    HGDIOBJ of=SelectObject(dc,g_hFont);
    SetBkMode(dc,TRANSPARENT);
    for(int i=0;i<MI_COUNT;i++){
        RECT r=g_miRect[i];
        BOOL hov=(g_hoverMI==i),dim=FALSE;
        COLORREF txt=RGB(214,214,220);
        if(i==MI_ONOFF){
            if(!g_hasSelection){ dim=TRUE; txt=RGB(108,108,114); }
            else txt=g_paused?RGB(224,180,110):RGB(110,232,150);
        }
        if(i==MI_RESET&&!g_hasSelection){ dim=TRUE; txt=RGB(108,108,114); }
        if(i==MI_PREFS&&g_hPrefs) hov=TRUE;
        if(i==MI_SCOPES&&g_hScopes) hov=TRUE;

        if(hov&&!dim){
            HBRUSH hb=CreateSolidBrush(RGB(52,52,58));
            FillRect(dc,&r,hb);
            DeleteObject(hb);
        }
        SetTextColor(dc,txt);
        char lbl[32]; MenuLabel(i,lbl,sizeof(lbl));
        DrawTextA(dc,lbl,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    if(of) SelectObject(dc,of);
}

static int MenuHit(POINT p)
{
    for(int i=0;i<MI_COUNT;i++) if(PtInRect(&g_miRect[i],p)) return i;
    return -1;
}

/* ================= layout : 2 scopes max par ligne ================= */
static int VisibleScopes(int*list)
{
    int n=0;
    list[n++]=SC_VEC;
    if(!g_split){
        for(int i=SC_WAVE;i<SC_COUNT;i++)
            if(g_sc[i].on) list[n++]=i;
    }
    return n;
}

static void FitWindowToContent(HWND hwnd)
{
    RECT cr; GetClientRect(hwnd,&cr);
    RECT wr; GetWindowRect(hwnd,&wr);
    int chromeW=(wr.right-wr.left)-(cr.right-cr.left);
    int chromeH=(wr.bottom-wr.top)-(cr.bottom-cr.top);

    int list[SC_COUNT];
    int n=VisibleScopes(list);
    int rows=(n+1)/2;
    int cols=(n>1)?2:1;

    int cell=380;   /* taille de reference d'une cellule */
    int contentW=cols*cell+(cols-1)*SPLIT_W;
    int contentH=rows*cell+(rows-1)*SPLIT_W;

    int wantW=contentW+2*MARGIN+chromeW;
    int wantH=contentH+2*MARGIN+MENUBAR_H+STATUS_H+chromeH;
    SetWindowPos(hwnd,NULL,0,0,wantW,wantH,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
}

static void RecomputeLayout(HWND hwnd)
{
    RECT rc; GetClientRect(hwnd,&rc);

    int x=8;
    int widths[MI_COUNT]={56,74,98,60};
    for(int i=0;i<MI_COUNT-1;i++){
        g_miRect[i].left=x; g_miRect[i].right=x+widths[i];
        g_miRect[i].top=4;  g_miRect[i].bottom=MENUBAR_H-5;
        x+=widths[i]+6;
    }
    g_miRect[MI_RESET].right=rc.right-8;
    g_miRect[MI_RESET].left=g_miRect[MI_RESET].right-widths[MI_RESET];
    g_miRect[MI_RESET].top=4;
    g_miRect[MI_RESET].bottom=MENUBAR_H-5;

    int contentX=MARGIN;
    int contentY=MENUBAR_H+MARGIN;
    int contentW=(rc.right-rc.left)-2*MARGIN;
    int contentH=(rc.bottom-rc.top)-MENUBAR_H-2*MARGIN-STATUS_H;
    if(contentW<MIN_CANVAS) contentW=MIN_CANVAS;
    if(contentH<MIN_CANVAS) contentH=MIN_CANVAS;

    int list[SC_COUNT];
    int n=VisibleScopes(list);
    int rows=(n+1)/2;

    for(int r=0;r<MAX_ROWS;r++){
        SetRectEmpty(&g_rowSplit[r]);
        g_rowSplitA[r]=g_rowSplitB[r]=-1;
    }

    int rowH=(contentH-(rows-1)*SPLIT_W)/rows;
    if(rowH<MIN_CANVAS) rowH=MIN_CANVAS;

    for(int r=0;r<rows;r++){
        int i0=r*2;
        int cnt=((i0+1)<n)?2:1;      /* 2 scopes max par ligne */
        int rowY=contentY+r*(rowH+SPLIT_W);
        int usable=contentW-(cnt-1)*SPLIT_W;

        double pa=g_sc[list[i0]].prop;
        double pb=(cnt==2)?g_sc[list[i0+1]].prop:0.0;
        double sum=pa+pb;
        if(sum<=0) sum=1;

        int cx=contentX;
        for(int c=0;c<cnt;c++){
            int idx=list[i0+c];
            int colW=(c==cnt-1)?(contentX+contentW-cx)
                               :(int)(usable*pa/sum);
            if(colW<80) colW=80;

            if(idx==SC_VEC){
                int size=(colW<rowH)?colW:rowH;
                if(size<MIN_CANVAS) size=MIN_CANVAS;
                if(size>MAX_CANVAS) size=MAX_CANVAS;
                EnsureScopeBmp(SC_VEC,size,size);
                int ox=cx+(colW-size)/2;
                int oy=rowY+(rowH-size)/2;
                SetRect(&g_sc[SC_VEC].rc,ox,oy,ox+size,oy+size);
            } else {
                EnsureScopeBmp(idx,colW,rowH);
                SetRect(&g_sc[idx].rc,cx,rowY,cx+colW,rowY+rowH);
            }

            cx+=colW;
            if(c==0&&cnt==2){
                SetRect(&g_rowSplit[r],cx,rowY,cx+SPLIT_W,rowY+rowH);
                g_rowSplitA[r]=list[i0];
                g_rowSplitB[r]=list[i0+1];
                cx+=SPLIT_W;
            }
        }
    }

    {
        RECT v=g_sc[SC_VEC].rc;
        SetRect(&g_zoomChkRc,v.left+8,v.bottom-26,v.left+96,v.bottom-6);
    }

    RenderAll();
    InvalidateRect(hwnd,NULL,TRUE);
}

/* ================= fenetre principale ================= */
static void ApplyDarkTitleBar(HWND hwnd)
{
    DWORD light=1,sz=sizeof(light);
    HKEY k;
    if(RegOpenKeyExA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0,KEY_READ,&k)==ERROR_SUCCESS){
        DWORD type=REG_DWORD;
        RegQueryValueExA(k,"AppsUseLightTheme",NULL,&type,(LPBYTE)&light,&sz);
        RegCloseKey(k);
    }
    BOOL dark=(light==0)?TRUE:FALSE;
    DwmSetWindowAttribute(hwnd,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
}

static LRESULT CALLBACK MainProc(HWND hwnd,UINT m,WPARAM wp,LPARAM lp)
{
    switch(m){
    case WM_CREATE:{
        g_hFont=CreateFontA(13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,"Segoe UI");
        g_hFontBold=CreateFontA(12,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH|FF_SWISS,"Segoe UI");

        ApplyDarkTitleBar(hwnd);

        for(int i=0;i<SC_COUNT;i++){
            g_sc[i].dc=CreateCompatibleDC(NULL);
            g_sc[i].prop=1.0;
        }
        g_sc[SC_VEC].on=TRUE;
        RecomputeLayout(hwnd);

        int ox=GetSystemMetrics(SM_XVIRTUALSCREEN);
        int oy=GetSystemMetrics(SM_YVIRTUALSCREEN);
        int ow=GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int oh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
        g_overlayOrigin.x=ox; g_overlayOrigin.y=oy;

        g_hOverlay=CreateWindowExA(
            WS_EX_LAYERED|WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
            OVERLAY_CLASS,"",WS_POPUP,ox,oy,ow,oh,NULL,NULL,g_inst,NULL);
        SetLayeredWindowAttributes(g_hOverlay,RGB(255,0,255),0,LWA_COLORKEY);
        SetWindowDisplayAffinity(g_hOverlay,WDA_EXCLUDEFROMCAPTURE);

        SetTimer(hwnd,1,25,NULL);
        return 0;
    }
    case WM_SETTINGCHANGE:
        ApplyDarkTitleBar(hwnd);
        return 0;
    case WM_GETMINMAXINFO:{
        MINMAXINFO*mi=(MINMAXINFO*)lp;
        mi->ptMinTrackSize.x=MIN_CANVAS+2*MARGIN+20;
        mi->ptMinTrackSize.y=MIN_CANVAS+2*MARGIN+MENUBAR_H+STATUS_H+40;
        return 0;
    }
    case WM_SIZE:
        if(g_sc[SC_VEC].dc) RecomputeLayout(hwnd);
        return 0;
    case WM_SETCURSOR:{
        POINT p; GetCursorPos(&p); ScreenToClient(hwnd,&p);
        for(int r=0;r<MAX_ROWS;r++)
            if(!IsRectEmpty(&g_rowSplit[r])&&PtInRect(&g_rowSplit[r],p)){
                SetCursor(LoadCursor(NULL,IDC_SIZEWE));
                return TRUE;
            }
        break;
    }
    case WM_LBUTTONDOWN:{
        POINT p={(short)LOWORD(lp),(short)HIWORD(lp)};

        for(int r=0;r<MAX_ROWS;r++){
            if(!IsRectEmpty(&g_rowSplit[r])&&PtInRect(&g_rowSplit[r],p)){
                g_splitDragRow=r;
                g_splitDragX=p.x;
                g_dragPropA=g_sc[g_rowSplitA[r]].prop;
                g_dragPropB=g_sc[g_rowSplitB[r]].prop;
                SetCapture(hwnd);
                return 0;
            }
        }

        if(PtInRect(&g_zoomChkRc,p)){
            g_zoom2=!g_zoom2;
            RenderVec();
            InvalidateRect(hwnd,NULL,FALSE);
            return 0;
        }

        int i=MenuHit(p);
        if(i==MI_ONOFF){ if(g_hasSelection) SetTracking(g_paused); }
        else if(i==MI_RESET){ if(g_hasSelection) ResetZone(); }
        else if(i==MI_SCOPES){ OpenPopup(hwnd,SCOPES_CLASS,&g_hScopes,MI_SCOPES,220,140); InvalidateRect(hwnd,NULL,FALSE); }
        else if(i==MI_PREFS){ OpenPopup(hwnd,PREFS_CLASS,&g_hPrefs,MI_PREFS,252,456); InvalidateRect(hwnd,NULL,FALSE); }
        return 0;
    }
    case WM_MOUSEMOVE:{
        POINT p={(short)LOWORD(lp),(short)HIWORD(lp)};

        if(g_splitDragRow>=0){
            int r=g_splitDragRow;
            RECT rc; GetClientRect(hwnd,&rc);
            int contentW=(rc.right-rc.left)-2*MARGIN-SPLIT_W;
            double sum=g_dragPropA+g_dragPropB;
            double dprop=(double)(p.x-g_splitDragX)*sum/(double)(contentW>0?contentW:1);
            double a=g_dragPropA+dprop,b=g_dragPropB-dprop;
            if(a>0.3&&b>0.3){
                g_sc[g_rowSplitA[r]].prop=a;
                g_sc[g_rowSplitB[r]].prop=b;
                RecomputeLayout(hwnd);
            }
            return 0;
        }

        BOOL zh=PtInRect(&g_zoomChkRc,p);
        if(zh!=g_zoomHover){ g_zoomHover=zh; InvalidateRect(hwnd,NULL,FALSE); }

        int i=MenuHit(p);
        if(i!=g_hoverMI){ g_hoverMI=i; InvalidateRect(hwnd,NULL,FALSE); }
        if(!g_tracking){
            TRACKMOUSEEVENT t={sizeof(t),TME_LEAVE,hwnd,0};
            TrackMouseEvent(&t); g_tracking=TRUE;
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if(g_splitDragRow>=0){ g_splitDragRow=-1; ReleaseCapture(); }
        return 0;
    case WM_MOUSELEAVE:
        g_tracking=FALSE;
        if(g_hoverMI!=-1||g_zoomHover){ g_hoverMI=-1; g_zoomHover=FALSE; InvalidateRect(hwnd,NULL,FALSE); }
        return 0;
    case WM_TIMER:{
        if(++g_topmostCounter>=40){
            g_topmostCounter=0;
            SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            if(g_hasSelection&&!g_paused)
                SetWindowPos(g_hOverlay,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        }

        if(g_rgbAtCursor&&++g_cursorCounter>=4){
            g_cursorCounter=0;
            POINT c; GetCursorPos(&c);
            HDC scr=GetDC(NULL);
            COLORREF col=GetPixel(scr,c.x,c.y);
            ReleaseDC(NULL,scr);
            if(col!=CLR_INVALID){
                BYTE nr=GetRValue(col),ng=GetGValue(col),nb=GetBValue(col);
                if(nr!=g_curR||ng!=g_curG||nb!=g_curB){
                    g_curR=nr; g_curG=ng; g_curB=nb;
                    RECT v=g_sc[SC_VEC].rc;
                    RECT ir={v.left,v.bottom-26,v.right,v.bottom};
                    InvalidateRect(hwnd,&ir,FALSE);
                }
            }
        }

        BOOL mod=(GetAsyncKeyState(g_modVK)&0x8000)!=0;
        BOOL lb =(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;
        BOOL pz =(GetAsyncKeyState(g_pauseVK)&0x8000)!=0;

        if(!g_dragging){
            if(mod&&lb){
                POINT cp; GetCursorPos(&cp);
                RECT mr; GetWindowRect(hwnd,&mr);
                if(PtInRect(&mr,cp)) return 0;
                for(int i=SC_WAVE;i<SC_COUNT;i++){
                    if(g_sc[i].wnd){
                        RECT sr; GetWindowRect(g_sc[i].wnd,&sr);
                        if(PtInRect(&sr,cp)) return 0;
                    }
                }
                g_dragging=TRUE; g_paused=FALSE;
                g_dragStart=cp;
                g_selRectScreen.left=g_selRectScreen.right=cp.x;
                g_selRectScreen.top=g_selRectScreen.bottom=cp.y;
                SetWindowPos(g_hOverlay,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                ShowWindow(g_hOverlay,SW_SHOWNOACTIVATE);
            } else if(pz&&g_hasSelection&&!g_paused){
                SetTracking(FALSE);
            } else if(g_hasSelection&&!g_paused){
                if(++g_refreshCounter>=REFRESH_TICKS){
                    g_refreshCounter=0;
                    Capture(g_selRectScreen,FALSE);
                }
            }
        } else {
            POINT c; GetCursorPos(&c);
            g_selRectScreen.left  =min(g_dragStart.x,c.x);
            g_selRectScreen.right =max(g_dragStart.x,c.x);
            g_selRectScreen.top   =min(g_dragStart.y,c.y);
            g_selRectScreen.bottom=max(g_dragStart.y,c.y);
            InvalidateRect(g_hOverlay,NULL,FALSE);

            if(!lb){
                g_dragging=FALSE;
                RECT s=g_selRectScreen;
                if((s.right-s.left)>3&&(s.bottom-s.top)>3) Capture(s,TRUE);
                else {
                    ShowWindow(g_hOverlay,SW_HIDE);
                    g_hasSelection=FALSE;
                    strcpy(g_status,"Selection trop petite - reessayez.");
                    InvalidateRect(hwnd,NULL,FALSE);
                }
                g_refreshCounter=0;
            }
        }
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:{
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);

        HBRUSH bg=CreateSolidBrush(UI_BG);
        RECT body={0,MENUBAR_H,rc.right,rc.bottom};
        FillRect(dc,&body,bg); DeleteObject(bg);

        DrawTopBar(dc,rc);

        int list[SC_COUNT];
        int n=VisibleScopes(list);
        for(int i=0;i<n;i++){
            Scope*s=&g_sc[list[i]];
            if(s->bits&&!IsRectEmpty(&s->rc))
                BitBlt(dc,s->rc.left,s->rc.top,s->w,s->h,s->dc,0,0,SRCCOPY);
        }

        for(int r=0;r<MAX_ROWS;r++){
            if(IsRectEmpty(&g_rowSplit[r])) continue;
            RECT sr=g_rowSplit[r];
            int mx=(sr.left+sr.right)/2;
            HPEN p=CreatePen(PS_SOLID,1,RGB(70,70,76));
            HGDIOBJ op=SelectObject(dc,p);
            MoveToEx(dc,mx,sr.top+8,NULL); LineTo(dc,mx,sr.bottom-8);
            SelectObject(dc,op); DeleteObject(p);
        }

        HGDIOBJ of=SelectObject(dc,g_hFont);
        SetBkMode(dc,TRANSPARENT);

        {
            RECT z=g_zoomChkRc;
            int bx=z.left+2,by=z.top+3;
            HPEN p=CreatePen(PS_SOLID,1,g_zoom2?RGB(90,180,255):RGB(120,120,126));
            HBRUSH b=CreateSolidBrush(g_zoom2?RGB(40,110,170):RGB(46,46,52));
            HGDIOBJ op=SelectObject(dc,p),obb=SelectObject(dc,b);
            RoundRect(dc,bx,by,bx+14,by+14,3,3);
            SelectObject(dc,obb); SelectObject(dc,op);
            DeleteObject(b); DeleteObject(p);
            if(g_zoom2){
                HPEN cp=CreatePen(PS_SOLID,2,RGB(170,230,255));
                HGDIOBJ ocp=SelectObject(dc,cp);
                MoveToEx(dc,bx+3,by+7,NULL);
                LineTo(dc,bx+6,by+10);
                LineTo(dc,bx+11,by+4);
                SelectObject(dc,ocp); DeleteObject(cp);
            }
            SetTextColor(dc,g_zoomHover?RGB(230,230,235):RGB(150,150,158));
            TextOutA(dc,bx+20,z.top+1,"Zoom x2",7);
        }

        if(g_rgbAtCursor){
            char t[48];
            snprintf(t,sizeof(t),"R:%d, G:%d, B:%d",g_curR,g_curG,g_curB);
            SetTextColor(dc,RGB(135,135,142));
            RECT v=g_sc[SC_VEC].rc;
            RECT tr={v.left,v.bottom-24,v.right-8,v.bottom-4};
            DrawTextA(dc,t,-1,&tr,DT_RIGHT|DT_SINGLELINE);
        }

        SetTextColor(dc,RGB(148,148,155));
        RECT sr={MARGIN,rc.bottom-STATUS_H,rc.right-MARGIN,rc.bottom};
        DrawTextA(dc,g_status,-1,&sr,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
        if(of) SelectObject(dc,of);

        EndPaint(hwnd,&ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd,1);
        for(int i=0;i<SC_COUNT;i++){
            if(g_sc[i].wnd){ HWND w=g_sc[i].wnd; g_sc[i].wnd=NULL; DestroyWindow(w); }
            FreeScopeBmp(i);
            if(g_sc[i].dc) DeleteDC(g_sc[i].dc);
        }
        if(g_pfBmp) DeleteObject(g_pfBmp);
        if(g_pfDC) DeleteDC(g_pfDC);
        if(g_hFont) DeleteObject(g_hFont);
        if(g_hFontBold) DeleteObject(g_hFontBold);
        if(g_hOverlay) DestroyWindow(g_hOverlay);
        free(g_sR); free(g_sG); free(g_sB); free(g_sCol);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd,m,wp,lp);
}

int WINAPI WinMain(HINSTANCE hi,HINSTANCE hp,LPSTR cl,int ns)
{
    (void)hp; (void)cl;
    g_inst=hi;
    g_icon=LoadIconA(hi,MAKEINTRESOURCEA(1));
    HBRUSH darkBrush=CreateSolidBrush(UI_BG);

    WNDCLASSA wc; memset(&wc,0,sizeof(wc));
    wc.lpfnWndProc=MainProc; wc.hInstance=hi;
    wc.lpszClassName=MAIN_CLASS;
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wc.hIcon=g_icon;
    wc.hbrBackground=darkBrush;
    RegisterClassA(&wc);

    WNDCLASSA wo; memset(&wo,0,sizeof(wo));
    wo.lpfnWndProc=OverlayProc; wo.hInstance=hi;
    wo.lpszClassName=OVERLAY_CLASS;
    wo.hCursor=LoadCursor(NULL,IDC_CROSS);
    RegisterClassA(&wo);

    WNDCLASSA wpc; memset(&wpc,0,sizeof(wpc));
    wpc.lpfnWndProc=PrefsProc; wpc.hInstance=hi;
    wpc.lpszClassName=PREFS_CLASS;
    wpc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wpc.hbrBackground=darkBrush;
    RegisterClassA(&wpc);

    WNDCLASSA wsc; memset(&wsc,0,sizeof(wsc));
    wsc.lpfnWndProc=ScopesProc; wsc.hInstance=hi;
    wsc.lpszClassName=SCOPES_CLASS;
    wsc.hCursor=LoadCursor(NULL,IDC_ARROW);
    wsc.hbrBackground=darkBrush;
    RegisterClassA(&wsc);

    /* fenetres de scope detachees : fond sombre des la creation, aucune
       zone non-client -> pas de bande blanche */
    WNDCLASSA wsw; memset(&wsw,0,sizeof(wsw));
    wsw.lpfnWndProc=ScopeWndProc; wsw.hInstance=hi;
    wsw.lpszClassName=SCOPEW_CLASS;
    wsw.hCursor=LoadCursor(NULL,IDC_ARROW);
    wsw.hIcon=g_icon;
    wsw.hbrBackground=darkBrush;
    RegisterClassA(&wsw);

    int w=380+2*MARGIN+16;
    int h=380+2*MARGIN+MENUBAR_H+STATUS_H+40;

    g_hMain=CreateWindowExA(WS_EX_TOPMOST,MAIN_CLASS,"ScopeWalker",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,w,h,
        NULL,NULL,hi,NULL);

    if(g_icon){
        SendMessage(g_hMain,WM_SETICON,ICON_BIG,(LPARAM)g_icon);
        SendMessage(g_hMain,WM_SETICON,ICON_SMALL,(LPARAM)g_icon);
    }

    ShowWindow(g_hMain,ns);
    UpdateWindow(g_hMain);

    MSG msg;
    while(GetMessage(&msg,NULL,0,0)){
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
