// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "steam_wire.h"

// What to start for a game, chosen the way the Steam client does: from the
// app's own launch options (appinfo config/launch), not a per-title table.
namespace launch_target {
// An option's architecture: stated by appinfo when it says, otherwise read
// from the executable's PE header. Play executes 64-bit guests only.
enum Arch { archUnknown = 0, arch32 = 32, arch64 = 64 };
struct Entry {
    std::string executable, arguments, workingdir, description, type, oslist, osarch, betakey, ownsdlc;
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
        e.description = option.str({"description"});
        e.type = lower(option.str({"type"}));
        e.oslist = lower(option.str({"config", "oslist"}));
        e.osarch = option.str({"config", "osarch"});
        e.betakey = option.str({"config", "betakey"});
        e.ownsdlc = option.str({"config", "ownsdlc"});
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
// pe maps an executable to Arch when appinfo states no osarch (empty = none).
using PeArch = std::function<int(const std::string& executable)>;
inline int arch_of(const Entry& e, const PeArch& pe = PeArch()) {
    if (e.osarch == "64") return arch64;
    if (e.osarch == "32") return arch32;
    if (e.osarch.empty() && pe) return pe(e.executable);
    return archUnknown;
}
// Ownership of a DLC we cannot check: "0" and an empty value state none.
inline bool needs_dlc(const Entry& e) { return !e.ownsdlc.empty() && e.ownsdlc != "0"; }
// Same preference as the client on 64-bit Windows, except that architecture
// comes first: Play executes 64-bit guests only, so a 64-bit option wins even
// when a 32-bit one is typed "default" (Hades ships both). Among options of
// equal architecture: a public (no beta key) Windows option, explicit
// "default" type first, then untyped or "none" (many apps store their Play
// entry that way), then "option1"; appinfo order among equals. Tools, servers,
// editors and VR options are never the game's main launch, and an option that
// needs a DLC we cannot check ownership for is skipped.
inline std::vector<const Entry*> ranked(const std::vector<Entry>& all, const PeArch& pe = PeArch()) {
    std::vector<std::pair<int, const Entry*>> scored;
    for (auto& e : all) {
        if (!windows(e) || !e.betakey.empty() || needs_dlc(e)) continue;
        int kind = e.type == "default" ? 8 : e.type.empty() || e.type == "none" ? 4 : e.type == "option1" ? 0 : -1;
        if (kind < 0) continue;
        int arch = arch_of(e, pe);
        // A 64-bit option outranks every 32-bit or unknown one; below that the
        // client's own order (type, then 64 before unspecified before 32) holds.
        scored.push_back({(arch == arch64 ? 100 : 0) + kind + (arch == arch64 ? 2 : arch == archUnknown ? 1 : 0), &e});
    }
    std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<const Entry*> out;
    for (auto& s : scored) out.push_back(s.second);
    return out;
}
// The option Play starts without asking (one valid option, or the auto-launch
// after an install when the app has no choice to offer).
inline const Entry* choose(const std::vector<Entry>& all, const PeArch& pe = PeArch()) {
    auto order = ranked(all, pe);
    return order.empty() ? nullptr : order.front();
}

// ---- launch-option picker (a game with several run options must not pick silently)
// One row of the selection screen. Play executes 64-bit guests only, so a
// 32-bit row is listed (the player should see the game's own options) but
// cannot be started until 32-bit support lands. The entry is owned: the
// picker outlives the appinfo buffer the options were parsed from.
struct Choice {
    Entry entry;
    int arch = archUnknown;
    std::string label;        // the option's description, else its executable
    bool selectable = false;
};
inline std::vector<Choice> choices(const std::vector<Entry>& all, const PeArch& pe = PeArch()) {
    std::vector<Choice> out;
    for (const Entry* e : ranked(all, pe)) {
        int arch = arch_of(*e, pe);
        out.push_back({*e, arch, e->description.empty() ? e->executable : e->description, arch != arch32});
    }
    return out;
}
// The row to start on: the remembered option when it is still offered and
// runnable, else the first runnable one (64-bit rows come first), else the first.
inline int preselect(const std::vector<Choice>& list, const std::string& remembered) {
    int firstSelectable = -1;
    for (size_t i = 0; i < list.size(); i++) {
        if (!list[i].selectable) continue;
        if (firstSelectable < 0) firstSelectable = (int)i;
        if (!remembered.empty() && list[i].entry.executable == remembered) return (int)i;
    }
    return firstSelectable >= 0 ? firstSelectable : 0;
}
// Why a row cannot be started today, or nullptr when it can.
inline const char* arch_note(int arch) { return arch == arch32 ? "needs 32-bit support (not yet available)" : nullptr; }

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
// Fallback when no Windows launch option names an installed executable: the
// largest top-level .exe, but a 64-bit (PE32+) one wins over a larger 32-bit
// one when the install has both (Play executes 64-bit guests only). `arch` is
// the PE header's class; anything that is not 64-bit keeps today's size order.
struct Candidate { std::string name; uint64_t size = 0; int arch = archUnknown; };
inline const Candidate* largest(const std::vector<Candidate>& all) {
    const Candidate* best = nullptr;
    for (auto& c : all) {
        if (!best || (c.arch == arch64) > (best->arch == arch64) ||
            ((c.arch == arch64) == (best->arch == arch64) && c.size > best->size)) best = &c;
    }
    return best;
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
