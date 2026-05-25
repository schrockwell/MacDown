#ifndef MDEDIT_FILE_IO_H
#define MDEDIT_FILE_IO_H

#include <Files.h>

typedef enum {
    kLE_CR = 0,    /* classic Mac, default if no line endings found */
    kLE_LF,        /* Unix */
    kLE_CRLF       /* DOS / Windows / many "modern" .md files */
} LineEndKind;

/* Maximum bytes we'll edit. TextEdit's structural limit. */
#define kMdMaxFileBytes 32000L

/* Open a file by HFS coordinates and read its full contents into a
   freshly-allocated Handle. The contents are normalized in place to CR
   line endings (the only thing TextEdit understands). The original
   line-ending kind is reported via *outLE; the host fold count (non-
   MacRoman chars folded) via *outFoldCount.

   On success returns noErr and *outData. Caller must DisposHandle().
   On failure returns an OSErr; *outData is not touched. */
OSErr FileIOReadDoc(short vRefNum, ConstStr255Param name,
                    Handle *outData,
                    LineEndKind *outLE,
                    long *outFoldCount);

/* Save data (CR-terminated) to vRefNum/name, expanding CR back to `le`
   on write. Creates the file if it doesn't exist. */
OSErr FileIOWriteDoc(short vRefNum, ConstStr255Param name,
                     const char *data, long len, LineEndKind le);

/* Set the Finder file type/creator on a freshly-saved file so it gets
   the MdEdit icon and double-click association. */
OSErr FileIOSetTypeAndCreator(short vRefNum, ConstStr255Param name,
                              OSType type, OSType creator);

#endif
