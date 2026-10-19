#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>

/**
 * @brief Owns the CPU clock thread.
 *
 * Implements the spec tick loop `while (COS is running) { cpuCycles++; }`.
 * No process scheduling yet: start()/stop() only own and join the clock
 * thread, and the destructor guarantees a clean shutdown.
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

    /// Starts the clock thread. No-op when already running.
    void start();

    /// Stops and joins the clock thread. Safe when not running.
    void stop();

    bool isRunning() const noexcept;

    /// Completed CPU ticks; monotonic while running, frozen after stop().
    std::uint64_t cycleCount() const noexcept;

private:
    void tickLoop();

    /// One CPU tick every 1 ms (~1000 ticks/second).
    static constexpr std::chrono::milliseconds kCycleInterval{1};

    std::atomic<std::uint64_t> cycles_{0};
    std::atomic<bool> running_{false};
    std::mutex lifecycleMutex_;
    std::thread clockThread_;
};
