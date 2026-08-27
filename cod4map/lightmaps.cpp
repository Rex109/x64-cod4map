/* Original: .\lightmaps.cpp */

#include "lightmaps.h"

#include "cod4map.h"


#ifndef BYTE4_HELPERS_DEFINED
#define BYTE4_HELPERS_DEFINED

inline void Byte4Copy( const byte from[4], byte to[4] )
{
    *( unsigned int * )to = *( const unsigned int * )from;
}

#endif


lmapList_t  *g_preferredLightmaps;   /* 0x11ba8424 */
int          numLightmaps;           /* 0x11ba8428 */
byte         debugLightmaps;         /* 0x11ba842c */
lmapBlock_t *g_lmapBlocks;           /* 0x11ba8430 */


static lmapBlock_t *AllocLightmapBlock( void );
static void         FreeLightmapBlock( lmapBlock_t *block );
static void         MergeLightmapBlocks( lmapBlock_t *block );
static void         SplitLightmapBlock( lmapBlock_t *block, int width, int height );
static qboolean     FindLightmapBlock( int width, int height, int *lightmapNum,
                                       int *x, int *y, int *rotated,
                                       const lmapList_t *allowed );
static void         DebugFillLightmapRegion( int lightmapNum, int x, int y,
                                             int width, int height, int rotated );


/* AllocLightmapBlock  0x004185a0 */
static lmapBlock_t *AllocLightmapBlock( void )
{
    lmapBlock_t *block;

    block = new lmapBlock_t;
    if ( !block )
        Com_Error( "Out of memory" );

    return block;
}


/* FreeLightmapBlock  0x00418990 */
static void FreeLightmapBlock( lmapBlock_t *block )
{
    if ( block->prev )
        block->prev->next = block->next;

    if ( block->next )
        block->next->prev = block->prev;

    if ( g_lmapBlocks == block )
        g_lmapBlocks = block->next;

    delete block;
}


/* AddLightmapBlock  0x00418510 */
void AddLightmapBlock( int lightmapNum, int x, int y, int width, int height )
{
    lmapBlock_t *block;

    block = AllocLightmapBlock();

    block->lightmapNum = lightmapNum;
    block->x           = x;
    block->y           = y;
    block->width       = width;
    block->height      = height;

    block->prev = NULL;
    block->next = g_lmapBlocks;

    Assert( g_lmapBlocks );

    g_lmapBlocks->prev = block;
    g_lmapBlocks = block;
}


/* MergeLightmapBlocks  0x00418be0 */
static void MergeLightmapBlocks( lmapBlock_t *block )
{
    lmapBlock_t *otherBlock;

restart:
    for ( otherBlock = g_lmapBlocks; otherBlock; otherBlock = otherBlock->next )
    {
        if ( block->lightmapNum != otherBlock->lightmapNum || block == otherBlock )
            continue;

        if ( block->x == otherBlock->x )
        {
            Assert( block->y != otherBlock->y );

            if ( block->width == otherBlock->width )
            {
                if ( block->y == otherBlock->y + otherBlock->height )
                {
                    otherBlock->height += block->height;
                    FreeLightmapBlock( block );
                    block = otherBlock;
                    goto restart;
                }

                if ( block->y + block->height == otherBlock->y )
                {
                    block->height += otherBlock->height;
                    FreeLightmapBlock( otherBlock );
                    goto restart;
                }
            }
        }
        else if ( block->y == otherBlock->y && block->height == otherBlock->height )
        {
            if ( block->x == otherBlock->x + otherBlock->width )
            {
                otherBlock->width += block->width;
                FreeLightmapBlock( block );
                block = otherBlock;
                goto restart;
            }

            if ( block->x + block->width == otherBlock->x )
            {
                block->width += otherBlock->width;
                FreeLightmapBlock( otherBlock );
                goto restart;
            }
        }
    }
}


