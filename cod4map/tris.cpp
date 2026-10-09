/* Original: .\tris.cpp */

#include "tris.h"
#include "tris_gridtree.h"
#include "tris_mergeverts.h"
#include "tris_mergeconcave.h"
#include "tris_sunshadow.h"
#include "surface.h"
#include "materials.h"
#include "lightmaps.h"
#include "errors.h"
#include "portals.h"
#include "patch.h"
#include "mesh.h"
#include "writebsp.h"
#include "brush_sides.h"
#include "facebsp.h"
#include "tree.h"
#include "polylib.h"
#include "poly2d.h"
#include "aabbtree.h"
#include "com_math.h"
#include "com_vector.h"
#include "surfaceflags.h"
#include "com_convexhull.h"
#include "cmdlib.h"
#include "assertive.h"
#include "tangentspace.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <algorithm>
#include "sort_vc8.h"

#include "tris_coalesce.h"
#include "tris_combinelayers.h"
#include "tris_lightmap.h"
#include "tris_mergeconcave.h"
#include "tris_tesselate.h"
#include "tris_tjunc.h"

#include "primarylights.h"
#include "d3dx9_34.h"
#include "qsort_vc8.h"

#ifndef BYTE4_HELPERS_DEFINED
#define BYTE4_HELPERS_DEFINED

inline void Byte4Copy( const byte from[4], byte to[4] )
{
    *( unsigned int * )to = *( const unsigned int * )from;
}

#endif

/* 0x004e4270 */
TrisContext_t   triGlobContext[TRIS_TYPE_COUNT];            /* 0x16ed9cd8 */
int             triGlobSelfTjuncCount;                      /* 0x16ed9d7c */
double          triGlobPhaseStartTime;                      /* 0x16ed9d80 */
int             triGlobTransientMode;                       /* 0x16ed9d88 */
int             triGlobLyrMtlCount;                         /* 0x16ed9d8c */
vec3_t          triGlobListBounds[MAX_TRI_SURF_LISTS][2];   /* 0x16ef8ed0 */
TriSurf_t      *triGlobSurfLists[MAX_TRI_SURF_LISTS];       /* 0x16f0aed0 */
TriSurfProps_t *triGlobPropsList;                           /* 0x16f0ded0 */
int             triGlobSurfCount;                           /* 0x16f0ded4 */
Tri_t           triGlobTris[MAX_MAP_TRIANGLES];             /* 0x16f0ded8 */
int             triGlobTriCount;                            /* 0x26e0ded8 */
int             triGlobVertCount;                           /* 0x26e0dedc */

int             g_degenerateTrisRemoved;                    /* 0x2b80def0 */
int             g_unmergedVertCount;                        /* 0x2b80dee8 */

TriVert_t       triGlobVerts[MAX_MAP_VERTEXES];             /* 0x1ab0ded8 */

static const char *s_trisTypeName[TRIS_TYPE_COUNT] = { "layered", "unlayered" };

byte g_matchSunShadowOnCoalesce = 1;        /* 0x0052969c */
byte g_combineLayeredMaterials  = 1;        /* 0x0052969d */

extern int reorderTris;                     /* 0x00529074 */

extern float sampleScale;                   /* 0x0052907c */

extern float defaultTessSize;               /* 0x123ce900 */

const vec4_t g_colorTintWhite = { 1.0f, 1.0f, 1.0f, 1.0f };  /* 0x0050e590 */

float smoothAngle = 0.01f;                  /* 0x005296a0 */

int   warnLayerUses = 1;                    /* 0x005296a4 */
float warnLayerArea;                        /* 0x2b80def4 */

static TrisContext_t *assembleTrisContext;  /* 0x2b80def8 */

static int *triGlobIndexMap;                /* 0x26e0dee0 */

int            *reorderGlobNewVertIndex;    /* 0x16ed9d68 */
int            *reorderGlobRemap;           /* 0x16ed9d6c */
int            *reorderGlobMapArray;        /* 0x16ed9d70 */
int            *reorderGlobIndexBuf;        /* 0x16ed9d74 */
unsigned short *reorderGlobOptIndices;      /* 0x16ed9d78 */

LayeredMaterialDesc_t triGlobLyrMtlDesc[MAX_LAYERED_MATERIALS];   /* 0x16ed9d90 */

extern byte s_cellCullGroupBits[MAX_MAP_CELLS * MAX_MAP_CULLGROUPS / 8];  /* 0x12ece938 */

#define TRIS_LAYERED_WORK_VERTS       0x80000
#define TRIS_VERT_LAYER_DATA_STRIDE   0x50

BspDrawVert_t s_layeredWorkVerts[TRIS_LAYERED_WORK_VERTS];        /* 0x26e0dee8 */
byte          s_layeredWorkLayerData[TRIS_LAYERED_WORK_VERTS *
                                     TRIS_VERT_LAYER_DATA_STRIDE]; /* 0x2900dee8 */
BspDrawVert_t s_simpleWorkVerts[MAX_MAP_DRAWVERTS];               /* 0x14899cd0 */

static void  EdgePlane( const vec3_t p0, const vec3_t p1, const vec3_t planeNormal, vec4_t out );
static void  PackColorTint( const vec4_t tint, byte out[4] );
static byte  Tris_LayerAlpha( const DrawSurf_t *ds, const byte color[4] );
static bool  TexVecsMatchOnWinding( const winding_t *w, const vec4_t v0, const vec4_t v1, float scale );
static bool  TexVecsMatchOnWindings( const winding_t *w0, const winding_t *w1,
                                     const vec4_t v0, const vec4_t v1, float scale );
static void  SmoothVertexNormalsForGroup( const int *sortedVerts, int groupSize );
void  Tris_PickBestTriangle( const DrawVert_t *verts, int vertCount, const vec3_t normal,
                             float dist, vec3_t outPositions[3], vec2_t outTexCoords[3],
                             vec2_t outLmapCoords[3] );
bool  DeriveTextureVectors( const DrawSurf_t *ds, const vec4_t plane, const vec3_t *points,
                            const vec2_t *texCoords, vec4_t *texVecsOut,
                            const vec2_t *lmapCoords, vec4_t *lmapVecsOut,
                            const byte *colors, vec4_t *colorVecsOut );
static void WrapLmapVecOffsets( vec4_t *lmapVecs );

#define TRIS_ON_EPSILON  0.1f

static int   Tris_LmapTexelLimitS( const TriSurfProps_t *props );
static int   Tris_LmapTexelLimitT( const TriSurfProps_t *props );
static int   CountLeadingZeros( unsigned int value );

void  Tris_FixSurfListHead( TriSurf_t *surf, TriSurf_t *newHead );
void  Tris_GridTreeInit( void );
void  Tris_GridTreeShutdown( void );
void  Tris_FindWindings( Entity_t *e, Tree_t *tree );
void  Tris_FindPatchWindings( DrawSurf_t *ds, Tree_t *tree );
void  Tris_FindIndexedMeshWindings( DrawSurf_t *ds, Tree_t *tree );
void  Tris_FindBrushWindings( DrawSurf_t *ds, Tree_t *tree );
bool  Tris_TerrainVertsMatch( const Material_t *material, const DrawVert_t *vert,
                              const vec4_t *texVecs, const vec4_t *lmapVecs,
                              const vec4_t *colorVecs );                 /* 0x00441320 */
bool  PointInsideTriangle( const vec3_t point, const winding_t *w, const vec4_t plane );
bool  Tris_TrianglesFormQuad( const DrawSurf_t *ds, winding_t **windings, const vec4_t *planes,
                              const vec4_t *texVecs, const vec4_t *lmapVecs,
                              const vec4_t *colorVecs );                 /* 0x00440c00 */
void  Tris_ComputeWindingBounds( void );
void  Tris_CoalesceWindings( void );
void  Tris_RemoveOccludedFragments( void );
void  Tris_ValidateSurfaceLightmap( TriSurf_t *surf, bool isTarget );
void  Tris_SurfaceMapError( int severity, TriSurf_t *surf, const char *message );
void  Tris_DrawSurfMapError( int severity, TriSurf_t *surf, const DrawSurf_t *ds, const char *message );
bool  Tris_CheckTextureRepeats( TriSurf_t *surf, const TriSurfProps_t *props );
bool  Tris_CheckLightmapVecs( TriSurf_t *surf, const vec4_t *lmapVecs );
bool  Tris_LmapExtentExceeds( const winding_t *w, const vec3_t lmapVec, float scale, float limit );
void  Tris_AssignPrimaryLights( void );
void  Tris_MarkAllSunShadowCasters( void );
void  Tris_FindSunShadowCasters( void );
void  Tris_SplitLargeWindings( void );
void  Tris_SplitLargeWinding( TriSurf_t *surf, TriSurf_t **listHead );
bool  Tris_FindLightmapSplitPlane( const winding_t *w, const TriSurfProps_t *props, vec4_t outPlane );
void  Tris_MergeIntoConcaveWindings( void );
void  Tris_FixTJunctions( void );
void  Tris_BuildLightmapGroups( void );
void  Tris_CheckLightmapsAssigned( void );
void  Tris_ReportFloatingSurfaces( void );
void  Tris_GenerateAllVertices( void );
int  *Tris_BuildVertexMergeMap( void );
void  Tris_SnapNearbyVertices( int *indexMap );
void  Tris_SnapWindingsToVertices( const int *indexMap );
void  Tris_TesselateAndPruneSurfaces( void );
void  Tris_FreeAllSurfaces( void );
void  Tris_TriangulateAndEmit( int trisType, Entity_t *e, Tree_t *tree );
void  Tris_TriangulateWindings( TrisContext_t *context );
void  Tris_EmitTriangleRecord( const winding_t *w, const winding_t *wOrig,
                               TriSurfProps_t *props, int i0, int i1, int i2, int visGroupIndex );
void  Tris_EmitSimpleTriangle( const winding_t *w, const winding_t *wOrig, int i0, int i1, int i2,
                               short visGroupIndex, const TriSurfProps_t *props,
                               const TriSurfProps_t *texProps, const vec4_t plane );
void  Tris_EmitChainTriangles( const winding_t *w, const winding_t *wOrig, int i0, int i1, int i2,
                               short visGroupIndex, TriSurfProps_t *props,
                               CoalesceNode_t *chain, const vec4_t plane );
CoalesceNode_t *Tris_EmitChainTriangle( const winding_t *w, const winding_t *wOrig,
                                        int i0, int i1, int i2, short visGroupIndex,
                                        TriSurfProps_t *props, CoalesceNode_t *chain,
                                        bool isFirst, const vec4_t plane );
void  Tris_EmitLayeredTriangle( const winding_t *w, const winding_t *wOrig, int i0, int i1, int i2,
                                short visGroupIndex, const TriSurfProps_t *props,
                                CoalesceNode_t *chain, LayeredMaterialDesc_t *lyrMtlDesc,
                                const vec4_t plane );
void  SmoothVertexNormals( int *indexMap );
bool  Tris_TriSortsBefore( const Tri_t &a, const Tri_t &b );
void  Tris_EmitTriangleSoups( TrisContext_t *context, Tree_t *tree, const int *indexMap );
void  Tris_GroupAndMergeTriSubgroups( Tri_t *tris, int triCount, const int *indexMap );
unsigned short Tris_MergeTriSubgroups( Tri_t *tris, int triCount, unsigned short groupCount );
void  Tris_MergeTriGroupPair( Tri_t *tris, int triCount, unsigned short firstGroup,
                              unsigned short secondGroup, const vec3_t mins, const vec3_t maxs,
                              const vec3_t extents, TriGroup_t *group, int groupCount );
void  Tris_CheckLayeredMaterialArea( Tri_t *tris, int triCount, int groupCount );
void  Tris_EmitTriangleGroup( TrisContext_t *context, Tri_t *tris, int triCount, Tree_t *tree );
int   Tris_EmitDrawVert( TrisContext_t *context, TrisSoup_t *soup, const TriVert_t *src );
int   Tris_WeldSoupVerts( TrisContext_t *context, int firstVert, int vertCount,
                          unsigned short *indices, int indexCount, Tri_t *firstTri,
                          int lmapIndex );
static bool Tris_PartitionSoupByGroup( TrisContext_t *context, TrisSoup_t *soup );
static bool Tris_PartitionSoupOnAxis( TrisContext_t *context, TrisSoup_t *soup,
                                      int axis, float dist );
static void Tris_SortAxesByExtent( const vec3_t extents, int order[3] );
static int  Tris_ReorderSortCompare( const void *va, const void *vb );
bool  Tris_ReorderAddSurfaceVerts( TrisContext_t *context, const BspTriSoup_t *soup,
                                   int *indexBuf, int *indexCount, unsigned int *vertCount );
void  Tris_ReorderTriSurfaceRun( TrisContext_t *context, int entityIndex,
                                 TrisReorderEntry_t *reorderList, int reorderCount );
void  Tris_CopyReorderedVert( TrisContext_t *context, int sourceIndex, int destIndex,
                              int layerDataOffset, const LayeredMaterialDesc_t *lyrMtlDesc );
LayeredMaterialDesc_t *Tris_FindLayeredMaterialByName( const char *name );
int   Tris_BuildMaterialRemapTable( int *remapTable, int materialIndex, int nextSlot );
int   Tris_FindNextEntity( const TrisContext_t *context, int entityIndex );
static int  Tris_CopyLayerTexCoord( TrisContext_t *context, const vec2_t texCoords, int offset );
static int  Tris_PackLayerTangentFrame( TrisContext_t *context, const vec3_t tangent,
                                        const vec3_t binormal, const vec3_t layerTangent,
                                        const vec3_t layerBinormal, int offset );
void  Tris_BuildAabbTree( TrisContext_t *context );

void  Tris_CanonicalizeIslandTexCoords( TrisContext_t *context, int firstVert, int vertCount,
                                        unsigned short *indices, int indexCount,
                                        const LayeredMaterialDesc_t *lyrMtlDesc, int lmapIndex );
void  Tris_BuildIslandAdjacency( const TrisContext_t *context, int firstVert, int vertCount,
                                 const unsigned short *indices, int indexCount,
                                 int *adjacency );                       /* 0x00446c90 */
void  Tris_FloodIslandFrom( TrisContext_t *context, int firstVert,
                            const unsigned short *indices, int indexCount,
                            const LayeredMaterialDesc_t *lyrMtlDesc, int lmapIndex,
                            const int *adjacency, unsigned int firstIndex,
                            byte *visited );                             /* 0x00446df0 */
bool  Tris_IslandShiftForNeighbour( const TrisContext_t *context, int firstVert,
                                    const unsigned short *indices,
                                    const LayeredMaterialDesc_t *lyrMtlDesc, int lmapIndex,
                                    unsigned int indexA, unsigned int indexB,
                                    int *shiftS, int *shiftT );          /* 0x00446f30 */
bool  Tris_VertsShareIslandSeam( const TrisContext_t *context, int vertA, int vertB,
                                 const LayeredMaterialDesc_t *lyrMtlDesc, int lmapIndex,
                                 int *shiftS, int *shiftT );             /* 0x004470a0 */
bool  Tris_TexCoordRepeatShift( const vec2_t uv0, const vec2_t uv1,
                                const Material_t *material,
                                int *shiftS, int *shiftT );              /* 0x00447300 */
void  Tris_ApplyIslandShift( TrisContext_t *context, int firstVert,
                             const unsigned short *indices,
                             const LayeredMaterialDesc_t *lyrMtlDesc, unsigned int firstIndex,
                             const int *shiftS, const int *shiftT );     /* 0x004473c0 */
void  Tris_ClampTexCoordsToLimit( TrisContext_t *context, int firstVert, int vertCount,
                                  const LayeredMaterialDesc_t *lyrMtlDesc );
void  Tris_ClampTexCoordStream( const Material_t *material, float *base, int stride,
                                int vertCount );                         /* 0x00447610 */
bool  Tris_TexCoordsExceedLimit( const vec2_t mins, const vec2_t maxs, const vec2_t shift,
                                 const vec2_t halfLimit );               /* 0x00447860 */
void  Tris_ShiftTexCoordsPerTriangle( float *base, int stride, unsigned int vertCount,
                                      const vec2_t soupShift,
                                      const vec2_t halfLimit );          /* 0x004478f0 */
float Tris_TexCoordShiftForRange( float mins, float maxs, float shiftGuess,
                                  float texCoordHalfLimit );             /* 0x00447a70 */
void  Tris_CombineAllLayeredMaterials( const int *indexMap );
void  Tris_FlushContextCounts( const TrisContext_t *context );
void  Tris_InitSurfList( int type, const char *name, int maxIndexCount, int maxVertCount,
                         BspDrawVert_t *workVerts, byte *workLayerData, byte *diskLayerData );
void  Tris_ReorderTriSurfaces( TrisContext_t *context );
int   Tris_ReorderAndOptimizeVerts( TrisContext_t *context, TrisReorderEntry_t *reorderList,
                                    int reorderCount );
int   Tris_MergeSurfacePairs( TriSurf_t **surfs, intWinding_t **intWindings, int surfCount,
                              winding_t **extraWinding, intWinding_t *extraIntWinding,
                              int mode );
int   Tris_TryMergeWindingPair( TriSurf_t **surfs, intWinding_t **intWindings, int surfCount,
                                int index, int mode );
bool  Tris_LmapCellsMatch( const TriSurf_t *surf0, const TriSurf_t *surf1 );  /* 0x0044c230 */
bool  Tris_LmapCellMatch( const TriSurf_t *a, const TriSurf_t *b,
                          float cellSize, int axis );                        /* 0x0044bc70 */
bool  Tris_MergedLmapFits( const TriSurf_t *surf0, const TriSurf_t *surf1,
                           const intWinding_t *w0, const intWinding_t *w1,
                           int start0, int start1, int dupCount );           /* 0x0044c320 */
bool  Tris_MergedLmapExceeds( const intWinding_t *w0, const intWinding_t *w1,
                              const TriSurfProps_t *props );                 /* 0x0044bd40 */
bool  Tris_MergedLmapExceedsProps( const intWinding_t *w0, const intWinding_t *w1,
                                   const TriSurfProps_t *props );            /* 0x0044bdb0 */
void  Tris_AddWindingLmapBounds( const intWinding_t *iw, const TriSurfProps_t *props,
                                 vec2_t mins, vec2_t maxs );
void  Tris_GetColorTint( const DrawSurf_t *ds, byte out[4] );
bool  Material_ConstantValue( const Material_t *material, const char *name, vec4_t out );
const byte *Material_ConstantTable( const Material_t *material );        /* 0x0044bfc0 */
void  Tris_ResetContext( TrisContext_t *context );                       /* 0x00449c40 */
int   Tris_FinishMergedSurfaces( TriSurf_t **surfArray, intWinding_t **windingArray, int count,
                                 winding_t **a, intWinding_t *b );       /* 0x0044c1b0 */

static const char *LayeredMaterialName( int index );
static LayeredMaterialDesc_t *LayeredMaterialDesc( int index );
static int LayeredMaterialLayerCount( const LayeredMaterialDesc_t *desc );
static Material_t *LayeredMaterialLayer( const LayeredMaterialDesc_t *desc, int layer );
void  Tris_EmitWindingAsDrawSurf( Tree_t *tree, winding_t *w, const vec4_t plane, DrawSurf_t *ds,
                                  const vec4_t *texVecs, const vec4_t *lmapVecs, const vec4_t *colorVecs );
TriSurfProps_t *Tris_AllocTriSurfProps( const vec4_t plane, DrawSurf_t *ds, const vec4_t *texVecs,
                                        const vec4_t *lmapVecs, const vec4_t *colorVecs );
void  Tris_EmitTriSurfForProps( Tree_t *tree, winding_t *w, TriSurfProps_t *props, const DrawSurf_t *ds );
void  Tris_EmitTriSurf_r( winding_t *w, TriSurfProps_t *props, Node_t *node );
LayeredMaterialDesc_t *Tris_EmitMaterial( Material_t * const *materials, int layerCount );
void  Tris_LayeredMaterialKey( Material_t * const *materials, int layerCount, char *out );
bool  Tris_LayeredMaterialAllNoTile( const LayeredMaterialDesc_t *lyrMtlDesc );
void  Tris_CopyVertexToDrawVert( BspDrawVert_t *dst, byte *layerData, const TriVert_t *src,
                                 const LayeredMaterialDesc_t *lyrMtlDesc );


/* Tris_LayeredMaterialIndex  0x0043c510 */
int Tris_LayeredMaterialIndex( const LayeredMaterialDesc_t *desc )
{
    unsigned int index;

    index = (unsigned int)( desc - triGlobLyrMtlDesc );

    AssertIn( index, MAX_LAYERED_MATERIALS );
    return index;
}


static const char *LayeredMaterialName( int index )
{
    return triGlobLyrMtlDesc[index].name;
}

static LayeredMaterialDesc_t *LayeredMaterialDesc( int index )
{
    return &triGlobLyrMtlDesc[index];
}

static int LayeredMaterialLayerCount( const LayeredMaterialDesc_t *desc )
{
    return desc->layerCount;
}

static Material_t *LayeredMaterialLayer( const LayeredMaterialDesc_t *desc, int layer )
{
    return desc->mtlRaw[layer];
}


/* Tris_ValidateSurfLmapCoords  0x0043c560 */
void Tris_ValidateSurfLmapCoords( const winding_t *w, const TriSurfProps_t *props )
{
    unsigned int i;
    float        lmapCoord[2];

    if ( !w || props->lmapIndex == LIGHTMAP_NONE )
        return;

    AssertIn( props->lmapIndex, LIGHTMAP_NONE );

    for ( i = 0; i < w->ptCount; i++ )
    {
        lmapCoord[0] = Vec3Dot( props->lmapVecs[0], w->pts[i] ) + props->lmapVecs[0][3];
        lmapCoord[1] = Vec3Dot( props->lmapVecs[1], w->pts[i] ) + props->lmapVecs[1][3];

        Assertx( lmapCoord[0] >= -0.01f / ( ( 1024 < 512 ) ? 1024 : 512 ) &&
                 lmapCoord[0] <= 1 + 0.01f / ( ( 1024 < 512 ) ? 1024 : 512 ),
                 "(lmapCoord[0]) = %g", lmapCoord[0] );
        Assertx( lmapCoord[1] >= -0.01f / ( ( 1024 < 512 ) ? 1024 : 512 ) &&
                 lmapCoord[1] <= 1 + 0.01f / ( ( 1024 < 512 ) ? 1024 : 512 ),
                 "(lmapCoord[1]) = %g", lmapCoord[1] );
    }
}


/* GetTrisTransientMode  0x0043c6c0 */
int GetTrisTransientMode( void )
{
    return triGlobTransientMode;
}


/* SetTrisTransientMode  0x0043c6d0 */
void SetTrisTransientMode( int mode, int keepExisting )
{
    int        listIndex;
    TriSurf_t *surf;

    Assert( mode == TRIS_TRANSIENT_NONE ||
            triGlobTransientMode == TRIS_TRANSIENT_NONE );

    triGlobTransientMode = mode;

    if ( keepExisting )
        return;

    for ( listIndex = 0; listIndex < Tris_GetSurfListCount(); listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
            surf->transient.wOrig = NULL;
    }
}


/* Tris_GetSurfListCount  0x0043c760 */
int Tris_GetSurfListCount( void )
{
    if ( entity_num >= 1 )
        return 1;
    return numCells + numCullGroups;
}


/* Tris_ClipWindingEpsilon  0x0043c790 */
void Tris_ClipWindingEpsilon( const winding_t *in, const winding_t *inOrig,
                              const vec3_t planeNormal, float planeDist,
                              float epsilon, winding_t **front, winding_t **back )
{
    float        minDist;
    float        maxDist;
    float        maxSurfDist;
    float        onEpsilon;
    double       dists[MAX_POINTS_ON_WINDING + 4];
    int          sides[MAX_POINTS_ON_WINDING + 4];
    int          counts[3];
    unsigned int i;
    unsigned int axis;
    unsigned int maxPts;
    qboolean     exactAxialPlane;
    unsigned int axialPlaneAxis;
    float        axialPlaneDist;
    winding_t   *f;
    winding_t   *b;
    const float *p0;
    const float *p1;
    vec3_t       mid;

    Assert( in );
    Assert( planeNormal );
    Assert( Vec3LengthSq( planeNormal ) > 0 );
    Assert( front );
    Assert( back );

    *front = NULL;
    *back  = NULL;

    onEpsilon = epsilon * 0.01f;
    WindingPlaneDistExtent( in, planeNormal, planeDist, &minDist, &maxDist );

    if ( maxDist <= onEpsilon )
    {
        *back = CopyWinding( in );
        return;
    }
    if ( minDist >= -onEpsilon )
    {
        *front = CopyWinding( in );
        return;
    }

    if ( maxDist < epsilon && minDist < maxDist * -3.0f )
    {
        maxSurfDist = WindingMaxPlaneDist( inOrig, planeNormal, planeDist );
        if ( maxDist <= maxSurfDist * 0.25f )
        {
            *back = CopyWinding( in );
            return;
        }
        epsilon = I_fmin( maxSurfDist * 0.25f, epsilon );
    }

    if ( minDist > -epsilon && minDist * -3.0f < maxDist )
    {
        maxSurfDist = WindingMaxPlaneDist( inOrig, planeNormal, planeDist );
        if ( minDist >= maxSurfDist * -0.25f )
        {
            *front = CopyWinding( in );
            return;
        }
        epsilon = I_fmin( epsilon, maxSurfDist * 0.25f );
    }

    onEpsilon = I_fmin( epsilon, ( maxDist - minDist ) * 0.125f );

    counts[0] = 0;
    counts[1] = 0;
    counts[2] = 0;

    for ( i = 0; i < in->ptCount; i++ )
    {
        double dist = Vec3Dot( in->pts[i], planeNormal ) - planeDist;

        dists[i] = dist;
        if ( (float)dist > onEpsilon )
            sides[i] = SIDE_FRONT;
        else if ( (float)dist < -onEpsilon )
            sides[i] = SIDE_BACK;
        else
            sides[i] = SIDE_ON;

        counts[sides[i]]++;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    Assert( counts[SIDE_FRONT] );
    Assert( counts[SIDE_BACK] );

    exactAxialPlane = qtrue;
    axialPlaneAxis  = (unsigned int)-1;
    axialPlaneDist  = 0.0f;
    for ( axis = 0; axis < 3; axis++ )
    {
        if ( planeNormal[axis] == 1.0f )
        {
            axialPlaneAxis = axis;
            axialPlaneDist = planeDist;
        }
        else if ( planeNormal[axis] == -1.0f )
        {
            axialPlaneAxis = axis;
            axialPlaneDist = -planeDist;
        }
        else if ( planeNormal[axis] != 0.0f )
        {
            exactAxialPlane = qfalse;
            break;
        }
    }
    Assertx( !exactAxialPlane || ( (int)axialPlaneAxis >= 0 && (int)axialPlaneAxis < 3 ),
             "(axialPlaneAxis) = %i", axialPlaneAxis );

    maxPts = in->ptCount + 4;
    f = AllocWinding( maxPts );
    b = AllocWinding( maxPts );

    for ( i = 0; i < in->ptCount; i++ )
    {
        p0 = in->pts[i];

        if ( sides[i] == SIDE_ON )
        {
            Vec3Copy( p0, f->pts[f->ptCount] );
            Vec3Copy( p0, b->pts[b->ptCount] );
            f->ptCount++;
            b->ptCount++;
            continue;
        }

        if ( sides[i] == SIDE_FRONT )
        {
            Vec3Copy( p0, f->pts[f->ptCount] );
            f->ptCount++;
        }
        if ( sides[i] == SIDE_BACK )
        {
            Vec3Copy( p0, b->pts[b->ptCount] );
            b->ptCount++;
        }

        if ( sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i] )
            continue;

        p1 = in->pts[( i + 1 ) % in->ptCount];
        for ( axis = 0; axis < 3; axis++ )
        {
            mid[axis] = (float)( ( p1[axis] * dists[i] - p0[axis] * dists[i + 1] ) /
                                 ( dists[i] - dists[i + 1] ) );
        }
        if ( exactAxialPlane )
            mid[axialPlaneAxis] = axialPlaneDist;

        Vec3Copy( mid, f->pts[f->ptCount] );
        Vec3Copy( mid, b->pts[b->ptCount] );
        f->ptCount++;
        b->ptCount++;
    }

    AssertCmp( f->ptCount, <=, maxPts );
    AssertCmp( b->ptCount, <=, maxPts );
    Assertx( f->ptCount <= MAX_POINTS_ON_WINDING, "(f->ptCount) = %i", f->ptCount );
    Assertx( b->ptCount <= MAX_POINTS_ON_WINDING, "(b->ptCount) = %i", b->ptCount );

    *front = f;
    *back  = b;
}


/* Tris_GetSurfCount  0x0043d070 */
int Tris_GetSurfCount( void )
{
    return triGlobSurfCount;
}


/* Tris_ForEachSurf  0x0043d080 */
void Tris_ForEachSurf( void (*callback)( TriSurf_t *, bool ), TriSurf_t *targetSurf )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;
    TriSurf_t *next;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = next )
        {
            next = surf->visGroupNext;
            callback( surf, surf == targetSurf );
        }
    }
}


/* AllocTriSurf  0x0043d0f0 */
TriSurf_t *AllocTriSurf( winding_t *w, TriSurfProps_t *props )
{
    TriSurf_t *surf;

    surf = (TriSurf_t *)::operator new( sizeof( TriSurf_t ) );
    memset( surf, 0, sizeof( TriSurf_t ) );
    surf->w     = w;
    surf->props = props;
    triGlobSurfCount++;
    return surf;
}


/* CopyTriSurf  0x0043d140 */
TriSurf_t *CopyTriSurf( const TriSurf_t *surf )
{
    TriSurf_t *copy;

    copy = AllocTriSurf( CopyWinding( surf->w ), surf->props );
    Vec3Copy( surf->mins, copy->mins );
    Vec3Copy( surf->maxs, copy->maxs );

    SanityCheck( surf->holeCount == 0 );
    return copy;
}


