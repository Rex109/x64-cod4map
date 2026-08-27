/* Original: ..\common\primarylights_region.cpp */

#ifndef PRIMARYLIGHTS_REGION_H
#define PRIMARYLIGHTS_REGION_H

#include "q_shared.h"
#include "com_math.h"
#include "com_vector.h"
#include "polylib.h"
#include "bspfile.h"

struct PrimaryLightInfo_s;


#define HULL_KDOP_AXIS_COUNT        9

#define HULL_CANDIDATE_DIR_LIMIT    0x400

#define HULL_POINT_LIMIT_TOTAL      0x1540

#define HULL_POINT_LIMIT_FACE       0x88

#define CONVEX_HULL_POINT_LIMIT     0x40

#define KDOP_NORMAL_COUNT           26

#define HULL_AXIS_MERGE_COS         0.98f

#define HULL_AXIS_LIMIT             0x11

#define HULL_KDOP_DIAGONAL_SCALE    ( ( double )0.70710677f )

#define FACE_CONE_GOOD_ENOUGH_COS   0.98f
#define FACE_CONE_STEP              0.0625f
#define FACE_CONE_EPSILON           0.0001f
#define FACE_CONE_MAX_ITERATIONS    10000

typedef struct LightRegionFace_s
{
    vec4_t                    plane;        /* +0x00 */
    vec3_t                    dir;          /* +0x10 */
    float                     cosHalfFov;   /* +0x1c */
    int                       twoSided;     /* +0x20 */
    winding_t                *w;            /* +0x24 */
    const void               *ds;           /* +0x28 */
    struct LightRegionFace_s *next;         /* +0x2c */
} LightRegionFace_t;


extern const vec3_t s_kdopNormals[KDOP_NORMAL_COUNT];


void LightRegion_AddFace( void **list, const struct PrimaryLightInfo_s *light,
                          const winding_t *w, qboolean twoSided,
                          const void *ds );                             /* 0x004360a0 */

void LightRegion_BuildFaces( void **list, const struct PrimaryLightInfo_s *light );
                                                                        /* 0x004367f0 */

unsigned int LightRegion_BuildHulls( void **list, const struct PrimaryLightInfo_s *light,
                                     unsigned int hullLimit,
                                     BspLightRegionHull_t **hullsOut ); /* 0x00436de0 */

void LightRegion_FreeBuilder( void *list );                             /* 0x00436030 */

inline float CosOfAngleSum( float cosA, float cosB )
{
    float sinProduct;

    sinProduct = ( 1.0f - cosA * cosA ) * ( 1.0f - cosB * cosB );

    return cosA * cosB - I_sqrt( sinProduct );
}

#endif
