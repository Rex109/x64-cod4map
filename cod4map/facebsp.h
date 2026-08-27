
#ifndef FACEBSP_H
#define FACEBSP_H

#include "q_shared.h"
#include "polylib.h"
#include "map.h"
#include "brush.h"

struct face_s
{
    struct face_s *next;      /* +0x00 */
    int            planenum;  /* +0x04 */
    int            checked;   /* +0x08 */
    winding_t     *w;         /* +0x0c */
};


#define CLASSIFY_COPLANAR   ( ( int )0x80000000 )

#define AXIAL_PLANE_BONUS   640

#define SPLIT_CLIP_EPSILON  0.2f

#define SPLIT_MIN_EXTENT    0.001f

#define DIRECT_EVAL_THRESHOLD   64

extern const float k_classifyDotThresholds[3];   /* 0x004f7cf8 */
extern const float k_classifyDistTolerance[3];   /* 0x004f7d04 */
extern const float k_classifyDistSqThreshold[3]; /* 0x004f7d10 */
extern const float k_classifyMaxExtent[3];       /* 0x004f7d1c */


extern int    c_faceLeafs;                       /* 0x11ba8410 */

extern float *splitMinsArray;                    /* 0x11ba8414 */
extern float *splitMaxsArray;                    /* 0x11ba8418 */
extern float *splitCoplanarArray;                /* 0x11ba841c */

extern int    useOptimizedSplit;                 /* 0x00a35bf4 */


Face_t *AllocFace( void );                                              /* 0x00414f10 */
void    FreeFace( Face_t *f );                                          /* 0x00414f40 */
int     CountFaceList( const Face_t *list );                            /* 0x00415410 */

int  SplitPlaneValue( int facing, int front, int back, int splits );    /* 0x00415280 */
int  ClassifyWindingAgainstPlane( const Face_t *face, const vec3_t planeNormal,
                                  float planeDist, int axis );          /* 0x004152c0 */
int  CheckPlaneAgainstAllWindings( const vec4_t plane, const Face_t *list,
                                   int axis );                          /* 0x00416000 */

int  SelectSplitPlane_BlockSubdivision( const Node_t *node, Face_t *list,
                                        int axis );                     /* 0x00414f70 */
void SelectSplitPlane_DirectEval( int *evalResult, const Face_t *list,
                                  int axis );                           /* 0x00415870 */
void SelectSplitPlane_EvaluateAxes( int *evalResult, const Face_t *list,
                                    int faceCount, int axis );          /* 0x00415a00 */
int  SelectSplitPlane_Optimized( const Node_t *node, const Face_t *list,
                                 int faceCount );                       /* 0x00415770 */

void    BuildBspTree_Recursive( Node_t *node, Face_t *list, int faceCount ); /* 0x00415440 */
Tree_t *BuildBspTree( Face_t *list );                                   /* 0x004160a0 */

Face_t *CollectBrushWindings( Brush_t *brushList );                     /* 0x00416240 */
void    ClipSidesIntoTree( Brush_t *brushList, Tree_t *tree );          /* 0x00416330 */

qboolean WindingInSolid_r( const Node_t *node, winding_t *w );          /* 0x004163c0 */

int  FloatCompare( const void *va, const void *vb );                    /* 0x00416050 */

#endif
