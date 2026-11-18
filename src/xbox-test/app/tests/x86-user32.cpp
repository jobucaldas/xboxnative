// SPDX-License-Identifier: GPL-3.0-or-later
// Focused guest user32 contract test for the remaining 411370 imports.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <vector>
#include "../host/x86/x86_play.h"
using namespace x86;

static int checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #value); } } while (0)

struct Posted { uintptr_t hwnd; uint32_t message, wp, lp, time; int32_t x, y; };
static bool run_timer_guest(const std::vector<uint8_t>& file, std::string& failure) {
    std::vector<Posted> queue;
    auto result = x86::run_image(file, "x86-user32.exe", "x86-user32.exe", nullptr, nullptr, 20'000'000, nullptr, false, nullptr, nullptr,
        [&queue](x86::Host& host) {
            host.register_window_class = [](const std::u16string& name) { return name == u"X86TimerWindow"; };
            host.create_window = [](const x86::Host::WindowCreate& q) -> uintptr_t {
                return q.class_name == u"X86TimerWindow" ? uintptr_t(0x1234) : 0;
            };
            host.destroy_window = [](uintptr_t hwnd) { return hwnd == 0x1234; };
            host.window_call = [&queue](x86::Host::WindowCall& q) {
                if (q.name == "PostMessageA" || q.name == "PostMessageW") {
                    queue.push_back({q.args[0], uint32_t(q.args[1]), uint32_t(q.args[2]), uint32_t(q.args[3]), 0, 0, 0});
                    q.result = 1; q.supported = true; return;
                }
                if (q.name == "PeekMessageA" || q.name == "PeekMessageW" || q.name == "GetMessageA" || q.name == "GetMessageW") {
                    const uintptr_t filter = q.args[1]; const uint32_t first = uint32_t(q.args[2]), last = uint32_t(q.args[3]);
                    auto it = std::find_if(queue.begin(), queue.end(), [filter, first, last](const Posted& message) {
                        return (message.message == 0x0012u || !filter || message.hwnd == filter) && ((!first && !last) || (message.message >= first && message.message <= last));
                    });
                    q.result = it == queue.end() ? 0 : 1; q.supported = true;
                    if (it != queue.end()) { q.output.resize(sizeof(Posted)); std::memcpy(q.output.data(), &*it, sizeof(Posted)); if (q.args[4] & 1) queue.erase(it); }
                    return;
                }
                if (q.name == "PostQuitMessage") {
                    queue.push_back({0, 0x0012u, uint32_t(q.args[0]), 0, 0, 0, 0}); q.result = 0; q.supported = true; return;
                }
                q.result = 0; q.supported = true;
            };
        });
    if (!result.loaded || !result.finished || result.exit_code || !result.unresolved.empty()) {
        failure = "loaded=" + std::to_string(result.loaded) + " finished=" + std::to_string(result.finished) +
            " exit=" + std::to_string(result.exit_code) + " fault=" + result.fault + " unresolved=" + std::to_string(result.unresolved.size());
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000; guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000; cpu.fs_base = guest.cur_teb;
    struct Route { const char* name; unsigned argc; Conv conv; } routes[] = {
        {"AllowSetForegroundWindow", 1, Conv::Stdcall}, {"BeginPaint", 2, Conv::Stdcall}, {"CheckDlgButton", 3, Conv::Stdcall}, {"CheckMenuItem", 3, Conv::Stdcall},
        {"CheckRadioButton", 4, Conv::Stdcall}, {"CreateDialogParamA", 5, Conv::Stdcall}, {"DestroyMenu", 1, Conv::Stdcall},
        {"DialogBoxParamA", 5, Conv::Stdcall}, {"EnableWindow", 2, Conv::Stdcall}, {"EndPaint", 2, Conv::Stdcall},
        {"FindWindowA", 2, Conv::Stdcall}, {"GetDlgCtrlID", 1, Conv::Stdcall}, {"GetDlgItemTextA", 4, Conv::Stdcall}, {"GetParent", 1, Conv::Stdcall}, {"GetWindowLongPtrW", 2, Conv::Stdcall}, {"MonitorFromRect", 2, Conv::Stdcall}, {"RegisterDeviceNotificationW", 3, Conv::Stdcall}, {"UnregisterDeviceNotification", 1, Conv::Stdcall},
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
    host.window_handles[0x1111] = 0xaaaa; host.window_handles[0x2222] = 0xbbbb;
    host.window_call = [](Host::WindowCall& q) {
        if (q.name == "GetParent") { CHECK(q.args[0] == 0xaaaa); q.result = 0xbbbb; q.supported = true; }
        else if (q.name == "AllowSetForegroundWindow") { CHECK(q.args[0] == 0xffffffffu); q.result = 1; q.supported = true; }
    };
    host.get_window_long = [](uintptr_t hwnd, int index, bool pointer) -> intptr_t {
        CHECK(hwnd == 0xaaaa && index == 0 && pointer); return intptr_t(uint32_t(0xfedcba98u));
    };
    CHECK(call("GetParent", {0x1111}) == 0x2222);
    CHECK(call("GetWindowLongPtrW", {0x1111, 0}) == 0xfedcba98u);
    CHECK(call("AllowSetForegroundWindow", {0xffffffffu}) == 1);
    guest.set_last_error(7); CHECK(call("MonitorFromRect", {0x4000, 2}) == 0 && guest.last_error() == 120);
    guest.set_last_error(7); CHECK(call("RegisterDeviceNotificationW", {1, 2, 1}) == 0 && guest.last_error() == 120);
    guest.set_last_error(7); CHECK(call("UnregisterDeviceNotification", {0xd1000001u}) == 0 && guest.last_error() == 6);
    uint32_t rect = 0x4000;
    CHECK(call("SetRect", {rect, 1, 2, 31, 42}) == 1);
    CHECK(mem.r32(rect) == 1 && mem.r32(rect + 4) == 2 && mem.r32(rect + 8) == 31 && mem.r32(rect + 12) == 42);
    guest.set_last_error(7); CHECK(call("BeginPaint", {0, 0}) == 0 && guest.last_error() == 120);
    CHECK(call("DialogBoxParamA", {0, 0, 0, 0, 0}) == uint32_t(-1) && guest.last_error() == 120);
    CHECK(call("SetTimer", {0, 1, 16, 0}) == 1);
    CHECK(call("KillTimer", {0, 1}) == 1);
    CHECK(call("wsprintfA", {0, 0}) == 0 && guest.last_error() == 120);
    CHECK(host.calls["unsupported:BeginPaint"] == 1 && host.calls["unsupported:DialogBoxParamA"] == 1);
    if (argc > 1) {
        std::ifstream in(argv[1], std::ios::binary);
        std::vector<uint8_t> file{std::istreambuf_iterator<char>(in), {}};
        std::string failure;
        CHECK(!file.empty() && run_timer_guest(file, failure));
        if (!failure.empty()) std::printf("FAIL timer guest: %s\n", failure.c_str());
    }
    std::printf("x86-user32: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
