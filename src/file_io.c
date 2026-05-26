#include "file_io.h"

#include <Files.h>
#include <Memory.h>

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

/* ---- wdRefNum ownership ----

   Standard File / 'odoc' Apple Events hand us references to folders.
   The File Manager's working-directory pool can RECYCLE entries we
   don't own, so a wdRefNum cached on a document can later point at
   a different folder and clobber a same-named file there.

   FileIOOwnWD opens a wdRefNum that *we* own via PBOpenWDSync — the
   system won't recycle it until we PBCloseWDSync. From that point on
   every file op uses the classic System-1-era (vRefNum, name) calls
   that work universally on System 6 / Mini vMac and don't have the
   reliability problems of the H- or PBH-prefixed variants. */

OSErr FileIOOwnWDFromDir(short vRefNum, long dirID, short *outOwnedWD)
{
    WDPBRec pb;
    OSErr err;
    pb.ioCompletion = NULL;
    pb.ioNamePtr    = NULL;
    pb.ioVRefNum    = vRefNum;
    pb.ioWDDirID    = dirID;
    pb.ioWDProcID   = 'MDED';
    err = PBOpenWDSync(&pb);
    if (err != noErr) return err;
    if (outOwnedWD) *outOwnedWD = pb.ioVRefNum;
    return noErr;
}

OSErr FileIOOwnWD(short anyWDRefNum, short *outOwnedWD)
{
    WDPBRec pb;
    OSErr err;
    /* Resolve whatever ref we were given (may already be a real
       vRefNum, may be a wdRefNum from SF) into (vRefNum, dirID). */
    pb.ioCompletion = NULL;
    pb.ioNamePtr    = NULL;
    pb.ioVRefNum    = anyWDRefNum;
    pb.ioWDIndex    = 0;
    pb.ioWDProcID   = 0;
    pb.ioWDVRefNum  = 0;
    err = PBGetWDInfoSync(&pb);
    if (err != noErr) return err;
    return FileIOOwnWDFromDir(pb.ioWDVRefNum, pb.ioWDDirID, outOwnedWD);
}

void FileIOReleaseWD(short ownedWD)
{
    WDPBRec pb;
    if (ownedWD == 0) return;
    pb.ioCompletion = NULL;
    pb.ioNamePtr    = NULL;
    pb.ioVRefNum    = ownedWD;
    (void) PBCloseWDSync(&pb);
}

/* ---- File ops ---- Plain non-H calls; the vRefNum is an owned wdRefNum. */

OSErr FileIOReadDoc(short vRefNum, ConstStr255Param name,
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

    err = FSOpen(name, vRefNum, &refNum);
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

OSErr FileIOWriteDoc(short vRefNum, ConstStr255Param name,
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

    (void) FSDelete(name, vRefNum);  /* fnfErr is fine, ignore all errors */

    err = Create(name, vRefNum, 'MDED', 'TEXT');
    if (err != noErr) goto done;

    err = FSOpen(name, vRefNum, &refNum);
    if (err != noErr) goto done;

    writeLen = outLen;
    err = FSWrite(refNum, &writeLen, out);

done:
    if (refNum != 0) {
        closeErr = FSClose(refNum);
        if (err == noErr) err = closeErr;
    }

    HUnlock(outH);
    DisposeHandle(outH);
    return err;
}

OSErr FileIOSetTypeAndCreator(short vRefNum, ConstStr255Param name,
                              OSType type, OSType creator)
{
    ParamBlockRec pb;
    OSErr err;
    short i;

    /* The Multiversal Interfaces `SetFInfo` glue doesn't clear
       ioFDirIndex before calling PBGetFInfoSync. If the stack has
       nonzero garbage there, PBGetFInfoSync does an *indexed* lookup,
       returns a random file's FInfo, AND overwrites the caller's name
       buffer (ioNamePtr) with the indexed file's name — clobbering
       doc->fileName and then setting the type/creator on the wrong
       file. We bypass the glue and zero the whole block first. */
    {
        char *p = (char *)&pb;
        for (i = 0; i < (short)sizeof(pb); i++) p[i] = 0;
    }
    pb.fileParam.ioNamePtr   = (StringPtr)name;
    pb.fileParam.ioVRefNum   = vRefNum;
    pb.fileParam.ioFVersNum  = 0;
    pb.fileParam.ioFDirIndex = 0;   /* lookup by NAME, not by index */
    err = PBGetFInfoSync(&pb);
    if (err != noErr) return err;

    pb.fileParam.ioFlFndrInfo.fdType    = type;
    pb.fileParam.ioFlFndrInfo.fdCreator = creator;

    /* Restore fields PBGetFInfoSync may have clobbered before the
       set call. ioNamePtr in particular gets overwritten on indexed
       lookups; we always restore as defense in depth. */
    pb.fileParam.ioNamePtr   = (StringPtr)name;
    pb.fileParam.ioVRefNum   = vRefNum;
    pb.fileParam.ioFVersNum  = 0;
    pb.fileParam.ioFDirIndex = 0;
    return PBSetFInfoSync(&pb);
}
