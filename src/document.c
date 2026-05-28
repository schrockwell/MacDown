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

#include "markdown.h"
#include "browser.h"
#include "file_io.h"

#define kWindowID 128
#define kSaveChangesALRT 129
#define kErrStrListID 128
#define kErrCantOpen 1
#define kErrTooBig 2
#define kErrFolded 3
#define kErrCantSave 4
#define kErrCantCreate 5
#define kErrTooManyDocs 6

#define kMaxOpenDocs 8

#define kUndoMenuID 130
#define kUndoMenuItem 1
#define kFileMenuID 129
#define kFileMenuClose    3
#define kFileMenuCloseAll 4
#define kFileMenuSave     5
#define kFileMenuSaveAs   6
#define kWindowsMenuID 132
#define kWindowsMenuStaticItems 3 /* Next, Browser, separator */

DocState *gDocs = NULL;

/* ---- forward decls ---- */
static pascal void ScrollAction(ControlHandle ctl, short part);
static void ComputeTERects(WindowPtr w, Rect *destR, Rect *viewR);
static void ShowError(short stringIndex);
static void SetUndoMenuEnabled(Boolean on);
void SyncFileMenuEnables(void);
static void YieldOnce(void);
static void ClearDocText(DocState *doc);
static DocState *FindEmptyUntitledDoc(void);

/* ---- helpers ---- */

/* Double-buffered redraw. The flicker during live restyle comes from
   the EraseRect+TEUpdate sequence painting two distinct frames to
   the screen. Drawing into an offscreen GrafPort and CopyBits'ing
   the result eliminates the gap -- the screen jumps from "old text"
   straight to "new text" with no flash of empty white in between. */
typedef struct {
    GrafPort port;
    BitMap   bm;
    Ptr      bits;
    Boolean  active;
} OffscreenBuf;

static OffscreenBuf gOffscreen;

/* Begin offscreen drawing into a buffer matching `r` (window coords).
   On success: current port is the offscreen, already cleared to white
   and clipped to `r`. Caller should draw, then call OffscreenCopyAndEnd.
   On failure: returns false; current port is unchanged. */
static Boolean OffscreenBegin(const Rect *r)
{
    GrafPtr savedPort;
    short width  = r->right  - r->left;
    short height = r->bottom - r->top;
    short rowBytes;
    long  pixSize;

    if (gOffscreen.active) return false;
    if (width <= 0 || height <= 0) return false;

    /* rowBytes must be a multiple of 2; round up to 16-bit. */
    rowBytes = ((width + 15) >> 4) << 1;
    pixSize  = (long)rowBytes * (long)height;
    if (pixSize <= 0) return false;

    gOffscreen.bits = NewPtrClear(pixSize);
    if (gOffscreen.bits == NULL) return false;

    gOffscreen.bm.baseAddr = gOffscreen.bits;
    gOffscreen.bm.rowBytes = rowBytes;
    gOffscreen.bm.bounds   = *r;

    GetPort(&savedPort);
    OpenPort(&gOffscreen.port);
    SetPortBits(&gOffscreen.bm);
    gOffscreen.port.portRect = *r;
    ClipRect(r);
    EraseRect(r);
    /* Leave offscreen port as the current port for the caller's draws. */
    (void)savedPort;
    gOffscreen.active = true;
    return true;
}

/* Copy the offscreen buffer to `dest` at the original rect, then
   tear down the offscreen port. */
static void OffscreenCopyAndEnd(GrafPtr dest, const Rect *r)
{
    if (!gOffscreen.active) return;
    SetPort(dest);
    CopyBits(&gOffscreen.bm, &dest->portBits, r, r, srcCopy, NULL);
    ClosePort(&gOffscreen.port);
    DisposePtr(gOffscreen.bits);
    gOffscreen.bits   = NULL;
    gOffscreen.active = false;
}

static void ShowError(short stringIndex)
{
    Handle h = GetResource('STR#', kErrStrListID);
    Str255 s;
    if (h == NULL)
        return;
    GetIndString(s, kErrStrListID, stringIndex);
    if (s[0] == 0)
        return;
    ParamText(s, "\p", "\p", "\p");
    StopAlert(128, NULL);
}

static void YieldOnce(void)
{
    EventRecord ev;
    EventAvail(0, &ev);
}

static void SetUndoMenuEnabled(Boolean on)
{
    MenuHandle m = GetMenuHandle(kUndoMenuID);
    if (m == NULL)
        return;
    if (on)
        EnableItem(m, kUndoMenuItem);
    else
        DisableItem(m, kUndoMenuItem);
}

/* Close / Save / Save As require an active document; grey them out
   when there are none open so Cmd-W / Cmd-S also no-op. */
void SyncFileMenuEnables(void)
{
    MenuHandle m = GetMenuHandle(kFileMenuID);
    Boolean haveDoc;
    if (m == NULL)
        return;
    haveDoc = (DocActive() != NULL);
    if (haveDoc)
    {
        EnableItem(m, kFileMenuClose);
        EnableItem(m, kFileMenuSave);
        EnableItem(m, kFileMenuSaveAs);
    }
    else
    {
        DisableItem(m, kFileMenuClose);
        DisableItem(m, kFileMenuSave);
        DisableItem(m, kFileMenuSaveAs);
    }
    if (gDocs != NULL)
        EnableItem(m, kFileMenuCloseAll);
    else
        DisableItem(m, kFileMenuCloseAll);
}

static void ComputeTERects(WindowPtr w, Rect *destR, Rect *viewR)
{
    Rect r = w->portRect;
    r.right -= kMdScrollWidth;
    r.bottom -= kMdScrollWidth;
    InsetRect(&r, 4, 4);
    *destR = r;
    *viewR = r;
}

/* Cached uniform line height (definition lives further down). */
static short LineHeight(TEHandle te);

/* ---- Active-doc tracking via scroll-action UPP context ----

   The scroll-bar action proc has a fixed signature (ctl, part) and no
   user data — we recover the doc from the control's owning window. */
static pascal void ScrollAction(ControlHandle ctl, short part)
{
    DocState *doc;
    WindowPtr w;
    short delta = 0;
    short lineH;
    short newVal;
    short oldVal;
    if (part == 0)
        return;
    w = (**ctl).contrlOwner;
    doc = DocFromWindow(w);
    if (doc == NULL)
        return;
    /* (**te).lineHeight is -1 for a TEStyleNew (variable height marker),
       so we'd previously fall back to a wrong literal. The true uniform
       line height is cached in LineHeight(). */
    lineH = LineHeight(doc->te);
    switch (part)
    {
    case inUpButton:
        delta = -lineH;
        break;
    case inDownButton:
        delta = lineH;
        break;
    case inPageUp:
        delta = -((**doc->te).viewRect.bottom - (**doc->te).viewRect.top);
        break;
    case inPageDown:
        delta = ((**doc->te).viewRect.bottom - (**doc->te).viewRect.top);
        break;
    default:
        return;
    }
    oldVal = GetControlValue(ctl);
    newVal = oldVal + delta;
    if (newVal < GetControlMinimum(ctl))
        newVal = GetControlMinimum(ctl);
    if (newVal > GetControlMaximum(ctl))
        newVal = GetControlMaximum(ctl);
    SetControlValue(ctl, newVal);
    TEScroll(0, oldVal - newVal, doc->te);
}

/* ---- lookup ---- */

DocState *DocFromWindow(WindowPtr w)
{
    DocState *d;
    if (w == NULL)
        return NULL;
    for (d = gDocs; d != NULL; d = d->next)
    {
        if (d->window == w)
            return d;
    }
    return NULL;
}

DocState *DocActive(void)
{
    return DocFromWindow(FrontWindow());
}

/* ---- lifecycle ---- */

void DocAppInit(void)
{
    gDocs = NULL;
    SyncFileMenuEnables();
}

/* Create a new untitled document with its own window. Returns NULL if
   memory or window creation fails. */
