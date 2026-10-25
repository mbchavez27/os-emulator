#pragma once

#include "Scheduler.h"

#include <cstddef>
#include <string>
#include <vector>

struct ProcessReportSnapshot
{
    std::size_t totalCores = 0;
    std::vector<ProcessView> processes;
};

/// Pure formatter shared by screen -ls and report-util.
std::string buildProcessReport(const ProcessReportSnapshot &snapshot);
