#include "kernel/Scheduler.h"
#include "shell/ProcessReport.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
std::vector<ProcessView> waitFor(Scheduler &scheduler, Predicate ready)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do
    {
        auto views = scheduler.snapshot();
        if (ready(views))
        {
            return views;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Timed out waiting for generation or workload completion");
}

void waitCycles(Scheduler &scheduler, std::uint64_t count)
{
    const auto target = scheduler.cycleCount() + count;
    waitFor(scheduler, [&](const auto &) { return scheduler.cycleCount() >= target; });
}

Config generationConfig()
{
    Config config;
    config.numCpu = 2;
    config.quantumCycles = 1;
    config.batchProcessFreq = 4;
    config.minIns = 1;
    config.maxIns = 3;
    return config;
}

void checkNamesAndBounds(const Config &config, const std::vector<ProcessView> &views)
{
    std::set<std::uint32_t> ids;
    std::set<std::string> names;
    for (std::size_t i = 0; i < views.size(); ++i)
    {
        const auto &view = views[i];
        const std::string expected = (i < 9 ? "p0" : "p") + std::to_string(i + 1);
        check(view.name == expected, "Generated names are not padded and sequential");
        check(ids.insert(view.id).second && names.insert(view.name).second, "Generated a duplicate identity");
        check(view.linesOfCode >= config.minIns && view.linesOfCode <= config.maxIns,
              "Generated program is outside the configured bounds");
        check(view.linesOfCode == generateProgram(config, view.name, view.id).instructionCount(),
              "Generated program did not use the instruction generator");
    }
}

void checkOpening()
{
    Config config;
    for (const std::uint64_t size : {1, 2, 100})
    {
        config.minIns = config.maxIns = size;
        const auto program = generateProgram(config, "opening", 1);
        check(program.instructionCount() == size, "I/O opening exceeded instruction bounds");
        const auto *greeting = std::get_if<PrintInstruction>(&program.instructions.front().payload);
        check(greeting && greeting->text == "Hello world from opening!", "Generated program lacks its greeting");
        if (size > 1)
        {
            const auto *sleep = std::get_if<SleepInstruction>(&program.instructions[1].payload);
            check(sleep && sleep->ticks == 255, "Generated program lacks its 255-tick I/O opening");
        }
    }
}

void checkDemo()
{
    Config config;
    config.numCpu = 8;
    config.scheduler = SchedulerType::RR;
    config.quantumCycles = 1;
    config.batchProcessFreq = 1;
    config.minIns = config.maxIns = 100;
    config.delayPerExec = 0;
    Scheduler scheduler;
    scheduler.startGeneration(config);
    std::this_thread::sleep_for(std::chrono::seconds(10));
    check(scheduler.stopGeneration(), "Demo generation was not enabled");
    const auto count = scheduler.snapshot().size();
    check(count > 0, "Demo generated no processes");
    std::this_thread::sleep_for(std::chrono::seconds(30));
    const auto completed = scheduler.snapshot();
    check(completed.size() == count, "Demo generated processes after scheduler-stop");
    check(std::all_of(completed.begin(), completed.end(), [](const auto &view) {
        return view.state == ProcessState::FINISHED && !view.core && view.linesOfCode == 100;
    }), "Demo workload did not finish within 30 seconds of scheduler-stop");
    const auto report = buildProcessReport({scheduler.coreCount(), completed});
    check(report.find("CPU utilization: 0%\nCores used: 0\nCores available: 8\n") == 0,
          "Demo report did not show idle CPUs");
    check(scheduler.isRunning(), "Demo stopped CPU execution");
    std::cout << "Demo passed: " << count << " processes finished; 0% CPU, 8 cores available.\n";
}

void checkIdleDuringGeneration(std::uint64_t frequency, std::uint64_t delay)
{
    Config config;
    config.numCpu = 1;
    config.scheduler = SchedulerType::RR;
    config.quantumCycles = 10;
    config.batchProcessFreq = frequency;
    config.minIns = 30;
    config.maxIns = 45;
    config.delayPerExec = delay;
    Scheduler scheduler;
    scheduler.startGeneration(config);
    bool sawBusy = false;
    std::uint64_t idleStart = 0;
    waitFor(scheduler, [&](const auto &views) {
        sawBusy = sawBusy || std::any_of(views.begin(), views.end(), [](const auto &view) {
            return view.core && view.currentLine > 0;
        });
        const bool sleeping = std::any_of(views.begin(), views.end(), [](const auto &view) {
            return view.state == ProcessState::WAITING && view.currentLine >= 2 && !view.logs.empty();
        });
        const bool runnable = std::any_of(views.begin(), views.end(), [](const auto &view) {
            return view.state == ProcessState::READY || view.state == ProcessState::RUNNING;
        });
        if (!sawBusy || !sleeping || runnable)
        {
            idleStart = 0;
            return false;
        }
        const auto cycle = scheduler.cycleCount();
        if (idleStart == 0)
        {
            idleStart = cycle;
        }
        check(buildProcessReport({1, views}).find("CPU utilization: 0%\n") == 0,
              "Case 7 genuine idle interval did not report 0%");
        return cycle >= idleStart + 2;
    });
    check(scheduler.stopGeneration(), "Case 7 stopped generating before its idle interval");
    std::cout << "Case 7 passed: batch=" << frequency << ", delay=" << delay
              << "; observed busy execution followed by genuine idle.\n";
}

void checkSaturation()
{
    Config config;
    config.numCpu = 16;
    config.scheduler = SchedulerType::RR;
    config.quantumCycles = 5;
    config.batchProcessFreq = 1;
    config.minIns = config.maxIns = 5000;
    config.delayPerExec = 3;
    Scheduler scheduler;
    scheduler.startGeneration(config);
    const auto started = std::chrono::steady_clock::now();
    for (int sample = 0; sample < 5; ++sample)
    {
        std::this_thread::sleep_until(started + std::chrono::seconds(10 + 5 * sample));
        const auto views = scheduler.snapshot();
        check(buildProcessReport({16, views}).find(
                  "CPU utilization: 100%\nCores used: 16\nCores available: 0\n") == 0,
              "Case 8 did not keep all 16 cores occupied");
        check(std::count_if(views.begin(), views.end(), [](const auto &view) {
            return view.state == ProcessState::FINISHED;
        }) < 10, "Case 8 finished too many processes");
    }
    scheduler.stopGeneration();
    const auto latest = scheduler.snapshot();
    check(buildProcessReport({16, latest}).find("CPU utilization: 100%\n") == 0,
          "Case 8 generation stop interrupted CPU execution");
    std::cout << "Case 8 passed: five 100% samples; " << latest.size()
              << " processes retained after generation stopped.\n";
}
} // namespace

