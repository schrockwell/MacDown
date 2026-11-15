#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <Events.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <Devices.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <Resources.h>
#include <Files.h>
#include <SegLoad.h>
#include <StandardFile.h>
#include <Sound.h>

#include "document.h"
#include "file_io.h"
#include "browser.h"
#include "markdown.h"
#include "apple_events.h"
#include "prefs.h"
#include "pstr.h"

#define kMenuBarID 128
#define kAppleMenuID 128
#define kFileMenuID 129
#define kEditMenuID 130
#define kFormatMenuID 131
#define kWindowsMenuID 132
#define kLineMenuID 133

#define kWindowsMenuBrowser 1
#define kWindowsMenuNext 2
#define kWindowsMenuStaticItems 3

#define kFileNew 1
#define kFileOpen 2
#define kFileOpenRecent 3 /* hierarchical: submenu kRecentMenuID */
#define kFileClose 5
#define kFileCloseAll 6
#define kFileSave 7
#define kFileSaveAs 8
#define kFileQuit 10

#define kRecentMenuID 134

#define kEditUndo 1
#define kEditCut 3
#define kEditCopy 4
#define kEditPaste 5
#define kEditClear 6
#define kEditSelAll 7
#define kEditDuplicate 9
#define kEditPreferences 11

#define kPrefsDLOG 133
/* DITL 133 items. The pop-ups are user items drawn and tracked here
   (System 6 has no pop-up menu control). */
#define kPrefsRestoreBox 1
#define kPrefsFontPop    3
#define kPrefsSizePop    4
#define kPrefsLEPop      6
#define kPrefsStylePop   8
/* Menu IDs for the pop-ups, inserted only while one is open. */
#define kPopFontMenuID   140
#define kPopSizeMenuID   141
#define kPopLEMenuID     142
#define kPopStyleMenuID  143

#define kFormatBold 1
#define kFormatItalic 2
#define kFormatCode 3
#define kFormatInsertLink 5
/* Items 7-9: the front document's line endings, in LineEndKind order
   (kLE_CR, kLE_LF, kLE_CRLF). */
#define kFormatLEFirst 7
#define kFormatLELast  9

#define kLineH1 1
#define kLineH2 2
#define kLineH3 3
#define kLineH4 4
#define kLineH5 5
#define kLineH6 6
#define kLineNoHeading 7
#define kLineToggleTask 9
#define kLineBlockquote 10
#define kLineIndent 12
#define kLineOutdent 13

#define kAboutItem 1
#define kShortcutsItem 2
#define kRestyleIdleTicks 6L

#define kLeftArrow 0x1C
#define kRightArrow 0x1D
#define kUpArrow 0x1E
#define kDownArrow 0x1F

/* Virtual key codes. The arrow keys on the Apple Extended Keyboard
   and the keypad arrows on the Mac Plus produce different char codes
   when Shift is held -- the Plus keypad flips into num-lock-style
   mode and emits '+' / '*' / '/' / '=' instead of the arrow chars.
   Detect by physical keycode instead, which Shift can't change. */
#define kKC_KP_Left 0x56   /* Plus keypad 4 */
#define kKC_KP_Right 0x58  /* Plus keypad 6 */
#define kKC_KP_Down 0x54   /* Plus keypad 2 */
#define kKC_KP_Up 0x5B     /* Plus keypad 8 */
#define kKC_Ext_Left 0x7B  /* Extended keyboard left */
#define kKC_Ext_Right 0x7C /* Extended keyboard right */
#define kKC_Ext_Down 0x7D  /* Extended keyboard down */
#define kKC_Ext_Up 0x7E    /* Extended keyboard up */

#define UC(c) ((unsigned char)(c))

static void Initialize(void);
static void ApplyFontPref(void);
static void OpenFromFinderLaunch(void);
static void HandleMouse(EventRecord *ev);
static void HandleKey(EventRecord *ev);
static void HandleMenu(long mResult);
static void AdjustMenus(void);
static void DoAbout(void);
static Boolean HandleReturnKey(DocState *doc);
static void DoToggleTask(DocState *doc);
static void DoDeleteWordBack(DocState *doc);
static void DoDeleteLineBack(DocState *doc);
static void DoInsertLink(DocState *doc);
static void DoFileOpen(void);
static Boolean IsArrow(char c);
static void HandleQuit(void);

static void Initialize(void)
{
    Handle menuBar;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
    MaxApplZone();
    MoreMasters();
    MoreMasters();
    MoreMasters();
    MoreMasters();

    menuBar = GetNewMBar(kMenuBarID);
    SetMenuBar(menuBar);
    AppendResMenu(GetMenuHandle(kAppleMenuID), 'DRVR');
    DisableItem(GetMenuHandle(kEditMenuID), kEditUndo);
    /* File > Open Recent submenu (hierMenu = -1: not in the menu bar). */
    {
        MenuHandle recent = GetMenu(kRecentMenuID);
        if (recent != NULL)
            InsertMenu(recent, -1);
    }
    DrawMenuBar();

    AEInit();
    DocAppInit();
    /* Before any document opens, so launch-time opens land in Open
       Recent and the saved folders are available. */
    PrefsLoad();
    ApplyFontPref();
    RebuildRecentMenu();
    {
        Rect r;
        if (PrefsGetBrowserBounds(&r))
            BrowserSetSavedBounds(&r);
    }
}

/* Reopen what was open at the last quit, if the user wants that.
   Anything that can't be found or read is skipped silently. Documents
   are stored back to front, so opening them in order leaves the old
   front window in front. */
static void RestoreSession(void)
{
    short i, n;

    if (!PrefsGetRestoreSession())
        return;
    if (PrefsGetBrowserOpen())
        BrowserShow();

    n = PrefsOpenDocCount();
    for (i = 0; i < n; i++)
    {
        short vRef;
        long dirID;
        Str255 name;
        Rect bounds;
        if (PrefsOpenDocGet(i, &vRef, &dirID, name, &bounds))
            (void)DocOpenFromDirQuiet(vRef, dirID, name, &bounds);
    }
}

/* ---- Edit > Preferences ----

   A modeless window (close box, no OK/Cancel). Every change takes
   effect and is written to the prefs file immediately. */

static DialogPtr gPrefsDlg = NULL;
static UserItemUPP gPopDrawUPP = NULL;

/* The editor font from prefs, as a font number. No saved font, or one
   that's no longer installed, means Geneva (or the application font if
   even Geneva is missing). GetFNum gives 0 both for the system font
   and for a missing font, so tell those apart by name. */
static void ApplyFontPref(void)
{
    Str255 name, sysName;
    short size, num = applFont, n = 0;
    GetFNum("\pGeneva", &n);
    if (n != 0)
        num = n;
    n = 0;
    PrefsGetFont(name, &size);
    if (name[0] != 0)
    {
        GetFNum(name, &n);
        if (n != 0)
            num = n;
        else
        {
            GetFontName(systemFont, sysName);
            if (EqualString(name, sysName, false, true))
                num = systemFont;
        }
    }
    DocSetTextStyle(num, size);
}

