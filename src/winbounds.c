#include "winbounds.h"

#include <Quickdraw.h>
#include <LowMem.h>

Boolean WindowGlobalBounds(WindowPtr w, Rect *out)
{
    GrafPtr saved;
    Point tl;
    if (w == NULL) return false;
    GetPort(&saved);
    SetPort(w);
    tl.h = w->portRect.left;
    tl.v = w->portRect.top;
    LocalToGlobal(&tl);
    SetPort(saved);
    out->left   = tl.h;
    out->top    = tl.v;
    out->right  = tl.h + (w->portRect.right - w->portRect.left);
    out->bottom = tl.v + (w->portRect.bottom - w->portRect.top);
    return true;
}

Boolean WindowBoundsUsable(const Rect *r, short minW, short minH)
{
    Rect titleBar, desk, hit;
    RgnHandle gray = LMGetGrayRgn();
    if (r->right - r->left < minW || r->bottom - r->top < minH) return false;
    if (gray == NULL) return false;
    desk = (**gray).rgnBBox;
    SetRect(&titleBar, r->left + 8, r->top - 16, r->right - 8, r->top - 2);
    if (!SectRect(&titleBar, &desk, &hit)) return false;
    if (hit.right - hit.left < 32) return false;
    return RectInRgn(&titleBar, gray);
}

void WindowApplyBounds(WindowPtr w, const Rect *r)
{
    MoveWindow(w, r->left, r->top, false);
    SizeWindow(w, r->right - r->left, r->bottom - r->top, false);
}
