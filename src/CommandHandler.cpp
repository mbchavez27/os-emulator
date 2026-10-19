#include "CommandHandler.h"
#include "Emulator.h"
#include "Scheduler.h"
#include "AsciiArt.h"
#include "ConsoleColors.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

CommandHandler::CommandHandler(Emulator &e, Scheduler &s) : emulator(e), scheduler(s) {}

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
        if (cmd == "initialize")
        {
            std::cout << "initialize command recognized. Doing something.\n";
            std::cout << "\n";
        }
        else if (cmd == "screen")
        {
            // Support `screen -ls`. First token is `screen`, args follow.
            std::string args;
            if (space_pos != std::string::npos)
            {
                size_t arg_start = line.find_first_not_of(" \t", space_pos);
                if (arg_start != std::string::npos)
                {
                    args = line.substr(arg_start);
                }
            }
            if (args.rfind("-ls", 0) == 0)
            {
                auto procs = scheduler.snapshot();
                std::cout << "--------------------------------------------------\n";
                std::cout << "Running processes:\n";
                for (const auto &p : procs)
                {
                    if (p->state == ProcState::Running)
                    {
                        std::cout << p->name << "  (" << Scheduler::formatTime(p->start) << ")  Core: " << p->core
                                  << "  " << p->done << " / " << p->total << "\n";
                    }
                }
                std::cout << "\nFinished processes:\n";
                for (const auto &p : procs)
                {
                    if (p->state == ProcState::Finished)
                    {
                        std::cout << p->name << "  (" << Scheduler::formatTime(p->start) << ")  Finished  " << p->done << " / " << p->total << "\n";
                    }
                }
                std::cout << "--------------------------------------------------\n\n";
            }
            else
            {
                std::cout << "screen command recognized. Doing something.\n\n";
            }
        }
        else if (cmd == "scheduler-start")
        {
            scheduler.start();
            std::cout << "Scheduler started.\n\n";
        }
        else if (cmd == "scheduler-stop")
        {
            scheduler.stop();
            std::cout << "Scheduler stopped.\n\n";
        }
        else if (cmd == "report-util")
        {
            std::cout << "report-util command recognized. Doing something.\n";
            std::cout << "\n";
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
        else if (cmd == "exit")
        {
            scheduler.stop();
            emulator.exit_app();
            break; // Closes the CLI immediately
        }
        else
        {
            std::cout << "Command not recognized. Please try again.\n";
            std::cout << "\n";
        }
    }
}
