# deps/basiliskii/

This directory holds the Basilisk II emulator install — the pre-configured
Emaculation bundle plus the Quadra ROM. Everything except this README is
**gitignored**.

## Expected Layout

```
deps/basiliskii/
├── BasiliskII.app           (auto-downloaded by scripts/fetch-deps.sh)
├── Quadra.rom               (auto-downloaded by scripts/fetch-deps.sh)
├── System753.dsk            (auto-downloaded by scripts/fetch-deps.sh)
├── prefs                    (written by scripts/run-basiliskii.sh)
└── shared/                  (host↔guest file exchange)
```

## How to Populate

1. **Emulator** — auto-downloaded by `scripts/fetch-deps.sh`. We mirror
   the Emaculation universal build at `cdn.schrockwell.com` because
   emaculation.com sits behind a Cloudflare JS challenge that blocks
   `curl`. Source page (manual download fallback):

       https://www.emaculation.com/forum/viewtopic.php?t=7361

2. **Quadra ROM** — auto-downloaded by `scripts/fetch-deps.sh`
   from the [Internet Archive Mac ROM archive][rom-archive] and placed
   here at `Quadra.rom`. Source page:

       https://archive.org/details/mac_rom_archive_-_as_of_8-19-2011

3. **System 7.5.3 disk image** — auto-downloaded by
   `scripts/fetch-deps.sh` from the Internet Archive and placed at
   `System753.dsk` (~200 MB). Source page:

       https://archive.org/details/system-753

4. **Prefs file** — copy into place (Basilisk II reads it from `$HOME`):

   ```bash
   cp deps/basiliskii/basilisk_ii_prefs.dotfile ~/.basilisk_ii_prefs
   ```

   Then edit `~/.basilisk_ii_prefs` so the `rom`, `disk`, and `extfs`
   lines point at the absolute paths under this project's
   `deps/basiliskii/`.

See [../../docs/EMULATOR_SETUP.md](../../docs/EMULATOR_SETUP.md) for full
configuration details.

[rom-archive]: https://archive.org/details/mac_rom_archive_-_as_of_8-19-2011
