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
 * Immutable once created: the only way to obtain a Config is the load()
 * factory, which validates every value before returning it.
 */
struct Config
{
    int numCpu = 0;
    SchedulerType scheduler = SchedulerType::FCFS;
    uint32_t quantumCycles = 0;
    uint32_t batchProcessFreq = 0;
    uint32_t minIns = 0;
    uint32_t maxIns = 0;
    uint32_t delayPerExec = 0;

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
