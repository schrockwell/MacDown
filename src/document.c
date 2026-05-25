#include "document.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <TextEdit.h>
#include <Memory.h>
#include <Resources.h>
#include <Sound.h>
#include <Dialogs.h>
#include <StandardFile.h>
#include <Devices.h>
#include <ToolUtils.h>
#include <Events.h>
#include <Multiverse.h>

/* Yield a tick to the system after a modal dialog dismisses. Standard
   File leaves the OS in a state where the next File Manager call from
   the main thread can wedge if we go straight into it. EventAvail
   peeks the event queue (non-destructive, silent), which forces the
   OS to service its internal queues. */
static void YieldOnce(void)
{
    EventRecord ev;
    EventAvail(0, &ev);
}

#include "markdown.h"
#include "file_io.h"

DocState gDoc;

#define kWindowID        128
#define kSaveChangesALRT 129
#define kErrStrListID    128
#define kErrCantOpen     1
#define kErrTooBig       2
#define kErrFolded       3
#define kErrCantSave     4
#define kErrCantCreate   5

/* ---- forward decls ---- */
static pascal void ScrollAction(ControlHandle ctl, short part);
static void ComputeTERects(WindowPtr w, Rect *destR, Rect *viewR);
static void ShowError(short stringIndex);

/* ---- error helper ---- */

static void ShowError(short stringIndex)
{
    Handle h = GetResource('STR#', kErrStrListID);
    Str255 s;
    if (h == NULL) return;
    GetIndString(s, kErrStrListID, stringIndex);
    if (s[0] == 0) return;
    ParamText(s, "\p", "\p", "\p");
    StopAlert(128, NULL);
}

/* ---- rect computation ---- */

static void ComputeTERects(WindowPtr w, Rect *destR, Rect *viewR)
{
    Rect r = w->portRect;
    r.right  -= kMdScrollWidth;   /* vertical scroll bar on the right */
    r.bottom -= kMdScrollWidth;   /* growbox row at the bottom */
    InsetRect(&r, 4, 4);
    *destR = r;
    *viewR = r;
}

/* ---- scroll-bar action proc ---- */

static pascal void ScrollAction(ControlHandle ctl, short part)
{
    short delta = 0;
    short lineH;
    short newVal;
    short oldVal;
    if (part == 0) return;
    lineH = (**gDoc.te).lineHeight;
    if (lineH < 1) lineH = 12;
    switch (part) {
        case inUpButton:    delta = -lineH; break;
        case inDownButton:  delta =  lineH; break;
        case inPageUp:      delta = -((**gDoc.te).viewRect.bottom - (**gDoc.te).viewRect.top); break;
        case inPageDown:    delta =  ((**gDoc.te).viewRect.bottom - (**gDoc.te).viewRect.top); break;
        default: return;
    }
    oldVal = GetControlValue(ctl);
    newVal = oldVal + delta;
    if (newVal < GetControlMinimum(ctl)) newVal = GetControlMinimum(ctl);
    if (newVal > GetControlMaximum(ctl)) newVal = GetControlMaximum(ctl);
    SetControlValue(ctl, newVal);
    TEScroll(0, oldVal - newVal, gDoc.te);
}

/* ---- lifecycle ---- */

void DocInit(void)
{
    Rect destR, viewR;
    WindowPtr w;
    Rect ctlR;

    w = GetNewWindow(kWindowID, NULL, (WindowPtr)-1L);
    SetPort(w);

    gDoc.window = w;
    gDoc.hasFile = false;
    gDoc.dirty = false;
    gDoc.leKind = kLE_CR;
    gDoc.dirtyLineStart = -1;
    gDoc.dirtyLineEnd = -1;
    gDoc.lastDirtyTick = 0;
    gDoc.fileName[0] = 0;
    gDoc.vRefNum = 0;
    gDoc.selAnchor = 0;
    gDoc.undoText = NULL;
    gDoc.canUndo = false;
    gDoc.inTypingRun = false;

    SetWRefCon(w, (long)&gDoc);

    ComputeTERects(w, &destR, &viewR);
    gDoc.te = TEStyleNew(&destR, &viewR);
    TEAutoView(true, gDoc.te);

    /* Vertical scroll bar along the right edge. */
    ctlR.top    = -1;
    ctlR.left   = w->portRect.right - kMdScrollWidth;
    ctlR.right  = w->portRect.right + 1;
    ctlR.bottom = w->portRect.bottom - 14;
    gDoc.vScroll = NewControl(w, &ctlR, "\p", true, 0, 0, 0,
                              scrollBarProc, 0);

    DocAdjustScrollbar();
    DocUpdateTitle();
}

void DocDispose(void)
{
    if (gDoc.undoText) { DisposeHandle(gDoc.undoText); gDoc.undoText = NULL; }
    if (gDoc.te) { TEDispose(gDoc.te); gDoc.te = NULL; }
    if (gDoc.window) { DisposeWindow(gDoc.window); gDoc.window = NULL; }
}

