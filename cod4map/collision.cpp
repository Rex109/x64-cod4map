/* Original: .\collision.cpp */

#include "collision.h"

#include "cod4map.h"
#include "bsp.h"
#include "mesh.h"
#include "materials.h"
#include "qsort_vc8.h"

collGlob_t collGlob;                       /* 0x11ba5d58 */
int        g_collNodeLimit = 0x7fffffff;   /* 0x00529078 */


static void  CollisionBegin( void );
static void  CollisionAddSurfaces( const Entity_t *ent );
static void  CollisionAddPatchTris( DrawSurf_t *ds );
static void  CollisionAddMeshTris( DrawSurf_t *ds );
static void  CollisionAddTriangle( const vec3_t v0, const vec3_t v1, const vec3_t v2,
                                   byte alpha, DrawSurf_t *ds );
static CollisionTri_t *CM_AllocCollisionTri( void );
static int   CollisionAddVertex( const vec3_t xyz );
static int   CollisionAddEdge( int vertIndex0, int vertIndex1, int oppositeVert,
                               int contents, const vec4_t plane );
static bool  CollisionEdgeIsConvex( CollisionEdge_t *edge, int contents, const vec4_t plane );
static unsigned CollisionEdgeHash( int vertIndex0, int vertIndex1 );
static int   CollisionTriHash( const unsigned short vertIndices[3] );
static qboolean CollisionFindDuplicateTri( const unsigned short vertIndices[3], int hash,
                                           byte alpha, DrawSurf_t *ds );
static bool  CollisionTriVertsMatch( const unsigned short a[3], const unsigned short b[3] );

static void  CollisionPartitionTris( void );
static void  CM_PartitionTris( void );
static void  CM_SortTrisByPartitionId( void );
static int   CM_ComparePartitionId( const void *a, const void *b );
static void  CM_ForEachPartitionGroup( void ( *callback )( CollisionTri_t *tris, int triCount ) );
static void  CM_SplitGroupByMaterial( CollisionTri_t *triArray, int triCount );
static int   CM_CompareTrisByMaterial( const void *a, const void *b );
static void  CM_SubdivideGroup( CollisionTri_t *triArray, int triCount );
static int   CM_SubdivideTriGroup( CollisionTri_t *triArray, unsigned int triCount );
static void  CollisionComputeTriBounds( const CollisionTri_t *tris, int triCount,
                                        vec3_t mins, vec3_t maxs );
static int   CM_SubdivideTriArray( CollisionTri_t *triArray, int triCount,
                                   int axis, int failureMode );
static void  CM_AssignTriToPartitionSide( CollisionTri_t *tri, int sideIndex,
                                          cmSplit_t *partition, CollisionTri_t *triArray,
                                          int axis );
static void  CM_SwapTris( CollisionTri_t *a, CollisionTri_t *b );
static void  CM_GetAxisSortOrder( const vec3_t extents, int *order );
static void  CM_RecordPartition( CollisionTri_t *tris, int triCount );

static void  CM_BuildTriWindings( Node_t *headnode );
static int   CM_FilterWindingIntoTree( winding_t *w, int partitionId, Node_t *node );
static void  CM_BuildNodeCollision_r( Node_t *node );
static void  CM_BuildLeafCollision( Node_t *leaf );

static CmCollideBox_t *CM_BuildCollideBoxTree_r( CmCollideBox_t *list, int listCount );
static qboolean CM_ChooseCollideBoxSplit( CmCollideBox_t *list, int listCount,
                                          unsigned int *splitAxis, float *splitValue );
static int   CM_ScoreCollideBoxSplitAxis( CmCollideBox_t *list, int listCount,
                                          unsigned int axis, float *splitValue );
static float CM_CollideBoxListAverage( CmCollideBox_t *list, int listCount, int axis );
static int   CM_CountCollideBoxSplit( CmCollideBox_t *list, int listCount, int axis,
                                      float splitValue );
static void  CM_SplitCollideBoxList( CmCollideBox_t **list, int *listCount,
                                     CmCollideBox_t **listRight, int *listCountRight,
                                     int axis, float splitValue );
static void  CM_AddCollideBoxToList( CmCollideBox_t **list, int *listCount,
                                     CmCollideBox_t *aabb );
static void  CM_AppendCollideBoxList( CmCollideBox_t *aabbList, CmCollideBox_t *tail );
static CmCollideBox_t *CM_AllocCollideBoxNode( CmCollideBox_t *children );
static CmCollideBox_t *CM_AllocCollideBoxLeaf( CmPartition_t *partition,
                                               CmCollideBox_t *next );
static int   CM_ComparePartitions( const CmPartition_t *a, const CmPartition_t *b );
static int   CM_ComparePartitionPtrs( const void *a, const void *b );

static void  CM_EmitCollision( void );
static void  CM_EmitCollisionTris( CollisionTri_t *tris, int triCount );
static void  CM_SetCollisionEdgeWalkBit( const CollisionEdge_t *edge, int triIndex, int edge3 );
static byte  CM_EmitCollisionBorders( CollisionTri_t *tris, int triCount );


/* EmitLeafBrushes  0x0040f010 */
void EmitLeafBrushes( Entity_t *ent, Tree_t *tree )
{
    if ( !entity_num )
        printf( "\nbuilding curve/terrain collision...\n" );

    CollisionBegin();
    CollisionAddSurfaces( ent );

    if ( collGlob.triCount != collGlob.firstTri )
    {
        CollisionPartitionTris();
        CM_BuildTriWindings( tree->headnode );
        CM_BuildNodeCollision_r( tree->headnode );
        CM_EmitCollision();
    }
}


/* CollisionBegin  0x0040f070 */
static void CollisionBegin( void )
{
    if ( collGlob.initialized )
    {
        collGlob.firstTri  = collGlob.triCount;
        collGlob.firstVert = collGlob.vertCount;
        collGlob.firstEdge = collGlob.edgeCount;
        collGlob.firstPart = collGlob.partCount;
        return;
    }

    collGlob.initialized = 1;

    collGlob.firstTri = 0;
    collGlob.maxTris  = MAX_COLLISION_TRIS;
    collGlob.triCount = 0;
    collGlob.tris     = new CollisionTri_t[collGlob.maxTris];

    collGlob.firstVert = 0;
    collGlob.maxVerts  = MAX_COLLISION_VERTS;
    collGlob.vertCount = 0;
    collGlob.verts     = new CmVert_t[collGlob.maxVerts];

    collGlob.maxEdges  = MAX_COLLISION_EDGES;
    collGlob.edgeCount = 0;
    collGlob.edges     = new CollisionEdge_t[collGlob.maxEdges];
    memset( collGlob.edgeHash, 0, sizeof( collGlob.edgeHash ) );

    collGlob.firstPart = 0;
    collGlob.maxParts  = MAX_COLLISION_PARTS;
    collGlob.partCount = 0;
    collGlob.parts     = new CmPartition_t[collGlob.maxParts];

    collGlob.minTrisPerLeaf = CM_MIN_TRIS_PER_LEAF;
    Vec3Set( collGlob.maxPartitionExtent,
             CM_MAX_PARTITION_EXTENT, CM_MAX_PARTITION_EXTENT, CM_MAX_PARTITION_EXTENT );
    collGlob.overlapRatio = CM_OVERLAP_RATIO;
}


