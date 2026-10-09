/* Original: .\tris_tjunc.cpp */

#include "tris_tjunc.h"

#include "cod4map.h"
#include "tris.h"
#include "tris_gridtree.h"

#include <stdlib.h>
#include <string.h>


extern void GridTree_Insert( TriSurf_t *surf );                        /* 0x00450440 */
extern void GridTree_ForEach( const vec3_t mins, const vec3_t maxs,
                                      void ( *callback )( TriSurf_t * ) ); /* 0x00450bf0 */
extern int  GridTree_CountOverlapping( const vec3_t mins, const vec3_t maxs,
                                    int limit );                        /* 0x00451030 */


tjuncGlob_t tjuncGlob;      /* 0x2b8209dc */


/* One block of memory holds the lines, the axis buckets, the points and the direction
   buckets one after another.  The offsets were 0x0, 0x600000, 0x630000 and 0xd30000 in the
   32-bit build; they follow from the sizes so they also hold with 8 byte pointers. */
#define TJUNC_AXIS_OFFSET   ( MAX_EDGE_LINES * sizeof( edgeLine_t ) )
#define TJUNC_POINTS_OFFSET ( TJUNC_AXIS_OFFSET \
                              + 3 * TJUNC_AXIS_BUCKETS * TJUNC_AXIS_BUCKETS * sizeof( edgeLine_t * ) )
#define TJUNC_DIR_OFFSET    ( TJUNC_POINTS_OFFSET + MAX_TJUNC_POINTS * sizeof( tjuncPoint_t ) )

#define TJUNC_LINES         ( ( edgeLine_t * )( ( byte * )tjuncGlob.mem + 0x000000 ) )
#define TJUNC_AXIS_BUCKET( axis, t, s ) \
    ( *( edgeLine_t ** )( ( byte * )tjuncGlob.mem + TJUNC_AXIS_OFFSET + \
                          ( ( ( axis ) * TJUNC_AXIS_BUCKETS + ( t ) ) * TJUNC_AXIS_BUCKETS + ( s ) ) \
                          * sizeof( edgeLine_t * ) ) )
#define TJUNC_POINTS        ( ( tjuncPoint_t * )( ( byte * )tjuncGlob.mem + TJUNC_POINTS_OFFSET ) )
#define TJUNC_DIR_BUCKET( face, t, s ) \
    ( *( edgeLine_t ** )( ( byte * )tjuncGlob.mem + TJUNC_DIR_OFFSET + \
                          ( ( ( face ) * TJUNC_DIR_BUCKETS + ( t ) ) * TJUNC_DIR_BUCKETS + ( s ) ) \
                          * sizeof( edgeLine_t * ) ) )

#ifndef _WIN64
typedef char tjunc_offsets_match_32bit_layout[ ( TJUNC_AXIS_OFFSET == 0x600000
                                                 && TJUNC_POINTS_OFFSET == 0x630000
                                                 && TJUNC_DIR_OFFSET == 0xd30000 ) ? 1 : -1 ];
#endif


static void TJuncAddEdge( const vec3_t v0, const vec3_t v1 );
static void TJuncAddPointsToLine( const vec3_t v0, const vec3_t v1, edgeLine_t *line,
                                  float epsilonSq );
static void TJuncAddSurfaceListEdges( TriSurf_t **surfs, int surfCount );
static void TJuncAddWindingEdges( winding_t *w );
static edgeLine_t **TJuncAxisBucket( const vec3_t v, unsigned int axis );
static int TJuncAxisForEdge( const vec3_t v0, const vec3_t v1 );
static void TJuncClearBuckets( void );
static void TJuncClearLines( void );
static void TJuncDirBucket( const vec3_t dir, int *face, int *s, int *t );
static edgeLine_t *TJuncDirBucketHead( int face, int s, int t );
static float TJuncFindBestLineInBucket( const vec3_t v0, const vec3_t v1,
                                        int face, int s, int t,
                                        float epsilonSq, edgeLine_t **best );
