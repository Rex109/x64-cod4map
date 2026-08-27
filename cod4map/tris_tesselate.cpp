/* Original: .\tris_tesselate.cpp */

#include "tris_tesselate.h"

#include "cod4map.h"
#include "tris.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "qsort_vc8.h"


extern int  GetTrisTransientMode( void );                           /* 0x0043c6c0 */
extern void SetTrisTransientMode( int mode, int keepExisting );     /* 0x0043c6d0 */

extern int  numCells;                                               /* 0x06996f98 */
extern int  numCullGroups;                                          /* 0x06741c30 */


typedef struct
{
    int   command;
    float tolerance;
} triClipEarCtrl_t;

static const triClipEarCtrl_t triCtrl[NUM_TRI_CLIP_EAR_PASSES] =
{
    { TRI_CLIP_EAR_FORBIDDING_COINCIDENT, 2.0f    },
    { TRI_CLIP_EAR_FORBIDDING_COINCIDENT, 0.5f    },
    { TRI_CLIP_EAR_FORBIDDING_COINCIDENT, 0.125f  },
    { TRI_CLIP_EAR_FORBIDDING_COINCIDENT, 0.01f   },
    { TRI_CLIP_EAR_ALLOWING_COINCIDENT,   2.0f    },
    { TRI_CLIP_EAR_ALLOWING_COINCIDENT,   0.5f    },
    { TRI_CLIP_EAR_ALLOWING_COINCIDENT,   0.125f  },
    { TRI_CLIP_EAR_ALLOWING_COINCIDENT,   0.01f   },
    { TRI_CLIP_EAR_FORBIDDING_COINCIDENT, 0.0005f },
    { TRI_CLIP_EAR_FORBIDDING_COINCIDENT, 0.0f    },
    { TRI_CLIP_EAR_ALLOWING_COINCIDENT,   0.0005f },
    { TRI_CLIP_EAR_ALLOWING_COINCIDENT,   0.0f    },
};


static float DistanceFromChord( const winding_t *w, unsigned int i, int axisX, int axisY );
static bool PointBehindSide( const triSide_t *side, const vec3_t v, int axisX, int axisY );
static bool PointOutsideSide( const triSide_t *side, const vec3_t v, int axisX, int axisY );
static bool PointOutsideTriangle( const vec3_t v, int axisX, int axisY,
                                  const triSide_t *sideA, const triSide_t *sideB,
                                  const triSide_t *sideC );
static void SetupTriSide( const vec3_t v0, const vec3_t v1, int axisX, int axisY,
                          triSide_t *side );
static int TessSplitPointCompare( const void *va, const void *vb );
static bool TesselateCheckEarValidity( const winding_t *w,
                                       unsigned int earPrev, unsigned int earTip,
                                       unsigned int earNext,
                                       int axisX, int axisY, bool allowCoincident,
                                       const triSide_t *side0, const triSide_t *side1,
                                       const triSide_t *side2 );
static void TesselateClipEar( tessWork_t *work, unsigned int ear );
static int TesselateEdgesIntersect( const vec3_t a0, const vec3_t a1,
                                    const vec3_t b0, const vec3_t b1,
                                    int axisX, int axisY,
                                    vec3_t hit, float *tA, float *tB );
static bool TesselateFindBestEar( tessWork_t *work );
static bool TesselateFixIntersections( tessWork_t *work, winding_t *w, float tolerance );
static void TesselateInsertVertices( const tessSplitPoint_t *splits, unsigned int splitCount,
                                     winding_t **w );
void TesselateNoopCallback( const winding_t *w, const winding_t *wOrig,
                            TriSurfProps_t *props,
                            int i0, int i1, int i2, int visGroupIndex );
static void TesselateRemoveDegenerateIndices( tessWork_t *work );
static void TesselateRemoveVertex( tessWork_t *work, unsigned int index );
static bool TrisCheckBothEdges( const vec3_t v0, const vec3_t v1, int axisX, int axisY,
                                const triSide_t *sideA, const triSide_t *sideB,
                                const triSide_t *sideC );
static double TrisLargestAngleCosine( double lenA, double lenB, double lenC );
static bool TrisSetupTrianglePlanes( const vec3_t v0, const vec3_t v1, const vec3_t v2,
                                     int axisX, int axisY, float tolerance,
                                     triSide_t *side0, triSide_t *side1, triSide_t *side2 );
static bool TrisVerticesCoincident2D( const winding_t *w, int axisX, int axisY,
                                      unsigned int a0, unsigned int a1,
                                      unsigned int b0, unsigned int b1 );

