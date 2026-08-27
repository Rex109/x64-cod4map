/* Original: .\map.cpp */

#ifndef MAP_H
#define MAP_H

#include "q_shared.h"
#include "polylib.h"
#include "bspfile.h"


#define MAX_BUILD_SIDES         300

#define MAX_MAP_PLANES_COMPILE  0x40000

#define PLANE_HASH_SIZE         0x400
#define PLANE_HASH_DIST_DIV     8

#define MAX_MAP_INFOS           0x8000

#define EQUAL_EPSILON           0.001f
#define NORMAL_EPSILON          1e-7

#define BEVEL_MIN_EDGE_LEN      0.5f
#define BEVEL_MAX_Z             0.866f

#define CONTENTS_CLIP_MASK  ( CONTENTS_MISSILECLIP | CONTENTS_VEHICLECLIP \
                            | CONTENTS_AI_NOSIGHT | CONTENTS_CLIPSHOT     \
                            | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP )   /* 0x00033280 */

#define SURF_IGNORE_MASK    ( SURF_NODAMAGE | SURF_NOIMPACT | SURF_NOMARKS \
                            | SURF_NOLIGHTMAP | SURF_NOSTEPS )               /* 0x00002431 */
#define SURF_PORTAL_BRUSH   ( SURF_PORTAL | SURF_NONSOLID | SURF_NODRAW )    /* 0x80004080 */

#define SMOOTHING_HARD          0
#define SMOOTHING_SMOOTH        1
#define SMOOTHING_SMOOTH_OTHER  2
#define SMOOTHING_COUNT         3

#ifndef PLANENUM_LEAF
#define PLANENUM_LEAF   ( -1 )
#endif


typedef struct plane_s
{
    vec3_t          normal;      /* +0x00 */
    float           dist;        /* +0x0c */
    int             type;        /* +0x10 */
    int             flags;       /* +0x14 */
    struct plane_s *hash_chain;  /* +0x18 */
} plane_t;


#ifndef COD4MAP_MATERIAL_T_DEFINED
#define COD4MAP_MATERIAL_T_DEFINED
typedef struct
{
    int            nameOffset;      /* +0x00 */
    int            pad04;           /* +0x04 */
    byte           techSetFlags;    /* +0x08 */
    byte           sortOrder;       /* +0x09 */
    byte           pad0a[0x08];     /* +0x0a */
    unsigned short toolFlags;       /* +0x12 */
    byte           pad14[0x04];     /* +0x14 */
    unsigned short textureWidth;    /* +0x18 */
    unsigned short textureHeight;   /* +0x1a */
    float          subdivisions;    /* +0x1c */
    int            surfaceFlags;    /* +0x20 */
    int            contentFlags;    /* +0x24 */
    int            pad28;           /* +0x28 */
    unsigned int   layerFlags;      /* +0x2c */
    unsigned short textureCount;    /* +0x30 */
    unsigned short constantCount;   /* +0x32 */
    int            techSetNameOffset;  /* +0x34 */
    int            textureTableOffset; /* +0x38 */
    int            constantTableOffset;/* +0x3c */
} Material_t;
#endif


typedef struct
{
    int         planenum;      /* +0x00 */
    vec4_t      texMat[2];     /* +0x04 */
    vec4_t      lmapMat[2];    /* +0x24 */
    winding_t  *winding;       /* +0x44 */
    void       *visibleHull;   /* +0x48 */
    void       *edges;         /* +0x4c */
    Material_t *material;      /* +0x50 */
    Material_t *lmapMaterial;  /* +0x54 */
    int         contents;      /* +0x58 */
    int         surfaceFlags;  /* +0x5c */
    int         toolFlags;     /* +0x60 */
    int         smoothing;     /* +0x64 */
    byte        bevel;         /* +0x68 */
    byte        portalIgnored; /* +0x69 */
    byte        pad6a[2];      /* +0x6a */
    int         cellOnPortalSide[2]; /* +0x6c */
    struct Brush_s *brush;     /* +0x74 */
} side_t;


typedef struct Brush_s
{
    struct Brush_s *next;              /* +0x00 */
    int             entitynum;         /* +0x04 */
    int             brushnum;          /* +0x08 */
    int             mapInfoIndex;      /* +0x0c */
    Material_t     *material;          /* +0x10 */
    int             contents;          /* +0x14 */
    byte            detail;            /* +0x18 */
    byte            forceVisible;      /* +0x19 */
    byte            opaque;            /* +0x1a */
    byte            noCollision;       /* +0x1b */
    int             cullGroup;         /* +0x1c */
    int             diskBrushNum;      /* +0x20 */
    int             outputNumber;      /* +0x24 */
    int             pad28;             /* +0x28 */
    struct Brush_s *original;          /* +0x2c */
    int             collisionSideCount;/* +0x30 */
    side_t         *collisionSides;    /* +0x34 */
    vec3_t          mins;              /* +0x38 */
    vec3_t          maxs;              /* +0x44 */
    unsigned int    sideCount;         /* +0x50 */
    side_t          sides[1];          /* +0x54 */
} Brush_t;


