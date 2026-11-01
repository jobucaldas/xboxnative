#include "route.h"
#include <shellapi.h>
#include <type_traits>
#include <cstdio>
#include <cstdlib>
static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    static_assert(std::is_same<decltype(&ShellExecuteW), decltype(&shell32_core::execute)>::value);
    auto call = reinterpret_cast<decltype(&ShellExecuteW)>(shell32_core::resolve("SHELL32.DLL", "ShellExecuteW"));
    check(call && !shell32_core::resolve("kernel32.dll", "ShellExecuteW"));
    check(!shell32_core::resolve("shell32.dll", "ShellExecuteA"));
    check(!shell32_core::resolve(nullptr, "ShellExecuteW"));
    check(!shell32_core::resolve("shell32.dll", nullptr));
    const WCHAR *poison = reinterpret_cast<const WCHAR *>(1);
    const WCHAR *verbs[] = {nullptr, L"open", L"runas", poison};
    const WCHAR *files[] = {nullptr, L"", L"game.exe", L"https://example.invalid/", poison};
    int count = 0;
    for (auto verb : verbs) for (auto file : files) for (int show : {-1, SW_HIDE, SW_SHOWNORMAL, 0x7fffffff}) {
        SetLastError(0x12345678);
        auto result = call(reinterpret_cast<HWND>(INT_PTR(-1)), verb, file, poison, poison, show);
        check(reinterpret_cast<INT_PTR>(result) == SE_ERR_NOASSOC);
        check(GetLastError() == ERROR_NO_ASSOCIATION);
        ++count;
    }
    printf("PASS: %d unavailable shell isolation cases; exact ABI and resolver\n", count);
}
