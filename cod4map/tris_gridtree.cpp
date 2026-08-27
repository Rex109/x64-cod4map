/* Original: .\tris_gridtree.cpp */

#include "tris_gridtree.h"
#include "polylib.h"
#include "com_math.h"
#include "com_vector.h"
#include "assertive.h"

GridTreeGlob_t gridTreeGlob;                    /* 0x2b80e314 */

static const int s_splitAxis[WINDING_TREE_LEAF_DEPTH + 1] =
{
    0, 1, 0, 1, 0, 1, 2,
    0, 1, 0, 1, 0, 1, 2,
    0, 1, 0, 1, 0, 1, 2
};


/* GridTree_ChildIndex  0x00450400 */
int GridTree_ChildIndex( int nodeIndex, int side )
{
    Assert( side == SIDE_FRONT || side == SIDE_BACK );
    return nodeIndex * 2 + side + 1;
}


/* GridTree_ClassifyBounds  0x00450560 */
int GridTree_ClassifyBounds( const vec3_t mins, const vec3_t maxs, int nodeIndex, int depth )
{
    int             axis;
    GridTreeNode_t *node;

    Assert( depth >= 0 && depth < WINDING_TREE_LEAF_DEPTH );

    axis = s_splitAxis[depth];
    node = &gridTreeGlob.root[nodeIndex];

    if ( maxs[axis] <= node->frontMin )
        return SIDE_BACK;
    if ( mins[axis] < node->backMax )
        return SIDE_CROSS;
    return SIDE_FRONT;
}


/* GridTree_FindNode  0x00450480 */
GridTreeNode_t *GridTree_FindNode( const vec3_t mins, const vec3_t maxs )
{
    int          nodeIndex;
    unsigned int depth;
    int          side;

    Assert( mins );
    Assert( maxs );
    Assert( gridTreeGlob.root );

    nodeIndex = 0;
    for ( depth = 0; depth < WINDING_TREE_LEAF_DEPTH; depth++ )
    {
        side = GridTree_ClassifyBounds( mins, maxs, nodeIndex, depth );
        if ( side == SIDE_CROSS )
            break;
        nodeIndex = GridTree_ChildIndex( nodeIndex, side );
    }
    return &gridTreeGlob.root[nodeIndex];
}


/* GridTree_Insert  0x00450440 */
void GridTree_Insert( TriSurf_t *entry )
{
    GridTreeNode_t *node;

    node = GridTree_FindNode( entry->mins, entry->maxs );
    GridTree_LinkEntry( node, entry );
}


/* GridTree_LinkEntry  0x004505f0 */
void GridTree_LinkEntry( GridTreeNode_t *node, TriSurf_t *entry )
{
    Assert( node );

    entry->gridTreePrev = NULL;
    entry->gridTreeNext = node->list;
    if ( entry->gridTreeNext )
        entry->gridTreeNext->gridTreePrev = entry;
    node->list = entry;
}


/* GridTree_UnlinkEntry  0x00450a00 */
void GridTree_UnlinkEntry( GridTreeNode_t *node, TriSurf_t *entry )
{
    Assert( node );

    if ( entry->gridTreeNext )
        entry->gridTreeNext->gridTreePrev = entry->gridTreePrev;

    if ( entry->gridTreePrev )
    {
        Assert( node->list != entry );
        entry->gridTreePrev->gridTreeNext = entry->gridTreeNext;
    }
    else
    {
        Assert( node->list == entry );
        node->list = entry->gridTreeNext;
        Assert( node->list == NULL || node->list->gridTreePrev == NULL );
    }

    entry->gridTreePrev = NULL;
    entry->gridTreeNext = NULL;
}


/* GridTree_Remove  0x00450650 */
void GridTree_Remove( TriSurf_t *entry )
{
    GridTreeNode_t *node;

    if ( !gridTreeGlob.root )
        return;

    if ( entry->gridTreeNext )
        entry->gridTreeNext->gridTreePrev = entry->gridTreePrev;

    if ( entry->gridTreePrev )
    {
        entry->gridTreePrev->gridTreeNext = entry->gridTreeNext;
    }
    else
    {
        node = GridTree_FindNode( entry->mins, entry->maxs );
        Assert( node );
        if ( node->list == entry )
        {
            node->list = entry->gridTreeNext;
            Assert( node->list == NULL || node->list->gridTreePrev == NULL );
        }
    }

    entry->gridTreePrev = NULL;
    entry->gridTreeNext = NULL;
}


