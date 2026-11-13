// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device run of the stage-3 DLL fixture: a freestanding i686 PE32 exe
// that loads guest DLLs (exe -> dll-a -> dll-b), resolves imports between
// guest PEs, and calls LoadLibraryA/GetProcAddress/FreeLibrary/GetModuleHandle
// from guest code. The driver maps the exe, feeds the loader's DLL source
// from the fixture directory, runs DllMain(DLL_PROCESS_ATTACH) and the entry,
// and reports the structured result. Usage: x86-dll <fixture-dll.exe> <dir>
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../host/x86/x86_guest.h"
#include "../host/x86/x86_play.h"
using namespace x86;
static std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary); if (!f) { printf("FAIL cannot open %s\n", path); exit(2); }
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: x86-dll <fixture-dll.exe> <fixture-dir>\n"); return 2; }
    auto file = slurp(argv[1]);
    std::string dir = argv[2];
    // DLL search: the fixture directory, by module file name (dll-a.dll...).
    DllSource source = [dir](const std::string& name) {
        // The guest asks by module name; the fixture files carry .dll already.
        std::string path = dir + "/" + name;
        std::ifstream f(path, std::ios::binary);
        if (!f) return std::vector<uint8_t>{};
        return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
    };
    auto r = run_image(file, "Fixture-Dll.EXE", "\"Fixture-Dll.EXE\"", nullptr, nullptr,
                       500ull * 1000 * 1000, source);
    printf("play: loaded=%d finished=%d exit=%u base=0x%08x imports=%u bound=%u relocs=%u\n",
           r.loaded ? 1 : 0, r.finished ? 1 : 0, r.exit_code, r.base, r.imports, r.bound, r.relocations);
    for (const auto& u : r.unresolved) printf("play: unresolved %s\n", u.c_str());
    if (!r.fault.empty()) printf("play: fault kind=%s eip=0x%08x %s\n", r.fault_kind.c_str(), r.fault_eip, r.fault.c_str());
    printf("play: instructions=%llu\n", (unsigned long long)r.instructions);
    if (!r.finished || r.exit_code != 42 || !r.fault.empty() || !r.unresolved.empty()) return 1;
    printf("play: dll-chain loaded, all imports bound, exit 42\n");
    return 0;
}
