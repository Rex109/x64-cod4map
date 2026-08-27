/* Original: .\tris_combinelayers.cpp */

#include "tris_combinelayers.h"

#include "cod4map.h"
#include "tris.h"
#include "materials.h"

#include <stdlib.h>
#include <string.h>
#include "qsort_vc8.h"


void UnpackVertexColor( const byte *color, byte rgba[4] );                      /* 0x00450270 */


/* UnpackVertexColor  0x00450270 */
void UnpackVertexColor( const byte *color, byte rgba[4] )
{
    Vec4bSet( rgba, color[2], color[1], color[0], color[3] );
}

static const triDescGroup_t *s_sortTriDescGroups;   /* 0x2b80e310 */


/* TriDescEqual  0x00450210 */
static bool TriDescEqual( const Tri_t *tri0, const Tri_t *tri1 )
{
    if ( tri0->lyrMtlDesc != tri1->lyrMtlDesc )
        return false;

    if ( tri0->lmapIndex != tri1->lmapIndex )
        return false;

    if ( tri0->reflectionProbeIndex != tri1->reflectionProbeIndex )
        return false;

    if ( tri0->primaryLightIndex != tri1->primaryLightIndex )
        return false;

    return true;
}


/* CanCombineTriDescs  0x0044f690 */
static bool CanCombineTriDescs( const Tri_t *tri0, const Tri_t *tri1 )
{
    const LayeredMaterialDesc_t *lyrMtlDescLo;
    const LayeredMaterialDesc_t *lyrMtlDescHi;
    int shift;
    int layerIndex;

    if ( tri0->reflectionProbeIndex != tri1->reflectionProbeIndex )
        return false;

    if ( tri0->primaryLightIndex != tri1->primaryLightIndex )
        return false;

    if ( tri0->lmapIndex != tri1->lmapIndex )
        return false;

    if ( tri1->lyrMtlDesc->layerCount < tri0->lyrMtlDesc->layerCount )
    {
        lyrMtlDescLo = tri1->lyrMtlDesc;
        lyrMtlDescHi = tri0->lyrMtlDesc;
    }
    else
    {
        lyrMtlDescLo = tri0->lyrMtlDesc;
        lyrMtlDescHi = tri1->lyrMtlDesc;
    }

    if ( lyrMtlDescLo->layerCount != lyrMtlDescHi->layerCount - 1 )
        return false;

    if ( lyrMtlDescLo->normalMapCount != lyrMtlDescHi->normalMapCount )
        return false;

    if ( lyrMtlDescLo->mtlRaw[0] != lyrMtlDescHi->mtlRaw[0] )
        return false;

    shift = 0;

    for ( layerIndex = 1; layerIndex < lyrMtlDescLo->layerCount; layerIndex++ )
    {
        if ( lyrMtlDescLo->mtlRaw[layerIndex] != lyrMtlDescHi->mtlRaw[layerIndex + shift] )
        {
            if ( shift )
                return false;

            shift = 1;

            if ( lyrMtlDescLo->mtlRaw[layerIndex] != lyrMtlDescHi->mtlRaw[layerIndex + 1] )
                return false;
        }
    }

    return true;
}


/* FindCombinableTriDescPairs  0x0044f560 */
static int FindCombinableTriDescPairs( const Tri_t *tris, const triDescGroup_t *triDescGroups,
                                       int triDescCount, triDescPair_t *pairs, int maxPairs )
{
    int pairCount;
    int i;
    int j;

    pairCount = 0;

    for ( i = 1; i < triDescCount; i++ )
    {
        const Tri_t *triI = &tris[ triDescGroups[i].firstTriIndex ];

        for ( j = 0; j < i && triDescGroups[j].triCount < MAX_PAIRABLE_TRI_COUNT; j++ )
        {
            const Tri_t *triJ = &tris[ triDescGroups[j].firstTriIndex ];

            if ( !CanCombineTriDescs( triI, triJ ) )
                continue;

            if ( triI->lyrMtlDesc->layerCount < triJ->lyrMtlDesc->layerCount )
            {
                pairs[pairCount].triDescIndex[0] = ( short )i;
                pairs[pairCount].triDescIndex[1] = ( short )j;
            }
            else
            {
                pairs[pairCount].triDescIndex[0] = ( short )j;
                pairs[pairCount].triDescIndex[1] = ( short )i;
            }

            pairs[pairCount].active = 1;
            pairCount++;

            if ( pairCount == maxPairs )
                return pairCount;
        }
    }

    return pairCount;
}


