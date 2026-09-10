/* Original: ..\common\primarylights_region.cpp */

#include <new>

#include "primarylights_region.h"

#include "assertive.h"
#include "cmdlib.h"
#include "com_convexhull.h"
#include "../cod4map/primarylights.h"

const vec3_t s_kdopNormals[KDOP_NORMAL_COUNT] =
{
    {  1.0f,        0.0f,        0.0f       },
    { -1.0f,        0.0f,        0.0f       },
    {  0.0f,        1.0f,        0.0f       },
    {  0.0f,       -1.0f,        0.0f       },
    {  0.0f,        0.0f,        1.0f       },
    {  0.0f,        0.0f,       -1.0f       },
    {  0.70710677f, 0.70710677f, 0.0f       },
    { -0.70710677f,-0.70710677f, 0.0f       },
    {  0.70710677f,-0.70710677f, 0.0f       },
    { -0.70710677f, 0.70710677f, 0.0f       },
    {  0.70710677f, 0.0f,        0.70710677f},
    { -0.70710677f, 0.0f,       -0.70710677f},
    {  0.70710677f, 0.0f,       -0.70710677f},
    { -0.70710677f, 0.0f,        0.70710677f},
    {  0.0f,        0.70710677f, 0.70710677f},
    {  0.0f,       -0.70710677f,-0.70710677f},
    {  0.0f,        0.70710677f,-0.70710677f},
    {  0.0f,       -0.70710677f, 0.70710677f},
    {  0.57735026f, 0.57735026f, 0.57735026f},
    { -0.57735026f,-0.57735026f,-0.57735026f},
    {  0.57735026f, 0.57735026f,-0.57735026f},
    { -0.57735026f,-0.57735026f, 0.57735026f},
    {  0.57735026f,-0.57735026f, 0.57735026f},
    { -0.57735026f, 0.57735026f,-0.57735026f},
    {  0.57735026f,-0.57735026f,-0.57735026f},
    { -0.57735026f, 0.57735026f, 0.57735026f}
};

/* s_hullVolumeGain  0x00529144 */
static float s_hullVolumeGain = 0.999f;                     /* 0x00529144 */


static LightRegionFace_t *AllocFace( void );
static void               FreeFace( LightRegionFace_t *face );
static void               AddFaceToList( LightRegionFace_t **list, winding_t *w,
                                         const vec4_t plane, int twoSided,
                                         const void *ds );
static void               ComputeFaceCone( LightRegionFace_t *face );
static qboolean           UpdateFaceCone( LightRegionFace_t *face, const vec3_t testDir,
                                          const vec3_t *pts, unsigned int ptCount,
                                          float *cosMinOut, float *cosMaxOut );
static LightRegionFace_t *CopyFaceList( const LightRegionFace_t *list );
static LightRegionFace_t **ClipFaceByFace( const LightRegionFace_t *clipper,
                                           LightRegionFace_t **at );
static void               ClipListByList( const LightRegionFace_t *clippers,
                                          LightRegionFace_t **list );
static void               SortFaceList( LightRegionFace_t **list );
static void               MergeSortFaceList( LightRegionFace_t **list );
static qboolean           FaceConeIsWider( const LightRegionFace_t *a,
                                           const LightRegionFace_t *b );
static LightRegionFace_t *ProjectRegionToPlane( LightRegionFace_t *list );
static void               ClipListToLightVolume( LightRegionFace_t **list,
                                                 const PrimaryLightInfo_t *light );
static void               ClipListToCone( LightRegionFace_t **list, const vec3_t coneDir,
                                          float cosHalfFov );
static LightRegionFace_t **ClipFaceToCone( const vec3_t coneDir, float sinHalfFov,
                                           float cosHalfFov, LightRegionFace_t **at );
static void               ClipListToPlane( LightRegionFace_t **list, const vec4_t plane );
static LightRegionFace_t **ClipFaceToPlane( const vec4_t plane, LightRegionFace_t **at );
static void               ClipListToFaceVolume( LightRegionFace_t **list,
                                                const LightRegionFace_t *face );
static void               BuildHullAxes( const LightRegionFace_t *list,
                                         const LightRegionFace_t *clipFace,
                                         const PrimaryLightInfo_t *light,
                                         float *hullOut );
static void               HullAxisExtent( const LightRegionFace_t *list,
                                          const LightRegionFace_t *clipFace,
                                          const vec3_t axis, float *minOut, float *maxOut );
static unsigned int       CollectCandidateAxes( const LightRegionFace_t *list,
                                                const LightRegionFace_t *clipFace,
                                                const PrimaryLightInfo_t *light,
                                                float *axesOut );
static unsigned int       AddCandidateAxis( const vec3_t dir, float *axes,
                                            unsigned int axisCount );
static unsigned int       AddClipFaceAxes( const LightRegionFace_t *clipFace, float *axes,
                                           unsigned int axisCount );
static unsigned int       AddSpotConeAxes( const PrimaryLightInfo_t *light, float *axes,
                                           unsigned int axisCount );
static unsigned int       AddKdopAxes( float *axes, unsigned int axisCount );
static int                PickBestAxis( float *hull, const float *candidates,
                                        unsigned int candidateCount );
static float              HullVolume( const float *axes, unsigned int axisCount );
static unsigned int       BuildHullWindings( const float *axes, unsigned int axisCount,
                                             winding_t **windingsOut );
static float              WindingListVolume( winding_t * const *windings,
                                             unsigned int windingCount );
static void               HullWindings( const BspLightRegionHull_t *hull,
                                        winding_t **windingsOut );
static BspLightRegionHull_t *EmitHull( const float *hull );
static unsigned int       BuildHullPoints( const float *hull, unsigned int axisCount,
                                           float *pointsOut );
static qboolean           SolvePlaneTriple( const float *hull, unsigned int a, unsigned int b,
                                            unsigned int c, vec3_t *cornersOut );
static qboolean           PointInsideAllSlabs( const float *hull, unsigned int axisCount,
                                               const vec3_t point );
static unsigned int       CollectFacePoints( unsigned int planeIndex, const float *points,
                                             unsigned int pointCount, vec3_t *out );
static winding_t         *ConvexHullOfPoints( const vec3_t normal, vec3_t *points,
                                              unsigned int pointCount );
static int                HullPlaneCount( const BspLightRegionHull_t *hull );
static void               HullPlane( const BspLightRegionHull_t *hull, unsigned int planeIndex,
                                     vec4_t out );

