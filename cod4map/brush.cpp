/* Original: .\brush.cpp */

#include "q_shared.h"
#include "assertive.h"
#include "com_math.h"
#include "com_vector.h"
#include "surfaceflags.h"
#include "cmdlib.h"
#include "polylib.h"
#include "bspfile.h"
#include "brush_edges.h"
#include "brush.h"
#include "map.h"
#include "bsp.h"

extern const char *StringFromOffset( const void *base );            /* 0x004053a0 */

typedef char brush_static_assert_side[ sizeof( side_t )  == 0x78 ? 1 : -1 ];
typedef char brush_static_assert_hdr [ BRUSH_HEADER_SIZE == 0x54 ? 1 : -1 ];
typedef char brush_static_assert_node[ sizeof( Node_t )  == 0x6c ? 1 : -1 ];
typedef char brush_static_assert_tree[ sizeof( Tree_t )  == 0x88 ? 1 : -1 ];
typedef char brush_static_assert_pt  [ sizeof( brushPointCapsule_t ) == 0x50 ? 1 : -1 ];


int g_removedBrushSides;                        /* 0x00534bac */

static union
{
    brushPoint_t        pts[MAX_BRUSH_POINTS];
    brushPointCapsule_t capsulePts[MAX_BRUSH_POINTS];
} brushPoints;                                  /* 0x00534bb0, 0x500000 */

typedef int ( *addBrushPointFn_t )( const Brush_t *brush, const side_t *sides, int sideCount,
                                    float capsuleRadius, float capsuleOffsetZ,
                                    const int *planeIndices, qboolean skipBevels,
                                    const vec3_t point, int ptCount );


/* CountBrushList  0x00402590 */
int CountBrushList( const Brush_t *brushes )
{
    int count;

    count = 0;
    for ( ; brushes; brushes = brushes->next )
    {
        if ( !brushes->noCollision && !brushes->original->noCollision )
            count++;
    }
    return count;
}


/* AllocBrush  0x004025e0 */
Brush_t *AllocBrush( int sideCount )
{
    size_t   size;
    Brush_t *brush;

    size = BRUSH_HEADER_SIZE + sideCount * sizeof( side_t );
    brush = ( Brush_t * )malloc( size );
    memset( brush, 0, size );
    brush->cullGroup = -1;
    return brush;
}


/* FreeBrushWindings  0x00402630 */
void FreeBrushWindings( Brush_t *brush )
{
    unsigned int sideIndex;

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        if ( brush->sides[sideIndex].winding )
        {
            FreeWinding( brush->sides[sideIndex].winding );
            brush->sides[sideIndex].winding = NULL;
        }
        if ( brush->sides[sideIndex].edges )
        {
            FreeAdjacencyWinding( ( adjacencyWinding_t * )brush->sides[sideIndex].edges );
            brush->sides[sideIndex].edges = NULL;
        }
    }

    if ( brush->collisionSides != brush->sides )
    {
        free( brush->collisionSides );
        brush->collisionSides = NULL;
    }
}


/* FreeBrush  0x00402710 */
void FreeBrush( Brush_t *brush )
{
    FreeBrushWindings( brush );
    free( brush );
}


/* FreeBrushList  0x00402730 */
void FreeBrushList( Brush_t *brushes )
{
    Brush_t *next;

    while ( brushes )
    {
        next = brushes->next;
        FreeBrush( brushes );
        brushes = next;
    }
}


/* CopyBrush  0x00402760 */
Brush_t *CopyBrush( const Brush_t *brush )
{
    size_t       brushSize;
    Brush_t     *newBrush;
    unsigned int sideIndex;

    Assert( brush->collisionSideCount == 0 );
    Assert( brush->collisionSides == NULL );

    brushSize = BRUSH_HEADER_SIZE + brush->sideCount * sizeof( side_t );
    newBrush = AllocBrush( brush->sideCount );
    memcpy( newBrush, brush, brushSize );

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        if ( brush->sides[sideIndex].winding )
            newBrush->sides[sideIndex].winding = CopyWinding( brush->sides[sideIndex].winding );
        if ( brush->sides[sideIndex].edges )
            newBrush->sides[sideIndex].edges =
                CopyAdjacencyWinding( ( adjacencyWinding_t * )brush->sides[sideIndex].edges );
    }
    return newBrush;
}


/* CopyCollisionBrush  0x00402890 */
Brush_t *CopyCollisionBrush( const Brush_t *brush )
{
    size_t       brushSize;
    Brush_t     *newBrush;
    unsigned int sideIndex;

    Assert( brush->collisionSideCount != 0 );
    Assert( brush->collisionSides != NULL );

    brushSize = BRUSH_HEADER_SIZE + brush->collisionSideCount * sizeof( side_t );
    newBrush = AllocBrush( brush->collisionSideCount );
    memcpy( newBrush, brush, BRUSH_HEADER_SIZE );

    for ( sideIndex = 0; sideIndex < brush->collisionSideCount; sideIndex++ )
    {
        newBrush->sides[sideIndex] = brush->collisionSides[sideIndex];

        if ( brush->collisionSides[sideIndex].winding )
            newBrush->sides[sideIndex].winding =
                CopyWinding( brush->collisionSides[sideIndex].winding );

        if ( brush->sides[sideIndex].edges )
            newBrush->sides[sideIndex].edges =
                CopyAdjacencyWinding( ( adjacencyWinding_t * )brush->sides[sideIndex].edges );
    }

    newBrush->sideCount = brush->collisionSideCount;
    return newBrush;
}