/* CompareTriDescPairs  0x0044f4f0 */
static int CompareTriDescPairs( const void *va, const void *vb )
{
    const triDescPair_t *a = ( const triDescPair_t * )va;
    const triDescPair_t *b = ( const triDescPair_t * )vb;

    return ( s_sortTriDescGroups[ a->triDescIndex[0] ].triCount +
             s_sortTriDescGroups[ a->triDescIndex[1] ].triCount ) -
           ( s_sortTriDescGroups[ b->triDescIndex[0] ].triCount +
             s_sortTriDescGroups[ b->triDescIndex[1] ].triCount );
}


/* CountAdjacencyRun  0x0044fcd0 */
static int CountAdjacencyRun( const Tri_t *tris, int triIndex, int triCount )
{
    int runLength;

    runLength = 1;

    while ( runLength < triCount &&
            tris[triIndex + runLength].triGroupId == tris[triIndex].triGroupId )
    {
        runLength++;
    }

    return runLength;
}


/* PromoteTrisToLayeredMaterial  0x0044fd20 */
static void PromoteTrisToLayeredMaterial( TriVert_t *verts, Tri_t *tris, int triCount,
                                          LayeredMaterialDesc_t *dstLyrMtlDesc )
{
    byte    weights[4];
    size_t  moveSize;
    int     layerIndex;
    int     triIndex;
    int     cornerIndex;
    int     weightIndexStart;
    int     weightIndexStop;
    int     weightIndex;

    Assert( dstLyrMtlDesc->layerCount == tris[0].lyrMtlDesc->layerCount + 1 );
    Assert( dstLyrMtlDesc->normalMapCount == tris[0].lyrMtlDesc->normalMapCount );
    Assert( dstLyrMtlDesc->mtlRaw[0] == tris[0].lyrMtlDesc->mtlRaw[0] );

    for ( layerIndex = 1;
          layerIndex < tris[0].lyrMtlDesc->layerCount &&
              dstLyrMtlDesc->mtlRaw[layerIndex] == tris[0].lyrMtlDesc->mtlRaw[layerIndex];
          layerIndex++ )
    {
    }

    weightIndexStart = dstLyrMtlDesc->layerCount - 1;
    weightIndexStop  = layerIndex;

    if ( dstLyrMtlDesc->layerCount > 4 )
    {
        weightIndexStart -= dstLyrMtlDesc->layerCount - 4;
        weightIndexStop  -= dstLyrMtlDesc->layerCount - 4;
    }

    Assert( weightIndexStart >= weightIndexStop );

    moveSize = ( dstLyrMtlDesc->layerCount - layerIndex ) * sizeof( vec2_t ) - sizeof( vec2_t );

    for ( triIndex = 0; triIndex < triCount; triIndex++ )
    {
        for ( cornerIndex = 0; cornerIndex < 3; cornerIndex++ )
        {
            TriVert_t *vert = &verts[ tris[triIndex].vertIndices[cornerIndex] ];

            memmove( vert->texCoord[layerIndex + 1], vert->texCoord[layerIndex], moveSize );
            Vec2Clear( vert->texCoord[layerIndex] );

            if ( dstLyrMtlDesc->layerCount == 2 )
            {
                SetVertexColor( vert->color, vert->color[3], 0, 255, 255 );
            }
            else
            {
                UnpackVertexColor( vert->color, weights );

                for ( weightIndex = weightIndexStart; weightIndex != weightIndexStop; weightIndex-- )
                    weights[weightIndex] = weights[weightIndex - 1];

                weights[weightIndex] = 0;

                PackVertexColor( weights, vert->color );
            }
        }

        tris[triIndex].lyrMtlDesc = dstLyrMtlDesc;
    }
}


/* MoveTriRun  0x0044ffb0 */
static void MoveTriRun( Tri_t *tris, int src, int dst, int count )
{
    Tri_t temp[MOVE_TRI_BLOCK];
    int   blockCount;

    do
    {
        blockCount = I_min( count, MOVE_TRI_BLOCK );

        memcpy( temp, &tris[src], blockCount * sizeof( Tri_t ) );

        if ( src < dst )
        {
            memmove( &tris[src], &tris[src + blockCount],
                     ( dst - ( src + blockCount ) ) * sizeof( Tri_t ) );
            memcpy( &tris[dst - blockCount], temp, blockCount * sizeof( Tri_t ) );
        }
        else
        {
            memmove( &tris[dst + blockCount], &tris[dst],
                     ( src - dst ) * sizeof( Tri_t ) );
            memcpy( &tris[dst], temp, blockCount * sizeof( Tri_t ) );

            src += blockCount;
            dst += blockCount;
        }

        count -= blockCount;
    }
    while ( count != 0 );
}