/* LightRegion_FreeBuilder  0x00436030 */
void LightRegion_FreeBuilder( void *list )
{
    LightRegionFace_t *face;
    LightRegionFace_t *next;

    face = ( LightRegionFace_t * )list;
    while ( face )
    {
        next = face->next;
        FreeFace( face );
        face = next;
    }
}

/* FreeFace  0x00436060 */
static void FreeFace( LightRegionFace_t *face )
{
    if ( face->w )
        FreeWinding( face->w );

    operator delete( face );
}

/* AllocFace  0x00436300 */
static LightRegionFace_t *AllocFace( void )
{
    LightRegionFace_t *face;

    face = ( LightRegionFace_t * )operator new( sizeof( LightRegionFace_t ), std::nothrow );
    if ( !face )
        Com_Error( "Out of memory" );

    return face;
}

/* LightRegion_AddFace  0x004360a0 */
void LightRegion_AddFace( void **list, const PrimaryLightInfo_t *light,
                          const winding_t *w, qboolean twoSided, const void *ds )
{
    winding_t   *local;
    winding_t   *reversed;
    vec4_t       plane;
    vec3_t       delta;
    vec3_t       normal;
    float        lenSq;
    float        len;
    unsigned int i;

    local = AllocWinding( w->ptCount );
    local->ptCount = w->ptCount;
    for ( i = 0; i < w->ptCount; i++ )
        Vec3Sub( w->pts[i], light->origin, local->pts[i] );

    if ( !PlaneFromWinding( local, plane ) || I_fabs( plane[3] ) < 0.001f )
    {
        FreeWinding( local );
        return;
    }

    if ( plane[3] > 0.0f )
    {
        if ( twoSided )
        {
            FreeWinding( local );
            return;
        }
        Vec4Negate( plane, plane );
        reversed = ReverseWinding( local );
        FreeWinding( local );
        local = reversed;
    }

    for ( i = 0; i < w->ptCount; i++ )
    {
        Vec3Sub( light->origin, w->pts[i], delta );
        lenSq = Vec3LengthSq( delta );
        if ( lenSq <= light->radius * light->radius )
            continue;

        len = I_sqrt( lenSq );
        Vec3Scale( delta, 1.0f / len, normal );

        ClipWindingByPlane( &local, normal, -light->radius, 0.1f );
        if ( !local )
            return;
    }

    AddFaceToList( ( LightRegionFace_t ** )list, local, plane, twoSided, ds );
}

/* AddFaceToList  0x004362a0 */
static void AddFaceToList( LightRegionFace_t **list, winding_t *w, const vec4_t plane,
                           int twoSided, const void *ds )
{
    LightRegionFace_t *face;

    face = AllocFace();

    face->ds       = ds;
    face->twoSided = twoSided;
    face->w        = w;
    Vec4Copy( plane, face->plane );

    face->next = *list;
    *list      = face;

    ComputeFaceCone( face );
}

/* ComputeFaceCone  0x00436340 */
static void ComputeFaceCone( LightRegionFace_t *face )
{
    vec3_t       testDir;
    vec3_t       dirs[MAX_POINTS_ON_WINDING];
    vec3_t       sum;
    float        cosMin;
    float        cosMax;
    float        prevCos;
    float        step;
    float        threshold;
    float        d;
    unsigned int i;
    unsigned int iterations;
    const winding_t *w;

    Assert( face->w );
    Assertx( face->w->ptCount <= MAX_POINTS_ON_WINDING, "%i, %i",
             face->w->ptCount, MAX_POINTS_ON_WINDING );

    w = face->w;

    Vec3Clear( sum );
    for ( i = 0; i < w->ptCount; i++ )
    {
        Vec3NormalizeTo( w->pts[i], dirs[i] );
        Vec3Add( dirs[i], sum, sum );   /* 0x00436404 */
    }
    Vec3NormalizeTo( sum, testDir );

    face->cosHalfFov = -2.0f;
    UpdateFaceCone( face, testDir, dirs, w->ptCount, &cosMin, &cosMax );

    SanityCheckx( face->cosHalfFov > -1.0f, "%g, %g", face->cosHalfFov, -1.0f );

    if ( face->cosHalfFov >= FACE_CONE_GOOD_ENOUGH_COS )
        return;

    Vec3Negate( face->plane, testDir );
    UpdateFaceCone( face, testDir, dirs, w->ptCount, &cosMin, &cosMax );

    Assertx( cosMin > 0.0f, "(cosHalfFovMin) = %g", cosMin );

    if ( face->cosHalfFov >= FACE_CONE_GOOD_ENOUGH_COS )
        return;

    iterations = 0;
    step       = FACE_CONE_STEP;
    do
    {
        iterations++;
        Assert( iterations < FACE_CONE_MAX_ITERATIONS );

        threshold = step * 0.5 * ( cosMax - cosMin ) + cosMin;

        Vec3Negate( face->dir, sum );
        for ( i = 0; i < w->ptCount; i++ )
        {
            d = -Vec3Dot( dirs[i], face->dir );
            if ( d <= threshold )
                Vec3Mad( sum, step, dirs[i], sum );
        }
        Vec3NormalizeTo( sum, testDir );

        prevCos = face->cosHalfFov;

        Assert( !Vec3Compare( face->dir, testDir ) );
    }
    while ( UpdateFaceCone( face, testDir, dirs, w->ptCount, &cosMin, &cosMax ) &&
            face->cosHalfFov >= prevCos + FACE_CONE_EPSILON );

    Assertx( face->cosHalfFov > 0.0f, "%g, %g", face->cosHalfFov, 0.0f );
}

/* UpdateFaceCone  0x00436730 */
static qboolean UpdateFaceCone( LightRegionFace_t *face, const vec3_t testDir,
                                const vec3_t *dirs, unsigned int ptCount,
                                float *cosMinOut, float *cosMaxOut )
{
    unsigned int i;
    float        d;
    qboolean     better;

    *cosMinOut = 1.0f;
    *cosMaxOut = -1.0f;

    for ( i = 0; i < ptCount; i++ )
    {
        d = Vec3Dot( dirs[i], testDir );
        if ( d < *cosMinOut )
            *cosMinOut = d;
        if ( *cosMaxOut < d )
            *cosMaxOut = d;
    }

    better = face->cosHalfFov < *cosMinOut;
    if ( better )
    {
        face->cosHalfFov = *cosMinOut;
        Vec3Negate( testDir, face->dir );
    }

    return better;
}

