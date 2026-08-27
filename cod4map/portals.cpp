/* Original: .\portals.cpp */

#include "portals.h"

#include "cod4map.h"
#include "bsp.h"
#include "tree.h"
#include "leakfile.h"

extern void SnapPointToPlanes( const vec4_t *planeArray, int numPlanes, vec3_t point,
                               float gridSize, float tolerance );       /* 0x00473290 */
extern int  numthreads;                                                 /* 0x0052964c */

int c_leafsfilled;                  /* 0x12ece910 */
int c_peak_portals;                 /* 0x12ece918 */
int c_solidleafs;                   /* 0x12ece91c */
int c_tinyportals;                  /* 0x12ece924 */
int c_insideleafs;                  /* 0x12ece928 */
int c_areas;                        /* 0x12ece92c */
int c_floodedleafs;                 /* 0x12ece930 */
int c_active_portals;               /* 0x12ece934 */

byte s_cellCullGroupBits[MAX_MAP_CELLS * MAX_MAP_CULLGROUPS / 8];   /* 0x12ece938 */

portalRef_t *s_cellPortalRefs[MAX_CELL_PORTAL_LISTS];   /* 0x12f0e938 */
int          debugPortals;                      /* 0x12f0f938 */
int          s_portalErrorCount;                        /* 0x12f0f93c */

static const char *s_nonFloodingClassnames[7] =
{
    "info_vehicle_node",
    "info_vehicle_node_rotate",
    "script_model",
    "script_origin",
    "script_vehicle",
    "misc_prefab",
    "script_struct"
};


/* AllocPortal  0x0042f000 */
portal_t *AllocPortal( void )
{
    portal_t *p;

    if ( numthreads == 1 )
        c_active_portals++;
    if ( c_active_portals > c_peak_portals )
        c_peak_portals = c_active_portals;

    p = ( portal_t * )malloc( sizeof( portal_t ) );
    memset( p, 0, sizeof( portal_t ) );
    return p;
}


/* FreePortal  0x0042f060 */
void FreePortal( portal_t *p )
{
    if ( p->winding )
        FreeWinding( p->winding );
    if ( numthreads == 1 )
        c_active_portals--;
    free( p );
}


/* Portal_Passable  0x0042f0a0 */
int Portal_Passable( const portal_t *p )
{
    if ( !p->onnode )
        return qfalse;

    Assert( p->nodes[0]->planenum == PLANENUM_LEAF
         && p->nodes[1]->planenum == PLANENUM_LEAF );

    if ( p->nodes[0]->opaque == 1 || p->nodes[1]->opaque == 1 )
        return qfalse;

    return qtrue;
}


/* AddPortalToNode  0x0042f110 */
void AddPortalToNode( portal_t *p, Node_t *front, Node_t *back )
{
    if ( p->nodes[0] || p->nodes[1] )
        Com_Error( "AddPortalToNode: already included" );

    p->nodes[0] = front;
    p->next[0]  = front->portals;
    front->portals = p;

    p->nodes[1] = back;
    p->next[1]  = back->portals;
    back->portals = p;
}


/* RemovePortalFromNode  0x0042f170 */
void RemovePortalFromNode( portal_t *portal, Node_t *l )
{
    portal_t **pp;
    portal_t  *t;

    pp = &l->portals;
    while ( 1 )
    {
        t = *pp;
        if ( !t )
            Com_Error( "RemovePortalFromNode: portal not in leaf" );

        if ( t == portal )
            break;

        if ( t->nodes[0] == l )
            pp = &t->next[0];
        else if ( t->nodes[1] == l )
            pp = &t->next[1];
        else
            Com_Error( "RemovePortalFromNode: portal not bounding leaf" );
    }

    if ( portal->nodes[0] == l )
    {
        *pp = portal->next[0];
        portal->nodes[0] = NULL;
    }
    else if ( portal->nodes[1] == l )
    {
        *pp = portal->next[1];
        portal->nodes[1] = NULL;
    }
}


/* MakeTreePortals  0x0042f230 */
void MakeTreePortals( Tree_t *tree )
{
    vec3_t    bounds[2];
    int       i, j, n;
    portal_t *p;
    portal_t *portals[6];
    plane_t   bplanes[6];
    plane_t  *pl;
    Node_t   *node;

    node = tree->headnode;

    for ( i = 0; i < 3; i++ )
    {
        bounds[0][i] = tree->mins[i] - SIDESPACE;
        bounds[1][i] = tree->maxs[i] + SIDESPACE;
        if ( bounds[0][i] >= bounds[1][i] )
            Com_Error( "Backwards tree volume.\n"
                       "This usually means there are no structural brushes.\n" );
    }

    tree->outside_node.planenum    = PLANENUM_LEAF;
    tree->outside_node.leafBrushes = NULL;
    tree->outside_node.portals     = NULL;
    tree->outside_node.opaque      = qfalse;

    for ( i = 0; i < 3; i++ )
    {
        for ( j = 0; j < 2; j++ )
        {
            n = j * 3 + i;

            p = AllocPortal();
            portals[n] = p;

            pl = &bplanes[n];
            memset( pl, 0, sizeof( *pl ) );
            if ( j )
            {
                pl->normal[i] = -1.0f;
                pl->dist      = -bounds[j][i];
            }
            else
            {
                pl->normal[i] = 1.0f;
                pl->dist      = bounds[j][i];
            }

            p->plane   = *pl;
            p->winding = BaseWindingForPlane( pl->normal, pl->dist );
            AddPortalToNode( p, node, &tree->outside_node );
        }
    }

    for ( i = 0; i < 6; i++ )
    {
        for ( j = 0; j < 6; j++ )
        {
            if ( j == i )
                continue;
            ClipWindingByPlane( &portals[i]->winding, bplanes[j].normal, bplanes[j].dist,
                                PORTAL_CLIP_EPSILON );
        }
    }
}


