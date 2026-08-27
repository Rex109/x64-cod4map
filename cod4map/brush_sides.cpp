/* Original: .\brush_sides.cpp */

#include <new>

#include "q_shared.h"
#include "assertive.h"
#include "com_math.h"
#include "com_vector.h"
#include "cmdlib.h"
#include "polylib.h"
#include "bspfile.h"
#include "brush_edges.h"
#include "brush.h"
#include "map.h"
#include "brush_sides.h"
#include "surface.h"
#include "surface.h"

#include "aabbtree.h"

typedef struct
{
    vec4_t         plane;   /* +0x00 */
    windingList_t *list;    /* +0x10 */
    const void    *owner;   /* +0x14 */
} clipContext_t;

typedef struct
{
    byte pad00[0x1c];
    int  entitynum;         /* +0x1c */
    int  brushnum;          /* +0x20 */
    int  mapInfoIndex;      /* +0x24 */
} brushSideOwner_t;

typedef void ( *brushSideCallback_t )( Brush_t *opaqueBrush, void *userData );

brushSideGlob_t brushSideGlob;          /* 0x00a34bb0 */

static void BrushSides_ForEachBrushInBounds( const brushSideNode_t *tree,
                                             const vec3_t mins, const vec3_t maxs,
                                             void *userData, brushSideCallback_t callback );


/* BrushIsOpaque  0x00407cd0 */
static bool BrushIsOpaque( const Brush_t *brush )
{
    return brush->opaque != 0;
}


/* BrushSides_SetNodeBounds  0x00407be0 */
static void BrushSides_SetNodeBounds( brushSideNode_t *node )
{
    unsigned int i;

    ClearBounds( node->mins, node->maxs );

    if ( node->childCount == 0 )
    {
        for ( i = 0; i < ( unsigned int )node->brushCount; i++ )
        {
            AddBoundsToBounds( node->brushes[i]->mins, node->brushes[i]->maxs,
                               node->mins, node->maxs );
        }
    }
    else
    {
        for ( i = 0; i < ( unsigned int )node->childCount; i++ )
        {
            BrushSides_SetNodeBounds( &node->children[i] );
            AddBoundsToBounds( node->children[i].mins, node->children[i].maxs,
                               node->mins, node->maxs );
        }
    }
}


/* BrushSides_BeginEntity  0x00407820 */
void BrushSides_BeginEntity( Entity_t *entity )
{
    AabbTreeBuilder_t      build;
    int                    opaqueBrushCount;
    int                    brushIndex;
    unsigned int           nodeCount;
    unsigned int           i;
    Brush_t               *b;
    float                 *mins;
    float                 *maxs;
    const AabbTreeNode_t  *nodes;

    Assert( !brushSideGlob.isTreeInitialized );
    brushSideGlob.isTreeInitialized = true;

    opaqueBrushCount = 0;
    for ( b = entity->brushes; b; b = b->next )
    {
        if ( BrushIsOpaque( b ) )
            opaqueBrushCount++;
    }

    if ( !opaqueBrushCount )
    {
        brushSideGlob.hasTree = false;
        return;
    }

    brushSideGlob.opaqueBrushes = new( std::nothrow ) Brush_t *[opaqueBrushCount];

    build.itemData      = brushSideGlob.opaqueBrushes;
    build.itemCount     = opaqueBrushCount;
    build.itemStride    = sizeof( Brush_t * );
    build.reorderBounds = 0;
    mins = new( std::nothrow ) float[build.itemCount * 3];
    build.itemMins = ( vec3_t * )mins;
    maxs = new( std::nothrow ) float[build.itemCount * 3];
    build.itemMaxs = ( vec3_t * )maxs;
    build.maxNodes = ( build.itemCount - 1 ) / 2 + 1;
    build.nodes = new( std::nothrow ) AabbTreeNode_t[build.maxNodes];
    build.minPartitionSize = 4;
    build.minLeafItems     = 8;

    if ( !brushSideGlob.opaqueBrushes || !build.itemMins || !build.itemMaxs || !build.nodes )
        Com_Error( "Out of memory" );

    brushIndex = 0;
    for ( b = entity->brushes; b; b = b->next )
    {
        if ( BrushIsOpaque( b ) )
        {
            brushSideGlob.opaqueBrushes[brushIndex] = b;
            Vec3Copy( b->mins, &mins[brushIndex * 3] );
            Vec3Copy( b->maxs, &maxs[brushIndex * 3] );
            brushIndex++;
        }
    }
    SanityCheckCmp( brushIndex, ==, opaqueBrushCount );

    nodeCount = AabbBuildTree( &build );
    delete[] mins;
    delete[] maxs;

    brushSideGlob.tree = new( std::nothrow ) brushSideNode_t[nodeCount];
    if ( !brushSideGlob.tree )
        Com_Error( "Out of memory" );

    nodes = ( const AabbTreeNode_t * )build.nodes;
    for ( i = 0; i < nodeCount; i++ )
    {
        brushSideGlob.tree[i].brushes    = &brushSideGlob.opaqueBrushes[nodes[i].firstItem];
        brushSideGlob.tree[i].brushCount = nodes[i].itemCount;
        brushSideGlob.tree[i].childCount = nodes[i].childCount;
        if ( nodes[i].childCount == 0 )
            brushSideGlob.tree[i].children = NULL;
        else
            brushSideGlob.tree[i].children = &brushSideGlob.tree[nodes[i].firstChild];
    }

    BrushSides_SetNodeBounds( brushSideGlob.tree );
    brushSideGlob.hasTree = true;
}