/* LightRegion_BuildFaces  0x004367f0 */
void LightRegion_BuildFaces( void **list, const PrimaryLightInfo_t *light )
{
    vec3_t       mins;
    vec3_t       maxs;
    vec4_t       plane;
    winding_t   *w;
    unsigned int i;
    unsigned int j;

    Vec3Set( mins, -light->radius, -light->radius, -light->radius );
    Vec3Set( maxs,  light->radius,  light->radius,  light->radius );

    for ( i = 0; i < KDOP_NORMAL_COUNT; i++ )
    {
        Vec3Copy( s_kdopNormals[i], plane );
        plane[3] = -light->radius;

        w = BaseWindingForPlaneInBounds( plane, mins, maxs );

        for ( j = 6; j < KDOP_NORMAL_COUNT; j++ )
        {
            if ( j == i )
                continue;
            Assert( w );
            ClipWindingByPlane( &w, s_kdopNormals[j], -light->radius, 0.1f );
        }

        Assert( w );

        AddFaceToList( ( LightRegionFace_t ** )list, w, plane, 1, NULL );
    }
}

/* CopyFaceList  0x004369f0 */
static LightRegionFace_t *CopyFaceList( const LightRegionFace_t *list )
{
    LightRegionFace_t  *head;
    LightRegionFace_t **at;
    const LightRegionFace_t *src;

    at = &head;

    for ( src = list; src; src = src->next )
    {
        *at        = AllocFace();
        **at       = *src;
        ( *at )->w = CopyWinding( src->w );
        at         = &( *at )->next;
    }

    *at = NULL;
    return head;
}

/* ClipListByList  0x00437210 */
static void ClipListByList( const LightRegionFace_t *clippers, LightRegionFace_t **list )
{
    const LightRegionFace_t *clipper;
    LightRegionFace_t      **at;

    for ( clipper = clippers; clipper; clipper = clipper->next )
        for ( at = list; *at; at = ClipFaceByFace( clipper, at ) )
            ;
}

/* ClipOccluders  0x00436970 */
static void ClipOccluders( LightRegionFace_t **list )
{
    LightRegionFace_t  *copy;
    LightRegionFace_t  *clipper;
    LightRegionFace_t **at;

    copy = CopyFaceList( *list );

    for ( clipper = copy; clipper; clipper = clipper->next )
    {
        if ( !clipper->twoSided )
            continue;
        for ( at = list; *at; at = ClipFaceByFace( clipper, at ) )
            ;
    }

    LightRegion_FreeBuilder( copy );
}

/* ClipFaceByFace  0x00436a70 */
static LightRegionFace_t **ClipFaceByFace( const LightRegionFace_t *clipper,
                                           LightRegionFace_t **at )
{
    LightRegionFace_t *face;
    LightRegionFace_t *piece;
    winding_t         *pieces[MAX_POINTS_ON_WINDING + 2];
    winding_t         *front;
    winding_t         *back;
    winding_t         *remaining;
    const winding_t   *casterWinding;
    vec3_t             sidePlane;
    float              cosBetween;
    int                pieceCount;
    int                side;
    unsigned int       i;
    unsigned int       prev;

    face = *at;

    cosBetween = Vec3Dot( clipper->dir, face->dir );
    if ( cosBetween < CosOfAngleSum( clipper->cosHalfFov, face->cosHalfFov ) - 0.001f )
        return &face->next;

    side = ClipWindingEpsilon_Internal( face->w, clipper->plane, clipper->plane[3],
                                        0.01f, &front, &back );
    if ( side == 0 || side == 2 )
        return &face->next;

    if ( side == 1 )
    {
        remaining  = face->w;
        pieceCount = 0;
    }
    else
    {
        SanityCheck( side == SIDE_CROSS );
        remaining  = back;
        pieces[0]  = front;
        pieceCount = 1;
    }

    casterWinding = clipper->w;
    Assertx( casterWinding->ptCount <= MAX_POINTS_ON_WINDING, "%i, %i",
             casterWinding->ptCount, MAX_POINTS_ON_WINDING );

    prev = casterWinding->ptCount - 1;
    for ( i = 0; i < casterWinding->ptCount; i++ )
    {
        Vec3Cross( casterWinding->pts[i], casterWinding->pts[prev], sidePlane );
        if ( Vec3Normalize( sidePlane ) != 0.0f )
        {
            side = ClipWindingEpsilon_Internal( remaining, sidePlane, 0.0f, 0.01f,
                                                &pieces[pieceCount], &back );
            if ( side != 1 )
            {
                if ( remaining != face->w )
                    FreeWinding( remaining );

                if ( side != 3 )
                {
                    while ( pieceCount )
                    {
                        pieceCount--;
                        FreeWinding( pieces[pieceCount] );
                    }
                    return &face->next;
                }

                remaining = back;
                if ( WindingMinWidth( pieces[pieceCount], face->plane ) < 0.02001f )
                    FreeWinding( pieces[pieceCount] );
                else
                    pieceCount++;
            }
        }
        prev = i;
    }

    if ( remaining != face->w )
        FreeWinding( remaining );

    *at = face->next;
    while ( pieceCount )
    {
        pieceCount--;
        piece       = AllocFace();
        piece->next = *at;
        *at         = piece;
        at          = &piece->next;
        piece->ds   = face->ds;
        piece->w    = pieces[pieceCount];
        Vec4Copy( face->plane, piece->plane );
        ComputeFaceCone( piece );
    }

    FreeFace( face );
    return at;
}

/* LightRegion_BuildHulls  0x00436de0 */
unsigned int LightRegion_BuildHulls( void **list, const PrimaryLightInfo_t *light,
                                     unsigned int hullLimit, BspLightRegionHull_t **hullsOut )
{
    LightRegionFace_t  *work;
    LightRegionFace_t  *face;
    LightRegionFace_t  *projected;
    LightRegionFace_t **at;
    float               hull[1 + HULL_AXIS_LIMIT * 5];
    unsigned int        hullCount;

    Assert( hullLimit >= 1 );

    SortFaceList( ( LightRegionFace_t ** )list );

    work = CopyFaceList( ( LightRegionFace_t * )*list );
    ClipListByList( ( LightRegionFace_t * )*list, &work );
    ClipListToLightVolume( &work, light );
    BuildHullAxes( work, NULL, light, hull );
    LightRegion_FreeBuilder( work );

    hullCount = 0;
    if ( hull[0] != 0.0f )
    {
        hullsOut[0] = EmitHull( hull );
        hullCount   = 1;
    }

    at = ( LightRegionFace_t ** )list;
    while ( *at )
    {
        if ( ( *at )->twoSided )
        {
            at = &( *at )->next;
            continue;
        }

        face  = *at;
        *at   = face->next;
        face->next = NULL;

        ClipListByList( ( LightRegionFace_t * )*list, &face );
        ClipListToLightVolume( &face, light );

        projected = ProjectRegionToPlane( face );
        if ( projected )
        {
            work = CopyFaceList( ( LightRegionFace_t * )*list );
            ClipListByList( ( LightRegionFace_t * )*list, &work );
            ClipListToFaceVolume( &work, projected );
            BuildHullAxes( work, projected, light, hull );
            LightRegion_FreeBuilder( work );
            FreeFace( projected );

            if ( hull[0] != 0.0f )
            {
                if ( hullCount == hullLimit )
                    Com_Error( "More than %i convex regions affected by primary light "
                               "at %g %g %g\n", hullLimit,
                               light->origin[0], light->origin[1], light->origin[2] );

                hullsOut[hullCount] = EmitHull( hull );
                hullCount++;
            }
        }
    }

    return hullCount;
}

