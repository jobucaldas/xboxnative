// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <windows.h>
#include <cstdint>
#include <map>
#include <mutex>
// Multimedia timers (timeSetEvent/timeKillEvent) for consoles whose winmm
// lacks them, on thread-pool timers. Windows semantics: one-shot or periodic;
// a callback, or an event set/pulsed; ids are nonzero and become invalid when
// killed or, for one-shot timers, after they fire.
namespace mm_timer {
using Callback = void (CALLBACK*)(UINT id, UINT msg, DWORD_PTR user, DWORD_PTR, DWORD_PTR);
constexpr UINT periodic = 0x0001, eventSet = 0x0010, eventPulse = 0x0020;
constexpr UINT noError = 0, invalidParameter = 11 /*MMSYSERR_INVALPARAM*/;
struct Timer { UINT id; Callback callback; DWORD_PTR user; UINT flags; PTP_TIMER pool; };
inline std::mutex mutex;
inline std::map<UINT, Timer*> timers;
inline UINT nextId = 1;
inline void signal(Timer* t) {
    if (t->flags & eventSet) { SetEvent(reinterpret_cast<HANDLE>(t->callback)); return; }
    if (t->flags & eventPulse) {
        using Pulse = BOOL (WINAPI*)(HANDLE);
        static Pulse pulse = reinterpret_cast<Pulse>(reinterpret_cast<uintptr_t>(
            GetProcAddress(LoadLibraryExW(L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32), "PulseEvent")));
        HANDLE e = reinterpret_cast<HANDLE>(t->callback);
        if (pulse) pulse(e); else { SetEvent(e); ResetEvent(e); }
        return;
    }
    t->callback(t->id, 0, t->user, 0, 0);
}
inline VOID CALLBACK fire(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER) {
    auto t = static_cast<Timer*>(context);
    signal(t);
    if (!(t->flags & periodic)) {   // a one-shot id is spent once it fires
        std::lock_guard<std::mutex> lock(mutex);
        timers.erase(t->id);   // the Timer stays allocated: its pool timer may still be referenced
    }
}
inline UINT WINAPI set_event(UINT delay, UINT, Callback callback, DWORD_PTR user, UINT flags) {
    if (!callback || delay < 1 || delay > 1000000) return 0;
    auto t = new Timer{0, callback, user, flags, nullptr};
    t->pool = CreateThreadpoolTimer(fire, t, nullptr);
    if (!t->pool) { delete t; return 0; }
    {
        std::lock_guard<std::mutex> lock(mutex);
        do t->id = nextId++; while (!t->id || timers.count(t->id));
        timers[t->id] = t;
    }
    LARGE_INTEGER due; due.QuadPart = -LONGLONG(delay) * 10000;
    FILETIME start; start.dwLowDateTime = due.LowPart; start.dwHighDateTime = DWORD(due.HighPart);
    SetThreadpoolTimer(t->pool, &start, (flags & periodic) ? delay : 0, 0);
    return t->id;
}
inline UINT WINAPI kill_event(UINT id) {
    Timer* t = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = timers.find(id);
        if (it == timers.end()) return invalidParameter;
        t = it->second; timers.erase(it);
    }
    SetThreadpoolTimer(t->pool, nullptr, 0, 0);
    // Not from inside the timer's own callback: waiting there would deadlock.
    WaitForThreadpoolTimerCallbacks(t->pool, TRUE);
    CloseThreadpoolTimer(t->pool);
    delete t;
    return noError;
}
}
