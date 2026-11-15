#include "file_io.h"

#include <Files.h>
#include <Memory.h>

#include "pstr.h"

#define UC(c) ((unsigned char)(c))

/* ---- Helpers ---- */

static LineEndKind DetectLineEnding(const char *buf, long len)
{
    long i;
    long max = (len < 4096) ? len : 4096;
    for (i = 0; i < max; i++) {
        if (buf[i] == '\r') {
            if (i + 1 < len && buf[i + 1] == '\n') return kLE_CRLF;
            return kLE_CR;
        }
        if (buf[i] == '\n') return kLE_LF;
    }
    return kLE_CR;
}

/* Replace common UTF-8 / "smart" sequences with MacRoman equivalents,
   collapse foreign newlines to CR, and report how many chars got folded.
   Operates in place; returns the new length (always <= original len). */
static long FoldAndNormalize(char *buf, long len, LineEndKind le,
                             long *outFoldCount)
{
    long src = 0, dst = 0;
    long folds = 0;

    while (src < len) {
        unsigned char c = UC(buf[src]);

        /* Line ending normalization to CR. */
        if (c == '\r') {
            buf[dst++] = '\r';
            src++;
            /* Eat trailing LF if CRLF. */
            if (le == kLE_CRLF && src < len && buf[src] == '\n') src++;
            continue;
        }
        if (c == '\n') {
            buf[dst++] = '\r';
            src++;
            continue;
        }

        /* UTF-8 multi-byte: fold common ones to MacRoman or '?'. */
        if (c >= 0x80) {
            /* Quick detect: if it's a leading byte of valid UTF-8,
               peek next bytes for known sequences. */
            if (c == 0xE2 && src + 2 < len) {
                unsigned char c1 = UC(buf[src + 1]);
                unsigned char c2 = UC(buf[src + 2]);
                /* U+2018 / U+2019 smart single quotes -> ' */
                if (c1 == 0x80 && (c2 == 0x98 || c2 == 0x99)) {
                    buf[dst++] = (c2 == 0x98) ? (char)0xD4 : (char)0xD5;
                    src += 3; folds++; continue;
                }
                /* U+201C / U+201D smart double quotes */
                if (c1 == 0x80 && (c2 == 0x9C || c2 == 0x9D)) {
                    buf[dst++] = (c2 == 0x9C) ? (char)0xD2 : (char)0xD3;
                    src += 3; folds++; continue;
                }
                /* U+2013 en dash -> 0xD0; U+2014 em dash -> 0xD1 */
                if (c1 == 0x80 && (c2 == 0x93 || c2 == 0x94)) {
                    buf[dst++] = (c2 == 0x93) ? (char)0xD0 : (char)0xD1;
                    src += 3; folds++; continue;
                }
                /* U+2026 ellipsis */
                if (c1 == 0x80 && c2 == 0xA6) {
                    buf[dst++] = (char)0xC9;
                    src += 3; folds++; continue;
                }
            }
            /* Any other 0x80-0xFF byte: pass through. If it's UTF-8
               we'll get garbled glyphs but won't crash. Best-effort. */
            if (c < 0xC0) {
                /* Standalone high-bit byte: copy verbatim. */
                buf[dst++] = (char)c;
                src++;
                continue;
            }
            /* UTF-8 leading byte we don't have a mapping for: replace
               with '?' and skip the continuation bytes. */
            buf[dst++] = '?';
            folds++;
            src++;
            while (src < len && (UC(buf[src]) & 0xC0) == 0x80) src++;
            continue;
        }

        /* Plain ASCII (incl. tab). */
        buf[dst++] = buf[src++];
    }

    if (outFoldCount) *outFoldCount = folds;
    return dst;
}

/* ---- Folder references ----

   Documents, prefs and the browser all address files by (real volume
   refNum, directory ID, name) and use the HFS "PBH" calls directly.
   We never open or close working directories.

   An earlier design cached an "owned" wdRefNum per document via
   PBOpenWD. That doesn't work: on System 7, PBOpenWD hands back the
   existing working directory for a folder whatever ioWDProcID is
   passed, and there's no reference count, so the first document to
   close (or the browser moving on) PBCloseWD'd the folder out from
   under every other document in it -- their next Save failed with
   nsvErr, or, once the slot was reused, wrote into another folder.

   Every param block is zeroed first. The Multiversal glue for the
   H calls (HOpen, HCreate, SetFInfo...) leaves fields like ioFVersNum,
   ioMisc and ioFDirIndex as stack garbage, which is what made those
   calls look unreliable. */

