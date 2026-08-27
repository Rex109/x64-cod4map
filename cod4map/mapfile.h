/* Original: .\mapfile.cpp */

#ifndef MAPFILE_H
#define MAPFILE_H

#include "q_shared.h"
#include "bspfile.h"

typedef struct
{
    const char *name;
    int         flags;
} mapFlagToken_t;

extern mapFlagToken_t contentsTokens[];    /* 0x004fa19c */
extern mapFlagToken_t toolFlagsTokens[];   /* 0x004fa1c8 */

#define TOOLFLAG_SPLITGEO   0x00000100

#define MAP_DEFAULT_LAYER            "000_Global"
#define MAP_SMOOTHING_KEYWORD        "smoothing"
#define MAP_SMOOTHING_HARD           "smoothing_hard"
#define MAP_SMOOTHING_SMOOTH         "smoothing_smooth"

#define PREFAB_NAME_COUNTER_COUNT    256


void ParseNamedString( const char **parsePos, const char *keyword,
                       const char *defaultValue, char *out );      /* 0x0041ebf0 */

void ParseSmoothingHardDefault( const char **parsePos, char *out );   /* 0x0041ec80 */
void ParseSmoothingSmoothDefault( const char **parsePos, char *out ); /* 0x0041eca0 */
void ParseLayerName( const char **parsePos, char *out );              /* 0x0041ecc0 */

void SkipLayerList( const char **parsePos );                       /* 0x0041ece0 */

int  ParseFlagTokens( const char **parsePos, const char *keyword,
                      const mapFlagToken_t *table );               /* 0x0041ed50 */
int  ParseContentsFlags( const char **parsePos );                  /* 0x0041ed30 */
int  ParseToolFlags( const char **parsePos );                      /* 0x0041ee30 */

void Map_BeginParseSession( const char *name );                    /* 0x0041ee50 */
void Map_EndParseSession( void );                                  /* 0x0041ee80 */

void RenumberPrefabKeyList( epair_t *epairs, int prefabIndex,
                            const char *key );                     /* 0x0041ee90 */

#endif
