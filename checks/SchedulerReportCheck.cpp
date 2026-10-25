#include "CommandHandler.h"
#include "Emulator.h"
#include "ProcessReport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
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
} // namespace

int main()
{
    try
    {
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

        std::istringstream commands("initialize\n  screen\t-ls  \nreport-util\nexit\n");
        std::ostringstream output;
        auto *previousInput = std::cin.rdbuf(commands.rdbuf());
        auto *previousOutput = std::cout.rdbuf(output.rdbuf());
        CommandHandler(emulator).run();
        std::cin.rdbuf(previousInput);
        std::cout.rdbuf(previousOutput);
        const std::string expected = buildProcessReport({
            static_cast<std::size_t>(emulator.config().numCpu), finished});
        check(output.str().find(expected) != std::string::npos, "screen -ls did not print the report");
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