/* ---- Undo ---- */

/* The Edit menu item ID for Undo — kept in sync with main.c's value. */
#define kUndoMenuID 130
#define kUndoMenuItem 1

static void SetUndoMenuEnabled(Boolean on)
{
    MenuHandle m = GetMenuHandle(kUndoMenuID);
    if (m == NULL) return;
    if (on) EnableItem(m, kUndoMenuItem);
    else    DisableItem(m, kUndoMenuItem);
}

/* Copy the TE's text into a freshly-allocated handle. Returns NULL on
   memory failure. */
static Handle SnapshotText(short *outLen)
{
    short len = (**gDoc.te).teLength;
    Handle h = NewHandle(len);
    CharsHandle ch;
    if (h == NULL) return NULL;
    if (len > 0) {
        HLock(h);
        ch = TEGetText(gDoc.te);
        HLock((Handle)ch);
        BlockMoveData(*ch, *h, len);
        HUnlock((Handle)ch);
        HUnlock(h);
    }
    if (outLen) *outLen = len;
    return h;
}

static void SaveUndoSnapshot(void)
{
    short len;
    Handle h = SnapshotText(&len);
    if (h == NULL) return;
    if (gDoc.undoText) DisposeHandle(gDoc.undoText);
    gDoc.undoText     = h;
    gDoc.undoLen      = len;
    gDoc.undoSelStart = (**gDoc.te).selStart;
    gDoc.undoSelEnd   = (**gDoc.te).selEnd;
    gDoc.undoLE       = gDoc.leKind;
    gDoc.canUndo      = true;
    SetUndoMenuEnabled(true);
}

void DocBeforeAction(void)
{
    SaveUndoSnapshot();
    gDoc.inTypingRun = false;
}

void DocBeforeTyping(void)
{
    if (!gDoc.inTypingRun) {
        SaveUndoSnapshot();
        gDoc.inTypingRun = true;
    }
}

void DocBreakTypingRun(void)
{
    gDoc.inTypingRun = false;
}

void DocClearUndo(void)
{
    if (gDoc.undoText) {
        DisposeHandle(gDoc.undoText);
        gDoc.undoText = NULL;
    }
    gDoc.canUndo = false;
    gDoc.inTypingRun = false;
    SetUndoMenuEnabled(false);
}

void DocUndo(void)
{
    short curLen;
    Handle curText;
    short curSelStart, curSelEnd;
    LineEndKind curLE;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    if (!gDoc.canUndo || gDoc.undoText == NULL) {
        SysBeep(1);
        return;
    }

    /* Snapshot the current state (so a second Undo serves as Redo). */
    curText = SnapshotText(&curLen);
    if (curText == NULL) { SysBeep(1); return; }
    curSelStart = (**gDoc.te).selStart;
    curSelEnd   = (**gDoc.te).selEnd;
    curLE       = gDoc.leKind;

    /* Replace the TE contents with the saved snapshot under a clipped
       port to avoid the redraw flicker. TESetText resets style runs
       so we follow it with a full re-style pass. */
    GetPort(&savedPort);
    SetPort(gDoc.window);
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    HLock(gDoc.undoText);
    TESetText(*gDoc.undoText, gDoc.undoLen, gDoc.te);
    HUnlock(gDoc.undoText);

    TESetSelect(gDoc.undoSelStart, gDoc.undoSelEnd, gDoc.te);
    gDoc.selAnchor = gDoc.undoSelStart;
    gDoc.leKind    = gDoc.undoLE;

    MdRestyleAll(gDoc.te);
    TECalText(gDoc.te);

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    /* The previous-current state becomes the new snapshot (redo). */
    DisposeHandle(gDoc.undoText);
    gDoc.undoText     = curText;
    gDoc.undoLen      = curLen;
    gDoc.undoSelStart = curSelStart;
    gDoc.undoSelEnd   = curSelEnd;
    gDoc.undoLE       = curLE;
    gDoc.canUndo      = true;
    gDoc.inTypingRun  = false;

    gDoc.dirty = true;
    DocUpdateTitle();
    DocAdjustScrollbar();
    InvalRect(&gDoc.window->portRect);
}

/* ---- window title ---- */

void DocUpdateTitle(void)
{
    Str255 title;
    short i;
    const unsigned char *src;
    const unsigned char *fallback = (const unsigned char *)"\puntitled";

    src = gDoc.hasFile ? (const unsigned char *)gDoc.fileName : fallback;

    title[0] = 0;
    if (gDoc.dirty) {
        title[++title[0]] = 0xA5;    /* bullet */
        title[++title[0]] = ' ';
    }
    for (i = 1; i <= src[0]; i++) title[++title[0]] = src[i];

    SetWTitle(gDoc.window, title);
}

/* ---- new / open / save ---- */

