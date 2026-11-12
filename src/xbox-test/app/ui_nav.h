// SPDX-License-Identifier: GPL-3.0-or-later
// Library navigation for the Xbox-style launcher screens (ui_launcher.h):
// sidebar <-> grid focus, sidebar view switching, sort cycling and the queue
// row model. Pure and Win32-free, so tests/ui-nav.cpp covers every rule and
// tests/ui-launcher.cpp keeps rendering the same screens.
#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
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
// Installed, the DOWNLOADS block and the DISK widget (item 31: not a list row
// — the bottom disk-information widget, selectable; A opens the Storage
// screen and it wears the active look while Storage is shown). main.cpp maps
// the indexes through sidebar_target; index kSidebarSearch opens the search
// overlay.
// Item D11: entry 4 is the Downloads block above the disk widget, not a
// 'Download queue' list row — it shows the running download (name, progress,
// speed, queued count) or 'No active downloads', and A opens the queue screen.
constexpr int kSidebarEntries = 6;   // Profile, Search, All games, Installed, Downloads block, disk widget
constexpr int kSidebarProfile = 0;   // the profile row (A opens the logout confirm sheet)
constexpr int kSidebarSearch = 1;    // the Search row's index (opens the search overlay)
constexpr int kSidebarQueue = 4;     // the Downloads block (the queue screen's active entry)
constexpr int kSidebarStorage = 5;   // the disk widget (item 31: reached by Down from the Downloads block; A opens Storage)

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
// library (All games / Installed), 4 download queue, 5 the Storage screen via
// the disk widget (item 31). Down from the queue row (4) reaches the widget
// (5); Up from it returns to the queue row. The library screen itself shows
// the sidebar; the other two draw their own page and B returns to the library.
inline int sidebar_target(int active) { return active; }

// ---- sort modes (item 25) ----
// The sort popup's option order AND the model mode indexes are the same
// numbers: 0 Alphabetical (the startup default), 1 Most played, 2 Recently
// played. main.cpp's SORT_NAMES and sort_games() key off these. Favorites is
// a FILTER only: sorting by a boolean said nothing about the rest of the
// library, so it is not a sort option.
constexpr int kSortAlphabetical = 0;
constexpr int kSortMostPlayed = 1;
constexpr int kSortRecentlyPlayed = 2;
constexpr int kSortOptionCount = 3;

// ---- account list popup (item D14) ----
// Rows: the console's Xbox users, the Steam account, the Steam ACTION row,
// and Cancel last (focused on open, so a stray A never logs out). The account
// rows are informational: A on one does nothing, B/Cancel closes the popup.
// Item D16: the action row is 'Log out of Steam' while an account is attached
// and 'Sign in to Steam' when there is none, so a signed-out player starts the
// QR flow from this popup. The row count is the same either way.
inline int accounts_open_sel(int accounts) { return accounts + 1; }   // the Cancel row
inline bool accounts_move(int& sel, int accounts, bool down) {
    const int n = (accounts < 0 ? 0 : accounts) + 2;   // the action row + Cancel
    if (n <= 0) return false;
    sel = down ? (sel + 1) % n : (sel + n - 1) % n;
    return true;
}
// A on row `sel` (n = the account-row count): Cancel is row n+1, the Steam
// action row is row n. main.cpp turns `steam` into log-out or sign-in from the
// launcher's own signed-in state.
enum class AccountAction { cancel, steam, none };
inline AccountAction accounts_activate(int sel, int accounts) {
    if (sel == accounts + 1) return AccountAction::cancel;
    if (sel == accounts) return AccountAction::steam;
    return AccountAction::none;
}
// Item D16: the account list's row count. `xbox` console users come first,
// then the Steam row (the account when signed in, else the sign-in action).
// The count is the same signed in or out, so the popup never changes shape.
inline int account_rows(int xbox_users) { return (xbox_users < 0 ? 0 : xbox_users) + 1; }
// The action row's label: sign out while an account is attached, offer sign-in
// when there is none. The row count never changes, only its label.
inline const char* account_steam_label(bool signed_in) { return signed_in ? "Log out of Steam" : "Sign in to Steam"; }
// The sidebar profile row's status line (item D16): the background session
// restore reports itself here, never on a blocking splash.
//   connecting: "Connecting to Steam..."   (restore in flight)
//   offline:    "Steam offline"            (restore failed, retryable)
//   signed_out: "Sign in required"         (no saved session at all)
//   ready:      ""                         (the persona row speaks for itself)
enum class SessionState { connecting, offline, signed_out, ready };
inline const char* session_hint(SessionState s) {
    switch (s) {
    case SessionState::connecting: return "Connecting to Steam...";
    case SessionState::offline: return "Steam offline";
    case SessionState::signed_out: return "Sign in required";
    default: return "";
    }
}
// Item D16: the state the sidebar hint reports, from the two session flags
// main.cpp keeps (restore: 0/1 pending, 2 done, 3 failed, 4 no saved session;
// signed in). A failed restore is 'offline' (retryable); having no session at
// all - never saved, or logged out - is 'signed_out'.
inline SessionState session_state(bool signed_in, int restore) {
    if (signed_in) return SessionState::ready;
    if (restore == 1 || restore == 0) return SessionState::connecting;
    return restore == 3 ? SessionState::offline : SessionState::signed_out;
}

