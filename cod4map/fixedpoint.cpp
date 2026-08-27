/* Original: .\fixedpoint.cpp */

#include "fixedpoint.h"

#include "cod4map.h"

/* FloatToFixed  0x00417c50 */
int FloatToFixed( float value )
{
    float scaled;

    scaled = value * FIXED_POINT_SCALE + 0.5;
    return ( int )floor( scaled );
}


/* DoubleToFixed  0x00417dc0 */
int DoubleToFixed( double value )
{
    return ( int )floor( value * FIXED_POINT_SCALE + 0.5 );
}


/* FixedToFloat  0x00417cd0 */
float FixedToFloat( int value )
{
    float f;

    f = value * FIXED_POINT_INVSCALE;
    return f;
}


/* FixedLerpToPlane  0x00417df0 */
int FixedLerpToPlane( int a, int b, double distA, double distB )
{
    double t;

    t = ( ( double )a * distB - ( double )b * distA ) / ( distB - distA );
    return ( int )floor( t + 0.5 );
}


/* Vec3ToFixed  0x00417c00 */
void Vec3ToFixed( const vec3_t from, fixedvec3_t to )
{
    to[0] = FloatToFixed( from[0] );
    to[1] = FloatToFixed( from[1] );
    to[2] = FloatToFixed( from[2] );
}


/* FixedToVec3  0x00417c80 */
void FixedToVec3( const fixedvec3_t from, vec3_t to )
{
    to[0] = FixedToFloat( from[0] );
    to[1] = FixedToFloat( from[1] );
    to[2] = FixedToFloat( from[2] );
}


/* FixedVec3Copy  0x00417d90 */
void FixedVec3Copy( const fixedvec3_t from, fixedvec3_t to )
{
    to[0] = from[0];
    to[1] = from[1];
    to[2] = from[2];
}


/* FixedVec3Sub  0x00417d50 */
void FixedVec3Sub( const fixedvec3_t v0, const fixedvec3_t v1, fixedvec3_t out )
{
    out[0] = v0[0] - v1[0];
    out[1] = v0[1] - v1[1];
    out[2] = v0[2] - v1[2];
}


/* FixedPointPlaneDist  0x00417cf0 */
double FixedPointPlaneDist( const fixedvec3_t pt, const vec3_t planeNormal,
                            double planeDist )
{
    double dist;

    dist = 0.0;
    dist += ( double )pt[0] * planeNormal[0];
    dist += ( double )pt[1] * planeNormal[1];
    dist += ( double )pt[2] * planeNormal[2];
    dist *= FIXED_POINT_INVSCALE;

    return dist - planeDist;
}


/* FixedTriangleNormal  0x00417b90 */
void FixedTriangleNormal( const fixedvec3_t a, const fixedvec3_t b,
                          const fixedvec3_t c, vec3_t out )
{
    fixedvec3_t d1i;
    fixedvec3_t d2i;
    vec3_t      d1;
    vec3_t      d2;

    FixedVec3Sub( b, a, d1i );
    FixedVec3Sub( c, a, d2i );

    FixedToVec3( d1i, d1 );
    FixedToVec3( d2i, d2 );

    Vec3Cross( d2, d1, out );
}


/* FixedWindingIsReversed  0x00417ae0 */
bool FixedWindingIsReversed( const FixedWinding_t *fw, const vec3_t planeNormal )
{
    vec3_t       cross;
    float        total;
    unsigned int i;

    total = 0.0f;

    for ( i = 2; i < fw->ptCount; i++ )
    {
        FixedTriangleNormal( fw->pts[i - 2], fw->pts[i - 1], fw->pts[i], cross );
        total += Vec3Dot( cross, planeNormal );
    }

    return total < 0.0;
}


/* AllocFixedWinding  0x00416560 */
FixedWinding_t *AllocFixedWinding( unsigned int ptCount )
{
    FixedWinding_t *fw;

    Assertx( ptCount >= 3, "(ptCount) = %i", ptCount );

    fw = ( FixedWinding_t * )malloc( ptCount * sizeof( fixedvec3_t ) + sizeof( int ) );
    if ( !fw )
        Com_Error( "out of memory: FixedWinding\n" );

    return fw;
}


/* FreeFixedWinding  0x004165d0 */
void FreeFixedWinding( FixedWinding_t *fw )
{
    free( fw );
}


