#include "find.h"

#include <Quickdraw.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <ToolUtils.h>
#include <Sound.h>
#include <OSUtils.h>
#include <Multiverse.h>

#include "document.h"
#include "pstr.h"

/* DLOG/DITL 135 */
#define kFindDLOG       135
#define kFindBtn        1
#define kReplaceBtn     2
#define kReplaceAllBtn  3
#define kFindText       5
#define kReplaceText    7
#define kMatchCaseBox   8
#define kStatusText     9
#define kDefaultOutline 10

static DialogPtr gFindDlg = NULL;
static UserItemUPP gOutlineUPP = NULL;

/* Kept after the window closes, so Find Again still works. */
static Str255  gFindStr;          /* empty */
static Str255  gReplStr;
static Boolean gMatchCase = false;

static void GetItemHandle(short item, Handle *h, Rect *r)
{
    short type;
    GetDialogItem(gFindDlg, item, &type, h, r);
}

static void SetStatus(ConstStr255Param s)
{
    Handle h;
    Rect r;
    if (gFindDlg == NULL) return;
    GetItemHandle(kStatusText, &h, &r);
    SetDialogItemText(h, s);
}

/* Copy the window's fields into the globals. */
static void ReadFields(void)
{
    Handle h;
    Rect r;
    if (gFindDlg == NULL) return;
    GetItemHandle(kFindText, &h, &r);
    GetDialogItemText(h, gFindStr);
    GetItemHandle(kReplaceText, &h, &r);
    GetDialogItemText(h, gReplStr);
    GetItemHandle(kMatchCaseBox, &h, &r);
    gMatchCase = GetControlValue((ControlHandle)h) != 0;
}

/* Default-button outline around Find (System 6 has no
   SetDialogDefaultItem). */
static pascal void DrawOutline(DialogPtr d, short item)
{
    short type;
    Handle h;
    Rect r;
    PenState ps;
    (void)item;
    GetDialogItem(d, kFindBtn, &type, &h, &r);
    GetPenState(&ps);
    PenSize(3, 3);
    InsetRect(&r, -4, -4);
    FrameRoundRect(&r, 16, 16);
    SetPenState(&ps);
}

static void FlashButton(short item)
{
    Handle h;
    Rect r;
    long t;
    GetItemHandle(item, &h, &r);
    HiliteControl((ControlHandle)h, 1);
    Delay(8, &t);
    HiliteControl((ControlHandle)h, 0);
}

/* Show the found text: the document has to be in front for TextEdit to
   draw its selection. The Find window stays open behind it; Cmd-F
   brings it back. */
static void ShowDoc(DocState *doc)
{
    if (FrontWindow() != doc->window)
        SelectWindow(doc->window);
}

static void DoFind(void)
{
    DocState *doc = DocFrontmost();
    if (doc == NULL || gFindStr[0] == 0) { SysBeep(1); return; }
    if (DocFindNext(doc, gFindStr, gMatchCase)) {
        SetStatus("\p");
        ShowDoc(doc);
    } else {
        SetStatus("\pNot found.");
        SysBeep(1);
    }
}

static void DoReplace(void)
{
    DocState *doc = DocFrontmost();
    if (doc == NULL || gFindStr[0] == 0) { SysBeep(1); return; }
    (void) DocReplaceSelection(doc, gFindStr, gReplStr, gMatchCase);
    DoFind();
}

static void DoReplaceAll(void)
{
    DocState *doc = DocFrontmost();
    long n;
    Str255 msg;
    if (doc == NULL || gFindStr[0] == 0) { SysBeep(1); return; }
    n = DocReplaceAll(doc, gFindStr, gReplStr, gMatchCase);
    if (n < 0) {
        SetStatus("\pToo long to fit.");
        SysBeep(1);
    } else if (n == 0) {
        SetStatus("\pNot found.");
        SysBeep(1);
    } else {
        NumToString(n, msg);
        {
            const unsigned char *tail = (n == 1) ? "\p replacement." : "\p replacements.";
            short i;
            for (i = 1; i <= tail[0]; i++) msg[++msg[0]] = tail[i];
        }
        SetStatus(msg);
    }
}

/* ---- public ---- */

Boolean FindHasPattern(void)
{
    return gFindStr[0] != 0;
}

Boolean FindIsWindow(WindowPtr w)
{
    return gFindDlg != NULL && w == (WindowPtr)gFindDlg;
}

Boolean FindWindowIsFront(void)
{
    return gFindDlg != NULL && FrontWindow() == (WindowPtr)gFindDlg;
}

