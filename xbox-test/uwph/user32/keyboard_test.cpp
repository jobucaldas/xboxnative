#include "keyboard_feed.h"
#include "pointer_buttons.h"
#include <cstdio>
#include <vector>
#include <utility>
#include <limits>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static std::vector<std::pair<int, bool>> events;
static user32_core::KeyboardFeed::Sink core_sink;
static void WINAPI sink(int vk, BOOL down) {
    events.emplace_back(vk, down != FALSE);
    core_sink(vk, down);
}
int main() {
    HMODULE module = LoadLibraryW(L"uwph-u32c.dll");
    CHECK(module);
    core_sink = reinterpret_cast<user32_core::KeyboardFeed::Sink>(GetProcAddress(module, "uwph_u32c_key_event"));
    using Poll = SHORT (WINAPI*)(int);
    auto poll = reinterpret_cast<Poll>(GetProcAddress(module, "GetAsyncKeyState"));
    auto state = reinterpret_cast<Poll>(GetProcAddress(module, "GetKeyState"));
    CHECK(core_sink && poll && state);
    user32_core::KeyboardFeed feed(sink);
    user32_core::KeyboardFeed unavailable;
    unavailable.key('A', 0x1e, false, true);
    unavailable.release_all();
    CHECK(events.empty());
    SetLastError(0xdead);
    feed.key('A', 0x1e, false, true);
    CHECK(poll('A') == static_cast<SHORT>(0x8001));
    CHECK(poll('A') == static_cast<SHORT>(0x8000));
    feed.key('A', 0x1e, false, true);
    CHECK(events.size() == 2 && (poll('A') & 1));
    CHECK(!(poll('A') & 1));
    feed.key('A', 0x1e, false, false);
    CHECK(!poll('A') && !state('A'));
    const unsigned generic[] = {VK_SHIFT, VK_CONTROL, VK_MENU};
    const unsigned left[] = {VK_LSHIFT, VK_LCONTROL, VK_LMENU};
    const unsigned right[] = {VK_RSHIFT, VK_RCONTROL, VK_RMENU};
    for (unsigned i = 0; i < 3; ++i) {
        feed.key(generic[i], i ? 0x1d : 0x2a, false, true);
        CHECK(state(left[i]) & 0x8000);
        CHECK(poll(generic[i]) == static_cast<SHORT>(0x8001));
        feed.key(generic[i], i ? 0x1d : 0x36, i != 0, true);
        CHECK(state(right[i]) & 0x8000);
        CHECK(!(poll(generic[i]) & 1));
        feed.key(generic[i], i ? 0x1d : 0x2a, false, false);
        CHECK(!state(left[i]) && (state(generic[i]) & 0x8000));
        feed.key(generic[i], i ? 0x1d : 0x36, i != 0, false);
        CHECK(!state(generic[i]) && !state(right[i]));
        // Already side-specific CoreWindow keys take the same path.
        feed.key(right[i], 0, false, true);
        CHECK(state(right[i]) & 0x8000);
    }
    feed.key('Z', 0x2c, false, true);
    feed.key(256, 0, false, true);
    feed.key(0xffffffff, 0, false, true);
    feed.release_all();
    for (int vk = 0; vk < 256; ++vk) CHECK(!state(vk));
    size_t count = events.size();
    feed.release_all();
    CHECK(events.size() == count);
    // A release clears held state, preserving the earlier asynchronous press.
    CHECK(poll('Z') == 1 && !poll('Z'));
    feed.key('Z', 0x2c, false, true);
    CHECK(poll('Z') == static_cast<SHORT>(0x8001));
    feed.release_all();
    user32_core::PointerButtons buttons(sink);
    user32_core::PointerButtons no_mouse;
    count = events.size();
    no_mouse.snapshot(true, true, true, true, true);
    no_mouse.release_all();
    CHECK(events.size() == count);
    const int mouse_keys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    buttons.snapshot(true, true, true, true, true);
    for (int vk : mouse_keys) CHECK(poll(vk) == static_cast<SHORT>(0x8001));
    count = events.size();
    buttons.snapshot(true, true, true, true, true);
    CHECK(events.size() == count);
    for (int vk : mouse_keys) CHECK(poll(vk) == static_cast<SHORT>(0x8000));
    buttons.snapshot(false, true, false, true, false);
    CHECK(!state(VK_LBUTTON) && !state(VK_MBUTTON) && !state(VK_XBUTTON2));
    CHECK((state(VK_RBUTTON) & 0x8000) && (state(VK_XBUTTON1) & 0x8000));
    buttons.release_all();
    for (int vk : mouse_keys) CHECK(!state(vk));
    buttons.snapshot(true, false, false, false, false);
    buttons.release_all();
    CHECK(poll(VK_LBUTTON) == 1 && !poll(VK_LBUTTON));
    for (unsigned mask = 0; mask < 32; ++mask) {
        buttons.snapshot(mask & 1, mask & 2, mask & 4, mask & 8, mask & 16);
        for (unsigned i = 0; i < 5; ++i)
            CHECK(((state(mouse_keys[i]) & 0x8000) != 0) == ((mask & (1u << i)) != 0));
    }
    buttons.release_all();
    using Position = void (WINAPI*)(double, double, double, double);
    using GetPosition = BOOL (WINAPI*)(POINT*);
    using Clip = BOOL (WINAPI*)(const RECT*);
    using Metrics = int (WINAPI*)(int);
    auto position = reinterpret_cast<Position>(GetProcAddress(module, "uwph_u32c_pointer_position"));
    auto get_position = reinterpret_cast<GetPosition>(GetProcAddress(module, "GetCursorPos"));
    auto clip = reinterpret_cast<Clip>(GetProcAddress(module, "ClipCursor"));
    auto metrics = reinterpret_cast<Metrics>(GetProcAddress(module, "GetSystemMetrics"));
    CHECK(position && get_position && clip && metrics);
    const int width = metrics(SM_CXSCREEN), height = metrics(SM_CYSCREEN);
    POINT point;
    position(480, 270, 960, 540);
    CHECK(get_position(&point) && point.x == width / 2 && point.y == height / 2);
    position(-10, 1000, 960, 540);
    CHECK(get_position(&point) && point.x == 0 && point.y == height - 1);
    position(960, 0, 960, 540);
    CHECK(get_position(&point) && point.x == width - 1 && point.y == 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double invalid[][4] = {{nan,0,960,540},{0,inf,960,540},
        {0,0,0,540},{0,0,960,-1},{0,0,inf,540},{0,0,960,nan}};
    for (auto& sample : invalid) {
        position(sample[0], sample[1], sample[2], sample[3]);
        CHECK(get_position(&point) && point.x == width - 1 && point.y == 0);
    }
    RECT rect{10,20,100,200};
    CHECK(clip(&rect));
    position(960, -1, 960, 540);
    CHECK(get_position(&point) && point.x == 99 && point.y == 20);
    CHECK(clip(nullptr));
    position(0, 540, 960, 540);
    CHECK(get_position(&point) && point.x == 0 && point.y == height - 1);
    using Configure = void (WINAPI*)(LONG, LONG);
    auto configure = reinterpret_cast<Configure>(GetProcAddress(module, "uwph_u32c_configure"));
    CHECK(configure);
    configure(1280, 720);
    position(320.25, 180.25, 640, 360);
    CHECK(get_position(&point) && point.x == 640 && point.y == 360);
    position(1, 1, 1, 1);
    CHECK(get_position(&point) && point.x == 1279 && point.y == 719);
    configure(width, height);
    CHECK(GetLastError() == 0xdead);
    std::puts("KEYBOARD/MOUSE OFFDEVICE PASS real record DLL polling, repeat presses, sided modifiers, mouse snapshots, pointer coordinates, release, invalid keys, last error");
}