DocState *DocNew(void)
{
    DocState *doc;
    WindowPtr w;
    Rect destR, viewR;
    Rect ctlR;
    short count;

    /* Count open docs and refuse if at the limit. */
    count = 0;
    for (doc = gDocs; doc != NULL; doc = doc->next) count++;
    if (count >= kMaxOpenDocs) {
        ShowError(kErrTooManyDocs);
        return NULL;
    }

    doc = (DocState *)NewPtrClear(sizeof(DocState));
    if (doc == NULL)
        return NULL;
    doc->leKind = kLE_CR;
    doc->dirtyLineStart = -1;
    doc->dirtyLineEnd = -1;

    /* Cascade: each new window steps 20px down and right from a fixed
       origin just below the menu bar. A static counter wraps when the
       next position would push the window off-screen, giving a clean
       diagonal pattern that doesn't depend on where the user has
       dragged existing windows. */
    {
        static short sCascade = 0;
        short baseH = 20;
        short baseV = GetMBarHeight() + 20;
        short step  = 20;
        short posH, posV;

        posH = baseH + sCascade * step;
        posV = baseV + sCascade * step;
        sCascade = (sCascade + 1) % 5;

        w = GetNewWindow(kWindowID, NULL, (WindowPtr)-1L);
        if (w == NULL)
        {
            DisposePtr((Ptr)doc);
            return NULL;
        }
        MoveWindow(w, posH, posV, false);
    }

    SetPort(w);
    doc->window = w;
    SetWRefCon(w, (long)doc);

    ComputeTERects(w, &destR, &viewR);
    doc->te = TEStyleNew(&destR, &viewR);
    TEAutoView(true, doc->te);

    ctlR.top = -1;
    ctlR.left = w->portRect.right - kMdScrollWidth;
    ctlR.right = w->portRect.right + 1;
    ctlR.bottom = w->portRect.bottom - 14;
    doc->vScroll = NewControl(w, &ctlR, "\p", true, 0, 0, 0,
                              scrollBarProc, 0);

    /* Push onto list head. */
    doc->next = gDocs;
    gDocs = doc;

    DocAdjustScrollbar(doc);
    DocUpdateTitle(doc);

    ShowWindow(w);
    SelectWindow(w);

    /* Force a full initial paint right now. The updateEvt path is
       unreliable for the front-most just-created window (the events
       seem to coalesce or get clipped against a not-yet-settled
       visRgn), so draw the chrome explicitly and only rely on
       updateEvt for the text area. */
    SetPort(w);
    {
        Rect r = w->portRect;
        EraseRect(&r);
    }
    DrawControls(w);
    DrawGrowIcon(w);
    TEUpdate(&w->portRect, doc->te);
    /* And still invalidate so any later re-draw round is clean. */
    InvalRect(&w->portRect);
    RebuildWindowsMenu();
    SyncFileMenuEnables();
    return doc;
}

/* If there's a single untitled, empty, clean doc already open, return
   it so Open can reuse it instead of stacking a window. */
static DocState *FindEmptyUntitledDoc(void)
{
    DocState *d;
    for (d = gDocs; d != NULL; d = d->next)
    {
        if (!d->hasFile && !d->dirty && (**d->te).teLength == 0)
            return d;
    }
    return NULL;
}

Boolean DocClose(DocState *doc)
{
    DocState **link;
    if (doc == NULL)
        return true;
    if (!DocPromptSaveIfDirty(doc))
        return false;

    /* Remove from list. */
    for (link = &gDocs; *link != NULL; link = &(*link)->next)
    {
        if (*link == doc)
        {
            *link = doc->next;
            break;
        }
    }

    DocClearUndo(doc);
    if (doc->hasFile)
        FileIOReleaseWD(doc->vRefNum);
    if (doc->te)
        TEDispose(doc->te);
    if (doc->window)
        DisposeWindow(doc->window);
    DisposePtr((Ptr)doc);
    RebuildWindowsMenu();
    SyncFileMenuEnables();
    return true;
}

Boolean DocCloseAll(void)
{
    while (gDocs != NULL)
    {
        DocState *next = gDocs->next;
        if (!DocClose(gDocs))
            return false;
        (void)next; /* gDocs already advanced via DocClose's list-removal */
    }
    return true;
}

/* ---- title ---- */

void DocUpdateTitle(DocState *doc)
{
    Str255 title;
    short i;
    const unsigned char *src;
    const unsigned char *fallback = (const unsigned char *)"\puntitled";

    src = doc->hasFile ? (const unsigned char *)doc->fileName : fallback;

    title[0] = 0;
    if (doc->dirty)
    {
        title[++title[0]] = 0xA5;
        title[++title[0]] = ' ';
    }
    for (i = 1; i <= src[0]; i++)
        title[++title[0]] = src[i];

    SetWTitle(doc->window, title);
    RebuildWindowsMenu();
}

/* ---- new/open/save/close ---- */

static void ClearDocText(DocState *doc)
{
    TESetText("", 0, doc->te);
    doc->dirty = false;
    doc->dirtyLineStart = doc->dirtyLineEnd = -1;
}

static Boolean DocOpenWithOwnedWD(short ownedWD, ConstStr255Param name)
{
    Handle data = NULL;
    LineEndKind le;
    long folded = 0;
    OSErr err;
    long len;
    short i;
    DocState *doc;
    Boolean reusedEmpty;
    CursHandle watch;

    /* Reading + restyling a long file can take seconds on real hardware.
       Show the watch cursor for the duration so the app doesn't look
       hung. Restored via InitCursor on every exit path. */
    watch = GetCursor(watchCursor);
    if (watch) SetCursor(*watch);

    err = FileIOReadDoc(ownedWD, name, &data, &le, &folded);
    if (err == -1)
    {
        FileIOReleaseWD(ownedWD);
        InitCursor();
        ShowError(kErrTooBig);
        return false;
    }
    if (err != noErr)
    {
        FileIOReleaseWD(ownedWD);
        InitCursor();
        ShowError(kErrCantOpen);
        return false;
    }

    len = GetHandleSize(data);
    if (len > kMdMaxFileBytes)
    {
        DisposeHandle(data);
        FileIOReleaseWD(ownedWD);
        InitCursor();
        ShowError(kErrTooBig);
        return false;
    }

    doc = FindEmptyUntitledDoc();
    reusedEmpty = (doc != NULL);
    if (!reusedEmpty)
    {
        doc = DocNew();
        if (doc == NULL)
        {
            DisposeHandle(data);
            FileIOReleaseWD(ownedWD);
            InitCursor();
            return false;
        }
    }

    /* If reused, release the old doc's wdRefNum before stomping it. */
    if (reusedEmpty && doc->hasFile)
        FileIOReleaseWD(doc->vRefNum);

    ClearDocText(doc);
    HLock(data);
    TESetText(*data, len, doc->te);
    HUnlock(data);
    DisposeHandle(data);

    doc->hasFile = true;
    doc->vRefNum = ownedWD;
    doc->fileName[0] = name[0];
    for (i = 1; i <= name[0]; i++)
        doc->fileName[i] = name[i];
    doc->leKind = le;
    doc->dirty = false;
    DocClearUndo(doc);

    MdRestyleAll(doc->te);
    TESetSelect(0, 0, doc->te);
    doc->selAnchor = 0;

    DocUpdateTitle(doc);
    DocAdjustScrollbar(doc);
    ShowWindow(doc->window);
    SelectWindow(doc->window);
    InvalRect(&doc->window->portRect);

    InitCursor();

    if (folded > 0)
        ShowError(kErrFolded);
    return true;
}

/* Take any folder ref (a wdRefNum from Standard File / AppFile, or
   even a plain volume ref) and own it before opening the document. */
/* Find an open doc that already maps to the same (real volume,
   dirID, name) -- so re-opening the same file just raises the
   existing window instead of creating a duplicate. Case-insensitive
   filename compare so callers coming from different code paths
   (HFS is case-preserving but case-insensitive) all match. */
