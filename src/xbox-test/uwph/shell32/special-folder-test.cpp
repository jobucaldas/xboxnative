// SPDX-License-Identifier: GPL-3.0-or-later
// special_folder.h parity with the sibling known-folder route (off-device, Wine).
// The reference is the same SHGetKnownFolderPath the guest's folder calls use; the
// table and every constant come from the real SDK headers.
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <cstdio>
#include <initializer_list>
#include <cwchar>
#include <cstring>
#include <type_traits>
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
int main() {
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
            if (mapped[i].csidl == row.csidl) { found = true; CHECK(std::memcmp(&mapped[i].folder, row.sdk, sizeof(GUID)) == 0); }
        CHECK(found);
    }
    for (const Row& row : rows) {
        // Sibling answer: SHGetKnownFolderPath, with and without KF_FLAG_CREATE.
        for (DWORD flags : {DWORD(0), DWORD(KF_FLAG_CREATE)}) {
            PWSTR expected = nullptr;
            bool sibling = SUCCEEDED(SHGetKnownFolderPath(*row.sdk, flags, nullptr, &expected)) && expected;
            WCHAR got[MAX_PATH] = L"stale";
            WCHAR real[MAX_PATH] = L"stale";
            int csidl = row.csidl | (flags ? CSIDL_FLAG_CREATE : 0);
            CHECK(sibling);
            CHECK(shell_folder::getSpecialFolderPathW(nullptr, got, csidl, flags != 0) == TRUE);
            CHECK(sibling && std::wcscmp(got, expected) == 0);
            // The platform's own SHGetSpecialFolderPathW agrees with the sibling.
            CHECK(SHGetSpecialFolderPathW(nullptr, real, csidl, flags != 0) == TRUE);
            CHECK(std::wcscmp(real, got) == 0);
            if (expected) CoTaskMemFree(expected);
        }
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
