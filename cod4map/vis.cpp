/* Original: .\vis.cpp */

#include "vis.h"

#include "cod4map.h"
#include "bsp.h"
#include "portals.h"
#include "qsort_vc8.h"

extern qboolean ValidatePlatformSet( void );                            /* 0x0043b3d0 */
extern void     BSP_SetTargetPlatform( void );                          /* 0x00408d90 */
extern void     FS_InitInstallDir( void );                              /* 0x004088e0 */
extern void     ThreadSetDefault( void );                               /* 0x0043bf80 */
extern void     RunThreadsOnIndividual( int workcnt, qboolean showpacifier,
                                        void ( *func )( int ) );        /* 0x0043bf10 */
extern int      numthreads;                                             /* 0x0052964c */

static void VisPlaneFromWinding( const winding_t *w, visPlane_t plane );

int         g_visMerge;         /* 0x2b820a10 */
int         numportals;         /* 0x2b820a18 */
char        inbase[32];         /* 0x2b820a1c */
int         leafbytes;          /* 0x2b820a40 */
int         portalbytes;        /* 0x2b820a44 */
int         portalclusters;     /* 0x2b820a4c */
int         leaflongs;          /* 0x2b820a50 */
leaf_t     *leafs;              /* 0x2b820a54 */
int         noPassageVis;       /* 0x2b820a58 */
leaf_t     *faceleafs;          /* 0x2b820a5c */
int         portallongs;        /* 0x2b820a60 */
int         fastvis;            /* 0x2b820a68 */
vportal_t  *portals;            /* 0x2b820a6c */
int         numfaces;           /* 0x2b820a70 */
int         saveprt;            /* 0x2b820a74 */
vportal_t  *sorted_portals[MAX_PORTALS * 2];    /* 0x2b820a78 */
int         nosort;             /* 0x2b920a78 */
vportal_t  *faces;              /* 0x2b920a7c */
int         testlevel;          /* 0x005296ac */
int         c_vis;              /* 0x2b920a90 */
int         c_flood;            /* 0x2b920a9c */
int         totalvis;           /* 0x2b820a48 */


/* NewWinding  0x0045e920 */
winding_t *NewWinding( int points )
{
    winding_t *w;
    int        size;

    if ( points > MAX_POINTS_ON_VIS_WINDING )
        Com_Error( "NewWinding: %i pts", points );

    size = points * sizeof( vec3_t ) + sizeof( int );
    w = ( winding_t * )malloc( size );
    memset( w, 0, size );

    return w;
}


/* PortalCompare  0x0045ea20 */
static int PortalCompare( const void *a, const void *b )
{
    const vportal_t *pa;
    const vportal_t *pb;

    pa = *( const vportal_t * const * )a;
    pb = *( const vportal_t * const * )b;

    if ( pa->nummightsee == pb->nummightsee )
        return 0;
    if ( pa->nummightsee < pb->nummightsee )
        return -1;
    return 1;
}


/* SortPortals  0x0045ea60 */
void SortPortals( void )
{
    int i;

    for ( i = 0; i < numportals * 2; i++ )
        sorted_portals[i] = &portals[i];

    if ( !nosort )
        qsort_vc8( sorted_portals, numportals * 2, sizeof( sorted_portals[0] ), PortalCompare );
}


/* LeafVectorFromPortalVector  0x0045ead0 */
static int LeafVectorFromPortalVector( const byte *portalbits, byte *leafbits )
{
    int        i;
    int        j;
    int        leafnum;
    vportal_t *p;

    for ( i = 0; i < numportals * 2; i++ )
    {
        if ( portalbits[i >> 3] & ( 1 << ( i & 7 ) ) )
        {
            p = &portals[i];
            leafbits[p->leaf >> 3] |= 1 << ( p->leaf & 7 );
        }
    }

    for ( j = 0; j < portalclusters; j++ )
    {
        leafnum = j;
        while ( leafs[leafnum].merged >= 0 )
            leafnum = leafs[leafnum].merged;

        if ( leafbits[leafnum >> 3] & ( 1 << ( leafnum & 7 ) ) )
            leafbits[j >> 3] |= 1 << ( j & 7 );
    }

    return CountBits( leafbits, portalclusters );
}


/* ClusterMerge  0x0045ec10 */
void ClusterMerge( int leafnum )
{
    leaf_t    *leaf;
    byte       portalvector[MAX_PORTALS / 8];
    byte       uncompressed[MAX_MAP_LEAFS / 8];
    int        i;
    int        j;
    int        numvis;
    int        mergedleafnum;
    vportal_t *p;
    int        pnum;

    mergedleafnum = leafnum;
    while ( leafs[mergedleafnum].merged >= 0 )
        mergedleafnum = leafs[mergedleafnum].merged;

    memset( portalvector, 0, portalbytes );
    leaf = &leafs[mergedleafnum];

    for ( i = 0; i < leaf->numportals; i++ )
    {
        p = leaf->portals[i];
        if ( p->removed )
            continue;

        if ( p->status != stat_done )
            Com_Error( "portal not done" );

        for ( j = 0; j < portallongs; j++ )
            ( ( int * )portalvector )[j] |= ( ( const int * )p->portalvis )[j];

        pnum = ( int )( p - portals );
        portalvector[pnum >> 3] |= 1 << ( pnum & 7 );
    }

    memset( uncompressed, 0, leafbytes );
    uncompressed[mergedleafnum >> 3] |= 1 << ( mergedleafnum & 7 );

    numvis = LeafVectorFromPortalVector( portalvector, uncompressed );
    numvis++;

    totalvis += numvis;

    Com_DPrintf( "cluster %4i : %4i visible\n", leafnum, numvis );

    memcpy( bspVisBytes + VIS_HEADER_SIZE + leafnum * leafbytes, uncompressed, leafbytes );
}


/* CalcFullVis  0x0045ee40 */
void CalcFullVis( void )
{
    RunThreadsOnIndividual( numportals * 2, qtrue, PortalFlow );
}


/* CalcPassageFullVis  0x0045ee60 */
void CalcPassageFullVis( void )
{
    PassageMemory();
    RunThreadsOnIndividual( numportals * 2, qtrue, CreatePassages );
    RunThreadsOnIndividual( numportals * 2, qtrue, PassagePortalFlow );
}


/* CalcFastVis  0x0045eea0 */
void CalcFastVis( void )
{
    int i;

    for ( i = 0; i < numportals * 2; i++ )
    {
        portals[i].portalvis = portals[i].portalflood;
        portals[i].status    = stat_done;
    }
}


/* CalcVis  0x0045ef00 */
void CalcVis( void )
{
    int i;

    RunThreadsOnIndividual( numportals * 2, qtrue, BasePortalVis );

    SortPortals();

    if ( fastvis )
        CalcFastVis();
    else if ( noPassageVis )
        CalcFullVis();
    else
        CalcPassageFullVis();

    Com_Printf( "creating leaf vis...\n" );

    for ( i = 0; i < portalclusters; i++ )
        ClusterMerge( i );

    Com_Printf( "Average clusters visible: %i\n", totalvis / portalclusters );
}


/* SetPortalSphere  0x0045efa0 */
void SetPortalSphere( vportal_t *p )
{
    unsigned int i;
    winding_t   *w;
    vec3_t       total;
    vec3_t       dist;
    float        r;
    float        bestr;

    w = p->winding;
    Vec3Copy( vec3_origin, total );

    for ( i = 0; i < w->ptCount; i++ )
        Vec3Add( total, w->pts[i], total );

    for ( i = 0; i < 3; i++ )
        total[i] /= w->ptCount;

    bestr = 0;
    for ( i = 0; i < w->ptCount; i++ )
    {
        Vec3Sub( w->pts[i], total, dist );
        r = Vec3Length( dist );
        if ( r > bestr )
            bestr = r;
    }

    Vec3Copy( total, p->origin );
    p->radius = bestr;
}


