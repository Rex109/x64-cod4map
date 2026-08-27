/* Original: .\tris_tesselate.cpp */

#ifndef TRIS_TESSELATE_H
#define TRIS_TESSELATE_H


#include "tris.h"
#include "q_shared.h"
#include "polylib.h"


#ifndef MAX_POINTS_ON_CONCAVE_WINDING
#define MAX_POINTS_ON_CONCAVE_WINDING   16384
#endif

#define TRI_CLIP_EAR_FORBIDDING_COINCIDENT  0
#define TRI_CLIP_EAR_ALLOWING_COINCIDENT    1

#define NUM_TRI_CLIP_EAR_PASSES             12

#ifndef EQUAL_EPSILON
#define EQUAL_EPSILON                       0.001
#endif

#define TESS_SPLIT_PERP_EPSILON             0.1225
#define TESS_SPLIT_EDGE_MARGIN              0.06125

typedef struct
{
    double normal[2];   /* +0x00, +0x08 */
    double dist;        /* +0x10 */
    double alongDist;   /* +0x18 */
    double length;      /* +0x20 */
    double height;      /* +0x28 */
} triSide_t;            /* sizeof == 0x30 */

typedef struct
{
    int   edgeIdx;      /* +0x00 */
    float position;     /* +0x04 */
    int   vertIdx;      /* +0x08 */
} tessSplitPoint_t;     /* sizeof == 0x0c */

typedef void ( *TessTriCallback_t )( const winding_t *w, const winding_t *wOrig,
                                     TriSurfProps_t *props,
                                     int i0, int i1, int i2, int visGroupIndex );

typedef struct
{
    qboolean        ownsWinding;    /* +0x00 */
    winding_t      *w;              /* +0x04 */
    winding_t      *wOrig;          /* +0x08 */
    TriSurfProps_t *props;          /* +0x0c */
    int             axisX;          /* +0x10 */
    int             axisY;          /* +0x14 */
    int             visGroupIndex;  /* +0x18 */
    unsigned int    ptCount;        /* +0x1c */
    int             indices[MAX_POINTS_ON_CONCAVE_WINDING];  /* +0x20 */
    TessTriCallback_t TriCallback;  /* +0x10020 */
} tessWork_t;

extern int g_tessSplitCount;        /* 0x2b8209d0 */
extern int g_tessDegenerateCount;   /* 0x2b8209d4 */
extern int g_tessWindingCount;      /* 0x2b8209d8 */


void TesselateSplitWinding( winding_t **w, winding_t **wOrig,
                            const vec3_t planeNormal );             /* 0x0045aec0 */

void TesselateSurfaceWinding( TriSurf_t *surf );                    /* 0x0045ae40 */

qboolean TesselateWinding( TriSurf_t *surf, int visGroupIndex,
                           TessTriCallback_t triCallback );         /* 0x0045b6f0 */

void TesselateRemoveDegenerateEdges( winding_t *w, winding_t *wOrig );  /* 0x0045b500 */

TriSurf_t *TrisLoadWindingBin( void );                              /* 0x0045ac40 */
void       TrisSaveWindingBin( const TriSurf_t *surf );             /* 0x0045ad90 */

void       TesselateFailed( const TriSurf_t *surf );                /* 0x0045ad70 */

void TesselateNoopCallback( const winding_t *w, const winding_t *wOrig,
                            TriSurfProps_t *props,
                            int i0, int i1, int i2, int visGroupIndex );  /* 0x0045ad60 */
void       TrisTestWindingBin( void );                              /* 0x0045abf0 */

#endif
