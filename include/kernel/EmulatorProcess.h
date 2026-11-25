#pragma once

#include "kernel/Instruction.h"
#include "kernel/ProcessMemory.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/** @brief When and where one PRINT executed; parallel to Process::logs(). */
struct LogStamp
{
    std::chrono::system_clock::time_point at; /**< Wall-clock time of the PRINT. */
    std::optional<std::size_t> core;          /**< Empty when stepped outside a core. */
};

/** @brief Where a process is in its lifecycle. */
enum class ProcessState
{
    READY,    /**< Waiting in the ready queue for a core. */
    RUNNING,  /**< Assigned to a core. */
    WAITING,  /**< Sleeping after a SLEEP; holds no core. */
    FINISHED  /**< Ran its last instruction; never changes again. */
};

/** @brief What a single Process::step() did. */
enum class StepOutcome
{
    EXECUTED, /**< Ran one instruction; more remain. */
    SLEEPING, /**< Started or is in a SLEEP and gave up its core. */
    FINISHED  /**< Has no instructions left. */
};

/**
 * @brief A runnable process: identity, symbol table, logs, and program counter.
 *
 * The generated Program is flattened into a linear code vector at construction,
 * so stepping is a single index advance plus an explicit FOR frame stack. The
 * class knows nothing about threads, cores, or the scheduler.
 *
 * Variables live in the process's own memory: the first kSymbolTableBytes bytes
 * are the symbol table, 2 bytes per uint16 variable in declaration order. Every
 * variable read and write goes through the ProcessMemory passed to step(), so
 * it can page-fault. Once kMaxVariables are declared, new names are ignored.
 */
class Process
{
public:
    /** @brief Size of the symbol table segment at the start of process memory. */
    static constexpr std::uint64_t kSymbolTableBytes = 64;
    /** @brief Variables that fit in the symbol table, one uint16 each. */
    static constexpr std::size_t kMaxVariables = kSymbolTableBytes / 2;

    /**
     * @brief Creates a ready process.
     *
     * @param id positive process ID
     * @param name non-empty, human-readable name such as "p01"
     * @param program the instructions to run; must not be empty
     * @throws std::invalid_argument if id is 0, name is empty, or program is empty
     */
    Process(std::uint32_t id, std::string name, Program program);

    /** Entity identity: copying a process is meaningless. */
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;
    Process(Process &&) noexcept = default;
    Process &operator=(Process &&) noexcept = default;

    /** @return the process ID */
    std::uint32_t id() const noexcept;
    /** @return the process name */
    const std::string &name() const noexcept;
    /** @return when the process was created */
    std::chrono::system_clock::time_point startedAt() const noexcept;
    /** @return the current lifecycle state */
    ProcessState state() const noexcept;
    /** @return true once the last instruction has run */
    bool isFinished() const noexcept;
    /** @return true while a SLEEP is counting down */
    bool isSleeping() const noexcept;

    /** @return every PRINT message so far, oldest first */
    const std::vector<std::string> &logs() const noexcept;

    /** @return the time and core of each entry in logs(), same length and order */
    const std::vector<LogStamp> &logStamps() const noexcept;

    /**
     * @return the declared variables as of their last read or write; empty once finished
     *
     * A copy kept for display, so snapshots never touch (or page-fault) process
     * memory. Execution always reads the symbol table itself.
     */
    const std::map<std::string, std::uint16_t> &variables() const noexcept;

    /**
     * @param name a variable name
     * @return its byte offset in the symbol table, or std::nullopt if not declared
     */
    std::optional<std::uint64_t> symbolOffset(const std::string &name) const;

    /** @return the 1-based line most recently executed, or 0 before the first step */
    std::size_t currentLine() const noexcept;

    /** @return the flattened program size, shown as `Lines of code:` */
    std::size_t linesOfCode() const noexcept;

    /** @return the 0-based index of the next instruction to run; linesOfCode() once finished */
    std::size_t nextInstructionIndex() const noexcept;

    /** @return the ticks left in the current SLEEP, or 0 when awake */
    std::uint8_t sleepTicksRemaining() const noexcept;

    /**
     * @brief Moves RUNNING to READY when the scheduler preempts the process.
     *
     * @throws std::logic_error if the process is neither RUNNING nor READY
     */
    void markReady();

    /**
     * @brief Moves READY to RUNNING when the scheduler assigns a core.
     *
     * @throws std::logic_error if the process is neither READY nor RUNNING
     */
    void markRunning();

