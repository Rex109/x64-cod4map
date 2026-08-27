
#ifndef D3DX9_34_H
#define D3DX9_34_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

int WINAPI D3DXOptimizeFaces( LPCVOID pbIndices, UINT cFaces, UINT cVertices,
                                  BOOL b32BitIndices, void *pFaceRemap );

int WINAPI D3DXOptimizeVertices( LPCVOID pbIndices, UINT cFaces, UINT cVertices,
                                     BOOL b32BitIndices, void *pVertexRemap );

#ifdef __cplusplus
}
#endif

#endif
