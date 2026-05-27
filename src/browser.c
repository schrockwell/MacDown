#include "browser.h"

#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Memory.h>
#include <Resources.h>
#include <Sound.h>
#include <Dialogs.h>
#include <StandardFile.h>
#include <ToolUtils.h>
#include <Events.h>
#include <Multiverse.h>

#include "file_io.h"
#include "document.h"
#include "toolbar_icons.h"

#define kBrowserWindowID  131
#define kRowHeight        14    /* Geneva 9pt line height + 2px padding so
                                   rows don't share an edge pixel with the
                                   row above (which the next row's white
                                   background would otherwise clobber). */
#define kLeftMargin       6
#define kScrollBarWidth   15

/* Top button strip. 15x15 icons inside a 1-px inner margin + 1-px frame
   = 19x19 button.  1-px drop shadow below, 1-px margin, 1-px separator,
   then the list.

   Layout:
       y=0..1   top padding (kButtonTopPad)
       y=2..20  button frame + body (kButtonHeight = 19)
       y=21     drop shadow row
       y=22     margin between shadow and separator
       y=23     separator line (kButtonRowHeight - 1)
       y=24     list begins (kButtonRowHeight) */
#define kButtonRowHeight  24
#define kButtonHeight     19
#define kButtonWidth      19
#define kButtonTopPad      2
#define kButtonGap         3

typedef struct {
    Str63   name;
    long    dirID;       /* meaningful when isFolder */
    Boolean isFolder;
} BrowserEntry;

static WindowPtr gWindow      = NULL;

/* Persistent across window opens/closes within the session, so
   re-opening the browser drops the user back where they were.
   Survives BrowserClose; only reset on quit. */
static short     gFolderVRef  = 0;
static long      gFolderID    = 0;
static long      gParentDirID = 0;
static Str63     gFolderName;

/* Transient -- owned only while gWindow is non-NULL. */
static short     gCurrentWD   = 0;

/* Auto-refresh: the HFS directory mod date bumps when files are added,
   removed, or renamed inside it. We poll it on idle and reload the
   list if it's changed. Throttle to once every ~2 seconds so we
   don't hammer PBGetCatInfoSync. */
#define kBrowserIdleTicks  120L
static long      gLastModDate    = 0;
static long      gLastIdleCheck  = 0;

static Handle    gEntries     = NULL;
static short     gNumEntries  = 0;
static short     gSelectedIdx = -1;
static short     gTopIdx      = 0;

static ControlHandle gScrollBar = NULL;
/* Custom buttons -- rect + draw helper rather than Control Manager
   controls. Lets us pick our own font (Geneva 9) and guarantees the
   buttons repaint on every BrowserDrawList instead of being clobbered
   by a window resize. */
static Rect gParentBtnR;   /* ".." -> go to parent */
static Rect gGoBtnR;       /* "go" -> Go to... dialog */
static Rect gNewBtnR;      /* "+"  -> new folder in current dir */
static Rect gDelBtnR;      /* "-"  -> delete selected entry */

static long      gLastClickTime = 0;
static short     gLastClickRow  = -1;

/* ---- Offscreen double-buffer ---- Draw the list into a temporary
   bitmap and blit in one CopyBits so the user never sees the
   intermediate erase + per-row paint frames. */
typedef struct {
    GrafPort port;
    BitMap   bm;
    Ptr      bits;
    Boolean  active;
} OffscreenBuf;
static OffscreenBuf gOffscreen;

static Boolean OffscreenBegin(const Rect *r)
{
    GrafPtr savedPort;
    short width  = r->right  - r->left;
    short height = r->bottom - r->top;
    short rowBytes;
    long  pixSize;
    if (gOffscreen.active) return false;
    if (width <= 0 || height <= 0) return false;
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
    (void)savedPort;
    gOffscreen.active = true;
    return true;
}

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

/* ---- forward decls ---- */
static Boolean BrowserPickFolder(short *outVRef, long *outDirID);
static void    BrowserCreateNewFolder(void);
static void    BrowserDeleteSelected(void);
static void    BrowserDrawButtonIcon(const Rect *r,
                                     const unsigned short *iconBits,
                                     Boolean pressed);
static void    BrowserComputeButtonRects(void);
static void    BrowserDrawButton(const Rect *r, const unsigned short *iconBits, Boolean pressed);
static void    BrowserDrawButtons(void);
static void    BrowserDrawChrome(void);
static Boolean BrowserTrackButton(const Rect *r, const unsigned short *iconBits);
static void    BrowserLoadCurrent(void);
static void    BrowserDrawList(void);
static void    BrowserUpdateTitle(void);
static void    BrowserNavigateTo(short vRefNum, long dirID);
static void    BrowserOpenFileEntry(short idx);
static BrowserEntry *BrowserEntries(void);
static void    BrowserSetCount(short n);
static Rect    BrowserListRect(void);
static short   BrowserRowsVisible(void);
static void    BrowserAdjustScrollBar(void);
static pascal void BrowserScrollAction(ControlHandle ctl, short part);

