/* Original: .\map.cpp */

#include "q_shared.h"
#include "assertive.h"
#include "com_math.h"
#include "com_vector.h"
#include "com_shared.h"
#include "com_memory.h"
#include "q_parse.h"
#include "surfaceflags.h"
#include "cmdlib.h"
#include "polylib.h"
#include "bspfile.h"
#include "errors.h"
#include "map.h"
#include "brush.h"
#include "bsp.h"


extern void AnglesToAxis( const vec3_t angles, vec3_t axis[3] );            /* 0x00412690 */

extern int  numMapDrawSurfs;                                                /* 0x14899cbc */

extern const char *StringFromOffset( const void *base );                    /* 0x004053a0 */

#include "materials.h"
#include "texturevecs.h"

extern void ParsePatch( const char **parsePos, int isMesh, const orientation_t *orient,
                        float scale, int mapInfoIndex, int prefabFlags );    /* 0x00428840 */

extern void ParseLayerName( const char **parsePos, char *out );              /* 0x0041ecc0 */
extern void SkipLayerList( const char **parsePos );                          /* 0x0041ece0 */
extern int  ParseContentsFlags( const char **parsePos );                     /* 0x0041ed30 */
extern int  ParseToolFlags( const char **parsePos );                         /* 0x0041ee30 */
extern void ParseSmoothingHardDefault( const char **parsePos, char *out );   /* 0x0041ec80 */
extern void ParseSmoothingSmoothDefault( const char **parsePos, char *out ); /* 0x0041eca0 */
extern void Map_BeginParseSession( const char *name );                       /* 0x0041ee50 */
extern void Map_EndParseSession( void );                                     /* 0x0041ee80 */
extern void RenumberPrefabKeyList( epair_t *epairs, int prefabIndex,
                                   const char *key );                        /* 0x0041ee90 */
#include "map_reflection_probe.h"

extern void CM_ParseCollNodeLimit( const Entity_t *ent );                    /* 0x004121b0 */


static void LoadPrefab( const char *prefabName, const orientation_t *orient, float scale,
                        int prefabIndex, int prefabFlags );
static void ParseMapFile( const char *mapPath, const char *mapName, const char *fileData,
                          const orientation_t *orient, float scale,
                          int prefabIndex, int prefabFlags );
static int  ParseMapEntity( const char **parsePos, const orientation_t *orient, float scale,
                            int mapInfoIndex, int prefabIndex, int prefabFlags );


plane_t   mapplanesStorage[MAX_MAP_PLANES_COMPILE];   /* 0x11ba8438 */
plane_t  *mapplanes = mapplanesStorage;
int       nummapplanes;                               /* 0x122a9468 */
plane_t  *planehash[PLANE_HASH_SIZE];                 /* 0x122a8440 */

Brush_t  *buildBrush;                                 /* 0x123a9478 */
Entity_t *mapent;                                     /* 0x122a9464 */
int       map_entity_num;                             /* 0x122a9440 */
int       entitySourceBrushes;                        /* 0x122a9470 */

int       c_detail;                                   /* 0x122a9444 */
int       c_structural;                               /* 0x122a9474 */
int       c_patches;                                  /* 0x122a9454 */
int       c_boxbevels;                                /* 0x122a946c */
int       c_edgebevels;                               /* 0x122a945c */
int       c_areaportals;                              /* 0x122a9460 */

vec3_t    map_mins;                                   /* 0x122a9448 */
vec3_t    map_maxs;                                   /* 0x123a947c */

FILE     *g_gridAutoFile;                             /* 0x122a9458 */
FILE     *g_gridNotFile;                              /* 0x122a8438 */

typedef struct
{
    const char *name;      /* +0x00 */
    vec3_t      origin;    /* +0x04 */
    vec3_t      angles;    /* +0x10 */
    float       scale;     /* +0x1c */
} mapInfo_t;

static mapInfo_t s_mapInfo[MAX_MAP_INFOS];            /* 0x122a9478 */
static int       s_mapInfoCount;                      /* 0x122a843c */

static const char *s_smoothingNames[SMOOTHING_COUNT] =   /* 0x00529080 */
{
    "smoothing_hard",
    "smoothing_smooth",
    "smoothing_smooth_other"
};


/* GetMapFileName  0x004191d0 */
const char *GetMapFileName( int mapIndex )
{
    if ( mapIndex < s_mapInfoCount )
        return s_mapInfo[mapIndex].name;
    return "(unknown map; ran out of room for map names)";
}


/* GetFileModifiedTime  0x004192b0 */
bool GetFileModifiedTime( const char *path, FILETIME *out )
{
    HANDLE h;
    BOOL   ok;

    h = CreateFileA( path, FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL,
                     OPEN_EXISTING, 0, NULL );
    if ( h == INVALID_HANDLE_VALUE )
    {
        h = CreateFileA( ExpandPath( va( "..\\map_source\\%s", path ) ),
                         FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL,
                         OPEN_EXISTING, 0, NULL );
        if ( h == INVALID_HANDLE_VALUE )
            return false;
    }

    ok = GetFileTime( h, NULL, NULL, out );
    CloseHandle( h );
    return ok != 0;
}


/* IsFirstUseOfMapName  0x00419350 */
int IsFirstUseOfMapName( int index )
{
    int i;

    for ( i = 0; i < index; i++ )
    {
        if ( !strcmp( s_mapInfo[i].name, s_mapInfo[index].name ) )
            return 0;
    }
    return 1;
}


/* CheckForNewerSourceMaps  0x00419200 */
void CheckForNewerSourceMaps( const char *bspPath )
{
    FILETIME bspTime;
    FILETIME mapTime;
    int      i;

    if ( !GetFileModifiedTime( bspPath, &bspTime ) )
        return;

    for ( i = 0; i < s_mapInfoCount; i++ )
    {
        if ( IsFirstUseOfMapName( i )
          && GetFileModifiedTime( s_mapInfo[i].name, &mapTime )
          && CompareFileTime( &bspTime, &mapTime ) < 0 )
        {
            printf( "Newer source map: %s\n", s_mapInfo[i].name );
        }
    }
}


/* GetMapOrientation  0x004193a0 */
void GetMapOrientation( int mapIndex, orientation_t *orient, float *scale )
{
    if ( mapIndex < s_mapInfoCount )
    {
        Vec3Copy( s_mapInfo[mapIndex].origin, orient->origin );
        AnglesToAxis( s_mapInfo[mapIndex].angles, orient->axis );
        *scale = s_mapInfo[mapIndex].scale;
    }
    else
    {
        Vec3Clear( orient->origin );
        AnglesToAxis( vec3_origin, orient->axis );
        *scale = 1.0f;
    }
}


/* AddMapInfo  0x0041ae80 */
int AddMapInfo( const char *sourceMapName, const vec3_t origin,
                const vec3_t angles, float scale )
{
    int index;

    Assert( sourceMapName );
    Assert( origin );
    Assert( angles );

    index = s_mapInfoCount;
    if ( s_mapInfoCount != MAX_MAP_INFOS )
    {
        s_mapInfo[s_mapInfoCount].name = sourceMapName;
        Vec3Copy( origin, s_mapInfo[s_mapInfoCount].origin );
        Vec3Copy( angles, s_mapInfo[s_mapInfoCount].angles );
        s_mapInfo[s_mapInfoCount].scale = scale;
        index = s_mapInfoCount;
        s_mapInfoCount++;
    }
    return index;
}


/* PlaneEqual  0x00419420 */
int PlaneEqual( const plane_t *p, const vec3_t normal, float dist )
{
    if ( fabs( p->normal[0] - normal[0] ) < NORMAL_EPSILON
      && fabs( p->normal[1] - normal[1] ) < NORMAL_EPSILON
      && fabs( p->normal[2] - normal[2] ) < NORMAL_EPSILON
      && fabs( p->dist - dist ) < EQUAL_EPSILON )
    {
        return 1;
    }
    return 0;
}