/* CollisionAddSurfaces  0x0040f230 */
static void CollisionAddSurfaces( const Entity_t *ent )
{
    DrawSurf_t *ds;
    int         i;

    for ( i = ent->firstDrawSurf; i < numMapDrawSurfs; i++ )
    {
        ds = &drawSurfs[i];

        if ( !ds->vertCount || !ds->contents || ( ds->contents & CONTENTS_NONCOLLIDING ) )
            continue;

        if ( ds->isPatch )
            CollisionAddPatchTris( ds );
        else if ( ds->isModel )
            CollisionAddMeshTris( ds );
    }
}


/* CollisionAddPatchTris  0x0040f2c0 */
static void CollisionAddPatchTris( DrawSurf_t *ds )
{
    Mesh_t  src;
    Mesh_t *mesh;
    Mesh_t *reduced;
    int     i;
    int     j;
    int     i0;
    int     i1;

    src.width  = ds->patch.width;
    src.height = ds->patch.height;
    src.verts  = ( MeshVert_t * )ds->verts;

    mesh = SubdivideMesh( src, ( float )ds->subdivisions, 999.0f, NULL, NULL );
    PutMeshOnCurve( *mesh );
    reduced = RemoveLinearMeshColumnsRows( mesh, NULL, NULL );
    FreeMesh( mesh );

    for ( i = 0; i < reduced->width - 1; i++ )
    {
        for ( j = 0; j < reduced->height - 1; j++ )
        {
            i0 = j * reduced->width + i;
            i1 = i0 + reduced->width;

            CollisionAddTriangle( reduced->verts[i1].xyz,
                                  reduced->verts[i1 + 1].xyz,
                                  reduced->verts[i0 + 1].xyz, 0xff, ds );

            CollisionAddTriangle( reduced->verts[i0 + 1].xyz,
                                  reduced->verts[i0].xyz,
                                  reduced->verts[i1].xyz, 0xff, ds );
        }
    }

    FreeMesh( reduced );
}


/* CollisionAddMeshTris  0x0040ffc0 */
static void CollisionAddMeshTris( DrawSurf_t *ds )
{
    Material_t *material;
    DrawVert_t *verts;
    int         i0;
    int         i1;
    int         i2;
    byte        alpha;
    int         i;

    for ( i = 0; i < ds->terrain.indexCount; i += 3 )
    {
        i0 = ds->terrain.indexes[i];
        i1 = ds->terrain.indexes[i + 1];
        i2 = ds->terrain.indexes[i + 2];

        verts = ds->verts;
        material = ds->mtlTex;

        if ( ( material->toolFlags & 0x70 ) == 0x10 )
        {
            alpha = 0xff;
        }
        else
        {
            alpha = ( byte )( ( verts[i0].color[3] + verts[i1].color[3]
                              + verts[i2].color[3] + 2 ) / 3 );
            if ( !alpha )
                continue;
        }

        CollisionAddTriangle( verts[i0].xyz, verts[i1].xyz, verts[i2].xyz, alpha, ds );
    }
}


/* CollisionAddTriangle  0x0040f470 */
static void CollisionAddTriangle( const vec3_t v0, const vec3_t v1, const vec3_t v2,
                                  byte alpha, DrawSurf_t *ds )
{
    CollisionTri_t *tri;
    vec4_t          plane;
    unsigned short  vertIndices[3];
    int             vertHash;
    int             vertCountBefore;

    Assert( ds->contents != CONTENTS_NONE );

    if ( !PlaneFromPoints( plane, v0, v1, v2 ) )
        return;

    vertCountBefore = collGlob.vertCount;

    vertIndices[0] = ( unsigned short )CollisionAddVertex( v0 );
    vertIndices[1] = ( unsigned short )CollisionAddVertex( v1 );
    vertIndices[2] = ( unsigned short )CollisionAddVertex( v2 );

    vertHash = CollisionTriHash( vertIndices );

    if ( vertCountBefore == collGlob.vertCount
      && CollisionFindDuplicateTri( vertIndices, vertHash, alpha, ds ) )
    {
        return;
    }

    tri = CM_AllocCollisionTri();

    Vec3Copy( v0, tri->mins );
    Vec3Copy( v0, tri->maxs );
    AddPointToBounds( v1, tri->mins, tri->maxs );
    AddPointToBounds( v2, tri->mins, tri->maxs );

    tri->ds          = ds;
    tri->partitionId = 0;

    Vec4Copy( plane, tri->plane );
    MakeCollisionVecs( v0, v1, v2, plane, tri->svec, tri->tvec );

    tri->alpha          = alpha;
    tri->vertHash       = vertHash;
    tri->vertIndices[0] = vertIndices[0];
    tri->vertIndices[1] = vertIndices[1];
    tri->vertIndices[2] = vertIndices[2];

    Assert( Vec3Compare( collGlob.verts[tri->vertIndices[0]].xyz, v0 ) );
    Assert( Vec3Compare( collGlob.verts[tri->vertIndices[1]].xyz, v1 ) );
    Assert( Vec3Compare( collGlob.verts[tri->vertIndices[2]].xyz, v2 ) );

    tri->edgeIndices[0] = CollisionAddEdge( tri->vertIndices[1], tri->vertIndices[2],
                                            tri->vertIndices[0], tri->ds->contents, plane );
    tri->edgeIndices[1] = CollisionAddEdge( tri->vertIndices[2], tri->vertIndices[0],
                                            tri->vertIndices[1], tri->ds->contents, plane );
    tri->edgeIndices[2] = CollisionAddEdge( tri->vertIndices[0], tri->vertIndices[1],
                                            tri->vertIndices[2], tri->ds->contents, plane );
}


