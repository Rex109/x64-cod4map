/* Original: .\threads.cpp */

#include "threads.h"

#include "cod4map.h"


qboolean pacifier;                      /* 0x2b98a35c */
int      oldf;                          /* 0x2b98a360 */
CRITICAL_SECTION crit;                  /* 0x2b98a380 */
int      workcount;                     /* 0x2b98a398 */
void   ( *workfunction )( int );        /* 0x2b98a39c */
int      dispatch;                      /* 0x2b98a3a0 */
qboolean threaded;                      /* 0x2b98a3a4 */

int      enter;                         /* 0x14899cc8 */

int      numthreads = -1;               /* 0x0052964c */


/* GetThreadWork  0x0043be80 */
int GetThreadWork( void )
{
    int r;
    int f;

    ThreadLock();

    if ( dispatch == workcount )
    {
        ThreadUnlock();
        return -1;
    }

    f = 10 * dispatch / workcount;
    if ( f != oldf )
    {
        oldf = f;
        if ( pacifier )
            Com_Printf( "%i...", f );
    }

    r = dispatch;
    dispatch++;

    ThreadUnlock();

    return r;
}


/* RunThreadsOnIndividual  0x0043bf10 */
void RunThreadsOnIndividual( int workcnt, qboolean showpacifier, void ( *func )( int ) )
{
    if ( numthreads == -1 )
        ThreadSetDefault();

    workfunction = func;

    RunThreadsOn( workcnt, showpacifier, ThreadWorkerFunction );
}


/* ThreadWorkerFunction  0x0043bf40 */
void ThreadWorkerFunction( int threadnum )
{
    int work;

    while ( 1 )
    {
        work = GetThreadWork();
        if ( work == -1 )
            break;
        Com_DPrintf( "thread %i, work %i\n", threadnum, work );
        workfunction( work );
    }
}


/* ThreadSetDefault  0x0043bf80 */
void ThreadSetDefault( void )
{
    SYSTEM_INFO info;

    if ( numthreads == -1 )
    {
        GetSystemInfo( &info );
        numthreads = info.dwNumberOfProcessors;
        if ( numthreads < 1 || numthreads > 32 )
            numthreads = 1;
    }

    Com_DPrintf( "%i threads\n", numthreads );
}


/* ThreadLock  0x0043bfe0 */
void ThreadLock( void )
{
    if ( !threaded )
        return;
    EnterCriticalSection( &crit );
    if ( enter )
        Com_Error( "Recursive ThreadLock\n" );
    enter = 1;
}


/* ThreadUnlock  0x0043c020 */
void ThreadUnlock( void )
{
    if ( !threaded )
        return;
    if ( !enter )
        Com_Error( "ThreadUnlock without lock\n" );
    enter = 0;
    LeaveCriticalSection( &crit );
}


/* RunThreadsOn  0x0043c060 */
void RunThreadsOn( int workcnt, qboolean showpacifier, void ( *func )( int ) )
{
    DWORD  threadid[MAX_THREADS];
    HANDLE threadhandle[MAX_THREADS];
    int    start, end;
    int    i;

    start     = ( int )I_FloatTime();
    dispatch  = 0;
    workcount = workcnt;
    oldf      = -1;
    pacifier  = showpacifier;
    threaded  = qtrue;

    InitializeCriticalSection( &crit );

    if ( numthreads == 1 )
    {
        func( 0 );
    }
    else
    {
        for ( i = 0; i < numthreads; i++ )
        {
            threadhandle[i] = CreateThread(
                NULL,
                0,
                ( LPTHREAD_START_ROUTINE )func,
                ( LPVOID )( size_t )i,
                0,
                &threadid[i] );
        }

        for ( i = 0; i < numthreads; i++ )
            WaitForSingleObject( threadhandle[i], INFINITE );
    }

    DeleteCriticalSection( &crit );

    threaded = qfalse;
    end      = ( int )I_FloatTime();
    if ( pacifier )
        Com_Printf( " (%i)\n", end - start );
}