/* CopyFixedWinding  0x004165f0 */
FixedWinding_t *CopyFixedWinding( const FixedWinding_t *fw )
{
    FixedWinding_t *copy;

    Assert( fw );

    copy = AllocFixedWinding( fw->ptCount );
    memcpy( copy, fw, fw->ptCount * sizeof( fixedvec3_t ) + sizeof( int ) );

    return copy;
}


/* FixedWindingFromWinding  0x00416650 */
FixedWinding_t *FixedWindingFromWinding( const winding_t *w )
{
    FixedWinding_t *fw;
    unsigned int    i;

    Assert( w );

    fw = AllocFixedWinding( w->ptCount );
    fw->ptCount = w->ptCount;

    for ( i = 0; i < fw->ptCount; i++ )
        Vec3ToFixed( w->pts[i], fw->pts[i] );

    return fw;
}


/* WindingFromFixedWinding  0x004166e0 */
winding_t *WindingFromFixedWinding( const FixedWinding_t *fw )
{
    winding_t   *w;
    unsigned int i;

    Assert( fw );

    w = AllocWinding( fw->ptCount );
    w->ptCount = fw->ptCount;

    for ( i = 0; i < w->ptCount; i++ )
        FixedToVec3( fw->pts[i], w->pts[i] );

    return w;
}


/* FixedWindingCenter  0x00416770 */
void FixedWindingCenter( const FixedWinding_t *fw, fixedvec3_t center )
{
    __int64      sum[3];
    unsigned int i;

    sum[0] = fw->pts[0][0];
    sum[1] = fw->pts[0][1];
    sum[2] = fw->pts[0][2];

    for ( i = 1; i < fw->ptCount; i++ )
    {
        sum[0] += fw->pts[i][0];
        sum[1] += fw->pts[i][1];
        sum[2] += fw->pts[i][2];
    }

    center[0] = ( int )( ( sum[0] + sum[0] % fw->ptCount ) / fw->ptCount );
    center[1] = ( int )( ( sum[1] + sum[1] % fw->ptCount ) / fw->ptCount );
    center[2] = ( int )( ( sum[2] + sum[2] % fw->ptCount ) / fw->ptCount );
}


/* FixedWindingMaxAbsDist  0x004168b0 */
double FixedWindingMaxAbsDist( const FixedWinding_t *fw, const vec3_t planeNormal,
                               double planeDist )
{
    double       maxDist;
    double       dist;
    unsigned int i;

    maxDist = 0.0;

    for ( i = 0; i < fw->ptCount; i++ )
    {
        dist = fabs( FixedPointPlaneDist( fw->pts[i], planeNormal, planeDist ) );
        if ( maxDist < dist )
            maxDist = dist;
    }

    return maxDist;
}


/* FixedWindingDistRange  0x00416930 */
void FixedWindingDistRange( const FixedWinding_t *fw, const vec3_t planeNormal,
                            double planeDist, double range[2] )
{
    double       dist;
    unsigned int i;

    range[0] =  1.7976931348623157e+308;
    range[1] = -1.7976931348623157e+308;

    for ( i = 0; i < fw->ptCount; i++ )
    {
        dist = FixedPointPlaneDist( fw->pts[i], planeNormal, planeDist );

        if ( dist < range[0] )
            range[0] = dist;

        if ( range[1] < dist )
            range[1] = dist;
    }
}


/* FixedEdgePlane  0x004169d0 */
bool FixedEdgePlane( const fixedvec3_t p0, const fixedvec3_t p1,
                     const vec3_t planeNormal, vec3_t outNormal, double *outDist )
{
    fixedvec3_t delta;
    double      cross[3];
    double      lengthSq;
    double      invLength;
    bool        valid;

    FixedVec3Sub( p1, p0, delta );

    cross[0] = ( double )delta[2] * planeNormal[1] - ( double )delta[1] * planeNormal[2];
    cross[1] = ( double )delta[0] * planeNormal[2] - ( double )delta[2] * planeNormal[0];
    cross[2] = ( double )delta[1] * planeNormal[0] - ( double )delta[0] * planeNormal[1];

    lengthSq = cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2];

    valid = ( lengthSq != 0.0 );

    if ( valid )
    {
        invLength = 1.0 / sqrt( lengthSq );

        outNormal[0] = ( float )( cross[0] * invLength );
        outNormal[1] = ( float )( cross[1] * invLength );
        outNormal[2] = ( float )( cross[2] * invLength );
    }
    else
    {
        outNormal[0] = 0.0f;
        outNormal[1] = 0.0f;
        outNormal[2] = 0.0f;
    }

    *outDist = ( ( double )p0[0] * outNormal[0]
               + ( double )p0[1] * outNormal[1]
               + ( double )p0[2] * outNormal[2] ) * FIXED_POINT_INVSCALE;

    return valid;
}