static void ClearDocText(void)
{
    /* TESetText destroys style runs on a styled TE — but we're about to
       reload anyway, and we want plain start state. */
    TESetText("", 0, gDoc.te);
    gDoc.dirty = false;
    gDoc.dirtyLineStart = gDoc.dirtyLineEnd = -1;
}

Boolean DocNew(void)
{
    if (!DocPromptSaveIfDirty()) return false;
    ClearDocText();
    gDoc.hasFile = false;
    gDoc.fileName[0] = 0;
    gDoc.vRefNum = 0;
    gDoc.leKind = kLE_CR;
    DocClearUndo();
    DocUpdateTitle();
    DocAdjustScrollbar();
    /* If the window was hidden (after a Close), bring it back. */
    ShowWindow(gDoc.window);
    SelectWindow(gDoc.window);
    InvalRect(&gDoc.window->portRect);
    return true;
}

Boolean DocClose(void)
{
    if (!DocPromptSaveIfDirty()) return false;
    ClearDocText();
    gDoc.hasFile = false;
    gDoc.fileName[0] = 0;
    gDoc.vRefNum = 0;
    gDoc.leKind = kLE_CR;
    DocClearUndo();
    DocUpdateTitle();
    HideWindow(gDoc.window);
    return true;
}

Boolean DocOpen(short vRefNum, ConstStr255Param name)
{
    Handle data = NULL;
    LineEndKind le;
    long folded = 0;
    OSErr err;
    long len;
    short i;

    if (!DocPromptSaveIfDirty()) return false;

    err = FileIOReadDoc(vRefNum, name, &data, &le, &folded);
    if (err == -1) { ShowError(kErrTooBig); return false; }
    if (err != noErr) { ShowError(kErrCantOpen); return false; }

    len = GetHandleSize(data);
    if (len > kMdMaxFileBytes) {
        DisposeHandle(data);
        ShowError(kErrTooBig);
        return false;
    }

    ClearDocText();
    HLock(data);
    TESetText(*data, len, gDoc.te);
    HUnlock(data);
    DisposeHandle(data);

    gDoc.hasFile = true;
    gDoc.vRefNum = vRefNum;
    gDoc.fileName[0] = name[0];
    for (i = 1; i <= name[0]; i++) gDoc.fileName[i] = name[i];
    gDoc.leKind = le;
    gDoc.dirty = false;
    DocClearUndo();

    /* Full reparse on load. */
    MdRestyleAll(gDoc.te);

    /* Park the cursor at the top of the document so the user starts
       reading from line 1, not the very end (which is where TESetText
       leaves the selection by default). */
    TESetSelect(0, 0, gDoc.te);
    gDoc.selAnchor = 0;

    DocUpdateTitle();
    DocAdjustScrollbar();
    /* If the window was hidden, show it. */
    ShowWindow(gDoc.window);
    SelectWindow(gDoc.window);
    InvalRect(&gDoc.window->portRect);

    if (folded > 0) ShowError(kErrFolded);
    return true;
}

Boolean DocSave(void)
{
    OSErr err;
    Handle copy;
    long len;

    if (!gDoc.hasFile) return DocSaveAs();

    len = (**gDoc.te).teLength;

    /* Snapshot the text into a private buffer before doing any file
       ops. Holding the TE chars handle locked through File Manager
       calls is fragile — the heap can't compact around it and the FM
       can wedge. Releasing it first sidesteps the whole class of bug. */
    copy = NewHandle(len);
    if (copy == NULL) {
        ShowError(kErrCantSave);
        return false;
    }
    HLock(copy);
    if (len > 0) {
        CharsHandle ch = TEGetText(gDoc.te);
        HLock((Handle)ch);
        BlockMoveData(*ch, *copy, len);
        HUnlock((Handle)ch);
    }

    err = FileIOWriteDoc(gDoc.vRefNum, gDoc.fileName, *copy, len, gDoc.leKind);

    HUnlock(copy);
    DisposeHandle(copy);

    if (err != noErr) {
        ShowError(kErrCantSave);
        return false;
    }

    FileIOSetTypeAndCreator(gDoc.vRefNum, gDoc.fileName, 'TEXT', 'MDED');
    gDoc.dirty = false;
    DocUpdateTitle();
    return true;
}

