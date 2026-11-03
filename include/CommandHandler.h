#pragma once

#include <sstream>
#include <string>

class Emulator;

/**
 * @brief The console: reads main-menu commands and runs the process screens.
 *
 * Owns no state of its own; every command acts on the borrowed Emulator.
 */
class CommandHandler
{
public:
    /**
     * @brief Binds the console to an emulator.
     *
     * @param emulator the emulator to drive; must outlive this handler
     */
    explicit CommandHandler(Emulator &emulator);

    /**
     * @brief Runs the main menu until `exit` or end of input.
     */
    void run();

private:
    /** @brief Prints the CSOPESY ASCII art and the developer banner. */
    void printHeader();

    /**
     * @brief Runs the nested `root:\>` loop for an attached process screen.
     *
     * Accepts only `process-smi` and `exit`. Holds no scheduler lock while
     * waiting for input, so the process keeps executing. `exit` and end of
     * input both detach to the main menu without touching the process.
     *
     * @param name the attached process
     */
    void runProcessScreen(const std::string &name);

    /** @brief Clears the terminal with the platform's clear command. */
    void clearScreen();

    /**
     * @brief Routes one main-menu command to its handler.
     *
     * Only `initialize` and `exit` are accepted before initialization.
     *
     * @param command the first word the user typed
     * @param arguments the remaining words
     */
    void dispatch(const std::string &command, std::istringstream &arguments);

    /** @brief Prints a background generation failure once, before the next command. */
    void reportGenerationFailure();

    /** @brief `initialize`: loads config.txt and starts the scheduler. */
    void initializeSystem();

    /**
     * @brief `screen`: routes -ls, -s, and -r, or prints usage.
     *
     * @param arguments the words after `screen`
     */
    void handleScreen(std::istringstream &arguments);

    /**
     * @brief `screen -s <name>`: creates the process and attaches to it.
     *
     * @param arguments the words after `-s`; exactly one name is expected
     */
    void createAndAttach(std::istringstream &arguments);

    /**
     * @brief `screen -r <name>`: attaches to an existing unfinished process.
     *
     * @param arguments the words after `-r`; exactly one name is expected
     */
    void reattach(std::istringstream &arguments);

    /** @brief `scheduler-start`: turns on dummy process generation. */
    void startGeneration();

    /** @brief `scheduler-stop`: turns off generation; existing processes keep running. */
    void stopGeneration();

    /** @brief `report-util`: writes the current report to csopesy-log.txt. */
    void saveReport();

    /**
     * @brief Builds the report for the scheduler's current state.
     *
     * @return the screen -ls / report-util text
     */
    std::string currentReport() const;

    /**
     * @brief Checks that the user typed nothing after the expected words.
     *
     * @param arguments the remaining words; consumed by the check
     * @return true if no words are left
     */
    static bool hasNoMoreWords(std::istringstream &arguments);

    Emulator &emulator; /**< The emulator every command acts on. */
};
