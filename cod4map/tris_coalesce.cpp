/* Original: .\tris_coalesce.cpp */


#include "tris_coalesce.h"

#include "cod4map.h"
#include "tris.h"
#include "surface.h"
#include "materials.h"
#include "errors.h"

#include <string.h>


extern void Com_DPrintf( const char *fmt, ... );                        /* 0x0040e760 */

extern void SetTrisTransientMode( int mode, int keepExisting );         /* 0x0043c6d0 */
extern void Tris_UnlinkSurf( TriSurf_t *surf, TriSurf_t **listHead ); /* 0x0043d1d0 */
extern void Tris_FreeSurface( TriSurf_t *surf );                        /* 0x0043d260 */
extern TriSurf_t *InsertTriSurfAfter( FixedWinding_t *fw, TriSurfProps_t *props,
                                          TriSurf_t *afterSurf );       /* 0x0043d3b0 */
extern TriSurfProps_t *CopyTriSurfProps( const TriSurfProps_t *props );  /* 0x0043dc80 */

extern bool TriSurfPropsGroupableFixed( const TriSurfProps_t *props0, const TriSurfProps_t *props1,
                               const FixedWinding_t *fw );              /* 0x0043dc40 */


coalesceGlob_t coalesceGlob;        /* 0x2b80df00 */


static int CoalesceAddToLeaf( WindingSurfBspNode_t *leaf, TriSurf_t *surf );
static unsigned int CoalescePropsHash( TriSurfProps_t **coalesceArray, int coalesceCount );
static int CoalesceTreeVisit( WindingSurfBspNode_t *node, const fixedvec3_t bounds[2],
                              CoalesceLeafFunc_t func, TriSurf_t *surf );
static void CoalesceBoundsForList( WindingSurfList_t *list, int count, fixedvec3_t bounds[2] );
static WindingSurfBspNode_t *CoalesceBuildNode( WindingSurfList_t *list, int count );
static void CoalesceBuildTree( TriSurf_t **listHead, fixedvec3_t bounds[2] );
static bool CoalesceChainMatches( const CoalesceNode_t *coalesceChain,
                                  TriSurfProps_t **coalesceArray, int coalesceCount );
static bool CoalesceChooseSplit( int best[4], WindingSurfList_t *list, int count,
                                 const fixedvec3_t mid );
static FixedWinding_t *CoalesceClipSurface( TriSurf_t *surf, const FixedWinding_t *chop,
                                            const vec3_t planeNormal, bool dbg );
static void CoalesceCrossingBounds( WindingSurfList_t *list, int count, fixedvec3_t bounds[2],
                                    const fixedvec3_t mid, int counts[3] );
static void CoalesceEmitFragment( FixedWinding_t *fw, TriSurfProps_t *props,
                                  TriSurf_t *afterSurf );
static void CoalesceFreeNode( WindingSurfBspNode_t *node );
static void CoalesceFreeTree( void );
static int CoalesceLeafSurf( WindingSurfBspNode_t *leaf, TriSurf_t *surf );
static void CoalesceLightmapVecs( TriSurfProps_t **coalesceArray, int coalesceCount,
                                  vec4_t lmapVecs[2] );
static CoalesceNode_t *CoalesceMakeChain( TriSurfProps_t **coalesceArray, int coalesceCount );
static void CoalesceMakeLeaf( WindingSurfBspNode_t *node, WindingSurfList_t *list );
static void CoalesceMakeNode( WindingSurfBspNode_t *node, int dist, int axis,
                              WindingSurfList_t *list );
static TriSurfProps_t *CoalesceProps( TriSurfProps_t *props0, TriSurfProps_t *props1,
                                      const FixedWinding_t *fw );
static bool CoalescePropsSortsBefore( const TriSurfProps_t *a, const TriSurfProps_t *b );
static void CoalesceRecurse( WindingSurfBspNode_t *node, fixedvec3_t bounds[2] );
static int CoalesceRemoveFromLeaf( WindingSurfBspNode_t *leaf, TriSurf_t *surf );
static bool CoalesceSplitBetter( const int a[4], const int b[4] );
static bool CoalesceSplitIsUseful( int dist, WindingSurfList_t *list, int count, int counts[4] );
static int CoalesceSplitScore( const int counts[4] );
static void CoalesceSurfaces( TriSurf_t *surf0, TriSurf_t *surf1, bool dbg );
static int CollectCoalesceLeaves( TriSurfProps_t *props, TriSurfProps_t **coalesceArray,
                                  int propsCount, const FixedWinding_t *fw );