int g_tessSplitCount;        /* 0x2b8209d0 */
int g_tessDegenerateCount;   /* 0x2b8209d4 */
int g_tessWindingCount;      /* 0x2b8209d8 */


/* SignedDistanceAlongSide  0x0045ab90 */
static double SignedDistanceAlongSide( const triSide_t *side, const vec3_t v, int x, int y )
{
    return v[x] * side->normal[1] - v[y] * side->normal[0] - side->alongDist;
}


/* SignedDistanceFromSide  0x0045abc0 */
static double SignedDistanceFromSide( const triSide_t *side, const vec3_t v, int x, int y )
{
    return v[x] * side->normal[0] + v[y] * side->normal[1] - side->dist;
}


/* TrisTestWindingBin  0x0045abf0 */
void TrisTestWindingBin( void )
{
    TriSurf_t *surf;

    surf = TrisLoadWindingBin();
    if ( !surf )
        return;

    SetTrisTransientMode( TRIS_TRANSIENT_WORIG, 1 );
    TesselateSurfaceWinding( surf );
    TesselateWinding( surf, CELLNUM_UNKNOWN, TesselateNoopCallback );
    SetTrisTransientMode( TRIS_TRANSIENT_NONE, 1 );
}


/* TrisLoadWindingBin  0x0045ac40 */
TriSurf_t *TrisLoadWindingBin( void )
{
    FILE           *file;
    TriSurfProps_t *props;
    winding_t      *w;
    winding_t      *wOrig;
    TriSurf_t      *surf;
    size_t          ptCount;

    file = fopen( "winding.bin", "rb" );
    if ( !file )
        return NULL;

    props = ( TriSurfProps_t * )operator new( sizeof( TriSurfProps_t ) );
    fread( props, sizeof( TriSurfProps_t ), 1, file );
    fread( &ptCount, sizeof( int ), 1, file );

    w = AllocWinding( ptCount );
    w->ptCount = ptCount;
    wOrig = AllocWinding( ptCount );
    wOrig->ptCount = ptCount;

    fread( w->pts, sizeof( vec3_t ), ptCount, file );
    fread( wOrig->pts, sizeof( vec3_t ), ptCount, file );
    fclose( file );

    surf = ( TriSurf_t * )operator new( sizeof( TriSurf_t ) );
    memset( surf, 0, sizeof( TriSurf_t ) );
    surf->props = props;
    surf->w = w;
    surf->transient.wOrig = wOrig;

    return surf;
}


/* TesselateNoopCallback  0x0045ad60 */
void TesselateNoopCallback( const winding_t *w, const winding_t *wOrig,
                            TriSurfProps_t *props,
                            int i0, int i1, int i2, int visGroupIndex )
{
}


/* TesselateFailed  0x0045ad70 */
void TesselateFailed( const TriSurf_t *surf )
{
    TrisSaveWindingBin( surf );
    Com_Error( "code bug: triangulation failed!\n"
               "Include 'winding.bin' from the cod2map directory in your bug report.\n" );
}


/* TrisSaveWindingBin  0x0045ad90 */
void TrisSaveWindingBin( const TriSurf_t *surf )
{
    FILE *file;

    file = fopen( "winding.bin", "wb" );
    if ( !file )
        return;

    fwrite( surf->props, sizeof( TriSurfProps_t ), 1, file );
    fwrite( &surf->w->ptCount, sizeof( int ), 1, file );
    fwrite( surf->w->pts, sizeof( vec3_t ), surf->w->ptCount, file );
    fwrite( surf->transient.wOrig->pts, sizeof( vec3_t ), surf->w->ptCount, file );
    fclose( file );
}


/* TesselateSurfaceWinding  0x0045ae40 */
void TesselateSurfaceWinding( TriSurf_t *surf )
{
    if ( surf->w->ptCount == 3 )
        return;

    if ( GetTrisTransientMode() == TRIS_TRANSIENT_WORIG )
    {
        TesselateSplitWinding( &surf->w, &surf->transient.wOrig, surf->props->plane );
        TesselateRemoveDegenerateEdges( surf->w, surf->transient.wOrig );
    }
    else
    {
        TesselateSplitWinding( &surf->w, NULL, surf->props->plane );
        TesselateRemoveDegenerateEdges( surf->w, NULL );
    }
}


