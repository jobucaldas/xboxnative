// SPDX-License-Identifier: GPL-3.0-or-later
// PE32 -> guest address space: maps a relocated image (via the existing, bounds
// checked wxl::Pe32 mapper) at its ImageBase, applies per-section protections,
// binds the IAT to host thunk stubs from an explicit allowlist, and builds the
// minimal TEB32/PEB32 + stack a CRT-free entry point needs. Imports outside the
// allowlist are bound to stubs that Fault when *called* (never silently stubbed).
#pragma once
#include <map>
#include "../wxl_pe32.h"
#include "x86_cpu.h"
namespace x86 {
// Guest layout (all fixed, outside every ImageBase a normal exe uses).
constexpr uint32_t STACK_TOP = 0x00300000, STACK_SIZE = 0x00100000;
constexpr uint32_t TEB_ADDR = 0x7FFDE000, PEB_ADDR = 0x7FFDF000;
struct Registry {  // (lowercase dll, symbol) -> thunk definition
    std::map<std::pair<std::string, std::string>, Thunk> map;
    void add(const std::string& dll, const std::string& name, Conv conv, unsigned argc, std::function<uint32_t(Cpu&)> fn) {
        map[{dll, name}] = Thunk{dll, name, conv, argc, std::move(fn)};
    }
};
struct Image {
    uint32_t base = 0, size = 0, entry = 0;
    unsigned relocations = 0, imports = 0, bound = 0;
    std::vector<std::string> unresolved;
};
// stdcall argument bytes of an unresolved import are unknown; the stub faults before returning.
inline uint32_t u32(const std::vector<uint8_t>& b, size_t p) { return wxl::Pe32::read(b, p); }
inline Image load_pe32(Memory& mem, ThunkTable& table, const Registry& reg, const std::vector<uint8_t>& file, uint32_t load_base = 0) {
    if (file.size() < 0x40) throw std::runtime_error("PE32 truncated");
    size_t nt = u32(file, 0x3c);
    uint32_t preferred = u32(file, nt + 24 + 28);
    wxl::Pe32 pe; pe.map(file, load_base ? load_base : preferred);
    Image img; img.base = pe.logical; img.size = uint32_t(pe.bytes.size()); img.entry = pe.logical + pe.entry; img.relocations = pe.relocations;
    // Sections: headers R; each section gets its own page protections.
    mem.map(img.base, img.size, PROT_R | PROT_W);
    mem.write_bytes(img.base, pe.bytes.data(), img.size);
    // Bind imports while the whole image is still writable.
    uint32_t imp = u32(pe.bytes, nt + 24 + 96 + 8);
    if (imp) for (size_t d = imp;; d += 20) {
        uint32_t name = u32(pe.bytes, d + 12);
        if (!name) break;
        std::string dll = pe.string(name); for (auto& c : dll) if (c >= 'A' && c <= 'Z') c += 32;
        size_t src = u32(pe.bytes, d), dst = u32(pe.bytes, d + 16); if (!src) src = dst;
        for (unsigned j = 0;; ++j) {
            uint32_t t = u32(pe.bytes, src + size_t(j) * 4); if (!t) break;
            std::string sym = (t & 0x80000000u) ? "#" + std::to_string(t & 65535) : pe.string(t + 2);
            ++img.imports;
            auto it = reg.map.find({dll, sym}); uint32_t stub;
            if (it != reg.map.end()) { stub = table.add(it->second); ++img.bound; }
            else {
                img.unresolved.push_back(dll + "!" + sym);
                std::string label = dll + "!" + sym;
                stub = table.add(Thunk{dll, sym, Conv::Stdcall, 0, [label](Cpu& c) -> uint32_t {
                    throw Fault(FaultKind::UnresolvedImport, c.eip, 0, "call to unresolved import " + label); }});
            }
            mem.w32(img.base + dst + j * 4, stub);
        }
    }
    // Section protections (headers stay read-only).
    size_t opt = nt + 24, ns = wxl::Pe32::read(pe.bytes, nt + 6, 2), secs = opt + wxl::Pe32::read(pe.bytes, nt + 20, 2);
    mem.protect(img.base, img.size, PROT_R);  // headers and any gap between sections
    for (size_t i = 0; i < ns; ++i) {
        size_t s = secs + 40 * i; uint32_t va = u32(pe.bytes, s + 12), vs = std::max(u32(pe.bytes, s + 8), u32(pe.bytes, s + 16)), ch = u32(pe.bytes, s + 36);
        uint8_t p = PROT_R | ((ch & 0x80000000u) ? PROT_W : 0) | ((ch & 0x20000000u) ? PROT_X : 0);
        mem.protect(img.base + va, vs, p);
    }
    return img;
}
// Thunk page (read+execute, never fetched through the interpreter), stack, TEB32, PEB32.
inline void map_thunk_page(Memory& mem, const ThunkTable& table) {
    uint32_t len = (table.span() + 4095) & ~4095u;
    mem.map(table.base, len, PROT_R | PROT_W);
    std::vector<uint8_t> int3(len, 0xCC); mem.write_bytes(table.base, int3.data(), len);
    mem.protect(table.base, len, PROT_R | PROT_X);
}
inline void init_process(Memory& mem, Cpu& cpu, const Image& img, uint32_t pid = 0x1000, uint32_t tid = 0x1004) {
    mem.map(STACK_TOP - STACK_SIZE, STACK_SIZE, PROT_R | PROT_W);
    mem.map(TEB_ADDR, 4096, PROT_R | PROT_W); mem.map(PEB_ADDR, 4096, PROT_R | PROT_W);
    mem.w32(TEB_ADDR + 0x00, 0xFFFFFFFFu);        // ExceptionList (empty chain)
    mem.w32(TEB_ADDR + 0x04, STACK_TOP);          // StackBase
    mem.w32(TEB_ADDR + 0x08, STACK_TOP - STACK_SIZE);  // StackLimit
    mem.w32(TEB_ADDR + 0x18, TEB_ADDR);           // Self
    mem.w32(TEB_ADDR + 0x20, pid); mem.w32(TEB_ADDR + 0x24, tid);
    mem.w32(TEB_ADDR + 0x30, PEB_ADDR);           // ProcessEnvironmentBlock
    mem.w32(PEB_ADDR + 0x08, img.base);           // ImageBaseAddress
    mem.w32(PEB_ADDR + 0xA4, 10); mem.w32(PEB_ADDR + 0xA8, 0);  // OSMajorVersion/OSMinorVersion
    cpu.fs_base = TEB_ADDR; cpu.reg[ESP] = STACK_TOP - 16; cpu.eip = img.entry;
}
// Runs the entry point like BaseProcessStart: entry(PEB) and an implicit ExitProcess(eax).
inline uint32_t run_entry(Cpu& cpu, const Image& img) {
    uint32_t r = cpu.call(img.entry, {PEB_ADDR});
    if (!cpu.exited) { cpu.exited = true; cpu.exit_code = r; }
    return cpu.exit_code;
}
}
