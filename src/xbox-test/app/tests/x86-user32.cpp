// SPDX-License-Identifier: GPL-3.0-or-later
// Focused guest user32 contract test for the remaining 411370 imports.
#include <cstdio>
#include <initializer_list>
#include <string>
#include "../host/x86/x86_guest.h"
using namespace x86;

static int checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #value); } } while (0)

int main() {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000; guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000; cpu.fs_base = guest.cur_teb;
    struct Route { const char* name; unsigned argc; Conv conv; } routes[] = {
        {"BeginPaint", 2, Conv::Stdcall}, {"CheckDlgButton", 3, Conv::Stdcall}, {"CheckMenuItem", 3, Conv::Stdcall},
        {"CheckRadioButton", 4, Conv::Stdcall}, {"CreateDialogParamA", 5, Conv::Stdcall}, {"DestroyMenu", 1, Conv::Stdcall},
        {"DialogBoxParamA", 5, Conv::Stdcall}, {"EnableWindow", 2, Conv::Stdcall}, {"EndPaint", 2, Conv::Stdcall},
        {"FindWindowA", 2, Conv::Stdcall}, {"GetDlgCtrlID", 1, Conv::Stdcall}, {"GetDlgItemTextA", 4, Conv::Stdcall},
        {"GetLastActivePopup", 1, Conv::Stdcall}, {"GetMenuState", 3, Conv::Stdcall}, {"GetSubMenu", 2, Conv::Stdcall},
        {"IsDlgButtonChecked", 2, Conv::Stdcall}, {"IsWindowEnabled", 1, Conv::Stdcall}, {"KillTimer", 2, Conv::Stdcall},
        {"LoadMenuA", 2, Conv::Stdcall}, {"PostThreadMessageA", 4, Conv::Stdcall}, {"SendDlgItemMessageA", 5, Conv::Stdcall},
        {"SetDlgItemTextA", 3, Conv::Stdcall}, {"SetTimer", 4, Conv::Stdcall}, {"SetRect", 5, Conv::Stdcall},
        {"wsprintfA", 2, Conv::Cdecl},
    };
    for (const auto& route : routes) {
        auto it = reg.map.find({"user32.dll", Registry::key_sym(route.name)});
        CHECK(it != reg.map.end());
        if (it != reg.map.end()) { CHECK(it->second.conv == route.conv); CHECK(it->second.argc == route.argc); }
    }
    auto call = [&](const char* name, std::initializer_list<uint32_t> args) -> uint32_t {
        unsigned index = 0; for (uint32_t value : args) mem.w32(cpu.reg[ESP] + 4 + 4 * index++, value);
        return reg.map.at({"user32.dll", Registry::key_sym(name)}).fn(cpu);
    };
    uint32_t rect = 0x4000;
    CHECK(call("SetRect", {rect, 1, 2, 31, 42}) == 1);
    CHECK(mem.r32(rect) == 1 && mem.r32(rect + 4) == 2 && mem.r32(rect + 8) == 31 && mem.r32(rect + 12) == 42);
    guest.set_last_error(7); CHECK(call("BeginPaint", {0, 0}) == 0 && guest.last_error() == 120);
    CHECK(call("DialogBoxParamA", {0, 0, 0, 0, 0}) == uint32_t(-1) && guest.last_error() == 120);
    CHECK(call("SetTimer", {0, 1, 16, 0}) == 0 && guest.last_error() == 120);
    CHECK(call("wsprintfA", {0, 0}) == 0 && guest.last_error() == 120);
    CHECK(host.calls["unsupported:BeginPaint"] == 1 && host.calls["unsupported:DialogBoxParamA"] == 1);
    std::printf("x86-user32: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
