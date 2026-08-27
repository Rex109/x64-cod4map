/* Original: .\tris_lightmap.cpp */

#include "tris_lightmap.h"

#include "cod4map.h"
#include "tris.h"
#include "tris_gridtree.h"
#include "tris_tesselate.h"
#include "surface.h"
#include "materials.h"
#include "errors.h"
#include "bsp.h"

#include <stdlib.h>
#include <string.h>

#include "qsort_vc8.h"


extern int  GetTrisTransientMode( void );                               /* 0x0043c6c0 */
extern int  Tris_GetSurfCount( void );                             /* 0x0043d070 */
extern void Tris_ForEachSurf( void ( *callback )( TriSurf_t *, bool ),
                              TriSurf_t *targetSurf );                  /* 0x0043d080 */
extern void Tris_ValidateSurfLmapCoords( const winding_t *w, const TriSurfProps_t *props ); /* 0x0043c560 */
extern TriSurfProps_t *CopyTriSurfProps( const TriSurfProps_t *props );  /* 0x0043dc80 */

extern void SetGridDivisionPoints( const vec3_t mins, const vec3_t maxs );      /* 0x004502a0 */
extern void GridTree_Insert( TriSurf_t *surf );                        /* 0x00450440 */
extern void GridTree_ForEach( const vec3_t mins, const vec3_t maxs,
                                      void ( *callback )( TriSurf_t * ) ); /* 0x00450bf0 */

extern float smoothAngle;                                               /* 0x005296a0 */


trisLmapGlob_t trisLmapGlob;        /* 0x2b80e330 */


static int LmapAreaScore( int w, int h );
static bool LmapBestRectAt( int x, int y, int *bestW, int *bestH, int *bestScore );
static void LmapBucketSurfaceByMaterial( TriSurf_t *surf, bool isTargetSurf );
static bool LmapCanShareChart( const TriSurf_t *surf0, const TriSurf_t *surf1 );
static void LmapClearSpanForBounds( const vec2_t bounds[2], vec2_t scratch[2] );
static void LmapClearSpanForTri( const winding_t *w, const winding_t *wOrig, TriSurfProps_t *props,
                                 int i0, int i1, int i2, int visGroupIndex );
static void LmapClearSpanRect( int minX, int minY, int x0, int y0, int x1, int y1 );
static void LmapCollectSurface( TriSurf_t *surf, bool isTargetSurf );
static int LmapCompareMtlChains( const void *va, const void *vb );
static int LmapCompareSurfsByPropsAndGroup( const void *va, const void *vb );
static void LmapDonateFreeRects( int lmapIndex, int minX, int minY, int maxX, int maxY );
static void LmapExpandBounds( float expand, float *bounds );
static bool LmapFindShift( TriSurf_t *surf0, TriSurf_t *surf1, char *haveShift,
                           vec2_t lmapShift );
static void LmapFreeAllGroups( void );
static void LmapFreeGroup( lmapGroup_t *group );
static bool LmapGroupIdIsSet( const lmapGroup_t *group, unsigned int id );
static unsigned int LmapClipToPlane( const winding_t *w, const float *dists,
                                     vec3_t *out, unsigned int ptLimit );
static int LmapGroupTexelScore( const TriSurf_t *surf );
static bool LmapGroupsAlreadyTried( const lmapGroup_t *group0, const lmapGroup_t *group1 );
static bool LmapGroupsCanMerge( TriSurf_t *surf0, TriSurf_t *surf1, const vec2_t lmapShift );
static void LmapMergeAcrossEdge( TriSurf_t *surf0, TriSurf_t *surf1, const float *dists );
static void LmapMergeCoplanar( TriSurf_t *surf0, TriSurf_t *surf1 );
static void LmapMergeGroups( lmapGroup_t *groupGrow, lmapGroup_t *groupKill,
                             const vec2_t lmapShift );
static void LmapNewGroup( TriSurf_t *surf );
static void LmapPlaceGroup( lmapGroup_t *group );
static void LmapPlaneDists( const winding_t *w, const vec4_t plane, float *dists,
                            float *outMin, float *outMax );
static TriSurfProps_t *LmapPropsForLightmap( TriSurfProps_t *props, unsigned int groupId,
                                             unsigned int lmapIndex,
                                             const vec4_t vecs0, const vec4_t vecs1,
                                             int x, int y, int blockW, int blockH );
static bool LmapPropsNeedsCopy( const TriSurfProps_t *props, unsigned int groupId,
                                unsigned int lmapIndex,
                                const vec4_t vecs0, const vec4_t vecs1 );
static void LmapReclaimTexels( lmapGroup_t *group, int lmapIndex, int x, int y,
                               int w, int h, int rotated );
static bool LmapSameChart( TriSurf_t *surf0, TriSurf_t *surf1, const vec2_t lmapShift );
static bool LmapSegmentsCross( const float *a0, const float *a1,
                               const float *b0, const float *b1 );
static void LmapSetGroupId( lmapGroup_t *group, unsigned int id );
static bool LmapShiftForPoint( const vec3_t xyz, const vec4_t *vecs0, const vec4_t *vecs1,
                               char haveShift, vec2_t lmapShift );
static TriSurf_t *LmapSortMtlChain( TriSurf_t *listHead );
static TriSurf_t *LmapSortMtlChainWorker( TriSurf_t *listHead, int count );
static TriSurf_t *LmapSortSurfList( TriSurf_t *listHead, unsigned int count );
static bool LmapSurfLessThan( const TriSurf_t *surf0, const TriSurf_t *surf1 );
static bool LmapSurfacesOverlap( TriSurf_t *surf0, TriSurf_t *surf1, const vec2_t lmapShift );
static void LmapTryMergeSurfaces( TriSurf_t *surf0, TriSurf_t *surf1 );
static void LmapTryMergeWithSurf( TriSurf_t *surf );
static bool LmapVecsMatch( const vec4_t v0, const vec4_t v1 );
static bool LmapWindingHasPoint( const winding_t *w, const vec3_t xyz, float epsilon );

/* TrisLmapWantsLightmap  0x00451590 */
bool TrisLmapWantsLightmap( const TriSurfProps_t *props )
{
    const CoalesceNode_t *entry;

    if ( !props->coalesceChain )
        return ( props->ds->mtlTex->techSetFlags & 2 ) != 0;

    for ( entry = props->coalesceChain; entry; entry = entry->next )
    {
        if ( TrisLmapWantsLightmap( entry->props ) )
            return true;
    }

    return false;
}


/* LmapSurfCoord  0x00453fd0 */
static void LmapSurfCoord( const vec3_t xyz, const vec4_t *vecs, vec2_t lmapCoord )
{
    lmapCoord[0] = Vec3Dot( xyz, vecs[0] ) + vecs[0][3];
    lmapCoord[1] = Vec3Dot( xyz, vecs[1] ) + vecs[1][3];
}


/* ValidateLmapSurf  0x00451290 */
static void ValidateLmapSurf( TriSurf_t *surf )
{
    vec2_t       lmapCoord;
    unsigned int i;

    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_LMAP );
    Assert( surf->transient.lmap );
    Assert( surf->transient.lmap->group );
    Assert( surf->props );
    Assert( !Vec4Compare( surf->transient.lmap->vecs[0], vec4_origin ) );
    Assert( !Vec4Compare( surf->transient.lmap->vecs[1], vec4_origin ) );

    for ( i = 0; i < surf->w->ptCount; i++ )
    {
        lmapCoord[0] = Vec3Dot( surf->w->pts[i], surf->transient.lmap->vecs[0] ) +
                       surf->transient.lmap->vecs[0][3];
        lmapCoord[1] = Vec3Dot( surf->w->pts[i], surf->transient.lmap->vecs[1] ) +
                       surf->transient.lmap->vecs[1][3];

        Assert( PointInBounds2D( lmapCoord, surf->transient.lmap->bounds[0],
                                            surf->transient.lmap->bounds[1] ) );
        Assert( PointInBounds2D( lmapCoord, surf->transient.lmap->group->bounds[0],
                                            surf->transient.lmap->group->bounds[1] ) );
    }
}


