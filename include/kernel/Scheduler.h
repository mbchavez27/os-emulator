#pragma once

#include "config/Config.h"
#include "kernel/EmulatorProcess.h"

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
#include <unordered_set>
#include <vector>

/** @brief Immutable copy of one process's observable state at snapshot time. */
struct ProcessView
{
    std::uint32_t id = 0;
    std::string name;
    ProcessState state = ProcessState::READY;
    std::size_t currentLine = 0;
    std::size_t linesOfCode = 0;
    std::uint8_t sleepTicksRemaining = 0;
    std::vector<std::string> logs;
    std::vector<LogStamp> logStamps; /**< Parallel to logs: execution time and core. */
    std::map<std::string, std::uint16_t> variables;
    std::chrono::system_clock::time_point startedAt;
    std::optional<std::size_t> core; /**< Empty for ready, sleeping, or finished processes. */
};

/**
 * @brief FCFS / round-robin scheduler: one clock thread, one dispatch thread,
 *        and one worker thread per core.
 *
 * <b>Locking strategy</b>
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

    /** @brief Stops and joins every thread. */
    ~Scheduler();

    Scheduler(const Scheduler &) = delete;
    Scheduler &operator=(const Scheduler &) = delete;
    Scheduler(Scheduler &&) = delete;
    Scheduler &operator=(Scheduler &&) = delete;

    /**
     * @brief Starts the clock, dispatch, and worker threads.
     *
     * Does nothing if already running.
     *
     * @param config supplies num-cpu, the algorithm, quantum, and delay
     * @throws std::invalid_argument if round robin is chosen with a zero quantum
     */
    void start(const Config &config);

    /** @brief Stops and joins every thread; safe when not running. */
    void stop();

    /**
     * @brief Turns on dummy process creation, starting the CPU threads if needed.
     *
     * @param config supplies batch-process-freq and the instruction bounds
     * @return false if generation was already on; it is not reset
     * @throws std::invalid_argument if the frequency is 0 or the bounds are invalid
     */
    bool startGeneration(const Config &config);

    /**
     * @brief Turns off process creation synchronously.
     *
     * Queued and running processes keep executing.
     *
     * @return false if generation was already off
     */
    bool stopGeneration();

    /**
     * @brief Reports a background process-creation failure once.
     *
     * @return true the first time after a failure, then false until the next one
     */
    bool takeGenerationFailure();

    /** @return true while the CPU threads run */
    bool isRunning() const noexcept;

    /** @return completed CPU ticks; increasing while running, frozen after stop() */
    std::uint64_t cycleCount() const noexcept;

    /** @return worker threads from the last start(); 0 before start and after stop */
    std::size_t coreCount() const noexcept;

    /**
     * @brief Adds a process to the back of the FIFO ready queue; safe while running.
     *
     * @param process the process to schedule
     * @throws std::invalid_argument if process is null
     */
    void submitProcess(std::unique_ptr<Process> process);

    /**
     * @brief Creates a process with a generated program and queues it.
     *
     * The name check and the insert happen under one lock, so concurrent
     * generation cannot claim the same name. Finished processes keep their names.
     *
     * @param name the process name
     * @param config supplies the instruction bounds
     * @return the new process ID, or std::nullopt if the name is taken
     * @throws std::overflow_error if process IDs are exhausted
     * @throws std::length_error if the program would exceed kMaxProgramInstructions
     */
    std::optional<std::uint32_t> createProcess(const std::string &name, const Config &config);

    /** @return a copy of every process, in submission order */
    std::vector<ProcessView> snapshot() const;

    /**
     * @brief Finds a process that can still be attached to.
     *
     * @param name the process name
     * @return its copied state, or std::nullopt if it does not exist or has finished
     */
    std::optional<ProcessView> findActiveProcessByName(const std::string &name) const;

private:
    /** @brief What one core is running. */
    struct CoreState
    {
        bool busy = false;                 /**< Whether a process holds this core. */
        std::size_t processIndex = 0;      /**< Index into processes_ when busy. */
        std::uint64_t quantumEndCycle = 0; /**< RR slice end; set when the worker begins the slice. */
    };

    /** @brief A sleeping process and the tick it wakes on. */
    struct SleepEntry
    {
        std::size_t processIndex = 0;
        std::uint64_t wakeCycle = 0;
    };

    void tickLoop();
    void startLocked(const Config &config);
    void dispatchLoop();
    void workerLoop(std::size_t core);
    void delayForExec(std::uint64_t nextExecutionCycle, std::uint64_t quantumEndCycle);

    void reapSleepersLocked();
    void generateProcessesLocked();
    void enqueueProcessLocked(std::unique_ptr<Process> process);
    bool hasProcessNamedLocked(const std::string &name) const;
    void assignReadyLocked();
    bool canDispatchLocked() const;

    /**
     * @brief Pause between CPU ticks.
     *
     * Each pass still counts exactly one tick; the pause keeps tick-based
     * settings at a usable speed (about 800 ticks per second on a typical machine).
     */
    static constexpr std::chrono::milliseconds kCycleInterval{1};

    /**
     * @brief Most processes built per dispatch pass.
     *
     * Building a program happens under mutex_, so an unbounded catch-up loop
     * would starve the console whenever generation falls behind the clock.
     * Leftover batches carry over to the next pass.
     */
    static constexpr std::size_t kMaxGeneratedPerPass = 4;

    std::atomic<std::uint64_t> cycles_{0};
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> coreCount_{0};

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::mutex lifecycleMutex_;

    std::uint64_t delayPerExec_ = 0;
    SchedulerType schedulerType_ = SchedulerType::FCFS;
    std::uint64_t quantumCycles_ = 0;
    bool generationEnabled_ = false;
    bool generationFailed_ = false;
    Config generationConfig_;
    std::uint64_t generationLastCycle_ = 0;
    std::uint64_t generationAccumulator_ = 0; /**< Ticks owed to generation not yet spent. */
    std::uint64_t nextGeneratedNumber_ = 1;   /**< Next ID, shared by generated and manual processes. */
    std::vector<CoreState> cores_;
    std::vector<std::uint64_t> nextCoreExecutionCycles_; /**< Survives process reassignment. */
    std::deque<std::size_t> ready_;
    std::vector<SleepEntry> sleeping_;
    std::vector<std::unique_ptr<Process>> processes_;
    std::unordered_set<std::string> processNames_; /**< Names in processes_; O(1) duplicate checks. */
    std::vector<std::uint64_t> nextProcessExecutionCycles_; /**< Survives preemption and stop/resume. */

    std::thread clockThread_;
    std::thread dispatchThread_;
    std::vector<std::thread> workerThreads_;
};