/* CreatePortalWinding  0x0042f530 */
winding_t *CreatePortalWinding( const Node_t *node )
{
    winding_t     *w;
    const Node_t  *n;
    const plane_t *plane;
    vec3_t         normal;
    float          dist;

    w = BaseWindingForPlane( mapplanes[node->planenum].normal, mapplanes[node->planenum].dist );

    for ( n = node->parent; n && w; n = n->parent )
    {
        plane = &mapplanes[n->planenum];
        if ( n->children[0] == node )
        {
            ClipWindingByPlane( &w, plane->normal, plane->dist, PORTAL_SPLIT_EPSILON );
        }
        else
        {
            Vec3Sub( vec3_origin, plane->normal, normal );
            dist = -plane->dist;
            ClipWindingByPlane( &w, normal, dist, PORTAL_SPLIT_EPSILON );
        }
        node = n;
    }

    return w;
}


/* MakeNodePortal  0x0042f620 */
void MakeNodePortal( Node_t *node )
{
    portal_t  *new_portal;
    portal_t  *p;
    winding_t *w;
    vec3_t     normal;
    float      dist;
    int        side;

    w = CreatePortalWinding( node );

    for ( p = node->portals; p && w; p = p->next[side] )
    {
        if ( p->nodes[0] == node )
        {
            side = 0;
            Vec3Copy( p->plane.normal, normal );
            dist = p->plane.dist;
        }
        else if ( p->nodes[1] == node )
        {
            side = 1;
            Vec3Sub( vec3_origin, p->plane.normal, normal );
            dist = -p->plane.dist;
        }
        else
        {
            side = -1;
            dist = 0.0f;
            Com_Error( "CutNodePortals_r: mislinked portal" );
        }

        ClipWindingByPlane( &w, normal, dist, PORTAL_CLIP_EPSILON );
    }

    if ( !w )
        return;

    if ( WindingIsTiny( w ) )
    {
        c_tinyportals++;
        FreeWinding( w );
        return;
    }

    new_portal          = AllocPortal();
    new_portal->plane   = mapplanes[node->planenum];
    new_portal->onnode  = node;
    new_portal->winding = w;

    AddPortalToNode( new_portal, node->children[0], node->children[1] );
}


/* SplitNodePortals  0x0042f790 */
void SplitNodePortals( Node_t *node )
{
    portal_t  *p;
    portal_t  *next_portal;
    portal_t  *new_portal;
    Node_t    *f;
    Node_t    *b;
    Node_t    *other_node;
    int        side;
    const plane_t *plane;
    winding_t *frontwinding;
    winding_t *backwinding;

    plane = &mapplanes[node->planenum];
    f     = node->children[0];
    b     = node->children[1];

    for ( p = node->portals; p; p = next_portal )
    {
        if ( p->nodes[0] == node )
            side = 0;
        else if ( p->nodes[1] == node )
            side = 1;
        else
        {
            side = -1;
            Com_Error( "SplitNodePortals: mislinked portal" );
        }

        next_portal = p->next[side];
        other_node  = p->nodes[!side];

        RemovePortalFromNode( p, p->nodes[0] );
        RemovePortalFromNode( p, p->nodes[1] );

        ClipWindingEpsilon( p->winding, plane->normal, plane->dist,
                            PORTAL_SPLIT_EPSILON, &frontwinding, &backwinding, qfalse );

        if ( frontwinding && WindingIsTiny( frontwinding ) )
        {
            if ( !f->tinyportals )
                Vec3Copy( frontwinding->pts[0], f->referencepoint );
            f->tinyportals++;

            if ( !other_node->tinyportals )
                Vec3Copy( frontwinding->pts[0], other_node->referencepoint );
            other_node->tinyportals++;

            FreeWinding( frontwinding );
            frontwinding = NULL;
            c_tinyportals++;
        }

        if ( backwinding && WindingIsTiny( backwinding ) )
        {
            if ( !b->tinyportals )
                Vec3Copy( backwinding->pts[0], b->referencepoint );
            b->tinyportals++;

            if ( !other_node->tinyportals )
                Vec3Copy( backwinding->pts[0], other_node->referencepoint );
            other_node->tinyportals++;

            FreeWinding( backwinding );
            backwinding = NULL;
            c_tinyportals++;
        }

        if ( !frontwinding && !backwinding )
            continue;

        if ( !frontwinding )
        {
            FreeWinding( backwinding );
            if ( side == 0 )
                AddPortalToNode( p, b, other_node );
            else
                AddPortalToNode( p, other_node, b );
            continue;
        }

        if ( !backwinding )
        {
            FreeWinding( frontwinding );
            if ( side == 0 )
                AddPortalToNode( p, f, other_node );
            else
                AddPortalToNode( p, other_node, f );
            continue;
        }

        new_portal          = AllocPortal();
        *new_portal         = *p;
        new_portal->winding = backwinding;
        FreeWinding( p->winding );
        p->winding          = frontwinding;

        if ( side == 0 )
        {
            AddPortalToNode( p, f, other_node );
            AddPortalToNode( new_portal, b, other_node );
        }
        else
        {
            AddPortalToNode( p, other_node, f );
            AddPortalToNode( new_portal, other_node, b );
        }
    }

    node->portals = NULL;
}


/* CalcNodeBounds  0x0042fb00 */
void CalcNodeBounds( Node_t *node )
{
    portal_t     *p;
    int           s;
    unsigned int  i;

    ClearBounds( node->mins, node->maxs );

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );
        for ( i = 0; i < p->winding->ptCount; i++ )
            AddPointToBounds( p->winding->pts[i], node->mins, node->maxs );
    }
}


