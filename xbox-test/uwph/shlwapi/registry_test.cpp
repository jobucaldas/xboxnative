#include "route.h"
#include <shlwapi.h>
#include <type_traits>
#include <cstdio>
#include <cstdlib>

static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    static_assert(std::is_same<decltype(&SHDeleteKeyW), decltype(&shlwapi_core::delete_key)>::value);
    auto call = reinterpret_cast<decltype(&SHDeleteKeyW)>(shlwapi_core::resolve("SHLWAPI.DLL", "SHDeleteKeyW"));
    check(call && !shlwapi_core::resolve("kernel32.dll", "SHDeleteKeyW"));
    check(!shlwapi_core::resolve("shlwapi.dll", "SHDeleteKeyA"));
    check(!shlwapi_core::resolve(nullptr, "SHDeleteKeyW"));
    check(!shlwapi_core::resolve("shlwapi.dll", nullptr));
    HKEY keys[] = {nullptr, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE,
                   reinterpret_cast<HKEY>(ULONG_PTR(1)),
                   reinterpret_cast<HKEY>(ULONG_PTR(-1))};
    const WCHAR *paths[] = {nullptr, L"", L"Software", L"Software\\Guest\\Child",
                            reinterpret_cast<const WCHAR *>(ULONG_PTR(1))};
    DWORD errors[] = {0, ERROR_ACCESS_DENIED, 0x12345678, 0xffffffff};
    int count = 0;
    for (auto key : keys) for (auto path : paths) for (auto error : errors) {
        SetLastError(error);
        check(call(key, path) == ERROR_CALL_NOT_IMPLEMENTED);
        check(GetLastError() == error);
        ++count;
    }
    puts("PASS: SHDeleteKeyW exact SDK ABI and resolver");
    printf("PASS: %d unavailable registry cases; last error preserved; poison pointers unread\n", count);
}
