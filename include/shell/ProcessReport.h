#pragma once

#include "kernel/Scheduler.h"

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

/**
 * @brief Formats a share as a percentage, as in the screen -ls mockup.
 *
 * @param used the part, e.g. cores running a process
 * @param total the whole, e.g. configured cores
 * @return "100%" for whole numbers, "33.33%" otherwise; "0%" when total is 0
 */
std::string formatUtilization(std::size_t used, std::size_t total);
