/* Original: .\patch.cpp */

#include "cod4map.h"
#include "map.h"
#include "mapfile.h"
#include "materials.h"
#include "mesh.h"
#include "patch.h"
#include "qsort_vc8.h"


extern const char *StringFromOffset( const void *base );                 /* 0x004053a0 */

extern int   LightmapSizeForRange( float lmapMin, float lmapMax );       /* 0x00419150 */


extern int   ParseSmoothingForPatch( const char **parsePos );            /* 0x0041a290 */


extern float sampleScale;                                       /* 0x0052907c */

static int  ParsePatchVertex( const char **parsePos, const orientation_t *orient,
                              float scale, MeshVert_t *patchVert );
static void WeldPatchVerts( MeshVert_t *verts, unsigned int vertCount );
static void PromoteVertexColorToAlpha( parseMesh_t *mesh );
static void *AllocOrUseStack( int count, void *stackBuf, int stackCapacity,
                              int elemSize );
static void FreeIfNotStack( void *ptr, void *stackBuf );
static void EmitTerrainGroup( Entity_t *entity, parseMesh_t **meshList,
                              int meshCount );
static void MeshToTriangles( const parseMesh_t *mesh, int *numTris, int *numVerts,
                             MeshVert_t *verts, unsigned short *indexes );
static int  FindSharedVertex( const MeshVert_t *srcVert, MeshVert_t *pool,
                              int *vertCount );
static void EmitTriangle( unsigned short *indexes, int *numTris, MeshVert_t *verts,
                          unsigned short i0, unsigned short i1, unsigned short i2 );
static void SubdivideTerrain( Entity_t *entity, MeshVert_t *verts, int vertCount,
                              unsigned short *indexes, int triCount,
                              const parseMesh_t *mesh );
static int  CompareTerrainPatches( const void *a, const void *b );


/* PromoteVertexColorToAlpha  0x00428730 */
static void PromoteVertexColorToAlpha( parseMesh_t *mesh )
{
    MeshVert_t *verts;
    int         vertCount;
    int         i;

    if ( ( mesh->toolFlags & TOOLFLAG_USAGE_MASK ) != TOOLFLAG_USAGE_VCOLOR )
    {
        return;
    }

    verts     = ( MeshVert_t * )mesh->verts;
    vertCount = mesh->width * mesh->height;

    for ( i = 0; i < vertCount; i++ )
    {
        if ( verts[i].color[3] != 255 )
        {
            return;
        }
    }

    for ( i = 0; i < vertCount; i++ )
    {
        verts[i].color[3] = ( byte )( ( verts[i].color[0]
                                      + verts[i].color[1]
                                      + verts[i].color[2] ) / 3 );
        verts[i].color[0] = 255;
        verts[i].color[1] = 255;
        verts[i].color[2] = 255;
    }
}