DocState *DocFindOpenFile(short vRefNum, long dirID, ConstStr255Param name)
{
    DocState *d;
    for (d = gDocs; d != NULL; d = d->next) {
        short dVRef;
        long  dDirID;
        short i;
        Boolean equal;
        if (!d->hasFile) continue;
        if (FileIOResolveWD(d->vRefNum, &dVRef, &dDirID) != noErr) continue;
        if (dVRef != vRefNum || dDirID != dirID) continue;
        if (d->fileName[0] != name[0]) continue;
        equal = true;
        for (i = 1; i <= name[0]; i++) {
            unsigned char a = (unsigned char)d->fileName[i];
            unsigned char b = (unsigned char)name[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { equal = false; break; }
        }
        if (equal) return d;
    }
    return NULL;
}

Boolean DocOpen(short anyWDRefNum, ConstStr255Param name)
{
    short ownedWD;
    short vRef;
    long  dirID;

    /* If the file is already open in another window, just raise that
       window -- don't create a duplicate. */
    if (FileIOResolveWD(anyWDRefNum, &vRef, &dirID) == noErr) {
        DocState *existing = DocFindOpenFile(vRef, dirID, name);
        if (existing) { SelectWindow(existing->window); return true; }
    }

    if (FileIOOwnWD(anyWDRefNum, &ownedWD) != noErr)
    {
        ShowError(kErrCantOpen);
        return false;
    }
    return DocOpenWithOwnedWD(ownedWD, name);
}

/* FSSpec callers (Apple Event 'odoc') already have a real (vRefNum,
   parID) pair — open our own wdRefNum directly from those. */
Boolean DocOpenFromDir(short vRefNum, long dirID, ConstStr255Param name)
{
    short ownedWD;

    {
        DocState *existing = DocFindOpenFile(vRefNum, dirID, name);
        if (existing) { SelectWindow(existing->window); return true; }
    }

    if (FileIOOwnWDFromDir(vRefNum, dirID, &ownedWD) != noErr)
    {
        ShowError(kErrCantOpen);
        return false;
    }
    return DocOpenWithOwnedWD(ownedWD, name);
}

Boolean DocSave(DocState *doc)
{
    OSErr err;
    Handle copy;
    long len;

    if (doc == NULL)
        return false;
    if (!doc->hasFile)
        return DocSaveAs(doc);

    len = (**doc->te).teLength;

    copy = NewHandle(len);
    if (copy == NULL)
    {
        ShowError(kErrCantSave);
        return false;
    }
    HLock(copy);
    if (len > 0)
    {
        CharsHandle ch = TEGetText(doc->te);
        HLock((Handle)ch);
        BlockMoveData(*ch, *copy, len);
        HUnlock((Handle)ch);
    }

    err = FileIOWriteDoc(doc->vRefNum, doc->fileName, *copy, len, doc->leKind);

    HUnlock(copy);
    DisposeHandle(copy);

    if (err != noErr)
    {
        ShowError(kErrCantSave);
        return false;
    }

    FileIOSetTypeAndCreator(doc->vRefNum, doc->fileName, 'TEXT', 'MDED');
    doc->dirty = false;
    /* Save is a checkpoint — next keystroke begins a new typing burst
       so undo doesn't quietly reach back across the save. */
    doc->inTypingRun = false;
    DocUpdateTitle(doc);
    return true;
}

Boolean DocSaveAs(DocState *doc)
{
    SFReply reply;
    Str63 suggestion;
    Point where;
    short i;
    GrafPtr savedPort;

    if (doc == NULL)
        return false;

    {
        char *p = (char *)&reply;
        for (i = 0; i < (short)sizeof(SFReply); i++)
            p[i] = 0;
    }

    if (doc->hasFile)
    {
        suggestion[0] = doc->fileName[0];
        for (i = 1; i <= doc->fileName[0]; i++)
            suggestion[i] = doc->fileName[i];
    }
    else
    {
        const unsigned char *def = (const unsigned char *)"\puntitled.md";
        suggestion[0] = def[0];
        for (i = 1; i <= def[0]; i++)
            suggestion[i] = def[i];
    }

    GetPort(&savedPort);
    SetPort(doc->window);
    InitCursor();

    /* Default the Save dialog to the browser's folder if it's visible,
       so the user can drop new files alongside whatever they're
       browsing. SFPutFile reads the starting location from the LM
       globals SFSaveDisk (negated vRefNum) and CurDirStore (dirID). */
    {
        short bvRef;
        long  bDirID;
        if (BrowserCurrentFolder(&bvRef, &bDirID)) {
            LMSetSFSaveDisk(-bvRef);
            LMSetCurDirStore(bDirID);
        }
    }

    where.h = 80;
    where.v = 60;
    SFPutFile(where, "\pSave document as:", suggestion, NULL, &reply);

    SetPort(savedPort);
    YieldOnce();

    if (!reply.good)
        return false;

    /* Own a wdRefNum for the chosen folder so the slot can't be
       recycled out from under us between this save and the next. */
    {
        short ownedWD;
        OSErr err = FileIOOwnWD(reply.vRefNum, &ownedWD);
        if (err != noErr)
        {
            ShowError(kErrCantSave);
            return false;
        }
        if (doc->hasFile)
            FileIOReleaseWD(doc->vRefNum);
        doc->vRefNum = ownedWD;
    }

    doc->fileName[0] = reply.fName[0];
    for (i = 1; i <= reply.fName[0]; i++)
        doc->fileName[i] = reply.fName[i];
    doc->hasFile = true;

    return DocSave(doc);
}

Boolean DocPromptSaveIfDirty(DocState *doc)
{
    short item;
    if (doc == NULL)
        return true;
    if (!doc->dirty)
        return true;
    InitCursor();
    SelectWindow(doc->window); /* so the alert appears in front of THIS doc */
    item = StopAlert(kSaveChangesALRT, NULL);
    switch (item)
    {
    case 1:
        return DocSave(doc);
    case 2:
        return true;
    case 3:
    default:
        return false;
    }
}

/* ---- window plumbing ---- */

void DocResize(DocState *doc)
{
    Rect destR, viewR;
    WindowPtr w = doc->window;
    GrafPtr savedPort;
    if (!w)
        return;

    GetPort(&savedPort);
    SetPort(w);

    EraseRect(&w->portRect);
    MoveControl(doc->vScroll, w->portRect.right - kMdScrollWidth, -1);
    SizeControl(doc->vScroll, kMdScrollWidth + 1, w->portRect.bottom - 14);

    ComputeTERects(w, &destR, &viewR);
    (**doc->te).destRect = destR;
    (**doc->te).viewRect = viewR;
    TECalText(doc->te);
    DocAdjustScrollbar(doc);
    InvalRect(&w->portRect);

    SetPort(savedPort);
}

void DocUpdate(DocState *doc)
{
    WindowPtr w = doc->window;
    Rect viewR;
    GrafPtr savedPort;

    GetPort(&savedPort);
    SetPort(w);

    BeginUpdate(w);
    viewR = (**doc->te).viewRect;
    {
        Rect strip;
        strip = w->portRect;
        strip.left = viewR.right;
        EraseRect(&strip);
        strip = w->portRect;
        strip.top = viewR.bottom;
        EraseRect(&strip);
    }
    /* Erase the view rect before TEUpdate so any stale glyphs (left over
       from an undo or other large content change) are cleared. The
       BeginUpdate clip keeps the cost proportional to the dirty region. */
    EraseRect(&viewR);
    DrawControls(w);
    DrawGrowIcon(w);
    TEUpdate(&w->portRect, doc->te);
    EndUpdate(w);

    SetPort(savedPort);
}

void DocActivate(DocState *doc, Boolean active)
{
    GrafPtr savedPort;
    GetPort(&savedPort);
    SetPort(doc->window);

    if (active)
    {
        TEActivate(doc->te);
        ShowControl(doc->vScroll);
        /* The Undo menu is global but the undo stack is per-doc; sync
           the menu to the now-frontmost doc. */
        SetUndoMenuEnabled(doc->canUndo);
    }
    else
    {
        TEDeactivate(doc->te);
        HideControl(doc->vScroll);
    }
    /* Switching windows always ends the current typing burst, so the
       next keystroke after coming back starts a fresh undo snapshot. */
    doc->inTypingRun = false;
    DrawGrowIcon(doc->window);
    RebuildWindowsMenu();
    SyncFileMenuEnables();

    SetPort(savedPort);
}

void DocClick(DocState *doc, EventRecord *ev)
{
    WindowPtr w = doc->window;
    Point local = ev->where;
    short part;
    ControlHandle ctl;

    SetPort(w);
    GlobalToLocal(&local);

    part = FindControl(local, w, &ctl);
    if (part != 0 && ctl == doc->vScroll)
    {
        if (part == inThumb)
        {
            short oldVal = GetControlValue(ctl);
            TrackControl(ctl, local, NULL);
            TEScroll(0, oldVal - GetControlValue(ctl), doc->te);
        }
        else
        {
            TrackControl(ctl, local, NewControlActionProc(ScrollAction));
        }
        return;
    }

    {
        Rect viewR = (**doc->te).viewRect;
        if (PtInRect(local, &viewR))
        {
            Boolean extend = (ev->modifiers & shiftKey) != 0;
            TEClick(local, extend, doc->te);
            if (!extend)
                doc->selAnchor = (**doc->te).selStart;
            /* Repositioning the caret ends the current typing burst —
               otherwise typing after a click extends the previous burst
               and Cmd-Z reaches back too far. */
            doc->inTypingRun = false;
        }
    }
}

void DocAdjustScrollbar(DocState *doc)
{
    short nLines, totalH, viewH, max, curVal;

    if (!doc->te || !doc->vScroll)
        return;

    /* Total height was O(nLines) via TEGetHeight on every keystroke,
       making typing in a long doc sluggish. Since every style we apply
       is size 12 (see MdRestyleLine), line height is uniform and total
       height is just nLines * lineHeight -- O(1). */
    nLines = (**doc->te).nLines;
    totalH = (short)((long)nLines * LineHeight(doc->te));
    viewH = (**doc->te).viewRect.bottom - (**doc->te).viewRect.top;

    max = totalH - viewH;
    if (max < 0)
        max = 0;
    SetControlMaximum(doc->vScroll, max);

    curVal = (**doc->te).viewRect.top - (**doc->te).destRect.top;
    if (curVal < 0)
        curVal = 0;
    if (curVal > max)
        curVal = max;
    SetControlValue(doc->vScroll, curVal);

    HiliteControl(doc->vScroll, (max == 0) ? 255 : 0);
}

/* ---- Windows menu ---- */

/* Rebuild the "Windows" menu after the static items (Next / Previous /
   separator). One menu item per open document, in the order they appear
   in gDocs (newest first). Check-marks the front window. */
void RebuildWindowsMenu(void)
{
    MenuHandle m = GetMenuHandle(kWindowsMenuID);
    DocState *d;
    short itemIdx;
    short total;
    WindowPtr front;

    if (m == NULL)
        return;

    /* Strip dynamic items. */
    total = CountMItems(m);
    while (total > kWindowsMenuStaticItems)
    {
        DeleteMenuItem(m, total);
        total--;
    }

    /* No docs: leave the menu with just the static items. */
    if (gDocs == NULL)
        return;

    front = FrontWindow();
    itemIdx = kWindowsMenuStaticItems;
    for (d = gDocs; d != NULL; d = d->next)
    {
        Str255 itemText;
        Str255 title;
        short j;

        GetWTitle(d->window, title);

        /* AppendMenu treats metacharacters like '/', ';', '(', '-' as
           syntax. Build the item text with AppendMenu("\px") then
           SetMenuItemText to bypass parsing. */
        AppendMenu(m, "\px");
        itemIdx++;

        itemText[0] = title[0];
        for (j = 1; j <= title[0]; j++)
            itemText[j] = title[j];
        SetMenuItemText(m, itemIdx, itemText);

        SetItemMark(m, itemIdx, (d->window == front) ? (short)checkMark : (short)noMark);
    }
}

void DocSelectFromMenu(short menuItem)
{
    DocState *d = gDocs;
    short idx = 1;
    while (d != NULL && idx < menuItem)
    {
        d = d->next;
        idx++;
    }
    if (d == NULL)
        return;
    SelectWindow(d->window);
}

void DocCycleWindow(void)
{
    WindowPtr front;
    WindowPtr w;
    WindowPtr target = NULL;

    if (gDocs == NULL || gDocs->next == NULL)
        return; /* 0 or 1 docs */

    front = FrontWindow();
    if (DocFromWindow(front) == NULL)
        return;

    /* Send front to the back; the next MacDown window comes forward. */
    SendBehind(front, NULL);
    w = FrontWindow();
    while (w != NULL && DocFromWindow(w) == NULL)
    {
        w = (WindowPtr)((WindowPeek)w)->nextWindow;
    }
    target = w;

    if (target != NULL && target != front)
        SelectWindow(target);
    RebuildWindowsMenu();
}

/* ---- dirty tracking ---- */

void DocMarkDirty(DocState *doc)
{
    if (!doc->dirty)
    {
        doc->dirty = true;
        DocUpdateTitle(doc);
    }
    doc->lastDirtyTick = TickCount();
}

void DocMarkLineDirty(DocState *doc, short pos)
{
    short lineStart, lineEnd, col, lineLen;
    CharsHandle ch;
    char *text;
    MdLineKind kind;
    Boolean hasEmphasis = false;
    short i;

    MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);

    col = pos - lineStart;
    if (col > 7)
    {
        short ilen = lineEnd - lineStart;
        CharsHandle hCh = TEGetText(doc->te);
        char *t;
        Boolean hasEmph = false;
        short j;
        HLock((Handle)hCh);
        t = *hCh + lineStart;
        for (j = 0; j < ilen; j++)
        {
            if (t[j] == '*' || t[j] == '_')
            {
                hasEmph = true;
                break;
            }
        }
        HUnlock((Handle)hCh);
        if (!hasEmph)
            return;
    }

    lineLen = lineEnd - lineStart;
    ch = TEGetText(doc->te);
    HLock((Handle)ch);
    text = *ch + lineStart;

    kind = MdClassifyLine(text, lineLen);

    if (kind == kLine_Plain)
    {
        for (i = 0; i < lineLen; i++)
        {
            if (text[i] == '*' || text[i] == '_')
            {
                hasEmphasis = true;
                break;
            }
        }
        if (!hasEmphasis)
        {
            TextStyle current;
            short lh, fa;
            HUnlock((Handle)ch);
            TEGetStyle(lineStart, &current, &lh, &fa, doc->te);
            if (current.tsFace == 0 && current.tsSize == 12)
                return;
            HLock((Handle)ch);
        }
    }

    HUnlock((Handle)ch);

    if (doc->dirtyLineStart < 0)
    {
        doc->dirtyLineStart = lineStart;
        doc->dirtyLineEnd = lineEnd;
    }
    else
    {
        if (lineStart < doc->dirtyLineStart)
            doc->dirtyLineStart = lineStart;
        if (lineEnd > doc->dirtyLineEnd)
            doc->dirtyLineEnd = lineEnd;
    }
}

