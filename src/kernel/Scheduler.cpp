#include "kernel/Scheduler.h"
#include "kernel/TickTimer.h"

#include <algorithm>
#include <limits>
#include <random>
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
        nextCoreExecutionCycles_.assign(cores_.size(), 0);
        if (!memory_.isConfigured())
        {
            configureMemoryLocked(config);
        }
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
        releaseIfFinishedLocked(core.processIndex);
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
        releaseIfFinishedLocked(entry.processIndex);
    }

    sleeping_.clear();
    cores_.clear();
    nextCoreExecutionCycles_.clear();
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

bool Scheduler::takeGenerationFailure()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(generationFailed_, false);
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
    ensureMemoryConfiguredLocked();
    const std::uint64_t memoryBytes = rollMemoryBytesLocked(process->id());
    enqueueProcessLocked(std::move(process), memoryBytes);
    cv_.notify_all();
}

std::optional<std::uint32_t> Scheduler::createProcess(const std::string &name,
                                                      std::uint64_t memoryBytes,
                                                      const Config &config)
{
    if (!isValidMemorySize(memoryBytes))
    {
        throw std::invalid_argument("Process memory must be a power of 2 in [64, 65536] bytes");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (hasProcessNamedLocked(name))
    {
        return std::nullopt;
    }
    if (nextGeneratedNumber_ > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::overflow_error("Process ids are exhausted");
    }

    // Share the generator's id counter so manual and generated ids never collide.
    const auto id = static_cast<std::uint32_t>(nextGeneratedNumber_);
    ensureMemoryConfiguredLocked();
    enqueueProcessLocked(std::make_unique<Process>(id, name, generateProgram(config, name, id)), memoryBytes);
    cv_.notify_all();
    return id;
}

bool Scheduler::hasProcessNamedLocked(const std::string &name) const
{
    return processNames_.count(name) != 0;
}

void Scheduler::enqueueProcessLocked(std::unique_ptr<Process> process, std::uint64_t memoryBytes)
{
    const std::uint64_t nextId = static_cast<std::uint64_t>(process->id()) + 1;
    const std::size_t index = processes_.size();
    const std::string name = process->name();
    bool nameInserted = false;
    try
    {
        nameInserted = processNames_.insert(name).second;
        nextProcessExecutionCycles_.resize(index + 1, 0);
        const std::uint32_t processId = process->id();
        processes_.push_back(std::move(process));
        // Keyed by index (submitted IDs may repeat); records show the real ID.
        memory_.allocate(index, memoryBytes, processId);
        ready_.push_back(index);
    }
    catch (...)
    {
        memory_.release(index);
        processes_.resize(index);
        nextProcessExecutionCycles_.resize(index);
        if (nameInserted)
        {
            processNames_.erase(name);
        }
        throw;
    }
    nextGeneratedNumber_ = std::max(nextGeneratedNumber_, nextId);
}

std::vector<ProcessView> Scheduler::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshotLocked();
}

SystemSnapshot Scheduler::systemSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    SystemSnapshot system;
    system.totalCores = cores_.size();
    system.processes = snapshotLocked();
    system.memory = memory_.stats();
    system.activeTicks = activeTicks_;
    system.idleTicks = idleTicks_;
    return system;
}

void Scheduler::setBackingStorePath(std::string path)
{
    std::lock_guard<std::mutex> lock(mutex_);
    backingStorePath_ = std::move(path);
}

