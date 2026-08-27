
#ifndef PATCH_H
#define PATCH_H

#include "q_shared.h"
#include "bspfile.h"
#include "map.h"
#include "mesh.h"
#include "surface.h"

#define MAX_PATCH_SIZE          32

#define MAX_LIGHTMAP_SPLITS     32

#define MAX_TERRAIN_LMAP_AXIS   512

#define LMAP_SPLIT_SCALE        0.0021186440717428923

#define PATCH_TEXCOORD_SCALE    0.0009765625

#define PATCH_ST_EPSILON        1e-5f

#define PATCH_WELD_EPSILON      0.01f

#define TERRAIN_BOUNDS_SLOP     0.1f


#ifndef COD4MAP_TERRAINNODE_T_DEFINED
#define COD4MAP_TERRAINNODE_T_DEFINED
typedef struct TerrainNode_s
{
    struct TerrainNode_s *next;          /* +0x00 */
    Material_t           *material;      /* +0x04 */
    Material_t           *lmapMaterial;  /* +0x08 */
    int                   contents;      /* +0x0c */
    int                   toolFlags;     /* +0x10 */
    int                   smoothing;     /* +0x14 */
    int                   cullGroup;     /* +0x18 */
    int                   indexCount;    /* +0x1c */
    unsigned short       *indexes;       /* +0x20 */
    int                   vertCount;     /* +0x24 */
    MeshVert_t           *verts;         /* +0x28 */
} TerrainNode_t;
#endif


extern int noCurveBrushes;                       /* 0x00a34bc0 */

extern float sampleScale;               /* 0x0052907c */


void  ParsePatch( const char **parsePos, int isMesh, const orientation_t *orient,
                  float scale, int mapInfoIndex, int prefabFlags );   /* 0x00428840 */

void  ProcessEntityPatches( Entity_t *entity );                       /* 0x00429160 */
void  PatchMapDrawSurfs( Entity_t *entity );                          /* 0x0042a940 */

DrawSurf_t *DrawSurfaceForTerrain( const TerrainNode_t *terrain,
                                   const parseMesh_t *mesh );         /* 0x0042a4f0 */
DrawSurf_t *DrawSurfaceForPatch( const parseMesh_t *mesh );           /* 0x0042a9f0 */

void  CenterPatchLightmapCoords( DrawSurf_t *surf );                  /* 0x0042a630 */
void  ExpandBounds( float dist, vec3_t mins, vec3_t maxs );           /* 0x0042a830 */
void  PackColor( unsigned int *out, byte r, byte g, byte b, byte a ); /* 0x0042abd0 */

#endif
