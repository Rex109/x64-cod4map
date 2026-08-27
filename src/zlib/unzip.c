/* -------------------------------------------------------------------------------

unzip.c - minizip's read-only .zip reader, as CoD uses it for .iwd archives

Reconstructed from cod4map.exe (Call of Duty 4 mod tools, MSVC 8.0, 2007-11-28).
Original path: ..\src\zlib\unzip.c   (declarations: ..\src\zlib\unzip.h)

WHICH MINIZIP
-------------
Exactly.  The `unz_copyright` guard string still sits in .rdata at 0x005100d8:

        " unzip 0.15 Copyright 1998 Gilles Vollant "

and unzOpen (0x0048bfc0) still opens with `if (unz_copyright[0]!=' ') return NULL;`
(0x0048bfd2: movsx eax,byte ptr ds:0x5100d8 / cmp eax,0x20 / jne -> return 0).
So this is **minizip 0.15**, the FILE*-based release that shipped alongside
zlib 1.1.4 -- not the later ioapi/zip64 minizip.  Every function below is stock
0.15 except where a comment says otherwise.

WHAT ID CHANGED
---------------
1. Byte-at-a-time I/O replaced by raw little-endian reads.  0.15 built a short
   out of two unzlocal_getByte calls; here unzlocal_getShort (0x0048c1e0) is
   literally `fread(&v,1,2,fin)` + sign-extending store, and unzlocal_getLong
   (0x0048c210) is `fread(&v,1,4,fin)`.  There is no unzlocal_getByte in the
   binary at all.  (Same edit Quake 3 made; the sign extension of the `short`
   is visible as `movsx edx,word ptr [ebp-4]`.)

2. Every `fread( p, n, 1, f ) != 1` became `fread( p, 1, n, f ) != n` -- the
   pushed arguments are (ptr, 1, count, file) at every call site.

3. All fseek()s go through fseek_file_func (0x0048d5a0), which maps a private
   origin enum onto the CRT's.  The mapping is proven by the compare chain at
   0x0048d5ac: origin 0 -> SEEK_CUR(1), 1 -> SEEK_END(2), 2 -> SEEK_SET(0), and
   an unknown origin returns 0 *without seeking*.  Note that this is NOT the
   ZLIB_FILEFUNC_SEEK_* numbering from the later minizip's ioapi.h (which is
   CUR=1, END=2, SET=0) -- CoD renumbered them 0/1/2, and com_files.h's
   FS_SEEK_CUR/END/SET use the same 0/1/2 order.

4. unzlocal_SearchCentralDir gets the file length from a helper
   (0x0048d600) that saves the position, seeks to the end, ftells and seeks
   back, instead of doing `fseek(fin,0,SEEK_END); uSizeFile=ftell(fin);` inline.

5. The CRC check is GONE.  file_in_zip_read_info_s is malloc(0x60) and its
   fields run rest_read_compressed at +0x4c, rest_read_uncompressed at +0x50 --
   i.e. the `crc32` / `crc32_wait` pair that 0.15 keeps between
   pos_local_extrafield and rest_read_compressed is not there, unzReadCurrentFile
   never calls crc32(), and unzCloseCurrentFile has no UNZ_CRCERROR path.

6. ALLOC/TRYFREE are plain malloc/free: the frees in unzClose (0x0048c3fa) and
   unzCloseCurrentFile (0x0048d49c, 0x0048d4d3) have no NULL test in front of
   them, so TRYFREE is `{free(p);}`, not 0.15's `{if (p) free(p);}`.

7. Three functions added for the CoD filesystem:
        unzReOpen                       0x0048bf50
        unzGetCurrentFileInfoPosition   0x0048ca70
        unzSetCurrentFileInfoPosition   0x0048caa0
   and `unz_s` was promoted out of here into unzip.h, because
   FS_FOpenFileReadForThread memcpy()s a whole unz_s between handles.

FUNCTION MAP (address -> name)
------------------------------
    0x0048be60  unzStringFileNameCompare
    0x0048bea0  strcmpcasenosensitive_internal
    0x0048bf50  unzReOpen                               (id addition)
    0x0048bfc0  unzOpen
    0x0048c1e0  unzlocal_getShort
    0x0048c210  unzlocal_getLong
    0x0048c240  unzlocal_SearchCentralDir
    0x0048c3c0  unzClose
    0x0048c410  unzGetGlobalInfo
    0x0048c440  unzGetCurrentFileInfo
    0x0048c470  unzlocal_GetCurrentFileInfoInternal
    0x0048c8b0  unzlocal_DosDateToTmuDate
    0x0048c930  unzGoToFirstFile
    0x0048c9b0  unzGoToNextFile
    0x0048ca70  unzGetCurrentFileInfoPosition           (id addition)
    0x0048caa0  unzSetCurrentFileInfoPosition           (id addition)
    0x0048cb00  unzLocateFile
    0x0048cc00  unzOpenCurrentFile
    0x0048cdd0  unzlocal_CheckCurrentFileCoherencyHeader
    0x0048d050  unzReadCurrentFile
    0x0048d310  unztell
    0x0048d350  unzeof
    0x0048d3a0  unzGetLocalExtrafield
    0x0048d460  unzCloseCurrentFile
    0x0048d4f0  unzGetGlobalComment
    0x0048d5a0  fseek_file_func
    0x0048d600  unzlocal_GetFileSize                    (name UNCERTAIN)

The functions are written below in that address order.  MSVC's /Gy COMDATs let
the linker pick the layout, so the address order is not proof of source order --
but nothing in the output depends on it, and it is the only ordering the binary
actually attests to.  The static helpers are forward-declared at the top so the
order compiles as written.

------------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zlib.h"
#include "unzip.h"

#ifndef local
#  define local static
#endif

/* The guard unzOpen tests before it will touch a file.  Do not "clean this up";
   the check at 0x0048bfd2 is real code.  (0x005100d8) */
const char unz_copyright[] =
   " unzip 0.15 Copyright 1998 Gilles Vollant ";

/* size of the read buffer used by unzReadCurrentFile -- malloc( 0x4000 ) at
   0x0048cc4b, and the same value caps uReadThis at 0x0048d13c. */
#define UNZ_BUFSIZE             (16384)

/* unzLocateFile rejects names >= this and passes ( sizeof( buf ) - 1 ) == 256
   as the filename buffer size (0x0048cb2e / 0x0048cb85). */
#define UNZ_MAXFILENAMEINZIP    (256)

#define ALLOC( size )   (malloc( size ))
#define TRYFREE( p )    {free( p );}

#define SIZECENTRALDIRITEM      (0x2e)
#define SIZEZIPLOCALHEADER      (0x1e)

