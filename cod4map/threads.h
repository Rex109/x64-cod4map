/* Original: .\threads.cpp */

#ifndef THREADS_H
#define THREADS_H

#include "q_shared.h"

#define MAX_THREADS     64

extern int numthreads;                          /* 0x0052964c */

extern int  dispatch;                           /* 0x2b98a3a0 */
extern int  workcount;                          /* 0x2b98a398 */
extern int  oldf;                               /* 0x2b98a360 */
extern qboolean pacifier;                       /* 0x2b98a35c */
extern qboolean threaded;                       /* 0x2b98a3a4 */
extern void (*workfunction)( int );             /* 0x2b98a39c */
extern CRITICAL_SECTION crit;                   /* 0x2b98a380 */
extern int  enter;                              /* 0x14899cc8 */


void ThreadSetDefault( void );                                      /* 0x0043bf80 */

void ThreadLock( void );                                            /* 0x0043bfe0 */
void ThreadUnlock( void );                                          /* 0x0043c020 */

int  GetThreadWork( void );                                         /* 0x0043be80 */

void ThreadWorkerFunction( int threadnum );                         /* 0x0043bf40 */

void RunThreadsOn( int workcnt, qboolean showpacifier, void ( *func )( int ) );
                                                                    /* 0x0043c060 */

void RunThreadsOnIndividual( int workcnt, qboolean showpacifier,
                             void ( *func )( int ) );               /* 0x0043bf10 */

#endif
