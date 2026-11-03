#pragma once

/**
 * @brief ANSI escape codes for colored console output.
 *
 * Header-only, so adding a color needs no .cpp or CMake change.
 */
namespace console_colors
{
    constexpr const char *GREEN = "\033[32m";  /**< Starts green text. */
    constexpr const char *YELLOW = "\033[33m"; /**< Starts yellow text. */
    constexpr const char *RESET = "\033[0m";   /**< Restores the terminal's default color. */
}
