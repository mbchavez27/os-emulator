#pragma once

#include <cstdint>
#include <optional>
#include <string>

enum class SchedulerType
{
    FCFS,
    RR
};

/**
 * @brief Emulator configuration loaded from config.txt.
 *
 * load() validates every value before returning a Config. The console never
 * changes it afterwards: Emulator keeps it private and hands out only a const
 * reference. Fields stay plain so tests can build configs directly; Scheduler
 * re-checks the values it depends on (quantum, frequency, instruction bounds).
 */
struct Config
{
    int numCpu = 0;
    SchedulerType scheduler = SchedulerType::FCFS;
    // 64-bit so the spec's inclusive upper bound of 2^32 fits.
    uint64_t quantumCycles = 0;
    uint64_t batchProcessFreq = 0;
    uint64_t minIns = 0;
    uint64_t maxIns = 0;
    uint64_t delayPerExec = 0;

    /**
     * @brief Parses and validates a space-separated config file.
     *
     * Unknown keys are ignored so extra lines do not break loading. Both
     * `delay-per-exec` and `delays-per-exec` are accepted.
     *
     * @return The config on success; std::nullopt on failure with `error`
     *         describing the first invalid key or value.
     */
    static std::optional<Config> load(const std::string &path, std::string &error);
};
