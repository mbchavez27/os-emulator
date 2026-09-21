#pragma once

// ANSI escape codes for colored console output. Header-only so no .cpp or CMake change needed.
namespace console_colors
{
    constexpr const char *GREEN = "\033[32m";
    constexpr const char *YELLOW = "\033[33m";
    constexpr const char *RESET = "\033[0m";
}