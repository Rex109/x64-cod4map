/* Original: .\surface.cpp */

#ifndef SURFACE_H
#define SURFACE_H

#include "q_shared.h"
#include "polylib.h"
#include "map.h"
#include "brush.h"

#define MAX_MAP_DRAW_SURFS      0x60000

#define MAX_FACE_WINDING_POINTS 64

#define SUBDIVIDE_EPSILON       0.1f

typedef struct
{
    vec3_t xyz;         /* +0x00 */
    vec2_t st;          /* +0x0c */
    vec2_t lmSt;        /* +0x14 */
    vec3_t normal;      /* +0x1c */
    byte   color[4];    /* +0x28 */
} DrawVert_t;

typedef struct
{
    Material_t  *mtlTex;        /* +0x00 */
    Material_t  *lmapMaterial;  /* +0x04 */
    int          reflectionProbeIndex; /* +0x08 */
    int          primaryLightIndex; /* +0x0c */
    int          contents;      /* +0x10 */
    int          toolFlags;     /* +0x14 */
    int          smoothing;     /* +0x18 */
    int          entityNum;     /* +0x1c */
    int          brushNum;      /* +0x20 */
    int          mapInfoIndex;  /* +0x24 */
    int          vertCount;     /* +0x28 */
    DrawVert_t  *verts;         /* +0x2c */
    int          outputNumber;  /* +0x30 */
    byte         castsSunShadow;/* +0x34 */
    byte         wasCoalesced;  /* +0x35 */
    byte         isPatch;       /* +0x36 */
    byte         isModel;       /* +0x37 */
    union
    {
        side_t *side;
        struct { int width;      int height;   } patch;
        struct { int indexCount; int *indexes; } terrain;
    };
    int          subdivisions;  /* +0x40 */
} DrawSurf_t;

extern FILE       *g_debugPolyFile;                         /* 0x14899cb8 */
extern int         numMapDrawSurfs;                         /* 0x14899cbc */
extern DrawSurf_t  drawSurfs[MAX_MAP_DRAW_SURFS];           /* 0x12f19cb8 */

DrawSurf_t *AllocDrawSurface( void );                                   /* 0x00439f20 */

DrawSurf_t *DrawSurfaceForSide( side_t *side, const winding_t *w,
                                int mapInfoIndex, int entityNum,
                                int brushNum, int outputNumber );       /* 0x00439f80 */

winding_t  *WindingFromDrawSurf( const DrawSurf_t *surf );              /* 0x0043a7f0 */

void SortDrawSurfaces( void );                                          /* 0x00439bc0 */
int  DrawSurfaceCompare( const void *va, const void *vb );              /* 0x00439c20 */

void SubdivideDrawSurfs( Entity_t *ent, Tree_t *tree );                 /* 0x0043a510 */
void SubdivideDrawSurf( DrawSurf_t *surf, winding_t *w, float subdivisions ); /* 0x0043a600 */

void OpenDebugFile( void );                                             /* 0x0043a230 */
void CloseDebugFile( void );                                            /* 0x0043a290 */
void WriteDebugWindingToFile( const side_t *side, const winding_t *w ); /* 0x0043a2c0 */
void WriteDebugTrisToFile( const void *triSurf );                       /* 0x0043a3f0 */

#endif