/* SortFaceList / MergeSortFaceList / FaceConeIsWider  0x00437060 / 0x00437090 / 0x004371e0 */
static void SortFaceList( LightRegionFace_t **list )
{
    if ( *list && ( *list )->next )
        MergeSortFaceList( list );
}

static void MergeSortFaceList( LightRegionFace_t **list )
{
    LightRegionFace_t  *half[2];
    LightRegionFace_t  *run[2];
    LightRegionFace_t **at;
    LightRegionFace_t  *node;
    unsigned int        side;

    half[0] = *list;
    half[1] = NULL;
    run[0]  = NULL;
    run[1]  = NULL;

    for ( side = 0; half[side]; side = 1 - side )
    {
        half[1 - side] = half[side]->next;
        half[side]->next = run[side];
        run[side]        = half[side];
    }

    if ( run[0]->next )
    {
        MergeSortFaceList( &run[0] );
        if ( run[1]->next )
            MergeSortFaceList( &run[1] );
    }

    half[0] = run[0];
    half[1] = run[1];

    at   = list;
    side = FaceConeIsWider( run[0], run[1] ) ? 0 : 1;

    for ( ;; )
    {
        *at         = half[side];
        node        = *at;
        at          = &node->next;
        half[side]  = node->next;
        if ( !half[side] )
            break;
        if ( FaceConeIsWider( half[1 - side], half[side] ) )
            side = 1 - side;
    }

    *at = half[1 - side];
}

static qboolean FaceConeIsWider( const LightRegionFace_t *a, const LightRegionFace_t *b )
{
    return b->cosHalfFov > a->cosHalfFov;
}

/* ProjectRegionToPlane  0x00437260 */
static LightRegionFace_t *ProjectRegionToPlane( LightRegionFace_t *list )
{
    LightRegionFace_t *face;
    winding_t         *w;
    vec4_t             plane;
    vec2_t             scratch[2][CONVEX_HULL_POINT_LIMIT];
    unsigned int       ptCount;
    int                srcBuf;
    int                dstBuf;
    int                axisU;
    int                axisV;
    int                axisW;
    unsigned int       i;

    if ( !list || !list->next )
        return list;

    GetProjectionAxes( list->plane, &axisU, &axisV );

    srcBuf  = 0;
    dstBuf  = 1;
    ptCount = 0;

    for ( face = list; face; face = face->next )
    {
        for ( i = 0; i < face->w->ptCount; i++ )
        {
            Vec2Set( scratch[srcBuf][ptCount],
                     face->w->pts[i][axisU], face->w->pts[i][axisV] );
            ptCount++;
            if ( ptCount != CONVEX_HULL_POINT_LIMIT )
                continue;

            ptCount = ConvexHull2DPoints( scratch[srcBuf], CONVEX_HULL_POINT_LIMIT,
                                          scratch[dstBuf] );
            if ( ptCount == CONVEX_HULL_POINT_LIMIT )
                Com_Error( "Too many vertices on convex hull of partially occluded "
                           "primary light portal" );
            dstBuf = 1 - dstBuf;
            srcBuf = 1 - srcBuf;
        }
    }

    ptCount = ConvexHull2DPoints( scratch[srcBuf], ptCount, scratch[dstBuf] );

    w          = AllocWinding( ptCount );
    w->ptCount = ptCount;
    axisW      = ( 3 - axisU ) - axisV;

    for ( i = 0; i < ptCount; i++ )
    {
        w->pts[i][axisU] = scratch[dstBuf][i][0];
        w->pts[i][axisV] = scratch[dstBuf][i][1];
        w->pts[i][axisW] = ( list->plane[3] -
                             ( scratch[dstBuf][i][1] * list->plane[axisV] +
                               scratch[dstBuf][i][0] * list->plane[axisU] ) ) /
                           list->plane[axisW];
    }

    if ( !PlaneFromWinding( w, plane ) )
    {
        LightRegion_FreeBuilder( list );
        FreeWinding( w );
        return NULL;
    }

    LightRegion_FreeBuilder( list->next );
    list->next = NULL;
    FreeWinding( list->w );

    if ( Vec3Dot( plane, list->plane ) < 0.0f )
    {
        list->w = ReverseWinding( w );
        FreeWinding( w );
    }
    else
    {
        list->w = w;
    }

    return list;
}

/* ClipListToLightVolume  0x004375d0 */
static void ClipListToLightVolume( LightRegionFace_t **list, const PrimaryLightInfo_t *light )
{
    vec4_t plane;

    if ( light->type == GFX_LIGHT_TYPE_OMNI )
        return;
    if ( light->cosHalfFov <= -1.0f )
        return;

    if ( light->cosHalfFov < 0.0f )
    {
        Vec3Copy( light->dir, plane );
        plane[3] = light->cosHalfFov * light->radius;
        ClipListToPlane( list, plane );
    }
    else
    {
        ClipListToCone( list, light->dir, light->cosHalfFov );
    }
}

/* ClipListToCone / ClipFaceToCone  0x00437660 / 0x004376c0 */
static void ClipListToCone( LightRegionFace_t **list, const vec3_t coneDir, float cosHalfFov )
{
    float               sinHalfFov;
    LightRegionFace_t **at;

    sinHalfFov = I_sqrt( 1.0f - cosHalfFov * cosHalfFov );

    for ( at = list; *at; at = ClipFaceToCone( coneDir, sinHalfFov, cosHalfFov, at ) )
        ;
}

