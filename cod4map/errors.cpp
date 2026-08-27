
#include "cod4map.h"
#include "errors.h"

extern const char *GetMapFileName( int mapIndex );                                  /* 0x004191d0 */
extern void        GetMapOrientation( int mapIndex, orientation_t *orient, float *scale ); /* 0x004193a0 */
extern void        GetOutputFileName( const char *extension, char *out, int outSize );     /* 0x00408a70 */
extern void        WindingCenter( const winding_t *w, vec3_t center );              /* 0x0042bde0 */
extern qboolean    PlaneFromWinding( const winding_t *w, vec4_t plane );            /* 0x0042ba30 */
extern Entity_t    entities[];                                                      /* 0x10757408 */
extern int         entity_num;                                                      /* 0x00a34fe4 */


#define SRCFILE ".\\errors.cpp"

extern void AssertFailed(const char *file, int line, int fatal, const char *fmt, ...);  /* 0x00402180 */

#ifndef Assert
#define Assert(exp) \
    do { if (!(exp)) AssertFailed(SRCFILE, __LINE__, 0, "%s", #exp); } while (0)
#endif

#ifndef Assertx
#define Assertx(exp, fmt, ...) \
    do { if (!(exp)) AssertFailed(SRCFILE, __LINE__, 0, "%s\n\t" fmt, #exp, __VA_ARGS__); } while (0)
#endif


static const vec3_t s_defaultErrorNormal = { 1.0f, 0.0f, 0.0f };    /* 0x004f7b38 */

static char s_errorFileName[MAX_OS_PATH];       /* 0x11ba8008 */
static bool s_mapErrorOccurred;                 /* 0x11ba8408 */
static int  s_mapErrorCount;                    /* 0x11ba840c */


/* MapError  0x004149e0 */
void MapError( int severity, const vec3_t pos, const vec3_t normal,
               int mapIndex, int entityNum, int brushNum,
               const char *fmt, ... )
{
    char           message[4096];
    va_list        argptr;
    const char    *mapName;
    FILE          *file;
    orientation_t  mapOrient;
    float          mapScale;
    vec3_t         offsetPos;
    vec3_t         invNormal;
    vec3_t         localPos;
    vec3_t         localDir;

    if ( severity == 1 )
        s_mapErrorOccurred = true;

    va_start( argptr, fmt );
    _vsnprintf( message, sizeof( message ), fmt, argptr );
    va_end( argptr );
    message[sizeof( message ) - 1] = '\0';

    Assertx( (strchr( message, '\"' ) == 0), "(message) = %s", message );
    Assertx( (strchr( message, '\n' ) == 0), "(message) = %s", message );

    mapName = GetMapFileName( mapIndex );

    if ( entityNum >= 0 && brushNum >= 0 )
    {
        printf( "ERROR: %s\nIn map %s on entity %i brush %i at %.0f %.0f %.0f\n",
                message, mapName, entityNum, brushNum, pos[0], pos[1], pos[2] );
    }
    else
    {
        printf( "ERROR: %s\nIn map %s at %.0f %.0f %.0f\n",
                message, mapName, pos[0], pos[1], pos[2] );
    }

    file = fopen( s_errorFileName, "at" );
    if ( file )
    {
        GetMapOrientation( mapIndex, &mapOrient, &mapScale );

        if ( !normal )
            normal = s_defaultErrorNormal;

        Vec3Mad( pos, 16.0f, normal, offsetPos );
        Vec3Negate( normal, invNormal );

        InverseTransformDir( &mapOrient, invNormal, localDir );
        InverseTransformPoint( &mapOrient, offsetPos, localPos );

        if ( mapScale != 0.0f )
            Vec3Scale( localPos, 1.0f / mapScale, localPos );

        Vec3Add( entities[entity_num].origin, localPos, localPos );

        fprintf( file, "\"%s\" %i %i %g %g %g %g %g %g \"%s\"\n",
                 mapName, entityNum, brushNum,
                 localPos[0], localPos[1], localPos[2],
                 localDir[0], localDir[1], localDir[2],
                 message );

        fclose( file );
        s_mapErrorCount++;
    }
}

/* MapErrorWinding  0x00414cd0 */
void MapErrorWinding( int severity, const winding_t *w,
                      int mapIndex, int entityNum, int brushNum,
                      const char *fmt, ... )
{
    char    message[4096];
    va_list argptr;
    vec3_t  center;
    vec4_t  plane;

    va_start( argptr, fmt );
    _vsnprintf( message, sizeof( message ), fmt, argptr );
    va_end( argptr );
    message[sizeof( message ) - 1] = '\0';

    Assertx( (strchr( message, '\"' ) == 0), "(message) = %s", message );
    Assertx( (strchr( message, '\n' ) == 0), "(message) = %s", message );

    WindingCenter( w, center );
    if ( !PlaneFromWinding( w, plane ) )
        Vec3Set( plane, 1.0f, 0.0f, 0.0f );

    MapError( severity, center, plane, mapIndex, entityNum, brushNum, "%s", message );
}

/* MapError_Init  0x00414e10 */
void MapError_Init( void )
{
    GetOutputFileName( ".errlog", s_errorFileName, sizeof( s_errorFileName ) );
    remove( s_errorFileName );
}

/* MapErrorsOccurred  0x00414e40 */
bool MapErrorsOccurred( void )
{
    return s_mapErrorOccurred;
}
