// SPDX-License-Identifier: GPL-3.0-or-later
// Play launch-option picker: a game that ships several run options (Hades:
// DirectX / 32-bit / Debug...) must not pick one silently. The rows, their
// order and the selection are pure here so tests/launch-target-test.cpp covers
// them; main.cpp only draws and feeds controller input.
#pragma once
#include <string>
#include <vector>
#include "launch_target.h"

namespace launch_picker {

// A game with more than one runnable Windows option asks; a single option
// (or none) starts directly, as before.
inline bool needed(const std::vector<launch_target::Choice>& rows) { return rows.size() > 1; }

struct State {
    bool open = false;
    std::vector<launch_target::Choice> rows;
    int sel = 0;
    std::string note;   // why the last A press did not start (a 32-bit row)
};

// Open on the remembered option when it is still offered and runnable, else
// the first runnable one (64-bit rows come first).
inline void open(State& s, std::vector<launch_target::Choice> rows, const std::string& remembered) {
    s.rows = std::move(rows);
    s.sel = launch_target::preselect(s.rows, remembered);
    s.note.clear();
    s.open = true;
}
inline void close(State& s) { s.open = false; s.rows.clear(); s.sel = 0; s.note.clear(); }

// D-pad up/down: every row is reachable, including the 32-bit ones (the
// player sees the game's own options); the list wraps.
inline void move(State& s, int delta) {
    if (s.rows.empty()) { s.sel = 0; return; }
    int n = (int)s.rows.size();
    s.sel = ((s.sel + delta) % n + n) % n;
    s.note.clear();
}
inline const launch_target::Choice* current(const State& s) {
    return s.sel >= 0 && s.sel < (int)s.rows.size() ? &s.rows[s.sel] : nullptr;
}

enum class Press { none, start, refused };
// A: start the highlighted option, or explain why it cannot run yet.
inline Press press_a(State& s) {
    if (!s.open) return Press::none;
    const launch_target::Choice* c = current(s);
    if (!c) return Press::refused;
    if (!c->selectable) {
        const char* why = launch_target::arch_note(c->arch);
        s.note = why ? std::string("This option ") + why : std::string("This option cannot be started.");
        return Press::refused;
    }
    return Press::start;
}
// The executable to remember once a row starts ("" when nothing is open).
inline std::string started(const State& s) {
    const launch_target::Choice* c = current(s);
    return c && c->entry ? c->entry->executable : std::string();
}

// ---- remembered choice: LocalState\launch_choice_<appid>.txt next to the
// install marker, holding the option's executable as appinfo spells it.
inline std::string remember_name(uint32_t appid) { return "launch_choice_" + std::to_string(appid) + ".txt"; }
inline std::string format_remember(const std::string& executable) { return executable + "\n"; }
// One line, no control characters; anything else is not a remembered choice.
inline std::string parse_remember(const std::string& text) {
    size_t end = text.find_first_of("\r\n");
    std::string v = text.substr(0, end);
    if (v.empty() || v.size() > 260) return std::string();
    for (unsigned char c : v) if (c < 32 || c == 127) return std::string();
    return v;
}

}
