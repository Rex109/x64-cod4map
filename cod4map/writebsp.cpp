/* Original: .\writebsp.cpp */

#include "writebsp.h"

#include "cod4map.h"
#include "errors.h"
#include "materials.h"
#include "lightmaps.h"
#include "surface.h"
#include "collision.h"
#include "bsp.h"
#include "../common/brush_edges.h"


extern const char *StringFromOffset( const void *base );              /* 0x004053a0 */

extern void UnparseEntities( void );                                  /* 0x0040cfb0 */

extern void PrintBSPMaterialList( void );                             /* 0x00423f70 */

extern void RemapPortalPlanes( const int *planeMap );                 /* 0x00430a80 */

extern void Tris_InitContexts( void );                                /* 0x00449ca0 */
extern void Tris_FinishBSPTris( void );                            /* 0x00449e50 */


extern void Mass_ComputeProperties( const vec3_t mins, const vec3_t maxs,
                                    bool ( *inside )( const vec3_t point,
                                                      const vec3_t halfCell,
                                                      Entity_t *ent ),
                                    Entity_t *ent, vec3_t centerOfMass,
                                    vec3_t momentOfInertia,
                                    vec3_t productOfInertia );        /* 0x0048f1c0 */

extern char g_outputBasePath[MAX_OS_PATH];                            /* 0x00a357e8 */

static bool PointInsideEntityBrushes( const vec3_t point, const vec3_t halfCell,
                                      Entity_t *ent );
static void SetPhysicsMassPropertiesForEntity( Entity_t *ent );
static void BeginModelForGeometry( BspModel_t *mod, Brush_t *brushes,
                                   parseMesh_t *patches );
static void EndModelForGeometry( BspModel_t *mod, Brush_t *brushes, Node_t *headnode );
static void CheckBrushCount( void );


/* EmitMaterial  0x00463c80 */
int EmitMaterial( const char *materialName, int surfaceFlags, int contentFlags )
{
    int i;

    if ( !materialName )
        materialName = "$default";

    for ( i = 0; i < numBSPMaterials; i++ )
    {
        if ( surfaceFlags == bspMaterials[i].surfaceFlags
          && contentFlags == bspMaterials[i].contentFlags
          && !Q_stricmp( materialName, bspMaterials[i].name ) )
        {
            return i;
        }
    }

    if ( numBSPMaterials == MAX_MAP_MATERIALS - 1 )
    {
        PrintBSPMaterialList();
        printf( "MAX_MAP_MATERIALS (%i) reached -- too many unique materials used in the map.\n",
                MAX_MAP_MATERIALS );
        printf( "Using 'weaponclip' in Radiant also turns one material into two." );
        printf( "All remaining materials will get turned into the default material." );
        materialName = "$default";
        contentFlags = 1;
    }
    else if ( numBSPMaterials == MAX_MAP_MATERIALS )
    {
        return MAX_MAP_MATERIALS - 1;
    }

    numBSPMaterials++;
    strcpy( bspMaterials[i].name, materialName );
    _strlwr( bspMaterials[i].name );
    bspMaterials[i].surfaceFlags = surfaceFlags;
    bspMaterials[i].contentFlags = contentFlags;

    return i;
}


/* EmitMaterialForSurface  0x00463dc0 */
int EmitMaterialForSurface( Material_t *mtlRaw, int contentFlags )
{
    if ( !mtlRaw )
    {
        mtlRaw = Material_Register( "default", 4 )->material;
        Assert( mtlRaw );
    }

    return EmitMaterial( StringFromOffset( mtlRaw ), mtlRaw->surfaceFlags, contentFlags );
}


/* EmitPlanes  0x00463e30 */
void EmitPlanes( void )
{
    int     *planeMap;
    plane_t *mp;
    int      i;

    planeMap = ( int * )malloc( nummapplanes * sizeof( int ) );

    mp = mapplanes;

    for ( i = 0; i < nummapplanes; i++ )
    {
        if ( !mp->flags )
        {
            planeMap[i] = -1;
        }
        else
        {
            if ( numBSPPlanes == MAX_MAP_PLANES )
                Com_Error( "MAX_MAP_PLANES (%i)", MAX_MAP_PLANES );

            planeMap[i] = numBSPPlanes;
            Vec3Copy( mp->normal, bspPlanes[numBSPPlanes].normal );
            bspPlanes[numBSPPlanes].dist = mp->dist;
            numBSPPlanes++;
        }

        mp++;
    }

    RemapNodePlanes( planeMap );
    RemapBrushSidePlanes( planeMap );
    RemapPortalPlanes( planeMap );

    free( planeMap );
}


