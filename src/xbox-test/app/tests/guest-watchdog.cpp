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
using guest_watchdog::Screen;
using guest_watchdog::Step;
using guest_watchdog::screen_for;
using guest_watchdog::step;
using guest_watchdog::containedFault;
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

    // Dead-screen precedence (bugbot r4558727825): the launch phase is stale
    // once the guest dies — it stays in its running phase and the watchdog
    // never rewinds it, so the dead page must be checked BEFORE the banner
    // or the launch splash covers the "Game not responding" page.
    {
        // Dead guest while the launch phase still says "running" (3): dead
        // page wins, not the banner.
        CHECK(screen_for(true, 3, false) == Screen::dead);
        // Same with the surface still up (dead was just latched).
        CHECK(screen_for(true, 3, true) == Screen::dead);
        // Dead during an in-flight launch (phase 1): still the dead page.
        CHECK(screen_for(true, 1, false) == Screen::dead);
        // Not dead, launch in progress with no surface yet: banner.
        CHECK(screen_for(false, 1, false) == Screen::banner);
        CHECK(screen_for(false, 3, false) == Screen::banner);
        // Not dead, surface up: game.
        CHECK(screen_for(false, 3, true) == Screen::game);
        CHECK(screen_for(false, 0, true) == Screen::game);
        // Not dead, no launch running, no surface: game (idle launcher).
        CHECK(screen_for(false, 0, false) == Screen::game);
    }

    // Recovery state machine (bugbot r4558727845): alive -> dead -> alive.
    // While dead the tick keeps evaluating; only a heartbeat that advanced
    // past the death tick clears the latch (silence recovery — faulted and
    // finished are terminal and never clear).
    {
        // Alive, no verdict: unchanged.
        Step s = step(false, true, {false, Reason::none}, false);
        CHECK(!s.dead);
        CHECK(s.guestSurface);
        // Alive, dead verdict: latch dead and take the surface away.
        s = step(false, true, {true, Reason::silent}, false);
        CHECK(s.dead);
        CHECK(!s.guestSurface);
        // Dead, verdict still dead, no heartbeat advance: stays dead.
        s = step(true, false, {true, Reason::silent}, false);
        CHECK(s.dead);
        CHECK(!s.guestSurface);
        // Dead, silence cleared (fresh present beat the death tick): recover.
        s = step(true, false, {false, Reason::none}, true);
        CHECK(!s.dead);
        CHECK(s.guestSurface);
        // Dead, heartbeat advanced but a fault is now reported: stays dead.
        s = step(true, false, {true, Reason::fault}, true);
        CHECK(s.dead);
        CHECK(!s.guestSurface);
        // Full cycle: alive -> dead -> (still silent) -> recovered.
        s = step(false, true, {true, Reason::silent}, false);
        CHECK(s.dead && !s.guestSurface);
        s = step(s.dead, s.guestSurface, {true, Reason::silent}, false);
        CHECK(s.dead && !s.guestSurface);
        s = step(s.dead, s.guestSurface, {false, Reason::none}, true);
        CHECK(!s.dead && s.guestSurface);
    }

    // One present then stall (bugbot r4558727855): the first hooked present
    // must stamp the heartbeat too, so lastActivityTick != 0 and the silence
    // clock runs. Before, the first-present path returned without stamping,
    // leaving lastActivityTick == 0 forever — which evaluate treats as
    // pre-first-present — and a game that presented once then hung was never
    // declared dead. Model the overlay's stamp-then-evaluate contract: every
    // present (including the first) sets lastActivityTick.
    {
        // The contract the overlay must uphold: a single present produces a
        // nonzero stamp, so evaluate can age it.
        const uint64_t firstPresentTick = hz * 100;
        CHECK(firstPresentTick != 0);   // a real QPC tick is never 0
        // The stall: 11 s after that one present, with the stamp in place
        // (as note_present on the first-present path now guarantees).
        Inputs in; in.ticksPerSecond = hz; in.lastActivityTick = firstPresentTick;
        in.nowTick = firstPresentTick + uint64_t((kSilenceSeconds + 1.0) * double(hz));
        auto v = evaluate(in);
        CHECK(v.dead);
        CHECK(v.reason == Reason::silent);
        // And the same trace WITHOUT the first-present stamp stays alive —
        // the pre-fix behavior, pinned so the stamp's absence is visible.
        Inputs pre; pre.ticksPerSecond = hz; pre.lastActivityTick = 0;
        pre.nowTick = firstPresentTick + uint64_t((kSilenceSeconds + 1.0) * double(hz));
        CHECK(!evaluate(pre).dead);
    }

    // Contained-crash decision (bugbot r4559006315): the SEH filters must
    // flag `faulted` only for real crashes. Play's control-flow pseudo-
    // exceptions and OutputDebugString's protocol exception are NOT faults —
    // flagging them showed the dead page for a game that exits cleanly.
    {
        // Real crashes: access violation, illegal instruction, stack
        // overflow, a game's own non-standard code, breakpoint, C++ EH.
        CHECK(containedFault(EXCEPTION_ACCESS_VIOLATION));
        CHECK(containedFault(EXCEPTION_ILLEGAL_INSTRUCTION));
        CHECK(containedFault(STATUS_STACK_OVERFLOW));
        CHECK(containedFault(EXCEPTION_BREAKPOINT));
        CHECK(containedFault(0xe0424242));   // the runner fixture's crash code
        CHECK(containedFault(0xe06d7363));   // MSVC C++ exception (.msh)
        CHECK(containedFault(0xE0000001));   // arbitrary vendor code
        // Play's control-flow pseudo-exceptions: not faults.
        CHECK(!containedFault(0xe0520002));  // exitException: normal-exit unwind
        CHECK(!containedFault(0xe0520001));  // missingException: contained missing import
        // OutputDebugString's protocol exception, raised by design in a
        // running guest (a debugger-less process still sees it in SEH).
        CHECK(!containedFault(0x40010006));  // DBG_PRINTEXCEPTION_C
        // Severity is not the discriminator — an informational debug print
        // has severity 3 (status), not an error; the code alone decides.
        // Play's own OutputDebugStringA/W replacements log directly and raise
        // no exception, but a guest that reaches a real OutputDebugString
        // (donor kernel32) gets the protocol exception in SEH; both protocol
        // codes are informational, so neither is a fault.
        CHECK(!containedFault(0x4001000A));  // DBG_PRINTEXCEPTION_WIDE_C
    }

    printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
