#pragma once

#include <cstdint>
#include <optional>
#include <string>

/** @brief The CPU scheduling algorithm named by config.txt's `scheduler` key. */
enum class SchedulerType
{
    FCFS, /**< First come, first served: a process keeps its core until it finishes or sleeps. */
    RR    /**< Round robin: a process is requeued when its quantum expires. */
};

/**
 * @brief Emulator configuration loaded from config.txt.
 *
 * load() validates every value before returning a Config. The console never
 * changes it afterwards: Emulator keeps it private and hands out only a const
 * reference. Fields stay plain so tests can build configs directly; Scheduler
 * re-checks the values it depends on (quantum, frequency, instruction bounds).
 *
 * The numeric fields are 64-bit so the spec's inclusive upper bound of 2^32 fits.
 */
struct Config
{
    int numCpu = 0;                                /**< `num-cpu`, [1, 128]. */
    SchedulerType scheduler = SchedulerType::FCFS; /**< `scheduler`, "fcfs" or "rr". */
    uint64_t quantumCycles = 0;                    /**< `quantum-cycles`, [1, 2^32]; RR only. */
    uint64_t batchProcessFreq = 0;                 /**< `batch-process-freq`, [1, 2^32] ticks. */
    uint64_t minIns = 0;                           /**< `min-ins`, [1, 2^32]. */
    uint64_t maxIns = 0;                           /**< `max-ins`, [1, 2^32]; at least minIns. */
    uint64_t delayPerExec = 0;                     /**< `delay-per-exec`, [0, 2^32] ticks. */

    // Memory keys: powers of 2 in [2^6, 2^16] bytes. Defaults are valid so
    // hand-built configs in checks get a working memory manager.
    uint64_t maxOverallMem = 16384; /**< `max-overall-mem`: physical memory in bytes. */
    uint64_t memPerFrame = 16;      /**< `mem-per-frame`: frame size, also the page size. */
    uint64_t minMemPerProc = 4096;  /**< `min-mem-per-proc`: smallest rolled process size. */
    uint64_t maxMemPerProc = 4096;  /**< `max-mem-per-proc`: largest rolled process size. */

    /**
     * @brief Parses and validates a space-separated config file.
     *
     * Unknown keys are ignored so extra lines do not break loading. Both
     * `delay-per-exec` (mockup spelling) and `delays-per-exec` (spec text)
     * are accepted.
     *
     * @param path the file to read, usually "config.txt"
     * @param error set to a message naming the first invalid key and its value
     * @return the config, or std::nullopt if the file is missing or invalid
     */
    static std::optional<Config> load(const std::string &path, std::string &error);
};
