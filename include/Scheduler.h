#pragma once

#include "Config.h"
#include "EmulatorProcess.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

/// Immutable copy of one process's observable state at snapshot time.
struct ProcessView
{
    std::uint32_t id = 0;
    std::string name;
    ProcessState state = ProcessState::READY;
    std::size_t currentLine = 0;
    std::size_t linesOfCode = 0;
    std::uint8_t sleepTicksRemaining = 0;
    std::vector<std::string> logs;
    std::map<std::string, std::uint16_t> variables;
    std::chrono::system_clock::time_point startedAt;
    std::optional<std::size_t> core; ///< Unassigned for ready, sleeping, or finished processes.
};

/**
 * @brief FCFS / round-robin scheduler: one clock thread, one dispatch thread,
 *        and one worker thread per core.
 *
 * Locking strategy
 * ----------------
 *  - mutex_ guards every piece of shared scheduler state: the ready queue, the
 *    core table, the sleeper list, and the process list. A worker holds it only
 *    while calling Process::step() and updating its core slot, never during the
 *    delay busy-wait.
 *  - snapshot() copies that state into ProcessView values under mutex_, so
 *    callers inspect and print with no lock held.
 *  - Because step() runs under the single mutex, only one core executes an
 *    instruction at a time. Cores still overlap during the delay spin and the
 *    sleep waits. Per-core locks would allow parallel stepping but violate the
 *    one-mutex rule.
 *  - lifecycleMutex_ serializes CPU start/stop and enabling generation.
 *
 * FCFS runs to completion unless a process sleeps. Round-robin requeues an
 * unfinished process when its per-core CPU-cycle quantum expires.
 */
class Scheduler
{
public:
    Scheduler() = default;
    ~Scheduler();

    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;
    Scheduler(Scheduler &&) = delete;
    Scheduler &operator=(Scheduler &&) = delete;

    /// Starts the clock, dispatch, and worker threads. No-op while running.
    void start(const Config &config);

    /// Stops and joins every thread. Safe when not running.
    void stop();

    /// Enables dummy process creation and starts CPU threads if needed.
    /// Returns false if generation is already enabled, without resetting it.
    bool startGeneration(const Config &config);

    /// Stops creation synchronously; queued and running processes keep executing.
    /// Returns false if generation was already disabled.
    bool stopGeneration();

    bool isRunning() const noexcept;

    /// Completed CPU ticks; monotonic while running, frozen after stop().
    std::uint64_t cycleCount() const noexcept;

    /// Worker thread count from the last start(); 0 before start and after stop.
    std::size_t coreCount() const noexcept;

    /// Adds a process to the FIFO ready queue. Safe while running.
    void submitProcess(std::unique_ptr<Process> process);

    /// Creates a process named `name` with a program generated from `config`
    /// and enqueues it. Returns its id, or nullopt when the name is taken;
    /// finished processes keep their names. The check and the insert happen
    /// under one lock, so concurrent generation cannot claim the same name.
    /// @throws std::overflow_error when process ids are exhausted.
    std::optional<std::uint32_t> createProcess(const std::string &name, const Config &config);

    /// Immutable copy of every process, in submission order.
    std::vector<ProcessView> snapshot() const;

private:
    struct CoreState
    {
        bool busy = false;
        std::size_t processIndex = 0;
        std::uint64_t quantumEndCycle = 0; ///< Starts when the worker begins its new slice.
    };

    struct SleepEntry
    {
        std::size_t processIndex = 0;
        std::uint64_t wakeCycle = 0;
    };

    void tickLoop();
    void startLocked(const Config &config);
    void dispatchLoop();
    void workerLoop(std::size_t core);
    void delayForExec(std::uint64_t quantumEndCycle);

    void reapSleepersLocked();
    void generateProcessesLocked();
    void enqueueProcessLocked(std::unique_ptr<Process> process);
    bool hasProcessNamedLocked(const std::string &name) const;
    void assignReadyLocked();
    bool canDispatchLocked() const;

    /// One CPU tick every 1 ms (~1000 ticks/second).
    static constexpr std::chrono::milliseconds kCycleInterval{1};

    std::atomic<std::uint64_t> cycles_{0};
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> coreCount_{0};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::mutex lifecycleMutex_;

    std::uint32_t delayPerExec_ = 0;
    SchedulerType schedulerType_ = SchedulerType::FCFS;
    std::uint32_t quantumCycles_ = 0;
    bool generationEnabled_ = false;
    Config generationConfig_;
    std::uint64_t generationLastCycle_ = 0;
    std::uint64_t generationAccumulator_ = 0;
    std::uint64_t nextGeneratedNumber_ = 1;
    std::vector<CoreState> cores_;
    std::deque<std::size_t> ready_;
    std::vector<SleepEntry> sleeping_;
    std::vector<std::unique_ptr<Process>> processes_;

    std::thread clockThread_;
    std::thread dispatchThread_;
    std::vector<std::thread> workerThreads_;
};