/* ParsePatch  0x00428840 */
void ParsePatch( const char **parsePos, int isMesh, const orientation_t *orient,
                 float scale, int mapInfoIndex, int prefabFlags )
{
    char         materialName[64];
    char         lmapMaterialName[64];
    char         layerName[1024];
    const char  *token;
    parseMesh_t *mesh;
    MeshVert_t  *verts;
    int         *vertFlags;
    int          contentFlags;
    int          toolFlags;
    int          smoothing;
    int          width, height, subdivisions;
    int          i, j, index;

    COM_MatchToken( parsePos, "{", 0 );

    ParseLayerName( parsePos, layerName );
    contentFlags = ParseContentsFlags( parsePos );
    toolFlags    = ParseToolFlags( parsePos );

    Com_SetSpaceDelimited( 1 );
    token = COM_Parse( parsePos );
    strcpy( materialName, token );
    token = COM_Parse( parsePos );
    strcpy( lmapMaterialName, token );
    Com_SetSpaceDelimited( 0 );

    smoothing = ParseSmoothingForPatch( parsePos );

    width        = COM_ParseInt( parsePos );
    height       = COM_ParseInt( parsePos );
                   COM_ParseInt( parsePos );
    subdivisions = COM_ParseInt( parsePos );

    if ( width < 0 || width > MAX_PATCH_SIZE || height < 0 || height > MAX_PATCH_SIZE )
    {
        Com_Error( "ParsePatch: bad size %i x %i", width, height );
    }

    verts     = ( MeshVert_t * )malloc( width * height * sizeof( MeshVert_t ) );
    vertFlags = ( int * )malloc( width * height * sizeof( int ) );

    for ( i = 0; i < width; i++ )
    {
        COM_MatchToken( parsePos, "(", 0 );

        for ( j = 0; j < height; j++ )
        {
            index = j * width + i;
            vertFlags[index] = ParsePatchVertex( parsePos, orient, scale,
                                                 &verts[index] );
        }

        COM_MatchToken( parsePos, ")", 0 );
    }

    COM_MatchToken( parsePos, "}", 0 );
    COM_MatchToken( parsePos, "}", 0 );

    if ( noCurveBrushes )
    {
        return;
    }

    WeldPatchVerts( verts, width * height );

    mesh = ( parseMesh_t * )malloc( sizeof( parseMesh_t ) );
    memset( mesh, 0, sizeof( parseMesh_t ) );
    mesh->cullGroup = -1;

    mesh->material     = Material_Register( materialName,     MTL_USAGE_WORLD_VCOL )->material;
    mesh->lmapMaterial = Material_Register( lmapMaterialName, MTL_USAGE_WORLD_VCOL )->material;
    mesh->smoothing    = smoothing;

    mesh->width        = width;
    mesh->height       = height;
    mesh->subdivisions = subdivisions;
    mesh->verts        = verts;
    mesh->vertFlags    = vertFlags;
    mesh->isMesh       = ( byte )isMesh;

    mesh->entitynum    = map_entity_num - 1;
    mesh->brushnum     = entitySourceBrushes;
    mesh->mapInfoIndex = mapInfoIndex;

    mesh->contents = mesh->material->contentFlags;

    if ( contentFlags & ( CONTENTS_CLIPSHOT | CONTENTS_MISSILECLIP ) )
    {
        mesh->contents &= ~( CONTENTS_DETAIL | CONTENTS_SOLID );
    }
    if ( contentFlags & CONTENTS_CLIPSHOT )
    {
        mesh->contents |= CONTENTS_CLIPSHOT;
    }
    if ( contentFlags & CONTENTS_MISSILECLIP )
    {
        mesh->contents |= CONTENTS_MISSILECLIP;
    }
    if ( contentFlags & CONTENTS_NONCOLLIDING )
    {
        mesh->contents |= CONTENTS_NONCOLLIDING;
    }

    mesh->toolFlags = mesh->material->toolFlags | toolFlags;

    PromoteVertexColorToAlpha( mesh );

    mesh->next      = mapent->patches;
    mapent->patches = mesh;
}


/* ParsePatchVertex  0x00428cd0 */
static int ParsePatchVertex( const char **parsePos, const orientation_t *orient,
                             float scale, MeshVert_t *patchVert )
{
    vec3_t      pos;
    const char *token;
    int         flags;

    Assert( patchVert );

    COM_MatchToken( parsePos, "v", 0 );

    pos[0] = COM_ParseFloat( parsePos );
    pos[1] = COM_ParseFloat( parsePos );
    pos[2] = COM_ParseFloat( parsePos );

    TransformPointScaled( orient, scale, pos, patchVert->xyz );

    token = COM_Parse( parsePos );

    if ( !strcmp( token, "c" ) )
    {
        patchVert->color[2] = ( byte )COM_ParseIntExt( parsePos );
        patchVert->color[1] = ( byte )COM_ParseIntExt( parsePos );
        patchVert->color[0] = ( byte )COM_ParseIntExt( parsePos );
        patchVert->color[3] = ( byte )COM_ParseIntExt( parsePos );
    }
    else
    {
        Com_UngetToken();
        PackColor( ( unsigned int * )patchVert->color, 255, 255, 255, 255 );
    }

    COM_MatchToken( parsePos, "t", 0 );

    patchVert->st[0]   = ( float )( COM_ParseFloat( parsePos ) * PATCH_TEXCOORD_SCALE );
    patchVert->st[1]   = ( float )( COM_ParseFloat( parsePos ) * PATCH_TEXCOORD_SCALE );
    patchVert->lmSt[0] = ( float )( COM_ParseFloat( parsePos ) * PATCH_TEXCOORD_SCALE
                                    * sampleScale );
    patchVert->lmSt[1] = ( float )( COM_ParseFloat( parsePos ) * PATCH_TEXCOORD_SCALE
                                    * sampleScale );

    token = COM_Parse( parsePos );

    if ( !strcmp( token, "f" ) )
    {
        flags = COM_ParseIntExt( parsePos );
    }
    else
    {
        flags = 0;
        Com_UngetToken();
    }

    return flags;
}


