// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); ++failures; } } while (0)
int main() {
    Memory mem; mem.map(0x1000, 0x20000, PROT_R | PROT_W);
    ThunkTable th; Host host; Registry reg; Guest guest(mem, host, reg);
    guest.register_all(); Cpu cpu(mem, th); cpu.reg[ESP] = 0x1f000;
    auto it = reg.map.find({"kernel32.dll", "isvalidcodepage"});
    CHECK(it != reg.map.end());
    if (it != reg.map.end()) {
        CHECK(it->second.conv == Conv::Stdcall);
        CHECK(it->second.argc == 1);
        auto valid = [&](uint32_t cp) {
            mem.w32(cpu.reg[ESP] + 4, cp);
            return it->second.fn(cpu);
        };
        const uint32_t supported[] = {
            437, 850, 932, 936, 949, 950, 1200, 1201,
            1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257, 1258,
            20127, 28591, 65000, 65001
        };
        for (uint32_t cp : supported) CHECK(valid(cp) == 1);
        CHECK(valid(0) == 0);
        CHECK(valid(42) == 0);
        CHECK(valid(65002) == 0);
    }
    std::printf("x86-nls: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