/* TesselateSplitWinding  0x0045aec0 */
void TesselateSplitWinding( winding_t **w, winding_t **wOrig, const vec3_t planeNormal )
{
    tessSplitPoint_t splits[MAX_POINTS_ON_CONCAVE_WINDING];
    unsigned int splitCount;
    unsigned int i;
    unsigned int j;
    unsigned int prev;
    unsigned int testIdx;
    int   axisX;
    int   axisY;
    vec2_t dir;
    float edgeLen;
    float edgeProjOfs;
    float edgePerpOfs;
    float perpDist;
    float projDist;
    float chordDist;

    GetProjectionAxes( planeNormal, &axisX, &axisY );

    splitCount = 0;
    prev = ( *w )->ptCount - 1;

    for ( i = 0; i < ( *w )->ptCount; i++ )
    {
        dir[0] = ( *w )->pts[i][axisX] - ( *w )->pts[prev][axisX];
        dir[1] = ( *w )->pts[i][axisY] - ( *w )->pts[prev][axisY];
        edgeLen = Vec2Normalize( dir );

        edgeProjOfs = dir[0] * ( *w )->pts[prev][axisX] + dir[1] * ( *w )->pts[prev][axisY];
        edgePerpOfs = dir[1] * ( *w )->pts[prev][axisX] - dir[0] * ( *w )->pts[prev][axisY];

        for ( j = 1; j < ( *w )->ptCount - 1; j++ )
        {
            testIdx = ( i + j ) % ( *w )->ptCount;

            perpDist = I_fabs( dir[1] * ( *w )->pts[testIdx][axisX] -
                               dir[0] * ( *w )->pts[testIdx][axisY] - edgePerpOfs );
            if ( perpDist >= TESS_SPLIT_PERP_EPSILON )
                continue;

            projDist = dir[0] * ( *w )->pts[testIdx][axisX] +
                       dir[1] * ( *w )->pts[testIdx][axisY] - edgeProjOfs;
            if ( projDist <= TESS_SPLIT_EDGE_MARGIN )
                continue;
            if ( projDist >= edgeLen - TESS_SPLIT_EDGE_MARGIN )
                continue;

            chordDist = DistanceFromChord( *w, testIdx, axisX, axisY );
            if ( perpDist > chordDist + chordDist )
                continue;

            splits[splitCount].edgeIdx  = prev;
            splits[splitCount].position = projDist;
            splits[splitCount].vertIdx  = testIdx;
            splitCount++;
        }

        prev = i;
    }

    if ( splitCount == 0 )
        return;

    g_tessSplitCount += splitCount;

    if ( ( int )splitCount > 1 )
        qsort_vc8( splits, splitCount, sizeof( splits[0] ), TessSplitPointCompare );

    TesselateInsertVertices( splits, splitCount, w );
    if ( wOrig )
        TesselateInsertVertices( splits, splitCount, wOrig );
}


/* TessSplitPointCompare  0x0045b1d0 */
static int TessSplitPointCompare( const void *va, const void *vb )
{
    const tessSplitPoint_t *a = ( const tessSplitPoint_t * )va;
    const tessSplitPoint_t *b = ( const tessSplitPoint_t * )vb;

    if ( a->edgeIdx != b->edgeIdx )
        return a->edgeIdx - b->edgeIdx;

    if ( a->position >= b->position )
        return 1;

    return -1;
}


/* TesselateInsertVertices  0x0045b230 */
static void TesselateInsertVertices( const tessSplitPoint_t *splits, unsigned int splitCount,
                                     winding_t **w )
{
    winding_t   *newW;
    unsigned int dst;
    unsigned int si;
    unsigned int src;

    Assert( w );
    Assert( *w );

    newW = AllocWinding( ( *w )->ptCount + splitCount );

    dst = 0;
    si  = 0;

    for ( src = 0; src < ( *w )->ptCount; src++ )
    {
        Vec3Copy( ( *w )->pts[src], newW->pts[dst] );
        dst++;

        for ( ; si < splitCount && splits[si].edgeIdx == ( int )src; si++ )
        {
            if ( si == 0 || splits[si - 1].edgeIdx != ( int )src ||
                 splits[si].position - splits[si - 1].position > EQUAL_EPSILON )
            {
                Vec3Copy( ( *w )->pts[ splits[si].vertIdx ], newW->pts[dst] );
                dst++;
            }
        }
    }

    newW->ptCount = dst;
    FreeWinding( *w );
    *w = newW;
}


