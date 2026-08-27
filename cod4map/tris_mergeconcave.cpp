/* Original: .\tris_mergeconcave.cpp */

#include "tris_mergeconcave.h"
#include "brush_sides.h"

#include "cod4map.h"
#include "tris.h"
#include "tris_gridtree.h"
#include "tris_tjunc.h"

#include <stdlib.h>
#include <string.h>
#include "qsort_vc8.h"


extern char *va( const char *fmt, ... );                                /* 0x0047fbd0 */

extern int        GetTrisTransientMode( void );                         /* 0x0043c6c0 */
extern void       Tris_UnlinkSurf( TriSurf_t *surf, TriSurf_t **listHead ); /* 0x0043d1d0 */
extern void       Tris_FreeSurface( TriSurf_t *surf );                  /* 0x0043d260 */
extern TriSurf_t *AllocTriSurf( winding_t *w, TriSurfProps_t *props ); /* 0x0043d0f0 */

extern void SetGridDivisionPoints( const vec3_t mins, const vec3_t maxs );       /* 0x004502a0 */
extern void GridTree_Insert( TriSurf_t *surf );                        /* 0x00450440 */
extern void GridTree_ForEach( const vec3_t mins, const vec3_t maxs,
                                      void ( *callback )( TriSurf_t * ) ); /* 0x00450bf0 */


static const vec3_t s_mergeExpand = { MERGE_GRID_EXPAND, MERGE_GRID_EXPAND, MERGE_GRID_EXPAND };


mergeGlob_t mergeGlob;      /* 0x2b8109a4 */


static int AddHole( winding_t *w, float area, const vec4_t plane,
                    winding_t **holes, float *holeArea, int holeCount );
static int AddWindingWithoutOverlap_r( int first, int surfCount, int prevSurfCount,
                                       winding_t *w, TriSurfProps_t *props );
static void AddWindingsWithoutOverlap( int mode );
static int FindHoles( winding_t **inout, const vec4_t plane, winding_t **holes,
                      float *holeArea, bool dbg );
static bool MergeAnyHoleCrossesEdge( winding_t **holes, int holeCount,
                                     const vec3_t v0, const vec3_t v1,
                                     int axisX, int axisY );
static void MergeAppendRun( const winding_t *src, int first, int last, winding_t *w );
static bool MergeHoleAreaIsSignificant( int ptCount, float area );
static void MergeBuildVertMap( intWinding_t **intWindings, const winding_t *extraWinding );
static bool MergeCleanSurface( TriSurf_t *surf, float epsilon );
static int MergeCompareSurfs( const void *va, const void *vb );
static bool MergeCutNotches( TriSurf_t *surf );
static int MergeFindNotch( const winding_t *w, unsigned int begin, const vec3_t normal );
static bool MergeFindSelfTouch( const winding_t *w, unsigned int *begin0,
                                unsigned int *begin1, unsigned int *end0,
                                unsigned int *end1 );
static void MergeFindSurfaceHoles( TriSurf_t *surf, bool dbg );
static void MergeFreeVertMap( void );
static void MergeGatherCallback( TriSurf_t *surf );
static void MergeGatherGroup( TriSurf_t *surf );
static void MergeGatherNeighbourCallback( TriSurf_t *surf );
static int MergeIndexForPoint( const vec3_t xyz );
static intWinding_t *MergeIntWindingFromWinding( const winding_t *w );
static bool MergeNotchWedge( winding_t **inout, unsigned int i0, unsigned int j0,
                             const vec4_t plane, int axisX, int axisY );
static void MergeSubWinding( const winding_t *w, int first, int last, winding_t **out );
static bool MergeSurfaceAllowsEdge( TriSurf_t *surf, const vec3_t v0, const vec3_t v1,
                                    int axisX, int axisY );
static bool MergeWindingBlocksEdge( const winding_t *w, const vec3_t planeNormal,
                                    const vec3_t v0, const vec3_t v1, const vec3_t v2 );
static winding_t *MergeWindingFromIntWinding( const intWinding_t *intWinding );
static bool TetherFindBestPair( const vec4_t plane, const winding_t *wInner,
                                const winding_t *wOuter,
                                winding_t **holes, int holeCount,
                                unsigned int *outInner, unsigned int *outOuter,
                                float *bestDistSq );
static void TetherWinding( const winding_t *wInner, const winding_t *wOuter,
                           int innerIndex, int outerIndex, winding_t *w );
static bool MergeSegmentsCross( const vec3_t a0, const vec3_t a1,
                                const vec3_t b0, const vec3_t b1, int axisX, int axisY );
static bool MergeCornerIsSafe( const winding_t *w, unsigned int i, unsigned int im1,
                               unsigned int im2, int axisX, int axisY );

/* MergeSegmentsCross  0x004555c0 */
static bool MergeSegmentsCross( const vec3_t a0, const vec3_t a1,
                                const vec3_t b0, const vec3_t b1, int axisX, int axisY )
{
    vec3_t dirA;
    vec3_t dirB;
    float  crossB0;
    float  crossB1;
    float  crossA0;
    float  crossA1;

    Vec3Sub( a1, a0, dirA );

    crossB0 = ( b0[axisX] - a0[axisX] ) * dirA[axisY] -
              ( b0[axisY] - a0[axisY] ) * dirA[axisX];
    crossB1 = ( b1[axisX] - a0[axisX] ) * dirA[axisY] -
              ( b1[axisY] - a0[axisY] ) * dirA[axisX];

    if ( crossB0 <= -0.001 && crossB1 <= -0.001 )
        return false;
    if ( crossB0 >= 0.001 && crossB1 >= 0.001 )
        return false;

    Vec3Sub( b1, b0, dirB );

    crossA0 = ( a0[axisX] - b0[axisX] ) * dirB[axisY] -
              ( a0[axisY] - b0[axisY] ) * dirB[axisX];
    crossA1 = ( a1[axisX] - b0[axisX] ) * dirB[axisY] -
              ( a1[axisY] - b0[axisY] ) * dirB[axisX];

    if ( crossA0 <= -0.001 && crossA1 <= -0.001 )
        return false;
    if ( crossA0 >= 0.001 && crossA1 >= 0.001 )
        return false;

    if ( Vec3Equal( a0, b0 ) )
        return false;
    if ( Vec3Equal( a0, b1 ) )
        return false;
    if ( Vec3Equal( a1, b0 ) )
        return false;
    if ( Vec3Equal( a1, b1 ) )
        return false;

    return crossB0 * crossB0 >= 0.00390625 ||
           crossB1 * crossB1 >= 0.00390625 ||
           crossA0 * crossA0 >= 0.00390625 ||
           crossA1 * crossA1 >= 0.00390625;
}


/* MergeCornerIsSafe  0x00456a30 */
static bool MergeCornerIsSafe( const winding_t *w, unsigned int i, unsigned int im1,
                               unsigned int im2, int axisX, int axisY )
{
    unsigned int prev;
    unsigned int cur;

    (void)im1;

    prev = ( im2 + 1 ) % w->ptCount;
    cur  = ( im2 + 2 ) % w->ptCount;

    while ( cur != i )
    {
        if ( MergeSegmentsCross( w->pts[i], w->pts[im2], w->pts[prev], w->pts[cur],
                                 axisX, axisY ) )
            return false;

        prev = cur;
        cur  = ( cur + 1 ) % w->ptCount;
    }

    return true;
}


/* MergeWindingCrossesEdge  0x00455830 */
static bool MergeWindingCrossesEdge( const winding_t *w, const vec3_t v0, const vec3_t v1,
                                     int axisX, int axisY )
{
    unsigned int prev;
    unsigned int i;

    prev = w->ptCount - 1;

    for ( i = 0; i < w->ptCount; i++ )
    {
        if ( MergeSegmentsCross( w->pts[prev], w->pts[i], v0, v1, axisX, axisY ) )
            return true;

        prev = i;
    }

    return false;
}


