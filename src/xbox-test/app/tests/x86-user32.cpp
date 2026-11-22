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

// Window state, text, coordinate and keyboard routes against a fake host record layer. Native handles
// 0xaaaa, 0xcccc (created below) and 0xdddd (the desktop) are live; 0xbbbb is a window the host has lost.
struct RecordFake {
    bool serves = true, enabled = true;
    std::vector<std::string> calls;
    uintptr_t last[8]{};
    size_t last_input = 0, text_capacity = 0;
    static bool live(uintptr_t native) { return native == 0xaaaa || native == 0xcccc || native == 0xdddd; }
};
static void put_wide(Memory& mem, uint32_t address, const char16_t* text) {
    uint32_t i = 0; for (; text[i]; ++i) mem.w(address + i * 2, text[i], 2);
    mem.w(address + i * 2, 0, 2);
}
static void fill_bytes(Memory& mem, uint32_t address, size_t size, uint8_t value) {
    std::vector<uint8_t> bytes(size, value); mem.write_bytes(address, bytes.data(), uint32_t(size));
}
static bool all_bytes(Memory& mem, uint32_t address, size_t size, uint8_t value) {
    for (uint8_t byte : mem.read_bytes(address, uint32_t(size))) if (byte != value) return false;
    return true;
}
static void test_u32c_routes() {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000; guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000; cpu.fs_base = guest.cur_teb;
    auto call = [&](const char* dll, const char* name, std::initializer_list<uint32_t> args) -> uint32_t {
        unsigned index = 0; for (uint32_t value : args) mem.w32(cpu.reg[ESP] + 4 + 4 * index++, value);
        return reg.map.at({dll, Registry::key_sym(name)}).fn(cpu);
    };
    auto u32 = [&](const char* name, std::initializer_list<uint32_t> args) { return call("user32.dll", name, args); };
    auto unsupported_logged = [&]() { for (const auto& item : host.calls) if (item.first.rfind("unsupported:", 0) == 0) return true; return false; };
    struct Route { const char* name; unsigned argc; } routes[] = {
        {"OffsetRect", 3}, {"IsWindow", 1}, {"IsWindowEnabled", 1}, {"EnableWindow", 2}, {"GetDesktopWindow", 0},
        {"GetWindowTextW", 3}, {"GetWindowTextA", 3}, {"GetWindowTextLengthW", 1}, {"GetWindowTextLengthA", 1},
        {"GetWindowThreadProcessId", 2}, {"MapWindowPoints", 4}, {"MonitorFromWindow", 2}, {"MonitorFromRect", 2},
        {"GetMonitorInfoA", 2}, {"GetMonitorInfoW", 2}, {"GetKeyState", 1}, {"GetAsyncKeyState", 1}, {"GetKeyboardState", 1},
        {"MapVirtualKeyA", 2}, {"MapVirtualKeyW", 2}, {"GetKeyNameTextA", 3}, {"GetKeyNameTextW", 3},
    };
    for (const auto& route : routes) {
        auto it = reg.map.find({"user32.dll", Registry::key_sym(route.name)});
        CHECK(it != reg.map.end());
        if (it != reg.map.end()) { CHECK(it->second.conv == Conv::Stdcall); CHECK(it->second.argc == route.argc); }
    }

    RecordFake f;
    host.system_metrics = [](int index, int& value) { if (index == 0) value = 1280; else if (index == 1) value = 720; else return false; return true; };
    host.register_window_class = [](const std::u16string& name) { return name == u"U32cWindow"; };
    host.create_window = [](const Host::WindowCreate& q) -> uintptr_t { return q.class_name == u"U32cWindow" ? 0xcccc : 0; };
    host.destroy_window = [](uintptr_t native) { return native == 0xcccc; };
    host.window_call = [&f](Host::WindowCall& q) {
        if (!f.serves) { q.supported = false; return; }
        f.calls.push_back(q.name); std::memcpy(f.last, q.args, sizeof f.last); f.last_input = q.input.size();
        q.supported = true; q.result = 0;
        const bool live = RecordFake::live(q.args[0]);
        auto text_w = [&](const char16_t* text, bool allowed) {
            f.text_capacity = q.output.size(); const size_t chars = q.output.size() / 2; if (!allowed || !chars) return;
            size_t n = 0; while (text[n]) ++n; n = std::min(n, chars - 1);
            std::memcpy(q.output.data(), text, n * 2); q.result = n;
        };
        auto text_a = [&](const char* text, bool allowed) {
            f.text_capacity = q.output.size(); if (!allowed || q.output.empty()) return;
            const size_t n = std::min(std::strlen(text), q.output.size() - 1);
            std::memcpy(q.output.data(), text, n); q.result = n;
        };
        if (q.name == "IsWindow") q.result = live;
        else if (q.name == "IsWindowEnabled") q.result = live && f.enabled;
        else if (q.name == "EnableWindow") { q.result = live && !f.enabled; if (live) f.enabled = q.args[1] != 0; }
        else if (q.name == "GetDesktopWindow") q.result = 0xdddd;
        else if (q.name == "GetWindowTextLengthW" || q.name == "GetWindowTextLengthA") q.result = live ? 5 : 0;
        else if (q.name == "GetWindowTextW") text_w(u"hello", live);
        else if (q.name == "GetWindowTextA") text_a("hello", live);
        else if (q.name == "GetWindowThreadProcessId") q.result = live ? 0xBEEF : 0;
        else if (q.name == "MapWindowPoints") {
            q.output = q.input;
            if ((q.args[0] && !RecordFake::live(q.args[0])) || (q.args[1] && !RecordFake::live(q.args[1]))) return;
            const int32_t dx = (q.args[0] ? 10 : 0) - (q.args[1] ? 3 : 0), dy = (q.args[0] ? 20 : 0) - (q.args[1] ? 5 : 0);
            for (size_t i = 0; i + 8 <= q.output.size(); i += 8) {
                int32_t point[2]; std::memcpy(point, &q.output[i], 8); point[0] += dx; point[1] += dy; std::memcpy(&q.output[i], point, 8);
            }
            q.result = uintptr_t(int32_t((uint32_t(uint16_t(dy)) << 16) | uint16_t(dx)));  // sign-extended int, as launch.h returns it
        }
        else if (q.name == "GetKeyState") q.result = q.args[0] == 0x41 ? 0x8001 : q.args[0] == 0x14 ? 1 : 0;
        else if (q.name == "GetAsyncKeyState") q.result = uintptr_t(intptr_t(int16_t(q.args[0] == 0x42 ? 0x8000 : q.args[0] == 0x14 ? 1 : 0)));
        else if (q.name == "GetKeyboardState") { for (size_t i = 0; i < q.output.size(); ++i) q.output[i] = uint8_t(i); q.result = 1; }
        else if (q.name == "MapVirtualKeyW") q.result = 5000 + q.args[0] + q.args[1];
        else if (q.name == "MapVirtualKeyA") q.result = 6000 + q.args[0] + q.args[1];
        else if (q.name == "GetKeyNameTextW") text_w(u"Enter", q.args[0] == 0x001c0000);
        else if (q.name == "GetKeyNameTextA") text_a("Enter", q.args[0] == 0x001c0000);
        else q.supported = false;
    };
    const uint32_t live_token = 0xD0F00001u, lost_token = 0xD0F00002u;  // clear of the serials the allocator hands out next
    host.window_handles[live_token] = 0xaaaa; host.window_handles[lost_token] = 0xbbbb;

    // OffsetRect: in-guest, wrapping 32-bit arithmetic, NULL fails without touching memory.
    const uint32_t rect = 0x4000;
    mem.w32(rect, 10); mem.w32(rect + 4, 20); mem.w32(rect + 8, 110); mem.w32(rect + 12, 220);
    CHECK(u32("OffsetRect", {rect, 5, uint32_t(-7)}) == 1);
    CHECK(mem.r32(rect) == 15 && mem.r32(rect + 4) == 13 && mem.r32(rect + 8) == 115 && mem.r32(rect + 12) == 213);
    mem.w32(rect, 0x7fffffffu); mem.w32(rect + 4, 0x80000000u); mem.w32(rect + 8, 0xffffffffu); mem.w32(rect + 12, 0);
    CHECK(u32("OffsetRect", {rect, 1, uint32_t(-1)}) == 1);
    CHECK(mem.r32(rect) == 0x80000000u && mem.r32(rect + 4) == 0x7fffffffu && mem.r32(rect + 8) == 0 && mem.r32(rect + 12) == 0xffffffffu);
    CHECK(u32("OffsetRect", {0, 1, 1}) == 0);

    // IsWindow / IsWindowEnabled / EnableWindow: unknown handles are "not a window" without an error or a host call.
    guest.set_last_error(7); f.calls.clear();
    CHECK(u32("IsWindow", {0}) == 0 && u32("IsWindow", {0xdeadu}) == 0 && u32("IsWindowEnabled", {0}) == 0 && u32("IsWindowEnabled", {0xdeadu}) == 0);
    CHECK(guest.last_error() == 7 && f.calls.empty() && !unsupported_logged());
    CHECK(u32("IsWindow", {live_token}) == 1 && u32("IsWindow", {lost_token}) == 0);
    CHECK(u32("IsWindowEnabled", {live_token}) == 1 && u32("IsWindowEnabled", {lost_token}) == 0);
    CHECK(u32("EnableWindow", {live_token, 0}) == 0 && u32("IsWindowEnabled", {live_token}) == 0);  // returns the previous disabled state
    CHECK(u32("EnableWindow", {live_token, 5}) == 1 && f.last[1] == 1 && u32("IsWindowEnabled", {live_token}) == 1);
    CHECK(u32("EnableWindow", {live_token, 1}) == 0);
    guest.set_last_error(0); CHECK(u32("EnableWindow", {0xdeadu, 1}) == 0 && guest.last_error() == 1400);
    guest.set_last_error(0); CHECK(u32("EnableWindow", {0, 1}) == 0 && guest.last_error() == 1400);

    // GetDesktopWindow: one stable guest token that every window route recognises, reused when already mapped.
    CHECK(u32("GetDesktopWindow", {}) != 0);
    const uint32_t desktop = u32("GetDesktopWindow", {});
    CHECK(desktop >= 0xD0000000u && host.window_handles[desktop] == 0xdddd && u32("GetDesktopWindow", {}) == desktop);
    size_t desktop_entries = 0; for (const auto& item : host.window_handles) desktop_entries += item.second == 0xdddd;
    CHECK(desktop_entries == 1);
    CHECK(u32("IsWindow", {desktop}) == 1 && u32("GetWindowTextLengthW", {desktop}) == 5);
    host.window_handles.erase(desktop); host.window_threads.erase(desktop); host.window_handles[0xD0009999u] = 0xdddd;
    CHECK(u32("GetDesktopWindow", {}) == 0xD0009999u && host.window_handles.size() == 3);
    host.window_handles.erase(0xD0009999u);
    mem.w32(guest.cur_teb + 0x24, 0x2222); const uint32_t fresh_desktop = u32("GetDesktopWindow", {});
    CHECK(fresh_desktop != 0 && host.window_threads[fresh_desktop] == 0x2222);
    host.window_handles.erase(fresh_desktop); host.window_threads.erase(fresh_desktop);

    // GetWindowText / GetWindowTextLength.
    const uint32_t buffer = 0x6000;
    CHECK(u32("GetWindowTextLengthW", {live_token}) == 5 && u32("GetWindowTextLengthA", {live_token}) == 5);
    guest.set_last_error(0); CHECK(u32("GetWindowTextLengthW", {0xdeadu}) == 0 && guest.last_error() == 1400);
    guest.set_last_error(0); CHECK(u32("GetWindowTextLengthA", {0}) == 0 && guest.last_error() == 1400);
    fill_bytes(mem, buffer, 64, 0xAA); f.calls.clear();
    CHECK(u32("GetWindowTextW", {live_token, buffer, 0}) == 0 && u32("GetWindowTextW", {live_token, buffer, uint32_t(-1)}) == 0);
    CHECK(u32("GetWindowTextA", {live_token, buffer, 0}) == 0 && u32("GetWindowTextA", {live_token, 0, 16}) == 0 && u32("GetWindowTextW", {live_token, 0, 16}) == 0);
    CHECK(f.calls.empty() && all_bytes(mem, buffer, 64, 0xAA));
    guest.set_last_error(0); CHECK(u32("GetWindowTextW", {0xdeadu, buffer, 16}) == 0 && guest.last_error() == 1400 && all_bytes(mem, buffer, 64, 0xAA));
    CHECK(u32("GetWindowTextW", {live_token, buffer, 16}) == 5 && f.text_capacity == 32);
    CHECK(mem.r(buffer, 2) == 'h' && mem.r(buffer + 8, 2) == 'o' && mem.r(buffer + 10, 2) == 0 && mem.r(buffer + 12, 2) == 0xAAAA);  // result + NUL only
    fill_bytes(mem, buffer, 64, 0xAA);
    CHECK(u32("GetWindowTextW", {live_token, buffer, 3}) == 2 && mem.r(buffer, 2) == 'h' && mem.r(buffer + 2, 2) == 'e' && mem.r(buffer + 4, 2) == 0 && mem.r(buffer + 6, 2) == 0xAAAA);
    fill_bytes(mem, buffer, 64, 0xAA);
    CHECK(u32("GetWindowTextA", {live_token, buffer, 16}) == 5 && f.text_capacity == 16);
    CHECK(mem.read_cstr(buffer, 16) == "hello" && mem.r(buffer + 6, 1) == 0xAA);
    fill_bytes(mem, buffer, 64, 0xAA);
    CHECK(u32("GetWindowTextA", {live_token, buffer, 1}) == 0 && mem.r(buffer, 1) == 0 && mem.r(buffer + 1, 1) == 0xAA);
    CHECK(u32("GetWindowTextW", {live_token, buffer, 0x7fffffffu}) == 5 && f.text_capacity == 32768u * 2);  // an absurd size is capped, not allocated
    CHECK(u32("GetWindowTextA", {live_token, buffer, 0x7fffffffu}) == 5 && f.text_capacity == 32768u);
    CHECK(!unsupported_logged());

    // GetWindowThreadProcessId: guest ids only. The creating guest thread owns the window, the process id is the guest's.
    const uint32_t wc = 0x5000, class_name = 0x12000;  // above 0xffff, or the class name reads as an atom
    put_wide(mem, class_name, u"U32cWindow"); mem.w32(wc, 48); mem.w32(wc + 8, 0x401000); mem.w32(wc + 40, class_name);
    CHECK(u32("RegisterClassExW", {wc}) != 0);
    mem.w32(guest.cur_teb + 0x24, 0x3333);
    const uint32_t created = u32("CreateWindowExW", {0, class_name, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(created != 0 && host.window_handles[created] == 0xcccc && host.window_threads[created] == 0x3333);
    mem.w32(guest.cur_teb + 0x24, 0x4444);  // another guest thread asks
    const uint32_t process = 0x7000; mem.w32(process, 0x7777);
    CHECK(u32("GetWindowThreadProcessId", {created, process}) == 0x3333 && mem.r32(process) == Host::process_id);
    CHECK(call("kernel32.dll", "GetCurrentProcessId", {}) == mem.r32(process) && call("kernel32.dll", "GetCurrentThreadId", {}) == 0x4444);
    mem.w32(process, 0x7777); CHECK(u32("GetWindowThreadProcessId", {created, 0}) == 0x3333 && mem.r32(process) == 0x7777);  // NULL lpdwProcessId
    guest.set_last_error(0);
    CHECK(u32("GetWindowThreadProcessId", {0xdeadu, process}) == 0 && mem.r32(process) == 0x7777 && guest.last_error() == 1400);
    guest.set_last_error(0); CHECK(u32("GetWindowThreadProcessId", {0, process}) == 0 && mem.r32(process) == 0x7777 && guest.last_error() == 1400);
    guest.set_last_error(0); CHECK(u32("GetWindowThreadProcessId", {lost_token, process}) == 0 && mem.r32(process) == 0x7777 && guest.last_error() == 1400);
    CHECK(u32("GetWindowThreadProcessId", {live_token, process}) == 0x4444);  // a host-made window falls back to the caller's thread
    CHECK(u32("DestroyWindow", {created}) == 1 && host.window_threads.count(created) == 0);
    guest.set_last_error(0); CHECK(u32("GetWindowThreadProcessId", {created, process}) == 0 && guest.last_error() == 1400);

    // MapWindowPoints: HWND_DESKTOP stays NULL natively, guest tokens translate, bad input fails before any host call.
    const uint32_t points = 0x8000;
    auto set_points = [&]() { mem.w32(points, 1); mem.w32(points + 4, 2); mem.w32(points + 8, 100); mem.w32(points + 12, 200); mem.w32(points + 16, 0x5a5a5a5au); };
    set_points(); f.calls.clear();
    CHECK(u32("MapWindowPoints", {0, 0, points, 2}) == 0 && f.last[0] == 0 && f.last[1] == 0 && f.last_input == 16);
    CHECK(mem.r32(points) == 1 && mem.r32(points + 4) == 2 && mem.r32(points + 8) == 100 && mem.r32(points + 12) == 200);
    CHECK(u32("MapWindowPoints", {live_token, 0, points, 2}) == ((20u << 16) | 10u) && f.last[0] == 0xaaaa && f.last[1] == 0);
    CHECK(mem.r32(points) == 11 && mem.r32(points + 4) == 22 && mem.r32(points + 8) == 110 && mem.r32(points + 12) == 220 && mem.r32(points + 16) == 0x5a5a5a5au);
    set_points();
    CHECK(u32("MapWindowPoints", {0, live_token, points, 1}) == ((uint32_t(uint16_t(-5)) << 16) | uint16_t(-3)) && f.last[1] == 0xaaaa);
    CHECK(mem.r32(points) == uint32_t(-2) && mem.r32(points + 4) == uint32_t(-3) && mem.r32(points + 8) == 100);  // one point only
    set_points();
    const size_t before = f.calls.size();
    guest.set_last_error(0); CHECK(u32("MapWindowPoints", {0xdeadu, 0, points, 2}) == 0 && guest.last_error() == 1400);
    guest.set_last_error(0); CHECK(u32("MapWindowPoints", {0, 0xdeadu, points, 2}) == 0 && guest.last_error() == 1400);
    guest.set_last_error(0); CHECK(u32("MapWindowPoints", {live_token, 0xdeadu, points, 2}) == 0 && guest.last_error() == 1400);
    CHECK(f.calls.size() == before && mem.r32(points) == 1 && mem.r32(points + 8) == 100);
    // cPoints == 0 and a NULL array only ask for the offsets.
    CHECK(u32("MapWindowPoints", {live_token, 0, points, 0}) == ((20u << 16) | 10u) && f.last[3] == 0 && f.last_input == 0 && mem.r32(points) == 1);
    CHECK(u32("MapWindowPoints", {live_token, 0, 0, 5}) == ((20u << 16) | 10u) && f.last[3] == 5 && f.last_input == 0);
    // cPoints * 8 must not wrap: 0x20000000 * 8 is 0 in 32 bits, and a huge count must not allocate.
    const size_t guarded = f.calls.size();
    for (uint32_t huge : {0x10001u, 0x20000000u, 0x20000001u, 0x7fffffffu, 0xffffffffu}) {
        guest.set_last_error(0); CHECK(u32("MapWindowPoints", {live_token, 0, points, huge}) == 0 && guest.last_error() == 87);
    }
    CHECK(f.calls.size() == guarded && mem.r32(points) == 1);

    // GetMonitorInfoA/W: exact struct sizes, 1461 for a monitor that is not the display, 87 for NULL or a bad cbSize.
    const uint32_t info = 0x9000, monitor = 0xCE000001u;
    auto monitor_info = [&](const char* name, uint32_t size, uint32_t handle) {
        fill_bytes(mem, info, 128, 0xAA); mem.w32(info, size); guest.set_last_error(0); return u32(name, {handle, info});
    };
    guest.set_last_error(0); CHECK(u32("GetMonitorInfoA", {monitor, 0}) == 0 && guest.last_error() == 87);
    guest.set_last_error(0); CHECK(u32("GetMonitorInfoW", {monitor, 0}) == 0 && guest.last_error() == 87);
    for (uint32_t bad : {0u, 4u, 39u, 41u, 44u, 71u, 103u, 105u, 0xffffffffu}) {
        CHECK(monitor_info("GetMonitorInfoA", bad, monitor) == 0 && guest.last_error() == 87);
        CHECK(monitor_info("GetMonitorInfoW", bad, monitor) == 0 && guest.last_error() == 87);
    }
    CHECK(monitor_info("GetMonitorInfoA", 104, monitor) == 0 && guest.last_error() == 87);  // the W size is not an A size
    CHECK(monitor_info("GetMonitorInfoW", 72, monitor) == 0 && guest.last_error() == 87);
    CHECK(all_bytes(mem, info + 4, 124, 0xAA));  // a rejected call writes nothing
    for (uint32_t bad_monitor : {0u, 1u, 0xCE000002u, 0xC0000001u}) {
        CHECK(monitor_info("GetMonitorInfoA", 40, bad_monitor) == 0 && guest.last_error() == 1461);
        CHECK(monitor_info("GetMonitorInfoW", 104, bad_monitor) == 0 && guest.last_error() == 1461);
    }
    CHECK(monitor_info("GetMonitorInfoA", 40, monitor) == 1);
    CHECK(mem.r32(info) == 40 && mem.r32(info + 12) == 1280 && mem.r32(info + 16) == 720 && mem.r32(info + 28) == 1280 && mem.r32(info + 32) == 720 && mem.r32(info + 36) == 1);
    CHECK(all_bytes(mem, info + 40, 88, 0xAA));  // MONITORINFO is 40 bytes: nothing past it
    CHECK(monitor_info("GetMonitorInfoA", 72, monitor) == 1 && mem.r32(info) == 72 && mem.r32(info + 12) == 1280);
    CHECK(mem.read_cstr(info + 40, 32) == "\\\\.\\DISPLAY1" && mem.r(info + 40 + 13, 1) == 0xAA && all_bytes(mem, info + 72, 56, 0xAA));
    CHECK(monitor_info("GetMonitorInfoW", 40, monitor) == 1 && mem.r32(info + 12) == 1280 && all_bytes(mem, info + 40, 88, 0xAA));
    CHECK(monitor_info("GetMonitorInfoW", 104, monitor) == 1 && mem.r32(info) == 104 && mem.r(info + 40, 2) == '\\' && mem.r(info + 40 + 11 * 2, 2) == '1' && mem.r(info + 40 + 12 * 2, 2) == 0);
    CHECK(all_bytes(mem, info + 104, 24, 0xAA));
    // MonitorFromRect(NULL) names no monitor; every other query still lands on the one display.
    guest.set_last_error(0); CHECK(u32("MonitorFromRect", {0, 2}) == 0);
    CHECK(u32("MonitorFromRect", {rect, 2}) == monitor && u32("MonitorFromWindow", {live_token, 2}) == monitor && u32("MonitorFromWindow", {0, 1}) == monitor);

    // Keyboard queries: SHORT results sign-extend to 32 bits; the neutral answers return only without key records.
    CHECK(u32("GetKeyState", {0x41}) == 0xFFFF8001u && u32("GetKeyState", {0x14}) == 1 && u32("GetKeyState", {0x42}) == 0);
    CHECK(u32("GetAsyncKeyState", {0x42}) == 0xFFFF8000u && u32("GetAsyncKeyState", {0x14}) == 1 && u32("GetAsyncKeyState", {0x41}) == 0);
    const uint32_t state = 0xa000;
    fill_bytes(mem, state - 4, 264, 0xAA); guest.set_last_error(0);
    CHECK(u32("GetKeyboardState", {0}) == 0 && guest.last_error() == 87);
    CHECK(u32("GetKeyboardState", {state}) == 1);
    bool state_ok = true; for (uint32_t i = 0; i < 256; ++i) state_ok = state_ok && mem.r(state + i, 1) == i;
    CHECK(state_ok && all_bytes(mem, state - 4, 4, 0xAA) && all_bytes(mem, state + 256, 4, 0xAA));
    CHECK(u32("MapVirtualKeyW", {0x41, 0}) == 5065 && u32("MapVirtualKeyA", {0x41, 2}) == 6067);  // each name reaches its own record entry
    const uint32_t key_name = 0xb000, enter = 0x001c0000;
    fill_bytes(mem, key_name, 64, 0xAA); f.calls.clear();
    CHECK(u32("GetKeyNameTextW", {enter, key_name, 0}) == 0 && u32("GetKeyNameTextW", {enter, key_name, uint32_t(-4)}) == 0 && u32("GetKeyNameTextW", {enter, 0, 16}) == 0);
    CHECK(u32("GetKeyNameTextA", {enter, key_name, 0}) == 0 && u32("GetKeyNameTextA", {enter, key_name, uint32_t(-4)}) == 0 && u32("GetKeyNameTextA", {enter, 0, 16}) == 0);
    CHECK(f.calls.empty() && all_bytes(mem, key_name, 64, 0xAA));
    CHECK(u32("GetKeyNameTextW", {enter, key_name, 16}) == 5 && f.text_capacity == 32 && mem.r(key_name, 2) == 'E' && mem.r(key_name + 8, 2) == 'r' && mem.r(key_name + 10, 2) == 0 && mem.r(key_name + 12, 2) == 0xAAAA);
    fill_bytes(mem, key_name, 64, 0xAA);
    CHECK(u32("GetKeyNameTextW", {enter, key_name, 3}) == 2 && mem.r(key_name + 4, 2) == 0 && mem.r(key_name + 6, 2) == 0xAAAA);
    fill_bytes(mem, key_name, 64, 0xAA);
    CHECK(u32("GetKeyNameTextA", {enter, key_name, 16}) == 5 && mem.read_cstr(key_name, 16) == "Enter" && mem.r(key_name + 6, 1) == 0xAA && f.text_capacity == 16);
    CHECK(u32("GetKeyNameTextA", {enter, key_name, 0x7fffffffu}) == 5 && f.text_capacity == 256);
    CHECK(u32("GetKeyNameTextW", {enter, key_name, 0x7fffffffu}) == 5 && f.text_capacity == 512);
    fill_bytes(mem, key_name, 64, 0xAA);
    CHECK(u32("GetKeyNameTextW", {0x7f0000, key_name, 16}) == 0 && mem.r(key_name, 2) == 0 && mem.r(key_name + 2, 2) == 0xAAAA);  // unknown key: empty name
    CHECK(!unsupported_logged());

    // Without host key/window records the neutral answers hold and the new routes fail closed.
    f.serves = false; guest.set_last_error(0);
    CHECK(u32("GetKeyState", {0x41}) == 0 && u32("MapVirtualKeyW", {'A', 0}) == 0x1e && u32("MapVirtualKeyA", {'A', 0}) == 0x1e);
    fill_bytes(mem, state, 256, 0xAA);
    CHECK(u32("GetKeyboardState", {state}) == 1 && all_bytes(mem, state, 256, 0));
    CHECK(u32("GetKeyboardState", {0}) == 0);
    fill_bytes(mem, buffer, 64, 0xAA); fill_bytes(mem, points, 16, 0xAA); fill_bytes(mem, key_name, 64, 0xAA);
    guest.set_last_error(0);
    CHECK(u32("GetAsyncKeyState", {0x41}) == 0 && guest.last_error() == 120);
    CHECK(u32("IsWindow", {live_token}) == 0 && u32("IsWindowEnabled", {live_token}) == 0 && u32("EnableWindow", {live_token, 1}) == 0);
    CHECK(u32("GetDesktopWindow", {}) == 0 && u32("GetWindowTextLengthW", {live_token}) == 0 && u32("GetWindowTextLengthA", {live_token}) == 0);
    CHECK(u32("GetWindowTextW", {live_token, buffer, 16}) == 0 && u32("GetWindowTextA", {live_token, buffer, 16}) == 0 && all_bytes(mem, buffer, 64, 0xAA));
    CHECK(u32("GetKeyNameTextW", {enter, key_name, 16}) == 0 && u32("GetKeyNameTextA", {enter, key_name, 16}) == 0 && all_bytes(mem, key_name, 64, 0xAA));
    mem.w32(process, 0x7777); CHECK(u32("GetWindowThreadProcessId", {live_token, process}) == 0 && mem.r32(process) == 0x7777);
    CHECK(u32("MapWindowPoints", {live_token, 0, points, 2}) == 0 && all_bytes(mem, points, 16, 0xAA));
    CHECK(host.calls["unsupported:IsWindow"] == 1 && host.calls["unsupported:GetAsyncKeyState"] == 1);
}

int main(int argc, char** argv) {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000; guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000; cpu.fs_base = guest.cur_teb;
    struct Route { const char* name; unsigned argc; Conv conv; } routes[] = {
        {"AllowSetForegroundWindow", 1, Conv::Stdcall}, {"BeginPaint", 2, Conv::Stdcall}, {"CheckDlgButton", 3, Conv::Stdcall}, {"CheckMenuItem", 3, Conv::Stdcall},
        {"CheckRadioButton", 4, Conv::Stdcall}, {"CreateDialogParamA", 5, Conv::Stdcall}, {"DestroyMenu", 1, Conv::Stdcall},
        {"DialogBoxParamA", 5, Conv::Stdcall}, {"EnableWindow", 2, Conv::Stdcall}, {"EndPaint", 2, Conv::Stdcall},
        {"FindWindowA", 2, Conv::Stdcall}, {"GetDlgCtrlID", 1, Conv::Stdcall}, {"GetDlgItemTextA", 4, Conv::Stdcall}, {"GetParent", 1, Conv::Stdcall}, {"GetWindowLongPtrW", 2, Conv::Stdcall}, {"MonitorFromRect", 2, Conv::Stdcall}, {"RegisterDeviceNotificationA", 3, Conv::Stdcall}, {"RegisterDeviceNotificationW", 3, Conv::Stdcall}, {"UnregisterDeviceNotification", 1, Conv::Stdcall},
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
        else if (q.name == "PeekMessageW") { q.result = 1; q.supported = true; }
        else if (q.name == "GetMessageW") { q.result = 0; q.supported = true; q.output.assign(32, 0); q.output[8] = 0x12; }  // WM_QUIT: returns 0 but fills MSG
        else if (q.name == "SetWindowPos") { CHECK(q.args[1] == uintptr_t(-1)); q.result = 1; q.supported = true; }
    };
    host.get_window_long = [](uintptr_t hwnd, int index, bool pointer) -> intptr_t {
        CHECK(hwnd == 0xaaaa && index == 0 && pointer); return intptr_t(uint32_t(0xfedcba98u));
    };
    CHECK(call("GetParent", {0x1111}) == 0x2222);
    CHECK(call("GetWindowLongPtrW", {0x1111, 0}) == 0xfedcba98u);
    CHECK(call("AllowSetForegroundWindow", {0xffffffffu}) == 1);
    uint32_t rect = 0x4000;
    CHECK(call("SetRect", {rect, 1, 2, 31, 42}) == 1);
    CHECK(mem.r32(rect) == 1 && mem.r32(rect + 4) == 2 && mem.r32(rect + 8) == 31 && mem.r32(rect + 12) == 42);
    guest.set_last_error(7); CHECK(call("BeginPaint", {0, 0}) == 0 && guest.last_error() == 120);
    CHECK(call("DialogBoxParamA", {0, 0, 0, 0, 0}) == uint32_t(-1) && guest.last_error() == 120);
    CHECK(call("SetTimer", {0, 1, 16, 0}) == 1);
    CHECK(call("KillTimer", {0, 1}) == 1);
    CHECK(call("wsprintfA", {0, 0}) == 0 && guest.last_error() == 120);
    CHECK(host.calls["unsupported:BeginPaint"] == 1 && host.calls["unsupported:DialogBoxParamA"] == 1);
    CHECK(call("GetMessageW", {0x4100, 0, 0, 0}) == 0 && mem.r32(0x4100 + 4) == 0x12);
    CHECK(call("SetWindowPos", {0x1111, 0xffffffffu, 0, 0, 0, 0, 0}) == 1);
    CHECK(call("SetWindowLongPtrW", {0x1111, uint32_t(-4), 0x4321}) == 0 && call("GetWindowLongPtrW", {0x1111, uint32_t(-4)}) == 0x4321);
    CHECK(reg.map.at({"gdi32.dll", Registry::key_sym("GetGlyphOutlineA")}).argc == 7);
    if (argc > 1) {
        std::ifstream in(argv[1], std::ios::binary);
        std::vector<uint8_t> file{std::istreambuf_iterator<char>(in), {}};
        std::string failure;
        CHECK(!file.empty() && run_timer_guest(file, failure));
        if (!failure.empty()) std::printf("FAIL timer guest: %s\n", failure.c_str());
    }
    // LoadImageA: bitmaps and LR_LOADFROMFILE are not served; unknown types and unknown modules fail truthfully.
    static unsigned g_stub_calls = 0;
    g_stub_calls = 0;
    reg.add("user32.dll", "LoadCursorA", Conv::Stdcall, 2, [](Cpu&) -> uint32_t { ++g_stub_calls; return 0x7777; });
    guest.set_last_error(0); CHECK(call("LoadImageA", {0, 32512, 0, 0, 0, 0}) == 0); CHECK(guest.last_error() == 1813);
    guest.set_last_error(0); CHECK(call("LoadImageA", {0, 32512, 5, 0, 0, 0}) == 0); CHECK(guest.last_error() == 87);
    guest.set_last_error(0); CHECK(call("LoadImageA", {0, 32512, 2, 0, 0, 0x10}) == 0); CHECK(guest.last_error() == 87);
    guest.set_last_error(0); CHECK(call("LoadImageA", {0x1234, 0x5000, 1, 0, 0, 0}) == 0); CHECK(guest.last_error() == 1813);
    CHECK(g_stub_calls == 0);
    // Stock cursor (hinst NULL, ordinal name) forwards to LoadCursorA and returns its handle.
    CHECK(call("LoadImageA", {0, 32512, 2, 0, 0, 0}) == 0x7777); CHECK(g_stub_calls == 1);
    // LoadImageA with LR_LOADFROMFILE: the file is checked through host.file_call (GetFileAttributesA's route).
    mem.write_bytes(0x22000, "exists.ico", 11); mem.write_bytes(0x22020, "missing.ico", 12); mem.write_bytes(0x22040, "exists.bmp", 11);
    host.file_call = [&](Host::FileCall& q) { const bool found = q.path_a == "exists.ico" || q.path_a == "exists.bmp"; q.succeeded = found; q.error = found ? 0 : 2; };
    reg.add("user32.dll", "LoadIconA", Conv::Stdcall, 2, [](Cpu& c) -> uint32_t { return c.arg(0) == 0 && c.arg(1) == 32512 ? 0x5555 : 0x4444; });
    guest.set_last_error(0); CHECK(call("LoadImageA", {0, 0x22000, 1, 0, 0, 0x10}) == 0x5555);
    guest.set_last_error(0); CHECK(call("LoadImageA", {0, 0x22020, 1, 0, 0, 0x10}) == 0); CHECK(guest.last_error() == 2);
    guest.set_last_error(0); CHECK(call("LoadImageA", {0, 0x22040, 0, 0, 0, 0x10}) == 0); CHECK(guest.last_error() == 120);
    guest.set_last_error(0); CHECK(call("LoadImageA", {0, 32512, 1, 0, 0, 0x10}) == 0); CHECK(guest.last_error() == 87);
    host.file_call = {};
    guest.set_last_error(0); CHECK(call("LoadImageA", {0, 0x22000, 1, 0, 0, 0x10}) == 0); CHECK(guest.last_error() == 120);
    test_u32c_routes();
    std::printf("x86-user32: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