static edgeLine_t *TJuncFindLine( const vec3_t v0, const vec3_t v1, int axis, float *epsilonSq );
static edgeLine_t *TJuncFindLineByDir( const vec3_t v0, const vec3_t v1, const vec3_t dir,
                                       float epsilonSq );
static edgeLine_t *TJuncFindLineInAxisBucket( const vec3_t v0, const vec3_t v1, int axis,
                                              float epsilonSq );
static edgeLine_t *TJuncFindLineInDirBuckets( const vec3_t v0, const vec3_t v1,
                                              const vec3_t dir, float epsilonSq );
static edgeLine_t *TJuncFindLineLinear( const vec3_t v0, const vec3_t v1, float epsilonSq );
static void TJuncFixAndCleanSurface( TriSurf_t *surf );
static void TJuncFixSurface( TriSurf_t *surf );
static float TJuncLineError( const edgeLine_t *line, const vec3_t v0, const vec3_t v1,
                             float best );
static void TJuncMergePoint( const vec3_t v, int sideFlags, const edgeLine_t *line,
                             tjuncPoint_t *pt );
static void TJuncRemoveDuplicatePoints( TriSurf_t *surf );
static void TJuncRemoveDuplicateWindingPoints( winding_t *w );
static void TJuncSubdivide( const vec3_t mins, const vec3_t maxs,
                            void ( *callback )( TriSurf_t * ) );
static void TJuncResetPoints( void );
static void TJunc_AddEdgeLine( const vec3_t v0, const vec3_t v1, int axis, float epsilonSq );
static void TJunc_InsertPoint( const vec3_t v, float dist, int sideFlags, edgeLine_t *line );

/* TJunc_Init  0x0045ce40 */
void TJunc_Init( void )
{
    Assert( tjuncGlob.mem == NULL );

    tjuncGlob.mem = malloc( MAX_EDGE_LINES * sizeof( edgeLine_t ) +
                            3 * TJUNC_AXIS_BUCKETS * TJUNC_AXIS_BUCKETS * sizeof( edgeLine_t * ) +
                            MAX_TJUNC_POINTS * sizeof( tjuncPoint_t ) +
                            3 * TJUNC_DIR_BUCKETS * TJUNC_DIR_BUCKETS * sizeof( edgeLine_t * ) );
    if ( !tjuncGlob.mem )
        Com_Error( "Out of memory" );

    memset( tjuncGlob.mem, 0, MAX_EDGE_LINES * sizeof( edgeLine_t ) +
                              3 * TJUNC_AXIS_BUCKETS * TJUNC_AXIS_BUCKETS * sizeof( edgeLine_t * ) +
                              MAX_TJUNC_POINTS * sizeof( tjuncPoint_t ) +
                              3 * TJUNC_DIR_BUCKETS * TJUNC_DIR_BUCKETS * sizeof( edgeLine_t * ) );
}


/* TJunc_Shutdown  0x0045ceb0 */
void TJunc_Shutdown( void )
{
    free( tjuncGlob.mem );
    tjuncGlob.mem = NULL;
}


/* TJuncAddSurfaceEdges  0x0045ced0 */
static void TJuncAddSurfaceEdges( TriSurf_t *surf )
{
    int i;

    TJuncAddWindingEdges( surf->w );

    for ( i = 0; i < surf->holeCount; i++ )
        TJuncAddWindingEdges( surf->holes[i] );
}


/* TJuncAddWindingEdges  0x0045cf20 */
static void TJuncAddWindingEdges( winding_t *w )
{
    unsigned int prev;
    unsigned int i;

    Assert( w );

    prev = w->ptCount - 1;
    for ( i = 0; i < w->ptCount; i++ )
    {
        TJuncAddEdge( w->pts[prev], w->pts[i] );
        prev = i;
    }
}


/* TJuncAddEdge  0x0045cfb0 */
static void TJuncAddEdge( const vec3_t v0, const vec3_t v1 )
{
    edgeLine_t *line;
    float       epsilonSq;
    int         axis;

    axis = TJuncAxisForEdge( v0, v1 );
    line = TJuncFindLine( v0, v1, axis, &epsilonSq );

    if ( !line )
        TJunc_AddEdgeLine( v0, v1, axis, epsilonSq );
    else
        TJuncAddPointsToLine( v0, v1, line, epsilonSq );
}


