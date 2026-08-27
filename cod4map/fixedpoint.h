/* Original: .\fixedpoint.cpp */

#ifndef FIXEDPOINT_H
#define FIXEDPOINT_H

#include "q_shared.h"
#include "polylib.h"


#define FIXED_POINT_SCALE       16384.0
#define FIXED_POINT_INVSCALE    6.103515625e-05

typedef int fixedvec3_t[3];

typedef struct
{
    unsigned int ptCount;    /* +0x00 */
    fixedvec3_t  pts[4];     /* +0x04 */
} FixedWinding_t;

int    FloatToFixed( float value );                                  /* 0x00417c50 */
int    DoubleToFixed( double value );                                /* 0x00417dc0 */
float  FixedToFloat( int value );                                    /* 0x00417cd0 */

int    FixedLerpToPlane( int a, int b, double distA, double distB );  /* 0x00417df0 */

void   Vec3ToFixed( const vec3_t from, fixedvec3_t to );             /* 0x00417c00 */
void   FixedToVec3( const fixedvec3_t from, vec3_t to );             /* 0x00417c80 */
void   FixedVec3Copy( const fixedvec3_t from, fixedvec3_t to );      /* 0x00417d90 */
void   FixedVec3Sub( const fixedvec3_t v0, const fixedvec3_t v1,
                     fixedvec3_t out );                              /* 0x00417d50 */

double FixedPointPlaneDist( const fixedvec3_t pt, const vec3_t planeNormal,
                            double planeDist );                      /* 0x00417cf0 */

void   FixedTriangleNormal( const fixedvec3_t a, const fixedvec3_t b,
                            const fixedvec3_t c, vec3_t out );       /* 0x00417b90 */

FixedWinding_t *AllocFixedWinding( unsigned int ptCount );           /* 0x00416560 */
void            FreeFixedWinding( FixedWinding_t *fw );              /* 0x004165d0 */
FixedWinding_t *CopyFixedWinding( const FixedWinding_t *fw );        /* 0x004165f0 */
FixedWinding_t *FixedWindingFromWinding( const winding_t *w );       /* 0x00416650 */
winding_t      *WindingFromFixedWinding( const FixedWinding_t *fw ); /* 0x004166e0 */

void   FixedWindingCenter( const FixedWinding_t *fw, fixedvec3_t center ); /* 0x00416770 */
void   FixedWindingBounds( const FixedWinding_t *fw, fixedvec3_t bounds[2] ); /* 0x00416de0 */

double FixedWindingMaxAbsDist( const FixedWinding_t *fw, const vec3_t planeNormal,
                               double planeDist );                   /* 0x004168b0 */
void   FixedWindingDistRange( const FixedWinding_t *fw, const vec3_t planeNormal,
                              double planeDist, double range[2] );   /* 0x00416930 */

bool   FixedEdgePlane( const fixedvec3_t p0, const fixedvec3_t p1,
                       const vec3_t planeNormal, vec3_t outNormal,
                       double *outDist );                            /* 0x004169d0 */

bool   FixedWindingIsReversed( const FixedWinding_t *fw,
                               const vec3_t planeNormal );           /* 0x00417ae0 */

bool   FixedWindingsAreDisjoint( const FixedWinding_t *a, const FixedWinding_t *b,
                                 const vec3_t planeNormal, double epsilon ); /* 0x00416c00 */
bool   FixedWindingsOverlap( const FixedWinding_t *a, const FixedWinding_t *b,
                             const vec3_t planeNormal, double epsilon );     /* 0x00416d70 */

void ClearFixedBounds( fixedvec3_t bounds[2] );                      /* 0x00416fb0 */
void AddFixedBoundsToBounds( const fixedvec3_t src[2], fixedvec3_t dst[2] ); /* 0x00416f10 */

qboolean FixedBoundsInsideBounds( const fixedvec3_t inner[2],
                                  const fixedvec3_t outer[2], int epsilon ); /* 0x00416ff0 */
qboolean FixedBoundsOverlap( const fixedvec3_t b0[2], const fixedvec3_t b1[2],
                             int epsilon );                          /* 0x004170e0 */

void ClipFixedWindingEpsilon( const FixedWinding_t *in, const FixedWinding_t *other,
                              const vec3_t planeNormal, double planeDist,
                              double epsilon,
                              FixedWinding_t **front, FixedWinding_t **back ); /* 0x004171d0 */

#endif
