// SPDX-License-Identifier: GPL-3.0-or-later
// Free-space and launcher-leftover regression on a real filesystem (run under Wine
// by storage-report.sh): the three GetDiskFreeSpaceExW figures are read, the install
// locations are probed with a name and a type, and only the launcher's own leftovers
// are counted and removed.
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

    // every figure the API returns, for the path an install is checked against
    storage_roots::Space sp = installs::space_of(root);
    CHECK(sp.ok && sp.total > 0 && sp.freeTotal > 0);
    CHECK(sp.available <= sp.total && sp.available <= sp.freeTotal);
    CHECK(installs::free_bytes(root) == sp.available);
    storage_roots::Space missing = installs::space_of(L"Q:\\no-such-drive-here");
    CHECK(!missing.ok && missing.available == 0 && missing.freeTotal == 0);
    CHECK(installs::free_bytes(L"Q:\\no-such-drive-here") == 0);
    CHECK(!installs::volume_text(root).empty() && !installs::error_text(5).empty() && installs::error_text(5).find("5") != std::string::npos);

    // install locations: internal first, then the drives the sandbox lets us create a folder on
    std::vector<std::string> note;
    auto roots = installs::list_roots(root, &note);
    CHECK(!roots.empty());
    CHECK(roots[0].root.empty() && roots[0].name == "Internal" && roots[0].type == storage_roots::kFixed);
    CHECK(roots[0].space.ok && roots[0].freeBytes == roots[0].space.available);
    for (size_t i = 1; i < roots.size(); i++) {
        CHECK(roots[i].root.size() == 18 && roots[i].root[1] == ':' && roots[i].root.compare(3, 15, "XboxNativeGames") == 0);
        CHECK(roots[i].name == storage_roots::drive_name(roots[i].root, roots[i].type));
        CHECK(roots[i].type == storage_roots::kRemovable || roots[i].type == storage_roots::kFixed);
        CHECK(GetFileAttributesW(std::wstring(roots[i].root.begin(), roots[i].root.end()).c_str()) != INVALID_FILE_ATTRIBUTES);
    }
    for (auto& n : note) CHECK(!n.empty());   // a skipped drive always says why

    // leftovers: the launcher's own files, and only those
    put(L"boot.prev.log", "old boot");
    put(L"launch_probe.previous.log", "old probe");
    put(L"frametimes.csv", "t,f");
    put(L"appinfo_123.vdf", "installed game: Play reads it");
    put(L"appinfo_456.vdf", "uninstalled game: leftover");
    put(L"appinfo_78a.vdf", "not an appid");
    put(L"appinfox.vdf", "not an appinfo cache");
    put(L"dl_123.done", "app 123 build 1 files 1 bytes 1 installdir alpha");
    put(L"dl_456.partial", "app 456 installdir beta");
    put(L"steam_session.bin", "never touched");
    mkdir(L"storecache");
    put(L"storecache\\100.json", "{}");
    put(L"storecache\\200.json", "{}");
    put(L"storecache\\notes.txt", "not a cache");
    // a fresh store cache is kept (the detail page reads it offline); only a stale one is a leftover
    const std::wstring old = at(L"storecache\\300.json");
    put(L"storecache\\300.json", "{}");
    HANDLE oh = CreateFileW(old.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(oh != INVALID_HANDLE_VALUE);
    if (oh != INVALID_HANDLE_VALUE) {
        FILETIME nowft; GetSystemTimeAsFileTime(&nowft);
        ULARGE_INTEGER u; u.LowPart = nowft.dwLowDateTime; u.HighPart = nowft.dwHighDateTime;
        u.QuadPart -= 30ull * 24 * 3600 * 10000000ull;   // 30 days of 100 ns units
        FILETIME ft; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
        CHECK(SetFileTime(oh, nullptr, nullptr, &ft) != 0);
        CloseHandle(oh);
    }

    std::set<uint32_t> installed = {123};
    auto left = installs::launcher_leftovers(root, installed);
    std::set<std::string> names;
    uint64_t total = 0;
    for (auto& l : left) { names.insert(l.name); total += l.bytes; }
    CHECK(names == (std::set<std::string>{"boot.prev.log", "launch_probe.previous.log", "frametimes.csv", "appinfo_456.vdf",
                                          "storecache\\300.json"}));
    CHECK(total > 0);

    uint64_t freed = installs::clean_leftovers(root, installed);
    CHECK(freed == total);
    CHECK(!exists(L"boot.prev.log") && !exists(L"launch_probe.previous.log") && !exists(L"frametimes.csv"));
    CHECK(!exists(L"appinfo_456.vdf") && !exists(L"storecache\\300.json"));
    CHECK(exists(L"storecache\\100.json") && exists(L"storecache\\200.json"));   // fresh: the detail page still reads them offline
    CHECK(exists(L"appinfo_123.vdf") && exists(L"appinfo_78a.vdf") && exists(L"appinfox.vdf"));
    CHECK(exists(L"dl_123.done") && exists(L"dl_456.partial") && exists(L"steam_session.bin") && exists(L"storecache\\notes.txt"));
    CHECK(installs::launcher_leftovers(root, installed).empty());
    CHECK(installs::clean_leftovers(root, installed) == 0);

    // a game no longer installed: its appinfo cache becomes a leftover
    CHECK(installs::launcher_leftovers(root, {}).size() == 1 && installs::launcher_leftovers(root, {})[0].name == "appinfo_123.vdf");

    // list_roots creates the folder it lists: remove the ones this run made
    for (size_t i = 1; i < roots.size(); i++) RemoveDirectoryW(std::wstring(roots[i].root.begin(), roots[i].root.end()).c_str());

    if (failures) { std::printf("STORAGE-REPORT FAIL: %d check(s)\n", failures); return 1; }
    std::printf("STORAGE-REPORT PASS: three free-space figures read, %zu install locations named, launcher leftovers counted and removed\n", roots.size());
    return 0;
}
