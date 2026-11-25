#include "shell/CommandHandler.h"
#include "system/Emulator.h"

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

/**
 * @brief Removes ANSI color codes so checks compare layout, not color.
 *
 * @param text console output that may contain ESC [ ... m sequences
 * @return the text with every color sequence removed
 */
std::string stripColors(const std::string &text)
{
    std::string plain;
    for (std::size_t at = 0; at < text.size(); ++at)
    {
        if (text[at] == '\033' && at + 1 < text.size() && text[at + 1] == '[')
        {
            const std::size_t end = text.find('m', at);
            if (end != std::string::npos)
            {
                at = end;
                continue;
            }
        }
        plain += text[at];
    }
    return plain;
}

/**
 * @brief Runs the console over scripted input.
 *
 * @param emulator the emulator to drive
 * @param input the commands to feed, one per line
 * @return everything the console printed, with colors stripped
 */
std::string drive(Emulator &emulator, const std::string &input)
{
    std::istringstream commands(input);
    std::ostringstream output;
    auto *previousInput = std::cin.rdbuf(commands.rdbuf());
    auto *previousOutput = std::cout.rdbuf(output.rdbuf());
    CommandHandler(emulator).run();
    std::cin.rdbuf(previousInput);
    std::cout.rdbuf(previousOutput);
    return stripColors(output.str());
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
        // trailing EOF skips exitApp() so the scheduler is still running below.
        // scheduler-start is typed inside the screen, where it must be refused.
        const std::string output = drive(
            emulator,
            "initialize\n"
            "screen -s\n"
            "screen -s a\n"
            "screen -s a b\n"
            "screen -s a 256 extra\n"
            "screen -s a -256\n"
            "screen -s a 100\n"
            "screen -s a 32\n"
            "screen -s a 131072\n"
            "screen -s a 99999999999999999999999\n"
            "screen -s myproc 256\n"
            "\n"
            "foo\n"
            "scheduler-start\n"
            "process-smi\n"
            "exit\n"
            "screen -s myproc 256\n");

        // A missing, non-numeric, or trailing argument prints usage.
        check(count(output, "Usage: screen -s <process name> <memory size>") == 5,
              "Malformed screen -s calls should each print usage");
        // A number that is not a power of 2 in [64, 65536] is refused.
        check(count(output, "invalid memory allocation") == 4,
              "Out-of-range or non-power-of-2 sizes should print invalid memory allocation");
        check(named(emulator, "a").empty(), "A rejected screen -s still created the process");
        check(output.find("Process screen: myproc") != std::string::npos,
              "screen -s did not enter the process screen");
        // The main menu shares the root:\> prompt, so look for it right after the attach banner.
        check(output.find("main menu.\n\nroot:\\> ") != std::string::npos, "Process screen prompt is missing");
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

        // process-smi prints the attached process's status, not a placeholder.
        check(output.find("not implemented") == std::string::npos,
              "process-smi is still a placeholder");
        check(output.find("root:\\> \nProcess name: myproc\nID: 1\nLogs:\n") != std::string::npos,
              "process-smi did not print a blank line, then the name, ID, and Logs header");
        check(output.find("Lines of code: ") != std::string::npos ||
                  output.find("Finished!") != std::string::npos,
              "process-smi did not print a progress block or Finished!");

        // Header is intact after the detach: once at startup, once after 'exit'.
        check(count(output, "Welcome to CSOPESY Emulator!") == 2,
              "Header was not reprinted after detaching");
        check(emulator.scheduler().isRunning(), "Attaching must start the CPU threads");

        // EOF inside the process screen detaches instead of terminating. The
        // stream is already at EOF, so the main loop then ends without exitApp().
        const std::string eofOutput = drive(emulator, "screen -s eofproc 64\n");
        check(eofOutput.find("Process screen: eofproc") != std::string::npos,
              "EOF case did not attach");
        check(count(eofOutput, "Welcome to CSOPESY Emulator!") == 2,
              "EOF inside the process screen did not detach to the main menu");
        check(emulator.isAlive(), "EOF inside the process screen terminated the emulator");
        check(named(emulator, "eofproc").size() == 1, "The process was lost on EOF detach");

        // Anything that is not -s, -r, or a bare -ls prints the full usage.
        const std::string usageOutput = drive(emulator, "screen\nscreen -x\nscreen -ls extra\n");
        check(count(usageOutput, "Usage:\n  screen -s <process name> <memory size>") == 3,
              "Bare or malformed screen did not print the usage");
        check(usageOutput.find("Doing something") == std::string::npos,
              "screen still prints the placeholder");

        const auto created = named(emulator, "myproc");
        check(created.size() == 1, "screen -s must create exactly one process with that name");
        check(created.front().memoryBytes == 256 || created.front().state == ProcessState::FINISHED,
              "screen -s myproc 256 did not allocate 256 bytes");
        check(named(emulator, "eofproc").front().memoryBytes == 64 ||
                  named(emulator, "eofproc").front().state == ProcessState::FINISHED,
              "screen -s eofproc 64 did not allocate 64 bytes");

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
        const auto first = emulator.scheduler().createProcess("manual", 512, emulator.config());
        check(first.has_value(), "createProcess rejected an unused name");
        check(!emulator.scheduler().createProcess("manual", 512, emulator.config()),
              "createProcess accepted a duplicate name");
        const auto second = emulator.scheduler().createProcess("manual2", 512, emulator.config());
        check(second.has_value() && *second != *first, "Manual processes reused an id");

        // createProcess enforces the size rule itself, not only the shell.
        bool rejected = false;
        try
        {
            emulator.scheduler().createProcess("bad-size", 100, emulator.config());
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "createProcess accepted a non-power-of-2 memory size");
        check(named(emulator, "bad-size").empty(), "A rejected memory size still created the process");

        emulator.exitApp();
        std::cout << "Process screen checks passed.\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