static LightRegionFace_t **ClipFaceToCone( const vec3_t coneDir, float sinHalfFov,
                                           float cosHalfFov, LightRegionFace_t **at )
{
    LightRegionFace_t *face;
    winding_t         *w;
    vec3_t             perp;
    vec3_t             normal;
    float              dot;
    float              perpLen;
    unsigned int       i;

    face = *at;

    dot = Vec3Dot( face->dir, coneDir );

    if ( dot < CosOfAngleSum( face->cosHalfFov, cosHalfFov ) )
    {
        *at = face->next;
        FreeFace( face );
        return at;
    }

    if ( cosHalfFov < CosOfAngleSum( face->cosHalfFov, dot ) )
        return &face->next;

    w = CopyWinding( face->w );

    for ( i = 0; i < face->w->ptCount; i++ )
    {
        dot = Vec3Dot( face->w->pts[i], coneDir );
        Vec3Mad( face->w->pts[i], -dot, coneDir, perp );
        perpLen = Vec3Length( perp );

        if ( dot * sinHalfFov + perpLen * cosHalfFov <= 0.0f )
            continue;

        Vec3Combine( -sinHalfFov, coneDir, -cosHalfFov / perpLen, perp, normal );
        ClipWindingByPlane( &w, normal, 0.0f, 0.1f );
        if ( !w )
        {
            *at = face->next;
            FreeFace( face );
            return at;
        }
    }

    FreeWinding( face->w );
    face->w = w;
    return &face->next;
}

/* ClipListToPlane / ClipFaceToPlane  0x004378b0 / 0x004378e0 */
static void ClipListToPlane( LightRegionFace_t **list, const vec4_t plane )
{
    LightRegionFace_t **at;

    for ( at = list; *at; at = ClipFaceToPlane( plane, at ) )
        ;
}

static LightRegionFace_t **ClipFaceToPlane( const vec4_t plane, LightRegionFace_t **at )
{
    LightRegionFace_t *face;

    face = *at;

    ClipWindingByPlane( &face->w, plane, plane[3], 0.01f );

    if ( !face->w )
    {
        *at = face->next;
        FreeFace( face );
        return at;
    }

    return &face->next;
}

/* ClipListToFaceVolume  0x00437950 */
static void ClipListToFaceVolume( LightRegionFace_t **list, const LightRegionFace_t *face )
{
    vec4_t       plane;
    unsigned int i;
    unsigned int prev;

    Vec4Negate( face->plane, plane );
    ClipListToPlane( list, plane );

    prev = face->w->ptCount - 1;
    for ( i = 0; i < face->w->ptCount; i++ )
    {
        Vec3Cross( face->w->pts[prev], face->w->pts[i], plane );
        Vec3Normalize( plane );
        plane[3] = Vec3Dot( face->w->pts[i], plane );
        ClipListToPlane( list, plane );
        prev = i;
    }
}

/* BuildHullAxes  0x00437a20 */
static void BuildHullAxes( const LightRegionFace_t *list, const LightRegionFace_t *clipFace,
                           const PrimaryLightInfo_t *light, float *hull )
{
    float        candidates[HULL_CANDIDATE_DIR_LIMIT * 5];
    float       *extents = candidates + 3;
    unsigned int candidateCount;
    unsigned int i;
    int          axisLimit;
    int          best;
    float        minDist;
    float        maxDist;

    if ( !list )
    {
        hull[0] = 0.0f;
        return;
    }

    candidateCount = CollectCandidateAxes( list, clipFace, light, candidates );
    Assert( candidateCount >= HULL_KDOP_AXIS_COUNT );

    for ( i = 0; i < candidateCount; i++ )
    {
        HullAxisExtent( list, clipFace, &candidates[i * 5], &minDist, &maxDist );
        extents[i * 5 + 0] = ( maxDist + minDist ) * 0.5f;
        extents[i * 5 + 1] = ( maxDist - minDist ) * 0.5f;
    }

    memcpy( hull + 1, candidates, HULL_KDOP_AXIS_COUNT * 5 * sizeof( float ) );
    hull[0] = ( float )HULL_KDOP_AXIS_COUNT;

    axisLimit      = I_min( HULL_AXIS_LIMIT, ( int )candidateCount );
    candidateCount = candidateCount - HULL_KDOP_AXIS_COUNT;
    memcpy( candidates, &candidates[candidateCount * 5],
            HULL_KDOP_AXIS_COUNT * 5 * sizeof( float ) );

    while ( hull[0] != ( float )axisLimit )
    {
        best = PickBestAxis( hull, candidates, candidateCount );
        if ( best == ( int )candidateCount )
            break;

        hull[( int )hull[0] * 5 + 1] = candidates[best * 5 + 0];
        hull[( int )hull[0] * 5 + 2] = candidates[best * 5 + 1];
        hull[( int )hull[0] * 5 + 3] = extents[best * 5 - 1];
        hull[( int )hull[0] * 5 + 4] = extents[best * 5 + 0];
        hull[( int )hull[0] * 5 + 5] = extents[best * 5 + 1];
        hull[0] += 1.0f;

        candidateCount--;
        candidates[best * 5 + 0] = candidates[candidateCount * 5 + 0];
        candidates[best * 5 + 1] = candidates[candidateCount * 5 + 1];
        extents[best * 5 - 1]    = extents[candidateCount * 5 - 1];
        extents[best * 5 + 0]    = extents[candidateCount * 5 + 0];
        extents[best * 5 + 1]    = extents[candidateCount * 5 + 1];
    }
}

/* HullAxisExtent  0x00437c30 */
static void HullAxisExtent( const LightRegionFace_t *list, const LightRegionFace_t *clipFace,
                            const vec3_t axis, float *minOut, float *maxOut )
{
    const LightRegionFace_t *face;
    const winding_t         *w;
    float                    d;
    unsigned int             i;

    if ( !clipFace )
    {
        *minOut = 0.0f;
        *maxOut = 0.0f;
    }
    else
    {
        w       = clipFace->w;
        *minOut = Vec3Dot( w->pts[0], axis );
        *maxOut = *minOut;
        for ( i = 1; i < w->ptCount; i++ )
        {
            d = Vec3Dot( w->pts[i], axis );
            if ( d < *minOut )
                *minOut = d;
            else if ( *maxOut < d )
                *maxOut = d;
        }
    }

    for ( face = list; face; face = face->next )
    {
        w = face->w;
        for ( i = 0; i < w->ptCount; i++ )
        {
            d = Vec3Dot( w->pts[i], axis );
            if ( d < *minOut )
                *minOut = d;
            else if ( *maxOut < d )
                *maxOut = d;
        }
    }
}

