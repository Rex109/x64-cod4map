/* Original: .\tris_sunshadow.cpp */

#ifndef TRIS_SUNSHADOW_H
#define TRIS_SUNSHADOW_H

#include "q_shared.h"
#include "surface.h"
#include "tris.h"

#define CASTS_SUN_SHADOW_UNKNOWN     0
#define CASTS_SUN_SHADOW_DENIED      1
#define CASTS_SUN_SHADOW_CONFIRMED   3

void Tris_MarkSunShadowCasters( void );                         /* 0x0045a750 */

void Tris_MarkAllDrawSurfsAsCasters( void );                    /* 0x0045a7f0 */

void Tris_SetCastsSunShadow( struct TriSurf_s *surf, byte value ); /* 0x0045a670 */
void Tris_ConfirmCastsSunShadow( struct TriSurf_s *surf );         /* 0x0045a650 */

bool Tris_CastsSunShadow( byte castsSunShadow );                /* 0x0045a700 */

#endif
