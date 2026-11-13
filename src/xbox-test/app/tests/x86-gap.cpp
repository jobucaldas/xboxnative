// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's gap list end to end, off-device: run a PE32 fixture that
// imports one export the x86 surface does not register (and never calls it)
// through the same entry point the launcher uses, map it through
// x86_route::report, and check the "x86 gap:" lines exactly. Usage:
// x86-gap <gap-fixture.exe>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "../x86_route.h"
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: x86-gap <gap-fixture.exe>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { printf("FAIL cannot open %s\n", argv[1]); return 2; }
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(f), {}};
    auto r = x86::run_image(file, "gap.exe", "\"gap.exe\"",
        [](const std::string&) {}, [](const x86::PlayResult&) {}, x86::kGuestRunCap);
    auto rep = x86_route::report(r, "gap.exe");
    std::vector<std::string> want = {"x86 gap: kernel32.dll!GetSystemFirmwareTable", "x86 gap: total=1"};
    bool ok = r.loaded && r.finished && r.exit_code == 0 && rep.ok && rep.gap == want;
    for (auto& l : rep.gap) printf("sink: %s\n", l.c_str());
    if (!ok) printf("FAIL: gap list differs from the one unresolved import\n");
    else printf("PASS: the launcher names the one unregistered import, and the run still exits 0\n");
    return ok ? 0 : 1;
}