/* ---- helpers ---- */

static BrowserEntry *BrowserEntries(void)
{
    return (gEntries == NULL) ? NULL : (BrowserEntry *) *gEntries;
}

static void BrowserSetCount(short n)
{
    long size = (long)n * (long)sizeof(BrowserEntry);
    if (size < (long)sizeof(BrowserEntry)) size = (long)sizeof(BrowserEntry);
    if (gEntries == NULL) gEntries = NewHandle(size);
    else                  SetHandleSize(gEntries, size);
    gNumEntries = n;
}

static void BrowserComputeButtonRects(void)
{
    short x = kButtonGap;
    gParentBtnR.top    = kButtonTopPad;
    gParentBtnR.bottom = kButtonTopPad + kButtonHeight;
    gParentBtnR.left   = x;
    gParentBtnR.right  = x + kButtonWidth;
    x = gParentBtnR.right + kButtonGap;
    gGoBtnR.top    = kButtonTopPad;
    gGoBtnR.bottom = kButtonTopPad + kButtonHeight;
    gGoBtnR.left   = x;
    gGoBtnR.right  = x + kButtonWidth;
    x = gGoBtnR.right + kButtonGap;
    gNewBtnR.top    = kButtonTopPad;
    gNewBtnR.bottom = kButtonTopPad + kButtonHeight;
    gNewBtnR.left   = x;
    gNewBtnR.right  = x + kButtonWidth;
    x = gNewBtnR.right + kButtonGap;
    gDelBtnR.top    = kButtonTopPad;
    gDelBtnR.bottom = kButtonTopPad + kButtonHeight;
    gDelBtnR.left   = x;
    gDelBtnR.right  = x + kButtonWidth;
}

/* CopyBits a kToolbarIconSize-square 1-bit bitmap centered inside `r`.
   Use srcOr when the button is unpressed (icon's 1-bits become black on
   the white interior) and srcBic when pressed (icon's 1-bits clear the
   black interior to white, giving a proper inverted look). */
static void BrowserDrawButtonIcon(const Rect *r,
                                  const unsigned short *iconBits,
                                  Boolean pressed)
{
    BitMap bm;
    Rect srcR, dstR;
    GrafPtr port;
    short cw = r->right - r->left;
    short ch = r->bottom - r->top;

    srcR.left = 0;  srcR.top = 0;
    srcR.right = kToolbarIconSize;  srcR.bottom = kToolbarIconSize;

    dstR.left = r->left + (cw - kToolbarIconSize) / 2;
    dstR.top  = r->top  + (ch - kToolbarIconSize) / 2;
    dstR.right  = dstR.left + kToolbarIconSize;
    dstR.bottom = dstR.top  + kToolbarIconSize;

    bm.baseAddr = (Ptr)iconBits;
    bm.rowBytes = kToolbarIconRowBytes;
    bm.bounds   = srcR;

    GetPort(&port);
    CopyBits(&bm, &port->portBits, &srcR, &dstR,
             pressed ? srcBic : srcOr, NULL);
}

static void BrowserDrawButton(const Rect *r, const unsigned short *iconBits,
                              Boolean pressed)
{
    EraseRect(r);
    FrameRect(r);

    /* 1px drop shadow -- an L along the bottom and right edges, one
       pixel outside the button's frame. Drawn the same way whether
       the button is pressed or not so the "weight" cue is consistent. */
    MoveTo(r->left + 1, r->bottom);
    LineTo(r->right,    r->bottom);
    MoveTo(r->right,    r->top + 1);
    LineTo(r->right,    r->bottom);

    if (pressed) {
        Rect inner = *r;
        InsetRect(&inner, 1, 1);
        PaintRect(&inner);
    }

    if (iconBits) BrowserDrawButtonIcon(r, iconBits, pressed);
}

static void BrowserDrawButtons(void)
{
    /* Erase the whole button strip so a resize that grows the window
       doesn't leave debris around the button rects. */
    Rect strip;
    if (gWindow == NULL) return;
    strip.left   = 0;
    strip.top    = 0;
    strip.right  = gWindow->portRect.right;
    strip.bottom = kButtonRowHeight - 1;
    EraseRect(&strip);
    BrowserDrawButton(&gParentBtnR, kIcon_folder_up_15px,   false);
    BrowserDrawButton(&gGoBtnR,     kIcon_folder_open_15px, false);
    BrowserDrawButton(&gNewBtnR,    kIcon_folder_new_15px,  false);
    BrowserDrawButton(&gDelBtnR,    kIcon_trash_15px,       false);
}