/* RemapNodePlanes  0x00464370 */
void RemapNodePlanes( const int *planeMap )
{
    int i;

    for ( i = 0; i < numBSPNodes; i++ )
    {
        if ( bspNodes[i].planeNum < 0 )
            continue;

        Assert( bspNodes[i].planeNum >= 0 && bspNodes[i].planeNum < nummapplanes );
        Assert( planeMap[bspNodes[i].planeNum] >= 0
             && planeMap[bspNodes[i].planeNum] < numBSPPlanes );

        bspNodes[i].planeNum = planeMap[bspNodes[i].planeNum];
    }
}


/* RemapBrushSidePlanes  0x00464b50 */
void RemapBrushSidePlanes( const int *planeMap )
{
    BspBrushSide_t *cp;
    int             i;
    int             j;

    Assert( planeMap );

    cp = bspBrushSides;

    for ( i = 0; i < numBSPBrushes; i++ )
    {
        cp += BRUSH_AXIAL_SIDES;

        for ( j = BRUSH_AXIAL_SIDES; j < bspBrushes[i].numSides; j++ )
        {
            Assert( cp->u.planeNum >= 0 && cp->u.planeNum < nummapplanes );
            Assert( planeMap[cp->u.planeNum] >= 0
                 && planeMap[cp->u.planeNum] < numBSPPlanes );

            cp->u.planeNum = planeMap[cp->u.planeNum];
            cp++;
        }
    }
}


/* EmitBrushes  0x004652c0 */
void EmitBrushes( Brush_t *brushes )
{
    Brush_t            *b;
    BspBrush_t         *db;
    BspBrushSide_t     *cp;
    side_t             *side;
    plane_t            *plane;
    adjacencyWinding_t *winding;
    int                 firstSide;
    int                 diskSideIndex;
    int                 axialSign;
    unsigned int        memSideIndex;
    int                 edgeIndex;

    for ( b = brushes; b; b = b->next )
    {
        if ( b->noCollision || b->original->noCollision )
            continue;

        Assert( b->collisionSideCount >= 6 );

        if ( numBSPBrushes == MAX_MAP_BRUSHES )
            Com_Error( "MAX_MAP_BRUSHES" );

        b->diskBrushNum = numBSPBrushes;
        db = &bspBrushes[numBSPBrushes];
        numBSPBrushes++;

        db->materialNum = EmitMaterialForSurface( b->material, b->contents );

        Assertx( bspMaterials[db->materialNum].contentFlags & ~( ( 1 << 2 ) | ( 1 << 29 ) ),
                 "(dshaders[db->materialNum].contentFlags) = %i",
                 bspMaterials[db->materialNum].contentFlags );

        firstSide    = numBSPBrushSides;
        db->numSides = 0;

        for ( memSideIndex = 0; memSideIndex < b->collisionSideCount; memSideIndex++ )
        {
            if ( numBSPBrushSides == MAX_MAP_BRUSHSIDES )
                Com_Error( "MAX_MAP_BRUSHSIDES " );

            cp            = &bspBrushSides[numBSPBrushSides];
            diskSideIndex = numBSPBrushSides - firstSide;
            side          = &b->collisionSides[memSideIndex];
            plane         = &mapplanes[side->planenum];

            db->numSides++;
            numBSPBrushSides++;

            if ( diskSideIndex < BRUSH_AXIAL_SIDES )
            {
                axialSign = plane->normal[diskSideIndex >> 1] == ( diskSideIndex & 1 ) ? 1 : -1;
                Assert( axialSign );

                if ( diskSideIndex & 1 )
                    cp->u.distance = plane->dist;
                else
                    cp->u.distance = -plane->dist;
            }
            else
            {
                plane->flags   = 1;
                cp->u.planeNum = side->planenum;
            }

            cp->materialNum = EmitMaterialForSurface( side->material, b->contents );

            winding = ( adjacencyWinding_t * )side->edges;

            if ( !winding )
            {
                bspBrushSideEdgeCounts[numBSPBrushSides - 1] = 0;
            }
            else
            {
                bspBrushSideEdgeCounts[numBSPBrushSides - 1] = ( byte )winding->numsides;

                Assert( bspBrushSideEdgeCounts[numBSPBrushSides - 1] == winding->numsides );

                for ( edgeIndex = 0;
                      edgeIndex < bspBrushSideEdgeCounts[numBSPBrushSides - 1];
                      edgeIndex++ )
                {
                    if ( numBSPBrushEdges == MAX_MAP_BRUSHEDGES )
                        Com_Error( "MAX_MAP_BRUSHEDGES" );

                    bspBrushEdges[numBSPBrushEdges] = ( byte )winding->sides[edgeIndex];

                    if ( bspBrushEdges[numBSPBrushEdges] != winding->sides[edgeIndex] )
                        Com_Error( "MAX_EDGES_PER_SIDE" );

                    AssertCmp( bspBrushEdges[numBSPBrushEdges], <, b->collisionSideCount );

                    numBSPBrushEdges++;
                }
            }
        }

        Assert( db->numSides >= 6 );
    }
}


