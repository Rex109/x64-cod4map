/* Original: .\bsp.cpp */

#include "q_shared.h"
#include "assertive.h"
#include "com_math.h"
#include "com_vector.h"
#include "com_shared.h"
#include "com_files.h"
#include "cmdlib.h"
#include "bspfile.h"
#include "targetplatform.h"
#include "errors.h"
#include "map.h"
#include "brush.h"
#include "brush_sides.h"
#include "bsp.h"

extern void  Com_InitFilesystem( const char *basePath, const char *game,
                                 const char *baseGame );                    /* 0x0040ef90 */
extern void  VisMain( int argc, const char **argv );                        /* 0x00460100 */
extern void  ThreadSetDefault( void );                                      /* 0x0043bf80 */
extern int   I_strlen( const char *string );                                /* 0x0040a290 */

extern Face_t  *CollectBrushWindings( Brush_t *list );                      /* 0x00416240 */
extern void     ClipSidesIntoTree( Brush_t *list, Tree_t *tree );           /* 0x00416330 */
extern Tree_t  *BuildBspTree( Face_t *faces );
extern void     ProcessBspTree( Tree_t *tree );
extern int      FloodEntities( Tree_t *tree );
extern void     LeakFile( Tree_t *tree );                                   /* 0x00417e30 */


#include "surface.h"
extern void  OpenDebugFile( void );                                         /* 0x0043a230 */
extern void  CloseDebugFile( void );                                        /* 0x0043a290 */
extern void  SubdivideDrawSurfs( Entity_t *ent, Tree_t *tree );

extern void  ProcessEntityPatches( Entity_t *ent );                         /* 0x00429160 */
extern void  PatchMapDrawSurfs( Entity_t *ent );                            /* 0x0042a940 */

extern void  FillOutside( Node_t *headnode );                               /* 0x00430230 */
extern void  PortalizeWorld( Brush_t *brushes, Tree_t *tree, int leaked );  /* 0x00430b90 */
extern void  NumberClusters( Tree_t *tree );                                /* 0x00439780 */
extern void  WritePortalFile( Tree_t *tree );                               /* 0x00439800 */
extern void  FloodAreas( Tree_t *tree );                                    /* 0x004300c0 */

extern void  FreeTree( Tree_t *tree );                                 /* 0x0043c3c0 */
extern void  Tris_EmitEntityTris( Entity_t *ent, Tree_t *tree );          /* 0x0043e7c0 */
extern void  Tris_ReportLayeredMaterialUsage( void );                                     /* 0x0043e5e0 */

extern void  BeginModel( void );                                            /* 0x00464c80 */
extern void  BeginPhysicsModel( void );                                     /* 0x00464f70 */
extern void  EndModel( Node_t *headnode );                                  /* 0x00465120 */
extern void  EndPhysicsModel( Node_t *headnode );                           /* 0x004656e0 */
extern void  EmitLeafBrushes( Entity_t *ent, Tree_t *tree );
extern void  SetBrushModelNumbers( void );                                  /* 0x00464460 */
extern void  SetPhysicsMassProperties( void );                              /* 0x00464670 */
extern void  BeginBSPFile( void );                                          /* 0x004649a0 */
extern void  EndBSPFile( void );                                            /* 0x004649f0 */

extern void  CopyReflectionProbeLumpPreV12( BspHeader_t *header );          /* 0x0040a5c0 */

#include "map_reflection_probe.h"

#include "primarylights.h"

int   noCurveBrushes;                       /* 0x00a34bc0 */
char  g_convertPath[MAX_OS_PATH];           /* 0x00a34bc8 */
int   fulldetail;                           /* 0x00a34fc8 */
int   nowater;                              /* 0x00a34fcc */
int   verboseEntities;                      /* 0x00a34fd0 */
int   g_onlyEnts;                           /* 0x00a34fd4 */
int   nodetail;                             /* 0x00a34fd8 */
int   nosubdivide;                          /* 0x00a34fdc */
int   entity_num;                           /* 0x00a34fe4 */
char  g_loadFromPath[MAX_OS_PATH];          /* 0x00a34fe8 */
char  g_mapSourceFile[MAX_OS_PATH];         /* 0x00a353e8 */
char  g_outputBasePath[MAX_OS_PATH];        /* 0x00a357e8 */
int   leaktest;                             /* 0x00a35be8 */
float blockSize;                            /* 0x00a35bf0 */
int   staticModelCollMaps;                  /* 0x00a35bf8 */
int   displayCollMapWarnings;               /* 0x00a35bfc */
int   testExpand;                           /* 0x00a35c00 */
float listSlowEntities;                     /* 0x00a35c04 */
int   sourcePlatform;                       /* 0x00a35c08 */

