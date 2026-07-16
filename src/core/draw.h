/* ScopeWalker - core/draw.h
 *
 * Primitives de dessin PORTABLES (aucune dependance a windows.h).
 * Elles ecrivent directement dans un buffer de pixels 32 bits au format
 * 0x00RRGGBB (identique a la disposition d'un DIB Windows top-down), donc
 * ce meme code sert de socle commun a toutes les plateformes.
 */
#ifndef SCOPEWALKER_CORE_DRAW_H
#define SCOPEWALKER_CORE_DRAW_H

#include <stdint.h>
#include <math.h>

/* Un pixel : 8 bits inutilises + R + G + B (0x00RRGGBB). */
typedef uint32_t Pixel;

/* ================= PRNG (dithering anti-moire) =================
 * xorshift32. Garde en inline dans l'en-tete : appele des milliers de fois
 * par frame dans les boucles chaudes, cross-unite de compilation. */
extern uint32_t g_rng;

static inline void SeedRng(uint32_t s){ g_rng = s ? s : 2463534242u; }

static inline double Jitter(void)
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return (double)(g_rng >> 8) * (1.0/16777216.0) - 0.5;
}

/* ================= mixage de pixels (inline, chemin chaud) ================= */

/* alpha-blend d'une couleur sur le pixel (x,y) avec couverture a in [0,1]. */
static inline void BlendPx(Pixel *buf,int W,int H,int x,int y,
                           uint8_t r,uint8_t g,uint8_t b,double a)
{
    if(x<0||x>=W||y<0||y>=H||a<=0.0) return;
    if(a>1.0) a=1.0;
    Pixel c=buf[y*W+x];
    uint8_t cr=(uint8_t)((c>>16)&0xFF),cg=(uint8_t)((c>>8)&0xFF),cb=(uint8_t)(c&0xFF);
    buf[y*W+x]=((Pixel)(uint8_t)(r*a+cr*(1-a))<<16)
              |((Pixel)(uint8_t)(g*a+cg*(1-a))<<8)
              | (Pixel)(uint8_t)(b*a+cb*(1-a));
}

/* accumulation additive (nuage de points des scopes). */
static inline void AddPx(Pixel *buf,int W,int x,int y,
                         uint8_t R,uint8_t G,uint8_t B,double gain)
{
    Pixel c=buf[y*W+x];
    int r=(int)((c>>16)&0xFF)+(int)(R*gain);
    int g=(int)((c>>8)&0xFF)+(int)(G*gain);
    int b=(int)(c&0xFF)+(int)(B*gain);
    if(r>255)r=255;
    if(g>255)g=255;
    if(b>255)b=255;
    buf[y*W+x]=((Pixel)r<<16)|((Pixel)g<<8)|(Pixel)b;
}

/* ================= conversion couleur ================= */
/* R,G,B (0..255) -> composantes de chrominance U,V (echelle Rec.601). */
void ComputeUVf(double R,double G,double B,double *u,double *v);

/* ================= primitives anti-aliasees ================= */
void AACircle(Pixel *buf,int W,int H,int cx,int cy,int R,
              uint8_t r,uint8_t g,uint8_t b,double a);
void AALine(Pixel *buf,int W,int H,double ax,double ay,double bx,double by,
            double wid,uint8_t r,uint8_t g,uint8_t b,double a);
void AASquare(Pixel *buf,int W,int H,int cx,int cy,int half,
              uint8_t r,uint8_t g,uint8_t b,double a);
void AADisc(Pixel *buf,int W,int H,double cx,double cy,double R,
            uint8_t r,uint8_t g,uint8_t b,double a);
void AARing(Pixel *buf,int W,int H,double cx,double cy,double R,double wid,
            uint8_t r,uint8_t g,uint8_t b,double a);

#endif /* SCOPEWALKER_CORE_DRAW_H */
