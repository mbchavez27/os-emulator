#pragma once

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

    /**
     * @brief Prints the ASCII art header for "CSOPESY".
     */
    void print_header();

private:
    /**
     * @brief Nested read loop for an attached process screen (`root:\>` prompt).
     *
     * Accepts only `process-smi` and `exit`. Holds no scheduler lock while
     * waiting for input, so the attached process keeps executing. `exit` and
     * EOF both detach back to the main menu without touching the process.
     */
    void run_process_screen(const std::string &name);

    /// Clears the console via the platform command.
    void clear_screen();

    Emulator &emulator; ///< Reference to the managed Emulator instance.
};