int   brushMethod = 2;                      /* 0x00529070 */
int   reorderTris = 1;                      /* 0x00529074 */
float sampleScale = 1.0f;                   /* 0x0052907c */

extern float smoothAngle;                   /* 0x005296a0 */
extern int   warnLayerUses;                 /* 0x005296a4 */
extern float warnLayerArea;                 /* 0x2b80def4 */
extern float defaultTessSize;               /* 0x123ce900 */
extern int   reflectionDebug;               /* 0x123a948c */
extern int   debugPortals;                  /* 0x12f0f938 */
extern byte  debugLightmaps;                /* 0x11ba842c */
extern int   numMapDrawSurfs;               /* 0x14899cbc */

#define MIN_BLOCK_SIZE  64.0f


/* OnlyEnts  0x00408af0 */
void OnlyEnts( void )
{
    char bspPath[MAX_OS_PATH];
    int  brushCount;
    int  i;

    sprintf( bspPath, "%s%s", g_outputBasePath, GetBSPFileExtension() );
    LoadBSPFileLumps( bspPath, qfalse );

    num_entities = 0;
    SaveReflectionProbes();
    SavePrimaryLights();

    BSP_LoadMapFile( g_mapSourceFile );

    FreeSavedReflectionProbes();
    SetupPrimaryLights();
    ComparePrimaryLights();
    SetBrushModelNumbers();
    UnparseEntitiesWithOrigins();

    brushCount = 0;
    for ( i = 0; i < num_entities; i++ )
        brushCount += CountBrushList( entities[i].brushes );

    if ( brushCount != numBSPBrushes )
    {
        CheckForNewerSourceMaps( bspPath );
        Com_Error( "\nERROR: Brush count has changed from %d to %d.  "
                   "You cannot use '-onlyents' when the brushes have changed.",
                   numBSPBrushes, brushCount );
    }

    Assert( targetPlatform != PLATFORM_VOID );
    WriteBSPFile( bspPath );
}


/* BSPInfo  0x00408900 */
void BSPInfo( int argc, const char **argv )
{
    const char *extensions[1] = { "d3d" };
    int   i;
    unsigned int j;
    int   found;
    int   fileSize;
    FILE *f;

    if ( argc < 1 )
    {
        Com_Printf( "No files to dump info for.\n" );
        return;
    }

    Swap_Init();

    for ( i = 0; i < argc; i++ )
    {
        Com_Printf( "---------------------\n" );
        found = 0;
        StripExtension( (char *)argv[i] );

        for ( j = 0; j < ARRAY_COUNT( extensions ); j++ )
        {
            SetBSPFileExtensions( extensions[j] );
            strcpy( g_outputBasePath, argv[i] );
            DefaultExtension( g_outputBasePath, GetBSPFileExtension() );

            f = fopen( g_outputBasePath, "rb" );
            if ( !f )
                continue;

            found++;
            fileSize = FS_FileLength( f );
            fclose( f );
            Com_Printf( "%s: %i\n", g_outputBasePath, fileSize );
            LoadBSPFileLumps( g_outputBasePath, qtrue );
            PrintBSPFileSizes( fileSize );
            Com_Printf( "---------------------\n" );
        }

        if ( !found )
            Com_Error( "no bsp files with name '%s'\n", argv[i] );
    }
}


/* GetOutputFileName  0x00408a70 */
void GetOutputFileName( const char *extension, char *out, int outSize )
{
    const char *source;
    int         len;

    if ( g_loadFromPath[0] )
        source = g_loadFromPath;
    else
        source = g_mapSourceFile;

    I_strncpyz( out, source, outSize );
    COM_StripExtension( out, out );
    len = I_strlen( out );
    I_strncpyz( out + len, extension, outSize - len );
}


/* FS_InitInstallDir  0x004088e0 */
void FS_InitInstallDir( void )
{
    Com_InitFilesystem( g_installDir, "", "" );
}