/* HashPlane  0x004194e0 */
void HashPlane( plane_t *p )
{
    int hash;

    hash = (int)fabs( p->dist ) / PLANE_HASH_DIST_DIV;
    hash &= PLANE_HASH_SIZE - 1;
    p->hash_chain = planehash[hash];
    planehash[hash] = p;
}


/* CreateNewFloatPlane  0x00419540 */
int CreateNewFloatPlane( const vec3_t normal, float dist )
{
    plane_t *p;
    plane_t  temp;
    int      type;

    Assert( Vec3IsNormalized( normal ) );

    if ( nummapplanes + 2 > MAX_MAP_PLANES_COMPILE )
        Com_Error( "MAX_MAP_PLANES_COMPILE (%i)", MAX_MAP_PLANES_COMPILE );

    p = &mapplanes[nummapplanes];
    Vec3Copy( normal, p->normal );
    p->dist = dist;

    if ( p->normal[0] == 1.0f || p->normal[0] == -1.0f )
        type = 0;
    else if ( p->normal[1] == 1.0f || p->normal[1] == -1.0f )
        type = 1;
    else if ( p->normal[2] == 1.0f || p->normal[2] == -1.0f )
        type = 2;
    else
        type = 3;

    p[1].type = type;
    p[0].type = type;

    Vec3Sub( vec3_origin, normal, p[1].normal );
    p[1].dist = -dist;

    nummapplanes += 2;

    if ( type < 3 && ( p->normal[0] < 0 || p->normal[1] < 0 || p->normal[2] < 0 ) )
    {
        temp = p[0];
        p[0] = p[1];
        p[1] = temp;
        HashPlane( &p[0] );
        HashPlane( &p[1] );
        return nummapplanes - 1;
    }

    HashPlane( &p[0] );
    HashPlane( &p[1] );
    return nummapplanes - 2;
}


/* SnapPlaneNormal  0x004197d0 */
int SnapPlaneNormal( vec3_t normal )
{
    int i;

    for ( i = 0; i < 3; i++ )
    {
        if ( fabs( normal[i] - 1.0f ) < NORMAL_EPSILON )
        {
            Vec3Clear( normal );
            normal[i] = 1.0f;
            return 1;
        }
        if ( fabs( normal[i] - -1.0f ) < NORMAL_EPSILON )
        {
            Vec3Clear( normal );
            normal[i] = -1.0f;
            return 1;
        }
    }
    return 0;
}


/* SnapPlane  0x00419770 */
void SnapPlane( vec3_t normal, float *dist )
{
    SnapPlaneNormal( normal );

    if ( fabs( *dist - Q_rint( *dist ) ) < EQUAL_EPSILON )
        *dist = Q_rint( *dist );
}


/* FindFloatPlane  0x004198a0 */
int FindFloatPlane( const vec3_t normal, float dist )
{
    int      hashBase;
    int      hashOffset;
    int      bucket;
    plane_t *p;

    SnapPlane( (float *)normal, &dist );

    hashBase = (int)( fabs( dist ) / 8.0 );
    hashBase &= PLANE_HASH_SIZE - 1;

    for ( hashOffset = -1; hashOffset <= 1; hashOffset++ )
    {
        bucket = ( hashBase + hashOffset ) & ( PLANE_HASH_SIZE - 1 );
        for ( p = planehash[bucket]; p; p = p->hash_chain )
        {
            if ( PlaneEqual( p, normal, dist ) )
                return ( p - mapplanes );
        }
    }

    return CreateNewFloatPlane( normal, dist );
}


/* MapPlaneFromPoints  0x00419970 */
int MapPlaneFromPoints( const vec3_t p0, const vec3_t p1, const vec3_t p2 )
{
    vec3_t d1, d2, normal;
    float  dist;

    Vec3Sub( p0, p1, d1 );
    Vec3Sub( p2, p1, d2 );
    Vec3Cross( d1, d2, normal );
    Vec3Normalize( normal );
    dist = Vec3Dot( p0, normal );
    return FindFloatPlane( normal, dist );
}


/* SetBrushContents  0x004199f0 */
void SetBrushContents( Brush_t *b )
{
    side_t      *side;
    unsigned int contentFlags;
    unsigned int allContentFlags;
    unsigned int allSurfaceFlags;
    qboolean     mixed;
    unsigned int i;

    contentFlags    = b->sides[0].contents;
    b->material     = b->sides[0].material;
    mixed           = qfalse;
    allSurfaceFlags = b->sides[0].surfaceFlags;
    allContentFlags = b->sides[0].contents;

    for ( i = 1; i < b->sideCount; i++ )
    {
        side = &b->sides[i];
        if ( !side->material )
            continue;
        if ( (unsigned int)side->contents != contentFlags )
            mixed = qtrue;
        allSurfaceFlags |= side->surfaceFlags;
        allContentFlags |= side->contents;
    }

    if ( mixed )
        Com_DPrintf( "Map %s, Entity %i, Brush %i: mixed face contents\n",
                     GetMapFileName( b->mapInfoIndex ), b->entitynum, b->brushnum );

    if ( ( contentFlags & CONTENTS_DETAIL ) && ( contentFlags & CONTENTS_STRUCTURAL ) )
    {
        Com_Printf( "Entity %i, Brush %i: mixed CONTENTS_DETAIL and CONTENTS_STRUCTURAL\n",
                    num_entities - 1, entitySourceBrushes );
        contentFlags &= ~CONTENTS_DETAIL;
    }

    if ( fulldetail )
        contentFlags &= ~CONTENTS_DETAIL;

    if ( ( contentFlags & CONTENTS_TRANSLUCENT ) && !( contentFlags & CONTENTS_STRUCTURAL ) )
        contentFlags |= CONTENTS_DETAIL;

    if ( allContentFlags & CONTENTS_MANTLE )
        contentFlags |= CONTENTS_MANTLE;

    if ( contentFlags & CONTENTS_DETAIL )
    {
        c_detail++;
        b->detail = 1;
    }
    else
    {
        c_structural++;
        b->detail = 0;
    }

    b->forceVisible = ( allSurfaceFlags & SURF_PORTAL ) != 0;
    b->opaque       = 1 - ( ( contentFlags & CONTENTS_TRANSLUCENT ) != 0 );

    b->noCollision = 0;
    if ( contentFlags & CONTENTS_NONCOLLIDING )
    {
        b->noCollision = 1;
    }
    else if ( !b->opaque
           && ( allSurfaceFlags & ~SURF_IGNORE_MASK ) == SURF_PORTAL_BRUSH )
    {
        b->noCollision = 1;
    }

    b->contents = contentFlags;
}


/* ValidateBrushSides  0x0041a2e0 */
int ValidateBrushSides( Brush_t *b )
{
    unsigned int i;
    unsigned int j;
    unsigned int k;

    for ( i = 1; i < b->sideCount; i++ )
    {
        if ( b->sides[i].planenum == -1 )
        {
            Com_Printf( "Map %s, Entity %i, Brush %i: degenerate plane\n",
                        GetMapFileName( b->mapInfoIndex ), b->entitynum, b->brushnum );
            for ( k = i + 1; k < b->sideCount; k++ )
                b->sides[k - 1] = b->sides[k];
            b->sideCount--;
            i--;
            continue;
        }

        for ( j = 0; j < i; j++ )
        {
            if ( b->sides[i].planenum == b->sides[j].planenum )
            {
                for ( k = i + 1; k < b->sideCount; k++ )
                    b->sides[k - 1] = b->sides[k];
                b->sideCount--;
                i--;
                break;
            }

            if ( (unsigned int)b->sides[i].planenum == ( (unsigned int)b->sides[j].planenum ^ 1 ) )
            {
                Com_Printf( "Map %s, Entity %i, Brush %i: mirrored plane\n",
                            GetMapFileName( b->mapInfoIndex ), b->entitynum, b->brushnum );
                return 0;
            }
        }
    }
    return 1;
}


