// SPDX-License-Identifier: GPL-3.0-or-later
// LoadCursorW must return a cursor token from the same table as LoadCursorA,
// so a guest SetCursor(LoadCursorW(...)) finds the native cursor.
#include <cstdio>
#include "../host/x86/x86_play.h"
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)

int main() {
    using namespace x86;
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry registry; register_win32(registry, host);
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000; cpu.fs_base = TEB_ADDR;
    constexpr uintptr_t native_cursor = 0x7777;
    unsigned load_calls = 0; uintptr_t set_native = 0;
    host.window_call = [&](Host::WindowCall& q) {
        q.supported = true; q.result = 0;
        if (q.name == "LoadCursorW") { ++load_calls; q.result = native_cursor; return; }
        if (q.name == "SetCursor") { set_native = q.args[0]; q.result = native_cursor; return; }
    };
    auto route = [&](const char* name) -> Thunk* {
        auto it = registry.map.find({"user32.dll", Registry::key_sym(name)});
        return it == registry.map.end() ? nullptr : &it->second;
    };
    auto args = [&](std::initializer_list<uint32_t> values) {
        unsigned slot = 0; for (uint32_t value : values) mem.w32(cpu.reg[ESP] + 4 + 4 * slot++, value);
    };
    auto load = route("LoadCursorW"), set = route("SetCursor");
    CHECK(load && set);
    if (!load || !set) { printf("x86-cursor: checks=%d failed=%d\n", checks, failures + 1); return 1; }
    // LoadCursorW(NULL, IDC_ARROW): the window route must ask the host and hand back a token.
    args({0, 32512}); const uint32_t token = load->fn(cpu);
    CHECK(load_calls == 1);
    CHECK(token >= 0xC0000000u && token != 0xCE000002u);
    // SetCursor(token) forwards the native cursor the token stands for.
    args({token}); const uint32_t applied = set->fn(cpu);
    CHECK(set_native == native_cursor);
    CHECK(applied != 0);
    printf("x86-cursor: checks=%d failed=%d\n", checks, failures);
    return failures ? 1 : 0;
}
