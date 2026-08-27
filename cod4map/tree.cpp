/* Original: .\tree.cpp */

#include "tree.h"

#include "cod4map.h"
#include "portals.h"

extern int numthreads;              /* 0x0052964c */

int c_nodes;                        /* 0x00534ba8 */


/* PointInLeaf  0x0043c180 */
Node_t *PointInLeaf( Node_t *headnode, const vec3_t point )
{
    Node_t        *node;
    const plane_t *plane;
    float          dist;

    node = headnode;
    while ( node->planenum != PLANENUM_LEAF )
    {
        plane = &mapplanes[node->planenum];
        dist  = Vec3Dot( point, plane->normal ) - plane->dist;
        if ( dist >= 0.0f )
            node = node->children[0];
        else
            node = node->children[1];
    }

    return node;
}


/* PointInOpenLeaf  0x0043c1f0 */
Node_t *PointInOpenLeaf( Node_t *headnode, const vec3_t point )
{
    Node_t        *node;
    const plane_t *plane;
    float          dist;

    node = headnode;
    while ( node->planenum != PLANENUM_LEAF )
    {
        plane = &mapplanes[node->planenum];

        if ( node->children[0]->opaque == 1 )
        {
            node = node->children[1];
            continue;
        }

        if ( node->children[1]->opaque == 1 )
        {
            node = node->children[0];
            continue;
        }

        dist = Vec3Dot( point, plane->normal ) - plane->dist;
        if ( dist >= 0.0f )
            node = node->children[0];
        else if ( dist < 0.0f )
            node = node->children[1];
    }

    return node;
}


/* FreeTreePortals_r  0x0043c2a0 */
void FreeTreePortals_r( Node_t *node )
{
    portal_t *p;
    portal_t *nextp;
    int       s;

    if ( node->planenum != PLANENUM_LEAF )
    {
        FreeTreePortals_r( node->children[0] );
        FreeTreePortals_r( node->children[1] );
    }

    for ( p = node->portals; p; p = nextp )
    {
        s     = ( p->nodes[1] == node );
        nextp = p->next[s];
        RemovePortalFromNode( p, p->nodes[!s] );
        FreePortal( p );
    }

    node->portals = NULL;
}


/* FreeTree_r  0x0043c340 */
void FreeTree_r( Node_t *node )
{
    if ( node->planenum != PLANENUM_LEAF )
    {
        FreeTree_r( node->children[0] );
        FreeTree_r( node->children[1] );
    }

    FreeBrushList( node->leafBrushes );

    if ( node->volume )
        FreeBrush( node->volume );

    if ( numthreads == 1 )
        c_nodes--;

    free( node );
}


/* FreeTree  0x0043c3c0 */
void FreeTree( Tree_t *tree )
{
    FreeTreePortals_r( tree->headnode );
    FreeTree_r( tree->headnode );
    free( tree );
}


/* PrintTree_r  0x0043c3f0 */
void PrintTree_r( const Node_t *node, int depth )
{
    const Brush_t *brush;
    const plane_t *plane;
    int            i;

    for ( i = 0; i < depth; i++ )
        Com_Printf( "  " );

    if ( node->planenum == PLANENUM_LEAF )
    {
        if ( !node->leafBrushes )
        {
            Com_Printf( "NULL\n" );
            return;
        }

        for ( brush = node->leafBrushes; brush; brush = brush->next )
            Com_Printf( "%i ", brush->original->brushnum );
        Com_Printf( "\n" );
        return;
    }

    plane = &mapplanes[node->planenum];
    Com_Printf( "#%i (%5.2f %5.2f %5.2f):%5.2f\n",
                node->planenum,
                plane->normal[0], plane->normal[1], plane->normal[2],
                plane->dist );

    PrintTree_r( node->children[0], depth + 1 );
    PrintTree_r( node->children[1], depth + 1 );
}
