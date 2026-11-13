// SPDX-License-Identifier: GPL-3.0-or-later
// Guest address space for the x86 (PE32) execution core. A guest pointer is a
// 32-bit index into page-granular host storage; it is never a host pointer.
// Pages carry R/W/X permissions that the interpreter enforces on every access,
// so the guest sees ordinary W^X-style protections without any host RWX memory:
// the interpreter never executes guest bytes natively, it only reads them.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace x86 {
enum : uint8_t { PROT_R = 1, PROT_W = 2, PROT_X = 4, PROT_COW = 8 };
enum class FaultKind { AccessViolation, ExecuteViolation, DivideError, InvalidOpcode, Unsupported, Breakpoint, InstructionLimit, BadThunk, UnresolvedImport };
struct Fault : std::runtime_error {
    FaultKind kind; uint32_t eip, addr;
    Fault(FaultKind k, uint32_t e, uint32_t a, const std::string& m) : std::runtime_error(m), kind(k), eip(e), addr(a) {}
};
struct Memory {
    static constexpr uint32_t PAGE = 4096;
    std::vector<uint8_t*> page = std::vector<uint8_t*>(1u << 20, nullptr);
    std::vector<uint8_t> prot = std::vector<uint8_t>(1u << 20, 0);
    std::vector<std::unique_ptr<uint8_t[]>> blocks;
    uint32_t fault_eip = 0;  // maintained by the CPU so faults name the instruction
    [[noreturn]] void fail(FaultKind k, uint32_t a, const char* what) const {
        char b[96]; snprintf(b, sizeof b, "%s at 0x%08x (eip 0x%08x)", what, a, fault_eip);
        throw Fault(k, fault_eip, a, b);
    }
    bool mapped(uint32_t a) const { return page[a >> 12] != nullptr; }
    // True when [addr, addr+size) is entirely unmapped (a load may map there).
    bool free_range(uint32_t addr, uint32_t size) const {
        if (addr % PAGE) return false;
        uint64_t end = (uint64_t(addr) + size + PAGE - 1) & ~uint64_t(PAGE - 1);
        if (end > 0x100000000ull) return false;
        for (uint64_t a = addr; a < end; a += PAGE) if (page[a >> 12]) return false;
        return true;
    }
    // Zero-filled, page-aligned region. Overlap with an existing mapping is an error.
    void map(uint32_t addr, uint32_t size, uint8_t p) {
        if (addr % PAGE || !size) throw std::runtime_error("x86 map: unaligned");
        uint64_t end = (uint64_t(addr) + size + PAGE - 1) & ~uint64_t(PAGE - 1);
        if (end > 0x100000000ull) throw std::runtime_error("x86 map: beyond 4 GiB");
        for (uint64_t a = addr; a < end; a += PAGE) if (page[a >> 12]) throw std::runtime_error("x86 map: overlap");
        size_t n = size_t((end - addr) / PAGE);
        std::unique_ptr<uint8_t[]> block(new uint8_t[n * PAGE]());
        for (size_t i = 0; i < n; ++i) { page[(addr >> 12) + i] = block.get() + i * PAGE; prot[(addr >> 12) + i] = p; }
        blocks.push_back(std::move(block));  // blocks live as long as the Memory
    }
    // Drops a mapping made by map(): its pages become unmapped again, so a later
    // load may use the range. The backing block stays in `blocks` (it is not
    // freed while the Memory lives), which keeps any stale pointer harmless.
    void unmap(uint32_t addr, uint32_t size) {
        uint64_t end = (uint64_t(addr) + size + PAGE - 1) & ~uint64_t(PAGE - 1);
        for (uint64_t a = addr; a < end; a += PAGE) { page[a >> 12] = nullptr; prot[a >> 12] = 0; }
    }
    void protect(uint32_t addr, uint32_t size, uint8_t p) {
        if (addr % PAGE) throw std::runtime_error("x86 protect: unaligned");
        uint64_t end = (uint64_t(addr) + size + PAGE - 1) & ~uint64_t(PAGE - 1);
        for (uint64_t a = addr; a < end; a += PAGE) if (!page[a >> 12]) throw std::runtime_error("x86 protect: unmapped");
        for (uint64_t a = addr; a < end; a += PAGE) prot[a >> 12] = p;
    }
    // Protection byte of the page holding `a` (0 when unmapped); VirtualQuery
    // reports this to the guest so callers that trust it stay correct.
    uint8_t protect_of(uint32_t a) const { return prot[a >> 12]; }
    uint8_t* at(uint32_t a, uint8_t need, FaultKind k) {
        uint32_t i = a >> 12;
        if (!page[i]) fail(k, a, k == FaultKind::ExecuteViolation ? "execute violation" : (need & PROT_W) ? "write access violation" : "read access violation");
        // Copy-on-write for image pages: Windows maps image sections
        // copy-on-write, so a guest write to a read-only image page (mingw's
        // runtime pseudo-relocator memcpy into .text after its VirtualQuery
        // check) promotes the page instead of faulting. Only pages mapped
        // with PROT_COW promote; the interpreter's own W^X code pages do not.
        if (!(prot[i] & need) && need == PROT_W && (prot[i] & PROT_COW)) prot[i] |= PROT_W;
        if (!(prot[i] & need)) fail(k, a, k == FaultKind::ExecuteViolation ? "execute violation" : (need & PROT_W) ? "write access violation" : "read access violation");
        return page[i] + (a & 4095);
    }
    uint8_t r8(uint32_t a) { return *at(a, PROT_R, FaultKind::AccessViolation); }
    void w8(uint32_t a, uint8_t v) { *at(a, PROT_W, FaultKind::AccessViolation) = v; }
    uint8_t fetch8(uint32_t a) { return *at(a, PROT_X, FaultKind::ExecuteViolation); }
    uint32_t r(uint32_t a, unsigned n) {
        uint32_t v = 0; for (unsigned i = 0; i < n; ++i) v |= uint32_t(r8(a + i)) << (8 * i); return v;
    }
    void w(uint32_t a, uint32_t v, unsigned n) {
        // Check the whole access first so a faulting store has no partial effect.
        for (unsigned i = 0; i < n; ++i) at(a + i, PROT_W, FaultKind::AccessViolation);
        for (unsigned i = 0; i < n; ++i) w8(a + i, uint8_t(v >> (8 * i)));
    }
    uint32_t r32(uint32_t a) { return r(a, 4); }
    void w32(uint32_t a, uint32_t v) { w(a, v, 4); }
    uint64_t r64(uint32_t a) { uint64_t v = 0; for (unsigned i = 0; i < 8; ++i) v |= uint64_t(r8(a + i)) << (8 * i); return v; }
    void w64(uint32_t a, uint64_t v) {
        for (unsigned i = 0; i < 8; ++i) at(a + i, PROT_W, FaultKind::AccessViolation);
        for (unsigned i = 0; i < 8; ++i) w8(a + i, uint8_t(v >> (8 * i)));
    }
    void r128(uint32_t a, uint32_t out[4]) { for (unsigned i = 0; i < 4; ++i) out[i] = r32(a + 4 * i); }
    void w128(uint32_t a, const uint32_t in[4]) {
        for (unsigned i = 0; i < 16; ++i) at(a + i, PROT_W, FaultKind::AccessViolation);
        for (unsigned i = 0; i < 4; ++i) w32(a + 4 * i, in[i]);
    }
    // Host-side marshalling helpers used by thunks: bounds and permission checked.
    std::vector<uint8_t> read_bytes(uint32_t a, uint32_t n) {
        std::vector<uint8_t> v(n); for (uint32_t i = 0; i < n; ++i) v[i] = r8(a + i); return v;
    }
    void write_bytes(uint32_t a, const void* src, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) at(a + i, PROT_W, FaultKind::AccessViolation);
        for (uint32_t i = 0; i < n; ++i) w8(a + i, static_cast<const uint8_t*>(src)[i]);
    }
    std::string read_cstr(uint32_t a, uint32_t max = 1u << 20) {
        std::string s; for (uint32_t i = 0; i < max; ++i) { uint8_t c = r8(a + i); if (!c) return s; s.push_back(char(c)); }
        fail(FaultKind::AccessViolation, a, "unterminated guest string");
    }
};
}
