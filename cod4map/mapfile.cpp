/* Original: .\mapfile.cpp */

#include <map>
#include <string>

#include "cod4map.h"
#include "mapfile.h"


mapFlagToken_t contentsTokens[] =                        /* 0x004fa19c */
{
    { "weaponClip",   CONTENTS_DETAIL | CONTENTS_CLIPSHOT | CONTENTS_MISSILECLIP },
    { "nonColliding", CONTENTS_DETAIL | CONTENTS_NONCOLLIDING },
    { "detail",       CONTENTS_DETAIL },
    { NULL,           0 }
};

mapFlagToken_t toolFlagsTokens[] =                       /* 0x004fa1c8 */
{
    { "splitGeo",     TOOLFLAG_SPLITGEO },
    { NULL,           0 }
};


static int s_prefabNameCounts[PREFAB_NAME_COUNTER_COUNT];      /* 0x123aa4d8 */

static std::map< std::pair< int, std::string >, std::string >
       s_prefabNames;                                          /* 0x123aa8d8 */


/* ParseNamedString  0x0041ebf0 */
void ParseNamedString( const char **parsePos, const char *keyword,
                       const char *defaultValue, char *out )
{
    const char *token;

    token = COM_Parse( parsePos );

    if ( strcmp( token, keyword ) )
    {
        Com_UngetToken();
        if ( out )
        {
            strcpy( out, defaultValue );
        }
        return;
    }

    token = COM_ParseExt( parsePos );

    if ( !out )
    {
        return;
    }

    if ( strlen( token ) )
    {
        strcpy( out, token );
    }
    else
    {
        strcpy( out, defaultValue );
    }
}


/* ParseSmoothingHardDefault  0x0041ec80 */
void ParseSmoothingHardDefault( const char **parsePos, char *out )
{
    ParseNamedString( parsePos, MAP_SMOOTHING_KEYWORD, MAP_SMOOTHING_HARD, out );
}


/* ParseSmoothingSmoothDefault  0x0041eca0 */
void ParseSmoothingSmoothDefault( const char **parsePos, char *out )
{
    ParseNamedString( parsePos, MAP_SMOOTHING_KEYWORD, MAP_SMOOTHING_SMOOTH, out );
}


/* ParseLayerName  0x0041ecc0 */
void ParseLayerName( const char **parsePos, char *out )
{
    ParseNamedString( parsePos, "layer", MAP_DEFAULT_LAYER, out );
}


/* SkipLayerList  0x0041ece0 */
void SkipLayerList( const char **parsePos )
{
    const char *token;

    for ( ;; )
    {
        token = COM_Parse( parsePos );

        if ( !*parsePos || *token == '{' || !token )
        {
            break;
        }

        COM_ParseExt( parsePos );
    }

    Com_UngetToken();
}


/* ParseContentsFlags  0x0041ed30 */
int ParseContentsFlags( const char **parsePos )
{
    return ParseFlagTokens( parsePos, "contents", contentsTokens );
}


/* ParseFlagTokens  0x0041ed50 */
int ParseFlagTokens( const char **parsePos, const char *keyword,
                     const mapFlagToken_t *table )
{
    const char *token;
    int         flags;
    int         i;

    token = COM_Parse( parsePos );

    if ( strcmp( token, keyword ) )
    {
        Com_UngetToken();
        return 0;
    }

    flags = 0;

    for ( ;; )
    {
        token = COM_ParseExt( parsePos );

        if ( !*token )
        {
            Com_Error( "missing token for '%s'", keyword );
        }

        if ( *token == ';' )
        {
            break;
        }

        for ( i = 0; ; i++ )
        {
            if ( !table[i].name )
            {
                Com_Error( "'%s' is not a known token for '%s'", token, keyword );
            }

            if ( !strcmp( token, table[i].name ) )
            {
                break;
            }
        }

        flags |= table[i].flags;
    }

    return flags;
}


/* ParseToolFlags  0x0041ee30 */
int ParseToolFlags( const char **parsePos )
{
    return ParseFlagTokens( parsePos, "toolFlags", toolFlagsTokens );
}


/* Map_BeginParseSession  0x0041ee50 */
void Map_BeginParseSession( const char *name )
{
    Com_BeginParseSession( name );
    Com_SetSpaceDelimited( 0 );
    Com_SetParseNegativeNumbers( 1 );
}


/* Map_EndParseSession  0x0041ee80 */
void Map_EndParseSession( void )
{
    Com_EndParseSession();
}


/* NextPrefabName  0x0041f080 */
static void NextPrefabName( const char *name, char *firstChar, int *index )
{
    *firstChar = *name;
    *index     = s_prefabNameCounts[ *firstChar ];
    s_prefabNameCounts[ *firstChar ] = s_prefabNameCounts[ *firstChar ] + 1;
}


/* RenumberPrefabKeyList  0x0041ee90 */
void RenumberPrefabKeyList( epair_t *epairs, int prefabIndex, const char *key )
{
    const char *value;
    char       *copy;
    char       *token;
    char        newList[1024];
    char        newName[32];
    char        firstChar;
    int         index;

    value = ValueForKeyInPairs( epairs, key );

    if ( !*value )
    {
        return;
    }

    copy       = _strdup( value );
    newList[0] = 0;

    for ( token = strtok( copy, " " ); token; token = strtok( NULL, " " ) )
    {
        std::pair< int, std::string > mapKey;
        std::map< std::pair< int, std::string >, std::string >::const_iterator it;

        mapKey.first  = prefabIndex;
        mapKey.second = token;

        it = s_prefabNames.find( mapKey );

        if ( it == s_prefabNames.end() )
        {
            NextPrefabName( token, &firstChar, &index );
            _snprintf( newName, sizeof( newName ), "%c%i ", firstChar, index );
            s_prefabNames[ mapKey ] = newName;
            strcat( newList, newName );
        }
        else
        {
            strcat( newList, it->second.c_str() );
        }
    }

    SetKeyValueInPairs( epairs, key, newList );
    free( copy );
}
