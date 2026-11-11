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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
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
constexpr float kAchRight = 531.0f;                            // meta right edge
constexpr float kToastW = 352.0f, kToastH = 88.0f;
constexpr float kToastX = 784.0f, kToastY = 912.0f;            // centred pill, 68 px off the bottom

// ---- destination rects on the game frame ------------------------------------
// All guide geometry is defined in the 1920x1080 design space (the figcraft).
// The card/toast render 1:1 into their design-size canvases; on a frame of
// another size the canvas is nearest-scaled by a uniform s (the tighter axis
// wins, so the whole design always fits) and blitted at the scaled design
// position -- exact at 1920x1080, in-bounds everywhere. Pure helpers so the
// native tests can assert the 1080p contract and the in-bounds invariant.
inline float guide_scale(int backW, int backH) {
    if (backW <= 0 || backH <= 0) return 1.0f;
    return std::min(float(backW) / float(ui::theme::width),
                    float(backH) / float(ui::theme::height));
}
inline Rect card_rect(int backW, int backH) {
    const float s = guide_scale(backW, backH);
    return rect(std::lround(kCardX * s), std::lround(kCardY * s),
                std::lround(kCardW * s), std::lround(kCardH * s));
}
inline Rect toast_rect(int backW, int backH) {
    const float s = guide_scale(backW, backH);
    return rect(std::lround(kToastX * s), std::lround(kToastY * s),
                std::lround(kToastW * s), std::lround(kToastH * s));
}
// Nearest-neighbour scale of an ARGB canvas (the 1:1 guide render onto a
// frame whose scale is not 1). Pure; tested.
inline std::vector<uint32_t> scale_nearest(const uint32_t* src, int sw, int sh, int dw, int dh) {
    std::vector<uint32_t> out(size_t(dw > 0 ? dw : 1) * size_t(dh > 0 ? dh : 1), 0);
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return out;
    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(sh - 1, int(int64_t(y) * sh / dh));
        const uint32_t* srow = src + size_t(sy) * sw;
        uint32_t* drow = out.data() + size_t(y) * dw;
        for (int x = 0; x < dw; ++x)
            drow[x] = srow[std::min(sw - 1, int(int64_t(x) * sw / dw))];
    }
    return out;
}

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
// Fallback mark drawn inside an icon tile when no Steam image is set (the
// mockups' placeholder marks: ring, squares, triangle, bar chart).
enum class Mark { Ring, Square, Badge, Triangle, Bars };

struct AchievementView {
    std::string name, description;
    bool unlocked = false, hidden = false;
    std::string earned;                       // "Nov 9, 2026"; empty while locked
    const uint32_t* icon = nullptr;           // Steam icon, 0xAARRGGBB
    int iconW = 0, iconH = 0;
    // Fallback art (icon == nullptr): tile fill and mark colour, 0 = default
    // (coloured tile when unlocked, #383838 when locked).
    uint32_t tileColor = 0, markColor = 0;
    Mark mark = Mark::Ring;
};

struct Toast {
    std::string title;                        // achievement display name under the fixed label
    const uint32_t* icon = nullptr;
    int iconW = 0, iconH = 0;
    double born = 0;                          // seconds on the owner's clock; the queue owner stamps it
};

// A queued toast is expired once `life` seconds have passed since its stamp
// (born <= 0 means "never stamped": it never expires). Pure so the native
// tests can drive it with an injected clock.
inline bool toast_expired(const Toast& t, double nowSeconds, double life) {
    return t.born > 0 && nowSeconds - t.born > life;
}