/* Tris_UnlinkSurf  0x0043d1d0 */
void Tris_UnlinkSurf( TriSurf_t *surf, TriSurf_t **listHead )
{
    Assert( surf );

    if ( surf->visGroupPrev )
        surf->visGroupPrev->visGroupNext = surf->visGroupNext;
    else if ( listHead )
        *listHead = surf->visGroupNext;

    if ( surf->visGroupNext )
        surf->visGroupNext->visGroupPrev = surf->visGroupPrev;

    surf->visGroupPrev = NULL;
    surf->visGroupNext = NULL;

    GridTree_Remove( surf );
}


/* Tris_FreeSurface  0x0043d260 */
void Tris_FreeSurface( TriSurf_t *surf )
{
    int i;

    if ( surf->visGroupNext )
        surf->visGroupNext->visGroupPrev = surf->visGroupPrev;

    if ( surf->visGroupPrev )
        surf->visGroupPrev->visGroupNext = surf->visGroupNext;
    else
        Tris_FixSurfListHead( surf, surf->visGroupNext );

    if ( surf->w )
        FreeWinding( surf->w );

    if ( triGlobTransientMode == TRIS_TRANSIENT_WORIG && surf->transient.wOrig )
        FreeWinding( surf->transient.wOrig );

    if ( surf->holeCount )
    {
        for ( i = 0; i < surf->holeCount; i++ )
            FreeWinding( surf->holes[i] );
        delete [] surf->holes;
    }

    ::operator delete( surf );
    triGlobSurfCount--;
}


/* Tris_FixSurfListHead  0x0043d360 */
void Tris_FixSurfListHead( TriSurf_t *surf, TriSurf_t *newHead )
{
    int listCount;
    int listIndex;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        if ( triGlobSurfLists[listIndex] == surf )
        {
            triGlobSurfLists[listIndex] = newHead;
            return;
        }
    }
}


/* InsertTriSurfAfter  0x0043d3b0 */
TriSurf_t *InsertTriSurfAfter( FixedWinding_t *fw, TriSurfProps_t *props, TriSurf_t *prev )
{
    TriSurf_t *surf;

    Assert( prev );

    surf = AllocTriSurf( WindingFromFixedWinding( fw ), props );
    surf->fw   = fw;
    surf->visGroupPrev = prev;
    surf->visGroupNext = prev->visGroupNext;
    prev->visGroupNext = surf;
    if ( surf->visGroupNext )
        surf->visGroupNext->visGroupPrev = surf;

    return surf;
}


/* Tris_FreeAllProps  0x0043d450 */
void Tris_FreeAllProps( void )
{
    TriSurfProps_t *next;
    CoalesceNode_t *chainNext;

    CoalesceClearPropsHash();

    while ( triGlobPropsList )
    {
        next = triGlobPropsList->next;

        while ( triGlobPropsList->coalesceChain )
        {
            chainNext = triGlobPropsList->coalesceChain->next;
            ::operator delete( triGlobPropsList->coalesceChain );
            triGlobPropsList->coalesceChain = chainNext;
        }

        ::operator delete( triGlobPropsList );
        triGlobPropsList = next;
    }
}


/* PlanesMatch  0x0043d4f0 */
static bool PlanesMatch( const vec4_t p0, const vec4_t p1 )
{
    if ( Vec3Dot( p0, p1 ) < 0.999f )
        return false;
    return fabs( p0[3] - p1[3] ) <= 0.001f;
}


#define MATERIAL_TEX_WIDTH( m )   ( *(const unsigned short *)( (const byte *)( m ) + 0x18 ) )
#define MATERIAL_TEX_HEIGHT( m )  ( *(const unsigned short *)( (const byte *)( m ) + 0x1a ) )

#define MTL_TECHSET_LIGHTMAP      0x02

#define MTL_TOOLFLAG_TEXTURE_WRAPS 0x0080


/* TexVecDelta  0x0043da80 */
static float TexVecDelta( const vec3_t point, const vec4_t v0, const vec4_t v1 )
{
    float d0;
    float d1;

    d0 = Vec3Dot( point, v0 ) + v0[3];
    d1 = Vec3Dot( point, v1 ) + v1[3];

    return d0 - d1;
}


/* TexVecWholeDelta  0x0043da40 */
static float TexVecWholeDelta( const vec3_t point, const vec4_t v0, const vec4_t v1 )
{
    return floor( ( float )( TexVecDelta( point, v0, v1 ) + 0.5f ) );
}


/* TexVecsMatchAtPoint  0x0043dad0 */
static bool TexVecsMatchAtPoint( const vec3_t point, const vec4_t v0, const vec4_t v1,
                                 int texSize, float tolerance, float offset )
{
    float delta;
    float texelDist;

    delta     = TexVecDelta( point, v0, v1 ) - offset;
    texelDist = fabs( delta ) * texSize;

    return texelDist <= tolerance;
}


/* VecsMatchAtPoint  0x0043d9e0 */
static bool VecsMatchAtPoint( const vec3_t point, const vec4_t v0, const vec4_t v1,
                              float toleranceSq )
{
    float d0;
    float delta;

    d0    = Vec3Dot( point, v0 ) + v0[3];
    delta = d0 - ( Vec3Dot( point, v1 ) + v1[3] );
    return delta * delta <= toleranceSq;
}


/* TriSurfPropsGroupable  0x0043d550 */
bool TriSurfPropsGroupable( const TriSurfProps_t *p0, const TriSurfProps_t *p1,
                            const winding_t *testPoints )
{
    unsigned int i;
    const Material_t *material;
    float        dist;
    float        texOffset[2];

    if ( p0 == p1 )
        return true;

    if ( p0->coalesceChain || p1->coalesceChain )
        return CoalesceChainGroupable( p0->coalesceChain, p1->coalesceChain, testPoints );

    if ( p0->ds->mtlTex      != p1->ds->mtlTex )      return false;
    if ( p0->ds->reflectionProbeIndex != p1->ds->reflectionProbeIndex ) return false;
    if ( p0->ds->primaryLightIndex            != p1->ds->primaryLightIndex )            return false;
    if ( p0->lmapIndex         != p1->lmapIndex )         return false;
    if ( p0->nodraw            != p1->nodraw )            return false;
    if ( p0->mergeTouching         != p1->mergeTouching )         return false;

    if ( g_matchSunShadowOnCoalesce &&
         p0->ds->castsSunShadow != p1->ds->castsSunShadow )
        return false;

    if ( Vec3Dot( p0->plane, p1->plane ) < 0.9f )
        return false;

    material = p1->ds->mtlTex;

    if ( !( material->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS ) )
    {
        Vec2Clear( texOffset );
    }
    else
    {
        texOffset[0] = TexVecWholeDelta( testPoints->pts[0], p0->texVecs[0], p1->texVecs[0] );
        texOffset[1] = TexVecWholeDelta( testPoints->pts[0], p0->texVecs[1], p1->texVecs[1] );
    }

    for ( i = 0; i < testPoints->ptCount; i++ )
    {
        dist = Vec3Dot( testPoints->pts[i], p1->plane ) - p1->plane[3];
        if ( dist * dist > 0.0001f )
            return false;

        dist = Vec3Dot( testPoints->pts[i], p0->plane ) - p0->plane[3];
        if ( dist * dist > 0.0001f )
            return false;

        if ( !TexVecsMatchAtPoint( testPoints->pts[i], p0->texVecs[0], p1->texVecs[0],
                                   MATERIAL_TEX_WIDTH( material ), 0.5f, texOffset[0] ) )
            return false;
        if ( !TexVecsMatchAtPoint( testPoints->pts[i], p0->texVecs[1], p1->texVecs[1],
                                   MATERIAL_TEX_HEIGHT( material ), 0.5f, texOffset[1] ) )
            return false;

        if ( material->techSetFlags & MTL_TECHSET_LIGHTMAP )
        {
            if ( !VecsMatchAtPoint( testPoints->pts[i], p0->lmapVecs[0], p1->lmapVecs[0], 9.5367432e-07f ) )
                return false;
            if ( !VecsMatchAtPoint( testPoints->pts[i], p0->lmapVecs[1], p1->lmapVecs[1], 9.5367432e-07f ) )
                return false;
        }

        if ( !VecsMatchAtPoint( testPoints->pts[i], p0->colorVecs[0], p1->colorVecs[0], 0.25f ) )
            return false;
        if ( !VecsMatchAtPoint( testPoints->pts[i], p0->colorVecs[1], p1->colorVecs[1], 0.25f ) )
            return false;
        if ( !VecsMatchAtPoint( testPoints->pts[i], p0->colorVecs[2], p1->colorVecs[2], 0.25f ) )
            return false;
        if ( !VecsMatchAtPoint( testPoints->pts[i], p0->colorVecs[3], p1->colorVecs[3], 0.25f ) )
            return false;
    }

    return true;
}


/* CoalesceChainLength  0x0043dc10 */
static int CoalesceChainLength( const CoalesceNode_t *chain )
{
    int count;

    count = 0;
    for ( ; chain; chain = chain->next )
        count++;
    return count;
}


/* CoalesceChainGroupable  0x0043db20 */
bool CoalesceChainGroupable( const CoalesceNode_t *chain0, const CoalesceNode_t *chain1,
                             const winding_t *testPoints )
{
    char                  used[64];
    unsigned int          count0;
    unsigned int          count1;
    const CoalesceNode_t *a;
    const CoalesceNode_t *b;
    int                   index;

    count0 = CoalesceChainLength( chain0 );
    count1 = CoalesceChainLength( chain1 );

    if ( count0 != count1 )
        return false;
    if ( count0 > sizeof( used ) )
        return false;

    memset( used, 0, sizeof( used ) );

    for ( a = chain0; a; a = a->next )
    {
        index = 0;
        for ( b = chain1; b; b = b->next, index++ )
        {
            if ( used[index] )
                continue;
            if ( TriSurfPropsGroupable( a->props, b->props, testPoints ) )
                break;
        }
        if ( !b )
            return false;
        used[index] = 1;
    }

    return true;
}


/* TriSurfPropsGroupableFixed  0x0043dc40 */
bool TriSurfPropsGroupableFixed( const TriSurfProps_t *p0, const TriSurfProps_t *p1,
                                 const FixedWinding_t *testPoints )
{
    winding_t *w;
    bool       result;

    w = WindingFromFixedWinding( testPoints );
    result = TriSurfPropsGroupable( p0, p1, w );
    FreeWinding( w );
    return result;
}


/* CopyTriSurfProps  0x0043dc80 */
TriSurfProps_t *CopyTriSurfProps( const TriSurfProps_t *props )
{
    TriSurfProps_t *copy;

    Assert( props );

    copy = (TriSurfProps_t *)::operator new( sizeof( TriSurfProps_t ) );
    if ( !copy )
    {
        Com_Error( "Out of memory: couldn't allocate %i bytes for surface properties\n",
                   sizeof( TriSurfProps_t ) );
    }

    *copy = *props;

    copy->coalesceChain = CopyCoalesceChain( props->coalesceChain );
    copy->next      = triGlobPropsList;
    triGlobPropsList    = copy;

    return copy;
}


/* PointInsideTriangle  0x0043dd30 */
bool PointInsideTriangle( const vec3_t point, const winding_t *w, const vec4_t plane )
{
    vec4_t edgePlane;
    float eps;
    float dist;

    Assert( w );
    Assert( w->ptCount == 3 );

    eps = 0.025f;
    dist = Vec3Dot( plane, point ) - plane[3];
    if ( I_fabs( dist ) > eps )
        return false;

    EdgePlane( w->pts[0], w->pts[1], plane, edgePlane );
    if ( Vec3Dot( edgePlane, point ) - edgePlane[3] < -eps )
        return false;

    EdgePlane( w->pts[1], w->pts[2], plane, edgePlane );
    if ( Vec3Dot( edgePlane, point ) - edgePlane[3] > eps )
        return false;

    EdgePlane( w->pts[2], w->pts[0], plane, edgePlane );
    if ( Vec3Dot( edgePlane, point ) - edgePlane[3] > eps )
        return false;

    return true;
}


/* EdgePlane  0x0043dea0 */
static void EdgePlane( const vec3_t p0, const vec3_t p1, const vec3_t planeNormal, vec4_t out )
{
    vec3_t dir;

    Vec3Sub( p1, p0, dir );
    Vec3Cross( planeNormal, dir, out );
    Vec3Normalize( out );
    out[3] = Vec3Dot( out, p0 );
}


/* SortTriVertIndices  0x0043e310 */
static void SortTriVertIndices( const int from[3], int to[3] )
{
    Assert( from );
    Assert( to );
    Assert( from != to );

    if ( from[0] < from[1] )
    {
        if ( from[0] < from[2] )
        {
            to[0] = from[0];
            if ( from[1] < from[2] )
            {
                to[1] = from[1];
                to[2] = from[2];
            }
            else
            {
                to[1] = from[2];
                to[2] = from[1];
            }
        }
        else
        {
            to[0] = from[2];
            to[1] = from[0];
            to[2] = from[1];
        }
    }
    else if ( from[1] < from[2] )
    {
        to[0] = from[1];
        if ( from[0] < from[2] )
        {
            to[1] = from[0];
            to[2] = from[2];
        }
        else
        {
            to[1] = from[2];
            to[2] = from[0];
        }
    }
    else
    {
        to[0] = from[2];
        to[1] = from[1];
        to[2] = from[0];
    }
}


/* SortedTrisShareEdge  0x0043e490 */
static bool SortedTrisShareEdge( const int a[3], const int b[3] )
{
    if ( a[2] == b[2] )
    {
        if ( a[0] == b[0] ) return true;
        if ( a[1] == b[0] ) return true;
        if ( a[0] == b[1] ) return true;
        if ( a[1] == b[1] ) return true;
        return false;
    }
    if ( a[2] == b[1] )
    {
        if ( a[0] == b[0] ) return true;
        if ( a[1] == b[0] ) return true;
        return false;
    }
    if ( a[1] == b[2] )
    {
        if ( a[0] == b[0] ) return true;
        if ( a[0] == b[1] ) return true;
        return false;
    }
    if ( a[1] == b[1] )
        return a[0] == b[0];

    return false;
}


/* CompareSortedTriIndices  0x0043e590 */
static int CompareSortedTriIndices( const void *va, const void *vb )
{
    const int *a = (const int *)va;
    const int *b = (const int *)vb;
    int        d;

    d = a[0] - b[0];
    if ( d == 0 )
    {
        d = a[1] - b[1];
        if ( d == 0 )
            d = a[2] - b[2];
    }
    return d;
}


/* CompareTrisByGroupId  0x0043e2e0 */
static int CompareTrisByGroupId( const void *va, const void *vb )
{
    return (unsigned short)( (const Tri_t *)va )->triGroupId -
           (unsigned short)( (const Tri_t *)vb )->triGroupId;
}


/* GroupTriSurfsIntoSubgroups  0x0043df00 */
unsigned short GroupTriSurfsIntoSubgroups( Tri_t **triLists, const int *triCounts,
                                int listCount, const int *indexMap )
{
    typedef struct { int v[3]; Tri_t *tri; } TriKey_t;

    TriKey_t      *keys;
    int            listIndex;
    int            triIndex;
    int            i;
    int            j;
    int            k;
    int            first;
    unsigned int   count;
    short          nextGroupId;
    unsigned short groupCount;
    short          groupId;
    short          otherGroupId;
    int            mapped[3];

    count = 0;
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
        count += triCounts[listIndex];

    Assert( (int)count > 0 );

    keys = new TriKey_t[count];

    count = 0;
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( triIndex = 0; triIndex < triCounts[listIndex]; triIndex++ )
        {
            Tri_t *tri = &triLists[listIndex][triIndex];

            tri->triGroupId = 0;

            mapped[0] = indexMap[tri->vertIndices[0]];
            mapped[1] = indexMap[tri->vertIndices[1]];
            mapped[2] = indexMap[tri->vertIndices[2]];

            SortTriVertIndices( mapped, keys[count].v );
            keys[count].tri = tri;
            count++;
        }
    }

    qsort_vc8( keys, count, sizeof( keys[0] ), CompareSortedTriIndices );

    nextGroupId = 1;
    groupCount  = 0;
    first       = 0;

    for ( i = 0; i < (int)count; i++ )
    {
        while ( keys[first].v[2] < keys[i].v[1] )
            first++;

        groupId = keys[i].tri->triGroupId;

        for ( j = first; j < i; j++ )
        {
            if ( keys[j].tri->triGroupId == groupId )
                continue;
            if ( !SortedTrisShareEdge( keys[i].v, keys[j].v ) )
                continue;

            otherGroupId = keys[j].tri->triGroupId;

            if ( groupId != 0 )
            {
                for ( k = 0; k < i; k++ )
                {
                    if ( keys[k].tri->triGroupId == groupId )
                        keys[k].tri->triGroupId = otherGroupId;
                }
                groupCount--;
            }

            keys[i].tri->triGroupId = otherGroupId;
            groupId = otherGroupId;
        }

        if ( groupId == 0 )
        {
            if ( groupCount == MAX_TRI_SUBGROUPS )
                Com_Error( "MAX_TRI_SUBGROUPS" );

            keys[i].tri->triGroupId = nextGroupId;
            nextGroupId++;
            groupCount++;
        }
    }

    delete [] keys;

    Assert( groupCount >= 1 );

    if ( groupCount > 1 )
    {
        for ( listIndex = 0; listIndex < listCount; listIndex++ )
            qsort_vc8( triLists[listIndex], triCounts[listIndex], sizeof( Tri_t ), CompareTrisByGroupId );
    }

    return groupCount;
}


/* Tris_LayeredMaterialName  0x0043e6f0 */
static void Tris_LayeredMaterialName( const LayeredMaterialDesc_t *desc, char *out )
{
    int layerCount;
    int len;
    int i;

    layerCount = desc->layerCount;

    len = 0;
    for ( i = 0; i < layerCount - 2; i++ )
    {
        len += _snprintf( out + len, 0x400, "%s, ",
                          StringFromOffset( desc->mtlRaw[i] ) );
    }

    _snprintf( out + len, 0x400, "%s and %s",
               StringFromOffset( desc->mtlRaw[layerCount - 2] ),
               StringFromOffset( desc->mtlRaw[layerCount - 1] ) );
}


/* Tris_ReportLayeredMaterialUsage  0x0043e5e0 */
void Tris_ReportLayeredMaterialUsage( void )
{
    char                        name[0x400];
    int                         descIndex;
    int                         refIndex;
    const LayeredMaterialDesc_t *desc;
    const LayeredMaterialRef_t  *ref;

    for ( descIndex = 0; descIndex < triGlobLyrMtlCount; descIndex++ )
    {
        desc = &triGlobLyrMtlDesc[descIndex];

        if ( !desc->refPoints )
            continue;

        Tris_LayeredMaterialName( desc, name );

        refIndex = 0;
        for ( ref = desc->refPoints; ref; ref = ref->next )
        {
            refIndex++;
            MapError( MAPERROR_NONFATAL, ref->pos, ref->normal, 0, -1, -1,
                      "use %i of %i for layered material combination %s",
                      refIndex, desc->useCount, name );
        }
    }
}


/* TrisTimerCheck  0x0043e9d0 */
void TrisTimerCheck( void )
{
    double elapsed;

    if ( triGlobPhaseStartTime == 0.0 )
        return;

    elapsed = I_FloatTime() - triGlobPhaseStartTime;
    if ( elapsed > 5.0 )
        Com_Printf( "finished in %.0lf seconds\n", elapsed );

    triGlobPhaseStartTime = 0.0;
}


/* Tris_StartPhase  0x0043ea30 */
void Tris_StartPhase( const char *description )
{
    if ( entity_num )
        return;

    TrisTimerCheck();
    Com_Printf( "%s...\n", description );
    triGlobPhaseStartTime = I_FloatTime();
}


/* TriangulateEntity  0x0043ea70 */
void TriangulateEntity( Entity_t *e, Tree_t *tree )
{
    int *indexMap;

    Tris_GridTreeInit();

    Tris_StartPhase( "\nfinding triangle windings" );
    Tris_FindWindings( e, tree );
    Tris_ComputeWindingBounds();

    Tris_StartPhase( "coalescing coincident windings" );
    Tris_CoalesceWindings();

    Tris_StartPhase( "removing occluded winding fragments" );
    Tris_RemoveOccludedFragments();
    Tris_ForEachSurf( Tris_ValidateSurfaceLightmap, NULL );

    Tris_StartPhase( "assigning primary lights" );
    Tris_ComputeWindingBounds();
    Tris_AssignPrimaryLights();

    if ( entity_num == 0 )
    {
        Tris_StartPhase( "finding sun shadow casters" );
        Tris_FindSunShadowCasters();
    }
    else
    {
        Tris_MarkAllSunShadowCasters();
    }

    Tris_StartPhase( "splitting large windings" );
    Tris_SplitLargeWindings();

    Tris_StartPhase( "merging into concave windings" );
    Tris_MergeIntoConcaveWindings();

    Tris_StartPhase( "fixing t-junctions" );
    Tris_FixTJunctions();

    Tris_StartPhase( "tethering holes to their concave windings" );
    Tris_ForEachSurf( (void (*)( TriSurf_t *, bool ))TetherHolesToWinding,
                      (TriSurf_t *)0x2c152b08 );

    Tris_StartPhase( "building lightmap groups" );
    SetTrisTransientMode( TRIS_TRANSIENT_LMAP, 0 );
    Tris_BuildLightmapGroups();

    Tris_StartPhase( "assigning lightmaps" );
    TrisLmapAssign();
    Tris_CheckLightmapsAssigned();
    SetTrisTransientMode( TRIS_TRANSIENT_NONE, 1 );

    Tris_StartPhase( "finding index mapping and snapping vertices" );
    SetTrisTransientMode( TRIS_TRANSIENT_WORIG, 1 );
    Tris_GenerateAllVertices();
    indexMap = Tris_BuildVertexMergeMap();
    Tris_SnapNearbyVertices( indexMap );
    ApplyMergeMap( triGlobVerts[0].xyz, sizeof( TriVert_t ), triGlobVertCount, 0, indexMap );
    Tris_SnapWindingsToVertices( indexMap );
    FreeMergeMap( indexMap );

    TrisTimerCheck();
    Tris_ReportFloatingSurfaces();
    Tris_GridTreeShutdown();
}


/* Tris_GridTreeInit / Tris_GridTreeShutdown  0x00442f70 / 0x00442f80 */
void Tris_GridTreeInit( void )
{
    GridTree_Init();
}

void Tris_GridTreeShutdown( void )
{
    GridTree_Shutdown();
}


/* Tris_EmitEntityTris  0x0043e7c0 */
void Tris_EmitEntityTris( Entity_t *e, Tree_t *tree )
{
    e->firstTriSoup[TRIS_TYPE_SIMPLE]  = triGlobContext[TRIS_TYPE_SIMPLE].triCount;
    e->firstTriSoup[TRIS_TYPE_LAYERED] = triGlobContext[TRIS_TYPE_LAYERED].triCount;

    if ( e->firstDrawSurf == numMapDrawSurfs )
        return;

    TJunc_SetBounds( tree->mins, tree->maxs );
    TriangulateEntity( e, tree );
    Tris_TesselateAndPruneSurfaces();

    Tris_TriangulateAndEmit( TRIS_TYPE_SIMPLE,  e, tree );
    Tris_TriangulateAndEmit( TRIS_TYPE_LAYERED, e, tree );

    Tris_FreeAllSurfaces();
    SetTrisTransientMode( TRIS_TRANSIENT_NONE, 1 );

    Tris_ResetContext( &triGlobContext[TRIS_TYPE_LAYERED] );
    Tris_ResetContext( &triGlobContext[TRIS_TYPE_SIMPLE] );

    if ( entity_num == 0 )
    {
        Com_Printf( "%i vertices couldn't be merged because the textures point different ways\n",
                    g_unmergedVertCount );
        Tris_StartPhase( "emitting cells and portals" );
        GetLeafBrushes( e, tree->headnode );
    }

    TrisTimerCheck();
    Tris_FreeAllProps();
}


/* Tris_TesselateAndPruneSurfaces  0x0043e8b0 */
void Tris_TesselateAndPruneSurfaces( void )
{
    int         listCount;
    int         listIndex;
    TriSurf_t **link;
    TriSurf_t  *surf;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        link = &triGlobSurfLists[listIndex];
        for ( surf = *link; surf; surf = *link )
        {
            TesselateSurfaceWinding( surf );

            if ( surf->w->ptCount < 3 )
            {
                *link = surf->visGroupNext;
                if ( surf->visGroupNext )
                    surf->visGroupNext->visGroupPrev = surf->visGroupPrev;
                Tris_FreeSurface( surf );
            }
            else
            {
                link = &surf->visGroupNext;
            }
        }
    }
}


/* Tris_FreeAllSurfaces  0x0043e960 */
void Tris_FreeAllSurfaces( void )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;
    TriSurf_t *next;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = next )
        {
            next = surf->visGroupNext;
            Tris_FreeSurface( surf );
        }
        triGlobSurfLists[listIndex] = NULL;
    }
}


/* Tris_ValidateSurfaceLightmap  0x0043ec20 */
void Tris_ValidateSurfaceLightmap( TriSurf_t *surf, bool isTarget )
{
    const CoalesceNode_t *chain;
    vec2_t                lmapBounds[2];
    int                   width;
    int                   height;
    float                 neededScale;

    (void)isTarget;

    if ( !surf->props->coalesceChain )
    {
        if ( !Tris_CheckTextureRepeats( surf, surf->props ) )
        {
            Tris_FreeSurface( surf );
            return;
        }
    }
    else
    {
        for ( chain = surf->props->coalesceChain; chain; chain = chain->next )
        {
            if ( !Tris_CheckTextureRepeats( surf, chain->props ) )
            {
                Tris_FreeSurface( surf );
                return;
            }
        }
    }

    if ( !TrisLmapWantsLightmap( surf->props ) )
        return;

    if ( !Tris_CheckLightmapVecs( surf, surf->props->lmapVecs ) )
    {
        Tris_FreeSurface( surf );
        return;
    }

    LmapSurfBounds2D( surf->props->lmapVecs, surf->w, lmapBounds );

    width  = LightmapSizeForRange( lmapBounds[0][0], lmapBounds[1][0] );
    height = LightmapSizeForRange( lmapBounds[0][1], lmapBounds[1][1] );

    if ( width <= 512 && height <= 512 )
        return;

    neededScale = I_fmax( width * 0.001953125f, height * 0.001953125f ) * 10.0f / sampleScale;

    Tris_SurfaceMapError( 0, surf,
                          va( "Lightmap for surface needs to be %ix%i, which is larger than %ix%i;"
                              " try multiplying sampleScale by at least %.1lf",
                              width, height, 512, 512,
                              (double)( ceil( neededScale ) * 0.1f ) ) );
    Tris_FreeSurface( surf );
}


/* Tris_SurfaceMapError  0x0043edf0 */
void Tris_SurfaceMapError( int severity, TriSurf_t *surf, const char *message )
{
    const CoalesceNode_t *chain;
    const DrawSurf_t     *ds;

    if ( !surf->props->coalesceChain )
    {
        Assert( TrisLmapWantsLightmap( surf->props ) );
        ds = surf->props->ds;
    }
    else
    {
        for ( chain = surf->props->coalesceChain; ; chain = chain->next )
        {
            Assert( chain );
            if ( TrisLmapWantsLightmap( chain->props ) )
                break;
        }
        ds = chain->props->ds;
    }

    MapErrorWinding( severity, surf->w, ds->mapInfoIndex, ds->entityNum, ds->brushNum, message );
}


/* Tris_CheckTextureRepeats  0x0043eee0 */
bool Tris_CheckTextureRepeats( TriSurf_t *surf, const TriSurfProps_t *props )
{
    int   i;
    float repeats[2];

    for ( i = 0; i <= 1; i++ )
    {
        repeats[i] = Vec3Length( props->texVecs[i] );
        if ( repeats[i] > 1.001f )
        {
            Tris_DrawSurfMapError( 0, surf, props->ds, "Texture repeats too many times." );
            return false;
        }
    }
    return true;
}


/* Tris_DrawSurfMapError  0x0043ef60 */
void Tris_DrawSurfMapError( int severity, TriSurf_t *surf, const DrawSurf_t *ds,
                            const char *message )
{
    MapErrorWinding( severity, surf->w, ds->mapInfoIndex, ds->entityNum, ds->brushNum, message );
}


/* Tris_CheckLightmapVecs  0x0043efa0 */
bool Tris_CheckLightmapVecs( TriSurf_t *surf, const vec4_t *lmapVecs )
{
    int    i;
    float  length[2];
    vec3_t cross;
    float  dist;

    for ( i = 0; i <= 1; i++ )
    {
        length[i] = Vec3Length( lmapVecs[i] );
        if ( length[i] == 0.0f )
        {
            Tris_SurfaceMapError( 0, surf,
                "Lightmap coordinates don't change in at least one direction;"
                " try the LMAP or NATURAL buttons in the surface dialog." );
            return false;
        }

        if ( length[i] > 0.0019550782f &&
             Tris_LmapExtentExceeds( surf->w, lmapVecs[i], 512.0f, 2.0f ) )
        {
            Tris_SurfaceMapError( 0, surf, "Lightmap uses more than one pixel per inch." );
            return false;
        }
    }

    Vec3Cross( lmapVecs[0], lmapVecs[1], cross );
    dist = Vec3Dot( cross, surf->props->plane );
    if ( I_fabs( dist ) < (float)( length[0] * 0.001f * length[1] ) )
    {
        Tris_SurfaceMapError( 0, surf,
            "Lightmap coordinates are degenerate;"
            " try the LMAP or NATURAL buttons in the surface dialog." );
        return false;
    }

    return true;
}


/* Tris_LmapExtentExceeds  0x0043f0f0 */
bool Tris_LmapExtentExceeds( const winding_t *w, const vec3_t lmapVec, float scale, float limit )
{
    unsigned int i;
    float        dist;
    float        minDist;
    float        maxDist;

    minDist =  3.4028235e+38f;
    maxDist = -3.4028235e+38f;

    for ( i = 0; i < w->ptCount; i++ )
    {
        dist = Vec3Dot( lmapVec, w->pts[i] );
        if ( dist < minDist )
            minDist = dist;
        if ( maxDist < dist )
            maxDist = dist;
    }

    return ( maxDist - minDist ) * scale > limit + 0.001f;
}


