// SPDX-License-Identifier: GPL-3.0-or-later
// Focused guest oleaut32 and CoSetProxyBlanket route contract test.
#include <cstdio>
#include <initializer_list>
#include "../host/x86/x86_guest.h"
using namespace x86;

static int checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #value); } } while (0)

int main() {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000; guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000; cpu.fs_base = guest.cur_teb;
    auto route = [&](const char* dll, const char* name, unsigned argc) {
        auto it = reg.map.find({dll, Registry::key_sym(name)}); CHECK(it != reg.map.end());
        if (it != reg.map.end()) { CHECK(it->second.conv == Conv::Stdcall); CHECK(it->second.argc == argc); }
    };
    route("ole32.dll", "CoSetProxyBlanket", 8); route("oleaut32.dll", "SysAllocString", 1);
    route("oleaut32.dll", "SysFreeString", 1); route("oleaut32.dll", "VariantInit", 1); route("oleaut32.dll", "VariantClear", 1);
    CHECK(reg.ordinals.at({"oleaut32.dll", 2}) == Registry::key_sym("SysAllocString"));
    CHECK(reg.ordinals.at({"oleaut32.dll", 6}) == Registry::key_sym("SysFreeString"));
    CHECK(reg.ordinals.at({"oleaut32.dll", 8}) == Registry::key_sym("VariantInit"));
    CHECK(reg.ordinals.at({"oleaut32.dll", 9}) == Registry::key_sym("VariantClear"));
    auto call = [&](const char* dll, const char* name, std::initializer_list<uint32_t> args) -> uint32_t {
        unsigned index = 0; for (uint32_t value : args) mem.w32(cpu.reg[ESP] + 4 + 4 * index++, value);
        return reg.map.at({dll, Registry::key_sym(name)}).fn(cpu);
    };
    uint32_t input = guest.put_wstr(u"Melty"); uint32_t bstr = call("oleaut32.dll", "SysAllocString", {input});
    CHECK(bstr != 0 && guest.wstr(bstr) == u"Melty" && mem.r32(bstr - 4) == 10);
    uint32_t bstr_base = bstr - 4; CHECK(call("oleaut32.dll", "SysFreeString", {bstr}) == 0); uint32_t recycled = guest.heap.alloc(16); CHECK(recycled == bstr_base); guest.heap.release(recycled);
    CHECK(call("oleaut32.dll", "SysAllocString", {0}) == 0);
    uint32_t variant = 0x4100; for (unsigned i = 0; i < 16; ++i) mem.w8(variant + i, 0xa5);
    bool zero = call("oleaut32.dll", "VariantInit", {variant}) == 0; for (unsigned i = 0; i < 16; ++i) zero = zero && mem.r8(variant + i) == 0; CHECK(zero);
    mem.w(variant, 8, 2); uint32_t owned = call("oleaut32.dll", "SysAllocString", {input}); mem.w32(variant + 8, owned);
    uint32_t owned_base = owned - 4; bool cleared = call("oleaut32.dll", "VariantClear", {variant}) == 0; for (unsigned i = 0; i < 16; ++i) cleared = cleared && mem.r8(variant + i) == 0; uint32_t recycled_variant = guest.heap.alloc(16); CHECK(cleared && recycled_variant == owned_base); guest.heap.release(recycled_variant);
    mem.w(variant, 3, 2); CHECK(call("oleaut32.dll", "VariantClear", {variant}) == 0x80004001u && mem.r(variant, 2) == 3);
    CHECK(call("oleaut32.dll", "VariantClear", {0}) == 0x80070057u);
    CHECK(call("ole32.dll", "CoSetProxyBlanket", {0, 0, 0, 0, 0, 0, 0, 0}) == 0x80004001u);
    std::printf("x86-oleaut: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