/* Winding_PlanesConcave  0x0045f0c0 */
static qboolean Winding_PlanesConcave( const winding_t *w1, const winding_t *w2,
                                       const vec3_t normal1, const vec3_t normal2,
                                       float dist1, float dist2 )
{
    unsigned int i;

    if ( !w1 || !w2 )
        return qfalse;

    for ( i = 0; i < w1->ptCount; i++ )
    {
        if ( Vec3Dot( normal2, w1->pts[i] ) - dist2 > WCONVEX_EPSILON )
            return qtrue;
    }

    for ( i = 0; i < w2->ptCount; i++ )
    {
        if ( Vec3Dot( normal1, w2->pts[i] ) - dist1 > WCONVEX_EPSILON )
            return qtrue;
    }

    return qfalse;
}


/* MergeLeaf  0x0045f180 */
int MergeLeaf( int l1num, int l2num )
{
    int        i;
    int        j;
    int        k;
    int        n;
    int        numPortals;
    vportal_t *p1;
    vportal_t *p2;
    vportal_t *portalList[MAX_PORTALS_ON_LEAF];
    leaf_t    *l1;
    leaf_t    *l2;
    visPlane_t plane1;
    visPlane_t plane2;

    for ( k = 0; k < 2; k++ )
    {
        if ( k )
            l1 = &leafs[l1num];
        else
            l1 = &faceleafs[l1num];

        for ( i = 0; i < l1->numportals; i++ )
        {
            p1 = l1->portals[i];
            if ( p1->leaf == l2num )
                continue;

            for ( n = 0; n < 2; n++ )
            {
                if ( n )
                    l2 = &leafs[l2num];
                else
                    l2 = &faceleafs[l2num];

                for ( j = 0; j < l2->numportals; j++ )
                {
                    p2 = l2->portals[j];
                    if ( p2->leaf == l1num )
                        continue;

                    Vec4Copy( p1->plane, plane1 );
                    Vec4Copy( p2->plane, plane2 );

                    if ( Winding_PlanesConcave( p1->winding, p2->winding,
                                                plane1, plane2, plane1[3], plane2[3] ) )
                        return qfalse;
                }
            }
        }
    }

    for ( k = 0; k < 2; k++ )
    {
        if ( k )
        {
            l1 = &leafs[l1num];
            l2 = &leafs[l2num];
        }
        else
        {
            l1 = &faceleafs[l1num];
            l2 = &faceleafs[l2num];
        }

        numPortals = 0;

        for ( i = 0; i < l1->numportals; i++ )
        {
            p1 = l1->portals[i];
            if ( p1->leaf == l2num )
            {
                p1->removed = qtrue;
                continue;
            }
            portalList[numPortals++] = p1;
        }

        for ( j = 0; j < l2->numportals; j++ )
        {
            p2 = l2->portals[j];
            if ( p2->leaf == l1num )
            {
                p2->removed = qtrue;
                continue;
            }
            portalList[numPortals++] = p2;
        }

        for ( n = 0; n < numPortals; n++ )
            l2->portals[n] = portalList[n];

        l2->numportals = numPortals;
        l1->merged     = l2num;
    }

    return qtrue;
}


/* VisMain  0x00460100 */
void VisMain( int argc, const char **argv )
{
    char   bspName[MAX_OS_PATH];
    char   prtName[MAX_OS_PATH];
    double start;
    double end;
    int    i;

    Com_Printf( "---- vis ----\n" );

    verbose = 0;

    for ( i = 1; i < argc; i++ )
    {
        if ( !strcmp( argv[i], "-threads" ) )
        {
            numthreads = atoi( argv[i + 1] );
            i++;
        }
        else if ( !strcmp( argv[i], "-threads" ) )
        {
            numthreads = atoi( argv[i + 1] );
            i++;
        }
        else if ( !strcmp( argv[i], "-fast" ) )
        {
            Com_Printf( "fastvis = true\n" );
            fastvis = 1;
        }
        else if ( !strcmp( argv[i], "-merge" ) )
        {
            Com_Printf( "merge = true\n" );
            g_visMerge = 1;
        }
        else if ( !strcmp( argv[i], "-nopassage" ) )
        {
            Com_Printf( "nopassage = true\n" );
            noPassageVis = 1;
        }
        else if ( !strcmp( argv[i], "-level" ) )
        {
            testlevel = atoi( argv[i + 1] );
            Com_Printf( "testlevel = %i\n", testlevel );
            i++;
        }
        else if ( !strcmp( argv[i], "-v" ) )
        {
            Com_Printf( "verbose = true\n" );
            verbose = 1;
        }
        else if ( !strcmp( argv[i], "-nosort" ) )
        {
            Com_Printf( "nosort = true\n" );
            nosort = 1;
        }
        else if ( !strcmp( argv[i], "-saveprt" ) )
        {
            Com_Printf( "saveprt = true\n" );
            saveprt = 1;
        }
        else if ( !strcmp( argv[i], "-tmpin" ) )
        {
            strcpy( inbase, "/tmp" );
        }
        else if ( !_stricmp( argv[i], "-platform" ) )
        {
            i++;
            SetTargetPlatformByName( argv[i] );
        }
        else if ( argv[i][0] != '-' )
        {
            break;
        }
        else
        {
            Com_Error( "Unknown option \"%s\"", argv[i] );
        }
    }

    if ( i != argc - 1 )
        Com_Error( "usage: vis [-threads #] [-level 0-4] [-fast] [-v] bspfile" );

    if ( !ValidatePlatformSet() )
        exit( -1 );

    BSP_SetTargetPlatform();

    start = I_FloatTime();

    ThreadSetDefault();
    FS_Startup( argv[0] );
    FS_InitInstallDir();

    sprintf( bspName, "%s%s", inbase, ExpandArg( argv[i] ) );
    StripExtension( bspName );
    strcat( bspName, GetBSPFileExtension() );
    Com_Printf( "reading %s\n", bspName );
    LoadBSPFileLumps( bspName, qfalse );
    ParseEntities();

    sprintf( prtName, "%s%s", inbase, ExpandArg( argv[i] ) );
    StripExtension( prtName );
    strcat( prtName, GetPRTFileExtension() );
    Com_Printf( "reading %s\n", prtName );

    LoadPortals( prtName );

    if ( g_visMerge )
    {
        MergeLeaves();
        MergeLeafPortals();
    }

    CountActivePortals();

    Com_Printf( "visdatasize:%i\n", numBSPVisBytes );
    if ( numBSPVisBytes > MAX_MAP_VISIBILITY )
        Com_Error( "MAX_MAP_VISIBILITY (%i) exceeded\n", MAX_MAP_VISIBILITY );

    CalcVis();

    Com_Printf( "writing %s\n", bspName );
    Assert( targetPlatform != PLATFORM_VOID );
    WriteBSPFile( bspName );

    end = I_FloatTime();
    Com_Printf( "%5.2f seconds elapsed\n", end - start );
}


