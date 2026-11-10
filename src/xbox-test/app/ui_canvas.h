// SPDX-License-Identifier: GPL-3.0-or-later
// Platform-free CPU canvas: anti-aliased rounded rects, circles, rings and arcs, alpha fills,
// clipped bilinear image blits and AA polygons over a caller-owned 0xAARRGGBB buffer.
// No Win32/WinRT, no exceptions, no per-pixel allocation: the only heap use is a reusable
// coverage scratch buffer that grows once. The launcher redraws 1920x1080 on the CPU every frame,
// so full-screen fills take a memset/std::fill path and shapes touch only their own bounding box.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ui {

struct Rect { float x = 0, y = 0, w = 0, h = 0; };
constexpr Rect rect(float x, float y, float w, float h) { return Rect{x, y, w, h}; }
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

// ARGB32 pixels; stride is in pixels, not bytes.
struct Image { const uint32_t* px = nullptr; int w = 0, h = 0, stride = 0; };

enum class Fit { Stretch, Cover, Contain };
enum class Align { Center, Inside, Outside };

namespace detail {
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int iround(float v) { return static_cast<int>(std::floor(v + 0.5f)); }

// Signed distance to a rounded rectangle: negative inside, positive outside.
inline float sd_round_rect(float px, float py, const Rect& r, float rad) {
    float hx = r.w * 0.5f, hy = r.h * 0.5f;
    float qx = std::fabs(px - (r.x + hx)) - (hx - rad);
    float qy = std::fabs(py - (r.y + hy)) - (hy - rad);
    float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
    return std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0f) - rad;
}

inline float dist_to_segment(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay;
    float wx = px - ax, wy = py - ay;
    float len2 = vx * vx + vy * vy;
    float t = len2 > 0 ? clampf((wx * vx + wy * vy) / len2, 0.0f, 1.0f) : 0.0f;
    float dx = wx - vx * t, dy = wy - vy * t;
    return std::sqrt(dx * dx + dy * dy);
}
}  // namespace detail

class Canvas {
public:
    Canvas(uint32_t* px, int w, int h, int stride)
        : px_(px), w_(w), h_(h), stride_(stride), clip_{0, 0, w, h} {}

    struct Clip { int x0, y0, x1, y1; };

    int width() const { return w_; }
    int height() const { return h_; }
    uint32_t* data() { return px_; }
    const uint32_t* data() const { return px_; }

    Clip clip() const { return clip_; }
    void set_clip(Rect r) {
        int x0 = std::max(0, detail::iround(r.x)), y0 = std::max(0, detail::iround(r.y));
        int x1 = std::min(w_, detail::iround(r.x + r.w)), y1 = std::min(h_, detail::iround(r.y + r.h));
        clip_ = Clip{x0, y0, std::max(x0, x1), std::max(y0, y1)};
    }
    void set_clip(Clip c) { clip_ = Clip{std::max(0, c.x0), std::max(0, c.y0), std::min(w_, c.x1), std::min(h_, c.y1)}; }
    void reset_clip() { clip_ = Clip{0, 0, w_, h_}; }

    // ---- flat fills
    void fill(uint32_t c) { fill_rect(0, 0, float(w_), float(h_), c); }

    void fill_rect(float fx, float fy, float fw, float fh, uint32_t c) {
        int x0 = std::max(clip_.x0, detail::iround(fx)), x1 = std::min(clip_.x1, detail::iround(fx + fw));
        int y0 = std::max(clip_.y0, detail::iround(fy)), y1 = std::min(clip_.y1, detail::iround(fy + fh));
        if (x0 >= x1 || y0 >= y1) return;
        bool uniform = ((c >> 24) == ((c >> 16) & 0xFF)) && (((c >> 16) & 0xFF) == ((c >> 8) & 0xFF)) &&
                       (((c >> 8) & 0xFF) == (c & 0xFF));
        for (int y = y0; y < y1; ++y) {
            uint32_t* row = px_ + static_cast<std::size_t>(y) * stride_;
            if (uniform && x0 == 0 && x1 == w_) std::memset(row, c & 0xFF, std::size_t(w_) * 4);
            else std::fill(row + x0, row + x1, c);
        }
    }

