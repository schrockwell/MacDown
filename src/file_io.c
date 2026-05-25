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

/* ---- Public API ---- */

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

    /* FSOpen is the original (System 1+) call — takes vRefNum + name
       and uses the volume's working directory. Works with the
       wdRefNum that Standard File hands us. */
    err = FSOpen(name, vRefNum, &refNum);
    if (err != noErr) return err;

    err = GetEOF(refNum, &eof);
    if (err != noErr) { FSClose(refNum); return err; }

    if (eof > kMdMaxFileBytes) {
        FSClose(refNum);
        return -1;  /* sentinel: too big */
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

    /* Build the full output buffer in memory with line endings converted
       once, then a single FSWrite. Simpler than streaming-with-batches
       and avoids any per-line FSWrite cost on slow disks. Worst-case
       expansion is 2x (every CR becomes CRLF). */
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

    /* Canonical classic-Mac save sequence (Inside Macintosh: Files):
       delete the old version, create fresh, open for writing, write,
       close. All non-H calls — they take a plain vRefNum (which can
       be a wdRefNum from Standard File) and don't need a dirID. */
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
    FInfo fi;
    OSErr err;

    /* GetFInfo/SetFInfo are the original (System 1+) calls — they
       take a plain vRefNum (which can be a wdRefNum from Standard
       File) and work on every Mac OS. The PB-prefixed variants used
       previously were System 7-era and could fail silently on
       System 6, leaving the file with the wrong creator and thereby
       breaking double-click-to-open in the Finder. */
    err = GetFInfo(name, vRefNum, &fi);
    if (err != noErr) return err;

    fi.fdType    = type;
    fi.fdCreator = creator;

    return SetFInfo(name, vRefNum, &fi);
}