/* backwards-scan window used to find the end-of-central-directory record --
   malloc( BUFREADCOMMENT + 4 ) == malloc( 0x404 ) at 0x0048c27b. */
#define BUFREADCOMMENT          (0x400)

/* iCaseSensitivity == 0 means "the platform default"; on Windows that is 2,
   i.e. case-insensitive.  0x0048be6b substitutes 2. */
#define CASESENSITIVITYDEFAULTVALUE (2)

/* Origins understood by fseek_file_func.  Proven by the compare chain at
   0x0048d5ac -- see the header comment. */
#define ZLIB_FILEFUNC_SEEK_CUR  (0)
#define ZLIB_FILEFUNC_SEEK_END  (1)
#define ZLIB_FILEFUNC_SEEK_SET  (2)

/* -------------------------------------------------------------------------------
   the state of the one file that is currently open for reading inside an archive

   sizeof == 0x60, from malloc( 0x60 ) at 0x0048cc31.  Every offset below is a
   store or load in unzOpenCurrentFile / unzReadCurrentFile / unzCloseCurrentFile.
   Note that `stream` is 0x34 bytes here, not 0x38: cod4map's zlib.h drops the
   trailing `uLong reserved` from z_stream, which is why inflateInit2_ (0x00487390)
   tests `stream_size == 0x34` and pos_in_zipfile lands at +0x38.  With a stock
   1.2.x zlib.h the struct is four bytes longer; nothing but sizeof cares.
------------------------------------------------------------------------------- */
typedef struct
{
    char         *read_buffer;              /* +0x00  UNZ_BUFSIZE bytes of raw zip data */
    z_stream      stream;                   /* +0x04  inflate state                     */

    uLong         pos_in_zipfile;           /* +0x38  where read_buffer was filled from  */
    uLong         stream_initialised;       /* +0x3c  inflateInit2 succeeded             */

    uLong         offset_local_extrafield;  /* +0x40 */
    uInt          size_local_extrafield;    /* +0x44 */
    uLong         pos_local_extrafield;     /* +0x48 */

    uLong         rest_read_compressed;     /* +0x4c  bytes left to feed inflate         */
    uLong         rest_read_uncompressed;   /* +0x50  bytes left to hand back            */
    FILE         *file;                     /* +0x54  the archive's stream               */
    uLong         compression_method;       /* +0x58  0 == stored, 8 == deflated         */
    uLong         byte_before_the_zipfile;  /* +0x5c  > 0 for a self-extracting archive  */
} file_in_zip_read_info_s;                  /* sizeof == 0x60 */

/* -------------------------------------------------------------------------------
   forward declarations for the file-local helpers
------------------------------------------------------------------------------- */
local int   strcmpcasenosensitive_internal( const char *fileName1, const char *fileName2 );
local int   unzlocal_getShort( FILE *fin, uLong *pX );
local int   unzlocal_getLong( FILE *fin, uLong *pX );
local uLong unzlocal_SearchCentralDir( FILE *fin );
local int   unzlocal_GetCurrentFileInfoInternal( unzFile file,
                                                 unz_file_info *pfile_info,
                                                 unz_file_info_internal *pfile_info_internal,
                                                 char *szFileName,
                                                 uLong fileNameBufferSize,
                                                 void *extraField,
                                                 uLong extraFieldBufferSize,
                                                 char *szComment,
                                                 uLong commentBufferSize );
local void  unzlocal_DosDateToTmuDate( uLong ulDosDate, tm_unz *ptm );
local int   unzlocal_CheckCurrentFileCoherencyHeader( unz_s *s,
                                                      uInt *piSizeVar,
                                                      uLong *poffset_local_extrafield,
                                                      uInt *psize_local_extrafield );
local long  fseek_file_func( FILE *file, long offset, int origin );
local long  unzlocal_GetFileSize( FILE *file );


/* -------------------------------------------------------------------------------
   unzStringFileNameCompare  (0x0048be60)

   Compare two filenames.  iCaseSensitivity: 1 = strcmp, 2 = case-insensitive,
   0 = whatever the platform default is (2 here).
------------------------------------------------------------------------------- */
int unzStringFileNameCompare( const char *fileName1, const char *fileName2, int iCaseSensitivity )
{
    if ( iCaseSensitivity == 0 )
    {
        iCaseSensitivity = CASESENSITIVITYDEFAULTVALUE;
    }

    if ( iCaseSensitivity == 1 )
    {
        return strcmp( fileName1, fileName2 );
    }

    return strcmpcasenosensitive_internal( fileName1, fileName2 );
}


/* -------------------------------------------------------------------------------
   strcmpcasenosensitive_internal  (0x0048bea0)

   ASCII-only case-folding strcmp.  Deliberately not stricmp -- it must behave
   the same on every platform the archive format is shared with.
------------------------------------------------------------------------------- */
local int strcmpcasenosensitive_internal( const char *fileName1, const char *fileName2 )
{
    for ( ;; )
    {
        char c1 = *( fileName1++ );
        char c2 = *( fileName2++ );

        if ( ( c1 >= 'a' ) && ( c1 <= 'z' ) )
        {
            c1 -= 0x20;
        }
        if ( ( c2 >= 'a' ) && ( c2 <= 'z' ) )
        {
            c2 -= 0x20;
        }
        if ( c1 == '\0' )
        {
            return ( ( c2 == '\0' ) ? 0 : -1 );
        }
        if ( c2 == '\0' )
        {
            return 1;
        }
        if ( c1 < c2 )
        {
            return -1;
        }
        if ( c1 > c2 )
        {
            return 1;
        }
    }
}


/* -------------------------------------------------------------------------------
   unzReOpen  (0x0048bf50)   -- id addition, not stock minizip

   Open a second, independent handle onto an archive whose central directory has
   already been read, by cloning the state and giving the clone its own FILE *.
   FS_FOpenFileReadForThread uses it when a second reader wants a file out of an
   archive that already has one open.

   NOTE the original calls Com_Memcpy (0x004762c0, com_shared.cpp) here rather
   than memcpy.  Com_Memcpy is two asserts plus a memcpy, and com_shared.cpp is
   compiled as C++ while this file is compiled as C, so the mangled symbol is not
   reachable from here -- plain memcpy is used instead.  Same bytes copied.
------------------------------------------------------------------------------- */
unzFile unzReOpen( const char *path, unzFile file )
{
    unz_s *s;
    FILE  *fin;

    fin = fopen( path, "rb" );
    if ( fin == NULL )
    {
        return NULL;
    }

    s = (unz_s *)ALLOC( sizeof( unz_s ) );
    memcpy( s, file, sizeof( unz_s ) );

    s->file = fin;
    s->pfile_in_zip_read = NULL;

    return (unzFile)s;
}


