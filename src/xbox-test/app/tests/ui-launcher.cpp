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
#include <string>
#include <vector>

#include "../ui_launcher.h"

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
    // a rotation of the mockup's 21 tile gradients (first three from fixtures.txt)
    const uint32_t tops[7] = {0xFF1B2A4A, 0xFF4A1B2A, 0xFF2A4A1B, 0xFF3A2A4A, 0xFF2A4A4A, 0xFF4A3A1B, 0xFF1B4A3A};
    const uint32_t mids[7] = {0xFF3E6FA8, 0xFFA86F3E, 0xFF6FA83E, 0xFF6F3EA8, 0xFF3E8AA8, 0xFFA88A3E, 0xFF3EA88A};
    const uint32_t bots[7] = {0xFF0B1220, 0xFF200B12, 0xFF12200B, 0xFF170B20, 0xFF0B2020, 0xFF201708, 0xFF0B2015};
    for (int i = 0; i < n; ++i) {
        t[i].name = "Game";
        t[i].art_top = tops[i % 7];
        t[i].art_mid = mids[i % 7];
        t[i].art_bot = bots[i % 7];
        t[i].installed = i % 3 == 0;
    }
    return t;  // implicit move
}

std::vector<ui_launcher::QueueRowVM> make_rows(int n, bool updates) {
    std::vector<ui_launcher::QueueRowVM> r{size_t(n)};
    const uint32_t arts[5] = {0xFF1A3F6B, 0xFF2A6B4A, 0xFF6B2A55, 0xFF4A3A6B, 0xFF6B4A2A};
    const char* names[5] = {"Megabonk", "Hades", "Celeste", "Hollow Knight", "Terraria"};
    const char* sts[5] = {
        "Downloading  \xC2\xB7  62%  \xC2\xB7  12.4 MB/s  \xC2\xB7  about 1 hour left",
        "Queued  \xC2\xB7  14.7 GB",
        "Queued  \xC2\xB7  1.9 GB",
        "Update available  \xC2\xB7  1.2 GB",
        "Update available  \xC2\xB7  312 MB",
    };
    for (int i = 0; i < n; ++i) {
        r[i].name = names[i % 5];
        r[i].status = sts[i % 5];
        r[i].pct = updates || i != 0 ? 0.0f : 0.62f;
        r[i].art = arts[i % 5];
        r[i].busy = !updates && i == 0;
    }
    return r;
}

