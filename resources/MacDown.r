#include "Windows.r"
#include "Menus.r"
#include "Dialogs.r"
#include "Processes.r"
#include "Finder.r"
#include "Types.r"

/* ---- Main document window ---- */

resource 'WIND' (128, "MacDown") {
    { 80, 56, 270, 456 },    /* 400x190 — small enough to leave room, top=80 */
    zoomDocProc,
    visible,
    goAway,
    0x0,
    "untitled",
    noAutoCenter
};

/* ---- Menu bar ---- */

resource 'MBAR' (128) {
    { 128, 129, 130, 131, 133, 132 }
};

resource 'MENU' (128, "Apple") {
    128, textMenuProc, allEnabled, enabled,
    apple,
    {
        "About MacDown\0xC9",         noIcon, noKey,  noMark, plain;
        "Keyboard Shortcuts\0xC9",   noIcon, noKey,  noMark, plain;
        "-",                         noIcon, noKey,  noMark, plain;
    }
};

resource 'MENU' (129, "File") {
    129, textMenuProc, allEnabled, enabled,
    "File",
    {
        "New",                    noIcon, "N",    noMark, plain;
        "Open\0xC9",              noIcon, "O",    noMark, plain;
        "Close",                  noIcon, "W",    noMark, plain;
        "Save",                   noIcon, "S",    noMark, plain;
        "Save As\0xC9",           noIcon, noKey,  noMark, plain;
        "-",                      noIcon, noKey,  noMark, plain;
        "Quit",                   noIcon, "Q",    noMark, plain;
    }
};

resource 'MENU' (130, "Edit") {
    130, textMenuProc, allEnabled, enabled,
    "Edit",
    {
        "Undo",                       noIcon, "Z",    noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
        "Cut",                        noIcon, "X",    noMark, plain;
        "Copy",                       noIcon, "C",    noMark, plain;
        "Paste",                      noIcon, "V",    noMark, plain;
        "Clear",                      noIcon, noKey,  noMark, plain;
        "Select All",                 noIcon, "A",    noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
        "Duplicate Line",             noIcon, "D",    noMark, plain;
    }
};

resource 'MENU' (131, "Format") {
    131, textMenuProc, allEnabled, enabled,
    "Format",
    {
        "Bold",                       noIcon, "B",    noMark, plain;
        "Italic",                     noIcon, "I",    noMark, plain;
        "Code",                       noIcon, noKey,  noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
        "Link",                       noIcon, "K",    noMark, plain;
    }
};

resource 'MENU' (133, "Line") {
    133, textMenuProc, allEnabled, enabled,
    "Line",
    {
        "Heading 1",                  noIcon, "1",    noMark, plain;
        "Heading 2",                  noIcon, "2",    noMark, plain;
        "Heading 3",                  noIcon, "3",    noMark, plain;
        "Heading 4",                  noIcon, "4",    noMark, plain;
        "Heading 5",                  noIcon, "5",    noMark, plain;
        "Heading 6",                  noIcon, "6",    noMark, plain;
        "No Heading",                 noIcon, "0",    noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
        "Task",                       noIcon, "L",    noMark, plain;
        "Blockquote",                 noIcon, noKey,  noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
        "Indent",                     noIcon, "]",    noMark, plain;
        "Outdent",                    noIcon, "[",    noMark, plain;
    }
};

resource 'MENU' (132, "Window") {
    132, textMenuProc, allEnabled, enabled,
    "Window",
    {
        "Browser",                    noIcon, "/",    noMark, plain;
        "Next Window (Cmd-`)",        noIcon, noKey,  noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
    }
};

/* ---- About box ---- A plain-bordered window (no title bar, no
   close box) dismissed by any click or keypress. Drawn by hand at
   runtime — see DoAbout() in src/main.c. */

resource 'WIND' (129, "About") {
    { 90, 80, 230, 432 },
    dBoxProc,
    visible,
    noGoAway,
    0x0,
    "",
    noAutoCenter
};

resource 'WIND' (130, "Shortcuts") {
    { 60, 60, 240, 420 },
    dBoxProc,
    visible,
    noGoAway,
    0x0,
    "",
    noAutoCenter
};

/* ---- Browser window ---- */

resource 'WIND' (131, "Browser") {
    { 40, 20, 250, 200 },     /* 210 tall x 180 wide, top-left of screen */
    zoomDocProc,
    visible,
    goAway,
    0x0,
    "Browser",
    noAutoCenter
};

/* ---- Save changes? prompt ---- */

