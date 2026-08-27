/* Original: .\tris.cpp */

#ifndef TRIS_H
#define TRIS_H

#include "q_shared.h"
#include "polylib.h"
#include "fixedpoint.h"
#include "bspfile.h"
#include "surface.h"
#include "map.h"


#define TRIS_TRANSIENT_NONE                 0
#define TRIS_TRANSIENT_WORIG                1
#define TRIS_TRANSIENT_MERGE_CHECK_COUNT    2
#define TRIS_TRANSIENT_COALESCE             3
#define TRIS_TRANSIENT_LMAP                 4

#define LIGHTMAP_NONE             31

#define REFLECTION_PROBE_INVALID  0xff

#define MAX_TRI_SURF_LISTS        ( MAX_MAP_CELLS + MAX_MAP_CULLGROUPS )

#define MAX_MAP_TRIANGLES         0xa0000
#define MAX_MAP_VERTEXES          0x1e0000

#define MAX_LAYERED_MATERIALS     0x4c8

#define MAX_TRI_SUBGROUPS         0xffff

#define MAX_ENTITY_TRI_SOUPS      ( 32 * 1024 )

typedef struct CoalesceNode_s
{
    struct TriSurfProps_s *props;   /* +0x00 */
    struct CoalesceNode_s *next;    /* +0x04 */
} CoalesceNode_t;

struct lmapSurf_s;

typedef struct TriSurfProps_s
{
    DrawSurf_t            *ds;              /* +0x00 */
    int                    lmapGroupId;     /* +0x04 */
    int                    lmapIndex;       /* +0x08 */
    vec4_t                 texVecs[2];      /* +0x0c */
    vec4_t                 lmapVecs[2];     /* +0x2c */
    vec4_t                 colorVecs[4];    /* +0x4c */
    float                  subdivisions;    /* +0x8c */
    byte                   nodraw;          /* +0x90 */
    byte                   mergeTouching;   /* +0x91 */
    byte                   pad92[2];        /* +0x92 */
    vec4_t                 plane;           /* +0x94 */
    CoalesceNode_t        *coalesceChain;   /* +0xa4 */
    struct TriSurfProps_s *next;            /* +0xa8 */
} TriSurfProps_t;

typedef struct TriSurf_s
{
    winding_t         *w;             /* +0x00 */
    FixedWinding_t    *fw;            /* +0x04 */
    TriSurfProps_t    *props;         /* +0x08 */

    union                             /* +0x0c */
    {
        struct
        {
            vec3_t     mins;          /* +0x0c */
            vec3_t     maxs;          /* +0x18 */
        };
        fixedvec3_t    bounds[2];     /* +0x0c */
    };

    union                             /* +0x24 */
    {
        winding_t         *wOrig;
        int                checkCount;
        struct
        {
            short          checkCount;    /* +0x24 */
            byte           pad26;         /* +0x26 */
            byte           alreadyVisited; /* +0x27 */
        }                  coalesce;
        struct lmapSurf_s *lmap;
    } transient;

    struct TriSurf_s  *visGroupPrev;  /* +0x28 */
    struct TriSurf_s  *visGroupNext;  /* +0x2c */
    struct TriSurf_s  *gridTreePrev;  /* +0x30 */
    struct TriSurf_s  *gridTreeNext;  /* +0x34 */
    int                holeCount;     /* +0x38 */
    winding_t        **holes;         /* +0x3c */
} TriSurf_t;                          /* sizeof == 0x40 */

typedef struct LayeredMaterialRef_s
{
    vec3_t                       pos;      /* +0x00 */
    vec3_t                       normal;   /* +0x0c */
    struct LayeredMaterialRef_s *next;     /* +0x18 */
} LayeredMaterialRef_t;

typedef struct LayeredMaterialDesc_s
{
    char                  name[0x40];      /* +0x00 */
    unsigned short        mtlIndex;        /* +0x40 */
    byte                  layerCount;      /* +0x42 */
    byte                  normalMapCount;  /* +0x43 */
    byte                  region[4];       /* +0x44 */
    Material_t           *mtlRaw[5];       /* +0x48 */
    int                   useCount;        /* +0x5c */
    float                 totalArea;       /* +0x60 */
    LayeredMaterialRef_t *refPoints;       /* +0x64 */
} LayeredMaterialDesc_t;

typedef struct
{
    short          visGroupIndex;        /* +0x00 */
    short          triGroupId;           /* +0x02 */
    byte           reflectionProbeIndex; /* +0x04 */
    byte           lmapIndex;            /* +0x05 */
    byte           primaryLightIndex;    /* +0x06 */
    byte           castsSunShadow;       /* +0x07 */
    LayeredMaterialDesc_t *lyrMtlDesc;    /* +0x08 */
    int            vertIndices[3];       /* +0x0c */
    vec3_t         texVecs[3][2];        /* +0x18 */
} Tri_t;

