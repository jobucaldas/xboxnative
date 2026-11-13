// SPDX-License-Identifier: GPL-3.0-or-later
// special_folder.h against the SDK's shlobj.h prototype (off-device, Wine).
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <cwchar>
#include <initializer_list>
#include <type_traits>
#include "special_folder.h"
static_assert(std::is_same<decltype(&shell_folder::getSpecialFolderPathW), decltype(&SHGetSpecialFolderPathW)>::value,
              "SHGetSpecialFolderPathW signature");
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
int main() {
    // No guest folder provider: FALSE, and the buffer is emptied for every folder id.
    WCHAR path[MAX_PATH];
    for (int csidl : {CSIDL_PERSONAL, CSIDL_LOCAL_APPDATA, CSIDL_APPDATA}) {
        wcscpy(path, L"stale");
        CHECK(!shell_folder::getSpecialFolderPathW(nullptr, path, csidl, FALSE));
        CHECK(path[0] == 0);
        wcscpy(path, L"stale");
        CHECK(!shell_folder::getSpecialFolderPathW(nullptr, path, csidl, TRUE));
        CHECK(path[0] == 0);
    }
    CHECK(!shell_folder::getSpecialFolderPathW(nullptr, nullptr, CSIDL_PERSONAL, FALSE));
    if (failures) { std::printf("SHELL32 FOLDER TEST FAIL checks=%d failures=%d\n", checks, failures); return 1; }
    std::printf("SHELL32 FOLDER TEST PASS checks=%d\n", checks);
    return 0;
}
