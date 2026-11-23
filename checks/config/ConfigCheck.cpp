#include "config/Config.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void check(bool condition, const std::string &message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

const char *const kScheduling = "num-cpu 4\n"
                                "scheduler \"rr\"\n"
                                "quantum-cycles 5\n"
                                "batch-process-freq 1\n"
                                "min-ins 100\n"
                                "max-ins 100\n"
                                "delay-per-exec 0\n";

/** @brief Loads a config made of the scheduling keys plus the given memory lines. */
std::optional<Config> loadWith(const std::string &memoryLines, std::string &error)
{
    const std::string path = "config-check.txt";
    {
        std::ofstream file(path);
        file << kScheduling << memoryLines;
    }
    auto config = Config::load(path, error);
    std::remove(path.c_str());
    return config;
}

std::string memory(const std::string &total, const std::string &frame, const std::string &min, const std::string &max)
{
    return "max-overall-mem " + total + "\nmem-per-frame " + frame + "\nmin-mem-per-proc " + min +
           "\nmax-mem-per-proc " + max + "\n";
}

void expectRejected(const std::string &memoryLines, const std::string &expectedError)
{
    std::string error;
    check(!loadWith(memoryLines, error), "Accepted invalid memory config: " + expectedError);
    check(error == expectedError, "Wrong error. Expected: " + expectedError + "\nGot: " + error);
}
} // namespace

int main()
{
    try
    {
        // The shipped config.txt must initialize; it once had an out-of-range mem-per-frame.
        std::string error;
        const auto shipped = Config::load(SOURCE_CONFIG_PATH, error);
        check(shipped.has_value(), "The repository's config.txt does not load: " + error);

        // Struct defaults (used by hand-built configs in checks) must satisfy the same rules.
        const Config defaults;
        for (const std::uint64_t value :
             {defaults.maxOverallMem, defaults.memPerFrame, defaults.minMemPerProc, defaults.maxMemPerProc})
        {
            check(value >= 64 && value <= 65536 && (value & (value - 1)) == 0,
                  "Config defaults must be powers of 2 in [64, 65536]");
        }
        check(defaults.memPerFrame <= defaults.maxOverallMem && defaults.minMemPerProc <= defaults.maxMemPerProc,
              "Config defaults must satisfy the memory relations");

        // Valid: the bounds themselves, and min == max.
        auto config = loadWith(memory("65536", "64", "64", "65536"), error);
        check(config && config->maxOverallMem == 65536 && config->memPerFrame == 64 &&
                  config->minMemPerProc == 64 && config->maxMemPerProc == 65536,
              "Valid bounds were rejected: " + error);
        check(loadWith(memory("1024", "1024", "256", "256"), error).has_value(),
              "A frame as large as memory and min == max must load: " + error);

        // Range and power of 2.
        expectRejected(memory("16384", "16", "4096", "4096"),
                       "Config key 'mem-per-frame' must be in [64, 65536], got '16'.");
        expectRejected(memory("131072", "64", "64", "64"),
                       "Config key 'max-overall-mem' must be in [64, 65536], got '131072'.");
        expectRejected(memory("1024", "64", "96", "128"),
                       "Config key 'min-mem-per-proc' must be a power of 2, got '96'.");
        expectRejected(memory("1024", "64", "64", "x"),
                       "Config key 'max-mem-per-proc' expects an unsigned integer, got 'x'.");

        // Relations between keys.
        expectRejected(memory("64", "128", "64", "64"),
                       "'mem-per-frame' (128) must not exceed 'max-overall-mem' (64).");
        expectRejected(memory("1024", "64", "512", "256"),
                       "'min-mem-per-proc' (512) must not exceed 'max-mem-per-proc' (256).");

        // Every memory key is required.
        expectRejected("max-overall-mem 1024\nmem-per-frame 64\nmin-mem-per-proc 64\n",
                       "Missing required config key: 'max-mem-per-proc'.");

        std::cout << "Config checks passed.\n";
    }
    catch (const std::exception &failure)
    {
        std::cerr << failure.what() << "\n";
        return 1;
    }
}