// Thread-safe snapshot the Steam thread fills and the UI thread reads.
// A generation counter bumps on every mutation so a renderer holding a
// snapshot pointer can cheaply notice "something changed" without a dirty
// flag the callers would have to remember to set.
struct AchievementStore {
    mutable std::mutex m;
    std::vector<AchievementView> all;
    unsigned gen = 0;                         // bumped by every mutation
    unsigned generation() const { std::lock_guard<std::mutex> l(m); return gen; }
    void touch() { ++gen; }
    // Icon pixel buffers the views point into (set_icon swaps the view's
    // pointer; the buffer must outlive every view that points into it).
    std::vector<std::shared_ptr<std::vector<uint32_t>>> iconPix;
    void set_achievements(std::vector<AchievementView> v) {
        std::lock_guard<std::mutex> l(m);
        all = std::move(v);
        iconPix.clear();
        touch();
    }
    // Installs one decoded icon (by index) into the view: unlocked pixels on
    // an unlocked entry, grey pixels on a locked one. `pix` must stay alive;
    // the store keeps a reference to it. Returns false on a bad index.
    bool set_icon(size_t index, const std::shared_ptr<std::vector<uint32_t>>& pix, int w, int h, bool unlockedIcon) {
        if (!pix || w <= 0 || h <= 0) return false;
        std::lock_guard<std::mutex> l(m);
        if (index >= all.size()) return false;
        iconPix.push_back(pix);
        AchievementView& a = all[index];
        if (unlockedIcon) { a.icon = pix->data(); a.iconW = w; a.iconH = h; }
        else if (!a.unlocked) { a.icon = pix->data(); a.iconW = w; a.iconH = h; }
        touch();
        return true;
    }
    // Marks one achievement unlocked (by index), stamps the date and returns
    // the toast to show, or nullopt-ish (empty title) if the index is bad.
    Toast push_unlock(size_t index, const std::string& earnedDate, double nowSeconds) {
        std::lock_guard<std::mutex> l(m);
        if (index >= all.size()) return {};
        all[index].unlocked = true;
        all[index].earned = earnedDate;
        touch();
        return {all[index].name, all[index].icon, all[index].iconW, all[index].iconH, nowSeconds};
    }
    // Same, plus the real name/description for a hidden achievement (its view
    // was built masked while locked). Empty names keep the view's text.
    Toast unlock(size_t index, const std::string& name, const std::string& description,
                 const std::string& earnedDate, double nowSeconds) {
        std::lock_guard<std::mutex> l(m);
        if (index >= all.size()) return {};
        AchievementView& a = all[index];
        a.unlocked = true;
        a.earned = earnedDate;
        if (!name.empty()) { a.name = name; a.description = description; }
        touch();
        return {a.name, a.icon, a.iconW, a.iconH, nowSeconds};
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
    return m.fpsOn ? 2 : 1;                                       // FPS counter; frame-time graph only while it is on
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
    // The achievements list consumes achSel (draw_achievements focuses on it);
    // keep it equal to sel so the tab's selection state lives in one place.
    if (m.tab == 1) { m.achSel = m.sel; sync_scroll(m); }
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
// vectors traced vertex-for-vertex (2.5px centred round-cap stroke).
namespace icons {

// Resume: outlined triangle (2.5px round stroke, as the mockup vector). Design box 14 x 18.667.
inline void play(Canvas& c, const Rect& r, uint32_t color, float strokePx = 2.5f) {
    const float p[6] = {r.x, r.y, r.x + r.w, r.y + r.h * 0.5f, r.x, r.y + r.h};
    c.stroke_polyline(p, 3, true, strokePx, color);
}
// Quit game: door with an arrow out. Design box 15.167 x 18.667.
inline void quit(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    const float dw = 15.16667f, dh = 18.66663f;
    const float s = std::min(r.w / dw, r.h / dh);
    auto X = [&](float x) { return r.x + x * s; };
    auto Y = [&](float y) { return r.y + y * s; };
    float p[8];
    p[0] = X(5.83337f); p[1] = Y(0); p[2] = X(0); p[3] = Y(0);
    p[4] = X(0); p[5] = Y(18.66663f); p[6] = X(5.83337f); p[7] = Y(18.66663f);
    c.stroke_polyline(p, 4, false, strokePx, color);
    p[0] = X(10.49997f); p[1] = Y(4.66666f); p[2] = X(15.16667f); p[3] = Y(9.33333f);
    p[4] = X(10.49997f); p[5] = Y(14.00003f);
    c.stroke_polyline(p, 3, false, strokePx, color);
    p[0] = X(15.16667f); p[1] = Y(9.33333f); p[2] = X(4.66667f); p[3] = Y(9.33333f);
    c.stroke_polyline(p, 2, false, strokePx, color);
}
// Force quit: X cross. Design box 14 x 14.
inline void power(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    const float p[8] = {r.x, r.y, r.x + r.w, r.y + r.h, r.x + r.w, r.y, r.x, r.y + r.h};
    c.stroke_polyline(p, 2, false, strokePx, color);
    c.stroke_polyline(p + 4, 2, false, strokePx, color);
}
// Home tab: roof plus walls with a door gap. Design box 30 x 26.667.
inline void home(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    const float p[6] = {r.x, r.y + r.h * 0.5f,
                        r.x + r.w * 0.5f, r.y,
                        r.x + r.w, r.y + r.h * 0.5f};
    c.stroke_polyline(p, 3, false, strokePx, color);
    const float q[10] = {r.x + r.w / 6.0f, r.y + r.h * 0.375f,
                         r.x + r.w / 6.0f, r.y + r.h,
                         r.x + r.w * 5.0f / 6.0f, r.y + r.h,
                         r.x + r.w * 5.0f / 6.0f, r.y + r.h * 0.375f};
    c.stroke_polyline(q, 4, false, strokePx, color);
}
// Achievements tab: the mockup's cup-with-side-handles vector (design 26.667 x 26.667).
inline void trophy(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    const float dw = 26.66663f, dh = 26.66663f;
    const float s = std::min(r.w / dw, r.h / dh);
    auto X = [&](float x) { return r.x + x * s; };
    auto Y = [&](float y) { return r.y + y * s; };
    float p[10];
    // cup body: left side, bottom curve, right side, top
    p[0] = X(5.00003f); p[1] = Y(0);
    p[2] = X(5.00003f); p[3] = Y(10.00003f);
    p[4] = X(13.33333f); p[5] = Y(18.33333f);
    p[6] = X(21.66663f); p[7] = Y(10.00003f);
    p[8] = X(21.66663f); p[9] = Y(0);
    c.stroke_polyline(p, 5, false, strokePx, color);
    p[0] = X(5.00003f); p[1] = Y(0); p[2] = X(21.66663f); p[3] = Y(0);
    c.stroke_polyline(p, 2, false, strokePx, color);
    // left handle: out, down, back in
    p[0] = X(5.00003f); p[1] = Y(3.33333f); p[2] = X(0); p[3] = Y(3.33333f);
    p[4] = X(0); p[5] = Y(6.66663f); p[6] = X(5.00003f); p[7] = Y(11.66663f);
    c.stroke_polyline(p, 4, false, strokePx, color);
    // right handle
    p[0] = X(21.66663f); p[1] = Y(3.33333f); p[2] = X(26.66663f); p[3] = Y(3.33333f);
    p[4] = X(26.66663f); p[5] = Y(6.66663f); p[6] = X(21.66663f); p[7] = Y(11.66663f);
    c.stroke_polyline(p, 4, false, strokePx, color);
    // stem and base
    p[0] = X(13.33333f); p[1] = Y(18.33333f); p[2] = X(13.33333f); p[3] = Y(23.33333f);
    c.stroke_polyline(p, 2, false, strokePx, color);
    p[0] = X(6.66663f); p[1] = Y(26.66663f); p[2] = X(20.00003f); p[3] = Y(26.66663f);
    c.stroke_polyline(p, 2, false, strokePx, color);
}
// Settings tab: three slider lines with knobs (the mockup's vector, design
// 26.667 x 16.667; knob centres on the lines at x 8.3 / 18.3 / 6.6).
inline void sliders(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    const float dw = 26.66663f, dh = 16.6666f;
    const float s = std::min(r.w / dw, r.h / dh);
    auto X = [&](float x) { return r.x + x * s; };
    auto Y = [&](float y) { return r.y + y * s; };
    float p[4];
    for (int i = 0; i < 3; ++i) {
        const float y = Y(float(i) * 8.3333f);
        p[0] = X(0); p[1] = y; p[2] = X(26.66663f); p[3] = y;
        c.stroke_polyline(p, 2, false, strokePx, color);
    }
    // knobs: 10px discs filled with the card colour so the line breaks
    const float kx[3] = {8.3f, 18.3f, 6.3f};
    const float ky[3] = {1.3f, 8.3f, 14.8f};
    for (int i = 0; i < 3; ++i) {
        c.fill_circle(X(kx[i]), Y(ky[i]), 5.0f * s, 0xFF1F1F1F);
        c.stroke_circle(X(kx[i]), Y(ky[i]), 5.0f * s, strokePx, color, ui::Align::Inside);
    }
}
// FPS row icon: three bottom-aligned vertical ticks (the mockup's gauge vector).
inline void gauge(Canvas& c, const Rect& r, uint32_t color, float strokePx) {
    const float dw = 16.33337f, dh = 16.33337f;
    const float s = std::min(r.w / dw, r.h / dh);
    auto X = [&](float x) { return r.x + x * s; };
    auto Y = [&](float y) { return r.y + y * s; };
    const float tops[3] = {8.16667f, 0.0f, 4.66667f};
    float p[4];
    for (int i = 0; i < 3; ++i) {
        p[0] = X(float(i) * 8.16667f); p[1] = Y(16.33337f);
        p[2] = X(float(i) * 8.16667f); p[3] = Y(tops[i]);
        c.stroke_polyline(p, 2, false, strokePx, color);
    }
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
    c.fill_circle(on ? x + 40 : x + 16, y + 16, 12, glyphBright);  // mock knob x+40 (24px knob, 4px inset)
}

// One focused/home row highlight: #303030 fill with the mockup's 3px green ring.
inline void draw_focus_row(Canvas& c, const Rect& r, float radius) {
    using namespace ui::theme;
    c.fill_round_rect(r, radius, control);
    c.stroke_round_rect(r, radius, ui::theme::ringWidth, accent, ui::Align::Inside);
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
    const uint32_t tile = a.tileColor ? a.tileColor
                                      : a.unlocked ? rgb(63, 91, 115) : rgb(56, 56, 56);
    c.fill_round_rect(r, ui::theme::radiusTile, tile);
    if (a.hidden) {
        const float px = 30.0f;
        const float w = f.measure(Face::Regular, px, "?");
        f.draw(c, Face::Regular, px, r.x + r.w * 0.5f - w * 0.5f,
               r.y + r.h * 0.5f + f.ascent(Face::Regular, px) * 0.5f, "?",
               a.markColor ? a.markColor : rgb(106, 106, 106));
        return;
    }
    const uint32_t mark = a.markColor ? a.markColor : rgb(234, 223, 196);
    switch (a.mark) {
        case Mark::Ring:     // 24px ring, 4px stroke (mockup mark nodes)
            c.stroke_circle(r.x + r.w * 0.5f, r.y + r.h * 0.5f, 12, 4, mark, ui::Align::Center);
            break;
        case Mark::Square:   // 20x20 filled square, 2px corner radius
            c.fill_round_rect(rect(r.x + r.w * 0.5f - 10, r.y + r.h * 0.5f - 10, 20, 20), 2, mark);
            break;
        case Mark::Badge: {  // filled circle with a tile-coloured dot
            c.fill_circle(r.x + r.w * 0.5f, r.y + r.h * 0.5f, 12, mark);
            c.fill_circle(r.x + r.w * 0.5f, r.y + r.h * 0.5f, 5, tile);
            break;
        }
        case Mark::Triangle: {   // outlined triangle, 4px stroke (mockup vector mark)
            const float p[6] = {r.x + r.w * 0.5f, r.y + r.h * 0.28f,
                                r.x + r.w * 0.75f, r.y + r.h * 0.72f,
                                r.x + r.w * 0.25f, r.y + r.h * 0.72f};
            c.stroke_polyline(p, 3, true, 4, mark);
            break;
        }
        case Mark::Bars: {   // three-bar chart, bottom aligned (mockup 4/16/20px bars)
            const float bw = 4.0f, gap = 6.0f;
            const float hs[3] = {12.0f, 16.0f, 20.0f};
            const float x0 = r.x + r.w * 0.5f - (bw * 3 + gap * 2) * 0.5f;
            for (int i = 0; i < 3; ++i) {
                const float bh = hs[i];
                c.fill_round_rect(rect(x0 + float(i) * (bw + gap), r.y + r.h * 0.5f + 10.0f - bh, bw, bh), 1, mark);
            }
            break;
        }
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
        else icons::sliders(c, box, color, stroke);
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
        // Each glyph keeps its design size, centred on the row (x 40, row middle y + 32.667).
        const float cy = y + 32.667f;
        if (i == 0) icons::play(c, rect(33, cy - 9.333f, 14, 18.667f), color);
        if (i == 1) icons::quit(c, rect(40 - 7.583f, cy - 9.333f, 15.167f, 18.667f), color, 2.5f);
        if (i == 2) icons::power(c, rect(33, cy - 7, 14, 14), color, 2.5f);
        f.draw(c, i == 0 ? Face::Semibold : Face::Regular, 24, 68,
               detail::baseline(f, i == 0 ? Face::Semibold : Face::Regular, 24, y, kHomeRowH), labels[i], color);
    }
}

inline void draw_achievements(Canvas& c, const Font& f, const Model& m) {
    using namespace ui::theme;
    f.draw(c, Face::Semibold, 28, 28, detail::baseline(f, Face::Semibold, 28, 112, 34), "Achievements", text);
    const int total = m.achievements ? int(m.achievements->size()) : 0;
    int unlocked = 0;
    for (int i = 0; i < total; ++i) unlocked += (*m.achievements)[size_t(i)].unlocked;
    char sub[96];
    std::snprintf(sub, sizeof sub, "Steam \xC2\xB7 %d of %d unlocked", unlocked, total);
    f.draw(c, Face::Regular, 20, 28, detail::baseline(f, Face::Regular, 20, 150, 24), sub, textDim);
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
        f.draw(c, Face::Semibold, 22, 100, detail::baseline(f, Face::Semibold, 22, y + 14, 26), title, text);
        f.draw(c, Face::Regular, 18, 100, detail::baseline(f, Face::Regular, 18, y + 44, 22), desc, textDim);
        const std::string meta = a.unlocked && !a.earned.empty() ? a.earned : "Locked";
        detail::draw_right(c, f, Face::Regular, 18, kAchRight, detail::baseline(f, Face::Regular, 18, y + 16, 22),
                           meta, rgb(143, 143, 143));
    }
}

inline void draw_settings(Canvas& c, const Font& f, const Model& m) {
    using namespace ui::theme;
    // Row 0: FPS counter (88px high, icon + label + sub + toggle)
    const Rect row = rect(16, 120, 528, kSetRowH);
    if (m.sel == 0 && !m.busy) draw_focus_row(c, row, ui::theme::radiusCard);
    const uint32_t color = m.busy ? textMuted : text;
    icons::gauge(c, rect(32, 156, 16, 16), color, 2.0f);
    f.draw(c, Face::Semibold, 24, 68, detail::baseline(f, Face::Semibold, 24, 136, 29), "FPS counter", color);
    f.draw(c, Face::Regular, 18, 68, detail::baseline(f, Face::Regular, 18, 170, 22),
           "Show FPS in the top-left corner", textDim);
    draw_toggle(c, 476, 148, m.fpsOn);
    // Sub-row: frame-time graph, shown and reachable only while the counter is on.
    if (m.fpsOn) {
        const Rect sub = rect(48, 216, 496, 64);
        // the mockup draws no sub-row fill; only the toggle shows
        if (m.sel == 1 && !m.busy) c.stroke_round_rect(sub, ui::theme::radiusControl, ui::theme::ringWidth, focus,
                                                       ui::Align::Inside);
        f.draw(c, Face::Regular, 24, 100, detail::baseline(f, Face::Regular, 24, 233.5f, 29), "Frame-time graph",
               m.busy ? textMuted : text);
        draw_toggle(c, 476, 232, m.graphOn);
    }
}

// The whole guide card at its fixed position; `dimmed` pixels (the game frame
// under the 55% scrim) show through the rounded corners when provided.
// draw_guide_at draws at an arbitrary origin (the overlay renders the card
// into a card-sized canvas at (0,0); the tests draw at the mockup's screen
// position); draw_guide is the mockup-position form.
inline void draw_guide_at(Canvas& c, const Font& f, const Model& m, float ox, float oy,
                          const uint32_t* dimmed = nullptr) {
    using namespace ui::theme;
    const Rect card = rect(ox, oy, kCardW, kCardH);
    if (dimmed) {   // backdrop: the already-dimmed game frame, card-relative
        for (int y = 0; y < int(kCardH); ++y)
            std::copy_n(dimmed + size_t(y) * int(kCardW), int(kCardW),
                        c.data() + size_t(int(oy) + y) * c.width() + int(ox));
    } else {
        c.fill_rect(card.x, card.y, card.w, card.h, page);
    }
    c.fill_round_rect(card, kCardR, panel);
    // clip everything below to the card
    ui::Canvas::Clip saved = c.clip();
    c.set_clip(card);
    Canvas cardC(c.data() + size_t(int(oy)) * c.width() + int(ox), int(kCardW), int(kCardH), c.width());
    draw_tabs(cardC, f, m);
    if (m.tab == 0) draw_home(cardC, f, m);
    else if (m.tab == 1) draw_achievements(cardC, f, m);
    else draw_settings(cardC, f, m);
    if (!m.status.empty())
        f.draw(cardC, Face::Regular, 24, 28, 900 + f.ascent(Face::Regular, 24), m.status, textDim);
    draw_footer(cardC, f);
    c.set_clip(saved);
}
inline void draw_guide(Canvas& c, const Font& f, const Model& m, const uint32_t* dimmed = nullptr) {
    draw_guide_at(c, f, m, kCardX, kCardY, dimmed);
}

// Achievement toast: centred bottom pill, icon left, fixed label + name.
inline void draw_toast(Canvas& c, const Font& f, const Toast& t, float x = kToastX, float y = kToastY) {
    using namespace ui::theme;
    c.fill_round_rect(rect(x, y, kToastW, kToastH), kToastH * 0.5f, panel);
    c.stroke_round_rect(rect(x, y, kToastW, kToastH), kToastH * 0.5f, 1, control, ui::Align::Inside);
    const Rect icon = rect(x + 16, y + 16, 56, 56);
    if (t.icon && t.iconW > 0 && t.iconH > 0) {
        c.draw_image(Image{t.icon, t.iconW, t.iconH, t.iconW}, icon, ui::Fit::Cover, ui::theme::radiusTile);
    } else {
        c.fill_round_rect(icon, ui::theme::radiusTile, rgb(63, 91, 115));
        c.stroke_circle(x + 44, y + 44, 12, 4, rgb(214, 226, 234), ui::Align::Center);
    }
    f.draw(c, Face::Semibold, 22, x + 88, detail::baseline(f, Face::Semibold, 22, y + 18, 26),
           "Achievement unlocked", text);
    f.draw(c, Face::Regular, 18, x + 88, detail::baseline(f, Face::Regular, 18, y + 48, 22), t.title, textDim);
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