/* TJuncAddPointsToLine  0x0045d030 */
static void TJuncAddPointsToLine( const vec3_t v0, const vec3_t v1, edgeLine_t *line,
                                  float epsilonSq )
{
    float dist0;
    float dist1;
    float delta;
    int   sideFlags;

    dist0 = Vec3Dot( v0, line->dir );
    dist1 = Vec3Dot( v1, line->dir );
    delta = dist1 - dist0;

    if ( delta * delta >= epsilonSq )
        sideFlags = ( delta >= 0.0f ) ? TJUNC_SIDE_FORWARD : TJUNC_SIDE_BACKWARD;
    else
        sideFlags = TJUNC_SIDE_DEGENERATE;

    if ( epsilonSq < line->epsilonSq )
        line->epsilonSq = epsilonSq;

    TJunc_InsertPoint( v0, dist0, sideFlags, line );
    TJunc_InsertPoint( v1, dist1, sideFlags, line );
}


/* TJunc_InsertPoint  0x0045d100 */
static void TJunc_InsertPoint( const vec3_t v, float dist, int sideFlags, edgeLine_t *line )
{
    tjuncPoint_t *pt;
    tjuncPoint_t *cur;
    float         delta;

    if ( tjuncGlob.ptCount == MAX_TJUNC_POINTS )
        Com_Error( "MAX_TJUNC_POINTS" );

    pt = &TJUNC_POINTS[ tjuncGlob.ptCount ];
    pt->dist = dist;
    Vec3Copy( v, pt->xyz );
    pt->sideFlags = sideFlags;

    for ( cur = line->head.next; cur != &line->head; cur = cur->next )
    {
        delta = cur->dist - pt->dist;
        if ( delta * delta < line->epsilonSq )
        {
            TJuncMergePoint( v, sideFlags, line, cur );
            return;
        }

        if ( pt->dist < cur->dist )
            break;
    }

    tjuncGlob.ptCount++;

    pt->prev = cur->prev;
    pt->next = cur;
    cur->prev->next = pt;
    cur->prev = pt;
}


/* TJuncMergePoint  0x0045d210 */
static void TJuncMergePoint( const vec3_t v, int sideFlags, const edgeLine_t *line,
                             tjuncPoint_t *pt )
{
    vec2_t existing;
    vec2_t candidate;

    pt->sideFlags |= sideFlags;

    existing[0]  = Vec3Dot( pt->xyz, line->normal0 ) - line->dist0;
    existing[1]  = Vec3Dot( pt->xyz, line->normal1 ) - line->dist1;
    candidate[0] = Vec3Dot( v, line->normal0 ) - line->dist0;
    candidate[1] = Vec3Dot( v, line->normal1 ) - line->dist1;

    if ( Vec2LengthSq( candidate ) < Vec2LengthSq( existing ) )
        Vec3Copy( v, pt->xyz );
}


/* TJunc_AddEdgeLine  0x0045d2e0 */
static void TJunc_AddEdgeLine( const vec3_t v0, const vec3_t v1, int axis, float epsilonSq )
{
    edgeLine_t  *line;
    edgeLine_t **bucket;
    vec3_t       dir;
    int          face;
    int          s;
    int          t;

    if ( tjuncGlob.lineCount == MAX_EDGE_LINES )
        Com_Error( "MAX_EDGE_LINES" );

    line = &TJUNC_LINES[ tjuncGlob.lineCount ];

    Vec3Sub( v1, v0, dir );

    if ( Vec3NormalizeTo( dir, line->dir ) == 0.0f )
        return;

    Vec3Copy( v0, line->origin );
    MakeNormalVectors( line->dir, line->normal0, line->normal1 );
    line->dist0 = Vec3Dot( v0, line->normal0 );
    line->dist1 = Vec3Dot( v0, line->normal1 );

    line->head.prev = &line->head;
    line->epsilonSq = epsilonSq;
    line->head.next = &line->head;

    TJuncDirBucket( dir, &face, &s, &t );
    s = s >> 1;
    t = t >> 1;
    line->dirBucketNext = TJUNC_DIR_BUCKET( face, t, s );
    TJUNC_DIR_BUCKET( face, t, s ) = line;

    TJuncAddPointsToLine( v0, v1, line, epsilonSq );

    tjuncGlob.lineCount++;

    if ( axis >= 0 )
    {
        bucket = TJuncAxisBucket( v0, axis );
        line->axisBucketNext = *bucket;
        *bucket = line;
    }
}


