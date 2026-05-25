#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <Events.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <Devices.h>
#include <ToolUtils.h>

#define kWindowID    128
#define kMenuBarID   128
#define kAppleMenuID 128
#define kFileMenuID  129
#define kAboutItem   1
#define kQuitItem    1

static Boolean gDone = false;

static void DrawHello(WindowPtr w)
{
    Rect r = w->portRect;
    short x, y;

    EraseRect(&r);
    TextFont(systemFont);
    TextSize(12);
    /* Rough centering — DrawString uses the current pen position
       as the baseline left edge. Width estimate: 80 px for our string. */
    x = (r.left + r.right) / 2 - 40;
    y = (r.top + r.bottom) / 2 + 4;
    MoveTo(x, y);
    DrawString("\pHello, world!");
}

static void HandleMenu(long menuChoice)
{
    short menuID = HiWord(menuChoice);
    short item   = LoWord(menuChoice);

    if (menuID == kAppleMenuID) {
        if (item != kAboutItem) {
            /* Desk accessory selected from Apple menu. */
            Str255 daName;
            GetMenuItemText(GetMenuHandle(kAppleMenuID), item, daName);
            OpenDeskAcc(daName);
        }
        /* About item: we have no alert resource — do nothing visible. */
    } else if (menuID == kFileMenuID && item == kQuitItem) {
        gDone = true;
    }
    HiliteMenu(0);
}

static void HandleMouse(EventRecord *event)
{
    WindowPtr w;
    short part = FindWindow(event->where, &w);

    switch (part) {
        case inMenuBar: {
            long choice = MenuSelect(event->where);
            HandleMenu(choice);
            break;
        }
        case inSysWindow:
            SystemClick(event, w);
            break;
        case inDrag: {
            /* Constrain dragging to the screen minus the menu bar. */
            Rect bounds = qd.screenBits.bounds;
            bounds.top += 20;
            DragWindow(w, event->where, &bounds);
            break;
        }
        case inGoAway:
            if (TrackGoAway(w, event->where)) gDone = true;
            break;
        case inContent:
            if (w != FrontWindow()) SelectWindow(w);
            break;
    }
}

static void HandleKey(EventRecord *event)
{
    char c = event->message & charCodeMask;
    if (event->modifiers & cmdKey) {
        long choice = MenuKey(c);
        if (HiWord(choice) != 0) HandleMenu(choice);
    }
}

int main(void)
{
    Handle      menuBar;
    WindowPtr   win;
    EventRecord event;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);

    menuBar = GetNewMBar(kMenuBarID);
    SetMenuBar(menuBar);
    AppendResMenu(GetMenuHandle(kAppleMenuID), 'DRVR');
    DrawMenuBar();

    win = GetNewWindow(kWindowID, NULL, (WindowPtr)-1L);
    SetPort(win);

    while (!gDone) {
        if (WaitNextEvent(everyEvent, &event, 30L, NULL)) {
            switch (event.what) {
                case mouseDown:
                    HandleMouse(&event);
                    break;
                case keyDown:
                case autoKey:
                    HandleKey(&event);
                    break;
                case updateEvt: {
                    WindowPtr uw = (WindowPtr) event.message;
                    SetPort(uw);
                    BeginUpdate(uw);
                    DrawHello(uw);
                    EndUpdate(uw);
                    break;
                }
            }
        }
    }

    return 0;
}