/* FixedWindingOutsideEdgesOf  0x00416b00 */
static bool FixedWindingOutsideEdgesOf( const FixedWinding_t *edges,
                                        const FixedWinding_t *test,
                                        const vec3_t planeNormal, double epsilon )
{
    vec3_t       edgeNormal;
    double       edgeDist;
    double       range[2];
    double       smallEps;
    double       maxAbsDist;
    unsigned int i;

    smallEps = epsilon * 0.01;

    for ( i = 0; i < edges->ptCount; i++ )
    {
        FixedEdgePlane( edges->pts[i], edges->pts[( i + 1 ) % edges->ptCount],
                        planeNormal, edgeNormal, &edgeDist );

        FixedWindingDistRange( test, edgeNormal, edgeDist, range );

        if ( -smallEps <= range[0] )
            return true;

        if ( -epsilon < range[0] && range[0] * -3.0 < range[1] )
        {
            maxAbsDist = FixedWindingMaxAbsDist( edges, edgeNormal, edgeDist );

            if ( maxAbsDist * -0.25 <= range[0] )
                return true;
        }
    }

    return false;
}


/* FixedWindingsAreDisjoint  0x00416c00 */
bool FixedWindingsAreDisjoint( const FixedWinding_t *a, const FixedWinding_t *b,
                               const vec3_t planeNormal, double epsilon )
{
    if ( !FixedWindingOutsideEdgesOf( a, b, planeNormal, epsilon )
      && !FixedWindingOutsideEdgesOf( b, a, planeNormal, epsilon ) )
    {
        return false;
    }

    return true;
}


/* FixedWindingInsideEdgesOf  0x00416c70 */
static bool FixedWindingInsideEdgesOf( const FixedWinding_t *edges,
                                       const FixedWinding_t *test,
                                       const vec3_t planeNormal, double epsilon )
{
    vec3_t       edgeNormal;
    double       edgeDist;
    double       range[2];
    double       smallEps;
    double       maxAbsDist;
    unsigned int i;

    smallEps = epsilon * 0.01;

    for ( i = 0; i < edges->ptCount; i++ )
    {
        FixedEdgePlane( edges->pts[i], edges->pts[( i + 1 ) % edges->ptCount],
                        planeNormal, edgeNormal, &edgeDist );

        FixedWindingDistRange( test, edgeNormal, edgeDist, range );

        if ( range[1] <= smallEps )
            return false;

        if ( range[1] < epsilon && range[0] < range[1] * -3.0 )
        {
            maxAbsDist = FixedWindingMaxAbsDist( edges, edgeNormal, edgeDist );

            if ( range[1] < maxAbsDist * 0.25 )
                return false;
        }
    }

    return true;
}


/* FixedWindingsOverlap  0x00416d70 */
bool FixedWindingsOverlap( const FixedWinding_t *a, const FixedWinding_t *b,
                           const vec3_t planeNormal, double epsilon )
{
    if ( FixedWindingInsideEdgesOf( a, b, planeNormal, epsilon )
      && FixedWindingInsideEdgesOf( b, a, planeNormal, epsilon ) )
    {
        return true;
    }

    return false;
}


/* FixedWindingBounds  0x00416de0 */
void FixedWindingBounds( const FixedWinding_t *fw, fixedvec3_t bounds[2] )
{
    unsigned int i;
    unsigned int axis;

    Assert( fw );
    Assertx( fw->ptCount > 0, "(fw->ptCount) = %i", fw->ptCount );

    FixedVec3Copy( fw->pts[0], bounds[0] );
    FixedVec3Copy( fw->pts[0], bounds[1] );

    for ( i = 1; i < fw->ptCount; i++ )
    {
        for ( axis = 0; axis < 3; axis++ )
        {
            if ( fw->pts[i][axis] < bounds[0][axis] )
                bounds[0][axis] = fw->pts[i][axis];
            else if ( bounds[1][axis] < fw->pts[i][axis] )
                bounds[1][axis] = fw->pts[i][axis];
        }
    }
}