/* TetherHolesToWinding  0x004558b0 */
void TetherHolesToWinding( TriSurf_t *surf )
{
    winding_t   *outline;
    winding_t   *tethered;
    unsigned int originalPointCount;
    unsigned int outerIndex;
    unsigned int innerIndex;
    float        bestDistSq;
    int          chosenHoleIndex;
    int          holeIndex;

    if ( surf->holeCount == 0 )
        return;

    originalPointCount = surf->w->ptCount;
    for ( holeIndex = 0; holeIndex < surf->holeCount; holeIndex++ )
        originalPointCount += surf->holes[holeIndex]->ptCount + 2;

    tethered = AllocWinding( originalPointCount );
    SanityCheck( tethered->ptCount == 0 );

    outline = surf->w;

    while ( surf->holeCount )
    {
        innerIndex      = 0;
        outerIndex      = 0;
        bestDistSq      = FLT_MAX;
        chosenHoleIndex = -1;

        for ( holeIndex = 0; holeIndex < surf->holeCount; holeIndex++ )
        {
            Assert( originalPointCount >=
                    surf->holes[holeIndex]->ptCount + outline->ptCount + 2 );

            if ( TetherFindBestPair( surf->props->plane, surf->holes[holeIndex], outline,
                                     surf->holes, surf->holeCount,
                                     &innerIndex, &outerIndex, &bestDistSq ) )
            {
                chosenHoleIndex = holeIndex;
            }
        }

        Assert( chosenHoleIndex >= 0 );

        TetherWinding( surf->holes[chosenHoleIndex], outline, innerIndex, outerIndex, tethered );

        FreeWinding( outline );
        FreeWinding( surf->holes[chosenHoleIndex] );

        surf->holeCount--;
        surf->holes[chosenHoleIndex] = surf->holes[ surf->holeCount ];

        if ( surf->holeCount )
            outline = CopyWinding( tethered );
    }

    operator delete[]( surf->holes );
    surf->holes = NULL;
    surf->w = tethered;
}


/* TetherWinding  0x00455b00 */
static void TetherWinding( const winding_t *wInner, const winding_t *wOuter,
                           int innerIndex, int outerIndex, winding_t *w )
{
    unsigned int innerCount;
    unsigned int outerCount;
    unsigned int innerLast;
    int          coincident;

    Assert( w );
    Assert( wInner );
    Assert( wOuter );
    Assert( w != wInner );
    Assert( w != wOuter );
    Assert( wInner != wOuter );

    innerCount = wInner->ptCount;
    outerCount = wOuter->ptCount;
    innerLast  = innerCount;

    coincident = Vec3Compare( wInner->pts[innerIndex], wOuter->pts[outerIndex] );
    if ( coincident )
        innerLast--;

    w->ptCount = 0;

    MergeAppendRun( wInner, ( innerIndex + 1 ) % innerCount,
                            ( innerIndex + innerLast ) % innerCount, w );
    MergeAppendRun( wOuter, outerIndex, ( outerIndex - 1 + outerCount ) % outerCount, w );
    MergeAppendRun( wOuter, outerIndex, outerIndex, w );

    if ( !coincident )
        MergeAppendRun( wInner, innerIndex, innerIndex, w );
}


/* MergeAppendRun  0x00455ce0 */
static void MergeAppendRun( const winding_t *src, int first, int last, winding_t *w )
{
    int count;

    if ( last < first )
    {
        count = src->ptCount - first;
        memcpy( w->pts[ w->ptCount ], src->pts[first], count * sizeof( vec3_t ) );
        w->ptCount += count;

        memcpy( w->pts[ w->ptCount ], src->pts[0], ( last + 1 ) * sizeof( vec3_t ) );
        w->ptCount += last + 1;
    }
    else
    {
        count = last - first + 1;
        memcpy( w->pts[ w->ptCount ], src->pts[first], count * sizeof( vec3_t ) );
        w->ptCount += count;
    }
}


/* TetherFindBestPair  0x00455dc0 */
static bool TetherFindBestPair( const vec4_t plane, const winding_t *wInner,
                                const winding_t *wOuter,
                                winding_t **holes, int holeCount,
                                unsigned int *outInner, unsigned int *outOuter,
                                float *bestDistSq )
{
    vec3_t       delta;
    float        distSq;
    unsigned int innerIndex;
    unsigned int innerPrev;
    unsigned int innerNext;
    unsigned int outerIndex;
    unsigned int outerPrev;
    unsigned int outerNext;
    int          axisX;
    int          axisY;
    bool         found;

    Assert( plane );
    Assert( wInner );
    Assert( wOuter );
    Assert( wInner != wOuter );
    Assert( WindingAreaWithKnownNormal( wOuter, plane ) >= 0 );
    Assert( WindingAreaWithKnownNormal( wInner, plane ) <= 0 );

    GetProjectionAxes( plane, &axisX, &axisY );

    found = false;

    for ( innerIndex = 0; innerIndex < wInner->ptCount; innerIndex++ )
    {
        innerPrev = ( innerIndex - 1 + wInner->ptCount ) % wInner->ptCount;
        innerNext = ( innerIndex + 1 ) % wInner->ptCount;

        for ( outerIndex = 0; outerIndex < wOuter->ptCount; outerIndex++ )
        {
            Vec3Sub( wInner->pts[innerIndex], wOuter->pts[outerIndex], delta );
            distSq = Vec3LengthSq( delta );

            if ( distSq >= *bestDistSq )
                continue;

            outerPrev = ( outerIndex - 1 + wOuter->ptCount ) % wOuter->ptCount;
            outerNext = ( outerIndex + 1 ) % wOuter->ptCount;

            if ( MergeWindingCrossesEdge( wInner, wInner->pts[innerIndex],
                                          wOuter->pts[outerIndex], axisX, axisY ) )
                continue;

            if ( MergeWindingCrossesEdge( wOuter, wInner->pts[innerIndex],
                                          wOuter->pts[outerIndex], axisX, axisY ) )
                continue;

            if ( MergeWindingBlocksEdge( wInner, plane, wOuter->pts[outerIndex],
                                         wInner->pts[innerIndex], wInner->pts[innerNext] ) )
                continue;

            if ( MergeWindingBlocksEdge( wInner, plane, wInner->pts[innerPrev],
                                         wInner->pts[innerIndex], wOuter->pts[outerIndex] ) )
                continue;

            if ( MergeWindingBlocksEdge( wOuter, plane, wOuter->pts[outerPrev],
                                         wOuter->pts[outerIndex], wInner->pts[innerIndex] ) )
                continue;

            if ( MergeWindingBlocksEdge( wOuter, plane, wInner->pts[innerIndex],
                                         wOuter->pts[outerIndex], wOuter->pts[outerNext] ) )
                continue;

            if ( MergeAnyHoleCrossesEdge( holes, holeCount, wInner->pts[innerIndex],
                                          wOuter->pts[outerIndex], axisX, axisY ) )
                continue;

            *bestDistSq = distSq;
            *outInner   = innerIndex;
            *outOuter   = outerIndex;
            found       = true;
        }
    }

    return found;
}


/* MergeAnyHoleCrossesEdge  0x004561d0 */
static bool MergeAnyHoleCrossesEdge( winding_t **holes, int holeCount,
                                     const vec3_t v0, const vec3_t v1,
                                     int axisX, int axisY )
{
    int i;

    for ( i = 0; i < holeCount; i++ )
    {
        if ( MergeWindingCrossesEdge( holes[i], v0, v1, axisX, axisY ) )
            return true;
    }

    return false;
}


