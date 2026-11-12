// SPDX-License-Identifier: GPL-3.0-or-later
// TrueType text for ui::Canvas: three faces (regular, semibold, bold) rasterised through
// stb_truetype (public domain / MIT, fetched by src/tools/fetch-ui-assets.sh, never committed)
// into a per-size glyph cache, plus measure / ellipsize / word-wrap helpers. UTF-8 input;
// codepoints the face lacks fall back to '?'.
//
// The caller owns the font bytes and must keep them alive while the Font is in use:
//     ui::Font font;
//     font.load(ui::Face::Regular, regular_ttf.data(), regular_ttf.size());
// Header-only and platform-free; stb_truetype is compiled static into each including TU.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui_canvas.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4505)
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace ui {

enum class Face : int { Regular = 0, Semibold = 1, Bold = 2 };

// Decodes one UTF-8 codepoint; returns the number of bytes consumed (>= 1). Invalid input yields U+FFFD.
inline int utf8_next(const char*& p, const char* end, uint32_t& cp) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(p);
    int n = 1;
    if (s[0] < 0x80) {
        cp = s[0];
    } else if ((s[0] & 0xE0) == 0xC0) {
        cp = s[0] & 0x1F; n = 2;
    } else if ((s[0] & 0xF0) == 0xE0) {
        cp = s[0] & 0x0F; n = 3;
    } else if ((s[0] & 0xF8) == 0xF0) {
        cp = s[0] & 0x07; n = 4;
    } else {
        cp = 0xFFFD;
    }
    if (n > 1) {
        if (p + n > end) { cp = 0xFFFD; n = 1; }
        else {
            for (int i = 1; i < n; ++i) {
                if ((s[i] & 0xC0) != 0x80) { cp = 0xFFFD; n = 1; break; }
                cp = (cp << 6) | (s[i] & 0x3F);
            }
        }
    }
    p += n;
    return n;
}

inline std::size_t utf8_count(const std::string& s) {
    const char* p = s.data(); const char* end = p + s.size();
    std::size_t n = 0;
    while (p < end) { uint32_t cp; utf8_next(p, end, cp); ++n; }
    return n;
}

class Font {
public:
    Font() = default;
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // `data` must outlive this Font (stb_truetype reads it on demand).
    bool load(Face face, const void* data, std::size_t size) {
        if (!data || size < 12) return false;
        FaceData& f = faces_[static_cast<int>(face)];
        f.data = static_cast<const unsigned char*>(data);
        f.ok = stbtt_InitFont(&f.info, f.data, stbtt_GetFontOffsetForIndex(f.data, 0)) != 0;
        glyphs_.clear();
        return f.ok;
    }

    bool ready() const { return faces_[0].ok && faces_[1].ok && faces_[2].ok; }
    bool ready(Face f) const { return faces_[static_cast<int>(f)].ok; }

    void clear_cache() { glyphs_.clear(); }

    // ---- metrics (pixel size = em size, as in CSS)
    float line_height(Face face, float px) const {
        const FaceData& f = faces_[static_cast<int>(face)];
        if (!f.ok) return px * 1.2f;
        int a, d, g;
        stbtt_GetFontVMetrics(&f.info, &a, &d, &g);
        return float(a - d + g) * scale_of(f, px);
    }
    float ascent(Face face, float px) const {
        const FaceData& f = faces_[static_cast<int>(face)];
        if (!f.ok) return px * 0.8f;
        int a, d, g;
        stbtt_GetFontVMetrics(&f.info, &a, &d, &g);
        (void)d; (void)g;
        return float(a) * scale_of(f, px);
    }
    float descent(Face face, float px) const {
        const FaceData& f = faces_[static_cast<int>(face)];
        if (!f.ok) return px * 0.2f;
        int a, d, g;
        stbtt_GetFontVMetrics(&f.info, &a, &d, &g);
        (void)a; (void)g;
        return float(-d) * scale_of(f, px);
    }

    // Ink box of the first codepoint of `s` relative to its pen origin and baseline (x right, y down,
    // so a cap letter has y0 < 0): lets glyph prompts centre the visible letter, not its line box.
    bool ink_box(Face face, float px, const std::string& s, float& x0, float& y0, float& x1, float& y1) const {
        const FaceData& f = faces_[static_cast<int>(face)];
        if (!f.ok || s.empty()) return false;
        const char* p = s.data(); uint32_t cp; utf8_next(p, s.data() + s.size(), cp);
        int bx0, by0, bx1, by1;
        if (!stbtt_GetGlyphBox(&f.info, glyph_index(f, cp), &bx0, &by0, &bx1, &by1)) return false;
        float sc = scale_of(f, px);
        x0 = float(bx0) * sc; x1 = float(bx1) * sc; y0 = -float(by1) * sc; y1 = -float(by0) * sc;
        return true;
    }