static int CountHiddenSurfaces( TriSurfProps_t **coalesceArray, int coalesceCount );
static int FixedRangeSide( int min, int max, int dist );
static void ReportFloatingSurfaces( TriSurf_t *surf );
static int TrisCoalesceTest( TriSurf_t *surf0, TriSurf_t *surf1 );
static void ValidateVisGroupList( TriSurf_t **listHead );

/* CopyCoalesceChain  0x0044d3b0 */
CoalesceNode_t *CopyCoalesceChain( const CoalesceNode_t *chain )
{
    CoalesceNode_t *copy;

    if ( !chain )
        return NULL;

    copy = new CoalesceNode_t;
    if ( !copy )
        Com_Error( "Out of memory" );

    copy->props = CopyTriSurfProps( chain->props );
    copy->next  = CopyCoalesceChain( chain->next );

    return copy;
}


/* ReportFloatingSurface  0x0044d420 */
void ReportFloatingSurface( TriSurf_t *surf )
{
    DrawSurf_t *ds;

    if ( !Material_HasKnownTechSet( surf->props->ds->mtlTex ) )
        return;

    ds = surf->props->ds;

    MapErrorWinding( 0, surf->w, ds->mapInfoIndex, ds->entityNum, ds->brushNum,
                     "surface '%s' is partially floating or needs to be aligned",
                     StringFromOffset( surf->props->ds->mtlTex ) );
}


/* TrisCoalesce  0x0044d490 */
void TrisCoalesce( TriSurf_t **listHead )
{
    fixedvec3_t bounds[2];

    ValidateVisGroupList( listHead );
    CoalesceBuildTree( listHead, bounds );

    SetTrisTransientMode( TRIS_TRANSIENT_COALESCE, 0 );
    CoalesceRecurse( coalesceGlob.tree, bounds );
    SetTrisTransientMode( TRIS_TRANSIENT_NONE, 1 );

    ReportFloatingSurfaces( *listHead );
    CoalesceFreeTree();
}


/* CoalesceBuildTree  0x0044d500 */
static void CoalesceBuildTree( TriSurf_t **listHead, fixedvec3_t bounds[2] )
{
    WindingSurfList_t *list;
    WindingSurfList_t *entry;
    TriSurf_t         *surf;
    int                count;

    count = 0;
    list  = NULL;

    for ( surf = *listHead; surf; surf = surf->visGroupNext )
    {
        if ( ( surf->props->ds->mtlTex->surfaceFlags & 0x80 ) != 0 )
            continue;

        surf->fw = FixedWindingFromWinding( surf->w );
        FixedWindingBounds( surf->fw, surf->bounds );
        count++;

        entry = new WindingSurfList_t;
        entry->surf = surf;
        entry->next = list;
        list = entry;
    }

    CoalesceBoundsForList( list, count, bounds );

    coalesceGlob.tree       = CoalesceBuildNode( list, count );
    coalesceGlob.listHead   = listHead;
    coalesceGlob.checkCount = 0;
}


/* CoalesceBoundsForList  0x0044d5f0 */
static void CoalesceBoundsForList( WindingSurfList_t *list, int count, fixedvec3_t bounds[2] )
{
    WindingSurfList_t *entry;

    ClearFixedBounds( bounds );

    for ( entry = list; entry; entry = entry->next )
        AddFixedBoundsToBounds( entry->surf->bounds, bounds );
}


