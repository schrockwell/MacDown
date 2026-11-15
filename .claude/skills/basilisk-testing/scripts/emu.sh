#!/bin/bash
# Drive Basilisk II for MacDown UI testing: prepare a disk image, boot it,
# take screenshots, and send mouse/keyboard input. See ../SKILL.md.
#
# All coordinates are EMULATED Mac screen coordinates (0..639, 0..479),
# which are also the pixel coordinates of `shot` images.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
BASILISK_BIN="$ROOT/deps/basiliskii/BasiliskII.app/Contents/MacOS/BasiliskII"
ROM="$ROOT/deps/basiliskii/Quadra.rom"
EMU_DIR="${EMU_DIR:-${TMPDIR:-/tmp}/macdown-emu}"
IMG="$EMU_DIR/test.dsk"
PREFS="$EMU_DIR/prefs"
SHARE="$EMU_DIR/share"
HFS_BIN="$ROOT/deps/retro68/Retro68-build/toolchain/bin"
TITLE_H=28          # macOS title bar (points) above the 640x480 Mac screen
W=640; H=480

die() { echo "emu.sh: $*" >&2; exit 1; }

hfs() {   # run an hfsutils command (toolchain copy first, then PATH)
    local t="$1"; shift
    if [ -x "$HFS_BIN/$t" ]; then "$HFS_BIN/$t" "$@"; else "$t" "$@"; fi
}

emu_pid() {   # the emulator itself -- NOT the shell that launched it
    pgrep -f "^$BASILISK_BIN" | head -1 || true
}

need_stopped() {
    [ -z "$(emu_pid)" ] || die "emulator is running; '$0 stop' first (the image is locked while it runs)"
}

with_image() {   # mount, run "$@", always unmount
    need_stopped
    hfs hmount "$IMG" >/dev/null
    local rc=0
    "$@" || rc=$?
    hfs humount >/dev/null 2>&1 || true
    return $rc
}

system_folder() {   # root folder holding both "System" and "Startup Items"
    # (hls exits 0 even for paths that don't exist, so match on listings.)
    local d listing
    while IFS= read -r d; do
        listing=$(hfs hls -1 ":$d:" 2>/dev/null) || continue
        if grep -qx "System" <<< "$listing" && grep -qx "Startup Items" <<< "$listing"; then
            echo "$d"; return
        fi
    done < <(hfs hls -1 ":")
    die "no System Folder with Startup Items found on $IMG"
}

