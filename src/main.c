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
#include "markdown.h"
#include "apple_events.h"

#define kMenuBarID    128
#define kAppleMenuID  128
#define kFileMenuID   129
#define kEditMenuID   130
#define kFormatMenuID 131

/* File menu item IDs */
#define kFileNew     1
#define kFileOpen    2
#define kFileClose   3
#define kFileSave    4
#define kFileSaveAs  5
/* item 6 = separator */
#define kFileQuit    7

/* Edit menu item IDs */
#define kEditUndo    1
/* item 2 = separator */
#define kEditCut     3
#define kEditCopy    4
#define kEditPaste   5
#define kEditClear   6
#define kEditSelAll  7
/* item 8 = separator */
#define kEditDeleteWord 9

/* Format menu item IDs */
#define kFormatToggleTask   1
/* item 2 = separator */
#define kFormatIndent       3
#define kFormatOutdent      4
/* item 5 = separator */
#define kFormatRestyleAll   6

#define kAboutItem    1

/* How long the user must pause typing before we run the styler.
   Short pause is fine now that DocMarkLineDirty filters out edits past
   col 4 — most keystrokes won't trigger a restyle at all. */
#define kRestyleIdleTicks 6L

#define UC(c) ((unsigned char)(c))

static void Initialize(void);
static void OpenFromFinderLaunch(void);
static void HandleMouse(EventRecord *ev);
static void HandleKey(EventRecord *ev);
static void HandleMenu(long mResult);
static void DoAbout(void);
static Boolean HandleReturnKey(void);
static void DoToggleTask(void);
static void DoDeleteWordBack(void);
static void DoFileOpen(void);

/* ---- Initialization ---- */

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

    menuBar = GetNewMBar(kMenuBarID);
    SetMenuBar(menuBar);
    AppendResMenu(GetMenuHandle(kAppleMenuID), 'DRVR');

    /* Undo starts disabled — DocClearUndo runs at init via DocInit
       indirectly, but ensure the menu reflects "nothing to undo" before
       anything else. */
    DisableItem(GetMenuHandle(kEditMenuID), kEditUndo);

    DrawMenuBar();

    AEInit();
    DocInit();
}

/* ---- Finder launch with documents (System 6 + early 7 pre-AE) ---- */

static void OpenFromFinderLaunch(void)
{
    short msg, count;
    AppFile af;

    CountAppFiles(&msg, &count);
    if (count < 1) return;
    GetAppFiles(1, &af);
    DocOpen(af.vRefNum, af.fName);
    ClrAppFiles(1);
}

/* ---- Mouse ---- */

static void HandleMouse(EventRecord *ev)
{
    WindowPtr w;
    short part = FindWindow(ev->where, &w);

    switch (part) {
        case inMenuBar:
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
        case inGrow: {
            long sz;
            Rect limits;
            SetRect(&limits, 200, 100, qd.screenBits.bounds.right,
                    qd.screenBits.bounds.bottom);
            sz = GrowWindow(w, ev->where, &limits);
            if (sz != 0) {
                SizeWindow(w, LoWord(sz), HiWord(sz), true);
                DocResize();
            }
            break;
        }
        case inGoAway:
            /* Close the document — the window hides and the app stays
               alive (Quit is Cmd-Q). File → New brings the window
               back; File → Open shows it with the loaded file. */
            if (TrackGoAway(w, ev->where)) {
                DocClose();
            }
            break;
        case inZoomIn:
        case inZoomOut:
            if (TrackBox(w, ev->where, part)) {
                ZoomWindow(w, part, true);
                DocResize();
            }
            break;
        case inContent:
            if (w != FrontWindow()) {
                SelectWindow(w);
            } else {
                DocClick(ev);
            }
            break;
    }
}

/* ---- Keyboard ---- */

/* Insert a Pascal-string marker after the current selection.
   outMarker[0] = length. */
static void InsertMarker(const char *marker)
{
    short n = (unsigned char)marker[0];
    if (n > 0) TEInsert((Ptr)(marker + 1), n, gDoc.te);
}

