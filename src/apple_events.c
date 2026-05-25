#include "apple_events.h"

#include <AppleEvents.h>
#include <Files.h>
#include <Gestalt.h>
#include <Memory.h>
#include <Multiverse.h>

#include "document.h"

/* Multiversal Interfaces doesn't expose keyMissedKeywordAttr by name. */
#ifndef keyMissedKeywordAttr
#define keyMissedKeywordAttr  ((AEKeyword)'miss')
#endif

Boolean gAEAvailable = false;
Boolean gQuitRequested = false;

static pascal OSErr HandleOAPP(const AppleEvent *ev, AppleEvent *reply, long ref);
static pascal OSErr HandleODOC(const AppleEvent *ev, AppleEvent *reply, long ref);
static pascal OSErr HandlePDOC(const AppleEvent *ev, AppleEvent *reply, long ref);
static pascal OSErr HandleQUIT(const AppleEvent *ev, AppleEvent *reply, long ref);

/* Mandatory check that we extracted every direct parameter — required
   by Apple Event etiquette. Returns errAEEventNotHandled if anything
   was left over so the sender knows. */
static OSErr GotRequiredParams(const AppleEvent *ev)
{
    DescType type;
    Size sz;
    OSErr err = AEGetAttributePtr((AppleEvent *)ev, keyMissedKeywordAttr,
                                  typeWildCard, &type, NULL, 0, &sz);
    if (err == errAEDescNotFound) return noErr;
    if (err == noErr) return errAEEventNotHandled;
    return err;
}

void AEInit(void)
{
    long response;
    OSErr err;

    if (Gestalt(gestaltAppleEventsAttr, &response) != noErr) {
        gAEAvailable = false;
        return;
    }
    gAEAvailable = true;

    err = AEInstallEventHandler(kCoreEventClass, kAEOpenApplication,
                                NewAEEventHandlerUPP(HandleOAPP), 0L, false);
    if (err != noErr) gAEAvailable = false;

    err = AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments,
                                NewAEEventHandlerUPP(HandleODOC), 0L, false);
    if (err != noErr) gAEAvailable = false;

    err = AEInstallEventHandler(kCoreEventClass, kAEPrintDocuments,
                                NewAEEventHandlerUPP(HandlePDOC), 0L, false);

    err = AEInstallEventHandler(kCoreEventClass, kAEQuitApplication,
                                NewAEEventHandlerUPP(HandleQUIT), 0L, false);
}

void AEDispatch(EventRecord *ev)
{
    if (gAEAvailable) {
        AEProcessAppleEvent(ev);
    }
}

/* ---- Handlers ---- */

static pascal OSErr HandleOAPP(const AppleEvent *ev, AppleEvent *reply, long ref)
{
    (void)reply; (void)ref;
    return GotRequiredParams(ev);
}

static pascal OSErr HandleODOC(const AppleEvent *ev, AppleEvent *reply, long ref)
{
    AEDescList docList;
    OSErr err;
    long count, i;
    AEKeyword kw;
    DescType typ;
    FSSpec fs;
    Size actualSize;

    (void)reply; (void)ref;

    err = AEGetParamDesc((AppleEvent *)ev, keyDirectObject, typeAEList,
                         &docList);
    if (err != noErr) return err;

    err = GotRequiredParams(ev);
    if (err != noErr) { AEDisposeDesc(&docList); return err; }

    AECountItems(&docList, &count);
    /* One window per opened file. */
    for (i = 1; i <= count; i++) {
        err = AEGetNthPtr(&docList, i, typeFSS, &kw, &typ,
                          &fs, sizeof(FSSpec), &actualSize);
        if (err == noErr) DocOpen(fs.vRefNum, fs.name);
    }

    AEDisposeDesc(&docList);
    return noErr;
}

static pascal OSErr HandlePDOC(const AppleEvent *ev, AppleEvent *reply, long ref)
{
    (void)reply; (void)ref;
    /* Print not supported. Acknowledge but no-op. */
    return GotRequiredParams(ev);
}

static pascal OSErr HandleQUIT(const AppleEvent *ev, AppleEvent *reply, long ref)
{
    OSErr err;
    (void)reply; (void)ref;
    err = GotRequiredParams(ev);
    if (err != noErr) return err;
    /* Walk every doc and prompt for unsaved changes. DocCloseAll
       returns false if the user hits Cancel on any prompt. */
    if (DocCloseAll()) gQuitRequested = true;
    return noErr;
}