/* TJuncAxisBucket  0x0045d4a0 */
static edgeLine_t **TJuncAxisBucket( const vec3_t v, unsigned int axis )
{
    unsigned int a0;
    unsigned int a1;
    int          s;
    int          t;

    a0 = ~axis & 1;
    a1 = ~axis & 2;

    s = FloorFloatToInt( ( ( v[a0] - tjuncGlob.mins[a0] ) /
                           ( ( tjuncGlob.maxs[a0] - tjuncGlob.mins[a0] ) + 1.0f ) ) * 128.0f );
    if ( s < 0 )
        s = 0;
    else if ( s > TJUNC_AXIS_BUCKETS - 1 )
        s = TJUNC_AXIS_BUCKETS - 1;

    t = FloorFloatToInt( ( ( v[a1] - tjuncGlob.mins[a1] ) /
                           ( ( tjuncGlob.maxs[a1] - tjuncGlob.mins[a1] ) + 1.0f ) ) * 128.0f );
    if ( t < 0 )
        t = 0;
    else if ( t > TJUNC_AXIS_BUCKETS - 1 )
        t = TJUNC_AXIS_BUCKETS - 1;

    return &TJUNC_AXIS_BUCKET( axis, t, s );
}


/* TJuncDirBucket  0x0045d5b0 */
static void TJuncDirBucket( const vec3_t dir, int *face, int *s, int *t )
{
    unsigned int axis;

    axis = VecLargestAxis( dir );

    *s = FloorFloatToInt( ( dir[~axis & 1] / dir[axis] + 1.0f ) * 7.99999f );
    *t = FloorFloatToInt( ( dir[~axis & 2] / dir[axis] + 1.0f ) * 7.99999f );

    *face = axis;
}


/* TJuncFindLine  0x0045d650 */
static edgeLine_t *TJuncFindLine( const vec3_t v0, const vec3_t v1, int axis, float *epsilonSq )
{
    vec3_t dir;
    float  lengthSq;
    float  eps;

    Vec3Sub( v0, v1, dir );
    lengthSq = Vec3LengthSq( dir );

    eps = tjuncGlob.epsilonSq;
    if ( lengthSq * 0.25 < eps )
        eps = lengthSq * 0.25;

    Assert( epsilonSq );
    *epsilonSq = eps;

    if ( axis < 0 )
        return TJuncFindLineByDir( v0, v1, dir, eps );

    return TJuncFindLineInAxisBucket( v0, v1, axis, eps );
}


/* TJuncFindLineByDir  0x0045d720 */
static edgeLine_t *TJuncFindLineByDir( const vec3_t v0, const vec3_t v1, const vec3_t dir,
                                       float epsilonSq )
{
    if ( Vec3LengthSq( dir ) > tjuncGlob.bigEpsilonSq )
        return TJuncFindLineInDirBuckets( v0, v1, dir, epsilonSq );

    return TJuncFindLineLinear( v0, v1, epsilonSq );
}


/* TJuncFindLineInDirBuckets  0x0045d780 */
static edgeLine_t *TJuncFindLineInDirBuckets( const vec3_t v0, const vec3_t v1,
                                              const vec3_t dir, float epsilonSq )
{
    edgeLine_t *best;
    int face;
    int s;
    int t;
    int ds;
    int dt;

    TJuncDirBucket( dir, &face, &s, &t );

    s = ( s + 1 ) >> 1;
    t = ( t + 1 ) >> 1;

    best = NULL;

    for ( dt = -1; dt != 1; dt++ )
    {
        for ( ds = -1; ds != 1; ds++ )
            epsilonSq = TJuncFindBestLineInBucket( v0, v1, face, s + ds, t + dt, epsilonSq, &best );
    }

    return best;
}