/* LoadPortals  0x00460680 */
void LoadPortals( const char *name )
{
    FILE         *f;
    int           i;
    unsigned int  j;
    vportal_t    *p;
    leaf_t       *l;
    char          magic[80];
    unsigned int  numpoints;
    unsigned int  leafnums[2];
    visPlane_t    plane;
    double        v[3];
    int           k;
    winding_t    *w;

    if ( !strcmp( name, "-" ) )
        f = stdin;
    else
    {
        f = fopen( name, "r" );
        if ( !f )
            Com_Error( "LoadPortals: couldn't read %s\n", name );
    }

    if ( fscanf( f, "%79s\n%i\n%i\n%i\n", magic, &portalclusters, &numportals, &numfaces ) != 4 )
        Com_Error( "LoadPortals: failed to read header" );
    if ( strcmp( magic, PORTALFILE ) )
        Com_Error( "LoadPortals: not a portal file" );

    Com_Printf( "%6i portalclusters\n", portalclusters );
    Com_Printf( "%6i numportals\n",     numportals );
    Com_Printf( "%6i numfaces\n",       numfaces );

    leafbytes   = ( ( portalclusters + 63 ) & ~63 ) >> 3;
    leaflongs   = leafbytes / sizeof( long );
    portalbytes = ( ( numportals * 2 + 63 ) & ~63 ) >> 3;
    portallongs = portalbytes / sizeof( long );

    portals = ( vportal_t * )malloc( 2 * numportals * sizeof( vportal_t ) );
    memset( portals, 0, 2 * numportals * sizeof( vportal_t ) );

    leafs = ( leaf_t * )malloc( portalclusters * sizeof( leaf_t ) );
    memset( leafs, 0, portalclusters * sizeof( leaf_t ) );

    for ( i = 0; i < portalclusters; i++ )
        leafs[i].merged = -1;

    numBSPVisBytes = VIS_HEADER_SIZE + portalclusters * leafbytes;

    ( ( int * )bspVisBytes )[0] = portalclusters;
    ( ( int * )bspVisBytes )[1] = leafbytes;

    p = portals;
    for ( i = 0; i < numportals; i++ )
    {
        if ( fscanf( f, "%i %i %i ", &numpoints, &leafnums[0], &leafnums[1] ) != 3 )
            Com_Error( "LoadPortals: reading portal %i", i );
        if ( numpoints > MAX_POINTS_ON_VIS_WINDING )
            Com_Error( "LoadPortals: portal %i has too many pts", i );
        if ( leafnums[0] > ( unsigned int )portalclusters
          || leafnums[1] > ( unsigned int )portalclusters )
            Com_Error( "LoadPortals: reading portal %i", i );

        w = NewWinding( numpoints );
        p->winding  = w;
        w->ptCount  = numpoints;

        for ( j = 0; j < numpoints; j++ )
        {
            if ( fscanf( f, "(%lf %lf %lf ) ", &v[0], &v[1], &v[2] ) != 3 )
                Com_Error( "LoadPortals: reading portal %i", i );
            for ( k = 0; k < 3; k++ )
                w->pts[j][k] = ( float )v[k];
        }
        fscanf( f, "\n" );

        VisPlaneFromWinding( w, plane );

        l = &leafs[leafnums[0]];
        if ( l->numportals == MAX_PORTALS_ON_LEAF )
            Com_Error( "Leaf with too many portals" );
        l->portals[l->numportals] = p;
        l->numportals++;

        p->num     = i + 1;
        p->winding = w;
        Vec3Sub( vec3_origin, plane, p->plane );
        p->plane[3] = -plane[3];
        p->leaf     = leafnums[1];
        SetPortalSphere( p );
        p++;

        l = &leafs[leafnums[1]];
        if ( l->numportals == MAX_PORTALS_ON_LEAF )
            Com_Error( "Leaf with too many portals" );
        l->portals[l->numportals] = p;
        l->numportals++;

        p->num     = i + 1;
        p->winding = NewWinding( w->ptCount );
        p->winding->ptCount = w->ptCount;
        for ( j = 0; j < w->ptCount; j++ )
            Vec3Copy( w->pts[w->ptCount - 1 - j], p->winding->pts[j] );

        p->plane[0] = plane[0];
        p->plane[1] = plane[1];
        p->plane[2] = plane[2];
        p->plane[3] = plane[3];
        p->leaf     = leafnums[0];
        SetPortalSphere( p );
        p++;
    }

    faces = ( vportal_t * )malloc( 2 * numfaces * sizeof( vportal_t ) );
    memset( faces, 0, 2 * numfaces * sizeof( vportal_t ) );

    faceleafs = ( leaf_t * )malloc( portalclusters * sizeof( leaf_t ) );
    memset( faceleafs, 0, portalclusters * sizeof( leaf_t ) );

    p = faces;
    for ( i = 0; i < numfaces; i++ )
    {
        if ( fscanf( f, "%i %i ", &numpoints, &leafnums[0] ) != 2 )
            Com_Error( "LoadPortals: reading portal %i", i );

        w = NewWinding( numpoints );
        p->winding = w;
        w->ptCount = numpoints;

        for ( j = 0; j < numpoints; j++ )
        {
            if ( fscanf( f, "(%lf %lf %lf ) ", &v[0], &v[1], &v[2] ) != 3 )
                Com_Error( "LoadPortals: reading portal %i", i );
            for ( k = 0; k < 3; k++ )
                w->pts[j][k] = ( float )v[k];
        }
        fscanf( f, "\n" );

        VisPlaneFromWinding( w, plane );

        l = &faceleafs[leafnums[0]];
        l->merged = -1;
        if ( l->numportals == MAX_PORTALS_ON_LEAF )
            Com_Error( "Leaf with too many faces" );
        l->portals[l->numportals] = p;
        l->numportals++;

        p->num     = i + 1;
        p->winding = w;
        Vec3Sub( vec3_origin, plane, p->plane );
        p->plane[3] = -plane[3];
        p->leaf     = -1;
        SetPortalSphere( p );
        p++;
    }

    fclose( f );
}


/* VisPlaneFromWinding  0x00460e80 */
static void VisPlaneFromWinding( const winding_t *w, visPlane_t plane )
{
    vec3_t v1;
    vec3_t v2;

    Vec3Sub( w->pts[2], w->pts[1], v1 );
    Vec3Sub( w->pts[0], w->pts[1], v2 );
    Vec3Cross( v2, v1, plane );
    Vec3Normalize( plane );
    plane[3] = Vec3Dot( w->pts[0], plane );
}


/* CountBits  0x00460f00 */
int CountBits( const byte *bits, int numbits )
{
    int i;
    int c;

    c = 0;
    for ( i = 0; i < numbits; i++ )
    {
        if ( bits[i >> 3] & ( 1 << ( i & 7 ) ) )
            c++;
    }
    return c;
}


/* CountActivePortals  0x0045fc70 */
int CountActivePortals( void )
{
    vportal_t *p;
    int        num;
    int        i;

    num = 0;
    for ( i = 0; i < numportals * 2; i++ )
    {
        p = portals + i;
        if ( p->removed )
            continue;
        num++;
    }

    Com_Printf( "%6d active portals\n", num );
    return num;
}


/* WritePortalFile  0x0045fce0 */
void WritePortalFile( const char *filename )
{
    FILE         *pf;
    vportal_t    *p;
    winding_t    *w;
    int           i;
    int           num;
    unsigned int  j;

    pf = fopen( filename, "w" );
    if ( !pf )
        Com_Error( "Error opening %s", filename );

    num = 0;
    for ( i = 0; i < numportals * 2; i++ )
    {
        p = portals + i;
        if ( p->removed )
            continue;
        num++;
    }

    fprintf( pf, "%s\n", PORTALFILE );
    fprintf( pf, "%i\n", 0 );
    fprintf( pf, "%i\n", num );
    fprintf( pf, "%i\n", 0 );

    for ( i = 0; i < numportals * 2; i++ )
    {
        p = portals + i;
        if ( p->removed )
            continue;

        w = p->winding;
        fprintf( pf, "%i %i %i ", w->ptCount, 0, 0 );
        for ( j = 0; j < w->ptCount; j++ )
        {
            fprintf( pf, "(" );
            WriteFloat( pf, w->pts[j][0] );
            WriteFloat( pf, w->pts[j][1] );
            WriteFloat( pf, w->pts[j][2] );
            fprintf( pf, ") " );
        }
        fprintf( pf, "\n" );
    }

    fclose( pf );
}


/* UpdatePortals  0x0045f580 */
void UpdatePortals( void )
{
    vportal_t *p;
    int        i;

    for ( i = 0; i < numportals * 2; i++ )
    {
        p = portals + i;
        if ( p->removed )
            continue;
        while ( leafs[p->leaf].merged >= 0 )
            p->leaf = leafs[p->leaf].merged;
    }
}


/* MergeLeaves  0x0045f600 */
void MergeLeaves( void )
{
    leaf_t *leaf;
    int     i;
    int     j;
    int     nummerges;
    int     totalmerges;

    totalmerges = 0;
    while ( 1 )
    {
        nummerges = 0;
        for ( i = 0; i < portalclusters; i++ )
        {
            leaf = &leafs[i];
            if ( leaf->merged >= 0 )
                continue;

            for ( j = 0; j < leaf->numportals; j++ )
            {
                if ( leaf->portals[j]->removed )
                    continue;
                if ( !MergeLeaf( i, leaf->portals[j]->leaf ) )
                    continue;
                UpdatePortals();
                nummerges++;
                break;
            }
        }

        totalmerges += nummerges;
        if ( !nummerges )
            break;
    }

    Com_Printf( "%6d leaves merged\n", totalmerges );
}


