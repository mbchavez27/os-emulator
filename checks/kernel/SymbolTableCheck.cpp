#include "kernel/Scheduler.h"

#include "../FlatMemory.h"

#include <algorithm>
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

Instruction declare(const std::string &name, std::uint16_t value)
{
    return Instruction{DeclareInstruction{name, value}};
}

Instruction print(const std::string &text, const std::string &variable)
{
    return Instruction{PrintInstruction{text, variable}};
}

/** Variables land 2 bytes apart, little-endian, from offset 0. */
void checkLayout()
{
    FlatMemory memory(Process::kSymbolTableBytes);
    Process process(1, "layout", Program{{declare("a", 0x1234), declare("b", 7),
                                          Instruction{AddInstruction{"c", Operand::variable("a"),
                                                                     Operand::literal(1)}},
                                          print("", "c")}});
    for (int i = 0; i < 3; ++i)
    {
        process.step(memory);
    }
    const auto &bytes = memory.bytes();
    check(bytes[0] == 0x34 && bytes[1] == 0x12, "First variable must sit at offset 0, little-endian");
    check(bytes[2] == 7 && bytes[3] == 0, "Second variable must sit at offset 2");
    check(bytes[4] == 0x35 && bytes[5] == 0x12, "ADD target must be declared at offset 4");
    check(process.symbolOffset("a") == 0u && process.symbolOffset("b") == 2u && process.symbolOffset("c") == 4u,
          "Symbol offsets must follow declaration order");
    check(process.variables().at("c") == 0x1235, "Display copy must track the stored value");
}

/** The symbol table holds 32 uint16 variables; the 33rd and later are ignored. */
void checkCapacity()
{
    Program program;
    for (std::size_t i = 0; i < Process::kMaxVariables; ++i)
    {
        program.instructions.push_back(declare("v" + std::to_string(i), static_cast<std::uint16_t>(i + 100)));
    }
    program.instructions.push_back(declare("overflow", 9));
    program.instructions.push_back(Instruction{AddInstruction{"sum", Operand::literal(1), Operand::literal(2)}});
    program.instructions.push_back(print("overflow=", "overflow"));
    program.instructions.push_back(print("sum=", "sum"));
    program.instructions.push_back(declare("v0", 5)); // Redeclaring an existing name still works.
    program.instructions.push_back(print("v0=", "v0"));
    program.instructions.push_back(print("v31=", "v31"));
    program.instructions.push_back(Instruction{SleepInstruction{1}});

    FlatMemory memory(Process::kSymbolTableBytes);
    Process process(2, "capacity", program);
    while (!process.isSleeping())
    {
        process.step(memory);
    }
    check(process.variables().size() == Process::kMaxVariables, "Symbol table must hold exactly 32 variables");
    check(!process.symbolOffset("overflow") && !process.symbolOffset("sum"),
          "The 33rd declaration must be ignored");
    check(process.logs() == std::vector<std::string>{"overflow=0", "sum=0", "v0=5", "v31=131"},
          "Ignored variables must read 0 and declared ones must keep their values");
    for (std::size_t i = 1; i < Process::kMaxVariables; ++i)
    {
        check(memory.read16(i * 2) == i + 100, "Ignored declarations must not overwrite the table");
    }

    // Variables stay while the process sleeps and are dropped when it finishes.
    check(process.variables().at("v1") == 101, "Variables must survive SLEEP");
    process.tickSleep();
    check(process.isFinished() && process.variables().empty() && !process.symbolOffset("v1"),
          "Finishing must release the symbol table");
}

/** Values are clamped to [0, 65535]. */
void checkClamping()
{
    FlatMemory memory(Process::kSymbolTableBytes);
    Process process(3, "clamp", Program{{
        declare("x", 65530),
        Instruction{AddInstruction{"x", Operand::variable("x"), Operand::literal(100)}},
        Instruction{SubtractInstruction{"y", Operand::literal(3), Operand::literal(10)}},
        print("x=", "x"),
        print("y=", "y")}});
    runToEnd(process, memory);
    check(process.logs() == std::vector<std::string>{"x=65535", "y=0"}, "Results must clamp to [0, 65535]");
}

/**
 * One 64-byte frame shared by two processes: every instruction-page touch
 * evicts the other page, so the symbol table round-trips through the backing
 * store and must come back intact.
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
    program.instructions.push_back(declare("x", 40));
    program.instructions.push_back(declare("y", 2));
    for (int i = 0; i < 12; ++i)
    {
        program.instructions.push_back(Instruction{PrintInstruction{"tick", std::nullopt}});
    }
    program.instructions.push_back(Instruction{AddInstruction{"x", Operand::variable("x"), Operand::variable("y")}});
    program.instructions.push_back(print("x=", "x"));

    // Start first: memory is configured from the first start() or submission.
    Scheduler scheduler;
    scheduler.start(config);
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

    check(system.processes.size() == 2, "Both processes must exist");
    for (const ProcessView &view : system.processes)
    {
        check(view.state == ProcessState::FINISHED, "Timed out waiting for the eviction workload");
        check(!view.logs.empty() && view.logs.back() == "x=42", "A variable did not survive eviction");
    }
    check(system.memory.pagedOut > 0, "The workload never evicted a page");
    check(system.memory.corruptReads == 0, "A symbol table page came back corrupted");
}
} // namespace

int main()
{
    try
    {
        checkLayout();
        checkCapacity();
        checkClamping();
        checkSurvivesEviction();
        std::cout << "Symbol table checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
