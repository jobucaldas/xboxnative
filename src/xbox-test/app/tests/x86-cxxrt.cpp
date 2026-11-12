// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device run of the stage-3 C++ runtime fixture: an ordinary i686 C++
// program (iostream, std::string, std::vector, exceptions, std::thread) built
// against the mingw C++ runtime, mapped and run through the interpreter with
// the full guest OS surface. The static CRT needs the kernel32/msvcrt/ntdll
// surface plus the stage-3 TLS-callback dispatch (mcfgthread initializes its
// globals from one). Usage: x86-cxxrt <fixture-cxx.exe>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../host/x86/x86_guest.h"
#include "../host/x86/x86_play.h"
using namespace x86;
int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: x86-cxxrt <fixture-cxx.exe>\n"); return 2; }
    std::ifstream f(argv[1], std::ios::binary);
    if (!f) { printf("FAIL cannot open %s\n", argv[1]); return 2; }
    std::vector<uint8_t> file{std::istreambuf_iterator<char>(f), {}};
    auto r = run_image(file, "Fixture-Cxx.EXE", "\"Fixture-Cxx.EXE\"");
    printf("play: loaded=%d finished=%d exit=%u base=0x%08x imports=%u bound=%u relocs=%u\n",
           r.loaded ? 1 : 0, r.finished ? 1 : 0, r.exit_code, r.base, r.imports, r.bound, r.relocations);
    for (const auto& u : r.unresolved) printf("play: unresolved %s\n", u.c_str());
    if (!r.fault.empty()) printf("play: fault kind=%s eip=0x%08x %s\n", r.fault_kind.c_str(), r.fault_eip, r.fault.c_str());
    printf("play: instructions=%llu\n", (unsigned long long)r.instructions);
    if (!r.finished || r.exit_code != 42 || !r.fault.empty() || !r.unresolved.empty()) return 1;
    printf("play: cxxrt ran to exit 42, all imports bound\n");
    return 0;
}