/* DistanceFromChord  0x0045b3d0 */
static float DistanceFromChord( const winding_t *w, unsigned int i, int axisX, int axisY )
{
    unsigned int prev;
    unsigned int next;
    vec2_t dir;
    float  dist;

    prev = ( i == 0 ) ? w->ptCount : i;
    prev = prev - 1;

    next = ( i + 1 == w->ptCount ) ? 0 : i + 1;

    dir[0] = w->pts[next][axisX] - w->pts[prev][axisX];
    dir[1] = w->pts[next][axisY] - w->pts[prev][axisY];
    Vec2Normalize( dir );

    dist = dir[1] * w->pts[prev][axisX] - dir[0] * w->pts[prev][axisY];

    return I_fabs( dir[1] * w->pts[i][axisX] - dir[0] * w->pts[i][axisY] - dist );
}


/* TesselateRemoveDegenerateEdges  0x0045b500 */
void TesselateRemoveDegenerateEdges( winding_t *w, winding_t *wOrig )
{
    unsigned int im2;
    unsigned int im1;
    unsigned int i;

    for ( ;; )
    {
        im2 = w->ptCount - 2;
        im1 = w->ptCount - 1;

        for ( i = 0; i < w->ptCount; i++ )
        {
            if ( Vec3Compare( w->pts[im2], w->pts[i] ) )
                break;

            im2 = im1;
            im1 = i;
        }

        if ( i >= w->ptCount )
            return;

        w->ptCount -= 2;
        if ( wOrig )
            wOrig->ptCount -= 2;

        if ( w->ptCount < 3 )
        {
            g_tessDegenerateCount += 2;
            return;
        }

        g_tessDegenerateCount++;

        if ( im1 == w->ptCount || im2 == w->ptCount )
            continue;

        if ( im1 == 0 )
        {
            memmove( w->pts, w->pts + 2, w->ptCount * sizeof( vec3_t ) );
            if ( wOrig )
                memmove( wOrig->pts, wOrig->pts + 2, wOrig->ptCount * sizeof( vec3_t ) );
        }
        else
        {
            Assert( w->ptCount + 2 - i > 0 );
            Assert( im2 < i );
            memmove( w->pts[im2], w->pts[i], ( w->ptCount - im2 ) * sizeof( vec3_t ) );
            if ( wOrig )
                memmove( wOrig->pts[im2], wOrig->pts[i], ( wOrig->ptCount - im2 ) * sizeof( vec3_t ) );
        }
    }
}


/* TesselateWinding  0x0045b6f0 */
qboolean TesselateWinding( TriSurf_t *surf, int visGroupIndex, TessTriCallback_t triCallback )
{
    vec4_t     plane;
    tessWork_t work;
    unsigned int i;

    g_tessWindingCount++;

    Assert( visGroupIndex == CELLNUM_UNKNOWN ||
            ( visGroupIndex >= 0 && visGroupIndex < numCells + numCullGroups ) );

    work.ownsWinding   = qfalse;
    work.w             = surf->w;
    work.props         = surf->props;
    work.visGroupIndex = visGroupIndex;

    if ( GetTrisTransientMode() == TRIS_TRANSIENT_WORIG )
        work.wOrig = surf->transient.wOrig;
    else
        work.wOrig = NULL;

    if ( work.w->ptCount == 3 )
    {
        triCallback( work.w, work.wOrig, work.props, 0, 1, 2, work.visGroupIndex );
        return qtrue;
    }

    if ( !PlaneFromWinding( work.w, plane ) )
        return qtrue;

    GetProjectionAxes( plane, &work.axisX, &work.axisY );
    work.TriCallback = triCallback;

    work.ptCount = work.w->ptCount;
    for ( i = 0; i < work.ptCount; i++ )
        work.indices[i] = i;

    for ( ;; )
    {
        if ( work.ptCount < 4 )
        {
            if ( work.ptCount == 3 )
            {
                triCallback( work.w, work.wOrig, work.props,
                             work.indices[0], work.indices[1], work.indices[2],
                             work.visGroupIndex );
            }

            if ( work.ownsWinding )
            {
                Assert( work.w != surf->w &&
                        ( work.wOrig == NULL || work.wOrig != surf->transient.wOrig ) );
                FreeWinding( work.w );
                if ( work.wOrig )
                    FreeWinding( work.wOrig );
            }

            return qtrue;
        }

        if ( !TesselateFindBestEar( &work ) )
            return qfalse;
    }
}


