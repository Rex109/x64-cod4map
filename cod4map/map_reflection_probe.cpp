/* Original: .\map_reflection_probe.cpp */

#include "map_reflection_probe.h"

#include "cod4map.h"
#include "brush.h"
#include "tree.h"
#include "portals.h"
#include "materials.h"

#include <stdlib.h>

BspReflectionProbe_t *s_savedProbes;                                /* 0x123a9488 */
int                   reflectionDebug;                            /* 0x123a948c */
int                   s_savedProbeCount;                            /* 0x123a9490 */
int                   s_ignorePortals;                              /* 0x123a9494 */
char                  s_defaultColorCorrection[MAX_COLOR_CORRECTION_NAME]; /* 0x123a9498 */
int                   s_cellVoteCount[MAX_MAP_CELLS];               /* 0x123a94d8 */

static void   ClearCellProbeLists( void );
static void   AddProbeToCell( int probeIndex, unsigned int cellIndex );
static void   FillEmptyCells( Node_t *headnode );
static void   CopyCellProbes( unsigned int to, unsigned int from );
static int    FindBestNeighborCell( int cellTestIndex, Node_t *headnode );
static void   AccumulateCellPortalArea_r( int cellTestIndex, float *area, Node_t *node );
static float  ReflectionWindingArea( const winding_t *winding );
static float  ReflectionTriangleArea( const vec3_t v0, const vec3_t v1, const vec3_t v2 );
static int    ReflectionProbeForSurface( Tree_t *tree, const DrawVert_t *verts,
                                         int vertCount, qboolean useCentroid );
static float  AverageProbeDistanceSq( const vec3_t probeOrigin, const DrawVert_t *verts,
                                      int vertCount );
static float  ProbeDistanceSq( const vec3_t probeOrigin, const vec3_t point );
static int    CellForSurfaceCentroid( Tree_t *tree, const DrawVert_t *verts, int vertCount );
static void   SurfaceCentroid( const DrawVert_t *verts, int vertCount, vec3_t out );
static int    CellWithMostSurfaceVerts( Tree_t *tree, const DrawVert_t *verts, int vertCount );
static int    NearestReflectionProbe( const DrawVert_t *verts, int vertCount );
static qboolean MaterialWantsReflectionProbe( const Material_t *material );
static void   FillProbeRainbow( BspReflectionProbe_t *probe );
static byte   RandomColorByte( void );
static void   FillProbeConstant( BspReflectionProbe_t *probe, byte value );
static void   CopyProbeCubeMap( BspReflectionProbe_t *probe, const BspReflectionProbe_t *from );
static BspReflectionProbe_t *FindSavedProbeAtOrigin( const vec3_t origin );


/* AssignReflectionProbesToCells  0x0041d8c0 */
void AssignReflectionProbesToCells( Tree_t *tree )
{
    Node_t      *node;
    unsigned int probeIndex;

    ClearCellProbeLists();

    Assert( tree );
    Assert( tree->headnode );

    for ( probeIndex = 0; probeIndex < ( unsigned int )numBSPReflectionProbes; probeIndex++ )
    {
        node = PointInLeaf( tree->headnode, bspReflectionProbes[probeIndex].origin );

        if ( node->opaque == 1 )
            Com_Error( "ERROR: Reflection probe at (%.1f %.1f %.1f) is in solid.\n",
                       bspReflectionProbes[probeIndex].origin[0],
                       bspReflectionProbes[probeIndex].origin[1],
                       bspReflectionProbes[probeIndex].origin[2] );

        Assert( node );

        AddProbeToCell( probeIndex, node->cellnum );
    }

    if ( numBSPReflectionProbes )
        FillEmptyCells( tree->headnode );
}


/* ClearCellProbeLists  0x0041db10 */
static void ClearCellProbeLists( void )
{
    int cellIndex;

    for ( cellIndex = 0; cellIndex < numCells; cellIndex++ )
        bspCells[cellIndex].reflectionProbeCount = 0;
}


