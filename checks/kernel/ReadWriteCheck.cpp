#include "config/Config.h"
#include "kernel/Scheduler.h"

#include "../FlatMemory.h"

#include <algorithm>
#include <chrono>
#include <functional>
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

void runToEnd(Process &process, ProcessMemory &memory)
{
    while (process.step(memory) != StepOutcome::FINISHED)
    {
        while (process.isSleeping())
        {
            process.tickSleep();
        }
    }
}

Instruction read(const std::string &variable, std::uint64_t address)
{
    return Instruction{ReadInstruction{variable, address}};
}

Instruction write(std::uint64_t address, Operand value)
{
    return Instruction{WriteInstruction{address, std::move(value)}};
}

Instruction print(const std::string &text, const std::string &variable)
{
    return Instruction{PrintInstruction{text, variable}};
}

/** The spec's examples: WRITE then READ returns the value; unwritten memory reads 0. */
void checkRoundTrip()
{
    FlatMemory memory(16384);
    Process process(1, "rw", Program{{
        write(0x2000, Operand::literal(42)),
        read("var", 0x2000),
        read("fresh", 0x1000),
        Instruction{DeclareInstruction{"varA", 7}},
        write(0x500, Operand::variable("varA")),
        read("copy", 0x500),
        write(0x500, Operand::literal(65535)),
        read("copy2", 0x500),
        print("var=", "var"),
        print("fresh=", "fresh"),
        print("copy=", "copy"),
        print("copy2=", "copy2")}});
    runToEnd(process, memory);
    check(process.logs() ==
              std::vector<std::string>{"var=42", "fresh=0", "copy=7", "copy2=65535"},
          "READ must return what WRITE stored, and 0 for memory never written");
    check(memory.read16(0x500) == 65535 && memory.read16(0x2000) == 42,
          "WRITE must store little-endian uint16 values at the address");
}

/** READ into a new variable declares it in the symbol table, like DECLARE. */
void checkReadDeclares()
{
    FlatMemory memory(256);
    memory.write16(0x80, 1234);
    Process process(2, "declare", Program{{read("x", 0x80), Instruction{SleepInstruction{1}}}});
    process.step(memory);
    check(process.symbolOffset("x") == 0u && memory.read16(0) == 1234,
          "READ must store the value in the variable's symbol-table slot");
    check(process.variables().at("x") == 1234, "READ must update the variable");
}

/** Rendering matches the spec's hex address form. */
void checkFormatting()
{
    const Program program{{read("var", 0x1000), write(0x2000, Operand::literal(42)),
                           write(0x500, Operand::variable("varA"))}};
    check(program.toString() == "READ (var, 0x1000)\nWRITE (0x2000, 42)\nWRITE (0x500, varA)\n",
          "READ / WRITE must render with hex addresses");
    check(opOf(program.instructions[0]) == Op::READ && opOf(program.instructions[1]) == Op::WRITE,
          "opOf must recognize READ and WRITE");
    check(formatAddress(0xABCD) == "0xABCD", "Addresses print as uppercase hex");
}

void forEachInstruction(const std::vector<Instruction> &instructions,
                        const std::function<void(const Instruction &)> &visit)
{
    for (const Instruction &instruction : instructions)
    {
        visit(instruction);
        if (const auto *loop = std::get_if<ForInstruction>(&instruction.payload))
        {
            forEachInstruction(loop->body, visit);
        }
    }
}

