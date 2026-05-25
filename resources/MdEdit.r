#include "Windows.r"
#include "Menus.r"
#include "Dialogs.r"
#include "Processes.r"
#include "Finder.r"
#include "Types.r"

/* ---- Main document window ---- */

resource 'WIND' (128, "MdEdit") {
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
    { 128, 129, 130, 131 }
};

resource 'MENU' (128, "Apple") {
    128, textMenuProc, allEnabled, enabled,
    apple,
    {
        "About MdEdit\0xC9",      noIcon, noKey,  noMark, plain;
        "-",                      noIcon, noKey,  noMark, plain;
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
        "Delete Word (Opt+Delete)",   noIcon, noKey,  noMark, plain;
    }
};

resource 'MENU' (131, "Format") {
    131, textMenuProc, allEnabled, enabled,
    "Format",
    {
        "Toggle Task",                noIcon, "L",    noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
        "Indent",                     noIcon, "]",    noMark, plain;
        "Outdent",                    noIcon, "[",    noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
        "Re-style Document",          noIcon, noKey,  noMark, plain;
    }
};

/* ---- About dialog ---- */

resource 'ALRT' (128, "About") {
    { 80, 80, 220, 400 },
    128,
    {
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
        OK, visible, sound1;
    },
    centerMainScreen
};

resource 'DITL' (128, "About") {
    {
        { 110, 230, 130, 300 },
        Button { enabled, "OK" };

        { 12, 12, 32, 308 },
        StaticText { disabled, "MdEdit \0xC9 a tiny Markdown editor" };

        { 36, 12, 56, 308 },
        StaticText { disabled, "Built with Retro68 / VibeRetro68" };

        { 60, 12, 100, 308 },
        StaticText { disabled,
            "Headings, lists, task lists, horizontal rules, "
            "and bold/italic styling. Detects and preserves "
            "CR / LF / CRLF line endings." };
    }
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
    "MdEdit 1.0"
};

/* App icon — a filled 24x24 black square. 32x32 1-bit format: data
   first (which pixels are black), mask second (which pixels are
   "ours"; outside the mask the desktop background shows through). */
resource 'ICN#' (128, "App") {
    {
        /* data: rows 0-3 blank, rows 4-27 filled cols 4-27, rows 28-31 blank */
        $"00000000 00000000 00000000 00000000"      /* rows 0-3 */
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"      /* rows 4-7 */
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"      /* rows 8-11 */
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"      /* rows 12-15 */
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"      /* rows 16-19 */
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"      /* rows 20-23 */
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"      /* rows 24-27 */
        $"00000000 00000000 00000000 00000000",     /* rows 28-31 */
        /* mask: same shape */
        $"00000000 00000000 00000000 00000000"
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"
        $"0FFFFFF0 0FFFFFF0 0FFFFFF0 0FFFFFF0"
        $"00000000 00000000 00000000 00000000"
    }
};

/* Document icon — outlined 22-wide page (cols 5-26), rows 3-28, with
   a black border and white interior so it reads as "a page". */
resource 'ICN#' (129, "Document") {
    {
        /* data: top edge row 3, side rails rows 4-27, bottom edge row 28 */
        $"00000000 00000000 00000000 07FFFFE0"      /* rows 0-2 blank; row 3 top */
        $"04000020 04000020 04000020 04000020"      /* rows 4-7 sides */
        $"04000020 04000020 04000020 04000020"      /* rows 8-11 */
        $"04000020 04000020 04000020 04000020"      /* rows 12-15 */
        $"04000020 04000020 04000020 04000020"      /* rows 16-19 */
        $"04000020 04000020 04000020 04000020"      /* rows 20-23 */
        $"04000020 04000020 04000020 04000020"      /* rows 24-27 */
        $"07FFFFE0 00000000 00000000 00000000",     /* row 28 bottom; rows 29-31 blank */
        /* mask: the full filled page so interior is white-on-desktop */
        $"00000000 00000000 00000000 07FFFFE0"
        $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
        $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
        $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
        $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
        $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
        $"07FFFFE0 07FFFFE0 07FFFFE0 07FFFFE0"
        $"07FFFFE0 00000000 00000000 00000000"
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