/* AddProbeToCell  0x0041da10 */
static void AddProbeToCell( int probeIndex, unsigned int cellIndex )
{
    BspCell_t *cell;

    AssertIn( cellIndex, MAX_MAP_CELLS );

    cell = &bspCells[cellIndex];

    if ( cell->reflectionProbeCount == MAX_CELL_REFLECTION_PROBES )
    {
        Com_Error( "ERROR: Cell %d has too many reflection probes %d.  "
                   "Not adding probe at (%f %f %f) to cell.\n",
                   cellIndex, MAX_CELL_REFLECTION_PROBES,
                   bspReflectionProbes[probeIndex].origin[0],
                   bspReflectionProbes[probeIndex].origin[1],
                   bspReflectionProbes[probeIndex].origin[2] );
        return;
    }

    cell->reflectionProbes[cell->reflectionProbeCount] =
        checked_cast< unsigned char >( probeIndex + 1 );
    cell->reflectionProbeCount++;
}


/* FillEmptyCells  0x0041db50 */
static void FillEmptyCells( Node_t *headnode )
{
    int emptyCount;
    int filledCount;
    int cellIndex;
    int bestCell;

    do
    {
        emptyCount  = 0;
        filledCount = 0;

        for ( cellIndex = 0; cellIndex < numCells; cellIndex++ )
        {
            if ( bspCells[cellIndex].reflectionProbeCount )
                continue;

            emptyCount++;
            bestCell = FindBestNeighborCell( cellIndex, headnode );
            if ( bestCell != -1 )
            {
                CopyCellProbes( cellIndex, bestCell );
                filledCount++;
            }
        }
    }
    while ( emptyCount && filledCount );

    if ( !emptyCount )
        return;

    for ( cellIndex = 0; cellIndex < numCells; cellIndex++ )
    {
        if ( bspCells[cellIndex].reflectionProbeCount )
            continue;
        bspCells[cellIndex].reflectionProbeCount = 1;
        bspCells[cellIndex].reflectionProbes[0]  = 0;
    }
}


/* CopyCellProbes  0x0041dc40 */
static void CopyCellProbes( unsigned int to, unsigned int from )
{
    Assert( to != from );
    AssertIn( to, ( unsigned int )numCells );
    AssertIn( from, ( unsigned int )numCells );
    Assert( bspCells[to].reflectionProbeCount == 0 );
    Assert( bspCells[from].reflectionProbeCount > 0 );

    bspCells[to].reflectionProbeCount = bspCells[from].reflectionProbeCount;
    memcpy( bspCells[to].reflectionProbes, bspCells[from].reflectionProbes,
            bspCells[from].reflectionProbeCount );
}


/* FindBestNeighborCell  0x0041dd80 */
static int FindBestNeighborCell( int cellTestIndex, Node_t *headnode )
{
    float area[MAX_MAP_CELLS];
    int   bestCellIndex;
    float bestArea;
    int   cellIndex;

    memset( area, 0, sizeof( area ) );
    AccumulateCellPortalArea_r( cellTestIndex, area, headnode );

    Assert( numCells < MAX_MAP_CELLS );

    bestCellIndex = -1;
    bestArea      = 0.0f;

    for ( cellIndex = 0; cellIndex < numCells; cellIndex++ )
    {
        if ( bestArea < area[cellIndex] )
        {
            bestArea      = area[cellIndex];
            bestCellIndex = cellIndex;
            Assert( bestCellIndex != cellTestIndex );
        }
    }

    return bestCellIndex;
}


/* AccumulateCellPortalArea_r  0x0041de80 */
static void AccumulateCellPortalArea_r( int cellTestIndex, float *area, Node_t *node )
{
    portal_t *portal;
    Node_t   *other;
    unsigned int side;

    if ( node->planenum != PLANENUM_LEAF )
    {
        AccumulateCellPortalArea_r( cellTestIndex, area, node->children[0] );
        AccumulateCellPortalArea_r( cellTestIndex, area, node->children[1] );
        return;
    }

    if ( node->opaque == 1 )
        return;
    if ( node->cellnum != cellTestIndex )
        return;

    for ( portal = node->portals; portal; portal = portal->next[side] )
    {
        side = ( portal->nodes[1] == node );

        if ( !Portal_Passable( portal ) )
            continue;

        other = portal->nodes[!side];

        AssertIn( ( unsigned int )other->cellnum, ( unsigned int )numCells );

        if ( other->cellnum == cellTestIndex )
            continue;

        AssertIn( ( unsigned int )other->cellnum, ( unsigned int )numCells );

        if ( !bspCells[other->cellnum].reflectionProbeCount )
            continue;

        area[other->cellnum] = ReflectionWindingArea( portal->winding ) + area[other->cellnum];
    }
}


