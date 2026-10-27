#include "CommandHandler.h"
#include "Emulator.h"
#include "ProcessReport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace
{
constexpr auto noAllocationFailure = std::numeric_limits<std::size_t>::max();
thread_local std::size_t failAllocationAfter = noAllocationFailure;
std::atomic<std::size_t> failAllocationAtLeast{noAllocationFailure};
}

// Leave runtime bookkeeping available while failing process-sized allocations.
void *operator new(std::size_t size)
{
    if (size >= sizeof(Instruction) && failAllocationAfter != noAllocationFailure && failAllocationAfter-- == 0)
    {
        failAllocationAfter = noAllocationFailure;
        throw std::bad_alloc{};
    }
    if (size >= failAllocationAtLeast.load() && size >= failAllocationAtLeast.exchange(noAllocationFailure))
    {
        throw std::bad_alloc{};
    }
    if (void *memory = std::malloc(size == 0 ? 1 : size))
    {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::size_t coresUsed(const std::vector<ProcessView> &views)
{
    return static_cast<std::size_t>(std::count_if(
        views.begin(), views.end(),
        [](const ProcessView &view) { return view.core.has_value(); }));
}

template <typename Predicate>
std::vector<ProcessView> waitFor(Scheduler &scheduler, Predicate ready)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do
    {
        auto views = scheduler.snapshot();
        if (ready(views))
        {
            return views;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    throw std::runtime_error("Timed out waiting for scheduler state");
}

std::string drive(Emulator &emulator, const std::string &input)
{
    std::istringstream commands(input);
    std::ostringstream output;
    auto *previousInput = std::cin.rdbuf(commands.rdbuf());
    auto *previousOutput = std::cout.rdbuf(output.rdbuf());
    CommandHandler(emulator).run();
    std::cin.rdbuf(previousInput);
    std::cout.rdbuf(previousOutput);
    return output.str();
}

void checkAllocationFailures()
{
    Config config;
    config.numCpu = 1;
    config.batchProcessFreq = 2;
    config.minIns = config.maxIns = 4;
    for (const std::size_t existing : {0u, 63u})
    {
        bool succeeded = false;
        for (std::size_t allocation = 0; allocation < 128; ++allocation)
        {
            Scheduler scheduler;
            Program queued{{Instruction{DeclareInstruction{"x", 1}}}};
            for (std::size_t i = 0; i < existing; ++i)
            {
                scheduler.submitProcess(std::make_unique<Process>(
                    static_cast<std::uint32_t>(i + 1), "queued" + std::to_string(i + 1), queued));
            }
            bool failed = false;
            failAllocationAfter = allocation;
            try
            {
                scheduler.createProcess("retry", config);
            }
            catch (const std::bad_alloc &)
            {
                failed = true;
            }
            failAllocationAfter = noAllocationFailure;
            if (!failed)
            {
                succeeded = true;
                break;
            }
            check(scheduler.snapshot().size() == existing, "Failed creation changed the process list");
            check(scheduler.createProcess("retry", config) == existing + 1,
                  "Failed creation consumed an id or name");
        }
        check(succeeded, "Allocation failure checks never reached successful creation");
    }

    Scheduler background;
    Program pending;
    pending.instructions.assign(32, Instruction{PrintInstruction{"draining", std::nullopt}});
    background.submitProcess(std::make_unique<Process>(1, "pending", pending));
    config.minIns = config.maxIns = 4096;
    failAllocationAtLeast.store(config.minIns * sizeof(Instruction));
    background.startGeneration(config);
    waitFor(background, [&](const auto &) { return background.takeGenerationFailure(); });
    failAllocationAtLeast.store(noAllocationFailure);
    check(!background.takeGenerationFailure(), "Generation failure was reported more than once");
    check(!background.stopGeneration() && background.isRunning(), "Creation failure did not stop only generation");
    const auto finished = waitFor(background, [](const auto &views) {
        return views.front().state == ProcessState::FINISHED;
    });
    check(finished.size() == 1 && finished.front().logs.size() == 32, "Creation failure interrupted existing work");
    config.minIns = config.maxIns = 1;
    check(background.startGeneration(config), "Generation could not restart after allocation failure");
    waitFor(background, [](const auto &views) { return views.size() >= 2; });
    background.stopGeneration();
    check(background.snapshot()[1].id == 2, "Failed generation consumed a process id");
    background.stop();

    Emulator emulator;
    std::string error;
    check(emulator.initialize("config.txt", error), "Failure test could not initialize");
    failAllocationAtLeast.store(100 * sizeof(Instruction));
    const auto manual = drive(emulator, "screen -s failed\nscreen -ls\n");
    failAllocationAtLeast.store(noAllocationFailure);
    check(manual.find("Failed to create process failed:") != std::string::npos,
          "Manual creation failure was not reported");
    check(emulator.scheduler().snapshot().empty(), "Failed manual creation registered a process");
    failAllocationAtLeast.store(100 * sizeof(Instruction));
    emulator.scheduler().startGeneration(emulator.config());
    waitFor(emulator.scheduler(), [](const auto &) {
        return failAllocationAtLeast.load() == noAllocationFailure;
    });
    const auto automatic = drive(emulator, "screen -ls\nscreen -ls\nexit\n");
    const std::string message = "Process generation stopped: unable to create a process.";
    check(automatic.find(message) != std::string::npos && automatic.find(message) == automatic.rfind(message),
          "Automatic creation failure was not reported exactly once");
    check(!emulator.is_app_alive, "Creation failure prevented the console from accepting exit");
}
} // namespace

int main()
{
    try
    {
        checkAllocationFailures();

        Emulator emulator;
        Scheduler &scheduler = emulator.scheduler();
        const std::string empty = buildProcessReport({4, scheduler.snapshot()});
        check(empty == "CPU utilization: 0.00%\nCores used: 0\nCores available: 4\n\n"
                       "Running processes:\n\nFinished processes:\n\n",
              "Empty sections or idle utilization are incorrect");
        check(buildProcessReport({}).find("CPU utilization: 0.00%") == 0,
              "Zero cores must not divide by zero");

        Program program;
        program.instructions.assign(32, Instruction{DeclareInstruction{"x", 1}});
        program.instructions.front() = Instruction{SleepInstruction{3}};
        scheduler.submitProcess(std::make_unique<Process>(1, "p01", program));
        const auto queued = scheduler.snapshot();
        check(buildProcessReport({4, queued}).find("Core: -- 0 / 32") != std::string::npos,
              "Queued processes must be visible without using a core");
        auto timestamped = queued;
        std::tm timestamp{};
        timestamp.tm_year = 126;
        timestamp.tm_mon = 0;
        timestamp.tm_mday = 2;
        timestamp.tm_hour = 15;
        timestamp.tm_min = 4;
        timestamp.tm_sec = 5;
        timestamp.tm_isdst = -1;
        timestamped.front().startedAt = std::chrono::system_clock::from_time_t(std::mktime(&timestamp));
        check(buildProcessReport({4, timestamped}).find(
                  "p01 (01/02/2026 03:04:05 PM) Core: -- 0 / 32") != std::string::npos,
              "Process name or start timestamp is missing");
        Config config;
        config.numCpu = 4;
        config.delayPerExec = 2;
        scheduler.start(config);
        const auto partial = waitFor(scheduler, [](const auto &views) {
            return coresUsed(views) == 1;
        });
        const std::string partialReport = buildProcessReport({4, partial});
        check(partialReport.find("CPU utilization: 25.00%\nCores used: 1\nCores available: 3") == 0,
              "Partial utilization is incorrect");
        check(buildProcessReport({3, partial}).find("CPU utilization: 33.33%") == 0,
              "Fractional utilization was truncated");
        check(partial.front().startedAt == queued.front().startedAt,
              "Process start timestamp changed after dispatch");
        for (std::uint32_t id = 2; id <= 4; ++id)
        {
            scheduler.submitProcess(std::make_unique<Process>(id, "p0" + std::to_string(id), program));
        }
        const auto full = waitFor(scheduler, [](const auto &views) {
            return coresUsed(views) == 4;
        });
        check(buildProcessReport({4, full}).find(
                  "CPU utilization: 100.00%\nCores used: 4\nCores available: 0") == 0,
              "Full utilization is incorrect");

        // Validate each copy while workers advance, including completion transitions.
        std::size_t liveSamples = 0;
        const auto finished = waitFor(scheduler, [&](const auto &views) {
            std::array<bool, 4> occupied{};
            for (const auto &view : views)
            {
                check(view.currentLine <= view.linesOfCode, "Torn instruction progress");
                check(view.core.has_value() == (view.state == ProcessState::RUNNING),
                      "Process state and core assignment disagree");
                if (view.core)
                {
                    check(*view.core < occupied.size(), "Invalid core assignment");
                    check(!occupied[*view.core], "Two processes share one core");
                    occupied[*view.core] = true;
                }
            }
            check(!buildProcessReport({4, views}).empty(), "Live formatting failed");
            liveSamples += coresUsed(views) > 0 ? 1 : 0;
            return std::all_of(views.begin(), views.end(), [](const auto &view) {
                return view.state == ProcessState::FINISHED;
            });
        });
        scheduler.stop();
        check(liveSamples > 0, "No active snapshots were checked");
        check(buildProcessReport({4, partial}) == partialReport, "Snapshot mutated after capture");
        check(buildProcessReport({4, finished}).find("Finished 32 / 32") != std::string::npos,
              "Finished rows must show total / total");

        std::istringstream commands(
            "scheduler-start\ninitialize\n  screen\t-ls  \nreport-util\n"
            "scheduler-stop\nscheduler-start\nscheduler-start\nscheduler-stop\nscheduler-stop\nexit\n");
        std::ostringstream output;
        auto *previousInput = std::cin.rdbuf(commands.rdbuf());
        auto *previousOutput = std::cout.rdbuf(output.rdbuf());
        CommandHandler(emulator).run();
        std::cin.rdbuf(previousInput);
        std::cout.rdbuf(previousOutput);
        const std::string expected = buildProcessReport({
            static_cast<std::size_t>(emulator.config().numCpu), finished});
        check(output.str().find(expected) != std::string::npos, "screen -ls did not print the report");
        check(output.str().find("System not yet initialized.") != std::string::npos,
              "scheduler-start was accepted before initialize");
        check(output.str().find("Process generation started.") != std::string::npos &&
                  output.str().find("Process generation is already running.") != std::string::npos,
              "CLI start commands did not control generation");
        check(output.str().find("Process generation stopped. Existing processes will continue running.") != std::string::npos &&
                  output.str().find("Process generation is not running.") != std::string::npos,
              "CLI stop commands did not control generation");
        std::ifstream file("csopesy-log.txt");
        check(file.is_open(), "report-util did not create csopesy-log.txt");
        const std::string saved{std::istreambuf_iterator<char>(file), {}};
        check(saved == expected, "report-util differs from screen -ls");
        std::cout << "Scheduler report checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