/* EmitDrawNode_r  0x00463f40 */
int EmitDrawNode_r( Node_t *node )
{
    BspNode_t *n;
    int        nodeIndex;
    int        i;

    if ( node->planenum == PLANENUM_LEAF )
    {
        EmitLeaf( node );
        return -numBSPLeafs;
    }

    if ( numBSPNodes == MAX_MAP_NODES )
        Com_Error( "MAX_MAP_NODES" );

    n = &bspNodes[numBSPNodes];
    numBSPNodes++;

    n->mins[0] = ( int )floor( node->mins[0] );
    n->mins[1] = ( int )floor( node->mins[1] );
    n->mins[2] = ( int )floor( node->mins[2] );
    n->maxs[0] = ( int )ceil( node->maxs[0] );
    n->maxs[1] = ( int )ceil( node->maxs[1] );
    n->maxs[2] = ( int )ceil( node->maxs[2] );

    if ( node->planenum & 1 )
        Com_Error( "WriteDrawNodes_r: odd planenum" );

    n->planeNum = node->planenum;
    mapplanes[n->planeNum].flags = 1;

    for ( i = 0; i < 2; i++ )
        n->children[i] = EmitDrawNode_r( node->children[i] );

    return ( int )( n - bspNodes );
}


/* EmitLeaf  0x004640d0 */
void EmitLeaf( Node_t *node )
{
    BspLeaf_t *leaf;
    Brush_t   *b;

    if ( numBSPLeafs > MAX_MAP_LEAFS - 1 )
        Com_Error( "MAX_MAP_LEAFS" );

    leaf = &bspLeafs[numBSPLeafs];
    numBSPLeafs++;

    leaf->cluster = node->cluster;
    leaf->cellNum = node->cellnum;

    leaf->firstLeafBrush = numBSPLeafBrushes;

    for ( b = node->leafBrushes; b; b = b->next )
    {
        if ( b->noCollision || b->original->noCollision )
            continue;

        if ( numBSPLeafBrushes > MAX_MAP_LEAFBRUSHES - 1 )
            Com_Error( "MAX_MAP_LEAFBRUSHES" );

        bspLeafBrushes[numBSPLeafBrushes] = b->original->diskBrushNum;
        numBSPLeafBrushes++;
    }

    leaf->leafBrushCount = numBSPLeafBrushes - leaf->firstLeafBrush;

    if ( node->opaque == 1 )
    {
        leaf->firstCollAabbIndex = 0;
        leaf->collAabbCount      = 0;
    }
    else
    {
        leaf->firstCollAabbIndex = numBSPCollisionAabbs;
        leaf->collAabbCount      = EmitCollisionAABBs_r( node->collisionBoxes );
    }
}