/* MakeTreePortals_r  0x0042fba0 */
void MakeTreePortals_r( Node_t *node )
{
    int i;

    CalcNodeBounds( node );

    if ( node->mins[0] >= node->maxs[0] )
    {
        Com_Printf( "WARNING: node without a volume\n" );
        Com_Printf( "node has %d tiny portals\n", node->tinyportals );
        Com_Printf( "node reference point %1.2f %1.2f %1.2f\n",
                    node->referencepoint[0],
                    node->referencepoint[1],
                    node->referencepoint[2] );
    }

    for ( i = 0; i < 3; i++ )
    {
        if ( node->mins[i] < -131072.0 || node->maxs[i] > 131072.0 )
        {
            Com_Printf( "WARNING: node with unbounded volume\n" );
            break;
        }
    }

    if ( node->planenum == PLANENUM_LEAF )
        return;

    MakeNodePortal( node );
    SplitNodePortals( node );

    MakeTreePortals_r( node->children[0] );
    MakeTreePortals_r( node->children[1] );
}


/* ProcessBspTree  0x0042fcc0 */
void ProcessBspTree( Tree_t *tree )
{
    Com_DPrintf( "----- MakeTreePortals-----\n" );
    MakeTreePortals( tree );
    MakeTreePortals_r( tree->headnode );
    Com_DPrintf( "%6d tiny portals\n", c_tinyportals );
}


/* FloodEntities  0x0042fd00 */
int FloodEntities( Tree_t *tree )
{
    Node_t     *headnode;
    vec3_t      origin;
    const char *cl;
    int         inside;
    int         i;

    headnode = tree->headnode;
    Com_DPrintf( "--- FloodEntities---\n" );

    inside = 0;
    tree->outside_node.occupied = 0;
    c_floodedleafs = 0;

    for ( i = 1; i < num_entities; i++ )
    {
        GetVectorForKey( &entities[i], "origin", origin );
        if ( Vec3Compare( origin, vec3_origin ) )
            continue;

        cl = ValueForKey( &entities[i], "classname" );
        if ( IsExcludedEntity( cl ) )
            continue;

        origin[2] += 1.0f;

        if ( PlaceOccupant( headnode, origin, &entities[i] ) )
            inside = 1;
    }

    Com_DPrintf( "%5i flooded leafs\n", c_floodedleafs );

    if ( !inside )
        Com_DPrintf( "no entities in open-- no filling\n" );
    else if ( tree->outside_node.occupied )
        Com_DPrintf( "entity reached from outside-- no filling\n" );

    if ( !inside || tree->outside_node.occupied )
        return 0;

    return 1;
}


/* PlaceOccupant  0x0042fe60 */
int PlaceOccupant( Node_t *headnode, const vec3_t origin, Entity_t *occupant )
{
    Node_t        *node;
    const plane_t *plane;
    float          d;

    node = headnode;
    while ( node->planenum != PLANENUM_LEAF )
    {
        plane = &mapplanes[node->planenum];
        d = Vec3Dot( origin, plane->normal ) - plane->dist;
        if ( d < 0.0f )
            node = node->children[1];
        else
            node = node->children[0];
    }

    if ( node->opaque == 1 )
        return qfalse;

    node->occupant = occupant;
    FloodPortals_r( node, 1 );
    return qtrue;
}


/* FloodPortals_r  0x0042fef0 */
void FloodPortals_r( Node_t *node, int dist )
{
    portal_t *p;
    int       s;

    if ( node->occupied )
        return;
    if ( node->opaque == 1 )
        return;

    c_floodedleafs++;
    node->occupied = dist;

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );
        FloodPortals_r( p->nodes[!s], dist + 1 );
    }
}


/* IsExcludedEntity  0x0042ff80 */
int IsExcludedEntity( const char *classname )
{
    unsigned int i;

    for ( i = 0; i < ARRAY_COUNT( s_nonFloodingClassnames ); i++ )
    {
        if ( !strcmp( classname, s_nonFloodingClassnames[i] ) )
            return 1;
    }
    return 0;
}


/* FloodAreas_r  0x0042ffd0 */
void FloodAreas_r( Node_t *node )
{
    portal_t *p;
    int       s;

    if ( node->area != -1 )
        return;
    if ( node->cluster == -1 )
        return;

    node->area = c_areas;

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );
        if ( Portal_Passable( p ) )
            FloodAreas_r( p->nodes[!s] );
    }
}


/* FindAreas_r  0x00430060 */
void FindAreas_r( Node_t *node )
{
    if ( node->planenum != PLANENUM_LEAF )
    {
        FindAreas_r( node->children[0] );
        FindAreas_r( node->children[1] );
        return;
    }

    if ( node->opaque == 1 )
        return;
    if ( node->area != -1 )
        return;

    FloodAreas_r( node );
    c_areas++;
}


/* FloodAreas  0x004300c0 */
void FloodAreas( Tree_t *tree )
{
    Com_DPrintf( "--- FloodAreas---\n" );
    FindAreas_r( tree->headnode );
    CheckAreas_r( tree->headnode );
    Com_DPrintf( "%5i areas\n", c_areas );
}


/* CheckAreas_r  0x00430110 */
void CheckAreas_r( const Node_t *node )
{
    if ( node->planenum != PLANENUM_LEAF )
    {
        CheckAreas_r( node->children[0] );
        CheckAreas_r( node->children[1] );
        return;
    }

    if ( node->opaque != 1 )
        Assert( node->cluster == -1 || node->area != -1 );
}


/* FillOutside_r  0x00430180 */
void FillOutside_r( Node_t *node )
{
    if ( node->planenum != PLANENUM_LEAF )
    {
        FillOutside_r( node->children[0] );
        FillOutside_r( node->children[1] );

        if ( node->children[0]->opaque == node->children[1]->opaque )
            node->opaque = node->children[0]->opaque;
        else
            node->opaque = 2;
        return;
    }

    if ( !node->occupied )
    {
        if ( node->opaque == 1 )
        {
            c_solidleafs++;
        }
        else
        {
            c_leafsfilled++;
            node->opaque = 1;
        }
    }
    else
    {
        c_insideleafs++;
    }
}