/* TesselateFindBestEar  0x0045b940 */
static bool TesselateFindBestEar( tessWork_t *work )
{
    triSide_t side0;
    triSide_t side1;
    triSide_t side2;
    winding_t *wCopy;
    double  bestCosine;
    double  cosine;
    float   tolerance;
    unsigned int bestEar;
    unsigned int ctrlIndex;
    unsigned int im2;
    unsigned int im1;
    unsigned int i;
    bool    allowCoincident;

    bestEar    = ( unsigned int )-1;
    bestCosine = FLT_MAX;

    for ( ;; )
    {
        for ( ctrlIndex = 0; ctrlIndex < NUM_TRI_CLIP_EAR_PASSES; ctrlIndex++ )
        {
            SanityCheck( triCtrl[ctrlIndex].command == TRI_CLIP_EAR_FORBIDDING_COINCIDENT ||
                         triCtrl[ctrlIndex].command == TRI_CLIP_EAR_ALLOWING_COINCIDENT );

            allowCoincident = ( triCtrl[ctrlIndex].command == TRI_CLIP_EAR_ALLOWING_COINCIDENT );
            tolerance       = triCtrl[ctrlIndex].tolerance;

            im2 = work->ptCount - 2;
            im1 = im2 + 1;

            for ( i = 0; i < work->ptCount; i++ )
            {
                const vec3_t *v0 = &work->w->pts[ work->indices[im2] ];
                const vec3_t *v1 = &work->w->pts[ work->indices[im1] ];
                const vec3_t *v2 = &work->w->pts[ work->indices[i] ];

                if ( TrisSetupTrianglePlanes( *v0, *v1, *v2, work->axisX, work->axisY,
                                              tolerance, &side0, &side1, &side2 ) )
                {
                    cosine = TrisLargestAngleCosine( side0.length, side1.length, side2.length );

                    if ( cosine < bestCosine &&
                         TesselateCheckEarValidity( work->w,
                                                    work->indices[im2],
                                                    work->indices[im1],
                                                    work->indices[i],
                                                    work->axisX, work->axisY,
                                                    allowCoincident,
                                                    &side0, &side1, &side2 ) )
                    {
                        bestCosine = cosine;
                        bestEar    = im1;
                    }
                }

                im2 = im1;
                im1 = i;
            }

            if ( bestEar != ( unsigned int )-1 )
            {
                TesselateClipEar( work, bestEar );
                return true;
            }
        }

        wCopy = CopyWinding( work->w );

        if ( !TesselateFixIntersections( work, wCopy, 0.1f ) &&
             !TesselateFixIntersections( work, wCopy, 0.01f ) &&
             !TesselateFixIntersections( work, wCopy, 0.001f ) )
        {
            FreeWinding( wCopy );
            return false;
        }
    }
}


/* TesselateFixIntersections  0x0045bc80 */
static bool TesselateFixIntersections( tessWork_t *work, winding_t *w, float tolerance )
{
    vec3_t hit;
    winding_t *wOrigCopy;
    float  toleranceSq;
    float  distA;
    float  distB;
    float  tA;
    float  tB;
    unsigned int i;
    unsigned int prev;
    unsigned int j;
    unsigned int other;
    unsigned int otherNext;
    unsigned int cand[2];
    unsigned int which;
    bool   movedAny;

    toleranceSq = tolerance * tolerance;
    movedAny    = false;

    prev = w->ptCount - 1;

    for ( i = 0; i < w->ptCount; i++ )
    {
        for ( j = 2; j < w->ptCount - 2; j++ )
        {
            other     = ( prev + j ) % w->ptCount;
            otherNext = ( other + 1 ) % w->ptCount;

            if ( TesselateEdgesIntersect( w->pts[prev], w->pts[i],
                                          w->pts[other], w->pts[otherNext],
                                          work->axisX, work->axisY,
                                          hit, &tA, &tB ) != 1 )
            {
                continue;
            }

            cand[0] = ( tA >= 0.5f ) ? i : prev;
            distA   = Vec3DistanceSq( w->pts[ cand[0] ], hit );
            if ( distA < toleranceSq )
                continue;

            cand[1] = ( tB >= 0.5f ) ? otherNext : other;
            distB   = Vec3DistanceSq( w->pts[ cand[1] ], hit );
            if ( distB < toleranceSq )
                continue;

            movedAny = true;
            which    = ( distB < distA );
            Vec3Copy( hit, w->pts[ cand[which] ] );
        }

        prev = i;
    }

    if ( !movedAny )
        return false;

    if ( work->wOrig == NULL )
    {
        TesselateSplitWinding( &w, NULL, work->props->plane );

        if ( work->ownsWinding )
            FreeWinding( work->w );

        work->w = w;
        work->ownsWinding = qtrue;
    }
    else
    {
        wOrigCopy = CopyWinding( work->wOrig );
        TesselateSplitWinding( &w, &wOrigCopy, work->props->plane );

        if ( work->ownsWinding )
        {
            FreeWinding( work->w );
            FreeWinding( work->wOrig );
        }

        work->w = w;
        work->wOrig = wOrigCopy;
        work->ownsWinding = qtrue;
    }

    return true;
}