/* SetGridDivisionPoints_r  0x00450310 */
void SetGridDivisionPoints_r( int nodeIndex, const vec3_t mins, const vec3_t maxs, int depth )
{
    int             axis;
    GridTreeNode_t *node;
    vec3_t          childMins;
    vec3_t          childMaxs;

    axis = s_splitAxis[depth];
    node = &gridTreeGlob.root[nodeIndex];

    node->backMax  = ( maxs[axis] + mins[axis] ) * 0.5f;
    node->frontMin = node->backMax;
    node->list     = NULL;

    if ( depth == WINDING_TREE_LEAF_DEPTH )
        return;

    Vec3Copy( maxs, childMaxs );
    childMaxs[axis] = node->backMax;
    Vec3Copy( mins, childMins );
    childMins[axis] = node->frontMin;

    SetGridDivisionPoints_r( GridTree_ChildIndex( nodeIndex, SIDE_BACK  ), mins,      childMaxs, depth + 1 );
    SetGridDivisionPoints_r( GridTree_ChildIndex( nodeIndex, SIDE_FRONT ), childMins, maxs,      depth + 1 );
}


/* SetGridDivisionPoints  0x004502a0 */
void SetGridDivisionPoints( const vec3_t mins, const vec3_t maxs )
{
    Assert( gridTreeGlob.root );

    Vec3Copy( mins, gridTreeGlob.mins );
    Vec3Copy( maxs, gridTreeGlob.maxs );
    SetGridDivisionPoints_r( 0, mins, maxs, 0 );
}


/* GridTree_MoveAllToRoot  0x00450b10 */
void GridTree_MoveAllToRoot( void )
{
    int        nodeIndex;
    TriSurf_t *list;
    TriSurf_t *tail;

    for ( nodeIndex = 1; nodeIndex < WINDING_TREE_NODE_COUNT; nodeIndex++ )
    {
        GridTreeNode_t *node = &gridTreeGlob.root[nodeIndex];

        list = node->list;
        if ( !list )
            continue;

        node->list = NULL;

        for ( tail = list; tail->gridTreeNext; tail = tail->gridTreeNext )
            ;

        tail->gridTreeNext = gridTreeGlob.root->list;
        if ( tail->gridTreeNext )
            tail->gridTreeNext->gridTreePrev = tail;
        gridTreeGlob.root->list = list;
    }

    Assert( gridTreeGlob.root->list == NULL ||
            gridTreeGlob.root->list->gridTreePrev == NULL );
}


/* SortGridTree_r  0x00450760 */
void SortGridTree_r( int nodeIndex, const vec3_t mins, const vec3_t maxs, int depth )
{
    GridTreeNode_t *node;
    GridTreeNode_t *back;
    GridTreeNode_t *front;
    TriSurf_t      *entry;
    TriSurf_t      *next;
    int             axis;
    float           mid;
    float           looseness;
    float           dmin;
    float           dmax;
    int             frontCount;
    int             backCount;
    int             crossCount;
    vec3_t          childMins;
    vec3_t          childMaxs;

    node = &gridTreeGlob.root[nodeIndex];
    if ( !node->list )
        return;

    back  = &gridTreeGlob.root[GridTree_ChildIndex( nodeIndex, SIDE_BACK  )];
    front = &gridTreeGlob.root[GridTree_ChildIndex( nodeIndex, SIDE_FRONT )];

    axis      = s_splitAxis[depth];
    mid       = ( maxs[axis] + mins[axis] ) * 0.5f;
    looseness = ( maxs[axis] - mins[axis] ) * ( depth * 0.0078947367f + 0.1f );

    node->frontMin = mid;
    node->backMax  = mid;

    frontCount = 0;
    backCount  = 0;
    crossCount = 0;

    for ( entry = node->list; entry; entry = next )
    {
        next = entry->gridTreeNext;

        dmin = entry->mins[axis] - mid;
        dmax = entry->maxs[axis] - mid;

        if ( dmax > -dmin )
        {
            if ( -dmin >= looseness )
            {
                crossCount++;
                continue;
            }
            GridTree_UnlinkEntry( node, entry );
            GridTree_LinkEntry( front, entry );
            if ( entry->mins[axis] < node->frontMin )
                node->frontMin = entry->mins[axis];
            frontCount++;
        }
        else
        {
            if ( dmax >= looseness )
            {
                crossCount++;
                continue;
            }
            GridTree_UnlinkEntry( node, entry );
            GridTree_LinkEntry( back, entry );
            if ( node->backMax < entry->maxs[axis] )
                node->backMax = entry->maxs[axis];
            backCount++;
        }
    }

    frontCount = WINDING_TREE_NODE_COUNT;

    Vec3Copy( maxs, childMaxs );
    childMaxs[axis] = node->backMax;
    Vec3Copy( mins, childMins );
    childMins[axis] = node->frontMin;

    if ( depth + 1 == WINDING_TREE_LEAF_DEPTH )
        return;

    SortGridTree_r( GridTree_ChildIndex( nodeIndex, SIDE_BACK  ), mins,      childMaxs, depth + 1 );
    SortGridTree_r( GridTree_ChildIndex( nodeIndex, SIDE_FRONT ), childMins, maxs,      depth + 1 );
}