typedef struct {
    short item;          /* DITL item */
    short menuID;
    MenuHandle menu;
    short sel;           /* current item, 1-based */
} PopUp;

static PopUp gPops[4] = {
    { kPrefsFontPop,  kPopFontMenuID,  NULL, 1 },
    { kPrefsSizePop,  kPopSizeMenuID,  NULL, 1 },
    { kPrefsLEPop,    kPopLEMenuID,    NULL, 1 },
    { kPrefsStylePop, kPopStyleMenuID, NULL, 1 },
};
static const short kSizes[] = { 9, 10, 12, 14, 18 };
#define kNumSizes ((short)(sizeof(kSizes) / sizeof(kSizes[0])))

static PopUp *FindPop(short item)
{
    short i;
    for (i = 0; i < 4; i++)
        if (gPops[i].item == item)
            return &gPops[i];
    return NULL;
}

/* Sizes the current font has as a bitmap are outlined, like a Size
   menu: those look best on screen. */
static void MarkRealSizes(void)
{
    short i;
    for (i = 0; i < kNumSizes; i++)
        SetItemStyle(gPops[1].menu, i + 1,
                     RealFont(MdBaseFont(), kSizes[i]) ? outline : 0);
}

/* Add items by text alone: AppendMenu would read "(" in "(CR)" as
   "disable this item" (and ; ^ ! < / as other codes). */
static void AppendItems(MenuHandle m, const unsigned char *const *items, short n)
{
    short i, base = CountMItems(m);
    for (i = 0; i < n; i++)
    {
        AppendMenu(m, "\px");
        SetMenuItemText(m, base + i + 1, items[i]);
    }
}

static const unsigned char *const kLEItems[] = {
    "\pMac line endings (CR)",       /* kLE_CR */
    "\pUnix line endings (LF)",      /* kLE_LF */
    "\pWindows line endings (CR+LF)" /* kLE_CRLF */
};
static const unsigned char *const kStyleItems[] = {
    "\pAlways", "\pIn files under 16K", "\pIn files under 8K",
    "\pIn files under 4K", "\pNever"   /* kPrefsStyle* order */
};

static void BuildPopUps(void)
{
    Str255 name, cur;
    short size, i, n;
    MenuHandle m;

    /* Font: every installed font. Show the one in use (the system
       font if the saved one is gone). */
    m = gPops[0].menu = NewMenu(kPopFontMenuID, "\p");
    AppendResMenu(m, 'FONT');
    GetFontName(MdBaseFont(), cur);
    if (cur[0] == 0)                       /* applFont has no name of */
        GetFontName(applFont, cur);        /* its own on some Systems  */
    if (cur[0] == 0)
        PStrCopy(cur, "\pGeneva");
    n = CountMItems(m);
    gPops[0].sel = 1;
    for (i = 1; i <= n; i++)
    {
        GetMenuItemText(m, i, name);
        if (EqualString(name, cur, false, true)) { gPops[0].sel = i; break; }
    }

    m = gPops[1].menu = NewMenu(kPopSizeMenuID, "\p");
    PrefsGetFont(name, &size);
    gPops[1].sel = 1;
    for (i = 0; i < kNumSizes; i++)
    {
        Str15 num;
        const unsigned char *one = num;
        NumToString(kSizes[i], num);
        AppendItems(m, &one, 1);
        if (kSizes[i] == MdBaseSize())
            gPops[1].sel = i + 1;
    }
    MarkRealSizes();

    m = gPops[2].menu = NewMenu(kPopLEMenuID, "\p");
    AppendItems(m, kLEItems, 3);
    gPops[2].sel = (short)PrefsGetNewLineEnding() + 1;   /* kLE_CR = 0 ... */

    m = gPops[3].menu = NewMenu(kPopStyleMenuID, "\p");
    AppendItems(m, kStyleItems, 5);
    gPops[3].sel = PrefsGetStyleLimit() + 1;
}

static void DisposePopUps(void)
{
    short i;
    for (i = 0; i < 4; i++)
        if (gPops[i].menu != NULL)
        {
            DisposeMenu(gPops[i].menu);
            gPops[i].menu = NULL;
        }
}

/* System 7-style pop-up box: framed, drop shadow, current choice and
   a down triangle. */
static pascal void DrawPopUpItem(DialogPtr d, short item)
{
    PopUp *p = FindPop(item);
    short itype;
    Handle ihdl;
    Rect r, box;
    Str255 text;
    short maxW, h;
    PolyHandle tri;

    if (p == NULL || p->menu == NULL)
        return;
    GetDialogItem(d, item, &itype, &ihdl, &r);

    box = r;
    box.right--; box.bottom--;
    EraseRect(&r);
    FrameRect(&box);
    MoveTo(box.left + 2, box.bottom);
    LineTo(box.right, box.bottom);
    LineTo(box.right, box.top + 2);

    GetMenuItemText(p->menu, p->sel, text);
    maxW = (box.right - box.left) - 26;
    while (text[0] > 1 && StringWidth(text) > maxW)
    {
        text[0]--;
        text[text[0]] = 0xC9;    /* ellipsis */
    }
    MoveTo(box.left + 8, box.bottom - 5);
    DrawString(text);

    /* Down triangle, 11 wide. */
    h = box.right - 18;
    tri = OpenPoly();
    MoveTo(h, box.top + 7);
    LineTo(h + 10, box.top + 7);
    LineTo(h + 5, box.top + 12);
    LineTo(h, box.top + 7);
    ClosePoly();
    PaintPoly(tri);
    KillPoly(tri);
}

static void RedrawPopUp(short item)
{
    GrafPtr saved;
    GetPort(&saved);
    SetPort((GrafPtr)gPrefsDlg);
    DrawPopUpItem(gPrefsDlg, item);
    SetPort(saved);
}

/* Track a click in a pop-up; apply and save the new choice. */
static void TrackPopUp(PopUp *p)
{
    short itype, i, n;
    Handle ihdl;
    Rect r;
    Point pt;
    long result;
    short chosen;

    GetDialogItem(gPrefsDlg, p->item, &itype, &ihdl, &r);
    SetPort((GrafPtr)gPrefsDlg);
    pt.v = r.top + 1;
    pt.h = r.left + 1;
    LocalToGlobal(&pt);

    n = CountMItems(p->menu);
    for (i = 1; i <= n; i++)
        CheckItem(p->menu, i, i == p->sel);
    InsertMenu(p->menu, -1);
    result = PopUpMenuSelect(p->menu, pt.v, pt.h, p->sel);
    DeleteMenu(p->menuID);

    chosen = LoWord(result);
    if (HiWord(result) == 0 || chosen == p->sel)
        return;
    p->sel = chosen;
    RedrawPopUp(p->item);

    switch (p->item)
    {
    case kPrefsFontPop:
    case kPrefsSizePop:
    {
        Str255 name;
        GetMenuItemText(gPops[0].menu, gPops[0].sel, name);
        PrefsSetFont(name, kSizes[gPops[1].sel - 1]);
        ApplyFontPref();
        if (p->item == kPrefsFontPop)
            MarkRealSizes();
        break;
    }
    case kPrefsLEPop:
        PrefsSetNewLineEnding((LineEndKind)(chosen - 1));
        break;
    case kPrefsStylePop:
        PrefsSetStyleLimit(chosen - 1);
        DocApplyStyleLimit();
        break;
    }
    PrefsSave();
}