/* ValidateLmapGroup  0x004514b0 */
static void ValidateLmapGroup( lmapGroup_t *group )
{
    TriSurf_t *surf;
    TriSurf_t *surfPrev;

    Assert( group );

    surfPrev = NULL;

    for ( surf = group->firstSurf; surf; surf = surf->transient.lmap->nextInGroup )
    {
        Assert( surf->transient.lmap );
        Assert( surf->transient.lmap->group == group );
        Assert( surf->transient.lmap->prevInGroup == surfPrev );
        surfPrev = surf;
    }
}


/* TrisLmapBuildGroups  0x00451600 */
TriSurf_t *TrisLmapBuildGroups( TriSurf_t *listHead, const vec3_t mins, const vec3_t maxs )
{
    TriSurf_t   *byMaterial[MAX_MTL_SORT_INDEX];
    TriSurf_t   *currentList;
    TriSurf_t   *remainderList;
    TriSurf_t  **currentTail;
    TriSurf_t  **remainderTail;
    TriSurf_t  **tail;
    TriSurf_t   *surf;
    TriSurf_t   *surfPrev;
    Material_t  *mtlTex;
    vec3_t       listMins;
    vec3_t       listMaxs;
    unsigned int mtlIndex;
    int          listCount;

    if ( !listHead )
        return NULL;

    memset( byMaterial, 0, sizeof( byMaterial ) );

    do
    {
        ClearBounds( listMins, listMaxs );
        listCount = 0;

        mtlTex = listHead->props->ds->mtlTex;

        currentTail   = &currentList;
        remainderTail = &remainderList;

        for ( surf = listHead; surf; surf = surf->visGroupNext )
        {
            if ( surf->props->ds->mtlTex == mtlTex )
            {
                *currentTail = surf;
                currentTail  = &surf->visGroupNext;
                AddBoundsToBounds( surf->mins, surf->maxs, listMins, listMaxs );
                listCount++;
            }
            else
            {
                *remainderTail = surf;
                remainderTail  = &surf->visGroupNext;
            }
        }

        *currentTail   = NULL;
        *remainderTail = NULL;

        Assert( listCount );
        Assert( currentList );

        currentList = LmapSortSurfList( currentList, listCount );

        surfPrev = NULL;
        for ( surf = currentList; surf; surf = surf->visGroupNext )
        {
            surf->visGroupPrev = surfPrev;
            surfPrev = surf;
        }

        mtlIndex = Material_SortIndex( mtlTex );
        byMaterial[mtlIndex] = currentList;

        SetGridDivisionPoints( listMins, listMaxs );

        for ( surf = currentList; surf; surf = surf->visGroupNext )
        {
            if ( !TrisLmapWantsLightmap( surf->props ) )
                continue;

            AssertCmp( surf->props->lmapIndex, ==, LIGHTMAP_NONE );

            LmapNewGroup( surf );

            trisLmapGlob.surf = surf;
            GridTree_ForEach( surf->mins, surf->maxs, LmapTryMergeWithSurf );
            GridTree_Insert( surf );
            ValidateLmapSurf( surf );
        }

        listHead = remainderList;
    }
    while ( remainderList );

    SetGridDivisionPoints( mins, maxs );
    ClearBounds( listMins, listMaxs );

    tail = &listHead;

    for ( mtlIndex = 0; mtlIndex < MAX_MTL_SORT_INDEX; mtlIndex++ )
    {
        currentList = byMaterial[mtlIndex];
        if ( !currentList )
            continue;

        TrisLmapMergeAcrossLists( currentList, listMins, listMaxs );

        currentList->visGroupPrev = *tail;
        *tail = currentList;

        for ( surf = currentList; surf; surf = surf->visGroupNext )
        {
            AddBoundsToBounds( surf->mins, surf->maxs, listMins, listMaxs );

            if ( TrisLmapWantsLightmap( surf->props ) )
                GridTree_Insert( surf );

            tail = &surf->visGroupNext;
        }
    }

    return listHead;
}


/* LmapNewGroup  0x00451980 */
static void LmapNewGroup( TriSurf_t *surf )
{
    lmapGroup_t *group;
    vec2_t       lmapCoord;
    unsigned int i;

    Assert( surf );
    Assert( surf->props );
    Assert( !Vec4Compare( surf->props->lmapVecs[0], vec4_origin ) );
    Assert( !Vec4Compare( surf->props->lmapVecs[1], vec4_origin ) );
    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_LMAP );

    group = ( lmapGroup_t * )operator new( sizeof( lmapGroup_t ) );
    surf->transient.lmap = ( lmapSurf_t * )operator new( sizeof( lmapSurf_t ) );

    if ( !group || !surf->transient.lmap )
    {
        Com_Error( "Out of memory: couldn't allocate %i bytes for a new lightmap group\n",
                   sizeof( lmapGroup_t ) + sizeof( lmapSurf_t ) );
    }

    group->next = trisLmapGlob.groups;
    if ( group->next )
        group->next->prev = group;
    group->prev = NULL;
    trisLmapGlob.groups = group;

    group->id = trisLmapGlob.nextGroupId;
    trisLmapGlob.nextGroupId++;

    group->mergedIds = NULL;
    group->firstSurf = surf;

    ClearBounds2D( group->bounds[0], group->bounds[1] );

    for ( i = 0; i < surf->w->ptCount; i++ )
    {
        lmapCoord[0] = Vec3Dot( surf->w->pts[i], surf->props->lmapVecs[0] ) +
                       surf->props->lmapVecs[0][3];
        lmapCoord[1] = Vec3Dot( surf->w->pts[i], surf->props->lmapVecs[1] ) +
                       surf->props->lmapVecs[1][3];

        AddPointToBounds2D( lmapCoord, group->bounds[0], group->bounds[1] );
    }

    LmapExpandBounds( LMAP_GROUP_BOUNDS_EXPAND, group->bounds[0] );

    surf->transient.lmap->group       = group;
    surf->transient.lmap->prevInGroup = NULL;
    surf->transient.lmap->nextInGroup = NULL;

    Vec2Copy( group->bounds[0], surf->transient.lmap->bounds[0] );
    Vec2Copy( group->bounds[1], surf->transient.lmap->bounds[1] );
    Vec4Copy( surf->props->lmapVecs[0], surf->transient.lmap->vecs[0] );
    Vec4Copy( surf->props->lmapVecs[1], surf->transient.lmap->vecs[1] );

    ValidateLmapSurf( surf );
}


/* LmapExpandBounds  0x00451c70 */
static void LmapExpandBounds( float expand, float *bounds )
{
    bounds[0] = bounds[0] - expand;
    bounds[1] = bounds[1] - expand;
    bounds[2] = bounds[2] + expand;
    bounds[3] = bounds[3] + expand;
}


/* LmapTryMergeWithSurf  0x00451cb0 */
static void LmapTryMergeWithSurf( TriSurf_t *surf )
{
    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_LMAP );
    Assert( surf );
    Assert( surf->transient.lmap );
    Assert( trisLmapGlob.surf );
    Assert( trisLmapGlob.surf->transient.lmap );

    if ( surf->transient.lmap->group == trisLmapGlob.surf->transient.lmap->group )
        return;

    Assert( surf->transient.lmap->group->id !=
            trisLmapGlob.surf->transient.lmap->group->id );

    if ( LmapGroupsAlreadyTried( surf->transient.lmap->group,
                                 trisLmapGlob.surf->transient.lmap->group ) )
    {
        return;
    }

    if ( trisLmapGlob.surf->props->ds->lmapMaterial != surf->props->ds->lmapMaterial )
        return;

    LmapTryMergeSurfaces( surf, trisLmapGlob.surf );
}


/* LmapGroupsAlreadyTried  0x00451e40 */
static bool LmapGroupsAlreadyTried( const lmapGroup_t *group0, const lmapGroup_t *group1 )
{
    if ( group0->id < group1->id )
        return LmapGroupIdIsSet( group1, group0->id );

    return LmapGroupIdIsSet( group0, group1->id );
}

static bool LmapGroupIdIsSet( const lmapGroup_t *group, unsigned int id )
{
    if ( !group->mergedIds )
        return false;

    return ( group->mergedIds[ id >> 5 ] & ( 1 << ( id & 31 ) ) ) != 0;
}


