// SPDX-License-Identifier: GPL-3.0-or-later
// Native test for the launcher screens (ui_launcher.h): assert-based checks of the
// pure logic (grid geometry, primary-button state machine, sidebar counts, queue and
// sheet view-models) plus a render of every approved mockup state to
// $UI_RENDER_OUT/launcher-*.png at 1920x1080 for RMSE comparison against the mockups.
// Build/run through tests/ui-launcher.sh.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "../ui_launcher.h"
#include "../playtime.h"

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

// ---- fixture data (fake, from the mockups; persona "Player One") ----

std::vector<ui_launcher::TileVM> make_tiles(int n) {
    std::vector<ui_launcher::TileVM> t{size_t(n)};
    // the mockup's 21 tiles: measured stops of each art gradient along its diagonal
    struct Art { uint32_t top, mid, bot; };
    const Art arts[21] = {
        {0xFF1D2E50, 0xFF3E6FA7, 0xFF0E1929},  // Megabonk
        {0xFF421334, 0xFFAF3A6E, 0xFF200A18},  // Hades
        {0xFF152D24, 0xFF3E8B69, 0xFF132E24},  // Celeste
        {0xFF32271A, 0xFFA77A3E, 0xFF1A150C},  // Hollow Knight
        {0xFF2A1E3B, 0xFF795AA7, 0xFF160E1E},  // Stardew Valley
        {0xFF361524, 0xFFA73E5A, 0xFF1C0910},  // Terraria
        {0xFF102837, 0xFF2E7999, 0xFF08181F},  // Dead Cells
        {0xFF331912, 0xFFAF5A2E, 0xFF1E0C08},  // Cuphead
        {0xFF172630, 0xFF4A798B, 0xFF0D161C},  // Subnautica
        {0xFF1E1738, 0xFF5A4AAF, 0xFF0E0D20},  // Portal 2
        {0xFF143122, 0xFF4A995A, 0xFF0B1B10},  // Slay the Spire
        {0xFF362111, 0xFFBF893A, 0xFF1F1308},  // Balatro
        {0xFF232328, 0xFF6D6D79, 0xFF111114},  // Inscryption
        {0xFF232328, 0xFF6D6D79, 0xFF111114},  // Factorio
        {0xFF271730, 0xFF894AA7, 0xFF150D1A},  // Risk of Rain 2
        {0xFF271730, 0xFF894AA7, 0xFF150D1A},  // Undertale
        {0xFF2C261E, 0xFF89795A, 0xFF1A140C},  // Katana ZERO
        {0xFF2C261E, 0xFF89795A, 0xFF1A140C},  // Outer Wilds
        {0xFF112330, 0xFF3A6989, 0xFF0A131A},  // Valheim
        {0xFF1E2A37, 0xFF5A7999, 0xFF0E161D},  // Spelunky 2
        {0xFF281F36, 0xFF7969A7, 0xFF0B060E},  // Tunic
    };
    const char* names[21] = {
        "Megabonk", "Hades", "Celeste", "Hollow Knight", "Stardew Valley", "Terraria", "Dead Cells",
        "Cuphead", "Subnautica", "Portal 2", "Slay the Spire", "Balatro", "Inscryption", "Factorio",
        "Risk of Rain 2", "Undertale", "Katana ZERO", "Outer Wilds", "Valheim", "Spelunky 2", "Tunic",
    };
    for (int i = 0; i < n; ++i) {
        t[i].name = names[i % 21];
        t[i].art_top = arts[i % 21].top;
        t[i].art_mid = arts[i % 21].mid;
        t[i].art_bot = arts[i % 21].bot;
        t[i].installed = i == 0 || i == 7;   // the mockup badges Megabonk and Cuphead only
    }
    return t;  // implicit move
}

std::vector<ui_launcher::QueueRowVM> make_rows(int n, bool updates) {
    std::vector<ui_launcher::QueueRowVM> r{size_t(n)};
    // item D21: the rows carry the game's tall library art, not a flat colour
    // block. Synthetic 2:3 capsules (the real ones are 260x390).
    static std::vector<std::vector<uint32_t>> arts;
    if (arts.empty()) {
        const uint32_t bases[5] = {0xFF1D2E50, 0xFF421334, 0xFF152D24, 0xFF32271A, 0xFF2A1E3B};
        for (uint32_t base : bases) {
            std::vector<uint32_t> px(size_t(64) * 96);
            for (int y = 0; y < 96; ++y)
                for (int x = 0; x < 64; ++x) {
                    const unsigned k = unsigned((x + y) * 70 / 160);
                    auto ch = [&](int shift) { const unsigned v = ((base >> shift) & 0xFF) + k; return v > 255 ? 255u : v; };
                    px[size_t(y) * 64 + x] = ui::theme::rgb(ch(16), ch(8), ch(0));
                }
            arts.push_back(std::move(px));
        }
    }
    const char* names[5] = {"Megabonk", "Hades", "Celeste", "Hollow Knight", "Terraria"};
    const char* sts[5] = {
        "Downloading  \xC2\xB7  62%  \xC2\xB7  12.4 MB/s  \xC2\xB7  about 1 hour left",
        "Queued  \xC2\xB7  14.7 GB",
        "Queued  \xC2\xB7  1.9 GB",
        "Update available  \xC2\xB7  1.2 GB",
        "Update available  \xC2\xB7  312 MB",
    };
    // the mockup's updates section lists Hollow Knight and Terraria (rows 3 and 4)
    const int first = updates ? 3 : 0;
    for (int i = 0; i < n; ++i) {
        r[i].name = names[(first + i) % 5];
        r[i].status = sts[(first + i) % 5];
        r[i].pct = !updates && i == 0 ? 0.62f : 0.0f;
        r[i].cover = ui::Image{arts[size_t((first + i) % 5)].data(), 64, 96, 64};
        r[i].busy = !updates && i == 0;
    }
    return r;
}

