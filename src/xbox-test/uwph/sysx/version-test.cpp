// Off-device operation proof for all three game-imported version exports.
#include <windows.h>
#include <winver.h>
#include <cstdio>
#include <cstring>
#include <vector>
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s error=%lu\n", __LINE__, #x, GetLastError()); return 1; } } while (0)
struct Route { const char* dll; const char* kind; };
static const Route routes[] = {
#include "../../app/host/mlp_routes.h"
};
int main() {
    bool system = false;
    for (const auto& route : routes)
        if (!_stricmp(route.dll, "version.dll")) system = !strcmp(route.kind, "system");
    CHECK(system);
    HMODULE module = LoadLibraryExW(L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(module);
    auto size = reinterpret_cast<decltype(&GetFileVersionInfoSizeA)>(GetProcAddress(module, "GetFileVersionInfoSizeA"));
    auto read = reinterpret_cast<decltype(&GetFileVersionInfoA)>(GetProcAddress(module, "GetFileVersionInfoA"));
    auto query = reinterpret_cast<decltype(&VerQueryValueA)>(GetProcAddress(module, "VerQueryValueA"));
    CHECK(size && read && query);
    char filename[MAX_PATH];
    DWORD filename_length = GetModuleFileNameA(nullptr, filename, MAX_PATH);
    CHECK(filename_length > 0 && filename_length + sizeof(".missing") <= MAX_PATH);
    DWORD ignored = 0xdeadbeef;
    DWORD bytes = size(filename, &ignored);
    CHECK(bytes > 0 && ignored == 0);
    std::vector<BYTE> data(bytes);
    CHECK(read(filename, 0, bytes, data.data()));
    void* value = nullptr;
    UINT length = 0;
    CHECK(query(data.data(), "\\", &value, &length));
    CHECK(length == sizeof(VS_FIXEDFILEINFO));
    auto fixed = static_cast<const VS_FIXEDFILEINFO*>(value);
    CHECK(fixed->dwSignature == VS_FFI_SIGNATURE);
    CHECK(fixed->dwFileVersionMS == MAKELONG(8, 7));
    CHECK(fixed->dwFileVersionLS == MAKELONG(10, 9));
    CHECK(fixed->dwProductVersionMS == MAKELONG(12, 11));
    CHECK(fixed->dwProductVersionLS == MAKELONG(14, 13));
    CHECK(fixed->dwFileType == VFT_APP);
    CHECK(query(data.data(), "\\VarFileInfo\\Translation", &value, &length));
    CHECK(length == 2 * sizeof(WORD));
    auto translation = static_cast<const WORD*>(value);
    CHECK(translation[0] == 0x0409 && translation[1] == 1200);
    CHECK(query(data.data(), "\\StringFileInfo\\040904b0\\FileDescription", &value, &length));
    CHECK(!strcmp(static_cast<const char*>(value), "sysx version regression"));
    CHECK(length == strlen("sysx version regression") + 1);
    CHECK(query(data.data(), "\\StringFileInfo\\040904b0\\FileVersion", &value, &length));
    CHECK(!strcmp(static_cast<const char*>(value), "7.8.9.10"));
    length = 0xdead;
    CHECK(!query(data.data(), "\\StringFileInfo\\040904b0\\Absent", &value, &length));
    CHECK(length == 0);
    // A sibling beneath the runner's unique temporary directory cannot exist.
    std::strcat(filename, ".missing");
    SetLastError(0xdead);
    CHECK(size(filename, &ignored) == 0);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    SetLastError(0xdead);
    CHECK(!read(filename, 0, bytes, data.data()));
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    CHECK(FreeLibrary(module));
    std::puts("PASS version: fixed versions, translations, ANSI strings, absent value and missing file errors");
    return 0;
}