/* CollectCandidateAxes  0x00437d80 */
static unsigned int CollectCandidateAxes( const LightRegionFace_t *list,
                                          const LightRegionFace_t *clipFace,
                                          const PrimaryLightInfo_t *light, float *axes )
{
    vec4_t                   plane;
    const LightRegionFace_t *face;
    unsigned int             axisCount;

    axisCount = AddKdopAxes( axes, 0 );

    for ( face = list; face; face = face->next )
    {
        PlaneFromWinding( face->w, plane );   /* 0x00437db9 */
        if ( plane[3] <= 0.0 )
            axisCount = AddCandidateAxis( plane, axes, axisCount );
    }

    if ( clipFace )
        return AddClipFaceAxes( clipFace, axes, axisCount );

    return AddSpotConeAxes( light, axes, axisCount );
}

/* AddClipFaceAxes  0x00437ed0 */
static unsigned int AddClipFaceAxes( const LightRegionFace_t *clipFace, float *axes,
                                     unsigned int axisCount )
{
    vec3_t       dir;
    unsigned int i;
    unsigned int prev;

    Vec3Negate( clipFace->plane, dir );
    axisCount = AddCandidateAxis( dir, axes, axisCount );

    prev = clipFace->w->ptCount - 1;
    for ( i = 0; i < clipFace->w->ptCount; i++ )
    {
        Vec3Cross( clipFace->w->pts[prev], clipFace->w->pts[i], dir );
        Vec3Normalize( dir );
        axisCount = AddCandidateAxis( dir, axes, axisCount );
        prev = i;
    }

    return axisCount;
}

/* AddSpotConeAxes  0x00437f90 */
static unsigned int AddSpotConeAxes( const PrimaryLightInfo_t *light, float *axes,
                                     unsigned int axisCount )
{
    vec3_t       apex;
    vec3_t       up;
    vec3_t       right;
    vec3_t       rim;
    vec3_t       prevRim;
    vec3_t       normal;
    float        scale;
    float        angle;
    float        sinAngle;
    float        cosAngle;
    unsigned int i;

    if ( light->type == GFX_LIGHT_TYPE_OMNI )
        return axisCount;

    axisCount = AddCandidateAxis( light->dir, axes, axisCount );

    if ( light->cosHalfFov < 0.1f )
        return axisCount;

    scale = ( float )cos( PI / 8.0f ) *
            ( light->cosHalfFov / ( float )sqrt( ( float )( 1.0f - light->cosHalfFov * light->cosHalfFov ) ) );

    Vec3Scale( light->dir, -scale, apex );
    MakeNormalVectors2( light->dir, up, right );

    angle = -( PI / 8.0f );
    SinCos( angle, &sinAngle, &cosAngle );
    Vec3MadCombine( apex, cosAngle, up, sinAngle, right, rim );

    for ( i = 0; i < 8; i++ )
    {
        Vec3Copy( rim, prevRim );

        angle = ( float )( ( i + 0.5 ) * ( PI / 4.0f ) );
        SinCos( angle, &sinAngle, &cosAngle );
        Vec3MadCombine( apex, cosAngle, up, sinAngle, right, rim );

        Vec3Cross( rim, prevRim, normal );
        Vec3Normalize( normal );
        axisCount = AddCandidateAxis( normal, axes, axisCount );
    }

    return axisCount;
}

/* AddCandidateAxis  0x00437e30 */
static unsigned int AddCandidateAxis( const vec3_t dir, float *axes, unsigned int axisCount )
{
    unsigned int i;

    for ( i = 0; i < axisCount; i++ )
    {
        if ( I_fabs( Vec3Dot( dir, &axes[i * 5] ) ) > HULL_AXIS_MERGE_COS )
            return axisCount;
    }

    AssertIn( axisCount, HULL_CANDIDATE_DIR_LIMIT );

    Vec3Copy( dir, &axes[axisCount * 5] );
    return axisCount + 1;
}

/* AddKdopAxes  0x00438170 */
static unsigned int AddKdopAxes( float *axes, unsigned int axisCount )
{
    unsigned int i;

    Assert( axisCount == 0 );

    for ( i = 0; i < KDOP_NORMAL_COUNT; i += 2 )
    {
        SanityCheck( Vec3Dot( s_kdopNormals[i], s_kdopNormals[i + 1] ) <= -0.99999f );
        Vec3Copy( s_kdopNormals[i], &axes[axisCount * 5] );
        axisCount++;
    }

    return axisCount;
}

/* PickBestAxis  0x00438240 */
static int PickBestAxis( float *hull, const float *candidates,
                         unsigned int candidateCount )
{
    const float *candidate;
    float       *slot;
    float        baseVolume;
    float        bestVolume;
    float        volume;
    unsigned int best;
    unsigned int i;

    baseVolume = HullVolume( hull + 1, ( unsigned int )( int )hull[0] );
    best       = candidateCount;
    bestVolume = baseVolume * s_hullVolumeGain;

    for ( i = 0; i < candidateCount; i++ )
    {
        candidate = &candidates[i * 5];
        slot      = hull + ( int )hull[0] * 5 + 1;

        slot[0] = candidate[0];
        slot[1] = candidate[1];
        slot[2] = candidate[2];
        slot[3] = candidate[3];
        slot[4] = candidate[4];

        volume = HullVolume( hull + 1, ( unsigned int )( int )hull[0] + 1 );
        if ( bestVolume > volume )
        {
            bestVolume = volume;
            best       = i;
        }
    }

    return ( int )best;
}

/* HullVolume  0x00438300 */
static float HullVolume( const float *axes, unsigned int axisCount )
{
    winding_t   *windings[HULL_AXIS_LIMIT * 2];
    unsigned int windingCount;
    float        volume;
    unsigned int i;

    windingCount = BuildHullWindings( axes, axisCount, windings );
    volume       = WindingListVolume( windings, windingCount );

    for ( i = 0; i < windingCount; i++ )
        FreeWinding( windings[i] );

    return volume;
}

/* BuildHullWindings  0x00438390 */
static unsigned int BuildHullWindings( const float *axes, unsigned int axisCount,
                                       winding_t **windingsOut )
{
    float        points[HULL_POINT_LIMIT_TOTAL * 6];
    vec3_t       facePoints[HULL_POINT_LIMIT_FACE];
    vec3_t       normal;
    unsigned int pointCount;
    unsigned int planeIndex;
    unsigned int ptCount;
    unsigned int windingCount;

    pointCount   = BuildHullPoints( axes, axisCount, points );
    windingCount = 0;

    for ( planeIndex = 0; planeIndex < axisCount * 2; planeIndex++ )
    {
        ptCount = CollectFacePoints( planeIndex, points, pointCount, facePoints );
        if ( ptCount <= 2 )
            continue;

        if ( ( planeIndex & 1 ) == 0 )
            Vec3Copy( &axes[( planeIndex >> 1 ) * 5], normal );
        else
            Vec3Negate( &axes[( planeIndex >> 1 ) * 5], normal );

        windingsOut[windingCount] = ConvexHullOfPoints( normal, facePoints, ptCount );
        if ( windingsOut[windingCount] )
            windingCount++;
    }

    return windingCount;
}

