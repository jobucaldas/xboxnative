// SPDX-License-Identifier: GPL-3.0-or-later
// Item D10: per-game favorites, persisted in LocalState\favorites.txt (one
// appid per line, '#' starts a comment). Pure and Win32-free, so
// tests/game_library_test.cpp covers the round trip the launcher relies on
// across restarts.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace favorites {

inline std::set<uint32_t> parse(const std::string& text) {
    std::set<uint32_t> out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        size_t a = 0, b = line.size();
        while (a < b && (line[a] == ' ' || line[a] == '\t' || line[a] == '\r')) ++a;
        while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t' || line[b - 1] == '\r')) --b;
        if (a == b) continue;
        const std::string num = line.substr(a, b - a);
        char* end = nullptr;
        const unsigned long v = std::strtoul(num.c_str(), &end, 10);
        if (!end || *end || v == 0 || v > 0xFFFFFFFFul) continue;   // one whole appid per line
        out.insert((uint32_t)v);
    }
    return out;
}

inline std::string format(const std::set<uint32_t>& ids) {
    std::string out;
    for (uint32_t id : ids) out += std::to_string(id) + "\n";
    return out;
}

inline bool contains(const std::set<uint32_t>& ids, uint32_t appid) { return ids.count(appid) != 0; }

// Returns the new state (true = now a favorite).
inline bool toggle(std::set<uint32_t>& ids, uint32_t appid) {
    if (ids.erase(appid)) return false;
    ids.insert(appid);
    return true;
}

}  // namespace favorites
