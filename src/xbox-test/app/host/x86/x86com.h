// SPDX-License-Identifier: GPL-3.0-or-later
// Generic COM thunking for the x86 (PE32) execution core: guest proxies whose
// vtable slots are thunk stubs, marshalled onto host COM objects.
//
// A 32-bit game receives COM pointers that must live in guest memory and point
// at guest-readable vtables (the interpreter never touches host memory: a guest
// pointer is an index into the soft MMU). So every host COM object handed to
// the guest gets a proxy:
//
//   guest block:  [vtable ptr]            <- 4 bytes, what the guest holds
//   guest vtable: [stub0] [stub1] ...     <- ThunkTable stub addresses, one per
//                                            method slot; the guest can read the
//                                            vtable and call any slot
//   host side:    the real 64-bit object, plus a Proxy record that maps the
//                 guest block address back to it and counts guest references
//
// A call lands in the stub's Thunk handler, which finds the Proxy by the guest
// `this` (arg 0), runs the slot's marshaller with the Cpu (so every guest
// pointer goes through Memory and a bad one is a Fault), and returns the value
// in EAX. COM methods are stdcall, so each slot registers its stack-dword count
// (this pointer included) and the stub pops exactly that.
//
// Slots 0-2 (QueryInterface/AddRef/Release) get default implementations: the
// default QI answers E_NOINTERFACE (a concrete layer overrides it where an
// object really exports more), AddRef/Release keep the guest's count, and the
// host object holds one reference for the proxy's lifetime as a whole (taken at
// create, released at proxy destruction) — the guest's internal refcounts then
// behave exactly as on Windows without multiplying host references.
//
// Slots with no marshaller fault by interface+slot name, so a milestone subset
// is never mistaken for full API coverage (the same rule x86_win32.h applies to
// imports). A proxy destroyed by Release is removed from the identity map, so a
// later call through the stale pointer is a named Fault, not a host crash.
#pragma once
#include <map>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>
#include "x86_cpu.h"
#include "x86_mem.h"
namespace x86 {

struct Proxy;
struct ComSpace;
using Marshaller = std::function<uint32_t(Proxy&, Cpu&)>;

struct Proxy {
    uint32_t guest = 0;       // guest address of the proxy block (what the guest holds)
    uint32_t vtable = 0;      // guest address of the vtable array
    void* host = nullptr;     // the real 64-bit object (one host ref held by the space)
    const char* iface = "";   // for fault names and QI identity
    unsigned guest_refs = 1;  // the reference the guest was handed
    unsigned slots = 0;
    ComSpace* space = nullptr;
    std::function<void()> host_release;  // drops the host object's one reference
};

struct ComSpace {
    // One entry per interface: the slot count and, per slot, the stack-dword
    // count (this included) and marshaller. Slots left without a marshaller
    // fault by name when called.
    struct Slot { unsigned argc; Marshaller fn; };
    struct Iface { unsigned slots = 0; uint32_t vtable = 0; std::map<unsigned, Slot> methods; };
    std::map<std::string, Iface> ifaces;

    void interface_(const char* iface, unsigned slots) { ifaces[iface] = Iface{slots, 0, {}}; }
    void method(const char* iface, unsigned slot, unsigned argc, Marshaller fn) { ifaces[iface].methods[slot] = Slot{argc, std::move(fn)}; }

    // Map the guest regions the proxies and vtables live in. Called once, before
    // the first create: both regions are ordinary RW guest pages (the guest may
    // read its own vtable, as real COM objects allow).
    void map_regions(Cpu& cpu) {
        if (mapped) return;
        cpu.mem.map(VTABLE_BASE, VTABLE_REGION, PROT_R | PROT_W);
        cpu.mem.map(BLOCK_BASE, BLOCK_REGION, PROT_R | PROT_W);
        mapped = true;
    }