/* BoundBrush  0x00402a00 */
qboolean BoundBrush( Brush_t *brush )
{
    unsigned int sideIndex;
    unsigned int ptIndex;
    unsigned int axis;
    const winding_t *w;

    ClearBounds( brush->mins, brush->maxs );
    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        w = brush->sides[sideIndex].winding;
        if ( !w )
            continue;
        for ( ptIndex = 0; ptIndex < w->ptCount; ptIndex++ )
            AddPointToBounds( w->pts[ptIndex], brush->mins, brush->maxs );
    }

    for ( axis = 0; axis < 3; axis++ )
    {
        if ( brush->mins[axis] < MIN_WORLD_COORD
          || brush->maxs[axis] > MAX_WORLD_COORD
          || brush->mins[axis] >= brush->maxs[axis] )
        {
            return qfalse;
        }
    }
    return qtrue;
}


/* AddBrushPoint  0x00402c50 */
static int AddBrushPoint( const Brush_t *brush, const side_t *sides, int sideCount,
                          float capsuleRadius, float capsuleOffsetZ, const int *planeIndices,
                          qboolean skipBevels, const vec3_t point, int ptCount )
{
    int            sideIndex;
    int            planeIndex;
    const plane_t *plane;

    for ( sideIndex = 0; sideIndex < sideCount; sideIndex++ )
    {
        if ( skipBevels && sides[sideIndex].bevel )
            continue;

        planeIndex = sides[sideIndex].planenum;
        if ( planeIndex == planeIndices[0] || planeIndex == planeIndices[1]
          || planeIndex == planeIndices[2] )
            continue;

        plane = &mapplanes[planeIndex];
        if ( Vec3Dot( plane->normal, point ) - plane->dist > 0.1f )
            return ptCount;
    }

    if ( ptCount == MAX_BRUSH_POINTS )
        Com_Error( "More than %i pts from plane intersections on %i-sided brush\n",
                   MAX_BRUSH_POINTS, sideCount );

    Vec3Copy( point, brushPoints.pts[ptCount].xyz );
    brushPoints.pts[ptCount].planes[0] = planeIndices[0];
    brushPoints.pts[ptCount].planes[1] = planeIndices[1];
    brushPoints.pts[ptCount].planes[2] = planeIndices[2];
    return ptCount + 1;
}


/* FindPointInBuffer  0x004048c0 */
static int FindPointInBuffer( const vec3_t point, const brushPointCapsule_t *pts, int ptCount )
{
    int i;

    i = 0;
    while ( i < ptCount && !Vec3Equal( point, pts[i].xyz ) )
        i++;

    return i;
}


/* ExpandPlaneForCapsule  0x00403030 */
static void ExpandPlaneForCapsule( const plane_t *plane, float capsuleRadius,
                                   float capsuleOffsetZ, vec4_t out )
{
    Vec3Copy( plane->normal, out );
    out[3] = plane->dist + capsuleRadius + I_fabs( plane->normal[2] * capsuleOffsetZ );
}


/* AddBrushPointCapsule  0x004045d0 */
static int AddBrushPointCapsule( const Brush_t *brush, const side_t *sides, int sideCount,
                                 float capsuleRadius, float capsuleOffsetZ, const int *planeIndices,
                                 qboolean skipBevels, const vec3_t point, int ptCount )
{
    float                expand;
    float                expandZ;
    int                  ptIndex;
    brushPointCapsule_t *pt;
    unsigned int         arrayIndex;
    unsigned int         bitMask;
    unsigned int         sideIndex;
    int                  planeIndex;
    const plane_t       *plane;
    vec4_t               expandedPlane;
    float                d;

    if ( ptCount == MAX_BRUSH_POINTS )
        Com_Error( "More than %i pts from plane intersections on %i-sided brush\n",
                   MAX_BRUSH_POINTS, brush->sideCount );

    expand  = capsuleRadius + 0.1f;
    expandZ = capsuleRadius + 0.1f + capsuleOffsetZ;

    if ( brush->mins[0] - expand > point[0] || brush->maxs[0] + expand < point[0]
      || brush->mins[1] - expand > point[1] || brush->maxs[1] + expand < point[1]
      || brush->mins[2] - expandZ > point[2] || brush->maxs[2] + expandZ < point[2] )
    {
        return ptCount;
    }

    ptIndex = FindPointInBuffer( point, brushPoints.capsulePts, ptCount );
    pt = &brushPoints.capsulePts[ptIndex];
    if ( ptIndex < ptCount )
        return ptCount;

    ptCount++;
    memset( pt, 0, sizeof( *pt ) );
    Vec3Copy( point, pt->xyz );

    arrayIndex = 0;
    bitMask = 1;
    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        planeIndex = brush->sides[sideIndex].planenum;
        if ( planeIndex != planeIndices[0] && planeIndex != planeIndices[1]
          && planeIndex != planeIndices[2] )
        {
            plane = &mapplanes[planeIndex];
            ExpandPlaneForCapsule( plane, capsuleRadius, capsuleOffsetZ, expandedPlane );
            d = Vec3Dot( expandedPlane, point ) - expandedPlane[3];

            Assert( arrayIndex == (sideIndex >> 5) );
            Assert( bitMask == (1u << (sideIndex & 31)) );

            if ( d > 0.1f )
            {
                if ( !( pt->frontBits[arrayIndex] & bitMask ) )
                {
                    pt->frontBits[arrayIndex] |= bitMask;
                    pt->sideCount++;
                }
            }
            else if ( d < -0.1f )
            {
                pt->behindBits[arrayIndex] |= bitMask;
            }
        }

        bitMask <<= 1;
        if ( !bitMask )
        {
            bitMask = 1;
            arrayIndex++;
        }
    }
    return ptCount;
}