    // Ink box of the WHOLE string relative to the pen origin (x) and baseline (y):
    // unions every glyph's bitmap box, so centring with it centres the real label
    // ink (ink_box is deliberately the first codepoint only, for glyph prompts).
    bool string_ink_box(Face face, float px, const std::string& s, float& x0, float& y0, float& x1, float& y1) const {
        const FaceData& f = faces_[static_cast<int>(face)];
        if (!f.ok || s.empty()) return false;
        float scale = scale_of(f, px), pen = 0;
        float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
        const char* p = s.data(); const char* end = p + s.size();
        uint32_t prev = 0;
        bool any = false;
        while (p < end) {
            uint32_t cp; utf8_next(p, end, cp);
            const Glyph& g = glyph(face, px, cp);
            if (prev) pen += float(stbtt_GetGlyphKernAdvance(&f.info, int(prev), glyph_index(f, prev))) * scale;
            if (g.w > 0 && g.h > 0) {
                bx0 = bx0 < pen + g.xoff ? bx0 : pen + g.xoff;
                bx1 = bx1 > pen + g.xoff + g.w ? bx1 : pen + g.xoff + g.w;
                by0 = by0 < g.yoff ? by0 : float(g.yoff);
                by1 = by1 > g.yoff + g.h ? by1 : float(g.yoff + g.h);
                any = true;
            }
            pen += g.adv;
            prev = cp;
        }
        if (!any) return false;
        x0 = bx0; x1 = bx1; y0 = by0; y1 = by1;
        return true;
    }

    // Advance width of `s`, including pair kerning.
    float measure(Face face, float px, const std::string& s, float ls = 0.0f) const {
        return measure(face, px, s.data(), s.size(), ls);
    }
    // ls: extra advance between characters (Figma letter spacing, pixels). The mockup
    // applies it between glyphs only — n characters get n-1 gaps — so centers match.
    float measure(Face face, float px, const char* s, std::size_t len, float ls = 0.0f) const {
        const FaceData& f = faces_[static_cast<int>(face)];
        if (!f.ok) return float(len) * px * 0.5f;
        float scale = scale_of(f, px), x = 0;
        const char* p = s; const char* end = s + len;
        uint32_t prev = 0;
        while (p < end) {
            uint32_t cp; utf8_next(p, end, cp);
            int gi = stbtt_FindGlyphIndex(&f.info, int(cp));
            if (!gi) gi = stbtt_FindGlyphIndex(&f.info, '?');
            int adv, lsb;
            stbtt_GetGlyphHMetrics(&f.info, gi, &adv, &lsb);
            (void)lsb;
            x += float(adv) * scale;
            if (p < end) x += ls;
            if (prev) x += float(stbtt_GetGlyphKernAdvance(&f.info, int(prev), gi)) * scale;
            prev = cp;
        }
        return x;
    }

    // Draws `s` with its baseline at (x, y). Returns the advance width.
    float draw(Canvas& c, Face face, float px, float x, float y, const std::string& s, uint32_t color,
               uint8_t opacity = 255, float ls = 0.0f) const {
        return draw(c, face, px, x, y, s.data(), s.size(), color, opacity, ls);
    }
    float draw(Canvas& c, Face face, float px, float x, float y, const char* s, std::size_t len, uint32_t color,
               uint8_t opacity = 255, float ls = 0.0f) const {
        const FaceData& f = faces_[static_cast<int>(face)];
        if (!f.ok) return 0;
        float scale = scale_of(f, px), pen = x;
        const char* p = s; const char* end = s + len;
        uint32_t prev = 0;
        while (p < end) {
            uint32_t cp; utf8_next(p, end, cp);
            const Glyph& g = glyph(face, px, cp);
            if (prev) pen += float(stbtt_GetGlyphKernAdvance(&f.info, int(prev), glyph_index(f, prev))) * scale;
            if (g.w > 0 && g.h > 0)
                c.blend_mask(int(std::floor(pen + 0.5f)) + g.xoff, int(std::floor(y + 0.5f)) + g.yoff, g.w, g.h,
                             g.bmp.data(), g.w, color, opacity);
            pen += g.adv;
            if (p < end) pen += ls;   // Figma letter spacing: between glyphs only
            prev = cp;
        }
        return pen - x;
    }

    // Shortens `s` with an ellipsis so it measures at most `maxw`. Returns `s` unchanged if it fits.
    std::string ellipsize(Face face, float px, const std::string& s, float maxw,
                          const char* ellipsis = "\xE2\x80\xA6") const {
        if (maxw <= 0) return std::string();
        if (measure(face, px, s) <= maxw) return s;
        float ew = measure(face, px, ellipsis, std::char_traits<char>::length(ellipsis));
        if (ew > maxw) return std::string();
        // Walk codepoints, keeping the longest prefix that fits with the ellipsis.
        std::string out;
        const char* p = s.data(); const char* end = p + s.size();
        while (p < end) {
            const char* start = p;
            uint32_t cp; int n = utf8_next(p, end, cp);
            std::string cand = out + std::string(start, std::size_t(n));
            if (measure(face, px, cand + ellipsis) > maxw) break;
            out.swap(cand);
        }
        // Do not leave a dangling space before the ellipsis.
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out + ellipsis;
    }

