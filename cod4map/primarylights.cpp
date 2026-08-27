/* Original: .\primarylights.cpp */

#include <new>

#include "primarylights.h"

#include "cod4map.h"
#include "map.h"
#include "brush.h"
#include "materials.h"
#include "mesh.h"
#include "tris.h"
#include "aabbtree.h"
#include "errors.h"
#include "primarylights_region.h"

#include <stdio.h>
#include <stdlib.h>

extern Entity_t *FindEntityWithPair( Entity_t *ents, int numEnts,
                                     const char *key, const char *value );  /* 0x0040d1e0 */

SurfaceTreeNode_t *s_surfaceTree;                                   /* 0x12f0f944 */
LightRegion_t      s_lightRegion[MAX_MAP_PRIMARY_LIGHTS];           /* 0x12f0f948 */
int                s_savedPrimaryLightCount;                        /* 0x12f0f940 */
BspPrimaryLight_t  s_savedPrimaryLights[MAX_MAP_PRIMARY_LIGHTS];    /* 0x12f11d28 */

static void        SetupSunPrimaryLight( void );
static qboolean    PrimaryLightLess( const BspPrimaryLight_t *a, const BspPrimaryLight_t *b );
static float       LinearToGamma( float value );
static float       GammaToLinear( float value );
static const char *PrimaryLightName( int lightIndex );
static qboolean    PrimaryLightReachesPoint( const BspPrimaryLight_t *light, const vec3_t point );
static qboolean    PrimaryLightAffectsSurface( int lightIndex, const TriSurf_t *surf );

static DrawSurf_t *SegmentHitsTreeNode( const SurfaceTreeNode_t *node, const Segment_t *seg );
static qboolean    SegmentHitsDrawSurf( const Segment_t *seg, const DrawSurf_t *ds );
static qboolean    SegmentHitsIndexedMesh( const Segment_t *seg, const DrawSurf_t *ds );
static qboolean    SegmentHitsPatch( const Segment_t *seg, const DrawSurf_t *ds );
static qboolean    SegmentHitsBrushFace( const Segment_t *seg, const DrawSurf_t *ds );
static qboolean    SegmentIntersectsTriangle( const Segment_t *seg, const vec3_t v0,
                                              const vec3_t v1, const vec3_t v2 );
static qboolean    SegmentOverlapsBox( const Segment_t *seg, const vec3_t center,
                                       const vec3_t halfSize );

static DrawSurf_t *TraceSurfaceOccluder( const vec3_t p0, const vec3_t p1, DrawSurf_t *lastHit );
static DrawSurf_t *TracePrimaryLightToPoint( const BspPrimaryLight_t *light,
                                             const vec3_t point, DrawSurf_t *lastHit );
static void        AssignPrimaryLightsAtTexel( float area, const vec2_t center,
                                               const vec2_t *coords, int vertCount,
                                               void *userData, int cellIndex );
static qboolean    SetSurfacePrimaryLight( TriSurf_t *surf, const vec3_t point, int lightIndex );

static qboolean    LightRegionSeparatesSurface( const winding_t *w, const vec3_t mid,
                                                const vec3_t halfSize, const vec3_t lightPos,
                                                const LightRegion_t *region );
static qboolean    HullSeparatesSurface( const winding_t *w, const vec3_t mid,
                                         const vec3_t halfSize, const vec3_t lightPos,
                                         const BspLightRegionHull_t *hull );
static qboolean    WindingSeparatedAlongAxis( const winding_t *w, float x, float y, float z,
                                              float dist, float slabMid, float slabHalfSize );

static void       *AddSurfacesToLightRegion( const PrimaryLightInfo_t *light,
                                             DrawSurf_t **surfs, unsigned int surfCount );
static void        AddPatchToLightRegion( const PrimaryLightInfo_t *light, const DrawSurf_t *ds,
                                          qboolean twoSided, void **builder );
static void        AddIndexedMeshToLightRegion( const PrimaryLightInfo_t *light,
                                                const DrawSurf_t *ds, qboolean twoSided,
                                                void **builder );
static void        AddBrushFaceToLightRegion( const PrimaryLightInfo_t *light,
                                              const DrawSurf_t *ds, qboolean twoSided,
                                              void **builder );
static unsigned int GatherLitSurfaces( const PrimaryLightInfo_t *light,
                                       int firstSurfCount, int shadowSurfCount,
                                       const int *shadowSurfIndex,
                                       const vec3_t *surfMids, const vec3_t *surfHalfSizes,
                                       DrawSurf_t **out );
static int         GatherLitSurface( const PrimaryLightInfo_t *light, int surfIndex,
                                     const vec3_t surfMid, const vec3_t surfHalfSize,
                                     int count, DrawSurf_t **out );
static void        EmitLightRegions( void );
static void        DrawSurfBoundsMidHalf( int surfIndex, vec3_t midOut, vec3_t halfSizeOut );

#define PRIMARY_LIGHT_PATCH_MIN_LENGTH  999.0f


