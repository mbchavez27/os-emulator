#pragma once

/**
 * @brief ANSI escape codes for colored console output.
 *
 * Header-only, so adding a color needs no .cpp or CMake change. Each role
 * names what the text means, so the console's color scheme lives in one place.
 */
namespace console_colors
{
    constexpr const char *RED = "\033[31m";     /**< Starts red text. */
    constexpr const char *GREEN = "\033[32m";   /**< Starts green text. */
    constexpr const char *YELLOW = "\033[33m";  /**< Starts yellow text. */
    constexpr const char *MAGENTA = "\033[35m"; /**< Starts magenta text. */
    constexpr const char *CYAN = "\033[36m";    /**< Starts cyan text. */
    constexpr const char *RESET = "\033[0m";    /**< Restores the terminal's default color. */

    constexpr const char *TITLE = YELLOW;    /**< ASCII header and screen titles. */
    constexpr const char *PROMPT = MAGENTA;  /**< The root:\> prompt. */
    constexpr const char *SUCCESS = GREEN;   /**< Completed operations. */
    constexpr const char *INFO = CYAN;       /**< Process details and reports. */
    constexpr const char *FAILURE = RED;     /**< Errors, rejections, and usage hints. */
}