/* TJuncFindBestLineInBucket  0x0045d830 */
static float TJuncFindBestLineInBucket( const vec3_t v0, const vec3_t v1,
                                        int face, int s, int t,
                                        float epsilonSq, edgeLine_t **best )
{
    edgeLine_t *line;
    float       error;

    for ( line = TJuncDirBucketHead( face, s, t ); line;
          line = ( edgeLine_t * )line->dirBucketNext )
    {
        error = TJuncLineError( line, v0, v1, epsilonSq );
        if ( error < epsilonSq )
        {
            *best = line;
            epsilonSq = error;
        }
    }

    return epsilonSq;
}


/* TJuncLineError  0x0045d8a0 */
static float TJuncLineError( const edgeLine_t *line, const vec3_t v0, const vec3_t v1,
                             float best )
{
    float dist[4];
    float worst;
    unsigned int i;

    dist[0] = Vec3Dot( v0, line->normal0 ) - line->dist0;
    dist[0] = dist[0] * dist[0];
    if ( best < dist[0] )
        return dist[0];

    dist[1] = Vec3Dot( v0, line->normal1 ) - line->dist1;
    dist[1] = dist[1] * dist[1];
    if ( best < dist[1] )
        return dist[1];

    dist[2] = Vec3Dot( v1, line->normal0 ) - line->dist0;
    dist[2] = dist[2] * dist[2];
    if ( best < dist[2] )
        return dist[2];

    dist[3] = Vec3Dot( v1, line->normal1 ) - line->dist1;
    dist[3] = dist[3] * dist[3];
    if ( best < dist[3] )
        return dist[3];

    worst = dist[0];
    for ( i = 1; i < 4; i++ )
    {
        if ( worst < dist[i] )
            worst = dist[i];
    }

    return worst;
}


/* TJuncDirBucketHead  0x0045d9d0 */
static edgeLine_t *TJuncDirBucketHead( int face, int s, int t )
{
    if ( s == -1 )
    {
        if ( face == 0 )
            return TJuncDirBucketHead( 1, 0, 7 - t );
        if ( face == 1 )
            return TJuncDirBucketHead( 0, 0, 7 - t );
        return TJuncDirBucketHead( 1, 7 - t, 0 );
    }

    if ( s == 8 )
    {
        if ( face == 0 )
            return TJuncDirBucketHead( 1, 7, t );
        if ( face == 1 )
            return TJuncDirBucketHead( 0, 7, t );
        return TJuncDirBucketHead( 1, t, 7 );
    }

    if ( t == -1 )
    {
        if ( face == 0 )
            return TJuncDirBucketHead( 2, 7 - s, 0 );
        if ( face == 1 )
            return TJuncDirBucketHead( 2, 0, 7 - s );
        return TJuncDirBucketHead( 0, 7 - s, 0 );
    }

    if ( t == 8 )
    {
        if ( face == 0 )
            return TJuncDirBucketHead( 2, s, 7 );
        if ( face == 1 )
            return TJuncDirBucketHead( 2, 7, s );
        return TJuncDirBucketHead( 0, s, 7 );
    }

    Assertx( face >= 0 && face < 3, "(face) = %i", face );
    Assertx( s >= 0 && s < 8, "(s) = %i", s );
    Assertx( t >= 0 && t < 8, "(t) = %i", t );

    return TJUNC_DIR_BUCKET( face, t, s );
}


/* TJuncFindLineLinear  0x0045dbf0 */
static edgeLine_t *TJuncFindLineLinear( const vec3_t v0, const vec3_t v1, float epsilonSq )
{
    edgeLine_t *best;
    float       error;
    int         i;

    best = NULL;

    for ( i = 0; i < tjuncGlob.lineCount; i++ )
    {
        error = TJuncLineError( &TJUNC_LINES[i], v0, v1, epsilonSq );
        if ( error < epsilonSq )
        {
            best = &TJUNC_LINES[i];
            epsilonSq = error;
        }
    }

    return best;
}


