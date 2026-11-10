// SPDX-License-Identifier: GPL-3.0-or-later
// Native smoke test for the UI toolkit (ui_theme.h, ui_canvas.h, ui_font.h, ui_glyphs.h):
// renders a 1920x1080 specimen page to a PNG and asserts the basics the later screens rely on.
// Build/run through tests/ui-render.sh; assets come from out/ui-assets (fetch-ui-assets.sh).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ui_canvas.h"
#include "ui_font.h"
#include "ui_glyphs.h"
#include "ui_theme.h"

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// ---- minimal PNG writer: 8-bit RGBA, stored (uncompressed) deflate blocks. No dependencies.
uint32_t crc_table[256];
void crc_init() {
    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x));
}
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, uint32_t(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}
bool write_png(const char* path, const uint32_t* px, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(w) * h * 4 + h);
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);  // filter: none
        for (int x = 0; x < w; ++x) {
            uint32_t p = px[size_t(y) * w + x];
            raw.push_back(uint8_t(p >> 16)); raw.push_back(uint8_t(p >> 8)); raw.push_back(uint8_t(p));
            raw.push_back(uint8_t(p >> 24));
        }
    }
    std::vector<uint8_t> z{0x78, 0x01};
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t n = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
        uint16_t inv = uint16_t(~n);
        z.push_back(uint8_t(inv)); z.push_back(uint8_t(inv >> 8));
        z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
        pos += n;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    put32(z, (b << 16) | a);
    std::vector<uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(w)); put32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    return std::fclose(f) == 0 && ok;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size_t(n > 0 ? n : 0));
    bool ok = n > 0 && std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

std::string out_dir() {
    const char* d = std::getenv("UI_RENDER_OUT");
    return d && *d ? d : "/tmp/xb51-renders";
}

}  // namespace