/* FillOutside  0x00430230 */
void FillOutside( Node_t *headnode )
{
    c_leafsfilled = 0;
    c_insideleafs = 0;
    c_solidleafs  = 0;

    Com_DPrintf( "--- FillOutside---\n" );
    FillOutside_r( headnode );

    Com_DPrintf( "%5i solid leafs\n",  c_solidleafs );
    Com_DPrintf( "%5i leafs filled\n", c_leafsfilled );
    Com_DPrintf( "%5i inside leafs\n", c_insideleafs );
}


/* PortalWarning  0x004305d0 */
void PortalWarning( const char *message, const winding_t *w, const Brush_t *brush )
{
    MapErrorWinding( MAPERROR_NONFATAL, w,
                     brush->mapInfoIndex, brush->entitynum, brush->brushnum,
                     "%s", message );
}


/* SnapNormal  0x004313e0 */
void SnapNormal( vec3_t normal )
{
    int i;

    for ( i = 0; i < 3; i++ )
    {
        if ( normal[i] >= SNAP_NORMAL_ZERO || normal[i] <= -SNAP_NORMAL_ZERO )
        {
            if ( normal[i] > SNAP_NORMAL_ONE )
                normal[i] = 1.0f;
            else if ( normal[i] < -SNAP_NORMAL_ONE )
                normal[i] = -1.0f;
        }
        else
        {
            normal[i] = 0.0f;
        }
    }

    Vec3Normalize( normal );
}


/* BuildSidePlanes  0x00430be0 */
void BuildSidePlanes( Brush_t *brushList, Tree_t *tree )
{
    Brush_t      *b;
    side_t       *s;
    winding_t    *w;
    vec3_t        dir;
    vec4_t        sidePlanes[MAX_POINTS_ON_WINDING];
    unsigned int  i;
    unsigned int  j;
    unsigned int  k;

    for ( b = brushList; b; b = b->next )
    {
        for ( i = 0; i < b->sideCount; i++ )
        {
            s = &b->sides[i];
            if ( ( s->surfaceFlags & SURF_PORTAL ) == 0 )
                continue;

            w = ( winding_t * )s->visibleHull;
            if ( !w )
                continue;

            for ( j = 0; j < w->ptCount; j++ )
            {
                Vec3Sub( w->pts[( j + 1 ) % w->ptCount], w->pts[j], dir );
                Vec3Cross( dir, mapplanes[s->planenum].normal, sidePlanes[j] );
                Vec3Normalize( sidePlanes[j] );
                sidePlanes[j][3] = Vec3Dot( sidePlanes[j], w->pts[j] );

                for ( k = 0; k < w->ptCount; k++ )
                {
                    Assert( Vec3Dot( sidePlanes[j], w->pts[k] ) - sidePlanes[j][3]
                            > I_fmin( -EQUAL_EPSILON, -0.000001f * fabs( sidePlanes[j][3] ) ) );
                }
            }

            s->portalIgnored = 1;
            FilterBrushIntoTree_r( s, b, sidePlanes, tree->headnode, tree );

            if ( s->portalIgnored )
                PortalWarning( "portal is not at a BSP split, is floating, "
                               "or is next to a solid brush",
                               ( const winding_t * )s->visibleHull, b );

            s->cellOnPortalSide[0] = CELLNUM_UNKNOWN;
            s->cellOnPortalSide[1] = CELLNUM_UNKNOWN;
            s->brush               = b;
        }
    }
}


/* FilterBrushIntoTree_r  0x00430ec0 */
void FilterBrushIntoTree_r( side_t *face, Brush_t *brush, const vec4_t *sidePlanes,
                            Node_t *node, Tree_t *tree )
{
    portal_t     *p;
    winding_t    *w;
    int           s;
    int           side;
    unsigned int  i;
    unsigned int  j;
    int           planenum;

    if ( node->planenum != PLANENUM_LEAF )
    {
        side = WindingPlaneSide( ( const winding_t * )face->winding,
                                 mapplanes[node->planenum].normal,
                                 mapplanes[node->planenum].dist );
        if ( side != SIDE_BACK )
            FilterBrushIntoTree_r( face, brush, sidePlanes, node->children[0], tree );
        if ( side != SIDE_FRONT )
            FilterBrushIntoTree_r( face, brush, sidePlanes, node->children[1], tree );
        return;
    }

    if ( node->opaque == 1 )
        return;

    planenum = face->planenum;

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );

        if ( p->nodes[0]->opaque == 1 || p->nodes[1]->opaque == 1 )
            continue;
        if ( WindingIsTiny( p->winding ) )
            continue;
        if ( WindingPlaneSide( p->winding, mapplanes[planenum].normal,
                               mapplanes[planenum].dist ) != SIDE_ON )
            continue;

        w = CopyWinding( p->winding );

        for ( i = 0; i < ( ( const winding_t * )face->visibleHull )->ptCount && w; i++ )
            ClipWindingByPlane( &w, sidePlanes[i], sidePlanes[i][3], PORTAL_CLIP_EPSILON );

        if ( w )
        {
            for ( j = 0; j < w->ptCount; j++ )
                SnapPointToPlanes( sidePlanes,
                                   ( ( const winding_t * )face->visibleHull )->ptCount,
                                   w->pts[j], 1.0f, PORTAL_CLIP_EPSILON );

            FilterBrushIntoTree( p, w, brush, face );
            FreeWinding( w );
        }
    }
}