/* WeldPatchVerts  0x00428ec0 */
static void WeldPatchVerts( MeshVert_t *verts, unsigned int vertCount )
{
    unsigned int vertMap[MAX_PATCH_SIZE * MAX_PATCH_SIZE];
    vec3_t       sum[MAX_PATCH_SIZE * MAX_PATCH_SIZE];
    unsigned int weldCount[MAX_PATCH_SIZE * MAX_PATCH_SIZE];
    unsigned int vertIter;
    unsigned int otherIter;
    unsigned int other;

    for ( vertIter = 0; vertIter < vertCount; vertIter++ )
    {
        vertMap[vertIter]   = vertIter;
        weldCount[vertIter] = 1;
        Vec3Copy( verts[vertIter].xyz, sum[vertIter] );

        for ( otherIter = 0; otherIter < vertIter; otherIter++ )
        {
            other = vertMap[otherIter];

            if ( Vec3CompareEpsilon( verts[other].xyz, verts[vertIter].xyz,
                                     PATCH_WELD_EPSILON ) )
            {
                vertMap[vertIter] = other;
                weldCount[other]++;
                Vec3Add( sum[vertIter], sum[other], sum[other] );
            }
        }
    }

    for ( vertIter = 0; vertIter < vertCount; vertIter++ )
    {
        AssertCmp( vertMap[vertIter], <=, vertIter );

        if ( vertMap[vertIter] == vertIter )
        {
            if ( weldCount[vertIter] == 1 )
            {
                continue;
            }

            sum[vertIter][0] = sum[vertIter][0] / ( float )weldCount[vertIter];
            sum[vertIter][1] = sum[vertIter][1] / ( float )weldCount[vertIter];
            sum[vertIter][2] = sum[vertIter][2] / ( float )weldCount[vertIter];
        }

        Vec3Copy( sum[vertMap[vertIter]], verts[vertIter].xyz );
    }
}


/* ProcessEntityPatches  0x00429160 */
void ProcessEntityPatches( Entity_t *entity )
{
    parseMesh_t  *stackList[1024];
    parseMesh_t **list;
    parseMesh_t  *pm;
    int           terrainCount;
    int           first;
    int           runLength;
    int           i;

    terrainCount = 0;

    for ( pm = ( parseMesh_t * )entity->patches; pm; pm = pm->next )
    {
        if ( pm->isMesh )
        {
            terrainCount++;
        }
    }

    Assert( terrainCount >= 0 );

    if ( !terrainCount )
    {
        return;
    }

    list = ( parseMesh_t ** )AllocOrUseStack( terrainCount, stackList,
                                              1024, sizeof( parseMesh_t * ) );

    terrainCount = 0;

    for ( pm = ( parseMesh_t * )entity->patches; pm; pm = pm->next )
    {
        if ( pm->isMesh )
        {
            list[terrainCount] = pm;
            terrainCount++;
        }
    }

    qsort_vc8( list, terrainCount, sizeof( list[0] ), CompareTerrainPatches );

    first     = 0;
    runLength = 1;

    for ( i = 1; i < terrainCount; i++ )
    {
        if ( CompareTerrainPatches( &list[i - 1], &list[i] ) == 0 )
        {
            runLength++;
        }
        else
        {
            EmitTerrainGroup( entity, &list[first], runLength );
            first     = i;
            runLength = 1;
        }
    }

    EmitTerrainGroup( entity, &list[first], runLength );

    FreeIfNotStack( list, stackList );
}


