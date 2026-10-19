# OS Emulator: First-Come-First-Serve (FCFS) Scheduler Specifications

## 1. System Architecture & Threading

- **Scheduler Approach:** The emulator must implement a First-Come-First-Serve (FCFS) scheduling algorithm.
- **Multi-threading:** The system must utilize a multi-threaded approach consisting of:
  - One (1) dedicated thread for the scheduler.
  - One (1) thread per CPU worker.
- **CPU Cores:** The CPU scheduler must be declared with exactly **4 cores**.

## 2. Command Implementations

### The "print" Command

- Each process must create a dedicated text file where all its associated print commands are written.
- If there are 10 processes running, exactly 10 distinct text files should be created.
- **Log Format:** Each logged print command must include the timestamp of execution and the specific CPU core that executed it.
- **Format Example:** Based on the reference `image_e77581.png`[cite: 2], the text file should include a header and formatted log entries:
  - Header: `Process name: <process_name>` followed by `Logs:`[cite: 2].
  - Entry format: `(<MM/DD/YYYY> <HH:MM:SS AM/PM>) Core:<core_number> "Hello world from <process_name>!"`[cite: 2].

_Important Note: The file creation behavior for the "print" command is temporary and is strictly for this homework's grading. It must be disabled for the final machine project submission, as disk I/O directly impacts scheduler performance._

### The "screen -ls" Command

- This command displays the current status of all processes in the scheduler.
- **UI Layout:** Based on the reference `image_e7757e.png`[cite: 1], the output must be divided into two sections:
  - **Running processes:** Displays the process name, start timestamp, assigned core (e.g., `Core: 0`), and current progress (e.g., `1235 / 5876`)[cite: 1].
  - **Finished processes:** Displays the process name, start timestamp, a `Finished` status tag, and the completed progress (e.g., `5876 / 5876`)[cite: 1].

### The "exit" Command

- Closes the OS emulator gracefully.

## 3. Test Case & Execution Procedure

To evaluate the FCFS scheduler, execute the following procedure:

1.  **Initialize:** Upon starting the OS emulator, create 10 processes, assigning exactly 100 print commands to each process.
2.  **Monitor:** Periodically execute the `screen -ls` command every 1 to 2 seconds. Continue this until all 10 processes have transitioned to the "Finished processes" list.
3.  **Terminate:** Type the `exit` command to close the emulator.

## 4. Expected Results & Submission

- **Expected Result:** The emulator must finish all processes in a strict First-Come-First-Serve sequence and successfully produce 10 individual text files containing the associated, correctly formatted print statements.
- **Submission Requirement:** Compress and submit the generated text files as a single ZIP file.
