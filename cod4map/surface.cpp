/* Original: .\surface.cpp */

#include "surface.h"

#include "cod4map.h"
#include "bsp.h"
#include "materials.h"
#include "qsort_vc8.h"

extern float defaultTessSize;                                        /* 0x123ce900 */
extern const char *StringFromOffset( const void *base );            /* 0x004053a0 */

FILE       *g_debugPolyFile;                        /* 0x14899cb8 */
int         numMapDrawSurfs;                        /* 0x14899cbc */
DrawSurf_t  drawSurfs[MAX_MAP_DRAW_SURFS];          /* 0x12f19cb8 */


/* AllocDrawSurface  0x00439f20 */
DrawSurf_t *AllocDrawSurface( void )
{
    int index;

    if ( numMapDrawSurfs >= MAX_MAP_DRAW_SURFS )
        Com_Error( "MAX_MAP_DRAW_SURFS" );

    index = numMapDrawSurfs;
    numMapDrawSurfs++;

    drawSurfs[index].reflectionProbeIndex = 255;
    drawSurfs[index].primaryLightIndex = 256;

    return &drawSurfs[index];
}


/* DrawSurfaceForSide  0x00439f80 */
DrawSurf_t *DrawSurfaceForSide( side_t *side, const winding_t *w,
                                int mapInfoIndex, int entityNum,
                                int brushNum, int outputNumber )
{
    DrawSurf_t   *surf;
    DrawVert_t   *mv;
    vec2_t        minBounds;
    vec2_t        maxBounds;
    float         midS;
    float         midT;
    unsigned int  i;

    if ( w->ptCount > MAX_FACE_WINDING_POINTS )
        Com_Error( "DrawSurfaceForSide: w->ptCount = %i", w->ptCount );

    surf = AllocDrawSurface();

    surf->entityNum    = entityNum;
    surf->brushNum     = brushNum;
    surf->mapInfoIndex = mapInfoIndex;
    surf->isPatch      = 0;
    surf->isModel      = 0;

    surf->mtlTex     = side->material;
    surf->lmapMaterial = side->lmapMaterial;
    surf->contents     = side->contents;
    surf->toolFlags    = side->toolFlags;
    surf->smoothing    = side->smoothing;
    surf->side         = side;
    surf->outputNumber = outputNumber;

    surf->vertCount = w->ptCount;
    surf->verts     = ( DrawVert_t * )malloc( surf->vertCount * sizeof( DrawVert_t ) );
    memset( surf->verts, 0, surf->vertCount * sizeof( DrawVert_t ) );

    minBounds[0] = 99999.0f;
    minBounds[1] = 99999.0f;
    maxBounds[0] = -99999.0f;
    maxBounds[1] = -99999.0f;

    for ( i = 0; i < w->ptCount; i++ )
    {
        mv = &surf->verts[i];

        Vec3Copy( w->pts[i], mv->xyz );
        Vec3Copy( mapplanes[side->planenum].normal, mv->normal );

        mv->st[0] = Vec3Dot( side->texMat[0], mv->xyz ) + side->texMat[0][3];
        mv->st[1] = Vec3Dot( side->texMat[1], mv->xyz ) + side->texMat[1][3];
        AddPointToBounds2D( mv->st, minBounds, maxBounds );

        mv->lmSt[0] = Vec3Dot( side->lmapMat[0], mv->xyz ) + side->lmapMat[0][3];
        mv->lmSt[1] = Vec3Dot( side->lmapMat[1], mv->xyz ) + side->lmapMat[1][3];
    }

    if ( surf->mtlTex->toolFlags & 0x80 )
    {
        midS = ( float )FloorFloatToInt( ( minBounds[0] + maxBounds[0] ) * 0.5f );
        midT = ( float )FloorFloatToInt( ( minBounds[1] + maxBounds[1] ) * 0.5f );

        for ( i = 0; i < w->ptCount; i++ )
        {
            surf->verts[i].st[0] -= midS;
            surf->verts[i].st[1] -= midT;
        }
    }

    return surf;
}


/* WindingFromDrawSurf  0x0043a7f0 */
winding_t *WindingFromDrawSurf( const DrawSurf_t *surf )
{
    winding_t *w;
    int        i;

    w          = AllocWinding( surf->vertCount );
    w->ptCount = surf->vertCount;

    for ( i = 0; i < surf->vertCount; i++ )
        Vec3Copy( surf->verts[i].xyz, w->pts[i] );

    return w;
}


