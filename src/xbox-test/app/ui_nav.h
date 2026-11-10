// SPDX-License-Identifier: GPL-3.0-or-later
// Library navigation for the Xbox-style launcher screens (ui_launcher.h):
// sidebar <-> grid focus, sidebar view switching, sort cycling and the queue
// row model. Pure and Win32-free, so tests/ui-nav.cpp covers every rule and
// tests/ui-launcher.cpp keeps rendering the same screens.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ui_nav {

// ---- sidebar <-> grid focus (library screen) ----
// D-pad left from the first grid column moves focus to the sidebar; right
// from the sidebar returns to the grid. Up/down inside the sidebar moves the
// active entry; A on an entry switches the view.
constexpr int kSidebarEntries = 4;   // All games, Installed, Download queue, Storage management

enum class Focus { grid, sidebar };

// Horizontal wrap between the sidebar and the grid. col is the focused tile's
// column (sel % cols); only column 0 borders the sidebar. Returns true when
// something moved.
inline bool lateral(Focus& focus, int col, bool left) {
    if (left) {
        if (focus == Focus::sidebar) return false;
        if (col == 0) { focus = Focus::sidebar; return true; }
        return false;
    }
    if (focus != Focus::sidebar) return false;
    focus = Focus::grid;
    return true;
}

// Vertical movement inside the sidebar; wraps. Returns true when it moved.
inline bool sidebar_move(int& active, bool down) {
    active = down ? (active + 1) % kSidebarEntries
                  : (active + kSidebarEntries - 1) % kSidebarEntries;
    return true;
}

// Which screen the sidebar's active entry opens: 0/1 library, 2 download
// queue, 3 storage management. The library screen itself shows the sidebar;
// the other two draw their own page and B returns to the library.
inline int sidebar_target(int active) { return active; }

// ---- sort mode cycling ----
// The sidebar's sort row cycles the launcher's existing modes; the label is
// SORT_NAMES[mode] ("Most played", "Recently played", "Alphabetical").
inline int cycle_sort(int mode, int n) { return n <= 0 ? 0 : (mode + 1) % n; }

// ---- queue row model (download queue screen) ----
// One row per queued or finished download; the row the queue thread is
// working on is busy and carries the progress percentage. Built by main.cpp
// from its g_dq / g_dl state; the status strings here are the queue screen's
// copy for each download state (steamdl.h DlState: 1 running, 2 done,
// 3 failed, 4 cancelled).
inline const char* dl_status_text(int state, int pct, const char* msg) {
    switch (state) {
        case 1: return "Downloading";
        case 2: return "Installed";
        case 3: return msg && *msg ? msg : "Failed";
        case 4: return "Cancelled";
        default: return pct > 0 ? "Queued" : "Waiting";
    }
}

// ---- game screen focus (icon buttons replace the old X/Y shortcuts) ----
// Icon ids: 0 primary button, 1 trash, 2 cloud, 3 cog. A focus slot numbers
// the buttons actually present on the row, left to right with no gaps: an
// installed page has four slots (0 primary, 1 trash, 2 cloud, 3 cog), a
// not-installed page three (0 primary, 1 cloud, 2 cog — no trash). Right
// walks the slots in that order, left walks back; main.cpp clamps at both
// ends. The ring drawing (ui_launcher.h) and main.cpp's A handler both go
// through these maps, so a ring can never sit on a button A does not
// activate.
inline int focus_count(bool installed) { return installed ? 4 : 3; }
// The focus slot of the i-th icon button present (i 0-based among the icons
// drawn left to right; the icons always start right of the primary button).
inline int icon_focus_slot(bool installed, int i) {
    (void)installed;
    return i + 1;
}
// The icon id (0 primary, 1 trash, 2 cloud, 3 cog) a focus slot lands on.
inline int icon_for_slot(bool installed, int slot) {
    if (slot <= 0) return 0;                 // the primary button
    if (installed) return slot < 4 ? slot : 0;
    return slot < 3 ? slot + 1 : 0;          // no trash: cloud/cog carry ids 2/3
}

// ---- logout reachability ----
// The old View filter chip is gone (Installed is a sidebar entry now); the
// logout confirm sheet opens from the library screen with B when the grid
// has focus (B never stops a download and never cancels an install), and the
// sheet itself is drawn by main.cpp with the restyled confirm copy. A
// confirms, B cancels.
inline bool logout_request(bool confirm_open, Focus focus, int sel, int sidebar_active, bool b_pressed) {
    (void)sel; (void)sidebar_active;
    if (confirm_open) return false;
    return focus == Focus::grid && b_pressed;
}

}  // namespace ui_nav
