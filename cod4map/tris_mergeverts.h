/* Original: .\tris_mergeverts.cpp */

#ifndef TRIS_MERGEVERTS_H
#define TRIS_MERGEVERTS_H

#include "q_shared.h"

#define MAX_WINDING_LOOPS   512

int  *BuildMergeMap( const float *baseVert, int coordCount, unsigned int vertStride,
                     int vertCount, float epsilon );                     /* 0x00459970 */
void  ApplyMergeMap( float *baseVert, unsigned int vertStride, unsigned int vertCount,
                     int firstVertIndex, const int *indexMap );          /* 0x00459e20 */
void  FreeMergeMap( int *indexMap );                                     /* 0x00459e00 */

int CollapseLoop( int start, int length, const int *indices, int *outIndices ); /* 0x0045a120 */
int RemoveLoops( int ptCount, int *indices );                            /* 0x0045a1f0 */

int FindLoops( int ptCount, const int *indices, int *loopStart, int *loopLength,
               int loopLimit );                                          /* 0x0045a280 */
int LoopLength( int start, int ptCount, const int *indices );             /* 0x0045a420 */
int RemoveDuplicateLoops( int ptCount, int *indices, const int *loopStart,
                          const int *loopLength, int loopCount );        /* 0x0045a4d0 */
bool LoopsAreIdentical( int ptCount, const int *indices, int start0, int start1,
                        int length );                                    /* 0x0045a5e0 */

void PrintWindingIndices( int ptCount, const int *indices );             /* 0x0045a4c0 */

#endif
