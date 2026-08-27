
#include "d3dx9_34.h"
#include "cmdlib.h"

typedef int ( WINAPI *OptimizeFn_t )( LPCVOID, UINT, UINT, BOOL, void * );

static HMODULE      s_d3dx;
static OptimizeFn_t s_optimizeFaces;
static OptimizeFn_t s_optimizeVertices;

static void D3DX_Load( void )
{
    if ( s_d3dx )
        return;

    s_d3dx = LoadLibraryA( "d3dx9_34.dll" );
    if ( !s_d3dx )
        Com_Error( "could not load d3dx9_34.dll; it ships with the mod tools\n" );

    s_optimizeFaces    = (OptimizeFn_t)GetProcAddress( s_d3dx, "D3DXOptimizeFaces" );
    s_optimizeVertices = (OptimizeFn_t)GetProcAddress( s_d3dx, "D3DXOptimizeVertices" );

    if ( !s_optimizeFaces || !s_optimizeVertices )
        Com_Error( "d3dx9_34.dll is missing D3DXOptimizeFaces/D3DXOptimizeVertices\n" );
}

extern "C" int WINAPI D3DXOptimizeFaces( LPCVOID pbIndices, UINT cFaces, UINT cVertices,
                                         BOOL b32BitIndices, void *pFaceRemap )
{
    D3DX_Load();
    return s_optimizeFaces( pbIndices, cFaces, cVertices, b32BitIndices, pFaceRemap );
}

extern "C" int WINAPI D3DXOptimizeVertices( LPCVOID pbIndices, UINT cFaces, UINT cVertices,
                                            BOOL b32BitIndices, void *pVertexRemap )
{
    D3DX_Load();
    return s_optimizeVertices( pbIndices, cFaces, cVertices, b32BitIndices, pVertexRemap );
}
