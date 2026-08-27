/* Original: .\tris_combinelayers.cpp */

#ifndef TRIS_COMBINELAYERS_H
#define TRIS_COMBINELAYERS_H

#include "q_shared.h"
#include "tris.h"

#define MAX_TRI_DESC_GROUPS     0x4c80
#define MAX_TRI_DESC_PAIRS      0x800

#define MAX_PAIRABLE_TRI_COUNT  0x40

#define MOVE_TRI_BLOCK          0x400

typedef struct
{
    int firstTriIndex;      /* +0x00 */
    int triCount;           /* +0x04 */
} triDescGroup_t;

typedef struct
{
    short triDescIndex[2];  /* +0x00, +0x02 */
    byte  active;           /* +0x04 */
} triDescPair_t;

void Tris_CombineLayeredMaterials( Tri_t *tris, int triCount, TriVert_t *verts,
                                   const int *vertMap );            /* 0x0044f250 */

#endif
