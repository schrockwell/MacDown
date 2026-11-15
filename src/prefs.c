#include "prefs.h"

#include <Files.h>
#include <Gestalt.h>
#include <OSUtils.h>
#include <Multiverse.h>
#include <stddef.h>

#include "file_io.h"
#include "pstr.h"

#ifndef kOnSystemDisk
#define kOnSystemDisk ((short)0x8000)
#endif
#ifndef kCreateFolder
#define kCreateFolder true
#endif
#ifndef gestaltFindFolderPresent
#define gestaltFindFolderPresent 0
#endif

#define kPrefsMagic   'MDpf'
#define kPrefsVersion 2
#define kPrefsType    'pref'
#define kPrefsCreator 'MDED'

static const unsigned char kPrefsName[] = "\pMacDown Prefs";

/* A folder location that survives a reboot. */
typedef struct {
    Boolean valid;
    char    pad;
    long    volCrDate;   /* volume creation date: tells same-named volumes apart */
    long    dirID;
    unsigned char volName[28];   /* Str27: HFS volume names max 27 chars */
} StoredFolder;

typedef struct {
    StoredFolder folder;
    Str63        name;
} StoredFile;

typedef struct {
    OSType       magic;
    short        version;
    StoredFolder openFolder;
    StoredFolder browserFolder;
    short        recentCount;
    StoredFile   recent[kPrefsMaxRecent];
    /* ---- version 2 ---- (version 1 files end here) */
    Boolean      restoreSession;   /* reopen docs + browser at launch */
    Boolean      browserOpen;      /* browser was showing at last quit */
    Boolean      browserBoundsValid;
    char         pad2;
    Rect         browserBounds;    /* global content rect */
    short        openDocCount;     /* back-to-front window order */
    StoredFile   openDocs[kPrefsMaxOpenDocs];
} PrefsRecord;

/* Bytes a version 1 prefs file holds (a prefix of the v2 layout). */
#define kPrefsV1Size offsetof(PrefsRecord, restoreSession)

static PrefsRecord gPrefs;

/* ---- folder <-> stored location ---- */

Boolean PrefsFolderExists(short vRefNum, long dirID)
{
    CInfoPBRec pb;
    Str63 nm;
    if (dirID == 0) return false;
    ZeroStruct(pb);
    nm[0] = 0;
    pb.dirInfo.ioNamePtr   = (StringPtr)nm;
    pb.dirInfo.ioVRefNum   = vRefNum;
    pb.dirInfo.ioFDirIndex = -1;          /* look up by dirID alone */
    pb.dirInfo.ioDrDirID   = dirID;
    if (PBGetCatInfoSync(&pb) != noErr) return false;
    return (pb.dirInfo.ioFlAttrib & 0x10) != 0;   /* is a directory */
}

static Boolean StoreFolder(short vRefNum, long dirID, StoredFolder *out)
{
    HParamBlockRec pb;
    Str255 volName;
    OSErr err;

    out->valid = false;
    if (dirID == 0) return false;

    ZeroStruct(pb);
    volName[0] = 0;
    pb.volumeParam.ioNamePtr  = volName;
    pb.volumeParam.ioVRefNum  = vRefNum;
    pb.volumeParam.ioVolIndex = 0;        /* look up by ioVRefNum */
    err = PBHGetVInfoSync(&pb);
    if (err != noErr || volName[0] == 0 || volName[0] > 27) return false;

    PStrCopy(out->volName, volName);
    out->volCrDate = (long)pb.volumeParam.ioVCrDate;
    out->dirID     = dirID;
    out->valid     = true;
    return true;
}

/* Find the mounted volume matching `s` and check the folder is still
   there. vRefNums are reassigned every boot, so match on name +
   creation date instead. */
static Boolean ResolveFolder(const StoredFolder *s, short *outVRef, long *outDirID)
{
    short index;
    if (!s->valid || s->dirID == 0 || s->volName[0] == 0) return false;

    for (index = 1; ; index++) {
        HParamBlockRec pb;
        Str255 volName;
        ZeroStruct(pb);
        volName[0] = 0;
        pb.volumeParam.ioNamePtr  = volName;
        pb.volumeParam.ioVolIndex = index;
        if (PBHGetVInfoSync(&pb) != noErr) break;   /* nsvErr: no more volumes */

        if ((long)pb.volumeParam.ioVCrDate != s->volCrDate) continue;
        if (!EqualString(volName, s->volName, false, true)) continue;

        if (!PrefsFolderExists(pb.volumeParam.ioVRefNum, s->dirID)) return false;
        *outVRef  = pb.volumeParam.ioVRefNum;
        *outDirID = s->dirID;
        return true;
    }
    return false;
}

