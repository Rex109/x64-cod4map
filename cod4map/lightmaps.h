
#ifndef LIGHTMAPS_H
#define LIGHTMAPS_H

#include "q_shared.h"

#define LIGHTMAP_SIZE           512

#define MIN_LIGHTMAP_BLOCK      3

typedef struct lmapBlock_s
{
    int                 lightmapNum;   /* +0x00 */
    int                 x;             /* +0x04 */
    int                 y;             /* +0x08 */
    int                 width;         /* +0x0c */
    int                 height;        /* +0x10 */
    struct lmapBlock_s *next;          /* +0x14 */
    struct lmapBlock_s *prev;          /* +0x18 */
} lmapBlock_t;

typedef struct lmapList_s
{
    struct lmapList_s *next;           /* +0x00 */
    int                lightmapNum;    /* +0x04 */
} lmapList_t;

extern lmapList_t  *g_preferredLightmaps;    /* 0x11ba8424 */
extern int          numLightmaps;            /* 0x11ba8428 */
extern byte         debugLightmaps;          /* 0x11ba842c */
extern lmapBlock_t *g_lmapBlocks;            /* 0x11ba8430 */


void AddLightmapBlock( int lightmapNum, int x, int y, int width, int height ); /* 0x00418510 */

qboolean AllocLightmapRegion( int width, int height, int *lightmapNum,
                              int *x, int *y, int *rotated );          /* 0x004185e0 */

void AllocNewLightmap( void );                                         /* 0x00418d80 */

void AddPreferredLightmap( int lightmapNum );                          /* 0x00419040 */
int  FreePreferredLightmaps( void );                                   /* 0x004190a0 */

int  LightmapCoordFloor( float lmapCoord );                            /* 0x004190f0 */
int  LightmapCoordCeil( float lmapCoord );                             /* 0x00419120 */
int  LightmapSizeForRange( float lmapMin, float lmapMax );             /* 0x00419150 */

#endif
