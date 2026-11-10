// SPDX-License-Identifier: GPL-3.0-or-later
// In-game Xbox-guide overlay as pure functions of a view-model: no Win32, no
// app globals, so guest_overlay.h fills the Model from its atomics and
// tests/ui-guide.cpp renders the same screens natively for RMSE comparison.
//
// Geometry is measured from the approved mockups (.mockups/ingame/figcraft,
// document.json; children are card-relative) at 1920x1080: a floating guide
// card 560x1016 at (80,32) with three tabs (Home / Achievements / Settings),
// an achievement toast pill 352x88 centred at the bottom, and the game frame
// dimmed behind the card (the dim itself is guest_overlay's GPU-side job).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "ui_canvas.h"
#include "ui_font.h"
#include "ui_glyphs.h"
#include "ui_theme.h"

namespace ui_guide {

using ui::Canvas;
using ui::Face;
using ui::Font;
using ui::Image;
using ui::Rect;
using ui::rect;

// ---- geometry (screen coords for the card/toast, card-relative inside draw) ----
constexpr float kCardX = 80.0f, kCardY = 32.0f, kCardW = 560.0f, kCardH = 1016.0f;
constexpr float kCardR = ui::theme::radiusGuide;               // 16
constexpr float kTabCy = 48.0f;                                // tab icon centre row
constexpr float kTabCx[3] = {104.0f, 280.0f, 456.0f};          // Home, Achievements, Settings
constexpr float kUnderY = 92.0f, kUnderW = 64.0f, kUnderH = 4.0f;
constexpr float kDividerY = 96.0f;
constexpr float kFooterY = 944.0f;                             // footer divider
constexpr float kHomeY = 120.0f, kHomeRowH = 64.0f, kHomePitch = 72.0f;
constexpr float kSetRowH = 88.0f;
constexpr float kAchY = 216.0f, kAchPitch = 88.0f, kAchRowH = 80.0f;
constexpr int   kAchVisible = 8;                               // rows between y=216 and the footer
constexpr float kAchRight = 532.0f;                            // meta right edge
constexpr float kToastW = 352.0f, kToastH = 88.0f;
constexpr float kToastX = 784.0f, kToastY = 912.0f;            // centred pill, 68 px off the bottom

// ---- navigation state machine (pure; unit-tested with assert) ----
enum class Button { Up, Down, Left, Right, A, B, LB, RB };
enum class Action { None, Up, Down, Left, Right, Select, Close, TabPrev, TabNext };

// LB/RB always switch tabs; D-pad left/right does too, unless the focused row
// itself uses left/right (a toggle would rather flip than leave the tab).
inline Action press(Button b, bool rowUsesLeftRight) {
    switch (b) {
        case Button::Up:    return Action::Up;
        case Button::Down:  return Action::Down;
        case Button::Left:  return rowUsesLeftRight ? Action::Left : Action::TabPrev;
        case Button::Right: return rowUsesLeftRight ? Action::Right : Action::TabNext;
        case Button::A:     return Action::Select;
        case Button::B:     return Action::Close;
        case Button::LB:    return Action::TabPrev;
        case Button::RB:    return Action::TabNext;
    }
    return Action::None;
}

// ---- view models ----
struct AchievementView {
    std::string name, description;
    bool unlocked = false, hidden = false;
    std::string earned;                       // "Nov 9, 2026"; empty while locked
    const uint32_t* icon = nullptr;           // Steam icon, 0xAARRGGBB
    int iconW = 0, iconH = 0;
};

struct Toast {
    std::string title;                        // achievement display name under the fixed label
    const uint32_t* icon = nullptr;
    int iconW = 0, iconH = 0;
    double born = 0;                          // seconds; the owner decides expiry
};

// Thread-safe snapshot the Steam thread fills and the UI thread reads.
struct AchievementStore {
    mutable std::mutex m;
    std::vector<AchievementView> all;
    void set_achievements(std::vector<AchievementView> v) {
        std::lock_guard<std::mutex> l(m);
        all = std::move(v);
    }
    // Marks one achievement unlocked (by index), stamps the date and returns
    // the toast to show, or nullopt-ish (empty title) if the index is bad.
    Toast push_unlock(size_t index, const std::string& earnedDate, double nowSeconds) {
        std::lock_guard<std::mutex> l(m);
        if (index >= all.size()) return {};
        all[index].unlocked = true;
        all[index].earned = earnedDate;
        return {all[index].name, all[index].icon, all[index].iconW, all[index].iconH, nowSeconds};
    }
    std::vector<AchievementView> snapshot() const {
        std::lock_guard<std::mutex> l(m);
        return all;
    }
    int unlocked() const {
        std::lock_guard<std::mutex> l(m);
        int n = 0;
        for (const auto& a : all) n += a.unlocked;
        return n;
    }
    int total() const {
        std::lock_guard<std::mutex> l(m);
        return int(all.size());
    }
};

struct Model {
    int tab = 0;                              // 0 Home, 1 Achievements, 2 Settings
    int sel = 0;                              // focused row within the tab
    int achSel = 0, achTop = 0;               // achievements list: selection + first visible row
    bool fpsOn = false, graphOn = false;
    bool busy = false;                        // quitting: rows grey out, status shows
    std::string status;
    const std::vector<AchievementView>* achievements = nullptr;
};

inline int row_count(const Model& m) {
    if (m.tab == 0) return 3;                                     // Resume, Quit game, Force quit
    if (m.tab == 1) return m.achievements ? int(m.achievements->size()) : 0;
    return 2;                                                     // FPS counter, Frame-time graph
}

inline void sync_scroll(Model& m) {
    if (m.achSel < m.achTop) m.achTop = m.achSel;
    if (m.achSel >= m.achTop + kAchVisible) m.achTop = m.achSel - kAchVisible + 1;
    const int n = row_count(m);
    if (m.achTop > n - kAchVisible) m.achTop = std::max(0, n - kAchVisible);
}

// Applies a navigation action to the model; Select/Close/Left/Right are left
// to the caller (they map to menu commands, not selection moves).
inline void move(Model& m, Action a) {
    const int n = row_count(m);
    switch (a) {
        case Action::Up:   if (n > 0) m.sel = (m.sel + n - 1) % n; if (m.tab == 1) sync_scroll(m); break;
        case Action::Down: if (n > 0) m.sel = (m.sel + 1) % n;     if (m.tab == 1) sync_scroll(m); break;
        case Action::TabPrev: m.tab = (m.tab + 2) % 3; m.sel = 0; break;
        case Action::TabNext: m.tab = (m.tab + 1) % 3; m.sel = 0; break;
        default: break;
    }
}

// ---- date formatting (portable, UTC; Howard Hinnant's civil-from-days) ----
inline std::string format_date(long long unixSecs) {
    static const char* mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const long long z = unixSecs / 86400 + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long d = doy - (153 * mp + 2) / 5 + 1;
    const long long m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y += 1;
    char b[40];
    std::snprintf(b, sizeof b, "%s %lld, %lld", mon[m - 1], d, y);
    return b;
}

// ---- drawing ----------------------------------------------------------------

// Row glyphs (home rows and tab icons), drawn from primitives; the mockups'
// vectors normalised to a box, 2.5px stroke.
namespace icons {

inline void play(Canvas& c, const Rect& r, uint32_t color) {           // Resume
    const float p[6] = {r.x, r.y, r.x, r.y + r.h, r.x + r.w, r.y + r.h * 0.5f};
    c.fill_polygon(p, 3, color);
}
inline void quit(Canvas& c, const Rect& r, uint32_t color, float strokePx) {   // Quit game: door + arrow out
    const float dw = 20, dh = 20, s = std::min(r.w / dw, r.h / dh);
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    auto X = [&](float x) { return cx + (x - dw * 0.5f) * s; };
    auto Y = [&](float y) { return cy + (y - dh * 0.5f) * s; };
    float p[8];
    p[0] = X(3); p[1] = Y(2); p[2] = X(9); p[3] = Y(2); p[4] = X(9); p[5] = Y(18); p[6] = X(3); p[7] = Y(18);
    c.stroke_polyline(p, 4, true, strokePx, color);
    p[0] = X(12); p[1] = Y(10); p[2] = X(18); p[3] = Y(10);
    c.stroke_polyline(p, 2, false, strokePx, color);
    p[0] = X(15.5f); p[1] = Y(7); p[2] = X(18); p[3] = Y(10); p[4] = X(15.5f); p[5] = Y(13);
    c.stroke_polyline(p, 3, false, strokePx, color);
}
inline void power(Canvas& c, const Rect& r, uint32_t color, float strokePx) {  // Force quit
    const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f + r.h * 0.08f;
    const float rad = std::min(r.w, r.h) * 0.42f;
    c.stroke_circle(cx, cy, rad, strokePx, color, ui::Align::Inside);
    c.fill_rect(cx - strokePx * 0.5f, r.y, strokePx, r.h * 0.5f, color);
}
inline void home(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    const float p[10] = {r.x, r.y + r.h * 0.45f, r.x + r.w * 0.5f, r.y,
                         r.x + r.w, r.y + r.h * 0.45f,
                         r.x + r.w * 0.85f, r.y + r.h * 0.45f,
                         r.x + r.w * 0.85f, r.y + r.h};
    c.stroke_polyline(p, 5, false, strokePx, color);
    const float q[6] = {r.x + r.w * 0.15f, r.y + r.h * 0.45f, r.x + r.w * 0.15f, r.y + r.h,
                        r.x + r.w * 0.85f, r.y + r.h};
    c.stroke_polyline(q, 3, false, strokePx, color);
}
inline void trophy(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    // cup: two sides, stem, base and handles
    float q[8];
    q[0] = r.x + r.w * 0.25f; q[1] = r.y;
    q[2] = r.x + r.w * 0.75f; q[3] = r.y;
    q[4] = r.x + r.w * 0.72f; q[5] = r.y + r.h * 0.5f;
    q[6] = r.x + r.w * 0.5f;  q[7] = r.y + r.h * 0.58f;
    c.stroke_polyline(q, 4, false, strokePx, color);
    q[0] = r.x + r.w * 0.25f; q[1] = r.y;
    q[2] = r.x + r.w * 0.28f; q[3] = r.y + r.h * 0.5f;
    q[4] = r.x + r.w * 0.5f;  q[5] = r.y + r.h * 0.58f;
    c.stroke_polyline(q, 3, false, strokePx, color);
    q[0] = r.x + r.w * 0.5f;  q[1] = r.y + r.h * 0.58f;
    q[2] = r.x + r.w * 0.5f;  q[3] = r.y + r.h * 0.8f;
    c.stroke_polyline(q, 2, false, strokePx, color);
    q[0] = r.x + r.w * 0.32f; q[1] = r.y + r.h * 0.95f;
    q[2] = r.x + r.w * 0.68f; q[3] = r.y + r.h * 0.95f;
    c.stroke_polyline(q, 2, false, strokePx, color);
    q[0] = r.x + r.w * 0.25f; q[1] = r.y + r.h * 0.12f;
    q[2] = r.x + r.w * 0.1f;  q[3] = r.y + r.h * 0.12f;
    q[4] = r.x + r.w * 0.1f;  q[5] = r.y + r.h * 0.4f;
    q[6] = r.x + r.w * 0.3f;  q[7] = r.y + r.h * 0.45f;
    c.stroke_polyline(q, 4, false, strokePx, color);
    q[0] = r.x + r.w * 0.75f; q[1] = r.y + r.h * 0.12f;
    q[2] = r.x + r.w * 0.9f;  q[3] = r.y + r.h * 0.12f;
    q[4] = r.x + r.w * 0.9f;  q[5] = r.y + r.h * 0.4f;
    q[6] = r.x + r.w * 0.7f;  q[7] = r.y + r.h * 0.45f;
    c.stroke_polyline(q, 4, false, strokePx, color);
}
inline void cog(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    ui::glyphs::cog(c, r, color, strokePx);
}

}  // namespace icons

namespace detail {
// Vertical centre baseline for a text box of `px` size at top `y`.
inline float baseline(const Font& f, Face face, float px, float boxY, float boxH) {
    return boxY + boxH * 0.5f + (f.ascent(face, px) - f.descent(face, px)) * 0.5f;
}
// Draws right-aligned text ending at `right`; returns the left x.
inline float draw_right(Canvas& c, const Font& f, Face face, float px, float right, float baselineY,
                        const std::string& s, uint32_t color) {
    const float w = f.measure(face, px, s);
    f.draw(c, face, px, right - w, baselineY, s, color);
    return right - w;
}
}  // namespace detail

// Toggle switch: 56x32 track, 24px knob right when on, left when off.
inline void draw_toggle(Canvas& c, float x, float y, bool on) {
    using namespace ui::theme;
    c.fill_round_rect(rect(x, y, 56, 32), 16, on ? accent : track);
    c.fill_circle(on ? x + 40 : x + 16, y + 16, 12, glyphBright);
}

// One focused/home row highlight: #303030 fill with the mockup's 3px white ring.
inline void draw_focus_row(Canvas& c, const Rect& r, float radius) {
    using namespace ui::theme;
    c.fill_round_rect(r, radius, control);
    c.stroke_round_rect(r, radius, ui::theme::ringWidth, focus, ui::Align::Inside);
}

// Achievement icon: the Steam image when present, else the mockup's fallbacks
// (coloured tile + mark when unlocked, #383838 tile, '?' when hidden).
inline void draw_ach_icon(Canvas& c, const Font& f, const Rect& r, const AchievementView& a) {
    using namespace ui::theme;
    if (a.icon && a.iconW > 0 && a.iconH > 0) {
        c.draw_image(Image{a.icon, a.iconW, a.iconH, a.iconW}, r, ui::Fit::Cover, ui::theme::radiusTile);
        if (!a.unlocked) {   // grey the locked icon in place
            ui::Canvas::Clip saved = c.clip();
            c.set_clip(rect(r.x, r.y, r.w, r.h));
            c.fill_rect_alpha(int(r.x), int(r.y), int(r.w), int(r.h), scrim, 140);
            c.set_clip(saved);
        }
        return;
    }
    if (a.unlocked) {
        c.fill_round_rect(r, ui::theme::radiusTile, rgb(63, 91, 115));   // mockup #3F5B73
        const float p[6] = {r.x + r.w * 0.28f, r.y + r.h * 0.52f, r.x + r.w * 0.44f, r.y + r.h * 0.68f,
                            r.x + r.w * 0.74f, r.y + r.h * 0.32f};
        c.stroke_polyline(p, 3, false, 4, rgb(234, 223, 196));           // mockup mark #EADFC4
        return;
    }
    c.fill_round_rect(r, ui::theme::radiusTile, rgb(56, 56, 56));        // mockup #383838
    if (a.hidden) {
        const float px = 30.0f;
        const float w = f.measure(Face::Regular, px, "?");
        f.draw(c, Face::Regular, px, r.x + r.w * 0.5f - w * 0.5f,
               r.y + r.h * 0.5f + f.ascent(Face::Regular, px) * 0.5f, "?", rgb(106, 106, 106));
    } else {
        c.stroke_circle(r.x + r.w * 0.5f, r.y + r.h * 0.5f, 12, 4, rgb(110, 110, 110), ui::Align::Inside);
    }
}

// The tab strip: three icons, the active one white with the 64x4 green underline.
inline void draw_tabs(Canvas& c, const Font& f, const Model& m) {
    using namespace ui::theme;
    (void)f;
    const float stroke = 2.5f;
    for (int i = 0; i < 3; ++i) {
        const uint32_t color = i == m.tab ? text : iconMuted;
        const Rect box = rect(kTabCx[i] - 14, kTabCy - 14, 28, 28);
        if (i == 0) icons::home(c, box, color, stroke);
        else if (i == 1) icons::trophy(c, box, color, stroke);
        else icons::cog(c, box, color, stroke);
        if (i == m.tab) c.fill_round_rect(rect(kTabCx[i] - kUnderW * 0.5f, kUnderY, kUnderW, kUnderH), 2, accent);
    }
    c.fill_rect(0, kDividerY, kCardW, 1, control);
}

// The footer: A Select / B Close prompts above the card's bottom edge.
inline void draw_footer(Canvas& c, const Font& f) {
    using namespace ui::theme;
    c.fill_rect(0, kFooterY, kCardW, 1, control);
    ui::glyphs::prompt(c, 335, 968, f, 'A', "Select", 24, glyphBright, rgb(26, 26, 26), hint, 20);
    ui::glyphs::prompt(c, 452, 968, f, 'B', "Close", 24, glyphBright, rgb(26, 26, 26), hint, 20);
}

// ---- tab contents ----
inline void draw_home(Canvas& c, const Font& f, const Model& m) {
    using namespace ui::theme;
    const char* labels[3] = {"Resume", "Quit game", "Force quit"};
    for (int i = 0; i < 3; ++i) {
        const float y = kHomeY + float(i) * kHomePitch;
        const Rect row = rect(16, y, 528, kHomeRowH);
        if (i == m.sel && !m.busy) draw_focus_row(c, row, ui::theme::radiusCard);
        const uint32_t color = m.busy ? textMuted : text;
        icons::play(c, rect(33, y + 23, 14, 18), color);
        if (i == 1) icons::quit(c, rect(30, y + 22, 20, 20), color, 2.0f);
        if (i == 2) icons::power(c, rect(31, y + 25, 16, 16), color, 2.5f);
        f.draw(c, Face::Regular, 28, 68, detail::baseline(f, Face::Regular, 28, y, kHomeRowH), labels[i], color);
    }
}

inline void draw_achievements(Canvas& c, const Font& f, const Model& m) {
    using namespace ui::theme;
    f.draw(c, Face::Semibold, 34, 28, 112 + f.ascent(Face::Semibold, 34), "Achievements", text);
    const int total = m.achievements ? int(m.achievements->size()) : 0;
    int unlocked = 0;
    for (int i = 0; i < total; ++i) unlocked += (*m.achievements)[size_t(i)].unlocked;
    char sub[96];
    std::snprintf(sub, sizeof sub, "Steam \xC2\xB7 %d of %d unlocked", unlocked, total);
    f.draw(c, Face::Regular, 24, 28, 150 + f.ascent(Face::Regular, 24), sub, textDim);
    c.fill_round_rect(rect(28, 186, 504, 4), 2, rgb(58, 58, 58));
    if (total > 0) c.fill_round_rect(rect(28, 186, 504.0f * unlocked / float(total), 4), 2, accent);

    if (!m.achievements) return;
    const int last = std::min(total, m.achTop + kAchVisible);
    for (int i = m.achTop; i < last; ++i) {
        const AchievementView& a = (*m.achievements)[size_t(i)];
        const float y = kAchY + float(i - m.achTop) * kAchPitch;
        const Rect row = rect(16, y, 528, kAchRowH);
        if (i == m.achSel && !m.busy) draw_focus_row(c, row, ui::theme::radiusCard);
        draw_ach_icon(c, f, rect(28, y + 12, 56, 56), a);
        const std::string title = a.hidden && !a.unlocked ? "Hidden achievement" : a.name;
        const std::string desc = a.hidden && !a.unlocked ? "Details are hidden until unlocked" : a.description;
        f.draw(c, Face::Regular, 26, 100, y + 14 + f.ascent(Face::Regular, 26), title, text);
        f.draw(c, Face::Regular, 22, 100, y + 44 + f.ascent(Face::Regular, 22), desc, textDim);
        const std::string meta = a.unlocked && !a.earned.empty() ? a.earned : "Locked";
        detail::draw_right(c, f, Face::Regular, 22, kAchRight, y + 16 + f.ascent(Face::Regular, 22), meta,
                           rgb(143, 143, 143));
    }
}

inline void draw_settings(Canvas& c, const Font& f, const Model& m) {
    using namespace ui::theme;
    // Row 0: FPS counter (88px high, icon + label + sub + toggle)
    const Rect row = rect(16, 120, 528, kSetRowH);
    if (m.sel == 0 && !m.busy) draw_focus_row(c, row, ui::theme::radiusCard);
    const uint32_t color = m.busy ? textMuted : text;
    icons::cog(c, rect(32, 156, 16, 16), color, 2.0f);
    f.draw(c, Face::Regular, 28, 68, 136 + f.ascent(Face::Regular, 28), "FPS counter", color);
    f.draw(c, Face::Regular, 22, 68, 170 + f.ascent(Face::Regular, 22), "Show FPS in the top-left corner", textDim);
    draw_toggle(c, 476, 148, m.fpsOn);
    // Sub-row: frame-time graph, shown and reachable only while the counter is on.
    if (m.fpsOn) {
        const Rect sub = rect(48, 216, 496, 64);
        c.fill_round_rect(sub, ui::theme::radiusControl, controlDark);
        if (m.sel == 1 && !m.busy) c.stroke_round_rect(sub, ui::theme::radiusControl, ui::theme::ringWidth, focus,
                                                       ui::Align::Inside);
        f.draw(c, Face::Regular, 28, 100, 233.5f + f.ascent(Face::Regular, 28), "Frame-time graph",
               m.busy ? textMuted : text);
        draw_toggle(c, 476, 232, m.graphOn);
    }
}

// The whole guide card at its fixed position; `dimmed` pixels (the game frame
// under the 55% scrim) show through the rounded corners when provided.
inline void draw_guide(Canvas& c, const Font& f, const Model& m, const uint32_t* dimmed = nullptr) {
    using namespace ui::theme;
    if (dimmed) {   // backdrop: the already-dimmed game frame, card-relative
        for (int y = 0; y < int(kCardH); ++y)
            std::copy_n(dimmed + size_t(y) * int(kCardW), int(kCardW),
                        c.data() + size_t(int(kCardY) + y) * c.width() + int(kCardX));
    } else {
        c.fill_rect(kCardX, kCardY, kCardW, kCardH, page);
    }
    c.fill_round_rect(rect(kCardX, kCardY, kCardW, kCardH), kCardR, panel);
    // clip everything below to the card
    ui::Canvas::Clip saved = c.clip();
    c.set_clip(rect(kCardX, kCardY, kCardW, kCardH));
    Canvas card(c.data() + size_t(int(kCardY)) * c.width() + int(kCardX), int(kCardW), int(kCardH), c.width());
    draw_tabs(card, f, m);
    if (m.tab == 0) draw_home(card, f, m);
    else if (m.tab == 1) draw_achievements(card, f, m);
    else draw_settings(card, f, m);
    if (!m.status.empty())
        f.draw(card, Face::Regular, 24, 28, 900 + f.ascent(Face::Regular, 24), m.status, textDim);
    draw_footer(card, f);
    c.set_clip(saved);
}

// Achievement toast: centred bottom pill, icon left, fixed label + name.
inline void draw_toast(Canvas& c, const Font& f, const Toast& t, float x = kToastX, float y = kToastY) {
    using namespace ui::theme;
    c.fill_round_rect(rect(x, y, kToastW, kToastH), kToastH * 0.5f, panel);
    const Rect icon = rect(x + 16, y + 16, 56, 56);
    if (t.icon && t.iconW > 0 && t.iconH > 0) {
        c.draw_image(Image{t.icon, t.iconW, t.iconH, t.iconW}, icon, ui::Fit::Cover, ui::theme::radiusTile);
    } else {
        c.fill_round_rect(icon, ui::theme::radiusTile, rgb(63, 91, 115));
        c.stroke_circle(x + 44, y + 44, 12, 4, rgb(234, 223, 196), ui::Align::Inside);
    }
    f.draw(c, Face::Semibold, 26, x + 88, y + 18 + f.ascent(Face::Semibold, 26), "Achievement unlocked", text);
    f.draw(c, Face::Regular, 22, x + 88, y + 48 + f.ascent(Face::Regular, 22), t.title, textDim);
}

// Dims one row of back-buffer pixels in place: scales the three colour bytes
// uniformly by pct_keep/100, leaving the alpha byte untouched. Works for both
// RGBA and BGRA memory orders, since the three colour channels scale the same.
inline void dim_row(uint8_t* px, int count, int pct_keep) {
    for (int i = 0; i < count * 4; i += 4) {
        px[i + 0] = uint8_t(px[i + 0] * pct_keep / 100);
        px[i + 1] = uint8_t(px[i + 1] * pct_keep / 100);
        px[i + 2] = uint8_t(px[i + 2] * pct_keep / 100);
        // px[i + 3] (alpha) stays
    }
}

// FPS counter + optional frame-time graph, drawn into a top-left overlay
// canvas the caller sized (240 x 44 px, +64 when the graph is on, times scale).// Graph: one column per frame (newest right), 0-50 ms tall; guides at 16.7 ms
// (60 fps) and 33.3 ms (30 fps); red over 33 ms, yellow over 16.7 ms.
inline void draw_fps_counter(Canvas& c, const Font& f, float fps, bool graph, float scale,
                             const float* history, unsigned n, unsigned head) {
    using namespace ui::theme;
    c.fill_rect_alpha(0, 0, c.width(), c.height(), scrim, 150);
    char b[32];
    std::snprintf(b, sizeof b, "FPS %.0f", fps);
    f.draw(c, Face::Semibold, 28 * scale, 12 * scale, 32 * scale, b, rgb(120, 255, 140));
    if (!graph || !history || n == 0) return;
    const float gx = 12 * scale, gy = 44 * scale, gw = c.width() - 24 * scale, gh = 56 * scale;
    c.fill_rect(gx, gy + gh - 16.7f / 50.0f * gh, gw, 1, rgb(60, 90, 60));
    c.fill_rect(gx, gy + gh - 33.3f / 50.0f * gh, gw, 1, rgb(90, 60, 60));
    const float col = gw / float(n);
    for (unsigned i = 0; i < n; ++i) {
        const float v = history[(head + i) % n];
        const float bar = std::min(gh, v / 50.0f * gh);
        const uint32_t color = v > 33.4f ? rgb(255, 90, 90) : v > 17.5f ? rgb(255, 210, 63) : rgb(120, 255, 140);
        c.fill_rect(gx + float(i) * col, gy + gh - bar, std::max(1.0f, col - 1.0f), bar, color);
    }
}

}  // namespace ui_guide