/* ReflectionWindingArea  0x0041e000 */
static float ReflectionWindingArea( const winding_t *winding )
{
    unsigned int i;
    float        total;

    Assert( winding->ptCount >= 3 );

    total = 0.0f;
    for ( i = winding->ptCount; ( int )i > 2; i-- )
        total = ReflectionTriangleArea( winding->pts[0],
                                        winding->pts[i - 2],
                                        winding->pts[i - 1] ) + total;

    return total;
}


/* ReflectionTriangleArea  0x0041e090 */
static float ReflectionTriangleArea( const vec3_t v0, const vec3_t v1, const vec3_t v2 )
{
    vec3_t a;
    vec3_t b;
    vec3_t cross;

    Vec3Sub( v1, v0, a );
    Vec3Sub( v2, v0, b );
    Vec3Cross( a, b, cross );

    return Vec3Length( cross ) * 0.5;
}


/* AssignReflectionProbesToDrawSurfs  0x0041e0f0 */
void AssignReflectionProbesToDrawSurfs( Tree_t *tree, int firstSurf )
{
    int        surfIndex;
    DrawSurf_t *ds;
    qboolean   useCentroid;

    for ( surfIndex = firstSurf; surfIndex < numMapDrawSurfs; surfIndex++ )
    {
        ds = &drawSurfs[surfIndex];

        if ( !MaterialWantsReflectionProbe( ds->mtlTex ) )
        {
            ds->reflectionProbeIndex = 0;
            continue;
        }

        useCentroid = !ds->isModel && !ds->isPatch;

        ds->reflectionProbeIndex = ReflectionProbeForSurface( tree, ds->verts, ds->vertCount,
                                                              useCentroid );
    }
}


/* ReflectionProbeForSurface  0x0041e190 */
static int ReflectionProbeForSurface( Tree_t *tree, const DrawVert_t *verts, int vertCount,
                                      qboolean useCentroid )
{
    BspCell_t   *cell;
    int          cellIndex;
    unsigned int bestProbe;
    float        bestDistSq;
    float        distSq;
    unsigned int i;
    byte         probe;

    Assert( vertCount > 0 );

    if ( numBSPReflectionProbes == 0 )
        return 0;

    if ( s_ignorePortals )
        return NearestReflectionProbe( verts, vertCount );

    if ( useCentroid )
        cellIndex = CellForSurfaceCentroid( tree, verts, vertCount );
    else
        cellIndex = CellWithMostSurfaceVerts( tree, verts, vertCount );

    cell = &bspCells[cellIndex];

    Assert( cell->reflectionProbeCount > 0 );

    bestProbe  = REFLECTION_PROBE_INVALID;
    bestDistSq = 3.4028235e+38f;

    Assert( cell->reflectionProbeCount > 0 );

    for ( i = 0; i < cell->reflectionProbeCount; i++ )
    {
        probe  = cell->reflectionProbes[i];
        distSq = AverageProbeDistanceSq( bspReflectionProbes[probe - 1].origin,
                                         verts, vertCount );
        if ( distSq < bestDistSq )
        {
            bestProbe  = probe;
            bestDistSq = distSq;
        }
    }

    Assert( bestProbe != REFLECTION_PROBE_INVALID );

    return bestProbe;
}


/* AverageProbeDistanceSq  0x0041e360 */
static float AverageProbeDistanceSq( const vec3_t probeOrigin, const DrawVert_t *verts,
                                     int vertCount )
{
    float total;
    int   i;

    Assert( vertCount > 0 );

    total = 0.0f;
    for ( i = 0; i < vertCount; i++ )
        total = ProbeDistanceSq( probeOrigin, verts[i].xyz ) + total;

    return total / ( float )vertCount;
}


/* ProbeDistanceSq  0x0041e3e0 */
static float ProbeDistanceSq( const vec3_t probeOrigin, const vec3_t point )
{
    return Vec3DistanceSq( point, probeOrigin );
}