/* Tris_FindWindings  0x0043f1a0 */
void Tris_FindWindings( Entity_t *e, Tree_t *tree )
{
    int         surfIndex;
    DrawSurf_t *ds;

    memset( triGlobSurfLists, 0, Tris_GetSurfListCount() * sizeof( triGlobSurfLists[0] ) );

    for ( surfIndex = e->firstDrawSurf; surfIndex < numMapDrawSurfs; surfIndex++ )
    {
        ds = &drawSurfs[surfIndex];

        if ( !ds->verts )
            continue;

        Assert( ds->mtlTex );
        Assert( ds->reflectionProbeIndex != REFLECTION_PROBE_INVALID );

        if ( ds->isPatch )
            Tris_FindPatchWindings( ds, tree );
        else if ( ds->isModel )
            Tris_FindIndexedMeshWindings( ds, tree );
        else
            Tris_FindBrushWindings( ds, tree );
    }
}


/* Tris_ComputeWindingBounds  0x004422d0 */
void Tris_ComputeWindingBounds( void )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;
    vec3_t     expand;

    listCount = Tris_GetSurfListCount();
    Vec3Set( expand, 0.2f, 0.2f, 0.2f );

    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        ClearBounds( triGlobListBounds[listIndex][0], triGlobListBounds[listIndex][1] );

        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
        {
            if ( surf->props->nodraw )
                continue;

            WindingBounds( surf->w, surf->mins, surf->maxs );
            Vec3Sub( surf->mins, expand, surf->mins );
            Vec3Add( surf->maxs, expand, surf->maxs );
            AddBoundsToBounds( surf->mins, surf->maxs,
                               triGlobListBounds[listIndex][0], triGlobListBounds[listIndex][1] );
        }
    }
}


/* Tris_CoalesceWindings  0x00441b40 */
void Tris_CoalesceWindings( void )
{
    int listCount;
    int listIndex;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
        TrisCoalesce( &triGlobSurfLists[listIndex] );
}


/* Tris_RemoveOccludedFragments  0x00441b90 */
void Tris_RemoveOccludedFragments( void )
{
    int         listCount;
    int         listIndex;
    TriSurf_t **link;
    TriSurf_t  *surf;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        link = &triGlobSurfLists[listIndex];
        while ( *link )
        {
            surf = *link;
            if ( !BrushSides_IsWindingVisible( CopyWinding( surf->w ) ) )
            {
                *link = surf->visGroupNext;
                Tris_FreeSurface( surf );
            }
            else
            {
                link = &surf->visGroupNext;
            }
        }
    }
}


/* Tris_AssignPrimaryLights  0x00442410 */
void Tris_AssignPrimaryLights( void )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
            AssignPrimaryLightsToSurface( surf );
    }
}


/* Tris_MarkAllSunShadowCasters  0x00442470 */
void Tris_MarkAllSunShadowCasters( void )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
            Tris_ConfirmCastsSunShadow( surf );
    }
}


/* Tris_FindSunShadowCasters  0x004424d0 */
void Tris_FindSunShadowCasters( void )
{
    int    listCount;
    int    listIndex;
    vec3_t mins;
    vec3_t maxs;

    listCount = Tris_GetSurfListCount();

    if ( !SunIsPrimaryLight() )
    {
        Tris_MarkAllSunShadowCasters();
        return;
    }

    ClearBounds( mins, maxs );
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        AddBoundsToBounds( triGlobListBounds[listIndex][0], triGlobListBounds[listIndex][1],
                           mins, maxs );
    }

    SetGridDivisionPoints( mins, maxs );
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
        TJuncAddSurfListToGrid( triGlobSurfLists[listIndex] );

    SortGridTree();
    Tris_MarkSunShadowCasters();
}


/* Tris_SplitLargeWindings  0x00441c30 */
void Tris_SplitLargeWindings( void )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;
    TriSurf_t *next;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = next )
        {
            next = surf->visGroupNext;
            Tris_SplitLargeWinding( surf, &triGlobSurfLists[listIndex] );
        }
    }
}


/* Tris_SplitLargeWinding  0x00441ca0 */
void Tris_SplitLargeWinding( TriSurf_t *surf, TriSurf_t **listHead )
{
    TriSurfProps_t *props;
    vec4_t          splitPlane;
    winding_t      *front;
    winding_t      *back;
    TriSurf_t      *frontSurf;
    TriSurf_t      *backSurf;

    props = surf->props;
    if ( !Tris_FindLightmapSplitPlane( surf->w, props, splitPlane ) )
        return;

    ClipWindingEpsilon( surf->w, splitPlane, splitPlane[3], 0.1f, &front, &back, 0 );

    Assert( front );
    Assert( back );

    Tris_FreeSurface( surf );

    frontSurf = PrependTriSurf( front, props, listHead );
    Tris_SplitLargeWinding( frontSurf, listHead );

    backSurf = PrependTriSurf( back, props, listHead );
    Tris_SplitLargeWinding( backSurf, listHead );
}


/* Tris_LmapTexelLimit  0x004421d0 */
static int Tris_LmapTexelLimit( int textureSize )
{
    (void)textureSize;
    return 0x2000;
}

static int Tris_LmapTexelLimitS( const TriSurfProps_t *props )  /* 0x004421b0 */
{
    return Tris_LmapTexelLimit( MATERIAL_TEX_WIDTH( props->ds->mtlTex ) );
}

static int Tris_LmapTexelLimitT( const TriSurfProps_t *props )  /* 0x004421e0 */
{
    return Tris_LmapTexelLimit( MATERIAL_TEX_HEIGHT( props->ds->mtlTex ) );
}


/* Tris_LmapCoordBounds  0x004420e0 */
static void Tris_LmapCoordBounds( const winding_t *w, const TriSurfProps_t *props,
                                  vec2_t mins, vec2_t maxs )
{
    unsigned int i;
    vec2_t       lmapCoord;

    Assert( props->coalesceChain == NULL );

    ClearBounds2D( mins, maxs );
    for ( i = 0; i < w->ptCount; i++ )
    {
        lmapCoord[0] = Vec3Dot( w->pts[i], props->lmapVecs[0] ) + props->lmapVecs[0][3];
        lmapCoord[1] = Vec3Dot( w->pts[i], props->lmapVecs[1] ) + props->lmapVecs[1][3];
        AddPointToBounds2D( lmapCoord, mins, maxs );
    }
}


/* Tris_LmapTexelExtent  0x00442060 */
static void Tris_LmapTexelExtent( const winding_t *w, const TriSurfProps_t *props, int extent[2] )
{
    vec2_t mins;
    vec2_t maxs;

    Tris_LmapCoordBounds( w, props, mins, maxs );

    extent[0] = (int)( ceil( maxs[0] ) - floor( mins[0] ) );
    extent[1] = (int)( ceil( maxs[1] ) - floor( mins[1] ) );
}


/* Tris_FindLightmapSplitPlane  0x00441db0 */
bool Tris_FindLightmapSplitPlane( const winding_t *w, const TriSurfProps_t *props, vec4_t outPlane )
{
    const CoalesceNode_t *chain;
    int    extent[2];
    int    over[2];
    int    axisIndex;
    int    axis;
    vec3_t mins;
    vec3_t maxs;
    vec3_t size;
    unsigned int i;
    float  mid;
    float  bestDist;
    float  dist;
    int    shift;
    int    grid;
    int    midInt;

    if ( props->coalesceChain )
    {
        for ( chain = props->coalesceChain; chain; chain = chain->next )
        {
            if ( Tris_FindLightmapSplitPlane( w, chain->props, outPlane ) )
                return true;
        }
        return false;
    }

    Tris_LmapTexelExtent( w, props, extent );
    over[0] = extent[0] - 2 * Tris_LmapTexelLimitS( props );
    over[1] = extent[1] - 2 * Tris_LmapTexelLimitT( props );

    if ( over[0] < 1 && over[1] < 1 )
        return false;

    axisIndex = ( over[0] < over[1] );

    Vec3Copy( w->pts[0], mins );
    Vec3Copy( w->pts[0], maxs );
    for ( i = 1; i < w->ptCount; i++ )
        AddPointToBounds( w->pts[i], mins, maxs );

    size[0] = ( maxs[0] - mins[0] ) * props->lmapVecs[axisIndex][0];
    size[1] = ( maxs[1] - mins[1] ) * props->lmapVecs[axisIndex][1];
    size[2] = ( maxs[2] - mins[2] ) * props->lmapVecs[axisIndex][2];

    axis = VecLargestAxis( size );
    mid  = ( mins[axis] + maxs[axis] ) * 0.5f;

    midInt = (int)( ceil( maxs[axis] ) - floor( mins[axis] ) );
    shift  = 0x1e - CountLeadingZeros( midInt );
    grid   = 1 << ( shift & 0x1f );

    outPlane[3] = (float)( ( (int)mid + grid / 2 ) & ~( grid - 1 ) );

    bestDist = midInt * 0.25f;
    for ( i = 0; i < w->ptCount; i++ )
    {
        dist = I_fabs( w->pts[i][axis] - mid );
        if ( dist < bestDist )
        {
            bestDist    = dist;
            outPlane[3] = w->pts[i][axis];
        }
    }

    Vec3Clear( outPlane );
    outPlane[axis] = 1.0f;
    return true;
}


/* Tris_PropsGroupableCallback  0x004422a0 */
static bool Tris_PropsGroupableCallback( const TriSurf_t *surf, const TriSurf_t *other )
{
    return TriSurfPropsGroupable( surf->props, other->props, other->w );
}


/* Tris_MergeIntoConcaveWindings  0x00442200 */
void Tris_MergeIntoConcaveWindings( void )
{
    int listCount;
    int listIndex;

    Merge_Init( 0x40000, Tris_PropsGroupableCallback, 1 );

    SetTrisTransientMode( 2, 0 );

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        MergeConcaveWindings( &triGlobSurfLists[listIndex],
                              triGlobListBounds[listIndex][0],
                              triGlobListBounds[listIndex][1],
                              Tris_MergeSurfacePairs );
    }

    SetTrisTransientMode( TRIS_TRANSIENT_NONE, 1 );
    Merge_Shutdown();
}


/* Tris_FixTJunctions  0x004425a0 */
void Tris_FixTJunctions( void )
{
    int    listCount;
    int    listIndex;
    vec3_t mins;
    vec3_t maxs;

    listCount = Tris_GetSurfListCount();

    ClearBounds( mins, maxs );
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        AddBoundsToBounds( triGlobListBounds[listIndex][0], triGlobListBounds[listIndex][1],
                           mins, maxs );
    }

    SetGridDivisionPoints( mins, maxs );
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
        TJuncAddSurfListToGrid( triGlobSurfLists[listIndex] );

    TJunc_Init();

    TJunc_SetEpsilon( 0.15f );
    TJunc_SetUseAxisBuckets( 1 );
    TJunc_FixGrid( mins, maxs );
    TJunc_SetEpsilon( 0.1f );
    TJunc_Shutdown();
}


/* Tris_BuildLightmapGroups  0x00442690 */
void Tris_BuildLightmapGroups( void )
{
    int    listCount;
    int    listIndex;
    int    otherIndex;
    vec3_t mins;
    vec3_t maxs;

    listCount = Tris_GetSurfListCount();

    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        triGlobSurfLists[listIndex] =
            TrisLmapBuildGroups( triGlobSurfLists[listIndex],
                                  triGlobListBounds[listIndex][0],
                                  triGlobListBounds[listIndex][1] );
    }

    ClearBounds( mins, maxs );
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        AddBoundsToBounds( triGlobListBounds[listIndex][0], triGlobListBounds[listIndex][1],
                           mins, maxs );
    }

    SetGridDivisionPoints( mins, maxs );

    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( otherIndex = 0; otherIndex < listIndex; otherIndex++ )
        {
            TrisLmapMergeAcrossLists( triGlobSurfLists[listIndex],
                                   triGlobListBounds[otherIndex][0],
                                   triGlobListBounds[otherIndex][1] );
        }
        TrisLmapAddListToGrid( triGlobSurfLists[listIndex] );
    }

    TrisLmapSplitPropsByGroup();
}


/* Tris_CheckLightmapsAssigned  0x004427e0 */
void Tris_CheckLightmapsAssigned( void )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
        {
            if ( surf->props->ds->mtlTex->techSetFlags & MTL_TECHSET_LIGHTMAP )
                Assert( surf->props->lmapIndex != LIGHTMAP_NONE );
        }
    }
}


/* Tris_SurfaceMayFloat  0x004428f0 */
static bool Tris_SurfaceMayFloat( const TriSurf_t *surf )
{
    const Material_t *material;

    Assert( surf->props->coalesceChain == NULL );

    material = surf->props->ds->mtlTex;

    if ( ( material->toolFlags & TOOLFLAG_USAGE_MASK ) == TOOLFLAG_USAGE_LIT )
        return true;
    if ( ( material->toolFlags & TOOLFLAG_USAGE_MASK ) == TOOLFLAG_USAGE_NOCOMBINE )
        return false;
    if ( ( material->contentFlags & 0x30 ) == 0 )
        return surf->props->ds->wasCoalesced == 0;

    return true;
}


/* Tris_ReportFloatingSurfaces  0x00442870 */
void Tris_ReportFloatingSurfaces( void )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;

    listCount = Tris_GetSurfListCount();
    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
        {
            if ( surf->props->coalesceChain )
                continue;
            if ( !Tris_SurfaceMayFloat( surf ) )
                ReportFloatingSurface( surf );
        }
    }
}


/* Tris_GenerateVerticesFromWinding  0x00442a00 */
static void Tris_GenerateVerticesFromWinding( const winding_t *w, const TriSurfProps_t *props )
{
    unsigned int i;
    int          vertIndex;

    (void)props;

    for ( i = 0; i < w->ptCount; i++ )
    {
        vertIndex = triGlobVertCount;
        triGlobVertCount++;
        Vec3Copy( w->pts[i], triGlobVerts[vertIndex].xyz );
        Vec3Copy( w->pts[i], triGlobVerts[vertIndex].xyzOrig );
    }
}


/* Tris_GenerateAllVertices  0x00442990 */
void Tris_GenerateAllVertices( void )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;

    listCount = Tris_GetSurfListCount();
    triGlobVertCount = 0;

    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
            Tris_GenerateVerticesFromWinding( surf->w, surf->props );
    }
}


/* Tris_BuildVertexMergeMap  0x00442a80 */
int *Tris_BuildVertexMergeMap( void )
{
    return BuildMergeMap( triGlobVerts[0].xyz, 3, sizeof( TriVert_t ), triGlobVertCount, 0.2f );
}


/* CountLeadingZeros  0x0044c040 */
static int CountLeadingZeros( unsigned int value )
{
    unsigned long index;

    if ( !value )
        return 0x1f ^ 63;

    for ( index = 31; !( value & ( 1u << index ) ); index-- )
        ;
    return 0x1f ^ (int)index;
}


/* MergeMapEntries  0x00442c70 */
static void MergeMapEntries( int a, int b, int *indexMap )
{
    int tmp;
    int i;

    tmp = a;
    if ( b < a )
    {
        a = b;
        b = tmp;
    }

    for ( i = b; i < triGlobVertCount; i++ )
    {
        if ( indexMap[i] == b )
            indexMap[i] = a;
    }
}


/* Tris_SnapNearbyVertices  0x00442ab0 */
void Tris_SnapNearbyVertices( int *indexMap )
{
    int          listCount;
    int          listIndex;
    int          firstVertIndex;
    TriSurf_t   *surf;
    int          axisU;
    int          axisV;
    unsigned int i;
    unsigned int j;
    vec2_t       delta;

    listCount      = Tris_GetSurfListCount();
    firstVertIndex = 0;

    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
        {
            GetProjectionAxes( surf->props->plane, &axisU, &axisV );

            for ( i = 0; i < surf->w->ptCount; i++ )
            {
                for ( j = i + 1; j < surf->w->ptCount; j++ )
                {
                    if ( indexMap[firstVertIndex + i] == indexMap[firstVertIndex + j] )
                        continue;

                    delta[0] = triGlobVerts[firstVertIndex + i].xyz[axisU] -
                               triGlobVerts[firstVertIndex + j].xyz[axisU];
                    delta[1] = triGlobVerts[firstVertIndex + i].xyz[axisV] -
                               triGlobVerts[firstVertIndex + j].xyz[axisV];

                    if ( Vec2LengthSq( delta ) < 1.9083023e-06f )
                    {
                        MergeMapEntries( indexMap[firstVertIndex + i],
                                         indexMap[firstVertIndex + j], indexMap );
                    }
                }
            }

            firstVertIndex += surf->w->ptCount;
        }
    }

    Assert( firstVertIndex == triGlobVertCount );
}


/* Tris_SnapWindingsToVertices  0x00442cd0 */
void Tris_SnapWindingsToVertices( const int *indexMap )
{
    int          listCount;
    int          listIndex;
    int          firstVertIndex;
    int          removedPointCount;
    TriSurf_t  **link;
    TriSurf_t   *surf;
    int          newPtCount;
    int          i;
    int          snappedWinding[MAX_POINTS_ON_WINDING * 16 + 1];  /* 0x4001 */

    listCount = Tris_GetSurfListCount();

    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_WORIG );

    firstVertIndex    = 0;
    removedPointCount = 0;

    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        link = &triGlobSurfLists[listIndex];
        for ( surf = *link; surf; surf = *link )
        {
            Assert( surf->w->ptCount <= ARRAY_COUNT( snappedWinding ) );

            newPtCount = CollapseLoop( firstVertIndex, surf->w->ptCount, indexMap, snappedWinding );

            surf->transient.wOrig = AllocWinding( newPtCount );
            for ( i = 0; i < newPtCount; i++ )
            {
                Vec3Copy( triGlobVerts[snappedWinding[i]].xyz,     surf->w->pts[i] );
                Vec3Copy( triGlobVerts[snappedWinding[i]].xyzOrig, surf->transient.wOrig->pts[i] );
            }

            firstVertIndex += surf->w->ptCount;

            if ( newPtCount < 3 )
            {
                removedPointCount += surf->w->ptCount;
                *link = surf->visGroupNext;
                Tris_FreeSurface( surf );
            }
            else
            {
                removedPointCount += surf->w->ptCount - newPtCount;
                surf->w->ptCount     = newPtCount;
                surf->transient.wOrig->ptCount = newPtCount;
                link = &surf->visGroupNext;
            }
        }
    }

    Assert( firstVertIndex == triGlobVertCount );
}


/* Tris_TriangulateAndEmit  0x00442f90 */
void Tris_TriangulateAndEmit( int trisType, Entity_t *e, Tree_t *tree )
{
    TrisContext_t *context;
    int           *indexMap;

    context = &triGlobContext[trisType];

    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_WORIG );

    Tris_StartPhase( "triangulating all windings" );
    Tris_TriangulateWindings( context );

    if ( entity_num == 0 )
    {
        Com_Printf( "%i self-tjunctions fixed\n%i degenerate tris removed\n",
                    triGlobSelfTjuncCount, g_degenerateTrisRemoved );
    }

    Tris_StartPhase( "smoothing normals" );
    indexMap = Tris_BuildVertexMergeMap();
    SmoothVertexNormals( indexMap );
    TrisTimerCheck();

    Sort_VC8( triGlobTris, triGlobTris + triGlobTriCount, Tris_TriSortsBefore );

    Tris_StartPhase( "emitting triangles" );

    if ( trisType == TRIS_TYPE_LAYERED && g_combineLayeredMaterials )
    {
        Tris_StartPhase( "combining layered materials" );
        Tris_CombineAllLayeredMaterials( indexMap );
    }

    SanityCheckx( e->firstTriSoup[trisType] == context->triCount,
                  "e->firstTriSurf[trisType] == context->triCount\n\t%i, %i",
                  e->firstTriSoup[trisType], context->triCount );

    Tris_EmitTriangleSoups( context, tree, indexMap );

    if ( e->firstTriSoup[trisType] == context->triCount )
        e->firstTriSoup[trisType] = 0;

    FreeMergeMap( indexMap );
}


/* Tris_TriangulateWindings  0x00443110 */
void Tris_TriangulateWindings( TrisContext_t *context )
{
    int        listCount;
    int        listIndex;
    TriSurf_t *surf;

    listCount = Tris_GetSurfListCount();

    Assert( assembleTrisContext == NULL );
    assembleTrisContext = context;

    triGlobTriCount  = 0;
    triGlobVertCount = 0;

    for ( listIndex = 0; listIndex < listCount; listIndex++ )
    {
        for ( surf = triGlobSurfLists[listIndex]; surf; surf = surf->visGroupNext )
        {
            if ( !TesselateWinding( surf, listIndex, Tris_EmitTriangleRecord ) )
                TesselateFailed( surf );
        }
    }

    assembleTrisContext = NULL;
}


/* Tris_EmitTriangleRecord  0x004431e0 */
void Tris_EmitTriangleRecord( const winding_t *w, const winding_t *wOrig,
                              TriSurfProps_t *props, int i0, int i1, int i2,
                              int visGroupIndex )
{
    vec4_t plane;

    Assert( w );
    Assert( wOrig );
    Assert( props );

    if ( !PlaneFromPoints( plane, w->pts[i0], w->pts[i1], w->pts[i2] ) )
        return;

    if ( !props->coalesceChain )
        Tris_EmitSimpleTriangle( w, wOrig, i0, i1, i2, visGroupIndex, props, props, plane );
    else
        Tris_EmitChainTriangles( w, wOrig, i0, i1, i2, visGroupIndex, props,
                                 props->coalesceChain, plane );
}


/* ColorFromVecs  0x00443b90 */
static void ColorFromVecs( const vec4_t colorVecs[4], const vec3_t point, byte out[4] )
{
    out[0] = (byte)( ( RoundFloatToInt( Vec3Dot( point, colorVecs[0] ) + colorVecs[0][3] ) > 255
                       ? 255 : RoundFloatToInt( Vec3Dot( point, colorVecs[0] ) + colorVecs[0][3] ) ) < 0
                     ? 0
                     : ( RoundFloatToInt( Vec3Dot( point, colorVecs[0] ) + colorVecs[0][3] ) > 255
                         ? 255 : RoundFloatToInt( Vec3Dot( point, colorVecs[0] ) + colorVecs[0][3] ) ) );

    out[1] = (byte)( ( RoundFloatToInt( Vec3Dot( point, colorVecs[1] ) + colorVecs[1][3] ) > 255
                       ? 255 : RoundFloatToInt( Vec3Dot( point, colorVecs[1] ) + colorVecs[1][3] ) ) < 0
                     ? 0
                     : ( RoundFloatToInt( Vec3Dot( point, colorVecs[1] ) + colorVecs[1][3] ) > 255
                         ? 255 : RoundFloatToInt( Vec3Dot( point, colorVecs[1] ) + colorVecs[1][3] ) ) );

    out[2] = (byte)( ( RoundFloatToInt( Vec3Dot( point, colorVecs[2] ) + colorVecs[2][3] ) > 255
                       ? 255 : RoundFloatToInt( Vec3Dot( point, colorVecs[2] ) + colorVecs[2][3] ) ) < 0
                     ? 0
                     : ( RoundFloatToInt( Vec3Dot( point, colorVecs[2] ) + colorVecs[2][3] ) > 255
                         ? 255 : RoundFloatToInt( Vec3Dot( point, colorVecs[2] ) + colorVecs[2][3] ) ) );

    out[3] = (byte)( ( RoundFloatToInt( Vec3Dot( point, colorVecs[3] ) + colorVecs[3][3] ) > 255
                       ? 255 : RoundFloatToInt( Vec3Dot( point, colorVecs[3] ) + colorVecs[3][3] ) ) < 0
                     ? 0
                     : ( RoundFloatToInt( Vec3Dot( point, colorVecs[3] ) + colorVecs[3][3] ) > 255
                         ? 255 : RoundFloatToInt( Vec3Dot( point, colorVecs[3] ) + colorVecs[3][3] ) ) );
}


/* Tris_EmitSimpleTriangle  0x00443310 */
void Tris_EmitSimpleTriangle( const winding_t *w, const winding_t *wOrig,
                              int i0, int i1, int i2, short visGroupIndex,
                              const TriSurfProps_t *props, const TriSurfProps_t *texProps,
                              const vec4_t plane )
{
    vec4_t     texVecS;
    vec4_t     texVecT;
    TriVert_t *verts;
    int        primaryLightIndex;
    int        i;

    if ( triGlobTriCount > MAX_MAP_TRIANGLES - 1 )
        Com_Error( "MAX_MAP_TRIANGLES (%i) exceeded\n", MAX_MAP_TRIANGLES );
    if ( triGlobVertCount + 3 > MAX_MAP_VERTEXES )
        Com_Error( "MAX_MAP_VERTEXES (%i) exceeded\n", MAX_MAP_VERTEXES );

    Vec4Copy( texProps->texVecs[0], texVecS );
    Vec4Copy( texProps->texVecs[1], texVecT );

    if ( !( texProps->ds->mtlTex->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS ) )
    {
        texVecS[3] -= FloorFloatToInt( texVecS[3] );
        texVecT[3] -= FloorFloatToInt( texVecT[3] );
    }

    verts = &triGlobVerts[triGlobVertCount];

    Vec3Copy( wOrig->pts[i0], verts[0].xyz );
    Vec3Copy( wOrig->pts[i1], verts[1].xyz );
    Vec3Copy( wOrig->pts[i2], verts[2].xyz );

    for ( i = 0; i < 3; i++ )
    {
        verts[i].texCoord[0][0] = Vec3Dot( verts[i].xyz, texVecS ) + texVecS[3];
        verts[i].texCoord[0][1] = Vec3Dot( verts[i].xyz, texVecT ) + texVecT[3];

        verts[i].lmapCoord[0] = Vec3Dot( verts[i].xyz, props->lmapVecs[0] ) + props->lmapVecs[0][3];
        verts[i].lmapCoord[1] = Vec3Dot( verts[i].xyz, props->lmapVecs[1] ) + props->lmapVecs[1][3];

        Vec3Copy( plane, verts[i].normal );
        verts[i].smoothing = texProps->ds->smoothing;

        ColorFromVecs( texProps->colorVecs, verts[i].xyz, verts[i].color );

        if ( verts[i].lmapCoord[0] < 0.0f )
            verts[i].lmapCoord[0] = 0.0f;
        else if ( verts[i].lmapCoord[0] > 1.0f )
            verts[i].lmapCoord[0] = 1.0f;

        if ( verts[i].lmapCoord[1] < 0.0f )
            verts[i].lmapCoord[1] = 0.0f;
        else if ( verts[i].lmapCoord[1] > 1.0f )
            verts[i].lmapCoord[1] = 1.0f;
    }

    Vec3Copy( w->pts[i0], verts[0].xyz );
    Vec3Copy( w->pts[i1], verts[1].xyz );
    Vec3Copy( w->pts[i2], verts[2].xyz );

    primaryLightIndex = props->ds->primaryLightIndex;
    if ( primaryLightIndex == 256 )
        primaryLightIndex = 0;

    Assertx( ( primaryLightIndex >= 0 && primaryLightIndex < numBSPPrimaryLights ) ||
             primaryLightIndex == 0,
             "(primaryLightIndex) = %i", primaryLightIndex );

    {
        Tri_t *tri = &triGlobTris[triGlobTriCount];

        tri->visGroupIndex        = visGroupIndex;
        tri->lmapIndex            = (byte)props->lmapIndex;
        tri->reflectionProbeIndex = (byte)texProps->ds->reflectionProbeIndex;
        tri->primaryLightIndex    = (byte)primaryLightIndex;
        tri->castsSunShadow       = props->ds->castsSunShadow;
        tri->lyrMtlDesc               = Tris_EmitMaterial( &texProps->ds->mtlTex, 1 );
        tri->vertIndices[0]       = triGlobVertCount;
        tri->vertIndices[1]       = triGlobVertCount + 1;
        tri->vertIndices[2]       = triGlobVertCount + 2;
        Vec3Copy( texVecS, tri->texVecs[0][0] );
        Vec3Copy( texVecT, tri->texVecs[0][1] );
    }

    triGlobTriCount++;
    triGlobVertCount += 3;
}


/* Tris_EmitChainTriangles  0x00443f50 */
void Tris_EmitChainTriangles( const winding_t *w, const winding_t *wOrig,
                              int i0, int i1, int i2, short visGroupIndex,
                              TriSurfProps_t *props, CoalesceNode_t *chain,
                              const vec4_t plane )
{
    bool isFirst = true;

    do
    {
        chain = Tris_EmitChainTriangle( w, wOrig, i0, i1, i2, visGroupIndex,
                                        props, chain, isFirst, plane );
        isFirst = false;
    }
    while ( chain );
}


/* Tris_EmitChainTriangle  0x00443fa0 */
CoalesceNode_t *Tris_EmitChainTriangle( const winding_t *w, const winding_t *wOrig,
                                        int i0, int i1, int i2, short visGroupIndex,
                                        TriSurfProps_t *props, CoalesceNode_t *chain,
                                        bool isFirst, const vec4_t plane )
{
    CoalesceNode_t *next[MTL_LAYER_LIMIT];
    Material_t     *materials[MTL_LAYER_LIMIT];
    CoalesceNode_t *node;
    Material_t     *lmapMaterial;
    int             layerCount;
    int             usedCount;
    LayeredMaterialDesc_t *lyrMtlDesc;

    if ( assembleTrisContext->type == TRIS_TYPE_SIMPLE )
    {
        Tris_EmitSimpleTriangle( w, wOrig, i0, i1, i2, visGroupIndex, props, chain->props, plane );
        return chain->next;
    }

    Assert( chain->props->coalesceChain == NULL );

    lmapMaterial = chain->props->ds->lmapMaterial;

    node       = chain;
    layerCount = 0;
    while ( node && layerCount < MTL_LAYER_LIMIT &&
            node->props->ds->lmapMaterial == lmapMaterial )
    {
        materials[layerCount] = node->props->ds->mtlTex;
        next[layerCount]      = node->next;
        node                  = node->next;
        layerCount++;
    }

    usedCount = Material_LayerCombineCount( materials, layerCount, isFirst );
    if ( usedCount == 1 )
    {
        Tris_EmitSimpleTriangle( w, wOrig, i0, i1, i2, visGroupIndex, props, chain->props, plane );
        return next[0];
    }

    lyrMtlDesc = Tris_EmitMaterial( materials, usedCount );
    Tris_EmitLayeredTriangle( w, wOrig, i0, i1, i2, visGroupIndex, props, chain, lyrMtlDesc, plane );
    return next[usedCount - 1];
}


