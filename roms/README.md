# ROMs and Boot Disks

This directory holds ROM dumps, system boot disk images, and emulator app
bundles. Binary files are gitignored due to size and licensing.

## What Goes Here

| File | Purpose | Source |
|------|---------|--------|
| `SEFDHD.ROM` | Mac SE FDHD ROM (256KB) | Dump from physical hardware or ROM archive |
| `disk1.dsk` | System 7 boot floppy | Apple legacy downloads / disk image archives |
| `minivmac-macOS-SEFDHD.app` | Mini vMac (SE FDHD build) | https://github.com/erichelgeson/minivmac/releases/ |

## macOS Quarantine

Downloaded ROMs and emulator apps will be quarantined by macOS, preventing
the emulator from reading the ROM file. Clear quarantine after downloading:

```bash
xattr -cr roms/SEFDHD.ROM roms/minivmac-macOS-SEFDHD.app
```

## Mini vMac

Mini vMac expects the ROM file in the same directory as the `.app` bundle.
The ROM filename must match the build variant (e.g. `SEFDHD.ROM` for the
SE FDHD build). See `docs/EMULATOR_SETUP.md` for full configuration.

## Basilisk II

Basilisk II uses a Quadra ROM bundled with its pre-configured setup at
`~/Code/Basilisk II/Quadra.rom`. No additional ROM needed here.
