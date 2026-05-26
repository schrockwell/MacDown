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

/* Standard File returns a working-directory ref (wdRefNum) that the
   File Manager can silently RECYCLE to a different folder once the
   per-process pool fills up. If we cache that wdRefNum and use it on
   a later save, we end up writing to whatever folder the slot got
   reassigned to — clobbering same-named files in unrelated folders.
   FileIOOwnWD opens a wdRefNum that *we* own (via OpenWD); the
   system can't recycle it until we CloseWD it. Pass the wdRefNum
   from any SFReply/AppFile here, get back a stable one to cache. */
OSErr FileIOOwnWD(short anyWDRefNum, short *outOwnedWD);

/* For Apple Events whose direct param is an FSSpec (vRefNum + parID).
   No wdRefNum involved on the inbound side, but we still want our
   own slot so it stays put for subsequent saves. */
OSErr FileIOOwnWDFromDir(short vRefNum, long dirID, short *outOwnedWD);

/* Release an owned wdRefNum. Call from DocClose when hasFile. */
void  FileIOReleaseWD(short ownedWD);

/* Open a file by HFS coordinates and read its full contents into a
   freshly-allocated Handle. Contents are normalized to CR line endings
   in place (the only thing TextEdit understands). Original kind reported
   via *outLE; fold count via *outFoldCount. Caller DisposHandles. */
OSErr FileIOReadDoc(short vRefNum, ConstStr255Param name,
                    Handle *outData,
                    LineEndKind *outLE,
                    long *outFoldCount);

/* Save data (CR-terminated) to (vRefNum, name), expanding CR back to
   `le` on write. Creates the file if it doesn't exist. */
OSErr FileIOWriteDoc(short vRefNum, ConstStr255Param name,
                     const char *data, long len, LineEndKind le);

/* Set Finder type/creator on a freshly-saved file. */
OSErr FileIOSetTypeAndCreator(short vRefNum, ConstStr255Param name,
                              OSType type, OSType creator);

#endif
