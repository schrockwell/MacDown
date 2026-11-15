---
name: basilisk-testing
description: Test MacDown in the Basilisk II emulator by booting a System 7.5.5 disk image, taking screenshots of the emulated Mac, and driving it with mouse clicks and keystrokes. Use when asked to try a change in the emulator, reproduce a bug on classic Mac OS, check UI (menus, dialogs, windows), or verify file/prefs behavior end to end.
---

# Testing MacDown in Basilisk II

Everything goes through `scripts/emu.sh` (path relative to this skill). It
prepares a disposable copy of a bootable disk image, boots it, captures
screenshots, and sends input. Run it from the repo root:

```sh
E=.claude/skills/basilisk-testing/scripts/emu.sh
export EMU_DIR=<scratch dir>    # optional; defaults to $TMPDIR/macdown-emu
```

## What it needs

- `deps/basiliskii/BasiliskII.app` and `deps/basiliskii/Quadra.rom`
  (`make fetch-deps`).
- `cliclick` (`brew install cliclick`) for input; `hfsutils` comes with the
  Retro68 toolchain in `deps/retro68/Retro68-build/toolchain/bin`.
- A **bootable** HFS image. The repo's `deps/basiliskii/System753.dsk` boots
  7.5.3. For 7.5.5, the "System 7.5.5 Bootable SCSI 2GB disk image" from
  macintoshrepository.org (`OS_755_2GB.zip`, SHA-1
  `5899633a393ecd1caacf94c870b1126c87932e0b`) works as-is; its System
  Folder is named `OS 7.5.5 (minimal)`. Download it into the scratchpad and
  check the hash; never into the repo.
- macOS permissions for whatever runs Claude Code: Screen Recording
  (screenshots) and Accessibility (System Events + cliclick).
- **Tell the user before driving input.** `cliclick` moves their real
  mouse and types into the frontmost app for as long as the test runs.

## Workflow

```sh
make build
$E setup /path/to/bootable.dsk      # clone image -> $EMU_DIR/test.dsk, write prefs
$E install                          # build/MacDown.bin -> System Folder:Startup Items
$E put notes/test.md ":Docs:a.md"   # optional fixtures (TEXT/MDED, CR line endings)
$E boot                             # run with run_in_background: true
$E wait 18                          # window exists + settle; MacDown auto-launches
$E shot start                       # -> $EMU_DIR/start.png; Read it to look
... drive it (below) ...
$E stop                             # kill the emulator (not just its shell)
$E get ":Docs:a.md"                 # inspect results on the disk image
```

`install`, `put`, `get`, `getraw` and `hfs` need the emulator **stopped**:
the running emulator holds a lock on the image. Use `$E hfs <cmd> ...` for
any other hfsutils command, e.g. `$E hfs hls -l ":Docs:"`,
`$E hfs hdel ":Docs:a.md"`.

## Coordinates

`shot` images are exactly 640x480 with the macOS title bar cropped off, so
**a pixel in a screenshot is the emulated Mac coordinate** you pass to
`click`/`dbl`/`path`. Menu bar is y≈9; the first menu titles are around
`File` x≈53, `Edit` x≈90 (read them off a screenshot rather than guessing).

## Input

| Command | Use |
|---|---|
| `$E click x y` | Single click (move, pause, press, hold 0.25s, release). |
| `$E dbl x y` | Double-click timed for MacDown's own list (its threshold is 30 ticks). |
| `$E path x0 y0 x1 y1 ...` | Press at the first point, move through the rest, release at the last: menus and submenus, dragging windows. |
| `$E press x0 y0 ...` then `$E shot name` then `$E release x y` | Hold a menu open to screenshot it. Release over the desktop to cancel. |
| `$E key cmd+q` / `key cmd+arrow-up` / `key return` | Key combos (cliclick key names for non-characters). |
| `$E type "text"` | Type text. |

Every input command first brings Basilisk II to the front and **aborts if
it isn't frontmost**, so keystrokes can't land in another app (that happened
once and ⌘W closed something on the host). If it aborts, the user probably
clicked another window; take a shot and retry.

Menu example (File → Open Recent → first item):

```sh
$E path 53 9  80 60  170 60  203 60  203 76
```

## Screenshots without stealing focus

`$E shot` uses `screencapture -l <window id>`, which captures the emulator
window **even when it's behind other windows**, so passive observation (boot,
launch, restore) never needs focus. Only input requires Basilisk II to be
frontmost: cliclick posts global events, and SDL ignores mouse input to an
unfocused window. If you only need to check startup behavior, put the app in
Startup Items (`install`) and just `wait` + `shot`. No input is needed.

## Gotchas learned the hard way

- **Instant clicks get dropped.** Basilisk needs to see the pointer arrive
  before the button goes down, and a short hold. Use the script's commands,
  not raw `cliclick c:`.
- **Standard File dialogs** didn't respond reliably to `key return` or to a
  fast double-click. Click the item, then click the Open/Save button.
  Type-to-select (`type Do`) and `key cmd+arrow-up` (parent folder) do work.
- **The first click on the desktop activates the Finder**, not MacDown.
  Click a MacDown window to bring it back.
- **`pkill -f` matches two processes**: the background shell wrapper and the
  emulator. `$E stop` targets the emulator binary only.
- **The user may have their own Basilisk II open** (e.g. with
  `deps/basiliskii/prefs`). The script only ever touches the instance
  booted with `$EMU_DIR/prefs`: `stop`, `shot`, focus and input all go
  by that process's pid, so it's safe to run alongside theirs. Never kill
  an emulator you didn't boot.
- **`hls` exits 0 even for paths that don't exist.** Check listings, not
  exit codes.
- **`sips` silently ignores `--cropOffset`** on these captures; the script
  crops with CoreGraphics via JXA instead.
- **Startup takes about 15-20s** with JIT on. Don't use foreground `sleep`
  loops to wait; use `$E wait` (or a Monitor / background command).
- **Basilisk II is 32-bit, 68040 only.** It can't reproduce 24-bit-mode
  bugs, which is what an SE/30 in 24-bit mode runs. For those, use Mini vMac's
  Mac II build (`minivmac-macOS-II.app` from the erichelgeson/minivmac
  releases, ROM `97221136 - Mac II FDHD & IIx & IIcx.ROM` from the same
  archive.org ROM pack as `scripts/fetch-deps.sh`, renamed `MacII.ROM` next
  to the app). Mini vMac wants a bare HFS volume, so extract the partition
  first (for the 7.5.5 image: 3850144 blocks starting at block 96) and name
  it `disk1.dsk` beside the app. It boots in about 70s at 1x speed.
- **Reading app state off the disk** is often the best assertion: stop the
  emulator, then `get`/`getraw` the file (e.g. a saved document or
  `:<System Folder>:Preferences:MacDown Prefs`) and decode it.