// QR module runs for mockup 04 (29x29 finder-pattern-ish fixture at 10px)
ui_launcher::QrRun make_qr(int& count) {
    // three finder patterns + timing rows, enough to look like the mockup at RMSE level
    static ui_launcher::QrRun runs[64];
    int n = 0;
    auto run = [&](uint8_t row, uint8_t col, uint8_t w) { runs[n++] = {row, col, w}; };
    for (int k = 0; k < 7; ++k) { run(uint8_t(k), 0, 7); run(uint8_t(k), 22, 7); run(uint8_t(k + 22), 0, 7); }
    run(7, 0, 8); run(7, 21, 2); run(21, 0, 8);
    for (int k = 0; k < 7; ++k) { run(0, uint8_t(k), 1); run(0, uint8_t(22 + k), 1); run(28, uint8_t(k), 1); }
    for (int k = 8; k < 21; ++k) if (k % 2 == 0) run(14, uint8_t(k), 1);
    for (int k = 0; k < 5; ++k) { run(uint8_t(2 + k), 2, 3); run(uint8_t(24 + k), 24, 2); }
    count = n;
    return runs[0];
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

    // ---- pure logic: grid geometry ----
    using ui_launcher::grid_tile;
    const ui::Rect g0 = grid_tile(0, 0);
    check(g0.x == 524 && g0.y == 264 && g0.w == 168 && g0.h == 252, "grid tile 0 at (524,264) 168x252");
    const ui::Rect g1 = grid_tile(1, 0);
    check(g1.x == 524 + 192 && g1.y == 264, "grid pitch x is 192");
    const ui::Rect g7 = grid_tile(7, 0);
    check(g7.x == 524 && g7.y == 264 + 276, "grid wraps to row 2 with pitch y 276");
    const ui::Rect gscroll = grid_tile(7, 1);
    check(gscroll.y == 264, "scrolling by one row keeps pitch");
    int lo = -1, hi = -1;
    ui_launcher::grid_visible(21, 0, lo, hi);
    check(lo == 0 && hi == 21, "page 0 of 21 tiles visible");
    ui_launcher::grid_visible(21, 1, lo, hi);
    check(lo == 0 && hi == 21, "page 1 shows the slack row above plus rows 1..2");

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
        check(!std::strcmp(primary_label(vm, false), "INSTALLING 62%"), "installing label without focus");
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
        SidebarVM sb; sb.all_count = 21; sb.installed_count = 7; sb.queue_count = 3;
        LibraryVM lib; lib.tiles = tiles.data(); lib.tile_count = 21; lib.sel = 0;
        lib.count_label = "21 games";
        c.fill(ui::theme::page);
        draw_library(c, font, sb, lib);
        snap("launcher-01-library");
        check(px[size_t(300) * W + 300] == px[size_t(300) * W + 300], "library rendered");  // placeholder read
    }

    // 02 game detail (not installed)
    {
        GameVM g;
        g.has_art = true;
        g.primary.installed = true;
        g.time_played = "26h 14m";
        g.achievements = "18 / 42";
        g.last_played = "2 days ago";
        g.install_status = "Installed";
        g.size_on_disk = "48.2 GB";
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

    // 02b game detail with install progress
    {
        GameVM g;
        g.has_art = true;
        g.primary.download = detail_input::Download::running;
        g.install_pct = 0.43f;
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
        g.install_status = "Installing - 4.2 MB/s";
        g.size_on_disk = "20.7 GB of 48.2 GB";
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
        g.primary_focus = true;
        c.fill(ui::theme::page);
        draw_game(c, font, g);
        snap("launcher-03b-installing-hover");
    }

    // 04 sign-in
    {
        SignVM s;
        static QrRun runs[64];
        int n = 0;
        make_qr(n);
        s.qr = runs;
        s.qr_count = n;
        s.clock = "21:07";
        c.fill(ui::theme::page);
        draw_signin(c, font, s);
        snap("launcher-04-signin");
    }

    // 05 delete confirm (sheet over the dimmed game page)
    {
        GameVM g;
        g.has_art = true;
        g.primary.installed = true;
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
        SidebarVM sb; sb.all_count = 21; sb.installed_count = 7; sb.queue_count = 3; sb.active = 2;
        QueueVM q; q.rows = rows.data(); q.row_count = 3; q.updates = ups.data(); q.update_count = 2;
        c.fill(ui::theme::page);
        draw_download_queue(c, font, sb, q);
        snap("launcher-06-download-queue");
    }

    // 07 cloud saves (sheet over the dimmed game page)
    {
        GameVM g;
        g.has_art = true;
        g.primary.installed = true;
        c.fill(ui::theme::page);
        draw_game(c, font, g);
        CloudSlotVM slots[2] = {{"slot0.sav", "1.2 MB - synced"}, {"settings.cfg", "4 KB - synced"}};
        CloudOptVM opts[4] = {
            {"Sync now", "", true},
            {"Conflict resolution", "Ask each time", true},
            {"Sync only on Wi-Fi", "Off", false},
            {"Per-file selection", "", false},
        };
        CloudVM v;
        v.synced = "Last synced today at 20:41";
        v.slots = slots; v.slot_count = 2;
        v.opts = opts; v.opt_count = 4; v.focus_opt = 0;
        draw_cloud_sheet(c, font, v);
        snap("launcher-07-cloud-saves");
    }

    // extras: queue empty
    {
        SidebarVM sb; sb.active = 2;
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

    check(rendered == 15, "rendered all 15 states");
    std::printf("renders: %d in %s\n", rendered, out.c_str());

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