static void BrowserDrawChrome(void)
{
    if (gWindow == NULL) return;
    /* The chrome (button strip + separator) is independent of list
       state. We redraw it on update / resize only; the row-redraw
       path (BrowserDrawList) deliberately doesn't touch this area so
       a scroll or selection doesn't flash the button strip. */
    SetPort(gWindow);
    BrowserDrawButtons();
    MoveTo(0, kButtonRowHeight - 1);
    LineTo(gWindow->portRect.right, kButtonRowHeight - 1);
}

static Boolean BrowserTrackButton(const Rect *r, const unsigned short *iconBits)
{
    Point pt;
    Boolean inside = true;
    Boolean lastInside = true;
    BrowserDrawButton(r, iconBits, true);
    while (StillDown()) {
        GetMouse(&pt);
        inside = PtInRect(pt, r);
        if (inside != lastInside) {
            BrowserDrawButton(r, iconBits, inside);
            lastInside = inside;
        }
    }
    BrowserDrawButton(r, iconBits, false);
    return inside;
}

static Rect BrowserListRect(void)
{
    Rect r;
    r.left   = 0;
    /* Start the list strictly below the button-row separator line. */
    r.top    = kButtonRowHeight;
    /* Leave the right column for the scrollbar and the bottom-right
       corner for the grow icon. */
    r.right  = (gWindow ? gWindow->portRect.right : 0) - kScrollBarWidth;
    r.bottom = (gWindow ? gWindow->portRect.bottom : 0) - kScrollBarWidth;
    return r;
}

static short BrowserRowsVisible(void)
{
    Rect r = BrowserListRect();
    if (r.bottom <= r.top) return 0;
    return (r.bottom - r.top) / kRowHeight;
}

static void BrowserAdjustScrollBar(void)
{
    short rowsVisible;
    short max;
    if (gScrollBar == NULL) return;
    rowsVisible = BrowserRowsVisible();
    max = gNumEntries - rowsVisible;
    if (max < 0) max = 0;
    SetControlMaximum(gScrollBar, max);
    if (gTopIdx > max) gTopIdx = max;
    if (gTopIdx < 0)   gTopIdx = 0;
    SetControlValue(gScrollBar, gTopIdx);
    HiliteControl(gScrollBar, (max == 0) ? 255 : 0);
}

static pascal void BrowserScrollAction(ControlHandle ctl, short part)
{
    short delta = 0;
    short rowsVisible = BrowserRowsVisible();
    short oldVal, newVal;
    if (part == 0) return;
    switch (part) {
        case inUpButton:    delta = -1; break;
        case inDownButton:  delta =  1; break;
        case inPageUp:      delta = -(rowsVisible - 1); if (delta > -1) delta = -1; break;
        case inPageDown:    delta =  (rowsVisible - 1); if (delta <  1) delta =  1; break;
        default: return;
    }
    oldVal = GetControlValue(ctl);
    newVal = oldVal + delta;
    if (newVal < GetControlMinimum(ctl)) newVal = GetControlMinimum(ctl);
    if (newVal > GetControlMaximum(ctl)) newVal = GetControlMaximum(ctl);
    if (newVal == oldVal) return;
    SetControlValue(ctl, newVal);
    gTopIdx = newVal;
    BrowserDrawList();
}

static Boolean StrEq(ConstStr255Param a, ConstStr255Param b)
{
    short i;
    if (a[0] != b[0]) return false;
    for (i = 1; i <= a[0]; i++) if (a[i] != b[i]) return false;
    return true;
}

static void StrCopy(StringPtr dst, ConstStr255Param src)
{
    short i;
    dst[0] = src[0];
    for (i = 1; i <= src[0]; i++) dst[i] = src[i];
}

/* ---- initial folder picker ---- */

/* Standard File's dlgHook lets us mutate the dialog while it's up.
   The Save button is item 1 in the SFPutFile DITL; we relabel it to
   "Open" on the first hook call so the dialog reads as a folder
   picker rather than a save dialog. gHookFirstCall is reset before
   each SFPutFile invocation so re-opens still get the relabel. */
static Boolean gHookFirstCall = false;

static pascal short FolderPickerHook(short item, DialogPtr dlg)
{
    if (gHookFirstCall) {
        short itype, i;
        Handle ihdl;
        Rect ir;
        gHookFirstCall = false;

        /* Relabel item 1 (the Save button) as "Open" so the dialog
           reads as a folder picker rather than a save sheet. */
        GetDialogItem(dlg, 1, &itype, &ihdl, &ir);
        if (ihdl != NULL) SetControlTitle((ControlHandle)ihdl, "\pOpen");

        /* Hide the filename edit field (and the "Save current
           document as:" label sitting beside it). Item numbers vary
           between system versions; walk items 3..20 and hide any
           editText or statText we find. The Save/Cancel buttons
           (items 1-2) stay visible. */
        for (i = 3; i <= 20; i++) {
            GetDialogItem(dlg, i, &itype, &ihdl, &ir);
            if (ihdl == NULL) continue;
            {
                short basicType = itype & 0x7F;
                if (basicType == editText || basicType == statText) {
                    HideDialogItem(dlg, i);
                }
            }
        }
    }
    return item;
}

