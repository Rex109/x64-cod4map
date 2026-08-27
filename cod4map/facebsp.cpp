/* Original: .\facebsp.cpp */

#include "facebsp.h"

#include "cod4map.h"
#include "bsp.h"
#include "qsort_vc8.h"

const float k_classifyDotThresholds[3]   = { 0.96592581f, 0.99144489f, 1.0f     };
const float k_classifyDistTolerance[3]   = { 0.0099999998f, 0.02f,     0.1f     };
const float k_classifyDistSqThreshold[3] = { 0.040000003f, 0.0025000002f, 0.0f  };
const float k_classifyMaxExtent[3]       = { 1.0f,         0.2f,      0.0f      };

int    useOptimizedSplit;    /* 0x00a35bf4 */

int    c_faceLeafs;          /* 0x11ba8410 */
float *splitMinsArray;       /* 0x11ba8414 */
float *splitMaxsArray;       /* 0x11ba8418 */
float *splitCoplanarArray;   /* 0x11ba841c */


/* AllocFace  0x00414f10 */
Face_t *AllocFace( void )
{
    Face_t *f;

    f = ( Face_t * )malloc( sizeof( Face_t ) );
    memset( f, 0, sizeof( Face_t ) );
    return f;
}


/* FreeFace  0x00414f40 */
void FreeFace( Face_t *f )
{
    if ( f->w )
        FreeWinding( f->w );
    free( f );
}


/* CountFaceList  0x00415410 */
int CountFaceList( const Face_t *list )
{
    const Face_t *f;
    int           c;

    c = 0;
    for ( f = list; f; f = f->next )
        c++;
    return c;
}


/* SplitPlaneValue  0x00415280 */
int SplitPlaneValue( int facing, int front, int back, int splits )
{
    if ( front == 0 || back == 0 )
        return ( int )0x80000000;

    return ( facing * 128 - splits * 256 ) - abs( front - back );
}


/* ClassifyWindingAgainstPlane  0x004152c0 */
int ClassifyWindingAgainstPlane( const Face_t *face, const vec3_t planeNormal,
                                 float planeDist, int axis )
{
    float minDist;
    float maxDist;

    WindingPlaneDistExtent( face->w, planeNormal, planeDist, &minDist, &maxDist );

    if ( minDist > -k_classifyMaxExtent[axis] && maxDist < k_classifyMaxExtent[axis] )
        return CLASSIFY_COPLANAR;

    if ( ( minDist * minDist < k_classifyDistSqThreshold[axis]
        || maxDist * maxDist < k_classifyDistSqThreshold[axis] )
      && Vec3Dot( planeNormal, mapplanes[face->planenum].normal ) > k_classifyDotThresholds[axis] )
        return CLASSIFY_COPLANAR;

    if ( minDist > -k_classifyDistTolerance[axis] )
        return SIDE_FRONT;

    if ( maxDist < k_classifyDistTolerance[axis] )
        return SIDE_BACK;

    if ( minDist > -k_classifyMaxExtent[axis] )
        return CLASSIFY_COPLANAR;

    if ( maxDist < k_classifyMaxExtent[axis] )
        return CLASSIFY_COPLANAR;

    return SIDE_CROSS;
}


/* CheckPlaneAgainstAllWindings  0x00416000 */
int CheckPlaneAgainstAllWindings( const vec4_t plane, const Face_t *list, int axis )
{
    const Face_t *f;

    for ( f = list; f; f = f->next )
    {
        if ( ClassifyWindingAgainstPlane( f, plane, plane[3], axis ) == CLASSIFY_COPLANAR )
            return 0;
    }
    return 1;
}


