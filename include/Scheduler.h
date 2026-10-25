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
 * @brief Non-preemptive FCFS scheduler: one clock thread, one dispatch thread,
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
 *  - lifecycleMutex_ serializes start()/stop() and the thread handles only.
 *
 * Non-preemptive: a process runs to completion unless it sleeps. Round-robin
 * and quantum-cycles are not handled yet.
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

    bool isRunning() const noexcept;

    /// Completed CPU ticks; monotonic while running, frozen after stop().
    std::uint64_t cycleCount() const noexcept;

    /// Worker thread count from the last start(); 0 before start and after stop.
    std::size_t coreCount() const noexcept;

    /// Adds a process to the FCFS ready queue. Safe while running.
    void submitProcess(std::unique_ptr<Process> process);

    /// Immutable copy of every process, in submission order.
    std::vector<ProcessView> snapshot() const;

private:
    struct CoreState
    {
        bool busy = false;
        std::size_t processIndex = 0;
    };

    struct SleepEntry
    {
        std::size_t processIndex = 0;
        std::uint64_t wakeCycle = 0;
    };

    void tickLoop();
    void dispatchLoop();
    void workerLoop(std::size_t core);
    void delayForExec();

    void reapSleepersLocked();
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
    std::vector<CoreState> cores_;
    std::deque<std::size_t> ready_;
    std::vector<SleepEntry> sleeping_;
    std::vector<std::unique_ptr<Process>> processes_;

    std::thread clockThread_;
    std::thread dispatchThread_;
    std::vector<std::thread> workerThreads_;
};
