#include "Scheduler.h"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

Scheduler::~Scheduler()
{
    stop();
}

void Scheduler::start(const Config &config)
{
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    if (running_.load(std::memory_order_acquire))
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        delayPerExec_ = config.delayPerExec;
        cores_.assign(static_cast<std::size_t>(config.numCpu), CoreState{});
    }

    coreCount_.store(cores_.size(), std::memory_order_relaxed);
    running_.store(true, std::memory_order_release);

    clockThread_ = std::thread(&Scheduler::tickLoop, this);
    dispatchThread_ = std::thread(&Scheduler::dispatchLoop, this);

    workerThreads_.reserve(cores_.size());
    for (std::size_t core = 0; core < cores_.size(); ++core)
    {
        workerThreads_.emplace_back(&Scheduler::workerLoop, this, core);
    }
}

void Scheduler::stop()
{
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);

    const bool hadThreads = clockThread_.joinable() || dispatchThread_.joinable() ||
                            !workerThreads_.empty();

    running_.store(false, std::memory_order_release);
    cv_.notify_all();

    if (clockThread_.joinable())
    {
        clockThread_.join();
    }
    if (dispatchThread_.joinable())
    {
        dispatchThread_.join();
    }
    for (std::thread &worker : workerThreads_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    workerThreads_.clear();

    if (!hadThreads)
    {
        coreCount_.store(0, std::memory_order_relaxed);
        return;
    }

    // Leave the process list consistent: interrupted processes go back to the
    // ready queue, and sleepers are drained back to READY.
    std::lock_guard<std::mutex> lock(mutex_);
    for (CoreState &core : cores_)
    {
        if (!core.busy)
        {
            continue;
        }
        Process *process = processes_[core.processIndex].get();
        if (process->state() == ProcessState::RUNNING)
        {
            process->markReady();
        }
        if (!process->isFinished())
        {
            ready_.push_back(core.processIndex);
        }
        core = CoreState{};
    }
    for (const SleepEntry &entry : sleeping_)
    {
        Process *process = processes_[entry.processIndex].get();
        while (process->isSleeping() && process->sleepTicksRemaining() > 0)
        {
            process->tickSleep();
        }
        if (!process->isFinished())
        {
            ready_.push_back(entry.processIndex);
        }
    }

    sleeping_.clear();
    cores_.clear();
    coreCount_.store(0, std::memory_order_relaxed);
}

bool Scheduler::isRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

std::uint64_t Scheduler::cycleCount() const noexcept
{
    return cycles_.load(std::memory_order_relaxed);
}

std::size_t Scheduler::coreCount() const noexcept
{
    return coreCount_.load(std::memory_order_relaxed);
}

void Scheduler::submitProcess(std::unique_ptr<Process> process)
{
    if (!process)
    {
        throw std::invalid_argument("submitProcess requires a non-null process");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t index = processes_.size();
    processes_.push_back(std::move(process));
    ready_.push_back(index);
    cv_.notify_all();
}

std::vector<ProcessView> Scheduler::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ProcessView> views;
    views.reserve(processes_.size());
    for (const std::unique_ptr<Process> &process : processes_)
    {
        ProcessView view;
        view.id = process->id();
        view.name = process->name();
        view.state = process->state();
        view.currentLine = process->currentLine();
        view.linesOfCode = process->linesOfCode();
        view.sleepTicksRemaining = process->sleepTicksRemaining();
        view.logs = process->logs();
        view.variables = process->variables();
        views.push_back(std::move(view));
    }
    return views;
}

void Scheduler::tickLoop()
{
    while (running_.load(std::memory_order_acquire))
    {
        cycles_.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(kCycleInterval);
    }
}

void Scheduler::dispatchLoop()
{
    std::unique_lock<std::mutex> lock(mutex_);

    while (running_.load(std::memory_order_acquire))
    {
        // Block until there is dispatchable work; the timeout still drives
        // sleeper reaping when no process is ready.
        cv_.wait_for(lock, kCycleInterval, [this] {
            return !running_.load(std::memory_order_acquire) || canDispatchLocked();
        });

        if (!running_.load(std::memory_order_acquire))
        {
            break;
        }

        reapSleepersLocked();
        assignReadyLocked();
        cv_.notify_all();
    }
}

void Scheduler::workerLoop(std::size_t core)
{
    while (true)
    {
        Process *process = nullptr;
        std::size_t index = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this, core] {
                return !running_.load(std::memory_order_acquire) ||
                       cores_[core].busy;
            });
            if (!running_.load(std::memory_order_acquire))
            {
                break;
            }
            index = cores_[core].processIndex;
            process = processes_[index].get(); // capture under the lock
        }

        while (running_.load(std::memory_order_acquire))
        {
            StepOutcome outcome = StepOutcome::EXECUTED;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                outcome = process->step();
                if (outcome == StepOutcome::SLEEPING)
                {
                    sleeping_.push_back(
                        SleepEntry{index,
                                   cycles_.load(std::memory_order_relaxed) +
                                       process->sleepTicksRemaining()});
                    cores_[core] = CoreState{};
                }
                else if (outcome == StepOutcome::FINISHED)
                {
                    cores_[core] = CoreState{};
                }
            }

            cv_.notify_all();

            if (outcome != StepOutcome::EXECUTED)
            {
                break;
            }

            delayForExec(); // busy-wait outside the lock
        }
    }
}

void Scheduler::delayForExec()
{
    if (delayPerExec_ == 0)
    {
        return;
    }

    const std::uint64_t start = cycles_.load(std::memory_order_relaxed);
    while (running_.load(std::memory_order_acquire) &&
           cycles_.load(std::memory_order_relaxed) - start < delayPerExec_)
    {
        std::this_thread::yield();
    }
}

void Scheduler::reapSleepersLocked()
{
    const std::uint64_t now = cycles_.load(std::memory_order_relaxed);

    auto it = sleeping_.begin();
    while (it != sleeping_.end())
    {
        Process *process = processes_[it->processIndex].get();
        if (now < it->wakeCycle)
        {
            ++it;
            continue;
        }

        while (process->isSleeping() && process->sleepTicksRemaining() > 0)
        {
            process->tickSleep();
        }
        if (!process->isFinished())
        {
            ready_.push_back(it->processIndex);
        }
        it = sleeping_.erase(it);
    }
}

void Scheduler::assignReadyLocked()
{
    for (std::size_t core = 0; core < cores_.size() && !ready_.empty(); ++core)
    {
        if (cores_[core].busy)
        {
            continue;
        }

        const std::size_t index = ready_.front();
        ready_.pop_front();
        processes_[index]->markRunning();
        cores_[core] = CoreState{true, index};
    }
}

bool Scheduler::canDispatchLocked() const
{
    return !ready_.empty() &&
           std::any_of(cores_.begin(), cores_.end(),
                       [](const CoreState &core) { return !core.busy; });
}
