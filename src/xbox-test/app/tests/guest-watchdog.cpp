// SPDX-License-Identifier: GPL-3.0-or-later
// Guest watchdog decision contract (host/guest_watchdog.h). The console
// wedges this pins: a guest that crashed (0xc0000005 in unityplayer) or hung
// (device-removed present blocked in the driver) stopped presenting and the
// launcher froze on the last game frame with a stale FPS counter and no
// recoverable guide. These tests pin the decision the UI loop will act on:
// fault/exited report immediately, silence past the threshold reports only
// after kSilenceSeconds, pre-first-present silence is the launch watchdog's
// phase and stays alive, and a live guest never reports dead. No D3D, no
// locks: synthetic QPC ticks. Runs under Wine headless.
#include <windows.h>
#include <cstdio>

#include "../host/guest_watchdog.h"

static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if(!(x)){++failures;printf("FAIL line %d: %s\n",__LINE__,#x);} } while(0)

using guest_watchdog::Inputs;
using guest_watchdog::evaluate;
using guest_watchdog::Reason;
using guest_watchdog::kSilenceSeconds;

int main() {
    const uint64_t hz = 10000000;   // 10 MHz, QPC-like

    // A live guest: presents 8 ms apart, never dead.
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz; in.nowTick = hz + 80000;
        auto v = evaluate(in);
        CHECK(!v.dead);
        CHECK(v.reason == Reason::none);
    }

    // No clock yet (QPC frequency not read): cannot judge, stays alive.
    {
        Inputs in; in.ticksPerSecond = 0; in.lastActivityTick = hz; in.nowTick = hz + 100 * hz;
        CHECK(!evaluate(in).dead);
    }

    // Pre-first-present silence: the launch watchdog's phase, stays alive.
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = 0; in.nowTick = 600 * hz;
        auto v = evaluate(in);
        CHECK(!v.dead);
        CHECK(v.reason == Reason::none);
    }

    // Silence just under the threshold: still alive (a 2 s stall was seen on
    // the console before a device removal).
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz;
        in.nowTick = hz + uint64_t((kSilenceSeconds - 0.5) * double(hz));
        CHECK(!evaluate(in).dead);
    }

    // Silence past the threshold: dead by silence (the device-removal hang).
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz;
        in.nowTick = hz + uint64_t((kSilenceSeconds + 0.5) * double(hz));
        auto v = evaluate(in);
        CHECK(v.dead);
        CHECK(v.reason == Reason::silent);
    }

    // A contained guest exception reports dead immediately, regardless of
    // presents (the 0xc0000005 unityplayer crash).
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz; in.nowTick = hz + 1000;   // 10 ms after the last present
        in.guestFault = true;
        auto v = evaluate(in);
        CHECK(v.dead);
        CHECK(v.reason == Reason::fault);
    }

    // A guest thread that exited reports dead immediately (clean exit path).
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz; in.nowTick = hz + 1000;
        in.guestFinished = true;
        auto v = evaluate(in);
        CHECK(v.dead);
        CHECK(v.reason == Reason::exited);
    }

    // Fault wins over a fresh present (the fault is the stronger evidence).
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz; in.nowTick = hz; in.guestFault = true;
        auto v = evaluate(in);
        CHECK(v.dead);
        CHECK(v.reason == Reason::fault);
    }

    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
