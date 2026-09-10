cmake_minimum_required( VERSION 3.21 )

project( cod4map LANGUAGES C CXX )

if( NOT MSVC )
    message( FATAL_ERROR
        "cod4map requires MSVC.  The original is an x87 MSVC build and other "
        "compilers change floating-point results." )
endif()

if( NOT CMAKE_SIZEOF_VOID_P EQUAL 4 )
    message( FATAL_ERROR
        "cod4map must be built for 32-bit x86.  Configure with:  "
        "cmake -B build -A Win32" )
endif()

if( NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE )
    set( CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE )
endif()

set( CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" )

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/bin" )
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin" )
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/bin" )

set( SRC_COD4MAP
    cod4map/brush.cpp
    cod4map/brush_sides.cpp
    cod4map/bsp.cpp
    cod4map/collision.cpp
    cod4map/errors.cpp
    cod4map/facebsp.cpp
    cod4map/fixedpoint.cpp
    cod4map/leakfile.cpp
    cod4map/lightmaps.cpp
    cod4map/map.cpp
    cod4map/map_reflection_probe.cpp
    cod4map/mapfile.cpp
    cod4map/materials.cpp
    cod4map/mesh.cpp
    cod4map/patch.cpp
    cod4map/portals.cpp
    cod4map/primarylights.cpp
    cod4map/surface.cpp
    cod4map/threads.cpp
    cod4map/tree.cpp
    cod4map/tris.cpp
    cod4map/tris_coalesce.cpp
    cod4map/tris_combinelayers.cpp
    cod4map/tris_gridtree.cpp
    cod4map/tris_lightmap.cpp
    cod4map/tris_mergeconcave.cpp
    cod4map/tris_mergeverts.cpp
    cod4map/tris_sunshadow.cpp
    cod4map/tris_tesselate.cpp
    cod4map/tris_tjunc.cpp
    cod4map/vis.cpp
    cod4map/writebsp.cpp
)

set( SRC_COMMON
    common/brush_edges.cpp
    common/bspfile.cpp
    common/collvec.cpp
    common/poly2d.cpp
    common/polylib.cpp
    common/primarylights_region.cpp
    common/targetplatform.cpp
    common/texturevecs.cpp
)

set( SRC_CMDLIB
    libs/cmdlib/cmdlib.cpp
)

set( SRC_UNIVERSAL
    src/universal/aabbtree.cpp
    src/universal/assertive.cpp
    src/universal/com_convexhull.cpp
    src/universal/com_files.cpp
    src/universal/com_math.cpp
    src/universal/com_memory.cpp
    src/universal/com_shared.cpp
    src/universal/com_vector.cpp
    src/universal/dvar.cpp
    src/universal/q_parse.cpp
    src/universal/q_shared.cpp
    src/universal/surfaceflags.cpp
    src/universal/tangentspace.cpp
)

set( SRC_ODE
    src/physics/ode/src/mass.cpp
    src/physics/ode/src/matrix.cpp
)

set( SRC_D3DX
    src/d3dx/d3dx9_34.cpp
)

set( SRC_ZLIB
    src/zlib/adler32.c
    src/zlib/compress.c
    src/zlib/crc32.c
    src/zlib/deflate.c
    src/zlib/inffast.c
    src/zlib/inflate.c
    src/zlib/inftrees.c
    src/zlib/trees.c
    src/zlib/uncompr.c
    src/zlib/unzip.c
    src/zlib/zutil.c
)

add_executable( cod4map
    ${SRC_COD4MAP}
    ${SRC_COMMON}
    ${SRC_CMDLIB}
    ${SRC_UNIVERSAL}
    ${SRC_ODE}
    ${SRC_D3DX}
    ${SRC_ZLIB}
)

target_include_directories( cod4map PRIVATE
    cod4map
    common
    src
    src/universal
    src/qcommon
    src/zlib
    src/d3dx
    libs/cmdlib
)

target_compile_definitions( cod4map PRIVATE
    WIN32
    _WINDOWS
    _CRT_SECURE_NO_WARNINGS
    $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

target_compile_options( cod4map PRIVATE
    /arch:IA32
    /fp:precise
    /GS-
    /Gy
    /W3
    /wd4996 /wd4244 /wd4018 /wd4267 /wd4305 /wd4101 /wd4700
    $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
    $<$<NOT:$<CONFIG:Debug>>:/O2>
    $<$<NOT:$<CONFIG:Debug>>:/Oy>
)

target_link_options( cod4map PRIVATE
    /SUBSYSTEM:CONSOLE
    /STACK:0x400000,0x1000
    /DYNAMICBASE:NO
    /FIXED
    /LARGEADDRESSAWARE
    /OPT:REF
    /OPT:ICF
    legacy_stdio_float_rounding.obj
)

target_link_libraries( cod4map PRIVATE
    user32 gdi32 kernel32 advapi32 winmm
)

source_group( "cod4map"        FILES ${SRC_COD4MAP} )
source_group( "common"         FILES ${SRC_COMMON} )
source_group( "libs/cmdlib"    FILES ${SRC_CMDLIB} )
source_group( "src/universal"  FILES ${SRC_UNIVERSAL} )
source_group( "src/physics"    FILES ${SRC_ODE} )
source_group( "src/d3dx"       FILES ${SRC_D3DX} )
source_group( "src/zlib"       FILES ${SRC_ZLIB} )