/* SetupPrimaryLights  0x00431df0 */
void SetupPrimaryLights( void )
{
    BspPrimaryLight_t light;
    int               lightEntityNum[MAX_MAP_PRIMARY_LIGHTS + 1];
    int               entNum;
    Entity_t         *ent;
    const char       *classname;
    unsigned int      spawnflags;
    const char       *target;
    Entity_t         *targetEnt;
    const char       *defName;
    float             distSq;
    float             fovOuter;
    float             fovInner;
    float             intensity;
    float             maxturn;
    vec3_t            color;
    int               lightIndex;
    int               insertIndex;
    int               lightType;
    char              indexText[16];

    numBSPPrimaryLights = 0;

    memset( &bspPrimaryLights[0], 0, sizeof( bspPrimaryLights[0] ) );
    SanityCheck( bspPrimaryLights[0].type == GFX_LIGHT_TYPE_NONE );
    numBSPPrimaryLights++;

    SetupSunPrimaryLight();

    for ( entNum = 1; entNum < num_entities; entNum++ )
    {
        ent = &entities[entNum];

        classname = ValueForKey( ent, "classname" );
        if ( strcmp( classname, "light" ) )
            continue;

        RemoveKey( ent, "pl#" );

        spawnflags = IntForKey( ent, "spawnflags" );
        if ( !( spawnflags & ( PRIMARY_LIGHT_POINT | PRIMARY_LIGHT_SPOT ) ) )
            continue;

        target = ValueForKey( ent, "target" );
        if ( !target[0] )
        {
            PrimaryLight_Error( ent, NULL, "ignoring primary light without a 'target' key" );
            continue;
        }

        targetEnt = FindEntityWithPair( entities, num_entities, "targetname", target );
        if ( !targetEnt )
        {
            PrimaryLight_Error( ent, NULL,
                va( "ignoring primary light because target '%s' is missing", target ) );
            continue;
        }

        if ( numBSPPrimaryLights >= MAX_MAP_PRIMARY_LIGHTS )
        {
            PrimaryLight_Error( ent, NULL,
                va( "ignoring primary light because max primary lights (%i) exceeded",
                    MAX_MAP_PRIMARY_LIGHTS ) );
            continue;
        }

        defName = ValueForKey( ent, "def" );
        if ( !defName[0] )
            defName = "light_point_linear";

        if ( strlen( defName ) >= sizeof( light.defName ) )
        {
            PrimaryLight_Error( ent, NULL,
                va( "ignoring primary light because def name %s is longer than %i characters",
                    defName, sizeof( light.defName ) - 1 ) );
            continue;
        }

        strcpy( light.defName, defName );
        Vec3Copy( ent->origin, light.origin );

        Vec3Sub( ent->origin, targetEnt->origin, light.dir );
        distSq = Vec3LengthSq( light.dir );
        Vec3Normalize( light.dir );

        light.radius = FloatForKey( ent, "radius" );
        if ( light.radius <= 0.0 )
        {
            PrimaryLight_Error( ent, light.dir,
                va( "ignoring primary light with invalid radius %g", light.radius ) );
            continue;
        }

        fovOuter = FloatForKey( ent, "fov_outer" );
        if ( fovOuter == 0.0 )
        {
            light.cosHalfFovOuter = I_sqrt( distSq / ( distSq + 4096.0 ) );
            fovOuter              = ( float )acos( light.cosHalfFovOuter ) * RAD2DEG;
        }
        else
        {
            light.cosHalfFovOuter = cos( fovOuter * DEG2RAD * 0.5 );
            if ( light.cosHalfFovOuter <= 0.0 )
            {
                PrimaryLight_Error( ent, light.dir,
                    "ignoring primary light with fov_outer >= 180 degrees" );
                continue;
            }
        }

        GetVectorForKey( ent, "_color", color );
        Vec3MaxNormalize( color, color );
        intensity = FloatForKey( ent, "intensity" );
        Vec3Scale( color, intensity, light.color );

        if ( spawnflags & PRIMARY_LIGHT_SPOT )
        {
            light.type            = GFX_LIGHT_TYPE_SPOT;
            fovInner              = FloatForKey( ent, "fov_inner" );
            light.cosHalfFovInner = cos( fovInner * DEG2RAD * 0.5 );
            if ( light.cosHalfFovOuter > light.cosHalfFovInner )
            {
                PrimaryLight_Error( ent, light.dir,
                    "ignoring primary spotlight because fov_inner > fov_outer" );
                continue;
            }
            *( int * )&light.cosHalfFovExpanded = IntForKey( ent, "exponent" );
        }
        else
        {
            light.type            = GFX_LIGHT_TYPE_OMNI;
            fovInner              = FloatForKey( ent, "fov_inner" );
            light.cosHalfFovInner = cos( fovInner * DEG2RAD * 0.5 );
            if ( light.cosHalfFovOuter > light.cosHalfFovInner )
                light.cosHalfFovInner = light.cosHalfFovOuter * 0.75 + 0.25;
            *( int * )&light.cosHalfFovExpanded = 0;
        }

        if ( spawnflags & PRIMARY_LIGHT_MOVING )
        {
            light.translationLimit = FloatForKey( ent, "maxmove" );
            maxturn                = FloatForKey( ent, "maxturn" );
            maxturn                = I_fclamp( maxturn, 0.0f, 180.0f );
            light.rotationLimit    = cos( maxturn * DEG2RAD );
        }
        else
        {
            light.translationLimit = 0.0f;
            light.rotationLimit    = 1.0f;
        }

        light.canUseShadowMap = 0;
        if ( !( spawnflags & PRIMARY_NOSHADOWMAP ) )
        {
            if ( light.cosHalfFovOuter <= PRIMARY_LIGHT_MIN_COS_HALF_FOV )
            {
                PrimaryLight_Error( ent, light.dir,
                    "ignoring primary light because PRIMARY_NOSHADOWMAP is not checked "
                    "and fov_outer > 120" );
                continue;
            }
            light.canUseShadowMap = 1;
        }

        insertIndex = numBSPPrimaryLights;
        while ( insertIndex - 1 > 0 && PrimaryLightLess( &light, &bspPrimaryLights[insertIndex - 1] ) )
        {
            bspPrimaryLights[insertIndex] = bspPrimaryLights[insertIndex - 1];
            lightEntityNum[insertIndex]   = lightEntityNum[insertIndex - 1];
            insertIndex--;
        }
        bspPrimaryLights[insertIndex] = light;
        lightEntityNum[insertIndex]   = entNum;
        numBSPPrimaryLights++;
    }

    SanityCheck( numBSPPrimaryLights > 0 );
    SanityCheck( bspPrimaryLights[PRIMARY_LIGHT_NONE].type == GFX_LIGHT_TYPE_NONE );

    for ( lightIndex = 0; lightIndex < numBSPPrimaryLights; lightIndex++ )
    {
        lightType = bspPrimaryLights[lightIndex].type;
        if ( lightType == GFX_LIGHT_TYPE_NONE || lightType == GFX_LIGHT_TYPE_DIR )
            continue;

        SanityCheckx( lightType == GFX_LIGHT_TYPE_OMNI || lightType == GFX_LIGHT_TYPE_SPOT,
                      "(lightType) = %i", lightType );

        entNum = lightEntityNum[lightIndex];
        SanityCheckRange( entNum, 1, num_entities - 1 );

        ent        = &entities[entNum];
        spawnflags = IntForKey( ent, "spawnflags" );
        if ( !( spawnflags & PRIMARY_LIGHT_MOVING ) )
            continue;

        itoa( lightIndex, indexText, 10 );
        SetKeyValue( ent, "pl#", indexText );
    }
}


/* PrimaryLightLess  0x00432740 */
static qboolean PrimaryLightLess( const BspPrimaryLight_t *a, const BspPrimaryLight_t *b )
{
    int cmp;
    int i;

    if ( a->type != b->type )
        return a->type < b->type;

    cmp = Q_strcmp( a->defName, b->defName );
    if ( cmp )
        return cmp < 0;

    if ( a->type != GFX_LIGHT_TYPE_DIR )
    {
        for ( i = 0; i < 3; i++ )
            if ( a->origin[i] != b->origin[i] )
                return a->origin[i] < b->origin[i];

        if ( a->radius != b->radius )
            return a->radius < b->radius;

        if ( a->cosHalfFovOuter != b->cosHalfFovOuter )
            return a->cosHalfFovOuter < b->cosHalfFovOuter;

        if ( a->cosHalfFovInner != b->cosHalfFovInner )
            return a->cosHalfFovInner < b->cosHalfFovInner;

        if ( a->type == GFX_LIGHT_TYPE_SPOT &&
             *( const int * )&a->cosHalfFovExpanded != *( const int * )&b->cosHalfFovExpanded )
            return *( const int * )&a->cosHalfFovExpanded < *( const int * )&b->cosHalfFovExpanded;
    }

    for ( i = 0; i < 3; i++ )
        if ( a->dir[i] != b->dir[i] )
            return a->dir[i] < b->dir[i];

    return qfalse;
}


/* SetupSunPrimaryLight  0x00432980 */
static void SetupSunPrimaryLight( void )
{
    const char        *value;
    int                sunIsPrimary;
    float              sunlight;
    vec3_t             sunColor;
    float              diffuseFraction;
    vec3_t             sunDiffuseColor;
    float              ambient;
    vec3_t             ambientColor;
    float              diffuse;
    float              sun;
    int                i;
    float              ambientPart;
    float              diffusePart;
    float              sunPart;
    BspPrimaryLight_t *light;
    vec3_t             angles;

    value = ValueForKey( &entities[0], "sunIsPrimaryLight" );
    if ( sscanf( value, "%i", &sunIsPrimary ) == 1 && sunIsPrimary == 0 )
        return;

    sunlight = FloatForKey( &entities[0], "sunlight" );
    GetVectorForKey( &entities[0], "suncolor", sunColor );
    if ( Vec3MaxNormalize( sunColor, sunColor ) == 0.0 )
        Vec3Clear( sunColor );

    diffuseFraction = FloatForKey( &entities[0], "diffusefraction" );
    GetVectorForKey( &entities[0], "sundiffusecolor", sunDiffuseColor );
    if ( Vec3MaxNormalize( sunDiffuseColor, sunDiffuseColor ) == 0.0 )
        Vec3Clear( sunDiffuseColor );

    ambient = FloatForKey( &entities[0], "ambient" );
    GetVectorForKey( &entities[0], "_color", ambientColor );
    if ( Vec3MaxNormalize( ambientColor, ambientColor ) == 0.0 )
        Vec3Clear( ambientColor );

    if ( sunlight < ambient )
    {
        printf( "WARNING: ambient %g > sunlight %g, increasing sunlight to match ambient\n",
                ambient, sunlight );
        sunlight = ambient;
    }

    if ( diffuseFraction < 0.0 || diffuseFraction > 1.0 )
    {
        printf( "WARNING: clamping diffuseFraction %g to the range [0, 1]\n", diffuseFraction );
        diffuseFraction = I_fclamp( diffuseFraction, 0.0f, 1.0f );
    }

    diffuse = ( sunlight - ambient ) * diffuseFraction;
    sun     = ( sunlight - ambient ) - diffuse;

    for ( i = 0; i < 3; i++ )
    {
        ambientPart = ambientColor[i]    * ambient;
        diffusePart = sunDiffuseColor[i] * diffuse;
        sunPart     = sunColor[i]        * sun;

        ambientColor[i] = LinearToGamma( ambientPart );

        if ( sunPart + diffusePart == 0.0 )
        {
            sunDiffuseColor[i] = 0.0f;
            sunColor[i]        = 0.0f;
        }
        else
        {
            sunDiffuseColor[i] = ( LinearToGamma( sunPart + diffusePart + ambientPart )
                                   - ambientColor[i] ) * diffusePart / ( sunPart + diffusePart );
            sunColor[i]        = ( LinearToGamma( sunPart + diffusePart + ambientPart )
                                   - ambientColor[i] ) * sunPart / ( sunPart + diffusePart );
        }
    }

    light = &bspPrimaryLights[numBSPPrimaryLights];
    numBSPPrimaryLights++;

    memset( light, 0, sizeof( *light ) );
    light->type     = GFX_LIGHT_TYPE_DIR;
    light->color[0] = GammaToLinear( sunColor[0] );
    light->color[1] = GammaToLinear( sunColor[1] );
    light->color[2] = GammaToLinear( sunColor[2] );

    GetVectorForKey( &entities[0], "sundirection", angles );
    AngleVectors( angles, light->dir, NULL, NULL );
}