/* BSP_LoadMapFile  0x00408c30 */
void BSP_LoadMapFile( const char *mapFile )
{
    OpenLightGridFiles();

    if ( g_loadFromPath[0] )
        LoadMapFile( g_loadFromPath );
    else
        LoadMapFile( mapFile );

    CloseLightGridFiles();
}


/* Com_FatalError  0x00408c70 */
void Com_FatalError( const char *fmt, va_list argptr )
{
    vprintf( fmt, argptr );
    Com_Printf( "\r\n" );
    fflush( stdout );
    exit( -1 );
}


/* BSP_InitErrorHandler  0x00408cb0 */
void BSP_InitErrorHandler( void )
{
    Com_SetErrorHandler( Com_FatalError );
}


/* ParseSmoothAngle  0x00408cd0 */
float ParseSmoothAngle( const char *str, const char *optionName )
{
    float angle;
    float radians;
    float threshold;

    angle = (float)atof( str );

    if ( angle > 0 )
    {
        if ( angle < 180.0f )
        {
            Com_Printf( "%s = %g\n", optionName, angle );
            radians   = angle * DEG2RAD;
            threshold = cos( radians ) - EQUAL_EPSILON;
            return I_fmax( 0.0f, threshold );
        }

        Com_Printf( "%s = 180\n", optionName );
        return 1.0f;
    }

    Com_Printf( "%s = 0\n", optionName );
    return 1.0f;
}


/* BSP_SetFileExtensions  0x00408dd0 */
void BSP_SetFileExtensions( void )
{
    SetBSPFileExtensions( "d3d" );
}


/* BSP_SetTargetPlatform  0x00408d90 */
void BSP_SetTargetPlatform( void )
{
    Assert( targetPlatform != PLATFORM_VOID );
    BSP_SetFileExtensions();
}


int Opt_loadFrom( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();
    strcpy( g_loadFromPath, argv[1] );
    return 2;
}

int Opt_OnlyEnts( void )
{
    Com_Printf( "onlyents = true\n" );
    g_onlyEnts = 1;
    return 1;
}

int Opt_Verbose( void )
{
    verbose = 1;
    return 1;
}

int Opt_VerboseEntities( void )
{
    Com_Printf( "verboseentities = true\n" );
    verboseEntities = 1;
    return 1;
}

/* ClampBlockSize  0x00408f20 */
void ClampBlockSize( void )
{
    if ( blockSize > 0 && blockSize <= MIN_BLOCK_SIZE )
        blockSize = MIN_BLOCK_SIZE;
    else if ( blockSize <= 0 )
        blockSize = 0.0f;
}

int Opt_blockSize( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();

    blockSize = (float)atof( argv[1] );
    ClampBlockSize();

    if ( blockSize != 0 )
        Com_Printf( "blocksize is %g units\n", blockSize );
    else
        Com_Printf( "blocksize is disabled\n", blockSize );

    return 2;
}

int Opt_sampleScale( int argc, const char **argv )
{
    float scale;

    if ( argc < 2 )
        PrintUsage();

    scale = (float)atof( argv[1] );
    if ( scale <= 0 )
        Com_Error( "sampleScale must be > 0" );

    sampleScale = 1.0f / scale;
    Com_Printf( "each lightmap sample will be scaled by %g\n", scale );
    return 2;
}

int Opt_NoWater( void )
{
    Com_Printf( "nowater = true\n" );
    nowater = 1;
    return 1;
}

int Opt_NoCurves( void )
{
    Com_Printf( "nocurves = true\n" );
    noCurveBrushes = 1;
    return 1;
}

int Opt_NoDetail( void )
{
    Com_Printf( "nodetail = true\n" );
    nodetail = 1;
    return 1;
}

int Opt_FullDetail( void )
{
    Com_Printf( "fulldetail = true\n" );
    fulldetail = 1;
    return 1;
}

int Opt_NoSubdivide( void )
{
    Com_Printf( "nosubdivide = true\n" );
    nosubdivide = 1;
    return 1;
}

int Opt_StaticModelCollMaps( void )
{
    Com_Printf( "staticModelCollMaps = true\n" );
    staticModelCollMaps = 1;
    return 1;
}

int Opt_DisplayCollMapWarnings( void )
{
    Com_Printf( "displayCollMapWarnings = true\n" );
    displayCollMapWarnings = 1;
    return 1;
}