int main()
{
    try
    {
        checkOpening();
        Config config = generationConfig();
        Scheduler cadence;
        check(!cadence.stopGeneration() && !cadence.isRunning(), "Stopping idle generation started CPU threads");
        cadence.start(config);
        waitCycles(cadence, 5);
        check(cadence.snapshot().empty(), "CPU startup must not enable generation");
        const auto earliestStart = cadence.cycleCount();
        check(cadence.startGeneration(config), "Generation did not start");
        const auto latestStart = cadence.cycleCount();
        check(!cadence.startGeneration(config), "Starting twice enabled a second generator");
        const auto early = cadence.snapshot();
        if (cadence.cycleCount() - earliestStart < config.batchProcessFreq)
        {
            check(early.empty(), "First process appeared before a full batch interval");
        }
        waitFor(cadence, [](const auto &views) { return views.size() >= 4; });
        check(cadence.stopGeneration(), "Generation did not stop");
        const auto stoppedCycle = cadence.cycleCount();
        const auto stopped = cadence.snapshot();
        check(stopped.size() <= (stoppedCycle - earliestStart) / config.batchProcessFreq,
              "Generation exceeded one process per batch interval");
        check(stopped.size() + 2 >= (stoppedCycle - latestStart) / config.batchProcessFreq,
              "Generation fell behind the configured cadence");
        waitCycles(cadence, 2 * config.batchProcessFreq);
        check(cadence.snapshot().size() == stopped.size(), "Creation continued after stopGeneration returned");
        check(!cadence.stopGeneration() && cadence.isRunning(), "Repeated stop shut down CPU execution");
        cadence.stop();

        config.batchProcessFreq = 1;
        Scheduler names;
        check(names.startGeneration(config), "Name test failed to start");
        waitFor(names, [](const auto &views) { return views.size() >= 100; });
        names.stopGeneration();
        checkNamesAndBounds(config, names.snapshot());
        const auto beforeRestart = names.snapshot().size();
        check(names.startGeneration(config), "Generation did not restart");
        waitFor(names, [&](const auto &views) { return views.size() >= beforeRestart + 2; });
        names.stopGeneration();
        checkNamesAndBounds(config, names.snapshot());
        names.stop();
        check(!names.stopGeneration(), "Full shutdown left generation enabled");
        const auto beforeCpuRestart = names.snapshot().size();
        names.start(config);
        waitCycles(names, 3);
        check(names.snapshot().size() == beforeCpuRestart, "CPU restart unexpectedly resumed generation");
        names.stop();

        for (const SchedulerType type : {SchedulerType::FCFS, SchedulerType::RR})
        {
            config.scheduler = type;
            config.numCpu = 1;
            config.minIns = config.maxIns = 1;
            config.delayPerExec = 2;
            Scheduler drain;
            Program program;
            program.instructions.assign(16, Instruction{AddInstruction{
                "x", Operand::variable("x"), Operand::literal(1)}});
            program.instructions.push_back(Instruction{PrintInstruction{"x=", std::string{"x"}}});
            drain.submitProcess(std::make_unique<Process>(1000, "manual", program));
            drain.startGeneration(config);
            waitFor(drain, [](const auto &views) { return views.size() >= 3; });
            drain.stopGeneration();
            const auto pending = drain.snapshot();
            check(pending.front().state != ProcessState::FINISHED,
                  "Drain scenario had no active work at generation stop");
            const auto completed = waitFor(drain, [&](const auto &views) {
                check(views.size() == pending.size(), "New processes appeared while draining");
                return std::all_of(views.begin(), views.end(), [](const auto &view) {
                    return view.state == ProcessState::FINISHED;
                });
            });
            check(completed.front().logs == std::vector<std::string>{"x=16"},
                  "Generation stop interrupted an existing process");
            check(completed.front().variables.empty(), "Finished process retained its variables");
            check(drain.isRunning(), "Draining stopped the CPU threads");
        }

        config = generationConfig();
        config.batchProcessFreq = 1;
        Scheduler collision;
        Program one;
        one.instructions.push_back(Instruction{DeclareInstruction{"x", 1}});
        collision.submitProcess(std::make_unique<Process>(1, "p02", one));
        collision.startGeneration(config);
        waitFor(collision, [](const auto &views) { return views.size() >= 2; });
        collision.stopGeneration();
        const auto collisionViews = collision.snapshot();
        check(collisionViews[1].id == 3 && collisionViews[1].name == "p03",
              "Generation reused a submitted ID or name");
        collision.stop();

        for (int invalid = 0; invalid < 3; ++invalid)
        {
            auto invalidConfig = config;
            if (invalid == 0) invalidConfig.batchProcessFreq = 0;
            if (invalid == 1) invalidConfig.minIns = 0;
            if (invalid == 2) invalidConfig.minIns = invalidConfig.maxIns + 1;
            bool rejected = false;
            try
            {
                collision.startGeneration(invalidConfig);
            }
            catch (const std::invalid_argument &)
            {
                rejected = true;
            }
            check(rejected && !collision.isRunning(), "Invalid generation config started CPU threads");
        }

        // The spec's 2^32 ceiling is valid config, but such a program cannot fit in
        // memory; generation must refuse it instead of allocating.
        {
            auto huge = config;
            huge.minIns = huge.maxIns = std::uint64_t{1} << 32;
            bool refused = false;
            try
            {
                generateProgram(huge, "huge", 1);
            }
            catch (const std::length_error &)
            {
                refused = true;
            }
            check(refused, "An oversized program was not refused");
        }
        checkDemo();
        checkIdleDuringGeneration(20, 3);
        checkIdleDuringGeneration(10, 1);
        checkSaturation();
        std::cout << "Scheduler generation checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