/* Standard File replies, AppFiles and SysEnvirons hand us a wdRefNum
   (or a plain vRefNum); turn it into (volume, dirID) right away while
   it's still valid. Read-only: opens nothing. */
OSErr FileIOResolveWD(short anyWDRefNum, short *outVRefNum, long *outDirID)
{
    WDPBRec pb;
    OSErr err;
    ZeroStruct(pb);
    pb.ioVRefNum   = anyWDRefNum;
    pb.ioWDIndex   = 0;
    err = PBGetWDInfoSync(&pb);
    if (err != noErr) return err;
    if (outVRefNum) *outVRefNum = pb.ioWDVRefNum;
    if (outDirID)   *outDirID   = pb.ioWDDirID;
    return noErr;
}

/* ---- File ops ---- */

OSErr FileIOOpen(short vRefNum, long dirID, ConstStr255Param name,
                 SignedByte perm, short *outRefNum)
{
    HParamBlockRec pb;
    OSErr err;

    ZeroStruct(pb);
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vRefNum;
    pb.fileParam.ioDirID   = dirID;
    pb.ioParam.ioPermssn   = perm;
    err = PBHOpenDFSync(&pb);
    if (err == paramErr) {
        /* OpenDF isn't implemented before System 7. */
        ZeroStruct(pb);
        pb.fileParam.ioNamePtr = (StringPtr)name;
        pb.fileParam.ioVRefNum = vRefNum;
        pb.fileParam.ioDirID   = dirID;
        pb.ioParam.ioPermssn   = perm;
        err = PBHOpenSync(&pb);
    }
    *outRefNum = (err == noErr) ? pb.ioParam.ioRefNum : 0;
    return err;
}

OSErr FileIOCreate(short vRefNum, long dirID, ConstStr255Param name,
                   OSType creator, OSType type)
{
    HParamBlockRec pb;
    OSErr err;

    ZeroStruct(pb);
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vRefNum;
    pb.fileParam.ioDirID   = dirID;
    err = PBHCreateSync(&pb);
    if (err != noErr) return err;
    return FileIOSetTypeAndCreator(vRefNum, dirID, name, type, creator);
}

OSErr FileIOReadDoc(short vRefNum, long dirID, ConstStr255Param name,
                    Handle *outData,
                    LineEndKind *outLE,
                    long *outFoldCount)
{
    short refNum;
    long eof = 0;
    long readLen;
    OSErr err;
    Handle h = NULL;
    LineEndKind le;
    long folded;

    err = FileIOOpen(vRefNum, dirID, name, fsRdPerm, &refNum);
    if (err != noErr) return err;

    err = GetEOF(refNum, &eof);
    if (err != noErr) { FSClose(refNum); return err; }

    if (eof > kMdMaxFileBytes) {
        FSClose(refNum);
        return -1;
    }

    h = NewHandle(eof);
    if (h == NULL) { FSClose(refNum); return MemError(); }

    HLock(h);
    readLen = eof;
    err = FSRead(refNum, &readLen, *h);
    FSClose(refNum);

    if (err != noErr && err != eofErr) {
        DisposeHandle(h);
        return err;
    }

    le = DetectLineEnding(*h, readLen);
    {
        long newLen = FoldAndNormalize(*h, readLen, le, &folded);
        SetHandleSize(h, newLen);
    }
    HUnlock(h);

    *outData = h;
    if (outLE) *outLE = le;
    if (outFoldCount) *outFoldCount = folded;
    return noErr;
}

