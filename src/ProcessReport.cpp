#include "ProcessReport.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>

namespace
{

// Layout follows the spec's screen -ls mockup.
const char *const kRule = "----------------------------------------\n";
constexpr std::size_t kNameGap = 3;     ///< Spaces after the longest name.
constexpr std::size_t kStatusWidth = 13; ///< "Core: 127" / "Finished" plus a gap.

std::string formatStartTime(std::chrono::system_clock::time_point time)
{
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(time);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::put_time(&localTime, "%m/%d/%Y %I:%M:%S%p");
    return out.str();
}

/// "100%" for whole numbers, "33.33%" otherwise.
std::string formatUtilization(std::size_t used, std::size_t total)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    if (total == 0 || (used * 100) % total == 0)
    {
        out << (total == 0 ? 0 : used * 100 / total);
    }
    else
    {
        out << std::fixed << std::setprecision(2)
            << 100.0 * static_cast<double>(used) / static_cast<double>(total);
    }
    out << "%";
    return out.str();
}

void appendRow(std::ostringstream &report, const ProcessView &process, std::size_t nameWidth)
{
    std::string status;
    std::size_t done = process.linesOfCode;
    if (process.state == ProcessState::FINISHED)
    {
        status = "Finished";
    }
    else
    {
        status = "Core: " + std::to_string(*process.core);
        done = process.currentLine;
    }
    report << std::left << std::setw(static_cast<int>(nameWidth)) << process.name
           << "(" << formatStartTime(process.startedAt) << ")    "
           << std::setw(static_cast<int>(kStatusWidth)) << status
           << done << " / " << process.linesOfCode << "\n";
}

} // namespace

std::string buildProcessReport(const ProcessReportSnapshot &snapshot)
{
    // "Running" means on a core, as in the mockup; ready and sleeping processes
    // are listed once they run again or finish.
    std::vector<const ProcessView *> running;
    std::vector<const ProcessView *> finished;
    std::size_t longestName = 0;
    for (const ProcessView &process : snapshot.processes)
    {
        if (process.state == ProcessState::FINISHED)
        {
            finished.push_back(&process);
        }
        else if (process.core)
        {
            running.push_back(&process);
        }
        else
        {
            continue;
        }
        longestName = std::max(longestName, process.name.size());
    }
    std::sort(running.begin(), running.end(),
              [](const ProcessView *a, const ProcessView *b) { return *a->core < *b->core; });

    const std::size_t used = running.size();
    const std::size_t available = snapshot.totalCores > used ? snapshot.totalCores - used : 0;
    const std::size_t nameWidth = longestName + kNameGap;

    std::ostringstream report;
    report.imbue(std::locale::classic());
    report << "CPU utilization: " << formatUtilization(used, snapshot.totalCores) << "\n"
           << "Cores used: " << used << "\n"
           << "Cores available: " << available << "\n\n"
           << kRule
           << "Running processes:\n";
    for (const ProcessView *process : running)
    {
        appendRow(report, *process, nameWidth);
    }
    report << "\nFinished processes:\n";
    for (const ProcessView *process : finished)
    {
        appendRow(report, *process, nameWidth);
    }
    report << kRule << "\n";
    return report.str();
}
