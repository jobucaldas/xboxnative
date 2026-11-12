// SPDX-License-Identifier: GPL-3.0-or-later
// Install-health regression on a real filesystem (run under Wine by
// install-health.sh): a dl_<appid>.done marker only counts as installed when
// the payload folder it names holds at least one non-empty file — the console
// saw markers claiming GBs beside empty folders (D:-drive deploys).
#include "../installs.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static std::wstring root;
static std::wstring at(const wchar_t* rel) { return root + L"\\" + rel; }
static void mkdir(const wchar_t* rel) { CreateDirectoryW(at(rel).c_str(), nullptr); }
static void put(const wchar_t* rel, const char* text) { FILE* f = _wfopen(at(rel).c_str(), L"wb"); if (f) { std::fputs(text, f); std::fclose(f); } }

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::string a = argv[1]; root.assign(a.begin(), a.end());
    CreateDirectoryW(root.c_str(), nullptr);

    // healthy install: marker + folder with a real file (the marker's byte
    // total is the depot figure; payload_state only asks for real content)
    put(L"dl_100.done", "app 100 build 1 files 4 bytes 1073741824 installdir alpha\n");
    mkdir(L"alpha"); put(L"alpha\\game.exe", "x");
    installs::Info ok = installs::read(root, 100);
    CHECK(ok.installed && ok.dir == "alpha" && ok.bytes == 1073741824ull);
    CHECK(installs::health_of(root, ok) == installs::Health::ok);
    CHECK(installs::payload_state(ok.folder) == installs::Health::ok);

    // the D:-drive failure: a full-size marker beside an EMPTY folder
    put(L"dl_200.done", "app 200 build 1 files 42 bytes 800000000 installdir emptied\n");
    mkdir(L"emptied");
    installs::Info empty = installs::read(root, 200);
    CHECK(empty.installed && empty.bytes == 800000000ull);
    CHECK(installs::payload_state(empty.folder) == installs::Health::empty);
    CHECK(installs::health_of(root, empty) != installs::Health::ok);

    // the folder is gone entirely (deploy wipe / a move that left the marker)
    put(L"dl_300.done", "app 300 build 1 files 42 bytes 900000000 installdir vanished\n");
    installs::Info gone = installs::read(root, 300);
    CHECK(gone.installed && !gone.folder.empty());
    CHECK(installs::payload_state(gone.folder) == installs::Health::missing);
    CHECK(installs::health_of(root, gone) != installs::Health::ok);

    // only subdirectories, no files: still empty
    put(L"dl_400.done", "app 400 build 1 files 0 bytes 1000 installdir dirs\n");
    mkdir(L"dirs"); mkdir(L"dirs\\sub"); mkdir(L"dirs\\sub\\deeper");
    installs::Info dirs = installs::read(root, 400);
    CHECK(installs::health_of(root, dirs) == installs::Health::empty);

    // files that live only under subfolders are a payload (a game layout keeps
    // its files under data dirs): the walk recurses, bounded in depth
    put(L"dl_450.done", "app 450 build 1 files 3 bytes 5000 installdir nested\n");
    mkdir(L"nested"); mkdir(L"nested\\data"); mkdir(L"nested\\data\\bin"); mkdir(L"nested\\data\\bin\\deeper");
    put(L"nested\\data\\bin\\deeper\\core.pack", "payload");
    CHECK(installs::payload_state(installs::read(root, 450).folder) == installs::Health::ok);
    CHECK(installs::health_of(root, installs::read(root, 450)) == installs::Health::ok);

    // a reparse point (junction/symlink) is never followed: a folder whose only
    // content links elsewhere does not count as a payload (needs the symlink
    // privilege; skipped when Wine cannot grant it)
    put(L"dl_460.done", "app 460 build 1 files 1 bytes 5 installdir linked\n");
    mkdir(L"linked"); mkdir(L"elsewhere"); put(L"elsewhere\\far.dat", "x");
    if (CreateSymbolicLinkW(at(L"linked\\jump").c_str(), at(L"elsewhere").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY))
        CHECK(installs::payload_state(installs::read(root, 460).folder) == installs::Health::empty);
    else
        std::printf("note: symlink case skipped (no symlink privilege)\n");

    // files that live only under subfolders are a payload (a game folder with
    // a data dir): the walk recurses, bounded in depth
    put(L"dl_450.done", "app 450 build 1 files 3 bytes 5000 installdir nested\n");
    mkdir(L"nested"); mkdir(L"nested\\data"); mkdir(L"nested\\data\\bin"); mkdir(L"nested\\data\\bin\\deeper");
    put(L"nested\\data\\bin\\deeper\\core.pack", "payload");
    CHECK(installs::health_of(root, installs::read(root, 450)) == installs::Health::ok);
    CHECK(installs::payload_state(installs::read(root, 450).folder) == installs::Health::ok);

    // a reparse point (junction/symlink) is never followed: a folder whose
    // only content links elsewhere does not count as a payload
    put(L"dl_460.done", "app 460 build 1 files 1 bytes 5 installdir linked\n");
    mkdir(L"linked"); mkdir(L"elsewhere"); put(L"elsewhere\\far.dat", "x");
    if (CreateSymbolicLinkW(at(L"linked\\jump").c_str(), at(L"elsewhere").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY))
        CHECK(installs::payload_state(installs::read(root, 460).folder) == installs::Health::empty);
    else
        std::printf("note: symlink case skipped (no symlink privilege)\n");

    // files that live only under subfolders (a game folder with a data dir)
    // are a payload: the check walks the tree (bounded depth)
    put(L"dl_450.done", "app 450 build 1 files 3 bytes 5000 installdir nested\n");
    mkdir(L"nested"); mkdir(L"nested\\data"); mkdir(L"nested\\data\\bin");
    put(L"nested\\data\\bin\\core.pack", "payload");
    CHECK(installs::health_of(root, installs::read(root, 450)) == installs::Health::ok);

    // a junction/symlink (reparse point) is never followed, so a folder whose
    // only content is a link to elsewhere does not count as a payload
    put(L"dl_460.done", "app 460 build 1 files 1 bytes 5 installdir linked\n");
    mkdir(L"linked"); mkdir(L"elsewhere"); put(L"elsewhere\\far.dat", "x");
    {   // CreateSymbolicW needs a privilege; a junction needs a handle. When
        // neither is available the case cannot be built and is skipped.
        if (CreateSymbolicLinkW(at(L"linked\\jump").c_str(), at(L"elsewhere").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY))
            CHECK(installs::payload_state(installs::read(root, 460).folder) == installs::Health::empty);
    }

    // a zero-byte file does not count as a payload; one real file beside it does
    put(L"dl_500.done", "app 500 build 1 files 2 bytes 10 installdir stub\n");
    mkdir(L"stub"); put(L"stub\\placeholder", "");
    CHECK(installs::payload_state(installs::read(root, 500).folder) == installs::Health::empty);
    put(L"stub\\real.dat", "content");
    CHECK(installs::payload_state(installs::read(root, 500).folder) == installs::Health::ok);

    // no marker at all: never installed, never healthy
    CHECK(!installs::read(root, 600).installed);
    CHECK(installs::health_of(root, installs::read(root, 600)) == installs::Health::missing);

    // a marker whose folder path is a FILE (not a directory) reads as missing
    put(L"dl_700.done", "app 700 build 1 files 1 bytes 5 installdir fileclash\n");
    put(L"fileclash", "not a dir");
    CHECK(installs::payload_state(installs::read(root, 700).folder) == installs::Health::missing);

    // clear_marker removes the stale marker so a reinstall starts fresh: the
    // read reports not-installed (download_app then honors the chosen root),
    // and a second call still succeeds (nothing left to remove)
    put(L"dl_800.done", "app 800 build 1 files 42 bytes 900000000 installdir stale\n");
    CHECK(installs::read(root, 800).installed);
    CHECK(installs::clear_marker(root, 800));
    CHECK(!installs::read(root, 800).installed);
    CHECK(installs::clear_marker(root, 800));

    // clear_marker removes the stale marker so a reinstall starts fresh: the
    // read reports not-installed (download_app then honors the chosen root)
    // and a second call still reports success (nothing left to remove)
    put(L"dl_800.done", "app 800 build 1 files 42 bytes 900000000 installdir stale\n");
    CHECK(installs::read(root, 800).installed);
    CHECK(installs::clear_marker(root, 800));
    CHECK(!installs::read(root, 800).installed);
    CHECK(installs::clear_marker(root, 800));
    CHECK(installs::payload_state(root + L"\\dl_800.done") == installs::Health::missing);

    std::printf("%s (%d failure%s)\n", failures ? "INSTALL-HEALTH FAILED" : "INSTALL-HEALTH OK",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