int Opt_smoothAngle( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();
    smoothAngle = ParseSmoothAngle( argv[1], "smoothAngle" );
    return 2;
}

int Opt_subdivisions( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();

    defaultTessSize = (float)atof( argv[1] );
    if ( defaultTessSize < 0 )
        defaultTessSize = 0.0f;

    if ( defaultTessSize == 0 )
        Com_Printf( "automatic subdivision disabled\n" );
    else
        Com_Printf( "automatically subdividing everything to %g x %g units\n",
                    defaultTessSize, defaultTessSize );

    return 2;
}

int Opt_LeakTest( void )
{
    Com_Printf( "leaktest = true\n" );
    leaktest = 1;
    return 1;
}

int Opt_brushMethod( int argc, const char **argv )
{
    struct
    {
        const char *name;
        int         value;
    }
    methods[4];
    unsigned int i;

    methods[0].name = "players";  methods[0].value =  2;
    methods[1].name = "bullets";  methods[1].value =  1;
    methods[2].name = "all";      methods[2].value = -1;
    methods[3].name = "none";     methods[3].value =  0;

    if ( argc < 2 )
        PrintUsage();

    for ( i = 0; i < ARRAY_COUNT( methods ); i++ )
    {
        if ( !Q_stricmp( argv[1], methods[i].name ) )
        {
            brushMethod = methods[i].value;
            return 2;
        }
    }

    Com_Printf( "Invalid brush method '%s'.  Valid methods are:\n", argv[1] );
    for ( i = 0; i < 4; i++ )
        Com_Printf( "  %s\n", methods[i].name );
    exit( -1 );
    return 2;
}

int Opt_ExpandPlayer( void )
{
    Com_Printf( "Writing expanded.map for player collision.\n" );
    testExpand = TEST_EXPAND_PLAYER;
    return 1;
}

int Opt_ExpandBullet( void )
{
    Com_Printf( "Writing expanded.map for bullet collision.\n" );
    testExpand = TEST_EXPAND_POINT;
    return 1;
}

int Opt_DebugPortals( void )
{
    debugPortals = 1;
    return 1;
}

int Opt_DebugLightmaps( void )
{
    debugLightmaps = 1;
    return 1;
}

int Opt_NoReorderTris( void )
{
    reorderTris = 0;
    return 1;
}

int Opt_listSlowEntities( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();
    listSlowEntities = (float)atof( argv[1] );
    return 2;
}

int Opt_warnLayerUses( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();
    warnLayerUses = atoi( argv[1] ) + 1;
    return 2;
}

int Opt_warnLayerArea( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();
    warnLayerArea = (float)atof( argv[1] );
    return 2;
}

int Opt_reflectionDebug( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();
    reflectionDebug = atoi( argv[1] );
    return 2;
}

int Opt_platform( int argc, const char **argv )
{
    if ( argc < 2 )
        PrintUsage();
    SetTargetPlatformByName( argv[1] );
    return 2;
}

int Opt_PcToXenon( void )
{
    sourcePlatform = PLATFORM_PC;
    targetPlatform = PLATFORM_XENON;
    return 1;
}

int Opt_XenonToPc( void )
{
    sourcePlatform = PLATFORM_XENON;
    targetPlatform = PLATFORM_PC;
    return 1;
}