/* LinearToGamma / GammaToLinear  0x00432d00 / 0x00432d20 */
static float LinearToGamma( float value )
{
    return ( float )pow( value, 2.2f );
}

static float GammaToLinear( float value )
{
    return ( float )pow( value, 1.0f / 2.2f );
}


/* PrimaryLight_Error  0x00432d40 */
void PrimaryLight_Error( Entity_t *ent, const vec3_t normal, const char *message )
{
    MapError( 0, ent->origin, normal, ent->mapIndex, ent->entityNum, 0, "%s", message );
    SetKeyValue( ent, "intensity", "0" );
}


/* SunIsPrimaryLight  0x00432d90 */
qboolean SunIsPrimaryLight( void )
{
    Assert( numBSPPrimaryLights > 0 );

    if ( numBSPPrimaryLights < 2 )
        return qfalse;

    return bspPrimaryLights[1].type == GFX_LIGHT_TYPE_DIR;
}


/* BuildDrawSurfTree  0x00432df0 */
void BuildDrawSurfTree( void )
{
    AabbTreeBuilder_t builder;
    AabbTreeNode_t   *nodes;
    vec3_t           *itemMins;
    vec3_t           *itemMaxs;
    unsigned int      surfCount;
    unsigned int      nodeCount;
    int               surfIndex;
    int               vertIndex;
    int               nodeIndex;
    int               childIter;
    int               firstItem;
    vec3_t            mins;
    vec3_t            maxs;

    Assert( entity_num == 0 );
    Assert( entities[0].firstDrawSurf == 0 );

    surfCount = 0;
    while ( ( int )surfCount < numMapDrawSurfs &&
            ( drawSurfs[surfCount].mtlTex->toolFlags & 0x70 ) == TOOLFLAG_USAGE_LIT &&
            ( drawSurfs[surfCount].mtlTex->surfaceFlags & 0x40004 ) == 0 )
    {
        surfCount++;
    }

    if ( surfCount == 0 )
        Com_Error( "ERROR: Map must have at least one visible non-sky surface\n" );

    builder.itemData         = drawSurfs;
    builder.itemStride       = sizeof( DrawSurf_t );
    builder.itemCount        = surfCount;
    builder.reorderBounds    = 1;

    itemMins = new( std::nothrow ) vec3_t[surfCount];
    itemMaxs = new( std::nothrow ) vec3_t[surfCount];
    if ( !itemMins || !itemMaxs )
        Com_Error( "ERROR: out of memory" );

    for ( surfIndex = 0; surfIndex < ( int )surfCount; surfIndex++ )
    {
        ClearBounds( itemMins[surfIndex], itemMaxs[surfIndex] );
        for ( vertIndex = 0; vertIndex < drawSurfs[surfIndex].vertCount; vertIndex++ )
            AddPointToBounds( drawSurfs[surfIndex].verts[vertIndex].xyz,
                              itemMins[surfIndex], itemMaxs[surfIndex] );
    }

    builder.itemMins = itemMins;
    builder.itemMaxs = itemMaxs;
    builder.maxNodes = surfCount;

    nodes = ( AabbTreeNode_t * )new( std::nothrow ) AabbTreeNode_t[surfCount];
    if ( !nodes )
        Com_Error( "ERROR: out of memory" );

    builder.nodes            = nodes;
    builder.minPartitionSize = 2;
    builder.minLeafItems     = 4;

    nodeCount = AabbBuildTree( &builder );

    s_surfaceTree = ( SurfaceTreeNode_t * )new( std::nothrow ) SurfaceTreeNode_t[nodeCount];
    if ( !s_surfaceTree )
        Com_Error( "ERROR: out of memory" );

    for ( nodeIndex = 0; nodeIndex < ( int )nodeCount; nodeIndex++ )
    {
        firstItem = nodes[nodeIndex].firstItem;

        Vec3Copy( itemMins[firstItem], mins );
        Vec3Copy( itemMaxs[firstItem], maxs );
        for ( childIter = 0; childIter < nodes[nodeIndex].itemCount; childIter++ )
            AddBoundsToBounds( itemMins[firstItem + childIter], itemMaxs[firstItem + childIter],
                               mins, maxs );

        Vec3Mid( mins, maxs, s_surfaceTree[nodeIndex].midPoint );
        Vec3Sub( maxs, s_surfaceTree[nodeIndex].midPoint, s_surfaceTree[nodeIndex].halfSize );

        s_surfaceTree[nodeIndex].firstSurf  = &drawSurfs[nodes[nodeIndex].firstItem];
        s_surfaceTree[nodeIndex].surfCount  = nodes[nodeIndex].itemCount;
        s_surfaceTree[nodeIndex].firstChild = &s_surfaceTree[nodes[nodeIndex].firstChild];
        s_surfaceTree[nodeIndex].childCount = nodes[nodeIndex].childCount;
    }

    delete[] nodes;
    delete[] itemMaxs;
    delete[] itemMins;
}


/* TraceSurfaceOccluder  0x00433240 */
static DrawSurf_t *TraceSurfaceOccluder( const vec3_t p0, const vec3_t p1, DrawSurf_t *lastHit )
{
    Segment_t seg;

    Vec3Mid( p0, p1, seg.midPoint );
    Vec3Sub( seg.midPoint, p0, seg.dir );

    seg.absDir[0] = I_fabs( seg.dir[0] );
    seg.absDir[1] = I_fabs( seg.dir[1] );
    seg.absDir[2] = I_fabs( seg.dir[2] );

    if ( !lastHit || !SegmentHitsDrawSurf( &seg, lastHit ) )
        lastHit = SegmentHitsTreeNode( s_surfaceTree, &seg );

    return lastHit;
}


/* SegmentHitsDrawSurf  0x004332e0 */
static qboolean SegmentHitsDrawSurf( const Segment_t *seg, const DrawSurf_t *ds )
{
    if ( ds->isModel )
        return SegmentHitsIndexedMesh( seg, ds );
    if ( ds->isPatch )
        return SegmentHitsPatch( seg, ds );
    return SegmentHitsBrushFace( seg, ds );
}


/* SegmentHitsIndexedMesh  0x00433330 */
static qboolean SegmentHitsIndexedMesh( const Segment_t *seg, const DrawSurf_t *ds )
{
    int i;

    for ( i = 0; i < ds->terrain.indexCount; i += 3 )
    {
        if ( SegmentIntersectsTriangle( seg,
                                        ds->verts[ds->terrain.indexes[i + 0]].xyz,
                                        ds->verts[ds->terrain.indexes[i + 1]].xyz,
                                        ds->verts[ds->terrain.indexes[i + 2]].xyz ) )
            return qtrue;
    }

    return qfalse;
}