/* RemoveTriDescGroup  0x004500c0 */
static void RemoveTriDescGroup( triDescGroup_t *triDescGroups, triDescPair_t *pairs,
                                int triDescIndex, int *pairCount, int *triDescCount )
{
    int i;
    int keptCount;

    ( *triDescCount )--;

    for ( i = triDescIndex; i < *triDescCount; i++ )
    {
        triDescGroups[i].firstTriIndex = triDescGroups[i + 1].firstTriIndex;
        triDescGroups[i].triCount      = triDescGroups[i + 1].triCount;
    }

    keptCount = 0;

    for ( i = 0; i < *pairCount; i++ )
    {
        if ( pairs[i].triDescIndex[0] == triDescIndex || pairs[i].triDescIndex[1] == triDescIndex )
            continue;

        pairs[keptCount].triDescIndex[0] = pairs[i].triDescIndex[0];
        if ( pairs[keptCount].triDescIndex[0] > triDescIndex )
            pairs[keptCount].triDescIndex[0]--;

        pairs[keptCount].triDescIndex[1] = pairs[i].triDescIndex[1];
        if ( pairs[keptCount].triDescIndex[1] > triDescIndex )
            pairs[keptCount].triDescIndex[1]--;

        keptCount++;
    }

    *pairCount = keptCount;
}


/* CombineTriDescPair  0x0044f7e0 */
static bool CombineTriDescPair( TriVert_t *verts, Tri_t *tris, triDescGroup_t *triDescGroups,
                                triDescPair_t *pairs, int pairIndex,
                                int *pairCount, int *triDescCount )
{
    triDescPair_t *activePair;
    LayeredMaterialDesc_t *dstLyrMtlDesc;
    bool combinedAny;
    int  triIndex[2];
    int  triCount[2];
    int  runLength[2];
    int  totalShift;
    int  shift;
    int  shiftB;
    int  i;
    int  firstGroup;
    int  lastGroup;
    int  groupIndex;

    activePair = &pairs[pairIndex];

    triIndex[0] = triDescGroups[ activePair->triDescIndex[0] ].firstTriIndex;
    triIndex[1] = triDescGroups[ activePair->triDescIndex[1] ].firstTriIndex;
    triCount[0] = triDescGroups[ activePair->triDescIndex[0] ].triCount;
    triCount[1] = triDescGroups[ activePair->triDescIndex[1] ].triCount;

    dstLyrMtlDesc = tris[ triIndex[1] ].lyrMtlDesc;

    AssertCmp( tris[triIndex[0]].lyrMtlDesc->layerCount + 1, ==, dstLyrMtlDesc->layerCount );

    combinedAny = false;
    totalShift  = 0;

    do
    {
        unsigned short group0 = tris[ triIndex[0] ].triGroupId;
        unsigned short group1 = tris[ triIndex[1] ].triGroupId;

        if ( group0 < group1 )
        {
            runLength[0] = CountAdjacencyRun( tris, triIndex[0], triCount[0] );
            triIndex[0] += runLength[0];
            triCount[0] -= runLength[0];
        }
        else if ( group1 < group0 )
        {
            runLength[0] = CountAdjacencyRun( tris, triIndex[1], triCount[1] );
            triIndex[1] += runLength[0];
            triCount[1] -= runLength[0];
        }
        else
        {
            combinedAny = true;

            runLength[0] = CountAdjacencyRun( tris, triIndex[0], triCount[0] );
            runLength[1] = CountAdjacencyRun( tris, triIndex[1], triCount[1] );

            PromoteTrisToLayeredMaterial( verts, &tris[ triIndex[0] ], runLength[0], dstLyrMtlDesc );
            MoveTriRun( tris, triIndex[0], triIndex[1] + runLength[1], runLength[0] );

            triDescGroups[ activePair->triDescIndex[0] ].triCount -= runLength[0];
            triDescGroups[ activePair->triDescIndex[1] ].triCount += runLength[0];

            if ( triIndex[0] < triIndex[1] )
            {
                SanityCheckx( totalShift <= 0, "(totalShift) = %i", totalShift );

                shiftB = runLength[1];
                shift  = -runLength[0];
            }
            else
            {
                SanityCheckx( totalShift >= 0, "(totalShift) = %i", totalShift );

                triIndex[0] += runLength[0];

                shiftB = runLength[0] + runLength[1];
                shift  = runLength[0];
            }

            totalShift  += shift;
            triIndex[1] += shiftB;
            triCount[0] -= runLength[0];
            triCount[1] -= runLength[1];
        }
    }
    while ( triCount[0] != 0 && triCount[1] != 0 );

    if ( !combinedAny )
        return false;

    for ( i = 0; i < *pairCount; i++ )
    {
        if ( pairs[i].triDescIndex[0] == activePair->triDescIndex[1] ||
             pairs[i].triDescIndex[1] == activePair->triDescIndex[1] )
        {
            pairs[i].active = 1;
        }
    }

    if ( totalShift < 0 )
    {
        AssertCmp( activePair->triDescIndex[0], <, activePair->triDescIndex[1] );
        firstGroup = activePair->triDescIndex[0];
        lastGroup  = activePair->triDescIndex[1];
    }
    else
    {
        AssertCmp( activePair->triDescIndex[0], >, activePair->triDescIndex[1] );
        firstGroup = activePair->triDescIndex[1];
        lastGroup  = activePair->triDescIndex[0];
    }

    for ( groupIndex = firstGroup + 1; groupIndex <= lastGroup; groupIndex++ )
        triDescGroups[groupIndex].firstTriIndex += totalShift;

    SanityCheckCmp( triIndex[0] + triCount[0], ==,
                    triDescGroups[ activePair->triDescIndex[0] ].firstTriIndex +
                    triDescGroups[ activePair->triDescIndex[0] ].triCount );
    SanityCheckCmp( triIndex[1] + triCount[1], ==,
                    triDescGroups[ activePair->triDescIndex[1] ].firstTriIndex +
                    triDescGroups[ activePair->triDescIndex[1] ].triCount );

    if ( triDescGroups[ activePair->triDescIndex[0] ].triCount == 0 )
    {
        RemoveTriDescGroup( triDescGroups, pairs, activePair->triDescIndex[0],
                            pairCount, triDescCount );
    }

    return true;
}