static Boolean PrefsWindowIsFront(void)
{
    return gPrefsDlg != NULL && FrontWindow() == (WindowPtr)gPrefsDlg;
}

static void PrefsWindowClose(void)
{
    if (gPrefsDlg == NULL)
        return;
    DisposeDialog(gPrefsDlg);
    gPrefsDlg = NULL;
    DisposePopUps();
}

static void DoPreferences(void)
{
    short itype, i;
    Handle ihdl;
    Rect ir;

    if (gPrefsDlg != NULL)
    {
        SelectWindow((WindowPtr)gPrefsDlg);
        return;
    }
    gPrefsDlg = GetNewDialog(kPrefsDLOG, NULL, (WindowPtr)-1L);
    if (gPrefsDlg == NULL)
        return;
    GetDialogItem(gPrefsDlg, kPrefsRestoreBox, &itype, &ihdl, &ir);
    SetControlValue((ControlHandle)ihdl, PrefsGetRestoreSession() ? 1 : 0);

    BuildPopUps();
    if (gPopDrawUPP == NULL)
        gPopDrawUPP = NewUserItemProc(DrawPopUpItem);
    for (i = 0; i < 4; i++)
    {
        GetDialogItem(gPrefsDlg, gPops[i].item, &itype, &ihdl, &ir);
        SetDialogItem(gPrefsDlg, gPops[i].item, itype, (Handle)gPopDrawUPP, &ir);
    }

    ShowWindow((WindowPtr)gPrefsDlg);
    SelectWindow((WindowPtr)gPrefsDlg);
}

/* Route events that belong to the Preferences window. Returns true if
   the event was consumed. Command-keys are left for the menus, and a
   click on the window while it's in back just brings it forward (via
   HandleMouse). */
static Boolean HandlePrefsEvent(EventRecord *ev)
{
    DialogPtr hitDlg;
    short item;

    if (gPrefsDlg == NULL)
        return false;
    if ((ev->what == keyDown || ev->what == autoKey) && (ev->modifiers & cmdKey))
        return false;
    if (ev->what == mouseDown)
    {
        WindowPtr w;
        if (FindWindow(ev->where, &w) != inContent || w != (WindowPtr)gPrefsDlg
            || w != FrontWindow())
            return false;
    }
    if (!IsDialogEvent(ev))
        return false;

    if (DialogSelect(ev, &hitDlg, &item) && hitDlg == gPrefsDlg)
    {
        if (item == kPrefsRestoreBox)
        {
            short itype;
            Handle ihdl;
            Rect ir;
            ControlHandle box;
            GetDialogItem(gPrefsDlg, kPrefsRestoreBox, &itype, &ihdl, &ir);
            box = (ControlHandle)ihdl;
            SetControlValue(box, 1 - GetControlValue(box));
            PrefsSetRestoreSession(GetControlValue(box) != 0);
            PrefsSave();
        }
        else if (FindPop(item) != NULL)
        {
            TrackPopUp(FindPop(item));
        }
    }
    return true;
}

/* System 6 launch-with-files: open every dropped file. DocOpen owns
   the wdRefNum internally so it can't be recycled later.

   We walk the Finder's AppParmHandle ourselves instead of calling the
   Multiversal CountAppFiles/GetAppFiles glue, which has two bugs:
   CountAppFiles returns WITHOUT writing *count when the handle is
   empty or NULL (normal for an Apple-Event-aware app under System 7,
   which gets 'odoc' instead), leaving the loop bound as stack garbage
   so we'd "open" hundreds of junk files at launch; and GetAppFiles
   never advances past the first entry, so asking for file #2 hangs
   forever. Layout: short message, short count, then per file
   { short vRefNum; OSType fType; short versNum; Str fName } padded to
   an even length. */
static void OpenFromFinderLaunch(void)
{
    Handle h = LMGetAppParmHandle();
    long size;
    long off;
    short count, i;

    if (h == NULL || *h == NULL)
        return;
    size = GetHandleSize(h);
    if (size < 4)
        return;

    count = ((short *)*h)[1];
    off = 4;
    for (i = 0; i < count; i++)
    {
        short vRefNum;
        Str63 name;
        unsigned char nameLen;
        long entryLen;

        if (off + 9 > size)
            break;
        nameLen = (unsigned char)(*h)[off + 8];
        entryLen = (9L + nameLen + 1L) & ~1L;
        if (off + 9 + nameLen > size)
            break;

        /* Copy out before DocOpen can allocate and move memory. */
        vRefNum = *(short *)(*h + off);
        if (nameLen > 63)
            nameLen = 63;
        name[0] = nameLen;
        BlockMoveData(*h + off + 9, name + 1, nameLen);
        off += entryLen;

        DocOpen(vRefNum, name);
    }
}

/* ---- Clipboard ----

   TECut/TECopy/TEPaste only talk to TextEdit's private scrap. Nothing
   moved text between it and the desk scrap (and the toolbox's
   TEFromScrap/TEToScrap glue isn't implemented in Multiversal), so
   pasting text copied in another app or a desk accessory pasted
   whatever MacDown itself had copied last instead. Publish after every
   cut/copy, and pull the desk scrap in before a paste whenever its
   scrapCount says someone else has changed it. */

static short   sScrapCount  = 0;
static Boolean sScrapSynced = false;

static void PublishTEScrap(void)
{
    Handle teScrap = LMGetTEScrpHandle();
    long len = LMGetTEScrpLength();

    ZeroScrap();
    if (teScrap != NULL && len > 0)
    {
        HLock(teScrap);
        (void)PutScrap(len, 'TEXT', *teScrap);
        HUnlock(teScrap);
    }
    sScrapCount = InfoScrap()->scrapCount;
    sScrapSynced = true;
}

static void SyncTEScrapFromDesk(void)
{
    Handle teScrap = LMGetTEScrpHandle();
    long off = 0;
    long len;
    short count = InfoScrap()->scrapCount;

    if (sScrapSynced && count == sScrapCount)
        return;
    sScrapCount = count;
    sScrapSynced = true;
    if (teScrap == NULL)
        return;

    len = GetScrap(teScrap, 'TEXT', &off);
    if (len < 0 || len > kMdMaxFileBytes)
        len = 0; /* no text on the clipboard (e.g. a picture) */
    LMSetTEScrpLength((short)len);
}