OSErr FileIOWriteDoc(short vRefNum, long dirID, ConstStr255Param name,
                     const char *data, long len, LineEndKind le)
{
    short refNum = 0;
    OSErr err;
    OSErr closeErr;
    Handle outH;
    char *out;
    long outLen = 0;
    long i;
    long writeLen;

    {
        long outMax = (le == kLE_CRLF) ? (len * 2L + 2L) : (len + 1L);
        if (outMax < 1) outMax = 1;
        outH = NewHandle(outMax);
        if (outH == NULL) return MemError();
        HLock(outH);
        out = *outH;
    }

    for (i = 0; i < len; i++) {
        if (data[i] == '\r') {
            if (le == kLE_LF) {
                out[outLen++] = '\n';
            } else if (le == kLE_CRLF) {
                out[outLen++] = '\r';
                out[outLen++] = '\n';
            } else {
                out[outLen++] = '\r';
            }
        } else {
            out[outLen++] = data[i];
        }
    }

    /* Overwrite the existing file in place rather than delete + create.
       Delete-first destroyed the user's only copy whenever the
       subsequent Create/Open/Write failed (disk full, locked disk),
       and it threw away the file's Finder info, creation date and
       resource fork on every save. Only create when it's missing.
       fsRdWrPerm (not fsCurPerm) so a locked file fails here, cleanly,
       instead of opening read-only. */
    err = FileIOOpen(vRefNum, dirID, name, fsRdWrPerm, &refNum);
    if (err == fnfErr) {
        err = FileIOCreate(vRefNum, dirID, name, 'MDED', 'TEXT');
        if (err == noErr)
            err = FileIOOpen(vRefNum, dirID, name, fsRdWrPerm, &refNum);
    }
    if (err != noErr) {
        refNum = 0;
        goto done;
    }

    /* Size the file first. Growing allocates the space up front, so a
       full disk fails here with the original contents still intact. */
    err = SetEOF(refNum, outLen);
    if (err != noErr) goto done;

    err = SetFPos(refNum, fsFromStart, 0);
    if (err != noErr) goto done;

    writeLen = outLen;
    err = FSWrite(refNum, &writeLen, out);
    if (err == noErr && writeLen != outLen) err = ioErr;

done:
    if (refNum != 0) {
        closeErr = FSClose(refNum);
        if (err == noErr) err = closeErr;
        /* Push the catalog + data to disk now; floppies get ejected. */
        (void) FlushVol(NULL, vRefNum);
    }

    HUnlock(outH);
    DisposeHandle(outH);
    return err;
}

OSErr FileIOSetTypeAndCreator(short vRefNum, long dirID, ConstStr255Param name,
                              OSType type, OSType creator)
{
    HParamBlockRec pb;
    OSErr err;

    /* Zeroed so ioFDirIndex is 0 (look up by NAME). With stack garbage
       there -- as the Multiversal SetFInfo glue leaves it -- the call
       does an indexed lookup, returns some other file's info and
       overwrites the caller's name buffer. */
    ZeroStruct(pb);
    pb.fileParam.ioNamePtr   = (StringPtr)name;
    pb.fileParam.ioVRefNum   = vRefNum;
    pb.fileParam.ioDirID     = dirID;
    pb.fileParam.ioFDirIndex = 0;
    err = PBHGetFInfoSync(&pb);
    if (err != noErr) return err;

    pb.fileParam.ioFlFndrInfo.fdType    = type;
    pb.fileParam.ioFlFndrInfo.fdCreator = creator;

    /* PBHGetFInfo returns the file number in ioDirID; put the folder
       back (and re-assert the rest) before the set call. */
    pb.fileParam.ioNamePtr   = (StringPtr)name;
    pb.fileParam.ioVRefNum   = vRefNum;
    pb.fileParam.ioDirID     = dirID;
    pb.fileParam.ioFVersNum  = 0;
    pb.fileParam.ioFDirIndex = 0;
    return PBHSetFInfoSync(&pb);
}

Boolean FileIOFileExists(short vRefNum, long dirID, ConstStr255Param name)
{
    CInfoPBRec pb;
    Str63 nm;
    if (name[0] == 0 || name[0] > 63) return false;
    PStrCopy(nm, name);   /* PBGetCatInfo may write to ioNamePtr */
    ZeroStruct(pb);
    pb.hFileInfo.ioNamePtr   = nm;
    pb.hFileInfo.ioVRefNum   = vRefNum;
    pb.hFileInfo.ioFDirIndex = 0;          /* look up by name */
    pb.hFileInfo.ioDirID     = dirID;
    if (PBGetCatInfoSync(&pb) != noErr) return false;
    return (pb.hFileInfo.ioFlAttrib & 0x10) == 0;   /* not a directory */
}