/* AddFixedBoundsToBounds  0x00416f10 */
void AddFixedBoundsToBounds( const fixedvec3_t src[2], fixedvec3_t dst[2] )
{
    if ( src[0][0] < dst[0][0] )
        dst[0][0] = src[0][0];
    if ( dst[1][0] < src[1][0] )
        dst[1][0] = src[1][0];

    if ( src[0][1] < dst[0][1] )
        dst[0][1] = src[0][1];
    if ( dst[1][1] < src[1][1] )
        dst[1][1] = src[1][1];

    if ( src[0][2] < dst[0][2] )
        dst[0][2] = src[0][2];
    if ( dst[1][2] < src[1][2] )
        dst[1][2] = src[1][2];
}


/* ClearFixedBounds  0x00416fb0 */
void ClearFixedBounds( fixedvec3_t bounds[2] )
{
    bounds[0][0] = 0x7fffffff;
    bounds[0][1] = 0x7fffffff;
    bounds[0][2] = 0x7fffffff;
    bounds[1][0] = ( int )0x80000000;
    bounds[1][1] = ( int )0x80000000;
    bounds[1][2] = ( int )0x80000000;
}


/* FixedBoundsInsideBounds  0x00416ff0 */
qboolean FixedBoundsInsideBounds( const fixedvec3_t inner[2], const fixedvec3_t outer[2],
                                  int epsilon )
{
    if ( inner[0][0] < outer[0][0] + epsilon || outer[0][0] + epsilon < outer[0][0] )
        return qfalse;

    if ( outer[1][0] - epsilon < inner[1][0] || outer[1][0] < outer[1][0] - epsilon )
        return qfalse;

    if ( inner[0][1] < outer[0][1] + epsilon || outer[0][1] + epsilon < outer[0][1] )
        return qfalse;

    if ( outer[1][1] - epsilon < inner[1][1] || outer[1][1] < outer[1][1] - epsilon )
        return qfalse;

    if ( inner[0][2] < outer[0][2] + epsilon || outer[0][2] + epsilon < outer[0][2] )
        return qfalse;

    if ( outer[1][2] - epsilon < inner[1][2] || outer[1][2] < outer[1][2] - epsilon )
        return qfalse;

    return qtrue;
}


/* FixedBoundsOverlap  0x004170e0 */
qboolean FixedBoundsOverlap( const fixedvec3_t b0[2], const fixedvec3_t b1[2],
                             int epsilon )
{
    if ( b1[1][0] + epsilon < b0[0][0] && b1[1][0] < b1[1][0] + epsilon )
        return qfalse;

    if ( b0[1][0] + epsilon < b1[0][0] && b0[1][0] < b0[1][0] + epsilon )
        return qfalse;

    if ( b1[1][1] + epsilon < b0[0][1] && b1[1][1] < b1[1][1] + epsilon )
        return qfalse;

    if ( b0[1][1] + epsilon < b1[0][1] && b0[1][1] < b0[1][1] + epsilon )
        return qfalse;

    if ( b1[1][2] + epsilon < b0[0][2] && b1[1][2] < b1[1][2] + epsilon )
        return qfalse;

    if ( b0[1][2] + epsilon < b1[0][2] && b0[1][2] < b0[1][2] + epsilon )
        return qfalse;

    return qtrue;
}


