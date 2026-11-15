// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include "../host/x86/x86_guest.h"
using namespace x86;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x); ++failures; } } while (0)
int main() {
    Memory mem; mem.map(0x1000, 0x20000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable th; Host host; Registry reg; Guest guest(mem, host, reg);
    guest.register_all(); Cpu cpu(mem, th); cpu.reg[ESP] = 0x1f000;
    auto it = reg.map.find({"msvcr110.dll", "__crtgetshowwindowmode"});
    CHECK(it != reg.map.end());
    if (it != reg.map.end()) {
        CHECK(it->second.conv == Conv::Cdecl);
        CHECK(it->second.argc == 0);
        CHECK(it->second.fn(cpu) == 10);  // SW_SHOWDEFAULT when STARTF_USESHOWWINDOW is clear
        mem.w32(guest.startupinfo + 44, 1);  // STARTUPINFO.dwFlags: STARTF_USESHOWWINDOW
        mem.w(guest.startupinfo + 48, 3, 2); // STARTUPINFO.wShowWindow
        CHECK(it->second.fn(cpu) == 3);
    }
    std::printf("x86-crt-startup: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