/* Show the SFPutFile-as-folder-picker dialog. On Open click, returns
   the chosen folder's real (vRefNum, dirID). Caller takes it from
   there -- BrowserPickFolder itself doesn't touch any global state. */
static Boolean BrowserPickFolder(short *outVRef, long *outDirID)
{
    SFReply reply;
    Point where;
    short i;
    GrafPtr savedPort;

    { char *p = (char *)&reply; for (i = 0; i < (short)sizeof(SFReply); i++) p[i] = 0; }

    GetPort(&savedPort);
    InitCursor();
    where.h = 80;
    where.v = 60;
    gHookFirstCall = true;
    SFPutFile(where, "\pBrowse this folder:", "\p(select)",
              (DlgHookUPP)FolderPickerHook, &reply);
    SetPort(savedPort);
    { EventRecord ev; EventAvail(0, &ev); }
    if (!reply.good) return false;

    return (FileIOResolveWD(reply.vRefNum, outVRef, outDirID) == noErr);
}

/* Set the persistent default location if it hasn't been set yet.
   Falls back to the boot volume's root (HFS dirID 2), which is the
   application's own drive in every normal launch scenario.

   IMPORTANT: SysEnvirons.sysVRefNum and GetVol's output are both
   wdRefNums, not real volume references. We have to resolve them to
   the underlying (vRefNum, dirID) so gFolderVRef stays comparable to
   what FileIOResolveWD returns for doc-window wdRefNums -- otherwise
   DocFindOpenFile mistakes the wdRefNum-vs-vRef as different paths
   and lets duplicate windows open. */
static void BrowserEnsureDefault(void)
{
    SysEnvRec se;
    short rawWD = 0;
    if (gFolderID != 0) return;
    if (SysEnvirons(2, &se) == noErr) {
        rawWD = se.sysVRefNum;
    } else {
        GetVol(NULL, &rawWD);
    }
    {
        short vRef;
        long  dirID;
        if (FileIOResolveWD(rawWD, &vRef, &dirID) == noErr) {
            gFolderVRef = vRef;
        } else {
            gFolderVRef = rawWD;
        }
    }
    gFolderID = 2;            /* HFS volume root */
    gFolderName[0] = 0;
}

/* ---- New Folder prompt ----

   DLOG/DITL 130: edit-text field + Create/Cancel buttons + a userItem
   we draw as a 3-px rounded outline around the Create button (the
   classic-Mac default-button convention; predates SetDialogDefaultItem
   which is System 7+). Return invokes Create, Escape invokes Cancel. */

static pascal void DrawDefaultBtnOutline(DialogPtr dlg, short itemNo)
{
    short itype;
    Handle ihdl;
    Rect r;
    PenState saved;
    GetDialogItem(dlg, itemNo, &itype, &ihdl, &r);
    GetPenState(&saved);
    PenNormal();
    PenSize(3, 3);
    FrameRoundRect(&r, 16, 16);
    SetPenState(&saved);
}

static pascal Boolean NewFolderFilter(DialogPtr dlg, EventRecord *ev, short *itemHit)
{
    if (ev->what == keyDown || ev->what == autoKey) {
        char c = ev->message & charCodeMask;
        if (c == '\r' || c == 0x03) { *itemHit = 1; return true; }
        if (c == 0x1B)              { *itemHit = 2; return true; }
    }
    (void)dlg;
    return false;
}

static Boolean BrowserPromptFolderName(StringPtr outName)
{
    DialogPtr dlg;
    short itype;
    Handle ihdl;
    Rect ir;
    short hit;
    ModalFilterUPP filter;
    UserItemUPP outlineUPP;
    Str255 entered;

    outName[0] = 0;
    dlg = GetNewDialog(130, NULL, (WindowPtr)-1L);
    if (dlg == NULL) return false;

    /* Install the default-button outline draw proc on item 5. */
    outlineUPP = NewUserItemProc(DrawDefaultBtnOutline);
    GetDialogItem(dlg, 5, &itype, &ihdl, &ir);
    SetDialogItem(dlg, 5, itype, (Handle)outlineUPP, &ir);

    /* Pre-select the default name so typing replaces it. */
    SelectDialogItemText(dlg, 4, 0, 32767);

    ShowWindow(dlg);
    SetPort(dlg);
    filter = NewModalFilterProc(NewFolderFilter);

    for (;;) {
        ModalDialog(filter, &hit);
        if (hit == 1 || hit == 2) break;
    }

    if (hit == 1) {
        GetDialogItem(dlg, 4, &itype, &ihdl, &ir);
        if (ihdl != NULL) {
            GetDialogItemText(ihdl, entered);
            if (entered[0] > 63) entered[0] = 63;
            StrCopy(outName, entered);
        }
    }

    DisposeRoutineDescriptor((UniversalProcPtr)filter);
    DisposeRoutineDescriptor((UniversalProcPtr)outlineUPP);
    DisposeDialog(dlg);
    return (hit == 1 && outName[0] > 0);
}

