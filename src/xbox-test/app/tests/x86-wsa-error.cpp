// SPDX-License-Identifier: GPL-3.0-or-later
// WSAGetLastError must report the error an unsupported Winsock stub left in the
// thread's last-error slot, the same slot the guest reads for other failures.
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
    auto accept = route("ws2_32.dll", "accept"), wsa = route("ws2_32.dll", "WSAGetLastError");
    CHECK(accept && wsa);
    if (!accept || !wsa) { printf("x86-wsa-error: checks=%d failed=%d\n", checks, failures + 1); return 1; }
    mem.w32(cpu.fs_base + 0x34, 0);
    accept->fn(cpu);
    CHECK(mem.r32(cpu.fs_base + 0x34) == 10045u);
    CHECK(wsa->fn(cpu) == 10045u);
    printf("x86-wsa-error: checks=%d failed=%d\n", checks, failures);
    return failures ? 1 : 0;
}
