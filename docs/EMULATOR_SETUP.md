# Emulator Setup for Classic Mac Development

Two emulators serve different roles in the development workflow.

## Basilisk II — Interactive Testing

Basilisk II emulates a 68040-class Mac (Quadra 900). It supports System 7.x
through Mac OS 8.1, with shared folders for easy host-guest file exchange.

**Pre-configured setup location:** `~/Code/Basilisk II/`

### Installation

```bash
# Extract the pre-configured setup from Dropbox
cd ~/Code
unzip "$HOME/Library/CloudStorage/Dropbox/Shared/Basilisk II_2024-12-11.zip"
```

### What's Included

| File | Purpose |
|------|---------|
| `BasiliskII.app` | The emulator binary |
| `Basilisk II GUI - RUN ME.app` | GUI configuration launcher |
| `Quadra.rom` | Quadra 900 ROM file |
| `HDD753.dsk` | Hard disk image with Mac OS 7.5.3 installed |
| `OS753InstallerParts.dsk` | Mac OS 7.5.3 installer extras |
| `DiskTools_MacOS8.image` | Mac OS 8 disk tools |
| `basilisk_ii_prefs.dotfile` | Pre-configured preferences |
| `shared/` | Shared folder (accessible from both host and guest) |

### Configuration

The prefs file needs to be placed where Basilisk II expects it:

```bash
cp ~/Code/Basilisk\ II/basilisk_ii_prefs.dotfile ~/.basilisk_ii_prefs
```

Or launch via the GUI app first — it will handle configuration and show where
settings are stored.

### Key Settings

Edit `~/.basilisk_ii_prefs` to verify:

```
rom /Users/erik/Code/Basilisk II/Quadra.rom
disk /Users/erik/Code/Basilisk II/HDD753.dsk
extfs /Users/erik/Code/Basilisk II/shared
```

The `extfs` line maps the `shared/` directory so it appears as a volume
inside the emulated Mac.

### Using the Shared Folder

Files placed in `~/Code/Basilisk II/shared/` appear automatically on the
Mac desktop as a mounted volume. Copy `.bin` (MacBinary) files here — the
emulated Mac preserves resource forks from MacBinary format.

### Running

```bash
open ~/Code/Basilisk\ II/BasiliskII.app
```

Or use the GUI launcher for first-time configuration:
```bash
open ~/Code/Basilisk\ II/Basilisk\ II\ GUI\ -\ RUN\ ME.app
```

## Mini vMac — Automated Testing

Mini vMac is a lightweight classic Mac emulator. Different builds target
different Mac models. We use the SE FDHD build from
https://github.com/erichelgeson/minivmac/releases/ which emulates a
Macintosh SE with a 1.44 MB SuperDrive (FDHD = Floppy Disk High Density).

**Location:** `roms/minivmac-macOS-SEFDHD.app`

### Requirements

Mini vMac needs two files in the **same directory** as the `.app` bundle:

1. **ROM file** — named to match the build variant (e.g. `SEFDHD.ROM`)
2. **A boot disk image** — System 6 or 7 boot floppy (`.dsk` or `.img`)

Both files and the app should live in the `roms/` directory.

### macOS Quarantine

Downloaded ROMs and emulator apps will be quarantined by macOS, which
silently prevents the emulator from reading the ROM. Clear it after
downloading:

```bash
xattr -cr roms/SEFDHD.ROM roms/minivmac-macOS-SEFDHD.app
```

If Mini vMac says "I can not find the ROM image file" even though the file
is right there, quarantine is almost certainly the cause.

### System Disk

Mini vMac boots from a 1.44 MB or 800K floppy image. System 7.0.1 is freely
available from Apple's legacy downloads. Place the boot disk in `roms/`:

```
roms/
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
~/Code/Retro68/LaunchAPPL/MiniVMacLauncher/
```

### LaunchAPPL Configuration

After building Retro68, configure `~/.LaunchAPPL.cfg`:

```ini
[LaunchMethod]
Method=minivmac

[minivmac]
minivmac=/Users/erik/Code/PrintWatch/roms/minivmac-macOS-SEFDHD.app/Contents/MacOS/minivmac
rom=/Users/erik/Code/PrintWatch/roms/SEFDHD.ROM
systemImage=/Users/erik/Code/PrintWatch/roms/disk1.dsk
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
