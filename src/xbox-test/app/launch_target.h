// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <vector>
#include "steam_wire.h"

// What to start for a game, chosen the way the Steam client does: from the
// app's own launch options (appinfo config/launch), not a per-title table.
namespace launch_target {
struct Entry {
    std::string executable, arguments, workingdir, type, oslist, osarch, betakey;
};
inline std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
// appinfo "config" { "launch" { "0" { executable, arguments, type, config { oslist, osarch, betakey } } } }
inline std::vector<Entry> entries(const Vdf& app) {
    std::vector<Entry> out;
    const Vdf* launch = app.at({"config", "launch"});
    if (!launch || !launch->obj) return out;
    for (auto& option : launch->kids) {
        if (!option.obj) continue;
        Entry e;
        e.executable = option.str({"executable"});
        e.arguments = option.str({"arguments"});
        e.workingdir = option.str({"workingdir"});
        e.type = lower(option.str({"type"}));
        e.oslist = lower(option.str({"config", "oslist"}));
        e.osarch = option.str({"config", "osarch"});
        e.betakey = option.str({"config", "betakey"});
        if (!e.executable.empty()) out.push_back(e);
    }
    return out;
}
inline bool windows(const Entry& e) {
    if (e.oslist.empty()) return true;
    size_t start = 0;
    for (;;) {
        size_t comma = e.oslist.find(',', start);
        if (e.oslist.substr(start, comma - start) == "windows") return true;
        if (comma == std::string::npos) return false;
        start = comma + 1;
    }
}
// Same preference as the client on 64-bit Windows: a public (no beta key)
// Windows option of the default type, 64-bit before unspecified before 32-bit,
// keeping appinfo order among equals. Tools, servers, editors and VR options
// are never the game's main launch.
inline const Entry* choose(const std::vector<Entry>& all) {
    const Entry* best = nullptr; int bestScore = -1;
    for (auto& e : all) {
        if (!windows(e) || !e.betakey.empty()) continue;
        if (!e.type.empty() && e.type != "default" && e.type != "option1") continue;
        int score = (e.type == "default" || e.type.empty() ? 4 : 0) + (e.osarch == "64" ? 2 : e.osarch.empty() ? 1 : 0);
        if (score > bestScore) { best = &e; bestScore = score; }
    }
    return best;
}
// Executable as an install-relative Windows path: forward slashes become
// backslashes; a leading ".\" and rooted or parent-escaping paths are refused.
inline bool relative_path(const std::string& executable, std::string& out) {
    out.clear();
    std::string p = executable;
    for (auto& c : p) if (c == '/') c = '\\';
    while (p.rfind(".\\", 0) == 0) p.erase(0, 2);
    if (p.empty() || p[0] == '\\' || p.find(':') != std::string::npos) return false;
    size_t start = 0;
    for (;;) {
        size_t sep = p.find('\\', start);
        std::string part = p.substr(start, sep - start);
        if (part.empty() || part == "..") return false;
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    out = p;
    return true;
}
// Engine options chosen from the game's own files, for any title.
// Unity: windowed (the swap chain covers the CoreWindow), D3D11 (the guest
// graphics route), and the player log beside the game for diagnosis.
inline const char* unity_arguments() { return "-screen-fullscreen 0 -force-d3d11 -logFile vis-player.log"; }
inline std::string arguments(const Entry* chosen, bool unity) {
    std::string args = chosen ? chosen->arguments : std::string();
    if (unity) args += std::string(args.empty() ? "" : " ") + unity_arguments();
    return args;
}
}
