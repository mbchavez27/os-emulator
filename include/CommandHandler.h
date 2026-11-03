#pragma once

#include <sstream>
#include <string>

// Forward declaration of the Emulator class
class Emulator;

/**
 * @brief Handles user commands and interactions for the Emulator instance.
 */
class CommandHandler
{
public:
    explicit CommandHandler(Emulator &emulator);

    /**
     * @brief Starts the main command processing loop.
     */
    void run();

private:
    /// Prints the CSOPESY ASCII art and the developer banner.
    void printHeader();

    /**
     * @brief Nested read loop for an attached process screen (`root:\>` prompt).
     *
     * Accepts only `process-smi` and `exit`. Holds no scheduler lock while
     * waiting for input, so the attached process keeps executing. `exit` and
     * EOF both detach back to the main menu without touching the process.
     */
    void runProcessScreen(const std::string &name);

    /// Clears the console via the platform command.
    void clearScreen();

    /// Routes one main-menu command; only initialize and exit work before initialization.
    void dispatch(const std::string &command, std::istringstream &arguments);
    void reportGenerationFailure();
    void initializeSystem();
    void handleScreen(std::istringstream &arguments);
    void createAndAttach(std::istringstream &arguments);
    void reattach(std::istringstream &arguments);
    void startGeneration();
    void stopGeneration();
    void saveReport();
    /// The screen -ls / report-util text for the current scheduler state.
    std::string currentReport() const;
    /// True when the user typed nothing after the expected words.
    static bool hasNoMoreWords(std::istringstream &arguments);

    Emulator &emulator; ///< Reference to the managed Emulator instance.
};