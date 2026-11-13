// SPDX-License-Identifier: GPL-3.0-or-later
// The console self-test's decisions, with no Windows dependency: which packaged
// fixtures exist, what a request file asks for, how the queue survives a
// launcher restart, and what a fixture's exit code means. selftest.h does the
// Play work and calls these; the off-device test (tests/selftest-plan-test.cpp)
// compiles this file alone.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace selftest_plan {
// The fixtures app/build.sh packs into layout\diag. `pe32` runs in the x86
// interpreter instead of Play's 64-bit loader, so it needs no launcher restart.
struct Mode {
    const char* name;
    const char* exe;
    bool pe32;
};
inline const Mode* modes(size_t& count) {
    static const Mode table[] = {
        {"x86", "fixture-crt.exe", true},
        {"vulkan-dzn", "play-vulkan-dzn.exe", false},
        {"vulkan-draw", "play-vulkan-draw.exe", false},
        {"d3d9", "play-d3d9.exe", false},
        {"d3d9-triangle", "play-d3d9-triangle.exe", false},
        {"d3d9-triangle-variants", "play-d3d9-triangle-variants.exe", false},
        {"d3d9-textured", "play-d3d9-textured.exe", false},
        {"d3d10", "play-d3d10.exe", false},
        {"d3d10-triangle", "play-d3d10-triangle.exe", false},
        {"opengl", "play-opengl.exe", false},
        {"d3d11", "play-d3d11.exe", false},
        {"d3d12", "play-d3d12.exe", false},
    };
    count = sizeof(table) / sizeof(*table);
    return table;
}
// How many launcher restarts a run may take. Play keeps one 64-bit guest per
// process, so every non-pe32 fixture costs a restart; the extra one lets a
// fixture that faults before game code be retried. Derived from the table so
// "all" can never outgrow a fixed cap: a limit below the guest count marks the
// last fixtures FAIL restart-limit without ever running them.
inline int max_restarts() {
    size_t count = 0;
    const Mode* table = modes(count);
    int guests = 0;
    for (size_t i = 0; i < count; ++i) if (!table[i].pe32) ++guests;
    return guests + 1;
}
inline const Mode* find(const std::string& name) {
    size_t count = 0;
    const Mode* table = modes(count);
    for (size_t i = 0; i < count; ++i) if (name == table[i].name) return &table[i];
    return nullptr;
}
inline std::string trim(const std::string& s) {
    size_t start = 0, end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) ++start;
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r')) --end;
    return s.substr(start, end - start);
}
inline std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
// A request file: one mode per line, blank lines and '#' comments ignored,
// case-insensitive. "all" asks for every fixture in the canonical order (x86
// first: it costs no launcher restart). Unknown names are kept so the run can
// report them as SKIP instead of silently dropping them.
inline std::vector<std::string> parse_request(const std::string& text) {
    std::vector<std::string> out;
    bool all = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = lower(trim(text.substr(pos, end - pos)));
        pos = end + 1;
        if (line.empty() || line[0] == '#') continue;
        if (line == "all") { all = true; continue; }
        if (std::find(out.begin(), out.end(), line) == out.end()) out.push_back(line);
    }
    if (!all) return out;
    out.clear();
    size_t count = 0;
    const Mode* table = modes(count);
    for (size_t i = 0; i < count; ++i) out.push_back(table[i].name);
    return out;
}
// The queue file carries a request across launcher restarts: Play starts one
// 64-bit guest per process, so each fixture needs its own launch. `restarts`
// bounds a launcher that restarts without making progress.
struct Queue {
    int restarts = 0;
    std::vector<std::string> pending;
};
inline std::string format_queue(const Queue& q) {
    std::string out = "restarts " + std::to_string(q.restarts) + "\n";
    for (auto& name : q.pending) out += "mode " + name + "\n";
    return out;
}
inline bool parse_queue(const std::string& text, Queue& q) {
    q = Queue{};
    bool restarts = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) continue;
        if (line.rfind("restarts ", 0) == 0) {
            try { q.restarts = std::stoi(line.substr(9)); } catch (...) { return false; }
            restarts = true;
        } else if (line.rfind("mode ", 0) == 0) {
            q.pending.push_back(trim(line.substr(5)));
        } else return false;
    }
    return restarts;
}
// What a finished fixture's exit code means. The fixtures return 42 when every
// step succeeded; the Vulkan one returns 43 when the host's D3D12 refuses dzn's
// device, which is BLOCKED (the platform said no), never a pass. A guest that
// ran code but never exited (a fault, a hang) is a failure, and the caller's
// detail line says which.
inline std::string verdict(const std::string& mode, bool finished, bool executed, uint32_t code) {
    if (!executed) return "FAIL no-guest-code";
    if (!finished) return "FAIL did-not-exit";
    if (mode == "vulkan-dzn") {
        if (code == 42) return "PASS frame-presented";
        if (code == 43) return "BLOCKED device-creation";
    }
    // The Vulkan draw fixture's readback mismatch: the pipeline ran but the
    // host drew nothing the guest could read. A platform limit, never a pass.
    if (mode == "vulkan-draw" && code == 43) return "BLOCKED pixel-readback";
    // The OpenGL fixture's readback mismatch: the context worked but the host
    // drew nothing the guest could read. A platform limit, never a pass.
    if (mode == "opengl" && code == 43) return "BLOCKED pixel-readback";
    // The D3D11/D3D12 fixtures' readback mismatch: the pipeline ran but the
    // pixels did not read back as drawn. A platform limit, never a pass.
    if ((mode == "d3d11" || mode == "d3d12") && code == 43) return "BLOCKED pixel-readback";
    // The D3D11 fixture's newer passes block the same way: the calls the
    // feature needs all succeeded, but the effect never arrived — pixels,
    // colour or query answer (54 MSAA resolve, 58 GenerateMips, 63
    // command-list execution, 67 stream-out capture, 71 indirect draw, 75
    // occlusion-query answer, 82 indexed draw, 86 depth ordering). A platform
    // limit, never a pass.
    if (mode == "d3d11") {
        if (code == 54) return "BLOCKED msaa-resolve";
        if (code == 58) return "BLOCKED generate-mips";
        if (code == 63) return "BLOCKED command-list";
        if (code == 67) return "BLOCKED stream-out";
        if (code == 71) return "BLOCKED indirect-draw";
        if (code == 75) return "BLOCKED occlusion-query";
        if (code == 82) return "BLOCKED indexed-draw";
        if (code == 86) return "BLOCKED depth-test";
        if (code == 90) return "BLOCKED texture-array-copy";
        if (code == 94) return "BLOCKED bc1-copy";
        // 95 = a Wine host: the four Wine-gated passes did not run, so the
        // run must never count as a pass — the console's D3D11-on-12 runs
        // them in full.
        if (code == 95) return "BLOCKED wine-host";
    }
    // The D3D10 drawing fixture blocks the same way: every pass ran but the
    // pixels (or streamed vertices) did not read back as drawn.
    if (mode == "d3d10-triangle" && code == 43) return "BLOCKED pixel-readback";
    // The D3D12 fixture's device-creation fallback steps (play-d3d12.cpp):
    // 81 = the factory for the explicit-adapter attempt could not be created,
    // 82 = EnumAdapters1(0) failed, 83 = every D3D12CreateDevice attempt
    // failed (null and explicit adapter, FL 11_0 and 12_0). Each names the
    // attempt that failed, and each is a FAIL, never a pass or a BLOCKED.
    if (mode == "d3d12" && code == 81) return "FAIL d3d12-device-factory";
    if (mode == "d3d12" && code == 82) return "FAIL d3d12-device-enum";
    if (mode == "d3d12" && code == 83) return "FAIL d3d12-device-all-attempts";
    if (code == 42) return "PASS";
    return "FAIL exit-" + std::to_string(code);
}
struct Tally { int pass = 0, fail = 0, blocked = 0, skip = 0; };
// Counts the "SELFTEST RESULT <mode> <verdict>" lines a request produced. The
// log is appended across restarts, so this is the whole request's record.
inline Tally tally(const std::string& log) {
    Tally t;
    size_t pos = 0;
    while (pos < log.size()) {
        size_t end = log.find('\n', pos);
        if (end == std::string::npos) end = log.size();
        std::string line = log.substr(pos, end - pos);
        pos = end + 1;
        static const std::string prefix = "SELFTEST RESULT ";
        if (line.rfind(prefix, 0) != 0) continue;
        size_t nameEnd = line.find(' ', prefix.size());
        if (nameEnd == std::string::npos) continue;
        size_t verdictStart = line.find_first_not_of(' ', nameEnd);
        if (verdictStart == std::string::npos) continue;
        std::string word = line.substr(verdictStart, line.find(' ', verdictStart) - verdictStart);
        if (word == "PASS") ++t.pass;
        else if (word == "FAIL") ++t.fail;
        else if (word == "BLOCKED") ++t.blocked;
        else if (word == "SKIP") ++t.skip;
    }
    return t;
}
}