typedef struct
{
    vec3_t xyz;               /* +0x00 */
    vec3_t xyzOrig;           /* +0x0c */
    vec2_t texCoord[5];       /* +0x18 */
    vec2_t lmapCoord;         /* +0x40 */
    vec3_t normal;            /* +0x48 */
    byte   color[4];          /* +0x54 */
    vec3_t smoothNormal;      /* +0x58 */
    int    smoothing;         /* +0x64 */
} TriVert_t;

typedef struct
{
    unsigned short materialIndex;        /* +0x00 */
    byte           reflectionProbeIndex; /* +0x02 */
    byte           lmapIndex;            /* +0x03 */
    byte           primaryLightIndex;    /* +0x04 */
    byte           castsSunShadow;       /* +0x05 */
    unsigned short pad06;                /* +0x06 */
    LayeredMaterialDesc_t *lyrMtlDesc;   /* +0x08 */
    int            firstVert;            /* +0x0c */
    int            vertCount;            /* +0x10 */
    int            indexCount;           /* +0x14 */
    int            firstIndex;           /* +0x18 */
} TrisSoup_t;

typedef struct
{
    int    firstTri;      /* +0x00 */
    int    triCount;      /* +0x04 */
    vec3_t mins;          /* +0x08 */
    vec3_t maxs;          /* +0x14 */
    vec3_t extents;       /* +0x20 */
} TriGroup_t;

typedef struct
{
    BspTriSoup_t          *soup;         /* +0x00 */
    LayeredMaterialDesc_t *lyrMtlDesc;   /* +0x04 */
    int                    firstVertex;  /* +0x08 */
    int                    lastVertex;   /* +0x0c */
} TrisReorderEntry_t;

#define REORDER_INDEX_LIMIT       0x80000

#define MAX_MERGE_DUP_VERTS       0x20000

#define MAX_TRI_SOUP_VERTS        0x154b
#define MAX_TRI_SOUP_INDICES      0x181

#define MAX_AABB_LEAF_SURFACES    0x10


typedef struct
{
    int             type;                    /* +0x00 */
    const char     *name;                    /* +0x04 */
    int             maxVertCount;            /* +0x08 */
    int             vertCount;               /* +0x0c */
    BspDrawVert_t  *verts;                   /* +0x10 */
    byte           *vertLayerData;           /* +0x14 */
    int             diskVertCount;           /* +0x18 */
    BspDrawVert_t  *diskVerts;               /* +0x1c */
    int             diskVertLayerDataUsed;   /* +0x20 */
    byte           *diskVertLayerData;       /* +0x24 */
    int             maxIndexCount;           /* +0x28 */
    int             indexCount;              /* +0x2c */
    unsigned short *indices;                 /* +0x30 */
    int             triCount;                /* +0x34 */
    BspTriSoup_t   *tris;                    /* +0x38 */
    int             aabbTreeCount;           /* +0x3c */
    BspAabbTree_t  *aabbTrees;               /* +0x40 */
    BspCullGroup_t *cullGroups;              /* +0x44 */
} TrisContext_t;

extern TrisContext_t   triGlobContext[TRIS_TYPE_COUNT];             /* 0x16ed9cd8 */
extern int             triGlobSelfTjuncCount;                       /* 0x16ed9d7c */
extern double          triGlobPhaseStartTime;                       /* 0x16ed9d80 */
extern int             triGlobTransientMode;                        /* 0x16ed9d88 */
extern int             triGlobLyrMtlCount;                          /* 0x16ed9d8c */
extern LayeredMaterialDesc_t triGlobLyrMtlDesc[MAX_LAYERED_MATERIALS];  /* 0x16ed9d90 */
extern vec3_t          triGlobListBounds[MAX_TRI_SURF_LISTS][2];    /* 0x16ef8ed0 */
extern TriSurf_t      *triGlobSurfLists[MAX_TRI_SURF_LISTS];        /* 0x16f0aed0 */
extern TriSurfProps_t *triGlobPropsList;                            /* 0x16f0ded0 */
extern int             triGlobSurfCount;                            /* 0x16f0ded4 */
extern Tri_t           triGlobTris[MAX_MAP_TRIANGLES];              /* 0x16f0ded8 */
extern TriVert_t       triGlobVerts[MAX_MAP_VERTEXES];               /* 0x1ab0ded8 */
extern int             triGlobTriCount;                             /* 0x26e0ded8 */
extern int             triGlobVertCount;                            /* 0x26e0dedc */

extern int             g_degenerateTrisRemoved;                     /* 0x2b80def0 */
extern int             g_unmergedVertCount;                         /* 0x2b80dee8 */