/* ---- Mouse ---- */

static void HandleMouse(EventRecord *ev)
{
    WindowPtr w;
    short part = FindWindow(ev->where, &w);
    DocState *doc = DocFromWindow(w);
    Boolean isBrowser = BrowserIsWindow(w);

    switch (part)
    {
    case inMenuBar:
        AdjustMenus();
        HandleMenu(MenuSelect(ev->where));
        break;
    case inSysWindow:
        SystemClick(ev, w);
        break;
    case inDrag:
    {
        Rect bounds = qd.screenBits.bounds;
        bounds.top += GetMBarHeight();
        DragWindow(w, ev->where, &bounds);
        break;
    }
    case inGrow:
        if (doc)
        {
            long sz;
            Rect limits;
            SetRect(&limits, 200, 100, qd.screenBits.bounds.right,
                    qd.screenBits.bounds.bottom);
            sz = GrowWindow(w, ev->where, &limits);
            if (sz != 0)
            {
                SizeWindow(w, LoWord(sz), HiWord(sz), true);
                DocResize(doc);
            }
        }
        else if (isBrowser)
        {
            long sz;
            Rect limits;
            SetRect(&limits, 140, 160, qd.screenBits.bounds.right,
                    qd.screenBits.bounds.bottom);
            sz = GrowWindow(w, ev->where, &limits);
            if (sz != 0)
            {
                SizeWindow(w, LoWord(sz), HiWord(sz), true);
                BrowserResize();
            }
        }
        break;
    case inGoAway:
        if (doc && TrackGoAway(w, ev->where))
            DocClose(doc);
        else if (isBrowser && TrackGoAway(w, ev->where))
            BrowserClose();
        else if (gPrefsDlg != NULL && w == (WindowPtr)gPrefsDlg
                 && TrackGoAway(w, ev->where))
            PrefsWindowClose();
        break;
    case inZoomIn:
    case inZoomOut:
        if (doc && TrackBox(w, ev->where, part))
        {
            ZoomWindow(w, part, true);
            DocResize(doc);
        }
        else if (isBrowser && TrackBox(w, ev->where, part))
        {
            ZoomWindow(w, part, true);
            BrowserResize();
        }
        break;
    case inContent:
        if (w != FrontWindow())
        {
            SelectWindow(w);
        }
        else if (doc)
        {
            DocClick(doc, ev);
        }
        else if (isBrowser)
        {
            BrowserClick(ev);
        }
        break;
    }
}

/* ---- Keyboard ---- */

static Boolean IsArrow(char c)
{
    return (c == kLeftArrow || c == kRightArrow ||
            c == kUpArrow || c == kDownArrow);
}

static void InsertMarker(DocState *doc, const char *marker)
{
    short n = (unsigned char)marker[0];
    if (n > 0)
        TEInsert((Ptr)(marker + 1), n, doc->te);
}

static void ResetInsertStyleToPlain(DocState *doc)
{
    short pos = (**doc->te).selStart;
    short crPos;
    TextStyle plain;
    ClipSuppress cs;

    if (pos < 1)
        return;
    crPos = pos - 1;

    plain.tsFont = MdBaseFont();
    plain.tsFace = 0;
    plain.tsSize = MdBaseSize();
    plain.tsColor.red = plain.tsColor.green = plain.tsColor.blue = 0;

    BeginClipSuppress(&cs, doc->window);
    TESetSelect(crPos, pos, doc->te);
    TESetStyle(doFont | doFace | doSize, &plain, false, doc->te);
    TESetSelect(pos, pos, doc->te);
    EndClipSuppress(&cs);
}

static Boolean HandleReturnKey(DocState *doc)
{
    short pos = (**doc->te).selStart;
    char marker[24];
    Boolean isEmpty = false;
    Boolean hasMarker;

    hasMarker = MdNextListMarker(doc->te, pos, marker, &isEmpty);
    if (!hasMarker)
        return false;

    if (isEmpty)
    {
        short lineStart, lineEnd;
        MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
        TESetSelect(lineStart, lineEnd, doc->te);
        TEDelete(doc->te);
        TEKey('\r', doc->te);
        DocMarkDirty(doc);
        DocMarkLineDirty(doc, (**doc->te).selStart);
        return true;
    }

    TEKey('\r', doc->te);
    InsertMarker(doc, marker);
    /* If we just inserted a continuation for an ordered list, renumber
       the subsequent items so the sequence stays correct. Each per-
       line digit-run rewrite via TEDelete/TEInsert moves the TE
       cursor, even when the totals cancel out -- so always restore. */
    {
        short selStart = (**doc->te).selStart;
        short selEnd = (**doc->te).selEnd;
        (void)MdRenumberOrderedList(doc->te, selStart, &selStart, &selEnd);
        TESetSelect(selStart, selEnd, doc->te);
        doc->selAnchor = selStart;
    }
    DocMarkDirty(doc);
    DocMarkLineDirty(doc, (**doc->te).selStart);
    return true;
}