/* LmapMarkGroupsTried  0x00452a90 */
static void LmapMarkGroupsTried( lmapGroup_t *group0, lmapGroup_t *group1 )
{
    if ( group0->id < group1->id )
        LmapSetGroupId( group1, group0->id );
    else
        LmapSetGroupId( group0, group1->id );
}

static void LmapSetGroupId( lmapGroup_t *group, unsigned int id )
{
    unsigned int dwordCount;

    if ( !group->mergedIds )
    {
        dwordCount = ( group->id + 31 ) >> 5;
        group->mergedIds = ( unsigned int * )operator new[]( dwordCount * sizeof( unsigned int ) );
        memset( group->mergedIds, 0, dwordCount * sizeof( unsigned int ) );
    }

    group->mergedIds[ id >> 5 ] |= 1 << ( id & 31 );
}


/* LmapTryMergeSurfaces  0x00451ed0 */
static void LmapTryMergeSurfaces( TriSurf_t *surf0, TriSurf_t *surf1 )
{
    float dists0[MAX_POINTS_ON_CONCAVE_WINDING / 16];
    float dists1[MAX_POINTS_ON_CONCAVE_WINDING / 16];
    float min0, max0;
    float min1, max1;

    ValidateLmapSurf( surf0 );
    ValidateLmapSurf( surf1 );

    LmapPlaneDists( surf0->w, surf1->props->plane, dists0, &min0, &max0 );

    if ( max0 < -LMAP_PLANE_EPSILON || min0 > LMAP_PLANE_EPSILON )
        return;

    if ( !( max0 > LMAP_PLANE_EPSILON || min0 < -LMAP_PLANE_EPSILON ) )
    {
        LmapMergeCoplanar( surf0, surf1 );
        return;
    }

    LmapPlaneDists( surf1->w, surf0->props->plane, dists1, &min1, &max1 );

    if ( max1 < -LMAP_PLANE_EPSILON || min1 > LMAP_PLANE_EPSILON )
        return;

    if ( !( max1 > LMAP_PLANE_EPSILON || min1 < -LMAP_PLANE_EPSILON ) )
        LmapMergeCoplanar( surf0, surf1 );
    else
        LmapMergeAcrossEdge( surf0, surf1, dists0 );
}


/* LmapPlaneDists  0x00452010 */
static void LmapPlaneDists( const winding_t *w, const vec4_t plane, float *dists,
                            float *outMin, float *outMax )
{
    unsigned int i;

    *outMin =  FLT_MAX;
    *outMax = -FLT_MAX;

    for ( i = 0; i < w->ptCount; i++ )
    {
        dists[i] = Vec3Dot( w->pts[i], plane ) - plane[3];

        if ( dists[i] < *outMin )
            *outMin = dists[i];

        if ( *outMax < dists[i] )
            *outMax = dists[i];
    }
}


/* LmapMergeCoplanar  0x004520d0 */
static void LmapMergeCoplanar( TriSurf_t *surf0, TriSurf_t *surf1 )
{
    vec2_t lmapShift;
    char   haveShift;

    haveShift = 0;

    if ( !LmapFindShift( surf0, surf1, &haveShift, lmapShift ) )
        return;

    if ( haveShift )
        Vec2Negate( lmapShift, lmapShift );

    if ( !LmapFindShift( surf1, surf0, &haveShift, lmapShift ) )
        return;

    if ( !haveShift )
        return;

    if ( !LmapGroupsCanMerge( surf0, surf1, lmapShift ) )
        return;

    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_LMAP );

    LmapMergeGroups( surf0->transient.lmap->group, surf1->transient.lmap->group, lmapShift );
    ValidateLmapSurf( surf0 );
    ValidateLmapSurf( surf1 );
}


/* LmapFindShift  0x004521d0 */
static bool LmapFindShift( TriSurf_t *surf0, TriSurf_t *surf1, char *haveShift,
                           vec2_t lmapShift )
{
    winding_t   *w;
    unsigned int i;

    w = surf0->w;

    for ( i = 0; i < w->ptCount; i++ )
    {
        if ( !PointInBounds( w->pts[i], surf1->mins, surf1->maxs ) )
            continue;

        if ( !LmapWindingHasPoint( surf1->w, w->pts[i], LMAP_POINT_EPSILON ) )
            continue;

        if ( !LmapShiftForPoint( w->pts[i], surf0->transient.lmap->vecs,
                                 surf1->transient.lmap->vecs, *haveShift, lmapShift ) )
        {
            return false;
        }

        *haveShift = 1;
    }

    return true;
}


/* LmapShiftForPoint  0x004522b0 */
static bool LmapShiftForPoint( const vec3_t xyz, const vec4_t *vecs0, const vec4_t *vecs1,
                               char haveShift, vec2_t lmapShift )
{
    vec2_t coord0;
    vec2_t coord1;
    vec2_t shift;
    float  scaled0;
    float  scaled1;

    coord0[0] = Vec3Dot( xyz, vecs0[0] ) + vecs0[0][3];
    coord0[1] = Vec3Dot( xyz, vecs0[1] ) + vecs0[1][3];
    coord1[0] = Vec3Dot( xyz, vecs1[0] ) + vecs1[0][3];
    coord1[1] = Vec3Dot( xyz, vecs1[1] ) + vecs1[1][3];

    Vec2Sub( coord1, coord0, shift );

    if ( !haveShift )
    {
        scaled0 = shift[0] * 512.0f + 0.5f;
        lmapShift[0] = floor( scaled0 ) * LMAP_TEXEL;
        scaled1 = shift[1] * 512.0f + 0.5f;
        lmapShift[1] = floor( scaled1 ) * LMAP_TEXEL;
    }

    return Vec2CompareEpsilon( shift, lmapShift, LMAP_SHIFT_EPSILON );
}


/* LmapWindingHasPoint  0x004523c0 */
static bool LmapWindingHasPoint( const winding_t *w, const vec3_t xyz, float epsilon )
{
    unsigned int i;

    for ( i = 0; i < w->ptCount; i++ )
    {
        if ( Vec3CompareEpsilon( w->pts[i], xyz, epsilon ) )
            return true;
    }

    return false;
}


/* LmapMergeGroups  0x00452420 */
static void LmapMergeGroups( lmapGroup_t *groupGrow, lmapGroup_t *groupKill,
                             const vec2_t lmapShift )
{
    vec2_t     shiftedMins;
    vec2_t     shiftedMaxs;
    vec2_t     mergedBounds[2];
    vec2_t     mergedSize;
    TriSurf_t *surf;
    TriSurf_t *lastSurf;
    int        axis;
    int        mergedTexels;
    int        killTexels;
    int        growTexels;

    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_LMAP );
    Assert( groupGrow );
    Assert( groupKill );
    Assert( groupGrow != groupKill );
    Assert( lmapShift );

    Vec2Add( groupKill->bounds[0], lmapShift, shiftedMins );
    Vec2Add( groupKill->bounds[1], lmapShift, shiftedMaxs );
    Vec2Copy( groupGrow->bounds[0], mergedBounds[0] );
    Vec2Copy( groupGrow->bounds[1], mergedBounds[1] );
    AddBoundsToBounds2D( shiftedMins, shiftedMaxs, mergedBounds[0], mergedBounds[1] );
    Vec2Sub( mergedBounds[1], mergedBounds[0], mergedSize );

    for ( axis = 0; axis < 2; axis++ )
    {
        mergedTexels = LightmapSizeForRange( mergedBounds[0][axis], mergedBounds[1][axis] );

        if ( mergedTexels >= LMAP_WIDTH_MIN )
            return;

        killTexels = LightmapSizeForRange( groupKill->bounds[0][axis], groupKill->bounds[1][axis] );
        growTexels = LightmapSizeForRange( groupGrow->bounds[0][axis], groupGrow->bounds[1][axis] );

        if ( killTexels + 1 + growTexels < mergedTexels )
            return;
    }

    Vec2Copy( shiftedMins, groupKill->bounds[0] );
    Vec2Copy( shiftedMaxs, groupKill->bounds[1] );

    for ( surf = groupKill->firstSurf; surf; surf = surf->transient.lmap->nextInGroup )
    {
        surf->transient.lmap->vecs[0][3] = surf->transient.lmap->vecs[0][3] + lmapShift[0];
        surf->transient.lmap->vecs[1][3] = surf->transient.lmap->vecs[1][3] + lmapShift[1];
        Vec2Add( surf->transient.lmap->bounds[0], lmapShift, surf->transient.lmap->bounds[0] );
        Vec2Add( surf->transient.lmap->bounds[1], lmapShift, surf->transient.lmap->bounds[1] );
        ValidateLmapSurf( surf );
    }

    lastSurf = NULL;
    for ( surf = groupKill->firstSurf; surf; surf = surf->transient.lmap->nextInGroup )
    {
        surf->transient.lmap->group = groupGrow;
        lastSurf = surf;
    }

    SanityCheck( lastSurf );
    SanityCheck( lastSurf->transient.lmap->nextInGroup == NULL );

    Vec2Copy( mergedBounds[0], groupGrow->bounds[0] );
    Vec2Copy( mergedBounds[1], groupGrow->bounds[1] );

    Assert( groupGrow->firstSurf );
    Assert( groupGrow->firstSurf->transient.lmap );
    Assert( groupGrow->firstSurf->transient.lmap->prevInGroup == NULL );

    lastSurf->transient.lmap->nextInGroup = groupGrow->firstSurf;
    lastSurf->transient.lmap->nextInGroup->transient.lmap->prevInGroup = lastSurf;
    groupGrow->firstSurf = groupKill->firstSurf;

    LmapFreeGroup( groupKill );
    ValidateLmapGroup( groupGrow );
}