/* TryMergeWinding  0x0045f6e0 */
winding_t *TryMergeWinding( const winding_t *f1, const winding_t *f2,
                            const vec3_t planenormal )
{
    const float  *p1;
    const float  *p2;
    const float  *p3;
    const float  *p4;
    const float  *back;
    winding_t    *newf;
    unsigned int  i;
    unsigned int  j;
    unsigned int  k;
    unsigned int  l;
    vec3_t        normal;
    vec3_t        delta;
    float         dot;
    int           keep1;
    int           keep2;

    p1 = NULL;
    p2 = NULL;
    j  = 0;

    for ( i = 0; i < f1->ptCount; i++ )
    {
        p1 = f1->pts[i];
        p2 = f1->pts[( i + 1 ) % f1->ptCount];

        for ( j = 0; j < f2->ptCount; j++ )
        {
            p3 = f2->pts[j];
            p4 = f2->pts[( j + 1 ) % f2->ptCount];

            for ( k = 0; k < 3; k++ )
            {
                if ( fabs( p1[k] - p4[k] ) > VIS_MERGE_EPSILON )
                    break;
                if ( fabs( p2[k] - p3[k] ) > VIS_MERGE_EPSILON )
                    break;
            }
            if ( k == 3 )
                break;
        }
        if ( j < f2->ptCount )
            break;
    }

    if ( i == f1->ptCount )
        return NULL;

    back = f1->pts[( ( i - 1 ) + f1->ptCount ) % f1->ptCount];
    Vec3Sub( p1, back, delta );
    Vec3Cross( planenormal, delta, normal );
    Vec3Normalize( normal );

    back = f2->pts[( j + 2 ) % f2->ptCount];
    Vec3Sub( back, p1, delta );
    dot = Vec3Dot( delta, normal );
    if ( dot > CONTINUOUS_EPSILON )
        return NULL;
    keep1 = ( dot < -CONTINUOUS_EPSILON );

    back = f1->pts[( i + 2 ) % f1->ptCount];
    Vec3Sub( back, p2, delta );
    Vec3Cross( planenormal, delta, normal );
    Vec3Normalize( normal );

    back = f2->pts[( ( j - 1 ) + f2->ptCount ) % f2->ptCount];
    Vec3Sub( back, p2, delta );
    dot = Vec3Dot( delta, normal );
    if ( dot > CONTINUOUS_EPSILON )
        return NULL;
    keep2 = ( dot < -CONTINUOUS_EPSILON );

    newf = NewWinding( f1->ptCount + f2->ptCount );

    for ( k = ( i + 1 ) % f1->ptCount; k != i; k = ( k + 1 ) % f1->ptCount )
    {
        if ( k == ( i + 1 ) % f1->ptCount && !keep2 )
            continue;
        Vec3Copy( f1->pts[k], newf->pts[newf->ptCount] );
        newf->ptCount++;
    }

    for ( l = ( j + 1 ) % f2->ptCount; l != j; l = ( l + 1 ) % f2->ptCount )
    {
        if ( l == ( j + 1 ) % f2->ptCount && !keep1 )
            continue;
        Vec3Copy( f2->pts[l], newf->pts[newf->ptCount] );
        newf->ptCount++;
    }

    return newf;
}


/* MergeLeafPortals  0x0045fb00 */
void MergeLeafPortals( void )
{
    leaf_t    *leaf;
    vportal_t *p1;
    vportal_t *p2;
    winding_t *w;
    int        i;
    int        j;
    int        k;
    int        nummerges;

    nummerges = 0;

    for ( i = 0; i < portalclusters; i++ )
    {
        leaf = &leafs[i];
        if ( leaf->merged >= 0 )
            continue;

        for ( j = 0; j < leaf->numportals; j++ )
        {
            p1 = leaf->portals[j];
            if ( p1->removed )
                continue;

            for ( k = j + 1; k < leaf->numportals; k++ )
            {
                p2 = leaf->portals[k];
                if ( p2->removed )
                    continue;
                if ( p1->leaf != p2->leaf )
                    continue;

                w = TryMergeWinding( p1->winding, p2->winding, p1->plane );
                if ( !w )
                    continue;

                FreeWinding( p1->winding );
                p1->winding = w;
                SetPortalSphere( p1 );
                p2->removed = 1;
                nummerges++;
                i--;
                break;
            }
            if ( k < leaf->numportals )
                break;
        }
    }

    Com_Printf( "%6d portals merged\n", nummerges );
}


/* BuildPHS  0x0045fed0 */
void BuildPHS( void )
{
    unsigned int  uncompressed[1025];
    const byte   *scan;
    int           i;
    int           j;
    int           k;
    int           l;
    int           index;
    unsigned int  bitbyte;
    int           count;

    Com_Printf( "Building PHS...\n" );

    count = 0;
    for ( i = 0; i < portalclusters; i++ )
    {
        scan = bspVisBytes + i * leafbytes;
        memcpy( uncompressed, scan, leafbytes );

        for ( j = 0; j < leafbytes; j++ )
        {
            bitbyte = scan[j];
            if ( !bitbyte )
                continue;

            for ( k = 0; k < 8; k++ )
            {
                if ( !( ( 1 << k ) & bitbyte ) )
                    continue;

                index = k + j * 8;
                if ( index >= portalclusters )
                    Com_Error( "Bad bit in PVS" );

                for ( l = 0; l < leaflongs; l++ )
                    uncompressed[l] |= ( ( const unsigned int * )( bspVisBytes + l * 4 + index * leafbytes ) )[0];
            }
        }

        for ( j = 0; j < portalclusters; j++ )
        {
            if ( ( ( const byte * )uncompressed )[j >> 3] & ( 1 << ( j & 7 ) ) )
                count++;
        }
    }

    Com_Printf( "Average clusters hearable: %i\n", count / portalclusters );
}


/* CheckStack  0x00460f60 */
void CheckStack( const leaf_t *leaf, const pstack_t *thread )
{
    const pstack_t *p;
    const pstack_t *p2;

    for ( p = thread->next; p; p = p->next )
    {
        if ( p->leaf == leaf )
            Com_Error( "CheckStack: leaf recursion" );
        for ( p2 = thread->next; p2 != p; p2 = p2->next )
        {
            if ( p2->leaf == p->leaf )
                Com_Error( "CheckStack: late leaf recursion" );
        }
    }
}


/* AllocStackWinding  0x00460ff0 */
stackWinding_t *AllocStackWinding( pstack_t *stack )
{
    int i;

    for ( i = 0; i < 3; i++ )
    {
        if ( stack->freewindings[i] )
        {
            stack->freewindings[i] = 0;
            return &stack->windings[i];
        }
    }

    Com_Error( "AllocStackWinding: failed" );
    return NULL;
}


/* FreeStackWinding  0x00461060 */
void FreeStackWinding( stackWinding_t *w, pstack_t *stack )
{
    int i;

    i = w - stack->windings;

    if ( i < 0 || i > 2 )
        return;

    if ( stack->freewindings[i] )
        Com_Error( "FreeStackWinding: allready free" );

    stack->freewindings[i] = 1;
}


