# os-emulator

CSOPESY command-line OS emulator. MCO1 is being built phase by phase; configuration
loading is implemented.

## What it is right now

Interactive REPL with validated configuration loading. `initialize` reads `config.txt`;
until it succeeds, every command except `initialize` and `exit` is rejected. Scheduling,
processes, and the screen multiplexer are not implemented yet.

Recognized commands:

| Command | Current behavior |
| --- | --- |
| `initialize` | Loads and validates `config.txt` |
| `screen` | Stub (requires initialization) |
| `scheduler-start` | Stub (requires initialization) |
| `scheduler-stop` | Stub (requires initialization) |
| `report-util` | Stub (requires initialization) |
| `clear` | Clears console via `system("clear"/"cls")`, re-prints header |
| `exit` | Sets `Emulator::is_app_alive = false`, exits loop |

Unknown input prints `Command not recognized. Please try again.` Empty lines are ignored. EOF (Ctrl+D / Ctrl+Z) also exits.

## Configuration (`config.txt`)

`initialize` reads `config.txt` from the current working directory. The file is
space-separated, one key per line; values may be quoted (`scheduler "rr"`).

| Key | Range | Meaning |
| --- | --- | --- |
| `num-cpu` | `[1, 128]` | Number of CPU worker threads |
| `scheduler` | `fcfs` or `rr` | Scheduling algorithm |
| `quantum-cycles` | `[1, 4294967295]` | Time slice for round-robin |
| `batch-process-freq` | `[1, 4294967295]` | Cycles between generated processes |
| `min-ins` | `[1, 4294967295]` | Minimum instructions per process |
| `max-ins` | `[1, 4294967295]` | Maximum instructions per process |
| `delay-per-exec` | `[0, 4294967295]` | Cycles to wait between instructions |

- `min-ins` must not exceed `max-ins`.
- Both `delay-per-exec` (mockup) and `delays-per-exec` (written spec) are accepted.
- Unknown keys are ignored; blank lines and `#` comments are skipped.
- `config.txt` is read relative to where the executable runs, not where it lives.

Invalid input keeps the system uninitialized and reports the offending key:

```text
Config key 'num-cpu' must be in [1, 128], got '0'.
```

## Project structure

```text
include/
  AsciiArt.h       # convert_to_ascii() declaration, ASCII_ROWS constant
  CommandHandler.h # REPL loop declaration, borrows Emulator by reference
  Config.h         # validated configuration value
  ConsoleColors.h  # ANSI color constants (header-only, no build change)
  Emulator.h       # liveness state + loaded configuration
  Instruction.h    # random program instructions (Op, Instruction, Program)
src/
  main.cpp            # owns Emulator, runs CommandHandler
  CommandHandler.cpp  # command parsing, gating, and dispatch
  Config.cpp          # config.txt parsing and validation
  Instruction.cpp     # seeded instruction generator + static size
  AsciiArt.cpp        # 5-row blocky font (A-Z, 0-9, space, ! ? .)
config.txt            # sample configuration
CMakeLists.txt
specs/
  csopesy_emulator_specifications.md  # machine project spec
```

## Dependencies

- CMake >= 3.16
- C++17 compiler: GCC (tested 15.2.1), Clang / Apple Clang, or MSVC
- No third-party libraries
- `clear` command uses `system("clear")` on Fedora/macOS, `system("cls")` on Windows
- Colors use ANSI escape codes; needs an ANSI-capable terminal (Terminal/iTerm2 on macOS, GNOME Terminal/Konsole on Fedora, Windows Terminal or modern cmd/PowerShell on Windows)

## Prerequisites

### Fedora

```bash
sudo dnf install cmake gcc-c++ make
```

Optional Clang alternative:

```bash
sudo dnf install cmake clang make
```

Single-config generator (Makefiles/Ninja), so the binary lands at `build/os-emulator`.

### macOS

```bash
xcode-select --install
brew install cmake
```

Uses Apple Clang by default. Single-config generator, so the binary lands at `build/os-emulator`. Any ANSI-capable terminal (Terminal.app, iTerm2) works.

### Windows

Install via winget (PowerShell, admin):

```powershell
winget install Kitware.CMake Git.Git Microsoft.VisualStudio.2022.Community
```

Select the `Desktop development with C++` workload in the Visual Studio installer. Alternatively, install only Build Tools for Visual Studio with the same workload.

MSVC is a multi-configuration generator, so the binary lands at `build/Debug/os-emulator.exe` by default, or `build/Release/os-emulator.exe` when building with `--config Release`. Use Windows Terminal for ANSI colors.

## How to run

> `build/` is gitignored and does not exist after a fresh clone. `cmake --build` never configures, so `cmake -S . -B build` must run once before any build. Skipping it fails with `build is not a directory`.

### Fedora

```bash
cmake -S . -B build
cmake --build build
./build/os-emulator
```

### macOS

```bash
cmake -S . -B build
cmake --build build
./build/os-emulator
```

### Windows (Git Bash)

```bash
cmake -S . -B build
cmake --build build
./build/Debug/os-emulator.exe
```

Release build:

```bash
cmake --build build --config Release
./build/Release/os-emulator.exe
```

### Windows (PowerShell)

```powershell
cmake -S . -B build
cmake --build build
.\build\Debug\os-emulator.exe
```

Release build:

```powershell
cmake --build build --config Release
.\build\Release\os-emulator.exe
```

### One-step build + run

Requires the configure step above to have run at least once:

```bash
cmake --build build --target run
```

With MSVC, specify the config explicitly:

```bash
cmake --build build --config Release --target run
```

### Quick non-interactive check

Run from the repository root so `config.txt` is found:

Fedora / macOS:

```bash
printf "initialize\nscreen\nfoo\nclear\nexit\n" | ./build/os-emulator
```

Windows (Git Bash):

```bash
printf "initialize\nscreen\nfoo\nclear\nexit\n" | ./build/Debug/os-emulator.exe
```

Windows (PowerShell):

```powershell
"initialize", "screen", "foo", "clear", "exit" | .\build\Debug\os-emulator.exe
```