OptionEntry_t optionsTable[29] =
{
    { "-platform",              "Required if not copying a bsp between platforms; specifies target platform", ( optionHandler_t )Opt_platform               },
    { "-pcToXenon",             "Copies a PC bsp to a Xenon bsp",                                             ( optionHandler_t )Opt_PcToXenon              },
    { "-xenonToPc",             "Copies a Xenon bsp to a PC bsp",                                             ( optionHandler_t )Opt_XenonToPc              },
    { "-v",                     "Verbose; enables extra compilation messages",                                ( optionHandler_t )Opt_Verbose                },
    { "-verboseEntities",       "Includes verbose messages for submodels if '-v' is given",                   ( optionHandler_t )Opt_VerboseEntities        },
    { "-onlyEnts",              "Compile doesn't touch triggers, geometry, or lighting",                      ( optionHandler_t )Opt_OnlyEnts               },
    { "-sampleScale",           "Scales all lightmaps; 2 doubles pixel size, 0.5 halves it",                  ( optionHandler_t )Opt_sampleScale            },
    { "-blockSize",             "Grid size for regular BSP splits; 0 uses largest possible",                  ( optionHandler_t )Opt_blockSize              },
    { "-subdivisions",          "Divides all geometry on a grid; only works for small maps",                  ( optionHandler_t )Opt_subdivisions           },
    { "-noSubdivide",           "Ignores the 'tessSize' setting in all materials",                            ( optionHandler_t )Opt_NoSubdivide            },
    { "-staticModelCollMaps",   "(Legacy support) Use collmaps for static models",                            ( optionHandler_t )Opt_StaticModelCollMaps    },
    { "-displayCollMapWarnings","Display missing collmap warnings",                                           ( optionHandler_t )Opt_DisplayCollMapWarnings },
    { "-smoothAngle",           "Smooth surfaces are only smoothed at angles less than this",                 ( optionHandler_t )Opt_smoothAngle            },
    { "-loadFrom",              "Reads this map instead, but still writes to <mapfile>",                      ( optionHandler_t )Opt_loadFrom               },
    { "-noWater",               "Ignores all water brushes",                                                  ( optionHandler_t )Opt_NoWater                },
    { "-noCurves",              "Ignores all patch and terrain geometry",                                     ( optionHandler_t )Opt_NoCurves               },
    { "-noDetail",              "Ignores all detail brushes",                                                 ( optionHandler_t )Opt_NoDetail               },
    { "-fullDetail",            "Turns all detail brushes into structural brushes",                           ( optionHandler_t )Opt_FullDetail             },
    { "-leakTest",              "Quits immediately if the map leaked",                                        ( optionHandler_t )Opt_LeakTest               },
    { "-brushMethod",           "Brush optimization method (players/bullets/all/none)",                       ( optionHandler_t )Opt_brushMethod            },
    { "-expandPlayer",          "Writes a map for Radiant to see player-to-brush collision",                  ( optionHandler_t )Opt_ExpandPlayer           },
    { "-expandBullet",          "Writes a map for Radiant to see bullet-to-brush collision",                  ( optionHandler_t )Opt_ExpandBullet           },
    { "-debugPortals",          "Writes a _portals.map showing portal/structural geometry",                   ( optionHandler_t )Opt_DebugPortals           },
    { "-debugLightmaps",        "Fills lightmaps with random colors to show seams",                           ( optionHandler_t )Opt_DebugLightmaps         },
    { "-noReorderTris",         "Disables reordering of optimized triangles for T&L cache",                   ( optionHandler_t )Opt_NoReorderTris          },
    { "-listSlowEntities",      "Lists entities that process in more than this many seconds",                 ( optionHandler_t )Opt_listSlowEntities       },
    { "-warnLayerUses",         "Generates warnings for layer combos used this many or fewer times",          ( optionHandler_t )Opt_warnLayerUses          },
    { "-warnLayerArea",         "Generates warnings for layer combos used less than this many square inches", ( optionHandler_t )Opt_warnLayerArea          },
    { "-reflectionDebug",       "Reflection debug mode. 1 is rainbow colors.",                                ( optionHandler_t )Opt_reflectionDebug        }
};


/* PrintUsage  0x00409500 */
void PrintUsage( void )
{
    unsigned int i;

    Com_Printf( "USAGE: cod2map [options] mapname, where options are 0 or more of the following.\n" );
    Com_Printf( "Options ignore capitalization; it is only present in the list for clarity.\n" );
    for ( i = 0; i < ARRAY_COUNT( optionsTable ); i++ )
        Com_Printf( "%-20s %s\n", optionsTable[i].name, optionsTable[i].description );
    exit( -1 );
}


/* ParseBSPOption  0x0040a170 */
int ParseBSPOption( int argc, const char **argv )
{
    unsigned int i;

    for ( i = 0; i < ARRAY_COUNT( optionsTable ); i++ )
    {
        if ( !_stricmp( argv[0], optionsTable[i].name ) )
            return optionsTable[i].func( argc, argv );
    }

    Com_Printf( "\n\nUnknown argument '%s'\n\n", argv[0] );
    PrintUsage();
    return 0;
}


