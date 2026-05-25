# Emulator Setup for Classic Mac Development

Two emulators serve different roles in the development workflow.

## Basilisk II — Interactive Testing

Basilisk II emulates a 68040-class Mac (Quadra 900). It supports System 7.x
through Mac OS 8.1, with shared folders for easy host-guest file exchange.

**Pre-configured setup location:** `deps/basiliskii/` (gitignored — see
[../deps/basiliskii/README.md](../deps/basiliskii/README.md)).

### Installation

Setup is two downloads — the bundle does not include the ROM (Apple IP).

**1. Emulator + disk images** — pre-configured bundle from the Emaculation
forum (no ROM):

https://www.emaculation.com/forum/viewtopic.php?t=7361

The bundle zip wraps its contents in a `Basilisk II/` folder. Flatten
it into `deps/basiliskii/` (without the space):

```bash
unzip "/path/to/Basilisk II.zip" -d /tmp/
mv "/tmp/Basilisk II"/* deps/basiliskii/
```

**2. Quadra ROM** — auto-downloaded by `scripts/fetch-deps.sh`
from the Mac ROM archive on archive.org and placed at
`deps/basiliskii/Quadra.rom`. Source:

https://archive.org/details/mac_rom_archive_-_as_of_8-19-2011

### What's Included

| File | Source |
|------|--------|
| `BasiliskII.app` | Emaculation bundle |
| `Basilisk II GUI - RUN ME.app` | Emaculation bundle |
| `Quadra.rom` | Auto-downloaded — archive.org Mac ROM archive |
| `HDD753.dsk` | Emaculation bundle (Mac OS 7.5.3 installed) |
| `OS753InstallerParts.dsk` | Emaculation bundle |
| `DiskTools_MacOS8.image` | Emaculation bundle |
| `basilisk_ii_prefs.dotfile` | Emaculation bundle |
| `shared/` | Emaculation bundle (shared host↔guest folder) |

### Configuration

The prefs file needs to be placed where Basilisk II expects it:

```bash
cp deps/basiliskii/basilisk_ii_prefs.dotfile ~/.basilisk_ii_prefs
```

Or launch via the GUI app first — it will handle configuration and show where
settings are stored.

### Key Settings

Basilisk II's prefs file requires absolute paths. Edit `~/.basilisk_ii_prefs`
to point at this project's `deps/basiliskii/`, e.g.:

```
rom /Users/you/Projects/VibeRetro68/deps/basiliskii/Quadra.rom
disk /Users/you/Projects/VibeRetro68/deps/basiliskii/HDD753.dsk
extfs /Users/you/Projects/VibeRetro68/deps/basiliskii/shared
```

The `extfs` line maps the `shared/` directory so it appears as a volume
inside the emulated Mac.

### Using the Shared Folder

Files placed in `deps/basiliskii/shared/` appear automatically on the
Mac desktop as a mounted volume. Copy `.bin` (MacBinary) files here — the
emulated Mac preserves resource forks from MacBinary format.

### Running

```bash
open deps/basiliskii/BasiliskII.app
```

Or use the GUI launcher for first-time configuration:
```bash
open deps/basiliskii/Basilisk\ II\ GUI\ -\ RUN\ ME.app
```

## Mini vMac — Automated Testing

Mini vMac is a lightweight classic Mac emulator. Different builds target
different Mac models. We use the SE FDHD build from
https://github.com/erichelgeson/minivmac/releases/ which emulates a
Macintosh SE with a 1.44 MB SuperDrive (FDHD = Floppy Disk High Density).

**Location:** `deps/minivmac/minivmac-macOS-SEFDHD.app` — auto-downloaded by
`scripts/fetch-deps.sh`.

### Requirements

Mini vMac needs two files in the **same directory** as the `.app` bundle:

1. **ROM file** — named to match the build variant (`SEFDHD.ROM` for this
   build). `scripts/fetch-deps.sh` already extracts this from the
   [Internet Archive Mac ROM archive](https://archive.org/details/mac_rom_archive_-_as_of_8-19-2011)
   into `deps/minivmac/SEFDHD.ROM`.
2. **A boot disk image** — System 6 or 7 boot floppy (`.dsk` or `.img`).
   You still need to acquire this yourself; see Apple's legacy downloads.

Both files and the app should live in the `deps/minivmac/` directory.

### macOS Quarantine

Downloaded ROMs and emulator apps will be quarantined by macOS, which
silently prevents the emulator from reading the ROM. `fetch-deps.sh`
strips quarantine from the artifacts it downloads, but if you ever fetch
them by hand:

```bash
xattr -cr deps/minivmac/SEFDHD.ROM deps/minivmac/minivmac-macOS-SEFDHD.app
```

If Mini vMac says "I can not find the ROM image file" even though the file
is right there, quarantine is almost certainly the cause.

### System Disk

Mini vMac boots from a 1.44 MB or 800K floppy image. System 7.0.1 is freely
available from Apple's legacy downloads. Place the boot disk in `deps/minivmac/`:

```
deps/minivmac/
├── minivmac-macOS-SEFDHD.app
├── SEFDHD.ROM
└── disk1.dsk
```

Drag the disk image onto the Mini vMac window to "insert" it.

### AutoQuit for LaunchAPPL Integration

For automated testing, the AutoQuit utility must be installed on the boot
disk's System Folder. AutoQuit makes Mini vMac exit when the launched
application calls `ExitToShell()`.

AutoQuit is included in the Retro68 source tree:
```
deps/retro68/Retro68/LaunchAPPL/MiniVMacLauncher/
```

### LaunchAPPL Configuration

After building Retro68, configure `~/.LaunchAPPL.cfg`:

```ini
[LaunchMethod]
Method=minivmac

[minivmac]
minivmac=/path/to/VibeRetro68/deps/minivmac/minivmac-macOS-SEFDHD.app/Contents/MacOS/minivmac
rom=/path/to/VibeRetro68/deps/minivmac/SEFDHD.ROM
systemImage=/path/to/VibeRetro68/deps/minivmac/disk1.dsk
```

Then launch apps from the command line:
```bash
LaunchAPPL -e minivmac build/MyApp.dsk
```

Mini vMac will start, boot, run the app, and exit when the app quits.

## Which Emulator for What

| Task | Emulator |
|------|----------|
| Quick build verification | Mini vMac + LaunchAPPL |
| Interactive UI testing | Basilisk II |
| Testing with Mac OS 7.5.3 features | Basilisk II |
| Automated test scripts | Mini vMac + LaunchAPPL |
| Screenshots / demos | Basilisk II |
| System extension testing | Basilisk II (reboot-safe) |

## Emulator Limitations

- **No source-level debugging.** There's no GDB stub for these emulators.
  Debugging is printf-style: write to the console or serial port.
- **Mini vMac emulates a Mac Plus.** No color, no 68020+ instructions,
  1-bit display, 4 MB RAM max. Good for compatibility testing.
- **Basilisk II emulates a Quadra.** Color, 68040, up to 1 GB RAM.
  More representative of a "real" System 7 development target.
