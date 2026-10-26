#include "CommandHandler.h"
#include "Emulator.h"
#include "AsciiArt.h"
#include "ConsoleColors.h"
#include "ProcessReport.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

CommandHandler::CommandHandler(Emulator &e) : emulator(e) {}

void CommandHandler::print_header()
{
    // Print the ASCII Art
    std::vector<std::string> art = ascii_art::convert_to_ascii("CSOPESY");
    for (const auto &line : art)
    {
        std::cout << line << "\n";
    }

    // Print the colored greeting text
    std::cout << console_colors::GREEN << "Hello, Welcome to CSOPESY commandline!" << console_colors::RESET << "\n";
    std::cout << console_colors::YELLOW << "Type 'exit' to quit, 'clear' to clear the screen\n\n";
    std::cout << "** IMPORTANT: Type 'initialize' to load config and start system **" << console_colors::RESET << "\n\n";
}

void CommandHandler::clear_screen()
{
    // Deliberate system() call: no portable C++ API for console clear
#ifdef _WIN32
    if (std::system("cls") != 0)
    {
        std::cout << "Failed to clear screen.\n";
    }
#else
    if (std::system("clear") != 0)
    {
        std::cout << "Failed to clear screen.\n";
    }
#endif
}

void CommandHandler::run_process_screen(const std::string &name)
{
    clear_screen();
    std::cout << "Process screen: " << name << "\n"
              << "Type 'process-smi' for process information, 'exit' to return to the main menu.\n\n";

    std::string line;
    while (emulator.is_app_alive)
    {
        std::cout << "root:\\> " << std::flush;

        if (!std::getline(std::cin, line))
        {
            // Detach on EOF; clearing the stream state lets an interactive
            // console (Ctrl+Z) keep using the main menu. A closed pipe fails
            // the main loop's next read, which ends the emulator as before.
            std::cin.clear();
            std::cout << "\n";
            break;
        }

        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
        {
            continue; // Ignore empty inputs
        }
        const size_t last = line.find_last_not_of(" \t\r");
        const std::string input = line.substr(first, last - first + 1);

        if (input == "exit")
        {
            break;
        }
        else if (input == "process-smi")
        {
            // The status view is a separate ticket; the command is accepted here.
            std::cout << "process-smi output is not implemented yet.\n\n";
        }
        else
        {
            std::cout << "Unknown command '" << input
                      << "'. Available commands: process-smi, exit\n\n";
        }
    }

    // Detach: the process stays in the scheduler and keeps running.
    clear_screen();
    print_header();
}

void CommandHandler::run()
{
    std::string line;

    // Prints header once on startup
    print_header();

    // Loop until `exit` flips Emulator::is_app_alive to false
    while (emulator.is_app_alive)
    {
        std::cout << "Enter a command: " << std::flush;

        if (!std::getline(std::cin, line))
        {
            break;
        }

        // Trim leading whitespace
        size_t first_non_space = line.find_first_not_of(" \t");
        if (first_non_space == std::string::npos)
        {
            continue; // Ignore empty inputs
        }

        // Extract command (ignoring trailing spaces or arguments)
        size_t space_pos = line.find_first_of(" \t", first_non_space);
        std::string cmd = line.substr(first_non_space, space_pos - first_non_space);

        // Process commands
        // initialize and exit are the only commands valid before initialization.
        if (cmd == "initialize")
        {
            if (emulator.isInitialized())
            {
                std::cout << "System is already initialized.\n\n";
            }
            else
            {
                std::string error;
                if (emulator.initialize("config.txt", error))
                {
                    std::cout << "Initialization successful.\n\n";
                }
                else
                {
                    std::cout << error << "\n\n";
                }
            }
        }
        else if (cmd == "exit")
        {
            emulator.exit_app();
            break; // Closes the CLI immediately
        }
        else if (!emulator.isInitialized())
        {
            std::cout << "System not yet initialized. Type 'initialize' to load config.\n\n";
        }
        else if (cmd == "screen")
        {
            std::istringstream arguments(line.substr(first_non_space));
            std::string subcommand;
            std::string processName;
            std::string extra;
            arguments >> cmd >> subcommand;
            if (subcommand == "-ls" && !(arguments >> extra))
            {
                std::cout << buildProcessReport({
                    static_cast<std::size_t>(emulator.config().numCpu),
                    emulator.scheduler().snapshot()});
            }
            else if (subcommand == "-s")
            {
                if (!(arguments >> processName) || (arguments >> extra))
                {
                    std::cout << "Usage: screen -s <process name>\n\n";
                    continue;
                }

                // Processes only execute while the CPU threads run; start() is a no-op if they do.
                emulator.scheduler().start(emulator.config());
                if (!emulator.scheduler().createProcess(processName, emulator.config()))
                {
                    std::cout << "Process " << processName << " already exists.\n\n";
                    continue;
                }
                run_process_screen(processName);
            }
            else
            {
                std::cout << "screen command recognized. Doing something.\n\n";
            }
        }
        else if (cmd == "scheduler-start")
        {
            if (emulator.scheduler().startGeneration(emulator.config()))
            {
                std::cout << "Process generation started.\n\n";
            }
            else
            {
                std::cout << "Process generation is already running.\n\n";
            }
        }
        else if (cmd == "scheduler-stop")
        {
            if (emulator.scheduler().stopGeneration())
            {
                std::cout << "Process generation stopped. Existing processes will continue running.\n\n";
            }
            else
            {
                std::cout << "Process generation is not running.\n\n";
            }
        }
        else if (cmd == "report-util")
        {
            const std::string report = buildProcessReport({
                static_cast<std::size_t>(emulator.config().numCpu),
                emulator.scheduler().snapshot()});
            std::ofstream file("csopesy-log.txt");
            file << report;
            file.close();
            if (file)
            {
                std::cout << "Report saved to csopesy-log.txt.\n\n";
            }
            else
            {
                std::cout << "Failed to write csopesy-log.txt.\n\n";
            }
        }
        else if (cmd == "clear")
        {
            clear_screen();
            print_header();
        }
        else
        {
            std::cout << "Command not recognized. Please try again.\n";
            std::cout << "\n";
        }
    }
}