/* CM_AllocCollisionTri  0x0040f7a0 */
static CollisionTri_t *CM_AllocCollisionTri( void )
{
    int triIndex;

    if ( collGlob.triCount == collGlob.maxTris )
        Com_Error( "max curve/terrain collision triangles (%i) exceeded\n", collGlob.maxTris );

    triIndex = collGlob.triCount;
    collGlob.triCount++;

    return &collGlob.tris[triIndex];
}


/* CollisionAddVertex  0x0040f7f0 */
static int CollisionAddVertex( const vec3_t xyz )
{
    CmVert_t    *cv;
    unsigned int vertIndex;

    vertIndex = collGlob.firstVert;
    cv        = &collGlob.verts[collGlob.firstVert];

    for ( ; ( int )vertIndex < collGlob.vertCount; vertIndex++, cv++ )
    {
        if ( Vec3Compare( xyz, cv->xyz ) )
        {
            Assertx( ( unsigned short )vertIndex == vertIndex, "(vertIndex) = %i", vertIndex );
            return vertIndex;
        }
    }

    if ( collGlob.vertCount == collGlob.maxVerts )
        Com_Error( "max curvenn/terrain collision vertices (%i) exceeded\n", collGlob.maxVerts );

    collGlob.vertCount++;
    cv->partitionId = 0;
    Vec3Copy( xyz, cv->xyz );

    Assertx( ( unsigned short )vertIndex == vertIndex, "(vertIndex) = %i", vertIndex );

    return vertIndex;
}


/* CollisionEdgeHash  0x0040fd00 */
static unsigned CollisionEdgeHash( int vertIndex0, int vertIndex1 )
{
    return ( ( vertIndex0 + 0x7a69 ) * ( vertIndex1 + 0x7a69 ) ) & ( COLLISION_EDGE_HASH_SIZE - 1 );
}


/* CollisionAddEdge  0x0040f910 */
static int CollisionAddEdge( int vertIndex0, int vertIndex1, int oppositeVert,
                             int contents, const vec4_t plane )
{
    CollisionEdge_t *edge;
    unsigned         hash;
    bool             forward;
    bool             backward;

    hash = CollisionEdgeHash( vertIndex0, vertIndex1 );

    for ( edge = collGlob.edgeHash[hash]; edge; edge = edge->hashNext )
    {
        backward = ( vertIndex0 == edge->vertIndices[1] && vertIndex1 == edge->vertIndices[0] );
        forward  = ( vertIndex0 == edge->vertIndices[0] && vertIndex1 == edge->vertIndices[1] );

        if ( !forward && !backward )
            continue;

        edge->useCount++;

        if ( edge->useCount == 2 )
        {
            edge->convex = CollisionEdgeIsConvex( edge, contents, plane );

            if ( !edge->convex )
            {
                if ( !edge->needsBorder
                  && plane[2] * edge->normalZ <= 0.0
                  && plane[2] != edge->normalZ )
                {
                    edge->needsBorder = 1;
                }

                if ( edge->needsBorder && edge->normalZ * edge->normalZ < ( float )( plane[2] * plane[2] ) )
                    edge->oppositeVert = oppositeVert;
            }
        }
        else
        {
            SanityCheck( edge->useCount >= 3 );
            edge->convex      = 0;
            edge->needsBorder = 1;
        }

        if ( !edge->walkable && CM_WALKABLE_NORMAL_Z <= plane[2] )
            edge->walkable = 1;

        return ( int )( edge - collGlob.edges );
    }

    if ( collGlob.edgeCount == collGlob.maxEdges )
        Com_Error( "max curve/terrain collision edges (%i) exceeded\n", collGlob.maxEdges );

    edge = &collGlob.edges[collGlob.edgeCount];
    collGlob.edgeCount++;

    edge->hashNext = collGlob.edgeHash[hash];
    collGlob.edgeHash[hash] = edge;

    edge->useCount       = 1;
    edge->walkable       = ( CM_WALKABLE_NORMAL_Z <= plane[2] );
    edge->needsBorder    = 0;
    edge->convex         = 0;
    edge->contents       = contents;
    edge->normalZ        = plane[2];
    edge->vertIndices[0] = vertIndex0;
    edge->vertIndices[1] = vertIndex1;
    edge->oppositeVert   = oppositeVert;

    return ( int )( edge - collGlob.edges );
}


/* CollisionEdgeIsConvex  0x0040fbe0 */
static bool CollisionEdgeIsConvex( CollisionEdge_t *edge, int contents, const vec4_t plane )
{
    float dist;
    float lengthSq0;
    float lengthSq1;

    if ( edge->contents != contents )
    {
        edge->contents    = 0;
        edge->needsBorder = 1;
        return false;
    }

    if ( edge->needsBorder )
        return false;

    dist = Vec3Dot( plane, collGlob.verts[edge->oppositeVert].xyz ) - plane[3];

    if ( dist <= 0.0 )
        return false;

    lengthSq0 = Vec3DistanceSq( collGlob.verts[edge->vertIndices[0]].xyz,
                                collGlob.verts[edge->oppositeVert].xyz );
    lengthSq1 = Vec3DistanceSq( collGlob.verts[edge->vertIndices[1]].xyz,
                                collGlob.verts[edge->oppositeVert].xyz );

    return I_fmax( lengthSq0, lengthSq1 ) * CM_CONVEX_EPSILON_SQ <= dist * dist;
}


/* CollisionTriHash  0x0040fd20 */
static int CollisionTriHash( const unsigned short vertIndices[3] )
{
    int minIndex;
    int maxIndex;

    minIndex = I_min( I_min( vertIndices[0], vertIndices[1] ), vertIndices[2] );
    maxIndex = I_max( I_max( vertIndices[0], vertIndices[1] ), vertIndices[2] );

    return vertIndices[0] + vertIndices[1] + vertIndices[2]
         + minIndex * 0x100 + maxIndex * 0x10000;
}


/* CollisionFindDuplicateTri  0x0040fdb0 */
static qboolean CollisionFindDuplicateTri( const unsigned short vertIndices[3], int hash,
                                           byte alpha, DrawSurf_t *ds )
{
    CollisionTri_t *tri;
    int             triIndex;

    for ( triIndex = 0; triIndex < collGlob.triCount; triIndex++ )
    {
        tri = &collGlob.tris[triIndex];

        if ( tri->vertHash != hash )
        {
            collGlob.c_hashMisses++;
            continue;
        }

        if ( tri->ds->contents != ds->contents )
        {
            collGlob.c_contentsMisses++;
            continue;
        }

        if ( !CollisionTriVertsMatch( tri->vertIndices, vertIndices ) )
        {
            collGlob.c_vertMisses++;
            continue;
        }

        collGlob.c_dupTris++;

        if ( Material_SortsBefore( ds->mtlTex, tri->ds->mtlTex ) )
        {
            if ( tri->alpha < CM_ALPHA_DOMINANT )
            {
                tri->ds    = ds;
                tri->alpha = alpha;
            }
        }
        else if ( alpha >= CM_ALPHA_DOMINANT )
        {
            tri->ds    = ds;
            tri->alpha = alpha;
        }

        return qtrue;
    }

    return qfalse;
}