static void HandleKey(EventRecord *ev)
{
    DocState *doc = DocActive();
    char c = ev->message & charCodeMask;
    short keyCode = (ev->message & keyCodeMask) >> 8;
    Boolean shift = (ev->modifiers & shiftKey) != 0;
    Boolean cmd = (ev->modifiers & cmdKey) != 0;
    Boolean option = (ev->modifiers & optionKey) != 0;
    short pos;

    /* Normalize arrow keys via their virtual keycode (the Plus
       keypad digits 2/4/6/8 and the Extended Keyboard arrow keys).
       Shift+arrow on the Plus arrives as a different physical key
       (the keypad operator row) and we don't try to detect that
       case here -- text selection by Shift+arrow on a Mac Plus just
       won't work; use the mouse or Shift+click instead. */
    switch (keyCode)
    {
    case kKC_Ext_Left:
    case kKC_KP_Left:
        c = kLeftArrow;
        break;
    case kKC_Ext_Right:
    case kKC_KP_Right:
        c = kRightArrow;
        break;
    case kKC_Ext_Up:
    case kKC_KP_Up:
        c = kUpArrow;
        break;
    case kKC_Ext_Down:
    case kKC_KP_Down:
        c = kDownArrow;
        break;
    }

    /* Cmd-` cycles to the next window. Done before MenuKey because
       the backtick isn't a real menu key equivalent. */
    if (cmd && (c == '`' || c == '~'))
    {
        DocCycleWindow();
        return;
    }

    if (cmd && (c == 'w' || c == 'W') && PrefsWindowIsFront())
    {
        PrefsWindowClose();
        return;
    }

    if (cmd)
    {
        AdjustMenus();
        {
            long mr = MenuKey(c);
            if (HiWord(mr) != 0)
            {
                HandleMenu(mr);
                return;
            }
        }
    }

    if (doc == NULL)
        return;

    if (option && !cmd)
    {
        if (c == kUpArrow)
        {
            DocMoveLineUp(doc);
            return;
        }
        if (c == kDownArrow)
        {
            DocMoveLineDown(doc);
            return;
        }
    }

    if (option && !cmd && c == 0x08)
    {
        DocBeforeAction(doc);
        DoDeleteWordBack(doc);
        return;
    }

    if (cmd && !option && c == 0x08)
    {
        DocBeforeAction(doc);
        DoDeleteLineBack(doc);
        return;
    }

    if (cmd && !option && !shift)
    {
        if (c == '[')
        {
            DocOutdentLine(doc);
            return;
        }
        if (c == ']')
        {
            DocIndentLine(doc);
            return;
        }
    }

    pos = (**doc->te).selStart;

    if (IsArrow(c))
    {
        short newCursor;
        short curEnd = (**doc->te).selEnd;
        short startFrom;
        DocBreakTypingRun(doc);
        if (shift)
        {
            startFrom = (pos == doc->selAnchor) ? curEnd : pos;
        }
        else
        {
            if (pos != curEnd)
                startFrom = (c == kLeftArrow || c == kUpArrow) ? pos : curEnd;
            else
                startFrom = pos;
        }

        if (cmd)
        {
            if (c == kLeftArrow)
                newCursor = DocLineStartOffset(doc, startFrom);
            else if (c == kRightArrow)
                newCursor = DocLineEndOffset(doc, startFrom);
            else if (c == kUpArrow)
                newCursor = 0;
            else
                newCursor = (**doc->te).teLength;
        }
        else if (option)
        {
            if (c == kLeftArrow)
                newCursor = DocOffsetWordLeft(doc, startFrom);
            else if (c == kRightArrow)
                newCursor = DocOffsetWordRight(doc, startFrom);
            else
                newCursor = startFrom;
        }
        else
        {
            switch (c)
            {
            case kLeftArrow:
                newCursor = DocOffsetLeft(doc, startFrom);
                break;
            case kRightArrow:
                newCursor = DocOffsetRight(doc, startFrom);
                break;
            case kUpArrow:
                newCursor = DocOffsetUp(doc, startFrom);
                break;
            case kDownArrow:
                newCursor = DocOffsetDown(doc, startFrom);
                break;
            default:
                newCursor = startFrom;
                break;
            }
        }

        DocMoveCursorTo(doc, newCursor, shift);
        return;
    }

    if (cmd)
    {
        if (c == 'l' || c == 'L')
        {
            DoToggleTask(doc);
            return;
        }
        {
            long mr = MenuKey(c);
            if (HiWord(mr) != 0)
                HandleMenu(mr);
        }
        return;
    }

    /* Smart-surround: typing one of *, _, (, or [ with text selected
       wraps the selection in the matching pair instead of replacing
       it. The original text stays selected so the user can keep
       chaining wraps (e.g. ** then _ for bold-italic) without
       re-selecting. */
    {
        char openCh = 0, closeCh = 0;
        switch (c)
        {
        case '*':
            openCh = '*';
            closeCh = '*';
            break;
        case '_':
            openCh = '_';
            closeCh = '_';
            break;
        case '`':
            openCh = '`';
            closeCh = '`';
            break;
        case '(':
            openCh = '(';
            closeCh = ')';
            break;
        case '[':
            openCh = '[';
            closeCh = ']';
            break;
        }
        if (openCh != 0)
        {
            short selStart = (**doc->te).selStart;
            short selEnd = (**doc->te).selEnd;
            if (selStart != selEnd)
            {
                DocBeforeAction(doc);
                /* Insert closer at the high end first -- its position
                   isn't perturbed by the later insertion at selStart. */
                TESetSelect(selEnd, selEnd, doc->te);
                TEKey(closeCh, doc->te);
                TESetSelect(selStart, selStart, doc->te);
                TEKey(openCh, doc->te);
                TESetSelect(selStart + 1, selEnd + 1, doc->te);
                doc->selAnchor = selStart + 1;
                DocCommitEdit(doc,
                              DocLineStartOffset(doc, selStart),
                              DocLineEndOffset(doc, selEnd + 1));
                return;
            }
        }
    }

    /* TextEdit's record falls apart past 32K. Anything other than a
       delete that wouldn't shrink the text gets refused at the limit. */
    if (c != 0x08 && c != 0x7F &&
        (**doc->te).selStart == (**doc->te).selEnd &&
        (**doc->te).teLength >= kMdMaxFileBytes)
    {
        SysBeep(1);
        return;
    }

    if (c == '\r')
    {
        DocBeforeAction(doc);
        if (HandleReturnKey(doc))
        {
            ResetInsertStyleToPlain(doc);
            doc->selAnchor = (**doc->te).selStart;
            return;
        }
    }
    else
    {
        DocBeforeTyping(doc);
    }

    TEKey(c, doc->te);
    if (c == '\r')
        ResetInsertStyleToPlain(doc);
    doc->selAnchor = (**doc->te).selStart;
    DocMarkDirty(doc);

    /* Skip the dirty mark (and the eventual restyle pass) when this
       keystroke can't possibly change the line's styling. Conditions:
       - typed char is a plain alphanumeric or high-bit byte (not a
         markdown marker like *, _, #, -, >, [, ], `, digit-at-line-
         start, space, period, etc.)
       - cursor is past the leading marker zone (col > 7), so it can't
         affect heading/list classification
       - no * or _ within 2 chars of the cursor on either side, so the
         emphasis flanking rules can't have shifted
       TE auto-inherits the run style at the insertion point, so the
       new glyph paints correctly without any restyle work. */
    {
        unsigned char uc = (unsigned char)c;
        Boolean isPlainText =
            (uc >= 'a' && uc <= 'z') ||
            (uc >= 'A' && uc <= 'Z') ||
            (uc >= '0' && uc <= '9') ||
            uc >= 0x80;
        Boolean needMark = true;

        if (isPlainText)
        {
            short pos = (**doc->te).selStart;
            short lineStart, lineEnd;
            MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
            if (pos - lineStart > 7)
            {
                CharsHandle ch = TEGetText(doc->te);
                char *t;
                short scanStart, scanEnd, j;
                Boolean nearMarker = false;
                HLock((Handle)ch);
                t = *ch;
                scanStart = pos - 2;
                if (scanStart < lineStart)
                    scanStart = lineStart;
                scanEnd = pos + 2;
                if (scanEnd > lineEnd)
                    scanEnd = lineEnd;
                for (j = scanStart; j < scanEnd; j++)
                {
                    if (t[j] == '*' || t[j] == '_')
                    {
                        nearMarker = true;
                        break;
                    }
                }
                HUnlock((Handle)ch);
                if (!nearMarker)
                    needMark = false;
            }
        }

        if (needMark)
            DocMarkLineDirty(doc, (**doc->te).selStart);
    }

    DocAdjustScrollbar(doc);
    /* End the typing burst at word boundaries so the next keystroke
       takes a fresh undo snapshot. Without this, fast continuous
       typing across multiple words counts as one burst and Cmd-Z
       wipes them all. */
    if (c == ' ' || c == '\t')
        doc->inTypingRun = false;
}

