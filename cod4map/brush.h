/* Original: .\brush.cpp */

#ifndef BRUSH_H
#define BRUSH_H

#include <stddef.h>

#include "q_shared.h"
#include "polylib.h"
#include "bspfile.h"
#include "brush_edges.h"

struct Brush_s;
struct cmPartList_s;
struct CmCollideBox_s;

#ifndef PLANENUM_LEAF
#define PLANENUM_LEAF   ( -1 )
#endif

#ifndef COD4MAP_TREE_T_DEFINED
#define COD4MAP_TREE_T_DEFINED
struct portal_s;
struct Entity_s;

typedef struct node_s
{
    int              planenum;       /* +0x00 */
    struct node_s   *parent;         /* +0x04 */
    vec3_t           mins;           /* +0x08 */
    vec3_t           maxs;           /* +0x14 */
    struct Brush_s  *volume;         /* +0x20 */
    int              opaque;         /* +0x24 */
    int              pad28;          /* +0x28 */
    struct node_s   *children[2];    /* +0x2c */
    int              tinyportals;    /* +0x34 */
    vec3_t           referencepoint; /* +0x38 */
    int              pad44;          /* +0x44 */
    int              cluster;        /* +0x48 */
    int              area;           /* +0x4c */
    struct Brush_s  *leafBrushes;    /* +0x50 */
    struct cmPartList_s *partitionList; /* +0x54 */
    struct CmCollideBox_s *collisionBoxes; /* +0x58 */
    int              occupied;       /* +0x5c */
    struct Entity_s *occupant;       /* +0x60 */
    int              cellnum;        /* +0x64 */
    struct portal_s *portals;        /* +0x68 */
} Node_t;                            /* sizeof == 0x6c */

typedef struct tree_s
{
    Node_t *headnode;               /* +0x00 */
    Node_t  outside_node;           /* +0x04 */
    vec3_t  mins;                   /* +0x70 */
    vec3_t  maxs;                   /* +0x7c */
} Tree_t;                           /* sizeof == 0x88 */

typedef struct face_s Face_t;
#endif

#include "map.h"


#define MAX_COLLISION_SIDES     256

#define MAX_BRUSH_SIDES         256

#define FIRST_NON_BBOX_SIDE     6

#define MAX_BRUSH_POINTS        0x10000

#define MIN_BRUSH_POINTS        4

#define MAX_POINTS_ON_BRUSH_FACE    64

#define EDGE_LENGTH             0.2

#define SNAP_GRID_SIZE          0.25f
#define SNAP_EPSILON            0.01f

#define PLAYER_CAPSULE_RADIUS       15.0f
#define PLAYER_CAPSULE_HALFHEIGHT   20.0f

#define CONTENTS_BRUSHEDGE_MASK 0x0280E491

#define BRUSH_HEADER_SIZE       ( offsetof( Brush_t, sides ) )

typedef struct
{
    vec3_t          xyz;                /* +0x00 */
    unsigned int    sideCount;          /* +0x0c */
    unsigned int    frontBits[MAX_COLLISION_SIDES / 32];  /* +0x10 */
    unsigned int    behindBits[MAX_COLLISION_SIDES / 32]; /* +0x30 */
} brushPointCapsule_t;                  /* sizeof == 0x50 */


extern int g_removedBrushSides;         /* 0x00534bac */


int      CountBrushList( const Brush_t *brushes );                      /* 0x00402590 */
Brush_t *AllocBrush( int sideCount );                                   /* 0x004025e0 */
void     FreeBrushWindings( Brush_t *brush );                           /* 0x00402630 */
void     FreeBrush( Brush_t *brush );                                   /* 0x00402710 */
void     FreeBrushList( Brush_t *brushes );                             /* 0x00402730 */
Brush_t *CopyBrush( const Brush_t *brush );                             /* 0x00402760 */
Brush_t *CopyCollisionBrush( const Brush_t *brush );                    /* 0x00402890 */

qboolean BoundBrush( Brush_t *brush );                                  /* 0x00402a00 */
qboolean CreateBrushWindings( Brush_t *brush );                         /* 0x00402b10 */
void     CreateBrushAdjacencyWindings( Brush_t *brush );                /* 0x004032a0 */
void     WriteBSPBrushMap( const char *name, const Brush_t *list );     /* 0x00403440 */

Tree_t  *AllocTree( void );                                             /* 0x00403870 */
Node_t  *AllocNode( void );                                             /* 0x004038c0 */

qboolean WindingIsTiny( const winding_t *w );                           /* 0x004038f0 */
void     SplitBrushByPlane( Brush_t *brush, unsigned int planenum,
                            Brush_t **frontBrush, Brush_t **backBrush ); /* 0x00403b40 */
int      TestBrushAgainstPlane( const Brush_t *brush, const vec3_t normal,
                                float dist, float epsilon );            /* 0x00403ca0 */
qboolean BrushContainsPoint( const Brush_t *brush, const vec3_t point ); /* 0x00403e20 */

void     FilterDetailBrushesIntoBspTree( Entity_t *entity, Tree_t *tree );     /* 0x00403610 */
void     FilterStructuralBrushesIntoBspTree( Entity_t *entity, Tree_t *tree ); /* 0x004037d0 */

void     AddBrushNeighborBevels( Brush_t *ebrushes );                   /* 0x00403eb0 */
qboolean BrushBoundsOverlap( const Brush_t *b0, const Brush_t *b1,
                             float epsilon );                           /* 0x004050e0 */
int      FindBrushNeighbors( const Brush_t *refBrush, Brush_t *ebrushes,
                             Brush_t **neighbors, int neighborLimit );  /* 0x00405150 */

#endif