static void BrowserCreateNewFolder(void)
{
    Str63 name;
    HParamBlockRec pb;
    short i;
    OSErr err;

    if (!BrowserPromptFolderName(name)) return;

    {
        char *p = (char *)&pb;
        for (i = 0; i < (short)sizeof(pb); i++) p[i] = 0;
    }
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = gFolderVRef;
    pb.fileParam.ioDirID   = gFolderID;
    err = PBDirCreateSync(&pb);
    if (err != noErr) {
        SysBeep(1);
        return;
    }

    BrowserLoadCurrent();
    BrowserDrawList();
}

/* ---- Permanently delete selected entry ----

   No Trash semantics: HDelete removes the catalog entry outright. We
   refuse non-empty folders up front so the user gets a clear error
   instead of an opaque ioErr. Always confirms. */

static Boolean FolderIsEmpty(short vRef, long dirID)
{
    CInfoPBRec pb;
    Str63 nm;
    OSErr err;
    nm[0] = 0;
    pb.hFileInfo.ioCompletion = NULL;
    pb.hFileInfo.ioNamePtr    = (StringPtr)nm;
    pb.hFileInfo.ioVRefNum    = vRef;
    pb.hFileInfo.ioFVersNum   = 0;
    pb.hFileInfo.ioFDirIndex  = 1;     /* first entry in dir */
    pb.hFileInfo.ioDirID      = dirID;
    err = PBGetCatInfoSync(&pb);
    return (err != noErr);             /* fnfErr / dir-end → empty */
}

static void BrowserDeleteSelected(void)
{
    BrowserEntry *entries;
    Str63 name;
    Boolean isFolder;
    long  targetDir;
    Str255 typeLabel;
    short hit;
    OSErr err;

    if (gSelectedIdx < 0 || gSelectedIdx >= gNumEntries) {
        SysBeep(1);
        return;
    }
    HLock(gEntries);
    entries = BrowserEntries();
    StrCopy(name, entries[gSelectedIdx].name);
    isFolder  = entries[gSelectedIdx].isFolder;
    targetDir = entries[gSelectedIdx].dirID;
    HUnlock(gEntries);

    if (isFolder && !FolderIsEmpty(gFolderVRef, targetDir)) {
        ParamText(name, "\p", "\p", "\p");
        (void)StopAlert(132, NULL);
        return;
    }

    {
        const unsigned char *folderLbl = (const unsigned char *)"\pfolder";
        const unsigned char *fileLbl   = (const unsigned char *)"\pfile";
        StrCopy(typeLabel, isFolder ? folderLbl : fileLbl);
    }
    ParamText(name, typeLabel, "\p", "\p");
    hit = CautionAlert(131, NULL);
    if (hit != 1) return;

    err = HDelete(gFolderVRef, gFolderID, name);
    if (err != noErr) {
        SysBeep(1);
        return;
    }

    gSelectedIdx = -1;
    BrowserLoadCurrent();
    BrowserDrawList();
}

/* ---- enumerate + reload ---- */

static void BrowserLoadCurrent(void)
{
    short index;
    short count = 0;
    short cap = 16;
    BrowserEntry *entries;

    if (gEntries) { DisposeHandle(gEntries); gEntries = NULL; }
    gNumEntries  = 0;
    gSelectedIdx = -1;

    /* Folder name + parent dirID via PBGetCatInfo on the directory itself. */
    {
        CInfoPBRec pb;
        Str63 nm;
        OSErr err;
        nm[0] = 0;
        pb.dirInfo.ioCompletion = NULL;
        pb.dirInfo.ioNamePtr    = (StringPtr)nm;
        pb.dirInfo.ioVRefNum    = gFolderVRef;
        pb.dirInfo.ioFDirIndex  = -1;     /* lookup by dirID */
        pb.dirInfo.ioDrDirID    = gFolderID;
        err = PBGetCatInfoSync(&pb);
        if (err == noErr) {
            StrCopy(gFolderName, nm);
            gParentDirID = pb.dirInfo.ioDrParID;
            gLastModDate = pb.dirInfo.ioDrMdDat;
        } else {
            const unsigned char *fb = (const unsigned char *)"\pFolder";
            StrCopy(gFolderName, fb);
            gParentDirID = 0;
            gLastModDate = 0;
        }
    }

    BrowserSetCount(cap);
    HLock(gEntries);
    entries = BrowserEntries();

    /* No synthetic ".." / "Go to..." rows -- those live in the
       button strip at the top of the window now. */
    count = 0;

    for (index = 1; ; index++) {
        CInfoPBRec pb;
        Str63 name;
        OSErr err;
        Boolean isDir;
        name[0] = 0;
        pb.hFileInfo.ioCompletion = NULL;
        pb.hFileInfo.ioNamePtr    = (StringPtr)name;
        pb.hFileInfo.ioVRefNum    = gFolderVRef;
        pb.hFileInfo.ioFVersNum   = 0;
        pb.hFileInfo.ioFDirIndex  = index;
        pb.hFileInfo.ioDirID      = gFolderID;
        err = PBGetCatInfoSync(&pb);
        if (err != noErr) break;
        isDir = (pb.hFileInfo.ioFlAttrib & 0x10) != 0;
        if (!isDir) {
            if (pb.hFileInfo.ioFlFndrInfo.fdFlags & 0x4000) continue;  /* invisible */
            if (pb.hFileInfo.ioFlFndrInfo.fdType != 'TEXT')   continue;
        }

        if (count >= cap) {
            cap *= 2;
            HUnlock(gEntries);
            BrowserSetCount(cap);
            HLock(gEntries);
            entries = BrowserEntries();
        }
        StrCopy(entries[count].name, name);
        entries[count].isFolder = isDir;
        entries[count].dirID    = isDir ? pb.dirInfo.ioDrDirID : 0;
        count++;
    }

    HUnlock(gEntries);
    BrowserSetCount(count);
    gTopIdx = 0;
    BrowserUpdateTitle();
}

