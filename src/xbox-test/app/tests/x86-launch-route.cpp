// SPDX-License-Identifier: GPL-3.0-or-later
// The launcher's PE32 route end to end, off-device: build the owned i686
// fixture (freestanding, no CRT), then run it exactly as launch.h's
// lp_run_pe32 does — x86::run reads the file from disk, tees guest stdout,
// raises the cap to kGuestRunCap — and map the PlayResult through
// x86_route::report, the same function the launch result is built from.
// Asserts the fixture's exit code 42, its exact stdout, and that the route
// reports success with the "PRUN EVENT x86 route:" line. Usage:
// x86-launch-route <fixture.exe>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include "../x86_route.h"
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: x86-launch-route <fixture.exe>\n"); return 2; }
    std::string seen;   // every tee call, in order
    bool running_published = false;
    // x86::run (the function lp_run_pe32 calls) is Windows-only; on a host
    // test the same path is: read the file, then run_image with the same
    // name, command line, tee, pre-entry callback and the game-sized cap.
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { printf("FAIL cannot open %s\n", argv[1]); return 2; }
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(f), {}};
    auto r = x86::run_image(file, "fixture.exe", "\"fixture.exe\"",
        [&seen](const std::string& s) { seen += s; },
        [&](const x86::PlayResult&) { running_published = true; },
        x86::kGuestRunCap);
    auto rep = x86_route::report(r, "fixture.exe");
    for (auto& l : rep.lines) printf("sink: %s\n", l.c_str());
    printf("route: ok=%d result=%s\n", rep.ok ? 1 : 0, rep.result.c_str());
    printf("play: loaded=%d finished=%d exit=%u imports=%u bound=%u\n",
           r.loaded ? 1 : 0, r.finished ? 1 : 0, r.exit_code, r.imports, r.bound);
    fwrite(seen.data(), 1, seen.size(), stdout);
    // The fixture's exact stdout (the same bytes Wine produces), the exit
    // code, and the route's verdict must all agree. The fixture exits 42 on
    // purpose (the core suites' contract); the launcher's rule is a clean
    // exit 0, so the route correctly reports the 42 as a failure naming the
    // code — the launch result a player sees — while the run itself is exact.
    const char* want =
        "x86-fixture: start\n"
        "x86-fixture: teb/peb ok\n"
        "crc=1632946162\n"
        "hello from 32-bit\n"
        "checks=15\n"
        "failed=0\n";   // the tee carries each line's newline
    bool ok = !rep.ok && rep.result == "32-bit guest fixture.exe exited with code 42" &&
              r.finished && r.exit_code == 42 && running_published &&
              r.imports == r.bound && r.unresolved.empty() && seen == want &&
              rep.lines.size() == 1 &&
              rep.lines[0] == "PRUN EVENT x86 route: fixture.exe exited code=42 instructions=" + std::to_string(r.instructions);
    if (!ok) printf("FAIL: route did not report the fixture's run exactly\n");
    else printf("PASS: the launcher's PE32 route ran the fixture to exit 42, stdout identical, result names the code\n");
    return ok ? 0 : 1;
}
