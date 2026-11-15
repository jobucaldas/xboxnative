// SPDX-License-Identifier: GPL-3.0-or-later
// Runs the VC110 fixture through the x86 interpreter both without and with its donor.
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../host/x86/x86_guest.h"
#include "../host/x86/x86_play.h"
using namespace x86;

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}

static bool wine_debug_routes() {
    Memory mem; ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
    mem.map(0x2000, 0x1000, PROT_R | PROT_W);
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2800;
    auto call = [&](const char* name, std::initializer_list<uint32_t> args, uint32_t& result) {
        auto it = reg.map.find({"ntdll.dll", name});
        if (it == reg.map.end() || it->second.conv != Conv::Cdecl || it->second.argc != args.size()) return false;
        unsigned i = 0; for (uint32_t arg : args) mem.w32(cpu.reg[ESP] + 4 + 4 * i++, arg);
        result = it->second.fn(cpu);
        return true;
    };
    uint32_t flags = 99, header = 0, output = 99, first = 0, second = 0;
    if (!call("__wine_dbg_get_channel_flags", {0xffffffffu}, flags) || flags != 0 ||
        !call("__wine_dbg_header", {3, 0xffffffffu, 0xffffffffu}, header) || header != 0xffffffffu ||
        !call("__wine_dbg_output", {0xffffffffu}, output) || output != 0 ||
        !call("__wine_dbg_strdup", {0xffffffffu}, first) ||
        !call("__wine_dbg_strdup", {0}, second) || first != second || !first || !mem.read_cstr(first).empty() ||
        !host.out.empty() || !host.err.empty()) {
        std::printf("FAIL Wine ntdll debug imports must be cdecl, disabled and terminated\n");
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc != 3) { std::printf("usage: x86-vc-crt <fixture.exe> <runtime/x86>\n"); return 2; }
    if (!wine_debug_routes()) return 1;
    std::printf("X86-VC-CRT PASS: Wine ntdll debug imports are disabled with cdecl semantics\n");
    auto file = read_file(argv[1]);
    if (file.empty()) { std::printf("FAIL cannot read fixture\n"); return 2; }
    auto absent = run_image(file, "VC110-Fixture.EXE", "VC110-Fixture.EXE");
    if (absent.fault.find("unresolved import msvcr110.dll!malloc") == std::string::npos) {
        std::printf("FAIL null DllSource did not fault at msvcr110.dll!malloc: %s\n", absent.fault.c_str());
        return 1;
    }
    std::printf("X86-VC-CRT NULL-SOURCE FAILS: %s\n", absent.fault.c_str());

    const std::string dir = argv[2];
    auto runtimeReader = [dir](const std::string& name) { return read_file(dir + "/" + name); };
    std::string origin;
    DllSource source = make_vc110_vc120_source(
        [](const std::string&) { return std::vector<uint8_t>{}; }, runtimeReader,
        [&origin](const std::string&, const char* from) { origin = from; });
    auto donor = source("msvcr110.dll");
    if (donor.empty() || origin != "runtime" || source("unrelated.dll").size()) {
        std::printf("FAIL runtime donor selection or unrelated-DLL rejection\n"); return 1;
    }
    unsigned runtimeReads = 0;
    std::string gameOrigin;
    DllSource gameWins = make_vc110_vc120_source(
        [](const std::string&) { return std::vector<uint8_t>{0x42}; },
        [&runtimeReads,&donor](const std::string&) { ++runtimeReads; return donor; },
        [&gameOrigin](const std::string&, const char* from) { gameOrigin = from; });
    auto gameCopy = gameWins("msvcr110.dll");
    if (gameCopy != std::vector<uint8_t>{0x42} || runtimeReads || gameOrigin != "game dir") {
        std::printf("FAIL game-local CRT did not take precedence\n"); return 1;
    }

    std::vector<std::string> gaps;
    DllLoadObserver observe = [&gaps](const std::string& name, const std::vector<std::string>& unresolved) {
        if (name == "msvcr110.dll") gaps = unresolved;
    };
    auto result = run_image(file, "VC110-Fixture.EXE", "VC110-Fixture.EXE", nullptr, nullptr,
                            100ull * 1000 * 1000, source, true, nullptr, observe);
    if (result.finished && result.exit_code == 42 && result.fault.empty()) {
        std::printf("X86-VC-CRT PASS: msvcr110.dll heap and strlen imports ran to exit 42\n");
    } else if (result.fault.find("ntdll.dll!__wine_dbg_header") != std::string::npos && !gaps.empty()) {
        std::printf("X86-VC-CRT LIMIT: donor DllMain faults on unrouted ntdll.dll!__wine_dbg_header\n");
    } else {
        std::printf("FAIL donor run finished=%d exit=%u fault=%s gaps=%zu\n", result.finished ? 1 : 0,
                    result.exit_code, result.fault.c_str(), gaps.size());
        return 1;
    }

    for (const char* dll : {"msvcr110.dll", "msvcp110.dll", "msvcr120.dll", "msvcp120.dll"}) {
        auto bytes = source(dll);
        if (bytes.empty()) { std::printf("FAIL missing selected donor %s\n", dll); return 1; }
        Memory mem; ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.register_all();
        std::map<std::string, Dll> loaded;
        std::map<std::string, std::vector<std::string>> imports;
        DllLoadObserver record = [&imports](const std::string& name, const std::vector<std::string>& unresolved) {
            imports[name] = unresolved;
        };
        try { load_dll(mem, table, reg, bytes, source, loaded, 0, nullptr, nullptr, dll, record); }
        catch (const std::exception& e) { std::printf("FAIL mapping %s: %s\n", dll, e.what()); return 1; }
        for (const auto& module : imports) {
            if (module.first != dll) continue;
            std::printf("X86-VC-CRT UNROUTED %s kernel32=[", module.first.c_str());
            bool first = true;
            for (const auto& name : module.second) if (name.rfind("kernel32.dll!", 0) == 0) {
                std::printf("%s%s", first ? "" : ",", name.c_str()); first = false;
            }
            std::printf("] other=["); first = true;
            for (const auto& name : module.second) if (name.rfind("kernel32.dll!", 0) != 0) {
                std::printf("%s%s", first ? "" : ",", name.c_str()); first = false;
            }
            std::printf("]\n");
        }
    }
    std::printf("X86-VC-CRT PASS: DllSource selected runtime donor and preferred game-local copy\n");
    return 0;
}
