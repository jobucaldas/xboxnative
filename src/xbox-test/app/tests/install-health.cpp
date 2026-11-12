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

    std::printf("%s (%d failure%s)\n", failures ? "INSTALL-HEALTH FAILED" : "INSTALL-HEALTH OK",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