/* Tris_EmitLayeredTriangle  0x00444120 */
void Tris_EmitLayeredTriangle( const winding_t *w, const winding_t *wOrig,
                               int i0, int i1, int i2, short visGroupIndex,
                               const TriSurfProps_t *props, CoalesceNode_t *chain,
                               LayeredMaterialDesc_t *lyrMtlDesc, const vec4_t plane )
{
    vec4_t          texVecS[MTL_LAYER_LIMIT];
    vec4_t          texVecT[MTL_LAYER_LIMIT];
    byte            layerAlpha[8];
    byte            color[4];
    CoalesceNode_t *node;
    TriVert_t      *verts;
    int             layerCount;
    int             layer;
    int             i;
    int             primaryLightIndex;
    byte            reflectionProbeIndex;
    byte            lmapIndex;

    if ( triGlobTriCount > MAX_MAP_TRIANGLES - 1 )
        Com_Error( "MAX_MAP_TRIANGLES (%i) exceeded\n", MAX_MAP_TRIANGLES );
    if ( triGlobVertCount + 3 > MAX_MAP_VERTEXES )
        Com_Error( "MAX_MAP_VERTEXES (%i) exceeded\n", MAX_MAP_VERTEXES );

    reflectionProbeIndex = 0;
    lmapIndex            = ( byte )props->lmapIndex;
    layerCount = LayeredMaterialLayerCount( lyrMtlDesc );

    node = chain;
    for ( layer = 0; layer < layerCount; layer++ )
    {
        Vec4Copy( node->props->texVecs[0], texVecS[layer] );
        Vec4Copy( node->props->texVecs[1], texVecT[layer] );

        if ( !( node->props->ds->mtlTex->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS ) )
        {
            texVecS[layer][3] -= FloorFloatToInt( texVecS[layer][3] );
            texVecT[layer][3] -= FloorFloatToInt( texVecT[layer][3] );
        }

        if ( reflectionProbeIndex == 0 )
            reflectionProbeIndex = (byte)node->props->ds->reflectionProbeIndex;

        node = node->next;
    }

    verts = &triGlobVerts[triGlobVertCount];

    Vec3Copy( wOrig->pts[i0], verts[0].xyz );
    Vec3Copy( wOrig->pts[i1], verts[1].xyz );
    Vec3Copy( wOrig->pts[i2], verts[2].xyz );

    for ( i = 0; i < 3; i++ )
    {
        Vec4bSet( layerAlpha, 255, 255, 255, 255 );

        node = chain;
        for ( layer = 0; layer < layerCount; layer++ )
        {
            verts[i].texCoord[layer][0] = Vec3Dot( verts[i].xyz, texVecS[layer] ) + texVecS[layer][3];
            verts[i].texCoord[layer][1] = Vec3Dot( verts[i].xyz, texVecT[layer] ) + texVecT[layer][3];

            ColorFromVecs( node->props->colorVecs, verts[i].xyz, color );
            layerAlpha[layer] = Tris_LayerAlpha( node->props->ds, color );

            node = node->next;
        }

        PackVertexColor( &layerAlpha[I_max( 0, layerCount - 4 )], verts[i].color );

        verts[i].lmapCoord[0] = Vec3Dot( verts[i].xyz, props->lmapVecs[0] ) + props->lmapVecs[0][3];
        verts[i].lmapCoord[1] = Vec3Dot( verts[i].xyz, props->lmapVecs[1] ) + props->lmapVecs[1][3];

        Vec3Copy( plane, verts[i].normal );
        verts[i].smoothing = chain->props->ds->smoothing;

        if ( verts[i].lmapCoord[0] < 0.0f )
            verts[i].lmapCoord[0] = 0.0f;
        else if ( verts[i].lmapCoord[0] > 1.0f )
            verts[i].lmapCoord[0] = 1.0f;

        if ( verts[i].lmapCoord[1] < 0.0f )
            verts[i].lmapCoord[1] = 0.0f;
        else if ( verts[i].lmapCoord[1] > 1.0f )
            verts[i].lmapCoord[1] = 1.0f;
    }

    Vec3Copy( w->pts[i0], verts[0].xyz );
    Vec3Copy( w->pts[i1], verts[1].xyz );
    Vec3Copy( w->pts[i2], verts[2].xyz );

    primaryLightIndex = props->ds->primaryLightIndex;
    if ( primaryLightIndex == 256 )
        primaryLightIndex = 0;

    Assertx( ( primaryLightIndex >= 0 && primaryLightIndex < numBSPPrimaryLights ) ||
             primaryLightIndex == 0,
             "(primaryLightIndex) = %i", primaryLightIndex );

    {
        Tri_t *tri = &triGlobTris[triGlobTriCount];

        tri->visGroupIndex        = visGroupIndex;
        tri->lmapIndex            = lmapIndex;
        tri->reflectionProbeIndex = reflectionProbeIndex;
        tri->primaryLightIndex    = (byte)primaryLightIndex;
        tri->castsSunShadow       = props->ds->castsSunShadow;
        tri->lyrMtlDesc               = lyrMtlDesc;
        tri->vertIndices[0]       = triGlobVertCount;
        tri->vertIndices[1]       = triGlobVertCount + 1;
        tri->vertIndices[2]       = triGlobVertCount + 2;

        for ( i = 0; i < lyrMtlDesc->normalMapCount; i++ )
        {
            layer = lyrMtlDesc->region[i];
            Vec3Copy( texVecS[layer], tri->texVecs[layer][0] );
            Vec3Copy( texVecT[layer], tri->texVecs[layer][1] );
        }
    }

    triGlobTriCount++;
    triGlobVertCount += 3;
}


/* Tris_LayerAlpha  0x00444810 */
static byte Tris_LayerAlpha( const DrawSurf_t *ds, const byte color[4] )
{
    byte tint[4];
    int  tintSum;
    int  colorSum;

    if ( ( ds->toolFlags & TOOLFLAG_USAGE_MASK ) != TOOLFLAG_USAGE_VCOLOR )
        return color[3];

    Tris_GetColorTint( ds, tint );

    tintSum  = tint[2] + tint[1] + tint[0];
    colorSum = color[2] + color[1] + color[0];

    return (byte)( ( colorSum * 255 + 127 ) / tintSum );
}


/* Tris_TriSortsBefore  0x00444c40 */
bool Tris_TriSortsBefore( const Tri_t &a, const Tri_t &b )
{
    if ( a.visGroupIndex != b.visGroupIndex )
        return a.visGroupIndex < b.visGroupIndex;
    if ( (size_t)a.lyrMtlDesc != (size_t)b.lyrMtlDesc )
        return (size_t)a.lyrMtlDesc < (size_t)b.lyrMtlDesc;
    if ( a.reflectionProbeIndex != b.reflectionProbeIndex )
        return a.reflectionProbeIndex < b.reflectionProbeIndex;
    if ( a.primaryLightIndex != b.primaryLightIndex )
        return a.primaryLightIndex < b.primaryLightIndex;
    if ( a.lmapIndex != b.lmapIndex )
        return a.lmapIndex < b.lmapIndex;
    if ( !g_matchSunShadowOnCoalesce )
        return false;
    return a.castsSunShadow < b.castsSunShadow;
}


/* CompareVertsByIndexMap  0x00444c00 */
static int CompareVertsByIndexMap( const void *va, const void *vb )
{
    return triGlobIndexMap[*(const int *)va] - triGlobIndexMap[*(const int *)vb];
}


/* SmoothVertexNormalsForGroup  0x00444a50 */
static void SmoothVertexNormalsForGroup( const int *sortedVerts, int groupSize )
{
    int        i;
    int        j;
    TriVert_t *vert;
    vec3_t     preferredMean;
    vec3_t     smoothMean;

    for ( i = 0; i < groupSize; i++ )
    {
        vert = &triGlobVerts[sortedVerts[i]];

        if ( vert->smoothing != 1 )
        {
            Vec3Copy( vert->normal, vert->smoothNormal );
            continue;
        }

        Vec3Clear( preferredMean );
        Vec3Clear( smoothMean );

        for ( j = 0; j < groupSize; j++ )
        {
            TriVert_t *other = &triGlobVerts[sortedVerts[j]];

            if ( Vec3Dot( vert->normal, other->normal ) < smoothAngle )
                continue;

            if ( other->smoothing == 2 )
                Vec3Add( preferredMean, other->normal, preferredMean );
            else if ( other->smoothing == 1 )
                Vec3Add( smoothMean, other->normal, smoothMean );
        }

        if ( Vec3LengthSq( preferredMean ) > 0.0f )
            Vec3Copy( preferredMean, smoothMean );

        Assert( Vec3LengthSq( smoothMean ) > 0.0f );

        Vec3Normalize( smoothMean );
        Vec3Copy( smoothMean, vert->smoothNormal );
    }
}


/* SmoothVertexNormals  0x00444880 */
void SmoothVertexNormals( int *indexMap )
{
    int  vertCount;
    int *sortedVerts;
    int  i;
    int  end;
    int  thisIndexMap;

    for ( i = 0; i < triGlobVertCount; i++ )
        Vec3Copy( triGlobVerts[i].normal, triGlobVerts[i].smoothNormal );

    vertCount   = triGlobVertCount;
    sortedVerts = new int[vertCount];
    for ( i = 0; i < vertCount; i++ )
        sortedVerts[i] = i;

    SanityCheck( triGlobIndexMap == NULL );
    triGlobIndexMap = indexMap;
    qsort_vc8( sortedVerts, vertCount, sizeof( sortedVerts[0] ), CompareVertsByIndexMap );
    triGlobIndexMap = NULL;

    for ( i = 0; i < vertCount; )
    {
        thisIndexMap = indexMap[sortedVerts[i]];

        Assert( i == 0 || thisIndexMap > indexMap[sortedVerts[i - 1]] );

        for ( end = i + 1; end < vertCount; end++ )
        {
            if ( indexMap[sortedVerts[end]] != thisIndexMap )
                break;
        }

        SmoothVertexNormalsForGroup( &sortedVerts[i], end - i );
        i = end;
    }

    delete [] sortedVerts;
}


/* Tris_EmitTriangleSoups  0x00444d20 */
void Tris_EmitTriangleSoups( TrisContext_t *context, Tree_t *tree, const int *indexMap )
{
    int   i;
    int   j;
    int   cellIndex;
    int   lastVisGroup;
    bool  aabbTreeOpen;
    Tri_t firstTri;

    if ( entity_num == 0 )
    {
        memset( s_cellCullGroupBits, 0, sizeof( s_cellCullGroupBits ) );
        for ( i = 0; i < numCullGroups; i++ )
            ClearBounds( context->cullGroups[i].mins, context->cullGroups[i].maxs );
        for ( cellIndex = 0; cellIndex < numCells; cellIndex++ )
            bspCells[cellIndex].aabbTreeIndex[context->type] = 0x8000;
    }

    Sort_VC8( triGlobTris, triGlobTris + triGlobTriCount, Tris_TriSortsBefore );

    lastVisGroup = -1;
    aabbTreeOpen = false;

    for ( i = 0; i < triGlobTriCount; i = j )
    {
        for ( j = i + 1; j < triGlobTriCount; j++ )
        {
            if ( j - i > 0x5554 )
                break;
            firstTri = triGlobTris[j];
            if ( Tris_TriSortsBefore( triGlobTris[i], firstTri ) )
                break;
        }

        if ( ( triGlobTris[i].lyrMtlDesc->mtlRaw[0]->surfaceFlags & ( SURF_NODRAW | SURF_NOCASTSHADOW ) )
             == ( SURF_NODRAW | SURF_NOCASTSHADOW ) )
            continue;

        Assert( j - i >= 1 );

        if ( entity_num == 0 && lastVisGroup != triGlobTris[i].visGroupIndex )
        {
            lastVisGroup = triGlobTris[i].visGroupIndex;
            Assert( triGlobTris[i].visGroupIndex >= 0 );

            if ( lastVisGroup < numCells )
            {
                if ( aabbTreeOpen )
                    Tris_BuildAabbTree( context );

                bspCells[lastVisGroup].aabbTreeIndex[context->type] = (short)context->aabbTreeCount;
                context->aabbTrees[context->aabbTreeCount].firstSurface = context->triCount;
                context->aabbTrees[context->aabbTreeCount].surfaceCount = 0;
                context->aabbTrees[context->aabbTreeCount].childCount   = 0;
                aabbTreeOpen = true;
            }
            else
            {
                if ( aabbTreeOpen )
                    Tris_BuildAabbTree( context );
                aabbTreeOpen = false;

                context->cullGroups[lastVisGroup - numCells].firstSurface = context->triCount;
                context->cullGroups[lastVisGroup - numCells].surfaceCount = 0;
            }
        }

        Tris_GroupAndMergeTriSubgroups( &triGlobTris[i], j - i, indexMap );
        Tris_EmitTriangleGroup( context, &triGlobTris[i], j - i, tree );

        if ( entity_num == 0 )
        {
            if ( triGlobTris[i].visGroupIndex < numCells )
            {
                BspAabbTree_t *node =
                    &context->aabbTrees[bspCells[triGlobTris[i].visGroupIndex].aabbTreeIndex[context->type]];
                node->surfaceCount = context->triCount - node->firstSurface;
            }
            else
            {
                BspCullGroup_t *group =
                    &context->cullGroups[triGlobTris[i].visGroupIndex - numCells];
                group->surfaceCount = context->triCount - group->firstSurface;
            }
        }
    }

    if ( aabbTreeOpen )
        Tris_BuildAabbTree( context );

    if ( entity_num == 0 )
    {
        for ( cellIndex = 0; cellIndex < numCells; cellIndex++ )
        {
            if ( bspCells[cellIndex].aabbTreeIndex[context->type] != 0x8000 )
                continue;

            bspCells[cellIndex].aabbTreeIndex[context->type] =
                checked_cast<unsigned short>( context->aabbTreeCount );
            context->aabbTrees[context->aabbTreeCount].firstSurface = 0;
            context->aabbTrees[context->aabbTreeCount].surfaceCount = 0;
            context->aabbTrees[context->aabbTreeCount].childCount   = 0;
            Tris_BuildAabbTree( context );
        }
    }
}


/* Tris_GroupAndMergeTriSubgroups  0x004451e0 */
void Tris_GroupAndMergeTriSubgroups( Tri_t *tris, int triCount, const int *indexMap )
{
    unsigned short groupCount;

    groupCount = GroupTriSurfsIntoSubgroups( &tris, &triCount, 1, indexMap );
    groupCount = Tris_MergeTriSubgroups( tris, triCount, groupCount );

    if ( tris->lyrMtlDesc->layerCount > 1 )
        Tris_CheckLayeredMaterialArea( tris, triCount, groupCount );
}


/* Tris_MergeTriSubgroups  0x00445240 */
unsigned short Tris_MergeTriSubgroups( Tri_t *tris, int triCount, unsigned short groupCount )
{
    TriGroup_t group[MAX_TRI_SUBGROUPS];

    unsigned short groupId;
    unsigned short groupIndex0;
    unsigned short groupIndex1;
    unsigned short oldGroupCount;
    int            triIndex;
    int            end;
    int            axis;
    vec3_t         mins;
    vec3_t         extents;
    vec3_t         maxs;

    groupId = 0;

    for ( triIndex = 0; triIndex < triCount; triIndex = end )
    {
        ClearBounds( group[groupId].mins, group[groupId].maxs );
        group[groupId].firstTri = triIndex;

        for ( end = triIndex + 1;
              end < triCount && tris[end].triGroupId == tris[triIndex].triGroupId;
              end++ )
        {
            tris[end].triGroupId = groupId;
            AddPointToBounds( triGlobVerts[tris[end].vertIndices[0]].xyz,
                              group[groupId].mins, group[groupId].maxs );
            AddPointToBounds( triGlobVerts[tris[end].vertIndices[1]].xyz,
                              group[groupId].mins, group[groupId].maxs );
            AddPointToBounds( triGlobVerts[tris[end].vertIndices[2]].xyz,
                              group[groupId].mins, group[groupId].maxs );
        }

        tris[triIndex].triGroupId = groupId;
        AddPointToBounds( triGlobVerts[tris[triIndex].vertIndices[0]].xyz,
                          group[groupId].mins, group[groupId].maxs );
        AddPointToBounds( triGlobVerts[tris[triIndex].vertIndices[1]].xyz,
                          group[groupId].mins, group[groupId].maxs );
        AddPointToBounds( triGlobVerts[tris[triIndex].vertIndices[2]].xyz,
                          group[groupId].mins, group[groupId].maxs );

        Vec3Sub( group[groupId].maxs, group[groupId].mins, group[groupId].extents );

        group[groupId].triCount = end - triIndex;
        groupId++;
    }

    Assert( groupId == groupCount );

    if ( groupCount == 1 )
        return 1;

    do
    {
        oldGroupCount = groupCount;

        for ( groupIndex0 = (unsigned short)( groupCount - 2 );
              groupIndex0 != (unsigned short)0xffff;
              groupIndex0-- )
        {
            groupIndex1 = groupIndex0;

            while ( ++groupIndex1 < groupCount )
            {
                if ( group[groupIndex1].triCount + group[groupIndex0].triCount > 0x20 )
                    continue;

                for ( axis = 0; axis < 3; axis++ )
                {
                    mins[axis] = group[groupIndex0].mins[axis] <= group[groupIndex1].mins[axis]
                                 ? group[groupIndex0].mins[axis]
                                 : group[groupIndex1].mins[axis];
                    maxs[axis] = group[groupIndex0].maxs[axis] >= group[groupIndex1].maxs[axis]
                                 ? group[groupIndex0].maxs[axis]
                                 : group[groupIndex1].maxs[axis];
                    extents[axis] = maxs[axis] - mins[axis];

                    if ( group[groupIndex0].extents[axis] + group[groupIndex1].extents[axis] + 128.0f
                         < extents[axis] )
                        break;
                }

                if ( axis > 2 )
                {
                    Tris_MergeTriGroupPair( tris, triCount, groupIndex0, groupIndex1,
                                            mins, maxs, extents, group, groupCount );
                    groupCount--;
                    groupIndex1 = groupIndex0;
                }
            }
        }
    }
    while ( groupCount != oldGroupCount );

    return groupCount;
}


/* Tris_MergeTriGroupPair  0x00445820 */
void Tris_MergeTriGroupPair( Tri_t *tris, int triCount, unsigned short firstGroup,
                             unsigned short secondGroup, const vec3_t mins, const vec3_t maxs,
                             const vec3_t extents, TriGroup_t *group, int groupCount )
{
    int    triIndex;
    int    groupIndex;
    int    insertAt;
    int    moveCount;
    Tri_t *scratch;

    Assert( tris );
    Assert( firstGroup < secondGroup );
    Assert( secondGroup < groupCount );
    Assert( group[firstGroup].firstTri + group[firstGroup].triCount
            <= group[secondGroup].firstTri );
    Assert( group[secondGroup].firstTri + group[secondGroup].triCount <= triCount );
    Assert( mins );
    Assert( maxs );

    for ( triIndex = group[secondGroup].firstTri;
          triIndex < group[secondGroup].firstTri + group[secondGroup].triCount;
          triIndex++ )
    {
        SanityCheck( tris[triIndex].triGroupId == secondGroup );
        tris[triIndex].triGroupId = firstGroup;
    }

    for ( ; triIndex < triCount; triIndex++ )
    {
        SanityCheck( tris[triIndex].triGroupId > secondGroup );
        tris[triIndex].triGroupId--;
    }

    moveCount = group[secondGroup].triCount;

    if ( secondGroup - firstGroup > 1 )
    {
        insertAt = group[firstGroup].firstTri + group[firstGroup].triCount;

        scratch = new Tri_t[moveCount];
        memcpy( scratch, &tris[group[secondGroup].firstTri], moveCount * sizeof( Tri_t ) );
        memmove( &tris[insertAt + moveCount], &tris[insertAt],
                 ( group[secondGroup].firstTri - insertAt ) * sizeof( Tri_t ) );
        memcpy( &tris[insertAt], scratch, moveCount * sizeof( Tri_t ) );
        delete [] scratch;

        for ( groupIndex = firstGroup + 1; groupIndex < (int)secondGroup; groupIndex++ )
            group[groupIndex].firstTri += moveCount;
    }

    for ( groupIndex = secondGroup; groupIndex + 1 < groupCount; groupIndex++ )
        group[groupIndex] = group[groupIndex + 1];

    group[firstGroup].triCount += moveCount;

    Vec3Copy( mins,    group[firstGroup].mins );
    Vec3Copy( maxs,    group[firstGroup].maxs );
    Vec3Copy( extents, group[firstGroup].extents );
}


/* Tris_TriangleNormalAndArea  0x00445fc0 */
static float Tris_TriangleNormalAndArea( const TriVert_t *v0, const TriVert_t *v1,
                                         const TriVert_t *v2, vec3_t normal )
{
    vec3_t edge0;
    vec3_t edge1;

    Vec3Sub( v1->xyz, v0->xyz, edge0 );
    Vec3Sub( v2->xyz, v0->xyz, edge1 );
    Vec3Cross( edge1, edge0, normal );

    return Vec3Length( normal );
}


/* Tris_FreeLayeredMaterialRefPoints  0x00446020 */
static void Tris_FreeLayeredMaterialRefPoints( LayeredMaterialDesc_t *desc )
{
    LayeredMaterialRef_t *ref;

    while ( desc->refPoints )
    {
        ref             = desc->refPoints;
        desc->refPoints = ref->next;
        delete ref;
    }
}


/* Tris_AddLayeredMaterialRefPoints  0x00446060 */
static void Tris_AddLayeredMaterialRefPoints( LayeredMaterialDesc_t *desc, const vec3_t *positions,
                                              const vec3_t *normals, int groupCount )
{
    int                   groupIndex;
    LayeredMaterialRef_t *ref;

    for ( groupIndex = 0; groupIndex < groupCount; groupIndex++ )
    {
        ref = new LayeredMaterialRef_t;

        if ( !ref )
            Com_Error( "Out of memory for layered material ref pts" );

        Vec3Add( entities[entity_num].origin, positions[groupIndex], ref->pos );
        Vec3Copy( normals[groupIndex], ref->normal );

        ref->next       = desc->refPoints;
        desc->refPoints = ref;
    }
}


/* Tris_CheckLayeredMaterialArea  0x00445c50 */
void Tris_CheckLayeredMaterialArea( Tri_t *tris, int triCount, int groupCount )
{
    vec3_t           groupPos[MAX_TRI_SUBGROUPS];
    vec3_t           groupNormal[MAX_TRI_SUBGROUPS];
    char             name[0x400];
    int              groupIndex;
    int              triIndex;
    float            totalArea;
    float            groupArea;
    float            largestArea;
    float            area;
    float            invArea;
    const TriVert_t *v0;
    const TriVert_t *v1;
    const TriVert_t *v2;
    vec3_t           normal;

    totalArea = 0.0f;
    triIndex  = 0;

    for ( groupIndex = 0; groupIndex < groupCount; groupIndex++ )
    {
        groupArea   = 0.0f;
        largestArea = 0.0f;

        while ( triIndex < triCount && tris[triIndex].triGroupId == groupIndex )
        {
            v0 = &triGlobVerts[tris[triIndex].vertIndices[0]];
            v1 = &triGlobVerts[tris[triIndex].vertIndices[1]];
            v2 = &triGlobVerts[tris[triIndex].vertIndices[2]];
            triIndex++;

            area = Tris_TriangleNormalAndArea( v0, v1, v2, normal );

            groupArea = area * 0.5f + groupArea;
            totalArea = area * 0.5f + totalArea;

            if ( largestArea <= area )
            {
                largestArea = area;

                invArea = 1.0f / area;
                Vec3Scale( normal, invArea, groupNormal[groupIndex] );

                Vec3Add( v0->xyz, v1->xyz, groupPos[groupIndex] );
                Vec3Add( v2->xyz, groupPos[groupIndex], groupPos[groupIndex] );
                Vec3Scale( groupPos[groupIndex], 0.33333334f, groupPos[groupIndex] );
            }
        }

        Assert( groupArea > 0.0f );

        if ( groupArea < warnLayerArea )
        {
            Tris_LayeredMaterialName( tris->lyrMtlDesc, name );
            MapError( 0, groupPos[groupIndex], groupNormal[groupIndex], 0, -1, -1,
                      "little area used by layered material combination %s", name );
        }
    }

    tris->lyrMtlDesc->totalArea += totalArea;
    tris->lyrMtlDesc->useCount  += groupCount;

    if ( tris->lyrMtlDesc->useCount >= warnLayerUses )
        Tris_FreeLayeredMaterialRefPoints( tris->lyrMtlDesc );
    else
        Tris_AddLayeredMaterialRefPoints( tris->lyrMtlDesc, groupPos, groupNormal, groupCount );
}


/* Tris_MarkCellCullGroups_r  0x004467b0 */
static void Tris_MarkCellCullGroups_r( winding_t *w, const vec4_t plane, const Tri_t *tri,
                                       Node_t *node )
{
    const plane_t *nodePlane;
    winding_t     *front;
    winding_t     *back;
    int            bitIndex;

    if ( !w )
        return;

    if ( node->cellnum != CELLNUM_UNKNOWN )
    {
        if ( node->cellnum >= 0 )
        {
            bitIndex = ( tri->visGroupIndex - numCells ) + node->cellnum * MAX_MAP_CULLGROUPS;
            s_cellCullGroupBits[bitIndex >> 3] |= 1 << ( bitIndex & 7 );
        }

        FreeWinding( w );
        return;
    }

    nodePlane = &mapplanes[node->planenum];

    if ( Vec3Dot( plane, nodePlane->normal ) > 0.999f
         && fabs( plane[3] - nodePlane->dist ) < 0.001f )
    {
        Tris_MarkCellCullGroups_r( w, plane, tri, node->children[0] );
    }
    else if ( Vec3Dot( plane, nodePlane->normal ) < -0.999f
              && fabs( plane[3] + nodePlane->dist ) < 0.001f )
    {
        Tris_MarkCellCullGroups_r( w, plane, tri, node->children[1] );
    }
    else
    {
        ClipWindingEpsilon( w, nodePlane->normal, nodePlane->dist, 0.1f, &front, &back, 0 );
        Tris_MarkCellCullGroups_r( front, plane, tri, node->children[0] );
        Tris_MarkCellCullGroups_r( back,  plane, tri, node->children[1] );
        FreeWinding( w );
    }
}