    // Create (or find) the proxy of `host` exposing `iface`. The guest receives
    // the proxy block address. `addref`/`host_release` manage the host object's
    // one reference for the proxy's whole lifetime (its IUnknown refcount).
    uint32_t create(Cpu& cpu, const char* iface, void* host, const std::function<void()>& addref, const std::function<void()>& host_release) {
        if (torn_down) throw std::runtime_error("COM: create after the space was torn down");
        map_regions(cpu);
        // Dedupe by (host, iface) via a hash index: the linear map scan made
        // create O(live proxies), quadratic under churn.
        auto dk = host_dedupe.find({host, iface});
        if (dk != host_dedupe.end()) { ++proxies.find(dk->second)->second.guest_refs; return dk->second; }
        auto it = ifaces.find(iface);
        if (it == ifaces.end()) throw std::runtime_error(std::string("COM: unregistered interface ") + iface);
        // The vtable is shared per interface and immutable once built: every
        // proxy of the interface points at the same stub array. It is built at
        // first create, when the ThunkTable is reachable, and bounds-checked
        // in the vtable window.
        if (it->second.vtable == 0) {
            if (vtable_top + it->second.slots * 4 > VTABLE_BASE + VTABLE_REGION) {
                char b[128]; snprintf(b, sizeof b, "COM: vtable window exhausted registering %s (%u slots)", iface, it->second.slots);
                throw Fault(FaultKind::AccessViolation, 0, 0, b);
            }
            it->second.vtable = vtable_top;
            vtable_top += it->second.slots * 4;
            for (unsigned s = 0; s < it->second.slots; ++s) {
                unsigned argc = 0; Marshaller fn;
                auto m = it->second.methods.find(s);
                if (m != it->second.methods.end()) { argc = m->second.argc; fn = m->second.fn; }
                else if (s == 0) { argc = 3; }
                else if (s == 1) { argc = 1; fn = default_addref; }
                else if (s == 2) { argc = 1; fn = default_release; }
                cpu.mem.w32(it->second.vtable + s * 4, cpu.thunks.add(Thunk{"com", slot_label(iface, s), Conv::Stdcall, argc,
                    [this, iface, s](Cpu& c) -> uint32_t { return dispatch(iface, s, c); }}));
            }
        }
        // Proxy blocks are 4 bytes (one vtable pointer); freed blocks are
        // recycled through a free list, so create/destroy churn never grows
        // the block window. Exhaustion with all blocks live is a named fault.
        if (block_free.empty() && block_top + 4 > BLOCK_BASE + BLOCK_REGION) {
            char b[128]; snprintf(b, sizeof b, "COM: proxy block window exhausted at 0x%08x", block_top);
            throw Fault(FaultKind::AccessViolation, 0, 0, b);
        }
        Proxy p;
        p.iface = iface; p.host = host; p.slots = it->second.slots; p.space = this; p.host_release = host_release;
        p.vtable = it->second.vtable;   // shared, immutable per interface
        if (!block_free.empty()) { p.guest = block_free.back(); block_free.pop_back(); }
        else { p.guest = block_top; block_top += 4; }
        cpu.mem.w32(p.guest, p.vtable);
        if (addref) addref();
        ++created;
        Proxy& stored = proxies.emplace(p.guest, std::move(p)).first->second;
        host_dedupe[{stored.host, stored.iface}] = stored.guest;
        return stored.guest;
    }

    Proxy& by_guest(uint32_t guest) {
        if (torn_down) {
            char b[96]; snprintf(b, sizeof b, "COM: the space was torn down (proxy 0x%08x)", guest);
            throw Fault(FaultKind::AccessViolation, 0, guest, b);
        }
        auto it = proxies.find(guest);
        if (it == proxies.end()) {
            char b[96]; snprintf(b, sizeof b, "COM: use of a released or foreign proxy at 0x%08x", guest);
            throw Fault(FaultKind::AccessViolation, 0, guest, b);
        }
        return it->second;
    }
    // Marshalled Release path: decrement, destroy at zero, return the new count.
    // The caller must not touch the Proxy& after this (the entry is gone).
    unsigned guest_release(Proxy& p) {
        if (torn_down) throw Fault(FaultKind::AccessViolation, 0, p.guest, "COM: Release on a torn-down space");
        unsigned left = --p.guest_refs;
        if (left == 0) {
            auto fn = p.host_release;
            uint32_t guest = p.guest;
            host_dedupe.erase({p.host, p.iface});
            proxies.erase(guest);
            block_free.push_back(guest);   // recycle the 4-byte block
            if (fn) fn();
        }
        return left;
    }