/* CoalesceBuildNode  0x0044d640 */
static WindingSurfBspNode_t *CoalesceBuildNode( WindingSurfList_t *list, int count )
{
    WindingSurfBspNode_t *node;
    fixedvec3_t bounds[2];
    fixedvec3_t mid;
    int         best[4];
    int         crossCounts[3];
    int         attempt;

    node = new WindingSurfBspNode_t;
    if ( !node )
        Com_Error( "out of memory: WindingSurfBspNode" );

    if ( count <= COALESCE_LEAF_SURFS )
    {
        CoalesceMakeLeaf( node, list );
        return node;
    }

    CoalesceBoundsForList( list, count, bounds );
    FixedVec3Mid( bounds[0], bounds[1], mid );

    if ( CoalesceChooseSplit( best, list, count, mid ) )
    {
        CoalesceMakeNode( node, mid[ best[0] ], best[0], list );
        return node;
    }

    for ( attempt = 0; attempt < COALESCE_SPLIT_ATTEMPTS; attempt++ )
    {
        CoalesceCrossingBounds( list, count, bounds, mid, crossCounts );
        FixedVec3Mid( bounds[0], bounds[1], mid );

        if ( CoalesceChooseSplit( best, list, count, mid ) )
        {
            CoalesceMakeNode( node, mid[ best[0] ], best[0], list );
            return node;
        }

        if ( count < COALESCE_FORCE_LEAF_SURFS )
        {
            CoalesceMakeLeaf( node, list );
            return node;
        }
    }

    Com_DPrintf( "Failed to find a valid split plane for leaf with %d triangles.\n", count );
    CoalesceMakeLeaf( node, list );

    return node;
}


/* CoalesceMakeLeaf  0x0044d7e0 */
static void CoalesceMakeLeaf( WindingSurfBspNode_t *node, WindingSurfList_t *list )
{
    Assert( node );

    node->axis = -1;
    node->list = list;
}


/* CoalesceCrossingBounds  0x0044d820 */
static void CoalesceCrossingBounds( WindingSurfList_t *list, int count, fixedvec3_t bounds[2],
                                    const fixedvec3_t mid, int counts[3] )
{
    WindingSurfList_t *entry;
    int axis;

    ClearFixedBounds( bounds );

    for ( axis = 0; axis < 3; axis++ )
    {
        counts[axis] = 0;

        for ( entry = list; entry; entry = entry->next )
        {
            if ( FixedRangeSide( entry->surf->bounds[0][axis], entry->surf->bounds[1][axis],
                                 mid[axis] ) == SIDE_CROSS )
            {
                continue;
            }

            counts[axis]++;

            if ( entry->surf->bounds[0][axis] < bounds[0][axis] )
                bounds[0][axis] = entry->surf->bounds[0][axis];

            if ( bounds[1][axis] < entry->surf->bounds[1][axis] )
                bounds[1][axis] = entry->surf->bounds[1][axis];
        }
    }
}


/* FixedRangeSide  0x0044d930 */
static int FixedRangeSide( int min, int max, int dist )
{
    if ( dist + COALESCE_EPSILON_FIXED < min )
        return SIDE_FRONT;

    if ( max < dist - COALESCE_EPSILON_FIXED )
        return SIDE_BACK;

    return SIDE_CROSS;
}


/* CoalesceMakeNode  0x0044d960 */
static void CoalesceMakeNode( WindingSurfBspNode_t *node, int dist, int axis,
                              WindingSurfList_t *list )
{
    WindingSurfList_t *frontList;
    WindingSurfList_t *backList;
    WindingSurfList_t *entry;
    WindingSurfList_t *next;
    WindingSurfList_t *dup;
    int frontCount;
    int backCount;
    int side;

    frontList  = NULL;
    backList   = NULL;
    frontCount = 0;
    backCount  = 0;
    entry      = list;

    while ( entry )
    {
        next = entry->next;

        side = FixedRangeSide( entry->surf->bounds[0][axis], entry->surf->bounds[1][axis], dist );

        if ( side == SIDE_BACK )
        {
            entry->next = backList;
            backList = entry;
            backCount++;
            entry = next;
        }
        else
        {
            SanityCheckx( side == SIDE_FRONT || side == SIDE_CROSS, "(side) = %i", side );

            if ( side == SIDE_CROSS )
            {
                dup = new WindingSurfList_t;
                if ( !dup )
                    Com_Error( "out of memory: WindingSurfList" );

                dup->surf = entry->surf;
                dup->next = backList;
                backCount++;
                backList = dup;
            }

            entry->next = frontList;
            frontList = entry;
            frontCount++;
            entry = next;
        }
    }

    node->axis = axis;
    node->dist = dist;
    node->children[SIDE_BACK]  = CoalesceBuildNode( backList,  backCount );
    node->children[SIDE_FRONT] = CoalesceBuildNode( frontList, frontCount );
}


