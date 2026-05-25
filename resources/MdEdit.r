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
    { 128, 129, 130, 131, 132 }
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
        "Duplicate Line",             noIcon, "D",    noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
        "Re-style Document",          noIcon, noKey,  noMark, plain;
    }
};

resource 'MENU' (132, "Windows") {
    132, textMenuProc, allEnabled, enabled,
    "Windows",
    {
        "Next Window (Cmd-`)",        noIcon, noKey,  noMark, plain;
        "-",                          noIcon, noKey,  noMark, plain;
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

/* App / document icons — generated from app.png / document.png by
   scripts/png-to-icn.py. The mask is a flood-filled silhouette so the
   white interior of the page outline stays opaque (no desktop bleed
   through) without needing a PNG alpha channel. */

resource 'ICN#' (128, "App") {
    {
        /* data */
        $"00000000 00000000 01FFFF80 02000040"
        $"02000040 02000040 02000040 02011040"
        $"02011040 02022040 021FFC40 02022040"
        $"02044040 023FF840 02044048 02088054"
        $"02088062 02000054 02000088 02000110"
        $"027FB220 02000440 0277C8C0 02001140"
        $"027EDA40 02001C40 027CC040 02000040"
        $"02000040 01FFFF80 00000000 00000000",
        /* mask */
        $"00000000 00000000 01FFFF80 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC8 03FFFFDC"
        $"03FFFFFE 03FFFFFC 03FFFFF8 03FFFFF0"
        $"03FFFFE0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 01FFFF80 00000000 00000000"
    }
};

resource 'ICN#' (129, "Document") {
    {
        /* data */
        $"00000000 00000000 01FFFF80 02000040"
        $"02000040 02000040 02000040 02011040"
        $"02011040 02022040 021FFC40 02022040"
        $"02044040 023FF840 02044040 02088040"
        $"02088040 02000040 02000040 02000040"
        $"027FBE40 02000040 0277E040 02000040"
        $"027EF840 02000040 027CFE40 02000040"
        $"02000040 01FFFF80 00000000 00000000",
        /* mask */
        $"00000000 00000000 01FFFF80 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 03FFFFC0 03FFFFC0 03FFFFC0"
        $"03FFFFC0 01FFFF80 00000000 00000000"
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
