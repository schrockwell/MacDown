#ifndef MDEDIT_APPLE_EVENTS_H
#define MDEDIT_APPLE_EVENTS_H

#include <Events.h>

/* Install the four required handlers if Apple Events is available.
   Sets the global gAEAvailable flag. Safe to call on System 6 — it
   just no-ops. */
void AEInit(void);

extern Boolean gAEAvailable;
extern Boolean gQuitRequested;

/* Dispatch a kHighLevelEvent through AEProcessAppleEvent. */
void AEDispatch(EventRecord *ev);

#endif
