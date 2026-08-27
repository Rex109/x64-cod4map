/* Original: .\primarylights.cpp */

#ifndef PRIMARYLIGHTS_H
#define PRIMARYLIGHTS_H

#include "q_shared.h"
#include "bspfile.h"
#include "surface.h"

struct TriSurf_s;


#define GFX_LIGHT_TYPE_DIR      1
#define GFX_LIGHT_TYPE_SPOT     2
#define GFX_LIGHT_TYPE_OMNI     3

#define PRIMARY_LIGHT_INVALID   256

#define PRIMARY_LIGHT_POINT     1
#define PRIMARY_LIGHT_SPOT      2
#define PRIMARY_LIGHT_MOVING    4
#define PRIMARY_NOSHADOWMAP     8

#define PRIMARY_LIGHT_MIN_COS_HALF_FOV  0.499f


typedef struct SurfaceTreeNode_s
{
    vec3_t                    midPoint;    /* +0x00 */
    vec3_t                    halfSize;    /* +0x0c */
    DrawSurf_t               *firstSurf;   /* +0x18 */
    int                       surfCount;   /* +0x1c */
    struct SurfaceTreeNode_s *firstChild;  /* +0x20 */
    int                       childCount;  /* +0x24 */
} SurfaceTreeNode_t;                       /* sizeof == 0x28 */

typedef struct
{
    vec3_t midPoint;    /* +0x00 */
    vec3_t dir;         /* +0x0c */
    vec3_t absDir;      /* +0x18 */
} Segment_t;            /* sizeof == 0x24 */

typedef struct PrimaryLightInfo_s
{
    unsigned int type;          /* +0x00 */
    vec3_t       origin;        /* +0x04 */
    vec3_t       dir;           /* +0x10 */
    float        radius;        /* +0x1c */
    float        cosHalfFov;    /* +0x20 */
} PrimaryLightInfo_t;           /* sizeof == 0x24 */

typedef struct
{
    struct TriSurf_s *surf;                                     /* +0x000 */
    vec3_t            origin;                                   /* +0x004 */
    vec3_t            sVec;                                     /* +0x010 */
    vec3_t            tVec;                                     /* +0x01c */
    byte              lightAffects[MAX_MAP_PRIMARY_LIGHTS + 1]; /* +0x028 */
    DrawSurf_t       *lastOccluder[MAX_MAP_PRIMARY_LIGHTS];     /* +0x128 */
} PrimaryLightTraceCtx_t;                                       /* sizeof == 0x524 */

typedef struct
{
    unsigned int          hullCount;                       /* +0x00 */
    BspLightRegionHull_t *hulls[MAX_HULLS_PER_LIGHT];      /* +0x04 */
} LightRegion_t;                                           /* sizeof == 0x24 */


extern SurfaceTreeNode_t *s_surfaceTree;                       /* 0x12f0f944 */

extern LightRegion_t      s_lightRegion[MAX_MAP_PRIMARY_LIGHTS];   /* 0x12f0f948 */

extern int                s_savedPrimaryLightCount;            /* 0x12f0f940 */
extern BspPrimaryLight_t  s_savedPrimaryLights[MAX_MAP_PRIMARY_LIGHTS]; /* 0x12f11d28 */


void SetupPrimaryLights( void );                            /* 0x00431df0 */

qboolean SunIsPrimaryLight( void );                         /* 0x00432d90 */

void BuildDrawSurfTree( void );                             /* 0x00432df0 */

void AssignPrimaryLightsToSurface( struct TriSurf_s *surf ); /* 0x00433a50 */

void BuildPrimaryLightRegions( void );                      /* 0x00434f40 */

void SavePrimaryLights( void );                             /* 0x00434c20 */
void ComparePrimaryLights( void );                          /* 0x00434c50 */

void PrimaryLight_Error( Entity_t *ent, const vec3_t normal, const char *message );
                                                            /* 0x00432d40 */

float PrimaryLightCosHalfFovExpanded( const BspPrimaryLight_t *light ); /* 0x00434bc0 */

#endif