/* ---- drawing ---- */

static void BrowserUpdateTitle(void)
{
    if (gWindow) SetWTitle(gWindow, gFolderName);
}

static void BrowserDrawList(void)
{
    Rect listR;
    short rowsVisible;
    short i;
    GrafPtr port;
    BrowserEntry *entries;

    if (gWindow == NULL) return;
    GetPort(&port);
    SetPort(gWindow);

    /* List body only. Chrome (button strip + separator) is drawn by
       BrowserDrawChrome on update/resize so a scroll or row select
       doesn't flash the buttons. */
    listR = BrowserListRect();
    rowsVisible = BrowserRowsVisible();

    /* Build the list off-screen, then copy in one shot so no
       intermediate erase + per-row paint flashes the user. */
    {
        Boolean usingOffscreen = OffscreenBegin(&listR);
        if (!usingOffscreen) EraseRect(&listR);   /* direct fallback */

        HLock(gEntries);
        entries = BrowserEntries();
        TextFont(3);    /* Geneva */
        TextSize(9);
        TextFace(0);

        for (i = 0; i < rowsVisible && (gTopIdx + i) < gNumEntries; i++) {
            short idx = gTopIdx + i;
            Rect rowR;
            rowR.left   = listR.left;
            rowR.right  = listR.right;
            rowR.top    = listR.top + i * kRowHeight;
            rowR.bottom = rowR.top + kRowHeight;
            if (idx == gSelectedIdx) {
                PaintRect(&rowR);
                TextMode(srcXor);
            } else {
                TextMode(srcCopy);
            }
            /* Baseline 3px above the row's bottom -- leaves descenders
               inside the row without crowding the divider with the row
               below. */
            MoveTo(rowR.left + kLeftMargin, rowR.bottom - 4);
            if (entries[idx].isFolder) TextFace(bold);
            DrawString(entries[idx].name);
            if (entries[idx].isFolder) {
                DrawString("\p/");
                TextFace(0);
            }
            TextMode(srcCopy);
        }
        HUnlock(gEntries);

        if (usingOffscreen) OffscreenCopyAndEnd(gWindow, &listR);
    }

    /* The grow icon and scrollbar live outside listR, so the
       offscreen blit can't touch them. We DON'T erase + redraw the
       grow corner here -- that pair flashed white between erase and
       icon paint on every row select. The icon stays drawn from
       window setup / activate / update events, which is when it
       actually changes state. */
    if (gScrollBar) {
        BrowserAdjustScrollBar();
        Draw1Control(gScrollBar);
    }

    SetPort(port);
}

/* ---- navigation ---- */

static void BrowserNavigateTo(short vRefNum, long dirID)
{
    short ownedWD;
    if (FileIOOwnWDFromDir(vRefNum, dirID, &ownedWD) != noErr) {
        SysBeep(1);
        return;
    }
    if (gCurrentWD != 0) FileIOReleaseWD(gCurrentWD);
    gCurrentWD  = ownedWD;
    gFolderVRef = vRefNum;
    gFolderID   = dirID;
    BrowserLoadCurrent();
    BrowserDrawList();
}

static void BrowserOpenFileEntry(short idx)
{
    BrowserEntry *entries;
    Str63 name;
    DocState *existing;

    if (idx < 0 || idx >= gNumEntries) return;
    HLock(gEntries);
    entries = BrowserEntries();
    if (entries[idx].isFolder) { HUnlock(gEntries); return; }
    StrCopy(name, entries[idx].name);
    HUnlock(gEntries);

    /* If the file is already open, raise that window instead of
       creating a duplicate. (DocOpenFromDir does the same check
       internally; the explicit guard here means we skip even the
       OpenWD round-trip and any error paths in the open code.) */
    existing = DocFindOpenFile(gFolderVRef, gFolderID, name);
    if (existing != NULL) {
        SelectWindow(existing->window);
        return;
    }

    DocOpenFromDir(gFolderVRef, gFolderID, name);
}