/* TesselateEdgesIntersect  0x0045bf40 */
static int TesselateEdgesIntersect( const vec3_t a0, const vec3_t a1,
                                    const vec3_t b0, const vec3_t b1,
                                    int axisX, int axisY,
                                    vec3_t hit, float *tA, float *tB )
{
    vec3_t dirA;
    vec3_t dirB;
    vec3_t hitA;
    vec3_t hitB;
    float  db0;
    float  db1;
    float  da0;
    float  da1;

    Vec3Sub( a1, a0, dirA );

    db0 = ( b0[axisX] - a0[axisX] ) * dirA[axisY] - ( b0[axisY] - a0[axisY] ) * dirA[axisX];
    db1 = ( b1[axisX] - a0[axisX] ) * dirA[axisY] - ( b1[axisY] - a0[axisY] ) * dirA[axisX];

    if ( !( db0 > -0.001 || db1 > -0.001 ) )
        return 0;
    if ( !( db0 < 0.001 || db1 < 0.001 ) )
        return 0;

    Vec3Sub( b1, b0, dirB );

    da0 = ( a0[axisX] - b0[axisX] ) * dirB[axisY] - ( a0[axisY] - b0[axisY] ) * dirB[axisX];
    da1 = ( a1[axisX] - b0[axisX] ) * dirB[axisY] - ( a1[axisY] - b0[axisY] ) * dirB[axisX];

    if ( !( da0 > -0.001 || da1 > -0.001 ) )
        return 0;
    if ( !( da0 < 0.001 || da1 < 0.001 ) )
        return 0;

    if ( !( db0 * db0 >= 0.00390625 || db1 * db1 >= 0.00390625 ||
            da0 * da0 >= 0.00390625 || da1 * da1 >= 0.00390625 ) )
    {
        return 2;
    }

    *tA = da0 / ( da0 - da1 );
    *tB = db0 / ( db0 - db1 );

    Vec3Lerp( a0, a1, *tA, hitA );
    Vec3Lerp( b0, b1, *tB, hitB );
    Vec3Mid( hitA, hitB, hit );

    return 1;
}


/* TrisLargestAngleCosine  0x0045c1b0 */
static double TrisLargestAngleCosine( double lenA, double lenB, double lenC )
{
    if ( lenA < lenB )
    {
        if ( lenA < lenC )
            return ( lenB * lenB + lenC * lenC - lenA * lenA ) * 0.5 / ( lenB * lenC );
    }
    else if ( lenB < lenC )
    {
        return ( lenA * lenA + lenC * lenC - lenB * lenB ) * 0.5 / ( lenA * lenC );
    }

    return ( lenA * lenA + lenB * lenB - lenC * lenC ) * 0.5 / ( lenA * lenB );
}


/* TrisSetupTrianglePlanes  0x0045c250 */
static bool TrisSetupTrianglePlanes( const vec3_t v0, const vec3_t v1, const vec3_t v2,
                                     int axisX, int axisY, float tolerance,
                                     triSide_t *side0, triSide_t *side1, triSide_t *side2 )
{
    SetupTriSide( v0, v2, axisX, axisY, side0 );
    side0->height = SignedDistanceFromSide( side0, v1, axisX, axisY );
    if ( side0->height < tolerance )
        return false;

    SetupTriSide( v2, v1, axisX, axisY, side1 );
    side1->height = SignedDistanceFromSide( side1, v0, axisX, axisY );
    if ( side1->height < tolerance )
        return false;

    SetupTriSide( v1, v0, axisX, axisY, side2 );
    side2->height = SignedDistanceFromSide( side2, v2, axisX, axisY );
    if ( side2->height < tolerance )
        return false;

    side0->dist = side0->dist - tolerance;
    side1->dist = side1->dist - tolerance;
    side2->dist = side2->dist - tolerance;

    side0->height = tolerance * 2.0 + side0->height;
    side1->height = tolerance * 2.0 + side1->height;
    side2->height = tolerance * 2.0 + side2->height;

    return true;
}