/* ---- restyling on idle ---- */

/* Cached line height (uniform across all docs because every style we
   apply is size 12). Set on first use; never invalidated. */
static short sCachedLineHeight = 0;

static short LineHeight(TEHandle te)
{
    if (sCachedLineHeight == 0)
        sCachedLineHeight = TEGetHeight(1, 0, te);
    return sCachedLineHeight;
}

static short LineIndexForOffset(DocState *doc, short offset)
{
    short n = (**doc->te).nLines;
    short i;
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++)
    {
        if ((**doc->te).lineStarts[i + 1] > offset)
            return i;
    }
    return n - 1;
}

void DocFlushRestyle(DocState *doc)
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

    if (doc->dirtyLineStart < 0)
        return;

    {
        short teLen = (**doc->te).teLength;
        if (doc->dirtyLineStart > teLen)
            doc->dirtyLineStart = teLen;
        if (doc->dirtyLineEnd > teLen)
            doc->dirtyLineEnd = teLen;
    }

    /* Renumber any ordered list that overlaps the dirty range. Lists
       whose numbers already match the expected sequence are a no-op;
       the call only writes when a delete or insert disturbed them.
       This is the "delete" half of smart-renumber -- the insert half
       is triggered explicitly from HandleReturnKey. Done before the
       restyle pass so the restyled lines see the correct text.

       Always restore the TE selection after the call, even when the
       totals cancel to zero: individual TEDelete/TEInsert pairs leave
       the caret parked on the last-edited digit run otherwise. */
    {
        short selStart = (**doc->te).selStart;
        short selEnd   = (**doc->te).selEnd;
        short origStart = selStart, origEnd = selEnd;
        short anchor    = doc->selAnchor;
        Boolean anchorEqStart = (anchor == origStart);
        Boolean anchorEqEnd   = (anchor == origEnd);
        long  delta;

        delta = MdRenumberOrderedList(doc->te, doc->dirtyLineStart,
                                      &selStart, &selEnd);
        if (delta != 0) {
            short teLen = (**doc->te).teLength;
            doc->dirtyLineEnd += (short)delta;
            if (doc->dirtyLineEnd > teLen)   doc->dirtyLineEnd = teLen;
            if (doc->dirtyLineEnd < doc->dirtyLineStart)
                doc->dirtyLineEnd = doc->dirtyLineStart;
        }
        TESetSelect(selStart, selEnd, doc->te);
        if      (anchorEqStart) doc->selAnchor = selStart;
        else if (anchorEqEnd)   doc->selAnchor = selEnd;
        else                    doc->selAnchor = selStart;
    }

    start = doc->dirtyLineStart;
    end = doc->dirtyLineEnd;
    doc->dirtyLineStart = doc->dirtyLineEnd = -1;

    startLineIdx = LineIndexForOffset(doc, start);

    GetPort(&savedPort);
    SetPort(doc->window);

    savedClip = NewRgn();
    emptyRgn = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    ch = TEGetText(doc->te);
    HLock((Handle)ch);
    text = *ch;

    lineStart = start;
    while (lineStart > 0 && text[lineStart - 1] != '\r')
        lineStart--;

    while (lineStart <= end)
    {
        short teLen = (**doc->te).teLength;
        lineEnd = lineStart;
        while (lineEnd < teLen && text[lineEnd] != '\r')
            lineEnd++;

        HUnlock((Handle)ch);
        MdRestyleLine(doc->te, lineStart, lineEnd);
        HLock((Handle)ch);
        text = *ch;

        if (lineEnd >= teLen)
            break;
        lineStart = lineEnd + 1;
    }

    HUnlock((Handle)ch);

    /* No TECalText: the restyler only changes face/size, and every
       size it applies is 12, so line heights never shift. */

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);

    /* If the restyle pass touched no styles (typical case for typing
       inside a plain paragraph), there's nothing to repaint -- TEKey
       already drew the new glyph into the window. Skip the offscreen
       allocation, TEUpdate, and CopyBits entirely. */
    if (MdConsumeStyleChanges() == 0) {
        SetPort(savedPort);
        return;
    }

    viewR = (**doc->te).viewRect;

    {
        short lh = LineHeight(doc->te);
        short endLineIdx = LineIndexForOffset(doc, end);
        short destTop = (**doc->te).destRect.top;
        short bottomY;

        topY = destTop + (short)((long)startLineIdx * lh);
        bottomY = destTop + (short)((long)(endLineIdx + 1) * lh);

        drawRect.top = topY;
        drawRect.bottom = bottomY;

        if (drawRect.top < viewR.top) drawRect.top = viewR.top;
        if (drawRect.top > viewR.bottom) drawRect.top = viewR.bottom;
        if (drawRect.bottom > viewR.bottom) drawRect.bottom = viewR.bottom;
        if (drawRect.bottom < drawRect.top) drawRect.bottom = drawRect.top;
    }

    drawRect.left = viewR.left;
    drawRect.right = viewR.right;

    if (drawRect.bottom > drawRect.top)
    {
        if (OffscreenBegin(&drawRect)) {
            /* TE saves the GrafPort it was created in (TERec.inPort)
               and draws directly into THAT port -- ignoring the
               current port. Retarget inPort to the offscreen for the
               update, then restore so future events still hit the
               window. */
            GrafPtr origInPort = (**doc->te).inPort;
            (**doc->te).inPort = &gOffscreen.port;
            TEUpdate(&drawRect, doc->te);
            (**doc->te).inPort = origInPort;
            OffscreenCopyAndEnd(doc->window, &drawRect);
        } else {
            /* No memory for offscreen -- fall back to direct draw. */
            EraseRect(&drawRect);
            TEUpdate(&drawRect, doc->te);
        }
    }

    SetPort(savedPort);
}

