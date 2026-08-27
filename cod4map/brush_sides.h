/* Original: .\brush_sides.cpp */

#ifndef BRUSH_SIDES_H
#define BRUSH_SIDES_H

#include "q_shared.h"
#include "polylib.h"
#include "bspfile.h"
#include "brush.h"

typedef struct brushSideNode_s
{
    Brush_t               **brushes;    /* +0x00 */
    int                     brushCount; /* +0x04 */
    struct brushSideNode_s *children;   /* +0x08 */
    int                     childCount; /* +0x0c */
    vec3_t                  mins;       /* +0x10 */
    vec3_t                  maxs;       /* +0x1c */
} brushSideNode_t;                      /* sizeof == 0x28 */

typedef struct
{
    bool             isTreeInitialized;  /* +0x00 */
    bool             hasTree;            /* +0x01 */
    Brush_t        **opaqueBrushes;      /* +0x04 */
    brushSideNode_t *tree;               /* +0x08 */
    int              clipCounter;        /* +0x0c */
} brushSideGlob_t;

extern brushSideGlob_t brushSideGlob;   /* 0x00a34bb0 */

void BrushSides_BeginEntity( Entity_t *entity );        /* 0x00407820 */
void BrushSides_Shutdown( void );                       /* 0x00407cf0 */
void BrushSides_BuildTree( Entity_t *entity );          /* 0x00407df0 */
void BrushSides_EndEntity( Entity_t *entity );          /* 0x00408510 */

winding_t *BrushSides_ClipWinding( winding_t *w, const void *owner,
                                   const vec3_t normal );   /* 0x00408750 */
qboolean   BrushSides_IsWindingVisible( winding_t *w );     /* 0x004087f0 */

#endif