Boolean DocSaveAs(void)
{
    SFReply reply;
    Str63 suggestion;
    Point where;
    short i;
    GrafPtr savedPort;

    /* Zero-init the reply — some Standard File implementations behave
       oddly with garbage in unused fields. */
    {
        char *p = (char *)&reply;
        for (i = 0; i < (short)sizeof(SFReply); i++) p[i] = 0;
    }

    /* Default name: current file name if any, else "untitled.md". */
    if (gDoc.hasFile) {
        suggestion[0] = gDoc.fileName[0];
        for (i = 1; i <= gDoc.fileName[0]; i++) suggestion[i] = gDoc.fileName[i];
    } else {
        const unsigned char *def = (const unsigned char *)"\puntitled.md";
        suggestion[0] = def[0];
        for (i = 1; i <= def[0]; i++) suggestion[i] = def[i];
    }

    /* SFPutFile draws its dialog in the current GrafPort — make sure
       that's our window, not some leftover from a prior modal. Also
       reset the cursor to arrow so it doesn't stay as a watch from an
       earlier operation, which can look like a hang. */
    GetPort(&savedPort);
    SetPort(gDoc.window);
    InitCursor();

    where.h = 80;
    where.v = 60;
    SFPutFile(where, "\pSave document as:", suggestion, NULL, &reply);

    SetPort(savedPort);
    YieldOnce();

    if (!reply.good) return false;

    gDoc.vRefNum     = reply.vRefNum;
    gDoc.fileName[0] = reply.fName[0];
    for (i = 1; i <= reply.fName[0]; i++)
        gDoc.fileName[i] = reply.fName[i];
    gDoc.hasFile = true;

    return DocSave();
}

Boolean DocPromptSaveIfDirty(void)
{
    short item;
    if (!gDoc.dirty) return true;
    InitCursor();
    item = StopAlert(kSaveChangesALRT, NULL);
    /* DITL: 1=Save, 2=Don't Save, 3=Cancel */
    switch (item) {
        case 1: return DocSave();
        case 2: return true;
        case 3:
        default: return false;
    }
}

/* ---- window plumbing ---- */

void DocResize(void)
{
    Rect destR, viewR;
    WindowPtr w = gDoc.window;
    if (!w) return;

    EraseRect(&w->portRect);

    /* Move scroll bar. */
    MoveControl(gDoc.vScroll, w->portRect.right - kMdScrollWidth, -1);
    SizeControl(gDoc.vScroll, kMdScrollWidth + 1, w->portRect.bottom - 14);

    ComputeTERects(w, &destR, &viewR);
    (**gDoc.te).destRect = destR;
    (**gDoc.te).viewRect = viewR;
    TECalText(gDoc.te);
    DocAdjustScrollbar();
    InvalRect(&w->portRect);
}

void DocUpdate(void)
{
    WindowPtr w = gDoc.window;
    Rect viewR;
    BeginUpdate(w);
    /* Erase only the chrome strips around the TE (where the scroll bar
       and growbox live), not the text area itself. TEUpdate paints the
       text on a properly-erased background. Erasing the whole portRect
       was producing a visible white flash even though BeginUpdate
       clipped to the dirty region. */
    viewR = (**gDoc.te).viewRect;
    {
        Rect strip;
        /* Strip to the right of the TE (scroll bar column). */
        strip = w->portRect;
        strip.left = viewR.right;
        EraseRect(&strip);
        /* Strip below the TE (growbox row). */
        strip = w->portRect;
        strip.top = viewR.bottom;
        EraseRect(&strip);
    }
    DrawControls(w);
    DrawGrowIcon(w);
    TEUpdate(&w->portRect, gDoc.te);
    EndUpdate(w);
}

void DocActivate(Boolean active)
{
    if (active) {
        TEActivate(gDoc.te);
        ShowControl(gDoc.vScroll);
    } else {
        TEDeactivate(gDoc.te);
        HideControl(gDoc.vScroll);
    }
    DrawGrowIcon(gDoc.window);
}

void DocClick(EventRecord *ev)
{
    WindowPtr w = gDoc.window;
    Point local = ev->where;
    short part;
    ControlHandle ctl;

    GlobalToLocal(&local);

    part = FindControl(local, w, &ctl);
    if (part != 0 && ctl == gDoc.vScroll) {
        if (part == inThumb) {
            short oldVal = GetControlValue(ctl);
            TrackControl(ctl, local, NULL);
            TEScroll(0, oldVal - GetControlValue(ctl), gDoc.te);
        } else {
            TrackControl(ctl, local, NewControlActionProc(ScrollAction));
        }
        return;
    }

    /* Click in TE area. */
    {
        Rect viewR = (**gDoc.te).viewRect;
        if (PtInRect(local, &viewR)) {
            Boolean extend = (ev->modifiers & shiftKey) != 0;
            TEClick(local, extend, gDoc.te);
            /* Shift-click extends from the existing anchor; plain click
               drops a new anchor at the click point. */
            if (!extend) gDoc.selAnchor = (**gDoc.te).selStart;
        }
    }
}

void DocAdjustScrollbar(void)
{
    short totalH;
    short viewH;
    short max;
    short curVal;

    if (!gDoc.te || !gDoc.vScroll) return;

    totalH = TEGetHeight((**gDoc.te).nLines, 0, gDoc.te);
    viewH  = (**gDoc.te).viewRect.bottom - (**gDoc.te).viewRect.top;

    max = totalH - viewH;
    if (max < 0) max = 0;
    SetControlMaximum(gDoc.vScroll, max);

    curVal = (**gDoc.te).viewRect.top - (**gDoc.te).destRect.top;
    if (curVal < 0) curVal = 0;
    if (curVal > max) curVal = max;
    SetControlValue(gDoc.vScroll, curVal);

    HiliteControl(gDoc.vScroll, (max == 0) ? 255 : 0);
}

