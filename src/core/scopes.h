/* ScopeWalker - core/scopes.h
 *
 * Rendu PORTABLE des scopes (aucune dependance a windows.h). Ces fonctions
 * dessinent le nuage de points / les courbes dans le buffer `bits` d'un scope,
 * a partir des echantillons de pixels captures par la coquille specifique a
 * chaque OS. Le texte des graticules reste pour l'instant dans la coquille
 * (voir PORTING.md, phase 1c).
 */
#ifndef SCOPEWALKER_CORE_SCOPES_H
#define SCOPEWALKER_CORE_SCOPES_H

#include <stdint.h>
#include "draw.h"

/* Chroma maximale d'une primaire saturee (rouge/cyan, |UV|=161.19) : definit
   le cercle 100% du vectorscope (cibles et nuage partagent la meme echelle). */
extern const double CHROMA_MAX;

#define CLIP_MAX 2048   /* largeur max d'un panneau pour la detection de clipping */

/* Couleur de fond de l'UI : RGB(32,32,35) au format 0x00RRGGBB. */
#define CORE_BG 0x00202023u

/* Callback de dessin de texte, fourni par la coquille (GDI sous Windows,
   CoreText/bitmap ailleurs). Le cœur calcule position/couleur/chaine ; la
   coquille decide du rendu. `ctx` est opaque (un HDC sous Windows). */
typedef void (*CoreTextFn)(void *ctx,int x,int y,const char *s,
                           uint8_t r,uint8_t g,uint8_t b);

/* Un scope, cote cœur : juste ses deux buffers de pixels et sa taille. */
typedef struct {
    Pixel *bits;   /* cible de rendu (w*h)                        */
    Pixel *tpl;    /* graticule pre-dessine, recopie a chaque frame */
    int    w,h;
    int    on;     /* actif ? (waveform/parade/hist)              */
} CoreScope;

/* Echantillons + parametres de rendu, fournis par la coquille. */
typedef struct {
    const uint8_t  *sR,*sG,*sB;   /* canaux des echantillons */
    const uint16_t *sCol;         /* colonne source de chaque echantillon */
    int    sCount;                /* nombre d'echantillons */
    int    sCols;                 /* nombre de colonnes sources */
    double brightness;            /* luminosite du nuage (0.25..3.0) */
    int    zoom2;                 /* vectorscope : zoom x2 */
    int    clipping;              /* barres de clipping on/off */
    int    wfRGB;                 /* waveform en mode RGB (parade a 3 panneaux) */
    int    showSkin;              /* vectorscope : ligne des tons chair */
} CoreParams;

void CoreRenderVec(CoreScope *s,const CoreParams *p);
void CoreRenderWaveLike(CoreScope *s,const CoreParams *p,int parade);
void CoreRenderHist(CoreScope *s,const CoreParams *p);

/* Graticules : dessinent la geometrie dans `bits` et emettent le texte via le
   callback. Ne recopient PAS vers `tpl` : la coquille s'en charge (elle doit
   d'abord vider le lot GDI sous Windows). */
void CoreTplVec(CoreScope *s,const CoreParams *p,CoreTextFn text,void *tctx);
void CoreTplWaveLike(CoreScope *s,const CoreParams *p,int parade,
                     CoreTextFn text,void *tctx);
void CoreTplHist(CoreScope *s,const CoreParams *p,CoreTextFn text,void *tctx);

#endif /* SCOPEWALKER_CORE_SCOPES_H */
