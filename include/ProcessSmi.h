#pragma once

#include "Scheduler.h"

#include <chrono>
#include <string>

/**
 * @brief Formats a log timestamp in local time.
 *
 * The only place the `MM/DD/YYYY HH:MM:SSAM` format is defined; there is no
 * space before AM/PM, as in the spec mockup.
 *
 * @param time the moment to format
 * @return e.g. "11/04/2026 01:42:23AM"
 */
std::string formatLogTimestamp(std::chrono::system_clock::time_point time);

/**
 * @brief Formats one process-smi log line.
 *
 * @param stamp when and on which core the PRINT executed
 * @param message the printed text
 * @return `(MM/DD/YYYY HH:MM:SSAM) Core:<n> "<message>"`, or `Core:--` when no
 *         core was recorded
 */
std::string formatLogLine(const LogStamp &stamp, const std::string &message);

/**
 * @brief Renders the process-smi output for one process.
 *
 * Prints the name, ID, and logs, then either the progress block or
 * `Finished!`. Reads only the copied view, so it is safe to call with no
 * scheduler lock held.
 *
 * @param process copied state of the attached process
 * @return the full process-smi text
 */
std::string buildProcessSmi(const ProcessView &process);
