#include "ProcessSmi.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::chrono::system_clock::time_point localTime(int year, int month, int day,
                                                int hour, int minute, int second)
{
    std::tm parts{};
    parts.tm_year = year - 1900;
    parts.tm_mon = month - 1;
    parts.tm_mday = day;
    parts.tm_hour = hour;
    parts.tm_min = minute;
    parts.tm_sec = second;
    parts.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&parts));
}

std::vector<std::string> linesOf(const std::string &text)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t end = text.find('\n'); end != std::string::npos; end = text.find('\n', start))
    {
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}
} // namespace

int main()
{
    try
    {
        // The provided mockup, byte for byte.
        ProcessView view;
        view.id = 1;
        view.name = "screen_01";
        view.state = ProcessState::RUNNING;
        view.currentLine = 153;
        view.linesOfCode = 1240;
        view.logs = {"Hello world from screen_01!"};
        view.logStamps = {LogStamp{localTime(2024, 5, 6, 9, 15, 12), 0}};
        check(buildProcessSmi(view) ==
                  "Process name: screen_01\n"
                  "ID: 1\n"
                  "Logs:\n"
                  "(05/06/2024 09:15:12AM) Core:0 \"Hello world from screen_01!\"\n"
                  "\n"
                  "Current instruction line: 153\n"
                  "Lines of code: 1240\n",
              "Running layout does not match the mockup");

        // PM and a multi-digit core, with no space after "Core:".
        check(formatLogLine(LogStamp{localTime(2026, 1, 2, 15, 4, 5), 12}, "x") ==
                  "(01/02/2026 03:04:05PM) Core:12 \"x\"",
              "Log line format is wrong");
        check(formatLogTimestamp(localTime(2026, 1, 2, 0, 0, 0)) == "01/02/2026 12:00:00AM",
              "Midnight must render as 12:00:00AM");
        check(formatLogTimestamp(localTime(2026, 1, 2, 12, 30, 0)) == "01/02/2026 12:30:00PM",
              "Noon must render as 12:30:00PM");

        // Finished: "Finished!" replaces the progress block, logs are kept.
        view.state = ProcessState::FINISHED;
        const std::string finished = buildProcessSmi(view);
        check(finished ==
                  "Process name: screen_01\n"
                  "ID: 1\n"
                  "Logs:\n"
                  "(05/06/2024 09:15:12AM) Core:0 \"Hello world from screen_01!\"\n"
                  "\n"
                  "Finished!\n",
              "Finished layout is wrong");
        check(finished.find("Current instruction line") == std::string::npos &&
                  finished.find("Lines of code") == std::string::npos,
              "Finished must not print the progress block");

        // No logs yet, and a mismatched hand-built view must not read out of bounds.
        ProcessView fresh;
        fresh.id = 7;
        fresh.name = "p07";
        fresh.linesOfCode = 12;
        check(buildProcessSmi(fresh) ==
                  "Process name: p07\nID: 7\nLogs:\n\n"
                  "Current instruction line: 0\nLines of code: 12\n",
              "Empty logs layout is wrong");
        fresh.logs = {"a", "b"};
        fresh.logStamps = {LogStamp{localTime(2026, 1, 2, 3, 4, 5), std::nullopt}};
        check(buildProcessSmi(fresh).find("Core:-- \"a\"") != std::string::npos,
              "A line with no core must print Core:--");

        // Sleeping and waiting processes still show progress.
        fresh.state = ProcessState::WAITING;
        check(buildProcessSmi(fresh).find("Lines of code: 12") != std::string::npos,
              "Non-finished states must show progress");

        // Live: a real scheduler records the executing core and a growing log.
        Config config;
        config.numCpu = 2;
        config.delayPerExec = 1;
        Program program;
        for (int i = 0; i < 40; ++i)
        {
            program.instructions.push_back(Instruction{PrintInstruction{"line " + std::to_string(i), std::nullopt}});
        }

        Scheduler scheduler;
        scheduler.start(config);
        scheduler.submitProcess(std::make_unique<Process>(1, "live", program));

        const std::regex logLine(
            R"(^\(\d\d/\d\d/\d{4} \d\d:\d\d:\d\d(AM|PM)\) Core:[01] "line \d+"$)");
        std::size_t previousLogs = 0;
        bool grew = false;
        bool sawProgress = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        ProcessView last;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto views = scheduler.snapshot();
            last = views.front();
            check(last.logs.size() == last.logStamps.size(), "Logs and stamps are out of step");
            check(last.logs.size() >= previousLogs, "Logs shrank between calls");
            grew = grew || last.logs.size() > previousLogs;
            previousLogs = last.logs.size();

            const std::string text = buildProcessSmi(last);
            sawProgress = sawProgress || (text.find("Current instruction line:") != std::string::npos &&
                                          last.state != ProcessState::FINISHED);
            for (const std::string &line : linesOf(text))
            {
                if (line.rfind('(', 0) == 0)
                {
                    check(std::regex_match(line, logLine), "Live log line has the wrong format");
                }
            }
            if (last.state == ProcessState::FINISHED)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        scheduler.stop();

        check(last.state == ProcessState::FINISHED, "The live process did not finish");
        check(grew, "Repeated calls never showed growing logs");
        check(sawProgress, "No running snapshot showed the progress block");
        check(last.logs.size() == 40, "Not every PRINT was logged");
        for (std::size_t i = 1; i < last.logStamps.size(); ++i)
        {
            check(last.logStamps[i].at >= last.logStamps[i - 1].at, "Logs are not in execution order");
        }
        check(std::all_of(last.logStamps.begin(), last.logStamps.end(),
                          [](const LogStamp &stamp) { return stamp.core && *stamp.core < 2; }),
              "A log line is missing its executing core");
        check(buildProcessSmi(last).find("Finished!\n") != std::string::npos,
              "A finished live process must print Finished!");

        std::cout << "Process-smi checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