/* EmitCollisionAABBs_r  0x00464200 */
int EmitCollisionAABBs_r( CmCollideBox_t *boxList )
{
    BspCollisionAabb_t *aabb;
    BspCollisionAabb_t *firstAabb;
    CmCollideBox_t     *box;
    DrawSurf_t         *ds;
    int                 count;

    firstAabb = &bspCollisionAabbs[numBSPCollisionAabbs];
    count     = 0;

    for ( box = boxList; box; box = box->next, count++ )
    {
        if ( numBSPCollisionAabbs == MAX_MAP_COLLISIONAABBS )
            Com_Error( "MAX_MAP_COLLISIONAABBS (%i) exceeded", MAX_MAP_COLLISIONAABBS );

        aabb = &bspCollisionAabbs[numBSPCollisionAabbs];

        Vec3Mid( box->mins, box->maxs, aabb->midPoint );
        Vec3Sub( box->maxs, aabb->midPoint, aabb->halfSize );

        numBSPCollisionAabbs++;
    }

    aabb = firstAabb;

    for ( box = boxList; box; box = box->next, aabb++ )
    {
        if ( box->children )
        {
            aabb->u             = numBSPCollisionAabbs;
            aabb->childCount    = ( unsigned short )EmitCollisionAABBs_r( box->children );
            aabb->materialIndex = bspCollisionAabbs[aabb->u].materialIndex;
        }
        else
        {
            aabb->u          = box->partition->partIndex;
            aabb->childCount = 0;

            ds = box->partition->tris->ds;
            aabb->materialIndex = ( unsigned short )EmitMaterialForSurface( ds->mtlTex, ds->contents );
        }
    }

    return count;
}


/* SetBrushModelNumbers  0x00464460 */
void SetBrushModelNumbers( void )
{
    Entity_t *ent;
    char      modelName[12];
    int       modelIndex;
    int       i;

    modelIndex = 1;

    for ( i = 1; i < num_entities; i++ )
    {
        if ( entities[i].brushes || entities[i].patches )
        {
            if ( strncmp( ValueForKey( &entities[i], "classname" ), "dyn_", 4 ) )
            {
                if ( modelIndex == 0x3ff )
                    Com_Error( "Error: Too many non dyn entity brush models.  Max is [%d]\n", 0x3ff );

                sprintf( modelName, "*%i", modelIndex );
                SetKeyValue( &entities[i], "model", modelName );
                modelIndex++;
            }
        }
    }

    for ( i = 1; i < num_entities; i++ )
    {
        ent = &entities[i];

        if ( !strncmp( ValueForKey( ent, "classname" ), "dyn_", 4 ) )
        {
            if ( ent->brushes || ent->patches )
            {
                if ( modelIndex == 0xfff )
                    Com_Error( "Error: Too many dyn entity brush models.  Max is [%d]\n", 0xfff );

                sprintf( modelName, "*%i", modelIndex );
                SetKeyValue( ent, "model", modelName );
                modelIndex++;
            }

            if ( ent->physicsBrushes )
            {
                if ( modelIndex == 0xfff )
                    Com_Error( "Error: Too many dyn entity brush models.  Max is [%d]\n", 0xfff );

                sprintf( modelName, "*%i", modelIndex );
                SetKeyValue( ent, "physicsmodel", modelName );
                modelIndex++;
            }
        }
    }
}


/* SetPhysicsMassProperties  0x00464670 */
void SetPhysicsMassProperties( void )
{
    int i;

    for ( i = 1; i < num_entities; i++ )
    {
        if ( entities[i].brushes || entities[i].physicsBrushes )
        {
            if ( !strncmp( ValueForKey( &entities[i], "classname" ), "dyn_", 4 ) )
                SetPhysicsMassPropertiesForEntity( &entities[i] );
        }
    }
}


/* SetPhysicsMassPropertiesForEntity  0x00464700 */
static void SetPhysicsMassPropertiesForEntity( Entity_t *ent )
{
    Brush_t *brushes;
    Brush_t *b;
    vec3_t   mins;
    vec3_t   maxs;
    vec3_t   centerOfMass;
    vec3_t   momentOfInertia;
    vec3_t   productOfInertia;

    ClearBounds( mins, maxs );

    if ( ent->physicsBrushes )
        brushes = ent->physicsBrushes;
    else
        brushes = ent->brushes;

    for ( b = brushes; b; b = b->next )
    {
        if ( !b->sideCount )
            continue;

        AddPointToBounds( b->mins, mins, maxs );
        AddPointToBounds( b->maxs, mins, maxs );
    }

    Mass_ComputeProperties( mins, maxs, PointInsideEntityBrushes, ent,
                            centerOfMass, momentOfInertia, productOfInertia );

    SetKeyValue( ent, "centerofmass",
                 va( "%g %g %g", centerOfMass[0], centerOfMass[1], centerOfMass[2] ) );
    SetKeyValue( ent, "momofinertia",
                 va( "%g %g %g", momentOfInertia[0], momentOfInertia[1], momentOfInertia[2] ) );
    SetKeyValue( ent, "prodofinertia",
                 va( "%g %g %g", productOfInertia[0], productOfInertia[1], productOfInertia[2] ) );
}


