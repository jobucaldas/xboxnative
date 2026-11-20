// SPDX-License-Identifier: GPL-3.0-or-later
// Loads the VC2015 fixture with the i386 donors (runtime/x86): the undeclared
// api-ms-win-crt-runtime name must bind through the ucrtbase donor, and msvcp140
// must bind as a donor module. Off-device: needs out/wine-donors (build-wine-donors.sh).
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include "../host/x86/x86_guest.h"
#include "../host/x86/x86_play.h"
using namespace x86;

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
    if (argc != 3) { std::printf("usage: x86-vc2015 <fixture.exe> <runtime/x86>\n"); return 2; }
    auto file = read_file(argv[1]);
    if (file.empty()) { std::printf("FAIL cannot read fixture\n"); return 2; }
    const std::string dir = argv[2];
    auto runtimeReader = [dir](const std::string& name) { return read_file(dir + "/" + name); };
    auto none = [](const std::string&) { return std::vector<uint8_t>{}; };
    DllSource source = make_d3dx9_source(none, runtimeReader, make_vc110_vc120_source(none, runtimeReader));
    std::map<std::string, std::vector<std::string>> modules;
    DllLoadObserver observe = [&modules](const std::string& name, const std::vector<std::string>& unresolved) {
        modules[name] = unresolved;
    };
    auto result = run_image(file, "VC2015-Fixture.EXE", "VC2015-Fixture.EXE", nullptr, nullptr,
                            100ull * 1000 * 1000, source, true, nullptr, observe);
    const auto& gaps = result.unresolved;
    const bool crtBound = std::find(gaps.begin(), gaps.end(), "api-ms-win-crt-runtime-l1-1-0.dll!_c_exit") == gaps.end();
    const bool msvcpBound = std::find(gaps.begin(), gaps.end(), "msvcp140.dll!_Mtx_lock") == gaps.end();
    const bool ucrtLoaded = modules.count("ucrtbase.dll") != 0;
    const bool msvcpLoaded = modules.count("msvcp140.dll") != 0;
    if (!result.loaded || !crtBound || !msvcpBound || !ucrtLoaded || !msvcpLoaded) {
        std::printf("FAIL loaded=%d crt_bound=%d msvcp_bound=%d ucrtbase=%d msvcp140=%d fault=%s\n",
                    result.loaded ? 1 : 0, crtBound ? 1 : 0, msvcpBound ? 1 : 0, ucrtLoaded ? 1 : 0,
                    msvcpLoaded ? 1 : 0, result.fault.c_str());
        return 1;
    }
    std::printf("X86-VC2015 PASS: api-ms-win-crt-runtime!_c_exit -> ucrtbase donor, msvcp140!_Mtx_lock -> msvcp140 donor\n");
    return 0;
}
