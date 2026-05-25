# Development Workflow: Claude Code + Retro68 + Emulators

How to iteratively develop classic Mac applications using Claude Code as the
coding partner, Retro68 as the cross-compiler, and Basilisk II / Mini vMac
for testing.

## The Core Loop

```
 ┌─────────────────────────────────────────────────────┐
 │                   Claude Code                       │
 │                                                     │
 │   1. Edit source (.c, .h, .r)                       │
 │   2. Build (cmake --build)                          │
 │   3. Copy .bin to shared folder                     │
 │   4. Read build output / errors                     │
 │                                                     │
 └────────────────┬────────────────────────────────────┘
                  │
                  │  .bin copied to ~/Code/Basilisk II/shared/
                  ▼
 ┌─────────────────────────────────────────────────────┐
 │                  Basilisk II                        │
 │                                                     │
 │   5. Erik opens app from shared volume              │
 │   6. Tests the feature / reproduces the bug         │
 │   7. Reports observations back to Claude Code       │
 │                                                     │
 └─────────────────────────────────────────────────────┘
```

### Step-by-Step

1. **Claude edits source** — C/C++ files, Rez resource files (`.r`)
2. **Claude builds** — `cmake --build build/` from the project directory
3. **Claude deploys** — copies the `.bin` file to the Basilisk II shared folder:
   ```bash
   cp build/MyApp.bin ~/Code/Basilisk\ II/shared/
   ```
4. **Claude reads output** — compiler warnings, errors, linker messages
5. **Erik tests** — switches to Basilisk II, opens the app from the shared
   volume, interacts with it
6. **Erik reports** — describes what happened, screenshots if useful
7. **Claude iterates** — fixes bugs, adds features, rebuilds

### Quick Build Command

From the project root:
```bash
cmake --build build/ && cp build/MyApp.bin "$HOME/Code/Basilisk II/shared/"
```

Claude can run this as a single command to build and deploy in one step.

## Project Setup Template

### Directory Structure

```
PrintWatch/
├── CMakeLists.txt
├── src/
│   ├── main.c          # Entry point, event loop
│   ├── app.h           # Application-wide types and declarations
│   └── ...
├── resources/
│   └── PrintWatch.r    # Rez resource definitions (menus, dialogs, icons)
├── build/              # CMake build output (gitignored)
├── docs/
│   ├── RETRO68_SETUP.md
│   ├── EMULATOR_SETUP.md
│   └── WORKFLOW.md
└── .gitignore
```

### CMakeLists.txt Template

```cmake
cmake_minimum_required(VERSION 3.9)
project(PrintWatch C)

add_application(PrintWatch
    SOURCES
        src/main.c
    RESOURCES
        resources/PrintWatch.r
)
```

### Build Directory Setup (One-Time)

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/Code/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
```

After this, `cmake --build build/` is all Claude needs to run.

### .gitignore

```
build/
*.dsk
*.bin
.DS_Store
```

(The `.dsk` and `.bin` are build artifacts — always reproducible from source.)

## Debugging Strategy

There is no source-level debugger for Retro68 + emulator. Debugging is
observation-based:

### Printf Debugging

Retro68 supports `printf()` and `cout` — output goes to a console window
inside the emulated Mac if you link with the console library:

```cmake
add_application(PrintWatch
    SOURCES src/main.c
    RESOURCES resources/PrintWatch.r
)
target_link_libraries(PrintWatch RetroConsole)
```

### Systematic Bug Hunting

1. **Reproduce** — Erik describes the bug, Claude identifies candidate code
2. **Hypothesize** — Claude forms a single hypothesis about root cause
3. **Add diagnostics** — Claude adds a printf or alert dialog to verify
4. **Build and deploy** — Claude rebuilds, copies to shared folder
5. **Observe** — Erik tests and reports what the diagnostic shows
6. **Fix** — Claude fixes the root cause, removes diagnostics, rebuilds
7. **Verify** — Erik confirms the fix

### Alert-Based Debugging

For GUI apps where console output isn't practical, use `ParamText` +
`StopAlert` to display diagnostic values:

```c
Str255 msg;
sprintf((char*)&msg[1], "value=%d", someVar);
msg[0] = strlen((char*)&msg[1]);
ParamText(msg, "\p", "\p", "\p");
StopAlert(128, nil);  /* 128 = ALRT resource ID */
```

### Common Pitfalls

- **Memory:** Classic Mac apps have limited heap. Watch for memory leaks —
  there's no virtual memory safety net.
- **Resource forks:** If your app crashes on launch, the resource fork is
  probably malformed. Check Rez compilation output.
- **Toolbox traps:** If a Toolbox call crashes, you're probably passing a bad
  handle or calling it before InitGraf/InitWindows.
- **68K alignment:** Structs may not be laid out how you expect. Check with
  `sizeof()` in a printf.

## Automated Testing with Mini vMac

For operations that can be verified programmatically (calculations, data
processing, etc.), use Mini vMac + LaunchAPPL:

```c
int main() {
    // Run tests
    int failures = run_tests();
    
    // Exit with status — LaunchAPPL captures the exit code
    return failures;
}
```

```bash
LaunchAPPL -e minivmac build/MyApp.dsk
echo "Exit code: $?"
```

Claude can run this and check the exit code without Erik needing to
interact with the emulator.

**Note:** This requires Mini vMac configured with AutoQuit and a Mac Plus
ROM. See EMULATOR_SETUP.md for details.

## Tips for Working with Claude Code

### What Claude Can Do

- Edit all source files (C, C++, Rez `.r` files, CMakeLists.txt)
- Run the full build (`cmake --build`)
- Read and interpret compiler/linker errors
- Copy build artifacts to the Basilisk II shared folder
- Run automated tests via LaunchAPPL + Mini vMac (if configured)
- Read Retro68 header files for API reference
- Consult Inside Macintosh documentation (if available locally)

### What Claude Cannot Do

- See the emulator screen (Erik must describe what's happening)
- Interact with the emulated Mac's GUI
- Set breakpoints or step through code
- Access the emulated Mac's filesystem directly (only via shared folder)

### Effective Bug Reports from Erik

The more specific, the faster Claude can fix it:
- "The window opens but nothing draws inside it"
- "Clicking the menu item crashes — the emulator shows bomb dialog, ID=2"
- "The text appears but it's offset 20 pixels to the right"

Less helpful:
- "It doesn't work"
- "Something's wrong with the display"

### Reference Material

Claude has general knowledge of the classic Mac Toolbox, but for specific
API details, having local copies of Inside Macintosh helps:

- **Inside Macintosh: Macintosh Toolbox Essentials** — events, windows, menus
- **Inside Macintosh: More Macintosh Toolbox** — dialogs, controls, lists
- **Inside Macintosh: Imaging with QuickDraw** — drawing, fonts, colors

These can be found as PDFs online and stored in `docs/reference/` for
Claude to consult during development.