/* PointInsideEntityBrushes  0x00464860 */
static bool PointInsideEntityBrushes( const vec3_t point, const vec3_t halfCell,
                                      Entity_t *ent )
{
    Brush_t     *brushes;
    Brush_t     *b;
    plane_t     *plane;
    float        slop;
    float        dist;
    unsigned int i;

    if ( ent->physicsBrushes )
        brushes = ent->physicsBrushes;
    else
        brushes = ent->brushes;

    for ( b = brushes; b; b = b->next )
    {
        if ( !b->sideCount )
            continue;

        for ( i = 0; i < b->sideCount; i++ )
        {
            plane = &mapplanes[b->sides[i].planenum];

            slop = fabs( plane->normal[0] * halfCell[0] )
                 + fabs( plane->normal[1] * halfCell[1] )
                 + fabs( plane->normal[2] * halfCell[2] );

            dist = Vec3Dot( point, plane->normal ) - plane->dist - slop;

            if ( dist > 0.1 )
                break;
        }

        if ( i == b->sideCount )
            return true;
    }

    return false;
}


/* BeginBSPFile  0x004649a0 */
void BeginBSPFile( void )
{
    numBSPModels       = 0;
    numBSPNodes        = 0;
    numBSPBrushSides   = 0;
    numBSPBrushEdges   = 0;
    numBSPLeafSurfaces = 0;
    numBSPLeafBrushes  = 0;
    numBSPLeafs        = 1;

    Tris_InitContexts();
}


/* EndBSPFile  0x004649f0 */
void EndBSPFile( void )
{
    char path[MAX_OS_PATH];

    EmitPlanes();
    UnparseEntities();
    Tris_FinishBSPTris();
    CheckBrushCount();

    if ( MapErrorsOccurred() )
        Com_Error( "\nNot writing map due to errors\n" );

    sprintf( path, "%s%s", g_outputBasePath, GetBSPFileExtension() );
    Com_Printf( "Writing %s\n", path );

    Assert( targetPlatform != PLATFORM_VOID );

    WriteBSPFile( path );
}


/* CheckBrushCount  0x00464ab0 */
static void CheckBrushCount( void )
{
    int brushCount;
    int i;

    brushCount = 0;

    for ( i = 0; i < num_entities; i++ )
    {
        brushCount += CountBrushList( entities[i].brushes )
                    + CountBrushList( entities[i].physicsBrushes );
    }

    Assert( brushCount == numBSPBrushes );
}


/* BeginModel  0x00464c80 */
void BeginModel( void )
{
    unsigned int modelIndex;

    Assert( numBSPModels < MAX_MAP_MODELS );

    modelIndex = GetBrushModelIndex( entity_num );
    BeginModelForGeometry( &bspModels[modelIndex],
                           entities[entity_num].brushes,
                           entities[entity_num].patches );
}


/* BeginPhysicsModel  0x00464f70 */
void BeginPhysicsModel( void )
{
    unsigned int modelIndex;

    Assert( numBSPModels < MAX_MAP_MODELS );

    modelIndex = GetPhysicsModelIndex( entity_num );
    BeginModelForGeometry( &bspModels[modelIndex],
                           entities[entity_num].physicsBrushes,
                           NULL );
}


