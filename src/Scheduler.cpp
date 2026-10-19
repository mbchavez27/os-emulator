#include "Scheduler.h"

#include <thread>
#include <utility>

Scheduler::~Scheduler()
{
    stop();
}

void Scheduler::start()
{
    std::lock_guard<std::mutex> lock(lifecycleMutex_);
    if (clockThread_.joinable())
    {
        return; // already running
    }

    running_.store(true, std::memory_order_release);
    clockThread_ = std::thread(&Scheduler::tickLoop, this);
}

void Scheduler::stop()
{
    std::thread thread;
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex_);
        if (!clockThread_.joinable())
        {
            return; // never started, or already stopped
        }
        running_.store(false, std::memory_order_release);
        thread = std::move(clockThread_); // join outside the lock
    }
    thread.join();
}

bool Scheduler::isRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

std::uint64_t Scheduler::cycleCount() const noexcept
{
    return cycles_.load(std::memory_order_relaxed);
}

void Scheduler::tickLoop()
{
    while (running_.load(std::memory_order_acquire))
    {
        cycles_.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(kCycleInterval);
    }
}