/* SetupTriSide  0x0045c3c0 */
static void SetupTriSide( const vec3_t v0, const vec3_t v1, int axisX, int axisY,
                          triSide_t *side )
{
    double invLength;

    side->normal[0] = v0[axisY] - v1[axisY];
    side->normal[1] = v1[axisX] - v0[axisX];

    side->length = sqrt( side->normal[0] * side->normal[0] +
                         side->normal[1] * side->normal[1] );

    if ( 0.0 < side->length )
    {
        invLength = 1.0 / side->length;
        side->normal[0] = side->normal[0] * invLength;
        side->normal[1] = side->normal[1] * invLength;
    }

    side->dist      = v0[axisX] * side->normal[0] + v0[axisY] * side->normal[1];
    side->alongDist = v0[axisX] * side->normal[1] - v0[axisY] * side->normal[0];

    Assert( fabs( SignedDistanceAlongSide( side, v1, axisX, axisY ) - side->length ) < EQUAL_EPSILON );
}


/* TesselateCheckEarValidity  0x0045c500 */
static bool TesselateCheckEarValidity( const winding_t *w,
                                       unsigned int earPrev, unsigned int earTip,
                                       unsigned int earNext,
                                       int axisX, int axisY, bool allowCoincident,
                                       const triSide_t *side0, const triSide_t *side1,
                                       const triSide_t *side2 )
{
    unsigned int firstScan;
    unsigned int prev;
    unsigned int cur;
    unsigned int next;

    firstScan = ( earNext + 1 ) % w->ptCount;

    prev = earNext;
    cur  = firstScan;
    next = ( earNext + 2 ) % w->ptCount;

    for ( ;; )
    {
        if ( cur == earPrev )
            return true;

        if ( !PointOutsideSide( side0, w->pts[cur], axisX, axisY ) &&
             !PointOutsideSide( side1, w->pts[cur], axisX, axisY ) &&
             !PointOutsideSide( side2, w->pts[cur], axisX, axisY ) )
        {
            if ( w->pts[earPrev][axisX] != w->pts[cur][axisX] ||
                 w->pts[earPrev][axisY] != w->pts[cur][axisY] )
            {
                if ( w->pts[earTip][axisX] != w->pts[cur][axisX] ||
                     w->pts[earTip][axisY] != w->pts[cur][axisY] )
                {
                    if ( w->pts[cur][axisX] != w->pts[earNext][axisX] )
                        return false;
                    if ( w->pts[cur][axisY] != w->pts[earNext][axisY] )
                        return false;

                    if ( !TrisCheckBothEdges( w->pts[prev], w->pts[next], axisX, axisY,
                                              side1, side0, side2 ) )
                    {
                        return false;
                    }

                    if ( !allowCoincident &&
                         TrisVerticesCoincident2D( w, axisX, axisY, prev, next,
                                                   earTip, ( earNext + 1 ) % w->ptCount ) )
                    {
                        return false;
                    }
                }
                else
                {
                    if ( !TrisCheckBothEdges( w->pts[prev], w->pts[next], axisX, axisY,
                                              side2, side1, side0 ) )
                    {
                        return false;
                    }

                    if ( !allowCoincident &&
                         TrisVerticesCoincident2D( w, axisX, axisY, prev, next,
                                                   earPrev, earNext ) )
                    {
                        return false;
                    }
                }
            }
            else
            {
                if ( !TrisCheckBothEdges( w->pts[prev], w->pts[next], axisX, axisY,
                                          side0, side2, side1 ) )
                {
                    return false;
                }

                if ( !allowCoincident &&
                     TrisVerticesCoincident2D( w, axisX, axisY, prev, next,
                                               ( earPrev - 1 + w->ptCount ) % w->ptCount,
                                               earTip ) )
                {
                    return false;
                }
            }
        }

        prev = cur;
        cur  = next;
        next = ( next + 1 ) % w->ptCount;
    }
}


/* PointOutsideSide  0x0045c8c0 */
static bool PointOutsideSide( const triSide_t *side, const vec3_t v, int axisX, int axisY )
{
    double dist;

    dist = SignedDistanceFromSide( side, v, axisX, axisY );

    if ( dist < 0.0 )
        return true;

    return dist > side->height;
}


