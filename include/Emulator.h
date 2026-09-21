#pragma once

/**
 * @brief Shared liveness state. CommandHandler mutates it, main loop observes it.
 */
class Emulator
{
public:
    /// True while CLI loop should keep running. Set false by exit_app().
    bool is_app_alive = true;

    /// Signals shutdown; loop exits on next iteration check.
    void exit_app()
    {
        is_app_alive = false;
    }
};