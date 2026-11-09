// SPDX-License-Identifier: GPL-3.0-or-later
// Delete-game regression on a real filesystem (run under Wine by game-delete.sh):
// only the chosen install folder and its marker go; neighbours, shared folders,
// launcher folders and link targets survive.
#include "../installs.h"
#include <cstdio>
#include <cstdlib>
#include <set>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static std::wstring root;
static std::wstring at(const wchar_t* rel) { return root + L"\\" + rel; }
static bool exists(const wchar_t* rel) { return GetFileAttributesW(at(rel).c_str()) != INVALID_FILE_ATTRIBUTES; }
static void mkdir(const wchar_t* rel) { CreateDirectoryW(at(rel).c_str(), nullptr); }
static void put(const wchar_t* rel, const char* text) { FILE* f = _wfopen(at(rel).c_str(), L"wb"); if (f) { std::fputs(text, f); std::fclose(f); } }

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string a = argv[1]; root.assign(a.begin(), a.end());
    CreateDirectoryW(root.c_str(), nullptr);

    put(L"dl_100.done", "app 100 build 1 files 4 bytes 40 installdir alpha\n");
    mkdir(L"alpha"); mkdir(L"alpha\\data"); mkdir(L"alpha\\data\\deep");
    put(L"alpha\\game.exe", "x"); put(L"alpha\\data\\a.bin", "x"); put(L"alpha\\data\\deep\\b.bin", "x"); put(L"alpha\\ro.txt", "x");
    SetFileAttributesW(at(L"alpha\\ro.txt").c_str(), FILE_ATTRIBUTE_READONLY);
    mkdir(L"alphabet"); put(L"alphabet\\keep.txt", "x");                       // neighbour with a common prefix
    mkdir(L"outside"); put(L"outside\\target.txt", "x");                       // reachable only through a link
    bool linked = CreateSymbolicLinkW(at(L"alpha\\link").c_str(), at(L"outside").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY) != 0;
    put(L"dl_200.done", "app 200 build 1 files 1 bytes 1 installdir shared\n");
    put(L"dl_201.done", "app 201 build 1 files 1 bytes 1 installdir shared\n");
    mkdir(L"shared"); put(L"shared\\s.bin", "x");
    put(L"dl_300.done", "app 300 build 1 files 1 bytes 1 installdir steam\n");   // launcher-owned folder
    mkdir(L"steam"); mkdir(L"steam\\300"); put(L"steam\\300\\save.dat", "x");
    put(L"dl_100.done.bak", "not a marker");

    auto all = installs::scan(root, true);
    std::set<uint32_t> ids; for (auto& i : all) ids.insert(i.first);
    CHECK((ids == std::set<uint32_t>{100, 200, 201, 300}));
    CHECK(installs::read(root, 100).installed && installs::read(root, 100).dir == "alpha" && installs::read(root, 100).bytes == 40);
    CHECK(game_library::installdir_shared(200, "shared", all) && !game_library::installdir_shared(100, "alpha", all));

    installs::Progress p;
    CHECK(installs::remove_install(root, 100, "alpha", false, p));
    CHECK(!exists(L"dl_100.done") && !exists(L"alpha"));
    CHECK(p.removed == 4 && p.failed == 0);
    CHECK(exists(L"alphabet\\keep.txt") && exists(L"outside\\target.txt") && exists(L"dl_100.done.bak"));

    installs::Progress ps;
    CHECK(installs::remove_install(root, 200, "shared", true, ps));
    CHECK(!exists(L"dl_200.done") && exists(L"shared\\s.bin") && exists(L"dl_201.done"));

    installs::Progress pr;
    CHECK(!installs::remove_install(root, 300, "steam", false, pr));
    CHECK(exists(L"dl_300.done") && exists(L"steam\\300\\save.dat"));

    installs::Progress pd;   // "steam." is the same folder as steam\ to Win32: refused, nothing touched
    put(L"dl_301.done", "app 301 build 1 files 1 bytes 1 installdir steam.\n");
    CHECK(!installs::remove_install(root, 301, "steam.", false, pd));
    CHECK(exists(L"dl_301.done") && exists(L"steam\\300\\save.dat"));

    installs::Progress pu;   // pre-0.5.0.0 marker without installdir: marker only, files kept
    put(L"dl_500.done", "app 500 build 1 files 1 bytes 1\n"); mkdir(L"app500"); put(L"app500\\f.bin", "x");
    CHECK(installs::read(root, 500).dir.empty());
    CHECK(installs::remove_install(root, 500, "", true, pu) && !exists(L"dl_500.done") && exists(L"app500\\f.bin"));

    installs::Progress pm;   // folder already gone: marker still removed, nothing else touched
    put(L"dl_400.done", "app 400 build 1 files 1 bytes 1 installdir gone\n");
    CHECK(installs::remove_install(root, 400, "gone", false, pm) && !exists(L"dl_400.done") && pm.failed == 0);

    // unfinished downloads (dl_<appid>.partial): a fresh install's folder goes, everything else stays
    installs::Progress pp;
    CHECK(installs::begin_partial(root, 600, "fresh") && installs::read_partial(root, 600).installdir == "fresh");
    mkdir(L"fresh"); mkdir(L"fresh\\sub"); put(L"fresh\\big.bin", "x"); put(L"fresh\\sub\\c.bin", "x");
    CHECK(installs::discard_partial(root, 600, "fresh", pp) == installs::Discard::removed);
    CHECK(!exists(L"fresh") && !exists(L"dl_600.partial") && pp.removed == 2 && pp.failed == 0);

    installs::Progress pk;   // failed update of a finished install: files and .done kept
    put(L"dl_700.done", "app 700 build 1 files 1 bytes 1 installdir upd\n"); mkdir(L"upd"); put(L"upd\\g.exe", "x");
    CHECK(installs::begin_partial(root, 700, "upd"));
    CHECK(installs::discard_partial(root, 700, "upd", pk) == installs::Discard::kept_installed);
    CHECK(exists(L"upd\\g.exe") && exists(L"dl_700.done") && !exists(L"dl_700.partial"));

    installs::Progress pq;   // folder shared with an installed app, or launcher-owned: kept
    CHECK(installs::begin_partial(root, 202, "shared") && installs::discard_partial(root, 202, "shared", pq) == installs::Discard::kept_shared);
    CHECK(exists(L"shared\\s.bin"));
    CHECK(installs::begin_partial(root, 302, "steam") && installs::discard_partial(root, 302, "steam", pq) == installs::Discard::kept_unsafe);
    CHECK(exists(L"steam\\300\\save.dat"));

    // start-up sweep of runs cut off mid-download; a dl_<appid>.partial.bak is not a marker
    installs::begin_partial(root, 800, "crashed"); mkdir(L"crashed"); put(L"crashed\\a.bin", "x");
    put(L"dl_800.partial.bak", "app 800 installdir alphabet\n");
    auto swept = installs::sweep_partials(root);
    CHECK(swept.size() == 1 && swept[0].appid == 800 && swept[0].dir == "crashed" && swept[0].result == installs::Discard::removed);
    CHECK(!exists(L"crashed") && !exists(L"dl_800.partial") && exists(L"alphabet\\keep.txt") && exists(L"dl_800.partial.bak"));
    CHECK(installs::sweep_partials(root).empty());

    // install on another drive: the marker stays in root and names the game's root
    {
        const std::wstring ext = L"C:\\xbn-usb-test"; const std::string extn = "C:\\xbn-usb-test";
        installs::Progress rm; installs::remove_tree(ext, rm);
        CreateDirectoryW(ext.c_str(), nullptr); CreateDirectoryW((ext + L"\\usbgame").c_str(), nullptr); CreateDirectoryW((ext + L"\\alpha").c_str(), nullptr);
        { FILE* f = _wfopen((ext + L"\\usbgame\\g.bin").c_str(), L"wb"); if (f) { std::fputs("x", f); std::fclose(f); } }
        { FILE* f = _wfopen((ext + L"\\alpha\\keep.bin").c_str(), L"wb"); if (f) { std::fputs("x", f); std::fclose(f); } }
        put(L"dl_900.done", "app 900 build 1 files 1 bytes 1 root C:\\xbn-usb-test installdir usbgame\n");
        put(L"dl_901.done", "app 901 build 1 files 1 bytes 1 root C:\\xbn-usb-test installdir alpha\n");   // same name as an internal folder, other drive
        mkdir(L"alpha"); put(L"alpha\\own.bin", "x"); put(L"dl_902.done", "app 902 build 1 files 1 bytes 1 installdir alpha\n");
        auto i = installs::read(root, 900);
        CHECK(i.installed && i.root == extn && i.folder == ext + L"\\usbgame");
        CHECK(installs::read(root, 902).folder == root + L"\\alpha" && installs::read(root, 902).root.empty());
        CHECK(!game_library::valid_root("..\\x") && installs::read(root, 100).root.empty());
        bool sawExt = false; for (auto& e : installs::scan(root, true)) if (e.first == 900 && e.second == "C:\\xbn-usb-test\\usbgame") sawExt = true;
        CHECK(sawExt);
        installs::Progress pu; CHECK(installs::remove_install(root, 900, "usbgame", false, pu, nullptr, extn));
        CHECK(!exists(L"dl_900.done") && GetFileAttributesW((ext + L"\\usbgame").c_str()) == INVALID_FILE_ATTRIBUTES);
        CHECK(exists(L"alpha\\own.bin") && GetFileAttributesW((ext + L"\\alpha\\keep.bin").c_str()) != INVALID_FILE_ATTRIBUTES);   // same-named folders on the other root untouched
        // unfinished download on the extra root: removed there, record names the root
        CHECK(installs::begin_partial(root, 910, "half", extn)); CreateDirectoryW((ext + L"\\half").c_str(), nullptr);
        { FILE* f = _wfopen((ext + L"\\half\\p.bin").c_str(), L"wb"); if (f) { std::fputs("x", f); std::fclose(f); } }
        auto m = installs::read_partial(root, 910); CHECK(m.installdir == "half" && m.root == extn);
        auto sw = installs::sweep_partials(root);
        CHECK(sw.size() == 1 && sw[0].root == extn && sw[0].result == installs::Discard::removed && GetFileAttributesW((ext + L"\\half").c_str()) == INVALID_FILE_ATTRIBUTES);
        // choices: internal first, then the extra drive folders the sandbox allows
        auto roots = installs::list_roots(root); CHECK(!roots.empty() && roots[0].root.empty());
        installs::Progress pc; installs::remove_tree(ext, pc); DeleteFileW(at(L"dl_901.done").c_str()); DeleteFileW(at(L"dl_902.done").c_str());
    }

    if (failures) { std::printf("GAME-DELETE FAIL: %d check(s)\n", failures); return 1; }
    std::printf("GAME-DELETE PASS: folder+marker removed, neighbours/shared/launcher folders kept, unfinished downloads removed%s\n",
                linked ? ", directory link removed without following it" : " (directory link not creatable here: link case not exercised)");
    return 0;
}
