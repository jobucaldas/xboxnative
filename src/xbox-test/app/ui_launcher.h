// SPDX-License-Identifier: GPL-3.0-or-later
// Xbox-inspired screens as pure functions of view-model structs over the CPU
// toolkit (ui_theme/ui_canvas/ui_font/ui_glyphs). No Win32, no app globals:
// main.cpp fills the structs; tests/ui-launch.cpp renders the same screens
// natively for RMSE comparison against the approved mockups.
//
// Geometry is measured from .mockups/launcher/figcraft/document.json at
// 1920x1080. Mockup text nodes are top-anchored; baseline_of() converts a
// node's top y into a draw baseline the same way for every screen.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "detail_input.h"
#include "ui_canvas.h"
#include "ui_font.h"
#include "ui_glyphs.h"
#include "ui_nav.h"
#include "ui_theme.h"

namespace ui_launcher {

using ui::Align;
using ui::Face;
using ui::Font;
using ui::Rect;
using ui::rect;

// ---- shared geometry (1920x1080) ----
// Library grid: item 4 — MAX 5 columns of 2:3 tiles inside the content area
// (x 524..1844 = 1320px): 5 tiles of 232 + 4 gaps of 40 = 1320. Two rows of
// 348 + a 24px row gap end at y=984 (row 1 at 264..612, row 2 at 636..984);
// rows scroll by one and clip above the hint band (kGridClipY). The focused
// bottom-row tile's ring extends 6px below the tile (to y=990), so the band
// top sits at 996 — the ring stays complete and the bar keeps its place.
constexpr float kHintDiscPx = 26.0f;    // prompt disc diameter in the theme::hint bar
constexpr float kHintY      = 1007.0f;  // prompt disc top row (inside the reserved band)
constexpr float kHintBandTop = 996.0f;  // item 21: reserved bottom band; page body ends here
constexpr float kHintRight  = 1844.0f;  // item 21: the hint bar right-aligns to this edge
constexpr float kTitleX     = 524.0f, kTitleY = 73.5f;   // screen title, top-left
constexpr float kGridX      = 524.0f, kGridY = 264.0f;   // first tile's top-left
constexpr float kTileW      = 232.0f, kTileH = 348.0f;   // 2:3 tiles
constexpr float kTilePitchX = 272.0f, kTilePitchY = 372.0f;
constexpr int   kGridCols   = 5;
constexpr float kGridClipY  = 996.0f;   // grid clips here, at the hint band top
// Item D8: the grid also clips at the TOP, below the header row (Sort/Filters
// at y 168..228). A scrolled row would otherwise paint over the title and the
// header controls; 240 leaves the header's 12px breathing room.
constexpr float kGridClipTop = 240.0f;
constexpr float kSidebarX   = 32.0f, kSidebarY = 32.0f;
constexpr float kSidebarW   = 411.0f, kSidebarH = 1016.0f;

// colors the theme does not name (measured from the mockups)
constexpr uint32_t kAvatarBg  = 0xFF3A4656;
constexpr uint32_t kHeroTop   = 0xFF16233A, kHeroMid = 0xFF2E5C8A, kHeroBot = 0xFF0A1018;
constexpr uint32_t kRingDark  = 0xFF0B1A2E;
constexpr uint32_t kIconLight = 0xFFE4E4E4;

// ---- text helpers: mockup text nodes are top-anchored (y = top of line box) ----
// Figma's line box is centred on the em: first baseline = y_top + (lineHeightPx + ascentPx
// - descentPx) / 2, where the metrics come from the face at the node's font size.
inline float baseline_of(const Font& f, ui::Face face, float px, float y_top) {
    const float lh = f.line_height(face, px);
    return y_top + (lh + f.ascent(face, px) - f.descent(face, px)) * 0.5f;
}
inline void text(ui::Canvas& c, const Font& f, ui::Face face, float px, float x, float y_top,
                 const char* s, uint32_t color, float ls = 0.0f) {
    f.draw(c, face, px, x, baseline_of(f, face, px, y_top), s, std::strlen(s), color, 255, ls);
}
inline void text_c(ui::Canvas& c, const Font& f, ui::Face face, float px, float cx, float y_top,
                   const char* s, uint32_t color, float ls = 0.0f) {
    const float w = f.measure(face, px, s, std::strlen(s), ls);
    text(c, f, face, px, cx - w * 0.5f, y_top, s, color, ls);
}
inline void text_r(ui::Canvas& c, const Font& f, ui::Face face, float px, float right, float y_top,
                   const char* s, uint32_t color, float ls = 0.0f) {
    const float w = f.measure(face, px, s, std::strlen(s), ls);
    text(c, f, face, px, right - w, y_top, s, color, ls);
}

// item 20: a button label centred by its true ink box (whole string, both
// axes). text_c centres by advance width, which lands a pixel or two off
// whenever the first/last side bearings differ; the ink box puts the
// visible glyphs' centre exactly on the button centre.
inline void button_label(ui::Canvas& c, const Font& f, ui::Face face, float px_size,
                         const ui::Rect& button, const char* s, uint32_t color) {
    float bx0, by0, bx1, by1;
    if (f.string_ink_box(face, px_size, s, bx0, by0, bx1, by1)) {
        f.draw(c, face, px_size,
               button.x + button.w * 0.5f - (bx0 + bx1) * 0.5f,
               button.y + button.h * 0.5f - (by0 + by1) * 0.5f,
               s, std::strlen(s), color);
    } else {
        text_c(c, f, face, px_size, button.x + button.w * 0.5f,
               button.y + button.h * 0.5f - px_size * 0.6f, s, color);
    }
}

// ---- pure grid geometry (unit-tested with assert) ----
// The live grid is 5 columns of 2:3 tiles (item 4). The mockup-comparison
// test passes cols=7, which restores the ORIGINAL mockup layout (168x252
// tiles, pitch 192x276) so the RMSE gate stays meaningful.
inline float tile_w(int cols) { return cols <= 5 ? 232.0f : 168.0f; }
inline float tile_h(int cols) { return cols <= 5 ? 348.0f : 252.0f; }
inline float tile_pitch_x(int cols) { return cols <= 5 ? 272.0f : 192.0f; }
inline float tile_pitch_y(int cols) { return cols <= 5 ? 372.0f : 276.0f; }
// cols lets the mockup-comparison test keep the original 7-column layout
// (LibraryVM::cols, default 5 per item 4); pass 7 there.
inline Rect grid_tile(int index, int top_row, int cols = kGridCols) {
    const int row = index / cols, col = index % cols;
    return rect(kGridX + col * tile_pitch_x(cols), kGridY + (row - top_row) * tile_pitch_y(cols),
                tile_w(cols), tile_h(cols));
}
// Rows that can be visible for a scroll offset; one row of slack on each side.
inline void grid_visible(int tile_count, int top_row, int& lo, int& hi, int cols = kGridCols) {
    lo = top_row > 0 ? (top_row - 1) * cols : 0;
    hi = tile_count < (top_row + 3) * cols ? tile_count : (top_row + 3) * cols;
}

// ---- primary button state machine (pure) ----
// One button per state; while installing, the same button reads INSTALLING n%
// and turns into CANCEL when it has focus (A then cancels; never a second button).
enum class Primary { install, play, installing, cancelling, launching, restart_required, deleting };

struct PrimaryVM {
    bool installed = false;
    detail_input::Download download = detail_input::Download::none;
    bool launching = false;
    bool deleting = false;
    bool restart_required = false;
    float install_pct = 0.0f;
};

inline Primary primary_state(const PrimaryVM& vm) {
    if (vm.launching) return Primary::launching;
    if (vm.deleting) return Primary::deleting;
    if (vm.download != detail_input::Download::none) return Primary::installing;
    if (vm.restart_required) return Primary::restart_required;
    return vm.installed ? Primary::play : Primary::install;
}

// The label the single button shows. The installing button reads INSTALLING n%
// (the real percentage: vm.install_pct 0..1, clamped, rounded down) and turns
// into CANCEL when it has focus (A then cancels; never a second button).
inline const char* primary_label(const PrimaryVM& vm, bool focus) {
    static char pct[32];
    switch (primary_state(vm)) {
        case Primary::play:             return "PLAY";
        case Primary::install:          return "INSTALL";
        case Primary::installing:
            if (focus) return "CANCEL";
            { float p = vm.install_pct; p = p < 0.0f ? 0.0f : p > 1.0f ? 1.0f : p;
              std::snprintf(pct, sizeof pct, "INSTALLING %d%%", int(p * 100.0f)); }
            return pct;
        case Primary::cancelling:       return "CANCEL";
        case Primary::launching:        return "LAUNCHING...";
        case Primary::restart_required: return "RESTART REQUIRED";
        case Primary::deleting:         return "DELETING...";
    }
    return "";
}

// the hint label under the A disc follows the same state
inline const char* primary_hint(const PrimaryVM& vm, bool /*focus*/) {
    switch (primary_state(vm)) {
        case Primary::play:             return "Play";
        case Primary::install:          return "Install";
        case Primary::installing:       return "Cancel install";   // mockups 03 and 03b both say it
        default:                        return "Select";
    }
}

// ---- view models ----

struct SidebarVM {
    const char* persona = "Player One";
    int all_count = 0, installed_count = 0, queue_count = 0;
    int free_gb_x10 = 4126, used_pct = 34;
    // item 31: the view rows are ONLY All games / Installed / Download queue
    // (indexes 2, 3, 4) — 'Storage management' is NOT a list row any more. The
    // bottom DISK widget (free GB / % used / donut) is the sidebar entry with
    // index 5 (profile 0, search 1). Focus still arrives through `focus`:
    // main.cpp keeps driving it with sidebar_focus_row(), which returns 5 when
    // the disk widget has the pad focus; `disk_focus`/`disk_active` below carry
    // the same states for the widget's own drawing.
    int active = 0;   // 2 All games, 3 Installed, 4 Download queue (view rows only)
    int focus = -1;   // row with the pad focus (pill fill); 5 = the disk widget;
                      // -1 = the sidebar is not focused (mockups: only the
                      // active row is highlighted)
    // item 3: the sidebar's Search row is the search entry — the live query
    // shows in the field; empty = the "Search" placeholder.
    const char* search_query = "";
    // item 14: the PROFILE row is focusable (logout affordance); main.cpp
    // derives it from (m_focus, m_sidebar) at draw time.
    bool profile_focus = false;
    // item 31: the disk widget's own focus/active states (see `focus` above —
    // focus == 5 means the same thing as disk_focus; main.cpp may drive either).
    bool disk_focus = false;
    bool disk_active = false;   // true while the Storage screen is shown
    // item 24 (drawing side): the signed-in user's avatar, circle-masked in
    // the sidebar's profile row; empty falls back to the placeholder circle.
    ui::Image avatar{};
};

struct TileVM {
    const char* name = "";
    uint32_t art_top = 0xFF1B2A4A, art_mid = 0xFF3E6FA8, art_bot = 0xFF0B1220;
    bool installed = false;
    bool favorite = false;   // item D10: a star badge in the tile's top-right corner
    bool no_cover = false;   // item 12: grey gradient tile, not the motif
    ui::Image art{};   // real cover art (main.cpp); empty falls back to the grey gradient
    float name_w = 0.0f;   // measured label width (font-dependent); 0 = fits, no marquee
};

struct LibraryVM {
    const TileVM* tiles = nullptr;
    int tile_count = 0;
    int sel = 0;             // focused tile index
    int top_row = 0;         // first visible grid row
    bool focus_tile = true;
    int cols = kGridCols;    // mockup-comparison test passes 7 (see grid_tile)
    const char* sort_label = "Sort by recently played";
    const char* count_label = "";   // e.g. "128 games"; empty hides it
    bool loading = false;    // true: count label reads "Loading..."
    // header selectors (item 1): real controls in the row above the grid
    int header_focus = -1;   // 0 sort, 1 filters, 2 search; -1 grid/sidebar
    bool sort_open = false;  // dropdown is up
    int sort_focus = 0;      // highlighted option while open
    bool filters_open = false;
    int filters_focus = 0;
    bool filter_playable = false;   // the Playable row's real state (main.cpp sets it from m_filterPlayable)
    bool filter_favorites = false;  // item D10: the Favorites row's state
    bool search_open = false;    // on-screen keyboard overlay is up
    int search_key = 0;          // keyboard cursor (ui_nav::search_key ids)
    const char* query = "";      // live query text; filters the caller's tiles
    int search_caret = 0;        // item D13: caret position in the query (codepoints)
    bool search_caps = false;    // item D13: caps lock (L3), keys show upper case
    int filters_active = 0;      // item 26: enabled filter count (label + accent)
    float t = 0.0f;              // seconds (item 11): drives the marquee scroll
};

struct QueueRowVM {
    const char* name = "";
    const char* status = "";
    float pct = 0.0f;
    uint32_t art = 0xFF1A3F6B;
    bool busy = false;       // progress + status ring (currently downloading)
    bool paused = false;     // paused row: button reads RESUME
};

struct QueueVM {
    bool auto_update = true;
    const char* update_note = "Keep installed games up to date automatically";
    const QueueRowVM* rows = nullptr;       int row_count = 0;     // Queue section
    const QueueRowVM* updates = nullptr;    int update_count = 0; // Updates section
    int focus = -1;          // 0 auto-update pill, 1..n queue rows (ring drawn)
};

// detail page: one layout for every state; fields shared, placeholders when unknown.
// Item 13: two image slots — hero (the wide library_hero.jpg, cover-fit across
// the 1920x520 region under the veil) and capsule (the portrait
// library_600x900 on the right, same art the tile uses). Grey-gradient
// fallbacks (item 12) while loading or missing; main.cpp fetches both.
struct GameVM {
    const char* title = "Megabonk";
    const char* subtitle = "Single-player  \xC2\xB7  Action, Roguelite  \xC2\xB7  48.2 GB";
    const char* time_played = "Not played yet";
    const char* achievements = "0 / 42";
    const char* last_played = "Never";
    const char* install_status = "Not installed";
    bool install_ok = false;             // status value draws in accentSoft when the game is in
    bool favorite = false;               // item D10: a star beside the page title
    // Item D9: ONE size row. Installed: the size on disk. Not installed: the
    // store's download figure, or "Calculating..." while it loads, or "Unknown"
    // when the store lists none. ("Size on disk" was a duplicate of the
    // Status row's figure and is gone.)
    const char* game_size = "Calculating...";
    const char* location = "Internal storage";
    const char* last_updated = "Never";
    const char* cloud_saves = "In the cloud";
    const char* about =
        "Fast, chaotic 3D roguelite. Pick a character, survive the waves, and build an absurd loadout.";
    const char* players = "1";
    const char* language = "English";
    const char* achievement = "";        // latest achievement; empty hides the row
    const char* achievement_sub = "";
    float progress_pct = 0.0f;           // 0..1 achievement progress bar
    PrimaryVM primary;
    int focus = -1;          // icon id the ring draws: -1 none, 0 primary button,
                             // 1 trash, 2 cloud, 3 cog (ui_nav::icon_for_slot maps
                             // the focus slot to it; main.cpp owns the slot walk)
    bool primary_focus = false;
    float install_pct = 0.0f;   // 0..1 while installing
    bool has_art = false;
    bool no_cover = false;   // item 12: grey-gradient fallbacks, not the wash
    ui::Image art{};   // capsule art (library_600x900; main.cpp, header.jpg fallback)
    ui::Image hero{};  // wide hero banner (library_hero.jpg; main.cpp, item 13)
    bool has_hero = false;
    uint32_t art_top = 0xFF1A3F6B, art_mid = 0xFF14304F, art_bottom = 0xFF0A0E14;
};

// logout confirm (item 8): the SAME sheet component as Delete — title, body,
// two buttons with focus — over the dimmed page. Item 14: focus_yes defaults
// FALSE — the ring starts on Cancel, never on the destructive button.
struct LogoutVM {
    const char* title = "Log out of Steam?";
    const char* body = "This deletes the saved login on this console only. "
                       "You can sign in again with the QR code.";
    const char* note = "";
    bool focus_yes = false;
};

// sheets over the dimmed game page (mockups 05 / 07). Item 14: focus_delete
// defaults FALSE — the ring starts on Cancel, never on the destructive button.
struct DeleteVM {
    const char* title = "Delete Megabonk?";
    const char* body = "Frees 48.2 GB on this console. Saves are kept.";
    const char* note = "You can install it again from your library at any time.";
    bool focus_delete = false;
};

struct CloudSlotVM { const char* name = ""; const char* detail = ""; };
struct CloudOptVM { const char* label = ""; const char* value = ""; bool enabled = true; };
struct CloudVM {
    const char* title = "Cloud saves";
    const char* synced = "Never synced";
    const CloudSlotVM* slots = nullptr; int slot_count = 0;
    const CloudOptVM* opts = nullptr;   int opt_count = 0;
    int focus_opt = 0;
};

// item 33: ACTION SHEET from a storage drive's game list — title = game name,
// subtitle = size + drive; full-width vertical option buttons, one per entry.
// `reasons[i]` empty = the option is enabled; otherwise the option draws dim
// with its reason line under the label (and main.cpp must refuse to run it).
// `sel` is the focused option (ring); default focus lands on Cancel.
struct ActionSheetVM {
    const char* title = "";
    const char* subtitle = "";
    const char* const* labels = nullptr;   // n option labels
    const char* const* reasons = nullptr;  // per option; empty string = enabled,
                                           // else disabled with this reason line
    int n = 0;
    int sel = 0;
};

// item 33: PROGRESS SHEET for a background move — title (game name), detail
// ('Moving to <drive>'), a 12px rounded progress bar with accent fill, the
// percentage right of the bar, and a focused Cancel button.
struct ProgressSheetVM {
    const char* title = "";
    const char* detail = "";
    int pct = 0;              // 0..100
    bool cancel_focus = true; // the Cancel button wears the ring (default)
};

// sign-in (mockup 04): 29x29 QR module runs at 10px, origin (467,415)
struct QrRun { uint8_t row, col, w; };
struct SignVM {
    const char* title = "Sign in to Steam";
    const char* status = "Waiting for approval";
    const char* timer = "The code refreshes automatically";
    const char* persona = "Not signed in";
    const char* avatar_letter = "";
    const char* clock = "";
    const QrRun* qr = nullptr; int qr_count = 0;
    bool refresh_focused = true;
};

// ---- storage management (item 22) -------------------------------------------
// Drive rows as selectable cards; open_drive >= 0 shows that drive's games.
struct StorageDriveVM {
    const char* title = "";        // "Internal", "E:" ...
    const char* detail = "";       // e.g. "49% used of 30.0 GB"
    int used_pct = 0;              // 0..100 — drives the usage bar under the detail
    bool install_target = false;   // the drive new installs go to
    bool focused = false;          // the pad's row (ring)
};

struct StorageGameVM {
    const char* name = "";
    const char* size = "";
    bool focused = false;
};

struct StorageVM {
    const StorageDriveVM* drives = nullptr; int n_drives = 0;
    const StorageGameVM* games = nullptr;   int n_games = 0;
    int open_drive = -1;           // >= 0: the drive list is open at this index
    const char* drive_title = "";  // the open drive's name (page title when open)
    int game_scroll = 0;           // the open drive's list: first visible row
};

// ---- drawing ----------------------------------------------------------------
//
// PUBLIC API for main.cpp wiring (items 1, 3, 4, 5, 7, 8, 9, 11, 12, 13) — the
// later main.cpp change fills these structs and calls the draw functions; every
// draw_* paints the FULL 1920x1080 page first (item 3: no last-screen remnants).
//
// Library (draw_library(c, f, sb, lib)):
//   LibraryVM gains: cols (default 5; the mockup test passes 7),
//   header_focus (-1 grid/sidebar, 0 sort, 1 filters — the header Search pill
//   is GONE, review fix 3: the sidebar's Search row is the search entry),
//   sort_open/sort_focus (dropdown VM), filters_open/filters_focus,
//   search_open/search_key/query (on-screen keyboard overlay), t (seconds,
//   drives the focused tile's marquee). Tiles carry name_w (measured label
//   width; 0 = fits). Sort options live in kSortOptions (3); the Filters panel
//   rows come from the caller (draw_filters_panel).
//   Grid (review fix 1): 5 columns of 232x348, pitch x 272 (40px gaps),
//   left-aligned at x=524 like the title; 5x232+4x40 = 1320 of the 1320
//   content width, rows pitch 372 (24px row gaps), two rows end at 984.
//   Popups (review fix 2): draw_sort_popup — ONLY the sort options, left-
//   aligned under the Sort control (x=524, w=430); draw_filters_panel — the
//   toggle rows under the Filters button, right edge at 1844; only one popup
//   open at a time — open them through ui_nav::popup_exclusive.
//   Sidebar (review fix 3): SidebarVM.search_query shows the active query in
//   the focusable Search row (ui_nav::kSidebarSearch = 0; nav order Search,
//   All games, Installed, Download queue, Storage management over
//   ui_nav::kSidebarEntries = 5).
//   Search overlay (review fix 4): keys are text-labelled ("Space", "Delete",
//   "Done" — no glyph needed); the sheet shrinks to its content; the focused
//   key is accent-highlighted; the hint bar sits at x=524 like every screen.
//   Hints (review fix 5): every library-family screen and popup draws its hint
//   bar ONCE, at the same x (524) and y (kHintY) — the sidebar is never
//   overlapped.
//   New: draw_sort_popup(c, f, lib) — dropdown under the sort control;
//        draw_filters_panel(c, f, rows, n, focus) — toggle rows popup;
//        draw_search_overlay(c, f, lib) — centred keyboard sheet.
//   Hints: kLibraryHints = A Select, B Back, M Menu (X/Y removed, item 1).
// Game page (draw_game): GameVM gains hero (ui::Image) + has_hero (item 13:
//   wide library_hero.jpg cover-fit across the 1920x520 region under the
//   existing veil; grey-gradient fallback = item 12's wash). art stays the
//   tall capsule (library_600x900) drawn in the right-hand box. Cloud values
//   should arrive pre-shortened via cloud_saves_short() ("Synced 10 Nov").
// Confirmation sheet (item 8): draw_confirm_sheet(c, f, title, body, note,
//   yes_label, no_label, focus_yes) — shared by draw_delete_sheet (DeleteVM,
//   unchanged) and draw_logout_sheet(c, f, LogoutVM). Both over draw_scrim.
// Sidebar (item 9): an empty persona draws "Signed in".

// The mockup's focus ring is a 3px white band just inside the focus rect (measured:
// rect edge 138 → band 138..141). Stroke centred on the rect inset by half the width.
inline void focus_ring(ui::Canvas& c, const ui::Rect& r, float radius, float width) {
    const float h = width * 0.5f;
    c.stroke_round_rect(rect(r.x + h, r.y + h, r.w - width, r.h - width), radius - h, width,
                        ui::theme::focus, Align::Center);
}

// The bottom hint bar (item 5 + item 21): ONE component on every screen,
// RIGHT-ALIGNED to kHintRight inside the reserved bottom band (y >= kHintBandTop),
// all items on one line, equal 40px gaps. pairs = {"A","Select",...}, n = pairs.
// x0 stays the LEFT limit (the bar never crosses it); the layout starts from
// the right edge and runs left.
inline float draw_hint_bar(ui::Canvas& c, const Font& f, const char* const* pairs, int n, float x0 = ui::theme::margin) {
    const float px = ui::theme::hintText.px;
    const float cy = kHintY + kHintDiscPx * 0.5f;
    // text() is top-anchored: top = disc-centre minus half the line box puts
    // the label's vertical centre on the disc centre (item 5: centred labels).
    const float lh = f.line_height(Face::Regular, px);
    const float label_top = cy - lh * 0.5f;
    // Item D13: a key with more than one character (LB, RB, L3) is drawn as a
    // pill wide enough for its text instead of a letter disc. Both passes use
    // this same width so the right-aligned bar stays exact.
    auto key_width = [&](int i) {
        const char* k = pairs[i];
        return k[1] ? f.measure(Face::Regular, 15.0f, k) + 20.0f : kHintDiscPx;
    };
    // measure pass for the right alignment (item 21)
    float total_w = 0.0f;
    int count = 0;
    for (int i = 0; i + 1 < n * 2; i += 2, ++count)
        total_w += key_width(i) + 12.0f + f.measure(Face::Regular, px, pairs[i + 1]);
    if (count > 1) total_w += 40.0f * (count - 1);
    float x = kHintRight - total_w;
    if (x < x0) x = x0;   // never run past the content's left edge
    for (int i = 0; i + 1 < n * 2; i += 2) {
        const char key = pairs[i][0];
        const char* label = pairs[i + 1];
        const float kw = key_width(i);
        if (pairs[i][1]) {
            // multi-character key: a pill with its text centred
            c.stroke_round_rect(rect(x, cy - kHintDiscPx * 0.5f + 1.0f, kw, kHintDiscPx - 2.0f), (kHintDiscPx - 2.0f) * 0.5f,
                                2.0f, ui::theme::glyph, Align::Center);
            text_c(c, f, Face::Regular, 15, x + kw * 0.5f, cy - 9.0f, pairs[i], 0xFFC8C8C8);
        } else if (key == 'M') {
            // item 20: same disc size as every other prompt (complete, equal)
            ui::glyphs::menu(c, x + kHintDiscPx * 0.5f, cy, kHintDiscPx, ui::theme::glyph, 0xFFC8C8C8);
        } else {
            const bool filled = key == 'A';
            const uint32_t disc = filled ? ui::theme::glyph : 0;
            const uint32_t letter = filled ? ui::theme::glyphLabel : 0xFFC8C8C8;
            if (filled) {
                ui::glyphs::face_button(c, x + kHintDiscPx * 0.5f, cy, kHintDiscPx, key, f, disc, letter);
            } else {
                // item 20: outer diameter == kHintDiscPx (stroke centred on r-1)
                c.stroke_circle(x + kHintDiscPx * 0.5f, cy, kHintDiscPx * 0.5f - 1.0f, 2.0f,
                                ui::theme::glyph, Align::Center);
                float ix0, iy0, ix1, iy1;   // centre the visible letter on the ring
                if (f.ink_box(Face::Bold, 15.0f, std::string(1, key), ix0, iy0, ix1, iy1))
                    f.draw(c, Face::Bold, 15.0f, x + kHintDiscPx * 0.5f - (ix0 + ix1) * 0.5f, cy - (iy0 + iy1) * 0.5f,
                           std::string(1, key), letter);
            }
        }
        text(c, f, Face::Regular, px, x + kw + 12.0f, label_top, label, ui::theme::hint);
        x += kw + 12.0f + f.measure(Face::Regular, px, label) + 40.0f;
    }
    return x;
}

// ---- sidebar (shared by library and download-queue screens) ----
inline void draw_sidebar(ui::Canvas& c, const Font& f, const SidebarVM& sb) {
    using namespace ui::theme;
    c.fill_round_rect(rect(kSidebarX, kSidebarY, kSidebarW, kSidebarH), radiusPanel, ui::theme::panel);
    // item 14: the PROFILE row (avatar + persona, sidebar index 0) is a
    // focusable entry; A on it opens the logout confirm sheet. It only ever
    // wears the focus pill — the green bar stays reserved for the active
    // VIEW entry (item 16's rule applied to every non-view row). The pill is
    // drawn BEFORE the avatar/persona so they sit on top of it.
    if (sb.profile_focus || ui_nav::kSidebarProfile == sb.focus)
        c.fill_round_rect(rect(40, 68, 395, 64), radiusControl, ui::theme::control);
    // item 24: the signed-in user's avatar, circle-masked; empty draws the
    // placeholder (head + shoulders). Same diameter as the placeholder (56).
    if (sb.avatar.px && sb.avatar.w > 0 && sb.avatar.h > 0)
        c.draw_image(sb.avatar, rect(kSidebarX + 32, 72, 56, 56), ui::Fit::Cover, 28.0f);
    else {
        c.fill_circle(kSidebarX + 60, 100, 28, kAvatarBg);
        c.fill_circle(kSidebarX + 60, 94, 10, 0xFFC8C8C8);
        c.fill_round_rect(rect(kSidebarX + 44, 106, 32, 14), 7, 0xFFC8C8C8);
    }
    // item 9: persona not resolved -> a neutral label, never a blank row
    text(c, f, Face::Regular, 24, 136, 85.5f, sb.persona && *sb.persona ? sb.persona : "Signed in",
         sb.profile_focus || ui_nav::kSidebarProfile == sb.focus ? ui::theme::text : ui::theme::textDim);
    c.fill_rect(64, 248, 347, 1, ui::theme::separator);
    // item 16: the Search row is a search FIELD — control fill, dim
    // placeholder ('Search'), the query when one is active; never the
    // active-entry green bar, only the focus look when focused.
    // Item D13: it is a real-looking input — narrower than the view rows,
    // with a magnifier mark, the typed text live and a caret at the end.
    {
        const bool focused = ui_nav::kSidebarSearch == sb.focus;
        const Rect field = rect(40, 160, 300, 64);
        c.fill_round_rect(field, radiusControl, ui::theme::control);
        if (focused) focus_ring(c, rect(field.x - ringOffset, field.y - ringOffset, field.w + ringOffset * 2, field.h + ringOffset * 2),
                                radiusControl + ringRadiusDelta, ringWidth);
        // magnifier: a ring and its handle, 20px, at the field's left pad
        const float mx = field.x + 26.0f, my = field.y + 32.0f;
        c.stroke_circle(mx, my - 2.0f, 7.0f, 2.0f, ui::theme::textDim, Align::Center);
        { const float h[4] = {mx + 5.0f, my + 3.0f, mx + 10.0f, my + 8.0f};
          c.stroke_polyline(h, 2, false, 2.0f, ui::theme::textDim); }
        const char* q = sb.search_query && *sb.search_query ? sb.search_query : "Search";
        const bool typed = sb.search_query && *sb.search_query;
        const float tx = field.x + 48.0f;
        text(c, f, Face::Regular, 22, tx, field.y + 17.5f, q, focused || typed ? ui::theme::text : ui::theme::textDim);
        if (focused) {   // the caret sits after the text (the field edits at the end)
            const float w = typed ? f.measure(Face::Regular, 22, sb.search_query) : 0.0f;
            const float lh = f.line_height(Face::Regular, 22);
            c.fill_rect(tx + w + 1.0f, field.y + 16.0f, 2.0f, lh, ui::theme::accent);
        }
    }
    // nav rows: the view entries, sidebar indexes 2..4 (item 31: only
    // All games / Installed / Download queue — Storage is the disk widget).
    struct Row { const char* label; int count; };
    const Row rows[3] = {{"All games", sb.all_count}, {"Installed", sb.installed_count},
                         {"Download queue", sb.queue_count}};
    float y = 264.0f;
    for (int i = 0; i < 3; ++i) {
        const int entry = i + 2;   // sidebar index: the view rows start at 2 (item 14)
        const bool on = entry == sb.active;
        const bool focused = entry == sb.focus;
        // item 30: the FOCUSED row wears ring + fill; the ACTIVE view row keeps
        // only the control pill + green bar (the two must stay visually distinct).
        if (focused) {
            c.fill_round_rect(rect(40, y, 395, 64), radiusControl, ui::theme::control);
            focus_ring(c, rect(40 - ringOffset, y - ringOffset, 395 + ringOffset * 2, 64 + ringOffset * 2),
                       radiusControl + ringRadiusDelta, ringWidth);
        } else if (on) {
            c.fill_round_rect(rect(40, y, 395, 64), radiusControl, ui::theme::control);
        }
        if (on) c.fill_rect(429, y, 6, 64, ui::theme::accent);
        text(c, f, Face::Regular, 24, 64, y + 17.5f, rows[i].label,
             on || focused ? ui::theme::text : ui::theme::text2);
        if (rows[i].count >= 0) {
            char n[16];
            std::snprintf(n, sizeof n, "%d", rows[i].count);
            text_r(c, f, Face::Regular, 22, 411, y + 18.5f, n, ui::theme::textDim);
        }
        y += 64.0f;
    }
    c.fill_rect(64, 920, 347, 1, ui::theme::separator);
    // item 31: the DISK WIDGET is a button-like region (sidebar entry 5), not a
    // list row: rounded-8 rect spanning the widget area with 12px padding.
    //   focused (sb.focus == 5 / sb.disk_focus): control fill + focus ring;
    //   active  (sb.disk_active, the Storage screen is shown): subtle fill +
    //           accent donut stroke;
    //   otherwise: today's plain look (no fill, no ring).
    {
        const bool focused = sb.focus == 5 || sb.disk_focus;
        const bool active = sb.disk_active;
        // item 31 round-6: the widget rect + its focus ring (offset 6 + 3px
        // width) must lie fully inside the sidebar panel (x 32..443,
        // y 32..1048) with >=8px margin. Widget rect x 52..424, y 936..1032:
        // ring band 43..433 / 927..1041 — inside the panel with 11px margin
        // left/right, 5px top... so the rect is y 940..1032 (ring 931..1041,
        // >=7px margin bottom). Text and donut keep >=12px padding inside it.
        const Rect w = rect(52, 940, 372, 92);
        if (focused)
            c.fill_round_rect(w, radiusCard, ui::theme::control);
        else if (active)
            c.fill_round_rect(w, radiusCard, ui::theme::controlDark);
        char b[48];
        std::snprintf(b, sizeof b, "%d.%d GB free", sb.free_gb_x10 / 10, sb.free_gb_x10 % 10);
        text(c, f, Face::Regular, 22, 64, 946.0f, b, ui::theme::text2);
        std::snprintf(b, sizeof b, "%d%% used", sb.used_pct);
        text(c, f, Face::Regular, 20, 64, 976.0f, b, ui::theme::textDim);
        // donut: track ring r=26 stroke 5; the used arc centred on 6 o'clock.
        // Active: the track+arc draw in ACCENT (item 31: accent donut stroke).
        // Donut centre (383, 981): its box 351..415 x 949..1013 stays inside
        // the widget rect with 12px padding right (424-415=9... the rect is
        // 52..424 and the donut 351..415 -> 9px; keep the rect at 52..424 and
        // the donut inside it).
        const uint32_t track = active ? ui::theme::accent : ui::theme::track;
        const uint32_t arc = active ? ui::theme::accentSoft : ui::theme::accent;
        c.fill_donut(383, 981, 26, 5, -ui::kPi * 0.5f, ui::kTau, track);
        if (sb.used_pct > 0) {
            const float sweep = ui::kTau * sb.used_pct / 100.0f;
            c.fill_donut(383, 981, 26, 5, ui::kPi * 0.5f - sweep * 0.5f, sweep, arc);
        }
        std::snprintf(b, sizeof b, "%d%%", sb.used_pct);
        text_c(c, f, Face::Regular, 16, 383, 970.5f, b, ui::theme::textDim);
        if (focused)
            focus_ring(c, rect(52 - ringOffset, 940 - ringOffset, 372 + ringOffset * 2, 92 + ringOffset * 2),
                       radiusCard + ringRadiusDelta, ringWidth);
    }
}

// ---- library screen (mockup 01) ----
// Item D10: the Menu button opens the focused tile's game options, so the
// library bar labels it "Manage" (the detail page already says the same).
constexpr const char* kLibraryHints[6] = {"A", "Select", "B", "Back", "M", "Manage"};
// Item D7: while a query is filtering the grid, the bar also offers the clear
// (X on the sidebar's Search row) — the filtered state is never a dead end.
constexpr const char* kLibrarySearchHints[8] = {"A", "Select", "B", "Back", "X", "Clear search", "M", "Manage"};
// sort options (item 1; item D10 adds Favorites); mode indexes match
// ui_nav::cycle_sort's caller
constexpr const char* kSortOptions[4] = {"Alphabetical", "Most played", "Recently played", "Favorites"};
constexpr int kSortOptionCount = ui_nav::kSortOptionCount;   // one source of truth with main.cpp

// item 11 + 23: marquee offset for a focused tile's label. Static when the
// text fits box_w. Otherwise, in READING order only (item 23): hold at the
// start (pause), scroll left until the end is visible, hold at the end, then
// reset INSTANTLY to the start (no slow scroll back) and loop.
inline float marquee_offset(float text_w, float box_w, float t) {
    if (text_w <= box_w || text_w <= 0) return 0.0f;
    if (t < 0) t = 0.0f;                                 // negative times clamp to the start
    const float travel = text_w - box_w + 24.0f;        // + ellipsis gap
    const float pause = 1.2f, speed = 30.0f;            // px/s
    const float move = travel / speed;
    const float period = 2.0f * pause + move;           // start hold + sweep + end hold, then reset
    float k = t - std::floor(t / period) * period;      // [0, period)
    if (k < pause) return 0.0f;                         // pause at the start
    k -= pause;
    if (k < move) return travel * (k / move);           // scroll left
    return travel;                                       // hold at the end, then the loop resets
}

// Item D4: the focused tile's label at marquee offset `off`, trimmed at a
// UTF-8 character boundary and marked with U+2026 when the tail is still
// outside the box — never a mid-glyph cut, and never the ambiguous "." form.
// Returns the text to draw and, in out_x, the pen x it starts at.
inline std::string marquee_view(const Font& f, Face face, float px, const std::string& name,
                                float off, float box_w, float x0, float& out_x) {
    const char* p = name.data(); const char* end = p + name.size();
    const char* cut = p;
    float adv = 0.0f;
    while (cut < end) {                       // skip whole glyphs that start before the offset
        const char* st = cut;
        uint32_t cp;
        ui::utf8_next(cut, end, cp);
        if (adv >= off) { cut = st; break; }
        adv += f.measure(face, px, std::string(st, std::size_t(cut - st)));
    }
    out_x = x0 + (adv - off);                 // never left of the box (no partial glyph)
    const float room = box_w - (out_x - x0);
    return f.ellipsize(face, px, std::string(cut, std::size_t(end - cut)), room > 0 ? room : 0.0f);
}

// 3-stop gradient into `dst` (one pixel per call site loop); used for tile and hero art.
// k is the position along the gradient axis: 0 → top, 0.5 → mid, 1 → bottom stop.
inline uint32_t grad3(uint32_t top, uint32_t mid, uint32_t bot, float k) {
    const uint32_t a = k < 0.5f ? top : mid;
    const uint32_t b = k < 0.5f ? mid : bot;
    const float u = k < 0.5f ? k * 2 : (k - 0.5f) * 2;
    const unsigned r = unsigned(((a >> 16) & 0xFF) * (1 - u) + ((b >> 16) & 0xFF) * u);
    const unsigned g = unsigned(((a >> 8) & 0xFF) * (1 - u) + ((b >> 8) & 0xFF) * u);
    const unsigned bl = unsigned((a & 0xFF) * (1 - u) + (b & 0xFF) * u);
    return ui::theme::argb(255, r, g, bl);
}

// item 12: games without cover art get a plain grey vertical gradient tile
// (#3A3A3A -> #232323), no ring/placeholder motif.
constexpr uint32_t kNoCoverTop = 0xFF3A3A3A, kNoCoverBot = 0xFF232323;

// scrim over a freshly drawn page (sheets/popups, items 3 + 8)
inline void draw_scrim(ui::Canvas& c) { c.fill_rect_alpha(0, 0, 1920, 1080, ui::theme::scrim, ui::theme::scrimAlpha); }

// check-mark polyline for the sort popup's current option (drawn inline)

inline void draw_tile(ui::Canvas& c, const Font& f, const TileVM& t, const ui::Rect& g, bool focus, float
 time_s = 0.0f) {
    using
 namespace ui::theme;
    // clip everything (art, label, marquee) to the tile rect (item 4);
    // set_clip REPLACES, so intersect with the caller's clip by hand
    const ui::Canvas::Clip outer = c.clip();
    c.set_clip(ui::Canvas::Clip{std::max(outer.x0, int(g.x)), std::max(outer.y0, int(g.y)),
                                std::min(outer.x1, int(g.x + g.w)), std::min(outer.y1, int(g.y + g.h))});
    const bool has_art = t.art.px && t.art.w > 0 && t.art.h > 0;
    if (has_art) {
        c.draw_image(t.art, g, ui::Fit::Cover, radiusTile);
    } else if (t.no_cover) {
        // item 12: plain grey vertical gradient, no motif
        for (int yy = 0; yy < int(g.h); ++yy)
            c.fill_rect(g.x, g.y + float(yy), g.w, 1, grad3(kNoCoverTop, kNoCoverTop, kNoCoverBot, float(yy) / (g.h - 1)));
    } else {
        // legacy gradient art (mockup fixtures)
        for (int yy = 0; yy < int(g.h); ++yy) {
            const float v = float(yy) / (g.h - 1);
            for (int xx = 0; xx < int(g.w); ++xx) {
                const float u = float(xx) / (g.w - 1);
                c.fill_rect(g.x + xx, g.y + yy, 1, 1, grad3(t.art_top, t.art_mid, t.art_bot, (u + v) * 0.5f));
            }
        }
        // art motif: dark ring + corner block in the tile's own dark color
        // (mockup: ring centre sits 150px below the tile top, block at 0.22 alpha)
        c.stroke_circle(g.x + g.w * 0.5f, g.y + 150, 48, 8, t.art_bot, Align::Center, 77);
        c.fill_rect_alpha(float(g.x + 112), float(g.y + 12), 44.0f, 44.0f, t.art_bot, 56);
    }
    // item 23: the label scrim runs on EVERY tile (art, no-cover, fixtures):
    // transparent at ~55% height down to 0.85 black at the bottom edge
    for (int yy = int(g.h) * 55 / 100; yy < int(g.h); ++yy) {
        const float k = (yy - g.h * 0.55f) / (g.h * 0.45f);
        c.fill_rect_alpha(g.x, g.y + yy, g.w, 1, scrim, uint8_t(0.85f * 255.0f * k));
    }
    // item 23: the name sits at the BOTTOM of the card — semibold 22, white,
    // single line, ellipsized to the tile width; the FOCUSED tile marquees
    const float label_w = g.w - 24.0f;
    const float label_top = g.y + g.h - 34.0f;   // 22px line box, 12px bottom pad
    if (focus && t.name_w > label_w && t.name_w > 0) {
        // full name, scrolled by the marquee offset. Item D4: the visible part
        // is trimmed at a whole glyph and marked with U+2026, so the label is
        // never cut mid-glyph and never ends in an ambiguous ".".
        const float off = marquee_offset(t.name_w, label_w, time_s);
        float tx = 0.0f;
        const std::string s = marquee_view(f, Face::Semibold, 22, t.name, off, label_w, g.x + 12, tx);
        text(c, f, Face::Semibold, 22, tx, label_top, s.c_str(), ui::theme::text);
    } else {
        const std::string s = f.ellipsize(Face::Semibold, 22, t.name, label_w);
        text(c, f, Face::Semibold, 22, g.x + 12, label_top, s.c_str(), ui::theme::text);
    }
    if (t.installed) {
        c.fill_round_rect(rect(g.x + 12, g.y + 12, 94, 26), radiusBadge, ui::theme::accent);
        text_c(c, f, Face::Bold, 15, g.x + 59, g.y + 16, "INSTALLED", ui::theme::onAccent);
    }
    // item D10: the favorite marker sits top-RIGHT (the INSTALLED badge owns
    // the left corner), so both can show at once on a 232-wide tile.
    if (t.favorite) {
        const float d = 34.0f;
        const float fx = g.x + g.w - 12.0f - d, fy = g.y + 12.0f;
        c.fill_round_rect(rect(fx, fy, d, d), d * 0.5f, ui::theme::argb(180, 0, 0, 0));
        ui::glyphs::star(c, rect(fx + 8.0f, fy + 8.0f, d - 16.0f, d - 16.0f), ui::theme::accentSoft);
    }
    c.set_clip(outer);  // restore the caller's clip (the tile clip above was narrower)
    if (focus)
        focus_ring(c, rect(g.x - ringOffset, g.y - ringOffset, g.w + ringOffset * 2, g.h + ringOffset * 2),
                   radiusTile + ringRadiusDelta, ringWidth);
}

inline void draw_sort_control(ui::Canvas& c, const Font& f, const LibraryVM& lib, float x, float y) {
    using namespace ui::theme;
    // closed control: pill with the current option; the OPEN control wears
    // the same focus treatment as every focusable control (item 1)
    const float w = 430.0f;
    c.fill_round_rect(rect(x, y, w, 60), radiusPill, lib.sort_open ? ui::theme::controlDark : ui::theme::control);
    if (lib.sort_open || lib.header_focus == 0)
        focus_ring(c, rect(x - ringOffset, y - ringOffset, w + ringOffset * 2, 60 + ringOffset * 2),
                   radiusPill + ringRadiusDelta, ringWidth);
    text(c, f, Face::Regular, 24, x + 24, y + 15.5f, lib.sort_label, ui::theme::text2);
}

// Filters control (item 1, review fix 3): the header keeps Sort + Filters
// only — the Search pill is gone (the sidebar's Search row is the entry).
inline void draw_filters_control(ui::Canvas& c, const Font& f, const LibraryVM& lib, float x, float y) {
    using namespace ui::theme;
    const float w = 152.0f;
    c.fill_round_rect(rect(x, y, w, 60), radiusPill, lib.filters_open ? ui::theme::controlDark : ui::theme::control);
    if (lib.filters_open || lib.header_focus == 1)
        focus_ring(c, rect(x - ringOffset, y - ringOffset, w + ringOffset * 2, 60 + ringOffset * 2),
                   radiusPill + ringRadiusDelta, ringWidth);
    text_c(c, f, Face::Regular, 24, x + w * 0.5f, y + 15.5f,
           lib.filters_active > 0 ? (std::string("Filters \xC2\xB7 ") + std::to_string(lib.filters_active)).c_str()
                                  : "Filters", ui::theme::text2);
    if (lib.filters_active > 0) {   // item 26: accent state when any filter is on
        c.stroke_round_rect(rect(x, y, w, 60), radiusPill, ringWidth, ui::theme::accent, Align::Center);
    }
}

// Sort dropdown (item 1, review fix 2): ONLY the sort options, left-aligned
// under the Sort control, same width (430). The Filters popup is a SEPARATE
// card under the Filters button, right edge aligned to the button's right
// edge (1844). Only one popup is open at a time (the caller guarantees it:
// opening one closes the other — see ui_nav::popup_exclusive).
inline void draw_sort_popup(ui::Canvas& c, const Font& f, const LibraryVM& lib) {
    using namespace ui::theme;
    if (!lib.sort_open) return;
    const float x = 524.0f, y = 236.0f, w = 430.0f, row_h = 64.0f;
    const int n = kSortOptionCount;
    c.fill_round_rect(rect(x, y, w, row_h * n + 16), radiusCard, panel);
    for (int i = 0; i < n; ++i) {
        const float ry = y + 8.0f + row_h * i;
        if (i == lib.sort_focus) c.fill_round_rect(rect(x + 8, ry, w - 16, row_h), radiusControl, rowHover);
        text(c, f, Face::Regular, 24, x + 28, ry + 17.5f, kSortOptions[i],
             i == lib.sort_focus ? ui::theme::text : ui::theme::text2);
        if (i == lib.sort_focus) {   // check mark (accent) right of the label
            const float cw = 18.0f, ch = 14.0f;
            const float cx = x + 28 + f.measure(Face::Regular, 24, kSortOptions[i]) + 16.0f, cy2 = ry + 25.0f;
            const float p[6] = {cx, cy2 + ch * 0.6f, cx + cw * 0.35f, cy2 + ch, cx + cw, cy2};
            c.stroke_polyline(p, 3, false, 3.0f, ui::theme::accent);
        }    }
    // item 17: the popup's own hint bar (the base screen's is suppressed)
    const char* pairs[4] = {"A", "Select", "B", "Close"};
    draw_hint_bar(c, f, pairs, 2, 524.0f);
}

// Filters panel (item 1, review fix 2): toggle rows the caller provides,
// anchored under the FILTERS button (right edge 1844 = the button's right
// edge), NOT under Sort. The popup is 430 wide like the Sort dropdown.
struct FilterRowVM { const char* label = ""; bool on = false; };

inline void draw_filters_panel(ui::Canvas& c, const Font& f, const FilterRowVM* rows, int n, int focus) {
    using namespace ui::theme;
    if (!rows || n <= 0) return;
    const float w = 430.0f, row_h = 64.0f;
    const float x = 1844.0f - w, y = 236.0f;   // right edge under the Filters button
    c.fill_round_rect(rect(x, y, w, row_h * n + 16), radiusCard, panel);
    for (int i = 0; i < n; ++i) {
        const float ry = y + 8.0f + row_h * i;
        if (i == focus) c.fill_round_rect(rect(x + 8, ry, w - 16, row_h), radiusControl, rowHover);
        text(c, f, Face::Regular, 24, x + 28, ry + 17.5f, rows[i].label,
             i == focus ? ui::theme::text : ui::theme::text2);
        // On/Off pill with a round knob (item 2: pill = h/2); round 6: NO
        // inner text (it overlapped the knob) — the knob position + the
        // accent track carry the state
        const float px = x + w - 92.0f, py = ry + 16.0f;
        c.fill_round_rect(rect(px, py, 56, 32), 16, rows[i].on ? ui::theme::accent : ui::theme::controlDark);
        c.fill_circle(rows[i].on ? px + 40 : px + 16, py + 16, 12, ui::theme::onAccent);
    }
    // item 17: the popup draws its own hint bar (the base screen's is suppressed)
    const char* pairs[4] = {"A", "Toggle", "B", "Close"};
    draw_hint_bar(c, f, pairs, 2, 524.0f);
}

// Search overlay (item 1, item D13): the on-screen keyboard is a panel anchored
// to the BOTTOM of the page, just above the hint band, spanning the content
// area — the library stays visible above it. Pure nav in ui_nav.h
// (search_move/search_activate/SearchState); this only draws the state. Key
// ids: 0..25 A-Z, 26..35 0-9, 36 space, 37 backspace, 38 done.
constexpr char kSearchLetter = 'A';

// Item D13: the panel's geometry, shared by the draw and the tests so a key's
// rect is never guessed. Bottom-anchored: the panel's bottom sits 16px above
// the hint band; the query line is the first row inside it.
struct SearchPanel {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;   // the panel rect
    float query_x = 0.0f, query_y = 0.0f;           // the query line's top-left
    float gx = 0.0f, gy = 0.0f;                     // the key grid's top-left
    float key_w = 0.0f, key_h = 0.0f, gap = 0.0f;
};
inline SearchPanel search_panel() {
    SearchPanel p;
    p.x = kGridX;                                  // the content area's left edge
    p.w = kHintRight - kGridX;                     // ... and its right edge
    p.key_h = 56.0f; p.gap = 8.0f;
    const float pad = 40.0f;
    p.key_w = (p.w - pad * 2.0f - 9.0f * p.gap) / 10.0f;
    const float query_h = 56.0f, query_gap = 16.0f, pad_bottom = 24.0f;
    p.h = 24.0f + query_h + query_gap + 4.0f * p.key_h + 3.0f * p.gap + pad_bottom;
    p.y = kHintBandTop - 16.0f - p.h;              // bottom-anchored above the hint band
    p.query_x = p.x + pad; p.query_y = p.y + 24.0f;
    p.gx = p.x + pad; p.gy = p.query_y + query_h + query_gap;
    return p;
}
inline Rect search_key_rect(const SearchPanel& p, int idx) {
    const int row = ui_nav::search_key_row(idx), col = ui_nav::search_key_col(idx);
    return rect(p.gx + col * (p.key_w + p.gap), p.gy + row * (p.key_h + p.gap), p.key_w, p.key_h);
}

// Item D13: the key's label. Letters follow caps lock (the focused key types
// what it shows), and the three non-alphanumeric keys keep text labels
// (Selawik has no backspace glyph).
inline std::string search_key_text(int i, bool caps) {
    if (i < 26) return std::string(1, char((caps ? 'A' : 'a') + i));
    if (i < 36) return std::string(1, char('0' + i - 26));
    return i == 36 ? "Space" : i == 37 ? "Delete" : "Done";
}

inline void draw_search_overlay(ui::Canvas& c, const Font& f, const LibraryVM& lib) {
    using namespace ui::theme;
    if (!lib.search_open) return;
    draw_scrim(c);
    const SearchPanel p = search_panel();
    c.fill_round_rect(rect(p.x, p.y, p.w, p.h), radiusGuide, panel);
    // query line: the typed text with a caret at the caret position, or the
    // dim placeholder when nothing is typed yet.
    const char* q = lib.query ? lib.query : "";
    float qx = p.query_x;
    if (*q) {
        text(c, f, Face::Regular, 28, qx, p.query_y, q, ui::theme::text);
        const std::size_t cut = ui_nav::search_offset(q, lib.search_caret);
        qx += f.measure(Face::Regular, 28, std::string(q, cut));
    } else {
        text(c, f, Face::Regular, 28, qx, p.query_y, "Search games", ui::theme::textDim);
    }
    {   // the caret: a 2px accent bar on the text's line
        const float lh = f.line_height(Face::Regular, 28);
        c.fill_rect(qx + 1.0f, p.query_y + 4.0f, 2.0f, lh - 8.0f, ui::theme::accent);
    }
    if (lib.search_caps) {   // L3 state, so the letter keys' case is never a mystery
        const float bw = 78.0f, bh = 32.0f;
        const float bx = p.x + p.w - 40.0f - bw, by = p.query_y + 6.0f;
        c.fill_round_rect(rect(bx, by, bw, bh), radiusBadge, ui::theme::accent);
        text_c(c, f, Face::Bold, 15, bx + bw * 0.5f, by + 6.0f, "CAPS", ui::theme::onAccent);
    }
    for (int i = 0; i < ui_nav::kSearchKeyCount; ++i) {
        const Rect kr = search_key_rect(p, i);
        const bool focused = i == lib.search_key;
        // review fix 4: the focused key reads clearly — accent fill, onAccent
        // label (the hover grey was too subtle against the key grid)
        c.fill_round_rect(kr, radiusControl, focused ? ui::theme::accent : ui::theme::controlDark);
        const std::string label = search_key_text(i, lib.search_caps);
        text_c(c, f, Face::Regular, i < 36 ? 24 : 18, kr.x + kr.w * 0.5f, kr.y + 13.0f, label.c_str(),
               focused ? ui::theme::onAccent : ui::theme::text2);
    }
    // Item D13: the keyboard's own controls — X deletes, LB/RB move the caret,
    // Y applies and closes, L3 toggles caps, A types, B closes.
    const char* pairs[12] = {"A", "Type", "X", "Delete", "LB", "RB", "Move", "Y", "Search", "L3", "Caps", "B"};
    draw_hint_bar(c, f, pairs, 6, 524.0f);
}

// Item D7: the active search query shows in the header as a chip, so a
// filtered grid is never mistaken for the whole library. It sits between the
// Sort control and the right-aligned count; the text is ellipsized into the
// room it has.
inline void draw_query_chip(ui::Canvas& c, const Font& f, const LibraryVM& lib) {
    using namespace ui::theme;
    if (!lib.query || !*lib.query) return;
    const float x = 970.0f, h = 44.0f, y = 176.0f, max_w = 300.0f;
    const std::string chip_text = f.ellipsize(Face::Regular, 22, lib.query, max_w - 96.0f);
    const float w = std::min(max_w, 96.0f + f.measure(Face::Regular, 22, chip_text));
    c.fill_round_rect(rect(x, y, w, h), radiusPill, ui::theme::control);
    c.stroke_circle(x + 24.0f, y + h * 0.5f - 1.0f, 6.0f, 2.0f, ui::theme::accent, Align::Center);
    { const float hd[4] = {x + 28.0f, y + h * 0.5f + 3.0f, x + 33.0f, y + h * 0.5f + 8.0f};
      c.stroke_polyline(hd, 2, false, 2.0f, ui::theme::accent); }
    text(c, f, Face::Regular, 22, x + 44.0f, y + 8.0f, chip_text.c_str(), ui::theme::text);
}

// item 17: draw_hints = false suppresses the base screen's hint bar (a sheet
// drawn over the page brings its own bar; the two must never stack).
inline void draw_library(ui::Canvas& c, const Font& f, const SidebarVM& sb, const LibraryVM& lib,
                         bool draw_hints = true) {
    using namespace ui::theme;
    c.fill(ui::theme::page);   // item 3: full clear first
    draw_sidebar(c, f, sb);
    // title + header row: Sort control, count, Filters (item 1; review fix 3:
    // the header Search pill is GONE — the sidebar's Search row is the entry)
    text(c, f, Face::Semibold, 44, kTitleX, kTitleY, "Library", ui::theme::text, -0.2f);
    draw_sort_control(c, f, lib, 524.0f, 168.0f);
    const char* count = lib.loading ? "Loading..." : lib.count_label;
    if (count && *count) text_r(c, f, Face::Regular, 22, 1444.0f, 184.5f, count, ui::theme::textDim);
    draw_query_chip(c, f, lib);   // item D7: the active query, so the count's "N of M" has a cause
    draw_filters_control(c, f, lib, 1692.0f, 168.0f);
    // grid: 5 columns, clipped to the viewport between the header row and the
    // hint band (item 4; item D8: the TOP edge matters too — a scrolled row
    // used to draw over the Sort/Filters controls and the count)
    int lo, hi;
    grid_visible(lib.tile_count, lib.top_row, lo, hi, lib.cols);
    c.set_clip(rect(0, kGridClipTop, 1920, kGridClipY - kGridClipTop));
    for (int i = lo; i < hi; ++i) {
        const Rect g = grid_tile(i, lib.top_row, lib.cols);
        if (g.y > kGridClipY || g.y + g.h < kGridClipTop) continue;
        draw_tile(c, f, lib.tiles[i], g, lib.focus_tile && i == lib.sel && lib.header_focus < 0, lib.t);
    }
    c.reset_clip();
    // item 17: ONE hint bar per frame. When any sheet/overlay is up, the base
    // screen's own bar must not remain under the sheet's — the sheet draws
    // its own bar over the scrim. So the base bar is drawn only when nothing
    // is open (the popups below draw their own). Sheets OUTSIDE LibraryVM
    // (logout, delete, cloud) suppress the bar through `draw_hints` = false.
    const bool sheet_up = lib.sort_open || lib.filters_open || lib.search_open;
    const bool searching = lib.query && *lib.query;
    if (!sheet_up && draw_hints) {
        if (searching) draw_hint_bar(c, f, kLibrarySearchHints, 4, 524.0f);
        else draw_hint_bar(c, f, kLibraryHints, 3, 524.0f);
    }
    // popups over the page (item 1); each draws its own hint bar when open.
    // Item 15: the filters panel draws ONLY while open — the library render
    // itself never paints it.
    draw_sort_popup(c, f, lib);
    if (lib.filters_open) {
        // item D10: the Favorites row joins Playable; both filter the grid
        FilterRowVM installed_only[2] = {{"Playable", lib.filter_playable}, {"Favorites", lib.filter_favorites}};
        draw_filters_panel(c, f, installed_only, 2, lib.filters_focus);
    }
    draw_search_overlay(c, f, lib);
}

// ---- download queue screen (mockup 06) ----
constexpr const char* kQueueHints[6] = {"A", "Select", "B", "Back", "M", "Menu"};

inline void draw_queue_card(ui::Canvas& c, const Font& f, const QueueRowVM& r, float y, bool update) {
    using namespace ui::theme;
    c.fill_round_rect(rect(524, y, 1320, 104), radiusCard, ui::theme::panel);
    c.fill_round_rect(rect(540, y + 16, 48, 72), radiusControl, r.art);
    // mockup: every row carries the dark status ring (26px, stroke 4) at its thumb
    c.stroke_circle(564, y + 52, 14, 4, kRingDark, Align::Center);
    // the busy row stacks name/status/progress from the top; a 2-row card centres them
    const float top = r.busy ? 16.0f : 24.0f;
    // item D4: a long game name is ellipsized to the row (the progress bar and
    // the buttons start at x=1556, so the name column ends well before that)
    const std::string row_name = f.ellipsize(Face::Regular, 22, r.name, 920.0f);
    text(c, f, Face::Regular, 22, 604, y + top, row_name.c_str(), ui::theme::text);
    text(c, f, Face::Regular, 20, 604, y + top + 31.0f, r.status, ui::theme::textDim);
    if (r.busy) {
        c.fill_round_rect(rect(604, y + 80, 920, 8), 4, ui::theme::track);
        if (r.pct > 0) c.fill_round_rect(rect(604, y + 80, 920 * r.pct, 8), 4, ui::theme::accent);
    }
    if (update) {
        c.fill_round_rect(rect(1668, y + 28, 160, 48), radiusControl, ui::theme::controlDark);
        button_label(c, f, Face::Semibold, 20, rect(1668, y + 28, 160, 48), "UPDATE NOW", ui::theme::text2);
    } else {
        c.fill_round_rect(rect(1700, y + 28, 128, 48), radiusControl, ui::theme::controlDark);
        button_label(c, f, Face::Semibold, 20, rect(1700, y + 28, 128, 48), r.paused ? "RESUME" : "CANCEL",
                     ui::theme::text2);
        if (r.busy) {   // the downloading row also shows PAUSE (mockup q0)
            c.fill_round_rect(rect(1556, y + 28, 128, 48), radiusControl, ui::theme::controlDark);
            button_label(c, f, Face::Semibold, 20, rect(1556, y + 28, 128, 48), "PAUSE", ui::theme::text2);
        }
    }
}

// item 17: draw_hints = false suppresses the base hint bar (see draw_library).
inline void draw_download_queue(ui::Canvas& c, const Font& f, const SidebarVM& sb, const QueueVM& q,
                                bool draw_hints = true) {
    using namespace ui::theme;
    c.fill(ui::theme::page);
    draw_sidebar(c, f, sb);
    text(c, f, Face::Semibold, 44, kTitleX, kTitleY, "Download queue", ui::theme::text, -0.2f);
    // auto-update pill (ui::theme::focus ring when q.focus == 0)
    c.fill_round_rect(rect(524, 144, 1320, 96), radiusCard, ui::theme::panel);
    text(c, f, Face::Semibold, 26, 556, 162, "Auto-update games", ui::theme::text);
    text(c, f, Face::Regular, 20, 556, 197, q.update_note, ui::theme::textDim);
    text_r(c, f, Face::Regular, 22, 1744, 178.5f, q.auto_update ? "On" : "Off", ui::theme::textDim);
    // auto-update pill: real pill (radius = h/2) with a round knob (item 2)
    c.fill_round_rect(rect(1756, 176, 56, 32), 16, q.auto_update ? ui::theme::accent : ui::theme::controlDark);
    c.fill_circle(q.auto_update ? 1796 : 1768, 192, 12, q.auto_update ? ui::theme::onAccent : ui::theme::textDim);
    if (q.focus == 0)
        focus_ring(c, rect(518, 138, 1332, 108), radiusCard + ringRadiusDelta, ringWidth);
    // queue section
    text(c, f, Face::Semibold, 26, 524, 272, "Queue", ui::theme::text);
    char b[32];
    std::snprintf(b, sizeof b, "%d items", q.row_count);
    text_r(c, f, Face::Regular, 22, 1844, 274, b, ui::theme::textDim);
    // queue rows: clipped to their own band (item 21 + item D8) — a long
    // queue must never paint over the Updates heading below it or into the
    // reserved hint band
    c.set_clip(rect(0, 300, 1920, 680 - 300));
    for (int i = 0; i < q.row_count; ++i) draw_queue_card(c, f, q.rows[i], 320 + 112.0f * i, false);
    c.reset_clip();
    // updates section
    text(c, f, Face::Semibold, 26, 524, 680, "Updates", ui::theme::text);
    std::snprintf(b, sizeof b, "%d pending", q.update_count);
    text_r(c, f, Face::Regular, 22, 1844, 682, b, ui::theme::textDim);
    c.set_clip(rect(0, 700, 1920, kGridClipY - 700));
    for (int i = 0; i < q.update_count; ++i) draw_queue_card(c, f, q.updates[i], 728 + 112.0f * i, true);
    c.reset_clip();
    if (draw_hints) draw_hint_bar(c, f, kQueueHints, 3, 524.0f);
}

// ---- storage management (item 22): drive cards + per-drive games ------------
inline void draw_storage(ui::Canvas& c, const Font& f, const SidebarVM& sb, const StorageVM& st,
                         bool draw_hints = true) {
    using namespace ui::theme;
    c.fill(ui::theme::page);
    draw_sidebar(c, f, sb);
    if (st.open_drive < 0) {
        // drive list: title + selectable drive cards (112 high, 24px gaps,
        // x 524..1844); content ends above the reserved hint band
        text(c, f, Face::Semibold, 44, kTitleX, kTitleY, "Storage", ui::theme::text, -0.2f);
        float y = 200.0f;
        for (int i = 0; i < st.n_drives && y + 112.0f <= kHintBandTop; ++i) {
            const StorageDriveVM& d = st.drives[i];
            const Rect card = rect(524.0f, y, 1320.0f, 112.0f);
            if (d.focused)
                focus_ring(c, rect(524 - ringOffset, y - ringOffset, 1320 + ringOffset * 2, 112 + ringOffset * 2),
                           radiusControl + ringRadiusDelta, ringWidth);
            c.fill_round_rect(card, radiusControl, d.focused ? ui::theme::controlDark : ui::theme::control);
            if (d.install_target) c.fill_rect(524, y, 6, 112, ui::theme::accent);
            text(c, f, Face::Semibold, 28, 548, y + 20, d.title, ui::theme::text);
            text(c, f, Face::Regular, 22, 548, y + 60, d.detail, ui::theme::textDim);
            // item 22: usage bar — 10px, rounded, full-width visible track
            c.fill_round_rect(rect(852, y + 51, 456, 10), 5, ui::theme::track);
            if (d.used_pct > 0)
                c.fill_round_rect(rect(852, y + 51, 456.0f * std::min(d.used_pct, 100) / 100.0f, 10), 5,
                                  ui::theme::accent);
            if (d.install_target) {
                // the marker as a real pill (radius = h/2) on the right
                const char* mk = "New installs go here";
                const float mw = f.measure(Face::Regular, 20, mk) + 32.0f;
                const float mh = 36.0f;
                c.fill_round_rect(rect(1812 - mw, y + (112 - mh) * 0.5f, mw, mh), mh * 0.5f,
                                  d.focused ? ui::theme::control : ui::theme::controlDark);
                text_r(c, f, Face::Regular, 20, 1812 - 16.0f, y + (112 - mh) * 0.5f + 8.0f, mk,
                       ui::theme::accentSoft);
            }
            y += 112.0f + 24.0f;
        }
        if (st.n_drives == 0)
            text(c, f, Face::Regular, 22, 548, y + 8.0f, "No drives found", ui::theme::textDim);
    } else {
        // the open drive's games list (item 22b): the page title carries the
        // drive's name with a muted "Installed games" subtitle; row 0 is the
        // caller-supplied "Use for new installs" action row (accent styling
        // when its size string is empty), then 72px rows: name Regular 26
        // left, size Regular 22 right in text2, no dividers; the focus ring
        // sits 4px off the row with 12px gaps so it never touches neighbours.
        text(c, f, Face::Semibold, 44, kTitleX, kTitleY, st.drive_title, ui::theme::text, -0.2f);
        text(c, f, Face::Regular, 22, kTitleX, kTitleY + 60.0f, "Installed games", ui::theme::textDim);
        const float list_top = 200.0f, row_h = 72.0f, row_pitch = 84.0f;   // 12px gaps: the 4px-off ring never touches a neighbour
        const float list_bottom = kHintBandTop - 16.0f;   // keep the band clear (item 21)
        const int visible = int((list_bottom - list_top) / row_pitch);
        const int first = std::max(0, std::min(st.game_scroll, st.n_games - visible));
        for (int i = first; i < st.n_games && i < first + visible; ++i) {
            const StorageGameVM& gm = st.games[i];
            const float ry = list_top + row_pitch * float(i - first);
            const bool action = i == 0 && (!gm.size || !*gm.size);   // the install-target action row
            // item D4: the name ellipsizes before the right-aligned size column
            const std::string row_name = f.ellipsize(Face::Regular, 26, gm.name, 1080.0f);
            text(c, f, Face::Regular, 26, 548, ry + 20.0f, row_name.c_str(), action ? ui::theme::accentSoft : ui::theme::text);
            if (!action)
                text_r(c, f, Face::Regular, 22, 1812, ry + 22.0f, gm.size, ui::theme::text2);
            if (gm.focused)
                focus_ring(c, rect(524 - 4.0f, ry - 4.0f, 1320 + 8.0f, row_h + 8.0f),
                           radiusControl + ringRadiusDelta, ringWidth);
        }
        if (st.n_games == 0)
            text(c, f, Face::Regular, 22, 548, list_top + 8.0f, "No games installed here", ui::theme::textDim);
    }
    if (draw_hints) {
        const char* pairs[4] = {"A", "Select", "B", "Back"};
        draw_hint_bar(c, f, pairs, 2, 524.0f);
    }
}

// ---- game screen (mockups 02 / 02b / 03 / 03b: one layout, state-driven) ----
constexpr const char* kGameHints[6] = {"A", "Play", "M", "Manage", "B", "Back"};

inline void draw_primary_button(ui::Canvas& c, const Font& f, const GameVM& g) {
    using namespace ui::theme;
    const float y = 384.0f;
    const Primary st = primary_state(g.primary);
    const bool focus = g.primary_focus;
    const char* label = primary_label(g.primary, focus);
    if (st == Primary::installing) {
        c.fill_round_rect(rect(64, y, 256, 80), radiusControl, ui::theme::controlDark);
        if (g.install_pct > 0)
            c.fill_round_rect(rect(64, y, 256 * g.install_pct, 80), radiusControl, ui::theme::accent);
        // mockup 03: the progress label is centred in the button, like the other states
        button_label(c, f, Face::Semibold, 26, rect(64, y, 256, 80), label, ui::theme::text2);
    } else {
        const bool green = st == Primary::play || st == Primary::install;
        c.fill_round_rect(rect(64, y, 256, 80), radiusControl, green ? ui::theme::accent : ui::theme::controlDark);
        button_label(c, f, Face::Semibold, 30, rect(64, y, 256, 80), label,
                     green ? ui::theme::onAccent : ui::theme::text2);
    }
    if (focus || st == Primary::deleting)
        focus_ring(c, rect(58, y - 6, 268, 92), radiusControl + ringRadiusDelta, ringWidth);
}

// The focus ring highlights the button the same icon id main.cpp's A handler
// activates: draw order left to right, i == 0 is the primary button (this
// file's own comments carry the mockup's slot numbers; the ring lives on the
// button, not the slot).
inline void draw_icon_buttons(ui::Canvas& c, const GameVM& g, bool installed) {
    using namespace ui::theme;
    const float y = 384.0f;
    // installed: trash, cloud, cog at 336/432/528; not installed: cloud, cog at 336/432 (no gap)
    const float bx[3] = {336, 432, 528};
    const int n = installed ? 3 : 2;
    for (int i = 0; i < n; ++i) {
        c.fill_round_rect(rect(bx[i], y, 80, 80), radiusControl, ui::theme::controlDark);
        if (installed ? i == 0 : false) ui::glyphs::trash(c, rect(bx[i] + 28, y + 28, 24, 24), kIconLight);
        else if (installed ? i == 1 : i == 0) ui::glyphs::cloud(c, rect(bx[i] + 26, y + 30, 28, 20), kIconLight);
        else ui::glyphs::cog(c, rect(bx[i] + 23, y + 23, 34, 34), kIconLight);
        if (g.focus == 1 + i)                     // ring: icon id 1..3 = the i-th button
            focus_ring(c, rect(bx[i] - 6, y - 6, 92, 92), radiusControl + ringRadiusDelta, ringWidth);
    }
}

inline void draw_kv(ui::Canvas& c, const Font& f, float kx, float vx, float y, const char* k, const char* v) {
    text(c, f, Face::Regular, 24, kx, y, k, ui::theme::text2);
    // item 7: the value ellipsizes to the 256-wide value column (mockup
    // right-aligns values), so a long value can never collide with its label
    const std::string s = f.ellipsize(Face::Regular, 24, v, 256.0f);
    const float w = f.measure(Face::Regular, 24, s);
    text(c, f, Face::Regular, 24, vx + 256.0f - w, y, s.c_str(), ui::theme::textDim);
}

// item 17: draw_hints = false suppresses the base hint bar (see draw_library).
inline void draw_game(ui::Canvas& c, const Font& f, const GameVM& g, bool draw_hints = true) {
    using namespace ui::theme;
    c.fill(ui::theme::page);   // item 3: full clear first
    // hero (item 13): the wide library_hero.jpg cover-fit across 1920x520
    // under the existing veil; the mockup's blue wash while loading
    // (fixtures), grey (item 12) when no_cover.
    if (g.has_hero && g.hero.px && g.hero.w > 0 && g.hero.h > 0) {
        c.draw_image(g.hero, rect(0, 0, 1920, 520), ui::Fit::Cover);
    } else if (g.no_cover) {
        for (int y = 0; y < 520; ++y)
            c.fill_rect(0, float(y), 1920, 1, grad3(kNoCoverTop, kNoCoverTop, kNoCoverBot, float(y) / 519.0f));
    } else {
        // mockup wash: 3-stop gradient over the hero, motif shapes at partial
        // opacity (ring 0.35, disc 0.22, block 0.10), veil + fade come last
        for (int y = 0; y < 520; ++y) {
            const float v = float(y) / 520.0f;
            for (int x = 0; x < 1920; ++x) {
                const float u = float(x) / 1920.0f;
                c.fill_rect(float(x), float(y), 1, 1, grad3(kHeroTop, kHeroMid, kHeroBot, (u + v) * 0.5f));
            }
        }
        c.stroke_circle(1180, 250, 210, 26, kHeroBot, Align::Center, 89);
        c.fill_circle(1180, 250, 120, kHeroBot, 56);  // mockup hero-disc: #0A1018 at 0.22
        c.fill_rect_alpha(240.0f, 40.0f, 520.0f, 440.0f, kHeroBot, 26);    // block at 0.10
    }
    // veil + fade into the page (unchanged layer model)
    c.fill_rect_alpha(0, 0, 1920, 520, ui::theme::page, 158);
    for (int y = 370; y < 520; ++y)
        c.fill_rect_alpha(0, float(y), 1920, 1, ui::theme::page, uint8_t(255.0f * (y - 370) / 150.0f));

    text(c, f, Face::Regular, 22, 64, 50.5f, "Library", ui::theme::textDim);
    // item D4: a long title ellipsizes (with U+2026) instead of running off the page
    const std::string hero_title = f.ellipsize(Face::Semibold, 56, g.title, 1500.0f);
    text(c, f, Face::Semibold, 56, 64, 230, hero_title.c_str(), ui::theme::text, -0.4f);
    // item D10: a favorite game says so beside its title
    if (g.favorite) {
        const float tw = f.measure(Face::Semibold, 56, hero_title);
        ui::glyphs::star(c, rect(64.0f + tw + 20.0f, 196.0f, 36.0f, 36.0f), ui::theme::accentSoft);
    }
    text(c, f, Face::Regular, 24, 64, 306, g.subtitle, ui::theme::textDim);

    draw_primary_button(c, f, g);
    draw_icon_buttons(c, g, g.primary.installed);

    // three equal info cards
    const float px[3] = {64, 700, 1336};
    for (int i = 0; i < 3; ++i) c.fill_round_rect(rect(px[i], 560, 520, 408), radiusCard, ui::theme::panel);

    // card 1: your progress
    text(c, f, Face::Semibold, 26, 96, 592, "Your progress", ui::theme::text);
    draw_kv(c, f, 96, 296, 664, "Time played", g.time_played);
    c.fill_rect(96, 708, 456, 1, ui::theme::controlDark);
    draw_kv(c, f, 96, 296, 724, "Achievements", g.achievements);
    c.fill_rect(96, 768, 456, 1, ui::theme::controlDark);
    draw_kv(c, f, 96, 296, 784, "Last played", g.last_played);
    c.fill_round_rect(rect(96, 840, 456, 8), 4, ui::theme::track);
    if (g.progress_pct > 0) c.fill_round_rect(rect(96, 840, 456 * g.progress_pct, 8), 4, ui::theme::accent);
    if (g.achievement && *g.achievement) {
        c.fill_round_rect(rect(96, 872, 64, 64), radiusControl, ui::theme::iconTile);
        text(c, f, Face::Regular, 22, 176, 878, g.achievement, ui::theme::text);
        text(c, f, Face::Regular, 19, 176, 907, g.achievement_sub, ui::theme::textDim);
    }

    // card 2: install
    text(c, f, Face::Semibold, 26, 732, 592, "Install", ui::theme::text);
    text(c, f, Face::Regular, 24, 732, 664, "Status", ui::theme::text2);
    text(c, f, Face::Regular, 24, 932 + 256.0f - f.measure(Face::Regular, 24, g.install_status, std::strlen(g.install_status)),
         664, g.install_status, g.install_ok ? ui::theme::accentSoft : ui::theme::textDim);
    c.fill_rect(732, 708, 456, 1, ui::theme::controlDark);
    draw_kv(c, f, 732, 932, 724, "Game size", g.game_size);
    c.fill_rect(732, 768, 456, 1, ui::theme::controlDark);
    draw_kv(c, f, 732, 932, 784, "Location", g.location);
    c.fill_rect(732, 828, 456, 1, ui::theme::controlDark);
    draw_kv(c, f, 732, 932, 844, "Last updated", g.last_updated);
    c.fill_rect(732, 888, 456, 1, ui::theme::controlDark);
    draw_kv(c, f, 732, 932, 904, "Cloud saves", g.cloud_saves);

    // card 3: about this game
    text(c, f, Face::Semibold, 26, 1368, 592, "About this game", ui::theme::text);
    // mockup 02 sets lineHeight 36 on this node; its first cap top lands 3px below the
    // natural-line-height conversion, so use the measured offset.
    f.draw_wrapped_lh(c, Face::Regular, 22, 1368, baseline_of(f, Face::Regular, 22, 664) + 3.0f, 456,
                      g.about, 0xFFC8C8C8, 36.0f, 3);
    draw_kv(c, f, 1368, 1568, 848, "Players", g.players);
    c.fill_rect(1368, 892, 456, 1, ui::theme::controlDark);
    draw_kv(c, f, 1368, 1568, 908, "Language", g.language);

    // capsule art right (item 13): the TALL library capsule (library_600x900,
    // the same portrait art the tile uses) in the right-hand box; the mockup's
    // blue wash while loading (fixtures), grey (item 12) when no_cover.
    if (g.has_art && g.art.px && g.art.w > 0 && g.art.h > 0) {
        c.draw_image(g.art, rect(1632, 72, 224, 336), ui::Fit::Cover, radiusTile);
    } else if (g.no_cover) {
        for (int yy = 0; yy < 336; ++yy)
            c.fill_rect(1632, 72.0f + yy, 224, 1, grad3(kNoCoverTop, kNoCoverTop, kNoCoverBot, yy / 335.0f));
    } else {
        for (int yy = 0; yy < 336; ++yy)
            c.fill_rect(1632, 72.0f + yy, 224, 1, grad3(g.art_top, g.art_mid, g.art_bottom, yy / 335.0f));
    }

    // ui::theme::hint bar: the A ui::theme::hint follows the primary state
    const char* pairs[6] = {"A", primary_hint(g.primary, g.primary_focus), "M", "Manage", "B", "Back"};
    if (draw_hints) draw_hint_bar(c, f, pairs, 3, 524.0f);   // review fix 5: same x as the library
}

// One transient note line over the dimmed game page (refusals, cloud warnings,
// launch failures): mockup 03 has no dedicated slot, so it takes the Status row
// of the Install card while it is up. main.cpp passes "" when there is none.
inline void draw_game_note(ui::Canvas& c, const Font& f, const GameVM& /*g*/, const char* note) {
    if (!note || !*note) return;
    using namespace ui::theme;
    c.fill_round_rect(rect(700, 560, 520, 56), radiusControl, ui::theme::controlDark);
    const float w = f.measure(Face::Regular, 22, note, std::strlen(note));
    text(c, f, Face::Regular, 22, 960 - w * 0.5f, 575.5f, note, ui::theme::text2);
}

// ---- sign-in screen (mockup 04) ----
constexpr const char* kSignHints[4] = {"A", "Refresh code", "B", "Quit"};

inline void draw_signin(ui::Canvas& c, const Font& f, const SignVM& s) {
    using namespace ui::theme;
    c.fill(ui::theme::page);
    text_c(c, f, Face::Semibold, 56, 956.5f, 264, s.title, ui::theme::text, -0.4f);
    // QR card: white, 29x29 modules at 10px, origin (467,415)
    c.fill_round_rect(rect(432, 380, 360, 360), radiusCard, 0xFFFFFFFF);
    for (int i = 0; i < s.qr_count; ++i)
        c.fill_rect(467.0f + s.qr[i].col * 10.0f, 415.0f + s.qr[i].row * 10.0f, s.qr[i].w * 10.0f, 10.0f,
                    0xFF000000);
    // status card. Item 18: the status wraps to the card's inner width
    // (648 - 2x32 padding = 584) so a long line can never overflow the card;
    // the block stays vertically centred on the card's text slot.
    c.fill_round_rect(rect(840, 380, 648, 360), radiusCard, ui::theme::panel);
    {
        const float inner_w = 648.0f - 64.0f;
        const std::vector<std::string> lines = f.wrap(Face::Semibold, 32, s.status, inner_w);
        const float lh = f.line_height(Face::Semibold, 32);
        const float block_h = float(lines.size()) * lh;
        float ty = 464.0f - block_h * 0.5f + (lh - 32.0f) * 0.5f;   // centre the block on the single-line slot
        for (const std::string& ln : lines) {
            text_c(c, f, Face::Semibold, 32, 1164, ty, ln.c_str(), ui::theme::text);
            ty += lh;
        }
    }
    text(c, f, Face::Regular, 22, 953, 510, s.timer, ui::theme::textDim);
    c.fill_round_rect(rect(981, 576, 366, 80), radiusControl, ui::theme::accent);
    // item 20: centre the label by its whole-string ink box (both axes) —
    // the first-glyph ink_box left the label ~67px right of centre.
    button_label(c, f, Face::Semibold, 26, rect(981, 576, 366, 80), "REFRESH QR",
                 ui::theme::onAccent);
    if (s.refresh_focused)
        focus_ring(c, rect(975, 570, 378, 92), radiusControl + ringRadiusDelta, ringWidth);
    // user pill top-left, clock top-right
    c.fill_circle(92, 100, 28, 0xFF2E2E2E);
    if (s.avatar_letter && *s.avatar_letter) text_c(c, f, Face::Semibold, 22, 92, 86.5f, s.avatar_letter, ui::theme::textDim);
    text(c, f, Face::Regular, 24, 136, 85.5f, s.persona, ui::theme::textDim);
    if (s.clock && *s.clock) text_r(c, f, Face::Semibold, 36, 1848, 78, s.clock, ui::theme::text);
    // mockup 04: the prompt pair sits left of centre (A disc at 823.5)
    draw_hint_bar(c, f, kSignHints, 2, 823.5f);
}

// ---- sheets over the dimmed page (mockups 05 / 07) ----
// (draw_scrim lives above, with the popup draw functions)

// item 8: the ONE confirmation sheet component, shared by Delete and Log out —
// title, body, note, two real buttons with focus, over the dimmed page.
// Item 14: focus_yes = false draws the ring on the CANCEL button (the
// default) — the destructive button is never focused first.
// Item D3: the body and note WRAP inside the sheet's 704px inner width (the
// sheet is 800 wide with 48px padding) and the sheet's height follows the
// wrapped content, so a long line never runs past the right edge and the
// buttons sit directly under the text instead of after a fixed gap.
// The layout is pure (confirm_sheet_layout) so tests can pin it without pixels.
struct ConfirmSheetLayout {
    float x = 560.0f, w = 800.0f, pad = 48.0f;
    float top = 0.0f, h = 0.0f;          // the sheet rect
    float title_y = 0.0f, body_y = 0.0f; // top-anchored text rows
    float buttons_y = 0.0f;              // the two buttons' top
    std::vector<std::string> body_lines, note_lines;
};

inline ConfirmSheetLayout confirm_sheet_layout(const Font& f, const char* body, const char* note) {
    ConfirmSheetLayout L;
    const float inner_w = L.w - L.pad * 2.0f;      // 704
    const float title_top = 40.0f;
    const float body_top = title_top + 46.0f;      // under the 36px title
    const float lh_body = f.line_height(Face::Regular, 24);
    const float lh_note = f.line_height(Face::Regular, 22);
    L.body_lines = f.wrap(Face::Regular, 24, body ? body : "", inner_w);
    if (note && *note) L.note_lines = f.wrap(Face::Regular, 22, note, inner_w);
    const float note_top = body_top + float(L.body_lines.size()) * lh_body + 10.0f;
    const float buttons_top = note_top + (L.note_lines.empty() ? 0.0f : float(L.note_lines.size()) * lh_note) + 30.0f;
    L.h = buttons_top + 64.0f + L.pad;
    L.top = (1080.0f - L.h) * 0.5f;
    L.title_y = L.top + title_top;
    L.body_y = L.top + body_top;
    L.buttons_y = L.top + buttons_top;
    return L;
}

// Item D2: what the hint bar offers for the CURRENT focus. A names the
// focused button (Cancel while Cancel wears the ring; Delete / Log out only
// once the destructive button does), B always cancels. Pure so tests pin it.
inline void confirm_hint_labels(bool focus_yes, const char* yes_label, const char* no_label,
                                const char* out[4]) {
    out[0] = "A";
    out[1] = focus_yes ? yes_label : no_label;
    out[2] = "B";
    out[3] = no_label;
}

inline void draw_confirm_sheet(ui::Canvas& c, const Font& f, const char* title, const char* body,
                               const char* note, const char* no_label, const char* yes_label, bool focus_yes) {
    using namespace ui::theme;
    draw_scrim(c);
    const ConfirmSheetLayout L = confirm_sheet_layout(f, body, note);
    const float lh_body = f.line_height(Face::Regular, 24);
    const float lh_note = f.line_height(Face::Regular, 22);
    c.fill_round_rect(rect(L.x, L.top, L.w, L.h), radiusGuide, panel);
    text(c, f, Face::Semibold, 36, L.x + L.pad, L.title_y, title, ui::theme::text);
    float ty = L.body_y;
    for (const std::string& ln : L.body_lines) { text(c, f, Face::Regular, 24, L.x + L.pad, ty, ln.c_str(), ui::theme::textDim); ty += lh_body; }
    ty += 10.0f;
    for (const std::string& ln : L.note_lines) { text(c, f, Face::Regular, 22, L.x + L.pad, ty, ln.c_str(), ui::theme::textMuted); ty += lh_note; }
    const float by = L.buttons_y;
    const Rect no_btn = rect(L.x + L.w - L.pad - 108.0f - 16.0f - 170.0f, by, 170, 64);
    const Rect yes_btn = rect(L.x + L.w - L.pad - 108.0f, by, 108, 64);
    c.fill_round_rect(no_btn, radiusControl, ui::theme::controlDark);
    button_label(c, f, Face::Semibold, 26, no_btn, no_label, ui::theme::text2);
    c.fill_round_rect(yes_btn, radiusControl, ui::theme::accent);
    button_label(c, f, Face::Semibold, 26, yes_btn, yes_label, ui::theme::onAccent);
    if (focus_yes)
        focus_ring(c, rect(yes_btn.x - 6.0f, by - 6.0f, 120, 76), radiusControl + ringRadiusDelta, ringWidth);
    else
        focus_ring(c, rect(no_btn.x - 6.0f, by - 6.0f, 182, 76), radiusControl + ringRadiusDelta, ringWidth);
    // Item D2: the hint bar says what A does for the CURRENT focus — A Cancel
    // while Cancel wears the ring, A Delete / A Log out only once the
    // destructive button does. B always cancels, so it is never offered as
    // the destructive label.
    const char* pairs[4];
    confirm_hint_labels(focus_yes, yes_label, no_label, pairs);
    draw_hint_bar(c, f, pairs, 2, 524.0f);   // review fix 5: same x as the library
}

// item 7: the cloud value shortened to the card's 256px value column
// ("Cloud: synced 2026-11-10 10:15" -> "Synced 10 Nov"). Writes into `buf`
// (>= 32 bytes); pass the result as GameVM::cloud_saves before drawing.
inline void cloud_saves_short(char* buf, size_t n, const char* synced) {
    // steam_cloud.h writes "Cloud: synced YYYY-MM-DD HH:MM" (or similar);
    // keep the short "Synced D Mon" form, anything unknown passes through
    // (draw_kv ellipsizes it to the column width anyway).
    int y = 0, m = 0, d = 0, hh = 0, mm = 0;
    if (synced && (std::sscanf(synced, "Cloud: synced %d-%d-%d %d:%d", &y, &m, &d, &hh, &mm) == 5 ||
                   std::sscanf(synced, "synced %d-%d-%d %d:%d", &y, &m, &d, &hh, &mm) == 5) &&
        m >= 1 && m <= 12) {
        static const char* mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        std::snprintf(buf, n, "Synced %d %s", d, mon[m - 1]);
        return;
    }
    std::snprintf(buf, n, "%s", synced ? synced : "");
}

inline void draw_delete_sheet(ui::Canvas& c, const Font& f, const DeleteVM& d) {
    draw_confirm_sheet(c, f, d.title, d.body, d.note, "Cancel", "Delete", d.focus_delete);
}

// logout confirm (item 8): the same sheet as Delete
inline void draw_logout_sheet(ui::Canvas& c, const Font& f, const LogoutVM& v) {
    draw_confirm_sheet(c, f, v.title, v.body, v.note, "Cancel", "Log out", v.focus_yes);
}

inline void draw_cloud_sheet(ui::Canvas& c, const Font& f, const CloudVM& v) {
    using namespace ui::theme;
    draw_scrim(c);
    c.fill_round_rect(rect(560, 252, 800, 576), radiusCard, ui::theme::panel);
    text(c, f, Face::Semibold, 36, 608, 300, v.title, ui::theme::text);
    text(c, f, Face::Regular, 24, 608, 359, v.synced, ui::theme::textDim);
    for (int i = 0; i < v.slot_count && i < 3; ++i) {
        const float y = 412 + 32.0f * i;
        text(c, f, Face::Regular, 22, 608, y, v.slots[i].name, ui::theme::text2);
        // mockup 07: slot details are right-aligned to the detail column's right edge
        text_r(c, f, Face::Regular, 22, 1312, y, v.slots[i].detail, ui::theme::textDim);
    }
    for (int i = 0; i < v.opt_count && i < 4; ++i) {
        const float y = 532 + 64.0f * i;
        c.fill_round_rect(rect(608, y, 704, 56), radiusControl, v.opts[i].enabled ? ui::theme::controlDark : chip);
        text(c, f, Face::Regular, 24, 632, y + 13.5f, v.opts[i].label, v.opts[i].enabled ? ui::theme::text2 : ui::theme::textMuted);
        if (v.opts[i].value && *v.opts[i].value)
            text_r(c, f, Face::Regular, 22, 1288, y + 14.5f, v.opts[i].value, ui::theme::textDim);
        if (i == v.focus_opt)
            focus_ring(c, rect(602, y - 6, 716, 68), radiusControl + ringRadiusDelta, ringWidth);
    }
    const char* pairs[4] = {"A", "Select", "B", "Close"};
    draw_hint_bar(c, f, pairs, 2, 524.0f);   // review fix 5: same x as the library
}

// item 33: the ACTION SHEET — title (game name), subtitle (size + drive), then
// one FULL-WIDTH vertical option button per entry (radius 8). The focused
// option wears the focus ring; a disabled option draws dim text with its
// reason line under the label (72 high so the reason fits; siblings are 64).
// The caller supplies Cancel as the LAST label and defaults sel to it.
// Layout: 32px padding, title Semibold 32, subtitle 22 text2, 24px gap,
// 64px options with 12px gaps, label inset 24. Hints: A Select / B Back.
inline void draw_action_sheet(ui::Canvas& c, const Font& f, const ActionSheetVM& v,
                              bool draw_hints = true) {
    using namespace ui::theme;
    draw_scrim(c);
    // sheet: 800 wide, centred; height follows the option count
    const float x = 560.0f, w = 800.0f;
    const float pad = 32.0f;
    const float opt_h = 64.0f, opt_gap = 12.0f;
    const float title_top = 40.0f;
    const float subtitle_top = title_top + 44.0f;
    const float head_end = subtitle_top + 30.0f;
    const float first_opt = head_end + 24.0f;        // 24px gap after subtitle
    // disabled options grow to 72 so the reason line fits inside the button
    float opts_h = 0.0f;
    for (int i = 0; i < v.n; ++i) {
        const bool dis = v.reasons && i < v.n && v.reasons[i] && *v.reasons[i];
        opts_h += (dis ? 72.0f : opt_h) + opt_gap;
    }
    if (opts_h > 0) opts_h -= opt_gap;
    const float h = first_opt + opts_h + pad;
    const float top = (1080.0f - h) * 0.5f;          // stays centred as it grows
    c.fill_round_rect(rect(x, top, w, h), radiusGuide, panel);
    text(c, f, Face::Semibold, 32, x + pad, top + title_top, v.title, ui::theme::text);
    if (v.subtitle && *v.subtitle)
        text(c, f, Face::Regular, 22, x + pad, top + subtitle_top, v.subtitle, ui::theme::text2);
    float oy = first_opt;
    for (int i = 0; i < v.n; ++i) {
        const bool dis = v.reasons && i < v.n && v.reasons[i] && *v.reasons[i];
        const float bh = dis ? 72.0f : opt_h;
        const Rect btn = rect(x + pad, top + oy, w - pad * 2, bh);
        c.fill_round_rect(btn, radiusCard, dis ? chip : ui::theme::controlDark);
        text(c, f, Face::Regular, 26, btn.x + 24, btn.y + (dis ? 10.0f : 17.5f), v.labels[i],
             dis ? ui::theme::textMuted : ui::theme::text);
        if (dis)
            text(c, f, Face::Regular, 20, btn.x + 24, btn.y + 42.0f, v.reasons[i], ui::theme::textMuted);
        if (i == v.sel)   // the ring follows focus even on a disabled row (navigation walks them)
            focus_ring(c, rect(btn.x - ringOffset, btn.y - ringOffset, btn.w + ringOffset * 2, btn.h + ringOffset * 2),
                       radiusCard + ringRadiusDelta, ringWidth);
        oy += bh + opt_gap;
    }
    if (draw_hints) {
        const char* pairs[4] = {"A", "Select", "B", "Back"};
        draw_hint_bar(c, f, pairs, 2, 524.0f);
    }
}

// item 33: the PROGRESS SHEET — title, detail, 20px gap, bar row (bar left,
// 'NN%' right-aligned in its own column, never overlapping the bar: the bar
// ends >=16px before the % column), 28px gap, Cancel centred under the bar
// row inside the sheet padding (>=28px bottom). Hints: A Cancel / B Back.
inline void draw_progress_sheet(ui::Canvas& c, const Font& f, const ProgressSheetVM& v,
                                bool draw_hints = true) {
    using namespace ui::theme;
    draw_scrim(c);
    const float x = 560.0f, w = 800.0f, pad = 48.0f;
    const float title_top = 40.0f;
    const float detail_top = title_top + 52.0f;
    const float bar_y = detail_top + 36.0f + 20.0f;  // detail line + 20px gap
    const float bar_h = 12.0f;
    // % column: 96px reserved at the right; the bar ends 16px before it
    const float pct_col_w = 96.0f;
    const float bar_w = w - pad * 2 - pct_col_w - 16.0f;
    const float h = bar_y + bar_h + 28.0f + 64.0f + 28.0f;
    const float y = (1080.0f - h) * 0.5f;
    c.fill_round_rect(rect(x, y, w, h), radiusGuide, panel);
    text(c, f, Face::Semibold, 36, x + pad, y + title_top, v.title, ui::theme::text);
    if (v.detail && *v.detail)
        text(c, f, Face::Regular, 24, x + pad, y + detail_top, v.detail, ui::theme::textDim);
    int p = v.pct < 0 ? 0 : v.pct > 100 ? 100 : v.pct;
    c.fill_round_rect(rect(x + pad, y + bar_y, bar_w, bar_h), bar_h * 0.5f, ui::theme::track);
    if (p > 0)
        c.fill_round_rect(rect(x + pad, y + bar_y, bar_w * float(p) / 100.0f, bar_h), bar_h * 0.5f, ui::theme::accent);
    // 'NN%' in its own column, vertically centred on the bar
    char b[16];
    std::snprintf(b, sizeof b, "%d%%", p);
    const float pct_w = f.measure(Face::Regular, 22, b);
    const float pct_x = x + w - pad - pct_col_w + (pct_col_w - pct_w) * 0.5f;
    text(c, f, Face::Regular, 22, pct_x, y + bar_y + bar_h * 0.5f - 13.0f, b, ui::theme::text2);
    // Cancel centred under the bar row, >=28px bottom padding
    const Rect cancel = rect(x + (w - 170.0f) * 0.5f, y + bar_y + bar_h + 28.0f, 170.0f, 64.0f);
    c.fill_round_rect(cancel, radiusControl, ui::theme::controlDark);
    button_label(c, f, Face::Semibold, 26, cancel, "Cancel", ui::theme::text2);
    if (v.cancel_focus)
        focus_ring(c, rect(cancel.x - ringOffset, cancel.y - ringOffset, cancel.w + ringOffset * 2, cancel.h + ringOffset * 2),
                   radiusControl + ringRadiusDelta, ringWidth);
    if (draw_hints) {
        const char* pairs[4] = {"A", "Cancel", "B", "Back"};
        draw_hint_bar(c, f, pairs, 2, 524.0f);
    }
}

}  // namespace ui_launcher
