// SPDX-License-Identifier: GPL-3.0-or-later
// Playtime formatting for the game page (item D6): Steam reports whole
// minutes, the page reads them as hours and minutes ("8 h 11 min", "45 min",
// "1 h"). Pure and Win32-free, so tests/ui-launcher.cpp covers it.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace playtime {

// 0 -> "0 min" (the caller says "Not played yet" instead); 45 -> "45 min";
// 60 -> "1 h"; 491 -> "8 h 11 min". A minute short of an hour stays minutes
// ("59 min"), an exact number of hours drops the minutes ("2 h").
inline std::string format_minutes(uint64_t minutes) {
    if (minutes < 60) return std::to_string(minutes) + " min";
    const uint64_t h = minutes / 60, m = minutes % 60;
    if (m == 0) return std::to_string(h) + " h";
    return std::to_string(h) + " h " + std::to_string(m) + " min";
}

}  // namespace playtime
