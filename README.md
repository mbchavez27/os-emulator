# os-emulator

CSOPESY command-line OS emulator with FCFS scheduler (`feat/fcfs-homework`).

## What it is right now

Interactive REPL plus FCFS CPU scheduler:

- Scheduler: First-Come-First-Serve, non-preemptive FIFO via isolated `pickNextFCFS()`.
- Threads: 1 scheduler thread + 4 CPU worker threads (`Scheduler::NUM_CORES = 4`).
- Auto-start: creates 10 processes (`screen_01`–`screen_10`), 100 print commands each.
- `print` (homework-only): each process appends to `logs/<name>.txt` with timestamp + core:
  `(MM/DD/YYYY HH:MM:SSAM/PM) Core:<n> "Hello world from <name>!"`.
- `screen -ls`: Running (`name (start) Core:N done/total`) vs Finished (`name (start) Finished done/total`) in FCFS order.

Recognized commands:

| Command | Current behavior |
| --- | --- |
| `initialize` | Prints stub acknowledgment |
| `screen -ls` | Shows Running / Finished process lists |
| `screen` | Prints stub acknowledgment (use `screen -ls`) |
| `scheduler-start` | Starts scheduler (auto-started on launch) |
| `scheduler-stop` | Stops scheduler threads |
| `report-util` | Prints stub acknowledgment |
| `clear` | Clears console via `system("clear"/"cls")`, re-prints header |
| `exit` | Stops scheduler, sets `Emulator::is_app_alive = false`, exits loop |

Unknown input prints `Command not recognized. Please try again.` Empty lines are ignored. EOF (Ctrl+D / Ctrl+Z) also exits.

## Project structure

```text
include/
  AsciiArt.h       # convert_to_ascii() declaration, ASCII_ROWS constant
  CommandHandler.h # REPL loop declaration, borrows Emulator + Scheduler by reference
  ConsoleColors.h  # ANSI color constants (header-only, no build change)
  Emulator.h       # shared liveness state (is_app_alive, exit_app())
  Process.h        # minimal FCFS process struct (name, arrival_id, done, state, core)
  Scheduler.h      # FCFS scheduler (1 + 4 threads, pickNextFCFS policy)
src/
  main.cpp            # owns Emulator + Scheduler, auto-starts scheduler
  CommandHandler.cpp  # command parsing and dispatch (screen -ls, exit stops scheduler)
  Scheduler.cpp       # schedLoop, workerLoop, file logging
  AsciiArt.cpp        # 5-row blocky font (A-Z, 0-9, space, ! ? .)
CMakeLists.txt
specs/
  fcfs_specs.md    # homework spec
```

## Dependencies

- CMake >= 3.16
- C++17 compiler: GCC (tested 15.2.1), Clang, or MSVC
- No third-party libraries
- Linux uses `clear`, Windows uses `cls` for the `clear` command
- Colors use ANSI escape codes; needs an ANSI-capable terminal (Windows Terminal, modern cmd/PowerShell work; raw Win32 console may not show colors)

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

## FCFS homework run

```bash
cmake -S . -B build
cmake --build build
./build/os-emulator
```

Inside the emulator:

```text
screen -ls   # every 1-2s until all 10 show under Finished processes
exit         # stops scheduler, closes emulator
```

Expected: finishes in FCFS order, produces 10 files:

```bash
ls logs/screen_*.txt        # screen_01.txt … screen_10.txt
head -n 5 logs/screen_01.txt
zip -j fcfs_logs.zip logs/screen_*.txt  # submission bundle
```

Notes:

- File logging is homework-only. Disable for MP via `Scheduler::setFileLogEnabled(false)` — disk I/O hurts scheduler perf.
- Generated `logs/` and `process*.txt` logs are gitignored, not committed.