void FindWindowClose(void)
{
    if (gFindDlg == NULL) return;
    ReadFields();
    DisposeDialog(gFindDlg);
    gFindDlg = NULL;
}

void FindShowWindow(void)
{
    DocState *doc = DocFrontmost();
    Handle h;
    Rect r;
    short type;

    /* Seed with a short single-line selection, like most Mac editors. */
    if (doc != NULL) {
        short s0 = (**doc->te).selStart, s1 = (**doc->te).selEnd;
        if (s1 > s0 && s1 - s0 <= 255) {
            CharsHandle ch = TEGetText(doc->te);
            Boolean oneLine = true;
            short i;
            for (i = s0; i < s1; i++)
                if ((*ch)[i] == '\r') { oneLine = false; break; }
            if (oneLine) {
                gFindStr[0] = (unsigned char)(s1 - s0);
                BlockMoveData(*ch + s0, gFindStr + 1, s1 - s0);
                if (gFindDlg != NULL) {
                    GetItemHandle(kFindText, &h, &r);
                    SetDialogItemText(h, gFindStr);
                }
            }
        }
    }

    if (gFindDlg == NULL) {
        gFindDlg = GetNewDialog(kFindDLOG, NULL, (WindowPtr)-1L);
        if (gFindDlg == NULL) return;
        GetItemHandle(kFindText, &h, &r);
        SetDialogItemText(h, gFindStr);
        GetItemHandle(kReplaceText, &h, &r);
        SetDialogItemText(h, gReplStr);
        GetItemHandle(kMatchCaseBox, &h, &r);
        SetControlValue((ControlHandle)h, gMatchCase ? 1 : 0);
        if (gOutlineUPP == NULL)
            gOutlineUPP = NewUserItemProc(DrawOutline);
        GetDialogItem(gFindDlg, kDefaultOutline, &type, &h, &r);
        SetDialogItem(gFindDlg, kDefaultOutline, type, (Handle)gOutlineUPP, &r);
        ShowWindow((WindowPtr)gFindDlg);
    }
    SetStatus("\p");
    SelectWindow((WindowPtr)gFindDlg);
    SelectDialogItemText(gFindDlg, kFindText, 0, 32767);
}

void FindAgain(void)
{
    ReadFields();
    if (gFindStr[0] == 0) { FindShowWindow(); return; }
    DoFind();
}

void FindReplaceAndFindAgain(void)
{
    ReadFields();
    if (gFindStr[0] == 0) { FindShowWindow(); return; }
    DoReplace();
}

void FindIdle(void)
{
    if (FindWindowIsFront())
        TEIdle(((DialogPeek)gFindDlg)->textH);
}

TEHandle FindFieldTE(void)
{
    if (!FindWindowIsFront() || ((DialogPeek)gFindDlg)->editField < 0)
        return NULL;
    return ((DialogPeek)gFindDlg)->textH;
}

Boolean FindHandleEvent(EventRecord *ev)
{
    DialogPtr hit;
    short item;

    if (gFindDlg == NULL)
        return false;
    if ((ev->what == keyDown || ev->what == autoKey) && (ev->modifiers & cmdKey))
        return false;
    if (ev->what == mouseDown) {
        WindowPtr w;
        if (FindWindow(ev->where, &w) != inContent || w != (WindowPtr)gFindDlg
            || w != FrontWindow())
            return false;
    }
    if ((ev->what == keyDown || ev->what == autoKey) && FindWindowIsFront()) {
        char c = (char)(ev->message & charCodeMask);
        if (c == '\r' || c == 0x03) {           /* Return / Enter = Find */
            FlashButton(kFindBtn);
            ReadFields();
            DoFind();
            return true;
        }
        if (c == 0x1B) {                         /* Esc closes */
            FindWindowClose();
            return true;
        }
    }
    if (!IsDialogEvent(ev))
        return false;

    if (DialogSelect(ev, &hit, &item) && hit == gFindDlg) {
        switch (item) {
        case kFindBtn:
            ReadFields();
            DoFind();
            break;
        case kReplaceBtn:
            ReadFields();
            DoReplace();
            break;
        case kReplaceAllBtn:
            ReadFields();
            DoReplaceAll();
            break;
        case kMatchCaseBox: {
            Handle h;
            Rect r;
            GetItemHandle(kMatchCaseBox, &h, &r);
            SetControlValue((ControlHandle)h, 1 - GetControlValue((ControlHandle)h));
            ReadFields();
            break;
        }
        }
    }
    return true;
}
