/* Original: ..\common\texturevecs.cpp */

#include "../src/universal/q_shared.h"
#include "../src/universal/assertive.h"
#include "../src/universal/com_math.h"
#include "../src/universal/com_vector.h"
#include "texturevecs.h"

/* s_textureAxes  0x004ffe88 */
static const struct
{
    vec3_t normal;
    vec3_t xv;
    vec3_t yv;
}
s_textureAxes[NUM_TEXTURE_AXES] =
{
     { {  0,  0,  1 }, { 1, 0, 0 }, { 0, -1,  0 } },
     { {  0,  0, -1 }, { 1, 0, 0 }, { 0, -1,  0 } },
     { {  1,  0,  0 }, { 0, 1, 0 }, { 0,  0, -1 } },
     { { -1,  0,  0 }, { 0, 1, 0 }, { 0,  0, -1 } },
     { {  0,  1,  0 }, { 1, 0, 0 }, { 0,  0, -1 } },
     { {  0, -1,  0 }, { 1, 0, 0 }, { 0,  0, -1 } }
};

/* TexturePlanesFromBasis  0x0043b400 */
void TexturePlanesFromBasis( const vec3_t normal, vec3_t xv, vec3_t yv )
{
    float        dot;
    unsigned int i;
    float        bestDot;
    int          bestAxis;

    bestDot  = 0.0f;
    bestAxis = 0;

    for ( i = 0; i < NUM_TEXTURE_AXES; i++ )
    {
        dot = Vec3Dot( normal, s_textureAxes[i].normal );
        if ( dot > bestDot )
        {
            bestDot  = dot;
            bestAxis = i;
        }
    }

    Vec3Copy( s_textureAxes[bestAxis].xv, xv );
    Vec3Copy( s_textureAxes[bestAxis].yv, yv );
}

/* TextureVecsForNormal  0x0043b4a0 */
void TextureVecsForNormal( const vec3_t normal, vec3_t xv, vec3_t yv, int *s, int *t )
{
    Assert( normal );
    Assert( xv );
    Assert( yv );
    Assert( s );
    Assert( t );

    TexturePlanesFromBasis( normal, xv, yv );

    if ( xv[0] != 0.0 )
        *s = 0;
    else if ( xv[1] != 0.0 )
        *s = 1;
    else
        *s = 2;

    if ( yv[0] != 0.0 )
        *t = 0;
    else if ( yv[1] != 0.0 )
        *t = 1;
    else
        *t = 2;
}

