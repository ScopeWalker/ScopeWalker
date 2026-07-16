/* ScopeWalker - core/draw.c
 * Implementation des primitives portables declarees dans draw.h. */
#include "draw.h"

/* etat du PRNG partage (Jitter/SeedRng sont inline dans l'en-tete). */
uint32_t g_rng = 2463534242u;

void ComputeUVf(double R,double G,double B,double *u,double *v)
{
    double Y=0.299*R+0.587*G+0.114*B;
    *u=(B-Y)*0.492;
    *v=(R-Y)*0.877;
}

void AACircle(Pixel *buf,int W,int H,int cx,int cy,int R,
              uint8_t r,uint8_t g,uint8_t b,double a)
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

void AALine(Pixel *buf,int W,int H,double ax,double ay,double bx,double by,
            double wid,uint8_t r,uint8_t g,uint8_t b,double a)
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

void AASquare(Pixel *buf,int W,int H,int cx,int cy,int half,
              uint8_t r,uint8_t g,uint8_t b,double a)
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

void AADisc(Pixel *buf,int W,int H,double cx,double cy,double R,
            uint8_t r,uint8_t g,uint8_t b,double a)
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

void AARing(Pixel *buf,int W,int H,double cx,double cy,double R,double wid,
            uint8_t r,uint8_t g,uint8_t b,double a)
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
