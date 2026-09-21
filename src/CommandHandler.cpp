#include "CommandHandler.h"
#include "Emulator.h"
#include "AsciiArt.h"
#include <iostream>
#include <string>
#include <cstdlib>

CommandHandler::CommandHandler(Emulator &e) : emulator(e) {}

void CommandHandler::print_header()
{
    std::vector<std::string> art = ascii_art::convert_to_ascii("CSOPESY");
    for (const auto &line : art)
    {
        std::cout << line << "\n";
    }
    std::cout << "Welcome to CSOPESY Command Line Emulator!\n\n";
}

void CommandHandler::run()
{
    std::string line;

    // Prints header once on startup
    print_header();

    // Runs continously until the 'exit' command is issued
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
        }
        else if (cmd == "screen")
        {
            std::cout << "screen command recognized. Doing something.\n";
        }
        else if (cmd == "scheduler-start")
        {
            std::cout << "scheduler-start command recognized. Doing something.\n";
        }
        else if (cmd == "scheduler-stop")
        {
            std::cout << "scheduler-stop command recognized. Doing something.\n";
        }
        else if (cmd == "report-util")
        {
            std::cout << "report-util command recognized. Doing something.\n";
        }
        else if (cmd == "clear")
        {
// Cross-platform clear screen
#ifdef _WIN32
            system("cls");
#else
            system("clear");
#endif

            // Reprint header after clearing
            print_header();
        }
        else if (cmd == "exit")
        {
            emulator.exit_app();
            break; // Closes the CLI immediately
        }
        else
        {
            std::cout << "Command not recognized. Please try again.\n";
        }
    }
}