// ---- Downloads block (item D11) ----
// The sidebar's bottom block replaces the old 'Download queue' list row. Its
// queued count is a short label, empty when nothing waits: '3 queued'.
inline std::string downloads_queued_text(int queued) {
    return queued > 0 ? std::to_string(queued) + " queued" : std::string();
}
// The block's title line: the running game's name, or nothing (idle).
inline const char* downloads_title(const char* running) { return running && *running ? running : "Downloads"; }

// ---- sort mode cycling ----
// The sidebar's sort row cycles the launcher's existing modes; the label is
// SORT_NAMES[mode] ("Alphabetical", "Most played", "Recently played").
inline int cycle_sort(int mode, int n) { return n <= 0 ? 0 : (mode + 1) % n; }

// ---- game options popup (item D10) ----
// ONE sheet, opened by the detail page's cog, by Menu on the detail page and
// by Menu on the library's focused tile. Rows walk with wrap and disabled
// rows stay focusable (the same rule as the storage sheet), so the D-pad can
// always pass a refusal to reach the rows below it. The sheet OPENS on its
// last row, which is always Cancel: a stray A never lands on Delete or Move.
inline int options_open_sel(int n) { return n <= 0 ? 0 : n - 1; }
inline bool options_move(int& sel, int n, bool down) {
    if (n <= 0) return false;
    sel = down ? (sel + 1) % n : (sel + n - 1) % n;
    return true;
}
// A row is refused (drawn muted with its reason) when it carries a reason.
inline bool options_refused(const char* reason) { return reason && *reason; }

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

// ---- filters state (item 32) ----------------------------------------------
// The one real filter today is 'Playable' (the old 'Installed only' toggle
// became it: same row, same toggle behaviour; it now filters WITHIN the
// current tab instead of switching views). filters_active counts the enabled
// filters so the Filters button can show 'Filters · N'; with more filters
// later, extend the caller's bool list, not this helper.
inline int filters_active(const bool* on, int n) {
    int count = 0;
    for (int i = 0; i < n; ++i) if (on && on[i]) ++count;
    return count;
}

