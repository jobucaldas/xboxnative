#include "route.h"
#include <shlobj.h>
#include <type_traits>
#include <cstdio>
#include <cstdlib>
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    static_assert(std::is_same<decltype(&SHGetFolderPathW), decltype(&shell32_core::folder_path)>::value);
    static_assert(std::is_same<decltype(&SHGetKnownFolderPath), decltype(&shell32_core::known_folder_path)>::value);
    auto old = reinterpret_cast<decltype(&SHGetFolderPathW)>(shell32_core::resolve("SHELL32.DLL", "SHGetFolderPathW"));
    auto known = reinterpret_cast<decltype(&SHGetKnownFolderPath)>(shell32_core::resolve("shell32.dll", "SHGetKnownFolderPath"));
    check(old && known);
    check(!shell32_core::resolve("kernel32.dll", "SHGetFolderPathW"));
    check(!shell32_core::resolve("shell32.dll", "SHGetFolderPathA"));
    int count = 0;
    const GUID id = {0xdeadbeef, 1, 2, {3,4,5,6,7,8,9,10}};
    for (DWORD error : {DWORD(0), DWORD(0x12345678), DWORD(0xffffffff)})
    for (DWORD flags : {DWORD(0), DWORD(1), DWORD(0xffffffff)})
    for (int folder : {-1, 0, CSIDL_APPDATA, CSIDL_PERSONAL, 0x7fffffff}) {
        WCHAR buffer[] = L"stale";
        SetLastError(error);
        check(old(reinterpret_cast<HWND>(1), folder, reinterpret_cast<HANDLE>(1), flags, buffer) == E_NOTIMPL);
        check(buffer[0] == 0 && buffer[1] == L't' && GetLastError() == error);
        check(old(nullptr, folder, nullptr, flags, nullptr) == E_NOTIMPL);
        WCHAR *out = reinterpret_cast<WCHAR *>(1);
        check(known(id, flags, reinterpret_cast<HANDLE>(1), &out) == E_NOTIMPL);
        check(!out && GetLastError() == error);
        check(known(id, flags, nullptr, nullptr) == E_INVALIDARG);
        check(GetLastError() == error);
        ++count;
    }
    printf("PASS: %d folder query failure/output/isolation cases; exact SDK ABI and routes\n", count);
}
