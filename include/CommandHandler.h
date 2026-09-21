#pragma once

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
    Emulator &emulator; ///< Reference to the managed Emulator instance.
};