int   GetTrisTransientMode( void );                                /* 0x0043c6c0 */
void  SetTrisTransientMode( int mode, int keepExisting );          /* 0x0043c6d0 */
int   Tris_GetSurfListCount( void );                                /* 0x0043c760 */
int   Tris_GetSurfCount( void );                                    /* 0x0043d070 */

void  Tris_ForEachSurf( void (*callback)( TriSurf_t *, bool ), TriSurf_t *targetSurf ); /* 0x0043d080 */

TriSurf_t *AllocTriSurf( winding_t *w, TriSurfProps_t *props );      /* 0x0043d0f0 */
TriSurf_t *CopyTriSurf( const TriSurf_t *surf );                     /* 0x0043d140 */
TriSurf_t *InsertTriSurfAfter( FixedWinding_t *fw, TriSurfProps_t *props,
                               TriSurf_t *prev );                    /* 0x0043d3b0 */
TriSurf_t *PrependTriSurf( winding_t *w, TriSurfProps_t *props,
                           TriSurf_t **listHead );                   /* 0x0043fe90 */
void  Tris_UnlinkSurf( TriSurf_t *surf, TriSurf_t **listHead );      /* 0x0043d1d0 */
void  Tris_FreeSurface( TriSurf_t *surf );                           /* 0x0043d260 */
void  Tris_FreeAllProps( void );                                     /* 0x0043d450 */

TriSurfProps_t *CopyTriSurfProps( const TriSurfProps_t *props );      /* 0x0043dc80 */
bool  TriSurfPropsGroupable( const TriSurfProps_t *p0, const TriSurfProps_t *p1,
                             const winding_t *testPoints );           /* 0x0043d550 */
bool  TriSurfPropsGroupableFixed( const TriSurfProps_t *p0, const TriSurfProps_t *p1,
                                  const FixedWinding_t *testPoints );  /* 0x0043dc40 */
bool  CoalesceChainGroupable( const CoalesceNode_t *c0, const CoalesceNode_t *c1,
                              const winding_t *testPoints );           /* 0x0043db20 */

void  Tris_ClipWindingEpsilon( const winding_t *in, const winding_t *inOrig,
                               const vec3_t planeNormal, float planeDist,
                               float epsilon, winding_t **front,
                               winding_t **back );                     /* 0x0043c790 */

void  Tris_ValidateSurfLmapCoords( const winding_t *w, const TriSurfProps_t *props ); /* 0x0043c560 */

bool  PointInsideTriangle( const vec3_t point, const winding_t *w, const vec4_t plane ); /* 0x0043dd30 */
int   Tris_LayeredMaterialIndex( const LayeredMaterialDesc_t *desc );   /* 0x0043c510 */

void  TrisTimerCheck( void );                                          /* 0x0043e9d0 */
void  Tris_StartPhase( const char *description );                      /* 0x0043ea30 */

void  TriangulateEntity( Entity_t *e, Tree_t *tree );                  /* 0x0043ea70 */
void  Tris_EmitEntityTris( Entity_t *e, Tree_t *tree );                /* 0x0043e7c0 */
void  Tris_ReportLayeredMaterialUsage( void );                         /* 0x0043e5e0 */
void  Tris_InitContexts( void );                                       /* 0x00449ca0 */
void  Tris_FinishBSPTris( void );                                      /* 0x00449e50 */

unsigned short GroupTriSurfsIntoSubgroups( Tri_t **triLists, const int *triCounts,
                                  int listCount, const int *indexMap ); /* 0x0043df00 */

int   Tris_MergeDuplicateVerts( TrisContext_t *context, int firstVert, int vertCount,
                                unsigned short *indices, int indexCount,
                                Tri_t *firstTri, int lmapIndex );        /* 0x00447cc0 */
void  Tris_EmitTriSoupRecord( TrisContext_t *context, const TrisSoup_t *soup ); /* 0x00449470 */
void  Tris_PartitionTriSoup( TrisContext_t *context, TrisSoup_t *soup,
                             bool allowGroupSplit );                     /* 0x00448230 */
void  Tris_BuildAabbTree( TrisContext_t *context );                      /* 0x00449840 */
void  Tris_ReorderTriSurfaces( TrisContext_t *context );                 /* 0x00449e90 */
void  Tris_ReorderTriSurfaceRun( TrisContext_t *context, int entityIndex,
                                 TrisReorderEntry_t *reorderList,
                                 int reorderCount );                     /* 0x0044a550 */
int   Tris_ReorderAndOptimizeVerts( TrisContext_t *context,
                                    TrisReorderEntry_t *reorderList,
                                    int reorderCount );                  /* 0x0044a690 */

#endif