/* CoalesceChooseSplit  0x0044dad0 */
static bool CoalesceChooseSplit( int best[4], WindingSurfList_t *list, int count,
                                 const fixedvec3_t mid )
{
    int cand[4];

    best[0] = -1;

    for ( cand[0] = 0; cand[0] < 3; cand[0]++ )
    {
        if ( !CoalesceSplitIsUseful( mid[ cand[0] ], list, count, cand ) )
            continue;

        if ( best[0] != -1 && !CoalesceSplitBetter( cand, best ) )
            continue;

        best[0] = cand[0];
        best[1] = cand[1];
        best[2] = cand[2];
        best[3] = cand[3];
    }

    return best[0] != -1;
}


/* CoalesceSplitIsUseful  0x0044db70 */
static bool CoalesceSplitIsUseful( int dist, WindingSurfList_t *list, int count, int counts[4] )
{
    WindingSurfList_t *entry;
    int axis;
    int side;

    axis = counts[0];

    counts[1] = 0;
    counts[2] = 0;
    counts[3] = 0;

    for ( entry = list; entry; entry = entry->next )
    {
        side = FixedRangeSide( entry->surf->bounds[0][axis], entry->surf->bounds[1][axis], dist );

        SanityCheckx( side == SIDE_FRONT || side == SIDE_BACK || side == SIDE_CROSS,
                      "(side) = %i", side );

        if ( side == SIDE_FRONT )
            counts[2]++;
        if ( side == SIDE_BACK )
            counts[1]++;
        if ( side == SIDE_CROSS )
            counts[3]++;
    }

    if ( counts[1] + counts[3] == count || counts[2] + counts[3] == count )
        return false;

    return true;
}


/* CoalesceSplitBetter  0x0044dc90 */
static bool CoalesceSplitBetter( const int a[4], const int b[4] )
{
    return CoalesceSplitScore( a ) < CoalesceSplitScore( b );
}

static int CoalesceSplitScore( const int counts[4] )
{
    return abs( counts[1] - counts[2] ) + counts[3];
}


/* CoalesceFreeTree  0x0044dce0 */
static void CoalesceFreeTree( void )
{
    CoalesceFreeNode( coalesceGlob.tree );
    coalesceGlob.tree     = NULL;
    coalesceGlob.listHead = NULL;
}


/* CoalesceFreeNode  0x0044dd10 */
static void CoalesceFreeNode( WindingSurfBspNode_t *node )
{
    WindingSurfList_t *dead;

    Assert( node );

    if ( node->axis == -1 )
    {
        while ( node->list )
        {
            FreeFixedWinding( node->list->surf->fw );
            node->list->surf->fw = NULL;

            dead = node->list;
            node->list = dead->next;
            delete dead;
        }
    }
    else
    {
        CoalesceFreeNode( node->children[SIDE_FRONT] );
        CoalesceFreeNode( node->children[SIDE_BACK] );
    }

    delete node;
}


/* ValidateVisGroupList  0x0044dde0 */
static void ValidateVisGroupList( TriSurf_t **listHead )
{
    TriSurf_t *surf;

    Assert( listHead );

    surf = *listHead;
    if ( !surf )
        return;

    Assert( surf->visGroupPrev == NULL );

    for ( ; surf->visGroupNext; surf = surf->visGroupNext )
        Assert( surf->visGroupNext->visGroupPrev == surf );
}


/* ReportFloatingSurfaces  0x0044de90 */
static void ReportFloatingSurfaces( TriSurf_t *surf )
{
    for ( ; surf; surf = surf->visGroupNext )
    {
        if ( surf->props->coalesceChain &&
             ( surf->props->coalesceChain->props->ds->mtlTex->toolFlags & TOOLFLAG_USAGE_MASK )
                 == TOOLFLAG_USAGE_NOCOMBINE )
        {
            ReportFloatingSurface( surf );
        }
    }
}


/* CoalesceRecurse  0x0044def0 */
static void CoalesceRecurse( WindingSurfBspNode_t *node, fixedvec3_t bounds[2] )
{
    WindingSurfList_t *entry;
    TriSurf_t         *surf;
    int                savedMax;
    int                merged;

    for ( ; node->axis != -1; node = node->children[SIDE_FRONT] )
    {
        savedMax = bounds[1][ node->axis ];
        bounds[1][ node->axis ] = node->dist;

        CoalesceRecurse( node->children[SIDE_BACK], bounds );

        bounds[1][ node->axis ] = savedMax;
        bounds[0][ node->axis ] = node->dist;
    }

    entry = node->list;

    while ( entry )
    {
        surf = entry->surf;

        if ( surf->transient.coalesce.alreadyVisited )
        {
            entry = entry->next;
            continue;
        }

        coalesceGlob.checkCount++;
        surf->transient.coalesce.alreadyVisited = 1;
        surf->transient.coalesce.checkCount     = coalesceGlob.checkCount;

        if ( FixedBoundsInsideBounds( surf->bounds, bounds, COALESCE_EPSILON_FIXED ) )
            merged = CoalesceLeafSurf( node, surf );
        else
            merged = CoalesceTreeVisit( coalesceGlob.tree, surf->bounds, CoalesceLeafSurf, surf );

        entry = ( merged == 1 ) ? node->list : entry->next;
    }
}