/* VisChopWinding  0x004610c0 */
stackWinding_t *VisChopWinding( stackWinding_t *in, pstack_t *stack, const visPlane_t split )
{
    float           dists[128];
    int             sides[128];
    int             counts[3];
    float           dot;
    unsigned int    i;
    unsigned int    j;
    const float    *p1;
    const float    *p2;
    vec3_t          mid;
    stackWinding_t *neww;

    counts[0] = 0;
    counts[1] = 0;
    counts[2] = 0;

    for ( i = 0; i < in->ptCount; i++ )
    {
        dot      = Vec3Dot( in->pts[i], split ) - split[3];
        dists[i] = dot;
        if ( dot > VIS_ON_EPSILON )
            sides[i] = SIDE_FRONT;
        else if ( dot < -VIS_ON_EPSILON )
            sides[i] = SIDE_BACK;
        else
            sides[i] = SIDE_ON;
        counts[sides[i]]++;
    }

    if ( !counts[1] )
        return in;

    if ( !counts[0] )
    {
        FreeStackWinding( in, stack );
        return NULL;
    }

    sides[i] = sides[0];
    dists[i] = dists[0];

    neww = AllocStackWinding( stack );
    neww->ptCount = 0;

    for ( i = 0; i < in->ptCount; i++ )
    {
        p1 = in->pts[i];

        if ( neww->ptCount == MAX_POINTS_ON_FIXED_WINDING )
        {
            FreeStackWinding( neww, stack );
            return in;
        }

        if ( sides[i] == SIDE_ON )
        {
            Vec3Copy( p1, neww->pts[neww->ptCount] );
            neww->ptCount++;
            continue;
        }

        if ( sides[i] == SIDE_FRONT )
        {
            Vec3Copy( p1, neww->pts[neww->ptCount] );
            neww->ptCount++;
        }

        if ( sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i] )
            continue;

        if ( neww->ptCount == MAX_POINTS_ON_FIXED_WINDING )
        {
            FreeStackWinding( neww, stack );
            return in;
        }

        p2  = in->pts[( i + 1 ) % in->ptCount];
        dot = dists[i] / ( dists[i] - dists[i + 1] );

        for ( j = 0; j < 3; j++ )
        {
            if ( split[j] == 1.0f )
                mid[j] = split[3];
            else if ( split[j] == -1.0f )
                mid[j] = -split[3];
            else
                mid[j] = ( p2[j] - p1[j] ) * dot + p1[j];
        }

        Vec3Copy( mid, neww->pts[neww->ptCount] );
        neww->ptCount++;
    }

    FreeStackWinding( in, stack );
    return neww;
}


/* ClipToSeperators  0x004614b0 */
stackWinding_t *ClipToSeperators( const stackWinding_t *source, const stackWinding_t *pass,
                                  stackWinding_t *target, int flipclip, pstack_t *stack )
{
    unsigned int i;
    unsigned int j;
    unsigned int k;
    unsigned int l;
    vec3_t       v1;
    vec3_t       v2;
    visPlane_t   plane;
    float        d;
    float        length;
    int          counts[3];
    int          fliptest;

    for ( i = 0; i < source->ptCount; i++ )
    {
        l = ( i + 1 ) % source->ptCount;
        Vec3Sub( source->pts[l], source->pts[i], v1 );

        for ( j = 0; j < pass->ptCount; j++ )
        {
            Vec3Sub( pass->pts[j], source->pts[i], v2 );

            plane[0] = v1[1] * v2[2] - v1[2] * v2[1];
            plane[1] = v1[2] * v2[0] - v1[0] * v2[2];
            plane[2] = v1[0] * v2[1] - v1[1] * v2[0];

            length = plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2];
            if ( length < VIS_ON_EPSILON )
                continue;

            length = 1.0f / sqrt( length );
            plane[0] *= length;
            plane[1] *= length;
            plane[2] *= length;
            plane[3]  = Vec3Dot( pass->pts[j], plane );

            fliptest = 0;
            for ( k = 0; k < source->ptCount; k++ )
            {
                if ( k == i || k == l )
                    continue;
                d = Vec3Dot( source->pts[k], plane ) - plane[3];
                if ( d < -VIS_ON_EPSILON )
                {
                    fliptest = 0;
                    break;
                }
                if ( d > VIS_ON_EPSILON )
                {
                    fliptest = 1;
                    break;
                }
            }
            if ( k == source->ptCount )
                continue;

            if ( fliptest )
            {
                Vec3Sub( vec3_origin, plane, plane );
                plane[3] = -plane[3];
            }

            counts[0] = 0;
            counts[1] = 0;
            counts[2] = 0;
            for ( k = 0; k < pass->ptCount; k++ )
            {
                if ( k == j )
                    continue;
                d = Vec3Dot( pass->pts[k], plane ) - plane[3];
                if ( d < -VIS_ON_EPSILON )
                    break;
                if ( d > VIS_ON_EPSILON )
                    counts[0]++;
                else
                    counts[2]++;
            }
            if ( k != pass->ptCount )
                continue;
            if ( !counts[0] )
                continue;

            if ( flipclip )
            {
                Vec3Sub( vec3_origin, plane, plane );
                plane[3] = -plane[3];
            }

            Vec4Copy( plane, stack->seperators[flipclip][stack->numseperators[flipclip]] );
            stack->numseperators[flipclip]++;
            if ( stack->numseperators[flipclip] >= MAX_SEPERATORS )
                Com_Error( "MAX_SEPERATORS" );

            d = Vec3Dot( stack->portal->origin, plane ) - plane[3];
            if ( d < -stack->portal->radius )
                return NULL;
            if ( d <= stack->portal->radius )
            {
                target = VisChopWinding( target, stack, plane );
                if ( !target )
                    return NULL;
            }
            break;
        }
    }

    return target;
}


/* RecursiveLeafFlow  0x00461890 */
void RecursiveLeafFlow( int leafnum, threaddata_t *thread, pstack_t *prevstack )
{
    pstack_t    stack;
    vportal_t  *p;
    visPlane_t  backplane;
    leaf_t     *leaf;
    int         i;
    int         j;
    int         n;
    long       *test;
    long       *might;
    long       *vis;
    long        more;
    int         pnum;
    float       d;

    thread->c_chains++;

    leaf = &leafs[leafnum];

    prevstack->next = &stack;
    stack.next      = NULL;
    stack.leaf      = leaf;
    stack.portal    = NULL;
    stack.depth     = prevstack->depth + 1;

    stack.numseperators[0] = 0;
    stack.numseperators[1] = 0;

    might = ( long * )stack.mightsee;
    vis   = ( long * )thread->base->portalvis;

    for ( i = 0; i < leaf->numportals; i++ )
    {
        p = leaf->portals[i];
        if ( p->removed )
            continue;
        pnum = p - portals;

        if ( !( prevstack->mightsee[pnum >> 3] & ( 1 << ( pnum & 7 ) ) ) )
            continue;

        if ( p->status == stat_done )
            test = ( long * )p->portalvis;
        else
            test = ( long * )p->portalflood;

        more = 0;
        for ( j = 0; j < portallongs; j++ )
        {
            might[j] = ( ( long * )prevstack->mightsee )[j] & test[j];
            more |= ( might[j] & ~vis[j] );
        }

        if ( !more && ( thread->base->portalvis[pnum >> 3] & ( 1 << ( pnum & 7 ) ) ) )
            continue;

        Vec4Copy( p->plane, stack.portalplane );
        Vec3Sub( vec3_origin, p->plane, backplane );
        backplane[3] = -p->plane[3];

        stack.portal          = p;
        stack.next            = NULL;
        stack.freewindings[0] = 1;
        stack.freewindings[1] = 1;
        stack.freewindings[2] = 1;

        d = Vec3Dot( p->origin, thread->pstack_head.portalplane ) - thread->pstack_head.portalplane[3];
        if ( d < -p->radius )
            continue;
        if ( d > p->radius )
        {
            stack.pass = ( stackWinding_t * )p->winding;
        }
        else
        {
            stack.pass = VisChopWinding( ( stackWinding_t * )p->winding, &stack,
                                      thread->pstack_head.portalplane );
            if ( !stack.pass )
                continue;
        }

        d = Vec3Dot( thread->base->origin, p->plane ) - p->plane[3];
        if ( d > thread->base->radius )
            continue;
        if ( d < -thread->base->radius )
        {
            stack.source = prevstack->source;
        }
        else
        {
            stack.source = VisChopWinding( prevstack->source, &stack, backplane );
            if ( !stack.source )
                continue;
        }

        if ( !prevstack->pass )
        {
            thread->base->portalvis[pnum >> 3] |= ( 1 << ( pnum & 7 ) );
            RecursiveLeafFlow( p->leaf, thread, &stack );
            continue;
        }

        if ( stack.numseperators[0] )
        {
            for ( n = 0; n < stack.numseperators[0]; n++ )
            {
                stack.pass = VisChopWinding( stack.pass, &stack, stack.seperators[0][n] );
                if ( !stack.pass )
                    break;
            }
            if ( n < stack.numseperators[0] )
                continue;
        }
        else
        {
            stack.pass = ClipToSeperators( prevstack->source, prevstack->pass,
                                           stack.pass, 0, &stack );
        }
        if ( !stack.pass )
            continue;

        if ( stack.numseperators[1] )
        {
            for ( n = 0; n < stack.numseperators[1]; n++ )
            {
                stack.pass = VisChopWinding( stack.pass, &stack, stack.seperators[1][n] );
                if ( !stack.pass )
                    break;
            }
        }
        else
        {
            stack.pass = ClipToSeperators( prevstack->pass, prevstack->source,
                                           stack.pass, 1, &stack );
        }
        if ( !stack.pass )
            continue;

        thread->base->portalvis[pnum >> 3] |= ( 1 << ( pnum & 7 ) );

        RecursiveLeafFlow( p->leaf, thread, &stack );
        stack.next = NULL;
    }
}


