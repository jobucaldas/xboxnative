// SPDX-License-Identifier: GPL-3.0-or-later
// Controller glyphs and the three icon-only icons, drawn from ui::Canvas primitives.
// Geometry is the mockups' vector source (.mockups/*/figcraft) normalised to a unit box and
// scaled to the requested rect, so nothing here depends on an icon font or an image.
#pragma once
#include <cmath>

#include "ui_canvas.h"
#include "ui_font.h"

namespace ui {
namespace glyphs {

// ---- helpers
namespace detail {
// Flattens a cubic Bezier into `out` (x,y pairs), excluding the start point.
inline void cubic(float* out, int& n, float x0, float y0, float x1, float y1, float x2, float y2, float x3,
                  float y3, int steps = 12) {
    for (int i = 1; i <= steps; ++i) {
        float t = float(i) / float(steps), u = 1 - t;
        float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        out[n * 2] = a * x0 + b * x1 + c * x2 + d * x3;
        out[n * 2 + 1] = a * y0 + b * y1 + c * y2 + d * y3;
        ++n;
    }
}
// Maps a point from a design box (dw x dh) into `r`.
inline void map(const Rect& r, float dw, float dh, float x, float y, float& ox, float& oy) {
    ox = r.x + x / dw * r.w;
    oy = r.y + y / dh * r.h;
}
}  // namespace detail

// A/B/X/Y: filled disc with the letter centred. `disc` is the fill, `letter` the text colour.
inline void face_button(Canvas& c, float cx, float cy, float diameter, char letter, const Font& font,
                        uint32_t disc, uint32_t letterColor) {
    float r = diameter * 0.5f;
    c.fill_circle(cx, cy, r, disc);
    std::string s(1, letter);
    float px = diameter * 0.62f;
    float ix0, iy0, ix1, iy1;   // centre the visible letter (its ink box) on the disc, not the line box
    if (font.ink_box(Face::Bold, px, s, ix0, iy0, ix1, iy1))
        font.draw(c, Face::Bold, px, cx - (ix0 + ix1) * 0.5f, cy - (iy0 + iy1) * 0.5f, s, letterColor);
}

// Menu (hamburger): ring with three short lines.
inline void menu(Canvas& c, float cx, float cy, float diameter, uint32_t ring, uint32_t lines) {
    float r = diameter * 0.5f;
    c.stroke_circle(cx, cy, r, std::max(2.0f, diameter * 0.077f), ring, Align::Inside);
    float lw = diameter * 0.46f, lh = std::max(1.5f, diameter * 0.077f);
    float x = cx - lw * 0.5f;
    for (int i = 0; i < 3; ++i) c.fill_round_rect(rect(x, cy - diameter * 0.19f + float(i) * diameter * 0.19f, lw, lh), lh * 0.5f, lines);
}

// View (two overlapping rounded squares): the second window sits up-right of the first.
inline void view(Canvas& c, float cx, float cy, float size, uint32_t color, float strokePx) {
    float s = size * 0.56f, gap = size * 0.10f;
    c.stroke_round_rect(rect(cx - size * 0.5f, cy - size * 0.5f + gap, s, s), size * 0.08f, strokePx, color, Align::Inside);
    c.stroke_round_rect(rect(cx - size * 0.5f + s - gap, cy - size * 0.5f, s, s), size * 0.08f, strokePx, color, Align::Inside);
}

// Trash can: lid, handle, tapered body and two ribs. Design box 24x24, 2px stroke.
inline void trash(Canvas& c, Rect r, uint32_t color, float strokePx = 2.0f) {
    const float dw = 24, dh = 24;
    float p[2 * 32];
    auto pt = [&](int i, float x, float y) { detail::map(r, dw, dh, x, y, p[i * 2], p[i * 2 + 1]); };
    int n = 0;
    pt(n++, 0, 4); pt(n++, 24, 4);
    c.stroke_polyline(p, n, false, strokePx, color);
    n = 0; pt(n++, 8, 4); pt(n++, 8, 0); pt(n++, 16, 0); pt(n++, 16, 4);
    c.stroke_polyline(p, n, false, strokePx, color);
    n = 0; pt(n++, 3, 4); pt(n++, 4.5f, 24); pt(n++, 19.5f, 24); pt(n++, 21, 4);
    c.stroke_polyline(p, n, false, strokePx, color);
    n = 0; pt(n++, 9, 9); pt(n++, 9, 19);
    c.stroke_polyline(p, n, false, strokePx, color);
    n = 0; pt(n++, 15, 9); pt(n++, 15, 19);
    c.stroke_polyline(p, n, false, strokePx, color);
}

// Cloud: one closed Bezier outline. Design box 28x20, 2px stroke.
inline void cloud(Canvas& c, Rect r, uint32_t color, float strokePx = 2.0f) {
    const float dw = 28, dh = 20;
    float p[2 * 96];  // 2 start points + 6 cubics at 12 steps each
    int n = 0;
    p[n * 2] = 6; p[n * 2 + 1] = 20; ++n;
    p[n * 2] = 20; p[n * 2 + 1] = 20; ++n;
    // (20,20) -> (28,13) -> (22,6.2) -> (13.6,0) -> (5.4,5.6) -> (0,13) -> (6,20)
    detail::cubic(p, n, 20, 20, 24.4f, 20, 28, 16.8f, 28, 13);
    detail::cubic(p, n, 28, 13, 28, 9.4f, 25.4f, 6.6f, 22, 6.2f);
    detail::cubic(p, n, 22, 6.2f, 21.2f, 2.0f, 17.8f, 0, 13.6f, 0);
    detail::cubic(p, n, 13.6f, 0, 9.6f, 0, 6.4f, 2.2f, 5.4f, 5.6f);
    detail::cubic(p, n, 5.4f, 5.6f, 2.0f, 6.2f, 0, 9.0f, 0, 13);
    detail::cubic(p, n, 0, 13, 0, 16.8f, 2.6f, 20, 6, 20);
    for (int i = 0; i < n; ++i) detail::map(r, dw, dh, p[i * 2], p[i * 2 + 1], p[i * 2], p[i * 2 + 1]);
    c.stroke_polyline(p, n, true, strokePx, color);
}

// Cog: four teeth, a ring and a hub. Design box 34x34, 2px stroke.
inline void cog(Canvas& c, Rect r, uint32_t color, float strokePx = 2.0f) {
    const float dw = 34, dh = 34;
    float p[8];
    auto seg = [&](float x0, float y0, float x1, float y1) {
        detail::map(r, dw, dh, x0, y0, p[0], p[1]);
        detail::map(r, dw, dh, x1, y1, p[2], p[3]);
        c.stroke_polyline(p, 2, false, strokePx, color);
    };
    seg(17, 0, 17, 5); seg(17, 29, 17, 34);
    seg(0, 17, 5, 17); seg(29, 17, 34, 17);
    seg(25.5f, 8.5f, 28.3f, 5.7f); seg(25.5f, 25.5f, 28.3f, 28.3f);
    seg(8.5f, 25.5f, 5.7f, 28.3f); seg(8.5f, 8.5f, 5.7f, 5.7f);
    float cx, cy;
    detail::map(r, dw, dh, 17, 17, cx, cy);
    float scale = std::min(r.w / dw, r.h / dh);
    c.stroke_circle(cx, cy, 12 * scale, strokePx, color, Align::Inside);
    c.stroke_circle(cx, cy, 5 * scale, strokePx, color, Align::Inside);
}

// ---- prompt row: a disc plus its label, as in the bottom hint bar
inline float prompt(Canvas& c, float x, float y, const Font& font, char letter, const std::string& label, float diameter,
                    uint32_t disc, uint32_t letterColor, uint32_t labelColor, float labelPx, float gap = 10) {
    face_button(c, x + diameter * 0.5f, y + diameter * 0.5f, diameter, letter, font, disc, letterColor);
    float lx = x + diameter + gap;
    float baseline = y + diameter * 0.5f + (font.ascent(Face::Regular, labelPx) - font.descent(Face::Regular, labelPx)) * 0.5f;
    return lx + font.draw(c, Face::Regular, labelPx, lx, baseline, label, labelColor) - x;
}

}  // namespace glyphs
}  // namespace ui