/* SegmentIntersectsTriangle  0x004333d0 */
static qboolean SegmentIntersectsTriangle( const Segment_t *seg,
                                           const vec3_t v0, const vec3_t v1, const vec3_t v2 )
{
    vec3_t start;
    vec3_t e1;
    vec3_t e2;
    vec3_t normal;
    vec3_t toV0;
    vec3_t cross;
    float  det;
    float  sign;
    float  d;
    float  u;
    float  v;

    Vec3Sub( seg->midPoint, seg->dir, start );

    Vec3Sub( v0, v1, e1 );
    Vec3Sub( v0, v2, e2 );
    Vec3Cross( e2, e1, normal );

    det  = Vec3Dot( seg->dir, normal );
    sign = 1.0f;
    if ( det >= 0.0 )
    {
        det  = -det;
        sign = -1.0f;
    }

    Vec3Sub( v0, seg->midPoint, toV0 );
    d = Vec3Dot( toV0, normal );
    if ( I_fabs( d ) >= -det )
        return qfalse;

    Vec3Cross( seg->dir, toV0, cross );

    u = Vec3Dot( cross, e1 );
    u = u * sign;
    if ( u > 0.0 || u < det )
        return qfalse;

    v = Vec3Dot( cross, e2 );
    v = v * sign;
    if ( v < 0.0 || u - v < det )
        return qfalse;

    return qtrue;
}


/* SegmentHitsPatch  0x00433550 */
static qboolean SegmentHitsPatch( const Segment_t *seg, const DrawSurf_t *ds )
{
    Mesh_t  src;
    Mesh_t *subdivided;
    Mesh_t *mesh;
    int     row;
    int     col;

    src.width  = ds->patch.width;
    src.height = ds->patch.height;
    src.verts  = ds->verts;

    subdivided = SubdivideMesh( src, ( float )ds->subdivisions,
                                PRIMARY_LIGHT_PATCH_MIN_LENGTH, NULL, NULL );
    PutMeshOnCurve( *subdivided );
    mesh = RemoveLinearMeshColumnsRows( subdivided, NULL, NULL );
    FreeMesh( subdivided );

    for ( col = 0; col < mesh->width - 1; col++ )
    {
        for ( row = 0; row < mesh->height - 1; row++ )
        {
            if ( SegmentIntersectsTriangle( seg,
                     mesh->verts[( row + 1 ) * mesh->width + col].xyz,
                     mesh->verts[( row + 1 ) * mesh->width + 1 + col].xyz,
                     mesh->verts[row * mesh->width + 1 + col].xyz ) )
            {
                FreeMesh( mesh );
                return qtrue;
            }

            if ( SegmentIntersectsTriangle( seg,
                     mesh->verts[row * mesh->width + 1 + col].xyz,
                     mesh->verts[row * mesh->width + col].xyz,
                     mesh->verts[( row + 1 ) * mesh->width + col].xyz ) )
            {
                FreeMesh( mesh );
                return qtrue;
            }
        }
    }

    FreeMesh( mesh );
    return qfalse;
}


/* SegmentHitsBrushFace  0x00433770 */
static qboolean SegmentHitsBrushFace( const Segment_t *seg, const DrawSurf_t *ds )
{
    int i;

    for ( i = 2; i < ds->vertCount; i++ )
    {
        if ( SegmentIntersectsTriangle( seg, ds->verts[0].xyz,
                                        ds->verts[i - 1].xyz, ds->verts[i].xyz ) )
            return qtrue;
    }

    return qfalse;
}


/* SegmentHitsTreeNode  0x004337e0 */
static DrawSurf_t *SegmentHitsTreeNode( const SurfaceTreeNode_t *node, const Segment_t *seg )
{
    DrawSurf_t *ds;
    DrawSurf_t *hit;
    int         i;

    if ( node->childCount == 0 )
    {
        for ( i = 0; i < node->surfCount; i++ )
        {
            ds = &node->firstSurf[i];
            if ( SegmentHitsDrawSurf( seg, ds ) )
                return ds;
        }
        return NULL;
    }

    for ( i = 0; i < node->childCount; i++ )
    {
        if ( !SegmentOverlapsBox( seg, node->midPoint, node->halfSize ) )
            continue;
        hit = SegmentHitsTreeNode( &node->firstChild[i], seg );
        if ( hit )
            return hit;
    }

    return NULL;
}


/* SegmentOverlapsBox  0x004338b0 */
static qboolean SegmentOverlapsBox( const Segment_t *seg, const vec3_t center, const vec3_t halfSize )
{
    float dx;
    float dy;
    float dz;

    dx = seg->midPoint[0] - center[0];
    if ( I_fabs( dx ) > seg->absDir[0] + halfSize[0] )
        return qfalse;

    dy = seg->midPoint[1] - center[1];
    if ( I_fabs( dy ) > seg->absDir[1] + halfSize[1] )
        return qfalse;

    dz = seg->midPoint[2] - center[2];
    if ( I_fabs( dz ) > seg->absDir[2] + halfSize[2] )
        return qfalse;

    if ( I_fabs( seg->dir[1] * dz - seg->dir[2] * dy ) >
         halfSize[2] * seg->absDir[1] + halfSize[1] * seg->absDir[2] )
        return qfalse;

    if ( I_fabs( seg->dir[2] * dx - seg->dir[0] * dz ) >
         halfSize[0] * seg->absDir[2] + halfSize[2] * seg->absDir[0] )
        return qfalse;

    if ( I_fabs( seg->dir[0] * dy - seg->dir[1] * dx ) >
         halfSize[1] * seg->absDir[0] + halfSize[0] * seg->absDir[1] )
        return qfalse;

    return qtrue;
}


/* AssignPrimaryLightsToSurface  0x00433a50 */
void AssignPrimaryLightsToSurface( TriSurf_t *surf )
{
    PrimaryLightTraceCtx_t ctx;
    poly2dBuf_t            coords[4];
    vec2_t                 mins;
    vec2_t                 maxs;
    float                  scale;
    float                  invScale;
    float                  startX;
    float                  startY;
    int                    countX;
    int                    countY;
    unsigned int           i;
    int                    lightIndex;

    if ( !( surf->props->ds->mtlTex->techSetFlags & 2 ) )
        return;
    if ( surf->props->ds->primaryLightIndex == PRIMARY_LIGHT_NONE )
        return;

    WindingLightmapCoords( surf->w, surf->props->lmapVecs, coords[0] );

    ClearBounds2D( mins, maxs );
    for ( i = 0; i < surf->w->ptCount; i++ )
        AddPointToBounds2D( coords[0][i], mins, maxs );

    scale    = 1.0f;
    invScale = 1.0f / scale;
    startX   = floor( mins[0] * scale );
    startY   = floor( mins[1] * scale );
    countX   = ( int )( ceil( maxs[0] * scale ) - startX );
    countY   = ( int )( ceil( maxs[1] * scale ) - startY );

    memset( &ctx, 0, sizeof( ctx ) );
    ctx.surf = surf;

    LightmapVecsToAxes( surf->props->plane, surf->props->lmapVecs,
                        ctx.sVec, ctx.tVec, ctx.origin );
    Vec3Add( ctx.origin, entities[entity_num].origin, ctx.origin );

    for ( lightIndex = 1; lightIndex < numBSPPrimaryLights; lightIndex++ )
    {
        if ( surf->props->ds->primaryLightIndex == lightIndex )
            continue;
        if ( PrimaryLightAffectsSurface( lightIndex, surf ) )
            ctx.lightAffects[lightIndex] = 1;
    }

    Poly2dSubdivideGrid( coords, surf->w->ptCount, countX, countY,
                         startX * invScale, startY * invScale, invScale, invScale,
                         AssignPrimaryLightsAtTexel, &ctx );
}