/* ---- dirty tracking ---- */

void DocMarkDirty(void)
{
    if (!gDoc.dirty) {
        gDoc.dirty = true;
        DocUpdateTitle();
    }
    gDoc.lastDirtyTick = TickCount();
}

void DocMarkLineDirty(short pos)
{
    short lineStart, lineEnd;
    short col;
    short lineLen;
    CharsHandle ch;
    char *text;
    MdLineKind kind;
    Boolean hasEmphasis = false;
    short i;

    MdFindLineBounds(gDoc.te, pos, &lineStart, &lineEnd);

    /* Filter 1 — past the markdown-marker zone? Still need to restyle
       if the line contains inline emphasis chars (`*` or `_`), since
       typing a closing delimiter past col 7 needs to apply bold to
       the run. */
    col = pos - lineStart;
    if (col > 7) {
        short ilen = lineEnd - lineStart;
        CharsHandle hCh = TEGetText(gDoc.te);
        char *t;
        Boolean hasEmph = false;
        short j;
        HLock((Handle)hCh);
        t = *hCh + lineStart;
        for (j = 0; j < ilen; j++) {
            if (t[j] == '*' || t[j] == '_') { hasEmph = true; break; }
        }
        HUnlock((Handle)hCh);
        if (!hasEmph) return;
    }

    /* Filter 2 — would this line already look correct?
       Restyle would be a no-op if:
         - line classifies as plain
         - no inline emphasis chars (`*` or `_`) on the line
         - existing style at lineStart is already plain Geneva 12
       That covers the common case of typing plain text on a plain
       line, which dominates real-world editing. */
    lineLen = lineEnd - lineStart;
    ch = TEGetText(gDoc.te);
    HLock((Handle)ch);
    text = *ch + lineStart;

    kind = MdClassifyLine(text, lineLen);

    if (kind == kLine_Plain) {
        for (i = 0; i < lineLen; i++) {
            if (text[i] == '*' || text[i] == '_') {
                hasEmphasis = true;
                break;
            }
        }
        if (!hasEmphasis) {
            TextStyle current;
            short lh, fa;
            HUnlock((Handle)ch);
            /* Use a position inside the line if it has chars, else the
               start (for empty lines, current style is the typing style). */
            TEGetStyle(lineStart, &current, &lh, &fa, gDoc.te);
            if (current.tsFace == 0 && current.tsSize == 12) {
                return;  /* already plain — no restyle/redraw needed */
            }
            HLock((Handle)ch);  /* re-lock for parity, will unlock below */
        }
    }

    HUnlock((Handle)ch);

    if (gDoc.dirtyLineStart < 0) {
        gDoc.dirtyLineStart = lineStart;
        gDoc.dirtyLineEnd   = lineEnd;
    } else {
        if (lineStart < gDoc.dirtyLineStart) gDoc.dirtyLineStart = lineStart;
        if (lineEnd   > gDoc.dirtyLineEnd)   gDoc.dirtyLineEnd   = lineEnd;
    }
}

/* ---- restyling on idle ---- */

/* ---- Option-Up / Option-Down line move ---- */

/* Shared implementation: swap the line containing the cursor with the
   adjacent line in the given direction. `down`=true moves the current
   line down, false moves it up. Cursor stays at the same column in the
   moved line. */
