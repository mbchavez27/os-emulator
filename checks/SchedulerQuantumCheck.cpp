#include "Scheduler.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
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

using Observer = std::function<void(const std::vector<ProcessView> &)>;

std::vector<ProcessView> finish(Scheduler &scheduler, const Observer &observe = {})
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do
    {
        auto views = scheduler.snapshot();
        std::vector<bool> occupied(scheduler.coreCount());
        for (const auto &view : views)
        {
            check(view.core.has_value() == (view.state == ProcessState::RUNNING),
                  "Snapshot state and core assignment disagree");
            if (view.core)
            {
                check(*view.core < occupied.size(), "Invalid core assignment");
                check(!occupied[*view.core], "A core is assigned twice");
                occupied[*view.core] = true;
            }
        }
        if (observe)
        {
            observe(views);
        }
        if (std::all_of(views.begin(), views.end(), [](const auto &view) {
                return view.state == ProcessState::FINISHED;
            }))
        {
            return views;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Timed out waiting for workload completion");
}

std::vector<ProcessView> run(const Config &config, const std::vector<Program> &programs,
                             const Observer &observe = {})
{
    Scheduler scheduler;
    std::uint32_t id = 0;
    for (const auto &program : programs)
    {
        ++id;
        scheduler.submitProcess(std::make_unique<Process>(id, "p" + std::to_string(id), program));
    }
    scheduler.start(config);
    return finish(scheduler, observe);
}

Program increments(std::size_t count)
{
    Program program;
    program.instructions.assign(count, Instruction{AddInstruction{
        "x", Operand::variable("x"), Operand::literal(1)}});
    return program;
}

void checkTickPacing(const Config &config, const std::vector<Program> &programs)
{
    Scheduler scheduler;
    std::uint32_t id = 0;
    for (const auto &program : programs)
    {
        ++id;
        scheduler.submitProcess(std::make_unique<Process>(id, "p" + std::to_string(id), program));
    }
    scheduler.start(config);
    const auto completed = finish(scheduler, [&](const auto &views) {
        // These fixtures have no loops, so the source line counts executed instructions.
        std::size_t instructions = 0;
        std::vector<std::size_t> printsPerCore(config.numCpu);
        for (const auto &view : views)
        {
            instructions += view.currentLine;
            for (const auto &stamp : view.logStamps)
            {
                check(stamp.core && *stamp.core < printsPerCore.size(), "PRINT has no valid core");
                ++printsPerCore[*stamp.core];
            }
        }
        // Read after the snapshot; allow one instruction in the initial tick.
        const auto ticks = scheduler.cycleCount() + 1;
        check(instructions <= ticks * scheduler.coreCount(), "Instructions exceeded the CPU tick budget");
        for (const auto prints : printsPerCore)
        {
            check(prints <= ticks, "A core executed multiple PRINTs per CPU tick");
        }
    });
    for (std::size_t i = 0; i < completed.size(); ++i)
    {
        check(completed[i].currentLine == programs[i].instructions.size(), "Paced workload lost instructions");
    }
}

void checkExecutionDelay(const Config &config, std::size_t processCount)
{
    Program program;
    program.instructions.assign(4, Instruction{PrintInstruction{"delayed", std::nullopt}});
    Scheduler scheduler;
    for (std::size_t i = 0; i < processCount; ++i)
    {
        scheduler.submitProcess(std::make_unique<Process>(
            static_cast<std::uint32_t>(i + 1), "p" + std::to_string(i + 1), program));
    }
    scheduler.start(config);
    const auto completed = finish(scheduler, [&](const auto &views) {
        const auto ticks = scheduler.cycleCount();
        for (const auto &view : views)
        {
            check(view.logs.size() <= 1 + ticks / config.delayPerExec,
                  "Dispatch bypassed a process's execution delay");
        }
    });
    for (const auto &view : completed)
    {
        check(view.logs == std::vector<std::string>(4, "delayed"), "Delayed workload lost or repeated output");
    }
}

void compare(const std::vector<ProcessView> &expected, const std::vector<ProcessView> &actual)
{
    check(actual.size() == expected.size(), "Processes were lost");
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        check(actual[i].logs == expected[i].logs, "Preemption changed program output");
        check(actual[i].variables == expected[i].variables, "Preemption lost or repeated instructions");
        check(actual[i].currentLine == expected[i].currentLine, "Program position was not preserved");
    }
}

void shortenSleeps(std::vector<Instruction> &instructions)
{
    // Keep seeded workloads quick on Windows, where a CPU tick can take ~15 ms.
    for (auto &instruction : instructions)
    {
        if (auto *sleep = std::get_if<SleepInstruction>(&instruction.payload))
        {
            sleep->ticks = 1;
        }
        if (auto *loop = std::get_if<ForInstruction>(&instruction.payload))
        {
            shortenSleeps(loop->body);
        }
    }
}
} // namespace