/* AssignPrimaryLightsAtTexel  0x00433d50 */
static void AssignPrimaryLightsAtTexel( float area, const vec2_t center, const vec2_t *coords,
                                        int vertCount, void *userData, int cellIndex )
{
    PrimaryLightTraceCtx_t *ctx;
    DrawSurf_t             *ds;
    vec3_t                  point;
    int                     lightIndex;

    ctx = ( PrimaryLightTraceCtx_t * )userData;
    ds  = ctx->surf->props->ds;

    if ( ds->primaryLightIndex == PRIMARY_LIGHT_NONE )
        return;

    Vec3MadCombine( ctx->origin, center[0], ctx->sVec, center[1], ctx->tVec, point );
    Vec3Mad( point, 0.1f, ctx->surf->props->plane, point );

    for ( lightIndex = 0; lightIndex < numBSPPrimaryLights; lightIndex++ )
    {
        if ( !ctx->lightAffects[lightIndex] )
            continue;
        if ( !PrimaryLightReachesPoint( &bspPrimaryLights[lightIndex], point ) )
            continue;

        ctx->lastOccluder[lightIndex] = TracePrimaryLightToPoint( &bspPrimaryLights[lightIndex],
                                                                 point,
                                                                 ctx->lastOccluder[lightIndex] );
        if ( ctx->lastOccluder[lightIndex] )
            continue;

        ctx->lightAffects[lightIndex] = 0;
        if ( !SetSurfacePrimaryLight( ctx->surf, point, lightIndex ) )
            return;
    }
}


/* SetSurfacePrimaryLight  0x00433ea0 */
static qboolean SetSurfacePrimaryLight( TriSurf_t *surf, const vec3_t point, int lightIndex )
{
    DrawSurf_t *ds;
    int         prev;

    ds   = surf->props->ds;
    prev = ds->primaryLightIndex;

    if ( ds->primaryLightIndex == PRIMARY_LIGHT_INVALID )
        ds->primaryLightIndex = lightIndex;

    if ( prev == PRIMARY_LIGHT_INVALID || prev == lightIndex )
        return qtrue;

    MapError( 0, point, surf->props->plane,
              ds->mapInfoIndex, ds->entityNum, ds->brushNum,
              "Surface '%s' is affected by more than one primary light: %s and %s",
              StringFromOffset( ds->mtlTex ),
              PrimaryLightName( prev ),
              PrimaryLightName( lightIndex ) );

    ds->primaryLightIndex = PRIMARY_LIGHT_NONE;
    return qfalse;
}


/* PrimaryLightName  0x00433f60 */
static const char *PrimaryLightName( int lightIndex )
{
    const char *kind;

    Assert( lightIndex != PRIMARY_LIGHT_NONE );

    if ( bspPrimaryLights[lightIndex].type == GFX_LIGHT_TYPE_DIR )
        return "sun";

    if ( bspPrimaryLights[lightIndex].type == GFX_LIGHT_TYPE_SPOT )
        kind = "spotlight";
    else
        kind = "point light";

    return va( "%s at %.0f %.0f %.0f", kind,
               bspPrimaryLights[lightIndex].origin[0],
               bspPrimaryLights[lightIndex].origin[1],
               bspPrimaryLights[lightIndex].origin[2] );
}


/* TracePrimaryLightToPoint  0x00434000 */
static DrawSurf_t *TracePrimaryLightToPoint( const BspPrimaryLight_t *light,
                                             const vec3_t point, DrawSurf_t *lastHit )
{
    vec3_t sunOrigin;

    if ( light->type == GFX_LIGHT_TYPE_DIR )
    {
        Vec3Mad( point, 262144.0f, light->dir, sunOrigin );
        return TraceSurfaceOccluder( point, sunOrigin, lastHit );
    }

    return TraceSurfaceOccluder( point, light->origin, lastHit );
}


/* PrimaryLightReachesPoint  0x00434070 */
static qboolean PrimaryLightReachesPoint( const BspPrimaryLight_t *light, const vec3_t point )
{
    vec3_t delta;
    float  distSq;
    float  range;
    float  cosExpanded;
    float  dot;
    float  dist;

    if ( light->type == GFX_LIGHT_TYPE_DIR )
        return qtrue;

    Vec3Sub( light->origin, point, delta );
    distSq = Vec3LengthSq( delta );

    range = light->radius + light->translationLimit;
    if ( distSq > range * range )
        return qfalse;

    if ( light->type == GFX_LIGHT_TYPE_OMNI )
        return qtrue;

    if ( light->rotationLimit == 1.0 )
    {
        dot = Vec3Dot( delta, light->dir );
        if ( dot < 0.0 )
            return qfalse;
        return light->cosHalfFovOuter * light->cosHalfFovOuter * distSq <= dot * dot;
    }

    if ( light->rotationLimit <= -light->cosHalfFovOuter )
        return qtrue;

    cosExpanded = CosOfAngleSum( light->cosHalfFovOuter, light->rotationLimit );
    dot         = Vec3Dot( delta, light->dir );
    dist        = I_sqrt( distSq );

    return dot >= cosExpanded * dist;
}


/* PrimaryLightAffectsSurface  0x004341e0 */
static qboolean PrimaryLightAffectsSurface( int lightIndex, const TriSurf_t *surf )
{
    const BspPrimaryLight_t *light;
    vec3_t                   lightPos;
    vec3_t                   mid;
    vec3_t                   halfSize;
    float                    d;
    float                    cosExpanded;
    float                    dist;

    light = &bspPrimaryLights[lightIndex];

    if ( light->type == GFX_LIGHT_TYPE_DIR )
        return Vec3Dot( light->dir, surf->props->plane ) > 0.0f;

    Vec3Sub( light->origin, entities[entity_num].origin, lightPos );

    d = Vec3Dot( lightPos, surf->props->plane ) - surf->props->plane[3];
    if ( d < 0.0f || d > light->radius )
        return qfalse;

    if ( light->type == GFX_LIGHT_TYPE_OMNI )
    {
        if ( Vec3DistanceSqToBounds( lightPos, surf->mins, surf->maxs ) >
             light->radius * light->radius )
            return qfalse;

        Vec3Mid( surf->mins, surf->maxs, mid );
        Vec3Sub( mid, surf->mins, halfSize );
    }
    else
    {
        SanityCheckCmp( light->type, ==, GFX_LIGHT_TYPE_SPOT );

        cosExpanded = PrimaryLightCosHalfFovExpanded( light );
        if ( cosExpanded <= 0.0f )
        {
            if ( Vec3DistanceSqToBounds( lightPos, surf->mins, surf->maxs ) >
                 light->radius * light->radius )
                return qfalse;

            dist = Vec3Dot( light->origin, light->dir ) - cosExpanded * light->radius;
            if ( !WindingPlaneSide( surf->w, light->dir, dist ) )
                return qfalse;

            Vec3Mid( surf->mins, surf->maxs, mid );
            Vec3Sub( mid, surf->mins, halfSize );
        }
        else
        {
            Vec3Mid( surf->mins, surf->maxs, mid );
            Vec3Sub( mid, surf->mins, halfSize );

            if ( BoxInConeRange( lightPos, light->dir, cosExpanded, light->radius,
                                 mid, halfSize ) )
                return qfalse;
        }
    }

    Vec3Sub( mid, lightPos, mid );

    return !LightRegionSeparatesSurface( surf->w, mid, halfSize, lightPos,
                                         &s_lightRegion[lightIndex] );
}


/* LightRegionSeparatesSurface  0x004344f0 */
static qboolean LightRegionSeparatesSurface( const winding_t *w, const vec3_t mid,
                                             const vec3_t halfSize, const vec3_t lightPos,
                                             const LightRegion_t *region )
{
    unsigned int i;

    for ( i = 0; i < region->hullCount; i++ )
    {
        if ( !HullSeparatesSurface( w, mid, halfSize, lightPos, region->hulls[i] ) )
            return qfalse;
    }

    return qtrue;
}


