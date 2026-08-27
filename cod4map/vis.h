/* Original: .\vis.cpp */

#ifndef VIS_H
#define VIS_H

#include "q_shared.h"
#include "polylib.h"


#define MAX_PORTALS_ON_LEAF         128

#define MAX_PORTALS                 32768

#define MAX_POINTS_ON_VIS_WINDING   1024

#define MAX_POINTS_ON_FIXED_WINDING 12

#define MAX_SEPERATORS              64

#define VIS_HEADER_SIZE             8

#define VIS_ON_EPSILON              0.1f

#define WCONVEX_EPSILON             0.2f

#define VIS_MERGE_EPSILON           0.1f
#define CONTINUOUS_EPSILON          0.005f

#define stat_none                   0
#define stat_working                1
#define stat_done                   2


typedef vec4_t visPlane_t;

typedef struct
{
    unsigned int ptCount;                                /* +0x00 */
    vec3_t       pts[MAX_POINTS_ON_FIXED_WINDING];       /* +0x04 */
} stackWinding_t;                                        /* sizeof == 0x94 */

typedef struct passage_s
{
    struct passage_s *next;         /* +0x00 */
    byte              cansee[1];    /* +0x04 */
} passage_t;

typedef struct
{
    int             num;            /* +0x00 */
    int             removed;        /* +0x04 */
    visPlane_t      plane;          /* +0x08 */
    int             leaf;           /* +0x18 */
    vec3_t          origin;         /* +0x1c */
    float           radius;         /* +0x28 */
    winding_t      *winding;        /* +0x2c */
    int             status;         /* +0x30 */
    byte           *portalfront;    /* +0x34 */
    byte           *portalflood;    /* +0x38 */
    byte           *portalvis;      /* +0x3c */
    int             nummightsee;    /* +0x40 */
    struct passage_s *passages;     /* +0x44 */
} vportal_t;                        /* sizeof == 0x48 */

typedef struct
{
    int         numportals;                         /* +0x00 */
    int         merged;                             /* +0x04 */
    vportal_t  *portals[MAX_PORTALS_ON_LEAF];       /* +0x08 */
} leaf_t;                                           /* sizeof == 0x208 */

typedef struct pstack_s
{
    byte             mightsee[0x1000];              /* +0x0000 */
    struct pstack_s *next;                          /* +0x1000 */
    leaf_t          *leaf;                          /* +0x1004 */
    vportal_t       *portal;                        /* +0x1008 */
    stackWinding_t  *source;                        /* +0x100c */
    stackWinding_t  *pass;                          /* +0x1010 */
    stackWinding_t   windings[3];                   /* +0x1014 */
    int              freewindings[3];               /* +0x11d0 */
    visPlane_t       portalplane;                   /* +0x11dc */
    int              depth;                         /* +0x11ec */
    visPlane_t       seperators[2][MAX_SEPERATORS]; /* +0x11f0 */
    int              numseperators[2];              /* +0x19f0 */
} pstack_t;                                         /* sizeof == 0x19f8 */

typedef struct
{
    vportal_t *base;            /* +0x0000 */
    int        c_chains;        /* +0x0004 */
    pstack_t   pstack_head;     /* +0x0008 */
} threaddata_t;                 /* sizeof == 0x1a00 */

extern int         g_visMerge;              /* 0x2b820a10 */
extern int         numportals;              /* 0x2b820a18 */
extern char        inbase[32];              /* 0x2b820a1c */
extern int         leafbytes;               /* 0x2b820a40 */
extern int         portalbytes;             /* 0x2b820a44 */
extern int         portalclusters;          /* 0x2b820a4c */
extern int         leaflongs;               /* 0x2b820a50 */
extern leaf_t     *leafs;                   /* 0x2b820a54 */
extern int         noPassageVis;            /* 0x2b820a58 */
extern leaf_t     *faceleafs;               /* 0x2b820a5c */
extern int         portallongs;             /* 0x2b820a60 */
extern int         fastvis;                 /* 0x2b820a68 */
extern vportal_t  *portals;                 /* 0x2b820a6c */
extern int         numfaces;                /* 0x2b820a70 */
extern int         saveprt;                 /* 0x2b820a74 */
extern vportal_t  *sorted_portals[MAX_PORTALS * 2]; /* 0x2b820a78 */
extern int         nosort;                  /* 0x2b920a78 */
extern vportal_t  *faces;                   /* 0x2b920a7c */
extern int         testlevel;               /* 0x005296ac */
extern int         c_vis;                   /* 0x2b920a90 */
extern int         c_flood;                 /* 0x2b920a9c */
extern int         totalvis;                /* 0x2b820a48 */

