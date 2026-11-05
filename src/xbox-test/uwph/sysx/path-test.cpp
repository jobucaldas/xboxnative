// Off-device operation proof for the two imported SHLWAPI native-tail routes.
#include <windows.h>
#include <shlwapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "../../shared/pe_tail.h"

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s error=%lu\n", __LINE__, #x, GetLastError()); return 1; } } while (0)
int main() {
    auto canonicalize = reinterpret_cast<decltype(&PathCanonicalizeW)>(
        pe_tail::resolve("shlwapi.dll", "PathCanonicalizeW"));
    auto remove = reinterpret_cast<decltype(&SHDeleteKeyW)>(
        pe_tail::resolve("shlwapi.dll", "SHDeleteKeyW"));
    CHECK(canonicalize && remove);
    HMODULE module = GetModuleHandleW(L"shlwapi.dll");
    CHECK(reinterpret_cast<FARPROC>(canonicalize) == GetProcAddress(module, "PathCanonicalizeW"));
    CHECK(reinterpret_cast<FARPROC>(remove) == GetProcAddress(module, "SHDeleteKeyW"));
    CHECK(!pe_tail::resolve("shlwapi.dll", "PathCanonicalizeA"));
    struct { const wchar_t* input; const wchar_t* expected; } cases[] = {
        {L"C:\\game\\data\\..\\.\\assets", L"C:\\game\\assets"},
        {L"C:\\..\\..\\game", L"C:\\game"},
        {L"\\\\server\\share\\data\\..\\assets", L"\\\\server\\share\\assets"},
        {L"assets\\.\\textures\\..\\mesh", L"assets\\mesh"},
    };
    for (const auto& item : cases) {
        wchar_t output[MAX_PATH]{};
        CHECK(canonicalize(output, item.input));
        CHECK(!wcscmp(output, item.expected));
    }
    wchar_t output[MAX_PATH] = L"sentinel";
    SetLastError(0xdead);
    CHECK(!canonicalize(output, nullptr));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CHECK(!output[0]);
    CHECK(canonicalize(output, L""));
    CHECK(!wcscmp(output, L"\\"));
    SetLastError(0xdead);
    CHECK(!canonicalize(nullptr, L"C:\\game"));
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    std::puts("PASS SHLWAPI path: absolute, UNC, relative, root clamp, empty path, null arguments");

    // All mutation stays beneath a unique volatile per-process HKCU key.
    wchar_t path[128];
    swprintf(path, 128, L"Software\\sysx-path-regression-%lu", GetCurrentProcessId());
    HKEY root = nullptr, child = nullptr;
    DWORD disposition = 0;
    CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, REG_OPTION_VOLATILE,
        KEY_ALL_ACCESS, nullptr, &root, &disposition) == ERROR_SUCCESS);
    CHECK(disposition == REG_CREATED_NEW_KEY);
    CHECK(RegCreateKeyExW(root, L"target\\nested", 0, nullptr, REG_OPTION_VOLATILE,
        KEY_ALL_ACCESS, nullptr, &child, nullptr) == ERROR_SUCCESS);
    const DWORD value = 42;
    CHECK(RegSetValueExW(child, L"value", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS);
    CHECK(RegCloseKey(child) == ERROR_SUCCESS);
    CHECK(RegCreateKeyExW(root, L"sibling", 0, nullptr, REG_OPTION_VOLATILE,
        KEY_ALL_ACCESS, nullptr, &child, nullptr) == ERROR_SUCCESS);
    CHECK(RegCloseKey(child) == ERROR_SUCCESS);
    CHECK(remove(root, L"target") == ERROR_SUCCESS);
    CHECK(RegOpenKeyExW(root, L"target", 0, KEY_READ, &child) == ERROR_FILE_NOT_FOUND);
    CHECK(remove(root, L"target") == ERROR_FILE_NOT_FOUND);
    CHECK(RegOpenKeyExW(root, L"sibling", 0, KEY_READ, &child) == ERROR_SUCCESS);
    CHECK(RegCloseKey(child) == ERROR_SUCCESS);
    CHECK(RegCloseKey(root) == ERROR_SUCCESS);
    CHECK(remove(HKEY_CURRENT_USER, path) == ERROR_SUCCESS);
    CHECK(RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &root) == ERROR_FILE_NOT_FOUND);
    std::puts("PASS SHLWAPI registry: recursive deletion, missing key error, sibling isolation, cleanup");
    return 0;
}