/* ---- prefs file location ---- */

static Boolean FindPrefsFolder(short *outVRef, long *outDirID)
{
    long resp;

    /* System 7: the real Preferences folder. */
    if (Gestalt(gestaltFindFolderAttr, &resp) == noErr
        && (resp & (1L << gestaltFindFolderPresent)))
    {
        short v;
        long  d;
        if (FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder,
                       &v, &d) == noErr) {
            *outVRef = v;
            *outDirID = d;
            return true;
        }
    }

    /* System 6: no Preferences folder; use the System Folder. */
    {
        SysEnvRec se;
        if (SysEnvirons(2, &se) == noErr
            && FileIOResolveWD(se.sysVRefNum, outVRef, outDirID) == noErr)
            return true;
    }
    return false;
}

/* ---- load / save ---- */

void PrefsLoad(void)
{
    short vRef, refNum;
    long  dirID, count;
    PrefsRecord rec;
    OSErr err;

    ZeroStruct(gPrefs);
    gPrefs.magic   = kPrefsMagic;
    gPrefs.version = kPrefsVersion;
    gPrefs.restoreSession = true;

    if (!FindPrefsFolder(&vRef, &dirID)) return;

    err = FileIOOpen(vRef, dirID, kPrefsName, fsRdPerm, &refNum);
    if (err != noErr) return;
    count = sizeof(rec);
    err = FSRead(refNum, &count, &rec);
    FSClose(refNum);

    if ((err == noErr || err == eofErr) && rec.magic == kPrefsMagic)
    {
        if (rec.version == kPrefsVersion && count == sizeof(rec)) {
            gPrefs = rec;
        } else if (rec.version == 1 && count >= (long)kPrefsV1Size) {
            /* Older file: keep its fields, defaults for the rest. */
            BlockMoveData(&rec, &gPrefs, kPrefsV1Size);
            gPrefs.version = kPrefsVersion;
        }
        if (gPrefs.recentCount < 0) gPrefs.recentCount = 0;
        if (gPrefs.recentCount > kPrefsMaxRecent)
            gPrefs.recentCount = kPrefsMaxRecent;
        if (gPrefs.openDocCount < 0) gPrefs.openDocCount = 0;
        if (gPrefs.openDocCount > kPrefsMaxOpenDocs)
            gPrefs.openDocCount = kPrefsMaxOpenDocs;
    }
}

void PrefsSave(void)
{
    short vRef, refNum;
    long  dirID, count;
    OSErr err;

    if (!FindPrefsFolder(&vRef, &dirID)) return;

    err = FileIOOpen(vRef, dirID, kPrefsName, fsRdWrPerm, &refNum);
    if (err == fnfErr) {
        if (FileIOCreate(vRef, dirID, kPrefsName, kPrefsCreator, kPrefsType) == noErr)
            err = FileIOOpen(vRef, dirID, kPrefsName, fsRdWrPerm, &refNum);
    }
    if (err != noErr) return;

    count = sizeof(gPrefs);
    if (SetEOF(refNum, count) == noErr
        && SetFPos(refNum, fsFromStart, 0) == noErr)
        (void)FSWrite(refNum, &count, &gPrefs);
    FSClose(refNum);
    (void)FlushVol(NULL, vRef);
}

/* ---- accessors ---- */

Boolean PrefsGetOpenFolder(short *outVRef, long *outDirID)
{
    return ResolveFolder(&gPrefs.openFolder, outVRef, outDirID);
}

void PrefsSetOpenFolder(short vRefNum, long dirID)
{
    StoredFolder s;
    if (StoreFolder(vRefNum, dirID, &s)) gPrefs.openFolder = s;
}

Boolean PrefsGetBrowserFolder(short *outVRef, long *outDirID)
{
    return ResolveFolder(&gPrefs.browserFolder, outVRef, outDirID);
}

void PrefsSetBrowserFolder(short vRefNum, long dirID)
{
    StoredFolder s;
    if (StoreFolder(vRefNum, dirID, &s)) gPrefs.browserFolder = s;
}