/* FilterBrushIntoTree  0x004310f0 */
void FilterBrushIntoTree( portal_t *portal, const winding_t *brushFace,
                          Brush_t *realPortalBrush, side_t *realPortalFace )
{
    unsigned int i;
    unsigned int prev;
    vec3_t       dir;
    vec3_t       normal;
    float        dist;
    winding_t   *front;
    winding_t   *back;
    portal_t    *newPortal;

    Assert( realPortalFace );
    Assert( realPortalBrush );

    if ( portal->manualPortalFace )
    {
        if ( portal->manualPortalFace == realPortalFace )
        {
            Assert( portal->manualPortalBrush == realPortalBrush );
        }
        else
        {
            PortalWarning( "first overlapping portal",
                           ( const winding_t * )realPortalFace->winding, realPortalBrush );
            PortalWarning( "second overlapping portal",
                           ( const winding_t * )portal->manualPortalFace->winding,
                           portal->manualPortalBrush );
        }
        return;
    }

    Assert( portal->manualPortalFace == NULL );
    Assert( portal->manualPortalBrush == NULL );

    prev = brushFace->ptCount - 1;
    for ( i = 0; i < brushFace->ptCount; i++ )
    {
        Vec3Sub( brushFace->pts[i], brushFace->pts[prev], dir );

        if ( Vec3LengthSq( dir ) >= PORTAL_CLIP_EPSILON )
        {
            Vec3Cross( portal->plane.normal, dir, normal );
            Vec3Normalize( normal );
            SnapNormal( normal );
            dist = Vec3Dot( normal, brushFace->pts[i] );

            ClipWindingEpsilon( portal->winding, normal, dist, PORTAL_CLIP_EPSILON,
                                &front, &back, qfalse );

            if ( !back )
            {
                Assert( front );
                FreeWinding( front );
            }
            else
            {
                if ( front )
                {
                    newPortal                    = AllocPortal();
                    newPortal->plane             = portal->plane;
                    newPortal->onnode            = portal->onnode;
                    newPortal->winding           = front;
                    newPortal->manualPortalFace  = NULL;
                    newPortal->manualPortalBrush = NULL;
                    AddPortalToNode( newPortal, portal->nodes[0], portal->nodes[1] );
                }

                FreeWinding( portal->winding );
                portal->winding = back;
            }
        }

        prev = i;
    }

    portal->manualPortalFace     = realPortalFace;
    portal->manualPortalBrush    = realPortalBrush;
    realPortalFace->portalIgnored = 0;
}


/* PortalizeWorld  0x00430b90 */
void PortalizeWorld( Brush_t *brushList, Tree_t *tree, int leaked )
{
    Com_DPrintf( "----- Building Portals and Cells-----\n" );
    BuildSidePlanes( brushList, tree );
    PortalizeNode( tree, leaked );

    if ( debugPortals )
        WritePortalMapFile( tree );
}


/* PortalizeNode  0x004314b0 */
void PortalizeNode( Tree_t *tree, int leaked )
{
    numCells = 0;

    InitBspTreeNodes( tree->headnode );

    if ( !leaked )
    {
        tree->outside_node.cellnum = CELLNUM_UNKNOWN;
        FloodOutsideCells_r( &tree->outside_node );
    }

    NumberCells_r( tree->headnode, tree );
    CheckPortalCells_r( tree->headnode );
    PropagateCellnum_r( tree->headnode );

    if ( !numCells )
        Com_Error( "no cells in map; map is probably empty or has "
                   "no structural brushes\n" );

    Com_DPrintf( "%5i unique cells\n", numCells );
}


/* InitBspTreeNodes  0x00431550 */
void InitBspTreeNodes( Node_t *node )
{
    node->cellnum  = -1 - ( node->opaque != 1 );
    node->occupied = 0x7fffffff;

    if ( node->planenum != PLANENUM_LEAF )
    {
        Assert( node->children[0]->parent == node );
        Assert( node->children[1]->parent == node );
        InitBspTreeNodes( node->children[0] );
        InitBspTreeNodes( node->children[1] );
    }
}


/* FloodOutsideCells_r  0x00431600 */
void FloodOutsideCells_r( Node_t *node )
{
    portal_t *p;
    int       s;

    if ( node->cellnum == -1 )
        return;
    if ( node->opaque == 1 )
        return;

    node->opaque  = 1;
    node->cellnum = -1;

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );
        if ( !p->manualPortalFace )
            FloodOutsideCells_r( p->nodes[!s] );
    }
}


/* NumberCells_r  0x00431690 */
void NumberCells_r( Node_t *node, Tree_t *tree )
{
    if ( node->planenum != PLANENUM_LEAF )
    {
        NumberCells_r( node->children[0], tree );
        NumberCells_r( node->children[1], tree );
        return;
    }

    if ( node->cellnum == CELLNUM_UNKNOWN && node->opaque != 1 )
    {
        ClearBounds( bspCells[numCells].mins, bspCells[numCells].maxs );
        FloodCell_r( node, 1, tree );
        numCells++;
    }
}


/* FloodCell_r  0x00431720 */
void FloodCell_r( Node_t *node, int depth, Tree_t *tree )
{
    portal_t *p;
    int       s;
    int       i;

    if ( node->cellnum == numCells )
        return;
    if ( node->opaque == 1 )
        return;

    Assert( node->cellnum == CELLNUM_UNKNOWN );
    node->cellnum = numCells;

    for ( i = 0; i < 3; i++ )
    {
        if ( node->mins[i] < bspCells[node->cellnum].mins[i] )
            bspCells[node->cellnum].mins[i] = node->mins[i];
        if ( bspCells[node->cellnum].maxs[i] < node->maxs[i] )
            bspCells[node->cellnum].maxs[i] = node->maxs[i];
    }

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );

        if ( WindingIsTiny( p->winding ) )
            continue;

        if ( !p->manualPortalFace )
            FloodCell_r( p->nodes[!s], depth + 1, tree );
        else if ( p->nodes[!s]->cellnum == node->cellnum )
            PortalLeakFile( tree, p );
    }
}


