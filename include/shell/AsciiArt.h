#pragma once
#include <string>
#include <vector>

/** @brief Number of rows each ASCII glyph occupies. */
constexpr int ASCII_ROWS = 5;

namespace ascii_art
{
    /**
     * @brief Renders text as multi-line blocky ASCII art.
     *
     * Supports A-Z, 0-9, space, and the punctuation `! ? .`.
     *
     * @param text the text to render, e.g. "CSOPESY"
     * @return one string per output row, top to bottom, ASCII_ROWS rows in total
     */
    std::vector<std::string> convertToAscii(const std::string &text);
}
