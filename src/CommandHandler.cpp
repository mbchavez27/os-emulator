#include "CommandHandler.h"
#include "Emulator.h"
#include "AsciiArt.h"
#include "ConsoleColors.h"
#include "ProcessReport.h"
#include "ProcessSmi.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

CommandHandler::CommandHandler(Emulator &e) : emulator(e) {}

void CommandHandler::printHeader()
{
    for (const auto &line : ascii_art::convertToAscii("CSOPESY"))
    {
        std::cout << line << "\n";
    }

    // Banner layout follows the spec's main-menu mockup.
    std::cout << "--------------------------------------------\n"
              << console_colors::GREEN << "Welcome to CSOPESY Emulator!" << console_colors::RESET << "\n"
              << "\n"
                 "Developers:\n"
                 "Chavez, Max Benedict B.\n"
                 "Leano, Jeremy L.\n"
                 "Go, Timothy Aaron S.\n"
                 "De La Calzada, Wanda\n"
                 "--------------------------------------------\n"
                 "\n";
}

void CommandHandler::clearScreen()
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

void CommandHandler::runProcessScreen(const std::string &name)
{
    clearScreen();
    std::cout << "Process screen: " << name << "\n"
              << "Type 'process-smi' for process information, 'exit' to return to the main menu.\n\n";

    std::string line;
    while (emulator.isAlive())
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
            // Copy the state under the scheduler lock, then print with no lock held.
            const auto views = emulator.scheduler().snapshot();
            const auto view = std::find_if(views.begin(), views.end(), [&](const ProcessView &candidate) {
                return candidate.name == name;
            });
            if (view == views.end())
            {
                std::cout << "Process " << name << " not found.\n\n";
            }
            else
            {
                // The mockup leaves a blank line between the command and its output.
                std::cout << "\n" << buildProcessSmi(*view) << "\n";
            }
        }
        else
        {
            std::cout << "Unknown command '" << input
                      << "'. Available commands: process-smi, exit\n\n";
        }
    }

    // Detach: the process stays in the scheduler and keeps running.
    clearScreen();
    printHeader();
}

void CommandHandler::run()
{
    printHeader();

    std::string line;
    while (emulator.isAlive())
    {
        std::cout << "root:\\> " << std::flush;
        if (!std::getline(std::cin, line))
        {
            break;
        }
        reportGenerationFailure();

        std::istringstream words(line);
        std::string command;
        if (words >> command)
        {
            dispatch(command, words);
        }
    }
}

void CommandHandler::dispatch(const std::string &command, std::istringstream &arguments)
{
    // initialize and exit are the only commands valid before initialization.
    if (command == "initialize")
    {
        initializeSystem();
    }
    else if (command == "exit")
    {
        emulator.exitApp();
    }
    else if (!emulator.isInitialized())
    {
        std::cout << "System not yet initialized. Type 'initialize' to load config.\n\n";
    }
    else if (command == "screen")
    {
        handleScreen(arguments);
    }
    else if (command == "scheduler-start")
    {
        startGeneration();
    }
    else if (command == "scheduler-stop")
    {
        stopGeneration();
    }
    else if (command == "report-util")
    {
        saveReport();
    }
    else
    {
        std::cout << "Command not recognized. Please try again.\n\n";
    }
}

void CommandHandler::reportGenerationFailure()
{
    if (emulator.scheduler().takeGenerationFailure())
    {
        std::cout << "Process generation stopped: unable to create a process.\n\n";
    }
}

void CommandHandler::initializeSystem()
{
    if (emulator.isInitialized())
    {
        std::cout << "System is already initialized.\n\n";
        return;
    }
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

void CommandHandler::handleScreen(std::istringstream &arguments)
{
    std::string subcommand;
    arguments >> subcommand;
    if (subcommand == "-ls" && hasNoMoreWords(arguments))
    {
        std::cout << currentReport();
    }
    else if (subcommand == "-s")
    {
        createAndAttach(arguments);
    }
    else if (subcommand == "-r")
    {
        reattach(arguments);
    }
    else
    {
        // Bare `screen`, unknown flags, and `screen -ls` with extra arguments.
        std::cout << "Usage:\n"
                     "  screen -s <process name>   Create a process and attach to it\n"
                     "  screen -r <process name>   Reattach to a running process\n"
                     "  screen -ls                 List running and finished processes\n\n";
    }
}

void CommandHandler::createAndAttach(std::istringstream &arguments)
{
    std::string processName;
    if (!(arguments >> processName) || !hasNoMoreWords(arguments))
    {
        std::cout << "Usage: screen -s <process name>\n\n";
        return;
    }

    // Processes only execute while the CPU threads run; start() is a no-op if they do.
    emulator.scheduler().start(emulator.config());
    try
    {
        if (!emulator.scheduler().createProcess(processName, emulator.config()))
        {
            std::cout << "Process " << processName << " already exists.\n\n";
            return;
        }
    }
    catch (const std::exception &error)
    {
        std::cout << "Failed to create process " << processName << ": " << error.what() << "\n\n";
        return;
    }
    runProcessScreen(processName);
}

void CommandHandler::reattach(std::istringstream &arguments)
{
    std::string processName;
    if (!(arguments >> processName) || !hasNoMoreWords(arguments))
    {
        std::cout << "Usage: screen -r <process name>\n\n";
        return;
    }
    if (!emulator.scheduler().findActiveProcessByName(processName))
    {
        std::cout << "Process " << processName << " not found.\n\n";
        return;
    }
    runProcessScreen(processName);
}

void CommandHandler::startGeneration()
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

void CommandHandler::stopGeneration()
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

void CommandHandler::saveReport()
{
    std::ofstream file("csopesy-log.txt");
    file << currentReport();
    file.close();
    if (!file)
    {
        std::cout << "Failed to write csopesy-log.txt.\n\n";
        return;
    }
    // Full path, forward slashes, as in the mockup ("C:/csopesy-log.txt").
    std::error_code pathError;
    const auto path = std::filesystem::absolute("csopesy-log.txt", pathError);
    std::cout << "Report generated at "
              << (pathError ? std::string("csopesy-log.txt") : path.generic_string()) << "!\n\n";
}

std::string CommandHandler::currentReport() const
{
    return buildProcessReport({static_cast<std::size_t>(emulator.config().numCpu),
                               emulator.scheduler().snapshot()});
}

bool CommandHandler::hasNoMoreWords(std::istringstream &arguments)
{
    std::string extra;
    return !(arguments >> extra);
}