/* AllocOrUseStack  0x00429360 */
static void *AllocOrUseStack( int count, void *stackBuf, int stackCapacity,
                              int elemSize )
{
    if ( count > stackCapacity )
    {
        return malloc( count * elemSize );
    }

    return stackBuf;
}


/* FreeIfNotStack  0x00429390 */
static void FreeIfNotStack( void *ptr, void *stackBuf )
{
    if ( ptr != stackBuf )
    {
        free( ptr );
    }
}


/* EmitTerrainGroup  0x004293b0 */
static void EmitTerrainGroup( Entity_t *entity, parseMesh_t **meshList,
                              int meshCount )
{
    unsigned short  indexScratchBuf[0x4000];
    unsigned short  indexBuf[0x4000];
    MeshVert_t      vertBuf[0x1000];
    unsigned short *indexScratch;
    unsigned short *indexes;
    MeshVert_t     *verts;
    int             triCount;
    int             vertCount;
    int             i;

    triCount  = 0;
    vertCount = 0;

    for ( i = 0; i < meshCount; i++ )
    {
        triCount  += ( meshList[i]->width - 1 ) * ( meshList[i]->height - 1 ) * 2;
        vertCount += meshList[i]->width * meshList[i]->height;
    }

    indexScratch = ( unsigned short * )AllocOrUseStack( triCount, indexScratchBuf,
                                                       0x4000, 2 );
    indexes      = ( unsigned short * )AllocOrUseStack( triCount * 3, indexBuf,
                                                       0x4000, 2 );
    verts        = ( MeshVert_t * )AllocOrUseStack( vertCount, vertBuf,
                                                    0x1000, sizeof( MeshVert_t ) );

    for ( i = 0; i < meshCount; i++ )
    {
        MeshToTriangles( meshList[i], &triCount, &vertCount, verts, indexes );
        SubdivideTerrain( entity, verts, vertCount, indexes, triCount, meshList[i] );
    }

    FreeIfNotStack( indexes, indexBuf );
    FreeIfNotStack( verts, vertBuf );
}


/* MeshToTriangles  0x004295a0 */
static void MeshToTriangles( const parseMesh_t *mesh, int *numTris, int *numVerts,
                             MeshVert_t *verts, unsigned short *indexes )
{
    unsigned short vertIndex[0x20000];
    int            i00, i01, i10, i11;
    int            i, row, col;

    *numTris  = 0;
    *numVerts = 0;

    for ( i = 0; i < mesh->width * mesh->height; i++ )
    {
        vertIndex[i] = ( unsigned short )FindSharedVertex(
                           &( ( MeshVert_t * )mesh->verts )[i], verts, numVerts );
    }

    for ( row = 0; row < mesh->height - 1; row++ )
    {
        for ( col = 0; col < mesh->width - 1; col++ )
        {
            i00 = row * mesh->width + col;
            i01 = i00 + mesh->width;
            i10 = i00 + 1;
            i11 = i01 + 1;

            if ( mesh->vertFlags[i00] & 1 )
            {
                EmitTriangle( indexes, numTris, verts,
                              vertIndex[i00], vertIndex[i11], vertIndex[i10] );
                EmitTriangle( indexes, numTris, verts,
                              vertIndex[i11], vertIndex[i00], vertIndex[i01] );
            }
            else
            {
                EmitTriangle( indexes, numTris, verts,
                              vertIndex[i01], vertIndex[i10], vertIndex[i00] );
                EmitTriangle( indexes, numTris, verts,
                              vertIndex[i10], vertIndex[i01], vertIndex[i11] );
            }
        }
    }

    for ( i = 0; i < *numVerts; i++ )
    {
        Vec3Normalize( verts[i].normal );
    }
}