/* CoalesceTreeVisit  0x0044e030 */
static int CoalesceTreeVisit( WindingSurfBspNode_t *node, const fixedvec3_t bounds[2],
                              CoalesceLeafFunc_t func, TriSurf_t *surf )
{
    int side;

    for ( ;; )
    {
        while ( node->axis != -1 )
        {
            side = FixedRangeSide( bounds[0][ node->axis ], bounds[1][ node->axis ], node->dist );
            if ( side == SIDE_CROSS )
                break;

            node = node->children[side];
        }

        if ( node->axis == -1 )
            return func( node, surf );

        if ( CoalesceTreeVisit( node->children[SIDE_BACK], bounds, func, surf ) == 1 )
            return 1;

        node = node->children[SIDE_FRONT];
    }
}


/* CoalesceLeafSurf  0x0044e0d0 */
static int CoalesceLeafSurf( WindingSurfBspNode_t *leaf, TriSurf_t *surf )
{
    WindingSurfList_t *iter;
    int  result;
    bool dbg;

    Assert( surf->transient.coalesce.checkCount == coalesceGlob.checkCount );
    Assert( surf->transient.coalesce.alreadyVisited );

    for ( iter = leaf->list; ; iter = iter->next )
    {
        if ( iter->surf == surf )
            return 0;

        if ( iter->surf->transient.coalesce.checkCount == coalesceGlob.checkCount )
            continue;

        Assert( iter->surf != surf );
        iter->surf->transient.coalesce.checkCount = coalesceGlob.checkCount;

        result = TrisCoalesceTest( surf, iter->surf );
        if ( !result )
            continue;

        dbg = ( surf == ( TriSurf_t * )0x512d0700 && iter->surf == ( TriSurf_t * )0x5383ffa8 );

        if ( result == 1 )
            CoalesceSurfaces( surf, iter->surf, dbg );
        else
            CoalesceSurfaces( iter->surf, surf, dbg );

        return 1;
    }
}


/* TrisCoalesceTest  0x0044e250 */
static int TrisCoalesceTest( TriSurf_t *surf0, TriSurf_t *surf1 )
{
    const float *plane;
    double dist1;
    double dist0;
    int    result;

    if ( Vec3Dot( surf0->props->plane, surf1->props->plane ) < COALESCE_MIN_DOT )
        return 0;

    if ( !FixedBoundsOverlap( surf0->bounds, surf1->bounds, COALESCE_EPSILON_FIXED ) )
        return 0;

    dist1 = FixedWindingMaxAbsDist( surf1->fw, surf0->props->plane, surf0->props->plane[3] );
    if ( dist1 > COALESCE_EPSILON )
        return 0;

    dist0 = FixedWindingMaxAbsDist( surf0->fw, surf1->props->plane, surf1->props->plane[3] );
    if ( dist0 > COALESCE_EPSILON )
        return 0;

    if ( dist0 < dist1 )
    {
        plane  = surf1->props->plane;
        result = 2;
    }
    else
    {
        plane  = surf0->props->plane;
        result = 1;
    }

    if ( !surf0->props->mergeTouching && !surf1->props->mergeTouching )
    {
        if ( !FixedWindingsOverlap( surf0->fw, surf1->fw, plane, COALESCE_EPSILON ) )
            result = 0;
    }
    else
    {
        if ( FixedWindingsAreDisjoint( surf0->fw, surf1->fw, plane, COALESCE_EPSILON ) )
            result = 0;
    }

    return result;
}