/* ProcessBSPArguments  0x0040a0f0 */
void ProcessBSPArguments( int argc, const char **argv )
{
    int argsUsed;

    for ( ; argc > 0; argv += argsUsed )
    {
        argsUsed = ParseBSPOption( argc, argv );
        SanityCheckx( argsUsed > 0, "%s", va( "%i:%s", argsUsed, argv[0] ) );
        argc -= argsUsed;
    }
}


/* BSP_ApplyWorldspawnBlockSize  0x0040a1f0 */
void BSP_ApplyWorldspawnBlockSize( const Entity_t *ent )
{
    const char *value;

    value = ValueForKey( ent, "blocksize" );
    Assert( value );

    if ( blockSize == 0 && value[0] && value[0] != '0' )
    {
        blockSize = (float)atof( value );
        ClampBlockSize();
        Com_Printf( "map blocksize is %g units\n", blockSize );
    }
}


/* BSP_LoadReflectionProbes  0x0040a320 */
void BSP_LoadReflectionProbes( const char *filename )
{
    BspHeader_t *header;

    if ( !LoadBSPFile( filename, &header, qtrue ) )
    {
        numBSPReflectionProbes = 0;
        return;
    }

    if ( header->version < 8 )
        numBSPReflectionProbes = 0;
    else if ( header->version < 12 )
        CopyReflectionProbeLumpPreV12( header );
    else
        numBSPReflectionProbes = CopyLump( header, LUMP_REFLECTIONPROBES,
                                           bspReflectionProbes,
                                           sizeof( BspReflectionProbe_t ),
                                           MAX_MAP_REFLECTION_PROBES * sizeof( BspReflectionProbe_t ) );

    free( header );
}


/* BSP_LoadOldReflectionProbes  0x00409f90 */
void BSP_LoadOldReflectionProbes( void )
{
    char  path[MAX_OS_PATH];
    FILE *f;

    sprintf( path, "%s%s", g_outputBasePath, GetBSPFileExtension() );

    f = fopen( path, "rb" );
    if ( f )
    {
        fclose( f );
        BSP_LoadReflectionProbes( path );
        SaveReflectionProbes();
    }
}


/* BSP_ConvertPlatform  0x0040a010 */
int BSP_ConvertPlatform( const char *basePath )
{
    SetBSPFileExtensions( "d3d" );
    FS_Startup( basePath );
    FS_InitInstallDir();

    strcpy( g_outputBasePath, ExpandArg( basePath ) );
    StripExtension( g_outputBasePath );
    strcat( g_outputBasePath, GetBSPFileExtension() );

    strcpy( g_convertPath, g_outputBasePath );
    ReplacePathComponent( g_convertPath,
                          g_platformNames[sourcePlatform],
                          g_platformNames[targetPlatform] );

    printf( "\nCopying bsp: \n" );
    printf( "\t %s -> \n", g_outputBasePath );
    printf( "\t %s \n", g_convertPath );

    LoadAndWriteBSPFile( g_outputBasePath, g_convertPath );
    return 0;
}


/* EmitWorldBSP  0x00409a50 */
void EmitWorldBSP( void )
{
    Face_t *faces;
    Tree_t *tree;
    int     leaked;

    BeginModel();
    entities[0].firstDrawSurf = 0;
    OpenDebugFile();
    ProcessEntityPatches( &entities[0] );
    PatchMapDrawSurfs( &entities[0] );
    BrushSides_BeginEntity( &entities[0] );
    BrushSides_BuildTree( &entities[0] );

    faces = CollectBrushWindings( entities[0].brushes );
    tree  = BuildBspTree( faces );
    ProcessBspTree( tree );
    FilterStructuralBrushesIntoBspTree( &entities[0], tree );

    if ( !FloodEntities( tree ) )
    {
        Com_Printf( "**********************\n" );
        Com_Printf( "******* leaked *******\n" );
        Com_Printf( "**********************\n" );
        LeakFile( tree );
        if ( leaktest )
        {
            Com_Printf( "--- MAP LEAKED, ABORTING LEAKTEST ---\n" );
            exit( -1 );
        }
        leaked = 1;
    }
    else
    {
        FillOutside( tree->headnode );
        ClipSidesIntoTree( entities[0].brushes, tree );
        faces = CollectBrushWindings( entities[0].brushes );
        FreeTree( tree );
        tree = BuildBspTree( faces );
        ProcessBspTree( tree );
        FilterStructuralBrushesIntoBspTree( &entities[0], tree );
        leaked = 0;
    }

    BrushSides_EndEntity( &entities[0] );
    CloseDebugFile();
    PortalizeWorld( entities[0].brushes, tree, leaked );
    NumberClusters( tree );
    if ( !leaked )
        WritePortalFile( tree );
    FloodAreas( tree );

    FilterDetailBrushesIntoBspTree( &entities[0], tree );
    if ( !nosubdivide )
        SubdivideDrawSurfs( &entities[0], tree );

    AssignReflectionProbesToCells( tree );
    AssignReflectionProbesToDrawSurfs( tree, 0 );
    SortDrawSurfaces();
    BuildDrawSurfTree();
    BuildPrimaryLightRegions();

    Tris_EmitEntityTris( &entities[0], tree );
    EmitLeafBrushes( &entities[0], tree );
    AddBrushNeighborBevels( entities[0].brushes );

    EndModel( tree->headnode );
    BrushSides_Shutdown();
    FreeTree( tree );

    if ( testExpand )
        ExpandBrushesAndWriteMap();
}