/* CollisionTriVertsMatch  0x0040fed0 */
static bool CollisionTriVertsMatch( const unsigned short a[3], const unsigned short b[3] )
{
    if ( a[0] == b[0] )
        return a[1] == b[1] && a[2] == b[2];

    if ( a[1] == b[1] )
        return a[2] == b[2] && a[0] == b[0];

    if ( a[2] == b[2] )
        return a[0] == b[0] && a[1] == b[1];

    return false;
}


/* CollisionPartitionTris  0x004100c0 */
static void CollisionPartitionTris( void )
{
    CM_PartitionTris();
    CM_SortTrisByPartitionId();
    CM_ForEachPartitionGroup( CM_SplitGroupByMaterial );
    CM_ForEachPartitionGroup( CM_SubdivideGroup );
    CM_ForEachPartitionGroup( CM_RecordPartition );
}


/* CM_PartitionTris  0x00410100 */
static void CM_PartitionTris( void )
{
    CollisionTri_t *tri;
    CmVert_t       *verts[3];
    int             triIndex;
    int             seedIndex;
    int             partitionId;
    bool            added;
    int             i;

    seedIndex   = collGlob.firstTri;
    partitionId = collGlob.partCount + 1;

    for ( ;; )
    {
        tri      = &collGlob.tris[seedIndex];
        verts[0] = &collGlob.verts[tri->vertIndices[0]];
        verts[1] = &collGlob.verts[tri->vertIndices[1]];
        verts[2] = &collGlob.verts[tri->vertIndices[2]];

        SanityCheck( verts[0]->partitionId == 0 );
        SanityCheck( verts[1]->partitionId == 0 );
        SanityCheck( verts[2]->partitionId == 0 );

        verts[0]->partitionId = partitionId;
        verts[1]->partitionId = partitionId;
        verts[2]->partitionId = partitionId;

        do
        {
            triIndex  = seedIndex;
            added     = false;
            i         = seedIndex;
            seedIndex = -1;

            SanityCheck( collGlob.tris[triIndex].partitionId == 0 );

            tri = &collGlob.tris[triIndex];

            for ( ; i < collGlob.triCount; i++, tri++ )
            {
                if ( tri->partitionId )
                    continue;

                verts[0] = &collGlob.verts[tri->vertIndices[0]];
                verts[1] = &collGlob.verts[tri->vertIndices[1]];
                verts[2] = &collGlob.verts[tri->vertIndices[2]];

                SanityCheck( verts[0]->partitionId == 0 || verts[0]->partitionId == partitionId );
                SanityCheck( verts[1]->partitionId == 0 || verts[1]->partitionId == partitionId );
                SanityCheck( verts[2]->partitionId == 0 || verts[2]->partitionId == partitionId );

                if ( !verts[0]->partitionId && !verts[1]->partitionId && !verts[2]->partitionId )
                {
                    if ( seedIndex < 0 )
                        seedIndex = i;

                    continue;
                }

                if ( !verts[0]->partitionId )
                {
                    verts[0]->partitionId = partitionId;
                    added = true;
                }

                if ( !verts[1]->partitionId )
                {
                    verts[1]->partitionId = partitionId;
                    added = true;
                }

                if ( !verts[2]->partitionId )
                {
                    verts[2]->partitionId = partitionId;
                    added = true;
                }

                tri->partitionId = partitionId;
            }

            if ( seedIndex < 0 )
            {
                collGlob.partCount = partitionId;
                return;
            }
        }
        while ( added );

        partitionId++;
    }
}


/* CM_SortTrisByPartitionId  0x00410fc0 */
static void CM_SortTrisByPartitionId( void )
{
    qsort_vc8( &collGlob.tris[collGlob.firstTri],
           collGlob.triCount - collGlob.firstTri,
           sizeof( CollisionTri_t ),
           CM_ComparePartitionId );
}


/* CM_ComparePartitionId  0x00411010 */
static int CM_ComparePartitionId( const void *a, const void *b )
{
    return ( ( const CollisionTri_t * )a )->partitionId
         - ( ( const CollisionTri_t * )b )->partitionId;
}


/* CM_ForEachPartitionGroup  0x00410f40 */
static void CM_ForEachPartitionGroup( void ( *callback )( CollisionTri_t *tris, int triCount ) )
{
    CollisionTri_t *tris;
    int             first;
    int             last;

    for ( first = collGlob.firstTri; first < collGlob.triCount; first = last )
    {
        tris = &collGlob.tris[first];

        last = first;
        do
        {
            last++;
            if ( last >= collGlob.triCount )
                break;
        }
        while ( tris->partitionId == collGlob.tris[last].partitionId );

        callback( tris, last - first );
    }
}


/* CM_SplitGroupByMaterial  0x004103e0 */
static void CM_SplitGroupByMaterial( CollisionTri_t *triArray, int triCount )
{
    CollisionTri_t *tri;
    int             partitionId;
    int             i;

    Assert( triArray );
    Assert( triCount );

    qsort_vc8( triArray, triCount, sizeof( CollisionTri_t ), CM_CompareTrisByMaterial );

    partitionId = triArray->partitionId;
    tri         = triArray;

    for ( i = 0; i < triCount - 1; i++, tri++ )
    {
        if ( tri->ds->contents != tri[1].ds->contents
          || tri->ds->mtlTex->surfaceFlags != tri[1].ds->mtlTex->surfaceFlags )
        {
            collGlob.partCount++;
            partitionId = collGlob.partCount;
        }

        tri[1].partitionId = partitionId;
    }
}


/* CM_CompareTrisByMaterial  0x004104f0 */
static int CM_CompareTrisByMaterial( const void *a, const void *b )
{
    const CollisionTri_t *tri0;
    const CollisionTri_t *tri1;
    int                   diff;

    tri0 = ( const CollisionTri_t * )a;
    tri1 = ( const CollisionTri_t * )b;

    Assert( tri0->ds );
    Assert( tri1->ds );

    diff = tri0->ds->contents - tri1->ds->contents;

    if ( !diff )
    {
        Assert( tri0->ds->mtlTex );
        Assert( tri1->ds->mtlTex );

        diff = tri0->ds->mtlTex->surfaceFlags - tri1->ds->mtlTex->surfaceFlags;
    }

    return diff;
}


