#include "CommandHandler.h"
#include "Emulator.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

std::size_t count(const std::string &text, const std::string &needle)
{
    std::size_t total = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1))
    {
        ++total;
    }
    return total;
}

/// Runs the handler over `input`, returning everything it printed.
std::string drive(Emulator &emulator, const std::string &input)
{
    std::istringstream commands(input);
    std::ostringstream output;
    auto *previousInput = std::cin.rdbuf(commands.rdbuf());
    auto *previousOutput = std::cout.rdbuf(output.rdbuf());
    CommandHandler(emulator).run();
    std::cin.rdbuf(previousInput);
    std::cout.rdbuf(previousOutput);
    return output.str();
}

std::vector<ProcessView> named(Emulator &emulator, const std::string &name)
{
    auto views = emulator.scheduler().snapshot();
    views.erase(std::remove_if(views.begin(), views.end(),
                               [&](const ProcessView &view) { return view.name != name; }),
                views.end());
    return views;
}
} // namespace

int main()
{
    try
    {
        Emulator emulator;

        // Attach, reject bad input, detach, then try the same name again. The
        // trailing EOF skips exit_app() so the scheduler is still running below.
        // scheduler-start is typed inside the screen, where it must be refused.
        const std::string output = drive(
            emulator,
            "initialize\n"
            "screen -s\n"
            "screen -s a b\n"
            "screen -s myproc\n"
            "\n"
            "foo\n"
            "scheduler-start\n"
            "process-smi\n"
            "exit\n"
            "screen -s myproc\n");

        check(output.find("Usage: screen -s <process name>") != std::string::npos,
              "Missing or extra process name was not rejected");
        check(count(output, "Usage: screen -s <process name>") == 2,
              "Both malformed screen -s calls should print usage");
        check(output.find("Process screen: myproc") != std::string::npos,
              "screen -s did not enter the process screen");
        check(output.find("root:\\> ") != std::string::npos, "Process screen prompt is missing");
        check(output.find("Unknown command 'foo'. Available commands: process-smi, exit") !=
                  std::string::npos,
              "Unknown input was not reported");
        check(output.find("Process screen: myproc") == output.rfind("Process screen: myproc"),
              "A duplicate name entered a second process screen");
        check(output.find("Process myproc already exists.") != std::string::npos,
              "Duplicate name was not rejected with a message");

        // Main-menu commands are not available inside the process screen.
        check(output.find("Process generation started.") == std::string::npos,
              "scheduler-start ran inside the process screen");

        check(output.find("Unknown command 'scheduler-start'") != std::string::npos,
              "scheduler-start should be unknown inside the process screen");

        // Header is intact after the detach: once at startup, once after 'exit'.
        check(count(output, "Hello, Welcome to CSOPESY commandline!") == 2,
              "Header was not reprinted after detaching");
        check(emulator.scheduler().isRunning(), "Attaching must start the CPU threads");

        // EOF inside the process screen detaches instead of terminating. The
        // stream is already at EOF, so the main loop then ends without exit_app().
        const std::string eofOutput = drive(emulator, "screen -s eofproc\n");
        check(eofOutput.find("Process screen: eofproc") != std::string::npos,
              "EOF case did not attach");
        check(count(eofOutput, "Hello, Welcome to CSOPESY commandline!") == 2,
              "EOF inside the process screen did not detach to the main menu");
        check(emulator.is_app_alive, "EOF inside the process screen terminated the emulator");
        check(named(emulator, "eofproc").size() == 1, "The process was lost on EOF detach");

        const auto created = named(emulator, "myproc");
        check(created.size() == 1, "screen -s must create exactly one process with that name");

        // The process keeps executing after detaching.
        const std::size_t before = created.front().currentLine;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::size_t after = before;
        while (after <= before && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const auto views = named(emulator, "myproc");
            check(views.size() == 1, "The process vanished after detaching");
            after = views.front().currentLine;
        }
        check(after > before, "The process stopped executing after detaching");

        // Generated and manual ids share one counter, so names stay unique.
        const auto first = emulator.scheduler().createProcess("manual", emulator.config());
        check(first.has_value(), "createProcess rejected an unused name");
        check(!emulator.scheduler().createProcess("manual", emulator.config()),
              "createProcess accepted a duplicate name");
        const auto second = emulator.scheduler().createProcess("manual2", emulator.config());
        check(second.has_value() && *second != *first, "Manual processes reused an id");

        emulator.exit_app();
        std::cout << "Process screen checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
