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

    // lastActivityTick NEWER than nowTick (the guest stamped a present
    // between the host's two atomic reads, or cross-core QPC skew): the
    // unsigned gap would wrap to ~2^64 and report a live game silent.
    {
        Inputs in; in.ticksPerSecond = hz;
        in.lastActivityTick = hz * 1000;
        in.nowTick = hz * 1000 - hz / 10;   // now is 100 ms BEHIND the last present
        auto v = evaluate(in);
        CHECK(!v.dead);
        CHECK(v.reason == Reason::none);
    }

    // Exactly equal ticks: zero elapsed, alive.
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz * 5000; in.nowTick = hz * 5000;
        auto v = evaluate(in);
        CHECK(!v.dead);
        CHECK(v.reason == Reason::none);
    }

    // Exactly at the threshold: alive (only strictly past it is dead).
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz;
        in.nowTick = hz + uint64_t(kSilenceSeconds * double(hz));
        auto v = evaluate(in);
        CHECK(!v.dead);
        CHECK(v.reason == Reason::none);
    }

    // One tick past the threshold: dead.
    {
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = hz;
        in.nowTick = hz + uint64_t(kSilenceSeconds * double(hz)) + 1;
        auto v = evaluate(in);
        CHECK(v.dead);
        CHECK(v.reason == Reason::silent);
    }

    // QPC near UINT64 wrap: the threshold math must not overflow the
    // multiply and must still judge the real gap. nowTick wraps past 0, so
    // the gap is (limit - last) + now; the saturating comparison must judge
    // the true elapsed time, not the wrapped difference.
    {
        const uint64_t nearWrap = ~0ull - hz * 3;   // 3 s of ticks before wrap
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = nearWrap;
        in.nowTick = nearWrap + uint64_t(kSilenceSeconds * double(hz)) + 1;   // wraps past 0
        auto v = evaluate(in);
        CHECK(v.dead);
        CHECK(v.reason == Reason::silent);
    }
    {
        const uint64_t nearWrap = ~0ull - hz * 3;
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = nearWrap;
        in.nowTick = nearWrap + hz;   // 1 s of presents ago, crossing the wrap
        CHECK(!evaluate(in).dead);
    }

    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