/* LmapFreeGroup  0x00452850 */
static void LmapFreeGroup( lmapGroup_t *group )
{
    if ( group->next )
        group->next->prev = group->prev;

    if ( !group->prev )
        trisLmapGlob.groups = group->next;
    else
        group->prev->next = group->next;

    if ( group->mergedIds )
        operator delete[]( group->mergedIds );

    if ( group->id == trisLmapGlob.nextGroupId - 1 )
        trisLmapGlob.nextGroupId--;

    delete group;
}


/* LmapGroupsCanMerge  0x004528f0 */
static bool LmapGroupsCanMerge( TriSurf_t *surf0, TriSurf_t *surf1, const vec2_t lmapShift )
{
    vec2_t     shiftedMins;
    vec2_t     shiftedMaxs;
    TriSurf_t *killSurf;
    TriSurf_t *growSurf;

    ValidateLmapGroup( surf0->transient.lmap->group );
    ValidateLmapGroup( surf1->transient.lmap->group );
    LmapMarkGroupsTried( surf0->transient.lmap->group, surf1->transient.lmap->group );

    for ( killSurf = surf1->transient.lmap->group->firstSurf; killSurf;
          killSurf = killSurf->transient.lmap->nextInGroup )
    {
        Vec2Add( killSurf->transient.lmap->bounds[0], lmapShift, shiftedMins );
        Vec2Add( killSurf->transient.lmap->bounds[1], lmapShift, shiftedMaxs );

        for ( growSurf = surf0->transient.lmap->group->firstSurf; growSurf;
              growSurf = growSurf->transient.lmap->nextInGroup )
        {
            if ( Vec3Dot( growSurf->props->plane, killSurf->props->plane ) < LMAP_OPPOSED_DOT )
            {
                if ( I_fabs( growSurf->props->plane[3] + killSurf->props->plane[3] ) <
                     LMAP_PLANE_EPSILON )
                {
                    return false;
                }
            }

            if ( !BoundsOverlap2D( growSurf->transient.lmap->bounds[0],
                                   growSurf->transient.lmap->bounds[1],
                                   shiftedMins, shiftedMaxs ) )
            {
                continue;
            }

            if ( growSurf == surf0 && killSurf == surf1 )
                continue;

            if ( LmapSurfacesOverlap( growSurf, killSurf, lmapShift ) )
                return false;
        }
    }

    return true;
}


/* LmapSurfacesOverlap  0x00452b70 */
static bool LmapSurfacesOverlap( TriSurf_t *surf0, TriSurf_t *surf1, const vec2_t lmapShift )
{
    float pts0[1024][5];
    float pts1[1024][5];
    unsigned int count0;
    unsigned int count1;
    unsigned int i;
    unsigned int j;
    unsigned int prev0;
    unsigned int prev1;

    Assert( surf0 );
    Assert( surf1 );
    Assert( surf0 != surf1 );
    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_LMAP );

    if ( LmapSameChart( surf0, surf1, lmapShift ) )
        return false;

    count0 = surf0->w->ptCount;
    for ( i = 0; i < count0; i++ )
    {
        pts0[i][0] = Vec3Dot( surf0->w->pts[i], surf0->transient.lmap->vecs[0] ) +
                     surf0->transient.lmap->vecs[0][3];
        pts0[i][1] = Vec3Dot( surf0->w->pts[i], surf0->transient.lmap->vecs[1] ) +
                     surf0->transient.lmap->vecs[1][3];
        Vec3Copy( surf0->w->pts[i], &pts0[i][2] );
    }

    count1 = surf1->w->ptCount;
    for ( i = 0; i < count1; i++ )
    {
        pts1[i][0] = Vec3Dot( surf1->w->pts[i], surf1->transient.lmap->vecs[0] ) +
                     surf1->transient.lmap->vecs[0][3] + lmapShift[0];
        pts1[i][1] = Vec3Dot( surf1->w->pts[i], surf1->transient.lmap->vecs[1] ) +
                     surf1->transient.lmap->vecs[1][3] + lmapShift[1];
        Vec3Copy( surf1->w->pts[i], &pts1[i][2] );
    }

    prev1 = count1 - 1;
    for ( j = 0; j < count1; j++ )
    {
        prev0 = count0 - 1;
        for ( i = 0; i < count0; i++ )
        {
            if ( LmapSegmentsCross( pts0[prev0], pts0[i], pts1[prev1], pts1[j] ) )
                return true;

            prev0 = i;
        }
        prev1 = j;
    }

    return false;
}


/* LmapSameChart  0x00452ea0 */
static bool LmapSameChart( TriSurf_t *surf0, TriSurf_t *surf1, const vec2_t lmapShift )
{
    if ( surf0->transient.lmap->vecs[0][3] !=
         surf1->transient.lmap->vecs[0][3] + lmapShift[0] )
    {
        return false;
    }

    if ( surf0->transient.lmap->vecs[1][3] !=
         surf1->transient.lmap->vecs[1][3] + lmapShift[1] )
    {
        return false;
    }

    if ( !Vec4Compare( surf0->transient.lmap->vecs[0], surf1->transient.lmap->vecs[0] ) )
        return false;

    if ( !Vec4Compare( surf0->transient.lmap->vecs[1], surf1->transient.lmap->vecs[1] ) )
        return false;

    return true;
}


/* LmapSegmentsCross  0x00452f40 */
static bool LmapSegmentsCross( const float *a0, const float *a1,
                               const float *b0, const float *b1 )
{
    vec2_t dirA;
    vec2_t dirB;
    vec3_t hitA;
    vec3_t hitB;
    float  denom;
    float  numA;
    float  numB;
    float  tA;
    float  tB;

    Vec2Sub( a1, a0, dirA );
    Vec2Sub( b1, b0, dirB );

    denom = ( b1[1] - b0[1] ) * ( a1[0] - a0[0] ) - ( b1[0] - b0[0] ) * ( a1[1] - a0[1] );
    if ( I_fabs( denom ) <= 0.00001f )
        return false;

    numA = ( b1[0] - b0[0] ) * ( a0[1] - b0[1] ) - ( b1[1] - b0[1] ) * ( a0[0] - b0[0] );
    tA   = numA / denom;
    if ( tA < -0.00001f || tA > 1.00001f )
        return false;

    numB = ( a1[0] - a0[0] ) * ( a0[1] - b0[1] ) - ( a1[1] - a0[1] ) * ( a0[0] - b0[0] );
    tB   = numB / denom;
    if ( tB < -0.00001f || tB > 1.00001f )
        return false;

    Vec3Lerp( a0 + 2, a1 + 2, tA, hitA );
    Vec3Lerp( b0 + 2, b1 + 2, tB, hitB );

    return !Vec3CompareEpsilon( hitA, hitB, LMAP_PLANE_EPSILON );
}


