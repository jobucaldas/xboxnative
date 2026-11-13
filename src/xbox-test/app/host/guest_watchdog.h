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
#include <windows.h>   // HRESULT, DXGI_STATUS/ERROR codes, DWORD (format enums)

namespace guest_watchdog {

// Seconds without a guest present (or other heartbeat) before the guest is
// declared dead. A 2000 ms present stall was logged on the console before a
// device removal; 10 s clears every transient stall seen in captures while
// still recovering the screen within a player's patience.
constexpr double kSilenceSeconds = 10.0;

// A gap above this many ticks cannot be real silence — it is out-of-order
// reads (or a wrap the caller did not align); treat it as fresh. One day at
// QPC's usual 10 MHz.
constexpr uint64_t kMaxGapTicks = 86400ull * 10000000ull;

enum class Reason : uint8_t {
    none = 0,      // guest looks alive
    silent,        // no present/heartbeat for kSilenceSeconds
    exited,        // guest thread finished (clean exit or contained crash)
    fault          // play_runtime reported a contained guest exception
};

// Home-tab rows (the guide's only tab the dead state can reach): the same
// indexes guest_overlay::Item and menu_command's select map on.
constexpr int kHomeRows = 3;          // Resume, Quit game, Force quit
constexpr int kForceQuitRow = 2;      // focus lands here when the guide opens while dead

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
//
// nowTick and lastActivityTick are two separate atomic reads, so they can
// arrive out of order (cross-core QPC skew, or the guest stamped a present
// between the two reads). The gap is computed with wrapping subtraction —
// which is also what makes a QPC crossing UINT64 wrap judge correctly — and
// a gap larger than a day of ticks can only be that skew, not real silence,
// so it saturates to 0 instead of declaring a live game dead. The threshold
// is computed divide-first so the multiply cannot overflow.
constexpr Verdict evaluate(const Inputs& in) {
    if (in.guestFault) return {true, Reason::fault};
    if (in.guestFinished) return {true, Reason::exited};
    if (in.ticksPerSecond == 0) return {false, Reason::none};   // no clock yet: cannot judge
    if (in.lastActivityTick == 0) return {false, Reason::none}; // pre-first-present: launch watchdog's phase
    const uint64_t gap = in.nowTick - in.lastActivityTick;      // wrapping
    const uint64_t elapsed = gap > kMaxGapTicks ? 0 : gap;      // out-of-order reads: fresh, not silent
    // kSilenceSeconds * hz without overflow: whole seconds first, then the
    // fractional remainder scaled down before it meets hz.
    const uint64_t limitTicks = uint64_t(kSilenceSeconds) * in.ticksPerSecond
                                + uint64_t((kSilenceSeconds - uint64_t(kSilenceSeconds)) * 1000.0)
                                      * in.ticksPerSecond / 1000;
    if (elapsed > limitTicks)
        return {true, Reason::silent};
    return {false, Reason::none};
}

// Which screen the launcher loop draws, in precedence order. The dead page
// must beat the launch banner: the launch phase number is stale once the
// guest is declared dead (it stays in its "running" phase while the guest
// ran and is never rewound by the watchdog), so without this ordering the
// launch splash would cover the "Game not responding" page exactly in the
// scenario the watchdog exists for. Mirrors ui_nav::launch_banner_up's
// banner condition with dead-first precedence.
enum class Screen : uint8_t { game = 0, banner, dead };
constexpr Screen screen_for(bool guestDead, int launchPhase, bool guestSurface) {
    if (guestDead) return Screen::dead;
    if ((launchPhase == 1 || launchPhase == 3) && !guestSurface) return Screen::banner;
    return Screen::game;
}

// One watchdog step of the dead/recovery state. A dead verdict latches
// guestDead; while latched the tick keeps evaluating, and only a heartbeat
// that advanced past the tick recorded at death clears it (the guest is
// presenting again — possible only after a silence verdict, because
// faulted/finished are never cleared). Returns the post-step state.
struct Step {
    bool dead = false;         // guestDead after this step
    bool guestSurface = false; // m_guestSurface after this step
};
constexpr Step step(bool wasDead, bool hadSurface, const Verdict& v, bool heartbeatAdvanced) {
    if (!wasDead) {
        if (v.dead) return {true, false};   // latch: take the surface away
        return {false, hadSurface};         // alive, unchanged
    }
    if (v.dead) return {true, false};            // still (or newly) dead
    if (heartbeatAdvanced) return {false, true}; // fresh present: recover
    return {true, false};                        // still silent: stays dead
}

// Which guest exceptions are CONTAINED crashes (the watchdog's fault signal)
// versus the pseudo-exceptions play_runtime raises for control flow. A clean
// exit goes through exitProcess() — no exception — but the SEH filters still
// see the pseudo-codes when a guest or Play raises them, and none of those is
// a fault: exitException is Play's own normal-exit unwind (no longer raised —
// exitProcess ends the thread directly — kept for the filter contract),
// missingException is a contained missing-import hit, and DBG_PRINTEXCEPTION_C
// is OutputDebugString's protocol exception, raised by design in a running
// guest. Everything else reaching the filter is a real crash.
constexpr DWORD kExitException = 0xe0520002;       // play_runtime's normal-exit unwind
constexpr DWORD kMissingException = 0xe0520001;    // contained missing-import hit
constexpr DWORD kDebugPrint = 0x40010006;          // DBG_PRINTEXCEPTION_C (OutputDebugString)
constexpr DWORD kDebugPrintWide = 0x4001000A;      // DBG_PRINTEXCEPTION_WIDE_C (same protocol)
constexpr bool containedFault(DWORD code) {
    return code != kExitException && code != kMissingException
        && code != kDebugPrint && code != kDebugPrintWide;
}

// The heartbeat decision for one guest Present result (bugbot
// r4559178794): a hooked present that FAILS must not stamp the heartbeat,
// or a guest that keeps calling Present into a removed device — getting
// DXGI_ERROR_DEVICE_REMOVED back every frame while the screen is frozen —
// stays "alive" forever. S_OK and DXGI_STATUS_OCCLUDED (a live game the
// compositor temporarily hides) keep the heartbeat; a lost device is
// reported immediately (no 10 s wait: the screen is already dead); any
// other failure stamps nothing but does not kill (transient — e.g.
// DXGI_ERROR_INVALID_CALL while the guest rebinds targets).
struct PresentAlive { enum : uint8_t { yes = 0, no, lost }; };
constexpr uint8_t present_alive(HRESULT hr) {
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG
        || hr == DXGI_ERROR_DEVICE_RESET)
        return PresentAlive::lost;
    return SUCCEEDED(hr) ? PresentAlive::yes : PresentAlive::no;
}