#ifndef COD4MAP_PARSEMESH_T_DEFINED
#define COD4MAP_PARSEMESH_T_DEFINED
typedef struct parseMesh_s
{
    struct parseMesh_s *next;          /* +0x00 */
    int                 width;         /* +0x04 */
    int                 height;        /* +0x08 */
    int                 subdivisions;  /* +0x0c */
    void               *verts;         /* +0x10 */
    int                *vertFlags;     /* +0x14 */
    Material_t         *material;      /* +0x18 */
    Material_t         *lmapMaterial;  /* +0x1c */
    byte                isMesh;        /* +0x20 */
    byte                pad21[3];      /* +0x21 */
    int                 contents;      /* +0x24 */
    int                 toolFlags;     /* +0x28 */
    int                 smoothing;     /* +0x2c */
    int                 brushnum;      /* +0x30 */
    int                 entitynum;     /* +0x34 */
    int                 mapInfoIndex;  /* +0x38 */
    int                 cullGroup;     /* +0x3c */
} parseMesh_t;
#endif

#ifndef COD4MAP_TREE_T_DEFINED
#define COD4MAP_TREE_T_DEFINED
struct portal_s;
struct Entity_s;
struct cmPartList_s;
struct CmCollideBox_s;

typedef struct node_s
{
    int              planenum;       /* +0x00 */
    struct node_s   *parent;         /* +0x04 */
    vec3_t           mins;           /* +0x08 */
    vec3_t           maxs;           /* +0x14 */
    struct Brush_s  *volume;         /* +0x20 */
    int              opaque;         /* +0x24 */
    int              pad28;          /* +0x28 */
    struct node_s   *children[2];    /* +0x2c */
    int              tinyportals;    /* +0x34 */
    vec3_t           referencepoint; /* +0x38 */
    int              pad44;          /* +0x44 */
    int              cluster;        /* +0x48 */
    int              area;           /* +0x4c */
    struct Brush_s  *leafBrushes;    /* +0x50 */
    struct cmPartList_s   *partitionList;  /* +0x54 */
    struct CmCollideBox_s *collisionBoxes; /* +0x58 */
    int              occupied;       /* +0x5c */
    struct Entity_s *occupant;       /* +0x60 */
    int              cellnum;        /* +0x64 */
    struct portal_s *portals;        /* +0x68 */
} Node_t;                            /* sizeof == 0x6c */

#define CELLNUM_UNKNOWN     ( -2 )

typedef struct tree_s
{
    Node_t *headnode;               /* +0x00 */
    Node_t  outside_node;           /* +0x04 */
    vec3_t  mins;                   /* +0x70 */
    vec3_t  maxs;                   /* +0x7c */
} Tree_t;                           /* sizeof == 0x88 */

typedef struct face_s Face_t;
#endif


extern plane_t     *mapplanes;                        /* 0x11ba8438 */
extern int          nummapplanes;                     /* 0x122a9468 */
extern plane_t     *planehash[PLANE_HASH_SIZE];       /* 0x122a8440 */

extern Brush_t     *buildBrush;                       /* 0x123a9478 */
extern Entity_t    *mapent;                           /* 0x122a9464 */
extern int          map_entity_num;                   /* 0x122a9440 */
extern int          entitySourceBrushes;              /* 0x122a9470 */

extern int          c_detail;                         /* 0x122a9444 */
extern int          c_structural;                     /* 0x122a9474 */
extern int          c_patches;                        /* 0x122a9454 */
extern int          c_boxbevels;                      /* 0x122a946c */
extern int          c_edgebevels;                     /* 0x122a945c */
extern int          c_areaportals;                    /* 0x122a9460 */

extern vec3_t       map_mins;                         /* 0x122a9448 */
extern vec3_t       map_maxs;                         /* 0x123a947c */

extern FILE        *g_gridAutoFile;                   /* 0x122a9458 */
extern FILE        *g_gridNotFile;                    /* 0x122a8438 */


int         AddMapInfo( const char *sourceMapName, const vec3_t origin,
                        const vec3_t angles, float scale );          /* 0x0041ae80 */
const char *GetMapFileName( int mapIndex );                          /* 0x004191d0 */
void        GetMapOrientation( int mapIndex, orientation_t *orient,
                               float *scale );                       /* 0x004193a0 */
void        CheckForNewerSourceMaps( const char *bspPath );          /* 0x00419200 */

int   FindFloatPlane( const vec3_t normal, float dist );             /* 0x004198a0 */
int   CreateNewFloatPlane( const vec3_t normal, float dist );        /* 0x00419540 */
int   SnapPlaneNormal( vec3_t normal );                              /* 0x004197d0 */
void  SnapPlane( vec3_t normal, float *dist );                       /* 0x00419770 */
void  HashPlane( plane_t *p );                                       /* 0x004194e0 */

Material_t *BestSideMaterialForNormal( const vec3_t normal );        /* 0x00419e80 */
void     SetBrushContents( Brush_t *b );                             /* 0x004199f0 */
int      ValidateBrushSides( Brush_t *b );                           /* 0x0041a2e0 */
void     AddBrushBevels( void );                                     /* 0x00419c10 */
void     AddEdgeBevels( void );                                      /* 0x00419f30 */
Brush_t *FinishBrush( int mapInfoIndex );                            /* 0x0041b360 */

void  MoveBrushesToWorld( Entity_t *ent, int cullGroup );            /* 0x0041a610 */
void  MovePhysicsBrushes( Entity_t *ent );                          /* 0x0041a6c0 */
void  SetEntityOriginFromGeometry( Entity_t *ent );                 /* 0x0041a780 */
void  AdjustBrushesForOrigin( Entity_t *ent );                      /* 0x0041a950 */

void  OpenLightGridFiles( void );                                    /* 0x0041a4a0 */
void  CloseLightGridFiles( void );                                   /* 0x0041a560 */

void  LoadMapFile( const char *filename );                           /* 0x0041c770 */
void  ExpandBrushesAndWriteMap( void );                              /* 0x0041d660 */

#endif