static void MoveLine(Boolean down)
{
    short pos;
    DocBeforeAction();
    pos = (**gDoc.te).selStart;
    short curStart, curEnd;
    short otherStart, otherEnd;
    short curLen, otherLen;
    short selOffsetInLine;
    short teLen = (**gDoc.te).teLength;
    short rangeStart, rangeEnd, newSel;
    CharsHandle ch;
    char *text;
    Handle buf;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    MdFindLineBounds(gDoc.te, pos, &curStart, &curEnd);

    if (down) {
        if (curEnd >= teLen) return;            /* already last line */
        MdFindLineBounds(gDoc.te, curEnd + 1, &otherStart, &otherEnd);
    } else {
        if (curStart == 0) return;              /* already first line */
        MdFindLineBounds(gDoc.te, curStart - 1, &otherStart, &otherEnd);
    }

    curLen          = curEnd - curStart;
    otherLen        = otherEnd - otherStart;
    selOffsetInLine = pos - curStart;
    if (selOffsetInLine > curLen) selOffsetInLine = curLen;

    buf = NewHandle((long)curLen + 1 + (long)otherLen);
    if (buf == NULL) return;
    HLock(buf);

    ch = TEGetText(gDoc.te);
    HLock((Handle)ch);
    text = *ch;

    if (down) {
        /* Original [curStart..otherEnd] = curr\rother
           Replace with                   = other\rcurr */
        BlockMoveData(text + otherStart, *buf, otherLen);
        (*buf)[otherLen] = '\r';
        BlockMoveData(text + curStart, *buf + otherLen + 1, curLen);
        rangeStart = curStart;
        rangeEnd   = otherEnd;
        newSel     = curStart + otherLen + 1 + selOffsetInLine;
    } else {
        /* Original [otherStart..curEnd] = other\rcurr
           Replace with                   = curr\rother */
        BlockMoveData(text + curStart, *buf, curLen);
        (*buf)[curLen] = '\r';
        BlockMoveData(text + otherStart, *buf + curLen + 1, otherLen);
        rangeStart = otherStart;
        rangeEnd   = curEnd;
        newSel     = otherStart + selOffsetInLine;
    }

    HUnlock((Handle)ch);

    /* Wrap the TEDelete + TEInsert in a clip-suppressed block so the
       intermediate flicker isn't seen. We'll redraw via DocFlushRestyle
       at the end. */
    GetPort(&savedPort);
    SetPort(gDoc.window);
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    TESetSelect(rangeStart, rangeEnd, gDoc.te);
    TEDelete(gDoc.te);
    TEInsert(*buf, curLen + 1 + otherLen, gDoc.te);
    TESetSelect(newSel, newSel, gDoc.te);

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    HUnlock(buf);
    DisposeHandle(buf);

    /* Both lines need restyle + redraw. Bypass DocMarkLineDirty's
       per-line filter and force the range directly, then flush now. */
    gDoc.dirtyLineStart = rangeStart;
    gDoc.dirtyLineEnd   = rangeStart + curLen + 1 + otherLen;
    gDoc.lastDirtyTick  = TickCount() - 1000;  /* fire immediately */
    DocFlushRestyle();
    DocMarkDirty();
    DocAdjustScrollbar();
}

void DocMoveLineUp(void)   { MoveLine(false); }
void DocMoveLineDown(void) { MoveLine(true); }

/* ---- Cursor movement helpers ---- */

short DocOffsetLeft(short pos)
{
    return (pos > 0) ? pos - 1 : 0;
}

short DocOffsetRight(short pos)
{
    short teLen = (**gDoc.te).teLength;
    return (pos < teLen) ? pos + 1 : teLen;
}

short DocOffsetUp(short pos)
{
    short lineStart, lineEnd;
    short prevLineStart, prevLineEnd;
    short col;
    short prevLen;

    MdFindLineBounds(gDoc.te, pos, &lineStart, &lineEnd);
    if (lineStart == 0) return 0;
    col = pos - lineStart;
    MdFindLineBounds(gDoc.te, lineStart - 1, &prevLineStart, &prevLineEnd);
    prevLen = prevLineEnd - prevLineStart;
    return prevLineStart + ((col < prevLen) ? col : prevLen);
}

short DocOffsetDown(short pos)
{
    short lineStart, lineEnd;
    short nextLineStart, nextLineEnd;
    short col;
    short nextLen;
    short teLen = (**gDoc.te).teLength;

    MdFindLineBounds(gDoc.te, pos, &lineStart, &lineEnd);
    if (lineEnd >= teLen) return teLen;
    col = pos - lineStart;
    MdFindLineBounds(gDoc.te, lineEnd + 1, &nextLineStart, &nextLineEnd);
    nextLen = nextLineEnd - nextLineStart;
    return nextLineStart + ((col < nextLen) ? col : nextLen);
}

/* Word-char predicate — mirrors markdown.c's definition. CR is not a
   word char, so word jumps cross line breaks naturally. */
static Boolean IsWordCharDoc(char c)
{
    unsigned char u = (unsigned char)c;
    return (u >= '0' && u <= '9') ||
           (u >= 'A' && u <= 'Z') ||
           (u >= 'a' && u <= 'z') ||
           u >= 0x80;
}

short DocOffsetWordRight(short pos)
{
    short teLen = (**gDoc.te).teLength;
    short lineEnd = DocLineEndOffset(pos);
    CharsHandle ch;
    char *text;

    /* At end of line: jump just past the CR to the start of the next
       line. Press Option-Right again from there to skip into the
       first word. */
    if (pos == lineEnd) {
        return (pos < teLen) ? pos + 1 : teLen;
    }

    ch = TEGetText(gDoc.te);
    HLock((Handle)ch);
    text = *ch;

    /* Skip non-word chars forward, but stop at end of line. */
    while (pos < lineEnd && !IsWordCharDoc(text[pos])) pos++;
    if (pos == lineEnd) {
        HUnlock((Handle)ch);
        return pos;
    }
    /* Then skip the word's chars forward. */
    while (pos < lineEnd && IsWordCharDoc(text[pos])) pos++;

    HUnlock((Handle)ch);
    return pos;
}