/* BestSideMaterialForNormal  0x00419e80 */
Material_t *BestSideMaterialForNormal( const vec3_t normal )
{
    Material_t  *best;
    float        bestDot;
    float        dot;
    unsigned int i;

    best    = NULL;
    bestDot = 0.0f;

    for ( i = 0; i < buildBrush->sideCount; i++ )
    {
        if ( !buildBrush->sides[i].material )
            continue;

        dot = Vec3Dot( mapplanes[buildBrush->sides[i].planenum].normal, normal );
        if ( dot > bestDot )
        {
            best    = buildBrush->sides[i].material;
            bestDot = dot;
        }
    }
    return best;
}


/* AddEdgeBevels  0x00419f30 */
void AddEdgeBevels( void )
{
    unsigned int i;
    unsigned int j;
    unsigned int prev;
    unsigned int existing;
    int          dir;
    winding_t   *w;
    vec3_t       edgeDir;
    vec3_t       bevelNormal;
    float        edgeLen;
    float        dist;
    int          planenum;
    side_t      *ns;

    for ( i = 6; i < buildBrush->sideCount; i++ )
    {
        w = buildBrush->sides[i].winding;
        if ( !w )
            continue;

        prev = w->ptCount - 1;
        for ( j = 0; j < w->ptCount; j++ )
        {
            Vec3Sub( w->pts[j], w->pts[prev], edgeDir );
            edgeLen = Vec3Normalize( edgeDir );

            if ( edgeLen >= BEVEL_MIN_EDGE_LEN
              && !SnapPlaneNormal( edgeDir )
              && edgeDir[2] <= BEVEL_MAX_Z
              && edgeDir[2] >= -BEVEL_MAX_Z )
            {
                Vec3SetPerpXY( edgeDir, bevelNormal );
                Vec3Normalize( bevelNormal );

                for ( dir = 0; dir < 2; dir++ )
                {
                    Vec3Negate( bevelNormal, bevelNormal );
                    dist = Vec3Dot( w->pts[prev], bevelNormal );
                    SnapPlane( bevelNormal, &dist );

                    if ( TestBrushAgainstPlane( buildBrush, bevelNormal, dist, 0.5f ) != 1 )
                        continue;

                    if ( buildBrush->sideCount == MAX_BUILD_SIDES )
                        Com_Error( "MAX_BUILD_SIDES" );

                    planenum = FindFloatPlane( bevelNormal, dist );
                    for ( existing = 0;
                          existing < buildBrush->sideCount
                            && buildBrush->sides[existing].planenum != planenum;
                          existing++ )
                        ;
                    if ( existing >= buildBrush->sideCount )
                    {
                        ns = &buildBrush->sides[buildBrush->sideCount];
                        buildBrush->sideCount++;
                        memset( ns, 0, sizeof( side_t ) );
                        ns->planenum = planenum;
                        ns->contents = buildBrush->sides[0].contents;
                        ns->bevel    = 1;
                        ns->material = BestSideMaterialForNormal( bevelNormal );
                        c_edgebevels++;
                    }
                }
            }

            prev = j;
        }
    }
}


/* AddBrushBevels  0x00419c10 */
void AddBrushBevels( void )
{
    unsigned int order;
    unsigned int i;
    int          axis;
    int          dir;
    side_t      *s;
    side_t       sidetemp;
    vec4_t       plane;

    order = 0;
    for ( axis = 0; axis < 3; axis++ )
    {
        for ( dir = -1; dir < 2; dir += 2 )
        {
            i = 0;
            s = &buildBrush->sides[0];
            while ( i < buildBrush->sideCount
                 && mapplanes[s->planenum].normal[axis] != (float)dir )
            {
                i++;
                s++;
            }

            if ( i == buildBrush->sideCount )
            {
                if ( buildBrush->sideCount == MAX_BUILD_SIDES )
                    Com_Error( "MAX_BUILD_SIDES" );

                memset( s, 0, sizeof( side_t ) );
                Vec3Clear( plane );
                plane[axis] = (float)dir;
                if ( dir == 1 )
                    plane[3] = buildBrush->maxs[axis];
                else
                    plane[3] = -buildBrush->mins[axis];

                buildBrush->sideCount++;
                s->planenum = FindFloatPlane( plane, plane[3] );
                s->contents = buildBrush->sides[0].contents;
                s->bevel    = 1;
                s->material = BestSideMaterialForNormal( plane );
                c_boxbevels++;
            }

            if ( i != order )
            {
                sidetemp                 = buildBrush->sides[order];
                buildBrush->sides[order] = buildBrush->sides[i];
                buildBrush->sides[i]     = sidetemp;
            }
            order++;
        }
    }

    AddEdgeBevels();
}


/* RemoveSidesWithoutWindings  0x0041b6c0 */
void RemoveSidesWithoutWindings( void )
{
    unsigned int keep;
    unsigned int i;

    keep = 0;
    for ( i = 0; i < buildBrush->sideCount; i++ )
    {
        if ( buildBrush->sides[i].winding )
        {
            if ( keep != i )
                buildBrush->sides[keep] = buildBrush->sides[i];
            keep++;
        }
    }
    buildBrush->sideCount = keep;
}


/* SmoothingIndexFromName  0x0041a230 */
int SmoothingIndexFromName( const char *name )
{
    int i;

    for ( i = 0; i < SMOOTHING_COUNT; i++ )
    {
        if ( !_stricmp( name, s_smoothingNames[i] ) )
            return i;
    }

    Com_Error( "ParseSmoothing(): Unknown smoothing name '%s'", name );
    return 0;
}

/* ParseSmoothing  0x0041a1e0 */
int ParseSmoothing( const char **parsePos )
{
    char name[MAX_OS_PATH];

    ParseSmoothingHardDefault( parsePos, name );
    return SmoothingIndexFromName( name );
}

/* ParseSmoothingForPatch  0x0041a290 */
int ParseSmoothingForPatch( const char **parsePos )
{
    char name[MAX_OS_PATH];

    ParseSmoothingSmoothDefault( parsePos, name );
    return SmoothingIndexFromName( name );
}


/* BuildLightGridFileName  0x0041a520 */
void BuildLightGridFileName( char *out, const char *extension )
{
    strcpy( out, g_outputBasePath );
    COM_StripExtension( out, out );
    strcat( out, extension );
}

/* OpenLightGridFiles  0x0041a4a0 */
void OpenLightGridFiles( void )
{
    char path[MAX_OS_PATH_SHORT];

    BuildLightGridFileName( path, ".grid_auto" );
    g_gridAutoFile = fopen( path, "wb" );

    BuildLightGridFileName( path, ".grid_not" );
    g_gridNotFile = fopen( path, "wb" );
}

/* CloseLightGridFile  0x0041a590 */
void CloseLightGridFile( FILE *f, const char *extension )
{
    char path[MAX_OS_PATH_SHORT];
    long size;

    if ( !f )
        return;

    size = ftell( f );
    fclose( f );
    if ( size == 0 )
    {
        BuildLightGridFileName( path, extension );
        remove( path );
    }
}

/* CloseLightGridFiles  0x0041a560 */
void CloseLightGridFiles( void )
{
    CloseLightGridFile( g_gridAutoFile, ".grid_auto" );
    CloseLightGridFile( g_gridNotFile,  ".grid_not"  );
}

/* WriteLightGridPoint  0x0041c190 */
void WriteLightGridPoint( FILE *f, const unsigned short point[3] )
{
    if ( f )
        fwrite( point, sizeof( unsigned short ), 3, f );
}