/* Tris_EmitTriangleGroup  0x00446110 */
void Tris_EmitTriangleGroup( TrisContext_t *context, Tri_t *tris, int triCount, Tree_t *tree )
{
    TangentSources_t sources;
    TrisSoup_t       soup;
    Tri_t           *firstTri;
    Tri_t           *tri;
    int              i;
    int              end;
    int              layer;
    unsigned int     layerIndex;
    int              cullGroupIndex;
    int              surfaceFlags;
    int              contentFlags;
    winding_t       *w;
    vec4_t           plane;

    sources.posStride    = sizeof( BspDrawVert_t );
    sources.normalStride = sizeof( BspDrawVert_t );

    if ( (unsigned int)( triCount * 3 + context->indexCount ) > (unsigned int)context->maxIndexCount )
    {
        Com_Error( "MAX_MAP_DRAW_INDICES (%i) exceeded for %s geometry",
                   context->maxIndexCount, context->name );
    }

    for ( i = 0; i < triCount; i = end )
    {
        firstTri = &tris[i];

        Assert( firstTri->reflectionProbeIndex != REFLECTION_PROBE_INVALID );

        surfaceFlags = firstTri->lyrMtlDesc->mtlRaw[0]->surfaceFlags;
        contentFlags = firstTri->lyrMtlDesc->mtlRaw[0]->contentFlags;
        (void)surfaceFlags; (void)contentFlags;

        soup.materialIndex = firstTri->lyrMtlDesc->mtlIndex;

        soup.reflectionProbeIndex = firstTri->reflectionProbeIndex;
        SanityCheck( soup.reflectionProbeIndex == firstTri->reflectionProbeIndex );

        soup.primaryLightIndex = firstTri->primaryLightIndex;
        SanityCheck( soup.primaryLightIndex == firstTri->primaryLightIndex );

        soup.lmapIndex  = firstTri->lmapIndex;
        soup.lyrMtlDesc = firstTri->lyrMtlDesc;
        soup.firstVert  = context->vertCount;
        soup.vertCount  = 0;
        soup.firstIndex = context->indexCount;
        soup.indexCount = 0;

        soup.castsSunShadow = firstTri->castsSunShadow;

        for ( end = i; end < triCount && tris[end].triGroupId == firstTri->triGroupId; end++ )
        {
            tri = &tris[end];

            if ( soup.castsSunShadow < tri->castsSunShadow )
                soup.castsSunShadow = tri->castsSunShadow;

            context->indices[soup.firstIndex + soup.indexCount] =
                (unsigned short)Tris_EmitDrawVert( context, &soup,
                                                   &triGlobVerts[tri->vertIndices[0]] );
            soup.indexCount++;
            context->indices[soup.firstIndex + soup.indexCount] =
                (unsigned short)Tris_EmitDrawVert( context, &soup,
                                                   &triGlobVerts[tri->vertIndices[1]] );
            soup.indexCount++;
            context->indices[soup.firstIndex + soup.indexCount] =
                (unsigned short)Tris_EmitDrawVert( context, &soup,
                                                   &triGlobVerts[tri->vertIndices[2]] );
            soup.indexCount++;

            if ( entity_num != 0 )
                continue;

            cullGroupIndex = firstTri->visGroupIndex - numCells;

            if ( cullGroupIndex < 0 )
                continue;

            w = AllocWinding( 3 );
            w->ptCount = 3;
            Vec3Copy( triGlobVerts[tri->vertIndices[0]].xyz, w->pts[0] );
            Vec3Copy( triGlobVerts[tri->vertIndices[1]].xyz, w->pts[1] );
            Vec3Copy( triGlobVerts[tri->vertIndices[2]].xyz, w->pts[2] );

            AddPointToBounds( w->pts[0], context->cullGroups[cullGroupIndex].mins,
                              context->cullGroups[cullGroupIndex].maxs );
            AddPointToBounds( w->pts[1], context->cullGroups[cullGroupIndex].mins,
                              context->cullGroups[cullGroupIndex].maxs );
            AddPointToBounds( w->pts[2], context->cullGroups[cullGroupIndex].mins,
                              context->cullGroups[cullGroupIndex].maxs );

            PlaneFromPoints( plane, w->pts[0], w->pts[1], w->pts[2] );
            Tris_MarkCellCullGroups_r( w, plane, tri, tree->headnode );
        }

        Assert( soup.vertCount > 0 );

        soup.vertCount = Tris_WeldSoupVerts( context, soup.firstVert, soup.vertCount,
                                             &context->indices[soup.firstIndex],
                                             soup.indexCount, firstTri, firstTri->lmapIndex );

        Assert( soup.vertCount > 0 );

        sources.pos    = (char *)&context->verts[soup.firstVert].xyz;
        sources.normal = (char *)&context->verts[soup.firstVert].normal;

        if ( !firstTri->lyrMtlDesc->normalMapCount )
        {
            sources.uvStride        = sizeof( BspDrawVert_t );
            sources.tangentStride   = sizeof( BspDrawVert_t );
            sources.bitangentStride = sizeof( BspDrawVert_t );

            sources.uv        = (char *)&context->verts[soup.firstVert].texCoord;
            sources.tangent   = (char *)&context->verts[soup.firstVert].binormal;
            sources.bitangent = (char *)&context->verts[soup.firstVert].tangent;

            TangentSpaceGenerate( &sources, soup.vertCount,
                                  &context->indices[soup.firstIndex], soup.indexCount );
        }
        else
        {
            layerIndex = firstTri->lyrMtlDesc->region[0];

            if ( layerIndex == 0 )
            {
                sources.uv       = (char *)&context->verts[soup.firstVert].texCoord;
                sources.uvStride = sizeof( BspDrawVert_t );
            }
            else
            {
                sources.uv       = (char *)( context->vertLayerData
                                             + soup.firstVert * TRIS_VERT_LAYER_DATA_STRIDE
                                             - 8 + layerIndex * 8 );
                sources.uvStride = TRIS_VERT_LAYER_DATA_STRIDE;
            }

            sources.tangent         = (char *)&context->verts[soup.firstVert].binormal;
            sources.bitangent       = (char *)&context->verts[soup.firstVert].tangent;
            sources.tangentStride   = sizeof( BspDrawVert_t );
            sources.bitangentStride = sizeof( BspDrawVert_t );

            TangentSpaceGenerate( &sources, soup.vertCount,
                                  &context->indices[soup.firstIndex], soup.indexCount );

            sources.uvStride        = TRIS_VERT_LAYER_DATA_STRIDE;
            sources.tangentStride   = TRIS_VERT_LAYER_DATA_STRIDE;
            sources.bitangentStride = TRIS_VERT_LAYER_DATA_STRIDE;

            for ( layer = 1; layer < (int)firstTri->lyrMtlDesc->normalMapCount; layer++ )
            {
                layerIndex = firstTri->lyrMtlDesc->region[layer];

                Assert( layerIndex > 0 );

                sources.uv        = (char *)( context->vertLayerData
                                              + soup.firstVert * TRIS_VERT_LAYER_DATA_STRIDE
                                              - 8 + layerIndex * 8 );
                sources.tangent   = (char *)( context->vertLayerData
                                              + soup.firstVert * TRIS_VERT_LAYER_DATA_STRIDE
                                              + 0x38 + ( layer - 1 ) * 0xc );
                sources.bitangent = (char *)( context->vertLayerData
                                              + soup.firstVert * TRIS_VERT_LAYER_DATA_STRIDE
                                              + 0x20 + ( layer - 1 ) * 0xc );

                TangentSpaceGenerate( &sources, soup.vertCount,
                                      &context->indices[soup.firstIndex], soup.indexCount );
            }
        }

        Tris_PartitionTriSoup( context, &soup, true );
    }
}


/* Tris_EmitDrawVert  0x00446980 */
int Tris_EmitDrawVert( TrisContext_t *context, TrisSoup_t *soup, const TriVert_t *src )
{
    int vertIndex;
    int globalIndex;

    vertIndex   = soup->vertCount;
    globalIndex = soup->firstVert + vertIndex;

    if ( globalIndex == context->maxVertCount )
    {
        Com_Error( "MAX_MAP_DRAW_VERTS (%i) exceeded for %s geometry\n",
                   context->maxVertCount, context->name );
    }

    soup->vertCount++;
    Tris_CopyVertexToDrawVert( &context->verts[globalIndex],
                               context->vertLayerData + globalIndex * TRIS_VERT_LAYER_DATA_STRIDE,
                               src, soup->lyrMtlDesc );
    return vertIndex;
}


/* Tris_CopyVertexToDrawVert  0x00446a10 */
void Tris_CopyVertexToDrawVert( BspDrawVert_t *dst, byte *layerData, const TriVert_t *src,
                                const LayeredMaterialDesc_t *lyrMtlDesc )
{
    unsigned int layer;
    int          layerCount;

    layerCount = LayeredMaterialLayerCount( lyrMtlDesc );

    AssertRange( layerCount, 1, MTL_LAYER_LIMIT );

    Vec3Copy( src->xyz,          dst->xyz );
    Vec3Copy( src->smoothNormal, dst->normal );
    Vec2Copy( src->texCoord[0],  dst->texCoord );
    Vec2Copy( src->lmapCoord,    dst->lmapCoord );
    Byte4Copy( src->color, dst->color );

    for ( layer = 1; layer < (unsigned int)layerCount; layer++ )
        Vec2Copy( src->texCoord[layer], (float *)( layerData + ( layer - 1 ) * 8 ) );
}


/* Tris_TexCoordsClose  0x00447290 */
static bool Tris_TexCoordsClose( const vec2_t uv0, const vec2_t uv1,
                                 unsigned int width, unsigned int height )
{
    vec2_t delta;

    Vec2Sub( uv1, uv0, delta );

    delta[0] = (float)width  * delta[0];
    delta[1] = (float)height * delta[1];

    return Vec2LengthSq( delta ) <= 0.25f;
}


/* Tris_TexCoordsCloseForMaterial  0x004481b0 */
static bool Tris_TexCoordsCloseForMaterial( const vec2_t uv0, const vec2_t uv1,
                                            const Material_t *material )
{
    return Tris_TexCoordsClose( uv0, uv1, material->textureWidth, material->textureHeight );
}


/* Tris_VertsAreMergeable  0x00447ed0 */
static bool Tris_VertsAreMergeable( const TrisContext_t *context, int vertA, int vertB,
                                    const Tri_t *triA, const Tri_t *triB, int lmapIndex )
{
    const BspDrawVert_t *a;
    const BspDrawVert_t *b;
    unsigned int         layer;
    unsigned int         region;
    const vec3_t        *vecsA;
    const vec3_t        *vecsB;

    if ( triA->lyrMtlDesc->layerCount != triB->lyrMtlDesc->layerCount )
        return false;

    if ( triA->lyrMtlDesc->normalMapCount != triB->lyrMtlDesc->normalMapCount )
        return false;

    a = &context->verts[vertA];
    b = &context->verts[vertB];

    if ( !Vec3Equal( a->xyz, b->xyz ) )
        return false;

    if ( Vec3Dot( a->normal, b->normal ) < 0.999f )
        return false;

    if ( !Tris_TexCoordsCloseForMaterial( a->texCoord, b->texCoord, triA->lyrMtlDesc->mtlRaw[0] ) )
        return false;

    if ( lmapIndex != LIGHTMAP_NONE
         && !Tris_TexCoordsClose( a->lmapCoord, b->lmapCoord, 0x200, 0x200 ) )
        return false;

    if ( a->color[0] != b->color[0] || a->color[1] != b->color[1]
         || a->color[2] != b->color[2] || a->color[3] != b->color[3] )
        return false;

    if ( triA->lyrMtlDesc->layerCount > 1 )
    {
        Assert( context->vertLayerData );

        for ( layer = 1; layer < triA->lyrMtlDesc->layerCount; layer++ )
        {
            if ( !Tris_TexCoordsCloseForMaterial(
                     (const float *)( context->vertLayerData
                                      + vertA * TRIS_VERT_LAYER_DATA_STRIDE - 8 + layer * 8 ),
                     (const float *)( context->vertLayerData
                                      + vertB * TRIS_VERT_LAYER_DATA_STRIDE - 8 + layer * 8 ),
                     triA->lyrMtlDesc->mtlRaw[layer] ) )
                return false;
        }
    }

    if ( triA != triB )
    {
        for ( region = 0; region < triA->lyrMtlDesc->normalMapCount; region++ )
        {
            vecsA = triA->texVecs[triA->lyrMtlDesc->region[region]];
            vecsB = triB->texVecs[triB->lyrMtlDesc->region[region]];

            if ( Vec3Dot( vecsA[0], vecsB[0] ) <= 0.0f )
            {
                g_unmergedVertCount++;
                return false;
            }

            if ( Vec3Dot( vecsA[1], vecsB[1] ) <= 0.0f )
            {
                g_unmergedVertCount++;
                return false;
            }
        }
    }

    return true;
}


/* Tris_MergeDuplicateVerts  0x00447cc0 */
int Tris_MergeDuplicateVerts( TrisContext_t *context, int firstVert, int vertCount,
                              unsigned short *indices, int indexCount,
                              Tri_t *firstTri, int lmapIndex )
{
    unsigned int   triOfVert[MAX_MERGE_DUP_VERTS + 1];
    unsigned short vertIndex;
    unsigned short otherIndex;
    unsigned short triIndex;
    int            i;

    if ( (unsigned int)vertCount > MAX_MERGE_DUP_VERTS )
    {
        Com_Error( "MergeDuplicateVerts: %i vertices > %i\n", vertCount, MAX_MERGE_DUP_VERTS );
    }

    triIndex = 0;

    for ( vertIndex = 0; vertIndex < (unsigned int)vertCount; vertIndex = (unsigned short)( vertIndex + 3 ) )
    {
        triOfVert[vertIndex]     = triIndex;
        triOfVert[vertIndex + 1] = triIndex;
        triOfVert[vertIndex + 2] = triIndex;
        triIndex++;
    }

    for ( vertIndex = 1; vertIndex < (unsigned int)vertCount; )
    {
        for ( otherIndex = 0; otherIndex < vertIndex; otherIndex++ )
        {
            if ( Tris_VertsAreMergeable( context, otherIndex + firstVert, vertIndex + firstVert,
                                         &firstTri[triOfVert[otherIndex]],
                                         &firstTri[triOfVert[vertIndex]], lmapIndex ) )
                break;
        }

        if ( otherIndex == vertIndex )
        {
            vertIndex++;
            continue;
        }

        vertCount--;

        context->verts[vertIndex + firstVert] = context->verts[vertCount + firstVert];

        if ( context->vertLayerData )
        {
            memcpy( context->vertLayerData + ( vertIndex + firstVert ) * TRIS_VERT_LAYER_DATA_STRIDE,
                    context->vertLayerData + ( vertCount + firstVert ) * TRIS_VERT_LAYER_DATA_STRIDE,
                    TRIS_VERT_LAYER_DATA_STRIDE );
        }

        triOfVert[vertIndex] = triOfVert[vertCount];

        for ( i = 0; i < indexCount; i++ )
        {
            if ( indices[i] == vertIndex )
                indices[i] = otherIndex;
            else if ( indices[i] == vertCount )
                indices[i] = vertIndex;
        }
    }

    return vertCount;
}


/* Tris_WeldSoupVerts  0x00446b00 */
int Tris_WeldSoupVerts( TrisContext_t *context, int firstVert, int vertCount,
                        unsigned short *indices, int indexCount, Tri_t *firstTri, int lmapIndex )
{
    if ( Tris_LayeredMaterialAllNoTile( firstTri->lyrMtlDesc ) )
    {
        Tris_CanonicalizeIslandTexCoords( context, firstVert, vertCount, indices, indexCount,
                                          firstTri->lyrMtlDesc, lmapIndex );
        Tris_ClampTexCoordsToLimit( context, firstVert, vertCount, firstTri->lyrMtlDesc );
    }

    return Tris_MergeDuplicateVerts( context, firstVert, vertCount, indices, indexCount,
                                     firstTri, lmapIndex );
}


/* Tris_TrisShareEdge  0x004488f0 */
static bool Tris_TrisShareEdge( const TrisContext_t *context, const TrisSoup_t *soup,
                                int firstIndex, int secondIndex )
{
    const BspDrawVert_t  *verts;
    const unsigned short *indices;
    unsigned int          a;
    unsigned int          aPrev;
    unsigned int          b;
    unsigned int          bPrev;

    verts   = &context->verts[soup->firstVert];
    indices = &context->indices[soup->firstIndex];

    aPrev = 2;

    for ( a = 0; a < 3; a++ )
    {
        bPrev = 2;

        for ( b = 0; b < 3; b++ )
        {
            if ( Vec3Compare( verts[indices[firstIndex + aPrev]].xyz,
                              verts[indices[secondIndex + b]].xyz )
              && Vec3Compare( verts[indices[firstIndex + a]].xyz,
                              verts[indices[secondIndex + bPrev]].xyz ) )
                return true;

            bPrev = b;
        }

        aPrev = a;
    }

    return false;
}


/* Tris_RenumberTriGroup  0x004489f0 */
static void Tris_RenumberTriGroup( short *groupIds, unsigned int count, short from, short to )
{
    unsigned int i;

    for ( i = 0; i < count; i++ )
    {
        if ( groupIds[i] == from )
            groupIds[i] = to;
    }
}


/* Tris_GroupSoupTriangles  0x004487b0 */
static int Tris_GroupSoupTriangles( const TrisContext_t *context, const TrisSoup_t *soup,
                                    short *groupIds )
{
    unsigned int i;
    unsigned int j;
    int          groupCount;
    short        nextGroupId;

    nextGroupId = 1;
    groupCount  = 0;

    for ( i = 0; i < (unsigned int)soup->indexCount / 3; i++ )
    {
        groupIds[i] = 0;

        for ( j = 0; j < i; j++ )
        {
            if ( groupIds[i] != groupIds[j]
                 && Tris_TrisShareEdge( context, soup, i * 3, j * 3 ) )
            {
                if ( groupIds[i] != 0 )
                {
                    Tris_RenumberTriGroup( groupIds, i, groupIds[i], groupIds[j] );
                    groupCount--;
                }

                groupIds[i] = groupIds[j];
            }
        }

        if ( groupIds[i] == 0 )
        {
            groupIds[i] = nextGroupId;
            nextGroupId++;
            groupCount++;
        }
    }

    return groupCount;
}


/* Tris_PartitionSoupByGroup  0x004483b0 */
static bool Tris_PartitionSoupByGroup( TrisContext_t *context, TrisSoup_t *soup )
{
    short          *groupIds;
    BspDrawVert_t  *savedVerts;
    byte           *savedLayerData;
    unsigned short *savedIndices;
    unsigned short *vertMap;
    TrisSoup_t      subSoup;
    int             groupCount;
    unsigned int    indexCount;
    unsigned int    groupId;
    unsigned int    tri;
    unsigned int    corner;
    unsigned int    vertIndex;

    groupIds   = new short[soup->indexCount / 3];
    groupCount = Tris_GroupSoupTriangles( context, soup, groupIds );

    Assert( groupCount >= 1 );

    if ( groupCount == 1 )
    {
        delete [] groupIds;
        return false;
    }

    savedVerts = new BspDrawVert_t[soup->vertCount];
    memcpy( savedVerts, &context->verts[soup->firstVert],
            soup->vertCount * sizeof( BspDrawVert_t ) );

    if ( !context->vertLayerData )
    {
        savedLayerData = NULL;
    }
    else
    {
        savedLayerData = new byte[soup->vertCount * TRIS_VERT_LAYER_DATA_STRIDE];
        memcpy( savedLayerData,
                context->vertLayerData + soup->firstVert * TRIS_VERT_LAYER_DATA_STRIDE,
                soup->vertCount * TRIS_VERT_LAYER_DATA_STRIDE );
    }

    savedIndices = new unsigned short[soup->indexCount];
    memcpy( savedIndices, &context->indices[soup->firstIndex],
            soup->indexCount * sizeof( unsigned short ) );

    vertMap = new unsigned short[soup->indexCount];

    indexCount = soup->indexCount;
    groupId    = 0;

    while ( groupCount != 0 )
    {
        subSoup.materialIndex        = soup->materialIndex;
        subSoup.reflectionProbeIndex = soup->reflectionProbeIndex;
        subSoup.primaryLightIndex    = soup->primaryLightIndex;
        subSoup.lmapIndex            = soup->lmapIndex;
        subSoup.castsSunShadow       = soup->castsSunShadow;
        subSoup.firstIndex           = context->indexCount;
        subSoup.indexCount           = 0;
        subSoup.firstVert            = context->vertCount;
        subSoup.vertCount            = 0;

        memset( vertMap, -1, indexCount * sizeof( unsigned short ) );

        while ( subSoup.indexCount == 0 )
        {
            groupId++;

            for ( tri = 0; tri < indexCount / 3; tri++ )
            {
                if ( groupIds[tri] != (short)groupId )
                    continue;

                for ( corner = 0; corner < 3; corner++ )
                {
                    vertIndex = savedIndices[tri * 3 + corner];

                    if ( vertMap[vertIndex] >= (unsigned int)subSoup.indexCount )
                    {
                        context->verts[subSoup.firstVert + subSoup.vertCount] =
                            savedVerts[vertIndex];

                        if ( savedLayerData )
                        {
                            memcpy( context->vertLayerData
                                    + ( subSoup.firstVert + subSoup.vertCount )
                                      * TRIS_VERT_LAYER_DATA_STRIDE,
                                    savedLayerData + vertIndex * TRIS_VERT_LAYER_DATA_STRIDE,
                                    TRIS_VERT_LAYER_DATA_STRIDE );
                        }

                        vertMap[vertIndex] = (unsigned short)subSoup.vertCount;
                        Assertx( vertMap[vertIndex] == subSoup.vertCount,
                                 "indexMap[vertIndex] == subSoup.vertCount\n\t%i, %i",
                                 vertMap[vertIndex], subSoup.vertCount );
                        subSoup.vertCount++;
                    }

                    context->indices[subSoup.firstIndex + subSoup.indexCount] =
                        vertMap[vertIndex];
                    subSoup.indexCount++;
                }
            }
        }

        groupCount--;
        Tris_PartitionTriSoup( context, &subSoup, false );
    }

    delete [] vertMap;
    delete [] savedIndices;
    delete [] savedVerts;
    delete [] groupIds;
    return true;
}


/* Tris_MarkSoupSideFlags  0x00448f60 */
static void Tris_MarkSoupSideFlags( const TrisContext_t *context, const TrisSoup_t *soup,
                                    int axis, float dist, byte *isLess, byte *isGreater )
{
    unsigned int i;
    unsigned int corner;
    unsigned int vertIndex;
    unsigned int lessCount;
    unsigned int greaterCount;
    byte         areAnyLess;
    byte         areAnyGreater;
    float        d;

    memset( isLess,    0, soup->vertCount );
    memset( isGreater, 0, soup->vertCount );

    lessCount    = 0;
    greaterCount = 0;

    for ( i = 0; i < (unsigned int)soup->indexCount; i += 3 )
    {
        areAnyLess    = 0;
        areAnyGreater = 0;

        for ( corner = 0; corner < 3; corner++ )
        {
            vertIndex = context->indices[soup->firstIndex + i + corner];

            Assertx( vertIndex < (unsigned int)soup->vertCount,
                     "%s\n\t(vertIndex) = %i",
                     "(vertIndex >= 0 && vertIndex < soup->vertCount)", vertIndex );

            d = context->verts[soup->firstVert + vertIndex].xyz[axis] - dist;

            if ( d < -0.001f )
                areAnyLess = 1;
            else if ( d > 0.001f )
                areAnyGreater = 1;
        }

        if ( !areAnyLess && !areAnyGreater )
        {
            if ( lessCount < greaterCount )
            {
                areAnyLess    = 1;
                areAnyGreater = 0;
            }
            else
            {
                areAnyLess    = 0;
                areAnyGreater = 1;
            }
        }

        Assert( areAnyLess || areAnyGreater );

        lessCount    += areAnyLess;
        greaterCount += areAnyGreater;

        for ( corner = 0; corner < 3; corner++ )
        {
            vertIndex = context->indices[soup->firstIndex + i + corner];
            isLess[vertIndex]    |= areAnyLess;
            isGreater[vertIndex] |= areAnyGreater;
        }
    }
}


/* Tris_MapSplitVert  0x004493b0 */
static unsigned short Tris_MapSplitVert( unsigned short vertIndex, int side,
                                         unsigned short **vertMap,
                                         const BspDrawVert_t *srcVerts,
                                         const byte *srcLayerData,
                                         BspDrawVert_t **dstVerts, byte **dstLayerData,
                                         int *vertCount )
{
    if ( vertMap[side][vertIndex] >= (unsigned int)vertCount[side] )
    {
        vertMap[side][vertIndex] = (unsigned short)vertCount[side];

        dstVerts[side][vertCount[side]] = srcVerts[vertIndex];

        if ( srcLayerData )
        {
            memcpy( dstLayerData[side] + vertCount[side] * TRIS_VERT_LAYER_DATA_STRIDE,
                    srcLayerData + vertIndex * TRIS_VERT_LAYER_DATA_STRIDE,
                    TRIS_VERT_LAYER_DATA_STRIDE );
        }

        vertCount[side]++;
    }

    return vertMap[side][vertIndex];
}


/* Tris_SplitSoupIndices  0x004491a0 */
static void Tris_SplitSoupIndices( const TrisContext_t *context, const TrisSoup_t *soup,
                                   const byte *isLess, const byte *isGreater,
                                   unsigned short **vertMap,
                                   unsigned short **indexBuf, int *indexCount,
                                   BspDrawVert_t **dstVerts, byte **dstLayerData,
                                   int *vertCount )
{
    const BspDrawVert_t *srcVerts;
    const byte          *srcLayerData;
    unsigned int         i;
    unsigned int         corner;
    bool                 areAllLess;
    byte                 side;
    unsigned short       newIndex;

    srcVerts = &context->verts[soup->firstVert];

    if ( !context->vertLayerData )
        srcLayerData = NULL;
    else
        srcLayerData = context->vertLayerData + soup->firstVert * TRIS_VERT_LAYER_DATA_STRIDE;

    if ( !dstVerts )
    {
        vertCount[0] = soup->vertCount;
        vertCount[1] = soup->vertCount;
    }

    indexCount[0] = 0;
    indexCount[1] = 0;

    for ( i = 0; i < (unsigned int)soup->indexCount; i += 3 )
    {
        areAllLess = true;
        side       = 1;

        for ( corner = 0; corner < 3; corner++ )
        {
            if ( !isLess[context->indices[soup->firstIndex + i + corner]] )
                areAllLess = false;

            if ( !isGreater[context->indices[soup->firstIndex + i + corner]] )
                side = 0;
        }

        Assert( areAllLess || side );

        if ( areAllLess && side && indexCount[0] < indexCount[1] )
            side = 0;

        for ( corner = 0; corner < 3; corner++ )
        {
            newIndex = context->indices[soup->firstIndex + i + corner];

            if ( dstVerts )
            {
                newIndex = Tris_MapSplitVert( newIndex, side, vertMap, srcVerts, srcLayerData,
                                              dstVerts, dstLayerData, vertCount );
            }

            indexBuf[side][indexCount[side]] = newIndex;
            indexCount[side]++;
        }
    }
}


/* Tris_PartitionSoupOnAxis  0x00448a40 */
static bool Tris_PartitionSoupOnAxis( TrisContext_t *context, TrisSoup_t *soup,
                                      int axis, float dist )
{
    byte           *layerBuf[2];
    BspDrawVert_t  *vertBuf[2];
    int             indexCount[2];
    unsigned short *indexBuf[2];
    unsigned short *vertMap[2];
    byte           *isLess;
    byte           *isGreater;
    int             vertCount[2];
    TrisSoup_t      subSoup;
    unsigned int    side;
    bool            vertsFit;
    bool            didSplit;

    vertsFit = ( (unsigned int)soup->vertCount < MAX_TRI_SOUP_VERTS );

    for ( side = 0; side < 2; side++ )
    {
        vertBuf[side]  = vertsFit ? NULL : new BspDrawVert_t[soup->vertCount];
        layerBuf[side] = vertsFit ? NULL
                                  : new byte[soup->vertCount * TRIS_VERT_LAYER_DATA_STRIDE];
        indexBuf[side]  = new unsigned short[soup->indexCount];
        vertMap[side]   = new unsigned short[soup->vertCount];
        vertCount[side]  = 0;
        indexCount[side] = 0;
    }

    isLess    = new byte[soup->vertCount];
    isGreater = new byte[soup->vertCount];

    Tris_MarkSoupSideFlags( context, soup, axis, dist, isLess, isGreater );

    memset( vertMap[0], -1, soup->vertCount * sizeof( unsigned short ) );
    memset( vertMap[1], -1, soup->vertCount * sizeof( unsigned short ) );
    vertCount[0] = 0;
    vertCount[1] = 0;

    if ( vertsFit )
    {
        Tris_SplitSoupIndices( context, soup, isLess, isGreater, vertMap,
                               indexBuf, indexCount, NULL, NULL, vertCount );
    }
    else
    {
        Tris_SplitSoupIndices( context, soup, isLess, isGreater, vertMap,
                               indexBuf, indexCount, vertBuf, layerBuf, vertCount );
    }

    didSplit = ( indexCount[0] != 0 && indexCount[1] != 0 );

    if ( didSplit )
    {
        Assert( vertCount[0] );
        Assert( vertCount[1] );
        Assert( (unsigned int)( vertCount[0] + vertCount[1] )
                >= (unsigned int)soup->vertCount );

        subSoup.materialIndex        = soup->materialIndex;
        subSoup.reflectionProbeIndex = soup->reflectionProbeIndex;
        subSoup.primaryLightIndex    = soup->primaryLightIndex;
        subSoup.lmapIndex            = soup->lmapIndex;
        subSoup.castsSunShadow       = soup->castsSunShadow;

        for ( side = 0; side < 2; side++ )
        {
            if ( vertsFit )
            {
                subSoup.firstVert = soup->firstVert;
                subSoup.vertCount = soup->vertCount;
            }
            else
            {
                subSoup.firstVert = context->vertCount;
                subSoup.vertCount = vertCount[side];

                memcpy( &context->verts[subSoup.firstVert], vertBuf[side],
                        subSoup.vertCount * sizeof( BspDrawVert_t ) );

                if ( context->vertLayerData )
                {
                    memcpy( context->vertLayerData
                            + subSoup.firstVert * TRIS_VERT_LAYER_DATA_STRIDE,
                            layerBuf[side],
                            subSoup.vertCount * TRIS_VERT_LAYER_DATA_STRIDE );
                }
            }

            subSoup.firstIndex = context->indexCount;
            subSoup.indexCount = indexCount[side];

            memcpy( &context->indices[subSoup.firstIndex], indexBuf[side],
                    subSoup.indexCount * sizeof( unsigned short ) );

            Tris_PartitionTriSoup( context, &subSoup, false );

            SanityCheck( (unsigned int)context->vertCount
                         >= (unsigned int)( subSoup.firstVert + subSoup.vertCount ) );
            SanityCheck( context->indexCount
                         == subSoup.firstIndex + subSoup.indexCount );
        }
    }

    delete [] isGreater;
    delete [] isLess;

    for ( side = 0; side < 2; side++ )
    {
        delete [] vertMap[side];
        delete [] indexBuf[side];
        delete [] layerBuf[side];
        delete [] vertBuf[side];
    }

    return didSplit;
}


/* Tris_SortAxesByExtent  0x00449710 */
static void Tris_SortAxesByExtent( const vec3_t extents, int order[3] )
{
    if ( extents[1] < extents[0] )
    {
        if ( extents[2] < extents[0] )
        {
            if ( extents[2] < extents[1] )
            {
                order[0] = 2; order[1] = 1; order[2] = 0;
            }
            else
            {
                order[0] = 1; order[1] = 2; order[2] = 0;
            }
        }
        else
        {
            order[0] = 1; order[1] = 0; order[2] = 2;
        }
    }
    else if ( extents[2] < extents[1] )
    {
        if ( extents[2] < extents[0] )
        {
            order[0] = 2; order[1] = 0; order[2] = 1;
        }
        else
        {
            order[0] = 0; order[1] = 2; order[2] = 1;
        }
    }
    else
    {
        order[0] = 0; order[1] = 1; order[2] = 2;
    }
}


/* Tris_PartitionTriSoup  0x00448230 */
void Tris_PartitionTriSoup( TrisContext_t *context, TrisSoup_t *soup, bool allowGroupSplit )
{
    vec3_t mins;
    vec3_t maxs;
    vec3_t extents;
    int    axisOrder[3];
    unsigned int i;
    unsigned int axisIndex;
    int    axis;

    if ( (unsigned int)soup->vertCount  < MAX_TRI_SOUP_VERTS
      && (unsigned int)soup->indexCount < MAX_TRI_SOUP_INDICES )
    {
        Tris_EmitTriSoupRecord( context, soup );
        return;
    }

    if ( allowGroupSplit && Tris_PartitionSoupByGroup( context, soup ) )
        return;

    ClearBounds( mins, maxs );

    for ( i = 0; i < (unsigned int)soup->indexCount; i++ )
    {
        AddPointToBounds( context->verts[soup->firstVert
                                         + context->indices[soup->firstIndex + i]].xyz,
                          mins, maxs );
    }

    Vec3Sub( maxs, mins, extents );
    Tris_SortAxesByExtent( extents, axisOrder );

    for ( axisIndex = 0; axisIndex < 3; axisIndex++ )
    {
        axis = axisOrder[axisIndex];

        if ( Tris_PartitionSoupOnAxis( context, soup, axis,
                                       ( maxs[axis] + mins[axis] ) * 0.5f ) )
            return;
    }

    if ( (unsigned int)soup->vertCount  > MAX_TRI_SOUP_VERTS - 1
      || (unsigned int)soup->indexCount > 0x7fc0 )
    {
        Com_Error( "couldn't partition large draw surface on any axis" );
    }

    Tris_EmitTriSoupRecord( context, soup );
}


