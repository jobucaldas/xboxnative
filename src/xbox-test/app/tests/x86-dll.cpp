// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device run of the stage-3 DLL fixture: a freestanding i686 PE32 exe
// that loads guest DLLs (exe -> dll-a -> dll-b), resolves imports between
// guest PEs, and calls LoadLibraryA/GetProcAddress/FreeLibrary/GetModuleHandle
// from guest code. The driver maps the exe, feeds the loader's DLL source
// from the fixture directory, runs DllMain(DLL_PROCESS_ATTACH) and the entry,
// and reports the structured result. Usage: x86-dll <fixture-dll.exe> <dir>
#include <algorithm>
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
static bool test_base_collision(const std::vector<uint8_t>& dll, bool expect_relocation) {
    constexpr uint32_t preferred = 0x68000000u, header_size = 0x10000u;
    Memory mem;
    mem.map(preferred, header_size, PROT_R | PROT_W);
    std::vector<uint8_t> header(header_size);
    for (size_t i = 0; i < header.size(); ++i) header[i] = uint8_t(i * 31 + 7);
    const uint8_t signature[] = {'M','Z',0x58,0x42,'P','E',0,0};
    std::copy(signature, signature + sizeof signature, header.begin());
    mem.write_bytes(preferred, header.data(), header_size);
    mem.protect(preferred, header_size, PROT_R);
    ThunkTable table; map_thunk_page(mem, table);
    mem.map(0x00200000, 0x10000, PROT_R | PROT_W);
    Registry reg; std::map<std::string, Dll> loaded;
    try {
        Dll image = load_dll(mem, table, reg, dll, {}, loaded, 0, nullptr, nullptr, "collision.dll");
        if (!expect_relocation || image.image.base == preferred || !image.image.relocations) return false;
        Cpu cpu(mem, table); cpu.reg[ESP] = 0x00210000;
        uint32_t fn = image.image.base + image.image.exports.at("collision_fn");
        if (cpu.call(fn, {}) != 42) return false;
        printf("x86-dll: collision relocated base=0x%08x relocs=%u export=42\n", image.image.base, image.image.relocations);
    } catch (const std::exception& e) {
        if (expect_relocation || std::string(e.what()).find("PE32 relocation directory absent") == std::string::npos) {
            printf("x86-dll: collision unexpected failure: %s\n", e.what()); return false;
        }
        printf("x86-dll: stripped collision rejected: %s\n", e.what());
    }
    for (size_t i = 0; i < header.size(); ++i) if (mem.r8(preferred + uint32_t(i)) != header[i]) return false;
    return true;
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
    if (!r.finished || r.exit_code != 42 || !r.fault.empty() || !r.unresolved.empty()) {
        if (r.finished && (r.exit_code & 0xC0000000u) == 0x40000000u)
            printf("x86-dll: FIRST FAIL id=%u line=%u\n", r.exit_code & 0xFFFFu, (r.exit_code >> 16) & 0x3FFFu);
        else
            printf("x86-dll: FIRST FAIL id=unavailable (fixture did not return an encoded check failure)\n");
        return 1;
    }
    printf("play: dll-chain loaded, all imports bound, exit 42\n");
    auto collision_reloc = slurp((dir + "/dll-collision-reloc.dll").c_str());
    auto collision_stripped = slurp((dir + "/dll-collision-stripped.dll").c_str());
    if (!test_base_collision(collision_reloc, true) || !test_base_collision(collision_stripped, false)) {
        printf("X86-DLL FAIL: image-base collision behavior\n"); return 1;
    }
    printf("X86-DLL PASS: preferred-base collision relocates safely or rejects stripped image without changing pseudo-module headers\n");
    return 0;
}