/* WriteLightGridVolume  0x0041bd70 */
bool WriteLightGridVolume( Brush_t *b )
{
    unsigned int  i;
    side_t       *s;
    const char   *materialName;
    bool          isVolume;
    bool          isSky;
    FILE         *f;
    unsigned short mins[3];
    unsigned short maxs[3];
    unsigned short point[3];
    vec3_t        pos;

    isVolume = false;
    isSky    = false;

    for ( i = 0; i < b->sideCount; i++ )
    {
        s = &b->sides[i];
        if ( s->bevel )
            continue;

        materialName = StringFromOffset( s->material );
        if ( !strcmp( materialName, "lightgrid_volume" ) )
            isVolume = true;
        else if ( !strcmp( materialName, "lightgrid_sky" ) )
            isSky = true;
        else
            return false;
    }

    if ( !CreateBrushWindings( b ) )
    {
        FreeBrushWindings( b );
        return true;
    }

    if ( isVolume && isSky )
    {
        printf( "Map %s, Entity %i, Brush %i: mixed lightgrid materials",
                GetMapFileName( b->mapInfoIndex ), b->entitynum, b->brushnum );
        FreeBrushWindings( b );
        return true;
    }

    f = isVolume ? g_gridAutoFile : g_gridNotFile;

    mins[0] = (unsigned short)(int)ceil ( (float)( ( b->mins[0] - MIN_WORLD_COORD ) * 0.03125  ) );
    mins[1] = (unsigned short)(int)ceil ( (float)( ( b->mins[1] - MIN_WORLD_COORD ) * 0.03125  ) );
    mins[2] = (unsigned short)(int)ceil ( (float)( ( b->mins[2] - MIN_WORLD_COORD ) * 0.015625 ) );
    maxs[0] = (unsigned short)(int)floor( (float)( ( b->maxs[0] - MIN_WORLD_COORD ) * 0.03125  ) );
    maxs[1] = (unsigned short)(int)floor( (float)( ( b->maxs[1] - MIN_WORLD_COORD ) * 0.03125  ) );
    maxs[2] = (unsigned short)(int)floor( (float)( ( b->maxs[2] - MIN_WORLD_COORD ) * 0.015625 ) );

    for ( point[2] = mins[2]; point[2] <= maxs[2]; point[2]++ )
    {
        pos[2] = (float)( (int)( point[2] * 0x40 - 0x20000 ) );
        for ( point[1] = mins[1]; point[1] <= maxs[1]; point[1]++ )
        {
            pos[1] = (float)( (int)( point[1] * 0x20 - 0x20000 ) );
            for ( point[0] = mins[0]; point[0] <= maxs[0]; point[0]++ )
            {
                pos[0] = (float)( (int)( point[0] * 0x20 - 0x20000 ) );
                if ( BrushContainsPoint( b, pos ) )
                    WriteLightGridPoint( f, point );
            }
        }
    }

    FreeBrushWindings( b );
    return true;
}


/* MoveBrushesToWorld  0x0041a610 */
void MoveBrushesToWorld( Entity_t *ent, int cullGroup )
{
    Brush_t     *b;
    Brush_t     *next;
    parseMesh_t *p;

    b = ent->brushes;
    while ( b )
    {
        next = b->next;
        b->cullGroup = cullGroup;
        b->next = entities[0].brushes;
        entities[0].brushes = b;
        b = next;
    }
    ent->brushes = NULL;

    if ( ent->patches )
    {
        for ( p = ent->patches; p->next; p = p->next )
            p->cullGroup = cullGroup;
        p->cullGroup = cullGroup;
        p->next = entities[0].patches;
        entities[0].patches = ent->patches;
        ent->patches = NULL;
    }
}


/* MovePhysicsBrushes  0x0041a6c0 */
void MovePhysicsBrushes( Entity_t *ent )
{
    Brush_t **link;
    Brush_t  *b;
    Brush_t  *next;

    Assert( ent );
    Assert( ent->physicsBrushes == 0 );

    link = &ent->brushes;
    b    = ent->brushes;
    while ( b )
    {
        next = b->next;
        if ( !( b->material->toolFlags & TOOLFLAG_PHYSICSGEOM ) )
        {
            link = &b->next;
            b    = next;
        }
        else
        {
            *link = next;
            b->next = ent->physicsBrushes;
            ent->physicsBrushes = b;
            b = next;
        }
    }
}


/* SetEntityOriginFromGeometry  0x0041a780 */
void SetEntityOriginFromGeometry( Entity_t *ent )
{
    vec3_t       mins;
    vec3_t       maxs;
    vec3_t       origin;
    char         text[32];
    Brush_t     *b;
    parseMesh_t *p;
    int          i;
    int          vertCount;

    Assert( ent );

    ClearBounds( mins, maxs );

    for ( b = ent->brushes; b; b = b->next )
    {
        if ( !b->sideCount )
            continue;
        AddPointToBounds( b->mins, mins, maxs );
        AddPointToBounds( b->maxs, mins, maxs );
    }

    for ( p = ent->patches; p; p = p->next )
    {
        vertCount = p->width * p->height;
        for ( i = 0; i < vertCount; i++ )
            AddPointToBounds( (float *)( (byte *)p->verts + i * 0x2c ), mins, maxs );
    }

    Vec3Mid( mins, maxs, origin );
    origin[0] = floor( ( float )( origin[0] + 0.5f ) );
    origin[1] = floor( ( float )( origin[1] + 0.5f ) );
    origin[2] = floor( ( float )( origin[2] + 0.5f ) );

    sprintf( text, "%.0f %.0f %.0f", origin[0], origin[1], origin[2] );
    SetKeyValue( ent, "origin", text );
    Vec3Copy( origin, ent->origin );
}


/* AdjustBrushesForOrigin  0x0041a950 */
void AdjustBrushesForOrigin( Entity_t *ent )
{
    Brush_t     *b;
    parseMesh_t *p;
    side_t      *s;
    unsigned int i;
    int          j;
    int          vertCount;
    int          planenum;
    float        newdist;

    Assert( ent );
    Assert( ent->brushes || ent->patches );

    for ( b = ent->brushes; b; b = b->next )
    {
        for ( i = 0; i < b->sideCount; i++ )
        {
            s        = &b->sides[i];
            planenum = s->planenum;
            newdist  = mapplanes[planenum].dist
                     - Vec3Dot( mapplanes[planenum].normal, ent->origin );
            s->planenum = FindFloatPlane( mapplanes[planenum].normal, newdist );

            if ( s->winding )
            {
                FreeWinding( s->winding );
                s->winding = NULL;
            }

            s->texMat[0][3]  += Vec3Dot( ent->origin, s->texMat[0]  );
            s->texMat[1][3]  += Vec3Dot( ent->origin, s->texMat[1]  );
            s->lmapMat[0][3] += Vec3Dot( ent->origin, s->lmapMat[0] );
            s->lmapMat[1][3] += Vec3Dot( ent->origin, s->lmapMat[1] );
        }
        CreateBrushWindings( b );
    }

    for ( p = ent->patches; p; p = p->next )
    {
        vertCount = p->width * p->height;
        for ( j = 0; j < vertCount; j++ )
        {
            float *xyz = (float *)( (byte *)p->verts + j * 0x2c );
            Vec3Sub( xyz, ent->origin, xyz );
        }
    }
}