/* HullSeparatesSurface  0x00434550 */
static qboolean HullSeparatesSurface( const winding_t *w, const vec3_t mid, const vec3_t halfSize,
                                      const vec3_t lightPos, const BspLightRegionHull_t *hull )
{
    const BspLightRegionAxis_t *axes;
    float                       sum01;
    float                       sum02;
    float                       sum12;
    float                       minDist;
    float                       maxDist;
    float                       extent;
    float                       dist;
    unsigned int                i;

    if ( I_fabs( hull->kdopMidPoint[0] - mid[0] ) > hull->kdopHalfSize[0] + halfSize[0] + 0.1f )
        return qtrue;
    if ( I_fabs( hull->kdopMidPoint[1] - mid[1] ) > hull->kdopHalfSize[1] + halfSize[1] + 0.1f )
        return qtrue;
    if ( I_fabs( hull->kdopMidPoint[2] - mid[2] ) > hull->kdopHalfSize[2] + halfSize[2] + 0.1f )
        return qtrue;

    sum01 = halfSize[0] + halfSize[1];
    if ( I_fabs( hull->kdopMidPoint[3] - ( mid[0] + mid[1] ) ) > hull->kdopHalfSize[3] + sum01 + 0.1f )
        return qtrue;
    if ( I_fabs( hull->kdopMidPoint[4] - ( mid[0] - mid[1] ) ) > hull->kdopHalfSize[4] + sum01 + 0.1f )
        return qtrue;

    sum02 = halfSize[0] + halfSize[2];
    if ( I_fabs( hull->kdopMidPoint[5] - ( mid[0] + mid[2] ) ) > hull->kdopHalfSize[5] + sum02 + 0.1f )
        return qtrue;
    if ( I_fabs( hull->kdopMidPoint[6] - ( mid[0] - mid[2] ) ) > hull->kdopHalfSize[6] + sum02 + 0.1f )
        return qtrue;

    sum12 = halfSize[1] + halfSize[2];
    if ( I_fabs( hull->kdopMidPoint[7] - ( mid[1] + mid[2] ) ) > hull->kdopHalfSize[7] + sum12 + 0.1f )
        return qtrue;
    if ( I_fabs( hull->kdopMidPoint[8] - ( mid[1] - mid[2] ) ) > hull->kdopHalfSize[8] + sum12 + 0.1f )
        return qtrue;

    axes = ( const BspLightRegionAxis_t * )( hull + 1 );
    for ( i = 0; i < hull->axisCount; i++ )
    {
        dist = Vec3Dot( axes[i].dir, lightPos );
        WindingPlaneDistExtent( w, axes[i].dir, dist, &minDist, &maxDist );

        extent = maxDist - minDist;
        if ( I_fabs( axes[i].midPoint - ( maxDist + minDist ) * 0.5f ) >
             axes[i].halfSize + extent * 0.5f + 0.1f )
            return qtrue;
    }

    if ( WindingSeparatedAlongAxis( w, 1.0f,  1.0f, 0.0f, lightPos[0] + lightPos[1],
                                    hull->kdopMidPoint[3], hull->kdopHalfSize[3] + 0.1f ) )
        return qtrue;
    if ( WindingSeparatedAlongAxis( w, 1.0f, -1.0f, 0.0f, lightPos[0] - lightPos[1],
                                    hull->kdopMidPoint[4], hull->kdopHalfSize[4] + 0.1f ) )
        return qtrue;
    if ( WindingSeparatedAlongAxis( w, 1.0f, 0.0f,  1.0f, lightPos[0] + lightPos[2],
                                    hull->kdopMidPoint[5], hull->kdopHalfSize[5] + 0.1f ) )
        return qtrue;
    if ( WindingSeparatedAlongAxis( w, 1.0f, 0.0f, -1.0f, lightPos[0] - lightPos[2],
                                    hull->kdopMidPoint[6], hull->kdopHalfSize[6] + 0.1f ) )
        return qtrue;
    if ( WindingSeparatedAlongAxis( w, 0.0f, 1.0f,  1.0f, lightPos[1] + lightPos[2],
                                    hull->kdopMidPoint[7], hull->kdopHalfSize[7] + 0.1f ) )
        return qtrue;
    if ( WindingSeparatedAlongAxis( w, 0.0f, 1.0f, -1.0f, lightPos[1] - lightPos[2],
                                    hull->kdopMidPoint[8], hull->kdopHalfSize[8] + 0.1f ) )
        return qtrue;

    return qfalse;
}


/* WindingSeparatedAlongAxis  0x00434b20 */
static qboolean WindingSeparatedAlongAxis( const winding_t *w, float x, float y, float z,
                                           float dist, float slabMid, float slabHalfSize )
{
    vec3_t normal;
    float  minDist;
    float  maxDist;

    Vec3Set( normal, x, y, z );
    WindingPlaneDistExtent( w, normal, dist, &minDist, &maxDist );

    return I_fabs( slabMid - ( maxDist + minDist ) * 0.5f ) >
           slabHalfSize + ( maxDist - minDist ) * 0.5f;
}


/* PrimaryLightCosHalfFovExpanded  0x00434bc0 */
float PrimaryLightCosHalfFovExpanded( const BspPrimaryLight_t *light )
{
    if ( light->rotationLimit == 1.0 )
        return light->cosHalfFovOuter;

    if ( light->rotationLimit <= -light->cosHalfFovOuter )
        return -1.0f;

    return CosOfAngleSum( light->cosHalfFovOuter, light->cosHalfFovInner );
}


/* SavePrimaryLights  0x00434c20 */
void SavePrimaryLights( void )
{
    s_savedPrimaryLightCount = numBSPPrimaryLights;
    memcpy( s_savedPrimaryLights, bspPrimaryLights,
            numBSPPrimaryLights * sizeof( BspPrimaryLight_t ) );
}


/* ComparePrimaryLights  0x00434c50 */
void ComparePrimaryLights( void )
{
    int   lightIndex;
    int   errorCount;
    float oldFovOuter;
    float newFovOuter;

    if ( s_savedPrimaryLightCount != numBSPPrimaryLights )
        Com_Error( "ERROR: Primary light count changed from %i to %i.  "
                   "You cannot use '-onlyents' if you add or remove primary lights.\n",
                   s_savedPrimaryLightCount, numBSPPrimaryLights );

    errorCount = 0;

    for ( lightIndex = 0; lightIndex < numBSPPrimaryLights; lightIndex++ )
    {
        if ( s_savedPrimaryLights[lightIndex].type != bspPrimaryLights[lightIndex].type )
        {
            Com_Printf( "ERROR: Primary light %i changed types\n", lightIndex );
            errorCount++;
        }
        else if ( bspPrimaryLights[lightIndex].type == GFX_LIGHT_TYPE_DIR ||
                  bspPrimaryLights[lightIndex].type == GFX_LIGHT_TYPE_OMNI )
        {
            if ( !Vec3Compare( s_savedPrimaryLights[lightIndex].origin,
                               bspPrimaryLights[lightIndex].origin ) )
            {
                Com_Printf( "ERROR: Primary light %i moved from (%g %g %g) to (%g %g %g)\n",
                            lightIndex,
                            s_savedPrimaryLights[lightIndex].origin[0],
                            s_savedPrimaryLights[lightIndex].origin[1],
                            s_savedPrimaryLights[lightIndex].origin[2],
                            bspPrimaryLights[lightIndex].origin[0],
                            bspPrimaryLights[lightIndex].origin[1],
                            bspPrimaryLights[lightIndex].origin[2] );
                errorCount++;
            }

            if ( s_savedPrimaryLights[lightIndex].radius < bspPrimaryLights[lightIndex].radius )
            {
                Com_Printf( "ERROR: Primary light %i radius increased from %g to %g\n",
                            lightIndex,
                            s_savedPrimaryLights[lightIndex].radius,
                            bspPrimaryLights[lightIndex].radius );
                errorCount++;
            }

            if ( bspPrimaryLights[lightIndex].cosHalfFovOuter <
                 s_savedPrimaryLights[lightIndex].cosHalfFovOuter )
            {
                newFovOuter = ( float )acos( bspPrimaryLights[lightIndex].cosHalfFovOuter ) * 2.0f * RAD2DEG;
                oldFovOuter = ( float )acos( s_savedPrimaryLights[lightIndex].cosHalfFovOuter ) * 2.0f * RAD2DEG;
                Com_Printf( "ERROR: Primary light %i fov_outer increased from %g to %g\n",
                            lightIndex, oldFovOuter, newFovOuter );
                errorCount++;
            }

            if ( bspPrimaryLights[lightIndex].type == GFX_LIGHT_TYPE_DIR &&
                 s_savedPrimaryLights[lightIndex].rotationLimit <
                 bspPrimaryLights[lightIndex].rotationLimit )
            {
                Com_Printf( "ERROR: Primary light %i maxturn increased from %g to %g\n",
                            lightIndex,
                            s_savedPrimaryLights[lightIndex].rotationLimit,
                            bspPrimaryLights[lightIndex].rotationLimit );
                errorCount++;
            }
        }

        if ( !Vec3Compare( s_savedPrimaryLights[lightIndex].dir,
                           bspPrimaryLights[lightIndex].dir ) )
        {
            Com_Printf( "ERROR: Primary light %i changed direction from (%g %g %g) to (%g %g %g)\n",
                        lightIndex,
                        s_savedPrimaryLights[lightIndex].dir[0],
                        s_savedPrimaryLights[lightIndex].dir[1],
                        s_savedPrimaryLights[lightIndex].dir[2],
                        bspPrimaryLights[lightIndex].dir[0],
                        bspPrimaryLights[lightIndex].dir[1],
                        bspPrimaryLights[lightIndex].dir[2] );
            errorCount++;
        }
    }

    if ( errorCount )
        Com_Error( "Canceling -onlyents compile due to %i primary light errors\n", errorCount );
}


