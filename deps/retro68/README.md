# deps/retro68/

This directory holds the Retro68 cross-compiler toolchain — large,
machine-specific, and built locally.

The contents are **gitignored** (except this README). Every clone of the
project builds its own copy here so that the parent repo stays small and
portable.

The emulators live in sibling directories: see [../basiliskii/](../basiliskii/)
for Basilisk II and [../minivmac/](../minivmac/) for Mini vMac.

## Expected Layout

After running [scripts/fetch-deps.sh](../../scripts/fetch-deps.sh)
and the steps in [docs/RETRO68_SETUP.md](../../docs/RETRO68_SETUP.md):

```
deps/retro68/
├── Retro68/               Retro68 source checkout (git clone)
└── Retro68-build/         Toolchain build output
    └── toolchain/
        ├── bin/           m68k-apple-macos-gcc, Rez, LaunchAPPL, ...
        └── m68k-apple-macos/cmake/retro68.toolchain.cmake
```

## Rebuilding

If you ever need to start clean, just delete `deps/retro68/` (keeping
this README) and follow [docs/RETRO68_SETUP.md](../../docs/RETRO68_SETUP.md) again.