/* ParseBrushSideTexture  0x0041bb20 */
Material_t *ParseBrushSideTexture( const char **parsePos, int mapInfoIndex,
                                   const vec4_t plane, const orientation_t *orient,
                                   float scale, float texScale, vec4_t out[2] )
{
    const char *matName;
    Material_t *material;
    qboolean    isLightmapGray;
    vec2_t      texScaleVec;
    vec2_t      texShift;
    float       rotate;
    float       flags;
    float       wrapped;
    vec4_t      texVecs[2];

    Com_SetSpaceDelimited( 1 );
    matName = COM_ParseExt( parsePos );
    Com_SetSpaceDelimited( 0 );

    material       = Material_Register( matName, MTL_USAGE_WORLD_VCOL )->material;
    isLightmapGray = !strcmp( matName, "lightmap_gray" );

    texScaleVec[0] = COM_ParseFloat( parsePos );
    texScaleVec[1] = COM_ParseFloat( parsePos );
    texShift[0]    = COM_ParseFloat( parsePos );
    texShift[1]    = COM_ParseFloat( parsePos );
    rotate         = COM_ParseFloat( parsePos );
    flags          = COM_ParseFloat( parsePos );

    if ( texScaleVec[0] == 0 || texScaleVec[1] == 0 )
    {
        Com_Error( "Map %s, Entity %i, Brush %i:  Material with 0 texture scale.\n"
                   "Open and re-save '%s' from Radiant to fix this.",
                   GetMapFileName( mapInfoIndex ), map_entity_num - 1,
                   entitySourceBrushes, GetMapFileName( mapInfoIndex ) );
    }

    if ( isLightmapGray )
    {
        texShift[0] = 0.0f;
        texShift[1] = 0.0f;
        wrapped     = (float)( floor( ( rotate + 45.0 ) / 90.0 ) * 90.0 );
        rotate      = rotate - wrapped;
        texScaleVec[0] = I_fabs( texScaleVec[0] );
        texScaleVec[1] = I_fabs( texScaleVec[1] );
    }

    BuildTextureVecs( plane, texScaleVec, texShift, rotate, flags, texVecs );

    texVecs[0][3] = -texVecs[0][3];
    TransformPlaneScaled( orient, scale, texVecs[0], out[0] );
    out[0][3] = -out[0][3];
    Vec4Scale( out[0], texScale / scale, out[0] );

    texVecs[1][3] = -texVecs[1][3];
    TransformPlaneScaled( orient, scale, texVecs[1], out[1] );
    out[1][3] = -out[1][3];
    Vec4Scale( out[1], texScale / scale, out[1] );

    return material;
}


/* ParseRawBrush  0x0041b760 */
void ParseRawBrush( const char **parsePos, const orientation_t *orient,
                    float brushScale, int mapInfoIndex )
{
    const char  *token;
    side_t      *s;
    vec3_t       planepts[3];
    vec4_t       plane;
    vec4_t       xformPlane;
    unsigned int contentFlags;
    unsigned int toolFlags;
    char         layerName[MAX_OS_PATH];

    Assert( brushScale * brushScale > EQUAL_EPSILON * EQUAL_EPSILON );

    buildBrush->sideCount = 0;
    buildBrush->detail    = 0;

    ParseLayerName( parsePos, layerName );
    contentFlags = ParseContentsFlags( parsePos );
    toolFlags    = ParseToolFlags( parsePos );

    while ( ( token = COM_Parse( parsePos ) ), token[0] && strcmp( token, "}" ) )
    {
        Com_UngetToken();

        if ( buildBrush->sideCount == MAX_BUILD_SIDES )
            Com_Error( "MAX_BUILD_SIDES (%i) -- brush too complex", buildBrush->sideCount );

        COM_Parse1DMatrix( parsePos, 3, planepts[0] );
        COM_Parse1DMatrix( parsePos, 3, planepts[1] );
        COM_Parse1DMatrix( parsePos, 3, planepts[2] );

        if ( !PlaneFromPoints( plane, planepts[0], planepts[1], planepts[2] ) )
        {
            SkipRestOfLine( parsePos );
            continue;
        }

        s = &buildBrush->sides[buildBrush->sideCount];
        memset( s, 0, sizeof( side_t ) );
        buildBrush->sideCount++;

        TransformPlaneScaled( orient, brushScale, plane, xformPlane );
        s->planenum = FindFloatPlane( xformPlane, xformPlane[3] );

        s->material     = ParseBrushSideTexture( parsePos, mapInfoIndex, plane,
                                                 orient, brushScale, 1.0f, s->texMat );
        s->lmapMaterial = ParseBrushSideTexture( parsePos, mapInfoIndex, plane,
                                                 orient, brushScale, sampleScale, s->lmapMat );
        s->smoothing    = ParseSmoothing( parsePos );

        s->contents = s->material->contentFlags;
        if ( contentFlags & CONTENTS_DETAIL )
            s->contents |= CONTENTS_DETAIL;
        if ( contentFlags & CONTENTS_NONCOLLIDING )
            s->contents |= CONTENTS_NONCOLLIDING;
        if ( contentFlags & ( CONTENTS_CLIPSHOT | CONTENTS_MISSILECLIP ) )
            s->contents &= ~( CONTENTS_DETAIL | CONTENTS_SOLID );
        if ( contentFlags & CONTENTS_CLIPSHOT )
            s->contents |= CONTENTS_CLIPSHOT;
        if ( contentFlags & CONTENTS_MISSILECLIP )
            s->contents |= CONTENTS_MISSILECLIP;

        s->surfaceFlags = s->material->surfaceFlags;
        s->toolFlags    = s->material->toolFlags;
        s->toolFlags   |= toolFlags;
    }
}


/* FinishBrush  0x0041b360 */
Brush_t *FinishBrush( int mapInfoIndex )
{
    Brush_t     *b;
    vec3_t       origin;
    char         text[32];
    unsigned int i;

    if ( !CreateBrushWindings( buildBrush ) )
    {
        FreeBrushWindings( buildBrush );
        return NULL;
    }

    if ( buildBrush->contents & CONTENTS_CLIP_MASK )
    {
        buildBrush->detail = 1;
        c_detail++;
    }

    if ( buildBrush->material->toolFlags & TOOLFLAG_ORIGIN )
    {
        if ( !buildBrush->entitynum )
        {
            Com_Error( "Map %s, Entity %i, Brush %i: origin brushes not allowed in world\n",
                       GetMapFileName( mapInfoIndex ),
                       buildBrush->entitynum, buildBrush->brushnum );
            return NULL;
        }

        Vec3Mid( buildBrush->mins, buildBrush->maxs, origin );
        origin[0] = floor( ( float )( origin[0] + 0.5f ) );
        origin[1] = floor( ( float )( origin[1] + 0.5f ) );
        origin[2] = floor( ( float )( origin[2] + 0.5f ) );

        sprintf( text, "%.0f %.0f %.0f", origin[0], origin[1], origin[2] );
        SetKeyValue( &entities[num_entities - 1], "origin", text );
        Vec3Copy( origin, entities[num_entities - 1].origin );
        FreeBrushWindings( buildBrush );
        return NULL;
    }

    RemoveSidesWithoutWindings();
    AddBrushBevels();

    b = CopyBrush( buildBrush );

    for ( i = 0; i < buildBrush->sideCount; i++ )
    {
        if ( buildBrush->sides[i].winding )
        {
            FreeWinding( buildBrush->sides[i].winding );
            buildBrush->sides[i].winding = NULL;
        }
        if ( buildBrush->sides[i].edges )
        {
            FreeAdjacencyWinding( ( adjacencyWinding_t * )buildBrush->sides[i].edges );
            buildBrush->sides[i].edges = NULL;
        }
    }

    AssertCmp( b->entitynum,    ==, map_entity_num - 1 );
    AssertCmp( b->brushnum,     ==, entitySourceBrushes );
    AssertCmp( b->mapInfoIndex, ==, mapInfoIndex );

    b->original = b;
    b->next = mapent->brushes;
    mapent->brushes = b;
    return b;
}


/* ParseBrush  0x0041b1f0 */
void ParseBrush( const char **parsePos, const orientation_t *orient, float scale,
                 int mapInfoIndex, unsigned int prefabFlags )
{
    unsigned int i;
    int          clipSides;

    ParseRawBrush( parsePos, orient, scale, mapInfoIndex );

    buildBrush->outputNumber = -1;
    buildBrush->pad28        = -1;
    buildBrush->entitynum    = map_entity_num - 1;
    buildBrush->brushnum     = entitySourceBrushes;
    buildBrush->mapInfoIndex = mapInfoIndex;

    if ( !ValidateBrushSides( buildBrush ) )
        return;
    if ( WriteLightGridVolume( buildBrush ) )
        return;

    SetBrushContents( buildBrush );

    if ( nodetail && ( buildBrush->contents & CONTENTS_DETAIL ) )
        return;
    if ( nowater && ( buildBrush->contents & CONTENTS_WATER ) )
        return;

    if ( prefabFlags & 1 )
    {
        clipSides = 0;
        for ( i = 0; i < buildBrush->sideCount; i++ )
        {
            if ( !strncmp( StringFromOffset( buildBrush->sides[i].material ), "clip", 4 ) )
                clipSides++;
        }
        if ( clipSides )
            return;
    }

    FinishBrush( mapInfoIndex );
}