    unsigned live() const { return unsigned(proxies.size()); }
    unsigned long created = 0;   // observability for tests and the launch log

    // Engine teardown: the space holds one host reference per live proxy (taken
    // at create). Releasing it here means a guest that exits holding proxies, or
    // a test harness torn down mid-flight, never leaks host COM references.
    // `torn_down` makes the release idempotent and turns every later use of a
    // live proxy into a loud fault rather than a double release.
    ~ComSpace() { destroy_all(); }
    void destroy_all() {
        if (torn_down) return;
        torn_down = true;
        for (auto& [guest, p] : proxies) if (p.host_release) p.host_release();
        proxies.clear();
    }

    static constexpr uint32_t VTABLE_BASE = 0x7F000000, VTABLE_REGION = 0x00100000;
    static constexpr uint32_t BLOCK_BASE = 0x7F100000, BLOCK_REGION = 0x00100000;

private:
    struct DedupeKey { void* host; const char* iface; bool operator==(const DedupeKey& o) const { return host == o.host && (iface == o.iface || std::string_view(iface) == o.iface); } };
    struct DedupeHash { size_t operator()(const DedupeKey& k) const { return std::hash<void*>()(k.host) ^ (std::hash<std::string_view>()(k.iface) << 1); } };
    std::unordered_map<DedupeKey, uint32_t, DedupeHash> host_dedupe;  // (host, iface) -> guest block
    std::map<uint32_t, Proxy> proxies;      // keyed by guest block: stable under insert
    uint32_t vtable_top = VTABLE_BASE, block_top = BLOCK_BASE;
    std::vector<uint32_t> block_free;       // recycled proxy blocks (4 bytes each)
    bool mapped = false;
    bool torn_down = false;

    static std::string slot_label(const char* iface, unsigned s) { return std::string(iface) + "#" + std::to_string(s); }

    static uint32_t default_addref(Proxy& p, Cpu&) { return ++p.guest_refs; }
    static uint32_t default_release(Proxy& p, Cpu&) { return p.space->guest_release(p); }

    uint32_t dispatch(const char* iface, unsigned slot, Cpu& c) {
        auto it = ifaces.find(iface);
        if (it == ifaces.end()) {
            char b[128]; snprintf(b, sizeof b, "COM: %s slot %u on an unregistered interface", iface, slot);
            throw Fault(FaultKind::UnresolvedImport, c.eip, 0, b);
        }
        Proxy& p = by_guest(c.arg(0));
        auto m = it->second.methods.find(slot);
        if (m != it->second.methods.end() && m->second.fn) return m->second.fn(p, c);
        if (slot == 0) return default_qi(p, c);        // QueryInterface(riid, out)
        if (slot == 1) return default_addref(p, c);
        if (slot == 2) return default_release(p, c);
        char b[128]; snprintf(b, sizeof b, "COM: %s slot %u not implemented (proxy 0x%08x)", iface, slot, p.guest);
        throw Fault(FaultKind::UnresolvedImport, c.eip, 0, b);
    }
    static uint32_t default_qi(Proxy&, Cpu& c) {
        uint32_t out = c.arg(2);
        if (!out) return 0x80004003u;  // E_POINTER
        c.mem.w32(out, 0);
        return 0x80004002u;            // E_NOINTERFACE: the concrete layer overrides
    }
};

} // namespace x86