/* BrushToPoints  0x00402d80 */
static int BrushToPoints( Brush_t *brush, const side_t *sides, unsigned int sideCount,
                          float capsuleRadius, float capsuleOffsetZ, qboolean skipBevels,
                          addBrushPointFn_t addPointFn )
{
    vec4_t        planeCopy0;
    vec4_t        planeCopy1;
    vec4_t        planeCopy2;
    const vec4_t *planes[3];
    vec3_t        point;
    int           planeIndices[3];
    unsigned int  sideIndex0;
    unsigned int  sideIndex1;
    unsigned int  sideIndex2;
    int           ptCount;

    Assert( &mapplanes[0].normal[3] == &mapplanes[0].dist );
    Assert( capsuleRadius >= 0.0f );
    Assert( capsuleOffsetZ >= 0.0f );

    planes[0] = &planeCopy0;
    planes[1] = &planeCopy1;
    planes[2] = &planeCopy2;
    ptCount = 0;

    for ( sideIndex0 = 0; sideIndex0 < sideCount - 2; sideIndex0++ )
    {
        if ( skipBevels && sides[sideIndex0].bevel )
            continue;

        planeIndices[0] = sides[sideIndex0].planenum;
        ExpandPlaneForCapsule( &mapplanes[planeIndices[0]], capsuleRadius, capsuleOffsetZ,
                               planeCopy0 );

        for ( sideIndex1 = sideIndex0 + 1; sideIndex1 < sideCount - 1; sideIndex1++ )
        {
            if ( skipBevels && sides[sideIndex1].bevel )
                continue;

            planeIndices[1] = sides[sideIndex1].planenum;
            if ( planeIndices[1] == planeIndices[0] )
                continue;

            ExpandPlaneForCapsule( &mapplanes[planeIndices[1]], capsuleRadius, capsuleOffsetZ,
                                   planeCopy1 );

            for ( sideIndex2 = sideIndex1 + 1; sideIndex2 < sideCount; sideIndex2++ )
            {
                if ( skipBevels && sides[sideIndex2].bevel )
                    continue;

                planeIndices[2] = sides[sideIndex2].planenum;
                if ( planeIndices[2] == planeIndices[1] || planeIndices[2] == planeIndices[0] )
                    continue;

                ExpandPlaneForCapsule( &mapplanes[planeIndices[2]], capsuleRadius, capsuleOffsetZ,
                                       planeCopy2 );

                if ( PlaneIntersection3( planes, point ) )
                {
                    SnapPlaneIntersection( planes, point, SNAP_GRID_SIZE, SNAP_EPSILON );
                    ptCount = addPointFn( brush, sides, sideCount, capsuleRadius, capsuleOffsetZ,
                                          planeIndices, skipBevels, point, ptCount );
                }
            }
        }
    }
    return ptCount;
}


/* FilterPointsOnPlane  0x00403170 */
static int FilterPointsOnPlane( const brushPoint_t *pt, vec3_t *out, int outCount, int maxOut )
{
    int i;

    for ( i = 0; i < outCount; i++ )
    {
        if ( Vec3Equal( pt->xyz, out[i] ) )
            return outCount;
    }

    if ( outCount == maxOut )
        Com_Error( "Winding point limit (%i) exceeded on brush face", maxOut );

    Vec3Copy( pt->xyz, out[outCount] );
    return outCount + 1;
}


/* GetPointsOnPlane  0x004030e0 */
static int GetPointsOnPlane( int planeIndex, const brushPoint_t *pts, int ptCount,
                             vec3_t *out, int maxOut )
{
    int outCount;
    int i;

    outCount = 0;
    for ( i = 0; i < ptCount; i++ )
    {
        if ( planeIndex == pts[i].planes[0] || planeIndex == pts[i].planes[1]
          || planeIndex == pts[i].planes[2] )
        {
            outCount = FilterPointsOnPlane( &pts[i], out, outCount, maxOut );
        }
    }
    return outCount;
}


/* GetWindingFromPoints  0x00403080 */
static winding_t *GetWindingFromPoints( const Brush_t *brush, int planeIndex,
                                        const brushPoint_t *pts, int ptCount )
{
    vec3_t xyz[MAX_POINTS_ON_BRUSH_FACE];
    int    xyzCount;

    xyzCount = GetPointsOnPlane( planeIndex, pts, ptCount, xyz, MAX_POINTS_ON_BRUSH_FACE );
    if ( xyzCount < 3 )
        return NULL;

    return WindingForConvexHull( mapplanes[planeIndex].normal, xyz, xyzCount );
}


/* CreateBrushWindings  0x00402b10 */
qboolean CreateBrushWindings( Brush_t *brush )
{
    unsigned int ptCount;
    unsigned int sideIndex;
    int          planeIndex;

    ptCount = BrushToPoints( brush, brush->sides, brush->sideCount, 0.0f, 0.0f, qtrue,
                             AddBrushPoint );
    if ( ptCount < MIN_BRUSH_POINTS )
        return qfalse;

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        Assert( brush->sides[sideIndex].winding == NULL );
        if ( brush->sides[sideIndex].bevel )
            continue;

        planeIndex = brush->sides[sideIndex].planenum;
        brush->sides[sideIndex].winding =
            GetWindingFromPoints( brush, planeIndex, brushPoints.pts, ptCount );
        CheckWindingInPlane( brush->sides[sideIndex].winding,
                             mapplanes[planeIndex].normal, mapplanes[planeIndex].dist );
    }

    return BoundBrush( brush );
}


/* RemapAdjacencyWindingToSideIndices  0x004031f0 */
static void RemapAdjacencyWindingToSideIndices( const Brush_t *brush, adjacencyWinding_t *w )
{
    unsigned int sideIndex;
    int          i;

    for ( i = 0; i < w->numsides; i++ )
    {
        for ( sideIndex = 0; sideIndex < brush->collisionSideCount; sideIndex++ )
        {
            if ( brush->collisionSides[sideIndex].planenum == w->sides[i] )
            {
                w->sides[i] = sideIndex;
                break;
            }
        }
        Assert( sideIndex < brush->collisionSideCount );
    }
}


