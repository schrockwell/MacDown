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

#define kMenuBarID     128
#define kAppleMenuID   128
#define kFileMenuID    129
#define kEditMenuID    130
#define kFormatMenuID  131
#define kWindowsMenuID 132
#define kLineMenuID    133

#define kWindowsMenuBrowser  1
#define kWindowsMenuNext     2
#define kWindowsMenuStaticItems 3

#define kFileNew      1
#define kFileOpen     2
#define kFileClose    4
#define kFileCloseAll 5
#define kFileSave     6
#define kFileSaveAs   7
#define kFileQuit     9

#define kEditUndo      1
#define kEditCut       3
#define kEditCopy      4
#define kEditPaste     5
#define kEditClear     6
#define kEditSelAll    7
#define kEditDuplicate 9

#define kFormatBold       1
#define kFormatItalic     2
#define kFormatCode       3
#define kFormatInsertLink 5

#define kLineH1          1
#define kLineH2          2
#define kLineH3          3
#define kLineH4          4
#define kLineH5          5
#define kLineH6          6
#define kLineNoHeading   7
#define kLineToggleTask  9
#define kLineBlockquote  10
#define kLineIndent      12
#define kLineOutdent     13

#define kAboutItem      1
#define kShortcutsItem  2
#define kRestyleIdleTicks 6L

#define kLeftArrow  0x1C
#define kRightArrow 0x1D
#define kUpArrow    0x1E
#define kDownArrow  0x1F

/* Virtual key codes. The arrow keys on the Apple Extended Keyboard
   and the keypad arrows on the Mac Plus produce different char codes
   when Shift is held -- the Plus keypad flips into num-lock-style
   mode and emits '+' / '*' / '/' / '=' instead of the arrow chars.
   Detect by physical keycode instead, which Shift can't change. */
#define kKC_KP_Left   0x56   /* Plus keypad 4 */
#define kKC_KP_Right  0x58   /* Plus keypad 6 */
#define kKC_KP_Down   0x54   /* Plus keypad 2 */
#define kKC_KP_Up     0x5B   /* Plus keypad 8 */
#define kKC_Ext_Left  0x7B   /* Extended keyboard left */
#define kKC_Ext_Right 0x7C   /* Extended keyboard right */
#define kKC_Ext_Down  0x7D   /* Extended keyboard down */
#define kKC_Ext_Up    0x7E   /* Extended keyboard up */


#define UC(c) ((unsigned char)(c))

static void Initialize(void);
static void OpenFromFinderLaunch(void);
static void HandleMouse(EventRecord *ev);
static void HandleKey(EventRecord *ev);
static void HandleMenu(long mResult);
static void AdjustMenus(void);
static void DoAbout(void);
static Boolean HandleReturnKey(DocState *doc);
static void DoToggleTask(DocState *doc);
static void DoDeleteWordBack(DocState *doc);
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
    DrawMenuBar();

    AEInit();
    DocAppInit();
}

/* System 6 launch-with-files: open every dropped file. DocOpen owns
   the wdRefNum internally so it can't be recycled later. */
static void OpenFromFinderLaunch(void)
{
    short msg, count, i;
    AppFile af;
    CountAppFiles(&msg, &count);
    for (i = 1; i <= count; i++) {
        GetAppFiles(i, &af);
        DocOpen(af.vRefNum, af.fName);
        ClrAppFiles(i);
    }
}

/* ---- Mouse ---- */