/* ---- recent files ---- */

short PrefsRecentCount(void)
{
    return gPrefs.recentCount;
}

void PrefsRecentName(short index, StringPtr outName)
{
    outName[0] = 0;
    if (index < 0 || index >= gPrefs.recentCount) return;
    PStrCopy(outName, gPrefs.recent[index].name);
}

Boolean PrefsRecentGet(short index, short *outVRef, long *outDirID,
                       StringPtr outName)
{
    if (index < 0 || index >= gPrefs.recentCount) return false;
    if (!ResolveFolder(&gPrefs.recent[index].folder, outVRef, outDirID))
        return false;
    PStrCopy(outName, gPrefs.recent[index].name);
    return true;
}

static Boolean SameStoredFile(const StoredFile *a, const StoredFile *b)
{
    return a->folder.dirID == b->folder.dirID
        && a->folder.volCrDate == b->folder.volCrDate
        && EqualString(a->folder.volName, b->folder.volName, false, true)
        && EqualString(a->name, b->name, false, true);
}

void PrefsRecentAdd(short vRefNum, long dirID, ConstStr255Param name)
{
    StoredFile f;
    short i, found;

    if (name[0] == 0 || name[0] > 63) return;
    if (!StoreFolder(vRefNum, dirID, &f.folder)) return;
    PStrCopy(f.name, name);

    /* Drop an existing entry for the same file (or the oldest entry if
       the list is full), then shift everything down and put it first. */
    found = -1;
    for (i = 0; i < gPrefs.recentCount; i++) {
        if (SameStoredFile(&gPrefs.recent[i], &f)) { found = i; break; }
    }
    if (found < 0) {
        found = (gPrefs.recentCount < kPrefsMaxRecent)
                    ? gPrefs.recentCount++
                    : kPrefsMaxRecent - 1;
    }
    for (i = found; i > 0; i--)
        gPrefs.recent[i] = gPrefs.recent[i - 1];
    gPrefs.recent[0] = f;
}

void PrefsRecentRemove(short index)
{
    short i;
    if (index < 0 || index >= gPrefs.recentCount) return;
    for (i = index; i < gPrefs.recentCount - 1; i++)
        gPrefs.recent[i] = gPrefs.recent[i + 1];
    gPrefs.recentCount--;
}

void PrefsRecentClear(void)
{
    gPrefs.recentCount = 0;
}

/* ---- session restore ---- */

Boolean PrefsGetRestoreSession(void)
{
    return gPrefs.restoreSession;
}

void PrefsSetRestoreSession(Boolean on)
{
    gPrefs.restoreSession = on;
}

void PrefsOpenDocsClear(void)
{
    gPrefs.openDocCount = 0;
}

void PrefsOpenDocsAdd(short vRefNum, long dirID, ConstStr255Param name)
{
    StoredFile f;
    if (gPrefs.openDocCount >= kPrefsMaxOpenDocs) return;
    if (name[0] == 0 || name[0] > 63) return;
    if (!StoreFolder(vRefNum, dirID, &f.folder)) return;
    PStrCopy(f.name, name);
    gPrefs.openDocs[gPrefs.openDocCount++] = f;
}

short PrefsOpenDocCount(void)
{
    return gPrefs.openDocCount;
}

Boolean PrefsOpenDocGet(short index, short *outVRef, long *outDirID,
                        StringPtr outName)
{
    if (index < 0 || index >= gPrefs.openDocCount) return false;
    if (!ResolveFolder(&gPrefs.openDocs[index].folder, outVRef, outDirID))
        return false;
    PStrCopy(outName, gPrefs.openDocs[index].name);
    return true;
}

void PrefsSetBrowserWindow(Boolean open, const Rect *globalBounds)
{
    gPrefs.browserOpen = open;
    if (globalBounds != NULL) {
        gPrefs.browserBounds = *globalBounds;
        gPrefs.browserBoundsValid = true;
    }
}

Boolean PrefsGetBrowserOpen(void)
{
    return gPrefs.browserOpen;
}

Boolean PrefsGetBrowserBounds(Rect *outGlobalBounds)
{
    if (!gPrefs.browserBoundsValid) return false;
    *outGlobalBounds = gPrefs.browserBounds;
    return true;
}
