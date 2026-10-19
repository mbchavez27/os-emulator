# CSOPESY Emulator - System Specifications

## 1. Overview
The CSOPESY Emulator is a main menu console application designed to simulate basic Operating System functionalities, specifically focusing on CPU scheduling, process management, and a mock screen multiplexer similar to the Linux `screen` command. 

## 2. Main Menu Commands
The console recognizes the following core commands. Note that `initialize` **must** be called before any other command is recognized, with the exception of `exit`.

*   **`initialize`**: Initializes the processor configuration by reading parameters from the `config.txt` file. 
*   **`exit`**: Terminates the console application.
*   **`screen`**: Accesses the screen multiplexer (see Section 3).
*   **`scheduler-start`**: Continuously generates a batch of dummy processes for the CPU scheduler. Processes are assigned human-readable names (e.g., `p01`, `p02`, ..., `p1240`).
*   **`scheduler-stop`**: Stops the generation of dummy processes.
*   **`report-util`**: Generates a CPU utilization report and saves it to `csopesy-log.txt`. The output is identical to the `screen -ls` command (see Section 3.3).

## 3. Screen Command Specifications
The `screen` command emulates a screen multiplexer. 

### 3.1. Process Creation and Attachment (`screen -s <process name>`)
*   Creates a new process and attaches the console to its screen.
*   The main menu clears, and the user enters the process screen environment.
*   *(Refer to `image_71a0ba.png` for the CLI mockup)*

**Commands available inside the process screen:**
*   **`process-smi`**: Prints information about the process. Dummy instructions run in the background. Running this command provides updated details and accompanying logs from the print instructions. If the process has finished, it prints `"Finished!"` after the process name, ID, and logs.
*   **`exit`**: Detaches from the process screen and returns the user to the main menu.

### 3.2. Re-attaching to a Process (`screen -r <process name>`)
*   Allows the user to re-attach to an existing running process from the main menu.
*   If the process name is not found or has finished execution, the console prints: `"Process <process name> not found."`
*   Finished processes can no longer be accessed via screen attachment.

### 3.3. Listing Processes (`screen -ls`)
*   Lists the CPU utilization, cores used, and cores available.
*   Prints a summary of all running and finished processes.
*   *(Refer to `image_71a083.png` for the CLI mockup)*

## 4. Barebones Process Instructions
Dummy processes run basic programming-like instructions. These are randomly generated based on bounds set in `config.txt`. 

### Instruction Set:
*   **`PRINT (msg)`**: Displays an output string to the console (only visible inside the attached screen). Can print 1 variable (e.g., `PRINT ("Value from: " + x)`). Unless specified, `msg` should default to `"Hello world from <process_name>!"`.
*   **`DECLARE (var, value)`**: Declares a `uint16` variable with name `var` and default `value`.
*   **`ADD (var1, var2/value, var3/value)`**: Addition operation: `var1 = var2/value + var3/value`. Variables not previously declared are automatically declared with a value of 0. Can accept `uint16` values.
*   **`SUBTRACT (var1, var2/value, var3/value)`**: Subtraction operation: `var1 = var2/value - var3/value`.
*   **`SLEEP (X)`**: Sleeps the current process for `X` (`uint8`) CPU ticks, relinquishing the CPU.
*   **`FOR([instructions], repeats)`**: Performs a for-loop over a set/array of instructions. 

### Notes on Instructions:
*   Instructions are pre-determined/randomized (e.g., via `scheduler-start`), not typed by the user.
*   Variables are stored in memory and are only released when the process finishes.
*   `uint16` variables are clamped between `0` and `max(uint16)`.
*   `FOR` loops can be nested up to 3 times.

## 5. CPU Scheduler and Ticks
*   **Real-time Scheduling**: The scheduler continuously schedules processes as long as the console is alive based on the chosen algorithm (`fcfs` or `rr`).
*   **CPU Ticks**: A CPU tick is an integer counter tallying the number of frame passes. 
*   *(Refer to `image_71a0da.png` for the pseudocode loop representing CPU ticks)*

## 6. Configuration Setting (`config.txt`)
The `initialize` command reads processor configuration and process attributes from `config.txt`. The file uses a space-separated format. 

*(Refer to `image_71a05c.png` for a sample `config.txt` snippet)*

| Parameter | Description | Range / Values |
| :--- | :--- | :--- |
| `num-cpu` | Number of CPUs available. | `[1, 128]` |
| `scheduler` | The scheduler algorithm. | `"fcfs"` or `"rr"` |
| `quantum-cycles` | The time slice for each processor (only used if scheduler is round-robin). | `[1, 2^32]` |
| `batch-process-freq`| Frequency of generating processes in `scheduler-start` (in CPU cycles). If 1, a new process is generated every cycle. | `[1, 2^32]` |
| `min-ins` | Minimum instructions/command per process. | `[1, 2^32]` |
| `max-ins` | Maximum instructions/command per process. | `[1, 2^32]` |
| `delays-per-exec` | Delay before executing the next instruction in CPU cycles (busy-waiting). If 0, executes per cycle. | `[0, 2^32]` |