/* CheckPortalCells_r  0x004318d0 */
void CheckPortalCells_r( Node_t *node )
{
    portal_t *p;
    int       s;

    if ( node->planenum != PLANENUM_LEAF )
    {
        CheckPortalCells_r( node->children[0] );
        CheckPortalCells_r( node->children[1] );
        return;
    }

    if ( node->opaque == 1 )
        return;

    Assert( node->cellnum != CELLNUM_UNKNOWN );

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );

        if ( p->manualPortalFace
          && p->nodes[0]->cellnum == p->nodes[1]->cellnum
          && !p->manualPortalFace->portalIgnored )
        {
            p->manualPortalFace->portalIgnored = 1;
            PortalWarning( "portal has the same cell on both sides, portal ignored",
                           ( const winding_t * )p->manualPortalFace->visibleHull,
                           p->manualPortalBrush );
        }
    }
}


/* PropagateCellnum_r  0x004319d0 */
int PropagateCellnum_r( Node_t *node )
{
    int c0;
    int c1;

    if ( node->planenum != PLANENUM_LEAF )
    {
        c0 = PropagateCellnum_r( node->children[0] );
        c1 = PropagateCellnum_r( node->children[1] );
        if ( c0 == c1 )
            node->cellnum = c0;
        else
            node->cellnum = CELLNUM_UNKNOWN;
    }

    return node->cellnum;
}


/* GetLeafBrushes  0x004302b0 */
void GetLeafBrushes( Entity_t *ent, Node_t *headnode )
{
    memset( s_cellPortalRefs, 0, sizeof( s_cellPortalRefs ) );
    Assert( s_portalErrorCount == 0 );

    CollectCellPortals_r( headnode );
    EmitPortalsAndCells();

    if ( s_portalErrorCount )
        Com_Error( "aborting due to %i bad portals", s_portalErrorCount );
}


/* CollectCellPortals_r  0x00430320 */
void CollectCellPortals_r( Node_t *node )
{
    portal_t *p;
    int       s;

    if ( node->planenum != PLANENUM_LEAF )
    {
        CollectCellPortals_r( node->children[0] );
        CollectCellPortals_r( node->children[1] );
        return;
    }

    if ( node->opaque == 1 )
        return;

    Assert( node->cellnum >= 0 );

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );
        if ( p->manualPortalFace && !p->manualPortalFace->portalIgnored )
            AddPortalRefToCell( p, s, node->cellnum );
    }
}


/* AddPortalRefToCell  0x004303f0 */
void AddPortalRefToCell( const portal_t *p, int side, int cellnum )
{
    portalRef_t *ref;

    for ( ref = s_cellPortalRefs[cellnum]; ref; ref = ref->next )
    {
        if ( ref->face == p->manualPortalFace )
            return;
    }

    ref               = ( portalRef_t * )malloc( sizeof( portalRef_t ) );
    ref->face         = p->manualPortalFace;
    ref->oppositenode = p->nodes[!side];
    ref->next         = s_cellPortalRefs[cellnum];
    s_cellPortalRefs[cellnum] = ref;
    bspCells[cellnum].portalCount++;

    Assert( p->nodes[0]->cellnum != CELLNUM_UNKNOWN );
    Assert( p->nodes[1]->cellnum != CELLNUM_UNKNOWN );

    if ( p->manualPortalFace->cellOnPortalSide[0] == CELLNUM_UNKNOWN )
    {
        Assert( p->manualPortalFace->cellOnPortalSide[1] == CELLNUM_UNKNOWN );
        p->manualPortalFace->cellOnPortalSide[0] = p->nodes[0]->cellnum;
        p->manualPortalFace->cellOnPortalSide[1] = p->nodes[1]->cellnum;
    }
    else
    {
        Assert( p->manualPortalFace->cellOnPortalSide[1] != CELLNUM_UNKNOWN );

        if ( p->manualPortalFace->cellOnPortalSide[0] != p->nodes[0]->cellnum
          || p->manualPortalFace->cellOnPortalSide[1] != p->nodes[1]->cellnum )
        {
            PortalWarning( "portal sees into two cells (aka portal t-junction)",
                           ( const winding_t * )p->manualPortalFace->winding,
                           p->manualPortalBrush );
            s_portalErrorCount++;
        }
    }
}


/* EmitPortalsAndCells  0x00430610 */
void EmitPortalsAndCells( void )
{
    int          i;
    unsigned int j;
    int          bit;
    portalRef_t *ref;
    winding_t   *w;
    int          portalIndex;

    numBSPPortals          = 0;
    numBSPCullGroupIndices = 0;

    for ( i = 0; i < numCells; i++ )
    {
        bspCells[i].firstPortal = numBSPPortals;

        for ( ref = s_cellPortalRefs[i]; ref; ref = ref->next )
        {
            w = ( winding_t * )ref->face->winding;
            RemoveColinearPoints( w );

            portalIndex = numBSPPortals;
            numBSPPortals++;

            if ( ref->oppositenode->cellnum == -1 )
            {
                PortalWarning( "portal is flush against only solid geometry",
                               ( const winding_t * )ref->face->winding,
                               ref->face->brush );
                s_portalErrorCount++;
            }
            else
            {
                AssertIn( ref->oppositenode->cellnum, numCells );
            }

            bspPortals[portalIndex].planeIndex      = SelectPortalPlane( ref->face,
                                                                        ref->oppositenode ) ^ 1;
            bspPortals[portalIndex].cellIndex       = ref->oppositenode->cellnum;
            bspPortals[portalIndex].firstPortalVert = numBSPPortalVerts;
            bspPortals[portalIndex].portalVertCount = w->ptCount;

            mapplanes[bspPortals[portalIndex].planeIndex].flags = 1;

            if ( bspPortals[portalIndex].planeIndex == ref->face->planenum )
            {
                for ( j = 0; j < w->ptCount; j++ )
                {
                    Vec3Copy( w->pts[j], bspPortalVerts[numBSPPortalVerts] );
                    numBSPPortalVerts++;
                }
            }
            else
            {
                j = w->ptCount;
                while ( j-- , ( int )j >= 0 )
                {
                    Vec3Copy( w->pts[j], bspPortalVerts[numBSPPortalVerts] );
                    numBSPPortalVerts++;
                }
            }
        }

        bspCells[i].firstCullGroup = numBSPCullGroupIndices;
        bspCells[i].cullGroupCount = 0;

        for ( j = 0; ( int )j < numCullGroups; j++ )
        {
            bit = i * MAX_MAP_CULLGROUPS + j;
            if ( s_cellCullGroupBits[bit >> 3] & ( 1 << ( bit & 7 ) ) )
            {
                bspCullGroupIndices[numBSPCullGroupIndices] = j;
                numBSPCullGroupIndices++;
                bspCells[i].cullGroupCount++;
            }
        }
    }
}