/* Reset the "typing style" so the next inserted char is plain Geneva 12.
   Classic TextEdit doesn't honor TESetStyle on a zero-width selection
   as a "pending style" hint — the next typed char still inherits the
   style of the char to its left. Workaround: apply plain style to the
   just-inserted CR itself; subsequent chars inherit the CR's style. */
static void ResetInsertStyleToPlain(void)
{
    short pos = (**gDoc.te).selStart;
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

    /* Suppress the selection-highlight flash during the style change. */
    GetPort(&savedPort);
    SetPort(gDoc.window);
    savedClip = NewRgn();
    emptyRgn  = NewRgn();
    GetClip(savedClip);
    SetClip(emptyRgn);

    TESetSelect(crPos, pos, gDoc.te);
    TESetStyle(doFace | doSize, &plain, false, gDoc.te);
    TESetSelect(pos, pos, gDoc.te);

    SetClip(savedClip);
    DisposeRgn(savedClip);
    DisposeRgn(emptyRgn);
    SetPort(savedPort);
}

/* Returns true if it handled the Return itself; false → let TEKey run. */
static Boolean HandleReturnKey(void)
{
    short pos = (**gDoc.te).selStart;
    char marker[24];
    Boolean isEmpty = false;
    Boolean hasMarker;

    hasMarker = MdNextListMarker(gDoc.te, pos, marker, &isEmpty);
    if (!hasMarker) return false;

    if (isEmpty) {
        /* Exit list: select the marker on the current line and delete it,
           then insert a plain CR. */
        short lineStart, lineEnd;
        MdFindLineBounds(gDoc.te, pos, &lineStart, &lineEnd);
        TESetSelect(lineStart, lineEnd, gDoc.te);
        TEDelete(gDoc.te);
        TEKey('\r', gDoc.te);
        DocMarkDirty();
        DocMarkLineDirty((**gDoc.te).selStart);
        return true;
    }

    /* Continue list: insert CR then marker. */
    TEKey('\r', gDoc.te);
    InsertMarker(marker);
    DocMarkDirty();
    DocMarkLineDirty((**gDoc.te).selStart);
    return true;
}

/* Arrow key character codes (charCodeMask). */
#define kLeftArrow  0x1C
#define kRightArrow 0x1D
#define kUpArrow    0x1E
#define kDownArrow  0x1F

static Boolean IsArrow(char c)
{
    return (c == kLeftArrow || c == kRightArrow ||
            c == kUpArrow   || c == kDownArrow);
}