/* CM_SubdivideGroup  0x004105f0 */
static void CM_SubdivideGroup( CollisionTri_t *triArray, int triCount )
{
    int splitCount;

    if ( triCount < collGlob.minTrisPerLeaf * 2 )
        return;

    splitCount = CM_SubdivideTriGroup( triArray, triCount );

    if ( !splitCount )
        return;

    CM_SubdivideGroup( triArray, splitCount );
    CM_SubdivideGroup( &triArray[splitCount], triCount - splitCount );
}


/* CM_SubdivideTriGroup  0x00410650 */
static int CM_SubdivideTriGroup( CollisionTri_t *triArray, unsigned int triCount )
{
    vec3_t mins;
    vec3_t maxs;
    vec3_t size;
    vec3_t overflow;
    int    order[3];
    bool   mustSplit;
    int    splitCount;
    int    i;

    CollisionComputeTriBounds( triArray, triCount, mins, maxs );

    Vec3Sub( maxs, mins, size );
    Vec3Sub( size, collGlob.maxPartitionExtent, overflow );

    mustSplit = ( triCount != ( triCount & 0xff ) );

    if ( !mustSplit && overflow[0] < 0.0 && overflow[1] < 0.0 && overflow[2] < 0.0 )
        return 0;

    CM_GetAxisSortOrder( overflow, order );

    for ( i = 0; i < 3 && overflow[order[i]] > 0.0; i++ )
    {
        splitCount = CM_SubdivideTriArray( triArray, triCount, order[i],
                                           SUBDIVIDE_FAILURE_ALLOWED );
        if ( splitCount )
            return splitCount;
    }

    if ( !mustSplit )
        return 0;

    return CM_SubdivideTriArray( triArray, triCount, order[0], SUBDIVIDE_FAILURE_FORBIDDEN );
}


/* CollisionComputeTriBounds  0x00410780 */
static void CollisionComputeTriBounds( const CollisionTri_t *tris, int triCount,
                                       vec3_t mins, vec3_t maxs )
{
    int i;

    Vec3Copy( tris->mins, mins );
    Vec3Copy( tris->maxs, maxs );

    for ( i = 1; i < triCount; i++ )
        AddBoundsToBounds( tris[i].mins, tris[i].maxs, mins, maxs );
}


/* CM_SubdivideTriArray  0x00410800 */
static int CM_SubdivideTriArray( CollisionTri_t *triArray, int triCount,
                                 int axis, int failureMode )
{
    cmSplit_t       partition;
    CollisionTri_t *best;
    CollisionTri_t *tri;
    int             triIndex;
    int             i;

    partition.sideMins[0]  =  131072.0f;
    partition.sideMins[1]  =  131072.0f;
    partition.sideMaxs[0]  = -131072.0f;
    partition.sideMaxs[1]  = -131072.0f;
    partition.sideCount[0] = 0;
    partition.sideCount[1] = 0;
    partition.unusedCount  = triCount;

    while ( partition.unusedCount )
    {
        best = &triArray[partition.sideCount[0]];

        if ( partition.sideCount[1] < partition.sideCount[0]
          || partition.sideMaxs[1] - partition.sideMins[1]
           < partition.sideMaxs[0] - partition.sideMins[0] )
        {
            tri = &triArray[partition.sideCount[0] + 1];

            for ( i = 1; i < partition.unusedCount; i++, tri++ )
            {
                if ( best->mins[axis] < tri->mins[axis] )
                    best = tri;
            }

            CM_AssignTriToPartitionSide( best, 1, &partition, triArray, axis );

            i   = 0;
            tri = &triArray[partition.sideCount[0] + partition.unusedCount - 1];

            while ( i < partition.unusedCount )
            {
                SanityCheck( partition.sideMins[1] >= tri->mins[axis] );

                if ( partition.sideMins[1] == tri->mins[axis] )
                    CM_AssignTriToPartitionSide( tri, 1, &partition, triArray, axis );
                else
                    i++;

                tri--;

                SanityCheck( tri == &triArray[partition.sideCount[0]
                                              + partition.unusedCount - 1 - i] );
            }
        }
        else
        {
            tri = &triArray[partition.sideCount[0] + 1];

            for ( i = 1; i < partition.unusedCount; i++, tri++ )
            {
                if ( tri->maxs[axis] < best->maxs[axis] )
                    best = tri;
            }

            CM_AssignTriToPartitionSide( best, 0, &partition, triArray, axis );

            i   = 0;
            tri = &triArray[partition.sideCount[0]];

            while ( i < partition.unusedCount )
            {
                SanityCheck( partition.sideMaxs[0] <= tri->maxs[axis] );

                if ( partition.sideMaxs[0] == tri->maxs[axis] )
                    CM_AssignTriToPartitionSide( tri, 0, &partition, triArray, axis );
                else
                    i++;

                tri++;

                SanityCheck( tri == &triArray[partition.sideCount[0] + i] );
            }
        }
    }

    if ( failureMode == SUBDIVIDE_FAILURE_FORBIDDEN )
    {
        if ( !partition.sideCount[0] || !partition.sideCount[1] )
            partition.sideCount[0] = triCount / 2;
    }
    else
    {
        Assert( failureMode == SUBDIVIDE_FAILURE_ALLOWED );

        if ( !partition.sideCount[0] || !partition.sideCount[1] )
            return 0;

        if ( partition.sideMaxs[0] - partition.sideMins[1] > 0.0f
          && partition.sideMaxs[0] - partition.sideMins[1]
           > ( partition.sideMaxs[1] - partition.sideMins[0] ) * collGlob.overlapRatio )
        {
            return 0;
        }
    }

    collGlob.partCount++;

    tri = triArray;
    for ( i = 0; i < partition.sideCount[0]; i++, tri++ )
        tri->partitionId = collGlob.partCount;

    return partition.sideCount[0];
}


