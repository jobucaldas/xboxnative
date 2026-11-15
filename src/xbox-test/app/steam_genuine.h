// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

namespace steam_genuine {
inline bool opt_in(const std::string& text) {
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || text[first] != '1') return false;
    return text.find_first_not_of(" \t\r\n", first + 1) == std::string::npos;
}
inline bool loginusers_vdf(const char* steam_id, char* out, size_t capacity) {
    if (!steam_id || !*steam_id || !out) return false;
    for (const char* p = steam_id; *p; ++p) if (*p < '0' || *p > '9') return false;
    int n = snprintf(out, capacity,
        "\"users\"\n{\n\t\"%s\"\n\t{\n\t\t\"AccountName\"\t\t\"%s\"\n"
        "\t\t\"PersonaName\"\t\t\"Player\"\n\t\t\"RememberPassword\"\t\t\"1\"\n"
        "\t\t\"AllowAutoLogin\"\t\t\"1\"\n\t\t\"MostRecent\"\t\t\"1\"\n\t}\n}\n",
        steam_id, steam_id);
    return n > 0 && static_cast<size_t>(n) < capacity;
}
inline bool autologin_user(const char* steam_id, char* out, size_t capacity) {
    if (!steam_id || !*steam_id || !out) return false;
    for (const char* p = steam_id; *p; ++p) if (*p < '0' || *p > '9') return false;
    int n = snprintf(out, capacity, "%s", steam_id);
    return n > 0 && static_cast<size_t>(n) < capacity;
}
}