/* Tris_EmitTriSoupRecord  0x00449470 */
void Tris_EmitTriSoupRecord( TrisContext_t *context, const TrisSoup_t *soup )
{
    BspTriSoup_t *tris;

    Assert( soup->vertCount );
    Assert( soup->indexCount );
    Assertx( ( soup->primaryLightIndex >= 0 &&
               soup->primaryLightIndex < numBSPPrimaryLights ) ||
             soup->primaryLightIndex == 0,
             "(soup->primaryLightIndex) = %i", soup->primaryLightIndex );

    if ( context->triCount == MAX_MAP_TRISOUPS )
        Com_Error( "MAX_MAP_TRIANGLE_SURFS" );

    tris = &context->tris[context->triCount];
    context->triCount++;

    tris->materialIndex = soup->materialIndex;

    tris->reflectionProbeIndex = soup->reflectionProbeIndex;
    SanityCheckx( tris->reflectionProbeIndex == soup->reflectionProbeIndex,
                  "tris->reflectionProbeIndex == soup->reflectionProbeIndex\n\t%i, %i",
                  tris->reflectionProbeIndex, soup->reflectionProbeIndex );

    tris->primaryLightIndex = soup->primaryLightIndex;
    SanityCheckx( tris->primaryLightIndex == soup->primaryLightIndex,
                  "tris->primaryLightIndex == soup->primaryLightIndex\n\t%i, %i",
                  tris->primaryLightIndex, soup->primaryLightIndex );

    tris->lightmapIndex = soup->lmapIndex;
    SanityCheckx( tris->lightmapIndex == soup->lmapIndex,
                  "tris->lightmapIndex == soup->lmapIndex\n\t%i, %i",
                  tris->lightmapIndex, soup->lmapIndex );

    tris->firstVertex = soup->firstVert;
    tris->vertexCount = checked_cast<unsigned short>( soup->vertCount );
    tris->indexCount  = checked_cast<unsigned short>( soup->indexCount );
    tris->firstIndex  = soup->firstIndex;

    tris->vertexLayerType = Tris_CastsSunShadow( soup->castsSunShadow );

    context->indexCount += tris->indexCount;

    SanityCheckx( context->vertCount == tris->firstVertex ||
                  context->vertCount == tris->firstVertex + tris->vertexCount,
                  "context->vertCount == tris->firstVertex || "
                  "context->vertCount == tris->firstVertex + tris->vertexCount\n\t%i, %i",
                  context->vertCount, tris->firstVertex );

    context->vertCount = tris->firstVertex + tris->vertexCount;

    if ( context->vertCount > context->maxVertCount )
    {
        Com_Error( "MAX_MAP_DRAW_VERTS (%i) exceeded for %s geometry\n",
                   context->maxVertCount, context->name );
    }
}


/* Tris_BuildAabbTree  0x00449840 */
void Tris_BuildAabbTree( TrisContext_t *context )
{
    vec3_t            stackMins[MAX_ENTITY_TRI_SOUPS];
    vec3_t            stackMaxs[MAX_ENTITY_TRI_SOUPS];
    AabbTreeBuilder_t builder;
    BspAabbTree_t    *node;
    AabbTreeNode_t   *nodes;
    unsigned int      soupIndex;
    int               vertIndex;
    int               nodeIndex;
    int               nodeCount;
    int               firstSurface;
    const BspTriSoup_t *soup;

    node = &context->aabbTrees[context->aabbTreeCount];

    if ( (unsigned int)node->surfaceCount <= MAX_AABB_LEAF_SURFACES )
    {
        node->childCount = 0;
        context->aabbTreeCount++;
        return;
    }

    builder.itemData      = &context->tris[node->firstSurface];
    builder.itemCount     = node->surfaceCount;
    builder.itemStride    = sizeof( BspTriSoup_t );
    builder.reorderBounds = 0;

    if ( (unsigned int)node->surfaceCount <= MAX_ENTITY_TRI_SOUPS )
    {
        builder.itemMins = stackMins;
        builder.itemMaxs = stackMaxs;
    }
    else
    {
        builder.itemMins = (vec3_t *)new float[node->surfaceCount * 3];
        builder.itemMaxs = (vec3_t *)new float[node->surfaceCount * 3];
    }

    nodes = (AabbTreeNode_t *)new byte[MAX_MAP_AABBTREES * sizeof( AabbTreeNode_t )];

    builder.nodes            = nodes;
    builder.maxNodes         = MAX_MAP_AABBTREES;
    builder.minPartitionSize = 4;
    builder.minLeafItems     = MAX_AABB_LEAF_SURFACES;

    for ( soupIndex = 0; soupIndex < (unsigned int)node->surfaceCount; soupIndex++ )
    {
        soup = &context->tris[node->firstSurface + soupIndex];

        ClearBounds( builder.itemMins[soupIndex], builder.itemMaxs[soupIndex] );

        for ( vertIndex = 0; vertIndex < (int)soup->vertexCount; vertIndex++ )
        {
            AddPointToBounds( context->verts[soup->firstVertex + vertIndex].xyz,
                              builder.itemMins[soupIndex], builder.itemMaxs[soupIndex] );
        }
    }

    nodeCount = AabbBuildTree( &builder );

    if ( (unsigned int)( nodeCount + context->aabbTreeCount ) > MAX_MAP_AABBTREES )
        Com_Error( "MAX_MAP_AABB_TREES (%i) exceeded\n", MAX_MAP_AABBTREES );

    firstSurface = node->firstSurface;

    for ( nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++ )
    {
        node = &context->aabbTrees[context->aabbTreeCount];
        context->aabbTreeCount++;

        node->firstSurface = nodes[nodeIndex].firstItem + firstSurface;
        node->surfaceCount = nodes[nodeIndex].itemCount;
        node->childCount   = nodes[nodeIndex].childCount;
    }

    delete [] (byte *)nodes;

    if ( builder.itemMins != stackMins )
    {
        delete [] (float *)builder.itemMins;
        delete [] (float *)builder.itemMaxs;
    }
}


/* Tris_CombineAllLayeredMaterials  0x00449bb0 */
void Tris_CombineAllLayeredMaterials( const int *indexMap )
{
    int i;
    int j;

    for ( i = 0; i < triGlobTriCount; i = j )
    {
        for ( j = i + 1; j < triGlobTriCount; j++ )
        {
            if ( triGlobTris[j].visGroupIndex != triGlobTris[i].visGroupIndex )
                break;
        }
        Tris_CombineLayeredMaterials( &triGlobTris[i], j - i, triGlobVerts, indexMap );
    }
}


/* Tris_FlushContextCounts  0x00449c40 */
void Tris_FlushContextCounts( const TrisContext_t *context )
{
    numBSPDrawVerts[context->type]   = context->diskVertCount;
    numBSPDrawIndices[context->type] = context->indexCount;
    numBSPTriSoups[context->type]    = context->triCount;
    numBSPAabbTrees[context->type]   = context->aabbTreeCount;

    if ( context->type == TRIS_TYPE_LAYERED )
        numBSPVertexLayerBytes = context->diskVertLayerDataUsed;
}


/* Tris_InitContexts  0x00449ca0 */
void Tris_InitContexts( void )
{
    Tris_InitSurfList( TRIS_TYPE_LAYERED,  "layered",   MAX_MAP_LAYERED_DRAWINDICES,
                       0x80000, s_layeredWorkVerts, s_layeredWorkLayerData,
                       bspVertexLayerData );
    Tris_InitSurfList( TRIS_TYPE_SIMPLE,   "unlayered", MAX_MAP_DRAWINDICES,
                       MAX_MAP_DRAWVERTS, s_simpleWorkVerts, NULL, NULL );
}


/* Tris_ResetContext  0x00449c40 */
void Tris_ResetContext( TrisContext_t *context )
{
    numBSPDrawVerts[context->type]   = context->diskVertCount;
    numBSPDrawIndices[context->type] = context->indexCount;
    numBSPTriSoups[context->type]    = context->triCount;
    numBSPAabbTrees[context->type]   = context->aabbTreeCount;

    if ( context->type == TRIS_TYPE_LAYERED )
        numBSPVertexLayerBytes = context->diskVertLayerDataUsed;
}


/* Tris_InitSurfList  0x00449cf0 */
void Tris_InitSurfList( int type, const char *name, int maxIndexCount, int maxVertCount,
                        BspDrawVert_t *workVerts, byte *workLayerData, byte *diskLayerData )
{
    TrisContext_t *context = &triGlobContext[type];

    context->type                  = type;
    context->name                  = name;
    context->maxVertCount          = maxVertCount;
    context->vertCount             = 0;
    context->verts                 = workVerts;
    context->vertLayerData         = workLayerData;
    context->diskVertCount         = 0;
    context->diskVerts             = bspDrawVerts[type];
    context->diskVertLayerDataUsed = 0;
    context->diskVertLayerData     = diskLayerData;
    context->maxIndexCount         = maxIndexCount;
    context->indexCount            = 0;
    context->indices               = bspDrawIndices[type];
    context->triCount              = 0;
    context->tris                  = bspTriSoups[type];
    context->aabbTreeCount         = 0;
    context->aabbTrees             = bspAabbTrees[type];
    context->cullGroups            = bspCullGroups[type];
}


/* Tris_FinishBSPTris  0x00449e50 */
void Tris_FinishBSPTris( void )
{
    Tris_ReorderTriSurfaces( &triGlobContext[TRIS_TYPE_LAYERED] );
    Tris_ReorderTriSurfaces( &triGlobContext[TRIS_TYPE_SIMPLE] );
    Tris_FlushContextCounts( &triGlobContext[TRIS_TYPE_LAYERED] );
    Tris_FlushContextCounts( &triGlobContext[TRIS_TYPE_SIMPLE] );
}


/* Tris_ReorderTriSurfaces  0x00449e90 */
void Tris_ReorderTriSurfaces( TrisContext_t *context )
{
    int                 remapTable[MAX_MAP_MATERIALS];
    TrisReorderEntry_t  reorderList[MAX_ENTITY_TRI_SOUPS];
    int                 materialIndex;
    int                 slot;
    int                 reorderCount;
    int                 entityIndex;
    int                 entityIndexNext;
    unsigned int        soupIndex;
    int                 i;
    BspTriSoup_t       *soup;

    Assertx( context->triCount <= MAX_ENTITY_TRI_SOUPS,
             "%s\n\t(context->triCount) = %i",
             "(context->triCount <= (32 * 1024))", context->triCount );

    reorderGlobNewVertIndex = new int[context->vertCount];
    reorderGlobRemap        = (int *)new byte[REORDER_INDEX_LIMIT * sizeof( int )];
    reorderGlobMapArray     = (int *)new byte[REORDER_INDEX_LIMIT * sizeof( int )];
    reorderGlobIndexBuf     = (int *)new byte[REORDER_INDEX_LIMIT * sizeof( int )];
    reorderGlobOptIndices   = (unsigned short *)new byte[REORDER_INDEX_LIMIT
                                                         * sizeof( unsigned short )];

    slot = 0;
    for ( materialIndex = 0; materialIndex < numBSPMaterials; materialIndex++ )
        slot = Tris_BuildMaterialRemapTable( remapTable, materialIndex, slot );

    slot = 0;

    for ( materialIndex = 0; materialIndex < numBSPMaterials; materialIndex++ )
    {
        if ( remapTable[materialIndex] != slot )
            continue;

        slot++;

        entityIndex     = 0;
        entityIndexNext = Tris_FindNextEntity( context, 0 );
        reorderCount    = 0;
        soup            = context->tris;

        for ( soupIndex = 0; soupIndex < (unsigned int)context->triCount; soupIndex++, soup++ )
        {
            if ( entityIndexNext != num_entities
                 && (unsigned int)entities[entityIndexNext].firstTriSoup[context->type] == soupIndex )
            {
                if ( reorderCount != 0 )
                {
                    Tris_ReorderTriSurfaceRun( context, entityIndex, reorderList, reorderCount );
                    reorderCount = 0;
                }

                entityIndex     = entityIndexNext;
                entityIndexNext = Tris_FindNextEntity( context, entityIndexNext );
            }

            if ( remapTable[soup->materialIndex] != remapTable[materialIndex] )
                continue;

            Assert( soup->indexCount > 0 );

            reorderList[reorderCount].soup       = soup;
            reorderList[reorderCount].lyrMtlDesc =
                Tris_FindLayeredMaterialByName( bspMaterials[soup->materialIndex].name );
            reorderList[reorderCount].firstVertex = context->indices[soup->firstIndex];
            reorderList[reorderCount].lastVertex  = context->indices[soup->firstIndex];

            for ( i = 1; i < (int)soup->indexCount; i++ )
            {
                if ( (int)context->indices[soup->firstIndex + i]
                     < reorderList[reorderCount].firstVertex )
                    reorderList[reorderCount].firstVertex = context->indices[soup->firstIndex + i];
                else if ( reorderList[reorderCount].lastVertex
                          < (int)context->indices[soup->firstIndex + i] )
                    reorderList[reorderCount].lastVertex = context->indices[soup->firstIndex + i];
            }

            Assertx( reorderList[reorderCount].firstVertex < reorderList[reorderCount].lastVertex,
                     "reorderList[reorderCount].firstVertex < reorderList[reorderCount].lastVertex"
                     "\n\t%i, %i",
                     reorderList[reorderCount].firstVertex,
                     reorderList[reorderCount].lastVertex );
            Assertx( (unsigned int)reorderList[reorderCount].lastVertex < soup->vertexCount,
                     "reorderList[reorderCount].lastVertex doesn't index triSurf->vertexCount"
                     "\n\t%i not in [0, %i)",
                     reorderList[reorderCount].lastVertex, soup->vertexCount );

            reorderList[reorderCount].firstVertex += soup->firstVertex;
            reorderList[reorderCount].lastVertex  += soup->firstVertex;
            reorderCount++;
        }

        if ( reorderCount != 0 )
            Tris_ReorderTriSurfaceRun( context, entityIndex, reorderList, reorderCount );
    }

    delete [] reorderGlobNewVertIndex;
    delete [] (byte *)reorderGlobRemap;
    delete [] (byte *)reorderGlobMapArray;
    delete [] (byte *)reorderGlobIndexBuf;
    delete [] (byte *)reorderGlobOptIndices;
}


/* Tris_FindLayeredMaterialByName  0x0044a450 */
LayeredMaterialDesc_t *Tris_FindLayeredMaterialByName( const char *name )
{
    int index;

    for ( index = 0; index < triGlobLyrMtlCount; index++ )
    {
        if ( !strcmp( name, LayeredMaterialName( index ) ) )
            return LayeredMaterialDesc( index );
    }

    SanityCheckMsg( "inconceivable" );
    return NULL;
}


/* Tris_BuildMaterialRemapTable  0x0044a4d0 */
int Tris_BuildMaterialRemapTable( int *remapTable, int materialIndex, int nextSlot )
{
    int i;

    for ( i = 0; i < materialIndex; i++ )
    {
        if ( !_stricmp( bspMaterials[i].name, bspMaterials[materialIndex].name ) )
        {
            remapTable[materialIndex] = remapTable[i];
            return nextSlot;
        }
    }

    remapTable[materialIndex] = nextSlot;
    return nextSlot + 1;
}


/* Tris_ReorderTriSurfaceRun  0x0044a550 */
void Tris_ReorderTriSurfaceRun( TrisContext_t *context, int entityIndex,
                                TrisReorderEntry_t *reorderList, int reorderCount )
{
    int reorderIndex;
    int usedCount;

    Assert( reorderList );
    Assert( reorderCount > 0 );

    qsort_vc8( reorderList, reorderCount, sizeof( TrisReorderEntry_t ), Tris_ReorderSortCompare );

    for ( reorderIndex = 0; reorderIndex < reorderCount; reorderIndex += usedCount )
    {
        usedCount = Tris_ReorderAndOptimizeVerts( context, &reorderList[reorderIndex],
                                                  reorderCount - reorderIndex );

        Assert( usedCount > 0 );
        Assert( reorderIndex + usedCount <= reorderCount );

        if ( entityIndex != 0 && usedCount != reorderCount )
        {
            Com_Error( "brush model entity %i uses more than 65536 vertices of material '%s'",
                       entityIndex, bspMaterials[reorderList[0].soup->materialIndex].name );
        }
    }
}


/* Tris_ReorderAndOptimizeVerts  0x0044a690 */
int Tris_ReorderAndOptimizeVerts( TrisContext_t *context, TrisReorderEntry_t *reorderList,
                                  int reorderCount )
{
    BspTriSoup_t *soup;
    unsigned int  reorderIndex;
    unsigned int  usedCount;
    int           newIndexCount;
    unsigned int  newVertCount;
    unsigned int  i;
    int           face;
    int           firstIndex;
    unsigned int  surfIndexCount;
    int           vertexLayerData;
    int           newFirstVertex;
    int           perVertexLayerDataSize;
    int           destIndex;
    int           optIndex;
    unsigned int  vertIndex;

    Assert( reorderList );
    Assert( reorderCount > 0 );

    memset( reorderGlobNewVertIndex, -1, context->vertCount * sizeof( int ) );

    newVertCount  = 0;
    newIndexCount = 0;

    for ( reorderIndex = 0; ; reorderIndex++ )
    {
        usedCount = reorderIndex;

        if ( reorderIndex >= (unsigned int)reorderCount )
            break;

        soup = reorderList[reorderIndex].soup;

        Assert( soup );

        if ( soup->indexCount + newIndexCount > REORDER_INDEX_LIMIT )
            break;

        if ( !Tris_ReorderAddSurfaceVerts( context, soup, reorderGlobIndexBuf,
                                           &newIndexCount, &newVertCount ) )
            break;
    }

    if ( usedCount == 0 )
        Com_Error( "No valid vertex groups.  This is a code bug.\n" );

    if ( !reorderTris )
    {
        for ( i = 0; i < (unsigned int)newIndexCount; i++ )
            reorderGlobRemap[i] = checked_cast< unsigned short >( i );
    }
    else
    {
        firstIndex = 0;

        for ( reorderIndex = 0; reorderIndex < usedCount; reorderIndex++ )
        {
            surfIndexCount = reorderList[reorderIndex].soup->indexCount;

            Assertx( (unsigned int)( firstIndex + surfIndexCount ) < REORDER_INDEX_LIMIT,
                     "firstIndex + surfIndexCount doesn't index REORDER_INDEX_LIMIT"
                     "\n\t%i not in [0, %i)",
                     firstIndex + surfIndexCount, REORDER_INDEX_LIMIT );

            D3DXOptimizeFaces( &reorderGlobIndexBuf[firstIndex], surfIndexCount / 3,
                               newVertCount, 1, reorderGlobRemap );

            for ( face = 0; (unsigned int)( face * 3 ) < surfIndexCount; face++ )
            {
                reorderGlobOptIndices[face * 3 + firstIndex] =
                    checked_cast< unsigned short >(
                        reorderGlobIndexBuf[reorderGlobRemap[face] * 3 + firstIndex] );
                reorderGlobOptIndices[face * 3 + firstIndex + 1] =
                    checked_cast< unsigned short >(
                        reorderGlobIndexBuf[reorderGlobRemap[face] * 3 + firstIndex + 1] );
                reorderGlobOptIndices[face * 3 + firstIndex + 2] =
                    checked_cast< unsigned short >(
                        reorderGlobIndexBuf[reorderGlobRemap[face] * 3 + firstIndex + 2] );
            }

            firstIndex += surfIndexCount;
        }

        D3DXOptimizeVertices( reorderGlobOptIndices, newIndexCount / 3, newVertCount, 0,
                              reorderGlobRemap );
    }

    for ( i = 0; i < newVertCount; i++ )
        reorderGlobMapArray[reorderGlobRemap[i]] = i;

    newFirstVertex  = context->diskVertCount;
    vertexLayerData = context->diskVertLayerDataUsed;

    perVertexLayerDataSize = reorderList[0].lyrMtlDesc->layerCount * 8 - 8;
    perVertexLayerDataSize += I_max( 0, reorderList[0].lyrMtlDesc->normalMapCount - 1 ) * 4;

    for ( vertIndex = 0; vertIndex < (unsigned int)context->vertCount; vertIndex++ )
    {
        if ( reorderGlobNewVertIndex[vertIndex] == -1 )
            continue;

        SanityCheckx( (unsigned int)reorderGlobNewVertIndex[vertIndex] < newVertCount,
                      "reorderGlob.newVertIndex[vertIndex] doesn't index newVertCount"
                      "\n\t%i not in [0, %i)",
                      reorderGlobNewVertIndex[vertIndex], newVertCount );
        SanityCheckx( (unsigned int)reorderGlobMapArray[reorderGlobNewVertIndex[vertIndex]]
                      < newVertCount,
                      "reorderGlob.mapArray[reorderGlob.newVertIndex[vertIndex]] doesn't index"
                      " newVertCount\n\t%i not in [0, %i)",
                      reorderGlobMapArray[reorderGlobNewVertIndex[vertIndex]], newVertCount );

        destIndex = reorderGlobMapArray[reorderGlobNewVertIndex[vertIndex]];

        Tris_CopyReorderedVert( context, vertIndex, newFirstVertex + destIndex,
                                perVertexLayerDataSize * destIndex + vertexLayerData,
                                reorderList[0].lyrMtlDesc );

        context->diskVertCount++;
        context->diskVertLayerDataUsed += perVertexLayerDataSize;
    }

    SanityCheck( context->diskVertCount == newFirstVertex + (int)newVertCount );
    SanityCheck( context->diskVertLayerDataUsed
                 == vertexLayerData + perVertexLayerDataSize * (int)newVertCount );

    if ( perVertexLayerDataSize == 0 )
        vertexLayerData = 0;

    optIndex = 0;

    for ( reorderIndex = 0; reorderIndex < usedCount; reorderIndex++ )
    {
        soup = reorderList[reorderIndex].soup;

        for ( i = 0; i < soup->indexCount; i++ )
        {
            context->indices[soup->firstIndex + i] =
                checked_cast< unsigned short >(
                    reorderGlobMapArray[reorderGlobOptIndices[optIndex]] );
            optIndex++;
        }

        soup->vertexLayerData = vertexLayerData;
        soup->firstVertex     = newFirstVertex;
        soup->vertexCount     = checked_cast< unsigned short >( newVertCount );

        Assertx( soup->vertexCount + soup->firstVertex == context->diskVertCount,
                 "triSurf->firstVertex + triSurf->vertexCount == context->diskVertCount\n\t%i, %i",
                 soup->vertexCount + soup->firstVertex, context->diskVertCount );
    }

    return usedCount;
}


/* Tris_CopyReorderedVert  0x0044aca0 */
void Tris_CopyReorderedVert( TrisContext_t *context, int sourceIndex, int destIndex,
                             int layerDataOffset, const LayeredMaterialDesc_t *lyrMtlDesc )
{
    const BspDrawVert_t *src;
    BspDrawVert_t       *dst;
    unsigned int         layer;
    unsigned int         region;

    src = &context->verts[sourceIndex];
    dst = &context->diskVerts[destIndex];

    Assertx( Vec3IsNormalized( src->normal ), "%s\n\t%s",
             "Vec3IsNormalized( context->verts[sourceIndex].normal )",
             va( "(%g %g %g) len %g", src->normal[0], src->normal[1], src->normal[2],
                 Vec3Length( src->normal ) ) );
    Assertx( Vec3IsNormalized( src->tangent ), "%s\n\t%s",
             "Vec3IsNormalized( context->verts[sourceIndex].binormal )",
             va( "(%g %g %g) len %g", src->tangent[0], src->tangent[1], src->tangent[2],
                 Vec3Length( src->tangent ) ) );
    Assertx( Vec3IsNormalized( src->binormal ), "%s\n\t%s",
             "Vec3IsNormalized( context->verts[sourceIndex].tangent )",
             va( "(%g %g %g) len %g", src->binormal[0], src->binormal[1], src->binormal[2],
                 Vec3Length( src->binormal ) ) );

    Assertx( src->lmapCoord[0] >= 0.0f && src->lmapCoord[0] <= 1.0f,
             "context->verts[sourceIndex].lmapCoord[0] not in [0.0f, 1.0f]\n\t%g not in [%g, %g]",
             src->lmapCoord[0], 0.0f, 1.0f );
    Assertx( src->lmapCoord[1] >= 0.0f && src->lmapCoord[1] <= 1.0f,
             "context->verts[sourceIndex].lmapCoord[1] not in [0.0f, 1.0f]\n\t%g not in [%g, %g]",
             src->lmapCoord[1], 0.0f, 1.0f );

    Assert( !IS_NAN( src->xyz[0] )      && !IS_NAN( src->xyz[1] )      && !IS_NAN( src->xyz[2] ) );
    Assert( !IS_NAN( src->normal[0] )   && !IS_NAN( src->normal[1] )   && !IS_NAN( src->normal[2] ) );
    Assert( !IS_NAN( src->tangent[0] )  && !IS_NAN( src->tangent[1] )  && !IS_NAN( src->tangent[2] ) );
    Assert( !IS_NAN( src->binormal[0] ) && !IS_NAN( src->binormal[1] ) && !IS_NAN( src->binormal[2] ) );
    Assert( !IS_NAN( src->texCoord[0] ) );
    Assert( !IS_NAN( src->texCoord[1] ) );
    Assert( !IS_NAN( src->lmapCoord[0] ) );
    Assert( !IS_NAN( src->lmapCoord[1] ) );

    Vec3Copy( src->xyz,       dst->xyz );
    Vec3Copy( src->normal,    dst->normal );
    Byte4Copy( src->color, dst->color );
    Vec2Copy( src->texCoord,  dst->texCoord );
    Vec2Copy( src->lmapCoord, dst->lmapCoord );

    Vec3Copy( src->tangent,  dst->binormal );
    Vec3Copy( src->binormal, dst->tangent );

    if ( lyrMtlDesc->layerCount == 1 )
        return;

    for ( layer = 1; layer < lyrMtlDesc->layerCount; layer++ )
    {
        layerDataOffset = Tris_CopyLayerTexCoord(
            context,
            (const float *)( context->vertLayerData
                             + sourceIndex * TRIS_VERT_LAYER_DATA_STRIDE - 8 + layer * 8 ),
            layerDataOffset );
    }

    for ( region = 1; region < lyrMtlDesc->normalMapCount; region++ )
    {
        layerDataOffset = Tris_PackLayerTangentFrame(
            context, src->binormal, src->tangent,
            (const float *)( context->vertLayerData
                             + sourceIndex * TRIS_VERT_LAYER_DATA_STRIDE
                             + 0x38 + ( region - 1 ) * 0xc ),
            (const float *)( context->vertLayerData
                             + sourceIndex * TRIS_VERT_LAYER_DATA_STRIDE
                             + 0x20 + ( region - 1 ) * 0xc ),
            layerDataOffset );
    }
}


/* Tris_CopyLayerTexCoord  0x0044b4d0 */
static int Tris_CopyLayerTexCoord( TrisContext_t *context, const vec2_t texCoords, int offset )
{
    Assert( !IS_NAN( texCoords[0] ) );
    Assert( !IS_NAN( texCoords[1] ) );

    memcpy( context->diskVertLayerData + offset, texCoords, 8 );

    return offset + 8;
}


/* Tris_PackTangentByte  0x0044b610 */
static byte Tris_PackTangentByte( float value )
{
    return (byte)(int)( I_fclamp( value, -1.0f, 1.0f ) * 127.5f + 128.0f );
}


/* Tris_PackLayerTangentFrame  0x0044b560 */
static int Tris_PackLayerTangentFrame( TrisContext_t *context, const vec3_t tangent,
                                       const vec3_t binormal, const vec3_t layerTangent,
                                       const vec3_t layerBinormal, int offset )
{
    context->diskVertLayerData[offset]     = Tris_PackTangentByte( Vec3Dot( layerTangent,  tangent ) );
    context->diskVertLayerData[offset + 1] = Tris_PackTangentByte( Vec3Dot( layerBinormal, tangent ) );
    context->diskVertLayerData[offset + 2] = Tris_PackTangentByte( Vec3Dot( layerTangent,  binormal ) );
    context->diskVertLayerData[offset + 3] = Tris_PackTangentByte( Vec3Dot( layerBinormal, binormal ) );

    return offset + 4;
}


/* Tris_CanonicalizeIslandTexCoords  0x00446b90 */
void Tris_CanonicalizeIslandTexCoords( TrisContext_t *context, int firstVert, int vertCount,
                                       unsigned short *indices, int indexCount,
                                       const LayeredMaterialDesc_t *lyrMtlDesc, int lmapIndex )
{
    int         *adjacency;
    byte        *visited;
    unsigned int i;

    adjacency = new int[indexCount];
    visited   = new byte[indexCount];

    Tris_BuildIslandAdjacency( context, firstVert, vertCount, indices, indexCount, adjacency );

    memset( visited, 0, indexCount );

    for ( i = 0; i < (unsigned int)indexCount; i += 3 )
    {
        if ( !visited[i] )
        {
            Tris_FloodIslandFrom( context, firstVert, indices, indexCount, lyrMtlDesc,
                                  lmapIndex, adjacency, i, visited );
        }
    }

    delete [] visited;
    delete [] adjacency;
}


/* Tris_BuildIslandAdjacency  0x00446c90 */
void Tris_BuildIslandAdjacency( const TrisContext_t *context, int firstVert, int vertCount,
                                const unsigned short *indices, int indexCount, int *adjacency )
{
    const BspDrawVert_t *verts;
    unsigned int         i;
    unsigned int         j;
    unsigned int         a;
    unsigned int         aPrev;
    unsigned int         b;
    unsigned int         bPrev;

    (void)vertCount;

    memset( adjacency, -1, indexCount * sizeof( adjacency[0] ) );

    verts = &context->verts[firstVert];

    for ( i = 3; i < (unsigned int)indexCount; i += 3 )
    {
        for ( j = 0; j < i; j += 3 )
        {
            aPrev = 2;

            for ( a = 0; a < 3; a++ )
            {
                bPrev = 2;

                for ( b = 0; b < 3; b++ )
                {
                    if ( Vec3Compare( verts[indices[i + aPrev]].xyz,
                                      verts[indices[j + b]].xyz ) &&
                         Vec3Compare( verts[indices[i + a]].xyz,
                                      verts[indices[j + bPrev]].xyz ) )
                    {
                        adjacency[i + aPrev] = j;
                        adjacency[j + bPrev] = i;
                        goto nextTriangle;
                    }

                    bPrev = b;
                }

                aPrev = a;
            }

nextTriangle: ;
        }
    }
}


