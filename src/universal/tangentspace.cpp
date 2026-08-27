/* Original: ..\src\universal\tangentspace.cpp */

#include "q_shared.h"
#include "assertive.h"
#include "com_math.h"
#include "com_vector.h"
#include "tangentspace.h"


static char *MulAdd( char *base, int stride, int index );            /* 0x0043b010 */
static void  TangentSpaceAccumulate( float weight, const vec3_t v,
                                     char *base, int stride, int index );
                                                                     /* 0x0043b020 */
static void  TangentSpaceCalcTBForTriangleFromSources( const TangentSources_t *sources,
                                                       int i0, int i1, int i2,
                                                       vec3_t tangent, vec3_t bitangent );
                                                                     /* 0x0043b060 */
static void  TangentSpaceCalcTriangleAngles( const TangentSources_t *sources,
                                             int i0, int i1, int i2, float angles[3] );
                                                                     /* 0x0043b140 */
static float TangentSpaceAngleBetweenVectors( const vec3_t a, const vec3_t b );
                                                                     /* 0x0043b250 */


/* TangentSpaceCalcTBForTriangle  0x0043a950 */
void TangentSpaceCalcTBForTriangle( const vec3_t pos0, const vec3_t pos1, const vec3_t pos2,
                                    const vec2_t uv0,  const vec2_t uv1,  const vec2_t uv2,
                                    vec3_t tangent, vec3_t bitangent )
{
    float du1, du2;
    float dv1, dv2;
    float dx1, dx2;
    float dy1, dy2;
    float dz1, dz2;

    du1 = uv1[0] - uv0[0];
    du2 = uv2[0] - uv0[0];
    dv1 = uv1[1] - uv0[1];
    dv2 = uv2[1] - uv0[1];

    if ( du1 * dv2 < du2 * dv1 )
    {
        dx1 = pos1[0] - pos0[0];
        dx2 = pos2[0] - pos0[0];
        tangent[0]   = dx2 * dv1 - dx1 * dv2;
        bitangent[0] = dx1 * du2 - dx2 * du1;

        dy1 = pos1[1] - pos0[1];
        dy2 = pos2[1] - pos0[1];
        tangent[1]   = dy2 * dv1 - dy1 * dv2;
        bitangent[1] = dy1 * du2 - dy2 * du1;

        dz1 = pos1[2] - pos0[2];
        dz2 = pos2[2] - pos0[2];
        tangent[2]   = dv1 * dz2 - dv2 * dz1;
        bitangent[2] = du2 * dz1 - du1 * dz2;
    }
    else
    {
        dx1 = pos1[0] - pos0[0];
        dx2 = pos2[0] - pos0[0];
        tangent[0]   = dx1 * dv2 - dx2 * dv1;
        bitangent[0] = dx2 * du1 - dx1 * du2;

        dy1 = pos1[1] - pos0[1];
        dy2 = pos2[1] - pos0[1];
        tangent[1]   = dy1 * dv2 - dy2 * dv1;
        bitangent[1] = dy2 * du1 - dy1 * du2;

        dz1 = pos1[2] - pos0[2];
        dz2 = pos2[2] - pos0[2];
        tangent[2]   = dv2 * dz1 - dv1 * dz2;
        bitangent[2] = du1 * dz2 - du2 * dz1;
    }

    Vec3Normalize( tangent );
    Vec3Normalize( bitangent );
}


/* TangentSpaceAngleBetweenVectors  0x0043b250 */
static float TangentSpaceAngleBetweenVectors( const vec3_t a, const vec3_t b )
{
    float dot;

    dot = Vec3Dot( a, b );

    if ( dot <= -1.0 )
        return -PI;

    if ( dot < 1.0 )
        return acos( dot );

    return PI;
}


/* MulAdd  0x0043b010 */
static char *MulAdd( char *base, int stride, int index )
{
    return index * stride + base;
}


/* TangentSpaceAccumulate  0x0043b020 */
static void TangentSpaceAccumulate( float weight, const vec3_t v, char *base, int stride, int index )
{
    float *p;

    p = (float *)MulAdd( base, stride, index );
    Vec3Mad( p, weight, v, p );
}


/* TangentSpaceCalcTBForTriangleFromSources  0x0043b060 */
static void TangentSpaceCalcTBForTriangleFromSources( const TangentSources_t *sources,
                                                      int i0, int i1, int i2,
                                                      vec3_t tangent, vec3_t bitangent )
{
    const float *uv0, *uv1, *uv2;
    const float *pos0, *pos1, *pos2;

    uv0  = (const float *)MulAdd( sources->uv,  sources->uvStride,  i0 );
    uv1  = (const float *)MulAdd( sources->uv,  sources->uvStride,  i1 );
    uv2  = (const float *)MulAdd( sources->uv,  sources->uvStride,  i2 );
    pos0 = (const float *)MulAdd( sources->pos, sources->posStride, i0 );
    pos1 = (const float *)MulAdd( sources->pos, sources->posStride, i1 );
    pos2 = (const float *)MulAdd( sources->pos, sources->posStride, i2 );

    TangentSpaceCalcTBForTriangle( pos0, pos1, pos2, uv0, uv1, uv2, tangent, bitangent );
}