/* ---- public lifecycle ---- */

Boolean BrowserIsWindow(WindowPtr w)
{
    return (w != NULL && w == gWindow);
}

void BrowserShow(void)
{
    if (gWindow == NULL) {
        Rect sbR;
        /* No folder picker on first appearance: fall back to the
           remembered location (gFolderVRef / gFolderID) or the boot
           volume's root if we've never been opened. */
        BrowserEnsureDefault();
        if (FileIOOwnWDFromDir(gFolderVRef, gFolderID, &gCurrentWD) != noErr) {
            SysBeep(1);
            return;
        }
        gWindow = GetNewWindow(kBrowserWindowID, NULL, (WindowPtr)-1L);
        if (gWindow == NULL) {
            FileIOReleaseWD(gCurrentWD); gCurrentWD = 0;
            return;
        }
        SetPort(gWindow);

        /* Top-strip button rects. Custom drawn (not Control Manager)
           so we can set Geneva 9 and have them repaint via our own
           list-redraw path -- a window resize would otherwise clobber
           pushButProc-drawn buttons after an EraseRect. */
        BrowserComputeButtonRects();

        /* Scrollbar runs from the separator line down to the
           grow-icon box. */
        sbR.left   = gWindow->portRect.right - kScrollBarWidth;
        sbR.right  = gWindow->portRect.right + 1;
        sbR.top    = kButtonRowHeight - 1;
        sbR.bottom = gWindow->portRect.bottom - (kScrollBarWidth - 1);
        gScrollBar = NewControl(gWindow, &sbR, "\p", true,
                                0, 0, 0, scrollBarProc, 0);

        BrowserLoadCurrent();
        gTopIdx = 0;
        ShowWindow(gWindow);
        SelectWindow(gWindow);
        {
            Rect r = gWindow->portRect;
            EraseRect(&r);
        }
        DrawGrowIcon(gWindow);
        BrowserDrawChrome();
        BrowserDrawList();
        InvalRect(&gWindow->portRect);
    } else {
        SelectWindow(gWindow);
    }
}

void BrowserToggle(void)
{
    if (gWindow == NULL) {
        BrowserShow();
        return;
    }
    if (((WindowPeek)gWindow)->visible) {
        HideWindow(gWindow);
    } else {
        ShowWindow(gWindow);
        SelectWindow(gWindow);
    }
}

void BrowserClose(void)
{
    if (gWindow == NULL) return;
    /* DisposeWindow disposes child controls automatically. The
       persistent gFolderVRef/gFolderID/gFolderName are intentionally
       NOT cleared so the next BrowserShow drops the user back where
       they left off. */
    DisposeWindow(gWindow);
    gWindow = NULL;
    gScrollBar = NULL;
    if (gEntries) { DisposeHandle(gEntries); gEntries = NULL; }
    if (gCurrentWD != 0) { FileIOReleaseWD(gCurrentWD); gCurrentWD = 0; }
    gNumEntries  = 0;
    gSelectedIdx = -1;
    gTopIdx      = 0;
    gLastClickRow = -1;
}

void BrowserUpdate(void)
{
    GrafPtr savedPort;
    if (gWindow == NULL) return;
    GetPort(&savedPort);
    SetPort(gWindow);
    BeginUpdate(gWindow);
    BrowserDrawChrome();
    BrowserDrawList();
    DrawGrowIcon(gWindow);
    EndUpdate(gWindow);
    SetPort(savedPort);
}

void BrowserActivate(Boolean active)
{
    GrafPtr savedPort;
    if (gWindow == NULL) return;
    GetPort(&savedPort);
    SetPort(gWindow);
    if (active) {
        if (gScrollBar) ShowControl(gScrollBar);
    } else {
        if (gScrollBar) HideControl(gScrollBar);
    }
    DrawGrowIcon(gWindow);
    SetPort(savedPort);
}