/* Tris_FloodIslandFrom  0x00446df0 */
void Tris_FloodIslandFrom( TrisContext_t *context, int firstVert,
                           const unsigned short *indices, int indexCount,
                           const LayeredMaterialDesc_t *lyrMtlDesc, int lmapIndex,
                           const int *adjacency, unsigned int firstIndex, byte *visited )
{
    int          shiftS[MTL_LAYER_LIMIT];
    int          shiftT[MTL_LAYER_LIMIT];
    unsigned int corner;
    int          neighbour;

    AssertIn( firstIndex, (unsigned int)indexCount );
    Assertx( firstIndex % 3 == 0, "(i) = %i", firstIndex );

    visited[firstIndex] = 1;

    for ( corner = 0; corner < 3; corner++ )
    {
        neighbour = adjacency[firstIndex + corner];

        if ( neighbour != -1 && !visited[neighbour] &&
             Tris_IslandShiftForNeighbour( context, firstVert, indices, lyrMtlDesc, lmapIndex,
                                           firstIndex, neighbour, shiftS, shiftT ) )
        {
            Tris_ApplyIslandShift( context, firstVert, indices, lyrMtlDesc, neighbour,
                                   shiftS, shiftT );

            Tris_FloodIslandFrom( context, firstVert, indices, indexCount, lyrMtlDesc,
                                  lmapIndex, adjacency, neighbour, visited );
        }
    }
}


/* Tris_IslandShiftForNeighbour  0x00446f30 */
bool Tris_IslandShiftForNeighbour( const TrisContext_t *context, int firstVert,
                                   const unsigned short *indices,
                                   const LayeredMaterialDesc_t *lyrMtlDesc, int lmapIndex,
                                   unsigned int indexA, unsigned int indexB,
                                   int *shiftS, int *shiftT )
{
    int          localS[MTL_LAYER_LIMIT];
    int          localT[MTL_LAYER_LIMIT];
    bool         haveShift;
    unsigned int a;
    unsigned int b;
    int          vertA;
    int          vertB;
    int          layer;

    haveShift = false;

    for ( a = 0; a < 3; a++ )
    {
        vertA = indices[indexA + a] + firstVert;

        for ( b = 0; b < 3; b++ )
        {
            vertB = indices[indexB + b] + firstVert;

            if ( !Vec3Compare( context->verts[vertA].xyz, context->verts[vertB].xyz ) )
                continue;

            if ( !Tris_VertsShareIslandSeam( context, vertA, vertB, lyrMtlDesc, lmapIndex,
                                             localS, localT ) )
                return false;

            if ( !haveShift )
            {
                for ( layer = 0; layer < lyrMtlDesc->layerCount; layer++ )
                {
                    shiftS[layer] = localS[layer];
                    shiftT[layer] = localT[layer];
                }

                haveShift = true;
            }
            else
            {
                for ( layer = 0; layer < lyrMtlDesc->layerCount; layer++ )
                {
                    if ( shiftS[layer] != localS[layer] || shiftT[layer] != localT[layer] )
                        return false;
                }
            }
        }
    }

    return true;
}


/* Tris_VertsShareIslandSeam  0x004470a0 */
bool Tris_VertsShareIslandSeam( const TrisContext_t *context, int vertA, int vertB,
                                const LayeredMaterialDesc_t *lyrMtlDesc, int lmapIndex,
                                int *shiftS, int *shiftT )
{
    const BspDrawVert_t *a;
    const BspDrawVert_t *b;
    unsigned int         layer;
    const byte          *layerA;
    const byte          *layerB;

    a = &context->verts[vertA];
    b = &context->verts[vertB];

    if ( !Vec3Equal( a->xyz, b->xyz ) )
        return false;

    if ( Vec3Dot( a->normal, b->normal ) < 0.999f )
        return false;

    if ( lmapIndex != LIGHTMAP_NONE
         && !Tris_TexCoordsClose( a->lmapCoord, b->lmapCoord, 0x200, 0x200 ) )
        return false;

    if ( a->color[0] != b->color[0] || a->color[1] != b->color[1]
         || a->color[2] != b->color[2] || a->color[3] != b->color[3] )
        return false;

    if ( !Tris_TexCoordRepeatShift( a->texCoord, b->texCoord, lyrMtlDesc->mtlRaw[0],
                                    &shiftS[0], &shiftT[0] ) )
        return false;

    if ( lyrMtlDesc->layerCount > 1 )
    {
        Assert( context->vertLayerData );

        layerA = context->vertLayerData + vertA * TRIS_VERT_LAYER_DATA_STRIDE;
        layerB = context->vertLayerData + vertB * TRIS_VERT_LAYER_DATA_STRIDE;

        for ( layer = 1; layer < lyrMtlDesc->layerCount; layer++ )
        {
            if ( !Tris_TexCoordRepeatShift( (const float *)( layerA - 8 + layer * 8 ),
                                            (const float *)( layerB - 8 + layer * 8 ),
                                            lyrMtlDesc->mtlRaw[layer],
                                            &shiftS[layer], &shiftT[layer] ) )
                return false;
        }
    }

    return true;
}


/* Tris_TexCoordRepeatShift  0x00447300 */
bool Tris_TexCoordRepeatShift( const vec2_t uv0, const vec2_t uv1, const Material_t *material,
                               int *shiftS, int *shiftT )
{
    vec2_t delta;
    float  wholeS;
    float  wholeT;

    Vec2Sub( uv1, uv0, delta );

    wholeS = floor( ( float )( delta[0] + 0.5f ) );
    wholeT = floor( ( float )( delta[1] + 0.5f ) );

    *shiftS = (int)wholeS;
    *shiftT = (int)wholeT;

    delta[0] = ( delta[0] - wholeS ) * material->textureWidth;
    delta[1] = ( delta[1] - wholeT ) * material->textureHeight;

    return Vec2LengthSq( delta ) <= 0.25f;
}


/* Tris_ApplyIslandShift  0x004473c0 */
void Tris_ApplyIslandShift( TrisContext_t *context, int firstVert,
                            const unsigned short *indices,
                            const LayeredMaterialDesc_t *lyrMtlDesc, unsigned int firstIndex,
                            const int *shiftS, const int *shiftT )
{
    int          vert[3];
    unsigned int k;
    unsigned int layer;
    float       *uv;

    for ( k = 0; k < 3; k++ )
        vert[k] = indices[firstIndex + k] + firstVert;

    if ( !( lyrMtlDesc->mtlRaw[0]->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS ) )
    {
        for ( k = 0; k < 3; k++ )
        {
            context->verts[vert[k]].texCoord[0] -= (float)shiftS[0];
            context->verts[vert[k]].texCoord[1] -= (float)shiftT[0];
        }
    }

    for ( layer = 1; layer < lyrMtlDesc->layerCount; layer++ )
    {
        if ( lyrMtlDesc->mtlRaw[layer]->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS )
            continue;

        for ( k = 0; k < 3; k++ )
        {
            uv = (float *)( context->vertLayerData + vert[k] * TRIS_VERT_LAYER_DATA_STRIDE
                            - 8 + layer * 8 );

            uv[0] -= (float)shiftS[layer];
            uv[1] -= (float)shiftT[layer];
        }
    }
}


/* Tris_ClampTexCoordsToLimit  0x00447560 */
void Tris_ClampTexCoordsToLimit( TrisContext_t *context, int firstVert, int vertCount,
                                 const LayeredMaterialDesc_t *lyrMtlDesc )
{
    unsigned int layer;

    if ( !( lyrMtlDesc->mtlRaw[0]->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS ) )
    {
        Tris_ClampTexCoordStream( lyrMtlDesc->mtlRaw[0],
                                  context->verts[firstVert].texCoord,
                                  (int)( sizeof( BspDrawVert_t ) / sizeof( float ) ),
                                  vertCount );
    }

    for ( layer = 1; layer < lyrMtlDesc->layerCount; layer++ )
    {
        if ( lyrMtlDesc->mtlRaw[layer]->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS )
            continue;

        Tris_ClampTexCoordStream( lyrMtlDesc->mtlRaw[layer],
                                  (float *)( context->vertLayerData
                                             + firstVert * TRIS_VERT_LAYER_DATA_STRIDE
                                             - 8 + layer * 8 ),
                                  (int)( TRIS_VERT_LAYER_DATA_STRIDE / sizeof( float ) ),
                                  vertCount );
    }
}


/* Tris_ClampTexCoordStream  0x00447610 */
void Tris_ClampTexCoordStream( const Material_t *material, float *base, int stride,
                               int vertCount )
{
    vec2_t       mins;
    vec2_t       maxs;
    vec2_t       texCoordShift;
    vec2_t       texCoordHalfLimit;
    float       *texCoord;
    unsigned int i;

    texCoord = base;
    Vec2Copy( texCoord, mins );
    Vec2Copy( texCoord, maxs );

    for ( i = 1; i < (unsigned int)vertCount; i++ )
    {
        texCoord = base + i * stride;
        AddPointToBounds2D( texCoord, mins, maxs );
    }

    texCoordHalfLimit[0] = (float)Tris_LmapTexelLimit( material->textureWidth );
    texCoordHalfLimit[1] = (float)Tris_LmapTexelLimit( material->textureHeight );

    texCoordShift[0] = floor( ( float )( ( maxs[0] + mins[0] ) * 0.5f ) );
    texCoordShift[1] = floor( ( float )( ( maxs[1] + mins[1] ) * 0.5f ) );

    if ( Tris_TexCoordsExceedLimit( mins, maxs, texCoordShift, texCoordHalfLimit ) )
    {
        texCoordShift[0] = floor( mins[0] );
        texCoordShift[1] = floor( mins[1] );

        Tris_ShiftTexCoordsPerTriangle( base, stride, vertCount,
                                        texCoordShift, texCoordHalfLimit );
        return;
    }

    for ( i = 0; i < (unsigned int)vertCount; i++ )
    {
        texCoord = base + i * stride;

        Vec2Sub( texCoord, texCoordShift, texCoord );

        AssertRangeFloat( texCoord[0], -texCoordHalfLimit[0], +texCoordHalfLimit[0] );
        AssertRangeFloat( texCoord[1], -texCoordHalfLimit[1], +texCoordHalfLimit[1] );
    }
}


/* Tris_TexCoordsExceedLimit  0x00447860 */
bool Tris_TexCoordsExceedLimit( const vec2_t mins, const vec2_t maxs, const vec2_t shift,
                                const vec2_t halfLimit )
{
    if ( mins[0] - shift[0] < -halfLimit[0] )
        return true;

    if ( mins[1] - shift[1] < -halfLimit[1] )
        return true;

    if ( maxs[0] - shift[0] > halfLimit[0] )
        return true;

    if ( maxs[1] - shift[1] > halfLimit[1] )
        return true;

    return false;
}


/* Tris_ShiftTexCoordsPerTriangle  0x004478f0 */
void Tris_ShiftTexCoordsPerTriangle( float *base, int stride, unsigned int vertCount,
                                     const vec2_t soupShift, const vec2_t halfLimit )
{
    vec2_t       mins;
    vec2_t       maxs;
    vec2_t       shift;
    float       *texCoord0;
    float       *texCoord1;
    float       *texCoord2;
    unsigned int i;

    Assertx( vertCount % 3 == 0, "(vertCount) = %i", vertCount );

    for ( i = 0; i < vertCount; i += 3 )
    {
        texCoord0 = base + i * stride;
        texCoord1 = base + ( i + 1 ) * stride;
        texCoord2 = base + ( i + 2 ) * stride;

        Vec2Copy( texCoord0, mins );
        Vec2Copy( texCoord0, maxs );
        AddPointToBounds2D( texCoord1, mins, maxs );
        AddPointToBounds2D( texCoord2, mins, maxs );

        shift[0] = Tris_TexCoordShiftForRange( mins[0], maxs[0], soupShift[0], halfLimit[0] );
        shift[1] = Tris_TexCoordShiftForRange( mins[1], maxs[1], soupShift[1], halfLimit[1] );

        Vec2Sub( texCoord0, shift, texCoord0 );
        Vec2Sub( texCoord1, shift, texCoord1 );
        Vec2Sub( texCoord2, shift, texCoord2 );
    }
}


/* Tris_TexCoordShiftForRange  0x00447a70 */
float Tris_TexCoordShiftForRange( float mins, float maxs, float shiftGuess,
                                  float texCoordHalfLimit )
{
    float fullLimit;
    float quotient;
    float shift;

    mins = SnapToIntegralPowerOf2( mins, 2, 10 );
    maxs = SnapToIntegralPowerOf2( maxs, 2, 10 );

    fullLimit = texCoordHalfLimit * 2.0f;
    quotient  = ( mins - shiftGuess ) / fullLimit;

    shift = floor( quotient ) * fullLimit + shiftGuess + texCoordHalfLimit;

    AssertCmpFloat( mins - shift, >=, -texCoordHalfLimit - 0.01f / 1024 );

    if ( maxs - shift > texCoordHalfLimit )
    {
        shift += texCoordHalfLimit;

        if ( mins - shift < -texCoordHalfLimit )
        {
            shift -= texCoordHalfLimit * 0.5f;

            if ( mins - shift < -texCoordHalfLimit )
            {
                while ( mins - shift < -texCoordHalfLimit )
                    shift -= 1.0f;

                AssertCmpFloat( maxs - shift, <=, texCoordHalfLimit + 0.01f / 1024 );
            }
            else
            {
                while ( maxs - shift > texCoordHalfLimit )
                    shift += 1.0f;

                AssertCmpFloat( mins - shift, >=, -texCoordHalfLimit - 0.01f / 1024 );
            }
        }
    }

    return shift;
}


/* Tris_ReorderAddSurfaceVerts  0x0044b660 */
bool Tris_ReorderAddSurfaceVerts( TrisContext_t *context, const BspTriSoup_t *soup,
                                  int *indexBuf, int *indexCount, unsigned int *vertCount )
{
    unsigned int i;
    int          outIndex;
    unsigned int outVertCount;
    int          globalVert;
    unsigned int j;

    outIndex     = *indexCount;
    outVertCount = *vertCount;

    for ( i = 0; i < soup->indexCount; i++ )
    {
        globalVert = context->indices[soup->firstIndex + i] + soup->firstVertex;

        if ( reorderGlobNewVertIndex[globalVert] == -1 )
        {
            if ( outVertCount > 0xfffe )
            {
                for ( j = 0; j < (unsigned int)context->vertCount; j++ )
                {
                    if ( reorderGlobNewVertIndex[j] >= (int)*vertCount )
                        reorderGlobNewVertIndex[j] = -1;
                }
                return false;
            }
            reorderGlobNewVertIndex[globalVert] = checked_cast<unsigned short>( outVertCount );
            outVertCount++;
        }

        indexBuf[outIndex] = reorderGlobNewVertIndex[globalVert];
        outIndex++;
    }

    *indexCount = outIndex;
    *vertCount  = outVertCount;
    return true;
}


/* Tris_ReorderSortCompare  0x0044b790 */
static int Tris_ReorderSortCompare( const void *va, const void *vb )
{
    const TrisReorderEntry_t *a = (const TrisReorderEntry_t *)va;
    const TrisReorderEntry_t *b = (const TrisReorderEntry_t *)vb;
    int                       d;

    d = a->firstVertex - b->firstVertex;
    if ( d == 0 )
    {
        d = b->lastVertex - a->lastVertex;
        if ( d == 0 )
            d = 0;
    }
    return d;
}


/* Tris_FindNextEntity  0x0044b7e0 */
int Tris_FindNextEntity( const TrisContext_t *context, int entityIndex )
{
    int entityIndexNext;

    for ( entityIndexNext = entityIndex + 1; entityIndexNext != num_entities; entityIndexNext++ )
    {
        if ( entities[entityIndexNext].firstTriSoup[context->type] )
            break;
    }

    Assert( entityIndexNext == num_entities ||
            entities[entityIndexNext].firstTriSoup[context->type] >
            entities[entityIndex].firstTriSoup[context->type] );

    return entityIndexNext;
}


/* Tris_MergeSurfacePairs  0x0044b880 */
int Tris_MergeSurfacePairs( TriSurf_t **surfArray, intWinding_t **windingArray, int count,
                            winding_t **a, intWinding_t *b, int flags )
{
    int prevCount;
    int i;

    do
    {
        prevCount = count;

        for ( i = 0; i < count; i++ )
            count = Tris_TryMergeWindingPair( surfArray, windingArray, count, i, flags );
    }
    while ( prevCount != count );

    return Tris_FinishMergedSurfaces( surfArray, windingArray, count, a, b );
}


/* Tris_FinishMergedSurfaces  0x0044c1b0 */
int Tris_FinishMergedSurfaces( TriSurf_t **surfArray, intWinding_t **windingArray, int count,
                               winding_t **a, intWinding_t *b )
{
    (void)surfArray;
    (void)windingArray;
    (void)a;
    (void)b;

    return count;
}


/* Tris_TryMergeWindingPair  0x0044b8f0 */
int Tris_TryMergeWindingPair( TriSurf_t **surfArray, intWinding_t **windingArray, int count,
                              int index, int flags )
{
    intWinding_t   *w0;
    intWinding_t   *w1;
    intWinding_t   *merged;
    TriSurf_t      *swapSurf;
    TriSurfProps_t *swapProps;
    int             i;
    int             start0;
    int             start1;
    int             dupCount;
    int             newStart1;

    (void)flags;

    w0 = windingArray[index];

    for ( i = index + 1; i < count; i++ )
    {
        w1 = windingArray[i];

        if ( !Tris_LmapCellsMatch( surfArray[index], surfArray[i] ) )
            continue;

        dupCount = MergeFindSharedEdge( w0, w1, &start0, &start1 );
        if ( !dupCount )
            continue;

        if ( !Tris_MergedLmapFits( surfArray[index], surfArray[i], w0, w1,
                                   start0, start1, dupCount ) )
            continue;

        if ( dupCount == w0->ptCount )
        {
            swapSurf            = surfArray[index];
            surfArray[index]    = surfArray[i];
            surfArray[i]        = swapSurf;

            windingArray[index] = w1;
            windingArray[i]     = w0;

            w0 = windingArray[index];
            w1 = windingArray[i];

            newStart1 = ( start0 + dupCount - 1 ) % w1->ptCount;
            start0    = ( start1 + w0->ptCount - ( dupCount - 1 ) ) % w0->ptCount;
            start1    = newStart1;
        }

        if ( surfArray[index]->props->ds->castsSunShadow <
             surfArray[i]->props->ds->castsSunShadow )
        {
            swapProps                 = surfArray[index]->props;
            surfArray[index]->props   = surfArray[i]->props;
            surfArray[i]->props       = swapProps;
        }

        if ( dupCount == w1->ptCount )
        {
            MergeRemoveDuplicateIndices( w0, start0, dupCount );

            Tris_FreeSurface( surfArray[i] );
            FreeIntWinding( w1 );

            count--;
            memmove( &surfArray[i], &surfArray[i + 1],
                     ( count - i ) * sizeof( surfArray[0] ) );
            memmove( &windingArray[i], &windingArray[i + 1],
                     ( count - i ) * sizeof( windingArray[0] ) );
            windingArray[count] = NULL;
        }
        else
        {
            merged = MergeJoinIntWindings( w0, w1, index, i, start0, start1, dupCount );

            AddBoundsToBounds( surfArray[i]->mins, surfArray[i]->maxs,
                               surfArray[index]->mins, surfArray[index]->maxs );

            Tris_FreeSurface( surfArray[i] );
            memmove( &surfArray[i], &surfArray[i + 1],
                     ( count - ( i + 1 ) ) * sizeof( surfArray[0] ) );

            FreeIntWinding( w0 );
            FreeIntWinding( w1 );

            w0                  = merged;
            windingArray[index] = w0;

            count--;
            memmove( &windingArray[i], &windingArray[i + 1],
                     ( count - i ) * sizeof( windingArray[0] ) );
            windingArray[count] = NULL;
        }

        i = index;
    }

    return count;
}


/* Tris_LmapCellsMatch  0x0044c230 */
bool Tris_LmapCellsMatch( const TriSurf_t *surf0, const TriSurf_t *surf1 )
{
    float cellSize;

    if ( !BoundsOverlap( surf0->mins, surf0->maxs, surf1->mins, surf1->maxs ) )
        return false;

    cellSize = surf0->props->subdivisions;

    if ( cellSize != 0.0f )
    {
        if ( !Tris_LmapCellMatch( surf0, surf1, cellSize, 0 ) )
            return false;

        if ( !Tris_LmapCellMatch( surf0, surf1, cellSize, 1 ) )
            return false;

        if ( !Tris_LmapCellMatch( surf0, surf1, cellSize, 2 ) )
            return false;
    }

    return true;
}


/* Tris_MergedLmapFits  0x0044c320 */
bool Tris_MergedLmapFits( const TriSurf_t *surf0, const TriSurf_t *surf1,
                          const intWinding_t *w0, const intWinding_t *w1,
                          int start0, int start1, int dupCount )
{
    (void)start0;
    (void)start1;
    (void)dupCount;

    if ( Tris_MergedLmapExceeds( w0, w1, surf0->props ) )
        return false;

    if ( Tris_MergedLmapExceeds( w1, w0, surf1->props ) )
        return false;

    return true;
}


/* Tris_LmapCellMatch  0x0044bc70 */
bool Tris_LmapCellMatch( const TriSurf_t *a, const TriSurf_t *b, float cellSize, int axis )
{
    if ( FloorFloatToInt( ( a->mins[axis] + 0.002f ) / cellSize ) !=
         FloorFloatToInt( ( b->mins[axis] + 0.002f ) / cellSize ) )
        return false;

    if ( RoundFloatToIntHalf( ( a->maxs[axis] - 0.002f ) / cellSize ) !=
         RoundFloatToIntHalf( ( b->maxs[axis] - 0.002f ) / cellSize ) )
        return false;

    return true;
}


/* Tris_MergedLmapExceeds  0x0044bd40 */
bool Tris_MergedLmapExceeds( const intWinding_t *w0, const intWinding_t *w1,
                             const TriSurfProps_t *props )
{
    const CoalesceNode_t *chain;

    if ( props->coalesceChain )
    {
        for ( chain = props->coalesceChain; chain; chain = chain->next )
        {
            if ( Tris_MergedLmapExceedsProps( w0, w1, chain->props ) )
                return true;
        }
        return false;
    }

    return Tris_MergedLmapExceedsProps( w0, w1, props );
}


/* Tris_MergedLmapExceedsProps  0x0044bdb0 */
bool Tris_MergedLmapExceedsProps( const intWinding_t *w0, const intWinding_t *w1,
                                  const TriSurfProps_t *props )
{
    vec2_t mins;
    vec2_t maxs;
    int    extent[2];

    ClearBounds2D( mins, maxs );
    Tris_AddWindingLmapBounds( w0, props, mins, maxs );
    Tris_AddWindingLmapBounds( w1, props, mins, maxs );

    extent[0] = (int)( ceil( maxs[0] ) - floor( mins[0] ) );
    if ( extent[0] > 2 * Tris_LmapTexelLimitS( props ) )
        return true;

    extent[1] = (int)( ceil( maxs[1] ) - floor( mins[1] ) );
    if ( extent[1] > 2 * Tris_LmapTexelLimitT( props ) )
        return true;

    return false;
}


/* Tris_AddWindingLmapBounds  0x0044be80 */
void Tris_AddWindingLmapBounds( const intWinding_t *iw, const TriSurfProps_t *props,
                                vec2_t mins, vec2_t maxs )
{
    int          i;
    const float *pt;
    vec2_t       texCoord;

    for ( i = 0; i < iw->ptCount; i++ )
    {
        pt = MergeVertForIndex( iw->pts[i] );
        texCoord[0] = Vec3Dot( pt, props->texVecs[0] ) + props->texVecs[0][3];
        texCoord[1] = Vec3Dot( pt, props->texVecs[1] ) + props->texVecs[1][3];
        AddPointToBounds2D( texCoord, mins, maxs );
    }
}


/* Tris_GetColorTint  0x0043f9e0 / 0x0043fa30 */
void Tris_GetColorTint( const DrawSurf_t *ds, byte out[4] )
{
    vec4_t tint;

    if ( Material_ConstantValue( ds->mtlTex, "colorTint", tint ) )
        PackColorTint( tint, out );
    else
        PackColorTint( g_colorTintWhite, out );
}


/* Material_ConstantValue  0x0043fa30 */
bool Material_ConstantValue( const Material_t *material, const char *name, vec4_t out )
{
    const byte  *table;
    unsigned int i;

    table = Material_ConstantTable( material );

    for ( i = 0; i < material->constantCount; i++ )
    {
        if ( !strcmp( StringFromOffset( material, *(const int *)( table + i * 0x14 ) ), name ) )
        {
            Vec4Copy( (const float *)( table + 4 + i * 0x14 ), out );
            return true;
        }
    }
    return false;
}


/* Material_ConstantTable  0x0044bfc0 */
const byte *Material_ConstantTable( const Material_t *material )
{
    return ( const byte * )material + material->constantTableOffset;
}


/* PackColorTint  0x0044bf10 / 0x0044bf80 */
static byte PackColorTintChannel( float value )
{
    return (byte)I_max( 0, I_min( 255, RoundFloatToInt( value * 255.0f ) ) );
}

static void PackColorTint( const vec4_t tint, byte out[4] )
{
    out[2] = PackColorTintChannel( tint[0] );
    out[1] = PackColorTintChannel( tint[1] );
    out[0] = PackColorTintChannel( tint[2] );
    out[3] = PackColorTintChannel( tint[3] );
}


/* Tris_ModulateVertexColor  0x00440f40 */
void Tris_ModulateVertexColor( const DrawSurf_t *ds, const byte rgb[3], byte alpha, byte out[4] )
{
    if ( ( ds->toolFlags & TOOLFLAG_USAGE_MASK ) == TOOLFLAG_USAGE_VCOLOR )
    {
        out[2] = (byte)( ( rgb[2] * alpha + 127 ) / 255 );
        out[1] = (byte)( ( rgb[1] * alpha + 127 ) / 255 );
        out[0] = (byte)( ( rgb[0] * alpha + 127 ) / 255 );
        out[3] = 255;
    }
    else
    {
        Byte4Copy( rgb, out );
        out[3] = alpha;
    }
}


/* Tris_FindPatchWindings  0x0043f2b0 */
void Tris_FindPatchWindings( DrawSurf_t *ds, Tree_t *tree )
{
    Mesh_t     control;
    Mesh_t    *subdivided;
    Mesh_t    *grid;
    winding_t *w[2];
    vec4_t     plane[2];
    vec4_t     texVecs[2][2];
    vec4_t     lmapVecs[2][2];
    vec4_t     colorVecs[2][4];
    vec2_t     texCoords[3];
    vec2_t     lmapCoords[3];
    byte       colors[3][4];
    byte       tint[4];
    int        i;
    int        j;
    int        corner[3];
    int        k;
    int        side;

    control.width  = ds->patch.width;
    control.height = ds->patch.height;
    control.verts  = ds->verts;

    subdivided = SubdivideMesh( control, (float)ds->subdivisions, 999.0f, NULL, NULL );
    PutMeshOnCurve( *subdivided );

    grid = RemoveLinearMeshColumnsRows( subdivided, NULL, NULL );
    FreeMesh( subdivided );

    Tris_GetColorTint( ds, tint );

    for ( i = 0; i < grid->width - 1; i++ )
    {
        for ( j = 0; j < grid->height - 1; j++ )
        {
            for ( side = 0; side < 2; side++ )
            {
                w[side] = AllocWinding( side == 0 ? 4 : 3 );
                w[side]->ptCount = 3;

                if ( side == 0 )
                {
                    corner[0] = ( j + 1 ) * grid->width + i;
                    corner[1] = ( j + 1 ) * grid->width + 1 + i;
                    corner[2] = j * grid->width + 1 + i;
                }
                else
                {
                    corner[0] = j * grid->width + 1 + i;
                    corner[1] = j * grid->width + i;
                    corner[2] = ( j + 1 ) * grid->width + i;
                }

                for ( k = 0; k < 3; k++ )
                {
                    Vec3Copy( grid->verts[ corner[k] ].xyz, w[side]->pts[k] );
                    Vec2Copy( grid->verts[ corner[k] ].st, texCoords[k] );
                    Vec2Copy( grid->verts[ corner[k] ].lmSt, lmapCoords[k] );

                    Tris_ModulateVertexColor( ds, tint, grid->verts[ corner[k] ].color[3],
                                              colors[k] );
                }

                if ( !PlaneFromPoints( plane[side], w[side]->pts[0], w[side]->pts[1],
                                       w[side]->pts[2] ) ||
                     !DeriveTextureVectors( ds, plane[side], w[side]->pts,
                                            texCoords, texVecs[side],
                                            lmapCoords, lmapVecs[side],
                                            colors[0], colorVecs[side] ) )
                {
                    FreeWinding( w[side] );
                    w[side] = NULL;
                }
                else
                {
                    WrapLmapVecOffsets( lmapVecs[side] );
                }
            }

            if ( w[0] && w[1] )
            {
                if ( Tris_TrianglesFormQuad( ds, w, plane, texVecs[0], lmapVecs[0],
                                             colorVecs[0] ) )
                {
                    Vec3Copy( w[1]->pts[1], w[0]->pts[3] );
                    w[0]->ptCount = 4;

                    FreeWinding( w[1] );
                    w[1] = NULL;
                }
            }

            if ( w[0] )
            {
                Tris_EmitWindingAsDrawSurf( tree, w[0], plane[0], ds, texVecs[0],
                                            lmapVecs[0], colorVecs[0] );
            }

            if ( w[1] )
            {
                Tris_EmitWindingAsDrawSurf( tree, w[1], plane[1], ds, texVecs[1],
                                            lmapVecs[1], colorVecs[1] );
            }
        }
    }

    FreeMesh( grid );
}


