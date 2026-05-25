#ifndef MDEDIT_DOCUMENT_H
#define MDEDIT_DOCUMENT_H

#include <Windows.h>
#include <TextEdit.h>
#include <Files.h>
#include <Multiverse.h>

#include "file_io.h"

#define kMdScrollWidth 15

typedef struct {
    WindowPtr     window;
    TEHandle      te;
    ControlHandle vScroll;
    short         vRefNum;
    Str63         fileName;          /* empty if !hasFile */
    Boolean       hasFile;
    Boolean       dirty;
    LineEndKind   leKind;
    short         dirtyLineStart;    /* -1 = clean */
    short         dirtyLineEnd;
    long          lastDirtyTick;     /* TickCount at last edit */
    short         selAnchor;         /* fixed end during shift-arrow ext */
    /* Single-level undo. The snapshot stores the doc state just before
       the last undoable action. DocUndo swaps current↔snapshot so a
       second Undo redoes the action. inTypingRun groups consecutive
       keystrokes into one undo step. */
    Handle        undoText;
    short         undoLen;
    short         undoSelStart;
    short         undoSelEnd;
    LineEndKind   undoLE;
    Boolean       canUndo;
    Boolean       inTypingRun;
} DocState;

extern DocState gDoc;

/* Lifecycle. */
void   DocInit(void);                       /* create window + empty TE */
void   DocDispose(void);
Boolean DocNew(void);                       /* discard current → blank doc, window shown */
Boolean DocOpen(short vRefNum, ConstStr255Param name);
Boolean DocSave(void);                      /* save to existing file */
Boolean DocSaveAs(void);                    /* prompt for destination */
Boolean DocPromptSaveIfDirty(void);         /* true = ok to proceed; false = cancel */
Boolean DocClose(void);                     /* hide window + clear state; app stays alive */

/* Window plumbing. */
void   DocResize(void);
void   DocUpdate(void);
void   DocActivate(Boolean active);
void   DocClick(EventRecord *ev);
void   DocAdjustScrollbar(void);

/* Selection helpers. */
void   DocMarkDirty(void);
void   DocMarkLineDirty(short pos);

/* Re-style any pending dirty region, then clear the marker. */
void   DocFlushRestyle(void);

/* Swap the current line with the line above / below. No-op at the
   top/bottom edges. */
void   DocMoveLineUp(void);
void   DocMoveLineDown(void);

/* Cursor movement helpers — return new offset given current pos. */
short  DocOffsetLeft(short pos);
short  DocOffsetRight(short pos);
short  DocOffsetUp(short pos);
short  DocOffsetDown(short pos);
short  DocOffsetWordLeft(short pos);
short  DocOffsetWordRight(short pos);
short  DocLineStartOffset(short pos);
short  DocLineEndOffset(short pos);

/* Apply a new cursor offset, either collapsing to a single point
   (extending=false) or extending the selection from selAnchor
   (extending=true). Resets selAnchor when not extending. */
void   DocMoveCursorTo(short newOffset, Boolean extending);

/* Insert one tab at the start of the current line. */
void   DocIndentLine(void);
/* Remove a single leading tab from the current line if present. */
void   DocOutdentLine(void);

/* Track mouse position and switch cursor between I-beam (over the text
   area) and arrow (over chrome). Call once per event loop iteration. */
void   DocAdjustCursor(void);

/* Undo support. DocBeforeAction snapshots before a discrete operation
   (paste, indent, line move, etc.). DocBeforeTyping snapshots only on
   the first keystroke of a typing run; subsequent keystrokes don't
   snapshot. DocClearUndo throws the snapshot away (used after Open/New).
   DocUndo swaps current state with the snapshot — repeated Undo
   alternates undo/redo. */
void   DocBeforeAction(void);
void   DocBeforeTyping(void);
void   DocBreakTypingRun(void);
void   DocClearUndo(void);
void   DocUndo(void);

/* Update window title from filename + dirty marker. */
void   DocUpdateTitle(void);

#endif
