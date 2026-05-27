#ifndef MacDown_MARKDOWN_H
#define MacDown_MARKDOWN_H

#include <TextEdit.h>

typedef enum
{
   kLine_Plain = 0,
   kLine_H1,
   kLine_H2,
   kLine_H3,
   kLine_H4,
   kLine_H5,
   kLine_H6,
   kLine_UnorderedItem,
   kLine_OrderedItem,
   kLine_TaskUnchecked,
   kLine_TaskChecked,
   kLine_Blockquote
} MdLineKind;

/* Classify a line given a pointer to its first byte and its length
   (no trailing CR). Used by both the styling pass and the
   list-auto-continue logic. */
MdLineKind MdClassifyLine(const char *line, short len);

/* Find the offsets of the current line in a TextEdit handle, given
   any offset within that line. *lineStart receives the offset of
   the line's first byte; *lineEnd receives the offset of the trailing
   CR (or text end). */
void MdFindLineBounds(TEHandle te, short pos,
                      short *lineStart, short *lineEnd);

/* Apply style to one line in te. Sets default plain style first, then
   the appropriate face/size for headers etc., then scans for inline
   emphasis and applies bold to **bold**, __bold__, *italic*, _italic_
   spans (italic collapses to bold per spec). Preserves caller's
   selection. */
void MdRestyleLine(TEHandle te, short lineStart, short lineEnd);

/* Restyle every line in the document. Used on Open and on Format →
   Re-style Document. */
void MdRestyleAll(TEHandle te);

/* If the line containing `pos` is a list item with non-empty content,
   write the marker for the next line into *outMarker (Pascal-string-
   style: byte 0 = length, bytes 1+ = chars) and return true.
   Returns false if the line is not a list line, or if it's a list
   line with empty content (in which case the caller should exit the
   list). For ordered lists, *outMarker contains the incremented
   number. Caller must allocate at least 24 bytes for outMarker. */
Boolean MdNextListMarker(TEHandle te, short pos,
                         char *outMarker, Boolean *outIsEmpty);

/* If the line containing `pos` has a `- [ ]` or `- [x]` checkbox,
   return the absolute offset of the state character (' ' or 'x' /
   'X'). Returns -1 if no checkbox on this line. */
short MdFindTaskBox(TEHandle te, short pos);

/* Renumber the ordered list that contains line `lineStart` so each
   item is one greater than the previous, starting from the existing
   first-item number. No-op if `lineStart` is not on an ordered list
   line. Returns the net change in document length.

   If ioSelStart / ioSelEnd are non-NULL, they are updated as text
   shifts so callers can restore the user's caret after the call.
   Each modified line's digit run can either shift adjacent positions
   by the digit-count delta or, for positions sitting inside a
   replaced digit run, clamp to the new run's end. */
long MdRenumberOrderedList(TEHandle te, short lineStart,
                           short *ioSelStart, short *ioSelEnd);

#endif
