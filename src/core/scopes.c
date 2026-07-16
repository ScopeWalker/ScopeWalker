/* ScopeWalker - core/scopes.c
 * Rendu portable des scopes. Logique identique a la version Win32 d'origine,
 * mais alimentee par CoreScope/CoreParams au lieu des variables globales. */
#include <string.h>
#include "scopes.h"

const double CHROMA_MAX = 161.19;

/* ================= rendu vectorscope ================= */
void CoreRenderVec(CoreScope *s,const CoreParams *p)
{
    if(!s->bits||!s->tpl||s->w<=0||s->h<=0) return;
    int W=s->w,H=s->h;
    /* W*H (et non W*W) : un bitmap non carre ne doit jamais deborder */
    memcpy(s->bits,s->tpl,(size_t)W*H*sizeof(Pixel));
    if(p->sCount<=0) return;

    int D=(W<H)?W:H;
    double cx0=W/2.0,cy0=H/2.0;
    double R1=D*0.44;
    /* MEME echelle que les cibles : une couleur pure tombe sur son repere */
    double k=(R1/CHROMA_MAX)*(p->zoom2?2.0:1.0);
    double gain=8000.0/(double)p->sCount;
    if(gain<0.05)gain=0.05;
    if(gain>0.9)gain=0.9;
    gain*=p->brightness;
    int dot=(D>=560)?2:1;

    SeedRng(0x1234567u);
    for(int i=0;i<p->sCount;i++){
        uint8_t R=p->sR[i],G=p->sG[i],B=p->sB[i];
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
void CoreRenderWaveLike(CoreScope *s,const CoreParams *p,int parade)
{
    if(!s->on||!s->bits||!s->tpl||s->w<=0) return;
    int W=s->w,H=s->h;
    memcpy(s->bits,s->tpl,(size_t)W*H*sizeof(Pixel));
    if(p->sCount<=0||p->sCols<=0) return;

    int rgb = parade || p->wfRGB;

    double gain=6000.0/(double)p->sCount;
    if(gain<0.05)gain=0.05;
    if(gain>0.9)gain=0.9;
    gain*=p->brightness;

    int panelW=rgb?(W/3):W;
    if(panelW<2) return;

    double xs=(p->sCols>1)?((double)(panelW-1)/(double)(p->sCols-1)):0.0;
    double ys=(double)(H-1)/255.0;

    static uint8_t clipLo[3*CLIP_MAX],clipHi[3*CLIP_MAX];
    int nPanels=rgb?3:1;
    int doClip=(p->clipping && panelW<=CLIP_MAX);
    if(doClip){
        memset(clipLo,0,(size_t)nPanels*panelW);
        memset(clipHi,0,(size_t)nPanels*panelW);
    }

    SeedRng(0x7654321u);
    for(int i=0;i<p->sCount;i++){
        uint8_t R=p->sR[i],G=p->sG[i],B=p->sB[i];
        double fx=(double)p->sCol[i]*xs+Jitter()*(xs>1.0?xs:1.0);
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
        for(int pn=0;pn<nPanels;pn++){
            for(int bx=0;bx<panelW;bx++){
                int i2=pn*panelW+bx;
                int x=pn*panelW+bx;
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
void CoreRenderHist(CoreScope *s,const CoreParams *p)
{
    if(!s->on||!s->bits||!s->tpl||s->w<=0) return;
    int W=s->w,H=s->h;
    memcpy(s->bits,s->tpl,(size_t)W*H*sizeof(Pixel));
    if(p->sCount<=0) return;

    static double bins[3][256],sm[3][256];
    memset(bins,0,sizeof(bins));
    for(int i=0;i<p->sCount;i++){
        bins[0][p->sR[i]]+=1.0;
        bins[1][p->sG[i]]+=1.0;
        bins[2][p->sB[i]]+=1.0;
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

    /* Table de la courbe gamma (0.45), calculee une seule fois. On interpole
       lineairement dedans au lieu d'appeler pow() par colonne de pixels :
       le rendu reste identique, mais on economise des milliers de pow(). */
    #define HIST_LUT 1024
    static double powlut[HIST_LUT+1];
    static int lutReady=0;
    if(!lutReady){
        for(int i=0;i<=HIST_LUT;i++) powlut[i]=pow((double)i/HIST_LUT,HIST_GAMMA);
        lutReady=1;
    }

    static const uint8_t col[3][3]={{255,70,70},{70,235,110},{90,130,255}};
    double alpha=0.5*p->brightness;
    if(alpha>1.0) alpha=1.0;

    /* hauteur de la courbe pour chaque colonne de pixels */
    static double hgt[3][4096];
    int WW=(W>4096)?4096:W;
    double invMax=(maxB>0)?1.0/maxB:0.0;
    for(int c=0;c<3;c++){
        for(int x=0;x<WW;x++){
            double fb=(WW>1)?((double)x*255.0/(double)(WW-1)):0.0;
            int b0=(int)fb; if(b0>255)b0=255;
            int b1=(b0<255)?b0+1:255;
            double f=fb-b0;
            double v=(1.0-f)*sm[c][b0]+f*sm[c][b1];
            double nv=v*invMax;
            if(nv<0)nv=0;
            if(nv>1)nv=1;
            double fi=nv*HIST_LUT;
            int li=(int)fi;
            double t=(li<HIST_LUT)?(powlut[li]+(powlut[li+1]-powlut[li])*(fi-li))
                                  :powlut[HIST_LUT];
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