/* BrushSides_Shutdown  0x00407cf0 */
void BrushSides_Shutdown( void )
{
    Assert( brushSideGlob.isTreeInitialized );
    brushSideGlob.isTreeInitialized = false;

    if ( brushSideGlob.hasTree )
    {
        delete[] brushSideGlob.opaqueBrushes;
        delete[] brushSideGlob.tree;
    }
}


/* CopyBrushSideWindings  0x00407d70 */
static void CopyBrushSideWindings( Entity_t *entity )
{
    Brush_t     *b;
    unsigned int sideIndex;

    for ( b = entity->brushes; b; b = b->next )
    {
        for ( sideIndex = 0; sideIndex < b->sideCount; sideIndex++ )
        {
            if ( !b->sides[sideIndex].winding )
                b->sides[sideIndex].visibleHull = NULL;
            else
                b->sides[sideIndex].visibleHull = CopyWinding( b->sides[sideIndex].winding );
        }
    }
}


/* ClipFragmentAgainstBrush  0x004080f0 */
static windingList_t **ClipFragmentAgainstBrush( windingList_t **list, const vec3_t normal,
                                                 const Brush_t *opaqueBrush )
{
    const plane_t *splitPlanes[MAX_BRUSH_SIDES];
    int            splitPlaneCount;
    int            splitIndex;
    unsigned int   sideIndex;
    const plane_t *plane;
    windingList_t *node;
    windingList_t *newNode;
    winding_t     *front;
    winding_t     *back;
    int            side;

    Assert( opaqueBrush->sideCount <= MAX_BRUSH_SIDES );

    node = *list;
    splitPlaneCount = 0;

    for ( sideIndex = 0; sideIndex < opaqueBrush->sideCount; sideIndex++ )
    {
        plane = &mapplanes[opaqueBrush->sides[sideIndex].planenum];
        side = WindingPlaneSide( node->w, plane->normal, plane->dist );

        if ( side == SIDE_CROSS )
        {
            splitPlanes[splitPlaneCount] = plane;
            splitPlaneCount++;
        }
        else if ( side == SIDE_FRONT
               || ( side == SIDE_ON && Vec3Dot( plane->normal, normal ) >= 0.0f ) )
        {
            return &node->next;
        }
    }

    brushSideGlob.clipCounter++;
    if ( brushSideGlob.clipCounter == 0x9ef )
        return &node->next;

    for ( splitIndex = splitPlaneCount; splitIndex != 0; )
    {
        splitIndex--;
        side = ClipWindingEpsilon_Internal( node->w, splitPlanes[splitIndex]->normal,
                                            splitPlanes[splitIndex]->dist, 0.1f,
                                            &front, &back );
        if ( side == 1 )
            continue;
        if ( side != 3 )
            return &node->next;

        FreeWinding( node->w );
        node->w = back;
        newNode = AllocWindingList();
        newNode->w = front;
        newNode->next = node;
        *list = newNode;
        list = &newNode->next;
    }

    *list = node->next;
    FreeWinding( node->w );
    FreeWindingList( node );
    return list;
}