/* MergeWindingBlocksEdge  0x00456230 */
static bool MergeWindingBlocksEdge( const winding_t *w, const vec3_t planeNormal,
                                    const vec3_t v0, const vec3_t v1, const vec3_t v2 )
{
    vec3_t dir[2];
    vec4_t wedge[2];
    float  dist0;
    float  dist1;
    unsigned int wedgeSides;
    unsigned int prev2;
    unsigned int prev;
    unsigned int i;
    int    side;

    Vec3Sub( v1, v0, dir[0] );
    Vec3Sub( v2, v1, dir[1] );

    for ( i = 0; i < 2; i++ )
    {
        Vec3Cross( dir[i], planeNormal, wedge[i] );
        Vec3Normalize( wedge[i] );
        wedge[i][3] = Vec3Dot( v1, wedge[i] ) + MERGE_WEDGE_BIAS;
    }

    if ( Vec3Dot( wedge[0], v2 ) - wedge[0][3] <= 0.0f )
        wedgeSides = 1;
    else
        wedgeSides = 2;

    prev2 = w->ptCount - 2;
    prev  = w->ptCount - 1;

    for ( i = 0; i < w->ptCount; i++ )
    {
        if ( Vec3Equal( w->pts[prev], v1 ) )
        {
            dist0 = Vec3Dot( w->pts[prev2], wedge[0] ) - wedge[0][3];
            dist1 = Vec3Dot( w->pts[prev2], wedge[1] ) - wedge[1][3];
            side  = ( dist0 > 0.0f ) + ( dist1 > 0.0f );
            if ( side >= ( int )wedgeSides )
                return true;

            dist0 = Vec3Dot( w->pts[i], wedge[0] ) - wedge[0][3];
            dist1 = Vec3Dot( w->pts[i], wedge[1] ) - wedge[1][3];
            side  = ( dist0 > 0.0f ) + ( dist1 > 0.0f );
            if ( side >= ( int )wedgeSides )
                return true;
        }

        prev2 = prev;
        prev  = i;
    }

    return false;
}


/* MergeRemoveDegenerateIndices  0x004564b0 */
int MergeRemoveDegenerateIndices( int ptCount, int *indices )
{
    int im2;
    int im1;
    int i;

    for ( ;; )
    {
        im2 = ptCount - 2;
        im1 = ptCount - 1;

        for ( i = 0; i < ptCount; i++ )
        {
            if ( indices[im2] == indices[i] )
                break;

            im2 = im1;
            im1 = i;
        }

        if ( i >= ptCount )
            return ptCount;

        ptCount -= 2;
        if ( ptCount < 3 )
            return 0;

        if ( im1 == ptCount || im2 == ptCount )
            continue;

        if ( im1 == 0 )
        {
            memmove( indices, indices + 2, ptCount * sizeof( int ) );
        }
        else
        {
            Assert( ptCount + 2 - i > 0 );
            Assert( im2 < i );
            memmove( &indices[im2], &indices[i], ( ptCount - im2 ) * sizeof( int ) );
        }
    }
}


/* AllocIntWinding  0x004565f0 */
intWinding_t *AllocIntWinding( int ptCount )
{
    intWinding_t *iw;
    size_t        size;

    Assertx( ptCount > 0, "(ptCount) = %i", ptCount );

    size = ptCount * sizeof( int ) + sizeof( int );
    iw = ( intWinding_t * )operator new[]( size );
    if ( !iw )
        Com_Error( "Out of memory on %i-point integer winding", ptCount );

    memset( iw, 0, size );

    return iw;
}

void FreeIntWinding( intWinding_t *iw )
{
    Assert( iw );
    operator delete[]( iw );
}

intWinding_t *CopyIntWinding( const intWinding_t *iw )
{
    intWinding_t *copy;

    Assert( iw );

    copy = AllocIntWinding( iw->ptCount );
    memcpy( copy, iw, iw->ptCount * sizeof( int ) + sizeof( int ) );

    return copy;
}


/* MergeVertForIndex  0x00456730 */
const float *MergeVertForIndex( unsigned int index )
{
    AssertIn( index, mergeGlob.vertCount );

    return mergeGlob.vertMap[index];
}


/* MergeRemoveColinearPoints  0x004568e0 */
static void MergeRemoveColinearPoints( winding_t *w, int axisX, int axisY, float epsilon )
{
    unsigned int i;
    unsigned int i1;
    unsigned int i2;

    Assert( w );

    i = 0;
    while ( i < w->ptCount )
    {
        i1 = ( i + 1 ) % w->ptCount;
        i2 = ( i + 2 ) % w->ptCount;

        if ( ( !PointOnLine( w->pts[i], w->pts[i1], w->pts[i2], epsilon ) &&
               !Vec3CompareEpsilon( w->pts[i1], w->pts[i2], epsilon ) ) ||
             !MergeCornerIsSafe( w, i, i1, i2, axisX, axisY ) )
        {
            i++;
            continue;
        }

        w->ptCount--;
        if ( i2 != 0 )
            memmove( w->pts[i1], w->pts[i2], ( w->ptCount - i1 ) * sizeof( vec3_t ) );
    }
}


/* MergeFindSharedEdge  0x00456ae0 */
int MergeFindSharedEdge( const intWinding_t *w0, const intWinding_t *w1,
                         int *start0, int *start1 )
{
    int limit;
    int i;
    int j;
    int run;
    int k0;
    int k1;

    limit = I_min( w0->ptCount, w1->ptCount );

    for ( i = 0; i < w0->ptCount; i++ )
    {
        for ( j = 0; j < w1->ptCount; j++ )
        {
            if ( w0->pts[i] != w1->pts[j] )
                continue;

            run = 1;

            if ( i == 0 )
            {
                k0 = 0;
                k1 = 0;

                do
                {
                    k0 = ( i + w0->ptCount - run ) % w0->ptCount;
                    k1 = ( j + run ) % w1->ptCount;

                    if ( w0->pts[k0] != w1->pts[k1] )
                        break;

                    run++;
                }
                while ( run < limit );

                if ( run > 1 )
                {
                    i = k0;
                    j = k1;

                    if ( run < limit )
                    {
                        i++;

                        if ( j == 0 )
                            j = w1->ptCount;
                        j--;
                    }
                }
            }

            while ( run < limit &&
                    w0->pts[( i + run ) % w0->ptCount] ==
                    w1->pts[( j + w1->ptCount - run ) % w1->ptCount] )
            {
                run++;
            }

            if ( run != 1 )
            {
                *start0 = i;
                *start1 = j;
                return run;
            }
        }
    }

    return 0;
}


/* MergeRemoveDuplicateIndices  0x00456c70 */
void MergeRemoveDuplicateIndices( intWinding_t *w0, int start, int dupCount )
{
    bool startedOnSpur;
    int  end;
    int  prev;
    int  next;

    startedOnSpur = false;

    if ( w0->pts[start] != w0->pts[ ( start + dupCount ) % w0->ptCount ] )
    {
        startedOnSpur = true;
        start++;
        if ( start == w0->ptCount )
            start = 0;
        dupCount -= 2;
    }

    if ( w0->ptCount - dupCount < 3 && w0->ptCount != dupCount )
    {
        Assertx( w0->ptCount - dupCount >= 3 || w0->ptCount == dupCount,
                 "%s", va( "%i, %i", w0->ptCount, dupCount ) );
    }

    if ( w0->ptCount == dupCount )
        return;

    end = start + dupCount;
    if ( end != w0->ptCount )
    {
        if ( w0->ptCount < end )
        {
            end -= w0->ptCount;
            start = 0;
        }

        memmove( &w0->pts[start], &w0->pts[end], ( w0->ptCount - end ) * sizeof( int ) );
    }

    w0->ptCount -= dupCount;

    if ( startedOnSpur )
        return;

    for ( ;; )
    {
        prev = ( start - 1 + w0->ptCount ) % w0->ptCount;
        next = ( start + 1 ) % w0->ptCount;

        if ( w0->pts[prev] != w0->pts[next] )
            return;

        w0->ptCount -= 2;

        if ( next == 0 )
        {
            prev = 0;
        }
        else if ( start != w0->ptCount )
        {
            memmove( &w0->pts[start], &w0->pts[start + 2],
                     ( w0->ptCount - start ) * sizeof( int ) );

            if ( start < prev )
                prev -= 2;
        }

        start = prev;

        Assert( w0->ptCount >= 3 );
    }
}


