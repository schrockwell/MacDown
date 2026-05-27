#ifndef MacDown_DOCUMENT_H
#define MacDown_DOCUMENT_H

#include <Windows.h>
#include <TextEdit.h>
#include <Files.h>
#include <Multiverse.h>

#include "file_io.h"

#define kMdScrollWidth 15

typedef struct DocState DocState;
struct DocState
{
    DocState *next; /* linked-list, gDocs is head */
    WindowPtr window;
    TEHandle te;
    ControlHandle vScroll;
    short vRefNum;  /* OWNED wdRefNum (via FileIOOwnWD) */
    Str63 fileName; /* empty if !hasFile */
    Boolean hasFile;
    Boolean dirty;
    LineEndKind leKind;
    short dirtyLineStart; /* -1 = clean */
    short dirtyLineEnd;
    long lastDirtyTick;
    short selAnchor;
    Handle undoText; /* single snapshot, NULL = no undo */
    short undoLen;
    short undoSelStart;
    short undoSelEnd;
    LineEndKind undoLE;
    Boolean canUndo;
    Boolean inTypingRun;
};

/* The list of all open documents. */
extern DocState *gDocs;

/* Lifecycle. */
void DocAppInit(void);  /* one-time module init */
DocState *DocNew(void); /* create + show new empty doc */
Boolean DocOpen(short anyWDRefNum, ConstStr255Param name);
Boolean DocOpenFromDir(short vRefNum, long dirID, ConstStr255Param name);
Boolean DocSave(DocState *doc);
Boolean DocSaveAs(DocState *doc);
Boolean DocPromptSaveIfDirty(DocState *doc); /* true = ok to proceed */
Boolean DocClose(DocState *doc);             /* dispose window + doc; remove from list */
Boolean DocCloseAll(void);                   /* prompt-and-close every doc; false if user cancelled */

/* Lookup. */
DocState *DocFromWindow(WindowPtr w);
DocState *DocActive(void); /* DocFromWindow(FrontWindow()) */

/* Windows menu. */
void RebuildWindowsMenu(void);
void DocSelectFromMenu(short menuItem); /* 1-based into dynamic doc items */
void DocCycleWindow(void);              /* send front to back, next comes forward */

/* Window plumbing — operate on a given doc. */
void DocResize(DocState *doc);
void DocUpdate(DocState *doc);
void DocActivate(DocState *doc, Boolean active);
void DocClick(DocState *doc, EventRecord *ev);
void DocAdjustScrollbar(DocState *doc);

/* Selection / dirty tracking. */
void DocMarkDirty(DocState *doc);
void DocMarkLineDirty(DocState *doc, short pos);
void DocFlushRestyle(DocState *doc);
void DocUpdateTitle(DocState *doc);

/* Line manipulation. */
void DocMoveLineUp(DocState *doc);
void DocMoveLineDown(DocState *doc);
void DocDuplicateLine(DocState *doc);

/* Heading toggle. `level` is 1..6 to set that level, or 0 to strip
   any existing heading marker. If the line is already at the
   requested level, also strips (toggle behavior). */
void DocToggleHeading(DocState *doc, short level);

/* Symmetric inline-pair wrap (Cmd-B, Cmd-I, Cmd-`). Wraps the
   selection with `n` copies of `ch` on each side. With no selection,
   inserts the pair and parks the caret between. */
void DocWrapPair(DocState *doc, char ch, short n);

/* Toggle "> " on every line that intersects the selection (or the
   current line if there's no selection). Direction is determined by
   the first line: if it's already a blockquote, the prefix is
   removed from each line; otherwise the prefix is added. */
void DocToggleBlockquote(DocState *doc);

/* Cursor helpers. */
short DocOffsetLeft(DocState *doc, short pos);
short DocOffsetRight(DocState *doc, short pos);
short DocOffsetUp(DocState *doc, short pos);
short DocOffsetDown(DocState *doc, short pos);
short DocOffsetWordLeft(DocState *doc, short pos);
short DocOffsetWordRight(DocState *doc, short pos);
short DocLineStartOffset(DocState *doc, short pos);
short DocLineEndOffset(DocState *doc, short pos);
void DocMoveCursorTo(DocState *doc, short newOffset, Boolean extending);

/* Indent / outdent (multi-line aware). */
void DocIndentLine(DocState *doc);
void DocOutdentLine(DocState *doc);

/* Cursor adjustment (I-beam over text area). */
void DocAdjustCursor(void);

/* Undo. */
void DocBeforeAction(DocState *doc);
void DocBeforeTyping(DocState *doc);
void DocBreakTypingRun(DocState *doc);
void DocClearUndo(DocState *doc);
void DocUndo(DocState *doc);

#endif
