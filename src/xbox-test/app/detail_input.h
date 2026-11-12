// SPDX-License-Identifier: GPL-3.0-or-later
// Detail-page button rules: A installs, plays, or cancels a download with one
// press; B only goes back to the library and never stops a download, and the
// d-pad only scrolls. A finished download never launches anything.
// Pure and Win32-free, so tests/detail-input.cpp and
// tests/detail-after-download.cpp cover every download state.
#pragma once
#include <cstdio>
#include <cstring>

namespace detail_input {

enum class Download { none, queued, running };
enum class Action { ignore, install, play, cancel, back };

constexpr double cancel_guard = 1.0;   // A is ignored this soon after the install started

struct State {
    bool installed = false;
    Download download = Download::none;
    double sinceStart = 1e9;   // seconds since this page started the install
};

// A cancels a running or queued download at once; otherwise it installs (not
// installed) or plays (installed). The press that started the install is
// ignored for cancel_guard seconds, so it can never cancel what it started.
inline Action press_a(const State& s) {
    if (s.download != Download::none) return s.sinceStart < cancel_guard ? Action::ignore : Action::cancel;
    return s.installed ? Action::play : Action::install;
}

// B never cancels: it goes back to the library and a download keeps running.
inline Action press_b(const State&) { return Action::back; }

// The d-pad never cancels either: it scrolls the description.
inline Action press_dpad(const State&) { return Action::ignore; }

// Main button text and the hint under it. While installing the single button
// reads INSTALLING n% (mockup 03) and turns into CANCEL when focused (03b);
// the hint under A says "Cancel install" in both. No X/Y shortcut hints.
inline const char* label(const State& s, int pct = 0) {
    if (s.download == Download::none) return s.installed ? "PLAY" : "INSTALL";
    static char buf[16];
    if (std::snprintf(buf, sizeof buf, "INSTALLING %d%%", pct) <= 0) buf[0] = 0;
    return buf;
}
inline const char* hint(const State& s) {
    if (s.download != Download::none) return "Cancel install";
    return s.installed ? "Installed. Press A to play" : "Press A to install";
}

// A finished download never launches anything: the detail page only re-reads
// the install state and shows finished_text(), so the player presses A to play.
enum class AfterDownload { none, refresh };
inline AfterDownload after_download(bool ok, bool installed) {
    return ok && installed ? AfterDownload::refresh : AfterDownload::none;
}
inline const char* finished_text() { return "Installed. Press A to play"; }

}