/* MergeJoinIntWindings  0x00456e80 */
intWinding_t *MergeJoinIntWindings( const intWinding_t *w0, const intWinding_t *w1,
                                    int surfIndex0, int surfIndex1,
                                    int start0, int start1, int dupCount )
{
    intWinding_t *w;
    int           i;

    w = AllocIntWinding( w0->ptCount + w1->ptCount - ( dupCount * 2 - 2 ) );
    w->ptCount = 0;

    SanityCheck( dupCount > 1 );
    SanityCheck( w0->ptCount > 1 );

    for ( i = dupCount - 1; i < w0->ptCount; i++ )
    {
        w->pts[ w->ptCount ] = w0->pts[ ( start0 + i ) % w0->ptCount ];
        w->ptCount++;
    }

    SanityCheck( w->ptCount );

    i = 0;
    while ( i <= w1->ptCount - dupCount &&
            w1->pts[ ( start1 + i ) % w1->ptCount ] == w->pts[ w->ptCount - 1 ] )
    {
        i++;
    }

    for ( ; i <= w1->ptCount - dupCount; i++ )
    {
        w->pts[ w->ptCount ] = w1->pts[ ( start1 + i ) % w1->ptCount ];
        w->ptCount++;
    }

    while ( w->pts[ w->ptCount - 1 ] == w->pts[0] )
        w->ptCount--;

    return w;
}


/* MergeSurfaces  0x00457040 */
static int MergeSurfaces( int mode, winding_t **extraWinding, MergeCallback_t callback,
                          int flags )
{
    intWinding_t *intWindings[MAX_WINDINGS_PER_CONCAVE_GROUP];
    intWinding_t *extraIntWinding;
    winding_t    *firstExtra;
    int           i;

    Assertx( mergeGlob.surfCount >= 1, "(mergeGlob.surfCount) = %i", mergeGlob.surfCount );

    AddWindingsWithoutOverlap( mode );

    if ( mergeGlob.surfCount == 1 && !extraWinding )
        return 1;

    if ( mode == 2 )
    {
        qsort_vc8( mergeGlob.surfs + 1, mergeGlob.surfCount - 1, sizeof( TriSurf_t * ),
               MergeCompareSurfs );
    }
    else
    {
        qsort_vc8( mergeGlob.surfs, mergeGlob.surfCount, sizeof( TriSurf_t * ),
               MergeCompareSurfs );
    }

    TJunc_FixSurfaceList( mergeGlob.surfs, mergeGlob.surfCount,
                          extraWinding ? ( TriSurf_t * )extraWinding : NULL );

    firstExtra = extraWinding ? *extraWinding : NULL;
    MergeBuildVertMap( intWindings, firstExtra );

    extraIntWinding = NULL;
    if ( extraWinding )
    {
        extraIntWinding = MergeIntWindingFromWinding( *extraWinding );
        if ( !extraIntWinding )
            goto done;
    }

    mergeGlob.surfCount = callback( mergeGlob.surfs, intWindings, mergeGlob.surfCount,
                                    extraWinding, extraIntWinding, mode );

done:
    for ( i = 0; i < mergeGlob.surfCount; i++ )
    {
        mergeGlob.surfs[i]->w = MergeWindingFromIntWinding( intWindings[i] );
        FreeIntWinding( intWindings[i] );
    }

    if ( extraWinding && extraIntWinding )
    {
        *extraWinding = MergeWindingFromIntWinding( extraIntWinding );
        FreeIntWinding( extraIntWinding );
    }

    MergeFreeVertMap();

    return mergeGlob.surfCount;
}


/* MergeWindingFromIntWinding  0x00457230 */
static winding_t *MergeWindingFromIntWinding( const intWinding_t *intWinding )
{
    winding_t *w;
    int        i;

    Assert( intWinding );

    w = AllocWinding( intWinding->ptCount );
    w->ptCount = intWinding->ptCount;

    for ( i = 0; i < intWinding->ptCount; i++ )
        Vec3Copy( mergeGlob.vertMap[ intWinding->pts[i] ], w->pts[i] );

    return w;
}


/* MergeIntWindingFromWinding  0x004572d0 */
static intWinding_t *MergeIntWindingFromWinding( const winding_t *w )
{
    int           indices[MAX_MERGE_INDICES + 2];
    intWinding_t *iw;
    unsigned int  i;

    Assert( w->ptCount <= ARRAY_COUNT( indices ) );

    indices[0] = 0;

    for ( i = 0; i < w->ptCount; i++ )
    {
        indices[ indices[0] + 1 ] = MergeIndexForPoint( w->pts[i] );

        if ( indices[0] == 0 || indices[ indices[0] + 1 ] != indices[ indices[0] ] )
            indices[0]++;
    }

    while ( indices[0] != 0 && indices[ indices[0] ] == indices[1] )
        indices[0]--;

    if ( indices[0] > 2 )
        indices[0] = MergeRemoveDegenerateIndices( indices[0], indices + 1 );

    if ( indices[0] < 3 )
        return NULL;

    iw = AllocIntWinding( indices[0] );
    iw->ptCount = indices[0];
    memcpy( iw->pts, indices + 1, indices[0] * sizeof( int ) );

    return iw;
}


/* MergeBuildVertMap  0x004574c0 */
static void MergeBuildVertMap( intWinding_t **intWindings, const winding_t *extraWinding )
{
    unsigned int totalPoints;
    int          i;

    Assert( mergeGlob.vertMap == NULL );
    Assert( mergeGlob.vertCount == 0 );

    totalPoints = extraWinding ? extraWinding->ptCount : 0;
    for ( i = 0; i < mergeGlob.surfCount; i++ )
        totalPoints += mergeGlob.surfs[i]->w->ptCount;

    mergeGlob.vertMap   = ( vec3_t * )operator new[]( totalPoints * 3 * sizeof( vec3_t ) );
    mergeGlob.vertCount = 0;

    i = 0;
    while ( i < mergeGlob.surfCount )
    {
        intWindings[i] = MergeIntWindingFromWinding( mergeGlob.surfs[i]->w );
        FreeWinding( mergeGlob.surfs[i]->w );
        mergeGlob.surfs[i]->w = NULL;

        if ( intWindings[i] )
        {
            i++;
            continue;
        }

        mergeGlob.surfCount--;
        Tris_FreeSurface( mergeGlob.surfs[i] );
        memmove( &mergeGlob.surfs[i], &mergeGlob.surfs[i + 1],
                 ( mergeGlob.surfCount - i ) * sizeof( TriSurf_t * ) );
    }
}


/* MergeIndexForPoint  0x00457440 */
static int MergeIndexForPoint( const vec3_t xyz )
{
    unsigned int i;

    for ( i = 0; i < mergeGlob.vertCount; i++ )
    {
        if ( Vec3CompareEpsilon( xyz, mergeGlob.vertMap[i], MERGE_EPSILON ) )
            return i;
    }

    Vec3Copy( xyz, mergeGlob.vertMap[i] );
    mergeGlob.vertCount++;

    return i;
}