/* ParseCollMapEntity  0x0041af80 */
int ParseCollMapEntity( int mapInfoIndex, const char **parsePos, const vec3_t origin,
                        const vec3_t angles, float modelScale )
{
    const char   *token;
    epair_t      *e;
    orientation_t placement;

    token = COM_Parse( parsePos );
    if ( !token[0] )
        return 0;

    if ( strcmp( token, "{" ) )
        Com_Error( "ParseCollMapEntity: { not found, found %s on line %d",
                   token, Com_GetCurrentParseLine() );

    if ( num_entities == MAX_MAP_ENTITIES )
        Com_Error( "num_entities == MAX_MAP_ENTITIES" );

    entitySourceBrushes = 0;
    mapent = &entities[num_entities];
    memset( mapent, 0, sizeof( Entity_t ) );

    AnglesToAxis( angles, placement.axis );
    Vec3Copy( origin, placement.origin );

    while ( 1 )
    {
        token = COM_Parse( parsePos );
        if ( !token[0] )
            Com_Error( "ParseEntity: EOF without closing brace" );
        if ( !strcmp( token, "}" ) )
            break;

        if ( strcmp( token, "{" ) )
        {
            e = ParseEPair( (char *)token, parsePos );
            e->next = mapent->epairs;
            mapent->epairs = e;
            continue;
        }

        token = COM_Parse( parsePos );
        if ( !token[0] )
            break;

        if ( !strcmp( token, "curve" ) )
        {
            c_patches++;
            Com_Error( "cannot have curves in collision maps" );
        }
        else if ( !strcmp( token, "mesh" ) )
        {
            c_patches++;
            Com_Error( "cannot have patch terrain in collision maps" );
        }
        else if ( !strcmp( token, "physics_cylinder" ) )
        {
            Com_Error( "cannot have physics cylinders in collision maps" );
        }
        else if ( !strcmp( token, "physics_box" ) )
        {
            Com_Error( "cannot have physics boxes in collision maps" );
        }
        else
        {
            Com_UngetToken();
            ParseBrush( parsePos, &placement, modelScale, mapInfoIndex, 0 );
        }

        entitySourceBrushes++;
    }

    MoveBrushesToWorld( mapent, -1 );
    return 1;
}


/* LoadCollisionMap  0x0041ad60 */
void LoadCollisionMap( const char *collmapPath, const char *modelName,
                       const vec3_t origin, const vec3_t angles, float modelScale )
{
    int         mapInfoIndex;
    void       *buf;
    const char *parsePos;
    const char *token;

    mapInfoIndex = AddMapInfo( collmapPath, origin, angles, modelScale );

    if ( LoadFile( collmapPath, &buf ) < 0 )
    {
        if ( displayCollMapWarnings )
            Com_Printf( "WARNING: No collision map file for misc_model \"%s\"\n", modelName );
        return;
    }

    Map_BeginParseSession( collmapPath );
    parsePos = (const char *)buf;

    token = COM_Parse( &parsePos );
    if ( strcmp( token, "iwmap" ) )
        Com_Error( "collmap '%s' is missing 'iwmap' version specification\n", collmapPath );

    token = COM_Parse( &parsePos );
    if ( atoi( token ) != 4 )
        Com_Error( "collmap '%s' is version '%s'; should be version '%i'\n",
                   collmapPath, token, 4 );

    SkipLayerList( &parsePos );

    while ( ParseCollMapEntity( mapInfoIndex, &parsePos, origin, angles, modelScale ) )
        ;

    Map_EndParseSession();
    free( buf );
}


/* ProcessMiscModelCollMaps  0x0041ac00 */
void ProcessMiscModelCollMaps( void )
{
    int         i;
    Entity_t   *ent;
    const char *classname;
    const char *model;
    char        modelName[MAX_OS_PATH];
    char        collmapPath[MAX_OS_PATH];
    vec3_t      origin;
    vec3_t      angles;
    float       modelScale;

    for ( i = 1; i < num_entities; i++ )
    {
        ent       = &entities[i];
        classname = ValueForKey( ent, "classname" );
        if ( Q_stricmp( "misc_model", classname ) )
            continue;

        model = ValueForKey( ent, "model" );
        if ( !model[0] )
            continue;

        ExtractFileName( model, modelName );
        sprintf( collmapPath, "%s/%s.map", ExpandPath( "../collmaps" ), modelName );

        GetVectorForKey( ent, "origin", origin );
        GetVectorForKey( ent, "angles", angles );
        modelScale = FloatForKey( ent, "modelscale" );
        if ( modelScale <= EQUAL_EPSILON )
            modelScale = 1.0f;

        LoadCollisionMap( collmapPath, model, origin, angles, modelScale );
    }
}


/* ParseVehicleCollMap  0x0041c3f0 */
int ParseVehicleCollMap( const char **parsePos, const char *modelName, int mapInfoIndex )
{
    const char   *token;
    orientation_t placement;
    float         modelScale;

    token = COM_Parse( parsePos );
    if ( strcmp( token, "iwmap" ) )
        Com_Error( "'%s' is missing 'iwmap' version specification\n",
                   GetMapFileName( mapInfoIndex ) );

    token = COM_Parse( parsePos );
    if ( atoi( token ) != 4 )
        Com_Error( "'%s' is version '%s'; should be version '%i'\n",
                   GetMapFileName( mapInfoIndex ), token, 4 );

    while ( 1 )
    {
        token = COM_Parse( parsePos );
        if ( !token[0] )
            return 0;
        if ( !strcmp( token, "{" ) )
            break;
        SkipRestOfLine( parsePos );
    }

    if ( strcmp( token, "{" ) )
        Com_Error( "ParseVehicleCollMap: { not found, found %s on line %d",
                   token, Com_GetCurrentParseLine() );

    if ( num_entities == MAX_MAP_ENTITIES )
        Com_Error( "num_entities == MAX_MAP_ENTITIES" );

    entitySourceBrushes = 0;
    mapent = &entities[num_entities];
    memset( mapent, 0, sizeof( Entity_t ) );
    map_entity_num++;
    num_entities++;

    Vec3Set( placement.origin,  0.0f, 0.0f, 0.0f );
    Vec3Set( placement.axis[0], 1.0f, 0.0f, 0.0f );
    Vec3Set( placement.axis[1], 0.0f, 1.0f, 0.0f );
    Vec3Set( placement.axis[2], 0.0f, 0.0f, 1.0f );
    modelScale = 1.0f;

    while ( 1 )
    {
        token = COM_Parse( parsePos );
        if ( !token[0] )
            Com_Error( "ParseVehicleCollMap: EOF without closing brace" );
        if ( !strcmp( token, "}" ) )
            break;

        if ( strcmp( token, "{" ) )
        {
            ParseEPair( (char *)token, parsePos );
            continue;
        }

        token = COM_Parse( parsePos );
        if ( !token[0] )
            break;

        if ( !strcmp( token, "curve" ) )
            Com_Error( "cannot have curves in collision maps" );
        else if ( !strcmp( token, "mesh" ) )
            Com_Error( "cannot have patch terrain in collision maps" );
        else if ( !strcmp( token, "physics_cylinder" ) )
            Com_Error( "cannot have physics cylinders in collision maps" );
        else if ( !strcmp( token, "physics_box" ) )
            Com_Error( "cannot have physics boxes in collision maps" );
        else
        {
            Com_UngetToken();
            ParseBrush( parsePos, &placement, modelScale, mapInfoIndex, 0 );
        }

        entitySourceBrushes++;
    }

    SetKeyValue( &entities[num_entities - 1], "targetname", modelName );
    SetKeyValue( &entities[num_entities - 1], "classname", "script_vehicle_collmap" );
    return 1;
}


