/* Original: .\collision.cpp */

#ifndef COLLISION_H
#define COLLISION_H

#include "q_shared.h"
#include "bspfile.h"
#include "map.h"
#include "brush.h"
#include "surface.h"
#include "writebsp.h"

#define MAX_COLLISION_TRIS        0x20000
#define MAX_COLLISION_VERTS       0x10000
#define MAX_COLLISION_EDGES       0x60000
#define MAX_COLLISION_PARTS       0x20000

#define COLLISION_EDGE_HASH_SIZE  1024

#define CM_MIN_TRIS_PER_LEAF      5

#define CM_MAX_PARTITION_EXTENT   64.0f

#define CM_OVERLAP_RATIO          0.5f

#define SUBDIVIDE_FAILURE_ALLOWED   0
#define SUBDIVIDE_FAILURE_FORBIDDEN 1

#define CM_CONVEX_EPSILON_SQ      6.4000000854491645e-09

#define CM_WALKABLE_NORMAL_Z      0.7f

#define CM_ALPHA_DOMINANT         0x80

typedef struct
{
    int    partitionId;   /* +0x00 */
    vec3_t xyz;           /* +0x04 */
} CmVert_t;

typedef struct CollisionEdge_s
{
    int                     useCount;      /* +0x00 */
    int                     walkable;      /* +0x04 */
    int                     convex;        /* +0x08 */
    int                     needsBorder;   /* +0x0c */
    int                     contents;      /* +0x10 */
    float                   normalZ;       /* +0x14 */
    int                     vertIndices[2];/* +0x18 */
    int                     oppositeVert;  /* +0x20 */
    struct CollisionEdge_s *hashNext;      /* +0x24 */
} CollisionEdge_t;

typedef struct
{
    int            partitionId;    /* +0x00 */
    vec3_t         mins;           /* +0x04 */
    vec3_t         maxs;           /* +0x10 */
    DrawSurf_t    *ds;             /* +0x1c */
    vec4_t         plane;          /* +0x20 */
    vec4_t         svec;           /* +0x30 */
    vec4_t         tvec;           /* +0x40 */
    int            edgeIndices[3]; /* +0x50 */
    int            vertHash;       /* +0x5c */
    unsigned short vertIndices[3]; /* +0x60 */
    byte           alpha;          /* +0x66 */
    byte           pad67;          /* +0x67 */
} CollisionTri_t;

typedef struct CmPartition_s
{
    vec3_t          mins;       /* +0x00 */
    vec3_t          maxs;       /* +0x0c */
    int             triCount;   /* +0x18 */
    CollisionTri_t *tris;       /* +0x1c */
    int             partIndex;  /* +0x20 */
} CmPartition_t;

typedef struct
{
    float sideMins[2];   /* +0x00 */
    float sideMaxs[2];   /* +0x08 */
    int   sideCount[2];  /* +0x10 */
    int   unusedCount;   /* +0x18 */
} cmSplit_t;

typedef struct cmPartList_s
{
    struct cmPartList_s *next;        /* +0x00 */
    int                  partIndex;   /* +0x04 */
} cmPartList_t;

typedef struct
{
    byte             initialized;                          /* 0x11ba5d58 */

    int              firstTri;                             /* 0x11ba5d5c */
    int              maxTris;                              /* 0x11ba5d60 */
    int              triCount;                             /* 0x11ba5d64 */
    CollisionTri_t  *tris;                                 /* 0x11ba5d68 */

    int              firstVert;                            /* 0x11ba5d6c */
    int              maxVerts;                             /* 0x11ba5d70 */
    int              vertCount;                            /* 0x11ba5d74 */
    CmVert_t        *verts;                                /* 0x11ba5d78 */

    int              firstEdge;                            /* 0x11ba5d7c */
    int              maxEdges;                             /* 0x11ba5d80 */
    int              edgeCount;                            /* 0x11ba5d84 */
    int              unusedEdgeCounter;                    /* 0x11ba5d88 */
    CollisionEdge_t *edges;                                /* 0x11ba5d8c */
    CollisionEdge_t *edgeHash[COLLISION_EDGE_HASH_SIZE];   /* 0x11ba5d90 */

    int              firstPart;                            /* 0x11ba6d90 */
    int              maxParts;                             /* 0x11ba6d94 */
    int              partCount;                            /* 0x11ba6d98 */
    CmPartition_t   *parts;                                /* 0x11ba6d9c */

    int              minTrisPerLeaf;                       /* 0x11ba6da0 */
    vec3_t           maxPartitionExtent;                   /* 0x11ba6da4 */
    float            overlapRatio;                         /* 0x11ba6db0 */

    int              c_dupTris;                            /* 0x11ba6db4 */
    int              c_hashMisses;                         /* 0x11ba6db8 */
    int              c_contentsMisses;                     /* 0x11ba6dbc */
    int              c_vertMisses;                         /* 0x11ba6dc0 */
} collGlob_t;

extern collGlob_t collGlob;      /* 0x11ba5d58 */

extern int g_collNodeLimit;      /* 0x00529078 */


void EmitLeafBrushes( Entity_t *ent, Tree_t *tree );                 /* 0x0040f010 */

void CM_ParseCollNodeLimit( const Entity_t *ent );                   /* 0x004121b0 */

void CM_EmitCollisionVerts( void );                                  /* 0x00411ca0 */
void CM_EmitCollisionPartitions( void );                             /* 0x00411d30 */

#endif