/* TrisCheckBothEdges  0x0045c910 */
static bool TrisCheckBothEdges( const vec3_t v0, const vec3_t v1, int axisX, int axisY,
                                const triSide_t *sideA, const triSide_t *sideB,
                                const triSide_t *sideC )
{
    if ( !PointOutsideTriangle( v0, axisX, axisY, sideA, sideB, sideC ) )
        return false;

    return PointOutsideTriangle( v1, axisX, axisY, sideA, sideB, sideC );
}


/* PointOutsideTriangle  0x0045c970 */
static bool PointOutsideTriangle( const vec3_t v, int axisX, int axisY,
                                  const triSide_t *sideA, const triSide_t *sideB,
                                  const triSide_t *sideC )
{
    if ( PointBehindSide( sideA, v, axisX, axisY ) )
        return true;
    if ( PointBehindSide( sideB, v, axisX, axisY ) )
        return true;

    return !PointBehindSide( sideC, v, axisX, axisY );
}


/* PointBehindSide  0x0045c9e0 */
static bool PointBehindSide( const triSide_t *side, const vec3_t v, int axisX, int axisY )
{
    return SignedDistanceFromSide( side, v, axisX, axisY ) < 0.0;
}


/* TrisVerticesCoincident2D  0x0045ca20 */
static bool TrisVerticesCoincident2D( const winding_t *w, int axisX, int axisY,
                                      unsigned int a0, unsigned int a1,
                                      unsigned int b0, unsigned int b1 )
{
    if ( w->pts[b1][axisX] != w->pts[a0][axisX] )
        return false;
    if ( w->pts[b1][axisY] != w->pts[a0][axisY] )
        return false;
    if ( w->pts[b0][axisX] != w->pts[a1][axisX] )
        return false;
    if ( w->pts[b0][axisY] != w->pts[a1][axisY] )
        return false;

    return true;
}


/* TesselateClipEar  0x0045caf0 */
static void TesselateClipEar( tessWork_t *work, unsigned int ear )
{
    unsigned int next;
    unsigned int prev;

    next = ( ear + 1 ) % work->ptCount;
    prev = ( ear - 1 + work->ptCount ) % work->ptCount;

    work->TriCallback( work->w, work->wOrig, work->props,
                       work->indices[prev], work->indices[ear], work->indices[next],
                       work->visGroupIndex );

    TesselateRemoveVertex( work, ear );

    if ( ear != work->ptCount )
    {
        next = next - 1;
        if ( ear == 0 )
            prev = prev - 1;
    }

    if ( !Vec3Compare( work->w->pts[ work->indices[prev] ],
                       work->w->pts[ work->indices[ ( next + 1 ) % work->ptCount ] ] ) &&
         !Vec3Compare( work->w->pts[ work->indices[ ( prev - 1 + work->ptCount ) % work->ptCount ] ],
                       work->w->pts[ work->indices[next] ] ) )
    {
        return;
    }

    TesselateRemoveDegenerateIndices( work );
}


/* TesselateRemoveDegenerateIndices  0x0045cc50 */
static void TesselateRemoveDegenerateIndices( tessWork_t *work )
{
    unsigned int im2;
    unsigned int im1;
    unsigned int i;

    for ( ;; )
    {
        im2 = work->ptCount - 2;
        im1 = work->ptCount - 1;

        for ( i = 0; i < work->ptCount; i++ )
        {
            if ( Vec3Compare( work->w->pts[ work->indices[im2] ],
                              work->w->pts[ work->indices[i] ] ) )
            {
                break;
            }

            im2 = im1;
            im1 = i;
        }

        if ( i >= work->ptCount )
            return;

        work->ptCount -= 2;

        if ( work->ptCount < 3 )
        {
            g_tessDegenerateCount += 2;
            return;
        }

        g_tessDegenerateCount++;

        if ( im1 == work->ptCount || im2 == work->ptCount )
            continue;

        if ( im1 == 0 )
        {
            memmove( work->indices, work->indices + 2, work->ptCount * sizeof( int ) );
        }
        else
        {
            Assert( work->ptCount + 2 - i > 0 );
            Assert( im2 < i );
            memmove( &work->indices[im2], &work->indices[i],
                     ( work->ptCount - im2 ) * sizeof( int ) );
        }
    }
}


/* TesselateRemoveVertex  0x0045cdf0 */
static void TesselateRemoveVertex( tessWork_t *work, unsigned int index )
{
    work->ptCount--;

    if ( index != work->ptCount )
    {
        memmove( &work->indices[index], &work->indices[index + 1],
                 ( work->ptCount - index ) * sizeof( int ) );
    }
}
