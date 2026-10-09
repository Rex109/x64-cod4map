/* Original: .\bsp.cpp */

#ifndef BSP_H
#define BSP_H

#include "q_shared.h"
#include "bspfile.h"

typedef int ( *optionHandler_t )( int argc, const char **argv );

typedef struct
{
    const char      *name;          /* +0x00 */
    const char      *description;   /* +0x04 */
    optionHandler_t  func;          /* +0x08 */
} OptionEntry_t;                    /* sizeof == 0x0c */

extern OptionEntry_t optionsTable[30];      /* 0x004f3c60 */

#define TEST_EXPAND_NONE    0
#define TEST_EXPAND_PLAYER  1
#define TEST_EXPAND_POINT   2

extern int   noCurveBrushes;                    /* 0x00a34bc0 */
extern char  g_convertPath[MAX_OS_PATH];        /* 0x00a34bc8 */
extern int   fulldetail;                        /* 0x00a34fc8 */
extern int   nowater;                           /* 0x00a34fcc */
extern int   splitLightmaps;                    /* -splitLightmaps */
extern int   verboseEntities;                   /* 0x00a34fd0 */
extern int   g_onlyEnts;                        /* 0x00a34fd4 */
extern int   nodetail;                          /* 0x00a34fd8 */
extern int   nosubdivide;                       /* 0x00a34fdc */
extern int   entity_num;                        /* 0x00a34fe4 */
extern char  g_loadFromPath[MAX_OS_PATH];       /* 0x00a34fe8 */
extern char  g_mapSourceFile[MAX_OS_PATH];      /* 0x00a353e8 */
extern char  g_outputBasePath[MAX_OS_PATH];     /* 0x00a357e8 */
extern int   leaktest;                          /* 0x00a35be8 */
extern float blockSize;                         /* 0x00a35bf0 */
extern int   staticModelCollMaps;               /* 0x00a35bf8 */
extern int   displayCollMapWarnings;            /* 0x00a35bfc */
extern int   testExpand;                        /* 0x00a35c00 */
extern float listSlowEntities;                  /* 0x00a35c04 */
extern int   sourcePlatform;                    /* 0x00a35c08 */

extern int   brushMethod;                       /* 0x00529070 */
extern int   reorderTris;                       /* 0x00529074 */
extern float sampleScale;                       /* 0x0052907c */


void  GetOutputFileName( const char *extension, char *out, int outSize );   /* 0x00408a70 */

float ParseSmoothAngle( const char *str, const char *optionName );          /* 0x00408cd0 */

void  PrintUsage( void );                                                   /* 0x00409500 */

void  BSP_LoadReflectionProbes( const char *filename );                     /* 0x0040a320 */

void  BSP_LoadMapFile( const char *mapFile );                               /* 0x00408c30 */

void  BSP_ApplyWorldspawnBlockSize( const Entity_t *ent );                  /* 0x0040a1f0 */

#endif
