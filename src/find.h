#ifndef MDEDIT_FIND_H
#define MDEDIT_FIND_H

#include <Events.h>
#include <Windows.h>
#include <TextEdit.h>

/* Edit > Find... : show the modeless Find window (or bring it forward),
   seeded with the front document's selection when it's a short,
   single-line one. */
void FindShowWindow(void);

/* Edit > Find Again / Replace & Find Again, from anywhere. Beep if
   there's nothing to find. */
void FindAgain(void);
void FindReplaceAndFindAgain(void);

/* True once something has been searched for (enables Find Again). */
Boolean FindHasPattern(void);

/* Event routing, like the Preferences window: returns true if the event
   was the Find window's. Command keys are left for the menus. */
Boolean FindHandleEvent(EventRecord *ev);
Boolean FindIsWindow(WindowPtr w);
Boolean FindWindowIsFront(void);
void    FindWindowClose(void);
void    FindIdle(void);

/* The Find window's active text field while it's in front (for the Edit
   menu's Cut/Copy/Paste/Clear/Select All), else NULL. */
TEHandle FindFieldTE(void);

#endif
