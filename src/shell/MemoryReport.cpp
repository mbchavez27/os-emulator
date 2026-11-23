#include "shell/MemoryReport.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

namespace
{

// Rules are 46 characters, two wider than the 44-character box line, as in the mockup.
const char *const kRule = "----------------------------------------------\n";
const char *const kDoubleRule = "==============================================\n";
constexpr int kValueWidth = 13; /**< vmstat -s's right-aligned number column. */

/**
 * @brief Formats a share as a whole percentage, rounded, as in the mockup ("26%").
 *
 * @return "0%" when total is 0
 */
std::string formatWholePercent(std::uint64_t used, std::uint64_t total)
{
    if (total == 0)
    {
        return "0%";
    }
    return std::to_string((used * 200 + total) / (total * 2)) + "%";
}

bool isActive(const ProcessView &process)
{
    return process.core.has_value();
}

bool isInactive(const ProcessView &process)
{
    return !process.core && process.state != ProcessState::FINISHED;
}

} // namespace

std::string buildProcessSmiSummary(const SystemSnapshot &snapshot)
{
    const auto busyCores = static_cast<std::size_t>(
        std::count_if(snapshot.processes.begin(), snapshot.processes.end(), isActive));

    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << kRule
        << "| PROCESS-SMI V01.00 Driver Version: 01.00 |\n"
        << kRule
        << "CPU-Util: " << formatWholePercent(busyCores, snapshot.totalCores) << "\n"
        << "Memory Usage: " << snapshot.memory.usedBytes << "B / " << snapshot.memory.totalBytes << "B\n"
        << "Memory Util: "
        << formatWholePercent(snapshot.memory.usedBytes, snapshot.memory.totalBytes)
        << "\n\n"
        << kDoubleRule
        << "Running processes and memory usage:\n"
        << kRule;
    for (const ProcessView &process : snapshot.processes)
    {
        if (process.state != ProcessState::FINISHED)
        {
            out << process.name << " " << process.residentBytes << "B\n";
        }
    }
    out << kRule;
    return out.str();
}

std::string buildVmstat(const SystemSnapshot &snapshot)
{
    const MemoryStats &memory = snapshot.memory;
    const auto active = std::count_if(snapshot.processes.begin(), snapshot.processes.end(), isActive);
    const auto inactive = std::count_if(snapshot.processes.begin(), snapshot.processes.end(), isInactive);

    std::ostringstream out;
    out.imbue(std::locale::classic());
    auto line = [&out](auto value, const char *label) {
        out << std::setw(kValueWidth) << value << " " << label << "\n";
    };
    line(memory.totalBytes, "B total memory");
    line(memory.usedBytes, "B used memory");
    line(memory.freeBytes, "B free memory");
    line(memory.totalFrames, "total frames");
    line(memory.usedFrames, "used frames");
    line(memory.totalFrames - memory.usedFrames, "free frames");
    line(active, "active processes");
    line(inactive, "inactive processes");
    line(snapshot.idleTicks, "idle cpu ticks");
    line(snapshot.activeTicks, "active cpu ticks");
    line(snapshot.idleTicks + snapshot.activeTicks, "total cpu ticks");
    line(memory.pagedIn, "num paged in");
    line(memory.pagedOut, "num paged out");
    return out.str();
}