/* MergeFreeVertMap  0x00457670 */
static void MergeFreeVertMap( void )
{
    Assert( mergeGlob.vertMap );

    operator delete[]( mergeGlob.vertMap );
    mergeGlob.vertMap   = NULL;
    mergeGlob.vertCount = 0;
}


/* MergeCompareSurfs  0x004576d0 */
static int MergeCompareSurfs( const void *va, const void *vb )
{
    const TriSurf_t *surf0 = *( const TriSurf_t * const * )va;
    const TriSurf_t *surf1 = *( const TriSurf_t * const * )vb;
    int axisX;
    int axisY;

    GetProjectionAxes( surf0->props->plane, &axisX, &axisY );

    if ( surf0->mins[axisX] < surf1->mins[axisX] )
        return -1;
    if ( surf1->mins[axisX] < surf0->mins[axisX] )
        return 1;

    if ( surf0->mins[axisY] < surf1->mins[axisY] )
        return -1;
    if ( surf1->mins[axisY] < surf0->mins[axisY] )
        return 1;

    return 0;
}


/* AddWindingsWithoutOverlap  0x004577a0 */
static void AddWindingsWithoutOverlap( int mode )
{
    unsigned int first;
    unsigned int i;
    unsigned int newCount;
    TriSurf_t   *surf;

    if ( mode == 0 )
        return;

    first = ( mode == 2 );

    for ( i = first; ( int )i < mergeGlob.surfCount; )
    {
        surf     = mergeGlob.surfs[i];
        newCount = AddWindingWithoutOverlap_r( first, i, i, surf->w, surf->props );

        if ( newCount != i )
        {
            i = newCount;
            continue;
        }

        mergeGlob.surfs[i]->w = NULL;
        Tris_FreeSurface( mergeGlob.surfs[i] );
        mergeGlob.surfCount--;
        mergeGlob.surfs[i] = mergeGlob.surfs[ mergeGlob.surfCount ];
    }
}


/* AddWindingWithoutOverlap_r  0x00457880 */
static int AddWindingWithoutOverlap_r( int first, int surfCount, int prevSurfCount,
                                       winding_t *w, TriSurfProps_t *props )
{
    const winding_t *other;
    const vec4_t    *otherPlane;
    winding_t       *front;
    winding_t       *back;
    vec3_t           edge;
    vec3_t           edgeNormal;
    float            edgeDist;
    int              surfIndex;
    unsigned int     i;

    for ( surfIndex = first; surfIndex < prevSurfCount; surfIndex++ )
    {
        other      = mergeGlob.surfs[surfIndex]->w;
        otherPlane = &mergeGlob.surfs[surfIndex]->props->plane;

        if ( CheckWindingSeparationBoth( other, w, *otherPlane, MERGE_EPSILON ) )
            continue;

        for ( i = 0; i < other->ptCount; i++ )
        {
            Vec3Sub( other->pts[ ( i + 1 ) % other->ptCount ], other->pts[i], edge );
            Vec3Cross( *otherPlane, edge, edgeNormal );
            Vec3Normalize( edgeNormal );
            edgeDist = Vec3Dot( edgeNormal, other->pts[i] );

            Tris_ClipWindingEpsilon( w, other, edgeNormal, edgeDist, MERGE_EPSILON, &front, &back );

            if ( !back )
            {
                if ( !front )
                    Com_Error( "code bug: AddWindingWithoutOverlap_r: winding disappeared\n" );

                FreeWinding( w );
                w = front;
                break;
            }

            if ( front )
                surfCount = AddWindingWithoutOverlap_r( first + 1, surfCount,
                                                        prevSurfCount, front, props );

            FreeWinding( w );
            w = back;

            if ( i + 1 == other->ptCount )
            {
                FreeWinding( back );
                return surfCount;
            }
        }
    }

    if ( surfCount == MAX_WINDINGS_PER_CONCAVE_GROUP )
    {
        Com_Error( "MAX_WINDINGS_PER_CONCAVE_GROUP (%i) exceeded\nTry -blocksize 0.\n",
                   MAX_WINDINGS_PER_CONCAVE_GROUP );
    }

    AssertCmp( surfCount, >=, prevSurfCount );
    AssertCmp( surfCount, <=, mergeGlob.surfCount );

    if ( surfCount != prevSurfCount )
    {
        mergeGlob.surfs[ mergeGlob.surfCount ] = mergeGlob.surfs[surfCount];
        mergeGlob.surfs[surfCount] = AllocTriSurf( w, props );
        mergeGlob.surfCount++;
    }

    mergeGlob.surfs[surfCount]->w = w;
    SanityCheck( mergeGlob.surfs[surfCount]->props == props );

    return surfCount + 1;
}


/* Merge_LinkSurfsToVisGroup  0x00457bd0 */
void Merge_LinkSurfsToVisGroup( TriSurf_t **visGroupList )
{
    TriSurf_t *surf;
    int        i;

    Assert( visGroupList );

    for ( i = 0; i < mergeGlob.surfCount; i++ )
    {
        surf = mergeGlob.surfs[i];
        surf->visGroupNext = *visGroupList;
        if ( surf->visGroupNext )
            surf->visGroupNext->visGroupPrev = surf;
        *visGroupList = surf;
    }

    Merge_Clear();
}

void Merge_Clear( void )
{
    mergeGlob.surfCount = 0;
}

qboolean Merge_HasSurfs( void )
{
    return mergeGlob.surfCount > 0;
}

void Merge_PushSurf( TriSurf_t *surf, TriSurf_t **visGroupList )
{
    Assert( surf );

    if ( mergeGlob.surfCount == mergeGlob.surfLimit )
        Com_Error( "more than %i windings\n", mergeGlob.surfLimit );

    Tris_UnlinkSurf( surf, visGroupList );

    mergeGlob.surfs[ mergeGlob.surfCount ] = mergeGlob.surfs[0];
    mergeGlob.surfs[0] = surf;
    mergeGlob.surfCount++;
}

void Merge_AppendSurf( TriSurf_t *surf, TriSurf_t **visGroupList )
{
    Assert( surf );

    if ( mergeGlob.surfCount == mergeGlob.surfLimit )
        Com_Error( "more than %i windings\n", mergeGlob.surfLimit );

    Tris_UnlinkSurf( surf, visGroupList );

    mergeGlob.surfs[ mergeGlob.surfCount ] = surf;
    mergeGlob.surfCount++;
}

int Merge_SurfCount( void )
{
    return mergeGlob.surfCount;
}

void Merge_TruncateTo( int mark )
{
    AssertCmp( mark, <=, mergeGlob.surfCount );

    while ( mark < mergeGlob.surfCount )
    {
        mergeGlob.surfCount--;
        Tris_FreeSurface( mergeGlob.surfs[ mergeGlob.surfCount ] );
    }
}


/* MergeBuildGrid  0x00457e20 */
static void MergeBuildGrid( TriSurf_t *listHead, const vec3_t mins, const vec3_t maxs )
{
    TriSurf_t *surf;

    SetGridDivisionPoints( mins, maxs );

    for ( surf = listHead; surf; surf = surf->visGroupNext )
    {
        WindingBounds( surf->w, surf->mins, surf->maxs );
        Vec3Sub( surf->mins, s_mergeExpand, surf->mins );
        Vec3Add( surf->maxs, s_mergeExpand, surf->maxs );
        GridTree_Insert( surf );
    }

    TJunc_SetEpsilon( MERGE_EPSILON );
    TJunc_SetUseAxisBuckets( qfalse );
}


