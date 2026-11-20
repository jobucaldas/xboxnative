// SPDX-License-Identifier: GPL-3.0-or-later
// WSAGetLastError must report the error the Winsock route left in the thread's
// last-error slot: unsupported stubs write it directly, host-backed calls copy the
// host's error into it on failure.
#include <cstdio>
#include "../host/x86/x86_play.h"
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

int main() {
    using namespace x86;
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry registry; register_ws2(registry, host);
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000; cpu.fs_base = TEB_ADDR;
    auto route = [&](const char* dll, const char* name) -> Thunk* {
        auto it = registry.map.find({dll, Registry::key_sym(name)});
        return it == registry.map.end() ? nullptr : &it->second;
    };
    auto args = [&](std::initializer_list<uint32_t> values) {
        unsigned slot = 0; for (uint32_t value : values) mem.w32(cpu.reg[ESP] + 4 + 4 * slot++, value);
    };
    // A failing host: WSAStartup returns its code directly, WSACleanup returns SOCKET_ERROR; both carry the host error.
    host.socket_call = [](Host::SocketCall& q) {
        if (q.kind == Host::SocketCall::Kind::Startup) { q.result = 10093; q.last_error = 10093; }
        else if (q.kind == Host::SocketCall::Kind::Cleanup) { q.result = -1; q.last_error = 10038; }
    };
    auto accept = route("ws2_32.dll", "accept"), wsa = route("ws2_32.dll", "WSAGetLastError");
    auto startup = route("ws2_32.dll", "WSAStartup"), cleanup = route("ws2_32.dll", "WSACleanup");
    CHECK(accept && wsa && startup && cleanup);
    if (!accept || !wsa || !startup || !cleanup) { printf("x86-wsa-error: checks=%d failed=%d\n", checks, failures + 1); return 1; }

    // Unsupported stub: the route writes the slot itself.
    mem.w32(cpu.fs_base + 0x34, 0);
    accept->fn(cpu);
    CHECK(mem.r32(cpu.fs_base + 0x34) == 10045u);
    CHECK(wsa->fn(cpu) == 10045u);

    // Host-backed failing WSAStartup(version, data): the host error reaches WSAGetLastError.
    mem.w32(cpu.fs_base + 0x34, 0);
    args({0x0202, 0x5000}); const uint32_t started = startup->fn(cpu);
    CHECK(started == 10093u);
    CHECK(mem.r32(cpu.fs_base + 0x34) == 10093u);
    CHECK(wsa->fn(cpu) == 10093u);

    // Host-backed failing WSACleanup(): SOCKET_ERROR with the host's error.
    mem.w32(cpu.fs_base + 0x34, 0);
    const uint32_t cleaned = cleanup->fn(cpu);
    CHECK(cleaned == 0xFFFFFFFFu);
    CHECK(wsa->fn(cpu) == 10038u);
    printf("x86-wsa-error: checks=%d failed=%d\n", checks, failures);
    return failures ? 1 : 0;
}
