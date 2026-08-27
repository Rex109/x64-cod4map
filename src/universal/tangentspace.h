/* Original: ..\src\universal\tangentspace.cpp */

#ifndef TANGENTSPACE_H
#define TANGENTSPACE_H

#include "q_shared.h"

#define MAX_TANGENT_SPACE_VERTS  0x10000

typedef struct
{
    char *pos;                  /* +0x00 */
    char *normal;               /* +0x04 */
    char *uv;                   /* +0x08 */
    char *tangent;              /* +0x0c */
    char *bitangent;            /* +0x10 */
    int   posStride;            /* +0x14 */
    int   normalStride;         /* +0x18 */
    int   uvStride;             /* +0x1c */
    int   tangentStride;        /* +0x20 */
    int   bitangentStride;      /* +0x24 */
} TangentSources_t;

void TangentSpaceCalcTBForTriangle( const vec3_t pos0, const vec3_t pos1, const vec3_t pos2,
                                    const vec2_t uv0,  const vec2_t uv1,  const vec2_t uv2,
                                    vec3_t tangent, vec3_t bitangent );   /* 0x0043a950 */

void TangentSpaceGenerate( const TangentSources_t *sources, int vertCount,
                           const unsigned short *indices, int indexCount );
                                                                          /* 0x0043ab70 */

#endif