/* ---- Cmd-L / delete-word implementations ---- */

static void ForceRestyleRangeFor(DocState *doc, short start, short end)
{
    doc->dirtyLineStart = start;
    doc->dirtyLineEnd = end;
    doc->lastDirtyTick = TickCount() - 1000;
    DocFlushRestyle(doc);
}

static void DoDeleteWordBack(DocState *doc)
{
    short curPos = (**doc->te).selStart;
    short curEnd = (**doc->te).selEnd;
    short newCur;

    if (curPos != curEnd)
    {
        TEDelete(doc->te);
    }
    else
    {
        short wordLeft = DocOffsetWordLeft(doc, curPos);
        if (wordLeft >= curPos)
            return;
        TESetSelect(wordLeft, curPos, doc->te);
        TEDelete(doc->te);
    }
    doc->selAnchor = (**doc->te).selStart;
    DocMarkDirty(doc);
    newCur = (**doc->te).selStart;
    ForceRestyleRangeFor(doc, DocLineStartOffset(doc, newCur), DocLineEndOffset(doc, newCur));
    DocAdjustScrollbar(doc);
}

/* Cmd-Delete: kill from cursor back to line start. If already at the
   start of the line, fall back to a single-char backspace so repeated
   presses keep nibbling across line breaks. */
static void DoDeleteLineBack(DocState *doc)
{
    short curPos = (**doc->te).selStart;
    short curEnd = (**doc->te).selEnd;
    short lineStart;
    short newCur;

    if (curPos != curEnd)
    {
        TEDelete(doc->te);
    }
    else
    {
        lineStart = DocLineStartOffset(doc, curPos);
        if (curPos > lineStart)
        {
            TESetSelect(lineStart, curPos, doc->te);
            TEDelete(doc->te);
        }
        else
        {
            if (curPos == 0)
                return;
            TESetSelect(curPos - 1, curPos, doc->te);
            TEDelete(doc->te);
        }
    }
    doc->selAnchor = (**doc->te).selStart;
    DocMarkDirty(doc);
    newCur = (**doc->te).selStart;
    ForceRestyleRangeFor(doc, DocLineStartOffset(doc, newCur), DocLineEndOffset(doc, newCur));
    DocAdjustScrollbar(doc);
}

static void DoToggleTask(DocState *doc)
{
    short pos;
    short lineStart, lineEnd, lineLen;
    short leading;      /* count of all leading whitespace bytes */
    short contentStart; /* lineStart + leading */
    short contentLen;
    CharsHandle ch;
    char *text;
    MdLineKind kind;

    DocBeforeAction(doc);
    pos = (**doc->te).selStart;
    MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
    lineLen = lineEnd - lineStart;

    ch = TEGetText(doc->te);
    HLock((Handle)ch);
    text = *ch + lineStart;

    /* Count every leading tab and space, not just up to 3 spaces.
       Classify the line as if it started after the indent so nested
       (deeply-indented or tab-indented) list/task lines are still
       recognized. All edits happen at offsets relative to the indent. */
    leading = 0;
    while (leading < lineLen &&
           (text[leading] == ' ' || text[leading] == '\t'))
        leading++;
    contentStart = lineStart + leading;
    contentLen = lineLen - leading;
    kind = MdClassifyLine(text + leading, contentLen);
    HUnlock((Handle)ch);

    if (kind == kLine_TaskUnchecked || kind == kLine_TaskChecked)
    {
        /* Layout from contentStart: "- [<state>] ..." -> state at +3. */
        short boxPos = contentStart + 3;
        short savedStart, savedEnd;
        char current, replacement;

        savedStart = (**doc->te).selStart;
        savedEnd = (**doc->te).selEnd;

        ch = TEGetText(doc->te);
        HLock((Handle)ch);
        current = (*ch)[boxPos];
        HUnlock((Handle)ch);

        replacement = (current == ' ') ? 'x' : ' ';

        TESetSelect(boxPos, boxPos + 1, doc->te);
        TEDelete(doc->te);
        TEKey(replacement, doc->te);

        TESetSelect(savedStart, savedEnd, doc->te);
        DocMarkDirty(doc);
        DocMarkLineDirty(doc, boxPos);
        return;
    }

    if (kind == kLine_UnorderedItem)
    {
        short insertPos = contentStart + 2; /* after "- " marker */
        short newSel = (pos >= insertPos) ? pos + 4 : pos;
        TESetSelect(insertPos, insertPos, doc->te);
        TEInsert((Ptr) "[ ] ", 4, doc->te);
        TESetSelect(newSel, newSel, doc->te);
        DocMarkDirty(doc);
        ForceRestyleRangeFor(doc, lineStart, lineEnd + 4);
        DocAdjustScrollbar(doc);
        return;
    }

    {
        /* Insert the full "- [ ] " prefix AFTER any indent, not at
           lineStart, so an indented plain line keeps its indentation. */
        short newSel = (pos >= contentStart) ? pos + 6 : pos;
        TESetSelect(contentStart, contentStart, doc->te);
        TEInsert((Ptr) "- [ ] ", 6, doc->te);
        TESetSelect(newSel, newSel, doc->te);
        DocMarkDirty(doc);
        ForceRestyleRangeFor(doc, lineStart, lineEnd + 6);
        DocAdjustScrollbar(doc);
    }
}

/* Insert a Markdown link at the cursor. No selection: "[]()" with the
   caret between the brackets so the user types the link text first.
   With a selection: wrap as "[selection]()" and put the caret between
   the parens so the user types the URL. */
static void DoInsertLink(DocState *doc)
{
    short selStart = (**doc->te).selStart;
    short selEnd = (**doc->te).selEnd;
    short caret;

    DocBeforeAction(doc);

    if (selStart == selEnd)
    {
        /* Empty selection: insert the full four chars. */
        TEKey('[', doc->te);
        TEKey(']', doc->te);
        TEKey('(', doc->te);
        TEKey(')', doc->te);
        caret = selStart + 1; /* between [ and ] */
    }
    else
    {
        /* Wrap the selection. Insert at the high end first so the
           low-end insertion's index doesn't shift. */
        TESetSelect(selEnd, selEnd, doc->te);
        TEKey(']', doc->te);
        TEKey('(', doc->te);
        TEKey(')', doc->te);
        TESetSelect(selStart, selStart, doc->te);
        TEKey('[', doc->te);
        /* Final layout: [ sel ] ( ) — caret between ( and ) */
        caret = selEnd + 3;
    }

    TESetSelect(caret, caret, doc->te);
    doc->selAnchor = caret;
    DocMarkDirty(doc);
    DocMarkLineDirty(doc, caret);
    DocAdjustScrollbar(doc);
}

