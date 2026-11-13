// SPDX-License-Identifier: GPL-3.0-or-later
// special_folder.h and folder_chain.h (off-device, Wine). Parity: the reference is the
// same SHGetKnownFolderPath the guest's folder calls use; constants and KNOWNFOLDERIDs
// come from the real SDK headers. Fallbacks: fake providers that fail the known folder.
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "special_folder.h"
static_assert(std::is_same<decltype(&shell_folder::getSpecialFolderPathW), decltype(&SHGetSpecialFolderPathW)>::value,
              "SHGetSpecialFolderPathW signature");
static_assert(shell_folder::csidl_personal == CSIDL_PERSONAL && shell_folder::csidl_appdata == CSIDL_APPDATA &&
              shell_folder::csidl_local_appdata == CSIDL_LOCAL_APPDATA && shell_folder::csidl_desktopdirectory == CSIDL_DESKTOPDIRECTORY &&
              shell_folder::csidl_common_appdata == CSIDL_COMMON_APPDATA && shell_folder::csidl_desktop == CSIDL_DESKTOP,
              "CSIDL values");
static_assert(shell_folder::kf_flag_create == KF_FLAG_CREATE, "KF_FLAG_CREATE value");
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
struct Row { int csidl; const KNOWNFOLDERID* sdk; };
static bool isGuid(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }
// Fake providers: the known folder fails for every id, the environment and package
// folders answer what the test sets. A known answer is injected per test as needed.
struct Fake {
    bool knownFails = true;
    const GUID* knownAnswerFor = nullptr;   // when set, only this id answers (with knownPath)
    std::wstring knownPath;
    std::wstring appdata, userprofile, allusers;
    std::wstring roaming, localCache, local;   // empty: the package lookup fails
};
static folder_chain::Providers providersFor(const Fake& f) {
    folder_chain::Providers p;
    p.known = [&f](const GUID& id, DWORD, std::wstring& out) {
        if (f.knownFails || !f.knownAnswerFor || !isGuid(id, *f.knownAnswerFor)) return false;
        out = f.knownPath; return true;
    };
    p.env = [&f](const wchar_t* name) -> std::wstring {
        if (!std::wcscmp(name, L"APPDATA")) return f.appdata;
        if (!std::wcscmp(name, L"USERPROFILE")) return f.userprofile;
        if (!std::wcscmp(name, L"ALLUSERSPROFILE")) return f.allusers;
        return L"";
    };
    p.package = [&f](folder_chain::Package kind, std::wstring& out) {
        const std::wstring& base = kind == folder_chain::Package::Roaming ? f.roaming
                                 : kind == folder_chain::Package::LocalCache ? f.localCache : f.local;
        if (base.empty()) return false;
        out = base; return true;
    };
    return p;
}
// Golden check for cloud_roots: the roaming block as it was written inline (reference),
// against the shared chain with the glue launch.h now uses. Every combination of known
// folder, APPDATA and package outcomes (fail, empty answer, path) must give the same
// WinAppDataRoaming value and the same log lines.
namespace golden {
enum class Pkg { Fail, Empty, Path };
struct Case { int known; const wchar_t* knownPath; const wchar_t* appdata; Pkg roaming, cache, local; };
static const wchar_t* pkgBase(Pkg s, const wchar_t* path) { return s == Pkg::Path ? path : L""; }
static bool pkgOk(Pkg s) { return s != Pkg::Fail; }
static std::pair<std::map<std::string, std::wstring>, std::vector<std::string>> reference(const Case& c) {
    std::map<std::string, std::wstring> roots; std::vector<std::string> logs;
    // known: 0 fail, 1 success with the path (possibly empty)
    if (c.known) roots["WinAppDataRoaming"] = c.knownPath;
    auto tryRoaming = [&](const char* what, std::wstring p) {
        if (roots.count("WinAppDataRoaming") || p.empty()) return;
        roots["WinAppDataRoaming"] = p;
        logs.push_back(std::string("STEAM cloud: WinAppDataRoaming from ") + what + " = " + std::string(p.begin(), p.end()));
    };
    if (!roots.count("WinAppDataRoaming")) {
        if (c.appdata && *c.appdata) tryRoaming("the APPDATA environment variable", c.appdata);
        if (pkgOk(c.roaming)) tryRoaming("RoamingFolder", pkgBase(c.roaming, L"C:\\pkg\\roam"));
        if (pkgOk(c.cache)) tryRoaming("LocalCache\\Roaming", std::wstring(pkgBase(c.cache, L"C:\\pkg\\cache")) + L"\\Roaming");
        if (pkgOk(c.local)) tryRoaming("LocalFolder\\..\\RoamingState", std::wstring(pkgBase(c.local, L"C:\\pkg\\local")) + L"\\..\\RoamingState");
        if (!roots.count("WinAppDataRoaming")) logs.push_back("STEAM cloud: no WinAppDataRoaming root; %WinAppDataRoaming% files will be skipped");
    }
    return {roots, logs};
}
static std::pair<std::map<std::string, std::wstring>, std::vector<std::string>> shared(const Case& c) {
    std::map<std::string, std::wstring> roots; std::vector<std::string> logs;
    folder_chain::Providers p;
    p.known = [&](const GUID& id, DWORD, std::wstring& out) {
        if (!c.known || !isGuid(id, FOLDERID_RoamingAppData)) return false;
        out = c.knownPath; return true;
    };
    p.env = [&](const wchar_t* name) -> std::wstring { return std::wcscmp(name, L"APPDATA") ? L"" : (c.appdata ? c.appdata : L""); };
    p.package = [&](folder_chain::Package kind, std::wstring& out) {
        Pkg s = kind == folder_chain::Package::Roaming ? c.roaming : kind == folder_chain::Package::LocalCache ? c.cache : c.local;
        if (!pkgOk(s)) return false;
        out = kind == folder_chain::Package::Roaming ? pkgBase(s, L"C:\\pkg\\roam")
            : kind == folder_chain::Package::LocalCache ? pkgBase(s, L"C:\\pkg\\cache") : pkgBase(s, L"C:\\pkg\\local");
        return true;
    };
    // The glue launch.h cloud_roots uses for the roaming block.
    auto roam = folder_chain::resolve(p, FOLDERID_RoamingAppData, 0, folder_chain::roamingChain());
    if (roam.known) roots["WinAppDataRoaming"] = roam.path;
    else if (!roam.path.empty()) {
        roots["WinAppDataRoaming"] = roam.path;
        logs.push_back(std::string("STEAM cloud: WinAppDataRoaming from ") + roam.what + " = " + std::string(roam.path.begin(), roam.path.end()));
    } else logs.push_back("STEAM cloud: no WinAppDataRoaming root; %WinAppDataRoaming% files will be skipped");
    return {roots, logs};
}
// Returns the number of combinations that differ.
static int compareAll() {
    const wchar_t* knownPaths[] = {L"", L"C:\\known\\roam"};
    const wchar_t* appdatas[] = {nullptr, L"", L"C:\\env\\appdata"};
    const Pkg states[] = {Pkg::Fail, Pkg::Empty, Pkg::Path};
    int differing = 0, total = 0;
    for (int known = 0; known <= 2; ++known)
    for (const wchar_t* kp : knownPaths)
    for (const wchar_t* ad : appdatas)
    for (Pkg r : states) for (Pkg ca : states) for (Pkg lo : states) {
        // known == 0: the known folder fails; 1 and 2: it answers (with an empty and a set path).
        Case c{known ? 1 : 0, known == 2 ? L"C:\\known\\roam" : kp, ad, r, ca, lo};
        ++total;
        if (reference(c) != shared(c)) ++differing;
    }
    std::printf("GOLDEN cloud_roots cases=%d differing=%d\n", total, differing);
    return differing;
}
}  // namespace golden