/* TJuncFindLineInAxisBucket  0x0045dc70 */
static edgeLine_t *TJuncFindLineInAxisBucket( const vec3_t v0, const vec3_t v1, int axis,
                                              float epsilonSq )
{
    edgeLine_t  *best;
    edgeLine_t **bucket;
    edgeLine_t  *line;
    float        error;

    best   = NULL;
    bucket = TJuncAxisBucket( v0, axis );

    for ( line = *bucket; line; line = ( edgeLine_t * )line->axisBucketNext )
    {
        error = TJuncLineError( line, v0, v1, epsilonSq );
        if ( error < epsilonSq )
        {
            best = line;
            epsilonSq = error;
        }
    }

    return best;
}


/* TJuncAxisForEdge  0x0045dcf0 */
static int TJuncAxisForEdge( const vec3_t v0, const vec3_t v1 )
{
    vec3_t dir;
    byte   flags;

    if ( !tjuncGlob.useAxisBuckets )
        return -1;

    Vec3Sub( v0, v1, dir );

    flags = ( byte )( tjuncGlob.epsilonSq * 0.25 < dir[0] * dir[0] ) |
            ( ( tjuncGlob.epsilonSq * 0.25 < dir[1] * dir[1] ) ? 2 : 0 ) |
            ( ( tjuncGlob.epsilonSq * 0.25 < dir[2] * dir[2] ) ? 4 : 0 );

    if ( flags == 1 )
        return 0;
    if ( flags == 2 )
        return 1;
    if ( flags == 4 )
        return 2;

    return -1;
}


/* TJunc_FixSurfaceList  0x0045ddf0 */
void TJunc_FixSurfaceList( TriSurf_t **surfs, int surfCount, TriSurf_t *extraSurf )
{
    int i;

    Assert( tjuncGlob.mem );
    Assert( tjuncGlob.lineCount == 0 );
    Assert( tjuncGlob.ptCount == 0 );

    if ( extraSurf )
        TJuncAddSurfaceEdges( extraSurf );
    TJuncAddSurfaceListEdges( surfs, surfCount );

    TJuncResetPoints();

    if ( extraSurf )
        TJuncAddSurfaceEdges( extraSurf );
    TJuncAddSurfaceListEdges( surfs, surfCount );

    for ( i = 0; i < surfCount; i++ )
        TJuncFixSurface( surfs[i] );

    TJuncClearLines();
}


/* FixSurfaceJunctions  0x0045e080 */
void FixSurfaceJunctions( winding_t **inout, const vec4_t plane )
{
    vec3_t        pts[MAX_POINTS_ON_CONCAVE_WINDING];
    winding_t    *w;
    const float  *v0;
    const float  *v1;
    edgeLine_t   *line;
    tjuncPoint_t *pt;
    float         epsilonSq;
    float         epsilon;
    float         dist0;
    float         dist1;
    float         planeDist;
    unsigned int  ptCount;
    unsigned int  i;
    int           axis;

    Assert( inout );
    Assert( *inout );

    w = *inout;
    ptCount = 0;

    for ( i = 0; i < w->ptCount; i++ )
    {
        if ( ptCount == MAX_POINTS_ON_CONCAVE_WINDING )
            Com_Error( "MAX_POINTS_ON_CONCAVE_WINDING" );

        Vec3Copy( w->pts[i], pts[ptCount] );
        ptCount++;

        v0 = w->pts[i];
        v1 = w->pts[ ( i + 1 ) % w->ptCount ];

        axis = TJuncAxisForEdge( v0, v1 );
        line = TJuncFindLine( v0, v1, axis, &epsilonSq );
        if ( !line )
            continue;

        if ( epsilonSq < line->epsilonSq )
            line->epsilonSq = epsilonSq;

        epsilon = sqrt( line->epsilonSq );

        dist0 = Vec3Dot( v0, line->dir );
        dist1 = Vec3Dot( v1, line->dir );

        if ( dist1 <= dist0 )
            pt = line->head.prev;
        else
            pt = line->head.next;

        while ( pt != &line->head )
        {
            if ( dist1 <= dist0 )
            {
                if ( pt->dist < dist1 + epsilon )
                    break;
            }
            else
            {
                if ( dist1 - epsilon < pt->dist )
                    break;
            }

            if ( ( dist0 < dist1 && dist0 + epsilon < pt->dist ) ||
                 ( dist1 < dist0 && pt->dist < dist0 - epsilon ) )
            {
                planeDist = Vec3Dot( pt->xyz, plane ) - plane[3];

                if ( -epsilon < planeDist && planeDist < epsilon )
                {
                    if ( ptCount == MAX_POINTS_ON_CONCAVE_WINDING )
                        Com_Error( "MAX_POINTS_ON_CONCAVE_WINDING" );

                    Vec3Copy( pt->xyz, pts[ptCount] );
                    ptCount++;
                }
            }

            if ( dist1 <= dist0 )
                pt = pt->prev;
            else
                pt = pt->next;
        }
    }

    Assert( ptCount >= w->ptCount );

    if ( ptCount > w->ptCount )
    {
        FreeWinding( w );
        w = AllocWinding( ptCount );
        w->ptCount = ptCount;
        memcpy( w->pts, pts, w->ptCount * sizeof( vec3_t ) );
        *inout = w;
    }
}


