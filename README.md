# os-emulator

CSOPESY command-line OS emulator.

## What it is right now

Interactive REPL with validated configuration loading, FCFS / round-robin scheduling, dummy
process generation, and a mock `screen` multiplexer. `initialize` reads `config.txt`; until it
succeeds, every command except `initialize` and `exit` is rejected.

## Usage

| Command | Behavior |
| --- | --- |
| `initialize` | Loads and validates `config.txt`, then starts the CPU scheduler with the configured algorithm; it runs until `exit`. Prints `Initialization successful.` or the offending key. Repeated calls print `System is already initialized.` |
| `scheduler-start` / `scheduler-test` | Starts dummy process generation (one process per `batch-process-freq` CPU ticks). |
| `scheduler-stop` | Stops generation synchronously; queued and running processes continue to completion. |
| `screen -ls` / `screen-ls` | Prints CPU utilization, core counts, and running/finished process rows. |
| `screen -s <name>` | Creates process `<name>` and attaches the console to its process screen. |
| `screen -r <name>` | Reattaches to an existing unfinished process; `Process <name> not found.` for missing or finished names. |
| `screen` (bare or unknown flag) | Prints usage for `screen -s`, `screen -r`, and `screen -ls`. |
| `report-util` | Writes the same report as `screen -ls` to `csopesy-log.txt`, replacing the previous report. |
| `exit` | Stops generation and all CPU threads, then ends the loop. |
| anything else | `Command not recognized. Please try again.` |

Empty lines are ignored. EOF (Ctrl+D / Ctrl+Z) also exits. Both the main menu
and process screens use the `root:\> ` prompt, as in the spec mockup. Attaching
prints `Process screen: <name>` and detaching reprints the header, so the screen
you are on is visible after every switch.

Commands inside a process screen:

| Command | Behavior |
| --- | --- |
| `process-smi` | Prints the process name, ID, logs, and progress (`Current instruction line` / `Lines of code`), or `Finished!` when the process is done. |
| `exit` | Detaches and returns to the main menu (it does **not** shut the emulator down). |
| anything else | `Unknown command '<input>'. Available commands: process-smi, exit` |

## Scheduling behavior

Both reports use one formatter over a copied scheduler snapshot. Utilization is
assigned cores / configured cores; available cores are configured cores minus
assigned cores. Only processes on a core appear in the running section; ready and
sleeping processes are omitted. Timestamps record process creation in local time; finished rows show
`Finished` and total / total. Separate commands capture separate moments while
processes advance; formatting the same snapshot always produces identical output.

`scheduler "fcfs"` keeps each process on its core until it finishes or sleeps;
`quantum-cycles` has no effect. `scheduler "rr"` gives each dispatched process a
fresh quantum, measured in CPU ticks from when its worker begins the slice. Expiry moves it
to the back of the ready queue with its program position intact. Sleeping releases
the core, and waking starts a fresh quantum on the next dispatch. With
`delay-per-exec 0`, instructions execute continuously within the time slice.
Quantum expiry lets another process run, but each process keeps its next
permitted execution tick across preemption, core changes, and CPU stop/resume.
Time spent off-core counts toward the execution delay.

`scheduler-start` creates one process per `batch-process-freq` CPU ticks, beginning
after one full interval. Names use a minimum of two digits (`p01`, `p02`, ...,
`p99`, `p100`, ...); IDs and names continue across stop/start cycles. Programs use
the configured instruction bounds and their process ID as the generator seed.
Generated SLEEP instructions wait one tick; the interpreter supports the full
`uint8` sleep range. FOR bodies still repeat and count toward the program's line count.
Starting twice keeps the original generation schedule. `scheduler-stop` stops
creation synchronously and leaves the CPU running so existing work can finish.
Restarting generation waits a fresh interval and keeps process history.

Processes release their variable storage when they finish. Failed manual creation
prints an error and returns to the main menu. Failed automatic creation stops
generation and reports an error on the next main-menu command; existing processes
continue running. `scheduler-start` can retry generation after the failure.