/* CM_AssignTriToPartitionSide  0x00410c00 */
static void CM_AssignTriToPartitionSide( CollisionTri_t *tri, int sideIndex,
                                         cmSplit_t *partition, CollisionTri_t *triArray,
                                         int axis )
{
    Assert( tri );
    Assert( triArray );
    Assert( axis >= 0 && axis < 3 );
    Assert( partition );
    Assert( partition->unusedCount > 0 );

    if ( tri->mins[axis] < partition->sideMins[sideIndex] )
        partition->sideMins[sideIndex] = tri->mins[axis];

    if ( partition->sideMaxs[sideIndex] < tri->maxs[axis] )
        partition->sideMaxs[sideIndex] = tri->maxs[axis];

    Assert( sideIndex == 0 || sideIndex == 1 );

    CM_SwapTris( &triArray[partition->sideCount[0]
                           + ( partition->unusedCount - 1 ) * sideIndex], tri );

    partition->unusedCount--;
    partition->sideCount[sideIndex]++;
}


/* CM_SwapTris  0x00410dc0 */
static void CM_SwapTris( CollisionTri_t *a, CollisionTri_t *b )
{
    CollisionTri_t temp;

    if ( a == b )
        return;

    temp = *a;
    *a   = *b;
    *b   = temp;
}


/* CM_GetAxisSortOrder  0x00410e20 */
static void CM_GetAxisSortOrder( const vec3_t extents, int *order )
{
    order[0] = 0;
    order[1] = 1;
    order[2] = 2;

    if ( extents[0] < extents[1] )
    {
        order[1] = order[0];
        order[0] = 1;
    }

    if ( extents[order[0]] < extents[2] )
    {
        order[2] = order[1];
        order[1] = order[0];
        order[0] = 2;
    }
    else if ( extents[order[1]] < extents[2] )
    {
        order[2] = order[1];
        order[1] = 2;
    }
}


/* CM_RecordPartition  0x00410ee0 */
static void CM_RecordPartition( CollisionTri_t *tris, int triCount )
{
    CmPartition_t *partition;

    partition = &collGlob.parts[tris->partitionId - 1];

    partition->tris      = tris;
    partition->triCount  = triCount;

    CollisionComputeTriBounds( tris, triCount, partition->mins, partition->maxs );

    partition->partIndex = ( int )( partition - collGlob.parts );
}


/* CM_BuildTriWindings  0x00411020 */
static void CM_BuildTriWindings( Node_t *headnode )
{
    CmPartition_t  *partition;
    CollisionTri_t *tri;
    winding_t      *w;
    int             partIndex;
    int             i;

    for ( partIndex = collGlob.firstPart; partIndex < collGlob.partCount; partIndex++ )
    {
        partition = &collGlob.parts[partIndex];
        tri       = partition->tris;

        for ( i = 0; i < partition->triCount; i++, tri++ )
        {
            w = AllocWinding( 3 );
            w->ptCount = 3;

            Vec3Copy( collGlob.verts[tri->vertIndices[0]].xyz, w->pts[0] );
            Vec3Copy( collGlob.verts[tri->vertIndices[1]].xyz, w->pts[1] );
            Vec3Copy( collGlob.verts[tri->vertIndices[2]].xyz, w->pts[2] );

            CM_FilterWindingIntoTree( w, partIndex, headnode );
        }
    }
}


/* CM_FilterWindingIntoTree  0x00411130 */
static int CM_FilterWindingIntoTree( winding_t *w, int partitionId, Node_t *node )
{
    winding_t   *front;
    winding_t   *back;
    cmPartList_t *entry;
    int          count;

    Assert( w );

    if ( node->planenum == PLANENUM_LEAF )
    {
        FreeWinding( w );

        if ( node->opaque == 1 )
            return 0;

        for ( entry = node->partitionList; entry; entry = entry->next )
        {
            if ( entry->partIndex == partitionId )
                return 0;
        }

        entry = new cmPartList_t;
        entry->partIndex = partitionId;
        entry->next = node->partitionList;
        node->partitionList = entry;

        return 1;
    }

    ClipWindingEpsilon( w, mapplanes[node->planenum].normal, mapplanes[node->planenum].dist,
                        0.1f, &front, &back, qtrue );

    Assert( front != w );
    Assert( back != w );

    FreeWinding( w );

    count = 0;

    if ( front )
        count += CM_FilterWindingIntoTree( front, partitionId, node->children[0] );

    if ( back )
        count += CM_FilterWindingIntoTree( back, partitionId, node->children[1] );

    return count;
}


/* CM_BuildNodeCollision_r  0x004112e0 */
static void CM_BuildNodeCollision_r( Node_t *node )
{
    Assert( node );

    if ( node->planenum == PLANENUM_LEAF )
    {
        CM_BuildLeafCollision( node );
        return;
    }

    CM_BuildNodeCollision_r( node->children[0] );
    CM_BuildNodeCollision_r( node->children[1] );
}


/* CM_BuildLeafCollision  0x00411340 */
static void CM_BuildLeafCollision( Node_t *leaf )
{
    CmPartition_t  *partitions[MAX_COLLISION_PARTS];
    CmCollideBox_t *list;
    CmCollideBox_t *tree;
    cmPartList_t   *entry;
    size_t          partitionCount;
    int             partitionIndexFirst;
    int             i;

    Assert( leaf->planenum == PLANENUM_LEAF );

    if ( !leaf->partitionList )
        return;

    partitionCount = 0;

    for ( entry = leaf->partitionList; entry; entry = entry->next )
    {
        partitions[partitionCount] = &collGlob.parts[entry->partIndex];
        partitionCount++;
    }

    qsort_vc8( partitions, partitionCount, sizeof( CmPartition_t * ), CM_ComparePartitionPtrs );

    partitionIndexFirst = 0;

    while ( partitionIndexFirst < ( int )partitionCount )
    {
        list = CM_AllocCollideBoxLeaf( partitions[partitionIndexFirst], NULL );

        for ( i = partitionIndexFirst + 1; i < ( int )partitionCount; i++ )
        {
            if ( CM_ComparePartitions( partitions[i], partitions[partitionIndexFirst] ) )
                break;

            list = CM_AllocCollideBoxLeaf( partitions[i], list );
        }

        SanityCheck( leaf->collisionBoxes == NULL || partitionIndexFirst != 0 );

        tree = CM_BuildCollideBoxTree_r( list, i - partitionIndexFirst );
        tree->next = leaf->collisionBoxes;
        leaf->collisionBoxes = tree;

        partitionIndexFirst = i;
    }
}


