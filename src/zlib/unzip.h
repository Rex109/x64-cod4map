/* -------------------------------------------------------------------------------

unzip.h - minizip's read-only .zip API, as CoD uses it for .iwd archives

Reconstructed from cod4map.exe (Call of Duty 4 mod tools, MSVC 8.0, 2007-11-28).
Original path: ..\src\zlib\unzip.h  (implementation: ..\src\zlib\unzip.c)

This file exists so that com_files.cpp and ..\qcommon\com_fileAccess.h have the
declarations they need.  Only what those two use is reconstructed; unzip.c itself
is a separate job.

WHAT THE BINARY SHOWS
---------------------
cod4map uses **minizip**, not a hand-rolled zip reader, and it is the Quake 3
lineage of minizip -- unmodified except for the three functions id added
(unzReOpen, unzGetCurrentFileInfoPosition, unzSetCurrentFileInfoPosition) and the
promotion of `unz_s` from unzip.c into this header, which FS_FOpenFileReadForThread
depends on because it memcpy()s a whole unz_s from the archive's shared handle
into the per-file-handle one.

Evidence:
  * 0x0048bfc0 unzOpen -> 0x0048c240 unzlocal_SearchCentralDir scans backwards in
    0x400-byte chunks for the "PK\5\6" end-of-central-directory signature, then
    reads the EOCD with the byte-at-a-time helpers at 0x0048c1e0 / 0x0048c210.
  * malloc( 0x80 ) and a 0x20-dword struct copy give sizeof( unz_s ) == 0x80,
    which is exactly stock minizip's layout (see below).
  * The error codes the functions return are minizip's verbatim: -100, -102,
    -103, -104.
  * zlib's inflate IS called directly, but from unzip.c, never from com_files.cpp:
    unzOpenCurrentFile (0x0048cc00) calls inflateInit2_ (0x00487390) with
    windowBits -15 and the version string "1.1.4", and unzReadCurrentFile
    (0x0048d050) calls inflate (0x00487520) with Z_SYNC_FLUSH.  A stored (method
    0) entry is copied straight out of the read buffer with no zlib involved.

The three addresses the Ghidra dump filed under ode/mass.cpp --
  0x0048c1e0  unzlocal_getShort( FILE *, unsigned long * )
  0x0048c210  unzlocal_getLong ( FILE *, unsigned long * )
  0x0048d5a0  fseek_file_func  ( FILE *, long, int )
-- are unzip.c internals, not com_files.cpp and not ODE.  They belong in
..\src\zlib\unzip.c with the rest of the file.

------------------------------------------------------------------------------- */

#ifndef UNZIP_H
#define UNZIP_H

#include <stdio.h>

/* unzip.c is compiled as C while every one of its callers (com_files.cpp,
   ..\qcommon\com_fileAccess.h) is compiled as C++, so the linkage guard is not
   optional -- without it MSVC looks for `?unzOpen@@YAPAXPBD@Z` and unzip.obj
   only offers `_unzOpen`.  Stock minizip's unzip.h carries the same guard. */