    /**
     * @brief Executes exactly one instruction.
     *
     * SLEEP executes, advances the program counter, and returns SLEEPING with
     * the loop frame stack intact. A finished process never mutates again.
     *
     * @param memory the process's memory; holds the symbol table
     * @param core the core executing the step; recorded on any log line it emits
     * @return what the step did
     */
    StepOutcome step(ProcessMemory &memory, std::optional<std::size_t> core = std::nullopt);

    /** @brief Counts down one SLEEP tick; wakes or finishes the process at zero. */
    void tickSleep();

private:
    /** @brief One active FOR loop: the body range and the repeats still to run. */
    struct ForFrame
    {
        std::size_t bodyStart = 0;          /**< First body instruction. */
        std::size_t bodyEnd = 0;            /**< One past the last body instruction. */
        std::uint16_t repeatsRemaining = 0; /**< Passes left after the current one. */
    };

    std::uint32_t id_;
    std::string name_;
    std::chrono::system_clock::time_point startedAt_ = std::chrono::system_clock::now();
    ProcessState state_ = ProcessState::READY;

    std::vector<Instruction> code_;         /**< Pre-order flattened instructions. */
    std::vector<std::size_t> forBodySizes_; /**< Parallel to code_: FOR body size in flat nodes. */
    std::vector<ForFrame> frames_;          /**< Active FOR loops, innermost last. */

    std::size_t pc_ = 0;                   /**< Index of the next instruction in code_. */
    std::size_t currentLine_ = 0;          /**< 1-based line most recently executed. */
    std::uint8_t sleepTicksRemaining_ = 0; /**< Ticks left in the current SLEEP. */

    std::map<std::string, std::uint64_t> symbols_;   /**< Variable name -> symbol-table byte offset. */
    std::map<std::string, std::uint16_t> variables_; /**< Display copy of the values; see variables(). */
    std::vector<std::string> logs_;
    std::vector<LogStamp> logStamps_; /**< Parallel to logs_: one stamp per entry. */

    /**
     * @brief Appends the message, plus the variable's value if any, to the logs.
     *
     * @param print the PRINT instruction
     * @param memory holds the variable
     * @param core the executing core, recorded on the log line
     */
    void execute(const PrintInstruction &print, ProcessMemory &memory, std::optional<std::size_t> core);
    /** @brief Sets a variable to its declared value; ignored once the symbol table is full. */
    void execute(const DeclareInstruction &declare, ProcessMemory &memory, std::optional<std::size_t> core);
    /** @brief Stores lhs + rhs, clamped to uint16. */
    void execute(const AddInstruction &add, ProcessMemory &memory, std::optional<std::size_t> core);
    /** @brief Stores lhs - rhs, clamped to uint16. */
    void execute(const SubtractInstruction &subtract, ProcessMemory &memory, std::optional<std::size_t> core);
    /** @brief Starts a SLEEP countdown; a zero-tick SLEEP does nothing. */
    void execute(const SleepInstruction &sleep, ProcessMemory &memory, std::optional<std::size_t> core);
    /** @brief Pushes a loop frame for the FOR at pc_, or skips an empty body. */
    void execute(const ForInstruction &loop, ProcessMemory &memory, std::optional<std::size_t> core);

    /**
     * @brief Finds a variable's symbol-table slot, declaring it if there is room.
     *
     * A new variable is written as 0 so stale bytes never leak into it.
     *
     * @param memory holds the symbol table
     * @param name the variable name
     * @return its byte offset, or std::nullopt if it is new and all kMaxVariables slots are taken
     */
    std::optional<std::uint64_t> declare(ProcessMemory &memory, const std::string &name);

    /**
     * @brief Reads a variable, auto-declaring it as 0 on first use (per the spec).
     *
     * @param memory holds the symbol table
     * @param name the variable name
     * @return its value; 0 if it could not be declared because the table is full
     */
    std::uint16_t loadVariable(ProcessMemory &memory, const std::string &name);

    /**
     * @brief Writes a variable, declaring it if new; dropped if the table is full.
     *
     * @param memory holds the symbol table
     * @param name the variable name
     * @param value the value to store
     */
    void storeVariable(ProcessMemory &memory, const std::string &name, std::uint16_t value);

    /**
     * @brief Evaluates an operand.
     *
     * @param memory holds the symbol table
     * @param operand a variable name or a literal
     * @return the literal, or the variable's value (auto-declared as 0)
     */
    std::uint16_t resolve(ProcessMemory &memory, const Operand &operand);

    /** @brief Moves pc_ past the executed instruction, looping FOR bodies as needed. */
    void advance();

    /** @brief Marks the process finished and forgets its symbol table; the scheduler frees the memory. */
    void finish();
};
