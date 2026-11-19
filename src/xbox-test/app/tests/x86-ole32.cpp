// SPDX-License-Identifier: GPL-3.0-or-later
// Focused guest OLE32 route test. It exercises guest-visible allocation,
// apartment, GUID, DirectSound class-boundary, and PROPVARIANT contracts.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../host/x86/x86_guest.h"
using namespace x86;

static int checks, failures;
#define CHECK(value) do { ++checks; if (!(value)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #value); } } while (0)

int main() {
    Memory mem; mem.map(0x1000, 0x30000, PROT_R | PROT_W); mem.map(TEB_ADDR, 0x1000, PROT_R | PROT_W);
    ThunkTable table; Host host; Registry reg; Guest guest(mem, host, reg); guest.cur_teb = 0x1000; guest.register_all();
    Cpu cpu(mem, table); cpu.reg[ESP] = 0x2f000;
    auto route = [&](const char* name, unsigned argc) -> Thunk* {
        auto it = reg.map.find({"ole32.dll", Registry::key_sym(name)});
        CHECK(it != reg.map.end());
        if (it == reg.map.end()) return nullptr;
        CHECK(it->second.conv == Conv::Stdcall); CHECK(it->second.argc == argc); return &it->second;
    };
    auto call = [&](const char* name, std::initializer_list<uint32_t> args) -> uint32_t {
        unsigned index = 0; for (uint32_t value : args) mem.w32(cpu.reg[ESP] + 4 + 4 * index++, value);
        return reg.map.at({"ole32.dll", Registry::key_sym(name)}).fn(cpu);
    };
    route("CoInitialize", 1); route("CoInitializeEx", 2); route("CoUninitialize", 0);
    route("CoTaskMemAlloc", 1); route("CoTaskMemFree", 1); route("CoTaskMemRealloc", 2);
    route("CoCreateGuid", 1); route("StringFromGUID2", 3); route("CLSIDFromString", 2); route("IIDFromString", 2);
    route("CoCreateInstance", 5); route("OleInitialize", 1); route("OleUninitialize", 0); route("PropVariantClear", 1);

    host.random_bytes = [](uint8_t* output, uint32_t size) { for (uint32_t i = 0; i < size; ++i) output[i] = uint8_t(i + 1); return true; };
    uint32_t allocation = call("CoTaskMemAlloc", {8}); CHECK(allocation != 0); mem.w32(allocation, 0x12345678u);
    uint32_t larger = call("CoTaskMemRealloc", {allocation, 16}); CHECK(larger != 0 && mem.r32(larger) == 0x12345678u);
    CHECK(call("CoTaskMemRealloc", {larger, 0}) == 0); CHECK(call("CoTaskMemFree", {0}) == 0);

    uint32_t guid = 0x4000, text = 0x4100, parsed = 0x4200;
    CHECK(call("CoCreateGuid", {guid}) == 0);
    CHECK(mem.r8(guid + 6) == 0x07 && mem.r8(guid + 7) == 0x48 && mem.r8(guid + 8) == 0x89);
    CHECK(call("StringFromGUID2", {guid, text, 39}) == 39);
    CHECK(guest.wstr(text) == u"{04030201-0605-4807-890A-0B0C0D0E0F10}");
    uint32_t input = guest.put_wstr(guest.wstr(text));
    CHECK(call("CLSIDFromString", {input, parsed}) == 0);
    for (unsigned i = 0; i < 16; ++i) CHECK(mem.r8(guid + i) == mem.r8(parsed + i));
    CHECK(call("IIDFromString", {guest.put_wstr(u"{not-a-guid}"), parsed}) == 0x800401f3u);
    CHECK(call("StringFromGUID2", {guid, text, 38}) == 0);

    mem.w32(guest.cur_teb + 0x24, 55);
    CHECK(call("CoInitialize", {0}) == 0); CHECK(call("CoInitialize", {0}) == 1);
    CHECK(call("OleInitialize", {0}) == 1); CHECK(call("CoInitializeEx", {0, 0}) == 0x80010106u);
    CHECK(call("CoInitializeEx", {0, 2}) == 1); CHECK(call("CoInitializeEx", {0, 0x40}) == 0x80070057u);
    call("OleUninitialize", {}); call("CoUninitialize", {}); call("CoUninitialize", {}); call("CoUninitialize", {});
    CHECK(guest.com_apartments.count(55) == 0);

    const uint8_t zero_guid[16] = {};
    mem.write_bytes(0x4300, zero_guid, 16); const uint8_t iid_unknown[16] = {0,0,0,0,0,0,0,0,0xc0,0,0,0,0,0,0,0x46};
    mem.write_bytes(0x4400, iid_unknown, 16); mem.w32(0x4500, 0xfeedbeefu);
    CHECK(call("CoCreateInstance", {0x4300, 0, 0x17, 0x4400, 0x4500}) == 0x80040154u && mem.r32(0x4500) == 0);
    const uint8_t clsid_sound8[16] = {0x3f,0xcc,0x01,0x39,0xb5,0x84,0xa4,0x4f,0xba,0x35,0xaa,0x81,0x72,0xb8,0xa0,0x9b};
    mem.write_bytes(0x4300, clsid_sound8, 16); uint32_t sound_hr = call("CoCreateInstance", {0x4300, 0, 0x17, 0x4400, 0x4500});
    CHECK(sound_hr == 0x80004001u); CHECK(mem.r32(0x4500) == 0);

    mem.w(0x4600, 3, 2); CHECK(call("PropVariantClear", {0x4600}) == 0 && mem.r(0x4600, 2) == 0);
    mem.w(0x4600, 8, 2); CHECK(call("PropVariantClear", {0x4600}) == 0x80004001u && mem.r(0x4600, 2) == 8);
    CHECK(call("PropVariantClear", {0}) == 0x80004003u);
    std::printf("x86-ole32: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