/* EmitEntityBSP  0x00409cc0 */
void EmitEntityBSP( void )
{
    Entity_t *ent;
    Node_t   *node;
    Tree_t   *tree;
    Brush_t  *brush;
    Brush_t  *copy;

    BeginModel();
    ent = &entities[entity_num];
    ent->firstDrawSurf = numMapDrawSurfs;

    ProcessEntityPatches( ent );
    PatchMapDrawSurfs( ent );
    SortDrawSurfaces();

    node = AllocNode();
    node->planenum = PLANENUM_LEAF;
    for ( brush = ent->brushes; brush; brush = brush->next )
    {
        copy = CopyBrush( brush );
        copy->next = node->leafBrushes;
        node->leafBrushes = copy;
    }

    tree = AllocTree();
    tree->headnode = node;

    BrushSides_BeginEntity( ent );
    BrushSides_BuildTree( ent );
    BrushSides_EndEntity( ent );

    if ( !nosubdivide )
        SubdivideDrawSurfs( ent, tree );

    AssignReflectionProbesToDrawSurfs( tree, ent->firstDrawSurf );
    Tris_EmitEntityTris( ent, tree );
    EmitLeafBrushes( ent, tree );
    AddBrushNeighborBevels( ent->brushes );

    EndModel( node );
    BrushSides_Shutdown();
    FreeTree( tree );
}


/* EmitPhysicsBSP  0x00409e10 */
void EmitPhysicsBSP( void )
{
    Entity_t *ent;
    Node_t   *node;
    Brush_t  *brush;
    Brush_t  *copy;

    BeginPhysicsModel();
    ent = &entities[entity_num];
    ent->firstDrawSurf = numMapDrawSurfs;

    node = AllocNode();
    node->planenum = PLANENUM_LEAF;
    for ( brush = ent->physicsBrushes; brush; brush = brush->next )
    {
        copy = CopyBrush( brush );
        copy->next = node->leafBrushes;
        node->leafBrushes = copy;
    }

    AddBrushNeighborBevels( ent->physicsBrushes );
    EndPhysicsModel( node );
}


/* PrintSlowEntity  0x00409eb0 */
void PrintSlowEntity( const Entity_t *ent, double seconds )
{
    const char *mapName;
    int         entityNum;

    if ( ent->brushes )
    {
        mapName   = GetMapFileName( ent->brushes->mapInfoIndex );
        entityNum = ent->brushes->entitynum;
    }
    else if ( ent->physicsBrushes )
    {
        mapName   = GetMapFileName( ent->physicsBrushes->mapInfoIndex );
        entityNum = ent->physicsBrushes->entitynum;
    }
    else
    {
        Assert( ent->patches );
        mapName   = GetMapFileName( ent->patches->mapInfoIndex );
        entityNum = ent->patches->entitynum;
    }

    Com_Printf( "entity %i in map %s took %.1f seconds to process (entity %i of %i to process)\n",
                entityNum, mapName, seconds, entity_num, num_entities );
}