/* CellForSurfaceCentroid  0x0041e400 */
static int CellForSurfaceCentroid( Tree_t *tree, const DrawVert_t *verts, int vertCount )
{
    vec3_t  point;
    Node_t *node;

    SurfaceCentroid( verts, vertCount, point );

    Assert( vertCount > 0 );

    Vec3Add( point, verts[0].normal, point );

    node = PointInOpenLeaf( tree->headnode, point );
    Assert( node );

    if ( node->cellnum == -1 )
    {
        Assert( numCells > 0 );
        return 0;
    }

    return node->cellnum;
}


/* SurfaceCentroid  0x0041e4d0 */
static void SurfaceCentroid( const DrawVert_t *verts, int vertCount, vec3_t out )
{
    int i;

    Assert( vertCount > 0 );

    Vec3Clear( out );
    for ( i = 0; i < vertCount; i++ )
        Vec3Add( out, verts[i].xyz, out );

    Vec3Scale( out, 1.0f / ( float )vertCount, out );
}


/* CellWithMostSurfaceVerts  0x0041e560 */
static int CellWithMostSurfaceVerts( Tree_t *tree, const DrawVert_t *verts, int vertCount )
{
    Node_t *node;
    int     vertIndex;
    int     cellIndex;
    int     bestCell;

    memset( s_cellVoteCount, 0, sizeof( s_cellVoteCount ) );

    for ( vertIndex = 0; vertIndex < vertCount; vertIndex++ )
    {
        node = PointInOpenLeaf( tree->headnode, verts[vertIndex].xyz );
        Assert( node );

        if ( node->cellnum == -1 )
            continue;

        AssertIn( ( unsigned int )node->cellnum, MAX_MAP_CELLS );
        s_cellVoteCount[node->cellnum] = s_cellVoteCount[node->cellnum] + 1;
    }

    Assert( numCells > 0 );

    bestCell = 0;
    for ( cellIndex = 1; cellIndex < numCells; cellIndex++ )
        if ( s_cellVoteCount[bestCell] < s_cellVoteCount[cellIndex] )
            bestCell = cellIndex;

    return bestCell;
}


/* NearestReflectionProbe  0x0041e6b0 */
static int NearestReflectionProbe( const DrawVert_t *verts, int vertCount )
{
    int          bestProbe;
    float        bestDistSq;
    float        distSq;
    unsigned int probeIndex;

    bestProbe  = REFLECTION_PROBE_NONE;
    bestDistSq = 3.4028235e+38f;

    for ( probeIndex = 0; probeIndex < ( unsigned int )numBSPReflectionProbes; probeIndex++ )
    {
        distSq = AverageProbeDistanceSq( bspReflectionProbes[probeIndex].origin,
                                         verts, vertCount );
        if ( distSq < bestDistSq )
        {
            bestProbe  = probeIndex + 1;
            bestDistSq = distSq;
        }
    }

    Assert( bestProbe != REFLECTION_PROBE_NONE );

    return bestProbe;
}


/* MaterialWantsReflectionProbe  0x0041e760 */
static qboolean MaterialWantsReflectionProbe( const Material_t *material )
{
    return ( material->techSetFlags & 0x10 ) != 0;
}


/* SaveReflectionProbes / FreeSavedReflectionProbes  0x0041e780 / 0x0041e7e0 */
void SaveReflectionProbes( void )
{
    size_t size;

    s_savedProbeCount = numBSPReflectionProbes;

    size          = numBSPReflectionProbes * sizeof( BspReflectionProbe_t );
    s_savedProbes = ( BspReflectionProbe_t * )malloc( size );
    memcpy( s_savedProbes, bspReflectionProbes, size );

    numBSPReflectionProbes = 0;
}

void FreeSavedReflectionProbes( void )
{
    free( s_savedProbes );
    s_savedProbes     = NULL;
    s_savedProbeCount = 0;
}


