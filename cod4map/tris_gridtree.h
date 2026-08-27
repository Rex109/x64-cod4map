/* Original: .\tris_gridtree.cpp */

#ifndef TRIS_GRIDTREE_H
#define TRIS_GRIDTREE_H

#include "q_shared.h"
#include "tris.h"

#define WINDING_TREE_LEAF_DEPTH   20
#define WINDING_TREE_NODE_COUNT   0x1fffff

typedef struct
{
    float             backMax;   /* +0x00 */
    float             frontMin;  /* +0x04 */
    struct TriSurf_s *list;      /* +0x08 */
} GridTreeNode_t;

typedef struct
{
    GridTreeNode_t *root;   /* +0x00 */
    vec3_t          mins;   /* +0x04 */
    vec3_t          maxs;   /* +0x10 */
} GridTreeGlob_t;

extern GridTreeGlob_t gridTreeGlob;   /* 0x2b80e314 */

void GridTree_Init( void );                                         /* 0x00451190 */
void GridTree_Shutdown( void );                                     /* 0x004511e0 */

void SetGridDivisionPoints( const vec3_t mins, const vec3_t maxs );  /* 0x004502a0 */
void SetGridDivisionPoints_r( int nodeIndex, const vec3_t mins,
                              const vec3_t maxs, int depth );        /* 0x00450310 */

void SortGridTree( void );                                           /* 0x00450740 */
void SortGridTree_r( int nodeIndex, const vec3_t mins,
                     const vec3_t maxs, int depth );                 /* 0x00450760 */
void GridTree_MoveAllToRoot( void );                                 /* 0x00450b10 */

int  GridTree_ChildIndex( int nodeIndex, int side );                 /* 0x00450400 */
int  GridTree_ClassifyBounds( const vec3_t mins, const vec3_t maxs,
                              int nodeIndex, int depth );            /* 0x00450560 */
GridTreeNode_t *GridTree_FindNode( const vec3_t mins, const vec3_t maxs ); /* 0x00450480 */

void GridTree_Insert( struct TriSurf_s *entry );                     /* 0x00450440 */
void GridTree_Remove( struct TriSurf_s *entry );                     /* 0x00450650 */
void GridTree_LinkEntry( GridTreeNode_t *node, struct TriSurf_s *entry );   /* 0x004505f0 */
void GridTree_UnlinkEntry( GridTreeNode_t *node, struct TriSurf_s *entry ); /* 0x00450a00 */

void GridTree_ForEach( const vec3_t mins, const vec3_t maxs,
                       void (*callback)( struct TriSurf_s * ) );     /* 0x00450bf0 */
void GridTree_ForEach_r( const vec3_t mins, const vec3_t maxs, int nodeIndex,
                         int depth, void (*callback)( struct TriSurf_s * ) ); /* 0x00450c10 */

int  GridTree_CountOverlapping( const vec3_t mins, const vec3_t maxs, int maxCount ); /* 0x00451030 */
int  GridTree_CountOverlapping_r( const vec3_t mins, const vec3_t maxs, int nodeIndex,
                                  int depth, int maxCount );         /* 0x00451050 */

qboolean GridTree_TraceLine( const vec3_t start, const vec3_t end, void *userData,
                             qboolean (*callback)( struct TriSurf_s *, const vec3_t,
                                                   const vec3_t, void * ) );  /* 0x00450d20 */
qboolean GridTree_TraceLine_r( const vec3_t traceStart, const vec3_t traceEnd,
                               const vec3_t segStart, const vec3_t segEnd,
                               int nodeIndex, int depth, void *userData,
                               qboolean (*callback)( struct TriSurf_s *, const vec3_t,
                                                     const vec3_t, void * ) ); /* 0x00450d50 */

#endif
