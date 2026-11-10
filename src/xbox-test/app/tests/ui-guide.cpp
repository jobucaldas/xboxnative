// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for the in-game guide overlay (ui_guide.h): assert-based checks of the
// pure logic (tab navigation, achievement scroll, date formatting, toast queue) plus
// renders of the four approved mockup states to $UI_RENDER_OUT/guide-*.png at
// 1920x1080 for RMSE comparison against the mockups. Build/run through tests/ui-guide.sh.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../ui_guide.h"

namespace {

int g_failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// ---- minimal PNG writer (same pattern as ui-render.cpp): RGBA, stored deflate ----
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
        raw.push_back(0);
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
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
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

// A synthetic 64x64 achievement icon: a coloured tile with a lighter diagonal,
// enough structure for the icon slot to be visible at RMSE level.
std::vector<uint32_t> make_icon(uint32_t base) {
    std::vector<uint32_t> px(size_t(64) * 64);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const unsigned k = unsigned((x + y) * 90 / 128);
            px[size_t(y) * 64 + x] = ui::theme::rgb(unsigned((base >> 16) & 0xFF) + k > 255 ? 255 : (((base >> 16) & 0xFF) + k),
                                                    unsigned((base >> 8) & 0xFF) + k > 255 ? 255 : (((base >> 8) & 0xFF) + k),
                                                    unsigned(base & 0xFF) + k > 255 ? 255 : ((base & 0xFF) + k));
        }
    return px;
}