/* BuildPrimaryLightRegions  0x00434f40 */
void BuildPrimaryLightRegions( void )
{
    int                 firstSurfCount;
    unsigned int        shadowSurfCount;
    int                *shadowSurfIndex;
    DrawSurf_t        **litSurfs;
    vec3_t             *surfMids;
    vec3_t             *surfHalfSizes;
    int                 surfIndex;
    int                 i;
    unsigned int        lightIndex;
    unsigned int        litSurfCount;
    PrimaryLightInfo_t  lightInfo;
    void               *builder;

    Assert( entity_num == 0 );
    Assert( entities[0].firstDrawSurf == 0 );

    firstSurfCount = 0;
    while ( firstSurfCount < numMapDrawSurfs &&
            ( drawSurfs[firstSurfCount].mtlTex->toolFlags & 0x70 ) == TOOLFLAG_USAGE_LIT &&
            ( drawSurfs[firstSurfCount].mtlTex->surfaceFlags & 0x40004 ) == 0 )
    {
        firstSurfCount++;
    }

    if ( firstSurfCount == 0 )
        Com_Error( "ERROR: Map must have at least one visible non-sky surface\n" );

    shadowSurfCount = 0;
    for ( surfIndex = firstSurfCount; surfIndex < numMapDrawSurfs; surfIndex++ )
        if ( Material_CastsShadow( drawSurfs[surfIndex].mtlTex ) )
            shadowSurfCount++;

    if ( shadowSurfCount == 0 )
    {
        shadowSurfIndex = NULL;
    }
    else
    {
        shadowSurfIndex = new( std::nothrow ) int[shadowSurfCount];
        if ( !shadowSurfIndex )
            Com_Error( "ERROR: out of memory" );

        shadowSurfCount = 0;
        for ( surfIndex = firstSurfCount; surfIndex < numMapDrawSurfs; surfIndex++ )
        {
            if ( Material_CastsShadow( drawSurfs[surfIndex].mtlTex ) )
            {
                shadowSurfIndex[shadowSurfCount] = surfIndex;
                shadowSurfCount++;
            }
        }
    }

    litSurfs      = new( std::nothrow ) DrawSurf_t *[firstSurfCount + shadowSurfCount];
    surfMids      = new( std::nothrow ) vec3_t[firstSurfCount + shadowSurfCount];
    surfHalfSizes = new( std::nothrow ) vec3_t[firstSurfCount + shadowSurfCount];
    if ( !litSurfs || !surfMids || !surfHalfSizes )
        Com_Error( "ERROR: out of memory" );

    for ( surfIndex = 0; surfIndex < firstSurfCount; surfIndex++ )
        DrawSurfBoundsMidHalf( surfIndex, surfMids[surfIndex], surfHalfSizes[surfIndex] );

    for ( i = 0; i < ( int )shadowSurfCount; i++ )
        DrawSurfBoundsMidHalf( shadowSurfIndex[i],
                               surfMids[firstSurfCount + i],
                               surfHalfSizes[firstSurfCount + i] );

    lightIndex = SunIsPrimaryLight() ? 1 : 0;
    while ( ++lightIndex < ( unsigned int )numBSPPrimaryLights )
    {
        Vec3Copy( bspPrimaryLights[lightIndex].origin, lightInfo.origin );
        Vec3Copy( bspPrimaryLights[lightIndex].dir, lightInfo.dir );
        lightInfo.cosHalfFov = PrimaryLightCosHalfFovExpanded( &bspPrimaryLights[lightIndex] );
        lightInfo.radius     = bspPrimaryLights[lightIndex].radius;
        lightInfo.type       = bspPrimaryLights[lightIndex].type;

        litSurfCount = GatherLitSurfaces( &lightInfo, firstSurfCount, shadowSurfCount,
                                          shadowSurfIndex, surfMids, surfHalfSizes, litSurfs );

        builder = AddSurfacesToLightRegion( &lightInfo, litSurfs, litSurfCount );

        LightRegion_BuildFaces( &builder, &lightInfo );
        s_lightRegion[lightIndex].hullCount =
            LightRegion_BuildHulls( &builder, &lightInfo, MAX_HULLS_PER_LIGHT,
                                    s_lightRegion[lightIndex].hulls );

        Assertx( s_lightRegion[lightIndex].hullCount <= MAX_HULLS_PER_LIGHT, "%i, %i",
                 s_lightRegion[lightIndex].hullCount, MAX_HULLS_PER_LIGHT );

        LightRegion_FreeBuilder( builder );
    }

    delete[] litSurfs;
    delete[] surfMids;
    delete[] surfHalfSizes;
    if ( shadowSurfIndex )
        delete[] shadowSurfIndex;

    EmitLightRegions();
}


/* AddSurfacesToLightRegion  0x00435400 */
static void *AddSurfacesToLightRegion( const PrimaryLightInfo_t *light,
                                       DrawSurf_t **surfs, unsigned int surfCount )
{
    void        *builder;
    unsigned int i;
    qboolean     twoSided;

    builder = NULL;

    for ( i = 0; i < surfCount; i++ )
    {
        twoSided = ( surfs[i]->mtlTex->toolFlags & 0x2000 ) == 0;

        if ( surfs[i]->isPatch )
            AddPatchToLightRegion( light, surfs[i], twoSided, &builder );
        else if ( surfs[i]->isModel )
            AddIndexedMeshToLightRegion( light, surfs[i], twoSided, &builder );
        else
            AddBrushFaceToLightRegion( light, surfs[i], twoSided, &builder );
    }

    return builder;
}