    // Constant-alpha fill (scrims, dimmed art). Source alpha is ignored; `alpha` is 0..255.
    void fill_rect_alpha(float fx, float fy, float fw, float fh, uint32_t c, uint8_t alpha) {
        if (!alpha) return;
        int x0 = std::max(clip_.x0, detail::iround(fx)), x1 = std::min(clip_.x1, detail::iround(fx + fw));
        int y0 = std::max(clip_.y0, detail::iround(fy)), y1 = std::min(clip_.y1, detail::iround(fy + fh));
        for (int y = y0; y < y1; ++y) {
            uint32_t* row = px_ + static_cast<std::size_t>(y) * stride_;
            for (int x = x0; x < x1; ++x) row[x] = blend(row[x], c, alpha);
        }
    }

    void fill_round_rect(Rect r, float radius, uint32_t c) {
        if (r.w <= 0 || r.h <= 0) return;
        radius = detail::clampf(radius, 0, std::min(r.w, r.h) * 0.5f);
        if (radius < 0.01f) { fill_rect(r.x, r.y, r.w, r.h, c); return; }
        int x0 = std::max(clip_.x0, detail::iround(r.x)), x1 = std::min(clip_.x1, detail::iround(r.x + r.w));
        int y0 = std::max(clip_.y0, detail::iround(r.y)), y1 = std::min(clip_.y1, detail::iround(r.y + r.h));
        if (x0 >= x1) return;
        // At any row a rounded rect is one horizontal interval [xl, xr]; only its two edge pixels need AA.
        float cy0 = r.y + radius, cy1 = r.y + r.h - radius;
        for (int y = y0; y < y1; ++y) {
            float py = float(y) + 0.5f;
            float dy = py < cy0 ? cy0 - py : py > cy1 ? py - cy1 : 0.0f;
            float inset = dy > 0 ? radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy)) : 0.0f;
            float xl = r.x + inset, xr = r.x + r.w - inset;
            int a = std::max(x0, int(std::floor(xl))), b = std::min(x1, int(std::ceil(xr)));
            if (a >= b) continue;
            uint32_t* row = px_ + static_cast<std::size_t>(y) * stride_;
            int solid0 = std::max(a, int(std::ceil(xl))), solid1 = std::min(b, int(std::floor(xr)));
            if (solid0 < solid1) std::fill(row + solid0, row + solid1, c);
            for (int x = a; x < solid0; ++x) {
                unsigned cov = unsigned(detail::clampf(xl - float(x), 0, 1) * 255.0f + 0.5f);
                if (cov) row[x] = blend(row[x], c, cov);
            }
            for (int x = std::max(solid1, a); x < b; ++x) {
                unsigned cov = unsigned(detail::clampf(float(x + 1) - xr, 0, 1) * 255.0f + 0.5f);
                if (cov) row[x] = blend(row[x], c, cov);
            }
        }
    }

    // Anti-aliased ring drawn on the shape's edge; `align` picks which side of the edge it occupies.
    void stroke_round_rect(Rect r, float radius, float width, uint32_t c, Align align = Align::Center) {
        if (r.w <= 0 || r.h <= 0 || width <= 0) return;
        radius = detail::clampf(radius, 0, std::min(r.w, r.h) * 0.5f);
        float half = width * 0.5f;
        // the stroke can reach one width past the shape's bounds (Center/Outside align),
        // so cull against the padded box, not the shape itself
        const Rect pad{r.x - width, r.y - width, r.w + width * 2, r.h + width * 2};
        aa_rect(pad, [&](float px, float py) {
            float sd = detail::sd_round_rect(px, py, r, radius), d;
            switch (align) {
                // Inside/Outside: signed distance to the band's far edge — negative
                // inside the band (full coverage), positive past it
                case Align::Inside:  d = std::max(sd, -width - sd); break;
                case Align::Outside: d = std::max(-sd, sd - width); break;
                default:             d = std::fabs(sd) - half; break;
            }
            return detail::clampf(0.5f - d, 0, 1);
        }, c);
    }

    void fill_circle(float cx, float cy, float radius, uint32_t c, uint8_t alpha = 255) {
        if (radius <= 0 || !alpha) return;
        aa_rect(Rect{cx - radius, cy - radius, radius * 2, radius * 2}, [&](float px, float py) {
            float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy)) - radius;
            return detail::clampf(0.5f - d, 0, 1) * (alpha / 255.0f);
        }, c);
    }

    void stroke_circle(float cx, float cy, float radius, float width, uint32_t c, Align align = Align::Center,
                       uint8_t alpha = 255) {
        if (radius <= 0 || width <= 0) return;
        float half = width * 0.5f, inner = radius - width, outer = radius;
        aa_rect(Rect{cx - radius, cy - radius, radius * 2, radius * 2}, [&](float px, float py) {
            float d = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
            float e;
            switch (align) {
                // Inside/Outside: signed distance to the band's far edge — negative
                // inside the band (full coverage), positive past it
                case Align::Inside:  e = std::max(d - outer, inner - d); break;
                case Align::Outside: e = std::max(d - outer - width, outer - d); break;
                default:             e = std::fabs(d - radius) - half; break;
            }
            return detail::clampf(0.5f - e, 0, 1) * (alpha / 255.0f);
        }, c);
    }

    // Ring segment: band from `radius - thickness` to `radius`, `sweep` radians clockwise from `start`.
    void fill_donut(float cx, float cy, float radius, float thickness, float start, float sweep, uint32_t c) {
        if (radius <= 0 || thickness <= 0 || sweep <= 0) return;
        float inner = std::max(0.0f, radius - thickness);
        aa_rect(Rect{cx - radius, cy - radius, radius * 2, radius * 2}, [&](float px, float py) {
            float dx = px - cx, dy = py - cy, d = std::sqrt(dx * dx + dy * dy);
            float e = std::max(d - radius, inner - d);
            float cov = detail::clampf(0.5f - e, 0, 1);
            if (cov <= 0 || sweep >= kTau - 1e-4f) return cov;
            float a = std::atan2(dy, dx) - start;
            a -= kTau * std::floor(a / kTau);  // [0, 2pi)
            float inside = std::min(a, sweep - a) * d;  // px inside the angular band, negative outside
            return cov * detail::clampf(0.5f + inside, 0, 1);
        }, c);
    }

    // ---- vector paths (icons, controller glyphs)
    void stroke_polyline(const float* xy, int count, bool closed, float width, uint32_t c) {
        if (count < 2 || width <= 0) return;
        float half = width * 0.5f;
        float x0 = xy[0], y0 = xy[1], x1 = x0, y1 = y0;
        for (int i = 1; i < count; ++i) {
            x0 = std::min(x0, xy[i * 2]); x1 = std::max(x1, xy[i * 2]);
            y0 = std::min(y0, xy[i * 2 + 1]); y1 = std::max(y1, xy[i * 2 + 1]);
        }
        int bx0 = std::max(clip_.x0, detail::iround(x0 - half - 1)), bx1 = std::min(clip_.x1, detail::iround(x1 + half + 2));
        int by0 = std::max(clip_.y0, detail::iround(y0 - half - 1)), by1 = std::min(clip_.y1, detail::iround(y1 + half + 2));
        if (bx0 >= bx1 || by0 >= by1) return;
        int bw = bx1 - bx0, bh = by1 - by0;
        cov_.assign(std::size_t(bw) * bh, 0);
        int segs = closed ? count : count - 1;
        for (int s = 0; s < segs; ++s) {
            int a = s, b = (s + 1) % count;
            float ax = xy[a * 2], ay = xy[a * 2 + 1], bx = xy[b * 2], by = xy[b * 2 + 1];
            int sx0 = std::max(bx0, detail::iround(std::min(ax, bx) - half - 1));
            int sx1 = std::min(bx1, detail::iround(std::max(ax, bx) + half + 2));
            int sy0 = std::max(by0, detail::iround(std::min(ay, by) - half - 1));
            int sy1 = std::min(by1, detail::iround(std::max(ay, by) + half + 2));
            for (int y = sy0; y < sy1; ++y)
                for (int x = sx0; x < sx1; ++x) {
                    float d = detail::dist_to_segment(float(x) + 0.5f, float(y) + 0.5f, ax, ay, bx, by);
                    float v = detail::clampf(half + 0.5f - d, 0, 1);
                    unsigned char& o = cov_[std::size_t(y - by0) * bw + (x - bx0)];
                    unsigned char n = static_cast<unsigned char>(v * 255.0f + 0.5f);
                    if (n > o) o = n;
                }
        }
        blend_cov_buffer(bx0, by0, bw, bh, c, 255);
    }

    // Even-odd-free (nonzero) polygon fill with 4x4 supersampling; meant for small icon shapes.
    void fill_polygon(const float* xy, int count, uint32_t c) {
        if (count < 3) return;
        float x0 = xy[0], y0 = xy[1], x1 = x0, y1 = y0;
        for (int i = 1; i < count; ++i) {
            x0 = std::min(x0, xy[i * 2]); x1 = std::max(x1, xy[i * 2]);
            y0 = std::min(y0, xy[i * 2 + 1]); y1 = std::max(y1, xy[i * 2 + 1]);
        }
        int bx0 = std::max(clip_.x0, detail::iround(x0)), bx1 = std::min(clip_.x1, detail::iround(x1) + 1);
        int by0 = std::max(clip_.y0, detail::iround(y0)), by1 = std::min(clip_.y1, detail::iround(y1) + 1);
        if (bx0 >= bx1 || by0 >= by1) return;
        int bw = bx1 - bx0, bh = by1 - by0;
        cov_.assign(std::size_t(bw) * bh, 0);
        static const float off[4] = {0.125f, 0.375f, 0.625f, 0.875f};
        for (int y = by0; y < by1; ++y)
            for (int x = bx0; x < bx1; ++x) {
                int hits = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        float px = float(x) + off[sx], py = float(y) + off[sy];
                        int wind = 0;
                        for (int i = 0; i < count; ++i) {
                            int j = (i + 1) % count;
                            float ax = xy[i * 2], ay = xy[i * 2 + 1], bx = xy[j * 2], by = xy[j * 2 + 1];
                            if (ay <= py) { if (by > py && (bx - ax) * (py - ay) - (px - ax) * (by - ay) > 0) ++wind; }
                            else if (by <= py && (bx - ax) * (py - ay) - (px - ax) * (by - ay) < 0) --wind;
                        }
                        if (wind != 0) ++hits;
                    }
                cov_[std::size_t(y - by0) * bw + (x - bx0)] = static_cast<unsigned char>(hits * 255 / 16);
            }
        blend_cov_buffer(bx0, by0, bw, bh, c, 255);
    }

    // ---- glyph bitmaps (used by ui_font.h)
    void blend_mask(int x, int y, int w, int h, const uint8_t* alpha, int stride, uint32_t c, uint8_t opacity = 255) {
        if (!alpha || w <= 0 || h <= 0 || !opacity) return;
        int x0 = std::max(clip_.x0, x), x1 = std::min(clip_.x1, x + w);
        int y0 = std::max(clip_.y0, y), y1 = std::min(clip_.y1, y + h);
        for (int py = y0; py < y1; ++py) {
            uint32_t* row = px_ + static_cast<std::size_t>(py) * stride_;
            const uint8_t* arow = alpha + static_cast<std::size_t>(py - y) * stride + (x0 - x);
            for (int px = x0; px < x1; ++px) {
                unsigned a = unsigned(arow[px - x0]) * opacity / 255u;
                if (a) row[px] = blend(row[px], c, a);
            }
        }
    }

    // ---- image blit: bilinear, optional rounded-corner mask, Cover crops the source, Contain letterboxes.
    void draw_image(const Image& img, Rect dst, Fit fit = Fit::Cover, float radius = 0, uint8_t opacity = 255) {
        if (!img.px || img.w <= 0 || img.h <= 0 || dst.w <= 0 || dst.h <= 0 || !opacity) return;
        float sx = 0, sy = 0, sw = float(img.w), sh = float(img.h);
        Rect d = dst;
        if (fit == Fit::Cover) {
            float scale = std::max(dst.w / float(img.w), dst.h / float(img.h));
            sw = std::min(float(img.w), dst.w / scale);
            sh = std::min(float(img.h), dst.h / scale);
            sx = (float(img.w) - sw) * 0.5f;
            sy = (float(img.h) - sh) * 0.5f;
        } else if (fit == Fit::Contain) {
            float scale = std::min(dst.w / float(img.w), dst.h / float(img.h));
            d.w = float(img.w) * scale;
            d.h = float(img.h) * scale;
            d.x = dst.x + (dst.w - d.w) * 0.5f;
            d.y = dst.y + (dst.h - d.h) * 0.5f;
        }
        radius = detail::clampf(radius, 0, std::min(d.w, d.h) * 0.5f);
        int x0 = std::max(clip_.x0, detail::iround(d.x)), x1 = std::min(clip_.x1, detail::iround(d.x + d.w));
        int y0 = std::max(clip_.y0, detail::iround(d.y)), y1 = std::min(clip_.y1, detail::iround(d.y + d.h));
        if (x0 >= x1 || y0 >= y1) return;
        // 16.16 fixed-point source coordinates: no float work per pixel.
        const int64_t du = int64_t(double(sw) / d.w * 65536.0), dv = int64_t(double(sh) / d.h * 65536.0);
        const int64_t u0 = int64_t((double(sx) + (x0 + 0.5 - d.x) / d.w * sw - 0.5) * 65536.0);
        const int64_t v0 = int64_t((double(sy) + (y0 + 0.5 - d.y) / d.h * sh - 0.5) * 65536.0);
        const bool exact = std::fabs(sw - d.w) < 1e-3f && std::fabs(sh - d.h) < 1e-3f;
        for (int y = y0; y < y1; ++y) {
            int64_t v = v0 + int64_t(y - y0) * dv;
            int iy = int(v >> 16), fy = int((v >> 8) & 0xFF);
            iy = std::min(std::max(iy, 0), img.h - 1);
            int iy1 = std::min(iy + 1, img.h - 1);
            if (fy == 0) iy1 = iy;
            const uint32_t* r0 = img.px + std::size_t(iy) * img.stride;
            const uint32_t* r1 = img.px + std::size_t(iy1) * img.stride;
            uint32_t* row = px_ + static_cast<std::size_t>(y) * stride_;
            float qy = 0;
            const bool mask = radius > 0.01f;
            if (mask) {
                float py = float(y) + 0.5f;
                qy = std::fabs(py - (d.y + d.h * 0.5f)) - (d.h * 0.5f - radius);
            }
            for (int x = x0; x < x1; ++x) {
                unsigned cov = 255;
                if (mask) {
                    float px = float(x) + 0.5f;
                    float qx = std::fabs(px - (d.x + d.w * 0.5f)) - (d.w * 0.5f - radius);
                    if (qx <= 0 && qy <= 0) {  // well inside the rounded shape: no distance work
                        cov = 255;
                    } else {
                        float ax = qx > 0 ? qx : 0, ay = qy > 0 ? qy : 0;
                        float sd = std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0f) - radius;
                        cov = unsigned(detail::clampf(0.5f - sd, 0, 1) * 255.0f + 0.5f);
                        if (!cov) continue;
                    }
                }
                int64_t u = u0 + int64_t(x - x0) * du;
                uint32_t s;
                if (exact) {
                    int ix = std::min(std::max(int(u >> 16), 0), img.w - 1);
                    s = r0[ix];
                } else {
                    int ix = int(u >> 16), fx = int((u >> 8) & 0xFF);
                    ix = std::min(std::max(ix, 0), img.w - 1);
                    int ix1 = std::min(ix + 1, img.w - 1);
                    if (fx == 0) ix1 = ix;
                    unsigned w00 = unsigned((256 - fx) * (256 - fy)), w01 = unsigned(fx * (256 - fy));
                    unsigned w10 = unsigned((256 - fx) * fy), w11 = unsigned(fx * fy);
                    s = 0xFF000000u | (bilinear_channel(r0[ix], r0[ix1], r1[ix], r1[ix1], w00, w01, w10, w11, 16) << 16) |
                        (bilinear_channel(r0[ix], r0[ix1], r1[ix], r1[ix1], w00, w01, w10, w11, 8) << 8) |
                        bilinear_channel(r0[ix], r0[ix1], r1[ix], r1[ix1], w00, w01, w10, w11, 0);
                }
                unsigned a = (s >> 24) * cov / 255u;
                a = a * opacity / 255u;
                if (a) row[x] = blend(row[x], s, a);
            }
        }
    }

    static uint32_t blend(uint32_t d, uint32_t s, unsigned a) {
        if (a == 0) return d;
        if (a >= 255) return s | 0xFF000000u;
        unsigned ia = 255 - a;
        unsigned r = (((s >> 16) & 0xFF) * a + ((d >> 16) & 0xFF) * ia) / 255;
        unsigned g = (((s >> 8) & 0xFF) * a + ((d >> 8) & 0xFF) * ia) / 255;
        unsigned b = ((s & 0xFF) * a + (d & 0xFF) * ia) / 255;
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }

