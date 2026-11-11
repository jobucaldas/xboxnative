// SPDX-License-Identifier: GPL-3.0-or-later
// Item D9: the game page's "Game size" row before a download starts. Steam's
// appdetails response carries no download size, but its pc_requirements text
// does publish a storage requirement ("Storage: 30 GB available space"), which
// is the figure the store page itself shows. Pure and Win32-free, so
// tests/game_library_test.cpp covers the parsing.
#pragma once
#include <cctype>
#include <string>

namespace store_size {

// Finds "storage" in the requirements text and returns the figure that follows
// it ("30 GB", "512 MB", "1.5 GB"), or "" when the text says nothing usable.
// The scan is case-insensitive and skips the HTML tags ascii_clean() has
// already flattened (it leaves plain text with newlines).
inline std::string from_requirements(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text) lower += (char)std::tolower((unsigned char)c);
    size_t at = lower.find("storage");
    if (at == std::string::npos) return std::string();
    size_t i = at + 7;
    // skip the separator the store uses ("Storage: 30 GB ...")
    while (i < text.size() && (text[i] == ':' || text[i] == ' ' || text[i] == '\t')) ++i;
    // a number, optionally with a decimal part
    size_t num0 = i;
    while (i < text.size() && (std::isdigit((unsigned char)text[i]) || text[i] == '.')) ++i;
    if (i == num0) return std::string();
    std::string num = text.substr(num0, i - num0);
    while (i < text.size() && text[i] == ' ') ++i;
    // the unit: GB / MB / TB (any case), as the store writes it
    size_t u0 = i;
    while (i < text.size() && std::isalpha((unsigned char)text[i])) ++i;
    std::string unit = text.substr(u0, i - u0);
    std::string ul;
    for (char c : unit) ul += (char)std::tolower((unsigned char)c);
    if (ul != "gb" && ul != "mb" && ul != "tb" && ul != "gib" && ul != "mib") return std::string();
    for (auto& c : unit) c = (char)std::toupper((unsigned char)c);
    return num + " " + unit;
}

}  // namespace store_size