/* MergeConcaveWindings  0x00457ed0 */
void MergeConcaveWindings( TriSurf_t **visGroupList, const vec3_t mins, const vec3_t maxs,
                           MergeCallback_t callback )
{
    TriSurf_t *outList;
    TriSurf_t *surf;
    TriSurf_t *mergeSurf;
    int        i;

    Assert( visGroupList );

    if ( !*visGroupList )
        return;

    MergeBuildGrid( *visGroupList, mins, maxs );

    mergeGlob.visGroupList = visGroupList;
    outList = NULL;

    for ( surf = *visGroupList; surf; surf = *mergeGlob.visGroupList )
    {
        MergeGatherGroup( surf );
        MergeSurfaces( mergeGlob.mode, NULL, callback, 0 );

        for ( i = 0; i < mergeGlob.surfCount; i++ )
        {
            mergeSurf = mergeGlob.surfs[i];

            MergeFindSurfaceHoles( mergeSurf, mergeSurf == ( TriSurf_t * )0x3c6159e8 );

            if ( !MergeCleanSurface( mergeSurf, MERGE_EPSILON ) )
            {
                Tris_FreeSurface( mergeSurf );
                continue;
            }

            if ( MergeCutNotches( mergeSurf ) &&
                 !MergeCleanSurface( mergeSurf, MERGE_EPSILON ) )
            {
                Tris_FreeSurface( mergeSurf );
                continue;
            }

            mergeSurf->visGroupNext = outList;
            mergeSurf->visGroupPrev = NULL;
            if ( mergeSurf->visGroupNext )
                mergeSurf->visGroupNext->visGroupPrev = mergeSurf;

            SanityCheckx( mergeSurf->holeCount >= 0 && mergeSurf->holeCount <= MAX_HOLES,
                          "(mergeSurf->holeCount) = %i", mergeSurf->holeCount );

            outList = mergeSurf;

            SanityCheckx( ( mergeSurf->holeCount == 0 ) == ( mergeSurf->holes == 0 ),
                          "(mergeSurf->holeCount) = %i", mergeSurf->holeCount );
        }
    }

    mergeGlob.visGroupList = NULL;
    *visGroupList = outList;
}


/* MergeFindSurfaceHoles  0x00458110 */
static void MergeFindSurfaceHoles( TriSurf_t *surf, bool dbg )
{
    winding_t *holes[MAX_HOLES];
    float      holeArea[MAX_HOLES];
    winding_t *outline;

    surf->holeCount = 0;
    surf->holes     = NULL;

    if ( surf->w->ptCount <= 8 )
        return;

    outline = CopyWinding( surf->w );
    surf->holeCount = FindHoles( &outline, surf->props->plane, holes, holeArea, dbg );

    Assertx( surf->holeCount >= 0, "(surf->holeCount) = %i", surf->holeCount );

    if ( surf->holeCount )
    {
        surf->holes = ( winding_t ** )operator new[]( surf->holeCount * sizeof( winding_t * ) );
        memcpy( surf->holes, holes, surf->holeCount * sizeof( winding_t * ) );
    }

    FreeWinding( surf->w );
    surf->w = outline;
}


/* FindHoles  0x00458230 */
static int FindHoles( winding_t **inout, const vec4_t plane, winding_t **holes,
                      float *holeArea, bool dbg )
{
    winding_t   *sub[2];
    float        subArea[2];
    unsigned int begin0;
    unsigned int end0;
    unsigned int begin1;
    unsigned int end1;
    unsigned int outerIndex;
    unsigned int innerIndex;
    int          holeCount;

    holeCount = 0;

    for ( ;; )
    {
        if ( !MergeFindSelfTouch( *inout, &begin0, &begin1, &end0, &end1 ) )
            return holeCount;

        if ( begin0 == end0 )
            return -1;

        if ( begin1 == end0 )
        {
            Assert( begin0 == end1 );
            end0   = begin0;
            end1   = begin1;
            Assert( begin1 != begin0 );
        }
        else
        {
            Assert( begin0 != end1 );
        }

        MergeSubWinding( *inout, begin1, begin0, &sub[0] );
        MergeSubWinding( *inout, end0, end1, &sub[1] );

        subArea[0] = WindingAreaWithKnownNormal( sub[0], plane );
        subArea[1] = WindingAreaWithKnownNormal( sub[1], plane );

        if ( ( subArea[0] >= 0.0f && subArea[1] >= 0.0f ) || begin0 == end0 )
        {
            outerIndex = ( subArea[0] < subArea[1] );
            innerIndex = 1 - outerIndex;
            FreeWinding( sub[innerIndex] );
        }
        else
        {
            if ( subArea[0] < 0.0f && subArea[1] < 0.0f )
            {
                SanityCheckx( subArea[0] >= 0 || subArea[1] >= 0,
                              "%s", va( "%g, %g", subArea[0], subArea[1] ) );
            }

            if ( subArea[0] < 0.0f )
            {
                innerIndex = 0;
                outerIndex = 1;
            }
            else
            {
                innerIndex = 1;
                outerIndex = 0;
            }

            SanityCheckCmpFloat( subArea[outerIndex], >, -subArea[innerIndex] );

            holeCount = AddHole( sub[innerIndex], subArea[innerIndex], plane,
                                 holes, holeArea, holeCount );
        }

        FreeWinding( *inout );
        *inout = sub[outerIndex];
    }
}


/* AddHole  0x00458780 */
static int AddHole( winding_t *w, float area, const vec4_t plane,
                    winding_t **holes, float *holeArea, int holeCount )
{
    winding_t   *sub[2];
    winding_t   *hull;
    winding_t   *visible;
    float        subArea[2];
    float        visibleArea;
    unsigned int begin0;
    unsigned int begin1;
    unsigned int end0;
    unsigned int end1;
    int          i;

    Assert( w );
    Assertx( w->ptCount >= 3, "(w->ptCount) = %i", w->ptCount );
    Assert( area < 0 );
    Assert( plane );
    Assert( holes );
    Assert( holeArea );
    Assertx( holeCount >= 0 && holeCount + 1 < MAX_HOLES, "(holeCount) = %i", holeCount );

    if ( MergeFindSelfTouch( w, &begin0, &begin1, &end0, &end1 ) )
    {
        if ( begin0 == end0 )
        {
            FreeWinding( w );
            return holeCount;
        }

        MergeSubWinding( w, begin1, begin0, &sub[0] );
        MergeSubWinding( w, end0, end1, &sub[1] );
        FreeWinding( w );

        for ( i = 0; i < 2; i++ )
        {
            subArea[i] = WindingAreaWithKnownNormal( sub[i], plane );

            if ( subArea[i] < 0.0f )
                holeCount = AddHole( sub[i], subArea[i], plane, holes, holeArea, holeCount );
            else
                FreeWinding( sub[i] );
        }

        return holeCount;
    }

    if ( !MergeHoleAreaIsSignificant( w->ptCount, -area ) )
    {
        hull = ReverseWinding( w );

        if ( !WindingIsConvex( hull, plane ) )
        {
            FreeWinding( hull );
        }
        else
        {
            visible = BrushSides_ClipWinding( hull, NULL, plane );

            if ( !visible )
            {
                FreeWinding( w );           /* 0x00458a3f */
                return holeCount;
            }

            if ( visible == hull )
            {
                FreeWinding( hull );
            }
            else
            {
                visibleArea = WindingAreaWithKnownNormal( visible, plane );

                if ( visibleArea == 0.0f )
                {
                    FreeWinding( visible );
                    FreeWinding( w );       /* 0x00458a9b */
                    return holeCount;
                }

                AssertCmpFloat( visibleArea, <=, -area + MERGE_EQUAL_EPSILON );

                if ( visibleArea >= -area - MERGE_EQUAL_EPSILON ||
                     MergeHoleAreaIsSignificant( visible->ptCount, visibleArea ) )
                {
                    FreeWinding( w );       /* 0x00458b34 */
                    w    = ReverseWinding( visible );
                    FreeWinding( visible );
                    area = -visibleArea;
                }
                else
                {
                    FreeWinding( visible );
                }
            }
        }
    }

    for ( i = holeCount; i > 0 && holeArea[i - 1] < -area; i-- )
    {
        holes[i]    = holes[i - 1];
        holeArea[i] = holeArea[i - 1];
    }

    holes[i]    = w;
    holeArea[i] = -area;

    return holeCount + 1;
}