int main() {
    CHECK(golden::compareAll() == 0);
    // The sibling table: each CSIDL with the SDK's KNOWNFOLDERID it maps to.
    const Row rows[] = {
        {CSIDL_PERSONAL, &FOLDERID_Documents},
        {CSIDL_APPDATA, &FOLDERID_RoamingAppData},
        {CSIDL_LOCAL_APPDATA, &FOLDERID_LocalAppData},
        {CSIDL_DESKTOPDIRECTORY, &FOLDERID_Desktop},
        {CSIDL_COMMON_APPDATA, &FOLDERID_ProgramData},
    };
    size_t count = 0;
    const shell_folder::Mapping* mapped = shell_folder::mappings(count);
    CHECK(count == sizeof rows / sizeof rows[0]);
    for (const Row& row : rows) {
        bool found = false;
        for (size_t i = 0; i < count; ++i)
            if (mapped[i].csidl == row.csidl) { found = true; CHECK(isGuid(mapped[i].folder, *row.sdk)); }
        CHECK(found);
    }
    // Parity with the sibling known-folder route on this platform.
    for (const Row& row : rows) {
        for (DWORD flags : {DWORD(0), DWORD(KF_FLAG_CREATE)}) {
            PWSTR expected = nullptr;
            bool sibling = SUCCEEDED(SHGetKnownFolderPath(*row.sdk, flags, nullptr, &expected)) && expected;
            WCHAR got[MAX_PATH] = L"stale";
            WCHAR real[MAX_PATH] = L"stale";
            int csidl = row.csidl | (flags ? CSIDL_FLAG_CREATE : 0);
            CHECK(sibling);
            CHECK(shell_folder::getSpecialFolderPathW(nullptr, got, csidl, flags != 0) == TRUE);
            CHECK(sibling && std::wcscmp(got, expected) == 0);
            CHECK(SHGetSpecialFolderPathW(nullptr, real, csidl, flags != 0) == TRUE);
            CHECK(std::wcscmp(real, got) == 0);
            if (expected) CoTaskMemFree(expected);
        }
    }
    // The known folder wins over every fallback, even when a fallback is set.
    {
        Fake f; f.knownFails = false; f.knownAnswerFor = &FOLDERID_RoamingAppData; f.knownPath = L"C:\\known\\roam";
        f.appdata = L"C:\\env\\appdata"; f.roaming = L"C:\\pkg\\roam";
        WCHAR path[MAX_PATH] = L"stale";
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(f), path, CSIDL_APPDATA, FALSE) == TRUE);
        CHECK(std::wcscmp(path, L"C:\\known\\roam") == 0);
    }
    // Known folder for RoamingAppData fails: the APPDATA environment variable answers.
    {
        Fake f; f.appdata = L"C:\\fake\\appdata";
        WCHAR path[MAX_PATH] = L"stale";
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(f), path, CSIDL_APPDATA, FALSE) == TRUE);
        CHECK(std::wcscmp(path, L"C:\\fake\\appdata") == 0);
        // The label cloud_roots logs for this step.
        folder_chain::Found found = folder_chain::resolve(providersFor(f), FOLDERID_RoamingAppData, 0, folder_chain::roamingChain());
        CHECK(!found.known && found.what == "the APPDATA environment variable");
    }
    // No environment variable: the package RoamingFolder answers, then LocalCache\Roaming.
    {
        Fake f; f.roaming = L"C:\\pkg\\roam";
        WCHAR path[MAX_PATH] = L"stale";
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(f), path, CSIDL_APPDATA, FALSE) == TRUE);
        CHECK(std::wcscmp(path, L"C:\\pkg\\roam") == 0);
        Fake g; g.localCache = L"C:\\pkg\\cache";
        WCHAR second[MAX_PATH] = L"stale";
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(g), second, CSIDL_APPDATA, FALSE) == TRUE);
        CHECK(std::wcscmp(second, L"C:\\pkg\\cache\\Roaming") == 0);
    }
    // Other folders use their own fallbacks.
    {
        Fake f; f.userprofile = L"C:\\fake\\user"; f.allusers = L"C:\\fake\\all"; f.local = L"C:\\pkg\\local";
        WCHAR documents[MAX_PATH] = L"stale", desktop[MAX_PATH] = L"stale", common[MAX_PATH] = L"stale", local[MAX_PATH] = L"stale";
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(f), documents, CSIDL_PERSONAL, FALSE) == TRUE);
        CHECK(std::wcscmp(documents, L"C:\\fake\\user\\Documents") == 0);
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(f), desktop, CSIDL_DESKTOPDIRECTORY, FALSE) == TRUE);
        CHECK(std::wcscmp(desktop, L"C:\\fake\\user\\Desktop") == 0);
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(f), common, CSIDL_COMMON_APPDATA, FALSE) == TRUE);
        CHECK(std::wcscmp(common, L"C:\\fake\\all") == 0);
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(f), local, CSIDL_LOCAL_APPDATA, FALSE) == TRUE);
        CHECK(std::wcscmp(local, L"C:\\pkg\\local") == 0);
    }
    // All steps fail: FALSE with an empty buffer, for every mapped CSIDL.
    {
        Fake none;
        for (const Row& row : rows) {
            WCHAR path[MAX_PATH] = L"stale";
            CHECK(shell_folder::getSpecialFolderPathWith(providersFor(none), path, row.csidl, FALSE) == FALSE);
            CHECK(path[0] == 0);
        }
    }
    // fCreate on a fallback path creates the folder; without it the path is only returned.
    {
        WCHAR temp[MAX_PATH];
        CHECK(GetTempPathW(MAX_PATH, temp) > 0);
        std::wstring base = std::wstring(temp) + L"xbn-sf-test";
        CreateDirectoryW(base.c_str(), nullptr);
        std::wstring created = base + L"\\new";
        Fake f; f.appdata = created;
        WCHAR path[MAX_PATH] = L"stale";
        CHECK(shell_folder::getSpecialFolderPathWith(providersFor(f), path, CSIDL_APPDATA, TRUE) == TRUE);
        CHECK(std::wcscmp(path, created.c_str()) == 0);
        DWORD attr = GetFileAttributesW(created.c_str());
        CHECK(attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
        RemoveDirectoryW(created.c_str());
        RemoveDirectoryW(base.c_str());
    }
    // Refused: no file path for CSIDL_DESKTOP (virtual root), unknown ids, and a NULL buffer.
    for (int csidl : {CSIDL_DESKTOP, 0x007f, CSIDL_PROFILE}) {
        WCHAR path[MAX_PATH] = L"stale";
        CHECK(shell_folder::getSpecialFolderPathW(nullptr, path, csidl, FALSE) == FALSE);
        CHECK(path[0] == 0);
    }
    CHECK(shell_folder::getSpecialFolderPathW(nullptr, nullptr, CSIDL_PERSONAL, FALSE) == FALSE);
    if (failures) { std::printf("SHELL32 FOLDER TEST FAIL checks=%d failures=%d\n", checks, failures); return 1; }
    std::printf("SHELL32 FOLDER TEST PASS checks=%d\n", checks);
    return 0;
}
