#ifndef MacDown_BROWSER_H
#define MacDown_BROWSER_H

#include <Windows.h>
#include <Events.h>

/* A singleton, vertical-list file browser. Window > Browser shows it
   (or brings it to the front if already open). Double-click a file
   to open it in a regular doc window; double-click a folder to dive
   in; double-click the ".." entry to back up one level. */

/* Show the browser. On the first call this opens a Standard File
   picker so the user can choose which folder to browse (we use the
   picked file's enclosing folder as the root). Subsequent calls just
   bring the existing window to front. */
void BrowserShow(void);

/* Toggle the browser's visibility. If never opened, opens it (which
   shows the folder picker). If open and visible, hides it. If open
   and hidden, shows + selects it. Bound to Cmd-/ on the Window menu's
   Browser item. */
void BrowserToggle(void);

/* Window-pointer test (used by event dispatch in main.c). */
Boolean BrowserIsWindow(WindowPtr w);

/* Lifecycle / event entry points. */
void BrowserClose(void);
void BrowserUpdate(void);
void BrowserActivate(Boolean active);
void BrowserClick(EventRecord *ev);
void BrowserKey(EventRecord *ev);
void BrowserResize(void);

/* Periodically polls the current folder's HFS modification date and
   reloads the list when it changes. Call from the main event loop's
   idle handler. Throttled internally so it's safe to call on every
   WaitNextEvent timeout. */
void BrowserIdle(void);

#endif