/* SelectSplitPlane_BlockSubdivision  0x00414f70 */
int SelectSplitPlane_BlockSubdivision( const Node_t *node, Face_t *list, int axis )
{
    Face_t *split;
    Face_t *check;
    Face_t *f;
    Face_t *bestSplit;
    const float *plane;
    vec3_t  normal;
    float   dist;
    int     planenum;
    int     bestValue;
    int     value;
    int     facing;
    int     front;
    int     back;
    int     splits;
    int     side;
    int     i;

    if ( blockSize != 0.0 )
    {
        for ( i = 0; i < 2; i++ )
        {
            dist = ( floor( ( float )( node->mins[i] / blockSize ) ) + 1.0 ) * blockSize;
            if ( dist < node->maxs[i] )
            {
                Vec3Clear( normal );
                normal[i] = 1.0f;
                dist = RoundFloatToInt( ( node->mins[i] + node->maxs[i] ) * 0.5 / blockSize ) * blockSize;

                SanityCheck( dist > node->mins[i] );
                SanityCheck( dist < node->maxs[i] );

                planenum = FindFloatPlane( normal, dist );
                Assert( ( planenum & 1 ) == 0 );
                return planenum;
            }
        }
    }

    bestValue = ( int )0x80000000;
    bestSplit = list;

    for ( f = list; f; f = f->next )
        f->checked = 0;

    for ( split = list; split; split = split->next )
    {
        if ( split->checked )
            continue;

        plane  = mapplanes[split->planenum].normal;
        splits = 0;
        facing = 0;
        front  = 0;
        back   = 0;

        for ( check = list; check; check = check->next )
        {
            if ( check->planenum == split->planenum )
            {
                facing++;
                check->checked = 1;
                continue;
            }

            side = ClassifyWindingAgainstPlane( check, plane, mapplanes[split->planenum].dist, axis );
            if ( side == CLASSIFY_COPLANAR )
                goto nextSplit;

            if ( side == SIDE_CROSS )
                splits++;
            else if ( side == SIDE_FRONT )
                front++;
            else if ( side == SIDE_BACK )
                back++;
        }

        value = SplitPlaneValue( facing, front, back, splits );
        if ( mapplanes[split->planenum].type < 3 )
            value += AXIAL_PLANE_BONUS;

        if ( bestValue < value )
        {
            bestValue = value;
            bestSplit = split;
        }

nextSplit: ;
    }

    return bestSplit ? bestSplit->planenum : -1;
}


/* SelectSplitPlane_DirectEval  0x00415870 */
void SelectSplitPlane_DirectEval( int *evalResult, const Face_t *list, int axis )
{
    const Face_t *split;
    const Face_t *check;
    const float  *plane;
    int           facing;
    int           front;
    int           back;
    int           splits;
    int           side;
    int           value;

    for ( split = list; split; split = split->next )
    {
        plane  = mapplanes[split->planenum].normal;
        facing = 0;
        front  = 0;
        back   = 0;
        splits = 0;

        for ( check = list; check; check = check->next )
        {
            if ( split->planenum == check->planenum )
            {
                facing++;
                continue;
            }

            side = ClassifyWindingAgainstPlane( check, plane, mapplanes[split->planenum].dist, axis );
            if ( side < SIDE_ON )
            {
                if ( side == SIDE_BACK )
                    back++;
                else if ( side == CLASSIFY_COPLANAR )
                    goto nextSplit;
                else if ( side == SIDE_FRONT )
                    front++;
                else
                    SanityCheckMsg( "unhandled side" );
            }
            else if ( side == SIDE_CROSS )
                splits++;
            else
                SanityCheckMsg( "unhandled side" );
        }

        value = SplitPlaneValue( facing, front, back, splits );
        if ( value > evalResult[1] || ( value == evalResult[1] && splits < evalResult[2] ) )
        {
            evalResult[0] = split->planenum;
            evalResult[1] = value;
            evalResult[2] = splits;
        }

nextSplit: ;
    }
}