/* ReflectionProbe_ParseColorCorrection  0x0041e810 */
void ReflectionProbe_ParseColorCorrection( const Entity_t *ent )
{
    const char  *ccName;
    unsigned int probeIndex;

    ccName = ValueForKey( ent, "reflection_color_correction" );
    Assert( ccName );

    if ( ( unsigned int )I_strlen( ccName ) >= MAX_COLOR_CORRECTION_NAME )
        Com_Error( "ERROR: color_correction name '%s' exceed %d characters.\n",
                   ccName, MAX_COLOR_CORRECTION_NAME );

    if ( ccName[0] )
        I_strncpyz( s_defaultColorCorrection, ccName, sizeof( s_defaultColorCorrection ) );

    for ( probeIndex = 0; probeIndex < ( unsigned int )numBSPReflectionProbes; probeIndex++ )
    {
        if ( !bspReflectionProbes[probeIndex].name[0] )
            I_strncpyz( bspReflectionProbes[probeIndex].name, s_defaultColorCorrection,
                        sizeof( bspReflectionProbes[probeIndex].name ) );
    }
}


/* ReflectionProbe_ParseIgnorePortals  0x0041e8f0 */
void ReflectionProbe_ParseIgnorePortals( const Entity_t *ent )
{
    const char *value;

    value = ValueForKey( ent, "reflection_ignore_portals" );
    Assert( value );

    if ( value[0] && value[0] != '0' )
        s_ignorePortals = 1;
}


/* ReflectionProbe_ParseEntity  0x0041e950 */
void ReflectionProbe_ParseEntity( const Entity_t *ent )
{
    BspReflectionProbe_t *probe;
    const BspReflectionProbe_t *saved;
    const char           *ccName;

    if ( numBSPReflectionProbes == MAX_MAP_REFLECTION_PROBES )
        Com_Error( "ERROR: More than %i reflection probes\n", MAX_MAP_REFLECTION_PROBES );

    probe = &bspReflectionProbes[numBSPReflectionProbes];
    numBSPReflectionProbes++;

    GetVectorForKey( ent, "origin", probe->origin );

    ccName = ValueForKey( ent, "color_correction" );
    Assert( ccName );

    if ( ( unsigned int )I_strlen( ccName ) >= MAX_COLOR_CORRECTION_NAME )
        Com_Error( "ERROR: color_correction name '%s' exceed %d characters.\n",
                   ccName, MAX_COLOR_CORRECTION_NAME );

    if ( ccName[0] )
        I_strncpyz( probe->name, ccName, sizeof( probe->name ) );
    else
        I_strncpyz( probe->name, s_defaultColorCorrection, sizeof( probe->name ) );

    saved = FindSavedProbeAtOrigin( probe->origin );

    if ( reflectionDebug )
        FillProbeRainbow( probe );
    else if ( saved )
        CopyProbeCubeMap( probe, saved );
    else
        FillProbeConstant( probe, 0 );
}


/* FillProbeRainbow  0x0041ea90 */
static void FillProbeRainbow( BspReflectionProbe_t *probe )
{
    unsigned int color;
    byte         c0;
    byte         c1;
    byte         c2;
    int          i;

    c0 = RandomColorByte();
    c1 = RandomColorByte();
    c2 = RandomColorByte();

    color = ( ( unsigned int )c0 << 24 ) |
            ( ( unsigned int )c1 << 16 ) |
            ( ( unsigned int )c2 <<  8 ) | 0xff;

    for ( i = 0; i < REFLECTION_PROBE_TEXELS; i++ )
        memcpy( probe->cubeMap + i * 4, &color, 4 );
}


/* RandomColorByte  0x0041eb10 */
static byte RandomColorByte( void )
{
    return ( byte )( ( double )rand() / 32767.0 * 255.0 );
}


/* FillProbeConstant / CopyProbeCubeMap  0x0041eb50 / 0x0041eb70 */
static void FillProbeConstant( BspReflectionProbe_t *probe, byte value )
{
    memset( probe->cubeMap, value, sizeof( probe->cubeMap ) );
}

static void CopyProbeCubeMap( BspReflectionProbe_t *probe, const BspReflectionProbe_t *from )
{
    memcpy( probe->cubeMap, from->cubeMap, sizeof( probe->cubeMap ) );
}


/* FindSavedProbeAtOrigin  0x0041eb90 */
static BspReflectionProbe_t *FindSavedProbeAtOrigin( const vec3_t origin )
{
    unsigned int i;

    for ( i = 0; i < ( unsigned int )s_savedProbeCount; i++ )
    {
        if ( Vec3Compare( origin, s_savedProbes[i].origin ) )
            return &s_savedProbes[i];
    }

    return NULL;
}