/* AddPatchToLightRegion  0x004354e0 */
static void AddPatchToLightRegion( const PrimaryLightInfo_t *light, const DrawSurf_t *ds,
                                   qboolean twoSided, void **builder )
{
    Mesh_t     src;
    Mesh_t    *subdivided;
    Mesh_t    *mesh;
    winding_t *wQuad;
    winding_t *wTri;
    vec4_t     planeQuad;
    vec4_t     planeTri;
    int        col;
    int        row;
    int        i0;
    int        i1;
    int        i2;
    int        haveQuad;
    int        haveTri;

    src.width  = ds->patch.width;
    src.height = ds->patch.height;
    src.verts  = ds->verts;

    subdivided = SubdivideMesh( src, ( float )ds->subdivisions,
                                PRIMARY_LIGHT_PATCH_MIN_LENGTH, NULL, NULL );
    PutMeshOnCurve( *subdivided );
    mesh = RemoveLinearMeshColumnsRows( subdivided, NULL, NULL );
    FreeMesh( subdivided );

    wQuad = AllocWinding( 4 );
    wTri  = AllocWinding( 3 );

    for ( col = 0; col < mesh->width - 1; col++ )
    {
        for ( row = 0; row < mesh->height - 1; row++ )
        {
            i0 = ( row + 1 ) * mesh->width + col;
            i1 = ( row + 1 ) * mesh->width + 1 + col;
            i2 = row * mesh->width + 1 + col;

            haveQuad = PlaneFromPoints( planeQuad, mesh->verts[i0].xyz,
                                        mesh->verts[i1].xyz, mesh->verts[i2].xyz );
            if ( haveQuad )
            {
                wQuad->ptCount = 3;
                Vec3Copy( mesh->verts[i0].xyz, wQuad->pts[0] );
                Vec3Copy( mesh->verts[i1].xyz, wQuad->pts[1] );
                Vec3Copy( mesh->verts[i2].xyz, wQuad->pts[2] );
            }

            i0 = row * mesh->width + 1 + col;
            i1 = row * mesh->width + col;
            i2 = ( row + 1 ) * mesh->width + col;

            haveTri = PlaneFromPoints( planeTri, mesh->verts[i0].xyz,
                                       mesh->verts[i1].xyz, mesh->verts[i2].xyz );
            if ( haveTri )
            {
                wTri->ptCount = 3;
                Vec3Copy( mesh->verts[i0].xyz, wTri->pts[0] );
                Vec3Copy( mesh->verts[i1].xyz, wTri->pts[1] );
                Vec3Copy( mesh->verts[i2].xyz, wTri->pts[2] );
            }

            if ( haveQuad && haveTri &&
                 WindingsCoincident( wQuad, planeQuad, planeQuad[3], wTri, planeTri, planeTri[3] ) )
            {
                Vec3Copy( wTri->pts[1], wQuad->pts[3] );
                wQuad->ptCount = 4;
                haveTri        = 0;
            }

            if ( haveQuad )
                LightRegion_AddFace( builder, light, wQuad, twoSided, ds );
            if ( haveTri )
                LightRegion_AddFace( builder, light, wTri, twoSided, ds );
        }
    }

    FreeWinding( wQuad );
    FreeWinding( wTri );
    FreeMesh( mesh );
}


/* AddIndexedMeshToLightRegion  0x00435870 */
static void AddIndexedMeshToLightRegion( const PrimaryLightInfo_t *light, const DrawSurf_t *ds,
                                         qboolean twoSided, void **builder )
{
    winding_t *w;
    vec4_t     plane;
    int        i;
    int        j;
    int        vertIndex;

    w = AllocWinding( 4 );

    for ( i = 0; i < ds->terrain.indexCount; i += 3 )
    {
        w->ptCount = 3;
        for ( j = 0; j < 3; j++ )
        {
            vertIndex = ds->terrain.indexes[i + j];
            Vec3Copy( ds->verts[vertIndex].xyz, w->pts[j] );
        }

        if ( !PlaneFromPoints( plane, w->pts[0], w->pts[1], w->pts[2] ) )
            continue;

        if ( i + 3 < ds->terrain.indexCount &&
             ds->terrain.indexes[i]     == ds->terrain.indexes[i + 4] &&
             ds->terrain.indexes[i + 1] == ds->terrain.indexes[i + 3] )
        {
            vertIndex = ds->terrain.indexes[i + 5];
            if ( PointInsideTriangle( ds->verts[vertIndex].xyz, w, plane ) )
            {
                Vec3Copy( w->pts[2], w->pts[3] );
                Vec3Copy( w->pts[1], w->pts[2] );
                Vec3Copy( ds->verts[vertIndex].xyz, w->pts[1] );
                w->ptCount = 4;
                i += 3;
            }
        }

        LightRegion_AddFace( builder, light, w, twoSided, ds );
    }

    FreeWinding( w );
}


/* AddBrushFaceToLightRegion  0x00435a40 */
static void AddBrushFaceToLightRegion( const PrimaryLightInfo_t *light, const DrawSurf_t *ds,
                                       qboolean twoSided, void **builder )
{
    Assert( ds->side );
    Assert( ds->side->winding );

    LightRegion_AddFace( builder, light, ds->side->winding, twoSided, ds );
}


/* GatherLitSurfaces  0x00435ac0 */
static unsigned int GatherLitSurfaces( const PrimaryLightInfo_t *light,
                                       int firstSurfCount, int shadowSurfCount,
                                       const int *shadowSurfIndex,
                                       const vec3_t *surfMids, const vec3_t *surfHalfSizes,
                                       DrawSurf_t **out )
{
    int i;
    int count;

    count = 0;

    for ( i = 0; i < firstSurfCount; i++ )
        count = GatherLitSurface( light, i, surfMids[i], surfHalfSizes[i], count, out );

    for ( i = 0; i < shadowSurfCount; i++ )
        count = GatherLitSurface( light, shadowSurfIndex[i],
                                  surfMids[firstSurfCount + i],
                                  surfHalfSizes[firstSurfCount + i], count, out );

    return count;
}


/* GatherLitSurface  0x00435b80 */
static int GatherLitSurface( const PrimaryLightInfo_t *light, int surfIndex,
                             const vec3_t surfMid, const vec3_t surfHalfSize,
                             int count, DrawSurf_t **out )
{
    if ( light->type == GFX_LIGHT_TYPE_OMNI || light->cosHalfFov < 0.0f )
    {
        if ( SphereOverlapsBox( light->origin, light->radius, surfMid, surfHalfSize ) )
            return count;
    }
    else
    {
        if ( BoxInConeRange( light->origin, light->dir, light->cosHalfFov, light->radius,
                             surfMid, surfHalfSize ) )
            return count;
    }

    out[count] = &drawSurfs[surfIndex];
    return count + 1;
}


/* EmitLightRegions  0x00435c30 */
static void EmitLightRegions( void )
{
    int                         lightIndex;
    unsigned int                hullIndex;
    const BspLightRegionHull_t *hull;

    for ( lightIndex = 0; lightIndex < numBSPPrimaryLights; lightIndex++ )
    {
        SanityCheckx( numBSPLightRegionBytes == lightIndex, "%i, %i",
                      numBSPLightRegionBytes, lightIndex );

        if ( bspPrimaryLights[lightIndex].type == GFX_LIGHT_TYPE_NONE ||
             bspPrimaryLights[lightIndex].type == GFX_LIGHT_TYPE_DIR )
        {
            bspLightRegions[numBSPLightRegionBytes] = 0;
            numBSPLightRegionBytes++;
            continue;
        }

        bspLightRegions[numBSPLightRegionBytes] =
            checked_cast< unsigned char >( s_lightRegion[lightIndex].hullCount );
        numBSPLightRegionBytes++;

        Assertx( s_lightRegion[lightIndex].hullCount <= MAX_HULLS_PER_LIGHT, "%i, %i",
                 s_lightRegion[lightIndex].hullCount, MAX_HULLS_PER_LIGHT );

        for ( hullIndex = 0; hullIndex < s_lightRegion[lightIndex].hullCount; hullIndex++ )
        {
            hull = s_lightRegion[lightIndex].hulls[hullIndex];

            memcpy( bspLightRegionHulls[numBSPLightRegionHulls].kdopMidPoint,
                    hull->kdopMidPoint, sizeof( hull->kdopMidPoint ) );
            memcpy( bspLightRegionHulls[numBSPLightRegionHulls].kdopHalfSize,
                    hull->kdopHalfSize, sizeof( hull->kdopHalfSize ) );
            bspLightRegionHulls[numBSPLightRegionHulls].axisCount = hull->axisCount;
            numBSPLightRegionHulls++;

            memcpy( &bspLightRegionAxes[numBSPLightRegionAxes], hull + 1,
                    hull->axisCount * sizeof( BspLightRegionAxis_t ) );
            numBSPLightRegionAxes += hull->axisCount;
        }
    }
}


/* DrawSurfBoundsMidHalf  0x00435e20 */
static void DrawSurfBoundsMidHalf( int surfIndex, vec3_t midOut, vec3_t halfSizeOut )
{
    vec3_t mins;
    vec3_t maxs;
    int    vertIndex;

    ClearBounds( mins, maxs );
    for ( vertIndex = 0; vertIndex < drawSurfs[surfIndex].vertCount; vertIndex++ )
        AddPointToBounds( drawSurfs[surfIndex].verts[vertIndex].xyz, mins, maxs );

    Vec3Mid( mins, maxs, midOut );
    Vec3Sub( midOut, mins, halfSizeOut );
}
