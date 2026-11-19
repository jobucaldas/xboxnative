// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 DirectInput8 factory and the no-device IDirectInput8 COM surface used
// by SDL and games that fall through to XInput.
#pragma once
#include <cstring>
#include "x86com.h"
#if defined(_WIN32)
#include <windows.h>
namespace x86 {
namespace dinput8_iid {
inline constexpr GUID unknown{0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
inline constexpr GUID clsid{0x25E609E4,0xB259,0x11CF,{0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00}};
inline constexpr GUID input8a{0xBF798030,0x483A,0x4DA2,{0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
inline constexpr GUID input8w{0xBF798031,0x483A,0x4DA2,{0xAA,0x99,0x5D,0x64,0xED,0x36,0x97,0x00}};
}
inline constexpr uint32_t DINPUT8_DEVICENOTREG = 0x80040154u; // DIERR_DEVICENOTREG
inline constexpr uint32_t DINPUT8_UNSUPPORTED = 0x80004001u;  // DIERR_UNSUPPORTED
inline constexpr uint32_t DINPUT8_NOAGGREGATION = 0x80040110u;

struct DInput8Object {};
inline DInput8Object& dinput8_object() {
    static DInput8Object object;
    return object;
}
inline bool dinput8_guid(Cpu& cpu, uint32_t address, GUID& value) {
    if (!address) return false;
    auto bytes = cpu.mem.read_bytes(address, sizeof(value));
    std::memcpy(&value, bytes.data(), sizeof(value));
    return true;
}
inline const char* dinput8_iface(const GUID& iid) {
    if (IsEqualGUID(iid, dinput8_iid::input8a)) return "IDirectInput8A";
    if (IsEqualGUID(iid, dinput8_iid::input8w)) return "IDirectInput8W";
    return nullptr;
}
inline uint32_t dinput8_wrap(ComSpace& space, Cpu& cpu, const char* iface, void* object) {
    return space.create(cpu, iface, object, [] {}, [] {});
}
inline uint32_t dinput8_query_interface(Proxy& proxy, Cpu& cpu) {
    const uint32_t out = cpu.arg(2);
    if (!out) return E_POINTER;
    cpu.mem.w32(out, 0);
    GUID iid{};
    if (!dinput8_guid(cpu, cpu.arg(1), iid)) return E_POINTER;
    const char* iface = IsEqualGUID(iid, dinput8_iid::unknown) ? proxy.iface : dinput8_iface(iid);
    if (!iface) return E_NOINTERFACE;
    cpu.mem.w32(out, dinput8_wrap(*proxy.space, cpu, iface, proxy.host));
    return S_OK;
}
inline void register_dinput8_com(ComSpace& space) {
    space.interface_("IDirectInput8A", 11);
    space.interface_("IDirectInput8W", 11);
    auto method = [&space](const char* iface, unsigned slot, unsigned argc, Marshaller fn) {
        space.method(iface, slot, argc, std::move(fn));
    };
    for (const char* iface : {"IDirectInput8A", "IDirectInput8W"}) {
        method(iface, 0, 3, [](Proxy& p, Cpu& c) { return dinput8_query_interface(p, c); });
        // Slots 3-10 follow IDirectInput8A/W in dinput.h. The proxy deliberately
        // has no devices: SDL sees an empty DirectInput backend and probes XInput.
        method(iface, 3, 4, [](Proxy&, Cpu& c) {
            if (!c.arg(2)) return uint32_t(E_POINTER);
            c.mem.w32(c.arg(2), 0);
            return DINPUT8_DEVICENOTREG;
        });
        method(iface, 4, 5, [](Proxy&, Cpu&) { return uint32_t(S_OK); });
        method(iface, 5, 2, [](Proxy&, Cpu&) { return DINPUT8_DEVICENOTREG; });
        method(iface, 6, 3, [](Proxy&, Cpu&) { return DINPUT8_UNSUPPORTED; });
        method(iface, 7, 3, [](Proxy&, Cpu&) { return uint32_t(S_OK); });
        method(iface, 8, 4, [](Proxy&, Cpu& c) {
            if (c.arg(3)) c.mem.w32(c.arg(3), 0);
            return DINPUT8_DEVICENOTREG;
        });
        method(iface, 9, 6, [](Proxy&, Cpu&) { return DINPUT8_UNSUPPORTED; });
        method(iface, 10, 5, [](Proxy&, Cpu&) { return DINPUT8_UNSUPPORTED; });
    }
}
inline uint32_t dinput8_create(ComSpace& space, Cpu& cpu, uint32_t iid_address, uint32_t out_address, uint32_t outer) {
    if (!iid_address || !out_address) return E_POINTER;
    cpu.mem.w32(out_address, 0);
    if (outer) return DINPUT8_NOAGGREGATION;
    GUID iid{};
    if (!dinput8_guid(cpu, iid_address, iid)) return E_POINTER;
    const char* iface = dinput8_iface(iid);
    if (IsEqualGUID(iid, dinput8_iid::unknown)) iface = "IDirectInput8A";
    if (!iface) return E_NOINTERFACE;
    cpu.mem.w32(out_address, dinput8_wrap(space, cpu, iface, &dinput8_object()));
    return S_OK;
}
inline uint32_t dinput8_cocreate(ComSpace& space, Cpu& cpu) {
    if (!cpu.arg(0) || !cpu.arg(3) || !cpu.arg(4)) return E_POINTER;
    cpu.mem.w32(cpu.arg(4), 0);
    if (cpu.arg(1)) return DINPUT8_NOAGGREGATION;
    GUID clsid{}, iid{};
    if (!dinput8_guid(cpu, cpu.arg(0), clsid) || !dinput8_guid(cpu, cpu.arg(3), iid)) return E_POINTER;
    if (!IsEqualGUID(clsid, dinput8_iid::clsid)) return DINPUT8_DEVICENOTREG;
    const char* iface = dinput8_iface(iid);
    if (IsEqualGUID(iid, dinput8_iid::unknown)) iface = "IDirectInput8A";
    if (!iface) return E_NOINTERFACE;
    cpu.mem.w32(cpu.arg(4), dinput8_wrap(space, cpu, iface, &dinput8_object()));
    return S_OK;
}
inline void register_dinput8(Registry& registry, Host& host) {
    register_dinput8_com(host.com);
    auto create = [&host](Cpu& c) -> uint32_t {
        return dinput8_create(host.com, c, c.arg(2), c.arg(3), c.arg(4));
    };
    registry.add("dinput8.dll", "DirectInput8Create", Conv::Stdcall, 5, create);
    registry.add_ordinal("dinput8.dll", 1, "DirectInput8Create");
}
}
#else
namespace x86 {
inline uint32_t dinput8_cocreate(ComSpace&, Cpu&) { return 0x80040154u; }
inline void register_dinput8(Registry&, Host&) {}
}
#endif