/* SolvePlaneTriple  0x00438650 */
static qboolean SolvePlaneTriple( const float *hull, unsigned int a, unsigned int b,
                                  unsigned int c, vec3_t *cornersOut )
{
    double       det;
    double       invDet;
    double       m00, m01, m02;
    double       m10, m11, m12;
    double       m20, m21, m22;
    double       da, db, dc;
    float        signA, signB, signC;
    unsigned int i;

    det = ( hull[b * 5 + 1] * hull[c * 5 + 2] - hull[c * 5 + 1] * hull[b * 5 + 2] ) * hull[a * 5 + 0]
        + ( hull[c * 5 + 1] * hull[a * 5 + 2] - hull[a * 5 + 1] * hull[c * 5 + 2] ) * hull[b * 5 + 0]
        + ( hull[a * 5 + 1] * hull[b * 5 + 2] - hull[b * 5 + 1] * hull[a * 5 + 2] ) * hull[c * 5 + 0];

    if ( fabs( det ) < 0.001f )
        return qfalse;

    invDet = 1.0 / det;

    m00 = ( hull[b * 5 + 1] * hull[c * 5 + 2] - hull[c * 5 + 1] * hull[b * 5 + 2] ) * invDet;
    m01 = ( hull[c * 5 + 1] * hull[a * 5 + 2] - hull[a * 5 + 1] * hull[c * 5 + 2] ) * invDet;
    m02 = ( hull[a * 5 + 1] * hull[b * 5 + 2] - hull[b * 5 + 1] * hull[a * 5 + 2] ) * invDet;

    m10 = ( hull[b * 5 + 2] * hull[c * 5 + 0] - hull[c * 5 + 2] * hull[b * 5 + 0] ) * invDet;
    m11 = ( hull[c * 5 + 2] * hull[a * 5 + 0] - hull[a * 5 + 2] * hull[c * 5 + 0] ) * invDet;
    m12 = ( hull[a * 5 + 2] * hull[b * 5 + 0] - hull[b * 5 + 2] * hull[a * 5 + 0] ) * invDet;

    m20 = ( hull[b * 5 + 0] * hull[c * 5 + 1] - hull[c * 5 + 0] * hull[b * 5 + 1] ) * invDet;
    m21 = ( hull[c * 5 + 0] * hull[a * 5 + 1] - hull[a * 5 + 0] * hull[c * 5 + 1] ) * invDet;
    m22 = ( hull[a * 5 + 0] * hull[b * 5 + 1] - hull[b * 5 + 0] * hull[a * 5 + 1] ) * invDet;

    for ( i = 0; i < 8; i++ )
    {
        signA = ( i & 1 ) ? 1.0f : -1.0f;
        da    = hull[a * 5 + 4] * signA + hull[a * 5 + 3];

        signB = ( i & 2 ) ? 1.0f : -1.0f;
        db    = hull[b * 5 + 4] * signB + hull[b * 5 + 3];

        signC = ( i & 4 ) ? 1.0f : -1.0f;
        dc    = hull[c * 5 + 4] * signC + hull[c * 5 + 3];

        cornersOut[i][0] = ( float )( da * m00 + db * m01 + dc * m02 );
        cornersOut[i][1] = ( float )( da * m10 + db * m11 + dc * m12 );
        cornersOut[i][2] = ( float )( da * m20 + db * m21 + dc * m22 );
    }

    return qtrue;
}

/* PointInsideAllSlabs  0x00438b20 */
static qboolean PointInsideAllSlabs( const float *hull, unsigned int axisCount,
                                     const vec3_t point )
{
    unsigned int i;

    for ( i = 0; i < axisCount; i++ )
    {
        if ( I_fabs( Vec3Dot( &hull[i * 5], point ) - hull[i * 5 + 3] ) - hull[i * 5 + 4] > 0.001f )
            return qfalse;
    }

    return qtrue;
}

/* BuildHullPoints  0x004384c0 */
static unsigned int BuildHullPoints( const float *hull, unsigned int axisCount, float *pointsOut )
{
    vec3_t       corners[8];
    unsigned int hullPointCount;
    unsigned int a;
    unsigned int b;
    unsigned int c;
    unsigned int corner;

    hullPointCount = 0;

    for ( c = 2; c < axisCount; c++ )
    {
        for ( b = 1; b < c; b++ )
        {
            for ( a = 0; a < b; a++ )
            {
                if ( !SolvePlaneTriple( hull, a, b, c, corners ) )
                    continue;

                for ( corner = 0; corner < 8; corner++ )
                {
                    if ( !PointInsideAllSlabs( hull, axisCount, corners[corner] ) )
                        continue;

                    AssertIn( hullPointCount, HULL_POINT_LIMIT_TOTAL );

                    Vec3Copy( corners[corner], &pointsOut[hullPointCount * 6] );
                    ( ( unsigned int * )pointsOut )[hullPointCount * 6 + 3] =
                        ( corner & 1 ) + a * 2;
                    ( ( unsigned int * )pointsOut )[hullPointCount * 6 + 4] =
                        ( ( corner >> 1 ) & 1 ) + b * 2;
                    ( ( unsigned int * )pointsOut )[hullPointCount * 6 + 5] =
                        ( ( corner >> 2 ) & 1 ) + c * 2;
                    hullPointCount++;
                }
            }
        }
    }

    return hullPointCount;
}

/* CollectFacePoints  0x00438bb0 */
static unsigned int CollectFacePoints( unsigned int planeIndex, const float *points,
                                       unsigned int pointCount, vec3_t *out )
{
    const unsigned int *tags;
    unsigned int        count;
    unsigned int        i;

    count = 0;
    for ( i = 0; i < pointCount; i++ )
    {
        tags = ( const unsigned int * )&points[i * 6 + 3];
        if ( tags[0] != planeIndex && tags[1] != planeIndex && tags[2] != planeIndex )
            continue;

        AssertIn( count, HULL_POINT_LIMIT_FACE );

        Vec3Copy( &points[i * 6], out[count] );
        count++;
    }

    return count;
}

