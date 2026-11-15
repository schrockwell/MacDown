#include "file_io.h"

#include <Files.h>
#include <Memory.h>
#include <OSUtils.h>
#include <Gestalt.h>
#include <Events.h>
#include <TextUtils.h>

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

/* ---- Safe save ----

   An existing file is never overwritten in place: the new contents go
   to a temporary file in the same folder first, and only once that's
   completely on disk does it replace the original. A crash, full disk
   or ejected floppy mid-save leaves the original (and at worst a stray
   "MacDown save" file holding the new text) instead of a truncated
   document. */

/* FIDParam (Inside Macintosh: Files), which the Multiversal headers
   don't define. Used by PBExchangeFiles. */
typedef struct {
    QElemPtr  qLink;
    short     qType;
    short     ioTrap;
    Ptr       ioCmdAddr;
    void     *ioCompletion;
    OSErr     ioResult;
    StringPtr ioNamePtr;
    short     ioVRefNum;
    long      filler14;
    StringPtr ioDestNamePtr;
    long      filler15;
    long      ioDestDirID;
    long      filler16;
    long      filler17;
    long      ioSrcDirID;
    short     filler18;
    long      ioFileID;
} MDFIDParam;

/* PBExchangeFiles is part of the System 7 File Manager. Don't trust
   GetVolParms' bHasFileIDs bit: local HFS volumes under 7.5 leave it
   clear yet exchange fine. Just try it there; a volume that can't
   (some file servers) fails the call and we fall back. */
static Boolean CanExchange(void)
{
    long resp;
    return Gestalt(gestaltFSAttr, &resp) == noErr;
}

static OSErr ExchangeFiles(short vRefNum, long dirA, ConstStr255Param nameA,
                           long dirB, ConstStr255Param nameB)
{
    MDFIDParam pb;
    ZeroStruct(pb);
    pb.ioNamePtr     = (StringPtr)nameA;
    pb.ioVRefNum     = vRefNum;
    pb.ioSrcDirID    = dirA;
    pb.ioDestNamePtr = (StringPtr)nameB;
    pb.ioDestDirID   = dirB;
    return PBExchangeFilesSync((ParmBlkPtr)&pb);
}

static OSErr DeleteFile(short vRefNum, long dirID, ConstStr255Param name)
{
    HParamBlockRec pb;
    ZeroStruct(pb);
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vRefNum;
    pb.fileParam.ioDirID   = dirID;
    return PBHDeleteSync(&pb);
}

static OSErr RenameFile(short vRefNum, long dirID, ConstStr255Param oldName,
                        ConstStr255Param newName)
{
    HParamBlockRec pb;
    ZeroStruct(pb);
    pb.ioParam.ioNamePtr   = (StringPtr)oldName;
    pb.ioParam.ioVRefNum   = vRefNum;
    pb.ioParam.ioMisc      = (Ptr)newName;
    pb.fileParam.ioDirID   = dirID;
    return PBHRenameSync(&pb);
}

/* Catalog info for a file (ioDirID is clobbered by the call, so the
   caller resets it before PBSetCatInfo). */
static OSErr GetFileCatInfo(short vRefNum, long dirID, ConstStr255Param name,
                            CInfoPBRec *pb)
{
    ZeroStruct(*pb);
    pb->hFileInfo.ioNamePtr   = (StringPtr)name;
    pb->hFileInfo.ioVRefNum   = vRefNum;
    pb->hFileInfo.ioFDirIndex = 0;
    pb->hFileInfo.ioDirID     = dirID;
    return PBGetCatInfoSync(pb);
}

/* "MacDown save 1234" / "MacDown old 1234": short enough for any
   volume, and recognizable if a crash leaves one behind. */
static void MakeScratchName(Str63 out, ConstStr255Param prefix, short n)
{
    Str15 num;
    short i;
    NumToString(n, num);
    PStrCopy(out, prefix);
    for (i = 1; i <= num[0] && out[0] < 31; i++)
        out[++out[0]] = num[i];
}