/* CoalesceSurfaces  0x0044e410 */
static void CoalesceSurfaces( TriSurf_t *surf0, TriSurf_t *surf1, bool dbg )
{
    FixedWinding_t *fw0;
    FixedWinding_t *shared;
    TriSurfProps_t *props;

    Assert( surf0 );
    Assert( surf1 );
    SanityCheck( surf0->fw );
    SanityCheck( surf1->fw );
    SanityCheck( surf0->fw != surf1->fw );
    SanityCheck( surf0->props );
    SanityCheck( surf1->props );

    CoalesceTreeVisit( coalesceGlob.tree, surf0->bounds, CoalesceRemoveFromLeaf, surf0 );
    CoalesceTreeVisit( coalesceGlob.tree, surf1->bounds, CoalesceRemoveFromLeaf, surf1 );

    fw0 = CopyFixedWinding( surf0->fw );

    shared = CoalesceClipSurface( surf0, surf1->fw, surf0->props->plane, dbg );
    if ( shared )
        FreeFixedWinding( shared );

    shared = CoalesceClipSurface( surf1, fw0, surf0->props->plane, dbg );
    if ( shared )
    {
        props = CoalesceProps( surf0->props, surf1->props, shared );
        CoalesceEmitFragment( shared, props, surf0 );
    }

    FreeFixedWinding( fw0 );

    surf0->fw = NULL;
    surf1->fw = NULL;

    Tris_UnlinkSurf( surf0, coalesceGlob.listHead );
    Tris_UnlinkSurf( surf1, coalesceGlob.listHead );
    Tris_FreeSurface( surf0 );
    Tris_FreeSurface( surf1 );
}


/* CoalesceRemoveFromLeaf  0x0044e680 */
static int CoalesceRemoveFromLeaf( WindingSurfBspNode_t *leaf, TriSurf_t *surf )
{
    WindingSurfList_t **link;
    WindingSurfList_t  *dead;

    link = &leaf->list;

    while ( *link )
    {
        if ( ( *link )->surf == surf )
        {
            dead = *link;
            *link = dead->next;
            delete dead;
        }
        else
        {
            link = &( *link )->next;
        }
    }

    return 0;
}


/* CoalesceProps  0x0044e6f0 */
static TriSurfProps_t *CoalesceProps( TriSurfProps_t *props0, TriSurfProps_t *props1,
                                      const FixedWinding_t *fw )
{
    TriSurfProps_t *coalesceArray[MAX_COINCIDENT_WINDINGS];
    TriSurfProps_t *result;
    unsigned int    coalesceCount;
    unsigned int    hiddenCount;
    int             propsCount;
    int             hash;

    if ( TriSurfPropsGroupableFixed( props0, props1, fw ) )
        return props0;

    propsCount    = CollectCoalesceLeaves( props0, coalesceArray, 0, fw );
    coalesceCount = CollectCoalesceLeaves( props1, coalesceArray, propsCount, fw );

    result = coalesceArray[0];

    if ( coalesceCount == 1 || coalesceGlob.disableMerge )
        return result;

    Assert( ( int )coalesceCount >= 2 );

    hiddenCount = CountHiddenSurfaces( coalesceArray, coalesceCount );
    AssertIn( ( int )hiddenCount, ( int )coalesceCount );

    if ( hiddenCount == coalesceCount - 1 )
    {
        Assert( coalesceArray[coalesceCount - 1] );
        Assert( coalesceArray[coalesceCount - 1]->coalesceChain == NULL );
        Assert( coalesceArray[coalesceCount - 1]->ds->mtlTex != NULL );

        return coalesceArray[coalesceCount - 1];
    }

    hash = CoalescePropsHash( coalesceArray + hiddenCount, coalesceCount - hiddenCount );

    for ( result = coalesceGlob.propsHash[hash]; result; result = result->next )
    {
        Assert( result->coalesceChain );

        if ( CoalesceChainMatches( result->coalesceChain, coalesceArray + hiddenCount,
                                   coalesceCount - hiddenCount ) )
        {
            return result;
        }
    }

    result = ( TriSurfProps_t * )operator new( sizeof( TriSurfProps_t ) );
    memset( result, 0, sizeof( TriSurfProps_t ) );

    Vec4Copy( props0->plane, result->plane );
    CoalesceLightmapVecs( coalesceArray + hiddenCount, coalesceCount - hiddenCount,
                          result->lmapVecs );

    result->ds        = coalesceArray[hiddenCount]->ds;
    result->lmapGroupId   = -1;
    result->lmapIndex = LIGHTMAP_NONE;

    result->mergeTouching = ( props0->mergeTouching || props1->mergeTouching );
    result->nodraw     = ( props0->nodraw && props1->nodraw );

    if ( props0->subdivisions != 0.0f && props1->subdivisions != 0.0f )
        result->subdivisions = I_fmin( props0->subdivisions, props1->subdivisions );
    else
        result->subdivisions = props0->subdivisions + props1->subdivisions;

    result->coalesceChain = CoalesceMakeChain( coalesceArray + hiddenCount,
                                               coalesceCount - hiddenCount );

    result->next = coalesceGlob.propsHash[hash];
    coalesceGlob.propsHash[hash] = result;

    return result;
}