static void HandleMouse(EventRecord *ev)
{
    WindowPtr w;
    short part = FindWindow(ev->where, &w);
    DocState *doc = DocFromWindow(w);
    Boolean isBrowser = BrowserIsWindow(w);

    switch (part) {
        case inMenuBar:
            AdjustMenus();
            HandleMenu(MenuSelect(ev->where));
            break;
        case inSysWindow:
            SystemClick(ev, w);
            break;
        case inDrag: {
            Rect bounds = qd.screenBits.bounds;
            bounds.top += GetMBarHeight();
            DragWindow(w, ev->where, &bounds);
            break;
        }
        case inGrow:
            if (doc) {
                long sz;
                Rect limits;
                SetRect(&limits, 200, 100, qd.screenBits.bounds.right,
                        qd.screenBits.bounds.bottom);
                sz = GrowWindow(w, ev->where, &limits);
                if (sz != 0) {
                    SizeWindow(w, LoWord(sz), HiWord(sz), true);
                    DocResize(doc);
                }
            } else if (isBrowser) {
                long sz;
                Rect limits;
                SetRect(&limits, 140, 160, qd.screenBits.bounds.right,
                        qd.screenBits.bounds.bottom);
                sz = GrowWindow(w, ev->where, &limits);
                if (sz != 0) {
                    SizeWindow(w, LoWord(sz), HiWord(sz), true);
                    BrowserResize();
                }
            }
            break;
        case inGoAway:
            if (doc && TrackGoAway(w, ev->where)) DocClose(doc);
            else if (isBrowser && TrackGoAway(w, ev->where)) BrowserClose();
            break;
        case inZoomIn:
        case inZoomOut:
            if (doc && TrackBox(w, ev->where, part)) {
                ZoomWindow(w, part, true);
                DocResize(doc);
            } else if (isBrowser && TrackBox(w, ev->where, part)) {
                ZoomWindow(w, part, true);
                BrowserResize();
            }
            break;
        case inContent:
            if (w != FrontWindow()) {
                SelectWindow(w);
            } else if (doc) {
                DocClick(doc, ev);
            } else if (isBrowser) {
                BrowserClick(ev);
            }
            break;
    }
}

/* ---- Keyboard ---- */

static Boolean IsArrow(char c)
{
    return (c == kLeftArrow || c == kRightArrow ||
            c == kUpArrow   || c == kDownArrow);
}

static void InsertMarker(DocState *doc, const char *marker)
{
    short n = (unsigned char)marker[0];
    if (n > 0) TEInsert((Ptr)(marker + 1), n, doc->te);
}

static void ResetInsertStyleToPlain(DocState *doc)
{
    short pos = (**doc->te).selStart;
    short crPos;
    TextStyle plain;
    GrafPtr savedPort;
    RgnHandle savedClip, emptyRgn;

    if (pos < 1) return;
    crPos = pos - 1;

    plain.tsFont = 0;
    plain.tsFace = 0;
    plain.tsSize = 12;
    plain.tsColor.red = plain.tsColor.green = plain.tsColor.blue = 0;

    GetPort(&savedPort);
    SetPort(doc->window);
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    TESetSelect(crPos, pos, doc->te);
    TESetStyle(doFace | doSize, &plain, false, doc->te);
    TESetSelect(pos, pos, doc->te);

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);
}