/* ExtractTextureVecs  0x0043b5f0 */
void ExtractTextureVecs( const vec3_t planeNormal, float planeDist, vec4_t *texVecs,
                         vec2_t scale, vec2_t shift, float *rotate, float *skew )
{
    int   i;
    int   s;
    float d;
    float pvecs[2][3];
    int   r;
    int   t;

    TextureVecsForNormal( planeNormal, pvecs[0], pvecs[1], &s, &t );

    SanityCheckx( s != t, "(s) = %i", s );

    r = ( 3 - s ) - t;

    SanityCheckx( pvecs[0][s] == 1 || pvecs[0][s] == -1,
                  "(pvecs[0][s]) = %g", pvecs[0][s] );
    SanityCheckx( pvecs[0][t] == 0, "(pvecs[0][t]) = %g", pvecs[0][t] );
    SanityCheckx( pvecs[0][r] == 0, "(pvecs[0][r]) = %g", pvecs[0][r] );

    SanityCheckx( pvecs[1][s] == 0, "(pvecs[1][s]) = %g", pvecs[1][s] );
    SanityCheckx( pvecs[1][t] == 1 || pvecs[1][t] == -1,
                  "(pvecs[1][t]) = %g", pvecs[1][t] );
    SanityCheckx( pvecs[1][r] == 0, "(pvecs[1][r]) = %g", pvecs[1][r] );

    SanityCheck( planeNormal[r] != 0 );

    for ( i = 0; i < 2; i++ )
    {
        if ( texVecs[i][r] != 0.0 )
        {
            d = texVecs[i][r] / planeNormal[r];
            Vec3Mad( texVecs[i], -d, planeNormal, texVecs[i] );
            texVecs[i][3] = planeDist * d + texVecs[i][3];
        }
    }

    *skew = Vec3Dot( texVecs[0], texVecs[1] ) / Vec3LengthSq( texVecs[1] );
    *skew = SnapToGrid( *skew, 1000.0f, 0.00001f );
    Vec3Mad( texVecs[0], -*skew, texVecs[1], texVecs[0] );

    scale[0] = 1.0f / Vec3Length( texVecs[0] );
    scale[1] = 1.0f / Vec3Length( texVecs[1] );

    if ( texVecs[0][s] * pvecs[0][s] * texVecs[1][t] * pvecs[1][t]
       - texVecs[0][t] * pvecs[0][s] * texVecs[1][s] * pvecs[1][t] < 0.0 )
    {
        scale[1] = -scale[1];
    }

    scale[0] = SnapToGrid( scale[0], 8.0f, 0.001f );
    scale[1] = SnapToGrid( scale[1], 8.0f, 0.001f );

    *rotate = ( float )atan2( texVecs[0][t] * pvecs[0][s], texVecs[0][s] * pvecs[0][s] ) * RAD2DEG;
    *rotate = SnapToGrid( *rotate, 4.0f, 0.005f );

    shift[0] = -texVecs[0][3] * scale[0];
    shift[1] = -texVecs[1][3] * scale[1];

    shift[0] = SnapToGrid( shift[0], 8.0f, 0.001f );
    shift[1] = SnapToGrid( shift[1], 8.0f, 0.001f );
}

/* BuildTextureVecs  0x0043bb10 */
void BuildTextureVecs( const vec3_t normal, const vec2_t scale, const vec2_t shift,
                       float rotate, float skew, vec4_t *texVecs )
{
    int    t;
    int    r;
    float  sinVal;
    float  pvecs[2][3];
    int    s;
    vec2_t texScale;
    float  cosVal;

    Vec2Copy( scale, texScale );

    if ( texScale[0] == 0.0 )
        texScale[0] = DEFAULT_TEXTURE_SCALE;
    if ( texScale[1] == 0.0 )
        texScale[1] = DEFAULT_TEXTURE_SCALE;

    TextureVecsForNormal( normal, pvecs[0], pvecs[1], &s, &t );

    SanityCheckx( s != t, "(s) = %i", s );

    r = ( 3 - s ) - t;

    SanityCheckx( pvecs[0][s] == 1 || pvecs[0][s] == -1,
                  "(pvecs[0][s]) = %g", pvecs[0][s] );
    SanityCheckx( pvecs[0][t] == 0, "(pvecs[0][t]) = %g", pvecs[0][t] );
    SanityCheckx( pvecs[0][r] == 0, "(pvecs[0][r]) = %g", pvecs[0][r] );

    SanityCheckx( pvecs[1][s] == 0, "(pvecs[1][s]) = %g", pvecs[1][s] );
    SanityCheckx( pvecs[1][t] == 1 || pvecs[1][t] == -1,
                  "(pvecs[1][t]) = %g", pvecs[1][t] );
    SanityCheckx( pvecs[1][r] == 0, "(pvecs[1][r]) = %g", pvecs[1][r] );

    SinCosDegrees( rotate, &sinVal, &cosVal );

    texVecs[0][s] = pvecs[0][s] / texScale[0] * cosVal;
    texVecs[0][t] = pvecs[0][s] / texScale[0] * sinVal;
    texVecs[0][r] = 0;

    texVecs[1][s] = -sinVal * ( pvecs[1][t] / texScale[1] );
    texVecs[1][t] = pvecs[1][t] / texScale[1] * cosVal;
    texVecs[1][r] = 0;

    Vec3Mad( texVecs[0], skew, texVecs[1], texVecs[0] );

    texVecs[0][3] = -shift[0] / texScale[0];
    texVecs[1][3] = -shift[1] / texScale[1];
}
