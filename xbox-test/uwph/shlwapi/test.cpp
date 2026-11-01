#include "route.h"
#include <cstdio>
#include <cwchar>
#include <string>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    using Fn = BOOL (WINAPI*)(WCHAR*, const WCHAR*);
    auto module = LoadLibraryW(L"shlwapi.dll");
    CHECK(module);
    auto reference = reinterpret_cast<Fn>(GetProcAddress(module, "PathCanonicalizeW"));
    auto candidate = reinterpret_cast<Fn>(shlwapi_core::resolve("SHLWAPI.DLL", "PathCanonicalizeW"));
    CHECK(reference && candidate);
    CHECK(!shlwapi_core::resolve("shell32.dll", "PathCanonicalizeW"));
    CHECK(!shlwapi_core::resolve("shlwapi.dll", "SHDeleteKeyA"));
    CHECK(!shlwapi_core::resolve(nullptr, "PathCanonicalizeW"));
    CHECK(!shlwapi_core::resolve("shlwapi.dll", nullptr));
    WCHAR actual[1024], expected[1024];
    const WCHAR* cases[] = {nullptr, L"", L"C:", L"C:\\", L"C:\\a\\..\\b",
        L"\\\\server\\share\\..\\file", L"\\\\server\\share\\a\\..\\file",
        L"..\\a", L"a\\..\\b", L"a/../b", L".\\a\\.\\b", L"C:foo\\..\\bar",
        L"\\..\\..\\a", L"C:\\a\\...\\b", L"C:\\a\\..", L"a\\\\b", L"a\\."
    };
    for (auto path : cases) {
        SetLastError(0x1234); BOOL a = candidate(actual, path); DWORD ae = GetLastError();
        SetLastError(0x1234); BOOL b = reference(expected, path); DWORD be = GetLastError();
        CHECK(a == b && ae == be && !wcscmp(actual, expected));
        if (path) {
            wcscpy(actual, path); wcscpy(expected, path);
            CHECK(candidate(actual, actual) == reference(expected, expected));
            CHECK(!wcscmp(actual, expected));
        }
    }
    SetLastError(0);
    CHECK(!candidate(nullptr, L"x") && GetLastError() == ERROR_INVALID_PARAMETER);
    // Deterministic differential corpus exercises odd colon/dot/separator combinations.
    unsigned state = 53;
    const WCHAR alphabet[] = L"abc.:\\/ ";
    for (unsigned i = 0; i < 10000; ++i) {
        std::wstring path;
        state = state * 1664525u + 1013904223u;
        unsigned length = state % 200;
        for (unsigned j = 0; j < length; ++j) {
            state = state * 1664525u + 1013904223u;
            path += alphabet[(state >> 16) % 8];
        }
        SetLastError(0x1234); BOOL a = candidate(actual, path.c_str()); DWORD ae = GetLastError();
        SetLastError(0x1234); BOOL b = reference(expected, path.c_str()); DWORD be = GetLastError();
        CHECK(a == b && ae == be && !wcscmp(actual, expected));
    }
    FreeLibrary(module);
    std::puts("SHLWAPI OFFDEVICE PASS resolver, errors, roots, UNC, aliasing, 10000 differential paths");
}