short DocOffsetWordLeft(short pos)
{
    short lineStart = DocLineStartOffset(pos);
    CharsHandle ch;
    char *text;

    /* At start of line: jump to the end of the previous line (one
       position before the CR). Press Option-Left again from there to
       skip back over the previous line's last word. */
    if (pos == lineStart) {
        return (pos > 0) ? pos - 1 : 0;
    }

    ch = TEGetText(gDoc.te);
    HLock((Handle)ch);
    text = *ch;

    /* Skip non-word chars backward, stopping at line start. */
    while (pos > lineStart && !IsWordCharDoc(text[pos - 1])) pos--;
    if (pos == lineStart) {
        HUnlock((Handle)ch);
        return pos;
    }
    /* Then skip the word's chars backward to land at the word's start. */
    while (pos > lineStart && IsWordCharDoc(text[pos - 1])) pos--;

    HUnlock((Handle)ch);
    return pos;
}

short DocLineStartOffset(short pos)
{
    short lineStart, lineEnd;
    MdFindLineBounds(gDoc.te, pos, &lineStart, &lineEnd);
    return lineStart;
}

short DocLineEndOffset(short pos)
{
    short lineStart, lineEnd;
    MdFindLineBounds(gDoc.te, pos, &lineStart, &lineEnd);
    return lineEnd;
}

void DocMoveCursorTo(short newOffset, Boolean extending)
{
    short teLen = (**gDoc.te).teLength;
    if (newOffset < 0)     newOffset = 0;
    if (newOffset > teLen) newOffset = teLen;

    if (extending) {
        short anchor = gDoc.selAnchor;
        short lo = (anchor < newOffset) ? anchor : newOffset;
        short hi = (anchor > newOffset) ? anchor : newOffset;
        TESetSelect(lo, hi, gDoc.te);
    } else {
        TESetSelect(newOffset, newOffset, gDoc.te);
        gDoc.selAnchor = newOffset;
    }
    TESelView(gDoc.te);
    DocAdjustScrollbar();
}

/* ---- I-beam cursor over the text area ---- */

void DocAdjustCursor(void)
{
    Point mouse;
    GrafPtr savedPort;
    WindowPtr w = gDoc.window;
    Rect viewR;
    CursHandle iBeam;

    if (w == NULL || w != FrontWindow()) {
        InitCursor();
        return;
    }
    GetPort(&savedPort);
    SetPort(w);
    GetMouse(&mouse);
    viewR = (**gDoc.te).viewRect;
    if (PtInRect(mouse, &viewR)) {
        iBeam = GetCursor(iBeamCursor);
        if (iBeam) SetCursor(*iBeam);
        else       InitCursor();
    } else {
        InitCursor();
    }
    SetPort(savedPort);
}

/* ---- Indent / Outdent ---- */

/* Indent or outdent every line that the current selection touches. With
   a collapsed cursor, just the cursor's line. The selection ends shift
   to track the inserted/removed leading characters. Walks lines from
   the bottom up so earlier line positions stay valid as we mutate. */
static void IndentRange(Boolean indent)
{
    short selStart, selEnd;
    DocBeforeAction();
    selStart = (**gDoc.te).selStart;
    selEnd   = (**gDoc.te).selEnd;
    Boolean isSelection = (selStart != selEnd);
    short firstLs, firstLe, lastLs, lastLe;
    short endProbe;
    short firstLineStart, lastLineEnd;
    short pos;
    short totalDelta = 0;
    short newStart, newEnd;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    MdFindLineBounds(gDoc.te, selStart, &firstLs, &firstLe);

    /* If selEnd is exactly on a line start, that line isn't "in" the
       selection content-wise — back up one byte so we don't include it. */
    endProbe = isSelection ? ((selEnd > selStart) ? selEnd - 1 : selEnd) : selStart;
    MdFindLineBounds(gDoc.te, endProbe, &lastLs, &lastLe);

    firstLineStart = firstLs;
    lastLineEnd    = lastLe;

    /* Suppress drawing during the mutation loop — repeated TESetSelect
       + TEInsert/TEDelete would otherwise flash the selection across
       each iteration. */
    GetPort(&savedPort);
    SetPort(gDoc.window);
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    pos = lastLs;
    for (;;) {
        if (indent) {
            char tab = '\t';
            TESetSelect(pos, pos, gDoc.te);
            TEInsert(&tab, 1, gDoc.te);
            totalDelta++;
        } else {
            CharsHandle ch = TEGetText(gDoc.te);
            char c0;
            HLock((Handle)ch);
            c0 = (*ch)[pos];
            HUnlock((Handle)ch);
            if (c0 == '\t' || c0 == ' ') {
                TESetSelect(pos, pos + 1, gDoc.te);
                TEDelete(gDoc.te);
                totalDelta--;
            }
        }

        if (pos == firstLineStart) break;
        {
            short prevLs, prevLe;
            MdFindLineBounds(gDoc.te, pos - 1, &prevLs, &prevLe);
            pos = prevLs;
        }
    }

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    if (totalDelta == 0) {
        SysBeep(1);
        return;
    }

    /* Restore selection. For a multi-line selection we snap the start
       to the beginning of the first affected line and shift the end by
       the cumulative char delta — the selection grows/shrinks with the
       indentation. For a collapsed cursor we just shift it. */
    if (isSelection) {
        newStart = firstLineStart;
        newEnd   = selEnd + totalDelta;
    } else if (indent) {
        newStart = selStart + 1;
        newEnd   = newStart;
    } else {
        newStart = (selStart > firstLineStart) ? selStart - 1 : firstLineStart;
        newEnd   = newStart;
    }
    if (newStart < 0)               newStart = 0;
    if (newEnd   < firstLineStart)  newEnd   = firstLineStart;

    TESetSelect(newStart, newEnd, gDoc.te);
    gDoc.selAnchor = newStart;

    DocMarkDirty();
    gDoc.dirtyLineStart = firstLineStart;
    gDoc.dirtyLineEnd   = lastLineEnd + totalDelta;
    gDoc.lastDirtyTick  = TickCount() - 1000;
    DocFlushRestyle();
    DocAdjustScrollbar();
}