resource 'ALRT' (129, "Save Changes") {
    { 80, 80, 200, 420 },
    129,
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'DITL' (129, "Save Changes") {
    {
        { 80, 232, 100, 312 },
        Button { enabled, "Save" };

        { 80, 12, 100, 92 },
        Button { enabled, "Don\0xD5t Save" };

        { 80, 122, 100, 202 },
        Button { enabled, "Cancel" };

        { 12, 64, 64, 308 },
        StaticText { disabled,
            "Save changes to this document before closing?" };
    }
};

/* ---- New Folder prompt (browser toolbar "+" button) ----

   Item 5 is a userItem we paint with a 3-px rounded outline around the
   Create button at runtime — the classic-Mac convention for marking a
   default button on dialogs without using System 7's
   SetDialogDefaultItem (which doesn't exist on 6.0.8). */

resource 'DLOG' (130, "New Folder") {
    { 80, 80, 200, 380 },
    movableDBoxProc,
    visible,
    noGoAway,
    0x0,
    130,
    "",
    centerMainScreen
};

resource 'DITL' (130, "New Folder") {
    {
        { 84, 220, 104, 280 },
        Button { enabled, "Create" };

        { 84, 140, 104, 200 },
        Button { enabled, "Cancel" };

        { 12, 12, 32, 288 },
        StaticText { disabled, "Folder name:" };

        { 40, 12, 60, 288 },
        EditText { enabled, "Untitled folder" };

        { 80, 216, 108, 284 },
        UserItem { disabled };
    }
};

/* ---- Error / info strings ---- */

resource 'STR#' (128, "Errors") {
    {
        "Couldn\0xD5t open the file.";
        "File is too large to edit (TextEdit\0xD5s 32K limit).";
        "Some characters were converted to MacRoman.";
        "Couldn\0xD5t save the file.";
        "Couldn\0xD5t create the file.";
    }
};

/* ---- Finder bundle: app icon + .TEXT document association ---- */

resource 'BNDL' (128) {
    'MDED',
    0,
    {
        'ICN#', { 0, 128, 1, 129 };
        'FREF', { 0, 128, 1, 129 };
    }
};

resource 'FREF' (128) {
    'APPL', 0, ""
};

resource 'FREF' (129) {
    'TEXT', 1, ""
};

/* App signature for the Finder. Pair with creator 'MDED' set on output. */
type 'MDED' as 'STR ';
resource 'MDED' (0, "Owner resource") {
    "MacDown 1.0"
};

/* App / document icons — generated from app.png / document.png by
   scripts/png-to-icn.py. The mask is a flood-filled silhouette so the
   white interior of the page outline stays opaque (no desktop bleed
   through) without needing a PNG alpha channel. */

resource 'ICN#' (128, "App") {
    {
        /* data */
        $"00010000 00028000 00044000 00082000"
        $"00101000 00240800 00420400 00812200"
        $"01019100 02025080 04022040 08114020"
        $"10288010 20650008 40923F04 81484082"
        $"41208041 20913022 1001C814 080E7F8F"
        $"04023007 02010007 01008007 00806007"
        $"00401FE7 0020021F 00100407 00080800"
        $"00041000 00022000 00014000 00008000",
        /* mask */
        $"00010000 00038000 0007C000 000FE000"
        $"001FF000 003FF800 007FFC00 00FFFE00"
        $"01FFFF00 03FFFF80 07FFFFC0 0FFFFFE0"
        $"1FFFFFF0 3FFFFFF8 7FFFFFFC FFFFFFFE"
        $"7FFFFFFF 3FFFFFFF 1FFFFFFF 0FFFFFFF"
        $"07FFFFFF 03FFFFFF 01FFFFFF 00FFFFFF"
        $"007FFFFF 003FFE1F 001FFC07 000FF800"
        $"0007F000 0003E000 0001C000 00008000"
    }
};

resource 'ICN#' (129, "Document") {
    {
        /* data */
        $"0FFFFE00 08000300 08000280 08000240"
        $"08001220 08001210 080013F8 08547008"
        $"08AA9008 082A9008 082AF408 08010808"
        $"08000008 08000008 083DEF88 08000008"
        $"08F7BD88 08000008 08DEF788 08000008"
        $"08F7DE08 08000008 08000008 083DDB88"
        $"08000008 08F77788 08000008 08EFBD88"
        $"08000008 08000008 08000008 0FFFFFF8",
        /* mask */
        $"0FFFFE00 0FFFFF00 0FFFFF80 0FFFFFC0"
        $"0FFFFFE0 0FFFFFF0 0FFFFFF8 0FFFFFF8"
        $"0FFFFFF8 0FFFFFF8 0FFFFFF8 0FFFFFF8"
        $"0FFFFFF8 0FFFFFF8 0FFFFFF8 0FFFFFF8"
        $"0FFFFFF8 0FFFFFF8 0FFFFFF8 0FFFFFF8"
        $"0FFFFFF8 0FFFFFF8 0FFFFFF8 0FFFFFF8"
        $"0FFFFFF8 0FFFFFF8 0FFFFFF8 0FFFFFF8"
        $"0FFFFFF8 0FFFFFF8 0FFFFFF8 0FFFFFF8"
    }
};

/* ---- SIZE: Apple-Event-aware so 'odoc' fires from Finder ---- */

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    isHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    524288,                 /* 512K preferred */
    262144                  /* 256K minimum */
};