/* ---- Menu handling ---- */

static void DrawAboutContent(WindowPtr w)
{
    Rect r = w->portRect;
    EraseRect(&r);

    TextFont(0); /* system (Chicago) */
    TextSize(12);
    TextFace(bold);
    MoveTo(24, 32);
    DrawString("\pMacDown");

    TextFace(0);
    MoveTo(24, 54);
    /* Build a Pascal string from MACDOWN_VERSION_STR (a C literal
       supplied by CMake) so the About box matches /VERSION. */
    {
        Str255 vline;
        const char *src = "Version " MACDOWN_VERSION_STR;
        short n = 0;
        while (src[n] != 0 && n < 255)
        {
            vline[n + 1] = src[n];
            n++;
        }
        vline[0] = n;
        DrawString(vline);
    }

    MoveTo(24, 76);
    DrawString("\pBy Rockwell Schrock (@schrockwell)");
}

static void DrawShortcutRow(short y, ConstStr255Param label, ConstStr255Param shortcut)
{
    MoveTo(20, y);
    DrawString(label);
    MoveTo(180, y);
    DrawString(shortcut);
}

static void DrawShortcutsContent(WindowPtr w)
{
    Rect r = w->portRect;
    short y;
    EraseRect(&r);

    TextFont(0); /* system (Chicago) */
    TextSize(12);
    MoveTo(20, 28);
    DrawString("\pKeyboard Shortcuts");

    /* Geneva 9 for the table -- wee bit of room for more rows. */
    TextFont(3); /* Geneva */
    TextSize(9);

    y = 56;
    DrawShortcutRow(y, "\pJump Word", "\pOption + Left / Right");
    y += 16;
    DrawShortcutRow(y, "\pDelete Word", "\pOption + Delete");
    y += 16;
    DrawShortcutRow(y, "\pJump To Start / End of Line", "\pCommand + Left / Right");
    y += 16;
    DrawShortcutRow(y, "\pDelete Line", "\pCommand + Delete");
    y += 16;
    DrawShortcutRow(y, "\pMove Line", "\pOption + Up / Down");
    y += 16;
    DrawShortcutRow(y, "\pText Selection", "\pHold Shift");
}

/* Show a plain-bordered window and pump events until any mouse/key
   dismisses it. Other windows' update events are forwarded so they
   keep painting if the modal moves or uncovers them. */
static void ShowModalSplash(short windID, void (*draw)(WindowPtr))
{
    WindowPtr w;
    GrafPtr savedPort;
    EventRecord ev;
    Boolean done = false;

    InitCursor();
    w = GetNewWindow(windID, NULL, (WindowPtr)-1L);
    if (w == NULL)
        return;

    GetPort(&savedPort);
    SetPort(w);
    draw(w);

    while (!done)
    {
        if (WaitNextEvent(everyEvent, &ev, 6, NULL))
        {
            switch (ev.what)
            {
            case mouseDown:
            case keyDown:
            case autoKey:
                done = true;
                break;
            case updateEvt:
            {
                WindowPtr uw = (WindowPtr)ev.message;
                if (uw == w)
                {
                    SetPort(w);
                    BeginUpdate(w);
                    draw(w);
                    EndUpdate(w);
                }
                else
                {
                    DocState *d = DocFromWindow(uw);
                    if (d)
                        DocUpdate(d);
                }
                break;
            }
            }
        }
    }

    DisposeWindow(w);
    SetPort(savedPort);
}

static void DoAbout(void) { ShowModalSplash(129, DrawAboutContent); }
static void DoShortcuts(void) { ShowModalSplash(130, DrawShortcutsContent); }

static void DoFileOpen(void)
{
    SFReply reply;
    SFTypeList types;
    Point where;
    GrafPtr savedPort;
    short i;

    {
        char *p = (char *)&reply;
        for (i = 0; i < (short)sizeof(SFReply); i++)
            p[i] = 0;
    }

    types[0] = 'TEXT';

    GetPort(&savedPort);
    InitCursor();

    /* Start in the folder a file was last opened from (this session or
       a previous one). SFGetFile reads its starting location from the
       SFSaveDisk (negated vRefNum) and CurDirStore (dirID) globals. If
       that folder is gone, leave them alone and let Standard File use
       its own default. */
    {
        short vRef;
        long  dirID;
        if (PrefsGetOpenFolder(&vRef, &dirID))
        {
            LMSetSFSaveDisk(-vRef);
            LMSetCurDirStore(dirID);
        }
    }

    where.h = 80;
    where.v = 60;
    SFGetFile(where, "\p", NULL, 1, types, NULL, &reply);

    SetPort(savedPort);
    {
        EventRecord ev;
        EventAvail(0, &ev);
    }

    if (!reply.good)
        return;
    {
        short vRef;
        long  dirID;
        if (FileIOResolveWD(reply.vRefNum, &vRef, &dirID) == noErr)
            PrefsSetOpenFolder(vRef, dirID);
    }
    DocOpen(reply.vRefNum, reply.fName);
}

static void HandleQuit(void)
{
    if (DocCloseAllForQuit())
        gQuitRequested = true;
}

static void AdjustMenus(void)
{
    MenuHandle fmt = GetMenuHandle(kFormatMenuID);
    DocState *doc = DocActive();
    short i;

    SyncFileMenuEnables();

    /* Check the front document's line endings; nothing to check (or
       change) when the front window isn't a document. */
    for (i = kFormatLEFirst; i <= kFormatLELast; i++)
    {
        CheckItem(fmt, i, doc != NULL && doc->leKind == i - kFormatLEFirst);
        if (doc != NULL)
            EnableItem(fmt, i);
        else
            DisableItem(fmt, i);
    }
}