// Pad routing while the dead page is up (bugbot r4559305224, extended by
// r4559524001): the dead guest's pad must drive ONLY the guide — never the
// launcher screens underneath (a hidden library/game page could act on
// A/B/D-pad the player cannot see). View+Menu toggles the guide; with it
// open, D-pad moves the selection, A activates and B closes; every other
// edge is swallowed.
//
// The r4559524001 bug: opening the guide reset the focus to Home row 0
// (Resume), and A on a dead guest "resumed" nothing — it just closed the
// guide (or worse, acted on a hidden screen). Force quit was unreachable.
// While dead the guide therefore OPENS with the focus on Force quit (row 2):
// a single A press kills the guest, the worst-case walk is one D-pad Down
// to reach Resume/Quit. While alive the old behaviour is kept exactly
// (focus row 0 on open).
//
// Pure and total over its inputs so tests walk every dead-state sequence
// (tests/guest-watchdog.cpp asserts ForceQuit is reachable within 3 presses
// from every reachable state). The caller applies `open`/`row` to the
// overlay atomics and maps `action`: forceQuit/quitGame to the menu
// commands, resume to closing the guide only while alive (a dead guest has
// nothing to resume — the guide stays open).
enum class GuideAction : uint8_t { none = 0, toggle, up, down, forceQuit, quitGame, resume, close };
struct GuideStep {
    GuideAction action = GuideAction::none;   // what the caller must do
    bool open = false;                        // guide visibility after the step
    int row = 0;                              // focused Home row after the step (0 Resume, 1 Quit, 2 Force quit)
};
constexpr GuideStep guide_step(bool dead, bool open, int row,
                               bool comboEdge, bool upEdge, bool downEdge,
                               bool aEdge, bool bEdge) {
    if (comboEdge) {                          // toggle wins over every other edge
        const bool nowOpen = !open;
        return {GuideAction::toggle, nowOpen, nowOpen && dead ? kForceQuitRow : 0};
    }
    if (!open) return {GuideAction::none, false, row};   // guide closed: everything swallowed
    if (bEdge) return {GuideAction::close, false, row};
    if (aEdge) {
        // A activates the focused row. While dead, `resume` must not act
        // (there is no resuming a dead guest): the caller keeps the guide
        // open, so the player can still move to Force quit.
        if (row == kForceQuitRow) return {GuideAction::forceQuit, open, row};
        if (row == 1) return {GuideAction::quitGame, open, row};
        return {dead ? GuideAction::resume : GuideAction::close, open, row};
    }
    if (upEdge) return {GuideAction::up, open, (row + kHomeRows - 1) % kHomeRows};
    if (downEdge) return {GuideAction::down, open, (row + 1) % kHomeRows};
    return {GuideAction::none, open, row};
}

} // namespace guest_watchdog