/* CreateBrushAdjacencyWindings  0x004032a0 */
void CreateBrushAdjacencyWindings( Brush_t *brush )
{
    unsigned int ptCount;
    unsigned int sideIndex;
    bool         isNodrawSolid;
    int          planeIndex;

    ptCount = BrushToPoints( brush, brush->collisionSides, brush->collisionSideCount,
                             0.0f, 0.0f, qfalse, AddBrushPoint );
    if ( ptCount < MIN_BRUSH_POINTS )
        return;

    for ( sideIndex = 0; sideIndex < brush->collisionSideCount; sideIndex++ )
    {
        Assert( brush->collisionSides[sideIndex].edges == NULL );

        if ( !brush->collisionSides[sideIndex].winding )
            continue;

        isNodrawSolid = brush->collisionSides[sideIndex].contents == CONTENTS_SOLID
                     && ( brush->collisionSides[sideIndex].surfaceFlags & SURF_NODRAW ) != 0;
        if ( isNodrawSolid )
            continue;

        if ( !( brush->collisionSides[sideIndex].contents & CONTENTS_BRUSHEDGE_MASK ) )
            continue;

        planeIndex = brush->collisionSides[sideIndex].planenum;
        brush->collisionSides[sideIndex].edges =
            BuildAdjacencyWinding( mapplanes[planeIndex].normal, planeIndex,
                                   brushPoints.pts, ptCount, NULL, 0 );

        if ( brush->collisionSides[sideIndex].edges )
            RemapAdjacencyWindingToSideIndices(
                brush, ( adjacencyWinding_t * )brush->collisionSides[sideIndex].edges );
    }
}


/* WriteBSPBrushMap  0x00403440 */
void WriteBSPBrushMap( const char *name, const Brush_t *list )
{
    FILE            *f;
    unsigned int     sideIndex;
    const winding_t *w;
    int              brushnum;

    Com_Printf( "writing %s\n", name );
    f = fopen( name, "wb" );
    if ( !f )
        Com_Error( "Can't write %s", name );

    fprintf( f, "{\n\"classname\" \"worldspawn\"\n" );

    brushnum = 0;
    for ( ; list; list = list->next )
    {
        fprintf( f, "{\n" );
        for ( sideIndex = 0; sideIndex < list->sideCount; sideIndex++ )
        {
            w = list->sides[sideIndex].winding;
            if ( !w )
                continue;

            fprintf( f, "( %.0f %.0f %.0f ) ", w->pts[0][0], w->pts[0][1], w->pts[0][2] );
            fprintf( f, "( %.0f %.0f %.0f ) ", w->pts[1][0], w->pts[1][1], w->pts[1][2] );
            fprintf( f, "( %.0f %.0f %.0f ) ", w->pts[2][0], w->pts[2][1], w->pts[2][2] );
            fprintf( f, "%s 0 0 0 1 1\n", StringFromOffset( list->sides[sideIndex].material ) );
        }
        fprintf( f, "}\n" );
        brushnum++;
    }

    fprintf( f, "}\n" );
    fclose( f );
}


/* AllocTree  0x00403870 */
Tree_t *AllocTree( void )
{
    Tree_t *tree;

    tree = ( Tree_t * )malloc( sizeof( Tree_t ) );
    memset( tree, 0, sizeof( Tree_t ) );
    ClearBounds( tree->mins, tree->maxs );
    return tree;
}


/* AllocNode  0x004038c0 */
Node_t *AllocNode( void )
{
    Node_t *node;

    node = ( Node_t * )malloc( sizeof( Node_t ) );
    memset( node, 0, sizeof( Node_t ) );
    return node;
}


/* WindingIsTiny  0x004038f0 */
qboolean WindingIsTiny( const winding_t *w )
{
    int          edgeCount;
    unsigned int i;
    unsigned int j;

    edgeCount = 0;
    j = w->ptCount - 1;
    for ( i = 0; i < w->ptCount; j = i, i++ )
    {
        if ( Vec3DistanceSq( w->pts[j], w->pts[i] ) > EDGE_LENGTH * EDGE_LENGTH )
        {
            edgeCount++;
            if ( edgeCount == 3 )
                return qfalse;
        }
    }
    return qtrue;
}


/* RemoveTinyBrushSides  0x00403a50 */
static bool RemoveTinyBrushSides( Brush_t *brush )
{
    int          tinyWindingCount;
    unsigned int usedSideIndex;
    unsigned int sideIndex;
    winding_t   *w;
    bool         result;

    tinyWindingCount = 0;
    usedSideIndex = 0;
    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        w = brush->sides[sideIndex].winding;
        if ( !w || WindingIsTiny( w ) )
        {
            tinyWindingCount++;
            if ( !brush->sides[sideIndex].bevel )
            {
                if ( w )
                    FreeWinding( w );
                continue;
            }
        }
        brush->sides[usedSideIndex] = brush->sides[sideIndex];
        usedSideIndex++;
    }

    result = brush->sideCount - tinyWindingCount >= 4;
    brush->sideCount = usedSideIndex;
    return result;
}


/* ClipBrushToPlane  0x00403980 */
static Brush_t *ClipBrushToPlane( const Brush_t *brush, unsigned int planenum )
{
    Brush_t     *newBrush;
    unsigned int sideIndex;

    newBrush = AllocBrush( brush->sideCount + 1 );
    memcpy( newBrush, brush, BRUSH_HEADER_SIZE + brush->sideCount * sizeof( side_t ) );

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
        newBrush->sides[sideIndex].winding = NULL;

    newBrush->sideCount = brush->sideCount + 1;
    newBrush->sides[brush->sideCount].planenum = planenum ^ 1;

    if ( !CreateBrushWindings( newBrush ) || !RemoveTinyBrushSides( newBrush ) )
    {
        FreeBrush( newBrush );
        return NULL;
    }
    return newBrush;
}