/* CM_BuildCollideBoxTree_r  0x00411510 */
static CmCollideBox_t *CM_BuildCollideBoxTree_r( CmCollideBox_t *list, int listCount )
{
    CmCollideBox_t *listRight;
    unsigned int    splitAxis;
    float           splitValue;
    int             listCountRight;

    if ( listCount == 1 )
        return list;

    if ( CM_ChooseCollideBoxSplit( list, listCount, &splitAxis, &splitValue ) )
    {
        listRight      = NULL;
        listCountRight = 0;

        CM_SplitCollideBoxList( &list, &listCount, &listRight, &listCountRight,
                                splitAxis, splitValue );

        Assert( listCount > 0 );
        Assert( listCountRight > 0 );

        list      = CM_BuildCollideBoxTree_r( list, listCount );
        listRight = CM_BuildCollideBoxTree_r( listRight, listCountRight );

        if ( listCount + listCountRight < g_collNodeLimit )
        {
            CM_AppendCollideBoxList( list, listRight );
            listCount += listCountRight;
        }
        else
        {
            list      = CM_AllocCollideBoxNode( list );
            listRight = CM_AllocCollideBoxNode( listRight );
            CM_AppendCollideBoxList( list, listRight );
        }
    }

    return CM_AllocCollideBoxNode( list );
}


/* CM_ChooseCollideBoxSplit  0x00411660 */
static qboolean CM_ChooseCollideBoxSplit( CmCollideBox_t *list, int listCount,
                                          unsigned int *splitAxis, float *splitValue )
{
    float        value;
    int          score;
    int          bestScore;
    unsigned int axis;

    if ( listCount < g_collNodeLimit )
        return qfalse;

    bestScore = 0x80000000;

    for ( axis = 0; ( int )axis < 3; axis++ )
    {
        score = CM_ScoreCollideBoxSplitAxis( list, listCount, axis, &value );

        if ( bestScore < score )
        {
            *splitAxis  = axis;
            *splitValue = value;
            bestScore   = score;
        }
    }

    if ( bestScore < 1 )
        return qfalse;

    AssertIn( *splitAxis, 3 );

    return qtrue;
}


/* CM_ScoreCollideBoxSplitAxis  0x00411710 */
static int CM_ScoreCollideBoxSplitAxis( CmCollideBox_t *list, int listCount,
                                        unsigned int axis, float *splitValue )
{
    AssertIn( axis, 3 );

    *splitValue = CM_CollideBoxListAverage( list, listCount, axis );

    return CM_CountCollideBoxSplit( list, listCount, axis, *splitValue );
}


/* CM_CollideBoxListAverage  0x00411780 */
static float CM_CollideBoxListAverage( CmCollideBox_t *list, int listCount, int axis )
{
    CmCollideBox_t *aabb;
    float           total;
    int             localCount;

    Assert( listCount > 0 );

    localCount = 0;
    total      = 0.0f;

    for ( aabb = list; aabb; aabb = aabb->next )
    {
        total += ( aabb->mins[axis] + aabb->maxs[axis] ) / 2.0f;
        localCount++;
    }

    Assert( localCount == listCount );

    return total / ( float )listCount;
}


/* CM_CountCollideBoxSplit  0x00411830 */
static int CM_CountCollideBoxSplit( CmCollideBox_t *list, int listCount, int axis,
                                    float splitValue )
{
    CmCollideBox_t *aabb;
    int             countLeft;
    int             countRight;

    Assert( listCount > 0 );

    countLeft  = 0;
    countRight = 0;

    for ( aabb = list; aabb; aabb = aabb->next )
    {
        if ( aabb->mins[axis] >= splitValue )
        {
            countRight++;
        }
        else if ( aabb->maxs[axis] < splitValue )
        {
            countLeft++;
        }
    }

    if ( countRight < countLeft )
        return countRight;

    return countLeft;
}


/* CM_SplitCollideBoxList  0x004118e0 */
static void CM_SplitCollideBoxList( CmCollideBox_t **list, int *listCount,
                                    CmCollideBox_t **listRight, int *listCountRight,
                                    int axis, float splitValue )
{
    CmCollideBox_t *aabb;
    CmCollideBox_t *next;
    CmCollideBox_t *listLeft;
    int             listCountLeft;

    Assert( listCount != NULL );

    listLeft      = NULL;
    listCountLeft = 0;

    for ( aabb = *list; aabb; aabb = next )
    {
        Assert( !aabb->children );

        next = aabb->next;
        aabb->next = NULL;

        if ( ( aabb->mins[axis] + aabb->maxs[axis] ) / 2.0f >= splitValue )
            CM_AddCollideBoxToList( listRight, listCountRight, aabb );
        else
            CM_AddCollideBoxToList( &listLeft, &listCountLeft, aabb );
    }

    Assert( ( *listCount ) == listCountLeft + *listCountRight );

    *list      = listLeft;
    *listCount = listCountLeft;
}


/* CM_AddCollideBoxToList  0x00411a10 */
static void CM_AddCollideBoxToList( CmCollideBox_t **list, int *listCount,
                                    CmCollideBox_t *aabb )
{
    Assert( aabb );
    Assert( !aabb->next );

    aabb->next = *list;
    *list      = aabb;
    ( *listCount )++;
}


/* CM_AppendCollideBoxList  0x00411a90 */
static void CM_AppendCollideBoxList( CmCollideBox_t *aabbList, CmCollideBox_t *tail )
{
    CmCollideBox_t *aabb;

    Assert( aabbList );

    for ( aabb = aabbList; aabb->next; aabb = aabb->next )
        ;

    Assert( !aabb->next );

    aabb->next = tail;
}


/* CM_AllocCollideBoxNode  0x00411b10 */
static CmCollideBox_t *CM_AllocCollideBoxNode( CmCollideBox_t *children )
{
    CmCollideBox_t *node;
    CmCollideBox_t *child;

    node = new CmCollideBox_t;

    node->partition = NULL;
    node->children  = children;
    node->next      = NULL;

    ClearBounds( node->mins, node->maxs );

    for ( child = children; child; child = child->next )
        AddBoundsToBounds( child->mins, child->maxs, node->mins, node->maxs );

    return node;
}


/* CM_AllocCollideBoxLeaf  0x00411c20 */
static CmCollideBox_t *CM_AllocCollideBoxLeaf( CmPartition_t *partition, CmCollideBox_t *next )
{
    CmCollideBox_t *leaf;

    leaf = new CmCollideBox_t;

    Vec3Copy( partition->mins, leaf->mins );
    Vec3Copy( partition->maxs, leaf->maxs );

    leaf->partition = partition;
    leaf->children  = NULL;
    leaf->next      = next;

    return leaf;
}


/* CM_ComparePartitions  0x00411ba0 */
static int CM_ComparePartitions( const CmPartition_t *a, const CmPartition_t *b )
{
    int diff;

    diff = a->tris->ds->contents - b->tris->ds->contents;

    if ( !diff )
        diff = a->tris->ds->mtlTex->surfaceFlags - b->tris->ds->mtlTex->surfaceFlags;

    return diff;
}


