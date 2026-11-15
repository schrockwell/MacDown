#ifndef MDEDIT_FILE_IO_H
#define MDEDIT_FILE_IO_H

#include <Files.h>

/* Zero every byte of a stack struct (typically a ParamBlock or SFReply).
   The Multiversal Interfaces' File-Manager glue functions sometimes read
   fields they didn't intend to set (e.g. SetFInfo's ioFDirIndex starts as
   stack garbage and triggers an indexed lookup, returning the wrong file).
   Bypassing the glue with a zeroed PB is the documented workaround. */
#define ZeroStruct(pb) \
    do { \
        char *_zp = (char *)&(pb); \
        short _zi; \
        for (_zi = 0; _zi < (short)sizeof(pb); _zi++) _zp[_zi] = 0; \
    } while (0)

typedef enum {
    kLE_CR = 0,    /* classic Mac, default if no line endings found */
    kLE_LF,        /* Unix */
    kLE_CRLF       /* DOS / Windows / many "modern" .md files */
} LineEndKind;

/* Maximum bytes we'll edit. TextEdit's structural limit. */
#define kMdMaxFileBytes 32000L

/* Files are addressed by (real volume refNum, directory ID, name) and
   accessed with the HFS PBH calls; MacDown never opens or closes
   working directories (see file_io.c for why). */

/* Convert a wdRefNum from Standard File / AppFiles / SysEnvirons (or a
   plain vRefNum) to (volume, dirID). Read-only; call it right away,
   while the wdRefNum is still valid. */
OSErr FileIOResolveWD(short anyWDRefNum, short *outVRefNum, long *outDirID);

/* Open a file's data fork with the given permission (fsRdPerm,
   fsRdWrPerm, ...). */
OSErr FileIOOpen(short vRefNum, long dirID, ConstStr255Param name,
                 SignedByte perm, short *outRefNum);

/* Create an empty file with the given Finder creator/type. */
OSErr FileIOCreate(short vRefNum, long dirID, ConstStr255Param name,
                   OSType creator, OSType type);

/* Read a file's full contents into a freshly-allocated Handle.
   Contents are normalized to CR line endings in place (the only thing
   TextEdit understands). Original kind reported via *outLE; fold count
   via *outFoldCount. Returns -1 if the file is too big to edit. Caller
   DisposeHandles. */
OSErr FileIOReadDoc(short vRefNum, long dirID, ConstStr255Param name,
                    Handle *outData,
                    LineEndKind *outLE,
                    long *outFoldCount);

/* Save data (CR-terminated), expanding CR back to `le` on write.
   Overwrites in place; creates the file if it doesn't exist. */
OSErr FileIOWriteDoc(short vRefNum, long dirID, ConstStr255Param name,
                     const char *data, long len, LineEndKind le);

/* True if a file (not a folder) named `name` exists in (vRefNum, dirID). */
Boolean FileIOFileExists(short vRefNum, long dirID, ConstStr255Param name);

/* Set Finder type/creator on a file. */
OSErr FileIOSetTypeAndCreator(short vRefNum, long dirID, ConstStr255Param name,
                              OSType type, OSType creator);

#endif
