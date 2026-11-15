#ifndef MacDown_PREFS_H
#define MacDown_PREFS_H

#include <Types.h>

/* Persistent preferences, stored in "MacDown Prefs" in the Preferences
   folder (System 7) or the System Folder (System 6).

   Folder locations are saved as (volume name, volume creation date,
   dirID) rather than a vRefNum, which is only valid for the current
   boot. Getters re-resolve and re-check the folder every time, so they
   return false if the volume isn't mounted or the folder is gone, and
   callers fall back to their defaults. */

/* Read the prefs file. Call once at startup; missing or unreadable
   prefs just leave every location unset. */
void PrefsLoad(void);

/* Write the prefs file. Call at quit. */
void PrefsSave(void);

/* Folder the Open dialog last opened a file from. */
Boolean PrefsGetOpenFolder(short *outVRef, long *outDirID);
void    PrefsSetOpenFolder(short vRefNum, long dirID);

/* Folder the browser was last showing. */
Boolean PrefsGetBrowserFolder(short *outVRef, long *outDirID);
void    PrefsSetBrowserFolder(short vRefNum, long dirID);

/* Most-recently-opened files, newest first (File > Open Recent). */
#define kPrefsMaxRecent 5

short   PrefsRecentCount(void);
/* Name of entry `index` (0-based) for display; no disk access. */
void    PrefsRecentName(short index, StringPtr outName);
/* Resolve entry `index` to its current folder; false if the volume
   isn't mounted or the folder is gone. */
Boolean PrefsRecentGet(short index, short *outVRef, long *outDirID,
                       StringPtr outName);
/* Move (or insert) a file to the top of the list. */
void    PrefsRecentAdd(short vRefNum, long dirID, ConstStr255Param name);
void    PrefsRecentRemove(short index);
void    PrefsRecentClear(void);

/* Edit > Preferences: reopen the last session's documents and browser
   at launch (default on). */
Boolean PrefsGetRestoreSession(void);
void    PrefsSetRestoreSession(Boolean on);

/* Documents open at the last quit, in back-to-front window order so
   reopening them in sequence leaves the old front window in front,
   each with its window's global content rect (empty if not known;
   globalBounds may be NULL). */
#define kPrefsMaxOpenDocs 8
void    PrefsOpenDocsClear(void);
void    PrefsOpenDocsAdd(short vRefNum, long dirID, ConstStr255Param name,
                         const Rect *globalBounds);
short   PrefsOpenDocCount(void);
Boolean PrefsOpenDocGet(short index, short *outVRef, long *outDirID,
                        StringPtr outName, Rect *outGlobalBounds);

/* Browser window: whether it was showing at quit, and its content rect
   in global coordinates. */
void    PrefsSetBrowserWindow(Boolean open, const Rect *globalBounds);
Boolean PrefsGetBrowserOpen(void);
Boolean PrefsGetBrowserBounds(Rect *outGlobalBounds);

/* True if (vRefNum, dirID) names an existing directory right now. */
Boolean PrefsFolderExists(short vRefNum, long dirID);

#endif