static void HandleKey(EventRecord *ev)
{
    char    c       = ev->message & charCodeMask;
    Boolean shift   = (ev->modifiers & shiftKey)   != 0;
    Boolean cmd     = (ev->modifiers & cmdKey)     != 0;
    Boolean option  = (ev->modifiers & optionKey)  != 0;
    short   pos;

    /* Option + arrow up/down: line move (mutates text, not cursor). */
    if (option && !cmd) {
        if (c == kUpArrow)   { DocMoveLineUp();   return; }
        if (c == kDownArrow) { DocMoveLineDown(); return; }
        /* Option + Left/Right (word jump) falls through to the arrow
           handling below, which knows about the `option` modifier. */
    }

    /* Option + Backspace: delete the previous word. */
    if (option && !cmd && c == 0x08) {
        DocBeforeAction();
        DoDeleteWordBack();
        return;
    }

    /* Cmd + [ / ] : outdent / indent. Check before MenuKey so the
       bracket keys aren't intercepted as menu shortcuts. */
    if (cmd && !option && !shift) {
        if (c == '[') { DocOutdentLine(); return; }
        if (c == ']') { DocIndentLine();  return; }
    }

    pos = (**gDoc.te).selStart;

    /* Arrow keys — handled manually (TEKey doesn't move the cursor).
       Cmd jumps to line start/end; Shift extends selection from
       selAnchor; combinations of both work as expected. */
    if (IsArrow(c)) {
        short newCursor;
        short curEnd = (**gDoc.te).selEnd;
        DocBreakTypingRun();
        /* The "active" cursor end during shift-extend is whichever end
           is opposite the anchor. Otherwise we start from selStart
           (left arrow / up) or selEnd (right arrow / down) so a
           collapsed-from-selection arrow goes to the natural edge. */
        short startFrom;
        if (shift) {
            startFrom = (pos == gDoc.selAnchor) ? curEnd : pos;
        } else {
            if (pos != curEnd) {
                startFrom = (c == kLeftArrow || c == kUpArrow) ? pos : curEnd;
            } else {
                startFrom = pos;
            }
        }

        if (cmd) {
            /* Cmd+Left/Right jump to line start/end. Cmd+Up/Down go
               to start/end of document. */
            if (c == kLeftArrow)       newCursor = DocLineStartOffset(startFrom);
            else if (c == kRightArrow) newCursor = DocLineEndOffset(startFrom);
            else if (c == kUpArrow)    newCursor = 0;
            else                       newCursor = (**gDoc.te).teLength;
        } else if (option) {
            /* Option+Left/Right: word jump. (Option+Up/Down was already
               handled above as line move and returned.) */
            if (c == kLeftArrow)       newCursor = DocOffsetWordLeft(startFrom);
            else if (c == kRightArrow) newCursor = DocOffsetWordRight(startFrom);
            else                       newCursor = startFrom;  /* unreachable */
        } else {
            switch (c) {
                case kLeftArrow:  newCursor = DocOffsetLeft(startFrom);  break;
                case kRightArrow: newCursor = DocOffsetRight(startFrom); break;
                case kUpArrow:    newCursor = DocOffsetUp(startFrom);    break;
                case kDownArrow:  newCursor = DocOffsetDown(startFrom);  break;
                default:          newCursor = startFrom;                 break;
            }
        }

        DocMoveCursorTo(newCursor, shift);
        return;
    }

    if (cmd) {
        /* Cmd-L: toggle/create task on current line. */
        if (c == 'l' || c == 'L') { DoToggleTask(); return; }
        /* Other Cmd-keys go through the menu manager. */
        {
            long mr = MenuKey(c);
            if (HiWord(mr) != 0) HandleMenu(mr);
        }
        return;
    }

    if (c == '\r') {
        DocBeforeAction();   /* Return ends any typing run and starts a new
                                undo step (snapshots before the CR). */
        if (HandleReturnKey()) {
            ResetInsertStyleToPlain();
            gDoc.selAnchor = (**gDoc.te).selStart;
            return;
        }
    } else {
        DocBeforeTyping();   /* First char of a typing run snapshots; rest
                                of the run is grouped into one undo step. */
    }

    TEKey(c, gDoc.te);
    if (c == '\r') ResetInsertStyleToPlain();
    gDoc.selAnchor = (**gDoc.te).selStart;
    DocMarkDirty();
    DocMarkLineDirty((**gDoc.te).selStart);
    DocAdjustScrollbar();
}

/* ---- Cmd-L task toggle ---- */

/* Force a restyle of an arbitrary range, bypassing DocMarkLineDirty's
   col-7 / plain-style filters. Used after structural edits like Cmd-L
   prefix insertion or line moves where we know the range must redraw. */
static void ForceRestyleRange(short start, short end)
{
    gDoc.dirtyLineStart = start;
    gDoc.dirtyLineEnd   = end;
    gDoc.lastDirtyTick  = TickCount() - 1000;
    DocFlushRestyle();
}

/* Delete the previous word (or current selection if any). Uses the
   same word-boundary logic as Option-Left, so it stops at the start of
   the current line — a second invocation deletes the CR, joining lines.
   Wired to Option-Backspace and Edit → Delete Word Back. */
static void DoDeleteWordBack(void)
{
    short curPos = (**gDoc.te).selStart;
    short curEnd = (**gDoc.te).selEnd;
    short newCur;

    if (curPos != curEnd) {
        TEDelete(gDoc.te);
    } else {
        short wordLeft = DocOffsetWordLeft(curPos);
        if (wordLeft >= curPos) return;
        TESetSelect(wordLeft, curPos, gDoc.te);
        TEDelete(gDoc.te);
    }
    gDoc.selAnchor = (**gDoc.te).selStart;
    DocMarkDirty();
    newCur = (**gDoc.te).selStart;
    ForceRestyleRange(DocLineStartOffset(newCur), DocLineEndOffset(newCur));
    DocAdjustScrollbar();
}