/* SplitBrushByPlane  0x00403b40 */
void SplitBrushByPlane( Brush_t *brush, unsigned int planenum,
                        Brush_t **frontBrush, Brush_t **backBrush )

{
    const plane_t *plane;
    unsigned int   sideIndex;
    unsigned int   ptIndex;
    const winding_t *w;
    float          d;
    float          maxDist;
    float          minDist;

    *backBrush  = NULL;
    *frontBrush = NULL;
    plane = &mapplanes[planenum];
    maxDist = minDist = 0.0f;

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        w = brush->sides[sideIndex].winding;
        if ( !w )
            continue;

        for ( ptIndex = 0; ptIndex < w->ptCount; ptIndex++ )
        {
            d = Vec3Dot( w->pts[ptIndex], plane->normal ) - plane->dist;
            if ( maxDist < d )
                maxDist = d;
            if ( d < minDist )
                minDist = d;
        }
    }

    if ( maxDist < 0.1f )
    {
        *backBrush = CopyBrush( brush );
    }
    else if ( minDist > -0.1f )
    {
        *frontBrush = CopyBrush( brush );
    }
    else
    {
        *frontBrush = ClipBrushToPlane( brush, planenum );
        *backBrush  = ClipBrushToPlane( brush, planenum ^ 1 );
    }
}


/* FilterBrushIntoBspTree  0x004036b0 */
static int FilterBrushIntoBspTree( Brush_t *brush, Node_t *node )
{
    Brush_t *frontBrush;
    Brush_t *backBrush;
    float    brushVolume;
    float    nodeVolume;
    int      count;

    if ( !brush )
        return 0;

    if ( node->planenum == PLANENUM_LEAF )
    {
        brush->next = node->leafBrushes;
        node->leafBrushes = brush;

        if ( !brush->detail && brush->opaque )
        {
            brushVolume = BoundsVolume( brush->mins, brush->maxs );
            nodeVolume  = BoundsVolume( node->mins, node->maxs );
            if ( I_fmin( 1.0f, nodeVolume * 0.125f ) <= brushVolume )
                node->opaque = 1;
        }
        return 1;
    }

    SplitBrushByPlane( brush, node->planenum, &frontBrush, &backBrush );
    FreeBrush( brush );

    count = 0;
    count += FilterBrushIntoBspTree( frontBrush, node->children[0] );
    count += FilterBrushIntoBspTree( backBrush, node->children[1] );
    return count;
}


/* FilterDetailBrushesIntoBspTree  0x00403610 */
void FilterDetailBrushesIntoBspTree( Entity_t *entity, Tree_t *tree )
{
    Brush_t *b;
    int      c_unique;
    int      c_clusters;

    Com_DPrintf( "----- FilterDetailBrushesIntoTree -----\n" );

    c_unique = 0;
    c_clusters = 0;
    for ( b = entity->brushes; b; b = b->next )
    {
        if ( b->detail )
        {
            c_unique++;
            c_clusters += FilterBrushIntoBspTree( CopyBrush( b ), tree->headnode );
        }
    }

    Com_DPrintf( "%5i detail brushes\n", c_unique );
    Com_DPrintf( "%5i cluster references\n", c_clusters );
}


/* FilterStructuralBrushesIntoBspTree  0x004037d0 */
void FilterStructuralBrushesIntoBspTree( Entity_t *entity, Tree_t *tree )
{
    Brush_t *b;
    int      c_unique;
    int      c_clusters;

    Com_DPrintf( "----- FilterStructuralBrushesIntoTree -----\n" );

    c_unique = 0;
    c_clusters = 0;
    for ( b = entity->brushes; b; b = b->next )
    {
        if ( !b->detail )
        {
            c_unique++;
            c_clusters += FilterBrushIntoBspTree( CopyBrush( b ), tree->headnode );
        }
    }

    Com_DPrintf( "%5i structural brushes\n", c_unique );
    Com_DPrintf( "%5i cluster references\n", c_clusters );
}


/* TestBrushAgainstPlane  0x00403ca0 */
int TestBrushAgainstPlane( const Brush_t *brush, const vec3_t normal, float dist, float epsilon )
{
    unsigned int     sideIndex;
    unsigned int     ptIndex;
    const winding_t *w;
    float            d;
    qboolean         anyFront;
    qboolean         anyBack;
    qboolean         allOn;

    anyFront = qfalse;
    anyBack = qfalse;

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        w = brush->sides[sideIndex].winding;
        if ( !w )
            continue;

        Assert( w->ptCount );
        allOn = qtrue;
        for ( ptIndex = 0; ptIndex < w->ptCount; ptIndex++ )
        {
            d = Vec3Dot( w->pts[ptIndex], normal ) - dist;
            if ( d > epsilon )
            {
                if ( anyBack )
                    return SIDE_CROSS;
                anyFront = qtrue;
                allOn = qfalse;
            }
            else if ( d < -epsilon )
            {
                if ( anyFront )
                    return SIDE_CROSS;
                anyBack = qtrue;
                allOn = qfalse;
            }
        }
        if ( allOn )
            return SIDE_ON;
    }

    Assertx( ( anyFront || anyBack ), "(brush->sideCount) = %i", brush->sideCount );
    return anyFront == 0;
}


