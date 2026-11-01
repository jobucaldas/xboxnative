#include "route.h"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    using Function = void (WINAPI*)(GUID*);
    auto call = reinterpret_cast<Function>(hid_core::resolve("HID.DLL", "HidD_GetHidGuid"));
    HMODULE oracle = LoadLibraryW(L"hid.dll");
    CHECK(oracle && call);
    auto wine_call = reinterpret_cast<Function>(GetProcAddress(oracle, "HidD_GetHidGuid"));
    CHECK(wine_call);
    struct Guarded { DWORD before; GUID guid; DWORD after; };
    static_assert(sizeof(GUID) == 16, "GUID ABI");
    Guarded value{0x12345678, {}, 0x87654321};
    GUID expected{};
    SetLastError(0xdead);
    wine_call(&expected);
    CHECK(GetLastError() == 0xdead);
    call(&value.guid);
    CHECK(GetLastError() == 0xdead);
    CHECK(!memcmp(&value.guid, &expected, sizeof(GUID)));
    CHECK(value.before == 0x12345678 && value.after == 0x87654321);
    CHECK(value.guid.Data1 == 0x4d1e55b2 && value.guid.Data2 == 0xf16f && value.guid.Data3 == 0x11cf);
    CHECK(!hid_core::resolve(nullptr, "HidD_GetHidGuid"));
    CHECK(!hid_core::resolve("hid.dll", nullptr));
    CHECK(!hid_core::resolve("user32.dll", "HidD_GetHidGuid"));
    CHECK(!hid_core::resolve("hid.dll", "hidd_gethidguid"));
    CHECK(!hid_core::resolve("hid.dll", "#1"));
    CHECK(!hid_core::resolve("hid.dll", "HidD_FreePreparsedData"));
    const char* queries[] = {"HidD_GetAttributes", "HidD_GetPreparsedData"};
    HANDLE handles[] = {nullptr, INVALID_HANDLE_VALUE, reinterpret_cast<HANDLE>(0x1234)};
    using Query = BOOLEAN (WINAPI*)(HANDLE, void*);
    unsigned cases = 0;
    for (const char* name : queries) {
        auto route = reinterpret_cast<Query>(hid_core::resolve("hid.dll", name));
        auto reference = reinterpret_cast<Query>(GetProcAddress(oracle, name));
        CHECK(route && reference);
        for (HANDLE handle : handles) for (bool null_output : {false, true}) {
            unsigned char actual[64], expected_bytes[64];
            memset(actual, 0xa5, sizeof(actual));
            memcpy(expected_bytes, actual, sizeof(actual));
            SetLastError(0xdead);
            BOOLEAN expected_result = reference(handle, null_output ? nullptr : expected_bytes);
            DWORD error = GetLastError();
            SetLastError(0xdead);
            CHECK(route(handle, null_output ? nullptr : actual) == expected_result);
            CHECK(GetLastError() == error && error == ERROR_INVALID_HANDLE);
            CHECK(!memcmp(actual, expected_bytes, sizeof(actual)));
            for (unsigned char byte : actual) CHECK(byte == 0xa5);
            ++cases;
        }
    }
    CHECK(!hid_core::resolve("hid.dll", "HidD_GetProductString"));
    FreeLibrary(oracle);
    std::printf("HID OFFDEVICE PASS GUID, %u query cases, WINAPI resolver, Wine differential, buffer guards, last error, isolation\n", cases);
}