void BrowserClick(EventRecord *ev)
{
    Point local;
    GrafPtr savedPort;
    short row;
    Rect listR;
    BrowserEntry *entries;
    Boolean isDouble;

    if (gWindow == NULL) return;
    GetPort(&savedPort);
    SetPort(gWindow);
    local = ev->where;
    GlobalToLocal(&local);

    /* Custom buttons: simple PtInRect + tracking loop. */
    if (PtInRect(local, &gParentBtnR)) {
        if (BrowserTrackButton(&gParentBtnR, kIcon_folder_up_15px)) {
            if (gParentDirID > 0) BrowserNavigateTo(gFolderVRef, gParentDirID);
        }
        SetPort(savedPort);
        return;
    }
    if (PtInRect(local, &gGoBtnR)) {
        if (BrowserTrackButton(&gGoBtnR, kIcon_folder_open_15px)) {
            short newVRef;
            long  newDirID;
            if (BrowserPickFolder(&newVRef, &newDirID)) {
                BrowserNavigateTo(newVRef, newDirID);
            }
        }
        SetPort(savedPort);
        return;
    }
    if (PtInRect(local, &gNewBtnR)) {
        if (BrowserTrackButton(&gNewBtnR, kIcon_folder_new_15px)) {
            BrowserCreateNewFolder();
        }
        SetPort(savedPort);
        return;
    }
    if (PtInRect(local, &gDelBtnR)) {
        if (BrowserTrackButton(&gDelBtnR, kIcon_trash_15px)) {
            BrowserDeleteSelected();
        }
        SetPort(savedPort);
        return;
    }

    /* Scrollbar hit-test. */
    if (gScrollBar) {
        ControlHandle ctl = NULL;
        short part = FindControl(local, gWindow, &ctl);
        if (part != 0 && ctl == gScrollBar) {
            if (part == inThumb) {
                short oldVal = GetControlValue(ctl);
                short newVal;
                TrackControl(ctl, local, NULL);
                newVal = GetControlValue(ctl);
                if (newVal != oldVal) {
                    gTopIdx = newVal;
                    BrowserDrawList();
                }
            } else {
                TrackControl(ctl, local, NewControlActionProc(BrowserScrollAction));
            }
            SetPort(savedPort);
            return;
        }
    }

    listR = BrowserListRect();
    if (!PtInRect(local, &listR)) { SetPort(savedPort); return; }
    row = gTopIdx + (local.v - listR.top) / kRowHeight;
    if (row < 0 || row >= gNumEntries) { SetPort(savedPort); return; }

    /* Double-click ~ 30 ticks default (System 6 GetDblTime glue isn't
       linked in our build). */
    isDouble = (gLastClickRow == row
                && (ev->when - gLastClickTime) <= 30L);
    gLastClickRow  = row;
    gLastClickTime = ev->when;

    HLock(gEntries);
    entries = BrowserEntries();
    {
        Boolean isFolder  = entries[row].isFolder;
        long    targetDir = entries[row].dirID;
        HUnlock(gEntries);

        if (!isDouble) {
            gSelectedIdx = row;
            BrowserDrawList();
            SetPort(savedPort);
            return;
        }

        if (isFolder) BrowserNavigateTo(gFolderVRef, targetDir);
        else          BrowserOpenFileEntry(row);
    }

    SetPort(savedPort);
}

void BrowserKey(EventRecord *ev)
{
    /* No keyboard interactions in v1. */
    (void)ev;
}

void BrowserIdle(void)
{
    long now;
    CInfoPBRec pb;
    Str63 nm;
    OSErr err;

    if (gWindow == NULL) return;
    /* Only poll while the browser is the frontmost window. If the
       user has another window in focus, leave the file system alone --
       they'll see the refresh next time they bring the browser up. */
    if (FrontWindow() != gWindow) return;
    now = TickCount();
    if (now - gLastIdleCheck < kBrowserIdleTicks) return;
    gLastIdleCheck = now;

    nm[0] = 0;
    pb.dirInfo.ioCompletion = NULL;
    pb.dirInfo.ioNamePtr    = (StringPtr)nm;
    pb.dirInfo.ioVRefNum    = gFolderVRef;
    pb.dirInfo.ioFDirIndex  = -1;
    pb.dirInfo.ioDrDirID    = gFolderID;
    err = PBGetCatInfoSync(&pb);
    if (err != noErr) return;
    if (pb.dirInfo.ioDrMdDat == gLastModDate) return;

    /* Directory changed since we last loaded -- a file was added,
       removed, or renamed. Reload + redraw. Selection resets, scroll
       starts at top (BrowserLoadCurrent zeroes gTopIdx). */
    BrowserLoadCurrent();
    BrowserDrawList();
}

Boolean BrowserCurrentFolder(short *outVRef, long *outDirID)
{
    if (gWindow == NULL || !((WindowPeek)gWindow)->visible) return false;
    if (gFolderID == 0) return false;
    if (outVRef)  *outVRef  = gFolderVRef;
    if (outDirID) *outDirID = gFolderID;
    return true;
}

void BrowserResize(void)
{
    GrafPtr savedPort;
    if (gWindow == NULL) return;
    GetPort(&savedPort);
    SetPort(gWindow);
    if (gScrollBar) {
        MoveControl(gScrollBar,
                    gWindow->portRect.right - kScrollBarWidth,
                    kButtonRowHeight - 1);
        SizeControl(gScrollBar,
                    kScrollBarWidth + 1,
                    gWindow->portRect.bottom - kButtonRowHeight
                        - (kScrollBarWidth - 2));
    }
    EraseRect(&gWindow->portRect);
    InvalRect(&gWindow->portRect);
    SetPort(savedPort);
}
