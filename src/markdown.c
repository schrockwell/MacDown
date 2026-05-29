#include "markdown.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Memory.h>
#include <TextEdit.h>

#define UC(c) ((unsigned char)(c))

/* ---- line classification ---- */

static short SkipLeadingSpaces(const char *line, short len, short max)
{
    short i = 0;
    while (i < len && i < max && line[i] == ' ') i++;
    return i;
}

MdLineKind MdClassifyLine(const char *line, short len)
{
    short i;

    if (len == 0) return kLine_Plain;

    i = SkipLeadingSpaces(line, len, 3);

    /* Headers: 1-6 hashes followed by a space. */
    if (i < len && line[i] == '#') {
        short hashes = 0;
        while (i < len && line[i] == '#' && hashes < 7) { hashes++; i++; }
        if (hashes >= 1 && hashes <= 6 && i < len && line[i] == ' ') {
            return (MdLineKind)(kLine_H1 + (hashes - 1));
        }
    }

    /* Blockquote: '>' optionally followed by a space and content. */
    if (i < len && line[i] == '>') {
        return kLine_Blockquote;
    }

    /* List markers: '-', '*', '+' followed by space. */
    if (i + 1 < len &&
        (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
        line[i + 1] == ' ')
    {
        /* Check for task-list extension: "- [ ]" or "- [x]" */
        if (i + 5 < len &&
            line[i + 2] == '[' && line[i + 4] == ']' &&
            (i + 5 == len || line[i + 5] == ' '))
        {
            char c = line[i + 3];
            if (c == ' ')                       return kLine_TaskUnchecked;
            if (c == 'x' || c == 'X')           return kLine_TaskChecked;
        }
        return kLine_UnorderedItem;
    }

    /* Ordered list: digits followed by '.' and space. */
    if (i < len && line[i] >= '0' && line[i] <= '9') {
        short j = i;
        while (j < len && line[j] >= '0' && line[j] <= '9') j++;
        if (j + 1 < len && line[j] == '.' && line[j + 1] == ' ') {
            return kLine_OrderedItem;
        }
    }

    return kLine_Plain;
}

/* ---- line bounds in a TEHandle ---- */

void MdFindLineBounds(TEHandle te, short pos,
                      short *lineStart, short *lineEnd)
{
    short len = (**te).teLength;
    CharsHandle ch = TEGetText(te);
    const char *text;
    short start, end;

    if (pos < 0) pos = 0;
    if (pos > len) pos = len;

    HLock((Handle)ch);
    text = *ch;

    start = pos;
    while (start > 0 && text[start - 1] != '\r') start--;

    end = pos;
    while (end < len && text[end] != '\r') end++;

    HUnlock((Handle)ch);

    *lineStart = start;
    *lineEnd = end;
}

/* ---- inline emphasis scanner ---- */

static Boolean IsWordChar(char c)
{
    unsigned char u = UC(c);
    return (u >= '0' && u <= '9') ||
           (u >= 'A' && u <= 'Z') ||
           (u >= 'a' && u <= 'z') ||
           u >= 0x80;  /* MacRoman extended chars treated as word chars */
}

/* Counter of "real" style mutations applied since the last consume.
   DocFlushRestyle reads it after restyling its dirty range to decide
   whether the on-screen repaint is needed at all. */
static long sStyleChanges = 0;

short MdConsumeStyleChanges(void)
{
    short n = (sStyleChanges > 32767) ? 32767 : (short)sStyleChanges;
    sStyleChanges = 0;
    return n;
}

static void ApplyFace(TEHandle te, short start, short end, short face)
{
    TextStyle ts;
    ts.tsFont  = 0;
    ts.tsFace  = face;
    ts.tsSize  = 0;
    ts.tsColor.red = ts.tsColor.green = ts.tsColor.blue = 0;
    TESetSelect(start, end, te);
    TESetStyle(doFace, &ts, false, te);
    sStyleChanges++;
}

/* Scan one line for *...*, **...**, _..._, __...__ runs and bold their
   content. Single-pass: when we see a delimiter, find the matching one
   that satisfies the "flanking" rule (left side preceded by start-of-
   line or whitespace; right side followed by end-of-line/punctuation/
   whitespace). */
static void StyleInlineEmphasis(TEHandle te,
                                const char *text, short lineStart, short lineLen)
{
    short i = 0;

    while (i < lineLen) {
        char c = text[i];
        short runLen;
        short contentStart;
        short j;
        short closer;

        if (c != '*' && c != '_') { i++; continue; }

        /* Left-flanking: opening delimiter mustn't immediately follow a
           word char (avoids matching intra-word `*` like "bird*song"). */
        if (i > 0 && IsWordChar(text[i - 1])) { i++; continue; }

        /* Run length: 1 or 2. */
        runLen = 1;
        if (i + 1 < lineLen && text[i + 1] == c) runLen = 2;

        contentStart = i + runLen;
        if (contentStart >= lineLen) { i++; continue; }

        /* Char right after the opener must not be whitespace. */
        if (text[contentStart] == ' ' || text[contentStart] == '\t') {
            i++; continue;
        }

        /* Find a matching closer of the same run length. */
        j = contentStart;
        closer = -1;
        while (j < lineLen) {
            if (text[j] == c) {
                short found = 1;
                if (runLen == 2 && j + 1 < lineLen && text[j + 1] == c) found = 2;
                if (found == runLen) {
                    /* Right-flanking: char before closer not whitespace,
                       and char after closer not a word char (end-of-line
                       or punctuation/space is fine). */
                    char before = text[j - 1];
                    char after = (j + runLen < lineLen) ? text[j + runLen] : ' ';
                    if (before != ' ' && before != '\t' && !IsWordChar(after)) {
                        closer = j;
                        break;
                    }
                }
            }
            j++;
        }

        if (closer < 0) { i++; continue; }

        ApplyFace(te, lineStart + contentStart, lineStart + closer,
                  (runLen == 2) ? bold : italic);
        i = closer + runLen;
    }
}

/* ---- per-line styling ---- */

void MdRestyleLine(TEHandle te, short lineStart, short lineEnd)
{
    short savedStart, savedEnd;
    short lineLen;
    TextStyle base;
    short faceMask = doFace | doSize;
    CharsHandle ch;
    char *text;
    MdLineKind kind;

    savedStart = (**te).selStart;
    savedEnd   = (**te).selEnd;

    lineLen = lineEnd - lineStart;
    if (lineLen < 0) lineLen = 0;

    /* Fast no-op detection: if the line classifies as plain, has no
       emphasis markers, and already has the default style at its first
       character, the restyle is guaranteed to produce no visible
       change -- skip everything and don't bump sStyleChanges.  The
       caller (DocFlushRestyle) then skips its offscreen repaint. */
    if (lineLen > 0) {
        Boolean canSkip = true;
        short i;
        TextStyle current;
        short lh, fa;

        ch = TEGetText(te);
        HLock((Handle)ch);
        text = *ch + lineStart;

        if (MdClassifyLine(text, lineLen) != kLine_Plain) {
            canSkip = false;
        } else {
            for (i = 0; i < lineLen; i++) {
                if (text[i] == '*' || text[i] == '_') { canSkip = false; break; }
            }
        }
        HUnlock((Handle)ch);

        if (canSkip) {
            TEGetStyle(lineStart, &current, &lh, &fa, te);
            if (current.tsFace == 0 && current.tsSize == 12)
                return;
        }
    }

    /* Reset the whole line to plain size 12 first so previous styling
       doesn't leak. Font is left alone -- we don't restyle by font. */
    base.tsFont = 0;
    base.tsFace = 0;
    base.tsSize = 12;
    base.tsColor.red = base.tsColor.green = base.tsColor.blue = 0;

    TESetSelect(lineStart, lineEnd, te);
    TESetStyle(faceMask, &base, false, te);
    sStyleChanges++;

    if (lineLen == 0) {
        TESetSelect(savedStart, savedEnd, te);
        return;
    }

    ch = TEGetText(te);
    HLock((Handle)ch);
    text = *ch + lineStart;

    kind = MdClassifyLine(text, lineLen);

    /* Headings get distinct faces but all stay at size 12, so line
       heights never change and callers never need TECalText. */
    base.tsSize = 12;
    switch (kind) {
        case kLine_H1:
        case kLine_H2: base.tsFace = bold | underline; break;
        case kLine_H3:
        case kLine_H4:
        case kLine_H5:
        case kLine_H6: base.tsFace = bold;             break;
        default:       base.tsFace = 0;                break;
    }

    if (kind != kLine_Plain) {
        TESetSelect(lineStart, lineEnd, te);
        TESetStyle(faceMask, &base, false, te);
        sStyleChanges++;
    }

    /* Inline emphasis scan -- ApplyFace bumps sStyleChanges per run. */
    StyleInlineEmphasis(te, text, lineStart, lineLen);

    HUnlock((Handle)ch);

    TESetSelect(savedStart, savedEnd, te);
}

void MdRestyleAll(TEHandle te)
{
    short len = (**te).teLength;
    short start = 0;
    short end;
    short savedStart, savedEnd;
    CharsHandle ch;
    char *text;
    TextStyle base;
    short faceMask = doFace | doSize;
    GrafPtr savedPort = NULL;
    GrafPtr inPort = (**te).inPort;
    RgnHandle savedClip = NULL;
    RgnHandle emptyRgn = NULL;
    MdLineKind kind;

    /* Suppress drawing for the whole pass: TESetStyle paints into TE's
       inPort on every call, which is what makes loading a long file
       slow. Clip to an empty region so style runs update in memory but
       nothing paints; the caller InvalRects for a single clean repaint. */
    if (inPort) {
        GetPort(&savedPort);
        SetPort(inPort);
        savedClip = NewRgn();
        emptyRgn = NewRgn();
        GetClip(savedClip);
        SetClip(emptyRgn);
    }

    savedStart = (**te).selStart;
    savedEnd   = (**te).selEnd;

    /* Reset the entire document to plain size 12 in a single call,
       instead of repeating that reset on every line. For a long mostly-
       plain doc this collapses N TESetStyle reset calls into 1. */
    base.tsFont = 0;
    base.tsFace = 0;
    base.tsSize = 12;
    base.tsColor.red = base.tsColor.green = base.tsColor.blue = 0;
    if (len > 0) {
        TESetSelect(0, len, te);
        TESetStyle(faceMask, &base, false, te);
        sStyleChanges++;
    }

    ch = TEGetText(te);
    HLock((Handle)ch);
    text = *ch;

    while (start <= len) {
        end = start;
        while (end < len && text[end] != '\r') end++;

        if (end > start) {
            kind = MdClassifyLine(text + start, end - start);

            /* Plain lines already have the default style from the
               document-wide reset above -- skip the per-line TESetStyle.
               All heading levels stay at size 12 so line heights are
               uniform and TECalText is never needed. */
            if (kind >= kLine_H1 && kind <= kLine_H6) {
                base.tsSize = 12;
                if (kind == kLine_H1 || kind == kLine_H2)
                    base.tsFace = bold | underline;
                else
                    base.tsFace = bold;
                TESetSelect(start, end, te);
                TESetStyle(faceMask, &base, false, te);
                sStyleChanges++;
            }

            StyleInlineEmphasis(te, text + start, start, end - start);
            text = *ch;  /* StyleInlineEmphasis may have moved the handle */
        }

        if (end >= len) break;
        start = end + 1;  /* skip CR */
    }

    HUnlock((Handle)ch);

    TESetSelect(savedStart, savedEnd, te);

    if (savedClip) {
        SetClip(savedClip);
        DisposeRgn(savedClip);
        DisposeRgn(emptyRgn);
        SetPort(savedPort);
    }

    /* No TECalText needed: every style we apply is size 12, so line
       heights computed at TESetText time stay correct. */
}

/* ---- list continuation marker ---- */

Boolean MdNextListMarker(TEHandle te, short pos,
                         char *outMarker, Boolean *outIsEmpty)
{
    short lineStart, lineEnd;
    short len;
    CharsHandle ch;
    char *text;
    short i, leading, contentStart;
    Boolean result = false;
    MdLineKind kind;

    MdFindLineBounds(te, pos, &lineStart, &lineEnd);
    len = lineEnd - lineStart;

    if (outIsEmpty) *outIsEmpty = false;
    outMarker[0] = 0;

    if (len == 0) return false;

    ch = TEGetText(te);
    HLock((Handle)ch);
    text = *ch + lineStart;

    /* Count every leading tab / space (capped to leave room for the
       marker in the caller's 24-byte buffer), classify the line as
       if it started after the indent, and remember `leading` so we
       can prefix the continuation marker with the same indent below. */
    leading = 0;
    while (leading < len && leading < 16 &&
           (text[leading] == ' ' || text[leading] == '\t'))
        leading++;

    kind = MdClassifyLine(text + leading, len - leading);

    /* Marker scan starts right after the indent. */
    i = leading;

    switch (kind) {
        case kLine_UnorderedItem:
            /* "- " / "* " / "+ " */
            contentStart = i + 2;
            outMarker[0] = 2;
            outMarker[1] = text[i];
            outMarker[2] = ' ';
            result = true;
            break;

        case kLine_TaskUnchecked:
        case kLine_TaskChecked:
            /* "- [ ] " — next line always gets a fresh unchecked box. */
            contentStart = i + 6;
            outMarker[0] = 6;
            outMarker[1] = text[i];
            outMarker[2] = ' ';
            outMarker[3] = '[';
            outMarker[4] = ' ';
            outMarker[5] = ']';
            outMarker[6] = ' ';
            result = true;
            break;

        case kLine_OrderedItem: {
            short n = 0, j;
            for (j = i; j < len && text[j] >= '0' && text[j] <= '9'; j++) {
                n = n * 10 + (text[j] - '0');
            }
            contentStart = j + 2;  /* digits + '.' + ' ' */
            n++;
            /* Format n into outMarker as "N. " */
            {
                char tmp[8];
                short k = 0, m;
                if (n == 0) tmp[k++] = '0';
                else { short v = n; while (v) { tmp[k++] = '0' + (v % 10); v /= 10; } }
                /* tmp is reversed; copy back. */
                outMarker[0] = (char)(k + 2);
                for (m = 0; m < k; m++) outMarker[1 + m] = tmp[k - 1 - m];
                outMarker[1 + k] = '.';
                outMarker[2 + k] = ' ';
            }
            result = true;
            break;
        }

        case kLine_Blockquote: {
            /* Skip the '>' and an optional space; the rest is content. */
            short j = i;
            if (j < len && text[j] == '>') {
                j++;
                if (j < len && text[j] == ' ') j++;
            }
            contentStart = j;
            outMarker[0] = 2;
            outMarker[1] = '>';
            outMarker[2] = ' ';
            result = true;
            break;
        }

        default:
            contentStart = len;
            break;
    }

    /* Determine "empty content" — only whitespace after the marker. */
    if (result && outIsEmpty) {
        short j = contentStart;
        Boolean empty = true;
        while (j < len) {
            if (text[j] != ' ' && text[j] != '\t') { empty = false; break; }
            j++;
        }
        *outIsEmpty = empty;
    }

    /* Prefix the marker with the same indent so the next line lines up
       with the current list item. Shift the existing marker bytes right
       by `leading` and copy the indent into the freed prefix. */
    if (result && leading > 0) {
        short markerLen = (unsigned char)outMarker[0];
        short j;
        for (j = markerLen; j >= 1; j--)
            outMarker[j + leading] = outMarker[j];
        for (j = 0; j < leading; j++)
            outMarker[1 + j] = text[j];
        outMarker[0] = (char)(markerLen + leading);
    }

    HUnlock((Handle)ch);
    return result;
}

/* ---- ordered list renumber ---- */

/* Read the integer at the start of an ordered-list line. Returns -1
   if `lineStart` isn't an ordered list item. `outDigitsStart` and
   `outDigitsEnd` (if non-null) receive the byte offsets of the
   digit run within the line so the caller knows what to replace. */
static long ReadOrderedNumber(TEHandle te,
                              short lineStart, short lineEnd,
                              short *outDigitsStart, short *outDigitsEnd)
{
    CharsHandle ch;
    char *text;
    short i, digitsStart;
    long  n;
    MdLineKind kind;

    if (lineStart >= lineEnd) return -1;
    ch = TEGetText(te);
    HLock((Handle)ch);
    text = *ch + lineStart;
    kind = MdClassifyLine(text, lineEnd - lineStart);
    if (kind != kLine_OrderedItem) { HUnlock((Handle)ch); return -1; }

    i = 0;
    while (i < lineEnd - lineStart && text[i] == ' ') i++;
    digitsStart = i;
    n = 0;
    while (i < lineEnd - lineStart && text[i] >= '0' && text[i] <= '9') {
        n = n * 10 + (text[i] - '0');
        i++;
    }
    HUnlock((Handle)ch);

    if (outDigitsStart) *outDigitsStart = lineStart + digitsStart;
    if (outDigitsEnd)   *outDigitsEnd   = lineStart + i;
    return n;
}

/* Adjust a tracked caret position after a digit-run replacement.
   `start`/`end` are the bounds of the OLD digit run; `bufLen` is the
   length of the new run. Positions strictly before the run are
   unchanged; positions at or beyond the old end shift by the delta;
   positions inside the old run land at the new run's end. */
static void AdjustCaret(short *caret, short start, short end, short bufLen)
{
    short delta;
    if (caret == NULL) return;
    delta = bufLen - (end - start);
    if (*caret >= end) {
        *caret += delta;
    } else if (*caret > start) {
        *caret = start + bufLen;
    }
}

long MdRenumberOrderedList(TEHandle te, short lineStart,
                           short *ioSelStart, short *ioSelEnd)
{
    short lineEnd;
    long  curNum;
    long  expected;
    long  totalDelta = 0;
    short scanFrom;
    short teLen;

    /* Identify the list head: walk backwards while the previous line
       is also an ordered item. Whatever number the head currently
       has, that's where the sequence starts. */
    MdFindLineBounds(te, lineStart, &lineStart, &lineEnd);
    if (ReadOrderedNumber(te, lineStart, lineEnd, NULL, NULL) < 0) return 0;

    while (lineStart > 0) {
        short pLs, pLe;
        MdFindLineBounds(te, lineStart - 1, &pLs, &pLe);
        if (ReadOrderedNumber(te, pLs, pLe, NULL, NULL) < 0) break;
        lineStart = pLs;
        lineEnd   = pLe;
    }

    curNum = ReadOrderedNumber(te, lineStart, lineEnd, NULL, NULL);
    if (curNum < 0) return 0;
    expected = curNum + 1;
    scanFrom = lineEnd;

    for (;;) {
        short ls, le;
        short digitsStart, digitsEnd;
        long  oldNum;
        char  buf[8];
        short bufLen, k, m;
        long  tmp;

        teLen = (**te).teLength;
        if (scanFrom >= teLen) break;
        MdFindLineBounds(te, scanFrom + 1, &ls, &le);
        oldNum = ReadOrderedNumber(te, ls, le, &digitsStart, &digitsEnd);
        if (oldNum < 0) break;

        if (oldNum != expected) {
            /* Format expected into buf (reversed, then output). */
            bufLen = 0;
            tmp = expected;
            if (tmp == 0) buf[bufLen++] = '0';
            else { while (tmp > 0) { buf[bufLen++] = (char)('0' + (tmp % 10)); tmp /= 10; } }
            /* Reverse in place. */
            for (k = 0, m = bufLen - 1; k < m; k++, m--) {
                char t = buf[k]; buf[k] = buf[m]; buf[m] = t;
            }
            TESetSelect(digitsStart, digitsEnd, te);
            TEDelete(te);
            TEInsert(buf, bufLen, te);
            AdjustCaret(ioSelStart, digitsStart, digitsEnd, bufLen);
            AdjustCaret(ioSelEnd,   digitsStart, digitsEnd, bufLen);
            totalDelta += (long)(bufLen - (digitsEnd - digitsStart));
            /* le shifted; recompute end-of-line so we walk past it next loop. */
            MdFindLineBounds(te, ls, &ls, &le);
        }
        expected++;
        scanFrom = le;
    }

    return totalDelta;
}

/* ---- task box locator ---- */

short MdFindTaskBox(TEHandle te, short pos)
{
    short lineStart, lineEnd, len;
    CharsHandle ch;
    char *text;
    short i;
    short result = -1;
    MdLineKind kind;

    MdFindLineBounds(te, pos, &lineStart, &lineEnd);
    len = lineEnd - lineStart;
    if (len < 5) return -1;

    ch = TEGetText(te);
    HLock((Handle)ch);
    text = *ch + lineStart;

    kind = MdClassifyLine(text, len);
    if (kind == kLine_TaskUnchecked || kind == kLine_TaskChecked) {
        i = SkipLeadingSpaces(text, len, 3);
        /* layout: <marker><space>[<state>] */
        result = lineStart + i + 3;
    }

    HUnlock((Handle)ch);
    return result;
}
