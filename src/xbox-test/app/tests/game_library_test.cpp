// SPDX-License-Identifier: GPL-3.0-or-later
// Native regression for the Delete-game and Installed-filter rules.
#include "../favorites.h"
#include "../game_library.h"
#include "../storage_roots.h"
#include "../store_size.h"
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

    // Item D7: the live search filter is a case-insensitive substring match
    // that keeps library order; the header count says how much is showing.
    {
        struct N { uint32_t appid; std::string name; };
        std::vector<std::shared_ptr<N>> lib = {
            std::make_shared<N>(N{1, "Halo Wars: Definitive Edition"}),
            std::make_shared<N>(N{2, "Megabonk"}),
            std::make_shared<N>(N{3, "Halo Infinite"}),
            std::make_shared<N>(N{4, "HALO 3"}),
        };
        assert(search(lib, "").size() == 4);                     // no query: everything
        auto halo = search(lib, "halo");
        assert(halo.size() == 3 && halo[0]->appid == 1 && halo[1]->appid == 3 && halo[2]->appid == 4);
        assert(search(lib, "HALO").size() == 3);                 // the query's case does not matter
        assert(search(lib, "halo i").size() == 1 && search(lib, "halo i")[0]->appid == 3);
        assert(search(lib, "zzz").empty());
        assert(search(lib, " ").size() == 3);                    // every name with a space (Megabonk has none)
        assert(count_label(3, 772, true) == "3 of 772 games");
        assert(count_label(772, 772, false) == "772 games");
        assert(count_label(0, 772, true) == "0 of 772 games");
    }

    // Item D9: the store's "Storage: N GB" requirement becomes the Game size
    // row before a download starts.
    {
        assert(store_size::from_requirements("OS: Windows 10\nProcessor: x64\nStorage: 30 GB available space") == "30 GB");
        assert(store_size::from_requirements("Storage: 512 MB available space") == "512 MB");
        assert(store_size::from_requirements("storage:1.5 gb") == "1.5 GB");
        assert(store_size::from_requirements("Hard Drive: 20 GB") == "");          // no 'storage' word
        assert(store_size::from_requirements("Storage: plenty") == "");            // no figure
        assert(store_size::from_requirements("Storage: 30") == "");                // no unit
        assert(store_size::from_requirements("") == "");
    }

    // install records with a root: parse, validity, path joining, shared-folder identity
    Marker usb = parse_marker("app 5 build 1 files 2 bytes 77 root E:\\XboxNativeGames installdir hades\n");
    assert(usb.root == "E:\\XboxNativeGames" && usb.installdir == "hades" && usb.bytes == 77);
    assert(parse_marker("app 5 build 1 files 2 bytes 77 installdir hades\n").root.empty());   // existing installs: LocalState
    assert(parse_marker("app 5 root E:\\G\n").installdir.empty() && parse_marker("app 5 root E:\\G\n").root == "E:\\G");
    assert(parse_marker("app 5 installdir a root b\n").root.empty());                         // root only counts before installdir
    for (const char* ok : {"E:\\XboxNativeGames", "d:\\Games\\Steam-1.x", "F:\\a_b"}) assert(valid_root(ok));
    for (const char* bad : {"", "E:", "E:\\", "E:\\x\\", "E:\\x\\\\y", "E:\\..\\x", "E:\\.x", "E:\\x.", "E:\\a b", "1:\\x", "\\\\srv\\share", "E:/x", "E:\\a\nb", "E:\\a:b"}) assert(!valid_root(bad));
    assert(join_root("L:\\Local", "", "hades") == "L:\\Local\\hades" && join_root("L:\\Local", "E:\\G", "hades") == "E:\\G\\hades");
    assert(share_key("", "x") == "x" && share_key("E:\\G", "x") == "E:\\G\\x" && share_key("E:\\G", "").empty());
    std::vector<std::pair<uint32_t, std::string>> two = {{1, "x"}, {2, share_key("E:\\G", "x")}, {3, share_key("e:\\g", "x")}};
    assert(!installdir_shared(1, "x", {two[1]}) && !installdir_shared(2, share_key("E:\\G", "x"), {two[0]}));   // same name, different drive: not shared
    assert(installdir_shared(2, share_key("E:\\G", "x"), two) && delete_kind(2, share_key("E:\\G", "x"), {two[0], two[1]}) == DeleteKind::folder);
    assert(delete_kind(3, share_key("e:\\g", "x"), two) == DeleteKind::shared);                               // drive letter case is the same folder

    // install location choices
    using namespace storage_roots;
    std::vector<Choice> ch(2); ch[0].name = "Internal"; ch[0].freeBytes = 5ull << 30;
    ch[1].root = "E:\\XboxNativeGames"; ch[1].name = drive_name(ch[1].root, kRemovable); ch[1].freeBytes = 120ull << 30;
    assert(label(ch[0]) == "Internal (5.0 GB free)" && label(ch[1]) == "USB E: (120.0 GB free)" && size_text(300ull << 20) == "300 MB");
    assert(index_of(ch, "") == 0 && index_of(ch, "E:\\XboxNativeGames") == 1 && index_of(ch, "e:\\xboxnativegames") == 1 && index_of(ch, "F:\\Gone") == 0);   // missing drive: back to internal
    assert(cycle(0, 2, 1) == 1 && cycle(1, 2, 1) == 0 && cycle(0, 1, 1) == 0 && cycle(0, 0, 1) == 0 && cycle(0, 3, -1) == 2);
    assert(!has_room(100ull << 20, 100ull << 20) && has_room(22000ull << 20, 20000ull << 20) && !has_room(4920ull << 20, 20461ull << 20));

    // drive names: only a removable drive is "USB"; a fixed D: on a Dev Mode console is the
    // developer scratch partition, so calling it USB would point at a drive that is not there
    assert(drive_name("E:\\XboxNativeGames", kRemovable) == "USB E:" && drive_name("D:\\XboxNativeGames", kFixed) == "Drive D:");
    assert(drive_name("", kRemovable) == "Drive" && drive_name("e:\\x", kRemovable) == "USB E:");

    // the storage line: the app's own figure, the pool it writes into, and the volume when
    // the volume has more (a quota or a separate partition), which is the console's report
    Space internal; internal.ok = true; internal.available = 4421ull << 20; internal.total = 5120ull << 20; internal.freeTotal = 4421ull << 20;
    assert(space_text(internal) == "4.3 GB for this app of 5.0 GB");
    Space quota = internal; quota.freeTotal = 200ull << 30;
    assert(space_text(quota) == "4.3 GB for this app of 5.0 GB, 200.0 GB volume free");
    Space none; assert(space_text(none) == "free space could not be read");
    Space zero = internal; zero.available = 0;
    assert(space_text(zero) == "0 MB for this app of 5.0 GB, 4.3 GB volume free");   // exhausted pool, volume still has room
    assert(space_line("Internal", "Q:\\Local", internal) == "Internal path=Q:\\Local GetDiskFreeSpaceEx available=4.3 GB total=5.0 GB free_total=4.3 GB");
    assert(space_line("Internal", "Q:\\Local", none) == "Internal path=Q:\\Local GetDiskFreeSpaceEx FAILED");
    Space wr = internal; wr.winrt = true; wr.winrtFree = 1ull << 30; wr.winrtCapacity = 5ull << 30;
    assert(space_line("Internal", "Q:\\Local", wr).find("WinRT System.FreeSpace=1.0 GB System.Capacity=5.0 GB") != std::string::npos);
    assert(space_line("USB E:", "E:\\XboxNativeGames", internal, "type=removable").find("type=removable") != std::string::npos);

    // item D1: the sidebar widget's figures come from the SAME volume figures
    // the Storage screen's drive row shows (freeTotal / total), so the two can
    // never disagree; an unreadable drive leaves the widget's defaults alone.
    {
        int gb10 = -1, pct = -1;
        Space vol; vol.ok = true; vol.total = 100ull << 30; vol.freeTotal = 66ull << 30;
        Choice c; c.name = "Internal"; c.space = vol;
        assert(widget_figures(c, gb10, pct) && gb10 == 660 && pct == 34);   // 66 of 100 GB free = 34% used
        vol.freeTotal = 0; c.space = vol;
        assert(widget_figures(c, gb10, pct) && gb10 == 0 && pct == 100);
        vol.freeTotal = vol.total; c.space = vol;
        assert(widget_figures(c, gb10, pct) && gb10 == 1000 && pct == 0);   // 100 GB = 1000 tenths
        Space bad; Choice none2; none2.space = bad;
        assert(!widget_figures(none2, gb10, pct));
        Space zero_total; zero_total.ok = true; zero_total.total = 0;
        Choice zt; zt.space = zero_total;
        assert(!widget_figures(zt, gb10, pct));   // never divides by zero
    }

    // the size of the pool beside the free figure, so 5 GB of dev storage is not read as 5 GB of disk
    Choice cap; cap.name = "Internal"; cap.freeBytes = 4421ull << 20; cap.space = internal;
    assert(label_capacity(cap) == "Internal (4.3 GB free of 5.0 GB)");
    Choice capped = cap; capped.space = quota;
    assert(label_capacity(capped) == "Internal (4.3 GB free of 5.0 GB)");   // the volume figure is for the refusal line and the log
    Choice nospace; nospace.name = "Internal"; nospace.freeBytes = 1ull << 30;
    assert(label_capacity(nospace) == "Internal (1.0 GB free)");   // no total read: plain label

    // refusal: the figure with the pool it is a share of, then the way out (another drive, else delete a game)
    std::vector<Choice> both = {cap, ch[1]};
    std::string r = refusal_text(cap, both, 0);
    assert(r.find("1.0 GB free") == std::string::npos && r.find("No space on Internal: 4.3 GB of 5.0 GB.") == 0);
    assert(r.find("\nX: install to USB E: (120.0 GB free).") != std::string::npos);
    std::vector<Choice> one = {cap};
    assert(refusal_text(cap, one, 0).find("\nDelete an installed game to free space.") != std::string::npos);
    assert(refusal_text(nospace, one, 0).find("Could not read free space on Internal") == 0);
    Choice usbfull; usbfull.name = "USB E:"; usbfull.freeBytes = 0; usbfull.space = internal; usbfull.space.available = 0;
    assert(refusal_text(usbfull, {cap, usbfull}, 1) == "No space on USB E: 0 MB of 5.0 GB.\nX: install to Internal (4.3 GB free).");
    assert(best_other(both, 0) == 1 && best_other(both, 1) == 0 && best_other(one, 0) == one.size());
    Choice big = ch[1]; big.freeBytes = 300ull << 30;
    std::vector<Choice> pair2 = {cap, big};
    assert(best_other(pair2, 1) == 0);   // internal is the only other one, whatever its size

    // the two lines the detail page draws a note on: an explicit break, else a word wrap
    std::string n1, n2;
    note_lines("No space on Internal: 0 MB of 5.0 GB.\nX: install to USB E: (15.2 GB free).", 42, n1, n2);
    assert(n1 == "No space on Internal: 0 MB of 5.0 GB." && n2 == "X: install to USB E: (15.2 GB free).");
    note_lines("Delete an installed game to free space.", 42, n1, n2);
    assert(n1 == "Delete an installed game to free space." && n2.empty());
    note_lines("A game was started: reopen the app to delete.", 42, n1, n2);
    assert(n1 == "A game was started: reopen the app to" && n2 == "delete.");
    note_lines("Wait for the delete to finish.", 42, n1, n2);
    assert(n1 == "Wait for the delete to finish." && n2.empty());
    note_lines(std::string(60, 'x'), 42, n1, n2);   // one long word: cut at the column, the tail starts line two
    assert(n1.size() == 42 && n2 == std::string(18, 'x'));
    note_lines(std::string(100, 'x'), 42, n1, n2);   // more than two lines: the second ends with U+2026 (item D4)
    assert(n1.size() == 42 && n2.size() == 42 && n2.substr(39) == "\xE2\x80\xA6");
    // a multi-byte character at the cut point is never split (item D4): the
    // 'e-acute' straddles byte 39 of the second line, so the cut backs off to
    // the previous whole glyph instead of emitting half of it
    note_lines(std::string(42, 'x') + std::string(38, 'x') + "\xC3\xA9" + std::string(30, 'y'), 42, n1, n2);
    assert(n2.size() >= 3 && n2.substr(n2.size() - 3) == "\xE2\x80\xA6");
    bool whole = true;
    for (size_t i = 0; i < n2.size();) {   // every sequence is a complete UTF-8 codepoint
        const unsigned char ch = (unsigned char)n2[i];
        const size_t need = ch < 0x80 ? 1 : (ch & 0xE0) == 0xC0 ? 2 : (ch & 0xF0) == 0xE0 ? 3 : 4;
        if (i + need > n2.size()) { whole = false; break; }
        for (size_t k = 1; k < need; ++k)
            if (((unsigned char)n2[i + k] & 0xC0) != 0x80) whole = false;
        i += need;
    }
    assert(whole);
    note_lines("", 42, n1, n2);
    assert(n1.empty() && n2.empty());

    assert(clamp_selection(5, 0) == 0 && clamp_selection(5, 3) == 2 && clamp_selection(-1, 3) == 0 && clamp_selection(1, 3) == 1);

    // item D10: favorites round-trip through LocalState\favorites.txt. The
    // file is the launcher's only record, so a restart must read back exactly
    // what the previous run wrote.
    {
        using namespace favorites;
        assert(parse("").empty());
        assert(parse("10\n20\n") == (std::set<uint32_t>{10, 20}));
        assert(parse("  480  \r\n# a comment\n\n\t3405340\t\n") == (std::set<uint32_t>{480, 3405340}));
        assert(parse("10\n10\n") == (std::set<uint32_t>{10}));                        // duplicates collapse
        assert(parse("10 # trailing note\n") == (std::set<uint32_t>{10}));
        // junk lines are dropped, never turned into a bogus appid
        for (const char* junk : {"abc", "0", "12a", "a12", "99999999999", "-5", "1.5", "#10"}) assert(parse(junk).empty());
        std::set<uint32_t> ids = {3405340, 480, 459220};
        const std::string text = format(ids);
        assert(text == "480\n459220\n3405340\n");                                     // sorted, one id per line
        assert(parse(text) == ids);                                                   // round trip
        assert(parse(format(parse(text))) == ids);                                    // and again, as on a second restart
        assert(format({}).empty() && parse(format({})).empty());
        assert(contains(ids, 480) && !contains(ids, 481));
        assert(toggle(ids, 481) && contains(ids, 481));                               // added
        assert(!toggle(ids, 481) && !contains(ids, 481));                             // removed again
        assert(!toggle(ids, 480) && ids.size() == 2 && !contains(ids, 480));
        std::set<uint32_t> back = parse(format(ids));                                 // the toggle survives a restart
        assert(back == ids && !contains(back, 480) && contains(back, 459220));
    }

    std::puts("game_library: all checks passed");
    return 0;
}