window_origin() {   # prints "OX OY": screen point of emulated (0,0)
    local pos
    pos=$(osascript -e 'tell application "System Events" to tell process "BasiliskII" to get position of window 1' 2>/dev/null) \
        || die "no Basilisk II window (is it booted?)"
    local x=${pos%%,*} y=${pos##*, }
    echo "$x $((y + TITLE_H))"
}

window_id() {
    osascript -l JavaScript -e '
      ObjC.import("CoreGraphics");
      var l = ObjC.castRefToObject($.CGWindowListCopyWindowInfo($.kCGWindowListOptionOnScreenOnly, 0));
      var r = "";
      for (var i = 0; i < l.count; i++) {
        var w = l.objectAtIndex(i);
        var n = ObjC.unwrap(w.objectForKey("kCGWindowOwnerName")) || "";
        if (n === "BasiliskII") { r = ObjC.unwrap(w.objectForKey("kCGWindowNumber")); break; }
      }
      r'
}

focus() {   # never send keys/clicks unless Basilisk is frontmost
    osascript -e 'tell application "System Events" to set frontmost of process "BasiliskII" to true' >/dev/null 2>&1 || true
    sleep 0.4
    local front
    front=$(osascript -e 'tell application "System Events" to get name of first process whose frontmost is true')
    [ "$front" = "BasiliskII" ] || die "focus failed: frontmost app is '$front' (did someone click another window?)"
}

pt() {   # emulated (x,y) -> "sx,sy" screen point
    read -r OX OY < <(window_origin)
    echo "$((OX + $1)),$((OY + $2))"
}

cmd=${1:-help}; shift || true
case "$cmd" in
  setup)    # setup <bootable-image>   copy image + write prefs
    [ $# -eq 1 ] || die "usage: setup <bootable-image.dsk>"
    need_stopped
    mkdir -p "$EMU_DIR" "$SHARE"
    cp -c "$1" "$IMG" 2>/dev/null || cp "$1" "$IMG"     # APFS clone when possible
    cat > "$PREFS" <<EOF
rom $ROM
disk $IMG
extfs $SHARE
ramsize 67108864
modelid 14
cpu 4
fpu true
jit true
jitfpu true
frameskip 0
screen win/$W/$H
nocdrom true
nosound true
EOF
    echo "image: $IMG"; echo "prefs: $PREFS" ;;

  install)  # install [app.bin]   put the app in Startup Items (launches at boot)
    BIN="${1:-$ROOT/build/MacDown.bin}"
    [ -f "$BIN" ] || die "no $BIN (run 'make build')"
    do_install() {
        local sf; sf=$(system_folder)
        hfs hdel ":$sf:Startup Items:MacDown" 2>/dev/null || true
        hfs hcopy -m "$BIN" ":$sf:Startup Items:"
        hfs hls -l ":$sf:Startup Items:"
    }
    with_image do_install ;;

  put)      # put <host-file> <:Mac:Path>   copy a text file in (CR line endings, TEXT/MDED)
    [ $# -eq 2 ] || die "usage: put <host-file> <:Mac:Path>"
    do_put() { hfs hcopy -t "$1" "$2" && hfs hattrib -t TEXT -c MDED "$2"; }
    with_image do_put "$1" "$2" ;;

  get)      # get <:Mac:Path>   print a text file (CR -> LF)
    [ $# -eq 1 ] || die "usage: get <:Mac:Path>"
    with_image hfs hcopy -t "$1" - ;;

  getraw)   # getraw <:Mac:Path> <host-file>   copy data fork byte-for-byte
    [ $# -eq 2 ] || die "usage: getraw <:Mac:Path> <host-file>"
    with_image hfs hcopy -r "$1" "$2" ;;

  hfs)      # hfs <hfsutils-cmd> args...   e.g. hfs hls -l ":Docs:"
    [ $# -ge 1 ] || die "usage: hfs <hls|hdel|hmkdir|hrename|...> args"
    t="$1"; shift
    with_image hfs "$t" "$@" ;;

  sysfolder) with_image system_folder ;;

  boot)     # boot   (run with run_in_background; returns when the emulator exits)
    need_stopped
    [ -f "$PREFS" ] || die "run '$0 setup <image>' first"
    exec "$BASILISK_BIN" --config "$PREFS" ;;

  wait)     # wait [settle-seconds]   block until the window exists, then settle
    for _ in $(seq 1 60); do
        osascript -e 'tell application "System Events" to tell process "BasiliskII" to get position of window 1' >/dev/null 2>&1 && break
        sleep 1
    done
    sleep "${1:-20}"; echo "ready" ;;

  pid)  emu_pid ;;

  stop)
    P=$(emu_pid); [ -n "$P" ] || { echo "not running"; exit 0; }
    kill "$P"
    for _ in 1 2 3 4 5; do kill -0 "$P" 2>/dev/null || break; sleep 1; done
    kill -0 "$P" 2>/dev/null && kill -9 "$P"
    echo "stopped" ;;

  shot)     # shot <name>   -> $EMU_DIR/<name>.png, 640x480, emulated coordinates
    [ $# -eq 1 ] || die "usage: shot <name>"
    ID=$(window_id); [ -n "$ID" ] || die "no Basilisk II window on screen"
    OUT="$EMU_DIR/$1.png"
    # -l captures this window even when other windows cover it.
    screencapture -x -o -l "$ID" "$OUT"
    # Drop the macOS title bar (sips silently ignores --cropOffset on
    # these captures, so crop with CoreGraphics), then scale Retina 2x
    # down to 1:1 with the emulated screen.
    osascript -l JavaScript - "$OUT" "$OUT" "$TITLE_H" "$W" >/dev/null <<'JXA'
function run(argv) {
  ObjC.import('AppKit'); ObjC.import('CoreGraphics');
  var rep = $.NSBitmapImageRep.imageRepWithContentsOfFile(argv[0]);
  var cg = rep.CGImage;
  var w = $.CGImageGetWidth(cg), h = $.CGImageGetHeight(cg);
  var top = parseInt(argv[2]) * (w / parseInt(argv[3]));
  var crop = $.CGImageCreateWithImageInRect(cg, $.CGRectMake(0, top, w, h - top));
  var out = $.NSBitmapImageRep.alloc.initWithCGImage(crop);
  out.representationUsingTypeProperties($.NSBitmapImageFileTypePNG, $({}))
     .writeToFileAtomically(argv[1], true);
}
JXA
    sips -Z $W "$OUT" --out "$OUT" >/dev/null
    echo "$OUT" ;;

  focus) focus ;;
  origin) window_origin ;;

  move)  focus; cliclick "m:$(pt "$1" "$2")" ;;

  click)    # click x y   (move, pause, press, hold, release: Basilisk drops instant clicks)
    focus; P=$(pt "$1" "$2")
    cliclick "m:$P"; sleep 0.3; cliclick "dd:$P"; sleep 0.25; cliclick "du:$P" ;;

  dbl)      # dbl x y   double-click with MacDown-friendly timing
    focus; P=$(pt "$1" "$2")
    cliclick "m:$P"; sleep 0.3
    cliclick "dd:$P"; sleep 0.08; cliclick "du:$P"; sleep 0.12
    cliclick "dd:$P"; sleep 0.08; cliclick "du:$P" ;;

  path)     # path x0 y0 x1 y1 ... xn yn   press at first point, move through, release at last
    [ $# -ge 4 ] && [ $(($# % 2)) -eq 0 ] || die "usage: path x0 y0 x1 y1 [...]"
    focus
    P=$(pt "$1" "$2"); shift 2
    cliclick "m:$P"; sleep 0.3; cliclick "dd:$P"; sleep 0.5
    while [ $# -gt 0 ]; do P=$(pt "$1" "$2"); shift 2; cliclick "m:$P"; sleep 0.4; done
    cliclick "du:$P" ;;

  press)    # press x0 y0 x1 y1 ...   like path but DON'T release (for screenshots of open menus)
    focus
    P=$(pt "$1" "$2"); shift 2
    cliclick "m:$P"; sleep 0.3; cliclick "dd:$P"; sleep 0.5
    while [ $# -gt 0 ]; do P=$(pt "$1" "$2"); shift 2; cliclick "m:$P"; sleep 0.4; done ;;

  release)  # release x y   let go of a press (e.g. on empty desktop to cancel a menu)
    focus; P=$(pt "$1" "$2"); cliclick "m:$P"; sleep 0.3; cliclick "du:$P" ;;

  key)      # key cmd+q | cmd+arrow-up | return | esc | cmd+shift+z ...
    [ $# -eq 1 ] || die "usage: key <combo>"
    focus
    IFS='+' read -ra parts <<< "$1"
    k="${parts[${#parts[@]}-1]}"; mods=("${parts[@]:0:${#parts[@]}-1}")
    args=()
    for m in "${mods[@]}"; do args+=("kd:$m"); done
    if [ ${#k} -eq 1 ]; then args+=("t:$k"); else args+=("kp:$k"); fi
    for m in "${mods[@]}"; do args+=("ku:$m"); done
    cliclick "${args[@]}" ;;

  type)     # type "some text"
    focus; cliclick "t:$1" ;;

  *)
    sed -n '2,6p' "$0"
    echo "commands: setup install put get getraw hfs sysfolder boot wait pid stop"
    echo "          shot focus origin move click dbl path press release key type"
    ;;
esac
