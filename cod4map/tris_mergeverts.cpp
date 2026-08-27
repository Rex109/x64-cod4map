/* Original: .\tris_mergeverts.cpp */

#include "tris_mergeverts.h"
#include "tris_mergeconcave.h"
#include "com_math.h"
#include "com_vector.h"
#include "cmdlib.h"
#include "assertive.h"

#include <string.h>

#define VERT_HASH_SIZE  0x4000

static int s_vertHashTable[VERT_HASH_SIZE];     /* 0x2b8109d0, 0x10000 */


/* HashVertIndices  0x00459d50 */
static int HashVertIndices( int x, int y, int z )
{
    return ( x * 0x9e33 + y * 0x61c7 + z * 0x3c71 ) & ( VERT_HASH_SIZE - 1 );
}


/* HashVert  0x00459d80 */
static int HashVert( float x, float y, float z )
{
    int ix = RoundFloatToInt( x );
    int iy = RoundFloatToInt( y );
    int iz = RoundFloatToInt( z );

    return HashVertIndices( ix, iy, iz );
}


/* VertAtIndex  0x00459de0 */
static const float *VertAtIndex( const float *baseVert, int vertIndex, unsigned int vertStride )
{
    return (const float *)( (const char *)baseVert + vertIndex * vertStride );
}


/* NextVert  0x00459df0 */
static const float *NextVert( const float *vert, unsigned int vertStride )
{
    return (const float *)( (const char *)vert + vertStride );
}


/* FreeMergeMap  0x00459e00 */
void FreeMergeMap( int *indexMap )
{
    delete [] indexMap;
}


/* BuildMergeMap  0x00459970 */
int *BuildMergeMap( const float *baseVert, int coordCount, unsigned int vertStride,
                    int vertCount, float epsilon )
{
    int         *indexMap;
    int         *chain;
    const float *vert;
    const float *otherVert;
    int          vertIndex;
    int          otherIndex;
    int          hash;
    int          minCell[3];
    int          maxCell[3];
    int          x, y, z;

    Assertx( epsilon < 0.5f, "(epsilon) = %g", epsilon );
    Assert( baseVert );
    Assertx( coordCount == 2 || coordCount == 3, "(coordCount) = %i", coordCount );
    Assertx( vertStride >= sizeof( baseVert[0] ) * coordCount,
             "(vertStride) = %i", vertStride );
    Assertx( vertCount >= 0, "(vertCount) = %i", vertCount );

    memset( s_vertHashTable, -1, sizeof( s_vertHashTable ) );

    indexMap = new int[vertCount];
    chain    = new int[vertCount];

    vert = baseVert;
    for ( vertIndex = 0; vertIndex < vertCount; vertIndex++ )
    {
        if ( coordCount == 3 )
            hash = HashVert( vert[0], vert[1], vert[2] );
        else
            hash = HashVert( vert[0], vert[1], 0.0f );

        chain[vertIndex]      = s_vertHashTable[hash];
        s_vertHashTable[hash] = vertIndex;
        indexMap[vertIndex]   = vertIndex;

        minCell[0] = RoundFloatToInt( vert[0] - epsilon );
        maxCell[0] = RoundFloatToInt( vert[0] + epsilon );
        minCell[1] = RoundFloatToInt( vert[1] - epsilon );
        maxCell[1] = RoundFloatToInt( vert[1] + epsilon );
        if ( coordCount == 3 )
        {
            minCell[2] = RoundFloatToInt( vert[2] - epsilon );
            maxCell[2] = RoundFloatToInt( vert[2] + epsilon );
        }
        else
        {
            minCell[2] = 0;
            maxCell[2] = 0;
        }

        for ( z = minCell[2]; z <= maxCell[2]; z++ )
        {
            for ( y = minCell[1]; y <= maxCell[1]; y++ )
            {
                for ( x = minCell[0]; x <= maxCell[0]; x++ )
                {
                    hash = HashVertIndices( x, y, z );

                    otherIndex = s_vertHashTable[hash];
                    if ( otherIndex == vertIndex )
                        otherIndex = chain[otherIndex];

                    for ( ; otherIndex >= 0; otherIndex = chain[otherIndex] )
                    {
                        otherVert = VertAtIndex( baseVert, otherIndex, vertStride );
                        if ( VecNCompareEpsilon( otherVert, vert, epsilon, coordCount ) )
                            indexMap[vertIndex] = indexMap[otherIndex];
                    }
                }
            }
        }

        vert = NextVert( vert, vertStride );
    }

    delete [] chain;
    return indexMap;
}


