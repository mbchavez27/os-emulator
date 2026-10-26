#pragma once

#include "Scheduler.h"

#include <chrono>
#include <string>

/// Formats a log timestamp as `MM/DD/YYYY HH:MM:SSAM/PM` in local time, with
/// no space before the meridiem. The only place that format is defined.
std::string formatLogTimestamp(std::chrono::system_clock::time_point time);

/// Formats one log line: `(MM/DD/YYYY HH:MM:SSAM/PM) Core:<n> "<message>"`.
/// A line with no recorded core prints `Core:--`.
std::string formatLogLine(const LogStamp &stamp, const std::string &message);

/// Pure renderer for `process-smi`. Prints the name, ID, and logs, then either
/// the progress block or `Finished!`. Reads only the copied view, so it is safe
/// to call with no scheduler lock held.
std::string buildProcessSmi(const ProcessView &process);