/* MergeFindSelfTouch  0x00458510 */
static bool MergeFindSelfTouch( const winding_t *w, unsigned int *begin0,
                                unsigned int *begin1, unsigned int *end0,
                                unsigned int *end1 )
{
    unsigned int i;
    unsigned int j;
    unsigned int other;
    unsigned int forward;
    unsigned int backward;

    for ( i = 0; i < w->ptCount; i++ )
    {
        for ( j = 3; j < w->ptCount - 3; j++ )
        {
            other = ( i + j ) % w->ptCount;

            if ( !Vec3CompareEpsilon( w->pts[i], w->pts[other], MERGE_EPSILON ) )
                continue;

            forward  = ( i + 1 ) % w->ptCount;
            backward = ( other - 1 + w->ptCount ) % w->ptCount;

            if ( !Vec3CompareEpsilon( w->pts[forward], w->pts[backward], MERGE_EPSILON ) )
                continue;

            *begin0 = i;
            *begin1 = other;

            if ( i == 0 )
            {
                i = w->ptCount;
                other = ( other + 1 ) % w->ptCount;

                for ( ;; )
                {
                    i--;
                    if ( !Vec3CompareEpsilon( w->pts[i], w->pts[other], MERGE_EPSILON ) )
                        break;

                    *begin0 = i;
                    *begin1 = other;
                    other = ( other + 1 ) % w->ptCount;
                }
            }

            for ( ;; )
            {
                *end0 = forward;
                *end1 = backward;

                forward  = ( forward + 1 ) % w->ptCount;
                backward = ( backward - 1 + w->ptCount ) % w->ptCount;

                if ( *begin0 == *end0 )
                    return true;

                if ( !Vec3CompareEpsilon( w->pts[forward], w->pts[backward], MERGE_EPSILON ) )
                    return true;
            }
        }
    }

    return false;
}


/* MergeSubWinding  0x00458710 */
static void MergeSubWinding( const winding_t *w, int first, int last, winding_t **out )
{
    int count;

    count = last - first;
    if ( count < 0 )
        count += w->ptCount;

    *out = AllocWinding( count );
    ( *out )->ptCount = 0;

    MergeAppendRun( w, first, ( last - 1 + w->ptCount ) % w->ptCount, *out );
}


/* MergeHoleAreaIsSignificant  0x00458bf0 */
static bool MergeHoleAreaIsSignificant( int ptCount, float area )
{
    if ( area < MERGE_TINY_AREA )
        return false;

    return ( float )ptCount * MERGE_AREA_PER_POINT <= area;
}


/* MergeCutNotches  0x00458c30 */
static bool MergeCutNotches( TriSurf_t *surf )
{
    unsigned int i;
    unsigned int end;
    int          axisX;
    int          axisY;
    int          notchLength;
    bool         changed;

    if ( surf->w->ptCount < 5 )
        return false;

    GetProjectionAxes( surf->props->plane, &axisX, &axisY );

    changed = false;
    i       = 0;

    while ( i < surf->w->ptCount )
    {
        notchLength = MergeFindNotch( surf->w, i, surf->props->plane );
        if ( !notchLength )
        {
            i++;
            continue;
        }

        end = i + 1 + notchLength;

        if ( MergeNotchWedge( &surf->w, i, end, surf->props->plane, axisX, axisY ) )
        {
            end++;
            notchLength++;
        }

        if ( surf->w->ptCount < end )
        {
            memmove( surf->w->pts[0], surf->w->pts[ end - surf->w->ptCount ],
                     ( surf->w->ptCount - notchLength ) * sizeof( vec3_t ) );
        }
        else
        {
            memmove( surf->w->pts[i + 1], surf->w->pts[end],
                     ( surf->w->ptCount - end ) * sizeof( vec3_t ) );
        }

        surf->w->ptCount -= notchLength;
        changed = true;
        i       = 0;
    }

    return changed;
}


/* MergeFindNotch  0x00458db0 */
static int MergeFindNotch( const winding_t *w, unsigned int begin, const vec3_t normal )
{
    winding_t   *notch;
    unsigned int next;
    unsigned int cur;
    unsigned int prev;
    unsigned int i;
    int          length;

    next   = ( begin + 1 ) % w->ptCount;
    prev   = begin;
    cur    = next;
    length = 0;

    for ( ;; )
    {
        i = ( cur + 1 ) % w->ptCount;

        Assert( i != begin );

        if ( WindingCornerIsConcave( w, i, cur, prev, normal ) )
            break;
        if ( WindingCornerIsConcave( w, begin, i, cur, normal ) )
            break;
        if ( WindingCornerIsConcave( w, next, begin, i, normal ) )
            break;

        prev = cur;
        length++;
        cur = i;
    }

    while ( length )
    {
        notch = AllocWinding( length + 2 );
        notch->ptCount = length + 2;

        for ( i = 0; i < notch->ptCount; i++ )
        {
            Vec3Copy( w->pts[ ( begin + notch->ptCount - i - 1 ) % w->ptCount ],
                      notch->pts[i] );
        }

        Assert( WindingIsConvex( notch, normal ) );

        if ( !BrushSides_IsWindingVisible( notch ) )
            break;

        length--;
    }

    return length;
}


/* MergeNotchWedge  0x00458f80 */
static bool MergeNotchWedge( winding_t **inout, unsigned int i0, unsigned int j0,
                             const vec4_t plane, int axisX, int axisY )
{
    winding_t *w;
    winding_t *tri;
    vec3_t     dir0;
    vec3_t     dir1;
    vec3_t     between;
    vec3_t     hit0;
    vec3_t     hit1;
    vec3_t     hit;
    float      d00;
    float      d01;
    float      d11;
    float      det;
    float      r0;
    float      r1;
    float      t0;
    float      t1;
    float      planeDist;
    unsigned int i1;
    unsigned int j1;

    w  = *inout;
    i1 = ( i0 - 1 + w->ptCount ) % w->ptCount;
    j1 = j0 % w->ptCount;
    j0 = ( j0 + 1 ) % w->ptCount;

    if ( j0 == i1 )
        return false;

    Assert( i1 != j1 && i0 != j1 && i0 != j0 );

    Vec3Sub( w->pts[i0], w->pts[i1], dir0 );
    Vec3Sub( w->pts[j0], w->pts[j1], dir1 );

    d00 = Vec3Dot( dir0, dir0 );
    d01 = Vec3Dot( dir0, dir1 );
    d11 = Vec3Dot( dir1, dir1 );

    det = d00 * d11 - d01 * d01;
    if ( det <= 1.0f )
        return false;

    Vec3Sub( w->pts[j1], w->pts[i0], between );

    r0 = Vec3Dot( dir0, between );
    r1 = Vec3Dot( dir1, between );

    t0 = ( r0 * d11 - r1 * d01 ) / det;
    if ( t0 <= MERGE_EQUAL_EPSILON )
        return false;

    t1 = ( r1 * d00 - r0 * d01 ) / det;
    if ( t1 <= MERGE_EQUAL_EPSILON )
        return false;

    Vec3Mad( w->pts[i0], t0, dir0, hit0 );
    Vec3Mad( w->pts[j1], -t1, dir1, hit1 );
    Vec3Mid( hit0, hit1, hit );

    planeDist = Vec3Dot( hit, plane ) - plane[3];
    Vec3Mad( hit, -planeDist, plane, hit );

    tri = AllocWinding( 3 );
    tri->ptCount = 3;
    Vec3Copy( hit,          tri->pts[0] );
    Vec3Copy( w->pts[j1],   tri->pts[1] );
    Vec3Copy( w->pts[i0],   tri->pts[2] );

    if ( WindingAreaWithKnownNormal( tri, plane ) < MERGE_EQUAL_EPSILON ||
         WindingMinWidth( tri, plane ) > 8.001f )
    {
        FreeWinding( tri );
        return false;
    }

    if ( BrushSides_IsWindingVisible( tri ) )
        return false;

    if ( !MergeSurfaceAllowsEdge( ( TriSurf_t * )inout, hit, w->pts[i0], axisX, axisY ) )
        return false;

    if ( !MergeSurfaceAllowsEdge( ( TriSurf_t * )inout, hit, w->pts[j1], axisX, axisY ) )
        return false;

    Vec3Copy( hit, ( *inout )->pts[i0] );

    return true;
}


