/* Original: ..\common\texturevecs.cpp */

#ifndef TEXTUREVECS_H
#define TEXTUREVECS_H

#include "../src/universal/q_shared.h"

#define NUM_TEXTURE_AXES  6

#define DEFAULT_TEXTURE_SCALE  128.0f


void TexturePlanesFromBasis( const vec3_t normal, vec3_t xv, vec3_t yv );  /* 0x0043b400 */

void TextureVecsForNormal( const vec3_t normal, vec3_t xv, vec3_t yv,
                           int *s, int *t );                              /* 0x0043b4a0 */

void BuildTextureVecs( const vec3_t normal, const vec2_t scale, const vec2_t shift,
                       float rotate, float skew, vec4_t *texVecs );        /* 0x0043bb10 */

void ExtractTextureVecs( const vec3_t planeNormal, float planeDist, vec4_t *texVecs,
                         vec2_t scale, vec2_t shift, float *rotate, float *skew );
                                                                           /* 0x0043b5f0 */

#endif
