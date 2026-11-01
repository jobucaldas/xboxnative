#include "route.h"
#include <cstdio>
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    using Fn = WCHAR **(WINAPI *)(const WCHAR *, int *);
    auto candidate = reinterpret_cast<Fn>(shell32_core::resolve("SHELL32.DLL", "CommandLineToArgvW"));
    HMODULE module = LoadLibraryW(L"shell32.dll");
    auto oracle = reinterpret_cast<Fn>(GetProcAddress(module, "CommandLineToArgvW"));
    CHECK(candidate && oracle);
    CHECK(!shell32_core::resolve(nullptr, "CommandLineToArgvW"));
    CHECK(!shell32_core::resolve("shell32.dll", nullptr));
    CHECK(!shell32_core::resolve("shlwapi.dll", "CommandLineToArgvW"));
    CHECK(!shell32_core::resolve("shell32.dll", "commandlinetoargvw"));
    CHECK(!shell32_core::resolve("shell32.dll", "ShellExecuteExW"));
    for (Fn fn : {candidate, oracle}) {
        SetLastError(0xdead);
        CHECK(!fn(L"game.exe", nullptr));
        CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    }
    int count = 123;
    SetLastError(0xdead);
    CHECK(!candidate(L"", &count));
    CHECK(count == 123 && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
    shell32_core::configure_image_path(L"C:\\games\\Megabonk\\Megabonk.exe");
    SetLastError(0xdead);
    auto empty = candidate(L"", &count);
    CHECK(empty && count == 1 && !empty[1]);
    CHECK(!wcscmp(empty[0], L"C:\\games\\Megabonk\\Megabonk.exe"));
    CHECK(GetLastError() == 0xdead);
    CHECK(!LocalFree(empty));
    std::wstring long_path = L"C:\\" + std::wstring(1024, L'x') + L"\\game.exe";
    shell32_core::configure_image_path(long_path.c_str());
    empty = candidate(L"", &count);
    CHECK(empty && count == 1 && !empty[1]);
    CHECK(!wcscmp(empty[0], long_path.c_str()));
    CHECK(!LocalFree(empty));
    shell32_core::configure_image_path(nullptr);
    count = 321;
    CHECK(!candidate(L"", &count));
    CHECK(count == 321 && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
    unsigned seed = 0x321abcd, cases = 0;
    const WCHAR alphabet[] = L"ab \\\"\t\n\u00e3";
    for (unsigned n = 0; n != 12000; ++n) {
        std::wstring input;
        // Deterministic lengths and quote/backslash-heavy strings exercise
        // argv[0]'s special grammar separately from ordinary argument rules.
        for (unsigned j = 0; j != 1 + n % 96; ++j) {
            seed = seed * 1664525u + 1013904223u;
            input += alphabet[(seed >> 16) % (sizeof(alphabet)/sizeof(WCHAR)-1)];
        }
        int ac = -1, bc = -1;
        SetLastError(0xdead);
        auto a = candidate(input.c_str(), &ac);
        DWORD ae = GetLastError();
        SetLastError(0xdead);
        auto b = oracle(input.c_str(), &bc);
        DWORD be = GetLastError();
        CHECK(a && b && ac == bc && ae == be);
        CHECK(!a[ac] && !b[bc]);
        for (int i = 0; i < ac; ++i) CHECK(!wcscmp(a[i], b[i]));
        CHECK(!LocalFree(a) && !LocalFree(b));
        ++cases;
    }
    FreeLibrary(module);
    printf("SHELL32 OFFDEVICE PASS %u Wine parser comparisons, errors, LocalFree ownership, guest empty-input identity, resolver isolation\n", cases);
}