/* SelectSplitPlane_EvaluateAxes  0x00415a00 */
void SelectSplitPlane_EvaluateAxes( int *evalResult, const Face_t *list,
                                    int faceCount, int axis )
{
    const Face_t *w;
    unsigned int  nCoplanar;
    unsigned int  nMins;
    unsigned int  j;
    int           i;
    int           minIdx, maxIdx, copIdx;
    int           frontCount, backCount, splitCount, coplanarCount;
    int           minsConsumed, coplanarConsumed;
    int           prevMinsConsumed, prevCoplanarConsumed;
    float         curDist, nextDist;
    float         lo, hi;
    int           score;
    int           found;
    vec4_t        testPlane;
    vec4_t        bestPlane;

    found = 0;

    for ( i = 0; i < 3; i++ )
    {
        nCoplanar = 0;
        nMins     = 0;

        for ( w = list; w; w = w->next )
        {
            lo = w->w->pts[0][i];
            hi = w->w->pts[0][i];

            for ( j = 0; j < w->w->ptCount; j++ )
            {
                if ( w->w->pts[j][i] < lo )
                    lo = w->w->pts[j][i];
                else if ( hi < w->w->pts[j][i] )
                    hi = w->w->pts[j][i];
            }

            if ( hi - lo < SPLIT_MIN_EXTENT )
            {
                splitCoplanarArray[nCoplanar] = ( lo + hi ) * 0.5f;
                nCoplanar++;
            }
            else
            {
                splitMinsArray[nMins] = lo;
                splitMaxsArray[nMins] = hi;
                nMins++;
            }
        }

        Assert( ( int )nMins <= faceCount );
        Assert( ( int )nCoplanar <= faceCount );
        Assert( nMins + nCoplanar == ( unsigned int )faceCount );

        qsort_vc8( splitMinsArray,     nMins,     sizeof( float ), FloatCompare );
        qsort_vc8( splitMaxsArray,     nMins,     sizeof( float ), FloatCompare );
        qsort_vc8( splitCoplanarArray, nCoplanar, sizeof( float ), FloatCompare );

        coplanarCount        = 0;
        frontCount           = 0;
        backCount            = faceCount;
        splitCount           = 0;
        minIdx               = 0;
        maxIdx               = 0;
        copIdx               = 0;
        prevMinsConsumed     = 0;
        prevCoplanarConsumed = 0;

        nextDist = I_fmin( splitMinsArray[0], splitCoplanarArray[0] );

        while ( nextDist < FLT_MAX )
        {
            curDist  = nextDist;
            nextDist = FLT_MAX;

            splitCount += prevMinsConsumed;
            backCount  -= prevMinsConsumed;

            minsConsumed = 0;
            for ( ; minIdx < ( int )nMins && splitMinsArray[minIdx] == curDist; minIdx++ )
                minsConsumed++;
            if ( minIdx < ( int )nMins && splitMinsArray[minIdx] < FLT_MAX )
                nextDist = splitMinsArray[minIdx];

            while ( maxIdx < ( int )nMins && splitMaxsArray[maxIdx] == curDist )
            {
                frontCount++;
                splitCount--;
                maxIdx++;
            }
            if ( maxIdx < ( int )nMins && splitMaxsArray[maxIdx] < nextDist )
                nextDist = splitMaxsArray[maxIdx];

            frontCount    += prevCoplanarConsumed;
            coplanarCount -= prevCoplanarConsumed;

            coplanarConsumed = 0;
            for ( ; copIdx < ( int )nCoplanar && splitCoplanarArray[copIdx] == curDist; copIdx++ )
                coplanarConsumed++;
            coplanarCount += coplanarConsumed;
            backCount     -= coplanarConsumed;
            if ( copIdx < ( int )nCoplanar && splitCoplanarArray[copIdx] < nextDist )
                nextDist = splitCoplanarArray[copIdx];

            Assert( coplanarCount + frontCount + backCount + splitCount == faceCount );
            Assert( coplanarCount >= 0 );
            Assert( frontCount >= 0 );
            Assert( backCount >= 0 );
            Assert( splitCount >= 0 );

            if ( frontCount != 0 && backCount != 0
              && abs( frontCount - backCount ) >= faceCount / 2 )
            {
                score = SplitPlaneValue( coplanarCount, frontCount, backCount, splitCount );
                if ( score > evalResult[1]
                  || ( score == evalResult[1] && splitCount < evalResult[2] ) )
                {
                    Vec3Clear( testPlane );
                    testPlane[i] = 1.0f;
                    testPlane[3] = curDist;

                    if ( CheckPlaneAgainstAllWindings( testPlane, list, axis ) )
                    {
                        evalResult[1] = score;
                        evalResult[2] = splitCount;
                        Vec4Copy( testPlane, bestPlane );
                        found = 1;
                    }
                }
            }

            prevMinsConsumed     = minsConsumed;
            prevCoplanarConsumed = coplanarConsumed;
        }
    }

    if ( found )
        evalResult[0] = FindFloatPlane( bestPlane, bestPlane[3] );
}