/* CM_ComparePartitionPtrs  0x00411bf0 */
static int CM_ComparePartitionPtrs( const void *a, const void *b )
{
    return CM_ComparePartitions( *( const CmPartition_t ** )a, *( const CmPartition_t ** )b );
}


/* CM_EmitCollision  0x00411c90 */
static void CM_EmitCollision( void )
{
    CM_EmitCollisionVerts();
    CM_EmitCollisionPartitions();
}


/* CM_EmitCollisionVerts  0x00411ca0 */
void CM_EmitCollisionVerts( void )
{
    int i;

    for ( i = collGlob.firstVert; i < collGlob.vertCount; i++ )
    {
        if ( numBSPCollisionVerts == MAX_MAP_COLLISIONVERTS )
            Com_Error( "MAX_MAP_COLLISIONVERTS (%i) exceeded", MAX_MAP_COLLISIONVERTS );

        Vec3Copy( collGlob.verts[i].xyz, bspCollisionVerts[numBSPCollisionVerts] );
        numBSPCollisionVerts++;
    }
}


/* CM_EmitCollisionPartitions  0x00411d30 */
void CM_EmitCollisionPartitions( void )
{
    BspCollisionPart_t *diskPartition;
    CmPartition_t      *partition;
    int                 i;

    for ( i = collGlob.firstPart; i < collGlob.partCount; i++ )
    {
        if ( numBSPCollisionParts == MAX_MAP_COLLISIONPARTITIONS )
            Com_Error( "MAX_MAP_COLLISIONPARTITIONS (%i) exceeded", MAX_MAP_COLLISIONPARTITIONS );

        partition     = &collGlob.parts[i];
        diskPartition = &bspCollisionParts[numBSPCollisionParts];

        diskPartition->checkStamp = 0;
        diskPartition->triCount   = ( byte )partition->triCount;

        Assert( diskPartition->triCount == partition->triCount );

        diskPartition->firstTri    = numBSPCollisionTris;
        diskPartition->firstBorder = numBSPCollisionBorders;

        CM_EmitCollisionTris( partition->tris, partition->triCount );
        diskPartition->borderCount = CM_EmitCollisionBorders( partition->tris,
                                                              partition->triCount );

        numBSPCollisionParts++;
    }
}


/* CM_EmitCollisionTris  0x00411e40 */
static void CM_EmitCollisionTris( CollisionTri_t *tris, int triCount )
{
    CollisionTri_t *tri;
    int             i;

    tri = tris;

    for ( i = 0; i < triCount; i++, tri++ )
    {
        if ( numBSPCollisionTris == MAX_MAP_COLLISIONTRIS )
            Com_Error( "MAX_MAP_COLLISIONTRIS (%i) exceeded", MAX_MAP_COLLISIONTRIS );

        bspCollisionTris[numBSPCollisionTris].vertIndices[0] = tri->vertIndices[0];
        bspCollisionTris[numBSPCollisionTris].vertIndices[1] = tri->vertIndices[1];
        bspCollisionTris[numBSPCollisionTris].vertIndices[2] = tri->vertIndices[2];

        CM_SetCollisionEdgeWalkBit( &collGlob.edges[tri->edgeIndices[0]], numBSPCollisionTris, 0 );
        CM_SetCollisionEdgeWalkBit( &collGlob.edges[tri->edgeIndices[1]], numBSPCollisionTris, 1 );
        CM_SetCollisionEdgeWalkBit( &collGlob.edges[tri->edgeIndices[2]], numBSPCollisionTris, 2 );

        numBSPCollisionTris++;
    }
}


/* CM_SetCollisionEdgeWalkBit  0x00411f50 */
static void CM_SetCollisionEdgeWalkBit( const CollisionEdge_t *edge, int triIndex, int edge3 )
{
    int bitIndex;

    if ( !edge->walkable )
        return;

    bitIndex = triIndex * 3 + edge3;
    bspCollisionEdgeWalk[bitIndex >> 3] |= 1 << ( bitIndex & 7 );
}


/* CM_EmitCollisionBorders  0x00411fa0 */
static byte CM_EmitCollisionBorders( CollisionTri_t *tris, int triCount )
{
    BspCollisionBorder_t *border;
    CollisionEdge_t      *edge;
    CollisionTri_t       *tri;
    const float          *v0;
    const float          *v1;
    const float          *base;
    vec3_t                delta;
    float                 length;
    byte                  borderCount;
    int                   i;
    int                   j;

    borderCount = 0;
    tri         = tris;

    for ( i = 0; i < triCount; i++, tri++ )
    {
        for ( j = 0; j < 3; j++ )
        {
            edge = &collGlob.edges[tri->edgeIndices[j]];

            if ( edge->convex || !edge->needsBorder )
                continue;

            if ( edge->contents )
                edge->needsBorder = 0;

            if ( numBSPCollisionBorders == MAX_MAP_COLLISIONBORDERS )
                Com_Error( "MAX_MAP_COLLISIONBORDERS (%i) exceeded", MAX_MAP_COLLISIONBORDERS );

            border = &bspCollisionBorders[numBSPCollisionBorders];

            v0 = collGlob.verts[edge->vertIndices[0]].xyz;
            v1 = collGlob.verts[edge->vertIndices[1]].xyz;

            Vec3Sub( v1, v0, delta );

            border->distEq[0] = -delta[1];
            border->distEq[1] = delta[0];

            length = Vec2Normalize( border->distEq );

            if ( length == 0.0 )
                continue;

            border->distEq[2] = Vec2Dot( border->distEq, v0 );

            if ( Vec2Dot( border->distEq, collGlob.verts[edge->oppositeVert].xyz )
               - border->distEq[2] < 0.0 )
            {
                base = v0;
                border->zSlope = delta[2] / length;
            }
            else
            {
                Vec3Negate( border->distEq, border->distEq );
                base = v1;
                border->zSlope = -delta[2] / length;
            }

            border->zBase  = base[2];
            border->start  = border->distEq[1] * base[0] - border->distEq[0] * base[1];
            border->length = length;

            numBSPCollisionBorders++;
            borderCount++;
        }
    }

    return borderCount;
}


/* CM_ParseCollNodeLimit  0x004121b0 */
void CM_ParseCollNodeLimit( const Entity_t *ent )
{
    const char *value;

    value = ValueForKey( ent, "coll_node_limit" );

    Assert( value );

    if ( value[0] && value[0] != '0' )
        g_collNodeLimit = atoi( value );
}