/* FindSharedVertex  0x00429820 */
static int FindSharedVertex( const MeshVert_t *srcVert, MeshVert_t *pool,
                             int *vertCount )
{
    int i;

    for ( i = 0; i < *vertCount; i++ )
    {
        if ( Vec3Equal( pool[i].xyz, srcVert->xyz )
             && fabs( pool[i].st[0] - srcVert->st[0] ) <= PATCH_ST_EPSILON
             && fabs( pool[i].st[1] - srcVert->st[1] ) <= PATCH_ST_EPSILON )
        {
            return i;
        }
    }

    Assert( i == *vertCount );

    ( *vertCount )++;
    pool[i] = *srcVert;
    Vec3Clear( pool[i].normal );

    return i;
}


/* EmitTriangle  0x00429950 */
static void EmitTriangle( unsigned short *indexes, int *numTris, MeshVert_t *verts,
                          unsigned short i0, unsigned short i1, unsigned short i2 )
{
    vec4_t plane;

    if ( i0 != i1 && i1 != i2 && i2 != i0
         && PlaneFromPoints( plane, verts[i0].xyz, verts[i1].xyz, verts[i2].xyz ) )
    {
        Vec3Add( verts[i0].normal, plane, verts[i0].normal );
        Vec3Add( verts[i1].normal, plane, verts[i1].normal );
        Vec3Add( verts[i2].normal, plane, verts[i2].normal );

        indexes[*numTris * 3 + 0] = i0;
        indexes[*numTris * 3 + 1] = i1;
        indexes[*numTris * 3 + 2] = i2;
        ( *numTris )++;
    }
}