/* EmitBSP  0x00409840 */
void EmitBSP( void )
{
    int       savedVerbose;
    double    lastReport;
    double    entityStart;
    double    now;
    Entity_t *ent;
    unsigned int modelIndex;

    savedVerbose = verbose;
    lastReport   = 0.0;

    for ( entity_num = 0; entity_num < num_entities; entity_num++ )
    {
        ent = &entities[entity_num];
        if ( !ent->brushes && !ent->physicsBrushes && !ent->patches )
            continue;

        Com_DPrintf( "############### model %i ###############\n", numBSPModels );

        if ( entity_num == 0 )
        {
            EmitWorldBSP();
            lastReport = I_FloatTime();
            if ( !verbose )
                Com_Printf( "Finished processing world entity\n\n" );
        }
        else
        {
            entityStart = I_FloatTime();
            if ( !verbose && entityStart - lastReport > 10.0 )
            {
                Com_Printf( "Processing entity %i of %i\n", entity_num + 1, num_entities );
                lastReport = entityStart;
            }

            EmitEntityBSP();
            if ( ent->physicsBrushes )
                EmitPhysicsBSP();

            if ( listSlowEntities > 0 )
            {
                now = I_FloatTime();
                if ( now - entityStart > listSlowEntities )
                {
                    PrintSlowEntity( ent, now - entityStart );
                    lastReport = now;
                }
            }
        }

        if ( !verboseEntities )
            verbose = 0;
    }

    Tris_ReportLayeredMaterialUsage();

    for ( modelIndex = 1; modelIndex < (unsigned int)numBSPModels; modelIndex++ )
    {
        Assert( bspModels[modelIndex].numBrushes
             || bspModels[modelIndex].numSurfaces
             || ( bspModels[modelIndex].triSoupCount[TRIS_TYPE_LAYERED]
               && bspModels[modelIndex].triSoupCount[TRIS_TYPE_SIMPLE] ) );
    }

    verbose = savedVerbose;
}


/* main  0x00409570 */
int main( int argc, const char **argv )
{
    double startTime;
    double endTime;
    char   path[MAX_OS_PATH];

    Com_Printf( "CoD4Map v1.1 (c) 2002 Id Software Inc. / Infinity Ward\n" );
    BSP_InitErrorHandler();

    bspLightGridHeader.rowAxis = 0;
    bspLightGridHeader.colAxis = 1;
    memset( bspLightGridHeader.rowDataStart, 0xff, sizeof( bspLightGridHeader.rowDataStart ) );

    if ( argc < 2 )
        PrintUsage();

    if ( !strcmp( argv[1], "-info" ) )
    {
        BSPInfo( argc - 2, argv + 2 );
        return 0;
    }
    if ( !strcmp( argv[1], "-vis" ) )
    {
        VisMain( argc - 1, argv + 1 );
        return 0;
    }

    Com_Printf( "---- cod2map ----\n" );
    g_loadFromPath[0] = 0;
    ProcessBSPArguments( argc - 2, argv + 1 );
    startTime = I_FloatTime();
    ThreadSetDefault();

    if ( sourcePlatform )
        return BSP_ConvertPlatform( argv[argc - 1] );

    if ( !ValidatePlatformSet() )
        return 0;

    BSP_SetTargetPlatform();
    FS_Startup( argv[argc - 1] );
    FS_InitInstallDir();

    strcpy( g_outputBasePath, ExpandArg( argv[argc - 1] ) );
    StripExtension( g_outputBasePath );

    sprintf( path, "%s%s", g_outputBasePath, GetPRTFileExtension() );
    remove( path );
    sprintf( path, "%s.lin", g_outputBasePath );
    remove( path );

    strcpy( g_mapSourceFile, ExpandArg( argv[argc - 1] ) );
    if ( strcmp( &g_mapSourceFile[strlen( g_mapSourceFile ) - 4], ".reg" ) )
    {
        sprintf( path, "%s.reg", g_outputBasePath );
        remove( path );
        DefaultExtension( g_mapSourceFile, ".map" );
    }

    MapError_Init();

    if ( g_onlyEnts )
    {
        OnlyEnts();
        return 0;
    }

    BSP_LoadOldReflectionProbes();
    BSP_LoadMapFile( g_mapSourceFile );
    FreeSavedReflectionProbes();
    SetupPrimaryLights();
    SetBrushModelNumbers();
    SetPhysicsMassProperties();
    BeginBSPFile();
    EmitBSP();
    EndBSPFile();

    endTime = I_FloatTime();
    Com_Printf( "%5.0f seconds elapsed\n", endTime - startTime );
    return 0;
}