/* MergeSurfaceAllowsEdge  0x00459370 */
static bool MergeSurfaceAllowsEdge( TriSurf_t *surf, const vec3_t v0, const vec3_t v1,
                                    int axisX, int axisY )
{
    if ( MergeWindingCrossesEdge( surf->w, v0, v1, axisX, axisY ) )
        return false;

    if ( MergeAnyHoleCrossesEdge( surf->holes, surf->holeCount, v0, v1, axisX, axisY ) )
        return false;

    return true;
}


/* MergeGatherGroup  0x004593e0 */
static void MergeGatherGroup( TriSurf_t *surf )
{
    int i;

    Assert( mergeGlob.GroupableCallback );
    Assert( mergeGlob.surfs );

    mergeGlob.checkCount++;
    mergeGlob.surfs[0] = surf;
    mergeGlob.surfCount = 1;

    Tris_UnlinkSurf( surf, mergeGlob.visGroupList );

    for ( i = 0; i < mergeGlob.surfCount; i++ )
    {
        mergeGlob.checkSurf = mergeGlob.surfs[i];
        SanityCheck( mergeGlob.checkSurf );

        GridTree_ForEach( mergeGlob.checkSurf->mins,
                                  mergeGlob.checkSurf->maxs,
                                  MergeGatherCallback );
    }
}


/* MergeGatherCallback  0x004594f0 */
static void MergeGatherCallback( TriSurf_t *surf )
{
    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_MERGE_CHECK_COUNT );

    if ( surf->transient.checkCount == mergeGlob.checkCount )
        return;

    surf->transient.checkCount = mergeGlob.checkCount;

    if ( mergeGlob.GroupableCallback( mergeGlob.surfs[0], surf ) )
        Merge_AppendSurf( surf, mergeGlob.visGroupList );
}


/* MergeGatherNeighbours  0x00459690 */
static void MergeGatherNeighbours( TriSurf_t *surf )
{
    int i;

    Assert( mergeGlob.GroupableCallback );
    Assert( mergeGlob.surfs );

    mergeGlob.checkCount++;
    mergeGlob.checkSurf = surf;
    mergeGlob.surfCount = 0;

    i = -1;
    for ( ;; )
    {
        SanityCheck( mergeGlob.checkSurf );

        GridTree_ForEach( mergeGlob.checkSurf->mins,
                                  mergeGlob.checkSurf->maxs,
                                  MergeGatherNeighbourCallback );

        i++;
        if ( i == mergeGlob.surfCount )
            return;

        mergeGlob.checkSurf = mergeGlob.surfs[i];
    }
}


/* MergeGatherNeighbourCallback  0x00459790 */
static void MergeGatherNeighbourCallback( TriSurf_t *surf )
{
    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_MERGE_CHECK_COUNT );

    if ( surf->transient.checkCount == mergeGlob.checkCount )
        return;

    surf->transient.checkCount = mergeGlob.checkCount;

    if ( !mergeGlob.GroupableCallback( mergeGlob.checkSurf, surf ) )
        return;

    if ( mergeGlob.surfCount == mergeGlob.surfLimit )
        Com_Error( "more than %i windings\n", mergeGlob.surfLimit );

    mergeGlob.surfs[ mergeGlob.surfCount ] = CopyTriSurf( surf );
    mergeGlob.surfCount++;
}


/* MergeSurfaceIntoNeighbours  0x00459570 */
void MergeSurfaceIntoNeighbours( TriSurf_t **visGroupList, const vec3_t mins,
                                 const vec3_t maxs, qboolean ( *skip )( TriSurf_t * ),
                                 MergeCallback_t callback )
{
    TriSurf_t   *surf;
    TriSurf_t   *next;
    unsigned int oldPtCount;
    int          i;

    MergeBuildGrid( *visGroupList, mins, maxs );

    surf = *visGroupList;
    while ( surf )
    {
        next = surf->visGroupNext;

        if ( !skip( surf ) )
        {
            MergeGatherNeighbours( surf );

            if ( mergeGlob.surfCount )
            {
                oldPtCount = surf->w->ptCount;

                MergeSurfaces( mergeGlob.mode, &surf->w, callback, 0 );

                if ( surf->w->ptCount != oldPtCount &&
                     !MergeCleanSurface( surf, MERGE_EPSILON ) )
                {
                    Tris_UnlinkSurf( surf, visGroupList );
                    Tris_FreeSurface( surf );
                }

                for ( i = 0; i < mergeGlob.surfCount; i++ )
                    Tris_FreeSurface( mergeGlob.surfs[i] );
            }
        }

        surf = next;
    }
}


/* MergeCleanSurface  0x00456780 */
static bool MergeCleanSurface( TriSurf_t *surf, float epsilon )
{
    int axisX;
    int axisY;
    int holeIndex;

    GetProjectionAxes( surf->props->plane, &axisX, &axisY );

    MergeRemoveColinearPoints( surf->w, axisX, axisY, epsilon );
    if ( surf->w->ptCount < 3 )
        return false;

    holeIndex = 0;
    while ( holeIndex < surf->holeCount )
    {
        MergeRemoveColinearPoints( surf->holes[holeIndex], axisX, axisY, epsilon );

        if ( surf->holes[holeIndex]->ptCount >= 3 &&
             WindingAreaWithKnownNormal( surf->holes[holeIndex], surf->props->plane ) < 0.0f )
        {
            holeIndex++;
            continue;
        }

        FreeWinding( surf->holes[holeIndex] );
        surf->holeCount--;

        if ( surf->holeCount == 0 )
        {
            operator delete[]( surf->holes );
            surf->holes = NULL;
            return true;
        }

        surf->holes[holeIndex] = surf->holes[ surf->holeCount ];
    }

    return true;
}


/* Merge_Init  0x00459850 */
void Merge_Init( unsigned int surfLimit, MergeGroupableCallback_t callback, int mode )
{
    Assert( mergeGlob.GroupableCallback == NULL );
    Assert( mergeGlob.surfs == NULL );

    mergeGlob.GroupableCallback = callback;
    mergeGlob.mode              = mode;
    mergeGlob.surfLimit         = surfLimit;

    mergeGlob.surfs = ( TriSurf_t ** )operator new[]( surfLimit * sizeof( TriSurf_t * ) );
    if ( !mergeGlob.surfs )
        Com_Error( "Couldn't allocate memory for merging up to %i windings\n", surfLimit );

    TJunc_Init();
}

void Merge_Shutdown( void )
{
    TJunc_Shutdown();

    Assert( mergeGlob.surfs != NULL );

    operator delete[]( mergeGlob.surfs );
    mergeGlob.surfs             = NULL;
    mergeGlob.GroupableCallback = NULL;
}
