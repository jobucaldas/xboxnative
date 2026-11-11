// SPDX-License-Identifier: GPL-3.0-or-later
// Library navigation for the Xbox-style launcher screens (ui_launcher.h):
// sidebar <-> grid focus, sidebar view switching, sort cycling and the queue
// row model. Pure and Win32-free, so tests/ui-nav.cpp covers every rule and
// tests/ui-launcher.cpp keeps rendering the same screens.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include "ui_font.h"   // ui::utf8_next (Backspace drops one codepoint)

namespace ui_nav {

// ---- sidebar <-> grid focus (library screen) ----
// D-pad left from the first grid column moves focus to the sidebar; right
// from the sidebar returns to the grid. Up/down inside the sidebar moves the
// active entry; A on an entry switches the view.
// Item 14: the sidebar is SIX entries — PROFILE (index 0, focusable; A opens
// the logout confirm sheet), Search (1, opens the search overlay), All games,
// Installed, Download queue, Storage management. main.cpp maps the indexes
// through sidebar_target; index kSidebarSearch opens the search overlay.
constexpr int kSidebarEntries = 6;   // Profile, Search, All games, Installed, Download queue, Storage management
constexpr int kSidebarProfile = 0;   // the profile row (A opens the logout confirm sheet)
constexpr int kSidebarSearch = 1;    // the Search row's index (opens the search overlay)
constexpr int kSidebarQueue = 4;     // the Download queue row (the queue screen's active entry)
constexpr int kSidebarStorage = 5;   // the Storage management row (the storage screen's active entry)

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

// Which screen the sidebar's active entry opens: kSidebarProfile (0) opens
// the logout confirm sheet, kSidebarSearch (1) the search overlay, 2/3
// library (All games / Installed), 4 download queue, 5 storage management.
// The library screen itself shows the sidebar; the other two draw their own
// page and B returns to the library.
inline int sidebar_target(int active) { return active; }

// ---- sort modes (item 25) ----
// The sort popup's option order AND the model mode indexes are the same
// numbers: 0 Alphabetical (the startup default), 1 Most played, 2 Recently
// played. main.cpp's SORT_NAMES and sort_games() key off these.
constexpr int kSortAlphabetical = 0;
constexpr int kSortMostPlayed = 1;
constexpr int kSortRecentlyPlayed = 2;
constexpr int kSortOptionCount = 3;

// ---- sort mode cycling ----
// The sidebar's sort row cycles the launcher's existing modes; the label is
// SORT_NAMES[mode] ("Alphabetical", "Most played", "Recently played").
inline int cycle_sort(int mode, int n) { return n <= 0 ? 0 : (mode + 1) % n; }

// ---- storage screen (item 22): drive rows + per-drive game lists ----
// A drive row: the figures the drive list shows. main.cpp fills them from
// storage_roots::Choice (label_capacity / size_text); the pure rules here are
// the focus walk and the list model.
struct StorageDriveRow {
    std::string name;        // "Internal", "E:" ...
    std::string used_line;   // "34% used of 476 GB"
    int used_pct = 0;        // 0..100, -1 unknown
    bool is_target = false;  // the drive new installs go to (the old X-cycle pick)
    uint64_t free_bytes = 0, total_bytes = 0;
};

// Clamp a drive-row focus into range; an empty list keeps 0.
inline int storage_drive_clamp(int sel, int n) { return n <= 0 ? 0 : sel < 0 ? 0 : sel >= n ? n - 1 : sel; }
// D-pad move with wrap; returns true when it moved.
inline bool storage_drive_move(int& sel, int n, bool down) {
    if (n <= 0) return false;
    sel = down ? (sel + 1) % n : (sel + n - 1) % n;
    return true;
}

// One row of a drive's installed-games list.
struct StorageGameRow {
    std::string name;      // library cache name, "App <id>" when unknown
    std::string size_text; // storage_roots::size_text of the install record
    uint32_t appid = 0;
    uint64_t bytes = 0;
};

// The rows of one drive's installed-games list, built from (appid, dir, root,
// bytes, name) records. Games whose marker names another root are skipped (a
// drive only lists what sits on it); the order is the caller's (alphabetical
// upstream). Pure so tests can pin the filtering without Win32.
inline std::vector<StorageGameRow> storage_game_rows(const std::string& drive_root,
                                                     const std::vector<std::tuple<uint32_t, std::string, std::string, uint64_t, std::string>>& installs) {
    std::vector<StorageGameRow> out;
    for (auto& [appid, dir, root, bytes, name] : installs) {
        if (root != drive_root) continue;
        StorageGameRow r;
        r.appid = appid;
        r.name = name.empty() ? "App " + std::to_string(appid) : name;
        r.bytes = bytes;
        out.push_back(std::move(r));
    }
    return out;
}

// ---- filters state (item 26) ----------------------------------------------
// The one real filter today is 'Installed only'. filters_active counts the
// enabled filters so the Filters button can show an accent state; with more
// filters later, extend the caller's bool list, not this helper.
inline int filters_active(const bool* on, int n) {
    int count = 0;
    for (int i = 0; i < n; ++i) if (on && on[i]) ++count;
    return count;
}

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

// ---- library header row: Sort / Filters ------------------------------------
// The library header carries TWO focusable controls (Sort, Filters), numbered
// 0..1 left to right, above the grid — the Search pill is gone (review fix 3:
// the sidebar's Search row is THE search entry). main.cpp owns the state;
// these helpers are the D-pad/A/B rules.
constexpr int kHeaderControlCount = 2;   // 0 sort, 1 filters

// Horizontal focus walk across the header controls; clamps at both ends.
inline int header_move(int focus, bool right) {
    focus += right ? 1 : -1;
    return focus < 0 ? 0 : focus > kHeaderControlCount - 1 ? kHeaderControlCount - 1 : focus;
}

// ---- Sort dropdown (open state) ---------------------------------------------
// main.cpp fills options/selected; focus is the highlighted row. A confirms
// (selected = focus, popup closes), B closes, up/down moves with wrap.
inline bool sort_open_move(int& focus, int n, bool down) {
    if (n <= 0) return false;
    focus = down ? (focus + 1) % n : (focus + n - 1) % n;
    return true;
}
inline bool sort_confirm(int& focus, int& selected, bool& open) {
    if (!open) return false;
    selected = focus;
    open = false;
    return true;
}
inline bool sort_close(bool& open) { const bool was = open; open = false; return was; }

// ---- Filters panel ----------------------------------------------------------
// Rows of {label, on}; focus walks the rows, A toggles the focused row's bool.
inline bool filters_move(int& focus, int n, bool down) {
    if (n <= 0) return false;
    focus = down ? (focus + 1) % n : (focus + n - 1) % n;
    return true;
}
inline bool filters_toggle(bool* on, int n, int focus) {
    if (!on || focus < 0 || focus >= n) return false;
    on[focus] = !on[focus];
    return true;
}

// ---- popup exclusivity (review fix 2/5) ------------------------------------
// Only one popup may be open at a time: the Sort dropdown, the Filters panel
// and the search overlay are mutually exclusive. which selects the popup to
// open (0 sort, 1 filters, 2 search); the other two flags are closed. Call it
// from main.cpp whenever a control OPENS a popup — the close paths (B, confirm)
// just clear their own flag.
inline void popup_exclusive(bool& sort_open, bool& filters_open, bool& search_open, int which) {
    sort_open = which == 0;
    filters_open = which == 1;
    search_open = which == 2;
}

// ---- Search overlay (on-screen keyboard) ------------------------------------
// One grid of keys: rows of A-Z, then 0-9, Space, Backspace, Done; the D-pad
// moves the cursor with wrap on every axis, A types/activates, B closes.
// Key ids: 0..25 A-Z, 26..35 0-9, 36 space, 37 backspace, 38 done.
constexpr int kSearchKeyCount = 39;
constexpr int kSearchCols = 10;
inline int search_key_at(int row, int col) {
    row = row < 0 ? 0 : row;
    int idx = row * kSearchCols + col;
    return idx < 0 ? 0 : idx > kSearchKeyCount - 1 ? kSearchKeyCount - 1 : idx;
}
// The 4th row is short (9 keys): the cursor never lands past the last key.
inline int search_key_row(int idx) { return idx / kSearchCols; }
inline int search_key_col(int idx) { return idx % kSearchCols; }
// D-pad move with wrap on every axis; dx/dy are -1/0/+1. Horizontal: wraps at
// the row ends (the short last row wraps back to its own start). Vertical:
// wraps top<->bottom and clamps the column into the (possibly short) target
// row. Returns true when the cursor moved.
inline bool search_move(int& idx, int dx, int dy) {
    if (!dx && !dy) return false;
    const int last = kSearchKeyCount - 1;                 // 38
    const int full_rows = last / kSearchCols;             // 3 full rows (0..2)
    const int rows = full_rows + 1;                       // 4 rows, the last short (9 keys)
    int row = search_key_row(idx), col = search_key_col(idx);
    if (dx) {
        const int end = row < full_rows ? kSearchCols - 1 : last - row * kSearchCols;
        col += dx;
        if (col > end) col = 0;                           // wrap past the row end
        if (col < 0) col = end;                           // wrap before the row start
    }
    if (dy) {
        row = (row + (dy > 0 ? 1 : rows - 1)) % rows;
        if (row * kSearchCols > last) row = 0;            // wrapped past the short row
        const int end = row < full_rows ? kSearchCols - 1 : last - row * kSearchCols;
        if (col > end) col = end;
    }
    const int next = row * kSearchCols + col;
    if (next == idx) return false;
    idx = next;
    return true;
}
// A on a key: appends to query / deletes the last character / closes. Returns
// true when the query changed (caller re-filters the library).
inline bool search_activate(int idx, std::string& query, bool& open) {
    if (idx < 0 || idx >= kSearchKeyCount) return false;
    if (idx == 38) { open = false; return false; }            // Done
    if (idx == 37) {                                          // Backspace
        if (query.empty()) return false;
        // drop one UTF-8 codepoint
        std::size_t n = 0; const char* p = query.data(); const char* end = p + query.size();
        const char* last = p; uint32_t cp;
        while (p < end) { last = p; ui::utf8_next(p, end, cp); ++n; }
        (void)n; (void)cp;
        query.resize(size_t(last - query.data()));
        return true;
    }
    if (idx == 36) { query += ' '; return true; }
    query += char('A' + idx);
    return true;
}

// ---- logout reachability ----
// Item 14: B on the library root does NOTHING (Xbox convention; it used to
// open the logout sheet with focus on the destructive button). Logout lives
// on the sidebar's PROFILE row: up from Search reaches it, A opens the
// confirm sheet (main.cpp). This helper stays for the sheet-open guard so a
// B press can never open it; it always returns false now.
inline bool logout_request(bool confirm_open, Focus focus, int sel, int sidebar_active, bool b_pressed) {
    (void)confirm_open; (void)focus; (void)sel; (void)sidebar_active; (void)b_pressed;
    return false;
}

}  // namespace ui_nav
