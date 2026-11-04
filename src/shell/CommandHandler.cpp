#include "shell/CommandHandler.h"
#include "system/Emulator.h"
#include "shell/AsciiArt.h"
#include "shell/ConsoleColors.h"
#include "shell/ProcessReport.h"
#include "shell/ProcessSmi.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>

namespace
{

/**
 * @brief Prints a console response in its role color, then the blank line
 * that separates responses.
 *
 * @param color the console_colors role for the message
 * @param message the response text, without trailing newlines
 */
void respond(const char *color, const std::string &message)
{
    std::cout << color << message << console_colors::RESET << "\n\n";
}

/** @brief Prints the root:\> prompt in the prompt color. */
void printPrompt()
{
    std::cout << console_colors::PROMPT << "root:\\>" << console_colors::RESET << " " << std::flush;
}

} // namespace

CommandHandler::CommandHandler(Emulator &e) : emulator(e) {}

void CommandHandler::printHeader()
{
    for (const auto &line : ascii_art::convertToAscii("CSOPESY"))
    {
        std::cout << console_colors::TITLE << line << console_colors::RESET << "\n";
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
        std::cout << console_colors::FAILURE << "Failed to clear screen." << console_colors::RESET << "\n";
    }
#else
    if (std::system("clear") != 0)
    {
        std::cout << console_colors::FAILURE << "Failed to clear screen." << console_colors::RESET << "\n";
    }
#endif
}

void CommandHandler::runProcessScreen(const std::string &name)
{
    clearScreen();
    std::cout << console_colors::TITLE << "Process screen: " << name << console_colors::RESET << "\n"
              << "Type 'process-smi' for process information, 'exit' to return to the main menu.\n\n";

    std::string line;
    while (emulator.isAlive())
    {
        printPrompt();

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
                respond(console_colors::FAILURE, "Process " + name + " not found.");
            }
            else
            {
                // The mockup leaves a blank line between the command and its output.
                std::cout << "\n" << console_colors::INFO << buildProcessSmi(*view) << console_colors::RESET << "\n";
            }
        }
        else
        {
            respond(console_colors::FAILURE,
                    "Unknown command '" + input + "'. Available commands: process-smi, exit");
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
        printPrompt();
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
        respond(console_colors::FAILURE, "System not yet initialized. Type 'initialize' to load config.");
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
        respond(console_colors::FAILURE, "Command not recognized. Please try again.");
    }
}

void CommandHandler::reportGenerationFailure()
{
    if (emulator.scheduler().takeGenerationFailure())
    {
        respond(console_colors::FAILURE, "Process generation stopped: unable to create a process.");
    }
}

void CommandHandler::initializeSystem()
{
    if (emulator.isInitialized())
    {
        respond(console_colors::FAILURE, "System is already initialized.");
        return;
    }
    std::string error;
    if (emulator.initialize("config.txt", error))
    {
        respond(console_colors::SUCCESS, "Initialization successful.");
    }
    else
    {
        respond(console_colors::FAILURE, error);
    }
}

void CommandHandler::handleScreen(std::istringstream &arguments)
{
    std::string subcommand;
    arguments >> subcommand;
    if (subcommand == "-ls" && hasNoMoreWords(arguments))
    {
        // Colored only on the console; report-util writes the same text plain.
        std::cout << console_colors::INFO << currentReport() << console_colors::RESET;
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
        respond(console_colors::FAILURE, "Usage:\n"
                                         "  screen -s <process name>   Create a process and attach to it\n"
                                         "  screen -r <process name>   Reattach to a running process\n"
                                         "  screen -ls                 List running and finished processes");
    }
}

void CommandHandler::createAndAttach(std::istringstream &arguments)
{
    std::string processName;
    if (!(arguments >> processName) || !hasNoMoreWords(arguments))
    {
        respond(console_colors::FAILURE, "Usage: screen -s <process name>");
        return;
    }

    // Processes only execute while the CPU threads run; start() is a no-op if they do.
    emulator.scheduler().start(emulator.config());
    try
    {
        if (!emulator.scheduler().createProcess(processName, emulator.config()))
        {
            respond(console_colors::FAILURE, "Process " + processName + " already exists.");
            return;
        }
    }
    catch (const std::exception &error)
    {
        respond(console_colors::FAILURE, "Failed to create process " + processName + ": " + error.what());
        return;
    }
    runProcessScreen(processName);
}

void CommandHandler::reattach(std::istringstream &arguments)
{
    std::string processName;
    if (!(arguments >> processName) || !hasNoMoreWords(arguments))
    {
        respond(console_colors::FAILURE, "Usage: screen -r <process name>");
        return;
    }
    if (!emulator.scheduler().findActiveProcessByName(processName))
    {
        respond(console_colors::FAILURE, "Process " + processName + " not found.");
        return;
    }
    runProcessScreen(processName);
}

void CommandHandler::startGeneration()
{
    if (emulator.scheduler().startGeneration(emulator.config()))
    {
        respond(console_colors::SUCCESS, "Process generation started.");
    }
    else
    {
        respond(console_colors::FAILURE, "Process generation is already running.");
    }
}

void CommandHandler::stopGeneration()
{
    if (emulator.scheduler().stopGeneration())
    {
        respond(console_colors::SUCCESS, "Process generation stopped. Existing processes will continue running.");
    }
    else
    {
        respond(console_colors::FAILURE, "Process generation is not running.");
    }
}

void CommandHandler::saveReport()
{
    std::ofstream file("csopesy-log.txt");
    file << currentReport();
    file.close();
    if (!file)
    {
        respond(console_colors::FAILURE, "Failed to write csopesy-log.txt.");
        return;
    }
    // Full path, forward slashes, as in the mockup ("C:/csopesy-log.txt").
    std::error_code pathError;
    const auto path = std::filesystem::absolute("csopesy-log.txt", pathError);
    respond(console_colors::SUCCESS,
            "Report generated at " + (pathError ? std::string("csopesy-log.txt") : path.generic_string()) + "!");
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