/* ProcessScriptVehicles  0x0041c1b0 */
void ProcessScriptVehicles( void )
{
    int         i;
    int         j;
    Entity_t   *ent;
    const char *classname;
    const char *model;
    char        modelName[MAX_OS_PATH];
    char        collmapPath[MAX_OS_PATH];
    void       *buf;
    const char *parsePos;
    int         mapInfoIndex;

    for ( i = 1; i < num_entities; i++ )
    {
        ent       = &entities[i];
        classname = ValueForKey( ent, "classname" );

        if ( ( Q_stricmp( "script_vehicle", classname )
            && Q_stricmp( "script_vehicle_mp", classname ) )
          || !( model = ValueForKey( ent, "model" ) )[0] )
        {
            continue;
        }

        ExtractFileName( model, modelName );
        sprintf( collmapPath, "%s%s.map", ExpandPath( "../collmaps/" ), modelName );

        for ( j = 0; j < num_entities; j++ )
        {
            if ( i == j )
                continue;
            if ( !Q_stricmp( "script_vehicle_collmap", ValueForKey( &entities[j], "classname" ) )
              && !Q_stricmp( model, ValueForKey( &entities[j], "targetname" ) ) )
            {
                break;
            }
        }
        if ( j != num_entities )
            continue;

        if ( LoadFile( collmapPath, &buf ) < 0 )
        {
            Com_Printf( "WARNING: No collision map file for script_vehicle \"%s\"\n", modelName );
            continue;
        }

        mapInfoIndex = AddMapInfo( collmapPath, vec3_origin, vec3_origin, 1.0f );
        Map_BeginParseSession( collmapPath );
        parsePos = (const char *)buf;
        ParseVehicleCollMap( &parsePos, model, mapInfoIndex );
        Map_EndParseSession();
    }
}


/* SkipLayerListWithName  0x0041ab70 */
void SkipLayerListWithName( const char **parsePos )
{
    char        name[MAX_OS_PATH];
    const char *token;

    while ( 1 )
    {
        token = COM_Parse( parsePos );
        if ( !*parsePos || *token == '{' || !token )
            break;
        strcpy( name, token );
        token = COM_ParseExt( parsePos );
    }
    Com_UngetToken();
}


/* ParseMapEntity  0x0041cb20 */
static int ParseMapEntity( const char **parsePos, const orientation_t *orient, float scale,
                           int mapInfoIndex, int prefabIndex, int prefabFlags )
{
    const char   *token;
    const char   *classname;
    const char   *model;
    const char   *targetname;
    const char   *target;
    const char   *exploder;
    epair_t      *e;
    Entity_t     *saved;
    orientation_t entOrient;
    orientation_t newOrient;
    vec3_t        angles;
    float         localScale;

    token = COM_Parse( parsePos );
    if ( !token[0] )
        return 0;

    if ( strcmp( token, "{" ) )
    {
        Com_Error( "ParseEntity: { not found, found %s on line %d - "
                   "last entity was at: <%4.2f, %4.2f, %4.2f>...",
                   token, Com_GetCurrentParseLine(),
                   entities[num_entities].origin[0],
                   entities[num_entities].origin[1],
                   entities[num_entities].origin[2] );
    }

    if ( num_entities == MAX_MAP_ENTITIES )
        Com_Error( "num_entities == MAX_MAP_ENTITIES" );

    entitySourceBrushes = 0;
    mapent = &entities[num_entities];
    memset( mapent, 0, sizeof( Entity_t ) );
    mapent->entityNum = map_entity_num;
    mapent->mapIndex  = mapInfoIndex;
    map_entity_num++;
    num_entities++;

    while ( 1 )
    {
        token = COM_Parse( parsePos );
        if ( !token[0] )
            Com_Error( "ParseEntity: EOF without closing brace" );
        if ( !strcmp( token, "}" ) )
            break;

        if ( strcmp( token, "{" ) )
        {
            e = ParseEPair( (char *)token, parsePos );
            e->next = mapent->epairs;
            mapent->epairs = e;
            continue;
        }

        token = COM_Parse( parsePos );
        if ( !token[0] )
            break;

        if ( !strcmp( token, "curve" ) )
        {
            c_patches++;
            ParsePatch( parsePos, 0, orient, scale, mapInfoIndex, prefabFlags );
        }
        else if ( !strcmp( token, "mesh" ) )
        {
            c_patches++;
            ParsePatch( parsePos, 1, orient, scale, mapInfoIndex, prefabFlags );
        }
        else if ( !strcmp( token, "physics_cylinder" ) )
        {
            Com_Error( "cannot have physics cylinders in a level map" );
        }
        else if ( !strcmp( token, "physics_box" ) )
        {
            Com_Error( "cannot have physics boxes in a level map" );
        }
        else
        {
            Com_UngetToken();
            ParseBrush( parsePos, orient, scale, mapInfoIndex, prefabFlags );
        }

        entitySourceBrushes++;
    }

    classname = ValueForKey( mapent, "classname" );
    if ( !strcmp( classname, "func_cullgroup" ) )
    {
        SetKeyValue( mapent, "classname", "func_group" );
        classname = ValueForKey( mapent, "classname" );
    }

    if ( strcmp( classname, "func_group" ) )
    {
        if ( mapent->origin[0] != 0 || mapent->origin[1] != 0 || mapent->origin[2] != 0 )
        {
            if ( !mapent->brushes && !mapent->patches )
            {
                Com_Error( "Map %s, Entity %i, Brush 0, Origin %f %f %f: Model is an origin brush only",
                           GetMapFileName( mapInfoIndex ), map_entity_num - 1,
                           mapent->origin[0], mapent->origin[1], mapent->origin[2] );
            }
            else
            {
                AdjustBrushesForOrigin( mapent );
            }
        }
        else if ( strcmp( classname, "worldspawn" )
               && ( mapent->brushes || mapent->patches ) )
        {
            Assert( num_entities != 1 );
            SetEntityOriginFromGeometry( mapent );
            AdjustBrushesForOrigin( mapent );
        }
    }

    if ( !mapent->brushes && !mapent->patches && !mapent->terrain )
    {
        Assert( KeyExistsInPairs( mapent->epairs, "origin" )
             || !strcmp( classname, "worldspawn" )
             || !strcmp( classname, "func_group" ) );

        GetVectorForKey( mapent, "origin", entOrient.origin );
        GetVectorForKey( mapent, "angles", angles );
        AnglesToAxis( angles, entOrient.axis );
        TransformOrientation( &entOrient, orient, &newOrient );
        AxisToAngles( newOrient.axis, angles );
        Vec3Copy( newOrient.origin, mapent->origin );
        localScale = scale;

        SetKeyValue( mapent, "origin", va( "%g %g %g", mapent->origin[0],
                                           mapent->origin[1], mapent->origin[2] ) );

        if ( !Vec3Compare( angles, vec3_origin )
          || ValueForKey( mapent, "angles" )[0] )
        {
            SetKeyValue( mapent, "angles", va( "%g %g %g",
                                               angles[0], angles[1], angles[2] ) );
        }
    }
    else
    {
        newOrient  = *orient;
        localScale = scale;
    }

    if ( !strcmp( classname, "group_info" ) )
    {
        num_entities--;
        return 1;
    }

    if ( !_stricmp( classname, "worldspawn" ) )
    {
        ReflectionProbe_ParseColorCorrection( mapent );
        ReflectionProbe_ParseIgnorePortals( mapent );
        CM_ParseCollNodeLimit( mapent );
        BSP_ApplyWorldspawnBlockSize( mapent );
    }

    if ( !strcmp( classname, "func_group" )
      || ( !strcmp( classname, "worldspawn" ) && num_entities > 1 ) )
    {
        MoveBrushesToWorld( mapent, -1 );
        num_entities--;
        return 1;
    }

    if ( !strcmp( classname, "reflection_probe" ) )
    {
        ReflectionProbe_ParseEntity( mapent );
        num_entities--;
        return 1;
    }

    if ( !strcmp( classname, "misc_prefab" ) )
    {
        model = ValueForKey( mapent, "model" );
        if ( !model[0] )
            Com_Error( "misc_prefab at (%g %g %g) has no model",
                       mapent->origin[0], mapent->origin[1], mapent->origin[2] );

        num_entities--;
        if ( IntForKey( mapent, "spawnflags" ) & 1 )
            prefabFlags |= 1;

        saved = mapent;
        LoadPrefab( model, &newOrient, localScale, num_entities, prefabFlags );
        mapent = saved;
    }

    if ( prefabIndex != -1 )
    {
        targetname = ValueForKey( mapent, "targetname" );
        if ( !strncmp( targetname, "auto", 4 ) )
        {
            targetname = va( "pf%i_%s", prefabIndex, targetname );
            SetKeyValue( mapent, "targetname", targetname );
        }

        target = ValueForKey( mapent, "target" );
        if ( !strncmp( target, "auto", 4 ) )
        {
            target = va( "pf%i_%s", prefabIndex, target );
            SetKeyValue( mapent, "target", target );
        }

        target = ValueForKey( mapent, "target" );
        if ( !strncmp( target, "auto", 4 ) )
        {
            target = va( "pf%i_%s", prefabIndex, target );
            SetKeyValue( mapent, "target", target );
        }

        exploder = ValueForKey( mapent, "script_exploder" );
        if ( exploder[0] )
        {
            exploder = va( "%i%s", prefabIndex, exploder );
            SetKeyValue( mapent, "script_exploder", exploder );
        }
    }

    RenumberPrefabKeyList( mapent->epairs, prefabIndex, "script_color_allies" );
    RenumberPrefabKeyList( mapent->epairs, prefabIndex, "script_color_axis" );
    MovePhysicsBrushes( mapent );
    return 1;
}


