/* Original: .\leakfile.cpp */

#include "leakfile.h"

#include "cod4map.h"
#include "bsp.h"
#include "portals.h"


int leakFileWritten;        /* 0x11ba8420 */


/* LeakFile  0x00417e30 */
void LeakFile( Tree_t *tree )
{
    FILE     *fp;
    Node_t   *nd;
    Node_t   *nextnode;
    portal_t *pp;
    portal_t *nextportal;
    vec3_t    mid;
    char      filename[MAX_OS_PATH];
    int       count;
    int       next;
    int       s;

    if ( leakFileWritten )
        return;
    if ( !tree->outside_node.occupied )
        return;

    Com_DPrintf( "--- LeakFile ---\n" );
    leakFileWritten = 1;

    GetOutputFileName( ".lin", filename, MAX_OS_PATH );
    fp = fopen( filename, "w" );
    if ( !fp )
        Com_Error( "Couldn't open %s\n", filename );

    count = 0;
    nd    = &tree->outside_node;

    while ( nd->occupied > 1 )
    {
        nextportal = NULL;
        nextnode   = NULL;
        next       = nd->occupied;

        for ( pp = nd->portals; pp; pp = pp->next[!s] )
        {
            s = ( pp->nodes[0] == nd );
            if ( pp->nodes[s]->occupied && pp->nodes[s]->occupied < next )
            {
                nextportal = pp;
                nextnode   = pp->nodes[s];
                next       = nextnode->occupied;
            }
        }

        SanityCheck( nextportal );
        SanityCheck( nextnode );

        nd = nextnode;
        WindingCenter( nextportal->winding, mid );
        fprintf( fp, "%f %f %f\n", mid[0], mid[1], mid[2] );
        count++;
    }

    GetVectorForKey( nd->occupant, "origin", mid );
    fprintf( fp, "%f %f %f\n", mid[0], mid[1], mid[2] );
    fclose( fp );

    printf( "\n\n================================\n\n"
            "WROTE BSP LEAKFILE: %s\n\n"
            "================================\n\n", filename );
    Com_DPrintf( "%5i point linefile\n", count + 1 );
}


/* FloodLeakPath_r  0x00418410 */
int FloodLeakPath_r( Node_t *node, Node_t *targetNode, int floodDist )
{
    portal_t *p;
    Node_t   *other;
    int       s;

    if ( node->planenum != PLANENUM_LEAF )
    {
        if ( FloodLeakPath_r( node->children[0], targetNode, floodDist ) )
            return 1;
        return FloodLeakPath_r( node->children[1], targetNode, floodDist );
    }

    if ( node->occupied != floodDist )
        return 0;

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );

        if ( p->manualPortalFace )
            continue;

        other = p->nodes[!s];
        if ( other->opaque == 1 || other->occupied <= floodDist )
            continue;

        if ( WindingIsTiny( p->winding ) )
            continue;

        other->occupied = floodDist + 1;
        if ( other == targetNode )
            return 1;
    }

    return 0;
}


/* PortalLeakFile  0x00418100 */
void PortalLeakFile( Tree_t *tree, portal_t *portal )
{
    FILE     *fp;
    Node_t   *source;
    Node_t   *target;
    Node_t   *nextnode;
    portal_t *pp;
    portal_t *nextportal;
    vec3_t    mid;
    vec3_t    point;
    char      filename[MAX_OS_PATH];
    float     offset;
    int       floodDist;
    int       count;
    int       s;

    if ( leakFileWritten )
        return;

    leakFileWritten = 1;

    source = portal->nodes[0];
    target = portal->nodes[1];
    source->occupied = 0;

    for ( floodDist = 0; !FloodLeakPath_r( tree->headnode, target, floodDist ); floodDist++ )
        ;

    GetOutputFileName( ".lin", filename, MAX_OS_PATH );
    fp = fopen( filename, "w" );
    if ( !fp )
        Com_Error( "Couldn't open %s\n", filename );

    WindingCenter( portal->winding, mid );

    offset = -LEAK_POINT_OFFSET;
    Vec3Mad( mid, offset, portal->plane.normal, point );
    fprintf( fp, "%f %f %f\n", point[0], point[1], point[2] );
    count = 1;

    while ( target != source )
    {
        nextportal = NULL;
        nextnode   = NULL;

        for ( pp = target->portals; pp; pp = pp->next[!s] )
        {
            s = ( pp->nodes[0] == target );
            if ( !pp->manualPortalFace
              && pp->nodes[s]->occupied == target->occupied - 1 )
            {
                nextportal = pp;
                nextnode   = pp->nodes[s];
            }
        }

        SanityCheck( nextnode );
        SanityCheck( nextportal );

        target = nextnode;
        WindingIsTiny( nextportal->winding );
        WindingCenter( nextportal->winding, point );
        fprintf( fp, "%f %f %f\n", point[0], point[1], point[2] );
        count++;
    }

    Vec3Mad( mid, -offset, portal->plane.normal, point );
    fprintf( fp, "%f %f %f\n", point[0], point[1], point[2] );
    count++;
    fclose( fp );

    printf( "\n\n================================\n\n"
            "WROTE PORTAL LEAKFILE: %s\n\n"
            "================================\n\n", filename );
    Com_DPrintf( "%5i point linefile\n", count );
}