void DocIndentLine(void)  { IndentRange(true); }
void DocOutdentLine(void) { IndentRange(false); }

/* Find the index of the line containing the given byte offset. */
static short LineIndexForOffset(short offset)
{
    short n = (**gDoc.te).nLines;
    short i;
    if (n <= 0) return 0;
    for (i = 0; i < n; i++) {
        if ((**gDoc.te).lineStarts[i + 1] > offset) return i;
    }
    return n - 1;
}

/* Restyle the dirty region. Suppresses drawing during the style mutation
   (empty clip), then composes the affected lines into an offscreen and
   blits back. Only the changed lines (down to the bottom of viewRect to
   absorb line-height shifts) are blitted, not the whole viewRect. */
void DocFlushRestyle(void)
{
    short start, end, lineStart, lineEnd;
    short startLineIdx;
    CharsHandle ch;
    char *text;
    GrafPtr savedPort;
    RgnHandle savedClip;
    RgnHandle emptyRgn;
    Rect drawRect;
    Rect viewR;
    short topY;

    if (gDoc.dirtyLineStart < 0) return;

    {
        short teLen = (**gDoc.te).teLength;
        if (gDoc.dirtyLineStart > teLen) gDoc.dirtyLineStart = teLen;
        if (gDoc.dirtyLineEnd   > teLen) gDoc.dirtyLineEnd   = teLen;
    }

    start = gDoc.dirtyLineStart;
    end   = gDoc.dirtyLineEnd;
    gDoc.dirtyLineStart = gDoc.dirtyLineEnd = -1;

    /* Snapshot line index BEFORE styling — styling may grow/shrink line
       heights but the line at this offset stays at the same line index. */
    startLineIdx = LineIndexForOffset(start);

    GetPort(&savedPort);
    SetPort(gDoc.window);

    /* Suppress on-screen drawing during the style mutation. */
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    ch = TEGetText(gDoc.te);
    HLock((Handle)ch);
    text = *ch;

    lineStart = start;
    while (lineStart > 0 && text[lineStart - 1] != '\r') lineStart--;

    while (lineStart <= end) {
        short teLen = (**gDoc.te).teLength;
        lineEnd = lineStart;
        while (lineEnd < teLen && text[lineEnd] != '\r') lineEnd++;

        HUnlock((Handle)ch);
        MdRestyleLine(gDoc.te, lineStart, lineEnd);
        HLock((Handle)ch);
        text = *ch;

        if (lineEnd >= teLen) break;
        lineStart = lineEnd + 1;
    }

    HUnlock((Handle)ch);

    TECalText(gDoc.te);

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);

    /* Blit from the top of the first changed line down to the bottom of
       the viewRect. (Line-height changes shift later content, so we
       have to repaint everything below.) */
    viewR = (**gDoc.te).viewRect;

    if (startLineIdx == 0) {
        /* Line 0 special case: TEGetHeight(0,0,te) returns 0, so the
           computed topY equals destRect.top. If destRect.top is below
           viewR.top (e.g., TextEdit has added top padding for the
           first line's ascent), the strip between viewR.top and
           destRect.top can hold stale pixels. Start the redraw at
           viewR.top instead to guarantee that strip is cleared. */
        drawRect.top = viewR.top;
    } else {
        topY = (**gDoc.te).destRect.top + TEGetHeight(startLineIdx, 0, gDoc.te);
        drawRect.top = topY;
        if (drawRect.top < viewR.top)    drawRect.top = viewR.top;
        if (drawRect.top > viewR.bottom) drawRect.top = viewR.bottom;
    }

    drawRect.left   = viewR.left;
    drawRect.right  = viewR.right;
    drawRect.bottom = viewR.bottom;

    if (drawRect.bottom > drawRect.top) {
        EraseRect(&drawRect);
        TEUpdate(&drawRect, gDoc.te);
    }

    SetPort(savedPort);
}