/* ConvexHullOfPoints  0x00438c70 */
static winding_t *ConvexHullOfPoints( const vec3_t normal, vec3_t *points, unsigned int pointCount )
{
    winding_t   *a;
    winding_t   *b;
    unsigned int half;

    if ( pointCount > CONVEX_HULL_POINT_LIMIT )
    {
        half       = pointCount >> 1;
        pointCount = pointCount - half;

        a = ConvexHullOfPoints( normal, points, half );
        b = ConvexHullOfPoints( normal, points + half, pointCount );

        if ( !a )
            return b;
        if ( !b )
            return a;

        Assertx( a->ptCount <= half, "%i, %i", a->ptCount, half );
        Assertx( b->ptCount <= pointCount, "%i, %i", b->ptCount, pointCount );
        Assertx( a->ptCount + b->ptCount <= CONVEX_HULL_POINT_LIMIT, "%i, %i",
                 a->ptCount + b->ptCount, CONVEX_HULL_POINT_LIMIT );

        memcpy( points, a->pts, a->ptCount * sizeof( vec3_t ) );
        memcpy( points + a->ptCount, b->pts, b->ptCount * sizeof( vec3_t ) );
        pointCount = a->ptCount + b->ptCount;

        FreeWinding( a );
        FreeWinding( b );
    }

    return WindingForConvexHull( normal, points, pointCount );
}

/* WindingListVolume  0x00438e00 */
static float WindingListVolume( winding_t * const *windings, unsigned int windingCount )
{
    vec3_t       center;
    vec4_t       plane;
    float        area;
    float        dist;
    float        volume;
    unsigned int total;
    unsigned int i;
    unsigned int j;

    Vec3Clear( center );

    total = 0;
    for ( i = 0; i < windingCount; i++ )
    {
        total += windings[i]->ptCount;
        for ( j = 0; j < windings[i]->ptCount; j++ )
            Vec3Add( windings[i]->pts[j], center, center );
    }

    Vec3Scale( center, 1.0f / total, center );

    volume = 0.0f;
    for ( i = 0; i < windingCount; i++ )
    {
        area   = WindingPlane( windings[i], plane );
        dist   = Vec3Dot( center, plane ) - plane[3];
        volume = area * dist + volume;
    }

    return ( float )( volume / 3.0 );
}

/* EmitHull  0x00438f30 */
static BspLightRegionHull_t *EmitHull( const float *hull )
{
    BspLightRegionHull_t *out;
    BspLightRegionAxis_t *axes;
    unsigned int          axisCount;
    unsigned int          i;

    axisCount = ( unsigned int )hull[0] - HULL_KDOP_AXIS_COUNT;

    out = ( BspLightRegionHull_t * )
          operator new( sizeof( BspLightRegionHull_t ) +
                        axisCount * sizeof( BspLightRegionAxis_t ), std::nothrow );
    if ( !out )
        Com_Error( "Out of memory" );

    for ( i = 0; i < 3; i++ )
    {
        out->kdopMidPoint[i] = hull[i * 5 + 4];
        out->kdopHalfSize[i] = hull[i * 5 + 5];
    }

    for ( ; i < HULL_KDOP_AXIS_COUNT; i++ )
    {
        out->kdopMidPoint[i] = ( float )( hull[i * 5 + 4] / HULL_KDOP_DIAGONAL_SCALE );
        out->kdopHalfSize[i] = ( float )( hull[i * 5 + 5] / HULL_KDOP_DIAGONAL_SCALE );
    }

    out->axisCount = axisCount;
    axes           = ( BspLightRegionAxis_t * )( out + 1 );

    memcpy( axes, &hull[HULL_KDOP_AXIS_COUNT * 5 + 1],
            axisCount * sizeof( BspLightRegionAxis_t ) );

    return out;
}

/* HullPlaneCount / HullPlane  0x00439030 / 0x00439040 */
static int HullPlaneCount( const BspLightRegionHull_t *hull )
{
    return hull->axisCount * 2 + HULL_KDOP_AXIS_COUNT * 2;
}

static void HullPlane( const BspLightRegionHull_t *hull, unsigned int planeIndex, vec4_t out )
{
    const BspLightRegionAxis_t *axes;
    unsigned int                slab;

    slab = planeIndex >> 1;

    if ( slab < HULL_KDOP_AXIS_COUNT )
    {
        Vec3Copy( s_kdopNormals[planeIndex], out );
        if ( ( planeIndex & 1 ) == 0 )
            out[3] = hull->kdopMidPoint[slab] - hull->kdopHalfSize[slab];
        else
            out[3] = -hull->kdopMidPoint[slab] - hull->kdopHalfSize[slab];

        if ( slab > 2 )
            out[3] = out[3] * 0.70710677f;
        return;
    }

    slab -= HULL_KDOP_AXIS_COUNT;
    AssertIn( slab, hull->axisCount );

    axes = ( const BspLightRegionAxis_t * )( hull + 1 );

    if ( ( planeIndex & 1 ) == 0 )
    {
        Vec3Copy( axes[slab].dir, out );
        out[3] = axes[slab].midPoint - axes[slab].halfSize;
    }
    else
    {
        Vec3Negate( axes[slab].dir, out );
        out[3] = -axes[slab].midPoint - axes[slab].halfSize;
    }
}

/* HullWindings  0x00439190 */
static void HullWindings( const BspLightRegionHull_t *hull, winding_t **windingsOut )
{
    const BspLightRegionAxis_t *axes;
    float                       work[HULL_AXIS_LIMIT * 5];
    unsigned int                i;
    unsigned int                slot;

    for ( i = 0; i < 3; i++ )
    {
        Vec3Copy( s_kdopNormals[i * 2], &work[i * 5] );
        work[i * 5 + 3] = hull->kdopMidPoint[i];
        work[i * 5 + 4] = hull->kdopHalfSize[i];
    }

    for ( i = 3; i < HULL_KDOP_AXIS_COUNT; i++ )
    {
        Vec3Copy( s_kdopNormals[i * 2], &work[i * 5] );
        work[i * 5 + 3] = hull->kdopMidPoint[i] * 0.70710677f;
        work[i * 5 + 4] = hull->kdopHalfSize[i] * 0.70710677f;
    }

    axes = ( const BspLightRegionAxis_t * )( hull + 1 );

    for ( i = 0; i < hull->axisCount; i++ )
    {
        slot = i + HULL_KDOP_AXIS_COUNT;

        work[slot * 5 + 0] = axes[i].dir[0];
        work[slot * 5 + 1] = axes[i].dir[1];
        work[slot * 5 + 2] = axes[i].dir[2];
        work[slot * 5 + 3] = axes[i].midPoint;
        work[slot * 5 + 4] = axes[i].halfSize;
    }

    BuildHullWindings( work, hull->axisCount + HULL_KDOP_AXIS_COUNT, windingsOut );
}
