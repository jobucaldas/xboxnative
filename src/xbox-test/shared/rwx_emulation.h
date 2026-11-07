#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Guest RWX memory on top of the console's W^X primitives (Play only).
//
// An AppContainer process may not hold a page that is writable and executable
// at once; it may allocate read/write memory and flip it to read/execute with
// VirtualProtectFromApp (vrx console evidence: caseA RWX VirtualAlloc fails,
// caseB/caseC RW->RX run). JIT runtimes, e.g. Unity's Mono, do not use that
// pattern: their code manager asks for PAGE_EXECUTE_READWRITE once and then
// writes new methods into it for the whole run, so a flat refusal leaves them
// with a NULL code buffer (Say No! More crashed at the first trampoline).
//
// This keeps the console rule intact and gives the guest the semantics it
// asked for: a region requested as RWX is backed by RW pages, and each page
// is only ever *one* of RW or RX in the process. The guest's first instruction
// fetch from an RW page faults (DEP); the fault handler flips just that page to
// RX and resumes. A later write to an RX page faults; the handler flips it back
// to RW and resumes. Only documented *FromApp calls are used; no restriction
// is bypassed, and nothing on disk is touched.
//
// Limits (by design, logged rather than hidden):
//  - A write by the OS itself into an RX page (ReadFile into JIT memory)
//    returns an error instead of faulting; a JIT does not do that.
//  - VirtualQuery shows the real protection (RW or RX), not RWX.
//  - Alloc/protect/free are intercepted for the guest's kernel32 imports only.
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace rwx_emulation {
enum : unsigned char { Native = 0, Writable = 1, Executable = 2 };
constexpr size_t pageSize = 4096;
constexpr size_t maxRegions = 256;
struct Region {
    uintptr_t base = 0, end = 0, allocationBase = 0;
    unsigned char* pages = nullptr;   // one state byte per page of [base,end)
};
struct Stats {
    std::atomic<unsigned> regions{0}, executeFlips{0}, writeFlips{0}, failures{0}, retries{0};
};
// Plain-old-data state: constant-initialised, so the fault handler never runs
// a static initialiser, allocates or takes a CRT lock.
struct State {
    SRWLOCK lock = SRWLOCK_INIT;
    Region regions[maxRegions];
    Stats stats;
    DWORD retryThread = 0; uintptr_t retryAddress = 0; ULONG_PTR retryKind = 0; unsigned retryCount = 0;
    void (*notify)(const char*) = nullptr;
};
inline State g_state;   // C++17 inline variable: constant-initialised, no guard
inline State& state() { return g_state; }

struct Guard {
    SRWLOCK* l;
    explicit Guard(SRWLOCK* lock) : l(lock) { AcquireSRWLockExclusive(l); }
    ~Guard() { ReleaseSRWLockExclusive(l); }
};
inline bool isRwx(DWORD protection) { return (protection & 0xff) == PAGE_EXECUTE_READWRITE; }
inline bool isExecuteCopy(DWORD protection) { return (protection & 0xff) == PAGE_EXECUTE_WRITECOPY; }
inline uintptr_t down(uintptr_t a) { return a & ~(uintptr_t)(pageSize - 1); }
inline uintptr_t up(uintptr_t a) { return (a + pageSize - 1) & ~(uintptr_t)(pageSize - 1); }
inline void note(const char* text) { if (auto n = state().notify) n(text); }

// Caller holds the lock.
inline Region* find(uintptr_t address) {
    for (auto& r : state().regions) if (r.pages && address >= r.base && address < r.end) return &r;
    return nullptr;
}
inline void forget(Region& r) {
    if (r.pages) HeapFree(GetProcessHeap(), 0, r.pages);
    r = Region{};
}
// Caller holds the lock. Marks [from,to) as `value` in every region it touches.
inline void mark(uintptr_t from, uintptr_t to, unsigned char value) {
    for (auto& r : state().regions) {
        if (!r.pages || to <= r.base || from >= r.end) continue;
        for (uintptr_t a = from > r.base ? down(from) : r.base; a < to && a < r.end; a += pageSize) r.pages[(a - r.base) / pageSize] = value;
    }
}
// Caller holds the lock. A fresh region for [from,to), or an existing one is
// reused when it already covers the range. False when out of slots/memory.
inline bool track(uintptr_t from, uintptr_t to) {
    auto& s = state();
    from = down(from); to = up(to);
    if (auto* r = find(from)) { if (to <= r->end) { mark(from, to, Writable); return true; } }
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t allocation = VirtualQuery((void*)from, &mbi, sizeof mbi) ? (uintptr_t)mbi.AllocationBase : from;
    for (auto& r : s.regions) {
        if (r.pages) continue;
        auto* pages = (unsigned char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (to - from) / pageSize);
        if (!pages) return false;
        r.base = from; r.end = to; r.allocationBase = allocation; r.pages = pages;
        for (uintptr_t a = from; a < to; a += pageSize) pages[(a - from) / pageSize] = Writable;
        ++s.stats.regions;
        return true;
    }
    return false;
}
// Caller holds the lock. Translates a native "old protection" for the guest.
inline DWORD guestOld(uintptr_t address, DWORD native) {
    auto* r = find(address);
    return r && r->pages[(down(address) - r->base) / pageSize] != Native ? PAGE_EXECUTE_READWRITE : native;
}

inline LPVOID WINAPI alloc(LPVOID p, SIZE_T n, DWORD flags, DWORD protection) {
    if (isExecuteCopy(protection)) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    if (!isRwx(protection)) {
        void* r = VirtualAllocFromApp(p, n, flags, protection);
        if (r && n) { Guard g(&state().lock); mark((uintptr_t)r, up((uintptr_t)r + n), Native); }
        return r;
    }
    void* r = VirtualAllocFromApp(p, n, flags, (protection & ~0xffu) | PAGE_READWRITE);
    if (!r) return nullptr;
    bool tracked;
    { Guard g(&state().lock); tracked = n && track((uintptr_t)r, (uintptr_t)r + n); }
    if (!tracked) {
        if (flags & MEM_RESERVE) VirtualFree(r, 0, MEM_RELEASE);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    char line[160];
    snprintf(line, sizeof line, "RWX emulation: guest requested RWX at 0x%llx size=0x%llx; backed by RW pages, flipped to RX on execute",
             (unsigned long long)(uintptr_t)r, (unsigned long long)n);
    note(line);
    return r;
}
inline BOOL WINAPI protect(LPVOID p, SIZE_T n, DWORD protection, PDWORD old) {
    if (isExecuteCopy(protection)) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    DWORD previous = 0;
    const uintptr_t from = (uintptr_t)p, to = from + n;
    if (!isRwx(protection)) {
        if (!VirtualProtectFromApp(p, n, protection, &previous)) return FALSE;
        // An explicit non-RWX choice ends emulation for those pages: the guest
        // now gets exactly what it asked for.
        { Guard g(&state().lock); previous = guestOld(from, previous); if (n) mark(from, up(to), Native); }
        if (old) *old = previous;
        return TRUE;
    }
    if (!VirtualProtectFromApp(p, n, (protection & ~0xffu) | PAGE_READWRITE, &previous)) return FALSE;
    bool tracked;
    { Guard g(&state().lock); previous = guestOld(from, previous); tracked = n && track(from, to); }
    if (!tracked) {
        DWORD ignored;
        VirtualProtectFromApp(p, n, previous & 0xff ? previous : PAGE_READWRITE, &ignored);
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (old) *old = previous;
    return TRUE;
}
inline BOOL WINAPI release(LPVOID p, SIZE_T n, DWORD type) {
    BOOL ok = VirtualFree(p, n, type);
    if (!ok) return ok;
    Guard g(&state().lock);
    if (type & MEM_RELEASE) {
        for (auto& r : state().regions) if (r.pages && r.allocationBase == (uintptr_t)p) forget(r);
    } else if ((type & MEM_DECOMMIT) && n) {
        mark((uintptr_t)p, up((uintptr_t)p + n), Native);
    }
    return ok;
}

// First-chance access violation: flip the faulting page and resume, or ignore.
// No allocation, logging or CRT locks here (a log lock may be held by the very
// thread that faulted).
inline LONG fault(EXCEPTION_POINTERS* ep) {
    auto* e = ep->ExceptionRecord;
    if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || e->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const ULONG_PTR kind = e->ExceptionInformation[0];   // 1 = write, 8 = execute (DEP)
    if (kind != 1 && kind != 8) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t address = (uintptr_t)e->ExceptionInformation[1];
    const unsigned char want = kind == 8 ? Executable : Writable;
    auto& s = state();
    Guard g(&s.lock);
    auto* r = find(address);
    if (!r) return EXCEPTION_CONTINUE_SEARCH;
    unsigned char& page = r->pages[(down(address) - r->base) / pageSize];
    if (page == Native) return EXCEPTION_CONTINUE_SEARCH;
    if (page == want) {
        // Another thread flipped it between the fault and now: retry, but never
        // spin forever on a fault the flip cannot resolve.
        const DWORD thread = GetCurrentThreadId();
        if (s.retryThread == thread && s.retryAddress == down(address) && s.retryKind == kind) {
            if (++s.retryCount > 1000) return EXCEPTION_CONTINUE_SEARCH;
        } else { s.retryThread = thread; s.retryAddress = down(address); s.retryKind = kind; s.retryCount = 1; }
        ++s.stats.retries;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    DWORD previous;
    if (!VirtualProtectFromApp((void*)down(address), pageSize, kind == 8 ? PAGE_EXECUTE_READ : PAGE_READWRITE, &previous)) {
        ++s.stats.failures;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    page = want;
    ++(kind == 8 ? s.stats.executeFlips : s.stats.writeFlips);
    return EXCEPTION_CONTINUE_EXECUTION;
}
}