/* Tris_FindIndexedMeshWindings  0x00440fd0 */
void Tris_FindIndexedMeshWindings( DrawSurf_t *ds, Tree_t *tree )
{
    winding_t *w;
    vec4_t     plane;
    vec2_t     texCoords[3];
    vec2_t     lmapCoords[3];
    byte       colors[3][4];
    vec4_t     texVecs[2];
    vec4_t     lmapVecs[2];
    vec4_t     colorVecs[4];
    byte       tint[4];
    int        i;
    int        k;
    int        vertIndex;

    Tris_GetColorTint( ds, tint );

    for ( i = 0; i < ds->terrain.indexCount; i += 3 )
    {
        w = AllocWinding( 4 );
        w->ptCount = 3;

        for ( k = 0; k < 3; k++ )
        {
            vertIndex = ds->terrain.indexes[i + k];

            Vec3Copy( ds->verts[vertIndex].xyz, w->pts[k] );
            Vec2Copy( ds->verts[vertIndex].st, texCoords[k] );
            Vec2Copy( ds->verts[vertIndex].lmSt, lmapCoords[k] );

            Tris_ModulateVertexColor( ds, tint, ds->verts[vertIndex].color[3], colors[k] );
        }

        if ( !PlaneFromPoints( plane, w->pts[0], w->pts[1], w->pts[2] ) )
        {
            FreeWinding( w );
            continue;
        }

        if ( !DeriveTextureVectors( ds, plane, w->pts, texCoords, texVecs,
                                    lmapCoords, lmapVecs, colors[0], colorVecs ) )
        {
            FreeWinding( w );
            continue;
        }

        if ( i + 3 < ds->terrain.indexCount &&
             ds->terrain.indexes[i]     == ds->terrain.indexes[i + 4] &&
             ds->terrain.indexes[i + 1] == ds->terrain.indexes[i + 3] )
        {
            vertIndex = ds->terrain.indexes[i + 5];

            if ( PointInsideTriangle( ds->verts[vertIndex].xyz, w, plane ) &&
                 Tris_TerrainVertsMatch( ds->mtlTex, &ds->verts[vertIndex],
                                         texVecs, lmapVecs, colorVecs ) )
            {
                Vec3Copy( w->pts[2], w->pts[3] );
                Vec3Copy( w->pts[1], w->pts[2] );
                Vec3Copy( ds->verts[vertIndex].xyz, w->pts[1] );

                w->ptCount = 4;
                i += 3;
            }
        }

        WrapLmapVecOffsets( lmapVecs );

        Tris_EmitWindingAsDrawSurf( tree, w, plane, ds, texVecs, lmapVecs, colorVecs );
    }
}


/* Tris_TerrainVertsMatch  0x00441320 */
bool Tris_TerrainVertsMatch( const Material_t *material, const DrawVert_t *vert,
                             const vec4_t *texVecs, const vec4_t *lmapVecs,
                             const vec4_t *colorVecs )
{
    int   i;
    float coord;

    coord = Vec3Dot( vert->xyz, texVecs[0] ) + texVecs[0][3];
    if ( I_fabs( coord - vert->st[0] ) * MATERIAL_TEX_WIDTH( material ) > 0.25f )
        return false;

    coord = Vec3Dot( vert->xyz, texVecs[1] ) + texVecs[1][3];
    if ( I_fabs( coord - vert->st[1] ) * MATERIAL_TEX_HEIGHT( material ) > 0.25f )
        return false;

    coord = Vec3Dot( vert->xyz, lmapVecs[0] ) + lmapVecs[0][3];
    if ( I_fabs( coord - vert->lmSt[0] ) > 0.00048828125f )
        return false;

    coord = Vec3Dot( vert->xyz, lmapVecs[1] ) + lmapVecs[1][3];
    if ( I_fabs( coord - vert->lmSt[1] ) > 0.00048828125f )
        return false;

    for ( i = 0; i < 4; i++ )
    {
        coord = Vec3Dot( vert->xyz, colorVecs[i] ) + colorVecs[i][3];
        if ( (unsigned int)RoundFloatToInt( coord ) != vert->color[i] )
            return false;
    }

    return true;
}


/* Tris_FindBrushWindings  0x004414d0 */
void Tris_FindBrushWindings( DrawSurf_t *ds, Tree_t *tree )
{
    winding_t *w;
    vec4_t     plane;
    vec3_t     positions[3];
    vec2_t     lmapCoords[3];
    vec4_t     lmapVecs[2];
    vec4_t     texVecs[2];
    vec4_t     colorVecs[4];
    vec2_t     texCoords[3];
    byte       tint[4];
    int        vertIndex;

    w = AllocWinding( ds->vertCount );
    w->ptCount = ds->vertCount;

    for ( vertIndex = 0; vertIndex < ds->vertCount; vertIndex++ )
        Vec3Copy( ds->verts[vertIndex].xyz, w->pts[vertIndex] );

    Vec3Copy( mapplanes[ds->side->planenum].normal, plane );
    plane[3] = mapplanes[ds->side->planenum].dist;

    for ( vertIndex = 0; vertIndex < ds->vertCount; vertIndex++ )
    {
        Assertx( I_fabs( Vec3Dot( plane, w->pts[vertIndex] ) - plane[3] ) <= TRIS_ON_EPSILON * 0.5f,
                 "I_fabs( Vec3Dot( plane, w->pts[vertIndex] ) - plane[3] ) <= ON_EPSILON * 0.5f"
                 "\n\t%g, %g",
                 I_fabs( Vec3Dot( plane, w->pts[vertIndex] ) - plane[3] ),
                 TRIS_ON_EPSILON * 0.5f );
    }

    Vec4Copy( ds->side->texMat[0], texVecs[0] );
    texVecs[0][3] -= (float)RoundFloatToInt( Vec3Dot( texVecs[0], ds->verts[0].xyz )
                                             + texVecs[0][3] - ds->verts[0].st[0] );

    Vec4Copy( ds->side->texMat[1], texVecs[1] );
    texVecs[1][3] -= (float)RoundFloatToInt( Vec3Dot( texVecs[1], ds->verts[0].xyz )
                                             + texVecs[1][3] - ds->verts[0].st[1] );

    if ( ds->mtlTex->techSetFlags & MTL_TECHSET_LIGHTMAP )
    {
        Tris_PickBestTriangle( ds->verts, ds->vertCount, plane, plane[3],
                               positions, texCoords, lmapCoords );

        if ( !DeriveTextureVectors( ds, plane, positions, NULL, NULL,
                                    lmapCoords, lmapVecs, NULL, NULL ) )
        {
            return;
        }

        WrapLmapVecOffsets( lmapVecs );

    }
    else
    {
        memset( lmapVecs[0], 0, sizeof( lmapVecs[0] ) );
        memset( lmapVecs[1], 0, sizeof( lmapVecs[1] ) );
    }

    memset( colorVecs, 0, sizeof( colorVecs ) );

    Tris_GetColorTint( ds, tint );
    colorVecs[0][3] = (float)tint[0];
    colorVecs[1][3] = (float)tint[1];
    colorVecs[2][3] = (float)tint[2];
    colorVecs[3][3] = (float)tint[3];

    Tris_EmitWindingAsDrawSurf( tree, w, plane, ds, texVecs, lmapVecs, colorVecs );
}


/* Tris_PickBestTriangle  0x00441870 */
void Tris_PickBestTriangle( const DrawVert_t *verts, int vertCount, const vec3_t normal,
                            float dist, vec3_t outPositions[3], vec2_t outTexCoords[3],
                            vec2_t outLmapCoords[3] )
{
    int    i;
    int    j;
    int    k;
    int    bestI;
    int    bestJ;
    int    bestK;
    float  bestArea;
    vec3_t edge0;
    vec3_t edge1;
    vec3_t cross;
    float  area;
    float  scale;

    bestI    = 0;
    bestJ    = 0;
    bestK    = 0;
    bestArea = 0.0f;

    for ( k = 2; k < vertCount; k++ )
    {
        for ( j = 1; j < k; j++ )
        {
            Vec3Sub( verts[k].xyz, verts[j].xyz, edge1 );
            for ( i = 0; i < j; i++ )
            {
                Vec3Sub( verts[i].xyz, verts[j].xyz, edge0 );
                Vec3Cross( edge0, edge1, cross );
                area = Vec3Dot( cross, normal );
                if ( area > bestArea )
                {
                    bestArea = area;
                    bestI    = i;
                    bestJ    = j;
                    bestK    = k;
                }
            }
        }
    }

    scale = dist - Vec3Dot( verts[bestI].xyz, normal );
    Vec3Mad( verts[bestI].xyz, scale, normal, outPositions[0] );
    Vec3Copy( verts[bestI].xyz, outPositions[0] );
    Vec2Copy( verts[bestI].st,   outTexCoords[0] );
    Vec2Copy( verts[bestI].lmSt, outLmapCoords[0] );

    scale = dist - Vec3Dot( verts[bestJ].xyz, normal );
    Vec3Mad( verts[bestJ].xyz, scale, normal, outPositions[1] );
    Vec3Copy( verts[bestJ].xyz, outPositions[1] );
    Vec2Copy( verts[bestJ].st,   outTexCoords[1] );
    Vec2Copy( verts[bestJ].lmSt, outLmapCoords[1] );

    scale = dist - Vec3Dot( verts[bestK].xyz, normal );
    Vec3Mad( verts[bestK].xyz, scale, normal, outPositions[2] );
    Vec3Copy( verts[bestK].xyz, outPositions[2] );
    Vec2Copy( verts[bestK].st,   outTexCoords[2] );
    Vec2Copy( verts[bestK].lmSt, outLmapCoords[2] );
}


/* Tris_EmitWindingAsDrawSurf  0x0043fac0 */
void Tris_EmitWindingAsDrawSurf( Tree_t *tree, winding_t *w, const vec4_t plane,
                                 DrawSurf_t *ds, const vec4_t *texVecs,
                                 const vec4_t *lmapVecs, const vec4_t *colorVecs )
{
    winding_t      *clipped;
    TriSurfProps_t *props;

    if ( ( ds->mtlTex->toolFlags & TOOLFLAG_USAGE_MASK ) != TOOLFLAG_USAGE_LIT &&
         Vec4Compare( colorVecs[3], vec4_origin ) )
        return;

    clipped = BrushSides_ClipWinding( w, ds, plane );
    if ( !clipped )
        return;

    props = Tris_AllocTriSurfProps( plane, ds, texVecs, lmapVecs, colorVecs );
    Tris_EmitTriSurfForProps( tree, clipped, props, ds );
}


/* Tris_AllocTriSurfProps  0x0043fb60 */
TriSurfProps_t *Tris_AllocTriSurfProps( const vec4_t plane, DrawSurf_t *ds,
                                        const vec4_t *texVecs, const vec4_t *lmapVecs,
                                        const vec4_t *colorVecs )
{
    TriSurfProps_t *props;

    Assert( plane );
    Assert( ds );
    Assert( texVecs );
    Assert( lmapVecs );
    Assert( colorVecs );
    Assert( ds->reflectionProbeIndex != REFLECTION_PROBE_INVALID );

    props = (TriSurfProps_t *)::operator new( sizeof( TriSurfProps_t ) );

    props->ds        = ds;
    props->lmapGroupId = -1;
    props->lmapIndex = LIGHTMAP_NONE;

    props->subdivisions = ds->mtlTex->subdivisions;
    if ( props->subdivisions == 0.0f )
        props->subdivisions = defaultTessSize;

    props->mergeTouching = 1;
    props->nodraw    = ( ds->mtlTex->surfaceFlags & SURF_NODRAW ) != 0;

    Vec4Copy( plane,        props->plane );
    Vec4Copy( texVecs[0],   props->texVecs[0] );
    Vec4Copy( texVecs[1],   props->texVecs[1] );
    Vec4Copy( lmapVecs[0],  props->lmapVecs[0] );
    Vec4Copy( lmapVecs[1],  props->lmapVecs[1] );
    Vec4Copy( colorVecs[0], props->colorVecs[0] );
    Vec4Copy( colorVecs[1], props->colorVecs[1] );
    Vec4Copy( colorVecs[2], props->colorVecs[2] );
    Vec4Copy( colorVecs[3], props->colorVecs[3] );

    props->coalesceChain = NULL;
    props->next      = triGlobPropsList;
    triGlobPropsList     = props;

    if ( !( ds->mtlTex->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS ) )
    {
        props->texVecs[0][3] -= FloorFloatToInt( props->texVecs[0][3] );
        props->texVecs[1][3] -= FloorFloatToInt( props->texVecs[1][3] );
    }

    return props;
}


/* Tris_EmitTriSurfForProps  0x0043fe20 */
void Tris_EmitTriSurfForProps( Tree_t *tree, winding_t *w, TriSurfProps_t *props,
                               const DrawSurf_t *ds )
{
    if ( ds->outputNumber < 0 )
    {
        if ( entity_num < 1 )
            Tris_EmitTriSurf_r( w, props, tree->headnode );
        else
            PrependTriSurf( w, props, &triGlobSurfLists[0] );
    }
    else
    {
        PrependTriSurf( w, props, &triGlobSurfLists[numCells + ds->outputNumber] );
    }
}


/* PrependTriSurf  0x0043fe90 */
TriSurf_t *PrependTriSurf( winding_t *w, TriSurfProps_t *props, TriSurf_t **listHead )
{
    TriSurf_t *surf;

    Assert( listHead );

    surf = AllocTriSurf( w, props );
    surf->visGroupPrev = NULL;
    surf->visGroupNext = *listHead;
    if ( surf->visGroupNext )
        surf->visGroupNext->visGroupPrev = surf;
    *listHead = surf;

    return surf;
}


/* Tris_EmitTriSurf_r  0x0043ff10 */
void Tris_EmitTriSurf_r( winding_t *w, TriSurfProps_t *props, Node_t *node )
{
    int        planenum;
    plane_t   *plane;
    int        side;
    winding_t *front;
    vec3_t     negNormal;

    while ( node->cellnum == -2 )
    {
        Assert( node->planenum != PLANENUM_LEAF );

        plane = &mapplanes[node->planenum];
        side  = WindingPlaneSide( w, plane->normal, plane->dist );

        if ( side == SIDE_ON )
            side = ( Vec3Dot( props->plane, plane->normal ) > 0.0f ) ? SIDE_FRONT : SIDE_BACK;

        if ( side == SIDE_CROSS )
        {
            front = CopyWinding( w );
            Vec3Negate( plane->normal, negNormal );
            ClipWindingByPlane( &front, negNormal, -plane->dist, 0.1f );
            ClipWindingByPlane( &w, plane->normal, plane->dist, 0.1f );
            Tris_EmitTriSurf_r( front, props, node->children[1] );
            node = node->children[0];
        }
        else
        {
            node = node->children[side];
        }
    }

    if ( node->cellnum == -1 )
    {
        FreeWinding( w );
        return;
    }

    SanityCheckx( node->cellnum >= 0, "(node->cellnum) = %i", node->cellnum );
    PrependTriSurf( w, props, &triGlobSurfLists[node->cellnum] );
}


/* Tris_TrianglesFormQuad  0x00440c00 */
bool Tris_TrianglesFormQuad( const DrawSurf_t *ds, winding_t **windings, const vec4_t *planes,
                             const vec4_t *texVecs, const vec4_t *lmapVecs,
                             const vec4_t *colorVecs )
{
    if ( !WindingsCoincident( windings[0], planes[0], planes[0][3],
                              windings[1], planes[1], planes[1][3] ) )
        return false;

    if ( !TexVecsMatchOnWindings( windings[0], windings[1], texVecs[0], texVecs[2],
                                  (float)MATERIAL_TEX_WIDTH( ds->mtlTex ) ) )
        return false;
    if ( !TexVecsMatchOnWindings( windings[0], windings[1], texVecs[1], texVecs[3],
                                  (float)MATERIAL_TEX_HEIGHT( ds->mtlTex ) ) )
        return false;

    if ( !TexVecsMatchOnWindings( windings[0], windings[1], lmapVecs[0], lmapVecs[2], 512.0f ) )
        return false;
    if ( !TexVecsMatchOnWindings( windings[0], windings[1], lmapVecs[1], lmapVecs[3], 512.0f ) )
        return false;

    if ( !TexVecsMatchOnWindings( windings[0], windings[1], colorVecs[0], colorVecs[4], 0.00392157f ) )
        return false;
    if ( !TexVecsMatchOnWindings( windings[0], windings[1], colorVecs[1], colorVecs[5], 0.00392157f ) )
        return false;
    if ( !TexVecsMatchOnWindings( windings[0], windings[1], colorVecs[2], colorVecs[6], 0.00392157f ) )
        return false;
    if ( !TexVecsMatchOnWindings( windings[0], windings[1], colorVecs[3], colorVecs[7], 0.00392157f ) )
        return false;

    return true;
}


/* TexVecsMatchOnWindings  0x00440e30 */
static bool TexVecsMatchOnWindings( const winding_t *w0, const winding_t *w1,
                                    const vec4_t v0, const vec4_t v1, float scale )
{
    return TexVecsMatchOnWinding( w0, v0, v1, scale ) &&
           TexVecsMatchOnWinding( w1, v0, v1, scale );
}


/* TexVecsMatchOnWinding  0x00440e90 */
static bool TexVecsMatchOnWinding( const winding_t *w, const vec4_t v0, const vec4_t v1,
                                   float scale )
{
    unsigned int i;

    for ( i = 0; i < w->ptCount; i++ )
    {
        float d0    = Vec3Dot( w->pts[i], v0 ) + v0[3];
        float d1    = Vec3Dot( w->pts[i], v1 ) + v1[3];
        float diff  = d1 - d0;
        float delta = I_fabs( diff );

        if ( delta * scale > 0.001f )
            return false;
    }
    return true;
}


/* WrapLmapVecOffsets  0x00440b90 */
static void WrapLmapVecOffsets( vec4_t *lmapVecs )
{
    float scaled0;
    float scaled1;

    scaled0 = lmapVecs[0][3] * 512.0f;
    lmapVecs[0][3] -= floor( scaled0 ) * 0.001953125f;
    scaled1 = lmapVecs[1][3] * 512.0f;
    lmapVecs[1][3] -= floor( scaled1 ) * 0.001953125f;
}


/* Tris_DegenerateTriangleError  0x00440b00 */
static void Tris_DegenerateTriangleError( int severity, const DrawSurf_t *ds,
                                          const vec3_t *points, const char *message )
{
    struct { unsigned int ptCount; vec3_t pts[3]; } tri;

    tri.ptCount = 3;
    Vec3Copy( points[0], tri.pts[0] );
    Vec3Copy( points[1], tri.pts[1] );
    Vec3Copy( points[2], tri.pts[2] );

    MapErrorWinding( severity, (const winding_t *)&tri,
                     ds->mapInfoIndex, ds->entityNum, ds->brushNum, message );
}


/* ludcmp  0x00440540 */
static bool ludcmp( double *matrix, int *perm )
{
    double scaling[3];
    double big;
    double sum;
    double temp;
    int    i;
    int    j;
    int    k;
    int    imax;

    for ( i = 0; i < 3; i++ )
    {
        big = 0.0;
        for ( j = 0; j < 3; j++ )
        {
            temp = fabs( matrix[i * 3 + j] );
            if ( big < temp )
                big = temp;
        }
        if ( big == 0.0 )
            return false;
        scaling[i] = 1.0 / big;
    }

    imax = 0;
    for ( j = 0; ; j++ )
    {
        for ( i = 0; i < j; i++ )
        {
            sum = matrix[i * 3 + j];
            for ( k = 0; k < i; k++ )
                sum -= matrix[i * 3 + k] * matrix[k * 3 + j];
            matrix[i * 3 + j] = sum;
        }

        big = 0.0;
        for ( i = j; i < 3; i++ )
        {
            sum = matrix[i * 3 + j];
            for ( k = 0; k < j; k++ )
                sum -= matrix[i * 3 + k] * matrix[k * 3 + j];
            matrix[i * 3 + j] = sum;

            temp = fabs( sum ) * scaling[i];
            if ( big < temp )
            {
                big  = temp;
                imax = i;
            }
        }

        if ( matrix[imax * 3 + j] == 0.0 )
            return false;

        if ( j != imax )
        {
            for ( k = 0; k < 3; k++ )
            {
                temp                 = matrix[imax * 3 + k];
                matrix[imax * 3 + k] = matrix[j * 3 + k];
                matrix[j * 3 + k]    = temp;
            }
            scaling[imax] = scaling[j];
        }

        perm[j] = imax;
        if ( j == 2 )
            return true;

        temp = 1.0 / matrix[j * 4];
        for ( i = j + 1; i < 3; i++ )
            matrix[i * 3 + j] *= temp;
    }
}


/* lubksb  0x00440900 */
static void lubksb( const double *luMatrix, const int *perm, double *rhs )
{
    double sum;
    int    i;
    int    j;
    int    ii;

    ii = -1;
    for ( i = 0; i < 3; i++ )
    {
        sum         = rhs[perm[i]];
        rhs[perm[i]] = rhs[i];

        if ( ii < 0 )
        {
            if ( sum != 0.0 )
                ii = i;
        }
        else
        {
            for ( j = ii; j <= i - 1; j++ )
                sum -= luMatrix[i * 3 + j] * rhs[j];
        }
        rhs[i] = sum;
    }

    for ( i = 2; i >= 0; i-- )
    {
        sum = rhs[i];
        for ( j = i + 1; j < 3; j++ )
            sum -= luMatrix[i * 3 + j] * rhs[j];
        rhs[i] = sum / luMatrix[i * 4];
    }
}


/* mprove  0x00440a40 */
static void mprove( const double *origMatrix, const double *luMatrix, const int *perm,
                    const double *origRHS, double *solution )
{
    double residual[3];
    int    i;
    int    j;

    for ( i = 0; i < 3; i++ )
    {
        residual[i] = -origRHS[i];
        for ( j = 0; j < 3; j++ )
            residual[i] += origMatrix[i * 3 + j] * solution[j];
    }

    lubksb( luMatrix, perm, residual );

    for ( i = 0; i < 3; i++ )
        solution[i] -= residual[i];
}


/* SolveTextureVector  0x00440830 */
static void SolveTextureVector( const double *origMatrix, const double *luMatrix,
                                const int *perm, float val0, float val1, float val2,
                                vec4_t out, int axisU, int axisV, int axisDrop )
{
    double rhs[3];
    double solution[3];

    if ( val1 != val0 || val2 != val0 )
    {
        solution[0] = val0;
        solution[1] = val1;
        solution[2] = val2;
        rhs[0]      = val0;
        rhs[1]      = val1;
        rhs[2]      = val2;

        lubksb( luMatrix, perm, solution );
        mprove( origMatrix, luMatrix, perm, rhs, solution );

        out[axisU]    = (float)solution[0];
        out[axisV]    = (float)solution[1];
        out[axisDrop] = 0.0f;
        out[3]        = (float)solution[2];
    }
    else
    {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        out[3] = val0;
    }
}


/* DeriveTextureVectors  0x004400d0 */
bool DeriveTextureVectors( const DrawSurf_t *ds, const vec4_t plane, const vec3_t *points,
                           const vec2_t *texCoords, vec4_t *texVecsOut,
                           const vec2_t *lmapCoords, vec4_t *lmapVecsOut,
                           const byte *colors, vec4_t *colorVecsOut )
{
    double matrix[9];
    double luMatrix[9];
    int    perm[3];
    int    axisDrop;
    int    axisU;
    int    axisV;

    axisDrop = 0;
    if ( fabs( plane[0] ) < fabs( plane[1] ) )
        axisDrop = 1;
    if ( fabs( plane[axisDrop] ) < fabs( plane[2] ) )
        axisDrop = 2;

    axisU = ~axisDrop & 1;
    axisV = ~axisDrop & 2;

    matrix[0] = points[0][axisU];  matrix[1] = points[0][axisV];  matrix[2] = 1.0;
    matrix[3] = points[1][axisU];  matrix[4] = points[1][axisV];  matrix[5] = 1.0;
    matrix[6] = points[2][axisU];  matrix[7] = points[2][axisV];  matrix[8] = 1.0;

    memcpy( luMatrix, matrix, sizeof( luMatrix ) );

    if ( !ludcmp( luMatrix, perm ) )
    {
        Tris_DegenerateTriangleError( 0, ds, points, "Degenerate triangle (ie, point or line)." );
        return false;
    }

    if ( texCoords )
    {
        SolveTextureVector( matrix, luMatrix, perm, texCoords[0][0], texCoords[1][0],
                            texCoords[2][0], texVecsOut[0], axisU, axisV, axisDrop );
        SolveTextureVector( matrix, luMatrix, perm, texCoords[0][1], texCoords[1][1],
                            texCoords[2][1], texVecsOut[1], axisU, axisV, axisDrop );
    }

    if ( lmapCoords )
    {
        SolveTextureVector( matrix, luMatrix, perm, lmapCoords[0][0], lmapCoords[1][0],
                            lmapCoords[2][0], lmapVecsOut[0], axisU, axisV, axisDrop );
        SolveTextureVector( matrix, luMatrix, perm, lmapCoords[0][1], lmapCoords[1][1],
                            lmapCoords[2][1], lmapVecsOut[1], axisU, axisV, axisDrop );
    }

    if ( colors )
    {
        SolveTextureVector( matrix, luMatrix, perm, colors[0], colors[4], colors[8],
                            colorVecsOut[0], axisU, axisV, axisDrop );
        SolveTextureVector( matrix, luMatrix, perm, colors[1], colors[5], colors[9],
                            colorVecsOut[1], axisU, axisV, axisDrop );
        SolveTextureVector( matrix, luMatrix, perm, colors[2], colors[6], colors[10],
                            colorVecsOut[2], axisU, axisV, axisDrop );
        SolveTextureVector( matrix, luMatrix, perm, colors[3], colors[7], colors[11],
                            colorVecsOut[3], axisU, axisV, axisDrop );
    }

    return true;
}


/* Tris_EmitMaterial  0x004437c0 */
LayeredMaterialDesc_t *Tris_EmitMaterial( Material_t * const *materials, int layerCount )
{
    char                   name[MAX_QPATH];
    LayeredMaterialDesc_t *desc;
    MaterialRef_t         *defaultRef;
    Material_t            *defaultMaterials[1];
    int                    lyrMtlDescIndex;
    int                    materialIndex;
    int                    layer;

    Tris_LayeredMaterialKey( materials, layerCount, name );

    for ( lyrMtlDescIndex = 0; lyrMtlDescIndex < triGlobLyrMtlCount; lyrMtlDescIndex++ )
    {
        if ( !strcmp( name, LayeredMaterialName( lyrMtlDescIndex ) ) )
            return LayeredMaterialDesc( lyrMtlDescIndex );
    }

    materialIndex = EmitMaterial( name, materials[0]->surfaceFlags, materials[0]->contentFlags );

    if ( strcmp( bspMaterials[materialIndex].name, name ) )
    {
        Assert( layerCount != 1 || strcmp( name, "$default" ) );
        Assertx( !strcmp( bspMaterials[materialIndex].name, "$default" ),
                 "%s\n\t(dshaders[materialIndex].material) = %s",
                 "(!strcmp( dshaders[materialIndex].material, \"$default\" ))",
                 bspMaterials[materialIndex].name );

        defaultRef          = Material_Register( "$default", MTL_USAGE_WORLD_VCOL );
        defaultMaterials[0] = defaultRef->material;

        return Tris_EmitMaterial( defaultMaterials, 1 );
    }

    if ( triGlobLyrMtlCount == MAX_LAYERED_MATERIALS )
    {
        PrintBSPMaterialList();
        Com_Error( "MAX_MAP_LAYERED_MATERIALS (%i) exceeded -- too many unique materials "
                   "and/or unique combinations of layered materials\n", MAX_LAYERED_MATERIALS );
    }

    SanityCheck( lyrMtlDescIndex == triGlobLyrMtlCount );

    triGlobLyrMtlCount++;

    desc = LayeredMaterialDesc( lyrMtlDescIndex );

    strcpy( desc->name, name );
    desc->mtlIndex       = checked_cast< short >( materialIndex );
    desc->layerCount     = checked_cast< unsigned char >( layerCount );
    desc->normalMapCount = 0;

    for ( layer = 0; layer < layerCount; layer++ )
    {
        desc->mtlRaw[layer] = materials[layer];

        if ( Material_HasNormalMap( materials[layer] ) )
        {
            desc->region[desc->normalMapCount] = checked_cast< unsigned char >( layer );
            desc->normalMapCount++;
        }
    }

    desc->totalArea = 0.0f;
    desc->useCount  = 0;
    desc->refPoints = NULL;

    return desc;
}


/* Tris_LayeredMaterialKey  0x00443a70 */
void Tris_LayeredMaterialKey( Material_t * const *materials, int layerCount, char *out )
{
    unsigned int len;
    int          i;

    if ( layerCount == 1 )
    {
        strcpy( out, StringFromOffset( materials[0] ) );
        return;
    }

    out[0] = '*';
    len    = 1;

    for ( i = 0; i < layerCount; i++ )
    {
        len += sprintf( out + len, "%i",
                        EmitMaterialForSurface( materials[i], materials[i]->contentFlags ) );

        if ( Material_HasNormalMap( materials[i] ) )
        {
            out[len] = 'n';
            len++;
        }
        if ( i + 1 != layerCount )
        {
            out[len] = '_';
            len++;
        }
    }

    Assert( len < MAX_QPATH );
    out[len] = '\0';
}


/* Tris_LayeredMaterialAllNoTile  0x004481e0 */
bool Tris_LayeredMaterialAllNoTile( const LayeredMaterialDesc_t *lyrMtlDesc )
{
    unsigned int layer;

    for ( layer = 0; layer < (unsigned int)LayeredMaterialLayerCount( lyrMtlDesc ); layer++ )
    {
        if ( !( LayeredMaterialLayer( lyrMtlDesc, layer )->toolFlags & MTL_TOOLFLAG_TEXTURE_WRAPS ) )
            return true;
    }
    return false;
}
