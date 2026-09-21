# os-emulator

CSOPESY command-line OS emulator (early stub stage).

## What it is right now

Interactive REPL that prints an ASCII `CSOPESY` header and prompts `Enter a command: `.
Commands are parsed by first token (leading whitespace trimmed, arguments ignored).

Recognized commands:

| Command | Current behavior |
| --- | --- |
| `initialize` | Prints stub acknowledgment |
| `screen` | Prints stub acknowledgment |
| `scheduler-start` | Prints stub acknowledgment |
| `scheduler-stop` | Prints stub acknowledgment |
| `report-util` | Prints stub acknowledgment |
| `clear` | Clears console via `system("clear"/"cls")`, re-prints header |
| `exit` | Sets `Emulator::is_app_alive = false`, exits loop |

Unknown input prints `Command not recognized. Please try again.` Empty lines are ignored. EOF (Ctrl+D / Ctrl+Z) also exits.

## Project structure

```text
include/
  AsciiArt.h       # convert_to_ascii() declaration, ASCII_ROWS constant
  CommandHandler.h # REPL loop declaration, borrows Emulator by reference
  Emulator.h       # shared liveness state (is_app_alive, exit_app())
src/
  main.cpp            # owns Emulator, runs CommandHandler
  CommandHandler.cpp  # command parsing and dispatch
  AsciiArt.cpp        # 5-row blocky font (A-Z, 0-9, space, ! ? .)
CMakeLists.txt
```

## Dependencies

- CMake >= 3.16
- C++17 compiler: GCC (tested 15.2.1), Clang, or MSVC
- No third-party libraries
- Linux uses `clear`, Windows uses `cls` for the `clear` command

## How to run

```bash
cmake -S . -B build
cmake --build build
./build/os-emulator
```

One-step build + run:

```bash
cmake --build build --target run
```

Windows (PowerShell):

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\os-emulator.exe
```

Quick non-interactive check:

```bash
printf "initialize\nscreen\nfoo\nclear\nexit\n" | ./build/os-emulator
```