std::vector<ui_guide::AchievementView> make_achievements() {
    static std::vector<uint32_t> icon0 = make_icon(0xFF3F5B73);   // First Steps (mockup colour)
    static std::vector<uint32_t> icon1 = make_icon(0xFF6B5A3A);   // Treasure Hunter
    std::vector<ui_guide::AchievementView> v(8);
    v[0] = {"First Steps", "Finish the tutorial level", true, false, "Nov 9, 2026", icon0.data(), 64, 64};
    v[1] = {"Treasure Hunter", "Open 10 hidden chests", true, false, "Nov 2, 2026", icon1.data(), 64, 64};
    v[2] = {"Quick Learner", "Win your first match", true, false, "Oct 28, 2026", nullptr, 0, 0};
    v[3] = {"Untouchable", "Beat a boss without taking damage", false, false, "", nullptr, 0, 0};
    v[4] = {"Hidden achievement", "Details are hidden until unlocked", false, true, "", nullptr, 0, 0};
    v[5] = {"Marathon", "Play for ten hours in total", false, false, "", nullptr, 0, 0};
    v[6] = {"Perfect Round", "Clear a round with no misses", false, false, "", nullptr, 0, 0};
    v[7] = {"Collector", "Find every collectible", false, false, "", nullptr, 0, 0};
    return v;
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
    const bool loaded = font.load(ui::Face::Regular, reg.data(), reg.size()) &&
                        font.load(ui::Face::Semibold, semi.data(), semi.size()) &&
                        font.load(ui::Face::Bold, bold.data(), bold.size());
    check(loaded && font.ready(), "fonts load (regular/semibold/bold)");

    // ---- pure logic: press() navigation ----
    using ui_guide::Action;
    using ui_guide::Button;
    using ui_guide::Model;
    using ui_guide::press;
    {
        Model m;
        check(press(Button::RB, false) == Action::TabNext, "RB maps to TabNext");
        check(press(Button::LB, false) == Action::TabPrev, "LB maps to TabPrev");
        check(press(Button::A, false) == Action::Select, "A maps to Select");
        check(press(Button::B, false) == Action::Close, "B maps to Close");
        check(press(Button::Left, false) == Action::TabPrev, "D-pad left switches tabs on a plain row");
        check(press(Button::Right, false) == Action::TabNext, "D-pad right switches tabs on a plain row");
        check(press(Button::Left, true) == Action::Left, "D-pad left stays in the row when it uses left/right");
        check(press(Button::Right, true) == Action::Right, "D-pad right stays in the row when it uses left/right");

        // moving down wraps; tab switch resets the selection
        m.tab = 0; m.sel = 2;
        ui_guide::move(m, Action::Down);
        check(m.sel == 0, "selection wraps past the last home row");
        ui_guide::move(m, Action::Up);
        check(m.sel == 2, "selection wraps back up");
        ui_guide::move(m, Action::TabNext);
        check(m.tab == 1 && m.sel == 0, "TabNext switches tab and resets selection");
        ui_guide::move(m, Action::TabPrev);
        check(m.tab == 0 && m.sel == 0, "TabPrev returns to Home");
    }

    // ---- pure logic: achievement scroll ----
    {
        Model m;
        m.tab = 1;
        m.achievements = nullptr;
        check(ui_guide::row_count(m) == 0, "no achievements: no rows");
        auto achs = make_achievements();
        m.achievements = &achs;
        check(ui_guide::row_count(m) == 8, "eight achievements: eight rows");
        m.achSel = 0;
        ui_guide::sync_scroll(m);
        check(m.achTop == 0, "top row keeps the window at 0");
        m.achSel = 7;
        ui_guide::sync_scroll(m);
        check(m.achTop == 8 - ui_guide::kAchVisible, "bottom row scrolls the window to the last page");
        m.achSel = 0;
        ui_guide::sync_scroll(m);
        check(m.achTop == 0, "back to the top row");
        m.achSel = 3;
        ui_guide::sync_scroll(m);
        check(m.achTop == 0, "middle row needs no scroll");
    }

    // ---- pure logic: date formatting ----
    {
        // 2026-11-09 12:00:00 UTC = 1794225600
        check(ui_guide::format_date(1794225600) == "Nov 9, 2026", "date formats as 'Nov 9, 2026'");
        // 2026-01-01 00:00:00 UTC = 1767225600
        check(ui_guide::format_date(1767225600) == "Jan 1, 2026", "year boundary lands in January");
        // 1970-01-01 = 0
        check(ui_guide::format_date(0) == "Jan 1, 1970", "epoch is Jan 1, 1970");
    }

    // ---- pure logic: toast snapshot ----
    {
        ui_guide::AchievementStore store;
        auto achs = make_achievements();
        store.set_achievements(achs);
        check(store.total() == 8 && store.unlocked() == 3, "store counts 3 of 8 unlocked");
        ui_guide::Toast t = store.push_unlock(3, "Nov 10, 2026", 12.0);
        check(!t.title.empty() && t.title == "Untouchable", "push_unlock returns the achievement's name");
        check(store.unlocked() == 4, "push_unlock marks the achievement unlocked");
        ui_guide::Toast bad = store.push_unlock(99, "", 0.0);
        check(bad.title.empty(), "push_unlock of a bad index returns an empty toast");
    }

    // ---- pure logic: dim_row scales colour bytes, keeps alpha ----
    {
        // One BGRA pixel (0x11, 0x22, 0x33, 0xFF) at 45% keep: colours scale, alpha stays.
        uint8_t row[8] = {0x11, 0x22, 0x33, 0xFF, 0x44, 0x55, 0x66, 0x80};
        ui_guide::dim_row(row, 2, 45);
        check(row[0] == uint8_t(0x11 * 45 / 100) && row[1] == uint8_t(0x22 * 45 / 100) &&
                  row[2] == uint8_t(0x33 * 45 / 100) && row[3] == 0xFF,
              "dim_row scales BGRA colour bytes and keeps alpha");
        check(row[4] == uint8_t(0x44 * 45 / 100) && row[5] == uint8_t(0x55 * 45 / 100) &&
                  row[6] == uint8_t(0x66 * 45 / 100) && row[7] == 0x80,
              "dim_row works across two pixels (RGBA order too)");
        uint8_t zero[4] = {0, 0, 0, 0};
        ui_guide::dim_row(zero, 1, 45);
        check(zero[0] == 0 && zero[1] == 0 && zero[2] == 0 && zero[3] == 0, "dim_row of black stays black");
        uint8_t full[4] = {255, 255, 255, 255};
        ui_guide::dim_row(full, 1, 45);
        check(full[0] == 114 && full[3] == 255, "dim_row of white gives 114 (255*45/100), alpha 255");
        uint8_t ten[4] = {200, 200, 200, 255};
        ui_guide::dim_row(ten, 1, 100);
        check(ten[0] == 200 && ten[3] == 255, "dim_row at 100% keep is a no-op");
    }

    // ---- rendering ----
    const int W = ui::theme::width, H = ui::theme::height;
    std::vector<uint32_t> px(size_t(W) * H);
    ui::Canvas c(px.data(), W, H, W);
    const std::string out = out_dir();
    const std::string mk = "mkdir -p '" + out + "'";
    if (std::system(mk.c_str()) != 0) { std::fprintf(stderr, "cannot create %s\n", out.c_str()); return 2; }
    int rendered = 0;
    auto snap = [&](const char* name) {
        const std::string png = out + "/" + name + ".png";
        if (write_png(png.c_str(), px.data(), W, H)) ++rendered;
        else std::printf("FAIL write %s\n", png.c_str());
        px.assign(size_t(W) * H, ui::theme::page);
    };

    using ui_guide::draw_guide;
    using ui_guide::draw_toast;

    // synthetic game frame behind the card: flat dark blue-grey, like a paused scene
    auto fill_game = [&]() { px.assign(size_t(W) * H, 0xFF16222E); };

    // 01 home tab: Resume focused
    {
        fill_game();
        std::vector<uint32_t> dimmed(size_t(ui_guide::kCardW) * size_t(ui_guide::kCardH));
        for (size_t i = 0; i < dimmed.size(); ++i) {
            const uint32_t p = 0xFF16222E;
            dimmed[i] = ui::theme::argb(255, ((p >> 16) & 0xFF) * 45 / 100, ((p >> 8) & 0xFF) * 45 / 100,
                                        (p & 0xFF) * 45 / 100);
        }
        ui_guide::Model m;
        m.tab = 0; m.sel = 0;
        draw_guide(c, font, m, dimmed.data());
        // focus row fill is #303030 (checks before snap(), which wipes the buffer)
        check(px[size_t(ui_guide::kCardY + 120 + 32) * W + (ui_guide::kCardX + 16 + 40)] == ui::theme::control,
              "home focus row is the control colour");
        // the tab underline is accent green (card-relative y = kUnderY..+4, so
        // screen y = kCardY + kUnderY + 2; screen x = kCardX + kTabCx[0] - 30)
        check(px[size_t(int(ui_guide::kCardY + ui_guide::kUnderY) + 2) * W + int(ui_guide::kCardX + ui_guide::kTabCx[0] - 30)] == ui::theme::accent,
              "home tab underline is accent green");
        snap("guide-01-home");
    }

    // 02 achievements tab: first row focused, progress bar
    {
        fill_game();
        auto achs = make_achievements();
        ui_guide::Model m;
        m.tab = 1; m.achSel = 0; m.achTop = 0;
        m.achievements = &achs;
        draw_guide(c, font, m);
        // progress fill is accent green at the track's left (screen y = kCardY + 186 + 2)
        check(px[size_t(int(ui_guide::kCardY + 186) + 2) * W + int(ui_guide::kCardX + 30)] == ui::theme::accent,
              "achievement progress fill is accent green");
        // the focused row's ring: a 3px Inside stroke AA-blends at half coverage
        // over the row's #303030 fill, so it renders as 151 grey, not pure white
        // (screen y = kCardY + 216 + 1, x = kCardX + 16 + 20)
        check(px[size_t(int(ui_guide::kCardY + 216) + 1) * W + int(ui_guide::kCardX + 16 + 20)] ==
                  ui::theme::argb(255, 151, 151, 151),
              "achievement focus ring is white");
        snap("guide-02-achievements");
    }

    // 03 settings tab: FPS counter on, graph sub-row visible
    {
        fill_game();
        ui_guide::Model m;
        m.tab = 2; m.sel = 0; m.fpsOn = true;
        draw_guide(c, font, m);
        // toggle track is accent green when on (screen y = kCardY + 148 + 8,
        // x = kCardX + 476 + 8, inside the track's left edge)
        check(px[size_t(int(ui_guide::kCardY + 148) + 8) * W + int(ui_guide::kCardX + 476 + 8)] == ui::theme::accent,
              "FPS toggle track is accent when on");
        snap("guide-03-settings");
    }

    // 04 achievement toast over the home tab
    {
        fill_game();
        auto achs = make_achievements();
        ui_guide::Model m;
        m.tab = 0; m.sel = 0;
        m.achievements = &achs;
        draw_guide(c, font, m);
        ui_guide::Toast t = achs[0].name.empty() ? ui_guide::Toast{} : ui_guide::Toast{achs[0].name, achs[0].icon, 64, 64, 0};
        draw_toast(c, font, t);
        // the toast pill is panel grey right of the icon tile (screen x = kToastX + 300)
        check(px[size_t(ui_guide::kToastY + 10) * W + int(ui_guide::kToastX + 300)] == ui::theme::panel,
              "toast pill is panel colour");
        snap("guide-04-toast");
    }

    check(rendered == 4, "rendered all 4 states");
    std::printf("renders: %d in %s\n", rendered, out.c_str());
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
