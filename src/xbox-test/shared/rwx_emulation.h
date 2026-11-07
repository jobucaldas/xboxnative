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
//  - A flip costs one VirtualProtectFromApp syscall. Threads that execute one
//    page while another writes it make progress (every fault completes at
//    least the faulting instruction) but pay a flip per switch.
//  - No FlushInstructionCache is issued on a flip: x86-64 keeps instruction
//    and data caches coherent, and a JIT flushes what it emitted itself.
//  - Alloc/protect/free/query are intercepted for the guest's kernel32 imports
//    only (Play's own loader does not use them).
//  - The table lock is also taken by alloc/protect/free of tracked memory.
//    Fresh non-RWX allocations and VirtualQuery never block on it, so a GC
//    that stops the world while a thread sits in the fault handler cannot
//    deadlock on its own bookkeeping allocations.
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace rwx_emulation {
enum : unsigned char { Native = 0, Writable = 1, Executable = 2 };
constexpr size_t pageSize = 4096;
constexpr size_t maxRegions = 1024;
struct Region {
    uintptr_t base = 0, end = 0, allocationBase = 0;
    unsigned char* pages = nullptr;   // one state byte per page of [base,end)
};
struct Stats {
    std::atomic<unsigned> regions{0}, executeFlips{0}, writeFlips{0}, failures{0}, retries{0}, live{0}, exhausted{0};
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
    if (r.pages) { HeapFree(GetProcessHeap(), 0, r.pages); state().stats.live.fetch_sub(1, std::memory_order_relaxed); }
    r = Region{};
}
// Caller holds the lock. Marks [from,to) as `value` in every region it touches.
inline void mark(uintptr_t from, uintptr_t to, unsigned char value) {
    for (auto& r : state().regions) {
        if (!r.pages || to <= r.base || from >= r.end) continue;
        for (uintptr_t a = from > r.base ? down(from) : r.base; a < to && a < r.end; a += pageSize) r.pages[(a - r.base) / pageSize] = value;
    }
}
// Caller holds the lock. Tracks [from,to) as writable RWX-emulated pages. A
// range that touches existing regions is merged with them into one region, so
// a page is never described by two tables that could disagree. False (and
// nothing changed) when out of slots or memory.
inline bool track(uintptr_t from, uintptr_t to) {
    auto& s = state();
    from = down(from); to = up(to);
    uintptr_t base = from, end = to;
    unsigned overlaps = 0;
    for (auto& r : s.regions) {
        if (!r.pages || to <= r.base || from >= r.end) continue;
        ++overlaps;
        if (r.base < base) base = r.base;
        if (r.end > end) end = r.end;
    }
    if (overlaps == 1) {
        auto* r = find(from);
        if (r && r->base <= from && to <= r->end) { mark(from, to, Writable); return true; }
    }
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t allocation = VirtualQuery((void*)from, &mbi, sizeof mbi) ? (uintptr_t)mbi.AllocationBase : from;
    auto* pages = (unsigned char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (end - base) / pageSize);
    if (!pages) return false;
    Region* slot = nullptr;
    for (auto& r : s.regions) {
        if (r.pages && !(to <= r.base || from >= r.end)) { if (!slot) slot = &r; }
    }
    if (!slot) for (auto& r : s.regions) if (!r.pages) { slot = &r; break; }
    if (!slot) { HeapFree(GetProcessHeap(), 0, pages); ++s.stats.exhausted; return false; }
    for (auto& r : s.regions) {   // carry over the state of what was tracked
        if (!r.pages || to <= r.base || from >= r.end) continue;
        for (uintptr_t a = r.base; a < r.end; a += pageSize) pages[(a - base) / pageSize] = r.pages[(a - r.base) / pageSize];
    }
    for (uintptr_t a = from; a < to; a += pageSize) pages[(a - base) / pageSize] = Writable;
    for (auto& r : s.regions) if (r.pages && !(to <= r.base || from >= r.end)) forget(r);
    slot->base = base; slot->end = end; slot->allocationBase = allocation; slot->pages = pages;
    ++s.stats.regions; s.stats.live.fetch_add(1, std::memory_order_relaxed);
    return true;
}
// Caller holds the lock. Translates a native "old protection" for the guest.
inline DWORD guestOld(uintptr_t address, DWORD native) {
    auto* r = find(address);
    return r && r->pages[(down(address) - r->base) / pageSize] != Native ? PAGE_EXECUTE_READWRITE : native;
}

inline LPVOID WINAPI alloc(LPVOID p, SIZE_T n, DWORD flags, DWORD protection) {
    if (isExecuteCopy(protection)) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    auto& s = state();
    if (!isRwx(protection)) {
        // A fresh allocation cannot be tracked yet: stay lock-free for it.
        if (!p || !s.stats.live.load(std::memory_order_relaxed)) return VirtualAllocFromApp(p, n, flags, protection);
        Guard g(&s.lock);
        void* r = VirtualAllocFromApp(p, n, flags, protection);
        if (r && n) mark((uintptr_t)r, up((uintptr_t)r + n), Native);
        return r;
    }
    const DWORD readWrite = (protection & ~0xffu) | PAGE_READWRITE;
    // Reserving only: protection is not applied yet, nothing to flip. The
    // later MEM_COMMIT with RWX is what gets tracked.
    if (!(flags & MEM_COMMIT)) return VirtualAllocFromApp(p, n, flags, readWrite);
    void* r;
    bool tracked = false;
    {
        Guard g(&s.lock);
        r = VirtualAllocFromApp(p, n, flags, readWrite);
        if (!r) return nullptr;
        tracked = n && track((uintptr_t)r, (uintptr_t)r + n);
        if (!tracked && (flags & MEM_RESERVE)) VirtualFree(r, 0, MEM_RELEASE);
    }
    if (!tracked) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        note("RWX emulation: region table exhausted or out of memory; RWX request refused");
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
    DWORD native = 0, previous = 0;
    const uintptr_t from = (uintptr_t)p, to = from + n;
    {
        // The lock spans the syscall so a concurrent fault never sees a page
        // whose real protection and table state disagree.
        Guard g(&state().lock);
        if (!isRwx(protection)) {
            if (!VirtualProtectFromApp(p, n, protection, &native)) return FALSE;
            // An explicit non-RWX choice ends emulation for those pages: the
            // guest now gets exactly what it asked for.
            previous = guestOld(from, native);
            if (n) mark(from, up(to), Native);
        } else {
            if (!VirtualProtectFromApp(p, n, (protection & ~0xffu) | PAGE_READWRITE, &native)) return FALSE;
            previous = guestOld(from, native);
            if (!n || !track(from, to)) {
                DWORD ignored;
                VirtualProtectFromApp(p, n, native, &ignored);
                SetLastError(ERROR_NOT_ENOUGH_MEMORY);
                return FALSE;
            }
        }
    }
    if (old) *old = previous;
    return TRUE;
}
inline BOOL WINAPI release(LPVOID p, SIZE_T n, DWORD type) {
    auto& s = state();
    if (!s.stats.live.load(std::memory_order_relaxed)) return VirtualFree(p, n, type);
    // Free and forget under one lock: another thread's allocation may be given
    // the same address the instant VirtualFree returns, and must not meet this
    // thread's stale region.
    Guard g(&s.lock);
    BOOL ok = VirtualFree(p, n, type);
    if (!ok) return ok;
    const DWORD error = GetLastError();
    if (type & MEM_RELEASE) {
        for (auto& r : s.regions) if (r.pages && r.allocationBase == (uintptr_t)p) forget(r);
    } else if ((type & MEM_DECOMMIT) && n) {
        mark((uintptr_t)p, up((uintptr_t)p + n), Native);
    }
    SetLastError(error);
    return ok;
}
// VirtualQuery as the guest expects it: emulated pages report RWX, and the
// region runs over neighbouring emulated pages whatever their current flip.
// Never blocks (a GC may query while the world is stopped): when the table is
// busy the real answer is returned.
inline SIZE_T WINAPI query(LPCVOID address, PMEMORY_BASIC_INFORMATION mbi, SIZE_T length) {
    SIZE_T got = VirtualQuery(address, mbi, length);
    auto& s = state();
    if (!got || length < sizeof(MEMORY_BASIC_INFORMATION) || mbi->State != MEM_COMMIT || !s.stats.live.load(std::memory_order_relaxed)) return got;
    if (!TryAcquireSRWLockExclusive(&s.lock)) return got;
    if (auto* r = find((uintptr_t)address)) {
        auto stateAt = [&](uintptr_t a) { return r->pages[(a - r->base) / pageSize]; };
        uintptr_t page = down((uintptr_t)address);
        if (stateAt(page) != Native) {
            uintptr_t first = page, last = page + pageSize;
            while (first > r->base && stateAt(first - pageSize) != Native) first -= pageSize;
            while (last < r->end && stateAt(last) != Native) last += pageSize;
            mbi->BaseAddress = (void*)first;
            mbi->RegionSize = last - first;
            mbi->Protect = (mbi->Protect & ~0xffu) | PAGE_EXECUTE_READWRITE;
        }
    }
    ReleaseSRWLockExclusive(&s.lock);
    return got;
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