/* PortalFlow  0x00461e40 */
void PortalFlow( int portalnum )
{
    threaddata_t data;
    vportal_t   *p;
    int          i;
    int          c_might;
    int          c_can;

    p = sorted_portals[portalnum];

    if ( p->removed )
    {
        p->status = stat_done;
        return;
    }

    p->status = stat_working;

    c_might = CountBits( p->portalflood, numportals * 2 );

    memset( &data, 0, sizeof( data ) );
    data.base = p;

    data.pstack_head.portal = p;
    data.pstack_head.source = ( stackWinding_t * )p->winding;
    Vec4Copy( p->plane, data.pstack_head.portalplane );
    data.pstack_head.depth = 0;

    for ( i = 0; i < portallongs; i++ )
        ( ( long * )data.pstack_head.mightsee )[i] = ( ( long * )p->portalflood )[i];

    RecursiveLeafFlow( p->leaf, &data, &data.pstack_head );

    p->status = stat_done;

    c_can = CountBits( p->portalvis, numportals * 2 );

    Com_DPrintf( "portal:%4i  mightsee:%4i  cansee:%4i (%i chains)\n",
                 ( int )( p - portals ), c_might, c_can, data.c_chains );
}


/* RecursivePassageFlow  0x00461fd0 */
void RecursivePassageFlow( vportal_t *portal, threaddata_t *thread, pstack_t *prevstack )
{
    pstack_t   stack;
    vportal_t *p;
    leaf_t    *leaf;
    passage_t *passage;
    passage_t *nextpassage;
    int        i;
    int        j;
    long      *might;
    long      *vis;
    long      *prevmight;
    long      *cansee;
    long      *portalvis;
    long       more;
    int        pnum;

    leaf = &leafs[portal->leaf];

    prevstack->next = &stack;

    stack.next  = NULL;
    stack.depth = prevstack->depth + 1;

    vis = ( long * )thread->base->portalvis;

    passage     = portal->passages;
    nextpassage = passage;
    for ( i = 0; i < leaf->numportals; i++, passage = nextpassage )
    {
        p = leaf->portals[i];
        if ( p->removed )
            continue;

        nextpassage = passage->next;
        pnum        = p - portals;

        if ( !( prevstack->mightsee[pnum >> 3] & ( 1 << ( pnum & 7 ) ) ) )
            continue;

        prevmight = ( long * )prevstack->mightsee;
        cansee    = ( long * )passage->cansee;
        might     = ( long * )stack.mightsee;
        memcpy( might, prevmight, portalbytes );

        if ( p->status == stat_done )
            portalvis = ( long * )p->portalvis;
        else
            portalvis = ( long * )p->portalflood;

        more = 0;
        for ( j = 0; j < portallongs; j++ )
        {
            if ( *might )
            {
                *might = *cansee & *portalvis & *might;
                portalvis++;
                cansee++;
                more |= ( *might & ~vis[j] );
            }
            else
            {
                cansee++;
                portalvis++;
            }
            might++;
        }

        if ( !more && ( thread->base->portalvis[pnum >> 3] & ( 1 << ( pnum & 7 ) ) ) )
            continue;

        thread->base->portalvis[pnum >> 3] |= ( 1 << ( pnum & 7 ) );
        RecursivePassageFlow( p, thread, &stack );

        stack.next = NULL;
    }
}


/* PassageFlow  0x00462260 */
void PassageFlow( int portalnum )
{
    threaddata_t data;
    vportal_t   *p;
    int          i;

    p = sorted_portals[portalnum];

    if ( p->removed )
    {
        p->status = stat_done;
        return;
    }

    p->status = stat_working;

    memset( &data, 0, sizeof( data ) );
    data.base = p;

    data.pstack_head.portal = p;
    data.pstack_head.source = ( stackWinding_t * )p->winding;
    Vec4Copy( p->plane, data.pstack_head.portalplane );
    data.pstack_head.depth = 0;

    for ( i = 0; i < portallongs; i++ )
        ( ( long * )data.pstack_head.mightsee )[i] = ( ( long * )p->portalflood )[i];

    RecursivePassageFlow( p, &data, &data.pstack_head );

    p->status = stat_done;
}


/* RecursivePassagePortalFlow  0x00462380 */
void RecursivePassagePortalFlow( vportal_t *portal, threaddata_t *thread, pstack_t *prevstack )
{
    pstack_t    stack;
    vportal_t  *p;
    leaf_t     *leaf;
    visPlane_t  backplane;
    passage_t  *passage;
    passage_t  *nextpassage;
    int         i;
    int         j;
    int         n;
    long       *might;
    long       *vis;
    long       *prevmight;
    long       *cansee;
    long       *portalvis;
    long        more;
    int         pnum;
    float       d;

    leaf = &leafs[portal->leaf];

    prevstack->next = &stack;

    stack.next   = NULL;
    stack.leaf   = leaf;
    stack.portal = NULL;
    stack.depth  = prevstack->depth + 1;

    stack.numseperators[0] = 0;
    stack.numseperators[1] = 0;

    vis = ( long * )thread->base->portalvis;

    passage     = portal->passages;
    nextpassage = passage;
    for ( i = 0; i < leaf->numportals; i++, passage = nextpassage )
    {
        p = leaf->portals[i];
        if ( p->removed )
            continue;

        nextpassage = passage->next;
        pnum        = p - portals;

        if ( !( prevstack->mightsee[pnum >> 3] & ( 1 << ( pnum & 7 ) ) ) )
            continue;

        prevmight = ( long * )prevstack->mightsee;
        cansee    = ( long * )passage->cansee;
        might     = ( long * )stack.mightsee;
        memcpy( might, prevmight, portalbytes );

        if ( p->status == stat_done )
            portalvis = ( long * )p->portalvis;
        else
            portalvis = ( long * )p->portalflood;

        more = 0;
        for ( j = 0; j < portallongs; j++ )
        {
            if ( *might )
            {
                *might = *cansee & *portalvis & *might;
                portalvis++;
                cansee++;
                more |= ( *might & ~vis[j] );
            }
            else
            {
                cansee++;
                portalvis++;
            }
            might++;
        }

        if ( !more && ( thread->base->portalvis[pnum >> 3] & ( 1 << ( pnum & 7 ) ) ) )
            continue;

        Vec4Copy( p->plane, stack.portalplane );
        Vec3Sub( vec3_origin, p->plane, backplane );
        backplane[3] = -p->plane[3];

        stack.portal          = p;
        stack.next            = NULL;
        stack.freewindings[0] = 1;
        stack.freewindings[1] = 1;
        stack.freewindings[2] = 1;

        d = Vec3Dot( p->origin, thread->pstack_head.portalplane ) - thread->pstack_head.portalplane[3];
        if ( d < -p->radius )
            continue;
        if ( d > p->radius )
        {
            stack.pass = ( stackWinding_t * )p->winding;
        }
        else
        {
            stack.pass = VisChopWinding( ( stackWinding_t * )p->winding, &stack,
                                         thread->pstack_head.portalplane );
            if ( !stack.pass )
                continue;
        }

        d = Vec3Dot( thread->base->origin, p->plane ) - p->plane[3];
        if ( d > thread->base->radius )
            continue;
        if ( d < -thread->base->radius )
        {
            stack.source = prevstack->source;
        }
        else
        {
            stack.source = VisChopWinding( prevstack->source, &stack, backplane );
            if ( !stack.source )
                continue;
        }

        if ( !prevstack->pass )
        {
            thread->base->portalvis[pnum >> 3] |= ( 1 << ( pnum & 7 ) );
            RecursivePassagePortalFlow( p, thread, &stack );
            continue;
        }

        if ( stack.numseperators[0] )
        {
            for ( n = 0; n < stack.numseperators[0]; n++ )
            {
                stack.pass = VisChopWinding( stack.pass, &stack, stack.seperators[0][n] );
                if ( !stack.pass )
                    break;
            }
            if ( n < stack.numseperators[0] )
                continue;
        }
        else
        {
            stack.pass = ClipToSeperators( prevstack->source, prevstack->pass,
                                           stack.pass, 0, &stack );
        }
        if ( !stack.pass )
            continue;

        if ( stack.numseperators[1] )
        {
            for ( n = 0; n < stack.numseperators[1]; n++ )
            {
                stack.pass = VisChopWinding( stack.pass, &stack, stack.seperators[1][n] );
                if ( !stack.pass )
                    break;
            }
        }
        else
        {
            stack.pass = ClipToSeperators( prevstack->pass, prevstack->source,
                                           stack.pass, 1, &stack );
        }
        if ( !stack.pass )
            continue;

        thread->base->portalvis[pnum >> 3] |= ( 1 << ( pnum & 7 ) );

        RecursivePassagePortalFlow( p, thread, &stack );
        stack.next = NULL;
    }
}