/* SelectSplitPlane_Optimized  0x00415770 */
int SelectSplitPlane_Optimized( const Node_t *node, const Face_t *list, int faceCount )
{
    int evalResult[3];
    int useDirectEval;
    int axis;

    if ( faceCount == 0 )
        return -1;

    if ( faceCount == 1 )
        return list->planenum;

    evalResult[0] = -1;
    evalResult[1] = ( int )0x80000000;
    evalResult[2] = 0x7fffffff;

    useDirectEval = 0;
    if ( faceCount <= DIRECT_EVAL_THRESHOLD
      || ( node->maxs[0] - node->mins[0] < blockSize
        && node->maxs[1] - node->mins[1] < blockSize ) )
        useDirectEval = 1;

    for ( axis = 0; axis < 3 && evalResult[0] < 0; axis++ )
    {
        if ( useDirectEval )
            SelectSplitPlane_DirectEval( evalResult, list, axis );

        SelectSplitPlane_EvaluateAxes( evalResult, list, faceCount, axis );

        if ( evalResult[0] < 0 && !useDirectEval )
            SelectSplitPlane_DirectEval( evalResult, list, axis );
    }

    return evalResult[0];
}


/* BuildBspTree_Recursive  0x00415440 */
void BuildBspTree_Recursive( Node_t *node, Face_t *list, int faceCount )
{
    Face_t   *frontList;
    Face_t   *backList;
    Face_t   *cur;
    Face_t   *next;
    Face_t   *f;
    winding_t *frontWinding;
    winding_t *backWinding;
    const float *plane;
    int       splitPlane;
    int       frontCount;
    int       backCount;
    int       axis;
    int       i;

    if ( useOptimizedSplit )
    {
        splitPlane = SelectSplitPlane_Optimized( node, list, faceCount );
    }
    else
    {
        splitPlane = -1;
        for ( axis = 0; axis < 3; axis++ )
        {
            splitPlane = SelectSplitPlane_BlockSubdivision( node, list, axis );
            if ( splitPlane >= 0 )
                break;
        }
    }

    if ( splitPlane == -1 )
    {
        node->planenum = PLANENUM_LEAF;
        c_faceLeafs++;
        return;
    }

    node->planenum = splitPlane;
    plane          = mapplanes[splitPlane].normal;

    frontList  = NULL;
    backList   = NULL;
    frontCount = 0;
    backCount  = 0;

    for ( cur = list; cur; cur = next )
    {
        next = cur->next;

        if ( cur->planenum == node->planenum )
        {
            FreeFace( cur );
            continue;
        }

        switch ( WindingPlaneSide( cur->w, plane, mapplanes[splitPlane].dist ) )
        {
        case SIDE_CROSS:
            ClipWindingEpsilon( cur->w, plane, mapplanes[splitPlane].dist,
                                SPLIT_CLIP_EPSILON, &frontWinding, &backWinding, qfalse );
            if ( frontWinding )
            {
                f            = AllocFace();
                f->w         = frontWinding;
                f->next      = frontList;
                f->planenum  = cur->planenum;
                frontCount++;
                frontList    = f;
            }
            if ( backWinding )
            {
                f            = AllocFace();
                f->w         = backWinding;
                f->next      = backList;
                f->planenum  = cur->planenum;
                backCount++;
                backList     = f;
            }
            FreeFace( cur );
            break;

        case SIDE_FRONT:
            cur->next = frontList;
            frontList = cur;
            frontCount++;
            break;

        case SIDE_BACK:
            cur->next = backList;
            backList  = cur;
            backCount++;
            break;
        }
    }

    for ( i = 0; i < 2; i++ )
    {
        node->children[i]         = AllocNode();
        node->children[i]->parent = node;
        Vec3Copy( node->mins, node->children[i]->mins );
        Vec3Copy( node->maxs, node->children[i]->maxs );
    }

    for ( i = 0; i < 3; i++ )
    {
        if ( plane[i] == 1.0f )
        {
            node->children[0]->mins[i] = mapplanes[splitPlane].dist;
            node->children[1]->maxs[i] = mapplanes[splitPlane].dist;
            break;
        }
    }

    BuildBspTree_Recursive( node->children[0], frontList, frontCount );
    BuildBspTree_Recursive( node->children[1], backList,  backCount );
}


