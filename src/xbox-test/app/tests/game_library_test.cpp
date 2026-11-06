// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the Delete-game and Installed-filter rules.
#include "../game_library.h"
#include <cassert>
#include <cstdio>
#include <memory>
using namespace game_library;

struct G { uint32_t appid; };

int main() {
    // marker written by the downloader
    Marker m = parse_marker("app 3405340 build 123 files 42 bytes 1073741824 installdir megabonk\r\n");
    assert(m.bytes == 1073741824ull && m.installdir == "megabonk");
    Marker legacy = parse_marker("app 480 build 1 files 3 bytes 99\n");   // pre-0.5.0.0: no installdir
    assert(legacy.bytes == 99 && legacy.installdir.empty());

    uint32_t id = 0;
    assert(marker_appid("dl_3405340.done", id) && id == 3405340);
    assert(!marker_appid("dl_.done", id) && !marker_appid("dl_12a.done", id) && !marker_appid("dl_1.done.bak", id));
    assert(marker_appid("dl_433340.partial", id, ".partial") && id == 433340);
    assert(!marker_appid("dl_433340.partial", id) && !marker_appid("dl_1.partial.bak", id, ".partial"));
    assert(!marker_appid("dl_0.done", id) && !marker_appid("dl_99999999999.done", id) && !marker_appid("x_1.done", id));

    // only a single downloader-shaped folder under LocalState may be removed
    for (const char* ok : {"megabonk", "app480", "steamworks_sdk_redist", "game-1.2"}) assert(deletable_installdir(ok));
    for (const char* bad : {"", ".", "..", "a..b", "steam", "steam.", ".steam", "Megabonk", "a\\b", "a/b", "c:", " x", "a b", "x.",
                            "con", "nul.txt", "aux", "com1", "lpt9.dat", "conin$"}) assert(!deletable_installdir(bad));
    assert(deletable_installdir("console") && deletable_installdir("com10") && deletable_installdir("nullpointer"));
    assert(!deletable_installdir(std::string(129, 'a')));

    // another installed app on the same folder keeps the files
    std::vector<std::pair<uint32_t, std::string>> installs = {{1, "shared"}, {2, "shared"}, {3, "alone"}};
    assert(installdir_shared(1, "shared", installs));
    assert(!installdir_shared(3, "alone", installs));
    assert(installdir_shared(3, "shared.", installs));                     // Win32 drops the trailing dot
    std::vector<std::pair<uint32_t, std::string>> old = {{7, ""}, {8, ""}, {9, "x"}};
    assert(!installdir_shared(7, "", old));                               // empty folder names nothing
    assert(delete_kind(7, "", old) == DeleteKind::unknown_folder);
    assert(delete_kind(9, "x", old) == DeleteKind::folder);
    assert(delete_kind(1, "shared", installs) == DeleteKind::shared);

    // Installed filter keeps library order and is a no-op when off
    std::vector<std::shared_ptr<G>> all = {std::make_shared<G>(G{10}), std::make_shared<G>(G{20}), std::make_shared<G>(G{30})};
    std::set<uint32_t> installed = {30, 10, 99};
    assert(shown(all, false, installed).size() == 3);
    auto only = shown(all, true, installed);
    assert(only.size() == 2 && only[0]->appid == 10 && only[1]->appid == 30);
    assert(shown(all, true, {}).empty());

    assert(clamp_selection(5, 0) == 0 && clamp_selection(5, 3) == 2 && clamp_selection(-1, 3) == 0 && clamp_selection(1, 3) == 1);
    std::puts("game_library: all checks passed");
    return 0;
}