/* ---- Option-Up / Option-Down line move ---- */

static void MoveLine(DocState *doc, Boolean down)
{
    short pos;
    short curStart, curEnd, otherStart, otherEnd;
    short curLen, otherLen, selOffsetInLine;
    short teLen = (**doc->te).teLength;
    short rangeStart, rangeEnd, newSel;
    CharsHandle ch;
    char *text;
    Handle buf;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    DocBeforeAction(doc);
    pos = (**doc->te).selStart;

    MdFindLineBounds(doc->te, pos, &curStart, &curEnd);

    if (down)
    {
        if (curEnd >= teLen)
            return;
        MdFindLineBounds(doc->te, curEnd + 1, &otherStart, &otherEnd);
    }
    else
    {
        if (curStart == 0)
            return;
        MdFindLineBounds(doc->te, curStart - 1, &otherStart, &otherEnd);
    }

    curLen = curEnd - curStart;
    otherLen = otherEnd - otherStart;
    selOffsetInLine = pos - curStart;
    if (selOffsetInLine > curLen)
        selOffsetInLine = curLen;

    buf = NewHandle((long)curLen + 1 + (long)otherLen);
    if (buf == NULL)
        return;
    HLock(buf);

    ch = TEGetText(doc->te);
    HLock((Handle)ch);
    text = *ch;

    if (down)
    {
        BlockMoveData(text + otherStart, *buf, otherLen);
        (*buf)[otherLen] = '\r';
        BlockMoveData(text + curStart, *buf + otherLen + 1, curLen);
        rangeStart = curStart;
        rangeEnd = otherEnd;
        newSel = curStart + otherLen + 1 + selOffsetInLine;
    }
    else
    {
        BlockMoveData(text + curStart, *buf, curLen);
        (*buf)[curLen] = '\r';
        BlockMoveData(text + otherStart, *buf + curLen + 1, otherLen);
        rangeStart = otherStart;
        rangeEnd = curEnd;
        newSel = otherStart + selOffsetInLine;
    }

    HUnlock((Handle)ch);

    GetPort(&savedPort);
    SetPort(doc->window);
    savedClip = NewRgn();
    emptyRgn = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    TESetSelect(rangeStart, rangeEnd, doc->te);
    TEDelete(doc->te);
    TEInsert(*buf, curLen + 1 + otherLen, doc->te);
    TESetSelect(newSel, newSel, doc->te);

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    HUnlock(buf);
    DisposeHandle(buf);

    doc->dirtyLineStart = rangeStart;
    doc->dirtyLineEnd = rangeStart + curLen + 1 + otherLen;
    doc->lastDirtyTick = TickCount() - 1000;
    DocFlushRestyle(doc);
    DocMarkDirty(doc);
    DocAdjustScrollbar(doc);
}

void DocMoveLineUp(DocState *doc) { MoveLine(doc, false); }
void DocMoveLineDown(DocState *doc) { MoveLine(doc, true); }

/* Insert a copy of the current line below it. Cursor moves to the
   duplicate at the same column. */
void DocDuplicateLine(DocState *doc)
{
    short pos, lineStart, lineEnd, lineLen, col, newPos;
    Handle buf;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    DocBeforeAction(doc);
    pos = (**doc->te).selStart;
    MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
    lineLen = lineEnd - lineStart;
    col = pos - lineStart;

    /* Buffer holds "\r" followed by the line's text. */
    buf = NewHandle((long)lineLen + 1);
    if (buf == NULL)
        return;
    HLock(buf);
    (*buf)[0] = '\r';
    if (lineLen > 0)
    {
        CharsHandle ch = TEGetText(doc->te);
        HLock((Handle)ch);
        BlockMoveData(*ch + lineStart, *buf + 1, lineLen);
        HUnlock((Handle)ch);
    }

    GetPort(&savedPort);
    SetPort(doc->window);
    savedClip = NewRgn();
    emptyRgn = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    TESetSelect(lineEnd, lineEnd, doc->te);
    TEInsert(*buf, lineLen + 1, doc->te);

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    HUnlock(buf);
    DisposeHandle(buf);

    newPos = lineEnd + 1 + col;
    TESetSelect(newPos, newPos, doc->te);
    doc->selAnchor = newPos;

    DocMarkDirty(doc);
    doc->dirtyLineStart = lineStart;
    doc->dirtyLineEnd = lineEnd + 1 + lineLen;
    doc->lastDirtyTick = TickCount() - 1000;
    DocFlushRestyle(doc);
    DocAdjustScrollbar(doc);
}