/* ClipBrushSidesAgainstOneOpaqueBrush  0x00408010 */
static void ClipBrushSidesAgainstOneOpaqueBrush( Brush_t *opaqueBrush, void *userData )
{
    Brush_t        *brush;
    unsigned int    sideIndex;
    int             planenum;
    windingList_t **list;

    brush = ( Brush_t * )userData;

    Assert( opaqueBrush->opaque );

    if ( opaqueBrush == brush )
        return;
    if ( opaqueBrush->detail && ( !brush->detail || brush->forceVisible ) )
        return;

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        planenum = brush->sides[sideIndex].planenum;
        for ( list = ( windingList_t ** )&brush->sides[sideIndex].visibleHull;
              *list;
              list = ClipFragmentAgainstBrush( list, mapplanes[planenum].normal, opaqueBrush ) )
        {
        }
    }
}


/* ClipBrushSidesAgainstOpaqueBrushes  0x00407e70 */
static void ClipBrushSidesAgainstOpaqueBrushes( Brush_t *brush )
{
    vec3_t         mins = { 0.0f, 0.0f, 0.0f };
    vec3_t         maxs = { 0.0f, 0.0f, 0.0f };
    unsigned int   sideIndex;
    side_t        *side;
    windingList_t *node;
    winding_t     *hull;

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        if ( !brush->sides[sideIndex].winding )
        {
            brush->sides[sideIndex].visibleHull = NULL;
        }
        else
        {
            brush->sides[sideIndex].visibleHull = AllocWindingList();
            ( ( windingList_t * )brush->sides[sideIndex].visibleHull )->w =
                CopyWinding( brush->sides[sideIndex].winding );
        }
    }

    BrushSides_ForEachBrushInBounds( brushSideGlob.tree, mins, maxs, brush,
                                     ClipBrushSidesAgainstOneOpaqueBrush );

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        side = &brush->sides[sideIndex];
        if ( !side->visibleHull )
        {
            side->visibleHull = NULL;
        }
        else if ( !( ( windingList_t * )side->visibleHull )->next )
        {
            hull = ( ( windingList_t * )side->visibleHull )->w;
            FreeWindingList( ( windingList_t * )side->visibleHull );
            side->visibleHull = hull;
        }
        else
        {
            hull = WindingForConvexHullList( mapplanes[side->planenum].normal,
                                             ( windingList_t * )side->visibleHull );
            while ( side->visibleHull )
            {
                node = ( windingList_t * )side->visibleHull;
                side->visibleHull = node->next;
                FreeWinding( node->w );
                FreeWindingList( node );
            }
            side->visibleHull = hull;
        }
    }
}


/* BrushSides_BuildTree  0x00407df0 */
void BrushSides_BuildTree( Entity_t *entity )
{
    Brush_t *b;

    Assert( brushSideGlob.isTreeInitialized );

    if ( !brushSideGlob.hasTree )
    {
        CopyBrushSideWindings( entity );
    }
    else
    {
        for ( b = entity->brushes; b; b = b->next )
            ClipBrushSidesAgainstOpaqueBrushes( b );
    }
}


/* BrushSides_RecurseBounds  0x00408410 */
static void BrushSides_RecurseBounds( const brushSideNode_t *node,
                                      const vec3_t mins, const vec3_t maxs,
                                      void *userData, brushSideCallback_t callback )
{
    unsigned int i;

    if ( node->childCount == 0 )
    {
        for ( i = 0; i < ( unsigned int )node->brushCount; i++ )
        {
            if ( BoundsOverlap( node->brushes[i]->mins, node->brushes[i]->maxs, mins, maxs ) )
                callback( node->brushes[i], userData );
        }
    }
    else
    {
        for ( i = 0; i < ( unsigned int )node->childCount; i++ )
        {
            if ( BoundsOverlap( node->children[i].mins, node->children[i].maxs, mins, maxs ) )
                BrushSides_RecurseBounds( &node->children[i], mins, maxs, userData, callback );
        }
    }
}