To see generated processes, enter `initialize`, then `scheduler-start`, wait at
least one batch interval, and enter `screen -ls`. Use `scheduler-stop` to stop
adding work; subsequent reports show the remaining work progressing to completion.

With the demo config (8 CPUs, RR, quantum 1, batch frequency 1, 100 instruction
lines, delay 0), run `scheduler-test`, wait 10 seconds, run `scheduler-stop`,
wait 30 seconds, then run `screen-ls`. The `scheduler-generation` check runs this
timing scenario and requires every generated process to finish with 0% utilization.
Zero-delay execution intentionally differs from the written spec's one-instruction-
per-cycle rule to meet this demonstration target.

## Configuration (`config.txt`)

`initialize` reads `config.txt` from the current working directory. The file is
space-separated, one key per line; values may be quoted (`scheduler "rr"`).
Every field below maps 1:1 to `include/Config.h`.

| Key | `Config.h` field | Type | Range / values |
| --- | --- | --- | --- |
| `num-cpu` | `numCpu` | `int` | `[1, 128]` |
| `scheduler` | `scheduler` | `SchedulerType` | `fcfs` or `rr` |
| `quantum-cycles` | `quantumCycles` | `uint64_t` | `[1, 4294967296]` (2^32) |
| `batch-process-freq` | `batchProcessFreq` | `uint64_t` | `[1, 4294967296]` (2^32) |
| `min-ins` | `minIns` | `uint64_t` | `[1, 4294967296]` (2^32) |
| `max-ins` | `maxIns` | `uint64_t` | `[1, 4294967296]` (2^32) |
| `delay-per-exec` | `delayPerExec` | `uint64_t` | `[0, 4294967296]` (2^32) |

- `min-ins` must not exceed `max-ins`.
- `delay-per-exec 0` executes without waiting between instructions. Positive
  values `d` wait `d` whole CPU cycles before the next instruction (busy-waiting),
  spacing instructions `d + 1` cycles apart; `1` runs one every 2 cycles.
- The spec's `delays-per-exec` spelling is accepted as an alias for the
  mockup's `delay-per-exec`.
- Unknown keys are ignored; blank lines and `#` comments are skipped.
- Missing keys fail with `Missing required config key: '<key>'.`
- `config.txt` is read relative to where the executable runs, not where it lives.

Invalid input keeps the system uninitialized and reports the offending key:

```text
Config key 'num-cpu' must be in [1, 128], got '0'.
```

## Project structure

The code is organized like an operating system: a **kernel** that schedules
and runs processes, a **shell** the user types into, and the **system** that
powers the machine on and off.

| Folder | Holds |
| --- | --- |
| `kernel/` | The scheduler (CPU ticks, cores, FCFS/RR dispatch), processes, and the instruction set with its program generator. |
| `config/` | Loading and validating `config.txt`. |
| `system/` | `Emulator`: owns the configuration and the kernel; `initialize` powers it on, `exit` shuts it down. |
| `shell/` | Everything the user sees: the main menu and process screens, `screen -ls` / `report-util` / `process-smi` output, and the header art. |

The kernel never includes anything from `shell/` or `system/`; the shell sits on top.

Every file tracked in git (`build/`, `csopesy-log.txt`, and `Testing/` are
generated and gitignored):

