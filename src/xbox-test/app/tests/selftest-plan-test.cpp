// SPDX-License-Identifier: GPL-3.0-or-later
// The console self-test's decisions, off-device: the fixture list, request
// parsing, the restart queue and what an exit code means. selftest.h runs the
// fixtures through Play on the console; this file is what makes its logic
// checkable without one.
#include "../selftest_plan.h"
#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using namespace selftest_plan;

    // The packaged fixtures, in the order a request runs them: the 32-bit one
    // first because it costs no launcher restart.
    size_t count = 0;
    const Mode* table = modes(count);
    assert(count == 13);
    assert(std::string(table[0].name) == "x86" && table[0].pe32);
    assert(std::string(table[1].name) == "vulkan-dzn" && !table[1].pe32);
    assert(std::string(table[2].name) == "vulkan-draw" && std::string(table[2].exe) == "play-vulkan-draw.exe" && !table[2].pe32);
    assert(std::string(table[3].name) == "d3d9" && std::string(table[7].name) == "d3d10");
    assert(std::string(table[4].name) == "d3d9-triangle" && std::string(table[5].name) == "d3d9-triangle-variants");
    assert(std::string(table[6].name) == "d3d9-textured");
    assert(std::string(table[8].name) == "d3d10-triangle" &&
           std::string(table[8].exe) == "play-d3d10-triangle.exe" && !table[8].pe32);
    assert(std::string(table[9].name) == "d3d10-compile" &&
           std::string(table[9].exe) == "play-d3d10-compile.exe" && !table[9].pe32);
    assert(std::string(table[10].name) == "opengl" && std::string(table[10].exe) == "play-opengl.exe" && !table[10].pe32);
    assert(std::string(table[11].name) == "d3d11" && std::string(table[11].exe) == "play-d3d11.exe" && !table[11].pe32);
    assert(std::string(table[12].name) == "d3d12" && std::string(table[12].exe) == "play-d3d12.exe" && !table[12].pe32);
    assert(find("d3d9") && find("d3d9")->pe32 == false);
    assert(find("d3d9-triangle") && std::string(find("d3d9-triangle")->exe) == "play-d3d9-triangle.exe");
    assert(find("d3d10-triangle") && std::string(find("d3d10-triangle")->exe) == "play-d3d10-triangle.exe");
    assert(find("d3d10-compile") && std::string(find("d3d10-compile")->exe) == "play-d3d10-compile.exe");
    assert(find("d3d11") && std::string(find("d3d11")->exe) == "play-d3d11.exe");
    assert(find("d3d12") && std::string(find("d3d12")->exe) == "play-d3d12.exe");
    assert(!find("d3d11")->pe32 && !find("d3d12")->pe32 && !find("d3d10-triangle")->pe32 && !find("d3d10-compile")->pe32);
    assert(find("x86") && find("x86")->pe32);
    assert(!find(""));

    // "all" must fit inside the restart cap: each 64-bit fixture costs one
    // launcher restart, so a cap below the guest count would mark the last
    // modes FAIL restart-limit without ever running them (which is what a
    // fixed cap of four did once the table grew past five guests).
    int guests = 0;
    for (size_t i = 0; i < count; ++i) if (!table[i].pe32) ++guests;
    assert(guests == 12);
    assert(max_restarts() > guests);
    assert(max_restarts() == guests + 1);

    // A request: one mode per line, blanks and comments ignored, names
    // case-insensitive, duplicates collapsed, unknown names kept (they are
    // reported as SKIP, never silently dropped).
    auto r = parse_request("vulkan-dzn\n\n# a comment\n D3D9 \n");
    assert(r.size() == 2 && r[0] == "vulkan-dzn" && r[1] == "d3d9");
    assert(parse_request("d3d9\nd3d9\n").size() == 1);
    assert(parse_request("").empty() && parse_request("#only a comment\n").empty());
    auto unknown = parse_request("nope\nx86\n");
    assert(unknown.size() == 2 && unknown[0] == "nope");
    // "all" expands to every fixture in canonical order, whatever else is named.
    auto all = parse_request("d3d10\nall\n");
    assert(all.size() == 13 && all[0] == "x86" && all[7] == "d3d10"
        && all[8] == "d3d10-triangle" && all[9] == "d3d10-compile" && all[10] == "opengl"
        && all[11] == "d3d11" && all[12] == "d3d12");
    assert(all[4] == "d3d9-triangle" && all[5] == "d3d9-triangle-variants");

    // The queue survives a launcher restart: restarts count and pending modes
    // round-trip; a malformed file is refused rather than half-read.
    Queue q;
    q.restarts = 2;
    q.pending = {"vulkan-dzn", "d3d9"};
    Queue back;
    assert(parse_queue(format_queue(q), back));
    assert(back.restarts == 2 && back.pending.size() == 2 && back.pending[1] == "d3d9");
    Queue empty;
    assert(parse_queue(format_queue(Queue{}), empty) && empty.restarts == 0 && empty.pending.empty());
    assert(!parse_queue("", empty));
    assert(!parse_queue("mode d3d9\n", empty));            // no restarts line: not ours
    assert(!parse_queue("restarts x\n", empty));           // not a number
    assert(!parse_queue("restarts 0\ngarbage\n", empty));  // unknown line

    // Exit codes. 42 is the fixtures' success; the Vulkan one has a BLOCKED
    // outcome (the host's D3D12 refused dzn's device) that is never a pass.
    assert(verdict("d3d9", true, true, 42) == "PASS");
    assert(verdict("d3d10", true, true, 42) == "PASS");
    assert(verdict("d3d10-triangle", true, true, 42) == "PASS");
    assert(verdict("d3d10-triangle", true, true, 43) == "BLOCKED pixel-readback");
    assert(verdict("d3d10-triangle", true, true, 44) == "FAIL exit-44");
    assert(verdict("d3d10-compile", true, true, 42) == "PASS");
    assert(verdict("d3d10-compile", true, true, 43) == "BLOCKED pixel-readback");
    assert(verdict("d3d10-compile", true, true, 44) == "FAIL exit-44");
    assert(verdict("d3d11", true, true, 42) == "PASS");
    assert(verdict("d3d12", true, true, 42) == "PASS");
    assert(verdict("x86", true, true, 42) == "PASS");
    assert(verdict("d3d9", true, true, 17) == "FAIL exit-17");
    assert(verdict("d3d11", true, true, 13) == "FAIL exit-13");
    assert(verdict("d3d12", true, true, 29) == "FAIL exit-29");
    assert(verdict("vulkan-dzn", true, true, 42) == "PASS frame-presented");
    assert(verdict("vulkan-dzn", true, true, 43) == "BLOCKED device-creation");
    assert(verdict("vulkan-dzn", true, true, 13) == "FAIL exit-13");
    // The draw fixture has a BLOCKED outcome too: the pipeline ran but the
    // pixels (or the compute buffer) did not read back as drawn.
    assert(verdict("vulkan-draw", true, true, 42) == "PASS");
    assert(verdict("vulkan-draw", true, true, 43) == "BLOCKED pixel-readback");
    assert(verdict("vulkan-draw", true, true, 13) == "FAIL exit-13");
    // The OpenGL one has a BLOCKED outcome too: everything up to the context
    // worked but the rendered pixels did not match (the host drew nothing).
    assert(verdict("opengl", true, true, 42) == "PASS");
    assert(verdict("opengl", true, true, 43) == "BLOCKED pixel-readback");
    assert(verdict("opengl", true, true, 13) == "FAIL exit-13");
    // The D3D11/D3D12 ones block the same way: everything ran but the pixels
    // did not read back as drawn (never a pass).
    assert(verdict("d3d11", true, true, 43) == "BLOCKED pixel-readback");
    assert(verdict("d3d12", true, true, 43) == "BLOCKED pixel-readback");
    // The D3D11 fixture's newer passes block per feature: the calls succeeded
    // but the effect never reached the readback (never a pass either).
    assert(verdict("d3d11", true, true, 54) == "BLOCKED msaa-resolve");
    assert(verdict("d3d11", true, true, 58) == "BLOCKED generate-mips");
    assert(verdict("d3d11", true, true, 63) == "BLOCKED command-list");
    assert(verdict("d3d11", true, true, 67) == "BLOCKED stream-out");
    assert(verdict("d3d11", true, true, 71) == "BLOCKED indirect-draw");
    assert(verdict("d3d11", true, true, 75) == "BLOCKED occlusion-query");
    assert(verdict("d3d11", true, true, 82) == "BLOCKED indexed-draw");
    assert(verdict("d3d11", true, true, 86) == "BLOCKED depth-test");
    assert(verdict("d3d11", true, true, 90) == "BLOCKED texture-array-copy");
    assert(verdict("d3d11", true, true, 94) == "BLOCKED bc1-copy");
    // 95 = a Wine host: the gated passes did not run, so never a pass.
    assert(verdict("d3d11", true, true, 95) == "BLOCKED wine-host");
    assert(verdict("d3d11", true, true, 50) == "FAIL exit-50");   // a step failure stays a failure
    assert(verdict("d3d12", true, true, 54) == "FAIL exit-54");   // not the d3d12 fixture's code
    // A guest that never ran code, or ran it and never exited, is a failure.
    assert(verdict("d3d9", true, false, 0) == "FAIL no-guest-code");
    assert(verdict("d3d9", false, true, 0) == "FAIL did-not-exit");
    assert(verdict("vulkan-dzn", false, true, 0) == "FAIL did-not-exit");

    // The summary counts the log's result lines, including ones written by
    // earlier launcher processes (the log is appended across restarts).
    std::string log =
        "SELFTEST BEGIN request=4 modes\n"
        "SELFTEST RESULT x86 PASS\n"
        "SELFTEST RESULT vulkan-dzn BLOCKED device-creation result=-9\n"
        "SELFTEST RESULT d3d9 FAIL exit-17\n"
        "SELFTEST RESULT d3d10 SKIP not-packaged\n"
        "SELFTEST SUMMARY pass=1 fail=1 blocked=1 skip=1\n";
    auto t = tally(log);
    assert(t.pass == 1 && t.fail == 1 && t.blocked == 1 && t.skip == 1);
    assert(tally("").pass == 0 && tally("SELFTEST RESULT\n").pass == 0);
    // A detail line never changes the verdict word it counts.
    assert(tally("SELFTEST RESULT d3d9 PASS frame-presented\n").pass == 1);

    std::puts("SELFTEST-PLAN-OK: fixtures, request parsing, restart queue and exit-code verdicts");
}
