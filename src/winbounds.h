#ifndef MDEDIT_WINBOUNDS_H
#define MDEDIT_WINBOUNDS_H

#include <Windows.h>

/* A window's content rect in global coordinates (what MoveWindow /
   SizeWindow take, so it round-trips through the prefs file). */
Boolean WindowGlobalBounds(WindowPtr w, Rect *out);

/* True if a saved content rect is at least minW x minH and enough of
   its title bar lands on a screen that the user can still drag it
   (e.g. not after switching to a smaller monitor). */
Boolean WindowBoundsUsable(const Rect *r, short minW, short minH);

/* Move and size w to a saved global content rect. Doesn't redraw. */
void WindowApplyBounds(WindowPtr w, const Rect *r);

#endif