static Boolean HandleReturnKey(DocState *doc)
{
    short pos = (**doc->te).selStart;
    char marker[24];
    Boolean isEmpty = false;
    Boolean hasMarker;

    hasMarker = MdNextListMarker(doc->te, pos, marker, &isEmpty);
    if (!hasMarker) return false;

    if (isEmpty) {
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
        short selEnd   = (**doc->te).selEnd;
        (void) MdRenumberOrderedList(doc->te, selStart, &selStart, &selEnd);
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
    char    c       = ev->message & charCodeMask;
    short   keyCode = (ev->message & keyCodeMask) >> 8;
    Boolean shift   = (ev->modifiers & shiftKey)   != 0;
    Boolean cmd     = (ev->modifiers & cmdKey)     != 0;
    Boolean option  = (ev->modifiers & optionKey)  != 0;
    short   pos;

    /* Normalize arrow keys via their virtual keycode (the Plus
       keypad digits 2/4/6/8 and the Extended Keyboard arrow keys).
       Shift+arrow on the Plus arrives as a different physical key
       (the keypad operator row) and we don't try to detect that
       case here -- text selection by Shift+arrow on a Mac Plus just
       won't work; use the mouse or Shift+click instead. */
    switch (keyCode) {
        case kKC_Ext_Left:  case kKC_KP_Left:  c = kLeftArrow;  break;
        case kKC_Ext_Right: case kKC_KP_Right: c = kRightArrow; break;
        case kKC_Ext_Up:    case kKC_KP_Up:    c = kUpArrow;    break;
        case kKC_Ext_Down:  case kKC_KP_Down:  c = kDownArrow;  break;
    }

    /* Cmd-` cycles to the next window. Done before MenuKey because
       the backtick isn't a real menu key equivalent. */
    if (cmd && (c == '`' || c == '~')) {
        DocCycleWindow();
        return;
    }

    if (cmd) {
        AdjustMenus();
        {
            long mr = MenuKey(c);
            if (HiWord(mr) != 0) { HandleMenu(mr); return; }
        }
    }

    if (doc == NULL) return;

    if (option && !cmd) {
        if (c == kUpArrow)   { DocMoveLineUp(doc);   return; }
        if (c == kDownArrow) { DocMoveLineDown(doc); return; }
    }

    if (option && !cmd && c == 0x08) {
        DocBeforeAction(doc);
        DoDeleteWordBack(doc);
        return;
    }

    if (cmd && !option && !shift) {
        if (c == '[') { DocOutdentLine(doc); return; }
        if (c == ']') { DocIndentLine(doc);  return; }
    }

    pos = (**doc->te).selStart;

    if (IsArrow(c)) {
        short newCursor;
        short curEnd = (**doc->te).selEnd;
        short startFrom;
        DocBreakTypingRun(doc);
        if (shift) {
            startFrom = (pos == doc->selAnchor) ? curEnd : pos;
        } else {
            if (pos != curEnd) startFrom = (c == kLeftArrow || c == kUpArrow) ? pos : curEnd;
            else                startFrom = pos;
        }

        if (cmd) {
            if (c == kLeftArrow)       newCursor = DocLineStartOffset(doc, startFrom);
            else if (c == kRightArrow) newCursor = DocLineEndOffset(doc, startFrom);
            else if (c == kUpArrow)    newCursor = 0;
            else                       newCursor = (**doc->te).teLength;
        } else if (option) {
            if (c == kLeftArrow)       newCursor = DocOffsetWordLeft(doc, startFrom);
            else if (c == kRightArrow) newCursor = DocOffsetWordRight(doc, startFrom);
            else                       newCursor = startFrom;
        } else {
            switch (c) {
                case kLeftArrow:  newCursor = DocOffsetLeft(doc, startFrom);  break;
                case kRightArrow: newCursor = DocOffsetRight(doc, startFrom); break;
                case kUpArrow:    newCursor = DocOffsetUp(doc, startFrom);    break;
                case kDownArrow:  newCursor = DocOffsetDown(doc, startFrom);  break;
                default:          newCursor = startFrom;                      break;
            }
        }

        DocMoveCursorTo(doc, newCursor, shift);
        return;
    }

    if (cmd) {
        if (c == 'l' || c == 'L') { DoToggleTask(doc); return; }
        {
            long mr = MenuKey(c);
            if (HiWord(mr) != 0) HandleMenu(mr);
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
        switch (c) {
            case '*': openCh = '*'; closeCh = '*'; break;
            case '_': openCh = '_'; closeCh = '_'; break;
            case '`': openCh = '`'; closeCh = '`'; break;
            case '(': openCh = '('; closeCh = ')'; break;
            case '[': openCh = '['; closeCh = ']'; break;
        }
        if (openCh != 0) {
            short selStart = (**doc->te).selStart;
            short selEnd   = (**doc->te).selEnd;
            if (selStart != selEnd) {
                DocBeforeAction(doc);
                /* Insert closer at the high end first -- its position
                   isn't perturbed by the later insertion at selStart. */
                TESetSelect(selEnd, selEnd, doc->te);
                TEKey(closeCh, doc->te);
                TESetSelect(selStart, selStart, doc->te);
                TEKey(openCh, doc->te);
                TESetSelect(selStart + 1, selEnd + 1, doc->te);
                doc->selAnchor = selStart + 1;
                DocMarkDirty(doc);
                doc->dirtyLineStart = DocLineStartOffset(doc, selStart);
                doc->dirtyLineEnd   = DocLineEndOffset(doc, selEnd + 1);
                doc->lastDirtyTick  = TickCount() - 1000;
                DocFlushRestyle(doc);
                DocAdjustScrollbar(doc);
                return;
            }
        }
    }

    if (c == '\r') {
        DocBeforeAction(doc);
        if (HandleReturnKey(doc)) {
            ResetInsertStyleToPlain(doc);
            doc->selAnchor = (**doc->te).selStart;
            return;
        }
    } else {
        DocBeforeTyping(doc);
    }

    TEKey(c, doc->te);
    if (c == '\r') ResetInsertStyleToPlain(doc);
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

        if (isPlainText) {
            short pos = (**doc->te).selStart;
            short lineStart, lineEnd;
            MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
            if (pos - lineStart > 7) {
                CharsHandle ch = TEGetText(doc->te);
                char *t;
                short scanStart, scanEnd, j;
                Boolean nearMarker = false;
                HLock((Handle)ch);
                t = *ch;
                scanStart = pos - 2;
                if (scanStart < lineStart) scanStart = lineStart;
                scanEnd = pos + 2;
                if (scanEnd > lineEnd) scanEnd = lineEnd;
                for (j = scanStart; j < scanEnd; j++) {
                    if (t[j] == '*' || t[j] == '_') {
                        nearMarker = true;
                        break;
                    }
                }
                HUnlock((Handle)ch);
                if (!nearMarker) needMark = false;
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
    if (c == ' ' || c == '\t') doc->inTypingRun = false;
}

/* ---- Cmd-L / delete-word implementations ---- */

static void ForceRestyleRangeFor(DocState *doc, short start, short end)
{
    doc->dirtyLineStart = start;
    doc->dirtyLineEnd   = end;
    doc->lastDirtyTick  = TickCount() - 1000;
    DocFlushRestyle(doc);
}

static void DoDeleteWordBack(DocState *doc)
{
    short curPos = (**doc->te).selStart;
    short curEnd = (**doc->te).selEnd;
    short newCur;

    if (curPos != curEnd) {
        TEDelete(doc->te);
    } else {
        short wordLeft = DocOffsetWordLeft(doc, curPos);
        if (wordLeft >= curPos) return;
        TESetSelect(wordLeft, curPos, doc->te);
        TEDelete(doc->te);
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
    CharsHandle ch;
    char *text;
    MdLineKind kind;
    short leading;

    DocBeforeAction(doc);
    pos = (**doc->te).selStart;
    MdFindLineBounds(doc->te, pos, &lineStart, &lineEnd);
    lineLen = lineEnd - lineStart;

    ch = TEGetText(doc->te);
    HLock((Handle)ch);
    text = *ch + lineStart;
    kind = MdClassifyLine(text, lineLen);

    leading = 0;
    while (leading < lineLen && leading < 3 && text[leading] == ' ') leading++;
    HUnlock((Handle)ch);

    if (kind == kLine_TaskUnchecked || kind == kLine_TaskChecked) {
        short boxPos = MdFindTaskBox(doc->te, pos);
        short savedStart, savedEnd;
        char current, replacement;

        if (boxPos < 0) { SysBeep(1); return; }

        savedStart = (**doc->te).selStart;
        savedEnd   = (**doc->te).selEnd;

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

    if (kind == kLine_UnorderedItem) {
        short insertPos = lineStart + leading + 2;
        short newSel = (pos >= insertPos) ? pos + 4 : pos;
        TESetSelect(insertPos, insertPos, doc->te);
        TEInsert((Ptr)"[ ] ", 4, doc->te);
        TESetSelect(newSel, newSel, doc->te);
        DocMarkDirty(doc);
        ForceRestyleRangeFor(doc, lineStart, lineEnd + 4);
        DocAdjustScrollbar(doc);
        return;
    }

    {
        short newSel = pos + 6;
        TESetSelect(lineStart, lineStart, doc->te);
        TEInsert((Ptr)"- [ ] ", 6, doc->te);
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
    short selEnd   = (**doc->te).selEnd;
    short caret;

    DocBeforeAction(doc);

    if (selStart == selEnd) {
        /* Empty selection: insert the full four chars. */
        TEKey('[', doc->te);
        TEKey(']', doc->te);
        TEKey('(', doc->te);
        TEKey(')', doc->te);
        caret = selStart + 1;   /* between [ and ] */
    } else {
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

    TextFont(0);          /* system (Chicago) */
    TextSize(12);
    TextFace(bold);
    MoveTo(24, 32);
    DrawString("\pMacDown");

    TextFace(0);
    MoveTo(24, 54);
    DrawString("\pVersion 1.0.0");

    MoveTo(24, 82);
    DrawString("\pA small Markdown editor for classic Mac OS.");
    MoveTo(24, 100);
    DrawString("\pBy Rockwell Schrock (@schrockwell)");

    TextFace(italic);
    MoveTo(24, 128);
    DrawString("\p(click anywhere to dismiss)");
    TextFace(0);
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

    TextFont(0);          /* system (Chicago) */
    TextSize(12);
    TextFace(bold);
    MoveTo(20, 28);
    DrawString("\pKeyboard Shortcuts");

    /* Geneva 9 for the table -- wee bit of room for more rows. */
    TextFont(3);          /* Geneva */
    TextSize(9);
    TextFace(0);

    y = 56;
    DrawShortcutRow(y, "\pMove One Word",          "\pOption + Left / Right");
    y += 16;
    DrawShortcutRow(y, "\pDelete Word",            "\pOption + Delete");
    y += 16;
    DrawShortcutRow(y, "\pMove Line",              "\pOption + Up / Down");
    y += 16;
    DrawShortcutRow(y, "\pStart / End of Line",    "\pCommand + Left / Right");
    y += 16;
    DrawShortcutRow(y, "\pText Selection",         "\pHold Shift");

    TextFace(italic);
    MoveTo(20, y + 28);
    DrawString("\p(click anywhere to dismiss)");
    TextFace(0);
}

/* Show a plain-bordered window and pump events until any mouse/key
   dismisses it. Other windows' update events are forwarded so they
   keep painting if the modal moves or uncovers them. */
static void ShowModalSplash(short windID, void (*draw)(WindowPtr))
{
    WindowPtr w;
    GrafPtr   savedPort;
    EventRecord ev;
    Boolean done = false;

    InitCursor();
    w = GetNewWindow(windID, NULL, (WindowPtr)-1L);
    if (w == NULL) return;

    GetPort(&savedPort);
    SetPort(w);
    draw(w);

    while (!done) {
        if (WaitNextEvent(everyEvent, &ev, 6, NULL)) {
            switch (ev.what) {
                case mouseDown:
                case keyDown:
                case autoKey:
                    done = true;
                    break;
                case updateEvt: {
                    WindowPtr uw = (WindowPtr)ev.message;
                    if (uw == w) {
                        SetPort(w);
                        BeginUpdate(w);
                        draw(w);
                        EndUpdate(w);
                    } else {
                        DocState *d = DocFromWindow(uw);
                        if (d) DocUpdate(d);
                    }
                    break;
                }
            }
        }
    }

    DisposeWindow(w);
    SetPort(savedPort);
}

static void DoAbout(void)     { ShowModalSplash(129, DrawAboutContent);     }
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
        for (i = 0; i < (short)sizeof(SFReply); i++) p[i] = 0;
    }

    types[0] = 'TEXT';

    GetPort(&savedPort);
    InitCursor();

    where.h = 80;
    where.v = 60;
    SFGetFile(where, "\p", NULL, 1, types, NULL, &reply);

    SetPort(savedPort);
    {
        EventRecord ev;
        EventAvail(0, &ev);
    }

    if (!reply.good) return;
    DocOpen(reply.vRefNum, reply.fName);
}

static void HandleQuit(void)
{
    if (DocCloseAll()) gQuitRequested = true;
}

static void AdjustMenus(void)
{
    SyncFileMenuEnables();
}

static void HandleMenu(long mResult)
{
    short menuID = HiWord(mResult);
    short item   = LoWord(mResult);
    DocState *doc = DocActive();

    switch (menuID) {
        case kAppleMenuID:
            if (item == kAboutItem) {
                DoAbout();
            } else if (item == kShortcutsItem) {
                DoShortcuts();
            } else {
                Str255 daName;
                GetMenuItemText(GetMenuHandle(kAppleMenuID), item, daName);
                OpenDeskAcc(daName);
            }
            break;

        case kFileMenuID:
            switch (item) {
                case kFileNew:    DocNew(); break;
                case kFileOpen:   DoFileOpen(); break;
                case kFileClose:    if (doc) DocClose(doc); break;
                case kFileCloseAll: DocCloseAll(); break;
                case kFileSave:     if (doc) DocSave(doc); break;
                case kFileSaveAs:   if (doc) DocSaveAs(doc); break;
                case kFileQuit:     HandleQuit(); break;
            }
            break;

        case kEditMenuID:
            if (SystemEdit(item - 1)) break;
            if (doc == NULL) break;
            switch (item) {
                case kEditUndo:   DocUndo(doc); break;
                case kEditCut:    DocBeforeAction(doc);
                                  TECut(doc->te);
                                  DocMarkDirty(doc);
                                  DocMarkLineDirty(doc, (**doc->te).selStart);
                                  break;
                case kEditCopy:   TECopy(doc->te); break;
                case kEditPaste:  {
                                      short pasteStart = (**doc->te).selStart;
                                      short pasteEnd;
                                      DocBeforeAction(doc);
                                      TEPaste(doc->te);
                                      pasteEnd = (**doc->te).selStart;
                                      DocMarkDirty(doc);
                                      ForceRestyleRangeFor(doc, pasteStart, pasteEnd);
                                  }
                                  break;
                case kEditClear:  DocBeforeAction(doc);
                                  TEDelete(doc->te);
                                  DocMarkDirty(doc);
                                  DocMarkLineDirty(doc, (**doc->te).selStart);
                                  break;
                case kEditSelAll:    DocBreakTypingRun(doc);
                                     TESetSelect(0, 32767, doc->te);
                                     break;
                case kEditDuplicate: DocDuplicateLine(doc); break;
            }
            if (doc) DocAdjustScrollbar(doc);
            break;

        case kFormatMenuID:
            if (doc == NULL) break;
            switch (item) {
                case kFormatBold:       DocWrapPair(doc, '*', 2); break;
                case kFormatItalic:     DocWrapPair(doc, '*', 1); break;
                case kFormatCode:       DocWrapPair(doc, '`', 1); break;
                case kFormatInsertLink: DoInsertLink(doc); break;
            }
            break;

        case kLineMenuID:
            if (doc == NULL) break;
            switch (item) {
                case kLineH1:         DocToggleHeading(doc, 1); break;
                case kLineH2:         DocToggleHeading(doc, 2); break;
                case kLineH3:         DocToggleHeading(doc, 3); break;
                case kLineH4:         DocToggleHeading(doc, 4); break;
                case kLineH5:         DocToggleHeading(doc, 5); break;
                case kLineH6:         DocToggleHeading(doc, 6); break;
                case kLineNoHeading:  DocToggleHeading(doc, 0); break;
                case kLineToggleTask: DoToggleTask(doc); break;
                case kLineBlockquote: DocToggleBlockquote(doc); break;
                case kLineIndent:     DocIndentLine(doc); break;
                case kLineOutdent:    DocOutdentLine(doc); break;
            }
            break;

        case kWindowsMenuID:
            if (item == kWindowsMenuNext)         DocCycleWindow();
            else if (item == kWindowsMenuBrowser) BrowserToggle();
            else if (item > kWindowsMenuStaticItems) {
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
    OpenFromFinderLaunch();
    if (gDocs == NULL) DocNew();

    while (!gQuitRequested) {
        if (WaitNextEvent(everyEvent, &event, sleepTicks, NULL)) {
            switch (event.what) {
                case mouseDown:    HandleMouse(&event); break;
                case keyDown:
                case autoKey:      HandleKey(&event); break;
                case updateEvt: {
                    WindowPtr uw = (WindowPtr)event.message;
                    DocState *d = DocFromWindow(uw);
                    if (d) DocUpdate(d);
                    else if (BrowserIsWindow(uw)) BrowserUpdate();
                    break;
                }
                case activateEvt: {
                    WindowPtr aw = (WindowPtr)event.message;
                    DocState *d = DocFromWindow(aw);
                    if (d) DocActivate(d, (event.modifiers & activeFlag) != 0);
                    else if (BrowserIsWindow(aw))
                        BrowserActivate((event.modifiers & activeFlag) != 0);
                    break;
                }
                case kHighLevelEvent:
                    AEDispatch(&event);
                    break;
                case osEvt:
                    if ((event.message >> 24) == 0x01) {
                        DocState *d = DocActive();
                        if (d) DocActivate(d, (event.message & 0x01) != 0);
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
            if (d) {
                TEIdle(d->te);
                if (d->dirtyLineStart >= 0 &&
                    (TickCount() - d->lastDirtyTick) > kRestyleIdleTicks) {
                    DocFlushRestyle(d);
                    DocAdjustScrollbar(d);
                }
            }
            BrowserIdle();
            DocAdjustCursor();
        }
    }

    return 0;
}
