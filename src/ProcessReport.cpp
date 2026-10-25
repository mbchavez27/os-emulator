#include "ProcessReport.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>

std::string buildProcessReport(const ProcessReportSnapshot &snapshot)
{
    const auto used = static_cast<std::size_t>(std::count_if(
        snapshot.processes.begin(), snapshot.processes.end(),
        [](const ProcessView &process) { return process.core.has_value(); }));
    const double utilization = snapshot.totalCores == 0
                                   ? 0.0
                                   : 100.0 * static_cast<double>(used) /
                                         static_cast<double>(snapshot.totalCores);

    std::ostringstream report;
    report.imbue(std::locale::classic());
    report << std::fixed << std::setprecision(2)
           << "CPU utilization: " << utilization << "%\n"
           << "Cores used: " << used << "\n"
           << "Cores available: " << snapshot.totalCores - used << "\n\n";

    for (const bool finished : {false, true})
    {
        report << (finished ? "Finished processes:\n" : "Running processes:\n");
        for (const ProcessView &process : snapshot.processes)
        {
            if ((process.state == ProcessState::FINISHED) != finished)
            {
                continue;
            }

            const std::time_t timestamp = std::chrono::system_clock::to_time_t(process.startedAt);
            std::tm localTime{};
#ifdef _WIN32
            localtime_s(&localTime, &timestamp);
#else
            localtime_r(&timestamp, &localTime);
#endif
            report << process.name << " ("
                   << std::put_time(&localTime, "%m/%d/%Y %I:%M:%S %p") << ") ";
            if (finished)
            {
                report << "Finished " << process.linesOfCode;
            }
            else
            {
                report << "Core: ";
                if (process.core)
                {
                    report << *process.core;
                }
                else
                {
                    report << "--";
                }
                report << " " << process.currentLine;
            }
            report << " / " << process.linesOfCode << "\n";
        }
        report << "\n";
    }
    return report.str();
}