int main()
{
    try
    {
        for (const int cores : {1, 4})
        {
            Config delayed;
            delayed.numCpu = cores;
            delayed.scheduler = SchedulerType::RR;
            delayed.quantumCycles = 1;
            delayed.delayPerExec = 8;
            checkExecutionDelay(delayed, 1);
            checkExecutionDelay(delayed, cores + 1);
            delayed.quantumCycles = 3;
            checkExecutionDelay(delayed, cores + 1);
            delayed.scheduler = SchedulerType::FCFS;
            checkExecutionDelay(delayed, 1);
        }

        Program print;
        print.instructions = {Instruction{PrintInstruction{"tick", std::nullopt}}};
        Program longPrint;
        longPrint.instructions.assign(32, print.instructions.front());
        Program yielding;
        yielding.instructions = {Instruction{SleepInstruction{2}}, print.instructions.front()};
        for (const auto type : {SchedulerType::FCFS, SchedulerType::RR})
        {
            for (const int cores : {1, 4})
            {
                Config pacing;
                pacing.numCpu = cores;
                pacing.scheduler = type;
                pacing.quantumCycles = 1;
                checkTickPacing(pacing, {longPrint});
                checkTickPacing(pacing, std::vector<Program>(32, print));
                checkTickPacing(pacing, std::vector<Program>(32, yielding));
            }
        }

        Config config;
        config.numCpu = 1;
        config.scheduler = SchedulerType::RR;
        config.quantumCycles = 1;
        bool rotated = false;
        const auto roundRobin = run(config, std::vector<Program>(3, increments(16)),
                                    [&](const auto &views) {
            rotated = rotated || (views[1].currentLine > 0 &&
                                   views[0].state != ProcessState::FINISHED);
            if (!(views[0].currentLine >= views[1].currentLine &&
                  views[1].currentLine >= views[2].currentLine &&
                  views[0].currentLine - views[2].currentLine <= 1))
            {
                throw std::runtime_error("Quantum 1 rotation: " +
                    std::to_string(views[0].currentLine) + ", " +
                    std::to_string(views[1].currentLine) + ", " +
                    std::to_string(views[2].currentLine));
            }
        });
        check(rotated, "RR ran the first process to completion before rotating");
        for (const auto &view : roundRobin)
        {
            check(view.variables.at("x") == 16, "Rotation skipped or repeated instructions");
        }

        config.scheduler = SchedulerType::FCFS;
        config.delayPerExec = 1;
        const auto fcfs = run(config, std::vector<Program>(3, increments(16)), [](const auto &views) {
            check(views[1].currentLine == 0 || views[0].state == ProcessState::FINISHED,
                  "FCFS was preempted by quantum-cycles");
        });
        config.quantumCycles = std::numeric_limits<std::uint32_t>::max();
        compare(fcfs, run(config, std::vector<Program>(3, increments(16))));
        config.scheduler = SchedulerType::RR;
        compare(fcfs, run(config, std::vector<Program>(3, increments(16)), [](const auto &views) {
            check(views[1].currentLine == 0 || views[0].state == ProcessState::FINISHED,
                  "Large quantum changed FCFS ordering");
        }));

        config.delayPerExec = 0;
        config.minIns = config.maxIns = 8;
        std::vector<Program> seeded;
        for (std::uint32_t seed = 1; seed <= 3; ++seed)
        {
            auto program = generateProgram(config, "seeded", seed);
            shortenSleeps(program.instructions);
            seeded.push_back(std::move(program));
        }
        config.scheduler = SchedulerType::FCFS;
        const auto expected = run(config, seeded);
        config.scheduler = SchedulerType::RR;
        compare(expected, run(config, seeded));
        config.quantumCycles = 1;
        compare(expected, run(config, seeded));
        config.numCpu = 4;
        config.delayPerExec = 3;
        compare(expected, run(config, seeded));

        Program nested;
        nested.instructions = {
            Instruction{ForInstruction{2, {
                increments(1).instructions.front(),
                Instruction{ForInstruction{3, {
                    increments(1).instructions.front(),
                    Instruction{PrintInstruction{"x=", std::string{"x"}}}}}},
                Instruction{SleepInstruction{2}}}}},
            Instruction{SleepInstruction{2}}};
        config.scheduler = SchedulerType::FCFS;
        const auto nestedExpected = run(config, {nested});
        check(nestedExpected.front().variables.at("x") == 8, "Invalid nested-loop fixture");
        config.scheduler = SchedulerType::RR;
        compare(nestedExpected, run(config, {nested}));

        config.numCpu = 1;
        config.quantumCycles = 8;
        config.delayPerExec = 1;
        Program sleeper = increments(21);
        sleeper.instructions[4] = Instruction{SleepInstruction{2}};
        bool slept = false;
        bool checkedFreshBudget = false;
        run(config, {sleeper, increments(24)}, [&](const auto &views) {
            slept = slept || views[0].state == ProcessState::WAITING;
            if (!checkedFreshBudget && views[1].currentLine > 8)
            {
                check(views[0].currentLine >= 12,
                      "Waking process did not receive a fresh quantum");
                checkedFreshBudget = true;
            }
        });
        check(slept && checkedFreshBudget, "Sleep budget scenario was not observed");

        config.numCpu = 2;
        config.quantumCycles = 1;
        config.delayPerExec = 8;
        Scheduler resumed;
        resumed.submitProcess(std::make_unique<Process>(1, "resume", increments(16)));
        resumed.start(config);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (resumed.snapshot().front().currentLine == 0)
        {
            check(std::chrono::steady_clock::now() < deadline, "Resume fixture never started");
            std::this_thread::yield();
        }
        resumed.stop();
        const auto stopped = resumed.snapshot();
        check(!stopped.front().core, "Stopped scheduler retained a core assignment");
        check(stopped.front().state == ProcessState::READY, "Stopped process was not requeued");
        resumed.start(config);
        check(finish(resumed, [&](const auto &views) {
            check(views.front().currentLine <= 1 + resumed.cycleCount() / config.delayPerExec,
                  "Restart bypassed the remaining execution delay");
        }).front().variables.at("x") == 16, "Restart lost program position");
        resumed.stop();
        for (int attempt = 0; attempt < 32; ++attempt)
        {
            resumed.start(config);
            resumed.stop();
        }
        config.quantumCycles = 0;
        bool rejectedZero = false;
        try
        {
            resumed.start(config);
        }
        catch (const std::invalid_argument &)
        {
            rejectedZero = true;
        }
        check(rejectedZero && !resumed.isRunning(), "Zero RR quantum must fail before starting threads");
        std::cout << "Scheduler quantum checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
