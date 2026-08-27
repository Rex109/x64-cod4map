/* Original: .\leakfile.cpp */

#ifndef LEAKFILE_H
#define LEAKFILE_H

#include "q_shared.h"
#include "map.h"
#include "brush.h"

struct portal_s;

#define LEAK_POINT_OFFSET   8.0f

extern int leakFileWritten;             /* 0x11ba8420 */

void LeakFile( Tree_t *tree );                                      /* 0x00417e30 */

int  FloodLeakPath_r( Node_t *node, Node_t *targetNode, int floodDist ); /* 0x00418410 */

void PortalLeakFile( Tree_t *tree, struct portal_s *portal );       /* 0x00418100 */

#endif