/* SubdivideTerrain  0x00429a80 */
static void SubdivideTerrain( Entity_t *entity, MeshVert_t *verts, int vertCount,
                              unsigned short *indexes, int triCount,
                              const parseMesh_t *mesh )
{
    vec3_t          mins, maxs, size;
    vec2_t          lmMins, lmMaxs;
    unsigned short *splitIndexes[MAX_LIGHTMAP_SPLITS];
    MeshVert_t     *splitVerts[MAX_LIGHTMAP_SPLITS];
    unsigned short *splitVertMap[MAX_LIGHTMAP_SPLITS];
    unsigned short  splitVertCount[MAX_LIGHTMAP_SPLITS];
    unsigned short  splitTriCount[MAX_LIGHTMAP_SPLITS];
    TerrainNode_t  *terrain;
    int             splitCount;
    int             axis;
    int             lmapWidth, lmapHeight;
    int             usedGrids;
    float           splitStep;
    float           center;
    unsigned short  i0, i1, i2;
    int             grid;
    int             i;

    ClearBounds( mins, maxs );
    ClearBounds2D( lmMins, lmMaxs );

    for ( i = 0; i < vertCount; i++ )
    {
        AddPointToBounds( verts[i].xyz, mins, maxs );
        AddPointToBounds2D( verts[i].lmSt, lmMins, lmMaxs );
    }

    ExpandBounds( TERRAIN_BOUNDS_SLOP, mins, maxs );
    Vec3Sub( maxs, mins, size );

    lmapWidth  = LightmapSizeForRange( lmMins[0], lmMaxs[0] );
    lmapHeight = LightmapSizeForRange( lmMins[1], lmMaxs[1] );

    splitCount = 0;
    axis       = 0;

    if ( ( mesh->material->techSetFlags & 2 )
         && ( lmapWidth > MAX_TERRAIN_LMAP_AXIS || lmapHeight > MAX_TERRAIN_LMAP_AXIS ) )
    {
        axis       = VecLargestAxis( size );
        splitCount = ( int )ceil( ( float )( I_max( lmapWidth, lmapHeight )
                                             * LMAP_SPLIT_SCALE ) );
    }

    if ( splitCount )
    {
        Assert( splitCount >= 2 );

        splitStep = ( float )splitCount / size[axis];
        Assert( splitStep > 0 );

        if ( splitCount > MAX_LIGHTMAP_SPLITS )
        {
            Com_Error( "Terrain mesh using material %s exceeded maximum lightmap "
                       "splits (%i > %i)\nThis probably means the terrain has "
                       "lightmap density set way too high.\nLook around "
                       "(%g %g %g)\n",
                       StringFromOffset( mesh->material ),
                       splitCount, MAX_LIGHTMAP_SPLITS,
                       verts[vertCount / 2].xyz[0],
                       verts[vertCount / 2].xyz[1],
                       verts[vertCount / 2].xyz[2] );
        }

        for ( i = 0; i < splitCount; i++ )
        {
            splitIndexes[i] = ( unsigned short * )malloc( triCount * 3 * 2 );
            splitVerts[i]   = ( MeshVert_t * )malloc( vertCount * sizeof( MeshVert_t ) );
            splitVertMap[i] = ( unsigned short * )malloc( vertCount * 2 );
            memset( splitVertMap[i], -1, vertCount * 2 );
            splitVertCount[i] = 0;
            splitTriCount[i]  = 0;
        }

        usedGrids = 0;

        for ( i = 0; i < triCount; i++ )
        {
            i0 = indexes[i * 3 + 0];
            i1 = indexes[i * 3 + 1];
            i2 = indexes[i * 3 + 2];

            center = ( verts[i0].xyz[axis] + verts[i1].xyz[axis]
                       + verts[i2].xyz[axis] ) / 3.0f;

            grid = ( int )floor( ( center - mins[axis] ) * splitStep );

            Assertx( grid >= 0 && grid < splitCount, "%s",
                     va( "%g %g %g -> %i", center, mins[axis], splitStep, grid ) );

            Assert( i0 < vertCount );
            Assert( i1 < vertCount );
            Assert( i2 < vertCount );

            if ( ( short )splitVertMap[grid][i0] == -1 )
            {
                splitVertMap[grid][i0] = splitVertCount[grid];
                splitVerts[grid][ splitVertMap[grid][i0] ] = verts[i0];
                splitVertCount[grid]++;
            }

            if ( ( short )splitVertMap[grid][i1] == -1 )
            {
                splitVertMap[grid][i1] = splitVertCount[grid];
                splitVerts[grid][ splitVertMap[grid][i1] ] = verts[i1];
                splitVertCount[grid]++;
            }

            if ( ( short )splitVertMap[grid][i2] == -1 )
            {
                splitVertMap[grid][i2] = splitVertCount[grid];
                splitVerts[grid][ splitVertMap[grid][i2] ] = verts[i2];
                splitVertCount[grid]++;
            }

            Assert( splitVertCount[grid] <= vertCount );

            splitIndexes[grid][ splitTriCount[grid] * 3 + 0 ] = splitVertMap[grid][i0];
            splitIndexes[grid][ splitTriCount[grid] * 3 + 1 ] = splitVertMap[grid][i1];
            splitIndexes[grid][ splitTriCount[grid] * 3 + 2 ] = splitVertMap[grid][i2];

            if ( splitTriCount[grid] == 0 )
            {
                usedGrids++;
            }

            splitTriCount[grid]++;
        }

        for ( i = 0; i < splitCount; i++ )
        {
            if ( usedGrids > 1 && splitVertCount[i] )
            {
                SubdivideTerrain( entity, splitVerts[i], splitVertCount[i],
                                  splitIndexes[i], splitTriCount[i], mesh );
            }

            free( splitIndexes[i] );
            free( splitVerts[i] );
            free( splitVertMap[i] );
        }

        if ( usedGrids > 1 )
        {
            return;
        }
    }

    terrain = ( TerrainNode_t * )malloc( sizeof( TerrainNode_t ) );

    terrain->next   = ( TerrainNode_t * )entity->terrain;
    entity->terrain = terrain;

    terrain->material     = mesh->material;
    terrain->lmapMaterial = mesh->lmapMaterial;
    terrain->contents     = mesh->contents;
    terrain->toolFlags    = mesh->toolFlags;
    terrain->smoothing    = mesh->smoothing;
    terrain->cullGroup    = mesh->cullGroup;

    terrain->vertCount = vertCount;
    terrain->verts     = ( MeshVert_t * )malloc( terrain->vertCount * sizeof( MeshVert_t ) );
    memcpy( terrain->verts, verts, terrain->vertCount * sizeof( MeshVert_t ) );

    terrain->indexCount = triCount * 3;
    terrain->indexes    = ( unsigned short * )malloc( terrain->indexCount * 2 );
    memcpy( terrain->indexes, indexes, terrain->indexCount * 2 );

    if ( ( terrain->material->surfaceFlags & SURF_NODRAW )
         && !( terrain->material->surfaceFlags & SURF_NOCASTSHADOW ) )
    {
        WriteDebugTrisToFile( terrain );
    }

    DrawSurfaceForTerrain( terrain, mesh );
}