/* SortDrawSurfaces  0x00439bc0 */
void SortDrawSurfaces( void )
{
    if ( entities[entity_num].firstDrawSurf != numMapDrawSurfs )
    {
        qsort_vc8( &drawSurfs[entities[entity_num].firstDrawSurf],
               numMapDrawSurfs - entities[entity_num].firstDrawSurf,
               sizeof( DrawSurf_t ),
               DrawSurfaceCompare );
    }
}


/* DrawSurfaceCompare  0x00439c20 */
int DrawSurfaceCompare( const void *va, const void *vb )
{
    const DrawSurf_t *surfs[2];
    const DrawSurf_t *a;
    const DrawSurf_t *b;
    vec3_t            mins[2];
    vec3_t            maxs[2];
    bool              skyA, skyB;
    char              opaqueA, opaqueB;
    bool              flagA, flagB;
    int               s;
    int               i;
    int               c;

    a = ( const DrawSurf_t * )va;
    b = ( const DrawSurf_t * )vb;
    surfs[0] = a;
    surfs[1] = b;

    skyA = ( a->mtlTex->toolFlags & 0x70 ) == 0x10;
    skyB = ( b->mtlTex->toolFlags & 0x70 ) == 0x10;
    if ( skyA != skyB )
        return skyA ? -1 : 1;

    opaqueA = ( char )( 1 - ( ( a->mtlTex->surfaceFlags & 0x40000 ) != 0 ) );
    opaqueB = ( char )( 1 - ( ( b->mtlTex->surfaceFlags & 0x40000 ) != 0 ) );
    if ( opaqueA != opaqueB )
        return opaqueA ? -1 : 1;

    flagA = ( a->mtlTex->surfaceFlags & 4 ) != 0;
    flagB = ( b->mtlTex->surfaceFlags & 4 ) != 0;
    if ( flagA != flagB )
        return flagA ? 1 : -1;

    if ( a->isPatch != b->isPatch )
        return a->isPatch ? 1 : -1;

    if ( a->isModel != b->isModel )
        return a->isModel ? 1 : -1;

    if ( a->vertCount != b->vertCount )
        return a->vertCount - b->vertCount;

    for ( s = 0; s < 2; s++ )
    {
        ClearBounds( mins[s], maxs[s] );
        for ( i = 0; i < surfs[s]->vertCount; i++ )
            AddPointToBounds( surfs[s]->verts[i].xyz, mins[s], maxs[s] );
    }

    for ( i = 0; i < 3; i++ )
    {
        if ( mins[0][i] < mins[1][i] )
            return -1;
        if ( mins[1][i] < mins[0][i] )
            return 1;
        if ( maxs[0][i] < maxs[1][i] )
            return -1;
        if ( maxs[1][i] < maxs[0][i] )
            return 1;
    }

    if ( a->mtlTex != b->mtlTex )
        return Material_SortsBefore( a->mtlTex, b->mtlTex ) ? -1 : 1;

    if ( a->entityNum != b->entityNum )
        return ( a->entityNum != b->entityNum );

    return a->brushNum - b->brushNum;
}


/* SubdivideDrawSurfs  0x0043a510 */
void SubdivideDrawSurfs( Entity_t *ent, Tree_t *tree )
{
    winding_t *w;
    float      subdivisions;
    int        numSurfs;
    int        i;

    Com_DPrintf( "----- SubdivideDrawSurfs -----\n" );

    numSurfs = numMapDrawSurfs;

    for ( i = ent->firstDrawSurf; i < numSurfs; i++ )
    {
        if ( drawSurfs[i].isPatch || drawSurfs[i].isModel )
            continue;
        if ( !drawSurfs[i].side->material )
            continue;

        subdivisions = drawSurfs[i].side->material->subdivisions;
        if ( subdivisions == 0.0f )
        {
            if ( defaultTessSize == 0.0f )
                continue;
            subdivisions = defaultTessSize;
        }

        w = WindingFromDrawSurf( &drawSurfs[i] );
        drawSurfs[i].vertCount = 0;
        SubdivideDrawSurf( &drawSurfs[i], w, subdivisions );
    }
}


