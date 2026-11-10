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
#include "ui_theme.h"

namespace ui_launcher {

using ui::Align;
using ui::Face;
using ui::Font;
using ui::Rect;
using ui::rect;

// ---- shared geometry (1920x1080, from the mockups) ----
constexpr float kHintDiscPx = 26.0f;    // prompt disc diameter in the theme::hint bar
constexpr float kHintY      = 1007.0f;  // prompt disc top row
constexpr float kTitleX     = 524.0f, kTitleY = 73.5f;   // screen title, top-left
constexpr float kGridX      = 524.0f, kGridY = 264.0f;   // first tile's top-left
constexpr float kTileW      = 168.0f, kTileH = 252.0f;
constexpr float kTilePitchX = 192.0f, kTilePitchY = 276.0f;
constexpr int   kGridCols   = 7;
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

// ---- pure grid geometry (unit-tested with assert) ----
inline Rect grid_tile(int index, int top_row) {
    const int row = index / kGridCols, col = index % kGridCols;
    return rect(kGridX + col * kTilePitchX, kGridY + (row - top_row) * kTilePitchY, kTileW, kTileH);
}
// Rows that can be visible for a scroll offset; one row of slack on each side.
inline void grid_visible(int tile_count, int top_row, int& lo, int& hi) {
    lo = top_row > 0 ? (top_row - 1) * kGridCols : 0;
    hi = tile_count < (top_row + 3) * kGridCols ? tile_count : (top_row + 3) * kGridCols;
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
// and turns into CANCEL when it has focus (A then cancels; never a second button).
inline const char* primary_label(const PrimaryVM& vm, bool focus) {
    switch (primary_state(vm)) {
        case Primary::play:             return "PLAY";
        case Primary::install:          return "INSTALL";
        case Primary::installing:       return focus ? "CANCEL" : "INSTALLING 62%";
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
    int active = 0;   // 0 All games, 1 Installed, 2 Download queue, 3 Storage management
};

struct TileVM {
    const char* name = "";
    uint32_t art_top = 0xFF1B2A4A, art_mid = 0xFF3E6FA8, art_bot = 0xFF0B1220;
    bool installed = false;
    ui::Image art{};   // real cover art (main.cpp); empty falls back to the gradient wash
};

struct LibraryVM {
    const TileVM* tiles = nullptr;
    int tile_count = 0;
    int sel = 0;             // focused tile index
    int top_row = 0;         // first visible grid row
    bool focus_tile = true;
    const char* sort_label = "Sort by recently played";
    const char* count_label = "";   // e.g. "128 games"; empty hides it
    bool loading = false;    // true: count label reads "Loading..."
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

// detail page: one layout for every state; fields shared, placeholders when unknown
struct GameVM {
    const char* title = "Megabonk";
    const char* subtitle = "Single-player  \xC2\xB7  Action, Roguelite  \xC2\xB7  48.2 GB";
    const char* time_played = "Not played yet";
    const char* achievements = "0 / 42";
    const char* last_played = "Never";
    const char* install_status = "Not installed";
    bool install_ok = false;             // status value draws in accentSoft when the game is in
    const char* size_on_disk = "Not installed";
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
    int focus = -1;          // 0 primary button, 1 trash, 2 cloud, 3 cog (ring drawn)
    bool primary_focus = false;
    float install_pct = 0.0f;   // 0..1 while installing
    bool has_art = false;
    ui::Image art{};   // header image (main.cpp); empty falls back to the gradient wash
    uint32_t art_top = 0xFF1A3F6B, art_mid = 0xFF14304F, art_bottom = 0xFF0A0E14;
};

// sheets over the dimmed game page (mockups 05 / 07)
struct DeleteVM {
    const char* title = "Delete Megabonk?";
    const char* body = "Frees 48.2 GB on this console. Saves are kept.";
    const char* note = "You can install it again from your library at any time.";
    bool focus_delete = true;
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

// ---- drawing ----------------------------------------------------------------

// The mockup's focus ring is a 3px white band just inside the focus rect (measured:
// rect edge 138 → band 138..141). Stroke centred on the rect inset by half the width.
inline void focus_ring(ui::Canvas& c, const ui::Rect& r, float radius, float width) {
    const float h = width * 0.5f;
    c.stroke_round_rect(rect(r.x + h, r.y + h, r.w - width, r.h - width), radius - h, width,
                        ui::theme::focus, Align::Center);
}

// The bottom hint bar: A/B/X/Y/Menu prompts on the page background.
// pairs = {"A","Select", "B","Back", "X","Sort", "Y","Filter", "M","Menu"}; n = number of pairs.
// A is a filled disc; B/X/Y are outlined; M is the hamburger.
inline float draw_hint_bar(ui::Canvas& c, const Font& f, const char* const* pairs, int n, float x0 = ui::theme::margin) {
    const float px = ui::theme::hintText.px;
    float x = x0;
    for (int i = 0; i + 1 < n * 2; i += 2) {
        const char key = pairs[i][0];
        const char* label = pairs[i + 1];
        const float cy = kHintY + kHintDiscPx * 0.5f;
        if (key == 'M') {
            // mockup: the hamburger sits in the same 28px disc as the outlined prompts
            ui::glyphs::menu(c, x + kHintDiscPx * 0.5f, cy, kHintDiscPx + 2.0f, ui::theme::glyph, 0xFFC8C8C8);
        } else {
            const bool filled = key == 'A';
            const uint32_t disc = filled ? ui::theme::glyph : 0;
            const uint32_t letter = filled ? ui::theme::glyphLabel : 0xFFC8C8C8;
            if (filled) {
                ui::glyphs::face_button(c, x + kHintDiscPx * 0.5f, cy, kHintDiscPx, key, f, disc, letter);
            } else {
                // mockup: 2px stroke centred on r=13 → 28px outer ring
                c.stroke_circle(x + kHintDiscPx * 0.5f, cy, kHintDiscPx * 0.5f, 2.0f,
                                ui::theme::glyph, Align::Center);
                float ix0, iy0, ix1, iy1;   // centre the visible letter on the ring
                if (f.ink_box(Face::Bold, 15.0f, std::string(1, key), ix0, iy0, ix1, iy1))
                    f.draw(c, Face::Bold, 15.0f, x + kHintDiscPx * 0.5f - (ix0 + ix1) * 0.5f, cy - (iy0 + iy1) * 0.5f,
                           std::string(1, key), letter);
            }
        }
        text(c, f, Face::Regular, px, x + kHintDiscPx + 12.0f, kHintY + 1.0f, label, ui::theme::hint);
        x += kHintDiscPx + 12.0f + f.measure(Face::Regular, px, label) + 40.0f;
    }
    return x;
}

// ---- sidebar (shared by library and download-queue screens) ----
inline void draw_sidebar(ui::Canvas& c, const Font& f, const SidebarVM& sb) {
    using namespace ui::theme;
    c.fill_round_rect(rect(kSidebarX, kSidebarY, kSidebarW, kSidebarH), radiusPanel, ui::theme::panel);
    // avatar: circle + head + shoulders
    c.fill_circle(kSidebarX + 60, 100, 28, kAvatarBg);
    c.fill_circle(kSidebarX + 60, 94, 10, 0xFFC8C8C8);
    c.fill_round_rect(rect(kSidebarX + 44, 106, 32, 14), 7, 0xFFC8C8C8);
    text(c, f, Face::Regular, 24, 136, 85.5f, sb.persona, ui::theme::textDim);
    // search pill
    c.fill_round_rect(rect(64, 160, 347, 56), radiusPill, ui::theme::control);
    text(c, f, Face::Regular, 22, 88, 174.5f, "Search", ui::theme::textDim);
    c.fill_rect(64, 248, 347, 1, ui::theme::separator);
    // nav rows
    struct Row { const char* label; int count; };
    const Row rows[4] = {{"All games", sb.all_count}, {"Installed", sb.installed_count},
                         {"Download queue", sb.queue_count}, {"Storage management", -1}};
    float y = 264.0f;
    for (int i = 0; i < 4; ++i) {
        const bool on = i == sb.active;
        if (on) {
            c.fill_round_rect(rect(40, y, 395, 64), radiusControl, ui::theme::control);
            c.fill_rect(429, y, 6, 64, ui::theme::accent);
        }
        text(c, f, Face::Regular, 24, 64, y + 17.5f, rows[i].label, on ? ui::theme::text : ui::theme::text2);
        if (rows[i].count >= 0) {
            char n[16];
            std::snprintf(n, sizeof n, "%d", rows[i].count);
            text_r(c, f, Face::Regular, 22, 411, y + 18.5f, n, ui::theme::textDim);
        }
        y += 64.0f;
    }
    c.fill_rect(64, 920, 347, 1, ui::theme::separator);
    // storage management: free GB, % used, donut (5px inside stroke, arc from 12 o'clock)
    char b[48];
    std::snprintf(b, sizeof b, "%d.%d GB free", sb.free_gb_x10 / 10, sb.free_gb_x10 % 10);
    text(c, f, Face::Regular, 22, 64, 952.0f, b, ui::theme::text2);
    std::snprintf(b, sizeof b, "%d%% used", sb.used_pct);
    text(c, f, Face::Regular, 20, 64, 984.0f, b, ui::theme::textDim);
    // donut: track ring r=26 stroke 5; the used arc is centred on 6 o'clock (mockup 01/06)
    c.fill_donut(383, 981, 26, 5, -ui::kPi * 0.5f, ui::kTau, ui::theme::track);
    if (sb.used_pct > 0) {
        const float sweep = ui::kTau * sb.used_pct / 100.0f;
        c.fill_donut(383, 981, 26, 5, ui::kPi * 0.5f - sweep * 0.5f, sweep, ui::theme::accent);
    }
    std::snprintf(b, sizeof b, "%d%%", sb.used_pct);
    text_c(c, f, Face::Regular, 16, 383, 970.5f, b, ui::theme::textDim);
}

// ---- library screen (mockup 01) ----
constexpr const char* kLibraryHints[10] = {"A", "Select", "B", "Back", "X", "Sort", "Y", "Filter", "M", "Menu"};

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

inline void draw_tile(ui::Canvas& c, const Font& f, const TileVM& t, const ui::Rect& g, bool focus) {
    using namespace ui::theme;
    if (t.art.px && t.art.w > 0 && t.art.h > 0) {
        c.draw_image(t.art, g, ui::Fit::Cover, radiusTile);
    } else {
        // mockup art is a 3-stop diagonal from the tile's top-left to bottom-right
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
    // label scrim band: transparent until mid, 0.78 black at the bottom (mockup band)
    for (int yy = int(g.h) / 2; yy < int(g.h); ++yy) {
        const float k = (yy - g.h * 0.5f) / (g.h * 0.5f);
        c.fill_rect_alpha(g.x, g.y + yy, g.w, 1, scrim, uint8_t(0.78f * 255.0f * k));
    }
    text(c, f, Face::Regular, 20, g.x + 12, g.y + 216, t.name, ui::theme::text);
    if (t.installed) {
        c.fill_round_rect(rect(g.x + 12, g.y + 12, 94, 26), radiusBadge, ui::theme::accent);
        text_c(c, f, Face::Bold, 15, g.x + 59, g.y + 16, "INSTALLED", ui::theme::onAccent);
    }
    if (focus)
        focus_ring(c, rect(g.x - ringOffset, g.y - ringOffset, g.w + ringOffset * 2, g.h + ringOffset * 2),
                   radiusTile + ringRadiusDelta, ringWidth);
}

inline void draw_library(ui::Canvas& c, const Font& f, const SidebarVM& sb, const LibraryVM& lib) {
    using namespace ui::theme;
    c.fill(ui::theme::page);
    draw_sidebar(c, f, sb);
    // title + sort bar
    text(c, f, Face::Semibold, 44, kTitleX, kTitleY, "Library", ui::theme::text, -0.2f);
    c.fill_round_rect(rect(524, 168, 430, 60), radiusControl, ui::theme::control);
    text(c, f, Face::Regular, 24, 548, 183.5f, lib.sort_label, ui::theme::text2);
    const char* count = lib.loading ? "Loading..." : lib.count_label;
    if (count && *count) text(c, f, Face::Regular, 22, 1556, 184.5f, count, ui::theme::textDim);
    c.fill_round_rect(rect(1692, 168, 152, 60), radiusControl, ui::theme::control);
    text_c(c, f, Face::Regular, 24, 1765, 183.5f, "Filters", ui::theme::text2);
    // grid
    int lo, hi;
    grid_visible(lib.tile_count, lib.top_row, lo, hi);
    c.set_clip(rect(0, 0, 1920, 988));   // rows scroll out above the hint bar, as in the mockup
    for (int i = lo; i < hi; ++i) {
        const Rect g = grid_tile(i, lib.top_row);
        if (g.y > 1040 || g.y + g.h < 0) continue;
        draw_tile(c, f, lib.tiles[i], g, lib.focus_tile && i == lib.sel);
    }
    c.reset_clip();
    draw_hint_bar(c, f, kLibraryHints, 5, 524.0f);
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
    text(c, f, Face::Regular, 22, 604, y + top, r.name, ui::theme::text);
    text(c, f, Face::Regular, 20, 604, y + top + 31.0f, r.status, ui::theme::textDim);
    if (r.busy) {
        c.fill_round_rect(rect(604, y + 80, 920, 8), 4, ui::theme::track);
        if (r.pct > 0) c.fill_round_rect(rect(604, y + 80, 920 * r.pct, 8), 4, ui::theme::accent);
    }
    if (update) {
        c.fill_round_rect(rect(1668, y + 28, 160, 48), radiusControl, ui::theme::controlDark);
        text_c(c, f, Face::Semibold, 20, 1748, y + 40, "UPDATE NOW", ui::theme::text2);
    } else {
        c.fill_round_rect(rect(1700, y + 28, 128, 48), radiusControl, ui::theme::controlDark);
        text_c(c, f, Face::Semibold, 20, 1764, y + 40, r.paused ? "RESUME" : "CANCEL", ui::theme::text2);
        if (r.busy) {   // the downloading row also shows PAUSE (mockup q0)
            c.fill_round_rect(rect(1556, y + 28, 128, 48), radiusControl, ui::theme::controlDark);
            text_c(c, f, Face::Semibold, 20, 1620, y + 40, "PAUSE", ui::theme::text2);
        }
    }
}

inline void draw_download_queue(ui::Canvas& c, const Font& f, const SidebarVM& sb, const QueueVM& q) {
    using namespace ui::theme;
    c.fill(ui::theme::page);
    draw_sidebar(c, f, sb);
    text(c, f, Face::Semibold, 44, kTitleX, kTitleY, "Download queue", ui::theme::text, -0.2f);
    // auto-update pill (ui::theme::focus ring when q.focus == 0)
    c.fill_round_rect(rect(524, 144, 1320, 96), radiusCard, ui::theme::panel);
    text(c, f, Face::Semibold, 26, 556, 162, "Auto-update games", ui::theme::text);
    text(c, f, Face::Regular, 20, 556, 197, q.update_note, ui::theme::textDim);
    text_r(c, f, Face::Regular, 22, 1744, 178.5f, q.auto_update ? "On" : "Off", ui::theme::textDim);
    c.fill_round_rect(rect(1756, 176, 56, 32), 16, q.auto_update ? ui::theme::accent : ui::theme::controlDark);
    c.fill_circle(q.auto_update ? 1796 : 1768, 192, 12, ui::theme::onAccent);
    if (q.focus == 0)
        focus_ring(c, rect(518, 138, 1332, 108), radiusCard + ringRadiusDelta, ringWidth);
    // queue section
    text(c, f, Face::Semibold, 26, 524, 272, "Queue", ui::theme::text);
    char b[32];
    std::snprintf(b, sizeof b, "%d items", q.row_count);
    text_r(c, f, Face::Regular, 22, 1844, 274, b, ui::theme::textDim);
    for (int i = 0; i < q.row_count; ++i) draw_queue_card(c, f, q.rows[i], 320 + 112.0f * i, false);
    // updates section
    text(c, f, Face::Semibold, 26, 524, 680, "Updates", ui::theme::text);
    std::snprintf(b, sizeof b, "%d pending", q.update_count);
    text_r(c, f, Face::Regular, 22, 1844, 682, b, ui::theme::textDim);
    for (int i = 0; i < q.update_count; ++i) draw_queue_card(c, f, q.updates[i], 728 + 112.0f * i, true);
    draw_hint_bar(c, f, kQueueHints, 3, 524.0f);
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
        text_c(c, f, Face::Semibold, 26, 192, y + 24.5f, label, ui::theme::text2);
    } else {
        const bool green = st == Primary::play || st == Primary::install;
        c.fill_round_rect(rect(64, y, 256, 80), radiusControl, green ? ui::theme::accent : ui::theme::controlDark);
        text_c(c, f, Face::Semibold, 30, 192, y + 22, label, green ? ui::theme::onAccent : ui::theme::text2);
    }
    if (focus || st == Primary::deleting)
        focus_ring(c, rect(58, y - 6, 268, 92), radiusControl + ringRadiusDelta, ringWidth);
}

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
        const int slot = installed ? i : i + 1;   // ui::theme::focus slots: 1 trash, 2 cloud, 3 cog
        if (g.focus == slot)
            focus_ring(c, rect(bx[i] - 6, y - 6, 92, 92), radiusControl + ringRadiusDelta, ringWidth);
    }
}

inline void draw_kv(ui::Canvas& c, const Font& f, float kx, float vx, float y, const char* k, const char* v) {
    text(c, f, Face::Regular, 24, kx, y, k, ui::theme::text2);
    // the mockup right-aligns values in their 256-wide column
    const float w = f.measure(Face::Regular, 24, v);
    text(c, f, Face::Regular, 24, vx + 256.0f - w, y, v, ui::theme::textDim);
}

inline void draw_game(ui::Canvas& c, const Font& f, const GameVM& g) {
    using namespace ui::theme;
    // hero: art gradient, ring+disc+block motif, dim veil, fade into the ui::theme::page.
    // Mockup layer model (measured): the 3-stop gradient maps over the whole 1920x520
    // hero (k = (x/1920 + y/520) / 2); the motif shapes sit on the art at partial
    // opacity (ring 0.35, disc 0.22, block 0.10); the veil and fade come last.
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
    c.fill_rect_alpha(0, 0, 1920, 520, ui::theme::page, 158);
    for (int y = 370; y < 520; ++y)
        c.fill_rect_alpha(0, float(y), 1920, 1, ui::theme::page, uint8_t(255.0f * (y - 370) / 150.0f));

    text(c, f, Face::Regular, 22, 64, 50.5f, "Library", ui::theme::textDim);
    text(c, f, Face::Semibold, 56, 64, 230, g.title, ui::theme::text, -0.4f);
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
    draw_kv(c, f, 732, 932, 724, "Size on disk", g.size_on_disk);
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

    // capsule art right: the store header image when it is in, else the wash
    if (g.has_art && g.art.px && g.art.w > 0 && g.art.h > 0) {
        c.draw_image(g.art, rect(1632, 72, 224, 336), ui::Fit::Cover, radiusTile);
    } else {
        for (int yy = 0; yy < 336; ++yy)
            c.fill_rect(1632, 72.0f + yy, 224, 1, grad3(g.art_top, g.art_mid, g.art_bottom, yy / 335.0f));
        c.stroke_circle(1744, 222, 72, 10, kRingDark, Align::Center);
        c.fill_round_rect(rect(1800, 84, 44, 44), 3, kRingDark);
    }

    // ui::theme::hint bar: the A ui::theme::hint follows the primary state
    const char* pairs[6] = {"A", primary_hint(g.primary, g.primary_focus), "M", "Manage", "B", "Back"};
    draw_hint_bar(c, f, pairs, 3);
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
    // status card
    c.fill_round_rect(rect(840, 380, 648, 360), radiusCard, ui::theme::panel);
    text_c(c, f, Face::Semibold, 32, 1164, 464, s.status, ui::theme::text);
    text(c, f, Face::Regular, 22, 953, 510, s.timer, ui::theme::textDim);
    c.fill_round_rect(rect(981, 576, 366, 80), radiusControl, ui::theme::accent);
    text_c(c, f, Face::Semibold, 26, 1158, 600.5f, "REFRESH QR", ui::theme::onAccent);
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

// ---- sheets over the dimmed game page (mockups 05 / 07) ----
inline void draw_scrim(ui::Canvas& c) { c.fill_rect_alpha(0, 0, 1920, 1080, ui::theme::scrim, ui::theme::scrimAlpha); }

inline void draw_delete_sheet(ui::Canvas& c, const Font& f, const DeleteVM& d) {
    using namespace ui::theme;
    draw_scrim(c);
    c.fill_round_rect(rect(560, 368, 800, 344), radiusCard, ui::theme::panel);
    text(c, f, Face::Semibold, 36, 608, 416, d.title, ui::theme::text);
    text(c, f, Face::Regular, 24, 608, 475, d.body, ui::theme::textDim);
    text(c, f, Face::Regular, 22, 608, 516, d.note, ui::theme::textMuted);
    c.fill_round_rect(rect(1018, 600, 170, 64), radiusControl, ui::theme::controlDark);
    text_c(c, f, Face::Semibold, 26, 1103, 616.5f, "Cancel", ui::theme::text2);
    c.fill_round_rect(rect(1204, 600, 108, 64), radiusControl, ui::theme::accent);
    text_c(c, f, Face::Semibold, 26, 1258, 616.5f, "Delete", ui::theme::onAccent);
    if (d.focus_delete)
        focus_ring(c, rect(1198, 594, 120, 76), radiusControl + ringRadiusDelta, ringWidth);
    const char* pairs[4] = {"A", "Yes, delete", "B", "Cancel"};
    draw_hint_bar(c, f, pairs, 2);
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
    draw_hint_bar(c, f, pairs, 2);
}

}  // namespace ui_launcher