/* ---- Heading toggle ---- */

void DocToggleHeading(DocState *doc, short level)
{
    short selStart, selEnd, lineStart, lineEnd;
    short markerStart, markerEnd, currentLevel;
    short oldPrefixLen, newPrefixLen;
    short targetLevel;
    short i;
    char newPrefix[8];
    CharsHandle ch;
    char *text;
    short newSelStart, newSelEnd;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    if (level < 0 || level > 6) return;
    DocBeforeAction(doc);

    selStart = (**doc->te).selStart;
    selEnd   = (**doc->te).selEnd;
    MdFindLineBounds(doc->te, selStart, &lineStart, &lineEnd);

    ch = TEGetText(doc->te);
    HLock((Handle)ch);
    text = *ch;

    /* Detect current heading level on this line. */
    i = lineStart;
    while (i < lineEnd && i < lineStart + 3 && text[i] == ' ') i++;
    markerStart = i;
    currentLevel = 0;
    {
        short hashes = 0;
        while (i < lineEnd && text[i] == '#' && hashes < 7) { hashes++; i++; }
        if (hashes >= 1 && hashes <= 6 && i < lineEnd && text[i] == ' ') {
            currentLevel = hashes;
            markerEnd = markerStart + hashes + 1;   /* include the space */
        } else {
            markerEnd = markerStart;
        }
    }
    HUnlock((Handle)ch);

    /* Toggle: same level requested as already on the line -> strip. */
    targetLevel = (level == currentLevel) ? 0 : level;

    newPrefixLen = 0;
    if (targetLevel > 0) {
        short k;
        for (k = 0; k < targetLevel; k++) newPrefix[newPrefixLen++] = '#';
        newPrefix[newPrefixLen++] = ' ';
    }
    oldPrefixLen = markerEnd - markerStart;

    /* If the marker is unchanged, nothing to do. */
    if (newPrefixLen == oldPrefixLen) {
        Boolean same = true;
        if (newPrefixLen > 0) {
            short k;
            HLock((Handle)ch);
            for (k = 0; k < newPrefixLen; k++) {
                if ((*ch)[markerStart + k] != newPrefix[k]) { same = false; break; }
            }
            HUnlock((Handle)ch);
        }
        if (same) return;
    }

    /* Adjust the selection to follow the replacement. */
    {
        short delta = newPrefixLen - oldPrefixLen;
        newSelStart = selStart;
        newSelEnd   = selEnd;
        if (selStart >= markerEnd) newSelStart = selStart + delta;
        else if (selStart > markerStart) newSelStart = markerStart + newPrefixLen;
        if (selEnd >= markerEnd) newSelEnd = selEnd + delta;
        else if (selEnd > markerStart) newSelEnd = markerStart + newPrefixLen;
        if (newSelStart < markerStart) newSelStart = markerStart;
        if (newSelEnd < newSelStart)   newSelEnd   = newSelStart;
    }

    GetPort(&savedPort);
    SetPort(doc->window);
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    TESetSelect(markerStart, markerEnd, doc->te);
    if (oldPrefixLen > 0) TEDelete(doc->te);
    if (newPrefixLen > 0) TEInsert(newPrefix, newPrefixLen, doc->te);

    TESetSelect(newSelStart, newSelEnd, doc->te);
    doc->selAnchor = newSelStart;

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    DocMarkDirty(doc);
    doc->dirtyLineStart = lineStart;
    doc->dirtyLineEnd   = lineEnd + (newPrefixLen - oldPrefixLen);
    doc->lastDirtyTick  = TickCount() - 1000;
    DocFlushRestyle(doc);
    DocAdjustScrollbar(doc);
}

/* ---- Symmetric pair wrap / toggle (Cmd-B, Cmd-I, Cmd-`) ---- */

/* True if the n bytes starting at `start` are all `ch`. */
static Boolean BytesAreAll(TEHandle te, short start, short count, char ch)
{
    CharsHandle h;
    short i;
    Boolean ok = true;
    if (count <= 0) return false;
    if (start < 0 || start + count > (**te).teLength) return false;
    h = TEGetText(te);
    HLock((Handle)h);
    for (i = 0; i < count; i++) {
        if ((*h)[start + i] != ch) { ok = false; break; }
    }
    HUnlock((Handle)h);
    return ok;
}

/* True if position p is exactly `ch` and neither its immediate
   neighbor is `ch` -- used to distinguish a lone `*` (italic) from a
   `**` run (bold) when n==1. */
static Boolean ByteIs(TEHandle te, short p, char ch)
{
    CharsHandle h;
    Boolean ok;
    if (p < 0 || p >= (**te).teLength) return false;
    h = TEGetText(te);
    HLock((Handle)h);
    ok = ((*h)[p] == ch);
    HUnlock((Handle)h);
    return ok;
}

void DocWrapPair(DocState *doc, char ch, short n)
{
    short selStart = (**doc->te).selStart;
    short selEnd   = (**doc->te).selEnd;
    short i;
    char  pair[8];
    Boolean wrappedInside  = false;   /* selection includes markers */
    Boolean wrappedOutside = false;   /* markers sit just outside selection */
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;
    short cursor;
    short lineStart, lineEnd;

    if (n < 1 || n > 4) return;
    for (i = 0; i < n; i++) pair[i] = ch;

    /* Toggle detection -- only when a non-empty selection exists. */
    if (selStart != selEnd) {
        /* Inside: [**bold**] selection, strip the inner markers. */
        if (selEnd - selStart >= 2 * n
            && BytesAreAll(doc->te, selStart,         n, ch)
            && BytesAreAll(doc->te, selEnd - n,       n, ch))
        {
            wrappedInside = true;
            /* For n=1 italic, make sure we didn't match a `**` run. */
            if (n == 1) {
                if (ByteIs(doc->te, selStart + 1, ch)) wrappedInside = false;
                if (ByteIs(doc->te, selEnd   - 2, ch)) wrappedInside = false;
            }
        }
        /* Outside: **|bold|** -- selection holds the content, markers
           sit right next to it. Strip them and keep content selected. */
        if (!wrappedInside
            && BytesAreAll(doc->te, selStart - n,     n, ch)
            && BytesAreAll(doc->te, selEnd,           n, ch))
        {
            wrappedOutside = true;
            if (n == 1) {
                if (ByteIs(doc->te, selStart - 2, ch)) wrappedOutside = false;
                if (ByteIs(doc->te, selEnd   + 1, ch)) wrappedOutside = false;
            }
        }
    }

    DocBeforeAction(doc);

    GetPort(&savedPort);
    SetPort(doc->window);
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    if (wrappedInside) {
        /* Delete the trailing markers first so positions for the head
           markers don't shift. */
        TESetSelect(selEnd - n, selEnd, doc->te);
        TEDelete(doc->te);
        TESetSelect(selStart, selStart + n, doc->te);
        TEDelete(doc->te);
        TESetSelect(selStart, selEnd - 2 * n, doc->te);
        doc->selAnchor = selStart;
        cursor = selStart;
    } else if (wrappedOutside) {
        TESetSelect(selEnd, selEnd + n, doc->te);
        TEDelete(doc->te);
        TESetSelect(selStart - n, selStart, doc->te);
        TEDelete(doc->te);
        TESetSelect(selStart - n, selEnd - n, doc->te);
        doc->selAnchor = selStart - n;
        cursor = selStart - n;
    } else if (selStart == selEnd) {
        /* Empty selection: insert "<pair><pair>" and park between. */
        TESetSelect(selStart, selStart, doc->te);
        TEInsert(pair, n, doc->te);
        TEInsert(pair, n, doc->te);
        TESetSelect(selStart + n, selStart + n, doc->te);
        doc->selAnchor = selStart + n;
        cursor = selStart + n;
    } else {
        /* Plain wrap -- markers added on both sides, content reselected. */
        TESetSelect(selEnd, selEnd, doc->te);
        TEInsert(pair, n, doc->te);
        TESetSelect(selStart, selStart, doc->te);
        TEInsert(pair, n, doc->te);
        TESetSelect(selStart + n, selEnd + n, doc->te);
        doc->selAnchor = selStart + n;
        cursor = selStart + n;
    }

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    DocMarkDirty(doc);
    MdFindLineBounds(doc->te, cursor, &lineStart, &lineEnd);
    doc->dirtyLineStart = lineStart;
    doc->dirtyLineEnd   = lineEnd;
    doc->lastDirtyTick  = TickCount() - 1000;
    DocFlushRestyle(doc);
    DocAdjustScrollbar(doc);
}

