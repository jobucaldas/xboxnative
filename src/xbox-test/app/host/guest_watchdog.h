// SPDX-License-Identifier: GPL-3.0-or-later
// Guest watchdog: the launcher-side decision that the guest is dead, from
// signals the UI thread already has. Pure logic, no D3D and no locks, so
// tests drive it with synthetic clocks (tests/guest-watchdog.cpp).
//
// Why: a guest that crashes (contained by play_runtime's topLevelFilter) or
// hangs (device-removal present blocked in the driver) stops presenting. The
// overlay's draw runs only inside a hooked guest present, so guide, status
// and the FPS counter all freeze with the last game frame, and the launcher
// UI loop has nothing of its own to show. The watchdog turns "no presents,
// no heartbeat, or a reported guest fault" into an explicit state the loop
// can act on: show the launcher again, report fps 0, let the guide open, and
// offer force quit.
#pragma once
#include <cstdint>

namespace guest_watchdog {

// Seconds without a guest present (or other heartbeat) before the guest is
// declared dead. A 2000 ms present stall was logged on the console before a
// device removal; 10 s clears every transient stall seen in captures while
// still recovering the screen within a player's patience.
constexpr double kSilenceSeconds = 10.0;

enum class Reason : uint8_t {
    none = 0,      // guest looks alive
    silent,        // no present/heartbeat for kSilenceSeconds
    exited,        // guest thread finished (clean exit or contained crash)
    fault          // play_runtime reported a contained guest exception
};

// The single piece of guest state the decision reads: the QPC tick of the
// last guest activity (a present, or a pad poll the guest drove). 0 = no
// activity ever observed.
struct Inputs {
    uint64_t lastActivityTick = 0;   // guest_overlay's last present (QPC)
    uint64_t nowTick = 0;            // QPC now
    uint64_t ticksPerSecond = 0;     // QPC frequency
    bool guestFault = false;         // play_runtime: contained guest exception
    bool guestFinished = false;      // play_runtime: guest thread ended
};

struct Verdict {
    bool dead = false;
    Reason reason = Reason::none;
};

// Before the guest's first present there is no heartbeat to age: that phase
// (image mapping, DllMain, engine load) is the launch watchdog's job
// (launch.h, 110 s), not this one's, so a missing first present stays alive
// here. Once presents flow, silence means the screen the player is watching
// can no longer update.
constexpr Verdict evaluate(const Inputs& in) {
    if (in.guestFault) return {true, Reason::fault};
    if (in.guestFinished) return {true, Reason::exited};
    if (in.ticksPerSecond == 0) return {false, Reason::none};   // no clock yet: cannot judge
    if (in.lastActivityTick == 0) return {false, Reason::none}; // pre-first-present: launch watchdog's phase
    const uint64_t elapsed = in.nowTick - in.lastActivityTick;
    if (elapsed > uint64_t(kSilenceSeconds * double(in.ticksPerSecond)))
        return {true, Reason::silent};
    return {false, Reason::none};
}

} // namespace guest_watchdog