static void DoToggleTask(void)
{
    short pos;
    short lineStart, lineEnd, lineLen;
    DocBeforeAction();
    pos = (**gDoc.te).selStart;
    CharsHandle ch;
    char *text;
    MdLineKind kind;
    short leading;

    MdFindLineBounds(gDoc.te, pos, &lineStart, &lineEnd);
    lineLen = lineEnd - lineStart;

    ch = TEGetText(gDoc.te);
    HLock((Handle)ch);
    text = *ch + lineStart;
    kind = MdClassifyLine(text, lineLen);

    /* Count leading spaces — markdown allows up to 3 before the marker. */
    leading = 0;
    while (leading < lineLen && leading < 3 && text[leading] == ' ') leading++;
    HUnlock((Handle)ch);

    /* Case A: line is already a task list item — toggle the box. */
    if (kind == kLine_TaskUnchecked || kind == kLine_TaskChecked) {
        short boxPos = MdFindTaskBox(gDoc.te, pos);
        short savedStart, savedEnd;
        char current, replacement;

        if (boxPos < 0) { SysBeep(1); return; }

        savedStart = (**gDoc.te).selStart;
        savedEnd   = (**gDoc.te).selEnd;

        ch = TEGetText(gDoc.te);
        HLock((Handle)ch);
        current = (*ch)[boxPos];
        HUnlock((Handle)ch);

        replacement = (current == ' ') ? 'x' : ' ';

        TESetSelect(boxPos, boxPos + 1, gDoc.te);
        TEDelete(gDoc.te);
        TEKey(replacement, gDoc.te);

        TESetSelect(savedStart, savedEnd, gDoc.te);
        DocMarkDirty();
        DocMarkLineDirty(boxPos);
        return;
    }

    /* Case B: line is an unordered list item — upgrade to a task by
       inserting "[ ] " after the marker. */
    if (kind == kLine_UnorderedItem) {
        short insertPos = lineStart + leading + 2;  /* after "- " */
        short newSel = (pos >= insertPos) ? pos + 4 : pos;

        TESetSelect(insertPos, insertPos, gDoc.te);
        TEInsert((Ptr)"[ ] ", 4, gDoc.te);
        TESetSelect(newSel, newSel, gDoc.te);

        DocMarkDirty();
        ForceRestyleRange(lineStart, lineEnd + 4);
        DocAdjustScrollbar();
        return;
    }

    /* Case C: any other line — prepend a full "- [ ] " prefix. Cursor
       follows its char forward by 6. */
    {
        short newSel = pos + 6;
        TESetSelect(lineStart, lineStart, gDoc.te);
        TEInsert((Ptr)"- [ ] ", 6, gDoc.te);
        TESetSelect(newSel, newSel, gDoc.te);

        DocMarkDirty();
        ForceRestyleRange(lineStart, lineEnd + 6);
        DocAdjustScrollbar();
    }
}

/* ---- Menu ---- */

static void DoAbout(void)
{
    InitCursor();
    NoteAlert(128, NULL);
}

static void DoFileOpen(void)
{
    SFReply reply;
    SFTypeList types;
    Point where;
    GrafPtr savedPort;
    short i;

    /* Zero-init the reply to avoid any garbage-field surprises in SF. */
    {
        char *p = (char *)&reply;
        for (i = 0; i < (short)sizeof(SFReply); i++) p[i] = 0;
    }

    types[0] = 'TEXT';

    /* SF draws its modal in the current GrafPort — ensure it's a real
       window and the cursor isn't stuck as a watch. */
    GetPort(&savedPort);
    SetPort(gDoc.window);
    InitCursor();

    where.h = 80;
    where.v = 60;
    SFGetFile(where, "\p", NULL, 1, types, NULL, &reply);

    SetPort(savedPort);
    /* Same OS-state-after-modal dance as DocSaveAs — yield once so the
       File Manager doesn't wedge on the immediately-following open. */
    {
        EventRecord ev;
        EventAvail(0, &ev);
    }

    if (!reply.good) return;
    DocOpen(reply.vRefNum, reply.fName);
}

