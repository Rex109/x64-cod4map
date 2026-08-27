/* Original: .\errors.cpp */

#ifndef ERRORS_H
#define ERRORS_H

#include "q_shared.h"
#include "polylib.h"

#define MAPERROR_NONFATAL   0
#define MAPERROR_FATAL      1

void MapError( int severity, const vec3_t pos, const vec3_t normal,
               int mapIndex, int entityNum, int brushNum,
               const char *fmt, ... );

void MapErrorWinding( int severity, const winding_t *w,
                      int mapIndex, int entityNum, int brushNum,
                      const char *fmt, ... );

void MapError_Init( void );
bool MapErrorsOccurred( void );

#endif
