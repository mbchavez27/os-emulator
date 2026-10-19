#include "Config.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

int failures = 0;

void expect(bool condition, const std::string &what)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

void expectError(const std::string &error, const std::string &needle, const std::string &what)
{
    expect(error.find(needle) != std::string::npos, what + " (error was: \"" + error + "\")");
}

std::filesystem::path testDir()
{
    return std::filesystem::temp_directory_path() / "csopesy_config_tests";
}

std::string writeConfig(const std::string &name, const std::string &content)
{
    const std::filesystem::path path = testDir() / name;
    std::ofstream out(path);
    out << content;
    return path.string();
}

const std::string validConfig =
    "num-cpu 4\n"
    "scheduler \"rr\"\n"
    "quantum-cycles 5\n"
    "batch-process-freq 1\n"
    "min-ins 1000\n"
    "max-ins 2000\n"
    "delay-per-exec 0\n";

void testValidConfig()
{
    std::string error;
    const auto config = Config::load(writeConfig("valid.txt", validConfig), error);

    expect(config.has_value(), "valid config loads");
    if (!config)
    {
        return;
    }
    expect(error.empty(), "valid config clears the error");
    expect(config->numCpu == 4, "num-cpu parsed");
    expect(config->scheduler == SchedulerType::RR, "quoted scheduler parsed");
    expect(config->quantumCycles == 5, "quantum-cycles parsed");
    expect(config->batchProcessFreq == 1, "batch-process-freq parsed");
    expect(config->minIns == 1000, "min-ins parsed");
    expect(config->maxIns == 2000, "max-ins parsed");
    expect(config->delayPerExec == 0, "delay-per-exec parsed");
}

void testDelayAlias()
{
    std::string content = validConfig;
    content.replace(content.find("delay-per-exec"), std::string("delay-per-exec").size(), "delays-per-exec");

    std::string error;
    const auto config = Config::load(writeConfig("alias.txt", content), error);

    expect(config.has_value(), "delays-per-exec alias loads");
    if (config)
    {
        expect(config->delayPerExec == 0, "delays-per-exec alias parsed");
    }
}

void testUnknownKeyIgnored()
{
    std::string error;
    const auto config = Config::load(
        writeConfig("unknown.txt", validConfig + "memory-pages 8\n"), error);

    expect(config.has_value(), "unknown key is ignored");
}

void testCrlfLineEndings()
{
    std::string content;
    for (const char c : validConfig)
    {
        content += c == '\n' ? "\r\n" : std::string(1, c);
    }

    std::string error;
    const auto config = Config::load(writeConfig("crlf.txt", content), error);

    expect(config.has_value(), "CRLF config loads");
    if (config)
    {
        expect(config->scheduler == SchedulerType::RR, "CRLF values parsed");
    }
}

void testMissingFile()
{
    std::string error;
    const auto config = Config::load((testDir() / "absent.txt").string(), error);

    expect(!config.has_value(), "missing file fails");
    expectError(error, "Cannot open config file", "missing file names the path");
}

void testMissingKey()
{
    std::string content = validConfig;
    content.replace(content.find("num-cpu 4\n"), std::string("num-cpu 4\n").size(), "");

    std::string error;
    const auto config = Config::load(writeConfig("missing.txt", content), error);

    expect(!config.has_value(), "missing key fails");
    expectError(error, "num-cpu", "missing key names the key");
}

void testNumCpuRange()
{
    for (const std::string &bad : {std::string("0"), std::string("129")})
    {
        std::string content = validConfig;
        content.replace(content.find("num-cpu 4"), std::string("num-cpu 4").size(), "num-cpu " + bad);

        std::string error;
        const auto config = Config::load(writeConfig("numcpu.txt", content), error);

        expect(!config.has_value(), "num-cpu " + bad + " fails");
        expectError(error, "num-cpu", "num-cpu " + bad + " names the key");
        expectError(error, "[1, 128]", "num-cpu " + bad + " reports the range");
    }
}

void testSchedulerValue()
{
    std::string content = validConfig;
    content.replace(content.find("\"rr\""), std::string("\"rr\"").size(), "\"srtf\"");

    std::string error;
    const auto config = Config::load(writeConfig("scheduler.txt", content), error);

    expect(!config.has_value(), "unknown scheduler fails");
    expectError(error, "scheduler", "unknown scheduler names the key");
}

void testQuantumRange()
{
    std::string content = validConfig;
    content.replace(content.find("quantum-cycles 5"), std::string("quantum-cycles 5").size(), "quantum-cycles 0");

    std::string error;
    const auto config = Config::load(writeConfig("quantum.txt", content), error);

    expect(!config.has_value(), "quantum-cycles 0 fails");
    expectError(error, "quantum-cycles", "quantum-cycles error names the key");
}

void testInsOrdering()
{
    std::string content = validConfig;
    content.replace(content.find("min-ins 1000"), std::string("min-ins 1000").size(), "min-ins 3000");

    std::string error;
    const auto config = Config::load(writeConfig("ins-order.txt", content), error);

    expect(!config.has_value(), "min-ins above max-ins fails");
    expectError(error, "must not exceed", "ins ordering explains the constraint");
}

void testNegativeDelay()
{
    std::string content = validConfig;
    content.replace(content.find("delay-per-exec 0"), std::string("delay-per-exec 0").size(), "delay-per-exec -1");

    std::string error;
    const auto config = Config::load(writeConfig("negative.txt", content), error);

    expect(!config.has_value(), "negative delay fails");
    expectError(error, "unsigned integer", "negative delay reports the expected type");
}

void testBoundaries()
{
    std::string content = validConfig;
    content.replace(content.find("num-cpu 4"), std::string("num-cpu 4").size(), "num-cpu 1");
    content.replace(content.find("quantum-cycles 5"), std::string("quantum-cycles 5").size(), "quantum-cycles 4294967295");

    std::string error;
    const auto config = Config::load(writeConfig("boundaries.txt", content), error);

    expect(config.has_value(), "boundary values load");
    if (config)
    {
        expect(config->numCpu == 1, "num-cpu lower bound");
        expect(config->quantumCycles == 4294967295U, "quantum-cycles upper bound");
    }
}

} // namespace

int main()
{
    std::error_code ec;
    std::filesystem::create_directories(testDir(), ec);

    testValidConfig();
    testDelayAlias();
    testUnknownKeyIgnored();
    testCrlfLineEndings();
    testMissingFile();
    testMissingKey();
    testNumCpuRange();
    testSchedulerValue();
    testQuantumRange();
    testInsOrdering();
    testNegativeDelay();
    testBoundaries();

    std::filesystem::remove_all(testDir(), ec);

    if (failures != 0)
    {
        std::cerr << failures << " config test(s) failed\n";
        return 1;
    }

    std::cout << "All config tests passed\n";
    return 0;
}
