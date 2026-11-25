#include "shell/MemoryReport.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

ProcessView process(const std::string &name, ProcessState state, std::optional<std::size_t> core,
                    std::uint64_t resident)
{
    ProcessView view;
    view.name = name;
    view.state = state;
    view.core = core;
    view.residentBytes = resident;
    return view;
}
} // namespace

int main()
{
    try
    {
        SystemSnapshot snapshot;
        snapshot.totalCores = 4;
        snapshot.processes = {process("p01", ProcessState::RUNNING, 0, 256),
                              process("p02", ProcessState::READY, std::nullopt, 768),
                              process("p03", ProcessState::WAITING, std::nullopt, 0),
                              process("p04", ProcessState::FINISHED, std::nullopt, 0)};
        snapshot.memory.totalBytes = 16384;
        snapshot.memory.usedBytes = 1024;
        snapshot.memory.freeBytes = 15360;
        snapshot.memory.frameSize = 16;
        snapshot.memory.totalFrames = 1024;
        snapshot.memory.usedFrames = 64;
        snapshot.memory.pagedIn = 57;
        snapshot.memory.pagedOut = 12;
        snapshot.activeTicks = 9822;
        snapshot.idleTicks = 4021;

        check(buildProcessSmiSummary(snapshot) ==
                  "----------------------------------------------\n"
                  "| PROCESS-SMI V01.00 Driver Version: 01.00 |\n"
                  "----------------------------------------------\n"
                  "CPU-Util: 25%\n"
                  "Memory Usage: 1024B / 16384B\n"
                  "Memory Util: 6%\n"
                  "\n"
                  "==============================================\n"
                  "Running processes and memory usage:\n"
                  "----------------------------------------------\n"
                  "p01 256B\n"
                  "p02 768B\n"
                  "p03 0B\n"
                  "----------------------------------------------\n",
              "process-smi layout is wrong");

        check(buildVmstat(snapshot) ==
                  "        16384 B total memory\n"
                  "         1024 B used memory\n"
                  "        15360 B free memory\n"
                  "         1024 total frames\n"
                  "           64 used frames\n"
                  "          960 free frames\n"
                  "            1 active processes\n"
                  "            2 inactive processes\n"
                  "         4021 idle cpu ticks\n"
                  "         9822 active cpu ticks\n"
                  "        13843 total cpu ticks\n"
                  "           57 num paged in\n"
                  "           12 num paged out\n",
              "vmstat layout is wrong");

        // Percentages round to whole numbers, as in the mockup's 1245MiB / 4799MiB = 26%.
        SystemSnapshot rounded;
        rounded.totalCores = 3;
        rounded.processes = {process("a", ProcessState::RUNNING, 0, 0)};
        rounded.memory.totalBytes = 4799;
        rounded.memory.usedBytes = 1245;
        const std::string roundedText = buildProcessSmiSummary(rounded);
        check(roundedText.find("Memory Util: 26%\n") != std::string::npos, "Memory Util must round to 26%");
        check(roundedText.find("CPU-Util: 33%\n") != std::string::npos, "CPU-Util must round to 33%");
        rounded.memory.usedBytes = 4799;
        check(buildProcessSmiSummary(rounded).find("Memory Util: 100%\n") != std::string::npos,
              "Full memory must be 100%");

        // Empty system: no division by zero, no process rows.
        const std::string empty = buildProcessSmiSummary(SystemSnapshot{});
        check(empty.find("CPU-Util: 0%") != std::string::npos &&
                  empty.find("Memory Usage: 0B / 0B") != std::string::npos,
              "An empty snapshot must print zeros");

        // Live: processes bigger than memory force evictions; memory never overfills.
        Config config;
        config.numCpu = 2;
        config.scheduler = SchedulerType::RR;
        config.quantumCycles = 5;
        config.batchProcessFreq = 1;
        config.minIns = 200;
        config.maxIns = 200;
        config.delayPerExec = 1; // Spread execution over many ticks.
        config.maxOverallMem = 256;
        config.memPerFrame = 64;
        config.minMemPerProc = 256;
        config.maxMemPerProc = 1024;

        Scheduler scheduler;
        scheduler.start(config);
        const std::uint64_t sizes[] = {256, 512, 1024, 512};
        for (std::uint32_t id = 1; id <= 4; ++id)
        {
            check(scheduler.createProcess("m0" + std::to_string(id), sizes[id - 1], config).has_value(),
                  "createProcess failed");
        }
        std::uint64_t previousTicks = 0;
        bool sawEviction = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        SystemSnapshot live;
        while (std::chrono::steady_clock::now() < deadline)
        {
            live = scheduler.systemSnapshot();
            check(live.memory.usedBytes <= live.memory.totalBytes, "Used memory exceeded total");
            check(live.memory.totalBytes == 256 && live.memory.totalFrames == 4, "Frame pool is wrong");
            check(live.memory.corruptReads == 0, "A page read back from the backing store did not match");
            const std::uint64_t ticks = live.activeTicks + live.idleTicks;
            check(ticks >= previousTicks, "CPU ticks went backwards");
            previousTicks = ticks;
            std::uint64_t resident = 0;
            for (const ProcessView &view : live.processes)
            {
                check(view.memoryBytes == 0 || (view.memoryBytes & (view.memoryBytes - 1)) == 0,
                      "Process memory must be a power of 2");
                resident += view.residentBytes;
            }
            check(resident == live.memory.usedBytes, "Resident bytes must add up to used memory");
            sawEviction = sawEviction || live.memory.pagedOut > 0;

            bool allFinished = true;
            for (const ProcessView &view : live.processes)
            {
                allFinished = allFinished && view.state == ProcessState::FINISHED;
            }
            if (allFinished)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        scheduler.stop();

        check(sawEviction, "Processes larger than memory never caused an eviction");
        check(live.memory.pagedIn >= live.memory.pagedOut, "Every evicted page was paged in first");
        check(live.memory.usedBytes == 0, "Finished processes must release their frames");
        check(live.activeTicks > 0, "No active CPU ticks were counted");
        check(buildVmstat(live).find("num paged out") != std::string::npos, "vmstat must print paging");

        std::cout << "Memory report checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