static void HandleMenu(long mResult)
{
    short menuID = HiWord(mResult);
    short item = LoWord(mResult);
    DocState *doc = DocActive();

    switch (menuID)
    {
    case kAppleMenuID:
        if (item == kAboutItem)
        {
            DoAbout();
        }
        else if (item == kShortcutsItem)
        {
            DoShortcuts();
        }
        else
        {
            Str255 daName;
            GetMenuItemText(GetMenuHandle(kAppleMenuID), item, daName);
            OpenDeskAcc(daName);
        }
        break;

    case kFileMenuID:
        switch (item)
        {
        case kFileNew:
            DocNew();
            break;
        case kFileOpen:
            DoFileOpen();
            break;
        case kFileClose:
            if (doc)
                DocClose(doc);
            break;
        case kFileCloseAll:
            DocCloseAll();
            break;
        case kFileSave:
            if (doc)
                DocSave(doc);
            break;
        case kFileSaveAs:
            if (doc)
                DocSaveAs(doc);
            break;
        case kFileQuit:
            HandleQuit();
            break;
        }
        break;

    case kEditMenuID:
        if (item == kEditPreferences)
        {
            DoPreferences();
            break;
        }
        if (SystemEdit(item - 1))
            break;
        if (doc == NULL)
            break;
        switch (item)
        {
        case kEditUndo:
            DocUndo(doc);
            break;
        case kEditCut:
            DocBeforeAction(doc);
            TECut(doc->te);
            PublishTEScrap();
            DocMarkDirty(doc);
            DocMarkLineDirty(doc, (**doc->te).selStart);
            break;
        case kEditCopy:
            TECopy(doc->te);
            PublishTEScrap();
            break;
        case kEditPaste:
        {
            short pasteStart = (**doc->te).selStart;
            short pasteEnd;
            SyncTEScrapFromDesk();
            /* TextEdit tops out at 32K; refuse a paste that would
               overflow it rather than corrupting the record. */
            if ((long)(**doc->te).teLength
                    - ((**doc->te).selEnd - (**doc->te).selStart)
                    + (long)LMGetTEScrpLength() > kMdMaxFileBytes)
            {
                SysBeep(1);
                break;
            }
            DocBeforeAction(doc);
            TEPaste(doc->te);
            pasteEnd = (**doc->te).selStart;
            DocMarkDirty(doc);
            ForceRestyleRangeFor(doc, pasteStart, pasteEnd);
        }
        break;
        case kEditClear:
            DocBeforeAction(doc);
            TEDelete(doc->te);
            DocMarkDirty(doc);
            DocMarkLineDirty(doc, (**doc->te).selStart);
            break;
        case kEditSelAll:
            DocBreakTypingRun(doc);
            TESetSelect(0, 32767, doc->te);
            break;
        case kEditDuplicate:
            DocDuplicateLine(doc);
            break;
        }
        if (doc)
            DocAdjustScrollbar(doc);
        break;

    case kFormatMenuID:
        if (doc == NULL)
            break;
        switch (item)
        {
        case kFormatBold:
            DocWrapPair(doc, '*', 2);
            break;
        case kFormatItalic:
            DocWrapPair(doc, '*', 1);
            break;
        case kFormatCode:
            DocWrapPair(doc, '`', 1);
            break;
        case kFormatInsertLink:
            DoInsertLink(doc);
            break;
        default:
            if (item >= kFormatLEFirst && item <= kFormatLELast)
                DocSetLineEnding(doc, (LineEndKind)(item - kFormatLEFirst));
            break;
        }
        break;

    case kLineMenuID:
        if (doc == NULL)
            break;
        switch (item)
        {
        case kLineH1:
            DocToggleHeading(doc, 1);
            break;
        case kLineH2:
            DocToggleHeading(doc, 2);
            break;
        case kLineH3:
            DocToggleHeading(doc, 3);
            break;
        case kLineH4:
            DocToggleHeading(doc, 4);
            break;
        case kLineH5:
            DocToggleHeading(doc, 5);
            break;
        case kLineH6:
            DocToggleHeading(doc, 6);
            break;
        case kLineNoHeading:
            DocToggleHeading(doc, 0);
            break;
        case kLineToggleTask:
            DoToggleTask(doc);
            break;
        case kLineBlockquote:
            DocToggleBlockquote(doc);
            break;
        case kLineIndent:
            DocIndentLine(doc);
            break;
        case kLineOutdent:
            DocOutdentLine(doc);
            break;
        }
        break;

    case kRecentMenuID:
        DocOpenRecent(item);
        break;

    case kWindowsMenuID:
        if (item == kWindowsMenuNext)
            DocCycleWindow();
        else if (item == kWindowsMenuBrowser)
            BrowserToggle();
        else if (item > kWindowsMenuStaticItems)
        {
            DocSelectFromMenu(item - kWindowsMenuStaticItems);
        }
        break;
    }
    HiliteMenu(0);
}

/* ---- Main loop ---- */

int main(void)
{
    EventRecord event;
    long sleepTicks = 6L;

    Initialize();

    /* On launch, either Finder dropped docs on us (System 6 path) or
       we got an 'odoc' Apple Event. If neither, spawn an untitled doc
       so the user has something to type into. */
    RestoreSession();
    OpenFromFinderLaunch();
    if (gDocs == NULL)
        DocNew();

    while (!gQuitRequested)
    {
        if (WaitNextEvent(everyEvent, &event, sleepTicks, NULL)
            && !HandlePrefsEvent(&event))
        {
            switch (event.what)
            {
            case mouseDown:
                HandleMouse(&event);
                break;
            case keyDown:
            case autoKey:
                HandleKey(&event);
                break;
            case updateEvt:
            {
                WindowPtr uw = (WindowPtr)event.message;
                DocState *d = DocFromWindow(uw);
                if (d)
                    DocUpdate(d);
                else if (BrowserIsWindow(uw))
                    BrowserUpdate();
                break;
            }
            case activateEvt:
            {
                WindowPtr aw = (WindowPtr)event.message;
                DocState *d = DocFromWindow(aw);
                if (d)
                    DocActivate(d, (event.modifiers & activeFlag) != 0);
                else if (BrowserIsWindow(aw))
                    BrowserActivate((event.modifiers & activeFlag) != 0);
                break;
            }
            case kHighLevelEvent:
                AEDispatch(&event);
                break;
            case osEvt:
                if ((event.message >> 24) == 0x01)
                {
                    DocState *d = DocActive();
                    if (d)
                        DocActivate(d, (event.message & 0x01) != 0);
                    else if (BrowserIsWindow(FrontWindow()))
                        BrowserActivate((event.message & 0x01) != 0);
                }
                break;
            }
        }

        /* Idle work: TE caret blink + restyle for the active doc, and
           a throttled directory-mod-date poll for the browser so it
           auto-refreshes when files are added/removed by anyone. */
        {
            DocState *d = DocActive();
            if (d)
            {
                TEIdle(d->te);
                if (d->dirtyLineStart >= 0 &&
                    (TickCount() - d->lastDirtyTick) > kRestyleIdleTicks)
                {
                    DocFlushRestyle(d);
                    DocAdjustScrollbar(d);
                }
            }
            BrowserIdle();
            DocAdjustCursor();
        }
    }

    /* Remember where the browser was, then write prefs (browser and
       Open-dialog folders, Open Recent list). */
    {
        short vRef;
        long  dirID;
        Boolean browserVisible;
        Rect browserBounds;
        if (BrowserLastFolder(&vRef, &dirID))
            PrefsSetBrowserFolder(vRef, dirID);
        if (BrowserGetWindowState(&browserVisible, &browserBounds))
            PrefsSetBrowserWindow(browserVisible, &browserBounds);
        else
            PrefsSetBrowserWindow(browserVisible, NULL);
    }
    PrefsSave();

    return 0;
}
