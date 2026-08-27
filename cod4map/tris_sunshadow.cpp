/* Original: .\tris_sunshadow.cpp */

#include "tris_sunshadow.h"
#include "tris_gridtree.h"
#include "surface.h"
#include "bspfile.h"
#include "collvec.h"
#include "com_math.h"
#include "com_vector.h"
#include "polylib.h"
#include "materials.h"
#include "assertive.h"

#define SUN_SHADOW_TRACE_LENGTH     16384.0f

#define SUN_SHADOW_TRACE_OFFSET     0.5f

#define SUN_SHADOW_FACING_EPSILON   0.01f

#define SUN_SHADOW_HIT_EPSILON      0.01f


/* Tris_SetCastsSunShadow  0x0045a670 */
void Tris_SetCastsSunShadow( TriSurf_t *surf, byte value )
{
    TriSurfProps_t  *props;
    CoalesceNode_t  *chain;

    props = surf->props;

    if ( props->coalesceChain )
    {
        Assert( props->ds == props->coalesceChain->props->ds );

        for ( chain = props->coalesceChain; chain; chain = chain->next )
            chain->props->ds->castsSunShadow = value;
    }
    else
    {
        props->ds->castsSunShadow = value;
    }
}


/* Tris_ConfirmCastsSunShadow  0x0045a650 */
void Tris_ConfirmCastsSunShadow( TriSurf_t *surf )
{
    Tris_SetCastsSunShadow( surf, CASTS_SUN_SHADOW_CONFIRMED );
}


/* Tris_CastsSunShadow  0x0045a700 */
bool Tris_CastsSunShadow( byte castsSunShadow )
{
    Assertx( castsSunShadow == CASTS_SUN_SHADOW_CONFIRMED ||
             castsSunShadow == CASTS_SUN_SHADOW_DENIED,
             "(castsSunShadow) = %i", castsSunShadow );

    return castsSunShadow == CASTS_SUN_SHADOW_CONFIRMED;
}


/* PointInWindingEpsilon  0x0045ab50 */
static int PointInWindingEpsilon( const vec3_t *points, int pointCount, const vec3_t normal,
                                  const vec3_t point, float epsilon )
{
    int axisU;
    int axisV;

    GetProjectionAxes( normal, &axisU, &axisV );
    return PointInPolyEpsilon( points, pointCount, axisU, axisV, point, epsilon );
}


/* SunShadowTraceCallback  0x0045a9e0 */
static qboolean SunShadowTraceCallback( TriSurf_t *surf, const vec3_t start,
                                        const vec3_t end, void *userData )
{
    const float *plane;
    float        distStart;
    float        distEnd;
    vec3_t       mid;

    (void)userData;

    if ( !( surf->props->ds->mtlTex->techSetFlags & 2 ) )
        return qtrue;

    plane = surf->props->plane;

    distStart = Vec3Dot( start, plane ) - plane[3];
    distEnd   = Vec3Dot( end,   plane ) - plane[3];

    if ( !( distStart * distEnd < 0.0f ) )
        return qtrue;

    Vec3Lerp( start, end, distStart / ( distStart - distEnd ), mid );

    if ( PointInWindingEpsilon( surf->w->pts, surf->w->ptCount, plane, mid,
                                SUN_SHADOW_HIT_EPSILON ) )
        return qfalse;

    return qtrue;
}


/* TraceVertDown  0x0045a970 */
static qboolean TraceVertDown( const vec3_t xyz, const vec3_t normal )
{
    vec3_t start;
    vec3_t end;

    Vec2Mad( xyz, SUN_SHADOW_TRACE_OFFSET, normal, start );
    start[2] = xyz[2] - SUN_SHADOW_TRACE_OFFSET;

    Vec3Copy( start, end );
    end[2] -= SUN_SHADOW_TRACE_LENGTH;

    return GridTree_TraceLine( start, end, NULL, SunShadowTraceCallback );
}


/* TriangleFacesSun  0x0045aac0 */
static bool TriangleFacesSun( const DrawVert_t *v0, const DrawVert_t *v1,
                              const DrawVert_t *v2, const vec3_t sunDir )
{
    vec4_t plane;
    float  dot;

    if ( !PlaneFromPoints( plane, v0->xyz, v1->xyz, v2->xyz ) )
        return false;

    dot = Vec3Dot( plane, sunDir );
    return dot <= SUN_SHADOW_FACING_EPSILON;
}


/* Tris_FindSunShadowCastersForSurf  0x0045a830 */
static void Tris_FindSunShadowCastersForSurf( DrawSurf_t *ds, const vec3_t sunDir )
{
    int i;

    ds->castsSunShadow = CASTS_SUN_SHADOW_CONFIRMED;

    for ( i = 0; i < ds->vertCount; i++ )
    {
        if ( ds->verts[i].normal[2] < 0.5f )
            return;
    }

    for ( i = 0; i < ds->terrain.indexCount; i += 3 )
    {
        if ( TriangleFacesSun( &ds->verts[ds->terrain.indexes[i + 0]],
                               &ds->verts[ds->terrain.indexes[i + 1]],
                               &ds->verts[ds->terrain.indexes[i + 2]],
                               sunDir ) )
            return;
    }

    for ( i = 0; i < ds->vertCount; i++ )
    {
        if ( !TraceVertDown( ds->verts[i].xyz, ds->verts[i].normal ) )
            return;
    }

    ds->castsSunShadow = CASTS_SUN_SHADOW_DENIED;
}


/* Tris_MarkAllDrawSurfsAsCasters  0x0045a7f0 */
void Tris_MarkAllDrawSurfsAsCasters( void )
{
    int i;

    for ( i = entities[entity_num].firstDrawSurf; i < numMapDrawSurfs; i++ )
        drawSurfs[i].castsSunShadow = CASTS_SUN_SHADOW_CONFIRMED;
}


/* Tris_MarkSunShadowCasters  0x0045a750 */
void Tris_MarkSunShadowCasters( void )
{
    vec3_t      sunAngles;
    vec3_t      sunDir;
    int         i;
    DrawSurf_t *ds;

    if ( entity_num )
    {
        Tris_MarkAllDrawSurfsAsCasters();
        return;
    }

    GetVectorForKey( &entities[0], "sundirection", sunAngles );
    AngleVectors( sunAngles, sunDir, NULL, NULL );

    for ( i = 0; i < numMapDrawSurfs; i++ )
    {
        ds = &drawSurfs[i];
        if ( ds->isModel )
            Tris_FindSunShadowCastersForSurf( ds, sunDir );
        else
            ds->castsSunShadow = CASTS_SUN_SHADOW_CONFIRMED;
    }
}