/* ApplyMergeMap  0x00459e20 */
void ApplyMergeMap( float *baseVert, unsigned int vertStride, unsigned int vertCount,
                    int firstVertIndex, const int *indexMap )
{
    vec3_t *sum;
    int    *addendCount;
    float  *vert;
    int     curVertIndex;
    int     mapped;

    sum         = new vec3_t[vertCount];
    memset( sum, 0, vertCount * sizeof( sum[0] ) );
    addendCount = new int[vertCount];
    memset( addendCount, 0, vertCount * sizeof( addendCount[0] ) );

    vert = (float *)VertAtIndex( baseVert, firstVertIndex, vertStride );
    for ( curVertIndex = firstVertIndex; curVertIndex < (int)vertCount; curVertIndex++ )
    {
        mapped = indexMap[curVertIndex];
        Vec3Add( vert, sum[mapped], sum[mapped] );
        addendCount[mapped]++;
        vert = (float *)NextVert( vert, vertStride );
    }

    vert = (float *)VertAtIndex( baseVert, firstVertIndex, vertStride );
    for ( curVertIndex = firstVertIndex; curVertIndex < (int)vertCount; curVertIndex++ )
    {
        Assert( indexMap[curVertIndex] <= curVertIndex );

        if ( indexMap[curVertIndex] == curVertIndex )
        {
            Assert( addendCount[curVertIndex] );

            Vec3Scale( sum[curVertIndex], 1.0f / addendCount[curVertIndex], sum[curVertIndex] );

            sum[curVertIndex][0] = RoundFloatToInt( sum[curVertIndex][0] * 1024.0f ) * 0.0009765625f;
            sum[curVertIndex][1] = RoundFloatToInt( sum[curVertIndex][1] * 1024.0f ) * 0.0009765625f;
            sum[curVertIndex][2] = RoundFloatToInt( sum[curVertIndex][2] * 1024.0f ) * 0.0009765625f;
        }

        Vec3Copy( sum[indexMap[curVertIndex]], vert );
        vert = (float *)NextVert( vert, vertStride );
    }

    delete [] addendCount;
    delete [] sum;
}


/* LoopLength  0x0045a420 */
int LoopLength( int start, int ptCount, const int *indices )
{
    int length;

    Assert( indices[start] != indices[( start + 1 ) % ptCount] );

    for ( length = 2; length <= ptCount / 2; length++ )
    {
        if ( indices[start] == indices[( start + length ) % ptCount] )
            return length;
    }
    return 1;
}


/* PrintWindingIndices  0x0045a4c0 */
void PrintWindingIndices( int ptCount, const int *indices )
{
    (void)ptCount;
    (void)indices;
}


/* FindLoops  0x0045a280 */
int FindLoops( int ptCount, const int *indices, int *loopStart, int *loopLength, int loopLimit )
{
    int start;
    int length;
    int loopCount;

    Assert( ptCount >= 3 );
    Assert( indices );
    Assert( loopStart );
    Assert( loopLength );
    Assertx( loopLimit > 0, "(loopLimit) = %i", loopLimit );

    loopCount = 0;
    for ( start = 0; start < ptCount; start++ )
    {
        length = LoopLength( start, ptCount, indices );

        SanityCheckx( length >= 1 && length <= ptCount - 1,
                      "length not in [1, ptCount - 1]\n\t%i not in [%i, %i]",
                      length, 1, ptCount - 1 );

        if ( length > 1 )
        {
            if ( loopCount == loopLimit )
            {
                PrintWindingIndices( ptCount, indices );
                Com_Error( "More than %i loops in a winding", loopLimit );
            }
            loopStart[loopCount]  = start;
            loopLength[loopCount] = length;
            loopCount++;
        }
    }
    return loopCount;
}


/* LoopsAreIdentical  0x0045a5e0 */
bool LoopsAreIdentical( int ptCount, const int *indices, int start0, int start1, int length )
{
    int i;

    if ( indices[start0] != indices[start1] )
        return false;

    for ( i = 1; i < length; i++ )
    {
        if ( indices[( start0 + i ) % ptCount] != indices[( start1 + i ) % ptCount] )
            return false;
    }
    return true;
}


/* RemoveDuplicateLoops  0x0045a4d0 */
int RemoveDuplicateLoops( int ptCount, int *indices, const int *loopStart,
                          const int *loopLength, int loopCount )
{
    int i;
    int j;
    int start;
    int length;
    int moveCount;

    for ( i = 0; i < loopCount; i++ )
    {
        start  = loopStart[i];
        length = loopLength[i];

        for ( j = i + 1; j < loopCount; j++ )
        {
            if ( length != loopLength[j] )
                continue;
            if ( !LoopsAreIdentical( ptCount, indices, start, loopStart[j], length ) )
                continue;

            if ( start + length > ptCount )
                start = loopStart[j];

            moveCount = ptCount - ( start + length );
            memmove( &indices[start], &indices[start + length], moveCount * sizeof( indices[0] ) );
            return ptCount - length;
        }
    }
    return ptCount;
}


/* RemoveLoops  0x0045a1f0 */
int RemoveLoops( int ptCount, int *indices )
{
    int loopStart[MAX_WINDING_LOOPS];
    int loopLength[MAX_WINDING_LOOPS];
    int loopCount;
    int newPtCount;

    if ( ptCount < 6 )
        return ptCount;

    for ( ;; )
    {
        loopCount = FindLoops( ptCount, indices, loopStart, loopLength, MAX_WINDING_LOOPS );
        if ( loopCount <= 1 )
            return ptCount;

        newPtCount = RemoveDuplicateLoops( ptCount, indices, loopStart, loopLength, loopCount );
        if ( newPtCount == ptCount )
            return ptCount;

        ptCount = newPtCount;
    }
}


/* CollapseLoop  0x0045a120 */
int CollapseLoop( int start, int length, const int *indices, int *outIndices )
{
    int outCount;
    int i;

    while ( length != 0 && indices[start] == indices[start + length - 1] )
        length--;

    outIndices[0] = indices[start];
    outCount = 0;
    for ( i = 1; i < length; i++ )
    {
        if ( indices[start + i] != outIndices[outCount] )
        {
            outCount++;
            outIndices[outCount] = indices[start + i];
        }
    }

    return RemoveLoops( MergeRemoveDegenerateIndices( outCount + 1, outIndices ), outIndices );
}