/* BuildBspTree  0x004160a0 */
Tree_t *BuildBspTree( Face_t *list )
{
    Tree_t   *tree;
    Face_t   *f;
    unsigned int i;
    int       faceCount;

    Com_DPrintf( "--- FaceBSP---\n" );

    tree      = AllocTree();
    faceCount = 0;

    for ( f = list; f; f = f->next )
    {
        faceCount++;
        for ( i = 0; i < f->w->ptCount; i++ )
            AddPointToBounds( f->w->pts[i], tree->mins, tree->maxs );
    }

    Com_DPrintf( "%5i faces\n", faceCount );

    tree->headnode = AllocNode();
    Vec3Copy( tree->mins, tree->headnode->mins );
    Vec3Copy( tree->maxs, tree->headnode->maxs );

    c_faceLeafs = 0;

    if ( useOptimizedSplit )
    {
        splitCoplanarArray = ( float * )malloc( faceCount * sizeof( float ) );
        splitMinsArray     = ( float * )malloc( faceCount * sizeof( float ) );
        splitMaxsArray     = ( float * )malloc( faceCount * sizeof( float ) );
        BuildBspTree_Recursive( tree->headnode, list, faceCount );
        free( splitMaxsArray );
        free( splitMinsArray );
        free( splitCoplanarArray );
    }
    else
    {
        BuildBspTree_Recursive( tree->headnode, list, faceCount );
    }

    Com_DPrintf( "%5i leafs\n", c_faceLeafs );
    return tree;
}


/* CollectBrushWindings  0x00416240 */
Face_t *CollectBrushWindings( Brush_t *brushList )
{
    Brush_t      *b;
    side_t       *s;
    winding_t    *w;
    Face_t       *f;
    Face_t       *list;
    unsigned int  i;

    list = NULL;

    for ( b = brushList; b; b = b->next )
    {
        if ( b->detail && !b->forceVisible )
            continue;

        for ( i = 0; i < b->sideCount; i++ )
        {
            s = &b->sides[i];
            w = ( winding_t * )s->visibleHull;
            if ( !w )
                continue;

            if ( b->detail && ( s->material->surfaceFlags & SURF_PORTAL ) == 0 )
                continue;

            f           = AllocFace();
            f->w        = CopyWinding( w );
            f->planenum = s->planenum & ~1;
            f->next     = list;
            list        = f;
        }
    }

    return list;
}


/* ClipSidesIntoTree  0x00416330 */
void ClipSidesIntoTree( Brush_t *brushList, Tree_t *tree )
{
    Brush_t      *b;
    unsigned int  i;

    for ( b = brushList; b; b = b->next )
    {
        for ( i = 0; i < b->sideCount; i++ )
        {
            if ( b->sides[i].visibleHull )
            {
                if ( WindingInSolid_r( tree->headnode, ( winding_t * )b->sides[i].visibleHull ) )
                {
                    FreeWinding( ( winding_t * )b->sides[i].visibleHull );
                    b->sides[i].visibleHull = NULL;
                }
            }
        }
    }
}


/* WindingInSolid_r  0x004163c0 */
qboolean WindingInSolid_r( const Node_t *node, winding_t *w )
{
    winding_t *frontWinding;
    winding_t *backWinding;
    qboolean   result;
    int        side;

    while ( node->opaque == 2 )
    {
        side = ClipWindingEpsilon_Internal( w, mapplanes[node->planenum].normal,
                                            mapplanes[node->planenum].dist, 0.1f,
                                            &frontWinding, &backWinding );
        if ( side == SIDE_ON )
            break;

        if ( side == SIDE_CROSS )
        {
            result = ( WindingInSolid_r( node->children[0], frontWinding )
                    && WindingInSolid_r( node->children[1], backWinding ) ) ? qtrue : qfalse;
            FreeWinding( frontWinding );
            FreeWinding( backWinding );
            return result;
        }

        node = node->children[side];
    }

    if ( node->opaque != 2 )
        return ( node->opaque == 1 ) ? qtrue : qfalse;

    return ( WindingInSolid_r( node->children[0], w )
          && WindingInSolid_r( node->children[1], w ) ) ? qtrue : qfalse;
}


/* FloatCompare  0x00416050 */
int FloatCompare( const void *va, const void *vb )
{
    float diff;

    diff = *( const float * )va - *( const float * )vb;
    if ( diff < 0.0f )
        return -1;
    if ( diff <= 0.0f )
        return 0;
    return 1;
}