/* ---- Cursor movement helpers ---- */

static Boolean IsWordCharDoc(char c)
{
    unsigned char u = (unsigned char)c;
    return (u >= '0' && u <= '9') ||
           (u >= 'A' && u <= 'Z') ||
           (u >= 'a' && u <= 'z') ||
           u >= 0x80;
}

short DocOffsetLeft(DocState *doc, short pos)
{
    (void)doc;
    return (pos > 0) ? pos - 1 : 0;
}

short DocOffsetRight(DocState *doc, short pos)
{
    short teLen = (**doc->te).teLength;
    return (pos < teLen) ? pos + 1 : teLen;
}

short DocOffsetUp(DocState *doc, short pos)
{
    short lineStart, lineEnd, prevLineStart, prevLineEnd, col, prevLen;
    MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
    if (lineStart == 0)
        return 0;
    col = pos - lineStart;
    MdFindLineBounds(doc->te, lineStart - 1, &prevLineStart, &prevLineEnd);
    prevLen = prevLineEnd - prevLineStart;
    return prevLineStart + ((col < prevLen) ? col : prevLen);
}

short DocOffsetDown(DocState *doc, short pos)
{
    short lineStart, lineEnd, nextLineStart, nextLineEnd, col, nextLen;
    short teLen = (**doc->te).teLength;
    MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
    if (lineEnd >= teLen)
        return teLen;
    col = pos - lineStart;
    MdFindLineBounds(doc->te, lineEnd + 1, &nextLineStart, &nextLineEnd);
    nextLen = nextLineEnd - nextLineStart;
    return nextLineStart + ((col < nextLen) ? col : nextLen);
}

short DocOffsetWordRight(DocState *doc, short pos)
{
    short teLen = (**doc->te).teLength;
    short lineEnd = DocLineEndOffset(doc, pos);
    CharsHandle ch;
    char *text;

    if (pos == lineEnd)
        return (pos < teLen) ? pos + 1 : teLen;

    ch = TEGetText(doc->te);
    HLock((Handle)ch);
    text = *ch;

    while (pos < lineEnd && !IsWordCharDoc(text[pos]))
        pos++;
    if (pos == lineEnd)
    {
        HUnlock((Handle)ch);
        return pos;
    }
    while (pos < lineEnd && IsWordCharDoc(text[pos]))
        pos++;

    HUnlock((Handle)ch);
    return pos;
}

short DocOffsetWordLeft(DocState *doc, short pos)
{
    short lineStart = DocLineStartOffset(doc, pos);
    CharsHandle ch;
    char *text;

    if (pos == lineStart)
        return (pos > 0) ? pos - 1 : 0;

    ch = TEGetText(doc->te);
    HLock((Handle)ch);
    text = *ch;

    while (pos > lineStart && !IsWordCharDoc(text[pos - 1]))
        pos--;
    if (pos == lineStart)
    {
        HUnlock((Handle)ch);
        return pos;
    }
    while (pos > lineStart && IsWordCharDoc(text[pos - 1]))
        pos--;

    HUnlock((Handle)ch);
    return pos;
}

short DocLineStartOffset(DocState *doc, short pos)
{
    short lineStart, lineEnd;
    MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
    return lineStart;
}

short DocLineEndOffset(DocState *doc, short pos)
{
    short lineStart, lineEnd;
    MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
    return lineEnd;
}

void DocMoveCursorTo(DocState *doc, short newOffset, Boolean extending)
{
    short teLen = (**doc->te).teLength;
    if (newOffset < 0)
        newOffset = 0;
    if (newOffset > teLen)
        newOffset = teLen;

    if (extending)
    {
        short anchor = doc->selAnchor;
        short lo = (anchor < newOffset) ? anchor : newOffset;
        short hi = (anchor > newOffset) ? anchor : newOffset;
        TESetSelect(lo, hi, doc->te);
    }
    else
    {
        TESetSelect(newOffset, newOffset, doc->te);
        doc->selAnchor = newOffset;
    }
    TESelView(doc->te);
    DocAdjustScrollbar(doc);
}

/* ---- Indent / Outdent ---- */

static void IndentRange(DocState *doc, Boolean indent)
{
    short selStart, selEnd;
    Boolean isSelection;
    short firstLs, firstLe, lastLs, lastLe;
    short endProbe, firstLineStart, lastLineEnd;
    short pos;
    short totalDelta = 0;
    short newStart, newEnd;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    DocBeforeAction(doc);
    selStart = (**doc->te).selStart;
    selEnd = (**doc->te).selEnd;
    isSelection = (selStart != selEnd);

    MdFindLineBounds(doc->te, selStart, &firstLs, &firstLe);
    endProbe = isSelection ? ((selEnd > selStart) ? selEnd - 1 : selEnd) : selStart;
    MdFindLineBounds(doc->te, endProbe, &lastLs, &lastLe);

    firstLineStart = firstLs;
    lastLineEnd = lastLe;

    GetPort(&savedPort);
    SetPort(doc->window);
    savedClip = NewRgn();
    emptyRgn = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    pos = lastLs;
    for (;;)
    {
        if (indent)
        {
            char tab = '\t';
            TESetSelect(pos, pos, doc->te);
            TEInsert(&tab, 1, doc->te);
            totalDelta++;
        }
        else
        {
            CharsHandle ch = TEGetText(doc->te);
            char c0;
            HLock((Handle)ch);
            c0 = (*ch)[pos];
            HUnlock((Handle)ch);
            if (c0 == '\t' || c0 == ' ')
            {
                TESetSelect(pos, pos + 1, doc->te);
                TEDelete(doc->te);
                totalDelta--;
            }
        }

        if (pos == firstLineStart)
            break;
        {
            short prevLs, prevLe;
            MdFindLineBounds(doc->te, pos - 1, &prevLs, &prevLe);
            pos = prevLs;
        }
    }

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    if (totalDelta == 0)
    {
        SysBeep(1);
        return;
    }

    if (isSelection)
    {
        newStart = firstLineStart;
        newEnd = selEnd + totalDelta;
    }
    else if (indent)
    {
        newStart = selStart + 1;
        newEnd = newStart;
    }
    else
    {
        newStart = (selStart > firstLineStart) ? selStart - 1 : firstLineStart;
        newEnd = newStart;
    }
    if (newStart < 0)
        newStart = 0;
    if (newEnd < firstLineStart)
        newEnd = firstLineStart;

    TESetSelect(newStart, newEnd, doc->te);
    doc->selAnchor = newStart;

    DocMarkDirty(doc);
    doc->dirtyLineStart = firstLineStart;
    doc->dirtyLineEnd = lastLineEnd + totalDelta;
    doc->lastDirtyTick = TickCount() - 1000;
    DocFlushRestyle(doc);
    DocAdjustScrollbar(doc);
}

void DocIndentLine(DocState *doc) { IndentRange(doc, true); }
void DocOutdentLine(DocState *doc) { IndentRange(doc, false); }

/* ---- Toggle blockquote ---- */

