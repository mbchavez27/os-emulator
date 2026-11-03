#pragma once

#include "Scheduler.h"

#include <cstddef>
#include <string>
#include <vector>

/** @brief Everything one screen -ls / report-util report is built from. */
struct ProcessReportSnapshot
{
    std::size_t totalCores = 0;         /**< Configured num-cpu. */
    std::vector<ProcessView> processes; /**< Copied process state, in submission order. */
};

/**
 * @brief Formats the CPU utilization report shared by screen -ls and report-util.
 *
 * Pure function: the same snapshot always produces the same text, so both
 * commands print identical output for the same moment.
 *
 * @param snapshot core count and copied process state
 * @return the full report, ending with a blank line
 */
std::string buildProcessReport(const ProcessReportSnapshot &snapshot);