/* BeginModelForGeometry  0x00464e40 */
static void BeginModelForGeometry( BspModel_t *mod, Brush_t *brushes,
                                   parseMesh_t *patches )
{
    Brush_t      *b;
    parseMesh_t  *p;
    vec3_t        mins;
    vec3_t        maxs;
    int           i;
    unsigned int  type;

    ClearBounds( mins, maxs );

    for ( b = brushes; b; b = b->next )
    {
        if ( !b->sideCount )
            continue;

        AddBoundsToBounds( b->mins, b->maxs, mins, maxs );
    }

    for ( p = patches; p; p = p->next )
    {
        for ( i = 0; i < p->width * p->height; i++ )
            AddPointToBounds( ( ( DrawVert_t * )p->verts )[i].xyz, mins, maxs );
    }

    Vec3Copy( mins, mod->mins );
    Vec3Copy( maxs, mod->maxs );

    for ( type = 0; type < TRIS_TYPE_COUNT; type++ )
        mod->firstTriSoup[type] = checked_cast< unsigned short >( numBSPTriSoups[type] );

    mod->firstSurface = numBSPCollisionAabbs;
    mod->firstBrush   = numBSPBrushes;
}


/* EndModel  0x00465120 */
void EndModel( Node_t *headnode )
{
    unsigned int modelIndex;

    Com_DPrintf( "--- EndBrushModel ---\n" );

    modelIndex = GetBrushModelIndex( entity_num );
    EndModelForGeometry( &bspModels[modelIndex], entities[entity_num].brushes, headnode );
}


/* EndPhysicsModel  0x004656e0 */
void EndPhysicsModel( Node_t *headnode )
{
    unsigned int modelIndex;

    Com_DPrintf( "--- EndPhysicsModel ---\n" );

    modelIndex = GetPhysicsModelIndex( entity_num );
    EndModelForGeometry( &bspModels[modelIndex], entities[entity_num].brushes, NULL );
}


/* EndModelForGeometry  0x00465170 */
static void EndModelForGeometry( BspModel_t *mod, Brush_t *brushes, Node_t *headnode )
{
    int          firstLeaf;
    bool         anyTriSoups;
    bool         allTriSoups;
    unsigned int type;

    firstLeaf = numBSPLeafs;

    EmitBrushes( brushes );

    if ( headnode )
        EmitDrawNode_r( headnode );

    anyTriSoups = false;
    allTriSoups = true;

    for ( type = 0; type < TRIS_TYPE_COUNT; type++ )
    {
        mod->triSoupCount[type] =
            checked_cast< unsigned short >( numBSPTriSoups[type] - mod->firstTriSoup[type] );

        if ( mod->triSoupCount[type] == 0 )
            allTriSoups = false;
        else
            anyTriSoups = true;
    }

    Assert( anyTriSoups == allTriSoups );

    mod->numSurfaces = bspLeafs[firstLeaf].collAabbCount;
    mod->numBrushes  = numBSPBrushes - mod->firstBrush;

    if ( !mod->numSurfaces && !mod->numBrushes && !anyTriSoups )
    {
        printf( "Ignoring empty brush model entity\nMap %s entity %i\n",
                GetMapFileName( entities[entity_num].mapIndex ),
                entities[entity_num].entityNum );
    }
    else
    {
        numBSPModels++;
    }
}


/* GetBrushModelIndex  0x00464d10 */
unsigned int GetBrushModelIndex( unsigned int entIndex )
{
    const char  *modelName;
    unsigned int brushModelIndex;

    AssertIn( entIndex, MAX_MAP_ENTITIES );

    if ( !entIndex )
        return 0;

    Assert( entities[entIndex].brushes || entities[entIndex].patches );

    modelName = ValueForKey( &entities[entIndex], "model" );
    Assert( modelName );
    Assert( modelName[0] == '*' );

    brushModelIndex = atoi( modelName + 1 );
    AssertIn( brushModelIndex, MAX_MAP_MODELS );

    return brushModelIndex;
}


/* GetPhysicsModelIndex  0x00464ff0 */
unsigned int GetPhysicsModelIndex( unsigned int entIndex )
{
    const char  *modelName;
    unsigned int brushModelIndex;

    AssertIn( entIndex, MAX_MAP_ENTITIES );

    if ( !entIndex )
        return 0;

    Assert( entities[entIndex].physicsBrushes );

    modelName = ValueForKey( &entities[entIndex], "physicsmodel" );
    Assert( modelName );
    Assert( modelName[0] == '*' );

    brushModelIndex = atoi( modelName + 1 );
    AssertIn( brushModelIndex, MAX_MAP_MODELS );

    return brushModelIndex;
}
