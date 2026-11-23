#pragma once

#include "kernel/Scheduler.h"

#include <string>

/**
 * @brief Formats the main-menu process-smi summary, in the style of nvidia-smi.
 *
 * Shows CPU and memory utilization, then every unfinished process with the
 * physical memory its resident pages occupy. Pure function of the snapshot.
 *
 * @param snapshot copied scheduler and memory state
 * @return the full summary text
 */
std::string buildProcessSmiSummary(const SystemSnapshot &snapshot);

/**
 * @brief Formats the vmstat report: memory, frames, process counts, CPU ticks, paging.
 *
 * Active processes hold a core; inactive ones are ready or sleeping. Ticks are
 * core-ticks, so total = idle + active = elapsed ticks * cores.
 *
 * @param snapshot copied scheduler and memory state
 * @return one right-aligned value and label per line
 */
std::string buildVmstat(const SystemSnapshot &snapshot);
