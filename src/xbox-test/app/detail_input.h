// SPDX-License-Identifier: GPL-3.0-or-later
// Detail-page button rules: A installs, plays, or cancels a download on a
// second press; B only goes back to the library and never stops a download.
// Pure and Win32-free, so tests/detail-input.cpp covers every download state.
#pragma once

namespace detail_input {

enum class Download { none, queued, running };
enum class Action { install, play, ask_cancel, cancel, back };

struct State {
    bool installed = false;
    Download download = Download::none;
    bool cancelArmed = false;   // a first A press is waiting for the second
};

// A: a running or queued download is cancelled by pressing A twice; otherwise
// install (not installed) or play (installed).
inline Action press_a(const State& s) {
    if (s.download != Download::none) return s.cancelArmed ? Action::cancel : Action::ask_cancel;
    return s.installed ? Action::play : Action::install;
}

// B never cancels: it goes back to the library and a download keeps running.
inline Action press_b(const State&) { return Action::back; }

// Main button text and the hint under it.
inline const char* label(const State& s) {
    return s.download != Download::none ? "INSTALLING... A: cancel" : s.installed ? "PLAY" : "INSTALL";
}
inline const char* hint(const State& s) {
    if (s.download == Download::none) return "press A";
    return s.cancelArmed ? "press A again to cancel" : "B: back, download keeps running";
}

constexpr double cancel_window = 5.0;   // seconds a first A press waits for the second

}