/* SplitLightmapBlock  0x004189f0 */
static void SplitLightmapBlock( lmapBlock_t *block, int width, int height )
{
    lmapBlock_t *split;

    split = NULL;

    if ( width == block->width )
    {
        block->y      += height;
        block->height -= height;

        if ( block->height < MIN_LIGHTMAP_BLOCK )
        {
            FreeLightmapBlock( block );
            return;
        }
    }
    else if ( height == block->height )
    {
        block->x     += width;
        block->width -= width;

        if ( block->width < MIN_LIGHTMAP_BLOCK )
        {
            FreeLightmapBlock( block );
            return;
        }
    }
    else if ( block->width * height < block->height * width )
    {
        block->y      += height;
        block->height -= height;

        if ( block->width - width > MIN_LIGHTMAP_BLOCK - 1 )
        {
            split = AllocLightmapBlock();
            split->x      = block->x + width;
            split->y      = block->y - height;
            split->width  = block->width - width;
            split->height = height;
        }
    }
    else
    {
        block->x     += width;
        block->width -= width;

        if ( block->height - height > MIN_LIGHTMAP_BLOCK - 1 )
        {
            split = AllocLightmapBlock();
            split->x      = block->x - width;
            split->y      = block->y + height;
            split->width  = width;
            split->height = block->height - height;
        }
    }

    if ( split )
    {
        split->lightmapNum = block->lightmapNum;
        split->prev = block;
        split->next = block->next;

        if ( block->next )
            block->next->prev = split;

        block->next = split;

        MergeLightmapBlocks( split );
    }

    MergeLightmapBlocks( block );
}


/* FindLightmapBlock  0x004186b0 */
static qboolean FindLightmapBlock( int width, int height, int *lightmapNum,
                                   int *x, int *y, int *rotated,
                                   const lmapList_t *allowed )
{
    lmapBlock_t      *block;
    lmapBlock_t      *best;
    const lmapList_t *cur;
    int               bestWaste;
    int               bestSlack;
    int               bestArea;
    int               bestRotated;
    int               extraWidth;
    int               extraHeight;
    int               area;
    int               pass;
    int               swap;

    bestWaste   = 0;
    bestSlack   = 0;
    bestArea    = 0;
    bestRotated = 0;
    best        = NULL;

    for ( pass = 0; pass < 2; pass++ )
    {
        for ( block = g_lmapBlocks; block; block = block->next )
        {
            if ( allowed )
            {
                for ( cur = allowed; cur; cur = cur->next )
                {
                    if ( block->lightmapNum == allowed->lightmapNum )
                        break;
                }

                if ( !cur )
                    continue;
            }

            if ( block->width == width && block->height == height )
            {
                *lightmapNum = block->lightmapNum;
                *x           = block->x;
                *y           = block->y;
                *rotated     = ( pass == 1 );
                FreeLightmapBlock( block );
                return qtrue;
            }

            if ( block->width == width && height < block->height )
            {
                if ( !best || bestWaste > 0 || bestSlack < block->height - height )
                {
                    best        = block;
                    bestWaste   = 0;
                    bestSlack   = block->height - height;
                    bestRotated = ( pass == 1 );
                }
            }
            else if ( block->height == height && width < block->width )
            {
                if ( !best || bestWaste > 0 || bestSlack < block->width - width )
                {
                    best        = block;
                    bestWaste   = 0;
                    bestSlack   = block->width - width;
                    bestRotated = ( pass == 1 );
                }
            }
            else if ( width < block->width && height < block->height )
            {
                extraWidth  = block->width  - width;
                extraHeight = block->height - height;

                area = I_min( extraWidth * block->height, extraHeight * block->width );

                if ( !best || ( bestWaste > 0 && area < bestArea ) )
                {
                    best        = block;
                    bestWaste   = I_min( extraWidth, extraHeight );
                    bestSlack   = ( extraWidth + extraHeight ) - bestWaste;
                    bestArea    = area;
                    bestRotated = ( pass == 1 );
                }
            }
        }

        if ( width == height )
            break;

        swap   = height;
        height = width;
        width  = swap;
    }

    if ( !best )
    {
        return qfalse;
    }

    *lightmapNum = best->lightmapNum;
    *x           = best->x;
    *y           = best->y;
    *rotated     = bestRotated;

    if ( bestRotated )
    {
        swap   = height;
        height = width;
        width  = swap;
    }

    SplitLightmapBlock( best, width, height );
    return qtrue;
}


/* AllocLightmapRegion  0x004185e0 */
qboolean AllocLightmapRegion( int width, int height, int *lightmapNum,
                              int *x, int *y, int *rotated )
{
    if ( !FindLightmapBlock( width, height, lightmapNum, x, y, rotated, g_preferredLightmaps )
      && ( !g_preferredLightmaps
        || !FindLightmapBlock( width, height, lightmapNum, x, y, rotated, NULL ) ) )
    {
        AllocNewLightmap();

        if ( !FindLightmapBlock( width, height, lightmapNum, x, y, rotated, NULL ) )
            return qfalse;
    }

    if ( debugLightmaps )
        DebugFillLightmapRegion( *lightmapNum, *x, *y, width, height, *rotated );

    return qtrue;
}


