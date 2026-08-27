/* Original: .\mesh.cpp */

#ifndef MESH_H
#define MESH_H


#include "surface.h"
#include "q_shared.h"

#define MAX_EXPANDED_AXIS   512

#define NUM_NEIGHBORS       8

#define MESH_LINEAR_EPSILON 0.1

#define MIN_NORMAL_LENGTH   0.001


#ifndef COD4MAP_MESHVERT_T_DEFINED
#define COD4MAP_MESHVERT_T_DEFINED
typedef DrawVert_t MeshVert_t;
#endif


#ifndef COD4MAP_MESH_T_DEFINED
#define COD4MAP_MESH_T_DEFINED
typedef struct
{
    int         width;      /* +0x00 */
    int         height;     /* +0x04 */
    int         reserved;   /* +0x08 */
    MeshVert_t *verts;      /* +0x0c */
    int         reserved2;  /* +0x10 */
} Mesh_t;
#endif


extern MeshVert_t expand[MAX_EXPANDED_AXIS][MAX_EXPANDED_AXIS];   /* 0x123ce908 */


void    LerpDrawVert( const MeshVert_t *a, const MeshVert_t *b,
                      MeshVert_t *out );                             /* 0x00426320 */
void    LerpDrawVertAmount( const MeshVert_t *a, const MeshVert_t *b,
                            float amount, MeshVert_t *out );         /* 0x00427da0 */

void    FreeMesh( void *mesh );                                      /* 0x00426430 */
Mesh_t *CopyMesh( const Mesh_t *src );                               /* 0x00426450 */
Mesh_t *TransposeMesh( Mesh_t *inMesh );                             /* 0x004264c0 */
void    MirrorMesh( Mesh_t *mesh );                                  /* 0x004265a0 */

void    MakeMeshNormals( Mesh_t *inMesh );                           /* 0x004266a0 */
int     GuessPatchPlane( const Mesh_t *inMesh, vec4_t plane );       /* 0x00426b30 */
void    PutMeshOnCurve( Mesh_t mesh );                               /* 0x00426e80 */

Mesh_t *SubdivideMesh( Mesh_t inMesh, float maxError, float minLength,
                       int *widthTable, int *heightTable );          /* 0x00427040 */

Mesh_t *RemoveLinearMeshColumnsRows( const Mesh_t *inMesh,
                                     int *widthTable,
                                     int *heightTable );             /* 0x00427940 */

Mesh_t *SubdivideMeshQuads( const Mesh_t *inMesh, float subdivSize, int maxsize,
                            int *widthTable, int *heightTable,
                            int *widthArr, int *heightArr );         /* 0x00427ff0 */

void    PrintCurve( const vec3_t *points );                          /* 0x00428680 */

#endif