std::vector<ProcessView> Scheduler::snapshotLocked() const
{
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
        view.logStamps = process->logStamps();
        view.variables = process->variables();
        view.startedAt = process->startedAt();
        view.memoryBytes = memory_.allocatedBytes(views.size());
        view.residentBytes = memory_.residentBytes(views.size());
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

std::optional<ProcessView> Scheduler::findActiveProcessByName(const std::string &name) const
{
    const std::vector<ProcessView> processes = snapshot();
    {
        for (const ProcessView &process : processes)
        {
            if (process.name == name && process.state != ProcessState::FINISHED)
            {
                return process;
            }
        }
    }
    return std::nullopt;
}

void Scheduler::tickLoop()
{
    // A plain sleep_for(1ms) is ~15.6 ms on Windows; see TickTimer.
    TickTimer timer(kCycleInterval);
    while (running_.load(std::memory_order_acquire))
    {
        {
            // Charge the tick to every core: busy cores are active, the rest idle.
            std::lock_guard<std::mutex> lock(mutex_);
            const auto busy = static_cast<std::uint64_t>(
                std::count_if(cores_.begin(), cores_.end(), [](const CoreState &core) { return core.busy; }));
            activeTicks_ += busy;
            idleTicks_ += cores_.size() - busy;
        }
        cycles_.fetch_add(1, std::memory_order_relaxed);
        cv_.notify_all();
        timer.wait();
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
        try
        {
            generateProcessesLocked();
        }
        catch (const std::exception &)
        {
            generationEnabled_ = false;
            generationAccumulator_ = 0;
            generationFailed_ = true;
        }
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

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this, core] {
                return !running_.load(std::memory_order_acquire) ||
                       (cores_[core].busy &&
                        cycles_.load(std::memory_order_relaxed) >= nextCoreExecutionCycles_[core] &&
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
            const auto nextExecutionCycle = nextProcessExecutionCycles_[index];
            if (cycles_.load(std::memory_order_relaxed) < nextExecutionCycle)
            {
                const auto quantumEndCycle = cores_[core].quantumEndCycle;
                lock.unlock();
                delayForExec(nextExecutionCycle, quantumEndCycle);
                continue;
            }
            touchNextPageLocked(index);
            outcome = process->step(core);
            const auto executedCycle = cycles_.load(std::memory_order_relaxed);
            nextCoreExecutionCycles_[core] = executedCycle + (delayPerExec_ == 0 ? 0 : 1);
            if (outcome == StepOutcome::EXECUTED)
            {
                // Zero delay runs continuously; positive delays wait whole cycles.
                nextProcessExecutionCycles_[index] = executedCycle +
                    (delayPerExec_ == 0 ? 0 : 1 + delayPerExec_);
            }
            if (outcome == StepOutcome::SLEEPING)
            {
                sleeping_.push_back(
                    SleepEntry{index,
                               cycles_.load(std::memory_order_relaxed) +
                                   process->sleepTicksRemaining()});
            }
            releaseIfFinishedLocked(index);
            if (outcome == StepOutcome::SLEEPING || outcome == StepOutcome::FINISHED)
            {
                cores_[core] = CoreState{};
                // Fill released cores before reports can observe the updated state.
                assignReadyLocked();
            }
        }
        cv_.notify_all();
    }
}

void Scheduler::delayForExec(std::uint64_t nextExecutionCycle, std::uint64_t quantumEndCycle)
{
    while (running_.load(std::memory_order_acquire) &&
           cycles_.load(std::memory_order_relaxed) < nextExecutionCycle &&
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
        releaseIfFinishedLocked(it->processIndex);
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
    // batch-process-freq is at most 2^32, so the product fits in 64 bits.
    const std::uint64_t interval = generationConfig_.batchProcessFreq * kTicksPerBatchCycle;
    for (std::size_t created = 0; created < kMaxGeneratedPerPass && generationAccumulator_ >= interval;)
    {
        if (nextGeneratedNumber_ > std::numeric_limits<std::uint32_t>::max())
        {
            generationEnabled_ = false;
            return;
        }
        const std::string name = (nextGeneratedNumber_ < 10 ? "p0" : "p") +
                                 std::to_string(nextGeneratedNumber_);
        if (hasProcessNamedLocked(name))
        {
            ++nextGeneratedNumber_;
            continue;
        }
        const auto id = static_cast<std::uint32_t>(nextGeneratedNumber_);
        ensureMemoryConfiguredLocked();
        const std::uint64_t memoryBytes = rollMemoryBytesLocked(id);
        enqueueProcessLocked(std::make_unique<Process>(
                                 id, name, generateProgram(generationConfig_, name, id)),
                             memoryBytes);
        generationAccumulator_ -= interval;
        ++created;
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

void Scheduler::configureMemoryLocked(const Config &config)
{
    memory_.configure(config, backingStorePath_);
    memoryConfig_ = config;
}

void Scheduler::ensureMemoryConfiguredLocked()
{
    if (!memory_.isConfigured())
    {
        configureMemoryLocked(memoryConfig_);
    }
}

void Scheduler::touchNextPageLocked(std::size_t index)
{
    // Instructions are spread evenly over the address space, so execution
    // walks the pages in order and FOR loops revisit earlier ones.
    const Process &process = *processes_[index];
    const std::size_t pages = memory_.pageCount(index);
    const std::size_t lines = process.linesOfCode();
    if (pages == 0 || lines == 0)
    {
        return;
    }
    // Page faults are served within the same tick, so paging never changes
    // instruction timing.
    memory_.access(index, process.nextInstructionIndex() * pages / lines);
}

void Scheduler::releaseIfFinishedLocked(std::size_t index)
{
    if (processes_[index]->isFinished())
    {
        memory_.release(index);
    }
}

std::uint64_t Scheduler::rollMemoryBytesLocked(std::uint32_t id) const
{
    auto exponentOf = [](std::uint64_t value) {
        int exponent = 0;
        while (value > 1)
        {
            value >>= 1;
            ++exponent;
        }
        return exponent;
    };
    const std::uint64_t minBytes = std::max<std::uint64_t>(memoryConfig_.minMemPerProc, 1);
    const std::uint64_t maxBytes = std::max(memoryConfig_.maxMemPerProc, minBytes);
    std::mt19937 rng(id ^ 0x9E3779B9u);
    std::uniform_int_distribution<int> pick(exponentOf(minBytes), exponentOf(maxBytes));
    return std::uint64_t{1} << pick(rng);
}