/* AllocNewLightmap  0x00418d80 */
void AllocNewLightmap( void )
{
    lmapBlock_t *block;

    block = AllocLightmapBlock();

    block->lightmapNum = numLightmaps;
    block->x           = 0;
    block->y           = 1;
    block->width       = LIGHTMAP_SIZE;
    block->height      = LIGHTMAP_SIZE - 1;

    block->next = g_lmapBlocks;
    block->prev = NULL;

    if ( g_lmapBlocks )
        g_lmapBlocks->prev = block;

    g_lmapBlocks = block;

    if ( debugLightmaps )
        numBSPLightBytes += LIGHTMAP_BYTES;

    numLightmaps++;

    if ( numLightmaps * LIGHTMAP_BYTES > MAX_MAP_LIGHTMAPS * LIGHTMAP_BYTES )
    {
        Com_Error( "MAX_MAP_LIGHTBYTES (%g MB) exceeded... more than %i lightmaps used\n",
                   93.0, numLightmaps - 1 );
    }
}


/* DebugFillLightmapRegion  0x00418e60 */
static void DebugFillLightmapRegion( int lightmapNum, int x, int y,
                                     int width, int height, int rotated )
{
    byte  colors[2][4];
    byte *pixel;
    int   xEnd;
    int   yEnd;
    int   i;
    int   j;

    if ( rotated )
    {
        xEnd = x + height;
        yEnd = y + width;
    }
    else
    {
        xEnd = x + width;
        yEnd = y + height;
    }

    colors[0][0] = rand() * 128 / 32768 + 64;
    colors[0][1] = rand() * 128 / 32768 + 64;
    colors[0][2] = rand() * 128 / 32768 + 64;
    colors[0][3] = 128;

    colors[1][0] = colors[0][0] * 4 / 5;
    colors[1][1] = colors[0][1] * 4 / 5;
    colors[1][2] = colors[0][2] * 4 / 5;
    colors[1][3] = 128;

    for ( j = y; j < yEnd; j++ )
    {
        for ( i = x; i < xEnd; i++ )
        {
            pixel = &bspLightBytes[ lightmapNum * LIGHTMAP_BYTES
                                    + ( j * LIGHTMAP_SIZE + i ) * 4 ];
            Assert( pixel[0] == 0 );
            Byte4Copy( colors[( i + j ) & 1], pixel );

            pixel = &bspLightBytes[ lightmapNum * LIGHTMAP_BYTES
                                    + ( ( j + LIGHTMAP_SIZE ) * LIGHTMAP_SIZE + i ) * 4 ];
            Assert( pixel[0] == 0 );
            Byte4Copy( colors[( i + j ) & 1], pixel );
        }
    }
}


/* AddPreferredLightmap  0x00419040 */
void AddPreferredLightmap( int lightmapNum )
{
    lmapList_t *entry;
    lmapList_t *cur;

    for ( cur = g_preferredLightmaps; cur; cur = cur->next )
    {
        if ( cur->lightmapNum == lightmapNum )
            return;
    }

    entry = new lmapList_t;
    entry->lightmapNum = lightmapNum;
    entry->next = g_preferredLightmaps;
    g_preferredLightmaps = entry;
}


/* FreePreferredLightmaps  0x004190a0 */
int FreePreferredLightmaps( void )
{
    lmapList_t *next;
    int         count;

    count = 0;

    while ( g_preferredLightmaps )
    {
        next = g_preferredLightmaps->next;
        delete g_preferredLightmaps;
        count++;
        g_preferredLightmaps = next;
    }

    return count;
}


/* LightmapCoordFloor  0x004190f0 */
int LightmapCoordFloor( float lmapCoord )
{
    float scaled;

    scaled = lmapCoord * 512.0 - 0.5;
    return ( int )floor( scaled );
}


/* LightmapCoordCeil  0x00419120 */
int LightmapCoordCeil( float lmapCoord )
{
    float scaled;

    scaled = lmapCoord * 512.0 + 0.5;
    return ( int )ceil( scaled );
}


/* LightmapSizeForRange  0x00419150 */
int LightmapSizeForRange( float lmapMin, float lmapMax )
{
    return LightmapCoordCeil( lmapMax ) + 1 - LightmapCoordFloor( lmapMin );
}
