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
    std::vector<std::string> gap_lines; bool before_guest=false;
    auto r = x86::run_image(file, "gap.exe", "\"gap.exe\"",
        [](const std::string&) {}, [](const x86::PlayResult&) {}, x86::kGuestRunCap, nullptr, false,
        [&](const x86::PlayResult& mapped) {
            before_guest=true;
            gap_summary::Binder binder;
            for(const auto& item:mapped.gap_imports)
                binder.record(std::get<0>(item),std::get<1>(item),std::get<2>(item));
            gap_lines=binder.report("gap.exe","x86").lines;
        });
    auto rep = x86_route::report(r, "gap.exe");
    std::vector<std::string> want = {"x86 gap: kernel32.dll!GetSystemFirmwareTable", "x86 gap: total=1"};
    bool summary = gap_lines.size()==3 &&
        gap_lines[0]=="GAP: dll=kernel32.dll resolved=0 routed=1 donor=0 stub=0 missing=1" &&
        gap_lines[1]=="GAP: summary exe=gap.exe arch=x86 resolved=0 routed=1 donor=0 stub=0 missing=1" &&
        gap_lines[2]=="GAP: missing kernel32.dll!GetSystemFirmwareTable";
    bool ok = r.loaded && r.finished && r.exit_code == 0 && rep.ok && rep.gap == want && before_guest && summary;
    for (auto& l : rep.gap) printf("sink: %s\n", l.c_str());
    if (!ok) printf("FAIL: gap list differs from the one unresolved import\n");
    else printf("PASS: the launcher names the one unregistered import, and the run still exits 0\n");
    return ok ? 0 : 1;
}
