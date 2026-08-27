/* Original: .\portals.cpp */

#ifndef PORTALS_H
#define PORTALS_H

#include "q_shared.h"
#include "polylib.h"
#include "map.h"
#include "brush.h"

typedef struct portal_s
{
    plane_t          plane;              /* +0x00 */
    struct node_s   *onnode;             /* +0x1c */
    struct node_s   *nodes[2];           /* +0x20 */
    struct portal_s *next[2];            /* +0x28 */
    winding_t       *winding;            /* +0x30 */
    side_t          *manualPortalFace;   /* +0x34 */
    struct Brush_s  *manualPortalBrush;  /* +0x38 */
} portal_t;

typedef struct portalRef_s
{
    side_t             *face;           /* +0x00 */
    struct node_s      *oppositenode;   /* +0x04 */
    struct portalRef_s *next;           /* +0x08 */
} portalRef_t;


#define SIDESPACE           8.0f

#define PORTAL_CLIP_EPSILON     0.1f
#define PORTAL_SPLIT_EPSILON    0.001f

#define SNAP_NORMAL_ZERO        1e-05f
#define SNAP_NORMAL_ONE         0.99999f

#define SELECT_PLANE_EPSILON        0.125f
#define SELECT_PLANE_MIN_EPSILON    0.0005

#define MAX_CELL_PORTAL_LISTS   1024


extern int c_leafsfilled;               /* 0x12ece910 */
extern int c_peak_portals;              /* 0x12ece918 */
extern int c_solidleafs;                /* 0x12ece91c */
extern int c_tinyportals;               /* 0x12ece924 */
extern int c_insideleafs;               /* 0x12ece928 */
extern int c_areas;                     /* 0x12ece92c */
extern int c_floodedleafs;              /* 0x12ece930 */
extern int c_active_portals;            /* 0x12ece934 */

extern portalRef_t *s_cellPortalRefs[MAX_CELL_PORTAL_LISTS];   /* 0x12f0e938 */

extern int debugPortals;        /* 0x12f0f938 */

extern int s_portalErrorCount;          /* 0x12f0f93c */

portal_t *AllocPortal( void );                                          /* 0x0042f000 */
void      FreePortal( portal_t *p );                                    /* 0x0042f060 */
int       Portal_Passable( const portal_t *p );                         /* 0x0042f0a0 */
void      AddPortalToNode( portal_t *p, Node_t *front, Node_t *back );  /* 0x0042f110 */
void      RemovePortalFromNode( portal_t *portal, Node_t *l );          /* 0x0042f170 */

void       MakeTreePortals( Tree_t *tree );                             /* 0x0042f230 */
winding_t *CreatePortalWinding( const Node_t *node );                   /* 0x0042f530 */
void       MakeNodePortal( Node_t *node );                              /* 0x0042f620 */
void       SplitNodePortals( Node_t *node );                            /* 0x0042f790 */
void       CalcNodeBounds( Node_t *node );                              /* 0x0042fb00 */
void       MakeTreePortals_r( Node_t *node );                           /* 0x0042fba0 */
void       ProcessBspTree( Tree_t *tree );                              /* 0x0042fcc0 */

int  FloodEntities( Tree_t *tree );                                     /* 0x0042fd00 */
int  PlaceOccupant( Node_t *headnode, const vec3_t origin, Entity_t *occupant ); /* 0x0042fe60 */
void FloodPortals_r( Node_t *node, int dist );                          /* 0x0042fef0 */
int  IsExcludedEntity( const char *classname );                         /* 0x0042ff80 */

void FloodAreas_r( Node_t *node );                                      /* 0x0042ffd0 */
void FindAreas_r( Node_t *node );                                       /* 0x00430060 */
void FloodAreas( Tree_t *tree );                                        /* 0x004300c0 */
void CheckAreas_r( const Node_t *node );                                /* 0x00430110 */

void FillOutside_r( Node_t *node );                                     /* 0x00430180 */
void FillOutside( Node_t *headnode );                                   /* 0x00430230 */

void PortalWarning( const char *message, const winding_t *w, const Brush_t *brush ); /* 0x004305d0 */
void SnapNormal( vec3_t normal );                                       /* 0x004313e0 */

void BuildSidePlanes( Brush_t *brushList, Tree_t *tree );               /* 0x00430be0 */
void FilterBrushIntoTree( portal_t *portal, const winding_t *brushFace,
                          Brush_t *realPortalBrush, side_t *realPortalFace ); /* 0x004310f0 */
void FilterBrushIntoTree_r( side_t *face, Brush_t *brush, const vec4_t *sidePlanes,
                            Node_t *node, Tree_t *tree );               /* 0x00430ec0 */

void PortalizeNode( Tree_t *tree, int leaked );                         /* 0x004314b0 */
void InitBspTreeNodes( Node_t *node );                                  /* 0x00431550 */
void FloodOutsideCells_r( Node_t *node );                               /* 0x00431600 */
void NumberCells_r( Node_t *node, Tree_t *tree );                       /* 0x00431690 */
void FloodCell_r( Node_t *node, int depth, Tree_t *tree );              /* 0x00431720 */
void CheckPortalCells_r( Node_t *node );                                /* 0x004318d0 */
int  PropagateCellnum_r( Node_t *node );                                /* 0x004319d0 */

void PortalizeWorld( Brush_t *brushList, Tree_t *tree, int leaked );    /* 0x00430b90 */

void GetLeafBrushes( Entity_t *ent, Node_t *headnode );                 /* 0x004302b0 */
void CollectCellPortals_r( Node_t *node );                              /* 0x00430320 */
void AddPortalRefToCell( const portal_t *p, int side, int cellnum );    /* 0x004303f0 */
void EmitPortalsAndCells( void );                                       /* 0x00430610 */
int  SelectPortalPlane( const side_t *face, const Node_t *node );       /* 0x00430910 */
int  SelectSplitPlane( int planenum, const Node_t *node, float epsilon ); /* 0x004309b0 */

void RemapPortalPlanes( const int *planeMap );                          /* 0x00430a80 */

void WritePortalMapFile( Tree_t *tree );                                /* 0x00431a30 */
void WritePortalMapFile_r( const Node_t *node, FILE *f );               /* 0x00431b00 */
void WritePortalBrush( const winding_t *w, const portal_t *p,
                       const char *material, FILE *f );                 /* 0x00431bd0 */
void WriteBrushFace( const vec3_t *verts, const char *material, FILE *f ); /* 0x00431d50 */


#define PORTALFILE      "PRT1"

extern int   num_visclusters;           /* 0x12f19ca8 */
extern int   num_solidfaces;            /* 0x12f19cac */
extern int   num_visportals;            /* 0x12f19cb0 */
extern FILE *pf;                        /* 0x12f19cb4 */

void WriteFloat( FILE *f, float v );                                    /* 0x00439400 */
void WritePortalFile_r( const Node_t *node );                           /* 0x00439480 */
void NumberLeafs_r( Node_t *node );                                     /* 0x00439680 */
void NumberClusters( Tree_t *tree );                                    /* 0x00439780 */
void WritePortalFile( Tree_t *tree );                                   /* 0x00439800 */
void WriteFaceFile_r( const Node_t *node );                             /* 0x00439930 */

#endif
