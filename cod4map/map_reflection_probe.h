
#ifndef MAP_REFLECTION_PROBE_H
#define MAP_REFLECTION_PROBE_H

#include "q_shared.h"
#include "bspfile.h"
#include "map.h"
#include "surface.h"


#define REFLECTION_PROBE_INVALID    0xff

#define REFLECTION_PROBE_NONE       0

#define MAX_COLOR_CORRECTION_NAME   64

#define REFLECTION_PROBE_TEXELS     0x7ffe


extern BspReflectionProbe_t *s_savedProbes;         /* 0x123a9488 */
extern int                   s_savedProbeCount;     /* 0x123a9490 */

extern int                   reflectionDebug;     /* 0x123a948c */

extern int                   s_ignorePortals;       /* 0x123a9494 */

extern char                  s_defaultColorCorrection[MAX_COLOR_CORRECTION_NAME]; /* 0x123a9498 */

extern int                   s_cellVoteCount[MAX_MAP_CELLS];  /* 0x123a94d8 */


void ReflectionProbe_ParseEntity( const Entity_t *ent );            /* 0x0041e950 */

void ReflectionProbe_ParseColorCorrection( const Entity_t *ent );   /* 0x0041e810 */
void ReflectionProbe_ParseIgnorePortals( const Entity_t *ent );     /* 0x0041e8f0 */

void AssignReflectionProbesToCells( Tree_t *tree );                 /* 0x0041d8c0 */

void AssignReflectionProbesToDrawSurfs( Tree_t *tree, int firstSurf ); /* 0x0041e0f0 */

void SaveReflectionProbes( void );                                  /* 0x0041e780 */
void FreeSavedReflectionProbes( void );                             /* 0x0041e7e0 */

#endif