void DocToggleBlockquote(DocState *doc)
{
    short selStart, selEnd;
    Boolean isSelection;
    short firstLs, firstLe, lastLs, lastLe;
    short firstLineStart, lastLineEnd;
    short endProbe;
    short pos;
    short totalDelta = 0;
    short newStart, newEnd;
    Boolean addQuote;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    DocBeforeAction(doc);
    selStart = (**doc->te).selStart;
    selEnd   = (**doc->te).selEnd;
    isSelection = (selStart != selEnd);

    MdFindLineBounds(doc->te, selStart, &firstLs, &firstLe);
    endProbe = isSelection ? ((selEnd > selStart) ? selEnd - 1 : selEnd) : selStart;
    MdFindLineBounds(doc->te, endProbe, &lastLs, &lastLe);

    firstLineStart = firstLs;
    lastLineEnd    = lastLe;

    /* Direction follows the FIRST line: blockquote -> strip from all,
       non-quote -> add to all. Makes the result uniform across a
       mixed selection. */
    {
        CharsHandle ch = TEGetText(doc->te);
        Boolean firstIsQuote = false;
        short i;
        HLock((Handle)ch);
        i = firstLs;
        while (i < firstLe && i < firstLs + 3 && (*ch)[i] == ' ') i++;
        if (i < firstLe && (*ch)[i] == '>') firstIsQuote = true;
        HUnlock((Handle)ch);
        addQuote = !firstIsQuote;
    }

    GetPort(&savedPort);
    SetPort(doc->window);
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    /* Walk lines back to front so earlier line positions stay valid. */
    pos = lastLs;
    for (;;) {
        if (addQuote) {
            TESetSelect(pos, pos, doc->te);
            TEInsert("> ", 2, doc->te);
            totalDelta += 2;
        } else {
            CharsHandle ch = TEGetText(doc->te);
            short i, removeStart, removeLen;
            short ls, le;
            MdFindLineBounds(doc->te, pos, &ls, &le);
            HLock((Handle)ch);
            i = pos;
            while (i < le && i < pos + 3 && (*ch)[i] == ' ') i++;
            removeStart = i;
            removeLen = 0;
            if (i < le && (*ch)[i] == '>') {
                removeLen = 1;
                i++;
                if (i < le && (*ch)[i] == ' ') removeLen++;
            }
            HUnlock((Handle)ch);
            if (removeLen > 0) {
                TESetSelect(removeStart, removeStart + removeLen, doc->te);
                TEDelete(doc->te);
                totalDelta -= removeLen;
            }
        }

        if (pos == firstLineStart) break;
        {
            short prevLs, prevLe;
            MdFindLineBounds(doc->te, pos - 1, &prevLs, &prevLe);
            pos = prevLs;
        }
    }

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);

    if (isSelection) {
        newStart = firstLineStart;
        newEnd   = selEnd + totalDelta;
    } else {
        /* No selection: shift the single caret position by however
           much we added/removed on its line. */
        newStart = selStart + (addQuote ? 2 : totalDelta);
        if (newStart < firstLineStart) newStart = firstLineStart;
        newEnd = newStart;
    }
    if (newStart < 0)        newStart = 0;
    if (newEnd < newStart)   newEnd   = newStart;

    TESetSelect(newStart, newEnd, doc->te);
    doc->selAnchor = newStart;

    DocMarkDirty(doc);
    doc->dirtyLineStart = firstLineStart;
    doc->dirtyLineEnd   = lastLineEnd + totalDelta;
    doc->lastDirtyTick  = TickCount() - 1000;
    DocFlushRestyle(doc);
    DocAdjustScrollbar(doc);
}

/* ---- I-beam cursor ---- */

void DocAdjustCursor(void)
{
    DocState *doc = DocActive();
    Point mouse;
    GrafPtr savedPort;
    WindowPtr w;
    Rect viewR;
    CursHandle iBeam;

    if (doc == NULL || doc->window == NULL)
    {
        InitCursor();
        return;
    }
    w = doc->window;
    GetPort(&savedPort);
    SetPort(w);
    GetMouse(&mouse);
    viewR = (**doc->te).viewRect;
    if (PtInRect(mouse, &viewR))
    {
        iBeam = GetCursor(iBeamCursor);
        if (iBeam)
            SetCursor(*iBeam);
        else
            InitCursor();
    }
    else
    {
        InitCursor();
    }
    SetPort(savedPort);
}

/* ---- Undo (single-level) ----

   One snapshot per doc, of the state *before* the most recent action
   or typing burst. Cmd-Z restores it; the prior current state then
   becomes the new snapshot so a second Cmd-Z toggles back (acts as
   redo). Memory cost is bounded at one TE-worth (~32K max) per doc. */

static Handle SnapshotText(DocState *doc, short *outLen)
{
    short len = (**doc->te).teLength;
    Handle h = NewHandle(len);
    CharsHandle ch;
    if (h == NULL)
        return NULL;
    if (len > 0)
    {
        HLock(h);
        ch = TEGetText(doc->te);
        HLock((Handle)ch);
        BlockMoveData(*ch, *h, len);
        HUnlock((Handle)ch);
        HUnlock(h);
    }
    if (outLen)
        *outLen = len;
    return h;
}

static void SaveUndoSnapshot(DocState *doc)
{
    short len;
    Handle h = SnapshotText(doc, &len);
    if (h == NULL)
        return;
    if (doc->undoText)
        DisposeHandle(doc->undoText);
    doc->undoText = h;
    doc->undoLen = len;
    doc->undoSelStart = (**doc->te).selStart;
    doc->undoSelEnd = (**doc->te).selEnd;
    doc->undoLE = doc->leKind;
    doc->canUndo = true;
    SetUndoMenuEnabled(true);
}

void DocBeforeAction(DocState *doc)
{
    SaveUndoSnapshot(doc);
    doc->inTypingRun = false;
}
void DocBeforeTyping(DocState *doc)
{
    if (!doc->inTypingRun)
    {
        SaveUndoSnapshot(doc);
        doc->inTypingRun = true;
    }
}
void DocBreakTypingRun(DocState *doc) { doc->inTypingRun = false; }

void DocClearUndo(DocState *doc)
{
    if (doc->undoText)
    {
        DisposeHandle(doc->undoText);
        doc->undoText = NULL;
    }
    doc->canUndo = false;
    doc->inTypingRun = false;
    SetUndoMenuEnabled(false);
}

void DocUndo(DocState *doc)
{
    Handle curText;
    short curLen;
    short curSelStart, curSelEnd;
    LineEndKind curLE;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    if (doc == NULL || !doc->canUndo || doc->undoText == NULL)
    {
        SysBeep(1);
        return;
    }

    /* Stash current state so a second Cmd-Z toggles back (redo).
       If memory is too tight for the snapshot, proceed with the undo
       anyway — the user just loses the ability to redo. */
    curText = SnapshotText(doc, &curLen);
    curSelStart = (**doc->te).selStart;
    curSelEnd = (**doc->te).selEnd;
    curLE = doc->leKind;

    GetPort(&savedPort);
    SetPort(doc->window);
    savedClip = NewRgn();
    emptyRgn = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    HLock(doc->undoText);
    TESetText(*doc->undoText, doc->undoLen, doc->te);
    HUnlock(doc->undoText);

    TESetSelect(doc->undoSelStart, doc->undoSelEnd, doc->te);
    doc->selAnchor = doc->undoSelStart;
    doc->leKind = doc->undoLE;

    MdRestyleAll(doc->te);

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);

    DisposeHandle(doc->undoText);
    if (curText != NULL) {
        doc->undoText = curText;
        doc->undoLen = curLen;
        doc->undoSelStart = curSelStart;
        doc->undoSelEnd = curSelEnd;
        doc->undoLE = curLE;
        doc->canUndo = true;
    } else {
        doc->undoText = NULL;
        doc->canUndo = false;
        SetUndoMenuEnabled(false);
    }
    doc->inTypingRun = false;

    doc->dirty = true;
    DocUpdateTitle(doc);
    DocAdjustScrollbar(doc);

    /* Wipe the view region so removed glyphs don't leave ghosts; the
       chrome will repaint from the InvalRect-scheduled updateEvt. */
    {
        Rect viewR = (**doc->te).viewRect;
        EraseRect(&viewR);
        TEUpdate(&viewR, doc->te);
    }
    SetPort(savedPort);
    InvalRect(&doc->window->portRect);
}
