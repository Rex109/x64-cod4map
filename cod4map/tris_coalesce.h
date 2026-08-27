/* Original: .\tris_coalesce.cpp */

#ifndef TRIS_COALESCE_H
#define TRIS_COALESCE_H


#include "tris.h"
#include "q_shared.h"
#include "fixedpoint.h"


#define MAX_COINCIDENT_WINDINGS     8

#define COALESCE_EPSILON_FIXED      0x333
#define COALESCE_EPSILON            0.05f

#define COALESCE_MIN_DOT            0.99f

#define COALESCE_LEAF_SURFS         16
#define COALESCE_SPLIT_ATTEMPTS     10
#define COALESCE_FORCE_LEAF_SURFS   256

#define COALESCE_PROPS_HASH_SIZE    256

typedef struct windingSurfList_s
{
    struct windingSurfList_s *next;     /* +0x00 */
    TriSurf_t                *surf;     /* +0x04 */
} WindingSurfList_t;

typedef struct windingSurfBspNode_s
{
    int axis;                                   /* +0x00 */
    union
    {
        int                 dist;               /* +0x04 */
        WindingSurfList_t  *list;               /* +0x04 */
    };
    struct windingSurfBspNode_s *children[2];   /* +0x08 */
} WindingSurfBspNode_t;                         /* sizeof == 0x10 */

typedef int ( *CoalesceLeafFunc_t )( struct windingSurfBspNode_s *leaf, TriSurf_t *surf );

/* coalesceGlob  0x2b80df00 */
typedef struct
{
    TriSurfProps_t       *propsHash[COALESCE_PROPS_HASH_SIZE];  /* 0x2b80df00 */
    WindingSurfBspNode_t *tree;                                 /* 0x2b80e300 */
    TriSurf_t           **listHead;                             /* 0x2b80e304 */
    short                 checkCount;                           /* 0x2b80e308 */
    int                   disableMerge;                         /* 0x2b80e30c */
} coalesceGlob_t;

extern coalesceGlob_t coalesceGlob;


void TrisCoalesce( TriSurf_t **listHead );                          /* 0x0044d490 */

void ReportFloatingSurface( TriSurf_t *surf );                      /* 0x0044d420 */

CoalesceNode_t *CopyCoalesceChain( const CoalesceNode_t *chain );  /* 0x0044d3b0 */

void CoalesceClearPropsHash( void );                                /* 0x0044f1a0 */

void FixedVec3Mid( const fixedvec3_t a, const fixedvec3_t b, fixedvec3_t out );  /* 0x0044f1c0 */
int  FixedMid( int a, int b );                                      /* 0x0044f220 */

#endif