/* BrushContainsPoint  0x00403e20 */
qboolean BrushContainsPoint( const Brush_t *brush, const vec3_t point )
{
    unsigned int   sideIndex;
    const plane_t *plane;

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        if ( brush->sides[sideIndex].bevel )
            continue;

        plane = &mapplanes[brush->sides[sideIndex].planenum];
        if ( Vec3Dot( point, plane->normal ) - plane->dist > 0.1f )
            return qfalse;
    }
    return qtrue;
}


/* BrushPointsAllInsidePlanes  0x00404a90 */
static qboolean BrushPointsAllInsidePlanes( const Brush_t *brush, float capsuleRadius,
                                            float capsuleOffsetZ, const vec3_t *pts,
                                            unsigned int ptCount )
{
    unsigned int sideIndex;
    unsigned int ptIndex;
    vec4_t       plane;

    for ( sideIndex = 0; sideIndex < brush->sideCount; sideIndex++ )
    {
        if ( brush->sides[sideIndex].bevel )
            continue;

        ExpandPlaneForCapsule( &mapplanes[brush->sides[sideIndex].planenum],
                               capsuleRadius, capsuleOffsetZ, plane );

        for ( ptIndex = 0; ptIndex < ptCount; ptIndex++ )
        {
            if ( Vec3Dot( pts[ptIndex], plane ) - plane[3] > 0.1f )
                return qfalse;
        }
    }
    return qtrue;
}


/* FilterBrushPointsAndTestBrushes  0x00404910 */
static qboolean FilterBrushPointsAndTestBrushes( int sideIndex, int ptCount,
                                                 float capsuleRadius, float capsuleOffsetZ,
                                                 Brush_t **neighbors, int neighborCount )
{
    vec3_t       newBrushPts[MAX_BRUSH_POINTS];
    unsigned int newBrushPtCount;
    unsigned int bit;
    int          wordIndex;
    int          ptIndex;
    int          i;

    newBrushPtCount = 0;
    bit = 1 << ( sideIndex & 31 );
    wordIndex = sideIndex >> 5;

    for ( ptIndex = 0; ptIndex < ptCount; ptIndex++ )
    {
        if ( !( brushPoints.capsulePts[ptIndex].behindBits[wordIndex] & bit )
          && brushPoints.capsulePts[ptIndex].sideCount <= 1
          && ( brushPoints.capsulePts[ptIndex].sideCount == 0
               || ( brushPoints.capsulePts[ptIndex].frontBits[wordIndex] & bit ) ) )
        {
            Assert( newBrushPtCount < ARRAY_COUNT( newBrushPts ) );
            Vec3Copy( brushPoints.capsulePts[ptIndex].xyz, newBrushPts[newBrushPtCount] );
            newBrushPtCount++;
        }
    }

    for ( i = 0; i < neighborCount; i++ )
    {
        if ( BrushPointsAllInsidePlanes( neighbors[i], capsuleRadius, capsuleOffsetZ,
                                         newBrushPts, newBrushPtCount ) )
        {
            return qtrue;
        }
    }
    return qfalse;
}


/* CountPointsOnSides  0x00404b60 */
static void CountPointsOnSides( int *soloPointsCulled, int *totalPointsCulled,
                                int sideCount, int ptCount )
{
    int ptIndex;
    int sideIndex;

    memset( soloPointsCulled, 0, sideCount * sizeof( int ) );
    memset( totalPointsCulled, 0, sideCount * sizeof( int ) );

    for ( ptIndex = 0; ptIndex < ptCount; ptIndex++ )
    {
        if ( !brushPoints.capsulePts[ptIndex].sideCount )
            continue;

        for ( sideIndex = 0; sideIndex < sideCount; sideIndex++ )
        {
            if ( ( 1 << ( sideIndex & 31 ) )
                 & brushPoints.capsulePts[ptIndex].frontBits[sideIndex >> 5] )
            {
                totalPointsCulled[sideIndex]++;
                if ( brushPoints.capsulePts[ptIndex].sideCount == 1 )
                {
                    soloPointsCulled[sideIndex]++;
                    break;
                }
            }
        }
    }
}


/* CullSideFromPoints  0x00404c40 */
static void CullSideFromPoints( int *soloPointsCulled, int *totalPointsCulled,
                                int culledSideIndex, int sideCount, int ptCount )
{
    int                  wordIndex;
    unsigned int         bit;
    int                  ptIndex;
    int                  sideIndex;
    brushPointCapsule_t *pt;

    wordIndex = culledSideIndex >> 5;
    bit = 1 << ( culledSideIndex & 31 );

    for ( ptIndex = 0; ptIndex < ptCount; ptIndex++ )
    {
        pt = &brushPoints.capsulePts[ptIndex];
        if ( pt->sideCount && ( pt->frontBits[wordIndex] & bit ) )
        {
            pt->frontBits[wordIndex] &= ~bit;
            pt->sideCount--;
            totalPointsCulled[culledSideIndex]--;

            if ( pt->sideCount == 0 )
            {
                soloPointsCulled[culledSideIndex]--;
            }
            else if ( pt->sideCount == 1 )
            {
                for ( sideIndex = 0; sideIndex < sideCount; sideIndex++ )
                {
                    if ( ( 1 << ( sideIndex & 31 ) ) & pt->frontBits[sideIndex >> 5] )
                    {
                        soloPointsCulled[sideIndex]++;
                        break;
                    }
                }
            }
        }
    }

    SanityCheck( soloPointsCulled[culledSideIndex] == 0 );
    SanityCheck( totalPointsCulled[culledSideIndex] == 0 );
}


