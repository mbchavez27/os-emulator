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
            std::string extra;
            arguments >> cmd >> subcommand;
            if (subcommand == "-ls" && !(arguments >> extra))
            {
                std::cout << buildProcessReport({
                    static_cast<std::size_t>(emulator.config().numCpu),
                    emulator.scheduler().snapshot()});
            }
            else
            {
                std::cout << "screen command recognized. Doing something.\n\n";
            }
        }
        else if (cmd == "scheduler-start")
        {
            if (emulator.scheduler().isRunning())
            {
                std::cout << "Scheduler already running.\n\n";
            }
            else
            {
                emulator.scheduler().start(emulator.config());
                std::cout << "Scheduler started.\n\n";
            }
        }
        else if (cmd == "scheduler-stop")
        {
            if (!emulator.scheduler().isRunning())
            {
                std::cout << "Scheduler is not running.\n\n";
            }
            else
            {
                emulator.scheduler().stop();
                std::cout << "Scheduler stopped after "
                          << emulator.scheduler().cycleCount() << " CPU cycles.\n\n";
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

            print_header();
        }
        else
        {
            std::cout << "Command not recognized. Please try again.\n";
            std::cout << "\n";
        }
    }
}