/* DrawSurfaceForTerrain  0x0042a4f0 */
DrawSurf_t *DrawSurfaceForTerrain( const TerrainNode_t *terrain,
                                   const parseMesh_t *mesh )
{
    DrawSurf_t *surf;
    int         i;

    surf = AllocDrawSurface();

    surf->entityNum    = mesh->entitynum;
    surf->brushNum     = mesh->brushnum;
    surf->mapInfoIndex = mesh->mapInfoIndex;

    surf->mtlTex     = terrain->material;
    surf->lmapMaterial = terrain->lmapMaterial;
    surf->contents     = terrain->contents;
    surf->toolFlags    = terrain->toolFlags;
    surf->smoothing    = terrain->smoothing;

    surf->isPatch   = 0;
    surf->isModel   = 1;

    surf->terrain.indexCount = terrain->indexCount;
    surf->terrain.indexes    = ( int * )malloc( surf->terrain.indexCount * 4 );

    for ( i = 0; i < surf->terrain.indexCount; i++ )
    {
        surf->terrain.indexes[i] = terrain->indexes[i];
    }

    surf->vertCount = terrain->vertCount;
    surf->verts     = ( DrawVert_t * )malloc( surf->vertCount * sizeof( MeshVert_t ) );
    memcpy( surf->verts, terrain->verts, surf->vertCount * sizeof( MeshVert_t ) );

    CenterPatchLightmapCoords( surf );

    surf->outputNumber = terrain->cullGroup;

    return surf;
}


/* CenterPatchLightmapCoords  0x0042a630 */
void CenterPatchLightmapCoords( DrawSurf_t *surf )
{
    float sMin, tMin, sMax, tMax;
    int   sShift, tShift;
    int   i;

    if ( !surf || !surf->mtlTex || !( surf->mtlTex->toolFlags & 0x80 ) )
    {
        return;
    }

    sMin = surf->verts[0].st[0];
    tMin = surf->verts[0].st[1];
    sMax = surf->verts[0].st[0];
    tMax = surf->verts[0].st[1];

    for ( i = 1; i < surf->vertCount; i++ )
    {
        if ( surf->verts[i].st[0] < sMin )
        {
            sMin = surf->verts[i].st[0];
        }
        else if ( surf->verts[i].st[0] > sMax )
        {
            sMax = surf->verts[i].st[0];
        }

        if ( surf->verts[i].st[1] < tMin )
        {
            tMin = surf->verts[i].st[1];
        }
        else if ( surf->verts[i].st[1] > tMax )
        {
            tMax = surf->verts[i].st[1];
        }
    }

    sShift = FloorFloatToInt( ( sMin + sMax ) * 0.5f );
    tShift = FloorFloatToInt( ( tMin + tMax ) * 0.5f );

    for ( i = 0; i < surf->vertCount; i++ )
    {
        surf->verts[i].st[0] = surf->verts[i].st[0] - ( float )sShift;
        surf->verts[i].st[1] = surf->verts[i].st[1] - ( float )tShift;
    }
}


/* ExpandBounds  0x0042a830 */
void ExpandBounds( float dist, vec3_t mins, vec3_t maxs )
{
    mins[0] = mins[0] - dist;
    mins[1] = mins[1] - dist;
    mins[2] = mins[2] - dist;
    maxs[0] = maxs[0] + dist;
    maxs[1] = maxs[1] + dist;
    maxs[2] = maxs[2] + dist;
}


