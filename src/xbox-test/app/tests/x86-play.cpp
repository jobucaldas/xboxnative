// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's PE32 entry point (host/x86/x86_play.h) against the same i686
// CRT program x86-guest.cpp runs: the structured PlayResult the launch log and
// launch_result.json are built from, the live output tee, and the pre-entry
// progress callback. Usage: x86-play <fixture-crt.exe>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../host/x86/x86_play.h"
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: x86-play <fixture-crt.exe>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { printf("FAIL cannot open %s\n", argv[1]); return 2; }
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(f), {}};
    std::string seen;   // every tee call, in order
    bool loaded_first = false;
    unsigned loaded_calls = 0;
    auto r = x86::run_image(file, "fixture-crt.exe", "\"fixture-crt.exe\"",
        [&seen](const std::string& s) { seen += s; },
        [&](const x86::PlayResult& at_load) {
            ++loaded_calls;
            // The callback runs after mapping and binding, before the entry
            // point: the result already says what was loaded, and nothing has
            // run yet.
            loaded_first = at_load.loaded && at_load.imports && at_load.bound == at_load.imports &&
                           at_load.entry >= at_load.base && at_load.fault.empty();
        });
    printf("play: loaded=%d finished=%d exit=%u instructions=%llu base=0x%08x entry=0x%08x imports=%u bound=%u relocs=%u\n",
           r.loaded ? 1 : 0, r.finished ? 1 : 0, r.exit_code, (unsigned long long)r.instructions,
           r.base, r.entry, r.imports, r.bound, r.relocations);
    if (!r.fault.empty()) printf("play: fault kind=%s eip=0x%08x %s\n", r.fault_kind.c_str(), r.fault_eip, r.fault.c_str());
    for (auto& u : r.unresolved) printf("play: unresolved %s\n", u.c_str());
    printf("play: on_loaded calls=%u before-entry=%d tee-bytes=%zu\n", loaded_calls, loaded_first ? 1 : 0, seen.size());
    fwrite(seen.data(), 1, seen.size(), stdout);
    return r.finished && r.exit_code == 42 && loaded_calls == 1 && loaded_first ? 0 : 1;
}