/* TangentSpaceCalcTriangleAngles  0x0043b140 */
static void TangentSpaceCalcTriangleAngles( const TangentSources_t *sources,
                                            int i0, int i1, int i2, float angles[3] )
{
    const float *v0, *v1, *v2;
    vec3_t       edge01, edge12, edge20;

    v0 = (const float *)MulAdd( sources->pos, sources->posStride, i0 );
    v1 = (const float *)MulAdd( sources->pos, sources->posStride, i1 );
    v2 = (const float *)MulAdd( sources->pos, sources->posStride, i2 );

    Vec3Sub( v0, v1, edge01 );
    Vec3Sub( v1, v2, edge12 );
    Vec3Sub( v2, v0, edge20 );

    Vec3Normalize( edge01 );
    Vec3Normalize( edge12 );
    Vec3Normalize( edge20 );

    angles[0] = TangentSpaceAngleBetweenVectors( edge20, edge01 );
    angles[1] = TangentSpaceAngleBetweenVectors( edge01, edge12 );
    angles[2] = TangentSpaceAngleBetweenVectors( edge12, edge20 );
}


/* TangentSpaceGenerate  0x0043ab70 */
void TangentSpaceGenerate( const TangentSources_t *sources, int vertCount,
                           const unsigned short *indices, int indexCount )
{
    char  *t;
    int    i;
    int    j;
    char  *n;
    vec3_t temp;

    float  vertWeights[MAX_TANGENT_SPACE_VERTS];

    char  *b;
    vec3_t tangent;
    float  angles[3];
    int    i1;
    int    i0;
    int    i2;

    Assert( sources );
    Assert( indices );
    Assert( vertCount <= MAX_TANGENT_SPACE_VERTS );

    for ( i = 0; i < vertCount; i++ )
    {
        Vec3Clear( (float *)MulAdd( sources->tangent,   sources->tangentStride,   i ) );
        Vec3Clear( (float *)MulAdd( sources->bitangent, sources->bitangentStride, i ) );
    }

    memset( vertWeights, 0, vertCount * sizeof( vertWeights[0] ) );

    for ( j = 0; j < indexCount; j += 3 )
    {
        i0 = indices[j];
        i1 = indices[j + 1];
        i2 = indices[j + 2];

        Assert( i0 >= 0 && i0 < vertCount );
        Assert( i1 >= 0 && i1 < vertCount );
        Assert( i2 >= 0 && i2 < vertCount );

        TangentSpaceCalcTBForTriangleFromSources( sources, i0, i1, i2, tangent, temp );
        TangentSpaceCalcTriangleAngles( sources, i0, i1, i2, angles );

        TangentSpaceAccumulate( angles[0], tangent, sources->tangent, sources->tangentStride, i0 );
        TangentSpaceAccumulate( angles[1], tangent, sources->tangent, sources->tangentStride, i1 );
        TangentSpaceAccumulate( angles[2], tangent, sources->tangent, sources->tangentStride, i2 );

        TangentSpaceAccumulate( angles[0], temp, sources->bitangent, sources->bitangentStride, i0 );
        TangentSpaceAccumulate( angles[1], temp, sources->bitangent, sources->bitangentStride, i1 );
        TangentSpaceAccumulate( angles[2], temp, sources->bitangent, sources->bitangentStride, i2 );
    }

    t = sources->tangent;
    b = sources->bitangent;
    n = sources->normal;

    for ( i = 0; i < vertCount; i++ )
    {
        Vec3Mad( (float *)t, -Vec3Dot( (float *)t, (float *)n ), (float *)n, (float *)t );

        if ( Vec3Normalize( (float *)t ) < 0.001 )
        {
            Vec3Cross( (float *)b, (float *)n, (float *)t );
            if ( Vec3Normalize( (float *)t ) < 0.001 )
                PerpendicularVector( (float *)n, (float *)t );
        }

        Vec3Cross( (float *)n, (float *)t, temp );

        if ( Vec3Dot( temp, (float *)b ) < 0.0 )
            Vec3Negate( temp, (float *)b );
        else
            Vec3Copy( temp, (float *)b );

        t += sources->tangentStride;
        b += sources->bitangentStride;
        n += sources->normalStride;
    }
}