void        VisMain( int argc, const char **argv );                 /* 0x00460100 */

void        LoadPortals( const char *name );                        /* 0x00460680 */
int         CountBits( const byte *bits, int numbits );             /* 0x00460f00 */
int         CountActivePortals( void );                             /* 0x0045fc70 */
void        WritePortalFile( const char *filename );                /* 0x0045fce0 */

void        UpdatePortals( void );                                  /* 0x0045f580 */
void        MergeLeaves( void );                                    /* 0x0045f600 */
void        MergeLeafPortals( void );                               /* 0x0045fb00 */
winding_t  *TryMergeWinding( const winding_t *f1, const winding_t *f2,
                             const vec3_t planenormal );            /* 0x0045f6e0 */

void        BuildPHS( void );                                       /* 0x0045fed0 */

void            CheckStack( const leaf_t *leaf, const pstack_t *thread ); /* 0x00460f60 */
stackWinding_t *AllocStackWinding( pstack_t *stack );               /* 0x00460ff0 */
void            FreeStackWinding( stackWinding_t *w, pstack_t *stack ); /* 0x00461060 */
stackWinding_t *VisChopWinding( stackWinding_t *in, pstack_t *stack,
                             const visPlane_t split );              /* 0x004610c0 */
stackWinding_t *ClipToSeperators( const stackWinding_t *source,
                                  const stackWinding_t *pass,
                                  stackWinding_t *target,
                                  int flipclip, pstack_t *stack );  /* 0x004614b0 */
void        RecursiveLeafFlow( int leafnum, threaddata_t *thread,
                               pstack_t *prevstack );               /* 0x00461890 */
void        PortalFlow( int portalnum );                            /* 0x00461e40 */


stackWinding_t *ChopWinding( stackWinding_t *in, stackWinding_t *neww,
                             const visPlane_t split );              /* 0x00462ac0 */

int  AddSeperators( const stackWinding_t *source, const stackWinding_t *pass,
                    qboolean flipclip, visPlane_t *seperators,
                    int maxseperators );                            /* 0x00462e60 */

void CreatePassages( int portalnum );                               /* 0x004631a0 */
void PassageMemory( void );                                         /* 0x004635d0 */

void RecursivePassageFlow( vportal_t *portal, threaddata_t *thread,
                           pstack_t *prevstack );                   /* 0x00461fd0 */
void PassageFlow( int portalnum );                                  /* 0x00462260 */

void RecursivePassagePortalFlow( vportal_t *portal, threaddata_t *thread,
                                 pstack_t *prevstack );             /* 0x00462380 */
void PassagePortalFlow( int portalnum );                            /* 0x004629a0 */

void SimpleFlood( vportal_t *srcportal, int leafnum );              /* 0x004636d0 */
void BasePortalVis( int portalnum );                                /* 0x004637d0 */

void RecursiveLeafBitFlow( int leafnum, const byte *mightsee, byte *cansee );
                                                                    /* 0x00463a90 */
void BetterPortalVis( int portalnum );                              /* 0x00463c10 */

winding_t  *NewWinding( int points );                               /* 0x0045e920 */
void        SetPortalSphere( vportal_t *p );                        /* 0x0045efa0 */
int         MergeLeaf( int l1num, int l2num );                      /* 0x0045f180 */
void        ClusterMerge( int leafnum );                            /* 0x0045ec10 */
void        CalcVis( void );                                        /* 0x0045ef00 */
void        SortPortals( void );                                    /* 0x0045ea60 */
void        CalcFastVis( void );                                    /* 0x0045eea0 */
void        CalcFullVis( void );                                    /* 0x0045ee40 */
void        CalcPassageFullVis( void );                             /* 0x0045ee60 */


#endif
