// SPDX-License-Identifier: GPL-3.0-or-later
// x86_route.h: the launcher's PE32 result mapping, as pure functions. A clean
// exit 0 is success; every other ending names the first unresolved import or
// the unsupported opcode, and the log carries a "PRUN EVENT x86 route:" line.
// These cases are the ones a real 32-bit game hits before it can run.
#include "../x86_route.h"
#include <cstdio>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

int main() {
    // Clean exit 0: success, and the log says the engine and the exit code.
    {
        x86::PlayResult r; r.loaded=true; r.finished=true; r.exit_code=0; r.instructions=1234;
        auto rep=x86_route::report(r,"game.exe");
        CHECK(rep.ok);
        CHECK(rep.result=="game.exe completed in the x86 interpreter (exit code 0)");
        CHECK(rep.lines.size()==1 && rep.lines[0]=="PRUN EVENT x86 route: game.exe exited code=0 instructions=1234");
    }
    // Nonzero exit: a failure naming the code, not a silent fallback.
    {
        x86::PlayResult r; r.loaded=true; r.finished=true; r.exit_code=3;
        auto rep=x86_route::report(r,"game.exe");
        CHECK(!rep.ok);
        CHECK(rep.result=="32-bit guest game.exe exited with code 3");
        CHECK(rep.lines.size()==1 && rep.lines[0]=="PRUN EVENT x86 route: game.exe exited code=3 instructions=0");
    }
    // A nonzero exit with an unresolved import names the import too.
    {
        x86::PlayResult r; r.loaded=true; r.finished=true; r.exit_code=1;
        r.unresolved.push_back("d3d11.dll!D3D11CreateDevice");
        auto rep=x86_route::report(r,"game.exe");
        CHECK(!rep.ok);
        CHECK(rep.result.find("first unresolved import d3d11.dll!D3D11CreateDevice")!=std::string::npos);
        CHECK(rep.lines.size()==2 && rep.lines[1]=="PRUN EVENT x86 route: first unresolved import d3d11.dll!D3D11CreateDevice");
    }
    // A fault on an unresolved import names the import from the fault text
    // itself (the IAT stub's message), not from the unresolved list.
    {
        x86::PlayResult r; r.loaded=true; r.fault_kind="UnresolvedImport"; r.fault_eip=0x00401234;
        r.fault="call to unresolved import user32.dll!GetMessageA";
        r.unresolved.push_back("user32.dll!GetMessageA");
        auto rep=x86_route::report(r,"game.exe");
        CHECK(!rep.ok);
        CHECK(rep.result=="32-bit guest game.exe: called call to unresolved import user32.dll!GetMessageA");
        CHECK(rep.lines.size()==1 && rep.lines[0]=="PRUN EVENT x86 route: game.exe fault kind=UnresolvedImport eip=0x00401234: called call to unresolved import user32.dll!GetMessageA");
    }
    // GetProcAddress raises the same fault kind with its own message and is
    // not an IAT stub: the fault text must stand, never the first unresolved
    // IAT entry (which may be an import that was never called).
    {
        x86::PlayResult r; r.loaded=true; r.fault_kind="UnresolvedImport"; r.fault_eip=0x00401240;
        r.fault="GetProcAddress: no export table for guest modules";
        r.unresolved.push_back("d3d11.dll!D3D11CreateDevice");
        auto rep=x86_route::report(r,"game.exe");
        CHECK(!rep.ok);
        CHECK(rep.result=="32-bit guest game.exe: called unresolved import GetProcAddress: no export table for guest modules");
    }
    // An unsupported opcode carries the fault text (opcode bytes) verbatim.
    {
        x86::PlayResult r; r.loaded=true; r.fault_kind="Unsupported"; r.fault_eip=0x00401000;
        r.fault="unsupported x86 instruction (fild) at 0x00401000: 66 db 40 08";
        auto rep=x86_route::report(r,"game.exe");
        CHECK(!rep.ok);
        CHECK(rep.result=="32-bit guest game.exe: unsupported x86 instruction (fild) at 0x00401000: 66 db 40 08");
    }
    // An image the loader refused: no guest code ran, the loader's reason stands.
    {
        x86::PlayResult r; r.fault_kind="Loader"; r.fault="cannot open the executable";
        auto rep=x86_route::report(r,"game.exe");
        CHECK(!rep.ok);
        CHECK(rep.result=="the x86 loader could not map game.exe: cannot open the executable");
        CHECK(rep.lines.size()==1 && rep.lines[0]=="PRUN EVENT x86 route: game.exe was not mapped by the x86 loader: cannot open the executable");
    }
    // A guest that neither exited nor faulted (the cap keeps this finite, but
    // the mapping must still say so rather than claim success).
    {
        x86::PlayResult r; r.loaded=true;
        auto rep=x86_route::report(r,"game.exe");
        CHECK(!rep.ok);
        CHECK(rep.result=="32-bit guest game.exe: the 32-bit guest did not exit");
    }
    // Every unresolved import is named, one "x86 gap:" line each, grouped by
    // DLL with IAT order kept inside a DLL, then the full total. The lines are
    // in rep.gap, so the exit-path lines above stay exactly as they were.
    {
        x86::PlayResult r; r.loaded=true; r.finished=true; r.exit_code=0;
        r.unresolved={"steam_api.dll!SteamAPI_Init","kernel32.dll!CreateEventW",
                      "steam_api.dll!SteamAPI_Shutdown","kernel32.dll!SetEvent"};
        auto rep=x86_route::report(r,"game.exe");
        std::vector<std::string> want={"x86 gap: kernel32.dll!CreateEventW","x86 gap: kernel32.dll!SetEvent",
                                       "x86 gap: steam_api.dll!SteamAPI_Init","x86 gap: steam_api.dll!SteamAPI_Shutdown",
                                       "x86 gap: total=4"};
        CHECK(rep.gap==want);
        // The exit line and the existing "first unresolved import" line; no gap line leaks into lines.
        CHECK(rep.lines.size()==2 && rep.ok);
    }
    // No unresolved import: the total still prints, as zero.
    {
        x86::PlayResult r; r.loaded=true; r.finished=true; r.exit_code=0;
        auto rep=x86_route::report(r,"game.exe");
        CHECK(rep.gap==std::vector<std::string>{"x86 gap: total=0"});
    }
    // A faulted run reports its gap too (the fault line is unchanged).
    {
        x86::PlayResult r; r.loaded=true; r.fault_kind="UnresolvedImport"; r.fault_eip=0x00401234;
        r.fault="call to unresolved import user32.dll!GetMessageA";
        r.unresolved.push_back("user32.dll!GetMessageA");
        auto rep=x86_route::report(r,"game.exe");
        CHECK(rep.gap==(std::vector<std::string>{"x86 gap: user32.dll!GetMessageA","x86 gap: total=1"}));
        CHECK(rep.lines.size()==1);
    }
    // A table larger than the bound: 600 names, a truncation line, the full total.
    {
        x86::PlayResult r; r.loaded=true; r.finished=true; r.exit_code=0;
        for (int i=0;i<601;++i) r.unresolved.push_back("big.dll!f"+std::to_string(i));
        auto rep=x86_route::report(r,"game.exe");
        CHECK(rep.gap.size()==602);
        CHECK(rep.gap.front()=="x86 gap: big.dll!f0");
        CHECK(rep.gap[599]=="x86 gap: big.dll!f599");
        CHECK(rep.gap[600]=="x86 gap: truncated after 600 names");
        CHECK(rep.gap[601]=="x86 gap: total=601");
    }
    // An image the loader refused has no gap block.
    {
        x86::PlayResult r; r.fault_kind="Loader"; r.fault="cannot open the executable";
        auto rep=x86_route::report(r,"game.exe");
        CHECK(rep.gap.empty());
    }
    printf("x86-route: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