```text
.gitignore                      # ignores build/, logs, binaries, IDE files
CMakeLists.txt                  # emulator_core lib, os-emulator CLI, 5 ctest checks, warnings
LICENSE                         # MIT
README.md                       # this file
config.txt                      # sample configuration (read from the working directory)
checks/
  kernel/
    SchedulerGenerationCheck.cpp  # generation cadence, naming, bounds, restart, 40-second demo
    SchedulerQuantumCheck.cpp     # RR quantum preemption, sleep/wake budgets, delay spacing
  shell/
    ProcessScreenCheck.cpp      # end-to-end REPL check: screen -s / process-smi / detach
    ProcessSmiCheck.cpp         # process-smi formatting incl. Finished! output
    SchedulerReportCheck.cpp    # report-util + screen -ls formatting across load levels
include/
  kernel/
    EmulatorProcess.h           # process entity + step states (avoids Windows process.h)
    Instruction.h               # Op, Operand, Instruction, Program, generator declaration
    Scheduler.h                 # ProcessView + FCFS/RR scheduler (clock, dispatch, workers)
  config/
    Config.h                    # SchedulerType + validated configuration struct
  system/
    Emulator.h                  # liveness, loaded configuration, scheduler start/shutdown
  shell/
    AsciiArt.h                  # convertToAscii() declaration, ASCII_ROWS constant
    CommandHandler.h            # REPL loop declaration, borrows Emulator by reference
    ConsoleColors.h             # ANSI color constants (header-only)
    ProcessReport.h             # shared screen -ls / report-util formatter
    ProcessSmi.h                # process-smi formatter declaration
specs/
  csopesy_emulator_specifications.md  # machine project spec
src/
  kernel/
    Instruction.cpp             # seeded random program generator + program printing
    Process.cpp                 # flattening, per-instruction execution, FOR frames, uint16 clamp
    Scheduler.cpp               # tick clock, dispatch, workers, generation, sleepers
  config/
    Config.cpp                  # config.txt parsing and validation
  shell/
    AsciiArt.cpp                # 5-row blocky font (A-Z, 0-9, space, ! ? .)
    CommandHandler.cpp          # command parsing, gating, dispatch, process screen
    ProcessReport.cpp           # CPU utilization and process table formatting
    ProcessSmi.cpp              # process-smi logs/progress formatting
  main.cpp                      # entry point: owns Emulator, runs CommandHandler
```

## Dependencies