/* CollectCoalesceLeaves  0x0044eac0 */
static int CollectCoalesceLeaves( TriSurfProps_t *props, TriSurfProps_t **coalesceArray,
                                  int propsCount, const FixedWinding_t *fw )
{
    CoalesceNode_t *entry;
    fixedvec3_t      center;
    vec3_t           pos;
    int              propsIndex;

    if ( props->coalesceChain )
    {
        for ( entry = props->coalesceChain; entry; entry = entry->next )
            propsCount = CollectCoalesceLeaves( entry->props, coalesceArray, propsCount, fw );

        return propsCount;
    }

    for ( propsIndex = 0; propsIndex < propsCount; propsIndex++ )
    {
        if ( TriSurfPropsGroupableFixed( props, coalesceArray[propsIndex], fw ) )
            return propsCount;
    }

    if ( propsCount == MAX_COINCIDENT_WINDINGS )
    {
        FixedWindingCenter( fw, center );
        FixedToVec3( center, pos );
        Com_Error( "MAX_COINCIDENT_WINDINGS (%i) exceeded.  Too many polys overlap at %g %g %g.\n",
                   MAX_COINCIDENT_WINDINGS, pos[0], pos[1], pos[2] );
    }

    Assert( propsIndex == propsCount );

    while ( propsIndex > 0 && CoalescePropsSortsBefore( props, coalesceArray[propsIndex - 1] ) )
    {
        coalesceArray[propsIndex] = coalesceArray[propsIndex - 1];
        propsIndex--;
    }

    coalesceArray[propsIndex] = props;

    return propsCount + 1;
}


/* CoalesceChainMatches  0x0044ec50 */
static bool CoalesceChainMatches( const CoalesceNode_t *coalesceChain,
                                  TriSurfProps_t **coalesceArray, int coalesceCount )
{
    int i;

    Assert( coalesceChain );
    Assert( coalesceArray );
    Assert( coalesceCount >= 2 );

    for ( i = 0; i < coalesceCount; i++ )
    {
        if ( !coalesceChain || coalesceArray[i] != coalesceChain->props )
            return false;

        coalesceChain = coalesceChain->next;
    }

    return coalesceChain == NULL;
}


/* CoalescePropsSortsBefore  0x0044ec30 */
static bool CoalescePropsSortsBefore( const TriSurfProps_t *a, const TriSurfProps_t *b )
{
    return Material_SortsBefore( a->ds->mtlTex, b->ds->mtlTex );
}


/* CoalesceMakeChain  0x0044ed20 */
static CoalesceNode_t *CoalesceMakeChain( TriSurfProps_t **coalesceArray, int coalesceCount )
{
    CoalesceNode_t *entry;

    if ( coalesceCount == 0 )
        return NULL;

    entry = new CoalesceNode_t;
    entry->props = coalesceArray[0];
    entry->props->ds->wasCoalesced = 1;
    entry->next = CoalesceMakeChain( coalesceArray + 1, coalesceCount - 1 );

    return entry;
}


/* CoalescePropsHash  0x0044ed80 */
static unsigned int CoalescePropsHash( TriSurfProps_t **coalesceArray, int coalesceCount )
{
    const byte  *bytes;
    unsigned int hash;
    unsigned int i;

    bytes = ( const byte * )coalesceArray;
    hash  = 0;

    for ( i = 0; i < ( unsigned int )( coalesceCount * 4 ); i++ )
        hash = bytes[i] + hash + ( i + 0x75 ) * hash;

    return hash & ( COALESCE_PROPS_HASH_SIZE - 1 );
}