/* ParseMapFile  0x0041ca20 */
static void ParseMapFile( const char *mapPath, const char *mapName, const char *fileData,
                          const orientation_t *orient, float scale,
                          int prefabIndex, int prefabFlags )
{
    int         mapInfoIndex;
    int         savedEntityNum;
    const char *parsePos;
    const char *token;
    vec3_t      angles;

    AxisToAngles( orient->axis, angles );
    mapInfoIndex = AddMapInfo( mapName, orient->origin, angles, scale );

    parsePos = fileData;

    token = COM_Parse( &parsePos );
    if ( strcmp( token, "iwmap" ) )
        Com_Error( "'%s' is missing 'iwmap' version specification\n", mapPath );

    token = COM_Parse( &parsePos );
    if ( atoi( token ) != 4 )
        Com_Error( "'%s' is version '%s'; should be version '%i'\n", mapPath, token, 4 );

    SkipLayerList( &parsePos );

    savedEntityNum  = map_entity_num;
    map_entity_num  = 0;
    while ( ParseMapEntity( &parsePos, orient, scale, mapInfoIndex, prefabIndex, prefabFlags ) )
        ;
    map_entity_num = savedEntityNum;
}


/* LoadPrefab  0x0041d550 */
static void LoadPrefab( const char *prefabName, const orientation_t *orient, float scale,
                        int prefabIndex, int prefabFlags )
{
    char  path[MAX_OS_PATH];
    void *buf;
    int   len;

    Assert( prefabName );
    Assert( orient );

    strcpy( path, ExpandPath( va( "map_source\\%s", prefabName ) ) );

    len = LoadFile( path, &buf );
    if ( len < 0 )
    {
        printf( "ERROR: Prefab '%s' not found\n", prefabName );
        return;
    }

    ParseMapFile( path, _strdup( prefabName ), (const char *)buf,
                  orient, scale, prefabIndex, prefabFlags );
    free( buf );
}


/* LoadMapFile  0x0041c770 */
void LoadMapFile( const char *filename )
{
    void         *buf;
    int           len;
    orientation_t identity;
    Brush_t      *b;

    Com_DPrintf( "--- LoadMapFile ---\n" );
    Com_Printf( "Loading map file %s\n", filename );

    len = LoadFile( filename, &buf );
    if ( len < 0 )
        Com_Error( "file '%s' not found\n", filename );

    Map_BeginParseSession( filename );

    num_entities    = 0;
    numMapDrawSurfs = 0;
    c_detail        = 0;
    buildBrush      = AllocBrush( MAX_BUILD_SIDES );

    Vec3Set( identity.origin,  0.0f, 0.0f, 0.0f );
    Vec3Set( identity.axis[0], 1.0f, 0.0f, 0.0f );
    Vec3Set( identity.axis[1], 0.0f, 1.0f, 0.0f );
    Vec3Set( identity.axis[2], 0.0f, 0.0f, 1.0f );

    ParseMapFile( filename, _strdup( filename ), (const char *)buf,
                  &identity, 1.0f, -1, 0 );

    if ( staticModelCollMaps )
        ProcessMiscModelCollMaps();
    ProcessScriptVehicles();

    ClearBounds( map_mins, map_maxs );
    for ( b = entities[0].brushes; b; b = b->next )
    {
        AddPointToBounds( b->mins, map_mins, map_maxs );
        AddPointToBounds( b->maxs, map_mins, map_maxs );
    }

    Com_DPrintf( "%5i total world brushes\n", CountBrushList( entities[0].brushes ) );
    Com_DPrintf( "%5i detail brushes\n",      c_detail );
    Com_DPrintf( "%5i patches\n",             c_patches );
    Com_DPrintf( "%5i boxbevels\n",           c_boxbevels );
    Com_DPrintf( "%5i edgebevels\n",          c_edgebevels );
    Com_DPrintf( "%5i entities\n",            num_entities );
    Com_DPrintf( "%5i planes\n",              nummapplanes );
    Com_DPrintf( "%5i areaportals\n",         c_areaportals );
    Com_DPrintf( "size: %5.0f,%5.0f,%5.0f to %5.0f,%5.0f,%5.0f\n",
                 map_mins[0], map_mins[1], map_mins[2],
                 map_maxs[0], map_maxs[1], map_maxs[2] );

    Map_EndParseSession();
    Materials_Finish();
}


/* ExpandBrushesAndWriteMap  0x0041d660 */
void ExpandBrushesAndWriteMap( void )
{
    Brush_t     *b;
    Brush_t     *copy;
    Brush_t     *list;
    unsigned int i;
    int          planenum;
    float        expandXY;
    float        expandZ;
    float        dist;

    if ( testExpand == TEST_EXPAND_PLAYER )
    {
        expandXY = 15.0f;
        expandZ  = 20.0f;
    }
    else
    {
        Assertx( testExpand == TEST_EXPAND_POINT, "(testExpand) = %i", testExpand );
        expandXY = 0.0f;
        expandZ  = 0.0f;
    }

    list = NULL;
    for ( b = entities[0].brushes; b; b = b->next )
    {
        copy = CopyCollisionBrush( b );
        copy->next = list;

        for ( i = 0; i < copy->sideCount; i++ )
        {
            planenum = copy->sides[i].planenum;
            dist = mapplanes[planenum].dist + expandXY
                 + fabs( mapplanes[planenum].normal[2] * expandZ );
            copy->sides[i].planenum = FindFloatPlane( mapplanes[planenum].normal, dist );
        }

        list = copy;
    }

    WriteBSPBrushMap( "expanded.map", list );
    Com_Error( "can't proceed after writing expanded.map" );
}
