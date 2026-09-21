#pragma once

/**
 * @brief Holds the core state of the Emulator
 */
class Emulator
{
public:
    bool is_app_alive = true;

    void exit_app()
    {
        is_app_alive = false;
    }
};