/* CountHiddenSurfaces  0x0044ede0 */
static int CountHiddenSurfaces( TriSurfProps_t **coalesceArray, int coalesceCount )
{
    int hiddenCount;
    int i;

    hiddenCount = 0;

    for ( i = 1; i < coalesceCount; i++ )
    {
        if ( ( coalesceArray[i]->ds->mtlTex->toolFlags & TOOLFLAG_USAGE_MASK ) == TOOLFLAG_USAGE_LIT )
            hiddenCount = i;
    }

    return hiddenCount;
}


/* CoalesceLightmapVecs  0x0044ee30 */
static void CoalesceLightmapVecs( TriSurfProps_t **coalesceArray, int coalesceCount,
                                  vec4_t lmapVecs[2] )
{
    int i;

    for ( i = 0; i < coalesceCount; i++ )
    {
        if ( coalesceArray[i]->ds->mtlTex->techSetFlags & 2 )
        {
            memcpy( lmapVecs, coalesceArray[i]->lmapVecs, sizeof( vec4_t ) * 2 );
            return;
        }
    }

    SanityCheck( Vec4Compare( lmapVecs[0], vec4_origin ) );
    SanityCheck( Vec4Compare( lmapVecs[1], vec4_origin ) );
}


/* CoalesceEmitFragment  0x0044ef00 */
static void CoalesceEmitFragment( FixedWinding_t *fw, TriSurfProps_t *props,
                                  TriSurf_t *afterSurf )
{
    TriSurf_t *surf;

    if ( FixedWindingIsReversed( fw, props->plane ) )
    {
        FreeFixedWinding( fw );
        return;
    }

    surf = InsertTriSurfAfter( fw, props, afterSurf );
    FixedWindingBounds( surf->fw, surf->bounds );

    CoalesceTreeVisit( coalesceGlob.tree, surf->bounds, CoalesceAddToLeaf, surf );
}


/* CoalesceAddToLeaf  0x0044ef80 */
static int CoalesceAddToLeaf( WindingSurfBspNode_t *leaf, TriSurf_t *surf )
{
    WindingSurfList_t  *entry;
    WindingSurfList_t **link;

    entry = new WindingSurfList_t;
    entry->surf = surf;
    entry->next = NULL;

    for ( link = &leaf->list; *link; link = &( *link )->next )
    {
    }

    *link = entry;

    return 0;
}


/* CoalesceClipSurface  0x0044efe0 */
static FixedWinding_t *CoalesceClipSurface( TriSurf_t *surf, const FixedWinding_t *chop,
                                            const vec3_t planeNormal, bool dbg )
{
    FixedWinding_t *remaining;
    FixedWinding_t *front;
    FixedWinding_t *back;
    vec3_t          edgeNormal;
    double          edgeDist;
    unsigned int    prev;
    unsigned int    i;
    bool            anyFrontYet;

    Assert( surf );
    Assert( chop );
    Assert( planeNormal );

    remaining = surf->fw;
    surf->fw  = NULL;

    anyFrontYet = false;
    prev = chop->ptCount - 1;

    for ( i = 0; i < chop->ptCount && remaining; i++ )
    {
        if ( !FixedEdgePlane( chop->pts[prev], chop->pts[i], planeNormal, edgeNormal, &edgeDist ) )
        {
            FreeFixedWinding( remaining );
            return NULL;
        }

        ClipFixedWindingEpsilon( remaining, chop, edgeNormal, edgeDist, COALESCE_EPSILON,
                                 &front, &back );

        Assert( back || anyFrontYet );

        if ( front )
        {
            CoalesceEmitFragment( front, surf->props, surf );
            anyFrontYet = true;
        }

        FreeFixedWinding( remaining );
        remaining = back;
        prev = i;
    }

    return remaining;
}


/* CoalesceClearPropsHash  0x0044f1a0 */
void CoalesceClearPropsHash( void )
{
    memset( coalesceGlob.propsHash, 0, sizeof( coalesceGlob.propsHash ) );
}


/* FixedVec3Mid  0x0044f1c0 */
void FixedVec3Mid( const fixedvec3_t a, const fixedvec3_t b, fixedvec3_t out )
{
    out[0] = FixedMid( a[0], b[0] );
    out[1] = FixedMid( a[1], b[1] );
    out[2] = FixedMid( a[2], b[2] );
}

int FixedMid( int a, int b )
{
    return ( int )( ( ( __int64 )( a + b ) ) >> 1 );
}