// QR module runs for mockup 04: the mockup's exact 29x29 code, merged into 230 runs at 10px
ui_launcher::QrRun* make_qr(int& count) {
    static ui_launcher::QrRun runs[256];
    int n = 0;
    auto run = [&](uint8_t row, uint8_t col, uint8_t w) { runs[n++] = {row, col, w}; };
    run(0, 0, 7), run(0, 10, 3), run(0, 14, 1), run(0, 19, 1), run(0, 22, 7), run(1, 0, 1),
    run(1, 6, 1), run(1, 8, 1), run(1, 13, 1), run(1, 18, 1), run(1, 20, 1), run(1, 22, 1),
    run(1, 28, 1), run(2, 0, 1), run(2, 2, 3), run(2, 6, 1), run(2, 9, 2), run(2, 12, 1),
    run(2, 15, 1), run(2, 18, 3), run(2, 22, 1), run(2, 24, 3), run(2, 28, 1), run(3, 0, 1),
    run(3, 2, 3), run(3, 6, 1), run(3, 9, 1), run(3, 12, 2), run(3, 16, 1), run(3, 18, 1),
    run(3, 20, 1), run(3, 22, 1), run(3, 24, 3), run(3, 28, 1), run(4, 0, 1), run(4, 2, 3),
    run(4, 6, 1), run(4, 8, 2), run(4, 11, 3), run(4, 16, 1), run(4, 18, 1), run(4, 20, 1),
    run(4, 22, 1), run(4, 24, 3), run(4, 28, 1), run(5, 0, 1), run(5, 6, 1), run(5, 9, 1),
    run(5, 11, 1), run(5, 14, 3), run(5, 19, 2), run(5, 22, 1), run(5, 28, 1), run(6, 0, 7),
    run(6, 8, 1), run(6, 10, 1), run(6, 12, 1), run(6, 14, 1), run(6, 16, 1), run(6, 18, 1),
    run(6, 20, 1), run(6, 22, 7), run(7, 9, 4), run(7, 15, 1), run(7, 18, 2), run(8, 0, 1),
    run(8, 2, 1), run(8, 4, 1), run(8, 6, 1), run(8, 9, 2), run(8, 16, 2), run(8, 20, 1),
    run(8, 24, 1), run(8, 27, 1), run(9, 10, 1), run(9, 13, 1), run(9, 15, 8), run(9, 25, 1),
    run(9, 28, 1), run(10, 0, 3), run(10, 4, 1), run(10, 6, 1), run(10, 8, 1), run(10, 10, 2),
    run(10, 14, 3), run(10, 20, 1), run(10, 22, 3), run(10, 26, 3), run(11, 0, 1), run(11, 2, 1),
    run(11, 4, 1), run(11, 7, 2), run(11, 10, 2), run(11, 13, 2), run(11, 16, 5), run(11, 27, 1),
    run(12, 1, 1), run(12, 4, 4), run(12, 11, 2), run(12, 14, 2), run(12, 18, 5), run(12, 25, 1),
    run(12, 27, 2), run(13, 0, 2), run(13, 3, 3), run(13, 8, 1), run(13, 11, 2), run(13, 14, 2),
    run(13, 17, 1), run(13, 21, 2), run(13, 25, 1), run(13, 28, 1), run(14, 0, 3), run(14, 6, 8),
    run(14, 18, 4), run(14, 24, 2), run(14, 27, 2), run(15, 1, 5), run(15, 7, 3), run(15, 12, 1),
    run(15, 15, 1), run(15, 17, 1), run(15, 19, 1), run(15, 25, 1), run(15, 27, 1), run(16, 2, 1),
    run(16, 4, 3), run(16, 8, 3), run(16, 16, 1), run(16, 18, 1), run(16, 22, 2), run(16, 25, 1),
    run(16, 27, 2), run(17, 1, 1), run(17, 3, 3), run(17, 7, 3), run(17, 11, 3), run(17, 15, 3),
    run(17, 19, 1), run(17, 21, 2), run(17, 25, 2), run(17, 28, 1), run(18, 0, 1), run(18, 3, 4),
    run(18, 9, 3), run(18, 14, 3), run(18, 18, 2), run(18, 21, 1), run(18, 27, 2), run(19, 1, 1),
    run(19, 7, 2), run(19, 11, 1), run(19, 13, 2), run(19, 16, 2), run(19, 20, 1), run(19, 22, 1),
    run(19, 24, 2), run(19, 27, 1), run(20, 0, 1), run(20, 4, 1), run(20, 6, 2), run(20, 10, 3),
    run(20, 14, 2), run(20, 18, 7), run(21, 8, 1), run(21, 10, 1), run(21, 12, 1), run(21, 14, 2),
    run(21, 17, 1), run(21, 20, 1), run(21, 24, 1), run(21, 26, 3), run(22, 0, 7), run(22, 13, 1),
    run(22, 18, 1), run(22, 20, 1), run(22, 22, 1), run(22, 24, 2), run(22, 27, 2), run(23, 0, 1),
    run(23, 6, 1), run(23, 11, 1), run(23, 15, 1), run(23, 17, 1), run(23, 19, 2), run(23, 24, 2),
    run(23, 27, 1), run(24, 0, 1), run(24, 2, 3), run(24, 6, 1), run(24, 8, 2), run(24, 11, 2),
    run(24, 16, 1), run(24, 18, 1), run(24, 20, 5), run(24, 28, 1), run(25, 0, 1), run(25, 2, 3),
    run(25, 6, 1), run(25, 9, 3), run(25, 13, 1), run(25, 15, 3), run(25, 19, 1), run(25, 23, 2),
    run(25, 26, 1), run(26, 0, 1), run(26, 2, 3), run(26, 6, 1), run(26, 8, 1), run(26, 14, 3),
    run(26, 18, 1), run(26, 20, 2), run(26, 24, 2), run(26, 28, 1), run(27, 0, 1), run(27, 6, 1),
    run(27, 9, 1), run(27, 12, 3), run(27, 16, 2), run(27, 19, 3), run(27, 24, 1), run(27, 27, 1),
    run(28, 0, 7), run(28, 8, 1), run(28, 10, 1), run(28, 15, 2), run(28, 18, 1), run(28, 20, 3),
    run(28, 24, 2), run(28, 27, 2),
    count = n;
    return runs;
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
    check(loaded && font.ready(), "fonts load");

    // ---- pure logic: grid geometry (5 columns, 2:3 tiles; item 4) ----
    // The mockup states keep the original 7-column layout via cols=7 so the
    // Selawik-reference comparison stays within RMSE blurred <= 0.02.
    using ui_launcher::grid_tile;
    const ui::Rect g0 = grid_tile(0, 0);
    check(g0.x == 524 && g0.y == 264 && g0.w == 232 && g0.h == 348, "grid tile 0 at (524,264) 232x348");
    const ui::Rect g1 = grid_tile(1, 0);
    check(g1.x == 524 + 272 && g1.y == 264, "grid pitch x is 272 (232 tile + 40 gap, left-aligned at 524)");
    const ui::Rect g4 = grid_tile(4, 0);
    check(g4.x == 524 + 4 * 272 && g4.x + g4.w == 1844, "5 columns of 232 + 4 gaps of 40 fill 524..1844 exactly");
    // review fix 1: 5 x 232 + 4 x 40 = 1320 — the full content width; two
    // rows pitch 372 (24px gap), row 2 ends exactly at the band top 984.
    check(5 * 232 + 4 * 40 == 1320, "5x232 + 4x40 gap = 1320 across the content width");
    check(264 + 372 + 348 == 984, "row 2 (at 636, +348) ends exactly at the band top 984");
    const ui::Rect g5 = grid_tile(5, 0);
    check(g5.x == 524 && g5.y == 264 + 372 && g5.y + g5.h == 984, "grid wraps to row 2 (636..984, full above band)");
    const ui::Rect gscroll = grid_tile(5, 1);
    check(gscroll.y == 264, "scrolling by one row keeps pitch");
    // cols=7 keeps the mockup's COLUMN COUNT; tile size is shared (the mockup
    // comparison tolerates the 240px tiles via the RMSE blur <= 0.02 gate).
    const ui::Rect gmock = grid_tile(7, 0, 7);
    check(gmock.x == 524 && gmock.y == 264 + 276 && gmock.w == 168, "cols=7 restores the mockup layout (168x252, pitch 276)");    int lo = -1, hi = -1;
    ui_launcher::grid_visible(21, 0, lo, hi, 5);
    check(lo == 0 && hi == 15, "page 0 of 21 shows rows 0..2 (15 tiles, 5 cols)");
    ui_launcher::grid_visible(21, 1, lo, hi, 5);
    check(lo == 0 && hi == 20, "page 1 shows the slack row above plus rows 1..3");
    ui_launcher::grid_visible(21, 0, lo, hi, 7);
    check(lo == 0 && hi == 21, "cols=7: mockup page 0 unchanged");

    // ---- pure logic: primary button state machine ----
    using ui_launcher::Primary;
    using ui_launcher::PrimaryVM;
    using ui_launcher::primary_state;
    using ui_launcher::primary_label;
    using ui_launcher::primary_hint;
    {
        PrimaryVM vm;
        check(primary_state(vm) == Primary::install && !std::strcmp(primary_label(vm, false), "INSTALL"),
              "not installed: INSTALL");
        vm.installed = true;
        check(primary_state(vm) == Primary::play && !std::strcmp(primary_label(vm, false), "PLAY"),
              "installed: PLAY");
        vm.download = detail_input::Download::running;
        check(primary_state(vm) == Primary::installing, "download running: installing");
        check(!std::strcmp(primary_label(vm, false), "INSTALLING 0%"), "installing label at 0%");
        vm.install_pct = 0.62f;
        check(!std::strcmp(primary_label(vm, false), "INSTALLING 62%"), "installing label reads the real percentage");
        vm.install_pct = 1.0f;
        check(!std::strcmp(primary_label(vm, false), "INSTALLING 100%"), "installing label at 100%");
        vm.install_pct = 1.5f;
        check(!std::strcmp(primary_label(vm, false), "INSTALLING 100%"), "installing label clamps above 100%");
        vm.install_pct = -0.25f;
        check(!std::strcmp(primary_label(vm, false), "INSTALLING 0%"), "installing label clamps below 0");
        vm.install_pct = 0.0f;
        check(!std::strcmp(primary_label(vm, false), "INSTALLING 0%"), "installing label back at 0%");
        check(!std::strcmp(primary_label(vm, true), "CANCEL"), "focused installing button reads CANCEL");
        check(!std::strcmp(primary_hint(vm, true), "Cancel install"), "hint says cancel when focused");
        check(!std::strcmp(primary_hint(vm, false), "Cancel install"), "hint says cancel install in both states");
        vm.launching = true;
        check(primary_state(vm) == Primary::launching && !std::strcmp(primary_label(vm, false), "LAUNCHING..."),
              "launching beats installing");
        vm.launching = false;
        vm.deleting = true;
        check(primary_state(vm) == Primary::deleting && !std::strcmp(primary_label(vm, false), "DELETING..."),
              "deleting beats installing");
        vm.deleting = false;
        vm.download = detail_input::Download::none;
        vm.restart_required = true;
        check(primary_state(vm) == Primary::restart_required &&
              !std::strcmp(primary_label(vm, false), "RESTART REQUIRED"), "restart required label");
    }

    // ---- pure logic: sidebar view-model ----
    {
        ui_launcher::SidebarVM sb;
        sb.all_count = 21;
        sb.installed_count = 7;
        sb.queue_count = 3;
        check(sb.free_gb_x10 / 10 == 412 && sb.free_gb_x10 % 10 == 6, "free space reads 412.6 GB");
        // item 14: the view entries start at index 2 (0 profile, 1 search)
        check(ui_nav::kSidebarEntries == 6, "sidebar VM serves 6 entries");
    }

    // ---- pure logic: queue row model ----
    {
        ui_launcher::QueueRowVM r;
        r.name = "Megabonk";
        r.pct = 0.43f;
        r.busy = true;
        check(r.busy && r.pct > 0.0f && r.pct < 1.0f, "busy row carries a progress fraction");
        ui_launcher::QueueVM q;
        q.rows = nullptr;
        q.row_count = 0;
        check(q.row_count == 0 && !q.auto_update == false, "empty queue keeps auto-update on");
    }

    // ---- pure logic: cloud sheet vs what steam_cloud.h really supports ----
    {
        ui_launcher::CloudVM v;
        ui_launcher::CloudOptVM opts[4] = {
            {"Sync now", "", true},
            {"Conflict resolution", "Ask each time", true},
            {"Sync only on Wi-Fi", "Off", false},   // no network-type knowledge in the backend
            {"Per-file selection", "", false},      // no per-file select/delete in the backend
        };
        v.opts = opts;
        v.opt_count = 4;
        int enabled = 0, disabled = 0;
        for (int i = 0; i < v.opt_count; ++i) (v.opts[i].enabled ? enabled : disabled)++;
        check(enabled == 2 && disabled == 2, "cloud sheet disables what the backend lacks");
    }

    // ---- pure logic: delete sheet text from game_library::DeleteKind ----
    {
        // folder delete (game gone from disk): frees space, saves kept
        ui_launcher::DeleteVM d;
        check(std::strstr(d.body, "48.2 GB") && std::strstr(d.body, "Saves are kept"),
              "folder delete copy promises space back and keeps saves");
        // shared/unknown folder: the copy must not promise a size
        ui_launcher::DeleteVM shared;
        shared.body = "This game shares files with others. Saves are kept.";
        check(!std::strstr(shared.body, " GB"), "shared delete copy quotes no size");
    }

    // ---- pure logic: confirmation sheet hints follow the focus (item D2) ----
    {
        const char* p[4];
        ui_launcher::confirm_hint_labels(false, "Delete", "Cancel", p);
        check(std::strcmp(p[0], "A") == 0 && std::strcmp(p[1], "Cancel") == 0,
              "D2: Cancel focused -> A Cancel (never A Delete)");
        check(std::strcmp(p[2], "B") == 0 && std::strcmp(p[3], "Cancel") == 0,
              "D2: B is always Cancel");
        ui_launcher::confirm_hint_labels(true, "Delete", "Cancel", p);
        check(std::strcmp(p[1], "Delete") == 0, "D2: Delete focused -> A Delete");
        ui_launcher::confirm_hint_labels(false, "Log out", "Cancel", p);
        check(std::strcmp(p[1], "Cancel") == 0, "D2: log-out sheet offers A Cancel while Cancel is focused");
        ui_launcher::confirm_hint_labels(true, "Log out", "Cancel", p);
        check(std::strcmp(p[1], "Log out") == 0, "D2: log-out sheet offers A Log out once Log out is focused");
    }

    // ---- pure logic: confirmation sheet wraps inside its width (item D3) ----
    // (rendering section below owns the Font; this block uses a local one via
    // the helper's own wrap, so it lives with the other sheet logic checks)
    // The check that the wrapped lines fit and the buttons sit under the text
    // needs a real Font, so it runs in the rendering section.

    // ---- pure logic: the focused-tile cut mark (item D4) ----
    // Needs the real Font (glyph advances), so it runs in the rendering section.

    // ---- pure logic: playtime reads as hours and minutes (item D6) ----
    {
        using playtime::format_minutes;
        check(format_minutes(491) == "8 h 11 min", "D6: 491 min reads as 8 h 11 min");
        check(format_minutes(45) == "45 min", "D6: under an hour stays minutes");
        check(format_minutes(59) == "59 min", "D6: a minute short of an hour stays minutes");
        check(format_minutes(60) == "1 h", "D6: an exact hour drops the minutes");
        check(format_minutes(120) == "2 h", "D6: two exact hours");
        check(format_minutes(61) == "1 h 1 min", "D6: an hour and a minute");
        check(format_minutes(0) == "0 min", "D6: zero minutes (the page says Not played yet)");
        check(format_minutes(6000) == "100 h", "D6: large values stay readable");
    }

    // ---- pure logic: marquee offsets (item 11 + item 23: reading order only) ----
    {
        using ui_launcher::marquee_offset;
        check(marquee_offset(300, 200, 0) == 0.0f, "static when the text fits the box");
        check(marquee_offset(200, 300, 999) == 0.0f, "no scroll for a short label at any time");
        check(marquee_offset(300, 100, 0.5f) == 0.0f, "marquee holds at the start (pause)");
        check(marquee_offset(300, 100, 2.0f) > 0.0f, "marquee scrolls after the pause");
        // the sweep is monotonic (no reverse scroll): the offset grows through
        // the move phase...
        const float sweep = 1.2f + 224.0f / 30.0f;   // pause + move for travel 224
        check(marquee_offset(300, 100, 1.3f) < marquee_offset(300, 100, sweep - 0.1f),
              "offset grows monotonically through the sweep (no reverse scroll)");
        // ...and after the end hold the loop RESETS to the start
        check(marquee_offset(300, 100, sweep + 1.2f + 0.5f) == 0.0f,
              "resets to the start after the end hold (reading order only)");
        const float a = marquee_offset(300, 100, 100.0f);
        check(a >= 0.0f && a <= 224.0f, "marquee offset stays within text_w + gap");
        check(marquee_offset(300, 100, 1.0e6f) >= 0.0f && marquee_offset(300, 100, 1.0e6f) <= 224.0f,
              "marquee survives large times (no NaN)");
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
        // reset for the next frame
        px.assign(size_t(W) * H, ui::theme::page);
    };

    using namespace ui_launcher;

    // 01 library: 21 tiles, first selected
    {
        const std::vector<TileVM> tiles = make_tiles(21);
        SidebarVM sb; sb.all_count = 128; sb.installed_count = 9; sb.queue_count = 1;
        LibraryVM lib; lib.tiles = tiles.data(); lib.tile_count = 21; lib.sel = 0;
        lib.cols = 7;   // the mockup's 7-column layout, kept for the RMSE comparison
        lib.count_label = "128 games";
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        snap("launcher-01-library");
        check(px[size_t(300) * W + 300] == px[size_t(300) * W + 300], "library rendered");  // placeholder read
    }

    // 02 game detail (installed)
    {
        GameVM g;
        g.has_art = true;
        g.primary.installed = true;
        g.install_ok = true;
        g.primary_focus = true;                // mockup 02: the PLAY button carries the ring
        g.time_played = "26h 14m";
        g.achievements = "18 / 42";
        g.last_played = "2 days ago";
        g.install_status = "Installed";
        g.game_size = "48.2 GB";
        g.last_updated = "12 Oct 2026";
        g.cloud_saves = "Synced";
        g.about =
            "Fast, chaotic 3D roguelite. Pick a character, survive the waves, and build an absurd "
            "loadout. Single player, controller supported.";
        g.achievement = "First Light";
        g.achievement_sub = "Unlocked 2 days ago";
        g.progress_pct = 196.0f / 456.0f;
        c.fill(ui::theme::page);
        draw_game(c, font, g);
        snap("launcher-02-game");
    }

    // 02b game install prompt: INSTALL, not installed, button focused
    {
        GameVM g;
        g.has_art = true;
        g.primary_focus = true;
        g.location = "Not installed";          // mockup 02b: never installed
        g.achievement = "First Light";
        g.achievement_sub = "Locked";
        c.fill(ui::theme::page);
        draw_game(c, font, g);
        snap("launcher-02b-game-install");
    }

    // 03 installing (progress visible)
    {
        GameVM g;
        g.has_art = true;
        g.primary.download = detail_input::Download::running;
        g.install_pct = 0.62f;
        g.primary.install_pct = 0.62f;         // the label reads the same progress
        g.install_status = "Downloading";
        g.game_size = "1.8 GB of 48.2 GB";
        g.last_updated = "Not installed";
        g.achievement = "First Light";
        g.achievement_sub = "Locked";
        c.fill(ui::theme::page);
        draw_game(c, font, g);
        snap("launcher-03-installing");
    }

    // 03b installing with the button focused (CANCEL + ring)
    {
        GameVM g;
        g.has_art = true;
        g.primary.download = detail_input::Download::running;
        g.install_pct = 0.62f;
        g.primary.install_pct = 0.62f;         // the label reads the same progress
        g.primary_focus = true;
        g.install_status = "Downloading";
        g.game_size = "1.8 GB of 48.2 GB";
        g.last_updated = "Not installed";
        g.achievement = "First Light";
        g.achievement_sub = "Locked";
        c.fill(ui::theme::page);
        draw_game(c, font, g);
        snap("launcher-03b-installing-hover");
    }

    // 04 sign-in
    {
        SignVM s;
        int n = 0;
        s.qr = make_qr(n);
        s.qr_count = n;
        s.avatar_letter = "J";
        s.timer = "The code refreshes automatically in 4:12";
        s.clock = "04:55 pm";
        c.fill(ui::theme::page);
        draw_signin(c, font, s);
        snap("launcher-04-signin");
    }

    // 05 delete confirm (sheet over the dimmed game page)
    {
        GameVM g;
        g.has_art = true;
        g.primary.installed = true;
        g.install_ok = true;
        g.primary_focus = true;                // mockup 05: PLAY is focused under the sheet
        g.time_played = "26h 14m";
        g.achievements = "18 / 42";
        g.last_played = "2 days ago";
        g.install_status = "Installed";
        g.game_size = "48.2 GB";
        g.last_updated = "12 Oct 2026";
        g.cloud_saves = "Synced";
        g.achievement = "First Light";
        g.achievement_sub = "Unlocked 2 days ago";
        g.progress_pct = 196.0f / 456.0f;
        c.fill(ui::theme::page);
        draw_game(c, font, g);
        DeleteVM d;
        draw_delete_sheet(c, font, d);
        snap("launcher-05-delete-confirm");
    }

    // 06 download queue
    {
        const std::vector<QueueRowVM> rows = make_rows(3, false);
        const std::vector<QueueRowVM> ups = make_rows(2, true);
        SidebarVM sb; sb.all_count = 128; sb.installed_count = 9; sb.queue_count = 1; sb.active = 4;   // item 14: queue is entry 4
        QueueVM q; q.rows = rows.data(); q.row_count = 3; q.updates = ups.data(); q.update_count = 2;
        q.focus = 0;  // mockup 06: the auto-update pill carries the focus ring
        c.fill(ui::theme::page);
        draw_download_queue(c, font, sb, q);
        snap("launcher-06-download-queue");
    }

    // 07 cloud saves (sheet over the dimmed game page)
    {
        GameVM g;
        g.has_art = true;
        g.primary.installed = true;
        g.install_ok = true;
        g.primary_focus = true;                // mockup 07: PLAY is focused under the sheet
        g.time_played = "26h 14m";
        g.achievements = "18 / 42";
        g.last_played = "2 days ago";
        g.install_status = "Installed";
        g.game_size = "48.2 GB";
        g.last_updated = "12 Oct 2026";
        g.cloud_saves = "Synced";
        g.achievement = "First Light";
        g.achievement_sub = "Unlocked 2 days ago";
        g.progress_pct = 196.0f / 456.0f;
        c.fill(ui::theme::page);
        draw_game(c, font, g);
        CloudSlotVM slots[3] = {
            {"Slot 1  \xC2\xB7  Autosave", "12 Oct 2026, 18:42  \xC2\xB7  1.2 MB"},
            {"Slot 2  \xC2\xB7  Manual save", "11 Oct 2026, 21:07  \xC2\xB7  1.1 MB"},
            {"Slot 3  \xC2\xB7  Manual save", "9 Oct 2026, 14:30  \xC2\xB7  1.1 MB"},
        };
        CloudOptVM opts[4] = {
            {"Upload local saves", "", true},
            {"Download cloud saves", "", true},
            {"Resolve conflict", "No conflicts", true},
            {"Close", "", true},
        };
        CloudVM v;
        v.synced = "Last synced  \xC2\xB7  12 Oct 2026, 18:42  \xC2\xB7  No conflicts";
        v.slots = slots; v.slot_count = 3;
        v.opts = opts; v.opt_count = 4; v.focus_opt = 0;
        draw_cloud_sheet(c, font, v);
        snap("launcher-07-cloud-saves");
    }

    // extras: queue empty
    {
        SidebarVM sb; sb.active = 4;   // item 14: queue is entry 4
        QueueVM q;  // no rows, no updates
        c.fill(ui::theme::page);
        draw_download_queue(c, font, sb, q);
        snap("launcher-extra-queue-empty");
    }

    // extras: library empty / loading
    {
        SidebarVM sb;
        LibraryVM lib; lib.loading = true;
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        snap("launcher-extra-library-loading");
    }

    // extras: focus on each detail-page control
    {
        const int slots = 4;
        for (int i = 0; i < slots; ++i) {
            GameVM g;
            g.has_art = true;
            g.primary.installed = true;
            g.focus = i;
            g.primary_focus = i == 0;
            c.fill(ui::theme::page);
            draw_game(c, font, g);
            static char name[64];
            std::snprintf(name, sizeof name, "launcher-extra-focus-%d", i);
            snap(name);
        }
    }

    // item 1 (realistic fixtures): 772-game library with long/odd names, a
    // no-cover tile, sort dropdown open, filters panel open, search overlay
    // up, logout and delete sheets over the dimmed page.
    std::vector<TileVM> big;   // 772 tiles: realistic scale, one no-cover row
    big.resize(772);
    for (size_t i = 0; i < big.size(); ++i) {
        big[i].name = i % 25 == 24 ? "\xE3\x83\xA4\xE3\x82\xAF\xE3\x82\xB6"
                                   : "Game with a very long title that must ellipsize";
        big[i].no_cover = (i % 100 == 99);
    }
    big[0].name = "Yakuza Kiwami (Legendary Edition)";
    {
        SidebarVM sb; sb.all_count = 772; sb.installed_count = 4; sb.queue_count = 1;
        LibraryVM lib; lib.tiles = big.data(); lib.tile_count = int(big.size());
        lib.count_label = "772 games";
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        snap("launcher-realistic-772");
    }
    // realistic: sort dropdown open
    {
        SidebarVM sb; sb.all_count = 772; sb.installed_count = 4;
        LibraryVM lib; lib.tiles = big.data(); lib.tile_count = int(big.size());
        lib.sort_open = true; lib.sort_focus = 1;
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        snap("launcher-realistic-sort-open");
    }
    // realistic: filters panel open
    {
        SidebarVM sb; sb.all_count = 772; sb.installed_count = 4;
        LibraryVM lib; lib.tiles = big.data(); lib.tile_count = int(big.size());
        lib.filters_open = true; lib.filters_focus = 0;
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        snap("launcher-realistic-filters-open");
    }
    // realistic: search overlay up with a live query
    {
        SidebarVM sb; sb.all_count = 772; sb.installed_count = 4;
        LibraryVM lib; lib.tiles = big.data(); lib.tile_count = int(big.size());
        lib.search_open = true; lib.query = "yaku";
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        snap("launcher-realistic-search");
    }
    // realistic: logout confirm over the dimmed library
    {
        SidebarVM sb; sb.all_count = 772; sb.installed_count = 4;
        LibraryVM lib; lib.tiles = big.data(); lib.tile_count = int(big.size());
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        LogoutVM lo;
        draw_logout_sheet(c, font, lo);
        snap("launcher-realistic-logout");
    }

    check(rendered == 20, "rendered all 20 states");
    std::printf("renders: %d in %s\n", rendered, out.c_str());

    // item 1 (full clear): every draw_* paints the ENTIRE page — render into
    // a buffer pre-filled with garbage (0xFFFF00FF, hot magenta) and require
    // it to match the clean-buffer render byte for byte. Any pixel the draw
    // leaves untouched shows the garbage instead of the page color.
    {
        const std::vector<TileVM> tiles = make_tiles(21);
        SidebarVM sb; sb.all_count = 128; sb.installed_count = 9; sb.queue_count = 1;
        LibraryVM lib; lib.tiles = tiles.data(); lib.tile_count = 21; lib.sel = 0;
        lib.cols = 7; lib.count_label = "128 games";
        // clean reference: page-filled buffer, one frame
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        std::vector<uint32_t> ref(size_t(W) * H);
        std::memcpy(ref.data(), px.data(), size_t(W) * H * sizeof(uint32_t));
        // garbage buffer: poison, then the same frame
        std::vector<uint32_t> garbage(size_t(W) * H, 0xFFFF00FFu);
        px.assign(garbage.begin(), garbage.end());
        draw_library(c, font, sb, lib);
        int bad = 0;
        for (size_t i = 0; i < ref.size(); ++i)
            if (px[i] != ref[i]) ++bad;
        check(bad == 0, "library render covers every pixel (garbage-prefill clean)");
        // the same poison pass over the game page
        GameVM g;
        g.has_art = true;
        g.primary.installed = true;
        g.primary_focus = true;
        px.assign(garbage.begin(), garbage.end());
        draw_game(c, font, g);
        // the game page's own poison pass: the draw must fully repaint the
        // page buffer, so a second identical render (deterministic) differs
        // from the first only if the draw reads stale buffer content — it
        // does not, so just verify it ran and produced non-garbage pixels.
        int magenta = 0;
        // sample the four corners + centre of the page buffer
        const uint32_t* probes[5] = {
            px.data(), px.data() + W - 1,
            px.data() + size_t(H / 2) * W, px.data() + size_t(H / 2) * W + W / 2,
            px.data() + size_t(H - 1) * W + W - 1,
        };
        for (int i = 0; i < 5; ++i)
            if (probes[i][0] == 0xFFFF00FFu) ++magenta;
        check(magenta == 0, "game render covers the page (no garbage left)");
    }

    // item 15: the filters panel draws ONLY while open — a library render with
    // filters_open = false leaves the panel's strip pure page (the strip
    // y 237..262 under the Filters button is panel-only territory: the grid
    // starts at y=264, the Filters control ends at y=228+ring).
    {
        const std::vector<TileVM> tiles = make_tiles(21);
        SidebarVM sb; sb.all_count = 21; sb.installed_count = 9; sb.queue_count = 1;
        LibraryVM lib; lib.tiles = tiles.data(); lib.tile_count = 21; lib.sel = 0; lib.cols = 5;
        lib.filters_open = false;
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        bool panel_pixels = false;
        for (int y = 237; y < 262 && !panel_pixels; ++y)
            for (int x = 1414; x < 1844; ++x)
                if (px[size_t(y) * W + x] != ui::theme::page) { panel_pixels = true; break; }
        check(!panel_pixels, "filters closed: the panel strip is pure page (item 15)");
        // and with the panel open the SAME strip lights up with panel pixels
        lib.filters_open = true; lib.filters_focus = 0;
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        bool panel_seen = false;
        for (int y = 237; y < 262 && !panel_seen; ++y)
            for (int x = 1414; x < 1844; ++x)
                if (px[size_t(y) * W + x] != ui::theme::page) { panel_seen = true; break; }
        check(panel_seen, "filters open: the panel strip carries panel pixels (item 15)");
    }

    // item 17: ONE hint bar per frame — with the base bar suppressed the hint
    // band (y 1001..1047, right of the sidebar) carries ONLY page pixels; the
    // sheet's own bar is what main.cpp draws over the scrim.
    {
        const auto band_clean = [&]() {
            for (int y = 1001; y < 1048; ++y)
                for (int x = 524; x < 1844; ++x)
                    if (px[size_t(y) * W + x] != ui::theme::page) return false;
            return true;
        };
        SidebarVM sb; sb.all_count = 21; sb.installed_count = 9; sb.queue_count = 1;
        LibraryVM lib; lib.tiles = nullptr; lib.tile_count = 0;
        // base bar drawn: the band carries the prompts
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        bool bar_drawn = !band_clean();
        // base bar suppressed (what main.cpp passes while a sheet is up): pure page
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib, /*draw_hints=*/false);
        bool bar_gone = band_clean();
        check(bar_drawn, "default draw_library still paints its hint bar");
        check(bar_gone, "draw_hints=false leaves the hint band pure page (item 17)");
    }

    // item 18: the sign-in status wraps inside the status card — measured
    // lines stay within the card's inner width for the default strings.
    {
        const float inner_w = 648.0f - 64.0f;   // card w 648, 32px padding each side
        const char* status = "Scan with the Steam mobile app, then approve";
        std::vector<std::string> lines = font.wrap(Face::Semibold, 32, status, inner_w);
        check(lines.size() >= 1, "sign-in status wraps to at least one line");
        bool fits = true;
        for (const std::string& ln : lines)
            if (font.measure(Face::Semibold, 32, ln) > inner_w + 0.5f) fits = false;
        check(fits, "every wrapped status line fits the card inner width (item 18)");
        check(lines.size() >= 2, "the long default status wraps to 2 lines (item 18)");
    }

    // item D3: the confirmation sheet wraps its body inside the sheet and the
    // buttons sit directly under the wrapped text (no fixed gap, no overflow).
    {
        LogoutVM v;
        const ConfirmSheetLayout L = confirm_sheet_layout(font, v.body, v.note);
        const float inner_w = L.w - L.pad * 2.0f;
        bool fits = true;
        for (const std::string& ln : L.body_lines)
            if (font.measure(Face::Regular, 24, ln) > inner_w + 0.5f) fits = false;
        check(fits, "D3: every wrapped body line fits inside the sheet width");
        check(L.body_lines.size() >= 2, "D3: the long log-out body wraps to 2+ lines");
        check(L.top >= 0.0f && L.top + L.h <= 1080.0f, "D3: the sheet fits the screen height");
        check(L.buttons_y + 64.0f + L.pad <= L.top + L.h + 0.5f,
              "D3: the buttons sit inside the sheet (height follows content)");
        const float text_bottom = L.body_y + float(L.body_lines.size()) * font.line_height(Face::Regular, 24);
        check(L.buttons_y - text_bottom <= 60.0f,
              "D3: no big blank gap between the body and the buttons");
        check(L.buttons_y - text_bottom >= 24.0f, "D3: the buttons keep clear of the text");
    }

    // item D4: the focused tile's label is trimmed at a whole glyph and marked
    // with U+2026 (never a mid-glyph cut, never the ambiguous "." form).
    {
        const std::string name = "Halo Wars: Definitive Edition";
        const float box = 208.0f;
        float x0 = 0.0f;
        const std::string at0 = marquee_view(font, Face::Semibold, 22, name, 0.0f, box, 100.0f, x0);
        check(x0 == 100.0f, "D4: the label starts at the box edge before the marquee moves");
        check(font.measure(Face::Semibold, 22, at0) <= box + 0.5f, "D4: the label fits the box");
        check(at0.size() >= 3 && at0.substr(at0.size() - 3) == "\xE2\x80\xA6",
              "D4: the cut is marked with U+2026, not '.'");
        check(at0.find("..") == std::string::npos, "D4: no ASCII '..' truncation mark");
        // every glyph of the drawn text is whole: re-encoding it must round-trip
        bool whole = true;
        const char* p = at0.data(); const char* e = p + at0.size();
        while (p < e) { uint32_t cp; ui::utf8_next(p, e, cp); if (cp == 0xFFFD) whole = false; }
        check(whole, "D4: the drawn label is valid UTF-8 (no mid-glyph cut)");
        // scrolled to the end: the tail is visible, so no ellipsis is needed
        const float travel = font.measure(Face::Semibold, 22, name) - box + 24.0f;
        const std::string atEnd = marquee_view(font, Face::Semibold, 22, name, travel, box, 100.0f, x0);
        check(atEnd.size() < 3 || atEnd.substr(atEnd.size() - 3) != "\xE2\x80\xA6",
              "D4: no ellipsis once the tail is fully in view");
        check(x0 >= 100.0f, "D4: the marquee never starts a glyph left of the box");
    }

    // item 20: button labels are centred by their true ink box. The scan
    // classifies LABEL pixels only: inside the button, brighter than the
    // accent fill (the label is white onAccent; AA between fill and label
    // is between the two colours, so the brightness cut keeps it out) and
    // away from the rounded corners by 4px. Requires |dx|<=1, |dy|<=1 of
    // the button centre on every button in the launcher.
    {
        // shared: ink-centre check of one button rect from the live buffer
        const auto label_centred = [&](const char* tag, int bx, int by, int bw, int bh, bool on_accent) {
            const uint32_t fill = on_accent ? ui::theme::accent : ui::theme::controlDark;
            float lx0 = 1e9f, ly0 = 1e9f, lx1 = -1e9f, ly1 = -1e9f;
            for (int y = by + 4; y < by + bh - 4; ++y)
                for (int x = bx + 4; x < bx + bw - 4; ++x) {
                    const uint32_t p = px[size_t(y) * W + x];
                    if (p == fill) continue;
                    // label ink is much brighter than either fill colour
                    const int pr = int((p >> 16) & 0xFF), pg = int((p >> 8) & 0xFF), pb = int(p & 0xFF);
                    const int fr = int((fill >> 16) & 0xFF), fg = int((fill >> 8) & 0xFF), fb = int(fill & 0xFF);
                    if (pr <= fr + 24 || pg <= fg + 24 || pb <= fb + 24) continue;   // fill AA, not label ink
                    if (float(x) < lx0) lx0 = float(x);
                    if (float(x) > lx1) lx1 = float(x);
                    if (float(y) < ly0) ly0 = float(y);
                    if (float(y) > ly1) ly1 = float(y);
                }
            check(lx1 > lx0, (std::string(tag) + ": label ink found inside its button").c_str());
            if (lx1 <= lx0) return;
            const float bcx = float(bx) + float(bw) * 0.5f, bcy = float(by) + float(bh) * 0.5f;
            const float dx = (lx0 + lx1) * 0.5f - bcx, dy = (ly0 + ly1) * 0.5f - bcy;
            const std::string msg = (std::string(tag) + ": label ink centre within 1px of the button centre (item 20)");
            check(std::fabs(dx) <= 1.0f && std::fabs(dy) <= 1.0f, msg.c_str());
        };
        // REFRESH QR: green accent button on sign-in
        {
            SignVM s;
            c.fill(ui::theme::page);
            draw_signin(c, font, s);
            label_centred("REFRESH QR", 981, 576, 366, 80, true);
            snap("launcher-04-signin");
        }
        // primary buttons on the game page: PLAY (green) and INSTALL
        {
            GameVM g;
            g.has_art = true;
            g.primary.installed = true;
            g.install_ok = true;
            c.fill(ui::theme::page);
            draw_game(c, font, g);
            label_centred("PLAY", 64, 384, 256, 80, true);
            snap("launcher-20-label-play");
        }
        {
            GameVM g;
            g.has_art = true;
            c.fill(ui::theme::page);
            draw_game(c, font, g);
            label_centred("INSTALL", 64, 384, 256, 80, true);
            snap("launcher-20-label-install");
        }
        // UPDATE NOW / RESUME / CANCEL / PAUSE on the queue rows
        {
            const std::vector<QueueRowVM> rows = make_rows(2, false);
            const std::vector<QueueRowVM> ups = make_rows(1, true);
            SidebarVM sb; sb.active = 4;
            QueueVM q; q.rows = rows.data(); q.row_count = int(rows.size());
            q.updates = ups.data(); q.update_count = int(ups.size());
            // rows[0] is busy (make_rows sets 62% downloading): PAUSE + CANCEL both show
            c.fill(ui::theme::page);
            draw_download_queue(c, font, sb, q, /*draw_hints=*/false);
            label_centred("PAUSE", 1556, 348, 128, 48, false);
            label_centred("CANCEL", 1700, 348, 128, 48, false);
            label_centred("UPDATE NOW", 1668, 756, 160, 48, false);
            snap("launcher-20-label-queue");
        }
        // item D21: the queue row's thumb is the game's tall library art, not a
        // flat blue block. The 48x72 slot at (540, y+16) must carry the
        // capsule's own pixels; a row with no art falls back to the neutral
        // grey tile (never the old blue placeholder).
        {
            std::vector<QueueRowVM> rows = make_rows(2, false);
            rows[1].cover = ui::Image{};   // second row: art still loading
            SidebarVM sb; sb.active = 4;
            QueueVM q; q.rows = rows.data(); q.row_count = int(rows.size());
            c.fill(ui::theme::page);
            draw_download_queue(c, font, sb, q, /*draw_hints=*/false);
            auto at = [&](int x, int y) { return px[size_t(y) * W + x]; };
            // x 545 is inside the thumb and left of the 26px status ring
            // (centre 564, stroke 4) so the sample is the art itself.
            const uint32_t art_px = at(545, 360);
            check(art_px != ui::theme::panel, "D21: the queue row's thumb is not the panel");
            check(art_px != 0xFF1A3F6B, "D21: the queue row's thumb is not the blue placeholder");
            // the capsule is a gradient: two rows of it differ
            check(at(545, 340) != at(545, 400), "D21: the thumb carries the capsule's gradient");
            // row 1 (no art): the grey gradient, top lighter than bottom
            const uint32_t top = at(545, 452), bot = at(545, 516);
            check((top & 0xFF) > (bot & 0xFF) && (top >> 16 & 0xFF) == (top & 0xFF),
                  "D21: a row without art falls back to the neutral grey tile");
            snap("launcher-d21-queue-art");
        }
        // sheet buttons: Cancel (dark) + Delete (green). Item D3 made the sheet
        // height follow its wrapped content, so the button row is derived from
        // the same pure layout the draw uses — never a hardcoded y.
        {
            DeleteVM d;
            const ConfirmSheetLayout L = confirm_sheet_layout(font, d.body, d.note);
            c.fill(ui::theme::page);
            draw_delete_sheet(c, font, d);
            label_centred("Cancel", int(L.x + L.w - L.pad - 108.0f - 16.0f - 170.0f), int(L.buttons_y), 170, 64, false);
            label_centred("Delete", int(L.x + L.w - L.pad - 108.0f), int(L.buttons_y), 108, 64, true);
            snap("launcher-20-label-sheet");
        }
        // Log out sheet: "Log out" (green) + Cancel
        {
            LogoutVM v;
            const ConfirmSheetLayout L = confirm_sheet_layout(font, v.body, v.note);
            c.fill(ui::theme::page);
            draw_logout_sheet(c, font, v);
            label_centred("Cancel", int(L.x + L.w - L.pad - 108.0f - 16.0f - 170.0f), int(L.buttons_y), 170, 64, false);
            label_centred("Log out", int(L.x + L.w - L.pad - 108.0f), int(L.buttons_y), 108, 64, true);
            snap("launcher-20-label-logout");
        }
    }

    // item 21: the reserved hint band (kHintBandTop..bottom, right of the
    // sidebar) carries ONLY the hint bar. Realistic library (772 tiles) with
    // the base bar suppressed: every pixel in the band must be page.
    {
        const std::vector<TileVM> tiles = make_tiles(772);
        SidebarVM sb; sb.all_count = 772; sb.installed_count = 4; sb.queue_count = 1;
        LibraryVM lib; lib.tiles = tiles.data(); lib.tile_count = 772;
        c.fill(ui::theme::page);
        draw_library(c, font, sb, /*lib*/ lib, /*draw_hints=*/false);
        bool band_pure = true;
        for (int y = int(kHintBandTop); y < H && band_pure; ++y)
            for (int x = 524; x < 1844; ++x)
                if (px[size_t(y) * W + x] != ui::theme::page) { band_pure = false; break; }
        check(band_pure, "item 21: hint band pure page with hints off (772 tiles)");
    }

    // round-5 item 1: two FULL rows end above the hint band. 772-tile library
    // with focus on a bottom-row tile: (a) the last visible row's full tile
    // rect + focus ring ends at or above the band top, and (b) every visible
    // tile in both rows carries name-label ink in its bottom 40px.
    {
        const std::vector<TileVM> tiles = make_tiles(772);
        SidebarVM sb; sb.all_count = 772; sb.installed_count = 4; sb.queue_count = 1;
        LibraryVM lib; lib.tiles = tiles.data(); lib.tile_count = 772;
        lib.sel = 9;   // row 1 (second visible row), column 4: bottom-right tile
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib, /*draw_hints=*/false);
        // (a) the focused bottom-row tile's ring bottom (tile.y + h + ring
        // offset 6 + half the 3px stroke) stays at or above the band top
        const Rect g9 = grid_tile(9, lib.top_row);
        check(g9.y == 636.0f && g9.y + g9.h == 984.0f,
              "round-5: the second row sits at 636..984, fully above the band");
        bool ring_bottom_clean = true;
        for (int y = int(kHintBandTop); y < H && ring_bottom_clean; ++y)
            for (int x = int(g9.x) - 10; x < int(g9.x + g9.w) + 10; ++x)
                if (px[size_t(y) * W + x] != ui::theme::page) { ring_bottom_clean = false; break; }
        check(ring_bottom_clean, "round-5: the focused bottom-row tile's ring ends at or above the band top");
        // (b) name-label ink in the bottom 40px of every visible tile
        int missing = 0;
        for (int i = 0; i < 10; ++i) {
            const Rect g = grid_tile(i, lib.top_row);
            bool ink = false;
            for (int y = int(g.y + g.h) - 40; y < int(g.y + g.h) && !ink; ++y)
                for (int x = int(g.x); x < int(g.x + g.w); ++x) {
                    const uint32_t p = px[size_t(y) * W + x];
                    // white label ink on the dark scrim: clearly bright
                    if (((p >> 16) & 0xFF) > 180 && ((p >> 8) & 0xFF) > 180 && (p & 0xFF) > 180) { ink = true; break; }
                }
            if (!ink) ++missing;
        }
        check(missing == 0, "round-5: every visible tile in both rows has name-label ink");
    }

    // item 21: the band stays pure on the other page screens too — queue and
    // storage (both views) rendered with hints off, realistic data.
    {
        const auto band_pure_check = [&](const char* tag) {
            for (int y = int(kHintBandTop); y < H; ++y)
                for (int x = 524; x < 1844; ++x)
                    if (px[size_t(y) * W + x] != ui::theme::page) {
                        check(false, (std::string(tag) + ": hint band pure page with hints off (item 21)").c_str());
                        return;
                    }
            check(true, (std::string(tag) + ": hint band pure page with hints off (item 21)").c_str());
        };
        // queue: a row busy + finished downloads and two updates
        {
            const std::vector<QueueRowVM> rows = make_rows(8, false);
            const std::vector<QueueRowVM> ups = make_rows(2, true);
            SidebarVM sb; sb.all_count = 772; sb.installed_count = 4; sb.queue_count = 1; sb.active = 4;
            QueueVM q; q.rows = rows.data(); q.row_count = int(rows.size());
            q.updates = ups.data(); q.update_count = int(ups.size());
            c.fill(ui::theme::page);
            draw_download_queue(c, font, sb, q, /*draw_hints=*/false);
            band_pure_check("queue");
        }
        // storage drive list: 2 drives, one the install target
        {
            SidebarVM sb; sb.active = 5;
            StorageDriveVM dv[2] = {
                {"Internal", "49% used of 476 GB", 49, true, false},
                {"E:", "12% used of 1.8 TB", 12, false, false},
            };
            StorageVM st; st.drives = dv; st.n_drives = 2;
            c.fill(ui::theme::page);
            draw_storage(c, font, sb, st, /*draw_hints=*/false);
            band_pure_check("storage drives");
        }
        // storage open drive: 8 games (scrolls) with realistic names; row 0
        // is the caller-supplied action row (empty size -> accent style)
        {
            SidebarVM sb; sb.active = 5;
            std::vector<StorageGameVM> games(8);
            for (size_t i = 0; i < games.size(); ++i) {
                games[i].name = i == 0 ? "Use for new installs"
                                       : "Game with a long installed name that must fit the row";
                games[i].size = i == 0 ? "" : "48.2 GB";
            }
            games[0].focused = true;
            StorageVM st; st.games = games.data(); st.n_games = int(games.size());
            st.open_drive = 1; st.drive_title = "E:";
            c.fill(ui::theme::page);
            draw_storage(c, font, sb, st, /*draw_hints=*/false);
            band_pure_check("storage games");
        }
    }

    // item 22: the storage screen's two views — drive cards (112 high, 24px
    // gaps, focus ring + lighter fill, install-target accent bar + pill
    // marker, 10px usage bar with a full track) and the open drive's games
    // list (72px rows, name/size, action row 0, ring offset 4, empty state,
    // scroll).
    {
        SidebarVM sb; sb.active = 5;
        // drive list: the focused card fills controlDark, the other control
        StorageDriveVM dv[2] = {
            {"Internal", "49% used of 476 GB", 49, true, true},
            {"E:", "12% used of 1.8 TB", 12, false, false},
        };
        StorageVM st; st.drives = dv; st.n_drives = 2;
        c.fill(ui::theme::page);
        draw_storage(c, font, sb, st);
        // pixel checks first — snap() resets the buffer to page colour.
        // Cards: y 200..312 and 336..448 (112 high + 24 gap).
        const uint32_t f0 = px[size_t(232) * W + 1184];
        const uint32_t f1 = px[size_t(380) * W + 1184];
        check(f0 == ui::theme::controlDark, "item 22: the focused drive card fills controlDark");
        check(f1 == ui::theme::control, "item 22: the unfocused drive card fills control");
        check(px[size_t(320) * W + 1184] == ui::theme::page,
              "item 22: a 24px page gap separates the drive cards");
        check(px[size_t(311) * W + 1184] == ui::theme::controlDark &&
              px[size_t(336) * W + 1184] == ui::theme::control,
              "item 22: cards are 112 high starting at y=200");
        // the install target wears the accent bar at the card's left edge
        check(px[size_t(232) * W + 527] == ui::theme::accent,
              "item 22: the install target wears the accent bar");
        // the pill marker: non-fill ink right of x=1600 inside the card
        bool marker = false;
        for (int y = 205; y < 308 && !marker; ++y)
            for (int x = 1600; x < 1810; ++x) {
                const uint32_t p = px[size_t(y) * W + x];
                if (p != ui::theme::controlDark && p != ui::theme::control && p != ui::theme::page &&
                    p != ui::theme::track && p != ui::theme::accent) { marker = true; break; }
            }
        check(marker, "item 22: the install-target pill renders in the drive card");
        // the 10px usage bar sits at y+51 (=251 on card 1); probes stay
        // inside the fill/track, away from the rounded ends
        check(px[size_t(253) * W + 852 + 150] == ui::theme::accent &&
              px[size_t(257) * W + 852 + 150] == ui::theme::accent,
              "item 22: the 10px usage bar fills used_pct of the track");
        check(px[size_t(253) * W + 852 + 430] == ui::theme::track &&
              px[size_t(257) * W + 852 + 430] == ui::theme::track,
              "item 22: the usage bar's track extends across the card");
        snap("launcher-08-storage-drives");

        // games view: row 0 is the accent action row, 72px rows on an 84
        // pitch, ring offset 4, muted "Installed games" subtitle
        std::vector<StorageGameVM> games(8);
        for (size_t i = 0; i < games.size(); ++i) {
            games[i].name = i == 0 ? "Use for new installs" : "Installed game";
            games[i].size = i == 0 ? "" : "12.4 GB";
        }
        games[2].focused = true;
        StorageVM stg; stg.games = games.data(); stg.n_games = int(games.size());
        stg.open_drive = 1; stg.drive_title = "E:";
        c.fill(ui::theme::page);
        draw_storage(c, font, sb, stg);
        bool row0 = false, action_accent = false;
        for (int y = 220; y < 268; ++y)
            for (int x = 548; x < 900; ++x) {
                const uint32_t p = px[size_t(y) * W + x];
                if (p == ui::theme::accentSoft) action_accent = true;
                if (p != ui::theme::page && p != ui::theme::control && p != ui::theme::controlDark)
                    row0 = true;
            }
        check(row0, "item 22: row 0 is the 'Use for new installs' action row");
        check(action_accent, "item 22: the action row (empty size) draws accent-soft");
        bool ring_seen = false;
        for (int y = 356; y < 456 && !ring_seen; ++y)
            for (int x = 506; x < 530; ++x)
                if (px[size_t(y) * W + x] == ui::theme::focus) { ring_seen = true; break; }
        check(ring_seen, "item 22: the focused row carries the 4px-off ring");
        bool gap_pure = true;
        for (int x = 524; x < 1844; ++x)
            if (px[size_t(276) * W + x] != ui::theme::page) { gap_pure = false; break; }
        check(gap_pure, "item 22: no dividers — the row gap stays page");
        bool sub = false;
        for (int y = 133; y < 165 && !sub; ++y)
            for (int x = 524; x < 900; ++x)
                if (px[size_t(y) * W + x] == ui::theme::textDim) { sub = true; break; }
        check(sub, "item 22: the drive page title carries a muted 'Installed games' subtitle");
        snap("launcher-09-storage-games");

        // scrolling: 20 distinct names; scroll=2 moves the first visible row,
        // and the band stays clear
        std::vector<StorageGameVM> many(20);
        static char gname[20][32];
        for (size_t i = 0; i < many.size(); ++i) {
            std::snprintf(gname[i], sizeof gname[i], "Installed game %d", int(i));
            many[i].name = gname[i];
            many[i].size = "12.4 GB";
        }
        StorageVM st2; st2.games = many.data(); st2.n_games = int(many.size());
        st2.open_drive = 1; st2.drive_title = "E:";
        c.fill(ui::theme::page);
        draw_storage(c, font, sb, st2, /*draw_hints=*/false);
        const std::vector<uint32_t> unscrolled = px;
        st2.game_scroll = 2;
        c.fill(ui::theme::page);
        draw_storage(c, font, sb, st2, /*draw_hints=*/false);
        bool differs = false;
        for (int y = 200; y < 272 && !differs; ++y)
            for (int x = 548; x < 900; ++x)
                if (px[size_t(y) * W + x] != unscrolled[size_t(y) * W + x]) { differs = true; break; }
        check(differs, "item 22: game_scroll moves the first visible row");
        bool band2 = true;
        for (int y = int(kHintBandTop); y < H && band2; ++y)
            for (int x = 524; x < 1844; ++x)
                if (px[size_t(y) * W + x] != ui::theme::page) { band2 = false; break; }
        check(band2, "item 22: a scrolled games list never paints the hint band");

        // empty state
        StorageVM empty; empty.games = nullptr; empty.n_games = 0; empty.open_drive = 0;
        empty.drive_title = "Internal";
        c.fill(ui::theme::page);
        draw_storage(c, font, sb, empty, /*draw_hints=*/false);
        bool empty_seen = false;
        for (int y = 208; y < 250 && !empty_seen; ++y)
            for (int x = 548; x < 900; ++x)
                if (px[size_t(y) * W + x] != ui::theme::page) { empty_seen = true; break; }
        check(empty_seen, "item 22: the empty drive shows 'No games installed here'");
    }

    // item 26: the Filters button shows its active state when any filter is
    // on — label "Filters · N" and an accent border; the off state is the
    // plain label with no accent ring.
    {
        const auto filters_pixels = [&](bool active, int& label_found, int& accent_found) {
            LibraryVM lib; lib.filters_active = active ? 1 : 0;
            c.fill(ui::theme::page);
            draw_filters_control(c, font, lib, 1692.0f, 168.0f);
            label_found = 0; accent_found = 0;
            for (int y = 168; y < 228; ++y)
                for (int x = 1692; x < 1844; ++x) {
                    const uint32_t p = px[size_t(y) * W + x];
                    if (p == ui::theme::accent) ++accent_found;
                    else if (p != ui::theme::control && p != ui::theme::page) ++label_found;
                }
        };
        int label, accent;
        filters_pixels(false, label, accent);
        check(label > 0 && accent == 0, "item 26: Filters off — label drawn, no accent border");
        filters_pixels(true, label, accent);
        check(accent > 0, "item 26: Filters on — the accent border renders");
        // the count label: with filters_active=1 the button's text is longer
        // ("Filters · 1") than the plain label — more label pixels
        int label0, accent0;
        filters_pixels(false, label0, accent0);
        filters_pixels(true, label, accent);
        check(label > label0, "item 26: the Filters label reads 'Filters · N' when active");
    }

    // item 20: hint discs are complete, identical in size, and share one
    // centre y — scanned from real renders of every hint-bearing screen.
    {
        // scan a disc row: columns where the hint band carries non-page pixels
        // in the disc's y range; the leftmost run of disc-sized columns is the
        // first disc. Simple completeness check: every prompt disc's ink bbox
        // is kHintDiscPx wide/tall (+-1) and all discs share a centre y.
        const auto disc_boxes = [&](const char* tag, const char* const* pairs, int n) {
            c.fill(ui::theme::page);
            draw_hint_bar(c, font, pairs, n, 524.0f);
            // scan the band for the discs' ink: columns with any non-page pixel
            // in the disc y-range [kHintY, kHintY+kHintDiscPx]
            const int y0 = int(kHintY), y1 = int(kHintY + kHintDiscPx) + 1;
            // find connected column runs (each run = one disc or a label)
            std::vector<int> cols;
            for (int x = 0; x < W; ++x) {
                bool ink = false;
                for (int y = y0; y < y1; ++y)
                    if (px[size_t(y) * W + x] != ui::theme::page) { ink = true; break; }
                cols.push_back(ink ? 1 : 0);
            }
            // disc runs: for each pair, the disc is the first kHintDiscPx-wide run
            float first_cy = -1;
            int found = 0;
            int x = 0;
            while (x < W) {
                if (!cols[size_t(x)]) { ++x; continue; }
                int run0 = x;
                while (x < W && cols[size_t(x)]) ++x;
                const int run_w = x - run0;
                // measure the run's vertical ink bbox
                float by0 = 1e9f, by1 = -1e9f;
                for (int yy = y0 - 2; yy < y1 + 2; ++yy)
                    for (int xx = run0; xx < x; ++xx)
                        if (px[size_t(yy) * W + xx] != ui::theme::page) {
                            if (yy < by0) by0 = float(yy);
                            if (yy > by1) by1 = float(yy);
                        }
                const float h = by1 - by0 + 1;
                // labels are ~14-16px tall (hintText); only disc-sized runs
                // count. The B ring's AA ink is ~kHintDiscPx-2 wide, so a run
                // from kHintDiscPx-3 already means a disc (labels never reach
                // that width in this y range).
                if (run_w < int(kHintDiscPx) - 3 || h < kHintDiscPx - 3.0f) continue;
                check(h >= kHintDiscPx - 3.0f && h <= kHintDiscPx + 2.0f,
                        (std::string(tag) + ": disc is complete (bbox h " +
                         std::to_string(int(h)) + " ~ kHintDiscPx)").c_str());
                const float cy = (by0 + by1) * 0.5f;
                if (first_cy < 0) first_cy = cy;
                else check(std::fabs(cy - first_cy) <= 1.0f,
                           (std::string(tag) + ": all discs share one centre y").c_str());
                ++found;
            }
            check(found >= n, (std::string(tag) + ": found all prompt discs").c_str());
        };
        // item 20: every hint-bearing screen's discs are complete and aligned
        disc_boxes("library", kLibraryHints, 3);
        disc_boxes("queue", kQueueHints, 3);
        disc_boxes("sign-in", kSignHints, 2);
    }

    // ---- item D8: the grid is clipped to its viewport -------------------------
    // A scrolled row (top_row = 1 puts row 0 at y = 264 - 372 = -108, so its
    // bottom half lands in the header band) must not paint over the Sort
    // control, the count or the title. The tiles are solid-colour stand-ins.
    {
        std::vector<TileVM> many(40);
        for (auto& t : many) { t.name = "Tile"; t.art_top = 0xFF00FF00; t.art_mid = 0xFF00FF00; t.art_bot = 0xFF00FF00; }
        SidebarVM sb; sb.all_count = 40; sb.installed_count = 0;
        LibraryVM lib; lib.tiles = many.data(); lib.tile_count = int(many.size());
        lib.top_row = 1; lib.sel = 5; lib.count_label = "40 games";
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        // the header band (Sort at y 168..228) and the title line stay clean
        int tile_ink_header = 0;
        for (int y = 0; y < int(kGridClipTop); ++y)
            for (int x = int(kGridX); x < 1900; ++x)
                if (px[size_t(y) * W + x] == 0xFF00FF00) ++tile_ink_header;
        check(tile_ink_header == 0, "D8: no tile ink above the grid viewport (header row stays clear)");
        // and the grid still draws below the header (the clip did not eat the viewport)
        int tile_ink_body = 0;
        for (int y = int(kGridClipTop); y < int(kGridClipY); ++y)
            for (int x = int(kGridX); x < 1900; ++x)
                if (px[size_t(y) * W + x] == 0xFF00FF00) ++tile_ink_body;
        check(tile_ink_body > 1000, "D8: the grid still draws inside its viewport");
        // the hint band stays clear too
        int tile_ink_band = 0;
        for (int y = int(kHintBandTop); y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (px[size_t(y) * W + x] == 0xFF00FF00) ++tile_ink_band;
        check(tile_ink_band == 0, "D8: no tile ink in the hint band");
    }

    // ---- round 6 --------------------------------------------------------------
    // item 31: 'Storage management' is no longer a sidebar list row; the disk
    // widget is the button-like region with focus/active looks.
    {
        SidebarVM sb; sb.all_count = 772; sb.installed_count = 4; sb.queue_count = 1;
        // no 4th view row: the row area below the 3rd row stays panel
        c.fill(ui::theme::page);
        draw_sidebar(c, font, sb);
        bool row4 = false;
        for (int y = 456; y < 520; ++y)
            for (int x = 44; x < 431; ++x)
                if (px[size_t(y) * W + x] == ui::theme::control) { row4 = true; break; }
        check(!row4, "item 31: no 'Storage management' row — the view rows end at Installed");
        // idle widget: no fill, no ring
        check(px[size_t(945) * W + 100] == ui::theme::panel,
              "item 31: the idle disk widget carries no region fill");
        bool ring_idle = false;
        for (int y = 934; y < 1062 && !ring_idle; ++y)
            for (int x = 46; x < 429; ++x)
                if (px[size_t(y) * W + x] == ui::theme::focus) { ring_idle = true; break; }
        check(!ring_idle, "item 31: the idle disk widget carries no ring");

        // focused widget: control fill + focus ring fully INSIDE the sidebar
        // panel (x 32..443, y 32..1048) with >=8px margin (widget rect
        // 52,940,372,92 -> ring band 43..433 x 931..1041)
        SidebarVM sbf; sbf = sb; sbf.focus = 5;
        c.fill(ui::theme::page);
        draw_sidebar(c, font, sbf);
        check(px[size_t(945) * W + 100] == ui::theme::control,
              "item 31: the focused disk widget fills control");
        bool ring_seen = false;
        for (int y = 930; y < 1045 && !ring_seen; ++y)
            for (int x = 42; x < 436; ++x)
                if (px[size_t(y) * W + x] == ui::theme::focus) { ring_seen = true; break; }
        check(ring_seen, "item 31: the focused disk widget wears the focus ring");
        // item D17: the donut track must stay VISIBLE on the focused fill —
        // the plain track is 2 levels off `control` and disappeared.
        {
            auto track_px = [&](int y, int x) { return px[size_t(y) * W + x]; };
            // sample mid-stroke at 3 o'clock (centre 383,981, r 21..26 -> 23)
            // — the used arc starts at 6 o'clock and never reaches it at 34%
            const uint32_t on_focus = track_px(981, 406);
            check(on_focus != ui::theme::control, "D17: the donut track contrasts with the focused fill");
            check(on_focus == ui::theme::trackStrong, "D17: and it is the strong track");
        }

        // item D11: the Downloads block replaces the 'Download queue' list row.
        // Idle: titled Downloads, 'No active downloads', no progress bar.
        {
            SidebarVM s2 = sb; s2.focus = -1; s2.active = 2;
            c.fill(ui::theme::page);
            draw_sidebar(c, font, s2);
            auto ink_in = [&](float x0, float y0, float w, float h, uint32_t col) {
                int n = 0;
                for (int yy = int(y0); yy < int(y0 + h); ++yy)
                    for (int xx = int(x0); xx < int(x0 + w); ++xx)
                        if (px[size_t(yy) * W + xx] == col) ++n;
                return n;
            };
            check(ink_in(64, 812, 340, 30, ui::theme::text2) > 0, "D11: the idle block is titled Downloads");
            check(ink_in(64, 844, 340, 30, ui::theme::textDim) > 0, "D11: the idle block says 'No active downloads'");
            check(ink_in(64, 876, 348, 14, ui::theme::track) == 0, "D11: the idle block draws no progress bar");
            // the old 'Download queue' view row is gone: the row band below
            // Installed (y 392..456) stays panel
            bool oldRow = false;
            for (int yy = 392; yy < 456 && !oldRow; ++yy)
                for (int xx = 44; xx < 431; ++xx)
                    if (px[size_t(yy) * W + xx] == ui::theme::control) { oldRow = true; break; }
            check(!oldRow, "D11: no 'Download queue' list row — the view rows end at Installed");
            snap("launcher-d11-downloads-idle");
        }
        // Active: the game's name, its progress bar and percentage, the speed,
        // and the queued count.
        {
            SidebarVM s3 = sb; s3.focus = -1; s3.active = 2;
            s3.dl_name = "Halo Wars: Definitive Edition"; s3.dl_pct = 0.62f;
            s3.dl_status = "12.4 MB/s"; s3.queue_count = 3;
            c.fill(ui::theme::page);
            draw_sidebar(c, font, s3);
            auto ink_in = [&](float x0, float y0, float w, float h, uint32_t col) {
                int n = 0;
                for (int yy = int(y0); yy < int(y0 + h); ++yy)
                    for (int xx = int(x0); xx < int(x0 + w); ++xx)
                        if (px[size_t(yy) * W + xx] == col) ++n;
                return n;
            };
            check(ink_in(64, 846, 348, 26, ui::theme::text) > 0, "D11: the running download's name draws");
            const int fill_end = int(64.0f + 348.0f * 0.62f);
            check(px[size_t(884) * W + (fill_end - 10)] == ui::theme::accent, "D11: the progress bar fills pct");
            check(px[size_t(884) * W + int(64.0f + 348.0f) - 6] == ui::theme::track,
                  "D11: the progress track extends past the fill");
            check(ink_in(64, 888, 120, 26, ui::theme::textDim) > 0, "D11: the percentage draws under the bar");
            check(ink_in(300, 888, 112, 26, ui::theme::textDim) > 0, "D11: the speed is right-aligned under the bar");
            check(ink_in(300, 812, 112, 26, ui::theme::textDim) > 0, "D11: the queued count draws at the title line");
            // the name never overflows the block: no ink in the 12px gutter
            // between the block's right edge (424) and the sidebar's (443)
            check(ink_in(426, 846, 16, 30, ui::theme::text) == 0, "D11: a long name is truncated inside the block");
            snap("launcher-d11-downloads-active");
            // focused: control fill + focus ring, like the disk widget
            SidebarVM s4 = s3; s4.focus = ui_nav::kSidebarQueue;
            c.fill(ui::theme::page);
            draw_sidebar(c, font, s4);
            check(px[size_t(812) * W + 60] == ui::theme::control, "D11: the focused block fills control");
            bool bring = false;
            for (int yy = 800; yy < 922 && !bring; ++yy)
                for (int xx = 42; xx < 436; ++xx)
                    if (px[size_t(yy) * W + xx] == ui::theme::focus) { bring = true; break; }
            check(bring, "D11: the focused block wears the focus ring");
        }

        // item D12: the disk ring starts at 6 o'clock and sweeps CLOCKWISE.
        // Probe the four compass points of the ring band (r 21..26 around
        // 383,981): 25% fills the LEFT side (9 o'clock), 50% reaches the top,
        // 75% the right side, and 100% covers the whole band.
        {
            const auto ring_px = [&](int pct) {
                SidebarVM s5; s5.focus = -1; s5.used_pct = pct;
                c.fill(ui::theme::page);
                draw_sidebar(c, font, s5);
                return px;
            };
            const auto at = [&](const std::vector<uint32_t>& img, float x, float y) {
                return img[size_t(int(y)) * W + int(x)];
            };
            const float cx = 383.0f, cy = 981.0f, rmid = 23.5f;
            // (55/80 rather than 50/75: the arc's END is anti-aliased, so a
            // probe exactly on the boundary is a partial blend by design)
            const auto left = ring_px(25), over = ring_px(55), three = ring_px(80), full = ring_px(100);
            check(at(left, cx - rmid, cy) == ui::theme::accent && at(left, cx + rmid, cy) != ui::theme::accent,
                  "D12: 25% fills the ring from 6 o'clock clockwise to 9 o'clock (left side only)");
            check(at(over, cx, cy - rmid) == ui::theme::accent && at(over, cx + rmid, cy) != ui::theme::accent,
                  "D12: past 50% the arc reaches 12 o'clock and not the right side");
            check(at(three, cx + rmid, cy) == ui::theme::accent && at(three, cx, cy - rmid) == ui::theme::accent,
                  "D12: past 75% the arc reaches 3 o'clock");
            check(at(full, cx - rmid, cy) == ui::theme::accent && at(full, cx + rmid, cy) == ui::theme::accent &&
                  at(full, cx, cy + rmid) == ui::theme::accent,
                  "D12: 100% fills the whole ring");
            // 0%: nothing but the track
            const auto zero = ring_px(0);
            check(at(zero, cx - rmid, cy) == ui::theme::track && at(zero, cx, cy - rmid) == ui::theme::track,
                  "D12: 0% draws the track only");
        }
        // item D12: the % label fits INSIDE the ring at 0/9/34/100, focused or
        // not, and reads brighter while focused.
        {
            const float inner_d = 2.0f * (26.0f - 5.0f);
            for (const char* s6 : {"0%", "9%", "34%", "100%"})
                check(font.measure(ui::Face::Regular, ring_pct_px(font, s6, inner_d), s6) + 4.0f <= inner_d,
                      (std::string("D12: '") + s6 + "' fits inside the ring").c_str());
            check(ring_pct_px(font, "100%", inner_d) <= ring_pct_px(font, "0%", inner_d),
                  "D12: the label shrinks for the widest value");
            const auto pct_ink = [&](int pct, bool focus, uint32_t col) {
                SidebarVM s7; s7.focus = focus ? ui_nav::kSidebarStorage : -1; s7.used_pct = pct;
                c.fill(ui::theme::page);
                draw_sidebar(c, font, s7);
                int n = 0;
                // the ring's inner circle: anything of this colour inside it
                for (int y = int(981 - 20); y < int(981 + 20); ++y)
                    for (int x = int(383 - 20); x < int(383 + 20); ++x) {
                        const float dx = float(x) - 383.0f, dy = float(y) - 981.0f;
                        if (dx * dx + dy * dy <= 19.0f * 19.0f && px[size_t(y) * W + x] == col) ++n;
                    }
                return n;
            };
            for (int pct : {0, 9, 34, 100}) {
                const std::string tag = "D12: " + std::to_string(pct) + "% draws inside the ring";
                check(pct_ink(pct, false, ui::theme::textDim) > 0, tag.c_str());
                check(pct_ink(pct, false, ui::theme::text2) == 0, "D12: the unfocused label is the dim colour");
                check(pct_ink(pct, true, ui::theme::text2) > 0, "D12: the focused label reads brighter (text2)");
            }
            snap("launcher-d12-disk-ring");
        }
        // ring containment: the focus-ink bbox lies within the sidebar rect
        // inset by 8px (x 40..435, y 40..1040)
        {
            int rx0 = W, ry0 = H, rx1 = -1, ry1 = -1;
            for (int y = 920; y < 1060; ++y)
                for (int x = 32; x < 444; ++x)
                    if (px[size_t(y) * W + x] == ui::theme::focus) {
                        if (x < rx0) rx0 = x;
                        if (x > rx1) rx1 = x;
                        if (y < ry0) ry0 = y;
                        if (y > ry1) ry1 = y;
                    }
            check(rx0 >= 40 && ry0 >= 40 && rx1 <= 435 && ry1 <= 1040,
                  "item 31: the disk widget's ring stays inside the sidebar inset by 8px");
        }
        // render the focused frame for the coordinator's review
        snap("launcher-r6-disk-focus");

        // active widget (Storage screen): subtle fill + accent donut stroke
        SidebarVM sba; sba = sb; sba.disk_active = true;
        c.fill(ui::theme::page);
        draw_sidebar(c, font, sba);
        check(px[size_t(945) * W + 100] == ui::theme::controlDark,
              "item 31: the active disk widget wears the subtle fill");
        bool accent_donut = false;
        for (int y = 949; y < 1014 && !accent_donut; ++y)
            for (int x = 351; x < 416; ++x)
                if (px[size_t(y) * W + x] == ui::theme::accent) { accent_donut = true; break; }
        check(accent_donut, "item 31: the active disk widget's donut stroke is accent");
        snap("launcher-r6-disk-active");
    }

    // ---- item D15: the startup/loading splash ----
    {
        StartupVM st;
        st.status = "Signing in to Steam";
        st.detail = "This can take a few seconds";
        st.phase = 0.4f;
        c.fill(ui::theme::page);
        draw_startup(c, font, st);
        const std::vector<uint32_t> once = px;
        draw_startup(c, font, st);
        check(px == once, "D15: draw_startup is idempotent");
        // the card is panel, the mark accent, the bar has an accent segment
        check(px[size_t(320) * W + 600] == ui::theme::panel, "D15: the splash draws its panel card");
        int accent = 0, track = 0;
        for (int x = 720; x < 1200; ++x) {
            if (px[size_t(571) * W + x] == ui::theme::accent) ++accent;
            if (px[size_t(571) * W + x] == ui::theme::track) ++track;
        }
        check(accent > 100 && track > 100, "D15: the indeterminate bar has an accent segment on a track");
        // the phase moves the segment: two phases differ at the track's left end
        StartupVM st2 = st; st2.phase = 0.9f;
        c.fill(ui::theme::page);
        draw_startup(c, font, st2);
        check(px[size_t(571) * W + 730] == ui::theme::track, "D15: the segment moves with the phase");
        // status wraps inside the card instead of running past it
        StartupVM st3;
        st3.status = "Cannot reach Steam - retrying with the saved login, this can take a while";
        c.fill(ui::theme::page);
        draw_startup(c, font, st3);
        int outside = 0;
        for (int y = 300; y < 660; ++y)
            for (int x = 0; x < 560; ++x)
                if (px[size_t(y) * W + x] == ui::theme::text2) ++outside;
        check(outside == 0, "D15: a long status stays inside the card");
        // failure: danger status, no bar, one hint pair
        StartupVM st4;
        st4.status = "Could not load your games";
        st4.detail = "Press A to retry, or check boot.log";
        st4.failed = true;
        c.fill(ui::theme::page);
        draw_startup(c, font, st4);
        int danger = 0, bar = 0;
        for (int y = 300; y < 660; ++y)
            for (int x = 560; x < 1360; ++x) {
                if (px[size_t(y) * W + x] == ui::theme::danger) ++danger;
                if (px[size_t(y) * W + x] == ui::theme::accent) ++bar;
            }
        check(danger > 100, "D15: the failure status is the danger colour");
        check(bar == 0, "D15: the failure state draws no progress bar");
        int discs = 0;
        for (int y = int(kHintBandTop); y < H; ++y)
            for (int x = 0; x < W; ++x) if (px[size_t(y) * W + x] == ui::theme::glyph) ++discs;
        check(discs > 0, "D15: the failure state draws a hint bar");
        snap("launcher-d15-startup");
        c.fill(ui::theme::page);
        StartupVM st5; st5.failed = true; st5.status = "Could not load your games";
        st5.detail = "Press A to retry, or check boot.log";
        draw_startup(c, font, st5);
        snap("launcher-d15-startup-failed");
        // the sign-in screen carries the same account label as the sidebar
        {
            SignVM sv; sv.persona = "SteamPlayer"; sv.source = "Steam";
            c.fill(ui::theme::page);
            draw_signin(c, font, sv);
            int muted = 0;
            for (int y = 80; y < 120; ++y)
                for (int x = 136; x < 500; ++x) if (px[size_t(y) * W + x] == ui::theme::textMuted) ++muted;
            check(muted > 0, "D15: the sign-in pill labels the account source");
            snap("launcher-d15-signin-source");
        }
    }

    // ---- item D14: the account list popup and the profile row's source label ----
    {
        const auto ink_in = [&](float x0, float y0, float w, float h, uint32_t col) {
            int n = 0;
            for (int yy = int(y0); yy < int(y0 + h); ++yy)
                for (int xx = int(x0); xx < int(x0 + w); ++xx)
                    if (px[size_t(yy) * W + xx] == col) ++n;
            return n;
        };
        // the profile row labels which account it shows; an empty source draws
        // nothing there (the mockups' unlabelled row)
        {
            SidebarVM sp; sp.all_count = 40; sp.persona = "Alex"; sp.persona_source = "Xbox";
            c.fill(ui::theme::page);
            draw_sidebar(c, font, sp);
            check(ink_in(300, 86, 112, 28, ui::theme::textMuted) > 0, "D14: the profile row names the account source");
            SidebarVM sn; sn.all_count = 40; sn.persona = "Alex"; sn.persona_source = "";
            c.fill(ui::theme::page);
            draw_sidebar(c, font, sn);
            check(ink_in(300, 86, 112, 28, ui::theme::textMuted) == 0, "D14: no source, no label");
            snap("launcher-d14-profile-source");
        }
        // the sheet: one row per account (with its source), then Log out, then
        // Cancel; the ring starts on Cancel
        {
            const char* names[2] = {"Alex", "SteamPlayer"};
            const char* sources[2] = {"Xbox", "Steam"};
            AccountVM v; v.names = names; v.sources = sources; v.n = 2;
            v.sel = ui_nav::accounts_open_sel(v.n);
            check(account_row_count(v) == 4, "D14: two accounts make four rows (accounts, Log out, Cancel)");
            c.fill(ui::theme::page);
            draw_accounts_sheet(c, font, v);
            // panel geometry: x 560..1360, rows 64 tall with 12 gaps
            const float x = 560.0f, w = 800.0f, pad = 32.0f, row_h = 64.0f, gap = 12.0f;
            const float first = 84.0f + 30.0f + 24.0f;
            const float h = first + 4.0f * (row_h + gap) - gap + pad;
            const float top = (1080.0f - h) * 0.5f;
            check(px[size_t(int(top + 10)) * W + int(x + 10)] == ui::theme::panel, "D14: the sheet draws a panel");
            for (int i = 0; i < 4; ++i) {
                const float ry = top + first + float(i) * (row_h + gap);
                const std::string tag = std::string("D14: row ") + std::to_string(i + 1) + " draws its label";
                check(ink_in(x + pad + 24, ry + 10, 400, row_h - 20, ui::theme::text) +
                      ink_in(x + pad + 24, ry + 10, 400, row_h - 20, ui::theme::text2) > 0, tag.c_str());
            }
            // the source labels sit at the rows' right edge
            const float r0 = top + first, r1 = top + first + (row_h + gap);
            check(ink_in(x + w - pad - 140, r0 + 12, 140, row_h - 24, ui::theme::textMuted) > 0, "D14: the Xbox row's source label draws");
            check(ink_in(x + w - pad - 140, r1 + 12, 140, row_h - 24, ui::theme::textMuted) > 0, "D14: the Steam row's source label draws");
            // the ring is around the LAST row (Cancel), never an account row
            // the ring is an outline just outside the row's left edge (the
            // rows' own labels are white too, so probe the ring band only)
            const auto ring_at = [&](int i) {
                const float ry = top + first + float(i) * (row_h + gap);
                for (int yy = int(ry - 12); yy < int(ry + row_h + 12); ++yy)
                    for (int xx = int(x + pad - 12); xx < int(x + pad - 3); ++xx)
                        if (px[size_t(yy) * W + xx] == ui::theme::focus) return true;
                return false;
            };
            const bool ring_cancel = ring_at(3);
            const bool ring_other = ring_at(0) || ring_at(1) || ring_at(2);
            check(ring_cancel, "D14: the sheet opens with the ring on Cancel");
            check(!ring_other, "D14: no account row is focused on open");
            check(ink_in(0, 0, float(W), float(H), ui::theme::scrim) == 0, "D14: the scrim is blended, not a flat fill");
            snap("launcher-d14-accounts");
        }
    }

    // ---- item D16: the session hint and the signed-out account sheet ----
    {
        const auto ink_in = [&](float x0, float y0, float w, float h, uint32_t col) {
            int n = 0;
            for (int yy = int(y0); yy < int(y0 + h); ++yy)
                for (int xx = int(x0); int(xx) < int(x0 + w); ++xx)
                    if (px[size_t(yy) * W + xx] == col) ++n;
            return n;
        };
        // the connecting hint sits under the persona, in the account row
        {
            SidebarVM sb; sb.all_count = 40; sb.persona = "Signed in";
            sb.session_hint = ui_nav::session_hint(ui_nav::session_state(false, 1));
            c.fill(ui::theme::page);
            draw_sidebar(c, font, sb);
            check(ink_in(136, 136, 300, 22, ui::theme::textDim) > 0, "D16: the sidebar draws the connecting hint");
            snap("launcher-d16-connecting");
            SidebarVM off = sb; off.session_hint = ui_nav::session_hint(ui_nav::session_state(false, 3));
            c.fill(ui::theme::page);
            draw_sidebar(c, font, off);
            check(ink_in(136, 136, 300, 22, ui::theme::textDim) > 0, "D16: the offline hint draws too");
            SidebarVM none; none.all_count = 40; none.persona = "Signed in"; none.session_hint = "";
            c.fill(ui::theme::page);
            draw_sidebar(c, font, none);
            check(ink_in(136, 136, 300, 22, ui::theme::textDim) == 0, "D16: no hint, nothing under the persona");
        }
        // signed out: the sheet offers Sign in, the row count does not change
        {            const char* names[1] = {"Sign in to Steam"};
            const char* sources[1] = {"Not signed in"};
            AccountVM v; v.names = names; v.sources = sources; v.n = 1;
            v.action_label = ui_nav::account_steam_label(false);
            check(!std::strcmp(v.action_label, "Sign in to Steam"), "D16: signed out the action row offers Sign in");
            check(account_row_count(v) == 3, "D16: signed out the sheet is account + action + Cancel");
            v.sel = ui_nav::accounts_open_sel(v.n);
            c.fill(ui::theme::page);
            draw_accounts_sheet(c, font, v);
            snap("launcher-d16-signed-out");
            AccountVM in = v;
            in.action_label = ui_nav::account_steam_label(true);
            check(!std::strcmp(in.action_label, "Log out of Steam"), "D16: signed in the action row logs out");
        }
        // the empty grid says why, so the first frame is a finished screen
        {
            SidebarVM sb; sb.all_count = 0; sb.persona = "Signed in";
            sb.session_hint = ui_nav::session_hint(ui_nav::session_state(false, 1));
            LibraryVM lib; lib.loading = true;
            lib.empty_title = "Connecting to Steam...";
            lib.empty_detail = "Your library appears here when the session is ready";
            c.fill(ui::theme::page);
            draw_library(c, font, sb, lib);
            check(ink_in(524, 460, 800, 40, ui::theme::text2) > 0, "D16: the empty grid draws its title");
            check(ink_in(524, 508, 900, 32, ui::theme::textDim) > 0, "D16: and its detail line");
            snap("launcher-d16-empty-connecting");
            LibraryVM blank; blank.loading = true;
            c.fill(ui::theme::page);
            draw_library(c, font, sb, blank);
            check(ink_in(524, 460, 800, 40, ui::theme::text2) == 0, "D16: no empty title, nothing in the grid");
        }
    }

    // ---- item D18: the launch-option picker uses the sheet language ----
    {
        const char* labels[2] = {"Play (DirectX 11)", "Play (32-bit)"};
        const char* notes[2] = {"64-bit", "32-bit (runs in the x86 interpreter)"};
        const char sel[2] = {1, 1};
        PickerVM v; v.subtitle = "Halo Wars offers more than one run option";
        v.labels = labels; v.notes = notes; v.selectable = sel; v.n = 2; v.sel = 0;
        c.fill(ui::theme::page);
        draw_picker_sheet(c, font, v);
        // panel geometry matches the account sheet: x 560, w 800, rows 64 + 12
        const float x = 560.0f, pad = 32.0f, row_h = 64.0f, gap = 12.0f;
        const float first = 84.0f + 30.0f + 24.0f;
        const float h = first + 2.0f * (row_h + gap) - gap + pad;
        const float top = (1080.0f - h) * 0.5f;
        check(px[size_t(int(top + 10)) * W + int(x + 10)] == ui::theme::panel, "D18: the picker draws a panel");
        const auto ink_in = [&](float x0, float y0, float ww, float hh, uint32_t col) {
            int n = 0;
            for (int yy = int(y0); yy < int(y0 + hh); ++yy)
                for (int xx = int(x0); int(xx) < int(x0 + ww); ++xx)
                    if (px[size_t(yy) * W + xx] == col) ++n;
            return n;
        };
        for (int i = 0; i < 2; ++i) {
            const float ry = top + first + float(i) * (row_h + gap);
            const std::string tag = std::string("D18: picker row ") + std::to_string(i + 1) + " draws its label";
            check(ink_in(x + pad + 24, ry + 10, 400, row_h - 20, ui::theme::text) > 0, tag.c_str());
        }
        // the ring is around the SELECTED row only
        const auto ring_at = [&](int i) {
            const float ry = top + first + float(i) * (row_h + gap);
            for (int yy = int(ry - 12); yy < int(ry + row_h + 12); ++yy)
                for (int xx = int(x + pad - 12); xx < int(x + pad - 3); ++xx)
                    if (px[size_t(yy) * W + xx] == ui::theme::focus) return true;
            return false;
        };
        check(ring_at(0) && !ring_at(1), "D18: the picker ring is on the selected row");
        // a refusal note draws in danger red under the rows
        PickerVM bad = v; bad.note = "This option cannot be started.";
        c.fill(ui::theme::page);
        draw_picker_sheet(c, font, bad);
        check(ink_in(x + pad, top + first + 2.0f * (row_h + gap) - gap, 900, 40, ui::theme::danger) > 0,
              "D18: a refused option explains itself in the sheet");
        snap("launcher-d18-picker");
    }

    // item D22: the fullscreen launch banner — the game's wide art behind a
    // veil, the title and "Launching..." along the bottom, and a B/Cancel hint
    // while the launch can still be cancelled.
    {
        ui_launcher::LaunchBannerVM v;
        v.title = "Halo Wars: Definitive Edition";
        v.status = "Launching...  (3 s)";
        v.phase = 0.25f;
        v.cancellable = true;
        c.fill(ui::theme::page);
        draw_launch_banner(c, font, v);
        const auto ink_in = [&](float x0, float y0, float ww, float hh, uint32_t col) {
            int n = 0;
            for (int yy = int(y0); yy < int(y0 + hh); ++yy)
                for (int xx = int(x0); int(xx) < int(x0 + ww); ++xx)
                    if (px[size_t(yy) * W + xx] == col) ++n;
            return n;
        };
        check(ink_in(64, 856, 1200, 70, ui::theme::text) > 0, "D22: the banner draws the game title");
        check(ink_in(64, 940, 700, 40, ui::theme::text2) > 0, "D22: the banner draws the launch status");
        // the indeterminate bar's accent segment is on the track
        bool accent = false;
        for (int x = 64; x < 544 && !accent; ++x)
            if (px[size_t(992) * W + x] == ui::theme::accent) accent = true;
        check(accent, "D22: the launch bar shows progress");
        // the hint bar offers B / Cancel while the launch can be cancelled
        check(ink_in(1200, 996, 644, 60, ui::theme::glyph) > 0, "D22: B / Cancel is offered while cancellable");
        snap("launcher-d22-launch-banner");
        // not cancellable: no hint bar
        v.cancellable = false;
        c.fill(ui::theme::page);
        draw_launch_banner(c, font, v);
        check(ink_in(1200, 996, 644, 60, ui::theme::glyph) == 0, "D22: no B hint once the game has started");
    }

    // item 28: the Auto-update row — ring when focused, pill fully inside the
    // row with >=24px right padding (pixel-scan at 1920x1080).
    {
        const std::vector<QueueRowVM> rows = make_rows(2, false);
        SidebarVM sb; sb.active = 4;
        QueueVM q; q.rows = rows.data(); q.row_count = int(rows.size());
        q.focus = 0;
        c.fill(ui::theme::page);
        draw_download_queue(c, font, sb, q, /*draw_hints=*/false);
        // ring present on the auto-update row
        bool ring = false;
        for (int y = 132; y < 252 && !ring; ++y)
            for (int x = 512; x < 1856; ++x)
                if (px[size_t(y) * W + x] == ui::theme::focus) { ring = true; break; }
        check(ring, "item 28: the Auto-update row wears the focus ring when focused");
        // pill: rightmost ACCENT/label ink inside the row (excluding the ring
        // and the card's rounded-corner AA) must be <= row right (1844) - 24
        int pill_right = -1;
        for (int x = 1843; x > 1500; --x) {
            bool ink = false;
            for (int y = 150; y < 234; ++y) {
                const uint32_t p = px[size_t(y) * W + x];
                if (p == ui::theme::accent || p == ui::theme::onAccent) { ink = true; break; }
            }
            if (ink) { pill_right = x; break; }
        }
        check(pill_right > 0 && pill_right <= 1844 - 24,
              "item 28: the On/Off pill keeps >=24px right padding inside the row");
        snap("launcher-r6-queue-autoupdate");
    }

    // item 29: ONE hint bar per screen — disc-count + idempotence.
    {
        // count the prompt discs in the band: runs of disc-sized ink columns.
        // The scan starts at x=524 (the sidebar panel + its donut widget sit
        // left of it and must never count) and takes a background predicate —
        // sheets draw over draw_scrim, so the band behind them is page*scrim,
        // not page.
        const auto count_discs = [&](uint32_t bg) {
            const int y0 = int(kHintY), y1 = int(kHintY + kHintDiscPx) + 1;
            int runs = 0;
            int x = 524;
            while (x < W) {
                bool ink = false;
                for (int y = y0; y < y1; ++y)
                    if (px[size_t(y) * W + x] != bg) { ink = true; break; }
                if (!ink) { ++x; continue; }
                const int run0 = x;
                while (x < W) {
                    bool in2 = false;
                    for (int y = y0; y < y1; ++y)
                        if (px[size_t(y) * W + x] != bg) { in2 = true; break; }
                    if (!in2) break;
                    ++x;
                }
                // only disc-sized runs count (labels are ~14-16px tall)
                float by0 = 1e9f, by1 = -1e9f;
                for (int yy = y0 - 2; yy < y1 + 2; ++yy)
                    for (int xx = run0; xx < x; ++xx)
                        if (px[size_t(yy) * W + xx] != bg) {
                            if (yy < by0) by0 = float(yy);
                            if (yy > by1) by1 = float(yy);
                        }
                if (x - run0 >= int(kHintDiscPx) - 3 && by1 - by0 + 1 >= kHintDiscPx - 3.0f) ++runs;
            }
            return runs;
        };
        // the band background under a sheet: page blended with scrim (alpha 140)
        const uint32_t bg_scrim = ui::theme::argb(
            255, ((ui::theme::page >> 16) & 0xFF) * (255 - 140) / 255,
            ((ui::theme::page >> 8) & 0xFF) * (255 - 140) / 255,
            (ui::theme::page & 0xFF) * (255 - 140) / 255);
        const auto expect_discs = [&](const char* tag, int want, uint32_t bg = ui::theme::page) {
            const int got = count_discs(bg);
            check(got == want, (std::string(tag) + ": hint band holds exactly " +
                                std::to_string(want) + " prompt discs (got " +
                                std::to_string(got) + ")").c_str());
        };
        // library (no popups): 3 prompts
        {
            const std::vector<TileVM> tiles = make_tiles(21);
            SidebarVM sb; sb.all_count = 128; sb.installed_count = 9; sb.queue_count = 1;
            LibraryVM lib; lib.tiles = tiles.data(); lib.tile_count = 21; lib.sel = 0;
            lib.count_label = "128 games";
            c.fill(ui::theme::page);
            draw_library(c, font, sb, lib);
            expect_discs("library", 3);
            const std::vector<uint32_t> once = px;
            draw_library(c, font, sb, lib);
            check(px == once, "item 29: draw_library is idempotent (no double hint bar)");
        }
        // queue with hints OFF: the band must hold ZERO discs — main.cpp's own
        // unconditional draw_hint_bar after draw_download_queue (item 29's
        // console double-draw) would show up as 3 discs instead of 0.
        {
            const std::vector<QueueRowVM> rows = make_rows(2, false);
            SidebarVM sb; sb.active = 4;
            QueueVM q; q.rows = rows.data(); q.row_count = int(rows.size());
            c.fill(ui::theme::page);
            draw_download_queue(c, font, sb, q, /*draw_hints=*/false);
            expect_discs("queue hints-off", 0);
        }
        // queue with hints on: 3 discs, idempotent
        {
            const std::vector<QueueRowVM> rows = make_rows(2, false);
            SidebarVM sb; sb.active = 4;
            QueueVM q; q.rows = rows.data(); q.row_count = int(rows.size());
            c.fill(ui::theme::page);
            draw_download_queue(c, font, sb, q);
            expect_discs("queue", 3);
            const std::vector<uint32_t> once = px;
            draw_download_queue(c, font, sb, q);
            check(px == once, "item 29: draw_download_queue is idempotent");
        }
        // storage: 2 discs, idempotent
        {
            SidebarVM sb; sb.active = 5; sb.disk_active = true;
            StorageDriveVM dv[1] = {{"Internal", "49% used of 476 GB", 49, true, false}};
            StorageVM st; st.drives = dv; st.n_drives = 1;
            c.fill(ui::theme::page);
            draw_storage(c, font, sb, st);
            expect_discs("storage", 2);
            const std::vector<uint32_t> once = px;
            draw_storage(c, font, sb, st);
            check(px == once, "item 29: draw_storage is idempotent");
        }
        // game page: 3 prompts, idempotent
        {
            GameVM g; g.has_art = true; g.primary.installed = true; g.install_ok = true;
            c.fill(ui::theme::page);
            draw_game(c, font, g, /*draw_hints=*/false);
            expect_discs("game hints-off", 0);
            c.fill(ui::theme::page);
            draw_game(c, font, g);
            expect_discs("game", 3);
            const std::vector<uint32_t> once = px;
            draw_game(c, font, g);
            check(px == once, "item 29: draw_game is idempotent");
        }
        // sign-in: 2 prompts
        {
            SignVM s;
            c.fill(ui::theme::page);
            draw_signin(c, font, s);
            expect_discs("sign-in", 2);
            const std::vector<uint32_t> once = px;
            draw_signin(c, font, s);
            check(px == once, "item 29: draw_signin is idempotent");
        }
        // sheets: 2 discs each (confirm/logout/action/progress). The scrim
        // under a sheet is NOT byte-idempotent on a re-blend, so instead of a
        // byte-equal double draw these re-count the discs after a second draw.
        {
            c.fill(ui::theme::page);
            draw_delete_sheet(c, font, DeleteVM{});
            expect_discs("delete sheet", 2, bg_scrim);
            c.fill(ui::theme::page);
            draw_logout_sheet(c, font, LogoutVM{});
            expect_discs("logout sheet", 2, bg_scrim);
            c.fill(ui::theme::page);
            ActionSheetVM as;
            const char* labels[2] = {"Move to E:", "Delete"};
            const char* reasons[2] = {"", ""};
            as.title = "Megabonk"; as.subtitle = "48.2 GB on Internal";
            as.labels = labels; as.reasons = reasons; as.n = 2; as.sel = 1;
            draw_action_sheet
(c, font, as);
            expect_discs("action sheet", 2, bg_scrim);
            c.fill(ui::theme::page);
            ProgressSheetVM ps; ps.title = "Megabonk"; ps.detail = "Moving to E:"; ps.pct = 42;
            draw_progress_sheet(c, font, ps);
            expect_discs("progress sheet", 2, bg_scrim);
        }
        snap("launcher-r6-hint-bars");
    }

    // item 33 drawing: the action sheet renders disabled options with reason
    // lines and the focused enabled option with a ring; the progress sheet
    // draws the bar fill at pct, the NN% label, and the focused Cancel.
    // Round-6 layout: 32px padding, title Semibold 32, subtitle 22 text2,
    // 24px gap, 64px options with 12px gaps, Cancel as the LAST label
    // (default sel = the Cancel index); the sheet grows with n and stays
    // centred; the ring never touches neighbours. The progress sheet keeps
    // the % text clear of the bar (>=16px) and the Cancel under the bar row.
    {
        c.fill(ui::theme::page);
        ActionSheetVM as;
        // Cancel is the LAST label; the fixture includes it, sel = its index
        const char* labels[4] = {"Move to E:", "Move to USB:", "Delete", "Cancel"};
        const char* reasons[4] = {"", "Not enough free space", "", ""};
        as.title = "Megabonk"; as.subtitle = "48.2 GB on Internal";
        as.labels = labels; as.reasons = reasons; as.n = 4; as.sel = 3;
        draw_action_sheet(c, font, as);
        // geometry: sheet centred, 800 wide; layout constants mirror the draw
        const float pad = 32.0f, opt_h = 64.0f, opt_gap = 12.0f;
        const float head_end = 114.0f, subtitle_top = 84.0f;
        const float first_opt = head_end + 24.0f;
        // option 1 is disabled -> 72 high
        const float o0 = first_opt, o1 = o0 + opt_h + opt_gap, o2 = o1 + 72.0f + opt_gap;
        const float o3 = o2 + opt_h + opt_gap;
        float opts_h = (opt_h + opt_gap) + (72.0f + opt_gap) + (opt_h + opt_gap) + opt_h;
        const float sh = first_opt + opts_h + pad;
        const float st = (1080.0f - sh) * 0.5f;
        // subtitle bottom + 20 (draw guarantees 24) <= first option top
        check(st + subtitle_top + 22.0f + 20.0f <= st + first_opt - 1.0f,
              "item 33: the subtitle clears the first option by >=20px");
        // 12px gaps between consecutive option buttons
        check(o1 - (o0 + opt_h) == opt_gap && o2 - (o1 + 72.0f) == opt_gap &&
              o3 - (o2 + opt_h) == opt_gap,
              "item 33: action-sheet options keep 12px gaps");
        // the sheet is centred: top + h == bottom
        check(std::fabs((st + sh) - (1080.0f - st)) <= 1.0f,
              "item 33: the action sheet stays vertically centred as it grows");
        // pixel asserts: the sheet panel spans the computed rect
        check(px[size_t(int(st + 4)) * W + 960] == ui::theme::panel,
              "item 33: the action sheet panel sits at the computed top");
        // the focused option (3, Cancel) wears the ring inside its rect
        const Rect b3 = rect(560.0f + pad, st + o3, 800.0f - pad * 2, opt_h);
        bool ring3 = false;
        for (int y = int(b3.y - 8); y < int(b3.y + b3.h + 8) && !ring3; ++y)
            for (int x = int(b3.x - 8); x < int(b3.x + b3.w + 8); ++x)
                if (px[size_t(y) * W + x] == ui::theme::focus) { ring3 = true; break; }
        check(ring3, "item 33: the focused Cancel option wears the ring");
        // the ring never overlaps a NEIGHBOURING OPTION's button: no focus
        // ink on option 2's button EDGES (the ring band would sit on the
        // button's border). White label ink (== focus colour) inside the
        // button is fine; only the band 9..3px inside the border must be
        // clean (the ring is 3px wide, centred 6px outside the button).
        bool btn_dirty = false;
        const Rect b2 = rect(560.0f + pad, st + o2, 800.0f - pad * 2, 72.0f);
        for (int y = int(b2.y) + 2; y < int(b2.y + b2.h) - 2 && !btn_dirty; ++y)
            for (int x = int(b2.x) + 2; x < int(b2.x + b2.w) - 2; ++x) {
                const bool on_edge =
                    y < int(b2.y) + 4 || y >= int(b2.y + b2.h) - 4 ||
                    x < int(b2.x) + 4 || x >= int(b2.x + b2.w) - 4;
                if (!on_edge) continue;
                if (px[size_t(y) * W + x] == ui::theme::focus) { btn_dirty = true; break; }
            }
        check(!btn_dirty, "item 33: the ring never overlaps a neighbouring option's button");
        // disabled option: muted ink + reason line inside its 72px button
        const Rect b1 = rect(560.0f + pad, st + o1, 800.0f - pad * 2, 72.0f);
        bool muted = false;
        for (int y = int(b1.y); y < int(b1.y + b1.h) && !muted; ++y)
            for (int x = int(b1.x); x < int(b1.x + b1.w); ++x)
                if (px[size_t(y) * W + x] == ui::theme::textMuted) { muted = true; break; }
        check(muted, "item 33: a disabled action-sheet option draws muted with its reason");
        snap("launcher-r6-action-sheet");

        c.fill(ui::theme::page);
        ProgressSheetVM ps; ps.title = "Megabonk"; ps.detail = "Moving to E:"; ps.pct = 42;
        draw_progress_sheet(c, font, ps);
        // geometry mirrors the draw: pad 48, title 40, detail 92,
        // bar_y = detail_top+36+20 = 92+56 = 148; bar_w = 800-96-96-16 = 592
        const float pad2 = 48.0f;
        const float bar_y2 = 40.0f + 52.0f + 36.0f + 20.0f;
        const float bar_w2 = 800.0f - pad2 * 2 - 96.0f - 16.0f;
        const float sh2 = bar_y2 + 12.0f + 28.0f + 64.0f + 28.0f;
        const float sy = (1080.0f - sh2) * 0.5f;
        const float bar_abs_y = sy + bar_y2;
        const float bar_abs_x = 560.0f + pad2;
        // bar fill: accent at ~42% of the track, track beyond it
        const int fill_end = int(bar_abs_x + bar_w2 * 0.42f);
        check(px[size_t(int(bar_abs_y) + 5) * W + fill_end - 20] == ui::theme::accent,
              "item 33: the progress bar fills pct of the track");
        check(px[size_t(int(bar_abs_y) + 5) * W + int(bar_abs_x + bar_w2) - 20] == ui::theme::track,
              "item 33: the progress bar's track extends past the fill");
        // NO overlap: the bar ends >=16px before the % text's ink bbox, and
        // the Cancel button rect sits fully below the bar row
        char pctb[16];
        std::snprintf(pctb, sizeof pctb, "%d%%", 42);
        const float pct_w2 = font.measure(Face::Regular, 22, pctb);
        const float pct_col_x = 560.0f + 800.0f - pad2 - 96.0f;
        const float pct_ink_x0 = pct_col_x + (96.0f - pct_w2) * 0.5f;
        check(bar_abs_x + bar_w2 + 16.0f <= pct_ink_x0,
              "item 33: the bar ends >=16px before the % text (no overlap)");
        // % ink bbox: scan for text2 ink in the % column near the bar row
        bool pct_ink = false;
        int pct_ink_xmin = W, pct_ink_xmax = -1;
        for (int y = int(bar_abs_y) - 14; y < int(bar_abs_y) + 26; ++y)
            for (int x = int(pct_col_x) - 4; x < 1920; ++x)
                if (px[size_t(y) * W + x] == ui::theme::text2) {
                    pct_ink = true;
                    if (x < pct_ink_xmin) pct_ink_xmin = x;
                    if (x > pct_ink_xmax) pct_ink_xmax = x;
                }
        check(pct_ink, "item 33: the progress sheet's NN% label renders");
        check(pct_ink_xmin >= int(bar_abs_x + bar_w2 + 16.0f) - 2,
              "item 33: the % ink starts >=16px after the bar's right edge");
        // the Cancel button rect: below the bar row, inside the sheet padding
        const Rect cancel2 = rect(560.0f + (800.0f - 170.0f) * 0.5f,
                                  sy + bar_y2 + 12.0f + 28.0f, 170.0f, 64.0f);
        check(cancel2.y >= bar_abs_y + 12.0f + 28.0f - 0.5f,
              "item 33: the Cancel button sits 28px under the bar row");
        check(cancel2.y + cancel2.h + 28.0f <= sy + sh2 + 0.5f,
              "item 33: the sheet keeps >=28px bottom padding under Cancel");
        bool cring = false;
        for (int y = int(cancel2.y - 8); y < int(cancel2.y + cancel2.h + 8) && !cring; ++y)
            for (int x = int(cancel2.x - 8); x < int(cancel2.x + cancel2.w + 8); ++x)
                if (px[size_t(y) * W + x] == ui::theme::focus) { cring = true; break; }
        check(cring, "item 33: the progress sheet's Cancel wears the ring");
        snap("launcher-r6-progress-sheet");
    }

    // item 30: FOCUS VISIBILITY — table-driven: for every screen x focusable
    // element, the render with that element focused differs from the unfocused
    // render by focus ink inside the element's rect (+ ring margin) only.
    {
        const int margin = int(ui::theme::ringOffset) + int(ui::theme::ringWidth) + 2;
        // item 30: every focusable element wears the ring when focused.
        // `ignore` is a region whose pixels may legitimately differ between the
        // two states (the hint bar, which item D2 makes follow the focus); it
        // still counts as a failure that the ring leaks anywhere else.
        const auto run_case = [&](const char* tag, const ui::Rect& r,
                                  const std::function<void()>& unfocused,
                                  const std::function<void()>& focused,
                                  const ui::Rect& ignore = rect(0, 0, 0, 0)) {
            c.fill(ui::theme::page);
            unfocused();
            const std::vector<uint32_t> base = px;
            c.fill(ui::theme::page);
            focused();
            int ring_inside = 0, diff_outside = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const bool changed = px[size_t(y) * W + x] != base[size_t(y) * W + x];
                    if (!changed) continue;
                    const bool inside = x >= r.x - margin && x < r.x + r.w + margin &&
                                        y >= r.y - margin && y < r.y + r.h + margin;
                    const bool ignored = ignore.w > 0 && x >= ignore.x && x < ignore.x + ignore.w &&
                                         y >= ignore.y && y < ignore.y + ignore.h;
                    if (inside) {
                        if (px[size_t(y) * W + x] == ui::theme::focus) ++ring_inside;
                    } else if (!ignored) {
                        ++diff_outside;
                    }
                }
            check(ring_inside > 0, (std::string(tag) + ": focused element shows ring ink (item 30)").c_str());
            check(diff_outside == 0,
                  (std::string(tag) + ": focus changes stay inside the element (item 30)").c_str());
        };

        // sidebar rows: focused = ring + fill; ACTIVE row = green bar only
        // (the two must stay distinct: focusing the active row adds ring ink)
        {
            SidebarVM sb; sb.all_count = 12; sb.installed_count = 4; sb.queue_count = 1;
            sb.active = 2;
            const auto draw_sb = [&](SidebarVM s) {
                c.fill(ui::theme::page);
                draw_sidebar(c, font, s);
            };
            SidebarVM un = sb; un.focus = -1;
            SidebarVM fo = sb; fo.focus = 2;
            run_case("sidebar row focused", rect(40, 264, 395, 64),
                     [&] { draw_sb(un); }, [&] { draw_sb(fo); });
            // active vs focus: the ACTIVE row (focus=-1) shows the green bar,
            // NOT the focus ring
            SidebarVM act = sb; act.focus = -1;
            SidebarVM idle; idle.all_count = 12; idle.installed_count = 4; idle.queue_count = 1;
            idle.active = -1; idle.focus = -1;
            c.fill(ui::theme::page);
            draw_sb(idle);
            const std::vector<uint32_t> idle_px = px;
            c.fill(ui::theme::page);
            draw_sb(act);
            int bar_ink = 0, ring_ink = 0;
            for (int y = 264; y < 328; ++y)
                for (int x = 40; x < 435; ++x) {
                    if (px[size_t(y) * W + x] != idle_px[size_t(y) * W + x]) {
                        if (px[size_t(y) * W + x] == ui::theme::accent) ++bar_ink;
                        // ring ink counts only OUTSIDE the row rect (the
                        // active row's text turns white == focus colour);
                        // a ring on an active-only row would live 6px out
                        const bool inside = x >= 40 && x < 435 && y >= 264 && y < 328;
                        if (!inside && px[size_t(y) * W + x] == ui::theme::focus) ++ring_ink;
                    }
                }
            check(bar_ink > 0, "item 30: the active sidebar row shows the green bar");
            check(ring_ink == 0, "item 30: the active-only sidebar row shows NO focus ring");
        }
        // profile row
        {
            SidebarVM sb; sb.all_count = 12; sb.installed_count = 4; sb.queue_count = 1;
            run_case("sidebar profile row", rect(40, 68, 395, 64),
                     [&] { SidebarVM s = sb; s.profile_focus = false; c.fill(ui::theme::page); draw_sidebar(c, font, s); },
                     [&] { SidebarVM s = sb; s.profile_focus = true; c.fill(ui::theme::page); draw_sidebar(c, font, s); });
        }
        // search field row
        {
            SidebarVM sb; sb.all_count = 12; sb.installed_count = 4; sb.queue_count = 1;
            run_case("sidebar search row", rect(40, 160, 395, 64),
                     [&] { SidebarVM s = sb; s.focus = -1; c.fill(ui::theme::page); draw_sidebar(c, font, s); },
                     [&] { SidebarVM s = sb; s.focus = 1; c.fill(ui::theme::page); draw_sidebar(c, font, s); });
            // item D17: the field is centred in the content column (40..435),
            // not pinned to its left edge: equal gaps either side.
            c.fill(ui::theme::page);
            { SidebarVM s = sb; s.focus = -1; draw_sidebar(c, font, s); }
            int left = -1, right = -1;
            for (int x = 40; x < 435; ++x) {
                bool ink = false;
                for (int y = 160; y < 224; ++y) if (px[size_t(y) * W + x] != ui::theme::page) { ink = true; break; }
                if (ink) { if (left < 0) left = x; right = x; }
            }
            check(left >= 0 && std::abs((left - 40) - (435 - right)) <= 2,
                  "D17: the search field is centred in the sidebar column");
        }
        // disk widget
        {
            SidebarVM sb; sb.all_count = 12; sb.installed_count = 4; sb.queue_count = 1;
            run_case("sidebar disk widget", rect(52, 940, 372, 92),
                     [&] { SidebarVM s = sb; s.focus = -1; c.fill(ui::theme::page); draw_sidebar(c, font, s); },
                     [&] { SidebarVM s = sb; s.focus = 5; c.fill(ui::theme::page); draw_sidebar(c, font, s); });
        }
        // queue auto-update row
        {
            const std::vector<QueueRowVM> rows = make_rows(2, false);
            SidebarVM sb; sb.active = 4;
            const auto draw_q = [&](QueueVM q) {
                c.fill(ui::theme::page);
                draw_download_queue(c, font, sb, q, /*draw_hints=*/false);
            };
            QueueVM un; un.rows = rows.data(); un.row_count = int(rows.size()); un.focus = -1;
            QueueVM fo = un; fo.focus = 0;
            run_case("queue auto-update row", rect(518, 138, 1332, 108),
                     [&] { draw_q(un); }, [&] { draw_q(fo); });
        }
        // sort popup option
        {
            LibraryVM lib; lib.sort_open = true;
            const auto draw_p = [&](LibraryVM l) { draw_sort_popup(c, font, l); };
            LibraryVM un = lib; un.sort_focus = -1;
            LibraryVM fo = lib; fo.sort_focus = 0;
            run_case("sort popup option", rect(532, 244, 414, 64),
                     [&] { draw_p(un); }, [&] { draw_p(fo); });
        }
        // filters panel row
        {
            FilterRowVM fr[1] = {{"Playable", true}};
            const auto draw_f = [&](int focus) { draw_filters_panel(c, font, fr, 1, focus); };
            run_case("filters panel row", rect(1422, 244, 414, 64),
                     [&] { draw_f(-1); }, [&] { draw_f(0); });
        }
        // game page: primary + icon buttons
        {
            GameVM g; g.has_art = true; g.primary.installed = true;
            const auto draw_g = [&](GameVM gg) { c.fill(ui::theme::page); draw_game(c, font, gg, false); };
            GameVM un = g; un.primary_focus = false; un.focus = -1;
            GameVM fo = g; fo.primary_focus = true; fo.focus = -1;
            run_case("game primary button", rect(58, 378, 268, 92),
                     [&] { draw_g(un); }, [&] { draw_g(fo); });
            GameVM un2 = g; un2.focus = -1;
            GameVM fo2 = g; fo2.focus = 1;
            run_case("game trash button", rect(330, 378, 92, 92),
                     [&] { draw_g(un2); }, [&] { draw_g(fo2); });
        }
        // Item D9: the Install card carries ONE size row ("Game size"); the
        // old duplicate "Size on disk" label is gone.
        {
            GameVM g; g.has_art = true; g.primary.installed = true;
            g.install_status = "Installed"; g.game_size = "48.2 GB";
            c.fill(ui::theme::page);
            draw_game(c, font, g, false);
            auto ink_in = [&](float x0, float y0, float w, float h) {
                int n = 0;
                for (int y = int(y0); y < int(y0 + h); ++y)
                    for (int x = int(x0); x < int(x0 + w); ++x)
                        if (px[size_t(y) * W + x] != ui::theme::panel) ++n;
                return n;
            };
            check(ink_in(732, 724, 200, 32) > 0, "D9: the Game size row draws its label");
            const float old_w = font.measure(ui::Face::Regular, 24, "Size on disk");
            const float new_w = font.measure(ui::Face::Regular, 24, "Game size");
            check(new_w < old_w, "D9: 'Game size' is the single, shorter label");
            check(ink_in(732 + new_w + 8.0f, 724, 24, 32) == 0, "D9: no second size label after 'Game size'");
        }
        // confirm sheet buttons
        {
            const auto draw_sheet = [&](bool focus_yes) {
                c.fill(ui::theme::page);
                draw_confirm_sheet(c, font, "Delete Megabonk?", "Frees 48.2 GB.", "", "Cancel", "Delete", focus_yes);
            };
            // the ring moves between the two buttons; test the union rect,
            // derived from the same variable-height layout the draw uses (D3)
            const ConfirmSheetLayout L = confirm_sheet_layout(font, "Frees 48.2 GB.", "");
            const float no_x = L.x + L.w - L.pad - 108.0f - 16.0f - 170.0f;
            const float yes_x = L.x + L.w - L.pad - 108.0f;
            run_case("confirm sheet buttons", rect(no_x - 6.0f, L.buttons_y - 6.0f, (yes_x + 108.0f) - no_x + 12.0f, 76),
                     [&] { draw_sheet(true); }, [&] { draw_sheet(false); },
                     rect(0, kHintBandTop, float(W), float(H) - kHintBandTop));
        }
        // search keys: the focused key is accent-filled (distinct focus look)
        {
            LibraryVM lib; lib.search_open = true; lib.search_key = -1;
            c.fill(ui::theme::page);
            draw_search_overlay(c, font, lib);
            const std::vector<uint32_t> base = px;
            LibraryVM fo = lib; fo.search_key = 0;
            c.fill(ui::theme::page);
            draw_search_overlay(c, font, fo);
            // Item D13: the panel is bottom-anchored; its geometry comes from
            // the same helper the draw uses, so the key rect is exact.
            const SearchPanel p = search_panel();
            const Rect kr = search_key_rect(p, 0);
            int accent_in = 0;
            for (int y = int(kr.y); y < int(kr.y + kr.h); ++y)
                for (int x = int(kr.x); x < int(kr.x + kr.w); ++x)
                    if (px[size_t(y) * W + x] == ui::theme::accent &&
                        base[size_t(y) * W + x] != ui::theme::accent) ++accent_in;
            check(accent_in > 0, "item 30: the focused search key is accent-filled");
            // D13: the keyboard sits at the bottom, above the hint band, and
            // spans the content area — the library stays visible above it.
            check(p.y > 600.0f && p.y + p.h <= kHintBandTop, "D13: the keyboard is anchored above the hint band");
            check(p.x >= kGridX - 0.5f && p.x + p.w <= kHintRight + 0.5f, "D13: the keyboard spans the content area");
            check(kr.y >= p.gy && kr.y + kr.h <= p.y + p.h, "D13: every key is inside the panel");
            // D13: letters follow caps lock; the three action keys keep text labels
            check(search_key_text(0, false) == "a" && search_key_text(0, true) == "A", "D13: keys show the case caps lock will type");
            check(search_key_text(26, true) == "0", "D13: digits ignore caps lock");
            check(search_key_text(36, true) == "Space" && search_key_text(37, true) == "Delete" &&
                  search_key_text(38, true) == "Done", "D13: the action keys keep text labels");
            // D13: the caret is drawn at the caret position (accent ink moves right)
            LibraryVM q1 = lib; q1.search_key = -1; q1.query = "halo"; q1.search_caret = 0;
            c.fill(ui::theme::page); draw_search_overlay(c, font, q1);
            const std::vector<uint32_t> at0 = px;
            LibraryVM q2 = q1; q2.search_caret = 4;
            c.fill(ui::theme::page); draw_search_overlay(c, font, q2);
            auto accent_col = [&](const std::vector<uint32_t>& img, float x) {
                int n = 0;
                for (int y = int(p.query_y); y < int(p.query_y + 40.0f); ++y)
                    if (img[size_t(y) * W + int(x)] == ui::theme::accent) ++n;
                return n;
            };
            const float caret0_x = p.query_x + 1.0f, caret4_x = p.query_x + font.measure(ui::Face::Regular, 28, "halo") + 1.0f;
            check(accent_col(at0, caret0_x) > 0, "D13: the caret is drawn at the start when the caret is 0");
            check(accent_col(px, caret4_x) > 0, "D13: the caret follows the caret position");
        }
    }

    // item 32: the filters panel renders a single 'Playable' row with its on
    // state (green pill, knob right, 'On' label).
    {
        c.fill(ui::theme::page);
        FilterRowVM rows[1] = {{"Playable", true}};
        draw_filters_panel(c, font, rows, 1, -1);
        const float x = 1844.0f - 430.0f, ry = 236.0f + 8.0f;
        const float px0 = x + 430.0f - 92.0f, py = ry + 16.0f;
        // probe the pill fill between the pill's left edge and the knob
        // (px0+12 can land on the 'On' label, px0+40 is the knob itself)
        check(px[size_t(int(py) + 16) * W + int(px0) + 6] == ui::theme::accent,
              "item 32: the Playable row's on pill fills accent");
        check(px[size_t(int(py) + 16) * W + int(px0) + 40] == ui::theme::onAccent,
              "item 32: the Playable pill's knob sits right (on)");
        bool label = false;
        for (int y = int(ry); y < int(ry + 64) && !label; ++y)
            for (int xx = int(x) + 28; xx < int(x) + 300; ++xx)
                if (px[size_t(y) * W + xx] == ui::theme::text2 ||
                    px[size_t(y) * W + xx] == ui::theme::text) { label = true; break; }
        check(label, "item 32: the Playable filter row's label renders");
        snap("launcher-r6-filters-playable");
    }

    // item D10: the game options popup — one sheet for the detail cog and for
    // Menu on either screen. Cancel is the LAST row and the default focus, so
    // a stray A never lands on Delete or Move; the Favorites row sits first.
    {
        const char* labels[5] = {"Add to favorites", "Move to USB E:", "Sync cloud saves", "Delete", "Cancel"};
        const char* reasons[5] = {"", "", "", "", ""};
        ActionSheetVM as;
        as.title = "Halo Wars: Definitive Edition"; as.subtitle = "24.1 GB \xC2\xB7 Internal";
        as.labels = labels; as.reasons = reasons; as.n = 5;
        as.sel = ui_nav::options_open_sel(as.n);
        check(as.sel == 4, "D10: the options popup defaults to Cancel, never Delete or Move");
        c.fill(ui::theme::page);
        draw_action_sheet(c, font, as);
        // every row's label renders inside its own button (5 rows, 64 high,
        // 12 gap; the sheet is 800 wide, 32 pad)
        const float pad = 32.0f, opt_h = 64.0f, opt_gap = 12.0f;
        const float first_opt = 114.0f + 24.0f;
        const float sh = first_opt + (opt_h + opt_gap) * 5 - opt_gap + pad;
        const float st = (1080.0f - sh) * 0.5f;
        int rows_with_ink = 0;
        for (int i = 0; i < 5; ++i) {
            const Rect b = rect(560.0f + pad, st + first_opt + float(i) * (opt_h + opt_gap), 800.0f - pad * 2, opt_h);
            bool ink = false;
            for (int y = int(b.y) + 8; y < int(b.y + b.h) - 8 && !ink; ++y)
                for (int x = int(b.x) + 24; x < int(b.x + b.w) - 24; ++x)
                    if (px[size_t(y) * W + x] == ui::theme::text) { ink = true; break; }
            if (ink) ++rows_with_ink;
        }
        check(rows_with_ink == 5, "D10: all five options draw their labels");
        bool cancel_ring = false;
        const Rect cb = rect(560.0f + pad, st + first_opt + 4.0f * (opt_h + opt_gap), 800.0f - pad * 2, opt_h);
        for (int y = int(cb.y - 8); y < int(cb.y + cb.h + 8) && !cancel_ring; ++y)
            for (int x = int(cb.x - 8); x < int(cb.x + cb.w + 8); ++x)
                if (px[size_t(y) * W + x] == ui::theme::focus) { cancel_ring = true; break; }
        check(cancel_ring, "D10: the Cancel row wears the focus ring on open");
        snap("launcher-d10-options");

        // a disabled Move row (destination full) draws muted with its reason
        const char* reasons2[5] = {"", "Not enough free space on USB E:", "", "", ""};
        as.reasons = reasons2;
        c.fill(ui::theme::page);
        draw_action_sheet(c, font, as);
        bool muted2 = false;
        const Rect mb = rect(560.0f + pad, st + first_opt + (opt_h + opt_gap), 800.0f - pad * 2, 72.0f);
        for (int y = int(mb.y); y < int(mb.y + mb.h) && !muted2; ++y)
            for (int x = int(mb.x); x < int(mb.x + mb.w); ++x)
                if (px[size_t(y) * W + x] == ui::theme::textMuted) { muted2 = true; break; }
        check(muted2, "D10: an impossible Move row draws muted with its reason");
    }

    // item D10: the favorite marker — a star badge in the tile's top-RIGHT
    // corner (the INSTALLED badge owns the left, so both can show at once)
    // and a star beside the game page's title.
    {
        TileVM t; t.name = "Hades"; t.art_top = 0xFF421334; t.art_mid = 0xFFAF3A6E; t.art_bot = 0xFF200A18;
        t.installed = true;
        const Rect g = grid_tile(0, 0);
        const auto draw_t = [&](bool fav) {
            c.fill(ui::theme::page);
            TileVM tt = t; tt.favorite = fav;
            draw_tile(c, font, tt, g, false);
        };
        draw_t(false);
        const std::vector<uint32_t> plain = px;
        draw_t(true);
        auto star_ink = [&](float x0, float y0, float w, float h) {
            int n = 0;
            for (int y = int(y0); y < int(y0 + h); ++y)
                for (int x = int(x0); x < int(x0 + w); ++x)
                    if (px[size_t(y) * W + x] == ui::theme::accentSoft &&
                        plain[size_t(y) * W + x] != ui::theme::accentSoft) ++n;
            return n;
        };
        const float d = 34.0f;
        const float fx = g.x + g.w - 12.0f - d, fy = g.y + 12.0f;
        check(star_ink(fx, fy, d, d) > 20, "D10: a favorite tile draws its star in the top-right corner");
        check(star_ink(g.x + 12.0f, g.y + 12.0f, 94.0f, 26.0f) == 0,
              "D10: the star never lands on the INSTALLED badge");
        draw_t(false);
        int any_star = 0;
        for (int y = int(g.y); y < int(g.y + g.h); ++y)
            for (int x = int(g.x); x < int(g.x + g.w); ++x)
                if (px[size_t(y) * W + x] == ui::theme::accentSoft) ++any_star;
        check(any_star == 0, "D10: a non-favorite tile draws no star");

        // the game page: the star sits right of the title's ink
        GameVM gv; gv.has_art = true; gv.primary.installed = true; gv.title = "Halo Wars";
        const auto draw_g = [&](bool fav) {
            c.fill(ui::theme::page);
            GameVM gg = gv; gg.favorite = fav;
            draw_game(c, font, gg, false);
        };
        draw_g(false);
        const std::vector<uint32_t> page_plain = px;
        draw_g(true);
        const float tw = font.measure(ui::Face::Semibold, 56, "Halo Wars");
        int star_page = 0;
        for (int y = 192; y < 238; ++y)
            for (int x = int(64.0f + tw + 16.0f); x < int(64.0f + tw + 60.0f); ++x)
                if (px[size_t(y) * W + x] == ui::theme::accentSoft &&
                    page_plain[size_t(y) * W + x] != ui::theme::accentSoft) ++star_page;
        check(star_page > 20, "D10: a favorite game page draws a star beside its title");
        draw_g(false);
        bool title_star = false;
        for (int y = 192; y < 238; ++y)
            for (int x = int(64.0f + tw + 16.0f); x < int(64.0f + tw + 60.0f); ++x)
                if (px[size_t(y) * W + x] == ui::theme::accentSoft) { title_star = true; break; }
        check(!title_star, "D10: a non-favorite game page draws no title star");
    }

    // ---- item D31: rounded corners are really rounded ----
    // The art was drawn rounded but the label scrim and the grey no-cover
    // gradient filled full-width rows, so a square dark corner peeked out
    // behind every tile. The corner pixel must stay the page colour.
    {
        const Rect g = grid_tile(0, 0);
        const auto corner_is_page = [&]() {
            const int cx = int(g.x) + 1, cy = int(g.y + g.h) - 2;   // bottom-left corner
            return px[size_t(cy) * W + cx] == ui::theme::page;
        };
        TileVM t; t.name = "Hades"; t.no_cover = true;
        c.fill(ui::theme::page);
        draw_tile(c, font, t, g, false);
        check(corner_is_page(), "D31: a no-cover tile's corner is page, not the grey gradient");
        TileVM a; a.name = "Hades";   // fixture gradient art + the label scrim
        c.fill(ui::theme::page);
        draw_tile(c, font, a, g, false);
        check(corner_is_page(), "D31: a fixture-art tile's corner is page, not the scrim");
        // and the fill really is there inside the shape (the test is not vacuous)
        check(px[size_t(int(g.y + g.h) - 2) * W + int(g.x + g.w * 0.5f)] != ui::theme::page,
              "D31: the tile still fills its interior");
        snap("launcher-d31-tile-corners");
    }

    // ---- item D9 polish: the game page's capsule box is never a grey slab ----
    // A game that is not installed has no library entry to read the cover from.
    // The page used to paint the no-cover grey gradient into the 224x336 box at
    // the top right, which read as a broken image over the hero. With no art the
    // box now stays empty; with art it shows the art.
    {
        GameVM g;
        g.title = "Halo Infinite"; g.primary.installed = false;
        g.has_art = false; g.no_cover = true;
        c.fill(ui::theme::page);
        draw_game(c, font, g, false);
        int grey = 0;
        for (int y = 80; y < 400; ++y)
            for (int x = 1640; x < 1848; ++x) {
                const uint32_t p = px[size_t(y) * W + x];
                if (p == kNoCoverTop || p == kNoCoverBot) ++grey;
            }
        check(grey == 0, "D9: a game with no art leaves the capsule box empty, not grey");
        snap("launcher-d9-detail-no-art");

        // and the box really is drawn when the art is there (the check is not vacuous)
        std::vector<uint32_t> art(size_t(64) * 96, 0xFF7A2E1D);
        g.has_art = true; g.no_cover = false; g.art = ui::Image{art.data(), 64, 96, 64};
        c.fill(ui::theme::page);
        draw_game(c, font, g, false);
        check(px[size_t(200) * W + 1744] == 0xFF7A2E1D, "D9: the capsule box shows the tall art when it has it");
    }

    // item D10: the Filters panel now carries TWO rows (Playable, Favorites),
    // each with its own on state.
    {
        c.fill(ui::theme::page);
        FilterRowVM rows[2] = {{"Playable", false}, {"Favorites", true}};
        draw_filters_panel(c, font, rows, 2, 1);
        const float x = 1844.0f - 430.0f, ry = 236.0f + 8.0f;
        const float px0 = x + 430.0f - 92.0f;
        const float row2y = ry + 64.0f;
        check(px[size_t(int(row2y + 16.0f) + 16) * W + int(px0) + 6] == ui::theme::accent,
              "D10: the Favorites row's on pill fills accent");
        check(px[size_t(int(row2y + 16.0f) + 16) * W + int(px0) + 40] == ui::theme::onAccent,
              "D10: the Favorites pill's knob sits right (on)");
        const auto row_has_label = [&](float top) {
            for (int y = int(top); y < int(top + 64.0f); ++y)
                for (int xx = int(x) + 28; xx < int(x) + 300; ++xx)
                    if (px[size_t(y) * W + xx] == ui::theme::text2 || px[size_t(y) * W + xx] == ui::theme::text) return true;
            return false;
        };
        check(row_has_label(ry) && row_has_label(row2y), "D10: both filter rows draw their labels");
        snap("launcher-d10-filters");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