/* Create a uniquely named file in (vRefNum, dirID). */
static OSErr CreateScratch(short vRefNum, long dirID, ConstStr255Param prefix,
                           Str63 outName)
{
    short n = (short)(TickCount() & 0x0FFF), tries;
    OSErr err = dupFNErr;
    for (tries = 0; tries < 20 && err == dupFNErr; tries++, n++)
    {
        MakeScratchName(outName, prefix, n);
        err = FileIOCreate(vRefNum, dirID, outName, 'MDED', 'TEXT');
    }
    return err;
}

/* Make sure a file exists only once, under the scratch name: used to
   find a free name for the old copy before renaming onto it. */
static OSErr FreeScratchName(short vRefNum, long dirID, ConstStr255Param prefix,
                             Str63 outName)
{
    short n = (short)(TickCount() & 0x0FFF), tries;
    for (tries = 0; tries < 20; tries++, n++)
    {
        MakeScratchName(outName, prefix, n);
        if (!FileIOFileExists(vRefNum, dirID, outName))
            return noErr;
    }
    return dupFNErr;
}

/* Replace a file's data fork with `out`. */
static OSErr WriteDataFork(short vRefNum, long dirID, ConstStr255Param name,
                           const char *out, long outLen)
{
    short refNum;
    long writeLen = outLen;
    OSErr err, closeErr;

    err = FileIOOpen(vRefNum, dirID, name, fsRdWrPerm, &refNum);
    if (err != noErr)
        return err;
    /* Size first: a full disk fails here, before any bytes move. */
    err = SetEOF(refNum, outLen);
    if (err == noErr)
        err = SetFPos(refNum, fsFromStart, 0);
    if (err == noErr)
        err = FSWrite(refNum, &writeLen, out);
    if (err == noErr && writeLen != outLen)
        err = ioErr;
    closeErr = FSClose(refNum);
    return (err != noErr) ? err : closeErr;
}

static OSErr OpenResFork(short vRefNum, long dirID, ConstStr255Param name,
                         SignedByte perm, short *outRefNum)
{
    HParamBlockRec pb;
    OSErr err;
    ZeroStruct(pb);
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = vRefNum;
    pb.fileParam.ioDirID   = dirID;
    pb.ioParam.ioPermssn   = perm;
    err = PBHOpenRFSync(&pb);
    *outRefNum = (err == noErr) ? pb.ioParam.ioRefNum : 0;
    return err;
}

/* Copy the original's resource fork (if any) to the new file, so other
   apps' resources (e.g. a 'styl' or 'ckid') survive the swap. */
static OSErr CopyResFork(short vRefNum, long srcDir, ConstStr255Param src,
                         long dstDir, ConstStr255Param dst)
{
    short in, outRef;
    long eof = 0, done = 0;
    Handle buf;
    OSErr err, err2;

    err = OpenResFork(vRefNum, srcDir, src, fsRdPerm, &in);
    if (err != noErr)
        return noErr;    /* no resource fork to keep */
    GetEOF(in, &eof);
    if (eof == 0) {
        FSClose(in);
        return noErr;
    }
    err = OpenResFork(vRefNum, dstDir, dst, fsRdWrPerm, &outRef);
    if (err != noErr) {
        FSClose(in);
        return err;
    }
    buf = NewHandle(4096);
    if (buf == NULL) err = MemError();
    if (err == noErr) err = SetEOF(outRef, eof);
    if (err == noErr) {
        HLock(buf);
        while (err == noErr && done < eof) {
            long n = eof - done;
            if (n > 4096) n = 4096;
            err = FSRead(in, &n, *buf);
            if (err == noErr || err == eofErr) {
                long w = n;
                err = FSWrite(outRef, &w, *buf);
                if (err == noErr && w != n) err = ioErr;
                done += n;
                if (n == 0) break;
            }
        }
    }
    if (buf != NULL) DisposeHandle(buf);
    FSClose(in);
    err2 = FSClose(outRef);
    return (err != noErr) ? err : err2;
}