// The filter model (item 32): tab_games applies the Playable filter WITHIN
// the current tab. `all` is the ALREADY tab-filtered list (All games or
// Installed — the caller keeps the tab's own installed-set rule), so this
// only drops the non-playable games when the filter is on. Order is kept
// (the caller's sort runs after). Works with pointers, smart pointers and
// value types.
template <class T>
inline const T& deref_game(const T& v) { return v; }
template <class T>
inline const T& deref_game(const T* p) { return *p; }
template <class T>
inline const T& deref_game(const std::shared_ptr<T>& p) { return *p; }
template <class T>
inline const T& deref_game(const std::unique_ptr<T>& p) { return *p; }
template <class Ptr, class PlayableFn>
inline std::vector<Ptr> tab_games(const std::vector<Ptr>& all, bool playable_on, PlayableFn is_playable) {
    std::vector<Ptr> out;
    for (auto& g : all) {
        const auto& gv = deref_game(g);   // pointers, smart pointers and value types alike
        if (playable_on && !is_playable(gv.appid)) continue;
        out.push_back(g);
    }
    return out;
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

// ---- queue focus walk (item 28) ----
// The queue screen's rows: 0 is the Auto-update row (a real focusable row —
// A toggles it), then the active download (if any), then the queue. The row
// count is 1 + downloads + updates; main.cpp passes the same count to the
// clamp/move helpers and indexes g_dq with queue_row_index. Clamped, so a
// drained queue keeps the focus on Auto-update.
inline int queue_row_total(int downloads, int updates) { return 1 + downloads + updates; }
// The queue index a focused row maps to: row 0 is Auto-update (no queue
// entry), row 1 with an active download is the download itself; the rest
// index the queued jobs in order. -1 = no queued job on this row.
inline int queue_row_index(int sel, bool active_row) {
    if (sel <= 0 || (active_row && sel == 1)) return -1;
    return sel - 1 - (active_row ? 1 : 0);         // index into the queued list
}
inline int queue_row_clamp(int sel, int n) { return n <= 0 ? 0 : sel < 0 ? 0 : sel >= n ? n - 1 : sel; }
inline bool queue_row_move(int& sel, int n, bool down) {
    if (n <= 0) return false;
    sel = down ? (sel + 1) % n : (sel + n - 1) % n;
    return true;
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
//
// Item D13: the query is a real text field — the caret sits between two
// characters, X deletes the character BEFORE it, LB/RB move it, Y applies
// (closes the keyboard, keeping the query), L3 toggles caps lock and A types
// the focused key. The state lives here so the rules are unit-tested.
constexpr int kSearchKeyCount = 39;
constexpr int kSearchCols = 10;

struct SearchState {
    std::string query;
    int caret = 0;      // codepoint index in [0, count(query)]
    bool caps = false;  // L3: letters type upper-case
};

// Codepoints in a UTF-8 string (the caret counts characters, not bytes).
inline int search_count(const std::string& s) {
    int n = 0; const char* p = s.data(); const char* end = p + s.size();
    while (p < end) { uint32_t cp; ui::utf8_next(p, end, cp); ++n; }
    return n;
}
// Byte offset of codepoint `index` (clamped); index == count -> s.size().
inline std::size_t search_offset(const std::string& s, int index) {
    const char* p = s.data(); const char* end = p + s.size();
    int i = 0;
    while (p < end && i < index) { uint32_t cp; ui::utf8_next(p, end, cp); ++i; }
    return std::size_t(p - s.data());
}
// Keeps the caret inside [0, count] after the query changes from elsewhere.
inline void search_clamp_caret(SearchState& st) {
    const int n = search_count(st.query);
    if (st.caret < 0) st.caret = 0;
    if (st.caret > n) st.caret = n;
}
// A types `c` at the caret (letters honour caps lock); the caret follows it.
inline bool search_insert(SearchState& st, char c) {
    if (c >= 'a' && c <= 'z' && st.caps) c = char(c - 'a' + 'A');
    const std::size_t at = search_offset(st.query, st.caret);
    st.query.insert(at, 1, c);
    ++st.caret;
    return true;
}
// X: delete the character before the caret. False at the start (nothing to do).
inline bool search_backspace(SearchState& st) {
    if (st.caret <= 0 || st.query.empty()) return false;
    const std::size_t at = search_offset(st.query, st.caret);
    const std::size_t prev = search_offset(st.query, st.caret - 1);
    st.query.erase(prev, at - prev);
    --st.caret;
    return true;
}
// LB/RB: move the caret one character; clamps at both ends.
inline bool search_caret_move(SearchState& st, int dx) {
    const int n = search_count(st.query);
    int next = st.caret + dx;
    if (next < 0) next = 0;
    if (next > n) next = n;
    if (next == st.caret) return false;
    st.caret = next;
    return true;
}
inline bool search_toggle_caps(SearchState& st) { st.caps = !st.caps; return st.caps; }
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
// A on a key: types at the caret / deletes before it / closes. Returns true
// when the query changed (caller re-filters the library). Item D13: the
// Backspace key and the X button do the same thing, and Done keeps the query.
inline bool search_activate(int idx, SearchState& st, bool& open) {
    if (idx < 0 || idx >= kSearchKeyCount) return false;
    if (idx == 38) { open = false; return false; }            // Done
    if (idx == 37) return search_backspace(st);               // Backspace
    if (idx == 36) return search_insert(st, ' ');
    return search_insert(st, idx < 26 ? char('a' + idx) : char('0' + (idx - 26)));   // 0..25 A-Z, 26..35 0-9
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

// Item D5: B at the library root. The root has nowhere to go back to, so B
// moves the focus to the sidebar (the app's left-hand rail) instead of doing
// nothing — a visible, non-destructive answer that keeps the hint bar honest
// ("B Back" from the grid lands on the rail; B on the rail does nothing).
// Returns true when the focus moved.
inline bool back_at_root(Focus& focus, bool b_pressed) {
    if (!b_pressed || focus == Focus::sidebar) return false;
    focus = Focus::sidebar;
    return true;
}

// Item D24: B anywhere on the main screen lands on the sidebar rail. The grid
// root already did (item D5); the header controls row now does too, instead of
// dropping the focus back into the grid.
inline bool header_back(int& hdr_focus, Focus& focus) {
    if (hdr_focus < 0) return false;
    hdr_focus = -1;
    focus = Focus::sidebar;
    return true;
}

// ---- launch banner (item D22) ----
// Play shows a fullscreen banner (the game's wide art + "Launching...") until
// the guest's own window is up. B cancels, but only while the launch is still
// preparing: once game code has started (phase 3) or the launch has ended
// (phase 2) there is nothing left to cancel. g_launch phases: 0 never,
// 1 starting, 2 finished, 3 guest running.
inline bool launch_banner_up(int phase, bool guest_surface) { return phase == 1 && !guest_surface; }
inline bool launch_cancel_allowed(int phase) { return phase == 1; }

}  // namespace ui_nav
