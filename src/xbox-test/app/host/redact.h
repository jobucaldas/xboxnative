// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
// Text Play writes to launch_probe.log from guest-controlled sources (debug
// output, file paths) may contain the account's SteamID; the log may be
// shared. A SteamID64 is 17 digits starting 7656119; a longer digit run is
// not one and is left alone.
namespace redact {
inline std::string steam_ids(std::string text) {
    for (size_t at = text.find("7656119"); at != std::string::npos; at = text.find("7656119", at)) {
        if (at && text[at - 1] >= '0' && text[at - 1] <= '9') { ++at; continue; }
        size_t end = at;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
        if (end - at == 17) { text.replace(at, 17, "<steamid>"); at += 9; }
        else at = end;
    }
    return text;
}
}