/* -------------------------------------------------------------------------------
   unzOpen  (0x0048bfc0)

   Open a .zip/.iwd, locate and validate the end-of-central-directory record, and
   leave the handle sitting on the first entry.  Returns NULL if the file is not
   a single-disk zip.
------------------------------------------------------------------------------- */
unzFile unzOpen( const char *path )
{
    unz_s  us;
    unz_s *s;
    uLong  central_pos, uL;
    FILE  *fin;

    uLong  number_disk;             /* number of the current disk, used for spanning */
    uLong  number_disk_with_CD;     /* number of the disk with central dir           */
    uLong  number_entry_CD;         /* total number of entries in the central dir    */
    int    err = UNZ_OK;

    if ( unz_copyright[0] != ' ' )
    {
        return NULL;
    }

    fin = fopen( path, "rb" );
    if ( fin == NULL )
    {
        return NULL;
    }

    central_pos = unzlocal_SearchCentralDir( fin );
    if ( central_pos == 0 )
    {
        err = UNZ_ERRNO;
    }

    if ( fseek_file_func( fin, central_pos, ZLIB_FILEFUNC_SEEK_SET ) != 0 )
    {
        err = UNZ_ERRNO;
    }

    /* the signature, already checked */
    if ( unzlocal_getLong( fin, &uL ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    /* number of this disk */
    if ( unzlocal_getShort( fin, &number_disk ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    /* number of the disk with the start of the central directory */
    if ( unzlocal_getShort( fin, &number_disk_with_CD ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    /* total number of entries in the central dir on this disk */
    if ( unzlocal_getShort( fin, &us.gi.number_entry ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    /* total number of entries in the central dir */
    if ( unzlocal_getShort( fin, &number_entry_CD ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( ( number_entry_CD != us.gi.number_entry ) ||
         ( number_disk_with_CD != 0 ) ||
         ( number_disk != 0 ) )
    {
        err = UNZ_BADZIPFILE;
    }

    /* size of the central directory */
    if ( unzlocal_getLong( fin, &us.size_central_dir ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    /* offset of start of central directory with respect to the starting disk */
    if ( unzlocal_getLong( fin, &us.offset_central_dir ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    /* zipfile comment length */
    if ( unzlocal_getShort( fin, &us.gi.size_comment ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( ( central_pos < us.offset_central_dir + us.size_central_dir ) &&
         ( err == UNZ_OK ) )
    {
        err = UNZ_BADZIPFILE;
    }

    if ( err != UNZ_OK )
    {
        fclose( fin );
        return NULL;
    }

    us.file = fin;
    us.byte_before_the_zipfile = central_pos - ( us.offset_central_dir + us.size_central_dir );
    us.central_pos = central_pos;
    us.pfile_in_zip_read = NULL;

    s = (unz_s *)ALLOC( sizeof( unz_s ) );
    *s = us;
    unzGoToFirstFile( (unzFile)s );

    return (unzFile)s;
}


/* -------------------------------------------------------------------------------
   unzlocal_getShort  (0x0048c1e0)

   Read a 16-bit little-endian field.  The value is read into a *signed* short and
   then widened, so a field with the top bit set arrives sign-extended -- that is
   what the `movsx` at 0x0048c1f8 does, and it is deliberate in the CoD/Q3 edit of
   minizip (all the fields it is used on are small).
------------------------------------------------------------------------------- */
local int unzlocal_getShort( FILE *fin, uLong *pX )
{
    short v;

    fread( &v, 1, sizeof( v ), fin );

    *pX = (uLong)v;
    return UNZ_OK;
}


/* -------------------------------------------------------------------------------
   unzlocal_getLong  (0x0048c210)

   Read a 32-bit little-endian field.
------------------------------------------------------------------------------- */
local int unzlocal_getLong( FILE *fin, uLong *pX )
{
    int v;

    fread( &v, 1, sizeof( v ), fin );

    *pX = (uLong)v;
    return UNZ_OK;
}


/* -------------------------------------------------------------------------------
   unzlocal_SearchCentralDir  (0x0048c240)

   Scan backwards from the end of the file, BUFREADCOMMENT bytes at a time, for
   the "PK\5\6" end-of-central-directory signature.  Returns its absolute offset,
   or 0 if the file has none within the last 64K.
------------------------------------------------------------------------------- */
local uLong unzlocal_SearchCentralDir( FILE *fin )
{
    unsigned char *buf;
    uLong          uSizeFile;
    uLong          uBackRead;
    uLong          uMaxBack = 0xffff;   /* maximum size of global comment */
    uLong          uPosFound = 0;

    uSizeFile = unzlocal_GetFileSize( fin );

    if ( uMaxBack > uSizeFile )
    {
        uMaxBack = uSizeFile;
    }

    buf = (unsigned char *)ALLOC( BUFREADCOMMENT + 4 );
    if ( buf == NULL )
    {
        return 0;
    }

    uBackRead = 4;
    while ( uBackRead < uMaxBack )
    {
        uLong uReadSize, uReadPos;
        int   i;

        if ( uBackRead + BUFREADCOMMENT > uMaxBack )
        {
            uBackRead = uMaxBack;
        }
        else
        {
            uBackRead += BUFREADCOMMENT;
        }
        uReadPos = uSizeFile - uBackRead;

        uReadSize = ( ( BUFREADCOMMENT + 4 ) < ( uSizeFile - uReadPos ) ) ?
                        ( BUFREADCOMMENT + 4 ) : ( uSizeFile - uReadPos );

        if ( fseek_file_func( fin, uReadPos, ZLIB_FILEFUNC_SEEK_SET ) != 0 )
        {
            break;
        }

        if ( fread( buf, 1, uReadSize, fin ) != uReadSize )
        {
            break;
        }

        for ( i = (int)uReadSize - 3; ( i-- ) > 0; )
        {
            if ( ( ( *( buf + i ) ) == 0x50 ) && ( ( *( buf + i + 1 ) ) == 0x4b ) &&
                 ( ( *( buf + i + 2 ) ) == 0x05 ) && ( ( *( buf + i + 3 ) ) == 0x06 ) )
            {
                uPosFound = uReadPos + i;
                break;
            }
        }

        if ( uPosFound != 0 )
        {
            break;
        }
    }

    TRYFREE( buf );
    return uPosFound;
}


/* -------------------------------------------------------------------------------
   unzClose  (0x0048c3c0)

   Close an archive opened by unzOpen or unzReOpen.  Any file left open inside it
   is closed first.
------------------------------------------------------------------------------- */
int unzClose( unzFile file )
{
    unz_s *s;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;

    if ( s->pfile_in_zip_read != NULL )
    {
        unzCloseCurrentFile( file );
    }

    fclose( s->file );
    TRYFREE( s );
    return UNZ_OK;
}


/* -------------------------------------------------------------------------------
   unzGetGlobalInfo  (0x0048c410)

   Hand back the entry count and comment length read out of the EOCD record.
------------------------------------------------------------------------------- */
int unzGetGlobalInfo( unzFile file, unz_global_info *pglobal_info )
{
    unz_s *s;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;

    *pglobal_info = s->gi;
    return UNZ_OK;
}


/* -------------------------------------------------------------------------------
   unzGetCurrentFileInfo  (0x0048c440)

   Public wrapper: read the central-directory entry the handle is sitting on.
   Any of the output pointers may be NULL.
------------------------------------------------------------------------------- */
int unzGetCurrentFileInfo( unzFile file,
                           unz_file_info *pfile_info,
                           char *szFileName,
                           unsigned long fileNameBufferSize,
                           void *extraField,
                           unsigned long extraFieldBufferSize,
                           char *szComment,
                           unsigned long commentBufferSize )
{
    return unzlocal_GetCurrentFileInfoInternal( file, pfile_info, NULL,
                                                szFileName, fileNameBufferSize,
                                                extraField, extraFieldBufferSize,
                                                szComment, commentBufferSize );
}


/* -------------------------------------------------------------------------------
   unzlocal_GetCurrentFileInfoInternal  (0x0048c470)

   The real central-directory reader.  Reads the 0x2e-byte fixed part at
   s->pos_in_central_dir, then optionally copies out the filename, the extra
   field and the comment, seeking over whatever the caller did not ask for.
   `lSeek` carries the number of bytes still to be skipped between blocks.
------------------------------------------------------------------------------- */
local int unzlocal_GetCurrentFileInfoInternal( unzFile file,
                                               unz_file_info *pfile_info,
                                               unz_file_info_internal *pfile_info_internal,
                                               char *szFileName,
                                               uLong fileNameBufferSize,
                                               void *extraField,
                                               uLong extraFieldBufferSize,
                                               char *szComment,
                                               uLong commentBufferSize )
{
    unz_s                  *s;
    unz_file_info           file_info;
    unz_file_info_internal  file_info_internal;
    int                     err = UNZ_OK;
    uLong                   uMagic;
    long                    lSeek = 0;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;

    if ( fseek_file_func( s->file,
                          s->pos_in_central_dir + s->byte_before_the_zipfile,
                          ZLIB_FILEFUNC_SEEK_SET ) != 0 )
    {
        err = UNZ_ERRNO;
    }

    /* we check the magic */
    if ( err == UNZ_OK )
    {
        if ( unzlocal_getLong( s->file, &uMagic ) != UNZ_OK )
        {
            err = UNZ_ERRNO;
        }
        else if ( uMagic != 0x02014b50 )
        {
            err = UNZ_BADZIPFILE;
        }
    }

    if ( unzlocal_getShort( s->file, &file_info.version ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &file_info.version_needed ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &file_info.flag ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &file_info.compression_method ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getLong( s->file, &file_info.dosDate ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    unzlocal_DosDateToTmuDate( file_info.dosDate, &file_info.tmu_date );

    if ( unzlocal_getLong( s->file, &file_info.crc ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getLong( s->file, &file_info.compressed_size ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getLong( s->file, &file_info.uncompressed_size ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &file_info.size_filename ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &file_info.size_file_extra ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &file_info.size_file_comment ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &file_info.disk_num_start ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &file_info.internal_fa ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getLong( s->file, &file_info.external_fa ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getLong( s->file, &file_info_internal.offset_curfile ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    lSeek += file_info.size_filename;
    if ( ( err == UNZ_OK ) && ( szFileName != NULL ) )
    {
        uLong uSizeRead;

        if ( file_info.size_filename < fileNameBufferSize )
        {
            *( szFileName + file_info.size_filename ) = '\0';
            uSizeRead = file_info.size_filename;
        }
        else
        {
            uSizeRead = fileNameBufferSize;
        }

        if ( ( file_info.size_filename > 0 ) && ( fileNameBufferSize > 0 ) )
        {
            if ( fread( szFileName, 1, uSizeRead, s->file ) != uSizeRead )
            {
                err = UNZ_ERRNO;
            }
        }
        lSeek -= uSizeRead;
    }

    if ( ( err == UNZ_OK ) && ( extraField != NULL ) )
    {
        uLong uSizeRead;

        if ( file_info.size_file_extra < extraFieldBufferSize )
        {
            uSizeRead = file_info.size_file_extra;
        }
        else
        {
            uSizeRead = extraFieldBufferSize;
        }

        if ( lSeek != 0 )
        {
            if ( fseek_file_func( s->file, lSeek, ZLIB_FILEFUNC_SEEK_CUR ) == 0 )
            {
                lSeek = 0;
            }
            else
            {
                err = UNZ_ERRNO;
            }
        }

        if ( ( file_info.size_file_extra > 0 ) && ( extraFieldBufferSize > 0 ) )
        {
            if ( fread( extraField, 1, uSizeRead, s->file ) != uSizeRead )
            {
                err = UNZ_ERRNO;
            }
        }
        lSeek += file_info.size_file_extra - uSizeRead;
    }
    else
    {
        lSeek += file_info.size_file_extra;
    }

    if ( ( err == UNZ_OK ) && ( szComment != NULL ) )
    {
        uLong uSizeRead;

        if ( file_info.size_file_comment < commentBufferSize )
        {
            *( szComment + file_info.size_file_comment ) = '\0';
            uSizeRead = file_info.size_file_comment;
        }
        else
        {
            uSizeRead = commentBufferSize;
        }

        if ( lSeek != 0 )
        {
            if ( fseek_file_func( s->file, lSeek, ZLIB_FILEFUNC_SEEK_CUR ) == 0 )
            {
                lSeek = 0;
            }
            else
            {
                err = UNZ_ERRNO;
            }
        }

        if ( ( file_info.size_file_comment > 0 ) && ( commentBufferSize > 0 ) )
        {
            if ( fread( szComment, 1, uSizeRead, s->file ) != uSizeRead )
            {
                err = UNZ_ERRNO;
            }
        }
        lSeek += file_info.size_file_comment - uSizeRead;
    }
    else
    {
        lSeek += file_info.size_file_comment;
    }

    if ( ( err == UNZ_OK ) && ( pfile_info != NULL ) )
    {
        *pfile_info = file_info;
    }

    if ( ( err == UNZ_OK ) && ( pfile_info_internal != NULL ) )
    {
        *pfile_info_internal = file_info_internal;
    }

    return err;
}


/* -------------------------------------------------------------------------------
   unzlocal_DosDateToTmuDate  (0x0048c8b0)

   Split a packed MS-DOS date/time dword into a tm_unz.
------------------------------------------------------------------------------- */
local void unzlocal_DosDateToTmuDate( uLong ulDosDate, tm_unz *ptm )
{
    uLong uDate;

    uDate = (uLong)( ulDosDate >> 16 );

    ptm->tm_mday = (uLong)( uDate & 0x1f );
    ptm->tm_mon  = (uLong)( ( ( ( uDate ) & 0x1e0 ) / 0x20 ) - 1 );
    ptm->tm_year = (uLong)( ( ( uDate ) & 0x0fe00 ) / 0x0200 + 1980 );

    ptm->tm_hour = (uLong)( ( ulDosDate & 0xf800 ) / 0x800 );
    ptm->tm_min  = (uLong)( ( ulDosDate & 0x7e0 ) / 0x20 );
    ptm->tm_sec  = (uLong)( 2 * ( ulDosDate & 0x1f ) );
}


/* -------------------------------------------------------------------------------
   unzGoToFirstFile  (0x0048c930)

   Rewind to the first entry of the central directory.
------------------------------------------------------------------------------- */
int unzGoToFirstFile( unzFile file )
{
    int    err;
    unz_s *s;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;

    s->pos_in_central_dir = s->offset_central_dir;
    s->num_file = 0;
    err = unzlocal_GetCurrentFileInfoInternal( file, &s->cur_file_info,
                                               &s->cur_file_info_internal,
                                               NULL, 0, NULL, 0, NULL, 0 );
    s->current_file_ok = ( err == UNZ_OK );
    return err;
}


/* -------------------------------------------------------------------------------
   unzGoToNextFile  (0x0048c9b0)

   Step to the next entry.  Returns UNZ_END_OF_LIST_OF_FILE past the last one.
------------------------------------------------------------------------------- */
int unzGoToNextFile( unzFile file )
{
    unz_s *s;
    int    err;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;

    if ( !s->current_file_ok )
    {
        return UNZ_END_OF_LIST_OF_FILE;
    }
    if ( s->num_file + 1 == s->gi.number_entry )
    {
        return UNZ_END_OF_LIST_OF_FILE;
    }

    s->pos_in_central_dir += SIZECENTRALDIRITEM + s->cur_file_info.size_filename +
                             s->cur_file_info.size_file_extra + s->cur_file_info.size_file_comment;
    s->num_file++;
    err = unzlocal_GetCurrentFileInfoInternal( file, &s->cur_file_info,
                                               &s->cur_file_info_internal,
                                               NULL, 0, NULL, 0, NULL, 0 );
    s->current_file_ok = ( err == UNZ_OK );
    return err;
}


/* -------------------------------------------------------------------------------
   unzGetCurrentFileInfoPosition  (0x0048ca70)   -- id addition

   Hand out the central-directory offset of the current entry, so the caller can
   come back to it later without re-walking the directory.  This is the cookie
   com_files.cpp stores in fileInIwd_t::pos.
------------------------------------------------------------------------------- */
int unzGetCurrentFileInfoPosition( unzFile file, unsigned long *pos )
{
    unz_s *s;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;

    *pos = s->pos_in_central_dir;
    return UNZ_OK;
}


/* -------------------------------------------------------------------------------
   unzSetCurrentFileInfoPosition  (0x0048caa0)   -- id addition

   Jump straight to a central-directory entry previously reported by
   unzGetCurrentFileInfoPosition and re-read it.

   NOTE it returns UNZ_OK even when the re-read failed; the caller is expected to
   look at current_file_ok (or simply at the next unzOpenCurrentFile).  That is
   what the binary does -- 0x0048caf3 loads a literal 0 into eax.
------------------------------------------------------------------------------- */
int unzSetCurrentFileInfoPosition( unzFile file, unsigned long pos )
{
    unz_s *s;
    int    err;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;

    s->pos_in_central_dir = pos;
    err = unzlocal_GetCurrentFileInfoInternal( file, &s->cur_file_info,
                                               &s->cur_file_info_internal,
                                               NULL, 0, NULL, 0, NULL, 0 );
    s->current_file_ok = ( err == UNZ_OK );
    return UNZ_OK;
}


/* -------------------------------------------------------------------------------
   unzLocateFile  (0x0048cb00)

   Linear search of the central directory for a name.  On failure the handle is
   put back where it was.
------------------------------------------------------------------------------- */
int unzLocateFile( unzFile file, const char *szFileName, int iCaseSensitivity )
{
    unz_s *s;
    int    err;

    uLong  num_fileSaved;
    uLong  pos_in_central_dirSaved;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }

    if ( strlen( szFileName ) >= UNZ_MAXFILENAMEINZIP )
    {
        return UNZ_PARAMERROR;
    }

    s = (unz_s *)file;
    if ( !s->current_file_ok )
    {
        return UNZ_END_OF_LIST_OF_FILE;
    }

    num_fileSaved = s->num_file;
    pos_in_central_dirSaved = s->pos_in_central_dir;

    err = unzGoToFirstFile( file );

    while ( err == UNZ_OK )
    {
        char szCurrentFileName[UNZ_MAXFILENAMEINZIP + 1];

        unzGetCurrentFileInfo( file, NULL,
                               szCurrentFileName, sizeof( szCurrentFileName ) - 1,
                               NULL, 0, NULL, 0 );
        if ( unzStringFileNameCompare( szCurrentFileName, szFileName, iCaseSensitivity ) == 0 )
        {
            return UNZ_OK;
        }
        err = unzGoToNextFile( file );
    }

    s->num_file = num_fileSaved;
    s->pos_in_central_dir = pos_in_central_dirSaved;
    return err;
}


/* -------------------------------------------------------------------------------
   unzOpenCurrentFile  (0x0048cc00)

   Get ready to read the entry the handle is sitting on: validate the local
   header, allocate the read buffer and, for a deflated entry, spin up a raw
   inflate stream (windowBits negative => no zlib header, no adler check).

   NOTE that `err` is dead: an unsupported compression method sets it to
   UNZ_BADZIPFILE but the function still returns UNZ_OK.  That is stock minizip
   0.15 behaviour and the binary keeps it (0x0048cdc6 xor eax,eax).
------------------------------------------------------------------------------- */
int unzOpenCurrentFile( unzFile file )
{
    int    err = UNZ_OK;
    int    Store;
    uInt   iSizeVar;
    unz_s *s;
    file_in_zip_read_info_s *pfile_in_zip_read_info;
    uLong  offset_local_extrafield;  /* offset of the local extra field */
    uInt   size_local_extrafield;    /* size of the local extra field   */

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;
    if ( !s->current_file_ok )
    {
        return UNZ_PARAMERROR;
    }

    if ( s->pfile_in_zip_read != NULL )
    {
        unzCloseCurrentFile( file );
    }

    if ( unzlocal_CheckCurrentFileCoherencyHeader( s, &iSizeVar,
                                                   &offset_local_extrafield,
                                                   &size_local_extrafield ) != UNZ_OK )
    {
        return UNZ_BADZIPFILE;
    }

    pfile_in_zip_read_info = (file_in_zip_read_info_s *)ALLOC( sizeof( file_in_zip_read_info_s ) );
    if ( pfile_in_zip_read_info == NULL )
    {
        return UNZ_INTERNALERROR;
    }

    pfile_in_zip_read_info->read_buffer = (char *)ALLOC( UNZ_BUFSIZE );
    pfile_in_zip_read_info->offset_local_extrafield = offset_local_extrafield;
    pfile_in_zip_read_info->size_local_extrafield = size_local_extrafield;
    pfile_in_zip_read_info->pos_local_extrafield = 0;

    if ( pfile_in_zip_read_info->read_buffer == NULL )
    {
        TRYFREE( pfile_in_zip_read_info );
        return UNZ_INTERNALERROR;
    }

    pfile_in_zip_read_info->stream_initialised = 0;

    if ( ( s->cur_file_info.compression_method != 0 ) &&
         ( s->cur_file_info.compression_method != Z_DEFLATED ) )
    {
        err = UNZ_BADZIPFILE;
    }
    Store = s->cur_file_info.compression_method == 0;

    pfile_in_zip_read_info->compression_method = s->cur_file_info.compression_method;
    pfile_in_zip_read_info->file = s->file;
    pfile_in_zip_read_info->byte_before_the_zipfile = s->byte_before_the_zipfile;

    pfile_in_zip_read_info->stream.total_out = 0;

    if ( !Store )
    {
        pfile_in_zip_read_info->stream.zalloc = (alloc_func)0;
        pfile_in_zip_read_info->stream.zfree = (free_func)0;
        pfile_in_zip_read_info->stream.opaque = (voidpf)0;

        err = inflateInit2( &pfile_in_zip_read_info->stream, -MAX_WBITS );
        if ( err == Z_OK )
        {
            pfile_in_zip_read_info->stream_initialised = 1;
        }
        /* windowBits is passed < 0 to tell that there is no zlib header.
           Note that in this case inflate *requires* an extra "dummy" byte
           after the compressed stream in order to complete inflation and
           return Z_STREAM_END.  In unzip, i don't wait absolutely Z_STREAM_END
           because i known the size of both compressed and uncompressed data */
    }

    pfile_in_zip_read_info->rest_read_compressed = s->cur_file_info.compressed_size;
    pfile_in_zip_read_info->rest_read_uncompressed = s->cur_file_info.uncompressed_size;

    pfile_in_zip_read_info->pos_in_zipfile = s->cur_file_info_internal.offset_curfile +
                                             SIZEZIPLOCALHEADER + iSizeVar;

    pfile_in_zip_read_info->stream.avail_in = (uInt)0;

    s->pfile_in_zip_read = pfile_in_zip_read_info;

    return UNZ_OK;
}


/* -------------------------------------------------------------------------------
   unzlocal_CheckCurrentFileCoherencyHeader  (0x0048cdd0)

   Read the entry's local header and check it against what the central directory
   said.  Fields covered by the "sizes in the data descriptor" flag (bit 3) are
   not compared.  Returns, through the out-parameters, how many bytes of variable
   header sit between the fixed part and the data.
------------------------------------------------------------------------------- */
local int unzlocal_CheckCurrentFileCoherencyHeader( unz_s *s,
                                                    uInt *piSizeVar,
                                                    uLong *poffset_local_extrafield,
                                                    uInt *psize_local_extrafield )
{
    uLong uMagic, uData, uFlags;
    uLong size_filename;
    uLong size_extra_field;
    int   err = UNZ_OK;

    *piSizeVar = 0;
    *poffset_local_extrafield = 0;
    *psize_local_extrafield = 0;

    if ( fseek_file_func( s->file,
                          s->cur_file_info_internal.offset_curfile + s->byte_before_the_zipfile,
                          ZLIB_FILEFUNC_SEEK_SET ) != 0 )
    {
        return UNZ_ERRNO;
    }

    if ( err == UNZ_OK )
    {
        if ( unzlocal_getLong( s->file, &uMagic ) != UNZ_OK )
        {
            err = UNZ_ERRNO;
        }
        else if ( uMagic != 0x04034b50 )
        {
            err = UNZ_BADZIPFILE;
        }
    }

    if ( unzlocal_getShort( s->file, &uData ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }
/*
    else if ( ( err == UNZ_OK ) && ( uData != s->cur_file_info.wVersion ) )
        err = UNZ_BADZIPFILE;
*/

    if ( unzlocal_getShort( s->file, &uFlags ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getShort( s->file, &uData ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }
    else if ( ( err == UNZ_OK ) && ( uData != s->cur_file_info.compression_method ) )
    {
        err = UNZ_BADZIPFILE;
    }

    if ( ( err == UNZ_OK ) && ( s->cur_file_info.compression_method != 0 ) &&
                              ( s->cur_file_info.compression_method != Z_DEFLATED ) )
    {
        err = UNZ_BADZIPFILE;
    }

    if ( unzlocal_getLong( s->file, &uData ) != UNZ_OK )        /* date/time */
    {
        err = UNZ_ERRNO;
    }

    if ( unzlocal_getLong( s->file, &uData ) != UNZ_OK )        /* crc */
    {
        err = UNZ_ERRNO;
    }
    else if ( ( err == UNZ_OK ) && ( uData != s->cur_file_info.crc ) &&
                                   ( ( uFlags & 8 ) == 0 ) )
    {
        err = UNZ_BADZIPFILE;
    }

    if ( unzlocal_getLong( s->file, &uData ) != UNZ_OK )        /* size compr */
    {
        err = UNZ_ERRNO;
    }
    else if ( ( err == UNZ_OK ) && ( uData != s->cur_file_info.compressed_size ) &&
                                   ( ( uFlags & 8 ) == 0 ) )
    {
        err = UNZ_BADZIPFILE;
    }

    if ( unzlocal_getLong( s->file, &uData ) != UNZ_OK )        /* size uncompr */
    {
        err = UNZ_ERRNO;
    }
    else if ( ( err == UNZ_OK ) && ( uData != s->cur_file_info.uncompressed_size ) &&
                                   ( ( uFlags & 8 ) == 0 ) )
    {
        err = UNZ_BADZIPFILE;
    }

    if ( unzlocal_getShort( s->file, &size_filename ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }
    else if ( ( err == UNZ_OK ) && ( size_filename != s->cur_file_info.size_filename ) )
    {
        err = UNZ_BADZIPFILE;
    }

    *piSizeVar += (uInt)size_filename;

    if ( unzlocal_getShort( s->file, &size_extra_field ) != UNZ_OK )
    {
        err = UNZ_ERRNO;
    }
    *poffset_local_extrafield = s->cur_file_info_internal.offset_curfile +
                                SIZEZIPLOCALHEADER + size_filename;
    *psize_local_extrafield = (uInt)size_extra_field;

    *piSizeVar += (uInt)size_extra_field;

    return err;
}


/* -------------------------------------------------------------------------------
   unzReadCurrentFile  (0x0048d050)

   Read up to `len` bytes out of the open entry.  Returns the byte count, 0 at
   end of file, or a negative UNZ_/Z_ error.  A stored entry is memcpy'd out of
   the read buffer; a deflated one goes through inflate() with Z_SYNC_FLUSH.
------------------------------------------------------------------------------- */
int unzReadCurrentFile( unzFile file, void *buf, unsigned len )
{
    int    err = UNZ_OK;
    uInt   iRead = 0;
    unz_s *s;
    file_in_zip_read_info_s *pfile_in_zip_read_info;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;
    pfile_in_zip_read_info = (file_in_zip_read_info_s *)s->pfile_in_zip_read;

    if ( pfile_in_zip_read_info == NULL )
    {
        return UNZ_PARAMERROR;
    }

    if ( ( pfile_in_zip_read_info->read_buffer == NULL ) )
    {
        return UNZ_END_OF_LIST_OF_FILE;
    }
    if ( len == 0 )
    {
        return 0;
    }

    pfile_in_zip_read_info->stream.next_out = (Bytef *)buf;

    pfile_in_zip_read_info->stream.avail_out = (uInt)len;

    if ( len > pfile_in_zip_read_info->rest_read_uncompressed )
    {
        pfile_in_zip_read_info->stream.avail_out =
            (uInt)pfile_in_zip_read_info->rest_read_uncompressed;
    }

    while ( pfile_in_zip_read_info->stream.avail_out > 0 )
    {
        if ( ( pfile_in_zip_read_info->stream.avail_in == 0 ) &&
             ( pfile_in_zip_read_info->rest_read_compressed > 0 ) )
        {
            uInt uReadThis = UNZ_BUFSIZE;

            if ( pfile_in_zip_read_info->rest_read_compressed < uReadThis )
            {
                uReadThis = (uInt)pfile_in_zip_read_info->rest_read_compressed;
            }
            if ( uReadThis == 0 )
            {
                return UNZ_EOF;
            }
            if ( fseek_file_func( pfile_in_zip_read_info->file,
                                  pfile_in_zip_read_info->pos_in_zipfile +
                                      pfile_in_zip_read_info->byte_before_the_zipfile,
                                  ZLIB_FILEFUNC_SEEK_SET ) != 0 )
            {
                return UNZ_ERRNO;
            }
            if ( fread( pfile_in_zip_read_info->read_buffer, 1, uReadThis,
                        pfile_in_zip_read_info->file ) != uReadThis )
            {
                return UNZ_ERRNO;
            }
            pfile_in_zip_read_info->pos_in_zipfile += uReadThis;

            pfile_in_zip_read_info->rest_read_compressed -= uReadThis;

            pfile_in_zip_read_info->stream.next_in = (Bytef *)pfile_in_zip_read_info->read_buffer;
            pfile_in_zip_read_info->stream.avail_in = (uInt)uReadThis;
        }

        if ( pfile_in_zip_read_info->compression_method == 0 )
        {
            uInt uDoCopy, i;

            if ( pfile_in_zip_read_info->stream.avail_out <
                 pfile_in_zip_read_info->stream.avail_in )
            {
                uDoCopy = pfile_in_zip_read_info->stream.avail_out;
            }
            else
            {
                uDoCopy = pfile_in_zip_read_info->stream.avail_in;
            }

            for ( i = 0; i < uDoCopy; i++ )
            {
                *( pfile_in_zip_read_info->stream.next_out + i ) =
                    *( pfile_in_zip_read_info->stream.next_in + i );
            }

            pfile_in_zip_read_info->rest_read_uncompressed -= uDoCopy;
            pfile_in_zip_read_info->stream.avail_in -= uDoCopy;
            pfile_in_zip_read_info->stream.avail_out -= uDoCopy;
            pfile_in_zip_read_info->stream.next_out += uDoCopy;
            pfile_in_zip_read_info->stream.next_in += uDoCopy;
            pfile_in_zip_read_info->stream.total_out += uDoCopy;
            iRead += uDoCopy;
        }
        else
        {
            uLong uTotalOutBefore, uTotalOutAfter;

            uTotalOutBefore = pfile_in_zip_read_info->stream.total_out;
            err = inflate( &pfile_in_zip_read_info->stream, Z_SYNC_FLUSH );
            uTotalOutAfter = pfile_in_zip_read_info->stream.total_out;

            pfile_in_zip_read_info->rest_read_uncompressed -=
                ( uTotalOutAfter - uTotalOutBefore );

            iRead += (uInt)( uTotalOutAfter - uTotalOutBefore );

            if ( err == Z_STREAM_END )
            {
                return ( iRead == 0 ) ? UNZ_EOF : iRead;
            }
            if ( err != Z_OK )
            {
                break;
            }
        }
    }

    if ( err == Z_OK )
    {
        return iRead;
    }
    return err;
}


/* -------------------------------------------------------------------------------
   unztell  (0x0048d310)

   Byte offset within the *uncompressed* entry.
------------------------------------------------------------------------------- */
long unztell( unzFile file )
{
    unz_s *s;
    file_in_zip_read_info_s *pfile_in_zip_read_info;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;
    pfile_in_zip_read_info = (file_in_zip_read_info_s *)s->pfile_in_zip_read;

    if ( pfile_in_zip_read_info == NULL )
    {
        return UNZ_PARAMERROR;
    }

    return (long)pfile_in_zip_read_info->stream.total_out;
}


/* -------------------------------------------------------------------------------
   unzeof  (0x0048d350)

   1 once the whole entry has been handed back.
------------------------------------------------------------------------------- */
int unzeof( unzFile file )
{
    unz_s *s;
    file_in_zip_read_info_s *pfile_in_zip_read_info;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;
    pfile_in_zip_read_info = (file_in_zip_read_info_s *)s->pfile_in_zip_read;

    if ( pfile_in_zip_read_info == NULL )
    {
        return UNZ_PARAMERROR;
    }

    if ( pfile_in_zip_read_info->rest_read_uncompressed == 0 )
    {
        return 1;
    }
    else
    {
        return 0;
    }
}


/* -------------------------------------------------------------------------------
   unzGetLocalExtrafield  (0x0048d3a0)

   Copy out the entry's *local*-header extra field (which is not always the same
   as the central directory's).  With buf == NULL it just reports the size.

   NOTE the fread asks for size_to_read bytes, not read_now -- i.e. it can
   overrun a short buffer.  That is what 0.15 does and what the binary does
   (0x0048d425 pushes the size_to_read local), so it is left alone.
------------------------------------------------------------------------------- */
int unzGetLocalExtrafield( unzFile file, void *buf, unsigned len )
{
    unz_s *s;
    file_in_zip_read_info_s *pfile_in_zip_read_info;
    uInt   read_now;
    uLong  size_to_read;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;
    pfile_in_zip_read_info = (file_in_zip_read_info_s *)s->pfile_in_zip_read;

    if ( pfile_in_zip_read_info == NULL )
    {
        return UNZ_PARAMERROR;
    }

    size_to_read = ( pfile_in_zip_read_info->size_local_extrafield -
                     pfile_in_zip_read_info->pos_local_extrafield );

    if ( buf == NULL )
    {
        return (int)size_to_read;
    }

    if ( len > size_to_read )
    {
        read_now = (uInt)size_to_read;
    }
    else
    {
        read_now = len;
    }

    if ( read_now == 0 )
    {
        return 0;
    }

    if ( fseek_file_func( pfile_in_zip_read_info->file,
                          pfile_in_zip_read_info->offset_local_extrafield +
                              pfile_in_zip_read_info->pos_local_extrafield,
                          ZLIB_FILEFUNC_SEEK_SET ) != 0 )
    {
        return UNZ_ERRNO;
    }

    if ( fread( buf, 1, size_to_read, pfile_in_zip_read_info->file ) != size_to_read )
    {
        return UNZ_ERRNO;
    }

    return (int)read_now;
}


/* -------------------------------------------------------------------------------
   unzCloseCurrentFile  (0x0048d460)

   Tear down the open entry.  The stock 0.15 CRC comparison that would return
   UNZ_CRCERROR here is absent from the binary, so `err` can only ever be UNZ_OK.
------------------------------------------------------------------------------- */
int unzCloseCurrentFile( unzFile file )
{
    int    err = UNZ_OK;

    unz_s *s;
    file_in_zip_read_info_s *pfile_in_zip_read_info;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;
    pfile_in_zip_read_info = (file_in_zip_read_info_s *)s->pfile_in_zip_read;

    if ( pfile_in_zip_read_info == NULL )
    {
        return UNZ_PARAMERROR;
    }

    TRYFREE( pfile_in_zip_read_info->read_buffer );
    pfile_in_zip_read_info->read_buffer = NULL;
    if ( pfile_in_zip_read_info->stream_initialised )
    {
        inflateEnd( &pfile_in_zip_read_info->stream );
    }

    pfile_in_zip_read_info->stream_initialised = 0;
    TRYFREE( pfile_in_zip_read_info );

    s->pfile_in_zip_read = NULL;

    return err;
}


/* -------------------------------------------------------------------------------
   unzGetGlobalComment  (0x0048d4f0)

   Copy out the archive-level comment that follows the EOCD record.
------------------------------------------------------------------------------- */
int unzGetGlobalComment( unzFile file, char *szComment, unsigned long uSizeBuf )
{
    unz_s *s;
    uLong  uReadThis;

    if ( file == NULL )
    {
        return UNZ_PARAMERROR;
    }
    s = (unz_s *)file;

    uReadThis = uSizeBuf;
    if ( uReadThis > s->gi.size_comment )
    {
        uReadThis = s->gi.size_comment;
    }

    if ( fseek_file_func( s->file, s->central_pos + 22, ZLIB_FILEFUNC_SEEK_SET ) != 0 )
    {
        return UNZ_ERRNO;
    }

    if ( uReadThis > 0 )
    {
        *szComment = '\0';
        if ( fread( szComment, 1, uReadThis, s->file ) != uReadThis )
        {
            return UNZ_ERRNO;
        }
    }

    if ( ( szComment != NULL ) && ( uSizeBuf > s->gi.size_comment ) )
    {
        *( szComment + s->gi.size_comment ) = '\0';
    }
    return (int)uReadThis;
}


/* -------------------------------------------------------------------------------
   fseek_file_func  (0x0048d5a0)

   The one place unzip.c touches the CRT's fseek.  `origin` is one of the
   ZLIB_FILEFUNC_SEEK_* values above, NOT a CRT SEEK_*; an unrecognised origin
   returns 0 without seeking (i.e. it looks like success, which is stock ioapi's
   behaviour inverted -- ioapi returns -1 -- but it is what the binary does).
------------------------------------------------------------------------------- */
local long fseek_file_func( FILE *file, long offset, int origin )
{
    int fseek_origin;

    switch ( origin )
    {
    case ZLIB_FILEFUNC_SEEK_CUR:
        fseek_origin = SEEK_CUR;
        break;
    case ZLIB_FILEFUNC_SEEK_END:
        fseek_origin = SEEK_END;
        break;
    case ZLIB_FILEFUNC_SEEK_SET:
        fseek_origin = SEEK_SET;
        break;
    default:
        return 0;
    }

    return fseek( file, offset, fseek_origin );
}


/* -------------------------------------------------------------------------------
   unzlocal_GetFileSize  (0x0048d600)   -- name UNCERTAIN

   Length of an open stream, leaving the read position where it found it.  Stock
   0.15 does the seek-to-end inline in unzlocal_SearchCentralDir and never
   restores the position; this helper is a CoD addition and is its only caller.
------------------------------------------------------------------------------- */
local long unzlocal_GetFileSize( FILE *file )
{
    long pos;
    long size;

    pos = ftell( file );
    fseek( file, 0, SEEK_END );
    size = ftell( file );
    fseek( file, pos, SEEK_SET );

    return size;
}