static void HandleMenu(long mResult)
{
    short menuID = HiWord(mResult);
    short item   = LoWord(mResult);

    switch (menuID) {
        case kAppleMenuID:
            if (item == kAboutItem) {
                DoAbout();
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
                case kFileClose:  DocClose(); break;
                case kFileSave:   DocSave(); break;
                case kFileSaveAs: DocSaveAs(); break;
                case kFileQuit:
                    /* If there's no file open, skip the save prompt —
                       there's nothing the user has explicitly named to
                       save. Untitled doc edits just get discarded. */
                    if (!gDoc.hasFile || DocPromptSaveIfDirty()) {
                        gQuitRequested = true;
                    }
                    break;
            }
            break;

        case kEditMenuID:
            /* Let the Edit Manager handle DA windows first. */
            if (SystemEdit(item - 1)) break;
            switch (item) {
                case kEditUndo:   DocUndo(); break;
                case kEditCut:    DocBeforeAction();
                                  TECut(gDoc.te);
                                  DocMarkDirty();
                                  DocMarkLineDirty((**gDoc.te).selStart);
                                  break;
                case kEditCopy:   TECopy(gDoc.te); break;
                case kEditPaste:  DocBeforeAction();
                                  TEPaste(gDoc.te);
                                  DocMarkDirty();
                                  DocMarkLineDirty((**gDoc.te).selStart);
                                  break;
                case kEditClear:  DocBeforeAction();
                                  TEDelete(gDoc.te);
                                  DocMarkDirty();
                                  DocMarkLineDirty((**gDoc.te).selStart);
                                  break;
                case kEditSelAll:
                    DocBreakTypingRun();
                    TESetSelect(0, 32767, gDoc.te);
                    break;
                case kEditDeleteWord:
                    DocBeforeAction();
                    DoDeleteWordBack();
                    break;
            }
            DocAdjustScrollbar();
            break;

        case kFormatMenuID:
            switch (item) {
                case kFormatToggleTask:   DoToggleTask(); break;
                case kFormatIndent:       DocIndentLine(); break;
                case kFormatOutdent:      DocOutdentLine(); break;
                case kFormatRestyleAll:   MdRestyleAll(gDoc.te);
                                          InvalRect(&gDoc.window->portRect);
                                          break;
            }
            break;
    }
    HiliteMenu(0);
}

/* ---- Main ---- */

int main(void)
{
    EventRecord event;
    long sleepTicks = 6L;

    Initialize();

    /* System 6 / pre-AE Finder launch-with-files. */
    OpenFromFinderLaunch();

    while (!gQuitRequested) {
        if (WaitNextEvent(everyEvent, &event, sleepTicks, NULL)) {
            switch (event.what) {
                case mouseDown:    HandleMouse(&event); break;
                case keyDown:
                case autoKey:      HandleKey(&event); break;
                case updateEvt:    DocUpdate(); break;
                case activateEvt:
                    DocActivate((event.modifiers & activeFlag) != 0);
                    break;
                case kHighLevelEvent:
                    AEDispatch(&event);
                    break;
                case osEvt:
                    /* Suspend/resume from MultiFinder. Upper byte = 0x01;
                       bit 0 of low byte = resume flag. */
                    if ((event.message >> 24) == 0x01) {
                        DocActivate((event.message & 0x01) != 0);
                    }
                    break;
            }
        }

        TEIdle(gDoc.te);
        DocAdjustCursor();

        /* Restyle dirty region after a brief quiet period.
           DocFlushRestyle already InvalRects the TE area so the
           next updateEvt does the redraw cleanly. */
        if (gDoc.dirtyLineStart >= 0 &&
            (TickCount() - gDoc.lastDirtyTick) > kRestyleIdleTicks)
        {
            DocFlushRestyle();
            DocAdjustScrollbar();
        }
    }

    DocDispose();
    /* `return 0` is fine under MultiFinder but on bare System 6 it can
       leave the Process Manager waiting indefinitely. ExitToShell()
       guarantees the app terminates and the Finder regains control. */
    ExitToShell();
    return 0;
}
