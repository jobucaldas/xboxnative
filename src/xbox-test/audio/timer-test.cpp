// SPDX-License-Identifier: GPL-3.0-or-later
// mm_timer.h: multimedia timers on thread-pool timers (off-device).
#include "mm_timer.h"
#include <atomic>
#include <cstdio>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static std::atomic<int> ticks{0}; static std::atomic<UINT> seenId{0};
static void CALLBACK tick(UINT id, UINT, DWORD_PTR user, DWORD_PTR, DWORD_PTR) { seenId = id; ticks += int(user); }
int main() {
    CHECK(!mm_timer::set_event(0, 1, tick, 1, mm_timer::periodic));
    CHECK(!mm_timer::set_event(10, 1, nullptr, 1, mm_timer::periodic));
    UINT periodic = mm_timer::set_event(10, 1, tick, 1, mm_timer::periodic);
    CHECK(periodic != 0);
    Sleep(200);
    CHECK(ticks >= 5 && seenId == periodic);
    CHECK(mm_timer::kill_event(periodic) == mm_timer::noError);
    int after = ticks; Sleep(60); CHECK(ticks == after);
    CHECK(mm_timer::kill_event(periodic) == mm_timer::invalidParameter);
    ticks = 0;
    UINT once = mm_timer::set_event(10, 1, tick, 2, 0);
    Sleep(100);
    CHECK(once != 0 && ticks == 2);
    CHECK(mm_timer::kill_event(once) == mm_timer::invalidParameter);   // spent after firing
    // timeKillEvent from inside the timer's own callback (allowed on Windows).
    static std::atomic<UINT> self{0}; static std::atomic<int> selfTicks{0}; static std::atomic<UINT> selfKill{99};
    self = mm_timer::set_event(10, 1, [](UINT id, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR) {
        ++selfTicks; if (selfTicks == 2) selfKill = mm_timer::kill_event(id); }, 0, mm_timer::periodic);
    Sleep(200);
    CHECK(self != 0 && selfKill == mm_timer::noError && selfTicks == 2);
    CHECK(mm_timer::kill_event(self) == mm_timer::invalidParameter);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    UINT signalled = mm_timer::set_event(10, 1, reinterpret_cast<mm_timer::Callback>(event), 0, mm_timer::eventSet);
    CHECK(signalled && WaitForSingleObject(event, 1000) == WAIT_OBJECT_0);
    CloseHandle(event);
    std::printf("mm-timer: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