/* ClipFixedWindingEpsilon  0x004171d0 */
void ClipFixedWindingEpsilon( const FixedWinding_t *in, const FixedWinding_t *other,
                              const vec3_t planeNormal, double planeDist,
                              double epsilon,
                              FixedWinding_t **front, FixedWinding_t **back )
{
    double          dists[MAX_POINTS_ON_WINDING + WINDING_CLIP_SLACK];
    int             sides[MAX_POINTS_ON_WINDING + WINDING_CLIP_SLACK];
    int             counts[3];
    double          range[2];
    double          smallEps;
    double          maxAbsDist;
    double          limit;
    bool            exactAxialPlane;
    int             axialPlaneAxis;
    int             axialPlaneValue;
    unsigned int    maxPts;
    FixedWinding_t *f;
    FixedWinding_t *b;
    const int      *p1;
    const int      *p2;
    fixedvec3_t     mid;
    unsigned int    i;
    unsigned int    axis;

    Assert( in );
    Assert( planeNormal );
    Assert( Vec3LengthSq( planeNormal ) > 0 );
    Assert( front );
    Assert( back );

    *front = NULL;
    *back  = NULL;

    smallEps = epsilon * 0.01;

    FixedWindingDistRange( in, planeNormal, planeDist, range );

    if ( !( smallEps < range[1] ) )
    {
        *back = CopyFixedWinding( in );
        return;
    }

    if ( -smallEps <= range[0] )
    {
        *front = CopyFixedWinding( in );
        return;
    }

    if ( range[1] < epsilon && range[0] < range[1] * -3.0 )
    {
        maxAbsDist = FixedWindingMaxAbsDist( other, planeNormal, planeDist );
        limit = maxAbsDist * 0.25;

        if ( range[1] <= limit )
        {
            *back = CopyFixedWinding( in );
            return;
        }

        if ( limit < epsilon )
            epsilon = limit;
    }

    if ( -epsilon < range[0] && range[0] * -3.0 < range[1] )
    {
        maxAbsDist = FixedWindingMaxAbsDist( other, planeNormal, planeDist );
        limit = maxAbsDist * 0.25;

        if ( -limit < range[0] )
        {
            *front = CopyFixedWinding( in );
            return;
        }

        if ( limit < epsilon )
            epsilon = limit;
    }

    limit = ( range[1] - range[0] ) * 0.125;
    if ( limit < epsilon )
        epsilon = limit;

    counts[SIDE_FRONT] = 0;
    counts[SIDE_BACK]  = 0;
    counts[SIDE_ON]    = 0;

    for ( i = 0; i < in->ptCount; i++ )
    {
        dists[i] = FixedPointPlaneDist( in->pts[i], planeNormal, planeDist );

        if ( epsilon <= dists[i] )
            sides[i] = SIDE_FRONT;
        else if ( -epsilon < dists[i] )
            sides[i] = SIDE_ON;
        else
            sides[i] = SIDE_BACK;

        counts[sides[i]]++;
    }

    sides[i] = sides[0];
    dists[i] = dists[0];

    Assert( counts[SIDE_FRONT] );
    Assert( counts[SIDE_BACK] );

    exactAxialPlane = true;
    axialPlaneAxis  = -1;
    axialPlaneValue = 0;

    for ( axis = 0; axis < 3; axis++ )
    {
        if ( planeNormal[axis] == 1.0 )
        {
            axialPlaneAxis  = axis;
            axialPlaneValue = DoubleToFixed( planeDist );
        }
        else if ( planeNormal[axis] == -1.0 )
        {
            axialPlaneAxis  = axis;
            axialPlaneValue = DoubleToFixed( -planeDist );
        }
        else if ( planeNormal[axis] != 0.0 )
        {
            exactAxialPlane = false;
            break;
        }
    }

    Assertx( !exactAxialPlane || ( axialPlaneAxis >= 0 && axialPlaneAxis < 3 ),
             "(axialPlaneAxis) = %i", axialPlaneAxis );

    maxPts = in->ptCount + WINDING_CLIP_SLACK;

    f = AllocFixedWinding( maxPts );
    b = AllocFixedWinding( maxPts );

    f->ptCount = 0;
    b->ptCount = 0;

    for ( i = 0; i < in->ptCount; i++ )
    {
        p1 = in->pts[i];

        if ( sides[i] == SIDE_ON )
        {
            FixedVec3Copy( p1, f->pts[f->ptCount] );
            FixedVec3Copy( p1, b->pts[b->ptCount] );
            f->ptCount++;
            b->ptCount++;
            continue;
        }

        if ( sides[i] == SIDE_FRONT )
        {
            FixedVec3Copy( p1, f->pts[f->ptCount] );
            f->ptCount++;
        }

        if ( sides[i] == SIDE_BACK )
        {
            FixedVec3Copy( p1, b->pts[b->ptCount] );
            b->ptCount++;
        }

        if ( sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i] )
            continue;

        p2 = in->pts[( i + 1 ) % in->ptCount];

        for ( axis = 0; axis < 3; axis++ )
            mid[axis] = FixedLerpToPlane( p1[axis], p2[axis], dists[i], dists[i + 1] );

        if ( exactAxialPlane )
            mid[axialPlaneAxis] = axialPlaneValue;

        FixedVec3Copy( mid, f->pts[f->ptCount] );
        FixedVec3Copy( mid, b->pts[b->ptCount] );
        f->ptCount++;
        b->ptCount++;
    }

    AssertCmp( f->ptCount, <=, maxPts );
    AssertCmp( b->ptCount, <=, maxPts );
    Assertx( f->ptCount <= MAX_POINTS_ON_WINDING, "(f->ptCount) = %i", f->ptCount );
    Assertx( b->ptCount <= MAX_POINTS_ON_WINDING, "(b->ptCount) = %i", b->ptCount );

    *front = f;
    *back  = b;
}
