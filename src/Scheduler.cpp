#include "Scheduler.h"

#include <algorithm>
#include <limits>
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
    startLocked(config);
}

void Scheduler::startLocked(const Config &config)
{
    if (running_.load(std::memory_order_acquire))
    {
        return;
    }
    if (config.scheduler == SchedulerType::RR && config.quantumCycles == 0)
    {
        throw std::invalid_argument("Round-robin requires quantum-cycles > 0");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        delayPerExec_ = config.delayPerExec;
        schedulerType_ = config.scheduler;
        quantumCycles_ = config.quantumCycles;
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

    {
        std::lock_guard<std::mutex> lock(mutex_);
        generationEnabled_ = false;
        generationAccumulator_ = 0;
        running_.store(false, std::memory_order_release);
    }
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

bool Scheduler::startGeneration(const Config &config)
{
    std::lock_guard<std::mutex> lifecycle(lifecycleMutex_);
    if (config.batchProcessFreq == 0 || config.minIns == 0 || config.minIns > config.maxIns)
    {
        throw std::invalid_argument("Generation requires batch-process-freq > 0 and 1 <= min-ins <= max-ins");
    }
    startLocked(config);

    std::lock_guard<std::mutex> lock(mutex_);
    if (generationEnabled_)
    {
        return false;
    }
    generationConfig_ = config;
    generationLastCycle_ = cycles_.load(std::memory_order_relaxed);
    generationAccumulator_ = 0;
    generationEnabled_ = true;
    cv_.notify_all();
    return true;
}

bool Scheduler::stopGeneration()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const bool wasEnabled = generationEnabled_;
    generationEnabled_ = false;
    generationAccumulator_ = 0;
    return wasEnabled;
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
    enqueueProcessLocked(std::move(process));
    cv_.notify_all();
}

void Scheduler::enqueueProcessLocked(std::unique_ptr<Process> process)
{
    const std::uint64_t nextId = static_cast<std::uint64_t>(process->id()) + 1;
    const std::size_t index = processes_.size();
    processes_.push_back(std::move(process));
    ready_.push_back(index);
    nextGeneratedNumber_ = std::max(nextGeneratedNumber_, nextId);
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
        view.startedAt = process->startedAt();
        views.push_back(std::move(view));
    }
    for (std::size_t core = 0; core < cores_.size(); ++core)
    {
        if (cores_[core].busy)
        {
            views[cores_[core].processIndex].core = core;
        }
    }
    return views;
}

void Scheduler::tickLoop()
{
    while (running_.load(std::memory_order_acquire))
    {
        cycles_.fetch_add(1, std::memory_order_relaxed);
        cv_.notify_all();
        std::this_thread::sleep_for(kCycleInterval);
    }
}

void Scheduler::dispatchLoop()
{
    std::unique_lock<std::mutex> lock(mutex_);

    while (running_.load(std::memory_order_acquire))
    {
        const std::uint64_t previousCycle = cycles_.load(std::memory_order_relaxed);
        // Block until there is dispatchable work; the timeout still drives
        // sleeper reaping when no process is ready.
        cv_.wait_for(lock, kCycleInterval, [this, previousCycle] {
            return !running_.load(std::memory_order_acquire) || canDispatchLocked() ||
                   ((schedulerType_ == SchedulerType::RR || generationEnabled_) &&
                    cycles_.load(std::memory_order_relaxed) != previousCycle);
        });

        if (!running_.load(std::memory_order_acquire))
        {
            break;
        }

        reapSleepersLocked();
        generateProcessesLocked();
        switch (schedulerType_)
        {
        case SchedulerType::FCFS:
            break;
        case SchedulerType::RR:
            for (CoreState &core : cores_)
            {
                if (core.busy && core.quantumEndCycle != 0 &&
                    cycles_.load(std::memory_order_relaxed) >= core.quantumEndCycle)
                {
                    processes_[core.processIndex]->markReady();
                    ready_.push_back(core.processIndex);
                    core = CoreState{};
                }
            }
            break;
        }
        assignReadyLocked();
        cv_.notify_all();
    }
}

void Scheduler::workerLoop(std::size_t core)
{
    while (true)
    {
        StepOutcome outcome;
        std::uint64_t quantumEndCycle = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this, core] {
                return !running_.load(std::memory_order_acquire) ||
                       (cores_[core].busy &&
                        (schedulerType_ == SchedulerType::FCFS ||
                         cores_[core].quantumEndCycle == 0 ||
                         cycles_.load(std::memory_order_relaxed) < cores_[core].quantumEndCycle));
            });
            if (!running_.load(std::memory_order_acquire))
            {
                break;
            }
            const std::size_t index = cores_[core].processIndex;
            Process *process = processes_[index].get();
            if (schedulerType_ == SchedulerType::RR && cores_[core].quantumEndCycle == 0)
            {
                cores_[core].quantumEndCycle = cycles_.load(std::memory_order_relaxed) + quantumCycles_;
            }
            quantumEndCycle = cores_[core].quantumEndCycle;
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
        if (outcome == StepOutcome::EXECUTED)
        {
            delayForExec(quantumEndCycle); // busy-wait outside the lock
        }
    }
}

void Scheduler::delayForExec(std::uint64_t quantumEndCycle)
{
    // Pace consecutive RR instructions by CPU ticks even when delay is zero.
    const std::uint32_t delay = schedulerType_ == SchedulerType::RR
                                    ? std::max(std::uint32_t{1}, delayPerExec_)
                                    : delayPerExec_;
    if (delay == 0)
    {
        return;
    }

    const std::uint64_t start = cycles_.load(std::memory_order_relaxed);
    while (running_.load(std::memory_order_acquire) &&
           cycles_.load(std::memory_order_relaxed) - start < delay &&
           (schedulerType_ == SchedulerType::FCFS ||
            cycles_.load(std::memory_order_relaxed) < quantumEndCycle))
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

void Scheduler::generateProcessesLocked()
{
    if (!generationEnabled_)
    {
        return;
    }
    const std::uint64_t now = cycles_.load(std::memory_order_relaxed);
    generationAccumulator_ += now - generationLastCycle_;
    generationLastCycle_ = now;
    while (generationAccumulator_ >= generationConfig_.batchProcessFreq)
    {
        if (nextGeneratedNumber_ > std::numeric_limits<std::uint32_t>::max())
        {
            generationEnabled_ = false;
            return;
        }
        const std::string name = (nextGeneratedNumber_ < 10 ? "p0" : "p") +
                                 std::to_string(nextGeneratedNumber_);
        if (std::any_of(processes_.begin(), processes_.end(), [&](const auto &process) {
                return process->name() == name;
            }))
        {
            ++nextGeneratedNumber_;
            continue;
        }
        const auto id = static_cast<std::uint32_t>(nextGeneratedNumber_);
        enqueueProcessLocked(std::make_unique<Process>(
            id, name, generateProgram(generationConfig_, name, id)));
        generationAccumulator_ -= generationConfig_.batchProcessFreq;
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