/* Tris_CombineLayeredMaterials  0x0044f250 */
void Tris_CombineLayeredMaterials( Tri_t *tris, int triCount, TriVert_t *verts,
                                   const int *vertMap )
{
    triDescGroup_t triDescGroups[MAX_TRI_DESC_GROUPS];
    triDescPair_t  pairs[MAX_TRI_DESC_PAIRS];
    Tri_t         *triLists[2];
    int            triCounts[2];
    int            triDescCount;
    int            pairCount;
    int            firstTriIndex;
    int            triIndex;
    int            pairIndex;

    triDescCount  = 0;
    firstTriIndex = 0;

    while ( firstTriIndex < triCount )
    {
        for ( triIndex = firstTriIndex + 1; triIndex < triCount; triIndex++ )
        {
            if ( !TriDescEqual( &tris[firstTriIndex], &tris[triIndex] ) )
                break;
        }

        if ( triDescCount == MAX_TRI_DESC_GROUPS )
            Com_Error( "Too many material / reflection probe pairs when optimizing layered materials\n" );

        triDescGroups[triDescCount].firstTriIndex = firstTriIndex;
        triDescGroups[triDescCount].triCount      = triIndex - firstTriIndex;
        triDescCount++;

        firstTriIndex = triIndex;
    }

    if ( triDescCount == 1 )
        return;

    pairCount = FindCombinableTriDescPairs( tris, triDescGroups, triDescCount,
                                            pairs, MAX_TRI_DESC_PAIRS );

restart:
    if ( pairCount == 0 )
        return;

    s_sortTriDescGroups = triDescGroups;
    qsort_vc8( pairs, pairCount, sizeof( pairs[0] ), CompareTriDescPairs );
    s_sortTriDescGroups = NULL;

    for ( pairIndex = 0; pairIndex < pairCount; pairIndex++ )
    {
        if ( !pairs[pairIndex].active )
            continue;

        triLists[0]  = &tris[ triDescGroups[ pairs[pairIndex].triDescIndex[0] ].firstTriIndex ];
        triLists[1]  = &tris[ triDescGroups[ pairs[pairIndex].triDescIndex[1] ].firstTriIndex ];
        triCounts[0] = triDescGroups[ pairs[pairIndex].triDescIndex[0] ].triCount;
        triCounts[1] = triDescGroups[ pairs[pairIndex].triDescIndex[1] ].triCount;

        GroupTriSurfsIntoSubgroups( triLists, triCounts, 2, vertMap );

        if ( CombineTriDescPair( verts, tris, triDescGroups, pairs, pairIndex,
                                 &pairCount, &triDescCount ) )
        {
            goto restart;
        }
    }
}