/* SelectPortalPlane  0x00430910 */
int SelectPortalPlane( const side_t *face, const Node_t *node )
{
    float  epsilon;
    int    planenum;
    vec3_t center;

    for ( epsilon = SELECT_PLANE_EPSILON; epsilon > SELECT_PLANE_MIN_EPSILON;
          epsilon = ( float )( epsilon * 0.5 ) )
    {
        planenum = SelectSplitPlane( face->planenum, node, epsilon );
        if ( planenum != -1 )
            return planenum;
    }

    WindingCenter( ( const winding_t * )face->winding, center );
    Com_Error( "couldn't determine plane for portal; shouldn't happen unless there "
               "is a bsp leaf with no volume\nportal center = (%g %g %g)\n",
               center[0], center[1], center[2] );
    return -1;
}


/* SelectSplitPlane  0x004309b0 */
int SelectSplitPlane( int planenum, const Node_t *node, float epsilon )
{
    portal_t     *p;
    unsigned int  i;
    float         d;
    int           s;

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );

        for ( i = 0; i < p->winding->ptCount; i++ )
        {
            d = Vec3Dot( p->winding->pts[i], mapplanes[planenum].normal )
              - mapplanes[planenum].dist;

            if ( d < -epsilon )
                return planenum;
            if ( d > epsilon )
                return planenum ^ 1;
        }
    }

    return -1;
}


/* RemapPortalPlanes  0x00430a80 */
void RemapPortalPlanes( const int *planeMap )
{
    int i;

    Assert( planeMap );

    for ( i = 0; i < numBSPPortals; i++ )
    {
        Assert( bspPortals[i].planeIndex >= 0 && bspPortals[i].planeIndex < nummapplanes );
        Assert( planeMap[bspPortals[i].planeIndex] >= 0
             && planeMap[bspPortals[i].planeIndex] < numBSPPlanes );
        bspPortals[i].planeIndex = planeMap[bspPortals[i].planeIndex];
    }
}


/* WritePortalMapFile  0x00431a30 */
void WritePortalMapFile( Tree_t *tree )
{
    char  filename[MAX_OS_PATH];
    FILE *f;

    Assert( tree );

    sprintf( filename, "%s_portals.map", g_outputBasePath );
    f = fopen( filename, "w" );
    if ( f )
    {
        fprintf( f, "iwmap %i\n{\n\"classname\" \"worldspawn\"\n", 4 );
        WritePortalMapFile_r( tree->headnode, f );
        fprintf( f, "}\n" );
        fclose( f );
    }
}


/* WritePortalMapFile_r  0x00431b00 */
void WritePortalMapFile_r( const Node_t *node, FILE *f )
{
    portal_t   *p;
    int         s;
    const char *material;

    if ( node->planenum != PLANENUM_LEAF )
    {
        WritePortalMapFile_r( node->children[0], f );
        WritePortalMapFile_r( node->children[1], f );
        return;
    }

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );
        if ( s )
            continue;

        if ( p->manualPortalFace )
            material = "portal";
        else if ( p->nodes[0]->opaque == 1 || p->nodes[1]->opaque == 1 )
            material = "portal_debug_solid";
        else
            material = "portal_debug_trans";

        WritePortalBrush( p->winding, p, material, f );
    }
}


/* WritePortalBrush  0x00431bd0 */
void WritePortalBrush( const winding_t *w, const portal_t *p, const char *material, FILE *f )
{
    vec3_t       offset;
    vec3_t       verts[3];
    unsigned int i;

    Vec3Scale( p->plane.normal, 1.0f, offset );

    fprintf( f, "{\n" );

    Vec3Copy( w->pts[2], verts[0] );
    Vec3Copy( w->pts[1], verts[1] );
    Vec3Copy( w->pts[0], verts[2] );
    WriteBrushFace( verts, material, f );

    for ( i = 0; i < w->ptCount; i++ )
    {
        Vec3Copy( w->pts[i], verts[0] );
        Vec3Copy( w->pts[( i + 1 ) % w->ptCount], verts[1] );
        Vec3Add( w->pts[i], offset, verts[2] );
        WriteBrushFace( verts, "portal_nodraw", f );
    }

    Vec3Add( w->pts[0], offset, verts[0] );
    Vec3Add( w->pts[1], offset, verts[1] );
    Vec3Add( w->pts[2], offset, verts[2] );
    WriteBrushFace( verts, "portal_nodraw", f );

    fprintf( f, "}\n" );
}


/* WriteBrushFace  0x00431d50 */
void WriteBrushFace( const vec3_t *verts, const char *material, FILE *f )
{
    int i;

    for ( i = 0; i < 3; i++ )
        fprintf( f, "( %g %g %g ) ", verts[i][0], verts[i][1], verts[i][2] );

    fprintf( f, "%s %i %i 0 0.25 0.25 0 0\n", material, rand() & 63, rand() & 63 );
}


