/* Original: .\tris_mergeconcave.cpp */

#ifndef TRIS_MERGECONCAVE_H
#define TRIS_MERGECONCAVE_H


#include "tris.h"
#include "q_shared.h"
#include "polylib.h"


#ifndef MAX_POINTS_ON_CONCAVE_WINDING
#define MAX_POINTS_ON_CONCAVE_WINDING   16384
#endif

#define MAX_HOLES               ( ( MAX_POINTS_ON_CONCAVE_WINDING - 3 ) / 5 )

#define MAX_WINDINGS_PER_CONCAVE_GROUP  0x8000

#define MAX_MERGE_INDICES       0x400

#define MERGE_EPSILON           0.1f
#define MERGE_WEDGE_BIAS        0.01f
#define MERGE_GRID_EXPAND       0.01f
#define MERGE_EQUAL_EPSILON     0.001f

#define MERGE_TINY_AREA         0.0625f
#define MERGE_AREA_PER_POINT    64.001f

typedef struct
{
    int ptCount;        /* +0x00 */
    int pts[1];         /* +0x04 */
} intWinding_t;

typedef bool ( *MergeGroupableCallback_t )( const TriSurf_t *surf0, const TriSurf_t *surf1 );

typedef int ( *MergeCallback_t )( TriSurf_t **surfs, intWinding_t **intWindings,
                                  int surfCount, winding_t **extraWinding,
                                  intWinding_t *extraIntWinding, int mode );

/* mergeGlob  0x2b8109a4 */
typedef struct
{
    MergeGroupableCallback_t GroupableCallback;  /* 0x2b8109a4 */
    int          mode;                           /* 0x2b8109a8 */
    TriSurf_t  **visGroupList;                   /* 0x2b8109ac */
    int          checkCount;                     /* 0x2b8109b0 */
    TriSurf_t   *checkSurf;                      /* 0x2b8109b4 */
    TriSurf_t  **surfs;                          /* 0x2b8109b8 */
    int          surfLimit;                      /* 0x2b8109bc */
    int          surfCount;                      /* 0x2b8109c0 */
    vec3_t      *vertMap;                        /* 0x2b8109c4 */
    unsigned int vertCount;                      /* 0x2b8109c8 */
} mergeGlob_t;

extern mergeGlob_t mergeGlob;


void Merge_Init( unsigned int surfLimit, MergeGroupableCallback_t callback, int mode ); /* 0x00459850 */
void Merge_Shutdown( void );                                                /* 0x00459910 */

void MergeConcaveWindings( TriSurf_t **visGroupList, const vec3_t mins,
                           const vec3_t maxs, MergeCallback_t callback );   /* 0x00457ed0 */

void MergeSurfaceIntoNeighbours( TriSurf_t **visGroupList, const vec3_t mins,
                                 const vec3_t maxs,
                                 qboolean ( *skip )( TriSurf_t * ),
                                 MergeCallback_t callback );                /* 0x00459570 */

void TetherHolesToWinding( TriSurf_t *surf );                               /* 0x004558b0 */

void     Merge_PushSurf( TriSurf_t *surf, TriSurf_t **visGroupList );       /* 0x00457c90 */
void     Merge_AppendSurf( TriSurf_t *surf, TriSurf_t **visGroupList );     /* 0x00457d20 */
void     Merge_Clear( void );                                              /* 0x00457c60 */
qboolean Merge_HasSurfs( void );                                           /* 0x00457c70 */
int      Merge_SurfCount( void );                                          /* 0x00457da0 */
void     Merge_TruncateTo( int mark );                                     /* 0x00457db0 */
void     Merge_LinkSurfsToVisGroup( TriSurf_t **visGroupList );            /* 0x00457bd0 */

int           MergeRemoveDegenerateIndices( int ptCount, int *indices );   /* 0x004564b0 */

intWinding_t *AllocIntWinding( int ptCount );                              /* 0x004565f0 */
void          FreeIntWinding( intWinding_t *iw );                          /* 0x00456680 */
intWinding_t *CopyIntWinding( const intWinding_t *iw );                    /* 0x004566c0 */
const float  *MergeVertForIndex( unsigned int index );                     /* 0x00456730 */

int           MergeFindSharedEdge( const intWinding_t *w0, const intWinding_t *w1,
                                   int *start0, int *start1 );              /* 0x00456ae0 */
void          MergeRemoveDuplicateIndices( intWinding_t *w0, int start,
                                           int dupCount );                  /* 0x00456c70 */
intWinding_t *MergeJoinIntWindings( const intWinding_t *w0, const intWinding_t *w1,
                                    int surfIndex0, int surfIndex1,
                                    int start0, int start1, int dupCount );  /* 0x00456e80 */

#endif
