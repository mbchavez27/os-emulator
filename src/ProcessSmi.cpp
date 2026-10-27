#include "ProcessSmi.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>

std::string formatLogTimestamp(std::chrono::system_clock::time_point time)
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

std::string formatLogLine(const LogStamp &stamp, const std::string &message)
{
    std::ostringstream out;
    out << "(" << formatLogTimestamp(stamp.at) << ") Core:";
    if (stamp.core)
    {
        out << *stamp.core;
    }
    else
    {
        out << "--";
    }
    out << " \"" << message << "\"";
    return out.str();
}

std::string buildProcessSmi(const ProcessView &process)
{
    std::ostringstream out;
    out << "Process name: " << process.name << "\n"
        << "ID: " << process.id << "\n"
        << "Logs:\n";

    // logs and logStamps are parallel and written together; min() keeps a
    // hand-built view with mismatched lengths from reading out of bounds.
    const std::size_t lines = std::min(process.logs.size(), process.logStamps.size());
    for (std::size_t i = 0; i < lines; ++i)
    {
        out << formatLogLine(process.logStamps[i], process.logs[i]) << "\n";
    }
    out << "\n";

    if (process.state == ProcessState::FINISHED)
    {
        out << "Finished!\n";
    }
    else
    {
        out << "Current instruction line: " << process.currentLine << "\n"
            << "Lines of code: " << process.linesOfCode << "\n";
    }
    return out.str();
}
