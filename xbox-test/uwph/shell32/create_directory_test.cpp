#include "route.h"
#include <shlobj.h>
#include <type_traits>
#include <cstdio>
#include <cstdlib>

static void check(bool ok) { if (!ok) std::abort(); }
int main() {
    static_assert(std::is_same<decltype(&SHCreateDirectoryExW),
                  decltype(&shell32_core::create_directory)>::value);
    auto call = reinterpret_cast<decltype(&SHCreateDirectoryExW)>(
        shell32_core::resolve("SHELL32.DLL", "SHCreateDirectoryExW"));
    check(call && !shell32_core::resolve("user32.dll", "SHCreateDirectoryExW"));
    check(!shell32_core::resolve(nullptr, "SHCreateDirectoryExW"));
    check(!shell32_core::resolve("shell32.dll", nullptr));
    auto module = LoadLibraryW(L"shell32.dll");
    auto reference = reinterpret_cast<decltype(call)>(GetProcAddress(module, "SHCreateDirectoryExW"));
    check(reference != nullptr);
    const WCHAR *relative[] = {nullptr, L"", L"a", L".", L"..", L"a\\b", L"/tmp/u32m", L"abc:def"};
    HWND owners[] = {nullptr, reinterpret_cast<HWND>(ULONG_PTR(1))};
    auto poison = reinterpret_cast<SECURITY_ATTRIBUTES *>(ULONG_PTR(1));
    int comparisons = 0;
    for (auto owner : owners) for (auto path : relative) {
        SetLastError(0x12345678);
        int expected = reference(owner, path, poison);
        DWORD error = GetLastError();
        SetLastError(0x12345678);
        check(call(owner, path, poison) == expected);
        check(GetLastError() == error && error == ERROR_BAD_PATHNAME);
        ++comparisons;
    }
    // No host call for absolute paths: these must never create a directory.
    const WCHAR *absolute[] = {L"C:\\u32m-must-not-create", L"C:relative", L"\\rooted", L"\\\\server\\share", L"?:odd"};
    int failures = 0;
    for (auto owner : owners) for (auto path : absolute) {
        SetLastError(0x12345678);
        check(call(owner, path, poison) == ERROR_CALL_NOT_IMPLEMENTED);
        check(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
        ++failures;
    }
    printf("PASS: exact SDK ABI/resolver; %d Wine relative-path comparisons; %d isolated absolute-path failures\n", comparisons, failures);
}
