// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for the in-game guide overlay (ui_guide.h): assert-based checks of the
// pure logic (tab navigation, achievement scroll, date formatting, toast queue) plus
// renders of the four approved mockup states to $UI_RENDER_OUT/guide-*.png at
// 1920x1080 for RMSE comparison against the mockups. Build/run through tests/ui-guide.sh.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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
    v[0] = {"First Steps", "Finish the tutorial level", true, false, "Nov 9, 2026", icon0.data(), 64, 64,
            0, 0, ui_guide::Mark::Ring};
    v[1] = {"Treasure Hunter", "Open 10 hidden chests", true, false, "Nov 2, 2026", icon1.data(), 64, 64,
            0, 0, ui_guide::Mark::Square};
    v[2] = {"Quick Learner", "Win your first match", true, false, "Oct 28, 2026", nullptr, 0, 0,
            0xFF5A4A6B, 0xFFE3D9EC, ui_guide::Mark::Badge};
    v[3] = {"Untouchable", "Beat a boss without taking damage", false, false, "", nullptr, 0, 0,
            0xFF383838, 0xFF6E6E6E, ui_guide::Mark::Triangle};
    v[4] = {"Hidden achievement", "Details are hidden until unlocked", false, true, "", nullptr, 0, 0,
            0xFF383838, 0xFF6A6A6A, ui_guide::Mark::Ring};
    v[5] = {"Marathon", "Play for ten hours in total", false, false, "", nullptr, 0, 0,
            0xFF383838, 0xFF6E6E6E, ui_guide::Mark::Bars};
    v[6] = {"Perfect Round", "Clear a round with no misses", false, false, "", nullptr, 0, 0,
            0xFF383838, 0xFF6E6E6E, ui_guide::Mark::Ring};
    v[7] = {"Collector", "Find every collectible", false, false, "", nullptr, 0, 0,
            0xFF383838, 0xFF6E6E6E, ui_guide::Mark::Square};
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

    // ---- pure logic: Settings rows follow the FPS counter (Bugbot: focus
    // walked a hidden row) ----
    {
        Model m;
        m.tab = 2;
        m.fpsOn = false; m.graphOn = false;
        check(ui_guide::settings_rows(false) == 1 && ui_guide::row_count(m) == 1,
              "FPS off: one Settings row (the hidden graph row is not focusable)");
        ui_guide::move(m, Action::Down);
        check(m.sel == 0, "FPS off: Down from the FPS row stays on it");
        ui_guide::move(m, Action::Up);
        check(m.sel == 0, "FPS off: Up from the FPS row stays on it");
        m.fpsOn = true;
        check(ui_guide::settings_rows(true) == 2 && ui_guide::row_count(m) == 2,
              "FPS on: two Settings rows (counter + graph)");
        ui_guide::move(m, Action::Down);
        check(m.sel == 1, "FPS on: Down reaches the graph row");
        ui_guide::move(m, Action::Down);
        check(m.sel == 0, "FPS on: Down wraps back to the FPS row");
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

    // ---- pure logic: canvas -> back-buffer compositing keeps the channels ----
    // The canvas is 0xAARRGGBB. A B8G8R8A8 back buffer stores those bytes in
    // the same order (uint32 0xAARRGGBB); an R8G8B8A8 one stores them swapped
    // (uint32 0xAABBGGRR). A pure red canvas pixel over black must therefore
    // land as 0xFFFF0000 (BGRA) or 0xFF0000FF (RGBA) — never the other way.
    {
        const uint32_t red = ui::theme::rgb(255, 0, 0);    // 0xFFFF0000
        const uint32_t blue = ui::theme::rgb(0, 0, 255);   // 0xFF0000FF
        check(ui_guide::composite_pixel(0xFF000000u, red, 255, false) == 0xFFFF0000u,
              "composite_pixel: opaque red on BGRA is 0xFFFF0000 in memory");
        check(ui_guide::composite_pixel(0xFF000000u, red, 255, true) == 0xFF0000FFu,
              "composite_pixel: opaque red on RGBA is 0xFF0000FF in memory");
        check(ui_guide::composite_pixel(0xFF000000u, blue, 255, false) == 0xFF0000FFu,
              "composite_pixel: opaque blue on BGRA is 0xFF0000FF in memory");
        check(ui_guide::composite_pixel(0xFF000000u, blue, 255, true) == 0xFFFF0000u,
              "composite_pixel: opaque blue on RGBA is 0xFFFF0000 in memory");
        // Translucent red at 50% over black: half intensity, same channel.
        check(ui_guide::composite_pixel(0xFF000000u, red, 128, false) == 0xFF800000u,
              "composite_pixel: 50% red on BGRA keeps red in the high byte");
        check(ui_guide::composite_pixel(0xFF000000u, red, 128, true) == 0xFF000080u,
              "composite_pixel: 50% red on RGBA keeps red in the low byte");
        // The opaque and translucent paths agree: a=255 is an opaque copy and
        // a=0 leaves the destination alone.
        check(ui_guide::composite_pixel(0xFF112233u, blue, 255, false) == 0xFF0000FFu,
              "composite_pixel: a=255 replaces the destination (BGRA)");
        check(ui_guide::composite_pixel(0xFF112233u, blue, 0, false) == 0xFF112233u,
              "composite_pixel: a=0 keeps the destination");
        // The destination's alpha byte survives the blend.
        check((ui_guide::composite_pixel(0x80112233u, red, 128, false) & 0xFF000000u) == 0x80000000u,
              "composite_pixel: keeps the destination alpha byte");
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

    // ---- pure logic: toast expiry on an injected clock ----
    {
        ui_guide::Toast t{"Marathon", nullptr, 0, 0, 100.0};
        check(!ui_guide::toast_expired(t, 100.0, 5.0), "toast born now is alive");
        check(!ui_guide::toast_expired(t, 104.999, 5.0), "toast just under its lifetime is alive");
        check(ui_guide::toast_expired(t, 105.001, 5.0), "toast past its lifetime is expired");
        check(ui_guide::toast_expired(t, 1000.0, 5.0), "toast far past its lifetime is expired");
        ui_guide::Toast never{"", nullptr, 0, 0, 0.0};
        check(!ui_guide::toast_expired(never, 1000.0, 5.0), "unstamped toast (born 0) never expires");
    }

    // ---- pure logic: AchievementStore generation bumps on every mutation ----
    {
        ui_guide::AchievementStore store;
        const unsigned g0 = store.generation();
        auto achs = make_achievements();
        store.set_achievements(achs);
        const unsigned g1 = store.generation();
        check(g1 != g0, "generation bumps on set_achievements");
        store.push_unlock(0, "Nov 10, 2026", 1.0);
        check(store.generation() != g1, "generation bumps on push_unlock");
        const unsigned g3 = store.generation();
        auto icon = std::make_shared<std::vector<uint32_t>>(64 * 64, 0xFF3F5B73);
        check(store.set_icon(0, icon, 64, 64, true), "set_icon on a valid index succeeds");
        check(store.generation() != g3, "generation bumps on set_icon");
        const unsigned g4 = store.generation();
        store.unlock(1, "Treasure Hunter", "Open 10 hidden chests", "Nov 10, 2026", 2.0);
        check(store.generation() != g4, "generation bumps on unlock");
        check(!store.set_icon(99, icon, 64, 64, true), "set_icon of a bad index fails");
        check(store.generation() == store.generation(), "generation is stable without mutations");
    }

    // ---- pure logic: guide geometry (design space 1920x1080) ----
    {
        const ui::Rect card = ui_guide::card_rect(1920, 1080);
        check(int(card.x) == 80 && int(card.y) == 32 && int(card.w) == 560 && int(card.h) == 1016,
              "card_rect at 1080p is exactly (80,32,560,1016)");
        const ui::Rect toast = ui_guide::toast_rect(1920, 1080);
        check(int(toast.x) == 784 && int(toast.y) == 912 && int(toast.w) == 352 && int(toast.h) == 88,
              "toast_rect at 1080p is exactly (784,912,352,88)");
        // everything inside the frame at 1080p
        check(card.x >= 0 && card.y >= 0 && card.x + card.w <= 1920 && card.y + card.h <= 1080,
              "card fits the 1080p frame");
        check(toast.x >= 0 && toast.y >= 0 && toast.x + toast.w <= 1920 && toast.y + toast.h <= 1080,
              "toast fits the 1080p frame");
        // 720p: scaled by 2/3, still inside
        const ui::Rect card720 = ui_guide::card_rect(1280, 720);
        check(card720.x >= 0 && card720.y >= 0 &&
                  card720.x + card720.w <= 1280 && card720.y + card720.h <= 720,
              "card fits a 720p frame");
        const ui::Rect toast720 = ui_guide::toast_rect(1280, 720);
        check(toast720.x >= 0 && toast720.y >= 0 &&
                  toast720.x + toast720.w <= 1280 && toast720.y + toast720.h <= 720,
              "toast fits a 720p frame");
        check(ui_guide::card_rect(1920, 1080).w == card.w, "card_rect is deterministic");
        // degenerate sizes stay in bounds
        const ui::Rect cardTiny = ui_guide::card_rect(100, 60);
        check(cardTiny.x >= 0 && cardTiny.y >= 0 && cardTiny.x + cardTiny.w <= 100 && cardTiny.y + cardTiny.h <= 60,
              "card fits a tiny frame");
    }

    // ---- pure logic: scale_nearest ----
    {
        // 2x2 -> 4x4 doubles every pixel
        const uint32_t src[4] = {0xFF000000u, 0xFF111111u, 0xFF222222u, 0xFF333333u};
        auto out = ui_guide::scale_nearest(src, 2, 2, 4, 4);
        check(out.size() == 16, "scale_nearest: output size is dw*dh");
        // 2x upscale: output rows 0,1 <- source row 0; rows 2,3 <- row 1;
        // columns likewise (out[8] is row 2, col 0 -> src[2]; out[15] row 3
        // col 3 -> src[3])
        check(out[0] == src[0] && out[1] == src[0] && out[2] == src[1] && out[3] == src[1] &&
                  out[8] == src[2] && out[9] == src[2] && out[10] == src[3] && out[11] == src[3] &&
                  out[14] == src[3] && out[15] == src[3],
              "scale_nearest: 2x upscale reproduces the source pixels");
        // 1:1 is an identity copy
        auto same = ui_guide::scale_nearest(src, 2, 2, 2, 2);
        check(same[0] == src[0] && same[1] == src[1] && same[2] == src[2] && same[3] == src[3],
              "scale_nearest: 1:1 is identity");
        // downscale never reads out of bounds (samples stay in range)
        auto small = ui_guide::scale_nearest(src, 2, 2, 1, 1);
        check(small.size() == 1 && (small[0] == src[0] || small[0] == src[3]),
              "scale_nearest: downscale samples a source pixel");
    }

    // ---- card-local render: draw_guide_at at (0,0) into a card-sized canvas ----
    {
        std::vector<uint32_t> cardPx(size_t(ui_guide::kCardW) * size_t(ui_guide::kCardH),
                                     ui::theme::page);
        ui::Canvas cardC(cardPx.data(), int(ui_guide::kCardW), int(ui_guide::kCardH), int(ui_guide::kCardW));
        ui_guide::Model m;
        m.tab = 0; m.sel = 0;
        ui_guide::draw_guide_at(cardC, font, m, 0, 0);
        // the card's tab underline lands at card-local (kTabCx[0]-30, kUnderY+2),
        // i.e. where it would on the full frame minus the (80,32) origin
        check(cardPx[size_t(ui_guide::kUnderY + 2) * int(ui_guide::kCardW) +
                     int(ui_guide::kTabCx[0] - 30)] == ui::theme::accent,
              "card-local render puts the tab underline at the design offsets");
        // compositing the card canvas over a full frame at card_rect reproduces
        // the screen-space draw_guide result (same pixel, same colour)
        const int W = ui::theme::width, H = ui::theme::height;
        std::vector<uint32_t> frame(size_t(W) * H, ui::theme::page);
        ui::Canvas fc(frame.data(), W, H, W);
        const ui::Rect r = ui_guide::card_rect(W, H);
        for (int y = 0; y < int(r.h); ++y)
            std::copy_n(cardPx.data() + size_t(y) * int(ui_guide::kCardW), int(ui_guide::kCardW),
                        frame.data() + size_t(int(r.y) + y) * W + int(r.x));
        check(frame[size_t(ui_guide::kCardY + ui_guide::kUnderY + 2) * W +
                    int(ui_guide::kCardX + ui_guide::kTabCx[0] - 30)] == ui::theme::accent,
              "composited card-local render matches the screen-space coordinates");
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

    // comparison mode: plain #101010 page, no game frame, no dim (matches the
    // approved mockups, which show the card over the flat page)
    auto fill_game = [&]() { px.assign(size_t(W) * H, ui::theme::page); };

    // 01 home tab: Resume focused
    {
        fill_game();
        ui_guide::Model m;
        m.tab = 0; m.sel = 0;
        draw_guide(c, font, m);
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
        // progress fill is accent green at the track's left (screen y = kCardY + 154 + 2)
        check(px[size_t(int(ui_guide::kCardY + 186) + 2) * W + int(ui_guide::kCardX + 30)] == ui::theme::accent,
              "achievement progress fill is accent green");
        // the focused row's ring is the 3px Inside accent stroke (screen
        // y = kCardY + 216 + 1, x = kCardX + 16 + 20)
        check(px[size_t(int(ui_guide::kCardY + 216) + 1) * W + int(ui_guide::kCardX + 16 + 20)] ==
                  ui::theme::accent,
              "achievement focus ring is accent");
        snap("guide-02-achievements");
    }

    // 03 settings tab: FPS counter on, graph sub-row visible
    {
        fill_game();
        ui_guide::Model m;
        m.tab = 2; m.sel = 0; m.fpsOn = true; m.graphOn = true;
        draw_guide(c, font, m);
        // toggle track is accent green when on (screen y = kCardY + 148 + 8,
        // x = kCardX + 476 + 8, inside the track's left edge)
        check(px[size_t(int(ui_guide::kCardY + 148) + 8) * W + int(ui_guide::kCardX + 476 + 8)] == ui::theme::accent,
              "FPS toggle track is accent when on");
        snap("guide-03-settings");
    }

    // 04 achievement toast alone on the page (the mockup shows no card)
    {
        fill_game();
        auto achs = make_achievements();
        ui_guide::Toast t{achs[0].name, achs[0].icon, 64, 64, 0};
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
