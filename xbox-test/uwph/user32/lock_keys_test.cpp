#include "keyboard_feed.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    HMODULE module = LoadLibraryW(L"uwph-u32c.dll");
    CHECK(module);
    auto sink = reinterpret_cast<user32_core::KeyboardFeed::Sink>(GetProcAddress(module, "uwph_u32c_key_event"));
    using Poll = SHORT (WINAPI*)(int);
    auto state = reinterpret_cast<Poll>(GetProcAddress(module, "GetKeyState"));
    auto poll = reinterpret_cast<Poll>(GetProcAddress(module, "GetAsyncKeyState"));
    CHECK(sink && state && poll);
    user32_core::KeyboardFeed feed(sink);
    for (unsigned vk : {VK_CAPITAL, VK_NUMLOCK, VK_SCROLL}) {
        SetLastError(0xdead);
        CHECK(state(vk) == 0);
        for (int cycle = 0; cycle < 4; ++cycle) {
            SHORT toggle = (cycle + 1) & 1;
            feed.key(vk, 0, false, true);
            CHECK(state(vk) == SHORT(0x8000 | toggle));
            CHECK(poll(vk) == SHORT(0x8001));
            CHECK(poll(vk) == SHORT(0x8000));
            feed.key(vk, 0, false, true);
            CHECK(state(vk) == SHORT(0x8000 | toggle));
            CHECK(poll(vk) == SHORT(0x8001));
            feed.release_all();
            CHECK(state(vk) == toggle);
            CHECK(poll(vk) == 0);
            feed.release_all();
            CHECK(state(vk) == toggle);
            CHECK(GetLastError() == 0xdead);
        }
    }
    std::puts("LOCK KEYS OFFDEVICE PASS three locks, four cycles, repeats, release-all, async isolation, last error");
}