/** Generated addresses are even, past the symbol table, and inside the process's memory. */
void checkGeneratedAddresses()
{
    Config config;
    config.minIns = 40;
    config.maxIns = 120;
    for (const std::uint64_t size : {64ULL, 128ULL, 256ULL, 4096ULL, 65536ULL})
    {
        std::size_t accesses = 0;
        for (std::uint32_t seed = 1; seed <= 100; ++seed)
        {
            const Program program = generateProgram(config, "gen", seed, size);
            check(program.instructionCount() == generateProgram(config, "gen", seed, 128).instructionCount(),
                  "Memory size must not change the instruction count");
            forEachInstruction(program.instructions, [&](const Instruction &instruction) {
                std::optional<std::uint64_t> address;
                if (const auto *r = std::get_if<ReadInstruction>(&instruction.payload))
                {
                    address = r->address;
                }
                else if (const auto *w = std::get_if<WriteInstruction>(&instruction.payload))
                {
                    address = w->address;
                }
                if (address)
                {
                    ++accesses;
                    check(*address % 2 == 0, "Generated address must be even");
                    check(*address >= Process::kSymbolTableBytes, "Generated address hit the symbol table");
                    check(*address + 2 <= size, "Generated address is out of range");
                }
            });
        }
        check(size == 64 ? accesses == 0 : accesses > 0,
              size == 64 ? "A 64-byte process has no room for READ / WRITE"
                         : "Generator never produced READ / WRITE");
    }
}

/**
 * One 64-byte frame shared by two processes: a written page is evicted to the
 * backing store before it is read back, and the value must survive.
 */
void checkSurvivesEviction()
{
    Config config;
    config.numCpu = 1;
    config.scheduler = SchedulerType::RR;
    config.quantumCycles = 1;
    config.maxOverallMem = 64;
    config.memPerFrame = 64;
    config.minMemPerProc = 256;
    config.maxMemPerProc = 256;

    Program program;
    program.instructions.push_back(write(0xC0, Operand::literal(4321)));
    for (int i = 0; i < 12; ++i)
    {
        program.instructions.push_back(Instruction{PrintInstruction{"tick", std::nullopt}});
    }
    program.instructions.push_back(read("r", 0xC0));
    program.instructions.push_back(print("r=", "r"));

    Scheduler scheduler;
    scheduler.start(config); // Memory is configured from the first start() or submission.
    scheduler.submitProcess(std::make_unique<Process>(1, "first", program));
    scheduler.submitProcess(std::make_unique<Process>(2, "second", program));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    SystemSnapshot system;
    do
    {
        system = scheduler.systemSnapshot();
        if (std::all_of(system.processes.begin(), system.processes.end(),
                        [](const ProcessView &view) { return view.state == ProcessState::FINISHED; }))
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    } while (std::chrono::steady_clock::now() < deadline);
    scheduler.stop();

    for (const ProcessView &view : system.processes)
    {
        check(view.state == ProcessState::FINISHED, "Timed out waiting for the eviction workload");
        check(!view.logs.empty() && view.logs.back() == "r=4321", "A written value did not survive eviction");
    }
    check(system.memory.pagedOut > 0, "The workload never evicted a page");
    check(system.memory.corruptReads == 0, "A written page came back corrupted");
}

/** Generated programs with READ / WRITE run to completion under the scheduler. */
void checkGeneratedRun()
{
    Config config;
    config.numCpu = 2;
    config.scheduler = SchedulerType::RR;
    config.quantumCycles = 4;
    config.minIns = 60;
    config.maxIns = 60;
    config.maxOverallMem = 256;
    config.memPerFrame = 64;

    Scheduler scheduler;
    scheduler.start(config);
    for (const std::uint64_t size : {128ULL, 256ULL, 1024ULL})
    {
        check(scheduler.createProcess("rw" + std::to_string(size), size, config).has_value(),
              "createProcess failed");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool finished = false;
    while (!finished && std::chrono::steady_clock::now() < deadline)
    {
        const auto views = scheduler.snapshot();
        finished = std::all_of(views.begin(), views.end(),
                               [](const ProcessView &view) { return view.state == ProcessState::FINISHED; });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const MemoryStats stats = scheduler.systemSnapshot().memory;
    scheduler.stop();
    check(finished, "Generated READ / WRITE programs did not finish");
    check(stats.corruptReads == 0 && stats.usedBytes == 0, "Finished processes must leave memory clean");
}
} // namespace

int main()
{
    try
    {
        checkRoundTrip();
        checkReadDeclares();
        checkFormatting();
        checkGeneratedAddresses();
        checkSurvivesEviction();
        checkGeneratedRun();
        std::cout << "READ / WRITE checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