/* LmapMergeAcrossEdge  0x00453100 */
static void LmapMergeAcrossEdge( TriSurf_t *surf0, TriSurf_t *surf1, const float *dists )
{
    vec3_t edgePts[2][1024];
    vec3_t edgeBounds[2][2];
    vec2_t lmapShift;
    int    edgeCount[2];
    int    which;
    int    i;
    char   haveShift;

    if ( !LmapCanShareChart( surf0, surf1 ) )
        return;

    edgeCount[0] = LmapClipToPlane( surf0->w, dists,        edgePts[0], 1024 );
    edgeCount[1] = LmapClipToPlane( surf1->w, dists + 1024, edgePts[1], 1024 );

    for ( which = 0; which < 2; which++ )
    {
        Vec3Copy( edgePts[which][0], edgeBounds[which][0] );
        Vec3Copy( edgePts[which][0], edgeBounds[which][1] );

        for ( i = 1; i < edgeCount[which]; i++ )
            AddPointToBounds( edgePts[which][i], edgeBounds[which][0], edgeBounds[which][1] );
    }

    if ( !BoundsOverlapTolerance( edgeBounds[0][0], edgeBounds[0][1],
                                  edgeBounds[1][0], edgeBounds[1][1],
                                  LMAP_PLANE_EPSILON ) )
        return;

    haveShift = 0;

    for ( which = 0; which < 2; which++ )
    {
        for ( i = 0; i < edgeCount[which]; i++ )
        {
            if ( !LmapShiftForPoint( edgePts[which][i],
                                     surf1->transient.lmap->vecs,
                                     surf0->transient.lmap->vecs,
                                     haveShift, lmapShift ) )
            {
                return;
            }

            haveShift = 1;
        }
    }

    if ( !LmapGroupsCanMerge( surf0, surf1, lmapShift ) )
        return;

    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_LMAP );

    LmapMergeGroups( surf0->transient.lmap->group, surf1->transient.lmap->group, lmapShift );
    ValidateLmapSurf( surf0 );
    ValidateLmapSurf( surf1 );
}


/* LmapClipToPlane  0x00453400 */
static unsigned int LmapClipToPlane( const winding_t *w, const float *dists,
                                     vec3_t *out, unsigned int ptLimit )
{
    int          sides[1025];
    unsigned int ptCount;
    unsigned int i;
    unsigned int next;
    unsigned int axis;

    for ( i = 0; i < w->ptCount; i++ )
    {
        if ( dists[i] < -LMAP_PLANE_EPSILON )
            sides[i] = SIDE_BACK;
        else if ( dists[i] <= LMAP_PLANE_EPSILON )
            sides[i] = SIDE_ON;
        else
            sides[i] = SIDE_FRONT;
    }
    sides[i] = sides[0];

    ptCount = 0;

    for ( i = 0; i < w->ptCount; i++ )
    {
        if ( sides[i] == SIDE_ON )
        {
            SanityCheckIn( ptCount, ptLimit );
            Vec3Copy( w->pts[i], out[ptCount] );
            ptCount++;
        }
        else if ( sides[i + 1] != SIDE_ON && sides[i + 1] != sides[i] )
        {
            next = ( i + 1 ) % w->ptCount;

            SanityCheckIn( ptCount, ptLimit );

            for ( axis = 0; axis < 3; axis++ )
            {
                out[ptCount][axis] = ( dists[i] * w->pts[next][axis] -
                                       dists[next] * w->pts[i][axis] ) /
                                     ( dists[i] - dists[next] );
            }

            ptCount++;
        }
    }

    SanityCheckRange( ptCount, 1, ptLimit );

    return ptCount;
}


/* LmapSortSurfList  0x00453780 */
static TriSurf_t *LmapSortSurfList( TriSurf_t *listHead, unsigned int count )
{
    TriSurf_t  *left;
    TriSurf_t  *right;
    TriSurf_t **tail;
    unsigned int leftCount;
    unsigned int rightCount;
    TriSurf_t   *surf;

    Assertx( count > 0, "(count) = %i", count );

    if ( count == 1 )
        return listHead;

    surf = listHead;
    for ( leftCount = 0; leftCount != count >> 1; leftCount++ )
        surf = surf->visGroupNext;

    rightCount = count - leftCount;

    left  = LmapSortSurfList( listHead, leftCount );
    right = LmapSortSurfList( surf, rightCount );

    tail = &listHead;

    while ( leftCount || rightCount )
    {
        if ( leftCount == 0 || ( rightCount != 0 && !LmapSurfLessThan( left, right ) ) )
        {
            *tail = right;
            right = right->visGroupNext;
            rightCount--;
        }
        else
        {
            *tail = left;
            left = left->visGroupNext;
            leftCount--;
        }

        tail = &( *tail )->visGroupNext;
    }

    *tail = NULL;

    return listHead;
}


/* LmapSurfLessThan  0x004538b0 */
static bool LmapSurfLessThan( const TriSurf_t *surf0, const TriSurf_t *surf1 )
{
    float area0;
    float area1;
    int   i;

    if ( surf0->props->ds->lmapMaterial != surf1->props->ds->lmapMaterial )
    {
        return strcmp( StringFromOffset( surf0->props->ds->lmapMaterial ),
                       StringFromOffset( surf1->props->ds->lmapMaterial ) ) < 0;
    }

    for ( i = 0; i < 3; i++ )
    {
        if ( surf0->mins[i] < surf1->mins[i] )
            return true;
        if ( surf1->mins[i] < surf0->mins[i] )
            return false;
    }

    for ( i = 0; i < 3; i++ )
    {
        if ( surf0->maxs[i] < surf1->maxs[i] )
            return true;
        if ( surf1->maxs[i] < surf0->maxs[i] )
            return false;
    }

    area0 = WindingAreaWithKnownNormal( surf0->w, surf0->props->plane );
    area1 = WindingAreaWithKnownNormal( surf1->w, surf1->props->plane );

    if ( area1 < area0 )
        return true;
    if ( area0 < area1 )
        return false;

    if ( surf0->w->ptCount < surf1->w->ptCount )
        return true;
    if ( surf1->w->ptCount < surf0->w->ptCount )
        return false;

    if ( surf0->props->ds->mapInfoIndex < surf1->props->ds->mapInfoIndex )
        return true;
    if ( surf1->props->ds->mapInfoIndex < surf0->props->ds->mapInfoIndex )
        return false;

    if ( surf0->props->ds->brushNum < surf1->props->ds->brushNum )
        return true;
    if ( surf1->props->ds->brushNum < surf0->props->ds->brushNum )
        return false;

    return surf0 < surf1;
}


/* LmapCanShareChart  0x004536e0 */
static bool LmapCanShareChart( const TriSurf_t *surf0, const TriSurf_t *surf1 )
{
    float dot;

    dot = Vec3Dot( surf0->props->plane, surf1->props->plane );

    if ( dot >= LMAP_COPLANAR_DOT )
        return true;

    if ( dot <= smoothAngle - 0.001f )
        return false;

    if ( surf0->props->ds->smoothing == 0 || surf1->props->ds->smoothing == 0 )
        return false;

    if ( surf0->props->ds->smoothing == 2 && surf1->props->ds->smoothing == 2 )
        return false;

    return true;
}


/* TrisLmapMergeAcrossLists  0x00453b80 */
void TrisLmapMergeAcrossLists( TriSurf_t *listHead, const vec3_t mins, const vec3_t maxs )
{
    TriSurf_t *surf;

    for ( surf = listHead; surf; surf = surf->visGroupNext )
    {
        if ( !TrisLmapWantsLightmap( surf->props ) )
            continue;

        if ( !BoundsOverlap( mins, maxs, surf->mins, surf->maxs ) )
            continue;

        trisLmapGlob.surf = surf;
        GridTree_ForEach( surf->mins, surf->maxs, LmapTryMergeWithSurf );
    }
}

