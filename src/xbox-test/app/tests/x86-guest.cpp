// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device run of a real i686 CRT program (mingw-built, PE32) through the
// interpreter and the guest OS surface in host/x86/x86_guest.h. The same exe
// runs under Wine (the cross-check): identical stdout and exit code. Usage:
// x86-guest <fixture-crt.exe> [--quiet]
#include <cstdio>
#include <fstream>
#include <iterator>
#include <filesystem>
#include <chrono>
#include "../host/x86/x86_guest.h"
#include "../host/x86/x86_file_guard.h"
using namespace x86;
static bool verify_file_guard() {
    namespace fs = std::filesystem;
    const fs::path temp = fs::temp_directory_path() / ("x86-file-guard-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(temp / "game", ec); fs::create_directories(temp / "LocalState", ec);
    fs::create_directories(temp / "otherGame", ec); fs::create_directories(temp / "outside", ec);
    fs::create_directories(temp / "known-folder-like", ec);
    const auto inside = temp / "game" / "owned.dat";
    const auto sibling = temp / "LocalState" / "steam_session.bin";
    const auto other = temp / "otherGame" / "file.dat";
    const auto outside = temp / "outside" / "file.dat";
    const auto knownFolder = temp / "known-folder-like" / "settings.ini";
    for (const auto& p : {inside, sibling, other, outside}) { std::ofstream f(p, std::ios::binary); f << "keep"; }
    unsigned refusals = 0;
    std::vector<std::u16string> protectedRoots{u"C:\\Data\\LocalState", u"D:\\XboxNativeGames", u"C:\\Program Files\\WindowsApps\\XboxNative"};
    std::vector<std::u16string> allowedRoots{u"D:\\XboxNativeGames\\Game", u"C:\\Data\\LocalState\\steam\\1234"};
    x86_file_guard::Policy guard(protectedRoots, allowedRoots, u"D:\\XboxNativeGames\\Game",
        [&](const std::string& message) { if (message.find("x86 file guard: refused ") == 0) ++refusals; });
    if (!guard.allow("DeleteFileA", u"\\\\?\\c:\\data\\xboxnativegames\\game\\sub/../owned.dat")) {
        fs::remove_all(temp, ec); std::fprintf(stderr, "X86-GUARD FAIL: normalized in-game path refused\\n"); return false;
    }
    if (guard.allow("DeleteFileA", u"owned.dat")) fs::remove(inside, ec);
    if (!guard.allow("CreateFileA(CREATE_ALWAYS)", u"C:\\Data\\Users\\Player\\AppData\\LocalLow\\Game\\settings.ini")) {
        fs::remove_all(temp, ec); std::fprintf(stderr, "X86-GUARD FAIL: known-folder write refused\\n"); return false;
    }
    { std::ofstream f(knownFolder, std::ios::binary | std::ios::trunc); f << "settings"; }
    if (!fs::exists(knownFolder) || !guard.allow("DeleteFileA", u"C:\\Data\\Users\\Player\\AppData\\LocalLow\\Game\\settings.ini")) {
        fs::remove_all(temp, ec); std::fprintf(stderr, "X86-GUARD FAIL: known-folder write/delete\n"); return false;
    }
    fs::remove(knownFolder, ec);
    if (fs::exists(knownFolder) || fs::exists(inside) ||
        !guard.allow("CreateFileA(CREATE_ALWAYS)", u"C:\\Data\\LocalState\\steam\\1234\\settings.ini") ||
        guard.allow("DeleteFileA", u"C:\\Data\\LocalState\\steam_session.bin") ||
        guard.allow("DeleteFileA", u"c:/data/localstate/STEAM_SESSION.bin") ||
        guard.allow("DeleteFileA", u"C:\\Data\\LocalState") ||
        guard.allow("DeleteFileA", u"C:\\Data\\LocalState\\logs\\launch.txt") ||
        guard.allow("DeleteFileA", u"..\\otherGame\\file.dat") ||
        guard.allow("DeleteFileA", u"D:\\XboxNativeGames\\OtherGame2\\file.dat") ||
        guard.allow("DeleteFileA", u"\\\\?\\D:\\XboxNativeGames\\outside\\file.dat") ||
        guard.allow("DeleteFileA", u"C:\\Program Files\\WindowsApps\\XboxNative\\main.exe") ||
        !fs::exists(sibling) || !fs::exists(other) || !fs::exists(outside) || refusals != 7) {
        fs::remove_all(temp, ec); std::fprintf(stderr, "X86-GUARD FAIL: containment or refusal logging\n"); return false;
    }
    fs::remove_all(temp, ec);
    std::puts("X86-GUARD PASS: in-game delete allowed; LocalState sibling, traversal and outside paths refused");
    return true;
}
static std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary); if (!f) { printf("FAIL cannot open %s\n", path); exit(2); }
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
    if (!verify_file_guard()) return 1;
    if (argc < 2) { printf("usage: x86-guest <fixture-crt.exe>\n"); return 2; }
    auto file = slurp(argv[1]);
    Memory mem; ThunkTable table; Host host; Registry reg;
    Guest guest(mem, host, reg);
    guest.exe_path = "fixture-crt.exe"; guest.cmdline = guest.exe_path;
    guest.register_all();
    Image img = load_pe32(mem, table, reg, file);
    guest.image_base = img.base;
    guest.exe_tls = img.tls;
    guest.add_module("fixture-crt.exe", img.base);
    map_thunk_page(mem, table);
    Cpu cpu(mem, table);
    init_process(mem, cpu, img);
    std::string fault;
    try { run_entry(cpu, img); } catch (const Fault& f) { fault = f.what(); }
    printf("guest: base=0x%08x entry=0x%08x imports=%u bound=%u relocs=%u\n", img.base, img.entry, img.imports, img.bound, img.relocations);
    printf("guest: instructions=%llu exit=%u%s%s\n", (unsigned long long)cpu.executed, cpu.exit_code,
           cpu.exited ? "" : " (not exited)", fault.empty() ? "" : (" fault: " + fault).c_str());
    fwrite(host.out.data(), 1, host.out.size(), stdout);
    if (!host.err.empty()) { fputs("--- stderr ---\n", stdout); fwrite(host.err.data(), 1, host.err.size(), stdout); }
    if (!fault.empty() || !cpu.exited) return 1;
    return int(cpu.exit_code);
}