/* TJunc_FixGrid  0x0045e4f0 */
void TJunc_FixGrid( const vec3_t mins, const vec3_t maxs )
{
    Assert( tjuncGlob.mem );

    TJuncSubdivide( mins, maxs, TJuncFixAndCleanSurface );
}


/* TJuncFixAndCleanSurface  0x0045e540 */
static void TJuncFixAndCleanSurface( TriSurf_t *surf )
{
    TJuncFixSurface( surf );
    TJuncRemoveDuplicatePoints( surf );
}


/* TJuncRemoveDuplicatePoints  0x0045e560 */
static void TJuncRemoveDuplicatePoints( TriSurf_t *surf )
{
    int i;

    TJuncRemoveDuplicateWindingPoints( surf->w );

    for ( i = 0; i < surf->holeCount; i++ )
        TJuncRemoveDuplicateWindingPoints( surf->holes[i] );
}


/* TJuncRemoveDuplicateWindingPoints  0x0045e5b0 */
static void TJuncRemoveDuplicateWindingPoints( winding_t *w )
{
    unsigned int i;
    unsigned int j;

    Assert( w );
    Assertx( w->ptCount > 0 && w->ptCount < MAX_POINTS_ON_CONCAVE_WINDING,
             "(w->ptCount) = %i", w->ptCount );

    i = 0;
    while ( i < w->ptCount )
    {
        j = ( i + 1 ) % w->ptCount;

        if ( !Vec3Equal( w->pts[i], w->pts[j] ) )
        {
            i++;
        }
        else
        {
            if ( j == 0 )
                i--;
            else
                memmove( w->pts[i], w->pts[j], ( w->ptCount - j ) * sizeof( vec3_t ) );

            w->ptCount--;
        }
    }
}


/* TJuncSubdivide  0x0045e6d0 */
static void TJuncSubdivide( const vec3_t mins, const vec3_t maxs,
                            void ( *callback )( TriSurf_t * ) )
{
    vec3_t mid;
    vec3_t subMins;
    vec3_t subMaxs;
    int    limit;
    int    count;
    unsigned int i;

    Vec3Sub( maxs, mins, mid );

    if ( TJUNC_MIN_BOX_SIZE <= mid[0] || TJUNC_MIN_BOX_SIZE <= mid[1] ||
         TJUNC_MIN_BOX_SIZE <= mid[2] )
    {
        limit = TJUNC_MAX_BOX_SURFS;
    }
    else
    {
        limit = 0x7fffffff;
    }

    count = GridTree_CountOverlapping( mins, maxs, limit );
    if ( count <= 1 )
        return;

    if ( count > limit )
    {
        Vec3Add( mins, maxs, mid );
        Vec3Scale( mid, 0.5f, mid );

        for ( i = 0; i < 8; i++ )
        {
            if ( ( i & 1 ) == 0 )
            {
                subMins[0] = mid[0];
                subMaxs[0] = maxs[0];
            }
            else
            {
                subMins[0] = mins[0];
                subMaxs[0] = mid[0];
            }

            if ( ( i & 2 ) == 0 )
            {
                subMins[1] = mid[1];
                subMaxs[1] = maxs[1];
            }
            else
            {
                subMins[1] = mins[1];
                subMaxs[1] = mid[1];
            }

            if ( ( i & 4 ) == 0 )
            {
                subMins[2] = mid[2];
                subMaxs[2] = maxs[2];
            }
            else
            {
                subMins[2] = mins[2];
                subMaxs[2] = mid[2];
            }

            TJuncSubdivide( subMins, subMaxs, callback );
        }
    }
    else
    {
        GridTree_ForEach( mins, maxs, TJuncAddSurfaceEdges );
        GridTree_ForEach( mins, maxs, callback );
        TJuncClearLines();
    }
}


