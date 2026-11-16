// SPDX-License-Identifier: GPL-3.0-or-later
// Guest address space for the x86 (PE32) execution core. A guest pointer is a
// 32-bit index into page-granular host storage; it is never a host pointer.
// Pages carry R/W/X permissions that the interpreter enforces on every access,
// so the guest sees ordinary W^X-style protections without any host RWX memory:
// the interpreter never executes guest bytes natively, it only reads them.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace x86 {
enum : uint8_t { PROT_R = 1, PROT_W = 2, PROT_X = 4, PROT_COW = 8 };
enum class FaultKind { AccessViolation, ExecuteViolation, DivideError, InvalidOpcode, Unsupported, Breakpoint, InstructionLimit, BadThunk, UnresolvedImport };
struct Fault : std::runtime_error {
    FaultKind kind; uint32_t eip, addr;
    Fault(FaultKind k, uint32_t e, uint32_t a, const std::string& m) : std::runtime_error(m), kind(k), eip(e), addr(a) {}
};
struct Memory {
    static constexpr uint32_t PAGE = 4096;
    static constexpr uint64_t ADDRESS_SPACE_SIZE = 0x100000000ull;
    struct ReservationDeleter {
        void operator()(uint8_t* p) const {
            if (!p) return;
#if defined(_WIN32)
            VirtualFree(p, 0, MEM_RELEASE);
#else
            munmap(p, ADDRESS_SPACE_SIZE);
#endif
        }
    };
    std::unique_ptr<uint8_t, ReservationDeleter> storage;
    std::vector<uint8_t*> page = std::vector<uint8_t*>(1u << 20, nullptr);
    std::vector<uint8_t> prot = std::vector<uint8_t>(1u << 20, 0);
    uint32_t fault_eip = 0;  // maintained by the CPU so faults name the instruction
    Memory() : storage(reserve()) {}
    Memory(Memory&&) = default;
    Memory& operator=(Memory&&) = default;
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;
    uint8_t* base() const { return storage.get(); }
    static uint8_t* reserve() {
#if defined(_WIN32) && defined(WINAPI_FAMILY_APP) && WINAPI_FAMILY == WINAPI_FAMILY_APP
        // UWP's app allocation APIs can reserve this low 4-GiB window, but the
        // runtime's existing VA mappings may prevent a contiguous reservation.
        void* p = VirtualAllocFromApp(nullptr, size_t(ADDRESS_SPACE_SIZE), MEM_RESERVE, PAGE_NOACCESS);
#elif defined(_WIN32)
        void* p = VirtualAlloc(nullptr, size_t(ADDRESS_SPACE_SIZE), MEM_RESERVE, PAGE_NOACCESS);
#else
        void* p = mmap(nullptr, size_t(ADDRESS_SPACE_SIZE), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED) p = nullptr;
#endif
        if (!p) throw std::runtime_error("x86 memory: unable to reserve 4 GiB guest address space");
        return static_cast<uint8_t*>(p);
    }
    void commit(uint32_t addr, size_t size) {
#if defined(_WIN32) && defined(WINAPI_FAMILY_APP) && WINAPI_FAMILY == WINAPI_FAMILY_APP
        if (!VirtualAllocFromApp(base() + addr, size, MEM_COMMIT, PAGE_READWRITE))
            throw std::runtime_error("x86 memory: unable to commit guest pages");
#elif defined(_WIN32)
        if (!VirtualAlloc(base() + addr, size, MEM_COMMIT, PAGE_READWRITE))
            throw std::runtime_error("x86 memory: unable to commit guest pages");
#else
        if (mprotect(base() + addr, size, PROT_READ | PROT_WRITE) != 0)
            throw std::runtime_error("x86 memory: unable to commit guest pages");
#endif
    }
    void decommit(uint32_t addr, size_t size) {
#if defined(_WIN32)
        if (size && !VirtualFree(base() + addr, size, MEM_DECOMMIT))
            throw std::runtime_error("x86 memory: unable to decommit guest pages");
#else
        if (size && (madvise(base() + addr, size, MADV_DONTNEED) != 0 || mprotect(base() + addr, size, PROT_NONE) != 0))
            throw std::runtime_error("x86 memory: unable to decommit guest pages");
#endif
    }
    uint32_t stack_reserve_base = 0, stack_limit = 0, stack_guard = 0, stack_teb_limit = 0;
    void configure_stack(uint32_t reserve_base, uint32_t committed_limit, uint32_t teb_limit) {
        stack_reserve_base=reserve_base;stack_limit=committed_limit;stack_teb_limit=teb_limit;
        stack_guard=committed_limit>reserve_base?committed_limit-PAGE:0;
    }
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
        commit(addr, n * PAGE);
        for (size_t i = 0; i < n; ++i) { page[(addr >> 12) + i] = base() + addr + i * PAGE; prot[(addr >> 12) + i] = p; }
    }
    // Drops a mapping made by map(): its pages become unmapped and decommitted,
    // so a later load may reuse the range as zero-filled memory.
    void unmap(uint32_t addr, uint32_t size) {
        uint64_t end = (uint64_t(addr) + size + PAGE - 1) & ~uint64_t(PAGE - 1);
        uint32_t first = addr & ~(PAGE - 1);
        for (uint64_t a = first; a < end;) {
            if (!page[a >> 12]) { a += PAGE; continue; }
            uint64_t run = a;
            do { a += PAGE; } while (a < end && page[a >> 12]);
            decommit(uint32_t(run), size_t(a - run));
        }
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
        if (!page[i] && stack_guard && (a & ~(PAGE - 1)) == stack_guard && need != PROT_X) {
            map(stack_guard,PAGE,PROT_R|PROT_W);
            stack_limit=stack_guard;
            w32(stack_teb_limit,stack_limit);
            stack_guard=stack_limit>stack_reserve_base?stack_limit-PAGE:0;
        }
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
        if (n > 1 && (a & (PAGE - 1)) + n <= PAGE) {
            const uint8_t* p = at(a, PROT_R, FaultKind::AccessViolation);
            uint32_t v = 0; for (unsigned i = 0; i < n; ++i) v |= uint32_t(p[i]) << (8 * i); return v;
        }
        uint32_t v = 0; for (unsigned i = 0; i < n; ++i) v |= uint32_t(r8(a + i)) << (8 * i); return v;
    }
    void w(uint32_t a, uint32_t v, unsigned n) {
        // Check the whole access first so a faulting store has no partial effect.
        if (n > 1 && (a & (PAGE - 1)) + n <= PAGE) {
            uint8_t* p = at(a, PROT_W, FaultKind::AccessViolation);
            for (unsigned i = 0; i < n; ++i) p[i] = uint8_t(v >> (8 * i));
            return;
        }
        for (unsigned i = 0; i < n; ++i) at(a + i, PROT_W, FaultKind::AccessViolation);
        for (unsigned i = 0; i < n; ++i) w8(a + i, uint8_t(v >> (8 * i)));
    }
    bool range_accessible(uint32_t addr, uint32_t size, uint8_t need) const {
        uint64_t end = uint64_t(addr) + size;
        if (!size || end > 0x100000000ull) return size == 0;
        uint32_t first = addr >> 12, last = uint32_t((end - 1) >> 12);
        for (uint32_t i = first;; ++i) {
            if (!page[i] || (!(prot[i] & need) && !(need == PROT_W && (prot[i] & PROT_COW)))) return false;
            if (i == last) break;
        }
        return true;
    }
    // REP MOVS/STOS may use a host block operation only when the entire range
    // is already mapped and permitted. Otherwise the CPU keeps the element-wise
    // path, which preserves partial progress, guard-page growth and fault state.
    bool copy_forward(uint32_t src, uint32_t dst, uint32_t size) {
        if (!size || uint64_t(src) + size > 0x100000000ull || uint64_t(dst) + size > 0x100000000ull ||
            (uint64_t(src) < uint64_t(dst) + size && uint64_t(dst) < uint64_t(src) + size) ||
            !range_accessible(src, size, PROT_R) || !range_accessible(dst, size, PROT_W)) return false;
        uint32_t first = dst >> 12, last = uint32_t((uint64_t(dst) + size - 1) >> 12);
        for (uint32_t i = first;; ++i) {
            if (prot[i] & PROT_COW) prot[i] |= PROT_W;
            if (i == last) break;
        }
        uint32_t done = 0;
        while (done < size) {
            uint32_t sa = src + done, da = dst + done;
            uint32_t chunk = std::min<uint32_t>(size - done, std::min(PAGE - (sa & (PAGE - 1)), PAGE - (da & (PAGE - 1))));
            std::memcpy(page[da >> 12] + (da & (PAGE - 1)), page[sa >> 12] + (sa & (PAGE - 1)), chunk);
            done += chunk;
        }
        return true;
    }
    bool fill_forward(uint32_t dst, uint32_t value, unsigned element_size, uint32_t size) {
        if (!size || uint64_t(dst) + size > 0x100000000ull || !range_accessible(dst, size, PROT_W)) return false;
        uint32_t first = dst >> 12, last = uint32_t((uint64_t(dst) + size - 1) >> 12);
        for (uint32_t i = first;; ++i) {
            if (prot[i] & PROT_COW) prot[i] |= PROT_W;
            if (i == last) break;
        }
        uint32_t done = 0;
        while (done < size) {
            uint32_t a = dst + done;
            uint32_t chunk = std::min<uint32_t>(size - done, PAGE - (a & (PAGE - 1)));
            uint8_t* p = page[a >> 12] + (a & (PAGE - 1));
            if (element_size == 1) std::memset(p, uint8_t(value), chunk);
            else for (uint32_t i = 0; i < chunk; ++i)
                p[i] = uint8_t(value >> (8 * ((done + i) % element_size)));
            done += chunk;
        }
        return true;
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
