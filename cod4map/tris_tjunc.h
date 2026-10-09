/* Original: .\tris_tjunc.cpp */

#ifndef TRIS_TJUNC_H
#define TRIS_TJUNC_H


#include "tris.h"
#include "q_shared.h"
#include "polylib.h"

#define MAX_EDGE_LINES          0x10000
#define MAX_TJUNC_POINTS        0x40000

#define TJUNC_AXIS_BUCKETS      128

#define TJUNC_DIR_BUCKETS       8

#define TJUNC_BIG_EPSILON_SCALE 23.0

#ifndef MAX_POINTS_ON_CONCAVE_WINDING
#define MAX_POINTS_ON_CONCAVE_WINDING   16384
#endif

#define TJUNC_MIN_BOX_SIZE      8.0f
#define TJUNC_MAX_BOX_SURFS     256

#define TJUNC_SIDE_FORWARD      1
#define TJUNC_SIDE_BACKWARD     2
#define TJUNC_SIDE_DEGENERATE   4

typedef struct tjuncPoint_s
{
    float                dist;      /* +0x00 */
    vec3_t               xyz;       /* +0x04 */
    int                  sideFlags; /* +0x10 */
    struct tjuncPoint_s *prev;      /* +0x14 */
    struct tjuncPoint_s *next;      /* +0x18 */
} tjuncPoint_t;

typedef struct edgeLine_s
{
    vec3_t       normal0;       /* +0x00 */
    float        dist0;         /* +0x0c */
    vec3_t       normal1;       /* +0x10 */
    float        dist1;         /* +0x1c */
    vec3_t       dir;           /* +0x20 */
    vec3_t       origin;        /* +0x2c */
    float        epsilonSq;     /* +0x38 */
    struct edgeLine_s *axisBucketNext;  /* +0x3c (an int in the original; it holds a pointer) */
    struct edgeLine_s *dirBucketNext;   /* +0x40 */
    tjuncPoint_t head;          /* +0x44 */
} edgeLine_t;

typedef struct
{
    void  *mem;             /* 0x2b8209dc */
    byte   useAxisBuckets;  /* 0x2b8209e0 */
    int    lineCount;       /* 0x2b8209e4 */
    int    ptCount;         /* 0x2b8209e8 */
    vec3_t mins;            /* 0x2b8209ec */
    vec3_t maxs;            /* 0x2b8209f8 */
    float  epsilonSq;       /* 0x2b820a04 */
    float  bigEpsilonSq;    /* 0x2b820a08 */
} tjuncGlob_t;

extern tjuncGlob_t tjuncGlob;

void TJunc_Init( void );                                            /* 0x0045ce40 */
void TJunc_Shutdown( void );                                        /* 0x0045ceb0 */

void TJunc_SetBounds( const vec3_t mins, const vec3_t maxs );       /* 0x0045e8b0 */

void TJunc_SetEpsilon( float epsilon );                             /* 0x0045e8e0 */
void TJunc_SetUseAxisBuckets( qboolean use );                       /* 0x0045e910 */

void TJunc_FixGrid( const vec3_t mins, const vec3_t maxs );         /* 0x0045e4f0 */

void TJunc_FixSurfaceList( TriSurf_t **surfs, int surfCount,
                           TriSurf_t *extraSurf );                  /* 0x0045ddf0 */

void FixSurfaceJunctions( winding_t **inout, const vec4_t plane );  /* 0x0045e080 */

void TJuncAddSurfListToGrid( TriSurf_t *surf );                     /* 0x0045e870 */

#endif