/* FindNextSoloSide  0x00404dc0 */
static unsigned int FindNextSoloSide( unsigned int sideCount, const int *soloPointsCulled,
                                      int startSide )
{
    unsigned int sideIndex;

    for ( sideIndex = startSide + 1; sideIndex < sideCount; sideIndex++ )
    {
        Assert( soloPointsCulled[sideIndex] >= 0 );
        if ( soloPointsCulled[sideIndex] > 0 )
            return sideIndex;
    }
    return sideCount;
}


/* RemoveRedundantBrushPlanes  0x00404440 */
static int RemoveRedundantBrushPlanes( Brush_t *brush, Brush_t **neighbors, int neighborCount,
                                       float capsuleRadius, float capsuleOffsetZ,
                                       byte passIndex, byte *sideFlags )
{
    int          totalPointsCulled[MAX_COLLISION_SIDES];
    int          soloPointsCulled[MAX_COLLISION_SIDES];
    unsigned int ptCount;
    unsigned int sideIndex;
    int          redundantCount;

    ptCount = BrushToPoints( brush, brush->collisionSides, brush->collisionSideCount,
                             capsuleRadius, capsuleOffsetZ, qfalse, AddBrushPointCapsule );
    Assertx( ( ptCount >= 4 ), "(ptCount) = %i", ptCount );

    CountPointsOnSides( soloPointsCulled, totalPointsCulled, brush->collisionSideCount, ptCount );

    redundantCount = 0;
    sideIndex = FIRST_NON_BBOX_SIDE - 1;
    while ( ( sideIndex = FindNextSoloSide( brush->collisionSideCount, soloPointsCulled,
                                            sideIndex ) ) != brush->collisionSideCount )
    {
        if ( sideFlags[sideIndex] == passIndex
          && FilterBrushPointsAndTestBrushes( sideIndex, ptCount, capsuleRadius, capsuleOffsetZ,
                                              neighbors, neighborCount ) )
        {
            redundantCount++;
            sideFlags[sideIndex]++;
            CullSideFromPoints( soloPointsCulled, totalPointsCulled, sideIndex,
                                brush->collisionSideCount, ptCount );
            sideIndex = FIRST_NON_BBOX_SIDE - 1;
        }
    }
    return redundantCount;
}


/* RemoveRedundantCollisionPlanes  0x00404190 */
static void RemoveRedundantCollisionPlanes( Brush_t *brush, Brush_t **neighbors,
                                            int neighborCount )
{
    float        capsuleParams[2][2] = { { 0.0f, 0.0f },
                                         { PLAYER_CAPSULE_RADIUS, PLAYER_CAPSULE_HALFHEIGHT } };
    byte         capsuleParamCount = ARRAY_COUNT( capsuleParams );
    byte         sideFlags[MAX_COLLISION_SIDES];
    byte         passIndex;
    byte         pass;
    int          redundantCount;
    unsigned int usedSideIndex;
    unsigned int sideIndex;

    memset( sideFlags, 0, brush->collisionSideCount );

    redundantCount = 0;
    passIndex = 0;
    for ( pass = 0; pass < capsuleParamCount; pass++ )
    {
        if ( !( ( 1 << pass ) & brushMethod ) )
            continue;

        redundantCount = RemoveRedundantBrushPlanes( brush, neighbors, neighborCount,
                                                     capsuleParams[pass][0], capsuleParams[pass][1],
                                                     passIndex, sideFlags );
        if ( !redundantCount )
            return;
        passIndex++;
    }

    if ( brush->collisionSides == brush->sides )
    {
        AssertCmp( brush->collisionSideCount, ==, brush->sideCount );
        brush->collisionSides =
            ( side_t * )malloc( brush->collisionSideCount * sizeof( side_t ) );
        memcpy( brush->collisionSides, brush->sides,
                brush->collisionSideCount * sizeof( side_t ) );
    }

    usedSideIndex = FIRST_NON_BBOX_SIDE;
    for ( sideIndex = FIRST_NON_BBOX_SIDE; sideIndex < brush->collisionSideCount; sideIndex++ )
    {
        if ( sideFlags[sideIndex] == passIndex )
            continue;

        if ( usedSideIndex != sideIndex )
        {
            SanityCheck( brush->collisionSides != brush->sides );
            SanityCheck( usedSideIndex < sideIndex );
            brush->collisionSides[usedSideIndex] = brush->collisionSides[sideIndex];
        }
        usedSideIndex++;
    }

    SanityCheck( usedSideIndex == brush->collisionSideCount - redundantCount );
    brush->collisionSideCount -= redundantCount;
    g_removedBrushSides += redundantCount;
}


