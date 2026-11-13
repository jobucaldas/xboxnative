// SPDX-License-Identifier: GPL-3.0-or-later
// Off-device run of a real i686 CRT program (mingw-built, PE32) through the
// interpreter and the guest OS surface in host/x86/x86_guest.h. The same exe
// runs under Wine (the cross-check): identical stdout and exit code. Usage:
// x86-guest <fixture-crt.exe> [--quiet]
#include <cstdio>
#include <fstream>
#include <iterator>
#include "../host/x86/x86_guest.h"
using namespace x86;
static std::vector<uint8_t> slurp(const char* path) {
    std::ifstream f(path, std::ios::binary); if (!f) { printf("FAIL cannot open %s\n", path); exit(2); }
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
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