/* CompareTerrainPatches  0x0042a890 */
static int CompareTerrainPatches( const void *a, const void *b )
{
    const parseMesh_t *pa = *( const parseMesh_t * const * )a;
    const parseMesh_t *pb = *( const parseMesh_t * const * )b;

    if ( pa->contents != pb->contents )
    {
        return pa->contents - pb->contents;
    }

    if ( pa->toolFlags != pb->toolFlags )
    {
        return pa->toolFlags - pb->toolFlags;
    }

    if ( pa->material != pb->material )
    {
        return ( int )( ( ( int )pa->material - ( int )pb->material ) >> 6 );
    }

    if ( pa->lmapMaterial != pb->lmapMaterial )
    {
        return ( int )( ( ( int )pa->lmapMaterial - ( int )pb->lmapMaterial ) >> 6 );
    }

    return pa->cullGroup - pb->cullGroup;
}


/* PatchMapDrawSurfs  0x0042a940 */
void PatchMapDrawSurfs( Entity_t *entity )
{
    parseMesh_t *pm;
    DrawSurf_t  *surf;
    int          count;

    Com_DPrintf( "----- PatchMapDrawSurfs -----\n" );

    count = 0;

    for ( pm = ( parseMesh_t * )entity->patches; pm; pm = pm->next )
    {
        if ( !pm->isMesh )
        {
            surf = DrawSurfaceForPatch( pm );

            surf->mtlTex     = pm->material;
            surf->lmapMaterial = pm->lmapMaterial;
            surf->contents     = pm->contents;
            surf->toolFlags    = pm->toolFlags;

            CenterPatchLightmapCoords( surf );
            count++;
        }
    }

    Com_DPrintf( "%5i patches\n", count );
}


/* DrawSurfaceForPatch  0x0042a9f0 */
DrawSurf_t *DrawSurfaceForPatch( const parseMesh_t *mesh )
{
    const Mesh_t *src;
    Mesh_t       *copy;
    DrawSurf_t   *surf;
    vec4_t        plane;
    qboolean      planar;
    int           i, j;

    src  = ( const Mesh_t * )&mesh->width;
    copy = CopyMesh( src );

    PutMeshOnCurve( *copy );
    MakeMeshNormals( copy );

    for ( i = 0; i < src->width; i++ )
    {
        for ( j = 0; j < mesh->height; j++ )
        {
            Vec3Copy( copy->verts[j * src->width + i].normal,
                      ( ( MeshVert_t * )mesh->verts )[j * src->width + i].normal );
        }
    }

    planar = GuessPatchPlane( copy, plane );

    FreeMesh( copy );

    surf = AllocDrawSurface();

    surf->entityNum    = mesh->entitynum;
    surf->brushNum     = mesh->brushnum;
    surf->mapInfoIndex = mesh->mapInfoIndex;

    surf->contents     = mesh->contents;
    surf->toolFlags    = mesh->toolFlags;
    surf->smoothing    = mesh->smoothing;

    surf->isPatch   = 1;
    surf->isModel   = 0;

    surf->subdivisions    = mesh->subdivisions;
    surf->patch.width   = src->width;
    surf->patch.height  = mesh->height;

    surf->vertCount = surf->patch.width * surf->patch.height;
    surf->verts     = ( DrawVert_t * )malloc( surf->vertCount * sizeof( MeshVert_t ) );
    memcpy( surf->verts, mesh->verts, surf->vertCount * sizeof( MeshVert_t ) );

    surf->outputNumber = mesh->cullGroup;

    return surf;
}


/* PackColor  0x0042abd0 */
void PackColor( unsigned int *out, byte r, byte g, byte b, byte a )
{
    *out = ( ( unsigned int )r << 16 )
         | ( ( unsigned int )g << 8 )
         | ( unsigned int )b
         | ( ( unsigned int )a << 24 );
}