private:
    template <class Cov>
    void aa_rect(Rect r, Cov cov, uint32_t c) {
        int x0 = std::max(clip_.x0, detail::iround(r.x)), x1 = std::min(clip_.x1, detail::iround(r.x + r.w));
        int y0 = std::max(clip_.y0, detail::iround(r.y)), y1 = std::min(clip_.y1, detail::iround(r.y + r.h));
        for (int y = y0; y < y1; ++y) {
            uint32_t* row = px_ + static_cast<std::size_t>(y) * stride_;
            for (int x = x0; x < x1; ++x) {
                float v = cov(float(x) + 0.5f, float(y) + 0.5f);
                if (v <= 0) continue;
                unsigned a = static_cast<unsigned>(v * 255.0f + 0.5f);
                if (a) row[x] = blend(row[x], c, a);
            }
        }
    }

    void blend_cov_buffer(int bx0, int by0, int bw, int bh, uint32_t c, uint8_t opacity) {
        for (int y = 0; y < bh; ++y) {
            uint32_t* row = px_ + static_cast<std::size_t>(by0 + y) * stride_;
            const uint8_t* cov = cov_.data() + std::size_t(y) * bw;
            for (int x = 0; x < bw; ++x) {
                unsigned a = cov[x] * opacity / 255u;
                if (a) row[bx0 + x] = blend(row[bx0 + x], c, a);
            }
        }
    }

    // One channel of a 2x2 bilinear sample; weights are 0..255 per axis and sum to 65536.
    static unsigned bilinear_channel(uint32_t p00, uint32_t p10, uint32_t p01, uint32_t p11, unsigned w00, unsigned w10,
                                     unsigned w01, unsigned w11, int shift) {
        unsigned v = ((p00 >> shift) & 0xFF) * w00 + ((p10 >> shift) & 0xFF) * w10 + ((p01 >> shift) & 0xFF) * w01 +
                     ((p11 >> shift) & 0xFF) * w11;
        return (v + 32768u) >> 16;
    }

    uint32_t* px_;
    int w_, h_, stride_;
    Clip clip_;
    std::vector<uint8_t> cov_;
};

}  // namespace ui
