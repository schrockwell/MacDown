# deps/minivmac/

This directory holds the Mini vMac emulator, its ROM, and the System
boot disk used for automated test runs. Everything except this README
is **gitignored**.

Mini vMac is a lightweight classic Mac emulator. We use the SE FDHD
build (Macintosh SE with 1.44 MB SuperDrive), which runs the
LaunchAPPL-driven automated tests described in
[../../docs/EMULATOR_SETUP.md](../../docs/EMULATOR_SETUP.md).

## Expected Layout

```
deps/minivmac/
├── minivmac-macOS-SEFDHD.app    (auto-downloaded — GitHub release)
├── SEFDHD.ROM                   (auto-downloaded — archive.org)
└── disk1.dsk                    (manual — System 6 or 7 boot floppy)
```

## What Goes Here

| File | Purpose | Source |
|------|---------|--------|
| `SEFDHD.ROM` | Mac SE FDHD ROM (256KB) | Auto-downloaded by [`scripts/fetch-deps.sh`](../../scripts/fetch-deps.sh) from the [Internet Archive Mac ROM archive](https://archive.org/details/mac_rom_archive_-_as_of_8-19-2011) |
| `minivmac-macOS-SEFDHD.app` | Mini vMac (SE FDHD build) | Auto-downloaded by [`scripts/fetch-deps.sh`](../../scripts/fetch-deps.sh) from [erichelgeson/minivmac releases](https://github.com/erichelgeson/minivmac/releases/) |
| `disk1.dsk` | System 6 or 7 boot floppy | **Manual** — Apple legacy downloads / disk image archives |

Mini vMac expects the ROM and boot disk in the **same directory** as
the `.app` bundle. Filenames must match what the build variant looks
for (e.g. `SEFDHD.ROM`).

## macOS Quarantine

Downloaded ROMs and emulator apps can be quarantined by macOS, which
silently prevents the emulator from reading the ROM.
`fetch-deps.sh` strips quarantine from what it downloads. If you
ever fetch files manually:

```bash
xattr -cr deps/minivmac/SEFDHD.ROM deps/minivmac/minivmac-macOS-SEFDHD.app
```