/* Swap the finished temp file in for the original. */
static OSErr ReplaceWithScratch(short vRefNum, long dirID,
                                ConstStr255Param name, ConstStr255Param tmp)
{
    CInfoPBRec ci;
    Str63 old;
    OSErr err;

    /* System 7: one catalog operation swaps the contents, and the
       original keeps its Finder info, dates, file ID and aliases. The
       temp file then holds the old contents and is deleted. */
    if (CanExchange()
        && ExchangeFiles(vRefNum, dirID, tmp, dirID, name) == noErr)
    {
        (void) DeleteFile(vRefNum, dirID, tmp);
        return noErr;
    }

    /* Elsewhere (System 6, some file servers): move the original aside,
       give the temp file its name, carry over its Finder info and
       creation date, then delete the old copy. Each step can be undone
       if the next one fails. */
    err = GetFileCatInfo(vRefNum, dirID, name, &ci);
    if (err != noErr) return err;
    err = FreeScratchName(vRefNum, dirID, "\pMacDown old ", old);
    if (err != noErr) return err;
    err = RenameFile(vRefNum, dirID, name, old);
    if (err != noErr) return err;
    err = RenameFile(vRefNum, dirID, tmp, name);
    if (err != noErr) {
        (void) RenameFile(vRefNum, dirID, old, name);
        return err;
    }
    {
        CInfoPBRec now;
        if (GetFileCatInfo(vRefNum, dirID, name, &now) == noErr) {
            now.hFileInfo.ioFlFndrInfo  = ci.hFileInfo.ioFlFndrInfo;
            now.hFileInfo.ioFlXFndrInfo = ci.hFileInfo.ioFlXFndrInfo;
            now.hFileInfo.ioFlCrDat     = ci.hFileInfo.ioFlCrDat;
            now.hFileInfo.ioNamePtr     = (StringPtr)name;
            now.hFileInfo.ioDirID       = dirID;
            (void) PBSetCatInfoSync(&now);
        }
    }
    (void) DeleteFile(vRefNum, dirID, old);
    return noErr;
}

OSErr FileIOWriteDoc(short vRefNum, long dirID, ConstStr255Param name,
                     const char *data, long len, LineEndKind le)
{
    OSErr err;
    Handle outH;
    char *out;
    long outLen = 0;
    long i;

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

    if (!FileIOFileExists(vRefNum, dirID, name)) {
        /* New file: nothing to protect. */
        err = FileIOCreate(vRefNum, dirID, name, 'MDED', 'TEXT');
        if (err == noErr)
            err = WriteDataFork(vRefNum, dirID, name, out, outLen);
    } else {
        short probe;
        Str63 tmp;

        /* Fail early, and cleanly, if the original can't be written at
           all (locked file or disk, open in another app). fsRdWrPerm
           rather than fsCurPerm so a locked file fails instead of
           opening read-only. */
        err = FileIOOpen(vRefNum, dirID, name, fsRdWrPerm, &probe);
        if (err == noErr) {
            FSClose(probe);
            err = CreateScratch(vRefNum, dirID, "\pMacDown save ", tmp);
        }
        if (err == noErr) {
            err = WriteDataFork(vRefNum, dirID, tmp, out, outLen);
            if (err == noErr)
                err = CopyResFork(vRefNum, dirID, name, dirID, tmp);
            if (err == noErr)
                err = ReplaceWithScratch(vRefNum, dirID, name, tmp);
            if (err != noErr)
                (void) DeleteFile(vRefNum, dirID, tmp);   /* original untouched */
        }
    }

    /* Push the catalog + data to disk now; floppies get ejected. */
    (void) FlushVol(NULL, vRefNum);

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