- CMake >= 3.16
- C++17 compiler: GCC (tested 15.2.1), Clang / Apple Clang, or MSVC
- No third-party libraries
- Entering and leaving a process screen clears the terminal with `system("clear")` on Fedora/macOS, `system("cls")` on Windows
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
printf "initialize\nscreen\nfoo\nexit\n" | ./build/os-emulator
```

Windows (Git Bash):

```bash
printf "initialize\nscreen\nfoo\nexit\n" | ./build/Debug/os-emulator.exe
```

Windows (PowerShell):

```powershell
"initialize", "screen", "foo", "exit" | .\build\Debug\os-emulator.exe
```

### Tests

The build includes scheduler/report checks covering idle, partial, and full
utilization, live snapshots, report commands, RR rotation, sleep/wake budgets,
stop/resume, FCFS/RR output comparisons on identical seeded programs, dummy
generation cadence, naming, bounds, restarts, draining after generation stops,
`process-smi` formatting (including `Finished!`), and an end-to-end
`screen -s` / `process-smi` / detach REPL pass. Run them with:

```bash
ctest --test-dir build -C Debug --output-on-failure
```

## Command examples

Outputs below come from a real run using `config.txt` in this repository
(`num-cpu 4`, `scheduler "rr"`, `quantum-cycles 5`, `batch-process-freq 1`,
`min-ins 1000`, `max-ins 2000`, `delay-per-exec 0`). Timestamps, IDs, line
counts, and the number of generated processes vary between runs.

Every run starts with the header:

```text
   ___    ___     ___     ___    ___    ___   __   __
  / __|  / __|   / _ \   | _ \  | __|  / __|  \ \ / /
 | (__   \__ \  | (_) |  |  _/  | _|   \__ \   \ V /
  \___|  |___/   \___/   |_|    |___|  |___/    |_|

--------------------------------------------
Welcome to CSOPESY Emulator!

Developers:
Chavez, Max Benedict B.
Leano, Jeremy L.
Go, Timothy Aaron S.
De La Calzada, Wanda
--------------------------------------------
```

### `initialize`

```text
root:\> initialize
Initialization successful.
```

Invalid config keeps the system uninitialized and reports the offending key:

```text
root:\> initialize
Config key 'num-cpu' must be in [1, 128], got '0'.
```

Calling it twice:

```text
root:\> initialize
System is already initialized.
```

### `scheduler-start` / `scheduler-stop`

```text
root:\> scheduler-start
Process generation started.

root:\> scheduler-start
Process generation is already running.

root:\> scheduler-stop
Process generation stopped. Existing processes will continue running.

root:\> scheduler-stop
Process generation is not running.
```

### `screen -ls`

```text
root:\> screen -ls
CPU utilization: 100%
Cores used: 4
Cores available: 0

----------------------------------------
Running processes:
p563   (11/04/2026 02:02:53AM)    Core: 0      1 / 1542
p564   (11/04/2026 02:02:53AM)    Core: 1      1 / 1447
p565   (11/04/2026 02:02:53AM)    Core: 2      1 / 1260
p137   (11/04/2026 02:02:52AM)    Core: 3      5 / 1734

Finished processes:
----------------------------------------
```

Once processes complete, they move to the finished section:

```text
Finished processes:
screen_01   (11/04/2026 02:02:12AM)    Finished     25 / 25
p02         (11/04/2026 02:02:13AM)    Finished     28 / 28
----------------------------------------
```

The layout follows the spec mockup. Running rows are
`name (created-at) Core: <core> <current line> / <total lines>`, sorted by core;
finished rows are `name (created-at) Finished <total> / <total>`. Columns are
padded to the longest listed name. Utilization is assigned / configured cores,
shown as a whole percentage (`100%`) or with two decimals when fractional
(`33.33%`). As in the mockup, "running" means currently on a core: processes
waiting in the ready queue or sleeping are not listed until they run again or
finish, and stay reachable with `screen -r`.

### `screen -s <name>` and `process-smi`

Attaching to a process while the generator is running (see the note in the
walkthrough — with the sample config the process queues behind the generated
backlog):

```text
root:\> screen -s demo1
Process screen: demo1
Type 'process-smi' for process information, 'exit' to return to the main menu.

root:\> process-smi

Process name: demo1
ID: 1390
Logs:

Current instruction line: 0
Lines of code: 1048

root:\> exit
```

`exit` clears the screen, reprints the header, and returns to the main-menu `root:\> ` prompt.

With no generator running, `process-smi` shows live `PRINT` output with the core
and execution timestamp of each log line:

```text
root:\> screen -s demo1
Process screen: demo1
Type 'process-smi' for process information, 'exit' to return to the main menu.

root:\> process-smi

Process name: demo1
ID: 1
Logs:
(11/04/2026 01:42:23AM) Core:0 "Hello world from demo1!"
(11/04/2026 01:42:23AM) Core:0 "Hello world from demo1!"
...
(11/04/2026 01:42:25AM) Core:0 "Hello world from demo1!"

Current instruction line: 45
Lines of code: 1037

root:\>
```

Once a process finishes, the progress block is replaced by `Finished!`.

Creating a duplicate name is rejected before attaching:

```text
root:\> screen -s demo1
Process demo1 already exists.
```

Malformed invocations print usage:

```text
root:\> screen -s
Usage: screen -s <process name>
```

### `screen -r <name>`

```text
root:\> screen -r demo1
Process screen: demo1
Type 'process-smi' for process information, 'exit' to return to the main menu.

root:\> process-smi

Process name: demo1
ID: 1
Logs:
(10/27/2026 09:20:51PM) Core:0 "Hello world from demo1!"
...

root:\> exit
```

Missing or finished processes:

```text
root:\> screen -r ghost
Process ghost not found.
```

### `report-util`

```text
root:\> report-util
Report generated at /path/to/os-emulator/csopesy-log.txt!
```

The message shows the file's absolute path (forward slashes, as in the mockup's
`C:/csopesy-log.txt`). `csopesy-log.txt` contains exactly what `screen -ls`
prints at that moment:

```text
CPU utilization: 100%
Cores used: 4
Cores available: 0

----------------------------------------
Running processes:
p563   (11/04/2026 02:02:53AM)    Core: 0      1 / 1542
p564   (11/04/2026 02:02:53AM)    Core: 1      1 / 1447
p565   (11/04/2026 02:02:53AM)    Core: 2      1 / 1260
p137   (11/04/2026 02:02:52AM)    Core: 3      5 / 1734

Finished processes:
----------------------------------------
```

### `exit`

`exit` stops generation and the CPU threads and terminates the process:

```text
root:\> exit
$ echo $?
0
```

### Unrecognized input

```text
root:\> foo
Command not recognized. Please try again.

root:\> screen
Usage:
  screen -s <process name>   Create a process and attach to it
  screen -r <process name>   Reattach to a running process
  screen -ls                 List running and finished processes

root:\> screen -r ghost
Process ghost not found.
```

## End-to-end walkthrough

Run from the repository root so `config.txt` is found:

```bash
cmake -S . -B build
cmake --build build
./build/os-emulator
```

Then enter the commands in this order (verified end to end from a clean
configure; the process exits with status 0):

```text
initialize
scheduler-start
screen -ls
screen -s demo1
process-smi
exit            <- detach back to the main menu
screen -r demo1
process-smi
exit            <- detach again
report-util
exit            <- shuts the emulator down
```

Transcript of a real run (headers trimmed):

```text
root:\> initialize
Initialization successful.

root:\> scheduler-start
Process generation started.

root:\> screen -ls
CPU utilization: 100%
Cores used: 4
Cores available: 0

----------------------------------------
Running processes:
p563   (11/04/2026 02:02:53AM)    Core: 0      1 / 1542
p564   (11/04/2026 02:02:53AM)    Core: 1      1 / 1447
p565   (11/04/2026 02:02:53AM)    Core: 2      1 / 1260
p137   (11/04/2026 02:02:52AM)    Core: 3      5 / 1734

Finished processes:
----------------------------------------

root:\> screen -s demo1
Process screen: demo1
Type 'process-smi' for process information, 'exit' to return to the main menu.

root:\> process-smi

Process name: demo1
ID: 1390
Logs:

Current instruction line: 0
Lines of code: 1048

root:\> exit

   ___    ___     ___     ___    ___    ___   __   __
  / __|  / __|   / _ \   | _ \  | __|  / __|  \ \ / /
 | (__   \__ \  | (_) |  |  _/  | _|   \__ \   \ V /
  \___|  |___/   \___/   |_|    |___|  |___/    |_|

--------------------------------------------
Welcome to CSOPESY Emulator!

Developers:
Chavez, Max Benedict B.
Leano, Jeremy L.
Go, Timothy Aaron S.
De La Calzada, Wanda
--------------------------------------------

root:\> screen -r demo1
Process screen: demo1
Type 'process-smi' for process information, 'exit' to return to the main menu.

root:\> process-smi

Process name: demo1
ID: 1390
Logs:

Current instruction line: 0
Lines of code: 1048

root:\> exit

   ___    ___     ___     ___    ___    ___   __   __
  / __|  / __|   / _ \   | _ \  | __|  / __|  \ \ / /
 | (__   \__ \  | (_) |  |  _/  | _|   \__ \   \ V /
  \___|  |___/   \___/   |_|    |___|  |___/    |_|

--------------------------------------------
Welcome to CSOPESY Emulator!

Developers:
Chavez, Max Benedict B.
Leano, Jeremy L.
Go, Timothy Aaron S.
De La Calzada, Wanda
--------------------------------------------

root:\> report-util
Report generated at /path/to/os-emulator/csopesy-log.txt!

root:\> exit
```

```bash
$ echo $?
0
```

Two behaviors to note:

- `exit` inside a process screen **detaches**; only `exit` at the main menu
  shuts the emulator down (per spec §3.1).
- The transcript above predates continuous zero-delay execution and one-tick
  generated sleeps. Current progress and completion timing differ.