int main(int argc, char** argv) {
    std::string assets = argc > 1 ? argv[1] : "out/ui-assets";
    crc_init();

    std::vector<uint8_t> reg, semi, bold;
    if (!read_file(assets + "/selawk.ttf", reg) || !read_file(assets + "/selawksb.ttf", semi) ||
        !read_file(assets + "/selawkb.ttf", bold)) {
        std::fprintf(stderr, "missing fonts under %s (run src/tools/fetch-ui-assets.sh)\n", assets.c_str());
        return 2;
    }
    ui::Font font;
    bool loaded = font.load(ui::Face::Regular, reg.data(), reg.size()) &&
                  font.load(ui::Face::Semibold, semi.data(), semi.size()) &&
                  font.load(ui::Face::Bold, bold.data(), bold.size());
    check(loaded && font.ready(), "fonts load (regular/semibold/bold)");

    const int W = ui::theme::width, H = ui::theme::height;
    std::vector<uint32_t> px(size_t(W) * H, ui::theme::page);
    ui::Canvas c(px.data(), W, H, W);
    const std::string out = out_dir();
    std::string mk = "mkdir -p '" + out + "'";
    if (std::system(mk.c_str()) != 0) { std::fprintf(stderr, "cannot create %s\n", out.c_str()); return 2; }
    const std::string png = out + "/ui-smoke.png";

    // ---- assertions that need no drawing
    float w1 = font.measure(ui::Face::Regular, 24, "Megabonk");
    float w2 = font.measure(ui::Face::Regular, 24, "Megabonk Deluxe");
    check(w2 > w1 && w1 > 0, "measure is monotonic in length");
    check(font.measure(ui::Face::Bold, 24, "Megabonk") > font.measure(ui::Face::Regular, 24, "Megabonk"),
          "bold is wider than regular");
    check(font.measure(ui::Face::Regular, 48, "Megabonk") > w1 * 1.8f, "measure scales with pixel size");
    check(font.measure(ui::Face::Regular, 24, "Caf\xC3\xA9 \xE2\x9C\x93") > 0, "UTF-8 decodes");
    std::string long_name = "The Legend of a Very Long Steam Game Name: Definitive Edition";
    std::string ell = font.ellipsize(ui::Face::Regular, 22, long_name, 168);
    check(font.measure(ui::Face::Regular, 22, ell) <= 168 && ell != long_name, "ellipsize fits the width");
    check(font.ellipsize(ui::Face::Regular, 22, "Hades", 168) == "Hades", "ellipsize leaves short text alone");
    auto lines = font.wrap(ui::Face::Regular, 24, "A long description that has to wrap across several lines inside a card.",
                           300);
    check(lines.size() > 1, "wrap produces multiple lines");
    bool wrap_fits = true;
    for (const auto& l : lines) wrap_fits = wrap_fits && font.measure(ui::Face::Regular, 24, l) <= 300;
    check(wrap_fits, "every wrapped line fits the width");

    // ---- specimen page
    c.fill(ui::theme::page);
    c.fill_round_rect(ui::rect(32, 32, 411, 1016), ui::theme::radiusPanel, ui::theme::panel);

    // type scale, all three faces
    using namespace ui::theme;
    float y = 96;
    const ui::theme::Type scale[] = {title, screenTitle, section, tab, cardLabel, button, panelHeading, sidebar, body, tile,
                              toastTitle, hintText, smallText, caption, micro, badge};
    const char* names[] = {"title 44/600", "screenTitle 56/600", "section 32/600", "tab 32/400", "cardLabel 32/400",
                           "button 30/600", "panelHeading 26/600", "sidebar 24/400", "body 24/400", "tile 22/400",
                           "toastTitle 22/600", "hintText 20/400", "small 19/400", "caption 18/400", "micro 16/400",
                           "badge 15/700"};
    for (size_t i = 0; i < sizeof(scale) / sizeof(scale[0]); ++i) {
        ui::Face f = scale[i].weight >= 700 ? ui::Face::Bold : scale[i].weight >= 600 ? ui::Face::Semibold : ui::Face::Regular;
        float lh = font.line_height(f, float(scale[i].px));
        font.draw(c, ui::Face::Regular, 16, 524, y + lh * 0.5f, names[i], textMuted);
        font.draw(c, f, float(scale[i].px), 700, y, "Library \xE2\x80\x94 42", text);
        y += lh + 2;
    }

    // wrapped paragraph in a card
    const float card_y = y + 16;
    c.fill_round_rect(ui::rect(524, card_y, 560, 140), radiusCard, card);
    font.draw_wrapped(c, ui::Face::Regular, 24, 548, card_y + 40, 512,
                      "A deck-building roguelike where every run rewrites the rules. Wraps to the card width and "
                      "ellipsizes when it runs out of lines.",
                      text2, 3);

    // rounded rects, focus ring, donut
    float bx = 1120, by = 96;
    c.fill_round_rect(ui::rect(bx, by, 256, 80), radiusControl, accent);
    c.stroke_round_rect(ui::rect(bx, by, 256, 80), radiusControl, 1, accentEdge, ui::Align::Inside);
    font.draw(c, ui::Face::Semibold, 30, bx + 40, by + 52, "PLAY", onAccent);
    c.stroke_round_rect(ui::rect(bx - ringOffset, by - ringOffset, 256 + ringOffset * 2, 80 + ringOffset * 2),
                        radiusControl + ringRadiusDelta, ringWidth, focus);
    c.fill_round_rect(ui::rect(bx, by + 120, 80, 80), radiusControl, controlDark);
    ui::glyphs::trash(c, ui::rect(bx + 28, by + 148, 24, 24), icon, 2);
    c.fill_round_rect(ui::rect(bx + 96, by + 120, 80, 80), radiusControl, controlDark);
    ui::glyphs::cloud(c, ui::rect(bx + 120, by + 148, 28, 20), icon, 2);
    c.fill_round_rect(ui::rect(bx + 192, by + 120, 80, 80), radiusControl, controlDark);
    ui::glyphs::cog(c, ui::rect(bx + 215, by + 143, 34, 34), icon, 2);

    float dcx = bx + 56, dcy = by + 300;
    c.fill_donut(dcx, dcy, 28, 5, 0, ui::kTau, track);
    c.fill_donut(dcx, dcy, 28, 5, -ui::kPi * 0.5f, ui::kTau * 0.62f, accent);
    font.draw(c, ui::Face::Regular, 16, dcx - 16, dcy + 5, "62%", textDim);
    c.fill_round_rect(ui::rect(dcx + 64, dcy - 4, 456, 8), 4, track);
    c.fill_round_rect(ui::rect(dcx + 64, dcy - 4, 456 * 0.43f, 8), 4, accent);

    // controller glyphs
    float gx = 1120, gy = 560;
    const char letters[4] = {'A', 'B', 'X', 'Y'};
    for (int i = 0; i < 4; ++i) {
        ui::glyphs::face_button(c, gx + 20 + i * 56, gy, 40, letters[i], font, glyph, glyphLabel);
        ui::glyphs::face_button(c, gx + 20 + i * 56, gy + 64, 40, letters[i], font, buttonA, onAccent);
    }
    ui::glyphs::menu(c, gx + 244, gy, 26, glyph, 0xFFC8C8C8);
    ui::glyphs::view(c, gx + 300, gy, 26, glyph, 2);
    float hint_x = gx, hint_y = gy + 120;
    hint_x += ui::glyphs::prompt(c, hint_x, hint_y, font, 'A', "Select", 26, glyph, glyphLabel, hint, 20) + 34;
    hint_x += ui::glyphs::prompt(c, hint_x, hint_y, font, 'B', "Back", 26, glyph, glyphLabel, hint, 20) + 34;
    ui::glyphs::prompt(c, hint_x, hint_y, font, 'Y', "Sort", 26, glyph, glyphLabel, hint, 20);

    // scrim + sheet over a card
    c.fill_round_rect(ui::rect(1120, 760, 704, 240), radiusCard, card);
    font.draw(c, ui::Face::Semibold, 26, 1148, 806, "Delete this game?", text);
    ui::Canvas::Clip saved = c.clip();
    c.set_clip(ui::rect(1120, 780, 704, 240));
    c.fill_rect_alpha(1120, 780, 704, 240, scrim, scrimAlpha);
    c.set_clip(saved);
    c.fill_round_rect(ui::rect(1240, 840, 464, 120), radiusCard, panel);
    font.draw(c, ui::Face::Regular, 22, 1264, 884, "The dimmed card shows through the scrim.", textDim);
    c.fill_round_rect(ui::rect(1264, 904, 160, 40), radiusControl, controlDark);
    font.draw(c, ui::Face::Semibold, 20, 1290, 931, "Cancel", text2);

    // image blit with a rounded mask, from a synthetic gradient
    const int iw = 168, ih = 252;
    std::vector<uint32_t> art(size_t(iw) * ih);
    for (int yy = 0; yy < ih; ++yy)
        for (int xx = 0; xx < iw; ++xx) {
            unsigned r = unsigned(24 + xx * 200 / iw), g = unsigned(60 + yy * 150 / ih), b = unsigned(90 + (xx + yy) * 120 / (iw + ih));
            art[size_t(yy) * iw + xx] = ui::theme::argb(255, r, g, b);
        }
    ui::Image img{art.data(), iw, ih, iw};
    const float img_y = card_y + 164;
    c.draw_image(img, ui::rect(524, img_y, 112, 168), ui::Fit::Cover, radiusTile);
    c.draw_image(img, ui::rect(660, img_y, 112, 168), ui::Fit::Cover, 0);
    c.draw_image(img, ui::rect(796, img_y, 112, 168), ui::Fit::Contain, radiusTile);

    // sidebar sample: selected row, active bar, storage donut
    c.fill_round_rect(ui::rect(64, 264, 347, 64), radiusControl, control);
    c.fill_rect(64 + 347 + 18, 264, 6, 64, accent);
    font.draw(c, ui::Face::Regular, 24, 96, 306, "All games", text);
    font.draw(c, ui::Face::Regular, 24, 64, 1000, "Storage", text2);

    // ---- pixel assertions
    auto at = [&](int xx, int yy) { return px[size_t(yy) * W + xx]; };
    check(at(33, 33) == page, "rounded rect corner pixel is page colour");
    check(at(200, 500) == panel, "rounded rect centre pixel is panel colour");
    check(at(1120 + 128, 96 + 40) == accent, "primary button centre is the accent green");
    // focus ring: 3px band 6px outside the button, centred on the ring rect's edge
    check(at(int(1120 - ringOffset), 96 + 40) == focus, "focus ring pixel is white");
    check(at(int(1120 - ringOffset - ringWidth - 3), 96 + 40) == page, "outside the focus ring is page colour");
    // scrim: black at 140/255 over the card leaves ~45% of the card colour
    uint32_t dimmed = at(1160, 820);
    unsigned dim_r = (dimmed >> 16) & 0xFF;
    check(dim_r >= 12 && dim_r <= 18, "scrim dims the card by the expected alpha");
    check(at(524, int(img_y)) == page, "image blit honours the rounded mask");
    check(at(660 + 56, int(img_y) + 84) != page, "unmasked image blit covers its centre");

    // ---- text coverage: the title band must contain near-white pixels
    int white = 0;
    for (int yy = 60; yy < 130; ++yy)
        for (int xx = 524; xx < 900; ++xx)
            if (((at(xx, yy) >> 16) & 0xFF) > 200) ++white;
    check(white > 200, "text renders (title band has white coverage)");
    // anti-aliasing: the title band must also contain intermediate greys
    int grey = 0;
    for (int yy = 60; yy < 130; ++yy)
        for (int xx = 524; xx < 900; ++xx) {
            unsigned v = (at(xx, yy) >> 16) & 0xFF;
            if (v > 30 && v < 200) ++grey;
        }
    check(grey > 100, "text is anti-aliased (intermediate greys present)");

    bool wrote = write_png(png.c_str(), px.data(), W, H);
    check(wrote, "PNG written");
    if (wrote) std::printf("render: %s\n", png.c_str());

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