    // Greedy word wrap to `maxw`; explicit '\n' starts a new line. max_lines > 0 truncates the last
    // kept line with an ellipsis.
    std::vector<std::string> wrap(Face face, float px, const std::string& s, float maxw, int max_lines = 0) const {
        std::vector<std::string> lines;
        std::string line;
        auto flush = [&]() { lines.push_back(line); line.clear(); };
        const char* p = s.data(); const char* end = p + s.size();
        std::string word;
        auto push_word = [&]() {
            if (word.empty()) return;
            std::string cand = line.empty() ? word : line + " " + word;
            if (measure(face, px, cand) <= maxw || line.empty()) {
                line.swap(cand);
                if (measure(face, px, line) > maxw) {  // a single word longer than the line: break by character
                    std::string rest = line;
                    line.clear();
                    const char* q = rest.data(); const char* qe = q + rest.size();
                    while (q < qe) {
                        const char* st = q;
                        uint32_t cp; int n = utf8_next(q, qe, cp);
                        std::string next = line + std::string(st, std::size_t(n));
                        if (!line.empty() && measure(face, px, next) > maxw) { flush(); line = std::string(st, std::size_t(n)); }
                        else line.swap(next);
                    }
                }
            } else {
                flush();
                line = word;
            }
            word.clear();
        };
        while (p < end) {
            const char* st = p;
            uint32_t cp; int n = utf8_next(p, end, cp);
            if (cp == '\n') { push_word(); flush(); continue; }
            if (cp == ' ') { push_word(); continue; }
            word.append(st, std::size_t(n));
        }
        push_word();
        if (!line.empty() || lines.empty()) flush();
        if (max_lines > 0 && int(lines.size()) > max_lines) {
            lines.resize(std::size_t(max_lines));
            lines.back() = ellipsize(face, px, lines.back() + "\xE2\x80\xA6", maxw);
            if (lines.back().size() > 3 && lines.back().substr(lines.back().size() - 3) == "\xE2\x80\xA6\xE2\x80\xA6")
                lines.back().resize(lines.back().size() - 3);
        }
        return lines;
    }

    // Draws wrapped text, first baseline at (x, y); returns the height consumed.
    float draw_wrapped(Canvas& c, Face face, float px, float x, float y, float maxw, const std::string& s,
                       uint32_t color, int max_lines = 0, uint8_t opacity = 255) const {
        return draw_wrapped_lh(c, face, px, x, y, maxw, s, color, line_height(face, px), max_lines, opacity);
    }
    // Same, with the mockup's explicit line height (the .figcraft text styles set one per node).
    float draw_wrapped_lh(Canvas& c, Face face, float px, float x, float y, float maxw, const std::string& s,
                          uint32_t color, float lh, int max_lines = 0, uint8_t opacity = 255) const {
        std::vector<std::string> lines = wrap(face, px, s, maxw, max_lines);
        for (std::size_t i = 0; i < lines.size(); ++i) draw(c, face, px, x, y + float(i) * lh, lines[i], color, opacity);
        return float(lines.size()) * lh;
    }

private:
    struct FaceData {
        const unsigned char* data = nullptr;
        stbtt_fontinfo info{};
        bool ok = false;
    };
    struct Glyph {
        int w = 0, h = 0, xoff = 0, yoff = 0;
        float adv = 0;
        std::vector<uint8_t> bmp;
    };

    static float scale_of(const FaceData& f, float px) {
        return stbtt_ScaleForMappingEmToPixels(&f.info, px > 0 ? px : 1);
    }
    static int glyph_index(const FaceData& f, uint32_t cp) {
        int gi = stbtt_FindGlyphIndex(&f.info, int(cp));
        if (!gi && cp != '?') gi = stbtt_FindGlyphIndex(&f.info, '?');
        return gi;
    }
    static uint64_t key(Face face, float px, uint32_t cp) {
        int pxq = int(px * 4.0f + 0.5f);
        return (uint64_t(pxq & 0xFFFFFF) << 32) | (uint64_t(static_cast<int>(face)) << 40) | uint64_t(cp & 0x1FFFFF);
    }

    const Glyph& glyph(Face face, float px, uint32_t cp) const {
        auto it = glyphs_.find(key(face, px, cp));
        if (it != glyphs_.end()) return it->second;
        const FaceData& f = faces_[static_cast<int>(face)];
        float scale = scale_of(f, px);
        int gi = glyph_index(f, cp);
        Glyph g;
        int adv, lsb;
        stbtt_GetGlyphHMetrics(&f.info, gi, &adv, &lsb);
        (void)lsb;
        g.adv = float(adv) * scale;
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(&f.info, gi, scale, scale, &x0, &y0, &x1, &y1);
        g.w = x1 - x0; g.h = y1 - y0; g.xoff = x0; g.yoff = y0;
        if (g.w > 0 && g.h > 0) {
            g.bmp.resize(std::size_t(g.w) * std::size_t(g.h));
            stbtt_MakeGlyphBitmap(&f.info, g.bmp.data(), g.w, g.h, g.w, scale, scale, gi);
        }
        return glyphs_.emplace(key(face, px, cp), std::move(g)).first->second;
    }

    FaceData faces_[3];
    mutable std::unordered_map<uint64_t, Glyph> glyphs_;
};

}  // namespace ui
