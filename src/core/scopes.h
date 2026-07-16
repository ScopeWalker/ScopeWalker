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
} CoreParams;

void CoreRenderVec(CoreScope *s,const CoreParams *p);
void CoreRenderWaveLike(CoreScope *s,const CoreParams *p,int parade);
void CoreRenderHist(CoreScope *s,const CoreParams *p);

#endif /* SCOPEWALKER_CORE_SCOPES_H */