/* SubdivideDrawSurf  0x0043a600 */
void SubdivideDrawSurf( DrawSurf_t *surf, winding_t *w, float subdivisions )
{
    vec3_t     normal;
    vec3_t     point;
    vec3_t     mins;
    vec3_t     maxs;
    float      dist;
    float      epsilon;
    winding_t *front;
    winding_t *back;
    int        lo;
    int        hi;
    unsigned int i;

    epsilon = SUBDIVIDE_EPSILON;

    if ( !w )
        return;

    if ( w->ptCount < 3 )
        Com_Error( "SubdivideDrawSurf: Bad w->ptCount" );

    WindingBounds( w, mins, maxs );

    for ( i = 0; i < 3; i++ )
    {
        Vec3Clear( normal );
        Vec3Clear( point );

        lo = ( int )( floor( ( float )( mins[i] / subdivisions ) ) * subdivisions );
        hi = ( int )( ceil ( ( float )( maxs[i] / subdivisions ) ) * subdivisions );

        point[i]  = ( float )lo + subdivisions;
        normal[i] = -1.0f;
        dist      = Vec3Dot( point, normal );

        if ( ( float )( hi - lo ) > subdivisions )
        {
            ClipWindingEpsilon( w, normal, dist, epsilon, &front, &back, qfalse );

            if ( !front )
            {
                w = back;
            }
            else if ( back )
            {
                SubdivideDrawSurf( surf, front, subdivisions );
                SubdivideDrawSurf( surf, back,  subdivisions );
                return;
            }
            else
            {
                w = front;
            }
        }
    }

    CheckWindingInPlane( w, mapplanes[surf->side->planenum].normal,
                            mapplanes[surf->side->planenum].dist );

    DrawSurfaceForSide( surf->side, w,
                        surf->mapInfoIndex, surf->entityNum,
                        surf->brushNum, surf->outputNumber );
}


/* OpenDebugFile  0x0043a230 */
void OpenDebugFile( void )
{
    char filePath[MAX_OS_PATH_SHORT];

    sprintf( filePath, "%s%s", g_outputBasePath, GetPolyFileExtension() );
    g_debugPolyFile = fopen( filePath, "wt" );
}


/* CloseDebugFile  0x0043a290 */
void CloseDebugFile( void )
{
    if ( g_debugPolyFile )
    {
        fclose( g_debugPolyFile );
        g_debugPolyFile = NULL;
    }
}


/* WriteDebugWindingToFile  0x0043a2c0 */
void WriteDebugWindingToFile( const side_t *side, const winding_t *w )
{
    const Material_t *material;
    unsigned int      i;

    if ( !g_debugPolyFile )
        return;

    material = side->material;
    fprintf( g_debugPolyFile, "%s\n", StringFromOffset( material ) );
    fprintf( g_debugPolyFile, "%i\n", w->ptCount );

    for ( i = 0; i < w->ptCount; i++ )
    {
        fprintf( g_debugPolyFile, "( %g %g %g %g %g )\n",
                 w->pts[i][0], w->pts[i][1], w->pts[i][2],
                 Vec3Dot( w->pts[i], side->texMat[0] ) + side->texMat[0][3],
                 Vec3Dot( w->pts[i], side->texMat[1] ) + side->texMat[1][3] );
    }
}


/* WriteDebugTrisToFile  0x0043a3f0 */
void WriteDebugTrisToFile( const void *triSurf )
{
    const byte     *surf = ( const byte * )triSurf;
    const Material_t *material;
    const byte     *verts;
    const unsigned short *indices;
    unsigned int    v;
    int             indexCount;
    int             i;

    if ( !g_debugPolyFile )
        return;

    material   = *( const Material_t * const * )( surf + 0x00 );
    indexCount = *( const int * )( surf + 0x1c );
    indices    = *( const unsigned short * const * )( surf + 0x20 );
    verts      = *( const byte * const * )( surf + 0x28 );

    for ( i = 0; i < indexCount; i++ )
    {
        if ( i % 3 == 0 )
        {
            fprintf( g_debugPolyFile, "%s\n", StringFromOffset( material ) );
            fprintf( g_debugPolyFile, "3\n" );
        }

        v = indices[i];
        fprintf( g_debugPolyFile, "( %g %g %g %g %g )\n",
                 *( const float * )( verts + v * 0x2c + 0x00 ),
                 *( const float * )( verts + v * 0x2c + 0x04 ),
                 *( const float * )( verts + v * 0x2c + 0x08 ),
                 *( const float * )( verts + v * 0x2c + 0x0c ),
                 *( const float * )( verts + v * 0x2c + 0x10 ) );
    }
}