#ifdef __cplusplus
extern "C" {
#endif

typedef void *unzFile;

#define UNZ_OK                      (0)
#define UNZ_END_OF_LIST_OF_FILE     (-100)
#define UNZ_ERRNO                   (-1)
#define UNZ_EOF                     (0)
#define UNZ_PARAMERROR              (-102)
#define UNZ_BADZIPFILE              (-103)
#define UNZ_INTERNALERROR           (-104)
#define UNZ_CRCERROR                (-105)

typedef struct tm_unz_s
{
    unsigned long tm_sec;
    unsigned long tm_min;
    unsigned long tm_hour;
    unsigned long tm_mday;
    unsigned long tm_mon;
    unsigned long tm_year;
} tm_unz;

typedef struct unz_global_info_s
{
    unsigned long number_entry;         /* +0x00 */
    unsigned long size_comment;         /* +0x04 */
} unz_global_info;

typedef struct unz_file_info_s
{
    unsigned long version;              /* +0x00 */
    unsigned long version_needed;       /* +0x04 */
    unsigned long flag;                 /* +0x08 */
    unsigned long compression_method;   /* +0x0c */
    unsigned long dosDate;              /* +0x10 */
    unsigned long crc;                  /* +0x14 */
    unsigned long compressed_size;      /* +0x18 */
    unsigned long uncompressed_size;    /* +0x1c */
    unsigned long size_filename;        /* +0x20 */
    unsigned long size_file_extra;      /* +0x24 */
    unsigned long size_file_comment;    /* +0x28 */
    unsigned long disk_num_start;       /* +0x2c */
    unsigned long internal_fa;          /* +0x30 */
    unsigned long external_fa;          /* +0x34 */
    tm_unz        tmu_date;             /* +0x38 */
} unz_file_info;                        /* sizeof == 0x50 */

typedef struct unz_file_info_internal_s
{
    unsigned long offset_curfile;       /* +0x00 */
} unz_file_info_internal;

/* The whole state of one open archive.  com_files.cpp copies this struct
   wholesale, so the layout is load-bearing: sizeof must be 0x80, `file` must be
   the first member, cur_file_info must start at +0x28 (so that
   cur_file_info.uncompressed_size lands at +0x44, which is what FS_filelength
   and FS_FOpenFileReadForThread read) and pfile_in_zip_read must be at +0x7c. */
typedef struct
{
    FILE                   *file;                       /* +0x00 */
    unz_global_info         gi;                         /* +0x04 */
    unsigned long           byte_before_the_zipfile;    /* +0x0c */
    unsigned long           num_file;                   /* +0x10 */
    unsigned long           pos_in_central_dir;         /* +0x14 */
    unsigned long           current_file_ok;            /* +0x18 */
    unsigned long           central_pos;                /* +0x1c */
    unsigned long           size_central_dir;           /* +0x20 */
    unsigned long           offset_central_dir;         /* +0x24 */
    unz_file_info           cur_file_info;              /* +0x28 */
    unz_file_info_internal  cur_file_info_internal;     /* +0x78 */
    void                   *pfile_in_zip_read;          /* +0x7c */
} unz_s;                                                /* sizeof == 0x80 */

unzFile unzOpen( const char *path );                                        /* 0x0048bfc0 */
unzFile unzReOpen( const char *path, unzFile file );                        /* 0x0048bf50 */
int     unzClose( unzFile file );                                           /* 0x0048c3c0 */
int     unzGetGlobalInfo( unzFile file, unz_global_info *pglobal_info );    /* 0x0048c410 */
int     unzGoToFirstFile( unzFile file );                                   /* 0x0048c930 */
int     unzGoToNextFile( unzFile file );                                    /* 0x0048c9b0 */
int     unzGetCurrentFileInfo( unzFile file, unz_file_info *pfile_info,
                               char *szFileName, unsigned long fileNameBufferSize,
                               void *extraField, unsigned long extraFieldBufferSize,
                               char *szComment, unsigned long commentBufferSize ); /* 0x0048c440 */
int     unzGetCurrentFileInfoPosition( unzFile file, unsigned long *pos );  /* 0x0048ca70 */
int     unzSetCurrentFileInfoPosition( unzFile file, unsigned long pos );   /* 0x0048caa0 */
int     unzOpenCurrentFile( unzFile file );                                 /* 0x0048cc00 */
int     unzCloseCurrentFile( unzFile file );                                /* 0x0048d460 */
int     unzReadCurrentFile( unzFile file, void *buf, unsigned len );        /* 0x0048d050 */
long    unztell( unzFile file );                                            /* 0x0048d310 */

/* The rest of minizip 0.15's public surface.  cod4map links these in but does
   not call them from outside unzip.c in this build; they are declared here so
   that unzip.c has a prototype in scope for every non-static function it
   defines, and so a future caller does not have to re-derive them. */
int     unzStringFileNameCompare( const char *fileName1, const char *fileName2,
                                  int iCaseSensitivity );                   /* 0x0048be60 */
int     unzLocateFile( unzFile file, const char *szFileName,
                       int iCaseSensitivity );                              /* 0x0048cb00 */
int     unzeof( unzFile file );                                             /* 0x0048d350 */
int     unzGetLocalExtrafield( unzFile file, void *buf, unsigned len );     /* 0x0048d3a0 */
int     unzGetGlobalComment( unzFile file, char *szComment,
                             unsigned long uSizeBuf );                      /* 0x0048d4f0 */

/* " unzip 0.15 Copyright 1998 Gilles Vollant " -- 0x005100d8.  unzOpen returns
   NULL if its first character is not a space; that check is real code. */
extern const char unz_copyright[];

#ifdef __cplusplus
}
#endif

#endif /* UNZIP_H */