void TrisLmapAddListToGrid( TriSurf_t *listHead )
{
    TriSurf_t *surf;

    for ( surf = listHead; surf; surf = surf->visGroupNext )
    {
        if ( TrisLmapWantsLightmap( surf->props ) )
            GridTree_Insert( surf );
    }
}


/* TrisLmapSplitPropsByGroup  0x00453c60 */
void TrisLmapSplitPropsByGroup( void )
{
    TriSurfProps_t *props;
    TriSurfProps_t *newProps;
    lmapGroup_t    *group;
    int             surfCount;
    int             surfIndex;
    int             next;

    surfCount = Tris_GetSurfCount();
    trisLmapGlob.surfArray = ( TriSurf_t ** )operator new[]( surfCount * sizeof( TriSurf_t * ) );

    trisLmapGlob.surfCountLmap   = 0;
    trisLmapGlob.surfCountNoLmap = 0;
    Tris_ForEachSurf( LmapCollectSurface, NULL );

    AssertCmp( trisLmapGlob.surfCountLmap + trisLmapGlob.surfCountNoLmap, ==, surfCount );

    qsort_vc8( trisLmapGlob.surfArray, trisLmapGlob.surfCountLmap, sizeof( TriSurf_t * ),
           LmapCompareSurfsByPropsAndGroup );

    surfIndex = 0;
    while ( surfIndex < trisLmapGlob.surfCountLmap )
    {
        props = trisLmapGlob.surfArray[surfIndex]->props;

        Assert( trisLmapGlob.surfArray[surfIndex]->transient.lmap );
        group    = trisLmapGlob.surfArray[surfIndex]->transient.lmap->group;
        newProps = props;

        for ( ;; )
        {
            next = surfIndex + 1;
            if ( next >= trisLmapGlob.surfCountLmap )
                break;
            if ( trisLmapGlob.surfArray[next]->props != props )
                break;

            if ( trisLmapGlob.surfArray[next]->transient.lmap->group != group )
            {
                newProps = CopyTriSurfProps( props );
                group    = trisLmapGlob.surfArray[next]->transient.lmap->group;
            }

            trisLmapGlob.surfArray[next]->props = newProps;
            surfIndex = next;
        }

        surfIndex = next;
    }

    operator delete[]( trisLmapGlob.surfArray );
    trisLmapGlob.surfArray = NULL;
}


/* LmapCollectSurface  0x00453e40 */
static void LmapCollectSurface( TriSurf_t *surf, bool isTargetSurf )
{
    if ( !surf->transient.lmap )
    {
        trisLmapGlob.surfCountNoLmap++;
        return;
    }

    trisLmapGlob.surfArray[ trisLmapGlob.surfCountLmap ] = surf;
    trisLmapGlob.surfCountLmap++;
}


/* LmapCompareSurfsByPropsAndGroup  0x00453e80 */
static int LmapCompareSurfsByPropsAndGroup( const void *va, const void *vb )
{
    const TriSurf_t *surf0 = *( const TriSurf_t * const * )va;
    const TriSurf_t *surf1 = *( const TriSurf_t * const * )vb;
    int              order;

    order = ( int )( surf0->props - surf1->props );
    if ( order )
        return order;

    Assert( surf0->transient.lmap );
    Assert( surf1->transient.lmap );

    return ( int )( surf0->transient.lmap->group - surf1->transient.lmap->group );
}


/* LmapTriBounds2D  0x00453f30 */
static void LmapTriBounds2D( const vec4_t *vecs, const vec3_t v0, const vec3_t v1,
                             const vec3_t v2, vec2_t bounds[2] )
{
    vec2_t lmapCoord;

    LmapSurfCoord( v0, vecs, lmapCoord );
    Vec2Copy( lmapCoord, bounds[0] );
    Vec2Copy( lmapCoord, bounds[1] );

    LmapSurfCoord( v1, vecs, lmapCoord );
    AddPointToBounds2D( lmapCoord, bounds[0], bounds[1] );

    LmapSurfCoord( v2, vecs, lmapCoord );
    AddPointToBounds2D( lmapCoord, bounds[0], bounds[1] );
}

void LmapSurfBounds2D( const vec4_t *vecs, const winding_t *w, vec2_t bounds[2] )
{
    vec2_t       lmapCoord;
    unsigned int i;

    ClearBounds2D( bounds[0], bounds[1] );

    for ( i = 0; i < w->ptCount; i++ )
    {
        lmapCoord[0] = Vec3Dot( w->pts[i], vecs[0] ) + vecs[0][3];
        lmapCoord[1] = Vec3Dot( w->pts[i], vecs[1] ) + vecs[1][3];
        AddPointToBounds2D( lmapCoord, bounds[0], bounds[1] );
    }
}


/* TrisLmapAssign  0x004540b0 */
void TrisLmapAssign( void )
{
    TriSurf_t *surf;
    int        mtlIndex;
    int        placedCount;
    int        preferredCount;

    Assert( GetTrisTransientMode() == TRIS_TRANSIENT_LMAP );

    trisLmapGlob.sortedMtlCount = 0;
    memset( trisLmapGlob.sortedMtl, 0, sizeof( trisLmapGlob.sortedMtl ) );

    Tris_ForEachSurf( LmapBucketSurfaceByMaterial, NULL );

    for ( mtlIndex = 0; mtlIndex < trisLmapGlob.sortedMtlCount; mtlIndex++ )
    {
        trisLmapGlob.sortedMtl[mtlIndex] =
            LmapSortMtlChain( trisLmapGlob.sortedMtl[mtlIndex] );
    }

    qsort_vc8( trisLmapGlob.sortedMtl, trisLmapGlob.sortedMtlCount, sizeof( TriSurf_t * ),
           LmapCompareMtlChains );

    trisLmapGlob.span = ( lmapSpan_t * )operator new[]( LMAP_WIDTH_MIN * LMAP_HEIGHT_MIN *
                                                        sizeof( lmapSpan_t ) );
    if ( !trisLmapGlob.span )
        Com_Error( "Out of memory" );

    placedCount    = 0;
    preferredCount = 0;

    for ( mtlIndex = 0; mtlIndex < trisLmapGlob.sortedMtlCount; mtlIndex++ )
    {
        for ( surf = trisLmapGlob.sortedMtl[mtlIndex]; surf;
              surf = surf->transient.lmap->nextInMtl )
        {
            if ( surf->props->lmapIndex == LIGHTMAP_NONE )
            {
                LmapPlaceGroup( surf->transient.lmap->group );
                Assert( surf->props->lmapIndex != LIGHTMAP_NONE );
                placedCount++;
            }

            AddPreferredLightmap( surf->props->lmapIndex );
            Tris_ValidateSurfLmapCoords( surf->w, surf->props );
        }

        preferredCount += FreePreferredLightmaps();
    }

    operator delete[]( trisLmapGlob.span );
    trisLmapGlob.span = NULL;

    LmapFreeAllGroups();

    if ( MapErrorsOccurred() )
        Com_Error( "Aborting due to lightmap errors" );
}


