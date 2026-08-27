
#ifndef WRITEBSP_H
#define WRITEBSP_H

#include "q_shared.h"
#include "bspfile.h"
#include "map.h"
#include "brush.h"

#define BRUSH_AXIAL_SIDES   6

struct CmPartition_s;

typedef struct CmCollideBox_s
{
    vec3_t                 mins;       /* +0x00 */
    vec3_t                 maxs;       /* +0x0c */
    struct CmPartition_s  *partition;  /* +0x18 */
    struct CmCollideBox_s *children;   /* +0x1c */
    struct CmCollideBox_s *next;       /* +0x20 */
} CmCollideBox_t;                      /* sizeof == 0x24 */


int  EmitMaterial( const char *materialName, int surfaceFlags, int contentFlags ); /* 0x00463c80 */

int  EmitMaterialForSurface( Material_t *mtlRaw, int contentFlags );    /* 0x00463dc0 */

void EmitPlanes( void );                                             /* 0x00463e30 */
void RemapNodePlanes( const int *planeMap );                         /* 0x00464370 */
void RemapBrushSidePlanes( const int *planeMap );                    /* 0x00464b50 */

void EmitBrushes( Brush_t *brushes );                                /* 0x004652c0 */

int  EmitDrawNode_r( Node_t *node );                                 /* 0x00463f40 */
void EmitLeaf( Node_t *node );                                       /* 0x004640d0 */

int  EmitCollisionAABBs_r( CmCollideBox_t *boxList );                 /* 0x00464200 */

void SetBrushModelNumbers( void );                                   /* 0x00464460 */

void SetPhysicsMassProperties( void );                               /* 0x00464670 */

void BeginBSPFile( void );                                           /* 0x004649a0 */

void EndBSPFile( void );                                             /* 0x004649f0 */

void BeginModel( void );                                             /* 0x00464c80 */
void BeginPhysicsModel( void );                                      /* 0x00464f70 */
void EndModel( Node_t *headnode );                                   /* 0x00465120 */
void EndPhysicsModel( Node_t *headnode );                            /* 0x004656e0 */

unsigned int GetBrushModelIndex( unsigned int entIndex );            /* 0x00464d10 */
unsigned int GetPhysicsModelIndex( unsigned int entIndex );          /* 0x00464ff0 */

#endif