/* TJuncClearLines  0x0045def0 */
static void TJuncClearLines( void )
{
    tjuncGlob.lineCount = 0;
    tjuncGlob.ptCount   = 0;
    TJuncClearBuckets();
}


/* TJuncClearBuckets  0x0045df10 */
static void TJuncClearBuckets( void )
{
    if ( tjuncGlob.useAxisBuckets )
    {
        memset( ( byte * )tjuncGlob.mem + TJUNC_AXIS_OFFSET, 0,
                3 * TJUNC_AXIS_BUCKETS * TJUNC_AXIS_BUCKETS * sizeof( edgeLine_t * ) );
    }

    memset( ( byte * )tjuncGlob.mem + TJUNC_DIR_OFFSET, 0,
            3 * TJUNC_DIR_BUCKETS * TJUNC_DIR_BUCKETS * sizeof( edgeLine_t * ) );
}


/* TJuncResetPoints  0x0045df60 */
static void TJuncResetPoints( void )
{
    int i;

    for ( i = 0; i < tjuncGlob.lineCount; i++ )
    {
        TJUNC_LINES[i].head.next = &TJUNC_LINES[i].head;
        TJUNC_LINES[i].head.prev = &TJUNC_LINES[i].head;
    }

    tjuncGlob.ptCount = 0;
}


/* TJuncAddSurfaceListEdges  0x0045dfd0 */
static void TJuncAddSurfaceListEdges( TriSurf_t **surfs, int surfCount )
{
    int i;

    for ( i = 0; i < surfCount; i++ )
        TJuncAddSurfaceEdges( surfs[i] );
}


/* TJuncFixSurface  0x0045e010 */
static void TJuncFixSurface( TriSurf_t *surf )
{
    int i;

    FixSurfaceJunctions( &surf->w, surf->props->plane );

    for ( i = 0; i < surf->holeCount; i++ )
        FixSurfaceJunctions( &surf->holes[i], surf->props->plane );
}


/* TJuncAddSurfListToGrid  0x0045e870 */
void TJuncAddSurfListToGrid( TriSurf_t *surf )
{
    for ( ; surf; surf = surf->visGroupNext )
    {
        if ( surf->props->nodraw == 0 )
            GridTree_Insert( surf );
    }
}


/* TJunc_SetBounds  0x0045e8b0 */
void TJunc_SetBounds( const vec3_t mins, const vec3_t maxs )
{
    Vec3Copy( mins, tjuncGlob.mins );
    Vec3Copy( maxs, tjuncGlob.maxs );
}


/* TJunc_SetEpsilon  0x0045e8e0 */
void TJunc_SetEpsilon( float epsilon )
{
    float bigEpsilon;

    tjuncGlob.epsilonSq = epsilon * epsilon;

    bigEpsilon = epsilon * TJUNC_BIG_EPSILON_SCALE;
    tjuncGlob.bigEpsilonSq = bigEpsilon * bigEpsilon;
}


/* TJunc_SetUseAxisBuckets  0x0045e910 */
void TJunc_SetUseAxisBuckets( qboolean use )
{
    tjuncGlob.useAxisBuckets = ( byte )use;
}