/* LmapPlaceGroup  0x00454340 */
static void LmapPlaceGroup( lmapGroup_t *group )
{
    TriSurf_t   *surf;
    DrawSurf_t  *ds;
    vec2_t       lmapCoord;
    int          w;
    int          h;
    int          lmapIndex;
    int          x;
    int          y;
    int          rotated;
    int          shiftAxis0;
    int          shiftAxis1;
    int          shiftTexels0;
    int          shiftTexels1;
    float        neededScale;
    unsigned int i;

    LmapExpandBounds( -LMAP_GROUP_BOUNDS_EXPAND, group->bounds[0] );

    w = LightmapSizeForRange( group->bounds[0][0], group->bounds[1][0] );
    h = LightmapSizeForRange( group->bounds[0][1], group->bounds[1][1] );

    if ( w > LMAP_WIDTH_MIN || h > LMAP_HEIGHT_MIN )
    {
        surf = group->firstSurf;
        ds   = surf->props->ds;

        neededScale = ( float )( I_fmax( w * LMAP_TEXEL, h * LMAP_TEXEL ) * 10.0 / sampleScale );

        MapErrorWinding( MAPERROR_FATAL, surf->w, ds->mapInfoIndex, ds->entityNum, ds->brushNum,
                         "Lightmap %ix%i is larger than %ix%i; need a sampleScale of at least %.1lf",
                         w, h, LMAP_WIDTH_MIN, LMAP_HEIGHT_MIN,
                         ceil( neededScale ) * 0.1f );
    }

    if ( !AllocLightmapRegion( w, h, &lmapIndex, &x, &y, &rotated ) )
        Com_Error( "lightmap allocation failed... may be out of lightmap memory" );

    shiftAxis0 = ( rotated != 0 );
    shiftAxis1 = ( rotated == 0 );

    shiftTexels0 = x - LightmapCoordFloor( group->bounds[0][shiftAxis0] );
    shiftTexels1 = y - LightmapCoordFloor( group->bounds[0][shiftAxis1] );

    for ( surf = group->firstSurf; surf; surf = surf->transient.lmap->nextInGroup )
    {
        Assertx( surf->props->lmapIndex == LIGHTMAP_NONE ||
                 surf->transient.lmap->group == group,
                 "(surf->props->lmapIndex) = %i", surf->props->lmapIndex );

        surf->transient.lmap->vecs[shiftAxis0][3] =
            surf->transient.lmap->vecs[shiftAxis0][3] + ( float )shiftTexels0 * LMAP_TEXEL;
        surf->transient.lmap->vecs[shiftAxis1][3] =
            surf->transient.lmap->vecs[shiftAxis1][3] + ( float )shiftTexels1 * LMAP_TEXEL;

        surf->props = LmapPropsForLightmap( surf->props, group->id, lmapIndex,
                                            surf->transient.lmap->vecs[shiftAxis0],
                                            surf->transient.lmap->vecs[shiftAxis1],
                                            x, y,
                                            rotated ? h : w,
                                            rotated ? w : h );

        for ( i = 0; i < surf->w->ptCount; i++ )
        {
            lmapCoord[0] = ( Vec3Dot( surf->w->pts[i], surf->props->lmapVecs[0] ) +
                             surf->props->lmapVecs[0][3] ) * 512.0f;
            lmapCoord[1] = ( Vec3Dot( surf->w->pts[i], surf->props->lmapVecs[1] ) +
                             surf->props->lmapVecs[1][3] ) * 512.0f;

            AssertRangeFloat( lmapCoord[0], ( float )x,
                              ( float )( x + ( rotated ? h : w ) ) );
            AssertRangeFloat( lmapCoord[1], ( float )y,
                              ( float )( y + ( rotated ? w : h ) ) );
        }
    }

    if ( w >= LMAP_RECLAIM_SIZE && h >= LMAP_RECLAIM_SIZE )
        LmapReclaimTexels( group, lmapIndex, x, y, w, h, rotated );
}


/* LmapReclaimTexels  0x00454820 */
static void LmapReclaimTexels( lmapGroup_t *group, int lmapIndex, int x, int y,
                               int w, int h, int rotated )
{
    TriSurf_t *surf;
    vec2_t     bounds[2];
    vec2_t     scratch[2];
    byte      *texel;
    int        maxX;
    int        maxY;
    int        px;
    int        py;

    AssertRange( w, LMAP_RECLAIM_SIZE, LMAP_WIDTH_MIN );
    AssertRange( h, LMAP_RECLAIM_SIZE, LMAP_HEIGHT_MIN );

    maxX = x - 1 + ( rotated ? h : w );
    maxY = y - 1 + ( rotated ? w : h );

    for ( py = y; py <= maxY; py++ )
    {
        for ( px = x; px <= maxX; px++ )
        {
            trisLmapGlob.span[ py * LMAP_WIDTH_MIN + px ].w = ( short )( maxX - px ) + 1;
            trisLmapGlob.span[ py * LMAP_WIDTH_MIN + px ].h = ( short )( maxY - py ) + 1;
        }
    }

    trisLmapGlob.spanX = x;
    trisLmapGlob.spanY = y;

    for ( surf = group->firstSurf; surf; surf = surf->transient.lmap->nextInGroup )
    {
        trisLmapGlob.lmapVecs = surf->props->lmapVecs;

        TesselateSurfaceWinding( surf );

        if ( !TesselateWinding( surf, 0, LmapClearSpanForTri ) )
        {
            LmapSurfBounds2D( surf->props->lmapVecs, surf->w, bounds );
            LmapClearSpanForBounds( bounds, scratch );
        }
    }

    if ( debugLightmaps )
    {
        for ( py = y; py <= maxY; py++ )
        {
            for ( px = x; px <= maxX; px++ )
            {
                Assert( ( trisLmapGlob.span[ py * LMAP_WIDTH_MIN + px ].w != 0 ) ==
                        ( trisLmapGlob.span[ py * LMAP_WIDTH_MIN + px ].h != 0 ) );

                if ( trisLmapGlob.span[ py * LMAP_WIDTH_MIN + px ].w == 0 )
                    continue;

                texel = bspLightBytes + lmapIndex * 0x300000 +
                        ( py * LMAP_WIDTH_MIN + px ) * 4;
                SetVertexColor( texel, 0, 0, 0, 0 );

                texel = bspLightBytes + lmapIndex * 0x300000 +
                        ( ( py + LMAP_HEIGHT_MIN ) * LMAP_WIDTH_MIN + px ) * 4;
                SetVertexColor( texel, 0, 0, 0, 0 );
            }
        }
    }

    LmapDonateFreeRects( lmapIndex, x, y, maxX, maxY );
}


/* LmapPropsForLightmap  0x00454f90 */
static TriSurfProps_t *LmapPropsForLightmap( TriSurfProps_t *props, unsigned int groupId,
                                             unsigned int lmapIndex,
                                             const vec4_t vecs0, const vec4_t vecs1,
                                             int x, int y, int blockW, int blockH )
{
    ( void )x;
    ( void )y;
    ( void )blockW;
    ( void )blockH;

    AssertIn( ( int )lmapIndex, LIGHTMAP_NONE );

    if ( LmapPropsNeedsCopy( props, groupId, lmapIndex, vecs0, vecs1 ) )
    {
        props = CopyTriSurfProps( props );
        props->lmapIndex = LIGHTMAP_NONE;
    }

    Assert( props->lmapIndex == LIGHTMAP_NONE || props->lmapIndex == lmapIndex );

    props->lmapGroupId = groupId;
    props->lmapIndex   = lmapIndex;
    Vec4Copy( vecs0, props->lmapVecs[0] );
    Vec4Copy( vecs1, props->lmapVecs[1] );

    return props;
}


/* LmapPropsNeedsCopy  0x00455070 */
static bool LmapPropsNeedsCopy( const TriSurfProps_t *props, unsigned int groupId,
                                unsigned int lmapIndex,
                                const vec4_t vecs0, const vec4_t vecs1 )
{
    if ( props->lmapGroupId == -1 )
        return false;

    if ( props->lmapGroupId != groupId )
        return true;

    if ( props->lmapIndex != LIGHTMAP_NONE && props->lmapIndex != lmapIndex )
        return true;

    if ( !LmapVecsMatch( vecs0, props->lmapVecs[0] ) ||
         !LmapVecsMatch( vecs1, props->lmapVecs[1] ) )
    {
        return true;
    }

    return false;
}


/* LmapVecsMatch  0x004550f0 */
static bool LmapVecsMatch( const vec4_t v0, const vec4_t v1 )
{
    return Vec4CompareEpsilon( v0, v1, LMAP_SHIFT_EPSILON );
}


/* LmapSortMtlChain  0x00455110 */
static TriSurf_t *LmapSortMtlChain( TriSurf_t *listHead )
{
    TriSurf_t *surf;
    int        count;

    count = 0;
    for ( surf = listHead; surf; surf = surf->transient.lmap->nextInMtl )
        count++;

    Assertx( count > 0, "(count) = %i", count );

    return LmapSortMtlChainWorker( listHead, count );
}