/* AddBevelSideForNeighbor  0x00404ea0 */
static void AddBevelSideForNeighbor( Brush_t *refBrush, const Brush_t *neighborBrush,
                                     unsigned int neighborSideIndex )
{
    int                planeIndex;
    Material_t        *material;
    Material_t        *lmapMaterial;
    const plane_t     *plane;
    qboolean           needsBevel;
    unsigned int       sideIndex;
    const winding_t   *w;
    unsigned int       onCount;
    unsigned int       ptIndex;
    float              d;
    side_t            *newSides;

    if ( refBrush->collisionSideCount == MAX_COLLISION_SIDES )
        return;

    planeIndex       = neighborBrush->collisionSides[neighborSideIndex].planenum;
    material         = neighborBrush->collisionSides[neighborSideIndex].material;
    lmapMaterial     = neighborBrush->collisionSides[neighborSideIndex].lmapMaterial;
    plane = &mapplanes[planeIndex];

    needsBevel = qfalse;
    for ( sideIndex = 0; sideIndex < refBrush->collisionSideCount; sideIndex++ )
    {
        if ( ( refBrush->collisionSides[sideIndex].planenum ^ planeIndex ) <= 1 )
            return;

        w = refBrush->collisionSides[sideIndex].winding;
        if ( !w )
            continue;

        onCount = 0;
        for ( ptIndex = 0; ptIndex < w->ptCount; ptIndex++ )
        {
            d = Vec3Dot( w->pts[ptIndex], plane->normal ) - plane->dist;
            if ( d > 0.1f )
                return;
            if ( d > -0.1f )
                onCount++;
        }

        if ( onCount >= 2 )
        {
            if ( onCount == w->ptCount )
                return;
            needsBevel = qtrue;
        }
    }

    if ( !needsBevel )
        return;

    newSides = ( side_t * )malloc( ( refBrush->collisionSideCount + 1 ) * sizeof( side_t ) );
    memcpy( newSides, refBrush->collisionSides,
            refBrush->collisionSideCount * sizeof( side_t ) );
    if ( refBrush->collisionSides != refBrush->sides )
        free( refBrush->collisionSides );
    refBrush->collisionSides = newSides;

    memset( &refBrush->collisionSides[refBrush->collisionSideCount], 0, sizeof( side_t ) );
    refBrush->collisionSides[refBrush->collisionSideCount].planenum = planeIndex;
    refBrush->collisionSides[refBrush->collisionSideCount].material = material;
    refBrush->collisionSides[refBrush->collisionSideCount].lmapMaterial = lmapMaterial;
    refBrush->collisionSides[refBrush->collisionSideCount].bevel = 1;
    refBrush->collisionSideCount++;
}


/* AddNeighborBevelsForBrushPair  0x00404e30 */
static void AddNeighborBevelsForBrushPair( Brush_t **brushes )
{
    unsigned int dir;
    unsigned int sideIndex;

    for ( dir = 0; dir < 2; dir++ )
    {
        for ( sideIndex = FIRST_NON_BBOX_SIDE;
              sideIndex < brushes[dir]->collisionSideCount;
              sideIndex++ )
        {
            AddBevelSideForNeighbor( brushes[1 - dir], brushes[dir], sideIndex );
        }
    }
}


/* BrushBoundsOverlap  0x004050e0 */
qboolean BrushBoundsOverlap( const Brush_t *b0, const Brush_t *b1, float epsilon )
{
    int axis;

    for ( axis = 0; axis < 3; axis++ )
    {
        if ( b1->maxs[axis] + epsilon < b0->mins[axis] )
            return qfalse;
        if ( b0->maxs[axis] + epsilon < b1->mins[axis] )
            return qfalse;
    }
    return qtrue;
}


/* FindBrushNeighbors  0x00405150 */
int FindBrushNeighbors( const Brush_t *refBrush, Brush_t *ebrushes,
                        Brush_t **neighbors, int neighborLimit )
{
    Brush_t *b;
    int      neighborCount;

    Assert( refBrush );
    Assert( ebrushes );
    Assert( neighbors );
    Assert( neighborLimit > 0 );

    neighborCount = 0;
    for ( b = ebrushes; b; b = b->next )
    {
        if ( b->contents == refBrush->contents
          && BrushBoundsOverlap( refBrush, b, -0.1f )
          && b != refBrush )
        {
            Assert( neighborCount < neighborLimit );
            neighbors[neighborCount] = b;
            neighborCount++;
            if ( neighborCount == neighborLimit )
                return neighborCount;
        }
    }
    return neighborCount;
}


/* AddBrushNeighborBevels  0x00403eb0 */
void AddBrushNeighborBevels( Brush_t *ebrushes )
{
    Brush_t *brushes[2];
    Brush_t *neighbors[MAX_COLLISION_SIDES];
    int      neighborCount;
    int      brushCount;
    int      brushCount2;
    double   startTime;
    double   endTime;

    startTime = I_FloatTime();
    if ( !entity_num )
        Com_Printf( "Adding brush neighbor bevels...\n" );

    brushCount = 0;
    for ( brushes[1] = ebrushes; brushes[1]; brushes[1] = brushes[1]->next )
    {
        brushCount++;
        brushes[1]->collisionSideCount = brushes[1]->sideCount;
        brushes[1]->collisionSides = brushes[1]->sides;

        if ( brushes[1]->noCollision )
            continue;

        for ( brushes[0] = ebrushes; brushes[0] != brushes[1]; brushes[0] = brushes[0]->next )
        {
            Assert( brushes[0] );
            if ( !brushes[0]->noCollision && BrushBoundsOverlap( brushes[0], brushes[1], -0.1f ) )
                AddNeighborBevelsForBrushPair( brushes );
        }
    }

    if ( !entity_num )
        Com_Printf( "Removing redundant brush collision planes...\n" );

    brushCount2 = 0;
    for ( brushes[0] = ebrushes; brushes[0]; brushes[0] = brushes[0]->next )
    {
        Assert( brushes[0]->collisionSideCount >= FIRST_NON_BBOX_SIDE );
        brushCount2++;

        if ( brushes[0]->noCollision )
            continue;
        if ( brushes[0]->collisionSideCount == FIRST_NON_BBOX_SIDE )
            continue;

        neighborCount = FindBrushNeighbors( brushes[0], ebrushes, neighbors,
                                            MAX_COLLISION_SIDES );
        if ( !neighborCount )
            continue;

        RemoveRedundantCollisionPlanes( brushes[0], neighbors, neighborCount );
        Assert( brushes[0]->collisionSideCount >= FIRST_NON_BBOX_SIDE );
    }

    endTime = I_FloatTime();
    if ( !entity_num )
    {
        Com_Printf( "removed %i brush sides\n", g_removedBrushSides );
        Com_Printf( "elapsed time %.0f seconds\n", endTime - startTime );
    }

    for ( brushes[0] = ebrushes; brushes[0]; brushes[0] = brushes[0]->next )
        CreateBrushAdjacencyWindings( brushes[0] );
}
