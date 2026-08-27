/* Original: .\tris_lightmap.cpp */

#ifndef TRIS_LIGHTMAP_H
#define TRIS_LIGHTMAP_H


#include "tris.h"
#include "q_shared.h"
#include "lightmaps.h"


#define LIGHTMAP_NONE           31

#define LMAP_WIDTH_MIN          512
#define LMAP_HEIGHT_MIN         512

#define LMAP_RECLAIM_SIZE       8

#define LMAP_TEXEL              0.001953125f

#define MAX_LMAP_MATERIALS      0x990

#define MAX_MTL_SORT_INDEX      0x4c8

#define LMAP_GROUP_BOUNDS_EXPAND    0.0000732421875f
#define LMAP_COPLANAR_DOT           0.984f
#define LMAP_OPPOSED_DOT            (-0.999f)
#define LMAP_PLANE_EPSILON          0.1f
#define LMAP_SHIFT_EPSILON          0.00009765625f    /* 0x38cccccd */
#define LMAP_POINT_EPSILON          0.01f             /* 0x3c23d70a */

typedef struct lmapGroup_s
{
    vec2_t              bounds[2];  /* +0x00, +0x08 */
    unsigned int        id;         /* +0x10 */
    unsigned int       *mergedIds;  /* +0x14 */
    TriSurf_t          *firstSurf;  /* +0x18 */
    struct lmapGroup_s *prev;       /* +0x1c */
    struct lmapGroup_s *next;       /* +0x20 */
} lmapGroup_t;

typedef struct lmapSurf_s
{
    vec2_t       bounds[2];     /* +0x00, +0x08 */
    vec4_t       vecs[2];       /* +0x10, +0x20 */
    lmapGroup_t *group;         /* +0x30 */
    TriSurf_t   *prevInGroup;   /* +0x34 */
    TriSurf_t   *nextInGroup;   /* +0x38 */
    TriSurf_t   *nextInMtl;     /* +0x3c */
} lmapSurf_t;

typedef struct
{
    short w;
    short h;
} lmapSpan_t;

/* trisLmapGlob  0x2b80e330 */
typedef struct
{
    TriSurf_t    *surf;                             /* 0x2b80e330 */
    lmapGroup_t  *groups;                           /* 0x2b80e334 */
    unsigned int  nextGroupId;                      /* 0x2b80e338 */
    int           sortedMtlCount;                   /* 0x2b80e33c */
    TriSurf_t    *sortedMtl[MAX_LMAP_MATERIALS];    /* 0x2b80e340 */
    int           surfCountLmap;                    /* 0x2b810980 */
    int           surfCountNoLmap;                  /* 0x2b810984 */
    TriSurf_t   **surfArray;                        /* 0x2b810988 */
    int           spanX;                            /* 0x2b81098c */
    int           spanY;                            /* 0x2b810990 */
    const vec4_t *lmapVecs;                         /* 0x2b810994 */
    lmapSpan_t   *span;                             /* 0x2b810998 */
} trisLmapGlob_t;

extern trisLmapGlob_t trisLmapGlob;


TriSurf_t *TrisLmapBuildGroups( TriSurf_t *listHead,
                                const vec3_t mins, const vec3_t maxs );  /* 0x00451600 */

void TrisLmapMergeAcrossLists( TriSurf_t *listHead,
                               const vec3_t mins, const vec3_t maxs );   /* 0x00453b80 */
void TrisLmapAddListToGrid( TriSurf_t *listHead );                       /* 0x00453c10 */

void TrisLmapSplitPropsByGroup( void );                                  /* 0x00453c60 */

void TrisLmapAssign( void );                                             /* 0x004540b0 */

bool TrisLmapWantsLightmap( const TriSurfProps_t *props );                /* 0x00451590 */

void LmapSurfBounds2D( const vec4_t *vecs, const winding_t *w, vec2_t bounds[2] );
                                                                         /* 0x00454010 */

#endif