/* SortGridTree  0x00450740 */
void SortGridTree( void )
{
    GridTree_MoveAllToRoot();
    SortGridTree_r( 0, gridTreeGlob.mins, gridTreeGlob.maxs, 0 );
}


/* GridTree_ForEach_r  0x00450c10 */
void GridTree_ForEach_r( const vec3_t mins, const vec3_t maxs, int nodeIndex,
                         int depth, void (*callback)( TriSurf_t * ) )
{
    GridTreeNode_t *node;
    TriSurf_t      *entry;
    TriSurf_t      *next;
    int             axis;

    node = &gridTreeGlob.root[nodeIndex];

    for ( entry = node->list; entry; entry = next )
    {
        next = entry->gridTreeNext;
        if ( BoundsOverlap( entry->mins, entry->maxs, mins, maxs ) )
            callback( entry );
    }

    if ( depth == WINDING_TREE_LEAF_DEPTH )
        return;

    axis = s_splitAxis[depth];

    if ( mins[axis] <= node->backMax )
        GridTree_ForEach_r( mins, maxs, GridTree_ChildIndex( nodeIndex, SIDE_BACK ), depth + 1, callback );

    if ( node->frontMin <= maxs[axis] )
        GridTree_ForEach_r( mins, maxs, GridTree_ChildIndex( nodeIndex, SIDE_FRONT ), depth + 1, callback );
}


/* GridTree_ForEach  0x00450bf0 */
void GridTree_ForEach( const vec3_t mins, const vec3_t maxs, void (*callback)( TriSurf_t * ) )
{
    GridTree_ForEach_r( mins, maxs, 0, 0, callback );
}


/* GridTree_CountOverlapping_r  0x00451050 */
int GridTree_CountOverlapping_r( const vec3_t mins, const vec3_t maxs, int nodeIndex,
                                 int depth, int maxCount )
{
    GridTreeNode_t *node;
    TriSurf_t      *entry;
    int             count;
    int             axis;

    count = 0;
    node  = &gridTreeGlob.root[nodeIndex];

    for ( entry = node->list; entry; entry = entry->gridTreeNext )
    {
        if ( BoundsOverlap( entry->mins, entry->maxs, mins, maxs ) )
        {
            count++;
            if ( count > maxCount )
                return count;
        }
    }

    if ( depth == WINDING_TREE_LEAF_DEPTH )
        return count;

    axis = s_splitAxis[depth];

    if ( mins[axis] <= node->backMax )
    {
        count += GridTree_CountOverlapping_r( mins, maxs, GridTree_ChildIndex( nodeIndex, SIDE_BACK ),
                                              depth + 1, maxCount - count );
        if ( count > maxCount )
            return count;
    }

    if ( node->frontMin <= maxs[axis] )
        count += GridTree_CountOverlapping_r( mins, maxs, GridTree_ChildIndex( nodeIndex, SIDE_FRONT ),
                                              depth + 1, maxCount - count );

    return count;
}


