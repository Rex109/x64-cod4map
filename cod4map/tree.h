
#ifndef TREE_H
#define TREE_H

#include "q_shared.h"
#include "map.h"
#include "brush.h"

extern int c_nodes;                     /* 0x00534ba8 */


Node_t *PointInLeaf( Node_t *headnode, const vec3_t point );        /* 0x0043c180 */

Node_t *PointInOpenLeaf( Node_t *headnode, const vec3_t point );    /* 0x0043c1f0 */

void FreeTreePortals_r( Node_t *node );                             /* 0x0043c2a0 */
void FreeTree_r( Node_t *node );                                    /* 0x0043c340 */
void FreeTree( Tree_t *tree );                                      /* 0x0043c3c0 */

void PrintTree_r( const Node_t *node, int depth );                  /* 0x0043c3f0 */

#endif
