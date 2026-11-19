// SPDX-License-Identifier: GPL-3.0-or-later
// Focused guest Shell32 route test: paths, command-line parsing, refusal
// boundaries, and documented non-launching ShellExecute behavior.
#include <cstdio>
#include <initializer_list>
#include "../host/x86/x86_guest.h"
using namespace x86;

static int checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #value); } } while (0)

int main() {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000; guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000;
    auto route = [&](const char* name, unsigned argc) -> Thunk* {
        auto it = reg.map.find({"shell32.dll", Registry::key_sym(name)}); CHECK(it != reg.map.end());
        if (it == reg.map.end()) return nullptr;
        CHECK(it->second.conv == Conv::Stdcall); CHECK(it->second.argc == argc); return &it->second;
    };
    auto call = [&](const char* name, std::initializer_list<uint32_t> args) -> uint32_t {
        unsigned index = 0; for (uint32_t value : args) mem.w32(cpu.reg[ESP] + 4 + 4 * index++, value);
        return reg.map.at({"shell32.dll", Registry::key_sym(name)}).fn(cpu);
    };
    route("SHGetSpecialFolderPathA", 4); route("SHGetSpecialFolderPathW", 4); route("SHGetFolderPathA", 5); route("SHGetFolderPathW", 5);
    route("SHGetKnownFolderPath", 4); route("SHCreateDirectoryExA", 3); route("SHCreateDirectoryExW", 3);
    route("ShellExecuteA", 6); route("ShellExecuteW", 6); route("CommandLineToArgvW", 2); route("SHFileOperationA", 1); route("SHFileOperationW", 1);

    uint32_t command = guest.put_wstr(u"game.exe \"alpha beta\" tail\\");
    uint32_t argv = call("CommandLineToArgvW", {command, 0x4000});
    CHECK(argv != 0 && mem.r32(0x4000) == 3);
    if (argv) {
        CHECK(guest.wstr(mem.r32(argv)) == u"game.exe"); CHECK(guest.wstr(mem.r32(argv + 4)) == u"alpha beta");
        CHECK(guest.wstr(mem.r32(argv + 8)) == u"tail\\"); call("CommandLineToArgvW", {0, 0x4000});
    }
    uint32_t file = guest.put_str("missing-association"); CHECK(call("ShellExecuteA", {0, 0, file, 0, 0, 1}) == 31); CHECK(call("ShellExecuteW", {0, 0, command, 0, 0, 1}) == 31); CHECK(call("ShellExecuteW", {0, 0, 0, 0, 0, 1}) == 0);

    CHECK(call("SHGetSpecialFolderPathW", {0, 0x4100, 5, 0}) == 0); CHECK(call("SHGetSpecialFolderPathA", {0, 0x4200, 5, 0}) == 0);
    CHECK(call("SHGetFolderPathA", {0, 5, 0, 0, 0x4200}) == 0x80004001u);
    uint8_t known_id[16]{}; mem.write_bytes(0x4300, known_id, 16); CHECK(call("SHGetKnownFolderPath", {0x4300, 0, 0, 0x4400}) == 0x80004001u && mem.r32(0x4400) == 0);

    std::u16string guarded; bool allow = true; host.destructive_file_guard = [&](const char*, const std::u16string& path) { guarded = path; return allow; };
    host.file_call = [&](Host::FileCall& q) { CHECK(q.kind == Host::FileCall::Kind::CreateDirectoryW || q.kind == Host::FileCall::Kind::AttributesA); q.succeeded = true; };
    uint32_t dirw = guest.put_wstr(u"owned\\new"); CHECK(call("SHCreateDirectoryExW", {0, dirw, 0}) == 0 && guarded == u"owned\\new");
    allow = false; CHECK(call("SHCreateDirectoryExA", {0, file, 0}) == 5 && guarded == u"missing-association"); allow = true;

    uint32_t op = 0x5000, source = guest.put_str("owned.dat"); mem.w32(op + 4, 3); mem.w32(op + 8, source); mem.w32(op + 12, 0); mem.w32(op + 18, 0xdeadbeefu);
    CHECK(call("SHFileOperationA", {op}) == 120 && mem.r32(op + 18) == 0 && guarded == u"owned.dat");
    const char multi[] = "owned.dat\0outside.dat\0"; for (unsigned i = 0; i < sizeof(multi); ++i) mem.w8(0x5100 + i, uint8_t(multi[i]));
    mem.w32(op + 8, 0x5100); allow = true;
    CHECK(call("SHFileOperationA", {op}) == 120 && guarded == u"outside.dat");
    uint32_t outside = guest.put_wstr(u"outside.dat"); mem.w32(op + 8, outside); mem.w32(op + 18, 0); allow = false;
    CHECK(call("SHFileOperationW", {op}) == 5 && mem.r32(op + 18) == 1 && guarded == u"outside.dat");
    CHECK(call("SHFileOperationW", {0}) == 87);

    std::printf("x86-shell32: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