/* PassagePortalFlow  0x004629a0 */
void PassagePortalFlow( int portalnum )
{
    threaddata_t data;
    vportal_t   *p;
    int          i;

    p = sorted_portals[portalnum];

    if ( p->removed )
    {
        p->status = stat_done;
        return;
    }

    p->status = stat_working;

    memset( &data, 0, sizeof( data ) );
    data.base = p;

    data.pstack_head.portal = p;
    data.pstack_head.source = ( stackWinding_t * )p->winding;
    Vec4Copy( p->plane, data.pstack_head.portalplane );
    data.pstack_head.depth = 0;

    for ( i = 0; i < portallongs; i++ )
        ( ( long * )data.pstack_head.mightsee )[i] = ( ( long * )p->portalflood )[i];

    RecursivePassagePortalFlow( p, &data, &data.pstack_head );

    p->status = stat_done;
}


/* ChopWinding  0x00462ac0 */
stackWinding_t *ChopWinding( stackWinding_t *in, stackWinding_t *neww, const visPlane_t split )
{
    float           dists[128];
    int             sides[128];
    int             counts[3];
    float           dot;
    unsigned int    i;
    unsigned int    j;
    const float    *p1;
    const float    *p2;
    vec3_t          mid;

    counts[0] = 0;
    counts[1] = 0;
    counts[2] = 0;

    for ( i = 0; i < in->ptCount; i++ )
    {
        dot      = Vec3Dot( in->pts[i], split ) - split[3];
        dists[i] = dot;
        if ( dot > VIS_ON_EPSILON )
            sides[i] = SIDE_FRONT;
        else if ( dot < -VIS_ON_EPSILON )
            sides[i] = SIDE_BACK;
        else
            sides[i] = SIDE_ON;
        counts[sides[i]]++;
    }

    if ( !counts[1] )
        return in;

    if ( !counts[0] )
        return NULL;

    sides[i] = sides[0];
    dists[i] = dists[0];

    neww->ptCount = 0;

    for ( i = 0; i < in->ptCount; i++ )
    {
        p1 = in->pts[i];

        if ( neww->ptCount == MAX_POINTS_ON_FIXED_WINDING )
            return in;

        if ( sides[i] == SIDE_ON )
        {
            Vec3Copy( p1, neww->pts[neww->ptCount] );
            neww->ptCount++;
            continue;
        }

        if ( sides[i] == SIDE_FRONT )
        {
            Vec3Copy( p1, neww->pts[neww->ptCount] );
            neww->ptCount++;
        }

        if ( sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i] )
            continue;

        if ( neww->ptCount == MAX_POINTS_ON_FIXED_WINDING )
            return in;

        p2  = in->pts[( i + 1 ) % in->ptCount];
        dot = dists[i] / ( dists[i] - dists[i + 1] );

        for ( j = 0; j < 3; j++ )
        {
            if ( split[j] == 1.0f )
                mid[j] = split[3];
            else if ( split[j] == -1.0f )
                mid[j] = -split[3];
            else
                mid[j] = ( p2[j] - p1[j] ) * dot + p1[j];
        }

        Vec3Copy( mid, neww->pts[neww->ptCount] );
        neww->ptCount++;
    }

    return neww;
}


/* AddSeperators  0x00462e60 */
int AddSeperators( const stackWinding_t *source, const stackWinding_t *pass,
                   qboolean flipclip, visPlane_t *seperators, int maxseperators )
{
    unsigned int i;
    unsigned int j;
    unsigned int k;
    unsigned int l;
    vec3_t       v1;
    vec3_t       v2;
    visPlane_t   plane;
    float        d;
    float        length;
    int          counts[3];
    int          numseperators;
    qboolean     fliptest;

    numseperators = 0;

    for ( i = 0; i < source->ptCount; i++ )
    {
        l = ( i + 1 ) % source->ptCount;
        Vec3Sub( source->pts[l], source->pts[i], v1 );

        for ( j = 0; j < pass->ptCount; j++ )
        {
            Vec3Sub( pass->pts[j], source->pts[i], v2 );

            plane[0] = v1[1] * v2[2] - v1[2] * v2[1];
            plane[1] = v1[2] * v2[0] - v1[0] * v2[2];
            plane[2] = v1[0] * v2[1] - v1[1] * v2[0];

            length = plane[0] * plane[0] + plane[1] * plane[1] + plane[2] * plane[2];
            if ( length < VIS_ON_EPSILON )
                continue;

            length = 1.0f / sqrt( length );
            plane[0] *= length;
            plane[1] *= length;
            plane[2] *= length;
            plane[3]  = Vec3Dot( pass->pts[j], plane );

            fliptest = qfalse;
            for ( k = 0; k < source->ptCount; k++ )
            {
                if ( k == i || k == l )
                    continue;
                d = Vec3Dot( source->pts[k], plane ) - plane[3];
                if ( d < -VIS_ON_EPSILON )
                {
                    fliptest = qfalse;
                    break;
                }
                if ( d > VIS_ON_EPSILON )
                {
                    fliptest = qtrue;
                    break;
                }
            }
            if ( k == source->ptCount )
                continue;

            if ( fliptest )
            {
                Vec3Sub( vec3_origin, plane, plane );
                plane[3] = -plane[3];
            }

            counts[0] = 0;
            counts[1] = 0;
            counts[2] = 0;
            for ( k = 0; k < pass->ptCount; k++ )
            {
                if ( k == j )
                    continue;
                d = Vec3Dot( pass->pts[k], plane ) - plane[3];
                if ( d < -VIS_ON_EPSILON )
                    break;
                if ( d > VIS_ON_EPSILON )
                    counts[0]++;
                else
                    counts[2]++;
            }
            if ( k != pass->ptCount )
                continue;
            if ( !counts[0] )
                continue;

            if ( flipclip )
            {
                Vec3Sub( vec3_origin, plane, plane );
                plane[3] = -plane[3];
            }

            if ( numseperators >= maxseperators )
                Com_Error( "max seperators" );

            Vec4Copy( plane, seperators[numseperators] );
            numseperators++;
            break;
        }
    }

    return numseperators;
}