/* GridTree_CountOverlapping  0x00451030 */
int GridTree_CountOverlapping( const vec3_t mins, const vec3_t maxs, int maxCount )
{
    return GridTree_CountOverlapping_r( mins, maxs, 0, 0, maxCount );
}


/* GridTree_TraceLine_r  0x00450d50 */
qboolean GridTree_TraceLine_r( const vec3_t traceStart, const vec3_t traceEnd,
                               const vec3_t segStart, const vec3_t segEnd,
                               int nodeIndex, int depth, void *userData,
                               qboolean (*callback)( TriSurf_t *, const vec3_t,
                                                     const vec3_t, void * ) )
{
    GridTreeNode_t *node;
    TriSurf_t      *entry;
    TriSurf_t      *next;
    int             axis;
    float           frac;
    vec3_t          mins;
    vec3_t          maxs;
    vec3_t          backEnd;
    vec3_t          frontStart;

    Vec3Copy( segStart, mins );
    Vec3Copy( segStart, maxs );
    AddPointToBounds( segEnd, mins, maxs );

    for ( ;; )
    {
        node = &gridTreeGlob.root[nodeIndex];

        for ( entry = node->list; entry; entry = next )
        {
            next = entry->gridTreeNext;
            if ( !BoundsOverlap( entry->mins, entry->maxs, mins, maxs ) )
                continue;
            if ( !callback( entry, traceStart, traceEnd, userData ) )
                return qfalse;
        }

        if ( depth == WINDING_TREE_LEAF_DEPTH )
            return qtrue;

        axis = s_splitAxis[depth];
        depth++;

        if ( node->frontMin >= maxs[axis] )
        {
            nodeIndex = GridTree_ChildIndex( nodeIndex, SIDE_BACK );
            continue;
        }
        if ( node->backMax <= mins[axis] )
        {
            nodeIndex = GridTree_ChildIndex( nodeIndex, SIDE_FRONT );
            continue;
        }

        frac = ( node->backMax - traceStart[axis] ) / ( traceEnd[axis] - traceStart[axis] );
        Vec3Lerp( traceStart, traceEnd, frac, backEnd );
        backEnd[axis] = node->backMax;

        frac = ( node->frontMin - traceStart[axis] ) / ( traceEnd[axis] - traceStart[axis] );
        Vec3Lerp( traceStart, traceEnd, frac, frontStart );
        frontStart[axis] = node->frontMin;

        if ( traceEnd[axis] > traceStart[axis] )
        {
            if ( !GridTree_TraceLine_r( traceStart, traceEnd, segStart, backEnd,
                                        GridTree_ChildIndex( nodeIndex, SIDE_BACK ),
                                        depth, userData, callback ) )
                return qfalse;
            return GridTree_TraceLine_r( traceStart, traceEnd, frontStart, segEnd,
                                         GridTree_ChildIndex( nodeIndex, SIDE_FRONT ),
                                         depth, userData, callback );
        }

        if ( !GridTree_TraceLine_r( traceStart, traceEnd, segStart, frontStart,
                                    GridTree_ChildIndex( nodeIndex, SIDE_FRONT ),
                                    depth, userData, callback ) )
            return qfalse;
        return GridTree_TraceLine_r( traceStart, traceEnd, backEnd, segEnd,
                                     GridTree_ChildIndex( nodeIndex, SIDE_BACK ),
                                     depth, userData, callback );
    }
}


/* GridTree_TraceLine  0x00450d20 */
qboolean GridTree_TraceLine( const vec3_t start, const vec3_t end, void *userData,
                             qboolean (*callback)( TriSurf_t *, const vec3_t,
                                                   const vec3_t, void * ) )
{
    return GridTree_TraceLine_r( start, end, start, end, 0, 0, userData, callback );
}


/* GridTree_Init  0x00451190 */
void GridTree_Init( void )
{
    Assert( gridTreeGlob.root == NULL );
    gridTreeGlob.root = new GridTreeNode_t[WINDING_TREE_NODE_COUNT];
}


/* GridTree_Shutdown  0x004511e0 */
void GridTree_Shutdown( void )
{
    Assert( gridTreeGlob.root );
    delete [] gridTreeGlob.root;
    gridTreeGlob.root = NULL;
}