static TriSurf_t *LmapSortMtlChainWorker( TriSurf_t *listHead, int count )
{
    TriSurf_t  *left;
    TriSurf_t  *right;
    TriSurf_t **tail;
    int         leftCount;
    int         rightCount;
    int         leftSize;
    int         rightSize;
    TriSurf_t  *surf;

    Assertx( count > 0, "(count) = %i", count );

    if ( count == 1 )
        return listHead;

    surf = listHead;
    for ( leftCount = 0; leftCount != count / 2; leftCount++ )
        surf = surf->transient.lmap->nextInMtl;

    rightCount = count - leftCount;

    left  = LmapSortMtlChainWorker( listHead, leftCount );
    right = LmapSortMtlChainWorker( surf, rightCount );

    tail = &listHead;

    leftSize  = LmapGroupTexelScore( left );
    rightSize = LmapGroupTexelScore( right );

    while ( leftCount || rightCount )
    {
        if ( leftCount == 0 || ( rightCount != 0 && leftSize <= rightSize ) )
        {
            *tail     = right;
            right     = right->transient.lmap->nextInMtl;
            rightCount--;
            rightSize = LmapGroupTexelScore( right );
        }
        else
        {
            *tail     = left;
            left      = left->transient.lmap->nextInMtl;
            leftCount--;
            leftSize  = LmapGroupTexelScore( left );
        }

        tail = &( *tail )->transient.lmap->nextInMtl;
    }

    *tail = NULL;

    return listHead;
}


/* LmapGroupTexelScore  0x004552f0 */
static int LmapGroupTexelScore( const TriSurf_t *surf )
{
    int w;
    int h;

    if ( !surf )
        return 0;

    w = LightmapSizeForRange( surf->transient.lmap->group->bounds[0][0],
                              surf->transient.lmap->group->bounds[1][0] );
    h = LightmapSizeForRange( surf->transient.lmap->group->bounds[0][1],
                              surf->transient.lmap->group->bounds[1][1] );

    return LmapAreaScore( w, h );
}

static int LmapAreaScore( int w, int h )
{
    return w * h + w + h;
}


/* LmapCompareMtlChains  0x00455520 */
static int LmapCompareMtlChains( const void *va, const void *vb )
{
    const TriSurf_t *surf0 = *( const TriSurf_t * const * )va;
    const TriSurf_t *surf1 = *( const TriSurf_t * const * )vb;

    return LmapGroupTexelScore( surf1 ) - LmapGroupTexelScore( surf0 );
}


/* LmapBucketSurfaceByMaterial  0x00455370 */
static void LmapBucketSurfaceByMaterial( TriSurf_t *surf, bool isTargetSurf )
{
    int i;

    Assert( surf );
    Assert( surf->props );

    if ( !TrisLmapWantsLightmap( surf->props ) )
        return;

    Assert( surf->transient.lmap );
    Assert( surf->transient.lmap->group );

    ValidateLmapSurf( surf );

    for ( i = 0; i < trisLmapGlob.sortedMtlCount; i++ )
    {
        if ( trisLmapGlob.sortedMtl[i]->props->ds->mtlTex == surf->props->ds->mtlTex )
        {
            surf->transient.lmap->nextInMtl = trisLmapGlob.sortedMtl[i];
            trisLmapGlob.sortedMtl[i] = surf;
            return;
        }
    }

    if ( i == MAX_LMAP_MATERIALS )
    {
        PrintBSPMaterialList();
        Com_Error( "Map uses more than %i unique materials\n", MAX_LMAP_MATERIALS );
    }

    SanityCheck( i == trisLmapGlob.sortedMtlCount );

    trisLmapGlob.sortedMtlCount++;
    trisLmapGlob.sortedMtl[i] = surf;
    surf->transient.lmap->nextInMtl = NULL;
}


/* LmapFreeAllGroups  0x004542c0 */
static void LmapFreeAllGroups( void )
{
    TriSurf_t *surf;
    TriSurf_t *next;

    while ( trisLmapGlob.groups )
    {
        surf = trisLmapGlob.groups->firstSurf;
        while ( surf )
        {
            next = surf->transient.lmap->nextInGroup;
            delete surf->transient.lmap;
            surf->transient.lmap = NULL;
            surf = next;
        }

        LmapFreeGroup( trisLmapGlob.groups );
    }

    trisLmapGlob.nextGroupId = 0;
}


/* LmapDonateFreeRects  0x00454b20 */
static void LmapDonateFreeRects( int lmapIndex, int minX, int minY, int maxX, int maxY )
{
    int bestW;
    int bestH;
    int bestScore;
    int bestX;
    int bestY;
    int x;
    int y;

    for ( ;; )
    {
        bestW     = 2;
        bestH     = 2;
        bestScore = LmapAreaScore( 2, 2 );
        bestX     = -1;
        bestY     = -1;

        for ( y = minY; y <= maxY; y++ )
        {
            for ( x = minX; x <= maxX; x++ )
            {
                if ( LmapAreaScore( trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].w,
                                    trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].h ) < bestScore )
                {
                    continue;
                }

                if ( LmapBestRectAt( x, y, &bestW, &bestH, &bestScore ) )
                {
                    bestX = x;
                    bestY = y;
                }
            }
        }

        if ( bestX == -1 )
            return;

        LmapClearSpanRect( minX, minY, bestX, bestY,
                           bestX - 1 + bestW, bestY - 1 + bestH );
        AddLightmapBlock( lmapIndex, bestX, bestY, bestW, bestH );
    }
}


/* LmapClearSpanRect  0x00454c60 */
static void LmapClearSpanRect( int minX, int minY, int x0, int y0, int x1, int y1 )
{
    int x;
    int y;

    for ( y = y0; y <= y1; y++ )
    {
        for ( x = x0; x <= x1; x++ )
        {
            trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].w = 0;
            trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].h = 0;
        }

        for ( x = x0 - 1; x >= minX && trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].w > 0; x-- )
            trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].w = ( short )( x0 - x );
    }

    for ( x = x0; x <= x1; x++ )
    {
        for ( y = y0 - 1; y >= minY && trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].h > 0; y-- )
            trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].h = ( short )( y0 - y );
    }
}


/* LmapBestRectAt  0x00454db0 */
static bool LmapBestRectAt( int x, int y, int *bestW, int *bestH, int *bestScore )
{
    int  runWidth;
    int  h;
    int  score;
    bool found;

    found    = false;
    runWidth = 0x7fffffff;

    for ( h = 1; ; h++ )
    {
        if ( trisLmapGlob.span[ y * LMAP_WIDTH_MIN + x ].h < h )
            return found;

        runWidth = I_min( runWidth,
                          trisLmapGlob.span[ ( y - 1 + h ) * LMAP_WIDTH_MIN + x ].w );

        if ( h > 1 )
        {
            if ( runWidth < 2 )
                return found;

            score = LmapAreaScore( runWidth, h );
            if ( *bestScore <= score )
            {
                *bestScore = score;
                *bestW     = runWidth;
                *bestH     = h;
                found      = true;
            }
        }
    }
}


/* LmapClearSpanForBounds  0x00454e70 */
static void LmapClearSpanForBounds( const vec2_t bounds[2], vec2_t scratch[2] )
{
    int x0;
    int y0;
    int x1;
    int y1;

    x0 = I_max( LightmapCoordFloor( bounds[0][0] ), 0 );
    y0 = I_max( LightmapCoordFloor( bounds[0][1] ), 0 );
    x1 = I_min( LightmapCoordCeil( bounds[1][0] ), LMAP_WIDTH_MIN - 1 );
    y1 = I_min( LightmapCoordCeil( bounds[1][1] ), LMAP_HEIGHT_MIN - 1 );

    LmapClearSpanRect( trisLmapGlob.spanX, trisLmapGlob.spanY, x0, y0, x1, y1 );
}

static void LmapClearSpanForTri( const winding_t *w, const winding_t *wOrig, TriSurfProps_t *props,
                                 int i0, int i1, int i2, int visGroupIndex )
{
    vec2_t bounds[2];
    vec2_t scratch[2];

    LmapTriBounds2D( props->lmapVecs, w->pts[i0], w->pts[i1], w->pts[i2], bounds );
    LmapClearSpanForBounds( bounds, scratch );
}