/* BrushSides_ForEachBrushInBounds  0x00408350 */
static void BrushSides_ForEachBrushInBounds( const brushSideNode_t *tree,
                                             const vec3_t mins, const vec3_t maxs,
                                             void *userData, brushSideCallback_t callback )
{
    vec3_t expandedMins;
    vec3_t expandedMaxs;

    Vec3Set( expandedMins, mins[0] - 0.1f, mins[1] - 0.1f, mins[2] - 0.1f );
    Vec3Set( expandedMaxs, maxs[0] + 0.1f, maxs[1] + 0.1f, maxs[2] + 0.1f );
    BrushSides_RecurseBounds( tree, expandedMins, expandedMaxs, userData, callback );
}


/* BrushSides_EndEntity  0x00408510 */
void BrushSides_EndEntity( Entity_t *entity )
{
    Brush_t     *b;
    unsigned int sideIndex;
    side_t      *side;

    for ( b = entity->brushes; b; b = b->next )
    {
        for ( sideIndex = 0; sideIndex < b->sideCount; sideIndex++ )
        {
            side = &b->sides[sideIndex];
            if ( side->visibleHull )
            {
                DrawSurfaceForSide( side, ( const winding_t * )side->visibleHull,
                                    b->mapInfoIndex, b->entitynum, b->brushnum, b->cullGroup );
            }
        }
    }
}


/* ClipFragmentsAgainstBrush  0x004086a0 */
static void ClipFragmentsAgainstBrush( Brush_t *opaqueBrush, void *userData )
{
    clipContext_t          *ctx;
    const brushSideOwner_t *owner;
    windingList_t         **list;

    ctx = ( clipContext_t * )userData;
    owner = ( const brushSideOwner_t * )ctx->owner;

    Assert( opaqueBrush->opaque );

    if ( !owner
      || opaqueBrush->mapInfoIndex != owner->mapInfoIndex
      || opaqueBrush->brushnum != owner->brushnum
      || opaqueBrush->entitynum != owner->entitynum )
    {
        for ( list = &ctx->list; *list; list = ClipFragmentAgainstBrush( list, ctx->plane, opaqueBrush ) )
        {
        }
    }
}


/* ClipWindingAgainstOpaqueBrushes  0x004085a0 */
static windingList_t *ClipWindingAgainstOpaqueBrushes( winding_t *w, const void *owner )
{
    vec3_t        mins;
    vec3_t        maxs;
    clipContext_t ctx;
    unsigned int  i;

    Assert( brushSideGlob.isTreeInitialized );

    ctx.list = AllocWindingList();
    ctx.list->w = w;

    if ( brushSideGlob.hasTree )
    {
        Vec3Copy( w->pts[0], mins );
        Vec3Copy( w->pts[0], maxs );
        for ( i = 0; i < w->ptCount; i++ )
            AddPointToBounds( w->pts[i], mins, maxs );

        PlaneFromWinding( w, ctx.plane );
        ctx.owner = owner;
        BrushSides_ForEachBrushInBounds( brushSideGlob.tree, mins, maxs, &ctx,
                                         ClipFragmentsAgainstBrush );
    }
    return ctx.list;
}


/* BrushSides_ClipWinding  0x00408750 */
winding_t *BrushSides_ClipWinding( winding_t *w, const void *owner, const vec3_t normal )
{
    windingList_t *list;
    windingList_t *next;

    list = ClipWindingAgainstOpaqueBrushes( w, owner );
    if ( !list )
        return NULL;

    if ( !list->next )
    {
        w = list->w;
        FreeWindingList( list );
        return w;
    }

    w = WindingForConvexHullList( normal, list );
    while ( list )
    {
        next = list->next;
        FreeWinding( list->w );
        FreeWindingList( list );
        list = next;
    }
    return w;
}


/* BrushSides_IsWindingVisible  0x004087f0 */
qboolean BrushSides_IsWindingVisible( winding_t *w )
{
    windingList_t *list;
    windingList_t *next;

    list = ClipWindingAgainstOpaqueBrushes( w, NULL );
    if ( !list )
        return qfalse;

    while ( list )
    {
        next = list->next;
        FreeWinding( list->w );
        FreeWindingList( list );
        list = next;
    }
    return qtrue;
}