int   num_visclusters;              /* 0x12f19ca8 */
int   num_solidfaces;               /* 0x12f19cac */
int   num_visportals;               /* 0x12f19cb0 */
FILE *pf;                           /* 0x12f19cb4 */


/* WriteFloat  0x00439400 */
void WriteFloat( FILE *f, float v )
{
    if ( fabs( v - Q_rint( v ) ) < 0.001 )
        fprintf( f, "%i ", ( int )Q_rint( v ) );
    else
        fprintf( f, "%f ", v );
}


/* WritePortalFile_r  0x00439480 */
void WritePortalFile_r( const Node_t *node )
{
    portal_t     *p;
    winding_t    *w;
    vec4_t        plane;
    unsigned int  i;
    int           s;

    if ( node->planenum != PLANENUM_LEAF )
    {
        WritePortalFile_r( node->children[0] );
        WritePortalFile_r( node->children[1] );
        return;
    }

    if ( node->opaque == 1 )
        return;

    for ( p = node->portals; p; p = p->next[s] )
    {
        w = p->winding;
        s = ( p->nodes[1] == node );

        if ( !w || p->nodes[0] != node )
            continue;
        if ( !Portal_Passable( p ) )
            continue;

        PlaneFromWindingTriangle( w, plane );

        if ( Vec3Dot( p->plane.normal, plane ) < 0.99f )
            fprintf( pf, "%i %i %i ", w->ptCount,
                     p->nodes[1]->cluster, p->nodes[0]->cluster );
        else
            fprintf( pf, "%i %i %i ", w->ptCount,
                     p->nodes[0]->cluster, p->nodes[1]->cluster );

        for ( i = 0; i < w->ptCount; i++ )
        {
            fprintf( pf, "(" );
            WriteFloat( pf, w->pts[i][0] );
            WriteFloat( pf, w->pts[i][1] );
            WriteFloat( pf, w->pts[i][2] );
            fprintf( pf, ") " );
        }
        fprintf( pf, "\n" );
    }
}


/* NumberLeafs_r  0x00439680 */
void NumberLeafs_r( Node_t *node )
{
    portal_t *p;

    if ( node->planenum != PLANENUM_LEAF )
    {
        node->cluster = -99;
        NumberLeafs_r( node->children[0] );
        NumberLeafs_r( node->children[1] );
        return;
    }

    node->area = -1;

    if ( node->opaque == 1 )
    {
        node->cluster = -1;
        return;
    }

    node->cluster = num_visclusters;
    num_visclusters++;

    for ( p = node->portals; p; )
    {
        if ( p->nodes[0] == node )
        {
            if ( Portal_Passable( p ) )
                num_visportals++;
            else
                num_solidfaces++;
            p = p->next[0];
        }
        else
        {
            if ( !Portal_Passable( p ) )
                num_solidfaces++;
            p = p->next[1];
        }
    }
}


/* NumberClusters  0x00439780 */
void NumberClusters( Tree_t *tree )
{
    num_visclusters = 0;
    num_visportals  = 0;
    num_solidfaces  = 0;

    Com_DPrintf( "--- NumberClusters ---\n" );
    NumberLeafs_r( tree->headnode );

    Com_DPrintf( "%5i visclusters\n", num_visclusters );
    Com_DPrintf( "%5i visportals\n",  num_visportals );
    Com_DPrintf( "%5i solidfaces\n",  num_solidfaces );
}


/* WritePortalFile  0x00439800 */
void WritePortalFile( Tree_t *tree )
{
    char filename[MAX_OS_PATH];

    Com_DPrintf( "--- WritePortalFile ---\n" );

    sprintf( filename, "%s%s", g_outputBasePath, GetPRTFileExtension() );
    Com_Printf( "writing %s\n", filename );

    pf = fopen( filename, "w" );
    if ( !pf )
        Com_Error( "Error opening %s", filename );

    fprintf( pf, "%s\n", PORTALFILE );
    fprintf( pf, "%i\n", num_visclusters );
    fprintf( pf, "%i\n", num_visportals );
    fprintf( pf, "%i\n", num_solidfaces );

    WritePortalFile_r( tree->headnode );
    WriteFaceFile_r( tree->headnode );

    fclose( pf );
}


/* WriteFaceFile_r  0x00439930 */
void WriteFaceFile_r( const Node_t *node )
{
    portal_t     *p;
    winding_t    *w;
    unsigned int  i;
    int           s;

    if ( node->planenum != PLANENUM_LEAF )
    {
        WriteFaceFile_r( node->children[0] );
        WriteFaceFile_r( node->children[1] );
        return;
    }

    if ( node->opaque == 1 )
        return;

    for ( p = node->portals; p; p = p->next[s] )
    {
        s = ( p->nodes[1] == node );
        w = p->winding;

        if ( !w || Portal_Passable( p ) )
            continue;

        if ( p->nodes[0] == node )
        {
            fprintf( pf, "%i %i ", w->ptCount, p->nodes[0]->cluster );
            for ( i = 0; i < w->ptCount; i++ )
            {
                fprintf( pf, "(" );
                WriteFloat( pf, w->pts[i][0] );
                WriteFloat( pf, w->pts[i][1] );
                WriteFloat( pf, w->pts[i][2] );
                fprintf( pf, ") " );
            }
            fprintf( pf, "\n" );
        }
        else
        {
            fprintf( pf, "%i %i ", w->ptCount, p->nodes[1]->cluster );
            for ( i = 0; i < w->ptCount; i++ )
            {
                fprintf( pf, "(" );
                WriteFloat( pf, w->pts[w->ptCount - 1 - i][0] );
                WriteFloat( pf, w->pts[w->ptCount - 1 - i][1] );
                WriteFloat( pf, w->pts[w->ptCount - 1 - i][2] );
                fprintf( pf, ") " );
            }
            fprintf( pf, "\n" );
        }
    }
}