/* CreatePassages  0x004631a0 */
void CreatePassages( int portalnum )
{
    int             i, j, k, n;
    int             numsep;
    int             numpassages;
    vportal_t      *portal;
    vportal_t      *p;
    vportal_t      *target;
    leaf_t         *leaf;
    passage_t      *passage;
    passage_t      *lastpassage;
    visPlane_t      seperators[MAX_SEPERATORS * 2];
    stackWinding_t *w;
    stackWinding_t  in;
    stackWinding_t  out;
    stackWinding_t *res;
    float           d;

    portal = sorted_portals[portalnum];

    if ( portal->removed )
    {
        portal->status = stat_done;
        return;
    }

    lastpassage = NULL;
    leaf        = &leafs[portal->leaf];

    for ( i = 0; i < leaf->numportals; i++ )
    {
        target = leaf->portals[i];
        if ( target->removed )
            continue;

        passage = ( passage_t * )malloc( sizeof( passage_t ) + portalbytes );
        memset( passage, 0, sizeof( passage_t ) + portalbytes );

        numsep  = AddSeperators( ( stackWinding_t * )portal->winding,
                                 ( stackWinding_t * )target->winding,
                                 qfalse, seperators, MAX_SEPERATORS * 2 );
        numsep += AddSeperators( ( stackWinding_t * )target->winding,
                                 ( stackWinding_t * )portal->winding,
                                 qtrue, &seperators[numsep], MAX_SEPERATORS * 2 - numsep );

        passage->next = NULL;
        if ( lastpassage )
            lastpassage->next = passage;
        else
            portal->passages = passage;
        lastpassage = passage;

        numpassages = 0;
        for ( j = 0; j < numportals * 2; j++ )
        {
            p = &portals[j];
            if ( p->removed )
                continue;
            if ( !( target->portalflood[j >> 3] & ( 1 << ( j & 7 ) ) ) )
                continue;
            if ( !( portal->portalflood[j >> 3] & ( 1 << ( j & 7 ) ) ) )
                continue;

            for ( k = 0; k < numsep; k++ )
            {
                d = Vec3Dot( p->origin, seperators[k] ) - seperators[k][3];
                if ( d < -p->radius + VIS_ON_EPSILON )
                    break;

                w = ( stackWinding_t * )p->winding;
                for ( n = 0; ( unsigned int )n < w->ptCount; n++ )
                {
                    d = Vec3Dot( w->pts[n], seperators[k] ) - seperators[k][3];
                    if ( d > VIS_ON_EPSILON )
                        break;
                }
                if ( ( unsigned int )n < w->ptCount )
                    break;
            }
            if ( k < numsep )
                continue;

            memcpy( &in, p->winding, sizeof( stackWinding_t ) );
            for ( k = 0; k < numsep; k++ )
            {
                res = ChopWinding( &in, &out, seperators[k] );
                if ( res == &out )
                    memcpy( &in, &out, sizeof( stackWinding_t ) );
                if ( res == NULL )
                    break;
            }
            if ( k < numsep )
                continue;

            passage->cansee[j >> 3] |= ( 1 << ( j & 7 ) );
            numpassages++;
        }
    }
}


/* PassageMemory  0x004635d0 */
void PassageMemory( void )
{
    int        i, j;
    int        portalmemory;
    int        numpassages;
    vportal_t *portal;
    vportal_t *p;
    leaf_t    *leaf;

    portalmemory = 0;
    numpassages  = 0;

    for ( i = 0; i < numportals; i++ )
    {
        portal = sorted_portals[i];
        if ( portal->removed )
            continue;
        leaf = &leafs[portal->leaf];
        for ( j = 0; j < leaf->numportals; j++ )
        {
            p = leaf->portals[j];
            if ( p->removed )
                continue;
            portalmemory += sizeof( passage_t ) + portalbytes;
            numpassages++;
        }
    }

    if ( numportals )
    {
        Com_Printf( "%7i average number of passages per leaf\n", numpassages / numportals );
        Com_Printf( "%7i MB required passage memory\n", portalmemory >> 20 );
    }
}


/* SimpleFlood  0x004636d0 */
void SimpleFlood( vportal_t *srcportal, int leafnum )
{
    int        i;
    leaf_t    *leaf;
    vportal_t *p;
    int        pnum;

    leaf = &leafs[leafnum];

    for ( i = 0; i < leaf->numportals; i++ )
    {
        p = leaf->portals[i];
        if ( p->removed )
            continue;
        pnum = p - portals;

        if ( !( srcportal->portalfront[pnum >> 3] & ( 1 << ( pnum & 7 ) ) ) )
            continue;

        if ( srcportal->portalflood[pnum >> 3] & ( 1 << ( pnum & 7 ) ) )
            continue;

        srcportal->portalflood[pnum >> 3] |= ( 1 << ( pnum & 7 ) );

        SimpleFlood( srcportal, p->leaf );
    }
}


/* BasePortalVis  0x004637d0 */
void BasePortalVis( int portalnum )
{
    int             j;
    unsigned int    k;
    vportal_t      *tp;
    vportal_t      *p;
    float           d;
    stackWinding_t *w;
    vec3_t          delta;
    float           fogclip;

    p = portals + portalnum;
    if ( p->removed )
        return;

    p->portalfront = ( byte * )malloc( portalbytes );
    memset( p->portalfront, 0, portalbytes );

    p->portalflood = ( byte * )malloc( portalbytes );
    memset( p->portalflood, 0, portalbytes );

    p->portalvis = ( byte * )malloc( portalbytes );
    memset( p->portalvis, 0, portalbytes );

    fogclip = FloatForKey( &entities[0], "fogclip" );

    for ( j = 0, tp = portals; j < numportals * 2; j++, tp++ )
    {
        if ( j == portalnum )
            continue;
        if ( tp->removed )
            continue;

        if ( fogclip != 0.0 )
        {
            Vec3Sub( p->origin, tp->origin, delta );
            if ( Vec3Length( delta ) - p->radius - tp->radius > fogclip )
                continue;
        }

        w = ( stackWinding_t * )tp->winding;
        for ( k = 0; k < w->ptCount; k++ )
        {
            d = Vec3Dot( w->pts[k], p->plane ) - p->plane[3];
            if ( d > VIS_ON_EPSILON )
                break;
        }
        if ( k == w->ptCount )
            continue;

        w = ( stackWinding_t * )p->winding;
        for ( k = 0; k < w->ptCount; k++ )
        {
            d = Vec3Dot( w->pts[k], tp->plane ) - tp->plane[3];
            if ( d < -VIS_ON_EPSILON )
                break;
        }
        if ( k == w->ptCount )
            continue;

        p->portalfront[j >> 3] |= ( 1 << ( j & 7 ) );
    }

    SimpleFlood( p, p->leaf );

    p->nummightsee = CountBits( p->portalflood, numportals * 2 );
    c_flood += p->nummightsee;
}


/* RecursiveLeafBitFlow  0x00463a90 */
void RecursiveLeafBitFlow( int leafnum, const byte *mightsee, byte *cansee )
{
    vportal_t *p;
    leaf_t    *leaf;
    int        i;
    int        j;
    long       more;
    int        pnum;
    byte       newmight[MAX_PORTALS / 8];

    leaf = &leafs[leafnum];

    for ( i = 0; i < leaf->numportals; i++ )
    {
        p = leaf->portals[i];
        if ( p->removed )
            continue;
        pnum = p - portals;

        if ( !( mightsee[pnum >> 3] & ( 1 << ( pnum & 7 ) ) ) )
            continue;

        more = 0;
        for ( j = 0; j < portallongs; j++ )
        {
            ( ( long * )newmight )[j] = ( ( long * )mightsee )[j] & ( ( long * )p->portalflood )[j];
            more |= ( ( long * )newmight )[j] & ~( ( long * )cansee )[j];
        }

        if ( !more )
            continue;

        cansee[pnum >> 3] |= ( 1 << ( pnum & 7 ) );

        RecursiveLeafBitFlow( p->leaf, newmight, cansee );
    }
}


/* BetterPortalVis  0x00463c10 */
void BetterPortalVis( int portalnum )
{
    vportal_t *p;

    p = portals + portalnum;

    if ( p->removed )
        return;

    RecursiveLeafBitFlow( p->leaf, p->portalflood, p->portalvis );

    p->nummightsee = CountBits( p->portalvis, numportals * 2 );
    c_vis += p->nummightsee;
}
