#pragma once

#include "Instruction.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

/// When and where one PRINT executed; parallel to Process::logs().
struct LogStamp
{
    std::chrono::system_clock::time_point at;
    std::optional<std::size_t> core; ///< Empty when stepped outside a core.
};

enum class ProcessState
{
    READY,
    RUNNING,
    WAITING,
    FINISHED
};

enum class StepOutcome
{
    EXECUTED,
    SLEEPING,
    FINISHED
};

/**
 * @brief A runnable process: identity, variables, logs, and program counter.
 *
 * The generated Program is flattened into a linear code vector at construction,
 * so stepping is a single index advance plus an explicit FOR frame stack. The
 * class knows nothing about threads, cores, or the scheduler.
 */
class Process
{
public:
    Process(std::uint32_t id, std::string name, Program program);

    Process(const Process &) = delete; // Entity identity: copying is meaningless.
    Process &operator=(const Process &) = delete;
    Process(Process &&) noexcept = default;
    Process &operator=(Process &&) noexcept = default;

    std::uint32_t id() const noexcept;
    const std::string &name() const noexcept;
    std::chrono::system_clock::time_point startedAt() const noexcept;
    ProcessState state() const noexcept;
    bool isFinished() const noexcept;
    bool isSleeping() const noexcept;

    const std::vector<std::string> &logs() const noexcept;

    /// Execution time and core of each entry in logs(), same length and order.
    const std::vector<LogStamp> &logStamps() const noexcept;
    const std::map<std::string, std::uint16_t> &variables() const noexcept;

    /// 1-based source line most recently executed; 0 before the first step.
    std::size_t currentLine() const noexcept;

    /// Static size of the flattened program, matching `Lines of code:`.
    std::size_t linesOfCode() const noexcept;

    std::uint8_t sleepTicksRemaining() const noexcept;

    /// RUNNING -> READY; used by the scheduler when a time slice expires.
    void markReady();

    /// READY -> RUNNING; called by the scheduler when a core is assigned.
    void markRunning();

    /**
     * @brief Executes exactly one instruction.
     *
     * SLEEP executes, advances the program counter, and returns SLEEPING with
     * the loop frame stack intact. A finished process never mutates again.
     *
     * @param core Core executing the step; recorded on any log line it emits.
     */
    StepOutcome step(std::optional<std::size_t> core = std::nullopt);

    /// Decrements the SLEEP countdown; wakes or finishes the process at zero.
    void tickSleep();

private:
    struct ForFrame
    {
        std::size_t bodyStart = 0;
        std::size_t bodyEnd = 0; ///< Exclusive.
        std::uint16_t repeatsRemaining = 0;
    };

    std::uint32_t id_;
    std::string name_;
    std::chrono::system_clock::time_point startedAt_ = std::chrono::system_clock::now();
    ProcessState state_ = ProcessState::READY;

    std::vector<Instruction> code_;         ///< Pre-order flattened instructions.
    std::vector<std::size_t> forBodySizes_; ///< Parallel: FOR body size in flat nodes.
    std::vector<ForFrame> frames_;

    std::size_t pc_ = 0;
    std::size_t currentLine_ = 0;
    std::uint8_t sleepTicksRemaining_ = 0;

    std::map<std::string, std::uint16_t> variables_;
    std::vector<std::string> logs_;
    std::vector<LogStamp> logStamps_; ///< Parallel: one stamp per logs_ entry.

    /// Returns the variable slot, inserting 0 when referenced for the first time.
    std::uint16_t &variableSlot(const std::string &name);
    std::uint16_t resolve(const Operand &operand);
    void advance();
};
