#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    using Function = int (WINAPI*)(HGDIOBJ, int, void*);
    auto call = reinterpret_cast<Function>(gdi32_core::resolve("GDI32.DLL", "GetObjectA"));
    HMODULE oracle = LoadLibraryW(L"gdi32.dll");
    CHECK(call && oracle);
    auto reference = reinterpret_cast<Function>(GetProcAddress(oracle, "GetObjectA"));
    CHECK(reference);
    unsigned cases = 0;
    for (UINT_PTR value : {UINT_PTR(0), UINT_PTR(1), UINT_PTR(-1), UINT_PTR(0x123456789ULL)})
    for (int size : {-1, 0, 1, 16, 60, 0x7fffffff})
    for (bool null_output : {false, true}) {
        unsigned char buffer[128], expected[128];
        memset(buffer, 0xa5, sizeof(buffer));
        memcpy(expected, buffer, sizeof(buffer));
        auto handle = reinterpret_cast<HGDIOBJ>(value);
        SetLastError(0xdead);
        int result = reference(handle, size, null_output ? nullptr : expected);
        CHECK(result == 0 && GetLastError() == 0xdead);
        CHECK(call(handle, size, null_output ? nullptr : buffer) == result);
        CHECK(GetLastError() == 0xdead);
        CHECK(!memcmp(buffer, expected, sizeof(buffer)));
        for (unsigned char byte : buffer) CHECK(byte == 0xa5);
        ++cases;
    }
    CHECK(!gdi32_core::resolve(nullptr, "GetObjectA"));
    CHECK(!gdi32_core::resolve("gdi32.dll", nullptr));
    CHECK(!gdi32_core::resolve("user32.dll", "GetObjectA"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "getobjecta"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "#1"));
    CHECK(!gdi32_core::resolve("gdi32.dll", "CreateBitmap"));
    FreeLibrary(oracle);
    std::printf("GDI32 OFFDEVICE PASS %u invalid-object cases, Wine differential, output guards, last error, resolver isolation\n", cases);
}
