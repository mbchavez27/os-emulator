#pragma once

#include <chrono>

/**
 * @brief Paces the CPU clock: each wait() blocks for about one tick interval.
 *
 * On Windows, sleep_for(1ms) rounds up to the ~15.6 ms system timer tick, and
 * timeBeginPeriod(1) is not honored for every process (MinGW's sleep, and
 * Windows 11 power throttling of background processes), which made the clock
 * about 15x slower than on Linux/macOS. There the timer uses a high-resolution
 * waitable timer, falling back to timeBeginPeriod + sleep_for on Windows
 * versions without one. Elsewhere it is a plain sleep_for.
 *
 * One timer belongs to one thread; it is not thread-safe.
 */
class TickTimer
{
public:
    /**
     * @param interval the pause each wait() aims for
     */
    explicit TickTimer(std::chrono::microseconds interval);
    ~TickTimer();

    TickTimer(const TickTimer &) = delete;
    TickTimer &operator=(const TickTimer &) = delete;

    /** @brief Blocks for about one interval. */
    void wait();

private:
    std::chrono::microseconds interval_;
    [[maybe_unused]] void *handle_ = nullptr; /**< Windows waitable timer; null when sleeping instead. */
};
